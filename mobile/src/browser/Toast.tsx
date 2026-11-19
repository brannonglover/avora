import { useEffect, useState } from 'react';
import { Animated, Pressable, StyleSheet, Text, useWindowDimensions } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { colors, radius, sideInset } from './theme';

export type ToastMessage = {
  id: number;
  text: string;
  actionLabel?: string;
  onAction?: () => void;
};

const VISIBLE_MS = 4000;

type Props = {
  toast: ToastMessage | null;
  onDismiss: () => void;
  // Distance from the bottom of the screen.
  bottom: number;
};

export function Toast({ toast, onDismiss, bottom }: Props) {
  const insets = useSafeAreaInsets();
  const { width } = useWindowDimensions();
  const side = sideInset(width, insets, 16);
  const [opacity] = useState(() => new Animated.Value(0));

  useEffect(() => {
    if (!toast) return;
    opacity.setValue(0);
    Animated.timing(opacity, { toValue: 1, duration: 160, useNativeDriver: true }).start();
    const timer = setTimeout(() => {
      Animated.timing(opacity, { toValue: 0, duration: 200, useNativeDriver: true }).start(onDismiss);
    }, VISIBLE_MS);
    return () => clearTimeout(timer);
  }, [toast, opacity, onDismiss]);

  if (!toast) return null;

  return (
    <Animated.View
      style={[
        styles.toast,
        {
          bottom,
          left: side,
          right: side,
          opacity,
          transform: [{ translateY: opacity.interpolate({ inputRange: [0, 1], outputRange: [8, 0] }) }],
        },
      ]}
    >
      <Text style={styles.text} numberOfLines={1}>
        {toast.text}
      </Text>
      {toast.actionLabel ? (
        <Pressable
          hitSlop={10}
          onPress={() => {
            toast.onAction?.();
            onDismiss();
          }}
        >
          <Text style={styles.action}>{toast.actionLabel}</Text>
        </Pressable>
      ) : null}
    </Animated.View>
  );
}

const styles = StyleSheet.create({
  toast: {
    position: 'absolute',
    zIndex: 20,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: 12,
    paddingHorizontal: 16,
    paddingVertical: 12,
    borderRadius: radius.row,
    backgroundColor: colors.raised,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.border,
    shadowColor: '#000',
    shadowOpacity: 0.35,
    shadowRadius: 12,
    shadowOffset: { width: 0, height: 4 },
    elevation: 6,
  },
  text: { color: colors.text, fontSize: 14, flexShrink: 1 },
  action: { color: colors.accent, fontSize: 14, fontWeight: '600' },
});
