#include "avora_browser_manager.h"

#include <algorithm>

#include "avora_layout.h"
#include "client_avora.h"

#include "include/wrapper/cef_helpers.h"

namespace avora {

namespace {

constexpr char kDefaultContentUrl[] = "https://www.google.com/";

}  // namespace

BrowserManager* BrowserManager::GetInstance() {
  static BrowserManager instance;
  return &instance;
}

BrowserManager::BrowserManager() = default;
BrowserManager::~BrowserManager() = default;

void BrowserManager::SetChromeBrowser(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  chrome_browser_ = browser;
}

void BrowserManager::SetContentBrowser(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  content_browser_ = browser;
}

void BrowserManager::SetWindowSize(int width, int height) {
  CEF_REQUIRE_UI_THREAD();
  window_width_ = std::max(width, kMinContentWidth + kSidebarMinWidth);
  window_height_ = std::max(height, kToolbarHeight + kMinContentHeight);
  sidebar_width_ = ClampSidebarWidth(sidebar_width_, window_width_);
  UpdateContentBounds();
}

void BrowserManager::SetSidebarWidth(int width) {
  CEF_REQUIRE_UI_THREAD();
  sidebar_width_ = ClampSidebarWidth(width, window_width_);
  UpdateContentBounds();
}

void BrowserManager::CreateContentBrowser(CefWindowHandle parent_handle) {
  CEF_REQUIRE_UI_THREAD();

  if (content_browser_.get() || !parent_handle) {
    return;
  }

  CefWindowInfo window_info;
  const CefRect bounds =
      ContentBounds(window_width_, window_height_, sidebar_width_);

#if defined(OS_WIN)
  window_info.SetAsChild(parent_handle, bounds);
#elif defined(OS_MAC)
  window_info.SetAsChild(parent_handle, bounds.x, bounds.y, bounds.width,
                         bounds.height);
#elif defined(OS_LINUX)
  window_info.SetAsChild(parent_handle, bounds);
#endif

  CefBrowserSettings settings;
  CefBrowserHost::CreateBrowser(window_info, new ContentClient(), kDefaultContentUrl,
                                settings, nullptr, nullptr);
}

void BrowserManager::UpdateContentBounds() {
  CEF_REQUIRE_UI_THREAD();

  if (!content_browser_.get()) {
    return;
  }

  auto host = content_browser_->GetHost();
  if (!host.get()) {
    return;
  }

  const CefRect bounds =
      ContentBounds(window_width_, window_height_, sidebar_width_);
  host->SetBounds(bounds);
}

void BrowserManager::NavigateContent(const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!content_browser_.get()) {
    return;
  }

  auto frame = content_browser_->GetMainFrame();
  if (frame.get()) {
    frame->LoadURL(url);
  }
}

}  // namespace avora
