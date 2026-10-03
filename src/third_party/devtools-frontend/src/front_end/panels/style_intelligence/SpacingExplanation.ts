// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Measured spacing between border boxes, kept separate from the declarations
 * that may contribute to it. Contributors are omitted when the used value
 * cannot be established. They are not forced to sum to the measured gap.
 */

import {Confidence, type DeclarationFact} from './LayoutExplanation.js';

export const SpacingAxis = {
  HORIZONTAL: 'horizontal',
  VERTICAL: 'vertical',
} as const;

export type SpacingAxis = (typeof SpacingAxis)[keyof typeof SpacingAxis];

export const SpacingRelationshipKind = {
  PREVIOUS_SIBLING: 'previous-sibling',
  NEXT_SIBLING: 'next-sibling',
  PARENT_START: 'parent-start',
  PARENT_END: 'parent-end',
} as const;

export type SpacingRelationshipKind = (typeof SpacingRelationshipKind)[keyof typeof SpacingRelationshipKind];

export const SpacingContributorType = {
  GAP: 'gap',
  MARGIN: 'margin',
  PADDING: 'padding',
} as const;

export type SpacingContributorType = (typeof SpacingContributorType)[keyof typeof SpacingContributorType];

export const SpacingDeclineReason = {
  OUT_OF_FLOW: 'out-of-flow',
  TRANSFORMED: 'transformed',
  TRANSFORMED_SIBLING: 'transformed-sibling',
  OUT_OF_FLOW_BETWEEN: 'out-of-flow-between',
  SIBLINGS_INCOMPLETE: 'siblings-incomplete',
  AMBIGUOUS_NEIGHBOR: 'ambiguous-neighbor',
  POSITION_UNKNOWN: 'position-unknown',
} as const;

export type SpacingDeclineReason = (typeof SpacingDeclineReason)[keyof typeof SpacingDeclineReason];

export interface SpacingContributor {
  type: SpacingContributorType;
  /** Used pixels. This is not the authored token when the two differ. */
  value: number;
  nodeId: number;
  label: string;
  property: string;
  declaredValue: string|null;
  declarationId: number|null;
  hasSource: boolean;
  confidence: Confidence;
  /** Limited-confidence contributors stay visible but do not reduce unexplained. */
  countsTowardGap: boolean;
}

export interface SpacingNeighbor {
  nodeId: number;
  label: string;
}

export interface SpacingRelationship {
  measuredGap: number;
  axis: SpacingAxis;
  relationship: SpacingRelationshipKind;
  neighbor: SpacingNeighbor|null;
  contributors: SpacingContributor[];
  /** measuredGap minus contributors that count. Not rounded away. */
  unexplained: number;
  /**
   * Facing margins participate, but the collapsed used value cannot be read
   * from computed style. Authored margin pixels are not claimed.
   */
  marginCollapseUnknown: boolean;
}

export interface SpacingDecline {
  reason: SpacingDeclineReason;
  axis: SpacingAxis|null;
}

export interface SpacingExplanation {
  relationships: SpacingRelationship[];
  declined: SpacingDecline[];
}

export interface SpacingBox {
  nodeId: number;
  label: string;
  x: number;
  y: number;
  width: number;
  height: number;
  display: string|null;
  position: string|null;
  transformNone: boolean;
  marginTop: number|null;
  marginRight: number|null;
  marginBottom: number|null;
  marginLeft: number|null;
  marginTopDeclaration: DeclarationFact|null;
  marginRightDeclaration: DeclarationFact|null;
  marginBottomDeclaration: DeclarationFact|null;
  marginLeftDeclaration: DeclarationFact|null;
}

export interface SpacingParent {
  nodeId: number;
  label: string;
  hasBox: boolean;
  display: string|null;
  flexDirection: string|null;
  flexWrap: string|null;
  writingMode: string|null;
  x: number;
  y: number;
  width: number;
  height: number;
  borderTop: number;
  borderRight: number;
  borderBottom: number;
  borderLeft: number;
  paddingTop: number;
  paddingRight: number;
  paddingBottom: number;
  paddingLeft: number;
  rowGap: number|null;
  columnGap: number|null;
  rowGapDeclaration: DeclarationFact|null;
  columnGapDeclaration: DeclarationFact|null;
  paddingTopDeclaration: DeclarationFact|null;
  paddingRightDeclaration: DeclarationFact|null;
  paddingBottomDeclaration: DeclarationFact|null;
  paddingLeftDeclaration: DeclarationFact|null;
}

