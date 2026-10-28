// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_ACCOUNT_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_ACCOUNT_BUTTON_H_

#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/avora/sync/avora_tab_sync_service.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/button/button.h"

class BrowserWindowInterface;
class GURL;
class Profile;

namespace views {
class Widget;
}

namespace avora {

struct LocalUser;
class WindowSpaceState;

// The person using Avora, at the leading edge of the spaces bar.
//
// One avatar for the person, above every identity they browse as.  Signed in
// to Avora, it is the Mac account's picture, or the person's initials when
// there is none; signed out, a plain person glyph, since there is no one to
// show yet.
//
// Clicking it opens the account menu -- who is signed in to Avora, each
// Space's identity under them, and the way into Settings -- which is what
// makes Settings reachable without hunting for the Manage button that only
// appears while hovering the address chip.  Signing in to Avora happens in
// that menu too, without leaving the page.
class AccountButton : public views::Button,
                      public sync::TabSyncService::Observer {
  METADATA_HEADER(AccountButton, views::Button)

 public:
  AccountButton(Profile* profile,
                BrowserWindowInterface* browser,
                WindowSpaceState* window_space_state);
  AccountButton(const AccountButton&) = delete;
  AccountButton& operator=(const AccountButton&) = delete;
  ~AccountButton() override;

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override;

  // sync::TabSyncService::Observer:
  void OnTabSyncStatusChanged() override;

  // The open account menu, or null.
  views::Widget* menu_widget_for_testing() const { return menu_widget_.get(); }

  // Stands in for the OS account in every AccountButton, so tests do not
  // depend on the machine's own name and picture.
  static void SetLocalUserForTesting(const LocalUser& user);

  // The initials the avatar falls back to when there is no picture.
  std::u16string InitialsForTesting() const;

 private:
  void ShowMenu();

  // Rebuilds what the avatar shows once the OS account has been read.
  void OnLocalUserChanged();

  // The signed-in person's name: the Mac account's full name, else the Avora
  // email.  Empty while signed out.
  std::u16string DisplayName() const;

  // The Mac account's picture while signed in to Avora, else null.
  gfx::ImageSkia Photo() const;

  // The signed-in Avora email, or empty.
  std::string AvoraEmail() const;

  void UpdateTooltipAndName();

  raw_ptr<Profile> profile_;
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<WindowSpaceState> window_space_state_;

  base::ScopedObservation<sync::TabSyncService,
                          sync::TabSyncService::Observer>
      sync_observation_{this};
  base::CallbackListSubscription local_user_subscription_;

  base::WeakPtr<views::Widget> menu_widget_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_ACCOUNT_BUTTON_H_
