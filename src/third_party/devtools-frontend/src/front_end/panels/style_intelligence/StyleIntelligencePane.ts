// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as Common from '../../core/common/common.js';
import * as i18n from '../../core/i18n/i18n.js';
import * as SDK from '../../core/sdk/sdk.js';
import * as Bindings from '../../models/bindings/bindings.js';
import * as ComputedStyle from '../../models/computed_style/computed_style.js';
import * as UI from '../../ui/legacy/legacy.js';
import {html, nothing, render, type TemplateResult} from '../../ui/lit/lit.js';
import * as VisualLogging from '../../ui/visual_logging/visual_logging.js';

import {type Diagnostic} from './Diagnostics.js';
import {
  buildElementChain,
  type ChainRung,
  type ChainStacking,
  chainRungLabel,
} from './ElementChain.js';
import {buildElementFacts, type ElementFact, factCount, type FactsLayering} from './ElementFacts.js';
import {formatLayoutStatement, type StyleIntelligenceSummary} from './ElementSummary.js';
import {type LayoutExplanation} from './LayoutExplanation.js';
import {positionNeedsLayoutRelationships, type LayoutRelationships} from './LayoutRelationships.js';
import {loadLayoutRelationships} from './LayoutRelationshipsLoader.js';
import {insetsFromExplanation} from './LayoutRelationshipsPresentation.js';
import {buildPlate, type PlateView} from './Plate.js';
import {type SpacingExplanation} from './SpacingExplanation.js';
import {explainStackingBoundary, type StackingBoundary} from './StackingBoundary.js';
import {isDocumentRootContext, needsStackingSnapshot, type StackingChain} from './StackingChain.js';
import {captureStackingSnapshot, loadStackingChain} from './StackingChainLoader.js';
import {applySnapshot, type SnapshotNodeRecord} from './StackingSnapshot.js';
import styleIntelligencePaneStyles from './styleIntelligencePane.css.js';
import {identityForNode, type StyleIntelligenceLoad, StyleIntelligenceModel} from './StyleIntelligenceModel.js';
import {bindStudioToSelection, observeInspectPicks, selectionForNode} from './StyleStudioDom.js';
import {StudioPickTracker} from './StyleStudioPicker.js';
import {StyleStudioTargeting} from './StyleStudioTargeting.js';
import {renderStudioPanel, type StudioViewInput, type StudioViewOutput} from './StyleStudioView.js';

const UIStrings = {
  /**
   * @description Shown in Style Intelligence when no element is selected.
   */
  selectAnElement: 'Select an element.',
  /**
   * @description Section heading for the ancestors that decide the selected element's layout.
   */
  decidedForYou: 'Decided for you',
  /**
   * @description Section heading for the properties the selected element decides itself.
   */
  yours: 'Yours',
  /**
   * @description Section heading for CSS custom properties used by the selected element.
   */
  tokens: 'Tokens',
  /**
   * @description Button that selects a layout ancestor in the Elements panel.
   */
  inspect: 'Inspect',
  /**
   * @description Button that reveals an author declaration in Sources.
   */
  source: 'Source',
  /**
   * @description Shown when styles for the selected element could not be read.
   */
  stylesFailed: 'Styles for this element could not be loaded.',
  /**
   * @description Shown when the selected node has no CSS model.
   */
  stylesUnavailable: 'Styles are not available for this element.',
  /**
   * @description Shown while the ancestor chain is still being read.
   */
  readingAncestors: 'Reading ancestors…',
  /**
   * @description Shown when the ancestor stacking context could not be read.
   */
  ancestorsFailed: 'Ancestor stacking context could not be loaded.',
  /**
   * @description Caption on the element diagram stating that it is not drawn to scale.
   */
  notToScale: 'not to scale',
  /**
   * @description Legend entry for the element's border box on the diagram.
   */
  borderBox: 'border box',
  /**
   * @description Legend entry for the element's margin on the diagram.
   */
  margin: 'margin',
  /**
   * @description Accessible name for the element diagram.
   */
  diagramLabel: 'Element nesting diagram',
  /**
   * @description Accessible name for the tabs at the top of Style Intelligence.
   */
  intelligenceViews: 'Intelligence views',
  /**
   * @description Tab that explains the layout of the selected element.
   */
  explain: 'Explain',
  /**
   * @description Tab for Style Studio, where styling changes are targeted.
   */
  studio: 'Studio',
} as const;
const str_ = i18n.i18n.registerUIStrings('panels/style_intelligence/StyleIntelligencePane.ts', UIStrings);
const i18nString = i18n.i18n.getLocalizedString.bind(undefined, str_);

type PanePhase = 'empty'|'loading'|'ready'|'failed'|'unavailable';
/** Explain is the read-only explanation; Studio is the styling tool. */
export type PaneTab = 'explain'|'studio';
const PANE_TABS: readonly PaneTab[] = ['explain', 'studio'];
type LayeringPhase = 'idle'|'loading'|'ready'|'failed';

/**
 * The existing Elements inspect mode. Studio's picker drives this action rather
 * than running an inspect mode of its own.
 */
const PICK_ACTION_ID = 'elements.toggle-element-search';

/** Delay before the ancestor walk starts, so fast reselection does not thrash it. */
const LAYERING_START_DELAY = 120;

interface PaneToken {
  name: string;
  value: string|null;
  use: string;
  title: string;
}

interface ViewInput {
  phase: PanePhase;
  identity: string|null;
  subtitle: string|null;
  plate: PlateView|null;
  rungs: ChainRung[];
  facts: ElementFact[];
  tokens: PaneToken[];
  issues: number;
  layeringPhase: LayeringPhase;
  hasSummary: boolean;
  tab: PaneTab;
  studio: StudioViewInput;
}

