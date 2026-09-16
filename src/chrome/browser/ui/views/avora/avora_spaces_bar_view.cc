// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_spaces_bar_view.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_space_icons.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "chrome/browser/ui/views/avora/avora_sidebar_view.h"
#include "chrome/browser/ui/views/avora/avora_space_icon_picker.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/metadata/metadata_utils.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr SkColor kLegacyBarBg = SkColorSetARGB(0xFF, 0x12, 0x14, 0x1A);
constexpr int kLegacyBarHeight = 36;
constexpr int kLegacyPillHeight = 26;
constexpr int kLegacyPillCornerRadius = 13;

constexpr SkColor kBarBg = SkColorSetRGB(0x14, 0x16, 0x1C);
constexpr SkColor kIconHoverBg = SkColorSetRGB(0x24, 0x28, 0x30);
constexpr SkColor kAddBtnBg = SkColorSetARGB(0x18, 0xFF, 0xFF, 0xFF);
constexpr SkColor kAddBtnHoverBg = SkColorSetARGB(0x30, 0xFF, 0xFF, 0xFF);
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

// Icons of Spaces this window isn't showing are dimmed rather than greyed, so
// the strip still reads as a row of accent colours.
constexpr SkAlpha kInactiveIconAlpha = 0xA0;
constexpr SkAlpha kActiveBackgroundAlpha = 0x33;

constexpr int kMenuEdit = 1;
constexpr int kMenuDelete = 2;
constexpr int kMenuNewProfile = 3;
constexpr int kMenuProfileBase = 200;

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
    if (is_active_) {
      FillRoundRect(canvas, GetLocalBounds(),
                    SkColorSetA(accent_, kActiveBackgroundAlpha),
                    kIconCornerRadius);
    } else if (GetState() == views::Button::STATE_HOVERED ||
               GetState() == views::Button::STATE_PRESSED) {
      FillRoundRect(canvas, GetLocalBounds(), kIconHoverBg, kIconCornerRadius);
    }

    gfx::Rect glyph(GetLocalBounds());
    glyph.ClampToCenteredSize(gfx::Size(kIconGlyphSize, kIconGlyphSize));
    avora::PaintLucideIcon(
        canvas, glyph, icon_id_,
        is_active_ ? accent_ : SkColorSetA(accent_, kInactiveIconAlpha));
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
    SetTooltipText(u"New Space");
    GetViewAccessibility().SetName(u"New Space");
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                         GetState() == views::Button::STATE_PRESSED;
    FillRoundRect(canvas, GetLocalBounds(),
                  hovered ? kAddBtnHoverBg : kAddBtnBg, kIconCornerRadius);

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

class ProfilePickerMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  ProfilePickerMenuDelegate(
      std::map<int, std::string> profile_command_ids,
      std::string active_profile_id,
      base::RepeatingCallback<void(const std::string&)> on_profile_selected,
      base::RepeatingClosure on_new_profile)
      : profile_command_ids_(std::move(profile_command_ids)),
        active_profile_id_(std::move(active_profile_id)),
        on_profile_selected_(std::move(on_profile_selected)),
        on_new_profile_(std::move(on_new_profile)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kMenuNewProfile && on_new_profile_) {
      on_new_profile_.Run();
      return;
    }
    auto it = profile_command_ids_.find(command_id);
    if (it != profile_command_ids_.end() && on_profile_selected_) {
      on_profile_selected_.Run(it->second);
    }
  }

  bool IsCommandIdChecked(int command_id) const override {
    auto it = profile_command_ids_.find(command_id);
    return it != profile_command_ids_.end() &&
           it->second == active_profile_id_;
  }

 private:
  std::map<int, std::string> profile_command_ids_;
  std::string active_profile_id_;
  base::RepeatingCallback<void(const std::string&)> on_profile_selected_;
  base::RepeatingClosure on_new_profile_;
};

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
  auto find_sidebar = [](views::View* root) -> AvoraSidebarView* {
    if (!root) {
      return nullptr;
    }
    std::vector<views::View*> stack;
    stack.push_back(root);
    while (!stack.empty()) {
      views::View* v = stack.back();
      stack.pop_back();
      if (auto* sidebar =
              ui::metadata::AsClass<AvoraSidebarView, views::View>(v)) {
        return sidebar;
      }
      for (views::View* child : v->children()) {
        stack.push_back(child);
      }
    }
    return nullptr;
  };

  auto* widget = GetWidget();
  if (!widget || !widget->GetRootView()) {
    return;
  }
  AvoraSidebarView* sidebar = find_sidebar(widget->GetRootView());
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

