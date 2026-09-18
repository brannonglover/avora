// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_downloads_panel_view.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/base_paths.h"
#include "base/byte_size.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

namespace {

// The panel borrows the sidebar's bubble palette so it reads as part of the
// same surface as the manage bubble and the Space editor.
constexpr SkColor kPanelBg = SkColorSetRGB(0x22, 0x28, 0x2E);
constexpr SkColor kPrimaryText = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kSecondaryText = SkColorSetARGB(0x88, 0xED, 0xF2, 0xF5);
constexpr SkColor kIconColor = SkColorSetARGB(0xBB, 0xED, 0xF2, 0xF5);
constexpr SkColor kRowHoverBg = SkColorSetARGB(0x18, 0xFF, 0xFF, 0xFF);
constexpr SkColor kSeparatorColor = SkColorSetARGB(0x22, 0xFF, 0xFF, 0xFF);
constexpr SkColor kFailedText = SkColorSetRGB(0xE8, 0x7A, 0x7A);

constexpr int kRowHeight = 48;
constexpr int kRowCornerRadius = 8;
constexpr int kRowHPad = 10;
constexpr int kRowGap = 8;
constexpr int kStatusGlyphSize = 18;
constexpr int kActionButtonSize = 24;
constexpr int kActionGlyphSize = 15;
constexpr int kHeaderHeight = 34;
constexpr int kSeparatorHeight = 1;

constexpr int kNameFontSize = 13;
constexpr int kStatusFontSize = 11;

// Lucide paths for the panel's own chrome.  These are browser icons rather
// than Space icons, so they are referenced directly instead of through the
// Space catalog.
constexpr char kDownloadIconPath[] =
    "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4 M7 10l5 5 5-5 M12 15V3";
constexpr char kFileIconPath[] =
    "M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7Z M14 2v4a2 2 0 "
    "0 0 2 2h4";
constexpr char kAlertIconPath[] =
    "M12 2A10 10 0 1 0 22 12A10 10 0 1 0 12 2Z M12 8v4 M12 16h.01";
constexpr char kFolderOpenIconPath[] =
    "M6 14l1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.55 6a2 2 0 0 1"
    "-1.94 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 "
    "2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2";
constexpr char kCloseIconPath[] = "M18 6 6 18 M6 6l12 12";

gfx::FontList PanelFont(int size, gfx::Font::Weight weight) {
  return gfx::FontList({std::string("system-ui")}, gfx::Font::NORMAL, size,
                       weight);
}

std::u16string FormatSize(int64_t bytes) {
  return ui::FormatBytes(base::ByteSize(
      base::checked_cast<uint64_t>(std::max<int64_t>(bytes, 0))));
}

std::u16string FormatRate(int64_t bytes_per_second) {
  return ui::FormatSpeed(base::ByteSize(
      base::checked_cast<uint64_t>(std::max<int64_t>(bytes_per_second, 0))));
}

// The download directory as the user thinks of it: the home prefix collapsed
// to "~", the way the same path reads in Finder and the shell.
std::u16string PrettyDirectory(const base::FilePath& dir) {
  if (dir.empty()) {
    return std::u16string();
  }
  base::FilePath home;
  base::FilePath relative;
  if (base::PathService::Get(base::DIR_HOME, &home) &&
      home.AppendRelativePath(dir, &relative)) {
    return base::StrCat({u"~/", relative.LossyDisplayName()});
  }
  return dir.LossyDisplayName();
}

std::u16string SourceHost(download::DownloadItem* item) {
  const std::string host(item->GetURL().host());
  return host.empty() ? std::u16string() : base::UTF8ToUTF16(host);
}

bool IsFailed(download::DownloadItem* item) {
  return item->GetState() == download::DownloadItem::INTERRUPTED &&
         !item->CanResume();
}

// One line saying where a download has got to.  While it runs that is bytes
// and speed; once it finishes it is size and origin, which is what tells two
// same-named files apart.
std::u16string StatusLine(download::DownloadItem* item) {
  const std::u16string host = SourceHost(item);
  const std::u16string host_suffix =
      host.empty() ? std::u16string() : base::StrCat({u" · ", host});

  switch (item->GetState()) {
    case download::DownloadItem::IN_PROGRESS: {
      const std::u16string received = FormatSize(item->GetReceivedBytes());
      if (item->IsPaused()) {
        return base::StrCat({u"Paused · ", received});
      }
      if (item->GetTotalBytes() > 0) {
        return base::StrCat({received, u" of ",
                             FormatSize(item->GetTotalBytes()), u" · ",
                             FormatRate(item->CurrentSpeed())});
      }
      return base::StrCat({received, u" downloaded"});
    }
    case download::DownloadItem::COMPLETE:
      if (item->GetFileExternallyRemoved()) {
        return u"No longer on disk";
      }
      return base::StrCat({FormatSize(item->GetReceivedBytes()), host_suffix});
    case download::DownloadItem::CANCELLED:
      return u"Canceled";
    case download::DownloadItem::INTERRUPTED:
      return item->CanResume() ? u"Paused · click to resume"
                               : base::StrCat({u"Failed", host_suffix});
    case download::DownloadItem::MAX_DOWNLOAD_STATE:
      break;
  }
  return std::u16string();
}

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

// A bare Lucide glyph that lights up on hover: the panel's row actions and its
// header button.
class PanelIconButton : public views::Button {
  METADATA_HEADER(PanelIconButton, views::Button)

