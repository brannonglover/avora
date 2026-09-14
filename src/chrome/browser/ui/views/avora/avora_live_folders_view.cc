// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_live_folders_view.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr int kMenuRefreshId = 1;
constexpr int kMenuDisconnectId = 2;
constexpr int kMenuRemoveFolderId = 3;
constexpr int kMenuConnectId = 4;

constexpr int kMenuFilterCreatedId = 10;
constexpr int kMenuFilterAssignedId = 11;
constexpr int kMenuFilterReviewId = 12;
constexpr int kMenuFilterMentionedId = 13;
constexpr int kMenuFilterTeamReviewId = 14;
constexpr int kMenuFilterDraftsId = 15;

constexpr int kMenuOpenItemId = 20;

// Matches the pinned section so the two read as one list.
constexpr SkColor kRowHoverBg = SkColorSetARGB(0x1A, 0xFF, 0xFF, 0xFF);
constexpr SkColor kLabelColor = SkColorSetARGB(0xE0, 0xED, 0xF2, 0xF5);
constexpr SkColor kSubtleColor = SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5);
constexpr SkColor kChevronColor = SkColorSetARGB(0xB0, 0xED, 0xF2, 0xF5);

// GitHub's own state colours, so the dots mean what they do on the site.
constexpr SkColor kStatusOpenColor = SkColorSetRGB(0x3F, 0xB9, 0x50);
constexpr SkColor kStatusDraftColor = SkColorSetRGB(0x8B, 0x94, 0x9E);
constexpr SkColor kStatusMergedColor = SkColorSetRGB(0xA3, 0x71, 0xF7);

constexpr SkColor kBadgeColor = SkColorSetRGB(0x31, 0x8C, 0xF5);

constexpr int kStatusDotSize = 8;
constexpr int kChevronSize = 8;

SkColor ColorForStatus(LiveFolderItemStatus status) {
  switch (status) {
    case LiveFolderItemStatus::kOpen:
      return kStatusOpenColor;
    case LiveFolderItemStatus::kDraft:
      return kStatusDraftColor;
    case LiveFolderItemStatus::kMerged:
      return kStatusMergedColor;
    case LiveFolderItemStatus::kClosed:
      return kStatusDraftColor;
  }
  return kStatusOpenColor;
}

void PaintDot(gfx::Canvas* canvas, int cx, int cy, int size, SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawCircle(gfx::Point(cx, cy), size / 2.0f, flags);
}

// Disclosure triangle: points down when expanded, right when collapsed.
void PaintChevron(gfx::Canvas* canvas,
                  int x,
                  int y,
                  int size,
                  bool expanded,
                  SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  flags.setStyle(cc::PaintFlags::kFill_Style);

  const float fx = static_cast<float>(x);
  const float fy = static_cast<float>(y);
  const float s = static_cast<float>(size);

  SkPathBuilder builder;
  if (expanded) {
    builder.moveTo(fx, fy + s * 0.25f);
    builder.lineTo(fx + s, fy + s * 0.25f);
    builder.lineTo(fx + s * 0.5f, fy + s * 0.85f);
  } else {
    builder.moveTo(fx + s * 0.25f, fy);
    builder.lineTo(fx + s * 0.25f, fy + s);
    builder.lineTo(fx + s * 0.85f, fy + s * 0.5f);
  }
  builder.close();
  canvas->DrawPath(builder.detach(), flags);
}

// Rounded pill behind the unseen count.
void PaintUnseenBadge(gfx::Canvas* canvas, const gfx::Rect& bounds) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(kBadgeColor);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(gfx::RectF(bounds), bounds.height() / 2.0f, flags);
}

std::u16string FormatSubtitle(const LiveFolderItem& item) {
  std::u16string repo = base::UTF8ToUTF16(item.metadata.repo);
  if (item.metadata.number > 0) {
    return base::StrCat(
        {repo, u" #", base::NumberToString16(item.metadata.number)});
  }
  return repo;
}

