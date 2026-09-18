// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_downloads_button.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/avora/avora_downloads_panel_view.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"

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

// Lucide "download".
constexpr char kDownloadIconPath[] =
    "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4 M7 10l5 5 5-5 M12 15V3";

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
               float sweep_degrees) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(kRingStrokeWidth);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  flags.setColor(color);

  const SkRect oval =
      SkRect::MakeLTRB(bounds.x(), bounds.y(), bounds.right(), bounds.bottom());
  SkPathBuilder builder;
  // Starts at twelve o'clock, so the arc fills the way a clock hand sweeps.
  builder.arcTo(oval, /*startAngleDeg=*/-90.0f, sweep_degrees,
                /*forceMoveTo=*/true);
  const SkPath path = builder.detach();
  canvas->DrawPath(path, flags);
}

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

  if (profile_) {
    manager_ = profile_->GetDownloadManager();
    if (manager_) {
      manager_->AddObserver(this);
    }
  }
  RefreshProgress();
}

DownloadsButton::~DownloadsButton() {
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
      // would mean anything.  The ring in the progress colour still says a
      // download is running.
      StrokeArc(canvas, ring, SkColorSetA(kRingProgress, 0x66), 360.0f);
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
}

void DownloadsButton::ManagerGoingDown(content::DownloadManager* manager) {
  StopObservingItems();
  if (manager_) {
    manager_->RemoveObserver(this);
    manager_ = nullptr;
  }
  active_count_ = 0;
  progress_ = -1.0;
  SchedulePaint();
}

void DownloadsButton::OnDownloadUpdated(download::DownloadItem* item) {
  RefreshProgress();
}

void DownloadsButton::OnDownloadRemoved(download::DownloadItem* item) {
  RefreshProgress();
}

void DownloadsButton::OnDownloadDestroyed(download::DownloadItem* item) {
  // Drop the reference without touching the item, then recount on a fresh
  // stack: the manager is still tearing this download down.
  std::erase(observed_items_, item);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DownloadsButton::RefreshProgress,
                                weak_factory_.GetWeakPtr()));
}

void DownloadsButton::OnPressed() {
  ShowDownloadsPanel(this, profile_, browser_);
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

  if (in_progress != observed_items_) {
    StopObservingItems();
    for (download::DownloadItem* item : in_progress) {
      item->AddObserver(this);
    }
    observed_items_ = std::move(in_progress);
  }

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
