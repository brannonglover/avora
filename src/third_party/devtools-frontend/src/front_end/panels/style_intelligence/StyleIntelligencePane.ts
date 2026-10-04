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
import {type StyleIntelligenceSummary} from './ElementSummary.js';
import {presentLayering, type LayeringView} from './LayeringPresentation.js';
import {type LayoutExplanation} from './LayoutExplanation.js';
import {type PaneAction, type PaneControl, type PaneDiagnostic, type PaneRow, type PaneSection, presentPane} from './Presentation.js';
import {type SpacingExplanation} from './SpacingExplanation.js';
import {explainStackingBoundary, type StackingBoundary} from './StackingBoundary.js';
import {needsStackingSnapshot, type StackingChain} from './StackingChain.js';
import {captureStackingSnapshot, loadStackingChain} from './StackingChainLoader.js';
import {applySnapshot, type SnapshotNodeRecord} from './StackingSnapshot.js';
import styleIntelligencePaneStyles from './styleIntelligencePane.css.js';
import {identityForNode, type StyleIntelligenceLoad, StyleIntelligenceModel} from './StyleIntelligenceModel.js';

const UIStrings = {
  /**
   * @description Shown in Style Intelligence when no element is selected.
   */
  selectAnElement: 'Select an element.',
  /**
   * @description Section heading for the selected element's layout system.
   */
  layout: 'Layout',
  /**
   * @description Section heading for width and height.
   */
  size: 'Size',
  /**
   * @description Section heading for horizontal and vertical position.
   */
  position: 'Position',
  /**
   * @description Section heading for measured gaps around the selected element.
   */
  spacing: 'Spacing',
  /**
   * @description Section heading for CSS custom properties used by the selected element.
   */
  tokens: 'Tokens',
  /**
   * @description Button that highlights a layout ancestor without selecting it.
   */
  highlight: 'Highlight',
  /**
   * @description Button that selects a layout ancestor in the Elements panel.
   */
  inspect: 'Inspect',
  /**
   * @description Button that reveals an author declaration in Sources.
   */
  source: 'Source',
  /**
   * @description Section heading for diagnostics that do not belong to a measured section.
   */
  potentialIssues: 'Potential issues',
  /**
   * @description Shown when styles for the selected element could not be read.
   */
  stylesFailed: 'Styles for this element could not be loaded.',
  /**
   * @description Shown when the selected node has no CSS model.
   */
  stylesUnavailable: 'Styles are not available for this element.',
  /**
   * @description Disclosure for secondary measurements and caveats.
   */
  details: 'Details',
  /**
   * @description Disclosure for stacking-context explanation. Collapsed until opened.
   */
  layering: 'Layering',
  /**
   * @description Shown while the Layering section is reading ancestor context.
   */
  layeringLoading: 'Reading stacking context…',
  /**
   * @description Shown when the Layering section could not be read.
   */
  layeringFailed: 'Stacking context could not be loaded.',
} as const;
const str_ = i18n.i18n.registerUIStrings('panels/style_intelligence/StyleIntelligencePane.ts', UIStrings);
const i18nString = i18n.i18n.getLocalizedString.bind(undefined, str_);

type PanePhase = 'empty'|'loading'|'ready'|'failed'|'unavailable';
type LayeringPhase = 'idle'|'loading'|'ready'|'failed';

interface ViewInput {
  phase: PanePhase;
  identity: string|null;
  summary: StyleIntelligenceSummary|null;
  explanation: LayoutExplanation|null;
  spacing: SpacingExplanation|null;
  diagnostics: Diagnostic[];
  selectedNodeId: number|null;
  layeringOpen: boolean;
  layeringPhase: LayeringPhase;
  layering: LayeringView|null;
}

interface ViewOutput {
  highlightNode: (nodeId: number) => void;
  hideHighlight: () => void;
  inspectNode: (nodeId: number) => void;
  revealDeclaration: (declarationId: number) => void;
  onLayeringToggle: (event: Event) => void;
}

type View = (input: ViewInput, output: ViewOutput, target: HTMLElement) => void;

