// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_responsive_frame_view.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"

namespace avora {
namespace {

// A contents area 1200 wide and 900 tall, offset the way it is in a window
// with the sidebar on the left.
const gfx::Rect kAvailable(240, 50, 1200, 900);

TEST(AvoraResponsiveLayoutTest, PageIsCentredAtTheRequestedSize) {
  const ResponsiveLayout layout =
      ComputeResponsiveLayout(kAvailable, ResponsiveSize{390, 600});

  EXPECT_EQ(gfx::Size(390, 600), layout.page.size());
  // Centred horizontally: equal space either side.
  EXPECT_EQ(layout.page.x() - kAvailable.x(),
            kAvailable.right() - layout.page.right());
  // Hangs from just under the toolbar.
  EXPECT_EQ(kAvailable.y() + kResponsiveToolbarHeight, layout.page.y());
  EXPECT_EQ(kAvailable.y(), layout.toolbar.y());
  EXPECT_EQ(kAvailable.width(), layout.toolbar.width());
}

TEST(AvoraResponsiveLayoutTest, ZeroFillsThatAxis) {
  const ResponsiveLayout layout =
      ComputeResponsiveLayout(kAvailable, ResponsiveSize{390, 0});

  EXPECT_EQ(390, layout.page.width());
  EXPECT_EQ(layout.max_page.height(), layout.page.height());
  EXPECT_EQ(kAvailable.height() - kResponsiveToolbarHeight -
                kResponsiveBottomGutter,
            layout.page.height());

  const ResponsiveLayout fill =
      ComputeResponsiveLayout(kAvailable, ResponsiveSize{});
  EXPECT_EQ(fill.max_page, fill.page.size());
  EXPECT_EQ(kAvailable.width() - 2 * kResponsiveSideGutter,
            fill.page.width());
}

TEST(AvoraResponsiveLayoutTest, ClampsToWhatFitsAndToTheMinimum) {
  const ResponsiveLayout big =
      ComputeResponsiveLayout(kAvailable, ResponsiveSize{4000, 4000});
  EXPECT_EQ(big.max_page, big.page.size());

  const ResponsiveLayout tiny =
      ComputeResponsiveLayout(kAvailable, ResponsiveSize{20, 20});
  EXPECT_EQ(gfx::Size(kResponsiveMinPageSize, kResponsiveMinPageSize),
            tiny.page.size());

  // A window smaller than the minimum gets what fits rather than overflowing.
  const ResponsiveLayout cramped = ComputeResponsiveLayout(
      gfx::Rect(0, 0, 150, 150), ResponsiveSize{390, 844});
  EXPECT_LE(cramped.page.right(), 150);
  EXPECT_LE(cramped.page.bottom(), 150);
}

TEST(AvoraResponsiveLayoutTest, HandlesHugTheEdgesOutsideThePage) {
  const ResponsiveLayout layout =
      ComputeResponsiveLayout(kAvailable, ResponsiveSize{390, 600});

  // Nothing may overlap the page: on macOS the page is a native view above
  // the views hierarchy and would swallow the handles' events.
  EXPECT_FALSE(layout.left_handle.Intersects(layout.page));
  EXPECT_FALSE(layout.right_handle.Intersects(layout.page));
  EXPECT_FALSE(layout.bottom_handle.Intersects(layout.page));
  EXPECT_FALSE(layout.toolbar.Intersects(layout.page));

  EXPECT_EQ(layout.page.x() - kResponsiveHandleGap,
            layout.left_handle.right());
  EXPECT_EQ(layout.page.right() + kResponsiveHandleGap,
            layout.right_handle.x());
  EXPECT_EQ(layout.page.bottom() + kResponsiveHandleGap,
            layout.bottom_handle.y());
  EXPECT_EQ(layout.page.height(), layout.left_handle.height());
  EXPECT_EQ(layout.page.width(), layout.bottom_handle.width());

  // The handles stay inside the contents area even when the page fills it.
  const ResponsiveLayout fill =
      ComputeResponsiveLayout(kAvailable, ResponsiveSize{});
  EXPECT_TRUE(kAvailable.Contains(fill.left_handle));
  EXPECT_TRUE(kAvailable.Contains(fill.right_handle));
  EXPECT_TRUE(kAvailable.Contains(fill.bottom_handle));
}

TEST(AvoraResponsiveLayoutTest, LayoutIsTranslationInvariant) {
  // MultiContentsView lays the page out in its own coordinates and the frame
  // view lays its handles out in local ones; the two must agree exactly.
  const ResponsiveSize size{391, 700};
  const ResponsiveLayout offset = ComputeResponsiveLayout(kAvailable, size);
  const ResponsiveLayout local = ComputeResponsiveLayout(
      gfx::Rect(kAvailable.size()), size);
  gfx::Rect moved = local.page;
  moved.Offset(kAvailable.OffsetFromOrigin());
  EXPECT_EQ(offset.page, moved);
}

TEST(AvoraResponsiveSnapTest, SnapsWithinDistanceOnly) {
  EXPECT_EQ(768, SnapResponsiveWidth(768));
  EXPECT_EQ(768, SnapResponsiveWidth(768 + kResponsiveSnapDistance));
  EXPECT_EQ(768, SnapResponsiveWidth(768 - kResponsiveSnapDistance));
  EXPECT_EQ(768 + kResponsiveSnapDistance + 1,
            SnapResponsiveWidth(768 + kResponsiveSnapDistance + 1));
  EXPECT_EQ(390, SnapResponsiveWidth(386));
  EXPECT_EQ(500, SnapResponsiveWidth(500));
}

TEST(AvoraResponsiveSnapTest, BreakpointNames) {
  EXPECT_STREQ("xs", ResponsiveBreakpointName(390));
  EXPECT_STREQ("xs", ResponsiveBreakpointName(639));
  EXPECT_STREQ("sm", ResponsiveBreakpointName(640));
  EXPECT_STREQ("md", ResponsiveBreakpointName(768));
  EXPECT_STREQ("md", ResponsiveBreakpointName(1023));
  EXPECT_STREQ("lg", ResponsiveBreakpointName(1024));
  EXPECT_STREQ("xl", ResponsiveBreakpointName(1280));
  EXPECT_STREQ("2xl", ResponsiveBreakpointName(1536));
}

}  // namespace
}  // namespace avora
