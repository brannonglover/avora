// Copyright (c) 2017 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "examples/avora/sidebar_resize_handle_mac.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>

#include "include/internal/cef_types_mac.h"

// CEF wrappers may omit full NSView method declarations needed by -Werror.
@interface NSView (AvoraSidebarView)
- (void)setOpaque:(BOOL)value;
@end

constexpr CGFloat kHandleWidth = 24.0;

// Matches app_browser_avora.cc sidebar + tab metrics.
static constexpr int kSidebarHorizontalPad = 10;
static constexpr int kCloseTrailingInsetMac = 8;
static constexpr int kCloseButtonWidth = 24;
static constexpr int kCloseGlyphSquareSizeMac = 24;
static constexpr int kSidebarTopInsetMac = 48;
static constexpr int kTrafficLightWidthMac = 86;
static constexpr int kSidebarAddressRowHeightMac = 44;
static constexpr int kPinnedLabelHeightMac = 42;
static constexpr int kPinnedEmptyHeightMac = 44;
static constexpr int kTabsLabelHeightMac = 36;
static constexpr int kTabHeightMac = 42;
static constexpr int kTabGapMac = 6;
static constexpr int kToolbarRightInsetMac = 12;
static constexpr int kToolbarNavButtonSizeMac = 36;
static constexpr int kSidebarNavButtonGapMac = 4;
static constexpr int kNavClusterWidthMac =
    kToolbarNavButtonSizeMac * 3 + kSidebarNavButtonGapMac * 2;
static constexpr int kNavRowRightPadMac = 2;
static constexpr int kExtensionIconSizeMac = 24;
static constexpr int kExtensionFieldRightPadMac = 6;

static NSPoint AvoraLogicalTopDownPoint(NSView* root_view, NSPoint point) {
  if (![root_view isFlipped]) {
    point.y = NSHeight([root_view bounds]) - point.y;
  }
  return point;
}

static int AvoraTabsHeaderTop(int sidebar_top_inset, int pinned_height) {
  return sidebar_top_inset + kSidebarAddressRowHeightMac + pinned_height;
}

static int AvoraFirstTabTop(int sidebar_top_inset, int pinned_height) {
  return AvoraTabsHeaderTop(sidebar_top_inset, pinned_height) +
         kTabsLabelHeightMac;
}

static int AvoraSidebarNavClusterLeft(int sidebar_width) {
  const int right =
      sidebar_width - kSidebarHorizontalPad - kNavRowRightPadMac;
  return std::max(kTrafficLightWidthMac, right - kNavClusterWidthMac);
}

static int AvoraSidebarNavButtonIndex(int sidebar_width,
                                      CGFloat logical_x,
                                      CGFloat logical_y) {
  if (logical_y < 0 || logical_y >= kSidebarTopInsetMac || logical_x < 0 ||
      logical_x > sidebar_width) {
    return -1;
  }
  CGFloat x = logical_x - static_cast<CGFloat>(
                              AvoraSidebarNavClusterLeft(sidebar_width));
  if (x < 0) {
    return -1;
  }
  const CGFloat button = static_cast<CGFloat>(kToolbarNavButtonSizeMac);
  const CGFloat gap = static_cast<CGFloat>(kSidebarNavButtonGapMac);
  for (int i = 0; i < 3; ++i) {
    if (x >= 0 && x < button) {
      return i;
    }
    x -= button + gap;
  }
  return -1;
}

static BOOL AvoraIsSidebarNavHit(int sidebar_width,
                                 CGFloat logical_x,
                                 CGFloat logical_y) {
  return AvoraSidebarNavButtonIndex(sidebar_width, logical_x, logical_y) >= 0;
}

static BOOL AvoraIsSidebarAddressHit(int sidebar_width,
                                     int sidebar_top_inset,
                                     CGFloat logical_x,
                                     CGFloat logical_y) {
  if (logical_y < sidebar_top_inset ||
      logical_y >= sidebar_top_inset + kSidebarAddressRowHeightMac) {
    return NO;
  }
  return logical_x >= 0 && logical_x <= sidebar_width;
}

static BOOL AvoraIsSidebarExtensionHit(int sidebar_width,
                                       int sidebar_top_inset,
                                       CGFloat logical_x,
                                       CGFloat logical_y) {
  if (!AvoraIsSidebarAddressHit(sidebar_width, sidebar_top_inset, logical_x,
                                logical_y)) {
    return NO;
  }
  const CGFloat icon_right = static_cast<CGFloat>(
      sidebar_width - kSidebarHorizontalPad - kExtensionFieldRightPadMac);
  const CGFloat icon_left =
      icon_right - static_cast<CGFloat>(kExtensionIconSizeMac);
  return logical_x >= icon_left && logical_x <= icon_right;
}

static BOOL AvoraIsSidebarPlusHit(int sidebar_width,
                                  int sidebar_top_inset,
                                  int pinned_height,
                                  CGFloat logical_x,
                                  CGFloat logical_y) {
  const int header_top =
      AvoraTabsHeaderTop(sidebar_top_inset, pinned_height);
  if (logical_y < header_top ||
      logical_y >= header_top + kTabsLabelHeightMac) {
    return NO;
  }
  const CGFloat plus_left = static_cast<CGFloat>(
      sidebar_width - kSidebarHorizontalPad - kToolbarNavButtonSizeMac);
  const CGFloat plus_right =
      static_cast<CGFloat>(sidebar_width - kSidebarHorizontalPad);
  return logical_x >= plus_left && logical_x <= plus_right;
}

static NSRect AvoraSidebarHandleFrame(NSView* root_view,
                                      int toolbar_height,
                                      int sidebar_width) {
  const NSRect bounds = [root_view bounds];
  const CGFloat height = std::max<CGFloat>(0, bounds.size.height -
                                                  toolbar_height);
  const CGFloat x = std::max<CGFloat>(0, sidebar_width - kHandleWidth / 2.0);
  const CGFloat y = [root_view isFlipped] ? toolbar_height : 0;
  return NSMakeRect(x, y, kHandleWidth, height);
}

@interface AvoraSidebarResizeHandle : NSView {
 @private
  void* _context;
  avora::SidebarResizeCallback _callback;
  int _sidebarWidth;
  int _startSidebarWidth;
  int _minWidth;
  int _maxWidth;
  CGFloat _startMouseX;
  NSTrackingArea* _trackingArea;
  id _eventMonitor;
  BOOL _hovering;
  BOOL _dragging;
}

- (instancetype)initWithFrame:(NSRect)frame
                 sidebarWidth:(int)sidebarWidth
                     minWidth:(int)minWidth
                     maxWidth:(int)maxWidth
                      context:(void*)context
                     callback:(avora::SidebarResizeCallback)callback;
- (void)setSidebarWidth:(int)width;

@end

@implementation AvoraSidebarResizeHandle

- (instancetype)initWithFrame:(NSRect)frame
                 sidebarWidth:(int)sidebarWidth
                     minWidth:(int)minWidth
                     maxWidth:(int)maxWidth
                      context:(void*)context
                     callback:(avora::SidebarResizeCallback)callback {
  self = [super initWithFrame:frame];
  if (self) {
    _sidebarWidth = sidebarWidth;
    _minWidth = minWidth;
    _maxWidth = maxWidth;
    _context = context;
    _callback = callback;
    [self setWantsLayer:YES];
    self.layer.backgroundColor = [[NSColor clearColor] CGColor];
    __block AvoraSidebarResizeHandle* weakSelf = self;
    _eventMonitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown |
                                             NSEventMaskLeftMouseDragged |
                                             NSEventMaskLeftMouseUp
                                     handler:^NSEvent*(NSEvent* event) {
                                       AvoraSidebarResizeHandle* strongSelf =
                                           weakSelf;
                                       if (!strongSelf ||
                                           [event window] !=
                                               [strongSelf window]) {
                                         return event;
                                       }

                                       if ([event type] ==
                                           NSEventTypeLeftMouseDown) {
                                         const NSPoint point =
                                             [[strongSelf superview]
                                                 convertPoint:
                                                     [event locationInWindow]
                                                   fromView:nil];
                                         const NSPoint logical =
                                             AvoraLogicalTopDownPoint(
                                                 [strongSelf superview],
                                                 point);
                                         if (AvoraIsSidebarPlusHit(
                                                 strongSelf->_sidebarWidth,
                                                 kSidebarTopInsetMac,
                                                 kPinnedLabelHeightMac +
                                                     kPinnedEmptyHeightMac,
                                                 logical.x, logical.y) ||
                                             AvoraIsSidebarAddressHit(
                                                 strongSelf->_sidebarWidth,
                                                 kSidebarTopInsetMac,
                                                 logical.x, logical.y) ||
                                             AvoraIsSidebarNavHit(
                                                 strongSelf->_sidebarWidth,
                                                 logical.x, logical.y)) {
                                           return event;
                                         }
                                         if (NSPointInRect(
                                                 point,
                                                 [strongSelf frame])) {
                                           [strongSelf
                                               beginResizeWithEvent:event];
                                           return nil;
                                         }
                                         return event;
                                       }

                                       if ([event type] ==
                                           NSEventTypeLeftMouseDragged) {
                                         if ([strongSelf isResizing]) {
                                           [strongSelf
                                               updateResizeWithEvent:event];
                                           return nil;
                                         }
                                         return event;
                                       }

                                       if ([event type] ==
                                           NSEventTypeLeftMouseUp) {
                                         if ([strongSelf isResizing]) {
                                           [strongSelf endResize];
                                           return nil;
                                         }
                                       }

                                       return event;
                                     }];
  }
  return self;
}

