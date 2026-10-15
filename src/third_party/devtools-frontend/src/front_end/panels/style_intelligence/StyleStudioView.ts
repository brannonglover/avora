// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../../ui/kit/kit.js';

import * as i18n from '../../core/i18n/i18n.js';
import {html, nothing, type TemplateResult} from '../../ui/lit/lit.js';

import type {StudioScope, StudioSource, StudioTarget, UnsupportedNodeReason} from './StyleStudioTarget.js';

const UIStrings = {
  /**
   * @description Heading of the Studio tab, naming what decides its target.
   */
  followsSelection: 'Follows the Elements selection',
  /**
   * @description Heading of the Studio tab when no element is selected yet.
   */
  noElementSelected: 'No element selected',
  /**
   * @description Section heading for choosing the elements Style Studio targets.
   */
  targeting: 'Targeting',
  /**
   * @description Accessible name for the choice of what Style Studio targets.
   */
  targetSource: 'Target',
  /**
   * @description Option that targets the element selected in the Elements panel.
   */
  selectedElement: 'Selected element',
  /**
   * @description Option that targets elements by CSS selector.
   */
  selector: 'Selector',
  /**
   * @description Accessible name and placeholder of the CSS selector input.
   */
  cssSelector: 'CSS selector',
  /**
   * @description Tooltip of the button that picks an element on the page while targeting the selected element.
   */
  pickElement: 'Pick an element on the page',
  /**
   * @description Tooltip of the button that picks an element on the page to fill the selector input.
   */
  pickForSelector: 'Pick an element on the page to use its selector',
  /**
   * @description Hint under the selector input.
   */
  selectorHint: 'Type a selector such as .card or nav a, or pick an element on the page.',
  /**
   * @description Hint shown while the page element picker is active.
   */
  picking: 'Click an element on the page. Press Esc to cancel.',
  /**
   * @description Accessible name for the choice of how many elements a selector targets.
   */
  scope: 'Scope',
  /**
   * @description Scope option that targets only the selected element.
   */
  onlySelected: 'Selected only',
  /**
   * @description Scope option that targets every element the selector matches.
   */
  allMatches: 'All matches',
  /**
   * @description Status when no node is selected.
   */
  noSelection: 'Pick an element on the page, or select one in the Elements tree.',
  /**
   * @description Status when the selected node is not an element.
   */
  notElement: 'This node is not an element. Only element nodes can be targeted.',
  /**
   * @description Status when the selected node is a pseudo-element.
   */
  pseudoElement: 'Pseudo-elements cannot be targeted as nodes. Select the element that generates it.',
  /**
   * @description Status when the selected node is inside a shadow tree.
   */
  shadowTree: 'Elements inside a shadow root are not supported yet. Style Studio targets the document, not shadow trees.',
  /**
   * @description Status when the selected node has no document to resolve selectors in.
   */
  noDocument: 'This node has no document, so it cannot be targeted.',
  /**
   * @description Status when the selector input is empty.
   */
  enterSelector: 'Enter a CSS selector.',
  /**
   * @description Status while a selector is being matched against the document.
   */
  matching: 'Matching…',
  /**
   * @description Status for a selector the browser rejected. PH1 is the browser's error message.
   * @example {'a[' is not a valid selector.} PH1
   */
  invalidSelector: 'Invalid selector: {PH1}',
  /**
   * @description Status when a valid selector matches nothing.
   */
  noMatches: 'No elements in this document match this selector.',
  /**
   * @description Status when the scope is one element and the selected element is not one of the matches. PH1 is the number of matches.
   */
  notMatched:
      '{PH1, plural, =1 {The selector matches 1 element, but not the selected one.} other {The selector matches # elements, but not the selected one.}}',
  /**
   * @description Status when the target is the selected element.
   */
  thisElementOnly: 'This element only',
  /**
   * @description Status when the target is the selected element, reached through a selector. PH1 is the number of matches.
   */
  selectedOfMatches:
      '{PH1, plural, =1 {This element only · the selector matches 1 element} other {This element only · the selector matches # elements}}',
  /**
   * @description Status when every match is targeted. PH1 is the number of matches.
   */
  allMatchesTargeted: '{PH1, plural, =1 {1 matching element} other {# matching elements}}',
  /**
   * @description Suffix when every match is targeted and the selected element is not among them.
   */
  selectedNotIncluded: 'the selected element is not one of them',
} as const;
const str_ = i18n.i18n.registerUIStrings('panels/style_intelligence/StyleStudioView.ts', UIStrings);
const i18nString = i18n.i18n.getLocalizedString.bind(undefined, str_);

export interface StudioViewInput {
  /** The selected node as Studio names it, including non-element nodes; null when nothing is selected. */
  nodeLabel: string|null;
  /** Whether the DevTools element picker is available, and whether it is active. */
  canPick: boolean;
  picking: boolean;
  source: StudioSource;
  scope: StudioScope;
  selectorText: string;
  target: StudioTarget;
}

export interface StudioViewOutput {
  setSource: (source: StudioSource) => void;
  setScope: (scope: StudioScope) => void;
  setSelectorText: (text: string) => void;
  commitSelector: () => void;
  highlightTarget: () => void;
  hideHighlight: () => void;
  togglePick: () => void;
}

function unsupportedText(reason: UnsupportedNodeReason): string {
  switch (reason) {
    case 'not-element':
      return i18nString(UIStrings.notElement);
    case 'pseudo-element':
      return i18nString(UIStrings.pseudoElement);
    case 'shadow-tree':
      return i18nString(UIStrings.shadowTree);
    case 'no-document':
      return i18nString(UIStrings.noDocument);
  }
}

