import AsyncStorage from '@react-native-async-storage/async-storage';
import { useEffect, useReducer, useRef } from 'react';

// Mirrors the desktop sidebar's split: pinned tabs stay put across sessions and
// "Clear Today", today tabs are the day-to-day working set.
export type Tab = {
  id: string;
  // Where the tab currently is. null means the start page.
  url: string | null;
  // What the WebView was last told to load. Kept apart from |url| so recording
  // in-page navigation doesn't feed back into the WebView as a reload.
  requestedUrl: string | null;
  // Bumped to force a load even when |requestedUrl| didn't change.
  requestId: number;
  title: string;
  pinned: boolean;
  // The tab that opened this one, so closing it can return you there.
  openerId?: string;
  lastActiveAt: number;
};

export type ClosedTab = {
  tab: Tab;
  index: number;
  closedAt: number;
};

export type TabsState = {
  tabs: Tab[];
  activeId: string;
  recentlyClosed: ClosedTab[];
  hydrated: boolean;
};

type Action =
  | { type: 'hydrate'; state: Pick<TabsState, 'tabs' | 'activeId' | 'recentlyClosed'> }
  | { type: 'open'; url: string | null; afterId?: string; openerId?: string; activate?: boolean }
  | { type: 'activate'; id: string }
  | { type: 'navigate'; id: string; url: string }
  | { type: 'record'; id: string; url?: string; title?: string }
  | { type: 'close'; id: string }
  | { type: 'reopen'; closedAt?: number }
  | { type: 'togglePin'; id: string }
  | { type: 'duplicate'; id: string }
  | { type: 'clearToday' };

const STORAGE_KEY = 'avora.tabs.v1';
const MAX_RECENTLY_CLOSED = 25;

let counter = 0;
const newId = () => `t${Date.now().toString(36)}${(counter++).toString(36)}`;

function makeTab(url: string | null, extra?: Partial<Tab>): Tab {
  return {
    id: newId(),
    url,
    requestedUrl: url,
    requestId: 0,
    title: '',
    pinned: false,
    lastActiveAt: Date.now(),
    ...extra,
  };
}

// Pinned tabs always lead the list; within each group order is insertion order.
function normalize(tabs: Tab[]): Tab[] {
  return [...tabs.filter((t) => t.pinned), ...tabs.filter((t) => !t.pinned)];
}

function touch(tabs: Tab[], id: string): Tab[] {
  return tabs.map((t) => (t.id === id ? { ...t, lastActiveAt: Date.now() } : t));
}

// When the active tab goes away, prefer the tab that opened it, then the most
// recently used tab -- the one you were most likely looking at before.
function successorOf(tabs: Tab[], closing: Tab): string | undefined {
  const rest = tabs.filter((t) => t.id !== closing.id);
  if (closing.openerId && rest.some((t) => t.id === closing.openerId)) {
    return closing.openerId;
  }
  return [...rest].sort((a, b) => b.lastActiveAt - a.lastActiveAt)[0]?.id;
}

// WebViews report an empty title mid-load; keep the old one unless the page
// actually changed.
function recordOn(tab: Tab, url?: string, title?: string): Tab {
  const nextUrl = url ?? tab.url;
  const nextTitle = title || (nextUrl === tab.url ? tab.title : '');
  return { ...tab, url: nextUrl, title: nextTitle };
}

function initialState(): TabsState {
  const tab = makeTab(null);
  return { tabs: [tab], activeId: tab.id, recentlyClosed: [], hydrated: false };
}