export const DEFAULT_VIEW: View = (input, output, target) => {
  if (input.phase === 'empty' || !input.identity) {
    render(
        html`<div class="style-intelligence empty" jslog=${VisualLogging.pane('style-intelligence')}>
          ${i18nString(UIStrings.selectAnElement)}
        </div>`,
        target);
    return;
  }
  if (!input.summary) {
    const status = input.phase === 'failed' ? i18nString(UIStrings.stylesFailed) :
        input.phase === 'unavailable'       ? i18nString(UIStrings.stylesUnavailable) :
                                               nothing;
    render(
        html`<div class="style-intelligence" jslog=${VisualLogging.pane('style-intelligence')} aria-busy=${input.phase === 'loading'}>
          <div class="identity">${input.identity}</div>
          ${status ? html`<p class="status">${status}</p>` : nothing}
          ${renderLayering(input, output)}
        </div>`,
        target);
    return;
  }

  const pane = presentPane(input.summary, input.explanation, input.spacing, input.diagnostics, input.selectedNodeId);
  // clang-format off
  render(html`
    <div class="style-intelligence" jslog=${VisualLogging.pane('style-intelligence').track({resize: true})} aria-busy=${input.phase === 'loading'}>
      <header>
        <div class="identity" title=${pane.identity}>${pane.identity}</div>
        ${pane.size ? html`<div class="size">${pane.size}</div>` : nothing}
      </header>
      ${pane.sections.map(section => renderSection(section, output))}
      ${renderLayering(input, output)}
    </div>`, target);
  // clang-format on
};

function renderLayering(input: ViewInput, output: ViewOutput): TemplateResult {
  const body = input.layeringPhase === 'loading' ? html`<p class="status">${i18nString(UIStrings.layeringLoading)}</p>` :
      input.layeringPhase === 'failed'           ? html`<p class="status">${i18nString(UIStrings.layeringFailed)}</p>` :
      input.layering                            ? html`
        ${input.layering.lines.map(line => html`<p class="line">${line}</p>`)}
        ${input.layering.actions.map(action => html`
          <div class="control">
            ${action.nodeId !== null ? html`<div class="control-text" title=${action.text}>${action.text}</div>` : nothing}
            ${renderActions({nodeId: action.nodeId, declarationId: action.declarationId}, action.text, output)}
          </div>`)}
      `                                             : nothing;
  return html`
    <section class="layering">
      <details ?open=${input.layeringOpen} @toggle=${output.onLayeringToggle}>
        <summary>${i18nString(UIStrings.layering)}</summary>
        ${body}
      </details>
    </section>`;
}

function sectionTitle(title: string): string {
  switch (title) {
    case 'Layout':
      return i18nString(UIStrings.layout);
    case 'Size':
      return i18nString(UIStrings.size);
    case 'Position':
      return i18nString(UIStrings.position);
    case 'Spacing':
      return i18nString(UIStrings.spacing);
    case 'Tokens':
      return i18nString(UIStrings.tokens);
    case 'Potential issues':
      return i18nString(UIStrings.potentialIssues);
    default:
      return title;
  }
}

function renderSection(section: PaneSection, output: ViewOutput): TemplateResult {
  return html`
    <section>
      <h2>${sectionTitle(section.title)}</h2>
      ${section.lines.map(line => html`<p class="line">${line}</p>`)}
      ${section.rows.map(row => renderRow(row, output))}
      ${section.tokens.map(token => html`
        <div class="token-row">
          <span class="token-label">${token.label}</span>
          <span class="token-value" title=${token.title}>
            <span class="token-name">${token.name}</span>
            ${token.value ? html`<span class="token-resolved"> → ${token.value}</span>` : nothing}
          </span>
        </div>`)}
      ${section.diagnostics.map(diagnostic => renderDiagnostic(diagnostic, output))}
    </section>`;
}

function renderRow(row: PaneRow, output: ViewOutput): TemplateResult {
  const visibleControls = row.controls.filter(control => control.text);
  return html`
    <div class="row">
      <h3>${row.label}</h3>
      <p class="primary">${row.primary}</p>
      ${row.note ? html`<p class="note">${row.note}</p>` : nothing}
      ${visibleControls.map(control => renderControl(control, output))}
      ${row.diagnostics.map(diagnostic => renderDiagnostic(diagnostic, output))}
      ${row.details.length ? html`
        <details>
          <summary>${i18nString(UIStrings.details)}</summary>
          ${row.details.map(detail => html`<p class="note">${detail}</p>`)}
        </details>` : nothing}
    </div>`;
}

