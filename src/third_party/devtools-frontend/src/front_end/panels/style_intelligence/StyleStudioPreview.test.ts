// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {renderElementIntoDOM} from '../../testing/DOMHelpers.js';

import * as StyleIntelligence from './style_intelligence.js';

type CreateSheetFailure = StyleIntelligence.StyleStudioPreview.CreateSheetFailure;
type PathStep = StyleIntelligence.StyleStudioPreview.PathStep;
type PreviewHost = StyleIntelligence.StyleStudioPreview.PreviewHost;
type PreviewPropertyEffect = StyleIntelligence.StyleStudioPreview.PreviewPropertyEffect;
type PreviewTargetRef = StyleIntelligence.StyleStudioPreview.PreviewTargetRef;
type RuleSelectorResult = StyleIntelligence.StyleStudioPreview.RuleSelectorResult;
const {
  buildPreviewRule,
  parseDeclarations,
  PREVIEW_MARKER,
  previewSheetText,
  singleElementSelector,
  splitDeclarations,
  StylePreviewEngine,
} = StyleIntelligence.StyleStudioPreview;

describe('StyleStudioPreview parsing', () => {
  it('splits declarations on top-level semicolons only', () => {
    assert.deepEqual(
        splitDeclarations('color: red; background: url("a;b.png"); --x: f(1;2); content: \';\''),
        ['color: red', 'background: url("a;b.png")', '--x: f(1;2)', 'content: \';\'']);
  });

  it('returns the browser serialization of valid declarations', () => {
    const parsed = parseDeclarations('color:red;  margin: 4px');
    assert.strictEqual(parsed?.text, 'color: red; margin: 4px;');
    assert.deepEqual(parsed?.invalid, []);
  });

  it('keeps author !important and custom properties, and adds no !important', () => {
    assert.strictEqual(parseDeclarations('color: red !important; --gap: 4px')?.text, 'color: red !important; --gap: 4px;');
    assert.notInclude(parseDeclarations('color: red')?.text ?? '', 'important');
  });

  it('reports declarations the browser drops', () => {
    const parsed = parseDeclarations('color: red; colr: blue; width: banana');
    assert.strictEqual(parsed?.text, 'color: red;');
    assert.deepEqual(parsed?.invalid, ['colr: blue', 'width: banana']);
  });

  it('rejects input with no valid declaration, or that would leave the rule', () => {
    for (const css of ['', '   ', 'colr: blue', 'color: red } body { color: blue', 'color: red; .a { color: blue }',
                       '@media print { a { color: red } }']) {
      assert.isNull(parseDeclarations(css), css);
    }
  });

  it('builds a rule only when it parses back as one plain rule', () => {
    const declarations = parseDeclarations('color: red');
    assert.isNotNull(declarations);
    if (!declarations) {
      return;
    }
    assert.strictEqual(buildPreviewRule('.card', declarations), '.card { color: red; }');
    assert.isNull(buildPreviewRule('.card {} body', declarations));
    assert.isNull(buildPreviewRule('', declarations));
  });

  it('labels sheet text for people reading it in the Styles pane', () => {
    assert.strictEqual(previewSheetText('.card { color: red; }'), `${PREVIEW_MARKER}\n.card { color: red; }\n`);
  });

  describe('single-element selector in a real cascade', () => {
    let container: HTMLElement;
    let style: HTMLStyleElement;

    beforeEach(() => {
      container = document.createElement('div');
      container.innerHTML = '<section><div class="card"></div><div class="card" id="target"></div>' +
          '<div class="card"></div><div class="card"></div></section><section><p class="card">p</p></section>';
      renderElementIntoDOM(container);
      style = document.createElement('style');
      document.head.append(style);
    });

    afterEach(() => {
      container.remove();
      style.remove();
    });

    /** What the host computes for `element`: its path from `:root`, with sibling counts. */
    function pathOf(element: Element): PathStep[] {
      const path: PathStep[] = [];
      let current: Element = element;
      while (current.parentElement) {
        const siblings = [...current.parentElement.children];
        path.unshift({localName: current.localName, position: siblings.indexOf(current) + 1, siblings: siblings.length});
        current = current.parentElement;
      }
      return path;
    }

    function selectorFor(element: Element, authoring = '.card'): string {
      return singleElementSelector(
          pathOf(element), {id: element.getAttribute('id'), className: element.getAttribute('class')}, authoring);
    }

    const target = (): Element => container.querySelector('#target') as Element;
    const matches = (selector: string): Element[] => [...document.querySelectorAll(selector)];
    const card = (attributes = 'class="card"'): Element => {
      const template = document.createElement('template');
      template.innerHTML = `<div ${attributes}></div>`;
      return template.content.firstElementChild as Element;
    };

    it('matches exactly the one element', () => {
      assert.deepEqual(matches(selectorFor(target())), [target()]);
    });

    it('carries the authoring selector\'s specificity, no more and no less', () => {
      const preview = selectorFor(target());
      // Same specificity as `.card`: the later rule wins.
      style.textContent = `.card { color: rgb(1, 1, 1); } ${preview} { color: rgb(2, 2, 2); }`;
      assert.strictEqual(getComputedStyle(target()).color, 'rgb(2, 2, 2)');
      // An id rule is more specific than `.card`, wherever it is.
      style.textContent = `#target { color: rgb(3, 3, 3); } ${preview} { color: rgb(2, 2, 2); }`;
      assert.strictEqual(getComputedStyle(target()).color, 'rgb(3, 3, 3)');
      // The siblings that also match `.card` are untouched.
      const sibling = container.querySelector('.card') as Element;
      assert.notStrictEqual(getComputedStyle(sibling).color, 'rgb(2, 2, 2)');
    });

    it('matches nothing once an element sibling is inserted before or after the target', () => {
      const selector = selectorFor(target());
      target().before(card());
      assert.deepEqual(matches(selector), []);
      target().previousElementSibling?.remove();
      assert.deepEqual(matches(selector), [target()]);
      target().parentElement?.append(card());
      assert.deepEqual(matches(selector), []);
    });

    it('matches nothing once an earlier or later sibling is removed', () => {
      const selector = selectorFor(target());
      target().previousElementSibling?.remove();
      assert.deepEqual(matches(selector), []);
      const again = selectorFor(target());
      target().parentElement?.lastElementChild?.remove();
      assert.deepEqual(matches(again), []);
    });

    it('matches nothing once an ancestor gains or loses a sibling', () => {
      const selector = selectorFor(target());
      const section = target().parentElement as Element;
      section.before(document.createElement('section'));
      assert.deepEqual(matches(selector), []);
      section.previousElementSibling?.remove();
      section.parentElement?.lastElementChild?.remove();
      assert.deepEqual(matches(selector), []);
    });

    it('is unaffected by text and comment nodes', () => {
      const selector = selectorFor(target());
      target().before(document.createTextNode('text'), document.createComment('c'));
      assert.deepEqual(matches(selector), [target()]);
    });

    it('does not match a reordered sibling with a different id or class', () => {
      const selector = selectorFor(target());
      const first = target().parentElement?.firstElementChild as Element;
      // Same count, so the positions are taken by elements that are not the target.
      target().parentElement?.insertBefore(target(), first);
      assert.deepEqual(matches(selector), []);
    });

    it('matches only the target after its class or id changes, never another element', () => {
      const selector = selectorFor(target());
      target().setAttribute('class', 'card other');
      assert.deepEqual(matches(selector), []);
      target().setAttribute('class', 'card');
      target().removeAttribute('id');
      assert.deepEqual(matches(selector), []);
    });

    it('escapes attribute values exactly', () => {
      const odd = card('class="card a&quot;b\\c" id="x y"');
      target().replaceWith(odd);
      const selector = selectorFor(odd);
      assert.deepEqual(matches(selector), [odd]);
    });

    it('can still match an identical element that takes exactly the target\'s place (the known limit)', () => {
      // Same tag, id, and class at the same path: no selector can tell them apart.
      const selector = selectorFor(target());
      const copy = target().cloneNode(true) as Element;
      target().replaceWith(copy);
      assert.deepEqual(matches(selector), [copy]);
    });
  });
});

