// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_mini_extensions_container.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/avora/avora_mini_toolbar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"

namespace avora {

AvoraMiniExtensionsContainer::AvoraMiniExtensionsContainer(
    BrowserWindowInterface* browser,
    AvoraMiniToolbarView* toolbar)
    : browser_(browser),
      toolbar_(toolbar),
      registration_(browser->GetUnownedUserDataHost(), *this) {}

AvoraMiniExtensionsContainer::~AvoraMiniExtensionsContainer() {
  // The window is going away with a popup still up; close it rather than
  // leaving a bubble anchored to a view that is about to be destroyed.
  HideActivePopup();
}

ToolbarActionViewModel* AvoraMiniExtensionsContainer::GetActionForId(
    const std::string& action_id) {
  // See the class comment: null is safe for every caller that can reach a mini
  // window, and popups here never go through a ToolbarActionViewModel.
  return nullptr;
}

void AvoraMiniExtensionsContainer::HideActivePopup() {
  if (!popup_host_) {
    return;
  }
  extensions::ExtensionViewHost* const host = popup_host_;
  popup_host_ = nullptr;
  popup_host_observation_.Reset();
  host->Close();
}

void AvoraMiniExtensionsContainer::CloseExtensionsMenuIfOpen() {
  // The mini window has no extensions menu -- its affordance is the icon row
  // in the toolbar.  Nothing to close, and callers rely on that being safe
  // rather than on there being something there (ExtensionContextMenuModel
  // calls this through an unchecked ExtensionsContainer::From()).
}

bool AvoraMiniExtensionsContainer::ShowToolbarActionPopupForAPICall(
    const std::string& action_id,
    ShowPopupCallback callback) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return false;
  }

  // Only one popup at a time, the same rule the main window's container keeps.
  HideActivePopup();

  // Anchored to the extension's own icon in the mini window's toolbar, falling
  // back to the row.  Either way the anchor is in *this* window, which is what
  // keeps the popup over the mini window rather than over the main window.
  views::View* anchor =
      toolbar_ ? toolbar_->ExtensionAnchor(action_id) : nullptr;
  if (!anchor && toolbar_) {
    anchor = toolbar_->extensions_row();
  }

  extensions::ExtensionViewHost* const host =
      browser_view->ShowAvoraExtensionPopup(action_id, anchor,
                                            std::move(callback));
  if (!host) {
    return false;
  }
  TrackPopupHost(host);
  return true;
}

void AvoraMiniExtensionsContainer::ToggleExtensionsMenu() {}

bool AvoraMiniExtensionsContainer::HasAnyExtensions() const {
  Profile* const profile = browser_ ? browser_->GetProfile() : nullptr;
  if (!profile) {
    return false;
  }
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  auto* action_manager = extensions::ExtensionActionManager::Get(profile);
  if (!registry || !action_manager) {
    return false;
  }
  for (const auto& extension : registry->enabled_extensions()) {
    if (extension->is_extension() &&
        extensions::ui_util::ShouldDisplayInExtensionSettings(*extension) &&
        action_manager->GetExtensionAction(*extension)) {
      return true;
    }
  }
  return false;
}

void AvoraMiniExtensionsContainer::OnExtensionHostDestroyed(
    extensions::ExtensionHost* host) {
  if (popup_host_ == host) {
    popup_host_ = nullptr;
    popup_host_observation_.Reset();
  }
}

void AvoraMiniExtensionsContainer::TrackPopupHost(
    extensions::ExtensionViewHost* host) {
  popup_host_ = host;
  popup_host_observation_.Reset();
  popup_host_observation_.Observe(host);
}

}  // namespace avora
