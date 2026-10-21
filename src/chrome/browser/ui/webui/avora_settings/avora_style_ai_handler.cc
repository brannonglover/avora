// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/webui/avora_settings/avora_style_ai_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/avora/style_ai/avora_style_ai_status.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"

namespace {

AvoraStyleAiHandler::KeyDialogLauncher& Launcher() {
  static base::NoDestructor<AvoraStyleAiHandler::KeyDialogLauncher> launcher;
  return *launcher;
}

}  // namespace

// static
void AvoraStyleAiHandler::SetKeyDialogLauncher(KeyDialogLauncher launcher) {
  Launcher() = std::move(launcher);
}

AvoraStyleAiHandler::AvoraStyleAiHandler(Profile* profile)
    : profile_(profile),
      settings_(avora::StyleAiSettings::GetForProfile(profile)) {}

AvoraStyleAiHandler::~AvoraStyleAiHandler() = default;

void AvoraStyleAiHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getStyleAiStatus",
      base::BindRepeating(&AvoraStyleAiHandler::HandleGetStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openStyleAiKeyDialog",
      base::BindRepeating(&AvoraStyleAiHandler::HandleOpenKeyDialog,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "removeStyleAiKey",
      base::BindRepeating(&AvoraStyleAiHandler::HandleRemoveKey,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setStyleAiModel",
      base::BindRepeating(&AvoraStyleAiHandler::HandleSetModel,
                          base::Unretained(this)));
}

void AvoraStyleAiHandler::OnJavascriptAllowed() {
  if (settings_ && !observation_.IsObserving()) {
    observation_.Observe(settings_.get());
  }
}

void AvoraStyleAiHandler::OnJavascriptDisallowed() {
  observation_.Reset();
  weak_factory_.InvalidateWeakPtrs();
}

void AvoraStyleAiHandler::OnStyleAiSettingsChanged() {
  SendStatus();
}

void AvoraStyleAiHandler::HandleGetStatus(const base::ListValue& args) {
  AllowJavascript();
  SendStatus();
}

void AvoraStyleAiHandler::HandleOpenKeyDialog(const base::ListValue& args) {
  AllowJavascript();
  if (!settings_ || !Launcher()) {
    return;
  }
  const bool replacing = settings_->key_store().GetKeyState() !=
                         avora::StyleAiKeyStore::KeyState::kAbsent;
  Launcher().Run(web_ui()->GetWebContents(), replacing,
                 base::BindOnce(&AvoraStyleAiHandler::OnKeySubmitted,
                                weak_factory_.GetWeakPtr()));
}

void AvoraStyleAiHandler::HandleRemoveKey(const base::ListValue& args) {
  AllowJavascript();
  if (!settings_) {
    return;
  }
  last_result_ = "removed";
  settings_->RemoveKey();
}

void AvoraStyleAiHandler::HandleSetModel(const base::ListValue& args) {
  AllowJavascript();
  if (!settings_ || args.size() != 1 || !args[0].is_string()) {
    return;
  }
  last_result_ = settings_->SetModel(args[0].GetString()) ? "model-saved"
                                                          : "model-rejected";
  SendStatus();
}

void AvoraStyleAiHandler::OnKeySubmitted(std::string key) {
  if (!settings_) {
    return;
  }
  // SaveKey also tells every open Settings page; this one then gets the
  // status again with the result of its own save.
  last_result_ = avora::SaveResultName(settings_->SaveKey(key));
  key.assign(key.size(), '\0');
  SendStatus();
}

void AvoraStyleAiHandler::SendStatus() {
  if (!IsJavascriptAllowed()) {
    return;
  }
  CallJavascriptFunction(
      "avoraSettings.onStyleAiStatus",
      base::Value(avora::StyleAiStatusForSettings(
          settings_ ? &settings_->key_store() : nullptr, last_result_)));
}
