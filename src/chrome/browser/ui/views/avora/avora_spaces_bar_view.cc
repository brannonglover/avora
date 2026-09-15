// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_spaces_bar_view.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_storage_partition.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/avora/avora_sidebar_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/metadata/metadata_utils.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr SkColor kLegacyBarBg = SkColorSetARGB(0xFF, 0x12, 0x14, 0x1A);
constexpr int kLegacyBarHeight = 36;
constexpr int kLegacyPillHeight = 26;
constexpr int kLegacyPillCornerRadius = 13;

constexpr SkColor kBarBg = SkColorSetRGB(0x14, 0x16, 0x1C);
constexpr SkColor kActiveIconBg = SkColorSetRGB(0x30, 0x36, 0x3E);
constexpr SkColor kInactiveIconBg = SkColorSetARGB(0x00, 0, 0, 0);
constexpr SkColor kIconHoverBg = SkColorSetRGB(0x24, 0x28, 0x30);
constexpr SkColor kAddBtnBg = SkColorSetARGB(0x18, 0xFF, 0xFF, 0xFF);
constexpr SkColor kAddBtnHoverBg = SkColorSetARGB(0x30, 0xFF, 0xFF, 0xFF);
constexpr SkColor kNameColor = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kDimText = SkColorSetARGB(0x80, 0xED, 0xF2, 0xF5);
constexpr SkColor kFieldBg = SkColorSetRGB(0x22, 0x28, 0x2E);

constexpr int kManagedBarHeight = 40;
constexpr int kBarHPad = 8;
constexpr int kBarVPad = 4;
constexpr int kIconSize = 28;
constexpr int kIconCornerRadius = 8;
constexpr int kIconGap = 4;
constexpr int kAddBtnSize = 28;
constexpr int kNameFontSize = 12;

constexpr int kMenuRename = 1;
constexpr int kMenuDelete = 2;
constexpr int kMenuNewProfile = 3;
constexpr int kMenuIconBase = 100;
constexpr int kMenuProfileBase = 200;
constexpr int kMenuCreateIconBase = 300;

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

class SpaceIconButton : public views::LabelButton {
  METADATA_HEADER(SpaceIconButton, views::LabelButton)
 public:
  SpaceIconButton(views::Button::PressedCallback callback,
                  base::RepeatingCallback<void(const std::string&,
                                               const gfx::Point&)>
                      context_callback,
                  const std::string& space_id,
                  const std::u16string& icon_text,
                  bool is_active)
      : views::LabelButton(std::move(callback), icon_text),
        context_callback_(std::move(context_callback)),
        space_id_(space_id),
        is_active_(is_active) {
    SetPreferredSize(gfx::Size(kIconSize, kIconSize));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label()->SetFontList(gfx::FontList({std::string("system-ui")},
                                       gfx::Font::NORMAL, 16,
                                       gfx::Font::Weight::NORMAL));
    SetEnabledTextColors(SK_ColorWHITE);
    SetBorder(nullptr);
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
    return views::LabelButton::OnMousePressed(event);
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    SkColor bg = kInactiveIconBg;
    if (is_active_) {
      bg = kActiveIconBg;
    } else if (GetState() == views::Button::STATE_HOVERED ||
               GetState() == views::Button::STATE_PRESSED) {
      bg = kIconHoverBg;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(bg);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(GetLocalBounds(), kIconCornerRadius, flags);
  }

 private:
  base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
      context_callback_;
  std::string space_id_;
  bool is_active_ = false;
};

BEGIN_METADATA(SpaceIconButton)
END_METADATA

class CreateSpaceButton : public views::LabelButton {
  METADATA_HEADER(CreateSpaceButton, views::LabelButton)
 public:
  CreateSpaceButton(
      base::RepeatingCallback<void(const gfx::Point&)> pressed_callback)
      : pressed_callback_(std::move(pressed_callback)) {
    SetPreferredSize(gfx::Size(kAddBtnSize, kAddBtnSize));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label()->SetFontList(gfx::FontList({std::string("system-ui")},
                                       gfx::Font::NORMAL, 18,
                                       gfx::Font::Weight::LIGHT));
    SetEnabledTextColors(kDimText);
    SetBorder(nullptr);
    SetTooltipText(u"New Space");
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (event.IsLeftMouseButton() || event.IsRightMouseButton()) {
      gfx::Point screen_point = event.location();
      ConvertPointToScreen(this, &screen_point);
      if (pressed_callback_) {
        pressed_callback_.Run(screen_point);
      }
      return true;
    }
    return views::LabelButton::OnMousePressed(event);
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    SkColor bg = kAddBtnBg;
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      bg = kAddBtnHoverBg;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(bg);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(GetLocalBounds(), kIconCornerRadius, flags);
  }

