// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_theme_studio.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_space_theme.h"
#include "chrome/browser/ui/color/avora_color_mixer.h"
#include "chrome/browser/ui/views/avora/avora_window_theme.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/text_utils.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace avora {

namespace {

constexpr int kContentWidth = 320;
constexpr int kPadHeight = 168;
constexpr int kPadRadius = 14;
constexpr int kOrbRadius = 11;
// How close a press has to land to an orb to pick it up rather than move the
// selected one there.
constexpr int kOrbHitRadius = 18;
constexpr int kRowGap = 10;
constexpr int kChipHeight = 28;
constexpr int kMoodColumns = 4;
constexpr int kMoodGap = 8;
constexpr int kMoodHeight = 58;

// The pad's colours are rounded to this many steps across and up before they
// reach the window.  Each distinct theme the window shows is a colour
// provider Chromium keeps until the studio closes, so a long drag must not
// mint one per pixel; at these steps neighbours are indistinguishable.
constexpr double kHueSteps = 120;
constexpr double kVibranceSteps = 40;

// How often a drag repaints the window.  Re-theming a window rebuilds its
// colour provider, so this is paced rather than per mouse move.
constexpr base::TimeDelta kPreviewInterval = base::Milliseconds(50);

// How long the orbs take to glide to a mood.
constexpr base::TimeDelta kGlideDuration = base::Milliseconds(340);

gfx::FontList UiFont(int size, gfx::Font::Weight weight) {
  return gfx::FontList({std::string("system-ui")}, gfx::Font::NORMAL, size,
                       weight);
}

void FillRoundRect(gfx::Canvas* canvas,
                   const gfx::RectF& bounds,
                   SkColor color,
                   float radius) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);
  canvas->DrawRoundRect(bounds, radius, flags);
}

// One colour on the pad.  |color| is what the theme stores; it is worked out
// from the position whenever the orb moves, but a mood's colours are kept
// exactly as the mood names them until the user drags.
struct Orb {
  double hue = 0;
  double vibrance = 0;
  SkColor color = SK_ColorWHITE;
};

Orb OrbForColor(SkColor color) {
  const auto [hue, vibrance] = SpaceThemePadPosition(color);
  return {hue, vibrance, color};
}

SkColor PadColor(double hue, double vibrance, SpaceTheme::Mode mode) {
  return SpaceThemePadColor(std::round(hue * kHueSteps) / kHueSteps,
                            std::round(vibrance * kVibranceSteps) /
                                kVibranceSteps,
                            mode);
}

// Hue runs round a circle, so the short way from 0.9 to 0.1 is up through 1.
double LerpHue(double from, double to, double t) {
  double delta = to - from;
  if (delta > 0.5) {
    delta -= 1.0;
  } else if (delta < -0.5) {
    delta += 1.0;
  }
  double hue = from + delta * t;
  return hue - std::floor(hue);
}

// ── Pad ─────────────────────────────────────────────────────────────────────

// Hue across, vibrance up, with one or two orbs to drag.
class ThemePadView : public views::View {
  METADATA_HEADER(ThemePadView, views::View)

 public:
  class Delegate {
   public:
    // An orb moved.  |settled| is false while a drag is still under way.
    virtual void OnOrbsMoved(bool settled) = 0;

   protected:
    virtual ~Delegate() = default;
  };

  explicit ThemePadView(Delegate* delegate) : delegate_(delegate) {
    SetPreferredSize(gfx::Size(kContentWidth, kPadHeight));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(
        u"Colour pad. Arrow keys move the selected colour; space switches "
        u"between colours.");
  }

  const std::vector<Orb>& orbs() const { return orbs_; }

  void SetOrbs(std::vector<Orb> orbs) {
    orbs_ = std::move(orbs);
    selected_ = std::min(selected_, orbs_.empty() ? 0 : orbs_.size() - 1);
    SchedulePaint();
  }

  void SetMode(SpaceTheme::Mode mode) {
    if (mode == mode_) {
      return;
    }
    mode_ = mode;
    field_ = gfx::ImageSkia();
    for (Orb& orb : orbs_) {
      orb.color = PadColor(orb.hue, orb.vibrance, mode_);
    }
    SchedulePaint();
  }

