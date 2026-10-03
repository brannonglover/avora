// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as SDK from '../../core/sdk/sdk.js';
import {nothing, render, type LitTemplate} from '../../ui/lit/lit.js';
import * as Elements from '../elements/elements.js';

import {
  authorDiagnostic,
  type Diagnostic,
  DiagnosticKind,
  flexFreeSpaceDiagnostic,
  type FlexFreeSpaceItem,
  percentageHeightDiagnostic,
  validatorDiagnostic,
} from './Diagnostics.js';
import {
  buildSummary,
  customPropertiesInValue,
  formatElementIdentity,
  type StyleIntelligenceSummary,
  tokenLabel,
} from './ElementSummary.js';
import {
  type DeclarationFact,
  type ExplanationInput,
  explainLayout,
  type LayoutExplanation,
  type ParentFacts,
  type SpecifiedFacts,
} from './LayoutExplanation.js';
import {explainSpacing, type SpacingBox, type SpacingExplanation, type SpacingParent} from './SpacingExplanation.js';

export type StyleIntelligenceLoad = {
  status: 'ok',
  summary: StyleIntelligenceSummary,
  explanation: LayoutExplanation,
  spacing: SpacingExplanation,
  diagnostics: Diagnostic[],
  declarations: Map<number, SDK.CSSProperty.CSSProperty>,
}|{status: 'unavailable'};

const ANIMATED_NAMES = [
  'width',
  'height',
  'inline-size',
  'block-size',
  'min-width',
  'max-width',
  'min-height',
  'max-height',
  'top',
  'right',
  'bottom',
  'left',
];

/**
 * Loads a node-scoped summary from CSSModel and DOMModel.
 * Does not snapshot the document.
 */
export class StyleIntelligenceModel {
  async describe(
      node: SDK.DOMModel.DOMNode,
      onPrimary?: (partial: Extract<StyleIntelligenceLoad, {status: 'ok'}>) => void,
      ): Promise<StyleIntelligenceLoad> {
    if (node.nodeType() !== Node.ELEMENT_NODE || !node.id) {
      return {status: 'unavailable'};
    }
    const cssModel = node.domModel().cssModel();
    if (!cssModel) {
      return {status: 'unavailable'};
    }

    const [computed, matched, box] = await Promise.all([
      cssModel.getComputedStyle(node.id),
      cssModel.cachedMatchedCascadeForNode(node),
      node.boxModel(),
    ]);
    if (!computed) {
      return {status: 'unavailable'};
    }

    const registry = new DeclarationRegistry();
    const specified = matched ? registry.readSpecified(matched) : emptySpecified();
    const parentLayoutNodeId = matched?.getParentLayoutNodeId();
    const parentNode = parentLayoutNodeId ? node.domModel().nodeForId(parentLayoutNodeId) ?? null : null;
    const parent = parentNode ? await readParent(parentNode, cssModel, registry) : null;
    const position = computed.get('position') ?? null;
    const scrollport = position === 'sticky' || position === '-webkit-sticky' ? findScrollport(node) : null;
    const geometry = box ? measureBox(box) : null;

    const display = computed.get('display') ?? null;
    const summary = buildSummary(
        identityForNode(node), {
          display,
          position,
          flexDirection: computed.get('flex-direction') ?? null,
          flexWrap: computed.get('flex-wrap') ?? null,
          alignItems: computed.get('align-items') ?? null,
          justifyContent: computed.get('justify-content') ?? null,
          rowGap: computed.get('row-gap') ?? computed.get('gap') ?? null,
          columnGap: computed.get('column-gap') ?? computed.get('gap') ?? null,
          gridTemplateColumns: computed.get('grid-template-columns') ?? null,
          gridTemplateRows: computed.get('grid-template-rows') ?? null,
          parentDisplay: parent?.display ?? null,
          hasBox: Boolean(box),
        },
        geometry ? {width: geometry.borderWidth, height: geometry.borderHeight} : null,
        matched ? tokenReferences(matched, computed) : []);

    const explanationInput: ExplanationInput = {
      display,
      position,
      boxSizing: computed.get('box-sizing') ?? null,
      writingMode: computed.get('writing-mode') ?? null,
      hasBox: Boolean(box),
      borderWidth: geometry?.borderWidth ?? null,
      borderHeight: geometry?.borderHeight ?? null,
      x: geometry?.x ?? null,
      y: geometry?.y ?? null,
      contentWidth: geometry?.contentWidth ?? null,
      contentHeight: geometry?.contentHeight ?? null,
      paddingTop: geometry?.paddingTop ?? 0,
      paddingRight: geometry?.paddingRight ?? 0,
      paddingBottom: geometry?.paddingBottom ?? 0,
      paddingLeft: geometry?.paddingLeft ?? 0,
      borderTop: geometry?.borderTop ?? 0,
      borderRight: geometry?.borderRight ?? 0,
      borderBottom: geometry?.borderBottom ?? 0,
      borderLeft: geometry?.borderLeft ?? 0,
      parent,
      scrollport: scrollport?.id ? {nodeId: scrollport.id, label: identityForNode(scrollport)} : null,
      specified,
      computedFlexGrow: numeric(computed.get('flex-grow')),
      computedFlexShrink: numeric(computed.get('flex-shrink')),
      computedFlexBasis: computed.get('flex-basis') ?? null,
      computedAlignSelf: computed.get('align-self') ?? null,
      computedJustifySelf: computed.get('justify-self') ?? null,
      animatedProperties: matched ? animatedNames(matched) : [],
    };
    const explanation = explainLayout(explanationInput);
    const earlyDiagnostics = await declarationAndPercentageDiagnostics(
        node, parentNode, cssModel, registry, computed, matched, explanation);
    onPrimary?.({
      status: 'ok',
      summary,
      explanation,
      spacing: {relationships: [], declined: []},
      diagnostics: earlyDiagnostics,
      declarations: registry.properties,
    });
    const spacing = await collectSpacing(node, parentNode, cssModel, registry, computed, matched, geometry);
    const freeSpace = await flexFreeSpace(node, cssModel, registry, computed, matched, geometry);
    const diagnostics = freeSpace ? [...earlyDiagnostics, freeSpace] : earlyDiagnostics;
    return {status: 'ok', summary, explanation, spacing, diagnostics, declarations: registry.properties};
  }
}

