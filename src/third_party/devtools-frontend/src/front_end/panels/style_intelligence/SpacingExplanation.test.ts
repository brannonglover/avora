// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {explainSpacing, presentSpacing, SpacingAxis, SpacingDeclineReason, SpacingRelationshipKind, type SpacingBox, type SpacingInput, type SpacingParent} from './SpacingExplanation.js';

function box(overrides: Partial<SpacingBox> = {}): SpacingBox {
  return {
    nodeId: 1,
    label: 'div',
    x: 0,
    y: 0,
    width: 100,
    height: 20,
    display: 'block',
    position: 'static',
    transformNone: true,
    marginTop: 0,
    marginRight: 0,
    marginBottom: 0,
    marginLeft: 0,
    marginTopDeclaration: null,
    marginRightDeclaration: null,
    marginBottomDeclaration: null,
    marginLeftDeclaration: null,
    ...overrides,
  };
}

function parent(overrides: Partial<SpacingParent> = {}): SpacingParent {
  return {
    nodeId: 7,
    label: 'div#page',
    hasBox: true,
    display: 'block',
    flexDirection: null,
    flexWrap: 'nowrap',
    writingMode: 'horizontal-tb',
    x: 0,
    y: 0,
    width: 400,
    height: 400,
    borderTop: 0,
    borderRight: 0,
    borderBottom: 0,
    borderLeft: 0,
    paddingTop: 0,
    paddingRight: 0,
    paddingBottom: 0,
    paddingLeft: 0,
    rowGap: null,
    columnGap: null,
    rowGapDeclaration: null,
    columnGapDeclaration: null,
    paddingTopDeclaration: null,
    paddingRightDeclaration: null,
    paddingBottomDeclaration: null,
    paddingLeftDeclaration: null,
    ...overrides,
  };
}

function input(overrides: Partial<SpacingInput> = {}): SpacingInput {
  return {
    selected: box({nodeId: 2, label: 'div#selected', y: 52}),
    parent: parent(),
    siblings: [],
    siblingsIncomplete: false,
    ...overrides,
  };
}

