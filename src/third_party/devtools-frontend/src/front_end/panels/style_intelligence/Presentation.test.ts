// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {authorDiagnostic, DiagnosticKind, percentageHeightDiagnostic} from './Diagnostics.js';
import {buildSummary} from './ElementSummary.js';
import {explainLayout, type ExplanationInput, type ParentFacts, type SpecifiedFacts} from './LayoutExplanation.js';
import {presentPane} from './Presentation.js';

function decl(id: number, property: string, value: string) {
  return {id, property, value, hasSource: true};
}

function specified(overrides: Partial<SpecifiedFacts> = {}): SpecifiedFacts {
  return {
    width: null, height: null, minWidth: null, maxWidth: null, minHeight: null, maxHeight: null,
    inlineSize: null, blockSize: null, minInlineSize: null, maxInlineSize: null, minBlockSize: null, maxBlockSize: null,
    flexGrow: null, flexShrink: null, flexBasis: null, alignSelf: null, justifySelf: null,
    top: null, right: null, bottom: null, left: null,
    marginTop: null, marginRight: null, marginBottom: null, marginLeft: null,
    gridColumn: null, gridRow: null,
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

function input(overrides: Partial<ExplanationInput> = {}, extra: Partial<SpecifiedFacts> = {}): ExplanationInput {
  return {
    display: 'block',
    position: 'static',
    boxSizing: 'content-box',
    writingMode: 'horizontal-tb',
    hasBox: true,
    borderWidth: 320,
    borderHeight: 40,
    x: 12,
    y: 18,
    contentWidth: 320,
    contentHeight: 40,
    paddingTop: 0, paddingRight: 0, paddingBottom: 0, paddingLeft: 0,
    borderTop: 0, borderRight: 0, borderBottom: 0, borderLeft: 0,
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

function summary() {
  return buildSummary('div#card', {
    display: 'block',
    position: 'static',
    flexDirection: null,
    flexWrap: null,
    alignItems: null,
    justifyContent: null,
    rowGap: null,
    columnGap: null,
    gridTemplateColumns: null,
    gridTemplateRows: null,
    parentDisplay: 'block',
    hasBox: true,
  }, {width: 320, height: 40}, []);
}

describe('presentPane', () => {
  it('leads with identity and size, and groups axes for scanning', () => {
    const pane = presentPane(summary(), explainLayout(input()), null, [], 4);
    assert.strictEqual(pane.identity, 'div#card');
    assert.strictEqual(pane.size, '320 × 40');
    assert.deepEqual(pane.sections.map(section => section.title), ['Layout', 'Size', 'Position']);
    const size = pane.sections[1];
    assert.deepEqual(size.rows.map(row => row.label), ['Width', 'Height']);
    assert.match(size.rows[0].primary, /Fills the available space/);
    assert.match(size.rows[1].primary, /content/);
    const position = pane.sections[2];
    assert.deepEqual(position.rows.map(row => row.label), ['X', 'Y']);
    assert.strictEqual(position.rows[0].primary, 'Normal flow');
    assert.deepEqual(position.rows[0].details, ['12px from the left edge of the viewport']);
    const nodeActions = size.rows.flatMap(row => row.controls.map(control => control.actions.nodeId));
    assert.deepEqual(nodeActions.filter(nodeId => nodeId === 7), [7]);
  });

  it('says a flex item is centered by the parent, and keeps the viewport offset in details', () => {
    const explanation = explainLayout(input({
      parent: parent({
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'center',
        alignItemsDeclaration: decl(8, 'align-items', 'center'),
        displayDeclaration: decl(2, 'display', 'flex'),
      }),
    }));
    const pane = presentPane(summary(), explanation, null, [], 4);
    const y = pane.sections.find(section => section.title === 'Position')?.rows.find(row => row.label === 'Y');
    assert.strictEqual(y?.primary, 'Centered by the parent');
    assert.match(y?.controls[0]?.text ?? '', /align-items: center/);
  });

  it('puts an unnamed containing block in details', () => {
    const explanation = explainLayout(input({position: 'absolute', x: 10, y: 20}, {left: decl(5, 'left', '10px')}));
    const pane = presentPane(summary(), explanation, null, [], 4);
    const x = pane.sections.find(section => section.title === 'Position')?.rows.find(row => row.label === 'X');
    assert.notMatch(x?.primary ?? '', /containing block/);
    assert.isTrue(x?.details.some(detail => detail.includes('containing block')));
  });

  it('attaches a percentage-height diagnostic to Height', () => {
    const explanation = explainLayout(input({borderHeight: 18, contentHeight: 18}, {height: decl(2, 'height', '100%')}));
    const diagnostic = percentageHeightDiagnostic(explanation);
    assert.isNotNull(diagnostic);
    const pane = presentPane(summary(), explanation, null, diagnostic ? [diagnostic] : [], 4);
    const height = pane.sections.find(section => section.title === 'Size')?.rows.find(row => row.label === 'Height');
    assert.strictEqual(height?.diagnostics.length, 1);
    assert.match(height?.diagnostics[0]?.headline ?? '', /height: 100%/);
    assert.isFalse(height?.details.some(detail => detail.includes('does not set this size')));
    assert.isUndefined(pane.sections.find(section => section.title === 'Potential issues'));
  });

  it('shows one cascade override for a flex shorthand and its longhands', () => {
    const diagnostics = [
      authorDiagnostic(DiagnosticKind.CASCADE_OVERRIDE, decl(1, 'flex', '0 0 auto')),
      authorDiagnostic(DiagnosticKind.CASCADE_OVERRIDE, {id: 2, property: 'flex-grow', value: '0', hasSource: false}),
      authorDiagnostic(DiagnosticKind.CASCADE_OVERRIDE, {id: 3, property: 'flex-shrink', value: '0', hasSource: false}),
      authorDiagnostic(DiagnosticKind.CASCADE_OVERRIDE, {id: 4, property: 'flex-basis', value: 'auto', hasSource: false}),
    ];
    const pane = presentPane(summary(), {axes: []}, null, diagnostics, 4);
    const layout = pane.sections.find(section => section.title === 'Layout');
    assert.strictEqual(layout?.diagnostics.length, 1);
    assert.match(layout?.diagnostics[0]?.headline ?? '', /flex: 0 0 auto/);
  });
});