export interface SpacingInput {
  selected: SpacingBox;
  parent: SpacingParent|null;
  siblings: SpacingBox[];
  /** A sibling that could affect adjacency was not measured. */
  siblingsIncomplete: boolean;
}

interface Edges {
  left: number;
  right: number;
  top: number;
  bottom: number;
}

export function explainSpacing(input: SpacingInput): SpacingExplanation {
  const selected = input.selected;
  if (selected.position === null) {
    return {relationships: [], declined: [{reason: SpacingDeclineReason.POSITION_UNKNOWN, axis: null}]};
  }
  if (isOutOfFlow(selected.position)) {
    return {relationships: [], declined: [{reason: SpacingDeclineReason.OUT_OF_FLOW, axis: null}]};
  }
  if (!selected.transformNone) {
    return {relationships: [], declined: [{reason: SpacingDeclineReason.TRANSFORMED, axis: null}]};
  }
  if (input.siblingsIncomplete) {
    return {relationships: [], declined: [{reason: SpacingDeclineReason.SIBLINGS_INCOMPLETE, axis: null}]};
  }

  const declined: SpacingDecline[] = [];
  const relationships: SpacingRelationship[] = [];
  const selectedEdges = edges(selected);

  for (const axis of [SpacingAxis.VERTICAL, SpacingAxis.HORIZONTAL]) {
    const previous = nearestNeighbor(selected, selectedEdges, input.siblings, axis, 'previous');
    const next = nearestNeighbor(selected, selectedEdges, input.siblings, axis, 'next');
    if (previous.kind === 'ambiguous' || next.kind === 'ambiguous') {
      declined.push({reason: SpacingDeclineReason.AMBIGUOUS_NEIGHBOR, axis});
    }
    if (previous.kind === 'interfering-transform' || next.kind === 'interfering-transform') {
      declined.push({reason: SpacingDeclineReason.TRANSFORMED_SIBLING, axis});
    }
    if (previous.kind === 'found') {
      const between = outOfFlowBetween(
          selectedEdges, edges(previous.box), input.siblings, axis, SpacingRelationshipKind.PREVIOUS_SIBLING);
      if (between) {
        declined.push({reason: SpacingDeclineReason.OUT_OF_FLOW_BETWEEN, axis});
      } else {
        relationships.push(siblingRelationship(
            axis, SpacingRelationshipKind.PREVIOUS_SIBLING, selected, previous.box, input.parent));
      }
    }
    if (next.kind === 'found') {
      const between = outOfFlowBetween(
          selectedEdges, edges(next.box), input.siblings, axis, SpacingRelationshipKind.NEXT_SIBLING);
      if (between) {
        declined.push({reason: SpacingDeclineReason.OUT_OF_FLOW_BETWEEN, axis});
      } else {
        relationships.push(siblingRelationship(
            axis, SpacingRelationshipKind.NEXT_SIBLING, selected, next.box, input.parent));
      }
    }
    if (previous.kind === 'none' && input.parent?.hasBox) {
      const edge = parentEdge(axis, SpacingRelationshipKind.PARENT_START, selected, selectedEdges, input.parent);
      if (edge) {
        relationships.push(edge);
      }
    }
    if (next.kind === 'none' && input.parent?.hasBox) {
      const edge = parentEdge(axis, SpacingRelationshipKind.PARENT_END, selected, selectedEdges, input.parent);
      if (edge) {
        relationships.push(edge);
      }
    }
  }

  return {relationships: relationships.filter(keepRelationship), declined: uniqueDeclines(declined)};
}

function uniqueDeclines(declined: SpacingDecline[]): SpacingDecline[] {
  const seen = new Set<string>();
  return declined.filter(item => {
    const key = `${item.reason}:${item.axis ?? ''}`;
    if (seen.has(key)) {
      return false;
    }
    seen.add(key);
    return true;
  });
}

export interface SpacingCopy {
  label: string;
  measured: string;
  unexplained: string|null;
  marginCollapse: string|null;
}

