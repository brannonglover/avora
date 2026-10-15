// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import * as UI from '../../ui/legacy/legacy.js';

import {bindStudioToSelection} from './StyleStudioDom.js';
import type {StudioNode} from './StyleStudioTarget.js';
import {
  DOM_CHANGE_RESOLVE_DELAY,
  type SelectorQueryResult,
  type StudioSelection,
  StyleStudioTargeting,
} from './StyleStudioTargeting.js';

/** Stands in for SDK.DOMModel.DOMNode as the Elements selection flavor. */
class FakeElement {
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

  constructor(readonly key: string) {
  }

  element(nodeId: number, overrides: Partial<StudioNode> = {}): FakeElement {
    return new FakeElement(
        {nodeId, label: `div#n${nodeId}`, documentKey: this.key, frameId: 'frame', unsupported: null, ...overrides},
        this);
  }

  async query(selector: string): Promise<SelectorQueryResult> {
    this.queries.push(selector);
    if (this.held) {
      await new Promise<void>(resolve => this.held?.push(resolve));
    }
    return this.results.get(selector) ?? {nodeIds: []};
  }

  mutate(): void {
    for (const listener of this.listeners) {
      listener();
    }
  }
}

function selectionFor(element: FakeElement): StudioSelection {
  return {
    node: element.studio,
    suggestedSelector: () => element.studio.unsupported ? null : `#n${element.studio.nodeId}`,
    querySelectorAll: selector => element.document.query(selector),
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
});
