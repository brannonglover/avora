// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_live_folders_view.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_favorite_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr int kMenuRefreshId = 1;
constexpr int kMenuRemoveFolderId = 3;
constexpr int kMenuSignInId = 4;

constexpr int kMenuFilterCreatedId = 10;
constexpr int kMenuFilterAssignedId = 11;
constexpr int kMenuFilterReviewId = 12;
constexpr int kMenuFilterMentionedId = 13;
constexpr int kMenuFilterTeamReviewId = 14;
constexpr int kMenuFilterDraftsId = 15;

constexpr int kMenuOpenItemId = 20;

// Matches the pinned section so the two read as one list.
constexpr SkColor kRowHoverBg = SkColorSetARGB(0x1A, 0xFF, 0xFF, 0xFF);
constexpr SkColor kRowActiveBg = SkColorSetARGB(0x2E, 0xFF, 0xFF, 0xFF);
constexpr SkColor kLabelColor = SkColorSetARGB(0xE0, 0xED, 0xF2, 0xF5);
constexpr SkColor kSubtleColor = SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5);
constexpr SkColor kChevronColor = SkColorSetARGB(0xB0, 0xED, 0xF2, 0xF5);

// GitHub's own state colours, so the dots mean what they do on the site.
constexpr SkColor kStatusOpenColor = SkColorSetRGB(0x3F, 0xB9, 0x50);
constexpr SkColor kStatusDraftColor = SkColorSetRGB(0x8B, 0x94, 0x9E);
constexpr SkColor kStatusMergedColor = SkColorSetRGB(0xA3, 0x71, 0xF7);

constexpr SkColor kBadgeColor = SkColorSetRGB(0x31, 0x8C, 0xF5);

constexpr int kStatusDotSize = 8;
constexpr int kChevronSize = 8;

// Lucide "minus", as on a pinned row: closing takes away the tab, not the pull
// request, which stays in the folder.
constexpr char kCloseTabIconPath[] = "M5 12h14";
constexpr int kCloseGlyphSize = 14;
constexpr int kCloseButtonSize = 24;

// What a tab is remembered by when matching it to a row.  A fragment is the
// page scrolling to a comment, not a different page.
GURL UrlForMatching(content::WebContents* contents) {
  GURL url = contents->GetLastCommittedURL();
  if (url.is_empty()) {
    url = contents->GetVisibleURL();
  }
  GURL::Replacements strip_ref;
  strip_ref.ClearRef();
  return url.ReplaceComponents(strip_ref);
}

// Whether |tab| is free to become a row's tab.  Everything excluded here is
// already represented by some other part of the sidebar.
bool IsAdoptableTab(tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  return contents && !tab->IsPinned() && !IsFavoriteTab(contents) &&
         !IsPinnedItemTab(contents) && !IsLinkPreviewTab(contents) &&
         !IsLiveFolderTab(contents);
}

SkColor ColorForStatus(LiveFolderItemStatus status) {
  switch (status) {
    case LiveFolderItemStatus::kOpen:
      return kStatusOpenColor;
    case LiveFolderItemStatus::kDraft:
      return kStatusDraftColor;
    case LiveFolderItemStatus::kMerged:
      return kStatusMergedColor;
    case LiveFolderItemStatus::kClosed:
      return kStatusDraftColor;
  }
  return kStatusOpenColor;
}

void PaintDot(gfx::Canvas* canvas, int cx, int cy, int size, SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawCircle(gfx::Point(cx, cy), size / 2.0f, flags);
}

// Disclosure triangle: points down when expanded, right when collapsed.
void PaintChevron(gfx::Canvas* canvas,
                  int x,
                  int y,
                  int size,
                  bool expanded,
                  SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  flags.setStyle(cc::PaintFlags::kFill_Style);

  const float fx = static_cast<float>(x);
  const float fy = static_cast<float>(y);
  const float s = static_cast<float>(size);

  SkPathBuilder builder;
  if (expanded) {
    builder.moveTo(fx, fy + s * 0.25f);
    builder.lineTo(fx + s, fy + s * 0.25f);
    builder.lineTo(fx + s * 0.5f, fy + s * 0.85f);
  } else {
    builder.moveTo(fx + s * 0.25f, fy);
    builder.lineTo(fx + s * 0.25f, fy + s);
    builder.lineTo(fx + s * 0.85f, fy + s * 0.5f);
  }
  builder.close();
  canvas->DrawPath(builder.detach(), flags);
}

// Rounded pill behind the unseen count.
void PaintUnseenBadge(gfx::Canvas* canvas, const gfx::Rect& bounds) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(kBadgeColor);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(gfx::RectF(bounds), bounds.height() / 2.0f, flags);
}

