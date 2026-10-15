// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  deriveTarget,
  initialTargetingState,
  needsResolution,
  normalizeSelector,
  type StudioAttachment,
  type StudioNode,
  type StudioScope,
  type StudioSource,
  type StudioTarget,
  type StudioTargetingState,
} from './StyleStudioTarget.js';

export type SelectorQueryResult = {
  nodeIds: number[],
}|{
  error: string,
};

/**
 * The selected DOM node plus the read-only DOM operations Style Studio needs in
 * that node's document. Nothing here selects, reveals, or edits anything.
 */
export interface StudioSelection {
  node: StudioNode;
  /** A readable selector for the node, used to prefill the input or fill it from a pick. */
  suggestedSelector(): string|null;
  /** Runs `querySelectorAll` in the node's own document. */
  querySelectorAll(selector: string): Promise<SelectorQueryResult>;
  /**
   * Whether the node and its document are still in the inspected page. Checked
   * on every observed change; never assumed.
   */
  attachment(): StudioAttachment;
  /**
   * Calls `listener` when the DOM of this selection's target changes in a way
   * that can affect the node, its document, or selector matches, and when the
   * target goes away. Returns an unsubscribe.
   */
  observeDocument(listener: () => void): () => void;
}

/** Delay after typing before a selector is queried. */
export const SELECTOR_RESOLVE_DELAY = 150;
/** Delay after a DOM mutation before matches are re-queried. */
export const DOM_CHANGE_RESOLVE_DELAY = 250;
/**
 * How often resolved matches are re-checked while Studio is visible. Blink only
 * reports mutations under nodes DevTools has loaded, so an element added or
 * re-classed elsewhere produces no event at all.
 */
export const BACKGROUND_REFRESH_INTERVAL = 2000;

/**
 * Holds the Style Studio target for the pane. It follows the Elements selection
 * it is given and never changes that selection itself.
 */
export class StyleStudioTargeting {
  #state: StudioTargetingState = initialTargetingState();
  #selection: StudioSelection|null = null;
  /**
   * The last selection before Elements cleared it. Elements clears the selection
   * before a reload replaces the document, so this is what tells whether the
   * next selection replaces a target that left the page.
   */
  #lastSelection: StudioSelection|null = null;
  #live = false;
  #refreshTimer = 0;
  readonly #refreshInterval: number;
  #unobserve: (() => void)|null = null;
  #onChange: () => void;
  #request = 0;
  #timer = 0;
  #disposed = false;

  constructor(onChange: () => void, options: {refreshInterval?: number} = {}) {
    this.#onChange = onChange;
    this.#refreshInterval = options.refreshInterval ?? BACKGROUND_REFRESH_INTERVAL;
  }

  state(): Readonly<StudioTargetingState> {
    return this.#state;
  }

  target(): StudioTarget {
    return deriveTarget(this.#state);
  }

  /**
   * Follows the Elements selection. A node in another document drops the old
   * resolution; a node in the same document keeps it, because the matches of a
   * selector do not depend on which element is selected.
   *
   * When the previous target had already left the page, the move to the new
   * selection is recorded so it can be announced: Elements re-selects on its own
   * after a removal or a reload, and Studio must not switch targets silently.
   */
  setSelection(selection: StudioSelection|null): void {
    if (this.#disposed) {
      return;
    }
    // Across a moment with no selection, compare against the last real one,
    // checked now: its document may have been replaced in the meantime.
    const prior = this.#state.node ? this.#selection : this.#lastSelection;
    const previous = prior?.node ?? null;
    const previousAttachment = this.#state.node ? this.#state.attachment : prior?.attachment();
    const previousDetached =
        previous !== null && previousAttachment !== undefined && (!previousAttachment.node || !previousAttachment.document);
    const previousDocument = this.#state.node?.documentKey ?? null;
    if (selection) {
      this.#lastSelection = null;
    } else if (this.#selection) {
      this.#lastSelection = this.#selection;
    }
    this.#selection = selection;
    const attachment = selection?.attachment() ?? {node: true, document: true};
    const changedNode = previous?.nodeId !== selection?.node.nodeId || previous?.documentKey !== selection?.node.documentKey;
    this.#state = {
      ...this.#state,
      node: selection?.node ?? null,
      attachment,
      previousDetachedLabel: !selection                         ? this.#state.previousDetachedLabel :
          previous !== null && previousDetached && changedNode ? previous.label :
          changedNode                                          ? null :
                                                                 this.#state.previousDetachedLabel,
    };
    const nextDocument = selection?.node.documentKey ?? null;
    // Resubscribe for every new selection object: the old subscription belongs
    // to the old node's target, which may be gone.
    this.#unobserve?.();
    this.#unobserve = selection?.observeDocument(() => this.#onDocumentChanged()) ?? null;
    if (previousDocument !== nextDocument || !attachment.document) {
      this.#request++;
      this.#state = {...this.#state, resolution: {kind: 'idle'}};
    }
    this.#scheduleResolve(0);
    this.#onChange();
  }

