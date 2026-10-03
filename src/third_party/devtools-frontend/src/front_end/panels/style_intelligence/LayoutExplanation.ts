// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Structured explanation of one element's measured box.
 *
 * The used width, height, and viewport position are measured. Mechanisms and
 * controllers explain those facts. They never replace them. parentLayoutNodeId
 * is the flat-tree layout parent (display:contents skipped). It is not the CSS
 * containing block of an absolute or fixed box, so those cases do not name one.
 */

import {EvidenceKind} from './ElementSummary.js';

export const Mechanism = {
  EXPLICIT: 'explicit',
  MIN_SIZE: 'min-size',
  MAX_SIZE: 'max-size',
  PERCENTAGE: 'percentage',
  VIEWPORT: 'viewport',
  INTRINSIC: 'intrinsic',
  BLOCK_AVAILABLE: 'block-available',
  CONTENT: 'content',
  FLEX_LAYOUT: 'flex-layout',
  FLEX_STRETCH: 'flex-stretch',
  GRID_LAYOUT: 'grid-layout',
  GRID_STRETCH: 'grid-stretch',
  INSET_DERIVED: 'inset-derived',
  BLOCK_FLOW: 'block-flow',
  FLEX_PLACEMENT: 'flex-placement',
  GRID_PLACEMENT: 'grid-placement',
  RELATIVE: 'relative',
  ABSOLUTE: 'absolute',
  FIXED: 'fixed',
  STICKY: 'sticky',
  AUTO_MARGINS: 'auto-margins',
  UNKNOWN: 'unknown',
} as const;

export type Mechanism = (typeof Mechanism)[keyof typeof Mechanism];

export const Confidence = {
  HIGH: 'high',
  LIMITED: 'limited',
} as const;

export type Confidence = (typeof Confidence)[keyof typeof Confidence];

export const ContributionRole = {
  SPECIFIES: 'specifies',
  CONTRIBUTES: 'contributes',
  CONSTRAINS: 'constrains',
  INEFFECTIVE: 'ineffective',
} as const;

export type ContributionRole = (typeof ContributionRole)[keyof typeof ContributionRole];

export interface DeclarationFact {
  id: number;
  property: string;
  value: string;
  hasSource: boolean;
}

export interface ControllerRef {
  nodeId: number;
  label: string;
  property: string|null;
  value: string|null;
  declarationId: number|null;
  hasSource: boolean;
}

export interface Contribution {
  kind: typeof EvidenceKind.MEASURED;
  property: string;
  value: string;
  declarationId: number|null;
  hasSource: boolean;
  role: ContributionRole;
}

export interface AxisExplanation {
  axis: 'width'|'height'|'x'|'y';
  /** Border-box size, or the border-box origin in viewport coordinates. */
  usedValue: number;
  mechanism: Mechanism;
  confidence: Confidence;
  controller: ControllerRef|null;
  contributions: Contribution[];
  /**
   * The used size includes this axis's padding or border, so a content-sized
   * explanation should mention them. Measured from the box model.
   */
  includesPadding: boolean;
  includesBorder: boolean;
  /**
   * An author length matches the content box rather than the border box shown
   * as usedValue. Presentation must not equate the two.
   */
  specifiedMatchesContentBox: boolean;
  /** Absolute, fixed, or an unresolved percentage whose basis cannot be named. */
  containingBlockUnknown: boolean;
}

export interface LayoutExplanation {
  axes: AxisExplanation[];
}

export interface ParentFacts {
  nodeId: number;
  label: string;
  display: string|null;
  position: string|null;
  flexDirection: string|null;
  alignItems: string|null;
  justifyContent: string|null;
  justifyItems: string|null;
  contentWidth: number|null;
  contentHeight: number|null;
  /** Author height/block-size is auto or absent. */
  heightIsAuto: boolean;
  /**
   * The parent is an in-flow flex or grid item, so an auto height can still
   * be a definite percentage basis. False when that cannot be established.
   */
  autoHeightMayBeDefinite: boolean;
  displayDeclaration: DeclarationFact|null;
  alignItemsDeclaration: DeclarationFact|null;
  justifyContentDeclaration: DeclarationFact|null;
  justifyItemsDeclaration: DeclarationFact|null;
  gridTemplateColumns: DeclarationFact|null;
  gridTemplateRows: DeclarationFact|null;
}

export interface SpecifiedFacts {
  width: DeclarationFact|null;
  height: DeclarationFact|null;
  minWidth: DeclarationFact|null;
  maxWidth: DeclarationFact|null;
  minHeight: DeclarationFact|null;
  maxHeight: DeclarationFact|null;
  inlineSize: DeclarationFact|null;
  blockSize: DeclarationFact|null;
  minInlineSize: DeclarationFact|null;
  maxInlineSize: DeclarationFact|null;
  minBlockSize: DeclarationFact|null;
  maxBlockSize: DeclarationFact|null;
  flexGrow: DeclarationFact|null;
  flexShrink: DeclarationFact|null;
  flexBasis: DeclarationFact|null;
  alignSelf: DeclarationFact|null;
  justifySelf: DeclarationFact|null;
  top: DeclarationFact|null;
  right: DeclarationFact|null;
  bottom: DeclarationFact|null;
  left: DeclarationFact|null;
  marginTop: DeclarationFact|null;
  marginRight: DeclarationFact|null;
  marginBottom: DeclarationFact|null;
  marginLeft: DeclarationFact|null;
  gridColumn: DeclarationFact|null;
  gridRow: DeclarationFact|null;
}