- (void)dealloc {
  if (_eventMonitor) {
    [NSEvent removeMonitor:_eventMonitor];
    _eventMonitor = nil;
  }
  [super dealloc];
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (NSView*)hitTest:(NSPoint)point {
  if (!NSPointInRect(point, [self frame])) {
    return nil;
  }
  NSView* root = [self superview];
  if (root) {
    const NSPoint logical = AvoraLogicalTopDownPoint(root, point);
    if (AvoraIsSidebarPlusHit(_sidebarWidth, kSidebarTopInsetMac,
                              kPinnedLabelHeightMac + kPinnedEmptyHeightMac,
                              logical.x, logical.y) ||
        AvoraIsSidebarAddressHit(_sidebarWidth, kSidebarTopInsetMac, logical.x,
                                 logical.y) ||
        AvoraIsSidebarNavHit(_sidebarWidth, logical.x, logical.y)) {
      return nil;
    }
  }
  return self;
}

- (void)setSidebarWidth:(int)width {
  _sidebarWidth = width;
}

- (void)resetCursorRects {
  [self addCursorRect:[self bounds] cursor:[NSCursor resizeLeftRightCursor]];
}

- (void)updateTrackingAreas {
  if (_trackingArea) {
    [self removeTrackingArea:_trackingArea];
  }

  _trackingArea = [[NSTrackingArea alloc]
      initWithRect:[self bounds]
           options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways |
                   NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:_trackingArea];
  [super updateTrackingAreas];
}

- (void)mouseEntered:(NSEvent*)event {
  _hovering = YES;
  [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event {
  _hovering = NO;
  [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event {
  NSView* root = [self superview];
  if (root) {
    NSPoint point =
        [root convertPoint:[event locationInWindow] fromView:nil];
    const NSPoint logical = AvoraLogicalTopDownPoint(root, point);
    if (AvoraIsSidebarPlusHit(_sidebarWidth, kSidebarTopInsetMac,
                              kPinnedLabelHeightMac + kPinnedEmptyHeightMac,
                              logical.x, logical.y) ||
        AvoraIsSidebarAddressHit(_sidebarWidth, kSidebarTopInsetMac, logical.x,
                                 logical.y) ||
        AvoraIsSidebarNavHit(_sidebarWidth, logical.x, logical.y)) {
      return;
    }
  }
  [self beginResizeWithEvent:event];
}

- (BOOL)isResizing {
  return _dragging;
}

- (void)beginResizeWithEvent:(NSEvent*)event {
  [[self window] makeFirstResponder:self];
  _startMouseX = [event locationInWindow].x;
  _startSidebarWidth = _sidebarWidth;
  _dragging = YES;
  [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event {
  [self updateResizeWithEvent:event];
}

- (void)updateResizeWithEvent:(NSEvent*)event {
  const CGFloat delta = [event locationInWindow].x - _startMouseX;
  const int nextWidth = std::clamp(static_cast<int>(_startSidebarWidth + delta),
                                   _minWidth, _maxWidth);
  if (nextWidth == _sidebarWidth) {
    return;
  }

  _sidebarWidth = nextWidth;
  if (_callback) {
    _callback(_context, nextWidth);
  }
}

- (void)endResize {
  _dragging = NO;
  [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event {
  [self endResize];
}

- (void)drawRect:(NSRect)dirtyRect {
  constexpr CGFloat kGripHeight = 44.0;
  constexpr CGFloat kGripWidth = 3.0;
  NSColor* gripColor = nil;
  if (_dragging) {
    gripColor = [NSColor colorWithCalibratedRed:0.82
                                          green:0.47
                                           blue:0.12
                                          alpha:1.0];
  } else if (_hovering) {
    gripColor = [NSColor colorWithCalibratedRed:0.72
                                          green:0.76
                                           blue:0.78
                                          alpha:0.78];
  } else {
    gripColor = [NSColor colorWithCalibratedRed:0.60
                                          green:0.65
                                           blue:0.68
                                          alpha:0.48];
  }
  [gripColor setFill];

  const NSRect bounds = [self bounds];
  const NSRect gripRect =
      NSMakeRect(NSMidX(bounds) - kGripWidth / 2.0,
                 NSMidY(bounds) - kGripHeight / 2.0, kGripWidth, kGripHeight);
  NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:gripRect
                                                       xRadius:kGripWidth / 2.0
                                                       yRadius:kGripWidth / 2.0];
  [path fill];
}

@end

static AvoraSidebarResizeHandle* AvoraSidebarHandleForRootView(
    NSView* root_view) {
  for (NSView* subview in [root_view subviews]) {
    if ([subview isKindOfClass:[AvoraSidebarResizeHandle class]]) {
      return static_cast<AvoraSidebarResizeHandle*>(subview);
    }
  }
  return nil;
}

@interface AvoraSidebarTabClickMonitor : NSView {
 @private
  NSView* _rootView;
  void* _context;
  avora::SidebarTabClickCallback _callback;
  avora::SidebarTabHoverCallback _hoverCallback;
  avora::SidebarTabCloseCallback _closeCallback;
  avora::ToolbarPlusClickCallback _newTabCallback;
  avora::SidebarNavClickCallback _navCallback;
  avora::ToolbarPlusClickCallback _addressClickCallback;
  avora::SidebarTabPinCallback _pinCallback;
  avora::ToolbarPlusClickCallback _extensionClickCallback;
  id _eventMonitor;
  NSTrackingArea* _trackingArea;
  int _toolbarHeight;
  int _sidebarWidth;
  int _tabCount;
  int _pinnedSectionHeight;
  NSInteger _lastHoveredRow;
  NSInteger _eraseGlyphForRowBeforeDisplay;
  BOOL _plusInvoking;
}

- (instancetype)initWithRootView:(NSView*)rootView
                   toolbarHeight:(int)toolbarHeight
                     sidebarWidth:(int)sidebarWidth
                         context:(void*)context
                        callback:(avora::SidebarTabClickCallback)callback
                   hoverCallback:(avora::SidebarTabHoverCallback)hoverCallback
                   closeCallback:(avora::SidebarTabCloseCallback)closeCallback
                  newTabCallback:(avora::ToolbarPlusClickCallback)newTabCallback
                    navCallback:(avora::SidebarNavClickCallback)navCallback
              addressClickCallback:
                  (avora::ToolbarPlusClickCallback)addressClickCallback
                     pinCallback:(avora::SidebarTabPinCallback)pinCallback
            extensionClickCallback:
                  (avora::ToolbarPlusClickCallback)extensionClickCallback;
- (void)setToolbarHeight:(int)toolbarHeight sidebarWidth:(int)sidebarWidth;
- (void)setTabCount:(int)count;
- (void)setPinnedSectionHeight:(int)height;
- (void)invokeNewTab;
- (void)invokeNavButton:(int)button;

@end

@implementation AvoraSidebarTabClickMonitor

- (instancetype)initWithRootView:(NSView*)rootView
                   toolbarHeight:(int)toolbarHeight
                     sidebarWidth:(int)sidebarWidth
                         context:(void*)context
                        callback:(avora::SidebarTabClickCallback)callback
                   hoverCallback:(avora::SidebarTabHoverCallback)hoverCallback
                   closeCallback:(avora::SidebarTabCloseCallback)closeCallback
                  newTabCallback:(avora::ToolbarPlusClickCallback)newTabCallback
                    navCallback:(avora::SidebarNavClickCallback)navCallback
              addressClickCallback:
                  (avora::ToolbarPlusClickCallback)addressClickCallback
                     pinCallback:(avora::SidebarTabPinCallback)pinCallback
            extensionClickCallback:
                  (avora::ToolbarPlusClickCallback)extensionClickCallback {
  self = [super initWithFrame:[rootView bounds]];
  if (self) {
    _rootView = rootView;
    _context = context;
    _callback = callback;
    _hoverCallback = hoverCallback;
    _closeCallback = closeCallback;
    _newTabCallback = newTabCallback;
    _navCallback = navCallback;
    _addressClickCallback = addressClickCallback;
    _pinCallback = pinCallback;
    _extensionClickCallback = extensionClickCallback;
    _toolbarHeight = toolbarHeight;
    _sidebarWidth = sidebarWidth;
    _tabCount = 0;
    _pinnedSectionHeight = kPinnedLabelHeightMac + kPinnedEmptyHeightMac;
    _lastHoveredRow = -1;
    _eraseGlyphForRowBeforeDisplay = -1;
    _plusInvoking = NO;
    [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [self setOpaque:NO];
    [self setWantsLayer:NO];

    __block AvoraSidebarTabClickMonitor* weakSelf = self;
    _eventMonitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown |
                                             NSEventMaskRightMouseDown
                                     handler:^NSEvent*(NSEvent* event) {
                                       AvoraSidebarTabClickMonitor* strongSelf =
                                           weakSelf;
                                       if (!strongSelf ||
                                           [event window] !=
                                               [strongSelf->_rootView window]) {
                                         return event;
                                       }

                                       NSPoint point =
                                           [strongSelf->_rootView
                                               convertPoint:
                                                   [event locationInWindow]
                                                 fromView:nil];
                                       point = AvoraLogicalTopDownPoint(
                                           strongSelf->_rootView, point);

                                       if (point.x < 0 ||
                                           point.x > strongSelf->_sidebarWidth) {
                                         return event;
                                       }

                                       if ([event type] ==
                                           NSEventTypeRightMouseDown) {
                                         [strongSelf
                                             showContextMenuAtLogicalPoint:point
                                                                withEvent:event];
                                         return event;
                                       }

                                       const int nav = AvoraSidebarNavButtonIndex(
                                           strongSelf->_sidebarWidth, point.x,
                                           point.y);
                                       if (nav >= 0) {
                                         [strongSelf invokeNavButton:nav];
                                         return nil;
                                       }

                                       if (point.y < strongSelf->_toolbarHeight) {
                                         return event;
                                       }

                                       if (AvoraIsSidebarAddressHit(
                                               strongSelf->_sidebarWidth,
                                               strongSelf->_toolbarHeight,
                                               point.x, point.y)) {
                                         if (AvoraIsSidebarExtensionHit(
                                                 strongSelf->_sidebarWidth,
                                                 strongSelf->_toolbarHeight,
                                                 point.x, point.y)) {
                                           if (strongSelf->_extensionClickCallback) {
                                             strongSelf->_extensionClickCallback(
                                                 strongSelf->_context);
                                           }
                                           return nil;
                                         }
                                         if (strongSelf->_addressClickCallback) {
                                           strongSelf->_addressClickCallback(
                                               strongSelf->_context);
                                         }
                                         return event;
                                       }

                                       if (AvoraIsSidebarPlusHit(
                                               strongSelf->_sidebarWidth,
                                               strongSelf->_toolbarHeight,
                                               strongSelf->_pinnedSectionHeight,
                                               point.x, point.y)) {
                                         [strongSelf invokeNewTab];
                                         return nil;
                                       }

                                       NSInteger row = [strongSelf
                                           tabRowForLogicalY:point.y];
                                       if (row >= 0 && row < strongSelf->_tabCount &&
                                           [strongSelf isCloseHitAtX:point.x] &&
                                           strongSelf->_closeCallback) {
                                         strongSelf->_closeCallback(
                                             strongSelf->_context,
                                             static_cast<int>(row));
                                       } else if (strongSelf->_callback) {
                                         strongSelf->_callback(
                                             strongSelf->_context, point.x,
                                             point.y);
                                       }
                                       return event;
                                     }];
  }
  return self;
}

- (void)dealloc {
  if (_eventMonitor) {
    [NSEvent removeMonitor:_eventMonitor];
    _eventMonitor = nil;
  }
  if (_trackingArea) {
    [self removeTrackingArea:_trackingArea];
  }
  [super dealloc];
}

- (void)updateTrackingAreas {
  if (_trackingArea) {
    [self removeTrackingArea:_trackingArea];
  }

  _trackingArea = [[NSTrackingArea alloc]
      initWithRect:[self bounds]
           options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                   NSTrackingActiveAlways | NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:_trackingArea];
  [super updateTrackingAreas];
}

- (NSInteger)tabRowForLogicalY:(CGFloat)logicalYTopDown {
  const int first_tab_top =
      AvoraFirstTabTop(_toolbarHeight, _pinnedSectionHeight);
  if (logicalYTopDown < first_tab_top) {
    return -1;
  }

  const int row_span = kTabHeightMac + kTabGapMac;
  const int clamped_y = static_cast<int>(logicalYTopDown);
  const int row = (clamped_y - first_tab_top) / row_span;
  const int row_offset = (clamped_y - first_tab_top) % row_span;
  if (row_offset >= kTabHeightMac) {
    return -1;
  }
  if (row >= _tabCount) {
    return -1;
  }
  return row;
}

- (NSRect)viewRectLogicalTopOriginX:(CGFloat)x
                          yTopLogical:(CGFloat)yTopLogical
                                width:(CGFloat)w
                               height:(CGFloat)h {
  const NSRect b = [self bounds];
  if ([self isFlipped]) {
    return NSMakeRect(x, yTopLogical, w, h);
  }

  const CGFloat y_from_bottom = NSHeight(b) - yTopLogical - h;
  return NSMakeRect(x, y_from_bottom, w, h);
}

- (CGFloat)closeHitZoneLeftEdge {
  return static_cast<CGFloat>(_sidebarWidth - kSidebarHorizontalPad -
                             kCloseTrailingInsetMac - kCloseButtonWidth);
}

- (CGFloat)closeHitZoneRightEdge {
  return static_cast<CGFloat>(_sidebarWidth - kSidebarHorizontalPad -
                             kCloseTrailingInsetMac);
}

// 28×28 close glyph centered vertically in the 42px tab row; matches hit x-range.
- (NSRect)viewRectForCloseGlyphInRow:(NSInteger)row {
  const CGFloat hit_left = [self closeHitZoneLeftEdge];
  const CGFloat hit_right = [self closeHitZoneRightEdge];
  const int first_tab_top =
      AvoraFirstTabTop(_toolbarHeight, _pinnedSectionHeight);
  const CGFloat row_top =
      static_cast<CGFloat>(
          first_tab_top +
          static_cast<int>(row) * (kTabHeightMac + kTabGapMac));
  const CGFloat square = static_cast<CGFloat>(kCloseGlyphSquareSizeMac);
  const CGFloat y_glyph_top =
      row_top +
      std::floor((static_cast<double>(kTabHeightMac) - square) / 2.0);
  return [self viewRectLogicalTopOriginX:hit_left
                             yTopLogical:y_glyph_top
                                   width:(hit_right - hit_left)
                                  height:square];
}

- (BOOL)isCloseHitAtX:(CGFloat)x {
  const CGFloat hit_left = [self closeHitZoneLeftEdge];
  const CGFloat hit_right = [self closeHitZoneRightEdge];
  return x >= hit_left && x <= hit_right;
}

- (void)invalidateGlyphsForHoveredRowTransitionFrom:(NSInteger)fromRow
                                                   to:(NSInteger)toRow {
  _eraseGlyphForRowBeforeDisplay = fromRow >= 0 ? fromRow : -1;
  if (fromRow >= 0) {
    [self setNeedsDisplayInRect:[self viewRectForCloseGlyphInRow:fromRow]];
  }
  if (toRow >= 0 && toRow != fromRow) {
    [self setNeedsDisplayInRect:[self viewRectForCloseGlyphInRow:toRow]];
  }
  [self setNeedsDisplay:YES];
}

- (void)dispatchHoverForEvent:(NSEvent*)event {
  NSPoint point =
      [_rootView convertPoint:[event locationInWindow] fromView:nil];
  if (![_rootView isFlipped]) {
    const NSRect bounds = [_rootView bounds];
    point.y = bounds.size.height - point.y;
  }

  NSInteger row = -1;
  if (point.x >= 0 && point.x <= _sidebarWidth && point.y >= _toolbarHeight &&
      !AvoraIsSidebarPlusHit(_sidebarWidth, _toolbarHeight,
                              _pinnedSectionHeight, point.x, point.y) &&
      !AvoraIsSidebarAddressHit(_sidebarWidth, _toolbarHeight, point.x,
                                point.y)) {
    row = [self tabRowForLogicalY:point.y];
  }

  if (row != _lastHoveredRow) {
    const NSInteger previous = _lastHoveredRow;
    _lastHoveredRow = row;
    [self invalidateGlyphsForHoveredRowTransitionFrom:previous to:row];
    if (_hoverCallback) {
      _hoverCallback(_context, static_cast<int>(row));
    }
  }
}

- (void)mouseMoved:(NSEvent*)event {
  [self dispatchHoverForEvent:event];
}

- (void)mouseExited:(NSEvent*)event {
  if (_lastHoveredRow < 0) {
    return;
  }

  const NSInteger previous = _lastHoveredRow;
  _lastHoveredRow = -1;
  [self invalidateGlyphsForHoveredRowTransitionFrom:previous to:-1];
  if (_hoverCallback) {
    _hoverCallback(_context, -1);
  }
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (void)invokeNewTab {
  if (_plusInvoking || !_newTabCallback) {
    return;
  }
  _plusInvoking = YES;
  _newTabCallback(_context);
  _plusInvoking = NO;
}

- (void)invokeNavButton:(int)button {
  if (_plusInvoking || !_navCallback || button < 0) {
    return;
  }
  _plusInvoking = YES;
  _navCallback(_context, button);
  _plusInvoking = NO;
}

- (NSView*)hitTest:(NSPoint)point {
  const NSRect bounds = [self bounds];
  const CGFloat topY =
      [self isFlipped] ? point.y : bounds.size.height - point.y;
  if (point.x >= 0 && point.x <= _sidebarWidth && topY >= _toolbarHeight) {
    if (AvoraIsSidebarPlusHit(_sidebarWidth, _toolbarHeight,
                               _pinnedSectionHeight, point.x, topY) ||
        AvoraIsSidebarAddressHit(_sidebarWidth, _toolbarHeight, point.x,
                                 topY)) {
      return nil;
    }
    return self;
  }
  return nil;
}

- (void)showContextMenuAtLogicalPoint:(NSPoint)point
                            withEvent:(NSEvent*)event {
  if (!_pinCallback) {
    return;
  }

  if (point.y < _toolbarHeight) {
    return;
  }

  const int address_bottom = _toolbarHeight + kSidebarAddressRowHeightMac;
  const int tabs_header_top =
      AvoraTabsHeaderTop(_toolbarHeight, _pinnedSectionHeight);

  BOOL is_pinned_area =
      (point.y >= address_bottom && point.y < tabs_header_top);
  BOOL is_tab_area = NO;
  if (!is_pinned_area) {
    NSInteger row = [self tabRowForLogicalY:point.y];
    is_tab_area = (row >= 0 && row < _tabCount);
  }

  if (!is_pinned_area && !is_tab_area) {
    return;
  }

  NSString* title = is_pinned_area ? @"Unpin Tab" : @"Pin Tab";
  bool should_pin = !is_pinned_area;

  NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                action:nil
                                         keyEquivalent:@""];
  item.target = self;
  item.action = @selector(contextMenuPinAction:);
  item.representedObject = @{
    @"wx" : @(point.x),
    @"wy" : @(point.y),
    @"pin" : @(should_pin),
  };
  [menu addItem:item];

  [menu popUpMenuPositioningItem:nil
                      atLocation:[event locationInWindow]
                          inView:nil];
}

- (void)contextMenuPinAction:(NSMenuItem*)sender {
  NSDictionary* info = sender.representedObject;
  double wx = [info[@"wx"] doubleValue];
  double wy = [info[@"wy"] doubleValue];
  bool pin = [info[@"pin"] boolValue];
  if (_pinCallback) {
    _pinCallback(_context, wx, wy, pin);
  }
}

- (void)mouseDown:(NSEvent*)event {
  NSPoint point =
      [_rootView convertPoint:[event locationInWindow] fromView:nil];
  point = AvoraLogicalTopDownPoint(_rootView, point);

  if (AvoraIsSidebarAddressHit(_sidebarWidth, _toolbarHeight, point.x,
                               point.y)) {
    return;
  }

  if (AvoraIsSidebarPlusHit(_sidebarWidth, _toolbarHeight,
                            _pinnedSectionHeight, point.x, point.y)) {
    [self invokeNewTab];
    return;
  }

  const NSInteger row = [self tabRowForLogicalY:point.y];
  if (row >= 0 && row < _tabCount && [self isCloseHitAtX:point.x] &&
      _closeCallback) {
    _closeCallback(_context, static_cast<int>(row));
  } else if (_callback) {
    _callback(_context, point.x, point.y);
  }
}

- (void)drawRect:(NSRect)dirtyRect {
  // After hover leaves or moves to another tab, repaint the former glyph rect.
  // Do not consume _eraseGlyphForRowBeforeDisplay until that rect intersects
  // dirtyRect, so coalesced partial updates still clear stale pixels.
  if (_eraseGlyphForRowBeforeDisplay >= 0) {
    const NSInteger eraseRow = _eraseGlyphForRowBeforeDisplay;
    const NSRect erase_rect = [self viewRectForCloseGlyphInRow:eraseRow];
    if (NSIntersectsRect(dirtyRect, erase_rect)) {
      [NSGraphicsContext saveGraphicsState];
      [[NSBezierPath bezierPathWithRect:erase_rect] addClip];
      CGContextRef cg = [[NSGraphicsContext currentContext] CGContext];
      const CGRect cg_rect =
          CGRectMake(NSMinX(erase_rect), NSMinY(erase_rect),
                     NSWidth(erase_rect), NSHeight(erase_rect));
      CGContextClearRect(cg, cg_rect);
      [NSGraphicsContext restoreGraphicsState];
      _eraseGlyphForRowBeforeDisplay = -1;
    }
  }

  if (_lastHoveredRow >= 0 && _lastHoveredRow < _tabCount) {
    const NSRect close_rect =
        [self viewRectForCloseGlyphInRow:_lastHoveredRow];
    if (NSIntersectsRect(dirtyRect, close_rect)) {
      constexpr CGFloat kCircleInset = 1.0;
      const NSRect circleRect = NSInsetRect(close_rect, kCircleInset, kCircleInset);
      NSBezierPath* circle =
          [NSBezierPath bezierPathWithOvalInRect:circleRect];
      [[NSColor colorWithCalibratedRed:1.0
                                 green:1.0
                                  blue:1.0
                                 alpha:0.10] setFill];
      [circle fill];

      constexpr CGFloat kGlyphInset = 7.0;
      const NSRect inner = NSInsetRect(close_rect, kGlyphInset, kGlyphInset);
      [[NSColor colorWithCalibratedRed:190 / 255.0
                                 green:200 / 255.0
                                  blue:208 / 255.0
                                 alpha:0.9] setStroke];

      NSBezierPath* xPath = [NSBezierPath bezierPath];
      [xPath moveToPoint:NSMakePoint(NSMinX(inner), NSMinY(inner))];
      [xPath lineToPoint:NSMakePoint(NSMaxX(inner), NSMaxY(inner))];
      [xPath moveToPoint:NSMakePoint(NSMaxX(inner), NSMinY(inner))];
      [xPath lineToPoint:NSMakePoint(NSMinX(inner), NSMaxY(inner))];
      [xPath setLineWidth:1.4];
      [xPath setLineCapStyle:NSLineCapStyleRound];
      [xPath stroke];
    }
  }
}

- (void)setToolbarHeight:(int)toolbarHeight sidebarWidth:(int)sidebarWidth {
  _toolbarHeight = toolbarHeight;
  _sidebarWidth = sidebarWidth;
  [self setNeedsDisplay:YES];
}

- (void)setTabCount:(int)count {
  if (count < 0) {
    count = 0;
  }
  if (count == _tabCount) {
    return;
  }

  const NSInteger previousHover = _lastHoveredRow;
  _tabCount = count;

  if (previousHover >= _tabCount) {
    _lastHoveredRow = -1;
    [self invalidateGlyphsForHoveredRowTransitionFrom:previousHover to:-1];
    if (_hoverCallback) {
      _hoverCallback(_context, -1);
    }
  } else {
    [self setNeedsDisplay:YES];
  }
}

- (void)setPinnedSectionHeight:(int)height {
  if (height < 0) {
    height = kPinnedLabelHeightMac + kPinnedEmptyHeightMac;
  }
  _pinnedSectionHeight = height;
  [self setNeedsDisplay:YES];
}

@end

static AvoraSidebarTabClickMonitor* AvoraSidebarTabClickMonitorForRootView(
    NSView* root_view) {
  for (NSView* subview in [root_view subviews]) {
    if ([subview isKindOfClass:[AvoraSidebarTabClickMonitor class]]) {
      return static_cast<AvoraSidebarTabClickMonitor*>(subview);
    }
  }
  return nil;
}

// Pass-through overlay: paints kChrome-colored wedges outside a rounded rect so
// CEF views (which often ignore NSView layer clipping) still read as rounded
// against the window chrome. Frame matches the logical content_well from
// app_browser_avora.cc (no fragile subview search).
@interface AvoraContentWellCornerOverlay : NSView {
 @private
  CGFloat _radius;
  NSColor* _chromeColor;
}

- (instancetype)initWithFrame:(NSRect)frame
                       radius:(CGFloat)radius
                  chromeColor:(NSColor*)chromeColor;
- (void)updateFrame:(NSRect)frame;

@end

@implementation AvoraContentWellCornerOverlay

- (instancetype)initWithFrame:(NSRect)frame
                       radius:(CGFloat)radius
                  chromeColor:(NSColor*)chromeColor {
  self = [super initWithFrame:frame];
  if (self) {
    _radius = radius;
    _chromeColor = [chromeColor copy];
    [self setWantsLayer:YES];
    [self setLayerContentsRedrawPolicy:NSViewLayerContentsRedrawOnSetNeedsDisplay];
    self.layer.opaque = NO;
  }
  return self;
}

- (void)updateFrame:(NSRect)frame {
  [self setFrame:frame];
  [self setNeedsDisplay:YES];
}

- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (NSView*)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  const NSRect b = [self bounds];
  if (NSWidth(b) < 2.0 || NSHeight(b) < 2.0) {
    return;
  }

  CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
  if (!ctx) {
    return;
  }

  const CGFloat lim = std::floor(std::min(NSWidth(b), NSHeight(b)) / 2.0);
  const CGFloat r = std::min(std::max<CGFloat>(_radius, 0.0), lim);

  CGContextSaveGState(ctx);
  CGContextSetAllowsAntialiasing(ctx, true);
  CGContextSetShouldAntialias(ctx, true);

  CGContextBeginPath(ctx);
  CGContextAddRect(ctx, NSRectToCGRect(b));

  CGPathRef rounded =
      CGPathCreateWithRoundedRect(NSRectToCGRect(b), r, r, nullptr);
  CGContextAddPath(ctx, rounded);
  CGPathRelease(rounded);

  CGContextSetFillColorWithColor(ctx, [_chromeColor CGColor]);
  CGContextEOFillPath(ctx);

  CGContextRestoreGState(ctx);
}

@end

static AvoraContentWellCornerOverlay* AvoraContentWellOverlayForRootView(
    NSView* root_view) {
  for (NSView* subview in [root_view subviews]) {
    if ([subview isKindOfClass:[AvoraContentWellCornerOverlay class]]) {
      return static_cast<AvoraContentWellCornerOverlay*>(subview);
    }
  }
  return nil;
}

static NSRect AvoraContentWellOverlayFrame(NSView* root_view,
                                           int toolbar_height,
                                           int sidebar_width,
                                           int content_inset) {
  const NSRect root_bounds = [root_view bounds];
  const CGFloat x = static_cast<CGFloat>(sidebar_width + content_inset);
  const CGFloat w = NSWidth(root_bounds) - x - static_cast<CGFloat>(content_inset);
  const CGFloat h = NSHeight(root_bounds) -
                    static_cast<CGFloat>(toolbar_height + 2 * content_inset);
  const CGFloat logical_y_top =
      static_cast<CGFloat>(toolbar_height + content_inset);
  if ([root_view isFlipped]) {
    return NSMakeRect(x, logical_y_top, std::max<CGFloat>(0, w),
                      std::max<CGFloat>(0, h));
  }
  const CGFloat y = NSHeight(root_bounds) - logical_y_top - std::max<CGFloat>(0, h);
  return NSMakeRect(x, y, std::max<CGFloat>(0, w), std::max<CGFloat>(0, h));
}

// Matches app_browser_avora.cc top nav row: plus is the trailing 36px control.
static NSRect AvoraToolbarPlusHitRect(NSView* root_view, int toolbar_height) {
  const NSRect bounds = [root_view bounds];
  const CGFloat width = NSWidth(bounds);
  const CGFloat height = NSHeight(bounds);
  const CGFloat button = static_cast<CGFloat>(kToolbarNavButtonSizeMac);
  const CGFloat x = width - static_cast<CGFloat>(kToolbarRightInsetMac) - button;
  const CGFloat w = button + static_cast<CGFloat>(kToolbarRightInsetMac);
  const CGFloat h = static_cast<CGFloat>(toolbar_height);
  if ([root_view isFlipped]) {
    return NSMakeRect(x, 0, std::max<CGFloat>(0, w), h);
  }
  return NSMakeRect(x, height - h, std::max<CGFloat>(0, w), h);
}

@interface AvoraToolbarPlusClickMonitor : NSView {
 @private
  NSView* _rootView;
  void* _context;
  avora::ToolbarPlusClickCallback _callback;
  id _eventMonitor;
  int _toolbarHeight;
  BOOL _invoking;
}

- (instancetype)initWithRootView:(NSView*)rootView
                   toolbarHeight:(int)toolbarHeight
                         context:(void*)context
                        callback:(avora::ToolbarPlusClickCallback)callback;
- (void)invokeCallback;

@end

@implementation AvoraToolbarPlusClickMonitor

- (instancetype)initWithRootView:(NSView*)rootView
                   toolbarHeight:(int)toolbarHeight
                         context:(void*)context
                        callback:(avora::ToolbarPlusClickCallback)callback {
  self = [super initWithFrame:AvoraToolbarPlusHitRect(rootView, toolbarHeight)];
  if (self) {
    _rootView = rootView;
    _toolbarHeight = toolbarHeight;
    _context = context;
    _callback = callback;
    _invoking = NO;
    [self setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [self setOpaque:NO];
    [self setWantsLayer:NO];
    __block AvoraToolbarPlusClickMonitor* weakSelf = self;
    _eventMonitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                     handler:^NSEvent*(NSEvent* event) {
                                       AvoraToolbarPlusClickMonitor* strongSelf =
                                           weakSelf;
                                       if (!strongSelf ||
                                           [event window] !=
                                               [strongSelf->_rootView window] ||
                                           !strongSelf->_callback) {
                                         return event;
                                       }

                                       const NSPoint point =
                                           [strongSelf->_rootView
                                               convertPoint:
                                                   [event locationInWindow]
                                                 fromView:nil];
                                       const NSRect hit = AvoraToolbarPlusHitRect(
                                           strongSelf->_rootView,
                                           strongSelf->_toolbarHeight);
                                       if (!NSPointInRect(point, hit)) {
                                         return event;
                                       }

                                       [strongSelf invokeCallback];
                                       // Consume the click so the frameless
                                       // toolbar drag region cannot steal it.
                                       return nil;
                                     }];
  }
  return self;
}

- (void)dealloc {
  if (_eventMonitor) {
    [NSEvent removeMonitor:_eventMonitor];
    _eventMonitor = nil;
  }
  [super dealloc];
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  (void)event;
  return YES;
}

- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (NSView*)hitTest:(NSPoint)point {
  return NSPointInRect(point, [self bounds]) ? self : nil;
}

- (void)invokeCallback {
  if (_invoking || !_callback) {
    return;
  }
  _invoking = YES;
  _callback(_context);
  _invoking = NO;
}

- (void)mouseDown:(NSEvent*)event {
  (void)event;
  [self invokeCallback];
}

@end

static AvoraToolbarPlusClickMonitor* AvoraToolbarPlusClickMonitorForRootView(
    NSView* root_view) {
  for (NSView* subview in [root_view subviews]) {
    if ([subview isKindOfClass:[AvoraToolbarPlusClickMonitor class]]) {
      return static_cast<AvoraToolbarPlusClickMonitor*>(subview);
    }
  }
  return nil;
}

// Overlay that paints on TOP of the CEF textfield to mask its inner border.
// Draws a filled frame (chip background color) covering the border area, then
// an outer stroke. The interior is cut out so text remains visible.
@interface AvoraAddressChipOutline : NSView {
 @private
  CGFloat _cornerRadius;
  CGFloat _frameWidth;
}
- (instancetype)initWithFrame:(NSRect)frame
                 cornerRadius:(CGFloat)radius
                   frameWidth:(CGFloat)fw;
- (void)updateFrame:(NSRect)frame;
@end

@implementation AvoraAddressChipOutline

- (instancetype)initWithFrame:(NSRect)frame
                 cornerRadius:(CGFloat)radius
                   frameWidth:(CGFloat)fw {
  self = [super initWithFrame:frame];
  if (self) {
    _cornerRadius = radius;
    _frameWidth = fw;
    [self setWantsLayer:YES];
    [self setLayerContentsRedrawPolicy:NSViewLayerContentsRedrawOnSetNeedsDisplay];
    self.layer.opaque = NO;
  }
  return self;
}

- (void)updateFrame:(NSRect)frame {
  [self setFrame:frame];
  [self setNeedsDisplay:YES];
}

- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (NSView*)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  const NSRect b = [self bounds];
  if (NSWidth(b) < 2.0 || NSHeight(b) < 2.0) {
    return;
  }
  const NSRect strokeRect = NSInsetRect(b, 0.5, 0.5);
  NSBezierPath* path =
      [NSBezierPath bezierPathWithRoundedRect:strokeRect
                                      xRadius:_cornerRadius
                                      yRadius:_cornerRadius];
  [[NSColor colorWithSRGBRed:0.36
                       green:0.40
                        blue:0.44
                       alpha:0.60] setStroke];
  [path setLineWidth:1.0];
  [path stroke];
}

@end

static AvoraAddressChipOutline* AvoraAddressChipOutlineForRootView(
    NSView* root_view) {
  for (NSView* subview in [root_view subviews]) {
    if ([subview isKindOfClass:[AvoraAddressChipOutline class]]) {
      return static_cast<AvoraAddressChipOutline*>(subview);
    }
  }
  return nil;
}

static NSRect AvoraAddressChipFrame(NSView* root_view,
                                    int sidebar_width,
                                    int sidebar_top_inset,
                                    int address_row_height,
                                    int horizontal_pad) {
  const NSRect bounds = [root_view bounds];
  const int chip_height = address_row_height - 8;
  const CGFloat x = static_cast<CGFloat>(horizontal_pad);
  const CGFloat w = static_cast<CGFloat>(sidebar_width - 2 * horizontal_pad);
  const CGFloat h = static_cast<CGFloat>(chip_height);
  const CGFloat logical_y_top =
      static_cast<CGFloat>(sidebar_top_inset +
                           (address_row_height - chip_height) / 2);
  if ([root_view isFlipped]) {
    return NSMakeRect(x, logical_y_top, w, h);
  }
  const CGFloat y = NSHeight(bounds) - logical_y_top - h;
  return NSMakeRect(x, y, w, h);
}

// ---------------------------------------------------------------------------
// DevTools splitter: draggable divider between web surface and DevTools panel.
// ---------------------------------------------------------------------------

constexpr CGFloat kDevToolsSplitterHitWidth = 16.0;

@interface AvoraDevToolsSplitter : NSView {
 @private
  void* _ctx;
  avora::DevToolsSplitterCallback _cb;
  int _webSurfaceWidth;
  int _startWebSurfaceWidth;
  int _minWidth;
  int _maxWidth;
  CGFloat _startMouseX;
  NSTrackingArea* _trackingArea;
  id _eventMonitor;
  BOOL _hovering;
  BOOL _dragging;
}

- (instancetype)initWithFrame:(NSRect)frame
              webSurfaceWidth:(int)webSurfaceWidth
                     minWidth:(int)minWidth
                     maxWidth:(int)maxWidth
                      context:(void*)context
                     callback:(avora::DevToolsSplitterCallback)callback;
- (void)setWebSurfaceWidth:(int)width;
- (void)setMinWidth:(int)minWidth maxWidth:(int)maxWidth;
- (BOOL)isDragging;
- (void)beginResizeWithEvent:(NSEvent*)event;
- (void)updateResizeWithEvent:(NSEvent*)event;
- (void)endResize;

@end

@implementation AvoraDevToolsSplitter

- (instancetype)initWithFrame:(NSRect)frame
              webSurfaceWidth:(int)webSurfaceWidth
                     minWidth:(int)minWidth
                     maxWidth:(int)maxWidth
                      context:(void*)context
                     callback:(avora::DevToolsSplitterCallback)callback {
  self = [super initWithFrame:frame];
  if (self) {
    _webSurfaceWidth = webSurfaceWidth;
    _minWidth = minWidth;
    _maxWidth = maxWidth;
    _ctx = context;
    _cb = callback;
    [self setWantsLayer:YES];
    self.layer.backgroundColor = [[NSColor clearColor] CGColor];
    __block AvoraDevToolsSplitter* weakSelf = self;
    _eventMonitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown |
                                             NSEventMaskLeftMouseDragged |
                                             NSEventMaskLeftMouseUp
                                     handler:^NSEvent*(NSEvent* event) {
                                       AvoraDevToolsSplitter* s = weakSelf;
                                       if (!s ||
                                           [event window] != [s window]) {
                                         return event;
                                       }
                                       if ([event type] ==
                                           NSEventTypeLeftMouseDown) {
                                         const NSPoint point =
                                             [[s superview]
                                                 convertPoint:
                                                     [event locationInWindow]
                                                   fromView:nil];
                                         if (NSPointInRect(point,
                                                           [s frame])) {
                                           [s beginResizeWithEvent:event];
                                           return nil;
                                         }
                                         return event;
                                       }
                                       if ([event type] ==
                                           NSEventTypeLeftMouseDragged) {
                                         if ([s isDragging]) {
                                           [s updateResizeWithEvent:event];
                                           return nil;
                                         }
                                         return event;
                                       }
                                       if ([event type] ==
                                           NSEventTypeLeftMouseUp) {
                                         if ([s isDragging]) {
                                           [s endResize];
                                           return nil;
                                         }
                                       }
                                       return event;
                                     }];
  }
  return self;
}

- (void)dealloc {
  if (_eventMonitor) {
    [NSEvent removeMonitor:_eventMonitor];
    _eventMonitor = nil;
  }
  [super dealloc];
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (NSView*)hitTest:(NSPoint)point {
  if (!NSPointInRect(point, [self frame])) {
    return nil;
  }
  return self;
}

- (void)setWebSurfaceWidth:(int)width {
  _webSurfaceWidth = width;
}

- (void)setMinWidth:(int)minWidth maxWidth:(int)maxWidth {
  _minWidth = minWidth;
  _maxWidth = maxWidth;
}

- (BOOL)isDragging {
  return _dragging;
}

- (void)resetCursorRects {
  [self addCursorRect:[self bounds] cursor:[NSCursor resizeLeftRightCursor]];
}

- (void)updateTrackingAreas {
  if (_trackingArea) {
    [self removeTrackingArea:_trackingArea];
  }
  _trackingArea = [[NSTrackingArea alloc]
      initWithRect:[self bounds]
           options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways |
                   NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:_trackingArea];
  [super updateTrackingAreas];
}

- (void)mouseEntered:(NSEvent*)event {
  _hovering = YES;
  [[NSCursor resizeLeftRightCursor] push];
  [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event {
  _hovering = NO;
  [NSCursor pop];
  [self setNeedsDisplay:YES];
}

- (void)beginResizeWithEvent:(NSEvent*)event {
  [[self window] makeFirstResponder:self];
  _startMouseX = [event locationInWindow].x;
  _startWebSurfaceWidth = _webSurfaceWidth;
  _dragging = YES;
  [[NSCursor resizeLeftRightCursor] push];
  [self setNeedsDisplay:YES];
}

- (void)updateResizeWithEvent:(NSEvent*)event {
  const CGFloat delta = [event locationInWindow].x - _startMouseX;
  const int nextWidth =
      std::clamp(static_cast<int>(_startWebSurfaceWidth + delta),
                 _minWidth, _maxWidth);
  if (nextWidth == _webSurfaceWidth) {
    return;
  }
  _webSurfaceWidth = nextWidth;
  if (_cb) {
    _cb(_ctx, nextWidth);
  }
}

- (void)endResize {
  _dragging = NO;
  [NSCursor pop];
  [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event {
  [self endResize];
}

- (void)drawRect:(NSRect)dirtyRect {
  constexpr CGFloat kGripHeight = 44.0;
  constexpr CGFloat kGripWidth = 3.0;
  NSColor* gripColor = nil;
  if (_dragging) {
    gripColor = [NSColor colorWithCalibratedRed:0.82
                                          green:0.47
                                           blue:0.12
                                          alpha:1.0];
  } else if (_hovering) {
    gripColor = [NSColor colorWithCalibratedRed:0.72
                                          green:0.76
                                           blue:0.78
                                          alpha:0.78];
  } else {
    gripColor = nil;
  }
  if (!gripColor) {
    return;
  }
  [gripColor setFill];
  const NSRect bounds = [self bounds];
  const NSRect gripRect =
      NSMakeRect(NSMidX(bounds) - kGripWidth / 2.0,
                 NSMidY(bounds) - kGripHeight / 2.0, kGripWidth, kGripHeight);
  NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:gripRect
                                                       xRadius:kGripWidth / 2.0
                                                       yRadius:kGripWidth / 2.0];
  [path fill];
}

@end

static AvoraDevToolsSplitter* AvoraDevToolsSplitterForRootView(
    NSView* root_view) {
  for (NSView* subview in [root_view subviews]) {
    if ([subview isKindOfClass:[AvoraDevToolsSplitter class]]) {
      return static_cast<AvoraDevToolsSplitter*>(subview);
    }
  }
  return nil;
}

static NSRect AvoraDevToolsSplitterFrame(NSView* root_view,
                                         int sidebar_width,
                                         int content_inset,
                                         int web_surface_width,
                                         int gap_width) {
  const NSRect bounds = [root_view bounds];
  const CGFloat gap_center =
      static_cast<CGFloat>(sidebar_width + content_inset + web_surface_width) +
      static_cast<CGFloat>(gap_width) / 2.0;
  const CGFloat x = gap_center - kDevToolsSplitterHitWidth / 2.0;
  return NSMakeRect(x, 0, kDevToolsSplitterHitWidth, NSHeight(bounds));
}

// ---------------------------------------------------------------------------
// Space navigation: trackpad swipe + mouse back/forward buttons.
// direction: -1 = previous space, +1 = next space
// ---------------------------------------------------------------------------

@interface AvoraSpaceNavigationMonitor : NSView {
 @private
  NSView* _spaceRootView;
  void* _spaceCtx;
  avora::SpaceNavigateCallback _spaceCb;
  id _spaceSwipeMon;
  id _spaceMouseMon;
}

- (instancetype)initWithRootView:(NSView*)rootView
                         context:(void*)ctx
                        callback:(avora::SpaceNavigateCallback)cb;

@end

@implementation AvoraSpaceNavigationMonitor

- (instancetype)initWithRootView:(NSView*)rootView
                         context:(void*)ctx
                        callback:(avora::SpaceNavigateCallback)cb {
  self = [super initWithFrame:NSMakeRect(0, 0, 0, 0)];
  if (self) {
    _spaceRootView = rootView;
    _spaceCtx = ctx;
    _spaceCb = cb;

    __block AvoraSpaceNavigationMonitor* weakSelf = self;

    _spaceSwipeMon = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskSwipe
                                     handler:^NSEvent*(NSEvent* event) {
                                       AvoraSpaceNavigationMonitor* s = weakSelf;
                                       if (!s || !s->_spaceCb) {
                                         return event;
                                       }
                                       if ([event window] !=
                                           [s->_spaceRootView window]) {
                                         return event;
                                       }
                                       CGFloat dx = [event deltaX];
                                       if (dx > 0.1) {
                                         s->_spaceCb(s->_spaceCtx, -1);
                                         return nil;
                                       } else if (dx < -0.1) {
                                         s->_spaceCb(s->_spaceCtx, 1);
                                         return nil;
                                       }
                                       return event;
                                     }];

    _spaceMouseMon = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskOtherMouseDown
                                     handler:^NSEvent*(NSEvent* event) {
                                       AvoraSpaceNavigationMonitor* s = weakSelf;
                                       if (!s || !s->_spaceCb) {
                                         return event;
                                       }
                                       if ([event window] !=
                                           [s->_spaceRootView window]) {
                                         return event;
                                       }
                                       NSInteger btn = [event buttonNumber];
                                       if (btn == 3) {
                                         s->_spaceCb(s->_spaceCtx, -1);
                                         return nil;
                                       } else if (btn == 4) {
                                         s->_spaceCb(s->_spaceCtx, 1);
                                         return nil;
                                       }
                                       return event;
                                     }];
  }
  return self;
}

- (void)dealloc {
  if (_spaceSwipeMon) {
    [NSEvent removeMonitor:_spaceSwipeMon];
    _spaceSwipeMon = nil;
  }
  if (_spaceMouseMon) {
    [NSEvent removeMonitor:_spaceMouseMon];
    _spaceMouseMon = nil;
  }
  [super dealloc];
}

- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (NSView*)hitTest:(NSPoint)point {
  return nil;
}

@end

static AvoraSpaceNavigationMonitor* AvoraSpaceNavMonitorForRootView(
    NSView* root_view) {
  for (NSView* subview in [root_view subviews]) {
    if ([subview isKindOfClass:[AvoraSpaceNavigationMonitor class]]) {
      return static_cast<AvoraSpaceNavigationMonitor*>(subview);
    }
  }
  return nil;
}

// ---------------------------------------------------------------------------

namespace avora {

void InstallAddressChipOutline(CefWindowHandle window_handle,
                               int sidebar_width,
                               int sidebar_top_inset,
                               int address_row_height,
                               int horizontal_pad) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  const NSRect frame = AvoraAddressChipFrame(
      root_view, sidebar_width, sidebar_top_inset, address_row_height,
      horizontal_pad);

  AvoraAddressChipOutline* outline =
      AvoraAddressChipOutlineForRootView(root_view);
  if (!outline) {
    outline = [[AvoraAddressChipOutline alloc] initWithFrame:frame
                                                cornerRadius:8.0
                                                  frameWidth:8.0];
    [root_view addSubview:outline positioned:NSWindowAbove relativeTo:nil];
  } else {
    [outline updateFrame:frame];
  }
}

void UpdateAddressChipOutline(CefWindowHandle window_handle,
                              int sidebar_width,
                              int sidebar_top_inset,
                              int address_row_height,
                              int horizontal_pad) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraAddressChipOutline* outline =
      AvoraAddressChipOutlineForRootView(root_view);
  if (!outline) {
    return;
  }

  const NSRect frame = AvoraAddressChipFrame(
      root_view, sidebar_width, sidebar_top_inset, address_row_height,
      horizontal_pad);
  [outline updateFrame:frame];
}

void RemoveAddressChipOutline(CefWindowHandle window_handle) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraAddressChipOutline* outline =
      AvoraAddressChipOutlineForRootView(root_view);
  if (outline) {
    [outline removeFromSuperview];
  }
}

void InstallSidebarResizeHandle(CefWindowHandle window_handle,
                                int toolbar_height,
                                int sidebar_width,
                                int min_width,
                                int max_width,
                                void* context,
                                SidebarResizeCallback callback) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraSidebarResizeHandle* handle = AvoraSidebarHandleForRootView(root_view);
  if (!handle) {
    handle = [[AvoraSidebarResizeHandle alloc]
        initWithFrame:AvoraSidebarHandleFrame(root_view, toolbar_height,
                                              sidebar_width)
         sidebarWidth:sidebar_width
             minWidth:min_width
             maxWidth:max_width
              context:context
             callback:callback];
    [handle setAutoresizingMask:NSViewHeightSizable];
    [root_view addSubview:handle positioned:NSWindowAbove relativeTo:nil];
  } else {
    [handle setFrame:AvoraSidebarHandleFrame(root_view, toolbar_height,
                                             sidebar_width)];
    [handle setSidebarWidth:sidebar_width];
  }
}

void UpdateSidebarResizeHandle(CefWindowHandle window_handle,
                               int toolbar_height,
                               int sidebar_width) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraSidebarResizeHandle* handle = AvoraSidebarHandleForRootView(root_view);
  if (!handle) {
    return;
  }

  [handle setFrame:AvoraSidebarHandleFrame(root_view, toolbar_height,
                                           sidebar_width)];
  [handle setSidebarWidth:sidebar_width];
  [handle setNeedsDisplay:YES];
}

void RemoveSidebarResizeHandle(CefWindowHandle window_handle) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraSidebarResizeHandle* handle = AvoraSidebarHandleForRootView(root_view);
  if (handle) {
    [handle removeFromSuperview];
  }
}

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
                                   ToolbarPlusClickCallback extension_click_callback) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  if (AvoraSidebarTabClickMonitorForRootView(root_view)) {
    return;
  }

  auto monitor = [[AvoraSidebarTabClickMonitor alloc]
      initWithRootView:root_view
         toolbarHeight:toolbar_height
          sidebarWidth:sidebar_width
               context:context
              callback:callback
         hoverCallback:hover_callback
         closeCallback:close_callback
        newTabCallback:new_tab_callback
          navCallback:nav_callback
  addressClickCallback:address_click_callback
           pinCallback:pin_callback
  extensionClickCallback:extension_click_callback];
  AvoraSidebarResizeHandle* handle = AvoraSidebarHandleForRootView(root_view);
  [root_view addSubview:monitor positioned:NSWindowAbove relativeTo:nil];
  if (handle) {
    [root_view addSubview:handle positioned:NSWindowAbove relativeTo:monitor];
  }
}