// A full-width row of text that behaves like the other sidebar rows.  Used for
// the connect prompt and the empty/error states; |on_click| is null for the
// purely informational ones, which then render as plain text.
class MessageRow : public views::View {
  METADATA_HEADER(MessageRow, views::View)

 public:
  MessageRow(const std::u16string& text,
             SkColor color,
             base::RepeatingClosure on_click)
      : on_click_(std::move(on_click)) {
    SetPreferredSize(gfx::Size(0, AvoraLiveFoldersView::kRowHeight));
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0,
                          AvoraLiveFoldersView::kHPadding +
                              AvoraLiveFoldersView::kIndent,
                          0, AvoraLiveFoldersView::kHPadding)));

    auto* label = AddChildView(std::make_unique<views::Label>(text));
    label->SetEnabledColor(color);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);

    if (!text.empty()) {
      SetTooltipText(text);
    }
  }

  ~MessageRow() override = default;

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (hovered_ && on_click_) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kRowHoverBg);
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 6.0f, flags);
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (event.IsLeftMouseButton() && on_click_) {
      on_click_.Run();
      return true;
    }
    return false;
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    hovered_ = true;
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    hovered_ = false;
    SchedulePaint();
  }

 private:
  bool hovered_ = false;
  base::RepeatingClosure on_click_;
};

BEGIN_METADATA(MessageRow)
END_METADATA

// Generic delegate that maps command ids onto callbacks, so each menu does not
// need its own subclass.
class CallbackMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  using CommandCallback = base::RepeatingCallback<void(int)>;
  using CheckedCallback = base::RepeatingCallback<bool(int)>;

  CallbackMenuDelegate(CommandCallback on_command, CheckedCallback is_checked)
      : on_command_(std::move(on_command)),
        is_checked_(std::move(is_checked)) {}

  void ExecuteCommand(int command_id, int event_flags) override {
    if (on_command_) {
      on_command_.Run(command_id);
    }
  }

  bool IsCommandIdChecked(int command_id) const override {
    return is_checked_ ? is_checked_.Run(command_id) : false;
  }

  bool IsCommandIdEnabled(int command_id) const override { return true; }

 private:
  CommandCallback on_command_;
  CheckedCallback is_checked_;
};

// Builds the modal that collects a GitHub personal access token.
//
// A plain token field rather than an OAuth redirect because Avora ships no
// client secret; the provider contract lets an OAuth flow replace this later
// without the folder, store, or view changing.
std::unique_ptr<views::DialogDelegate> MakeGitHubConnectDialog(
    base::OnceCallback<void(const std::string&)> on_connect) {
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetModalType(ui::mojom::ModalType::kWindow);
  delegate->SetTitle(u"Connect GitHub");
  delegate->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Connect");
  delegate->SetShowCloseButton(true);

  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 10));

  auto* description = contents->AddChildView(std::make_unique<views::Label>(
      u"Paste a personal access token with the \"repo\" scope. Avora uses it "
      u"only to list the pull requests that involve you, and stores it "
      u"encrypted for this Space."));
  description->SetMultiLine(true);
  description->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  description->SetMaximumWidth(360);

  auto* token_field =
      contents->AddChildView(std::make_unique<views::Textfield>());
  token_field->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
  token_field->SetPlaceholderText(u"ghp_...");
  token_field->SetAccessibleName(u"GitHub personal access token");

  delegate->SetContentsView(std::move(contents));
  delegate->SetInitiallyFocusedView(token_field);

  // The contents view outlives the accept callback, so the raw field pointer
  // is safe to read here.
  delegate->SetAcceptCallback(base::BindOnce(
      [](views::Textfield* field,
         base::OnceCallback<void(const std::string&)> on_connect) {
        std::move(on_connect).Run(base::UTF16ToUTF8(field->GetText()));
      },
      token_field, std::move(on_connect)));

  return delegate;
}

}  // namespace

// ---------------------------------------------------------------------------
// LiveFolderHeaderRow
// ---------------------------------------------------------------------------

