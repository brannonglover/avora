import * as Clipboard from 'expo-clipboard';
import * as Haptics from 'expo-haptics';
import { router } from 'expo-router';
import { StatusBar } from 'expo-status-bar';
import {
  ArrowUpRight,
  Copy,
  CopyPlus,
  Pin,
  PinOff,
  Plus,
  Share as ShareIcon,
  Trash2,
  Undo2,
  X,
} from 'lucide-react-native';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  Alert,
  BackHandler,
  Keyboard,
  Linking,
  Platform,
  Pressable,
  ScrollView,
  Share,
  StyleSheet,
  Text,
  type TextInputInstance,
  View,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { WebView } from 'react-native-webview';

import { useAuth } from '../sync/auth';
import { useSyncedDevices } from '../sync/devices';

import { ActionSheet, type SheetAction } from './ActionSheet';
import { BottomBar } from './BottomBar';
import { Favicon } from './Favicon';
import { StartPage } from './StartPage';
import { SyncedTabs } from './SyncedTabs';
import { TabSwitcher } from './TabSwitcher';
import { type Tab, useTabs } from './tabs';
import { colors, radius } from './theme';
import { Toast, type ToastMessage } from './Toast';
import { displayText, hostOf, isWebUrl, resolveInput } from './url';

// Background tabs keep their page alive up to this many; older ones reload
// from their last URL when you come back to them.
const MAX_LIVE_WEBVIEWS = 5;

// The library's default generic resolves its props to `never` under TS 6.
type WebViewInstance = WebView<object>;

type NavState = {
  canGoBack: boolean;
  canGoForward: boolean;
  loading: boolean;
  progress: number;
};

const IDLE_NAV: NavState = { canGoBack: false, canGoForward: false, loading: false, progress: 0 };

type Sheet = { title?: string; subtitle?: string; actions: SheetAction[] };

