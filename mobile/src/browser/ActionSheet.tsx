import type { LucideIcon } from 'lucide-react-native';
import { Modal, Pressable, StyleSheet, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { colors, radius } from './theme';

export type SheetAction = {
  label: string;
  icon: LucideIcon;
  onPress: () => void;
  destructive?: boolean;
};

type Props = {
  visible: boolean;
  title?: string;
  subtitle?: string;
  actions: SheetAction[];
  onClose: () => void;
};

export function ActionSheet({ visible, title, subtitle, actions, onClose }: Props) {
  const insets = useSafeAreaInsets();

  return (
    <Modal visible={visible} transparent animationType="fade" onRequestClose={onClose}>
      <Pressable style={styles.backdrop} onPress={onClose}>
        <Pressable style={[styles.sheet, { marginBottom: insets.bottom + 8 }]}>
          {title ? (
            <View style={styles.header}>
              <Text style={styles.title} numberOfLines={1}>
                {title}
              </Text>
              {subtitle ? (
                <Text style={styles.subtitle} numberOfLines={1}>
                  {subtitle}
                </Text>
              ) : null}
            </View>
          ) : null}
          {actions.map(({ label, icon: Icon, onPress, destructive }, i) => (
            <Pressable
              key={label}
              style={({ pressed }) => [
                styles.row,
                i > 0 || title ? styles.rowDivider : null,
                pressed && styles.rowPressed,
              ]}
              onPress={() => {
                onClose();
                onPress();
              }}
            >
              <Text style={[styles.label, destructive && styles.destructive]}>{label}</Text>
              <Icon size={20} color={destructive ? colors.danger : colors.textSecondary} />
            </Pressable>
          ))}
        </Pressable>
      </Pressable>
    </Modal>
  );
}

const styles = StyleSheet.create({
  backdrop: {
    flex: 1,
    justifyContent: 'flex-end',
    backgroundColor: 'rgba(0,0,0,0.45)',
    paddingHorizontal: 10,
  },
  sheet: {
    backgroundColor: colors.raised,
    borderRadius: radius.card + 4,
    overflow: 'hidden',
  },
  header: { paddingHorizontal: 18, paddingVertical: 12 },
  title: { color: colors.text, fontSize: 15, fontWeight: '600' },
  subtitle: { color: colors.textTertiary, fontSize: 13, marginTop: 2 },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 18,
    paddingVertical: 15,
  },
  rowDivider: { borderTopWidth: StyleSheet.hairlineWidth, borderTopColor: colors.border },
  rowPressed: { backgroundColor: colors.fill },
  label: { color: colors.text, fontSize: 16 },
  destructive: { color: colors.danger },
});