interface StudioStatus {
  text: string;
  /** `target` names a usable target; anything else is a reason there is none. */
  tone: 'target'|'pending'|'unsupported'|'error';
  highlightable: boolean;
}

function studioStatus(target: StudioTarget): StudioStatus {
  switch (target.kind) {
    case 'none':
      return {text: i18nString(UIStrings.noSelection), tone: 'unsupported', highlightable: false};
    case 'unsupported':
      return {text: unsupportedText(target.reason), tone: 'unsupported', highlightable: false};
    case 'empty-selector':
      return {text: i18nString(UIStrings.enterSelector), tone: 'pending', highlightable: false};
    case 'pending':
      return {text: i18nString(UIStrings.matching), tone: 'pending', highlightable: false};
    case 'invalid-selector':
      return {
        text: i18nString(UIStrings.invalidSelector, {PH1: target.message}),
        tone: 'error',
        highlightable: false,
      };
    case 'no-matches':
      return {text: i18nString(UIStrings.noMatches), tone: 'unsupported', highlightable: false};
    case 'node-not-matched':
      return {
        text: i18nString(UIStrings.notMatched, {PH1: target.matchCount}),
        tone: 'unsupported',
        highlightable: false,
      };
    case 'node':
      return {
        text: target.matchCount === null ? i18nString(UIStrings.thisElementOnly) :
                                           i18nString(UIStrings.selectedOfMatches, {PH1: target.matchCount}),
        tone: 'target',
        highlightable: true,
      };
    case 'matches': {
      const count = i18nString(UIStrings.allMatchesTargeted, {PH1: target.nodeIds.length});
      return {
        text: target.includesSelected ? count : `${count} · ${i18nString(UIStrings.selectedNotIncluded)}`,
        tone: 'target',
        highlightable: true,
      };
    }
  }
}

/** The whole Studio tab: the element it follows, then targeting. */
export function renderStudioPanel(input: StudioViewInput, output: StudioViewOutput): TemplateResult {
  // clang-format off
  return html`
    <div class="style-intelligence studio-panel">
      <header>
        ${input.nodeLabel !== null ? html`
          <div class="identity" title=${input.nodeLabel}>${input.nodeLabel}</div>` : html`
          <div class="identity none">${i18nString(UIStrings.noElementSelected)}</div>`}
        <div class="size">${i18nString(UIStrings.followsSelection)}</div>
      </header>
      ${renderStudio(input, output)}
    </div>`;
  // clang-format on
}

function renderStudio(input: StudioViewInput, output: StudioViewOutput): TemplateResult {
  const status = studioStatus(input.target);
  const isSelector = input.source === 'selector';
  const pickTitle = isSelector ? i18nString(UIStrings.pickForSelector) : i18nString(UIStrings.pickElement);
  // clang-format off
  return html`
    <section class="studio">
      <h2>${i18nString(UIStrings.targeting)}</h2>
      <div class="studio-field">
        <span class="studio-field-label" aria-hidden="true">${i18nString(UIStrings.targetSource)}</span>
        <div class="studio-row">
          <div class="studio-choice" role="radiogroup" aria-label=${i18nString(UIStrings.targetSource)}>
            <label>
              <input type="radio" name="studio-source" .checked=${!isSelector}
                  @change=${() => output.setSource('selected-node')}>
              ${i18nString(UIStrings.selectedElement)}
            </label>
            <label>
              <input type="radio" name="studio-source" .checked=${isSelector}
                  @change=${() => output.setSource('selector')}>
              ${i18nString(UIStrings.selector)}
            </label>
          </div>
          ${input.canPick ? html`
            <button class="studio-pick" aria-pressed=${input.picking} title=${pickTitle}
                aria-label=${pickTitle} @click=${() => output.togglePick()}>
              <devtools-icon name="select-element"></devtools-icon>
            </button>` : nothing}
        </div>
        ${input.picking ? html`<p class="studio-hint" role="status">${i18nString(UIStrings.picking)}</p>` : nothing}
      </div>
      ${isSelector ? html`
        <div class="studio-field">
          <input class="studio-selector" type="text" spellcheck="false" autocomplete="off"
              aria-label=${i18nString(UIStrings.cssSelector)}
              placeholder=${i18nString(UIStrings.cssSelector)}
              .value=${input.selectorText}
              @input=${(event: Event) => output.setSelectorText((event.target as HTMLInputElement).value)}
              @keydown=${(event: KeyboardEvent) => {
                if (event.key === 'Enter') {
                  output.commitSelector();
                }
              }}>
          <p class="studio-hint">${i18nString(UIStrings.selectorHint)}</p>
        </div>
        <div class="studio-field">
          <span class="studio-field-label" aria-hidden="true">${i18nString(UIStrings.scope)}</span>
          <div class="studio-choice" role="radiogroup" aria-label=${i18nString(UIStrings.scope)}>
            <label>
              <input type="radio" name="studio-scope" .checked=${input.scope === 'node'}
                  @change=${() => output.setScope('node')}>
              ${i18nString(UIStrings.onlySelected)}
            </label>
            <label>
              <input type="radio" name="studio-scope" .checked=${input.scope === 'all-matches'}
                  @change=${() => output.setScope('all-matches')}>
              ${i18nString(UIStrings.allMatches)}
            </label>
          </div>
        </div>` : nothing}
      <p class="studio-status ${status.tone}" role="status" aria-live="polite"
          @mouseenter=${status.highlightable ? () => output.highlightTarget() : nothing}
          @mouseleave=${status.highlightable ? () => output.hideHighlight() : nothing}>
        ${status.text}
      </p>
    </section>`;
  // clang-format on
}