describe('explainSpacing', () => {
  it('measures a vertical sibling gap and keeps an unexplained remainder', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'flex', flexDirection: 'column', rowGap: 16, rowGapDeclaration: {id: 4, property: 'row-gap', value: '16px', hasSource: true}}),
      selected: box({
        nodeId: 2,
        label: 'div#selected',
        y: 68,
        marginTop: 8,
        marginTopDeclaration: {id: 5, property: 'margin-top', value: '8px', hasSource: true},
      }),
      siblings: [box({nodeId: 3, label: 'div#before', y: 0, height: 20})],
    }));

    const above = result.relationships.find(item => item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING &&
                                             item.axis === SpacingAxis.VERTICAL);
    assert.isDefined(above);
    assert.strictEqual(above?.measuredGap, 48);
    assert.strictEqual(above?.neighbor?.nodeId, 3);
    assert.deepEqual(above?.contributors.map(item => item.value), [16, 8]);
    assert.strictEqual(above?.unexplained, 24);
    assert.isFalse(above?.marginCollapseUnknown);
    assert.strictEqual(presentSpacing(above!).unexplained, '24px could not be attributed confidently.');
  });

  it('attributes a flex column gap and the facing margins without a remainder', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'flex', flexDirection: 'column', rowGap: 16}),
      selected: box({nodeId: 2, y: 44, marginTop: 8}),
      siblings: [box({nodeId: 3, y: 0, height: 20, marginBottom: 0})],
    }));
    const above = result.relationships.find(item => item.axis === SpacingAxis.VERTICAL &&
                                             item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING);
    assert.strictEqual(above?.measuredGap, 24);
    assert.deepEqual(above?.contributors.map(item => [item.type, item.property, item.value]), [
      ['gap', 'row-gap', 16],
      ['margin', 'margin-top', 8],
    ]);
    assert.strictEqual(above?.unexplained, 0);
  });

  it('attributes column-gap for a horizontal flex row', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'flex', flexDirection: 'row', columnGap: 16}),
      selected: box({nodeId: 2, x: 116, y: 0}),
      siblings: [box({nodeId: 3, x: 0, y: 0, width: 100})],
    }));
    const before = result.relationships.find(item => item.axis === SpacingAxis.HORIZONTAL &&
                                              item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING);
    assert.strictEqual(before?.measuredGap, 16);
    assert.strictEqual(before?.contributors[0]?.property, 'column-gap');
    assert.strictEqual(before?.unexplained, 0);
  });

  it('attributes grid row and column gaps from visual neighbors', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'grid', rowGap: 12, columnGap: 20}),
      selected: box({nodeId: 2, x: 120, y: 32, width: 100, height: 20}),
      siblings: [
        box({nodeId: 3, label: 'div#above', x: 120, y: 0, width: 100, height: 20}),
        box({nodeId: 4, label: 'div#left', x: 0, y: 32, width: 100, height: 20}),
      ],
    }));
    const above = result.relationships.find(item => item.axis === SpacingAxis.VERTICAL &&
                                             item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING);
    const left = result.relationships.find(item => item.axis === SpacingAxis.HORIZONTAL &&
                                            item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING);
    assert.strictEqual(above?.measuredGap, 12);
    assert.strictEqual(above?.contributors[0]?.property, 'row-gap');
    assert.strictEqual(above?.neighbor?.nodeId, 3);
    assert.strictEqual(left?.measuredGap, 20);
    assert.strictEqual(left?.contributors[0]?.property, 'column-gap');
    assert.strictEqual(left?.neighbor?.nodeId, 4);
  });

  it('does not treat DOM order as visual order', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'flex', flexDirection: 'row', columnGap: 8}),
      selected: box({nodeId: 2, label: 'div#selected', x: 108, y: 0}),
      siblings: [
        box({nodeId: 3, label: 'div#dom-first', x: 216, y: 0}),
        box({nodeId: 4, label: 'div#visual-previous', x: 0, y: 0}),
      ],
    }));
    const before = result.relationships.find(item => item.axis === SpacingAxis.HORIZONTAL &&
                                              item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING);
    assert.strictEqual(before?.neighbor?.nodeId, 4);
    assert.strictEqual(before?.measuredGap, 8);
  });

  it('attributes parent padding when it separates the child from the parent edge', () => {
    const result = explainSpacing(input({
      parent: parent({
        paddingTop: 16,
        paddingTopDeclaration: {id: 9, property: 'padding-top', value: '16px', hasSource: true},
      }),
      selected: box({nodeId: 2, y: 16}),
      siblings: [],
    }));
    const inside = result.relationships.find(item => item.relationship === SpacingRelationshipKind.PARENT_START &&
                                              item.axis === SpacingAxis.VERTICAL);
    assert.strictEqual(inside?.measuredGap, 16);
    assert.strictEqual(inside?.contributors[0]?.type, 'padding');
    assert.strictEqual(inside?.contributors[0]?.value, 16);
    assert.strictEqual(inside?.unexplained, 0);
    assert.isFalse(inside?.marginCollapseUnknown);
  });

  it('does not claim collapsed block margins', () => {
    const result = explainSpacing(input({
      selected: box({
        nodeId: 2,
        y: 40,
        marginTop: 30,
        marginTopDeclaration: {id: 5, property: 'margin-top', value: '30px', hasSource: true},
      }),
      siblings: [box({
        nodeId: 3,
        y: 0,
        height: 20,
        marginBottom: 20,
        marginBottomDeclaration: {id: 6, property: 'margin-bottom', value: '20px', hasSource: true},
      })],
    }));
    const above = result.relationships.find(item => item.axis === SpacingAxis.VERTICAL &&
                                             item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING);
    assert.strictEqual(above?.measuredGap, 20);
    assert.deepEqual(above?.contributors, []);
    assert.isTrue(above?.marginCollapseUnknown);
    assert.strictEqual(above?.unexplained, 20);
    assert.match(presentSpacing(above!).marginCollapse ?? '', /collapsed contribution cannot be determined/);
    assert.isNull(presentSpacing(above!).unexplained);
  });

  it('counts a negative flex margin as overlap', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'flex', flexDirection: 'row', columnGap: 0}),
      selected: box({nodeId: 2, x: 92, y: 0, marginLeft: -8}),
      siblings: [box({nodeId: 3, x: 0, y: 0, width: 100})],
    }));
    const before = result.relationships.find(item => item.axis === SpacingAxis.HORIZONTAL &&
                                              item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING);
    assert.strictEqual(before?.measuredGap, -8);
    assert.strictEqual(before?.contributors[0]?.property, 'margin-left');
    assert.strictEqual(before?.contributors[0]?.value, -8);
    assert.strictEqual(before?.unexplained, 0);
  });

  it('declines when the selected box is out of flow, transformed, or siblings are incomplete', () => {
    const absolute = explainSpacing(input({selected: box({position: 'absolute'})}));
    assert.deepEqual(absolute.relationships, []);
    assert.strictEqual(absolute.declined[0]?.reason, SpacingDeclineReason.OUT_OF_FLOW);

    const transformed = explainSpacing(input({selected: box({transformNone: false})}));
    assert.strictEqual(transformed.declined[0]?.reason, SpacingDeclineReason.TRANSFORMED);

    const incomplete = explainSpacing(input({siblingsIncomplete: true, siblings: [box({nodeId: 3, y: 0})]}));
    assert.deepEqual(incomplete.relationships, []);
    assert.strictEqual(incomplete.declined[0]?.reason, SpacingDeclineReason.SIBLINGS_INCOMPLETE);
  });

  it('declines a side when an absolute sibling sits in the measured gap', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'flex', flexDirection: 'row'}),
      selected: box({nodeId: 2, x: 80, y: 0}),
      siblings: [
        box({nodeId: 3, x: 0, y: 0, width: 40}),
        box({nodeId: 4, x: 50, y: 0, width: 20, position: 'absolute'}),
      ],
    }));
    assert.isUndefined(result.relationships.find(item => item.axis === SpacingAxis.HORIZONTAL &&
                                                 item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING));
    assert.isTrue(result.declined.some(item => item.reason === SpacingDeclineReason.OUT_OF_FLOW_BETWEEN &&
                                       item.axis === SpacingAxis.HORIZONTAL));
  });

  it('declines an axis when a transformed sibling shares that cross axis', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'flex', flexDirection: 'row', columnGap: 16}),
      selected: box({nodeId: 2, x: 116, y: 0}),
      siblings: [
        box({nodeId: 3, x: 0, y: 0}),
        box({nodeId: 4, x: 240, y: 0, transformNone: false}),
      ],
    }));
    assert.isUndefined(result.relationships.find(item => item.axis === SpacingAxis.HORIZONTAL));
    assert.isTrue(result.declined.some(item => item.reason === SpacingDeclineReason.TRANSFORMED_SIBLING));
  });

  it('declines when two neighbors are equally close', () => {
    const result = explainSpacing(input({
      selected: box({nodeId: 2, x: 0, y: 40, width: 200}),
      siblings: [
        box({nodeId: 3, x: 0, y: 0, width: 80, height: 20}),
        box({nodeId: 4, x: 100, y: 0, width: 80, height: 20}),
      ],
    }));
    assert.isUndefined(result.relationships.find(item => item.axis === SpacingAxis.VERTICAL &&
                                                 item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING));
    assert.isTrue(result.declined.some(item => item.reason === SpacingDeclineReason.AMBIGUOUS_NEIGHBOR &&
                                       item.axis === SpacingAxis.VERTICAL));
  });

  it('omits the far parent edge when a margin does not fill the unused space', () => {
    const result = explainSpacing(input({
      selected: box({
        nodeId: 2,
        x: 16,
        y: 16,
        width: 40,
        marginTop: 16,
        marginRight: 16,
        marginLeft: 16,
        marginTopDeclaration: {id: 3, property: 'margin-top', value: '16px', hasSource: true},
        marginLeftDeclaration: {id: 1, property: 'margin-left', value: '16px', hasSource: true},
        marginRightDeclaration: {id: 2, property: 'margin-right', value: '16px', hasSource: true},
      }),
      parent: parent({width: 250, height: 400}),
      siblings: [],
    }));
    const above = result.relationships.find(item => item.relationship === SpacingRelationshipKind.PARENT_START &&
                                             item.axis === SpacingAxis.VERTICAL);
    const left = result.relationships.find(item => item.relationship === SpacingRelationshipKind.PARENT_START &&
                                            item.axis === SpacingAxis.HORIZONTAL);
    const right = result.relationships.find(item => item.relationship === SpacingRelationshipKind.PARENT_END &&
                                             item.axis === SpacingAxis.HORIZONTAL);
    assert.strictEqual(above?.measuredGap, 16);
    assert.isTrue(above?.marginCollapseUnknown);
    assert.strictEqual(left?.measuredGap, 16);
    assert.strictEqual(left?.unexplained, 0);
    assert.strictEqual(left?.contributors[0]?.property, 'margin-left');
    assert.isUndefined(right);
  });

  it('does not invent a sibling gap when grid cells only meet at a corner', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'grid', columnGap: 10, rowGap: 0}),
      selected: box({nodeId: 2, x: 90, y: 0, width: 80, height: 20}),
      siblings: [box({nodeId: 3, x: 0, y: 20, width: 80, height: 20})],
    }));
    assert.isUndefined(result.relationships.find(item => item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING ||
                                                 item.relationship === SpacingRelationshipKind.NEXT_SIBLING));
  });

  it('does not attribute row-gap on the cross axis of a nowrap flex row', () => {
    const result = explainSpacing(input({
      parent: parent({display: 'flex', flexDirection: 'row', flexWrap: 'nowrap', rowGap: 16, columnGap: 0}),
      selected: box({nodeId: 2, x: 0, y: 36}),
      siblings: [box({nodeId: 3, x: 0, y: 0, height: 20})],
    }));
    const above = result.relationships.find(item => item.axis === SpacingAxis.VERTICAL &&
                                             item.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING);
    assert.strictEqual(above?.measuredGap, 16);
    assert.isFalse(above?.contributors.some(item => item.type === 'gap'));
    assert.strictEqual(above?.unexplained, 16);
  });
});
