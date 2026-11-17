import { useEffect, useRef, useState } from 'react';
import { AccessibilityInfo, Animated, Easing, Image, StyleSheet, View, type ViewInstance } from 'react-native';

import { Glow, GLOW_TURN_MS, glowSize, Sheen, sheenStyle } from './glow';
import { glowPhase, setLogoTarget, useSplashDone } from './handoff';

// How long between glints of light across the logo.
const SHEEN_EVERY_MS = 5200;
// One breath of the glow, in and out.
const BREATH_MS = 5600;

// The start page's logo, still alive: the launch splash lands its logo and
// glow here, and the colours keep drifting and breathing until the start page
// goes away (a search or a site opens in its place).
export function LivingLogo({ size, radius }: { size: number; radius: number }) {
  const ref = useRef<ViewInstance>(null);
  // Until the splash lands, it draws this logo itself; showing ours too would
  // put two on screen.
  const splashDone = useSplashDone();
  // A start page opened later (a new tab) fades its glow in rather than
  // taking over a hand-off.
  const [lateArrival] = useState(splashDone);

  const [a] = useState(() => ({
    appear: new Animated.Value(lateArrival ? 0 : 1),
    phase: new Animated.Value(0),
    spin: new Animated.Value(0),
    breath: new Animated.Value(0),
    sheen: new Animated.Value(0),
  }));

  useEffect(() => () => setLogoTarget(null), []);

  useEffect(() => {
    if (!splashDone) return;
    let cancelled = false;
    let loops: Animated.CompositeAnimation[] = [];

    // Pick up the drift where the splash left it.
    if (!lateArrival) a.phase.setValue(glowPhase());

    AccessibilityInfo.isReduceMotionEnabled()
      .catch(() => false)
      .then((reduceMotion) => {
        if (cancelled) return;
        if (lateArrival) {
          Animated.timing(a.appear, {
            toValue: 1,
            duration: reduceMotion ? 0 : 500,
            easing: Easing.out(Easing.quad),
            useNativeDriver: true,
          }).start();
        }
        if (reduceMotion) return;

        loops = [
          Animated.loop(
            Animated.timing(a.spin, {
              toValue: 1,
              duration: GLOW_TURN_MS,
              easing: Easing.linear,
              useNativeDriver: true,
            }),
          ),
          Animated.loop(
            Animated.sequence([
              Animated.timing(a.breath, {
                toValue: 1,
                duration: BREATH_MS / 2,
                easing: Easing.inOut(Easing.sin),
                useNativeDriver: true,
              }),
              Animated.timing(a.breath, {
                toValue: 0,
                duration: BREATH_MS / 2,
                easing: Easing.inOut(Easing.sin),
                useNativeDriver: true,
              }),
            ]),
          ),
          Animated.loop(
            Animated.sequence([
              Animated.delay(SHEEN_EVERY_MS),
              Animated.timing(a.sheen, {
                toValue: 1,
                duration: 900,
                easing: Easing.inOut(Easing.cubic),
                useNativeDriver: true,
              }),
            ]),
          ),
        ];
        loops.forEach((l) => l.start());
      });

    return () => {
      cancelled = true;
      loops.forEach((l) => l.stop());
    };
  }, [splashDone, lateArrival, a]);

  const glow = glowSize(size);
  // spin counts turns from 0 to 1; phase is where it started, so the sum can
  // run up to 2.
  const rotate = Animated.add(a.spin, a.phase).interpolate({
    inputRange: [0, 2],
    outputRange: ['0deg', '720deg'],
  });
  const glowOpacity = Animated.multiply(
    a.appear,
    a.breath.interpolate({ inputRange: [0, 1], outputRange: [1, 0.65] }),
  );
  const glowScale = a.breath.interpolate({ inputRange: [0, 1], outputRange: [1, 1.08] });
  const sheenX = a.sheen.interpolate({ inputRange: [0, 1], outputRange: [-size, size] });

  return (
    <View
      ref={ref}
      style={[{ width: size, height: size }, !splashDone && styles.hidden]}
      onLayout={() =>
        ref.current?.measureInWindow((x, y, width, height) => setLogoTarget({ x, y, width, height }))
      }
    >
      <Animated.View
        pointerEvents="none"
        style={[
          styles.glow,
          {
            left: (size - glow) / 2,
            top: (size - glow) / 2,
            opacity: glowOpacity,
            transform: [{ scale: glowScale }, { rotate }],
          },
        ]}
      >
        <Glow logo={size} />
      </Animated.View>
      <View style={{ width: size, height: size, borderRadius: radius, overflow: 'hidden' }}>
        <Image source={require('../../assets/icon.png')} style={{ width: size, height: size }} />
        <Animated.View
          pointerEvents="none"
          style={[sheenStyle(size), { transform: [{ translateX: sheenX }, { rotate: '18deg' }] }]}
        >
          <Sheen logo={size} />
        </Animated.View>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  hidden: { opacity: 0 },
  glow: { position: 'absolute' },
});