export function presentSpacing(relationship: SpacingRelationship): SpacingCopy {
  return {
    label: spacingLabel(relationship),
    measured: `${relationship.measuredGap}px measured`,
    unexplained: relationship.marginCollapseUnknown ? null : unexplainedCopy(relationship.unexplained),
    marginCollapse: relationship.marginCollapseUnknown ?
        'Margins participate in this spacing, but their final collapsed contribution cannot be determined confidently from the available DevTools data.' :
        null,
  };
}

function keepRelationship(relationship: SpacingRelationship): boolean {
  if (Math.abs(relationship.measuredGap) > 1) {
    return true;
  }
  return relationship.marginCollapseUnknown || relationship.contributors.some(contributor => contributor.value !== 0);
}

function spacingLabel(relationship: SpacingRelationship): string {
  const insideParent = relationship.relationship === SpacingRelationshipKind.PARENT_START ||
      relationship.relationship === SpacingRelationshipKind.PARENT_END;
  if (relationship.axis === SpacingAxis.VERTICAL) {
    const side = relationship.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING ||
            relationship.relationship === SpacingRelationshipKind.PARENT_START ?
        'above' :
        'below';
    return insideParent ? `Inside parent, ${side}` : `Space ${side}`;
  }
  const side = relationship.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING ||
          relationship.relationship === SpacingRelationshipKind.PARENT_START ?
      'left' :
      'right';
  return insideParent ? `Inside parent, ${side}` : `Space to the ${side}`;
}

function unexplainedCopy(unexplained: number): string|null {
  if (Math.abs(unexplained) <= 1) {
    return null;
  }
  if (unexplained > 0) {
    return `${unexplained}px could not be attributed confidently.`;
  }
  return `${Math.abs(unexplained)}px of overlap could not be attributed confidently.`;
}

function siblingRelationship(
    axis: SpacingAxis, kind: SpacingRelationshipKind, selected: SpacingBox, neighbor: SpacingBox,
    parent: SpacingParent|null): SpacingRelationship {
  const measuredGap = gapBetween(selected, neighbor, axis, kind);
  const contributors: SpacingContributor[] = [];
  const gap = gapContributor(parent, axis, kind);
  if (gap) {
    contributors.push(gap);
  }
  const marginMode = marginModeFor(parent, axis, selected, neighbor);
  let marginCollapseUnknown = false;
  if (marginMode === 'count') {
    contributors.push(...facingMargins(selected, neighbor, axis, kind));
  } else if (marginMode === 'collapse' && facingMarginParticipates(selected, neighbor, axis, kind)) {
    marginCollapseUnknown = true;
  }
  return finish(measuredGap, axis, kind, {nodeId: neighbor.nodeId, label: neighbor.label}, contributors, marginCollapseUnknown);
}

function parentEdge(
    axis: SpacingAxis, kind: SpacingRelationshipKind, selected: SpacingBox, selectedEdges: Edges,
    parent: SpacingParent): SpacingRelationship|null {
  const measuredGap = parentGap(selectedEdges, parent, axis, kind);
  const padding = paddingContributor(parent, axis, kind);
  const margin = edgeMargin(selected, axis, kind);
  const separated = padding !== null && padding.value > 0 || separatingBorder(parent, axis, kind);
  const marginMode = parentMarginMode(parent, axis, separated);
  const contributors: SpacingContributor[] = [];
  if (padding && padding.value !== 0) {
    contributors.push(padding);
  }
  let marginCollapseUnknown = false;
  if (margin && marginMode === 'count') {
    contributors.push(margin);
  } else if (marginMode === 'collapse' && marginParticipates(margin, selected, axis, kind)) {
    marginCollapseUnknown = true;
  }
  if (Math.abs(measuredGap) <= 1) {
    return null;
  }
  if (!marginCollapseUnknown && contributors.length === 0) {
    return null;
  }
  const relationship = finish(
      measuredGap, axis, kind, {nodeId: parent.nodeId, label: parent.label}, contributors, marginCollapseUnknown);
  // A narrow box leaves empty room before the parent's far edge. That room is
  // not a spacing relationship unless padding or margin accounts for it.
  if (!relationship.marginCollapseUnknown &&
      Math.abs(relationship.unexplained) > Math.max(1, Math.abs(sumCounted(relationship.contributors)))) {
    return null;
  }
  return relationship;
}