export interface ExplanationInput {
  display: string|null;
  position: string|null;
  boxSizing: string|null;
  writingMode: string|null;
  hasBox: boolean;
  borderWidth: number|null;
  borderHeight: number|null;
  x: number|null;
  y: number|null;
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
  parent: ParentFacts|null;
  scrollport: {nodeId: number, label: string}|null;
  specified: SpecifiedFacts;
  computedFlexGrow: number|null;
  computedFlexShrink: number|null;
  computedFlexBasis: string|null;
  computedAlignSelf: string|null;
  computedJustifySelf: string|null;
  animatedProperties: string[];
}

export function explainLayout(input: ExplanationInput): LayoutExplanation {
  if (!input.hasBox) {
    return {axes: []};
  }
  const axes = [
    explainSize('width', input),
    explainSize('height', input),
    explainPosition('x', input),
    explainPosition('y', input),
  ].filter((axis): axis is AxisExplanation => axis !== null);
  return {axes};
}

function explainSize(axis: 'width'|'height', input: ExplanationInput): AxisExplanation|null {
  const usedValue = axis === 'width' ? input.borderWidth : input.borderHeight;
  if (usedValue === null) {
    return null;
  }
  const base = emptyAxis(axis, usedValue);
  if (animated(input, axis === 'width' ? ['width', 'inline-size', 'min-width', 'max-width'] :
                                          ['height', 'block-size', 'min-height', 'max-height'])) {
    return {...base, mechanism: Mechanism.UNKNOWN, confidence: Confidence.LIMITED};
  }

  const specified = authorSize(input, axis);
  const minSpecified = authorMin(input, axis);
  const maxSpecified = authorMax(input, axis);
  const kind = classify(specified?.value ?? null);
  const content = axis === 'width' ? input.contentWidth : input.contentHeight;
  const match = lengthMatch(specified, input.boxSizing, usedValue, content);

  if (match && kind === 'length') {
    return {
      ...base,
      mechanism: Mechanism.EXPLICIT,
      confidence: Confidence.HIGH,
      contributions: [contribution(specified, ContributionRole.SPECIFIES)],
      specifiedMatchesContentBox: match === 'content' && content !== null && !nearly(content, usedValue),
    };
  }

  const constrained = minMaxConstraint(axis, input, specified, minSpecified, maxSpecified, usedValue);
  if (constrained) {
    return {...base, ...constrained};
  }

  if (kind === 'viewport' && specified) {
    return {
      ...base,
      mechanism: Mechanism.VIEWPORT,
      confidence: Confidence.HIGH,
      contributions: [contribution(specified, ContributionRole.SPECIFIES)],
    };
  }

  if (kind === 'percent' && specified) {
    const percentage = percentageSize(axis, input, specified);
    return {
      ...base,
      ...percentage,
      includesPadding: percentage.mechanism === Mechanism.CONTENT && paddingAlong(axis, input),
      includesBorder: percentage.mechanism === Mechanism.CONTENT && borderAlong(axis, input),
    };
  }

  if (kind === 'intrinsic' && specified) {
    return {
      ...base,
      mechanism: Mechanism.INTRINSIC,
      confidence: Confidence.HIGH,
      contributions: [contribution(specified, ContributionRole.SPECIFIES)],
    };
  }

  if (kind === 'other' && specified && !isFlexMain(axis, input) && !isGridItem(input)) {
    return {
      ...base,
      mechanism: Mechanism.EXPLICIT,
      confidence: Confidence.LIMITED,
      contributions: [contribution(specified, ContributionRole.CONTRIBUTES)],
    };
  }

  if (input.position === null) {
    return {...base, mechanism: Mechanism.UNKNOWN, confidence: Confidence.LIMITED};
  }

  if (isFlexItem(input)) {
    const flex = flexSize(axis, input, specified);
    if (flex) {
      return {...base, ...flex};
    }
    return {
      ...base,
      mechanism: Mechanism.CONTENT,
      confidence: Confidence.HIGH,
      includesPadding: paddingAlong(axis, input),
      includesBorder: borderAlong(axis, input),
    };
  }

  if (isGridItem(input)) {
    return {...base, ...gridSize(axis, input, specified)};
  }

  if (isOutOfFlow(input.position)) {
    return {...base, ...outOfFlowSize(axis, input, specified)};
  }

  if (axis === 'width' && isBlockFilling(input)) {
    return {
      ...base,
      mechanism: Mechanism.BLOCK_AVAILABLE,
      confidence: blockParentKnown(input) ? Confidence.HIGH : Confidence.LIMITED,
      controller: blockParentKnown(input) ? displayController(input.parent) : null,
      containingBlockUnknown: !blockParentKnown(input),
    };
  }

  if (isInline(input) || input.display === 'inline-block' || axis === 'height') {
    return {
      ...base,
      mechanism: Mechanism.CONTENT,
      confidence: isInline(input) ? Confidence.LIMITED : Confidence.HIGH,
      includesPadding: paddingAlong(axis, input),
      includesBorder: borderAlong(axis, input),
    };
  }

  return {...base, mechanism: Mechanism.UNKNOWN, confidence: Confidence.LIMITED};
}