void UpdateSidebarTabClickMonitor(CefWindowHandle window_handle,
                                  int toolbar_height,
                                  int sidebar_width) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraSidebarTabClickMonitor* monitor =
      AvoraSidebarTabClickMonitorForRootView(root_view);
  if (!monitor) {
    return;
  }

  [monitor setFrame:[root_view bounds]];
  [monitor setToolbarHeight:toolbar_height sidebarWidth:sidebar_width];
}

void UpdateSidebarTabClickMonitorTabCount(CefWindowHandle window_handle,
                                          int tab_count) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraSidebarTabClickMonitor* monitor =
      AvoraSidebarTabClickMonitorForRootView(root_view);
  if (!monitor) {
    return;
  }

  [monitor setTabCount:tab_count];
}

void UpdateSidebarTabClickMonitorPinnedHeight(CefWindowHandle window_handle,
                                              int pinned_height) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraSidebarTabClickMonitor* monitor =
      AvoraSidebarTabClickMonitorForRootView(root_view);
  if (!monitor) {
    return;
  }

  [monitor setPinnedSectionHeight:pinned_height];
}

void RemoveSidebarTabClickMonitor(CefWindowHandle window_handle) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraSidebarTabClickMonitor* monitor =
      AvoraSidebarTabClickMonitorForRootView(root_view);
  if (monitor) {
    [monitor removeFromSuperview];
  }
}