export function identityForNode(node: SDK.DOMModel.DOMNode): string {
  if (node.pseudoType()) {
    return formatElementIdentity({name: node.nodeNameInCorrectCase()});
  }
  const classAttribute = node.getAttribute('class') ?? '';
  return formatElementIdentity({
    name: node.nodeNameInCorrectCase(),
    id: node.getAttribute('id') ?? null,
    classes: classAttribute.split(/\s+/),
  });
}

class DeclarationRegistry {
  #next = 1;
  readonly properties = new Map<number, SDK.CSSProperty.CSSProperty>();

  readSpecified(matched: SDK.CSSMatchedStyles.CSSMatchedStyles): SpecifiedFacts {
    const read = (name: string): DeclarationFact|null => this.fact(activeAuthorProperty(matched, name));
    return {
      width: read('width'),
      height: read('height'),
      minWidth: read('min-width'),
      maxWidth: read('max-width'),
      minHeight: read('min-height'),
      maxHeight: read('max-height'),
      inlineSize: read('inline-size'),
      blockSize: read('block-size'),
      minInlineSize: read('min-inline-size'),
      maxInlineSize: read('max-inline-size'),
      minBlockSize: read('min-block-size'),
      maxBlockSize: read('max-block-size'),
      flexGrow: read('flex-grow'),
      flexShrink: read('flex-shrink'),
      flexBasis: read('flex-basis'),
      alignSelf: read('align-self'),
      justifySelf: read('justify-self'),
      top: read('top'),
      right: read('right'),
      bottom: read('bottom'),
      left: read('left'),
      marginTop: read('margin-top'),
      marginRight: read('margin-right'),
      marginBottom: read('margin-bottom'),
      marginLeft: read('margin-left'),
      gridColumn: read('grid-column'),
      gridRow: read('grid-row'),
    };
  }

