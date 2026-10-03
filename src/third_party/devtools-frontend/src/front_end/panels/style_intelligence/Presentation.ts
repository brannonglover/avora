// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Default presentation for Style Intelligence. The analysis model stays
 * detailed. This view model is the short scan, with secondary evidence in
 * details.
 */

import {type Diagnostic, DiagnosticKind, presentDiagnostic} from './Diagnostics.js';
import {formatGeometry, formatLayoutStatement, type LayoutStatement, type StyleIntelligenceSummary} from './ElementSummary.js';
import {type AxisExplanation, type Contribution, type ControllerRef, type LayoutExplanation, presentAxis} from './LayoutExplanation.js';
import {presentSpacing, type SpacingContributor, type SpacingExplanation, type SpacingRelationship} from './SpacingExplanation.js';

export interface PaneAction {
  nodeId: number|null;
  declarationId: number|null;
}

export interface PaneControl {
  text: string;
  title: string;
  actions: PaneAction;
}

export interface PaneDiagnostic {
  category: string;
  headline: string;
  detail: string;
  declarationId: number|null;
}

export interface PaneRow {
  label: string;
  primary: string;
  note: string|null;
  controls: PaneControl[];
  details: string[];
  diagnostics: PaneDiagnostic[];
}

export interface PaneToken {
  label: string;
  name: string;
  value: string|null;
  title: string;
}

export interface PaneSection {
  title: string;
  lines: string[];
  rows: PaneRow[];
  tokens: PaneToken[];
  diagnostics: PaneDiagnostic[];
}

export interface PaneViewModel {
  identity: string;
  size: string|null;
  sections: PaneSection[];
}

const FLEX_LONGHANDS = new Set(['flex-grow', 'flex-shrink', 'flex-basis']);
const MARGIN_LONGHANDS = new Set(['margin-top', 'margin-right', 'margin-bottom', 'margin-left']);
const PADDING_LONGHANDS = new Set(['padding-top', 'padding-right', 'padding-bottom', 'padding-left']);
const GAP_LONGHANDS = new Set(['row-gap', 'column-gap']);

export function presentPane(
    summary: StyleIntelligenceSummary, explanation: LayoutExplanation|null, spacing: SpacingExplanation|null,
    diagnostics: Diagnostic[], selectedNodeId: number|null = null): PaneViewModel {
  const shown = visibleDiagnostics(diagnostics);
  const axes = explanation?.axes ?? [];
  const sections: PaneSection[] = [];
  const seenNodes = new Set<number>();
  const seenDeclarations = new Set<number>();
  const layout = layoutSection(summary, axes, shown, spacing, seenNodes, seenDeclarations);
  if (layout) {
    sections.push(layout);
  }
  const size = axisSection('Size', ['width', 'height'], axes, shown, selectedNodeId, seenNodes, seenDeclarations);
  if (size) {
    sections.push(size);
  }
  const position = axisSection('Position', ['x', 'y'], axes, shown, selectedNodeId, seenNodes, seenDeclarations);
  if (position) {
    sections.push(position);
  }
  const spacingSection = spacingRows(spacing, shown, selectedNodeId, seenNodes, seenDeclarations);
  if (spacingSection) {
    sections.push(spacingSection);
  }
  if (summary.tokens.length) {
    sections.push({
      title: 'Tokens',
      lines: [],
      rows: [],
      tokens: summary.tokens.map(token => ({
        label: token.label,
        name: token.customProperty,
        value: token.resolvedValue ? displayValue(token.resolvedValue) : null,
        title: token.resolvedValue ?? token.customProperty,
      })),
      diagnostics: [],
    });
  }
  const loose = shown.filter(diagnostic => !isAttached(diagnostic, axes, spacing));
  if (loose.length) {
    sections.push({
      title: 'Potential issues',
      lines: [],
      rows: [],
      tokens: [],
      diagnostics: loose.map(paneDiagnostic),
    });
  }
  return {
    identity: summary.identity.label,
    size: summary.geometry ? formatGeometry(summary.geometry) : null,
    sections,
  };
}

