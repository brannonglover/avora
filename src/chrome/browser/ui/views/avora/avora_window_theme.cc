// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_window_theme.h"

#include "chrome/browser/avora/avora_space.h"
#include "ui/color/color_provider_key.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace avora {

namespace {

// The widget property a browser window's WindowTheme is published under.
constexpr char kWindowThemeKey[] = "__AVORA_WINDOW_THEME__";

// A theme change doesn't repaint anything by itself, and Avora's views read
// the palette as they paint, so every view is invalidated: many sit on
// layers of their own, which a repaint of their parent doesn't reach.
std::optional<ui::ColorProviderKey::ColorMode> ColorModeFor(
    const SpaceTheme& theme) {
  if (theme.empty()) {
    return std::nullopt;
  }
  return theme.mode == SpaceTheme::Mode::kLight
             ? ui::ColorProviderKey::ColorMode::kLight
             : ui::ColorProviderKey::ColorMode::kDark;
}

void RepaintTree(views::View* view) {
  view->SchedulePaint();
  for (views::View* child : view->children()) {
    RepaintTree(child);
  }
}

}  // namespace

// static
void WindowTheme::Attach(WindowSpaceState* space_state, views::Widget* widget) {
  if (!FromWidget(widget)) {
    new WindowTheme(space_state, widget);
  }
}

WindowTheme::WindowTheme(WindowSpaceState* space_state, views::Widget* widget)
    : space_state_(space_state),
      widget_(widget),
      palette_(DefaultSpaceThemePalette()) {
  space_state_observation_.Observe(space_state_);
  space_manager_observation_.Observe(space_state_->space_manager());
  widget_observation_.Observe(widget_);
  widget_->SetNativeWindowProperty(kWindowThemeKey, this);
  Update();
}

WindowTheme::~WindowTheme() = default;

void WindowTheme::OnWidgetDestroying(views::Widget* widget) {
  widget_->SetNativeWindowProperty(kWindowThemeKey, nullptr);
  delete this;
}

// static
WindowTheme* WindowTheme::FromWidget(const views::Widget* widget) {
  for (; widget; widget = widget->parent()) {
    if (void* theme = widget->GetNativeWindowProperty(kWindowThemeKey)) {
      return static_cast<WindowTheme*>(theme);
    }
  }
  return nullptr;
}

// static
const SpaceThemePalette& WindowTheme::PaletteFor(const views::View* view) {
  if (const WindowTheme* theme =
          view ? FromWidget(view->GetWidget()) : nullptr) {
    return theme->palette();
  }
  static const SpaceThemePalette kDefault = DefaultSpaceThemePalette();
  return kDefault;
}

// static
void WindowTheme::MatchColorMode(views::Widget* bubble) {
  if (const WindowTheme* theme = bubble ? FromWidget(bubble) : nullptr) {
    bubble->SetColorModeOverride(ColorModeFor(theme->theme()));
  }
}

void WindowTheme::SetPreview(std::optional<SpaceTheme> theme) {
  preview_ = std::move(theme);
  Update();
}

const std::string& WindowTheme::space_id() const {
  return space_state_->active_space_id();
}

void WindowTheme::OnWindowActiveSpaceChanged(const std::string& space_id) {
  // A preview belongs to the Space it was made for.
  preview_.reset();
  Update();
}

void WindowTheme::OnSpacesChanged() {
  Update();
}

SpaceTheme WindowTheme::CurrentTheme() const {
  if (preview_) {
    return *preview_;
  }
  const Space* space = space_state_->space_manager()->GetSpaceById(space_id());
  return space ? space->theme : SpaceTheme();
}

void WindowTheme::Update() {
  SpaceTheme theme = CurrentTheme();
  if (theme == theme_) {
    return;
  }
  theme_ = std::move(theme);
  palette_ = PaletteForSpaceTheme(theme_);
  widget_->SetColorModeOverride(ColorModeFor(theme_));
  widget_->ThemeChanged();
  RepaintTree(widget_->GetRootView());
  // Bubbles open over the window (the Theme Studio among them) keep their own
  // colour key, so ThemeChanged() above skips them; they repaint here.
  for (views::Widget* child :
       views::Widget::GetAllOwnedWidgets(widget_->GetNativeView())) {
    child->SetColorModeOverride(ColorModeFor(theme_));
    if (child->GetRootView()) {
      RepaintTree(child->GetRootView());
    }
  }
}

}  // namespace avora