LiveFolderHeaderRow::LiveFolderHeaderRow(
    const std::string& folder_id,
    const std::string& name,
    bool expanded,
    bool syncing,
    int unseen_count,
    base::RepeatingCallback<void(const std::string&)> on_toggle,
    base::RepeatingCallback<void(const std::string&, const gfx::Point&)>
        on_context)
    : folder_id_(folder_id),
      expanded_(expanded),
      syncing_(syncing),
      unseen_count_(unseen_count),
      on_toggle_(std::move(on_toggle)),
      on_context_(std::move(on_context)) {
  SetPreferredSize(gfx::Size(0, AvoraLiveFoldersView::kRowHeight));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, AvoraLiveFoldersView::kHPadding + kChevronSize + 8,
                        0, AvoraLiveFoldersView::kHPadding),
      8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* label = AddChildView(
      std::make_unique<views::Label>(base::UTF8ToUTF16(name)));
  label->SetEnabledColor(kLabelColor);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  layout->SetFlexForView(label, 1);

  if (syncing_) {
    auto* status = AddChildView(std::make_unique<views::Label>(u"Updating…"));
    status->SetEnabledColor(kSubtleColor);
  }
}

LiveFolderHeaderRow::~LiveFolderHeaderRow() = default;

void LiveFolderHeaderRow::OnPaintBackground(gfx::Canvas* canvas) {
  if (hovered_) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kRowHoverBg);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 6.0f, flags);
  }

  PaintChevron(canvas, AvoraLiveFoldersView::kHPadding,
               (height() - kChevronSize) / 2, kChevronSize, expanded_,
               kChevronColor);

  // The badge only earns its space when the rows it refers to are hidden.
  if (!expanded_ && unseen_count_ > 0) {
    const int badge_height = 16;
    const int badge_width = unseen_count_ > 9 ? 26 : 20;
    const gfx::Rect badge(width() - AvoraLiveFoldersView::kHPadding -
                              badge_width,
                          (height() - badge_height) / 2, badge_width,
                          badge_height);
    PaintUnseenBadge(canvas, badge);
    canvas->DrawStringRectWithFlags(
        base::NumberToString16(unseen_count_), gfx::FontList(), SK_ColorWHITE,
        badge, gfx::Canvas::TEXT_ALIGN_CENTER);
  }
}

bool LiveFolderHeaderRow::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsRightMouseButton()) {
    gfx::Point screen_point = event.location();
    views::View::ConvertPointToScreen(this, &screen_point);
    if (on_context_) {
      on_context_.Run(folder_id_, screen_point);
    }
    return true;
  }
  if (event.IsLeftMouseButton() && on_toggle_) {
    on_toggle_.Run(folder_id_);
    return true;
  }
  return false;
}

void LiveFolderHeaderRow::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
}

void LiveFolderHeaderRow::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
}

BEGIN_METADATA(LiveFolderHeaderRow)
END_METADATA

// ---------------------------------------------------------------------------
// LiveFolderItemRow
// ---------------------------------------------------------------------------

LiveFolderItemRow::LiveFolderItemRow(
    const std::string& folder_id,
    const LiveFolderItem& item,
    base::RepeatingCallback<void(const std::string&, const std::string&)>
        on_click,
    base::RepeatingCallback<void(const std::string&, const std::string&,
                                 const gfx::Point&)> on_context)
    : folder_id_(folder_id),
      external_id_(item.external_id),
      url_(item.url),
      status_(item.status),
      unseen_(!item.seen),
      on_click_(std::move(on_click)),
      on_context_(std::move(on_context)) {
  SetPreferredSize(gfx::Size(0, AvoraLiveFoldersView::kItemRowHeight));

  const int left_inset = AvoraLiveFoldersView::kHPadding +
                         AvoraLiveFoldersView::kIndent + kStatusDotSize + 8;
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(5, left_inset, 5, AvoraLiveFoldersView::kHPadding),
      1));

  auto* title = AddChildView(
      std::make_unique<views::Label>(base::UTF8ToUTF16(item.title)));
  title->SetEnabledColor(kLabelColor);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetElideBehavior(gfx::ELIDE_TAIL);
  if (unseen_) {
    title->SetFontList(title->font_list().DeriveWithWeight(
        gfx::Font::Weight::SEMIBOLD));
  }

  const std::u16string subtitle = FormatSubtitle(item);
  if (!subtitle.empty()) {
    auto* repo = AddChildView(std::make_unique<views::Label>(subtitle));
    repo->SetEnabledColor(kSubtleColor);
    repo->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    repo->SetElideBehavior(gfx::ELIDE_HEAD);
    repo->SetFontList(repo->font_list().DeriveWithSizeDelta(-1));
  }

  SetTooltipText(base::UTF8ToUTF16(item.title));
}

