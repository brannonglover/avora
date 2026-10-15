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
import {InspectPickHub, type InspectPickObserver} from './StyleStudioPicker.js';
import {readableSelector, type StudioAttachment, type StudioNode, type UnsupportedNodeReason} from './StyleStudioTarget.js';
import type {SelectorQueryResult, StudioSelection, StyleStudioTargeting} from './StyleStudioTargeting.js';

/**
 * Every DOMModel event that can change selector matches or detach the selection.
 * ChildNodeCountUpdated is the only event for an insertion under a node whose
 * children DevTools has not loaded, so it counts as a change too.
 */
const DOCUMENT_EVENTS = [
  SDK.DOMModel.Events.NodeInserted,
  SDK.DOMModel.Events.NodeRemoved,
  SDK.DOMModel.Events.AttrModified,
  SDK.DOMModel.Events.AttrRemoved,
  SDK.DOMModel.Events.ChildNodeCountUpdated,
] as const;

/**
 * True when `node` is still the node DevTools holds for its id and every
 * ancestor up to its target's document is too. DOMModel unbinds a removed
 * subtree and replaces its whole map on a new document, so a removed, moved, or
 * replaced node fails here, and its id never comes back for another element.
 */
export function isNodeAttached(node: SDK.DOMModel.DOMNode): boolean {
  const domModel = node.domModel();
  if (domModel.target().isDisposed()) {
    return false;
  }
  let current: SDK.DOMModel.DOMNode = node;
  while (true) {
    if (domModel.nodeForId(current.id) !== current) {
      return false;
    }
    const parent = current.parentNode;
    // A child target's document hangs off a frame owner in the parent target.
    if (!parent || parent.domModel() !== domModel) {
      break;
    }
    current = parent;
  }
  return current === domModel.existingDocument();
}

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
    attachment(): StudioAttachment {
      const documentAttached = document !== null && isNodeAttached(document);
      return {node: documentAttached && isNodeAttached(node), document: documentAttached};
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
      // Changes in this document are reported, and so is anything that changes
      // whether the selection is attached, wherever it happens: a child target
      // replaces its document without a DocumentUpdated event, and removing an
      // iframe detaches every node inside it.
      let lastAttachment = this.attachment();
      const onChange = (event?: Common.EventTarget.EventTargetEvent<unknown>): void => {
        const attachment = this.attachment();
        const attachmentChanged =
            attachment.node !== lastAttachment.node || attachment.document !== lastAttachment.document;
        lastAttachment = attachment;
        const changed = event ? nodeFromEvent(event.data) : null;
        if (attachmentChanged || !changed || changed.ownerDocument === document) {
          listener();
        }
      };
      const targetObserver: SDK.TargetManager.SDKModelObserver<SDK.DOMModel.DOMModel> = {
        modelAdded(): void {
        },
        modelRemoved(removed: SDK.DOMModel.DOMModel): void {
          if (removed === domModel) {
            onChange();
          }
        },
      };
      for (const type of DOCUMENT_EVENTS) {
        domModel.addEventListener(type, onChange);
      }
      domModel.addEventListener(SDK.DOMModel.Events.DocumentUpdated, onChange);
      SDK.TargetManager.TargetManager.instance().observeModels(SDK.DOMModel.DOMModel, targetObserver);
      return () => {
        for (const type of DOCUMENT_EVENTS) {
          domModel.removeEventListener(type, onChange);
        }
        domModel.removeEventListener(SDK.DOMModel.Events.DocumentUpdated, onChange);
        SDK.TargetManager.TargetManager.instance().unobserveModels(SDK.DOMModel.DOMModel, targetObserver);
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

let pickHub: InspectPickHub<SDK.Target.Target, SDK.DOMModel.DOMNode>|null = null;

/**
 * The one pick hub for this DevTools window. Every subscriber shares it, so
 * creating panes, subscribing, and unsubscribing never adds another protocol
 * listener: each page target gets exactly one, for its lifetime. A new DevTools
 * window is a new JavaScript realm and starts with a new hub.
 */
export function inspectPickHub(): InspectPickHub<SDK.Target.Target, SDK.DOMModel.DOMNode> {
  if (!pickHub) {
    pickHub = new InspectPickHub<SDK.Target.Target, SDK.DOMModel.DOMNode>({
      observeTargets(added, removed): void {
        SDK.TargetManager.TargetManager.instance().observeModels(SDK.OverlayModel.OverlayModel, {
          modelAdded: model => added(model.target()),
          modelRemoved: model => removed(model.target()),
        }, {scoped: true});
      },
      listen(target, onPick): void {
        // Added beside OverlayModel's own dispatcher; the protocol client keeps a
        // list per domain and has no public removal, which is why the hub only
        // ever adds one per target.
        target.registerOverlayDispatcher(new InspectPickDispatcher(
            target,
            backendNodeId => onPick(
                () => new SDK.DOMModel.DeferredDOMNode(target, backendNodeId)
                          .resolvePromise()
                          .then(node => node ? selectionForPickedNode(node) : null))));
      },
    });
  }
  return pickHub;
}

/** Subscribes to picks made with the existing Elements picker. Returns an unsubscribe. */
export function observeInspectPicks(observer: InspectPickObserver<SDK.DOMModel.DOMNode>): () => void {
  return inspectPickHub().subscribe(observer);
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

/**
 * Listens for `Overlay.inspectNodeRequested`, the event the existing picker
 * completes with. It is the only signal that fires for every pick: picking the
 * element that is already selected changes no selection. Every other Overlay
 * event is left to OverlayModel.
 */
class InspectPickDispatcher implements ProtocolProxyApi.OverlayDispatcher {
  readonly #target: SDK.Target.Target;
  readonly #onPick: (backendNodeId: Protocol.DOM.BackendNodeId) => void;

  constructor(target: SDK.Target.Target, onPick: (backendNodeId: Protocol.DOM.BackendNodeId) => void) {
    this.#target = target;
    this.#onPick = onPick;
  }

  inspectNodeRequested({backendNodeId}: Protocol.Overlay.InspectNodeRequestedEvent): void {
    if (!this.#target.isDisposed()) {
      this.#onPick(backendNodeId);
    }
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
