import AsyncStorage from '@react-native-async-storage/async-storage';
import * as Crypto from 'expo-crypto';
import { useCallback, useEffect, useLayoutEffect, useMemo, useReducer, useRef, useState } from 'react';
import { AppState } from 'react-native';

import { useAuth } from '../sync/auth';
import { getDeviceId, registerDevice } from '../sync/devices';
import { fetchAll, fetchSince, type Rows, sendChanges, subscribe } from '../sync/remote';
import {
  byPosition,
  isShareableUrl,
  type ItemChange,
  type ItemRow,
  keyAfter,
  keyAtEnd,
  keyAtStart,
  keyBefore,
  keyBetween,
  laterOf,
  type SpaceChange,
  type SpaceRow,
  tsMicros,
} from '../sync/rows';

import { accentRotation } from './theme';

// The phone's tabs are a cache of the account's shared Spaces and items
// (docs/SYNC.md). Every Mac and phone on the account shows the same rows; what
// stays on this phone is which tab is active, where each tab's page is right
// now, and the recently closed list.

export type TabKind = 'favorite' | 'pinned' | 'today';

// One tab as the UI sees it: a shared item plus what this phone keeps to
// itself.
export type Tab = {
  id: string;
  spaceId: string;
  kind: TabKind;
  // Favorites and pinned tabs stay put through Clear Today.
  pinned: boolean;
  // False for a draft: a start page, or a tab on a page that can't be shared.
  synced: boolean;
  // Where the tab currently is on this phone. null means the start page.
  url: string | null;
  // The shared row's URL: a today tab's last shared page, a pinned item's or
  // Favorite's saved destination. null for drafts.
  rowUrl: string | null;
  title: string;
  // What the WebView was last told to load. Kept apart from |url| so recording
  // in-page navigation doesn't feed back into the WebView as a reload.
  requestedUrl: string | null;
  // Bumped to force a load even when |requestedUrl| didn't change.
  requestId: number;
  // The tab that opened this one, so closing it can return you there.
  openerId?: string;
  // When this phone last showed it; 0 if it never has.
  lastUsedAt: number;
  position: string;
  parentId: string | null;
  previewPath: string | null;
  previewUpdatedAt: string | null;
};

export type ClosedTab = {
  tab: Tab;
  closedAt: number;
};

// This phone's side of a tab. A draft has no row yet and lives only here,
// at |spaceId| and |position|.
type LocalTab = {
  id: string;
  draft: boolean;
  spaceId: string;
  position: string;
  url: string | null;
  title: string;
  requestedUrl: string | null;
  requestId: number;
  openerId?: string;
  lastUsedAt: number;
};

export type TabsState = {
  hydrated: boolean;
  deviceId: string;
  // The account the rows belong to; null while they're this phone's alone.
  accountId: string | null;
  spaces: Record<string, SpaceRow>;
  items: Record<string, ItemRow>;
  local: Record<string, LocalTab>;
  activeSpaceId: string;
  activeBySpace: Record<string, string>;
  recentlyClosed: ClosedTab[];
  // Unsent changes: only the changed columns, merged per row. They win over
  // whatever the server says until they're sent.
  pendingSpaces: Record<string, SpaceChange>;
  pendingItems: Record<string, ItemChange>;
  // The largest updated_at seen; catch-up reads from a minute before it.
  cursor: string | null;
  // How many of this phone's tabs the first sign-in added to the account.
  merged: number | null;
};

type Action =
  | { type: 'hydrate'; deviceId: string; saved: Persisted | null }
  | { type: 'open'; url: string | null; afterId?: string; openerId?: string; activate?: boolean }
  | { type: 'activate'; id: string }
  | { type: 'navigate'; id: string; url: string }
  | { type: 'record'; id: string; url?: string; title?: string }
  | { type: 'close'; id: string }
  | { type: 'remove'; id: string }
  | { type: 'reopen'; closedAt?: number }
  | { type: 'togglePin'; id: string }
  | { type: 'duplicate'; id: string }
  | { type: 'move'; id: string; by: -1 | 1 }
  | { type: 'clearToday' }
  | { type: 'switchSpace'; id: string }
  | { type: 'createSpace'; name: string }
  | { type: 'renameSpace'; id: string; name: string }
  | { type: 'setPreview'; id: string; path: string | null }
  | { type: 'remote'; accountId: string; rows: Rows; full?: boolean }
  | { type: 'attach'; accountId: string; rows: Rows }
  | { type: 'detach' }
  | {
      type: 'sent';
      accountId: string;
      spaces: Record<string, SpaceChange>;
      items: Record<string, ItemChange>;
    }
  | { type: 'dismissMerged' };

const STORAGE_KEY = 'avora.tabs.v2';
const LEGACY_STORAGE_KEY = 'avora.tabs.v1';
const MAX_RECENTLY_CLOSED = 25;
const TOMBSTONE_TTL_MS = 30 * 24 * 60 * 60 * 1000;
const SEND_DELAY_MS = 1500;
const RETRY_DELAY_MS = 15_000;

const uuid = () => Crypto.randomUUID();
const nowIso = () => new Date().toISOString();
const isLive = <T extends { deleted_at: string | null }>(row: T | undefined): row is T =>
  !!row && !row.deleted_at;

function blankSpace(id: string): SpaceRow {
  return {
    id,
    name: '',
    icon: '',
    color: '',
    position: '',
    updated_at: null,
    updated_by: null,
    deleted_at: null,
  };
}