 private:
  base::RepeatingCallback<void(const gfx::Point&)> pressed_callback_;
};

BEGIN_METADATA(CreateSpaceButton)
END_METADATA

class ActiveSpaceNameView : public views::Label {
  METADATA_HEADER(ActiveSpaceNameView, views::Label)
 public:
  ActiveSpaceNameView(
      const std::u16string& text,
      base::RepeatingClosure on_double_click,
      base::RepeatingCallback<void(const gfx::Point&)> on_context_menu)
      : views::Label(text),
        on_double_click_(std::move(on_double_click)),
        on_context_menu_(std::move(on_context_menu)) {}

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (event.IsRightMouseButton()) {
      gfx::Point screen_point = event.location();
      ConvertPointToScreen(this, &screen_point);
      if (on_context_menu_) {
        on_context_menu_.Run(screen_point);
      }
      return true;
    }
    if (event.IsLeftMouseButton() && event.GetClickCount() == 2) {
      if (on_double_click_) {
        on_double_click_.Run();
      }
      return true;
    }
    return views::Label::OnMousePressed(event);
  }

 private:
  base::RepeatingClosure on_double_click_;
  base::RepeatingCallback<void(const gfx::Point&)> on_context_menu_;
};

BEGIN_METADATA(ActiveSpaceNameView)
END_METADATA

class SpaceBarContextMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  SpaceBarContextMenuDelegate(base::RepeatingClosure on_rename,
                              base::RepeatingClosure on_delete,
                              bool can_delete)
      : on_rename_(std::move(on_rename)),
        on_delete_(std::move(on_delete)),
        can_delete_(can_delete) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kMenuRename && on_rename_) {
      on_rename_.Run();
    } else if (command_id == kMenuDelete && can_delete_ && on_delete_) {
      on_delete_.Run();
    }
  }

  bool IsCommandIdEnabled(int command_id) const override {
    return command_id != kMenuDelete || can_delete_;
  }

 private:
  base::RepeatingClosure on_rename_;
  base::RepeatingClosure on_delete_;
  bool can_delete_ = true;
};

class IconPickerMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  explicit IconPickerMenuDelegate(
      base::RepeatingCallback<void(const std::string&)> on_icon_selected)
      : on_icon_selected_(std::move(on_icon_selected)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id >= kMenuIconBase &&
        command_id <
            kMenuIconBase + static_cast<int>(avora::kDefaultSpaceIconCount) &&
        on_icon_selected_) {
      const size_t icon_index =
          static_cast<size_t>(command_id - kMenuIconBase);
      on_icon_selected_.Run(base::span(avora::kDefaultSpaceIcons)[icon_index]);
    }
  }

 private:
  base::RepeatingCallback<void(const std::string&)> on_icon_selected_;
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

class CreateSpaceIconMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  explicit CreateSpaceIconMenuDelegate(
      base::RepeatingCallback<void(const std::string&)> on_icon_selected)
      : on_icon_selected_(std::move(on_icon_selected)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id >= kMenuCreateIconBase &&
        command_id <
            kMenuCreateIconBase + static_cast<int>(avora::kDefaultSpaceIconCount) &&
        on_icon_selected_) {
      const size_t icon_index =
          static_cast<size_t>(command_id - kMenuCreateIconBase);
      on_icon_selected_.Run(base::span(avora::kDefaultSpaceIcons)[icon_index]);
    }
  }

 private:
  base::RepeatingCallback<void(const std::string&)> on_icon_selected_;
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
        gfx::Insets::VH(kBarVPad, kBarHPad), kIconGap));
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

std::u16string AvoraSpacesBarView::IdentityTooltipLine(
    const std::string& profile_id) const {
  const std::u16string name = ProfileDisplayName(profile_id);

  // The default identity intentionally uses Chromium's default partition, so
  // it shares cookies and logins with any other Space pointing at it.
  bool isolated = false;
  if (profile_store_) {
    const avora::BrowserProfile* profile =
        profile_store_->GetProfileById(profile_id);
    isolated = profile && !avora::UsesDefaultPartition(*profile);
  }

  return base::StrCat({u"\nIdentity: ", name,
                       isolated ? u" (isolated session)"
                                : u" (shared session)"});
}

