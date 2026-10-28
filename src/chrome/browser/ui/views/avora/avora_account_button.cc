// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_account_button.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/avora/avora_local_user.h"
#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_space.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_storage_partition.h"
#include "chrome/browser/avora/avora_window_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace avora {

namespace {

// The spaces bar's button slot, and the circle drawn inside it.
constexpr int kButtonSize = 28;
constexpr int kAvatarSize = 22;
constexpr int kHeaderAvatarSize = 36;
constexpr int kButtonCornerRadius = 8;

// The person sits above every Space, so the avatar takes no Space's accent.
constexpr SkColor kAvatarFill = SkColorSetRGB(0x5B, 0x6B, 0x7D);
constexpr SkColor kAvatarText = SK_ColorWHITE;
constexpr SkColor kButtonHoverBg = SkColorSetARGB(0x14, 0xFF, 0xFF, 0xFF);

// The menu matches the Manage bubble (AvoraSidebarView), its sibling.
constexpr SkColor kMenuBg = SkColorSetRGB(0x22, 0x28, 0x2E);
constexpr SkColor kSectionText = SkColorSetARGB(0x88, 0xED, 0xF2, 0xF5);
constexpr SkColor kRowText = SkColorSetRGB(0xED, 0xF2, 0xF5);
constexpr SkColor kRowSubText = SkColorSetARGB(0x99, 0xED, 0xF2, 0xF5);
constexpr SkColor kRowHoverBg = SkColorSetARGB(0x18, 0xFF, 0xFF, 0xFF);
constexpr SkColor kRowIconColor = SkColorSetARGB(0xBB, 0xED, 0xF2, 0xF5);
constexpr int kMenuWidth = 260;
constexpr int kRowHeight = 36;
constexpr int kTwoLineRowHeight = 46;
constexpr int kRowIconSize = 16;

// Lucide "check" and "chevron-left".
constexpr char kCheckIconPath[] = "M20 6 9 17l-5-5";
constexpr char kChevronLeftIconPath[] = "m15 18-6-6 6-6";

// Sign-in pages.
constexpr int kBackButtonSize = 24;
constexpr int kFieldHeight = 32;
constexpr int kPrimaryButtonHeight = 32;
constexpr SkColor kFieldBg = SkColorSetRGB(0x18, 0x1C, 0x20);
constexpr SkColor kFieldBorder = SkColorSetARGB(0x33, 0xED, 0xF2, 0xF5);
constexpr SkColor kPrimaryButtonBg = SkColorSetRGB(0x3B, 0x82, 0xF6);
constexpr SkColor kPrimaryButtonHoverBg = SkColorSetRGB(0x2F, 0x6F, 0xE0);
constexpr SkColor kErrorText = SkColorSetRGB(0xFF, 0x8A, 0x80);
constexpr SkColor kLinkText = SkColorSetRGB(0x8A, 0xB4, 0xF8);
constexpr SkColor kSyncedText = SkColorSetRGB(0x7D, 0xD3, 0xA0);
constexpr SkColor kWarningText = SkColorSetRGB(0xF5, 0xC0, 0x6B);
constexpr base::TimeDelta kSlideDuration = base::Milliseconds(220);

constexpr char kSettingsUrl[] = "chrome://avora-settings/";
constexpr char kAccountSettingsUrl[] = "chrome://avora-settings/#account";
constexpr char kExtensionsUrl[] = "chrome://extensions/";

gfx::FontList SystemFont(int size, gfx::Font::Weight weight) {
  return gfx::FontList({std::string("system-ui")}, gfx::Font::NORMAL, size,
                       weight);
}

// ── The OS account, read once per run and shared by every window ──

struct LocalUserCache {
  bool requested = false;
  bool overridden_for_testing = false;
  std::u16string full_name;
  gfx::ImageSkia photo;
  base::RepeatingClosureList on_changed;
};

LocalUserCache& GetLocalUserCache() {
  static base::NoDestructor<LocalUserCache> cache;
  return *cache;
}

void StoreLocalUser(const LocalUser& user) {
  LocalUserCache& cache = GetLocalUserCache();
  cache.full_name = user.full_name;
  cache.photo = gfx::ImageSkia();
  if (!user.photo_png.empty()) {
    const gfx::Image image = gfx::Image::CreateFrom1xPNGBytes(user.photo_png);
    if (!image.IsEmpty()) {
      cache.photo = image.AsImageSkia();
    }
  }
  cache.on_changed.Notify();
}

void OnLocalUserRead(LocalUser user) {
  // A test's stand-in wins over the real account, whenever that arrives.
  if (!GetLocalUserCache().overridden_for_testing) {
    StoreLocalUser(user);
  }
}

void EnsureLocalUserRequested() {
  LocalUserCache& cache = GetLocalUserCache();
  if (cache.requested) {
    return;
  }
  cache.requested = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&GetLocalUserBlocking), base::BindOnce(&OnLocalUserRead));
}

