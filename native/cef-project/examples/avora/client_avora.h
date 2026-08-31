// Copyright (c) 2017 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEF_EXAMPLES_AVORA_CLIENT_H_
#define CEF_EXAMPLES_AVORA_CLIENT_H_

#include <functional>
#include <string>
#include <vector>

#include "include/cef_client.h"
#include "include/cef_image.h"

namespace avora {

// Minimal implementation of client handlers.
class Client : public CefClient,
               public CefDisplayHandler,
               public CefKeyboardHandler,
               public CefLifeSpanHandler,
               public CefLoadHandler,
               public CefRequestHandler {
 public:
  using TitleCallback = std::function<void(const std::string&)>;
  using UrlCallback = std::function<void(const std::string&)>;
  using ShortcutCallback = std::function<void()>;
  using NewTabUrlCallback = std::function<void(const std::string&)>;
  using FaviconCallback = std::function<void(CefRefPtr<CefImage>)>;
  using SpaceSwitchCallback = std::function<void(int space_number)>;

  explicit Client(TitleCallback title_callback = nullptr,
                  UrlCallback url_callback = nullptr,
                  ShortcutCallback new_tab_callback = nullptr,
                  ShortcutCallback close_overlay_callback = nullptr,
                  NewTabUrlCallback new_tab_url_callback = nullptr,
                  ShortcutCallback focus_location_callback = nullptr,
                  FaviconCallback favicon_callback = nullptr,
                  ShortcutCallback devtools_callback = nullptr,
                  SpaceSwitchCallback space_switch_callback = nullptr);
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  // CefClient methods:
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  // CefDisplayHandler methods:
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                          const std::vector<CefString>& icon_urls) override;

  // CefKeyboardHandler methods:
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;

  // CefLifeSpanHandler methods:
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefLoadHandler methods:
  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int httpStatusCode) override;

  // CefRequestHandler methods:
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;

 private:
  TitleCallback title_callback_;
  UrlCallback url_callback_;
  ShortcutCallback new_tab_callback_;
  ShortcutCallback close_overlay_callback_;
  NewTabUrlCallback new_tab_url_callback_;
  ShortcutCallback focus_location_callback_;
  FaviconCallback favicon_callback_;
  ShortcutCallback devtools_callback_;
  SpaceSwitchCallback space_switch_callback_;

  IMPLEMENT_REFCOUNTING(Client);
};

}  // namespace avora

#endif  // CEF_EXAMPLES_AVORA_CLIENT_H_
