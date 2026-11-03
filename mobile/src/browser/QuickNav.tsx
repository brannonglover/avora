import { ArrowUpRight, Search, X } from 'lucide-react-native';
import { type Ref, useEffect, useMemo, useState } from 'react';
import {
  Animated,
  Easing,
  Platform,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  type TextInputInstance,
  useWindowDimensions,
  View,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { AuroraRing } from './Aurora';
import { Favicon } from './Favicon';
import type { Tab } from './tabs';
import { colors } from './theme';
import { displayText, editText, hostOf, isSearchUrl, resolveInput } from './url';

// The desktop quick-nav overlay (avora_quick_nav_view.cc), fitted to a phone:
// the same dimmed backdrop and card, the input on top, one list of rows under
// it. The card sits around the middle of the space above the keyboard.
const INPUT_HEIGHT = 48;
const INPUT_RADIUS = 12;
const CARD_RADIUS = 16;
const ROW_HEIGHT = 52;
const ROW_RADIUS = 10;
const MAX_ROWS = 8;
// Where the card's top sits, as a fraction of the space between the status bar
// and the keyboard. A fixed anchor rather than true centering, so the input
// stays put while the list grows and shrinks under it.
const ANCHOR = 0.3;
// Card padding above and below, the list's top margin, and a gap above the
// keyboard: everything around the input and list that isn't them.
const CARD_CHROME = 12 + 12 + 12 + 12;

const quickNav = {
  backdrop: 'rgba(10,12,18,0.72)',
  card: 'rgba(30,34,42,0.97)',
  cardBorder: 'rgba(255,255,255,0.19)',
  input: '#14171C',
  rowSelected: 'rgba(110,161,247,0.19)',
  rowSelectedBorder: 'rgba(110,161,247,0.33)',
  title: '#E0E4E8',
  detail: 'rgba(237,242,245,0.47)',
} as const;

type Row =
  | { key: string; type: 'go'; url: string; title: string; detail: string }
  | { key: string; type: 'tab'; tab: Tab; title: string; detail: string };

type Props = {
  inputRef: Ref<TextInputInstance>;
  currentTab: Tab;
  tabs: Tab[];
  query: string;
  keyboardHeight: number;
  onQueryChange: (q: string) => void;
  onInputFocus: () => void;
  onSubmit: (text: string) => void;
  onSelectTab: (tab: Tab) => void;
  onCancel: () => void;
};

function urlOf(tab: Tab) {
  return tab.url ?? tab.rowUrl;
}

function tabTitle(tab: Tab) {
  const url = urlOf(tab);
  return tab.title || displayText(url) || hostOf(url) || 'Start Page';
}

function useRows(currentTab: Tab, tabs: Tab[], query: string): Row[] {
  return useMemo(() => {
    const q = query.trim();
    const others = tabs.filter((t) => t.id !== currentTab.id && urlOf(t));

    // Nothing typed yet (or just the current address): the current page, then
    // Favorites, then the other open tabs, most recently used first (tabs that
    // arrived from another device and were never shown here go last).
    if (!q || q === editText(currentTab.url)) {
      const rows: Row[] = [];
      if (currentTab.url) {
        rows.push({
          key: 'current',
          type: 'go',
          url: currentTab.url,
          title: isSearchUrl(currentTab.url) ? displayText(currentTab.url) : tabTitle(currentTab),
          detail: isSearchUrl(currentTab.url) ? 'Google Search' : hostOf(currentTab.url),
        });
      }
      const favorites = others.filter((t) => t.kind === 'favorite');
      const recent = others
        .filter((t) => t.kind !== 'favorite')
        .sort((a, b) => b.lastUsedAt - a.lastUsedAt);
      for (const tab of [...favorites, ...recent]) {
        rows.push({
          key: tab.id,
          type: 'tab',
          tab,
          title: tabTitle(tab),
          detail: tab.kind === 'favorite' ? hostOf(urlOf(tab)) : `Switch to tab · ${hostOf(urlOf(tab))}`,
        });
      }
      return rows.slice(0, MAX_ROWS);
    }

    // Typing: what Return would do comes first, then open tabs that match.
    const url = resolveInput(q);
    const rows: Row[] = [
      isSearchUrl(url)
        ? { key: 'go', type: 'go', url, title: q, detail: 'Google Search' }
        : { key: 'go', type: 'go', url, title: displayText(url), detail: 'Open' },
    ];
    const needle = q.toLowerCase();
    for (const tab of others) {
      const tabUrl = urlOf(tab)!;
      if (!tab.title.toLowerCase().includes(needle) && !tabUrl.toLowerCase().includes(needle)) continue;
      rows.push({
        key: tab.id,
        type: 'tab',
        tab,
        title: tabTitle(tab),
        detail: `Switch to tab · ${hostOf(tabUrl)}`,
      });
    }
    return rows.slice(0, MAX_ROWS);
  }, [currentTab, tabs, query]);
}

export function QuickNav({
  inputRef,
  currentTab,
  tabs,
  query,
  keyboardHeight,
  onQueryChange,
  onInputFocus,
  onSubmit,
  onSelectTab,
  onCancel,
}: Props) {
  const insets = useSafeAreaInsets();
  const { height: windowHeight } = useWindowDimensions();
  const [shown] = useState(() => new Animated.Value(0));
  const bottom = keyboardHeight || insets.bottom;
  const free = windowHeight - insets.top - bottom;
  const cardTop = insets.top + Math.max(12, free * ANCHOR);
  const listMaxHeight = Math.max(ROW_HEIGHT * 2, windowHeight - bottom - cardTop - INPUT_HEIGHT - CARD_CHROME);
  // Glide to the new spot when the keyboard comes or goes instead of jumping.
  const [top] = useState(() => new Animated.Value(cardTop));
  useEffect(() => {
    Animated.timing(top, {
      toValue: cardTop,
      duration: 250,
      easing: Easing.out(Easing.cubic),
      useNativeDriver: true,
    }).start();
  }, [top, cardTop]);
  const rows = useRows(currentTab, tabs, query);
  // Like the desktop: the row Return would act on is highlighted, which is the
  // current page when the field still holds its address.
  const showingCurrent = !!currentTab.url && query === editText(currentTab.url);
  const selectedKey = showingCurrent ? 'current' : query.trim() ? rows[0]?.key : undefined;

  useEffect(() => {
    Animated.timing(shown, {
      toValue: 1,
      duration: 180,
      easing: Easing.out(Easing.cubic),
      useNativeDriver: true,
    }).start();
  }, [shown]);

  const choose = (row: Row) => {
    if (row.type === 'tab') onSelectTab(row.tab);
    else onSubmit(row.url);
  };

  return (
    <View style={StyleSheet.absoluteFill}>
      <Animated.View style={[StyleSheet.absoluteFill, { opacity: shown }]}>
        <Pressable
          style={[StyleSheet.absoluteFill, { backgroundColor: quickNav.backdrop }]}
          onPress={onCancel}
          accessibilityLabel="Close"
        />
      </Animated.View>

      <Animated.View
        style={[
          styles.card,
          {
            opacity: shown,
            transform: [
              { translateY: top },
              { translateY: shown.interpolate({ inputRange: [0, 1], outputRange: [-12, 0] }) },
              { scale: shown.interpolate({ inputRange: [0, 1], outputRange: [0.98, 1] }) },
            ],
          },
        ]}
      >
        <View style={styles.input}>
          <AuroraRing radius={INPUT_RADIUS} />
          {showingCurrent && !isSearchUrl(currentTab.url) ? (
            <Favicon url={currentTab.url} size={16} />
          ) : (
            <Search size={16} color={quickNav.detail} />
          )}
          <TextInput
            ref={inputRef}
            style={styles.field}
            value={query}
            onChangeText={onQueryChange}
            onSubmitEditing={(e) => onSubmit(e.nativeEvent.text)}
            autoFocus
            onFocus={onInputFocus}
            autoCapitalize="none"
            autoCorrect={false}
            autoComplete="off"
            spellCheck={false}
            keyboardType={Platform.OS === 'ios' ? 'web-search' : 'default'}
            keyboardAppearance="dark"
            returnKeyType="go"
            placeholder="Search or enter a URL…"
            placeholderTextColor={quickNav.detail}
            selectionColor={colors.accent}
          />
          {query ? (
            <Pressable hitSlop={10} onPress={() => onQueryChange('')} accessibilityLabel="Clear">
              <View style={styles.clear}>
                <X size={12} color={colors.background} strokeWidth={3} />
              </View>
            </Pressable>
          ) : null}
        </View>

        {rows.length ? (
          <ScrollView
            style={[styles.list, { maxHeight: listMaxHeight }]}
            contentContainerStyle={styles.listContent}
            keyboardShouldPersistTaps="handled"
          >
            {rows.map((row) => (
              <ResultRow
                key={row.key}
                row={row}
                selected={row.key === selectedKey}
                onPress={() => choose(row)}
              />
            ))}
          </ScrollView>
        ) : null}
      </Animated.View>
    </View>
  );
}

function ResultRow({ row, selected, onPress }: { row: Row; selected: boolean; onPress: () => void }) {
  const url = row.type === 'tab' ? urlOf(row.tab) : row.url;
  const search = row.type === 'go' && isSearchUrl(row.url) && row.key !== 'current';
  return (
    <Pressable
      onPress={onPress}
      style={({ pressed }) => [styles.row, selected && styles.rowSelected, pressed && styles.rowPressed]}
    >
      <View style={styles.rowIcon}>
        {search || (row.key === 'current' && isSearchUrl(url)) ? (
          <Search size={16} color={quickNav.detail} />
        ) : (
          <Favicon url={url} size={18} />
        )}
      </View>
      <View style={styles.rowText}>
        <Text style={styles.rowTitle} numberOfLines={1}>
          {row.title}
        </Text>
        <Text style={styles.rowDetail} numberOfLines={1}>
          {row.detail}
        </Text>
      </View>
      {row.type === 'tab' && row.tab.kind !== 'favorite' ? (
        <ArrowUpRight size={15} color={quickNav.detail} />
      ) : null}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  card: {
    position: 'absolute',
    top: 0,
    left: 12,
    right: 12,
    padding: 12,
    borderRadius: CARD_RADIUS,
    backgroundColor: quickNav.card,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: quickNav.cardBorder,
    shadowColor: '#000',
    shadowOpacity: 0.45,
    shadowRadius: 24,
    shadowOffset: { width: 0, height: 12 },
  },
  input: {
    height: INPUT_HEIGHT,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    paddingHorizontal: 14,
    borderRadius: INPUT_RADIUS,
    backgroundColor: quickNav.input,
  },
  field: { flex: 1, color: colors.text, fontSize: 16, paddingVertical: 0 },
  clear: {
    width: 18,
    height: 18,
    borderRadius: 9,
    backgroundColor: colors.textTertiary,
    alignItems: 'center',
    justifyContent: 'center',
  },
  list: { marginTop: 12 },
  listContent: { gap: 2 },
  row: {
    height: ROW_HEIGHT,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    paddingHorizontal: 12,
    borderRadius: ROW_RADIUS,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: 'transparent',
  },
  rowSelected: { backgroundColor: quickNav.rowSelected, borderColor: quickNav.rowSelectedBorder },
  rowPressed: { backgroundColor: 'rgba(255,255,255,0.09)' },
  rowIcon: { width: 20, alignItems: 'center' },
  rowText: { flex: 1 },
  rowTitle: { color: quickNav.title, fontSize: 15, fontWeight: '500' },
  rowDetail: { color: quickNav.detail, fontSize: 12, marginTop: 2 },
});
