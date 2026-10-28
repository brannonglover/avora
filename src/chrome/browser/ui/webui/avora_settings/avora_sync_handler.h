// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SYNC_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SYNC_HANDLER_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "chrome/browser/avora/sync/avora_tab_sync_service.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

// The Account section of chrome://avora-settings: sign in with an emailed
// one-time code, see who is signed in and when this Mac's tabs last reached
// the account, and sign out. The page sends the email and the code; it never
// receives a token -- those stay in TabSyncService and its encrypted store.
class AvoraSyncHandler : public content::WebUIMessageHandler,
                         public avora::sync::TabSyncService::Observer {
 public:
  explicit AvoraSyncHandler(Profile* profile);
  AvoraSyncHandler(const AvoraSyncHandler&) = delete;
  AvoraSyncHandler& operator=(const AvoraSyncHandler&) = delete;
  ~AvoraSyncHandler() override;

  // The status object the page renders. Public for tests.
  static base::DictValue StatusForPage(
      const avora::sync::TabSyncService* service);

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // avora::sync::TabSyncService::Observer:
  void OnTabSyncStatusChanged() override;

 private:
  void HandleGetStatus(const base::ListValue& args);
  void HandleSendCode(const base::ListValue& args);
  void HandleVerifyCode(const base::ListValue& args);
  void HandleCancelSignIn(const base::ListValue& args);
  void HandleSignOut(const base::ListValue& args);
  void HandleSetPreviews(const base::ListValue& args);

  void SendStatus();

  // Null for off-the-record profiles.
  raw_ptr<avora::sync::TabSyncService> service_;
  base::ScopedObservation<avora::sync::TabSyncService,
                          avora::sync::TabSyncService::Observer>
      observation_{this};
};

#endif  // CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SYNC_HANDLER_H_
