// Copyright (c) 2017 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEF_EXAMPLES_AVORA_SIDEBAR_RESIZE_HANDLE_MAC_H_
#define CEF_EXAMPLES_AVORA_SIDEBAR_RESIZE_HANDLE_MAC_H_

#include "include/internal/cef_mac.h"

namespace avora {

using SidebarResizeCallback = void (*)(void* context, int width);
using DevToolsSplitterCallback = void (*)(void* context, int width);
using SidebarTabClickCallback = void (*)(void* context, double window_x,
                                         double window_y);
using SidebarTabHoverCallback = void (*)(void* context, int hovered_row);
using SidebarTabCloseCallback = void (*)(void* context, int row);
using ToolbarPlusClickCallback = void (*)(void* context);
using SidebarNavClickCallback = void (*)(void* context, int button);
using SidebarTabPinCallback = void (*)(void* context,
                                       double window_x,
                                       double window_y,
                                       bool should_pin);
using SpaceNavigateCallback = void (*)(void* context, int direction);

void InstallSidebarResizeHandle(CefWindowHandle window_handle,
                                int toolbar_height,
                                int sidebar_width,
                                int min_width,
                                int max_width,
                                void* context,
                                SidebarResizeCallback callback);
void UpdateSidebarResizeHandle(CefWindowHandle window_handle,
                               int toolbar_height,
                               int sidebar_width);
void RemoveSidebarResizeHandle(CefWindowHandle window_handle);

void InstallSidebarTabClickMonitor(CefWindowHandle window_handle,
                                   int toolbar_height,
                                   int sidebar_width,
                                   void* context,
                                   SidebarTabClickCallback callback,
                                   SidebarTabHoverCallback hover_callback,
                                   SidebarTabCloseCallback close_callback,
                                   ToolbarPlusClickCallback new_tab_callback,
                                   SidebarNavClickCallback nav_callback,
                                   ToolbarPlusClickCallback address_click_callback,
                                   SidebarTabPinCallback pin_callback,
                                   ToolbarPlusClickCallback extension_click_callback);
void UpdateSidebarTabClickMonitor(CefWindowHandle window_handle,
                                  int toolbar_height,
                                  int sidebar_width);
void UpdateSidebarTabClickMonitorTabCount(CefWindowHandle window_handle,
                                          int tab_count);
void UpdateSidebarTabClickMonitorPinnedHeight(CefWindowHandle window_handle,
                                              int pinned_height);
void RemoveSidebarTabClickMonitor(CefWindowHandle window_handle);

void InstallToolbarPlusClickMonitor(CefWindowHandle window_handle,
                                    int toolbar_height,
                                    void* context,
                                    ToolbarPlusClickCallback callback);
void RemoveToolbarPlusClickMonitor(CefWindowHandle window_handle);

void ApplyContentWellCornerRadius(CefWindowHandle window_handle,
                                  int toolbar_height,
                                  int sidebar_width,
                                  int content_inset,
                                  double radius);
void RemoveContentWellCornerOverlay(CefWindowHandle window_handle);

void InstallDevToolsSplitter(CefWindowHandle window_handle,
                             int sidebar_width,
                             int content_inset,
                             int web_surface_width,
                             int gap_width,
                             int min_width,
                             int max_width,
                             void* context,
                             DevToolsSplitterCallback callback);
void UpdateDevToolsSplitter(CefWindowHandle window_handle,
                            int sidebar_width,
                            int content_inset,
                            int web_surface_width,
                            int gap_width);
void RemoveDevToolsSplitter(CefWindowHandle window_handle);

void DisableNativeFocusRings(CefWindowHandle window_handle);

void InstallSpaceNavigationMonitor(CefWindowHandle window_handle,
                                   void* context,
                                   SpaceNavigateCallback callback);
void RemoveSpaceNavigationMonitor(CefWindowHandle window_handle);

void InstallAddressChipOutline(CefWindowHandle window_handle,
                               int sidebar_width,
                               int sidebar_top_inset,
                               int address_row_height,
                               int horizontal_pad);
void UpdateAddressChipOutline(CefWindowHandle window_handle,
                              int sidebar_width,
                              int sidebar_top_inset,
                              int address_row_height,
                              int horizontal_pad);
void RemoveAddressChipOutline(CefWindowHandle window_handle);

}  // namespace avora

#endif  // CEF_EXAMPLES_AVORA_SIDEBAR_RESIZE_HANDLE_MAC_H_
