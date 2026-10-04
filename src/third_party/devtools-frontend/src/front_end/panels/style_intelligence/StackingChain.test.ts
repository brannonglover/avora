// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {buildStackingChain, isDocumentRootContext, needsStackingSnapshot, type StackingChainSource} from './StackingChain.js';
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

function source(
    nodeId: number, label: string, overrides: Partial<StackingReasonInput> = {},
    extra: Partial<StackingChainSource> = {}): StackingChainSource {
  return {nodeId, backendNodeId: nodeId + 100, label, style: style(overrides), topLayer: false, ...extra};
}

describe('StackingChain', () => {
  it('places a transformed ancestor between a z-index descendant and the document', () => {
    const chain = buildStackingChain(
        source(3, 'div#modal', {position: 'absolute', zIndex: '9999', layoutParentDisplay: 'block'}), [
          source(2, 'div#content', {
            position: 'relative',
            transform: 'matrix(1, 0, 0, 1, 0, 0)',
            declarations: {transform: {id: 7, value: 'translateZ(0)', hasSource: true}},
          }),
          source(1, 'document', {isDocumentElement: true}),
        ]);

    assert.strictEqual(chain.selected.createsStackingContext, true);
    assert.deepEqual(chain.selected.reasons.map(reason => reason.property), ['z-index']);
    assert.deepEqual(chain.ancestorContexts.map(node => node.label), ['div#content', 'document']);
    assert.strictEqual(chain.ancestorContexts[0].reasons[0].authoredValue, 'translateZ(0)');
    assert.strictEqual(isDocumentRootContext(chain.ancestorContexts[1]), true);
    assert.strictEqual(needsStackingSnapshot(chain), true);
  });

  it('does not treat a relative box with z-index:auto as a stacking context', () => {
    const chain = buildStackingChain(source(4, 'div#pos-auto', {position: 'relative', zIndex: 'auto'}), [
      source(1, 'document', {isDocumentElement: true}),
    ]);
    assert.strictEqual(chain.selected.createsStackingContext, false);
    assert.deepEqual(chain.ancestorContexts.map(node => node.label), ['document']);
    assert.strictEqual(needsStackingSnapshot(chain), false);
  });

  it('omits a display:contents box because the layout parent skipped it', () => {
    const chain = buildStackingChain(source(5, 'div#child', {opacity: '0.5'}), [
      source(6, 'div#layout-parent', {opacity: '0.8'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    assert.deepEqual(chain.ancestorContexts.map(node => node.label), ['div#layout-parent', 'document']);
  });

  it('releases a descendant into the document context when the ancestor transform is gone', () => {
    const chain = buildStackingChain(
        source(3, 'div#modal', {position: 'absolute', zIndex: '9999'}),
        [source(2, 'div#content', {position: 'relative', transform: 'none'}), source(1, 'document', {isDocumentElement: true})]);
    assert.deepEqual(chain.ancestorContexts.map(node => node.label), ['document']);
    assert.strictEqual(needsStackingSnapshot(chain), false);
  });

  it('keeps a static flex item z-index on the item and not on a non-context flex parent', () => {
    const chain = buildStackingChain(
        source(8, 'div#flex-item', {position: 'static', zIndex: '3', layoutParentDisplay: 'flex'}),
        [source(7, 'div#flex-host', {display: 'flex'}), source(1, 'document', {isDocumentElement: true})]);
    assert.deepEqual(chain.selected.reasons.map(reason => reason.property), ['z-index']);
    assert.strictEqual(chain.ancestorContexts[0].label, 'document');
  });

  it('stops a frame element at its own document root', () => {
    const chain = buildStackingChain(
        source(11, 'div#frame-card', {position: 'relative', zIndex: '5'}),
        [source(10, 'document', {isDocumentElement: true})]);
    assert.deepEqual(chain.ancestorContexts.map(node => node.label), ['document']);
    assert.strictEqual(chain.ancestorContexts[0].nodeId, 10);
  });

  it('does not make a shadow host a stacking context when only the shadow child has opacity', () => {
    const chain = buildStackingChain(source(13, 'div#shadow-card', {opacity: '0.8'}), [
      source(12, 'div#shadow-host'),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    assert.strictEqual(chain.selected.createsStackingContext, true);
    assert.deepEqual(chain.ancestorContexts.map(node => node.label), ['document']);
  });
});