// The avatar: the account picture cropped to a circle, else initials on a
// neutral disc, else a person glyph when there are not even initials.
void PaintAvatar(gfx::Canvas* canvas,
                 const gfx::Rect& bounds,
                 const gfx::ImageSkia& photo,
                 const std::u16string& initials) {
  const int diameter = std::min(bounds.width(), bounds.height());
  gfx::Rect circle(bounds);
  circle.ClampToCenteredSize(gfx::Size(diameter, diameter));

  if (!photo.isNull()) {
    gfx::ScopedCanvas scoped(canvas);
    canvas->ClipPath(SkPath::Oval(gfx::RectToSkRect(circle)),
                     /*do_anti_alias=*/true);
    canvas->DrawImageInt(photo, 0, 0, photo.width(), photo.height(),
                         circle.x(), circle.y(), diameter, diameter,
                         /*filter=*/true);
    return;
  }

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(kAvatarFill);
  canvas->DrawCircle(circle.CenterPoint(), diameter / 2, flags);

  if (initials.empty()) {
    gfx::Rect glyph(circle);
    glyph.Inset(diameter / 5);
    PaintLucideIcon(canvas, glyph, "user", kAvatarText);
    return;
  }
  canvas->DrawStringRectWithFlags(
      initials,
      SystemFont(std::max(9, diameter * 2 / 5), gfx::Font::Weight::SEMIBOLD),
      kAvatarText, circle, gfx::Canvas::TEXT_ALIGN_CENTER);
}

// The larger avatar at the head of the menu.
class AvatarView : public views::View {
  METADATA_HEADER(AvatarView, views::View)

 public:
  AvatarView(gfx::ImageSkia photo, std::u16string initials)
      : photo_(std::move(photo)), initials_(std::move(initials)) {
    SetPreferredSize(gfx::Size(kHeaderAvatarSize, kHeaderAvatarSize));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    PaintAvatar(canvas, GetLocalBounds(), photo_, initials_);
  }

 private:
  gfx::ImageSkia photo_;
  std::u16string initials_;
};

BEGIN_METADATA(AvatarView)
END_METADATA

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        int size,
                                        gfx::Font::Weight weight,
                                        SkColor color) {
  auto label = std::make_unique<views::Label>(
      text, views::Label::CustomFont{SystemFont(size, weight)});
  label->SetEnabledColor(color);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  return label;
}

// One row of the menu: an icon, a title with an optional second line, and a
// check when it is the current choice.
class MenuRow : public views::Button {
  METADATA_HEADER(MenuRow, views::Button)

 public:
  MenuRow(PressedCallback callback,
          const ui::ImageModel& icon,
          const std::u16string& title,
          const std::u16string& subtitle,
          bool checked)
      : views::Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(
        kMenuWidth, subtitle.empty() ? kRowHeight : kTwoLineRowHeight));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetName(title);
    if (!subtitle.empty()) {
      GetViewAccessibility().SetDescription(subtitle);
    }
    if (checked) {
      GetViewAccessibility().SetCheckedState(ax::mojom::CheckedState::kTrue);
    }

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 12),
        10));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    AddChildView(std::make_unique<views::ImageView>(icon));

    auto text = std::make_unique<views::View>();
    text->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
    text->AddChildView(
        MakeLabel(title, 13, gfx::Font::Weight::NORMAL, kRowText));
    if (!subtitle.empty()) {
      text->AddChildView(
          MakeLabel(subtitle, 11, gfx::Font::Weight::NORMAL, kRowSubText));
    }
    layout->SetFlexForView(AddChildView(std::move(text)), 1);

    if (checked) {
      auto check = std::make_unique<views::View>();
      check->SetPreferredSize(gfx::Size(kRowIconSize, kRowIconSize));
      check_ = AddChildView(std::move(check));
    }

    // The row is the button; its parts only draw.
    for (views::View* child : children()) {
      child->SetCanProcessEventsWithinSubtree(false);
    }
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (check_) {
      gfx::Rect glyph = check_->bounds();
      PaintLucidePathData(canvas, glyph, kCheckIconPath, kRowIconColor);
    }
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (GetState() != views::Button::STATE_HOVERED &&
        GetState() != views::Button::STATE_PRESSED) {
      return;
    }
    gfx::Rect bounds = GetLocalBounds();
    bounds.Inset(gfx::Insets::VH(0, 4));
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(kRowHoverBg);
    canvas->DrawRoundRect(bounds, 6, flags);
  }

 private:
  raw_ptr<views::View> check_ = nullptr;
};

BEGIN_METADATA(MenuRow)
END_METADATA

std::unique_ptr<views::View> MakeSectionLabel(const std::u16string& text) {
  auto label = MakeLabel(text, 11, gfx::Font::Weight::MEDIUM, kSectionText);
  label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(6, 12, 4, 12)));
  return label;
}

std::unique_ptr<views::View> MakeSeparator() {
  auto separator = std::make_unique<views::Separator>();
  separator->SetColorId(ui::kColorSeparator);
  separator->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 12)));
  return separator;
}

ui::ImageModel RowIcon(std::string_view lucide_id, SkColor color) {
  return ui::ImageModel::FromImageSkia(
      LucideIconImage(lucide_id, kRowIconSize, color));
}

