// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Avora: Replaces the standard Chromium extensions menu with a compact
// icon-grid popup.  Extensions are shown by image in a wrapping grid.
// A "+" button opens the Chrome Web Store to add new extensions.
// A gear button opens chrome://extensions for management.

#include "chrome/browser/ui/views/extensions/extensions_menu_coordinator.h"

#include <memory>
#include <string>
#include <vector>

#include "base/check_deref.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/extensions/extensions_container.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_action_view_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/extensions/extensions_menu_delegate_desktop.h"
#include "extensions/common/extension_features.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/metadata/view_factory.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr char kChromeWebStoreURL[] =
    "https://chromewebstore.google.com/";

// Avora extensions popup dimensions.
constexpr int kIconSize = 24;
constexpr int kButtonSize = 36;
constexpr int kGridPadding = 10;
constexpr int kIconSpacing = 6;
constexpr int kIconsPerRow = 4;
constexpr int kPopupFixedWidth =
    2 * kGridPadding + kIconsPerRow * kButtonSize +
    (kIconsPerRow - 1) * kIconSpacing;

// ---------------------------------------------------------------------------
// PlusIconSource — draws a "+" sign for the add-extension button.
// ---------------------------------------------------------------------------
class PlusIconSource : public gfx::CanvasImageSource {
 public:
  PlusIconSource(int icon_size, SkColor color)
      : gfx::CanvasImageSource(gfx::Size(icon_size, icon_size)),
        color_(color) {}

  void Draw(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_);
    flags.setStrokeWidth(2.0f);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);

    const float center = size().width() / 2.0f;
    const float arm = size().width() / 4.0f;

    canvas->DrawLine(gfx::PointF(center - arm, center),
                     gfx::PointF(center + arm, center), flags);
    canvas->DrawLine(gfx::PointF(center, center - arm),
                     gfx::PointF(center, center + arm), flags);
  }

 private:
  SkColor color_;
};

// ---------------------------------------------------------------------------
// SlidersIconSource — draws horizontal slider bars for "Manage extensions".
// ---------------------------------------------------------------------------
class SlidersIconSource : public gfx::CanvasImageSource {
 public:
  SlidersIconSource(int icon_size, SkColor color)
      : gfx::CanvasImageSource(gfx::Size(icon_size, icon_size)),
        color_(color) {}

  void Draw(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_);
    flags.setStrokeWidth(1.5f);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);

    const float w = size().width();
    const float pad = w * 0.15f;
    const float right = w - pad;

    // Three horizontal lines at different vertical positions.
    const float y1 = w * 0.28f;
    const float y2 = w * 0.50f;
    const float y3 = w * 0.72f;

    canvas->DrawLine(gfx::PointF(pad, y1), gfx::PointF(right, y1), flags);
    canvas->DrawLine(gfx::PointF(pad, y2), gfx::PointF(right, y2), flags);
    canvas->DrawLine(gfx::PointF(pad, y3), gfx::PointF(right, y3), flags);

    // Small knob circles on each line at staggered positions.
    cc::PaintFlags dot_flags;
    dot_flags.setAntiAlias(true);
    dot_flags.setColor(color_);
    dot_flags.setStyle(cc::PaintFlags::kFill_Style);

    const float dot_r = 2.5f;
    canvas->DrawCircle(gfx::PointF(w * 0.60f, y1), dot_r, dot_flags);
    canvas->DrawCircle(gfx::PointF(w * 0.35f, y2), dot_r, dot_flags);
    canvas->DrawCircle(gfx::PointF(w * 0.70f, y3), dot_r, dot_flags);
  }

 private:
  SkColor color_;
};

// ---------------------------------------------------------------------------
// AvoraExtensionsGridView
//
// A compact view that displays installed extension icons in a wrapping
// grid.  A "+" button opens the Chrome Web Store and a gear button
// opens chrome://extensions for management.
// ---------------------------------------------------------------------------
class AvoraExtensionsGridView : public views::View {
  METADATA_HEADER(AvoraExtensionsGridView, views::View)