void InstallToolbarPlusClickMonitor(CefWindowHandle window_handle,
                                    int toolbar_height,
                                    void* context,
                                    ToolbarPlusClickCallback callback) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view || !callback) {
    return;
  }

  if (AvoraToolbarPlusClickMonitorForRootView(root_view)) {
    return;
  }

  auto monitor = [[AvoraToolbarPlusClickMonitor alloc]
      initWithRootView:root_view
         toolbarHeight:toolbar_height
               context:context
              callback:callback];
  [root_view addSubview:monitor positioned:NSWindowAbove relativeTo:nil];
}

void RemoveToolbarPlusClickMonitor(CefWindowHandle window_handle) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraToolbarPlusClickMonitor* monitor =
      AvoraToolbarPlusClickMonitorForRootView(root_view);
  if (monitor) {
    [monitor removeFromSuperview];
  }
}

void ApplyContentWellCornerRadius(CefWindowHandle window_handle,
                                  int toolbar_height,
                                  int sidebar_width,
                                  int content_inset,
                                  double radius) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  const NSRect frame = AvoraContentWellOverlayFrame(
      root_view, toolbar_height, sidebar_width, content_inset);
  if (NSWidth(frame) < 1.0 || NSHeight(frame) < 1.0) {
    return;
  }

  NSColor* chromeColor = [NSColor colorWithSRGBRed:24.0 / 255.0
                                             green:28.0 / 255.0
                                              blue:32.0 / 255.0
                                             alpha:1.0];

  AvoraContentWellCornerOverlay* overlay =
      AvoraContentWellOverlayForRootView(root_view);
  if (!overlay) {
    overlay = [[AvoraContentWellCornerOverlay alloc]
        initWithFrame:frame
               radius:radius
          chromeColor:chromeColor];
    AvoraSidebarResizeHandle* handle = AvoraSidebarHandleForRootView(root_view);
    if (handle) {
      [root_view addSubview:overlay positioned:NSWindowBelow relativeTo:handle];
    } else {
      [root_view addSubview:overlay positioned:NSWindowAbove relativeTo:nil];
    }
  } else {
    [overlay updateFrame:frame];
  }
}

