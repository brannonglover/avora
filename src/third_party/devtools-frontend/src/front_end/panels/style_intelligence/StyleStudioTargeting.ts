// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  deriveTarget,
  initialTargetingState,
  needsResolution,
  normalizeSelector,
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
  /** Calls `listener` when the document's DOM changes. Returns an unsubscribe. */
  observeDocument(listener: () => void): () => void;
}

/** Delay after typing before a selector is queried. */
export const SELECTOR_RESOLVE_DELAY = 150;
/** Delay after a DOM mutation before matches are re-queried. */
export const DOM_CHANGE_RESOLVE_DELAY = 250;

/**
 * Holds the Style Studio target for the pane. It follows the Elements selection
 * it is given and never changes that selection itself.
 */
export class StyleStudioTargeting {
  #state: StudioTargetingState = initialTargetingState();
  #selection: StudioSelection|null = null;
  #unobserve: (() => void)|null = null;
  #onChange: () => void;
  #request = 0;
  #timer = 0;
  #disposed = false;

  constructor(onChange: () => void) {
    this.#onChange = onChange;
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
   */
  setSelection(selection: StudioSelection|null): void {
    if (this.#disposed) {
      return;
    }
    const previousDocument = this.#state.node?.documentKey ?? null;
    this.#selection = selection;
    this.#state = {...this.#state, node: selection?.node ?? null};
    const nextDocument = selection?.node.documentKey ?? null;
    if (previousDocument !== nextDocument) {
      this.#unobserve?.();
      this.#unobserve = selection?.observeDocument(() => this.#onDocumentChanged()) ?? null;
      this.#state = {...this.#state, resolution: {kind: 'idle'}};
    }
    this.#scheduleResolve(0);
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
    this.#state = {...this.#state, source, scope: 'node', selectorText};
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
    this.#state = {...this.#state, selectorText: selector, scope: 'node'};
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
    this.#state = {...this.#state, scope};
    this.#onChange();
  }

  setSelectorText(text: string): void {
    if (this.#disposed || text === this.#state.selectorText) {
      return;
    }
    this.#state = {...this.#state, selectorText: text};
    this.#scheduleResolve(SELECTOR_RESOLVE_DELAY);
    this.#onChange();
  }

  /** Resolves the current selector now, skipping the typing delay. */
  async resolveNow(): Promise<void> {
    window.clearTimeout(this.#timer);
    await this.#resolve();
  }

  dispose(): void {
    this.#disposed = true;
    window.clearTimeout(this.#timer);
    this.#request++;
    this.#unobserve?.();
    this.#unobserve = null;
    this.#selection = null;
  }

  #onDocumentChanged(): void {
    if (this.#state.source !== 'selector' || this.#state.resolution.kind === 'idle') {
      return;
    }
    // The previous matches may no longer hold, and neither may a query still in
    // flight. Show the target as pending and query again once mutations settle.
    this.#request++;
    this.#state = {...this.#state, resolution: {kind: 'idle'}};
    this.#scheduleResolve(DOM_CHANGE_RESOLVE_DELAY);
    this.#onChange();
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
    if (request !== this.#request || this.#disposed) {
      return;
    }
    // The selection may have moved to another document while the query ran.
    if (this.#state.node?.documentKey !== documentKey) {
      return;
    }
    this.#state = {
      ...this.#state,
      resolution: 'error' in result ? {kind: 'invalid', selector, documentKey, message: result.error} :
                                      {kind: 'resolved', selector, documentKey, nodeIds: result.nodeIds},
    };
    this.#onChange();
    // The text may have changed while the query ran.
    this.#scheduleResolve(SELECTOR_RESOLVE_DELAY);
  }
}