function explainPosition(axis: 'x'|'y', input: ExplanationInput): AxisExplanation|null {
  const usedValue = axis === 'x' ? input.x : input.y;
  if (usedValue === null) {
    return null;
  }
  const base = emptyAxis(axis, usedValue);
  const startName = axis === 'x' ? 'left' : 'top';
  const endName = axis === 'x' ? 'right' : 'bottom';
  if (animated(input, [startName, endName])) {
    return {...base, mechanism: Mechanism.UNKNOWN, confidence: Confidence.LIMITED};
  }

  const start = input.specified[startName];
  const end = input.specified[endName];
  const offsets = [start, end].filter((item): item is DeclarationFact => Boolean(item) && !isAuto(item?.value ?? null));

  if (input.position === 'absolute' || input.position === 'fixed') {
    return {
      ...base,
      mechanism: input.position === 'absolute' ? Mechanism.ABSOLUTE : Mechanism.FIXED,
      confidence: Confidence.HIGH,
      contributions: offsets.map(item => contribution(item, ContributionRole.CONTRIBUTES)),
      containingBlockUnknown: true,
    };
  }

  if (input.position === 'sticky' || input.position === '-webkit-sticky') {
    return {
      ...base,
      mechanism: Mechanism.STICKY,
      confidence: input.scrollport ? Confidence.LIMITED : Confidence.HIGH,
      controller: input.scrollport ? {nodeId: input.scrollport.nodeId, label: input.scrollport.label, property: null, value: null, declarationId: null, hasSource: false} : null,
      contributions: offsets.map(item => contribution(item, ContributionRole.CONTRIBUTES)),
      containingBlockUnknown: !input.scrollport,
    };
  }

  const relativeOffset = nonZeroOffsets(offsets);
  if (input.position === 'relative' && relativeOffset.length) {
    return {
      ...base,
      mechanism: Mechanism.RELATIVE,
      confidence: Confidence.HIGH,
      controller: placementController(input),
      contributions: relativeOffset.map(item => contribution(item, ContributionRole.CONTRIBUTES)),
    };
  }

  const autoMargins = autoMarginContribution(axis, input);
  if (autoMargins) {
    return {...base, ...autoMargins};
  }

  if (isFlexItem(input)) {
    return {...base, ...flexPlacement(axis, input)};
  }
  if (isGridItem(input)) {
    return {...base, ...gridPlacement(axis, input)};
  }
  if (input.position === null) {
    return {...base, mechanism: Mechanism.UNKNOWN, confidence: Confidence.LIMITED};
  }
  return {
    ...base,
    mechanism: Mechanism.BLOCK_FLOW,
    confidence: blockParentKnown(input) ? Confidence.HIGH : Confidence.LIMITED,
    controller: blockParentKnown(input) ? displayController(input.parent) : null,
    containingBlockUnknown: !blockParentKnown(input),
  };
}

function percentageSize(
    axis: 'width'|'height', input: ExplanationInput, specified: DeclarationFact,
    ): Pick<AxisExplanation, 'mechanism'|'confidence'|'controller'|'contributions'|'containingBlockUnknown'> {
  const percent = parsePercent(specified.value);
  const basis = axis === 'width' ? input.parent?.contentWidth ?? null : input.parent?.contentHeight ?? null;
  const actual = axis === 'width' ? input.contentWidth : input.contentHeight;
  const border = axis === 'width' ? input.borderWidth : input.borderHeight;
  const matches = percent !== null && basis !== null &&
      ((actual !== null && nearly(actual, basis * percent / 100)) ||
       (input.boxSizing === 'border-box' && border !== null && nearly(border, basis * percent / 100)));

  if (isOutOfFlow(input.position) || input.position === null) {
    return {
      mechanism: Mechanism.PERCENTAGE,
      confidence: Confidence.LIMITED,
      controller: null,
      contributions: [contribution(specified, ContributionRole.CONTRIBUTES)],
      containingBlockUnknown: true,
    };
  }

  if (axis === 'height' && input.parent?.heightIsAuto && !isFlexItem(input) && !isGridItem(input) &&
      isInFlowPosition(input.position) && !(input.parent.autoHeightMayBeDefinite && matches)) {
    return {
      mechanism: Mechanism.CONTENT,
      confidence: Confidence.HIGH,
      controller: null,
      contributions: [contribution(specified, ContributionRole.INEFFECTIVE)],
      containingBlockUnknown: false,
    };
  }

  if (matches && input.parent && blockParentKnown(input)) {
    return {
      mechanism: Mechanism.PERCENTAGE,
      confidence: Confidence.HIGH,
      controller: displayController(input.parent),
      contributions: [contribution(specified, ContributionRole.SPECIFIES)],
      containingBlockUnknown: false,
    };
  }

  return {
    mechanism: Mechanism.PERCENTAGE,
    confidence: Confidence.LIMITED,
    controller: input.parent && !isOutOfFlow(input.position) ? displayController(input.parent) : null,
    contributions: [contribution(specified, ContributionRole.CONTRIBUTES)],
    containingBlockUnknown: !input.parent,
  };
}