const REF: PreviewTargetRef = {
  targetId: 't1',
  frameId: 'f1',
  documentKey: 't1:1',
  scope: 'node',
  nodeId: 10,
  selector: '.card',
};

const EMPTY = `${PREVIEW_MARKER}\n`;
const nodeRule = (declarations: string, position = 2): string =>
    previewSheetText(`:where(:root > :nth-child(${position})):is(.card) { ${declarations} }`);

/** A promise the test settles by hand, to order asynchronous steps exactly. */
class Gate {
  readonly promise: Promise<void>;
  open: () => void = () => {};
  constructor() {
    this.promise = new Promise(resolve => {
      this.open = resolve;
    });
  }
}

/** Lets every pending promise continuation run, until the engine waits on a gate. */
function settle(): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, 0));
}

/**
 * The inspector stylesheets of one target, as the protocol would hold them.
 * Each sheet records the document that owns it; `defaults` are default
 * inspector stylesheets, which belong to the Styles pane.
 */
class FakeHost implements PreviewHost {
  readonly sheets = new Map<string, string>();
  readonly sheetDocument = new Map<string, string>();
  readonly defaults = new Set<string>();
  /** Ids this host created for Studio. */
  readonly createdIds: string[] = [];
  /** Every sheet id a write was attempted on, in order. */
  readonly writeLog: Array<[string, string]> = [];
  created = 0;
  writes = 0;
  failWrites = 0;
  createFailure: CreateSheetFailure|null = null;
  /** Held before a sheet is created, to order creation against other events. */
  createGate: Gate|null = null;
  /** Held before a write reaches the sheet. */
  writeGate: Gate|null = null;
  resolveCalls = 0;
  resolve: (ref: PreviewTargetRef) => Promise<RuleSelectorResult> =
      async ref => ({ok: true, ruleSelector: ref.scope === 'node' ? `:where(:root > :nth-child(2)):is(${ref.selector})` : ref.selector});