function finish(
    measuredGap: number, axis: SpacingAxis, relationship: SpacingRelationshipKind, neighbor: SpacingNeighbor,
    contributors: SpacingContributor[], marginCollapseUnknown: boolean): SpacingRelationship {
  let kept = contributors;
  if (overshoots(sumCounted(kept), measuredGap)) {
    kept = kept.filter(contributor => contributor.type !== SpacingContributorType.MARGIN);
  }
  if (overshoots(sumCounted(kept), measuredGap)) {
    kept = kept.filter(contributor => contributor.type !== SpacingContributorType.GAP);
  }
  if (overshoots(sumCounted(kept), measuredGap)) {
    kept = kept.filter(contributor => contributor.type !== SpacingContributorType.PADDING);
  }
  const unexplained = measuredGap - sumCounted(kept);
  return {
    measuredGap,
    axis,
    relationship,
    neighbor,
    contributors: kept,
    unexplained,
    marginCollapseUnknown,
  };
}

type NeighborSearch = {kind: 'found', box: SpacingBox}|{kind: 'none'}|{kind: 'ambiguous'}|{kind: 'interfering-transform'};

function nearestNeighbor(
    selected: SpacingBox, selectedEdges: Edges, siblings: SpacingBox[], axis: SpacingAxis,
    side: 'previous'|'next'): NeighborSearch {
  if (siblings.some(sibling => siblingInterferes(sibling, selected, selectedEdges, axis))) {
    return {kind: 'interfering-transform'};
  }
  const matches: SpacingBox[] = [];
  for (const sibling of siblings) {
    if (!isCandidate(sibling, selected)) {
      continue;
    }
    if (!aligned(selectedEdges, edges(sibling), axis)) {
      continue;
    }
    if (!onSide(selectedEdges, edges(sibling), axis, side)) {
      continue;
    }
    matches.push(sibling);
  }
  if (matches.length === 0) {
    return {kind: 'none'};
  }
  matches.sort((left, right) => closeness(selectedEdges, edges(right), axis, side) -
              closeness(selectedEdges, edges(left), axis, side));
  const closest = matches[0];
  const next = matches[1];
  if (next && Math.abs(closeness(selectedEdges, edges(closest), axis, side) -
                       closeness(selectedEdges, edges(next), axis, side)) <= 1) {
    return {kind: 'ambiguous'};
  }
  return {kind: 'found', box: closest};
}

function siblingInterferes(sibling: SpacingBox, selected: SpacingBox, selectedEdges: Edges, axis: SpacingAxis): boolean {
  if (sibling.nodeId === selected.nodeId || sibling.transformNone || isOutOfFlow(sibling.position)) {
    return false;
  }
  return aligned(selectedEdges, edges(sibling), axis);
}

function outOfFlowBetween(
    selectedEdges: Edges, neighborEdges: Edges, siblings: SpacingBox[], axis: SpacingAxis,
    kind: SpacingRelationshipKind): boolean {
  const interval = facingInterval(selectedEdges, neighborEdges, axis, kind);
  if (interval.end - interval.start <= 1) {
    return false;
  }
  return siblings.some(sibling => {
    if (!isOutOfFlow(sibling.position)) {
      return false;
    }
    const box = edges(sibling);
    if (!aligned(selectedEdges, box, axis)) {
      return false;
    }
    return overlapsInterval(box, interval, axis);
  });
}

function facingInterval(
    selectedEdges: Edges, neighborEdges: Edges, axis: SpacingAxis, kind: SpacingRelationshipKind): {start: number, end: number} {
  const previous = kind === SpacingRelationshipKind.PREVIOUS_SIBLING;
  const first = axis === SpacingAxis.VERTICAL ? (previous ? neighborEdges.bottom : selectedEdges.bottom) :
                                                (previous ? neighborEdges.right : selectedEdges.right);
  const second = axis === SpacingAxis.VERTICAL ? (previous ? selectedEdges.top : neighborEdges.top) :
                                                 (previous ? selectedEdges.left : neighborEdges.left);
  return {start: Math.min(first, second), end: Math.max(first, second)};
}

function overlapsInterval(box: Edges, interval: {start: number, end: number}, axis: SpacingAxis): boolean {
  const start = axis === SpacingAxis.VERTICAL ? box.top : box.left;
  const end = axis === SpacingAxis.VERTICAL ? box.bottom : box.right;
  return end > interval.start + 1 && start < interval.end - 1;
}

