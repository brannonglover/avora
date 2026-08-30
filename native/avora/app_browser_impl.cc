#include "client_avora.h"
#include "examples/shared/app_factory.h"
#include "examples/shared/browser_util.h"
#include "examples/shared/resource_util.h"

namespace avora {

namespace {

constexpr char kShellURL[] = "https://example.com/index.html";

}  // namespace

class BrowserApp : public CefApp, public CefBrowserProcessHandler {
 public:
  BrowserApp() = default;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override {
    if (process_type.empty()) {
#if defined(OS_MACOSX)
      command_line->AppendSwitch("use-mock-keychain");
#endif
    }
  }

  void OnContextInitialized() override {
    CefBrowserSettings settings;
    settings.background_color = CefColorSetARGB(0, 0, 0, 0);
    shared::CreateBrowser(new ChromeClient(), kShellURL, settings);
  }

 private:
  IMPLEMENT_REFCOUNTING(BrowserApp);
};

}  // namespace avora

namespace shared {

CefRefPtr<CefApp> CreateBrowserProcessApp() {
  return new avora::BrowserApp();
}

}  // namespace shared