function layoutSection(
    summary: StyleIntelligenceSummary, axes: AxisExplanation[], diagnostics: Diagnostic[],
    spacing: SpacingExplanation|null, seenNodes: Set<number>, seenDeclarations: Set<number>): PaneSection|null {
  const positionProperties = new Set(axes.filter(axis => axis.axis === 'x' || axis.axis === 'y')
                                       .map(axis => axis.controller?.property)
                                       .filter((property): property is string => Boolean(property)));
  const lines = summary.layout.filter(statement => keepLayoutLine(statement, positionProperties, spacing))
                    .map(formatLayoutStatement);
  const layoutDiagnostics = diagnostics.filter(diagnostic => diagnostic.placement === 'layout')
                               .map(diagnostic => paneDiagnostic(diagnostic, seenNodes, seenDeclarations));
  if (!lines.length && !layoutDiagnostics.length) {
    return null;
  }
  return {title: 'Layout', lines, rows: [], tokens: [], diagnostics: layoutDiagnostics};
}

function keepLayoutLine(
    statement: LayoutStatement, positionProperties: Set<string>, spacing: SpacingExplanation|null): boolean {
  if (statement.type === 'align-items' && positionProperties.has('align-items')) {
    return false;
  }
  if (statement.type === 'justify-content' && positionProperties.has('justify-content')) {
    return false;
  }
  if (statement.type === 'gap' &&
      spacing?.relationships.some(relationship => relationship.contributors.some(contributor => contributor.type === 'gap'))) {
    return false;
  }
  return true;
}

function axisSection(
    title: string, keys: Array<AxisExplanation['axis']>, axes: AxisExplanation[], diagnostics: Diagnostic[],
    selectedNodeId: number|null, seenNodes: Set<number>, seenDeclarations: Set<number>): PaneSection|null {
  const rows: PaneRow[] = [];
  for (const key of keys) {
    const axis = axes.find(item => item.axis === key);
    if (!axis) {
      continue;
    }
    rows.push(axisRow(
        axis, diagnostics.filter(diagnostic => diagnostic.placement === key), seenNodes, seenDeclarations, selectedNodeId));
  }
  if (!rows.length) {
    return null;
  }
  return {title, lines: [], rows, tokens: [], diagnostics: []};
}

function axisRow(
    axis: AxisExplanation, diagnostics: Diagnostic[], seenNodes: Set<number>, seenDeclarations: Set<number>,
    selectedNodeId: number|null): PaneRow {
  const copy = presentAxis(axis);
  const controls: PaneControl[] = [];
  if (axis.controller) {
    const text = controlText(axis.controller);
    if (text) {
      controls.push({
        text,
        title: text,
        actions: actionsFor(axis.controller.nodeId, axis.controller.hasSource ? axis.controller.declarationId : null, seenNodes, seenDeclarations, selectedNodeId),
      });
    }
  }
  const details: string[] = [];
  if (axis.axis === 'x') {
    details.push(`${axis.usedValue}px from the left edge of the viewport`);
  } else if (axis.axis === 'y') {
    details.push(`${axis.usedValue}px from the top edge of the viewport`);
  }
  for (const contribution of axis.contributions) {
    if (!contribution.property || contribution.role === 'ineffective') {
      continue;
    }
    if (contribution.declarationId !== null && contribution.declarationId === axis.controller?.declarationId) {
      continue;
    }
    const line = contributionLine(contribution, `${contribution.property}: ${contribution.value}`);
    controls.push({
      text: line,
      title: line,
      actions: actionsFor(null, contribution.hasSource ? contribution.declarationId : null, seenNodes, seenDeclarations, selectedNodeId),
    });
  }
  const percentage = diagnostics.some(diagnostic => diagnostic.kind === DiagnosticKind.PERCENTAGE_HEIGHT);
  if (copy.caveat && !percentage) {
    details.push(copy.caveat);
  }
  return {
    label: copy.label === 'X position' ? 'X' : copy.label === 'Y position' ? 'Y' : copy.label,
    primary: axisPrimary(axis, copy.headline, copy.value),
    note: null,
    controls,
    details,
    diagnostics: diagnostics.map(diagnostic => paneDiagnostic(diagnostic, seenNodes, seenDeclarations)),
  };
}

