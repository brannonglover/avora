// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_import_dialog_view.h"

#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_imported_link_store.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "ui/base/base_window.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/base/models/combobox_model.h"
#include "ui/gfx/canvas.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"

namespace avora {

namespace {

constexpr int kDialogWidth = 400;
constexpr int kRowHeight = 40;
constexpr int kRowHPadding = 16;
constexpr int kRowVPadding = 6;
constexpr int kSectionSpacing = 12;
constexpr SkColor kSelectedBg = SkColorSetARGB(0x20, 0x6E, 0xA8, 0xFF);
constexpr SkColor kHoverBg = SkColorSetARGB(0x10, 0xFF, 0xFF, 0xFF);

// A clickable profile row with name + bookmark count.
class ProfileRow : public views::View {
  METADATA_HEADER(ProfileRow, views::View)

 public:
  ProfileRow(const std::u16string& name,
             const std::u16string& detail,
             bool selected,
             base::RepeatingClosure on_click)
      : selected_(selected), on_click_(std::move(on_click)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(kRowVPadding, kRowHPadding + 16), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* name_label = AddChildView(std::make_unique<views::Label>(name));
    name_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name_label->SetEnabledColor(SkColorSetRGB(0xED, 0xF2, 0xF5));
    layout->SetFlexForView(name_label, 1);

    auto* detail_label =
        AddChildView(std::make_unique<views::Label>(detail));
    detail_label->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    detail_label->SetEnabledColor(SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5));

    SetPreferredSize(gfx::Size(kDialogWidth, kRowHeight));
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) return;
    selected_ = selected;
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (selected_) {
      canvas->FillRect(GetLocalBounds(), kSelectedBg);
    } else if (hovered_) {
      canvas->FillRect(GetLocalBounds(), kHoverBg);
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (event.IsOnlyLeftMouseButton() && on_click_) {
      on_click_.Run();
    }
    return true;
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
  bool selected_ = false;
  bool hovered_ = false;
  base::RepeatingClosure on_click_;
};

BEGIN_METADATA(ProfileRow)
END_METADATA

class SpaceComboboxModel : public ui::ComboboxModel {
 public:
  explicit SpaceComboboxModel(std::vector<std::u16string> names)
      : names_(std::move(names)) {}

  size_t GetItemCount() const override { return names_.size(); }
  std::u16string GetItemAt(size_t index) const override {
    return index < names_.size() ? names_[index] : std::u16string();
  }