function flexSize(
    axis: 'width'|'height', input: ExplanationInput, specified: DeclarationFact|null,
    ): Pick<AxisExplanation, 'mechanism'|'confidence'|'controller'|'contributions'|'containingBlockUnknown'>|null {
  const mainHorizontal = flexMainIsHorizontal(input.parent?.flexDirection ?? null);
  if (mainHorizontal === null) {
    return {
      mechanism: Mechanism.FLEX_LAYOUT,
      confidence: Confidence.LIMITED,
      controller: displayController(input.parent),
      contributions: flexContributions(input, specified),
      containingBlockUnknown: false,
    };
  }
  const mainAxis = mainHorizontal ? 'width' : 'height';
  if (axis !== mainAxis && stretches(axis, input)) {
    return {
      mechanism: Mechanism.FLEX_STRETCH,
      confidence: Confidence.HIGH,
      controller: displayController(input.parent),
      contributions: stretchContributions(input),
      containingBlockUnknown: false,
    };
  }
  if (axis !== mainAxis) {
    return null;
  }
  return {
    mechanism: Mechanism.FLEX_LAYOUT,
    confidence: Confidence.HIGH,
    controller: displayController(input.parent),
    contributions: flexContributions(input, specified),
    containingBlockUnknown: false,
  };
}

function gridSize(
    axis: 'width'|'height', input: ExplanationInput, specified: DeclarationFact|null,
    ): Pick<AxisExplanation, 'mechanism'|'confidence'|'controller'|'contributions'|'containingBlockUnknown'> {
  if (stretches(axis, input)) {
    return {
      mechanism: Mechanism.GRID_STRETCH,
      confidence: Confidence.HIGH,
      controller: displayController(input.parent),
      contributions: gridTrackContribution(axis, input),
      containingBlockUnknown: false,
    };
  }
  return {
    mechanism: Mechanism.GRID_LAYOUT,
    confidence: Confidence.HIGH,
    controller: displayController(input.parent),
    contributions: [...(specified ? [contribution(specified, ContributionRole.CONTRIBUTES)] : []), ...gridTrackContribution(axis, input)],
    containingBlockUnknown: false,
  };
}

function outOfFlowSize(
    axis: 'width'|'height', input: ExplanationInput, specified: DeclarationFact|null,
    ): Pick<AxisExplanation, 'mechanism'|'confidence'|'controller'|'contributions'|'containingBlockUnknown'> {
  const start = axis === 'width' ? input.specified.left : input.specified.top;
  const end = axis === 'width' ? input.specified.right : input.specified.bottom;
  const bothInsets = start && end && !isAuto(start.value) && !isAuto(end.value);
  if ((specified === null || isAuto(specified.value)) && bothInsets) {
    return {
      mechanism: Mechanism.INSET_DERIVED,
      confidence: Confidence.HIGH,
      controller: null,
      contributions: [contribution(start, ContributionRole.CONTRIBUTES), contribution(end, ContributionRole.CONTRIBUTES)],
      containingBlockUnknown: true,
    };
  }
  return {
    mechanism: Mechanism.INTRINSIC,
    confidence: Confidence.LIMITED,
    controller: null,
    contributions: specified && !isAuto(specified.value) ? [contribution(specified, ContributionRole.CONTRIBUTES)] : [],
    containingBlockUnknown: true,
  };
}

function flexPlacement(
    axis: 'x'|'y', input: ExplanationInput,
    ): Pick<AxisExplanation, 'mechanism'|'confidence'|'controller'|'contributions'|'containingBlockUnknown'> {
  const mainHorizontal = flexMainIsHorizontal(input.parent?.flexDirection ?? null);
  const onMain = mainHorizontal === null ? null : (axis === 'x') === mainHorizontal;
  let controller = displayController(input.parent);
  const contributions: Contribution[] = [];
  if (onMain === true) {
    const justify = input.parent?.justifyContentDeclaration;
    if (justify && !isDefaultJustify(justify.value)) {
      controller = declarationController(input.parent, justify);
    }
  } else if (onMain === false) {
    const self = input.specified.alignSelf;
    if (self && !isAuto(self.value) && !isDefaultAlign(self.value)) {
      contributions.push(contribution(self, ContributionRole.CONTRIBUTES));
    } else {
      const align = input.parent?.alignItemsDeclaration ?? null;
      if (align && !isDefaultAlign(align.value)) {
        controller = declarationController(input.parent, align);
      }
    }
  }
  return {
    mechanism: Mechanism.FLEX_PLACEMENT,
    confidence: mainHorizontal === null ? Confidence.LIMITED : Confidence.HIGH,
    controller,
    contributions,
    containingBlockUnknown: false,
  };
}

function gridPlacement(
    axis: 'x'|'y', input: ExplanationInput,
    ): Pick<AxisExplanation, 'mechanism'|'confidence'|'controller'|'contributions'|'containingBlockUnknown'> {
  const placement = axis === 'x' ? input.specified.gridColumn : input.specified.gridRow;
  const self = axis === 'x' ? input.specified.justifySelf : input.specified.alignSelf;
  const contributions: Contribution[] = [];
  if (placement && !isAuto(placement.value) && placement.value.trim() !== 'auto / auto') {
    contributions.push(contribution(placement, ContributionRole.CONTRIBUTES));
  }
  if (self && !isAuto(self.value) && self.value !== 'stretch') {
    contributions.push(contribution(self, ContributionRole.CONTRIBUTES));
  }
  return {
    mechanism: Mechanism.GRID_PLACEMENT,
    confidence: Confidence.HIGH,
    controller: displayController(input.parent),
    contributions,
    containingBlockUnknown: false,
  };
}