LiveFolderItemRow::~LiveFolderItemRow() = default;

void LiveFolderItemRow::OnPaintBackground(gfx::Canvas* canvas) {
  if (hovered_) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kRowHoverBg);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 6.0f, flags);
  }

  PaintDot(canvas,
           AvoraLiveFoldersView::kHPadding + AvoraLiveFoldersView::kIndent +
               kStatusDotSize / 2,
           height() / 2, kStatusDotSize, ColorForStatus(status_));
}

bool LiveFolderItemRow::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsRightMouseButton()) {
    gfx::Point screen_point = event.location();
    views::View::ConvertPointToScreen(this, &screen_point);
    if (on_context_) {
      on_context_.Run(folder_id_, external_id_, screen_point);
    }
    return true;
  }
  if (event.IsLeftMouseButton() && on_click_) {
    on_click_.Run(folder_id_, external_id_);
    return true;
  }
  return false;
}

void LiveFolderItemRow::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  SchedulePaint();
}

void LiveFolderItemRow::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  SchedulePaint();
}

BEGIN_METADATA(LiveFolderItemRow)
END_METADATA

// ---------------------------------------------------------------------------
// AvoraLiveFoldersView
// ---------------------------------------------------------------------------

AvoraLiveFoldersView::AvoraLiveFoldersView(BrowserWindowInterface* browser,
                                           avora::WindowSpaceState* window_space_state)
    : browser_(browser), window_space_state_(window_space_state) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  service_ = LiveFolderService::GetForProfile(browser_->GetProfile());
  if (service_) {
    store_ = service_->store();
    service_observation_.Observe(service_);
  }
  if (store_) {
    store_observation_.Observe(store_);
  }

  if (window_space_state_) {
    window_space_state_->AddObserver(this);
  }

  tab_strip_model_ = browser_->GetTabStripModel();
  if (tab_strip_model_) {
    tab_strip_model_->AddObserver(this);
  }

  Rebuild();
}

AvoraLiveFoldersView::~AvoraLiveFoldersView() {
  if (window_space_state_) {
    window_space_state_->RemoveObserver(this);
  }
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void AvoraLiveFoldersView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  ReportActiveHost();
}

void AvoraLiveFoldersView::OnTabChangedAt(tabs::TabInterface* tab,
                                          TabChangeType change_type) {
  ReportActiveHost();
}

void AvoraLiveFoldersView::ReportActiveHost() {
  if (!service_ || !tab_strip_model_) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetActiveWebContents();
  if (!contents) {
    return;
  }
  service_->OnHostVisited(std::string(contents->GetLastCommittedURL().host()));
}

void AvoraLiveFoldersView::OnLiveFoldersChanged() {
  ScheduleRebuild();
}

void AvoraLiveFoldersView::OnWindowActiveSpaceChanged(
    const std::string& space_id) {
  ScheduleRebuild();
}

void AvoraLiveFoldersView::OnLiveFolderSyncStateChanged(
    const std::string& folder_id) {
  ScheduleRebuild();
}