function axisPrimary(axis: AxisExplanation, headline: string, value: string): string {
  if (axis.axis === 'x' || axis.axis === 'y') {
    return headline;
  }
  if (axis.specifiedMatchesContentBox) {
    return headline;
  }
  if (headline.startsWith('Specified as ')) {
    return `${value} · specified`;
  }
  if (headline === 'Content width' || headline === 'Content height') {
    return `${value} · content`;
  }
  return `${value} · ${headline}`;
}

function contributionLine(contribution: Contribution, line: string): string {
  if (contribution.role === 'constrains') {
    return `Constrained by ${line}`;
  }
  if (contribution.role === 'contributes') {
    return `${line} contributes`;
  }
  return line;
}

function spacingRows(
    spacing: SpacingExplanation|null, diagnostics: Diagnostic[], selectedNodeId: number|null, seenNodes: Set<number>,
    seenDeclarations: Set<number>): PaneSection|null {
  const relationships = spacing?.relationships ?? [];
  const spacingDiagnostics = relationships.length ? diagnostics.filter(diagnostic => diagnostic.placement === 'spacing') : [];
  if (!relationships.length && !spacingDiagnostics.length) {
    return null;
  }
  const rows = relationships.map(relationship => spacingRow(relationship, seenNodes, seenDeclarations, selectedNodeId));
  return {
    title: 'Spacing',
    lines: [],
    rows,
    tokens: [],
    diagnostics: spacingDiagnostics.map(diagnostic => paneDiagnostic(diagnostic, seenNodes, seenDeclarations)),
  };
}

function spacingRow(
    relationship: SpacingRelationship, seenNodes: Set<number>, seenDeclarations: Set<number>,
    selectedNodeId: number|null): PaneRow {
  const copy = presentSpacing(relationship);
  const controls: PaneControl[] = [];
  if (relationship.neighbor && relationship.neighbor.nodeId > 0) {
    controls.push({
      text: relationship.neighbor.label,
      title: relationship.neighbor.label,
      actions: actionsFor(relationship.neighbor.nodeId, null, seenNodes, seenDeclarations, selectedNodeId),
    });
  }
  for (const contributor of relationship.contributors) {
    const text = `${contributor.property} · ${contributor.value}px`;
    controls.push({
      text,
      title: contributorTitle(contributor),
      actions: actionsFor(
          contributor.nodeId > 0 ? contributor.nodeId : null,
          contributor.hasSource ? contributor.declarationId : null, seenNodes, seenDeclarations, selectedNodeId),
    });
  }
  const details: string[] = [];
  for (const contributor of relationship.contributors) {
    if (contributor.declaredValue && contributor.declaredValue !== `${contributor.value}px`) {
      details.push(`${contributor.property}: ${contributor.declaredValue}`);
    }
  }
  return {
    label: shortSpacingLabel(relationship),
    primary: `${relationship.measuredGap}px`,
    note: copy.marginCollapse ?? copy.unexplained,
    controls,
    details,
    diagnostics: [],
  };
}

function shortSpacingLabel(relationship: SpacingRelationship): string {
  const inside = relationship.relationship === 'parent-start' || relationship.relationship === 'parent-end';
  const vertical = relationship.axis === 'vertical';
  const start = relationship.relationship === 'previous-sibling' || relationship.relationship === 'parent-start';
  const side = vertical ? (start ? 'Above' : 'Below') : (start ? 'Left' : 'Right');
  return inside ? `Inside, ${side.toLowerCase()}` : side;
}

