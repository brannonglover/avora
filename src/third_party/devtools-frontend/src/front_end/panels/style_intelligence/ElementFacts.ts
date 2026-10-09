// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * What the selected element decides for itself, as aligned rows.
 *
 * This is the other half of the ElementChain split. An axis whose controller
 * is an ancestor is that ancestor's rung, not a row here; an axis with no
 * controller, or one the element declares itself, is a row. Nothing appears in
 * both places.
 *
 * The source column doubles as the severity column, so a diagnostic attaches
 * to the row it is about instead of opening a separate issues section. A
 * diagnostic that names no property the rows already carry keeps its own row
 * rather than being dropped.
 */

import {type Diagnostic, DiagnosticKind, presentDiagnostic} from './Diagnostics.js';
import {type AxisExplanation, ContributionRole, Mechanism} from './LayoutExplanation.js';
import {SpacingContributorType, type SpacingRelationship} from './SpacingExplanation.js';

export const FactSeverity = {
  NONE: 'none',
  WARNING: 'warning',
  CRITICAL: 'critical',
} as const;

export type FactSeverity = (typeof FactSeverity)[keyof typeof FactSeverity];

export interface ElementFact {
  property: string;
  /** Used value, already formatted. Tabular in the view. */
  used: string;
  /** Where the value came from, or what is wrong with it. */
  source: string;
  severity: FactSeverity;
  declarationId: number|null;
  /** Full text for a tooltip, since the source column truncates. */
  title: string;
}

/** The trapped z-index, once a snapshot has confirmed it. */
export interface FactsLayering {
  zIndex: string|null;
  trapped: boolean;
  trapLabel: string|null;
}

export interface FactsInput {
  selectedNodeId: number|null;
  axes: AxisExplanation[];
  spacing: SpacingRelationship[];
  diagnostics: Diagnostic[];
  layering: FactsLayering|null;
}

export function buildElementFacts(input: FactsInput): ElementFact[] {
  const facts: ElementFact[] = [];
  const byProperty = new Map<string, ElementFact>();
  const add = (fact: ElementFact): void => {
    if (byProperty.has(fact.property)) {
      return;
    }
    byProperty.set(fact.property, fact);
    facts.push(fact);
  };

  for (const axis of input.axes) {
    const own = axisRow(axis, input.selectedNodeId);
    if (own) {
      add(own);
    }
    for (const contribution of contributionRows(axis)) {
      add(contribution);
    }
  }
  for (const row of spacingRows(input.spacing, input.selectedNodeId)) {
    add(row);
  }
  const trapped = layeringRow(input.layering);
  if (trapped) {
    // A trapped z-index outranks a plain z-index contribution row.
    const existing = byProperty.get(trapped.property);
    if (existing) {
      existing.source = trapped.source;
      existing.severity = trapped.severity;
      existing.title = trapped.title;
    } else {
      add(trapped);
    }
  }
  applyDiagnostics(input.diagnostics, byProperty, add);
  return facts;
}

export function factCount(facts: ElementFact[]): {issues: number} {
  return {issues: facts.filter(fact => fact.severity !== FactSeverity.NONE).length};
}

/**
 * An axis belongs here only when the element itself decides it. A controller on
 * another node means the value is explained by that node's rung.
 */
function axisRow(axis: AxisExplanation, selectedNodeId: number|null): ElementFact|null {
  if (axis.axis !== 'width' && axis.axis !== 'height') {
    return null;
  }
  const controller = axis.controller;
  if (controller && controller.nodeId > 0 && controller.nodeId !== selectedNodeId) {
    return null;
  }
  const source = controller?.property ?? mechanismWord(axis.mechanism);
  if (!source) {
    return null;
  }
  const title = controller?.property && controller.value ? `${controller.property}: ${controller.value}` : source;
  return {
    property: axis.axis,
    used: `${axis.usedValue}px`,
    source,
    severity: FactSeverity.NONE,
    declarationId: controller?.hasSource ? controller.declarationId : null,
    title,
  };
}

function contributionRows(axis: AxisExplanation): ElementFact[] {
  const rows: ElementFact[] = [];
  for (const contribution of axis.contributions) {
    if (!contribution.property || contribution.role === ContributionRole.INEFFECTIVE) {
      continue;
    }
    if (contribution.declarationId !== null && contribution.declarationId === axis.controller?.declarationId) {
      continue;
    }
    rows.push({
      property: contribution.property,
      used: contribution.value,
      source: roleWord(contribution.role),
      severity: FactSeverity.NONE,
      declarationId: contribution.hasSource ? contribution.declarationId : null,
      title: `${contribution.property}: ${contribution.value}`,
    });
  }
  return rows;
}

/**
 * Only the selected element's own margin and padding. A neighbour's margin is
 * reported on that neighbour's rung, where the gap is measured.
 */
