import * as Clipboard from 'expo-clipboard';
import * as Haptics from 'expo-haptics';
import { router } from 'expo-router';
import { StatusBar } from 'expo-status-bar';
import {
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
import { memo, useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import {
  Alert,
  AppState,
  BackHandler,
  Image,
  type ImageSourcePropType,
  Keyboard,
  Linking,
  Platform,
  Share,
  StyleSheet,
  type TextInputInstance,
  View,
  type ViewInstance,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { WebView } from 'react-native-webview';

import { AUTH_REDIRECT, useAuth } from '../sync/auth';
import { deletePreview, previewPath, previewSource, uploadPreview } from '../sync/remote';
import { isShareableUrl } from '../sync/rows';

import { ActionSheet, type SheetAction } from './ActionSheet';
import { BottomBar } from './BottomBar';
import { onWebUrl } from './incoming';
import {
  captureJpeg,
  PASSWORD_PROBE,
  parsePasswordProbe,
  PREVIEW_INTERVAL_MS,
  usePreviewsEnabled,
} from './previews';
import { QuickNav } from './QuickNav';
import { StartPage } from './StartPage';
import { TabBackdrop } from './TabBackdrop';
import { TabSwitcher } from './TabSwitcher';
import { type Tab, useTabs } from './tabs';
import { colors } from './theme';
import { Toast, type ToastMessage } from './Toast';
import { displayText, editText, hostOf, isWebUrl, resolveInput } from './url';

// The most recently used tabs keep their page alive; the rest unload and
// show their preview until they're selected and load again.
const MAX_LIVE_WEBVIEWS = 3;
// How long after a page finishes loading to capture its preview, so late
// layout and images are in.
const CAPTURE_DELAY_MS = 1200;
// A capture snapshots the page on the main thread, which stalls whatever is
// animating. So it waits until this long after the last swipe or tab switch,
// with no tab switcher, menu or address bar up, and gives up after a few
// tries.
const CAPTURE_IDLE_MS = 1500;
const MAX_CAPTURE_RETRIES = 4;
// Creating or tearing down a WebView takes the main thread for a moment,
// which froze the address bar halfway through a swipe. So the set of loaded
// pages stays as it was until a swipe or tab switch has settled, and the new
// tab's preview stands in until then.
const SETTLE_MS = 380;
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
  const { state, spaces, activeSpace, tabs, folders, activeTab, attached, dispatch } = useTabs();
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
  const latestNav = useRef(navById);
  // Nothing is moving and nothing covers the page: safe to capture it.
  const calm = useRef(true);
  const lastTransition = useRef(0);
  useLayoutEffect(() => {
    latestTabs.current = tabsById;
    latestActiveId.current = activeTab?.id;
    latestNav.current = navById;
    calm.current = !switcherOpen && !editing && !sheet;
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

  // See SETTLE_MS. While held, these are the pages that stay mounted.
  const [heldIds, setHeldIds] = useState<string[] | null>(null);
  const latestLiveIds = useRef(liveIds);
  const holdTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  useLayoutEffect(() => {
    latestLiveIds.current = liveIds;
  });
  useEffect(() => () => clearTimeout(holdTimer.current), []);
  const holdPages = useCallback(() => {
    setHeldIds((held) => held ?? latestLiveIds.current);
    clearTimeout(holdTimer.current);
    holdTimer.current = setTimeout(() => setHeldIds(null), SETTLE_MS);
  }, []);
  const mountedIds = useMemo(
    () => (heldIds ?? liveIds).filter((id) => tabsById.has(id)),
    [heldIds, liveIds, tabsById],
  );

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
      let retries = 0;
      const attempt = () => {
        if (id !== latestActiveId.current) return;
        if (!calm.current || Date.now() - lastTransition.current < CAPTURE_IDLE_MS) {
          if (++retries <= MAX_CAPTURE_RETRIES) setTimeout(attempt, CAPTURE_IDLE_MS);
          return;
        }
        capturePreview(latestTabs.current.get(id));
      };
      setTimeout(attempt, CAPTURE_DELAY_MS);
    },
    [capturePreview],
  );

  // Every switch of tab goes through here. It used to capture the tab being
  // left, but that snapshot landed mid-animation; previews are now taken
  // only once a page has loaded and the screen is calm.
  const markTransition = useCallback(() => {
    lastTransition.current = Date.now();
  }, []);

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
    markTransition();
    dispatch({ type: 'open', url: null });
    setSwitcherOpen(false);
    setQuery('');
    setEditing(true);
  }, [dispatch, markTransition]);

  // Links from other apps, when Avora is the default browser, each open in a
  // new tab. Waits for the saved tabs so the new one isn't lost to them.
  const hydrated = state.hydrated;
  useEffect(() => {
    if (!hydrated) return;
    return onWebUrl((url) => {
      markTransition();
      dispatch({ type: 'open', url });
      setSwitcherOpen(false);
      setSheet(null);
      setEditing(false);
      Keyboard.dismiss();
    });
  }, [hydrated, dispatch, markTransition]);

  const selectTab = useCallback(
    (id: string) => {
      markTransition();
      holdPages();
      dispatch({ type: 'activate', id });
      setSwitcherOpen(false);
    },
    [dispatch, markTransition, holdPages],
  );

  const swipeTab = useCallback(
    (direction: -1 | 1) => {
      const index = tabs.findIndex((t) => t.id === activeTab.id);
      const next = tabs[index + direction];
      Haptics.selectionAsync().catch(() => {});
      markTransition();
      holdPages();
      if (next) {
        dispatch({ type: 'activate', id: next.id });
      } else if (direction === 1 && activeTab.url !== null) {
        // Swiping past the last tab opens a fresh one, unless you're already on one.
        dispatch({ type: 'open', url: null });
      }
    },
    [tabs, activeTab, dispatch, markTransition, holdPages],
  );

  // --- Per-tab callbacks ----------------------------------------------------
  // Stable, so a page's loading progress re-renders only the bar, not every
  // live page and the tab switcher.

  const registerWebView = useCallback((id: string, ref: WebViewInstance | null) => {
    webviews.current.set(id, ref);
  }, []);
  const registerContainer = useCallback((id: string, ref: ViewInstance | null) => {
    containers.current.set(id, ref);
  }, []);
  const recordPage = useCallback(
    (id: string, url: string, title: string) => dispatch({ type: 'record', id, url, title }),
    [dispatch],
  );
  const openWindow = useCallback(
    (id: string, url: string) => {
      markTransition();
      dispatch({ type: 'open', url, afterId: id, openerId: id });
    },
    [dispatch, markTransition],
  );
  const loadStarted = useCallback((id: string) => passwordFields.current.set(id, null), []);
  const reloadTerminated = useCallback((id: string) => webviews.current.get(id)?.reload(), []);
  // A sign-in or confirmation link from an email opens in a tab only to
  // redirect back into the app. Once it has, that tab has nothing to show,
  // unless it was already on a page of its own before.
  const handOff = useCallback(
    (id: string, url: string) => {
      if (!url.startsWith(AUTH_REDIRECT) || latestNav.current[id]?.canGoBack) return;
      dispatch({ type: 'close', id, forget: true });
    },
    [dispatch],
  );

  // The active tab's is shown even before its page is mounted (SETTLE_MS).
  const activeId = activeTab?.id;
  const livePreviews = useMemo(() => {
    const ids = new Set([...mountedIds, ...(activeId ? [activeId] : [])]);
    return new Map([...ids].map((id) => [id, previewFor(tabsById.get(id)!)]));
  }, [mountedIds, activeId, tabsById, previewFor]);

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
    [dispatch, closeTab, removeItem, showToast],
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
  const promptNewSpace = useCallback(
    () =>
      Alert.prompt('New Space', undefined, (name) => {
        if (name?.trim()) dispatch({ type: 'createSpace', name: name.trim() });
      }),
    [dispatch],
  );
  const newSpace = Platform.OS === 'ios' ? promptNewSpace : undefined;

  const promptRenameSpace = useCallback(
    (id: string) => {
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
    },
    [spaces, dispatch],
  );
  const renameSpace = Platform.OS === 'ios' ? promptRenameSpace : undefined;

  // --- Tab switcher callbacks -----------------------------------------------

  const closeTabById = useCallback(
    (id: string) => {
      const tab = latestTabs.current.get(id);
      if (tab) closeTab(tab);
    },
    [closeTab],
  );
  const moveTab = useCallback((id: string, by: number) => dispatch({ type: 'move', id, by }), [dispatch]);
  const showTabActions = useCallback(
    (tab: Tab) =>
      setSheet({
        title: tab.title || hostOf(tab.url) || 'Start Page',
        subtitle: tab.url ? hostOf(tab.url) : undefined,
        actions: tabActions(tab),
      }),
    [tabActions],
  );
  const openAccount = useCallback(() => router.push('/account'), []);
  const closeSwitcher = useCallback(() => setSwitcherOpen(false), []);

  // --- Address bar ----------------------------------------------------------

  const startEditing = useCallback(() => {
    setQuery(editText(activeTab.url));
    setEditing(true);
  }, [activeTab]);

  // selectTextOnFocus loses a race with autoFocus on iOS and leaves the caret
  // at the end, so typing would append to the URL. Select it once the field
  // is actually focused instead.
  const selectAddress = useCallback(() => {
    const length = editText(activeTab.url).length;
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
      const unchanged = url === activeTab.url || raw.trim() === editText(activeTab.url);
      if (unchanged) webviews.current.get(activeTab.id)?.reload();
      else dispatch({ type: 'navigate', id: activeTab.id, url });
    },
    [activeTab, dispatch],
  );

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

      {/* Pages stay clear of the Dynamic Island and rounded corners in
          landscape; the WebView doesn't inset itself for them. */}
      <View style={[styles.content, { paddingLeft: insets.left, paddingRight: insets.right }]}>
        {/* Tabs fill this absolutely, which would ignore the padding above. */}
        <View
          style={styles.pages}
          onLayout={(e) => {
            contentSize.current = e.nativeEvent.layout;
          }}
        >
          {mountedIds.map((id) => (
            <TabWebView
              key={id}
              tab={tabsById.get(id)!}
              active={id === activeTab.id}
              preview={livePreviews.get(id)}
              onWebViewRef={registerWebView}
              onContainerRef={registerContainer}
              onNav={updateNav}
              onRecord={recordPage}
              onOpenWindow={openWindow}
              onLoadStarted={loadStarted}
              onLoaded={scheduleCapture}
              onTerminated={reloadTerminated}
              onPasswordField={onPasswordField}
              onHandOff={handOff}
            />
          ))}

          {activeTab.url !== null && !mountedIds.includes(activeTab.id) ? (
            <PagePlaceholder tab={activeTab} preview={livePreviews.get(activeTab.id)} />
          ) : null}

          {activeTab.url === null ? (
            <StartPage
              recentlyClosed={state.recentlyClosed}
              onReopen={(closedAt) => dispatch({ type: 'reopen', closedAt })}
            />
          ) : null}
        </View>
      </View>

      {/* While the overlay is up the keyboard belongs to it, so the bar stays put
          underneath; otherwise it rides up with the keyboard for page inputs. */}
      <View style={[styles.bottom, { paddingBottom: (!editing && keyboardHeight) || insets.bottom }]}>
        <BottomBar
          tab={activeTab}
          tabCount={tabs.length}
          spaceName={activeSpace?.name || 'Tabs'}
          spaceColor={activeSpace?.color || colors.accent}
          hasPrevTab={tabs[0]?.id !== activeTab.id}
          progress={nav.progress}
          loading={nav.loading}
          canGoBack={nav.canGoBack}
          canGoForward={nav.canGoForward}
          onStartEditing={startEditing}
          onBack={() => webviews.current.get(activeTab.id)?.goBack()}
          onForward={() => webviews.current.get(activeTab.id)?.goForward()}
          onReloadOrStop={() => {
            const wv = webviews.current.get(activeTab.id);
            if (nav.loading) wv?.stopLoading();
            else wv?.reload();
          }}
          onNewTab={openNewTab}
          onShowTabs={() => {
            markTransition();
            setEditing(false);
            setSwitcherOpen(true);
          }}
          onTabsLongPress={openTabsMenu}
          onMenu={openMenu}
          onSwipeTab={swipeTab}
        />
      </View>

      {editing ? (
        <QuickNav
          inputRef={inputRef}
          currentTab={activeTab}
          tabs={tabs}
          query={query}
          keyboardHeight={keyboardHeight}
          onQueryChange={setQuery}
          onInputFocus={selectAddress}
          onSubmit={go}
          onSelectTab={(tab) => {
            setEditing(false);
            Keyboard.dismiss();
            if (tab.id === activeTab.id) return;
            markTransition();
            holdPages();
            dispatch({ type: 'activate', id: tab.id });
          }}
          onCancel={cancelEditing}
        />
      ) : null}

      <TabSwitcher
        visible={switcherOpen}
        tabs={tabs}
        folders={folders}
        activeId={activeTab.id}
        spaces={spaces}
        activeSpaceId={state.activeSpaceId}
        onSwitchSpace={switchSpace}
        onNewSpace={newSpace}
        onRenameSpace={renameSpace}
        onSelect={selectTab}
        onCloseTab={closeTabById}
        onMoveTab={moveTab}
        onTabActions={showTabActions}
        onNewTab={openNewTab}
        onClearToday={clearToday}
        onAccount={openAccount}
        onDone={closeSwitcher}
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

// Every callback takes the tab's id and is stable, so the component is
// memoized and skips the many renders a loading page causes elsewhere.
type TabWebViewProps = {
  tab: Tab;
  active: boolean;
  // Shown over the page until it first paints.
  preview?: ImageSourcePropType;
  onWebViewRef: (id: string, ref: WebViewInstance | null) => void;
  onContainerRef: (id: string, ref: ViewInstance | null) => void;
  onNav: (id: string, patch: Partial<NavState>) => void;
  onRecord: (id: string, url: string, title: string) => void;
  onOpenWindow: (id: string, url: string) => void;
  onLoadStarted: (id: string) => void;
  onLoaded: (id: string) => void;
  // iOS reclaimed the page's content process (usually while the app was in
  // the background), leaving the WebView blank.
  onTerminated: (id: string) => void;
  onPasswordField: (id: string, has: boolean) => void;
  // The page sent a link to another app (or back to this one) instead of
  // loading it.
  onHandOff: (id: string, url: string) => void;
};

// Progress past which a page has usually painted something worth showing.
const PAINTED_PROGRESS = 0.7;

const TabWebView = memo(function TabWebView({
  tab,
  active,
  preview,
  onWebViewRef,
  onContainerRef,
  onNav,
  onRecord,
  onOpenWindow,
  onLoadStarted,
  onLoaded,
  onTerminated,
  onPasswordField,
  onHandOff,
}: TabWebViewProps) {
  // A WebView that was unloaded and mounted again resumes from where the tab
  // was left, not from the last URL typed into it.
  const [mount] = useState(() => ({
    requestId: tab.requestId,
    uri: tab.url ?? tab.requestedUrl ?? 'about:blank',
  }));
  // The preview (or, without one, the backdrop) covers only the first load
  // after mounting.
  const [painted, setPainted] = useState(false);
  const uri = tab.requestId === mount.requestId ? mount.uri : tab.requestedUrl ?? 'about:blank';
  const source = useMemo(() => ({ uri }), [uri]);
  const id = tab.id;
  const webViewRef = useCallback((ref: WebViewInstance | null) => onWebViewRef(id, ref), [id, onWebViewRef]);
  const containerRef = useCallback((ref: ViewInstance | null) => onContainerRef(id, ref), [id, onContainerRef]);

  return (
    <View
      ref={containerRef}
      collapsable={false}
      style={[StyleSheet.absoluteFill, !active && styles.hidden]}
      pointerEvents={active ? 'auto' : 'none'}
    >
      <WebView<object>
        ref={webViewRef}
        source={source}
        style={styles.webview}
        containerStyle={styles.webview}
        allowsBackForwardNavigationGestures
        pullToRefreshEnabled
        allowsInlineMediaPlayback
        // No sharedCookiesEnabled: every tab already shares iOS's persistent
        // website data store, so logins carry across tabs and launches. That
        // option also copied the app's whole cookie jar into each new WebView
        // and back again on every cookie change, all on the main thread, which
        // froze the screen for seconds whenever a tab loaded.
        setSupportMultipleWindows
        decelerationRate="normal"
        forceDarkOn={false}
        injectedJavaScript={PASSWORD_PROBE}
        onMessage={({ nativeEvent }) => {
          const has = parsePasswordProbe(nativeEvent.data);
          if (has !== null) onPasswordField(id, has);
        }}
        onLoadStart={() => {
          onNav(id, { loading: true, progress: 0.1 });
          onLoadStarted(id);
        }}
        onLoadProgress={({ nativeEvent }) => {
          onNav(id, { progress: nativeEvent.progress });
          if (nativeEvent.progress >= PAINTED_PROGRESS) setPainted(true);
        }}
        onLoadEnd={() => {
          onNav(id, { loading: false, progress: 1 });
          setPainted(true);
          onLoaded(id);
        }}
        onNavigationStateChange={(e) => {
          onNav(id, { canGoBack: e.canGoBack, canGoForward: e.canGoForward });
          if (e.url && e.url !== 'about:blank') onRecord(id, e.url, e.title && e.title !== e.url ? e.title : '');
        }}
        onOpenWindow={({ nativeEvent }) => onOpenWindow(id, nativeEvent.targetUrl)}
        onContentProcessDidTerminate={() => {
          setPainted(false);
          onTerminated(id);
        }}
        onShouldStartLoadWithRequest={(req) => {
          if (isWebUrl(req.url)) return true;
          // mailto:, tel:, app links and the like belong to the OS.
          Linking.openURL(req.url).catch(() => {});
          onHandOff(id, req.url);
          return false;
        }}
      />
      {preview && !painted ? (
        <Image source={preview} style={styles.preview} resizeMode="cover" pointerEvents="none" />
      ) : null}
      <TabBackdrop url={tab.url ?? tab.requestedUrl} title={tab.title} visible={!preview && !painted} />
    </View>
  );
});

// Stands in for a tab's page while it waits to be mounted: the same cover
// TabWebView shows until its page first paints, so the hand-over is seamless.
function PagePlaceholder({ tab, preview }: { tab: Tab; preview?: ImageSourcePropType }) {
  return (
    <View style={StyleSheet.absoluteFill} pointerEvents="none">
      {preview ? <Image source={preview} style={styles.preview} resizeMode="cover" /> : null}
      <TabBackdrop url={tab.url ?? tab.requestedUrl} title={tab.title} visible={!preview} />
    </View>
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
  pages: { flex: 1 },
  webview: { flex: 1, backgroundColor: colors.background },
  hidden: { display: 'none' },
  preview: { ...StyleSheet.absoluteFill, backgroundColor: colors.background },
  bottom: { backgroundColor: colors.surface },
});
