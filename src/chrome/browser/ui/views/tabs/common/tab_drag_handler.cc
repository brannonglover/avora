// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/common/tab_drag_handler.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>

#include "base/check_deref.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/task/sequenced_task_runner.h"
#include "base/types/to_address.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/avora/avora_favorites.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/ui/views/avora/avora_favorites_view.h"
#include "chrome/browser/ui/views/avora/avora_pinned_section_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_link_drop_handler.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/dragging/drag_session_data.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_types.h"
#include "chrome/browser/ui/views/tabs/tab_slot_view.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/split_tab_collection.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_collection.h"
#include "components/tabs/public/tab_collection_types.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_group_tab_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/class_property.h"
#include "ui/base/clipboard/clipboard_constants.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_target.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/view_utils.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(gfx::Vector2d*)

namespace {

// Stores the offset of the view's initial position to the target position
// it should be, relative to the source dragged view's origin.
DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(gfx::Vector2d, kOffsetAtTabDragStart)

// This is a shim around `TabCollectionNode` to make it compatible with the
// tab dragging logic in `TabDragController`. Longer term, core tab dragging
// logic will be updated to remove the `TabSlotView` dependency, which
// should make this shim view no longer needed.
class DraggedTabSlotView : public TabSlotView {
  METADATA_HEADER(DraggedTabSlotView, TabSlotView)
 public:
  explicit DraggedTabSlotView(const TabCollectionNode& node) : node_(node) {}

  ~DraggedTabSlotView() override = default;
  DraggedTabSlotView(const DraggedTabSlotView&) = delete;
  DraggedTabSlotView& operator=(const DraggedTabSlotView&) = delete;

  TabSlotView::ViewType GetTabSlotViewType() const override {
    switch (node_->type()) {
      case TabCollectionNode::Type::TAB:
        return ViewType::kTab;
      case TabCollectionNode::Type::GROUP:
        return ViewType::kTabGroupHeader;
      default:
        NOTREACHED();
    }
  }

  // Updates the bounds of the slot view to match the bounds of the real view
  // being dragged, skipping animations.
  // Accurate bounds are necessary for the core `TabDragController` to determine
  // the offset from the mouse when detaching into a new window.
  void UpdateBounds() {
    auto* view = node().view();
    CHECK(view->parent());
    gfx::Rect bounds = view->GetLocalBounds();
    bounds.set_size(
        view->GetPreferredSize(views::SizeBounds(view->parent()->size())));
    SetBoundsRect(bounds);
  }

  TabSizeInfo GetTabSizeInfo() const override { return TabSizeInfo(); }

  const TabCollectionNode& node() const { return node_.get(); }

 private:
  const raw_ref<const TabCollectionNode> node_;
};

BEGIN_METADATA(DraggedTabSlotView)
END_METADATA

ui::mojom::DragEventSource EventSourceFromEvent(const ui::LocatedEvent& event) {
  return event.IsGestureEvent() ? ui::mojom::DragEventSource::kTouch
                                : ui::mojom::DragEventSource::kMouse;
}

const TabGroup& TabGroupDataFromNode(const TabCollectionNode& node) {
  CHECK_EQ(node.type(), TabCollectionNode::Type::GROUP);
  const auto* collection = static_cast<const tabs::TabGroupTabCollection*>(
      std::get<tabs::ConstDanglingUntriagedTabCollection>(node.GetNodeData()));
  CHECK(collection);
  const auto* group_data = collection->GetTabGroup();
  CHECK(group_data);
  return *group_data;
}

// Determines the target model range from the container.
gfx::Range GetContainerRange(const TabCollectionNode& container_node,
                             const TabStripModel& tab_strip_model) {
  switch (container_node.type()) {
    case TabCollectionNode::Type::GROUP:
      return TabGroupDataFromNode(container_node).ListTabs();
    case TabCollectionNode::Type::PINNED:
      return gfx::Range(0, tab_strip_model.IndexOfFirstNonPinnedTab());
    case TabCollectionNode::Type::UNPINNED:
      return gfx::Range(tab_strip_model.IndexOfFirstNonPinnedTab(),
                        tab_strip_model.count());
    default:
      NOTREACHED();
  }
}

// Determines the target group ID from the container.
std::optional<tab_groups::TabGroupId> GetContainerGroupId(
    const TabCollectionNode& container_node,
    const TabStripModel& tab_strip_model) {
  if (container_node.type() == TabCollectionNode::Type::GROUP) {
    const auto& group = TabGroupDataFromNode(container_node);
    return group.id();
  }
  return std::nullopt;
}

// Returns the index for insertion before the given collection node.
int GetInsertionIndexForNode(const TabCollectionNode& node,
                             const TabStripModel& tab_strip_model) {
  if (node.type() == TabCollectionNode::Type::TAB) {
    const tabs::TabInterface* tab =
        std::get<tabs::ConstDanglingUntriagedTabInterface>(node.GetNodeData());
    int index = tab_strip_model.GetIndexOfTab(tab);
    return index;
  } else if (node.type() == TabCollectionNode::Type::GROUP) {
    const auto* group_collection =
        static_cast<const tabs::TabGroupTabCollection*>(
            std::get<tabs::ConstDanglingUntriagedTabCollection>(
                node.GetNodeData()));
    const auto* group_data = group_collection->GetTabGroup();
    gfx::Range range = group_data->ListTabs();
    return range.start();
  } else if (node.type() == TabCollectionNode::Type::SPLIT) {
    const auto* split_collection = static_cast<const tabs::SplitTabCollection*>(
        std::get<tabs::ConstDanglingUntriagedTabCollection>(
            node.GetNodeData()));
    const auto* split_data = split_collection->data();
    gfx::Range range = split_data->GetIndexRange();
    return range.start();
  }
  NOTREACHED() << "Unsupported Tab Collection Node";
}

}  // namespace

TabDragHandlerImpl::TabDragHandlerImpl(
    TabStripModel& tab_strip_model,
    TabCollectionNode& root_node,
    TabStripRegionView& tab_strip_region_view)
    : tab_strip_model_(tab_strip_model),
      root_node_(root_node),
      tab_strip_region_view_(tab_strip_region_view),
      link_drop_handler_(
          std::make_unique<TabLinkDropHandler>(tab_strip_model)) {}

TabDragHandlerImpl::~TabDragHandlerImpl() = default;