 public:
  PanelIconButton(PressedCallback callback,
                  std::string_view path_data,
                  const std::u16string& tooltip)
      : views::Button(std::move(callback)), path_data_(path_data) {
    SetPreferredSize(gfx::Size(kActionButtonSize, kActionButtonSize));
    SetTooltipText(tooltip);
    GetViewAccessibility().SetName(tooltip);
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      FillRoundRect(canvas, GetLocalBounds(), kRowHoverBg, 6);
    }
    gfx::Rect glyph(GetLocalBounds());
    glyph.ClampToCenteredSize(gfx::Size(kActionGlyphSize, kActionGlyphSize));
    PaintLucidePathData(canvas, glyph, path_data_, kIconColor);
  }

 private:
  std::string path_data_;
};

BEGIN_METADATA(PanelIconButton)
END_METADATA

// The glyph at the head of a row, saying what state the download is in without
// the user having to read the status line.
class DownloadStatusGlyph : public views::View {
  METADATA_HEADER(DownloadStatusGlyph, views::View)

 public:
  DownloadStatusGlyph() {
    SetPreferredSize(gfx::Size(kStatusGlyphSize, kStatusGlyphSize));
  }

  void SetDownloadState(download::DownloadItem::DownloadState state,
                        bool failed) {
    state_ = state;
    failed_ = failed;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    std::string_view path = kFileIconPath;
    SkColor color = kIconColor;
    if (failed_ || state_ == download::DownloadItem::CANCELLED) {
      path = kAlertIconPath;
      color = failed_ ? kFailedText : kSecondaryText;
    } else if (state_ == download::DownloadItem::IN_PROGRESS) {
      path = kDownloadIconPath;
    }
    PaintLucidePathData(canvas, GetLocalBounds(), path, color);
  }

 private:
  download::DownloadItem::DownloadState state_ =
      download::DownloadItem::IN_PROGRESS;
  bool failed_ = false;
};

BEGIN_METADATA(DownloadStatusGlyph)
END_METADATA

std::unique_ptr<views::View> MakeSeparator() {
  auto separator = std::make_unique<views::View>();
  separator->SetPreferredSize(
      gfx::Size(DownloadsPanelView::kPanelWidth, kSeparatorHeight));
  separator->SetBackground(views::CreateSolidBackground(kSeparatorColor));
  return separator;
}

}  // namespace

// One download in the panel.  The row keeps no copy of the download's state:
// every refresh re-reads the DownloadItem, so a progress tick is a call to
// Refresh() rather than a rebuild of the list.
class DownloadRowView : public views::Button {
  METADATA_HEADER(DownloadRowView, views::Button)

 public:
  explicit DownloadRowView(download::DownloadItem* item)
      : views::Button(base::BindRepeating(&DownloadRowView::OnPressed,
                                          base::Unretained(this))),
        item_(item) {
    SetPreferredSize(gfx::Size(DownloadsPanelView::kPanelWidth, kRowHeight));
    SetFocusBehavior(FocusBehavior::ALWAYS);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kRowHPad), kRowGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    glyph_ = AddChildView(std::make_unique<DownloadStatusGlyph>());

