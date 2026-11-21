import { CircleUser, Folder as FolderIcon, Pin, Plus, Star, X } from 'lucide-react-native';
import { memo, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
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

import type { SpaceRow } from '../sync/rows';

import { Favicon } from './Favicon';
import { useAutoScroller, useReorder } from './reorder';
import type { Folder, Tab } from './tabs';
import { COLUMN_WIDTH, colors, radius } from './theme';
import { hostOf } from './url';

type Props = {
  visible: boolean;
  tabs: Tab[];
  folders: Folder[];
  activeId: string;
  spaces: SpaceRow[];
  activeSpaceId: string;
  onSwitchSpace: (id: string) => void;
  // Missing where the platform can't ask for a name.
  onNewSpace?: () => void;
  onRenameSpace?: (id: string) => void;
  onSelect: (id: string) => void;
  onCloseTab: (id: string) => void;
  onTabActions: (tab: Tab) => void;
  // Moves a tab `by` places within its own section (Favorites, Pinned, a
  // folder, or Today).
  onMoveTab: (id: string, by: number) => void;
  onNewTab: () => void;
  onClearToday: () => void;
  onAccount: () => void;
  onDone: () => void;
};

const PINNED_COLUMNS = 4;
// Hold this long to lift a tab for dragging; let go without moving for its actions.
const LONG_PRESS_MS = 350;
const HINT = 'Long press and drag to reorder, or long press for actions';

// Memoized: the browser screen re-renders on every bit of page-load progress,
// and this is the biggest thing on it.
export const TabSwitcher = memo(function TabSwitcher(props: Props) {
  const { visible, tabs, activeId } = props;
  const insets = useSafeAreaInsets();
  const { width } = useWindowDimensions();
  const [slide] = useState(() => new Animated.Value(0));
  // Stays mounted through the closing animation.
  const [mounted, setMounted] = useState(visible);
  if (visible && !mounted) setMounted(true);
  const scrollRef = useRef<ScrollViewInstance>(null);
  // Read when the switcher opens, not on every change of tab.
  const activeIdRef = useRef(activeId);
  useLayoutEffect(() => {
    activeIdRef.current = activeId;
  });
  const todayY = useRef(0);
  const [openFolder, setOpenFolder] = useState<{ id: string; spaceId: string } | null>(null);
  if (!visible && openFolder) setOpenFolder(null);

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

  const favorites = tabs.filter((t) => t.kind === 'favorite');
  const pinned = useMemo(
    () => groupPinned(tabs.filter((t) => t.kind === 'pinned'), props.folders),
    [tabs, props.folders],
  );
  const today = tabs.filter((t) => t.kind === 'today');
  const activeSpace = props.spaces.find((s) => s.id === props.activeSpaceId);
  const usableWidth = width - insets.left - insets.right;
  const tileSize = (Math.min(usableWidth, COLUMN_WIDTH) - 32 - (PINNED_COLUMNS - 1) * 10) / PINNED_COLUMNS;
  const tabsById = new Map(tabs.map((t) => [t.id, t]));
  const showActions = (id: string) => {
    const tab = tabsById.get(id);
    if (tab) props.onTabActions(tab);
  };
  const { scroller, scrollProps } = useAutoScroller(scrollRef);
  const favoritesDrag = useReorder({
    keys: favorites.map((t) => t.id),
    mode: 'grid',
    onMove: props.onMoveTab,
    onLongPress: showActions,
    scroller,
  });
  const pinnedDrag = useReorder({
    keys: pinned.map((e) => (e.kind === 'tab' ? e.tab.id : e.folder.id)),
    mode: 'grid',
    onMove: props.onMoveTab,
    onLongPress: showActions,
    scroller,
  });
  const todayDrag = useReorder({
    keys: today.map((t) => t.id),
    mode: 'list',
    onMove: props.onMoveTab,
    onLongPress: showActions,
    scroller,
  });
  // Open with the current tab in view; once open, leave scrolling to the user.
  const todayLayoutOf = todayDrag.layoutOf;
  useEffect(() => {
    if (!visible) return;
    const frame = requestAnimationFrame(() => {
      const rowY = todayLayoutOf(activeIdRef.current)?.y ?? 0;
      scrollRef.current?.scrollTo({ y: Math.max(0, todayY.current + rowY - 240), animated: false });
    });
    return () => cancelAnimationFrame(frame);
  }, [visible, todayLayoutOf]);
  const shownFolder =
    openFolder?.spaceId === props.activeSpaceId
      ? pinned.find((e) => e.kind === 'folder' && e.folder.id === openFolder.id)
      : undefined;

  if (!mounted) return null;

  const height = Dimensions.get('window').height;

  return (
    <Animated.View
      style={[
        StyleSheet.absoluteFill,
        styles.sheet,
        {
          paddingTop: insets.top,
          paddingLeft: insets.left,
          paddingRight: insets.right,
          opacity: slide,
          transform: [
            { translateY: slide.interpolate({ inputRange: [0, 1], outputRange: [height * 0.12, 0] }) },
          ],
        },
      ]}
      pointerEvents={visible ? 'auto' : 'none'}
    >
      <View style={[styles.column, styles.header]}>
        <Text style={styles.headerTitle} numberOfLines={1}>
          {activeSpace?.name || 'Tabs'}
        </Text>
        <Text style={styles.headerCount}>{tabs.length}</Text>
        <Pressable hitSlop={10} onPress={props.onAccount} style={styles.account} accessibilityLabel="Account">
          <CircleUser size={24} color={colors.textSecondary} />
        </Pressable>
      </View>

      <View style={styles.column}>
        <SpaceBar
          spaces={props.spaces}
          activeId={props.activeSpaceId}
          onSelect={props.onSwitchSpace}
          onNew={props.onNewSpace}
          onLongPress={props.onRenameSpace}
        />
      </View>

      <ScrollView
        ref={scrollRef}
        style={styles.scroll}
        contentContainerStyle={styles.scrollContent}
        {...scrollProps}
      >
        {favorites.length ? (
          <View style={styles.section}>
            <SectionLabel text="Favorites" />
            <View style={styles.grid}>
              {favorites.map((tab) => {
                const drag = favoritesDrag.item(tab.id);
                return (
                  <Animated.View key={tab.id} {...drag.wrapper}>
                    <Tile
                      size={tileSize}
                      active={tab.id === activeId}
                      onPress={() => props.onSelect(tab.id)}
                      onLongPress={drag.onLongPress}
                      onPressOut={drag.onPressOut}
                      accessibilityLabel={`Favorite, ${titleOf(tab)}`}
                      accessibilityHint={HINT}
                    >
                      <Favicon url={tab.url} size={26} />
                    </Tile>
                  </Animated.View>
                );
              })}
            </View>
          </View>
        ) : null}
        {pinned.length ? (
          <View style={styles.section}>
            <SectionLabel text="Pinned" />
            <View style={styles.grid}>
              {pinned.map((entry) => {
                const drag = pinnedDrag.item(entry.kind === 'tab' ? entry.tab.id : entry.folder.id);
                return entry.kind === 'tab' ? (
                  <Animated.View key={entry.tab.id} {...drag.wrapper}>
                    <Tile
                      size={tileSize}
                      label={titleOf(entry.tab)}
                      active={entry.tab.id === activeId}
                      onPress={() => props.onSelect(entry.tab.id)}
                      onLongPress={drag.onLongPress}
                      onPressOut={drag.onPressOut}
                      accessibilityLabel={`Pinned tab, ${titleOf(entry.tab)}`}
                      accessibilityHint={HINT}
                    >
                      <Favicon url={entry.tab.url} size={26} />
                    </Tile>
                  </Animated.View>
                ) : (
                  // Folders hold their place; tabs can be dragged past them.
                  <Animated.View key={entry.folder.id} {...drag.wrapper}>
                  <Tile
                    size={tileSize}
                    label={entry.folder.title || 'Folder'}
                    folder
                    active={entry.tabs.some((t) => t.id === activeId)}
                    onPress={() => setOpenFolder({ id: entry.folder.id, spaceId: props.activeSpaceId })}
                    accessibilityLabel={`Folder, ${entry.folder.title || 'Untitled'}, ${entry.tabs.length} tabs`}
                  >
                    <View style={styles.mosaic}>
                      {entry.tabs.slice(0, 4).map((t) => (
                        <Favicon key={t.id} url={t.url} size={15} />
                      ))}
                    </View>
                  </Tile>
                  </Animated.View>
                );
              })}
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
              {today.map((tab, i) => {
                const drag = todayDrag.item(tab.id);
                return (
                  <Animated.View key={tab.id} {...drag.wrapper}>
                    <TabRow
                      tab={tab}
                      active={tab.id === activeId}
                      lifted={drag.lifted}
                      first={i === 0}
                      onPress={() => props.onSelect(tab.id)}
                      onLongPress={drag.onLongPress}
                      onPressOut={drag.onPressOut}
                      onClose={() => props.onCloseTab(tab.id)}
                    />
                  </Animated.View>
                );
              })}
            </View>
          ) : (
            <Text style={styles.empty}>No tabs today. Pinned tabs stay put; everything else lands here.</Text>
          )}
        </View>
      </ScrollView>

      {/* The bar runs edge to edge under the side insets; its buttons don't. */}
      <View
        style={[
          styles.footer,
          { paddingBottom: insets.bottom + 8, marginLeft: -insets.left, marginRight: -insets.right },
        ]}
      >
        <View style={[styles.column, styles.footerRow]}>
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
      </View>

      {shownFolder?.kind === 'folder' ? (
        <FolderSheet
          folder={shownFolder.folder}
          tabs={shownFolder.tabs}
          activeId={activeId}
          onSelect={props.onSelect}
          onLongPress={props.onTabActions}
          onMove={props.onMoveTab}
          onClose={() => setOpenFolder(null)}
        />
      ) : null}
    </Animated.View>
  );
});

type PinnedEntry = { kind: 'tab'; tab: Tab } | { kind: 'folder'; folder: Folder; tabs: Tab[] };

// Loose pinned tabs and folders in sidebar order. Pinned tabs arrive with a
// folder's contents in its place, so a folder takes its first tab's spot.
function groupPinned(pinned: Tab[], folders: Folder[]): PinnedEntry[] {
  const byId = new Map(folders.map((f) => [f.id, f]));
  const groups = new Map<string, Tab[]>();
  const entries: PinnedEntry[] = [];
  for (const tab of pinned) {
    const folder = tab.parentId ? byId.get(tab.parentId) : undefined;
    if (!folder) {
      entries.push({ kind: 'tab', tab });
      continue;
    }
    let group = groups.get(folder.id);
    if (!group) {
      group = [];
      groups.set(folder.id, group);
      entries.push({ kind: 'folder', folder, tabs: group });
    }
    group.push(tab);
  }
  return entries;
}

function titleOf(tab: Tab) {
  return tab.title || hostOf(tab.url) || 'Start';
}

// One square in the Favorites or Pinned grid. Favorites go without a label:
// there are only a few and they're known by their icons.
function Tile({
  size,
  label,
  folder,
  active,
  onPress,
  onLongPress,
  onPressOut,
  accessibilityLabel,
  accessibilityHint,
  children,
}: {
  size: number;
  label?: string;
  folder?: boolean;
  active: boolean;
  onPress: () => void;
  onLongPress?: () => void;
  onPressOut?: () => void;
  accessibilityLabel: string;
  accessibilityHint?: string;
  children: React.ReactNode;
}) {
  return (
    <Pressable
      onPress={onPress}
      onLongPress={onLongPress}
      onPressOut={onPressOut}
      delayLongPress={LONG_PRESS_MS}
      accessibilityHint={accessibilityHint}
      style={({ pressed }) => [styles.tile, { width: size }, pressed && styles.pressed]}
      accessibilityLabel={accessibilityLabel}
    >
      <View
        style={[
          styles.tileIcon,
          { height: size * 0.72 },
          folder && styles.tileFolder,
          active && styles.tileIconActive,
        ]}
      >
        {children}
      </View>
      {label ? (
        <Text style={styles.tileLabel} numberOfLines={1}>
          {label}
        </Text>
      ) : null}
    </Pressable>
  );
}

const FOLDER_COLUMNS = 4;

// A folder opened over the switcher, like a folder on the home screen.
function FolderSheet({
  folder,
  tabs,
  activeId,
  onSelect,
  onLongPress,
  onMove,
  onClose,
}: {
  folder: Folder;
  tabs: Tab[];
  activeId: string;
  onSelect: (id: string) => void;
  onLongPress: (tab: Tab) => void;
  onMove: (id: string, by: number) => void;
  onClose: () => void;
}) {
  const { width, height } = useWindowDimensions();
  const scrollRef = useRef<ScrollViewInstance>(null);
  const { scroller, scrollProps } = useAutoScroller(scrollRef);
  const drag = useReorder({
    keys: tabs.map((t) => t.id),
    mode: 'grid',
    onMove,
    onLongPress: (id) => {
      const tab = tabs.find((t) => t.id === id);
      if (tab) onLongPress(tab);
    },
    scroller,
  });
  const [fade] = useState(() => new Animated.Value(0));
  useEffect(() => {
    Animated.timing(fade, { toValue: 1, duration: 160, useNativeDriver: true }).start();
  }, [fade]);

  const cardWidth = Math.min(width, 600) - 32;
  const tileSize = (cardWidth - 32 - (FOLDER_COLUMNS - 1) * 10) / FOLDER_COLUMNS;

  return (
    <Animated.View style={[StyleSheet.absoluteFill, styles.folderBackdrop, { opacity: fade }]}>
      <Pressable style={StyleSheet.absoluteFill} onPress={onClose} accessibilityLabel="Close folder" />
      <Animated.View
        style={[
          styles.folderCard,
          { width: cardWidth },
          { transform: [{ scale: fade.interpolate({ inputRange: [0, 1], outputRange: [0.94, 1] }) }] },
        ]}
      >
        <View style={styles.folderHeader}>
          <FolderIcon size={18} color={colors.textSecondary} />
          <Text style={styles.folderTitle} numberOfLines={1}>
            {folder.title || 'Folder'}
          </Text>
          <Text style={styles.folderCount}>{tabs.length}</Text>
          <Pressable hitSlop={10} onPress={onClose} style={styles.folderClose} accessibilityLabel="Close folder">
            <X size={18} color={colors.textSecondary} />
          </Pressable>
        </View>
        <ScrollView
          ref={scrollRef}
          style={{ maxHeight: height * 0.55 }}
          contentContainerStyle={styles.grid}
          {...scrollProps}
        >
          {tabs.map((tab) => {
            const item = drag.item(tab.id);
            return (
              <Animated.View key={tab.id} {...item.wrapper}>
                <Tile
                  size={tileSize}
                  label={titleOf(tab)}
                  active={tab.id === activeId}
                  onPress={() => {
                    onClose();
                    onSelect(tab.id);
                  }}
                  onLongPress={item.onLongPress}
                  onPressOut={item.onPressOut}
                  accessibilityLabel={`Pinned tab, ${titleOf(tab)}`}
                  accessibilityHint={HINT}
                >
                  <Favicon url={tab.url} size={26} />
                </Tile>
              </Animated.View>
            );
          })}
        </ScrollView>
      </Animated.View>
    </Animated.View>
  );
}

// One chip per Space, like the desktop's Spaces bar.
function SpaceBar({
  spaces,
  activeId,
  onSelect,
  onNew,
  onLongPress,
}: {
  spaces: SpaceRow[];
  activeId: string;
  onSelect: (id: string) => void;
  onNew?: () => void;
  onLongPress?: (id: string) => void;
}) {
  if (spaces.length < 2 && !onNew) return null;
  return (
    <ScrollView
      horizontal
      showsHorizontalScrollIndicator={false}
      style={styles.spaceBar}
      contentContainerStyle={styles.spaceBarContent}
    >
      {spaces.map((space) => {
        const active = space.id === activeId;
        const tint = space.color || colors.accent;
        return (
          <Pressable
            key={space.id}
            onPress={() => onSelect(space.id)}
            onLongPress={onLongPress && (() => onLongPress(space.id))}
            style={({ pressed }) => [
              styles.spaceChip,
              active && { backgroundColor: colors.raised, borderColor: tint },
              pressed && styles.pressed,
            ]}
            accessibilityRole="button"
            accessibilityState={{ selected: active }}
            accessibilityLabel={`${space.name || 'Untitled'} Space`}
          >
            <View style={[styles.spaceDot, { backgroundColor: tint }]} />
            <Text style={[styles.spaceName, active && styles.spaceNameActive]} numberOfLines={1}>
              {space.name || 'Untitled'}
            </Text>
          </Pressable>
        );
      })}
      {onNew ? (
        <Pressable
          onPress={onNew}
          hitSlop={6}
          style={({ pressed }) => [styles.spaceChip, styles.spaceAdd, pressed && styles.pressed]}
          accessibilityLabel="New Space"
        >
          <Plus size={14} color={colors.textSecondary} />
        </Pressable>
      ) : null}
    </ScrollView>
  );
}

function SectionLabel({ text }: { text: string }) {
  return (
    <View style={styles.sectionLabel}>
      {text === 'Pinned' ? <Pin size={12} color={colors.textTertiary} /> : null}
      {text === 'Favorites' ? <Star size={12} color={colors.textTertiary} /> : null}
      <Text style={styles.sectionLabelText}>{text}</Text>
    </View>
  );
}

const CLOSE_THRESHOLD = -110;

function TabRow({
  tab,
  active,
  lifted,
  first,
  onPress,
  onLongPress,
  onPressOut,
  onClose,
}: {
  tab: Tab;
  active: boolean;
  lifted: boolean;
  first: boolean;
  onPress: () => void;
  onLongPress: () => void;
  onPressOut: () => void;
  onClose: () => void;
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
    <View style={[styles.rowOuter, !first && styles.rowDivider]}>
      <View style={styles.rowCloseUnder}>
        <Text style={styles.rowCloseUnderText}>Close</Text>
      </View>
      <Animated.View style={{ transform: [{ translateX: x }] }} {...pan.panHandlers}>
        <Pressable
          onPress={onPress}
          onLongPress={onLongPress}
          onPressOut={onPressOut}
          delayLongPress={LONG_PRESS_MS}
          style={({ pressed }) => [
            styles.row,
            active && styles.rowActive,
            pressed && !lifted && styles.pressed,
            lifted && styles.rowLifted,
          ]}
          accessibilityLabel={`${title}${active ? ', current tab' : ''}`}
          accessibilityHint={HINT}
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
  // Header, Space chips and footer line up with the tab grid on wide screens.
  column: { width: '100%', maxWidth: COLUMN_WIDTH, alignSelf: 'center' },
  header: {
    flexDirection: 'row',
    alignItems: 'baseline',
    gap: 8,
    paddingHorizontal: 20,
    paddingTop: 12,
    paddingBottom: 8,
  },
  headerTitle: { color: colors.text, fontSize: 28, fontWeight: '700', flexShrink: 1 },
  headerCount: { color: colors.textTertiary, fontSize: 20, fontWeight: '600' },
  account: { marginLeft: 'auto', alignSelf: 'center' },
  spaceBar: { flexGrow: 0 },
  spaceBarContent: { paddingHorizontal: 16, paddingVertical: 4, gap: 8 },
  spaceChip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 7,
    height: 32,
    paddingHorizontal: 12,
    borderRadius: radius.pill,
    backgroundColor: colors.surface,
    borderWidth: 1,
    borderColor: 'transparent',
    maxWidth: 180,
  },
  spaceAdd: { paddingHorizontal: 10 },
  spaceDot: { width: 8, height: 8, borderRadius: 4 },
  spaceName: { color: colors.textSecondary, fontSize: 14, fontWeight: '500', flexShrink: 1 },
  spaceNameActive: { color: colors.text, fontWeight: '600' },
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
  tileFolder: { backgroundColor: colors.raised },
  tileIconActive: { borderColor: colors.accent, backgroundColor: colors.accentSoft },
  mosaic: { width: 34, flexDirection: 'row', flexWrap: 'wrap', gap: 4 },
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
  rowLifted: { backgroundColor: colors.border },
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
  folderBackdrop: { backgroundColor: 'rgba(0,0,0,0.55)', alignItems: 'center', justifyContent: 'center' },
  folderCard: { backgroundColor: colors.raised, borderRadius: 22, padding: 16, gap: 14 },
  folderHeader: { flexDirection: 'row', alignItems: 'center', gap: 8, paddingHorizontal: 2 },
  folderTitle: { color: colors.text, fontSize: 20, fontWeight: '700', flexShrink: 1 },
  folderCount: { color: colors.textTertiary, fontSize: 16, fontWeight: '600' },
  folderClose: { marginLeft: 'auto' },
  empty: { color: colors.textTertiary, fontSize: 14, paddingHorizontal: 4, lineHeight: 20 },
  footer: {
    paddingTop: 10,
    borderTopWidth: StyleSheet.hairlineWidth,
    borderTopColor: colors.border,
    backgroundColor: colors.surface,
  },
  footerRow: { flexDirection: 'row', alignItems: 'center', paddingHorizontal: 20 },
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