void TabDragHandlerImpl::InitializeDrag(
    TabCollectionNode& node,
    const ui::ListSelectionModel& original_selection_model,
    const ui::LocatedEvent& event) {
  ResetDragState();
  drag_controller_ = std::make_unique<TabDragController>();

  DragInitData drag_init_data;
  switch (node.type()) {
    case TabCollectionNode::Type::TAB:
      drag_init_data = GetDragInitDataForTabDrag(node);
      break;
    case TabCollectionNode::Type::GROUP:
      drag_init_data = GetDragInitDataForGroupHeaderDrag(node);
      break;
    default:
      NOTREACHED();
  }

  CHECK(drag_init_data.source_dragged_view);

  // The event source is not guaranteed to be the same as the source dragged
  // view (e.g. the tab group header view handles the event, while the tab
  // group view is considered the drag source).
  // Calculate the offset accordingly.
  std::unique_ptr<ui::Event> cloned_event = event.Clone();
  ui::LocatedEvent* located_event = cloned_event->AsLocatedEvent();
  if (event.target()) {
    event.target()->ConvertEventToTarget(drag_init_data.source_dragged_view,
                                         located_event);
  }
  const gfx::Point offset_from_source = located_event->location();

  if (drag_controller_->Init(this, drag_init_data.source_dragged_view,
                             drag_init_data.dragged_views, offset_from_source,
                             original_selection_model,
                             EventSourceFromEvent(event)) ==
      TabDragController::Liveness::kDeleted) {
    ResetDragState();
  }
}

TabDragHandlerImpl::DragInitData::DragInitData() = default;
TabDragHandlerImpl::DragInitData::~DragInitData() = default;
TabDragHandlerImpl::DragInitData::DragInitData(
    const TabDragHandlerImpl::DragInitData&) = default;
TabDragHandlerImpl::DragInitData& TabDragHandlerImpl::DragInitData::operator=(
    const TabDragHandlerImpl::DragInitData&) = default;

TabDragHandlerImpl::DragInitData TabDragHandlerImpl::GetDragInitDataForTabDrag(
    TabCollectionNode& source_node) {
  const auto& selected_tabs_indices =
      tab_strip_model_->selection_model().GetListSelectionModel();
  std::vector<tabs::TabInterface*> selected_tabs;
  for (int index : selected_tabs_indices.selected_indices()) {
    selected_tabs.push_back(tab_strip_model_->GetTabAtIndex(index));
  }
  DragInitData drag_init_data;
  drag_init_data.dragged_views.reserve(selected_tabs.size());

  const tabs::TabInterface* source_tab =
      std::get<tabs::ConstDanglingUntriagedTabInterface>(
          source_node.GetNodeData());
  CHECK(source_tab);

  std::map<tab_groups::TabGroupId, TabSlotView*> dragged_groups;
  if (!source_tab->IsPinned()) {
    // A TabSlotView must be added for each dragged group, in addition to the
    // tabs themselves. These must be inserted into `dragged_views` in the
    // order they appear within the tab strip.
    dragged_groups = GetFullySelectedGroups(selected_tabs);
  }

  // Track the node and build a shim view for each selected node.
  for (tabs::TabInterface* tab : selected_tabs) {
    // Filter out selections that don't match the pinned state of the latest
    // selected tab.
    if (source_tab->IsPinned() != tab->IsPinned()) {
      continue;
    }

    if (auto group_id = tab->GetGroup()) {
      // If the tab belongs to a group that is fully selected, then add the
      // group's TabSlotView first.
      if (auto it = dragged_groups.find(*group_id);
          it != dragged_groups.end()) {
        drag_init_data.dragged_views.push_back(it->second);
        dragged_groups.erase(it);
      }
    }

    size_t index = tab_strip_model_->GetIndexOfTab(tab);
    drag_init_data.list_selection_model.AddIndexToSelection(index);
    TabCollectionNode* selected_node =
        root_node_->GetNodeForHandle(tab->GetHandle());
    CHECK(selected_node);
    auto* slot_view = &GetOrCreateSlotViewForNode(*selected_node);
    drag_init_data.dragged_views.push_back(slot_view);
    if (selected_node == &source_node) {
      drag_init_data.source_dragged_view = slot_view;
      drag_init_data.list_selection_model.set_active(index);
    }
  }
  drag_init_data.dragged_views.shrink_to_fit();
  return drag_init_data;
}

TabDragHandlerImpl::DragInitData
TabDragHandlerImpl::GetDragInitDataForGroupHeaderDrag(
    TabCollectionNode& source_node) {
  DragInitData drag_init_data;
  drag_init_data.source_dragged_view = &GetOrCreateSlotViewForNode(source_node);
  drag_init_data.dragged_views.push_back(drag_init_data.source_dragged_view);
  for (const auto& child : source_node.children()) {
    if (child->type() == TabCollectionNode::Type::SPLIT) {
      for (const auto& split_child : child->children()) {
        drag_init_data.dragged_views.push_back(
            &GetOrCreateSlotViewForNode(*split_child.get()));
      }
    } else {
      drag_init_data.dragged_views.push_back(
          &GetOrCreateSlotViewForNode(*child.get()));
    }
  }
  return drag_init_data;
}

std::map<tab_groups::TabGroupId, TabSlotView*>
TabDragHandlerImpl::GetFullySelectedGroups(
    const std::vector<tabs::TabInterface*>& selected_tabs) {
  std::map<tab_groups::TabGroupId, int> dragged_group_tab_counts;
  for (tabs::TabInterface* tab : selected_tabs) {
    if (auto group_id = tab->GetGroup()) {
      dragged_group_tab_counts[*group_id]++;
    }
  }

  std::map<tab_groups::TabGroupId, TabSlotView*> selected_groups;
  for (const auto& [group_id, dragged_tab_count] : dragged_group_tab_counts) {
    const auto* group = tab_strip_model_->group_model()->GetTabGroup(group_id);
    CHECK(group);
    if (group->tab_count() == dragged_tab_count) {
      auto* selected_node = GetNodeForTabGroup(group_id);
      auto& slot_view = GetOrCreateSlotViewForNode(*selected_node);
      selected_groups.insert({group_id, &slot_view});
    }
  }
  return selected_groups;
}

