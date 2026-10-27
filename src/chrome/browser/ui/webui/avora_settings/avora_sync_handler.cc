// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/webui/avora_settings/avora_sync_handler.h"

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"

namespace {

using avora::sync::TabSyncService;

const char* StateName(TabSyncService::State state) {
  switch (state) {
    case TabSyncService::State::kSignedOut:
      return "signed-out";
    case TabSyncService::State::kAwaitingCode:
      return "awaiting-code";
    case TabSyncService::State::kSignedIn:
      return "signed-in";
  }
}

const char* ResultName(TabSyncService::Result result) {
  switch (result) {
    case TabSyncService::Result::kNone:
      return "";
    case TabSyncService::Result::kCodeSent:
      return "code-sent";
    case TabSyncService::Result::kSignedIn:
      return "signed-in";
    case TabSyncService::Result::kSignedOut:
      return "signed-out";
    case TabSyncService::Result::kInvalidEmail:
      return "invalid-email";
    case TabSyncService::Result::kInvalidCode:
      return "invalid-code";
    case TabSyncService::Result::kWrongCode:
      return "wrong-code";
    case TabSyncService::Result::kRateLimited:
      return "rate-limited";
    case TabSyncService::Result::kNetworkError:
      return "network-error";
    case TabSyncService::Result::kSessionExpired:
      return "session-expired";
    case TabSyncService::Result::kSignedOutDeviceNotRemoved:
      return "signed-out-device-not-removed";
  }
}

}  // namespace

AvoraSyncHandler::AvoraSyncHandler(Profile* profile)
    : service_(TabSyncService::GetForProfile(profile)) {}

AvoraSyncHandler::~AvoraSyncHandler() = default;

// static
base::DictValue AvoraSyncHandler::StatusForPage(
    const TabSyncService* service) {
  base::DictValue dict;
  dict.Set("available", service != nullptr);
  if (!service) {
    return dict;
  }
  const TabSyncService::Status status = service->GetStatus();
  dict.Set("state", StateName(status.state));
  dict.Set("starting", status.starting);
  dict.Set("busy", status.busy);
  dict.Set("email", status.email);
  dict.Set("deviceName", status.device_name);
  dict.Set("result", ResultName(status.last_result));
  dict.Set("memoryOnly", status.memory_only);
  if (!status.last_upload.is_null()) {
    dict.Set("lastUpload", status.last_upload.InMillisecondsFSinceUnixEpoch());
  }
  dict.Set("tabCount", static_cast<int>(status.uploaded_tab_count));
  dict.Set("uploadFailing", status.upload_failing);
  return dict;
}

void AvoraSyncHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getSyncStatus", base::BindRepeating(&AvoraSyncHandler::HandleGetStatus,
                                           base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "sendSyncCode", base::BindRepeating(&AvoraSyncHandler::HandleSendCode,
                                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "verifySyncCode",
      base::BindRepeating(&AvoraSyncHandler::HandleVerifyCode,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelSyncSignIn",
      base::BindRepeating(&AvoraSyncHandler::HandleCancelSignIn,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "signOutSync", base::BindRepeating(&AvoraSyncHandler::HandleSignOut,
                                         base::Unretained(this)));
}

void AvoraSyncHandler::OnJavascriptAllowed() {
  if (service_ && !observation_.IsObserving()) {
    observation_.Observe(service_.get());
  }
}

void AvoraSyncHandler::OnJavascriptDisallowed() {
  observation_.Reset();
}

void AvoraSyncHandler::OnTabSyncStatusChanged() {
  SendStatus();
}

void AvoraSyncHandler::HandleGetStatus(const base::ListValue& args) {
  AllowJavascript();
  SendStatus();
}

void AvoraSyncHandler::HandleSendCode(const base::ListValue& args) {
  AllowJavascript();
  if (!service_ || args.size() != 1 || !args[0].is_string()) {
    return;
  }
  service_->RequestCode(args[0].GetString());
}

void AvoraSyncHandler::HandleVerifyCode(const base::ListValue& args) {
  AllowJavascript();
  if (!service_ || args.size() != 1 || !args[0].is_string()) {
    return;
  }
  service_->VerifyCode(args[0].GetString());
}

void AvoraSyncHandler::HandleCancelSignIn(const base::ListValue& args) {
  AllowJavascript();
  if (service_) {
    service_->CancelSignIn();
  }
}

void AvoraSyncHandler::HandleSignOut(const base::ListValue& args) {
  AllowJavascript();
  if (service_) {
    service_->SignOut();
  }
}

void AvoraSyncHandler::SendStatus() {
  if (!IsJavascriptAllowed()) {
    return;
  }
  CallJavascriptFunction("avoraSettings.onSyncStatus",
                         base::Value(StatusForPage(service_.get())));
}
