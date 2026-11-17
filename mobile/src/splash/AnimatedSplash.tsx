import * as SplashScreen from 'expo-splash-screen';
import { useEffect, useState } from 'react';
import { AccessibilityInfo, Animated, Dimensions, Easing, StyleSheet, View } from 'react-native';

import { colors } from '../browser/theme';
import { Glow, GLOW_TURN_MS, Sheen, sheenStyle, SPLASH_LOGO as LOGO } from './glow';
import { logoTarget, markSplashDone, setGlowPhase } from './handoff';

// The overlay starts as a pixel-for-pixel copy of the native splash, so hiding
// the native one is invisible. The start page clips its 72pt logo to an 18pt
// radius; same ratio here, so nothing changes shape when the logo lands.
const LOGO_RADIUS = LOGO * (18 / 72);

// How long the logo holds the stage before handing off to the browser.
const INTRO_MS = 950;

export function AnimatedSplash() {
  const [visible, setVisible] = useState(true);
  const [exiting, setExiting] = useState(false);

  const [v] = useState(() => ({
    backdrop: new Animated.Value(1),
    glow: new Animated.Value(0),
    spin: new Animated.Value(0),
    pulse: new Animated.Value(1),
    sheen: new Animated.Value(0),
    logoOpacity: new Animated.Value(1),
    // 0 = centred like the native splash, 1 = landed on the start page.
    fly: new Animated.Value(0),
    // Where the logo flies to: offset from the centre and the final scale.
    dx: new Animated.Value(0),
    dy: new Animated.Value(0),
    endScale: new Animated.Value(1),
  }));

  useEffect(() => {
    let cancelled = false;

    const finish = () => {
      if (cancelled) return;
      // The start page's glow carries on from wherever this one got to.
      v.spin.stopAnimation((turns) => {
        setGlowPhase(turns % 1);
        markSplashDone();
        setVisible(false);
      });
    };

    const exit = (reduceMotion: boolean) => {
      if (cancelled) return;
      setExiting(true);
      const target = logoTarget();

      if (reduceMotion) {
        Animated.timing(v.backdrop, { toValue: 0, duration: 250, useNativeDriver: true }).start(finish);
        Animated.timing(v.logoOpacity, { toValue: 0, duration: 250, useNativeDriver: true }).start();
        return;
      }

      const fadeBackdrop = Animated.timing(v.backdrop, {
        toValue: 0,
        duration: 420,
        easing: Easing.out(Easing.quad),
        useNativeDriver: true,
      });

      if (target) {
        // Glide onto the start page's logo, glow and all, then swap in the
        // start page's own on the same frame.
        const { width, height } = Dimensions.get('window');
        v.dx.setValue(target.x + target.width / 2 - width / 2);
        v.dy.setValue(target.y + target.height / 2 - height / 2);
        v.endScale.setValue(target.width / LOGO);
        Animated.parallel([
          fadeBackdrop,
          Animated.timing(v.fly, {
            toValue: 1,
            duration: 560,
            easing: Easing.bezier(0.2, 0.8, 0.2, 1),
            useNativeDriver: true,
          }),
        ]).start(finish);
      } else {
        // No start page (a site is open): the logo swells and dissolves.
        v.endScale.setValue(1.5);
        Animated.parallel([
          fadeBackdrop,
          Animated.timing(v.glow, {
            toValue: 0,
            duration: 320,
            easing: Easing.in(Easing.quad),
            useNativeDriver: true,
          }),
          Animated.timing(v.fly, {
            toValue: 1,
            duration: 420,
            easing: Easing.out(Easing.cubic),
            useNativeDriver: true,
          }),
          Animated.timing(v.logoOpacity, {
            toValue: 0,
            duration: 380,
            easing: Easing.in(Easing.quad),
            useNativeDriver: true,
          }),
        ]).start(finish);
      }
    };

    AccessibilityInfo.isReduceMotionEnabled()
      .catch(() => false)
      .then((reduceMotion) => {
        if (cancelled) return;
        if (reduceMotion) {
          setTimeout(() => exit(true), 300);
          return;
        }

        // Drifts at the same pace as the start page's glow, which takes over.
        Animated.loop(
          Animated.timing(v.spin, {
            toValue: 1,
            duration: GLOW_TURN_MS,
            easing: Easing.linear,
            useNativeDriver: true,
          }),
        ).start();

        Animated.parallel([
          // The glow blooms out from behind the logo.
          Animated.timing(v.glow, {
            toValue: 1,
            duration: 700,
            easing: Easing.out(Easing.cubic),
            useNativeDriver: true,
          }),
          // The logo draws a breath in and settles.
          Animated.sequence([
            Animated.timing(v.pulse, {
              toValue: 0.92,
              duration: 220,
              easing: Easing.inOut(Easing.quad),
              useNativeDriver: true,
            }),
            Animated.spring(v.pulse, {
              toValue: 1,
              friction: 4,
              tension: 90,
              useNativeDriver: true,
            }),
          ]),
          // A glint of light crosses it.
          Animated.sequence([
            Animated.delay(260),
            Animated.timing(v.sheen, {
              toValue: 1,
              duration: 620,
              easing: Easing.inOut(Easing.cubic),
              useNativeDriver: true,
            }),
          ]),
        ]).start();

        setTimeout(() => exit(false), INTRO_MS);
      });

    return () => {
      cancelled = true;
    };
  }, [v]);

  if (!visible) return null;

  const glowScale = v.glow.interpolate({ inputRange: [0, 1], outputRange: [0.55, 1] });
  const rotate = v.spin.interpolate({ inputRange: [0, 1], outputRange: ['0deg', '360deg'] });
  const sheenX = v.sheen.interpolate({ inputRange: [0, 1], outputRange: [-LOGO, LOGO] });
  const translateX = Animated.multiply(v.fly, v.dx);
  const translateY = Animated.multiply(v.fly, v.dy);
  // Scale from 1 to endScale as fly goes 0 to 1, times the intro's pulse.
  const flyScale = Animated.add(1, Animated.multiply(v.fly, Animated.add(v.endScale, -1)));
  const scale = Animated.multiply(flyScale, v.pulse);

  return (
    <View
      style={StyleSheet.absoluteFill}
      pointerEvents={exiting ? 'none' : 'auto'}
      onLayout={() => SplashScreen.hide()}
    >
      <Animated.View style={[StyleSheet.absoluteFill, styles.backdrop, { opacity: v.backdrop }]} />

      <View style={styles.center} pointerEvents="none">
        {/* The glow rides along with the logo as it leaves. */}
        <Animated.View
          style={{
            opacity: v.glow,
            transform: [
              { translateX },
              { translateY },
              { scale: Animated.multiply(glowScale, flyScale) },
              { rotate },
            ],
          }}
        >
          <Glow logo={LOGO} />
        </Animated.View>
      </View>

      <View style={styles.center} pointerEvents="none">
        <Animated.View
          style={[
            styles.logo,
            { opacity: v.logoOpacity, transform: [{ translateX }, { translateY }, { scale }] },
          ]}
        >
          <Animated.Image source={require('../../assets/splash-icon.png')} style={styles.logoImage} />
          <Animated.View
            style={[sheenStyle(LOGO), { transform: [{ translateX: sheenX }, { rotate: '18deg' }] }]}
          >
            <Sheen logo={LOGO} />
          </Animated.View>
        </Animated.View>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  backdrop: { backgroundColor: colors.background },
  center: {
    position: 'absolute',
    top: 0,
    left: 0,
    right: 0,
    bottom: 0,
    alignItems: 'center',
    justifyContent: 'center',
  },
  logo: {
    width: LOGO,
    height: LOGO,
    borderRadius: LOGO_RADIUS,
    overflow: 'hidden',
  },
  logoImage: { width: LOGO, height: LOGO },
});