function blankItem(id: string): ItemRow {
  return {
    id,
    space_id: '',
    kind: 'today',
    parent_id: null,
    url: '',
    title: '',
    favicon_url: null,
    preview_path: null,
    preview_updated_at: null,
    position: '',
    last_active_at: null,
    updated_at: null,
    updated_by: null,
    deleted_at: null,
  };
}

// --- Writing rows ---------------------------------------------------------------

// Every local edit goes through these two, which also queue it for the
// server. Rows applied from the server never do, so they aren't re-uploaded.
function changeItem(state: TabsState, id: string, change: ItemChange, base?: ItemRow): TabsState {
  const item = { ...(base ?? state.items[id] ?? blankItem(id)), ...change };
  return {
    ...state,
    items: { ...state.items, [id]: item },
    pendingItems: state.accountId
      ? { ...state.pendingItems, [id]: { ...state.pendingItems[id], ...change } }
      : state.pendingItems,
  };
}

function changeSpace(state: TabsState, id: string, change: SpaceChange): TabsState {
  const space = { ...(state.spaces[id] ?? blankSpace(id)), ...change };
  return {
    ...state,
    spaces: { ...state.spaces, [id]: space },
    pendingSpaces: state.accountId
      ? { ...state.pendingSpaces, [id]: { ...state.pendingSpaces[id], ...change } }
      : state.pendingSpaces,
  };
}

// A new row needs every required column; see "Writing" in docs/SYNC.md.
function createItem(
  state: TabsState,
  row: Pick<ItemRow, 'id' | 'space_id' | 'kind' | 'url' | 'title' | 'position'> & Partial<ItemRow>,
): TabsState {
  const change: ItemChange = {
    space_id: row.space_id,
    kind: row.kind,
    parent_id: row.parent_id ?? null,
    url: row.url,
    title: row.title,
    position: row.position,
    last_active_at: row.last_active_at ?? nowIso(),
  };
  return changeItem(state, row.id, change, blankItem(row.id));
}

function setLocal(state: TabsState, id: string, patch: Partial<LocalTab>): TabsState {
  const current = state.local[id] ?? localFor(state, id);
  if (!current) return state;
  return { ...state, local: { ...state.local, [id]: { ...current, ...patch } } };
}

function dropLocal(state: TabsState, id: string): TabsState {
  if (!state.local[id]) return state;
  const local = { ...state.local };
  delete local[id];
  return { ...state, local };
}

// A row this phone hasn't shown yet gets its local side on first use.
function localFor(state: TabsState, id: string): LocalTab | null {
  const item = state.items[id];
  if (!isLive(item)) return null;
  return {
    id,
    draft: false,
    spaceId: item.space_id,
    position: item.position,
    url: item.url,
    title: item.title,
    requestedUrl: item.url,
    requestId: 0,
    lastUsedAt: 0,
  };
}

// --- Reading the cache --------------------------------------------------------

export function liveSpaces(state: TabsState): SpaceRow[] {
  return Object.values(state.spaces).filter(isLive).sort(byPosition);
}

function liveItemsIn(state: TabsState, spaceId: string): ItemRow[] {
  return Object.values(state.items).filter((i) => isLive(i) && i.space_id === spaceId);
}

function isDraft(state: TabsState, local: LocalTab) {
  return local.draft && !state.items[local.id];
}

// Today rows and this phone's drafts, as one list in position order.
function todaySlots(state: TabsState, spaceId: string): { id: string; position: string }[] {
  const rows = liveItemsIn(state, spaceId).filter((i) => i.kind === 'today');
  const drafts = Object.values(state.local).filter((l) => isDraft(state, l) && l.spaceId === spaceId);
  return [...rows, ...drafts].sort(byPosition);
}

// Top-level pinned items and folders, in the order the desktop sidebar shows.
function pinnedTop(state: TabsState, spaceId: string): ItemRow[] {
  const items = liveItemsIn(state, spaceId);
  const folders = new Set(items.filter((i) => i.kind === 'folder').map((i) => i.id));
  return items
    .filter(
      (i) => i.kind === 'folder' || (i.kind === 'pinned' && (!i.parent_id || !folders.has(i.parent_id))),
    )
    .sort(byPosition);
}

function toTab(state: TabsState, id: string, onScreen: boolean): Tab | null {
  const item = state.items[id];
  const local = state.local[id];
  if (isLive(item) && item.kind !== 'folder') {
    const kind = item.kind as TabKind;
    // A today tab reads where the account says it is, except on screen,
    // where the page shown wins (another device may have moved it meanwhile),
    // and on a page that couldn't be shared.
    const showLocal =
      !!local && (kind !== 'today' || onScreen || (local.url !== null && !isShareableUrl(local.url)));
    const url = showLocal ? local.url : item.url;
    const title = showLocal ? local.title || (url === item.url ? item.title : '') : item.title;
    return {
      id,
      spaceId: item.space_id,
      kind,
      pinned: kind !== 'today',
      synced: true,
      url,
      rowUrl: item.url,
      title,
      requestedUrl: local?.requestedUrl ?? item.url,
      requestId: local?.requestId ?? 0,
      openerId: local?.openerId,
      lastUsedAt: local?.lastUsedAt ?? 0,
      position: item.position,
      parentId: item.parent_id,
      previewPath: item.preview_path,
      previewUpdatedAt: item.preview_updated_at,
    };
  }
  if (local && isDraft(state, local)) {
    return {
      id,
      spaceId: local.spaceId,
      kind: 'today',
      pinned: false,
      synced: false,
      url: local.url,
      rowUrl: null,
      title: local.title,
      requestedUrl: local.requestedUrl,
      requestId: local.requestId,
      openerId: local.openerId,
      lastUsedAt: local.lastUsedAt,
      position: local.position,
      parentId: null,
      previewPath: null,
      previewUpdatedAt: null,
    };
  }
  return null;
}