function isCandidate(sibling: SpacingBox, selected: SpacingBox): boolean {
  if (sibling.nodeId === selected.nodeId || !sibling.transformNone || isOutOfFlow(sibling.position)) {
    return false;
  }
  if (sibling.position === null) {
    return false;
  }
  const outer = edges(sibling);
  const inner = edges(selected);
  if (contains(outer, inner) || contains(inner, outer)) {
    return false;
  }
  return true;
}

function onSide(selectedEdges: Edges, siblingEdges: Edges, axis: SpacingAxis, side: 'previous'|'next'): boolean {
  if (axis === SpacingAxis.VERTICAL) {
    const selectedMid = (selectedEdges.top + selectedEdges.bottom) / 2;
    const siblingMid = (siblingEdges.top + siblingEdges.bottom) / 2;
    return side === 'previous' ? siblingMid < selectedMid : siblingMid > selectedMid;
  }
  const selectedMid = (selectedEdges.left + selectedEdges.right) / 2;
  const siblingMid = (siblingEdges.left + siblingEdges.right) / 2;
  return side === 'previous' ? siblingMid < selectedMid : siblingMid > selectedMid;
}

function closeness(selectedEdges: Edges, siblingEdges: Edges, axis: SpacingAxis, side: 'previous'|'next'): number {
  if (axis === SpacingAxis.VERTICAL) {
    return side === 'previous' ? siblingEdges.bottom : -siblingEdges.top;
  }
  return side === 'previous' ? siblingEdges.right : -siblingEdges.left;
}

function aligned(selectedEdges: Edges, siblingEdges: Edges, axis: SpacingAxis): boolean {
  if (axis === SpacingAxis.VERTICAL) {
    return meaningfullyOverlaps(
        overlap(selectedEdges.left, selectedEdges.right, siblingEdges.left, siblingEdges.right),
        selectedEdges.right - selectedEdges.left, siblingEdges.right - siblingEdges.left);
  }
  return meaningfullyOverlaps(
      overlap(selectedEdges.top, selectedEdges.bottom, siblingEdges.top, siblingEdges.bottom),
      selectedEdges.bottom - selectedEdges.top, siblingEdges.bottom - siblingEdges.top);
}

function meaningfullyOverlaps(overlapPx: number, selectedSize: number, siblingSize: number): boolean {
  if (selectedSize <= 0 || siblingSize <= 0) {
    return false;
  }
  const smaller = Math.min(selectedSize, siblingSize);
  return overlapPx >= Math.min(smaller, Math.max(1, smaller * 0.5));
}

function overlap(a0: number, a1: number, b0: number, b1: number): number {
  return Math.min(a1, b1) - Math.max(a0, b0);
}

function contains(outer: Edges, inner: Edges): boolean {
  return outer.left <= inner.left + 1 && outer.right >= inner.right - 1 && outer.top <= inner.top + 1 &&
      outer.bottom >= inner.bottom - 1;
}

function gapBetween(selected: SpacingBox, neighbor: SpacingBox, axis: SpacingAxis, kind: SpacingRelationshipKind): number {
  const selectedEdges = edges(selected);
  const neighborEdges = edges(neighbor);
  if (axis === SpacingAxis.VERTICAL) {
    return kind === SpacingRelationshipKind.PREVIOUS_SIBLING ? Math.round(selectedEdges.top - neighborEdges.bottom) :
                                                               Math.round(neighborEdges.top - selectedEdges.bottom);
  }
  return kind === SpacingRelationshipKind.PREVIOUS_SIBLING ? Math.round(selectedEdges.left - neighborEdges.right) :
                                                             Math.round(neighborEdges.left - selectedEdges.right);
}

function parentGap(selectedEdges: Edges, parent: SpacingParent, axis: SpacingAxis, kind: SpacingRelationshipKind): number {
  if (axis === SpacingAxis.VERTICAL) {
    return kind === SpacingRelationshipKind.PARENT_START ?
        Math.round(selectedEdges.top - (parent.y + parent.borderTop)) :
        Math.round((parent.y + parent.height - parent.borderBottom) - selectedEdges.bottom);
  }
  return kind === SpacingRelationshipKind.PARENT_START ?
      Math.round(selectedEdges.left - (parent.x + parent.borderLeft)) :
      Math.round((parent.x + parent.width - parent.borderRight) - selectedEdges.right);
}