  resolveRuleSelector(ref: PreviewTargetRef): Promise<RuleSelectorResult> {
    this.resolveCalls++;
    return this.resolve(ref);
  }

  async createOwnedSheet(ref: PreviewTargetRef):
      Promise<{ok: true, sheetId: string}|{ok: false, reason: CreateSheetFailure, detail: string}> {
    if (this.createGate) {
      await this.createGate.promise;
    }
    if (this.createFailure) {
      return {ok: false, reason: this.createFailure, detail: this.createFailure};
    }
    const id = `sheet-${++this.created}`;
    this.sheets.set(id, '');
    this.sheetDocument.set(id, ref.documentKey);
    this.createdIds.push(id);
    return {ok: true, sheetId: id};
  }

  sheetExists(sheetId: string): boolean {
    return this.sheets.has(sheetId);
  }

  verifyOwnership(ref: PreviewTargetRef, sheetId: string): string|null {
    if (this.sheetDocument.get(sheetId) !== ref.documentKey) {
      return 'different document';
    }
    return this.defaults.has(sheetId) ? 'default' : null;
  }

  async getText(sheetId: string): Promise<string|null> {
    return this.sheets.get(sheetId) ?? null;
  }

  async setText(sheetId: string, text: string): Promise<boolean> {
    this.writeLog.push([sheetId, text]);
    this.timeline.push(`write ${sheetId} ${text === EMPTY ? 'empty' : 'preview'}`);
    if (this.writeGate) {
      await this.writeGate.promise;
    }
    if (!this.sheets.has(sheetId)) {
      return false;
    }
    if (this.failWrites > 0) {
      this.failWrites--;
      return false;
    }
    this.writes++;
    this.sheets.set(sheetId, text);
    return true;
  }

  readonly sentNow: Array<[string, string]> = [];
  setTextNow(sheetId: string, text: string): void {
    this.sentNow.push([sheetId, text]);
  }

  async inspectEffect(): Promise<PreviewPropertyEffect[]|null> {
    return [];
  }

  /** The persisted ownership record: sheet id to the texts it may hold. */
  readonly records = new Map<string, string[]>();
  /** Records and writes, in the order they happened. */
  readonly timeline: string[] = [];
  failRecords = false;

