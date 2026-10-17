// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../../ui/kit/kit.js';

import * as i18n from '../../core/i18n/i18n.js';
import * as Buttons from '../../ui/components/buttons/buttons.js';
import {html, nothing, type TemplateResult} from '../../ui/lit/lit.js';

import type {PreviewFailure, PreviewPropertyEffect, PreviewScope} from './StyleStudioPreview.js';
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
   * @description Status when the selected element left the page.
   */
  detachedNode: 'The selected element is no longer in the page. It was removed, replaced, or moved. Select it again.',
  /**
   * @description Status when the page or frame of the selected element navigated or was closed.
   */
  detachedDocument:
      'The page or frame of the selected element navigated or was closed. Select an element in the current page.',
  /**
   * @description Notice when Studio's target changed because the previous target left the page. PH1 is the previous target.
   * @example {div#card} PH1
   */
  previousDetached: 'The previous target, {PH1}, is no longer in the page.',
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
  /**
   * @description Section heading for the manual CSS preview editor.
   */
  preview: 'Preview',
  /**
   * @description Label of the text area where the user writes CSS declarations to preview.
   */
  cssDeclarations: 'CSS declarations',
  /**
   * @description Placeholder of the CSS declarations text area.
   */
  cssPlaceholder: 'color: rebeccapurple;\npadding: 8px;',
  /**
   * @description Hint under the CSS declarations text area.
   */
  cssHint: 'Declarations only. The page does not change until you apply. Ctrl+Enter or Cmd+Enter applies.',
  /**
   * @description Button that applies the draft CSS to the page as a temporary preview.
   */
  applyPreview: 'Apply preview',
  /**
   * @description Button that replaces the applied preview with the edited draft CSS.
   */
  updatePreview: 'Update preview',
  /**
   * @description Button that removes the preview from the page.
   */
  revert: 'Revert',
  /**
   * @description Hint when no valid target is chosen, so nothing can be previewed.
   */
  chooseTarget: 'Choose a valid target above to preview CSS.',
  /**
   * @description Status while a preview is being applied.
   */
  applying: 'Applying the preview…',
  /**
   * @description Status while a preview is being reverted.
   */
  reverting: 'Reverting the preview…',
  /**
   * @description Status when no preview is applied and the draft is empty.
   */
  noPreview: 'No preview applied.',
  /**
   * @description Status when no preview is applied and there is draft CSS.
   */
  draftNotApplied: 'Draft not applied yet. The page is unchanged.',
  /**
   * @description Status when the applied preview differs from the edited draft.
   */
  draftChanged: 'Draft changes not applied. The page still shows the applied preview.',
  /**
   * @description Status after a preview was applied and all of its declarations take effect.
   */
  appliedAllActive: 'Preview applied. Every declaration takes effect on the selected element.',
  /**
   * @description Status after a preview was updated and all of its declarations take effect.
   */
  updatedAllActive: 'Preview updated. Every declaration takes effect on the selected element.',
  /**
   * @description Status after a preview was applied and some declarations lose in the cascade. PH1 is the number overridden, PH2 the total.
   * @example {1} PH1
   * @example {3} PH2
   */
  appliedOverridden:
      'Preview applied, but {PH1} of {PH2} declarations are overridden by the cascade on the selected element.',
  /**
   * @description Status after a preview was updated and some declarations lose in the cascade. PH1 is the number overridden, PH2 the total.
   * @example {1} PH1
   * @example {3} PH2
   */
  updatedOverridden:
      'Preview updated, but {PH1} of {PH2} declarations are overridden by the cascade on the selected element.',
  /**
   * @description Status after a preview was applied whose effect could not be measured on the selected element.
   */
  appliedUnmeasured: 'Preview applied. Its effect on the selected element could not be measured.',
  /**
   * @description Status after a preview was updated whose effect could not be measured on the selected element.
   */
  updatedUnmeasured: 'Preview updated. Its effect on the selected element could not be measured.',
  /**
   * @description Status after the preview was reverted.
   */
  reverted: 'Preview reverted. The page’s original styles are back.',
  /**
   * @description Status when the draft CSS is not valid CSS declarations.
   */
  invalidCss: 'Invalid CSS. Enter declarations such as color: red. Nothing was applied, and your draft is kept.',
  /**
   * @description Status when the target changed and the preview was removed.
   */
  targetChanged:
      'The target or scope changed, so the preview was removed. Your draft is kept; apply it again to preview the new target.',
  /**
   * @description Status when the previewed element left the page and the preview was removed.
   */
  targetDetached:
      'The previewed element left the page (removed, replaced, or moved), or stopped matching its selector, so the preview was removed.',
  /**
   * @description Status when the page or frame navigated and the preview was removed.
   */
  navigated: 'The page or frame navigated, so the preview was removed with the old document.',
  /**
   * @description Status when Studio's preview stylesheet was changed by something else.
   */
  conflict: 'Studio’s preview stylesheet was changed elsewhere, so Studio will not write over it. Revert to remove it.',
  /**
   * @description Status when applying a preview failed. PH1 is the reason.
   * @example {The target is no longer in the page.} PH1
   */
  applyFailed: 'The preview was not applied. {PH1} Your draft is kept.',
  /**
   * @description Status when reverting a preview failed.
   */
  revertFailed: 'The preview could not be removed. Try Revert again, or reload the page.',
  /**
   * @description Failure reason: the target left the page.
   */
  failDetached: 'The target is no longer in the page.',
  /**
   * @description Failure reason: the target can't be previewed.
   */
  failUnsupported: 'This target cannot be previewed.',
  /**
   * @description Failure reason: the selector stopped matching the target.
   */
  failMismatch: 'The selector does not match the target any more.',
  /**
   * @description Failure reason: no selector could match exactly the target.
   */
  failNotUnique: 'Studio could not address exactly this element.',
  /**
   * @description Failure reason: the stylesheet failed the ownership check.
   */
  failOwnership: 'Studio’s stylesheet failed its ownership check.',
  /**
   * @description Failure reason: the stylesheet write failed.
   */
  failWrite: 'The stylesheet could not be written.',
  /**
   * @description Failure reason: the ownership record could not be saved.
   */
  failRecord: 'Studio could not save the record it needs to clean up after a crash.',
  /**
   * @description Failure reason: the target changed while the preview was being applied.
   */
  failSuperseded: 'The target changed while the preview was being applied.',
  /**
   * @description Failure reason: Studio's stylesheet was modified elsewhere.
   */
  failExternal: 'Studio’s stylesheet was changed elsewhere.',
  /**
   * @description Heading over the CSS currently applied to the page.
   */
  appliedHeading: 'Applied to the page',
  /**
   * @description Scope of the applied preview: only the selected element.
   */
  appliedToElement: 'Selected element only',
  /**
   * @description Scope of the applied preview: every element the selector matches.
   */
  appliedToMatches: 'Every element matching the selector',
  /**
   * @description Notice listing declarations the browser rejected. PH1 is the list.
   * @example {colr: red} PH1
   */
  ignoredDeclarations: 'Ignored as invalid: {PH1}',
  /**
   * @description Accessible name of the list of declaration effects.
   */
  effectsLabel: 'Effect on the selected element',
  /**
   * @description Effect of a declaration that wins in the cascade. PH1 is the computed value.
   * @example {rgb(255, 0, 0)} PH1
   */
  effectActive: 'takes effect · computed {PH1}',
  /**
   * @description Effect of a declaration that loses in the cascade.
   */
  effectOverridden: 'overridden by the cascade',
  /**
   * @description Effect of a declaration that could not be determined.
   */
  effectUnknown: 'effect unknown',
  /**
   * @description Message when a preview from a previous DevTools session is still applied to the page.
   */
  orphanFound: 'A Style Studio preview from an earlier DevTools session is still applied to this page.',
  /**
   * @description Button that removes a preview left by an earlier DevTools session.
   */
  remove: 'Remove',
  /**
   * @description Button that leaves a preview left by an earlier DevTools session in place.
   */
  keep: 'Keep',
  /**
   * @description Message when a stylesheet Studio created earlier was changed by something else.
   */
  orphanChanged:
      'A stylesheet Style Studio created earlier was changed by something else, so Studio will not touch it. Reloading the page clears it.',
  /**
   * @description Button that forgets a changed stylesheet without touching it.
   */
  forget: 'Forget',
  /**
   * @description Result after an earlier session's preview was removed.
   */
  orphanRemoved: 'The earlier preview was removed.',
  /**
   * @description Result when an earlier session's preview could not be removed.
   */
  orphanRemoveFailed: 'The earlier preview could not be removed safely, so it was left in place.',
} as const;
const str_ = i18n.i18n.registerUIStrings('panels/style_intelligence/StyleStudioView.ts', UIStrings);
const i18nString = i18n.i18n.getLocalizedString.bind(undefined, str_);

