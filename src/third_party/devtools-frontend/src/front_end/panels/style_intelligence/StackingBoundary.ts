// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Explains when an element is inside an ancestor stacking context and a
 * sibling of that context is ordered later.
 *
 * The ancestor chain identifies the trapping context. The snapshot identifies
 * sibling contexts of that trap. A strictly greater effective z-index names
 * one sibling. Several later siblings resolve to the greatest z-index. A tie
 * at that value declines, including when their paint orders differ.
 *
 * When no sibling has a strictly greater z-index, paint order can still say
 * that one same-level sibling context is ordered after the trap. Both values
 * must be non-zero and different, both nodes must be confirmed stacking
 * contexts in the same document and parent context, and the sibling's
 * effective z-index must equal the trap's. Paint order 0 is the missing-layer
 * sentinel. This is stacking order, not an occlusion or covering claim.
 * Top-layer boxes are not ordinary siblings.
 */

import {isDocumentRootContext, type StackingChain} from './StackingChain.js';
import {zIndexAllowed} from './StackingReasons.js';
import {type SnapshotNodeRecord} from './StackingSnapshot.js';

export type StackingOrder = {
  kind: 'z-index',
  value: string,
}|{
  kind: 'paint-order',
};

export interface StackingBoundary {
  trapNodeId: number;
  trapLabel: string;
  trapText: string;
  trapDeclarationId: number|null;
  trapHasSource: boolean;
  otherBackendNodeId: number;
  otherLabel: string;
  otherText: string;
  order: StackingOrder;
}

export function explainStackingBoundary(chain: StackingChain, records: SnapshotNodeRecord[]): StackingBoundary|null {
  if (chain.selected.topLayer) {
    return null;
  }
  const trap = chain.ancestorContexts.find(node => !isDocumentRootContext(node));
  if (!trap || trap.backendNodeId === null) {
    return null;
  }
  const byBackend = new Map(records.map(record => [record.backendNodeId, record]));
  const trapRecord = byBackend.get(trap.backendNodeId);
  if (!trapRecord?.stackingContext) {
    return null;
  }
  const parentContext = nearestStackingAncestor(trapRecord, byBackend);
  if (!parentContext) {
    return null;
  }
  const trapZ = effectiveZIndex(trap.position, trap.zIndex, trap.layoutParentDisplay);
  const siblings = records.filter(record => {
    if (record.documentIndex !== trapRecord.documentIndex || record.backendNodeId === trap.backendNodeId ||
        !record.stackingContext || record.overlay === 'auto') {
      return false;
    }
    const ancestor = nearestStackingAncestor(record, byBackend);
    return ancestor?.backendNodeId === parentContext.backendNodeId;
  });
  const byZIndex = laterSiblingByZIndex(siblings, trapZ, byBackend);
  if (byZIndex === 'tie') {
    return null;
  }
  const chosen = byZIndex ?? laterSiblingByPaintOrder(siblings, trapZ, trapRecord.paintOrder, byBackend);
  if (!chosen) {
    return null;
  }
  const trapReason = trap.reasons.find(reason => reason.property !== 'root' && reason.property !== 'internal');
  const zIndexText = chosen.record.zIndex === 'auto' || chosen.record.zIndex === null ? 'auto' : chosen.record.zIndex;
  return {
    trapNodeId: trap.nodeId,
    trapLabel: trap.label,
    trapText: trapReason ? formatReason(trapReason.property, trapReason.authoredValue ?? trapReason.computedValue) : trap.label,
    trapDeclarationId: trapReason?.declarationId ?? null,
    trapHasSource: trapReason?.hasSource ?? false,
    otherBackendNodeId: chosen.record.backendNodeId,
    otherLabel: recordLabel(chosen.record),
    otherText: chosen.order.kind === 'z-index' ? formatReason('z-index', zIndexText) : '',
    order: chosen.order,
  };
}

function laterSiblingByZIndex(
    siblings: SnapshotNodeRecord[], trapZ: number, byBackend: Map<number, SnapshotNodeRecord>):
    {record: SnapshotNodeRecord, order: StackingOrder}|'tie'|null {
  let best: SnapshotNodeRecord|null = null;
  let bestZ = 0;
  let tied = false;
  for (const sibling of siblings) {
    const siblingZ = effectiveZIndex(sibling.position, sibling.zIndex, layoutParentDisplay(sibling, byBackend));
    if (siblingZ <= trapZ) {
      continue;
    }
    if (!best || siblingZ > bestZ) {
      best = sibling;
      bestZ = siblingZ;
      tied = false;
    } else if (siblingZ === bestZ) {
      tied = true;
    }
  }
  if (!best || tied) {
    return tied ? 'tie' : null;
  }
  const value = best.zIndex === 'auto' || best.zIndex === null ? 'auto' : best.zIndex;
  return {record: best, order: {kind: 'z-index', value}};
}

function laterSiblingByPaintOrder(
    siblings: SnapshotNodeRecord[], trapZ: number, trapPaint: number|null,
    byBackend: Map<number, SnapshotNodeRecord>): {record: SnapshotNodeRecord, order: StackingOrder}|null {
  if (trapPaint === null || trapPaint <= 0) {
    return null;
  }
  let best: SnapshotNodeRecord|null = null;
  let tied = false;
  for (const sibling of siblings) {
    const siblingZ = effectiveZIndex(sibling.position, sibling.zIndex, layoutParentDisplay(sibling, byBackend));
    const paint = sibling.paintOrder;
    if (siblingZ !== trapZ || paint === null || paint <= trapPaint) {
      continue;
    }
    if (!best || paint < (best.paintOrder ?? 0)) {
      best = sibling;
      tied = false;
    } else if (paint === best.paintOrder) {
      tied = true;
    }
  }
  if (!best || tied) {
    return null;
  }
  return {record: best, order: {kind: 'paint-order'}};
}

export function effectiveZIndex(position: string|null, zIndex: string|null, layoutParentDisplay: string|null): number {
  if (!zIndexAllowed({position, layoutParentDisplay})) {
    return 0;
  }
  if (zIndex === null || zIndex === 'auto') {
    return 0;
  }
  const value = Number(zIndex);
  return Number.isFinite(value) ? value : 0;
}

function nearestStackingAncestor(
    record: SnapshotNodeRecord, byBackend: Map<number, SnapshotNodeRecord>): SnapshotNodeRecord|null {
  let parentId = record.parentBackendNodeId;
  while (parentId !== null) {
    const parent = byBackend.get(parentId);
    if (!parent) {
      return null;
    }
    if (parent.stackingContext) {
      return parent;
    }
    parentId = parent.parentBackendNodeId;
  }
  return null;
}

function layoutParentDisplay(record: SnapshotNodeRecord, byBackend: Map<number, SnapshotNodeRecord>): string|null {
  let parentId = record.parentBackendNodeId;
  while (parentId !== null) {
    const parent = byBackend.get(parentId);
    if (!parent) {
      return null;
    }
    if (parent.hasLayout && parent.display && parent.display !== 'contents') {
      return parent.display;
    }
    parentId = parent.parentBackendNodeId;
  }
  return null;
}

function recordLabel(record: SnapshotNodeRecord): string {
  const name = record.nodeName.toLowerCase();
  return record.id ? `${name}#${record.id}` : name;
}

function formatReason(property: string, value: string|null): string {
  return `${property}: ${value ?? ''}`;
}