  fact(property: SDK.CSSProperty.CSSProperty|null): DeclarationFact|null {
    if (!property) {
      return null;
    }
    const id = this.#next++;
    this.properties.set(id, property);
    const style = property.ownerStyle;
    const hasSource = style.type === SDK.CSSStyleDeclaration.Type.Regular && Boolean(style.styleSheetId) &&
        Boolean(property.valueRange());
    return {id, property: property.name, value: property.value, hasSource};
  }
}

async function readParent(
    parentNode: SDK.DOMModel.DOMNode, cssModel: SDK.CSSModel.CSSModel, registry: DeclarationRegistry,
    ): Promise<ParentFacts|null> {
  if (!parentNode.id) {
    return null;
  }
  const [computed, matched, box] = await Promise.all([
    cssModel.getComputedStyle(parentNode.id),
    cssModel.cachedMatchedCascadeForNode(parentNode),
    parentNode.boxModel(),
  ]);
  if (!computed) {
    return null;
  }
  const displayDeclaration = matched ? registry.fact(activeAuthorProperty(matched, 'display')) : null;
  const height = matched ? activeAuthorProperty(matched, 'height') : null;
  const blockSize = matched ? activeAuthorProperty(matched, 'block-size') : null;
  const content = box ? measureBox(box) : null;
  return {
    nodeId: parentNode.id,
    label: identityForNode(parentNode),
    display: computed.get('display') ?? null,
    position: computed.get('position') ?? null,
    flexDirection: computed.get('flex-direction') ?? null,
    alignItems: computed.get('align-items') ?? null,
    justifyContent: computed.get('justify-content') ?? null,
    justifyItems: computed.get('justify-items') ?? null,
    contentWidth: content?.contentWidth ?? null,
    contentHeight: content?.contentHeight ?? null,
    heightIsAuto: isAutomatic(height?.value) && isAutomatic(blockSize?.value),
    autoHeightMayBeDefinite: await parentAutoHeightMayBeDefinite(parentNode, cssModel, computed, matched),
    displayDeclaration,
    alignItemsDeclaration: matched ? registry.fact(activeAuthorProperty(matched, 'align-items')) : null,
    justifyContentDeclaration: matched ? registry.fact(activeAuthorProperty(matched, 'justify-content')) : null,
    justifyItemsDeclaration: matched ? registry.fact(activeAuthorProperty(matched, 'justify-items')) : null,
    gridTemplateColumns: matched ? registry.fact(activeAuthorProperty(matched, 'grid-template-columns')) : null,
    gridTemplateRows: matched ? registry.fact(activeAuthorProperty(matched, 'grid-template-rows')) : null,
  };
}