 public:
  AvoraExtensionsGridView(BrowserWindowInterface* browser,
                          ExtensionsContainer* extensions_container)
      : browser_(browser),
        extensions_container_(extensions_container) {
    auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets(kGridPadding), kIconSpacing));
    root_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    Profile* profile = browser_->GetProfile();
    ToolbarActionsModel* model = ToolbarActionsModel::Get(profile);
    content::WebContents* web_contents =
        browser_->GetTabStripModel()->GetActiveWebContents();
    const gfx::Size icon_size(kIconSize, kIconSize);

    // --- Icon grid rows ---
    views::View* current_row = nullptr;
    int items_in_row = 0;

    auto ensure_row = [&]() {
      if (!current_row || items_in_row >= kIconsPerRow) {
        current_row = AddChildView(std::make_unique<views::View>());
        auto* row_layout =
            current_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
                views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
                kIconSpacing));
        row_layout->set_main_axis_alignment(
            views::BoxLayout::MainAxisAlignment::kCenter);
        items_in_row = 0;
      }
    };

    for (const auto& action_id : model->action_ids()) {
      ToolbarActionViewModel* action =
          extensions_container->GetActionForId(action_id);
      if (!action) {
        continue;
      }

      ensure_row();

      auto button = std::make_unique<views::ImageButton>(
          base::BindRepeating(&AvoraExtensionsGridView::OnExtensionClicked,
                              base::Unretained(this), action_id));

      ui::ImageModel icon = action->GetIcon(web_contents, icon_size);
      button->SetImageModel(views::Button::STATE_NORMAL, icon);
      button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
      button->SetTooltipText(action->GetActionName());
      button->SetAccessibleName(action->GetActionName());

      current_row->AddChildView(std::move(button));
      items_in_row++;
    }

    // "+" button — opens the Chrome Web Store.
    ensure_row();

    auto plus_button = std::make_unique<views::ImageButton>(
        base::BindRepeating(&AvoraExtensionsGridView::OnPlusClicked,
                            base::Unretained(this)));

    gfx::ImageSkia plus_icon = gfx::CanvasImageSource::MakeImageSkia<
        PlusIconSource>(kIconSize, SkColorSetARGB(200, 255, 255, 255));
    plus_button->SetImageModel(views::Button::STATE_NORMAL,
                               ui::ImageModel::FromImageSkia(plus_icon));
    plus_button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
    plus_button->SetTooltipText(u"Get extensions");
    plus_button->SetAccessibleName(u"Get extensions");

    current_row->AddChildView(std::move(plus_button));

    // --- Separator (thin horizontal line matching sidebar border style) ---
    auto separator = std::make_unique<views::View>();
    separator->SetPreferredSize(
        gfx::Size(kPopupFixedWidth - 2 * kGridPadding, 1));
    separator->SetBackground(views::CreateSolidBackground(
        SkColorSetARGB(0x40, 0xED, 0xF2, 0xF5)));
    AddChildView(std::move(separator));

    // --- Manage extensions row (left-aligned) ---
    auto* manage_row = AddChildView(std::make_unique<views::View>());
    manage_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
        kIconSpacing));

    auto manage_button = std::make_unique<views::ImageButton>(
        base::BindRepeating(&AvoraExtensionsGridView::OnManageClicked,
                            base::Unretained(this)));

    gfx::ImageSkia manage_icon = gfx::CanvasImageSource::MakeImageSkia<
        SlidersIconSource>(kIconSize, SkColorSetARGB(200, 255, 255, 255));
    manage_button->SetImageModel(views::Button::STATE_NORMAL,
                                 ui::ImageModel::FromImageSkia(manage_icon));
    manage_button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
    manage_button->SetTooltipText(u"Manage extensions");
    manage_button->SetAccessibleName(u"Manage extensions");

    manage_row->AddChildView(std::move(manage_button));
  }

  ~AvoraExtensionsGridView() override = default;

 private:
  void OnExtensionClicked(const std::string& extension_id) {
    auto* container = extensions_container_.get();

    if (auto* widget = GetWidget()) {
      widget->Close();
    }

    if (auto* action = container->GetActionForId(extension_id)) {
      action->ExecuteUserAction(
          ToolbarActionViewModel::InvocationSource::kMenuEntry);
    }
  }

  void OnPlusClicked() {
    auto* browser = browser_.get();

    if (auto* widget = GetWidget()) {
      widget->Close();
    }

    NavigateParams params(browser, GURL(kChromeWebStoreURL),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  }

  void OnManageClicked() {
    auto* browser = browser_.get();

    if (auto* widget = GetWidget()) {
      widget->Close();
    }

    chrome::ShowExtensions(browser);
  }

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<ExtensionsContainer> extensions_container_;
};