// Favorites, then pinned items (a folder's contents in its place), then today.
export function tabsIn(state: TabsState, spaceId: string): Tab[] {
  const activeId = state.activeBySpace[spaceId];
  const items = liveItemsIn(state, spaceId);
  const favorites = items.filter((i) => i.kind === 'favorite').sort(byPosition);
  const pinned = pinnedTop(state, spaceId).flatMap((i) =>
    i.kind === 'folder'
      ? items.filter((c) => c.kind === 'pinned' && c.parent_id === i.id).sort(byPosition)
      : [i],
  );
  const ids = [...favorites, ...pinned, ...todaySlots(state, spaceId)].map((r) => r.id);
  return ids.map((id) => toTab(state, id, id === activeId)).filter((t): t is Tab => !!t);
}

function tabById(state: TabsState, id: string): Tab | null {
  const spaceId = state.items[id]?.space_id ?? state.local[id]?.spaceId;
  return toTab(state, id, !!spaceId && state.activeBySpace[spaceId] === id);
}

// --- Keeping the state whole --------------------------------------------------

function newDraft(state: TabsState, spaceId: string, url: string | null = null, openerId?: string) {
  const id = uuid();
  const local: LocalTab = {
    id,
    draft: true,
    spaceId,
    position: keyAtEnd(todaySlots(state, spaceId)),
    url,
    title: '',
    requestedUrl: url,
    requestId: 0,
    openerId,
    lastUsedAt: Date.now(),
  };
  return { id, state: { ...state, local: { ...state.local, [id]: local } } };
}

// Run after every change: there is always a live Space, an active one, and an
// active tab in it; tabs whose rows went away are forgotten.
function settle(state: TabsState): TabsState {
  let next = state;
  let spaces = liveSpaces(next);
  if (!spaces.length) {
    next = changeSpace(next, uuid(), {
      name: 'Personal',
      icon: '',
      color: accentRotation[0],
      position: keyAtEnd([]),
    });
    spaces = liveSpaces(next);
  }

  let local = next.local;
  for (const l of Object.values(next.local)) {
    const gone = isDraft(next, l) ? !isLive(next.spaces[l.spaceId]) : !isLive(next.items[l.id]);
    if (!gone) continue;
    if (local === next.local) local = { ...local };
    delete local[l.id];
  }
  if (local !== next.local) next = { ...next, local };

  if (!isLive(next.spaces[next.activeSpaceId])) next = { ...next, activeSpaceId: spaces[0].id };
  const spaceId = next.activeSpaceId;

  let tabs = tabsIn(next, spaceId);
  if (!tabs.length) {
    // A Space with nothing in it shows a start page.
    const draft = newDraft(next, spaceId);
    next = { ...draft.state, activeBySpace: { ...next.activeBySpace, [spaceId]: draft.id } };
    tabs = tabsIn(next, spaceId);
  }
  const activeId = next.activeBySpace[spaceId];
  if (!tabs.some((t) => t.id === activeId)) {
    // Prefer the tab you were most likely looking at before.
    const recent = [...tabs].sort((a, b) => b.lastUsedAt - a.lastUsedAt)[0];
    const fallback = recent.lastUsedAt ? recent : (tabs.filter((t) => !t.pinned).at(-1) ?? tabs[0]);
    next = { ...next, activeBySpace: { ...next.activeBySpace, [spaceId]: fallback.id } };
  }
  return next;
}

// WebViews report an empty title mid-load; keep the old one unless the page
// actually changed.
function recordOn(local: LocalTab, url?: string, title?: string): Pick<LocalTab, 'url' | 'title'> {
  const nextUrl = url ?? local.url;
  const nextTitle = title || (nextUrl === local.url ? local.title : '');
  return { url: nextUrl, title: nextTitle };
}

// Shares where a tab is now: a today tab's row follows it, and a draft that
// reaches a shareable page becomes a row. Pinned items and Favorites keep
// their saved destination.
function shareLocation(state: TabsState, id: string): TabsState {
  const local = state.local[id];
  if (!local || !isShareableUrl(local.url)) return state;
  const item = state.items[id];
  if (isDraft(state, local)) {
    const next = createItem(state, {
      id,
      space_id: local.spaceId,
      kind: 'today',
      url: local.url,
      title: local.title,
      position: local.position,
    });
    return setLocal(next, id, { draft: false });
  }
  if (!isLive(item) || item.kind !== 'today') return state;
  const change: ItemChange = {};
  if (item.url !== local.url) change.url = local.url;
  if (item.title !== local.title && (local.title || change.url)) change.title = local.title;
  return Object.keys(change).length ? changeItem(state, id, change) : state;
}

function remember(state: TabsState, tab: Tab): TabsState {
  if (!tab.url) return state;
  const recentlyClosed = [{ tab, closedAt: Date.now() }, ...state.recentlyClosed].slice(0, MAX_RECENTLY_CLOSED);
  return { ...state, recentlyClosed };
}

