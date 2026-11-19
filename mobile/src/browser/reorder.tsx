import * as Haptics from 'expo-haptics';
import { useCallback, useLayoutEffect, useMemo, useRef, useState } from 'react';
import {
  Animated,
  type LayoutChangeEvent,
  type LayoutRectangle,
  PanResponder,
  type ScrollViewInstance,
} from 'react-native';

// How close to the scroll view's top or bottom edge, in points, a dragged item
// has to be before the list starts scrolling under it, and how fast it goes.
const EDGE = 70;
const MAX_SPEED = 14;

export type AutoScroller = {
  // The scroll view's current offset, kept up to date by its onScroll.
  y: () => number;
  // Scrolls toward the edge the finger is near; returns how far it moved.
  step: (pageY: number) => number;
  setLocked: (locked: boolean) => void;
};

// Wires a ScrollView up so a drag inside it can scroll it.
export function useAutoScroller(scrollRef: React.RefObject<ScrollViewInstance | null>) {
  const [locked, setLocked] = useState(false);
  const metrics = useRef({ y: 0, top: 0, height: 0, content: 0 });

  const scroller = useMemo<AutoScroller>(
    () => ({
      y: () => metrics.current.y,
      step: (pageY) => {
        const m = metrics.current;
        let speed = 0;
        if (pageY < m.top + EDGE) speed = -MAX_SPEED * Math.min(1, (m.top + EDGE - pageY) / EDGE);
        else if (pageY > m.top + m.height - EDGE)
          speed = MAX_SPEED * Math.min(1, (pageY - (m.top + m.height - EDGE)) / EDGE);
        if (!speed) return 0;
        const next = Math.max(0, Math.min(m.content - m.height, m.y + speed));
        const moved = next - m.y;
        if (!moved) return 0;
        m.y = next;
        scrollRef.current?.scrollTo({ y: next, animated: false });
        return moved;
      },
      setLocked: (lock) => {
        // Measure where the scroll view sits now, not at its first layout:
        // the switcher is still sliding in then, which would put the top
        // edge too low and scroll lists the finger isn't near.
        if (lock) {
          scrollRef.current?.measureInWindow((_x, y, _w, h) => {
            metrics.current.top = y;
            metrics.current.height = h;
          });
        }
        setLocked(lock);
      },
    }),
    [scrollRef],
  );

  const scrollProps = {
    scrollEnabled: !locked,
    scrollEventThrottle: 16,
    onScroll: (e: { nativeEvent: { contentOffset: { y: number } } }) => {
      metrics.current.y = e.nativeEvent.contentOffset.y;
    },
    onContentSizeChange: (_: number, h: number) => {
      metrics.current.content = h;
    },
    onLayout: () => {
      scrollRef.current?.measureInWindow((_x, y, _w, h) => {
        metrics.current.top = y;
        metrics.current.height = h;
      });
    },
  };

  return { scroller, scrollProps };
}

type Drag = {
  key: string;
  from: number;
  to: number;
  moved: boolean;
  dx: number;
  dy: number;
  pageY: number;
  startScroll: number;
  frame: number | null;
};

