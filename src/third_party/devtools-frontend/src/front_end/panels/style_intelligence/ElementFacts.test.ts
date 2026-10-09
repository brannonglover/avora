// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {type Diagnostic, DiagnosticCategory, DiagnosticKind, DiagnosticPlacement} from './Diagnostics.js';
import {buildElementFacts, factCount, FactSeverity, type FactsInput} from './ElementFacts.js';
import {type AxisExplanation, Confidence, ContributionRole, Mechanism} from './LayoutExplanation.js';
import {SpacingAxis, SpacingRelationshipKind, type SpacingRelationship} from './SpacingExplanation.js';

function axis(overrides: Partial<AxisExplanation> = {}): AxisExplanation {
  return {
    axis: 'height',
    usedValue: 176,
    mechanism: Mechanism.CONTENT,
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

function diagnostic(overrides: Partial<Diagnostic> = {}): Diagnostic {
  return {
    kind: DiagnosticKind.CASCADE_OVERRIDE,
    category: DiagnosticCategory.NO_VISIBLE_EFFECT,
    placement: DiagnosticPlacement.SPACING,
    property: 'margin-top',
    value: '8px',
    declarationId: 7,
    hasSource: true,
    evidence: {measured: null, freeSpace: null, parentHeightAuto: false},
    validatorMessage: null,
    ...overrides,
  };
}

function spacing(overrides: Partial<SpacingRelationship> = {}): SpacingRelationship {
  return {
    measuredGap: 16,
    axis: SpacingAxis.VERTICAL,
    relationship: SpacingRelationshipKind.PARENT_END,
    neighbor: null,
    contributors: [],
    unexplained: 0,
    marginCollapseUnknown: false,
    ...overrides,
  };
}

function input(overrides: Partial<FactsInput> = {}): FactsInput {
  return {
    selectedNodeId: 10,
    axes: [],
    spacing: [],
    diagnostics: [],
    layering: null,
    ...overrides,
  };
}

describe('ElementFacts', () => {
  it('keeps an axis the element decides itself', () => {
    const facts = buildElementFacts(input({axes: [axis()]}));

    assert.lengthOf(facts, 1);
    assert.strictEqual(facts[0].property, 'height');
    assert.strictEqual(facts[0].used, '176px');
    assert.strictEqual(facts[0].source, 'content');
  });

  it('leaves an axis whose controller is an ancestor to that ancestor’s rung', () => {
    const facts = buildElementFacts(input({
      axes: [axis({
        axis: 'width',
        usedValue: 312,
        mechanism: Mechanism.FLEX_STRETCH,
        controller: {nodeId: 8, label: 'div.card__body', property: 'align-items', value: 'stretch', declarationId: 4, hasSource: true},
      })],
    }));

    assert.isEmpty(facts.filter(fact => fact.property === 'width'));
  });

  it('keeps an axis the element declares itself even when a controller is set', () => {
    const facts = buildElementFacts(input({
      axes: [axis({
        axis: 'width',
        usedValue: 312,
        mechanism: Mechanism.EXPLICIT,
        controller: {nodeId: 10, label: 'div.card__media', property: 'width', value: '312px', declarationId: 2, hasSource: true},
      })],
    }));

    assert.lengthOf(facts, 1);
    assert.strictEqual(facts[0].property, 'width');
    assert.strictEqual(facts[0].source, 'width');
    assert.strictEqual(facts[0].declarationId, 2);
  });

  it('attaches a diagnostic to the row it is about instead of a separate section', () => {
    const facts = buildElementFacts(input({
      spacing: [spacing({
        contributors: [{
          type: 'margin',
          value: 8,
          nodeId: 10,
          label: 'div.card__media',
          property: 'margin-top',
          declaredValue: '8px',
          declarationId: 5,
          hasSource: true,
          confidence: Confidence.HIGH,
          countsTowardGap: true,
        }],
      })],
      diagnostics: [diagnostic()],
    }));

    const row = facts.find(fact => fact.property === 'margin-top');
    assert.exists(row);
    assert.strictEqual(row?.source, 'overridden');
    assert.strictEqual(row?.severity, FactSeverity.WARNING);
    assert.strictEqual(factCount(facts).issues, 1);
  });

  it('keeps a diagnostic that matches no row as its own row', () => {
    const facts = buildElementFacts(input({
      diagnostics: [diagnostic({kind: DiagnosticKind.PERCENTAGE_HEIGHT, property: 'height', value: '50%'})],
    }));

    assert.lengthOf(facts, 1);
    assert.strictEqual(facts[0].property, 'height');
    assert.strictEqual(facts[0].source, 'no effect');
    assert.strictEqual(facts[0].severity, FactSeverity.WARNING);
  });

  it('reports a trapped z-index as critical and does not let a warning downgrade it', () => {
    const facts = buildElementFacts(input({
      axes: [axis({
        axis: 'height',
        contributions: [{
          kind: 'measured',
          property: 'z-index',
          value: '40',
          declarationId: 6,
          hasSource: true,
          role: ContributionRole.CONTRIBUTES,
        }],
      })],
      diagnostics: [diagnostic({property: 'z-index', value: '40'})],
      layering: {zIndex: '40', trapped: true, trapLabel: 'div.card'},
    }));

    const row = facts.find(fact => fact.property === 'z-index');
    assert.strictEqual(row?.severity, FactSeverity.CRITICAL);
    assert.strictEqual(row?.source, 'trapped');
    assert.include(row?.title ?? '', 'div.card');
  });

  it('ignores a neighbour’s margin, which belongs to that neighbour’s rung', () => {
    const facts = buildElementFacts(input({
      spacing: [spacing({
        relationship: SpacingRelationshipKind.PREVIOUS_SIBLING,
        neighbor: {nodeId: 21, label: 'div.card__header'},
        contributors: [{
          type: 'margin',
          value: 24,
          nodeId: 21,
          label: 'div.card__header',
          property: 'margin-bottom',
          declaredValue: '24px',
          declarationId: 9,
          hasSource: true,
          confidence: Confidence.HIGH,
          countsTowardGap: true,
        }],
      })],
    }));

    assert.isEmpty(facts);
  });

  it('shows an authored token in the source column when it differs from the used pixels', () => {
    const facts = buildElementFacts(input({
      spacing: [spacing({
        contributors: [{
          type: 'padding',
          value: 16,
          nodeId: 10,
          label: 'div.card__media',
          property: 'padding-bottom',
          declaredValue: 'var(--space-4)',
          declarationId: 11,
          hasSource: true,
          confidence: Confidence.HIGH,
          countsTowardGap: true,
        }],
      })],
    }));

    assert.lengthOf(facts, 1);
    assert.strictEqual(facts[0].used, '16px');
    assert.strictEqual(facts[0].source, 'var(--space-4)');
  });

  it('drops an ineffective contribution', () => {
    const facts = buildElementFacts(input({
      axes: [axis({
        contributions: [{
          kind: 'measured',
          property: 'min-width',
          value: '0px',
          declarationId: 12,
          hasSource: true,
          role: ContributionRole.INEFFECTIVE,
        }],
      })],
    }));

    assert.isEmpty(facts.filter(fact => fact.property === 'min-width'));
  });
});
