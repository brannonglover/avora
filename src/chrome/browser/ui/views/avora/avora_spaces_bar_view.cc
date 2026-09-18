// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_spaces_bar_view.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_space_icons.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_storage_partition.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/avora/avora_downloads_button.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "chrome/browser/ui/views/avora/avora_pinned_section_view.h"
#include "chrome/browser/ui/views/avora/avora_sidebar_view.h"
#include "chrome/browser/ui/views/avora/avora_space_icon_picker.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr SkColor kLegacyBarBg = SkColorSetARGB(0xFF, 0x12, 0x14, 0x1A);
constexpr int kLegacyBarHeight = 36;
constexpr int kLegacyPillHeight = 26;
constexpr int kLegacyPillCornerRadius = 13;

constexpr SkColor kBarBg = SkColorSetRGB(0x14, 0x16, 0x1C);

// Nothing in the strip carries a resting background: an icon's own accent
// colour is the only fill in the bar.  This is the transient wash drawn
// under the cursor alone, faint enough to read as a hover state rather than
// as a chip the icon permanently sits in.
constexpr SkColor kIconHoverBg = SkColorSetARGB(0x14, 0xFF, 0xFF, 0xFF);
constexpr SkColor kDimText = SkColorSetARGB(0xB0, 0xED, 0xF2, 0xF5);

// Lucide "plus", used for the create button.  Chrome UI icons live outside the
// Space catalog but still come from the same library.
constexpr char kPlusIconPath[] = "M5 12h14 M12 5v14";

constexpr int kManagedBarHeight = 40;
constexpr int kBarHPad = 8;
constexpr int kBarVPad = 4;
constexpr int kIconSize = 28;
constexpr int kIconGlyphSize = 18;
constexpr int kIconCornerRadius = 8;
constexpr int kIconGap = 4;
constexpr int kAddBtnSize = 28;
constexpr int kAddGlyphSize = 16;

// With no background behind it, the active Space is called out by its icon
// alone: full-strength accent, a heavier stroke, and an accent underline.
// Icons of Spaces this window isn't showing are dimmed rather than greyed, so
// the strip still reads as a row of accent colours.
constexpr SkAlpha kInactiveIconAlpha = 0x8C;
constexpr float kActiveStrokeWidth = 2.4f;
constexpr int kActiveUnderlineWidth = 12;
constexpr int kActiveUnderlineHeight = 3;

constexpr int kMenuEdit = 1;
constexpr int kMenuDelete = 2;
constexpr int kMenuNewSpace = 3;
constexpr int kMenuNewFolder = 4;

constexpr int kMenuIconSize = 16;

// Lucide ids for the "+" menu.  "layout-grid" is also the fallback Space
// icon, so a new Space's menu entry looks like the Space it creates.
constexpr char kNewSpaceMenuIconId[] = "layout-grid";
constexpr char kNewFolderMenuIconId[] = "folder";

void RemovePinnedDataForSpace(PrefService* prefs, const std::string& space_id) {
  if (!prefs || space_id.empty()) {
    return;
  }
  {
    ScopedDictPrefUpdate folders(
        prefs, avora::PinnedFoldersManager::kPinnedFoldersBySpacePref);
    folders->Remove(space_id);
  }
  {
    ScopedDictPrefUpdate titles(
        prefs, avora::PinnedFoldersManager::kPinnedTabTitlesBySpacePref);
    titles->Remove(space_id);
  }
}

void FillRoundRect(gfx::Canvas* canvas,
                   const gfx::Rect& bounds,
                   SkColor color,
                   int radius) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);
  canvas->DrawRoundRect(bounds, radius, flags);
}

// A Space in the strip: its Lucide icon, stroked in the Space's accent colour.
class SpaceIconButton : public views::Button {
  METADATA_HEADER(SpaceIconButton, views::Button)