// ── Who is signed in ──

// The signed-in Avora email, or empty while signed out or mid-sign-in.
std::string SignedInEmail(Profile* profile) {
  sync::TabSyncService* service =
      sync::TabSyncService::GetForProfileIfExists(profile);
  if (!service || service->state() != sync::TabSyncService::State::kSignedIn) {
    return std::string();
  }
  return service->GetStatus().email;
}

// The Mac account only puts a face and a name to an Avora account; it is not
// one.  Showing it while signed out made Avora look signed in already.
std::u16string DisplayNameFor(Profile* profile) {
  const std::string email = SignedInEmail(profile);
  if (email.empty()) {
    return std::u16string();
  }
  const std::u16string& full_name = GetLocalUserCache().full_name;
  return full_name.empty() ? base::UTF8ToUTF16(email) : full_name;
}

gfx::ImageSkia PhotoFor(Profile* profile) {
  return SignedInEmail(profile).empty() ? gfx::ImageSkia()
                                        : GetLocalUserCache().photo;
}

// The message line under a sign-in step, worded as the settings page words it.
// Empty for results that need no message.
std::u16string MessageFor(sync::TabSyncService::Result result,
                          const std::string& email) {
  using Result = sync::TabSyncService::Result;
  switch (result) {
    case Result::kInvalidEmail:
      return u"Enter a valid email address.";
    case Result::kInvalidCode:
      return u"Enter the code from the email: 6 or more digits.";
    case Result::kWrongCode:
      return u"That code is wrong or has expired. Check the email, or send a "
             u"new code.";
    case Result::kRateLimited:
      return u"Too many attempts. Wait a few minutes, then try again.";
    case Result::kNetworkError:
      return u"Avora could not reach the server. Check your connection and "
             u"try again.";
    case Result::kCodeSent:
      return u"We emailed a new code to " + base::UTF8ToUTF16(email) + u".";
    default:
      return std::u16string();
  }
}

// How long ago |when| was, as the menu's sync line puts it.
std::u16string Ago(base::Time when) {
  const base::TimeDelta age = base::Time::Now() - when;
  if (age < base::Minutes(1)) {
    return u"just now";
  }
  if (age < base::Hours(1)) {
    return base::NumberToString16(age.InMinutes()) + u" min ago";
  }
  if (age < base::Days(1)) {
    return base::NumberToString16(age.InHours()) + u" hr ago";
  }
  return base::NumberToString16(age.InDays()) + u" d ago";
}

// The menu's line on whether the account's shared tabs are in sync, and the
// colour to draw it in.
std::pair<std::u16string, SkColor> SyncLine(
    const sync::TabSyncService::Status& status) {
  if (status.upload_failing) {
    return {u"Tabs couldn't sync. Avora will keep trying.", kWarningText};
  }
  if (status.last_upload.is_null()) {
    return {u"Syncing tabs\u2026", kRowSubText};
  }
  const std::u16string count =
      status.uploaded_tab_count == 1
          ? u"1 tab"
          : base::NumberToString16(status.uploaded_tab_count) + u" tabs";
  return {count + u" synced \u00B7 " + Ago(status.last_upload),
          kSyncedText};
}

bool IsError(sync::TabSyncService::Result result) {
  using Result = sync::TabSyncService::Result;
  return result == Result::kInvalidEmail || result == Result::kInvalidCode ||
         result == Result::kWrongCode || result == Result::kRateLimited ||
         result == Result::kNetworkError;
}

// The chevron that steps a sign-in page back.
class BackButton : public views::Button {
  METADATA_HEADER(BackButton, views::Button)

 public:
  explicit BackButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(kBackButtonSize, kBackButtonSize));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetName(u"Back");
    SetTooltipText(u"Back");
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setColor(kRowHoverBg);
      canvas->DrawRoundRect(GetLocalBounds(), 6, flags);
    }
    gfx::Rect glyph(GetLocalBounds());
    glyph.ClampToCenteredSize(gfx::Size(kRowIconSize, kRowIconSize));
    PaintLucidePathData(canvas, glyph, kChevronLeftIconPath, kRowText);
  }
};

BEGIN_METADATA(BackButton)
END_METADATA

// The one filled button on a sign-in page.
class PrimaryButton : public views::LabelButton {
  METADATA_HEADER(PrimaryButton, views::LabelButton)