void AvoraSpacesBarView::RebuildManaged() {
  RemoveAllChildViews();
  active_name_label_ = nullptr;
  rename_field_ = nullptr;

  if (!space_manager_) {
    return;
  }

  const auto spaces = space_manager_->GetSpaces();

  // Use the window-local active Space when available; fall back to the
  // global pref for legacy / unconnected callers.
  const avora::Space* active = nullptr;
  if (window_space_state_) {
    active =
        space_manager_->GetSpaceById(window_space_state_->active_space_id());
  }
  if (!active) {
    active = space_manager_->GetActiveSpace();
  }

  auto* row_layout = static_cast<views::BoxLayout*>(GetLayoutManager());

  std::u16string display_name =
      active ? base::UTF8ToUTF16(active->name) : u"Default Space";

  auto label = std::make_unique<ActiveSpaceNameView>(
      display_name,
      base::BindRepeating(&AvoraSpacesBarView::BeginRename,
                          base::Unretained(this)),
      base::BindRepeating(&AvoraSpacesBarView::ShowSpaceContextMenu,
                          base::Unretained(this),
                          active ? active->id : std::string()));
  label->SetFontList(gfx::FontList({std::string("system-ui")},
                                   gfx::Font::NORMAL, kNameFontSize,
                                   gfx::Font::Weight::MEDIUM));
  label->SetEnabledColor(kNameColor);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  if (active) {
    label->SetTooltipText(
        base::StrCat({display_name, IdentityTooltipLine(active->profile_id)}));
  }
  active_name_label_ = AddChildView(std::move(label));

  auto field = std::make_unique<views::Textfield>();
  field->SetController(this);
  field->SetFontList(gfx::FontList({std::string("system-ui")},
                                   gfx::Font::NORMAL, kNameFontSize,
                                   gfx::Font::Weight::MEDIUM));
  field->SetColor(kNameColor);
  field->SetBackgroundColor(kFieldBg);
  field->SetBorder(nullptr);
  field->SetVisible(false);
  rename_field_ = AddChildView(std::move(field));

  if (row_layout) {
    row_layout->SetFlexForView(active_name_label_, 1);
    row_layout->SetFlexForView(rename_field_, 1);
  }

  for (const auto& space : spaces) {
    std::u16string icon_text =
        space.icon.empty() ? u"🏠" : base::UTF8ToUTF16(space.icon);

    auto btn = std::make_unique<SpaceIconButton>(
        base::BindRepeating(&AvoraSpacesBarView::OnSpaceClicked,
                            base::Unretained(this), space.id),
        base::BindRepeating(&AvoraSpacesBarView::OnSpaceContextMenu,
                            base::Unretained(this)),
        space.id,
        icon_text,
        active && space.id == active->id);
    btn->SetTooltipText(base::StrCat({base::UTF8ToUTF16(space.name),
                                      IdentityTooltipLine(space.profile_id)}));
    AddChildView(std::move(btn));
  }

  AddChildView(std::make_unique<CreateSpaceButton>(
      base::BindRepeating(&AvoraSpacesBarView::OnCreateSpaceClicked,
                          base::Unretained(this))));

  InvalidateLayout();
}

void AvoraSpacesBarView::OnSpaceClicked(const std::string& id) {
  if (!space_manager_) {
    if (clicked_cb_) {
      clicked_cb_.Run(id);
    }
    return;
  }

  // Determine the currently active space for this window.
  const std::string current_active =
      window_space_state_ ? window_space_state_->active_space_id()
                          : (space_manager_->GetActiveSpace()
                                 ? space_manager_->GetActiveSpace()->id
                                 : std::string());
  if (current_active == id) {
    BeginRename();
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

  icon_submenu_delegate_ = std::make_unique<IconPickerMenuDelegate>(
      base::BindRepeating(
          [](avora::SpaceManager* manager, const std::string& id,
             const std::string& icon) {
            manager->SetSpaceIcon(id, icon);
          },
          base::Unretained(space_manager_.get()), space_id));
  icon_submenu_model_ = std::make_unique<ui::SimpleMenuModel>(
      icon_submenu_delegate_.get());
  for (size_t i = 0; i < avora::kDefaultSpaceIconCount; ++i) {
    icon_submenu_model_->AddItem(
        kMenuIconBase + static_cast<int>(i),
        base::UTF8ToUTF16(base::span(avora::kDefaultSpaceIcons)[i]));
  }

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
      base::BindRepeating(&AvoraSpacesBarView::BeginRename,
                          base::Unretained(this)),
      base::BindRepeating(&AvoraSpacesBarView::ConfirmDeleteSpace,
                          base::Unretained(this), space_id),
      can_delete);
  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kMenuRename, u"Rename Space");
  context_menu_model_->AddSubMenu(kMenuIconBase, u"Change Icon",
                                  icon_submenu_model_.get());
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

