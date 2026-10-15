// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {
  deriveTarget,
  initialTargetingState,
  needsResolution,
  readableSelector,
  type SelectorResolution,
  type StudioNode,
  type StudioTargetingState,
} from './StyleStudioTarget.js';

const DOC = 'target-1:1';

function studioNode(overrides: Partial<StudioNode> = {}): StudioNode {
  return {
    nodeId: 10,
    label: 'div.card',
    documentKey: DOC,
    frameId: 'frame-1',
    unsupported: null,
    ...overrides,
  };
}

function resolved(selector: string, nodeIds: number[], documentKey = DOC): SelectorResolution {
  return {kind: 'resolved', selector, documentKey, nodeIds};
}

function state(overrides: Partial<StudioTargetingState>): StudioTargetingState {
  return {...initialTargetingState(), node: studioNode(), ...overrides};
}

describe('StyleStudioTarget', () => {
  it('has no target without a selected node', () => {
    assert.deepEqual(deriveTarget(initialTargetingState()), {kind: 'none'});
  });

  it('targets the selected node by node id, with its frame and document', () => {
    const target = deriveTarget(state({}));
    assert.deepEqual(target, {
      kind: 'node',
      nodeId: 10,
      label: 'div.card',
      documentKey: DOC,
      frameId: 'frame-1',
      selector: null,
      matchCount: null,
    });
  });

  for (const reason of ['not-element', 'pseudo-element', 'shadow-tree'] as const) {
    it(`declines a ${reason} selected node`, () => {
      const target = deriveTarget(state({node: studioNode({unsupported: reason})}));
      assert.deepEqual(target, {kind: 'unsupported', reason});
    });
  }

  it('declines a node with no document', () => {
    assert.deepEqual(deriveTarget(state({node: studioNode({documentKey: null})})), {
      kind: 'unsupported',
      reason: 'no-document',
    });
    assert.deepEqual(deriveTarget(state({source: 'selector', selectorText: 'p', node: studioNode({documentKey: null})})), {
      kind: 'unsupported',
      reason: 'no-document',
    });
  });

  it('reports an unsupported node before an empty selector in one-node scope', () => {
    assert.deepEqual(
        deriveTarget(state({source: 'selector', selectorText: '', node: studioNode({unsupported: 'shadow-tree'})})),
        {kind: 'unsupported', reason: 'shadow-tree'});
  });

  it('reports a detached node and never its id as a target', () => {
    const detachedNode = {attachment: {node: false, document: true}};
    assert.deepEqual(deriveTarget(state(detachedNode)), {kind: 'detached', reason: 'node'});
    assert.deepEqual(
        deriveTarget(state({...detachedNode, source: 'selector', selectorText: '#x', resolution: resolved('#x', [10])})),
        {kind: 'detached', reason: 'node'});
    // All matches stays usable while the document is alive.
    assert.strictEqual(
        deriveTarget(state({
          ...detachedNode,
          source: 'selector',
          scope: 'all-matches',
          selectorText: '.card',
          resolution: resolved('.card', [11, 12]),
        })).kind,
        'matches');
  });

  it('reports a detached document for every source and resolves nothing in it', () => {
    const detachedDocument = {attachment: {node: false, document: false}};
    assert.deepEqual(deriveTarget(state(detachedDocument)), {kind: 'detached', reason: 'document'});
    const selectorState = state({
      ...detachedDocument,
      source: 'selector',
      scope: 'all-matches',
      selectorText: '.card',
      resolution: resolved('.card', [11]),
    });
    assert.deepEqual(deriveTarget(selectorState), {kind: 'detached', reason: 'document'});
    assert.isFalse(needsResolution({...selectorState, resolution: {kind: 'idle'}}));
  });

  it('asks for a selector when the input is empty or whitespace', () => {
    assert.deepEqual(deriveTarget(state({source: 'selector', selectorText: '   '})), {kind: 'empty-selector'});
  });

  it('is pending until the current selector is resolved', () => {
    assert.deepEqual(deriveTarget(state({source: 'selector', selectorText: '.card'})), {
      kind: 'pending',
      selector: '.card',
    });
    assert.deepEqual(
        deriveTarget(state({
          source: 'selector',
          selectorText: '.card',
          resolution: {kind: 'resolving', selector: '.card', documentKey: DOC},
        })),
        {kind: 'pending', selector: '.card'});
  });

  it('never reuses a resolution made for another selector or another document', () => {
    assert.strictEqual(
        deriveTarget(state({source: 'selector', selectorText: '.card', resolution: resolved('.other', [10])})).kind,
        'pending');
    assert.strictEqual(
        deriveTarget(state({source: 'selector', selectorText: '.card', resolution: resolved('.card', [10], 'target-1:2')}))
            .kind,
        'pending');
  });

  it('reports a valid selector with zero matches', () => {
    const target = deriveTarget(state({source: 'selector', selectorText: '.missing', resolution: resolved('.missing', [])}));
    assert.deepEqual(target, {kind: 'no-matches', selector: '.missing'});
  });

  it('reports an invalid selector with the browser message', () => {
    const target = deriveTarget(state({
      source: 'selector',
      selectorText: 'a[',
      resolution: {kind: 'invalid', selector: 'a[', documentKey: DOC, message: 'not a valid selector'},
    }));
    assert.deepEqual(target, {kind: 'invalid-selector', selector: 'a[', message: 'not a valid selector'});
  });

  it('targets the selected node through a selector with one match', () => {
    const target = deriveTarget(state({source: 'selector', selectorText: '#main', resolution: resolved('#main', [10])}));
    assert.deepEqual(target, {
      kind: 'node',
      nodeId: 10,
      label: 'div.card',
      documentKey: DOC,
      frameId: 'frame-1',
      selector: '#main',
      matchCount: 1,
    });
  });

  it('keeps one-node scope as one node when the selector matches several elements', () => {
    const target = deriveTarget(state({source: 'selector', selectorText: '.card', resolution: resolved('.card', [9, 10, 11])}));
    assert.strictEqual(target.kind, 'node');
    assert.strictEqual(target.kind === 'node' && target.nodeId, 10);
    assert.strictEqual(target.kind === 'node' && target.matchCount, 3);
  });

  it('targets every match only when the scope is all matches', () => {
    const target = deriveTarget(state({
      source: 'selector',
      scope: 'all-matches',
      selectorText: '.card',
      resolution: resolved('.card', [9, 10, 11]),
    }));
    assert.deepEqual(target, {
      kind: 'matches',
      selector: '.card',
      documentKey: DOC,
      frameId: 'frame-1',
      nodeIds: [9, 10, 11],
      includesSelected: true,
    });
  });

  it('does not substitute a unique match for a selected node it does not match', () => {
    const target = deriveTarget(state({source: 'selector', selectorText: '#other', resolution: resolved('#other', [42])}));
    assert.deepEqual(target, {kind: 'node-not-matched', selector: '#other', matchCount: 1});
  });

  it('reports all matches even when the selected node is not one of them', () => {
    const target = deriveTarget(state({
      source: 'selector',
      scope: 'all-matches',
      selectorText: '.card',
      resolution: resolved('.card', [42, 43]),
    }));
    assert.strictEqual(target.kind, 'matches');
    assert.isFalse(target.kind === 'matches' && target.includesSelected);
  });

  it('ignores the scope for the selected-node source', () => {
    const target = deriveTarget(state({source: 'selected-node', scope: 'all-matches'}));
    assert.strictEqual(target.kind, 'node');
  });

  it('declines a shadow-tree node in one-node scope but allows document matches', () => {
    const shadow = studioNode({unsupported: 'shadow-tree'});
    assert.deepEqual(
        deriveTarget(state({node: shadow, source: 'selector', selectorText: 'p', resolution: resolved('p', [10])})),
        {kind: 'unsupported', reason: 'shadow-tree'});
    assert.strictEqual(
        deriveTarget(state({
          node: shadow,
          source: 'selector',
          scope: 'all-matches',
          selectorText: 'p',
          resolution: resolved('p', [3]),
        })).kind,
        'matches');
  });

  it('needs a resolution only for an unresolved selector in a known document', () => {
    assert.isFalse(needsResolution(state({})));
    assert.isFalse(needsResolution(state({source: 'selector', selectorText: ' '})));
    assert.isTrue(needsResolution(state({source: 'selector', selectorText: 'p'})));
    assert.isFalse(needsResolution(state({source: 'selector', selectorText: ' p ', resolution: resolved('p', [])})));
    assert.isTrue(needsResolution(state({source: 'selector', selectorText: 'p', resolution: resolved('p', [], 'x:2')})));
  });

  describe('readableSelector', () => {
    it('prefers the id', () => {
      assert.strictEqual(readableSelector({localName: 'div', id: 'main', classes: ['card']}), '#main');
    });

    it('escapes ids and classes that are not plain identifiers', () => {
      assert.strictEqual(readableSelector({localName: 'div', id: 'a:b', classes: []}), '#a\\:b');
      assert.strictEqual(readableSelector({localName: 'div', id: null, classes: ['md:flex']}), 'div.md\\:flex');
    });

    it('uses the tag and at most three classes', () => {
      assert.strictEqual(
          readableSelector({localName: 'li', id: null, classes: ['', 'a', 'b', 'c', 'd']}), 'li.a.b.c');
    });

    it('has no readable selector without an id or classes', () => {
      assert.isNull(readableSelector({localName: 'p', id: '  ', classes: ['', ' ']}));
    });
  });
});
