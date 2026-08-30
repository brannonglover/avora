#include "client_avora.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "avora_browser_manager.h"
#include "avora_layout.h"

#include "examples/shared/client_manager.h"
#include "examples/shared/client_util.h"
#include "examples/shared/resource_util.h"

namespace avora {

namespace {

constexpr char kShellOrigin[] = "https://example.com/";

class ShellMessageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    const std::string& message = request;
    auto* manager = BrowserManager::GetInstance();

    if (message.rfind("sidebar:resize:", 0) == 0) {
      const int width = std::stoi(message.substr(std::strlen("sidebar:resize:")));
      manager->SetSidebarWidth(width);
      callback->Success("");
      return true;
    }

    if (message.rfind("navigate:", 0) == 0) {
      const std::string url = message.substr(std::strlen("navigate:"));
      manager->NavigateContent(url);
      callback->Success("");
      return true;
    }

    return false;
  }
};

void SetupResourceManager(CefRefPtr<CefResourceManager> resource_manager) {
  if (!CefCurrentlyOn(TID_IO)) {
    CefPostTask(TID_IO,
                base::BindOnce(SetupResourceManager, resource_manager));
    return;
  }

#if defined(OS_WIN)
  resource_manager->AddProvider(
      shared::CreateBinaryResourceProvider(kShellOrigin), 100, std::string());
#elif defined(OS_POSIX)
  std::string resource_dir;
  if (shared::GetResourceDir(resource_dir)) {
    resource_manager->AddDirectoryProvider(kShellOrigin, resource_dir, 100,
                                           std::string());
  }
#endif
}

}  // namespace

ChromeClient::ChromeClient() {
  resource_manager_ = new CefResourceManager();
  SetupResourceManager(resource_manager_);

  CefMessageRouterConfig config;
  message_router_ = CefMessageRouterBrowserSide::Create(config);
  message_handler_ = std::make_unique<ShellMessageHandler>();
  message_router_->AddHandler(message_handler_.get(), false);
}

void ChromeClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                 const CefString& title) {
  shared::OnTitleChange(browser, title);
}

void ChromeClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  browser_count_++;

  auto* manager = BrowserManager::GetInstance();
  if (!manager->HasChromeBrowser()) {
    manager->SetChromeBrowser(browser);

    if (auto host = browser->GetHost()) {
      manager->SetWindowSize(1280, 820);
      manager->CreateContentBrowser(host->GetWindowHandle());
    }
  }

  shared::OnAfterCreated(browser);
}

bool ChromeClient::DoClose(CefRefPtr<CefBrowser> browser) {
  return shared::DoClose(browser);
}

void ChromeClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  message_router_->OnBeforeClose(browser);
  if (--browser_count_ == 0) {
    message_router_->RemoveHandler(message_handler_.get());
    message_handler_.reset();
    message_router_ = nullptr;
  }

  shared::OnBeforeClose(browser);
}

void ChromeClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             int httpStatusCode) {
  if (frame->IsMain()) {
    BrowserManager::GetInstance()->UpdateContentBounds();
  }
}

bool ChromeClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  return message_router_->OnProcessMessageReceived(browser, frame,
                                                   source_process, message);
}

CefRefPtr<CefResourceRequestHandler> ChromeClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  CEF_REQUIRE_IO_THREAD();
  return this;
}

cef_return_value_t ChromeClient::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  CEF_REQUIRE_IO_THREAD();
  return resource_manager_->OnBeforeResourceLoad(browser, frame, request,
                                                 callback);
}

CefRefPtr<CefResourceHandler> ChromeClient::GetResourceHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request) {
  CEF_REQUIRE_IO_THREAD();
  return resource_manager_->GetResourceHandler(browser, frame, request);
}

void ContentClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString& title) {
  if (auto chrome = BrowserManager::GetInstance()->ChromeBrowser()) {
    shared::OnTitleChange(chrome, title);
  }
}

void ContentClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  BrowserManager::GetInstance()->SetContentBrowser(browser);
  BrowserManager::GetInstance()->UpdateContentBounds();
  shared::OnAfterCreated(browser);
}

bool ContentClient::DoClose(CefRefPtr<CefBrowser> browser) {
  return false;
}

void ContentClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  BrowserManager::GetInstance()->SetContentBrowser(nullptr);
  shared::OnBeforeClose(browser);
}

}  // namespace avora
