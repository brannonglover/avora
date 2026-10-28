// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_downloads_button.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/avora/avora_downloads_panel_view.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace avora {

namespace {

// Matched to the Space bar's own metrics: the button occupies the slot that
// balances the "+" at the other end of the strip.
constexpr int kButtonSize = 28;
constexpr int kGlyphSize = 16;
constexpr int kActiveGlyphSize = 13;
constexpr int kCornerRadius = 8;

constexpr SkColor kHoverBg = SkColorSetARGB(0x14, 0xFF, 0xFF, 0xFF);
constexpr SkColor kGlyphColor = SkColorSetARGB(0xB0, 0xED, 0xF2, 0xF5);
constexpr SkColor kRingTrack = SkColorSetARGB(0x33, 0xED, 0xF2, 0xF5);
constexpr SkColor kRingProgress = SkColorSetRGB(0x6E, 0xA8, 0xFE);

constexpr float kRingStrokeWidth = 2.0f;
constexpr int kRingInset = 2;

// The indeterminate arc: a quarter of the ring, going round about once a
// second.
constexpr float kSpinnerSweep = 90.0f;
constexpr float kSpinnerStepDegrees = 6.0f;
constexpr base::TimeDelta kSpinnerInterval = base::Hertz(60);

// The flight: a chip that starts about the size of a file icon under the
// pointer and shrinks to the glyph it is landing on.
constexpr base::TimeDelta kFlightDuration = base::Milliseconds(650);
constexpr int kChipStartSize = 34;
constexpr int kChipEndSize = 14;
constexpr float kFlightLift = 90.0f;
// The last stretch of the flight, over which the chip fades into the button.
constexpr double kFlightFadeFrom = 0.8;

constexpr base::TimeDelta kLandPulseDuration = base::Milliseconds(420);
constexpr SkColor kLandPulseColor = SkColorSetARGB(0x55, 0x6E, 0xA8, 0xFE);

// Lucide "download".
constexpr char kDownloadIconPath[] =
    "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4 M7 10l5 5 5-5 M12 15V3";
// Lucide "file".
constexpr char kFileIconPath[] =
    "M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7Z M14 2v4a2 2 0 "
    "0 0 2 2h4";

void FillRoundRect(gfx::Canvas* canvas,
                   const gfx::Rect& bounds,
                   SkColor color,
                   int radius) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);
  canvas->DrawRoundRect(bounds, radius, flags);
}

void StrokeArc(gfx::Canvas* canvas,
               const gfx::Rect& bounds,
               SkColor color,
               float sweep_degrees,
               float start_degrees = -90.0f) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(kRingStrokeWidth);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  flags.setColor(color);

  const SkRect oval =
      SkRect::MakeLTRB(bounds.x(), bounds.y(), bounds.right(), bounds.bottom());
  SkPathBuilder builder;
  // Starts at twelve o'clock by default, so the arc fills the way a clock
  // hand sweeps.
  builder.arcTo(oval, start_degrees, sweep_degrees, /*forceMoveTo=*/true);
  const SkPath path = builder.detach();
  canvas->DrawPath(path, flags);
}