 private:
  std::vector<std::u16string> names_;
};

}  // namespace

// static
std::u16string AvoraImportDialogView::BrowserDisplayName(
    const std::string& browser) {
  if (browser == "chrome") return u"Chrome";
  if (browser == "edge") return u"Microsoft Edge";
  if (browser == "firefox") return u"Firefox";
  if (browser == "safari") return u"Safari";
  return base::UTF8ToUTF16(browser);
}

// static
void AvoraImportDialogView::Show(BrowserWindowInterface* browser,
                                 WindowSpaceState* window_space_state,
                                 RevealCallback on_reveal,
                                 const std::string& pre_select_source_id) {
  auto dialog = std::make_unique<AvoraImportDialogView>(
      browser, window_space_state, std::move(on_reveal),
      pre_select_source_id);
  views::DialogDelegate::CreateDialogWidget(
      std::move(dialog),
      browser->GetWindow()->GetNativeWindow(),
      gfx::NativeView())
      ->Show();
}

AvoraImportDialogView::AvoraImportDialogView(
    BrowserWindowInterface* browser,
    WindowSpaceState* window_space_state,
    RevealCallback on_reveal,
    const std::string& pre_select_source_id)
    : browser_(browser),
      window_space_state_(window_space_state),
      on_reveal_(std::move(on_reveal)),
      pre_select_source_id_(pre_select_source_id) {
  SetTitle(u"Import Browser Data");
  SetModalType(ui::mojom::ModalType::kWindow);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  // Detect all supported browsers.
  browsers_ = AvoraImportCoordinator::DetectAllBrowsers();

  // Load available Spaces.
  Profile* profile = browser_->GetProfile();
  SpaceManager space_manager(profile->GetPrefs());
  spaces_ = space_manager.GetSpaces();

  // Check if any importable profiles exist across all browsers.
  bool any_importable = false;
  bool any_permission_denied = false;
  for (const auto& b : browsers_) {
    for (const auto& p : b.profiles) {
      if (p.bookmark_count > 0) {
        any_importable = true;
      }
      if (p.access_status == ProfileAccessStatus::kPermissionDenied) {
        any_permission_denied = true;
      }
    }
  }

  if (browsers_.empty()) {
    BuildErrorUI(u"No supported browsers were detected on this computer.");
    return;
  }

  if (!any_importable && !any_permission_denied) {
    BuildErrorUI(
        u"Supported browsers were found, but no profiles contain bookmarks.");
    return;
  }

  if (!any_importable && any_permission_denied) {
    // Safari-only: no directly importable profiles, but we detected Safari
    // data behind TCC.  Show the export fallback UI rather than a dead end.
    if (spaces_.empty()) {
      BuildErrorUI(u"No Avora Spaces are available. Create a Space first.");
      return;
    }
    BuildSafariExportFallbackUI();
    return;
  }

  if (spaces_.empty()) {
    BuildErrorUI(u"No Avora Spaces are available. Create a Space first.");
    return;
  }

  BuildSelectionUI();
}

AvoraImportDialogView::~AvoraImportDialogView() = default;

bool AvoraImportDialogView::Accept() {
  if (state_ == State::kSelection) {
    OnImportClicked();
    return false;
  }
  if (state_ == State::kMissingSource) {
    // Transition to the generic selection UI.
    pre_select_source_id_.clear();
    BuildSelectionUI();
    InvalidateLayout();
    if (GetWidget()) GetWidget()->SetSize(GetPreferredSize());
    return false;
  }
  if (state_ == State::kSafariExport) {
    // Open a file picker for the exported .html file.
    ui::SelectFileDialog::FileTypeInfo file_types;
    file_types.extensions.push_back({FILE_PATH_LITERAL("html"),
                                     FILE_PATH_LITERAL("htm")});
    file_types.extension_description_overrides.push_back(
        u"HTML Bookmark Files");
    file_types.include_all_files = true;

    select_file_dialog_ = ui::SelectFileDialog::Create(
        this, nullptr);
    select_file_dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_OPEN_FILE,
        u"Select exported bookmark file",
        base::FilePath(),
        &file_types,
        0,
        std::string(),
        GetWidget()->GetNativeWindow(),
        nullptr);
    return false;
  }
  // kResult or kError — closing the dialog.
  if (state_ == State::kResult && !post_import_source_id_.empty() &&
      on_reveal_) {
    std::move(on_reveal_).Run(post_import_source_id_);
  }
  return true;
}

