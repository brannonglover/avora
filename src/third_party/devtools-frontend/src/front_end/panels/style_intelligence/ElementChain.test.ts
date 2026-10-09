// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {buildElementChain, type ChainInput, ChainRoleKind, chainRungLabel} from './ElementChain.js';
import {Confidence, type AxisExplanation, Mechanism} from './LayoutExplanation.js';
import {type StickyInsets} from './LayoutRelationshipsPresentation.js';
import {SpacingAxis, SpacingRelationshipKind, type SpacingRelationship} from './SpacingExplanation.js';

function insets(overrides: Partial<StickyInsets> = {}): StickyInsets {
  return {top: null, right: null, bottom: null, left: null, ...overrides};
}

function axis(overrides: Partial<AxisExplanation> = {}): AxisExplanation {
  return {
    axis: 'width',
    usedValue: 312,
    mechanism: Mechanism.FLEX_STRETCH,
    confidence: Confidence.HIGH,
    controller: null,
    contributions: [],
    includesPadding: false,
    includesBorder: false,
    specifiedMatchesContentBox: false,
    containingBlockUnknown: false,
    ...overrides,
  };
}

function sibling(overrides: Partial<SpacingRelationship> = {}): SpacingRelationship {
  return {
    measuredGap: 24,
    axis: SpacingAxis.VERTICAL,
    relationship: SpacingRelationshipKind.PREVIOUS_SIBLING,
    neighbor: {nodeId: 21, label: 'div.card__header'},
    contributors: [],
    unexplained: 0,
    marginCollapseUnknown: false,
    ...overrides,
  };
}

function input(overrides: Partial<ChainInput> = {}): ChainInput {
  return {
    selectedNodeId: 10,
    relationships: null,
    labels: new Map(),
    insets: insets(),
    axes: [],
    spacing: [],
    stacking: null,
    ...overrides,
  };
}

