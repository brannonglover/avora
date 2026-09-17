// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_link_preview_tab_state.h"

#include <utility>

#include "base/no_destructor.h"
#include "chrome/browser/avora/avora_tab_site_instance.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"

namespace avora {

namespace {

const char kLinkPreviewTabKey[] = "avora_link_preview_tab";

using StateChangedCallbackList =
    base::RepeatingCallbackList<void(content::WebContents*)>;

StateChangedCallbackList& GetStateChangedCallbacks() {
  static base::NoDestructor<StateChangedCallbackList> callbacks;
  return *callbacks;
}

void NotifyStateChanged(content::WebContents* tab) {
  GetStateChangedCallbacks().Notify(tab);
}

}  // namespace

LinkPreviewTabState::LinkPreviewTabState(
    std::unique_ptr<content::WebContents> preview,
    const GURL& url)
    : preview_(std::move(preview)), url_(url) {}

LinkPreviewTabState::~LinkPreviewTabState() = default;

// static
LinkPreviewTabState* LinkPreviewTabState::Get(content::WebContents* tab) {
  if (!tab) {
    return nullptr;
  }
  return static_cast<LinkPreviewTabState*>(
      tab->GetUserData(kLinkPreviewTabKey));
}

// static
void LinkPreviewTabState::Set(content::WebContents* tab,
                              std::unique_ptr<content::WebContents> preview,
                              const GURL& url) {
  if (!tab || !preview) {
    return;
  }
  tab->SetUserData(
      kLinkPreviewTabKey,
      std::make_unique<LinkPreviewTabState>(std::move(preview), url));
  NotifyStateChanged(tab);
}

// static
void LinkPreviewTabState::Clear(content::WebContents* tab) {
  if (!Get(tab)) {
    return;
  }
  tab->RemoveUserData(kLinkPreviewTabKey);
  NotifyStateChanged(tab);
}

bool TabHasLinkPreview(content::WebContents* tab) {
  return LinkPreviewTabState::Get(tab) != nullptr;
}

content::WebContents* GetLinkPreviewContents(content::WebContents* tab) {
  LinkPreviewTabState* state = LinkPreviewTabState::Get(tab);
  return state ? state->preview_contents() : nullptr;
}

GURL GetLinkPreviewUrl(content::WebContents* tab) {
  LinkPreviewTabState* state = LinkPreviewTabState::Get(tab);
  return state ? state->previewed_url() : GURL();
}

std::unique_ptr<content::WebContents> CreateLinkPreviewContents(
    BrowserWindowInterface* browser,
    const GURL& url) {
  if (!browser) {
    return nullptr;
  }
  Profile* profile = browser->GetProfile();
  return content::WebContents::Create(content::WebContents::CreateParams(
      profile, GetSiteInstanceForNewAvoraTab(browser, url)));
}

base::CallbackListSubscription AddLinkPreviewStateChangedCallback(
    base::RepeatingCallback<void(content::WebContents*)> callback) {
  return GetStateChangedCallbacks().Add(std::move(callback));
}

}  // namespace avora