void AvoraImportDialogView::BuildSelectionUI() {
  RemoveAllChildViews();
  profile_rows_.clear();

  // Section header with Avora model explanation.
  auto* section_label = AddChildView(std::make_unique<views::Label>(
      u"Select a profile to import:"));
  section_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  section_label->SetEnabledColor(SkColorSetARGB(0xCC, 0xED, 0xF2, 0xF5));
  section_label->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(12, kRowHPadding, 4, kRowHPadding)));

  auto* model_hint = AddChildView(std::make_unique<views::Label>(
      u"Your bookmarks will stay organized in Imported. Drag the ones "
      u"you use most to Favorites."));
  model_hint->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  model_hint->SetEnabledColor(SkColorSetARGB(0x80, 0xED, 0xF2, 0xF5));
  model_hint->SetMultiLine(true);
  model_hint->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding);
  model_hint->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, kRowHPadding, 8, kRowHPadding)));

  // Pre-select: if pre_select_source_id_ is set, find the matching
  // browser/profile/Space by computing source IDs for all detected profiles.
  bool found_default = false;
  size_t pre_select_space_index = 0;
  bool pre_select_requested = !pre_select_source_id_.empty();

  if (pre_select_requested) {
    // Look up the persisted source to find its space_id and display info.
    Profile* ps_profile = browser_->GetProfile();
    ImportedLinkStore ps_store(ps_profile->GetPrefs());
    const ImportedSource* existing =
        ps_store.GetSourceById(pre_select_source_id_);
    std::string target_space_id =
        existing ? existing->space_id : std::string();

    // Find the matching Space index.
    for (size_t si = 0; si < spaces_.size(); si++) {
      if (spaces_[si].id == target_space_id) {
        pre_select_space_index = si;
        break;
      }
    }

    // Match by recomputing source IDs against all detected profiles.
    for (size_t bi = 0; bi < browsers_.size() && !found_default; bi++) {
      for (size_t pi = 0; pi < browsers_[bi].profiles.size(); pi++) {
        const auto& p = browsers_[bi].profiles[pi];
        if (p.bookmark_count <= 0) continue;
        std::string computed_id =
            AvoraImportCoordinator::GenerateImportedSourceId(
                browsers_[bi].browser, p.directory_name, target_space_id);
        if (computed_id == pre_select_source_id_) {
          selected_ = {bi, pi};
          found_default = true;
          break;
        }
      }
    }

    // If the pre-selected source could not be found, show the
    // missing-source UI instead of silently selecting another profile.
    if (!found_default && existing) {
      BuildMissingSourceUI(existing->browser, existing->profile_name);
      return;
    }
    if (!found_default && !existing) {
      // Source itself was deleted from the store — stale context menu.
      BuildMissingSourceUI(std::string(), std::string());
      return;
    }
  }

  // Generic open: select the first importable profile.
  if (!found_default) {
    for (size_t bi = 0; bi < browsers_.size(); bi++) {
      for (size_t pi = 0; pi < browsers_[bi].profiles.size(); pi++) {
        if (browsers_[bi].profiles[pi].bookmark_count > 0 && !found_default) {
          selected_ = {bi, pi};
          found_default = true;
        }
      }
    }
  }

  // Build browser groups with profile rows.
  for (size_t bi = 0; bi < browsers_.size(); bi++) {
    const auto& browser = browsers_[bi];

    // Check if this browser has importable or permission-denied profiles.
    bool has_importable = false;
    bool has_permission_denied = false;
    for (const auto& p : browser.profiles) {
      if (p.bookmark_count > 0) {
        has_importable = true;
      }
      if (p.access_status == ProfileAccessStatus::kPermissionDenied) {
        has_permission_denied = true;
      }
    }
    if (!has_importable && !has_permission_denied) continue;

    // Browser heading.
    auto* browser_label = AddChildView(std::make_unique<views::Label>(
        BrowserDisplayName(browser.browser)));
    browser_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    browser_label->SetEnabledColor(SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5));
    browser_label->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::TLBR(bi > 0 ? kSectionSpacing : 4, kRowHPadding,
                           2, kRowHPadding)));

    // Show a permission note if any profile is blocked by TCC.
    if (has_permission_denied && !has_importable) {
      auto* perm_label = AddChildView(std::make_unique<views::Label>(
          u"Avora cannot read Safari directly. Export from Safari "
          u"(File \u2192 Export \u2192 Bookmarks) and import below."));
      perm_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      perm_label->SetEnabledColor(SkColorSetARGB(0x80, 0xFF, 0xAA, 0x55));
      perm_label->SetMultiLine(true);
      perm_label->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding - 16);
      perm_label->SetBorder(views::CreateEmptyBorder(
          gfx::Insets::TLBR(2, kRowHPadding + 16, 4, kRowHPadding)));
      continue;
    }

    for (size_t pi = 0; pi < browser.profiles.size(); pi++) {
      const auto& prof = browser.profiles[pi];
      if (prof.bookmark_count <= 0) continue;

      ProfileSelection sel{bi, pi};
      bool is_selected = (sel.browser_index == selected_.browser_index &&
                          sel.profile_index == selected_.profile_index);

      std::u16string name = base::UTF8ToUTF16(prof.display_name);
      std::u16string detail =
          base::UTF8ToUTF16(base::NumberToString(prof.bookmark_count) +
                            " bookmarks");

      auto* row = AddChildView(std::make_unique<ProfileRow>(
          name, detail, is_selected,
          base::BindRepeating(&AvoraImportDialogView::OnProfileSelected,
                              base::Unretained(this), sel)));
      profile_rows_.push_back({row, sel});
    }
  }

  // Spacer.
  auto* spacer = AddChildView(std::make_unique<views::View>());
  spacer->SetPreferredSize(gfx::Size(kDialogWidth, kSectionSpacing));

  // "Import into:" label.
  auto* dest_label = AddChildView(std::make_unique<views::Label>(
      u"Import into:"));
  dest_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  dest_label->SetEnabledColor(SkColorSetARGB(0xCC, 0xED, 0xF2, 0xF5));
  dest_label->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, kRowHPadding, 4, kRowHPadding)));

  // Space combobox.
  std::vector<std::u16string> space_names;
  size_t default_index = 0;

  if (!pre_select_source_id_.empty()) {
    // Pre-selected from "Update Import" — use the source's Space.
    default_index = pre_select_space_index;
  } else {
    // Default to the originating window's active Space.
    const std::string active_space_id =
        window_space_state_ ? window_space_state_->active_space_id()
                            : std::string();
    for (size_t i = 0; i < spaces_.size(); i++) {
      if (spaces_[i].id == active_space_id) {
        default_index = i;
        break;
      }
    }
  }

  for (size_t i = 0; i < spaces_.size(); i++) {
    space_names.push_back(base::UTF8ToUTF16(
        spaces_[i].icon + " " + spaces_[i].name));
  }

  auto combobox_model = std::make_unique<SpaceComboboxModel>(
      std::move(space_names));

  auto* combobox_container = AddChildView(
      std::make_unique<views::BoxLayoutView>());
  combobox_container->SetOrientation(
      views::BoxLayout::Orientation::kHorizontal);
  combobox_container->SetInsideBorderInsets(
      gfx::Insets::VH(0, kRowHPadding));

  auto combobox = std::make_unique<views::Combobox>(
      std::move(combobox_model));
  combobox->SetSelectedIndex(default_index);
  combobox->SetCallback(base::BindRepeating(
      &AvoraImportDialogView::UpdateImportButtonLabel,
      base::Unretained(this)));
  space_combobox_ = combobox_container->AddChildView(std::move(combobox));

  // Bottom padding.
  auto* bottom_spacer = AddChildView(std::make_unique<views::View>());
  bottom_spacer->SetPreferredSize(gfx::Size(kDialogWidth, 8));

  // Buttons — label depends on whether this is a re-import.
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");

  state_ = State::kSelection;
  UpdateImportButtonLabel();
}