export function BrowserScreen() {
  const { state, activeTab, dispatch } = useTabs();
  const insets = useSafeAreaInsets();
  const keyboardHeight = useKeyboardHeight();
  const auth = useAuth();
  const devices = useSyncedDevices();

  const webviews = useRef(new Map<string, WebViewInstance | null>());
  const inputRef = useRef<TextInputInstance>(null);
  const [navById, setNavById] = useState<Record<string, NavState>>({});
  const [editing, setEditing] = useState(false);
  const [query, setQuery] = useState('');
  const [switcherOpen, setSwitcherOpen] = useState(false);
  const [sheet, setSheet] = useState<Sheet | null>(null);
  const [toast, setToast] = useState<ToastMessage | null>(null);

  const nav = navById[activeTab.id] ?? IDLE_NAV;

  // The most recently used tabs keep a mounted WebView.
  const liveIds = useMemo(() => {
    const recent = state.tabs
      .filter((t) => t.url !== null && t.id !== activeTab.id)
      .sort((a, b) => b.lastActiveAt - a.lastActiveAt)
      .map((t) => t.id);
    const live = activeTab.url !== null ? [activeTab.id, ...recent] : recent;
    return live.slice(0, MAX_LIVE_WEBVIEWS);
  }, [state.tabs, activeTab.id, activeTab.url]);

  const updateNav = useCallback((id: string, patch: Partial<NavState>) => {
    setNavById((prev) => ({ ...prev, [id]: { ...(prev[id] ?? IDLE_NAV), ...patch } }));
  }, []);

  const showToast = useCallback((message: Omit<ToastMessage, 'id'>) => {
    setToast({ ...message, id: Date.now() });
  }, []);
  const dismissToast = useCallback(() => setToast(null), []);

  // --- Tab operations -------------------------------------------------------

  const closeTab = useCallback(
    (tab: Tab) => {
      Haptics.impactAsync(Haptics.ImpactFeedbackStyle.Light).catch(() => {});
      dispatch({ type: 'close', id: tab.id });
      if (tab.url) {
        showToast({
          text: `Closed ${tab.title || hostOf(tab.url)}`,
          actionLabel: 'Undo',
          onAction: () => dispatch({ type: 'reopen' }),
        });
      }
    },
    [dispatch, showToast],
  );

  const openNewTab = useCallback(() => {
    dispatch({ type: 'open', url: null });
    setSwitcherOpen(false);
    setQuery('');
    setEditing(true);
  }, [dispatch]);

  const selectTab = useCallback(
    (id: string) => {
      dispatch({ type: 'activate', id });
      setSwitcherOpen(false);
    },
    [dispatch],
  );

  const swipeTab = useCallback(
    (direction: -1 | 1) => {
      const index = state.tabs.findIndex((t) => t.id === activeTab.id);
      const next = state.tabs[index + direction];
      Haptics.selectionAsync().catch(() => {});
      if (next) {
        dispatch({ type: 'activate', id: next.id });
      } else if (direction === 1 && activeTab.url !== null) {
        // Swiping past the last tab opens a fresh one, unless you're already on one.
        dispatch({ type: 'open', url: null });
      }
    },
    [state.tabs, activeTab, dispatch],
  );

  const clearToday = useCallback(() => {
    const today = state.tabs.filter((t) => !t.pinned);
    if (!today.length) return;
    const previousActive = state.activeId;
    Alert.alert(
      `Close ${today.length} tab${today.length === 1 ? '' : 's'}?`,
      'Pinned tabs stay put.',
      [
        { text: 'Cancel', style: 'cancel' },
        {
          text: 'Close',
          style: 'destructive',
          onPress: () => {
            dispatch({ type: 'clearToday' });
            const reopenable = today.filter((t) => t.url !== null).length;
            if (!reopenable) return;
            showToast({
              text: `Closed ${today.length} tab${today.length === 1 ? '' : 's'}`,
              actionLabel: 'Undo',
              onAction: () => {
                for (let i = 0; i < reopenable; i++) dispatch({ type: 'reopen' });
                dispatch({ type: 'activate', id: previousActive });
              },
            });
          },
        },
      ],
    );
  }, [state.tabs, state.activeId, dispatch, showToast]);

  const tabActions = useCallback(
    (tab: Tab): SheetAction[] => {
      const actions: SheetAction[] = [
        {
          label: tab.pinned ? 'Unpin Tab' : 'Pin Tab',
          icon: tab.pinned ? PinOff : Pin,
          onPress: () => dispatch({ type: 'togglePin', id: tab.id }),
        },
        {
          label: 'Duplicate Tab',
          icon: CopyPlus,
          onPress: () => {
            dispatch({ type: 'duplicate', id: tab.id });
            setSwitcherOpen(false);
          },
        },
      ];
      if (tab.url) {
        actions.push({
          label: 'Copy Link',
          icon: Copy,
          onPress: () => {
            Clipboard.setStringAsync(tab.url!).catch(() => {});
            showToast({ text: 'Link copied' });
          },
        });
      }
      actions.push({ label: 'Close Tab', icon: X, destructive: true, onPress: () => closeTab(tab) });
      return actions;
    },
    [dispatch, closeTab, showToast],
  );

  const openMenu = useCallback(() => {
    const actions: SheetAction[] = [];
    if (activeTab.url) {
      actions.push({
        label: 'Share',
        icon: ShareIcon,
        onPress: () => {
          Share.share(
            Platform.OS === 'ios' ? { url: activeTab.url! } : { message: activeTab.url! },
          ).catch(() => {});
        },
      });
    }
    actions.push(...tabActions(activeTab));
    if (state.recentlyClosed.length) {
      actions.splice(actions.length - 1, 0, {
        label: 'Reopen Closed Tab',
        icon: Undo2,
        onPress: () => dispatch({ type: 'reopen' }),
      });
    }
    setSheet({
      title: activeTab.title || displayText(activeTab.url) || 'Start Page',
      subtitle: activeTab.url ? hostOf(activeTab.url) : undefined,
      actions,
    });
  }, [activeTab, tabActions, state.recentlyClosed.length, dispatch]);

  const openTabsMenu = useCallback(() => {
    const actions: SheetAction[] = [{ label: 'New Tab', icon: Plus, onPress: openNewTab }];
    if (state.recentlyClosed.length) {
      actions.push({ label: 'Reopen Closed Tab', icon: Undo2, onPress: () => dispatch({ type: 'reopen' }) });
    }
    if (state.tabs.some((t) => !t.pinned)) {
      actions.push({ label: 'Clear Today', icon: Trash2, destructive: true, onPress: clearToday });
    }
    actions.push({ label: 'Close This Tab', icon: X, destructive: true, onPress: () => closeTab(activeTab) });
    setSheet({ actions });
  }, [openNewTab, state.recentlyClosed.length, state.tabs, clearToday, closeTab, activeTab, dispatch]);

  // --- Address bar ----------------------------------------------------------

  const startEditing = useCallback(() => {
    setQuery(activeTab.url ?? '');
    setEditing(true);
  }, [activeTab.url]);

  // selectTextOnFocus loses a race with autoFocus on iOS and leaves the caret
  // at the end, so typing would append to the URL. Select it once the field
  // is actually focused instead.
  const selectAddress = useCallback(() => {
    const length = activeTab.url?.length ?? 0;
    requestAnimationFrame(() => inputRef.current?.setSelection(0, length));
  }, [activeTab.url]);

  const cancelEditing = useCallback(() => {
    setEditing(false);
    Keyboard.dismiss();
  }, []);

  const go = useCallback(
    (raw: string) => {
      const url = resolveInput(raw);
      if (!url) return;
      setEditing(false);
      Keyboard.dismiss();
      if (url === activeTab.url) webviews.current.get(activeTab.id)?.reload();
      else dispatch({ type: 'navigate', id: activeTab.id, url });
    },
    [activeTab, dispatch],
  );

  const matchingTabs = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q || q === activeTab.url?.toLowerCase()) return [];
    return state.tabs
      .filter(
        (t) =>
          t.id !== activeTab.id &&
          t.url &&
          (t.title.toLowerCase().includes(q) || t.url.toLowerCase().includes(q)),
      )
      .slice(0, 4);
  }, [query, state.tabs, activeTab]);

  // --- Android back ---------------------------------------------------------

  useEffect(() => {
    const sub = BackHandler.addEventListener('hardwareBackPress', () => {
      if (sheet) return setSheet(null), true;
      if (switcherOpen) return setSwitcherOpen(false), true;
      if (editing) return cancelEditing(), true;
      if (nav.canGoBack) return webviews.current.get(activeTab.id)?.goBack(), true;
      // A tab opened from a link goes away on back, returning you to its opener.
      if (activeTab.openerId && state.tabs.some((t) => t.id === activeTab.openerId)) {
        dispatch({ type: 'close', id: activeTab.id });
        return true;
      }
      return false;
    });
    return () => sub.remove();
  }, [sheet, switcherOpen, editing, nav.canGoBack, activeTab, state.tabs, cancelEditing, dispatch]);

  if (!state.hydrated) return <View style={styles.root} />;

  const tabsById = new Map(state.tabs.map((t) => [t.id, t]));

  return (
    <View style={[styles.root, { paddingTop: insets.top }]}>
      <StatusBar style="light" />

      <View style={styles.content}>
        {liveIds.map((id) => {
          const tab = tabsById.get(id)!;
          return (
            <TabWebView
              key={id}
              tab={tab}
              active={id === activeTab.id}
              ref={(ref) => {
                webviews.current.set(id, ref);
              }}
              onNav={updateNav}
              onRecord={(url, title) => dispatch({ type: 'record', id, url, title })}
              onOpenWindow={(url) => dispatch({ type: 'open', url, afterId: id, openerId: id })}
            />
          );
        })}

        {activeTab.url === null ? (
          <StartPage
            recentlyClosed={state.recentlyClosed}
            onReopen={(closedAt) => dispatch({ type: 'reopen', closedAt })}
          />
        ) : null}

        {editing ? (
          <Pressable style={styles.scrim} onPress={cancelEditing}>
            <ScrollView
              style={styles.suggestions}
              contentContainerStyle={styles.suggestionsContent}
              keyboardShouldPersistTaps="handled"
            >
              {matchingTabs.map((tab) => (
                <Suggestion
                  key={tab.id}
                  icon={<Favicon url={tab.url} size={20} />}
                  title={tab.title || hostOf(tab.url)}
                  detail={`Switch to tab · ${hostOf(tab.url)}`}
                  trailing={<ArrowUpRight size={16} color={colors.textTertiary} />}
                  onPress={() => {
                    setEditing(false);
                    Keyboard.dismiss();
                    dispatch({ type: 'activate', id: tab.id });
                  }}
                />
              ))}
            </ScrollView>
          </Pressable>
        ) : null}
      </View>

      <View style={[styles.bottom, { paddingBottom: keyboardHeight || insets.bottom }]}>
        <BottomBar
          ref={inputRef}
          tab={activeTab}
          tabCount={state.tabs.length}
          hasPrevTab={state.tabs[0]?.id !== activeTab.id}
          progress={nav.progress}
          loading={nav.loading}
          canGoBack={nav.canGoBack}
          canGoForward={nav.canGoForward}
          editing={editing}
          query={query}
          onQueryChange={setQuery}
          onStartEditing={startEditing}
          onInputFocus={selectAddress}
          onCancelEditing={cancelEditing}
          onSubmit={go}
          onBack={() => webviews.current.get(activeTab.id)?.goBack()}
          onForward={() => webviews.current.get(activeTab.id)?.goForward()}
          onReloadOrStop={() => {
            const wv = webviews.current.get(activeTab.id);
            if (nav.loading) wv?.stopLoading();
            else wv?.reload();
          }}
          onNewTab={openNewTab}
          onShowTabs={() => {
            setEditing(false);
            setSwitcherOpen(true);
          }}
          onTabsLongPress={openTabsMenu}
          onMenu={openMenu}
          onSwipeTab={swipeTab}
        />
      </View>

      <TabSwitcher
        visible={switcherOpen}
        tabs={state.tabs}
        activeId={activeTab.id}
        onSelect={selectTab}
        onCloseTab={(id) => {
          const tab = tabsById.get(id);
          if (tab) closeTab(tab);
        }}
        onTabActions={(tab) =>
          setSheet({
            title: tab.title || hostOf(tab.url) || 'Start Page',
            subtitle: tab.url ? hostOf(tab.url) : undefined,
            actions: tabActions(tab),
          })
        }
        onNewTab={openNewTab}
        onClearToday={clearToday}
        onAccount={() => router.push('/account')}
        onDone={() => setSwitcherOpen(false)}
      >
        <SyncedTabs
          configured={auth.configured}
          signedIn={!!auth.session}
          devices={devices}
          onOpen={(url) => {
            const existing = state.tabs.find((t) => t.url === url);
            if (existing) dispatch({ type: 'activate', id: existing.id });
            else dispatch({ type: 'open', url });
            setSwitcherOpen(false);
          }}
          onAccount={() => router.push('/account')}
        />
      </TabSwitcher>

      <Toast
        toast={toast}
        onDismiss={dismissToast}
        bottom={insets.bottom + (switcherOpen ? 76 : 116)}
      />

      <ActionSheet
        visible={!!sheet}
        title={sheet?.title}
        subtitle={sheet?.subtitle}
        actions={sheet?.actions ?? []}
        onClose={() => setSheet(null)}
      />
    </View>
  );
}