void RemoveContentWellCornerOverlay(CefWindowHandle window_handle) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraContentWellCornerOverlay* overlay =
      AvoraContentWellOverlayForRootView(root_view);
  if (overlay) {
    [overlay removeFromSuperview];
  }
}

void InstallDevToolsSplitter(CefWindowHandle window_handle,
                             int sidebar_width,
                             int content_inset,
                             int web_surface_width,
                             int gap_width,
                             int min_width,
                             int max_width,
                             void* context,
                             DevToolsSplitterCallback callback) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraDevToolsSplitter* splitter =
      AvoraDevToolsSplitterForRootView(root_view);
  if (!splitter) {
    splitter = [[AvoraDevToolsSplitter alloc]
        initWithFrame:AvoraDevToolsSplitterFrame(root_view, sidebar_width,
                                                 content_inset,
                                                 web_surface_width, gap_width)
      webSurfaceWidth:web_surface_width
             minWidth:min_width
             maxWidth:max_width
              context:context
             callback:callback];
    [root_view addSubview:splitter positioned:NSWindowAbove relativeTo:nil];
  } else {
    [splitter setFrame:AvoraDevToolsSplitterFrame(root_view, sidebar_width,
                                                  content_inset,
                                                  web_surface_width,
                                                  gap_width)];
    [splitter setWebSurfaceWidth:web_surface_width];
    [splitter setMinWidth:min_width maxWidth:max_width];
  }
}