function gapContributor(
    parent: SpacingParent|null, axis: SpacingAxis, kind: SpacingRelationshipKind): SpacingContributor|null {
  if (!parent || kind === SpacingRelationshipKind.PARENT_START || kind === SpacingRelationshipKind.PARENT_END) {
    return null;
  }
  if (!gapApplies(parent, axis)) {
    return null;
  }
  const horizontal = axis === SpacingAxis.HORIZONTAL;
  const value = horizontal ? parent.columnGap : parent.rowGap;
  const declaration = horizontal ? parent.columnGapDeclaration : parent.rowGapDeclaration;
  const property = horizontal ? 'column-gap' : 'row-gap';
  if (value === null || value === 0) {
    return null;
  }
  return {
    type: SpacingContributorType.GAP,
    value,
    nodeId: parent.nodeId,
    label: parent.label,
    property,
    declaredValue: declaration?.value ?? null,
    declarationId: declaration?.id ?? null,
    hasSource: declaration?.hasSource ?? false,
    confidence: Confidence.HIGH,
    countsTowardGap: true,
  };
}

function gapApplies(parent: SpacingParent, axis: SpacingAxis): boolean {
  if (parent.writingMode && parent.writingMode !== 'horizontal-tb') {
    return false;
  }
  const display = parent.display ?? '';
  if (display === 'grid' || display === 'inline-grid') {
    return true;
  }
  if (display !== 'flex' && display !== 'inline-flex') {
    return false;
  }
  if (parent.flexDirection !== 'row' && parent.flexDirection !== 'row-reverse' && parent.flexDirection !== 'column' &&
      parent.flexDirection !== 'column-reverse') {
    return false;
  }
  const mainHorizontal = parent.flexDirection === 'row' || parent.flexDirection === 'row-reverse';
  const relationshipHorizontal = axis === SpacingAxis.HORIZONTAL;
  if (mainHorizontal === relationshipHorizontal) {
    return true;
  }
  return parent.flexWrap === 'wrap' || parent.flexWrap === 'wrap-reverse';
}

type MarginMode = 'count'|'collapse'|'omit';

function marginModeFor(parent: SpacingParent|null, axis: SpacingAxis, selected: SpacingBox, neighbor: SpacingBox): MarginMode {
  const display = parent?.display ?? '';
  if (isFlexOrGrid(display)) {
    return 'count';
  }
  if (axis === SpacingAxis.VERTICAL) {
    return 'collapse';
  }
  if (isInlineBlockish(selected.display) && isInlineBlockish(neighbor.display)) {
    return 'count';
  }
  return 'omit';
}

function parentMarginMode(parent: SpacingParent, axis: SpacingAxis, separated: boolean): MarginMode {
  if (isFlexOrGrid(parent.display ?? '')) {
    return 'count';
  }
  if (axis === SpacingAxis.HORIZONTAL) {
    return 'count';
  }
  return separated ? 'count' : 'collapse';
}

function facingMargins(
    selected: SpacingBox, neighbor: SpacingBox, axis: SpacingAxis, kind: SpacingRelationshipKind): SpacingContributor[] {
  const selectedSide = selectedFacing(axis, kind);
  const neighborSide = neighborFacing(axis, kind);
  return [marginContributor(selected, selectedSide), marginContributor(neighbor, neighborSide)].filter(
      (contributor): contributor is SpacingContributor => contributor !== null);
}

function edgeMargin(selected: SpacingBox, axis: SpacingAxis, kind: SpacingRelationshipKind): SpacingContributor|null {
  return marginContributor(selected, selectedFacing(axis, kind));
}

function selectedFacing(axis: SpacingAxis, kind: SpacingRelationshipKind): 'top'|'right'|'bottom'|'left' {
  if (axis === SpacingAxis.VERTICAL) {
    return kind === SpacingRelationshipKind.PREVIOUS_SIBLING || kind === SpacingRelationshipKind.PARENT_START ? 'top' :
                                                                                                               'bottom';
  }
  return kind === SpacingRelationshipKind.PREVIOUS_SIBLING || kind === SpacingRelationshipKind.PARENT_START ? 'left' :
                                                                                                             'right';
}

function neighborFacing(axis: SpacingAxis, kind: SpacingRelationshipKind): 'top'|'right'|'bottom'|'left' {
  if (axis === SpacingAxis.VERTICAL) {
    return kind === SpacingRelationshipKind.PREVIOUS_SIBLING ? 'bottom' : 'top';
  }
  return kind === SpacingRelationshipKind.PREVIOUS_SIBLING ? 'right' : 'left';
}

