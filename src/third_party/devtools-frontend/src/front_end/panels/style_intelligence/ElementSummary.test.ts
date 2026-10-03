// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import * as StyleIntelligence from './style_intelligence.js';

const {
  buildSummary,
  customPropertiesInValue,
  describeLayout,
  EvidenceKind,
  formatElementIdentity,
  formatGeometry,
  formatLayoutStatement,
  summarizeTokens,
} = StyleIntelligence.ElementSummary;

function facts(overrides: Partial<StyleIntelligence.ElementSummary.LayoutFacts> = {}):
    StyleIntelligence.ElementSummary.LayoutFacts {
  return {
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
    parentDisplay: null,
    hasBox: true,
    ...overrides,
  };
}

describe('ElementSummary', () => {
  it('formats an element label from its tag, id, and classes', () => {
    assert.strictEqual(
        formatElementIdentity({name: 'div', id: 'main', classes: ['card', 'card-header']}), 'div#main.card.card-header');
    assert.strictEqual(formatElementIdentity({name: '::before'}), '::before');
  });

  it('formats measured border-box geometry', () => {
    assert.strictEqual(formatGeometry({kind: EvidenceKind.MEASURED, width: 842, height: 64}), '842 × 64');
  });

  it('describes a horizontal flex container from its computed style', () => {
    const layout = describeLayout(facts({
      display: 'flex',
      flexDirection: 'row',
      alignItems: 'center',
      justifyContent: 'flex-start',
      columnGap: '12px',
      rowGap: '12px',
    }));

    assert.deepEqual(layout.map(formatLayoutStatement), [
      'Flex container · horizontal',
      'Items aligned center',
      '12px gap',
    ]);
    assert.strictEqual(layout[0].kind, EvidenceKind.MEASURED);
  });

  it('omits default flex alignment and a normal gap', () => {
    const layout = describeLayout(facts({
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'stretch',
      justifyContent: 'normal',
      columnGap: 'normal',
      rowGap: '0px',
    }));
    assert.deepEqual(layout.map(formatLayoutStatement), ['Flex container · vertical']);
  });

  it('marks an in-flow child of a flex container as a flex item', () => {
    const layout = describeLayout(facts({parentDisplay: 'flex'}));
    assert.deepEqual(layout.map(statement => statement.type), ['flex-item']);
    assert.strictEqual(layout[0].kind, EvidenceKind.INFERRED);
  });

  it('does not call an absolutely positioned child a flex item', () => {
    const layout = describeLayout(facts({position: 'absolute', parentDisplay: 'flex', display: 'block'}));
    assert.deepEqual(layout.map(formatLayoutStatement), ['Absolutely positioned']);
  });

  it('does not invent a flex item when the layout parent is unknown', () => {
    const layout = describeLayout(facts({parentDisplay: null, position: 'static'}));
    assert.deepEqual(layout.map(formatLayoutStatement), ['Block']);
  });

  it('does not invent a flex item when position was not measured', () => {
    const layout = describeLayout(facts({parentDisplay: 'flex', position: null}));
    assert.deepEqual(layout.map(statement => statement.type), ['block']);
  });

  it('describes grid, subgrid, and grid items from computed style', () => {
    assert.deepEqual(
        describeLayout(facts({display: 'grid', columnGap: '8px', rowGap: '16px'})).map(formatLayoutStatement),
        ['Grid', 'Row gap 16px · column gap 8px']);
    assert.deepEqual(
        describeLayout(facts({display: 'grid', gridTemplateColumns: 'subgrid'})).map(formatLayoutStatement),
        ['Subgrid']);
    const item = describeLayout(facts({parentDisplay: 'inline-grid', position: 'relative'}));
    assert.deepEqual(item.map(statement => statement.type), ['position', 'grid-item']);
  });

  it('reports display contents and display none without a box', () => {
    assert.deepEqual(
        describeLayout(facts({display: 'contents', hasBox: false})).map(formatLayoutStatement),
        ['Does not generate a box']);
    assert.deepEqual(
        describeLayout(facts({display: 'none', hasBox: false, parentDisplay: 'flex'})).map(formatLayoutStatement),
        ['Not rendered']);
  });

  it('keeps a measured box when display contents did not match the box model', () => {
    const summary = buildSummary('div', facts({display: 'contents', hasBox: true}), {width: 20, height: 10}, []);
    assert.strictEqual(summary.geometry?.width, 20);
    assert.deepEqual(summary.layout.map(formatLayoutStatement), []);
  });

  it('collects directly referenced custom properties and drops unrelated names', () => {
    assert.deepEqual(customPropertiesInValue('var(--surface-primary) var(--radius-md, 4px)'), [
      '--surface-primary',
      '--radius-md',
    ]);
    const tokens = summarizeTokens([
      {propertyName: 'background-color', customProperty: '--surface-primary', resolvedValue: '#171A1D'},
      {propertyName: 'background', customProperty: '--surface-primary', resolvedValue: '#171A1D'},
      {propertyName: 'padding', customProperty: '--spacing-lg', resolvedValue: '16px'},
      {propertyName: 'animation-name', customProperty: '--motion', resolvedValue: 'rise'},
      {propertyName: 'color', customProperty: '--text', resolvedValue: null},
    ]);
    assert.deepEqual(tokens.map(token => `${token.label} ${token.customProperty} ${token.resolvedValue ?? ''}`), [
      'Background --surface-primary #171A1D',
      'Color --text ',
      'Padding --spacing-lg 16px',
    ]);
    assert.strictEqual(tokens[0].kind, EvidenceKind.MEASURED);
  });
});
