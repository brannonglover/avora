// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/webui/avora_settings/avora_telemetry_handler.h"

#include "base/functional/bind.h"
#include "chrome/browser/avora/avora_telemetry.h"
#include "content/public/browser/web_ui.h"

AvoraTelemetryHandler::AvoraTelemetryHandler() = default;

AvoraTelemetryHandler::~AvoraTelemetryHandler() = default;

void AvoraTelemetryHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getTelemetryStatus",
      base::BindRepeating(&AvoraTelemetryHandler::HandleGetStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setTelemetryEnabled",
      base::BindRepeating(&AvoraTelemetryHandler::HandleSetEnabled,
                          base::Unretained(this)));
}

void AvoraTelemetryHandler::HandleGetStatus(const base::ListValue& args) {
  AllowJavascript();
  SendStatus();
}

void AvoraTelemetryHandler::HandleSetEnabled(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() == 1 && args[0].is_bool()) {
    avora::SetTelemetryEnabled(args[0].GetBool());
  }
  SendStatus();
}

void AvoraTelemetryHandler::SendStatus() {
  base::DictValue status;
  status.Set("enabled", avora::IsTelemetryEnabled());
  CallJavascriptFunction("avoraSettings.onTelemetryStatus", status);
}
