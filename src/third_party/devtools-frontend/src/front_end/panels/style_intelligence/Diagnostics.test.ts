// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {flexFreeSpaceDiagnostic, percentageHeightDiagnostic, presentDiagnostic, type FlexFreeSpaceInput} from './Diagnostics.js';
import {ContributionRole, explainLayout, Mechanism, type AxisExplanation, type ExplanationInput, type ParentFacts, type SpecifiedFacts} from './LayoutExplanation.js';

function decl(id: number, property: string, value: string, hasSource = true) {
  return {id, property, value, hasSource};
}

function specified(overrides: Partial<SpecifiedFacts> = {}): SpecifiedFacts {
  return {
    width: null,
    height: null,
    minWidth: null,
    maxWidth: null,
    minHeight: null,
    maxHeight: null,
    inlineSize: null,
    blockSize: null,
    minInlineSize: null,
    maxInlineSize: null,
    minBlockSize: null,
    maxBlockSize: null,
    flexGrow: null,
    flexShrink: null,
    flexBasis: null,
    alignSelf: null,
    justifySelf: null,
    top: null,
    right: null,
    bottom: null,
    left: null,
    marginTop: null,
    marginRight: null,
    marginBottom: null,
    marginLeft: null,
    gridColumn: null,
    gridRow: null,
    ...overrides,
  };
}

function parent(overrides: Partial<ParentFacts> = {}): ParentFacts {
  return {
    nodeId: 7,
    label: 'div#page',
    display: 'block',
    position: 'static',
    flexDirection: null,
    alignItems: 'stretch',
    justifyContent: 'normal',
    justifyItems: 'normal',
    contentWidth: 800,
    contentHeight: 200,
    heightIsAuto: true,
    autoHeightMayBeDefinite: false,
    displayDeclaration: decl(1, 'display', 'block'),
    alignItemsDeclaration: null,
    justifyContentDeclaration: null,
    justifyItemsDeclaration: null,
    gridTemplateColumns: null,
    gridTemplateRows: null,
    ...overrides,
  };
}

function layoutInput(overrides: Partial<ExplanationInput> = {}, extra: Partial<SpecifiedFacts> = {}): ExplanationInput {
  return {
    display: 'block',
    position: 'static',
    boxSizing: 'content-box',
    writingMode: 'horizontal-tb',
    hasBox: true,
    borderWidth: 100,
    borderHeight: 18,
    x: 0,
    y: 0,
    contentWidth: 100,
    contentHeight: 18,
    paddingTop: 0,
    paddingRight: 0,
    paddingBottom: 0,
    paddingLeft: 0,
    borderTop: 0,
    borderRight: 0,
    borderBottom: 0,
    borderLeft: 0,
    parent: parent(),
    scrollport: null,
    specified: specified(extra),
    computedFlexGrow: null,
    computedFlexShrink: null,
    computedFlexBasis: null,
    computedAlignSelf: null,
    computedJustifySelf: null,
    animatedProperties: [],
    ...overrides,
  };
}

function flexInput(overrides: Partial<FlexFreeSpaceInput> = {}): FlexFreeSpaceInput {
  return {
    display: 'flex',
    flexDirection: 'row',
    flexWrap: 'nowrap',
    justifyContent: 'center',
    justifyContentDeclaration: decl(3, 'justify-content', 'center'),
    contentMainSize: 200,
    mainGap: 0,
    items: [
      {mainSize: 100, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: false},
      {mainSize: 100, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: false},
    ],
    itemsIncomplete: false,
    ...overrides,
  };
}