std::u16string FormatSubtitle(const LiveFolderItem& item) {
  std::u16string repo = base::UTF8ToUTF16(item.metadata.repo);
  if (item.metadata.number > 0) {
    return base::StrCat(
        {repo, u" #", base::NumberToString16(item.metadata.number)});
  }
  return repo;
}

// A full-width row of text that behaves like the other sidebar rows.  Used for
// the sign-in prompt and the empty/error states; |on_click| is null for the
// purely informational ones, which then render as plain text.
class MessageRow : public views::View {
  METADATA_HEADER(MessageRow, views::View)

 public:
  MessageRow(const std::u16string& text,
             SkColor color,
             base::RepeatingClosure on_click)
      : on_click_(std::move(on_click)) {
    SetPreferredSize(gfx::Size(0, AvoraLiveFoldersView::kRowHeight));
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0,
                          AvoraLiveFoldersView::kHPadding +
                              AvoraLiveFoldersView::kIndent,
                          0, AvoraLiveFoldersView::kHPadding)));

    auto* label = AddChildView(std::make_unique<views::Label>(text));
    label->SetEnabledColor(color);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);

    if (!text.empty()) {
      SetTooltipText(text);
    }
  }

  ~MessageRow() override = default;

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (hovered_ && on_click_) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kRowHoverBg);
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 6.0f, flags);
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (event.IsLeftMouseButton() && on_click_) {
      on_click_.Run();
      return true;
    }
    return false;
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    hovered_ = true;
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    hovered_ = false;
    SchedulePaint();
  }

 private:
  bool hovered_ = false;
  base::RepeatingClosure on_click_;
};

BEGIN_METADATA(MessageRow)
END_METADATA

// Generic delegate that maps command ids onto callbacks, so each menu does not
// need its own subclass.
class CallbackMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  using CommandCallback = base::RepeatingCallback<void(int)>;
  using CheckedCallback = base::RepeatingCallback<bool(int)>;

  CallbackMenuDelegate(CommandCallback on_command, CheckedCallback is_checked)
      : on_command_(std::move(on_command)),
        is_checked_(std::move(is_checked)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (on_command_) {
      on_command_.Run(command_id);
    }
  }

  bool IsCommandIdChecked(int command_id) const override {
    return is_checked_ ? is_checked_.Run(command_id) : false;
  }

  bool IsCommandIdEnabled(int command_id) const override { return true; }

 private:
  CommandCallback on_command_;
  CheckedCallback is_checked_;
};

// The minus on a row whose pull request has a tab open.
class LiveFolderRowCloseButton : public views::Button {
  METADATA_HEADER(LiveFolderRowCloseButton, views::Button)

 public:
  explicit LiveFolderRowCloseButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(kCloseButtonSize, kCloseButtonSize));
    SetTooltipText(u"Close Tab");
    GetViewAccessibility().SetName(u"Close Tab");
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kRowHoverBg);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      canvas->DrawRoundRect(GetLocalBounds(), 6.0f, flags);
    }
    gfx::Rect glyph(GetLocalBounds());
    glyph.ClampToCenteredSize(gfx::Size(kCloseGlyphSize, kCloseGlyphSize));
    PaintLucidePathData(canvas, glyph, kCloseTabIconPath, kLabelColor);
  }
};

BEGIN_METADATA(LiveFolderRowCloseButton)
END_METADATA

}  // namespace

// ---------------------------------------------------------------------------
// LiveFolderHeaderRow
// ---------------------------------------------------------------------------

LiveFolderHeaderRow::LiveFolderHeaderRow(
    const std::string& folder_id,
    const std::string& name,
    bool expanded,
    bool syncing,
    int unseen_count,
    base::RepeatingCallback<void(const std::string&)> on_toggle,
    base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
        on_context)
    : folder_id_(folder_id),
      expanded_(expanded),
      syncing_(syncing),
      unseen_count_(unseen_count),
      on_toggle_(std::move(on_toggle)),
      on_context_(std::move(on_context)) {
  SetPreferredSize(gfx::Size(0, AvoraLiveFoldersView::kRowHeight));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, AvoraLiveFoldersView::kHPadding + kChevronSize + 8,
                        0, AvoraLiveFoldersView::kHPadding),
      8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* label = AddChildView(
      std::make_unique<views::Label>(base::UTF8ToUTF16(name)));
  label->SetEnabledColor(kLabelColor);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  layout->SetFlexForView(label, 1);

  if (syncing_) {
    auto* status = AddChildView(std::make_unique<views::Label>(u"Updating…"));
    status->SetEnabledColor(kSubtleColor);
  }
}

LiveFolderHeaderRow::~LiveFolderHeaderRow() = default;

