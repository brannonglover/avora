// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {explainStackingBoundary} from './StackingBoundary.js';
import {buildStackingChain, type StackingChainSource} from './StackingChain.js';
import {type StackingReasonInput} from './StackingReasons.js';
import {applySnapshot, readSnapshot, type SnapshotDocumentData, type SnapshotNodeRecord} from './StackingSnapshot.js';

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

function record(partial: Partial<SnapshotNodeRecord> & Pick<SnapshotNodeRecord, 'backendNodeId'>): SnapshotNodeRecord {
  return {
    documentIndex: 0,
    parentBackendNodeId: null,
    nodeName: 'DIV',
    id: null,
    hasLayout: true,
    stackingContext: false,
    paintOrder: null,
    position: 'static',
    zIndex: 'auto',
    display: 'block',
    overlay: 'none',
    ...partial,
  };
}

describe('StackingBoundary', () => {
  it('explains the canonical trapped modal from sibling z-index, not from paint order', () => {
    const chain = buildStackingChain(source(4, 'div#modal', {position: 'absolute', zIndex: '9999'}), [
      source(3, 'div#content', {
        position: 'relative',
        transform: 'matrix(1, 0, 0, 1, 0, 0)',
        declarations: {transform: {id: 9, value: 'translateZ(0)', hasSource: true}},
      }),
      source(1, 'document', {isDocumentElement: true, display: 'block'}),
    ]);
    const records = [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, display: 'block', paintOrder: 1}),
      record({
        backendNodeId: 2,
        id: 'header',
        parentBackendNodeId: 1,
        stackingContext: true,
        position: 'relative',
        zIndex: '10',
        paintOrder: 4,
      }),
      record({
        backendNodeId: 3,
        id: 'content',
        parentBackendNodeId: 1,
        stackingContext: true,
        position: 'relative',
        zIndex: 'auto',
        paintOrder: 2,
      }),
      record({
        backendNodeId: 4,
        id: 'modal',
        parentBackendNodeId: 3,
        stackingContext: true,
        position: 'absolute',
        zIndex: '9999',
        paintOrder: 3,
      }),
    ];
    const boundary = explainStackingBoundary(chain, records);
    assert.strictEqual(boundary?.trapLabel, 'div#content');
    assert.strictEqual(boundary?.trapText, 'transform: translateZ(0)');
    assert.strictEqual(boundary?.otherLabel, 'div#header');
    assert.strictEqual(boundary?.otherText, 'z-index: 10');
    assert.strictEqual(boundary?.order.kind, 'z-index');
    assert.strictEqual(boundary?.trapDeclarationId, 9);
  });

  it('uses paint order for same-level sibling order and does not treat that as a covering element', () => {
    const chain = buildStackingChain(source(4, 'div#modal', {position: 'absolute', zIndex: '1'}), [
      source(3, 'div#content', {position: 'relative', transform: 'matrix(1, 0, 0, 1, 0, 0)'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const records = [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true}),
      record({
        backendNodeId: 2,
        id: 'header',
        parentBackendNodeId: 1,
        stackingContext: true,
        position: 'relative',
        zIndex: 'auto',
        paintOrder: 30,
      }),
      record({
        backendNodeId: 3,
        id: 'content',
        parentBackendNodeId: 1,
        stackingContext: true,
        position: 'relative',
        zIndex: 'auto',
        paintOrder: 2,
      }),
      record({backendNodeId: 4, id: 'modal', parentBackendNodeId: 3, stackingContext: true, position: 'absolute', zIndex: '1'}),
    ];
    const boundary = explainStackingBoundary(chain, records);
    assert.strictEqual(boundary?.order.kind, 'paint-order');
    assert.strictEqual(boundary?.otherLabel, 'div#header');
    assert.strictEqual(boundary?.otherText, '');
  });

  it('does not emit a boundary for the trapping context itself', () => {
    const chain = buildStackingChain(
        source(3, 'div#content', {position: 'relative', transform: 'matrix(1, 0, 0, 1, 0, 0)'}),
        [source(1, 'document', {isDocumentElement: true})]);
    const records = [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true}),
      record({backendNodeId: 2, id: 'header', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '10'}),
      record({backendNodeId: 3, id: 'content', parentBackendNodeId: 1, stackingContext: true, position: 'relative'}),
    ];
    assert.strictEqual(explainStackingBoundary(chain, records), null);
  });

  it('names the later sibling with the greatest z-index', () => {
    const chain = buildStackingChain(source(4, 'div#modal', {position: 'absolute', zIndex: '9999'}), [
      source(3, 'div#content', {position: 'relative', transform: 'matrix(1, 0, 0, 1, 0, 0)'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const records = [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true}),
      record({backendNodeId: 2, id: 'header', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '10'}),
      record({backendNodeId: 5, id: 'sibling-a', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '1'}),
      record({backendNodeId: 3, id: 'content', parentBackendNodeId: 1, stackingContext: true, position: 'relative'}),
      record({backendNodeId: 4, id: 'modal', parentBackendNodeId: 3, stackingContext: true, position: 'absolute', zIndex: '9999'}),
    ];
    assert.strictEqual(explainStackingBoundary(chain, records)?.otherLabel, 'div#header');
  });

  it('declines when two sibling contexts share the greatest higher z-index even if paint order differs', () => {
    const chain = buildStackingChain(source(4, 'div#modal', {position: 'absolute', zIndex: '9999'}), [
      source(3, 'div#content', {transform: 'matrix(1, 0, 0, 1, 0, 0)'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const records = [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true}),
      record({backendNodeId: 2, id: 'a', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '10', paintOrder: 4}),
      record({backendNodeId: 5, id: 'b', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '10', paintOrder: 8}),
      record({backendNodeId: 3, id: 'content', parentBackendNodeId: 1, stackingContext: true}),
      record({backendNodeId: 4, id: 'modal', parentBackendNodeId: 3, stackingContext: true, position: 'absolute', zIndex: '9999'}),
    ];
    assert.strictEqual(explainStackingBoundary(chain, records), null);
  });

  it('orders an equal auto sibling by paint order, following tree order rather than DOM assumptions', () => {
    const chain = buildStackingChain(source(4, 'div#child', {position: 'relative', zIndex: '4'}), [
      source(3, 'div#early', {opacity: '0.9'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const records = [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, paintOrder: 1}),
      record({backendNodeId: 3, id: 'early', parentBackendNodeId: 1, stackingContext: true, paintOrder: 8}),
      record({backendNodeId: 5, id: 'later', parentBackendNodeId: 1, stackingContext: true, paintOrder: 9}),
      record({backendNodeId: 4, id: 'child', parentBackendNodeId: 3, stackingContext: true, position: 'relative', zIndex: '4', paintOrder: 8}),
    ];
    const boundary = explainStackingBoundary(chain, records);
    assert.strictEqual(boundary?.order.kind, 'paint-order');
    assert.strictEqual(boundary?.otherLabel, 'div#later');
    assert.strictEqual(boundary?.otherText, '');
  });

  it('orders z-index 0 and auto by paint order, so 0 is not treated as later by itself', () => {
    const chain = buildStackingChain(source(4, 'div#child'), [
      source(3, 'div#zero', {position: 'relative', zIndex: '0'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const autoLater = explainStackingBoundary(chain, [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, paintOrder: 1}),
      record({backendNodeId: 3, id: 'zero', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '0', paintOrder: 19}),
      record({backendNodeId: 5, id: 'auto', parentBackendNodeId: 1, stackingContext: true, paintOrder: 20}),
      record({backendNodeId: 4, id: 'child', parentBackendNodeId: 3, stackingContext: false, paintOrder: 19}),
    ]);
    assert.strictEqual(autoLater?.order.kind, 'paint-order');
    assert.strictEqual(autoLater?.otherLabel, 'div#auto');

    const zeroLater = explainStackingBoundary(
        buildStackingChain(source(4, 'div#child'), [
          source(3, 'div#auto', {opacity: '0.8'}),
          source(1, 'document', {isDocumentElement: true}),
        ]),
        [
          record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, paintOrder: 1}),
          record({backendNodeId: 3, id: 'auto', parentBackendNodeId: 1, stackingContext: true, paintOrder: 16}),
          record({backendNodeId: 5, id: 'zero', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '0', paintOrder: 17}),
          record({backendNodeId: 4, id: 'child', parentBackendNodeId: 3, stackingContext: false, paintOrder: 16}),
        ]);
    assert.strictEqual(zeroLater?.otherLabel, 'div#zero');
    assert.strictEqual(zeroLater?.order.kind, 'paint-order');
  });

  it('uses paint order when flex order puts the DOM-first context later', () => {
    const chain = buildStackingChain(source(4, 'div#child'), [
      source(3, 'div#flex-dom-second', {opacity: '0.9'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const boundary = explainStackingBoundary(chain, [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, paintOrder: 1}),
      record({backendNodeId: 5, id: 'flex-dom-first', parentBackendNodeId: 1, stackingContext: true, paintOrder: 34}),
      record({backendNodeId: 3, id: 'flex-dom-second', parentBackendNodeId: 1, stackingContext: true, paintOrder: 33}),
      record({backendNodeId: 4, id: 'child', parentBackendNodeId: 3, stackingContext: false, paintOrder: 33}),
    ]);
    assert.strictEqual(boundary?.otherLabel, 'div#flex-dom-first');
    assert.strictEqual(boundary?.order.kind, 'paint-order');
  });

  it('keeps a strictly greater z-index ahead of a same-level context that paint order also places later', () => {
    const chain = buildStackingChain(source(4, 'div#modal', {position: 'absolute', zIndex: '9999'}), [
      source(3, 'div#content', {opacity: '0.9'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const boundary = explainStackingBoundary(chain, [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, paintOrder: 1}),
      record({backendNodeId: 6, id: 'same', parentBackendNodeId: 1, stackingContext: true, paintOrder: 12}),
      record({backendNodeId: 2, id: 'header', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '10', paintOrder: 35}),
      record({backendNodeId: 3, id: 'content', parentBackendNodeId: 1, stackingContext: true, paintOrder: 10}),
      record({backendNodeId: 4, id: 'modal', parentBackendNodeId: 3, stackingContext: true, position: 'absolute', zIndex: '9999', paintOrder: 11}),
    ]);
    assert.strictEqual(boundary?.otherLabel, 'div#header');
    assert.strictEqual(boundary?.order.kind, 'z-index');
  });

  it('does not treat paint order 0 or a shared paint order as a later sibling', () => {
    const chain = buildStackingChain(source(4, 'div#child'), [
      source(3, 'div#early', {opacity: '0.9'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const missing = explainStackingBoundary(chain, [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, paintOrder: 1}),
      record({backendNodeId: 3, id: 'early', parentBackendNodeId: 1, stackingContext: true, paintOrder: 8}),
      record({backendNodeId: 5, id: 'missing', parentBackendNodeId: 1, stackingContext: true, paintOrder: 0}),
      record({backendNodeId: 4, id: 'child', parentBackendNodeId: 3, stackingContext: false, paintOrder: 8}),
    ]);
    assert.strictEqual(missing, null);

    const shared = explainStackingBoundary(chain, [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, paintOrder: 1}),
      record({backendNodeId: 3, id: 'early', parentBackendNodeId: 1, stackingContext: true, paintOrder: 8}),
      record({backendNodeId: 5, id: 'same', parentBackendNodeId: 1, stackingContext: true, paintOrder: 8}),
      record({backendNodeId: 4, id: 'child', parentBackendNodeId: 3, stackingContext: false, paintOrder: 8}),
    ]);
    assert.strictEqual(shared, null);
  });

  it('does not promote a lower z-index sibling because its paint order is higher', () => {
    const chain = buildStackingChain(source(4, 'div#child'), [
      source(3, 'div#content', {opacity: '0.9'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const boundary = explainStackingBoundary(chain, [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, paintOrder: 1}),
      record({backendNodeId: 3, id: 'content', parentBackendNodeId: 1, stackingContext: true, paintOrder: 10}),
      record({backendNodeId: 5, id: 'neg', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '-1', paintOrder: 40}),
      record({backendNodeId: 4, id: 'child', parentBackendNodeId: 3, stackingContext: false, paintOrder: 10}),
    ]);
    assert.strictEqual(boundary, null);
  });

  it('does not name a top-layer sibling from paint order', () => {
    const chain = buildStackingChain(source(4, 'div#child'), [
      source(3, 'div#content', {transform: 'matrix(1, 0, 0, 1, 0, 0)'}),
      source(1, 'document', {isDocumentElement: true}),
    ]);
    const boundary = explainStackingBoundary(chain, [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true, paintOrder: 1}),
      record({backendNodeId: 3, id: 'content', parentBackendNodeId: 1, stackingContext: true, paintOrder: 41}),
      record({
        backendNodeId: 6,
        id: 'dlg',
        nodeName: 'DIALOG',
        parentBackendNodeId: 1,
        stackingContext: true,
        position: 'fixed',
        zIndex: 'auto',
        overlay: 'auto',
        paintOrder: 52,
      }),
      record({backendNodeId: 4, id: 'child', parentBackendNodeId: 3, stackingContext: false, paintOrder: 41}),
    ]);
    assert.strictEqual(boundary, null);
  });

  it('does not compare z-index across an iframe document boundary', () => {
    const chain = buildStackingChain(source(4, 'div#frame-card', {position: 'relative', zIndex: '5'}), [
      source(3, 'document', {isDocumentElement: true}),
    ]);
    const records = [
      record({backendNodeId: 1, documentIndex: 0, nodeName: 'HTML', stackingContext: true}),
      record({
        backendNodeId: 2,
        documentIndex: 0,
        id: 'header',
        parentBackendNodeId: 1,
        stackingContext: true,
        position: 'relative',
        zIndex: '10',
      }),
      record({backendNodeId: 3, documentIndex: 1, nodeName: 'HTML', stackingContext: true}),
      record({
        backendNodeId: 4,
        documentIndex: 1,
        id: 'frame-card',
        parentBackendNodeId: 3,
        stackingContext: true,
        position: 'relative',
        zIndex: '5',
      }),
    ];
    assert.strictEqual(explainStackingBoundary(chain, records), null);
  });

  it('does not treat a top-layer element as trapped by a document sibling', () => {
    const chain = buildStackingChain(
        source(4, 'dialog#dlg', {position: 'fixed'}),
        [source(1, 'document', {isDocumentElement: true})]);
    chain.selected.topLayer = true;
    const records = [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true}),
      record({backendNodeId: 2, id: 'header', parentBackendNodeId: 1, stackingContext: true, position: 'relative', zIndex: '10'}),
      record({backendNodeId: 4, id: 'dlg', nodeName: 'DIALOG', parentBackendNodeId: 1, stackingContext: true, position: 'fixed', overlay: 'auto'}),
    ];
    assert.strictEqual(explainStackingBoundary(chain, records), null);
  });

  it('marks a snapshot-only stacking context as internal and drops a predicate the bit rejects', () => {
    const chain = buildStackingChain(source(2, 'div#mystery'), [source(1, 'document', {isDocumentElement: true})]);
    const confirmed = applySnapshot(chain, [
      record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true}),
      record({backendNodeId: 2, id: 'mystery', parentBackendNodeId: 1, stackingContext: true}),
    ]);
    assert.strictEqual(confirmed.selected.createsStackingContext, true);
    assert.deepEqual(confirmed.selected.reasons.map(reason => reason.property), ['internal']);
    assert.strictEqual(confirmed.selected.reasons[0].confidence, 'limited');

    const rejected = applySnapshot(
        buildStackingChain(source(2, 'div#opacity', {opacity: '0.5'}), [source(1, 'document', {isDocumentElement: true})]),
        [
          record({backendNodeId: 1, nodeName: 'HTML', stackingContext: true}),
          record({backendNodeId: 2, id: 'opacity', parentBackendNodeId: 1, stackingContext: false}),
        ]);
    assert.strictEqual(rejected.selected.createsStackingContext, false);
    assert.deepEqual(rejected.selected.reasons, []);
  });

  it('reads the stacking bit and paint order from a captureSnapshot payload', () => {
    const strings = ['HTML', 'DIV', 'id', 'header', 'static', 'relative', 'auto', '10', 'block', 'none'];
    const documents: SnapshotDocumentData[] = [{
      nodes: {
        parentIndex: [-1, 0],
        nodeName: [0, 1],
        backendNodeId: [1, 2],
        attributes: [[], [2, 3]],
      },
      layout: {
        nodeIndex: [0, 1],
        styles: [[4, 6, 8, 9], [5, 7, 8, 9]],
        stackingContexts: {index: [0, 1]},
        paintOrders: [1, 4],
      },
    }];
    const records = readSnapshot(documents, strings);
    assert.strictEqual(records[1].id, 'header');
    assert.strictEqual(records[1].stackingContext, true);
    assert.strictEqual(records[1].paintOrder, 4);
    assert.strictEqual(records[1].zIndex, '10');
    assert.strictEqual(records[1].parentBackendNodeId, 1);
  });
});
