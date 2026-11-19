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
  useWindowDimensions,
  View,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { AuroraProgress } from './Aurora';
import { Favicon } from './Favicon';
import type { Tab } from './tabs';
import { colors, radius, WIDE_WIDTH, withAlpha } from './theme';
import { displayText, hostOf, isInsecure, isSearchUrl, splitHost } from './url';

const SWIPE_COMMIT = 70;
// On one row the address bar stays readable rather than spanning a 13" iPad.
const WIDE_PILL_MAX = 680;

type Props = {
  tab: Tab;
  tabCount: number;
  spaceName: string;
  spaceColor: string;
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
  const { tab, tabCount, spaceName, spaceColor, progress, loading, canGoBack, canGoForward, onStartEditing } = props;

  const insets = useSafeAreaInsets();
  const { width } = useWindowDimensions();
  // iPad, or a phone in landscape: one row, so the page keeps its height.
  const wide = width - insets.left - insets.right >= WIDE_WIDTH;

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

  const addressPill = (
    <Animated.View
      style={[styles.pillWrap, wide && styles.pillWrapWide, { transform: [{ translateX: pillX }] }]}
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
  );

  // Back and forward share one capsule: they're a pair, not two of five.
  const navPair = (
    <View style={styles.navPair}>
      <Pressable
        accessibilityRole="button"
        accessibilityLabel="Back"
        disabled={!canGoBack}
        onPress={props.onBack}
        style={({ pressed }) => [styles.navHalf, pressed && styles.navHalfPressed]}
      >
        <ChevronLeft size={22} color={canGoBack ? colors.text : colors.textTertiary} />
      </Pressable>
      <View style={styles.navDivider} />
      <Pressable
        accessibilityRole="button"
        accessibilityLabel="Forward"
        disabled={!canGoForward}
        onPress={props.onForward}
        style={({ pressed }) => [styles.navHalf, pressed && styles.navHalfPressed]}
      >
        <ChevronRight size={22} color={canGoForward ? colors.text : colors.textTertiary} />
      </Pressable>
    </View>
  );

  // Tabs live in Spaces, so the way into them says which Space you're in.
  const spaceChip = (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={`Show tabs, ${spaceName}, ${tabCount} open`}
      accessibilityHint="Long press for tab actions"
      onPress={props.onShowTabs}
      onLongPress={props.onTabsLongPress}
      style={({ pressed }) => [
        styles.spaceChip,
        wide ? styles.spaceChipWide : styles.spaceChipNarrow,
        {
          backgroundColor: withAlpha(spaceColor, pressed ? 0.26 : 0.14),
          borderColor: withAlpha(spaceColor, 0.38),
        },
      ]}
    >
      <View style={[styles.spaceDot, { backgroundColor: spaceColor }]} />
      <Text style={styles.spaceName} numberOfLines={1}>
        {spaceName}
      </Text>
      <View style={[styles.spaceCount, { backgroundColor: withAlpha(spaceColor, 0.3) }]}>
        <Text style={styles.spaceCountText}>{tabCount > 99 ? '99+' : tabCount}</Text>
      </View>
    </Pressable>
  );

  const trailing = (
    <View style={styles.trailing}>
      <Pressable
        accessibilityRole="button"
        accessibilityLabel="More"
        onPress={props.onMenu}
        style={({ pressed }) => [styles.roundButton, pressed && styles.roundButtonPressed]}
      >
        <Ellipsis size={20} color={colors.text} />
      </Pressable>
      <Pressable
        accessibilityRole="button"
        accessibilityLabel="New tab"
        onPress={props.onNewTab}
        style={({ pressed }) => [styles.newTab, pressed && styles.newTabPressed]}
      >
        <Plus size={22} color={colors.background} strokeWidth={2.5} />
      </Pressable>
    </View>
  );

  return (
    <View style={[styles.container, { paddingLeft: insets.left, paddingRight: insets.right }]}>
      {wide ? (
        <View style={styles.toolbar}>
          {navPair}
          <View style={styles.wideMiddle}>{addressPill}</View>
          {spaceChip}
          {trailing}
        </View>
      ) : (
        <>
          <View style={styles.addressRow}>{addressPill}</View>
          <View style={styles.toolbar}>
            {navPair}
            {spaceChip}
            {trailing}
          </View>
        </>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    borderTopWidth: StyleSheet.hairlineWidth,
    borderTopColor: colors.hairline,
  },
  addressRow: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingHorizontal: 12,
    paddingTop: 8,
    gap: 10,
  },
  pillWrap: { flex: 1, height: 44 },
  pillWrapWide: { maxWidth: WIDE_PILL_MAX },
  wideMiddle: { flex: 1, minWidth: 0, flexDirection: 'row', justifyContent: 'center' },
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
    alignItems: 'center',
    paddingHorizontal: 12,
    paddingTop: 8,
    paddingBottom: 6,
    gap: 10,
  },
  navPair: {
    flexDirection: 'row',
    alignItems: 'center',
    height: 40,
    borderRadius: radius.pill,
    backgroundColor: colors.raised,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.hairline,
    overflow: 'hidden',
  },
  navHalf: { width: 46, height: 40, alignItems: 'center', justifyContent: 'center' },
  navHalfPressed: { backgroundColor: colors.fill },
  navDivider: { width: StyleSheet.hairlineWidth, height: 20, backgroundColor: colors.border },
  spaceChip: {
    minWidth: 0,
    height: 40,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 8,
    paddingLeft: 12,
    paddingRight: 6,
    borderRadius: radius.pill,
    borderWidth: 1,
  },
  // Shares its row with back/forward and the buttons on a phone in portrait;
  // sized to its name once the address bar joins the row.
  spaceChipNarrow: { flex: 1 },
  spaceChipWide: { maxWidth: 240 },
  spaceDot: { width: 8, height: 8, borderRadius: 4 },
  spaceName: { flexShrink: 1, color: colors.text, fontSize: 15, fontWeight: '600' },
  spaceCount: {
    minWidth: 26,
    height: 26,
    paddingHorizontal: 7,
    borderRadius: 13,
    alignItems: 'center',
    justifyContent: 'center',
  },
  spaceCountText: { color: colors.text, fontSize: 13, fontWeight: '700' },
  trailing: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  roundButton: {
    width: 40,
    height: 40,
    borderRadius: 20,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: colors.raised,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.hairline,
  },
  roundButtonPressed: { backgroundColor: colors.border },
  newTab: {
    width: 40,
    height: 40,
    borderRadius: 20,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: colors.accent,
  },
  newTabPressed: { opacity: 0.8 },
});
