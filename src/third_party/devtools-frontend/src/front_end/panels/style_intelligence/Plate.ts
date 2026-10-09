// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * The chain, drawn: nested frames outermost first, with the selected element
 * inside them.
 *
 * The drawing is schematic and says so. Nesting order and the roles are real;
 * frame sizes are not, and the element box is drawn at a fixed height. That is
 * deliberate rather than a shortcut: a to-scale plate cannot stay honest for a
 * 4000px-tall section, a zero-height box, or an element wider than its
 * ancestor, and a drawing that silently clamps those is worse than one that
 * never claimed to measure. Every number on the plate is a dimension label
 * carrying a measured value, which is where the measuring happens.
 *
 * Only ancestors that hold a role are drawn, so depth is bounded by the number
 * of roles rather than by DOM depth. Past MAX_FRAMES the outermost are elided
 * and counted, because the nearest ancestors are the ones whose effect the
 * element is most likely to be fighting.
 */

import {ChainRoleKind, type ChainRole, type ChainRung} from './ElementChain.js';
import {type AxisExplanation, Mechanism} from './LayoutExplanation.js';
import {SpacingContributorType, SpacingRelationshipKind, type SpacingRelationship} from './SpacingExplanation.js';

const MAX_FRAMES = 3;

export interface PlateFrame {
  nodeId: number|null;
  label: string;
  /** Short role word shown on the frame's top edge. */
  role: string;
  warn: boolean;
}

export interface PlateGap {
  value: number;
  /** Authored value when it differs from the used pixels, usually a token. */
  detail: string|null;
  neighborNodeId: number|null;
  neighborLabel: string|null;
  /** The neighbour is a sibling rather than the parent's content edge. */
  sibling: boolean;
}

export interface PlateView {
  frames: PlateFrame[];
  /** Ancestors with a role that did not fit. */
  elidedFrames: number;
  /** Border-box label for the element, or null when geometry is unavailable. */
  sizeLabel: string|null;
  /** Dimension label under the element box. */
  widthLabel: string|null;
  paddingLabel: string|null;
  gapAbove: PlateGap|null;
}

export interface PlateInput {
  selectedNodeId: number|null;
  rungs: ChainRung[];
  geometry: {width: number, height: number}|null;
  axes: AxisExplanation[];
  spacing: SpacingRelationship[];
}

export function buildPlate(input: PlateInput): PlateView|null {
  const all = input.rungs.filter(isContainerRung).map(toFrame);
  const frames = all.slice(Math.max(0, all.length - MAX_FRAMES));
  const gapAbove = findGapAbove(input.spacing, input.selectedNodeId);
  const sizeLabel = input.geometry ? `${input.geometry.width} × ${input.geometry.height}` : null;
  if (!frames.length && !gapAbove && !sizeLabel) {
    return null;
  }
  return {
    frames,
    elidedFrames: all.length - frames.length,
    sizeLabel,
    widthLabel: widthLabel(input.axes),
    paddingLabel: paddingLabel(input.spacing, input.selectedNodeId),
    gapAbove,
  };
}

/**
 * A sibling is drawn as the bar above the element, not as a frame around it.
 * Everything else that holds a role contains the element.
 */
function isContainerRung(rung: ChainRung): boolean {
  return rung.roles.some(role => role.kind !== ChainRoleKind.SIBLING);
}

function toFrame(rung: ChainRung): PlateFrame {
  const roles = rung.roles.filter(role => role.kind !== ChainRoleKind.SIBLING);
  // A warning is the reason the reader is looking at the plate, so it names the
  // frame even when an outer role sorted the rung.
  const chosen = roles.find(role => role.warn) ?? roles[0];
  return {
    nodeId: rung.nodeId,
    label: rung.label,
    role: chosen ? frameRoleWord(chosen) : 'ancestor',
    warn: rung.warn,
  };
}

function frameRoleWord(role: ChainRole): string {
  switch (role.kind) {
    case ChainRoleKind.SCROLL_CONTAINER:
    case ChainRoleKind.STICKY_CONTAINER:
      return 'scrolls';
    case ChainRoleKind.CONTAINING_BLOCK:
      return 'containing block';
    case ChainRoleKind.STACKING_CONTEXT:
      return 'stacking context';
    case ChainRoleKind.LAYOUT_PARENT:
      return 'layout parent';
    case ChainRoleKind.SIBLING:
      return 'sibling';
  }
}