void AvoraLiveFoldersView::ScheduleRebuild() {
  if (rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<AvoraLiveFoldersView> view) {
                       if (!view) {
                         return;
                       }
                       view->rebuild_pending_ = false;
                       view->Rebuild();
                     },
                     weak_factory_.GetWeakPtr()));
}

gfx::Size AvoraLiveFoldersView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  int height = 0;
  for (const views::View* child : children()) {
    if (child->GetVisible()) {
      height += child->GetPreferredSize().height();
    }
  }
  return gfx::Size(available_size.width().is_bounded()
                       ? available_size.width().value()
                       : 0,
                   height);
}

void AvoraLiveFoldersView::Rebuild() {
  RemoveAllChildViews();

  // Use the window-local active Space to query the right set of folders.
  const std::vector<LiveFolder> folders =
      (window_space_state_ && store_)
          ? store_->GetFoldersForSpace(window_space_state_->active_space_id())
          : (store_ ? store_->GetFolders()
                    : std::vector<LiveFolder>());

  if (folders.empty()) {
    InvalidateLayout();
    return;
  }

  for (const LiveFolder& folder : folders) {
    const LiveFolderService::SyncState state =
        service_ ? service_->GetSyncState(folder.id)
                 : LiveFolderService::SyncState();

    AddChildView(std::make_unique<LiveFolderHeaderRow>(
        folder.id, folder.name, folder.expanded, state.syncing,
        folder.UnseenCount(),
        base::BindRepeating(&AvoraLiveFoldersView::OnFolderToggle,
                            weak_factory_.GetWeakPtr()),
        base::BindRepeating(&AvoraLiveFoldersView::OnFolderContextMenu,
                            weak_factory_.GetWeakPtr())));

    if (!folder.expanded) {
      continue;
    }

    if (!state.connected) {
      AddConnectRow(folder.id);
      continue;
    }

    const std::vector<LiveFolderItem> items = folder.ActiveItems();
    if (!items.empty()) {
      for (const LiveFolderItem& item : items) {
        AddChildView(std::make_unique<LiveFolderItemRow>(
            folder.id, item,
            base::BindRepeating(&AvoraLiveFoldersView::OnItemClicked,
                                weak_factory_.GetWeakPtr()),
            base::BindRepeating(&AvoraLiveFoldersView::OnItemContextMenu,
                                weak_factory_.GetWeakPtr())));
      }
      continue;
    }

    // No rows.  Failures get a message, because a broken folder and an empty
    // one would otherwise look identical.  A successful sync that simply
    // matched nothing gets no row at all: "you have no pull requests" is the
    // expected resting state, and a line of text restating it is noise the
    // user cannot act on.
    switch (state.last_status) {
      case LiveFolderFetchStatus::kAuthFailed:
      case LiveFolderFetchStatus::kNetworkError:
      case LiveFolderFetchStatus::kRateLimited:
        AddMessageRow(base::UTF8ToUTF16(state.last_error));
        break;
      case LiveFolderFetchStatus::kNotConnected:
        AddConnectRow(folder.id);
        break;
      case LiveFolderFetchStatus::kSuccess:
        if (folder.last_synced_at.is_null()) {
          AddMessageRow(u"Looking for pull requests…");
        }
        break;
    }
  }

  InvalidateLayout();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void AvoraLiveFoldersView::AddMessageRow(const std::u16string& text) {
  AddChildView(std::make_unique<MessageRow>(text, kSubtleColor,
                                            base::RepeatingClosure()));
}

void AvoraLiveFoldersView::AddConnectRow(const std::string& folder_id) {
  AddChildView(std::make_unique<MessageRow>(
      u"Connect GitHub…", kBadgeColor,
      base::BindRepeating(&AvoraLiveFoldersView::ShowConnectDialog,
                          weak_factory_.GetWeakPtr(), folder_id)));
}

// ── Actions ─────────────────────────────────────────────────────────────────

void AvoraLiveFoldersView::OnFolderToggle(const std::string& folder_id) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder = store_->GetFolderById(folder_id);
  if (!folder) {
    return;
  }
  store_->SetExpanded(folder_id, !folder->expanded);

  // Opening a folder is the moment its contents matter, so take the chance to
  // refresh -- subject to the service's minimum spacing.
  if (!folder->expanded && service_) {
    service_->RefreshFolder(folder_id);
  }
}