interface ViewOutput {
  highlightNode: (nodeId: number) => void;
  hideHighlight: () => void;
  inspectNode: (nodeId: number) => void;
  revealDeclaration: (declarationId: number) => void;
  selectTab: (tab: PaneTab) => void;
  studio: StudioViewOutput;
}

type View = (input: ViewInput, output: ViewOutput, target: HTMLElement) => void;

export const DEFAULT_VIEW: View = (input, output, target) => {
  // clang-format off
  render(html`
    ${renderTabs(input.tab, output)}
    <div class="tab-panel" role="tabpanel" id="tab-panel-${input.tab}" aria-labelledby="tab-${input.tab}">
      ${input.tab === 'studio' ? renderStudioTab(input, output) : renderExplain(input, output)}
    </div>`, target);
  // clang-format on
};

function tabLabel(tab: PaneTab): string {
  return tab === 'studio' ? i18nString(UIStrings.studio) : i18nString(UIStrings.explain);
}

/** A WAI-ARIA tablist: arrow keys, Home, and End move between tabs and select them. */
function renderTabs(selected: PaneTab, output: ViewOutput): TemplateResult {
  const onKeyDown = (event: KeyboardEvent): void => {
    const index = PANE_TABS.indexOf(selected);
    let next: PaneTab|null = null;
    if (event.key === 'ArrowRight') {
      next = PANE_TABS[(index + 1) % PANE_TABS.length];
    } else if (event.key === 'ArrowLeft') {
      next = PANE_TABS[(index - 1 + PANE_TABS.length) % PANE_TABS.length];
    } else if (event.key === 'Home') {
      next = PANE_TABS[0];
    } else if (event.key === 'End') {
      next = PANE_TABS[PANE_TABS.length - 1];
    }
    if (!next) {
      return;
    }
    event.preventDefault();
    output.selectTab(next);
    const list = (event.currentTarget as HTMLElement).parentElement;
    list?.querySelector<HTMLElement>(`#tab-${next}`)?.focus();
  };
  // clang-format off
  return html`
    <div class="tabs" role="tablist" aria-label=${i18nString(UIStrings.intelligenceViews)}>
      ${PANE_TABS.map(tab => html`
        <button class="tab" role="tab" id="tab-${tab}" aria-controls="tab-panel-${tab}"
            aria-selected=${tab === selected} tabindex=${tab === selected ? 0 : -1}
            @click=${() => output.selectTab(tab)} @keydown=${onKeyDown}>${tabLabel(tab)}</button>`)}
    </div>`;
  // clang-format on
}

function renderStudioTab(input: ViewInput, output: ViewOutput): TemplateResult {
  return renderStudioPanel(input.studio, output.studio);
}

function renderExplain(input: ViewInput, output: ViewOutput): TemplateResult {
  if (input.phase === 'empty' || !input.identity) {
    return html`<div class="style-intelligence empty" jslog=${VisualLogging.pane('style-intelligence')}>
      ${i18nString(UIStrings.selectAnElement)}
    </div>`;
  }
  if (!input.hasSummary) {
    const status = input.phase === 'failed' ? i18nString(UIStrings.stylesFailed) :
        input.phase === 'unavailable'       ? i18nString(UIStrings.stylesUnavailable) :
                                              nothing;
    return html`<div class="style-intelligence" jslog=${VisualLogging.pane('style-intelligence')}
        aria-busy=${input.phase === 'loading'}>
      <div class="identity">${input.identity}</div>
      ${status ? html`<p class="status">${status}</p>` : nothing}
    </div>`;
  }

  // clang-format off
  return html`
    <div class="style-intelligence" jslog=${VisualLogging.pane('style-intelligence').track({resize: true})}
        aria-busy=${input.phase === 'loading'}>
      <header>
        <div class="identity" title=${input.identity}>${input.identity}</div>
        ${input.subtitle ? html`<div class="size">${input.subtitle}</div>` : nothing}
      </header>
      ${input.plate ? renderPlate(input.plate, output) : nothing}
      ${renderChain(input, output)}
      ${input.facts.length ? html`
        <section>
          <h2>${i18nString(UIStrings.yours)}${input.issues ?
            html`<span class="count"> · ${issueText(input.issues)}</span>` : nothing}</h2>
          <div class="facts">
            ${input.facts.map(fact => renderFact(fact, output))}
          </div>
        </section>` : nothing}
      ${input.tokens.length ? html`
        <section>
          <h2>${i18nString(UIStrings.tokens)}</h2>
          <div class="tokens">
            ${input.tokens.map(token => html`
              <div class="cell token-name">${token.name}</div>
              <div class="cell token-resolved">${token.value ?? ''}</div>
              <div class="cell token-use" title=${token.title}>${token.use}</div>`)}
          </div>
        </section>` : nothing}
    </div>`;
  // clang-format on
}

function issueText(count: number): string {
  return count === 1 ? '1 issue' : `${count} issues`;
}

/**
 * The diagram is the affordance: frames and the element box highlight on hover
 * and on focus, which is what lets every row below drop its inline links.
 */