function autoMarginContribution(
    axis: 'x'|'y', input: ExplanationInput,
    ): Pick<AxisExplanation, 'mechanism'|'confidence'|'controller'|'contributions'|'containingBlockUnknown'>|null {
  const start = axis === 'x' ? input.specified.marginLeft : input.specified.marginTop;
  const end = axis === 'x' ? input.specified.marginRight : input.specified.marginBottom;
  if (!start || !end || !isAuto(start.value) || !isAuto(end.value)) {
    return null;
  }
  if (axis === 'y' && !isFlexItem(input) && !isGridItem(input)) {
    return null;
  }
  const sizeAxis = axis === 'x' ? 'width' : 'height';
  if (sizeIsAuto(input, sizeAxis) && !isFlexItem(input) && !isGridItem(input)) {
    return null;
  }
  return {
    mechanism: Mechanism.AUTO_MARGINS,
    confidence: Confidence.HIGH,
    controller: null,
    contributions: [contribution(start, ContributionRole.CONTRIBUTES), contribution(end, ContributionRole.CONTRIBUTES)],
    containingBlockUnknown: false,
  };
}

function minMaxConstraint(
    axis: 'width'|'height', input: ExplanationInput, specified: DeclarationFact|null,
    minSpecified: DeclarationFact|null, maxSpecified: DeclarationFact|null, usedValue: number,
    ): Pick<AxisExplanation, 'mechanism'|'confidence'|'controller'|'contributions'|'containingBlockUnknown'|'specifiedMatchesContentBox'>|
    null {
  const maxPx = maxSpecified ? pixels(maxSpecified.value) : null;
  const minPx = minSpecified ? pixels(minSpecified.value) : null;
  const specifiedPx = specified ? pixels(specified.value) : null;
  const content = axis === 'width' ? input.contentWidth : input.contentHeight;
  const compared = input.boxSizing === 'border-box' ? usedValue : (content ?? usedValue);
  if (maxPx !== null && maxSpecified && nearly(compared, maxPx) && specifiedPx !== null && specifiedPx > maxPx + 1) {
    return {
      mechanism: Mechanism.MAX_SIZE,
      confidence: Confidence.HIGH,
      controller: null,
      contributions: [contribution(maxSpecified, ContributionRole.CONSTRAINS), ...(specified ? [contribution(specified, ContributionRole.CONTRIBUTES)] : [])],
      containingBlockUnknown: false,
      specifiedMatchesContentBox: false,
    };
  }
  if (minPx !== null && minSpecified && nearly(compared, minPx) && specifiedPx !== null && specifiedPx + 1 < minPx) {
    return {
      mechanism: Mechanism.MIN_SIZE,
      confidence: Confidence.HIGH,
      controller: null,
      contributions: [contribution(minSpecified, ContributionRole.CONSTRAINS), ...(specified ? [contribution(specified, ContributionRole.CONTRIBUTES)] : [])],
      containingBlockUnknown: false,
      specifiedMatchesContentBox: false,
    };
  }
  const grow = input.computedFlexGrow ?? 0;
  const parentRoom = axis === 'width' ? input.parent?.contentWidth ?? null : input.parent?.contentHeight ?? null;
  if (maxPx !== null && maxSpecified && nearly(compared, maxPx) && isFlexItem(input) && grow > 0 && parentRoom !== null &&
      parentRoom > compared + 1) {
    return {
      mechanism: Mechanism.MAX_SIZE,
      confidence: Confidence.LIMITED,
      controller: displayController(input.parent),
      contributions: [contribution(maxSpecified, ContributionRole.CONSTRAINS), ...flexContributions(input, specified)],
      containingBlockUnknown: false,
      specifiedMatchesContentBox: false,
    };
  }
  return null;
}

function flexContributions(input: ExplanationInput, specified: DeclarationFact|null): Contribution[] {
  const contributions: Contribution[] = [];
  const grow = input.computedFlexGrow ?? 0;
  if (input.specified.flexGrow && grow > 0) {
    contributions.push(contribution(input.specified.flexGrow, ContributionRole.CONTRIBUTES));
  } else if (input.specified.flexBasis && !isAuto(input.specified.flexBasis.value)) {
    contributions.push(contribution(input.specified.flexBasis, ContributionRole.CONTRIBUTES));
  }
  if (specified && !isAuto(specified.value) && specified.property !== 'flex-basis') {
    contributions.push(contribution(specified, ContributionRole.CONTRIBUTES));
  }
  if (input.specified.flexShrink && (input.computedFlexShrink ?? 1) !== 1) {
    contributions.push(contribution(input.specified.flexShrink, ContributionRole.CONTRIBUTES));
  }
  return contributions;
}

function gridTrackContribution(axis: 'width'|'height', input: ExplanationInput): Contribution[] {
  const track = axis === 'width' ? input.parent?.gridTemplateColumns : input.parent?.gridTemplateRows;
  return track ? [contribution(track, ContributionRole.CONTRIBUTES)] : [];
}