void AvoraLiveFoldersView::OnItemClicked(const std::string& folder_id,
                                         const std::string& external_id) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder = store_->GetFolderById(folder_id);
  if (!folder) {
    return;
  }
  for (const LiveFolderItem& item : folder->items) {
    if (item.external_id == external_id) {
      store_->MarkItemSeen(folder_id, external_id);
      OpenUrl(item.url);
      return;
    }
  }
}

void AvoraLiveFoldersView::OpenUrl(const std::string& url) {
  if (url.empty() || !browser_) {
    return;
  }

  // Reuse an existing tab so clicking the same pull request twice does not
  // accumulate duplicates, matching the pinned section's behaviour.
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  if (tab_strip) {
    for (int i = 0; i < tab_strip->count(); ++i) {
      content::WebContents* contents = tab_strip->GetWebContentsAt(i);
      if (contents && contents->GetLastCommittedURL().spec() == url) {
        tab_strip->ActivateTabAt(i);
        return;
      }
    }
  }

  NavigateParams params(browser_, GURL(url), ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void AvoraLiveFoldersView::StartGitHubConnectFlow() {
  if (!store_) {
    return;
  }
  const std::string folder_id = store_->EnsureGitHubPullRequestsFolder();
  if (!folder_id.empty()) {
    ShowConnectDialog(folder_id);
  }
}

void AvoraLiveFoldersView::ShowConnectDialog(const std::string& folder_id) {
  if (!service_ || !GetWidget()) {
    return;
  }

  auto dialog = MakeGitHubConnectDialog(base::BindOnce(
      [](base::WeakPtr<AvoraLiveFoldersView> view, const std::string& token) {
        if (!view || !view->service_) {
          return;
        }
        view->service_->ConnectProvider(
            LiveFolderProviderId::kGitHub, token,
            base::BindOnce(
                [](base::WeakPtr<AvoraLiveFoldersView> view, bool success,
                   const std::string& error) {
                  if (view) {
                    view->ScheduleRebuild();
                  }
                },
                view));
      },
      weak_factory_.GetWeakPtr()));

  views::DialogDelegate::CreateDialogWidget(std::move(dialog),
                                            gfx::NativeWindow(),
                                            GetWidget()->GetNativeView())
      ->Show();
}

void AvoraLiveFoldersView::ToggleQueryFilter(const std::string& folder_id,
                                             int command_id) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder = store_->GetFolderById(folder_id);
  if (!folder) {
    return;
  }

  LiveFolderQuery query = folder->query;
  switch (command_id) {
    case kMenuFilterCreatedId:
      query.created_by_me = !query.created_by_me;
      break;
    case kMenuFilterAssignedId:
      query.assigned_to_me = !query.assigned_to_me;
      break;
    case kMenuFilterReviewId:
      query.review_requested = !query.review_requested;
      break;
    case kMenuFilterMentionedId:
      query.mentioned = !query.mentioned;
      break;
    case kMenuFilterTeamReviewId:
      query.team_review_requested = !query.team_review_requested;
      break;
    case kMenuFilterDraftsId:
      query.include_drafts = !query.include_drafts;
      break;
    default:
      return;
  }

  store_->SetQuery(folder_id, query);
  if (service_) {
    service_->ForceRefreshFolder(folder_id);
  }
}

// ── Context menus ───────────────────────────────────────────────────────────

void AvoraLiveFoldersView::OnFolderContextMenu(
    const std::string& folder_id,
    const gfx::Point& screen_point) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder = store_->GetFolderById(folder_id);
  if (!folder) {
    return;
  }

  const bool connected =
      service_ && service_->GetSyncState(folder_id).connected;

  context_menu_delegate_ = std::make_unique<CallbackMenuDelegate>(
      base::BindRepeating(
          [](base::WeakPtr<AvoraLiveFoldersView> view,
             const std::string& folder_id, int command_id) {
            if (!view) {
              return;
            }
            switch (command_id) {
              case kMenuRefreshId:
                if (view->service_) {
                  view->service_->ForceRefreshFolder(folder_id);
                }
                break;
              case kMenuConnectId:
                view->ShowConnectDialog(folder_id);
                break;
              case kMenuDisconnectId:
                if (view->service_) {
                  view->service_->DisconnectProvider(
                      LiveFolderProviderId::kGitHub);
                }
                break;
              case kMenuRemoveFolderId:
                if (view->store_) {
                  view->store_->RemoveFolder(folder_id);
                }
                break;
              default:
                view->ToggleQueryFilter(folder_id, command_id);
                break;
            }
          },
          weak_factory_.GetWeakPtr(), folder_id),
      base::BindRepeating(
          [](LiveFolderQuery query, int command_id) {
            switch (command_id) {
              case kMenuFilterCreatedId:
                return query.created_by_me;
              case kMenuFilterAssignedId:
                return query.assigned_to_me;
              case kMenuFilterReviewId:
                return query.review_requested;
              case kMenuFilterMentionedId:
                return query.mentioned;
              case kMenuFilterTeamReviewId:
                return query.team_review_requested;
              case kMenuFilterDraftsId:
                return query.include_drafts;
              default:
                return false;
            }
          },
          folder->query));

  filter_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  filter_menu_model_->AddCheckItem(kMenuFilterCreatedId, u"Created by Me");
  filter_menu_model_->AddCheckItem(kMenuFilterAssignedId, u"Assigned to Me");
  filter_menu_model_->AddCheckItem(kMenuFilterReviewId, u"Review Requested");
  filter_menu_model_->AddCheckItem(kMenuFilterTeamReviewId,
                                   u"Team Review Requested");
  filter_menu_model_->AddCheckItem(kMenuFilterMentionedId, u"Mentions Me");
  filter_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  filter_menu_model_->AddCheckItem(kMenuFilterDraftsId, u"Include Drafts");

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  if (connected) {
    context_menu_model_->AddItem(kMenuRefreshId, u"Refresh Now");
    context_menu_model_->AddSubMenu(0, u"Filter", filter_menu_model_.get());
    context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    context_menu_model_->AddItem(kMenuDisconnectId, u"Disconnect GitHub");
  } else {
    context_menu_model_->AddItem(kMenuConnectId, u"Connect GitHub…");
  }
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddItem(kMenuRemoveFolderId, u"Remove Folder");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
}

