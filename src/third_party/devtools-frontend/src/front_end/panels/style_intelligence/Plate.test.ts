// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {ChainRoleKind, type ChainRole, type ChainRung} from './ElementChain.js';
import {Confidence, type AxisExplanation, Mechanism} from './LayoutExplanation.js';
import {buildPlate, type PlateInput} from './Plate.js';
import {SpacingAxis, SpacingRelationshipKind, type SpacingRelationship} from './SpacingExplanation.js';

function role(kind: ChainRoleKind, overrides: Partial<ChainRole> = {}): ChainRole {
  return {
    kind,
    effect: 'effect',
    declaration: null,
    declarationId: null,
    hasSource: false,
    measurement: null,
    warn: false,
    ...overrides,
  };
}

function rung(label: string, kinds: ChainRoleKind[], overrides: Partial<ChainRung> = {}): ChainRung {
  const roles = kinds.map(kind => role(kind));
  return {nodeId: 1, label, roles, warn: roles.some(item => item.warn), ...overrides};
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

function spacing(overrides: Partial<SpacingRelationship> = {}): SpacingRelationship {
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

function input(overrides: Partial<PlateInput> = {}): PlateInput {
  return {
    selectedNodeId: 10,
    rungs: [],
    geometry: {width: 312, height: 176},
    axes: [],
    spacing: [],
    ...overrides,
  };
}

describe('Plate', () => {
  it('draws containers as frames and leaves a sibling out of them', () => {
    const plate = buildPlate(input({
      rungs: [
        rung('main.feed', [ChainRoleKind.SCROLL_CONTAINER], {nodeId: 2}),
        rung('div.card', [ChainRoleKind.CONTAINING_BLOCK], {nodeId: 5}),
        rung('div.card__header', [ChainRoleKind.SIBLING], {nodeId: 21}),
      ],
    }));

    assert.deepStrictEqual(plate?.frames.map(frame => frame.label), ['main.feed', 'div.card']);
    assert.strictEqual(plate?.elidedFrames, 0);
  });

  it('elides the outermost frames past the cap and counts them', () => {
    const plate = buildPlate(input({
      rungs: [
        rung('html', [ChainRoleKind.SCROLL_CONTAINER], {nodeId: 1}),
        rung('main.feed', [ChainRoleKind.SCROLL_CONTAINER], {nodeId: 2}),
        rung('div.card', [ChainRoleKind.CONTAINING_BLOCK], {nodeId: 5}),
        rung('div.card__body', [ChainRoleKind.LAYOUT_PARENT], {nodeId: 8}),
        rung('div.inner', [ChainRoleKind.LAYOUT_PARENT], {nodeId: 9}),
      ],
    }));

    assert.deepStrictEqual(plate?.frames.map(frame => frame.label), ['div.card', 'div.card__body', 'div.inner']);
    assert.strictEqual(plate?.elidedFrames, 2);
  });

  it('names a frame after its warning role rather than the role that sorted it', () => {
    const warned: ChainRung = {
      nodeId: 5,
      label: 'div.card',
      roles: [
        role(ChainRoleKind.CONTAINING_BLOCK),
        role(ChainRoleKind.STACKING_CONTEXT, {warn: true}),
      ],
      warn: true,
    };
    const plate = buildPlate(input({rungs: [warned]}));

    assert.strictEqual(plate?.frames[0].role, 'stacking context');
    assert.isTrue(plate?.frames[0].warn);
  });

  it('prefers a previous sibling over the parent edge for the gap above', () => {
    const plate = buildPlate(input({
      spacing: [
        spacing({relationship: SpacingRelationshipKind.PARENT_START, neighbor: null, measuredGap: 4}),
        spacing({measuredGap: 24}),
      ],
    }));

    assert.strictEqual(plate?.gapAbove?.value, 24);
    assert.isTrue(plate?.gapAbove?.sibling);
    assert.strictEqual(plate?.gapAbove?.neighborLabel, 'div.card__header');
  });

  it('ignores a horizontal gap when drawing the gap above', () => {
    const plate = buildPlate(input({
      spacing: [spacing({axis: SpacingAxis.HORIZONTAL})],
    }));

    assert.isNull(plate?.gapAbove ?? null);
  });

  it('labels the gap with the authored token when it differs from the used pixels', () => {
    const plate = buildPlate(input({
      spacing: [spacing({
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

    assert.strictEqual(plate?.gapAbove?.detail, 'var(--card-gap)');
  });

  it('names how the width was arrived at, not the controller’s property name', () => {
    const plate = buildPlate(input({
      axes: [axis({
        mechanism: Mechanism.FLEX_STRETCH,
        controller: {nodeId: 8, label: 'div.card__body', property: 'display', value: 'flex', declarationId: 4, hasSource: true},
      })],
    }));

    assert.strictEqual(plate?.widthLabel, '312 · stretched');
  });

  it('falls back to the controller when the mechanism does not describe a width', () => {
    const plate = buildPlate(input({
      axes: [axis({
        mechanism: Mechanism.STICKY,
        controller: {nodeId: 8, label: 'div.card__body', property: 'align-items', value: 'stretch', declarationId: 4, hasSource: true},
      })],
    }));

    assert.strictEqual(plate?.widthLabel, '312 · align-items');
  });

  it('reports the element’s own padding and ignores a neighbour’s', () => {
    const plate = buildPlate(input({
      spacing: [spacing({
        contributors: [
          {
            type: 'padding',
            value: 16,
            nodeId: 10,
            label: 'div.card__media',
            property: 'padding-bottom',
            declaredValue: '16px',
            declarationId: 11,
            hasSource: true,
            confidence: Confidence.HIGH,
            countsTowardGap: true,
          },
          {
            type: 'padding',
            value: 48,
            nodeId: 21,
            label: 'div.card__header',
            property: 'padding-top',
            declaredValue: '48px',
            declarationId: 12,
            hasSource: true,
            confidence: Confidence.HIGH,
            countsTowardGap: true,
          },
        ],
      })],
    }));

    assert.strictEqual(plate?.paddingLabel, 'padding 16');
  });

  it('collapses to no plate when there is nothing to draw', () => {
    assert.isNull(buildPlate(input({geometry: null})));
  });

  it('still draws the element alone when it has no ancestors with a role', () => {
    const plate = buildPlate(input());

    assert.isEmpty(plate?.frames ?? ['unexpected']);
    assert.strictEqual(plate?.sizeLabel, '312 × 176');
  });
});
