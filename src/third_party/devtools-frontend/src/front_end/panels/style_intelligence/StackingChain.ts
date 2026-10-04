// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Ancestor stacking-context chain.
 *
 * Callers pass layout parents, nearest first. That walk is
 * parentLayoutNodeId, which already skips display:contents. A DOM parent
 * that is not a layout parent must not be inserted here.
 *
 * "Creates a stacking context" is the CSS-visible predicate. A positioned
 * box with z-index:auto can be stacked in paint without creating a context,
 * so those ancestors are omitted. Belonging to a context is the nearest
 * remaining ancestor. Compositor layers are not represented.
 */

import {explainStackingContext, type StackingReason, type StackingReasonInput} from './StackingReasons.js';

export interface StackingChainSource {
  nodeId: number;
  backendNodeId: number|null;
  label: string;
  style: StackingReasonInput;
  topLayer: boolean;
}

export interface StackingChainNode {
  nodeId: number;
  backendNodeId: number|null;
  label: string;
  createsStackingContext: boolean;
  reasons: StackingReason[];
  topLayer: boolean;
  position: string|null;
  zIndex: string|null;
  layoutParentDisplay: string|null;
  /** Null until a snapshot confirms Blink's bit. */
  blinkStackingContext: boolean|null;
  paintOrder: number|null;
}

export interface StackingChain {
  selected: StackingChainNode;
  /** Nearest ancestor stacking context first. Non-contexts are omitted. */
  ancestorContexts: StackingChainNode[];
}

export function buildStackingChain(
    selected: StackingChainSource, layoutParentsNearestFirst: StackingChainSource[]): StackingChain {
  return {
    selected: toNode(selected),
    ancestorContexts: layoutParentsNearestFirst.map(toNode).filter(node => node.createsStackingContext),
  };
}

/**
 * A snapshot is needed to compare sibling contexts when the element sits
 * inside a stacking context other than the document root. The root has no
 * sibling context to escape.
 */
export function needsStackingSnapshot(chain: StackingChain): boolean {
  return chain.ancestorContexts.some(node => !isDocumentRootContext(node));
}

export function isDocumentRootContext(node: StackingChainNode): boolean {
  return node.reasons.some(reason => reason.property === 'root');
}

function toNode(source: StackingChainSource): StackingChainNode {
  const facts = explainStackingContext(source.style);
  return {
    nodeId: source.nodeId,
    backendNodeId: source.backendNodeId,
    label: source.label,
    createsStackingContext: facts.createsStackingContext,
    reasons: facts.reasons,
    topLayer: source.topLayer,
    position: source.style.position,
    zIndex: source.style.zIndex,
    layoutParentDisplay: source.style.layoutParentDisplay,
    blinkStackingContext: null,
    paintOrder: null,
  };
}