void LiveFolderHeaderRow::OnPaintBackground(gfx::Canvas* canvas) {
  if (hovered_) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kRowHoverBg);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 6.0f, flags);
  }

  PaintChevron(canvas, AvoraLiveFoldersView::kHPadding,
               (height() - kChevronSize) / 2, kChevronSize, expanded_,
               kChevronColor);

  // The badge only earns its space when the rows it refers to are hidden.
  if (!expanded_ && unseen_count_ > 0) {
    const int badge_height = 16;
    const int badge_width = unseen_count_ > 9 ? 26 : 20;
    const gfx::Rect badge(width() - AvoraLiveFoldersView::kHPadding -
                              badge_width,
                          (height() - badge_height) / 2, badge_width,
                          badge_height);
    PaintUnseenBadge(canvas, badge);
    canvas->DrawStringRectWithFlags(
        base::NumberToString16(unseen_count_), gfx::FontList(), SK_ColorWHITE,
        badge, gfx::Canvas::TEXT_ALIGN_CENTER);
  }
}

bool LiveFolderHeaderRow::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsRightMouseButton()) {
    gfx::Point screen_point = event.location();
    views::View::ConvertPointToScreen(this, &screen_point);
    if (on_context_) {
      on_context_.Run(folder_id_, screen_point);
    }
    return true;
  }
  if (event.IsLeftMouseButton() && on_toggle_) {
    on_toggle_.Run(folder_id_);
    return true;
  }
  return false;
}

void LiveFolderHeaderRow::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
}

void LiveFolderHeaderRow::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
}

BEGIN_METADATA(LiveFolderHeaderRow)
END_METADATA

// ---------------------------------------------------------------------------
// LiveFolderItemRow
// ---------------------------------------------------------------------------

LiveFolderItemRow::LiveFolderItemRow(
    const std::string& folder_id,
    const LiveFolderItem& item,
    bool tab_active,
    ItemCallback on_click,
    ItemCallback on_close,
    base::RepeatingCallback<void(const std::string&, const std::string&,
                                 const gfx::Point&)> on_context)
    : folder_id_(folder_id),
      external_id_(item.external_id),
      url_(item.url),
      status_(item.status),
      unseen_(!item.seen),
      tab_active_(tab_active),
      on_click_(std::move(on_click)),
      on_close_(std::move(on_close)),
      on_context_(std::move(on_context)) {
  SetPreferredSize(gfx::Size(0, AvoraLiveFoldersView::kItemRowHeight));

  const int left_inset = AvoraLiveFoldersView::kHPadding +
                         AvoraLiveFoldersView::kIndent + kStatusDotSize + 8;
  // The close button, when shown, sits in the right padding's place.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(5, left_inset, 5, AvoraLiveFoldersView::kHPadding / 2),
      4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* text = AddChildView(std::make_unique<views::View>());
  text->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
  text->SetCanProcessEventsWithinSubtree(false);
  layout->SetFlexForView(text, 1);

  auto* title = text->AddChildView(
      std::make_unique<views::Label>(base::UTF8ToUTF16(item.title)));
  title->SetEnabledColor(kLabelColor);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetElideBehavior(gfx::ELIDE_TAIL);
  if (unseen_) {
    title->SetFontList(title->font_list().DeriveWithWeight(
        gfx::Font::Weight::SEMIBOLD));
  }

  const std::u16string subtitle = FormatSubtitle(item);
  if (!subtitle.empty()) {
    auto* repo = text->AddChildView(std::make_unique<views::Label>(subtitle));
    repo->SetEnabledColor(kSubtleColor);
    repo->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    repo->SetElideBehavior(gfx::ELIDE_HEAD);
    repo->SetFontList(repo->font_list().DeriveWithSizeDelta(-1));
  }

  // Hidden until hovered, as on a pinned row.  A press on it is the button's
  // own, so it never reaches OnMousePressed() as a click on the row.
  if (on_close_) {
    close_button_ = AddChildView(
        std::make_unique<LiveFolderRowCloseButton>(base::BindRepeating(
            &LiveFolderItemRow::CloseTab, base::Unretained(this))));
    close_button_->SetVisible(false);
  }

  SetTooltipText(base::UTF8ToUTF16(item.title));
  SetNotifyEnterExitOnChild(true);
}

LiveFolderItemRow::~LiveFolderItemRow() = default;

void LiveFolderItemRow::CloseTab() {
  if (on_close_) {
    // The tab's removal rebuilds the folder, and with it this row.
    auto on_close = on_close_;
    on_close.Run(folder_id_, external_id_);
  }
}

void LiveFolderItemRow::UpdateCloseButtonVisibility() {
  if (!close_button_) {
    return;
  }
  if (close_button_->GetVisible() != hovered_) {
    close_button_->SetVisible(hovered_);
    InvalidateLayout();
  }
}

