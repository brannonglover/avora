// Manages the chrome shell browser and the native content browser.

#ifndef AVORA_BROWSER_MANAGER_H_
#define AVORA_BROWSER_MANAGER_H_

#include "avora_layout.h"

#include "include/cef_browser.h"

namespace avora {

class BrowserManager {
 public:
  static BrowserManager* GetInstance();

  void SetChromeBrowser(CefRefPtr<CefBrowser> browser);
  void SetContentBrowser(CefRefPtr<CefBrowser> browser);
  void SetWindowSize(int width, int height);

  bool HasChromeBrowser() const { return chrome_browser_.get() != nullptr; }
  bool HasContentBrowser() const { return content_browser_.get() != nullptr; }

  CefRefPtr<CefBrowser> ChromeBrowser() const { return chrome_browser_; }
  CefRefPtr<CefBrowser> ContentBrowser() const { return content_browser_; }

  int sidebar_width() const { return sidebar_width_; }
  void SetSidebarWidth(int width);

  void CreateContentBrowser(CefWindowHandle parent_handle);
  void UpdateContentBounds();
  void NavigateContent(const CefString& url);

 private:
  BrowserManager();
  ~BrowserManager();

  CefRefPtr<CefBrowser> chrome_browser_;
  CefRefPtr<CefBrowser> content_browser_;
  int window_width_ = 1280;
  int window_height_ = 820;
  int sidebar_width_ = kSidebarWidthDefault;

  BrowserManager(const BrowserManager&) = delete;
  BrowserManager& operator=(const BrowserManager&) = delete;
};

}  // namespace avora

#endif  // AVORA_BROWSER_MANAGER_H_
