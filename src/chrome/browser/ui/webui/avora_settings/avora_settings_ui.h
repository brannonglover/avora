// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SETTINGS_UI_H_
#define CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SETTINGS_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

class AvoraSettingsUI;

class AvoraSettingsUIConfig
    : public content::DefaultWebUIConfig<AvoraSettingsUI> {
 public:
  AvoraSettingsUIConfig();
};

class AvoraSettingsUI : public content::WebUIController {
 public:
  explicit AvoraSettingsUI(content::WebUI* web_ui);

  AvoraSettingsUI(const AvoraSettingsUI&) = delete;
  AvoraSettingsUI& operator=(const AvoraSettingsUI&) = delete;

  ~AvoraSettingsUI() override;
};

#endif  // CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SETTINGS_UI_H_