  // Dims the orbs while the Space has no theme, so the pad reads as "not
  // applied yet" rather than as a colour already in use.
  void SetDormant(bool dormant) {
    dormant_ = dormant;
    SchedulePaint();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Rect bounds = GetLocalBounds();
    if (field_.isNull() || field_.size() != bounds.size()) {
      field_ = BuildField(bounds.size());
    }
    {
      gfx::ScopedCanvas scoped(canvas);
      canvas->sk_canvas()->clipRRect(
          SkRRect::MakeRectXY(gfx::RectToSkRect(bounds), kPadRadius,
                              kPadRadius),
          true);
      canvas->DrawImageInt(field_, 0, 0);
    }

    if (orbs_.size() == 2) {
      // The two colours blend into each other: say so with a thread.
      cc::PaintFlags line;
      line.setAntiAlias(true);
      line.setStyle(cc::PaintFlags::kStroke_Style);
      line.setStrokeWidth(2);
      line.setColor(SkColorSetA(SK_ColorWHITE, dormant_ ? 0x40 : 0x90));
      canvas->DrawLine(OrbCenter(orbs_[0]), OrbCenter(orbs_[1]), line);
    }

    for (size_t i = 0; i < orbs_.size(); ++i) {
      PaintOrb(canvas, orbs_[i], i == selected_);
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    RequestFocus();
    const gfx::PointF point(event.location());
    std::optional<size_t> hit;
    float best = kOrbHitRadius;
    for (size_t i = 0; i < orbs_.size(); ++i) {
      const float distance = (OrbCenter(orbs_[i]) - point).Length();
      if (distance <= best) {
        best = distance;
        hit = i;
      }
    }
    if (hit) {
      selected_ = *hit;
    } else if (orbs_.empty()) {
      orbs_.push_back(Orb());
      selected_ = 0;
    }
    MoveSelected(point);
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    MoveSelected(gfx::PointF(event.location()));
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    delegate_->OnOrbsMoved(/*settled=*/true);
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (orbs_.empty()) {
      return false;
    }
    if (event.key_code() == ui::VKEY_SPACE) {
      selected_ = (selected_ + 1) % orbs_.size();
      SchedulePaint();
      return true;
    }
    double hue = 0;
    double vibrance = 0;
    switch (event.key_code()) {
      case ui::VKEY_LEFT:
        hue = -1.0 / 60;
        break;
      case ui::VKEY_RIGHT:
        hue = 1.0 / 60;
        break;
      case ui::VKEY_UP:
        vibrance = 1.0 / 20;
        break;
      case ui::VKEY_DOWN:
        vibrance = -1.0 / 20;
        break;
      default:
        return false;
    }
    Orb& orb = orbs_[selected_];
    orb.hue = LerpHue(orb.hue, orb.hue + hue, 1.0);
    orb.vibrance = std::clamp(orb.vibrance + vibrance, 0.0, 1.0);
    orb.color = PadColor(orb.hue, orb.vibrance, mode_);
    SchedulePaint();
    delegate_->OnOrbsMoved(/*settled=*/true);
    return true;
  }

  void OnFocus() override { SchedulePaint(); }
  void OnBlur() override { SchedulePaint(); }

 private:
  gfx::ImageSkia BuildField(const gfx::Size& size) const {
    SkBitmap bitmap;
    bitmap.allocN32Pixels(size.width(), size.height());
    const int last_x = std::max(1, size.width() - 1);
    const int last_y = std::max(1, size.height() - 1);
    for (int y = 0; y < size.height(); ++y) {
      const double vibrance = 1.0 - static_cast<double>(y) / last_y;
      for (int x = 0; x < size.width(); ++x) {
        *bitmap.getAddr32(x, y) = SkPreMultiplyColor(SpaceThemePadColor(
            static_cast<double>(x) / last_x, vibrance, mode_));
      }
    }
    bitmap.setImmutable();
    return gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
  }

  gfx::PointF OrbCenter(const Orb& orb) const {
    const gfx::RectF area = OrbArea();
    return gfx::PointF(area.x() + orb.hue * area.width(),
                       area.y() + (1.0 - orb.vibrance) * area.height());
  }

  // Where orb centres may go: inset so an orb never hangs off the pad.
  gfx::RectF OrbArea() const {
    gfx::RectF area(GetLocalBounds());
    area.Inset(kOrbRadius + 2);
    return area;
  }

  void MoveSelected(const gfx::PointF& point) {
    if (orbs_.empty()) {
      return;
    }
    const gfx::RectF area = OrbArea();
    Orb& orb = orbs_[selected_];
    orb.hue = std::clamp((point.x() - area.x()) / area.width(), 0.0f, 1.0f);
    orb.vibrance =
        1.0 - std::clamp((point.y() - area.y()) / area.height(), 0.0f, 1.0f);
    orb.color = PadColor(orb.hue, orb.vibrance, mode_);
    SchedulePaint();
    delegate_->OnOrbsMoved(/*settled=*/false);
  }

  void PaintOrb(gfx::Canvas* canvas, const Orb& orb, bool selected) const {
    const gfx::PointF center = OrbCenter(orb);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);

    // A soft glow in the orb's own colour, so it seems lit from inside.
    if (!dormant_) {
      for (int ring = 3; ring >= 1; --ring) {
        flags.setColor(SkColorSetA(orb.color, 0x22));
        canvas->DrawCircle(center, kOrbRadius + ring * 3, flags);
      }
    }
    flags.setColor(SkColorSetA(SK_ColorBLACK, 0x40));
    canvas->DrawCircle(center + gfx::Vector2dF(0, 1.5f), kOrbRadius, flags);
    flags.setColor(dormant_ ? SkColorSetA(orb.color, 0x99) : orb.color);
    canvas->DrawCircle(center, kOrbRadius, flags);

    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(selected ? 3 : 2);
    flags.setColor(SK_ColorWHITE);
    canvas->DrawCircle(center, kOrbRadius, flags);
    if (selected && HasFocus()) {
      flags.setStrokeWidth(1.5f);
      flags.setColor(SkColorSetA(SK_ColorWHITE, 0xB0));
      canvas->DrawCircle(center, kOrbRadius + 5, flags);
    }
  }

