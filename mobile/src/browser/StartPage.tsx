import { History } from 'lucide-react-native';
import { Image, Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';

import { Favicon } from './Favicon';
import type { ClosedTab } from './tabs';
import { colors, radius } from './theme';
import { hostOf } from './url';

type Props = {
  recentlyClosed: ClosedTab[];
  onReopen: (closedAt: number) => void;
};

export function StartPage({ recentlyClosed, onReopen }: Props) {
  const recent = recentlyClosed.slice(0, 6);

  return (
    <ScrollView
      style={styles.page}
      contentContainerStyle={styles.content}
      keyboardShouldPersistTaps="handled"
    >
      <View style={styles.hero}>
        <Image source={require('../../assets/icon.png')} style={styles.logo} />
        <Text style={styles.wordmark}>Avora</Text>
      </View>

      {recent.length ? (
        <View style={styles.section}>
          <View style={styles.sectionHeader}>
            <History size={14} color={colors.textTertiary} />
            <Text style={styles.sectionTitle}>Recently closed</Text>
          </View>
          {recent.map((entry) => (
            <Pressable
              key={entry.closedAt}
              onPress={() => onReopen(entry.closedAt)}
              style={({ pressed }) => [styles.row, pressed && styles.rowPressed]}
            >
              <Favicon url={entry.tab.url} size={20} />
              <View style={styles.rowText}>
                <Text style={styles.rowTitle} numberOfLines={1}>
                  {entry.tab.title || hostOf(entry.tab.url)}
                </Text>
                <Text style={styles.rowHost} numberOfLines={1}>
                  {hostOf(entry.tab.url)}
                </Text>
              </View>
            </Pressable>
          ))}
        </View>
      ) : null}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  page: { flex: 1, backgroundColor: colors.background },
  content: { paddingHorizontal: 20, paddingTop: 72, paddingBottom: 32 },
  hero: { alignItems: 'center', marginBottom: 40 },
  logo: { width: 72, height: 72, borderRadius: 18 },
  wordmark: {
    marginTop: 14,
    color: colors.text,
    fontSize: 28,
    fontWeight: '700',
    letterSpacing: 0.5,
  },
  section: {
    backgroundColor: colors.surface,
    borderRadius: radius.card,
    paddingVertical: 6,
  },
  sectionHeader: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    paddingHorizontal: 14,
    paddingTop: 8,
    paddingBottom: 6,
  },
  sectionTitle: {
    color: colors.textTertiary,
    fontSize: 12,
    fontWeight: '600',
    textTransform: 'uppercase',
    letterSpacing: 0.6,
  },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    paddingHorizontal: 14,
    paddingVertical: 10,
  },
  rowPressed: { backgroundColor: colors.fill },
  rowText: { flex: 1 },
  rowTitle: { color: colors.text, fontSize: 15 },
  rowHost: { color: colors.textTertiary, fontSize: 12, marginTop: 1 },
});
