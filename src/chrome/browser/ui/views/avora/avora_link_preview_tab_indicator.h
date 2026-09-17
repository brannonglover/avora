// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_INDICATOR_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_INDICATOR_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/color_palette.h"
#include "ui/views/view.h"

namespace avora {

// Marker shown in a tab row while that tab carries a parked link preview.
// A preview only paints over the tab it was opened from, so without this the
// preview would be invisible from every other tab and the user would have no
// way of knowing a tab still has one waiting behind it.
class AvoraLinkPreviewTabIndicator : public views::View {
  METADATA_HEADER(AvoraLinkPreviewTabIndicator, views::View)

 public:
  // Matches the 16dip design slot the favicon and alert indicator occupy.
  static constexpr int kSize = 16;

  AvoraLinkPreviewTabIndicator();
  AvoraLinkPreviewTabIndicator(const AvoraLinkPreviewTabIndicator&) = delete;
  AvoraLinkPreviewTabIndicator& operator=(const AvoraLinkPreviewTabIndicator&) =
      delete;
  ~AvoraLinkPreviewTabIndicator() override;

  bool has_preview() const { return has_preview_; }
  void SetHasPreview(bool has_preview);

  // Tracks the tab's own foreground colour so the marker reads as part of the
  // row rather than as chrome pasted on top of it.
  void SetColor(SkColor color);

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  bool has_preview_ = false;
  SkColor color_ = gfx::kPlaceholderColor;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_LINK_PREVIEW_TAB_INDICATOR_H_
