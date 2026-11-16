// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_TELEMETRY_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_TELEMETRY_HANDLER_H_

#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"

// The Usage statistics switch on chrome://avora-settings.  The setting is
// browser-wide (Local State), so every profile's page shows the same one.
class AvoraTelemetryHandler : public content::WebUIMessageHandler {
 public:
  AvoraTelemetryHandler();
  AvoraTelemetryHandler(const AvoraTelemetryHandler&) = delete;
  AvoraTelemetryHandler& operator=(const AvoraTelemetryHandler&) = delete;
  ~AvoraTelemetryHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleGetStatus(const base::ListValue& args);
  void HandleSetEnabled(const base::ListValue& args);
  void SendStatus();
};

#endif  // CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_TELEMETRY_HANDLER_H_