bool TabDragHandlerImpl::ContinueDrag(views::View& event_source_view,
                                      const ui::LocatedEvent& event) {
  if (!drag_controller_) {
    return false;
  }
  gfx::Point screen_location(event.location());
  ConvertPointToScreen(&event_source_view, &screen_location);

  // Avora: Always call Drag() first so Chromium handles normal tab
  // reordering, detach, etc.  Then layer Avora drop-zone feedback on top.
  auto ref = weak_factory_.GetWeakPtr();
  auto liveness = drag_controller_->Drag(screen_location);
  if (!ref) {
    return false;
  }
  if (liveness == TabDragController::Liveness::kDeleted) {
    ResetDragState();
    return false;
  }

  // Avora: Show drop-zone highlights when the cursor is above the daily
  // section (over favorites, pinned section, or the gap between them).
  auto* tab_strip_view = views::AsViewClass<TabStripView>(
      tab_strip_region_view_->GetTabStripView());
  if (tab_strip_view) {
    auto* unpinned_scroll = tab_strip_view->unpinned_tabs_scroll_view();
    bool in_avora_drop_zone = false;
    if (unpinned_scroll && unpinned_scroll->GetVisible()) {
      int daily_top = unpinned_scroll->GetBoundsInScreen().y();
      gfx::Rect strip_bounds = tab_strip_view->GetBoundsInScreen();
      if (screen_location.y() < daily_top &&
          screen_location.x() >= strip_bounds.x() &&
          screen_location.x() <= strip_bounds.right() &&
          screen_location.y() >= strip_bounds.y()) {
        in_avora_drop_zone = true;
      }
    }

    // Avora: Hide the actual dragged tab views when in the Avora drop
    // zone so they don't appear in their original slots while the ghost
    // overlay is shown.  Restore opacity when back in the daily section.
    if (drag_controller_) {
      for (auto& [_, slot_view] : slot_views_) {
        views::View* dragged_view = ViewFromTabSlot(slot_view);
        if (dragged_view && dragged_view->layer()) {
          dragged_view->layer()->SetOpacity(in_avora_drop_zone ? 0.f : 1.f);
        }
      }
    }

    if (in_avora_drop_zone) {
      // Show a drag ghost so the user sees what they're dragging.
      if (drag_controller_) {
        const auto& drag_data = drag_controller_->GetSessionData();
        std::u16string title;
        if (!drag_data.tab_drag_data_.empty() &&
            drag_data.tab_drag_data_[0].contents) {
          title = drag_data.tab_drag_data_[0].contents->GetTitle();
        }
        tab_strip_view->ShowDragGhost(screen_location, title);
      }

      // Highlight favorites or pinned drop zones.
      auto* favorites = tab_strip_view->GetFavoritesView();
      if (favorites) {
        bool over = favorites->GetVisible() &&
            favorites->GetBoundsInScreen().Contains(screen_location);
        favorites->SetDropHighlighted(over);
        if (over) {
          favorites->UpdateInsertionGap(screen_location);
        }
      }

      auto* pinned_section = tab_strip_view->GetPinnedSectionView();
      if (pinned_section) {
        bool over_pinned = pinned_section->GetVisible() &&
            pinned_section->GetBoundsInScreen().Contains(screen_location);

        if (!over_pinned) {
          auto* fav = tab_strip_view->GetFavoritesView();
          if (unpinned_scroll && fav) {
            int top = fav->GetBoundsInScreen().bottom();
            int bottom = unpinned_scroll->GetBoundsInScreen().y();
            gfx::Rect gap(fav->GetBoundsInScreen().x(), top,
                          fav->GetBoundsInScreen().width(), bottom - top);
            over_pinned = gap.Contains(screen_location);
          }
        }

        pinned_section->SetDropHighlighted(over_pinned);
        if (over_pinned) {
          int folder_idx =
              pinned_section->GetFolderIndexAtScreenPoint(screen_location);
          pinned_section->HighlightFolder(folder_idx);
        } else {
          pinned_section->ClearHighlight();
        }
      }
    } else {
      // Clear highlights when not in the drop zone.
      tab_strip_view->HideDragGhost();
      auto* pinned_section = tab_strip_view->GetPinnedSectionView();
      if (pinned_section) {
        pinned_section->SetDropHighlighted(false);
        pinned_section->ClearHighlight();
      }
      auto* favorites = tab_strip_view->GetFavoritesView();
      if (favorites) {
        favorites->SetDropHighlighted(false);
      }
    }
  }

  return true;
}

void TabDragHandlerImpl::EndDrag(EndDragReason reason) {
  // Note: we avoid ending the SystemDnD if the `EndDragReason` is "capture
  // lost" because some window managers on Wayland don't send exit events to tab
  // strips, which would cause this to incorrectly end the SystemDnD while the
  // drag is attaching to a new tab strip. See http://crbug.com/505023370 for an
  // example of this.
  if (TabDragController::IsSystemDnDSessionRunning() &&
      reason != EndDragReason::kCaptureLost) {
    TabDragController::OnSystemDnDEnded();
    return;
  }

  // Let TabDragController decide whether this reason should actually end the
  // drag (e.g. capture loss while dragging a detached window) and destroy
  // itself when appropriate.
  if (drag_controller_) {
    drag_controller_->EndDrag(reason);
  }
}

void TabDragHandlerImpl::HandleDraggedTabsIntoNode(
    const TabCollectionNode& node) {
  CHECK(drag_controller_);
  const auto& drag_session_data = drag_controller_->GetSessionData();

  const TabDragData* source_drag_data =
      drag_session_data.source_view_drag_data();
  const content::WebContents* source_contents = source_drag_data->contents;
  if (!source_contents) {
    return;
  }

  int target_index;
  std::optional<tab_groups::TabGroupId> target_group_id;
  if (node.type() == TabCollectionNode::Type::GROUP) {
    // If dragging into a group, then either put the dragged tabs into the
    // start/end if the source dragged tab is before/after the group, or move
    // all dragged tabs to the source dragged tab's position if it's already
    // in the group.
    const auto& group = TabGroupDataFromNode(node);
    target_group_id = group.id();
    int source_tab_index =
        tab_strip_model_->GetIndexOfWebContents(source_contents);
    auto group_indexes = group.ListTabs();
    if (source_tab_index < static_cast<int>(group_indexes.start())) {
      target_index = group_indexes.start();
    } else if (source_tab_index > static_cast<int>(group_indexes.end())) {
      target_index = group_indexes.end();
    } else {
      target_index = source_tab_index;
    }
  } else if (auto source_tab_group_id =
                 tabs::TabInterface::GetFromContents(source_contents)
                     ->GetGroup()) {
    // If the source dragged tab is in a group, and we're not entering a
    // group, then move the dragged tabs to be after the group.
    const auto* group =
        tab_strip_model_->group_model()->GetTabGroup(*source_tab_group_id);
    target_index = group->ListTabs().end();
  } else {
    target_index = tab_strip_model_->GetIndexOfWebContents(source_contents);
  }

  int num_dragged_tabs_before_target = 0;
  for (const TabDragData& tab_drag_data : drag_session_data.tab_drag_data_) {
    const content::WebContents* contents = tab_drag_data.contents;
    if (contents &&
        tab_strip_model_->GetIndexOfWebContents(contents) < target_index) {
      ++num_dragged_tabs_before_target;
    }
  }
  target_index = target_index - num_dragged_tabs_before_target;
  target_index = std::clamp(target_index, 0, tab_strip_model_->count() - 1);

  tab_strip_model_->MoveSelectedTabsTo(target_index, target_group_id);
}