  /**
   * Checks the selection against the page again and re-queries the selector.
   * Called when Studio becomes visible: attribute changes on nodes DevTools has
   * not loaded produce no protocol event, so matches can drift unobserved.
   */
  revalidate(): void {
    if (this.#disposed || !this.#selection) {
      return;
    }
    this.#refreshAttachment();
    if (this.#state.source === 'selector' && normalizeSelector(this.#state.selectorText) &&
        this.#state.attachment.document) {
      this.#request++;
      this.#state = {...this.#state, resolution: {kind: 'idle'}};
      this.#scheduleResolve(0);
    }
    this.#onChange();
  }

  /**
   * Changing the source always resets the scope to one node, so a switch never
   * widens the target to every match without an explicit choice.
   */
  setSource(source: StudioSource): void {
    if (this.#disposed || source === this.#state.source) {
      return;
    }
    let selectorText = this.#state.selectorText;
    if (source === 'selector' && !normalizeSelector(selectorText)) {
      selectorText = this.#selection?.suggestedSelector() ?? '';
    }
    this.#state = {...this.#state, source, scope: 'node', selectorText, previousDetachedLabel: null};
    this.#scheduleResolve(0);
    this.#onChange();
  }

  /**
   * Replaces the selector with one for the selected element, after the user
   * picked that element on the page. The scope goes back to one node: a new
   * selector is never widened to all of its matches without a fresh choice.
   */
  fillSelectorFromSelection(): void {
    if (this.#disposed || this.#state.source !== 'selector') {
      return;
    }
    const selector = this.#selection?.suggestedSelector();
    if (!selector) {
      return;
    }
    this.#state = {...this.#state, selectorText: selector, scope: 'node', previousDetachedLabel: null};
    this.#scheduleResolve(0);
    this.#onChange();
  }

  setScope(scope: StudioScope): void {
    if (this.#disposed || scope === this.#state.scope) {
      return;
    }
    // Every match only exists for a selector; the selected node alone is one node.
    if (scope === 'all-matches' && this.#state.source !== 'selector') {
      return;
    }
    this.#state = {...this.#state, scope, previousDetachedLabel: null};
    this.#onChange();
  }

  setSelectorText(text: string): void {
    if (this.#disposed || text === this.#state.selectorText) {
      return;
    }
    this.#state = {...this.#state, selectorText: text, previousDetachedLabel: null};
    this.#scheduleResolve(SELECTOR_RESOLVE_DELAY);
    this.#onChange();
  }

