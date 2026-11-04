// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SIDEBAR_BACKGROUND_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SIDEBAR_BACKGROUND_H_

#include <memory>

namespace gfx {
class Canvas;
}

namespace views {
class Background;
class View;
}  // namespace views

namespace avora {

// The sidebar's fill in the window's theme.  A gradient theme runs top to
// bottom of the whole window, and each view paints only the slice it covers,
// so the sidebar header, tab strip and spaces bar join without a seam.
// Opaque, like the solid fills it replaces.
std::unique_ptr<views::Background> CreateSidebarBackground();

// Paints |view|'s slice of the window's sidebar gradient over its bounds and
// returns true, or returns false without painting when the window's theme is
// a single colour.  For backgrounds Avora doesn't own, such as Chromium's
// frame-coloured ones, which paint their solid colour when this says no.
bool PaintSidebarGradient(gfx::Canvas* canvas, const views::View* view);

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_SIDEBAR_BACKGROUND_H_