TabDragContext* TabDragHandlerImpl::GetDragContext() {
  return this;
}

bool TabDragHandlerImpl::IsDragging() const {
  // We check if the drag controller is attached to this context instead of
  // `started_drag()` because `started_drag()` only becomes true after the
  // initial selection reset that occurs when a drag truly begins. If we
  // relied on `started_drag()`, the tab strip might incorrectly
  // expand a collapsed group during that initial selection change.
  return drag_controller_ && drag_controller_->attached_context() == this &&
         drag_controller_->active();
}

bool TabDragHandlerImpl::IsViewDragging(const views::View& view) const {
  if (!drag_controller_) {
    return false;
  }
  for (TabSlotView* slot_view :
       drag_controller_->GetSessionData().attached_views()) {
    if (&view == ViewFromTabSlot(slot_view)) {
      return true;
    }
  }
  return false;
}

bool TabDragHandlerImpl::IsDraggingPinnedTabs() const {
  if (!drag_controller_) {
    return false;
  }
  const auto& drag_data = drag_controller_->GetSessionData().tab_drag_data_;
  return std::any_of(drag_data.cbegin(), drag_data.cend(),
                     [](const auto& tab_data) { return tab_data.pinned; });
}

bool TabDragHandlerImpl::IsDraggingGroups() const {
  if (!drag_controller_) {
    return false;
  }
  return !drag_controller_->GetSessionData().dragging_groups.empty();
}

std::optional<tab_groups::TabGroupId>
TabDragHandlerImpl::GetDraggingGroupHeaderId() const {
  return drag_controller_ ? drag_controller_->GetSessionData().group_header_id()
                          : std::nullopt;
}

views::View* TabDragHandlerImpl::ViewFromTabSlot(TabSlotView* view) const {
  CHECK(drag_controller_);
  auto* slot_view = views::AsViewClass<DraggedTabSlotView>(view);
  CHECK(slot_view);

  const TabCollectionNode& node = slot_view->node();

  if (node.type() == TabCollectionNode::Type::TAB) {
    const tabs::TabInterface* tab =
        std::get<tabs::ConstDanglingUntriagedTabInterface>(node.GetNodeData());
    CHECK(tab);

    if (auto group_id = tab->GetGroup();
        group_id && drag_controller_->GetSessionData().dragging_groups.contains(
                        *group_id)) {
      // If the tab belongs to a group that is being dragged, return the group's
      // view instead.
      const TabCollectionNode* group_node = GetNodeForTabGroup(*group_id);
      CHECK(group_node);
      return group_node->view();
    }

    if (tab->IsSplit()) {
      // If the dragged tab view is in a split, return the split's tab view
      // instead.
      const TabCollectionNode* split_node =
          root_node_->GetParentNodeForHandle(tab->GetHandle());
      CHECK(split_node);
      CHECK_EQ(split_node->type(), TabCollectionNode::Type::SPLIT);
      return split_node->view();
    }
  }

  return node.view();
}

std::optional<gfx::Vector2d> TabDragHandlerImpl::GetOffsetFromSourceAtDragStart(
    View* view) const {
  gfx::Vector2d* offset = view->GetProperty(kOffsetAtTabDragStart);
  return offset ? std::make_optional(*offset) : std::nullopt;
}

std::optional<BrowserRootView::DropIndex>
TabDragHandlerImpl::GetLinkDropIndexForNode(
    const TabCollectionNode& node,
    std::optional<DragPositionHint> position_hint) const {
  return link_drop_handler_->GetDropIndexForNode(node, position_hint);
}

void TabDragHandlerImpl::OnTabWillBeAdded() {
  if (drag_controller_) {
    drag_controller_->EndDrag(EndDragReason::kModelAddedTab);
  }
}

void TabDragHandlerImpl::OnTabWillBeRemoved(content::WebContents* contents) {
  if (drag_controller_) {
    drag_controller_->OnTabWillBeRemoved(contents);
  }
}

bool TabDragHandlerImpl::CanAcceptEvent(const ui::Event& event) {
  // The drag context has to be able to process mouse events during the drag.
  // By default, this is predicated on visibility, but the handler should not
  // be visible. Instead, defer the check to the parent.
  return parent()->CanAcceptEvent(event);
}

void TabDragHandlerImpl::OnGestureEvent(ui::GestureEvent* event) {
  if (!drag_controller_) {
    return;
  }

  switch (event->type()) {
    case ui::EventType::kGestureScrollEnd:
    case ui::EventType::kScrollFlingStart:
    case ui::EventType::kGestureEnd:
      EndDrag(EndDragReason::kComplete);
      break;

    case ui::EventType::kGestureLongTap: {
      const auto& session_data = drag_controller_->GetSessionData();
      TabCollectionNode* source_node = nullptr;
      if (session_data.group_header_drag_data_) {
        source_node =
            GetNodeForTabGroup(session_data.group_header_drag_data_->group);
      } else {
        source_node =
            GetNodeForContents(session_data.source_dragged_contents());
      }

      if (source_node && source_node->view()) {
        views::View* source_view = source_node->view();
        ui::GestureEvent converted_event(*event, static_cast<View*>(this),
                                         source_view);

        // OnGestureEvent could start a blocking loop, so this may be destroyed.
        auto ref = weak_factory_.GetWeakPtr();
        source_view->OnGestureEvent(&converted_event);
        if (!ref) {
          return;
        }
      }

      EndDrag(EndDragReason::kCancel);
      break;
    }

    case ui::EventType::kGestureTapDown:
      EndDrag(EndDragReason::kCancel);
      break;

    case ui::EventType::kGestureScrollUpdate:
      ContinueDrag(*this, *event);
      break;

    default:
      break;
  }

  event->SetHandled();
}

bool TabDragHandlerImpl::OnMouseDragged(const ui::MouseEvent& event) {
  return ContinueDrag(*this, event);
}

