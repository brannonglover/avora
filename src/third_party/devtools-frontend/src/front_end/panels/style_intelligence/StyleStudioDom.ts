// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as Common from '../../core/common/common.js';
import type * as Platform from '../../core/platform/platform.js';
import * as SDK from '../../core/sdk/sdk.js';
import type * as ProtocolProxyApi from '../../generated/protocol-proxy-api.js';
import type * as Protocol from '../../generated/protocol.js';
import type * as UI from '../../ui/legacy/legacy.js';
import * as Elements from '../elements/elements.js';

import {identityForNode} from './StyleIntelligenceModel.js';
import {readableSelector, type StudioNode, type UnsupportedNodeReason} from './StyleStudioTarget.js';
import type {SelectorQueryResult, StudioSelection, StyleStudioTargeting} from './StyleStudioTargeting.js';

const DOCUMENT_EVENTS = [
  SDK.DOMModel.Events.NodeInserted,
  SDK.DOMModel.Events.NodeRemoved,
  SDK.DOMModel.Events.AttrModified,
  SDK.DOMModel.Events.AttrRemoved,
] as const;

function unsupportedReason(node: SDK.DOMModel.DOMNode): UnsupportedNodeReason|null {
  if (node.nodeType() !== Node.ELEMENT_NODE) {
    return 'not-element';
  }
  if (node.pseudoType()) {
    return 'pseudo-element';
  }
  if (node.isInShadowTree()) {
    return 'shadow-tree';
  }
  return null;
}

/**
 * The document key carries the target id as well as the document node id: node
 * ids are only unique within one target, and a navigation replaces the document
 * node, so a stale resolution can never match the new document.
 */
export function studioNodeFor(node: SDK.DOMModel.DOMNode): StudioNode {
  const document = node.ownerDocument;
  const unsupported = unsupportedReason(node);
  return {
    nodeId: node.id,
    label: unsupported === 'not-element' ? node.nodeNameInCorrectCase() : identityForNode(node),
    documentKey: document ? `${node.domModel().target().id()}:${document.id}` : null,
    frameId: node.frameId(),
    unsupported,
  };
}

/** The production StudioSelection: read-only queries in the node's own document. */
export function selectionForNode(node: SDK.DOMModel.DOMNode): StudioSelection {
  const studioNode = studioNodeFor(node);
  const domModel = node.domModel();
  const document = node.ownerDocument;
  return {
    node: studioNode,
    suggestedSelector(): string|null {
      if (studioNode.unsupported) {
        return null;
      }
      return readableSelector({
               localName: node.localName(),
               id: node.getAttribute('id') ?? null,
               classes: (node.getAttribute('class') ?? '').split(/\s+/),
             }) ??
          Elements.DOMPath.cssPath(node, true);
    },
    async querySelectorAll(selector: string): Promise<SelectorQueryResult> {
      if (!document) {
        return {error: 'No document'};
      }
      // DOMModel.querySelectorAll drops the protocol error, and the error is
      // what tells an invalid selector apart from one with no matches.
      const response = await domModel.agent.invoke_querySelectorAll({nodeId: document.id, selector});
      const error = response.getError();
      if (error) {
        return {error};
      }
      return {nodeIds: response.nodeIds ?? []};
    },
    observeDocument(listener: () => void): () => void {
      const onNodeEvent = (event: Common.EventTarget.EventTargetEvent<unknown>): void => {
        const changed = nodeFromEvent(event.data);
        if (!changed || changed.ownerDocument === document) {
          listener();
        }
      };
      const onDocumentUpdated = (): void => listener();
      for (const type of DOCUMENT_EVENTS) {
        domModel.addEventListener(type, onNodeEvent);
      }
      domModel.addEventListener(SDK.DOMModel.Events.DocumentUpdated, onDocumentUpdated);
      return () => {
        for (const type of DOCUMENT_EVENTS) {
          domModel.removeEventListener(type, onNodeEvent);
        }
        domModel.removeEventListener(SDK.DOMModel.Events.DocumentUpdated, onDocumentUpdated);
      };
    },
  };
}