    auto text_column = std::make_unique<views::View>();
    auto* text_layout =
        text_column->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStart);

    auto name = std::make_unique<views::Label>(
        std::u16string(), views::Label::CustomFont{PanelFont(
                              kNameFontSize, gfx::Font::Weight::MEDIUM)});
    name->SetEnabledColor(kPrimaryText);
    name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name->SetElideBehavior(gfx::ELIDE_MIDDLE);
    name_label_ = text_column->AddChildView(std::move(name));

    auto status = std::make_unique<views::Label>(
        std::u16string(), views::Label::CustomFont{PanelFont(
                              kStatusFontSize, gfx::Font::Weight::NORMAL)});
    status->SetEnabledColor(kSecondaryText);
    status->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    status->SetElideBehavior(gfx::ELIDE_TAIL);
    status_label_ = text_column->AddChildView(std::move(status));

    views::View* text_column_raw = AddChildView(std::move(text_column));
    layout->SetFlexForView(text_column_raw, 1);

    reveal_button_ = AddChildView(std::make_unique<PanelIconButton>(
        base::BindRepeating(&DownloadRowView::OnReveal,
                            base::Unretained(this)),
        kFolderOpenIconPath, u"Show in Finder"));

    dismiss_button_ = AddChildView(std::make_unique<PanelIconButton>(
        base::BindRepeating(&DownloadRowView::OnDismiss,
                            base::Unretained(this)),
        kCloseIconPath, u"Remove from list"));

    Refresh();
  }

  DownloadRowView(const DownloadRowView&) = delete;
  DownloadRowView& operator=(const DownloadRowView&) = delete;
  ~DownloadRowView() override = default;

  // Re-reads the item.  Cheap enough to call on every progress tick.
  void Refresh() {
    if (!item_) {
      return;
    }
    const std::u16string name =
        item_->GetFileNameToReportUser().LossyDisplayName();
    const std::u16string status = StatusLine(item_);

    name_label_->SetText(name);
    status_label_->SetText(status);
    status_label_->SetEnabledColor(IsFailed(item_) ? kFailedText
                                                   : kSecondaryText);
    glyph_->SetDownloadState(item_->GetState(), IsFailed(item_));

    reveal_button_->SetVisible(
        item_->GetState() == download::DownloadItem::COMPLETE &&
        !item_->GetFileExternallyRemoved());
    dismiss_button_->SetTooltipText(
        item_->GetState() == download::DownloadItem::IN_PROGRESS
            ? u"Cancel download"
            : u"Remove from list");

    SetTooltipText(base::StrCat({name, u"\n", status}));
    GetViewAccessibility().SetName(base::StrCat({name, u", ", status}));
    InvalidateLayout();
  }

  // The download this row shows has been destroyed.  The row outlives it by
  // one task — the panel rebuilds on a fresh stack — so it drops the pointer
  // and takes itself out of view rather than painting stale data.
  void OnItemGone() {
    item_ = nullptr;
    SetVisible(false);
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      FillRoundRect(canvas, GetLocalBounds(), kRowHoverBg, kRowCornerRadius);
    }
  }

 private:
  // Clicking a row does the obvious thing for the state it is in: opens a
  // finished file, picks an interrupted one back up, and leaves a download
  // that is already running alone.
  void OnPressed() {
    if (!item_) {
      return;
    }
    switch (item_->GetState()) {
      case download::DownloadItem::COMPLETE:
        if (!item_->GetFileExternallyRemoved()) {
          item_->OpenDownload();
        }
        break;
      case download::DownloadItem::INTERRUPTED:
        if (item_->CanResume()) {
          item_->Resume(/*user_resume=*/true);
        }
        break;
      case download::DownloadItem::IN_PROGRESS:
      case download::DownloadItem::CANCELLED:
      case download::DownloadItem::MAX_DOWNLOAD_STATE:
        break;
    }
  }

  void OnReveal() {
    if (item_ && item_->CanShowInFolder()) {
      item_->ShowDownloadInShell();
    }
  }

  // Cancels a download that is still running; otherwise takes the finished one
  // out of the list.  Neither deletes the file.
  void OnDismiss() {
    if (!item_) {
      return;
    }
    if (item_->GetState() == download::DownloadItem::IN_PROGRESS) {
      item_->Cancel(/*user_cancel=*/true);
      Refresh();
      return;
    }
    // Remove() destroys the item, which calls back into the panel and can
    // retire this row.  Nothing may touch `this` afterwards.
    item_->Remove();
  }

  raw_ptr<download::DownloadItem> item_;

  raw_ptr<DownloadStatusGlyph> glyph_ = nullptr;
  raw_ptr<views::Label> name_label_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::Button> reveal_button_ = nullptr;
  raw_ptr<views::Button> dismiss_button_ = nullptr;
};

BEGIN_METADATA(DownloadRowView)
END_METADATA

// --- DownloadsPanelView -----------------------------------------------------