void LiveFolderItemRow::OnPaintBackground(gfx::Canvas* canvas) {
  if (tab_active_ || hovered_) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(tab_active_ ? kRowActiveBg : kRowHoverBg);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 6.0f, flags);
  }

  PaintDot(canvas,
           AvoraLiveFoldersView::kHPadding + AvoraLiveFoldersView::kIndent +
               kStatusDotSize / 2,
           height() / 2, kStatusDotSize, ColorForStatus(status_));
}

bool LiveFolderItemRow::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsRightMouseButton()) {
    gfx::Point screen_point = event.location();
    views::View::ConvertPointToScreen(this, &screen_point);
    if (on_context_) {
      on_context_.Run(folder_id_, external_id_, screen_point);
    }
    return true;
  }
  if (event.IsLeftMouseButton() && on_click_) {
    on_click_.Run(folder_id_, external_id_);
    return true;
  }
  return false;
}

void LiveFolderItemRow::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  UpdateCloseButtonVisibility();
  SchedulePaint();
}

void LiveFolderItemRow::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  UpdateCloseButtonVisibility();
  SchedulePaint();
}

BEGIN_METADATA(LiveFolderItemRow)
END_METADATA

// ---------------------------------------------------------------------------
// AvoraLiveFoldersView
// ---------------------------------------------------------------------------

AvoraLiveFoldersView::AvoraLiveFoldersView(BrowserWindowInterface* browser,
                                           avora::WindowSpaceState* window_space_state)
    : browser_(browser), window_space_state_(window_space_state) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  service_ = LiveFolderService::GetForProfile(browser_->GetProfile());
  if (service_) {
    store_ = service_->store();
    service_observation_.Observe(service_);
  }
  if (store_) {
    store_observation_.Observe(store_);
  }

  if (window_space_state_) {
    window_space_state_->AddObserver(this);
  }

  tab_strip_model_ = browser_->GetTabStripModel();
  if (tab_strip_model_) {
    tab_strip_model_->AddObserver(this);
  }

  UpdateWatchedSpace();
  Rebuild();
  ScheduleAdoptTabs();
}

AvoraLiveFoldersView::~AvoraLiveFoldersView() {
  if (service_ && !watched_space_id_.empty()) {
    service_->UnwatchSpace(watched_space_id_);
  }
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void AvoraLiveFoldersView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  ReportActiveHost();

  if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      evaluated_tabs_.erase(removed.tab->GetHandle());
    }
  }
  if (change.type() == TabStripModelChange::kInserted) {
    // Deferred: the Space filter tags a new tab from its own observer, which
    // may run after this one, and adoption needs the tag.
    ScheduleAdoptTabs();
  }

  // Which rows have a tab, and which of them is selected.
  if (change.type() == TabStripModelChange::kInserted ||
      change.type() == TabStripModelChange::kRemoved ||
      selection.active_tab_changed()) {
    ScheduleRebuild();
  }
}

void AvoraLiveFoldersView::OnTabChangedAt(tabs::TabInterface* tab,
                                          TabChangeType change_type) {
  ReportActiveHost();

  // A restored tab may only learn its URL here, after it was inserted.
  if (change_type == TabChangeType::kAll && tab &&
      !evaluated_tabs_.contains(tab->GetHandle())) {
    ScheduleAdoptTabs();
  }
}

// ── Row tabs ────────────────────────────────────────────────────────────────

std::optional<LiveFolderTabKey> AvoraLiveFoldersView::FindItemForUrl(
    const std::string& space_id,
    const GURL& url) const {
  if (!store_ || space_id.empty() || !url.is_valid()) {
    return std::nullopt;
  }
  for (const LiveFolder& folder : store_->GetFolders(space_id)) {
    for (const LiveFolderItem& item : folder.ActiveItems()) {
      if (GURL(item.url) == url) {
        return LiveFolderTabKey{space_id, folder.id, item.external_id};
      }
    }
  }
  return std::nullopt;
}

int AvoraLiveFoldersView::FindItemTab(const LiveFolderTabKey& key) const {
  if (!tab_strip_model_) {
    return TabStripModel::kNoTab;
  }
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    if (GetLiveFolderTabKey(tab_strip_model_->GetWebContentsAt(i)) == key) {
      return i;
    }
  }
  return TabStripModel::kNoTab;
}

void AvoraLiveFoldersView::ScheduleAdoptTabs() {
  if (adopt_pending_) {
    return;
  }
  adopt_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<AvoraLiveFoldersView> view) {
                       if (!view) {
                         return;
                       }
                       view->adopt_pending_ = false;
                       view->AdoptTabs();
                     },
                     weak_factory_.GetWeakPtr()));
}