void UpdateDevToolsSplitter(CefWindowHandle window_handle,
                            int sidebar_width,
                            int content_inset,
                            int web_surface_width,
                            int gap_width) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraDevToolsSplitter* splitter =
      AvoraDevToolsSplitterForRootView(root_view);
  if (!splitter) {
    return;
  }

  [splitter setFrame:AvoraDevToolsSplitterFrame(root_view, sidebar_width,
                                                content_inset,
                                                web_surface_width, gap_width)];
  [splitter setWebSurfaceWidth:web_surface_width];
  [splitter setNeedsDisplay:YES];
}

void RemoveDevToolsSplitter(CefWindowHandle window_handle) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraDevToolsSplitter* splitter =
      AvoraDevToolsSplitterForRootView(root_view);
  if (splitter) {
    [splitter removeFromSuperview];
  }
}

static void AvoraDisableNativeFocusRingsOnView(NSView* view) {
  if (!view) {
    return;
  }
  [view setFocusRingType:NSFocusRingTypeNone];
  if ([view isKindOfClass:[NSControl class]]) {
    NSCell* cell = [(NSControl*)view cell];
    [cell setFocusRingType:NSFocusRingTypeNone];
  }
  if ([view isKindOfClass:[NSTextField class]]) {
    NSTextField* field = (NSTextField*)view;
    [field setBordered:NO];
    [field setBezeled:NO];
    [field setFocusRingType:NSFocusRingTypeNone];
  }
  for (NSView* child in [view subviews]) {
    AvoraDisableNativeFocusRingsOnView(child);
  }
}