function marginContributor(box: SpacingBox, side: 'top'|'right'|'bottom'|'left'): SpacingContributor|null {
  const value = side === 'top' ? box.marginTop :
      side === 'right'         ? box.marginRight :
      side === 'bottom'        ? box.marginBottom :
                                 box.marginLeft;
  const declaration = side === 'top' ? box.marginTopDeclaration :
      side === 'right'               ? box.marginRightDeclaration :
      side === 'bottom'              ? box.marginBottomDeclaration :
                                       box.marginLeftDeclaration;
  if (value === null || value === 0) {
    return null;
  }
  const property = `margin-${side}`;
  return {
    type: SpacingContributorType.MARGIN,
    value,
    nodeId: box.nodeId,
    label: box.label,
    property,
    declaredValue: declaration?.value ?? null,
    declarationId: declaration?.id ?? null,
    hasSource: declaration?.hasSource ?? false,
    confidence: Confidence.HIGH,
    countsTowardGap: true,
  };
}

function paddingContributor(
    parent: SpacingParent, axis: SpacingAxis, kind: SpacingRelationshipKind): SpacingContributor|null {
  const start = kind === SpacingRelationshipKind.PARENT_START;
  const value = axis === SpacingAxis.VERTICAL ? (start ? parent.paddingTop : parent.paddingBottom) :
                                                (start ? parent.paddingLeft : parent.paddingRight);
  const declaration = axis === SpacingAxis.VERTICAL ?
      (start ? parent.paddingTopDeclaration : parent.paddingBottomDeclaration) :
      (start ? parent.paddingLeftDeclaration : parent.paddingRightDeclaration);
  const property = axis === SpacingAxis.VERTICAL ? (start ? 'padding-top' : 'padding-bottom') :
                                                   (start ? 'padding-left' : 'padding-right');
  if (value === 0) {
    return null;
  }
  return {
    type: SpacingContributorType.PADDING,
    value,
    nodeId: parent.nodeId,
    label: parent.label,
    property,
    declaredValue: declaration?.value ?? null,
    declarationId: declaration?.id ?? null,
    hasSource: declaration?.hasSource ?? false,
    confidence: Confidence.HIGH,
    countsTowardGap: true,
  };
}

function facingMarginParticipates(
    selected: SpacingBox, neighbor: SpacingBox, axis: SpacingAxis, kind: SpacingRelationshipKind): boolean {
  return marginParticipates(marginContributor(selected, selectedFacing(axis, kind)), selected, axis, kind) ||
      marginParticipates(marginContributor(neighbor, neighborFacing(axis, kind)), neighbor, axis, kind);
}

function marginParticipates(
    contributor: SpacingContributor|null, _box: SpacingBox, _axis: SpacingAxis, _kind: SpacingRelationshipKind): boolean {
  return contributor !== null && contributor.value !== 0;
}

function separatingBorder(parent: SpacingParent, axis: SpacingAxis, kind: SpacingRelationshipKind): boolean {
  const start = kind === SpacingRelationshipKind.PARENT_START;
  const border = axis === SpacingAxis.VERTICAL ? (start ? parent.borderTop : parent.borderBottom) :
                                                 (start ? parent.borderLeft : parent.borderRight);
  return border > 0;
}

function sumCounted(contributors: SpacingContributor[]): number {
  return contributors.reduce((sum, contributor) => contributor.countsTowardGap ? sum + contributor.value : sum, 0);
}

function overshoots(sum: number, measured: number): boolean {
  if (measured >= 0) {
    return sum > measured + 1;
  }
  return sum < measured - 1;
}

function edges(box: SpacingBox): Edges {
  return {left: box.x, right: box.x + box.width, top: box.y, bottom: box.y + box.height};
}

function isOutOfFlow(position: string|null): boolean {
  return position === 'absolute' || position === 'fixed';
}

function isFlexOrGrid(display: string): boolean {
  return display === 'flex' || display === 'inline-flex' || display === 'grid' || display === 'inline-grid';
}

function isInlineBlockish(display: string|null): boolean {
  return display === 'inline-block' || display === 'inline-flex' || display === 'inline-grid';
}
