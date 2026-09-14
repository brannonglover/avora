// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_space_label_view.h"

#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace avora {

namespace {

// Matches the metrics the label previously had when it was inlined in the tab
// strip, so this swap is not a visual change.
constexpr int kRowHeight = 28;
constexpr int kHorizontalInset = 16;
constexpr int kFontSize = 12;
constexpr SkColor kTextColor = SkColorSetARGB(0x80, 0xED, 0xF2, 0xF5);

}  // namespace

AvoraSpaceLabelView::AvoraSpaceLabelView(BrowserWindowInterface* browser,
                                         WindowSpaceState* window_space_state)
    : browser_(browser), window_space_state_(window_space_state) {
  SetPreferredSize(gfx::Size(0, kRowHeight));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto label = std::make_unique<views::Label>(
      std::u16string(),
      views::Label::CustomFont{gfx::FontList({std::string("system-ui")},
                                             gfx::Font::NORMAL, kFontSize,
                                             gfx::Font::Weight::MEDIUM)});
  label->SetEnabledColor(kTextColor);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::VH(0, kHorizontalInset)));
  label_ = AddChildView(std::move(label));

  if (Profile* profile = browser_ ? browser_->GetProfile() : nullptr) {
    space_manager_ = std::make_unique<SpaceManager>(profile->GetPrefs());
    space_manager_->AddObserver(this);
  }

  if (window_space_state_) {
    window_space_state_->AddObserver(this);
  }

  UpdateLabel();
}

AvoraSpaceLabelView::~AvoraSpaceLabelView() {
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
  }
}

void AvoraSpaceLabelView::OnSpacesChanged() {
  UpdateLabel();
}

void AvoraSpaceLabelView::OnActiveSpaceChanged(const std::string& space_id) {
  // Ignored when per-window state is driving the label.
  if (window_space_state_) {
    return;
  }
  UpdateLabel();
}

void AvoraSpaceLabelView::OnWindowActiveSpaceChanged(
    const std::string& space_id) {
  UpdateLabel();
}

void AvoraSpaceLabelView::UpdateLabel() {
  if (!label_) {
    return;
  }

  std::u16string text = u"Default Space";

  // Prefer the window-local active Space.
  if (window_space_state_ && space_manager_) {
    if (const Space* active = space_manager_->GetSpaceById(
            window_space_state_->active_space_id())) {
      if (!active->name.empty()) {
        text = base::UTF8ToUTF16(active->name);
      }
    }
  } else if (space_manager_) {
    if (const Space* active = space_manager_->GetActiveSpace()) {
      if (!active->name.empty()) {
        text = base::UTF8ToUTF16(active->name);
      }
    }
  }

  label_->SetText(text);
}

BEGIN_METADATA(AvoraSpaceLabelView)
END_METADATA

}  // namespace avora
