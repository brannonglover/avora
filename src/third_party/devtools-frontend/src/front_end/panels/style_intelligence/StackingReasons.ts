// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * CSS-visible stacking-context predicate.
 *
 * Mirrors the conditions Blink exposes through computed style:
 * StyleAdjuster::AdjustComputedStyle (z-index, root) and
 * ComputedStyle::CalculateIsStackingContextWithoutContainment, plus
 * layout/paint containment on boxes that are eligible for it.
 *
 * Several properties can match at once. Every match is returned.
 * Blink-internal causes (compositor animations, overscroll, replaced
 * elements) are not invented here. A later snapshot can mark those.
 *
 * display:contents and display:none do not generate a box, so they do
 * not create a stacking context. The document element does, because
 * Blink forces one for the root.
 */

export const StackingConfidence = {
  HIGH: 'high',
  LIMITED: 'limited',
} as const;

export type StackingConfidence = (typeof StackingConfidence)[keyof typeof StackingConfidence];

export interface StackingDeclaration {
  id: number;
  value: string;
  hasSource?: boolean;
}

export interface StackingReasonInput {
  display: string|null;
  position: string|null;
  zIndex: string|null;
  opacity: string|null;
  transform: string|null;
  translate: string|null;
  rotate: string|null;
  scale: string|null;
  offsetPath: string|null;
  offsetPosition: string|null;
  transformStyle: string|null;
  perspective: string|null;
  filter: string|null;
  backdropFilter: string|null;
  clipPath: string|null;
  maskImage: string|null;
  webkitMaskImage: string|null;
  isolation: string|null;
  mixBlendMode: string|null;
  contain: string|null;
  willChange: string|null;
  viewTransitionName: string|null;
  webkitBoxReflect: string|null;
  /** Computed display of the flat-tree layout parent. Not the DOM parent. */
  layoutParentDisplay: string|null;
  isDocumentElement: boolean;
  declarations?: Partial<Record<string, StackingDeclaration>>;
}

export interface StackingReason {
  property: string;
  computedValue: string|null;
  authoredValue: string|null;
  declarationId: number|null;
  hasSource: boolean;
  confidence: StackingConfidence;
}

export interface StackingContextFacts {
  createsStackingContext: boolean;
  reasons: StackingReason[];
}

const Z_INDEX_PARENT_DISPLAYS = new Set([
  'flex',
  'inline-flex',
  '-webkit-box',
  '-webkit-inline-box',
  'grid',
  'inline-grid',
  'grid-lanes',
  'inline-grid-lanes',
]);

/** LayoutBox objects. Table-internal and non-atomic inline boxes are not eligible. */
const CONTAINMENT_ELIGIBLE_DISPLAYS = new Set([
  'block',
  'flow-root',
  'inline-block',
  'list-item',
  'flow-root-list-item',
  'inline-list-item',
  'inline-flow-root-list-item',
  'flex',
  'inline-flex',
  'grid',
  'inline-grid',
  'grid-lanes',
  'inline-grid-lanes',
  'table',
  'inline-table',
  'table-cell',
  'table-caption',
  '-webkit-box',
  '-webkit-inline-box',
]);

const WILL_CHANGE_PROPERTIES = new Set([
  'opacity',
  'transform',
  'transform-style',
  'perspective',
  'translate',
  'rotate',
  'scale',
  'offset-path',
  'offset-position',
  'mask-image',
  '-webkit-mask-box-image-source',
  'clip-path',
  '-webkit-box-reflect',
  'filter',
  'backdrop-filter',
  'position',
  'mix-blend-mode',
  'isolation',
  'contain',
  'view-transition-name',
]);