// The chip a new download throws from the pointer into the downloads button.
// It lives in the window's root view, stacked above everything including the
// page, for the length of one flight and then takes itself out.  It never
// takes events or focus, so the page under it keeps working while it is in
// the air.
class DownloadFlightView : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(DownloadFlightView, views::View)

 public:
  // |start| and |end| are centres, in the coordinates of the view this is
  // added to.
  DownloadFlightView(const gfx::PointF& start,
                     const gfx::PointF& end,
                     base::OnceClosure on_landed)
      : start_(start),
        end_(end),
        // Above both ends, so the chip is tossed up and drops into the button
        // rather than sliding along a straight line.
        control_((start.x() + end.x()) / 2.0f,
                 std::max(0.0f, std::min(start.y(), end.y()) - kFlightLift)),
        on_landed_(std::move(on_landed)) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
    SetFocusBehavior(FocusBehavior::NEVER);
    GetViewAccessibility().SetIsIgnored(true);
    // The root view fills its children to its own size; the chip places
    // itself.
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    animation_.SetDuration(kFlightDuration);
    MoveTo(0.0);
  }
  DownloadFlightView(const DownloadFlightView&) = delete;
  DownloadFlightView& operator=(const DownloadFlightView&) = delete;
  ~DownloadFlightView() override = default;

  void Start() { animation_.Start(); }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Rect bounds = GetLocalBounds();
    FillRoundRect(canvas, bounds, kRingProgress, bounds.width() / 4);
    gfx::Rect glyph(bounds);
    const int glyph_size = bounds.width() * 3 / 5;
    glyph.ClampToCenteredSize(gfx::Size(glyph_size, glyph_size));
    PaintLucidePathData(canvas, glyph, kFileIconPath, SK_ColorWHITE);
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    MoveTo(animation_.GetCurrentValue());
  }

  void AnimationEnded(const gfx::Animation* animation) override {
    SetVisible(false);
    if (on_landed_) {
      std::move(on_landed_).Run();
    }
    // Taking the view out deletes it, and the animation calling this with it,
    // so it waits for a fresh stack.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DownloadFlightView::RemoveSelf,
                                  weak_factory_.GetWeakPtr()));
  }

 private:
  // Places the chip |t| of the way along its flight.
  void MoveTo(double t) {
    const double eased = gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT, t);
    const float e = static_cast<float>(eased);
    const float u = 1.0f - e;
    // A quadratic Bézier from start, pulled towards the control point, to end.
    const float x =
        u * u * start_.x() + 2 * u * e * control_.x() + e * e * end_.x();
    const float y =
        u * u * start_.y() + 2 * u * e * control_.y() + e * e * end_.y();
    const int size =
        gfx::Tween::LinearIntValueBetween(eased, kChipStartSize, kChipEndSize);
    SetBoundsRect(gfx::Rect(static_cast<int>(x) - size / 2,
                            static_cast<int>(y) - size / 2, size, size));
    layer()->SetOpacity(
        t < kFlightFadeFrom
            ? 1.0f
            : static_cast<float>((1.0 - t) / (1.0 - kFlightFadeFrom)));
  }

  void RemoveSelf() {
    if (parent()) {
      parent()->RemoveChildViewT(this);
    }
  }

  const gfx::PointF start_;
  const gfx::PointF end_;
  const gfx::PointF control_;
  base::OnceClosure on_landed_;

  gfx::LinearAnimation animation_{this};

  base::WeakPtrFactory<DownloadFlightView> weak_factory_{this};
};

BEGIN_METADATA(DownloadFlightView)
END_METADATA

}  // namespace

DownloadsButton::DownloadsButton(Profile* profile,
                                 BrowserWindowInterface* browser)
    : views::Button(base::BindRepeating(&DownloadsButton::OnPressed,
                                        base::Unretained(this))),
      profile_(profile),
      browser_(browser) {
  SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  SetTooltipText(u"Downloads");
  GetViewAccessibility().SetName(u"Downloads");
  GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kDialog);
  SetFocusBehavior(FocusBehavior::ALWAYS);
  land_pulse_.SetDuration(kLandPulseDuration);

  if (profile_) {
    manager_ = profile_->GetDownloadManager();
    if (manager_) {
      manager_->AddObserver(this);
    }
  }
  RefreshProgress();
}

DownloadsButton::~DownloadsButton() {
  spinner_timer_.Stop();
  StopObservingItems();
  if (manager_) {
    manager_->RemoveObserver(this);
    manager_ = nullptr;
  }
}

void DownloadsButton::PaintButtonContents(gfx::Canvas* canvas) {
  if (GetState() == views::Button::STATE_HOVERED ||
      GetState() == views::Button::STATE_PRESSED) {
    FillRoundRect(canvas, GetLocalBounds(), kHoverBg, kCornerRadius);
  }

  // A chip has just landed: a wash of the progress colour that fades out.
  if (land_pulse_.is_animating()) {
    const double t = land_pulse_.GetCurrentValue();
    FillRoundRect(
        canvas, GetLocalBounds(),
        SkColorSetA(kLandPulseColor,
                    static_cast<U8CPU>(SkColorGetA(kLandPulseColor) * (1 - t))),
        kCornerRadius);
  }

  const bool active = active_count_ > 0;

  if (active) {
    gfx::Rect ring(GetLocalBounds());
    ring.Inset(kRingInset);
    StrokeArc(canvas, ring, kRingTrack, 360.0f);
    if (progress_ >= 0.0) {
      StrokeArc(canvas, ring, kRingProgress,
                static_cast<float>(progress_ * 360.0));
    } else {
      // Nothing known about the total size, so there is no arc to draw that
      // would mean anything.  A spinning arc still says a download is running.
      StrokeArc(canvas, ring, kRingProgress, kSpinnerSweep,
                spinner_angle_ - 90.0f);
    }
  }

  gfx::Rect glyph(GetLocalBounds());
  glyph.ClampToCenteredSize(
      gfx::Size(active ? kActiveGlyphSize : kGlyphSize,
                active ? kActiveGlyphSize : kGlyphSize));
  PaintLucidePathData(canvas, glyph, kDownloadIconPath,
                      active ? kRingProgress : kGlyphColor);
}