void TabDragHandlerImpl::OnMouseReleased(const ui::MouseEvent& event) {
  EndDrag(EndDragReason::kComplete);
}

bool TabDragHandlerImpl::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  if (!TabDragController::IsSystemDnDSessionRunning()) {
    return false;
  }
  format_types->insert(
      ui::ClipboardFormatType::CustomPlatformType(ui::kMimeTypeWindowDrag));
  return true;
}

bool TabDragHandlerImpl::CanDrop(const OSExchangeData& data) {
  return TabDragController::IsSystemDnDSessionRunning() &&
         data.HasCustomFormat(ui::ClipboardFormatType::CustomPlatformType(
             ui::kMimeTypeWindowDrag));
}

void TabDragHandlerImpl::OnDragEntered(const ui::DropTargetEvent& event) {
  CHECK(TabDragController::IsSystemDnDSessionRunning());
  TabDragController::OnSystemDnDUpdated(event);
}
int TabDragHandlerImpl::OnDragUpdated(const ui::DropTargetEvent& event) {
  // This can be false because we can still receive drag events after
  // TabDragController is destroyed due to the asynchronous nature of the
  // platform DnD.
  if (TabDragController::IsSystemDnDSessionRunning()) {
    TabDragController::OnSystemDnDUpdated(event);
    return ui::DragDropTypes::DRAG_MOVE;
  }
  return ui::DragDropTypes::DRAG_NONE;
}

void TabDragHandlerImpl::OnDragExited() {
  // See comment in OnDragUpdated().
  if (TabDragController::IsSystemDnDSessionRunning()) {
    TabDragController::OnSystemDnDExited();
  }
}

void TabDragHandlerImpl::OnMouseCaptureLost() {
  EndDrag(EndDragReason::kCaptureLost);
}

TabDragContext* TabDragHandlerImpl::GetContextForNewBrowser(
    BrowserView* browser_view) const {
  return browser_view->tab_strip_view()->GetDragContext();
}

TabSlotView* TabDragHandlerImpl::GetTabForContents(
    content::WebContents* contents) {
  TabCollectionNode* node = GetNodeForContents(contents);
  return node ? &GetOrCreateSlotViewForNode(*node) : nullptr;
}

content::WebContents* TabDragHandlerImpl::GetContentsForTab(TabSlotView* view) {
  auto* slot_view = views::AsViewClass<DraggedTabSlotView>(view);
  CHECK(slot_view);
  if (slot_view->node().type() != TabCollectionNode::Type::TAB) {
    return nullptr;
  }
  const tabs::TabInterface* tab =
      std::get<tabs::ConstDanglingUntriagedTabInterface>(
          slot_view->node().GetNodeData());
  CHECK(tab);
  return tab->GetContents();
}

bool TabDragHandlerImpl::IsTabDetachable(const TabSlotView* view) const {
  return true;
}

TabSlotView* TabDragHandlerImpl::GetTabGroupHeader(
    const tab_groups::TabGroupId& group_id) {
  TabCollectionNode* node = GetNodeForTabGroup(group_id);
  return node ? &GetOrCreateSlotViewForNode(*node) : nullptr;
}

TabStripModel* TabDragHandlerImpl::GetTabStripModel() {
  return base::to_address(tab_strip_model_);
}

TabDragController* TabDragHandlerImpl::GetDragController() {
  return drag_controller_.get();
}

void TabDragHandlerImpl::OwnDragController(
    std::unique_ptr<TabDragController> controller) {
  drag_controller_ = std::move(controller);
}

std::unique_ptr<TabDragController> TabDragHandlerImpl::ReleaseDragController() {
  return std::move(drag_controller_);
}

void TabDragHandlerImpl::DestroyDragController() {
  drag_controller_.reset();
}

void TabDragHandlerImpl::StartedDragging(
    const std::vector<TabSlotView*>& views) {
  const auto& drag_data = drag_controller_->GetSessionData();
  if (base::FeatureList::IsEnabled(features::kCollapseTabGroupDuringDrag) &&
      drag_data.group_header_drag_data_.has_value()) {
    tab_groups::TabGroupId group_id = drag_data.group_header_drag_data_->group;
    const TabGroup* group =
        tab_strip_model_->group_model()->GetTabGroup(group_id);
    if (group && !group->visual_data()->is_collapsed()) {
      drag_controller_->SetGroupHeaderWasCollapsedFromDrag(true);
      tab_groups::TabGroupVisualData new_data(group->visual_data()->title(),
                                              group->visual_data()->color(),
                                              /*is_collapsed=*/true);
      tab_strip_model_->ChangeTabGroupVisuals(group_id, new_data);
    }
  }

  if (gfx::NativeWindow source_window = GetWidget()->GetNativeWindow()) {
    const BrowserView* browser_view =
        BrowserView::GetBrowserViewForNativeWindow(source_window);

    const auto* state_controller =
        tabs::VerticalTabStripStateController::From(browser_view->browser());

    if (state_controller && state_controller->IsExpandOnHoverEnabled()) {
      expand_on_hover_lock_ =
          browser_view->tab_strip_view()->GetExpandOnHoverLock(
              ExpandOnHoverLockType::kKeepExpanded);
    }

    auto* tab_strip_view = views::AsViewClass<TabStripView>(
        tab_strip_region_view_->GetTabStripView());
    CHECK(tab_strip_view);
    for (views::ScrollView* scroll_view :
         {tab_strip_view->pinned_tabs_scroll_view(),
          tab_strip_view->unpinned_tabs_scroll_view()}) {
      CHECK(scroll_view);
      scroll_synchronizers_.push_back(
          scroll_view->EnableScrollSynchronization());
    }

    // Avora: Notify the favorites view that a drag session has started so it
    // can show a drop-zone placeholder (even when empty).
    auto* favorites = tab_strip_view->GetFavoritesView();
    if (favorites) {
      favorites->SetDragActive(true);
    }
    auto* pinned_section = tab_strip_view->GetPinnedSectionView();
    if (pinned_section) {
      pinned_section->SetDragActive(true);
    }
  }

  CHECK(drag_controller_);
  auto* source_dragged_view = ViewFromTabSlot(drag_controller_->GetSessionData()
                                                  .source_view_drag_data()
                                                  ->attached_view);
  CHECK(source_dragged_view);
  gfx::Point source_view_origin_in_screen =
      source_dragged_view->GetBoundsInScreen().origin();

  for (auto* view : views) {
    auto* slot_view = views::AsViewClass<DraggedTabSlotView>(view);
    CHECK(slot_view);

    views::View* dragged_view = ViewFromTabSlot(slot_view);
    CHECK(dragged_view);
    dragged_view->SetPaintToLayer();
    dragged_view->layer()->SetFillsBoundsOpaquely(false);
    gfx::Vector2d offset = dragged_view->GetBoundsInScreen().origin() -
                           source_view_origin_in_screen;
    dragged_view->SetProperty(kOffsetAtTabDragStart, offset);

    slot_view->UpdateBounds();
  }
}

