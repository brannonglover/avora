// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/defaults.h"

#include "build/build_config.h"

namespace browser_defaults {

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)
const bool kBrowserAliveWithNoWindows = true;
const bool kShowExitMenuItem = false;
#else
const bool kBrowserAliveWithNoWindows = false;
const bool kShowExitMenuItem = true;
#endif

#if BUILDFLAG(IS_CHROMEOS)
const bool kShowUpgradeMenuItem = false;
const bool kShowImportOnBookmarkBar = false;
const bool kAlwaysOpenIncognitoBrowserIfStartedWithIncognitoSwitch = true;
const bool kAlwaysCreateTabbedBrowserOnSessionRestore = false;
const bool kShowHelpMenuItemIcon = true;
#else
const bool kShowUpgradeMenuItem = true;
const bool kShowImportOnBookmarkBar = true;
const bool kAlwaysOpenIncognitoBrowserIfStartedWithIncognitoSwitch = false;
const bool kAlwaysCreateTabbedBrowserOnSessionRestore = true;
const bool kShowHelpMenuItemIcon = false;
#endif

#if BUILDFLAG(IS_LINUX)
const bool kScrollEventChangesTab = true;
#else
const bool kScrollEventChangesTab = false;
#endif

// Avora: bookmarks are disabled browser-wide.  Avora has no bookmark model --
// links imported from other browsers live in the sidebar's Imported section
// (see chrome/browser/avora/avora_imported_link_store.h), so upstream's
// bookmark surfaces would only offer commands that write to a store nothing
// in Avora reads.
//
// This is the single chokepoint for that: it disables IDC_BOOKMARK_THIS_TAB /
// IDC_BOOKMARK_ALL_TABS via CanBookmarkCurrentTab(), disables
// IDC_SHOW_BOOKMARK_MANAGER and the bookmark bar commands in
// BrowserCommandController, keeps BookmarkBarController from ever showing the
// bar, and hides the omnibox star via
// BookmarkPageActionController::ShouldShowPageAction().  ChromeOS guest
// sessions flip the same flag.
//
// Menu *items* are a separate concern: a disabled command greys an item out
// rather than removing it, so the menu-bar and app-menu entries are removed at
// their construction sites (chrome/browser/ui/cocoa/main_menu_builder.mm and
// chrome/browser/ui/toolbar/app_menu_model.cc).
bool bookmarks_enabled = false;

}  // namespace browser_defaults