void AvoraLiveFoldersView::AdoptTabs() {
  if (!tab_strip_model_ || !store_) {
    return;
  }

  bool adopted = false;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(i);
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    if (!contents || evaluated_tabs_.contains(tab->GetHandle())) {
      continue;
    }

    // Not decided yet: without a Space there is no folder to look in, and
    // without a URL nothing to match.  A later event brings this back.
    const std::string space_id = GetTabSpaceId(contents);
    const GURL url = UrlForMatching(contents);
    if (space_id.empty() || !url.is_valid() || url.IsAboutBlank()) {
      continue;
    }
    evaluated_tabs_.insert(tab->GetHandle());

    if (!IsAdoptableTab(tab)) {
      continue;
    }
    const std::optional<LiveFolderTabKey> key = FindItemForUrl(space_id, url);
    if (!key || FindItemTab(*key) != TabStripModel::kNoTab) {
      continue;
    }
    MarkLiveFolderTab(contents, *key);
    adopted = true;
  }

  if (adopted) {
    ScheduleRebuild();
  }
}

void AvoraLiveFoldersView::ReleaseOrphanedTabs() {
  if (!tab_strip_model_ || !store_) {
    return;
  }
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(i);
    const std::optional<LiveFolderTabKey> key = GetLiveFolderTabKey(contents);
    if (!key) {
      continue;
    }
    bool has_row = false;
    if (const std::optional<LiveFolder> folder =
            store_->GetFolderById(key->space_id, key->folder_id)) {
      for (const LiveFolderItem& item : folder->ActiveItems()) {
        if (item.external_id == key->external_id) {
          has_row = true;
          break;
        }
      }
    }
    if (!has_row) {
      UnmarkLiveFolderTab(contents);
    }
  }
}

void AvoraLiveFoldersView::ReportActiveHost() {
  if (!service_ || !tab_strip_model_) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetActiveWebContents();
  if (!contents) {
    return;
  }
  service_->OnHostVisited(SpaceId(),
                          std::string(contents->GetLastCommittedURL().host()));
}

std::string AvoraLiveFoldersView::SpaceId() const {
  if (window_space_state_) {
    return window_space_state_->active_space_id();
  }
  // A window with no Space of its own shows the global default, as its new
  // tabs do.
  if (PrefService* prefs = browser_->GetProfile()->GetPrefs()) {
    if (const Space* space = SpaceManager(prefs).GetActiveSpace()) {
      return space->id;
    }
  }
  return std::string();
}

void AvoraLiveFoldersView::UpdateWatchedSpace() {
  if (!service_) {
    return;
  }
  const std::string space_id = SpaceId();
  if (space_id == watched_space_id_) {
    return;
  }
  // Watch the new Space before releasing the old one, so a Space this window
  // merely re-enters never drops to zero watchers in between.
  service_->WatchSpace(space_id);
  if (!watched_space_id_.empty()) {
    service_->UnwatchSpace(watched_space_id_);
  }
  watched_space_id_ = space_id;
}

void AvoraLiveFoldersView::OnLiveFoldersChanged() {
  ScheduleRebuild();
}

void AvoraLiveFoldersView::OnWindowActiveSpaceChanged(
    const std::string& space_id) {
  UpdateWatchedSpace();
  ScheduleRebuild();
}

void AvoraLiveFoldersView::OnLiveFolderSyncStateChanged(
    const std::string& space_id,
    const std::string& folder_id) {
  if (space_id == SpaceId()) {
    ScheduleRebuild();
  }
}

void AvoraLiveFoldersView::ScheduleRebuild() {
  if (rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<AvoraLiveFoldersView> view) {
                       if (!view) {
                         return;
                       }
                       view->rebuild_pending_ = false;
                       view->Rebuild();
                     },
                     weak_factory_.GetWeakPtr()));
}

gfx::Size AvoraLiveFoldersView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  int height = 0;
  for (const views::View* child : children()) {
    if (child->GetVisible()) {
      height += child->GetPreferredSize().height();
    }
  }
  return gfx::Size(available_size.width().is_bounded()
                       ? available_size.width().value()
                       : 0,
                   height);
}