void TabDragHandlerImpl::DraggedTabsDetached() {
  expand_on_hover_lock_.reset();
  scroll_synchronizers_.clear();

  // Avora: Clear drag-active state on favorites when tabs detach to a new
  // window.
  auto* tab_strip_view = views::AsViewClass<TabStripView>(
      tab_strip_region_view_->GetTabStripView());
  if (tab_strip_view) {
    auto* favorites = tab_strip_view->GetFavoritesView();
    if (favorites) {
      favorites->SetDragActive(false);
    }
    auto* pinned_section = tab_strip_view->GetPinnedSectionView();
    if (pinned_section) {
      pinned_section->SetDropHighlighted(false);
      pinned_section->ClearHighlight();
      pinned_section->SetDragActive(false);
    }
    tab_strip_view->HideDragGhost();
  }
}

void TabDragHandlerImpl::StoppedDragging() {
  expand_on_hover_lock_.reset();
  scroll_synchronizers_.clear();

  // Avora: Determine drop targets BEFORE destroying compositor layers.
  // This avoids the visual "snap-back" where a tab briefly reappears in its
  // original slot before being closed or moved.
  bool drop_on_favorites = false;
  bool drop_on_pinned = false;
  bool drop_on_unpinned = false;
  bool was_dragging_pinned = false;
  std::vector<int> favorite_indices_to_close;
  std::set<content::WebContents*> favorited_contents;

  auto* tab_strip_view = views::AsViewClass<TabStripView>(
      tab_strip_region_view_->GetTabStripView());

  if (drag_controller_ && tab_strip_view) {
    const DragSessionData& drag_data = drag_controller_->GetSessionData();
    if (!drag_data.tab_drag_data_.empty()) {
      gfx::Point drop_point =
          display::Screen::Get()->GetCursorScreenPoint();

      // Check favorites.
      auto* favorites = tab_strip_view->GetFavoritesView();
      if (favorites && favorites->GetVisible() &&
          favorites->GetBoundsInScreen().Contains(drop_point)) {
        drop_on_favorites = true;
        int insert_at = favorites->insertion_gap_index();
        auto* mgr = favorites->GetFavoritesManager();
        int offset = 0;
        for (const auto& tab_data : drag_data.tab_drag_data_) {
          if (tab_data.contents) {
            std::string url =
                tab_data.contents->GetLastCommittedURL().spec();
            std::string title =
                base::UTF16ToUTF8(tab_data.contents->GetTitle());
            if (!mgr->IsFavorited(url)) {
              if (insert_at >= 0) {
                mgr->InsertFavoriteAt(insert_at + offset, url, title);
                offset++;
              } else {
                mgr->AddFavorite(url, title);
              }
            }
            favorited_contents.insert(tab_data.contents);
            int idx = tab_strip_model_->GetIndexOfWebContents(
                tab_data.contents);
            if (idx != TabStripModel::kNoTab) {
              favorite_indices_to_close.push_back(idx);
            }
          }
        }
      }

      // Avora: Check if dropped on the pinned section or a folder within it.
      // The drop zone covers the pinned section view itself AND the gap
      // between favorites and the daily scroll view (space name, new-tab btn).
      bool drop_on_folder = false;
      if (!drop_on_favorites) {
        auto* pinned_section = tab_strip_view->GetPinnedSectionView();
        bool in_pinned_zone = false;
        if (pinned_section && pinned_section->GetVisible() &&
            pinned_section->GetBoundsInScreen().Contains(drop_point)) {
          in_pinned_zone = true;
        }
        if (!in_pinned_zone) {
          auto* unpinned_scroll = tab_strip_view->unpinned_tabs_scroll_view();
          auto* fav = tab_strip_view->GetFavoritesView();
          if (unpinned_scroll && fav) {
            int top = fav->GetBoundsInScreen().bottom();
            int bottom = unpinned_scroll->GetBoundsInScreen().y();
            gfx::Rect gap(fav->GetBoundsInScreen().x(), top,
                          fav->GetBoundsInScreen().width(), bottom - top);
            in_pinned_zone = gap.Contains(drop_point);
          }
        }
        if (in_pinned_zone && pinned_section) {
          int folder_idx =
              pinned_section->GetFolderIndexAtScreenPoint(drop_point);
          if (folder_idx >= 0) {
            // Dropped on a folder header — add to that folder and close.
            drop_on_folder = true;
            auto* mgr = pinned_section->GetFoldersManager();
            std::vector<base::WeakPtr<content::WebContents>>
                folder_contents_to_close;
            for (const auto& tab_data : drag_data.tab_drag_data_) {
              if (tab_data.contents) {
                std::string url =
                    tab_data.contents->GetLastCommittedURL().spec();
                std::string title =
                    base::UTF16ToUTF8(tab_data.contents->GetTitle());
                mgr->MoveTabToFolder(url, title, folder_idx);
                folder_contents_to_close.push_back(
                    tab_data.contents->GetWeakPtr());
              }
            }
            // Defer tab closure to avoid model mutations during drag
            // teardown (same reason as deferred SetTabPinned above).
            if (!folder_contents_to_close.empty()) {
              TabStripModel* model_ptr = &*tab_strip_model_;
              base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](TabStripModel* model,
                         std::vector<base::WeakPtr<content::WebContents>>
                             to_close) {
                        std::vector<int> indices;
                        for (auto& wc : to_close) {
                          if (!wc) {
                            continue;
                          }
                          int idx =
                              model->GetIndexOfWebContents(wc.get());
                          if (idx != TabStripModel::kNoTab) {
                            indices.push_back(idx);
                          }
                        }
                        std::sort(indices.rbegin(), indices.rend());
                        for (int idx : indices) {
                          model->CloseWebContentsAt(
                              idx,
                              TabCloseTypes::CLOSE_USER_GESTURE |
                                  TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
                        }
                      },
                      base::Unretained(model_ptr),
                      std::move(folder_contents_to_close)));
            }
          } else {
            // Dropped on the pinned section but not on a folder — pin the tab.
            drop_on_pinned = true;
          }
        }
      }

      // Check pinned/unpinned sections.
      was_dragging_pinned = IsDraggingPinnedTabs();
      auto* pinned_scroll = tab_strip_view->pinned_tabs_scroll_view();
      auto* unpinned_scroll = tab_strip_view->unpinned_tabs_scroll_view();
      if (!drop_on_pinned) {
        drop_on_pinned = pinned_scroll &&
                         pinned_scroll->GetBoundsInScreen().Contains(
                             drop_point);
      }
      drop_on_unpinned =
          !drop_on_folder && unpinned_scroll &&
          unpinned_scroll->GetBoundsInScreen().Contains(drop_point);
    }
  }

  // Avora: Clear drop-zone highlights, drag-active state, and ghost.
  if (tab_strip_view) {
    auto* favorites = tab_strip_view->GetFavoritesView();
    if (favorites) {
      favorites->SetDragActive(false);
    }
    auto* pinned_section = tab_strip_view->GetPinnedSectionView();
    if (pinned_section) {
      pinned_section->SetDropHighlighted(false);
      pinned_section->ClearHighlight();
      pinned_section->SetDragActive(false);
    }
    tab_strip_view->HideDragGhost();
  }

  // Destroy compositor layers. For tabs being moved to favorites, hide the
  // layer first (opacity 0) so the tab doesn't visually "snap back" to its
  // slot before being closed.
  for (auto& [_, slot_view] : slot_views_) {
    views::View* dragged_view = ViewFromTabSlot(slot_view);
    CHECK(dragged_view);

    if (!favorited_contents.empty()) {
      content::WebContents* contents = GetContentsForTab(slot_view);
      if (contents && favorited_contents.count(contents)) {
        if (dragged_view->layer()) {
          dragged_view->layer()->SetOpacity(0.f);
        }
        dragged_view->DestroyLayer();
        dragged_view->ClearProperty(kOffsetAtTabDragStart);
        continue;
      }
    }

    dragged_view->DestroyLayer();
    dragged_view->ClearProperty(kOffsetAtTabDragStart);
  }

  // Avora: Close tabs that were moved to favorites.
  if (drop_on_favorites && !favorite_indices_to_close.empty()) {
    std::sort(favorite_indices_to_close.rbegin(),
              favorite_indices_to_close.rend());
    for (int idx : favorite_indices_to_close) {
      tab_strip_model_->CloseWebContentsAt(
          idx, TabCloseTypes::CLOSE_USER_GESTURE |
                   TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
    }
  }

  // Avora: Pin or unpin tabs based on where the drag ended.
  // IMPORTANT: SetTabPinned() triggers model moves and layout animations that
  // crash if run during drag teardown (the drag controller still holds stale
  // view references). Collect the WebContents and defer the mutation to a
  // PostTask so it runs after the drag session is fully destroyed.
  if (!drop_on_favorites && drag_controller_ && tab_strip_view) {
    const DragSessionData& drag_data = drag_controller_->GetSessionData();
    if (!drag_data.tab_drag_data_.empty()) {
      std::vector<base::WeakPtr<content::WebContents>> contents_to_pin;
      std::vector<base::WeakPtr<content::WebContents>> contents_to_unpin;

      if (drop_on_pinned && !was_dragging_pinned) {
        for (const auto& tab_drag_data : drag_data.tab_drag_data_) {
          if (!tab_drag_data.pinned && tab_drag_data.contents) {
            contents_to_pin.push_back(
                tab_drag_data.contents->GetWeakPtr());
          }
        }
      } else if (drop_on_unpinned && was_dragging_pinned) {
        for (const auto& tab_drag_data : drag_data.tab_drag_data_) {
          if (tab_drag_data.pinned && tab_drag_data.contents) {
            contents_to_unpin.push_back(
                tab_drag_data.contents->GetWeakPtr());
          }
        }
      }

      if (!contents_to_pin.empty() || !contents_to_unpin.empty()) {
        TabStripModel* model_ptr = &*tab_strip_model_;
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](TabStripModel* model,
                   std::vector<base::WeakPtr<content::WebContents>> to_pin,
                   std::vector<base::WeakPtr<content::WebContents>> to_unpin) {
                  for (auto& weak_contents : to_pin) {
                    if (!weak_contents) {
                      continue;
                    }
                    int idx = model->GetIndexOfWebContents(
                        weak_contents.get());
                    if (idx != TabStripModel::kNoTab &&
                        !model->IsTabPinned(idx)) {
                      model->SetTabPinned(idx, true);
                    }
                  }
                  for (auto& weak_contents : to_unpin) {
                    if (!weak_contents) {
                      continue;
                    }
                    int idx = model->GetIndexOfWebContents(
                        weak_contents.get());
                    if (idx != TabStripModel::kNoTab &&
                        model->IsTabPinned(idx)) {
                      model->SetTabPinned(idx, false);
                    }
                  }
                },
                base::Unretained(model_ptr), std::move(contents_to_pin),
                std::move(contents_to_unpin)));
      }
    }
  }

  if (drag_controller_) {
    const DragSessionData& drag_data = drag_controller_->GetSessionData();
    if (drag_data.group_header_drag_data_.has_value() &&
        drag_data.group_header_drag_data_->was_collapsed_from_drag) {
      tab_groups::TabGroupId group_id =
          drag_data.group_header_drag_data_->group;
      const TabGroup* group =
          tab_strip_model_->group_model()->GetTabGroup(group_id);
      if (group && group->visual_data()->is_collapsed()) {
        tab_groups::TabGroupVisualData new_data(group->visual_data()->title(),
                                                group->visual_data()->color(),
                                                /*is_collapsed=*/false);
        tab_strip_model_->ChangeTabGroupVisuals(group_id, new_data);
      }
    }
  }

  if (!drag_controller_) {
    return;
  }

  const DragSessionData& drag_data = drag_controller_->GetSessionData();
  if (!drag_data.group_header_drag_data_.has_value()) {
    return;
  }

  // Offset by 1 to account for the group header.
  const int drag_data_index =
      1 + drag_data.group_header_drag_data_->active_tab_index_within_group;
  const int index = tab_strip_model_->GetIndexOfWebContents(
      drag_data.tab_drag_data_[drag_data_index].contents);

  // The tabs in the group may have been closed during the drag.
  if (index != TabStripModel::kNoTab) {
    ui::ListSelectionModel selection;
    CHECK_GE(index, 0);
    selection.AddIndexToSelection(static_cast<size_t>(index));
    selection.set_active(static_cast<size_t>(index));
    selection.set_anchor(static_cast<size_t>(index));
    tab_strip_model_->SetSelectionFromModel(selection);
  }
}