describe('percentageHeightDiagnostic', () => {
  it('reports height 100% when the parent height is indefinite and the used size does not match', () => {
    const explanation = explainLayout(layoutInput({}, {height: decl(2, 'height', '100%')}));
    const height = explanation.axes.find(axis => axis.axis === 'height') as AxisExplanation;
    assert.strictEqual(height.mechanism, Mechanism.CONTENT);
    assert.strictEqual(height.contributions[0]?.role, ContributionRole.INEFFECTIVE);

    const diagnostic = percentageHeightDiagnostic(explanation);
    assert.strictEqual(diagnostic?.kind, 'percentage-height');
    assert.strictEqual(diagnostic?.placement, 'height');
    assert.strictEqual(diagnostic?.category, 'no-visible-effect');
    assert.strictEqual(diagnostic?.evidence.measured, 18);
    assert.strictEqual(diagnostic?.evidence.parentHeightAuto, true);
    assert.isTrue(diagnostic?.hasSource);
    const copy = presentDiagnostic(diagnostic!);
    assert.strictEqual(copy.category, 'No visible effect');
    assert.match(copy.headline, /height: 100%/);
    assert.match(copy.detail, /parent height is auto/);
  });

  it('does not diagnose an absolute percentage whose containing block cannot be named', () => {
    const explanation = explainLayout(layoutInput({position: 'absolute'}, {height: decl(2, 'height', '100%')}));
    assert.isNull(percentageHeightDiagnostic(explanation));
  });

  it('diagnoses a percentage that matches only because the parent sized to the child', () => {
    const explanation = explainLayout(layoutInput({
      borderHeight: 18,
      contentHeight: 18,
      parent: parent({heightIsAuto: true, autoHeightMayBeDefinite: false, contentHeight: 18}),
    }, {height: decl(2, 'height', '100%')}));
    assert.strictEqual(percentageHeightDiagnostic(explanation)?.kind, 'percentage-height');
  });

  it('stays silent when an auto parent may still have a definite stretched height', () => {
    const explanation = explainLayout(layoutInput({
      borderHeight: 200,
      contentHeight: 200,
      parent: parent({heightIsAuto: true, autoHeightMayBeDefinite: true, contentHeight: 200}),
    }, {height: decl(2, 'height', '100%')}));
    assert.isNull(percentageHeightDiagnostic(explanation));
  });

  it('stays silent when the percentage resolves against the parent content box', () => {
    const explanation = explainLayout(layoutInput({
      borderHeight: 200,
      contentHeight: 200,
      parent: parent({heightIsAuto: false, contentHeight: 200}),
    }, {height: decl(2, 'height', '100%')}));
    assert.isNull(percentageHeightDiagnostic(explanation));
  });
});

describe('flexFreeSpaceDiagnostic', () => {
  it('reports justify-content when measured free space is zero', () => {
    const diagnostic = flexFreeSpaceDiagnostic(flexInput());
    assert.strictEqual(diagnostic?.kind, 'zero-free-space');
    assert.strictEqual(diagnostic?.evidence.freeSpace, 0);
    assert.strictEqual(diagnostic?.property, 'justify-content');
    assert.match(presentDiagnostic(diagnostic!).detail, /0px/);
  });

  it('accounts for gap and item margins', () => {
    const diagnostic = flexFreeSpaceDiagnostic(flexInput({
      contentMainSize: 200,
      mainGap: 16,
      items: [
        {mainSize: 80, marginStart: 12, marginEnd: 0, outOfFlow: false, transformed: false},
        {mainSize: 80, marginStart: 0, marginEnd: 12, outOfFlow: false, transformed: false},
      ],
    }));
    assert.isNotNull(diagnostic);
  });

  it('stays silent when free space remains', () => {
    assert.isNull(flexFreeSpaceDiagnostic(flexInput({
      items: [
        {mainSize: 40, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: false},
        {mainSize: 40, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: false},
      ],
    })));
  });

  it('reports a single item that already fills the main axis', () => {
    assert.isNotNull(flexFreeSpaceDiagnostic(flexInput({
      items: [{mainSize: 200, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: false}],
    })));
    assert.isNull(flexFreeSpaceDiagnostic(flexInput({
      items: [{mainSize: 40, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: false}],
    })));
  });

  it('stays silent for default alignment, wrapping, and missing geometry', () => {
    assert.isNull(flexFreeSpaceDiagnostic(flexInput({justifyContent: 'flex-start'})));
    assert.isNull(flexFreeSpaceDiagnostic(flexInput({flexWrap: 'wrap'})));
    assert.isNull(flexFreeSpaceDiagnostic(flexInput({flexDirection: null})));
    assert.isNull(flexFreeSpaceDiagnostic(flexInput({itemsIncomplete: true})));
    assert.isNull(flexFreeSpaceDiagnostic(flexInput({mainGap: null})));
    assert.isNull(flexFreeSpaceDiagnostic(flexInput({
      items: [
        {mainSize: 200, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: false},
        {mainSize: null, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: false},
      ],
    })));
    assert.isNull(flexFreeSpaceDiagnostic(flexInput({
      items: [
        {mainSize: 100, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: true},
        {mainSize: 100, marginStart: 0, marginEnd: 0, outOfFlow: false, transformed: false},
      ],
    })));
  });
});