 public:
  PrimaryButton(PressedCallback callback, const std::u16string& text)
      : views::LabelButton(std::move(callback), text) {
    label()->SetFontList(SystemFont(13, gfx::Font::Weight::MEDIUM));
    for (const auto state :
         {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
          views::Button::STATE_PRESSED}) {
      SetTextColor(state, SK_ColorWHITE);
    }
    SetTextColor(views::Button::STATE_DISABLED,
                 SkColorSetA(SK_ColorWHITE, 0x99));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetMinSize(gfx::Size(0, kPrimaryButtonHeight));
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    SkColor color = kPrimaryButtonBg;
    if (!GetEnabled()) {
      color = SkColorSetA(color, 0x80);
    } else if (GetState() == views::Button::STATE_HOVERED ||
               GetState() == views::Button::STATE_PRESSED) {
      color = kPrimaryButtonHoverBg;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(color);
    canvas->DrawRoundRect(GetLocalBounds(), 6, flags);
  }
};

BEGIN_METADATA(PrimaryButton)
END_METADATA

// The parts of a sign-in page the account menu reaches into after building it.
struct StepParts {
  raw_ptr<views::Textfield> field = nullptr;
  raw_ptr<views::Label> message = nullptr;
  raw_ptr<views::LabelButton> button = nullptr;
};

// The account menu, and the sign-in steps that slide in over it.
//
// Signing in used to mean leaving for the settings page in a new tab.  Here
// it happens in place, over the avatar: "Sign in to Avora..." slides the menu
// aside for an email field, sending the code slides that aside for a code
// field, and signing in slides back to the menu, now showing who is signed
// in.  The sign-in itself is TabSyncService's, the same calls the settings
// page makes, so either place can finish a sign-in the other started.
class AccountMenuView : public views::View,
                        public views::TextfieldController,
                        public sync::TabSyncService::Observer {
  METADATA_HEADER(AccountMenuView, views::View)

 public:
  AccountMenuView(Profile* profile,
                  BrowserWindowInterface* browser,
                  WindowSpaceState* window_space_state)
      : profile_(profile),
        browser_(browser),
        window_space_state_(window_space_state) {
    // The sliding pages draw on layers of their own; this one clips them to
    // the bubble.
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetMasksToBounds(true);

    if (sync::TabSyncService* service =
            sync::TabSyncService::GetForProfile(profile_)) {
      sync_observation_.Observe(service);
    }
    ShowMenuPage(Slide::kNone);

    // "just now" has to become "2 min ago" with the menu left open.
    sync_line_timer_.Start(FROM_HERE, base::Seconds(30),
                           base::BindRepeating(&AccountMenuView::UpdateSyncLine,
                                               base::Unretained(this)));
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return current_page_ ? current_page_->GetPreferredSize()
                         : gfx::Size(kMenuWidth, 0);
  }

  void Layout(PassKey) override {
    // Every page, the outgoing one included, is laid out at the bubble's
    // full width; the slide is a transform on top of this.
    for (views::View* page : children()) {
      page->SetBounds(0, 0, width(), page->GetPreferredSize().height());
    }
  }

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override {
    if (key_event.type() != ui::EventType::kKeyPressed ||
        key_event.key_code() != ui::VKEY_RETURN) {
      return false;
    }
    Submit();
    return true;
  }

  // sync::TabSyncService::Observer:
  void OnTabSyncStatusChanged() override {
    sync::TabSyncService* service = Service();
    if (!service) {
      return;
    }
    const sync::TabSyncService::Status status = service->GetStatus();
    if (status.busy) {
      return;
    }

    switch (page_) {
      case Page::kMenu:
        // Signed in or out from the settings page while the menu is open.
        if (signed_in_shown_ != !SignedInEmail(profile_).empty()) {
          ShowMenuPage(Slide::kNone);
        } else {
          UpdateSyncLine();
        }
        return;
      case Page::kEmail:
        if (!waiting_) {
          return;
        }
        waiting_ = false;
        if (status.state == sync::TabSyncService::State::kAwaitingCode) {
          ShowCodePage(Slide::kForward);
          return;
        }
        SetWaiting(false, u"Send code");
        ShowMessage(MessageFor(status.last_result, status.email),
                    IsError(status.last_result));
        return;
      case Page::kCode:
        if (!waiting_) {
          return;
        }
        waiting_ = false;
        if (status.state == sync::TabSyncService::State::kSignedIn) {
          ShowMenuPage(Slide::kBack);
          return;
        }
        SetWaiting(false, u"Sign in");
        ShowMessage(MessageFor(status.last_result, status.email),
                    IsError(status.last_result));
        return;
    }
  }

 private:
  enum class Page { kMenu, kEmail, kCode };
  enum class Slide { kNone, kForward, kBack };

  sync::TabSyncService* Service() {
    return sync::TabSyncService::GetForProfileIfExists(profile_);
  }

  // Replaces the page on show.  The outgoing page slides off one side as the
  // new one slides in from the other, and is removed once it is out of view.
  void ShowPage(Page page,
                std::unique_ptr<views::View> contents,
                Slide slide,
                StepParts parts = StepParts()) {
    // The outgoing page's parts are about to go; nothing may reach them.
    page_ = page;
    waiting_ = false;
    sync_line_ = nullptr;
    email_field_ = page == Page::kEmail ? parts.field.get() : nullptr;
    code_field_ = page == Page::kCode ? parts.field.get() : nullptr;
    message_label_ = parts.message;
    primary_button_ = parts.button;

    views::View* const old_page = current_page_;
    current_page_ = AddChildView(std::move(contents));
    current_page_->SetPaintToLayer();
    current_page_->layer()->SetFillsBoundsOpaquely(false);

    PreferredSizeChanged();
    if (views::Widget* widget = GetWidget()) {
      if (views::BubbleDialogDelegate* bubble =
              widget->widget_delegate()->AsBubbleDialogDelegate()) {
        bubble->SizeToContents();
      }
    }
    DeprecatedLayoutImmediately();
    FocusFirstField();

    if (!old_page) {
      return;
    }
    // Removal always waits for a fresh task: a page usually changes from a
    // click on one of the outgoing page's own buttons, which must not be
    // freed under it.
    old_page->SetCanProcessEventsWithinSubtree(false);
    if (slide == Slide::kNone || width() == 0) {
      old_page->SetVisible(false);
      PostRemoveOutgoingPages();
      return;
    }

    const int offset = slide == Slide::kForward ? width() : -width();
    gfx::Transform incoming_start;
    incoming_start.Translate(offset, 0);
    current_page_->layer()->SetTransform(incoming_start);
    gfx::Transform outgoing_end;
    outgoing_end.Translate(-offset, 0);

    views::AnimationBuilder()
        .OnEnded(base::BindOnce(&AccountMenuView::PostRemoveOutgoingPages,
                                weak_factory_.GetWeakPtr()))
        .OnAborted(base::BindOnce(&AccountMenuView::PostRemoveOutgoingPages,
                                  weak_factory_.GetWeakPtr()))
        .Once()
        .SetDuration(kSlideDuration)
        .SetTransform(current_page_, gfx::Transform(),
                      gfx::Tween::FAST_OUT_SLOW_IN)
        .SetTransform(old_page, outgoing_end, gfx::Tween::FAST_OUT_SLOW_IN);
  }

  void PostRemoveOutgoingPages() {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&AccountMenuView::RemoveOutgoingPages,
                                  weak_factory_.GetWeakPtr()));
  }

  void RemoveOutgoingPages() {
    std::vector<views::View*> outgoing;
    for (views::View* page : children()) {
      if (page != current_page_) {
        outgoing.push_back(page);
      }
    }
    for (views::View* page : outgoing) {
      RemoveChildViewT(page);
    }
  }

  void FocusFirstField() {
    if (email_field_) {
      email_field_->RequestFocus();
    } else if (code_field_) {
      code_field_->RequestFocus();
    }
  }

  // A row that closes the menu before acting.  Switching Spaces rebuilds the
  // spaces bar, avatar and all, so the action must not depend on anything the
  // menu owns.
  views::Button::PressedCallback CloseThen(base::RepeatingClosure action) {
    return base::BindRepeating(
        [](base::WeakPtr<AccountMenuView> menu, base::RepeatingClosure action) {
          if (menu && menu->GetWidget()) {
            menu->GetWidget()->Close();
          }
          action.Run();
        },
        weak_factory_.GetWeakPtr(), std::move(action));
  }

  base::RepeatingClosure OpenUrl(const char* url) {
    return base::BindRepeating(
        [](BrowserWindowInterface* browser, const GURL& url) {
          ShowSingletonTab(browser, url);
        },
        browser_.get(), GURL(url));
  }

  // ── The menu ──

  void ShowMenuPage(Slide slide) {
    auto page = std::make_unique<views::View>();
    auto* layout = page->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    const std::u16string name = DisplayNameFor(profile_);
    const std::string email = SignedInEmail(profile_);
    const bool signed_in = !email.empty();
    signed_in_shown_ = signed_in;
    views::Label* sync_line = nullptr;

    // The person.  Signed in: who, and the account it is.  Signed out: no
    // name or face at all, and what signing in is for.
    {
      auto header = std::make_unique<views::View>();
      auto* header_layout =
          header->SetLayoutManager(std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal,
              gfx::Insets::TLBR(4, 12, 8, 12), 10));
      header_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      header->AddChildView(std::make_unique<AvatarView>(PhotoFor(profile_),
                                                        InitialsFor(name)));

      const std::u16string subtitle =
          !signed_in ? u"Sign in to share your tabs across devices"
          : base::UTF8ToUTF16(email) != name ? base::UTF8ToUTF16(email)
                                             : std::u16string();
      auto text = std::make_unique<views::View>();
      text->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
      text->AddChildView(MakeLabel(signed_in ? name : u"Not signed in", 14,
                                   gfx::Font::Weight::MEDIUM, kRowText));
      if (!subtitle.empty()) {
        text->AddChildView(MakeLabel(subtitle, 12, gfx::Font::Weight::NORMAL,
                                     kRowSubText));
      }
      // Signed in, whether the account's shared tabs are in sync -- the only
      // sign that signing in did anything.
      if (signed_in) {
        sync_line = text->AddChildView(MakeLabel(
            std::u16string(), 11, gfx::Font::Weight::NORMAL, kRowSubText));
        sync_line->SetMultiLine(true);
      }
      header_layout->SetFlexForView(header->AddChildView(std::move(text)), 1);
      page->AddChildView(std::move(header));
    }

    if (signed_in) {
      // Signing out, and how the sync is doing, stay on the settings page.
      page->AddChildView(std::make_unique<MenuRow>(
          CloseThen(OpenUrl(kAccountSettingsUrl)),
          RowIcon("user", kRowIconColor), u"Avora Account", std::u16string(),
          /*checked=*/false));
    } else {
      page->AddChildView(std::make_unique<MenuRow>(
          base::BindRepeating(&AccountMenuView::StartSignIn,
                              base::Unretained(this)),
          RowIcon("user", kRowIconColor), u"Sign in to Avora…",
          std::u16string(), /*checked=*/false));
    }

    // The identities they browse as: one per Space.
    if (profile_ && profile_->GetPrefs()) {
      page->AddChildView(MakeSeparator());
      page->AddChildView(MakeSectionLabel(u"Identities"));

      const SpaceManager space_manager(profile_->GetPrefs());
      const BrowserProfileStore profile_store(profile_->GetPrefs());
      const std::string active_space_id =
          window_space_state_ ? window_space_state_->active_space_id()
                              : std::string();
      for (const Space& space : space_manager.GetSpaces()) {
        const BrowserProfile* identity =
            profile_store.GetProfileById(space.profile_id);
        const bool isolated = identity && !UsesDefaultPartition(*identity);
        page->AddChildView(std::make_unique<MenuRow>(
            CloseThen(base::BindRepeating(
                [](WindowSpaceState* state, const std::string& space_id) {
                  if (state) {
                    state->SetActiveSpaceId(space_id);
                  }
                },
                window_space_state_.get(), space.id)),
            RowIcon(space.icon, space.AccentColor()),
            base::UTF8ToUTF16(space.name),
            isolated ? u"Signed in separately"
                     : u"Shares the default browser session",
            space.id == active_space_id));
      }
    }

    page->AddChildView(MakeSeparator());
    page->AddChildView(std::make_unique<MenuRow>(
        CloseThen(OpenUrl(kSettingsUrl)), RowIcon("settings", kRowIconColor),
        u"Avora Settings", std::u16string(), /*checked=*/false));
    page->AddChildView(std::make_unique<MenuRow>(
        CloseThen(OpenUrl(kExtensionsUrl)), RowIcon("puzzle", kRowIconColor),
        u"Manage Extensions", std::u16string(), /*checked=*/false));

    ShowPage(Page::kMenu, std::move(page), slide);
    sync_line_ = sync_line;
    UpdateSyncLine();
  }

  void UpdateSyncLine() {
    sync::TabSyncService* service = Service();
    if (!sync_line_ || !service) {
      return;
    }
    const auto [text, color] = SyncLine(service->GetStatus());
    if (sync_line_->GetText() == text) {
      return;
    }
    sync_line_->SetText(text);
    sync_line_->SetEnabledColor(color);
    sync_line_->SizeToFit(kMenuWidth - 24 - kHeaderAvatarSize - 10);
    PreferredSizeChanged();
    if (views::Widget* widget = GetWidget()) {
      if (views::BubbleDialogDelegate* bubble =
              widget->widget_delegate()->AsBubbleDialogDelegate()) {
        bubble->SizeToContents();
      }
    }
  }

  void StartSignIn() {
    // A code already on its way -- asked for here or on the settings page --
    // goes straight to entering it.
    sync::TabSyncService* service = Service();
    if (service &&
        service->state() == sync::TabSyncService::State::kAwaitingCode) {
      ShowCodePage(Slide::kForward);
    } else {
      ShowEmailPage(Slide::kForward);
    }
  }

  // ── Sign-in steps ──

  // A sign-in page: a back chevron and title, a line of explanation, one
  // field, a message line for what went wrong, and the button that submits.
  std::unique_ptr<views::View> BuildStepPage(
      const std::u16string& title,
      const std::u16string& explanation,
      base::RepeatingClosure on_back,
      std::unique_ptr<views::Textfield> field,
      const std::u16string& submit_text,
      StepParts* parts) {
    auto page = std::make_unique<views::View>();
    auto* layout = page->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(0, 12, 6, 12), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto header = std::make_unique<views::View>();
    auto* header_layout =
        header->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::TLBR(0, -6, 0, 0), 4));
    header_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    header->AddChildView(std::make_unique<BackButton>(std::move(on_back)));
    header->AddChildView(
        MakeLabel(title, 14, gfx::Font::Weight::MEDIUM, kRowText));
    page->AddChildView(std::move(header));

    auto explanation_label =
        MakeLabel(explanation, 12, gfx::Font::Weight::NORMAL, kRowSubText);
    explanation_label->SetMultiLine(true);
    explanation_label->SizeToFit(kMenuWidth - 24);
    page->AddChildView(std::move(explanation_label));

    field->SetFontList(SystemFont(13, gfx::Font::Weight::NORMAL));
    field->SetColor(kRowText);
    field->SetBackgroundColor(kFieldBg);
    field->SetBorder(views::CreatePaddedBorder(
        views::CreateRoundedRectBorder(1, 6, kFieldBorder),
        gfx::Insets::VH(6, 8)));
    field->SetPreferredSize(gfx::Size(kMenuWidth - 24, kFieldHeight));
    field->set_controller(this);
    parts->field = page->AddChildView(std::move(field));

    auto message = MakeLabel(std::u16string(), 12, gfx::Font::Weight::NORMAL,
                             kErrorText);
    message->SetMultiLine(true);
    message->SetVisible(false);
    parts->message = page->AddChildView(std::move(message));

    parts->button = page->AddChildView(std::make_unique<PrimaryButton>(
        base::BindRepeating(&AccountMenuView::Submit, base::Unretained(this)),
        submit_text));
    return page;
  }

  void ShowEmailPage(Slide slide) {
    auto field = std::make_unique<views::Textfield>();
    field->SetTextInputType(ui::TEXT_INPUT_TYPE_EMAIL);
    field->SetPlaceholderText(u"you@example.com");
    field->GetViewAccessibility().SetName(u"Email");
    if (sync::TabSyncService* service = Service()) {
      field->SetText(base::UTF8ToUTF16(service->GetStatus().email));
    }

    StepParts parts;
    std::unique_ptr<views::View> page = BuildStepPage(
        u"Sign in to Avora",
        u"Avora emails you a one-time code. Signing in shares your tabs with "
        u"your phone and your other Macs.",
        base::BindRepeating(&AccountMenuView::ShowMenuPage,
                            base::Unretained(this), Slide::kBack),
        std::move(field), u"Send code", &parts);
    ShowPage(Page::kEmail, std::move(page), slide, parts);
  }

  void ShowCodePage(Slide slide) {
    sync::TabSyncService* service = Service();
    const std::string email = service ? service->GetStatus().email : "";

    StepParts parts;
    auto field = std::make_unique<views::Textfield>();
    field->SetTextInputType(ui::TEXT_INPUT_TYPE_NUMBER);
    field->SetPlaceholderText(u"123456");
    field->GetViewAccessibility().SetName(u"Code");

    std::unique_ptr<views::View> page = BuildStepPage(
        u"Enter the code",
        u"We emailed a code to " + base::UTF8ToUTF16(email) +
            u". It can take a minute to arrive.",
        base::BindRepeating(&AccountMenuView::BackToEmail,
                            base::Unretained(this)),
        std::move(field), u"Sign in", &parts);

    auto resend = std::make_unique<views::Link>(u"Send a new code");
    resend->SetCallback(base::BindRepeating(
        [](AccountMenuView* menu, const ui::Event&) { menu->ResendCode(); },
        base::Unretained(this)));
    resend->SetFontList(SystemFont(12, gfx::Font::Weight::NORMAL));
    resend->SetEnabledColor(kLinkText);
    resend->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    page->AddChildView(std::move(resend));

    ShowPage(Page::kCode, std::move(page), slide, parts);
  }

  void BackToEmail() {
    if (sync::TabSyncService* service = Service()) {
      service->CancelSignIn();
    }
    ShowEmailPage(Slide::kBack);
  }

  void Submit() {
    sync::TabSyncService* service = Service();
    if (!service || waiting_) {
      return;
    }
    if (page_ == Page::kEmail && email_field_) {
      const std::string email = base::UTF16ToUTF8(
          base::TrimWhitespace(email_field_->GetText(), base::TRIM_ALL));
      if (email.empty()) {
        ShowMessage(MessageFor(sync::TabSyncService::Result::kInvalidEmail, ""),
                    /*is_error=*/true);
        return;
      }
      SetWaiting(true, u"Sending…");
      service->RequestCode(email);
    } else if (page_ == Page::kCode && code_field_) {
      const std::string code = base::UTF16ToUTF8(
          base::TrimWhitespace(code_field_->GetText(), base::TRIM_ALL));
      if (code.empty()) {
        ShowMessage(MessageFor(sync::TabSyncService::Result::kInvalidCode, ""),
                    /*is_error=*/true);
        return;
      }
      SetWaiting(true, u"Signing in…");
      service->VerifyCode(code);
    }
  }

  void ResendCode() {
    sync::TabSyncService* service = Service();
    if (!service || waiting_) {
      return;
    }
    SetWaiting(true, u"Sending\u2026");
    service->RequestCode(service->GetStatus().email);
  }

  // Disables the page's button while a request is out, and says so on it.
  // The service answers through OnTabSyncStatusChanged().
  void SetWaiting(bool waiting, const std::u16string& button_text) {
    waiting_ = waiting;
    if (waiting) {
      ShowMessage(std::u16string(), /*is_error=*/false);
    }
    if (primary_button_) {
      primary_button_->SetEnabled(!waiting);
      primary_button_->SetText(button_text);
    }
  }

  void ShowMessage(const std::u16string& message, bool is_error) {
    if (!message_label_) {
      return;
    }
    message_label_->SetText(message);
    message_label_->SetEnabledColor(is_error ? kErrorText : kRowSubText);
    message_label_->SizeToFit(kMenuWidth - 24);
    message_label_->SetVisible(!message.empty());
    PreferredSizeChanged();
    if (views::Widget* widget = GetWidget()) {
      if (views::BubbleDialogDelegate* bubble =
              widget->widget_delegate()->AsBubbleDialogDelegate()) {
        bubble->SizeToContents();
      }
    }
  }

  raw_ptr<Profile> profile_;
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<WindowSpaceState> window_space_state_;

  Page page_ = Page::kMenu;
  raw_ptr<views::View> current_page_ = nullptr;
  bool signed_in_shown_ = false;

  // A request is out to the service; its answer moves the page on, or puts
  // a message under it.
  bool waiting_ = false;

  // The menu page's line on how the tab sync is doing.  Null when signed out
  // and on the sign-in pages.
  raw_ptr<views::Label> sync_line_ = nullptr;
  base::RepeatingTimer sync_line_timer_;

  // The current sign-in page's parts.  Null on the menu page.
  raw_ptr<views::Textfield> email_field_ = nullptr;
  raw_ptr<views::Textfield> code_field_ = nullptr;
  raw_ptr<views::Label> message_label_ = nullptr;
  raw_ptr<views::LabelButton> primary_button_ = nullptr;

  base::ScopedObservation<sync::TabSyncService,
                          sync::TabSyncService::Observer>
      sync_observation_{this};

  base::WeakPtrFactory<AccountMenuView> weak_factory_{this};
};