function stretches(axis: 'width'|'height', input: ExplanationInput): boolean {
  if (!sizeIsAuto(input, axis)) {
    return false;
  }
  const vertical = isVerticalWriting(input.writingMode);
  const inlineAxis = vertical === true ? 'height' : 'width';
  if (isFlexItem(input)) {
    const self = input.computedAlignSelf;
    if (self === 'stretch' || self === 'normal') {
      return true;
    }
    if (self !== 'auto' && self !== null) {
      return false;
    }
    const align = input.parent?.alignItems ?? null;
    return align === null || align === 'stretch' || align === 'normal';
  }
  if (isGridItem(input)) {
    if (axis === inlineAxis || vertical === null && axis === 'width') {
      const self = input.computedJustifySelf;
      if (self === 'stretch' || self === 'normal') {
        return true;
      }
      if (self !== 'auto' && self !== null) {
        return false;
      }
      const items = input.parent?.justifyItems ?? null;
      return items === null || items === 'stretch' || items === 'normal' || items === 'legacy';
    }
    const self = input.computedAlignSelf;
    if (self === 'stretch' || self === 'normal') {
      return true;
    }
    if (self !== 'auto' && self !== null) {
      return false;
    }
    const align = input.parent?.alignItems ?? null;
    return align === null || align === 'stretch' || align === 'normal';
  }
  return false;
}

function stretchContributions(input: ExplanationInput): Contribution[] {
  const self = input.specified.alignSelf;
  if (self && !isAuto(self.value)) {
    return [contribution(self, ContributionRole.CONTRIBUTES)];
  }
  const align = input.parent?.alignItemsDeclaration;
  if (align && !isDefaultAlign(align.value)) {
    return [contribution(align, ContributionRole.CONTRIBUTES)];
  }
  return [];
}

function placementController(input: ExplanationInput): ControllerRef|null {
  if (isFlexItem(input) || isGridItem(input) || blockParentKnown(input)) {
    return displayController(input.parent);
  }
  return null;
}

function displayController(parent: ParentFacts|null): ControllerRef|null {
  if (!parent || !parent.display) {
    return null;
  }
  const declaration = parent.displayDeclaration;
  return {
    nodeId: parent.nodeId,
    label: parent.label,
    property: 'display',
    value: parent.display,
    declarationId: declaration?.id ?? null,
    hasSource: declaration?.hasSource ?? false,
  };
}

function declarationController(parent: ParentFacts|null, declaration: DeclarationFact): ControllerRef|null {
  if (!parent) {
    return null;
  }
  return {
    nodeId: parent.nodeId,
    label: parent.label,
    property: declaration.property,
    value: declaration.value,
    declarationId: declaration.id,
    hasSource: declaration.hasSource,
  };
}

function emptyAxis(axis: AxisExplanation['axis'], usedValue: number): AxisExplanation {
  return {
    axis,
    usedValue,
    mechanism: Mechanism.UNKNOWN,
    confidence: Confidence.LIMITED,
    controller: null,
    contributions: [],
    includesPadding: false,
    includesBorder: false,
    specifiedMatchesContentBox: false,
    containingBlockUnknown: false,
  };
}

function authorSize(input: ExplanationInput, axis: 'width'|'height'): DeclarationFact|null {
  const vertical = isVerticalWriting(input.writingMode);
  if (vertical === null) {
    return axis === 'width' ? input.specified.width : input.specified.height;
  }
  if (axis === 'width') {
    return (vertical ? input.specified.blockSize : input.specified.inlineSize) ?? input.specified.width;
  }
  return (vertical ? input.specified.inlineSize : input.specified.blockSize) ?? input.specified.height;
}

function authorMin(input: ExplanationInput, axis: 'width'|'height'): DeclarationFact|null {
  const vertical = isVerticalWriting(input.writingMode);
  if (axis === 'width') {
    return (vertical ? input.specified.minBlockSize : input.specified.minInlineSize) ?? input.specified.minWidth;
  }
  return (vertical ? input.specified.minInlineSize : input.specified.minBlockSize) ?? input.specified.minHeight;
}

function authorMax(input: ExplanationInput, axis: 'width'|'height'): DeclarationFact|null {
  const vertical = isVerticalWriting(input.writingMode);
  if (axis === 'width') {
    return (vertical ? input.specified.maxBlockSize : input.specified.maxInlineSize) ?? input.specified.maxWidth;
  }
  return (vertical ? input.specified.maxInlineSize : input.specified.maxBlockSize) ?? input.specified.maxHeight;
}

function sizeIsAuto(input: ExplanationInput, axis: 'width'|'height'): boolean {
  const specified = authorSize(input, axis);
  return !specified || isAuto(specified.value);
}

function lengthMatch(
    specified: DeclarationFact|null, boxSizing: string|null, border: number, content: number|null,
    ): 'border'|'content'|null {
  if (!specified) {
    return null;
  }
  const px = pixels(specified.value);
  if (px === null) {
    return null;
  }
  if (boxSizing === 'border-box' && nearly(border, px)) {
    return 'border';
  }
  if ((boxSizing === 'content-box' || boxSizing === null) && content !== null && nearly(content, px)) {
    return 'content';
  }
  return null;
}

function isFlexMain(axis: 'width'|'height', input: ExplanationInput): boolean {
  if (!isFlexItem(input)) {
    return false;
  }
  const mainHorizontal = flexMainIsHorizontal(input.parent?.flexDirection ?? null);
  if (mainHorizontal === null) {
    return true;
  }
  return mainHorizontal ? axis === 'width' : axis === 'height';
}

