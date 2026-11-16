import {
  ChevronLeft,
  ChevronRight,
  Ellipsis,
  LockOpen,
  Plus,
  RotateCw,
  Search,
  X,
} from 'lucide-react-native';
import { useLayoutEffect, useMemo, useRef, useState } from 'react';
import {
  Animated,
  PanResponder,
  Pressable,
  StyleSheet,
  Text,
  View,
} from 'react-native';

import { AuroraProgress } from './Aurora';
import { Favicon } from './Favicon';
import type { Tab } from './tabs';
import { colors, radius } from './theme';
import { displayText, hostOf, isInsecure, isSearchUrl, splitHost } from './url';

const SWIPE_COMMIT = 70;

type Props = {
  tab: Tab;
  tabCount: number;
  hasPrevTab: boolean;
  progress: number;
  loading: boolean;
  canGoBack: boolean;
  canGoForward: boolean;

  onStartEditing: () => void;

  onBack: () => void;
  onForward: () => void;
  onReloadOrStop: () => void;
  onNewTab: () => void;
  onShowTabs: () => void;
  onTabsLongPress: () => void;
  onMenu: () => void;
  // -1 for the tab to the left, 1 for the tab to the right (or a new one).
  onSwipeTab: (direction: -1 | 1) => void;
};

export function BottomBar(props: Props) {
  const { tab, tabCount, progress, loading, canGoBack, canGoForward, onStartEditing } = props;

  const [pillX] = useState(() => new Animated.Value(0));
  // The responder is created once; read the latest props through a ref.
  const latest = useRef(props);
  useLayoutEffect(() => {
    latest.current = props;
  });

  const pan = useMemo(
    () =>
      // The handlers only read refs during a gesture, never while rendering.
      // eslint-disable-next-line react-hooks/refs
      PanResponder.create({
        onMoveShouldSetPanResponder: (_, g) =>
          (Math.abs(g.dx) > 12 && Math.abs(g.dx) > Math.abs(g.dy) * 1.5) ||
          (g.dy < -16 && Math.abs(g.dy) > Math.abs(g.dx) * 1.5),
        onPanResponderMove: (_, g) => {
          if (Math.abs(g.dy) > Math.abs(g.dx)) return;
          // Resist dragging toward a tab that doesn't exist.
          const dx = g.dx > 0 && !latest.current.hasPrevTab ? g.dx / 4 : g.dx;
          pillX.setValue(dx);
        },
        onPanResponderRelease: (_, g) => {
          const p = latest.current;
          if (g.dy < -40 && Math.abs(g.dy) > Math.abs(g.dx)) {
            Animated.spring(pillX, { toValue: 0, useNativeDriver: true }).start();
            p.onShowTabs();
            return;
          }
          const direction = g.dx <= -SWIPE_COMMIT ? 1 : g.dx >= SWIPE_COMMIT && p.hasPrevTab ? -1 : 0;
          if (!direction) {
            Animated.spring(pillX, { toValue: 0, useNativeDriver: true }).start();
            return;
          }
          // Slide out, swap tabs, slide the new tab's pill in from the far side.
          Animated.timing(pillX, {
            toValue: -direction * 400,
            duration: 140,
            useNativeDriver: true,
          }).start(() => {
            p.onSwipeTab(direction as -1 | 1);
            pillX.setValue(direction * 400);
            Animated.spring(pillX, {
              toValue: 0,
              useNativeDriver: true,
              speed: 18,
              bounciness: 4,
            }).start();
          });
        },
        onPanResponderTerminate: () => {
          Animated.spring(pillX, { toValue: 0, useNativeDriver: true }).start();
        },
      }),
    [pillX],
  );

  const label = displayText(tab.url);
  const isSearch = isSearchUrl(tab.url);
  const host = tab.url && !isSearch ? splitHost(hostOf(tab.url)) : null;

  return (
    <View style={styles.container}>
      <View style={styles.addressRow}>
        <Animated.View
          style={[styles.pillWrap, { transform: [{ translateX: pillX }] }]}
          {...pan.panHandlers}
        >
          <Pressable
            style={({ pressed }) => [styles.pill, pressed && styles.pillPressed]}
            onPress={onStartEditing}
            accessibilityRole="search"
            accessibilityLabel={label ? `Address, ${label}` : 'Search or enter address'}
            accessibilityHint="Swipe left or right to switch tabs"
          >
            <AuroraProgress loading={loading} progress={progress} radius={radius.row} />
            <View style={styles.pillSide}>
              {!tab.url ? (
                <Search size={15} color={colors.textTertiary} />
              ) : isInsecure(tab.url) ? (
                <LockOpen size={13} color={colors.danger} />
              ) : null}
            </View>
            <View style={styles.pillCenter}>
              {isSearch ? (
                <Search size={14} color={colors.textSecondary} />
              ) : tab.url ? (
                <Favicon url={tab.url} size={16} />
              ) : null}
              <Text style={[styles.pillText, !label && styles.placeholder]} numberOfLines={1}>
                {host ? (
                  <>
                    <Text style={styles.subdomain}>{host.sub}</Text>
                    {host.domain}
                  </>
                ) : (
                  label || 'Search or enter address'
                )}
              </Text>
            </View>
            <View style={styles.pillSide}>
              {tab.url ? (
                <Pressable hitSlop={12} onPress={props.onReloadOrStop}>
                  {loading ? (
                    <X size={16} color={colors.textSecondary} />
                  ) : (
                    <RotateCw size={15} color={colors.textSecondary} />
                  )}
                </Pressable>
              ) : null}
            </View>
          </Pressable>
        </Animated.View>
      </View>

      <View style={styles.toolbar}>
        <ToolButton label="Back" disabled={!canGoBack} onPress={props.onBack}>
          <ChevronLeft size={26} color={canGoBack ? colors.text : colors.textTertiary} />
        </ToolButton>
        <ToolButton label="Forward" disabled={!canGoForward} onPress={props.onForward}>
          <ChevronRight size={26} color={canGoForward ? colors.text : colors.textTertiary} />
        </ToolButton>
        <ToolButton label="New tab" onPress={props.onNewTab}>
          <Plus size={24} color={colors.text} />
        </ToolButton>
        <ToolButton
          label={`Show tabs, ${tabCount} open`}
          onPress={props.onShowTabs}
          onLongPress={props.onTabsLongPress}
        >
          <View style={styles.tabCount}>
            <Text style={styles.tabCountText}>{tabCount > 99 ? ':D' : tabCount}</Text>
          </View>
        </ToolButton>
        <ToolButton label="More" onPress={props.onMenu}>
          <Ellipsis size={24} color={colors.text} />
        </ToolButton>
      </View>
    </View>
  );
}