void DownloadsButton::OnDownloadCreated(content::DownloadManager* manager,
                                        download::DownloadItem* item) {
  RefreshProgress();

  // Downloads restored from history arrive here too, but never in progress.
  if (!item || item->IsTransient() ||
      item->GetState() != download::DownloadItem::IN_PROGRESS ||
      !StartedInThisWindow(item)) {
    return;
  }

  views::Widget* widget = GetWidget();
  if (!widget) {
    return;
  }
  // Where the pointer is now is where the user clicked to start this.  It is
  // read here rather than at launch, which may be after a Save As sheet.
  views::View* root = widget->GetRootView();
  gfx::Point start = display::Screen::Get()->GetCursorScreenPoint();
  views::View::ConvertPointFromScreen(root, &start);
  if (!root->GetLocalBounds().Contains(start)) {
    // Started from a menu or a shortcut with the pointer elsewhere: throw
    // from the middle of the window instead.
    start = root->GetLocalBounds().CenterPoint();
  }
  pending_flights_[item] = start;
  MaybeLaunchFlight(item);
}

void DownloadsButton::ManagerGoingDown(content::DownloadManager* manager) {
  StopObservingItems();
  if (manager_) {
    manager_->RemoveObserver(this);
    manager_ = nullptr;
  }
  pending_flights_.clear();
  active_count_ = 0;
  progress_ = -1.0;
  UpdateSpinner();
  SchedulePaint();
}

void DownloadsButton::OnDownloadUpdated(download::DownloadItem* item) {
  MaybeLaunchFlight(item);
  RefreshProgress();
}

void DownloadsButton::OnDownloadRemoved(download::DownloadItem* item) {
  pending_flights_.erase(item);
  RefreshProgress();
}

void DownloadsButton::OnDownloadDestroyed(download::DownloadItem* item) {
  // Drop the reference without touching the item, then recount on a fresh
  // stack: the manager is still tearing this download down.
  std::erase(observed_items_, item);
  pending_flights_.erase(item);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DownloadsButton::RefreshProgress,
                                weak_factory_.GetWeakPtr()));
}

void DownloadsButton::AnimationProgressed(const gfx::Animation* animation) {
  if (animation != &land_pulse_) {
    views::Button::AnimationProgressed(animation);
    return;
  }
  SchedulePaint();
}

void DownloadsButton::AnimationEnded(const gfx::Animation* animation) {
  if (animation != &land_pulse_) {
    views::Button::AnimationEnded(animation);
    return;
  }
  SchedulePaint();
}

void DownloadsButton::OnPressed() {
  ShowDownloadsPanel(this, profile_, browser_);
}

bool DownloadsButton::StartedInThisWindow(download::DownloadItem* item) const {
  const views::Widget* widget = GetWidget();
  const views::Widget* mine = widget ? widget->GetTopLevelWidget() : nullptr;
  if (!mine) {
    return false;
  }
  if (content::WebContents* contents =
          content::DownloadItemUtils::GetOriginalWebContents(item)) {
    if (const views::Widget* theirs =
            views::Widget::GetTopLevelWidgetForNativeView(
                contents->GetNativeView())) {
      return theirs == mine;
    }
  }
  // No tab to go by (it closed, or the download came from a tab that is not
  // showing): the window the user is working in is the one that asked.
  return mine->IsActive();
}