/** The most recent thing that happened to the preview, for the status line. */
export type PreviewEvent = {
  kind: 'none'|'applied'|'updated'|'reverted'|'invalid'|'target-changed'|'target-detached'|'navigated'|'revert-failed',
}|{
  kind: 'failed',
  reason: PreviewFailure,
};

export interface StudioPreviewInput {
  /** A valid target exists, so a preview can be applied to it. */
  canApply: boolean;
  draft: string;
  /** What is applied to the page now, or null. */
  applied: {declarations: string, invalid: string[], scope: PreviewScope}|null;
  /** Studio's sheet was changed elsewhere; only a revert is allowed. */
  conflict: boolean;
  /** The draft differs from what is applied (or, with nothing applied, is not empty). */
  dirty: boolean;
  busy: 'apply'|'revert'|null;
  event: PreviewEvent;
  /** The applied declarations' effect on the selected element, or null when unmeasured. */
  effects: PreviewPropertyEffect[]|null;
}

export interface StudioRecoveryItem {
  id: string;
  kind: 'orphan'|'changed';
  /** The sheet's rule text, without Studio's label. */
  css: string;
}

export interface StudioRecoveryInput {
  items: StudioRecoveryItem[];
  busy: boolean;
  /** The outcome of the last Remove, if any. */
  result: 'removed'|'failed'|null;
}

