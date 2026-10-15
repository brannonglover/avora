// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Style Studio targeting. A target is either one DOM node, named by its node id,
 * or the set of elements a selector matches in one document. The two are never
 * interchangeable: a selector that happens to match one element today is still
 * a selector, and a node is never re-identified by a selector derived from it.
 */

/** Where the target comes from. */
export type StudioSource = 'selected-node'|'selector';

/**
 * How much the target covers. It is only ever widened to `all-matches` by an
 * explicit choice; changing the source resets it to `node`.
 */
export type StudioScope = 'node'|'all-matches';

export type UnsupportedNodeReason =
    /** Text, comment, document, doctype, and other non-element nodes. */
    'not-element'|
    /** A pseudo-element node. Selectors cannot address it as a node. */
    'pseudo-element'|
    /**
     * Inside a shadow tree. Inspector stylesheets are not added to shadow-tree
     * scopes, so a later preview could not reach the element.
     */
    'shadow-tree'|
    /** The node has no owner document to resolve selectors in. */
    'no-document';

/**
 * A DOM node as Style Studio sees it. `documentKey` names the document the node
 * lives in, including its target and document node id, so a resolution made in
 * another frame or before a navigation is never applied to this node.
 */
export interface StudioNode {
  nodeId: number;
  label: string;
  documentKey: string|null;
  frameId: string|null;
  unsupported: UnsupportedNodeReason|null;
}

export type SelectorResolution = {
  kind: 'idle',
}|{
  kind: 'resolving',
  selector: string,
  documentKey: string,
}|{
  kind: 'invalid',
  selector: string,
  documentKey: string,
  message: string,
}|{
  kind: 'resolved',
  selector: string,
  documentKey: string,
  nodeIds: number[],
};

export interface StudioTargetingState {
  source: StudioSource;
  scope: StudioScope;
  node: StudioNode|null;
  selectorText: string;
  resolution: SelectorResolution;
}

export type StudioTarget = {
  kind: 'none',
}|{
  kind: 'unsupported',
  reason: UnsupportedNodeReason,
}|{
  kind: 'empty-selector',
}|{
  kind: 'pending',
  selector: string,
}|{
  kind: 'invalid-selector',
  selector: string,
  message: string,
}|{
  kind: 'no-matches',
  selector: string,
}|{
  /** Scope is one node, and the selected node is not one of the matches. */
  kind: 'node-not-matched',
  selector: string,
  matchCount: number,
}|{
  kind: 'node',
  nodeId: number,
  label: string,
  documentKey: string,
  frameId: string|null,
  /** Present when the node was reached through a selector. */
  selector: string|null,
  matchCount: number|null,
}|{
  kind: 'matches',
  selector: string,
  documentKey: string,
  frameId: string|null,
  nodeIds: number[],
  includesSelected: boolean,
};

/** Trims a selector for querying. An empty result means "no selector". */
export function normalizeSelector(text: string): string {
  return text.trim();
}

export function initialTargetingState(): StudioTargetingState {
  return {
    source: 'selected-node',
    scope: 'node',
    node: null,
    selectorText: '',
    resolution: {kind: 'idle'},
  };
}

/**
 * Derives the target from the state. A resolution only counts when it was made
 * for the current selector in the current node's document; anything else is
 * pending, never reused.
 */
export function deriveTarget(state: StudioTargetingState): StudioTarget {
  const node = state.node;
  if (!node) {
    return {kind: 'none'};
  }

  if (state.source === 'selected-node') {
    if (node.unsupported) {
      return {kind: 'unsupported', reason: node.unsupported};
    }
    if (!node.documentKey) {
      return {kind: 'unsupported', reason: 'no-document'};
    }
    return {
      kind: 'node',
      nodeId: node.nodeId,
      label: node.label,
      documentKey: node.documentKey,
      frameId: node.frameId,
      selector: null,
      matchCount: null,
    };
  }

  // A selector is resolved in the selected node's document. Without a document
  // there is nothing to resolve against.
  if (!node.documentKey) {
    return {kind: 'unsupported', reason: 'no-document'};
  }
  // One-node scope can only ever target the selected node, so an unsupported
  // node is reported before anything about the selector. This is what a pick of
  // an unsupported element shows instead of an unrelated selector status.
  if (state.scope === 'node' && node.unsupported) {
    return {kind: 'unsupported', reason: node.unsupported};
  }
  const selector = normalizeSelector(state.selectorText);
  if (!selector) {
    return {kind: 'empty-selector'};
  }
  const resolution = state.resolution;
  if (resolution.kind === 'idle' || resolution.selector !== selector ||
      resolution.documentKey !== node.documentKey) {
    return {kind: 'pending', selector};
  }
  if (resolution.kind === 'resolving') {
    return {kind: 'pending', selector};
  }
  if (resolution.kind === 'invalid') {
    return {kind: 'invalid-selector', selector, message: resolution.message};
  }
  if (!resolution.nodeIds.length) {
    return {kind: 'no-matches', selector};
  }

  const includesSelected = resolution.nodeIds.includes(node.nodeId);
  if (state.scope === 'all-matches') {
    return {
      kind: 'matches',
      selector,
      documentKey: node.documentKey,
      frameId: node.frameId,
      nodeIds: [...resolution.nodeIds],
      includesSelected,
    };
  }

  // Scope is one node: the selected one, and only if the selector matches it.
  // A unique match elsewhere is not substituted for the selection.
  if (node.unsupported) {
    return {kind: 'unsupported', reason: node.unsupported};
  }
  if (!includesSelected) {
    return {kind: 'node-not-matched', selector, matchCount: resolution.nodeIds.length};
  }
  return {
    kind: 'node',
    nodeId: node.nodeId,
    label: node.label,
    documentKey: node.documentKey,
    frameId: node.frameId,
    selector,
    matchCount: resolution.nodeIds.length,
  };
}

/** True when the state needs a selector query to derive its target. */
export function needsResolution(state: StudioTargetingState): boolean {
  if (state.source !== 'selector' || !state.node?.documentKey) {
    return false;
  }
  const selector = normalizeSelector(state.selectorText);
  if (!selector) {
    return false;
  }
  const resolution = state.resolution;
  return resolution.kind === 'idle' || resolution.selector !== selector ||
      resolution.documentKey !== state.node.documentKey;
}

/** At most this many classes go into a suggested selector, so it stays readable. */
const MAX_SUGGESTED_CLASSES = 3;

/**
 * A short, readable selector for an element: `#id` when it has one, otherwise
 * its tag and classes. This is a starting point for the selector input, not an
 * identity: the resolved match count is always shown, because a class selector
 * is rarely unique. Returns null when there is nothing readable to use.
 */
export function readableSelector(element: {localName: string, id: string|null, classes: readonly string[]}):
    string|null {
  const id = element.id?.trim();
  if (id) {
    return `#${CSS.escape(id)}`;
  }
  const classes = element.classes.map(name => name.trim()).filter(Boolean).slice(0, MAX_SUGGESTED_CLASSES);
  if (!classes.length || !element.localName) {
    return null;
  }
  return element.localName + classes.map(name => `.${CSS.escape(name)}`).join('');
}