void TabDragHandlerImpl::SetDragControllerCallbackForTesting(
    base::OnceCallback<void(TabDragController*)> callback) {}

TabDragPositioningDelegate* TabDragHandlerImpl::GetPositioningDelegate() {
  // Positioning is implemented through `TabDragDelegate` on individual
  // containers.
  return nullptr;
}

bool TabDragHandlerImpl::NotifyCustomEvent(
    ui::CustomElementEventType event_type,
    TabSlotView* tab_slot_view) {
  return views::ElementTrackerViews::GetInstance()->NotifyCustomEvent(
      event_type, ViewFromTabSlot(tab_slot_view));
}

TabCollectionNode* TabDragHandlerImpl::GetNodeForContents(
    content::WebContents* contents) {
  tabs::TabInterface* tab = tabs::TabInterface::GetFromContents(contents);
  CHECK(tab);
  return root_node_->GetNodeForHandle(tab->GetHandle());
}

TabCollectionNode* TabDragHandlerImpl::GetNodeForTabGroup(
    const tab_groups::TabGroupId& group_id) {
  return const_cast<TabCollectionNode*>(
      std::as_const(*this).GetNodeForTabGroup(group_id));
}

const TabCollectionNode* TabDragHandlerImpl::GetNodeForTabGroup(
    const tab_groups::TabGroupId& group_id) const {
  const auto* group = tab_strip_model_->group_model()->GetTabGroup(group_id);
  CHECK(group);
  return root_node_->GetNodeForHandle(group->GetCollectionHandle());
}

