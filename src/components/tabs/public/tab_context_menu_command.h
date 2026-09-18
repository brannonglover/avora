// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_TABS_PUBLIC_TAB_CONTEXT_MENU_COMMAND_H_
#define COMPONENTS_TABS_PUBLIC_TAB_CONTEXT_MENU_COMMAND_H_

namespace tabs {

// Context menu commands for tabs.
// LINT.IfChange(TabContextMenuCommand)
enum TabContextMenuCommand {
  CommandNewTabToRight,
  CommandReload,
  CommandDuplicate,
  CommandCloseTab,
  CommandCloseOtherTabs,
  CommandCloseTabsToRight,
  CommandTogglePinned,
  CommandToggleGrouped,
  CommandToggleSiteMuted,
  CommandSendTabToSelf,
  CommandAddToReadLater,
  CommandAddToNewGroup,
  CommandAddToExistingGroup,
  CommandAddToNewGroupFromMenuItem,
  CommandAddToSplit,
  CommandSwapWithActiveSplit,
  CommandArrangeSplit,
  CommandRemoveFromGroup,
  CommandMoveToExistingWindow,
  CommandMoveTabsToNewWindow,
  CommandCopyURL,
  CommandGoBack,
  CommandCloseAllTabs,
  CommandToggleVertical,
  CommandGlicShare,
  CommandGlicCreateNewChat,
  CommandGlicSwitchToRecentConversation,
  CommandGlicUnshare,
  // Avora: toggle favorite state for the tab.
  CommandToggleFavorited,
  // Avora: parent of the "Move Tab to Space" submenu.  Never executed --
  // the submenu owns its own command range and delegate -- but the parent
  // model still queries the enabled/visible state of the submenu's own item
  // through this id.
  CommandMoveToSpace,
};
// LINT.ThenChange(//tools/metrics/histograms/metadata/tab/histograms.xml:TabContextMenuCommand)

}  // namespace tabs

#endif  // COMPONENTS_TABS_PUBLIC_TAB_CONTEXT_MENU_COMMAND_H_