BEGIN_METADATA(AccountMenuView)
END_METADATA

}  // namespace

AccountButton::AccountButton(Profile* profile,
                             BrowserWindowInterface* browser,
                             WindowSpaceState* window_space_state)
    : views::Button(base::BindRepeating(&AccountButton::ShowMenu,
                                        base::Unretained(this))),
      profile_(profile),
      browser_(browser),
      window_space_state_(window_space_state) {
  SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);

  if (sync::TabSyncService* service =
          sync::TabSyncService::GetForProfile(profile_)) {
    sync_observation_.Observe(service);
  }
  local_user_subscription_ = GetLocalUserCache().on_changed.Add(
      base::BindRepeating(&AccountButton::OnLocalUserChanged,
                          base::Unretained(this)));
  EnsureLocalUserRequested();
  UpdateTooltipAndName();
}

AccountButton::~AccountButton() {
  // The menu is anchored here and should go with it.  Close(), not
  // CloseNow(): picking a Space in the menu is what destroys this button, from
  // inside the menu row's own click, and tearing the menu down synchronously
  // would free that row mid-click.
  if (menu_widget_) {
    menu_widget_->Close();
  }
}

// static
void AccountButton::SetLocalUserForTesting(const LocalUser& user) {
  LocalUserCache& cache = GetLocalUserCache();
  cache.requested = true;
  cache.overridden_for_testing = true;
  StoreLocalUser(user);
}