void AvoraLiveFoldersView::Rebuild() {
  RemoveAllChildViews();
  ReleaseOrphanedTabs();

  // Each window shows its own Space's folders, never the global default's.
  const std::string space_id = SpaceId();
  const std::vector<LiveFolder> folders =
      store_ ? store_->GetFolders(space_id) : std::vector<LiveFolder>();

  if (folders.empty()) {
    InvalidateLayout();
    return;
  }

  for (const LiveFolder& folder : folders) {
    const LiveFolderService::SyncState state =
        service_ ? service_->GetSyncState(space_id, folder.id)
                 : LiveFolderService::SyncState();

    AddChildView(std::make_unique<LiveFolderHeaderRow>(
        folder.id, folder.name, folder.expanded, state.syncing,
        folder.UnseenCount(),
        base::BindRepeating(&AvoraLiveFoldersView::OnFolderToggle,
                            weak_factory_.GetWeakPtr()),
        base::BindRepeating(&AvoraLiveFoldersView::OnFolderContextMenu,
                            weak_factory_.GetWeakPtr())));

    const int active_index = tab_strip_model_ ? tab_strip_model_->active_index()
                                              : TabStripModel::kNoTab;
    const std::vector<LiveFolderItem> items = folder.ActiveItems();
    for (const LiveFolderItem& item : items) {
      const int tab_index =
          FindItemTab(LiveFolderTabKey{space_id, folder.id, item.external_id});
      const bool tab_open = tab_index != TabStripModel::kNoTab;
      const bool tab_active = tab_open && tab_index == active_index;

      // A collapsed folder still shows the pull request being looked at, as
      // a pinned folder does, so the selected tab never has no row.
      if (!folder.expanded && !tab_active) {
        continue;
      }
      AddChildView(std::make_unique<LiveFolderItemRow>(
          folder.id, item, tab_active,
          base::BindRepeating(&AvoraLiveFoldersView::OnItemClicked,
                              weak_factory_.GetWeakPtr()),
          tab_open ? base::BindRepeating(&AvoraLiveFoldersView::CloseItemTab,
                                         weak_factory_.GetWeakPtr())
                   : LiveFolderItemRow::ItemCallback(),
          base::BindRepeating(&AvoraLiveFoldersView::OnItemContextMenu,
                              weak_factory_.GetWeakPtr())));
    }

    if (!folder.expanded) {
      continue;
    }

    // Organisations that enforce single sign-on keep their pull requests out
    // of the results until this Space has been through their identity
    // provider.  Without saying so, the folder would just look incomplete.
    for (const LiveFolderProtectedSource& source : state.protected_sources) {
      AddChildView(std::make_unique<MessageRow>(
          base::UTF8ToUTF16(
              base::StrCat({"Sign in to ", source.name, " (SSO)"})),
          kBadgeColor,
          base::BindRepeating(&AvoraLiveFoldersView::OpenUrl,
                              weak_factory_.GetWeakPtr(),
                              source.sign_in_url.spec())));
    }

    if (!items.empty()) {
      continue;
    }

    // No rows.  Failures get a message, because a broken folder and an empty
    // one would otherwise look identical.  Signing out clears the rows, so a
    // signed-out folder always lands here and offers the sign-in.  A
    // successful sync that simply matched nothing gets no row at all: "you
    // have no pull requests" is the expected resting state, and a line of text
    // restating it is noise the user cannot act on.
    switch (state.last_status) {
      case LiveFolderFetchStatus::kAuthFailed:
      case LiveFolderFetchStatus::kNetworkError:
      case LiveFolderFetchStatus::kRateLimited:
        AddMessageRow(base::UTF8ToUTF16(state.last_error));
        break;
      case LiveFolderFetchStatus::kNotConnected:
        AddSignInRow(folder.id);
        break;
      case LiveFolderFetchStatus::kSuccess:
        if (folder.last_synced_at.is_null()) {
          AddMessageRow(u"Looking for pull requests…");
        }
        break;
    }
  }

  InvalidateLayout();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void AvoraLiveFoldersView::AddMessageRow(const std::u16string& text) {
  AddChildView(std::make_unique<MessageRow>(text, kSubtleColor,
                                            base::RepeatingClosure()));
}

void AvoraLiveFoldersView::AddSignInRow(const std::string& folder_id) {
  AddChildView(std::make_unique<MessageRow>(
      u"Sign in to GitHub", kBadgeColor,
      base::BindRepeating(&AvoraLiveFoldersView::OpenSignIn,
                          weak_factory_.GetWeakPtr(), folder_id)));
}

// ── Actions ─────────────────────────────────────────────────────────────────

void AvoraLiveFoldersView::OnFolderToggle(const std::string& folder_id) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder =
      store_->GetFolderById(SpaceId(), folder_id);
  if (!folder) {
    return;
  }
  store_->SetExpanded(SpaceId(), folder_id, !folder->expanded);

  // Opening a folder is the moment its contents matter, so take the chance to
  // refresh -- subject to the service's minimum spacing.
  if (!folder->expanded && service_) {
    service_->RefreshFolder(SpaceId(), folder_id);
  }
}