BEGIN_METADATA(AvoraExtensionsGridView)
END_METADATA

}  // namespace

// ---------------------------------------------------------------------------
// ExtensionsMenuCoordinator
// ---------------------------------------------------------------------------

ExtensionsMenuCoordinator::ExtensionsMenuCoordinator(
    BrowserWindowInterface* browser,
    ExtensionsContainer* extensions_container)
    : browser_(browser),
      extensions_container_(CHECK_DEREF(extensions_container)) {}

ExtensionsMenuCoordinator::~ExtensionsMenuCoordinator() {
  if (views::Widget* const menu = GetExtensionsMenuWidget()) {
    menu->CloseNow();
  }
}

void ExtensionsMenuCoordinator::Show(
    views::BubbleAnchor anchor,
    ExtensionsContainerViews* extensions_container_views) {
  std::unique_ptr<views::BubbleDialogDelegate> bubble_delegate =
      CreateExtensionsMenuBubbleDialogDelegate(anchor,
                                               extensions_container_views);

  views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble_delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET)
      ->Show();
}

void ExtensionsMenuCoordinator::Hide() {
  if (views::Widget* const menu = GetExtensionsMenuWidget()) {
    menu->CloseNow();
  }
}

bool ExtensionsMenuCoordinator::IsShowing() const {
  return bubble_tracker_.view() != nullptr;
}

views::Widget* ExtensionsMenuCoordinator::GetExtensionsMenuWidget() {
  return IsShowing() ? bubble_tracker_.view()->GetWidget() : nullptr;
}

std::unique_ptr<views::BubbleDialogDelegate>
ExtensionsMenuCoordinator::CreateExtensionsMenuBubbleDialogDelegateForTesting(
    views::BubbleAnchor anchor,
    ExtensionsContainerViews* extensions_container_views) {
  return CreateExtensionsMenuBubbleDialogDelegate(anchor,
                                                  extensions_container_views);
}

std::unique_ptr<views::BubbleDialogDelegate>
ExtensionsMenuCoordinator::CreateExtensionsMenuBubbleDialogDelegate(
    views::BubbleAnchor anchor,
    ExtensionsContainerViews* extensions_container_views) {
  auto bubble_delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::TOP_CENTER,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  bubble_delegate->SetOwnedByWidget(
      views::WidgetDelegate::OwnedByWidgetPassKey());
  bubble_delegate->set_margins(gfx::Insets(0));
  bubble_delegate->set_fixed_width(kPopupFixedWidth);
  bubble_delegate->set_highlight_button_when_shown(false);
  bubble_delegate->SetButtons(
      static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble_delegate->SetEnableArrowKeyTraversal(true);

  // Avora: use the compact icon-grid view instead of the standard menu.
  auto* bubble_contents = bubble_delegate->SetContentsView(
      std::make_unique<AvoraExtensionsGridView>(
          browser_, &extensions_container_.get()));

  bubble_view_observation_.Observe(bubble_contents);
  bubble_tracker_.SetView(bubble_contents);

  return bubble_delegate;
}

void ExtensionsMenuCoordinator::OnViewIsDeleting(views::View* observed_view) {
  bubble_tracker_.SetView(nullptr);
  bubble_view_observation_.Reset();
  menu_delegate_.reset();
}