void DisableNativeFocusRings(CefWindowHandle window_handle) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }
  AvoraDisableNativeFocusRingsOnView(root_view);
  NSWindow* window = [root_view window];
  if (!window) {
    return;
  }
  AvoraDisableNativeFocusRingsOnView([window contentView]);
  id responder = [window firstResponder];
  if ([responder isKindOfClass:[NSView class]]) {
    AvoraDisableNativeFocusRingsOnView((NSView*)responder);
  }
}

void InstallSpaceNavigationMonitor(CefWindowHandle window_handle,
                                   void* context,
                                   SpaceNavigateCallback callback) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view || !callback) {
    return;
  }

  if (AvoraSpaceNavMonitorForRootView(root_view)) {
    return;
  }

  auto monitor = [[AvoraSpaceNavigationMonitor alloc]
      initWithRootView:root_view
               context:context
              callback:callback];
  [root_view addSubview:monitor positioned:NSWindowAbove relativeTo:nil];
}

void RemoveSpaceNavigationMonitor(CefWindowHandle window_handle) {
  NSView* root_view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(window_handle);
  if (!root_view) {
    return;
  }

  AvoraSpaceNavigationMonitor* monitor =
      AvoraSpaceNavMonitorForRootView(root_view);
  if (monitor) {
    [monitor removeFromSuperview];
  }
}

}  // namespace avora