function isFlexItem(input: ExplanationInput): boolean {
  if (input.display === 'none' || input.display === 'contents') {
    return false;
  }
  if (!isFlexDisplay(input.parent?.display ?? null)) {
    return false;
  }
  return isInFlowPosition(input.position);
}

function isGridItem(input: ExplanationInput): boolean {
  if (input.display === 'none' || input.display === 'contents') {
    return false;
  }
  if (!isGridDisplay(input.parent?.display ?? null)) {
    return false;
  }
  return isInFlowPosition(input.position);
}

function isBlockFilling(input: ExplanationInput): boolean {
  return input.display === 'block' || input.display === 'flow-root' || input.display === 'list-item';
}

function blockParentKnown(input: ExplanationInput): boolean {
  const display = input.parent?.display ?? null;
  return display === 'block' || display === 'flow-root' || display === 'list-item' || display === 'inline-block' ||
      isFlexDisplay(display) || isGridDisplay(display);
}

function isInline(input: ExplanationInput): boolean {
  return input.display === 'inline';
}

function isOutOfFlow(position: string|null): boolean {
  return position === 'absolute' || position === 'fixed';
}

function isInFlowPosition(position: string|null): boolean {
  return position === 'static' || position === 'relative' || position === 'sticky' || position === '-webkit-sticky';
}

function isFlexDisplay(display: string|null): boolean {
  return display === 'flex' || display === 'inline-flex';
}

function isGridDisplay(display: string|null): boolean {
  return display === 'grid' || display === 'inline-grid' || display === 'grid-lanes' || display === 'inline-grid-lanes';
}

function flexMainIsHorizontal(direction: string|null): boolean|null {
  switch (direction) {
    case 'row':
    case 'row-reverse':
      return true;
    case 'column':
    case 'column-reverse':
      return false;
    default:
      return null;
  }
}

function isVerticalWriting(writingMode: string|null): boolean|null {
  if (!writingMode) {
    return null;
  }
  if (writingMode === 'horizontal-tb') {
    return false;
  }
  if (writingMode === 'vertical-rl' || writingMode === 'vertical-lr' || writingMode === 'sideways-rl' ||
      writingMode === 'sideways-lr') {
    return true;
  }
  return null;
}

function paddingAlong(axis: 'width'|'height', input: ExplanationInput): boolean {
  if (axis === 'width') {
    return input.paddingLeft + input.paddingRight >= 1;
  }
  return input.paddingTop + input.paddingBottom >= 1;
}

function borderAlong(axis: 'width'|'height', input: ExplanationInput): boolean {
  if (axis === 'width') {
    return input.borderLeft + input.borderRight >= 1;
  }
  return input.borderTop + input.borderBottom >= 1;
}

function animated(input: ExplanationInput, names: string[]): boolean {
  return names.some(name => input.animatedProperties.includes(name));
}

function contribution(declaration: DeclarationFact|null, role: ContributionRole): Contribution {
  return {
    kind: EvidenceKind.MEASURED,
    property: declaration?.property ?? '',
    value: declaration?.value ?? '',
    declarationId: declaration?.id ?? null,
    hasSource: declaration?.hasSource ?? false,
    role,
  };
}

function nonZeroOffsets(offsets: DeclarationFact[]): DeclarationFact[] {
  return offsets.filter(item => {
    const px = pixels(item.value);
    return px === null || Math.abs(px) >= 0.5;
  });
}

function classify(value: string|null): 'absent'|'auto'|'none'|'length'|'percent'|'viewport'|'intrinsic'|'other' {
  if (value === null) {
    return 'absent';
  }
  const normalized = value.trim().toLowerCase();
  if (normalized === 'auto' || normalized === 'normal') {
    return 'auto';
  }
  if (normalized === 'none') {
    return 'none';
  }
  if (normalized === 'max-content' || normalized === 'min-content' || normalized === 'fit-content' ||
      normalized.startsWith('fit-content(')) {
    return 'intrinsic';
  }
  if (/^-?\d*\.?\d+%$/.test(normalized)) {
    return 'percent';
  }
  if (/(vw|vh|dvw|dvh|svw|svh|lvw|lvh|vmin|vmax|vb|vi|cqw|cqh|cqi|cqb)\b/.test(normalized)) {
    return 'viewport';
  }
  if (/^-?\d*\.?\d+(px|em|rem|ch|ex|lh|rlh|cap|ic|cm|mm|in|pt|pc|q)$/.test(normalized)) {
    return 'length';
  }
  return 'other';
}

function pixels(value: string): number|null {
  const match = value.trim().match(/^(-?\d*\.?\d+)px$/i);
  if (!match) {
    return null;
  }
  return Number(match[1]);
}

function parsePercent(value: string): number|null {
  const match = value.trim().match(/^(-?\d*\.?\d+)%$/);
  if (!match) {
    return null;
  }
  return Number(match[1]);
}

function isAuto(value: string|null): boolean {
  if (value === null) {
    return true;
  }
  const normalized = value.trim().toLowerCase();
  return normalized === 'auto' || normalized === 'normal' || normalized === 'none';
}

function isDefaultJustify(value: string): boolean {
  return value === 'normal' || value === 'flex-start' || value === 'start';
}

function isDefaultAlign(value: string): boolean {
  return value === 'normal' || value === 'stretch';
}

function nearly(left: number, right: number): boolean {
  return Math.abs(left - right) <= 1;
}