TabSlotView& TabDragHandlerImpl::GetOrCreateSlotViewForNode(
    TabCollectionNode& node) {
  auto update_tab_slot_view = [&node](TabSlotView& slot_view) -> void {
    DraggedTabSlotView& dragged_slot_view =
        static_cast<DraggedTabSlotView&>(slot_view);
    switch (node.type()) {
      case TabCollectionNode::Type::TAB: {
        const tabs::TabInterface* tab =
            std::get<tabs::ConstDanglingUntriagedTabInterface>(
                node.GetNodeData());
        CHECK(tab);
        dragged_slot_view.SetGroup(tab->GetGroup());
        dragged_slot_view.SetSplit(tab->GetSplit());
      } break;
      case TabCollectionNode::Type::GROUP:
        dragged_slot_view.SetGroup(TabGroupDataFromNode(node).id());
        break;
      default:
        NOTREACHED();
    }
    dragged_slot_view.UpdateBounds();
  };

  CHECK(node.view());
  auto it = slot_views_.find(&node);
  if (it != slot_views_.end()) {
    update_tab_slot_view(*it->second);
    return *it->second;
  }

  auto tab_slot_view = std::make_unique<DraggedTabSlotView>(node);
  auto& tab_slot_view_ref = *tab_slot_view.get();
  update_tab_slot_view(tab_slot_view_ref);

  slot_views_.insert(
      {&node, node.view()->AddChildView(std::move(tab_slot_view))});
  node_destroyed_callbacks_.push_back(node.RegisterWillDestroyCallback(
      base::BindOnce(&TabDragHandlerImpl::OnNodeWillDestroy,
                     base::Unretained(this), std::ref(node))));
  tab_slot_view_ref.SetVisible(false);
  return tab_slot_view_ref;
}

void TabDragHandlerImpl::OnNodeWillDestroy(TabCollectionNode& node) {
  auto it = slot_views_.find(&node);
  CHECK(it != slot_views_.end());
  auto view = node.view()->RemoveChildViewT(it->second);
  slot_views_.erase(it);
}

void TabDragHandlerImpl::ResetDragState() {
  drag_controller_.reset();
  scroll_synchronizers_.clear();
}

bool TabDragHandlerImpl::HandleDraggedTabsIntoPosition(
    const TabCollectionNode& container_node,
    const TabCollectionNode* target) {
  CHECK(drag_controller_);

  const auto& drag_session_data = drag_controller_->GetSessionData();

  auto container_range = GetContainerRange(container_node, *tab_strip_model_);
  int container_start_index = container_range.start();
  int container_end_index = container_range.end();
  std::optional<tab_groups::TabGroupId> target_group_id =
      GetContainerGroupId(container_node, *tab_strip_model_);

  int target_index = container_end_index;
  if (target) {
    target_index = GetInsertionIndexForNode(*target, *tab_strip_model_);
  }

  // Clamp the target index to the container's allowed range in the model.
  target_index =
      std::clamp(target_index, container_start_index, container_end_index);

  // Calculate adjustment for dragging tabs and check for No-Op.
  int num_dragged_tabs_before_target = 0;
  bool are_dragged_tabs_in_target_group = true;
  for (const TabDragData& tab_drag_data : drag_session_data.tab_drag_data_) {
    if (!tab_drag_data.contents) {
      continue;
    }
    int current_idx =
        tab_strip_model_->GetIndexOfWebContents(tab_drag_data.contents);
    CHECK(current_idx != TabStripModel::kNoTab);

    if (tabs::TabInterface::GetFromContents(tab_drag_data.contents)
            ->GetGroup() != target_group_id) {
      are_dragged_tabs_in_target_group = false;
    }

    if (current_idx < target_index) {
      ++num_dragged_tabs_before_target;
    }
  }
  int adjusted_target_index =
      std::max(0, target_index - num_dragged_tabs_before_target);

  // If all dragged tabs are already in the group, and the first dragged tab
  // is already at the target index, then consider this a no-op.
  const auto& selected_indices = tab_strip_model_->selection_model()
                                     .GetListSelectionModel()
                                     .selected_indices();
  CHECK(!selected_indices.empty());
  if (are_dragged_tabs_in_target_group &&
      *selected_indices.begin() == static_cast<size_t>(adjusted_target_index)) {
    return false;
  }

  if (auto dragged_group = GetDraggingGroupHeaderId()) {
    if (tab_strip_model_->GetFocusedGroup() == dragged_group) {
      return false;
    }
    tab_strip_model_->MoveGroupTo(*dragged_group, adjusted_target_index);
  } else {
    tab_strip_model_->MoveSelectedTabsTo(adjusted_target_index,
                                         target_group_id);
  }
  return true;
}

BEGIN_METADATA(TabDragHandlerImpl)
END_METADATA