 public:
  SpaceIconButton(PressedCallback callback,
                  base::RepeatingCallback<void(const std::string&,
                                               const gfx::Point&)>
                      context_callback,
                  const std::string& space_id,
                  const std::string& icon_id,
                  SkColor accent,
                  bool is_active)
      : views::Button(std::move(callback)),
        context_callback_(std::move(context_callback)),
        space_id_(space_id),
        icon_id_(icon_id),
        accent_(accent),
        is_active_(is_active) {
    SetPreferredSize(gfx::Size(kIconSize, kIconSize));
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (event.IsRightMouseButton()) {
      gfx::Point screen_point = event.location();
      ConvertPointToScreen(this, &screen_point);
      if (context_callback_) {
        context_callback_.Run(space_id_, screen_point);
      }
      return true;
    }
    return views::Button::OnMousePressed(event);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      FillRoundRect(canvas, GetLocalBounds(), kIconHoverBg, kIconCornerRadius);
    }

    // The glyph sits above the underline rather than centred in the button,
    // so active and inactive icons stay on the same baseline.
    gfx::Rect glyph(GetLocalBounds());
    glyph.Inset(gfx::Insets::TLBR(0, 0, kActiveUnderlineHeight, 0));
    glyph.ClampToCenteredSize(gfx::Size(kIconGlyphSize, kIconGlyphSize));
    avora::PaintLucideIcon(
        canvas, glyph, icon_id_,
        is_active_ ? accent_ : SkColorSetA(accent_, kInactiveIconAlpha),
        is_active_ ? kActiveStrokeWidth : avora::kLucideStrokeWidth);

    if (is_active_) {
      gfx::Rect underline(
          GetLocalBounds().CenterPoint().x() - kActiveUnderlineWidth / 2,
          height() - kActiveUnderlineHeight, kActiveUnderlineWidth,
          kActiveUnderlineHeight);
      FillRoundRect(canvas, underline, accent_, kActiveUnderlineHeight / 2);
    }
  }

 private:
  base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
      context_callback_;
  std::string space_id_;
  std::string icon_id_;
  SkColor accent_;
  bool is_active_ = false;
};

BEGIN_METADATA(SpaceIconButton)
END_METADATA

class CreateSpaceButton : public views::Button {
  METADATA_HEADER(CreateSpaceButton, views::Button)

 public:
  explicit CreateSpaceButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(kAddBtnSize, kAddBtnSize));
    SetTooltipText(u"New Space or Folder");
    GetViewAccessibility().SetName(u"New");
    GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      FillRoundRect(canvas, GetLocalBounds(), kIconHoverBg, kIconCornerRadius);
    }

    gfx::Rect glyph(GetLocalBounds());
    glyph.ClampToCenteredSize(gfx::Size(kAddGlyphSize, kAddGlyphSize));
    avora::PaintLucidePathData(canvas, glyph, kPlusIconPath, kDimText);
  }
};

BEGIN_METADATA(CreateSpaceButton)
END_METADATA

std::unique_ptr<views::View> MakeSpacer(int width) {
  auto spacer = std::make_unique<views::View>();
  spacer->SetPreferredSize(gfx::Size(width, 0));
  return spacer;
}

// Whether |point| lands on a button anywhere under |root|.  The bar nests its
// Space icons inside a centring container, so this has to recurse rather than
// look at direct children alone.
bool HitsButton(views::View* root, const gfx::Point& point) {
  for (views::View* child : root->children()) {
    if (!child->GetVisible()) {
      continue;
    }
    gfx::Point point_in_child = point;
    views::View::ConvertPointToTarget(root, child, &point_in_child);
    if (!child->HitTestPoint(point_in_child)) {
      continue;
    }
    if (views::AsViewClass<views::Button>(child)) {
      return true;
    }
    if (HitsButton(child, point_in_child)) {
      return true;
    }
  }
  return false;
}

class SpaceBarContextMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  SpaceBarContextMenuDelegate(base::RepeatingClosure on_edit,
                              base::RepeatingClosure on_delete,
                              bool can_delete)
      : on_edit_(std::move(on_edit)),
        on_delete_(std::move(on_delete)),
        can_delete_(can_delete) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kMenuEdit && on_edit_) {
      on_edit_.Run();
    } else if (command_id == kMenuDelete && can_delete_ && on_delete_) {
      on_delete_.Run();
    }
  }

  bool IsCommandIdEnabled(int command_id) const override {
    return command_id != kMenuDelete || can_delete_;
  }

 private:
  base::RepeatingClosure on_edit_;
  base::RepeatingClosure on_delete_;
  bool can_delete_ = true;
};

// Delegate for the "+" button's menu.  Unlike the Space context menu every
// entry here creates something, so there is nothing to disable: the model is
// a pair of closures.
class CreateMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  CreateMenuDelegate(base::RepeatingClosure on_new_space,
                     base::RepeatingClosure on_new_folder)
      : on_new_space_(std::move(on_new_space)),
        on_new_folder_(std::move(on_new_folder)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kMenuNewSpace && on_new_space_) {
      on_new_space_.Run();
    } else if (command_id == kMenuNewFolder && on_new_folder_) {
      on_new_folder_.Run();
    }
  }

 private:
  base::RepeatingClosure on_new_space_;
  base::RepeatingClosure on_new_folder_;
};

// The first descendant of |root| that is a |T|, or nullptr.  Sibling views
// owned by other parts of the window (the pinned section, the sidebar) are
// reached this way rather than threaded through BrowserView.
template <typename T>
T* FindDescendantOfType(views::View* root) {
  if (!root) {
    return nullptr;
  }
  std::vector<views::View*> stack;
  stack.push_back(root);
  while (!stack.empty()) {
    views::View* view = stack.back();
    stack.pop_back();
    if (auto* found = views::AsViewClass<T>(view)) {
      return found;
    }
    for (views::View* child : view->children()) {
      stack.push_back(child);
    }
  }
  return nullptr;
}

}  // namespace

AvoraSpacesBarView::AvoraSpacesBarView(SpaceClickedCallback clicked_cb)
    : clicked_cb_(std::move(clicked_cb)) {
  SetBackground(views::CreateSolidBackground(kLegacyBarBg));
  SetPreferredSize(gfx::Size(0, kLegacyBarHeight));
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(5, 8), 6));
}

AvoraSpacesBarView::~AvoraSpacesBarView() {
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
  }
  if (profile_store_) {
    profile_store_->RemoveObserver(this);
  }
}

void AvoraSpacesBarView::AddedToWidget() {
  views::View::AddedToWidget();
  TryAutoConnectSpaceManager();
}

void AvoraSpacesBarView::TryAutoConnectSpaceManager() {
  if (space_manager_) {
    return;
  }
  auto* widget = GetWidget();
  if (!widget) {
    return;
  }
  AvoraSidebarView* sidebar =
      FindDescendantOfType<AvoraSidebarView>(widget->GetRootView());
  if (sidebar && sidebar->profile()) {
    ConnectToProfile(sidebar->profile());
  }
}

void AvoraSpacesBarView::SetSpaces(const std::vector<SpaceInfo>& spaces) {
  if (space_manager_) {
    return;
  }
  legacy_spaces_ = spaces;
  RebuildLegacy();
}

void AvoraSpacesBarView::RebuildLegacy() {
  RemoveAllChildViews();
  space_buttons_.clear();
  create_button_ = nullptr;
  downloads_button_ = nullptr;
  for (const auto& space : legacy_spaces_) {
    auto btn = std::make_unique<views::LabelButton>(
        base::BindRepeating(
            [](SpaceClickedCallback cb, const std::string& id) {
              cb.Run(id);
            },
            clicked_cb_, space.id),
        base::UTF8ToUTF16(space.name));
    btn->SetPreferredSize(gfx::Size(0, kLegacyPillHeight));
    SkColor bg = space.is_active ? space.color
                                 : SkColorSetA(space.color, 0x33);
    btn->SetBackground(
        views::CreateRoundedRectBackground(bg, kLegacyPillCornerRadius));
    btn->SetEnabledTextColors(SK_ColorWHITE);
    AddChildView(std::move(btn));
  }
}