export interface AxisCopy {
  label: string;
  value: string;
  headline: string;
  caveat: string|null;
}

export function presentAxis(axis: AxisExplanation): AxisCopy {
  return {
    label: axisLabel(axis.axis),
    value: axisValue(axis),
    headline: axisHeadline(axis),
    caveat: axisCaveat(axis),
  };
}

function axisLabel(axis: AxisExplanation['axis']): string {
  switch (axis) {
    case 'width':
      return 'Width';
    case 'height':
      return 'Height';
    case 'x':
      return 'X position';
    case 'y':
      return 'Y position';
  }
}

function axisValue(axis: AxisExplanation): string {
  return `${axis.usedValue}px`;
}

function axisHeadline(axis: AxisExplanation): string {
  const specified = axis.contributions.find(item => item.role === ContributionRole.SPECIFIES);
  const limited = axis.confidence === Confidence.LIMITED;
  switch (axis.mechanism) {
    case Mechanism.EXPLICIT:
      if (specified && axis.specifiedMatchesContentBox) {
        return `Content size specified as ${specified.value}`;
      }
      if (specified) {
        return `Specified as ${specified.value}`;
      }
      return 'Specified size';
    case Mechanism.MIN_SIZE:
      return limited ? 'Appears to be limited by a minimum size' : 'Limited by a minimum size';
    case Mechanism.MAX_SIZE:
      return limited ? 'Appears to be limited by a maximum size' : 'Limited by a maximum size';
    case Mechanism.PERCENTAGE:
      return limited ? 'Appears to be a percentage of the containing block' : 'Percentage of the containing block';
    case Mechanism.VIEWPORT:
      return 'Viewport-relative';
    case Mechanism.INTRINSIC:
      return limited ? 'Appears to be sized to its content' : 'Sized to its content';
    case Mechanism.BLOCK_AVAILABLE:
      return limited ? 'Appears to fill the available space' : 'Fills the available space';
    case Mechanism.CONTENT:
      return contentHeadline(axis);
    case Mechanism.FLEX_LAYOUT:
      return limited ? 'Appears to be set by the parent flex layout' : 'Set by the parent flex layout';
    case Mechanism.FLEX_STRETCH:
      return 'Stretches to the flex line';
    case Mechanism.GRID_LAYOUT:
      return 'Set by the parent grid';
    case Mechanism.GRID_STRETCH:
      return 'Stretches to the grid area';
    case Mechanism.INSET_DERIVED:
      return axis.axis === 'height' ? 'Derived from the vertical insets' : 'Derived from the horizontal insets';
    case Mechanism.BLOCK_FLOW:
      return limited ? 'Appears to be in normal flow' : 'Normal flow';
    case Mechanism.FLEX_PLACEMENT:
      return placementHeadline(axis, limited, 'flex');
    case Mechanism.GRID_PLACEMENT:
      return placementHeadline(axis, limited, 'grid');
    case Mechanism.RELATIVE:
      return 'Offset from its in-flow position';
    case Mechanism.ABSOLUTE:
      return 'Absolutely positioned';
    case Mechanism.FIXED:
      return 'Fixed positioning';
    case Mechanism.STICKY:
      return axis.controller ? 'Appears to be constrained by a scrollport' : 'Sticky';
    case Mechanism.AUTO_MARGINS:
      return 'Centered by auto margins';
    case Mechanism.UNKNOWN:
      return axis.axis === 'x' || axis.axis === 'y' ? 'Could not identify what controls this position.' :
                                                       'Could not identify what controls this size.';
  }
}

function placementHeadline(axis: AxisExplanation, limited: boolean, kind: 'flex'|'grid'): string {
  const property = axis.controller?.property;
  const value = axis.controller?.value ?? '';
  const centers = property === 'align-items' || property === 'align-self' || property === 'justify-content' ||
      property === 'justify-self' || property === 'justify-items';
  if (centers && value.trim() === 'center') {
    return limited ? 'Appears to be centered by the parent' : 'Centered by the parent';
  }
  if (kind === 'grid') {
    return 'Placed by the parent grid';
  }
  return limited ? 'Appears to be placed by the parent flex layout' : 'Placed by the parent flex layout';
}

function contentHeadline(axis: AxisExplanation): string {
  const vertical = axis.axis === 'height' || axis.axis === 'y';
  if (axis.includesPadding && axis.includesBorder) {
    return vertical ? 'Content + vertical padding and border' : 'Content + horizontal padding and border';
  }
  if (axis.includesPadding) {
    return vertical ? 'Content + vertical padding' : 'Content + horizontal padding';
  }
  if (axis.includesBorder) {
    return vertical ? 'Content + vertical border' : 'Content + horizontal border';
  }
  return vertical ? 'Content height' : 'Content width';
}

function axisCaveat(axis: AxisExplanation): string|null {
  const ineffective = axis.contributions.find(item => item.role === ContributionRole.INEFFECTIVE);
  if (ineffective) {
    return `${ineffective.property}: ${ineffective.value} does not set this size. The parent height is auto.`;
  }
  if (axis.mechanism === Mechanism.STICKY && !axis.controller) {
    return 'The scrollport could not be identified confidently.';
  }
  if (axis.containingBlockUnknown && axis.mechanism !== Mechanism.STICKY) {
    return 'The containing block could not be identified confidently.';
  }
  return null;
}
