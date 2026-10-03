// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Contextual diagnostics. Categories describe behavior. They are not CSS
 * validity errors. Copy is derived at presentation time from these facts.
 */

import {ContributionRole, type DeclarationFact, type LayoutExplanation, Mechanism} from './LayoutExplanation.js';

export const DiagnosticCategory = {
  NO_VISIBLE_EFFECT: 'no-visible-effect',
  CONSTRAINED_BY: 'constrained-by',
  DOES_NOT_APPLY: 'does-not-apply',
  POTENTIAL_ISSUE: 'potential-issue',
} as const;

export type DiagnosticCategory = (typeof DiagnosticCategory)[keyof typeof DiagnosticCategory];

export const DiagnosticKind = {
  PERCENTAGE_HEIGHT: 'percentage-height',
  ZERO_FREE_SPACE: 'zero-free-space',
  VALIDATOR: 'validator',
  ANIMATION_OVERRIDE: 'animation-override',
  CASCADE_OVERRIDE: 'cascade-override',
} as const;

export type DiagnosticKind = (typeof DiagnosticKind)[keyof typeof DiagnosticKind];

export const DiagnosticPlacement = {
  WIDTH: 'width',
  HEIGHT: 'height',
  X: 'x',
  Y: 'y',
  LAYOUT: 'layout',
  SPACING: 'spacing',
  GENERAL: 'general',
} as const;

export type DiagnosticPlacement = (typeof DiagnosticPlacement)[keyof typeof DiagnosticPlacement];

export interface DiagnosticEvidence {
  measured: number|null;
  freeSpace: number|null;
  parentHeightAuto: boolean;
}

export interface Diagnostic {
  kind: DiagnosticKind;
  category: DiagnosticCategory;
  placement: DiagnosticPlacement;
  property: string|null;
  value: string|null;
  declarationId: number|null;
  hasSource: boolean;
  evidence: DiagnosticEvidence;
  /** Text produced by Chromium's CSSRuleValidator, when this diagnostic is one. */
  validatorMessage: string|null;
}

export interface DiagnosticCopy {
  category: string;
  headline: string;
  detail: string;
}

export function percentageHeightDiagnostic(explanation: LayoutExplanation): Diagnostic|null {
  const height = explanation.axes.find(axis => axis.axis === 'height');
  if (!height || height.mechanism !== Mechanism.CONTENT) {
    return null;
  }
  const ineffective = height.contributions.find(item => item.role === ContributionRole.INEFFECTIVE &&
                                                (item.property === 'height' || item.property === 'block-size'));
  if (!ineffective) {
    return null;
  }
  return {
    kind: DiagnosticKind.PERCENTAGE_HEIGHT,
    category: DiagnosticCategory.NO_VISIBLE_EFFECT,
    placement: DiagnosticPlacement.HEIGHT,
    property: ineffective.property,
    value: ineffective.value,
    declarationId: ineffective.declarationId,
    hasSource: ineffective.hasSource,
    evidence: {measured: height.usedValue, freeSpace: null, parentHeightAuto: true},
    validatorMessage: null,
  };
}

export interface FlexFreeSpaceItem {
  mainSize: number|null;
  marginStart: number|null;
  marginEnd: number|null;
  outOfFlow: boolean;
  transformed: boolean;
}

export interface FlexFreeSpaceInput {
  display: string|null;
  flexDirection: string|null;
  flexWrap: string|null;
  justifyContent: string|null;
  justifyContentDeclaration: DeclarationFact|null;
  contentMainSize: number|null;
  mainGap: number|null;
  items: FlexFreeSpaceItem[];
  itemsIncomplete: boolean;
}

/**
 * justify-content has nowhere to distribute when measured free space is 0.
 * Returns null when free space cannot be measured, including when free space
 * is actually available.
 */
export function flexFreeSpaceDiagnostic(input: FlexFreeSpaceInput): Diagnostic|null {
  if (input.display !== 'flex' && input.display !== 'inline-flex') {
    return null;
  }
  if (input.flexWrap !== 'nowrap') {
    return null;
  }
  if (!input.justifyContent || isDefaultJustify(input.justifyContent)) {
    return null;
  }
  if (mainAxis(input.flexDirection) === null) {
    return null;
  }
  if (input.itemsIncomplete || input.contentMainSize === null || input.mainGap === null) {
    return null;
  }
  const items = input.items.filter(item => !item.outOfFlow);
  if (items.length === 0) {
    return null;
  }
  if (items.some(item => item.transformed || item.mainSize === null || item.marginStart === null ||
                 item.marginEnd === null)) {
    return null;
  }
  const occupied = items.reduce(
      (sum, item) => sum + (item.mainSize ?? 0) + (item.marginStart ?? 0) + (item.marginEnd ?? 0), 0);
  const freeSpace = input.contentMainSize - occupied - input.mainGap * (items.length - 1);
  if (Math.abs(freeSpace) > 1) {
    return null;
  }
  const declaration = input.justifyContentDeclaration;
  return {
    kind: DiagnosticKind.ZERO_FREE_SPACE,
    category: DiagnosticCategory.NO_VISIBLE_EFFECT,
    placement: DiagnosticPlacement.LAYOUT,
    property: declaration?.property ?? 'justify-content',
    value: declaration?.value ?? input.justifyContent,
    declarationId: declaration?.id ?? null,
    hasSource: declaration?.hasSource ?? false,
    evidence: {measured: input.contentMainSize, freeSpace: 0, parentHeightAuto: false},
    validatorMessage: null,
  };
}

