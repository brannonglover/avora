// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Authoritative layout relationships from CSS.getLayoutRelationshipsForNode.
 *
 * containingBlock, scrollContainer, and stickyScrollContainers stay distinct.
 * A fixture that returns the same node for more than one of them does not
 * collapse the model. Sticky axes are Blink's physical axes. This module does
 * not walk CSS to invent a container.
 */

export const RelationshipKind = {
  ELEMENT: 'element',
  INITIAL_CONTAINING_BLOCK: 'initial-containing-block',
  ANONYMOUS: 'anonymous',
  DOCUMENT: 'document',
} as const;

export type RelationshipKind = (typeof RelationshipKind)[keyof typeof RelationshipKind];

export interface LayoutRelationship {
  kind: RelationshipKind;
  /** Set only when kind is element. */
  nodeId: number|null;
  fixedToViewport: boolean;
}

export interface StickyScrollContainers {
  horizontal: LayoutRelationship|null;
  vertical: LayoutRelationship|null;
}

export interface LayoutRelationships {
  containingBlock: LayoutRelationship|null;
  scrollContainer: LayoutRelationship|null;
  stickyScrollContainers: StickyScrollContainers|null;
}

export interface ProtocolRelationshipTarget {
  kind?: string;
  nodeId?: number;
  fixedToViewport?: boolean;
}

export interface ProtocolLayoutRelationships {
  containingBlock?: ProtocolRelationshipTarget;
  scrollContainer?: ProtocolRelationshipTarget;
  stickyScrollContainers?: {
    horizontal?: ProtocolRelationshipTarget,
    vertical?: ProtocolRelationshipTarget,
  };
}

const CONTAINING_BLOCK_KINDS = new Set<string>([
  RelationshipKind.ELEMENT,
  RelationshipKind.INITIAL_CONTAINING_BLOCK,
  RelationshipKind.ANONYMOUS,
]);

const SCROLL_KINDS = new Set<string>([
  RelationshipKind.ELEMENT,
  RelationshipKind.DOCUMENT,
]);

export function readLayoutRelationships(raw: ProtocolLayoutRelationships|null|undefined): LayoutRelationships|null {
  if (!raw) {
    return null;
  }
  const containingBlock = readTarget(raw.containingBlock, CONTAINING_BLOCK_KINDS, false);
  const scrollContainer = readTarget(raw.scrollContainer, SCROLL_KINDS, true);
  const horizontal = readTarget(raw.stickyScrollContainers?.horizontal, SCROLL_KINDS, true);
  const vertical = readTarget(raw.stickyScrollContainers?.vertical, SCROLL_KINDS, true);
  const stickyScrollContainers = horizontal || vertical ? {horizontal, vertical} : null;
  if (!containingBlock && !scrollContainer && !stickyScrollContainers) {
    return null;
  }
  return {containingBlock, scrollContainer, stickyScrollContainers};
}

export function positionNeedsLayoutRelationships(position: string|null): boolean {
  return position === 'absolute' || position === 'fixed' || position === 'sticky' || position === '-webkit-sticky';
}

function readTarget(
    raw: ProtocolRelationshipTarget|undefined, allowed: Set<string>, scroll: boolean): LayoutRelationship|null {
  if (!raw || !raw.kind || !allowed.has(raw.kind)) {
    return null;
  }
  const kind = raw.kind as RelationshipKind;
  if (kind === RelationshipKind.ELEMENT) {
    if (!raw.nodeId) {
      return null;
    }
    return {kind, nodeId: raw.nodeId, fixedToViewport: scroll ? Boolean(raw.fixedToViewport) : false};
  }
  return {kind, nodeId: null, fixedToViewport: scroll ? Boolean(raw.fixedToViewport) : false};
}