void AvoraImportDialogView::BuildErrorUI(const std::u16string& message) {
  state_ = State::kError;
  RemoveAllChildViews();
  profile_rows_.clear();
  space_combobox_ = nullptr;

  auto* label = AddChildView(std::make_unique<views::Label>(message));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(SkColorSetARGB(0xCC, 0xED, 0xF2, 0xF5));
  label->SetMultiLine(true);
  label->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding);
  label->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(kRowHPadding, kRowHPadding)));

  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  SetButtonLabel(ui::mojom::DialogButton::kOk, u"Close");
  DialogModelChanged();
}

void AvoraImportDialogView::BuildResultUI(
    const ImportResult& result,
    const std::string& browser_name,
    const std::string& profile_name,
    const std::string& space_name) {
  state_ = State::kResult;
  RemoveAllChildViews();
  profile_rows_.clear();
  space_combobox_ = nullptr;

  std::u16string browser_display =
      BrowserDisplayName(browser_name);

  // Checkmark + count.
  std::u16string msg = u"\u2713 " +
      base::UTF8ToUTF16(
          base::NumberToString(result.stats.links_imported)) +
      u" bookmarks imported from " + browser_display +
      u" \u2014 " +
      base::UTF8ToUTF16(profile_name) +
      u" into " +
      base::UTF8ToUTF16(space_name) + u".";

  auto* label = AddChildView(std::make_unique<views::Label>(msg));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(SkColorSetRGB(0xED, 0xF2, 0xF5));
  label->SetMultiLine(true);
  label->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding);
  label->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(kRowHPadding, kRowHPadding)));

  // Contextual help.
  auto* hint = AddChildView(std::make_unique<views::Label>(
      u"They\u2019re ready in Imported. Drag the ones you use most "
      u"to Favorites."));
  hint->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  hint->SetEnabledColor(SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5));
  hint->SetMultiLine(true);
  hint->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding);
  hint->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, kRowHPadding, 4, kRowHPadding)));

  if (result.stats.items_skipped > 0 || result.stats.items_excluded > 0) {
    std::u16string detail_msg;
    if (result.stats.items_skipped > 0) {
      detail_msg +=
          base::UTF8ToUTF16(
              base::NumberToString(result.stats.items_skipped)) +
          u" unsupported or invalid items were skipped.";
    }
    if (result.stats.items_excluded > 0) {
      if (!detail_msg.empty()) detail_msg += u" ";
      detail_msg +=
          base::UTF8ToUTF16(
              base::NumberToString(result.stats.items_excluded)) +
          u" items (e.g. Reading List) were not included.";
    }

    auto* detail_label = AddChildView(
        std::make_unique<views::Label>(detail_msg));
    detail_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    detail_label->SetEnabledColor(
        SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5));
    detail_label->SetMultiLine(true);
    detail_label->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding);
    detail_label->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::TLBR(0, kRowHPadding, kRowHPadding, kRowHPadding)));
  }

  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  SetButtonLabel(ui::mojom::DialogButton::kOk, u"View Imported");
  DialogModelChanged();
}

