import * as Clipboard from 'expo-clipboard';
import * as Haptics from 'expo-haptics';
import { router } from 'expo-router';
import { StatusBar } from 'expo-status-bar';
import {
  ArrowDown,
  ArrowUp,
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
import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import {
  Alert,
  AppState,
  BackHandler,
  Image,
  type ImageSourcePropType,
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
  type ViewInstance,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { WebView } from 'react-native-webview';

import { useAuth } from '../sync/auth';
import { deletePreview, previewPath, previewSource, uploadPreview } from '../sync/remote';
import { isShareableUrl } from '../sync/rows';

import { ActionSheet, type SheetAction } from './ActionSheet';
import { BottomBar } from './BottomBar';
import { Favicon } from './Favicon';
import {
  captureJpeg,
  PASSWORD_PROBE,
  parsePasswordProbe,
  PREVIEW_INTERVAL_MS,
  usePreviewsEnabled,
} from './previews';
import { StartPage } from './StartPage';
import { TabSwitcher } from './TabSwitcher';
import { type Tab, useTabs } from './tabs';
import { colors, radius } from './theme';
import { Toast, type ToastMessage } from './Toast';
import { displayText, hostOf, isWebUrl, resolveInput } from './url';

// The most recently used tabs keep their page alive; the rest unload and
// show their preview until they're selected and load again.
const MAX_LIVE_WEBVIEWS = 3;
// How long after a page finishes loading to capture its preview, so late
// layout and images are in.
const CAPTURE_DELAY_MS = 1200;
// Previews captured here, kept in memory so they show without a download.
const MAX_LOCAL_PREVIEWS = 12;

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
  const { state, spaces, activeSpace, tabs, activeTab, attached, dispatch } = useTabs();
  const insets = useSafeAreaInsets();
  const keyboardHeight = useKeyboardHeight();
  const { session } = useAuth();
  const previewsOn = usePreviewsEnabled();

  const webviews = useRef(new Map<string, WebViewInstance | null>());
  const containers = useRef(new Map<string, ViewInstance | null>());
  const contentSize = useRef({ width: 0, height: 0 });
  // Per tab: does the page have a password field (null until it reports).
  const passwordFields = useRef(new Map<string, boolean | null>());
  const lastCapture = useRef(new Map<string, number>());
  const inputRef = useRef<TextInputInstance>(null);
  const [navById, setNavById] = useState<Record<string, NavState>>({});
  const [localPreviews, setLocalPreviews] = useState<Record<string, string>>({});
  const [editing, setEditing] = useState(false);
  const [query, setQuery] = useState('');
  const [switcherOpen, setSwitcherOpen] = useState(false);
  const [sheet, setSheet] = useState<Sheet | null>(null);
  const [toast, setToast] = useState<ToastMessage | null>(null);

  const nav = navById[activeTab?.id ?? ''] ?? IDLE_NAV;
  const tabsById = useMemo(() => new Map(tabs.map((t) => [t.id, t])), [tabs]);
  const latestTabs = useRef(tabsById);
  const latestActiveId = useRef(activeTab?.id);
  useLayoutEffect(() => {
    latestTabs.current = tabsById;
    latestActiveId.current = activeTab?.id;
  });

  // The most recently used tabs keep a mounted WebView. A tab that arrived
  // from another device stays unloaded until it's selected.
  const liveIds = useMemo(() => {
    if (!activeTab) return [];
    const recent = tabs
      .filter((t) => t.url !== null && t.id !== activeTab.id && t.lastUsedAt > 0)
      .sort((a, b) => b.lastUsedAt - a.lastUsedAt)
      .map((t) => t.id);
    const live = activeTab.url !== null ? [activeTab.id, ...recent] : recent;
    return live.slice(0, MAX_LIVE_WEBVIEWS);
  }, [tabs, activeTab]);

  const updateNav = useCallback((id: string, patch: Partial<NavState>) => {
    setNavById((prev) => ({ ...prev, [id]: { ...(prev[id] ?? IDLE_NAV), ...patch } }));
  }, []);

  const showToast = useCallback((message: Omit<ToastMessage, 'id'>) => {
    setToast({ ...message, id: Date.now() });
  }, []);
  // Until something else comes up, say what the first sign-in did with this
  // phone's tabs.
  const merged = state.merged;
  const shownToast = useMemo<ToastMessage | null>(
    () =>
      toast ??
      (merged ? { id: -merged, text: `Added ${merged} tab${merged === 1 ? '' : 's'} from this phone to your account` } : null),
    [toast, merged],
  );
  const dismissToast = useCallback(() => {
    if (merged) dispatch({ type: 'dismissMerged' });
    setToast(null);
  }, [merged, dispatch]);

  // --- Previews -------------------------------------------------------------

  const userId = session?.user.id ?? null;
  const canPreview = previewsOn && attached && !!userId;

  // Only the device showing a tab captures it, at most once a minute, and
  // never a page with a password field.
  const capturePreview = useCallback(
    (tab: Tab | undefined) => {
      if (!tab || !canPreview || !userId || !tab.synced || !isShareableUrl(tab.url)) return;
      if (passwordFields.current.get(tab.id) !== false) return;
      if (Date.now() - (lastCapture.current.get(tab.id) ?? 0) < PREVIEW_INTERVAL_MS) return;
      const view = containers.current.get(tab.id);
      if (!view) return;
      lastCapture.current.set(tab.id, Date.now());
      const path = previewPath(userId, tab.id);
      const { width, height } = contentSize.current;
      captureJpeg(view, width, height)
        .then(async (shot) => {
          if (!shot) return;
          await uploadPreview(path, shot.bytes);
          setLocalPreviews((prev) => {
            const next = { ...prev, [tab.id]: shot.dataUri };
            const keys = Object.keys(next);
            for (const key of keys.slice(0, Math.max(0, keys.length - MAX_LOCAL_PREVIEWS))) delete next[key];
            return next;
          });
          dispatch({ type: 'setPreview', id: tab.id, path });
        })
        .catch(() => {});
    },
    [canPreview, userId, dispatch],
  );

  const onPasswordField = useCallback(
    (id: string, has: boolean) => {
      passwordFields.current.set(id, has);
      // The page stopped qualifying for a preview: take its preview down.
      const tab = latestTabs.current.get(id);
      if (!has || !tab?.previewPath) return;
      deletePreview(tab.previewPath).catch(() => {});
      dispatch({ type: 'setPreview', id, path: null });
    },
    [dispatch],
  );

  const scheduleCapture = useCallback(
    (id: string) => {
      setTimeout(() => {
        if (id === latestActiveId.current) capturePreview(latestTabs.current.get(id));
      }, CAPTURE_DELAY_MS);
    },
    [capturePreview],
  );

  // Leaving the app counts as leaving the tab.
  useEffect(() => {
    const sub = AppState.addEventListener('change', (s) => {
      if (s !== 'active') capturePreview(latestTabs.current.get(latestActiveId.current ?? ''));
    });
    return () => sub.remove();
  }, [capturePreview]);

  const previewFor = useCallback(
    (tab: Tab): ImageSourcePropType | undefined => {
      const local = localPreviews[tab.id];
      if (local) return { uri: local };
      if (!tab.previewPath || !session) return undefined;
      return previewSource(tab.previewPath, tab.previewUpdatedAt, session.access_token);
    },
    [localPreviews, session],
  );

  // --- Tab operations -------------------------------------------------------

  // Every way of leaving the current tab goes through here first, so its
  // preview is taken while it's still on screen.
  const leave = useCallback(() => capturePreview(activeTab), [capturePreview, activeTab]);

  const dropPreview = useCallback((tab: Tab) => {
    if (tab.previewPath) deletePreview(tab.previewPath).catch(() => {});
  }, []);

  const closeTab = useCallback(
    (tab: Tab) => {
      Haptics.impactAsync(Haptics.ImpactFeedbackStyle.Light).catch(() => {});
      if (!tab.pinned) dropPreview(tab);
      dispatch({ type: 'close', id: tab.id });
      if (tab.url && !tab.pinned) {
        showToast({
          text: `Closed ${tab.title || hostOf(tab.url)}`,
          actionLabel: 'Undo',
          onAction: () => dispatch({ type: 'reopen' }),
        });
      }
    },
    [dispatch, showToast, dropPreview],
  );

  const removeItem = useCallback(
    (tab: Tab) => {
      dropPreview(tab);
      dispatch({ type: 'remove', id: tab.id });
      showToast({
        text: `Removed ${tab.title || hostOf(tab.rowUrl)}`,
        actionLabel: 'Undo',
        onAction: () => dispatch({ type: 'reopen' }),
      });
    },
    [dispatch, showToast, dropPreview],
  );

  const openNewTab = useCallback(() => {
    leave();
    dispatch({ type: 'open', url: null });
    setSwitcherOpen(false);
    setQuery('');
    setEditing(true);
  }, [dispatch, leave]);

  const selectTab = useCallback(
    (id: string) => {
      dispatch({ type: 'activate', id });
      setSwitcherOpen(false);
    },
    [dispatch],
  );

  const swipeTab = useCallback(
    (direction: -1 | 1) => {
      const index = tabs.findIndex((t) => t.id === activeTab.id);
      const next = tabs[index + direction];
      Haptics.selectionAsync().catch(() => {});
      leave();
      if (next) {
        dispatch({ type: 'activate', id: next.id });
      } else if (direction === 1 && activeTab.url !== null) {
        // Swiping past the last tab opens a fresh one, unless you're already on one.
        dispatch({ type: 'open', url: null });
      }
    },
    [tabs, activeTab, dispatch, leave],
  );

  const clearToday = useCallback(() => {
    const today = tabs.filter((t) => !t.pinned);
    if (!today.length) return;
    const previousActive = activeTab.id;
    const where = activeSpace?.name ? ` in ${activeSpace.name}` : '';
    Alert.alert(
      `Close ${today.length} tab${today.length === 1 ? '' : 's'}${where}?`,
      'They close on your other devices too. Pinned tabs stay put.',
      [
        { text: 'Cancel', style: 'cancel' },
        {
          text: 'Close',
          style: 'destructive',
          onPress: () => {
            today.forEach(dropPreview);
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
  }, [tabs, activeTab, activeSpace, dispatch, showToast, dropPreview]);

  const tabActions = useCallback(
    (tab: Tab): SheetAction[] => {
      const actions: SheetAction[] = [];
      if (tab.kind !== 'favorite' && tab.synced) {
        actions.push({
          label: tab.pinned ? 'Unpin Tab' : 'Pin Tab',
          icon: tab.pinned ? PinOff : Pin,
          onPress: () => dispatch({ type: 'togglePin', id: tab.id }),
        });
      }
      actions.push({
        label: 'Duplicate Tab',
        icon: CopyPlus,
        onPress: () => {
          dispatch({ type: 'duplicate', id: tab.id });
          setSwitcherOpen(false);
        },
      });
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
      // Pinned items and Favorites sit in a grid; today tabs in a list.
      const group = tabs.filter((t) => t.kind === tab.kind && t.parentId === tab.parentId);
      const index = group.findIndex((t) => t.id === tab.id);
      const [earlier, later] = tab.pinned ? ['Move Left', 'Move Right'] : ['Move Up', 'Move Down'];
      if (index > 0) {
        actions.push({ label: earlier, icon: ArrowUp, onPress: () => dispatch({ type: 'move', id: tab.id, by: -1 }) });
      }
      if (index >= 0 && index < group.length - 1) {
        actions.push({ label: later, icon: ArrowDown, onPress: () => dispatch({ type: 'move', id: tab.id, by: 1 }) });
      }
      actions.push({ label: 'Close Tab', icon: X, destructive: true, onPress: () => closeTab(tab) });
      if (tab.pinned) {
        actions.push({
          label: tab.kind === 'favorite' ? 'Remove Favorite' : 'Remove Pinned Tab',
          icon: Trash2,
          destructive: true,
          onPress: () => removeItem(tab),
        });
      }
      return actions;
    },
    [tabs, dispatch, closeTab, removeItem, showToast],
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
      const closeIndex = actions.findIndex((a) => a.label === 'Close Tab');
      actions.splice(closeIndex, 0, {
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
    if (tabs.some((t) => !t.pinned)) {
      actions.push({ label: 'Clear Today', icon: Trash2, destructive: true, onPress: clearToday });
    }
    actions.push({ label: 'Close This Tab', icon: X, destructive: true, onPress: () => closeTab(activeTab) });
    setSheet({ actions });
  }, [openNewTab, state.recentlyClosed.length, tabs, clearToday, closeTab, activeTab, dispatch]);

  // --- Spaces ---------------------------------------------------------------

  const switchSpace = useCallback(
    (id: string) => {
      if (id === state.activeSpaceId) return;
      Haptics.selectionAsync().catch(() => {});
      dispatch({ type: 'switchSpace', id });
    },
    [state.activeSpaceId, dispatch],
  );

  // Alert.prompt is iOS-only; on Android, Spaces are made and renamed on the
  // desktop for now.
  const newSpace =
    Platform.OS === 'ios'
      ? () =>
          Alert.prompt('New Space', undefined, (name) => {
            if (name?.trim()) dispatch({ type: 'createSpace', name: name.trim() });
          })
      : undefined;

  const renameSpace =
    Platform.OS === 'ios'
      ? (id: string) => {
          const space = spaces.find((s) => s.id === id);
          if (!space) return;
          Alert.prompt(
            'Rename Space',
            undefined,
            (name) => {
              if (name?.trim()) dispatch({ type: 'renameSpace', id, name: name.trim() });
            },
            'plain-text',
            space.name,
          );
        }
      : undefined;

  // --- Address bar ----------------------------------------------------------

  const startEditing = useCallback(() => {
    setQuery(activeTab.url ?? '');
    setEditing(true);
  }, [activeTab]);

  // selectTextOnFocus loses a race with autoFocus on iOS and leaves the caret
  // at the end, so typing would append to the URL. Select it once the field
  // is actually focused instead.
  const selectAddress = useCallback(() => {
    const length = activeTab.url?.length ?? 0;
    requestAnimationFrame(() => inputRef.current?.setSelection(0, length));
  }, [activeTab]);

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
    if (!activeTab || !q || q === activeTab.url?.toLowerCase()) return [];
    return tabs
      .filter(
        (t) =>
          t.id !== activeTab.id &&
          t.url &&
          (t.title.toLowerCase().includes(q) || t.url.toLowerCase().includes(q)),
      )
      .slice(0, 4);
  }, [query, tabs, activeTab]);

  // --- Android back ---------------------------------------------------------

  useEffect(() => {
    if (!activeTab) return;
    const sub = BackHandler.addEventListener('hardwareBackPress', () => {
      if (sheet) return setSheet(null), true;
      if (switcherOpen) return setSwitcherOpen(false), true;
      if (editing) return cancelEditing(), true;
      if (nav.canGoBack) return webviews.current.get(activeTab.id)?.goBack(), true;
      // A tab opened from a link goes away on back, returning you to its opener.
      if (activeTab.openerId && tabs.some((t) => t.id === activeTab.openerId)) {
        dispatch({ type: 'close', id: activeTab.id });
        return true;
      }
      return false;
    });
    return () => sub.remove();
  }, [sheet, switcherOpen, editing, nav.canGoBack, activeTab, tabs, cancelEditing, dispatch]);

  if (!state.hydrated || !activeTab) return <View style={styles.root} />;

  return (
    <View style={[styles.root, { paddingTop: insets.top }]}>
      <StatusBar style="light" />

      <View
        style={styles.content}
        onLayout={(e) => {
          contentSize.current = e.nativeEvent.layout;
        }}
      >
        {liveIds.map((id) => {
          const tab = tabsById.get(id)!;
          return (
            <TabWebView
              key={id}
              tab={tab}
              active={id === activeTab.id}
              preview={previewFor(tab)}
              ref={(ref) => {
                webviews.current.set(id, ref);
              }}
              containerRef={(ref) => {
                containers.current.set(id, ref);
              }}
              onNav={updateNav}
              onRecord={(url, title) => dispatch({ type: 'record', id, url, title })}
              onOpenWindow={(url) => {
                leave();
                dispatch({ type: 'open', url, afterId: id, openerId: id });
              }}
              onLoadStarted={() => passwordFields.current.set(id, null)}
              onLoaded={() => scheduleCapture(id)}
              onPasswordField={(has) => onPasswordField(id, has)}
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
                    leave();
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
          tabCount={tabs.length}
          hasPrevTab={tabs[0]?.id !== activeTab.id}
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
            leave();
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
        tabs={tabs}
        activeId={activeTab.id}
        spaces={spaces}
        activeSpaceId={state.activeSpaceId}
        onSwitchSpace={switchSpace}
        onNewSpace={newSpace}
        onRenameSpace={renameSpace}
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
      />

      <Toast
        toast={shownToast}
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
  // Shown over the page until it first paints.
  preview?: ImageSourcePropType;
  ref: (ref: WebViewInstance | null) => void;
  containerRef: (ref: ViewInstance | null) => void;
  onNav: (id: string, patch: Partial<NavState>) => void;
  onRecord: (url: string, title: string) => void;
  onOpenWindow: (url: string) => void;
  onLoadStarted: () => void;
  onLoaded: () => void;
  onPasswordField: (has: boolean) => void;
};

// Progress past which a page has usually painted something worth showing.
const PAINTED_PROGRESS = 0.7;

function TabWebView({
  tab,
  active,
  preview,
  ref,
  containerRef,
  onNav,
  onRecord,
  onOpenWindow,
  onLoadStarted,
  onLoaded,
  onPasswordField,
}: TabWebViewProps) {
  // A WebView that was unloaded and mounted again resumes from where the tab
  // was left, not from the last URL typed into it.
  const [mount] = useState(() => ({
    requestId: tab.requestId,
    uri: tab.url ?? tab.requestedUrl ?? 'about:blank',
  }));
  // The preview covers only the first load after mounting.
  const [painted, setPainted] = useState(false);
  const uri = tab.requestId === mount.requestId ? mount.uri : tab.requestedUrl ?? 'about:blank';
  const source = useMemo(() => ({ uri }), [uri]);
  const id = tab.id;

  return (
    <View
      ref={containerRef}
      collapsable={false}
      style={[StyleSheet.absoluteFill, !active && styles.hidden]}
      pointerEvents={active ? 'auto' : 'none'}
    >
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
        injectedJavaScript={PASSWORD_PROBE}
        onMessage={({ nativeEvent }) => {
          const has = parsePasswordProbe(nativeEvent.data);
          if (has !== null) onPasswordField(has);
        }}
        onLoadStart={() => {
          onNav(id, { loading: true, progress: 0.1 });
          onLoadStarted();
        }}
        onLoadProgress={({ nativeEvent }) => {
          onNav(id, { progress: nativeEvent.progress });
          if (nativeEvent.progress >= PAINTED_PROGRESS) setPainted(true);
        }}
        onLoadEnd={() => {
          onNav(id, { loading: false, progress: 1 });
          setPainted(true);
          onLoaded();
        }}
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
      {preview && !painted ? (
        <Image source={preview} style={styles.preview} resizeMode="cover" pointerEvents="none" />
      ) : null}
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
  preview: { ...StyleSheet.absoluteFill, backgroundColor: colors.background },
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
