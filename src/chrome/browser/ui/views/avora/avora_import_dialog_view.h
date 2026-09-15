// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORT_DIALOG_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORT_DIALOG_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/import/avora_import_coordinator.h"
#include "chrome/browser/avora/import/bookmark_import_types.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"

class BrowserWindowInterface;
class PrefService;

namespace avora {

class ImportedLinkStore;
class WindowSpaceState;

// A modal dialog that lets the user import bookmarks from supported
// browsers (Chrome, Edge, Firefox, Safari) into an Avora Space.
//
// Flow:
//   1. Detection  — detects all supported browsers.
//   2. Selection  — user picks a browser/profile and destination Space.
//   3. Import     — parses the selected profile via the coordinator.
//   4. Result     — shows imported count and any skipped items.
//
// Safari-specific: if Safari data is detected but macOS TCC blocks
// access, the dialog shows a permission-denied notice instead of
// a selectable profile row.
//
// The Imported sidebar section updates automatically through the
// ImportedLinkStore observer — this dialog does not touch the view.
class AvoraImportDialogView : public views::DialogDelegateView,
                              public ui::SelectFileDialog::Listener {
  METADATA_HEADER(AvoraImportDialogView, views::DialogDelegateView)

 public:
  // Callback invoked with the source_id after a successful import when
  // the user clicks "View Imported".
  using RevealCallback = base::OnceCallback<void(const std::string&)>;

  static void Show(BrowserWindowInterface* browser,
                   WindowSpaceState* window_space_state,
                   RevealCallback on_reveal = RevealCallback(),
                   const std::string& pre_select_source_id = std::string());

  AvoraImportDialogView(BrowserWindowInterface* browser,
                        WindowSpaceState* window_space_state,
                        RevealCallback on_reveal,
                        const std::string& pre_select_source_id);
  AvoraImportDialogView(const AvoraImportDialogView&) = delete;
  AvoraImportDialogView& operator=(const AvoraImportDialogView&) = delete;
  ~AvoraImportDialogView() override;

  // Returns a human-readable display name for a browser identifier.
  static std::u16string BrowserDisplayName(const std::string& browser);

  // views::DialogDelegateView:
  bool Accept() override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file,
                    int index) override;
  void FileSelectionCanceled() override;

 private:
  // Identity of a selectable profile: browser index + profile index.
  struct ProfileSelection {
    size_t browser_index = 0;
    size_t profile_index = 0;
  };

  enum class State {
    kSelection,
    kResult,
    kError,
    kSafariExport,
    kMissingSource,
  };

  void BuildSelectionUI();
  void BuildErrorUI(const std::u16string& message);
  void BuildResultUI(const ImportResult& result,
                     const std::string& browser_name,
                     const std::string& profile_name,
                     const std::string& space_name);
  void BuildSafariExportFallbackUI();
  void BuildMissingSourceUI(const std::string& browser,
                            const std::string& profile_name);
  void OnImportClicked();
  void OnSafariExportFileSelected(const base::FilePath& path);
  void OnProfileSelected(ProfileSelection sel);
  void UpdateImportButtonLabel();
  bool IsReimport() const;

  State state_ = State::kSelection;
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<WindowSpaceState> window_space_state_;

  // All detected browsers with their profiles.
  std::vector<DetectedBrowser> browsers_;

  // Available Avora Spaces for the destination selector.
  std::vector<Space> spaces_;

  // Currently selected browser/profile.
  ProfileSelection selected_;

  // Space combobox.
  raw_ptr<views::Combobox> space_combobox_ = nullptr;

  // Profile selection row views (for highlighting the selected one).
  // Each entry maps to a ProfileSelection stored alongside.
  struct ProfileRowEntry {
    raw_ptr<views::View> view = nullptr;
    ProfileSelection selection;
  };
  std::vector<ProfileRowEntry> profile_rows_;

  // Source ID from the last successful import, for "View Imported".
  std::string post_import_source_id_;

  // Callback to reveal the imported source in the originating window.
  RevealCallback on_reveal_;

  // If non-empty, the dialog pre-selects the profile/Space matching
  // this persisted source ID (for "Update Import" from source headers).
  std::string pre_select_source_id_;

  // File picker for Safari export import.
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_IMPORT_DIALOG_VIEW_H_