// --- Pieces -----------------------------------------------------------------

type TabWebViewProps = {
  tab: Tab;
  active: boolean;
  ref: (ref: WebViewInstance | null) => void;
  onNav: (id: string, patch: Partial<NavState>) => void;
  onRecord: (url: string, title: string) => void;
  onOpenWindow: (url: string) => void;
};

function TabWebView({ tab, active, ref, onNav, onRecord, onOpenWindow }: TabWebViewProps) {
  // A WebView that was evicted and remounted resumes from where the tab was
  // left, not from the last URL typed into it.
  const [mount] = useState(() => ({
    requestId: tab.requestId,
    uri: tab.url ?? tab.requestedUrl ?? 'about:blank',
  }));
  const uri = tab.requestId === mount.requestId ? mount.uri : tab.requestedUrl ?? 'about:blank';
  const source = useMemo(() => ({ uri }), [uri]);
  const id = tab.id;

  return (
    <View style={[StyleSheet.absoluteFill, !active && styles.hidden]} pointerEvents={active ? 'auto' : 'none'}>
      <WebView<object>
        ref={ref}
        source={source}
        style={styles.webview}
        containerStyle={styles.webview}
        allowsBackForwardNavigationGestures
        pullToRefreshEnabled
        allowsInlineMediaPlayback
        sharedCookiesEnabled
        setSupportMultipleWindows
        decelerationRate="normal"
        forceDarkOn={false}
        onLoadStart={() => onNav(id, { loading: true, progress: 0.1 })}
        onLoadProgress={({ nativeEvent }) => onNav(id, { progress: nativeEvent.progress })}
        onLoadEnd={() => onNav(id, { loading: false, progress: 1 })}
        onNavigationStateChange={(e) => {
          onNav(id, { canGoBack: e.canGoBack, canGoForward: e.canGoForward });
          if (e.url && e.url !== 'about:blank') onRecord(e.url, e.title && e.title !== e.url ? e.title : '');
        }}
        onOpenWindow={({ nativeEvent }) => onOpenWindow(nativeEvent.targetUrl)}
        onShouldStartLoadWithRequest={(req) => {
          if (isWebUrl(req.url)) return true;
          // mailto:, tel:, app links and the like belong to the OS.
          Linking.openURL(req.url).catch(() => {});
          return false;
        }}
      />
    </View>
  );
}