void AvoraSpacesBarView::ConnectToProfile(Profile* profile) {
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
    space_manager_.reset();
  }
  if (profile_store_) {
    profile_store_->RemoveObserver(this);
    profile_store_.reset();
  }

  profile_ = profile;
  if (profile && profile->GetPrefs()) {
    space_manager_ =
        std::make_unique<avora::SpaceManager>(profile->GetPrefs());
    space_manager_->AddObserver(this);

    profile_store_ =
        std::make_unique<avora::BrowserProfileStore>(profile->GetPrefs());
    profile_store_->AddObserver(this);

    SetBackground(views::CreateSolidBackground(kBarBg));
    SetPreferredSize(gfx::Size(0, kManagedBarHeight));
    auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(kBarVPad, kBarHPad), 0));
    root_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    RebuildManaged();
  }
}

void AvoraSpacesBarView::SetBrowser(BrowserWindowInterface* browser) {
  browser_ = browser;
  if (space_manager_) {
    RebuildManaged();
  }
}

void AvoraSpacesBarView::SetWindowSpaceState(avora::WindowSpaceState* state) {
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
  window_space_state_ = state;
  if (window_space_state_) {
    window_space_state_->AddObserver(this);
  }
  if (space_manager_) {
    RebuildManaged();
  }
}

bool AvoraSpacesBarView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  return !HitsButton(const_cast<AvoraSpacesBarView*>(this), point);
}

void AvoraSpacesBarView::OnSpacesChanged() {
  if (space_manager_) {
    RebuildManaged();
  }
}

void AvoraSpacesBarView::OnActiveSpaceChanged(const std::string& space_id) {
  // Global pref-based active space change.  Ignored when a per-window
  // WindowSpaceState drives the active highlight (see
  // OnWindowActiveSpaceChanged).
  if (window_space_state_) {
    return;
  }
  if (space_manager_) {
    RebuildManaged();
  }
}

void AvoraSpacesBarView::OnWindowActiveSpaceChanged(
    const std::string& space_id) {
  if (space_manager_) {
    RebuildManaged();
  }
}

void AvoraSpacesBarView::OnBrowserProfilesChanged() {
  if (space_manager_) {
    RebuildManaged();
  }
}

const avora::Space* AvoraSpacesBarView::GetActiveSpace() const {
  if (!space_manager_) {
    return nullptr;
  }
  if (window_space_state_) {
    if (const avora::Space* active = space_manager_->GetSpaceById(
            window_space_state_->active_space_id())) {
      return active;
    }
  }
  return space_manager_->GetActiveSpace();
}

std::u16string AvoraSpacesBarView::IdentityTooltipLine(
    const std::string& profile_id) const {
  // Under the one-Space-one-identity rule the identity's name always matches
  // the Space's, so naming it again would just repeat the line above.  What
  // the user cannot otherwise see is whether this Space has a cookie jar to
  // itself, which is what the default identity does not give them.
  bool isolated = false;
  if (profile_store_) {
    const avora::BrowserProfile* profile =
        profile_store_->GetProfileById(profile_id);
    isolated = profile && !avora::UsesDefaultPartition(*profile);
  }

  return isolated ? u"\nSigned in separately"
                  : u"\nShares the default browser session";
}

