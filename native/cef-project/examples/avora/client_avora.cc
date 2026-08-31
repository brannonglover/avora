// Copyright (c) 2017 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "examples/avora/client_avora.h"

#include <utility>

#include "include/cef_browser.h"
#include "include/cef_parser.h"
#include "examples/shared/client_util.h"

namespace avora {

namespace {

class FaviconDownloadCallback : public CefDownloadImageCallback {
 public:
  using Callback = std::function<void(CefRefPtr<CefImage>)>;
  explicit FaviconDownloadCallback(Callback cb) : cb_(std::move(cb)) {}

  void OnDownloadImageFinished(const CefString& image_url,
                               int http_status_code,
                               CefRefPtr<CefImage> image) override {
    if (cb_ && image && !image->IsEmpty()) {
      cb_(image);
    }
  }

 private:
  Callback cb_;
  IMPLEMENT_REFCOUNTING(FaviconDownloadCallback);
};

}  // namespace

Client::Client(TitleCallback title_callback,
               UrlCallback url_callback,
               ShortcutCallback new_tab_callback,
               ShortcutCallback close_overlay_callback,
               NewTabUrlCallback new_tab_url_callback,
               ShortcutCallback focus_location_callback,
               FaviconCallback favicon_callback,
               ShortcutCallback devtools_callback,
               SpaceSwitchCallback space_switch_callback)
    : title_callback_(std::move(title_callback)),
      url_callback_(std::move(url_callback)),
      new_tab_callback_(std::move(new_tab_callback)),
      close_overlay_callback_(std::move(close_overlay_callback)),
      new_tab_url_callback_(std::move(new_tab_url_callback)),
      focus_location_callback_(std::move(focus_location_callback)),
      favicon_callback_(std::move(favicon_callback)),
      devtools_callback_(std::move(devtools_callback)),
      space_switch_callback_(std::move(space_switch_callback)) {}

void Client::OnTitleChange(CefRefPtr<CefBrowser> browser,
                           const CefString& title) {
  if (title_callback_) {
    title_callback_(title.ToString());
  }

  // Call the default shared implementation.
  shared::OnTitleChange(browser, title);
}

void Client::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                const std::vector<CefString>& icon_urls) {
  if (!favicon_callback_) {
    return;
  }

  if (icon_urls.empty()) {
    favicon_callback_(nullptr);
    return;
  }

  auto cb = favicon_callback_;
  browser->GetHost()->DownloadImage(
      icon_urls[0], true, 32, false,
      new FaviconDownloadCallback(
          [cb](CefRefPtr<CefImage> image) { cb(image); }));
}

bool Client::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                           const CefKeyEvent& event,
                           CefEventHandle os_event,
                           bool* is_keyboard_shortcut) {
  // CTRL+1..9 switches to Space 1..9 (actual Control key, not Command).
  if (event.type == KEYEVENT_RAWKEYDOWN &&
      event.windows_key_code >= '1' && event.windows_key_code <= '9' &&
      (event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
      !(event.modifiers & EVENTFLAG_COMMAND_DOWN)) {
    if (is_keyboard_shortcut) {
      *is_keyboard_shortcut = true;
    }
    if (space_switch_callback_) {
      space_switch_callback_(event.windows_key_code - '0');
    }
    return true;
  }

  if (event.type == KEYEVENT_RAWKEYDOWN &&
      event.windows_key_code == 'T' &&
      (event.modifiers & (EVENTFLAG_COMMAND_DOWN | EVENTFLAG_CONTROL_DOWN))) {
    if (is_keyboard_shortcut) {
      *is_keyboard_shortcut = true;
    }

    if (new_tab_callback_) {
      new_tab_callback_();
    }
    return true;
  }

  if (event.type == KEYEVENT_RAWKEYDOWN &&
      event.windows_key_code == 'L' &&
      (event.modifiers & (EVENTFLAG_COMMAND_DOWN | EVENTFLAG_CONTROL_DOWN))) {
    if (is_keyboard_shortcut) {
      *is_keyboard_shortcut = true;
    }

    if (focus_location_callback_) {
      focus_location_callback_();
    }
    return true;
  }

  if (event.type == KEYEVENT_RAWKEYDOWN &&
      event.windows_key_code == 'I' &&
      (event.modifiers & EVENTFLAG_ALT_DOWN) &&
      (event.modifiers & (EVENTFLAG_COMMAND_DOWN | EVENTFLAG_CONTROL_DOWN))) {
    if (is_keyboard_shortcut) {
      *is_keyboard_shortcut = true;
    }

    if (devtools_callback_) {
      devtools_callback_();
    }
    return true;
  }

  if ((event.type == KEYEVENT_RAWKEYDOWN || event.type == KEYEVENT_KEYDOWN) &&
      event.windows_key_code == 27) {
    if (close_overlay_callback_) {
      close_overlay_callback_();
      return true;
    }
  }

  return false;
}

void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  // Call the default shared implementation.
  shared::OnAfterCreated(browser);
}

bool Client::DoClose(CefRefPtr<CefBrowser> browser) {
  // Call the default shared implementation.
  return shared::DoClose(browser);
}

void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  // Call the default shared implementation.
  return shared::OnBeforeClose(browser);
}

void Client::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int httpStatusCode) {
  if (!frame->IsMain()) {
    return;
  }

  if (url_callback_) {
    url_callback_(frame->GetURL().ToString());
  }

  frame->ExecuteJavaScript(R"JS(
(() => {
  const styleId = 'avora-viewport-radius';
  let style = document.getElementById(styleId);
  if (!style) {
    style = document.createElement('style');
    style.id = styleId;
    document.documentElement.appendChild(style);
  }
  style.textContent = `
    html {
      border-radius: 18px !important;
      overflow: hidden !important;
      background: transparent !important;
      clip-path: inset(0 round 18px) !important;
    }
    body {
      border-radius: 18px !important;
      min-height: 100vh !important;
      overflow: hidden !important;
    }
  `;
})();
)JS",
                           frame->GetURL(), 0);
}

bool Client::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefRequest> request,
                            bool user_gesture,
                            bool is_redirect) {
  const std::string url = request->GetURL().ToString();
  constexpr char kNewTabPrefix[] = "hm-new-tab:";
  if (url.rfind(kNewTabPrefix, 0) != 0) {
    if (frame && frame->IsMain() && url_callback_) {
      url_callback_(url);
    }
    return false;
  }

  if (new_tab_url_callback_) {
    const auto encoded_url = url.substr(std::string(kNewTabPrefix).size());
    new_tab_url_callback_(
        CefURIDecode(encoded_url, true,
                     static_cast<cef_uri_unescape_rule_t>(
                         UU_NORMAL | UU_PATH_SEPARATORS |
                         UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS))
            .ToString());
  }

  return true;
}

}  // namespace avora
