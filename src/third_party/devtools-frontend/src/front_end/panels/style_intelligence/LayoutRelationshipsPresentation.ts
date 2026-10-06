// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Position and sticky copy for Blink layout relationships.
 * Identical sticky axes share one element row. An unconstrained axis is omitted.
 * scrollContainer is not substituted for stickyScrollContainers.
 */

import {type LayoutExplanation} from './LayoutExplanation.js';
import {
  RelationshipKind,
  type LayoutRelationship,
  type LayoutRelationships,
} from './LayoutRelationships.js';
import {type PaneControl, type PaneRow, type PaneSection} from './Presentation.js';

const UNKNOWN_CONTAINING_BLOCK = 'The containing block could not be identified confidently.';
const UNKNOWN_SCROLLPORT = 'The scrollport could not be identified confidently.';

export interface StickyInsets {
  top: string|null;
  right: string|null;
  bottom: string|null;
  left: string|null;
}

export function insetsFromExplanation(explanation: LayoutExplanation|null): StickyInsets {
  const insets: StickyInsets = {top: null, right: null, bottom: null, left: null};
  for (const axis of explanation?.axes ?? []) {
    for (const contribution of axis.contributions) {
      if (contribution.property === 'top' || contribution.property === 'right' ||
          contribution.property === 'bottom' || contribution.property === 'left') {
        if (!insets[contribution.property]) {
          insets[contribution.property] = contribution.value;
        }
      }
    }
  }
  return insets;
}

export function presentLayoutRelationships(
    relationships: LayoutRelationships, labels: ReadonlyMap<number, string>, insets: StickyInsets,
    selectedNodeId: number|null): PaneRow[] {
  const rows: PaneRow[] = [];
  if (relationships.containingBlock) {
    rows.push(containingBlockRow(relationships.containingBlock, labels, selectedNodeId));
  }
  rows.push(...stickyRows(relationships, labels, insets, selectedNodeId));
  const scrollRow = scrollContainerRow(relationships, labels, selectedNodeId, rows);
  if (scrollRow) {
    rows.push(scrollRow);
  }
  return rows;
}

export function mergePositionRelationships(
    sections: PaneSection[], relationships: LayoutRelationships|null, labels: ReadonlyMap<number, string>,
    insets: StickyInsets, selectedNodeId: number|null): PaneSection[] {
  if (!relationships) {
    return sections;
  }
  const rows = presentLayoutRelationships(relationships, labels, insets, selectedNodeId);
  if (!rows.length && !relationships.containingBlock && !relationships.stickyScrollContainers) {
    return sections;
  }
  const next = sections.map(section => section.title === 'Position' ? adjustPositionSection(section, relationships) : section);
  const position = next.find(section => section.title === 'Position');
  if (!position) {
    if (!rows.length) {
      return next;
    }
    return [...next, {title: 'Position', lines: [], rows, tokens: [], diagnostics: []}];
  }
  position.rows = [...position.rows, ...rows];
  return next;
}

function adjustPositionSection(section: PaneSection, relationships: LayoutRelationships): PaneSection {
  const dropContainingBlockCaveat = Boolean(relationships.containingBlock);
  const dropInferredScrollport = Boolean(relationships.stickyScrollContainers);
  return {
    ...section,
    rows: section.rows.map(row => ({
      ...row,
      details: row.details.filter(detail => {
        if (dropContainingBlockCaveat && detail === UNKNOWN_CONTAINING_BLOCK) {
          return false;
        }
        if (dropInferredScrollport && detail === UNKNOWN_SCROLLPORT) {
          return false;
        }
        return true;
      }),
      primary: dropInferredScrollport && row.primary.includes('scrollport') ? 'Sticky' : row.primary,
      controls: dropInferredScrollport && isStickyRow(row) ?
          row.controls.filter(control => !isInferredScrollportControl(control)) :
          row.controls,
    })),
  };
}

function isStickyRow(row: PaneRow): boolean {
  return row.primary === 'Sticky' || row.primary.includes('scrollport');
}

function isInferredScrollportControl(control: PaneControl): boolean {
  // Phase 1 names the nearest scrollport as an appearance. Once Blink reports
  // the axis containers, that label is dropped even when Highlight was already
  // consumed and the control no longer carries a node id. Inset contributions stay.
  return control.actions.declarationId === null && !control.text.includes('contributes');
}

function containingBlockRow(
    relationship: LayoutRelationship, labels: ReadonlyMap<number, string>, selectedNodeId: number|null): PaneRow {
  if (relationship.kind === RelationshipKind.ELEMENT && relationship.nodeId !== null) {
    const label = labels.get(relationship.nodeId) ?? 'element';
    return row('Containing block', `Positioned relative to ${label}`, elementControl(label, relationship.nodeId, selectedNodeId));
  }
  if (relationship.kind === RelationshipKind.ANONYMOUS) {
    return row('Containing block', 'Positioned relative to an anonymous layout container.', null);
  }
  return row('Containing block', 'Positioned relative to the viewport.', null);
}

