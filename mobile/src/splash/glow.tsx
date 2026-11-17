import Svg, { Circle, Defs, LinearGradient, RadialGradient, Rect, Stop } from 'react-native-svg';

// The logo's aurora, drawn the same way by the launch splash and by the start
// page it hands off to, so the hand-off doesn't change anything on screen.

// Must match the expo-splash-screen plugin's imageWidth in app.config.js.
export const SPLASH_LOGO = 160;

// The glow's size relative to the logo it sits behind.
const GLOW_RATIO = 460 / SPLASH_LOGO;
const BLOB_RATIO = 150 / SPLASH_LOGO;
const ORBIT_RATIO = 62 / SPLASH_LOGO;

// One full turn of the drifting colours.
export const GLOW_TURN_MS = 9000;

// The icon's own gradient, left to right along the "A".
const glowColors = ['#6A7DFF', '#7DF2D3', '#F28AC8'] as const;

export function glowSize(logo: number) {
  return logo * GLOW_RATIO;
}

// Three soft blobs a third of a turn apart. Whoever draws this rotates it, so
// the colours drift around the logo like an aurora.
export function Glow({ logo }: { logo: number }) {
  const size = glowSize(logo);
  const c = size / 2;
  return (
    <Svg width={size} height={size}>
      <Defs>
        {glowColors.map((color, i) => (
          <RadialGradient key={color} id={`blob${i}`} cx="50%" cy="50%" r="50%">
            <Stop offset="0" stopColor={color} stopOpacity={0.55} />
            <Stop offset="0.55" stopColor={color} stopOpacity={0.16} />
            <Stop offset="1" stopColor={color} stopOpacity={0} />
          </RadialGradient>
        ))}
      </Defs>
      {glowColors.map((color, i) => {
        const a = (i / glowColors.length) * Math.PI * 2 - Math.PI / 2;
        return (
          <Circle
            key={color}
            cx={c + Math.cos(a) * logo * ORBIT_RATIO}
            cy={c + Math.sin(a) * logo * ORBIT_RATIO}
            r={logo * BLOB_RATIO}
            fill={`url(#blob${i})`}
          />
        );
      })}
    </Svg>
  );
}

// A soft diagonal band of light. Place it with sheenStyle inside a view that
// clips to the logo, and slide it from -logo to +logo on translateX.
export function Sheen({ logo }: { logo: number }) {
  return (
    <Svg width={logo * 0.55} height={logo * 1.8}>
      <Defs>
        <LinearGradient id="sheen" x1="0" y1="0" x2="1" y2="0">
          <Stop offset="0" stopColor="#FFFFFF" stopOpacity={0} />
          <Stop offset="0.5" stopColor="#FFFFFF" stopOpacity={0.28} />
          <Stop offset="1" stopColor="#FFFFFF" stopOpacity={0} />
        </LinearGradient>
      </Defs>
      <Rect width="100%" height="100%" fill="url(#sheen)" />
    </Svg>
  );
}

export function sheenStyle(logo: number) {
  return { position: 'absolute', top: -logo * 0.4, left: logo * 0.225 } as const;
}