function reducer(state: TabsState, action: Action): TabsState {
  switch (action.type) {
    case 'hydrate': {
      if (!action.state.tabs.length) return { ...state, hydrated: true };
      return { ...action.state, hydrated: true };
    }

    case 'open': {
      // Blank tabs don't pile up: asking for another one moves the existing
      // blank tab to the end and switches to it.
      const blank = action.url === null && state.tabs.find((t) => t.url === null && !t.pinned);
      if (blank) {
        return {
          ...state,
          tabs: [...state.tabs.filter((t) => t !== blank), { ...blank, lastActiveAt: Date.now() }],
          activeId: blank.id,
        };
      }
      const tab = makeTab(action.url, { openerId: action.openerId });
      const tabs = [...state.tabs];
      const anchor = action.afterId ? tabs.findIndex((t) => t.id === action.afterId) : -1;
      // Tabs opened from a page land beside it; fresh tabs go to the end.
      if (anchor >= 0 && !tabs[anchor].pinned) tabs.splice(anchor + 1, 0, tab);
      else tabs.push(tab);
      return {
        ...state,
        tabs: normalize(tabs),
        activeId: action.activate === false ? state.activeId : tab.id,
      };
    }

    case 'activate': {
      if (!state.tabs.some((t) => t.id === action.id)) return state;
      return { ...state, activeId: action.id, tabs: touch(state.tabs, action.id) };
    }

    case 'navigate':
      return {
        ...state,
        tabs: state.tabs.map((t) =>
          t.id === action.id
            ? { ...t, url: action.url, requestedUrl: action.url, requestId: t.requestId + 1 }
            : t,
        ),
      };

    case 'record':
      return {
        ...state,
        tabs: state.tabs.map((t) =>
          t.id === action.id ? recordOn(t, action.url, action.title) : t,
        ),
      };

    case 'close': {
      const index = state.tabs.findIndex((t) => t.id === action.id);
      if (index < 0) return state;
      const closing = state.tabs[index];
      let tabs = state.tabs.filter((t) => t.id !== action.id);
      let activeId = state.activeId;
      if (activeId === closing.id) activeId = successorOf(state.tabs, closing) ?? '';
      // There is always a tab; closing the last one leaves a fresh start page.
      if (!tabs.length) {
        const fresh = makeTab(null);
        tabs = [fresh];
        activeId = fresh.id;
      }
      const recentlyClosed =
        closing.url === null
          ? state.recentlyClosed
          : [{ tab: closing, index, closedAt: Date.now() }, ...state.recentlyClosed].slice(
              0,
              MAX_RECENTLY_CLOSED,
            );
      return { ...state, tabs, activeId, recentlyClosed };
    }

    case 'reopen': {
      const entry =
        action.closedAt === undefined
          ? state.recentlyClosed[0]
          : state.recentlyClosed.find((c) => c.closedAt === action.closedAt);
      if (!entry) return state;
      const tab: Tab = {
        ...entry.tab,
        requestedUrl: entry.tab.url,
        requestId: entry.tab.requestId + 1,
        lastActiveAt: Date.now(),
      };
      let tabs = [...state.tabs];
      // A start page left behind by closing the last tab has nothing worth keeping.
      if (tabs.length === 1 && tabs[0].url === null) tabs = [];
      tabs.splice(Math.min(entry.index, tabs.length), 0, tab);
      return {
        ...state,
        tabs: normalize(tabs),
        activeId: tab.id,
        recentlyClosed: state.recentlyClosed.filter((c) => c !== entry),
      };
    }

    case 'togglePin': {
      const target = state.tabs.find((t) => t.id === action.id);
      if (!target) return state;
      const toggled = { ...target, pinned: !target.pinned };
      const rest = state.tabs.filter((t) => t.id !== action.id);
      // Sitting on the pinned/today boundary means a newly pinned tab joins the
      // end of the pinned group and an unpinned one heads the today group, so
      // the tab barely moves either way.
      const pinned = rest.filter((t) => t.pinned);
      const today = rest.filter((t) => !t.pinned);
      return { ...state, tabs: [...pinned, toggled, ...today] };
    }

    case 'duplicate': {
      const source = state.tabs.find((t) => t.id === action.id);
      if (!source) return state;
      return reducer(state, { type: 'open', url: source.url, afterId: source.id, openerId: source.id });
    }

    case 'clearToday': {
      const today = state.tabs.filter((t) => !t.pinned);
      let next = state;
      for (const tab of today) next = reducer(next, { type: 'close', id: tab.id });
      return next;
    }
  }
}

type Persisted = Pick<TabsState, 'tabs' | 'activeId' | 'recentlyClosed'>;

export function useTabs() {
  const [state, dispatch] = useReducer(reducer, undefined, initialState);
  const saveTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    AsyncStorage.getItem(STORAGE_KEY)
      .then((raw) => {
        if (!raw) return dispatch({ type: 'hydrate', state: { tabs: [], activeId: '', recentlyClosed: [] } });
        const saved = JSON.parse(raw) as Persisted;
        const tabs = normalize(
          saved.tabs.map((t) => ({ ...t, requestedUrl: t.url, requestId: 0 })),
        );
        const activeId = tabs.some((t) => t.id === saved.activeId) ? saved.activeId : tabs[0]?.id;
        dispatch({
          type: 'hydrate',
          state: { tabs, activeId: activeId ?? '', recentlyClosed: saved.recentlyClosed ?? [] },
        });
      })
      .catch(() => dispatch({ type: 'hydrate', state: { tabs: [], activeId: '', recentlyClosed: [] } }));
  }, []);

  useEffect(() => {
    if (!state.hydrated) return;
    if (saveTimer.current) clearTimeout(saveTimer.current);
    saveTimer.current = setTimeout(() => {
      const persisted: Persisted = {
        tabs: state.tabs,
        activeId: state.activeId,
        recentlyClosed: state.recentlyClosed,
      };
      AsyncStorage.setItem(STORAGE_KEY, JSON.stringify(persisted)).catch(() => {});
    }, 400);
  }, [state]);

  const activeTab = state.tabs.find((t) => t.id === state.activeId) ?? state.tabs[0];

  return { state, activeTab, dispatch };
}

export type TabsDispatch = ReturnType<typeof useTabs>['dispatch'];
