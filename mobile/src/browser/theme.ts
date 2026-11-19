// Palette lifted from the desktop browser's sidebar views (avora_sidebar_view,
// avora_spaces_bar_view) so the two apps read as the same product.
export const colors = {
  background: '#101418',
  surface: '#181C20',
  raised: '#22282E',
  border: '#323840',
  hairline: 'rgba(255,255,255,0.08)',
  fill: 'rgba(255,255,255,0.10)',

  text: '#EDF2F5',
  textSecondary: 'rgba(237,242,245,0.70)',
  textTertiary: 'rgba(237,242,245,0.45)',

  // Default Space accent.
  accent: '#7C93FF',
  accentSoft: 'rgba(124,147,255,0.18)',
  danger: '#FF6B81',
} as const;

// Avora's Space accent rotation, used to tint favicon fallbacks.
export const accentRotation = [
  '#7C93FF',
  '#5AC8FA',
  '#4FD1A5',
  '#F5C451',
  '#FF8A5B',
  '#FF6B81',
  '#F78FD0',
  '#D98CFF',
] as const;

export const radius = {
  pill: 999,
  card: 14,
  row: 12,
} as const;

// On wide screens (iPad, a phone in landscape) content keeps to a centred
// column this wide instead of stretching edge to edge.
export const COLUMN_WIDTH = 600;

// The usable width (inside the safe area) from which the bottom bar fits on
// one row.
export const WIDE_WIDTH = 600;

// How far in from each side an absolutely positioned overlay sits so that it
// stays inside the safe area, centred, and no wider than `max`.
export function sideInset(
  windowWidth: number,
  insets: { left: number; right: number },
  gutter: number,
  max: number = COLUMN_WIDTH,
): number {
  return Math.max(gutter + Math.max(insets.left, insets.right), (windowWidth - max) / 2);
}

// '#RRGGBB' at the given opacity, for tinting surfaces with a Space's colour.
export function withAlpha(hex: string, alpha: number): string {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex);
  if (!m) return `rgba(124,147,255,${alpha})`;
  const n = parseInt(m[1], 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${alpha})`;
}