function renderControl(control: PaneControl, output: ViewOutput): TemplateResult {
  return html`
    <div class="control">
      <div class="control-text" title=${control.title}>${control.text}</div>
      ${renderActions(control.actions, control.title, output)}
    </div>`;
}

function renderDiagnostic(diagnostic: PaneDiagnostic, output: ViewOutput): TemplateResult {
  return html`
    <div class="diagnostic">
      <div class="diagnostic-category">${diagnostic.category}</div>
      ${diagnostic.headline ? html`<p>${diagnostic.headline}</p>` : nothing}
      ${diagnostic.detail ? html`<p class="note">${diagnostic.detail}</p>` : nothing}
      ${renderActions({nodeId: null, declarationId: diagnostic.declarationId}, diagnostic.headline, output)}
    </div>`;
}

function renderActions(actions: PaneAction, name: string, output: ViewOutput): TemplateResult|typeof nothing {
  if (actions.nodeId === null && actions.declarationId === null) {
    return nothing;
  }
  const targetName = name.length > 80 ? `${name.slice(0, 77)}…` : (name || i18nString(UIStrings.highlight));
  return html`
    <div class="actions">
      ${actions.nodeId !== null ? html`
        <button type="button" aria-label="${i18nString(UIStrings.highlight)} ${targetName}"
            @mouseenter=${() => output.highlightNode(actions.nodeId!)}
            @mouseleave=${() => output.hideHighlight()}
            @focus=${() => output.highlightNode(actions.nodeId!)}
            @blur=${() => output.hideHighlight()}>
          ${i18nString(UIStrings.highlight)}
        </button>
        <button type="button" aria-label="${i18nString(UIStrings.inspect)} ${targetName}"
            @click=${() => output.inspectNode(actions.nodeId!)}>${i18nString(UIStrings.inspect)}</button>
      ` : nothing}
      ${actions.declarationId !== null ? html`
        <button type="button" aria-label="${i18nString(UIStrings.source)} ${targetName}"
            @click=${() => output.revealDeclaration(actions.declarationId!)}>${i18nString(UIStrings.source)}</button>
      ` : nothing}
    </div>`;
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
  #layeringOpen = false;
  #layeringPhase: LayeringPhase = 'idle';
  #layeringChain: StackingChain|null = null;
  #layeringBoundary: StackingBoundary|null = null;
  #layeringDeclarations = new Map<number, SDK.CSSProperty.CSSProperty>();
  #layeringRequest = 0;
  #layeringTimer = 0;
  #snapshotRecords: SnapshotNodeRecord[]|null = null;
  #snapshotTargetId: string|null = null;
  #domModel: SDK.DOMModel.DOMModel|null = null;
  #applyingSelection = false;

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
  }

  static instance(opts: {forceNew: boolean|null} = {forceNew: null}): StyleIntelligencePane {
    if (!paneInstance || opts.forceNew) {
      paneInstance = new StyleIntelligencePane();
    }
    return paneInstance;
  }

  override wasShown(): void {
    super.wasShown();
    this.#scheduleLoad();
  }

  override willHide(): void {
    super.willHide();
    this.#hideHighlight();
  }

  override onDetach(): void {
    super.onDetach();
    this.#hideHighlight();
    this.#unbindDomModel();
  }

  override performUpdate(): Promise<void> {
    const node = this.#computedStyleModel.node;
    this.#view({
      phase: this.#phase,
      identity: this.#identity,
      summary: this.#summary,
      explanation: this.#explanation,
      spacing: this.#spacing,
      diagnostics: this.#diagnostics,
      selectedNodeId: node?.id ?? null,
      layeringOpen: this.#layeringOpen,
      layeringPhase: this.#layeringPhase,
      layering: this.#layeringChain ? presentLayering(this.#layeringChain, this.#layeringBoundary, node?.id ?? null) : null,
    }, {
      highlightNode: this.#highlightNode,
      hideHighlight: this.#hideHighlight,
      inspectNode: this.#inspectNode,
      revealDeclaration: this.#revealDeclaration,
      onLayeringToggle: this.#onLayeringToggle,
    }, this.contentElement);
    return Promise.resolve();
  }

  #onSelectedNodeChanged(): void {
    window.clearTimeout(this.#refreshTimer);
    this.#hideHighlight();
    this.#applyingSelection = true;
    this.#computedStyleModel.node = UI.Context.Context.instance().flavor(SDK.DOMModel.DOMNode);
    this.#applyingSelection = false;
    this.#bindDomModel(this.#computedStyleModel.node?.domModel() ?? null);
    this.#scheduleLoad();
    this.#clearLayeringNode();
    if (this.#layeringOpen) {
      this.#scheduleLayering(0);
    }
  }

  #onStyleChanged = (_event?: Common.EventTarget.EventTargetEvent<unknown>): void => {
    window.clearTimeout(this.#refreshTimer);
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

  #onLayeringToggle = (event: Event): void => {
    const open = event.target instanceof HTMLDetailsElement && event.target.open;
    if (open === this.#layeringOpen) {
      return;
    }
    this.#layeringOpen = open;
    if (open) {
      this.#scheduleLayering(0);
      return;
    }
    this.#hideHighlight();
  };

  #clearLayeringNode(): void {
    this.#layeringRequest++;
    this.#layeringChain = null;
    this.#layeringBoundary = null;
    this.#layeringDeclarations.clear();
    this.#layeringPhase = this.#layeringOpen ? 'loading' : 'idle';
  }

  #invalidateLayeringDocument = (): void => {
    const hadAnalysis = this.#snapshotRecords !== null || this.#layeringChain !== null || this.#layeringOpen;
    this.#snapshotRecords = null;
    this.#snapshotTargetId = null;
    if (!hadAnalysis) {
      return;
    }
    this.#clearLayeringNode();
    if (this.#layeringOpen) {
      this.#scheduleLayering(50);
    } else {
      this.requestUpdate();
    }
  };

  #scheduleLayering(delay: number): void {
    window.clearTimeout(this.#layeringTimer);
    this.#layeringTimer = window.setTimeout(() => void this.#loadLayering(), delay);
  }

  #loadLayering = async (): Promise<void> => {
    if (!this.#layeringOpen) {
      return;
    }
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
      if (request !== this.#layeringRequest || !this.#layeringOpen) {
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
      // Stay on the loading line until the snapshot confirms the CSS predicate.
      // A ready paint here would show a CSS context that the bit may reject.
      this.#layeringPhase = 'loading';
      this.requestUpdate();
      const targetId = node.domModel().target().id();
      let records = this.#snapshotTargetId === targetId ? this.#snapshotRecords : null;
      if (!records) {
        records = await captureStackingSnapshot(node);
        if (request !== this.#layeringRequest || !this.#layeringOpen) {
          return;
        }
        this.#snapshotRecords = records;
        this.#snapshotTargetId = targetId;
      }
      if (request !== this.#layeringRequest || !this.#layeringOpen) {
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
    model.addEventListener(SDK.DOMModel.Events.NodeInserted, this.#invalidateLayeringDocument, this);
    model.addEventListener(SDK.DOMModel.Events.NodeRemoved, this.#invalidateLayeringDocument, this);
    model.addEventListener(SDK.DOMModel.Events.AttrModified, this.#invalidateLayeringDocument, this);
    model.addEventListener(SDK.DOMModel.Events.AttrRemoved, this.#invalidateLayeringDocument, this);
    model.addEventListener(SDK.DOMModel.Events.TopLayerElementsChanged, this.#invalidateLayeringDocument, this);
  }

  #unbindDomModel(): void {
    if (!this.#domModel) {
      return;
    }
    this.#domModel.removeEventListener(SDK.DOMModel.Events.DocumentUpdated, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.NodeInserted, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.NodeRemoved, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.AttrModified, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.AttrRemoved, this.#invalidateLayeringDocument, this);
    this.#domModel.removeEventListener(SDK.DOMModel.Events.TopLayerElementsChanged, this.#invalidateLayeringDocument, this);
    this.#domModel = null;
  }

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

let paneInstance: StyleIntelligencePane|null = null;
