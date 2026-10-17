// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import * as StyleIntelligence from './style_intelligence.js';

type LedgerEntry = StyleIntelligence.StyleStudioLedger.LedgerEntry;
type LedgerStore = StyleIntelligence.StyleStudioLedger.LedgerStore;
type SheetFacts = StyleIntelligence.StyleStudioLedger.SheetFacts;
const {classifyEntry, LEDGER_KEY_PREFIX, LEDGER_MAX_AGE_MS, StudioLedger} = StyleIntelligence.StyleStudioLedger;
const {EMPTY_SHEET_TEXT} = StyleIntelligence.StyleStudioPreview;

/** Host preferences, as a map. `dropWrites` models a store that loses writes. */
class MemoryStore implements LedgerStore {
  readonly values = new Map<string, string>();
  dropWrites = false;

  async read(key: string): Promise<string|null> {
    return this.values.get(key) ?? null;
  }

  async write(key: string, value: string): Promise<void> {
    if (!this.dropWrites) {
      this.values.set(key, value);
    }
  }

  async remove(key: string): Promise<void> {
    if (!this.dropWrites) {
      this.values.delete(key);
    }
  }

  async keys(): Promise<string[]> {
    return [...this.values.keys()];
  }
}

const PREVIEW = `${EMPTY_SHEET_TEXT}#a { color: red; }\n`;

function entry(overrides: Partial<LedgerEntry> = {}): Omit<LedgerEntry, 'updatedAt'> {
  return {sheetId: 'style-sheet-7-3', frameId: 'F', documentBackendNodeId: 2, texts: [PREVIEW], ...overrides};
}

describe('StudioLedger', () => {
  let store: MemoryStore;
  let now: number;
  let ledger: InstanceType<typeof StudioLedger>;

  beforeEach(() => {
    store = new MemoryStore();
    now = 1_000_000;
    ledger = new StudioLedger(store, () => now);
  });

  it('records an entry per page target and reads it back', async () => {
    assert.isTrue(await ledger.record('T1', entry()));
    assert.isTrue(await ledger.record('T2', entry({sheetId: 'style-sheet-7-9'})));
    assert.deepEqual((await ledger.entries('T1')).map(e => e.sheetId), ['style-sheet-7-3']);
    assert.deepEqual((await ledger.entries('T2')).map(e => e.sheetId), ['style-sheet-7-9']);
    assert.deepEqual([...store.values.keys()].sort(), [`${LEDGER_KEY_PREFIX}T1`, `${LEDGER_KEY_PREFIX}T2`]);
  });

  it('replaces an entry for the same sheet, and deduplicates its texts', async () => {
    await ledger.record('T1', entry());
    await ledger.record('T1', entry({texts: [PREVIEW, PREVIEW, 'x']}));
    const entries = await ledger.entries('T1');
    assert.lengthOf(entries, 1);
    assert.deepEqual(entries[0].texts, [PREVIEW, 'x']);
  });

  it('reports a record that did not persist', async () => {
    store.dropWrites = true;
    assert.isFalse(await ledger.record('T1', entry()));
  });

  it('forgets an entry, removing the record once empty, and confirms it', async () => {
    await ledger.record('T1', entry());
    await ledger.record('T1', entry({sheetId: 'style-sheet-7-4'}));
    assert.isTrue(await ledger.forget('T1', 'style-sheet-7-3'));
    assert.deepEqual((await ledger.entries('T1')).map(e => e.sheetId), ['style-sheet-7-4']);
    assert.isTrue(await ledger.forget('T1', 'style-sheet-7-4'));
    assert.isFalse(store.values.has(`${LEDGER_KEY_PREFIX}T1`));
    store.dropWrites = true;
    await ledger.forget('T1', 'nothing');
  });

  it('reports a forget that did not persist', async () => {
    await ledger.record('T1', entry());
    store.dropWrites = true;
    assert.isFalse(await ledger.forget('T1', 'style-sheet-7-3'));
  });

  it('ignores malformed records and records of another target under its key', async () => {
    store.values.set(`${LEDGER_KEY_PREFIX}T1`, '{not json');
    assert.deepEqual(await ledger.entries('T1'), []);
    store.values.set(`${LEDGER_KEY_PREFIX}T1`, JSON.stringify({version: 1, targetId: 'T9', entries: [entry()]}));
    assert.deepEqual(await ledger.entries('T1'), []);
  });

  it('prunes only records older than the maximum age, and leaves other preferences', async () => {
    await ledger.record('OLD', entry());
    now += LEDGER_MAX_AGE_MS + 1;
    await ledger.record('NEW', entry());
    store.values.set('unrelated', 'x');
    await ledger.prune();
    assert.deepEqual([...store.values.keys()].sort(), [`${LEDGER_KEY_PREFIX}NEW`, 'unrelated']);
  });

  it('runs operations one at a time, so concurrent records all land', async () => {
    await Promise.all([1, 2, 3, 4].map(n => ledger.record('T1', entry({sheetId: `style-sheet-7-${n}`}))));
    assert.lengthOf(await ledger.entries('T1'), 4);
  });
});

describe('classifyEntry', () => {
  const recorded: LedgerEntry = {...entry(), texts: ['old', PREVIEW], updatedAt: 0};
  const facts = (overrides: Partial<SheetFacts> = {}): SheetFacts => ({
    reported: true,
    viaInspector: true,
    frameId: 'F',
    ownerBackendNodeId: 2,
    knownDefault: false,
    liveOwned: false,
    text: PREVIEW,
    ...overrides,
  });
  const classify = (overrides: Partial<SheetFacts>): string => classifyEntry(recorded, facts(overrides), EMPTY_SHEET_TEXT);

  it('finds a previous session\'s preview by id, frame, owner document, and unchanged text', () => {
    assert.strictEqual(classify({}), 'orphan');
    assert.strictEqual(classify({text: 'old'}), 'orphan');
  });

  it('treats a sheet that is gone, or holds no preview, as needing nothing', () => {
    assert.strictEqual(classify({reported: false}), 'gone');
    assert.strictEqual(classify({text: EMPTY_SHEET_TEXT}), 'clean');
    assert.strictEqual(classify({text: ''}), 'clean');
  });

  it('never offers a sheet the live engine owns', () => {
    assert.strictEqual(classify({liveOwned: true}), 'live');
  });

  it('never offers the default inspector stylesheet, a non-inspector sheet, or another frame or document\'s', () => {
    assert.strictEqual(classify({knownDefault: true}), 'mismatch');
    assert.strictEqual(classify({viaInspector: false}), 'mismatch');
    assert.strictEqual(classify({frameId: 'G'}), 'mismatch');
    assert.strictEqual(classify({ownerBackendNodeId: 3}), 'mismatch');
  });

  it('never offers a sheet whose text something else changed, even if it looks like Studio\'s', () => {
    assert.strictEqual(classify({text: `${EMPTY_SHEET_TEXT}#b { color: red; }\n`}), 'changed');
  });

  it('decides nothing when the text cannot be read', () => {
    assert.strictEqual(classify({text: null}), 'unknown');
  });
});