void AvoraLiveFoldersView::OnItemClicked(const std::string& folder_id,
                                         const std::string& external_id) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder =
      store_->GetFolderById(SpaceId(), folder_id);
  if (!folder) {
    return;
  }
  for (const LiveFolderItem& item : folder->items) {
    if (item.external_id == external_id) {
      store_->MarkItemSeen(SpaceId(), folder_id, external_id);
      OpenItem(folder_id, external_id);
      return;
    }
  }
}

void AvoraLiveFoldersView::OpenItem(const std::string& folder_id,
                                    const std::string& external_id) {
  if (!browser_ || !tab_strip_model_ || !store_) {
    return;
  }
  const std::string space_id = SpaceId();
  const std::optional<LiveFolder> folder =
      store_->GetFolderById(space_id, folder_id);
  if (!folder) {
    return;
  }
  GURL url;
  for (const LiveFolderItem& item : folder->items) {
    if (item.external_id == external_id) {
      url = GURL(item.url);
      break;
    }
  }
  if (!url.is_valid()) {
    return;
  }
  const LiveFolderTabKey key{space_id, folder_id, external_id};

  // The row's own tab.  Clicking the selected row after browsing away from
  // the pull request takes the tab back to it, as a pinned row's return
  // control does; Back still has the page that was left.
  if (const int index = FindItemTab(key); index != TabStripModel::kNoTab) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
    if (index == tab_strip_model_->active_index() &&
        UrlForMatching(contents) != url) {
      contents->GetController().LoadURL(url, content::Referrer(),
                                        ui::PAGE_TRANSITION_AUTO_BOOKMARK,
                                        std::string());
      return;
    }
    tab_strip_model_->ActivateTabAt(index);
    return;
  }

  // A daily tab of this Space already showing the pull request becomes the
  // row's tab, rather than leaving two tabs open on one page.
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(i);
    if (IsAdoptableTab(tab) && GetTabSpaceId(tab->GetContents()) == space_id &&
        UrlForMatching(tab->GetContents()) == url) {
      MarkLiveFolderTab(tab->GetContents(), key);
      tab_strip_model_->ActivateTabAt(i);
      return;
    }
  }

  NavigateParams params(browser_, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  if (content::WebContents* contents = params.navigated_or_inserted_contents) {
    SetTabSpaceId(contents, space_id);
    MarkLiveFolderTab(contents, key);
  }
}

