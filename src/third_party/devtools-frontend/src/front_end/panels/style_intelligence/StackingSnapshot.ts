// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Reads the stacking bit, paint order, and a few computed fields from
 * DOMSnapshot.captureSnapshot. Paint order is the enclosing PaintLayer's
 * index. Nodes painted together share it. It is not an occlusion test
 * and not a compositor layer id. Sibling stacking order may use it only
 * under the rules in StackingBoundary.
 */

import {StackingConfidence, type StackingReason} from './StackingReasons.js';
import {type StackingChain, type StackingChainNode} from './StackingChain.js';

export interface SnapshotStrings {
  strings: string[];
}

export interface SnapshotDocumentData {
  nodes: {
    parentIndex: number[],
    nodeName: number[],
    backendNodeId: number[],
    attributes: number[][],
  };
  layout: {
    nodeIndex: number[],
    styles: number[][],
    stackingContexts: {index: number[]},
    paintOrders?: number[],
  };
}

export interface SnapshotNodeRecord {
  documentIndex: number;
  backendNodeId: number;
  parentBackendNodeId: number|null;
  nodeName: string;
  id: string|null;
  hasLayout: boolean;
  stackingContext: boolean;
  paintOrder: number|null;
  position: string|null;
  zIndex: string|null;
  display: string|null;
  overlay: string|null;
}

const STYLE_NAMES = ['position', 'z-index', 'display', 'overlay'];

export function snapshotStyleNames(): string[] {
  return STYLE_NAMES.slice();
}

export function readSnapshot(documents: SnapshotDocumentData[], strings: string[]): SnapshotNodeRecord[] {
  const records: SnapshotNodeRecord[] = [];
  documents.forEach((document, documentIndex) => {
    const layoutByNode = new Map<number, number>();
    document.layout.nodeIndex.forEach((nodeIndex, layoutIndex) => layoutByNode.set(nodeIndex, layoutIndex));
    const stacking = new Set(document.layout.stackingContexts?.index ?? []);
    for (let nodeIndex = 0; nodeIndex < document.nodes.backendNodeId.length; nodeIndex++) {
      const layoutIndex = layoutByNode.get(nodeIndex) ?? null;
      const styles = layoutIndex === null ? null : document.layout.styles[layoutIndex] ?? null;
      const parentIndex = document.nodes.parentIndex[nodeIndex];
      records.push({
        documentIndex,
        backendNodeId: document.nodes.backendNodeId[nodeIndex],
        parentBackendNodeId: parentIndex >= 0 ? document.nodes.backendNodeId[parentIndex] ?? null : null,
        nodeName: strings[document.nodes.nodeName[nodeIndex]] ?? '',
        id: attributeValue(document, strings, nodeIndex, 'id'),
        hasLayout: layoutIndex !== null,
        stackingContext: layoutIndex !== null && stacking.has(layoutIndex),
        paintOrder: layoutIndex === null ? null : document.layout.paintOrders?.[layoutIndex] ?? null,
        position: styleValue(styles, strings, 0),
        zIndex: styleValue(styles, strings, 1),
        display: styleValue(styles, strings, 2),
        overlay: styleValue(styles, strings, 3),
      });
    }
  });
  return records;
}

export function applySnapshot(chain: StackingChain, records: SnapshotNodeRecord[]): StackingChain {
  return {
    selected: confirmNode(chain.selected, records),
    ancestorContexts: chain.ancestorContexts.map(node => confirmNode(node, records)).filter(node => node.createsStackingContext),
  };
}

function confirmNode(node: StackingChainNode, records: SnapshotNodeRecord[]): StackingChainNode {
  const record = node.backendNodeId === null ? undefined : records.find(item => item.backendNodeId === node.backendNodeId);
  if (!record || !record.hasLayout) {
    return node;
  }
  const reasons = record.stackingContext ? (node.reasons.length > 0 ? node.reasons : [internalReason()]) : [];
  return {
    ...node,
    createsStackingContext: record.stackingContext,
    reasons,
    blinkStackingContext: record.stackingContext,
    paintOrder: record.paintOrder,
    topLayer: node.topLayer || record.overlay === 'auto',
  };
}

function internalReason(): StackingReason {
  return {
    property: 'internal',
    computedValue: null,
    authoredValue: null,
    declarationId: null,
    hasSource: false,
    confidence: StackingConfidence.LIMITED,
  };
}

function attributeValue(document: SnapshotDocumentData, strings: string[], nodeIndex: number, name: string): string|null {
  const attrs = document.nodes.attributes[nodeIndex] ?? [];
  for (let i = 0; i + 1 < attrs.length; i += 2) {
    if (strings[attrs[i]] === name) {
      return strings[attrs[i + 1]] ?? null;
    }
  }
  return null;
}

function styleValue(styles: number[]|null, strings: string[], slot: number): string|null {
  if (!styles || slot >= styles.length || styles[slot] < 0) {
    return null;
  }
  return strings[styles[slot]] ?? null;
}
