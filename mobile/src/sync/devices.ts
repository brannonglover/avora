import AsyncStorage from '@react-native-async-storage/async-storage';
import * as Crypto from 'expo-crypto';
import { useEffect, useState } from 'react';
import { AppState, Platform } from 'react-native';

import { useAuth } from './auth';
import { supabase } from './supabase';

// public.devices only lists the signed-in devices now; tabs live in
// public.items. See docs/SYNC.md.
export type Device = {
  id: string;
  name: string;
  platform: string;
  lastSeenAt: string;
};

type Row = { id: string; name: string; platform: string; last_seen_at: string };

const DEVICE_ID_KEY = 'avora.deviceId';

let deviceId: Promise<string> | null = null;

// Generated once per install and kept for good; written to |updated_by| on
// every row this phone changes.
export function getDeviceId(): Promise<string> {
  deviceId ??= AsyncStorage.getItem(DEVICE_ID_KEY)
    .then(async (stored) => {
      if (stored) return stored;
      const id = Crypto.randomUUID();
      await AsyncStorage.setItem(DEVICE_ID_KEY, id);
      return id;
    })
    .catch(() => Crypto.randomUUID());
  return deviceId;
}

function deviceName(): string {
  if (Platform.OS === 'ios') {
    return (Platform.constants as { interfaceIdiom?: string }).interfaceIdiom === 'pad' ? 'iPad' : 'iPhone';
  }
  const model = (Platform.constants as { Model?: string }).Model;
  return model || 'Android phone';
}

export async function registerDevice(): Promise<void> {
  if (!supabase) return;
  await supabase.from('devices').upsert({
    id: await getDeviceId(),
    name: deviceName(),
    platform: Platform.OS === 'ios' ? 'ios' : 'android',
    last_seen_at: new Date().toISOString(),
  });
}

// Called while the session is still valid, just before signing out.
export async function unregisterDevice(): Promise<void> {
  if (!supabase) return;
  await supabase.from('devices').delete().eq('id', await getDeviceId());
}

// supabase-js hands back the existing channel for a repeated topic, so each
// subscriber needs its own or one unmount would tear down the other's.
let channelSeq = 0;

type Loaded = { userId: string; devices: Device[] };

// The account's devices, this one included, most recently seen first.
export function useDevices(): Device[] {
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

    // Realtime doesn't deliver DELETE through a column filter, so a device
    // signing out shows up on the next foreground load instead.
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
    .select('id, name, platform, last_seen_at')
    .order('last_seen_at', { ascending: false });
  return error
    ? null
    : (data as Row[]).map((row) => ({
        id: row.id,
        name: row.name,
        platform: row.platform,
        lastSeenAt: row.last_seen_at,
      }));
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