  /**
   * Whether Studio is on screen. While it is, resolved matches are re-checked
   * in the background every refresh interval; hidden, nothing is polled.
   */
  setLive(live: boolean): void {
    if (this.#disposed || live === this.#live) {
      return;
    }
    this.#live = live;
    window.clearTimeout(this.#refreshTimer);
    if (live) {
      this.#scheduleBackgroundRefresh();
    }
  }

  /** Resolves the current selector now, skipping the typing delay. */
  async resolveNow(): Promise<void> {
    window.clearTimeout(this.#timer);
    await this.#resolve();
  }

  dispose(): void {
    this.#disposed = true;
    window.clearTimeout(this.#timer);
    window.clearTimeout(this.#refreshTimer);
    this.#lastSelection = null;
    this.#request++;
    this.#unobserve?.();
    this.#unobserve = null;
    this.#selection = null;
  }

  #onDocumentChanged(): void {
    if (this.#disposed) {
      return;
    }
    const attachmentChanged = this.#refreshAttachment();
    const resolution = this.#state.resolution;
    // An invalid selector stays invalid whatever the DOM does, so it is not re-queried.
    if (this.#state.source === 'selector' && resolution.kind !== 'idle' && resolution.kind !== 'invalid') {
      // The previous matches may no longer hold, and neither may a query still in
      // flight. Show the target as pending and query again once mutations settle.
      this.#request++;
      this.#state = {...this.#state, resolution: {kind: 'idle'}};
      this.#scheduleResolve(DOM_CHANGE_RESOLVE_DELAY);
      this.#onChange();
      return;
    }
    if (attachmentChanged) {
      this.#onChange();
    }
  }

  /** Re-reads attachment; a document that left the page also drops its resolution. Returns whether it changed. */
  #refreshAttachment(): boolean {
    const selection = this.#selection;
    if (!selection) {
      return false;
    }
    const next = selection.attachment();
    const current = this.#state.attachment;
    if (next.node === current.node && next.document === current.document) {
      return false;
    }
    this.#state = {...this.#state, attachment: next};
    if (!next.document) {
      window.clearTimeout(this.#timer);
      this.#request++;
      this.#state = {...this.#state, resolution: {kind: 'idle'}};
    }
    return true;
  }

  #scheduleBackgroundRefresh(): void {
    window.clearTimeout(this.#refreshTimer);
    this.#refreshTimer = window.setTimeout(() => void this.#backgroundRefresh(), this.#refreshInterval);
  }

  /**
   * Re-queries settled matches without showing a pending state, and replaces
   * them only when they differ. It only runs when nothing else is resolving,
   * and any newer request (typing, a DOM change, a new selection) supersedes it.
   */
  async #backgroundRefresh(): Promise<void> {
    if (this.#disposed || !this.#live) {
      return;
    }
    const selection = this.#selection;
    const resolution = this.#state.resolution;
    const selector = normalizeSelector(this.#state.selectorText);
    if (selection && this.#state.source === 'selector' && this.#state.attachment.document &&
        resolution.kind === 'resolved' && resolution.selector === selector &&
        resolution.documentKey === selection.node.documentKey) {
      const request = ++this.#request;
      let result: SelectorQueryResult|null = null;
      try {
        result = await selection.querySelectorAll(selector);
      } catch {
        result = null;
      }
      const current = this.#state.resolution;
      if (result && !('error' in result) && request === this.#request && !this.#disposed &&
          this.#selection === selection && this.#state.attachment.document && current.kind === 'resolved' &&
          current.selector === selector && normalizeSelector(this.#state.selectorText) === selector &&
          !sameIds(current.nodeIds, result.nodeIds)) {
        this.#state = {...this.#state, resolution: {...current, nodeIds: result.nodeIds}};
        this.#onChange();
      }
    }
    if (this.#live && !this.#disposed) {
      this.#scheduleBackgroundRefresh();
    }
  }

  #scheduleResolve(delay: number): void {
    window.clearTimeout(this.#timer);
    if (!needsResolution(this.#state)) {
      return;
    }
    if (delay === 0) {
      void this.#resolve();
      return;
    }
    this.#timer = window.setTimeout(() => void this.#resolve(), delay);
  }

  async #resolve(): Promise<void> {
    const selection = this.#selection;
    const documentKey = selection?.node.documentKey;
    if (this.#disposed || !selection || !documentKey || !needsResolution(this.#state)) {
      return;
    }
    const selector = normalizeSelector(this.#state.selectorText);
    const request = ++this.#request;
    this.#state = {...this.#state, resolution: {kind: 'resolving', selector, documentKey}};
    this.#onChange();

    let result: SelectorQueryResult;
    try {
      result = await selection.querySelectorAll(selector);
    } catch (error) {
      result = {error: error instanceof Error ? error.message : String(error)};
    }
    // Only a response for the current request generation, document, and
    // selector text may change state. Anything else is dropped; when only the
    // text moved on, the new text is queried instead.
    if (request !== this.#request || this.#disposed) {
      return;
    }
    if (this.#state.node?.documentKey !== documentKey || !this.#state.attachment.document) {
      return;
    }
    if (normalizeSelector(this.#state.selectorText) !== selector) {
      this.#scheduleResolve(SELECTOR_RESOLVE_DELAY);
      return;
    }
    this.#state = {
      ...this.#state,
      resolution: 'error' in result ? {kind: 'invalid', selector, documentKey, message: result.error} :
                                      {kind: 'resolved', selector, documentKey, nodeIds: result.nodeIds},
    };
    this.#onChange();
  }
}

function sameIds(a: readonly number[], b: readonly number[]): boolean {
  return a.length === b.length && a.every((id, index) => id === b[index]);
}