  const raw_ptr<Delegate> delegate_;
  std::vector<Orb> orbs_;
  size_t selected_ = 0;
  SpaceTheme::Mode mode_ = SpaceTheme::Mode::kDark;
  bool dormant_ = false;
  gfx::ImageSkia field_;
};

BEGIN_METADATA(ThemePadView)
END_METADATA

// ── Chips ───────────────────────────────────────────────────────────────────

// A small pill button: the Dark / Light switch, the gradient toggle, Reset.
class ChipButton : public views::Button {
  METADATA_HEADER(ChipButton, views::Button)

 public:
  ChipButton(std::u16string text, PressedCallback callback)
      : views::Button(std::move(callback)), text_(std::move(text)) {
    GetViewAccessibility().SetName(text_);
    const int width =
        gfx::GetStringWidth(text_, UiFont(12, gfx::Font::Weight::MEDIUM)) + 24;
    SetPreferredSize(gfx::Size(width, kChipHeight));
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    GetViewAccessibility().SetIsSelected(selected_);
    SchedulePaint();
  }

 protected:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const SpaceThemePalette& palette = WindowTheme::PaletteFor(this);
    const gfx::RectF bounds(GetLocalBounds());
    const bool hovered = GetState() == STATE_HOVERED ||
                         GetState() == STATE_PRESSED;
    if (selected_) {
      FillRoundRect(canvas, bounds, palette.text, bounds.height() / 2);
    } else {
      FillRoundRect(canvas, bounds,
                    SkColorSetA(palette.wash, hovered ? 0x22 : 0x12),
                    bounds.height() / 2);
    }
    canvas->DrawStringRectWithFlags(
        text_, UiFont(12, gfx::Font::Weight::MEDIUM),
        selected_ ? palette.surface : palette.text, GetLocalBounds(),
        gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  std::u16string text_;
  bool selected_ = false;
};

BEGIN_METADATA(ChipButton)
END_METADATA

// A mood, drawn as a tiny sidebar in that mood's own colours.
class MoodButton : public views::Button {
  METADATA_HEADER(MoodButton, views::Button)

