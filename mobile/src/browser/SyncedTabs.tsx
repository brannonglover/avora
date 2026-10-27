import { ChevronRight, Laptop, Pin } from 'lucide-react-native';
import { useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { type Device, type SyncedTab, updatedAgo } from '../sync/devices';
import { Favicon } from './Favicon';
import { colors, radius } from './theme';
import { hostOf } from './url';

// Enough to find what you were just doing on the Mac without burying the
// phone's own tabs under a desktop's worth of them.
const COLLAPSED_COUNT = 6;

type Props = {
  configured: boolean;
  signedIn: boolean;
  devices: Device[];
  onOpen: (url: string) => void;
  onAccount: () => void;
};

export function SyncedTabs({ configured, signedIn, devices, onOpen, onAccount }: Props) {
  if (!configured) return null;

  if (!signedIn) {
    return (
      <Pressable onPress={onAccount} style={({ pressed }) => [styles.promo, pressed && styles.pressed]}>
        <Laptop size={22} color={colors.accent} />
        <View style={styles.promoText}>
          <Text style={styles.promoTitle}>See your Mac’s tabs here</Text>
          <Text style={styles.promoDetail}>Sign in to pick up where you left off.</Text>
        </View>
        <ChevronRight size={18} color={colors.textTertiary} />
      </Pressable>
    );
  }

  return devices
    .filter((d) => d.tabs.length)
    .map((device) => <DeviceSection key={device.id} device={device} onOpen={onOpen} />);
}

function DeviceSection({ device, onOpen }: { device: Device; onOpen: (url: string) => void }) {
  const [expanded, setExpanded] = useState(false);
  const tabs = sortTabs(device.tabs);
  const shown = expanded ? tabs : tabs.slice(0, COLLAPSED_COUNT);
  const hidden = tabs.length - shown.length;

  return (
    <View style={styles.section}>
      <View style={styles.label}>
        <Laptop size={12} color={colors.textTertiary} />
        <Text style={styles.labelText} numberOfLines={1}>
          {device.name}
        </Text>
        <Text style={styles.labelDetail}>{updatedAgo(device.updatedAt)}</Text>
      </View>
      <View style={styles.list}>
        {shown.map((tab, i) => (
          <Pressable
            key={`${i}:${tab.url}`}
            onPress={() => onOpen(tab.url)}
            style={({ pressed }) => [styles.row, i > 0 && styles.divider, pressed && styles.pressed]}
            accessibilityLabel={`Open ${tab.title || hostOf(tab.url)} from ${device.name}`}
          >
            <Favicon url={tab.url} size={22} />
            <View style={styles.rowText}>
              <Text style={styles.rowTitle} numberOfLines={1}>
                {tab.title || hostOf(tab.url)}
              </Text>
              <Text style={styles.rowHost} numberOfLines={1}>
                {tab.space ? `${tab.space} · ${hostOf(tab.url)}` : hostOf(tab.url)}
              </Text>
            </View>
            {tab.pinned ? <Pin size={14} color={colors.textTertiary} /> : null}
          </Pressable>
        ))}
        {hidden > 0 ? (
          <Pressable
            onPress={() => setExpanded(true)}
            style={({ pressed }) => [styles.more, styles.divider, pressed && styles.pressed]}
          >
            <Text style={styles.moreText}>Show {hidden} more</Text>
          </Pressable>
        ) : null}
      </View>
    </View>
  );
}

// Pinned first, like the desktop sidebar, then most recently used.
function sortTabs(tabs: SyncedTab[]): SyncedTab[] {
  return [...tabs].sort(
    (a, b) => Number(b.pinned) - Number(a.pinned) || (b.lastActiveAt ?? 0) - (a.lastActiveAt ?? 0),
  );
}

const styles = StyleSheet.create({
  section: { marginTop: 14 },
  label: { flexDirection: 'row', alignItems: 'center', gap: 5, paddingHorizontal: 4, marginBottom: 8 },
  labelText: {
    flexShrink: 1,
    color: colors.textTertiary,
    fontSize: 12,
    fontWeight: '600',
    textTransform: 'uppercase',
    letterSpacing: 0.6,
  },
  labelDetail: { marginLeft: 'auto', color: colors.textTertiary, fontSize: 12 },
  list: { backgroundColor: colors.surface, borderRadius: radius.card, overflow: 'hidden' },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    paddingLeft: 14,
    paddingRight: 14,
    paddingVertical: 11,
  },
  divider: { borderTopWidth: StyleSheet.hairlineWidth, borderTopColor: colors.border },
  rowText: { flex: 1 },
  rowTitle: { color: colors.text, fontSize: 15 },
  rowHost: { color: colors.textTertiary, fontSize: 12, marginTop: 1 },
  more: { paddingVertical: 12, alignItems: 'center' },
  moreText: { color: colors.accent, fontSize: 14 },
  pressed: { opacity: 0.7 },
  promo: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    marginTop: 20,
    padding: 14,
    borderRadius: radius.card,
    backgroundColor: colors.surface,
  },
  promoText: { flex: 1 },
  promoTitle: { color: colors.text, fontSize: 15, fontWeight: '600' },
  promoDetail: { color: colors.textTertiary, fontSize: 13, marginTop: 2 },
});
