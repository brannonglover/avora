// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * One rung per ancestor that decides something about the selected element.
 *
 * The pane used to split this chain across three places: containing block and
 * scroll container under Position, and the stacking context inside a collapsed
 * Layering disclosure. A node holding more than one of those roles appeared
 * once per role. Here a node appears once and carries every role it holds.
 *
 * Roles are only reported from data the loaders already produced. This module
 * does not walk the DOM and does not read computed style. It never promotes
 * the selected element to a rung: whatever the element decides for itself
 * belongs to ElementFacts, which is the other half of the split.
 *
 * Rung order is layout containment, outermost first. It is not DOM depth: the
 * scroll container is reported before the containing block because an element
 * resolves against the containing block inside the scrollport, and no walk
 * here can prove the DOM distance between the two.
 */

import {type AxisExplanation} from './LayoutExplanation.js';
import {type LayoutRelationship, type LayoutRelationships, RelationshipKind} from './LayoutRelationships.js';
import {type StickyInsets} from './LayoutRelationshipsPresentation.js';
import {SpacingRelationshipKind, type SpacingRelationship} from './SpacingExplanation.js';

export const ChainRoleKind = {
  SCROLL_CONTAINER: 'scroll-container',
  STICKY_CONTAINER: 'sticky-container',
  CONTAINING_BLOCK: 'containing-block',
  STACKING_CONTEXT: 'stacking-context',
  LAYOUT_PARENT: 'layout-parent',
  SIBLING: 'sibling',
} as const;

export type ChainRoleKind = (typeof ChainRoleKind)[keyof typeof ChainRoleKind];

/**
 * Containment depth, outermost first. Two roles sharing a depth keep the order
 * they were collected in, so a merged rung never reorders its own roles.
 */
const ROLE_DEPTH: Readonly<Record<ChainRoleKind, number>> = {
  [ChainRoleKind.SCROLL_CONTAINER]: 0,
  [ChainRoleKind.STICKY_CONTAINER]: 0,
  [ChainRoleKind.CONTAINING_BLOCK]: 1,
  [ChainRoleKind.STACKING_CONTEXT]: 2,
  [ChainRoleKind.LAYOUT_PARENT]: 3,
  [ChainRoleKind.SIBLING]: 4,
};

export interface ChainRole {
  kind: ChainRoleKind;
  /** What this role does to the selected element. One clause. */
  effect: string;
  /** The declaration that creates the role, when one is known. */
  declaration: string|null;
  declarationId: number|null;
  hasSource: boolean;
  /** Measured consequence for the selected element. */
  measurement: string|null;
  warn: boolean;
}

export interface ChainRung {
  /** Null for the document, the viewport, or an anonymous container. */
  nodeId: number|null;
  label: string;
  roles: ChainRole[];
  /** True when any role on this rung is a warning. */
  warn: boolean;
}

/**
 * The stacking context the selected element belongs to, reduced to what a rung
 * needs. `trapped` is only true once a snapshot has confirmed the element
 * cannot escape by raising its own z-index; the CSS predicate alone states
 * that a context exists, which is a weaker claim.
 */
export interface ChainStacking {
  nodeId: number|null;
  label: string;
  declaration: string|null;
  declarationId: number|null;
  hasSource: boolean;
  trapped: boolean;
  /** The selected element's z-index, when that is the value being trapped. */
  trappedZIndex: string|null;
}

export interface ChainInput {
  selectedNodeId: number|null;
  relationships: LayoutRelationships|null;
  labels: ReadonlyMap<number, string>;
  insets: StickyInsets;
  axes: AxisExplanation[];
  spacing: SpacingRelationship[];
  stacking: ChainStacking|null;
}

interface Entry {
  nodeId: number|null;
  label: string;
  role: ChainRole;
}

export function buildElementChain(input: ChainInput): ChainRung[] {
  const entries: Entry[] = [
    ...stickyEntries(input),
    ...scrollEntries(input),
    ...containingBlockEntries(input),
    ...stackingEntries(input),
    ...layoutParentEntries(input),
    ...siblingEntries(input),
  ];
  return mergeByNode(entries, input.selectedNodeId);
}

export function chainRoleLabel(kind: ChainRoleKind): string {
  switch (kind) {
    case ChainRoleKind.SCROLL_CONTAINER:
      return 'Scroll container';
    case ChainRoleKind.STICKY_CONTAINER:
      return 'Sticky container';
    case ChainRoleKind.CONTAINING_BLOCK:
      return 'Containing block';
    case ChainRoleKind.STACKING_CONTEXT:
      return 'Stacking context';
    case ChainRoleKind.LAYOUT_PARENT:
      return 'Layout parent';
    case ChainRoleKind.SIBLING:
      return 'Sibling';
  }
}

/** Joined role label for a merged rung, longest-lived role first. */
export function chainRungLabel(rung: ChainRung): string {
  const seen: string[] = [];
  for (const role of rung.roles) {
    const label = chainRoleLabel(role.kind);
    if (!seen.includes(label)) {
      seen.push(label);
    }
  }
  return seen.join(' + ');
}

