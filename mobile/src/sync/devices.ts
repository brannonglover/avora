import { useEffect, useState } from 'react';
import { AppState } from 'react-native';

import { useAuth } from './auth';
import { supabase } from './supabase';

// The shape of public.devices; docs/SYNC.md is the contract the desktop writes to.
export type SyncedTab = {
  url: string;
  title: string;
  pinned: boolean;
  space?: string;
  lastActiveAt?: number;
};

export type Device = {
  id: string;
  name: string;
  platform: string;
  tabs: SyncedTab[];
  updatedAt: string;
};

type Row = { id: string; name: string; platform: string; tabs: unknown; updated_at: string };

type Loaded = { userId: string; devices: Device[] };

// supabase-js hands back the existing channel for a repeated topic, so each
// subscriber needs its own or one unmount would tear down the other's.
let channelSeq = 0;

// Other devices' tabs for the signed-in account, kept current over Realtime
// and refreshed whenever the app comes back to the foreground.
export function useSyncedDevices(): Device[] {
  const { session } = useAuth();
  const userId = session?.user.id ?? null;
  const [loaded, setLoaded] = useState<Loaded | null>(null);

  useEffect(() => {
    if (!supabase || !userId) return;
    const client = supabase;
    let live = true;
    const load = () =>
      fetchDevices().then((devices) => {
        // Keep showing the last good list through a flaky connection.
        if (live && devices) setLoaded({ userId, devices });
      });
    load();

    const channel = client
      .channel(`devices:${userId}:${++channelSeq}`)
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'devices', filter: `user_id=eq.${userId}` },
        load,
      )
      .subscribe();
    const appState = AppState.addEventListener('change', (state) => {
      if (state === 'active') load();
    });

    return () => {
      live = false;
      appState.remove();
      client.removeChannel(channel);
    };
  }, [userId]);

  // Dropped the moment you sign out or switch accounts.
  return loaded && loaded.userId === userId ? loaded.devices : [];
}

async function fetchDevices(): Promise<Device[] | null> {
  if (!supabase) return null;
  const { data, error } = await supabase
    .from('devices')
    .select('id, name, platform, tabs, updated_at')
    .order('updated_at', { ascending: false });
  return error ? null : (data as Row[]).map(toDevice);
}

export function updatedAgo(iso: string, now = Date.now()): string {
  const minutes = Math.round((now - Date.parse(iso)) / 60_000);
  if (!(minutes >= 1)) return 'just now';
  if (minutes < 60) return `${minutes} min ago`;
  const hours = Math.round(minutes / 60);
  if (hours < 24) return `${hours} hr ago`;
  const days = Math.round(hours / 24);
  if (days === 1) return 'yesterday';
  if (days < 7) return `${days} days ago`;
  return new Date(iso).toLocaleDateString();
}

function toDevice(row: Row): Device {
  return {
    id: row.id,
    name: row.name,
    platform: row.platform,
    tabs: Array.isArray(row.tabs) ? row.tabs.flatMap(toTab) : [],
    updatedAt: row.updated_at,
  };
}

// Rows are written by other clients, so nothing is taken on trust: only
// http(s) pages make it into the browser.
function toTab(value: unknown): SyncedTab[] {
  if (!value || typeof value !== 'object') return [];
  const t = value as Record<string, unknown>;
  if (typeof t.url !== 'string' || !/^https?:\/\//i.test(t.url)) return [];
  return [
    {
      url: t.url,
      title: typeof t.title === 'string' ? t.title : '',
      pinned: t.pinned === true,
      space: typeof t.space === 'string' ? t.space : undefined,
      lastActiveAt: typeof t.lastActiveAt === 'number' ? t.lastActiveAt : undefined,
    },
  ];
}
