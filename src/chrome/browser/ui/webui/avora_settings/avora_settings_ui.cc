// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/avora_settings/avora_settings_ui.h"

#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/web_ui.h"

AvoraSettingsUIConfig::AvoraSettingsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, "avora-settings") {}

AvoraSettingsUI::AvoraSettingsUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {}

AvoraSettingsUI::~AvoraSettingsUI() = default;
