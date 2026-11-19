// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_quick_nav_commands.h"

#include <string>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_toast_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/view_utils.h"

namespace avora {

namespace {

// "cache" / "cookies", for the toast.
std::u16string CommandNoun(SiteDataCommand command) {
  switch (command) {
    case SiteDataCommand::kClearCache:
      return u"cache";
    case SiteDataCommand::kClearCookies:
      return u"cookies";
  }
}

bool ShowsSite(content::WebContents* contents) {
  return contents &&
         !ClearableSiteForURL(contents->GetLastCommittedURL()).empty();
}

// BrowserView::ShowAvoraToast() always draws the link icon, so this shows the
// window's toast directly, the way that method does.
void ShowToast(BrowserView* browser_view,
               const std::u16string& message,
               const gfx::VectorIcon& icon) {
  for (views::View* child : browser_view->children()) {
    if (auto* toast = views::AsViewClass<AvoraToastView>(child)) {
      toast->Show(message, icon);
      browser_view->ReorderChildView(toast, browser_view->children().size());
      browser_view->DeprecatedLayoutImmediately();
      return;
    }
  }
}

void OnCleared(base::WeakPtr<BrowserView> browser_view,
               base::WeakPtr<content::WebContents> target,
               SiteDataCommand command,
               const std::u16string& site,
               bool success) {
  if (success && target) {
    target->GetController().Reload(
        command == SiteDataCommand::kClearCache
            ? content::ReloadType::BYPASSING_CACHE
            : content::ReloadType::NORMAL,
        /*check_for_repost=*/true);
  }
  if (!browser_view) {
    return;
  }
  if (!success) {
    ShowToast(browser_view.get(),
              u"Couldn’t clear " + CommandNoun(command) + u" for " + site,
              SiteDataCommandIcon(command));
    return;
  }
  std::u16string noun = CommandNoun(command);
  noun[0] = base::ToUpperASCII(noun[0]);
  ShowToast(browser_view.get(), noun + u" cleared for " + site,
            SiteDataCommandIcon(command));
}

}  // namespace

const gfx::VectorIcon& SiteDataCommandIcon(SiteDataCommand command) {
  switch (command) {
    case SiteDataCommand::kClearCache:
      return vector_icons::kDatabaseIcon;
    case SiteDataCommand::kClearCookies:
      return vector_icons::kCookieIcon;
  }
}

content::WebContents* GetQuickNavCommandTarget(TabStripModel* model) {
  content::WebContents* active = model->GetActiveWebContents();
  if (!active) {
    return nullptr;
  }
  if (ShowsSite(active)) {
    return active;
  }

  // A tab with history of its own is not the fresh tab Cmd-T opened, so the
  // tab it came from is no stand-in for it.
  if (active->GetController().GetEntryCount() > 1) {
    return nullptr;
  }
  tabs::TabInterface* opener = model->GetOpenerOfTabAt(model->active_index());
  if (!opener || !ShowsSite(opener->GetContents())) {
    return nullptr;
  }
  return opener->GetContents();
}

void RunQuickNavCommand(BrowserView* browser_view, SiteDataCommand command) {
  TabStripModel* model = browser_view->browser()->GetTabStripModel();
  content::WebContents* target = GetQuickNavCommandTarget(model);
  if (!target) {
    ShowToast(browser_view, u"Open a site to clear its " + CommandNoun(command),
              SiteDataCommandIcon(command));
    return;
  }
  const std::u16string site =
      base::UTF8ToUTF16(ClearableSiteForURL(target->GetLastCommittedURL()));

  // Cmd-T opened a blank tab for the overlay, but the command was about the
  // page underneath.  Go back to that page, where the reload will show, and
  // drop the blank tab that was only ever a place to type.
  content::WebContents* active = model->GetActiveWebContents();
  if (active && active != target) {
    const int target_index = model->GetIndexOfWebContents(target);
    if (target_index != TabStripModel::kNoTab) {
      model->ActivateTabAt(target_index);
      const int blank_index = model->GetIndexOfWebContents(active);
      if (blank_index != TabStripModel::kNoTab) {
        model->CloseWebContentsAt(blank_index, TabCloseTypes::CLOSE_NONE);
      }
    }
  }

  ClearSiteData(target, command,
                base::BindOnce(&OnCleared, browser_view->GetAsWeakPtr(),
                               target->GetWeakPtr(), command, site));
}

}  // namespace avora