function contributorTitle(contributor: SpacingContributor): string {
  const declared = contributor.declaredValue ? `${contributor.property}: ${contributor.declaredValue}` : contributor.property;
  return contributor.label ? `${contributor.label} · ${declared}` : declared;
}

function controlText(controller: ControllerRef): string|null {
  const declaration = controller.property && controller.value ? `${controller.property}: ${controller.value}` : null;
  if (controller.label && declaration) {
    return `${controller.label} · ${declaration}`;
  }
  return controller.label || declaration;
}

function actionsFor(
    nodeId: number|null, declarationId: number|null, seenNodes: Set<number>, seenDeclarations: Set<number>,
    selectedNodeId: number|null): PaneAction {
  const showNode = nodeId !== null && nodeId > 0 && nodeId !== selectedNodeId && !seenNodes.has(nodeId);
  const showDeclaration = declarationId !== null && !seenDeclarations.has(declarationId);
  if (showNode && nodeId !== null) {
    seenNodes.add(nodeId);
  }
  if (showDeclaration && declarationId !== null) {
    seenDeclarations.add(declarationId);
  }
  return {
    nodeId: showNode ? nodeId : null,
    declarationId: showDeclaration ? declarationId : null,
  };
}

function paneDiagnostic(diagnostic: Diagnostic, seenNodes: Set<number>, seenDeclarations: Set<number>): PaneDiagnostic {
  const copy = presentDiagnostic(diagnostic);
  const actions = actionsFor(
      null, diagnostic.hasSource ? diagnostic.declarationId : null, seenNodes, seenDeclarations, null);
  return {
    category: copy.category,
    headline: copy.headline,
    detail: copy.detail,
    declarationId: actions.declarationId,
  };
}

function isAttached(diagnostic: Diagnostic, axes: AxisExplanation[], spacing: SpacingExplanation|null): boolean {
  if (diagnostic.placement === 'layout') {
    return true;
  }
  if (diagnostic.placement === 'spacing') {
    return Boolean(spacing?.relationships.length);
  }
  return axes.some(axis => axis.axis === diagnostic.placement);
}

function visibleDiagnostics(diagnostics: Diagnostic[]): Diagnostic[] {
  const cascadeProperties = new Set(diagnostics.filter(diagnostic => diagnostic.kind === DiagnosticKind.CASCADE_OVERRIDE)
                                         .map(diagnostic => diagnostic.property));
  const seen = new Set<string>();
  const visible: Diagnostic[] = [];
  for (const diagnostic of diagnostics) {
    if (diagnostic.kind === DiagnosticKind.CASCADE_OVERRIDE && hiddenLonghand(diagnostic.property, cascadeProperties)) {
      continue;
    }
    const key = `${diagnostic.kind}:${diagnostic.declarationId ?? ''}:${diagnostic.property ?? ''}`;
    if (seen.has(key)) {
      continue;
    }
    seen.add(key);
    visible.push(diagnostic);
  }
  return visible;
}

function hiddenLonghand(property: string|null, cascadeProperties: Set<string|null>): boolean {
  if (!property) {
    return false;
  }
  if (FLEX_LONGHANDS.has(property) && cascadeProperties.has('flex')) {
    return true;
  }
  if (MARGIN_LONGHANDS.has(property) && cascadeProperties.has('margin')) {
    return true;
  }
  if (PADDING_LONGHANDS.has(property) && cascadeProperties.has('padding')) {
    return true;
  }
  if (GAP_LONGHANDS.has(property) && cascadeProperties.has('gap')) {
    return true;
  }
  return false;
}

function displayValue(value: string): string {
  if (value.length <= 48) {
    return value;
  }
  return `${value.slice(0, 45)}…`;
}