async function parentAutoHeightMayBeDefinite(
    parentNode: SDK.DOMModel.DOMNode, cssModel: SDK.CSSModel.CSSModel, computed: Map<string, string>,
    matched: SDK.CSSMatchedStyles.CSSMatchedStyles|null): Promise<boolean> {
  const position = computed.get('position') ?? 'static';
  const display = computed.get('display') ?? '';
  if (display === 'none' || display === 'contents' ||
      (position !== 'static' && position !== 'relative' && position !== 'sticky')) {
    return false;
  }
  const grandId = matched?.getParentLayoutNodeId();
  const grand = grandId ? parentNode.domModel().nodeForId(grandId) : null;
  if (!grand?.id) {
    return false;
  }
  const grandComputed = await cssModel.getComputedStyle(grand.id);
  const grandDisplay = grandComputed?.get('display') ?? '';
  return grandDisplay === 'flex' || grandDisplay === 'inline-flex' || grandDisplay === 'grid' ||
      grandDisplay === 'inline-grid';
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

/**
 * Implicit longhands expanded from a shorthand often have no source range.
 * The shorthand is the author declaration, so Source should reveal that.
 */
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

function animatedNames(matched: SDK.CSSMatchedStyles.CSSMatchedStyles): string[] {
  const names = new Set<string>();
  for (const style of matched.nodeStyles()) {
    for (const property of style.allProperties()) {
      if (ANIMATED_NAMES.includes(property.name) && matched.isPropertyOverriddenByAnimation(property)) {
        names.add(property.name);
      }
    }
  }
  return [...names];
}

function findScrollport(node: SDK.DOMModel.DOMNode): SDK.DOMModel.DOMNode|null {
  let current = node.parentNode;
  while (current) {
    if (current.nodeType() === Node.ELEMENT_NODE && current.isScrollable()) {
      return current;
    }
    current = current.parentNode;
  }
  return null;
}

interface MeasuredBox {
  borderWidth: number;
  borderHeight: number;
  x: number;
  y: number;
  contentWidth: number|null;
  contentHeight: number|null;
  paddingTop: number;
  paddingRight: number;
  paddingBottom: number;
  paddingLeft: number;
  borderTop: number;
  borderRight: number;
  borderBottom: number;
  borderLeft: number;
}

function measureBox(box: {
  width: number,
  height: number,
  border?: number[],
  padding?: number[],
  content?: number[],
}): MeasuredBox {
  const border = box.border;
  const padding = box.padding;
  const content = box.content;
  return {
    borderWidth: box.width,
    borderHeight: box.height,
    x: Math.round(border?.[0] ?? 0),
    y: Math.round(border?.[1] ?? 0),
    contentWidth: span(content, 0, 2),
    contentHeight: span(content, 1, 5),
    paddingTop: gap(padding, content, 1),
    paddingRight: gap(padding, content, 2),
    paddingBottom: gap(padding, content, 5),
    paddingLeft: gap(content, padding, 0),
    borderTop: gap(padding, border, 1),
    borderRight: gap(border, padding, 2),
    borderBottom: gap(border, padding, 5),
    borderLeft: gap(padding, border, 0),
  };
}

function span(quad: number[]|undefined, start: number, end: number): number|null {
  if (!quad || quad.length < 8) {
    return null;
  }
  return Math.round(Math.abs(quad[end] - quad[start]));
}

function gap(outer: number[]|undefined, inner: number[]|undefined, index: number): number {
  if (!outer || !inner || outer.length <= index || inner.length <= index) {
    return 0;
  }
  return Math.max(0, Math.round(Math.abs(outer[index] - inner[index])));
}

function numeric(value: string|undefined): number|null {
  if (value === undefined) {
    return null;
  }
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function isAutomatic(value: string|undefined): boolean {
  if (!value) {
    return true;
  }
  const normalized = value.trim().toLowerCase();
  return normalized === 'auto' || normalized === 'normal';
}

function emptySpecified(): SpecifiedFacts {
  return {
    width: null,
    height: null,
    minWidth: null,
    maxWidth: null,
    minHeight: null,
    maxHeight: null,
    inlineSize: null,
    blockSize: null,
    minInlineSize: null,
    maxInlineSize: null,
    minBlockSize: null,
    maxBlockSize: null,
    flexGrow: null,
    flexShrink: null,
    flexBasis: null,
    alignSelf: null,
    justifySelf: null,
    top: null,
    right: null,
    bottom: null,
    left: null,
    marginTop: null,
    marginRight: null,
    marginBottom: null,
    marginLeft: null,
    gridColumn: null,
    gridRow: null,
  };
}

function tokenReferences(
    matched: SDK.CSSMatchedStyles.CSSMatchedStyles,
    computed: Map<string, string>,
    ): Array<{propertyName: string, customProperty: string, resolvedValue: string|null}> {
  const references = [];
  for (const style of matched.nodeStyles()) {
    for (const property of style.allProperties()) {
      if (matched.propertyState(property) !== SDK.CSSMatchedStyles.PropertyState.ACTIVE || !property.parsedOk ||
          property.disabled) {
        continue;
      }
      if (!tokenLabel(property.name)) {
        continue;
      }
      for (const customProperty of customPropertiesInValue(property.value)) {
        references.push({
          propertyName: property.name,
          customProperty,
          resolvedValue: resolveCustomProperty(customProperty, computed, matched, property.ownerStyle),
        });
      }
    }
  }
  return references;
}

function resolveCustomProperty(
    customProperty: string, computed: Map<string, string>, matched: SDK.CSSMatchedStyles.CSSMatchedStyles,
    style: SDK.CSSStyleDeclaration.CSSStyleDeclaration): string|null {
  const fromComputed = concreteValue(computed.get(customProperty));
  if (fromComputed) {
    return fromComputed;
  }
  return concreteValue(matched.computeCSSVariable(style, customProperty)?.value ?? null);
}

const MAX_LAYOUT_CHILDREN = 24;

const CASCADE_PROPERTIES = new Set([
  'width',
  'height',
  'inline-size',
  'block-size',
  'min-width',
  'max-width',
  'min-height',
  'max-height',
  'top',
  'right',
  'bottom',
  'left',
  'z-index',
  'position',
  'margin',
  'margin-top',
  'margin-right',
  'margin-bottom',
  'margin-left',
  'padding',
  'padding-top',
  'padding-right',
  'padding-bottom',
  'padding-left',
  'gap',
  'row-gap',
  'column-gap',
  'justify-content',
  'align-items',
  'align-content',
  'align-self',
  'justify-items',
  'justify-self',
  'flex',
  'flex-grow',
  'flex-shrink',
  'flex-basis',
  'grid-column',
  'grid-row',
  'display',
]);

async function collectSpacing(
    node: SDK.DOMModel.DOMNode, parentNode: SDK.DOMModel.DOMNode|null, cssModel: SDK.CSSModel.CSSModel,
    registry: DeclarationRegistry, computed: Map<string, string>, matched: SDK.CSSMatchedStyles.CSSMatchedStyles|null,
    geometry: MeasuredBox|null): Promise<SpacingExplanation> {
  if (!geometry) {
    return {relationships: [], declined: []};
  }
  const selected = spacingBox(node, computed, matched, registry, geometry);
  let parent: SpacingParent|null = null;
  let siblings: SpacingBox[] = [];
  let siblingsIncomplete = false;
  if (parentNode) {
    parent = await readSpacingParent(parentNode, cssModel, registry);
    const flattened = await flattenLayoutChildren(parentNode, cssModel);
    const selectedIncluded = flattened.nodes.some(child => child.id === node.id);
    siblingsIncomplete = flattened.incomplete || !selectedIncluded;
    if (!siblingsIncomplete) {
      const boxes = await Promise.all(flattened.nodes.filter(child => child.id !== node.id)
                                        .map(child => readSpacingBox(child, cssModel, registry)));
      if (boxes.some(box => box === null)) {
        siblingsIncomplete = true;
      } else {
        siblings = boxes.filter((box): box is SpacingBox => box !== null);
      }
    }
  }
  return explainSpacing({
    selected,
    parent,
    siblings: siblingsIncomplete ? [] : siblings,
    siblingsIncomplete,
  });
}

async function declarationAndPercentageDiagnostics(
    node: SDK.DOMModel.DOMNode, parentNode: SDK.DOMModel.DOMNode|null, cssModel: SDK.CSSModel.CSSModel,
    registry: DeclarationRegistry, computed: Map<string, string>, matched: SDK.CSSMatchedStyles.CSSMatchedStyles|null,
    explanation: LayoutExplanation): Promise<Diagnostic[]> {
  const diagnostics: Diagnostic[] = [];
  const percentage = percentageHeightDiagnostic(explanation);
  if (percentage) {
    diagnostics.push(percentage);
  }
  if (matched && !node.isSVGNode()) {
    const parentComputed = parentNode?.id ? await cssModel.getComputedStyle(parentNode.id) : null;
    diagnostics.push(...declarationDiagnostics(node, matched, computed, parentComputed, registry, cssModel));
  }
  return diagnostics;
}

async function flexFreeSpace(
    node: SDK.DOMModel.DOMNode, cssModel: SDK.CSSModel.CSSModel, registry: DeclarationRegistry,
    computed: Map<string, string>, matched: SDK.CSSMatchedStyles.CSSMatchedStyles|null,
    geometry: MeasuredBox|null): Promise<Diagnostic|null> {
  const display = computed.get('display') ?? null;
  if (display !== 'flex' && display !== 'inline-flex') {
    return null;
  }
  const flexWrap = computed.get('flex-wrap') ?? null;
  const flexDirection = computed.get('flex-direction') ?? null;
  const justifyContent = computed.get('justify-content') ?? null;
  if (flexWrap !== 'nowrap' || !justifyAffectsFreeSpace(justifyContent)) {
    return null;
  }
  const horizontal = flexDirection === 'row' || flexDirection === 'row-reverse';
  const vertical = flexDirection === 'column' || flexDirection === 'column-reverse';
  if (!horizontal && !vertical) {
    return null;
  }
  const flattened = await flattenLayoutChildren(node, cssModel);
  const items: FlexFreeSpaceItem[] = [];
  if (!flattened.incomplete) {
    const measured = await Promise.all(flattened.nodes.map(child => readFlexItem(child, cssModel, horizontal)));
    if (measured.some(item => item === null)) {
      return flexFreeSpaceDiagnostic({
        display,
        flexDirection,
        flexWrap,
        justifyContent,
        justifyContentDeclaration: matched ? registry.fact(activeAuthorProperty(matched, 'justify-content')) : null,
        contentMainSize: horizontal ? geometry?.contentWidth ?? null : geometry?.contentHeight ?? null,
        mainGap: px(computed.get(horizontal ? 'column-gap' : 'row-gap') ?? computed.get('gap')),
        items: [],
        itemsIncomplete: true,
      });
    }
    items.push(...measured.filter((item): item is FlexFreeSpaceItem => item !== null));
  }
  return flexFreeSpaceDiagnostic({
    display,
    flexDirection,
    flexWrap,
    justifyContent,
    justifyContentDeclaration: matched ? registry.fact(activeAuthorProperty(matched, 'justify-content')) : null,
    contentMainSize: horizontal ? geometry?.contentWidth ?? null : geometry?.contentHeight ?? null,
    mainGap: px(computed.get(horizontal ? 'column-gap' : 'row-gap') ?? computed.get('gap')),
    items,
    itemsIncomplete: flattened.incomplete,
  });
}

function justifyAffectsFreeSpace(value: string|null): boolean {
  return Boolean(value) && value !== 'normal' && value !== 'flex-start' && value !== 'start' && value !== 'stretch';
}

async function readFlexItem(
    node: SDK.DOMModel.DOMNode, cssModel: SDK.CSSModel.CSSModel, horizontal: boolean): Promise<FlexFreeSpaceItem|null> {
  if (!node.id) {
    return null;
  }
  const [computed, box] = await Promise.all([cssModel.getComputedStyle(node.id), node.boxModel()]);
  if (!computed || !box) {
    return null;
  }
  const position = computed.get('position') ?? null;
  return {
    mainSize: horizontal ? box.width : box.height,
    marginStart: px(computed.get(horizontal ? 'margin-left' : 'margin-top')),
    marginEnd: px(computed.get(horizontal ? 'margin-right' : 'margin-bottom')),
    outOfFlow: position === 'absolute' || position === 'fixed',
    transformed: !transformIsNone(computed),
  };
}

function declarationDiagnostics(
    node: SDK.DOMModel.DOMNode, matched: SDK.CSSMatchedStyles.CSSMatchedStyles, computed: Map<string, string>,
    parentComputed: Map<string, string>|null, registry: DeclarationRegistry, cssModel: SDK.CSSModel.CSSModel,
    ): Diagnostic[] {
  const diagnostics: Diagnostic[] = [];
  const seen = new Set<string>();
  let cascadeCount = 0;
  const fontFaces = cssModel.fontFaces();
  const localName = node.localName()?.toLowerCase();
  for (const style of matched.nodeStyles()) {
    if (matched.isInherited(style) || style.parentRule?.isUserAgent() || style.parentRule?.isViaInspector()) {
      continue;
    }
    if (style.type !== SDK.CSSStyleDeclaration.Type.Regular && style.type !== SDK.CSSStyleDeclaration.Type.Inline) {
      continue;
    }
    for (const property of style.allProperties()) {
      if (!property.parsedOk || property.disabled || !property.value || property.name.startsWith('--')) {
        continue;
      }
      const key = property.name;
      if (matched.isPropertyOverriddenByAnimation(property)) {
        if (seen.has(key)) {
          continue;
        }
        const fact = registry.fact(authoredDeclaration(matched, property));
        if (!fact) {
          continue;
        }
        seen.add(key);
        diagnostics.push(authorDiagnostic(DiagnosticKind.ANIMATION_OVERRIDE, fact));
        continue;
      }
      if (matched.propertyState(property) === SDK.CSSMatchedStyles.PropertyState.OVERLOADED &&
          CASCADE_PROPERTIES.has(property.name)) {
        if (seen.has(key) || cascadeCount >= 8) {
          continue;
        }
        const fact = registry.fact(authoredDeclaration(matched, property));
        if (!fact || seen.has(`decl:${fact.id}`)) {
          continue;
        }
        seen.add(key);
        seen.add(`decl:${fact.id}`);
        cascadeCount++;
        diagnostics.push(authorDiagnostic(DiagnosticKind.CASCADE_OVERRIDE, fact));
        continue;
      }
      if (matched.propertyState(property) !== SDK.CSSMatchedStyles.PropertyState.ACTIVE || seen.has(key)) {
        continue;
      }
      const validators = Elements.CSSRuleValidator.cssRuleValidatorsMap.get(property.name);
      if (!validators) {
        continue;
      }
      for (const validator of validators) {
        const hint = validator.getHint(property.name, computed, parentComputed ?? undefined, localName, fontFaces);
        if (!hint) {
          continue;
        }
        const message = hintText(hint.getMessage());
        if (!message) {
          break;
        }
        const fact = registry.fact(authoredDeclaration(matched, property));
        if (!fact) {
          break;
        }
        seen.add(key);
        diagnostics.push(validatorDiagnostic(fact, message, property.name === 'font-variation-settings'));
        break;
      }
    }
  }
  return diagnostics;
}

function hintText(message: string|LitTemplate): string {
  if (typeof message === 'string') {
    return message.replace(/\s+/g, ' ').trim();
  }
  if (message === nothing) {
    return '';
  }
  const host = document.createElement('span');
  render(message, host);
  return (host.textContent ?? '').replace(/\s+/g, ' ').trim();
}

async function flattenLayoutChildren(
    node: SDK.DOMModel.DOMNode, cssModel: SDK.CSSModel.CSSModel, depth = 0): Promise<{nodes: SDK.DOMModel.DOMNode[], incomplete: boolean}> {
  if (depth > 3) {
    return {nodes: [], incomplete: true};
  }
  const children = await node.getChildNodesPromise();
  if (!children) {
    return {nodes: [], incomplete: true};
  }
  const elements = children.filter(child => child.nodeType() === Node.ELEMENT_NODE && !child.pseudoType());
  if (elements.length > MAX_LAYOUT_CHILDREN) {
    return {nodes: [], incomplete: true};
  }
  const displays = await Promise.all(elements.map(element => computedDisplay(element, cssModel)));
  const nodes: SDK.DOMModel.DOMNode[] = [];
  for (let index = 0; index < elements.length; index++) {
    const display = displays[index];
    if (display === null) {
      return {nodes: [], incomplete: true};
    }
    if (display === 'none') {
      continue;
    }
    if (display === 'contents') {
      const inner = await flattenLayoutChildren(elements[index], cssModel, depth + 1);
      if (inner.incomplete) {
        return {nodes: [], incomplete: true};
      }
      nodes.push(...inner.nodes);
      if (nodes.length > MAX_LAYOUT_CHILDREN) {
        return {nodes: [], incomplete: true};
      }
      continue;
    }
    nodes.push(elements[index]);
  }
  return {nodes, incomplete: false};
}

async function computedDisplay(node: SDK.DOMModel.DOMNode, cssModel: SDK.CSSModel.CSSModel): Promise<string|null> {
  if (!node.id) {
    return null;
  }
  const computed = await cssModel.getComputedStyle(node.id);
  return computed?.get('display') ?? null;
}

async function readSpacingParent(
    parentNode: SDK.DOMModel.DOMNode, cssModel: SDK.CSSModel.CSSModel, registry: DeclarationRegistry,
    ): Promise<SpacingParent|null> {
  if (!parentNode.id) {
    return null;
  }
  const [computed, matched, box] = await Promise.all([
    cssModel.getComputedStyle(parentNode.id),
    cssModel.cachedMatchedCascadeForNode(parentNode),
    parentNode.boxModel(),
  ]);
  if (!computed) {
    return null;
  }
  const geometry = box ? measureBox(box) : null;
  const fact = (name: string): DeclarationFact|null => matched ? registry.fact(activeAuthorProperty(matched, name)) : null;
  return {
    nodeId: parentNode.id,
    label: identityForNode(parentNode),
    hasBox: Boolean(geometry),
    display: computed.get('display') ?? null,
    flexDirection: computed.get('flex-direction') ?? null,
    flexWrap: computed.get('flex-wrap') ?? null,
    writingMode: computed.get('writing-mode') ?? null,
    x: geometry?.x ?? 0,
    y: geometry?.y ?? 0,
    width: geometry?.borderWidth ?? 0,
    height: geometry?.borderHeight ?? 0,
    borderTop: geometry?.borderTop ?? 0,
    borderRight: geometry?.borderRight ?? 0,
    borderBottom: geometry?.borderBottom ?? 0,
    borderLeft: geometry?.borderLeft ?? 0,
    paddingTop: geometry?.paddingTop ?? 0,
    paddingRight: geometry?.paddingRight ?? 0,
    paddingBottom: geometry?.paddingBottom ?? 0,
    paddingLeft: geometry?.paddingLeft ?? 0,
    rowGap: px(computed.get('row-gap')),
    columnGap: px(computed.get('column-gap')),
    rowGapDeclaration: fact('row-gap'),
    columnGapDeclaration: fact('column-gap'),
    paddingTopDeclaration: fact('padding-top'),
    paddingRightDeclaration: fact('padding-right'),
    paddingBottomDeclaration: fact('padding-bottom'),
    paddingLeftDeclaration: fact('padding-left'),
  };
}

function spacingBox(
    node: SDK.DOMModel.DOMNode, computed: Map<string, string>, matched: SDK.CSSMatchedStyles.CSSMatchedStyles|null,
    registry: DeclarationRegistry, geometry: MeasuredBox): SpacingBox {
  const fact = (name: string): DeclarationFact|null => matched ? registry.fact(activeAuthorProperty(matched, name)) : null;
  return {
    nodeId: node.id ?? -1,
    label: identityForNode(node),
    x: geometry.x,
    y: geometry.y,
    width: geometry.borderWidth,
    height: geometry.borderHeight,
    display: computed.get('display') ?? null,
    position: computed.get('position') ?? null,
    transformNone: transformIsNone(computed),
    marginTop: px(computed.get('margin-top')),
    marginRight: px(computed.get('margin-right')),
    marginBottom: px(computed.get('margin-bottom')),
    marginLeft: px(computed.get('margin-left')),
    marginTopDeclaration: fact('margin-top'),
    marginRightDeclaration: fact('margin-right'),
    marginBottomDeclaration: fact('margin-bottom'),
    marginLeftDeclaration: fact('margin-left'),
  };
}

async function readSpacingBox(
    node: SDK.DOMModel.DOMNode, cssModel: SDK.CSSModel.CSSModel, registry: DeclarationRegistry,
    ): Promise<SpacingBox|null> {
  if (!node.id) {
    return null;
  }
  const [computed, matched, box] = await Promise.all([
    cssModel.getComputedStyle(node.id),
    cssModel.cachedMatchedCascadeForNode(node),
    node.boxModel(),
  ]);
  if (!computed || !box) {
    return null;
  }
  return spacingBox(node, computed, matched, registry, measureBox(box));
}

function transformIsNone(computed: Map<string, string>): boolean {
  const transform = computed.get('transform');
  const translate = computed.get('translate');
  const rotate = computed.get('rotate');
  const scale = computed.get('scale');
  if (transform && transform !== 'none') {
    return false;
  }
  if (translate && translate !== 'none') {
    return false;
  }
  if (rotate && rotate !== 'none') {
    return false;
  }
  if (scale && scale !== 'none') {
    return false;
  }
  return true;
}

function px(value: string|undefined): number|null {
  if (value === undefined) {
    return null;
  }
  const trimmed = value.trim().toLowerCase();
  if (trimmed === 'normal' || trimmed === '0') {
    return 0;
  }
  const match = /^(-?\d+(?:\.\d+)?)px$/.exec(trimmed);
  if (!match) {
    return null;
  }
  return Math.round(Number(match[1]));
}

function concreteValue(value: string|null|undefined): string|null {
  if (!value) {
    return null;
  }
  const trimmed = value.trim();
  if (!trimmed || trimmed.includes('var(')) {
    return null;
  }
  return trimmed;
}
