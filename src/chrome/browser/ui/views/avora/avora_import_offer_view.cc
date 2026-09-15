// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_import_offer_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_prefs.h"
#include "chrome/browser/avora/import/avora_import_coordinator.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/avora/avora_import_dialog_view.h"
#include "components/prefs/pref_service.h"
#include "ui/base/base_window.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace avora {

namespace {
constexpr int kDialogWidth = 380;
constexpr int kPadding = 20;
constexpr SkColor kHeadingColor = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kBodyColor = SkColorSetARGB(0xCC, 0xED, 0xF2, 0xF5);
constexpr SkColor kDetailColor = SkColorSetARGB(0x80, 0xED, 0xF2, 0xF5);
}  // namespace

// static
bool AvoraImportOfferView::MaybeShow(BrowserWindowInterface* browser,
                                     WindowSpaceState* window_space_state) {
  if (!browser) return false;
  Profile* profile = browser->GetProfile();
  if (!profile) return false;
  PrefService* prefs = profile->GetPrefs();
  if (!prefs) return false;

  if (prefs->GetBoolean(kImportOfferedPref)) return false;

  auto browsers = AvoraImportCoordinator::DetectAllBrowsers();

  // Only show if we found at least one importable profile.
  bool any_importable = false;
  for (const auto& b : browsers) {
    for (const auto& p : b.profiles) {
      if (p.bookmark_count > 0) {
        any_importable = true;
        break;
      }
    }
    if (any_importable) break;
  }

  // Do not consume the one-time offer when detection found nothing.
  // A transient detection failure should not permanently suppress the offer.
  if (!any_importable) return false;

  // Mark as offered now that we are about to present the dialog.
  prefs->SetBoolean(kImportOfferedPref, true);

  auto delegate = std::make_unique<views::DialogDelegate>();
  views::DialogDelegate* delegate_ptr = delegate.get();
  delegate_ptr->SetContentsView(std::make_unique<AvoraImportOfferView>(
      std::move(delegate), browser, window_space_state, std::move(browsers)));
  views::DialogDelegate::CreateDialogWidget(
      delegate_ptr, browser->GetWindow()->GetNativeWindow(),
      gfx::NativeView())
      ->Show();
  return true;
}

AvoraImportOfferView::AvoraImportOfferView(
    std::unique_ptr<views::DialogDelegate> delegate,
    BrowserWindowInterface* browser,
    WindowSpaceState* window_space_state,
    std::vector<DetectedBrowser> browsers)
    : delegate_(std::move(delegate)),
      browser_(browser),
      window_space_state_(window_space_state),
      browsers_(std::move(browsers)) {
  delegate_->SetTitle(u"Bring your bookmarks to Avora");
  delegate_->SetModalType(ui::mojom::ModalType::kWindow);

  delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                        static_cast<int>(ui::mojom::DialogButton::kCancel));
  delegate_->SetButtonLabel(ui::mojom::DialogButton::kOk,
                            u"Import bookmarks");
  delegate_->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Not now");

  // `delegate_` is owned by this view, so it can never outlive `this`.
  delegate_->SetAcceptCallback(base::BindOnce(
      &AvoraImportOfferView::OnAccept, base::Unretained(this)));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kPadding, kPadding), 8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Heading.
  auto* heading = AddChildView(std::make_unique<views::Label>(
      u"We found bookmarks from:"));
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  heading->SetEnabledColor(kBodyColor);

  // Browser list.
  for (const auto& browser_data : browsers_) {
    int count = 0;
    for (const auto& p : browser_data.profiles) {
      if (p.bookmark_count > 0) count += p.bookmark_count;
    }
    if (count == 0) continue;

    std::u16string browser_name =
        AvoraImportDialogView::BrowserDisplayName(browser_data.browser);

    for (const auto& p : browser_data.profiles) {
      if (p.bookmark_count <= 0) continue;

      std::u16string text = browser_name + u"  \u2014  " +
          base::UTF8ToUTF16(p.display_name) + u"    " +
          base::UTF8ToUTF16(base::NumberToString(p.bookmark_count));

      auto* row = AddChildView(std::make_unique<views::Label>(text));
      row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      row->SetEnabledColor(kHeadingColor);
      row->SetBorder(views::CreateEmptyBorder(
          gfx::Insets::TLBR(0, 8, 0, 0)));
    }
  }

  // Explanation.
  auto* spacer = AddChildView(std::make_unique<views::View>());
  spacer->SetPreferredSize(gfx::Size(kDialogWidth, 4));

  auto* explanation = AddChildView(std::make_unique<views::Label>(
      u"Your bookmarks will stay organized in Imported. Drag the ones "
      u"you use most to Favorites."));
  explanation->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  explanation->SetEnabledColor(kDetailColor);
  explanation->SetMultiLine(true);
  explanation->SetMaximumWidth(kDialogWidth - 2 * kPadding);
}

AvoraImportOfferView::~AvoraImportOfferView() = default;

void AvoraImportOfferView::OnAccept() {
  // Open the full import dialog.
  AvoraImportDialogView::Show(browser_, window_space_state_);
}

BEGIN_METADATA(AvoraImportOfferView)
END_METADATA

}  // namespace avora
