// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Style Studio's ownership ledger: a persisted record of the inspector
 * stylesheets Studio created, so a later DevTools session can offer to remove
 * a preview that a crashed or killed session left applied.
 *
 * Identity is the browser's page target id plus the stylesheet id the renderer
 * issued (`style-sheet-<process>-<counter>`, never reissued within a process),
 * cross-checked against the sheet's frame and owner document. Sheet text only
 * confirms that nothing else changed the sheet since Studio wrote it; it never
 * identifies a sheet on its own. Nothing is ever removed without the user
 * asking, and an entry is deleted only after its sheet is confirmed clean or
 * gone (see style-studio-checkpoint-2a3-findings.md).
 */

/** One stylesheet Studio created. */
export interface LedgerEntry {
  sheetId: string;
  frameId: string;
  /** Backend node id of the document that owns the sheet. */
  documentBackendNodeId: number;
  /**
   * Texts the sheet may hold if only Studio wrote it: what it last wrote, plus
   * the text of a write in flight, recorded before that write is sent.
   */
  texts: string[];
  updatedAt: number;
}

interface LedgerRecord {
  version: 1;
  targetId: string;
  entries: LedgerEntry[];
}

/** Where ledger records persist. Every write must be confirmed by reading it back. */
export interface LedgerStore {
  read(key: string): Promise<string|null>;
  write(key: string, value: string): Promise<void>;
  remove(key: string): Promise<void>;
  keys(): Promise<string[]>;
}

export const LEDGER_KEY_PREFIX = 'avora-style-studio-ledger:';

/** Records older than this belong to a browsing session long gone; they are dropped unread. */
export const LEDGER_MAX_AGE_MS = 30 * 24 * 60 * 60 * 1000;

function keyFor(targetId: string): string {
  return `${LEDGER_KEY_PREFIX}${targetId}`;
}

function parseRecord(text: string|null, targetId: string): LedgerEntry[] {
  if (!text) {
    return [];
  }
  try {
    const record = JSON.parse(text) as LedgerRecord;
    if (record.version !== 1 || record.targetId !== targetId || !Array.isArray(record.entries)) {
      return [];
    }
    return record.entries.filter(
        entry => typeof entry.sheetId === 'string' && typeof entry.frameId === 'string' &&
            typeof entry.documentBackendNodeId === 'number' && Array.isArray(entry.texts));
  } catch {
    return [];
  }
}

/**
 * Reads and writes ledger records, one per page target, one operation at a
 * time. Each write is read back from the store before it counts as done.
 */
export class StudioLedger {
  readonly #store: LedgerStore;
  readonly #now: () => number;
  #queue: Promise<unknown> = Promise.resolve();

  constructor(store: LedgerStore, now: () => number = Date.now) {
    this.#store = store;
    this.#now = now;
  }

  entries(targetId: string): Promise<LedgerEntry[]> {
    return this.#enqueue(async () => parseRecord(await this.#store.read(keyFor(targetId)), targetId));
  }

  /** Records (or replaces) the entry for `entry.sheetId`. Resolves true once it reads back. */
  record(targetId: string, entry: Omit<LedgerEntry, 'updatedAt'>): Promise<boolean> {
    return this.#enqueue(async () => {
      const entries = parseRecord(await this.#store.read(keyFor(targetId)), targetId)
                          .filter(existing => existing.sheetId !== entry.sheetId);
      entries.push({...entry, texts: [...new Set(entry.texts)], updatedAt: this.#now()});
      return await this.#save(targetId, entries);
    });
  }

  /** Deletes the entry for `sheetId`. Resolves true once the store no longer has it. */
  forget(targetId: string, sheetId: string): Promise<boolean> {
    return this.#enqueue(async () => {
      const entries = parseRecord(await this.#store.read(keyFor(targetId)), targetId);
      const remaining = entries.filter(entry => entry.sheetId !== sheetId);
      if (remaining.length === entries.length) {
        return true;
      }
      return await this.#save(targetId, remaining);
    });
  }

  /** Drops whole records whose newest entry is older than LEDGER_MAX_AGE_MS. */
  prune(): Promise<void> {
    return this.#enqueue(async () => {
      for (const key of await this.#store.keys()) {
        if (!key.startsWith(LEDGER_KEY_PREFIX)) {
          continue;
        }
        const entries = parseRecord(await this.#store.read(key), key.slice(LEDGER_KEY_PREFIX.length));
        const newest = Math.max(0, ...entries.map(entry => entry.updatedAt));
        if (this.#now() - newest > LEDGER_MAX_AGE_MS) {
          await this.#store.remove(key);
        }
      }
    });
  }

  async #save(targetId: string, entries: LedgerEntry[]): Promise<boolean> {
    const key = keyFor(targetId);
    try {
      if (!entries.length) {
        await this.#store.remove(key);
        return await this.#store.read(key) === null;
      }
      const text = JSON.stringify({version: 1, targetId, entries} satisfies LedgerRecord);
      await this.#store.write(key, text);
      return await this.#store.read(key) === text;
    } catch {
      return false;
    }
  }

  #enqueue<T>(operation: () => Promise<T>): Promise<T> {
    const run = this.#queue.then(operation, operation);
    this.#queue = run.catch(() => undefined);
    return run;
  }
}

/** What DevTools reports now about a ledger entry's sheet. */
export interface SheetFacts {
  /** The header is reported by the target's CSS model. */
  reported: boolean;
  viaInspector: boolean;
  frameId: string|null;
  ownerBackendNodeId: number|null;
  /** One of the frame's default inspector stylesheets, as far as DevTools knows. */
  knownDefault: boolean;
  /** A sheet the live preview engine owns: not an orphan. */
  liveOwned: boolean;
  text: string|null;
}

export type EntryVerdict =
    /** A previous session's preview, still applied: offer Remove and Keep. */
    'orphan'|
    /** The sheet holds no preview: drop the entry without writing. */
    'clean'|
    /** The sheet is gone with its document: drop the entry without writing. */
    'gone'|
    /** The live engine owns it: leave the entry alone. */
    'live'|
    /** Something else changed the sheet since Studio wrote it: never touch it. */
    'changed'|
    /** The reported sheet is not the one recorded: never touch it, drop the entry. */
    'mismatch'|
    /** Its text could not be read now: decide later. */
    'unknown';

/**
 * Decides what an entry means now. Only `orphan` may lead to a write, and only
 * after the user chooses Remove and the entry is checked again.
 */
export function classifyEntry(entry: LedgerEntry, facts: SheetFacts, emptyText: string): EntryVerdict {
  if (!facts.reported) {
    return 'gone';
  }
  if (facts.liveOwned) {
    return 'live';
  }
  if (!facts.viaInspector || facts.knownDefault || facts.frameId !== entry.frameId ||
      facts.ownerBackendNodeId !== entry.documentBackendNodeId) {
    return 'mismatch';
  }
  if (facts.text === null) {
    return 'unknown';
  }
  if (facts.text === '' || facts.text === emptyText) {
    return 'clean';
  }
  return entry.texts.includes(facts.text) ? 'orphan' : 'changed';
}