function mergeByNode(entries: Entry[], selectedNodeId: number|null): ChainRung[] {
  const order: string[] = [];
  const byKey = new Map<string, ChainRung>();
  for (const entry of entries) {
    // The selected element is never its own ancestor. A fixture that reports it
    // as one must not produce a rung pointing back at the element.
    if (entry.nodeId !== null && entry.nodeId === selectedNodeId) {
      continue;
    }
    const key = entry.nodeId !== null ? `node:${entry.nodeId}` : `label:${entry.label}`;
    const existing = byKey.get(key);
    if (!existing) {
      order.push(key);
      byKey.set(key, {nodeId: entry.nodeId, label: entry.label, roles: [entry.role], warn: entry.role.warn});
      continue;
    }
    if (!existing.roles.some(role => sameRole(role, entry.role))) {
      existing.roles.push(entry.role);
    }
    existing.warn = existing.warn || entry.role.warn;
  }
  const rungs = order.map(key => byKey.get(key)).filter((rung): rung is ChainRung => Boolean(rung));
  return rungs
      .map((rung, index) => ({rung, index, depth: rungDepth(rung)}))
      .sort((left, right) => left.depth - right.depth || left.index - right.index)
      .map(item => item.rung);
}

function sameRole(left: ChainRole, right: ChainRole): boolean {
  return left.kind === right.kind && left.effect === right.effect && left.measurement === right.measurement;
}

function rungDepth(rung: ChainRung): number {
  return rung.roles.reduce((depth, role) => Math.min(depth, ROLE_DEPTH[role.kind]), ROLE_DEPTH[ChainRoleKind.SIBLING]);
}

function stickyEntries(input: ChainInput): Entry[] {
  const sticky = input.relationships?.stickyScrollContainers;
  if (!sticky) {
    return [];
  }
  const {horizontal, vertical} = sticky;
  if (horizontal && vertical && sameRelationship(horizontal, vertical)) {
    return [stickyEntry(horizontal, allInsetText(input.insets), input)];
  }
  const entries: Entry[] = [];
  if (vertical) {
    entries.push(stickyEntry(vertical, insetText([['top', input.insets.top], ['bottom', input.insets.bottom]]), input));
  }
  if (horizontal) {
    entries.push(stickyEntry(horizontal, insetText([['left', input.insets.left], ['right', input.insets.right]]), input));
  }
  return entries;
}

function stickyEntry(relationship: LayoutRelationship, measurement: string|null, input: ChainInput): Entry {
  return {
    nodeId: elementNodeId(relationship),
    label: relationshipLabel(relationship, input.labels),
    role: {
      kind: ChainRoleKind.STICKY_CONTAINER,
      effect: 'you stick inside it while it scrolls',
      declaration: null,
      declarationId: null,
      hasSource: false,
      measurement,
      warn: false,
    },
  };
}

function scrollEntries(input: ChainInput): Entry[] {
  const scroll = input.relationships?.scrollContainer;
  if (!scroll) {
    return [];
  }
  const sticky = input.relationships?.stickyScrollContainers;
  // A sticky axis already named this container. Reporting it again as a plain
  // scroll container would restate the same relationship with weaker copy.
  if (sticky &&
      ((sticky.horizontal && sameRelationship(sticky.horizontal, scroll)) ||
       (sticky.vertical && sameRelationship(sticky.vertical, scroll)))) {
    return [];
  }
  return [{
    nodeId: elementNodeId(scroll),
    label: relationshipLabel(scroll, input.labels),
    role: {
      kind: ChainRoleKind.SCROLL_CONTAINER,
      effect: scroll.fixedToViewport ? 'you are fixed to the viewport' : 'it scrolls you',
      declaration: null,
      declarationId: null,
      hasSource: false,
      measurement: null,
      warn: false,
    },
  }];
}

function containingBlockEntries(input: ChainInput): Entry[] {
  const containingBlock = input.relationships?.containingBlock;
  if (!containingBlock) {
    return [];
  }
  return [{
    nodeId: elementNodeId(containingBlock),
    label: relationshipLabel(containingBlock, input.labels),
    role: {
      kind: ChainRoleKind.CONTAINING_BLOCK,
      effect: 'your position resolves against it',
      declaration: null,
      declarationId: null,
      hasSource: false,
      measurement: null,
      warn: false,
    },
  }];
}

function stackingEntries(input: ChainInput): Entry[] {
  const stacking = input.stacking;
  if (!stacking) {
    return [];
  }
  const zIndex = stacking.trappedZIndex;
  const effect = stacking.trapped ?
      (zIndex ? `painted as one unit — traps your z-index: ${zIndex}` : 'painted as one unit — traps your z-index') :
      'it is painted as one unit';
  return [{
    nodeId: stacking.nodeId,
    label: stacking.label,
    role: {
      kind: ChainRoleKind.STACKING_CONTEXT,
      effect,
      declaration: stacking.declaration,
      declarationId: stacking.hasSource ? stacking.declarationId : null,
      hasSource: stacking.hasSource,
      measurement: null,
      warn: stacking.trapped,
    },
  }];
}