export function authorDiagnostic(
    kind: typeof DiagnosticKind.ANIMATION_OVERRIDE|typeof DiagnosticKind.CASCADE_OVERRIDE,
    fact: DeclarationFact): Diagnostic {
  return {
    kind,
    category: DiagnosticCategory.NO_VISIBLE_EFFECT,
    placement: placementFor(fact.property),
    property: fact.property,
    value: fact.value,
    declarationId: fact.id,
    hasSource: fact.hasSource,
    evidence: {measured: null, freeSpace: null, parentHeightAuto: false},
    validatorMessage: null,
  };
}

export function validatorDiagnostic(fact: DeclarationFact, message: string, potentialIssue: boolean): Diagnostic {
  return {
    kind: DiagnosticKind.VALIDATOR,
    category: potentialIssue ? DiagnosticCategory.POTENTIAL_ISSUE : DiagnosticCategory.DOES_NOT_APPLY,
    placement: placementFor(fact.property),
    property: fact.property,
    value: fact.value,
    declarationId: fact.id,
    hasSource: fact.hasSource,
    evidence: {measured: null, freeSpace: null, parentHeightAuto: false},
    validatorMessage: message,
  };
}

export function presentDiagnostic(diagnostic: Diagnostic): DiagnosticCopy {
  const category = categoryLabel(diagnostic.category);
  const declaration = diagnostic.property && diagnostic.value ? `${diagnostic.property}: ${diagnostic.value}` : '';
  switch (diagnostic.kind) {
    case DiagnosticKind.PERCENTAGE_HEIGHT:
      return {
        category,
        headline: `${declaration} does not determine this height.`,
        detail: 'The parent height is auto, so this percentage does not set the used height.',
      };
    case DiagnosticKind.ZERO_FREE_SPACE:
      return {
        category,
        headline: declaration,
        detail: 'The items fill the main axis, leaving 0px for justify-content to distribute.',
      };
    case DiagnosticKind.VALIDATOR:
      return {category, headline: declaration, detail: diagnostic.validatorMessage ?? ''};
    case DiagnosticKind.ANIMATION_OVERRIDE:
      return {
        category,
        headline: declaration,
        detail: 'An animation overrides this declaration, so it does not determine the used value.',
      };
    case DiagnosticKind.CASCADE_OVERRIDE:
      return {
        category,
        headline: declaration,
        detail: 'Another declaration overrides this one, so it does not determine the used value.',
      };
  }
}

export function categoryLabel(category: DiagnosticCategory): string {
  switch (category) {
    case DiagnosticCategory.NO_VISIBLE_EFFECT:
      return 'No visible effect';
    case DiagnosticCategory.CONSTRAINED_BY:
      return 'Constrained by';
    case DiagnosticCategory.DOES_NOT_APPLY:
      return 'Does not apply';
    case DiagnosticCategory.POTENTIAL_ISSUE:
      return 'Potential issue';
  }
}

export function placementFor(property: string): DiagnosticPlacement {
  switch (property) {
    case 'width':
    case 'min-width':
    case 'max-width':
    case 'inline-size':
    case 'min-inline-size':
    case 'max-inline-size':
      return DiagnosticPlacement.WIDTH;
    case 'height':
    case 'min-height':
    case 'max-height':
    case 'block-size':
    case 'min-block-size':
    case 'max-block-size':
      return DiagnosticPlacement.HEIGHT;
    case 'left':
    case 'right':
      return DiagnosticPlacement.X;
    case 'top':
    case 'bottom':
      return DiagnosticPlacement.Y;
    case 'justify-content':
    case 'justify-items':
    case 'justify-self':
    case 'align-items':
    case 'align-content':
    case 'align-self':
    case 'flex-direction':
    case 'flex-wrap':
    case 'flex':
    case 'flex-grow':
    case 'flex-shrink':
    case 'flex-basis':
      return DiagnosticPlacement.LAYOUT;
    case 'gap':
    case 'row-gap':
    case 'column-gap':
    case 'margin':
    case 'margin-top':
    case 'margin-right':
    case 'margin-bottom':
    case 'margin-left':
    case 'padding':
    case 'padding-top':
    case 'padding-right':
    case 'padding-bottom':
    case 'padding-left':
      return DiagnosticPlacement.SPACING;
    default:
      return DiagnosticPlacement.GENERAL;
  }
}

function mainAxis(flexDirection: string|null): 'horizontal'|'vertical'|null {
  if (flexDirection === 'row' || flexDirection === 'row-reverse') {
    return 'horizontal';
  }
  if (flexDirection === 'column' || flexDirection === 'column-reverse') {
    return 'vertical';
  }
  return null;
}

function isDefaultJustify(value: string): boolean {
  return value === 'normal' || value === 'flex-start' || value === 'start' || value === 'stretch';
}