function nodeFromEvent(data: unknown): SDK.DOMModel.DOMNode|null {
  if (data instanceof SDK.DOMModel.DOMNode) {
    return data;
  }
  if (data && typeof data === 'object') {
    const value = data as {node?: unknown, parent?: unknown};
    if (value.node instanceof SDK.DOMModel.DOMNode) {
      return value.node;
    }
    if (value.parent instanceof SDK.DOMModel.DOMNode) {
      return value.parent;
    }
  }
  return null;
}

/**
 * Feeds the Elements selection into Style Studio. It only reads the flavor and
 * listens for changes; it never sets one, so targeting cannot move the
 * Elements selection.
 */
export function bindStudioToSelection<T>(
    context: UI.Context.Context, flavorType: Platform.Constructor.Constructor<T>,
    toSelection: (value: T) => StudioSelection | null, targeting: StyleStudioTargeting): () => void {
  const sync = (): void => {
    const value = context.flavor(flavorType);
    targeting.setSelection(value ? toSelection(value) : null);
  };
  context.addFlavorChangeListener(flavorType, sync);
  sync();
  return () => context.removeFlavorChangeListener(flavorType, sync);
}

/** Receives picks made with the existing Elements picker, on any page target. */
export interface InspectPickObserver {
  /** The page reported a pick; its node is being resolved. */
  received(): void;
  /** The node the Elements panel selects for that pick, or null if it could not be resolved. */
  resolved(node: SDK.DOMModel.DOMNode|null): void;
  /** A page target that can be picked from was added or removed. */
  availabilityChanged(): void;
}

/**
 * Observes `Overlay.inspectNodeRequested`, the event the existing picker
 * completes with. It is the only signal that fires for every pick: picking the
 * element that is already selected leaves the selection, and so every selection
 * event, unchanged.
 *
 * The dispatcher is added beside OverlayModel's own (the protocol client keeps
 * a list per domain) and only listens. Inspect mode, the pick handler, and the
 * selection all stay with InspectElementModeController and ElementsPanel.
 */
export function observeInspectPicks(observer: InspectPickObserver): void {
  const registered = new WeakSet<SDK.Target.Target>();
  SDK.TargetManager.TargetManager.instance().observeModels(SDK.OverlayModel.OverlayModel, {
    modelAdded(model: SDK.OverlayModel.OverlayModel): void {
      const target = model.target();
      if (registered.has(target)) {
        return;
      }
      registered.add(target);
      target.registerOverlayDispatcher(new InspectPickDispatcher(target, observer));
      observer.availabilityChanged();
    },
    modelRemoved(): void {
      observer.availabilityChanged();
    },
  }, {scoped: true});
}

/**
 * The node ElementsPanel.revealAndSelectNode selects for a picked node: the
 * picked node itself when user-agent shadow DOM is shown, otherwise its
 * outermost host outside user-agent shadow trees.
 */
export function selectionForPickedNode(node: SDK.DOMModel.DOMNode): SDK.DOMModel.DOMNode {
  if (Common.Settings.Settings.instance().moduleSetting('show-ua-shadow-dom').get()) {
    return node;
  }
  let userAgentShadowRoot;
  while ((userAgentShadowRoot = node.ancestorUserAgentShadowRoot()) && userAgentShadowRoot.parentNode) {
    node = userAgentShadowRoot.parentNode;
  }
  return node;
}

class InspectPickDispatcher implements ProtocolProxyApi.OverlayDispatcher {
  readonly #target: SDK.Target.Target;
  readonly #observer: InspectPickObserver;

  constructor(target: SDK.Target.Target, observer: InspectPickObserver) {
    this.#target = target;
    this.#observer = observer;
  }

  inspectNodeRequested({backendNodeId}: Protocol.Overlay.InspectNodeRequestedEvent): void {
    this.#observer.received();
    void new SDK.DOMModel.DeferredDOMNode(this.#target, backendNodeId)
        .resolvePromise()
        .then(node => this.#observer.resolved(node ? selectionForPickedNode(node) : null))
        .catch(() => this.#observer.resolved(null));
  }

  nodeHighlightRequested(): void {
  }

  screenshotRequested(): void {
  }

  inspectPanelShowRequested(): void {
  }

  inspectedElementWindowRestored(): void {
  }

  inspectModeCanceled(): void {
  }
}