void AvoraLiveFoldersView::CloseItemTab(const std::string& folder_id,
                                        const std::string& external_id) {
  if (!tab_strip_model_) {
    return;
  }
  const int index =
      FindItemTab(LiveFolderTabKey{SpaceId(), folder_id, external_id});
  if (index == TabStripModel::kNoTab) {
    return;
  }
  tab_strip_model_->CloseWebContentsAt(
      index, TabCloseTypes::CLOSE_USER_GESTURE |
                 TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
}

void AvoraLiveFoldersView::OpenUrl(const std::string& url) {
  if (url.empty() || !browser_) {
    return;
  }

  // Reuse an existing tab so clicking the same pull request twice does not
  // accumulate duplicates, matching the pinned section's behaviour.
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  if (tab_strip) {
    for (int i = 0; i < tab_strip->count(); ++i) {
      content::WebContents* contents = tab_strip->GetWebContentsAt(i);
      if (contents && contents->GetLastCommittedURL().spec() == url) {
        tab_strip->ActivateTabAt(i);
        return;
      }
    }
  }

  NavigateParams params(browser_, GURL(url), ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void AvoraLiveFoldersView::StartGitHubSignInFlow() {
  if (!store_) {
    return;
  }
  const std::string folder_id =
      store_->EnsureGitHubPullRequestsFolder(SpaceId());
  if (folder_id.empty()) {
    return;
  }
  if (service_ && service_->GetSyncState(SpaceId(), folder_id).connected) {
    service_->ForceRefreshFolder(SpaceId(), folder_id);
    return;
  }
  OpenSignIn(folder_id);
}

void AvoraLiveFoldersView::OpenSignIn(const std::string& folder_id) {
  if (!service_) {
    return;
  }
  // There is nothing to connect: signing in on github.com, in this Space, is
  // the whole flow.  The service notices the new session when the tab lands
  // back on github.com and fills the folder.
  const GURL url =
      service_->GetSignInUrl(SpaceId(), LiveFolderProviderId::kGitHub);
  if (url.is_valid()) {
    OpenUrl(url.spec());
  }
}

void AvoraLiveFoldersView::ToggleQueryFilter(const std::string& folder_id,
                                             int command_id) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder =
      store_->GetFolderById(SpaceId(), folder_id);
  if (!folder) {
    return;
  }

  LiveFolderQuery query = folder->query;
  switch (command_id) {
    case kMenuFilterCreatedId:
      query.created_by_me = !query.created_by_me;
      break;
    case kMenuFilterAssignedId:
      query.assigned_to_me = !query.assigned_to_me;
      break;
    case kMenuFilterReviewId:
      query.review_requested = !query.review_requested;
      break;
    case kMenuFilterMentionedId:
      query.mentioned = !query.mentioned;
      break;
    case kMenuFilterTeamReviewId:
      query.team_review_requested = !query.team_review_requested;
      break;
    case kMenuFilterDraftsId:
      query.include_drafts = !query.include_drafts;
      break;
    default:
      return;
  }

  store_->SetQuery(SpaceId(), folder_id, query);
  if (service_) {
    service_->ForceRefreshFolder(SpaceId(), folder_id);
  }
}

// ── Context menus ───────────────────────────────────────────────────────────

void AvoraLiveFoldersView::OnFolderContextMenu(
    const std::string& folder_id,
    const gfx::Point& screen_point) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder =
      store_->GetFolderById(SpaceId(), folder_id);
  if (!folder) {
    return;
  }

  const bool connected =
      service_ && service_->GetSyncState(SpaceId(), folder_id).connected;

  context_menu_delegate_ = std::make_unique<CallbackMenuDelegate>(
      base::BindRepeating(
          [](base::WeakPtr<AvoraLiveFoldersView> view,
             const std::string& folder_id, int command_id) {
            if (!view) {
              return;
            }
            switch (command_id) {
              case kMenuRefreshId:
                if (view->service_) {
                  view->service_->ForceRefreshFolder(view->SpaceId(),
                                                     folder_id);
                }
                break;
              case kMenuSignInId:
                view->OpenSignIn(folder_id);
                break;
              case kMenuRemoveFolderId:
                if (view->store_) {
                  view->store_->RemoveFolder(view->SpaceId(), folder_id);
                }
                break;
              default:
                view->ToggleQueryFilter(folder_id, command_id);
                break;
            }
          },
          weak_factory_.GetWeakPtr(), folder_id),
      base::BindRepeating(
          [](LiveFolderQuery query, int command_id) {
            switch (command_id) {
              case kMenuFilterCreatedId:
                return query.created_by_me;
              case kMenuFilterAssignedId:
                return query.assigned_to_me;
              case kMenuFilterReviewId:
                return query.review_requested;
              case kMenuFilterMentionedId:
                return query.mentioned;
              case kMenuFilterTeamReviewId:
                return query.team_review_requested;
              case kMenuFilterDraftsId:
                return query.include_drafts;
              default:
                return false;
            }
          },
          folder->query));

  filter_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  filter_menu_model_->AddCheckItem(kMenuFilterCreatedId, u"Created by Me");
  filter_menu_model_->AddCheckItem(kMenuFilterAssignedId, u"Assigned to Me");
  filter_menu_model_->AddCheckItem(kMenuFilterReviewId, u"Review Requested");
  filter_menu_model_->AddCheckItem(kMenuFilterTeamReviewId,
                                   u"Team Review Requested");
  filter_menu_model_->AddCheckItem(kMenuFilterMentionedId, u"Mentions Me");
  filter_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  filter_menu_model_->AddCheckItem(kMenuFilterDraftsId, u"Include Drafts");

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  if (connected) {
    context_menu_model_->AddItem(kMenuRefreshId, u"Refresh Now");
    context_menu_model_->AddSubMenu(0, u"Filter", filter_menu_model_.get());
  } else {
    context_menu_model_->AddItem(kMenuSignInId, u"Sign in to GitHub");
  }
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(kMenuRemoveFolderId, u"Remove Folder");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
}

void AvoraLiveFoldersView::OnItemContextMenu(const std::string& folder_id,
                                             const std::string& external_id,
                                             const gfx::Point& screen_point) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder =
      store_->GetFolderById(SpaceId(), folder_id);
  if (!folder) {
    return;
  }

  std::string url;
  for (const LiveFolderItem& item : folder->items) {
    if (item.external_id == external_id) {
      url = item.url;
      break;
    }
  }
  if (url.empty()) {
    return;
  }

  context_menu_delegate_ = std::make_unique<CallbackMenuDelegate>(
      base::BindRepeating(
          [](base::WeakPtr<AvoraLiveFoldersView> view,
             const std::string& folder_id, const std::string& external_id,
             int command_id) {
            if (!view) {
              return;
            }
            if (command_id == kMenuOpenItemId) {
              view->OpenItem(folder_id, external_id);
            }
          },
          weak_factory_.GetWeakPtr(), folder_id, external_id),
      base::RepeatingCallback<bool(int)>());

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kMenuOpenItemId, u"Open Pull Request");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
}

BEGIN_METADATA(AvoraLiveFoldersView)
END_METADATA

}  // namespace avora
