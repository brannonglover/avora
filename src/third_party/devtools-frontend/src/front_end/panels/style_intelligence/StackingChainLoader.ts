// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Lazy stacking-chain load. Not used by StyleIntelligenceModel.describe.
 * The walk follows parentLayoutNodeId, so display:contents is skipped.
 */

import * as SDK from '../../core/sdk/sdk.js';

import {buildStackingChain, type StackingChain, type StackingChainSource} from './StackingChain.js';
import {type StackingDeclaration, type StackingReasonInput} from './StackingReasons.js';
import {readSnapshot, snapshotStyleNames, type SnapshotNodeRecord} from './StackingSnapshot.js';
import {identityForNode} from './StyleIntelligenceModel.js';

const MAX_LAYOUT_ANCESTORS = 64;

const DECLARATION_PROPERTIES = [
  'position',
  'z-index',
  'opacity',
  'transform',
  'translate',
  'rotate',
  'scale',
  'offset-path',
  'offset-position',
  'transform-style',
  'perspective',
  'filter',
  'backdrop-filter',
  'clip-path',
  'mask-image',
  '-webkit-mask-image',
  'isolation',
  'mix-blend-mode',
  'contain',
  'will-change',
  'view-transition-name',
  '-webkit-box-reflect',
];

export interface StackingChainLoad {
  chain: StackingChain;
  declarations: Map<number, SDK.CSSProperty.CSSProperty>;
}

export async function loadStackingChain(node: SDK.DOMModel.DOMNode): Promise<StackingChainLoad|null> {
  if (node.nodeType() !== Node.ELEMENT_NODE || !node.id) {
    return null;
  }
  const cssModel = node.domModel().cssModel();
  if (!cssModel) {
    return null;
  }
  const registry = new DeclarationRegistry();
  const reads: NodeRead[] = [];
  const seen = new Set<number>();
  let current: SDK.DOMModel.DOMNode|null = node;
  while (current?.id && !seen.has(current.id) && reads.length < MAX_LAYOUT_ANCESTORS) {
    seen.add(current.id);
    const read = await readNode(current, cssModel, registry);
    if (!read) {
      if (reads.length === 0) {
        return null;
      }
      break;
    }
    reads.push(read);
    if (read.isDocumentElement || read.layoutParentId === null) {
      break;
    }
    current = current.domModel().nodeForId(read.layoutParentId);
  }
  if (reads.length === 0) {
    return null;
  }
  const sources = reads.map((read, index) => toSource(read, reads[index + 1]?.display ?? null));
  const [selected, ...parents] = sources;
  return {chain: buildStackingChain(selected, parents), declarations: registry.properties};
}

export async function captureStackingSnapshot(node: SDK.DOMModel.DOMNode): Promise<SnapshotNodeRecord[]|null> {
  const agent = node.domModel().target().domsnapshotAgent();
  const result = await agent.invoke_captureSnapshot({
    computedStyles: snapshotStyleNames(),
    includePaintOrder: true,
    includeDOMRects: false,
  });
  return readSnapshot(result.documents.map(document => ({
    nodes: {
      parentIndex: document.nodes.parentIndex ?? [],
      nodeName: document.nodes.nodeName ?? [],
      backendNodeId: document.nodes.backendNodeId ?? [],
      attributes: document.nodes.attributes ?? [],
    },
    layout: {
      nodeIndex: document.layout.nodeIndex,
      styles: document.layout.styles,
      stackingContexts: document.layout.stackingContexts ?? {index: []},
      paintOrders: document.layout.paintOrders,
    },
  })), result.strings);
}

interface NodeRead {
  nodeId: number;
  backendNodeId: number;
  label: string;
  display: string|null;
  isDocumentElement: boolean;
  layoutParentId: number|null;
  topLayer: boolean;
  style: Omit<StackingReasonInput, 'layoutParentDisplay'>;
}

function toSource(read: NodeRead, layoutParentDisplay: string|null): StackingChainSource {
  return {
    nodeId: read.nodeId,
    backendNodeId: read.backendNodeId,
    label: read.label,
    topLayer: read.topLayer,
    style: {...read.style, layoutParentDisplay},
  };
}

