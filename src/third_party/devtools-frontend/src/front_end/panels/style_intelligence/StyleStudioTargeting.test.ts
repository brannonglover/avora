// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import * as UI from '../../ui/legacy/legacy.js';

import {bindStudioToSelection} from './StyleStudioDom.js';
import type {StudioNode} from './StyleStudioTarget.js';
import {
  DOM_CHANGE_RESOLVE_DELAY,
  SELECTOR_RESOLVE_DELAY,
  type SelectorQueryResult,
  type StudioSelection,
  StyleStudioTargeting,
} from './StyleStudioTargeting.js';

/** Stands in for SDK.DOMModel.DOMNode as the Elements selection flavor. */
class FakeElement {
  /** False once the element is removed, replaced, or moved; its id never comes back. */
  attached = true;

  constructor(readonly studio: StudioNode, readonly document: FakeDocument) {
  }
}

/** A document that answers querySelectorAll from a table, like DOM.querySelectorAll would. */
class FakeDocument {
  readonly queries: string[] = [];
  readonly listeners = new Set<() => void>();
  results = new Map<string, SelectorQueryResult>();
  /** When set, queries wait until the test releases them. */
  held: Array<() => void>|null = null;
  /** False once the document is replaced by a navigation or its frame is destroyed. */
  attached = true;

  constructor(readonly key: string) {
  }

  element(nodeId: number, overrides: Partial<StudioNode> = {}): FakeElement {
    return new FakeElement(
        {nodeId, label: `div#n${nodeId}`, documentKey: this.key, frameId: 'frame', unsupported: null, ...overrides},
        this);
  }

  async query(selector: string): Promise<SelectorQueryResult> {
    this.queries.push(selector);
    // The answer reflects the DOM when the query ran, not when it is released.
    const result = this.results.get(selector) ?? {nodeIds: []};
    if (this.held) {
      await new Promise<void>(resolve => this.held?.push(resolve));
    }
    return result;
  }

  mutate(): void {
    for (const listener of [...this.listeners]) {
      listener();
    }
  }

  /** Removes an element, like DOM.childNodeRemoved; DevTools unbinds its id. */
  remove(element: FakeElement): void {
    element.attached = false;
    this.mutate();
  }

  /** A navigation or frame removal: the document and every node in it detach. */
  detach(): void {
    this.attached = false;
    this.mutate();
  }

  releaseAll(): void {
    const held = this.held ?? [];
    this.held = null;
    for (const release of held) {
      release();
    }
  }
}

function selectionFor(element: FakeElement): StudioSelection {
  return {
    node: element.studio,
    suggestedSelector: () => element.studio.unsupported ? null : `#n${element.studio.nodeId}`,
    querySelectorAll: selector => element.document.query(selector),
    attachment: () => ({
      node: element.document.attached && element.attached,
      document: element.document.attached,
    }),
    observeDocument: listener => {
      element.document.listeners.add(listener);
      return () => element.document.listeners.delete(listener);
    },
  };
}

function flush(ms = 0): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

