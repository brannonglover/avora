import { generateKeyBetween } from 'fractional-indexing';

// The shapes of public.spaces and public.items, kept in the server's own
// column names so a row from the server can replace the local copy wholesale.
// docs/SYNC.md is the contract the desktop implements too.

export type ItemKind = 'today' | 'pinned' | 'favorite' | 'folder';

export type SpaceRow = {
  id: string;
  name: string;
  icon: string;
  color: string;
  position: string;
  // Null on a row the server hasn't seen yet.
  updated_at: string | null;
  updated_by: string | null;
  deleted_at: string | null;
};

export type ItemRow = {
  id: string;
  space_id: string;
  kind: ItemKind;
  parent_id: string | null;
  url: string;
  title: string;
  favicon_url: string | null;
  preview_path: string | null;
  preview_updated_at: string | null;
  position: string;
  last_active_at: string | null;
  updated_at: string | null;
  updated_by: string | null;
  deleted_at: string | null;
};

// Only the columns that changed; a key that's present is written, even as null.
export type SpaceChange = Partial<Omit<SpaceRow, 'id' | 'updated_at' | 'updated_by'>>;
export type ItemChange = Partial<Omit<ItemRow, 'id' | 'updated_at' | 'updated_by'>>;

const KINDS = new Set<string>(['today', 'pinned', 'favorite', 'folder']);
const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

const str = (v: unknown, fallback = '') => (typeof v === 'string' ? v : fallback);
const strOrNull = (v: unknown) => (typeof v === 'string' ? v : null);

// Rows are written by other clients, so nothing is taken on trust.
export function parseSpace(raw: unknown): SpaceRow | null {
  if (!raw || typeof raw !== 'object') return null;
  const r = raw as Record<string, unknown>;
  if (typeof r.id !== 'string' || !UUID_RE.test(r.id) || typeof r.position !== 'string') return null;
  return {
    id: r.id,
    name: str(r.name),
    icon: str(r.icon),
    color: str(r.color),
    position: r.position,
    updated_at: strOrNull(r.updated_at),
    updated_by: strOrNull(r.updated_by),
    deleted_at: strOrNull(r.deleted_at),
  };
}

export function parseItem(raw: unknown): ItemRow | null {
  if (!raw || typeof raw !== 'object') return null;
  const r = raw as Record<string, unknown>;
  if (typeof r.id !== 'string' || !UUID_RE.test(r.id)) return null;
  if (typeof r.space_id !== 'string' || typeof r.position !== 'string') return null;
  if (typeof r.kind !== 'string' || !KINDS.has(r.kind)) return null;
  const url = str(r.url);
  if (url ? !/^https?:\/\//i.test(url) : r.kind !== 'folder') return null;
  return {
    id: r.id,
    space_id: r.space_id,
    kind: r.kind as ItemKind,
    parent_id: strOrNull(r.parent_id),
    url,
    title: str(r.title),
    favicon_url: strOrNull(r.favicon_url),
    preview_path: strOrNull(r.preview_path),
    preview_updated_at: strOrNull(r.preview_updated_at),
    position: r.position,
    last_active_at: strOrNull(r.last_active_at),
    updated_at: strOrNull(r.updated_at),
    updated_by: strOrNull(r.updated_by),
    deleted_at: strOrNull(r.deleted_at),
  };
}

// --- Timestamps ---------------------------------------------------------------

const TS_RE = /^(\d{4})-(\d\d)-(\d\d)[T ](\d\d):(\d\d):(\d\d)(?:\.(\d+))?(Z|[+-]\d\d(?::?\d\d)?)?$/i;

// Microseconds since the epoch. PostgREST and Realtime format timestamptz
// differently ("…T…+00:00" vs "… …+00") and both carry microseconds, which
// Date.parse would drop; ordering a row against its own echo needs them.
export function tsMicros(ts: string | null | undefined): number {
  if (!ts) return 0;
  const m = TS_RE.exec(ts.trim());
  if (!m) return (Date.parse(ts) || 0) * 1000;
  const [, y, mo, d, h, mi, s, frac = '', zone = 'Z'] = m;
  let offsetMinutes = 0;
  if (zone.toUpperCase() !== 'Z') {
    const sign = zone[0] === '-' ? -1 : 1;
    const digits = zone.slice(1).replace(':', '');
    offsetMinutes = sign * (Number(digits.slice(0, 2)) * 60 + Number(digits.slice(2, 4) || 0));
  }
  const seconds = Date.UTC(+y, +mo - 1, +d, +h, +mi, +s) / 1000 - offsetMinutes * 60;
  return seconds * 1_000_000 + Number(frac.slice(0, 6).padEnd(6, '0'));
}

export function laterOf(a: string | null, b: string | null): string | null {
  if (!a) return b;
  if (!b) return a;
  return tsMicros(b) > tsMicros(a) ? b : a;
}

// --- Positions ----------------------------------------------------------------

// Bytewise, never locale-aware; equal positions fall back to the id. JS string
// comparison is by UTF-16 unit, which is bytewise for the base-62 alphabet.
export function byPosition(a: { position: string; id: string }, b: { position: string; id: string }) {
  if (a.position !== b.position) return a.position < b.position ? -1 : 1;
  return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
}

// A key strictly between |a| and |b| (either may be null for an open end). A
// malformed key from another client degrades to "after a" or a fresh key
// rather than throwing.
export function keyBetween(a: string | null, b: string | null): string {
  try {
    return generateKeyBetween(a, b !== null && a !== null && a >= b ? null : b);
  } catch {
    try {
      return generateKeyBetween(a, null);
    } catch {
      return generateKeyBetween(null, null);
    }
  }
}

export function keyAtEnd(sorted: { position: string }[]): string {
  return keyBetween(sorted[sorted.length - 1]?.position ?? null, null);
}

export function keyAtStart(sorted: { position: string }[]): string {
  return keyBetween(null, sorted[0]?.position ?? null);
}

// A key right after sorted[index]. Rows that share its position are skipped,
// since nothing fits between two equal keys.
export function keyAfter(sorted: { position: string }[], index: number): string {
  const a = sorted[index].position;
  const next = sorted.slice(index + 1).find((r) => r.position > a);
  return keyBetween(a, next?.position ?? null);
}

export function keyBefore(sorted: { position: string }[], index: number): string {
  const b = sorted[index].position;
  const prev = [...sorted.slice(0, index)].reverse().find((r) => r.position < b);
  return keyBetween(prev?.position ?? null, b);
}

// --- URLs ---------------------------------------------------------------------

// Pages that mean something on another device: http(s), and not this phone's
// own localhost.
export function isShareableUrl(url: string | null | undefined): url is string {
  if (!url || !/^https?:\/\//i.test(url)) return false;
  const host = url.replace(/^https?:\/\//i, '').split(/[/?#]/)[0].replace(/:\d+$/, '').toLowerCase();
  return !(host === 'localhost' || host.endsWith('.localhost') || /^127\./.test(host) || host === '[::1]');
}