function stickyRows(
    relationships: LayoutRelationships, labels: ReadonlyMap<number, string>, insets: StickyInsets,
    selectedNodeId: number|null): PaneRow[] {
  const sticky = relationships.stickyScrollContainers;
  if (!sticky) {
    return [];
  }
  const horizontal = sticky.horizontal;
  const vertical = sticky.vertical;
  if (horizontal && vertical && sameRelationship(horizontal, vertical)) {
    return [stickyGroupRow('Sticky', [...verticalInsetLines(insets), ...horizontalInsetLines(insets)], horizontal, labels, selectedNodeId)];
  }
  const rows: PaneRow[] = [];
  if (vertical) {
    rows.push(stickyGroupRow('Vertical', verticalInsetLines(insets), vertical, labels, selectedNodeId));
  }
  if (horizontal) {
    rows.push(stickyGroupRow('Horizontal', horizontalInsetLines(insets), horizontal, labels, selectedNodeId));
  }
  return rows;
}

function stickyGroupRow(
    label: string, insetLines: string[], relationship: LayoutRelationship, labels: ReadonlyMap<number, string>,
    selectedNodeId: number|null): PaneRow {
  const [primary, ...rest] = insetLines.length ? insetLines : ['Sticky'];
  const controls = rest.map(line => ({text: line, title: line, actions: {nodeId: null, declarationId: null}}));
  const target = relationshipControl(relationship, labels, selectedNodeId);
  if (target) {
    controls.push(target);
  }
  return {label, primary, note: null, controls, details: [], diagnostics: []};
}

function scrollContainerRow(
    relationships: LayoutRelationships, labels: ReadonlyMap<number, string>, selectedNodeId: number|null,
    shown: PaneRow[]): PaneRow|null {
  const scroll = relationships.scrollContainer;
  if (!scroll) {
    return null;
  }
  if (scroll.kind === RelationshipKind.DOCUMENT) {
    if (relationships.containingBlock?.kind === RelationshipKind.INITIAL_CONTAINING_BLOCK && scroll.fixedToViewport) {
      return null;
    }
    const namedBySticky = relationships.stickyScrollContainers &&
        (relationships.stickyScrollContainers.horizontal?.kind === RelationshipKind.DOCUMENT ||
         relationships.stickyScrollContainers.vertical?.kind === RelationshipKind.DOCUMENT);
    if (namedBySticky && !scroll.fixedToViewport) {
      return null;
    }
    const text = scroll.fixedToViewport ? 'Fixed to the viewport.' : 'Scroll container is the document.';
    return row('Scroll container', text, null);
  }
  if (scroll.nodeId === null) {
    return null;
  }
  if (shown.some(item => item.controls.some(control => control.actions.nodeId === scroll.nodeId))) {
    return null;
  }
  const label = labels.get(scroll.nodeId) ?? 'element';
  return row('Scroll container', label, elementControl(label, scroll.nodeId, selectedNodeId));
}

function relationshipControl(
    relationship: LayoutRelationship, labels: ReadonlyMap<number, string>, selectedNodeId: number|null): PaneRow['controls'][number]|null {
  if (relationship.kind === RelationshipKind.ELEMENT && relationship.nodeId !== null) {
    const label = labels.get(relationship.nodeId) ?? 'element';
    const prefix = 'Constrained by';
    return {
      text: `${prefix} ${label}`,
      title: `${prefix} ${label}`,
      actions: {nodeId: relationship.nodeId !== selectedNodeId ? relationship.nodeId : null, declarationId: null},
    };
  }
  const text = relationship.fixedToViewport ? 'Constrained by the viewport.' : 'Constrained by the document.';
  return {text, title: text, actions: {nodeId: null, declarationId: null}};
}

function elementControl(label: string, nodeId: number, selectedNodeId: number|null): PaneRow['controls'][number] {
  return {
    text: label,
    title: label,
    actions: {nodeId: nodeId !== selectedNodeId ? nodeId : null, declarationId: null},
  };
}

function row(label: string, primary: string, control: PaneRow['controls'][number]|null): PaneRow {
  return {
    label,
    primary,
    note: null,
    controls: control ? [control] : [],
    details: [],
    diagnostics: [],
  };
}

function verticalInsetLines(insets: StickyInsets): string[] {
  return insetLines([['top', insets.top], ['bottom', insets.bottom]]);
}

function horizontalInsetLines(insets: StickyInsets): string[] {
  return insetLines([['left', insets.left], ['right', insets.right]]);
}

function insetLines(pairs: Array<[string, string|null]>): string[] {
  return pairs.filter((pair): pair is [string, string] => Boolean(pair[1] && pair[1] !== 'auto'))
      .map(([property, value]) => `${property}: ${value}`);
}

function sameRelationship(horizontal: LayoutRelationship, vertical: LayoutRelationship): boolean {
  if (horizontal.kind !== vertical.kind) {
    return false;
  }
  if (horizontal.kind === RelationshipKind.ELEMENT) {
    return horizontal.nodeId !== null && horizontal.nodeId === vertical.nodeId;
  }
  return true;
}
