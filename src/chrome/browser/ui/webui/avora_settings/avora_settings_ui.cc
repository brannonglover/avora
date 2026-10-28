// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/avora_settings/avora_settings_ui.h"

#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/avora_settings/avora_settings_page.h"
#include "chrome/browser/ui/webui/avora_settings/avora_style_ai_handler.h"
#include "chrome/browser/ui/webui/avora_settings/avora_sync_handler.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

namespace {

constexpr char kHost[] = "avora-settings";

// The request path arrives with the URL's query and fragment still on it
// (URLDataSource::URLToRequestPath), so chrome://avora-settings/#account asks
// for "#account".  Neither names a file: the fragment is for the page's own
// scrolling, and the page takes no query.
std::string_view FilePath(std::string_view path) {
  return path.substr(0, path.find_first_of("?#"));
}

bool ShouldServePage(const std::string& path) {
  const std::string_view file = FilePath(path);
  return file.empty() || file == "index.html" || file == "avora_settings.js";
}

void ServePage(const std::string& path,
               content::WebUIDataSource::GotDataCallback callback) {
  std::string body = FilePath(path) == "avora_settings.js"
                         ? std::string(avora_settings::kSettingsJs)
                         : std::string(avora_settings::kIndexHtml);
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::move(body)));
}

}  // namespace

AvoraSettingsUIConfig::AvoraSettingsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kHost) {}

AvoraSettingsUI::AvoraSettingsUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, kHost);
  source->SetRequestFilter(base::BindRepeating(&ShouldServePage),
                           base::BindRepeating(&ServePage));
  web_ui->AddMessageHandler(std::make_unique<AvoraStyleAiHandler>(profile));
  web_ui->AddMessageHandler(std::make_unique<AvoraSyncHandler>(profile));
}

AvoraSettingsUI::~AvoraSettingsUI() = default;
