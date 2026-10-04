// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Short Layering copy. The reason list stays complete in the model.
 * Display order is not a claim that one property is the primary cause.
 */

import {type StackingBoundary} from './StackingBoundary.js';
import {isDocumentRootContext, type StackingChain, type StackingChainNode} from './StackingChain.js';
import {type StackingReason} from './StackingReasons.js';

export interface LayeringAction {
  text: string;
  nodeId: number|null;
  declarationId: number|null;
}

export interface LayeringView {
  lines: string[];
  actions: LayeringAction[];
}

export function presentLayering(chain: StackingChain, boundary: StackingBoundary|null, selectedNodeId: number|null):
    LayeringView {
  if (chain.selected.topLayer) {
    return {
      lines: [
        'Top layer',
        'This element is in the browser\'s top layer.',
        'Top-layer elements paint above ordinary document z-index stacking.',
      ],
      actions: [],
    };
  }
  if (boundary && boundary.trapNodeId !== selectedNodeId) {
    const lines = boundary.order.kind === 'z-index' ?
        [
          `${chain.selected.label} cannot rise above ${boundary.otherLabel} by increasing its z-index.`,
          `${chain.selected.label} is inside ${boundary.trapLabel}'s stacking context.`,
          boundary.trapLabel,
          boundary.trapText,
          `${boundary.otherLabel} is in a later sibling stacking context.`,
          boundary.otherText,
        ] :
        [
          `${chain.selected.label} is confined to ${boundary.trapLabel}'s stacking context.`,
          boundary.trapLabel,
          boundary.trapText,
          `${boundary.otherLabel}'s sibling context is ordered later.`,
        ];
    return {
      lines: lines.filter(line => line.length > 0),
      actions: [{
        text: boundary.trapLabel,
        nodeId: boundary.trapNodeId,
        declarationId: boundary.trapHasSource ? boundary.trapDeclarationId : null,
      }],
    };
  }
  if (isInternal(chain.selected)) {
    return {
      lines: [
        'Creates a stacking context',
        'Chromium reports this as a stacking context, but no CSS-visible cause could be identified.',
      ],
      actions: [],
    };
  }
  if (chain.selected.createsStackingContext) {
    return ownContext(chain.selected, selectedNodeId);
  }
  const ancestor = chain.ancestorContexts[0] ?? null;
  if (!ancestor) {
    return {lines: ['This element does not create a stacking context.'], actions: []};
  }
  if (isDocumentRootContext(ancestor) && ancestor.reasons.every(reason => reason.property === 'root')) {
    return {
      lines: ['Inside the document', 'The document creates a stacking context.'],
      actions: ancestor.nodeId === selectedNodeId ? [] : [{text: ancestor.label, nodeId: ancestor.nodeId, declarationId: null}],
    };
  }
  return contextLines(`Inside ${ancestor.label}`, `${ancestor.label} creates a stacking context`, ancestor, selectedNodeId);
}

function ownContext(node: StackingChainNode, selectedNodeId: number|null): LayeringView {
  const lines = reasonLines(node.reasons);
  const viewLines = ['Creates a stacking context'];
  if (lines.length > 0) {
    viewLines.push(lines[0]);
  }
  if (lines.length > 1) {
    viewLines.push('Also:');
    viewLines.push(...lines.slice(1));
  }
  return {lines: viewLines, actions: declarationActions(node, selectedNodeId)};
}

function contextLines(first: string, second: string, node: StackingChainNode, selectedNodeId: number|null): LayeringView {
  return {
    lines: [first, second, ...reasonLines(node.reasons)],
    actions: declarationActions(node, selectedNodeId),
  };
}

function declarationActions(node: StackingChainNode, selectedNodeId: number|null): LayeringAction[] {
  const highlight = node.nodeId === selectedNodeId ? null : node.nodeId;
  const sourced = node.reasons.filter(reason => reason.declarationId !== null && reason.hasSource);
  if (sourced.length === 0) {
    return highlight === null ? [] : [{text: node.label, nodeId: highlight, declarationId: null}];
  }
  return sourced.map((reason, index) => ({
    text: formatReason(reason) ?? node.label,
    nodeId: index === 0 ? highlight : null,
    declarationId: reason.declarationId,
  }));
}

function reasonLines(reasons: StackingReason[]): string[] {
  const lines: string[] = [];
  for (const reason of reasons) {
    const line = formatReason(reason);
    if (line && !lines.includes(line)) {
      lines.push(line);
    }
  }
  return lines;
}

function formatReason(reason: StackingReason): string|null {
  if (reason.property === 'root' || reason.property === 'internal') {
    return null;
  }
  if (reason.property === 'will-change') {
    const value = reason.authoredValue ?? reason.computedValue;
    return value ? `will-change: ${value}` : null;
  }
  const value = reason.authoredValue ?? reason.computedValue;
  return value ? `${reason.property}: ${value}` : null;
}

function isInternal(node: StackingChainNode): boolean {
  return node.createsStackingContext && node.reasons.length > 0 && node.reasons.every(reason => reason.property === 'internal');
}
