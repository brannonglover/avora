import { useEffect, useState } from 'react';
import {
  AccessibilityInfo,
  Animated,
  Easing,
  type LayoutChangeEvent,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import Svg, { Defs, RadialGradient, Rect, Stop } from 'react-native-svg';

import { aurora } from './Aurora';
import { Favicon } from './Favicon';
import { colors } from './theme';
import { displayText, hostOf } from './url';

// Slow-drifting aurora glows, each on its own period so the pattern never
// visibly repeats. Positions and drift are fractions of the backdrop.
const GLOWS = [
  { id: 'a', color: aurora.start, opacity: 0.42, x: 0.15, y: 0.18, size: 1.25, dx: 0.12, dy: 0.06, period: 11000 },
  { id: 'b', color: aurora.mid, opacity: 0.26, x: 0.75, y: 0.3, size: 1.05, dx: -0.14, dy: 0.08, period: 14000 },
  { id: 'c', color: aurora.end, opacity: 0.36, x: 0.45, y: 0.58, size: 1.35, dx: 0.1, dy: -0.07, period: 17000 },
] as const;

const FADE_MS = 320;

function useReduceMotion() {
  const [reduce, setReduce] = useState(false);
  useEffect(() => {
    AccessibilityInfo.isReduceMotionEnabled().then(setReduce).catch(() => {});
    const sub = AccessibilityInfo.addEventListener('reduceMotionChanged', setReduce);
    return () => sub.remove();
  }, []);
  return reduce;
}

// A 0→1→0 value on a gentle sine, forever.
function useSwing(period: number, enabled: boolean) {
  const [value] = useState(() => new Animated.Value(0));
  useEffect(() => {
    if (!enabled) return;
    const half = { duration: period / 2, easing: Easing.inOut(Easing.sin), useNativeDriver: true };
    const loop = Animated.loop(
      Animated.sequence([
        Animated.timing(value, { toValue: 1, ...half }),
        Animated.timing(value, { toValue: 0, ...half }),
      ]),
    );
    loop.start();
    return () => loop.stop();
  }, [value, period, enabled]);
  return value;
}

function Glow({
  glow,
  width,
  height,
  animate,
}: {
  glow: (typeof GLOWS)[number];
  width: number;
  height: number;
  animate: boolean;
}) {
  const t = useSwing(glow.period, animate);
  const d = width * glow.size;
  return (
    <Animated.View
      style={{
        position: 'absolute',
        left: glow.x * width - d / 2,
        top: glow.y * height - d / 2,
        width: d,
        height: d,
        transform: [
          { translateX: t.interpolate({ inputRange: [0, 1], outputRange: [0, glow.dx * width] }) },
          { translateY: t.interpolate({ inputRange: [0, 1], outputRange: [0, glow.dy * height] }) },
          { scale: t.interpolate({ inputRange: [0, 1], outputRange: [1, 1.15] }) },
        ],
      }}
    >
      <Svg width={d} height={d}>
        <Defs>
          <RadialGradient id={`glow-${glow.id}`} cx="0.5" cy="0.5" r="0.5">
            <Stop offset="0" stopColor={glow.color} stopOpacity={glow.opacity} />
            <Stop offset="0.45" stopColor={glow.color} stopOpacity={glow.opacity * 0.4} />
            <Stop offset="1" stopColor={glow.color} stopOpacity="0" />
          </RadialGradient>
        </Defs>
        <Rect width={d} height={d} fill={`url(#glow-${glow.id})`} />
      </Svg>
    </Animated.View>
  );
}

// Covers a tab until its page first paints, so a fresh or restored tab shows
// where it's headed instead of an empty dark page. Fades out when hidden.
export function TabBackdrop({ url, title, visible }: { url: string | null; title: string; visible: boolean }) {
  const [size, setSize] = useState({ width: 0, height: 0 });
  const [mounted, setMounted] = useState(visible);
  const [opacity] = useState(() => new Animated.Value(visible ? 1 : 0));
  const reduceMotion = useReduceMotion();
  const breathe = useSwing(2400, mounted && !reduceMotion);

  if (visible && !mounted) setMounted(true);

  useEffect(() => {
    const anim = Animated.timing(opacity, {
      toValue: visible ? 1 : 0,
      duration: visible ? 0 : FADE_MS,
      useNativeDriver: true,
    });
    anim.start(({ finished }) => {
      if (finished && !visible) setMounted(false);
    });
    return () => anim.stop();
  }, [visible, opacity]);

  if (!mounted) return null;

  const onLayout = (e: LayoutChangeEvent) => {
    const { width, height } = e.nativeEvent.layout;
    setSize((s) => (s.width === width && s.height === height ? s : { width, height }));
  };
  const host = hostOf(url);
  const label = title || displayText(url) || host;

  return (
    <Animated.View pointerEvents="none" onLayout={onLayout} style={[styles.root, { opacity }]}>
      {size.width > 0
        ? GLOWS.map((glow) => (
            <Glow key={glow.id} glow={glow} width={size.width} height={size.height} animate={!reduceMotion} />
          ))
        : null}

      <View style={styles.center}>
        <Animated.View
          style={[
            styles.tile,
            { opacity: breathe.interpolate({ inputRange: [0, 1], outputRange: [0.7, 1] }) },
          ]}
        >
          <Favicon url={url} size={32} />
        </Animated.View>
        {label ? (
          <Text style={styles.title} numberOfLines={2}>
            {label}
          </Text>
        ) : null}
        {host && host !== label ? (
          <Text style={styles.host} numberOfLines={1}>
            {host}
          </Text>
        ) : null}
      </View>
    </Animated.View>
  );
}

const styles = StyleSheet.create({
  root: { ...StyleSheet.absoluteFill, backgroundColor: colors.background, overflow: 'hidden' },
  center: {
    ...StyleSheet.absoluteFill,
    alignItems: 'center',
    justifyContent: 'center',
    paddingHorizontal: 40,
  },
  tile: {
    width: 64,
    height: 64,
    borderRadius: 18,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(24,28,32,0.72)',
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.hairline,
  },
  title: {
    marginTop: 16,
    color: colors.text,
    fontSize: 17,
    fontWeight: '600',
    textAlign: 'center',
  },
  host: { marginTop: 4, color: colors.textTertiary, fontSize: 13 },
});