function spacingRows(relationships: SpacingRelationship[], selectedNodeId: number|null): ElementFact[] {
  const rows: ElementFact[] = [];
  for (const relationship of relationships) {
    for (const contributor of relationship.contributors) {
      if (contributor.type === SpacingContributorType.GAP) {
        continue;
      }
      if (selectedNodeId === null || contributor.nodeId !== selectedNodeId) {
        continue;
      }
      const declared = contributor.declaredValue;
      const token = declared && declared !== `${contributor.value}px` ? declared : '';
      rows.push({
        property: contributor.property,
        used: `${contributor.value}px`,
        source: token,
        severity: FactSeverity.NONE,
        declarationId: contributor.hasSource ? contributor.declarationId : null,
        title: declared ? `${contributor.property}: ${declared}` : contributor.property,
      });
    }
  }
  return rows;
}

function layeringRow(layering: FactsLayering|null): ElementFact|null {
  if (!layering?.trapped || !layering.zIndex || layering.zIndex === 'auto') {
    return null;
  }
  const where = layering.trapLabel ? `trapped by ${layering.trapLabel}` : 'trapped';
  return {
    property: 'z-index',
    used: layering.zIndex,
    source: 'trapped',
    severity: FactSeverity.CRITICAL,
    declarationId: null,
    title: where,
  };
}

function applyDiagnostics(
    diagnostics: Diagnostic[], byProperty: Map<string, ElementFact>, add: (fact: ElementFact) => void): void {
  const seen = new Set<string>();
  for (const diagnostic of diagnostics) {
    const key = `${diagnostic.kind}:${diagnostic.property ?? ''}:${diagnostic.declarationId ?? ''}`;
    if (seen.has(key)) {
      continue;
    }
    seen.add(key);
    const copy = presentDiagnostic(diagnostic);
    const word = diagnosticWord(diagnostic.kind);
    const title = copy.detail || copy.headline;
    const existing = diagnostic.property ? byProperty.get(diagnostic.property) : undefined;
    if (existing) {
      // Keep the strongest verdict already on the row.
      if (existing.severity === FactSeverity.CRITICAL) {
        continue;
      }
      existing.source = word;
      existing.severity = FactSeverity.WARNING;
      existing.title = title;
      if (existing.declarationId === null && diagnostic.hasSource) {
        existing.declarationId = diagnostic.declarationId;
      }
      continue;
    }
    add({
      property: diagnostic.property ?? copy.category,
      used: diagnostic.value ?? '',
      source: word,
      severity: FactSeverity.WARNING,
      declarationId: diagnostic.hasSource ? diagnostic.declarationId : null,
      title,
    });
  }
}

function diagnosticWord(kind: DiagnosticKind): string {
  switch (kind) {
    case DiagnosticKind.CASCADE_OVERRIDE:
      return 'overridden';
    case DiagnosticKind.ANIMATION_OVERRIDE:
      return 'animated over';
    case DiagnosticKind.PERCENTAGE_HEIGHT:
      return 'no effect';
    case DiagnosticKind.ZERO_FREE_SPACE:
      return 'no free space';
    case DiagnosticKind.VALIDATOR:
      return 'does not apply';
  }
}

function roleWord(role: ContributionRole): string {
  switch (role) {
    case ContributionRole.CONSTRAINS:
      return 'constrains';
    case ContributionRole.CONTRIBUTES:
      return 'contributes';
    case ContributionRole.SPECIFIES:
      return 'specified';
    case ContributionRole.INEFFECTIVE:
      return '';
  }
}

function mechanismWord(mechanism: Mechanism): string {
  switch (mechanism) {
    case Mechanism.EXPLICIT:
      return 'specified';
    case Mechanism.MIN_SIZE:
      return 'min size';
    case Mechanism.MAX_SIZE:
      return 'max size';
    case Mechanism.PERCENTAGE:
      return 'percentage';
    case Mechanism.VIEWPORT:
      return 'viewport';
    case Mechanism.INTRINSIC:
      return 'intrinsic';
    case Mechanism.BLOCK_AVAILABLE:
      return 'available';
    case Mechanism.CONTENT:
      return 'content';
    case Mechanism.FLEX_LAYOUT:
    case Mechanism.FLEX_STRETCH:
      return 'flex';
    case Mechanism.GRID_LAYOUT:
    case Mechanism.GRID_STRETCH:
      return 'grid';
    case Mechanism.INSET_DERIVED:
      return 'insets';
    case Mechanism.AUTO_MARGINS:
      return 'auto margins';
    case Mechanism.BLOCK_FLOW:
      return 'block flow';
    case Mechanism.FLEX_PLACEMENT:
      return 'flex placement';
    case Mechanism.GRID_PLACEMENT:
      return 'grid placement';
    case Mechanism.RELATIVE:
      return 'relative';
    case Mechanism.ABSOLUTE:
      return 'absolute';
    case Mechanism.FIXED:
      return 'fixed';
    case Mechanism.STICKY:
      return 'sticky';
    case Mechanism.UNKNOWN:
      return '';
  }
}