void DownloadsButton::MaybeLaunchFlight(download::DownloadItem* item) {
  auto it = pending_flights_.find(item);
  if (it == pending_flights_.end()) {
    return;
  }
  if (item->GetState() != download::DownloadItem::IN_PROGRESS) {
    // Cancelled or failed before it ever had a name.
    pending_flights_.erase(it);
    return;
  }
  if (item->GetTargetFilePath().empty()) {
    return;
  }
  const gfx::Point start = it->second;
  pending_flights_.erase(it);
  LaunchFlight(start);
}

void DownloadsButton::LaunchFlight(const gfx::Point& start) {
  views::Widget* widget = GetWidget();
  // A sidebar that is hidden has no button on screen to land on.
  if (!widget || !IsDrawn()) {
    return;
  }
  views::View* root = widget->GetRootView();
  gfx::Point end = GetLocalBounds().CenterPoint();
  views::View::ConvertPointToTarget(this, root, &end);

  auto* flight = root->AddChildView(std::make_unique<DownloadFlightView>(
      gfx::PointF(start), gfx::PointF(end),
      base::BindOnce(&DownloadsButton::OnFlightLanded,
                     weak_factory_.GetWeakPtr())));
  flight->Start();
}

void DownloadsButton::OnFlightLanded() {
  land_pulse_.Start();
}

void DownloadsButton::UpdateSpinner() {
  const bool spin = active_count_ > 0 && progress_ < 0.0;
  if (!spin) {
    spinner_timer_.Stop();
    return;
  }
  if (spinner_timer_.IsRunning()) {
    return;
  }
  spinner_timer_.Start(FROM_HERE, kSpinnerInterval,
                       base::BindRepeating(
                           [](DownloadsButton* button) {
                             button->spinner_angle_ = std::fmod(
                                 button->spinner_angle_ + kSpinnerStepDegrees,
                                 360.0f);
                             button->SchedulePaint();
                           },
                           base::Unretained(this)));
}

void DownloadsButton::RefreshProgress() {
  int active = 0;
  int64_t received = 0;
  int64_t total = 0;
  bool any_unknown_size = false;
  std::vector<download::DownloadItem*> in_progress;

  if (manager_) {
    content::DownloadManager::DownloadVector all;
    manager_->GetAllDownloads(&all);
    for (download::DownloadItem* item : all) {
      if (!item || item->IsTransient() ||
          item->GetState() != download::DownloadItem::IN_PROGRESS) {
        continue;
      }
      ++active;
      in_progress.push_back(item);
      if (item->GetTotalBytes() > 0) {
        received += item->GetReceivedBytes();
        total += item->GetTotalBytes();
      } else {
        any_unknown_size = true;
      }
    }
  }

  // Progress across everything in flight at once, so two downloads read as one
  // ring rather than fighting over it.  Any download of unknown size makes the
  // whole ring indeterminate: a bar that ignored it would lie.
  const double progress =
      (total > 0 && !any_unknown_size)
          ? std::clamp(static_cast<double>(received) / static_cast<double>(total),
                       0.0, 1.0)
          : -1.0;

  // An active download updates many times a second; only a change the user
  // could see is worth a repaint.
  const bool changed =
      active != active_count_ ||
      static_cast<int>(progress * 100) != static_cast<int>(progress_ * 100);

  active_count_ = active;
  progress_ = progress;

  // A flight still waiting on a filename for a download that has stopped
  // will never launch, and the item may be gone before it could say so.
  std::erase_if(pending_flights_, [&in_progress](const auto& entry) {
    return std::find(in_progress.begin(), in_progress.end(), entry.first) ==
           in_progress.end();
  });

  if (in_progress != observed_items_) {
    StopObservingItems();
    for (download::DownloadItem* item : in_progress) {
      item->AddObserver(this);
    }
    observed_items_ = std::move(in_progress);
  }

  UpdateSpinner();

  if (changed) {
    SetTooltipText(
        active_count_ > 0
            ? base::StrCat({u"Downloads · ",
                            base::NumberToString16(active_count_),
                            u" in progress"})
            : u"Downloads");
    SchedulePaint();
  }
}

void DownloadsButton::StopObservingItems() {
  for (download::DownloadItem* item : observed_items_) {
    item->RemoveObserver(this);
  }
  observed_items_.clear();
}

BEGIN_METADATA(DownloadsButton)
END_METADATA

}  // namespace avora