// Takes a tab off the account (or this phone, for a draft); only this phone
// can bring it back.
function closeTab(state: TabsState, tab: Tab): TabsState {
  let next = remember(state, tab);
  // The closing device deletes the preview object, so the row drops its path.
  const preview: ItemChange = tab.previewPath ? { preview_path: null, preview_updated_at: null } : {};
  if (tab.synced) next = changeItem(next, tab.id, { deleted_at: nowIso(), ...preview });
  next = dropLocal(next, tab.id);
  if (state.activeBySpace[tab.spaceId] === tab.id) {
    const opener = tab.openerId && tabsIn(next, tab.spaceId).find((t) => t.id === tab.openerId);
    const activeBySpace = { ...next.activeBySpace };
    if (opener) activeBySpace[tab.spaceId] = opener.id;
    else delete activeBySpace[tab.spaceId];
    next = { ...next, activeBySpace };
  }
  return next;
}

// --- Applying the server's rows -----------------------------------------------

// Replace the local copy wholesale, except columns with unsent changes, which
// win when they're sent. Skip anything older than the local copy, which
// includes this phone's own writes coming back.
function applyRow<T extends { id: string; updated_at: string | null; updated_by: string | null }>(
  map: Record<string, T>,
  row: T,
  pending: object | undefined,
  deviceId: string,
) {
  const local = map[row.id];
  if (local?.updated_at) {
    const incoming = tsMicros(row.updated_at);
    const known = tsMicros(local.updated_at);
    if (incoming < known || (incoming === known && row.updated_by === deviceId)) return;
  }
  map[row.id] = pending ? { ...row, ...pending } : row;
}

function pick<T>(from: Record<string, T>, ids: Iterable<string>): Record<string, T> {
  const out: Record<string, T> = {};
  for (const id of ids) if (from[id]) out[id] = from[id];
  return out;
}

function applyRemote(state: TabsState, rows: Rows, full: boolean): TabsState {
  // A full load also forgets rows the server no longer has (purged
  // tombstones), unless they're new here and not sent yet.
  const spaces = full
    ? pick(state.spaces, [...Object.keys(state.pendingSpaces), ...rows.spaces.map((r) => r.id)])
    : { ...state.spaces };
  const items = full
    ? pick(state.items, [...Object.keys(state.pendingItems), ...rows.items.map((r) => r.id)])
    : { ...state.items };
  let cursor = state.cursor;
  for (const row of rows.spaces) {
    applyRow(spaces, row, state.pendingSpaces[row.id], state.deviceId);
    cursor = laterOf(cursor, row.updated_at);
  }
  for (const row of rows.items) {
    applyRow(items, row, state.pendingItems[row.id], state.deviceId);
    cursor = laterOf(cursor, row.updated_at);
  }
  return { ...state, spaces, items, cursor };
}

// First sign-in on this phone: merge its tabs into the account, never replace
// them. See "First sign-in on a device that already has tabs".
function attach(state: TabsState, accountId: string, rows: Rows): TabsState {
  let next: TabsState = {
    ...state,
    accountId,
    spaces: Object.fromEntries(rows.spaces.map((r) => [r.id, r])),
    items: Object.fromEntries(rows.items.map((r) => [r.id, r])),
    pendingSpaces: {},
    pendingItems: {},
    cursor: [...rows.spaces, ...rows.items].reduce<string | null>((c, r) => laterOf(c, r.updated_at), null),
  };

  const accountSpaces = rows.spaces.filter(isLive).sort(byPosition);
  const accountItems = rows.items.filter(isLive);
  const localItems = Object.values(state.items).filter(isLive);
  const spaceMap: Record<string, string> = {};
  const claimed = new Set<string>();
  let lastSpace = accountSpaces.at(-1)?.position ?? null;

  for (const space of liveSpaces(state)) {
    const name = space.name.trim().toLowerCase();
    const match = accountSpaces.find((a) => !claimed.has(a.id) && a.name.trim().toLowerCase() === name);
    if (match) {
      claimed.add(match.id);
      spaceMap[space.id] = match.id;
    } else if (accountSpaces.length && !localItems.some((i) => i.space_id === space.id)) {
      // Nothing to bring along (a fresh install's empty "Personal"): don't
      // add an empty Space to every Mac.
      spaceMap[space.id] = accountSpaces[0].id;
    } else {
      const id = next.spaces[space.id] ? uuid() : space.id;
      lastSpace = keyBetween(lastSpace, null);
      next = changeSpace(next, id, { name: space.name, icon: space.icon, color: space.color, position: lastSpace });
      spaceMap[space.id] = id;
    }
  }

  // Folders first, so their contents can follow a folder's new id.
  const ordered = localItems
    .filter((i) => spaceMap[i.space_id])
    .sort((a, b) => Number(b.kind === 'folder') - Number(a.kind === 'folder') || byPosition(a, b));
  const idMap: Record<string, string> = {};
  const adopted = new Set<string>();
  const ends = new Map<string, string | null>();
  let uploaded = 0;

  for (const item of ordered) {
    const spaceId = spaceMap[item.space_id];
    const parentId = item.parent_id ? (idMap[item.parent_id] ?? item.parent_id) : null;
    const match = accountItems.find(
      (a) =>
        !adopted.has(a.id) &&
        a.space_id === spaceId &&
        a.kind === item.kind &&
        a.url === item.url &&
        (item.kind !== 'folder' || a.title === item.title),
    );
    if (match) {
      adopted.add(match.id);
      idMap[item.id] = match.id;
      continue;
    }
    const id = next.items[item.id] ? uuid() : item.id;
    idMap[item.id] = id;
    // Appended after the account's own, in this phone's order.
    const group = `${spaceId}|${item.kind}|${parentId ?? ''}`;
    if (!ends.has(group)) {
      const siblings = accountItems
        .filter((a) => a.space_id === spaceId && a.kind === item.kind && (a.parent_id ?? null) === parentId)
        .sort(byPosition);
      ends.set(group, siblings.at(-1)?.position ?? null);
    }
    const position = keyBetween(ends.get(group) ?? null, null);
    ends.set(group, position);
    next = createItem(next, { ...item, id, space_id: spaceId, parent_id: parentId, position });
    if (item.kind !== 'folder') uploaded++;
  }

  const remapId = (id: string) => idMap[id] ?? id;
  const remapSpace = (id: string) => spaceMap[id] ?? id;
  const local: Record<string, LocalTab> = {};
  for (const l of Object.values(state.local)) {
    const id = remapId(l.id);
    local[id] = { ...l, id, spaceId: remapSpace(l.spaceId), openerId: l.openerId && remapId(l.openerId) };
  }
  const activeBySpace: Record<string, string> = {};
  for (const [spaceId, id] of Object.entries(state.activeBySpace)) {
    activeBySpace[remapSpace(spaceId)] ??= remapId(id);
  }
  return {
    ...next,
    local,
    activeBySpace,
    activeSpaceId: remapSpace(state.activeSpaceId),
    recentlyClosed: state.recentlyClosed.map((c) => ({
      ...c,
      tab: { ...c.tab, spaceId: remapSpace(c.tab.spaceId) },
    })),
    merged: uploaded || null,
  };
}

