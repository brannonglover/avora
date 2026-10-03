// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Plain-data summary of one element.
 *
 * Measured values come from computed style, the box model, or an active
 * declaration. Inferred values are high-confidence readings of those facts
 * (for example, an in-flow child of a flex container is a flex item). Blink
 * does not return an "is flex item" flag. Inference never invents a pixel
 * size or a computed value.
 */

export const EvidenceKind = {
  MEASURED: 'measured',
  INFERRED: 'inferred',
} as const;

export type EvidenceKind = (typeof EvidenceKind)[keyof typeof EvidenceKind];

export interface ElementIdentity {
  kind: typeof EvidenceKind.MEASURED;
  label: string;
}

export interface MeasuredGeometry {
  kind: typeof EvidenceKind.MEASURED;
  width: number;
  height: number;
}

export type LayoutStatement = {
  kind: typeof EvidenceKind.MEASURED,
  type: 'not-rendered' | 'no-box' | 'block' | 'inline' | 'inline-block' | 'list-item' | 'wrap',
}|{
  kind: typeof EvidenceKind.MEASURED,
  type: 'flex-container',
  inline: boolean,
  direction: 'horizontal' | 'vertical' | null,
  reverse: boolean,
}|{
  kind: typeof EvidenceKind.MEASURED,
  type: 'grid-container',
  inline: boolean,
  subgrid: boolean,
}|{
  kind: typeof EvidenceKind.MEASURED,
  type: 'display',
  value: string,
}|{
  kind: typeof EvidenceKind.MEASURED,
  type: 'align-items' | 'justify-content',
  value: string,
}|{
  kind: typeof EvidenceKind.MEASURED,
  type: 'gap',
  text: string,
}|{
  kind: typeof EvidenceKind.MEASURED,
  type: 'position',
  value: string,
}|{
  kind: typeof EvidenceKind.INFERRED,
  type: 'flex-item' | 'grid-item',
};

export interface TokenUse {
  kind: typeof EvidenceKind.MEASURED;
  label: string;
  customProperty: string;
  /** Computed or cascade-resolved value. Null when Chromium did not produce a concrete value. */
  resolvedValue: string|null;
}

export interface StyleIntelligenceSummary {
  identity: ElementIdentity;
  geometry: MeasuredGeometry|null;
  layout: LayoutStatement[];
  tokens: TokenUse[];
}

export interface LayoutFacts {
  display: string|null;
  position: string|null;
  flexDirection: string|null;
  flexWrap: string|null;
  alignItems: string|null;
  justifyContent: string|null;
  rowGap: string|null;
  columnGap: string|null;
  gridTemplateColumns: string|null;
  gridTemplateRows: string|null;
  /** Computed display of the layout parent. Null when that parent is unknown. */
  parentDisplay: string|null;
  hasBox: boolean;
}

export interface TokenReference {
  propertyName: string;
  customProperty: string;
  resolvedValue: string|null;
}

const TOKEN_LABEL_ORDER = [
  'Background',
  'Color',
  'Border',
  'Radius',
  'Shadow',
  'Padding',
  'Gap',
  'Font',
  'Weight',
  'Line height',
];

export function formatElementIdentity(parts: {name: string, id?: string|null, classes?: string[]}): string {
  const classes = (parts.classes ?? []).map(className => className.trim()).filter(className => className.length > 0);
  let label = parts.name;
  if (parts.id) {
    label += `#${parts.id}`;
  }
  for (const className of classes) {
    label += `.${className}`;
  }
  return label;
}

export function formatGeometry(geometry: MeasuredGeometry): string {
  return `${geometry.width} × ${geometry.height}`;
}

