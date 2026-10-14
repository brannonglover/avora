// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_EXTENSIONS_CONTAINER_H_
#define CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_EXTENSIONS_CONTAINER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/extensions/extensions_container.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_host_observer.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"

class BrowserWindowInterface;

namespace extensions {
class ExtensionViewHost;
}  // namespace extensions

namespace avora {

class AvoraMiniToolbarView;

// The mini window's ExtensionsContainer.
//
// Chromium reaches for a window's ExtensionsContainer whenever non-UI code has
// to do something with that window's extension actions, and the mini window
// had none: it is a popup window, and a popup's ToolbarView returns from Init()
// before the standard container is ever built.  That absence is what made
// chrome.action.openPopup() fail there -- the API refuses a window with no
// container -- and it also left a latent crash, because
// ExtensionContextMenuModel dereferences ExtensionsContainer::From() without
// checking it.
//
// This is deliberately the smallest container that satisfies every caller that
// can actually reach a mini window:
//
//   * GetActionForId() returns null.  Every reachable consumer null-checks it
//     (the media picker, the share-this-tab dialog and Avora's extensions grid
//     all do).  The one consumer that does not --
//     ExtensionActionDelegateDesktop::GetPopupOwnerDelegate() -- can never see
//     this container: all four construction sites of that delegate pass their
//     own container, never ExtensionsContainer::From(browser).  Showing popups
//     here therefore goes through ExtensionPopup directly rather than through
//     a ToolbarActionViewModel, which is also what keeps that promise true.
//   * HideActivePopup() and CloseExtensionsMenuIfOpen() are safe when nothing
//     is showing, which is what those unchecked callers need.
//   * ToggleExtensionsMenu() is a no-op: the mini window's affordance is the
//     row of extension icons in its toolbar, not a menu.
//
// Popups shown here are anchored to the mini window's own toolbar, so an
// extension that asks to show its popup while the mini window is active gets
// it over the mini window -- against the mini window's tab, in the mini
// window's Space -- rather than over the main window behind it.
class AvoraMiniExtensionsContainer : public ExtensionsContainer,
                                     public extensions::ExtensionHostObserver {
 public:
  AvoraMiniExtensionsContainer(BrowserWindowInterface* browser,
                               AvoraMiniToolbarView* toolbar);
  AvoraMiniExtensionsContainer(const AvoraMiniExtensionsContainer&) = delete;
  AvoraMiniExtensionsContainer& operator=(const AvoraMiniExtensionsContainer&) =
      delete;
  ~AvoraMiniExtensionsContainer() override;

  // ExtensionsContainer:
  ToolbarActionViewModel* GetActionForId(const std::string& action_id) override;
  void HideActivePopup() override;
  void CloseExtensionsMenuIfOpen() override;
  bool ShowToolbarActionPopupForAPICall(const std::string& action_id,
                                        ShowPopupCallback callback) override;
  void ToggleExtensionsMenu() override;
  bool HasAnyExtensions() const override;

  // extensions::ExtensionHostObserver:
  void OnExtensionHostDestroyed(extensions::ExtensionHost* host) override;

  extensions::ExtensionViewHost* popup_host_for_testing() {
    return popup_host_;
  }

 private:
  // Starts tracking |host| so HideActivePopup() can close it and so this stops
  // pointing at it when the popup goes away on its own.
  void TrackPopupHost(extensions::ExtensionViewHost* host);

  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<AvoraMiniToolbarView> toolbar_;

  // The popup currently showing in this window, or null.  Owned by the popup
  // widget, not by this.
  raw_ptr<extensions::ExtensionViewHost> popup_host_ = nullptr;
  base::ScopedObservation<extensions::ExtensionHost,
                          extensions::ExtensionHostObserver>
      popup_host_observation_{this};

  ui::ScopedUnownedUserData<ExtensionsContainer> registration_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_UI_VIEWS_AVORA_AVORA_MINI_EXTENSIONS_CONTAINER_H_