void AvoraSpacesBarView::OnCreateSpaceClicked(const gfx::Point& screen_point) {
  ShowCreateSpaceMenu(screen_point);
}

void AvoraSpacesBarView::ShowCreateSpaceMenu(const gfx::Point& screen_point) {
  if (!space_manager_) {
    return;
  }

  create_icon_submenu_delegate_ = std::make_unique<CreateSpaceIconMenuDelegate>(
      base::BindRepeating(&AvoraSpacesBarView::CreateSpaceWithIcon,
                          base::Unretained(this)));
  create_icon_submenu_model_ = std::make_unique<ui::SimpleMenuModel>(
      create_icon_submenu_delegate_.get());
  for (size_t i = 0; i < avora::kDefaultSpaceIconCount; ++i) {
    create_icon_submenu_model_->AddItem(
        kMenuCreateIconBase + static_cast<int>(i),
        base::UTF8ToUTF16(base::span(avora::kDefaultSpaceIcons)[i]));
  }

  menu_runner_ = std::make_unique<views::MenuRunner>(
      create_icon_submenu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(GetWidget(), nullptr,
                          gfx::Rect(screen_point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kMouse);
}

void AvoraSpacesBarView::CreateSpaceWithIcon(const std::string& icon) {
  if (!space_manager_) {
    return;
  }

  const auto spaces = space_manager_->GetSpaces();
  const int index = static_cast<int>(spaces.size());
  const std::string name = "Space " + std::to_string(index + 1);

  // Give the Space its own browser identity so it gets an isolated cookie
  // jar, rather than sharing the default one.  The very first Space stays on
  // the default identity, which is what lets an existing install keep its
  // current cookies and logins; SpaceManager applies that default when
  // |profile_id| is empty.
  std::string profile_id;
  if (profile_store_ && !spaces.empty()) {
    profile_id = profile_store_->CreateProfile(name);
  }

  const std::string new_id = space_manager_->CreateSpace(name, icon, profile_id);
  if (window_space_state_) {
    window_space_state_->SetActiveSpaceId(new_id);
  } else {
    space_manager_->ActivateSpace(new_id);
  }
  if (clicked_cb_) {
    clicked_cb_.Run(new_id);
  }
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

  const avora::Space* active = window_space_state_
      ? space_manager_->GetSpaceById(window_space_state_->active_space_id())
      : space_manager_->GetActiveSpace();
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

void AvoraSpacesBarView::BeginRename() {
  if (!active_name_label_ || !rename_field_ || !space_manager_) {
    return;
  }
  const avora::Space* active = window_space_state_
      ? space_manager_->GetSpaceById(window_space_state_->active_space_id())
      : space_manager_->GetActiveSpace();
  if (!active) {
    return;
  }

  active_name_label_->SetVisible(false);
  rename_field_->SetVisible(true);
  rename_field_->SetText(base::UTF8ToUTF16(active->name));
  rename_field_->SelectAll(true);
  rename_field_->RequestFocus();
}

void AvoraSpacesBarView::CommitRename() {
  if (!rename_field_ || !space_manager_) {
    return;
  }
  const avora::Space* active = window_space_state_
      ? space_manager_->GetSpaceById(window_space_state_->active_space_id())
      : space_manager_->GetActiveSpace();
  if (!active) {
    return;
  }

  const std::u16string new_name(rename_field_->GetText());
  if (!new_name.empty()) {
    space_manager_->RenameSpace(active->id, base::UTF16ToUTF8(new_name));
  }

  rename_field_->SetVisible(false);
  if (active_name_label_) {
    active_name_label_->SetVisible(true);
  }
}

void AvoraSpacesBarView::CancelRename() {
  if (rename_field_) {
    rename_field_->SetVisible(false);
  }
  if (active_name_label_) {
    active_name_label_->SetVisible(true);
  }
}

bool AvoraSpacesBarView::HandleKeyEvent(views::Textfield* sender,
                                        const ui::KeyEvent& key_event) {
  if (sender != rename_field_) {
    return false;
  }
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    CommitRename();
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    CancelRename();
    return true;
  }
  return false;
}

BEGIN_METADATA(AvoraSpacesBarView)
END_METADATA