void AvoraLiveFoldersView::OnItemContextMenu(const std::string& folder_id,
                                             const std::string& external_id,
                                             const gfx::Point& screen_point) {
  if (!store_) {
    return;
  }
  const std::optional<LiveFolder> folder = store_->GetFolderById(folder_id);
  if (!folder) {
    return;
  }

  std::string url;
  for (const LiveFolderItem& item : folder->items) {
    if (item.external_id == external_id) {
      url = item.url;
      break;
    }
  }
  if (url.empty()) {
    return;
  }

  context_menu_delegate_ = std::make_unique<CallbackMenuDelegate>(
      base::BindRepeating(
          [](base::WeakPtr<AvoraLiveFoldersView> view, const std::string& url,
             int command_id) {
            if (!view) {
              return;
            }
            if (command_id == kMenuOpenItemId) {
              view->OpenUrl(url);
            }
          },
          weak_factory_.GetWeakPtr(), url),
      base::RepeatingCallback<bool(int)>());

  context_menu_model_ =
      std::make_unique<ui::SimpleMenuModel>(context_menu_delegate_.get());
  context_menu_model_->AddItem(kMenuOpenItemId, u"Open Pull Request");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
}

BEGIN_METADATA(AvoraLiveFoldersView)
END_METADATA

}  // namespace avora