 public:
  MoodButton(const SpaceThemeMood& mood, PressedCallback callback)
      : views::Button(std::move(callback)),
        mood_(mood),
        theme_(mood.ToTheme()),
        palette_(PaletteForSpaceTheme(theme_)) {
    GetViewAccessibility().SetName(base::UTF8ToUTF16(mood.name) + u" mood");
    SetPreferredSize(gfx::Size(
        (kContentWidth - (kMoodColumns - 1) * kMoodGap) / kMoodColumns,
        kMoodHeight));
  }

  const SpaceThemeMood& mood() const { return mood_; }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    GetViewAccessibility().SetIsSelected(selected_);
    SchedulePaint();
  }

 protected:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    gfx::RectF card(GetLocalBounds());
    card.Inset(2);
    const bool hovered = GetState() == STATE_HOVERED ||
                         GetState() == STATE_PRESSED;
    if (hovered) {
      // Lifts a touch towards the pointer.
      card.Offset(0, -1);
    }

    const SkPoint points[] = {SkPoint::Make(0, card.y()),
                              SkPoint::Make(0, card.bottom())};
    const SkColor4f colors[] = {SkColor4f::FromColor(palette_.sidebar_top),
                                SkColor4f::FromColor(palette_.sidebar_bottom)};
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setShader(cc::PaintShader::MakeLinearGradient(
        points, colors, nullptr, 2, SkTileMode::kClamp));
    canvas->DrawRoundRect(card, 10, flags);

    // The picked colours as dots, the way they sit on the pad.
    cc::PaintFlags dot;
    dot.setAntiAlias(true);
    float x = card.x() + 12;
    for (SkColor color : theme_.colors) {
      dot.setColor(color);
      canvas->DrawCircle(gfx::PointF(x, card.y() + 14), 6, dot);
      x += 10;
    }

    gfx::Rect label = gfx::ToEnclosingRect(card);
    label.Inset(gfx::Insets::TLBR(24, 10, 4, 6));
    canvas->DrawStringRectWithFlags(base::UTF8ToUTF16(mood_.name),
                                    UiFont(12, gfx::Font::Weight::SEMIBOLD),
                                    palette_.text, label,
                                    gfx::Canvas::TEXT_ALIGN_LEFT);

    if (selected_ || HasFocus()) {
      cc::PaintFlags ring;
      ring.setAntiAlias(true);
      ring.setStyle(cc::PaintFlags::kStroke_Style);
      ring.setStrokeWidth(2);
      ring.setColor(WindowTheme::PaletteFor(this).text);
      gfx::RectF outline(GetLocalBounds());
      outline.Inset(1);
      canvas->DrawRoundRect(outline, 11, ring);
    }
  }

 private:
  const SpaceThemeMood mood_;
  const SpaceTheme theme_;
  const SpaceThemePalette palette_;
  bool selected_ = false;
};

BEGIN_METADATA(MoodButton)
END_METADATA

// ── Studio ──────────────────────────────────────────────────────────────────