/**
 * The gap drawn above the element. A previous sibling is preferred over the
 * parent's start edge, because that is the gap an author is usually chasing.
 */
function findGapAbove(relationships: SpacingRelationship[], selectedNodeId: number|null): PlateGap|null {
  const vertical = relationships.filter(relationship => relationship.axis === 'vertical');
  const previous = vertical.find(
      relationship => relationship.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING && relationship.neighbor);
  const parentStart =
      vertical.find(relationship => relationship.relationship === SpacingRelationshipKind.PARENT_START);
  const chosen = previous ?? parentStart;
  if (!chosen) {
    return null;
  }
  const sibling = chosen.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING;
  const neighbor = chosen.neighbor;
  return {
    value: chosen.measuredGap,
    detail: authoredDetail(chosen, selectedNodeId),
    neighborNodeId: neighbor && neighbor.nodeId > 0 ? neighbor.nodeId : null,
    neighborLabel: neighbor?.label ?? null,
    sibling,
  };
}

function authoredDetail(relationship: SpacingRelationship, selectedNodeId: number|null): string|null {
  for (const contributor of relationship.contributors) {
    if (contributor.declaredValue && contributor.declaredValue !== `${contributor.value}px`) {
      return contributor.declaredValue;
    }
  }
  // Nothing authored differs from the used pixels, so there is no token to name.
  // Fall back to naming whose box the gap came out of, which the number alone
  // does not say.
  const own = relationship.contributors.find(
      contributor => selectedNodeId !== null && contributor.nodeId === selectedNodeId);
  return own ? own.property : null;
}

/**
 * A dimension label names how the width was arrived at, not which property the
 * controlling node happened to declare. Reading the controller's property name
 * produced labels like "1069 · display", which says nothing about the width.
 */
function widthLabel(axes: AxisExplanation[]): string|null {
  const width = axes.find(axis => axis.axis === 'width');
  if (!width) {
    return null;
  }
  const word = widthMechanismWord(width.mechanism) ?? width.controller?.property ?? null;
  return word ? `${width.usedValue} · ${word}` : `${width.usedValue}`;
}

function widthMechanismWord(mechanism: Mechanism): string|null {
  switch (mechanism) {
    case Mechanism.FLEX_STRETCH:
    case Mechanism.GRID_STRETCH:
      return 'stretched';
    case Mechanism.FLEX_LAYOUT:
      return 'flex';
    case Mechanism.GRID_LAYOUT:
      return 'grid';
    case Mechanism.CONTENT:
      return 'content';
    case Mechanism.INTRINSIC:
      return 'intrinsic';
    case Mechanism.EXPLICIT:
      return 'specified';
    case Mechanism.PERCENTAGE:
      return 'percentage';
    case Mechanism.VIEWPORT:
      return 'viewport';
    case Mechanism.MIN_SIZE:
      return 'min size';
    case Mechanism.MAX_SIZE:
      return 'max size';
    case Mechanism.BLOCK_AVAILABLE:
      return 'available';
    case Mechanism.INSET_DERIVED:
      return 'insets';
    case Mechanism.AUTO_MARGINS:
      return 'auto margins';
    default:
      // Positioning mechanisms do not describe a width; fall back to the
      // controller so the label still says something true.
      return null;
  }
}

function paddingLabel(relationships: SpacingRelationship[], selectedNodeId: number|null): string|null {
  if (selectedNodeId === null) {
    return null;
  }
  const values = new Set<number>();
  for (const relationship of relationships) {
    for (const contributor of relationship.contributors) {
      if (contributor.type === SpacingContributorType.PADDING && contributor.nodeId === selectedNodeId) {
        values.add(contributor.value);
      }
    }
  }
  if (!values.size) {
    return null;
  }
  const sorted = [...values].sort((left, right) => left - right);
  return sorted.length === 1 ? `padding ${sorted[0]}` : `padding ${sorted[0]}–${sorted[sorted.length - 1]}`;
}