DownloadsPanelView::DownloadsPanelView(Profile* profile,
                                       BrowserWindowInterface* browser)
    : profile_(profile), browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(6, 0), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // ── Header: the title, and the one action that applies to the whole list ──
  auto header = std::make_unique<views::View>();
  header->SetPreferredSize(gfx::Size(kPanelWidth, kHeaderHeight));
  auto* header_layout =
      header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kRowHPad), kRowGap));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto title = std::make_unique<views::Label>(
      u"Downloads", views::Label::CustomFont{PanelFont(
                        kNameFontSize, gfx::Font::Weight::MEDIUM)});
  title->SetEnabledColor(kPrimaryText);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  views::View* title_raw = header->AddChildView(std::move(title));
  header_layout->SetFlexForView(title_raw, 1);

  clear_button_ = header->AddChildView(std::make_unique<PanelIconButton>(
      base::BindRepeating(&DownloadsPanelView::ClearFinished,
                          weak_factory_.GetWeakPtr()),
      kCloseIconPath, u"Clear finished downloads"));

  AddChildView(std::move(header));

  // ── The list itself ──
  auto scroll = std::make_unique<views::ScrollView>();
  scroll->SetBackgroundColor(std::nullopt);
  scroll->ClipHeightTo(0, kListMaxHeight);
  scroll->SetDrawOverflowIndicator(false);

  auto list = std::make_unique<views::View>();
  auto* list_layout = list->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(0, 4), 0));
  list_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  list_ = scroll->SetContents(std::move(list));
  AddChildView(std::move(scroll));

  auto empty = std::make_unique<views::Label>(
      u"Nothing downloaded yet",
      views::Label::CustomFont{PanelFont(kStatusFontSize,
                                         gfx::Font::Weight::NORMAL)});
  empty->SetEnabledColor(kSecondaryText);
  empty->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  empty->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(12, kRowHPad)));
  empty_label_ = AddChildView(std::move(empty));

  // ── Footer: where the files land.  The panel answers "where did it go?"
  //    even when the list is empty. ──
  AddChildView(MakeSeparator());

  const base::FilePath dir =
      profile_ ? profile_->GetPrefs()->GetFilePath(
                     prefs::kDownloadDefaultDirectory)
               : base::FilePath();
  auto footer = std::make_unique<views::Label>(
      base::StrCat({u"Saving to ", PrettyDirectory(dir)}),
      views::Label::CustomFont{PanelFont(kStatusFontSize,
                                         gfx::Font::Weight::NORMAL)});
  footer->SetEnabledColor(kSecondaryText);
  footer->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  footer->SetElideBehavior(gfx::ELIDE_MIDDLE);
  footer->SetTooltipText(dir.LossyDisplayName());
  footer->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(6, kRowHPad, 2,
                                                               kRowHPad)));
  footer_label_ = AddChildView(std::move(footer));

  // The list above is the recent end of the history; chrome://downloads holds
  // all of it.  Without a window to navigate, the row is simply absent.
  if (browser_) {
    auto all = std::make_unique<views::LabelButton>(
        base::BindRepeating(&DownloadsPanelView::ShowAllDownloads,
                            weak_factory_.GetWeakPtr()),
        u"Show all downloads");
    all->SetTextColor(views::Button::STATE_NORMAL, kSecondaryText);
    all->SetTextColor(views::Button::STATE_HOVERED, kPrimaryText);
    all->SetTextColor(views::Button::STATE_PRESSED, kPrimaryText);
    all->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    all->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kRowHPad)));
    all->SetPreferredSize(gfx::Size(kPanelWidth, kHeaderHeight));
    AddChildView(std::move(all));
  }

  if (profile_) {
    manager_ = profile_->GetDownloadManager();
    if (manager_) {
      manager_->AddObserver(this);
    }
  }

  RebuildList();
}

DownloadsPanelView::~DownloadsPanelView() {
  StopObservingItems();
  if (manager_) {
    manager_->RemoveObserver(this);
    manager_ = nullptr;
  }
}

void DownloadsPanelView::OnDownloadCreated(content::DownloadManager* manager,
                                           download::DownloadItem* item) {
  ScheduleRebuild();
}

void DownloadsPanelView::ManagerGoingDown(content::DownloadManager* manager) {
  StopObservingItems();
  if (manager_) {
    manager_->RemoveObserver(this);
    manager_ = nullptr;
  }
  ScheduleRebuild();
}

void DownloadsPanelView::OnDownloadUpdated(download::DownloadItem* item) {
  // A progress tick changes one row.  Rebuilding the list here would throw
  // away hover and focus several times a second.
  auto it = rows_.find(item);
  if (it != rows_.end() && it->second) {
    it->second->Refresh();
  }
}