// Signed out: the tabs stay on this phone but stop syncing.
function detach(state: TabsState): TabsState {
  const liveOnly = <T extends { deleted_at: string | null }>(rows: Record<string, T>) =>
    Object.fromEntries(Object.entries(rows).filter(([, r]) => isLive(r)));
  const items = liveOnly(state.items);
  for (const [id, item] of Object.entries(items)) {
    items[id] = { ...item, preview_path: null, preview_updated_at: null };
  }
  return {
    ...state,
    accountId: null,
    spaces: liveOnly(state.spaces),
    items,
    pendingSpaces: {},
    pendingItems: {},
    cursor: null,
    merged: null,
  };
}

function sent<T extends object>(pending: Record<string, T>, sentChanges: Record<string, T>) {
  const next = { ...pending };
  for (const [id, change] of Object.entries(sentChanges)) {
    const current = next[id] as Record<string, unknown> | undefined;
    if (!current) continue;
    const rest = { ...current };
    for (const [key, value] of Object.entries(change)) {
      // A column edited again while the request was in flight stays queued.
      if (JSON.stringify(rest[key]) === JSON.stringify(value)) delete rest[key];
    }
    if (Object.keys(rest).length) next[id] = rest as T;
    else delete next[id];
  }
  return next;
}

// --- Reducer --------------------------------------------------------------------

function emptyState(): TabsState {
  return {
    hydrated: false,
    deviceId: '',
    accountId: null,
    spaces: {},
    items: {},
    local: {},
    activeSpaceId: '',
    activeBySpace: {},
    recentlyClosed: [],
    pendingSpaces: {},
    pendingItems: {},
    cursor: null,
    merged: null,
  };
}

function reducer(state: TabsState, action: Action): TabsState {
  if (action.type === 'hydrate') {
    return settle({ ...emptyState(), ...pruned(action.saved), hydrated: true, deviceId: action.deviceId });
  }
  if (!state.hydrated) return state;
  return settle(step(state, action));
}

