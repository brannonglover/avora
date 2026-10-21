// Copyright 2026 Avora. All rights reserved.

// End-to-end checks for Style Studio AI's key entry (Checkpoint 3B.1):
// chrome://avora-settings opens the native key dialog, the key typed into it
// is encrypted into the profile pref, and the page never receives it.

#include <string>

#include "base/command_line.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/avora/style_ai/avora_style_ai_key_store.h"
#include "chrome/browser/avora/style_ai/avora_style_ai_settings.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "ui/base/base_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"

namespace {

constexpr char kKey[] = "sk-ant-api03-BROWSERTESTKEYabcdefghijklmnopqrstuv12";
constexpr char kSettingsUrl[] = "chrome://avora-settings";

// Waits until the page has rendered a status, then returns it.
constexpr char kStateScript[] = R"(
  new Promise(resolve => {
    const poll = () => {
      const text = document.getElementById('style-ai-state').textContent;
      text === 'Loading…' ? setTimeout(poll, 20) : resolve(text);
    };
    poll();
  }))";

views::Textfield* FindTextfield(views::View* view) {
  if (auto* field = views::AsViewClass<views::Textfield>(view)) {
    return field;
  }
  for (views::View* child : view->children()) {
    if (views::Textfield* found = FindTextfield(child)) {
      return found;
    }
  }
  return nullptr;
}

class AvoraStyleAiSettingsBrowserTest : public InProcessBrowserTest {
 protected:
  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  avora::StyleAiSettings* settings() {
    return avora::StyleAiSettings::GetForProfile(browser()->GetProfile());
  }

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  void OpenSettings() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kSettingsUrl)));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return settings() && settings()->key_store().is_ready(); }));
  }

  std::string State() {
    return content::EvalJs(contents(), kStateScript).ExtractString();
  }

  std::string WaitForState(const std::string& needle) {
    std::string state;
    EXPECT_TRUE(base::test::RunUntil([&] {
      state = State();
      return state.find(needle) != std::string::npos;
    })) << state;
    return state;
  }

  std::string Message() {
    return content::EvalJs(contents(),
                           "document.getElementById('style-ai-message')"
                           ".textContent")
        .ExtractString();
  }

  // The open key dialog, once the page has asked for it.
  views::Widget* WaitForKeyDialog() {
    views::Widget* dialog = nullptr;
    views::Widget* browser_widget = views::Widget::GetWidgetForNativeWindow(
        browser()->GetWindow()->GetNativeWindow());
    EXPECT_TRUE(base::test::RunUntil([&] {
      views::Widget::Widgets widgets =
          views::Widget::GetAllOwnedWidgets(browser_widget->GetNativeView());
      for (views::Widget* child :
           views::Widget::GetAllChildWidgets(browser_widget->GetNativeView())) {
        widgets.insert(child);
      }
      for (views::Widget* widget : widgets) {
        if (!widget->widget_delegate()) {
          continue;
        }
        std::u16string title = widget->widget_delegate()->GetWindowTitle();
        if (title.find(u"Anthropic API key") != std::u16string::npos &&
            widget->IsVisible()) {
          dialog = widget;
          return true;
        }
      }
      return false;
    }));
    return dialog;
  }

  // Types `key` into the real dialog and presses Save.
  void EnterKeyThroughDialog(const std::string& key) {
    ASSERT_TRUE(content::ExecJs(
        contents(), "document.getElementById('style-ai-add').click()"));
    views::Widget* dialog = WaitForKeyDialog();
    ASSERT_TRUE(dialog);
    views::Textfield* field =
        FindTextfield(dialog->widget_delegate()->GetContentsView());
    ASSERT_TRUE(field);
    EXPECT_EQ(field->GetTextInputType(), ui::TEXT_INPUT_TYPE_PASSWORD);
    field->SetText(base::UTF8ToUTF16(key));
    dialog->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  }

  std::string StoredCredential() {
    std::string out;
    for (auto [name, value] :
         prefs()->GetDict(avora::StyleAiKeyStore::kCredentialPref)) {
      out += name + "=" + value.DebugString();
    }
    return out;
  }

  // Everything the page could have learned: its DOM and its script state.
  std::string PageKnowledge() {
    return content::EvalJs(contents(),
                           "document.documentElement.outerHTML + "
                           "JSON.stringify(Object.keys(window))")
        .ExtractString();
  }
};

// Browser tests run with --use-mock-keychain; the testing switch makes the
// store accept a key there, marked as an insecure test key.
class AvoraStyleAiSettingsTestingBrowserTest
    : public AvoraStyleAiSettingsBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(avora::kStyleAiTestingSwitch);
  }
};

IN_PROC_BROWSER_TEST_F(AvoraStyleAiSettingsTestingBrowserTest,
                       SavesAKeyTypedIntoTheNativeDialog) {
  OpenSettings();
  EXPECT_NE(State().find("No API key is saved"), std::string::npos);
  EnterKeyThroughDialog(kKey);
  std::string state = WaitForState("An API key is saved");
  EXPECT_NE(state.find("test key"), std::string::npos);
  EXPECT_EQ(Message(), "The key was saved.");

  // Stored encrypted, usable by the store, and marked as a test key.
  EXPECT_EQ(settings()->key_store().GetKeyForRequest(), std::string(kKey));
  EXPECT_TRUE(settings()->key_store().IsInsecureTestKey());
  const std::string stored = StoredCredential();
  EXPECT_NE(stored.find("ciphertext"), std::string::npos);
  EXPECT_EQ(stored.find(kKey), std::string::npos);
  EXPECT_EQ(stored.find("BROWSERTESTKEY"), std::string::npos);

  // The page never received the key.
  const std::string page = PageKnowledge();
  EXPECT_EQ(page.find(kKey), std::string::npos);
  EXPECT_EQ(page.find("BROWSERTESTKEY"), std::string::npos);
  EXPECT_EQ(page.find("sk-ant"), std::string::npos);
}