class ThemeStudioView : public views::View,
                        public ThemePadView::Delegate,
                        public views::AnimationDelegateViews {
  METADATA_HEADER(ThemeStudioView, views::View)

 public:
  ThemeStudioView(PrefService* prefs,
                  std::string space_id,
                  base::WeakPtr<WindowTheme> window_theme)
      : views::AnimationDelegateViews(this),
        prefs_(prefs),
        space_id_(std::move(space_id)),
        window_theme_(std::move(window_theme)) {
    SpaceManager space_manager(prefs_);
    const Space* space = space_manager.GetSpaceById(space_id_);
    if (space) {
      original_ = space->theme;
      accent_ = space->AccentColor();
    }
    theme_ = original_;

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    pad_ = AddChildView(std::make_unique<ThemePadView>(this));
    caption_ = AddChildView(std::make_unique<views::Label>());
    caption_->SetFontList(UiFont(12, gfx::Font::Weight::NORMAL));
    caption_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    AddChildView(BuildControlsRow());
    moods_heading_ = AddChildView(std::make_unique<views::Label>(u"Moods"));
    moods_heading_->SetFontList(UiFont(12, gfx::Font::Weight::SEMIBOLD));
    moods_heading_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    AddChildView(BuildMoodGrid());

    glide_.SetSlideDuration(kGlideDuration);
    glide_.SetTweenType(gfx::Tween::EASE_OUT_3);

    LoadTheme(original_);
  }

  ~ThemeStudioView() override {
    // However the bubble went away, the window must not keep a preview.
    Finish(/*keep=*/false);
  }

  // The bubble is closing.  |keep| saves the theme the window shows, else the
  // window goes back to the Space's saved theme.
  void Finish(bool keep) {
    if (finished_) {
      return;
    }
    finished_ = true;
    preview_timer_.Stop();
    StopGlide();
    if (keep && touched_ && theme_ != original_) {
      SpaceManager(prefs_).SetSpaceTheme(
          space_id_, theme_,
          theme_.empty() ? std::string()
                         : SpaceThemeColorHex(theme_.colors.front()));
    }
    if (window_theme_) {
      window_theme_->SetPreview(std::nullopt);
    }
    if (previewed_) {
      // Every spot the drag passed through left a colour provider behind;
      // drop them all and have every window build what it needs afresh.
      ReleaseAvoraSpaceThemeSuppliers();
      ui::NativeTheme::GetInstanceForNativeUi()->NotifyOnNativeThemeUpdated();
    }
  }

  // views::View:
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    ApplyThemeColors();
  }

  // ThemePadView::Delegate:
  void OnOrbsMoved(bool settled) override {
    // Grabbing an orb mid-glide keeps it where the user has it.
    StopGlide();
    theme_.mood.clear();
    TakeOrbs();
    if (settled) {
      PushPreview();
    } else if (!preview_timer_.IsRunning()) {
      preview_timer_.Start(FROM_HERE, kPreviewInterval, this,
                           &ThemeStudioView::PushPreview);
    }
  }

  // views::AnimationDelegateViews:
  void AnimationProgressed(const gfx::Animation* animation) override {
    if (glide_from_.empty() || glide_to_.empty()) {
      return;
    }
    const double t = animation->GetCurrentValue();
    std::vector<Orb> orbs;
    for (size_t i = 0; i < glide_to_.size(); ++i) {
      const Orb& from = glide_from_[std::min(i, glide_from_.size() - 1)];
      const Orb& to = glide_to_[i];
      Orb orb;
      orb.hue = LerpHue(from.hue, to.hue, t);
      orb.vibrance = from.vibrance + (to.vibrance - from.vibrance) * t;
      orb.color = color_utils::AlphaBlend(to.color, from.color,
                                          static_cast<float>(t));
      orbs.push_back(orb);
    }
    pad_->SetOrbs(std::move(orbs));
  }

  void AnimationEnded(const gfx::Animation* animation) override {
    if (!glide_to_.empty()) {
      pad_->SetOrbs(glide_to_);
    }
  }

 private:
  // Leaves the orbs wherever the glide had got them.
  void StopGlide() {
    glide_to_.clear();
    glide_.Stop();
  }

  std::unique_ptr<views::View> BuildControlsRow() {
    auto row = std::make_unique<views::View>();
    auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    dark_ = row->AddChildView(std::make_unique<ChipButton>(
        u"Dark", base::BindRepeating(&ThemeStudioView::SetMode,
                                     base::Unretained(this),
                                     SpaceTheme::Mode::kDark)));
    light_ = row->AddChildView(std::make_unique<ChipButton>(
        u"Light", base::BindRepeating(&ThemeStudioView::SetMode,
                                      base::Unretained(this),
                                      SpaceTheme::Mode::kLight)));

    auto spacer = std::make_unique<views::View>();
    layout->SetFlexForView(row->AddChildView(std::move(spacer)), 1);

    gradient_ = row->AddChildView(std::make_unique<ChipButton>(
        u"Blend two", base::BindRepeating(&ThemeStudioView::ToggleGradient,
                                          base::Unretained(this))));
    reset_ = row->AddChildView(std::make_unique<ChipButton>(
        u"Reset", base::BindRepeating(&ThemeStudioView::Reset,
                                      base::Unretained(this))));
    return row;
  }

  std::unique_ptr<views::View> BuildMoodGrid() {
    auto grid = std::make_unique<views::View>();
    grid->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), kMoodGap));
    views::View* row = nullptr;
    int column = 0;
    for (const SpaceThemeMood& mood : GetSpaceThemeMoods()) {
      if (!row || column == kMoodColumns) {
        row = grid->AddChildView(std::make_unique<views::View>());
        row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
            kMoodGap));
        column = 0;
      }
      auto button = std::make_unique<MoodButton>(mood, views::Button::PressedCallback());
      MoodButton* raw = button.get();
      button->SetCallback(base::BindRepeating(&ThemeStudioView::PickMood,
                                              base::Unretained(this), raw));
      mood_buttons_.push_back(row->AddChildView(std::move(button)));
      ++column;
    }
    return grid;
  }

  // Puts |theme| on the pad.  An empty theme shows one dormant orb on the
  // Space's accent, ready to be dragged.
  void LoadTheme(const SpaceTheme& theme) {
    mode_ = theme.mode;
    pad_->SetMode(mode_);
    std::vector<Orb> orbs;
    if (theme.empty()) {
      Orb orb = OrbForColor(accent_);
      orb.color = PadColor(orb.hue, orb.vibrance, mode_);
      orbs.push_back(orb);
    } else {
      for (SkColor color : theme.colors) {
        orbs.push_back(OrbForColor(color));
      }
    }
    pad_->SetOrbs(std::move(orbs));
    UpdateControls();
  }

  // Copies the pad's orbs into the theme.
  void TakeOrbs() {
    touched_ = true;
    theme_.mode = mode_;
    theme_.colors.clear();
    for (const Orb& orb : pad_->orbs()) {
      theme_.colors.push_back(orb.color);
    }
    UpdateControls();
  }

  void PushPreview() {
    preview_timer_.Stop();
    if (!window_theme_ || window_theme_->space_id() != space_id_) {
      // The window moved to another Space under the studio.
      if (GetWidget()) {
        GetWidget()->Close();
      }
      return;
    }
    previewed_ = true;
    window_theme_->SetPreview(theme_);
    ApplyThemeColors();
  }

  void SetMode(SpaceTheme::Mode mode) {
    if (mode == mode_) {
      return;
    }
    mode_ = mode;
    StopGlide();
    pad_->SetMode(mode_);
    theme_.mood.clear();
    TakeOrbs();
    PushPreview();
  }

  void ToggleGradient() {
    StopGlide();
    std::vector<Orb> orbs = pad_->orbs();
    if (orbs.size() == 2) {
      orbs.pop_back();
    } else if (orbs.size() == 1) {
      // The second colour starts a third of the way round the hue circle,
      // where it reads as a deliberate pair rather than a near-miss.
      Orb second = orbs.front();
      second.hue = LerpHue(second.hue, second.hue + 0.33, 1.0);
      second.vibrance = std::max(second.vibrance, 0.55);
      second.color = PadColor(second.hue, second.vibrance, mode_);
      orbs.push_back(second);
    }
    pad_->SetOrbs(std::move(orbs));
    theme_.mood.clear();
    TakeOrbs();
    PushPreview();
  }

  void Reset() {
    StopGlide();
    touched_ = true;
    theme_ = SpaceTheme();
    LoadTheme(theme_);
    PushPreview();
  }

  void PickMood(MoodButton* button) {
    const SpaceTheme theme = button->mood().ToTheme();
    glide_from_ = pad_->orbs();
    glide_to_.clear();
    for (SkColor color : theme.colors) {
      glide_to_.push_back(OrbForColor(color));
    }
    mode_ = theme.mode;
    pad_->SetMode(mode_);
    touched_ = true;
    theme_ = theme;
    UpdateControls();
    PushPreview();
    if (glide_from_.empty()) {
      pad_->SetOrbs(glide_to_);
    } else {
      glide_.Reset(0);
      glide_.Show();
    }
  }

  void UpdateControls() {
    pad_->SetDormant(theme_.empty());
    dark_->SetSelected(mode_ == SpaceTheme::Mode::kDark);
    light_->SetSelected(mode_ == SpaceTheme::Mode::kLight);
    gradient_->SetSelected(pad_->orbs().size() == 2);
    for (MoodButton* button : mood_buttons_) {
      button->SetSelected(button->mood().id == theme_.mood);
    }
    if (theme_.empty()) {
      caption_->SetText(touched_ ? u"Back to Avora's own look."
                                 : u"Drag the orb. Your Space follows along.");
    } else if (theme_.colors.size() == 2) {
      caption_->SetText(u"Two colours, blended top to bottom.");
    } else {
      caption_->SetText(u"Up for vivid, down for calm.");
    }
  }

  void ApplyThemeColors() {
    const SpaceThemePalette& palette = WindowTheme::PaletteFor(this);
    caption_->SetEnabledColor(palette.text_dim);
    caption_->SetBackgroundColor(palette.surface);
    moods_heading_->SetEnabledColor(palette.text_faint);
    moods_heading_->SetBackgroundColor(palette.surface);
    if (GetWidget()) {
      if (auto* bubble = GetWidget()->widget_delegate()->AsBubbleDialogDelegate()) {
        bubble->SetBackgroundColor(palette.surface);
      }
    }
    SchedulePaint();
  }

  const raw_ptr<PrefService> prefs_;
  const std::string space_id_;
  const base::WeakPtr<WindowTheme> window_theme_;

  // The Space's theme when the studio opened, and the one on the pad.
  SpaceTheme original_;
  SpaceTheme theme_;
  SkColor accent_ = SK_ColorWHITE;
  SpaceTheme::Mode mode_ = SpaceTheme::Mode::kDark;
  // Whether the user changed anything; an untouched studio saves nothing.
  bool touched_ = false;
  // Whether the window was ever repainted with a preview.
  bool previewed_ = false;
  bool finished_ = false;

  raw_ptr<ThemePadView> pad_ = nullptr;
  raw_ptr<views::Label> caption_ = nullptr;
  raw_ptr<views::Label> moods_heading_ = nullptr;
  raw_ptr<ChipButton> dark_ = nullptr;
  raw_ptr<ChipButton> light_ = nullptr;
  raw_ptr<ChipButton> gradient_ = nullptr;
  raw_ptr<ChipButton> reset_ = nullptr;
  std::vector<raw_ptr<MoodButton>> mood_buttons_;

  base::OneShotTimer preview_timer_;
  gfx::SlideAnimation glide_{this};
  std::vector<Orb> glide_from_;
  std::vector<Orb> glide_to_;
};

