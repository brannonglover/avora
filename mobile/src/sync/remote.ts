import type { RealtimeChannel } from '@supabase/supabase-js';

import {
  type ItemChange,
  type ItemRow,
  parseItem,
  parseSpace,
  type SpaceChange,
  type SpaceRow,
  tsMicros,
} from './rows';
import { supabase } from './supabase';

// The network half of docs/SYNC.md: full load, catch-up, Realtime and writes.

export type Rows = { spaces: SpaceRow[]; items: ItemRow[] };

const PAGE = 1000;
// Catch-up deliberately re-reads the last minute: a transaction that committed
// late can carry an updated_at a little older than one already seen.
const CATCH_UP_OVERLAP_MS = 60_000;

function client() {
  if (!supabase) throw new Error('Sync isn’t set up in this build.');
  return supabase;
}

// PostgREST caps a response at 1000 rows, so read in pages.
async function readAll(table: 'spaces' | 'items', since: string | null): Promise<unknown[]> {
  const rows: unknown[] = [];
  for (let from = 0; ; from += PAGE) {
    let query = client().from(table).select('*');
    if (since) query = query.gte('updated_at', since);
    const { data, error } = await query
      .order('updated_at', { ascending: true })
      .order('id', { ascending: true })
      .range(from, from + PAGE - 1);
    if (error) throw error;
    rows.push(...data);
    if (data.length < PAGE) return rows;
  }
}

function parse(spaces: unknown[], items: unknown[]): Rows {
  return {
    spaces: spaces.map(parseSpace).filter((r): r is SpaceRow => !!r),
    items: items.map(parseItem).filter((r): r is ItemRow => !!r),
  };
}

// Every row the account has, tombstones included.
export async function fetchAll(): Promise<Rows> {
  const [spaces, items] = await Promise.all([readAll('spaces', null), readAll('items', null)]);
  return parse(spaces, items);
}

export async function fetchSince(cursor: string): Promise<Rows> {
  const since = new Date(tsMicros(cursor) / 1000 - CATCH_UP_OVERLAP_MS).toISOString();
  const [spaces, items] = await Promise.all([readAll('spaces', since), readAll('items', since)]);
  return parse(spaces, items);
}

export async function sendChanges(
  deviceId: string,
  spaces: Record<string, SpaceChange>,
  items: Record<string, ItemChange>,
): Promise<void> {
  const toList = (changes: Record<string, object>) =>
    Object.entries(changes).map(([id, change]) => ({ id, ...change, updated_by: deviceId }));
  // Spaces first, so a Mac never sees an item for a Space it hasn't heard of.
  if (Object.keys(spaces).length) {
    const { error } = await client().rpc('sync_spaces', { changes: toList(spaces) });
    if (error) throw error;
  }
  if (Object.keys(items).length) {
    const { error } = await client().rpc('sync_items', { changes: toList(items) });
    if (error) throw error;
  }
}

let channelSeq = 0;

// Live changes to the account's rows. |onSubscribed| fires on the first
// connection and on every reconnect, which is when a catch-up is due.
export function subscribe(
  userId: string,
  onRows: (rows: Rows) => void,
  onSubscribed: () => void,
): () => void {
  const c = client();
  const channel: RealtimeChannel = c
    .channel(`shared-tabs:${userId}:${++channelSeq}`)
    .on(
      'postgres_changes',
      { event: '*', schema: 'public', table: 'spaces', filter: `user_id=eq.${userId}` },
      (payload) => onRows(parse([payload.new], [])),
    )
    .on(
      'postgres_changes',
      { event: '*', schema: 'public', table: 'items', filter: `user_id=eq.${userId}` },
      (payload) => onRows(parse([], [payload.new])),
    )
    .subscribe((status) => {
      if (status === 'SUBSCRIBED') onSubscribed();
    });
  return () => {
    c.removeChannel(channel);
  };
}

// --- Previews -----------------------------------------------------------------

const BUCKET = 'tab-previews';

export function previewPath(userId: string, itemId: string) {
  return `${userId}/${itemId}.jpg`;
}

export async function uploadPreview(path: string, jpeg: ArrayBuffer): Promise<void> {
  const { error } = await client()
    .storage.from(BUCKET)
    .upload(path, jpeg, { contentType: 'image/jpeg', upsert: true });
  if (error) throw error;
}

export async function deletePreview(path: string): Promise<void> {
  await client().storage.from(BUCKET).remove([path]);
}

// An <Image> source for a stored preview. The URL carries preview_updated_at,
// so the image cache refetches exactly when the preview changes.
export function previewSource(path: string, updatedAt: string | null, accessToken: string) {
  const base = process.env.EXPO_PUBLIC_SUPABASE_URL;
  return {
    uri: `${base}/storage/v1/object/authenticated/${BUCKET}/${path}?v=${encodeURIComponent(updatedAt ?? '')}`,
    headers: {
      Authorization: `Bearer ${accessToken}`,
      apikey: process.env.EXPO_PUBLIC_SUPABASE_PUBLISHABLE_KEY ?? '',
    },
  };
}