function layoutParentEntries(input: ChainInput): Entry[] {
  const entries: Entry[] = [];
  for (const axis of input.axes) {
    const controller = axis.controller;
    if (!controller || controller.nodeId <= 0 || controller.nodeId === input.selectedNodeId) {
      continue;
    }
    const declaration =
        controller.property && controller.value ? `${controller.property}: ${controller.value}` : null;
    entries.push({
      nodeId: controller.nodeId,
      label: controller.label || (input.labels.get(controller.nodeId) ?? 'element'),
      role: {
        kind: ChainRoleKind.LAYOUT_PARENT,
        effect: `it sets your ${axisNoun(axis.axis)}`,
        declaration,
        declarationId: controller.hasSource ? controller.declarationId : null,
        hasSource: controller.hasSource,
        measurement: axisMeasurement(axis),
        warn: false,
      },
    });
  }
  return entries;
}

function siblingEntries(input: ChainInput): Entry[] {
  const entries: Entry[] = [];
  for (const relationship of input.spacing) {
    if (relationship.relationship !== SpacingRelationshipKind.PREVIOUS_SIBLING &&
        relationship.relationship !== SpacingRelationshipKind.NEXT_SIBLING) {
      continue;
    }
    const neighbor = relationship.neighbor;
    if (!neighbor || neighbor.nodeId <= 0) {
      continue;
    }
    const before = relationship.relationship === SpacingRelationshipKind.PREVIOUS_SIBLING;
    const side = relationship.axis === 'vertical' ? (before ? 'above you' : 'below you') :
                                                    (before ? 'before you' : 'after you');
    entries.push({
      nodeId: neighbor.nodeId,
      label: neighbor.label,
      role: {
        kind: ChainRoleKind.SIBLING,
        effect: `it sits ${side}`,
        declaration: gapDeclaration(relationship),
        declarationId: null,
        hasSource: false,
        measurement: `${relationship.measuredGap}px`,
        warn: false,
      },
    });
  }
  return entries;
}

/**
 * The authored value when it differs from the used pixels — usually a token.
 * The used pixels are already the rung's measurement, so repeating them here
 * would say the same thing twice.
 */
function gapDeclaration(relationship: SpacingRelationship): string|null {
  for (const contributor of relationship.contributors) {
    if (contributor.declaredValue && contributor.declaredValue !== `${contributor.value}px`) {
      return `${contributor.property}: ${contributor.declaredValue}`;
    }
  }
  const first = relationship.contributors[0];
  return first ? first.property : null;
}

function axisNoun(axis: AxisExplanation['axis']): string {
  switch (axis) {
    case 'width':
      return 'width';
    case 'height':
      return 'height';
    case 'x':
      return 'horizontal position';
    case 'y':
      return 'vertical position';
  }
}

function axisMeasurement(axis: AxisExplanation): string|null {
  if (axis.axis === 'width' || axis.axis === 'height') {
    return `${axis.usedValue}px`;
  }
  return null;
}

function elementNodeId(relationship: LayoutRelationship): number|null {
  return relationship.kind === RelationshipKind.ELEMENT ? relationship.nodeId : null;
}

function relationshipLabel(relationship: LayoutRelationship, labels: ReadonlyMap<number, string>): string {
  if (relationship.kind === RelationshipKind.ELEMENT && relationship.nodeId !== null) {
    return labels.get(relationship.nodeId) ?? 'element';
  }
  switch (relationship.kind) {
    case RelationshipKind.INITIAL_CONTAINING_BLOCK:
      return 'viewport';
    case RelationshipKind.ANONYMOUS:
      return 'anonymous container';
    case RelationshipKind.DOCUMENT:
      return relationship.fixedToViewport ? 'viewport' : 'document';
    default:
      return 'element';
  }
}

function sameRelationship(left: LayoutRelationship, right: LayoutRelationship): boolean {
  if (left.kind !== right.kind) {
    return false;
  }
  if (left.kind === RelationshipKind.ELEMENT) {
    return left.nodeId !== null && left.nodeId === right.nodeId;
  }
  return true;
}

function allInsetText(insets: StickyInsets): string|null {
  return insetText([
    ['top', insets.top],
    ['right', insets.right],
    ['bottom', insets.bottom],
    ['left', insets.left],
  ]);
}

function insetText(pairs: Array<[string, string|null]>): string|null {
  const lines = pairs.filter((pair): pair is [string, string] => Boolean(pair[1] && pair[1] !== 'auto'))
                    .map(([property, value]) => `${property}: ${value}`);
  return lines.length ? lines.join(' · ') : null;
}
