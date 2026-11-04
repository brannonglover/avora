// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_theme_studio.h"

#include <string>
#include <string_view>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_space_theme.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/avora/avora_window_theme.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/dialog_delegate.h"

namespace avora {

class AvoraThemeStudioBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  views::Widget* window() { return browser_view()->GetWidget(); }
  WindowTheme* window_theme() { return WindowTheme::FromWidget(window()); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  std::string space_id() {
    return GetWindowSpaceState(browser())->active_space_id();
  }

  // A copy: GetSpaceById() points into the manager, which goes with it.
  Space SavedSpace() {
    const SpaceManager space_manager(prefs());
    const Space* space = space_manager.GetSpaceById(space_id());
    return space ? *space : Space();
  }
  SpaceTheme SavedTheme() { return SavedSpace().theme; }

  // Opens the studio over this window and returns its bubble.
  views::Widget* OpenStudio() {
    ShowThemeStudioBubble(browser_view(), prefs(), space_id());
    for (views::Widget* child :
         views::Widget::GetAllOwnedWidgets(window()->GetNativeView())) {
      if (child->widget_delegate()->GetWindowTitle() == u"Theme Studio") {
        return child;
      }
    }
    return nullptr;
  }

  static views::View* FindByClass(views::View* view, std::string_view name) {
    if (view->GetClassName() == name) {
      return view;
    }
    for (views::View* child : view->children()) {
      if (views::View* found = FindByClass(child, name)) {
        return found;
      }
    }
    return nullptr;
  }

  static views::View* FindByName(views::View* view, std::u16string_view name) {
    if (view->GetViewAccessibility().GetCachedName() == name) {
      return view;
    }
    for (views::View* child : view->children()) {
      if (views::View* found = FindByName(child, name)) {
        return found;
      }
    }
    return nullptr;
  }

  // Drags the pad's orb from the middle to |to|, in the pad's coordinates.
  static void DragOrb(views::View* pad, const gfx::Point& to) {
    const gfx::Point from = pad->GetLocalBounds().CenterPoint();
    const auto event = [](ui::EventType type, const gfx::Point& point) {
      return ui::MouseEvent(type, point, point, ui::EventTimeForNow(),
                            ui::EF_LEFT_MOUSE_BUTTON,
                            ui::EF_LEFT_MOUSE_BUTTON);
    };
    pad->OnMousePressed(event(ui::EventType::kMousePressed, from));
    pad->OnMouseDragged(event(ui::EventType::kMouseDragged, to));
    pad->OnMouseReleased(event(ui::EventType::kMouseReleased, to));
  }

  static void Close(views::Widget* studio, bool accept) {
    views::test::WidgetDestroyedWaiter waiter(studio);
    views::DialogDelegate* dialog =
        studio->widget_delegate()->AsDialogDelegate();
    if (accept) {
      dialog->AcceptDialog();
    } else {
      dialog->CancelDialog();
    }
    waiter.Wait();
  }

  SkColor FrameColor() {
    return window()->GetColorProvider()->GetColor(ui::kColorFrameActive);
  }
};

// Dragging repaints the window at once, but saves nothing until Done.
IN_PROC_BROWSER_TEST_F(AvoraThemeStudioBrowserTest, DragPreviewsThenDoneSaves) {
  ASSERT_TRUE(window_theme());
  ASSERT_TRUE(SavedTheme().empty());
  const SkColor default_frame = FrameColor();

  views::Widget* studio = OpenStudio();
  ASSERT_TRUE(studio);
  views::View* pad = FindByClass(studio->GetRootView(), "ThemePadView");
  ASSERT_TRUE(pad);

  DragOrb(pad, gfx::Point(pad->width() / 4, pad->height() / 5));
  EXPECT_TRUE(window_theme()->previewing());
  const SpaceTheme previewed = window_theme()->theme();
  ASSERT_FALSE(previewed.empty());
  EXPECT_TRUE(SavedTheme().empty());
  EXPECT_NE(FrameColor(), default_frame);
  EXPECT_EQ(FrameColor(), PaletteForSpaceTheme(previewed).sidebar_top);

  Close(studio, /*accept=*/true);
  EXPECT_FALSE(window_theme()->previewing());
  EXPECT_EQ(SavedTheme(), previewed);
  EXPECT_EQ(window_theme()->theme(), previewed);
  EXPECT_EQ(SavedSpace().accent_color,
            SpaceThemeColorHex(previewed.colors.front()));
  // The colour cache was dropped on close; the window rebuilt its colours.
  EXPECT_EQ(FrameColor(), PaletteForSpaceTheme(previewed).sidebar_top);
}

// Cancel puts the window back as it was.
IN_PROC_BROWSER_TEST_F(AvoraThemeStudioBrowserTest, CancelReverts) {
  const SkColor default_frame = FrameColor();
  views::Widget* studio = OpenStudio();
  ASSERT_TRUE(studio);
  views::View* pad = FindByClass(studio->GetRootView(), "ThemePadView");
  ASSERT_TRUE(pad);
  DragOrb(pad, gfx::Point(pad->width() - 20, 20));
  ASSERT_TRUE(window_theme()->previewing());

  Close(studio, /*accept=*/false);
  EXPECT_FALSE(window_theme()->previewing());
  EXPECT_TRUE(window_theme()->theme().empty());
  EXPECT_TRUE(SavedTheme().empty());
  EXPECT_EQ(FrameColor(), default_frame);
}

// A mood card applies that mood exactly, gradient and all.
IN_PROC_BROWSER_TEST_F(AvoraThemeStudioBrowserTest, MoodApplies) {
  views::Widget* studio = OpenStudio();
  ASSERT_TRUE(studio);
  auto* dusk = static_cast<views::Button*>(
      FindByName(studio->GetRootView(), u"Dusk mood"));
  ASSERT_TRUE(dusk);
  views::test::ButtonTestApi(dusk).NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
      ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
      ui::EF_LEFT_MOUSE_BUTTON));

  SpaceTheme expected;
  for (const SpaceThemeMood& mood : GetSpaceThemeMoods()) {
    if (mood.id == "dusk") {
      expected = mood.ToTheme();
    }
  }
  ASSERT_EQ(expected.colors.size(), 2u);
  EXPECT_EQ(window_theme()->theme(), expected);

  Close(studio, /*accept=*/true);
  EXPECT_EQ(SavedTheme(), expected);
  EXPECT_NE(window_theme()->palette().sidebar_top,
            window_theme()->palette().sidebar_bottom);
}

// A themed Space's colours follow the window into and out of it.
IN_PROC_BROWSER_TEST_F(AvoraThemeStudioBrowserTest, ThemeFollowsTheSpace) {
  SpaceManager space_manager(prefs());
  const std::string themed = space_id();
  const SpaceTheme theme = GetSpaceThemeMoods().back().ToTheme();
  space_manager.SetSpaceTheme(themed, theme);
  EXPECT_EQ(window_theme()->theme(), theme);

  const std::string plain = space_manager.CreateSpace("Plain");
  GetWindowSpaceState(browser())->SetActiveSpaceId(plain);
  EXPECT_TRUE(window_theme()->theme().empty());
  EXPECT_EQ(FrameColor(), DefaultSpaceThemePalette().sidebar_top);

  GetWindowSpaceState(browser())->SetActiveSpaceId(themed);
  EXPECT_EQ(window_theme()->theme(), theme);
  EXPECT_EQ(FrameColor(), PaletteForSpaceTheme(theme).sidebar_top);
}

}  // namespace avora
