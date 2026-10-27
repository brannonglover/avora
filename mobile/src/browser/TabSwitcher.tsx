import { CircleUser, Pin, Plus, X } from 'lucide-react-native';
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import {
  Animated,
  Dimensions,
  PanResponder,
  Pressable,
  ScrollView,
  type ScrollViewInstance,
  StyleSheet,
  Text,
  useWindowDimensions,
  View,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { Favicon } from './Favicon';
import type { Tab } from './tabs';
import { colors, radius } from './theme';
import { hostOf } from './url';

type Props = {
  visible: boolean;
  tabs: Tab[];
  activeId: string;
  onSelect: (id: string) => void;
  onCloseTab: (id: string) => void;
  onTabActions: (tab: Tab) => void;
  onNewTab: () => void;
  onClearToday: () => void;
  onAccount: () => void;
  onDone: () => void;
  // Rendered below the phone's own tabs; tabs from other devices go here.
  children?: React.ReactNode;
};

const PINNED_COLUMNS = 4;

export function TabSwitcher(props: Props) {
  const { visible, tabs, activeId } = props;
  const insets = useSafeAreaInsets();
  const { width } = useWindowDimensions();
  const [slide] = useState(() => new Animated.Value(0));
  // Stays mounted through the closing animation.
  const [mounted, setMounted] = useState(visible);
  if (visible && !mounted) setMounted(true);
  const scrollRef = useRef<ScrollViewInstance>(null);
  const activeRowY = useRef(0);
  const todayY = useRef(0);

  useEffect(() => {
    Animated.spring(slide, {
      toValue: visible ? 1 : 0,
      useNativeDriver: true,
      speed: 20,
      bounciness: 0,
    }).start(({ finished }) => {
      if (finished && !visible) setMounted(false);
    });
  }, [visible, slide]);

  // Open with the current tab in view; once open, leave scrolling to the user.
  useEffect(() => {
    if (!visible) return;
    const frame = requestAnimationFrame(() => {
      const listTop = todayY.current;
      scrollRef.current?.scrollTo({ y: Math.max(0, listTop + activeRowY.current - 240), animated: false });
    });
    return () => cancelAnimationFrame(frame);
  }, [visible]);

  const pinned = tabs.filter((t) => t.pinned);
  const today = tabs.filter((t) => !t.pinned);
  const tileSize = (Math.min(width, 600) - 32 - (PINNED_COLUMNS - 1) * 10) / PINNED_COLUMNS;

  if (!mounted) return null;

  const height = Dimensions.get('window').height;

  return (
    <Animated.View
      style={[
        StyleSheet.absoluteFill,
        styles.sheet,
        {
          paddingTop: insets.top,
          opacity: slide,
          transform: [
            { translateY: slide.interpolate({ inputRange: [0, 1], outputRange: [height * 0.12, 0] }) },
          ],
        },
      ]}
      pointerEvents={visible ? 'auto' : 'none'}
    >
      <View style={styles.header}>
        <Text style={styles.headerTitle}>Tabs</Text>
        <Text style={styles.headerCount}>{tabs.length}</Text>
        <Pressable hitSlop={10} onPress={props.onAccount} style={styles.account} accessibilityLabel="Account">
          <CircleUser size={24} color={colors.textSecondary} />
        </Pressable>
      </View>

      <ScrollView
        ref={scrollRef}
        style={styles.scroll}
        contentContainerStyle={styles.scrollContent}
      >
        {pinned.length ? (
          <View style={styles.section}>
            <SectionLabel text="Pinned" />
            <View style={styles.grid}>
              {pinned.map((tab) => (
                <Pressable
                  key={tab.id}
                  onPress={() => props.onSelect(tab.id)}
                  onLongPress={() => props.onTabActions(tab)}
                  style={({ pressed }) => [
                    styles.tile,
                    { width: tileSize },
                    pressed && styles.pressed,
                  ]}
                  accessibilityLabel={`Pinned tab, ${tab.title || hostOf(tab.url)}`}
                >
                  <View
                    style={[
                      styles.tileIcon,
                      { height: tileSize * 0.72 },
                      tab.id === activeId && styles.tileIconActive,
                    ]}
                  >
                    <Favicon url={tab.url} size={26} />
                  </View>
                  <Text style={styles.tileLabel} numberOfLines={1}>
                    {tab.title || hostOf(tab.url) || 'Start'}
                  </Text>
                </Pressable>
              ))}
            </View>
          </View>
        ) : null}

        <View style={styles.section} onLayout={(e) => (todayY.current = e.nativeEvent.layout.y)}>
          <View style={styles.sectionRow}>
            <SectionLabel text="Today" />
            {today.length ? (
              <Pressable hitSlop={8} onPress={props.onClearToday}>
                <Text style={styles.clear}>Clear</Text>
              </Pressable>
            ) : null}
          </View>
          {today.length ? (
            <View style={styles.list}>
              {today.map((tab, i) => (
                <TabRow
                  key={tab.id}
                  tab={tab}
                  active={tab.id === activeId}
                  first={i === 0}
                  onPress={() => props.onSelect(tab.id)}
                  onLongPress={() => props.onTabActions(tab)}
                  onClose={() => props.onCloseTab(tab.id)}
                  onLayoutY={(y) => {
                    if (tab.id === activeId) activeRowY.current = y;
                  }}
                />
              ))}
            </View>
          ) : (
            <Text style={styles.empty}>No tabs today. Pinned tabs stay put; everything else lands here.</Text>
          )}
        </View>

        {props.children}
      </ScrollView>

      <View style={[styles.footer, { paddingBottom: insets.bottom + 8 }]}>
        <View style={styles.footerSide} />
        <Pressable
          onPress={props.onNewTab}
          style={({ pressed }) => [styles.newTab, pressed && { opacity: 0.85 }]}
          accessibilityLabel="New tab"
        >
          <Plus size={20} color={colors.background} strokeWidth={2.5} />
          <Text style={styles.newTabText}>New Tab</Text>
        </Pressable>
        <View style={[styles.footerSide, { alignItems: 'flex-end' }]}>
          <Pressable hitSlop={10} onPress={props.onDone}>
            <Text style={styles.done}>Done</Text>
          </Pressable>
        </View>
      </View>
    </Animated.View>
  );
}

function SectionLabel({ text }: { text: string }) {
  return (
    <View style={styles.sectionLabel}>
      {text === 'Pinned' ? <Pin size={12} color={colors.textTertiary} /> : null}
      <Text style={styles.sectionLabelText}>{text}</Text>
    </View>
  );
}

const CLOSE_THRESHOLD = -110;

function TabRow({
  tab,
  active,
  first,
  onPress,
  onLongPress,
  onClose,
  onLayoutY,
}: {
  tab: Tab;
  active: boolean;
  first: boolean;
  onPress: () => void;
  onLongPress: () => void;
  onClose: () => void;
  onLayoutY: (y: number) => void;
}) {
  const [x] = useState(() => new Animated.Value(0));
  const onCloseRef = useRef(onClose);
  useLayoutEffect(() => {
    onCloseRef.current = onClose;
  });

  const pan = useMemo(
    () =>
      // The handlers only read refs during a gesture, never while rendering.
      // eslint-disable-next-line react-hooks/refs
      PanResponder.create({
        onMoveShouldSetPanResponder: (_, g) => g.dx < -10 && Math.abs(g.dx) > Math.abs(g.dy) * 1.5,
        onPanResponderTerminationRequest: () => false,
        onPanResponderMove: (_, g) => x.setValue(Math.min(0, g.dx)),
        onPanResponderRelease: (_, g) => {
          if (g.dx < CLOSE_THRESHOLD || g.vx < -0.8) {
            Animated.timing(x, { toValue: -600, duration: 160, useNativeDriver: true }).start(() =>
              onCloseRef.current(),
            );
          } else {
            Animated.spring(x, { toValue: 0, useNativeDriver: true }).start();
          }
        },
        onPanResponderTerminate: () => Animated.spring(x, { toValue: 0, useNativeDriver: true }).start(),
      }),
    [x],
  );

  const title = tab.title || hostOf(tab.url) || 'Start Page';

  return (
    <View
      style={[styles.rowOuter, !first && styles.rowDivider]}
      onLayout={(e) => onLayoutY(e.nativeEvent.layout.y)}
    >
      <View style={styles.rowCloseUnder}>
        <Text style={styles.rowCloseUnderText}>Close</Text>
      </View>
      <Animated.View style={{ transform: [{ translateX: x }] }} {...pan.panHandlers}>
        <Pressable
          onPress={onPress}
          onLongPress={onLongPress}
          style={({ pressed }) => [
            styles.row,
            active && styles.rowActive,
            pressed && styles.pressed,
          ]}
          accessibilityLabel={`${title}${active ? ', current tab' : ''}`}
        >
          {active ? <View style={styles.activeBar} /> : null}
          <Favicon url={tab.url} size={22} />
          <View style={styles.rowText}>
            <Text style={styles.rowTitle} numberOfLines={1}>
              {title}
            </Text>
            {tab.url ? (
              <Text style={styles.rowHost} numberOfLines={1}>
                {hostOf(tab.url)}
              </Text>
            ) : null}
          </View>
          <Pressable
            hitSlop={10}
            onPress={onClose}
            style={styles.rowClose}
            accessibilityLabel={`Close ${title}`}
          >
            <X size={16} color={colors.textSecondary} />
          </Pressable>
        </Pressable>
      </Animated.View>
    </View>
  );
}

const styles = StyleSheet.create({
  sheet: { backgroundColor: colors.background, zIndex: 10 },
  header: {
    flexDirection: 'row',
    alignItems: 'baseline',
    gap: 8,
    paddingHorizontal: 20,
    paddingTop: 12,
    paddingBottom: 8,
  },
  headerTitle: { color: colors.text, fontSize: 28, fontWeight: '700' },
  headerCount: { color: colors.textTertiary, fontSize: 20, fontWeight: '600' },
  account: { marginLeft: 'auto', alignSelf: 'center' },
  scroll: { flex: 1 },
  scrollContent: { paddingHorizontal: 16, paddingBottom: 24, maxWidth: 600, width: '100%', alignSelf: 'center' },
  section: { marginTop: 14 },
  sectionRow: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  sectionLabel: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 5,
    paddingHorizontal: 4,
    marginBottom: 8,
  },
  sectionLabelText: {
    color: colors.textTertiary,
    fontSize: 12,
    fontWeight: '600',
    textTransform: 'uppercase',
    letterSpacing: 0.6,
  },
  clear: { color: colors.accent, fontSize: 14, marginBottom: 8, paddingHorizontal: 4 },
  grid: { flexDirection: 'row', flexWrap: 'wrap', gap: 10 },
  tile: { alignItems: 'center', gap: 6 },
  tileIcon: {
    width: '100%',
    borderRadius: radius.card,
    backgroundColor: colors.surface,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 1.5,
    borderColor: 'transparent',
  },
  tileIconActive: { borderColor: colors.accent, backgroundColor: colors.accentSoft },
  tileLabel: { color: colors.textSecondary, fontSize: 11, maxWidth: '100%' },
  list: { backgroundColor: colors.surface, borderRadius: radius.card, overflow: 'hidden' },
  rowOuter: { backgroundColor: colors.danger },
  rowDivider: { borderTopWidth: StyleSheet.hairlineWidth, borderTopColor: colors.border },
  rowCloseUnder: {
    ...StyleSheet.absoluteFill,
    alignItems: 'flex-end',
    justifyContent: 'center',
    paddingRight: 20,
  },
  rowCloseUnderText: { color: colors.background, fontWeight: '700', fontSize: 14 },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    paddingLeft: 14,
    paddingRight: 8,
    paddingVertical: 11,
    backgroundColor: colors.surface,
  },
  rowActive: { backgroundColor: colors.raised },
  activeBar: {
    position: 'absolute',
    left: 0,
    top: 10,
    bottom: 10,
    width: 3,
    borderTopRightRadius: 2,
    borderBottomRightRadius: 2,
    backgroundColor: colors.accent,
  },
  rowText: { flex: 1 },
  rowTitle: { color: colors.text, fontSize: 15 },
  rowHost: { color: colors.textTertiary, fontSize: 12, marginTop: 1 },
  rowClose: { width: 32, height: 32, alignItems: 'center', justifyContent: 'center', borderRadius: 16 },
  pressed: { opacity: 0.7 },
  empty: { color: colors.textTertiary, fontSize: 14, paddingHorizontal: 4, lineHeight: 20 },
  footer: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingHorizontal: 20,
    paddingTop: 10,
    borderTopWidth: StyleSheet.hairlineWidth,
    borderTopColor: colors.border,
    backgroundColor: colors.surface,
  },
  footerSide: { flex: 1 },
  newTab: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    height: 44,
    paddingHorizontal: 22,
    borderRadius: radius.pill,
    backgroundColor: colors.accent,
  },
  newTabText: { color: colors.background, fontSize: 16, fontWeight: '700' },
  done: { color: colors.accent, fontSize: 17, fontWeight: '600' },
});