void AvoraImportDialogView::BuildMissingSourceUI(
    const std::string& browser,
    const std::string& profile_name) {
  state_ = State::kMissingSource;
  RemoveAllChildViews();
  profile_rows_.clear();
  space_combobox_ = nullptr;

  // Build the source label: "Chrome — Work" or just "This source".
  std::u16string source_label;
  if (!browser.empty()) {
    source_label = BrowserDisplayName(browser);
    if (!profile_name.empty()) {
      source_label += u" \u2014 " + base::UTF8ToUTF16(profile_name);
    }
  }

  std::u16string heading = source_label.empty()
      ? u"This source could not be found."
      : source_label;

  auto* heading_label = AddChildView(std::make_unique<views::Label>(heading));
  heading_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  heading_label->SetEnabledColor(SkColorSetRGB(0xED, 0xF2, 0xF5));
  heading_label->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(kRowHPadding, kRowHPadding)));

  std::u16string body = source_label.empty()
      ? u"The imported source may have been removed."
      : u"This browser profile could not be found. "
        u"It may have been removed or renamed.";

  auto* body_label = AddChildView(std::make_unique<views::Label>(body));
  body_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  body_label->SetEnabledColor(SkColorSetARGB(0xCC, 0xED, 0xF2, 0xF5));
  body_label->SetMultiLine(true);
  body_label->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding);
  body_label->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, kRowHPadding, 4, kRowHPadding)));

  auto* note = AddChildView(std::make_unique<views::Label>(
      u"Your existing imported bookmarks are still available."));
  note->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  note->SetEnabledColor(SkColorSetARGB(0x80, 0xED, 0xF2, 0xF5));
  note->SetMultiLine(true);
  note->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding);
  note->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, kRowHPadding, kRowHPadding, kRowHPadding)));

  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kOk, u"Choose Another Source");
  SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  DialogModelChanged();

  InvalidateLayout();
  if (GetWidget()) GetWidget()->SetSize(GetPreferredSize());
}