function Suggestion({
  icon,
  title,
  detail,
  trailing,
  onPress,
}: {
  icon: React.ReactNode;
  title: string;
  detail: string;
  trailing?: React.ReactNode;
  onPress: () => void;
}) {
  return (
    <Pressable onPress={onPress} style={({ pressed }) => [styles.suggestion, pressed && styles.suggestionPressed]}>
      <View style={styles.suggestionIcon}>{icon}</View>
      <View style={styles.suggestionText}>
        <Text style={styles.suggestionTitle} numberOfLines={1}>
          {title}
        </Text>
        <Text style={styles.suggestionDetail} numberOfLines={1}>
          {detail}
        </Text>
      </View>
      {trailing}
    </Pressable>
  );
}

function useKeyboardHeight() {
  const [height, setHeight] = useState(0);
  useEffect(() => {
    const showEvent = Platform.OS === 'ios' ? 'keyboardWillShow' : 'keyboardDidShow';
    const hideEvent = Platform.OS === 'ios' ? 'keyboardWillHide' : 'keyboardDidHide';
    const show = Keyboard.addListener(showEvent, (e) => setHeight(e.endCoordinates.height));
    const hide = Keyboard.addListener(hideEvent, () => setHeight(0));
    return () => {
      show.remove();
      hide.remove();
    };
  }, []);
  return height;
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: colors.background },
  content: { flex: 1, backgroundColor: colors.background },
  webview: { flex: 1, backgroundColor: colors.background },
  hidden: { display: 'none' },
  bottom: { backgroundColor: colors.surface },
  scrim: {
    ...StyleSheet.absoluteFill,
    backgroundColor: 'rgba(16,20,24,0.82)',
    justifyContent: 'flex-end',
  },
  suggestions: { flexGrow: 0 },
  suggestionsContent: { padding: 12, gap: 6 },
  suggestion: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    paddingHorizontal: 14,
    paddingVertical: 11,
    borderRadius: radius.row,
    backgroundColor: colors.raised,
  },
  suggestionPressed: { backgroundColor: colors.border },
  suggestionIcon: { width: 22, alignItems: 'center' },
  suggestionText: { flex: 1 },
  suggestionTitle: { color: colors.text, fontSize: 15 },
  suggestionDetail: { color: colors.textTertiary, fontSize: 12, marginTop: 1 },
});