std::u16string AccountButton::InitialsForTesting() const {
  return InitialsFor(DisplayName());
}

void AccountButton::PaintButtonContents(gfx::Canvas* canvas) {
  if (GetState() == views::Button::STATE_HOVERED ||
      GetState() == views::Button::STATE_PRESSED) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(kButtonHoverBg);
    canvas->DrawRoundRect(GetLocalBounds(), kButtonCornerRadius, flags);
  }
  gfx::Rect avatar(GetLocalBounds());
  avatar.ClampToCenteredSize(gfx::Size(kAvatarSize, kAvatarSize));
  PaintAvatar(canvas, avatar, Photo(), InitialsFor(DisplayName()));
}

void AccountButton::OnTabSyncStatusChanged() {
  // Signing in or out is what puts the person's face on the avatar, or takes
  // it off.
  UpdateTooltipAndName();
  SchedulePaint();
}

void AccountButton::OnLocalUserChanged() {
  UpdateTooltipAndName();
  SchedulePaint();
}

std::u16string AccountButton::DisplayName() const {
  return DisplayNameFor(profile_);
}

gfx::ImageSkia AccountButton::Photo() const {
  return PhotoFor(profile_);
}

std::string AccountButton::AvoraEmail() const {
  return SignedInEmail(profile_);
}

void AccountButton::UpdateTooltipAndName() {
  const std::u16string name = DisplayName();
  const std::string email = AvoraEmail();
  std::u16string tooltip = name.empty() ? u"Not signed in to Avora" : name;
  if (!email.empty() && base::UTF8ToUTF16(email) != name) {
    tooltip += u"\n" + base::UTF8ToUTF16(email);
  }
  SetTooltipText(tooltip);
  GetViewAccessibility().SetName(u"Account and settings");
}

void AccountButton::ShowMenu() {
  // A second click on the avatar dismisses the menu it opened.
  if (menu_widget_) {
    menu_widget_->Close();
    return;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      this, views::BubbleBorder::BOTTOM_LEFT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets::VH(8, 0));
  delegate->SetTitle(u"");
  delegate->SetShowTitle(false);
  delegate->SetShowCloseButton(false);
  delegate->SetBackgroundColor(kMenuBg);
  delegate->SetContentsView(std::make_unique<AccountMenuView>(
      profile_, browser_, window_space_state_));

  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  menu_widget_ = widget->GetWeakPtr();
  widget->Show();
}

BEGIN_METADATA(AccountButton)
END_METADATA

}  // namespace avora