export function customPropertiesInValue(value: string): string[] {
  const names: string[] = [];
  const pattern = /var\(\s*(--[A-Za-z_][\w-]*)/g;
  for (const match of value.matchAll(pattern)) {
    const name = match[1];
    if (!names.includes(name)) {
      names.push(name);
    }
  }
  return names;
}

export function tokenLabel(propertyName: string): string|null {
  switch (propertyName) {
    case 'background':
    case 'background-color':
    case 'background-image':
      return 'Background';
    case 'color':
      return 'Color';
    case 'border':
    case 'border-color':
    case 'border-top-color':
    case 'border-right-color':
    case 'border-bottom-color':
    case 'border-left-color':
      return 'Border';
    case 'border-radius':
    case 'border-top-left-radius':
    case 'border-top-right-radius':
    case 'border-bottom-right-radius':
    case 'border-bottom-left-radius':
      return 'Radius';
    case 'box-shadow':
      return 'Shadow';
    case 'padding':
    case 'padding-top':
    case 'padding-right':
    case 'padding-bottom':
    case 'padding-left':
    case 'padding-block':
    case 'padding-inline':
    case 'padding-block-start':
    case 'padding-block-end':
    case 'padding-inline-start':
    case 'padding-inline-end':
      return 'Padding';
    case 'gap':
    case 'row-gap':
    case 'column-gap':
      return 'Gap';
    case 'font':
    case 'font-size':
    case 'font-family':
      return 'Font';
    case 'font-weight':
      return 'Weight';
    case 'line-height':
      return 'Line height';
    default:
      return null;
  }
}

export function summarizeTokens(references: TokenReference[]): TokenUse[] {
  const seen = new Set<string>();
  const tokens: TokenUse[] = [];
  for (const reference of references) {
    const label = tokenLabel(reference.propertyName);
    if (!label) {
      continue;
    }
    const key = `${label}\0${reference.customProperty}\0${reference.resolvedValue ?? ''}`;
    if (seen.has(key)) {
      continue;
    }
    seen.add(key);
    tokens.push({
      kind: EvidenceKind.MEASURED,
      label,
      customProperty: reference.customProperty,
      resolvedValue: reference.resolvedValue,
    });
  }
  tokens.sort((left, right) => {
    const labelDelta = tokenLabelOrder(left.label) - tokenLabelOrder(right.label);
    if (labelDelta !== 0) {
      return labelDelta;
    }
    return left.customProperty.localeCompare(right.customProperty);
  });
  return tokens;
}

export function describeLayout(facts: LayoutFacts): LayoutStatement[] {
  const display = facts.display;
  if (display === 'none' && !facts.hasBox) {
    return [{kind: EvidenceKind.MEASURED, type: 'not-rendered'}];
  }
  if (display === 'contents' && !facts.hasBox) {
    return [{kind: EvidenceKind.MEASURED, type: 'no-box'}];
  }

  const statements: LayoutStatement[] = [];
  const position = facts.position;
  const outOfFlow = position === 'absolute' || position === 'fixed';
  const flexItem = isFlexItem(facts);
  const gridItem = isGridItem(facts);

  if (isFlexDisplay(display)) {
    const axis = flexAxis(facts.flexDirection);
    statements.push({
      kind: EvidenceKind.MEASURED,
      type: 'flex-container',
      inline: display === 'inline-flex',
      direction: axis?.direction ?? null,
      reverse: axis?.reverse ?? false,
    });
    appendFlexDetails(statements, facts);
  } else if (isGridDisplay(display)) {
    statements.push({
      kind: EvidenceKind.MEASURED,
      type: 'grid-container',
      inline: display === 'inline-grid' || display === 'inline-grid-lanes',
      subgrid: isSubgrid(facts),
    });
    appendGap(statements, facts);
  } else if (!flexItem && !gridItem && !outOfFlow) {
    appendOwnDisplay(statements, display);
  }

  if (position && position !== 'static') {
    statements.push({kind: EvidenceKind.MEASURED, type: 'position', value: position});
  }
  if (flexItem) {
    statements.push({kind: EvidenceKind.INFERRED, type: 'flex-item'});
  }
  if (gridItem) {
    statements.push({kind: EvidenceKind.INFERRED, type: 'grid-item'});
  }
  return statements;
}

export function formatLayoutStatement(statement: LayoutStatement): string {
  switch (statement.type) {
    case 'not-rendered':
      return 'Not rendered';
    case 'no-box':
      return 'Does not generate a box';
    case 'block':
      return 'Block';
    case 'inline':
      return 'Inline';
    case 'inline-block':
      return 'Inline block';
    case 'list-item':
      return 'List item';
    case 'wrap':
      return 'Wraps';
    case 'flex-container': {
      const placement = statement.inline ? 'Inline flex container' : 'Flex container';
      if (!statement.direction) {
        return placement;
      }
      const axis = statement.direction === 'horizontal' ? 'horizontal' : 'vertical';
      return statement.reverse ? `${placement} · ${axis}, reversed` : `${placement} · ${axis}`;
    }
    case 'grid-container':
      if (statement.subgrid) {
        return statement.inline ? 'Inline subgrid' : 'Subgrid';
      }
      return statement.inline ? 'Inline grid' : 'Grid';
    case 'display':
      return statement.value;
    case 'align-items':
      return `Items aligned ${statement.value}`;
    case 'justify-content':
      return `Main axis ${statement.value}`;
    case 'gap':
      return statement.text;
    case 'position':
      switch (statement.value) {
        case 'absolute':
          return 'Absolutely positioned';
        case 'fixed':
          return 'Fixed positioning';
        case 'sticky':
        case '-webkit-sticky':
          return 'Sticky';
        case 'relative':
          return 'Relative';
        default:
          return statement.value;
      }
    case 'flex-item':
      return 'Flex item';
    case 'grid-item':
      return 'Grid item';
  }
}

export function buildSummary(
    identityLabel: string, facts: LayoutFacts, geometry: {width: number, height: number}|null,
    references: TokenReference[]): StyleIntelligenceSummary {
  return {
    identity: {kind: EvidenceKind.MEASURED, label: identityLabel},
    geometry: geometry ? {kind: EvidenceKind.MEASURED, width: geometry.width, height: geometry.height} : null,
    layout: describeLayout(facts),
    tokens: summarizeTokens(references),
  };
}

function appendOwnDisplay(statements: LayoutStatement[], display: string|null): void {
  switch (display) {
    case 'block':
    case 'flow-root':
      statements.push({kind: EvidenceKind.MEASURED, type: 'block'});
      break;
    case 'inline':
      statements.push({kind: EvidenceKind.MEASURED, type: 'inline'});
      break;
    case 'inline-block':
      statements.push({kind: EvidenceKind.MEASURED, type: 'inline-block'});
      break;
    case 'list-item':
      statements.push({kind: EvidenceKind.MEASURED, type: 'list-item'});
      break;
    case null:
    case 'flex':
    case 'inline-flex':
    case 'grid':
    case 'inline-grid':
    case 'grid-lanes':
    case 'inline-grid-lanes':
    case 'none':
    case 'contents':
      break;
    default:
      statements.push({kind: EvidenceKind.MEASURED, type: 'display', value: display});
      break;
  }
}

function appendFlexDetails(statements: LayoutStatement[], facts: LayoutFacts): void {
  if (facts.flexWrap === 'wrap' || facts.flexWrap === 'wrap-reverse') {
    statements.push({kind: EvidenceKind.MEASURED, type: 'wrap'});
  }
  if (facts.alignItems && !isDefaultAlign(facts.alignItems)) {
    statements.push({kind: EvidenceKind.MEASURED, type: 'align-items', value: facts.alignItems});
  }
  if (facts.justifyContent && !isDefaultJustify(facts.justifyContent)) {
    statements.push({kind: EvidenceKind.MEASURED, type: 'justify-content', value: facts.justifyContent});
  }
  appendGap(statements, facts);
}

function appendGap(statements: LayoutStatement[], facts: LayoutFacts): void {
  const row = isDefaultGap(facts.rowGap) ? null : facts.rowGap;
  const column = isDefaultGap(facts.columnGap) ? null : facts.columnGap;
  if (!row && !column) {
    return;
  }
  let text: string;
  if (row && column && row !== column) {
    text = `Row gap ${row} · column gap ${column}`;
  } else {
    text = `${row ?? column} gap`;
  }
  statements.push({kind: EvidenceKind.MEASURED, type: 'gap', text});
}

function isFlexItem(facts: LayoutFacts): boolean {
  if (facts.display === 'none' || facts.display === 'contents') {
    return false;
  }
  if (!isFlexDisplay(facts.parentDisplay)) {
    return false;
  }
  return isInFlowPosition(facts.position);
}

function isGridItem(facts: LayoutFacts): boolean {
  if (facts.display === 'none' || facts.display === 'contents') {
    return false;
  }
  if (!isGridDisplay(facts.parentDisplay)) {
    return false;
  }
  return isInFlowPosition(facts.position);
}

function isInFlowPosition(position: string|null): boolean {
  return position === 'static' || position === 'relative' || position === 'sticky' || position === '-webkit-sticky';
}

function isFlexDisplay(display: string|null): boolean {
  return display === 'flex' || display === 'inline-flex';
}

function isGridDisplay(display: string|null): boolean {
  return display === 'grid' || display === 'inline-grid' || display === 'grid-lanes' ||
      display === 'inline-grid-lanes';
}

function isSubgrid(facts: LayoutFacts): boolean {
  return Boolean(facts.gridTemplateColumns?.startsWith('subgrid') || facts.gridTemplateRows?.startsWith('subgrid'));
}

function flexAxis(flexDirection: string|null): {direction: 'horizontal' | 'vertical', reverse: boolean}|null {
  switch (flexDirection) {
    case 'row':
      return {direction: 'horizontal', reverse: false};
    case 'row-reverse':
      return {direction: 'horizontal', reverse: true};
    case 'column':
      return {direction: 'vertical', reverse: false};
    case 'column-reverse':
      return {direction: 'vertical', reverse: true};
    default:
      return null;
  }
}

function isDefaultGap(value: string|null): boolean {
  if (!value) {
    return true;
  }
  const normalized = value.trim();
  return normalized === 'normal' || normalized === '0px' || normalized === '0';
}

function isDefaultAlign(value: string): boolean {
  return value === 'normal' || value === 'stretch';
}

function isDefaultJustify(value: string): boolean {
  return value === 'normal' || value === 'flex-start' || value === 'start';
}

function tokenLabelOrder(label: string): number {
  const index = TOKEN_LABEL_ORDER.indexOf(label);
  return index === -1 ? TOKEN_LABEL_ORDER.length : index;
}