function renderPlate(plate: PlateView, output: ViewOutput): TemplateResult {
  const gap = plate.gapAbove;
  const neighborId = gap?.neighborNodeId ?? null;
  const neighborLabel = gap?.neighborLabel ?? null;
  // clang-format off
  return html`
    <section class="plate-section">
      <div class="plate" role="group" aria-label=${i18nString(UIStrings.diagramLabel)}>
        ${plate.frames.map((frame, index) => renderFrame(frame, index, output))}
        ${gap?.sibling && neighborLabel ? html`
          <div class="plate-sibling">
            ${neighborId !== null ? html`
              <button title=${neighborLabel}
                  @mouseenter=${() => output.highlightNode(neighborId)}
                  @mouseleave=${() => output.hideHighlight()}
                  @focus=${() => output.highlightNode(neighborId)}
                  @blur=${() => output.hideHighlight()}
                  @click=${() => output.inspectNode(neighborId)}>
                ${neighborLabel}
              </button>` : html`<span>${neighborLabel}</span>`}
          </div>` : nothing}
        ${gap ? html`
          <div class="plate-gap">
            <span>${gap.value}${gap.detail ? ` · ${gap.detail}` : ''}</span>
          </div>` : nothing}
        <div class="plate-margin"></div>
        <div class="plate-box">
          <div class="plate-padding">
            ${plate.sizeLabel ? html`<span class="plate-size">${plate.sizeLabel}</span>` : nothing}
          </div>
        </div>
        ${plate.widthLabel ? html`
          <div class="plate-width"></div>
          <span class="plate-width-label">${plate.widthLabel}</span>` : nothing}
      </div>
      <div class="plate-legend">
        <span><i class="swatch border"></i>${i18nString(UIStrings.borderBox)}</span>
        <span><i class="swatch margin"></i>${i18nString(UIStrings.margin)}</span>
        ${plate.paddingLabel ? html`<span><i class="swatch padding"></i>${plate.paddingLabel}</span>` : nothing}
        <span>${i18nString(UIStrings.notToScale)}</span>
      </div>
      ${plate.elidedFrames > 0 ? html`
        <p class="elided">+${plate.elidedFrames} more ancestors with a role</p>` : nothing}
    </section>`;
  // clang-format on
}

function renderFrame(
    frame: PlateView['frames'][number], depth: number, output: ViewOutput): TemplateResult {
  const nodeId = frame.nodeId;
  const text = `${frame.label} · ${frame.role}`;
  // clang-format off
  return html`
    <div class="plate-frame depth-${depth} ${frame.warn ? 'warn' : ''}">
      ${nodeId !== null ? html`
        <button class="plate-label" title=${text}
            @mouseenter=${() => output.highlightNode(nodeId)}
            @mouseleave=${() => output.hideHighlight()}
            @focus=${() => output.highlightNode(nodeId)}
            @blur=${() => output.hideHighlight()}
            @click=${() => output.inspectNode(nodeId)}>${text}</button>` : html`
        <span class="plate-label" title=${text}>${text}</span>`}
    </div>`;
  // clang-format on
}

function renderChain(input: ViewInput, output: ViewOutput): TemplateResult|typeof nothing {
  const status = input.layeringPhase === 'loading' ? i18nString(UIStrings.readingAncestors) :
      input.layeringPhase === 'failed'             ? i18nString(UIStrings.ancestorsFailed) :
                                                     null;
  if (!input.rungs.length && !status) {
    return nothing;
  }
  // clang-format off
  return html`
    <section>
      <h2>${i18nString(UIStrings.decidedForYou)}</h2>
      ${input.rungs.length ? html`
        <div class="chain">
          ${input.rungs.map(rung => renderRung(rung, output))}
        </div>` : nothing}
      ${status ? html`<p class="status">${status}</p>` : nothing}
    </section>`;
  // clang-format on
}

function renderRung(rung: ChainRung, output: ViewOutput): TemplateResult {
  const roleLabel = chainRungLabel(rung);
  const measurement = rung.roles.map(role => role.measurement).find(value => Boolean(value)) ?? null;
  const effect = rung.roles.map(role => role.effect).join(' · ');
  const declaration = rung.roles.map(role => role.declaration).find(value => Boolean(value)) ?? null;
  const sourceId = rung.roles.map(role => role.declarationId).find(value => value !== null) ?? null;
  const nodeId = rung.nodeId;
  // clang-format off
  return html`
    <div class="rung actionable ${rung.warn ? 'warn' : ''}">
      ${nodeId !== null ? html`
        <button class="rung-main" aria-label="${i18nString(UIStrings.inspect)} ${rung.label}"
            @mouseenter=${() => output.highlightNode(nodeId)}
            @mouseleave=${() => output.hideHighlight()}
            @focus=${() => output.highlightNode(nodeId)}
            @blur=${() => output.hideHighlight()}
            @click=${() => output.inspectNode(nodeId)}>
          ${rungBody(roleLabel, rung.label, measurement, effect, declaration)}
        </button>` : html`
        <div class="rung-main">
          ${rungBody(roleLabel, rung.label, measurement, effect, declaration)}
        </div>`}
      ${sourceId !== null ? html`
        <div class="gutter">
          <button aria-label="${i18nString(UIStrings.source)} ${rung.label}"
              @click=${() => output.revealDeclaration(sourceId)}>${i18nString(UIStrings.source)}</button>
        </div>` : nothing}
    </div>`;
  // clang-format on
}

function rungBody(
    roleLabel: string, label: string, measurement: string|null, effect: string,
    declaration: string|null): TemplateResult {
  // clang-format off
  return html`
    <p class="rung-role">${roleLabel}</p>
    <p class="rung-who">
      <span class="name" title=${label}>${label}</span>
      ${measurement ? html`<span class="measurement">${measurement}</span>` : nothing}
    </p>
    <p class="rung-effect">
      ${effect}${declaration ? html` · <span class="declaration">${declaration}</span>` : nothing}
    </p>`;
  // clang-format on
}