void AvoraSpacesBarView::RebuildManaged() {
  RemoveAllChildViews();
  space_buttons_.clear();
  create_button_ = nullptr;
  downloads_button_ = nullptr;

  if (!space_manager_) {
    return;
  }

  auto* row_layout = static_cast<views::BoxLayout*>(GetLayoutManager());
  const avora::Space* active = GetActiveSpace();

  // The icon strip is centred in the bar, so it needs to be balanced against
  // the "+" pinned to the right edge.  Downloads take that leading slot: the
  // same width, so the Space icons stay centred, and the far edge from the
  // "+" so creating and finding things sit at opposite ends of the bar.
  if (profile_) {
    downloads_button_ = AddChildView(
        std::make_unique<avora::DownloadsButton>(profile_, browser_));
  } else {
    AddChildView(MakeSpacer(kAddBtnSize));
  }
  views::View* leading_flex = AddChildView(MakeSpacer(0));

  auto icons = std::make_unique<views::View>();
  auto* icons_layout = icons->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kIconGap));
  icons_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  for (const auto& space : space_manager_->GetSpaces()) {
    const std::u16string tooltip = base::UTF8ToUTF16(space.name);
    auto button = std::make_unique<SpaceIconButton>(
        base::BindRepeating(&AvoraSpacesBarView::OnSpaceClicked,
                            base::Unretained(this), space.id),
        base::BindRepeating(&AvoraSpacesBarView::OnSpaceContextMenu,
                            base::Unretained(this)),
        space.id, space.icon, space.AccentColor(),
        active && space.id == active->id);
    button->SetTooltipText(
        base::StrCat({tooltip, IdentityTooltipLine(space.profile_id)}));
    button->GetViewAccessibility().SetName(base::UTF8ToUTF16(space.name));
    space_buttons_[space.id] = icons->AddChildView(std::move(button));
  }

  AddChildView(std::move(icons));
  views::View* trailing_flex = AddChildView(MakeSpacer(0));

  create_button_ = AddChildView(std::make_unique<CreateSpaceButton>(
      base::BindRepeating(&AvoraSpacesBarView::OnCreateSpaceClicked,
                          base::Unretained(this))));

  if (row_layout) {
    row_layout->SetFlexForView(leading_flex, 1);
    row_layout->SetFlexForView(trailing_flex, 1);
  }

  InvalidateLayout();
}

void AvoraSpacesBarView::OnSpaceClicked(const std::string& id) {
  if (!space_manager_) {
    if (clicked_cb_) {
      clicked_cb_.Run(id);
    }
    return;
  }

  // Clicking the Space this window is already showing opens its editor, which
  // is where the name, icon, and accent colour live.
  const avora::Space* active = GetActiveSpace();
  if (active && active->id == id) {
    ShowEditSpaceEditor(id);
    return;
  }

  // Switch this window's active space (window-local, not global pref).
  if (window_space_state_) {
    window_space_state_->SetActiveSpaceId(id);
  } else {
    space_manager_->ActivateSpace(id);
  }
  if (clicked_cb_) {
    clicked_cb_.Run(id);
  }
}

void AvoraSpacesBarView::OnSpaceContextMenu(const std::string& id,
                                            const gfx::Point& screen_point) {
  ShowSpaceContextMenu(id, screen_point);
}

void AvoraSpacesBarView::ShowSpaceContextMenu(const std::string& space_id,
                                              const gfx::Point& screen_point) {
  if (!space_manager_ || space_id.empty() ||
      !space_manager_->GetSpaceById(space_id)) {
    return;
  }

  const bool can_delete = space_manager_->GetSpaces().size() > 1;

  // No identity picker: a Space owns its identity, so there is nothing to
  // choose between.  Creating a Space is what creates an identity.
  context_menu_delegate_ = std::make_unique<SpaceBarContextMenuDelegate>(
      base::BindRepeating(&AvoraSpacesBarView::ShowEditSpaceEditor,
                          base::Unretained(this), space_id),
      base::BindRepeating(&AvoraSpacesBarView::ConfirmDeleteSpace,
                          base::Unretained(this), space_id),
      can_delete);
  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kMenuEdit, u"Edit Space…");
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(kMenuDelete, u"Delete Space…");

  menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(GetWidget(), nullptr,
                          gfx::Rect(screen_point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kMouse);
}