function step(state: TabsState, action: Exclude<Action, { type: 'hydrate' }>): TabsState {
  const spaceId = state.activeSpaceId;

  switch (action.type) {
    case 'open': {
      // Blank tabs don't pile up: asking for another one moves the existing
      // blank tab to the end and switches to it.
      if (action.url === null) {
        const blank = Object.values(state.local).find(
          (l) => isDraft(state, l) && l.spaceId === spaceId && l.url === null,
        );
        if (blank) {
          const others = todaySlots(state, spaceId).filter((s) => s.id !== blank.id);
          const next = setLocal(state, blank.id, { position: keyAtEnd(others), lastUsedAt: Date.now() });
          return { ...next, activeBySpace: { ...next.activeBySpace, [spaceId]: blank.id } };
        }
      }
      const slots = todaySlots(state, spaceId);
      const anchor = action.afterId ? slots.findIndex((s) => s.id === action.afterId) : -1;
      const draft = newDraft(state, spaceId, action.url, action.openerId);
      // Tabs opened from a page land beside it; fresh tabs go to the end.
      let next = setLocal(draft.state, draft.id, {
        position: anchor >= 0 ? keyAfter(slots, anchor) : keyAtEnd(slots),
        lastUsedAt: action.activate === false ? 0 : Date.now(),
      });
      next = shareLocation(next, draft.id);
      if (action.activate === false) return next;
      return { ...next, activeBySpace: { ...next.activeBySpace, [spaceId]: draft.id } };
    }

    case 'activate': {
      const tab = tabById(state, action.id);
      if (!tab) return state;
      let next = setLocal(state, tab.id, { lastUsedAt: Date.now() });
      const local = next.local[tab.id];
      // Another device moved this tab since this phone last showed it: go
      // there now that it's selected, never while it's on screen.
      if (tab.kind === 'today' && tab.rowUrl && local.url !== tab.rowUrl && isShareableUrl(local.url)) {
        next = setLocal(next, tab.id, {
          url: tab.rowUrl,
          title: next.items[tab.id]?.title ?? '',
          requestedUrl: tab.rowUrl,
          requestId: local.requestId + 1,
        });
      }
      if (tab.synced) next = changeItem(next, tab.id, { last_active_at: nowIso() });
      return {
        ...next,
        activeSpaceId: tab.spaceId,
        activeBySpace: { ...next.activeBySpace, [tab.spaceId]: tab.id },
      };
    }

    case 'navigate': {
      const tab = tabById(state, action.id);
      if (!tab) return state;
      const local = state.local[tab.id] ?? localFor(state, tab.id);
      const next = setLocal(state, tab.id, {
        url: action.url,
        title: action.url === tab.url ? tab.title : '',
        requestedUrl: action.url,
        requestId: (local?.requestId ?? 0) + 1,
      });
      return shareLocation(next, tab.id);
    }

    case 'record': {
      const local = state.local[action.id] ?? localFor(state, action.id);
      if (!local) return state;
      const next = setLocal(state, action.id, recordOn(local, action.url, action.title));
      return shareLocation(next, action.id);
    }

    case 'close': {
      const tab = tabById(state, action.id);
      if (!tab) return state;
      if (!tab.pinned) return closeTab(state, tab);
      // Closing a pinned tab or Favorite unloads it; the item stays.
      const next = dropLocal(state, tab.id);
      if (state.activeBySpace[tab.spaceId] !== tab.id) return next;
      const successor = tabsIn(next, tab.spaceId)
        .filter((t) => t.id !== tab.id && t.lastUsedAt)
        .sort((a, b) => b.lastUsedAt - a.lastUsedAt)[0];
      if (successor) return { ...next, activeBySpace: { ...next.activeBySpace, [tab.spaceId]: successor.id } };
      return step(next, { type: 'open', url: null });
    }

    case 'remove': {
      const tab = tabById(state, action.id);
      return tab ? closeTab(state, tab) : state;
    }

    case 'reopen': {
      const entry =
        action.closedAt === undefined
          ? state.recentlyClosed[0]
          : state.recentlyClosed.find((c) => c.closedAt === action.closedAt);
      if (!entry) return state;
      const tab = entry.tab;
      const target = isLive(state.spaces[tab.spaceId]) ? tab.spaceId : spaceId;
      let next: TabsState = { ...state, recentlyClosed: state.recentlyClosed.filter((c) => c !== entry) };

      // A start page left behind by closing the last tab has nothing worth keeping.
      const left = tabsIn(next, target);
      if (left.length === 1 && left[0].url === null && !left[0].synced) next = dropLocal(next, left[0].id);

      const rowUrl = tab.kind === 'today' ? (isShareableUrl(tab.url) ? tab.url : null) : tab.rowUrl;
      const synced = tab.synced && !!rowUrl;
      const id = synced ? tab.id : uuid();
      if (synced) {
        const parent = tab.parentId ? next.items[tab.parentId] : undefined;
        // The only write that brings a tombstone back. Every required column
        // goes too, in case the row was purged meanwhile.
        next = changeItem(
          next,
          id,
          {
            space_id: target,
            kind: tab.kind,
            parent_id: isLive(parent) && parent.kind === 'folder' ? parent.id : null,
            url: rowUrl,
            title: tab.title,
            position: tab.position,
            last_active_at: nowIso(),
            deleted_at: null,
          },
          next.items[id] ?? blankItem(id),
        );
      }
      const local: LocalTab = {
        id,
        draft: !synced,
        spaceId: target,
        position: tab.position,
        url: tab.url,
        title: tab.title,
        requestedUrl: tab.url,
        requestId: tab.requestId + 1,
        lastUsedAt: Date.now(),
      };
      return {
        ...next,
        local: { ...next.local, [id]: local },
        activeSpaceId: target,
        activeBySpace: { ...next.activeBySpace, [target]: id },
      };
    }

    case 'togglePin': {
      const tab = tabById(state, action.id);
      if (!tab?.synced || tab.kind === 'favorite') return state;
      if (tab.kind === 'today') {
        // Joins the end of the pinned group; its page becomes the saved one.
        return changeItem(state, tab.id, {
          kind: 'pinned',
          parent_id: null,
          position: keyAtEnd(pinnedTop(state, tab.spaceId)),
        });
      }
      // Heads the today group, wherever the tab has got to.
      return changeItem(state, tab.id, {
        kind: 'today',
        parent_id: null,
        position: keyAtStart(todaySlots(state, tab.spaceId)),
        url: isShareableUrl(tab.url) ? tab.url : tab.rowUrl!,
        title: tab.title,
      });
    }

    case 'duplicate': {
      const source = tabById(state, action.id);
      if (!source) return state;
      return step(state, { type: 'open', url: source.url, afterId: source.id, openerId: source.id });
    }

    case 'move': {
      const tab = tabById(state, action.id);
      if (!tab) return state;
      const items = liveItemsIn(state, tab.spaceId);
      const group: { id: string; position: string }[] =
        tab.kind === 'today'
          ? todaySlots(state, tab.spaceId)
          : tab.kind === 'favorite'
            ? items.filter((i) => i.kind === 'favorite').sort(byPosition)
            : tab.parentId && isLive(state.items[tab.parentId])
              ? items.filter((i) => i.kind === 'pinned' && i.parent_id === tab.parentId).sort(byPosition)
              : pinnedTop(state, tab.spaceId);
      const index = group.findIndex((g) => g.id === tab.id);
      const target = index + action.by;
      if (index < 0 || target < 0 || target >= group.length) return state;
      const position = action.by < 0 ? keyBefore(group, target) : keyAfter(group, target);
      return tab.synced ? changeItem(state, tab.id, { position }) : setLocal(state, tab.id, { position });
    }

    case 'clearToday': {
      let next = state;
      for (const tab of tabsIn(state, spaceId).filter((t) => !t.pinned)) next = closeTab(next, tab);
      return next;
    }

    case 'switchSpace':
      return isLive(state.spaces[action.id]) ? { ...state, activeSpaceId: action.id } : state;

    case 'createSpace': {
      const spaces = liveSpaces(state);
      const id = uuid();
      const next = changeSpace(state, id, {
        name: action.name,
        icon: '',
        color: accentRotation[spaces.length % accentRotation.length],
        position: keyAtEnd(spaces),
      });
      return { ...next, activeSpaceId: id };
    }

    case 'renameSpace':
      return isLive(state.spaces[action.id]) ? changeSpace(state, action.id, { name: action.name }) : state;

    case 'setPreview':
      if (!isLive(state.items[action.id])) return state;
      return changeItem(state, action.id, {
        preview_path: action.path,
        preview_updated_at: action.path ? nowIso() : null,
      });

    case 'remote':
      if (state.accountId !== action.accountId) return state;
      return applyRemote(state, action.rows, !!action.full);

    case 'attach':
      return attach(state.accountId ? detach(state) : state, action.accountId, action.rows);

    case 'detach':
      return state.accountId ? detach(state) : state;

    case 'sent':
      if (state.accountId !== action.accountId) return state;
      return {
        ...state,
        pendingSpaces: sent(state.pendingSpaces, action.spaces),
        pendingItems: sent(state.pendingItems, action.items),
      };

    case 'dismissMerged':
      return { ...state, merged: null };
  }
}