function renderFact(fact: ElementFact, output: ViewOutput): TemplateResult {
  const declarationId = fact.declarationId;
  // clang-format off
  return html`
    <div class="cell property">${fact.property}</div>
    <div class="cell used">${fact.used}</div>
    <div class="cell actionable">
      <span class="origin severity-${fact.severity}" title=${fact.title}>${fact.source}</span>
      ${declarationId !== null ? html`
        <div class="gutter">
          <button aria-label="${i18nString(UIStrings.source)} ${fact.property}"
              @click=${() => output.revealDeclaration(declarationId)}>
            ${i18nString(UIStrings.source)}
          </button>
        </div>` : nothing}
    </div>`;
  // clang-format on
}

export class StyleIntelligencePane extends UI.Widget.VBox {
  #view: View;
  #model = new StyleIntelligenceModel();
  #computedStyleModel: ComputedStyle.ComputedStyleModel.ComputedStyleModel;
  #phase: PanePhase = 'empty';
  #identity: string|null = null;
  #summary: StyleIntelligenceSummary|null = null;
  #explanation: LayoutExplanation|null = null;
  #spacing: SpacingExplanation|null = null;
  #diagnostics: Diagnostic[] = [];
  #declarations = new Map<number, SDK.CSSProperty.CSSProperty>();
  #requestId = 0;
  #loadedNodeId: number|null = null;
  #refreshTimer = 0;
  #highlightedNodeId: number|null = null;
  #layeringPhase: LayeringPhase = 'idle';
  #layeringChain: StackingChain|null = null;
  #layeringBoundary: StackingBoundary|null = null;
  #layeringDeclarations = new Map<number, SDK.CSSProperty.CSSProperty>();
  #layeringRequest = 0;
  #layeringTimer = 0;
  #snapshotRecords: SnapshotNodeRecord[]|null = null;
  #snapshotTargetId: string|null = null;
  #relationships: LayoutRelationships|null = null;
  #relationshipLabels = new Map<number, string>();
  #relationshipGeneration = 0;
  #relationshipKey: string|null = null;
  #relationshipRequest = 0;
  #domModel: SDK.DOMModel.DOMModel|null = null;
  #applyingSelection = false;
  #studio = new StyleStudioTargeting(() => this.requestUpdate());
  #tabSetting = Common.Settings.Settings.instance().createSetting<PaneTab>('style-intelligence-tab', 'explain');
  #picks = new StudioPickTracker<SDK.DOMModel.DOMNode>(() => this.#studio.fillSelectorFromSelection());
  /** True only while Studio's own button is starting the picker. */
  #startingPick = false;
  #unbindStudio: () => void = () => {};
  #unsubscribePicks: () => void = () => {};