void AvoraSpacesBarView::OnCreateSpaceClicked() {
  ShowCreateMenu();
}

void AvoraSpacesBarView::ShowCreateMenu() {
  if (!create_button_) {
    return;
  }

  create_menu_delegate_ = std::make_unique<CreateMenuDelegate>(
      base::BindRepeating(&AvoraSpacesBarView::ShowCreateSpaceEditor,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&AvoraSpacesBarView::CreatePinnedFolder,
                          weak_factory_.GetWeakPtr()));

  create_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(create_menu_delegate_.get());
  create_menu_model_->AddItemWithIcon(
      kMenuNewSpace, u"New Space",
      avora::LucideIconImageModel(kNewSpaceMenuIconId, kMenuIconSize,
                                  kDimText));
  create_menu_model_->AddItemWithIcon(
      kMenuNewFolder, u"New Folder",
      avora::LucideIconImageModel(kNewFolderMenuIconId, kMenuIconSize,
                                  kDimText));

  // The bar sits at the bottom of the sidebar, so the menu rises from the
  // "+" rather than dropping off the window; kBubbleTopRight only falls back
  // to opening downwards when there is no room above on the display.
  menu_runner_ = std::make_unique<views::MenuRunner>(
      create_menu_model_.get(), views::MenuRunner::NO_FLAGS);
  menu_runner_->RunMenuAt(GetWidget(), nullptr,
                          create_button_->GetBoundsInScreen(),
                          views::MenuAnchorPosition::kBubbleTopRight,
                          ui::mojom::MenuSourceType::kMouse);
}

void AvoraSpacesBarView::CreatePinnedFolder() {
  views::Widget* widget = GetWidget();
  if (!widget) {
    return;
  }
  // Folders belong to the pinned section, which owns the Space-scoped
  // PinnedFoldersManager and the row that inline rename runs on.
  auto* pinned_section = FindDescendantOfType<avora::AvoraPinnedSectionView>(
      widget->GetRootView());
  if (pinned_section) {
    pinned_section->CreateFolderAndBeginRename();
  }
}

void AvoraSpacesBarView::ShowCreateSpaceEditor() {
  if (!space_manager_ || !create_button_) {
    return;
  }

  const size_t index = space_manager_->GetSpaces().size();
  avora::SpaceEditorFields fields;
  fields.name = "Space " + std::to_string(index + 1);
  fields.icon = avora::NextDefaultSpaceIconId(index);
  fields.accent_color = avora::NextDefaultSpaceAccentColor(index);

  avora::ShowSpaceEditorBubble(
      create_button_, u"New Space", u"Create", fields,
      base::BindOnce(&AvoraSpacesBarView::OnCreateSpaceCommitted,
                     weak_factory_.GetWeakPtr()));
}

void AvoraSpacesBarView::ShowEditSpaceEditor(const std::string& space_id) {
  if (!space_manager_) {
    return;
  }
  const avora::Space* space = space_manager_->GetSpaceById(space_id);
  if (!space) {
    return;
  }

  avora::SpaceEditorFields fields;
  fields.name = space->name;
  fields.icon = space->icon;
  fields.accent_color = space->accent_color;

  auto anchor = space_buttons_.find(space_id);
  views::View* anchor_view = anchor != space_buttons_.end()
                                  ? anchor->second.get()
                                  : create_button_.get();

  avora::ShowSpaceEditorBubble(
      anchor_view, u"Edit Space", u"Save", fields,
      base::BindOnce(&AvoraSpacesBarView::OnEditSpaceCommitted,
                     weak_factory_.GetWeakPtr(), space_id));
}

