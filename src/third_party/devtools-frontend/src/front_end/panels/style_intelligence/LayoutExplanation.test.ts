// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import * as StyleIntelligence from './style_intelligence.js';

const {explainLayout, Mechanism, presentAxis} = StyleIntelligence.LayoutExplanation;
type AxisExplanation = StyleIntelligence.LayoutExplanation.AxisExplanation;
type DeclarationFact = StyleIntelligence.LayoutExplanation.DeclarationFact;
type ExplanationInput = StyleIntelligence.LayoutExplanation.ExplanationInput;
type ParentFacts = StyleIntelligence.LayoutExplanation.ParentFacts;
type SpecifiedFacts = StyleIntelligence.LayoutExplanation.SpecifiedFacts;

function decl(id: number, property: string, value: string, hasSource = true): DeclarationFact {
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
    contentHeight: 600,
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

function input(overrides: Partial<ExplanationInput> = {}, extra: Partial<SpecifiedFacts> = {}): ExplanationInput {
  return {
    display: 'block',
    position: 'static',
    boxSizing: 'content-box',
    writingMode: 'horizontal-tb',
    hasBox: true,
    borderWidth: 320,
    borderHeight: 40,
    x: 24,
    y: 48,
    contentWidth: 320,
    contentHeight: 40,
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
    computedFlexGrow: 0,
    computedFlexShrink: 1,
    computedFlexBasis: 'auto',
    computedAlignSelf: 'auto',
    computedJustifySelf: 'auto',
    animatedProperties: [],
    ...overrides,
    specified: specified({...extra, ...overrides.specified}),
  };
}

function axis(result: ReturnType<typeof explainLayout>, name: AxisExplanation['axis']): AxisExplanation {
  const found = result.axes.find(item => item.axis === name);
  assert.exists(found);
  return found as AxisExplanation;
}

describe('LayoutExplanation', () => {
  it('explains an in-flow block from its parent and its content height', () => {
    const result = explainLayout(input());
    const width = axis(result, 'width');
    const height = axis(result, 'height');
    assert.strictEqual(width.mechanism, Mechanism.BLOCK_AVAILABLE);
    assert.strictEqual(width.usedValue, 320);
    assert.strictEqual(width.controller?.label, 'div#page');
    assert.strictEqual(height.mechanism, Mechanism.CONTENT);
    assert.strictEqual(axis(result, 'x').mechanism, Mechanism.BLOCK_FLOW);
    assert.strictEqual(axis(result, 'y').mechanism, Mechanism.BLOCK_FLOW);
    assert.strictEqual(presentAxis(width).headline, 'Fills the available space');
  });

  it('treats a matching author length as the specified size', () => {
    const result = explainLayout(input({}, {width: decl(3, 'width', '320px'), height: decl(4, 'height', '40px')}));
    assert.strictEqual(axis(result, 'width').mechanism, Mechanism.EXPLICIT);
    assert.strictEqual(axis(result, 'width').contributions[0].role, 'specifies');
    assert.strictEqual(axis(result, 'height').mechanism, Mechanism.EXPLICIT);
    assert.strictEqual(presentAxis(axis(result, 'width')).headline, 'Specified as 320px');
  });

  it('does not equate a content-box length with the border box', () => {
    const result = explainLayout(input({
      borderWidth: 336,
      contentWidth: 320,
      paddingLeft: 8,
      paddingRight: 8,
    }, {width: decl(3, 'width', '320px')}));
    const width = axis(result, 'width');
    assert.strictEqual(width.mechanism, Mechanism.EXPLICIT);
    assert.isTrue(width.specifiedMatchesContentBox);
    assert.strictEqual(presentAxis(width).headline, 'Content size specified as 320px');
    assert.strictEqual(presentAxis(width).value, '336px');
  });

  it('confirms a percentage against the layout parent and declines it for absolute boxes', () => {
    const matched = explainLayout(input({contentWidth: 400, borderWidth: 400}, {width: decl(3, 'width', '50%')}));
    assert.strictEqual(axis(matched, 'width').mechanism, Mechanism.PERCENTAGE);
    assert.strictEqual(axis(matched, 'width').confidence, 'high');
    assert.strictEqual(axis(matched, 'width').controller?.nodeId, 7);

    const absolute = explainLayout(input({
      position: 'absolute',
      contentWidth: 400,
      borderWidth: 400,
    }, {width: decl(3, 'width', '50%'), left: decl(5, 'left', '12px')}));
    const width = axis(absolute, 'width');
    assert.strictEqual(width.mechanism, Mechanism.PERCENTAGE);
    assert.isNull(width.controller);
    assert.isTrue(width.containingBlockUnknown);
    assert.isTrue(width.contributions.every(item => item.role !== 'specifies'));
    assert.strictEqual(axis(absolute, 'x').mechanism, Mechanism.ABSOLUTE);
    assert.isNull(axis(absolute, 'x').controller);
    assert.include(presentAxis(axis(absolute, 'x')).caveat ?? '', 'containing block');
  });

  it('does not let an auto parent height pretend a percentage height was used', () => {
    const result = explainLayout(input({
      contentHeight: 40,
      borderHeight: 40,
      parent: parent({heightIsAuto: true, contentHeight: 200}),
    }, {height: decl(4, 'height', '100%')}));
    const height = axis(result, 'height');
    assert.strictEqual(height.mechanism, Mechanism.CONTENT);
    assert.strictEqual(height.contributions[0].role, 'ineffective');
    assert.include(presentAxis(height).caveat ?? '', 'does not set this size');

    const circular = explainLayout(input({
      contentHeight: 18,
      borderHeight: 18,
      parent: parent({heightIsAuto: true, autoHeightMayBeDefinite: false, contentHeight: 18}),
    }, {height: decl(4, 'height', '100%')}));
    assert.strictEqual(axis(circular, 'height').mechanism, Mechanism.CONTENT);
    assert.strictEqual(axis(circular, 'height').contributions[0].role, 'ineffective');
  });

  it('attributes a maximum only when it disagrees with the specified size', () => {
    const bound = explainLayout(input({
      borderWidth: 200,
      contentWidth: 200,
    }, {width: decl(3, 'width', '400px'), maxWidth: decl(4, 'max-width', '200px')}));
    assert.strictEqual(axis(bound, 'width').mechanism, Mechanism.MAX_SIZE);
    assert.strictEqual(axis(bound, 'width').contributions[0].role, 'constrains');

    const coincidence = explainLayout(input({
      borderWidth: 200,
      contentWidth: 200,
    }, {maxWidth: decl(4, 'max-width', '200px')}));
    assert.notStrictEqual(axis(coincidence, 'width').mechanism, Mechanism.MAX_SIZE);
  });

  it('explains a flex item from the flex container rather than from flex-grow alone', () => {
    const result = explainLayout(input({
      parent: parent({
        display: 'flex',
        flexDirection: 'row',
        label: 'div.header',
        displayDeclaration: decl(2, 'display', 'flex'),
        justifyContent: 'space-between',
        justifyContentDeclaration: decl(8, 'justify-content', 'space-between'),
      }),
      computedFlexGrow: 1,
      borderWidth: 180,
      contentWidth: 180,
    }, {flexGrow: decl(9, 'flex-grow', '1')}));
    const width = axis(result, 'width');
    assert.strictEqual(width.mechanism, Mechanism.FLEX_LAYOUT);
    assert.strictEqual(width.controller?.label, 'div.header');
    assert.strictEqual(width.contributions[0].role, 'contributes');
    assert.notStrictEqual(width.contributions[0].role, 'specifies');
    assert.strictEqual(presentAxis(width).headline, 'Set by the parent flex layout');
    const x = axis(result, 'x');
    assert.strictEqual(x.mechanism, Mechanism.FLEX_PLACEMENT);
    assert.strictEqual(x.controller?.property, 'justify-content');
    assert.strictEqual(x.controller?.value, 'space-between');
  });

  it('explains flex stretch on the cross axis without claiming a pixel declaration', () => {
    const result = explainLayout(input({
      parent: parent({display: 'flex', flexDirection: 'column', alignItems: 'stretch'}),
      computedAlignSelf: 'auto',
      borderWidth: 800,
      contentWidth: 800,
    }));
    const width = axis(result, 'width');
    assert.strictEqual(width.mechanism, Mechanism.FLEX_STRETCH);
    assert.strictEqual(width.controller?.property, 'display');
    assert.strictEqual(width.contributions.length, 0);
  });

  it('does not invent stretch when flex direction was not measured', () => {
    const result = explainLayout(input({
      parent: parent({display: 'flex', flexDirection: null}),
      computedFlexGrow: 1,
    }, {flexGrow: decl(9, 'flex-grow', '1')}));
    const width = axis(result, 'width');
    assert.strictEqual(width.mechanism, Mechanism.FLEX_LAYOUT);
    assert.strictEqual(width.confidence, 'limited');
    assert.notStrictEqual(width.mechanism, Mechanism.FLEX_STRETCH);
  });

  it('explains grid stretch and grid placement from the grid parent', () => {
    const result = explainLayout(input({
      parent: parent({
        display: 'grid',
        label: 'div.board',
        displayDeclaration: decl(2, 'display', 'grid'),
        gridTemplateColumns: decl(6, 'grid-template-columns', '1fr 1fr'),
      }),
      computedJustifySelf: 'stretch',
      borderWidth: 120,
      contentWidth: 120,
    }, {gridColumn: decl(10, 'grid-column', '2')}));
    assert.strictEqual(axis(result, 'width').mechanism, Mechanism.GRID_STRETCH);
    assert.strictEqual(axis(result, 'width').controller?.label, 'div.board');
    assert.strictEqual(axis(result, 'width').contributions[0].property, 'grid-template-columns');
    assert.strictEqual(axis(result, 'width').contributions[0].role, 'contributes');
    const x = axis(result, 'x');
    assert.strictEqual(x.mechanism, Mechanism.GRID_PLACEMENT);
    assert.strictEqual(x.contributions[0].property, 'grid-column');
  });

  it('names insets for absolute and fixed boxes and does not name a containing block', () => {
    const absolute = explainLayout(input({
      position: 'absolute',
      x: 428,
      y: 16,
    }, {right: decl(5, 'right', '24px'), top: decl(6, 'top', '16px')}));
    assert.strictEqual(axis(absolute, 'x').mechanism, Mechanism.ABSOLUTE);
    assert.strictEqual(axis(absolute, 'x').contributions[0].value, '24px');
    assert.isNull(axis(absolute, 'x').controller);
    assert.include(presentAxis(axis(absolute, 'x')).value, '428px');
    assert.strictEqual(axis(absolute, 'width').mechanism, Mechanism.INTRINSIC);
    assert.isTrue(axis(absolute, 'width').containingBlockUnknown);

    const fixed = explainLayout(input({position: 'fixed', x: 0, y: 0}, {top: decl(6, 'top', '0px')}));
    assert.strictEqual(axis(fixed, 'y').mechanism, Mechanism.FIXED);
    assert.isNull(axis(fixed, 'y').controller);
    assert.isTrue(axis(fixed, 'y').containingBlockUnknown);
  });

  it('derives an automatic absolute size from both insets', () => {
    const result = explainLayout(input({
      position: 'absolute',
      borderWidth: 200,
      contentWidth: 200,
    }, {left: decl(5, 'left', '10px'), right: decl(6, 'right', '10px')}));
    const width = axis(result, 'width');
    assert.strictEqual(width.mechanism, Mechanism.INSET_DERIVED);
    assert.isNull(width.controller);
    assert.isTrue(width.containingBlockUnknown);
    assert.deepEqual(width.contributions.map(item => item.property), ['left', 'right']);
  });

  it('explains a relative offset separately from the in-flow position', () => {
    const result = explainLayout(input({position: 'relative', x: 36}, {left: decl(5, 'left', '12px')}));
    const x = axis(result, 'x');
    assert.strictEqual(x.mechanism, Mechanism.RELATIVE);
    assert.strictEqual(x.contributions[0].property, 'left');
    assert.strictEqual(x.controller?.label, 'div#page');
  });

  it('attributes sticky positioning to a scrollport only as an appearance', () => {
    const known = explainLayout(input({
      position: 'sticky',
      scrollport: {nodeId: 4, label: 'div.scroller'},
    }, {top: decl(6, 'top', '8px')}));
    const y = axis(known, 'y');
    assert.strictEqual(y.mechanism, Mechanism.STICKY);
    assert.strictEqual(y.confidence, 'limited');
    assert.strictEqual(y.controller?.label, 'div.scroller');
    assert.strictEqual(y.controller?.nodeId, 4);

    const unknown = explainLayout(input({position: 'sticky'}, {top: decl(6, 'top', '8px')}));
    assert.isNull(axis(unknown, 'y').controller);
    assert.include(presentAxis(axis(unknown, 'y')).caveat ?? '', 'scrollport');
  });

  it('uses the layout parent of a nested flex item, which has already skipped display contents', () => {
    const result = explainLayout(input({
      parent: parent({
        nodeId: 11,
        label: 'div.inner',
        display: 'flex',
        flexDirection: 'row',
        displayDeclaration: decl(2, 'display', 'flex'),
      }),
      computedFlexGrow: 1,
      borderWidth: 90,
      contentWidth: 90,
    }, {flexGrow: decl(9, 'flex-grow', '1')}));
    assert.strictEqual(axis(result, 'width').controller?.label, 'div.inner');
    assert.notStrictEqual(axis(result, 'width').controller?.label, 'div.outer');
  });

  it('omits geometry for display contents and display none when there is no box', () => {
    assert.deepEqual(explainLayout(input({display: 'contents', hasBox: false, borderWidth: null, borderHeight: null, x: null, y: null})).axes, []);
    assert.deepEqual(explainLayout(input({display: 'none', hasBox: false, borderWidth: null, borderHeight: null, x: null, y: null})).axes, []);
  });

  it('declines when position was not measured', () => {
    const result = explainLayout(input({position: null, parent: parent({display: 'flex', flexDirection: 'row'})}));
    assert.strictEqual(axis(result, 'width').mechanism, Mechanism.UNKNOWN);
    assert.strictEqual(axis(result, 'x').mechanism, Mechanism.UNKNOWN);
  });

  it('declines size attribution when an animation overrides the property', () => {
    const result = explainLayout(input({animatedProperties: ['width']}, {width: decl(3, 'width', '320px')}));
    assert.strictEqual(axis(result, 'width').mechanism, Mechanism.UNKNOWN);
    assert.strictEqual(axis(result, 'width').contributions.length, 0);
  });

  it('centers a sized block with auto horizontal margins', () => {
    const result = explainLayout(input({}, {
      width: decl(3, 'width', '320px'),
      marginLeft: decl(12, 'margin-left', 'auto'),
      marginRight: decl(13, 'margin-right', 'auto'),
    }));
    assert.strictEqual(axis(result, 'x').mechanism, Mechanism.AUTO_MARGINS);
  });
});