  constructor(view: View = DEFAULT_VIEW) {
    super({useShadowDom: true});
    this.registerRequiredCSS(styleIntelligencePaneStyles);
    this.#view = view;
    this.#computedStyleModel = new ComputedStyle.ComputedStyleModel.ComputedStyleModel(null);
    this.#computedStyleModel.addEventListener(
        ComputedStyle.ComputedStyleModel.Events.CSS_MODEL_CHANGED, this.#onStyleChanged, this);
    this.#computedStyleModel.addEventListener(
        ComputedStyle.ComputedStyleModel.Events.COMPUTED_STYLE_CHANGED, this.#onStyleChanged, this);
    UI.Context.Context.instance().addFlavorChangeListener(
        SDK.DOMModel.DOMNode, this.#onSelectedNodeChanged, this);
    this.#onSelectedNodeChanged();
    this.#unbindStudio =
        bindStudioToSelection(UI.Context.Context.instance(), SDK.DOMModel.DOMNode, selectionForNode, this.#studio);
    // Registered after the Studio binding, so Studio already follows the picked
    // node when the pick fills its selector.
    UI.Context.Context.instance().addFlavorChangeListener(SDK.DOMModel.DOMNode, this.#onSelectionForPick, this);
    this.#pickAction()?.addEventListener(UI.ActionRegistration.Events.TOGGLED, this.#onPickToggled, this);
    this.#unsubscribePicks = observeInspectPicks({
      received: () => this.#picks.pickReceived(),
      resolved: node => this.#picks.pickResolved(node, UI.Context.Context.instance().flavor(SDK.DOMModel.DOMNode)),
      availabilityChanged: () => this.requestUpdate(),
    });
  }

  static instance(opts: {forceNew: boolean|null} = {forceNew: null}): StyleIntelligencePane {
    if (!paneInstance || opts.forceNew) {
      // A replaced pane must stop following the selection and picks, or every
      // replacement would add another set of Studio listeners.
      if (paneInstance) {
        paneInstance.#disposeStudio();
      }
      paneInstance = new StyleIntelligencePane();
    }
    return paneInstance;
  }

  override wasShown(): void {
    super.wasShown();
    this.#scheduleLoad();
    if (this.#selectedTab() === 'studio') {
      this.#studio.revalidate();
      this.#studio.setLive(true);
    }
  }

  /** Releases every Studio listener this pane added. */
  #disposeStudio(): void {
    this.#unbindStudio();
    this.#unsubscribePicks();
    UI.Context.Context.instance().removeFlavorChangeListener(SDK.DOMModel.DOMNode, this.#onSelectionForPick, this);
    this.#pickAction()?.removeEventListener(UI.ActionRegistration.Events.TOGGLED, this.#onPickToggled, this);
    this.#picks.dispose();
    this.#studio.dispose();
  }

  override willHide(): void {
    super.willHide();
    this.#hideHighlight();
    this.#studio.setLive(false);
  }

  override onDetach(): void {
    super.onDetach();
    this.#hideHighlight();
    this.#unbindDomModel();
  }

  override performUpdate(): Promise<void> {
    const node = this.#computedStyleModel.node;
    const selectedNodeId = node?.id ?? null;
    const axes = this.#explanation?.axes ?? [];
    const relationships = this.#spacing?.relationships ?? [];
    const rungs = this.#summary ? buildElementChain({
      selectedNodeId,
      relationships: this.#relationships,
      labels: this.#relationshipLabels,
      insets: insetsFromExplanation(this.#explanation),
      axes,
      spacing: relationships,
      stacking: this.#chainStacking(selectedNodeId),
    }) : [];
    const facts = this.#summary ? buildElementFacts({
      selectedNodeId,
      axes,
      spacing: relationships,
      diagnostics: this.#diagnostics,
      layering: this.#factsLayering(),
    }) : [];
    const plate = this.#summary ? buildPlate({
      selectedNodeId,
      rungs,
      geometry: this.#summary.geometry ? {width: this.#summary.geometry.width, height: this.#summary.geometry.height} :
                                        null,
      axes,
      spacing: relationships,
    }) : null;

    this.#view(
        {
          phase: this.#phase,
          identity: this.#identity,
          subtitle: this.#subtitle(rungs.length),
          plate,
          rungs,
          facts,
          tokens: this.#tokens(),
          issues: factCount(facts).issues,
          layeringPhase: this.#layeringPhase,
          hasSummary: this.#summary !== null,
          tab: this.#selectedTab(),
          studio: this.#studioInput(),
        },
        {
          highlightNode: this.#highlightNode,
          hideHighlight: this.#hideHighlight,
          inspectNode: this.#inspectNode,
          revealDeclaration: this.#revealDeclaration,
          selectTab: this.#selectTab,
          studio: this.#studioOutput,
        },
        this.contentElement);
    return Promise.resolve();
  }

  #selectedTab(): PaneTab {
    const tab = this.#tabSetting.get();
    return PANE_TABS.includes(tab) ? tab : 'explain';
  }

  #selectTab = (tab: PaneTab): void => {
    if (tab === this.#selectedTab()) {
      return;
    }
    // A highlight drawn from one tab must not outlive it.
    this.#hideHighlight();
    this.#tabSetting.set(tab);
    if (tab === 'studio') {
      this.#studio.revalidate();
    }
    this.#studio.setLive(tab === 'studio' && this.isShowing());
    this.requestUpdate();
  };

  #pickAction(): UI.ActionRegistration.Action|null {
    const registry = UI.ActionRegistry.ActionRegistry.instance();
    return registry.hasAction(PICK_ACTION_ID) ? registry.getAction(PICK_ACTION_ID) : null;
  }

  /**
   * Toggles the existing Elements picker. A pick becomes the Elements selection
   * through the normal path, which Studio already follows. In Selector mode, a
   * pick started here also fills the selector, including a pick of the element
   * that is already selected (see StudioPickTracker).
   */
  #togglePick = async(): Promise<void> => {
    const action = this.#pickAction();
    if (!action) {
      return;
    }
    if (action.toggled()) {
      // Clicking again cancels, exactly like the toolbar button.
      await action.execute();
      return;
    }
    this.#picks.arm();
    this.#startingPick = true;
    try {
      await action.execute();
    } finally {
      this.#startingPick = false;
    }
  };

  #onPickToggled = (event: Common.EventTarget.EventTargetEvent<boolean>): void => {
    if (event.data && !this.#startingPick) {
      // The toolbar button or the shortcut started this pick, not Studio.
      this.#picks.disarm();
    } else if (!event.data) {
      this.#picks.inspectModeEnded();
    }
    this.requestUpdate();
  };

  #onSelectionForPick(): void {
    this.#picks.selectionChanged(UI.Context.Context.instance().flavor(SDK.DOMModel.DOMNode));
  }

  /** The picker is usable whenever the action exists and a page target can be inspected. */
  #canPick(): boolean {
    return this.#pickAction() !== null &&
        SDK.TargetManager.TargetManager.instance().models(SDK.OverlayModel.OverlayModel, {scoped: true}).length > 0;
  }

  /** Always present: with nothing selected, Studio still offers the picker. */
  #studioInput(): StudioViewInput {
    const state = this.#studio.state();
    return {
      nodeLabel: state.node?.label ?? null,
      notice: state.previousDetachedLabel,
      canPick: this.#canPick(),
      picking: this.#pickAction()?.toggled() ?? false,
      source: state.source,
      scope: state.scope,
      selectorText: state.selectorText,
      target: this.#studio.target(),
    };
  }

  readonly #studioOutput: StudioViewOutput = {
    setSource: source => this.#studio.setSource(source),
    setScope: scope => this.#studio.setScope(scope),
    setSelectorText: text => this.#studio.setSelectorText(text),
    commitSelector: () => void this.#studio.resolveNow(),
    highlightTarget: () => this.#highlightStudioTarget(),
    hideHighlight: () => this.#hideHighlight(),
    togglePick: () => void this.#togglePick(),
  };

  /**
   * Draws the overlay over the Studio target without selecting it. All matches
   * are drawn by the overlay's own selector highlight, which queries the same
   * document the matches were resolved in.
   */
  #highlightStudioTarget(): void {
    const target = this.#studio.target();
    if (target.kind === 'node') {
      this.#highlightNode(target.nodeId);
      return;
    }
    if (target.kind !== 'matches') {
      return;
    }
    const first = this.#nodeById(target.nodeIds[0]);
    if (!first) {
      this.#hideHighlight();
      return;
    }
    this.#highlightedNodeId = null;
    first.domModel().overlayModel().highlightInOverlay({node: first, selectorList: target.selector}, 'all');
  }

  /**
   * The Layout section folded into the header, because at 266px a one-line
   * subtitle says what three stacked prose lines said.
   */
  #subtitle(rungCount: number): string|null {
    if (!this.#summary) {
      return null;
    }
    const parts: string[] = [];
    if (this.#summary.geometry) {
      parts.push(`${this.#summary.geometry.width} × ${this.#summary.geometry.height}`);
    }
    for (const statement of this.#summary.layout.slice(0, 2)) {
      parts.push(formatLayoutStatement(statement).toLocaleLowerCase());
    }
    if (rungCount > 0) {
      parts.push(rungCount === 1 ? '1 ancestor' : `${rungCount} ancestors`);
    }
    return parts.length ? parts.join(' · ') : null;
  }

  /**
   * Tokens keep their list but gain the use column. The existing label already
   * names what the token is doing here, so this is a reorder, not new data.
   */
  #tokens(): PaneToken[] {
    return (this.#summary?.tokens ?? []).map(token => ({
                                              name: token.customProperty,
                                              value: token.resolvedValue ? displayValue(token.resolvedValue) : null,
                                              use: token.label,
                                              title: token.resolvedValue ?? token.customProperty,
                                            }));
  }

  /**
   * The stacking context reduced to a rung. The CSS predicate alone supports
   * "this ancestor is painted as one unit"; only a confirmed boundary supports
   * the stronger claim that the element's own z-index cannot escape it.
   */
  #chainStacking(selectedNodeId: number|null): ChainStacking|null {
    const chain = this.#layeringChain;
    if (!chain || chain.selected.topLayer) {
      return null;
    }
    const boundary = this.#layeringBoundary;
    if (boundary && boundary.trapNodeId !== selectedNodeId) {
      return {
        nodeId: boundary.trapNodeId,
        label: boundary.trapLabel,
        declaration: boundary.trapText || null,
        declarationId: boundary.trapDeclarationId,
        hasSource: boundary.trapHasSource,
        trapped: true,
        trappedZIndex: effectiveZIndexText(chain),
      };
    }
    const ancestor = chain.ancestorContexts.find(node => !isDocumentRootContext(node));
    if (!ancestor || ancestor.nodeId === selectedNodeId) {
      return null;
    }
    const reason = ancestor.reasons.find(item => item.property !== 'root' && item.property !== 'internal');
    const value = reason ? reason.authoredValue ?? reason.computedValue : null;
    return {
      nodeId: ancestor.nodeId,
      label: ancestor.label,
      declaration: reason && value ? `${reason.property}: ${value}` : null,
      declarationId: reason?.declarationId ?? null,
      hasSource: reason?.hasSource ?? false,
      trapped: false,
      trappedZIndex: null,
    };
  }

  #factsLayering(): FactsLayering|null {
    const chain = this.#layeringChain;
    const boundary = this.#layeringBoundary;
    if (!chain || !boundary) {
      return null;
    }
    return {
      zIndex: effectiveZIndexText(chain),
      trapped: true,
      trapLabel: boundary.trapLabel,
    };
  }

  #onSelectedNodeChanged(): void {
    window.clearTimeout(this.#refreshTimer);
    this.#hideHighlight();
    this.#clearRelationships();
    this.#applyingSelection = true;
    this.#computedStyleModel.node = UI.Context.Context.instance().flavor(SDK.DOMModel.DOMNode);
    this.#applyingSelection = false;
    this.#bindDomModel(this.#computedStyleModel.node?.domModel() ?? null);
    this.#scheduleLoad();
    this.#clearLayeringNode();
    this.#scheduleLayering(LAYERING_START_DELAY);
  }

  #onStyleChanged = (_event?: Common.EventTarget.EventTargetEvent<unknown>): void => {
    window.clearTimeout(this.#refreshTimer);
    this.#relationshipGeneration++;
    this.#refreshTimer = window.setTimeout(() => this.#scheduleLoad(), 50);
    if (!this.#applyingSelection) {
      this.#invalidateLayeringDocument();
    }
  };

  #scheduleLoad = (): void => {
    const requestId = ++this.#requestId;
    if (this.#highlightedNodeId !== null && !this.#nodeById(this.#highlightedNodeId)) {
      this.#hideHighlight();
    }
    const node = this.#computedStyleModel.node;
    if (!node || node.nodeType() !== Node.ELEMENT_NODE) {
      this.#identity = null;
      this.#summary = null;
      this.#explanation = null;
      this.#spacing = null;
      this.#diagnostics = [];
      this.#declarations.clear();
      this.#loadedNodeId = null;
      this.#phase = 'empty';
      this.#clearRelationships();
      this.requestUpdate();
      return;
    }

    const sameNode = this.#loadedNodeId === node.id && this.#summary !== null;
    this.#identity = identityForNode(node);
    this.#loadedNodeId = node.id;
    if (!sameNode) {
      this.#summary = null;
      this.#explanation = null;
      this.#spacing = null;
      this.#diagnostics = [];
      this.#declarations.clear();
    }
    this.#phase = 'loading';
    this.requestUpdate();
    const apply = (result: Extract<StyleIntelligenceLoad, {status: 'ok'}>, final: boolean): void => {
      if (requestId !== this.#requestId) {
        return;
      }
      this.#summary = result.summary;
      this.#explanation = result.explanation;
      this.#spacing = result.spacing;
      this.#diagnostics = result.diagnostics;
      this.#declarations = result.declarations;
      this.#phase = final ? 'ready' : 'loading';
      this.requestUpdate();
      if (this.#highlightedNodeId !== null) {
        this.#highlightNode(this.#highlightedNodeId);
      }
    };
    void this.#model.describe(node, partial => apply(partial, false)).then(result => {
      if (requestId !== this.#requestId) {
        return;
      }
      if (result.status === 'ok') {
        apply(result, true);
        this.#loadRelationships(node, requestId);
        return;
      }
      this.#phase = 'unavailable';
      this.#summary = null;
      this.#explanation = null;
      this.#spacing = null;
      this.#diagnostics = [];
      this.#declarations.clear();
      this.requestUpdate();
    }).catch(() => {
      if (requestId !== this.#requestId) {
        return;
      }
      if (this.#summary) {
        this.#phase = 'ready';
      } else {
        this.#phase = 'failed';
      }
      this.requestUpdate();
    });
  };

  #clearRelationships(): void {
    this.#relationshipRequest++;
    this.#relationships = null;
    this.#relationshipLabels = new Map();
    this.#relationshipKey = null;
  }

  #loadRelationships(node: SDK.DOMModel.DOMNode, styleRequestId: number): void {
    const position = nodePosition(this.#summary);
    if (!positionNeedsLayoutRelationships(position)) {
      this.#clearRelationships();
      return;
    }
    const key = `${node.id}:${this.#relationshipGeneration}`;
    if (this.#relationshipKey === key && this.#relationships) {
      return;
    }
    const request = ++this.#relationshipRequest;
    const generation = this.#relationshipGeneration;
    void loadLayoutRelationships(node).then(relationships => {
      if (request !== this.#relationshipRequest || styleRequestId !== this.#requestId) {
        return;
      }
      if (this.#computedStyleModel.node?.id !== node.id || generation !== this.#relationshipGeneration) {
        return;
      }
      this.#relationships = relationships;
      this.#relationshipKey = key;
      this.#relationshipLabels = relationshipLabels(node, relationships);
      if (this.#highlightedNodeId !== null && !this.#nodeById(this.#highlightedNodeId)) {
        this.#hideHighlight();
      } else if (this.#highlightedNodeId !== null) {
        this.#highlightNode(this.#highlightedNodeId);
      }
      this.requestUpdate();
    }).catch(() => {
      if (request !== this.#relationshipRequest) {
        return;
      }
      this.#relationships = null;
      this.#relationshipKey = null;
      this.requestUpdate();
    });
  }

  #nodeById(nodeId: number): SDK.DOMModel.DOMNode|null {
    return this.#computedStyleModel.node?.domModel().nodeForId(nodeId) ?? null;
  }

  #highlightNode = (nodeId: number): void => {
    const node = this.#nodeById(nodeId);
    if (!node) {
      this.#hideHighlight();
      return;
    }
    this.#highlightedNodeId = nodeId;
    node.domModel().overlayModel().highlightInOverlay({node}, 'all');
  };

  #hideHighlight = (): void => {
    const highlighted = this.#highlightedNodeId !== null ? this.#nodeById(this.#highlightedNodeId) : null;
    const node = this.#computedStyleModel.node ?? highlighted;
    this.#highlightedNodeId = null;
    if (!node) {
      return;
    }
    SDK.OverlayModel.OverlayModel.hideDOMNodeHighlight(node.domModel().target().targetManager());
  };

  #inspectNode = (nodeId: number): void => {
    this.#hideHighlight();
    const node = this.#nodeById(nodeId);
    if (node) {
      void Common.Revealer.reveal(node);
    }
  };

  #clearLayeringNode(): void {
    this.#layeringRequest++;
    this.#layeringChain = null;
    this.#layeringBoundary = null;
    this.#layeringDeclarations.clear();
    this.#layeringPhase = 'loading';
  }

  #invalidateLayeringDocument = (): void => {
    const hadAnalysis = this.#snapshotRecords !== null || this.#layeringChain !== null;
    this.#snapshotRecords = null;
    this.#snapshotTargetId = null;
    if (!hadAnalysis) {
      return;
    }
    this.#clearLayeringNode();
    this.#scheduleLayering(LAYERING_START_DELAY);
  };

  #scheduleLayering(delay: number): void {
    window.clearTimeout(this.#layeringTimer);
    this.#layeringTimer = window.setTimeout(() => void this.#loadLayering(), delay);
  }

  /**
   * The ancestor walk now starts on its own instead of waiting for a
   * disclosure, because a trapped z-index is the reason most people open this
   * pane and it should not take a click to see. The walk is still deferred and
   * the document snapshot is still gated on needsStackingSnapshot, so the
   * first paint does not wait on either.
   */
  #loadLayering = async (): Promise<void> => {
    const request = ++this.#layeringRequest;
    const node = this.#computedStyleModel.node;
    if (!node || node.nodeType() !== Node.ELEMENT_NODE) {
      this.#layeringPhase = 'idle';
      this.#layeringChain = null;
      this.requestUpdate();
      return;
    }
    this.#layeringPhase = 'loading';
    this.requestUpdate();
    try {
      const loaded = await loadStackingChain(node);
      if (request !== this.#layeringRequest) {
        return;
      }
      if (!loaded) {
        this.#layeringPhase = 'failed';
        this.#layeringChain = null;
        this.requestUpdate();
        return;
      }
      this.#layeringChain = loaded.chain;
      this.#layeringDeclarations = loaded.declarations;
      this.#layeringBoundary = null;
      if (!needsStackingSnapshot(loaded.chain)) {
        this.#layeringPhase = 'ready';
        this.requestUpdate();
        return;
      }
      // The CSS predicate alone would paint a context the Blink bit may reject,
      // so the rung stays on its weaker claim until the snapshot confirms.
      this.#layeringPhase = 'ready';
      this.requestUpdate();
      const targetId = node.domModel().target().id();
      let records = this.#snapshotTargetId === targetId ? this.#snapshotRecords : null;
      if (!records) {
        records = await captureStackingSnapshot(node);
        if (request !== this.#layeringRequest) {
          return;
        }
        this.#snapshotRecords = records;
        this.#snapshotTargetId = targetId;
      }
      if (request !== this.#layeringRequest) {
        return;
      }
      if (!records) {
        this.#layeringPhase = 'ready';
        this.requestUpdate();
        return;
      }
      const chain = applySnapshot(loaded.chain, records);
      this.#layeringChain = chain;
      this.#layeringBoundary = explainStackingBoundary(chain, records);
      this.#layeringPhase = 'ready';
      this.requestUpdate();
    } catch {
      if (request !== this.#layeringRequest) {
        return;
      }
      if (!this.#layeringChain) {
        this.#layeringPhase = 'failed';
      } else {
        this.#layeringPhase = 'ready';
      }
      this.requestUpdate();
    }
  };

  #bindDomModel(model: SDK.DOMModel.DOMModel|null): void {
    if (this.#domModel === model) {
      return;
    }
    this.#unbindDomModel();
    this.#domModel = model;
    if (!model) {
      return;
    }
    model.addEventListener(SDK.DOMModel.Events.DocumentUpdated, this.#invalidateLayeringDocument, this);
    model.addEventListener(SDK.DOMModel.Events.DocumentUpdated, this.#onRelationshipDomChanged, this);
    model.addEventListener(SDK.DOMModel.Events.NodeInserted, this.#invalidateLayeringDocument, this);
    model.addEventListener(SDK.DOMModel.Events.NodeRemoved, this.#invalidateLayeringDocument, this);
    model.addEventListener(SDK.DOMModel.Events.NodeRemoved, this.#onRelationshipDomChanged, this);
    model.addEventListener(SDK.DOMModel.Events.AttrModified, this.#invalidateLayeringDocument, this);
    model.addEventListener(SDK.DOMModel.Events.AttrRemoved, this.#invalidateLayeringDocument, this);
    model.addEventListener(SDK.DOMModel.Events.TopLayerElementsChanged, this.#invalidateLayeringDocument, this);
  }

  #unbindDomModel(): void {
    if (!this.#domModel) {
      return;
    }
    this.#domModel.removeEventListener(SDK.DOMModel.Events.DocumentUpdated, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.DocumentUpdated, this.#onRelationshipDomChanged, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.NodeInserted, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.NodeRemoved, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.NodeRemoved, this.#onRelationshipDomChanged, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.AttrModified, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.AttrRemoved, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(
        SDK.DOMModel.Events.TopLayerElementsChanged, this.#invalidateLayeringDocument, this);
    this.#domModel = null;
  }

  #onRelationshipDomChanged = (): void => {
    if (this.#highlightedNodeId !== null && !this.#nodeById(this.#highlightedNodeId)) {
      this.#hideHighlight();
    }
    if (!this.#relationships) {
      return;
    }
    const missing = relationshipNodeIds(this.#relationships).some(nodeId => !this.#nodeById(nodeId));
    if (!missing) {
      return;
    }
    this.#relationshipGeneration++;
    this.#relationships = null;
    this.#relationshipLabels = new Map();
    this.#relationshipKey = null;
    this.#relationshipRequest++;
    this.#scheduleLoad();
  };

  #revealDeclaration = (declarationId: number): void => {
    const property = this.#declarations.get(declarationId) ?? this.#layeringDeclarations.get(declarationId);
    if (!property) {
      return;
    }
    const location = Bindings.CSSWorkspaceBinding.CSSWorkspaceBinding.instance().propertyUILocation(property, false);
    if (location) {
      void Common.Revealer.reveal(location);
    }
  };
}

function effectiveZIndexText(chain: StackingChain): string|null {
  const zIndex = chain.selected.zIndex;
  if (!zIndex || zIndex === 'auto') {
    return null;
  }
  return zIndex;
}

function displayValue(value: string): string {
  if (value.length <= 48) {
    return value;
  }
  return `${value.slice(0, 45)}…`;
}

function nodePosition(summary: StyleIntelligenceSummary|null): string|null {
  const statement = summary?.layout.find(item => item.type === 'position');
  return statement && statement.type === 'position' ? statement.value : null;
}

function relationshipLabels(node: SDK.DOMModel.DOMNode, relationships: LayoutRelationships|null): Map<number, string> {
  const labels = new Map<number, string>();
  if (!relationships) {
    return labels;
  }
  for (const nodeId of relationshipNodeIds(relationships)) {
    const related = node.domModel().nodeForId(nodeId);
    if (related) {
      labels.set(nodeId, identityForNode(related));
    }
  }
  return labels;
}

function relationshipNodeIds(relationships: LayoutRelationships): number[] {
  const ids: number[] = [];
  const add = (relationship: {nodeId: number|null}|null|undefined): void => {
    if (relationship?.nodeId) {
      ids.push(relationship.nodeId);
    }
  };
  add(relationships.containingBlock);
  add(relationships.scrollContainer);
  add(relationships.stickyScrollContainers?.horizontal);
  add(relationships.stickyScrollContainers?.vertical);
  return ids;
}

let paneInstance: StyleIntelligencePane|null = null;
