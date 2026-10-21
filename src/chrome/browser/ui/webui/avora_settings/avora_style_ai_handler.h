// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_STYLE_AI_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_STYLE_AI_HANDLER_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "chrome/browser/avora/style_ai/avora_style_ai_settings.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace content {
class WebContents;
}

// The Style Studio AI section of chrome://avora-settings. The page can ask for
// status, open the native key dialog, remove the key, and choose the model. It
// never receives the key: the key is typed into a native dialog in the browser
// process and goes straight to StyleAiSettings.
class AvoraStyleAiHandler : public content::WebUIMessageHandler,
                            public avora::StyleAiSettings::Observer {
 public:
  // Shows the native key dialog (avora::ShowStyleAiKeyDialog). Supplied from
  // //chrome/browser/ui/views, which this target cannot depend on.
  using KeyDialogLauncher = base::RepeatingCallback<void(
      content::WebContents* web_contents,
      bool replacing,
      base::OnceCallback<void(std::string key)> on_submit)>;
  static void SetKeyDialogLauncher(KeyDialogLauncher launcher);

  explicit AvoraStyleAiHandler(Profile* profile);
  AvoraStyleAiHandler(const AvoraStyleAiHandler&) = delete;
  AvoraStyleAiHandler& operator=(const AvoraStyleAiHandler&) = delete;
  ~AvoraStyleAiHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // avora::StyleAiSettings::Observer:
  void OnStyleAiSettingsChanged() override;

 private:
  void HandleGetStatus(const base::ListValue& args);
  void HandleOpenKeyDialog(const base::ListValue& args);
  void HandleRemoveKey(const base::ListValue& args);
  void HandleSetModel(const base::ListValue& args);

  void OnKeySubmitted(std::string key);
  void SendStatus();

  raw_ptr<Profile> profile_;
  // Null for off-the-record profiles.
  raw_ptr<avora::StyleAiSettings> settings_;
  std::optional<std::string> last_result_;
  base::ScopedObservation<avora::StyleAiSettings,
                          avora::StyleAiSettings::Observer>
      observation_{this};
  base::WeakPtrFactory<AvoraStyleAiHandler> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_STYLE_AI_HANDLER_H_