async function readNode(
    node: SDK.DOMModel.DOMNode, cssModel: SDK.CSSModel.CSSModel, registry: DeclarationRegistry): Promise<NodeRead|null> {
  if (!node.id) {
    return null;
  }
  const [computed, matched] = await Promise.all([
    cssModel.getComputedStyle(node.id),
    cssModel.cachedMatchedCascadeForNode(node),
  ]);
  if (!computed) {
    return null;
  }
  const declarations: StackingReasonInput['declarations'] = {};
  if (matched) {
    for (const name of DECLARATION_PROPERTIES) {
      const fact = registry.fact(activeAuthorProperty(matched, name));
      if (fact) {
        declarations[name] = fact;
      }
    }
  }
  const documentElement = isDocumentElement(node);
  const value = (name: string): string|null => computed.get(name) ?? null;
  return {
    nodeId: node.id,
    backendNodeId: node.backendNodeId(),
    label: documentElement && !node.getAttribute('id') ? 'document' : identityForNode(node),
    display: value('display'),
    isDocumentElement: documentElement,
    layoutParentId: matched?.getParentLayoutNodeId() ?? null,
    topLayer: value('overlay') === 'auto',
    style: {
      display: value('display'),
      position: value('position'),
      zIndex: value('z-index'),
      opacity: value('opacity'),
      transform: value('transform'),
      translate: value('translate'),
      rotate: value('rotate'),
      scale: value('scale'),
      offsetPath: value('offset-path'),
      offsetPosition: value('offset-position'),
      transformStyle: value('transform-style'),
      perspective: value('perspective'),
      filter: value('filter'),
      backdropFilter: value('backdrop-filter'),
      clipPath: value('clip-path'),
      maskImage: value('mask-image'),
      webkitMaskImage: value('-webkit-mask-image'),
      isolation: value('isolation'),
      mixBlendMode: value('mix-blend-mode'),
      contain: value('contain'),
      willChange: value('will-change'),
      viewTransitionName: value('view-transition-name'),
      webkitBoxReflect: value('-webkit-box-reflect'),
      layoutParentDisplay: null,
      isDocumentElement: documentElement,
      declarations,
    },
  };
}

function isDocumentElement(node: SDK.DOMModel.DOMNode): boolean {
  return node.nodeNameInCorrectCase() === 'html' && node.parentNode?.nodeType() === Node.DOCUMENT_NODE;
}

class DeclarationRegistry {
  #next = 1;
  readonly properties = new Map<number, SDK.CSSProperty.CSSProperty>();

  fact(property: SDK.CSSProperty.CSSProperty|null): StackingDeclaration|null {
    if (!property) {
      return null;
    }
    const id = this.#next++;
    this.properties.set(id, property);
    const style = property.ownerStyle;
    const hasSource = style.type === SDK.CSSStyleDeclaration.Type.Regular && Boolean(style.styleSheetId) &&
        Boolean(property.valueRange());
    return {id, value: property.value, hasSource};
  }
}

function activeAuthorProperty(
    matched: SDK.CSSMatchedStyles.CSSMatchedStyles, name: string): SDK.CSSProperty.CSSProperty|null {
  let found: SDK.CSSProperty.CSSProperty|null = null;
  for (const style of matched.nodeStyles()) {
    if (matched.isInherited(style)) {
      continue;
    }
    if (style.parentRule?.isUserAgent() || style.parentRule?.isViaInspector()) {
      continue;
    }
    if (style.type !== SDK.CSSStyleDeclaration.Type.Regular && style.type !== SDK.CSSStyleDeclaration.Type.Inline) {
      continue;
    }
    for (const property of style.allProperties()) {
      if (property.name !== name || property.disabled || !property.parsedOk || !property.value) {
        continue;
      }
      if (matched.propertyState(property) !== SDK.CSSMatchedStyles.PropertyState.ACTIVE) {
        continue;
      }
      found = property;
    }
  }
  return found ? authoredDeclaration(matched, found) : null;
}

function authoredDeclaration(
    matched: SDK.CSSMatchedStyles.CSSMatchedStyles, property: SDK.CSSProperty.CSSProperty,
    ): SDK.CSSProperty.CSSProperty {
  if (!property.implicit && property.valueRange()) {
    return property;
  }
  const shorthands = SDK.CSSMetadata.cssMetadata().getShorthands(property.name) ?? [];
  for (const shorthandName of shorthands) {
    for (const candidate of property.ownerStyle.allProperties()) {
      if (candidate.name !== shorthandName || candidate.disabled || !candidate.parsedOk || !candidate.value) {
        continue;
      }
      if (matched.propertyState(candidate) !== SDK.CSSMatchedStyles.PropertyState.ACTIVE || !candidate.valueRange()) {
        continue;
      }
      return candidate;
    }
  }
  return property;
}