export function explainStackingContext(input: StackingReasonInput): StackingContextFacts {
  if (!input.isDocumentElement && !generatesBox(input.display)) {
    return {createsStackingContext: false, reasons: []};
  }

  const reasons: StackingReason[] = [];
  if (input.isDocumentElement) {
    reasons.push(reason('root', null, input));
  }

  const position = input.position ?? 'static';
  if (position === 'fixed' || position === 'sticky') {
    reasons.push(reason('position', position, input));
  }

  if (zIndexCreatesContext(input)) {
    reasons.push(reason('z-index', input.zIndex, input));
  }

  if (isNonInitial(input.opacity, '1') && !opacityIsOne(input.opacity)) {
    reasons.push(reason('opacity', input.opacity, input));
  }
  pushIfSet(reasons, input, 'transform', input.transform, 'none');
  pushIfSet(reasons, input, 'translate', input.translate, 'none');
  pushIfSet(reasons, input, 'rotate', input.rotate, 'none');
  pushIfSet(reasons, input, 'scale', input.scale, 'none');
  pushIfSet(reasons, input, 'offset-path', input.offsetPath, 'none');
  if (offsetPositionCreatesContext(input.offsetPosition)) {
    reasons.push(reason('offset-position', input.offsetPosition, input));
  }
  if (input.transformStyle === 'preserve-3d') {
    reasons.push(reason('transform-style', input.transformStyle, input));
  }
  pushIfSet(reasons, input, 'perspective', input.perspective, 'none');
  pushIfSet(reasons, input, 'filter', input.filter, 'none');
  pushIfSet(reasons, input, 'backdrop-filter', input.backdropFilter, 'none');
  pushIfSet(reasons, input, 'clip-path', input.clipPath, 'none');
  if (isNonInitial(input.maskImage, 'none')) {
    reasons.push(reason('mask-image', input.maskImage, input));
  } else if (isNonInitial(input.webkitMaskImage, 'none')) {
    reasons.push(reason('-webkit-mask-image', input.webkitMaskImage, input));
  }
  if (isNonInitial(input.isolation, 'auto')) {
    reasons.push(reason('isolation', input.isolation, input));
  }
  if (isNonInitial(input.mixBlendMode, 'normal')) {
    reasons.push(reason('mix-blend-mode', input.mixBlendMode, input));
  }
  if (containCreatesContext(input.contain, input.display)) {
    reasons.push(reason('contain', input.contain, input));
  }
  // The root element’s automatic `view-transition-name: root` is a UA value.
  // The document-element reason already records that context. An author
  // declaration is still listed.
  if (!(input.isDocumentElement && input.viewTransitionName === 'root' && !input.declarations?.['view-transition-name'])) {
    pushIfSet(reasons, input, 'view-transition-name', input.viewTransitionName, 'none');
  }
  pushIfSet(reasons, input, '-webkit-box-reflect', input.webkitBoxReflect, 'none');

  for (const property of willChangeProperties(input.willChange)) {
    if (property === 'z-index' && !zIndexAllowed(input)) {
      continue;
    }
    if (WILL_CHANGE_PROPERTIES.has(property) || property === 'z-index') {
      reasons.push(willChangeReason(property, input));
    }
  }

  return {createsStackingContext: reasons.length > 0, reasons};
}

export function zIndexAllowed(input: Pick<StackingReasonInput, 'position'|'layoutParentDisplay'>): boolean {
  return (input.position ?? 'static') !== 'static' ||
      Z_INDEX_PARENT_DISPLAYS.has(input.layoutParentDisplay ?? '');
}

function zIndexCreatesContext(input: StackingReasonInput): boolean {
  return zIndexAllowed(input) && isNonInitial(input.zIndex, 'auto');
}

function generatesBox(display: string|null): boolean {
  return display !== 'none' && display !== 'contents';
}

function containCreatesContext(contain: string|null, display: string|null): boolean {
  if (!isNonInitial(contain, 'none') || !CONTAINMENT_ELIGIBLE_DISPLAYS.has(display ?? '')) {
    return false;
  }
  const keywords = new Set((contain ?? '').split(/\s+/));
  return keywords.has('strict') || keywords.has('content') || keywords.has('layout') || keywords.has('paint');
}

function offsetPositionCreatesContext(value: string|null): boolean {
  if (!value || value === 'auto' || value === 'none' || value === 'normal') {
    return false;
  }
  return true;
}

function willChangeProperties(value: string|null): string[] {
  if (!value || value === 'auto') {
    return [];
  }
  return value.split(',').map(part => part.trim().toLowerCase()).filter(part => part.length > 0);
}

function pushIfSet(
    reasons: StackingReason[], input: StackingReasonInput, property: string, value: string|null,
    initial: string): void {
  if (isNonInitial(value, initial)) {
    reasons.push(reason(property, value, input));
  }
}

function reason(property: string, computedValue: string|null, input: StackingReasonInput): StackingReason {
  const declaration = input.declarations?.[property] ?? null;
  return {
    property,
    computedValue,
    authoredValue: declaration?.value ?? null,
    declarationId: declaration?.id ?? null,
    hasSource: declaration?.hasSource ?? false,
    confidence: StackingConfidence.HIGH,
  };
}

function willChangeReason(property: string, input: StackingReasonInput): StackingReason {
  const declaration = input.declarations?.['will-change'] ?? null;
  return {
    property: 'will-change',
    computedValue: property,
    authoredValue: declaration?.value ?? null,
    declarationId: declaration?.id ?? null,
    hasSource: declaration?.hasSource ?? false,
    confidence: StackingConfidence.HIGH,
  };
}

function isNonInitial(value: string|null, initial: string): boolean {
  return value !== null && value !== '' && value !== initial;
}

function opacityIsOne(value: string|null): boolean {
  if (value === null) {
    return true;
  }
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed === 1;
}