std::u16string AvoraSpacesBarView::ProfileDisplayName(
    const std::string& profile_id) const {
  if (!profile_store_) {
    return u"Default";
  }
  if (const avora::BrowserProfile* profile =
          profile_store_->GetProfileById(profile_id)) {
    return base::UTF8ToUTF16(profile->name);
  }
  return u"Default";
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

void AvoraSpacesBarView::RebuildManaged() {
  RemoveAllChildViews();
  space_buttons_.clear();
  create_button_ = nullptr;

  if (!space_manager_) {
    return;
  }

  auto* row_layout = static_cast<views::BoxLayout*>(GetLayoutManager());
  const avora::Space* active = GetActiveSpace();

  // The icon strip is centred in the bar, so it needs to be balanced against
  // the "+" pinned to the right edge: a spacer of the same width leads.
  AddChildView(MakeSpacer(kAddBtnSize));
  views::View* leading_flex = AddChildView(MakeSpacer(0));

  auto icons = std::make_unique<views::View>();
  auto* icons_layout = icons->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kIconGap));
  icons_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  for (const auto& space : space_manager_->GetSpaces()) {
    const std::u16string tooltip = base::StrCat(
        {base::UTF8ToUTF16(space.name), u"\nProfile: ",
         ProfileDisplayName(space.profile_id)});
    auto button = std::make_unique<SpaceIconButton>(
        base::BindRepeating(&AvoraSpacesBarView::OnSpaceClicked,
                            base::Unretained(this), space.id),
        base::BindRepeating(&AvoraSpacesBarView::OnSpaceContextMenu,
                            base::Unretained(this)),
        space.id, space.icon, space.AccentColor(),
        active && space.id == active->id);
    button->SetTooltipText(tooltip);
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
  if (!space_manager_ || !profile_store_ || space_id.empty()) {
    return;
  }

  const avora::Space* space = space_manager_->GetSpaceById(space_id);
  if (!space) {
    return;
  }

  const bool can_delete = space_manager_->GetSpaces().size() > 1;

  std::map<int, std::string> profile_command_ids;
  int next_profile_cmd = kMenuProfileBase;
  for (const auto& profile : profile_store_->GetProfiles()) {
    profile_command_ids[next_profile_cmd] = profile.id;
    ++next_profile_cmd;
  }
  profile_submenu_delegate_ = std::make_unique<ProfilePickerMenuDelegate>(
      profile_command_ids, space->profile_id,
      base::BindRepeating(
          [](avora::SpaceManager* manager, const std::string& id,
             const std::string& profile_id) {
            manager->SetSpaceProfile(id, profile_id);
          },
          base::Unretained(space_manager_.get()), space_id),
      base::BindRepeating(
          [](avora::BrowserProfileStore* store, avora::SpaceManager* manager,
             const std::string& space_id) {
            const auto profiles = store->GetProfiles();
            const std::string name =
                "Profile " + std::to_string(profiles.size() + 1);
            const std::string new_profile_id = store->CreateProfile(name);
            manager->SetSpaceProfile(space_id, new_profile_id);
          },
          base::Unretained(profile_store_.get()),
          base::Unretained(space_manager_.get()), space_id));
  profile_submenu_model_ = std::make_unique<ui::SimpleMenuModel>(
      profile_submenu_delegate_.get());
  next_profile_cmd = kMenuProfileBase;
  for (const auto& profile : profile_store_->GetProfiles()) {
    profile_submenu_model_->AddCheckItem(
        next_profile_cmd, base::UTF8ToUTF16(profile.name));
    ++next_profile_cmd;
  }
  profile_submenu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  profile_submenu_model_->AddItem(kMenuNewProfile, u"New Profile…");

  context_menu_delegate_ = std::make_unique<SpaceBarContextMenuDelegate>(
      base::BindRepeating(&AvoraSpacesBarView::ShowEditSpaceEditor,
                          base::Unretained(this), space_id),
      base::BindRepeating(&AvoraSpacesBarView::ConfirmDeleteSpace,
                          base::Unretained(this), space_id),
      can_delete);
  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kMenuEdit, u"Edit Space…");
  context_menu_model_->AddSubMenu(kMenuProfileBase, u"Profile",
                                  profile_submenu_model_.get());
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
  ShowCreateSpaceEditor();
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
  const std::string new_id = space_manager_->CreateSpace(
      name, fields.icon, std::string(), fields.accent_color);

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
      u"removed. Open browser tabs are not closed.";

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