  async recordOwnership(_ref: PreviewTargetRef, sheetId: string, texts: string[]): Promise<boolean> {
    this.timeline.push(`record ${sheetId}`);
    if (this.failRecords) {
      return false;
    }
    this.records.set(sheetId, texts);
    return true;
  }

  async forgetOwnership(_ref: PreviewTargetRef, sheetId: string): Promise<boolean> {
    this.timeline.push(`forget ${sheetId}`);
    this.records.delete(sheetId);
    return true;
  }

  /** The document goes away, and its sheets with it. */
  destroyDocument(documentKey: string, engine: StylePreviewEngine): void {
    for (const [id, document] of this.sheetDocument) {
      if (document === documentKey && this.sheets.delete(id)) {
        engine.sheetRemoved(id);
      }
    }
  }
}

describe('StylePreviewEngine', () => {
  let host: FakeHost;
  let hostAvailable: boolean;
  let engine: StylePreviewEngine;

  beforeEach(() => {
    host = new FakeHost();
    hostAvailable = true;
    engine = new StylePreviewEngine(() => hostAvailable ? host : null);
    // The Styles pane's default inspector stylesheet, with the user's rules.
    host.sheets.set('default', '/* mine */\n.user { color: green; }\n');
    host.sheetDocument.set('default', REF.documentKey);
    host.defaults.add('default');
  });

  afterEach(async () => {
    host.writeGate = null;
    host.createGate = null;
    await engine.dispose();
    // Whatever happened, Studio wrote only sheets it created, and never the user's.
    for (const [id] of host.writeLog) {
      assert.include(host.createdIds, id, `wrote ${id}, which Studio did not create`);
    }
    assert.strictEqual(host.sheets.get('default') ?? null, host.sheets.has('default') ?
                           '/* mine */\n.user { color: green; }\n' :
                           null);
  });

  function studioSheetText(): string|undefined {
    assert.lengthOf(host.createdIds, 1);
    return host.sheets.get(host.createdIds[0]);
  }

  describe('previews', () => {
    it('applies, replaces whole, and reverts in one owned sheet', async () => {
      let result = await engine.apply(REF, 'color: red');
      assert.isTrue(result.ok);
      assert.strictEqual(studioSheetText(), nodeRule('color: red;'));

      result = await engine.replace(REF, 'background: blue');
      assert.isTrue(result.ok);
      // Replaced, not appended: the old declaration is gone.
      assert.strictEqual(studioSheetText(), nodeRule('background: blue;'));

      result = await engine.revert();
      assert.isTrue(result.ok);
      assert.deepEqual(engine.state(), {status: 'none'});
      assert.strictEqual(studioSheetText(), EMPTY);
    });

    it('reuses its sheet across many replacements and reverts', async () => {
      // Ends on i = 4: applied, not reverted.
      for (let i = 0; i < 5; i++) {
        assert.isTrue((await engine.apply(REF, `width: ${i}px`)).ok);
        if (i % 2) {
          assert.isTrue((await engine.revert()).ok);
        }
      }
      assert.strictEqual(host.created, 1);
      assert.strictEqual(studioSheetText(), nodeRule('width: 4px;'));
    });

    it('uses the selector itself for all matches', async () => {
      await engine.apply({...REF, scope: 'all-matches', nodeId: null}, 'color: red');
      assert.strictEqual(studioSheetText(), previewSheetText('.card { color: red; }'));
    });

    it('rejects invalid CSS without creating or writing a sheet', async () => {
      const result = await engine.apply(REF, 'color: red } body { display: none');
      assert.isFalse(result.ok);
      assert.strictEqual(!result.ok && result.reason, 'invalid-css');
      assert.strictEqual(host.created, 0);
      assert.deepEqual(engine.state(), {status: 'none'});
    });

    it('keeps the last good preview when a replacement fails to write', async () => {
      await engine.apply(REF, 'color: red');
      const good = studioSheetText();
      host.failWrites = 1;
      const result = await engine.apply(REF, 'color: blue');
      assert.isFalse(result.ok);
      assert.strictEqual(!result.ok && result.reason, 'write-failed');
      assert.strictEqual(studioSheetText(), good);
      assert.strictEqual(engine.state().status, 'applied');
    });

    it('keeps tracking a sheet a failed restore left unknown, and still empties it on revert', async () => {
      await engine.apply(REF, 'color: red');
      host.failWrites = 2;
      const result = await engine.apply(REF, 'color: blue');
      assert.isFalse(result.ok);
      assert.strictEqual(engine.state().status, 'conflict');
      assert.lengthOf(engine.ownedSheetIds(), 1);
      // Nothing writes over it while its contents are unknown.
      const blocked = await engine.apply(REF, 'color: green');
      assert.strictEqual(!blocked.ok && blocked.reason, 'external-modification');
      assert.isTrue((await engine.revert()).ok);
      assert.strictEqual(studioSheetText(), EMPTY);
    });

    it('refuses a target that fails its check, without writing', async () => {
      host.resolve = async () => ({ok: false, reason: 'target-detached', detail: 'gone'});
      const result = await engine.apply(REF, 'color: red');
      assert.strictEqual(!result.ok && result.reason, 'target-detached');
      assert.strictEqual(host.writes, 0);
    });

    it('detects an external change to an applied preview and will not write over it', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      host.sheets.set(id, `${host.sheets.get(id)}\n.user { color: green; }`);
      await engine.sheetChanged(id);
      assert.strictEqual(engine.state().status, 'conflict');

      const result = await engine.apply(REF, 'color: blue');
      assert.strictEqual(!result.ok && result.reason, 'external-modification');
      assert.include(host.sheets.get(id), '.user');

      assert.isTrue((await engine.revert()).ok);
      assert.deepEqual(engine.state(), {status: 'none'});
    });

    it('leaves an idle sheet that was changed elsewhere and uses a new one', async () => {
      await engine.apply(REF, 'color: red');
      await engine.revert();
      const [id] = engine.ownedSheetIds();
      host.sheets.set(id, '.someone-else { color: green; }');
      const result = await engine.apply(REF, 'color: blue');
      assert.isTrue(result.ok);
      assert.strictEqual(host.sheets.get(id), '.someone-else { color: green; }');
      assert.strictEqual(host.created, 2);
      assert.deepEqual(engine.ownedSheetIds(), ['sheet-2']);
    });

    it('forgets a preview whose sheet left with its document', async () => {
      await engine.apply(REF, 'color: red');
      host.destroyDocument(REF.documentKey, engine);
      assert.deepEqual(engine.state(), {status: 'none'});
      assert.deepEqual(engine.ownedSheetIds(), []);
    });

    it('empties the previous document\'s sheet when previewing in another document', async () => {
      await engine.apply(REF, 'color: red');
      const [first] = engine.ownedSheetIds();
      await engine.apply({...REF, documentKey: 't1:2', nodeId: 20}, 'color: blue');
      assert.strictEqual(host.sheets.get(first), EMPTY);
      assert.lengthOf(engine.ownedSheetIds(), 2);
    });

    it('rewrites the same element\'s rule when it moves, and reverts once it cannot be resolved', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      host.resolve = async ref => ({ok: true, ruleSelector: `:where(:root > :nth-child(3)):is(${ref.selector})`});
      assert.isTrue((await engine.revalidate()).ok);
      assert.strictEqual(host.sheets.get(id), nodeRule('color: red;', 3));
      assert.strictEqual(engine.state().status, 'applied');

      host.resolve = async () => ({ok: false, reason: 'not-unique', detail: 'moved'});
      const result = await engine.revalidate();
      assert.strictEqual(!result.ok && result.reason, 'not-unique');
      assert.deepEqual(engine.state(), {status: 'none'});
      assert.strictEqual(host.sheets.get(id), EMPTY);
    });

    it('reports state only for the reference it was applied to', async () => {
      await engine.apply(REF, 'color: red');
      assert.strictEqual(engine.stateFor(REF).status, 'applied');
      assert.strictEqual(engine.stateFor({...REF, nodeId: 11}).status, 'none');
    });
  });

  describe('ownership', () => {
    it('never creates, reads as its own, or writes the default inspector stylesheet', async () => {
      await engine.apply(REF, 'color: red');
      await engine.replace(REF, 'color: blue');
      await engine.revert();
      await engine.apply({...REF, scope: 'all-matches', nodeId: null}, 'color: red');
      engine.releaseNow();
      assert.notInclude(engine.ownedSheetIds(), 'default');
      assert.isFalse(host.sentNow.some(([id]) => id === 'default'));
      // afterEach checks the default's text and every write.
    });

    it('refuses a sheet that fails the ownership check at creation, without writing', async () => {
      host.createFailure = 'ownership-unsafe';
      const result = await engine.apply(REF, 'color: red');
      assert.strictEqual(!result.ok && result.reason, 'ownership-unsafe');
      assert.lengthOf(host.writeLog, 0);
      assert.deepEqual(engine.ownedSheetIds(), []);
    });

    it('stops using, and never writes again, a sheet that later fails the ownership check', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      host.defaults.add(id);
      const writes = host.writeLog.length;
      const later = await engine.apply(REF, 'color: blue');
      assert.strictEqual(!later.ok && later.reason, 'ownership-unsafe');
      assert.isTrue((await engine.revert()).ok);
      engine.releaseNow();
      assert.lengthOf(host.writeLog, writes);
      assert.deepEqual(host.sentNow, []);
      assert.deepEqual(engine.ownedSheetIds(), []);
      host.defaults.delete(id);
    });

    it('checks ownership immediately before reverting, and does not write a sheet that fails it', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      host.sheetDocument.set(id, 't1:other');
      const writes = host.writeLog.length;
      const result = await engine.revert();
      assert.strictEqual(!result.ok && result.reason, 'write-failed');
      assert.lengthOf(host.writeLog, writes);
      assert.deepEqual(engine.state(), {status: 'none'});
      host.sheetDocument.set(id, REF.documentKey);
    });

    it('reports a sheet created in a document that replaced the target\'s', async () => {
      host.createFailure = 'target-detached';
      const result = await engine.apply(REF, 'color: red');
      assert.strictEqual(!result.ok && result.reason, 'target-detached');
      assert.lengthOf(host.writeLog, 0);
    });
  });

  describe('ownership record', () => {
    it('records ownership before any preview text is written, and narrows it after', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      assert.deepEqual(host.timeline, [`record ${id}`, `write ${id} preview`, `record ${id}`]);
      assert.deepEqual(host.records.get(id), [nodeRule('color: red;')]);
    });

    it('records both texts while a replacement is in flight', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      host.writeGate = new Gate();
      const pending = engine.replace(REF, 'color: blue');
      await settle();
      assert.deepEqual(host.records.get(id), [nodeRule('color: red;'), nodeRule('color: blue;')]);
      host.writeGate.open();
      host.writeGate = null;
      assert.isTrue((await pending).ok);
      assert.deepEqual(host.records.get(id), [nodeRule('color: blue;')]);
    });

    it('writes nothing when the record cannot be saved', async () => {
      host.failRecords = true;
      const result = await engine.apply(REF, 'color: red');
      assert.strictEqual(!result.ok && result.reason, 'record-failed');
      assert.lengthOf(host.writeLog, 0);
      assert.deepEqual(engine.state(), {status: 'none'});
    });

    it('forgets the record only after the sheet is confirmed empty', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      host.failWrites = 1;
      assert.isFalse((await engine.revert()).ok);
      assert.isTrue(host.records.has(id));
      // Still on the page, so still applied: Revert can be tried again.
      assert.strictEqual(engine.state().status, 'applied');
      await engine.apply(REF, 'color: red');
      assert.isTrue((await engine.revert()).ok);
      assert.isFalse(host.records.has(id));
      assert.deepEqual(host.timeline.slice(-2), [`write ${id} empty`, `forget ${id}`]);
    });

    it('forgets the record of a sheet that left with its document', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      host.destroyDocument(REF.documentKey, engine);
      await settle();
      assert.isFalse(host.records.has(id));
    });

    it('keeps the record when DevTools closes, since the release cannot be confirmed', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      engine.releaseNow();
      assert.isTrue(host.records.has(id));
    });
  });

  describe('asynchronous ordering', () => {
    it('apply then an immediate revert leaves no preview', async () => {
      const applied = engine.apply(REF, 'color: red');
      const reverted = engine.revert();
      assert.isTrue((await applied).ok);
      assert.isTrue((await reverted).ok);
      assert.deepEqual(engine.state(), {status: 'none'});
      assert.strictEqual(studioSheetText(), EMPTY);
    });

    it('a target change while the target is being checked stops the apply before it writes', async () => {
      const gate = new Gate();
      host.resolve = async ref => {
        await gate.promise;
        return {ok: true, ruleSelector: ref.selector};
      };
      const pending = engine.apply(REF, 'color: red');
      await settle();
      // No preview is applied yet; the target change must still stop this apply.
      const changed = engine.targetChanged();
      gate.open();
      const result = await pending;
      await changed;
      assert.strictEqual(!result.ok && result.reason, 'superseded');
      assert.lengthOf(host.writeLog, 0);
      assert.deepEqual(engine.state(), {status: 'none'});
    });

    it('a target change while the write is in flight takes the write back', async () => {
      host.writeGate = new Gate();
      const pending = engine.apply(REF, 'color: red');
      await settle();
      assert.lengthOf(host.writeLog, 1);
      const changed = engine.targetChanged();
      host.writeGate.open();
      host.writeGate = null;
      const result = await pending;
      assert.strictEqual(!result.ok && result.reason, 'superseded');
      assert.deepEqual(engine.state(), {status: 'none'});
      assert.strictEqual(studioSheetText(), EMPTY);
      assert.isTrue((await changed).ok);
    });

    it('replace then navigation during the write: nothing is restored, and the new document is untouched', async () => {
      await engine.apply(REF, 'color: red');
      host.writeGate = new Gate();
      const pending = engine.replace(REF, 'color: blue');
      await settle();
      host.destroyDocument(REF.documentKey, engine);
      void engine.targetChanged();
      host.writeGate.open();
      host.writeGate = null;
      const result = await pending;
      assert.strictEqual(!result.ok && result.reason, 'target-detached');
      assert.deepEqual(engine.state(), {status: 'none'});
      assert.deepEqual(engine.ownedSheetIds(), []);
      // The navigated-to document's own sheets are never written.
      host.sheets.set('new-default', '');
      host.sheetDocument.set('new-default', 't1:2');
      await engine.revert();
      assert.strictEqual(host.sheets.get('new-default'), '');
    });

    it('rapid replacements run in order, write one sheet, and the last one wins', async () => {
      const results = await Promise.all(Array.from({length: 10}, (_, i) => engine.replace(REF, `width: ${i}px`)));
      assert.isTrue(results.every(result => result.ok));
      assert.strictEqual(host.created, 1);
      assert.strictEqual(studioSheetText(), nodeRule('width: 9px;'));
    });

    it('a sheet created after the operation was cancelled is never written', async () => {
      host.createGate = new Gate();
      const pending = engine.apply(REF, 'color: red');
      await settle();
      void engine.targetChanged();
      host.createGate.open();
      host.createGate = null;
      const result = await pending;
      assert.strictEqual(!result.ok && result.reason, 'superseded');
      assert.lengthOf(host.writeLog, 0);
      assert.strictEqual(studioSheetText(), '');
      assert.deepEqual(engine.state(), {status: 'none'});
    });

    it('frame destruction during a write leaves no tracked preview', async () => {
      host.writeGate = new Gate();
      const pending = engine.apply(REF, 'color: red');
      await settle();
      host.destroyDocument(REF.documentKey, engine);
      hostAvailable = false;
      host.writeGate.open();
      host.writeGate = null;
      const result = await pending;
      assert.strictEqual(!result.ok && result.reason, 'target-detached');
      assert.deepEqual(engine.state(), {status: 'none'});
      assert.deepEqual(engine.ownedSheetIds(), []);
    });

    it('disposal during an in-flight apply takes it back, and refuses everything after', async () => {
      host.writeGate = new Gate();
      const pending = engine.apply(REF, 'color: red');
      const queued = engine.apply(REF, 'color: blue');
      await settle();
      const disposed = engine.dispose();
      host.writeGate.open();
      host.writeGate = null;
      const result = await pending;
      assert.strictEqual(!result.ok && result.reason, 'disposed');
      const queuedResult = await queued;
      assert.strictEqual(!queuedResult.ok && queuedResult.reason, 'disposed');
      assert.isTrue((await disposed).ok);
      assert.strictEqual(studioSheetText(), EMPTY);
      const later = await engine.apply(REF, 'color: green');
      assert.strictEqual(!later.ok && later.reason, 'disposed');
    });

    it('an old operation finishing after a new one started never overwrites the newer target', async () => {
      const gate = new Gate();
      host.resolve = async ref => {
        if (ref.nodeId === 10) {
          await gate.promise;
        }
        return {ok: true, ruleSelector: `#n${ref.nodeId}`};
      };
      const old = engine.apply(REF, 'color: red');
      await settle();
      void engine.targetChanged();
      const newer = engine.apply({...REF, nodeId: 11}, 'color: blue');
      gate.open();
      const oldResult = await old;
      const newerResult = await newer;
      assert.strictEqual(!oldResult.ok && oldResult.reason, 'superseded');
      assert.isTrue(newerResult.ok);
      assert.strictEqual(studioSheetText(), previewSheetText('#n11 { color: blue; }'));
      assert.strictEqual(engine.stateFor({...REF, nodeId: 11}).status, 'applied');
    });

    it('re-resolves the target immediately before writing, and does not write if it moved', async () => {
      let calls = 0;
      host.resolve = async ref => ({ok: true, ruleSelector: `:where(:root > :nth-child(${++calls})):is(${ref.selector})`});
      const result = await engine.apply(REF, 'color: red');
      assert.strictEqual(!result.ok && result.reason, 'superseded');
      assert.lengthOf(host.writeLog, 0);
      assert.strictEqual(calls, 2);
    });

    it('does not write if the target stopped resolving just before the write', async () => {
      let calls = 0;
      host.resolve = async ref => ++calls === 1 ? {ok: true, ruleSelector: ref.selector} :
                                                  {ok: false, reason: 'target-detached', detail: 'removed'};
      const result = await engine.apply(REF, 'color: red');
      assert.strictEqual(!result.ok && result.reason, 'target-detached');
      assert.lengthOf(host.writeLog, 0);
    });

    it('a revalidation queued before a target change does not rewrite the old target', async () => {
      await engine.apply(REF, 'color: red');
      host.resolve = async ref => ({ok: true, ruleSelector: `:where(:root > :nth-child(5)):is(${ref.selector})`});
      const revalidated = engine.revalidate();
      void engine.targetChanged();
      const result = await revalidated;
      await settle();
      assert.isFalse(result.ok);
      assert.deepEqual(engine.state(), {status: 'none'});
      assert.strictEqual(studioSheetText(), EMPTY);
    });
  });

  describe('shutdown', () => {
    it('reverts on dispose and refuses afterwards', async () => {
      await engine.apply(REF, 'color: red');
      await engine.dispose();
      assert.strictEqual(studioSheetText(), EMPTY);
      const result = await engine.apply(REF, 'color: blue');
      assert.strictEqual(!result.ok && result.reason, 'disposed');
    });

    it('sends emptying writes immediately while DevTools closes, without waiting', async () => {
      await engine.apply(REF, 'color: red');
      const [id] = engine.ownedSheetIds();
      engine.releaseNow();
      assert.deepEqual(host.sentNow, [[id, EMPTY]]);
      assert.deepEqual(engine.state(), {status: 'none'});
      assert.deepEqual(engine.ownedSheetIds(), []);
      const result = await engine.apply(REF, 'color: blue');
      assert.strictEqual(!result.ok && result.reason, 'disposed');
    });

    it('sends nothing on close for sheets with no preview', async () => {
      await engine.apply(REF, 'color: red');
      await engine.revert();
      engine.releaseNow();
      assert.deepEqual(host.sentNow, []);
    });
  });
});