void AvoraImportDialogView::BuildSafariExportFallbackUI() {
  state_ = State::kSafariExport;
  RemoveAllChildViews();
  profile_rows_.clear();
  space_combobox_ = nullptr;

  auto* heading = AddChildView(std::make_unique<views::Label>(
      u"Safari bookmarks require permission"));
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  heading->SetEnabledColor(SkColorSetRGB(0xED, 0xF2, 0xF5));
  heading->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kRowHPadding, kRowHPadding, 8, kRowHPadding)));

  auto* body = AddChildView(std::make_unique<views::Label>(
      u"Avora cannot read Safari bookmarks directly. You can export "
      u"them from Safari instead:\n\n"
      u"1. In Safari, choose File \u2192 Export \u2192 Bookmarks\n"
      u"2. Save the file anywhere\n"
      u"3. Click \u201cImport Safari Export\u2026\u201d below"));
  body->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  body->SetEnabledColor(SkColorSetARGB(0xCC, 0xED, 0xF2, 0xF5));
  body->SetMultiLine(true);
  body->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding);
  body->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, kRowHPadding, 12, kRowHPadding)));

  // "Import into:" Space selector.
  auto* dest_label = AddChildView(std::make_unique<views::Label>(
      u"Import into:"));
  dest_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  dest_label->SetEnabledColor(SkColorSetARGB(0xCC, 0xED, 0xF2, 0xF5));
  dest_label->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, kRowHPadding, 4, kRowHPadding)));

  std::vector<std::u16string> space_names;
  size_t default_index = 0;
  const std::string active_space_id =
      window_space_state_ ? window_space_state_->active_space_id()
                          : std::string();
  for (size_t i = 0; i < spaces_.size(); i++) {
    space_names.push_back(base::UTF8ToUTF16(
        spaces_[i].icon + " " + spaces_[i].name));
    if (spaces_[i].id == active_space_id) default_index = i;
  }

  auto combobox_model = std::make_unique<SpaceComboboxModel>(
      std::move(space_names));
  auto* combobox_container = AddChildView(
      std::make_unique<views::BoxLayoutView>());
  combobox_container->SetOrientation(
      views::BoxLayout::Orientation::kHorizontal);
  combobox_container->SetInsideBorderInsets(
      gfx::Insets::VH(0, kRowHPadding));
  auto combobox = std::make_unique<views::Combobox>(
      std::move(combobox_model));
  combobox->SetSelectedIndex(default_index);
  space_combobox_ = combobox_container->AddChildView(std::move(combobox));

  auto* spacer = AddChildView(std::make_unique<views::View>());
  spacer->SetPreferredSize(gfx::Size(kDialogWidth, 8));

  // Full Disk Access secondary note.
  auto* fda_note = AddChildView(std::make_unique<views::Label>(
      u"Alternatively, grant Avora Full Disk Access in System Settings."));
  fda_note->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  fda_note->SetEnabledColor(SkColorSetARGB(0x60, 0xED, 0xF2, 0xF5));
  fda_note->SetMultiLine(true);
  fda_note->SetMaximumWidth(kDialogWidth - 2 * kRowHPadding);
  fda_note->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, kRowHPadding, 8, kRowHPadding)));

  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kOk,
                 u"Import Safari Export\u2026");
  SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  DialogModelChanged();

  InvalidateLayout();
  if (GetWidget()) GetWidget()->SetSize(GetPreferredSize());
}