function ToolButton({
  label,
  disabled,
  onPress,
  onLongPress,
  children,
}: {
  label: string;
  disabled?: boolean;
  onPress: () => void;
  onLongPress?: () => void;
  children: React.ReactNode;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={label}
      disabled={disabled}
      onPress={onPress}
      onLongPress={onLongPress}
      style={({ pressed }) => [styles.toolButton, pressed && styles.toolButtonPressed]}
    >
      {children}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  container: {
    borderTopWidth: StyleSheet.hairlineWidth,
    borderTopColor: colors.border,
  },
  addressRow: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingHorizontal: 12,
    paddingTop: 8,
    gap: 10,
  },
  pillWrap: { flex: 1, height: 44 },
  pill: {
    flex: 1,
    minHeight: 44,
    flexDirection: 'row',
    alignItems: 'center',
    borderRadius: radius.row,
    backgroundColor: colors.raised,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.hairline,
    paddingHorizontal: 12,
  },
  pillPressed: { backgroundColor: colors.border },
  pillSide: { width: 22, alignItems: 'center' },
  pillCenter: {
    flex: 1,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 7,
  },
  pillText: {
    flexShrink: 1,
    textAlign: 'center',
    color: colors.text,
    fontSize: 16,
    fontWeight: '500',
  },
  subdomain: { color: colors.textTertiary, fontWeight: '400' },
  placeholder: { color: colors.textTertiary, fontWeight: '400' },
  toolbar: {
    flexDirection: 'row',
    justifyContent: 'space-around',
    alignItems: 'center',
    paddingHorizontal: 8,
    height: 48,
  },
  toolButton: {
    width: 52,
    height: 40,
    alignItems: 'center',
    justifyContent: 'center',
    borderRadius: 10,
  },
  toolButtonPressed: { backgroundColor: colors.fill },
  tabCount: {
    minWidth: 24,
    height: 24,
    paddingHorizontal: 4,
    borderRadius: 7,
    borderWidth: 2,
    borderColor: colors.text,
    alignItems: 'center',
    justifyContent: 'center',
  },
  tabCountText: { color: colors.text, fontSize: 12, fontWeight: '700' },
});
