// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {presentLayering} from './LayeringPresentation.js';
import {type StackingBoundary} from './StackingBoundary.js';
import {buildStackingChain, type StackingChain, type StackingChainSource} from './StackingChain.js';
import {type StackingReasonInput} from './StackingReasons.js';

function style(overrides: Partial<StackingReasonInput> = {}): StackingReasonInput {
  return {
    display: 'block',
    position: 'static',
    zIndex: 'auto',
    opacity: '1',
    transform: 'none',
    translate: 'none',
    rotate: 'none',
    scale: 'none',
    offsetPath: 'none',
    offsetPosition: 'auto',
    transformStyle: 'flat',
    perspective: 'none',
    filter: 'none',
    backdropFilter: 'none',
    clipPath: 'none',
    maskImage: 'none',
    webkitMaskImage: 'none',
    isolation: 'auto',
    mixBlendMode: 'normal',
    contain: 'none',
    willChange: 'auto',
    viewTransitionName: 'none',
    webkitBoxReflect: 'none',
    layoutParentDisplay: 'block',
    isDocumentElement: false,
    ...overrides,
  };
}

function source(nodeId: number, label: string, overrides: Partial<StackingReasonInput> = {}): StackingChainSource {
  return {nodeId, backendNodeId: nodeId, label, style: style(overrides), topLayer: false};
}

describe('LayeringPresentation', () => {
  it('explains the nearest ancestor when the selected element creates no context', () => {
    const chain = buildStackingChain(source(4, 'div#child'), [
      source(3, 'div#content', {
        transform: 'matrix(1, 0, 0, 1, 0, 0)',
        declarations: {transform: {id: 2, value: 'translateZ(0)', hasSource: true}},
      }),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const view = presentLayering(chain, null, 4);
    assert.deepEqual(view.lines.slice(0, 3), [
      'Inside div#content',
      'div#content creates a stacking context',
      'transform: translateZ(0)',
    ]);
    assert.strictEqual(view.actions[0].nodeId, 3);
    assert.strictEqual(view.actions[0].declarationId, 2);
    assert.notStrictEqual(view.actions[0].nodeId, 4);
  });

  it('lists every reason on the selected element without highlighting it', () => {
    const chain = buildStackingChain(source(5, 'div#both', {
      opacity: '0.8',
      transform: 'matrix(1, 0, 0, 1, 0, 0)',
      declarations: {
        opacity: {id: 1, value: '0.8', hasSource: true},
        transform: {id: 2, value: 'translateZ(0)', hasSource: true},
      },
    }), [source(1, 'document', {isDocumentElement: true})]);
    const view = presentLayering(chain, null, 5);
    assert.deepEqual(view.lines, [
      'Creates a stacking context',
      'opacity: 0.8',
      'Also:',
      'transform: translateZ(0)',
    ]);
    assert.strictEqual(view.actions[0].nodeId, null);
    assert.strictEqual(view.actions[0].declarationId, 1);
    assert.strictEqual(view.actions[1].declarationId, 2);
  });

  it('names the document when that is the only ancestor context', () => {
    const chain = buildStackingChain(source(4, 'div#flow'), [source(1, 'document', {isDocumentElement: true})]);
    const view = presentLayering(chain, null, 4);
    assert.deepEqual(view.lines, ['Inside the document', 'The document creates a stacking context.']);
  });

  it('explains a provable sibling boundary and highlights the trapping context', () => {
    const chain = buildStackingChain(source(4, 'div#modal', {position: 'absolute', zIndex: '9999'}), [
      source(3, 'div#content', {
        position: 'relative',
        transform: 'matrix(1, 0, 0, 1, 0, 0)',
        declarations: {transform: {id: 9, value: 'translateZ(0)', hasSource: true}},
      }),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const boundary: StackingBoundary = {
      trapNodeId: 3,
      trapLabel: 'div#content',
      trapText: 'transform: translateZ(0)',
      trapDeclarationId: 9,
      trapHasSource: true,
      otherBackendNodeId: 2,
      otherLabel: 'div#header',
      otherText: 'z-index: 10',
      order: {kind: 'z-index', value: '10'},
    };
    const view = presentLayering(chain, boundary, 4);
    assert.strictEqual(view.lines[0], 'div#modal cannot rise above div#header by increasing its z-index.');
    assert.strictEqual(view.lines[1], 'div#modal is inside div#content\'s stacking context.');
    assert.strictEqual(view.actions[0].nodeId, 3);
    assert.strictEqual(view.actions[0].declarationId, 9);
    assert.strictEqual(view.lines.some(line => /cover/i.test(line)), false);
  });

  it('describes a same-level paint-order boundary without a covering claim', () => {
    const chain = buildStackingChain(source(4, 'div#child', {position: 'relative', zIndex: '5'}), [
      source(3, 'div#early', {opacity: '0.9'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const boundary: StackingBoundary = {
      trapNodeId: 3,
      trapLabel: 'div#early',
      trapText: 'opacity: 0.9',
      trapDeclarationId: 2,
      trapHasSource: true,
      otherBackendNodeId: 5,
      otherLabel: 'div#later',
      otherText: '',
      order: {kind: 'paint-order'},
    };
    const view = presentLayering(chain, boundary, 4);
    assert.deepEqual(view.lines, [
      'div#child is confined to div#early\'s stacking context.',
      'div#early',
      'opacity: 0.9',
      'div#later\'s sibling context is ordered later.',
    ]);
    assert.strictEqual(view.lines.some(line => /cannot rise above|cover/i.test(line)), false);
  });

  it('explains top layer without a sibling z-index comparison', () => {
    const chain = buildStackingChain(source(6, 'dialog#dlg', {position: 'fixed'}), [
      source(1, 'document', {isDocumentElement: true}),
    ]);
    chain.selected.topLayer = true;
    const view = presentLayering(chain, null, 6);
    assert.strictEqual(view.lines[0], 'Top layer');
    assert.match(view.lines[1], /top layer/);
    assert.strictEqual(view.actions.length, 0);
  });

  it('reports an internal stacking context without guessing a property', () => {
    const chain: StackingChain = buildStackingChain(source(2, 'div#mystery'), [source(1, 'document', {isDocumentElement: true})]);
    chain.selected.createsStackingContext = true;
    chain.selected.reasons = [{
      property: 'internal',
      computedValue: null,
      authoredValue: null,
      declarationId: null,
      hasSource: false,
      confidence: 'limited',
    }];
    const view = presentLayering(chain, null, 2);
    assert.match(view.lines[1], /no CSS-visible cause/);
    assert.strictEqual(view.actions.length, 0);
  });
});