// --- Storage ----------------------------------------------------------------------

type Persisted = Pick<
  TabsState,
  | 'accountId'
  | 'spaces'
  | 'items'
  | 'local'
  | 'activeSpaceId'
  | 'activeBySpace'
  | 'recentlyClosed'
  | 'pendingSpaces'
  | 'pendingItems'
  | 'cursor'
>;

function toPersisted(state: TabsState): Persisted {
  return {
    accountId: state.accountId,
    spaces: state.spaces,
    items: state.items,
    local: state.local,
    activeSpaceId: state.activeSpaceId,
    activeBySpace: state.activeBySpace,
    recentlyClosed: state.recentlyClosed,
    pendingSpaces: state.pendingSpaces,
    pendingItems: state.pendingItems,
    cursor: state.cursor,
  };
}

// Tombstones are kept a while so a late, older copy of a row can't bring it
// back, then dropped like the server's.
function pruned(saved: Persisted | null): Partial<TabsState> {
  if (!saved) return {};
  const cutoff = Date.now() - TOMBSTONE_TTL_MS;
  const keep = <T extends { deleted_at: string | null }>(rows: Record<string, T>, pending: Record<string, unknown>) =>
    Object.fromEntries(
      Object.entries(rows).filter(
        ([id, r]) => !r.deleted_at || pending[id] || tsMicros(r.deleted_at) / 1000 > cutoff,
      ),
    );
  return {
    ...saved,
    spaces: keep(saved.spaces, saved.pendingSpaces),
    items: keep(saved.items, saved.pendingItems),
  };
}

type LegacyTab = {
  id: string;
  url: string | null;
  title: string;
  pinned: boolean;
  openerId?: string;
  lastActiveAt: number;
};
type Legacy = { tabs: LegacyTab[]; activeId: string; recentlyClosed?: { tab: LegacyTab; closedAt: number }[] };

// Tabs saved before tabs were shared move into a "Personal" Space, which
// merges into the account on sign-in.
function fromLegacy(saved: Legacy): Persisted {
  const spaceId = uuid();
  const ids = new Map(saved.tabs.map((t) => [t.id, uuid()]));
  const items: Record<string, ItemRow> = {};
  const local: Record<string, LocalTab> = {};
  let lastPinned: string | null = null;
  let lastToday: string | null = null;
  for (const t of saved.tabs) {
    const id = ids.get(t.id)!;
    const position = keyBetween(t.pinned ? lastPinned : lastToday, null);
    if (t.pinned) lastPinned = position;
    else lastToday = position;
    const shareable = isShareableUrl(t.url);
    if (shareable) {
      items[id] = {
        ...blankItem(id),
        space_id: spaceId,
        kind: t.pinned ? 'pinned' : 'today',
        url: t.url!,
        title: t.title,
        position,
        last_active_at: new Date(t.lastActiveAt).toISOString(),
      };
    }
    // A pinned tab on a page that can't be shared has nowhere to go.
    if (shareable || !t.pinned) {
      local[id] = {
        id,
        draft: !shareable,
        spaceId,
        position,
        url: t.url,
        title: t.title,
        requestedUrl: t.url,
        requestId: 0,
        openerId: t.openerId && ids.get(t.openerId),
        lastUsedAt: t.lastActiveAt,
      };
    }
  }
  const closedPosition = keyBetween(lastToday, null);
  const recentlyClosed: ClosedTab[] = (saved.recentlyClosed ?? []).map((c) => ({
    closedAt: c.closedAt,
    tab: {
      id: uuid(),
      spaceId,
      kind: c.tab.pinned ? 'pinned' : 'today',
      pinned: c.tab.pinned,
      synced: isShareableUrl(c.tab.url),
      url: c.tab.url,
      rowUrl: isShareableUrl(c.tab.url) ? c.tab.url : null,
      title: c.tab.title,
      requestedUrl: c.tab.url,
      requestId: 0,
      lastUsedAt: 0,
      position: closedPosition,
      parentId: null,
      previewPath: null,
      previewUpdatedAt: null,
    },
  }));
  const activeId = ids.get(saved.activeId);
  return {
    accountId: null,
    spaces: {
      [spaceId]: {
        ...blankSpace(spaceId),
        name: 'Personal',
        color: accentRotation[0],
        position: keyBetween(null, null),
      },
    },
    items,
    local,
    activeSpaceId: spaceId,
    activeBySpace: activeId ? { [spaceId]: activeId } : {},
    recentlyClosed,
    pendingSpaces: {},
    pendingItems: {},
    cursor: null,
  };
}