BEGIN_METADATA(ThemeStudioView)
END_METADATA

}  // namespace

void ShowThemeStudioBubble(views::View* anchor_view,
                           PrefService* prefs,
                           const std::string& space_id) {
  if (!anchor_view || !prefs) {
    return;
  }
  WindowTheme* window_theme = WindowTheme::FromWidget(anchor_view->GetWidget());
  if (!window_theme) {
    return;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::BOTTOM_LEFT);
  delegate->SetTitle(u"Theme Studio");
  delegate->SetShowTitle(true);
  delegate->SetShowCloseButton(false);
  delegate->SetBackgroundColor(WindowTheme::PaletteFor(anchor_view).surface);
  delegate->set_margins(gfx::Insets::VH(12, 16));
  delegate->set_fixed_width(kContentWidth + 32);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                       static_cast<int>(ui::mojom::DialogButton::kCancel));
  delegate->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Done");
  delegate->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");

  auto* studio = delegate->SetContentsView(std::make_unique<ThemeStudioView>(
      prefs, space_id, window_theme->GetWeakPtr()));

  // Done and clicking away both keep the theme: the window already shows
  // it, and losing a theme to a stray click would sting.  Cancel and Esc
  // put the window back.
  auto keep = [](ThemeStudioView* studio) { studio->Finish(/*keep=*/true); };
  delegate->SetAcceptCallback(base::BindOnce(keep, base::Unretained(studio)));
  delegate->SetCloseCallback(base::BindOnce(keep, base::Unretained(studio)));
  delegate->SetCancelCallback(base::BindOnce(
      [](ThemeStudioView* studio) { studio->Finish(/*keep=*/false); },
      base::Unretained(studio)));

  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  avora::WindowTheme::MatchColorMode(widget);
  widget->Show();
}

}  // namespace avora