void AvoraSpacesBarView::OnCreateSpaceCommitted(
    const avora::SpaceEditorFields& fields) {
  if (!space_manager_) {
    return;
  }

  const std::string name =
      fields.name.empty()
          ? "Space " + std::to_string(space_manager_->GetSpaces().size() + 1)
          : fields.name;

  // SpaceManager mints the Space's identity as part of creating it, so the
  // |profile_id| argument is left empty here.
  const std::string new_id = space_manager_->CreateSpace(
      name, fields.icon, /*profile_id=*/std::string(), fields.accent_color);

  if (window_space_state_) {
    window_space_state_->SetActiveSpaceId(new_id);
  } else {
    space_manager_->ActivateSpace(new_id);
  }
  if (clicked_cb_) {
    clicked_cb_.Run(new_id);
  }
}

void AvoraSpacesBarView::OnEditSpaceCommitted(
    const std::string& space_id,
    const avora::SpaceEditorFields& fields) {
  if (!space_manager_) {
    return;
  }
  space_manager_->UpdateSpace(space_id, fields.name, fields.icon,
                              fields.accent_color);
}

void AvoraSpacesBarView::ConfirmDeleteSpace(const std::string& space_id) {
  if (!space_manager_ || space_manager_->GetSpaces().size() <= 1) {
    return;
  }
  const avora::Space* space = space_manager_->GetSpaceById(space_id);
  if (!space) {
    return;
  }

  const std::u16string body =
      u"Delete \"" + base::UTF8ToUTF16(space->name) +
      u"\"? Its favorites, pinned items, and sidebar tabs will be "
      u"removed, and the identity it browsed with is retired, so its "
      u"sign-ins go with it. Open browser tabs are not closed.";

  auto dialog_model =
      ui::DialogModel::Builder(std::make_unique<ui::DialogModelDelegate>())
          .SetTitle(u"Delete Space")
          .AddParagraph(ui::DialogModelLabel(body).set_is_secondary())
          .AddOkButton(base::BindOnce(&AvoraSpacesBarView::DeleteSpace,
                                      weak_factory_.GetWeakPtr(), space_id),
                       ui::DialogModel::Button::Params().SetLabel(u"Delete"))
          .AddCancelButton(base::DoNothing(),
                           ui::DialogModel::Button::Params().SetLabel(u"Cancel"))
          .Build();

  views::Widget* parent_widget = GetWidget();
  constrained_window::ShowBrowserModal(
      std::move(dialog_model),
      parent_widget ? parent_widget->GetNativeWindow() : gfx::NativeWindow());
}

void AvoraSpacesBarView::DeleteSpace(const std::string& space_id) {
  if (!space_manager_ || !profile_ || !profile_->GetPrefs()) {
    return;
  }
  if (space_manager_->GetSpaces().size() <= 1) {
    return;
  }

  avora::SidebarItemStore item_store(profile_->GetPrefs());
  item_store.RemoveItemsForSpace(space_id);
  RemovePinnedDataForSpace(profile_->GetPrefs(), space_id);
  // RemoveSpace retires the Space's identity too; see SpaceManager.

  const avora::Space* active = GetActiveSpace();
  const bool was_active = active && active->id == space_id;

  space_manager_->RemoveSpace(space_id);

  if (was_active) {
    // If we deleted the active space, the WindowSpaceState will get notified
    // via OnSpacesChanged and fall back to the first space.  Notify the
    // callback with whatever space ended up active.
    const std::string new_active_id =
        window_space_state_ ? window_space_state_->active_space_id()
                            : (space_manager_->GetActiveSpace()
                                   ? space_manager_->GetActiveSpace()->id
                                   : std::string());
    if (clicked_cb_ && !new_active_id.empty()) {
      clicked_cb_.Run(new_active_id);
    }
  }
}

BEGIN_METADATA(AvoraSpacesBarView)
END_METADATA
