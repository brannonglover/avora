import { useEffect, useRef, useState } from 'react';
import { Animated, Easing, type LayoutChangeEvent, StyleSheet, View } from 'react-native';
import Svg, { Defs, LinearGradient, RadialGradient, Rect, Stop } from 'react-native-svg';

// The desktop loading bar's ramp (avora_loading_bar_view.cc): accent blue
// rising through cyan into violet, with an almost-white core at the head.
export const aurora = {
  start: '#5B8BFF',
  mid: '#68E0F8',
  end: '#A78BFA',
  core: '#EDF7FF',
} as const;

// Never fill the last slice while loading; reaching the edge is reserved for
// the moment the page is actually done.
const LOADING_CEILING = 0.985;
const MIN_VISIBLE = 0.08;
const HEAD_RADIUS = 14;
const RIBBON = 2;

function useSize() {
  const [size, setSize] = useState({ width: 0, height: 0 });
  const onLayout = (e: LayoutChangeEvent) => {
    const { width, height } = e.nativeEvent.layout;
    setSize((s) => (s.width === width && s.height === height ? s : { width, height }));
  };
  return [size, onLayout] as const;
}

// LinearGradient wants its stops as direct children, so this returns an array
// rather than a component.
function auroraStops() {
  return [
    <Stop key="start" offset="0" stopColor={aurora.start} />,
    <Stop key="mid" offset="0.55" stopColor={aurora.mid} />,
    <Stop key="end" offset="1" stopColor={aurora.end} />,
  ];
}

// Fills the address pill from the left as the page loads: a faint aurora wash
// with a bright ribbon along the bottom edge and a bloom at its head. The ramp
// is anchored to the pill, not the filled part, so colours are uncovered as
// the page loads rather than stretching.
export function AuroraProgress({
  loading,
  progress,
  radius,
}: {
  loading: boolean;
  progress: number;
  radius: number;
}) {
  const [{ width: w, height: h }, onLayout] = useSize();
  const [p] = useState(() => new Animated.Value(0));
  const [opacity] = useState(() => new Animated.Value(0));
  const wasLoading = useRef(false);

  useEffect(() => {
    if (loading) {
      if (!wasLoading.current) p.setValue(0);
      Animated.timing(opacity, { toValue: 1, duration: 120, useNativeDriver: true }).start();
      Animated.timing(p, {
        toValue: Math.min(Math.max(progress, MIN_VISIBLE), LOADING_CEILING),
        duration: 260,
        easing: Easing.out(Easing.cubic),
        useNativeDriver: true,
      }).start();
    } else if (wasLoading.current) {
      // Snap home, then fade.
      Animated.sequence([
        Animated.timing(p, {
          toValue: 1,
          duration: 140,
          easing: Easing.out(Easing.quad),
          useNativeDriver: true,
        }),
        Animated.timing(opacity, { toValue: 0, duration: 320, useNativeDriver: true }),
      ]).start();
    }
    wasLoading.current = loading;
  }, [loading, progress, p, opacity]);

  const ready = w > 0 && h > 0;

  return (
    <View
      pointerEvents="none"
      onLayout={onLayout}
      style={[StyleSheet.absoluteFill, { borderRadius: radius, overflow: 'hidden' }]}
    >
      {ready ? (
        <>
          <Animated.View
            style={[
              styles.clip,
              {
                width: w,
                opacity,
                transform: [{ translateX: p.interpolate({ inputRange: [0, 1], outputRange: [-w, 0] }) }],
              },
            ]}
          >
            <Animated.View
              style={[
                styles.clip,
                {
                  width: w,
                  transform: [{ translateX: p.interpolate({ inputRange: [0, 1], outputRange: [w, 0] }) }],
                },
              ]}
            >
              <Svg width={w} height={h}>
                <Defs>
                  <LinearGradient id="auroraFill" x1="0" y1="0" x2="1" y2="0">
                    {auroraStops()}
                  </LinearGradient>
                  <LinearGradient id="auroraWash" x1="0" y1="0" x2="0" y2="1">
                    <Stop offset="0" stopColor={aurora.mid} stopOpacity="0" />
                    <Stop offset="1" stopColor={aurora.start} stopOpacity="0.22" />
                  </LinearGradient>
                </Defs>
                <Rect x="0" y="0" width={w} height={h} fill="url(#auroraFill)" opacity={0.1} />
                <Rect x="0" y="0" width={w} height={h} fill="url(#auroraWash)" />
                <Rect x="0" y={h - RIBBON} width={w} height={RIBBON} fill="url(#auroraFill)" />
              </Svg>
            </Animated.View>
          </Animated.View>

          <Animated.View
            style={[
              styles.head,
              {
                top: h - HEAD_RADIUS - RIBBON / 2,
                opacity,
                transform: [
                  {
                    translateX: p.interpolate({
                      inputRange: [0, 1],
                      outputRange: [-HEAD_RADIUS, w - HEAD_RADIUS],
                    }),
                  },
                ],
              },
            ]}
          >
            <Svg width={HEAD_RADIUS * 2} height={HEAD_RADIUS * 2}>
              <Defs>
                <RadialGradient id="auroraHead" cx="0.5" cy="0.5" r="0.5">
                  <Stop offset="0" stopColor={aurora.core} stopOpacity="0.95" />
                  <Stop offset="0.25" stopColor={aurora.mid} stopOpacity="0.55" />
                  <Stop offset="1" stopColor={aurora.mid} stopOpacity="0" />
                </RadialGradient>
              </Defs>
              <Rect
                width={HEAD_RADIUS * 2}
                height={HEAD_RADIUS * 2}
                fill="url(#auroraHead)"
              />
            </Svg>
          </Animated.View>
        </>
      ) : null}
    </View>
  );
}

// An aurora-gradient outline, drawn over the pill while it's being edited.
export function AuroraRing({ radius, strokeWidth = 1.5 }: { radius: number; strokeWidth?: number }) {
  const [{ width: w, height: h }, onLayout] = useSize();
  const inset = strokeWidth / 2;
  return (
    <View pointerEvents="none" onLayout={onLayout} style={StyleSheet.absoluteFill}>
      {w > 0 && h > 0 ? (
        <Svg width={w} height={h}>
          <Defs>
            <LinearGradient id="auroraRing" x1="0" y1="0" x2="1" y2="1">
              {auroraStops()}
            </LinearGradient>
          </Defs>
          <Rect
            x={inset}
            y={inset}
            width={w - strokeWidth}
            height={h - strokeWidth}
            rx={radius - inset}
            fill="none"
            stroke="url(#auroraRing)"
            strokeWidth={strokeWidth}
          />
        </Svg>
      ) : null}
    </View>
  );
}

const styles = StyleSheet.create({
  clip: { position: 'absolute', top: 0, bottom: 0, left: 0, overflow: 'hidden' },
  head: { position: 'absolute', left: 0, width: HEAD_RADIUS * 2, height: HEAD_RADIUS * 2 },
});