// Long-press-and-drag reordering for a list (rows of any height, stacked) or a
// grid (equal tiles that wrap). Long-press lifts an item; moving the finger
// drags it while the others make room; letting go without moving hands the
// long-press back to `onLongPress`, so the item's action sheet still works.
export function useReorder({
  keys,
  mode,
  onMove,
  onLongPress,
  scroller,
}: {
  keys: string[];
  mode: 'list' | 'grid';
  onMove: (key: string, by: number) => void;
  onLongPress: (key: string) => void;
  scroller?: AutoScroller;
}) {
  const [layouts] = useState(() => new Map<string, LayoutRectangle>());
  const [offsets] = useState(() => new Map<string, Animated.ValueXY>());
  const [scale] = useState(() => new Animated.Value(1));
  const [lifted, setLifted] = useState<string | null>(null);
  const drag = useRef<Drag | null>(null);
  const latest = useRef({ keys, mode, onMove, onLongPress, scroller });
  useLayoutEffect(() => {
    latest.current = { keys, mode, onMove, onLongPress, scroller };
  });

  const offsetOf = (key: string) => {
    let v = offsets.get(key);
    if (!v) {
      v = new Animated.ValueXY();
      offsets.set(key, v);
    }
    return v;
  };

  // Once the new order renders, every item already sits where its offset was
  // carrying it, so the offsets go back to zero.
  const order = keys.join('\n');
  useLayoutEffect(() => {
    for (const v of offsets.values()) v.setValue({ x: 0, y: 0 });
  }, [order, offsets]);

  const handlers = useMemo(() => {
    const rects = () => latest.current.keys.map((k) => layouts.get(k));

    // Where the dragged item would land, given where its centre is now.
    const targetFor = (d: Drag) => {
      const all = rects();
      const self = all[d.from];
      if (!self) return d.from;
      const cx = self.x + self.width / 2 + d.dx;
      const cy = self.y + self.height / 2 + d.dy;
      let best = d.from;
      let bestDist = Infinity;
      all.forEach((r, i) => {
        if (!r) return;
        const dist =
          latest.current.mode === 'list'
            ? Math.abs(r.y + r.height / 2 - cy)
            : Math.hypot(r.x + r.width / 2 - cx, r.y + r.height / 2 - cy);
        if (dist < bestDist) {
          best = i;
          bestDist = dist;
        }
      });
      return best;
    };

    // Where item `j` sits, relative to its own layout, when `from` moves to `to`.
    const shiftFor = (j: number, from: number, to: number) => {
      const all = rects();
      let n = j;
      if (from < to && j > from && j <= to) n = j - 1;
      else if (to < from && j >= to && j < from) n = j + 1;
      if (n === j) return { x: 0, y: 0 };
      if (latest.current.mode === 'list') {
        const h = all[from]?.height ?? 0;
        return { x: 0, y: n < j ? -h : h };
      }
      const a = all[j];
      const b = all[n];
      return a && b ? { x: b.x - a.x, y: b.y - a.y } : { x: 0, y: 0 };
    };

    // Where the dragged item comes to rest, relative to its own layout.
    const restFor = (from: number, to: number) => {
      const all = rects();
      const self = all[from];
      const target = all[to];
      if (!self || !target || from === to) return { x: 0, y: 0 };
      if (latest.current.mode === 'list') {
        return { x: 0, y: to > from ? target.y + target.height - self.height - self.y : target.y - self.y };
      }
      return { x: target.x - self.x, y: target.y - self.y };
    };

    const apply = () => {
      const d = drag.current;
      if (!d) return;
      const scrolled = (latest.current.scroller?.y() ?? 0) - d.startScroll;
      offsetOf(d.key).setValue({ x: d.dx, y: d.dy + scrolled });
      const to = targetFor({ ...d, dy: d.dy + scrolled });
      if (to === d.to) return;
      d.to = to;
      Haptics.selectionAsync().catch(() => {});
      latest.current.keys.forEach((k, j) => {
        if (j === d.from) return;
        Animated.spring(offsetOf(k), {
          toValue: shiftFor(j, d.from, to),
          useNativeDriver: false,
          speed: 28,
          bounciness: 0,
        }).start();
      });
    };

    const tick = () => {
      const d = drag.current;
      if (!d) return;
      if (latest.current.scroller?.step(d.pageY)) apply();
      d.frame = requestAnimationFrame(tick);
    };

    const end = () => {
      const d = drag.current;
      drag.current = null;
      latest.current.scroller?.setLocked(false);
      Animated.spring(scale, { toValue: 1, useNativeDriver: false, speed: 24, bounciness: 0 }).start();
      if (!d) return;
      if (d.frame !== null) cancelAnimationFrame(d.frame);
      Animated.timing(offsetOf(d.key), {
        toValue: restFor(d.from, d.to),
        duration: 140,
        useNativeDriver: false,
      }).start(() => {
        setLifted(null);
      if (d.to !== d.from) latest.current.onMove(d.key, d.to - d.from);
      });
    };

    // The handlers only read refs during a gesture, never while rendering.
    // eslint-disable-next-line react-hooks/refs
    const pan = PanResponder.create({
      // Only a lifted item takes the touch, and then ahead of its children
      // (a today row's swipe-to-close) and the Pressable underneath.
      onMoveShouldSetPanResponderCapture: (_, g) => {
        const d = drag.current;
        if (!d || Math.abs(g.dx) + Math.abs(g.dy) < 3) return false;
        d.moved = true;
        return true;
      },
      onPanResponderTerminationRequest: () => false,
      onPanResponderGrant: (_, g) => {
        const d = drag.current;
        if (!d) return;
        // Start from where the finger is, or the first frame would read the
        // placeholder 0 as "at the top edge" and scroll.
        d.pageY = g.moveY || g.y0;
        d.startScroll = latest.current.scroller?.y() ?? 0;
        d.frame = requestAnimationFrame(tick);
      },
      onPanResponderMove: (_, g) => {
        const d = drag.current;
        if (!d) return;
        d.dx = latest.current.mode === 'list' ? 0 : g.dx;
        d.dy = g.dy;
        d.pageY = g.moveY;
        apply();
      },
      onPanResponderRelease: end,
      onPanResponderTerminate: end,
    });

    const lift = (key: string) => {
      const from = latest.current.keys.indexOf(key);
      if (from < 0) return;
      drag.current = {
        key,
        from,
        to: from,
        moved: false,
        dx: 0,
        dy: 0,
        pageY: 0,
        startScroll: latest.current.scroller?.y() ?? 0,
        frame: null,
      };
      setLifted(key);
      latest.current.scroller?.setLocked(true);
      Haptics.impactAsync(Haptics.ImpactFeedbackStyle.Medium).catch(() => {});
      Animated.spring(scale, { toValue: 1.04, useNativeDriver: false, speed: 24, bounciness: 6 }).start();
    };

    // The finger came up. If it never moved, this was a plain long-press.
    const pressOut = (key: string) => {
      const d = drag.current;
      if (!d || d.key !== key || d.moved) return;
      drag.current = null;
      setLifted(null);
      latest.current.scroller?.setLocked(false);
      Animated.spring(scale, { toValue: 1, useNativeDriver: false, speed: 24, bounciness: 0 }).start();
      latest.current.onLongPress(key);
    };

    return { pan, lift, pressOut };
    // The handlers read everything else through refs, at gesture time.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [layouts, scale]);

  // Spread onto the view wrapping each item, and hand the press callbacks to
  // the item's Pressable.
  const item = (key: string) => {
    const offset = offsetOf(key);
    const isLifted = lifted === key;
    return {
      wrapper: {
        ...handlers.pan.panHandlers,
        onLayout: (e: LayoutChangeEvent) => {
          layouts.set(key, e.nativeEvent.layout);
        },
        style: {
          zIndex: isLifted ? 10 : 0,
          shadowColor: '#000',
          shadowOpacity: isLifted ? 0.35 : 0,
          shadowRadius: 12,
          shadowOffset: { width: 0, height: 6 },
          transform: [
            { translateX: offset.x },
            { translateY: offset.y },
            { scale: isLifted && mode === 'grid' ? scale : 1 },
          ],
        },
      },
      onLongPress: () => handlers.lift(key),
      onPressOut: () => handlers.pressOut(key),
      lifted: isLifted,
    };
  };

  // An item's last layout, relative to its container.
  const layoutOf = useCallback((key: string) => layouts.get(key), [layouts]);

  return { item, layoutOf, dragging: lifted !== null };
}