IN_PROC_BROWSER_TEST_F(AvoraStyleAiSettingsTestingBrowserTest,
                       ReplacesAndRemovesTheKey) {
  OpenSettings();
  EnterKeyThroughDialog(kKey);
  WaitForState("An API key is saved");
  EXPECT_EQ(content::EvalJs(contents(),
                            "document.getElementById('style-ai-add').textContent")
                .ExtractString(),
            "Replace key…");

  const std::string replacement =
      "sk-ant-api03-REPLACEMENTzyxwvutsrqponmlkjihgfedc98";
  EnterKeyThroughDialog(replacement);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return settings()->key_store().GetKeyForRequest() == replacement;
  }));

  ASSERT_TRUE(content::ExecJs(
      contents(), "document.getElementById('style-ai-remove').click()"));
  WaitForState("No API key is saved");
  EXPECT_EQ(Message(), "The key was removed.");
  EXPECT_TRUE(StoredCredential().empty());
  EXPECT_FALSE(settings()->key_store().GetKeyForRequest().has_value());
}

IN_PROC_BROWSER_TEST_F(AvoraStyleAiSettingsTestingBrowserTest,
                       RejectsAMalformedKeyWithoutStoring) {
  OpenSettings();
  EnterKeyThroughDialog("not a key");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Message().find("does not look like an API key") != std::string::npos;
  }));
  EXPECT_TRUE(StoredCredential().empty());
}

IN_PROC_BROWSER_TEST_F(AvoraStyleAiSettingsTestingBrowserTest,
                       CancellingTheDialogStoresNothing) {
  OpenSettings();
  ASSERT_TRUE(content::ExecJs(
      contents(), "document.getElementById('style-ai-add').click()"));
  views::Widget* dialog = WaitForKeyDialog();
  ASSERT_TRUE(dialog);
  FindTextfield(dialog->widget_delegate()->GetContentsView())
      ->SetText(base::UTF8ToUTF16(std::string(kKey)));
  dialog->widget_delegate()->AsDialogDelegate()->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return dialog->IsClosed(); }));
  EXPECT_TRUE(StoredCredential().empty());
}

IN_PROC_BROWSER_TEST_F(AvoraStyleAiSettingsTestingBrowserTest,
                       StoresOnlyAllowedModels) {
  OpenSettings();
  EXPECT_EQ(content::EvalJs(contents(),
                            "[...document.getElementById('style-ai-model')"
                            ".options].map(o => o.value).join(',')")
                .ExtractString(),
            "claude-opus-5-5,claude-sonnet-5-5,claude-haiku-5-5");
  EXPECT_EQ(content::EvalJs(contents(),
                            "document.getElementById('style-ai-model').value")
                .ExtractString(),
            "claude-opus-5-5");
  ASSERT_TRUE(content::ExecJs(contents(), R"(
      const select = document.getElementById('style-ai-model');
      select.value = 'claude-sonnet-5-5';
      select.dispatchEvent(new Event('change'));)"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return prefs()->GetString(avora::StyleAiKeyStore::kModelPref) ==
           "claude-sonnet-5-5";
  }));
  // A page asking for anything else is refused.
  ASSERT_TRUE(content::ExecJs(contents(),
                              "chrome.send('setStyleAiModel', ['gpt-5'])"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Message() == "That model is not available.";
  }));
  EXPECT_EQ(prefs()->GetString(avora::StyleAiKeyStore::kModelPref),
            "claude-sonnet-5-5");
}

// Without the testing switch, a mock keychain cannot protect a real key.
IN_PROC_BROWSER_TEST_F(AvoraStyleAiSettingsBrowserTest,
                       RefusesToStoreUnderAMockKeychain) {
  OpenSettings();
  EnterKeyThroughDialog(kKey);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Message().find("test keychain") != std::string::npos;
  }));
  EXPECT_TRUE(StoredCredential().empty());
  EXPECT_NE(State().find("No API key is saved"), std::string::npos);
}

IN_PROC_BROWSER_TEST_F(AvoraStyleAiSettingsBrowserTest,
                       IsUnavailableInPrivateWindows) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, GURL(kSettingsUrl)));
  content::WebContents* page =
      incognito->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(content::EvalJs(page, kStateScript).ExtractString(),
            "Style Studio AI is not available in private windows.");
  EXPECT_EQ(content::EvalJs(page,
                            "document.getElementById('style-ai-add').disabled")
                .ExtractBool(),
            true);
  // Asking anyway opens nothing and stores nothing.
  ASSERT_TRUE(content::ExecJs(page, "chrome.send('openStyleAiKeyDialog')"));
  EXPECT_EQ(avora::StyleAiSettings::GetForProfile(incognito->GetProfile()),
            nullptr);
  views::Widget* browser_widget = views::Widget::GetWidgetForNativeWindow(
      incognito->GetWindow()->GetNativeWindow());
  for (views::Widget* widget :
       views::Widget::GetAllOwnedWidgets(browser_widget->GetNativeView())) {
    EXPECT_EQ(widget->widget_delegate()->GetWindowTitle().find(
                  u"Anthropic API key"),
              std::u16string::npos);
  }
}

}  // namespace