describe('ElementChain', () => {
  it('gives a node holding two roles one rung carrying both', () => {
    const rungs = buildElementChain(input({
      relationships: {
        containingBlock: {kind: 'element', nodeId: 5, fixedToViewport: false},
        scrollContainer: null,
        stickyScrollContainers: null,
      },
      labels: new Map([[5, 'div.card']]),
      stacking: {
        nodeId: 5,
        label: 'div.card',
        declaration: 'transform: translateZ(0)',
        declarationId: 3,
        hasSource: true,
        trapped: true,
        trappedZIndex: '40',
      },
    }));

    assert.lengthOf(rungs, 1);
    assert.strictEqual(rungs[0].nodeId, 5);
    assert.deepStrictEqual(rungs[0].roles.map(role => role.kind), [
      ChainRoleKind.CONTAINING_BLOCK,
      ChainRoleKind.STACKING_CONTEXT,
    ]);
    assert.strictEqual(chainRungLabel(rungs[0]), 'Containing block + Stacking context');
    assert.isTrue(rungs[0].warn);
  });

  it('orders rungs by containment, outermost first', () => {
    const rungs = buildElementChain(input({
      relationships: {
        containingBlock: {kind: 'element', nodeId: 5, fixedToViewport: false},
        scrollContainer: {kind: 'element', nodeId: 2, fixedToViewport: false},
        stickyScrollContainers: null,
      },
      labels: new Map([[2, 'main.feed'], [5, 'div.card']]),
      axes: [axis({controller: {nodeId: 8, label: 'div.card__body', property: 'align-items', value: 'stretch', declarationId: 4, hasSource: true}})],
      spacing: [sibling()],
    }));

    assert.deepStrictEqual(rungs.map(rung => rung.label), [
      'main.feed',
      'div.card',
      'div.card__body',
      'div.card__header',
    ]);
  });

  it('never reports the selected element as its own ancestor', () => {
    const rungs = buildElementChain(input({
      selectedNodeId: 5,
      relationships: {
        containingBlock: {kind: 'element', nodeId: 5, fixedToViewport: false},
        scrollContainer: null,
        stickyScrollContainers: null,
      },
      labels: new Map([[5, 'div.card']]),
      axes: [axis({controller: {nodeId: 5, label: 'div.card', property: 'width', value: '312px', declarationId: 1, hasSource: true}})],
    }));

    assert.isEmpty(rungs);
  });

  it('does not restate a sticky container as a plain scroll container', () => {
    const rungs = buildElementChain(input({
      relationships: {
        containingBlock: null,
        scrollContainer: {kind: 'element', nodeId: 2, fixedToViewport: false},
        stickyScrollContainers: {
          horizontal: null,
          vertical: {kind: 'element', nodeId: 2, fixedToViewport: false},
        },
      },
      labels: new Map([[2, 'main.feed']]),
      insets: insets({top: '0px'}),
    }));

    assert.lengthOf(rungs, 1);
    assert.deepStrictEqual(rungs[0].roles.map(role => role.kind), [ChainRoleKind.STICKY_CONTAINER]);
    assert.strictEqual(rungs[0].roles[0].measurement, 'top: 0px');
  });

  it('keeps distinct sticky axes as separate rungs', () => {
    const rungs = buildElementChain(input({
      relationships: {
        containingBlock: null,
        scrollContainer: null,
        stickyScrollContainers: {
          horizontal: {kind: 'element', nodeId: 3, fixedToViewport: false},
          vertical: {kind: 'element', nodeId: 2, fixedToViewport: false},
        },
      },
      labels: new Map([[2, 'main.feed'], [3, 'div.rail']]),
      insets: insets({top: '0px', left: '8px'}),
    }));

    assert.deepStrictEqual(rungs.map(rung => rung.label), ['main.feed', 'div.rail']);
    assert.strictEqual(rungs[0].roles[0].measurement, 'top: 0px');
    assert.strictEqual(rungs[1].roles[0].measurement, 'left: 8px');
  });

  it('states a stacking context without claiming a trap until one is confirmed', () => {
    const rungs = buildElementChain(input({
      stacking: {
        nodeId: 5,
        label: 'div.card',
        declaration: 'transform: translateZ(0)',
        declarationId: 3,
        hasSource: true,
        trapped: false,
        trappedZIndex: null,
      },
    }));

    assert.lengthOf(rungs, 1);
    assert.strictEqual(rungs[0].roles[0].effect, 'it is painted as one unit');
    assert.isFalse(rungs[0].warn);
  });

  it('names the trapped z-index once a boundary is confirmed', () => {
    const rungs = buildElementChain(input({
      stacking: {
        nodeId: 5,
        label: 'div.card',
        declaration: 'transform: translateZ(0)',
        declarationId: 3,
        hasSource: true,
        trapped: true,
        trappedZIndex: '40',
      },
    }));

    assert.include(rungs[0].roles[0].effect, 'traps your z-index: 40');
    assert.isTrue(rungs[0].roles[0].warn);
  });

  it('labels a sibling gap with the authored token rather than the used pixels', () => {
    const rungs = buildElementChain(input({
      spacing: [sibling({
        contributors: [{
          type: 'margin',
          value: 24,
          nodeId: 21,
          label: 'div.card__header',
          property: 'margin-bottom',
          declaredValue: 'var(--card-gap)',
          declarationId: 9,
          hasSource: true,
          confidence: Confidence.HIGH,
          countsTowardGap: true,
        }],
      })],
    }));

    assert.lengthOf(rungs, 1);
    assert.strictEqual(rungs[0].roles[0].measurement, '24px');
    assert.strictEqual(rungs[0].roles[0].declaration, 'margin-bottom: var(--card-gap)');
  });

  it('drops a layout-parent controller that names no element', () => {
    const rungs = buildElementChain(input({
      axes: [axis({controller: {nodeId: 0, label: '', property: null, value: null, declarationId: null, hasSource: false}})],
    }));

    assert.isEmpty(rungs);
  });

  it('labels a viewport containing block without a node', () => {
    const rungs = buildElementChain(input({
      relationships: {
        containingBlock: {kind: 'initial-containing-block', nodeId: null, fixedToViewport: false},
        scrollContainer: null,
        stickyScrollContainers: null,
      },
    }));

    assert.lengthOf(rungs, 1);
    assert.strictEqual(rungs[0].label, 'viewport');
    assert.isNull(rungs[0].nodeId);
  });
});