describe('StyleStudioTargeting', () => {
  let context: UI.Context.Context;
  let targeting: StyleStudioTargeting;
  let unbind: () => void;
  let flavorChanges: number;
  const onFlavorChanged = (): void => {
    flavorChanges++;
  };

  beforeEach(() => {
    context = UI.Context.Context.instance({forceNew: true});
    targeting = new StyleStudioTargeting(() => {});
    unbind = bindStudioToSelection(context, FakeElement, selectionFor, targeting);
    flavorChanges = 0;
    context.addFlavorChangeListener(FakeElement, onFlavorChanged);
  });

  afterEach(() => {
    context.removeFlavorChangeListener(FakeElement, onFlavorChanged);
    unbind();
    targeting.dispose();
    UI.Context.Context.removeInstance();
  });

  function select(element: FakeElement|null): void {
    context.setFlavor(FakeElement, element);
  }

  it('follows the current Elements selection', () => {
    const document = new FakeDocument('t:1');
    assert.deepEqual(targeting.target(), {kind: 'none'});

    select(document.element(1));
    const first = targeting.target();
    assert.strictEqual(first.kind === 'node' && first.nodeId, 1);

    select(document.element(2));
    const second = targeting.target();
    assert.strictEqual(second.kind === 'node' && second.nodeId, 2);

    select(null);
    assert.deepEqual(targeting.target(), {kind: 'none'});
  });

  it('picks up a selection that existed before it was bound', () => {
    const document = new FakeDocument('t:1');
    select(document.element(5));
    const late = new StyleStudioTargeting(() => {});
    const lateUnbind = bindStudioToSelection(context, FakeElement, selectionFor, late);
    const target = late.target();
    assert.strictEqual(target.kind === 'node' && target.nodeId, 5);
    lateUnbind();
    late.dispose();
  });

  it('stops following the selection once unbound', () => {
    const document = new FakeDocument('t:1');
    select(document.element(1));
    unbind();
    select(document.element(2));
    const target = targeting.target();
    assert.strictEqual(target.kind === 'node' && target.nodeId, 1);
  });

  it('reports a valid selector with zero matches', async () => {
    const document = new FakeDocument('t:1');
    select(document.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('.missing');
    await targeting.resolveNow();
    assert.deepEqual(targeting.target(), {kind: 'no-matches', selector: '.missing'});
    assert.deepEqual(document.queries.at(-1), '.missing');
  });

  it('reports a valid selector with one match', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('#n1', {nodeIds: [1]});
    select(document.element(1));
    targeting.setSource('selector');
    await targeting.resolveNow();
    const target = targeting.target();
    assert.strictEqual(target.kind, 'node');
    assert.strictEqual(target.kind === 'node' && target.selector, '#n1');
    assert.strictEqual(target.kind === 'node' && target.matchCount, 1);
  });

  it('reports a valid selector with multiple matches without widening the scope', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('.card', {nodeIds: [1, 2, 3]});
    select(document.element(2));
    targeting.setSource('selector');
    targeting.setSelectorText('.card');
    await targeting.resolveNow();

    const single = targeting.target();
    assert.strictEqual(single.kind, 'node');
    assert.strictEqual(single.kind === 'node' && single.nodeId, 2);
    assert.strictEqual(single.kind === 'node' && single.matchCount, 3);

    targeting.setScope('all-matches');
    const all = targeting.target();
    assert.strictEqual(all.kind, 'matches');
    assert.deepEqual(all.kind === 'matches' && all.nodeIds, [1, 2, 3]);
  });

  it('reports invalid selector syntax from the query error', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('a[', {error: '\'a[\' is not a valid selector.'});
    select(document.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('a[');
    await targeting.resolveNow();
    assert.deepEqual(
        targeting.target(), {kind: 'invalid-selector', selector: 'a[', message: '\'a[\' is not a valid selector.'});
  });

  it('switches between node and selector targeting', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('#n1', {nodeIds: [1]});
    select(document.element(1));
    assert.strictEqual(targeting.target().kind, 'node');

    targeting.setSource('selector');
    // An empty input is prefilled with the suggested selector for the node.
    assert.strictEqual(targeting.state().selectorText, '#n1');
    await flush();
    const viaSelector = targeting.target();
    assert.strictEqual(viaSelector.kind === 'node' && viaSelector.selector, '#n1');

    targeting.setSource('selected-node');
    assert.deepEqual(targeting.target(), {
      kind: 'node',
      nodeId: 1,
      label: 'div#n1',
      documentKey: 't:1',
      frameId: 'frame',
      selector: null,
      matchCount: null,
    });
    // The typed selector is kept, not overwritten, when switching back.
    targeting.setSelectorText('.kept');
    targeting.setSource('selector');
    assert.strictEqual(targeting.state().selectorText, '.kept');
  });

  it('only targets all matches after an explicit choice, every time', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('.card', {nodeIds: [1, 2]});
    select(document.element(1));
    targeting.setSource('selector');
    assert.strictEqual(targeting.state().scope, 'node');

    targeting.setSelectorText('.card');
    targeting.setScope('all-matches');
    await targeting.resolveNow();
    assert.strictEqual(targeting.target().kind, 'matches');

    // Leaving the selector source and coming back resets the scope to one node.
    targeting.setSource('selected-node');
    targeting.setSource('selector');
    assert.strictEqual(targeting.state().scope, 'node');
    assert.strictEqual(targeting.target().kind, 'node');
  });

  it('does not allow all matches for the selected-node source', () => {
    const document = new FakeDocument('t:1');
    select(document.element(1));
    targeting.setScope('all-matches');
    assert.strictEqual(targeting.state().scope, 'node');
    assert.strictEqual(targeting.target().kind, 'node');
  });

  it('does not substitute a unique match elsewhere for the selected node', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('#other', {nodeIds: [9]});
    select(document.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('#other');
    await targeting.resolveNow();
    assert.deepEqual(targeting.target(), {kind: 'node-not-matched', selector: '#other', matchCount: 1});
  });

  it('never changes the Elements selection', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('.card', {nodeIds: [1, 2, 3]});
    const selected = document.element(2);
    select(selected);
    flavorChanges = 0;

    targeting.setSource('selector');
    targeting.setSelectorText('.card');
    await targeting.resolveNow();
    targeting.setScope('all-matches');
    targeting.setScope('node');
    targeting.setSource('selected-node');
    document.mutate();
    await flush(DOM_CHANGE_RESOLVE_DELAY + 50);

    assert.strictEqual(flavorChanges, 0);
    assert.strictEqual(context.flavor(FakeElement), selected);
  });

  it('keeps the frame and document of the selected node', async () => {
    const main = new FakeDocument('t:1');
    const frame = new FakeDocument('t:7');
    main.results.set('p', {nodeIds: [1, 2]});
    frame.results.set('p', {nodeIds: [70]});

    select(main.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('p');
    targeting.setScope('all-matches');
    await targeting.resolveNow();
    const inMain = targeting.target();
    assert.deepEqual(inMain.kind === 'matches' && inMain.nodeIds, [1, 2]);

    select(frame.element(70, {frameId: 'child-frame'}));
    await flush();
    const inFrame = targeting.target();
    assert.strictEqual(inFrame.kind, 'matches');
    assert.deepEqual(inFrame.kind === 'matches' && inFrame.nodeIds, [70]);
    assert.strictEqual(inFrame.kind === 'matches' && inFrame.documentKey, 't:7');
    assert.strictEqual(inFrame.kind === 'matches' && inFrame.frameId, 'child-frame');
  });

  it('drops a resolution that finishes after the selection moved to another document', async () => {
    const main = new FakeDocument('t:1');
    const other = new FakeDocument('t:2');
    main.results.set('p', {nodeIds: [1]});
    other.results.set('p', {nodeIds: [20, 21]});
    main.held = [];

    select(main.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('p');
    const stale = targeting.resolveNow();
    await flush();

    select(other.element(20));
    await flush();
    for (const release of main.held) {
      release();
    }
    await stale;

    const target = targeting.target();
    assert.strictEqual(target.kind, 'node');
    assert.strictEqual(target.kind === 'node' && target.matchCount, 2);
    assert.strictEqual(target.kind === 'node' && target.documentKey, 't:2');
  });

  it('keeps matches when the selection moves within the same document', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('.card', {nodeIds: [1, 2]});
    select(document.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('.card');
    await targeting.resolveNow();
    const queries = document.queries.length;

    select(document.element(2));
    await flush();
    const target = targeting.target();
    assert.strictEqual(target.kind === 'node' && target.nodeId, 2);
    assert.strictEqual(document.queries.length, queries);
  });

  it('queries again after the document changes', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('.card', {nodeIds: [1]});
    select(document.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('.card');
    targeting.setScope('all-matches');
    await targeting.resolveNow();

    document.results.set('.card', {nodeIds: [1, 2]});
    document.mutate();
    assert.strictEqual(targeting.target().kind, 'pending');
    await flush(DOM_CHANGE_RESOLVE_DELAY + 50);
    const target = targeting.target();
    assert.deepEqual(target.kind === 'matches' && target.nodeIds, [1, 2]);
  });

  it('declines a shadow-tree selection with an explicit reason', () => {
    const document = new FakeDocument('t:1');
    select(document.element(3, {unsupported: 'shadow-tree'}));
    assert.deepEqual(targeting.target(), {kind: 'unsupported', reason: 'shadow-tree'});
  });

  it('fills the selector from a picked element and keeps one-node scope', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('.card', {nodeIds: [1, 2]});
    document.results.set('#n2', {nodeIds: [2]});
    select(document.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('.card');
    targeting.setScope('all-matches');
    await targeting.resolveNow();

    // The page pick changes the Elements selection first, then fills.
    select(document.element(2));
    targeting.fillSelectorFromSelection();
    assert.strictEqual(targeting.state().selectorText, '#n2');
    assert.strictEqual(targeting.state().scope, 'node');
    await flush();
    const target = targeting.target();
    assert.strictEqual(target.kind, 'node');
    assert.strictEqual(target.kind === 'node' && target.nodeId, 2);
    assert.strictEqual(target.kind === 'node' && target.selector, '#n2');
  });

  it('does not fill a selector while targeting the selected element', () => {
    const document = new FakeDocument('t:1');
    select(document.element(1));
    targeting.fillSelectorFromSelection();
    assert.strictEqual(targeting.state().source, 'selected-node');
    assert.strictEqual(targeting.state().selectorText, '');
  });

  it('fills again when the already-selected element is picked again', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('.card', {nodeIds: [1, 2]});
    document.results.set('#n1', {nodeIds: [1]});
    select(document.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('.card');
    targeting.setScope('all-matches');
    await targeting.resolveNow();

    // No selection change: the same node is picked again.
    targeting.fillSelectorFromSelection();
    assert.strictEqual(targeting.state().selectorText, '#n1');
    assert.strictEqual(targeting.state().scope, 'node');
    await flush();
    const target = targeting.target();
    assert.strictEqual(target.kind === 'node' && target.selector, '#n1');
  });

  it('establishes the first target from a pick when nothing was selected', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('#n4', {nodeIds: [4]});
    targeting.setSource('selector');
    assert.deepEqual(targeting.target(), {kind: 'none'});
    targeting.fillSelectorFromSelection();
    assert.strictEqual(targeting.state().selectorText, '');

    select(document.element(4));
    targeting.fillSelectorFromSelection();
    await flush();
    const target = targeting.target();
    assert.strictEqual(target.kind === 'node' && target.nodeId, 4);
    assert.strictEqual(target.kind === 'node' && target.selector, '#n4');
  });

  it('keeps the selector and scope when a pick is cancelled', async () => {
    const document = new FakeDocument('t:1');
    document.results.set('.card', {nodeIds: [1, 2]});
    select(document.element(1));
    targeting.setSource('selector');
    targeting.setSelectorText('.card');
    targeting.setScope('all-matches');
    await targeting.resolveNow();
    // A cancelled pick never calls fillSelectorFromSelection.
    assert.strictEqual(targeting.state().selectorText, '.card');
    assert.strictEqual(targeting.state().scope, 'all-matches');
    assert.strictEqual(targeting.target().kind, 'matches');
  });

  it('reports an unsupported picked element instead of a selector status', () => {
    const document = new FakeDocument('t:1');
    select(document.element(3, {unsupported: 'shadow-tree'}));
    targeting.setSource('selector');
    targeting.fillSelectorFromSelection();
    assert.strictEqual(targeting.state().selectorText, '');
    assert.deepEqual(targeting.target(), {kind: 'unsupported', reason: 'shadow-tree'});
  });

  describe('lifecycle', () => {
    it('reports a removed selected element as detached and never as another element', () => {
      const document = new FakeDocument('t:1');
      const selected = document.element(1);
      select(selected);
      document.remove(selected);
      assert.deepEqual(targeting.target(), {kind: 'detached', reason: 'node'});
    });

    it('reports a detached element in one-node selector scope instead of a selector status', async () => {
      const document = new FakeDocument('t:1');
      document.results.set('#n1', {nodeIds: [1]});
      const selected = document.element(1);
      select(selected);
      targeting.setSource('selector');
      await flush();
      document.results.set('#n1', {nodeIds: []});
      document.remove(selected);
      assert.deepEqual(targeting.target(), {kind: 'detached', reason: 'node'});
      await flush(DOM_CHANGE_RESOLVE_DELAY + 50);
      assert.deepEqual(targeting.target(), {kind: 'detached', reason: 'node'});
    });

    it('keeps all-matches targeting for a live document when the selected element is removed', async () => {
      const document = new FakeDocument('t:1');
      document.results.set('.card', {nodeIds: [1, 2, 3]});
      const selected = document.element(1);
      select(selected);
      targeting.setSource('selector');
      targeting.setSelectorText('.card');
      targeting.setScope('all-matches');
      await targeting.resolveNow();

      document.results.set('.card', {nodeIds: [2, 3]});
      document.remove(selected);
      await flush(DOM_CHANGE_RESOLVE_DELAY + 50);
      const target = targeting.target();
      assert.strictEqual(target.kind, 'matches');
      assert.deepEqual(target.kind === 'matches' && target.nodeIds, [2, 3]);
      assert.isFalse(target.kind === 'matches' && target.includesSelected);
    });

    it('announces the change when Elements moves on from a replaced element', () => {
      const document = new FakeDocument('t:1');
      const original = document.element(1, {label: 'div#card'});
      select(original);
      document.remove(original);
      // A visually identical replacement is a new node with a new id.
      select(document.element(7, {label: 'div#card'}));
      const target = targeting.target();
      assert.strictEqual(target.kind === 'node' && target.nodeId, 7);
      assert.strictEqual(targeting.state().previousDetachedLabel, 'div#card');
      // The next explicit action clears the announcement.
      targeting.setSource('selector');
      assert.isNull(targeting.state().previousDetachedLabel);
    });

    it('keeps the announcement through a moment with no selection', () => {
      const before = new FakeDocument('t:1');
      select(before.element(1, {label: 'p.lonely'}));
      before.detach();
      select(null);
      const after = new FakeDocument('t:2');
      select(after.element(30, {label: 'p.lonely'}));
      assert.strictEqual(targeting.state().previousDetachedLabel, 'p.lonely');
    });

    it('announces a reload where Elements clears the selection before the document is replaced', () => {
      const before = new FakeDocument('t:1');
      select(before.element(1, {label: 'p.lonely'}));
      // Elements clears the selection first; the old node is still attached then.
      select(null);
      assert.isNull(targeting.state().previousDetachedLabel);
      before.detach();
      const after = new FakeDocument('t:2');
      select(after.element(30, {label: 'p.lonely'}));
      assert.strictEqual(targeting.state().previousDetachedLabel, 'p.lonely');
    });

    it('does not announce a cleared and restored selection whose node is still attached', () => {
      const document = new FakeDocument('t:1');
      select(document.element(1));
      select(null);
      select(document.element(2));
      assert.isNull(targeting.state().previousDetachedLabel);
    });

    it('does not announce an ordinary selection change', () => {
      const document = new FakeDocument('t:1');
      select(document.element(1));
      select(document.element(2));
      assert.isNull(targeting.state().previousDetachedLabel);
    });

    it('treats a moved element as detached until it is selected again', () => {
      const document = new FakeDocument('t:1');
      const moved = document.element(1);
      select(moved);
      // Moving a node between parents re-binds it under a new id.
      document.remove(moved);
      assert.deepEqual(targeting.target(), {kind: 'detached', reason: 'node'});
      select(document.element(12));
      const target = targeting.target();
      assert.strictEqual(target.kind === 'node' && target.nodeId, 12);
    });

    it('keeps the selector across a reload and resolves it in the new document', async () => {
      const before = new FakeDocument('t:1');
      before.results.set('.card', {nodeIds: [1, 2]});
      select(before.element(1));
      targeting.setSource('selector');
      targeting.setSelectorText('.card');
      targeting.setScope('all-matches');
      await targeting.resolveNow();

      before.detach();
      assert.deepEqual(targeting.target(), {kind: 'detached', reason: 'document'});
      assert.strictEqual(targeting.state().selectorText, '.card');

      const after = new FakeDocument('t:9');
      after.results.set('.card', {nodeIds: [40, 41, 42]});
      select(after.element(40));
      await flush();
      const target = targeting.target();
      assert.strictEqual(target.kind, 'matches');
      assert.deepEqual(target.kind === 'matches' && target.nodeIds, [40, 41, 42]);
      assert.strictEqual(target.kind === 'matches' && target.documentKey, 't:9');
      assert.strictEqual(targeting.state().previousDetachedLabel, 'div#n1');
    });

    it('never queries a document that has gone away', async () => {
      const document = new FakeDocument('t:1');
      select(document.element(1));
      targeting.setSource('selector');
      await flush();
      const queries = document.queries.length;
      document.detach();
      targeting.setSelectorText('.anything');
      await targeting.resolveNow();
      targeting.revalidate();
      await flush(SELECTOR_RESOLVE_DELAY + 50);
      assert.strictEqual(document.queries.length, queries);
    });

    it('re-queries matches on revalidate, for changes DevTools was not told about', async () => {
      const document = new FakeDocument('t:1');
      document.results.set('.active', {nodeIds: [1]});
      select(document.element(1));
      targeting.setSource('selector');
      targeting.setSelectorText('.active');
      targeting.setScope('all-matches');
      await targeting.resolveNow();

      // A class added to a node DevTools never loaded produces no event.
      document.results.set('.active', {nodeIds: [1, 5]});
      targeting.revalidate();
      await flush();
      const target = targeting.target();
      assert.deepEqual(target.kind === 'matches' && target.nodeIds, [1, 5]);
    });
  });

  describe('async correctness', () => {
    it('debounces rapid typing into one query for the final selector', async () => {
      const document = new FakeDocument('t:1');
      select(document.element(1));
      targeting.setSource('selector');
      await flush();
      document.queries.length = 0;
      for (const text of ['.c', '.ca', '.car', '.card']) {
        targeting.setSelectorText(text);
      }
      await flush(SELECTOR_RESOLVE_DELAY + 50);
      assert.deepEqual(document.queries, ['.card']);
    });

    it('drops a response for selector text that has since changed', async () => {
      const document = new FakeDocument('t:1');
      document.results.set('.old', {nodeIds: [1]});
      document.results.set('.new', {nodeIds: [1, 2, 3]});
      select(document.element(1));
      targeting.setSource('selector');
      await flush();
      document.held = [];
      targeting.setSelectorText('.old');
      const stale = targeting.resolveNow();
      await flush();
      targeting.setSelectorText('.new');
      document.releaseAll();
      await stale;
      // The .old answer never became a resolution.
      const resolution = targeting.state().resolution;
      assert.isFalse(resolution.kind === 'resolved' && resolution.selector === '.old');
      await flush(SELECTOR_RESOLVE_DELAY + 50);
      const target = targeting.target();
      assert.strictEqual(target.kind === 'node' && target.matchCount, 3);
    });

    it('drops a response that finishes after a navigation', async () => {
      const document = new FakeDocument('t:1');
      document.results.set('p', {nodeIds: [1]});
      select(document.element(1));
      targeting.setSource('selector');
      await flush();
      document.held = [];
      targeting.setSelectorText('p');
      const inFlight = targeting.resolveNow();
      await flush();
      document.detach();
      document.releaseAll();
      await inFlight;
      assert.deepEqual(targeting.state().resolution, {kind: 'idle'});
      assert.deepEqual(targeting.target(), {kind: 'detached', reason: 'document'});
    });

    it('drops a response that races a DOM mutation and queries again', async () => {
      const document = new FakeDocument('t:1');
      document.results.set('.card', {nodeIds: [1]});
      select(document.element(1));
      targeting.setSource('selector');
      targeting.setSelectorText('.card');
      targeting.setScope('all-matches');
      await targeting.resolveNow();

      document.held = [];
      targeting.revalidate();
      await flush();
      document.results.set('.card', {nodeIds: [1, 2]});
      document.mutate();
      document.releaseAll();
      await flush();
      assert.strictEqual(targeting.target().kind, 'pending');
      await flush(DOM_CHANGE_RESOLVE_DELAY + 50);
      const target = targeting.target();
      assert.deepEqual(target.kind === 'matches' && target.nodeIds, [1, 2]);
    });

    it('lets only the latest of overlapping requests update state', async () => {
      const document = new FakeDocument('t:1');
      select(document.element(1));
      targeting.setSource('selector');
      targeting.setSelectorText('.card');
      targeting.setScope('all-matches');
      await flush();
      document.held = [];
      document.results.set('.card', {nodeIds: [1]});
      targeting.revalidate();
      await flush();
      document.results.set('.card', {nodeIds: [1, 2, 3]});
      targeting.revalidate();
      await flush();
      // Release the older request last: it must still not win.
      const held = document.held;
      document.held = null;
      held[1]();
      await flush();
      held[0]();
      await flush();
      const target = targeting.target();
      assert.deepEqual(target.kind === 'matches' && target.nodeIds, [1, 2, 3]);
    });

    it('does not re-query an invalid selector when the DOM changes', async () => {
      const document = new FakeDocument('t:1');
      document.results.set('a[', {error: 'not a valid selector'});
      select(document.element(1));
      targeting.setSource('selector');
      targeting.setSelectorText('a[');
      await targeting.resolveNow();
      const queries = document.queries.length;
      document.mutate();
      await flush(DOM_CHANGE_RESOLVE_DELAY + 50);
      assert.strictEqual(document.queries.length, queries);
      assert.strictEqual(targeting.target().kind, 'invalid-selector');
    });
  });

  describe('background refresh', () => {
    const INTERVAL = 60;
    let live: StyleStudioTargeting;
    let unbindLive: () => void;

    beforeEach(() => {
      live = new StyleStudioTargeting(() => {}, {refreshInterval: INTERVAL});
      unbindLive = bindStudioToSelection(context, FakeElement, selectionFor, live);
    });

    afterEach(() => {
      unbindLive();
      live.dispose();
    });

    async function resolvedCards(document: FakeDocument): Promise<void> {
      document.results.set('.card', {nodeIds: [1]});
      select(document.element(1));
      live.setSource('selector');
      live.setSelectorText('.card');
      live.setScope('all-matches');
      await live.resolveNow();
    }

    it('picks up matches that changed without any DOM event, while live', async () => {
      const document = new FakeDocument('t:1');
      await resolvedCards(document);
      live.setLive(true);
      document.results.set('.card', {nodeIds: [1, 9]});
      await flush(INTERVAL * 3);
      const target = live.target();
      assert.deepEqual(target.kind === 'matches' && target.nodeIds, [1, 9]);
    });

    it('never shows a pending state for a background check', async () => {
      const document = new FakeDocument('t:1');
      await resolvedCards(document);
      const kinds: string[] = [];
      const watch = setInterval(() => kinds.push(live.target().kind), 5);
      live.setLive(true);
      await flush(INTERVAL * 4);
      clearInterval(watch);
      assert.notInclude(kinds, 'pending');
    });

    it('does not poll while hidden', async () => {
      const document = new FakeDocument('t:1');
      await resolvedCards(document);
      const queries = document.queries.length;
      live.setLive(true);
      live.setLive(false);
      await flush(INTERVAL * 3);
      assert.strictEqual(document.queries.length, queries);
    });

    it('yields to newer typing', async () => {
      const document = new FakeDocument('t:1');
      await resolvedCards(document);
      document.results.set('.card', {nodeIds: [1, 2, 3, 4]});
      document.results.set('.other', {nodeIds: [5]});
      document.held = [];
      live.setLive(true);
      await flush(INTERVAL + 20);
      // A background check is in flight; the user types a new selector.
      live.setSelectorText('.other');
      document.releaseAll();
      await flush(SELECTOR_RESOLVE_DELAY + 50);
      const target = live.target();
      assert.strictEqual(target.kind === 'matches' && target.selector, '.other');
      assert.deepEqual(target.kind === 'matches' && target.nodeIds, [5]);
    });
  });
});