void AvoraImportDialogView::OnImportClicked() {
  if (selected_.browser_index >= browsers_.size()) return;

  const auto& browser_data = browsers_[selected_.browser_index];
  if (selected_.profile_index >= browser_data.profiles.size()) return;

  // Resolve selected Space.
  const size_t space_index =
      space_combobox_ ? space_combobox_->GetSelectedIndex().value_or(0) : 0;
  if (space_index >= spaces_.size()) return;

  const DetectedProfile& profile =
      browser_data.profiles[selected_.profile_index];
  const Space& space = spaces_[space_index];

  // Verify the Space still exists.
  Profile* browser_profile = browser_->GetProfile();
  SpaceManager space_manager(browser_profile->GetPrefs());
  if (!space_manager.GetSpaceById(space.id)) {
    BuildErrorUI(
        u"The selected Space was deleted. Please close this dialog and "
        u"select another destination.");
    InvalidateLayout();
    if (GetWidget()) {
      GetWidget()->SetSize(GetPreferredSize());
    }
    return;
  }

  // Perform the import using the browser-neutral API.
  ImportedLinkStore store(browser_profile->GetPrefs());
  ImportResult result = AvoraImportCoordinator::ImportProfile(
      browser_data.browser, profile, space.id, &store);

  if (!result.success) {
    BuildErrorUI(base::UTF8ToUTF16(result.error_message));
  } else {
    post_import_source_id_ = result.source_id;
    BuildResultUI(result, browser_data.browser,
                  profile.display_name, space.name);
  }

  InvalidateLayout();
  if (GetWidget()) {
    GetWidget()->SetSize(GetPreferredSize());
  }
}

bool AvoraImportDialogView::IsReimport() const {
  if (selected_.browser_index >= browsers_.size()) return false;
  const auto& browser_data = browsers_[selected_.browser_index];
  if (selected_.profile_index >= browser_data.profiles.size()) return false;

  const size_t space_index =
      space_combobox_ ? space_combobox_->GetSelectedIndex().value_or(0) : 0;
  if (space_index >= spaces_.size()) return false;

  const auto& profile = browser_data.profiles[selected_.profile_index];
  const std::string source_id =
      AvoraImportCoordinator::GenerateImportedSourceId(
          browser_data.browser, profile.directory_name, spaces_[space_index].id);

  Profile* browser_profile = browser_->GetProfile();
  ImportedLinkStore store(browser_profile->GetPrefs());
  return store.GetSourceById(source_id) != nullptr;
}

void AvoraImportDialogView::UpdateImportButtonLabel() {
  if (state_ != State::kSelection) return;
  if (IsReimport()) {
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Update Import");
  } else {
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Import");
  }
  DialogModelChanged();
}

void AvoraImportDialogView::OnProfileSelected(ProfileSelection sel) {
  if (sel.browser_index == selected_.browser_index &&
      sel.profile_index == selected_.profile_index) {
    return;
  }

  selected_ = sel;

  for (auto& entry : profile_rows_) {
    bool is_sel = (entry.selection.browser_index == sel.browser_index &&
                   entry.selection.profile_index == sel.profile_index);
    static_cast<ProfileRow*>(entry.view.get())->SetSelected(is_sel);
  }

  UpdateImportButtonLabel();
}

void AvoraImportDialogView::FileSelected(
    const ui::SelectedFileInfo& file,
    int index) {
  OnSafariExportFileSelected(file.path());
}

void AvoraImportDialogView::FileSelectionCanceled() {
  // User canceled the file picker — no changes.
}

void AvoraImportDialogView::OnSafariExportFileSelected(
    const base::FilePath& path) {
  // Resolve destination Space.
  const size_t space_index =
      space_combobox_ ? space_combobox_->GetSelectedIndex().value_or(0) : 0;
  if (space_index >= spaces_.size()) return;
  const auto& space = spaces_[space_index];

  Profile* browser_profile = browser_->GetProfile();
  ImportedLinkStore store(browser_profile->GetPrefs());

  ImportResult result = AvoraImportCoordinator::ImportHtmlFile(
      path, "safari", "default", "Exported Bookmarks",
      space.id, &store);

  if (!result.success) {
    BuildErrorUI(base::UTF8ToUTF16(result.error_message));
  } else {
    post_import_source_id_ = result.source_id;
    BuildResultUI(result, "safari", "Exported Bookmarks", space.name);
  }

  InvalidateLayout();
  if (GetWidget()) GetWidget()->SetSize(GetPreferredSize());
}

BEGIN_METADATA(AvoraImportDialogView)
END_METADATA

}  // namespace avora