export interface StudioViewInput {
  /** The selected node as Studio names it, including non-element nodes; null when nothing is selected. */
  nodeLabel: string|null;
  /** The label of a previous target that left the page, announced once Studio follows a new selection. */
  notice: string|null;
  /** Whether the DevTools element picker is available, and whether it is active. */
  canPick: boolean;
  picking: boolean;
  source: StudioSource;
  scope: StudioScope;
  selectorText: string;
  target: StudioTarget;
  preview: StudioPreviewInput;
  recovery: StudioRecoveryInput;
}

export interface StudioViewOutput {
  setSource: (source: StudioSource) => void;
  setScope: (scope: StudioScope) => void;
  setSelectorText: (text: string) => void;
  commitSelector: () => void;
  highlightTarget: () => void;
  hideHighlight: () => void;
  togglePick: () => void;
  setDraft: (text: string) => void;
  applyPreview: () => void;
  revertPreview: () => void;
  removeRecovered: (id: string) => void;
  keepRecovered: (id: string) => void;
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
    case 'detached':
      return {
        text: target.reason === 'node' ? i18nString(UIStrings.detachedNode) : i18nString(UIStrings.detachedDocument),
        tone: 'error',
        highlightable: false,
      };
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
      ${renderRecovery(input.recovery, output)}
      ${renderStudio(input, output)}
      ${renderPreview(input.preview, output)}
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
      ${input.notice ? html`
        <p class="studio-notice" role="status">${i18nString(UIStrings.previousDetached, {PH1: input.notice})}</p>` :
        nothing}
      <p class="studio-status ${status.tone}" role="status" aria-live="polite"
          @mouseenter=${status.highlightable ? () => output.highlightTarget() : nothing}
          @mouseleave=${status.highlightable ? () => output.hideHighlight() : nothing}>
        ${status.text}
      </p>
    </section>`;
  // clang-format on
}

function failureText(reason: PreviewFailure): string {
  switch (reason) {
    case 'target-detached':
      return i18nString(UIStrings.failDetached);
    case 'target-unsupported':
      return i18nString(UIStrings.failUnsupported);
    case 'selector-mismatch':
      return i18nString(UIStrings.failMismatch);
    case 'not-unique':
      return i18nString(UIStrings.failNotUnique);
    case 'ownership-unsafe':
      return i18nString(UIStrings.failOwnership);
    case 'record-failed':
      return i18nString(UIStrings.failRecord);
    case 'superseded':
    case 'disposed':
      return i18nString(UIStrings.failSuperseded);
    case 'external-modification':
      return i18nString(UIStrings.failExternal);
    case 'invalid-css':
      return i18nString(UIStrings.invalidCss);
    case 'write-failed':
      return i18nString(UIStrings.failWrite);
  }
}

export interface PreviewStatus {
  text: string;
  tone: 'idle'|'pending'|'applied'|'warning'|'error';
}

/**
 * The preview status line. An applied preview is described by its measured
 * effect on the selected element, never by the stylesheet write alone.
 */
export function previewStatus(input: StudioPreviewInput): PreviewStatus {
  if (input.busy === 'apply') {
    return {text: i18nString(UIStrings.applying), tone: 'pending'};
  }
  if (input.busy === 'revert') {
    return {text: i18nString(UIStrings.reverting), tone: 'pending'};
  }
  if (input.conflict) {
    return {text: i18nString(UIStrings.conflict), tone: 'error'};
  }
  const event = input.event;
  switch (event.kind) {
    case 'invalid':
      return {text: i18nString(UIStrings.invalidCss), tone: 'error'};
    case 'failed':
      return {
        text: event.reason === 'invalid-css' ? i18nString(UIStrings.invalidCss) :
                                               i18nString(UIStrings.applyFailed, {PH1: failureText(event.reason)}),
        tone: 'error',
      };
    case 'revert-failed':
      return {text: i18nString(UIStrings.revertFailed), tone: 'error'};
    case 'target-changed':
      return {text: i18nString(UIStrings.targetChanged), tone: 'warning'};
    case 'target-detached':
      return {text: i18nString(UIStrings.targetDetached), tone: 'warning'};
    case 'navigated':
      return {text: i18nString(UIStrings.navigated), tone: 'warning'};
    default:
      break;
  }
  if (input.applied) {
    if (input.dirty) {
      return {text: i18nString(UIStrings.draftChanged), tone: 'pending'};
    }
    const updated = event.kind === 'updated';
    if (!input.effects) {
      return {
        text: updated ? i18nString(UIStrings.updatedUnmeasured) : i18nString(UIStrings.appliedUnmeasured),
        tone: 'applied',
      };
    }
    const overridden = input.effects.filter(effect => effect.state !== 'active').length;
    if (overridden) {
      const counts = {PH1: overridden, PH2: input.effects.length};
      return {
        text: updated ? i18nString(UIStrings.updatedOverridden, counts) : i18nString(UIStrings.appliedOverridden, counts),
        tone: 'warning',
      };
    }
    return {
      text: updated ? i18nString(UIStrings.updatedAllActive) : i18nString(UIStrings.appliedAllActive),
      tone: 'applied',
    };
  }
  if (event.kind === 'reverted') {
    return {text: i18nString(UIStrings.reverted), tone: 'idle'};
  }
  return input.dirty ? {text: i18nString(UIStrings.draftNotApplied), tone: 'pending'} :
                       {text: i18nString(UIStrings.noPreview), tone: 'idle'};
}

/** Whether Apply (or Update) may be pressed now. */
export function canSubmitPreview(input: StudioPreviewInput): boolean {
  return input.canApply && input.busy === null && !input.conflict && input.draft.trim() !== '' &&
      (input.applied === null || input.dirty);
}

function effectText(effect: PreviewPropertyEffect): string {
  switch (effect.state) {
    case 'active':
      return i18nString(UIStrings.effectActive, {PH1: effect.computed ?? '—'});
    case 'overridden':
      return i18nString(UIStrings.effectOverridden);
    case 'unknown':
      return i18nString(UIStrings.effectUnknown);
  }
}

function renderPreview(input: StudioPreviewInput, output: StudioViewOutput): TemplateResult {
  const status = previewStatus(input);
  const submittable = canSubmitPreview(input);
  const submit = (): void => {
    if (canSubmitPreview(input)) {
      output.applyPreview();
    }
  };
  // clang-format off
  return html`
    <section class="studio studio-preview">
      <h2>${i18nString(UIStrings.preview)}</h2>
      <div class="studio-field">
        <label class="studio-field-label" for="studio-css">${i18nString(UIStrings.cssDeclarations)}</label>
        <textarea id="studio-css" class="studio-css" rows="5" spellcheck="false" autocomplete="off"
            aria-describedby="studio-css-hint"
            placeholder=${i18nString(UIStrings.cssPlaceholder)}
            .value=${input.draft}
            @input=${(event: Event) => output.setDraft((event.target as HTMLTextAreaElement).value)}
            @keydown=${(event: KeyboardEvent) => {
              if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
                event.preventDefault();
                submit();
              }
            }}></textarea>
        <p class="studio-hint" id="studio-css-hint">${i18nString(UIStrings.cssHint)}</p>
      </div>
      <div class="studio-actions">
        <devtools-button class="studio-apply" .variant=${Buttons.Button.Variant.PRIMARY}
            .disabled=${!submittable} @click=${submit}>
          ${input.applied ? i18nString(UIStrings.updatePreview) : i18nString(UIStrings.applyPreview)}
        </devtools-button>
        <devtools-button class="studio-revert" .variant=${Buttons.Button.Variant.OUTLINED}
            .disabled=${!input.applied || input.busy !== null}
            @click=${() => output.revertPreview()}>${i18nString(UIStrings.revert)}</devtools-button>
      </div>
      ${!input.canApply && !input.applied ? html`
        <p class="studio-hint">${i18nString(UIStrings.chooseTarget)}</p>` : nothing}
      <p class="studio-status studio-preview-status ${status.tone}" role="status" aria-live="polite">${status.text}</p>
      ${input.applied ? html`
        <div class="studio-applied">
          <h3>${i18nString(UIStrings.appliedHeading)}
            <span class="studio-applied-scope">· ${input.applied.scope === 'node' ?
                i18nString(UIStrings.appliedToElement) : i18nString(UIStrings.appliedToMatches)}</span></h3>
          <code class="studio-applied-css">${input.applied.declarations}</code>
          ${input.applied.invalid.length ? html`
            <p class="studio-hint studio-ignored">
              ${i18nString(UIStrings.ignoredDeclarations, {PH1: input.applied.invalid.join('; ')})}</p>` : nothing}
          ${input.effects?.length ? html`
            <ul class="studio-effects" aria-label=${i18nString(UIStrings.effectsLabel)}>
              ${input.effects.map(effect => html`
                <li class=${effect.state}><code>${effect.name}: ${effect.value}</code> — ${effectText(effect)}</li>`)}
            </ul>` : nothing}
        </div>` : nothing}
    </section>`;
  // clang-format on
}

function renderRecovery(input: StudioRecoveryInput, output: StudioViewOutput): TemplateResult|typeof nothing {
  if (!input.items.length && !input.result) {
    return nothing;
  }
  // clang-format off
  return html`
    <section class="studio studio-recovery" aria-label=${i18nString(UIStrings.orphanFound)}>
      ${input.items.map(item => html`
        <div class="studio-recovery-item ${item.kind}" data-sheet=${item.id}>
          <p role="status">${item.kind === 'orphan' ? i18nString(UIStrings.orphanFound) :
                                                      i18nString(UIStrings.orphanChanged)}</p>
          ${item.css ? html`<code class="studio-applied-css">${item.css}</code>` : nothing}
          <div class="studio-actions">
            ${item.kind === 'orphan' ? html`
              <devtools-button class="studio-recovery-remove" .variant=${Buttons.Button.Variant.PRIMARY}
                  .disabled=${input.busy} @click=${() => output.removeRecovered(item.id)}>
                ${i18nString(UIStrings.remove)}</devtools-button>
              <devtools-button class="studio-recovery-keep" .variant=${Buttons.Button.Variant.OUTLINED}
                  .disabled=${input.busy} @click=${() => output.keepRecovered(item.id)}>
                ${i18nString(UIStrings.keep)}</devtools-button>` : html`
              <devtools-button class="studio-recovery-forget" .variant=${Buttons.Button.Variant.OUTLINED}
                  .disabled=${input.busy} @click=${() => output.keepRecovered(item.id)}>
                ${i18nString(UIStrings.forget)}</devtools-button>`}
          </div>
        </div>`)}
      ${input.result ? html`
        <p class="studio-status ${input.result === 'removed' ? 'idle' : 'error'}" role="status" aria-live="polite">
          ${input.result === 'removed' ? i18nString(UIStrings.orphanRemoved) :
                                         i18nString(UIStrings.orphanRemoveFailed)}</p>` : nothing}
    </section>`;
  // clang-format on
}