async function loadSaved(): Promise<Persisted | null> {
  const raw = await AsyncStorage.getItem(STORAGE_KEY);
  if (raw) return JSON.parse(raw) as Persisted;
  const legacy = await AsyncStorage.getItem(LEGACY_STORAGE_KEY);
  return legacy ? fromLegacy(JSON.parse(legacy) as Legacy) : null;
}

// --- Hook ---------------------------------------------------------------------------

export function useTabs() {
  const [state, dispatch] = useReducer(reducer, undefined, emptyState);
  const { session, ready } = useAuth();
  const userId = session?.user.id ?? null;
  const latest = useRef(state);
  useLayoutEffect(() => {
    latest.current = state;
  });
  // Bumped to retry a failed sign-in merge or send.
  const [retry, setRetry] = useState(0);
  const retryLater = useCallback(() => {
    setTimeout(() => setRetry((n) => n + 1), RETRY_DELAY_MS);
  }, []);

  useEffect(() => {
    Promise.all([getDeviceId(), loadSaved().catch(() => null)]).then(([deviceId, saved]) =>
      dispatch({ type: 'hydrate', deviceId, saved }),
    );
  }, []);

  // AsyncStorage is the offline cache and the queue of unsent changes.
  useEffect(() => {
    if (!state.hydrated) return;
    const timer = setTimeout(() => {
      AsyncStorage.setItem(STORAGE_KEY, JSON.stringify(toPersisted(state))).catch(() => {});
    }, 400);
    return () => clearTimeout(timer);
  }, [state]);

  // Follow the session: merge on sign-in, keep the tabs locally on sign-out.
  const { hydrated, accountId } = state;
  useEffect(() => {
    if (!hydrated || !ready) return;
    if (!userId) {
      if (accountId) dispatch({ type: 'detach' });
      return;
    }
    registerDevice().catch(() => {});
    if (accountId === userId) return;
    let cancelled = false;
    fetchAll().then(
      (rows) => !cancelled && dispatch({ type: 'attach', accountId: userId, rows }),
      () => !cancelled && retryLater(),
    );
    return () => {
      cancelled = true;
    };
  }, [hydrated, ready, userId, accountId, retry, retryLater]);

  const attached = hydrated && !!userId && accountId === userId;

  // Catch up on launch, on reconnect and on returning to the foreground;
  // Realtime keeps up in between.
  useEffect(() => {
    if (!attached || !userId) return;
    let live = true;
    const catchUp = () => {
      const { cursor } = latest.current;
      const full = !cursor || Date.now() - tsMicros(cursor) / 1000 > TOMBSTONE_TTL_MS;
      (full ? fetchAll() : fetchSince(cursor)).then(
        (rows) => live && dispatch({ type: 'remote', accountId: userId, rows, full }),
        () => {},
      );
    };
    catchUp();
    const unsubscribe = subscribe(userId, (rows) => dispatch({ type: 'remote', accountId: userId, rows }), catchUp);
    const appState = AppState.addEventListener('change', (s) => {
      if (s !== 'active') return;
      catchUp();
      registerDevice().catch(() => {});
    });
    return () => {
      live = false;
      unsubscribe();
      appState.remove();
    };
  }, [attached, userId]);

  // Writes go out a moment after a burst of changes, as one request.
  const sending = useRef(false);
  const flush = useCallback(() => {
    const { pendingSpaces, pendingItems, deviceId, accountId: account } = latest.current;
    if (sending.current || !account) return;
    if (!Object.keys(pendingSpaces).length && !Object.keys(pendingItems).length) return;
    sending.current = true;
    sendChanges(deviceId, pendingSpaces, pendingItems)
      .then(
        () => dispatch({ type: 'sent', accountId: account, spaces: pendingSpaces, items: pendingItems }),
        retryLater,
      )
      .finally(() => {
        sending.current = false;
      });
  }, [retryLater]);

  const { pendingSpaces, pendingItems } = state;
  useEffect(() => {
    if (!attached) return;
    const timer = setTimeout(flush, SEND_DELAY_MS);
    return () => clearTimeout(timer);
  }, [attached, pendingSpaces, pendingItems, retry, flush]);

  useEffect(() => {
    if (!attached) return;
    const appState = AppState.addEventListener('change', (s) => {
      if (s === 'background') flush();
    });
    return () => appState.remove();
  }, [attached, flush]);

  const spaces = useMemo(() => liveSpaces(state), [state]);
  const tabs = useMemo(() => tabsIn(state, state.activeSpaceId), [state]);
  const activeId = state.activeBySpace[state.activeSpaceId];
  const activeTab = tabs.find((t) => t.id === activeId) ?? tabs[0];
  const activeSpace = state.spaces[state.activeSpaceId];

  return { state, spaces, activeSpace, tabs, activeTab, attached, dispatch };
}

export type TabsDispatch = ReturnType<typeof useTabs>['dispatch'];