void DownloadsPanelView::OnDownloadRemoved(download::DownloadItem* item) {
  ScheduleRebuild();
}

void DownloadsPanelView::OnDownloadDestroyed(download::DownloadItem* item) {
  // The item is going away under us: drop every reference to it before the
  // rebuild, and never call back into it.
  std::erase(observed_items_, item);
  auto it = rows_.find(item);
  if (it != rows_.end()) {
    if (it->second) {
      it->second->OnItemGone();
    }
    rows_.erase(it);
  }
  ScheduleRebuild();
}

std::vector<download::DownloadItem*> DownloadsPanelView::GetVisibleDownloads() {
  std::vector<download::DownloadItem*> visible;
  if (!manager_) {
    return visible;
  }

  content::DownloadManager::DownloadVector all;
  manager_->GetAllDownloads(&all);
  for (download::DownloadItem* item : all) {
    // Transient downloads are the browser fetching something for itself; they
    // are not what the user means by "my downloads".
    if (!item || item->IsTransient()) {
      continue;
    }
    visible.push_back(item);
  }

  std::sort(visible.begin(), visible.end(),
            [](download::DownloadItem* a, download::DownloadItem* b) {
              return a->GetStartTime() > b->GetStartTime();
            });
  if (visible.size() > kMaxRows) {
    visible.resize(kMaxRows);
  }
  return visible;
}

void DownloadsPanelView::StopObservingItems() {
  for (download::DownloadItem* item : observed_items_) {
    item->RemoveObserver(this);
  }
  observed_items_.clear();
}

void DownloadsPanelView::ScheduleRebuild() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DownloadsPanelView::RebuildList,
                                weak_factory_.GetWeakPtr()));
}

void DownloadsPanelView::RebuildList() {
  StopObservingItems();
  rows_.clear();
  if (list_) {
    list_->RemoveAllChildViews();
  }

  const std::vector<download::DownloadItem*> visible = GetVisibleDownloads();
  for (download::DownloadItem* item : visible) {
    item->AddObserver(this);
    observed_items_.push_back(item);
    rows_[item] =
        list_->AddChildView(std::make_unique<DownloadRowView>(item));
  }

  if (empty_label_) {
    empty_label_->SetVisible(visible.empty());
  }
  if (clear_button_) {
    clear_button_->SetVisible(!visible.empty());
  }

  InvalidateLayout();

  // The bubble is sized to its contents at creation, so a list that grew or
  // shrank has to push a new size out to the widget.
  if (views::Widget* widget = GetWidget()) {
    if (views::BubbleDialogDelegate* bubble =
            widget->widget_delegate()->AsBubbleDialogDelegate()) {
      bubble->SizeToContents();
    }
  }
}

void DownloadsPanelView::ClearFinished() {
  // Collected first: removing an item destroys it, which invalidates the
  // manager's vector mid-walk.
  std::vector<download::DownloadItem*> finished;
  for (download::DownloadItem* item : GetVisibleDownloads()) {
    if (item->GetState() != download::DownloadItem::IN_PROGRESS) {
      finished.push_back(item);
    }
  }
  StopObservingItems();
  rows_.clear();
  for (download::DownloadItem* item : finished) {
    item->Remove();
  }
  ScheduleRebuild();
}

void DownloadsPanelView::ShowAllDownloads() {
  if (!browser_) {
    return;
  }
  NavigateParams params(browser_, GURL("chrome://downloads"),
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  if (GetWidget()) {
    GetWidget()->Close();
  }
}

BEGIN_METADATA(DownloadsPanelView)
END_METADATA

void ShowDownloadsPanel(views::View* anchor,
                        Profile* profile,
                        BrowserWindowInterface* browser) {
  if (!anchor || !profile) {
    return;
  }

  // The Space bar sits at the foot of the sidebar, so the panel opens upward
  // out of the button rather than down off the bottom of the window.  The
  // width is fixed rather than derived from the contents: a row's filename
  // would otherwise decide how wide the panel is.
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::BOTTOM_LEFT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets::VH(4, 0));
  delegate->set_fixed_width(DownloadsPanelView::kPanelWidth);
  delegate->SetTitle(u"");
  delegate->SetShowTitle(false);
  delegate->SetShowCloseButton(false);
  delegate->SetBackgroundColor(kPanelBg);

  delegate->SetContentsView(
      std::make_unique<DownloadsPanelView>(profile, browser));

  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  widget->Show();
}

}  // namespace avora
