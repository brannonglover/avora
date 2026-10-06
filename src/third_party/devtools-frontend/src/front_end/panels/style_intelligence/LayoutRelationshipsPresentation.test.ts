// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {type LayoutRelationships} from './LayoutRelationships.js';
import {mergePositionRelationships, presentLayoutRelationships, type StickyInsets} from './LayoutRelationshipsPresentation.js';

const noInsets: StickyInsets = {top: null, right: null, bottom: null, left: null};

function elementRelationships(overrides: Partial<LayoutRelationships> = {}): LayoutRelationships {
  return {
    containingBlock: null,
    scrollContainer: null,
    stickyScrollContainers: null,
    ...overrides,
  };
}

describe('LayoutRelationshipsPresentation', () => {
  it('names an element containing block once and leaves source off that element', () => {
    const rows = presentLayoutRelationships(
        elementRelationships({containingBlock: {kind: 'element', nodeId: 7, fixedToViewport: false}}),
        new Map([[7, 'div.card']]), noInsets, 3);
    assert.lengthOf(rows, 1);
    assert.strictEqual(rows[0].primary, 'Positioned relative to div.card');
    assert.strictEqual(rows[0].controls[0].actions.nodeId, 7);
    assert.isNull(rows[0].controls[0].actions.declarationId);
  });

  it('uses viewport and anonymous wording without an element', () => {
    const viewport = presentLayoutRelationships(
        elementRelationships({containingBlock: {kind: 'initial-containing-block', nodeId: null, fixedToViewport: false}}),
        new Map(), noInsets, 1);
    assert.strictEqual(viewport[0].primary, 'Positioned relative to the viewport.');
    assert.lengthOf(viewport[0].controls, 0);

    const anonymous = presentLayoutRelationships(
        elementRelationships({containingBlock: {kind: 'anonymous', nodeId: null, fixedToViewport: false}}),
        new Map(), noInsets, 1);
    assert.strictEqual(anonymous[0].primary, 'Positioned relative to an anonymous layout container.');
    assert.lengthOf(anonymous[0].controls, 0);
  });

  it('collapses sticky axes that resolve to the same element', () => {
    const rows = presentLayoutRelationships(elementRelationships({
      scrollContainer: {kind: 'element', nodeId: 4, fixedToViewport: false},
      stickyScrollContainers: {
        horizontal: {kind: 'element', nodeId: 4, fixedToViewport: false},
        vertical: {kind: 'element', nodeId: 4, fixedToViewport: false},
      },
    }), new Map([[4, 'div.scroller']]), {top: '8px', right: null, bottom: null, left: '12px'}, 1);
    const sticky = rows.filter(row => row.label === 'Sticky');
    assert.lengthOf(sticky, 1);
    assert.strictEqual(sticky[0].primary, 'top: 8px');
    assert.strictEqual(sticky[0].controls[0].text, 'left: 12px');
    const actions = sticky[0].controls.filter(control => control.actions.nodeId !== null);
    assert.lengthOf(actions, 1);
    assert.strictEqual(actions[0].text, 'Constrained by div.scroller');
    assert.strictEqual(actions[0].actions.nodeId, 4);
    assert.isFalse(rows.some(row => row.label === 'Scroll container'));
  });

  it('keeps separate rows when sticky axes differ and omits an empty axis', () => {
    const split = presentLayoutRelationships(elementRelationships({
      stickyScrollContainers: {
        horizontal: {kind: 'element', nodeId: 2, fixedToViewport: false},
        vertical: {kind: 'element', nodeId: 3, fixedToViewport: false},
      },
    }), new Map([[2, 'div.horizontal-scroller'], [3, 'div.vertical-scroller']]), {
      top: '8px',
      right: null,
      bottom: null,
      left: '12px',
    }, 1);
    assert.strictEqual(split[0].label, 'Vertical');
    assert.strictEqual(split[0].primary, 'top: 8px');
    assert.strictEqual(split[0].controls[0].actions.nodeId, 3);
    assert.strictEqual(split[1].label, 'Horizontal');
    assert.strictEqual(split[1].primary, 'left: 12px');
    assert.strictEqual(split[1].controls[0].actions.nodeId, 2);

    const topOnly = presentLayoutRelationships(elementRelationships({
      stickyScrollContainers: {
        horizontal: null,
        vertical: {kind: 'element', nodeId: 8, fixedToViewport: false},
      },
    }), new Map([[8, 'div.y']]), {top: '8px', right: null, bottom: null, left: null}, 1);
    assert.lengthOf(topOnly, 1);
    assert.strictEqual(topOnly[0].label, 'Vertical');
    assert.isFalse(topOnly.some(row => row.label === 'Horizontal'));
  });

  it('shows bottom and right on one element and document wording without a node', () => {
    const rows = presentLayoutRelationships(elementRelationships({
      stickyScrollContainers: {
        horizontal: {kind: 'element', nodeId: 5, fixedToViewport: false},
        vertical: {kind: 'element', nodeId: 5, fixedToViewport: false},
      },
    }), new Map([[5, 'div.scroller']]), {top: null, right: '6px', bottom: '4px', left: null}, 1);
    assert.strictEqual(rows[0].primary, 'bottom: 4px');
    assert.strictEqual(rows[0].controls[0].text, 'right: 6px');

    const documentRows = presentLayoutRelationships(elementRelationships({
      stickyScrollContainers: {
        horizontal: null,
        vertical: {kind: 'document', nodeId: null, fixedToViewport: false},
      },
    }), new Map(), {top: '0px', right: null, bottom: null, left: null}, 1);
    assert.strictEqual(documentRows[0].controls[0].text, 'Constrained by the document.');
    assert.isNull(documentRows[0].controls[0].actions.nodeId);
  });

  it('drops the inferred scrollport once Blink names the sticky containers', () => {
    const sections = [{
      title: 'Position',
      lines: [],
      tokens: [],
      diagnostics: [],
      rows: [{
        label: 'Y',
        primary: 'Appears to be constrained by a scrollport',
        note: null,
        controls: [
          {text: 'div#axis-inner-x', title: 'div#axis-inner-x', actions: {nodeId: null, declarationId: null}},
          {text: 'top: 8px contributes', title: 'top: 8px contributes', actions: {nodeId: null, declarationId: 3}},
        ],
        details: ['The scrollport could not be identified confidently.'],
        diagnostics: [],
      }],
    }];
    const merged = mergePositionRelationships(
        sections,
        elementRelationships({
          stickyScrollContainers: {
            horizontal: null,
            vertical: {kind: 'element', nodeId: 8, fixedToViewport: false},
          },
        }),
        new Map([[8, 'div#axis-outer-y']]), {top: '8px', right: null, bottom: null, left: null}, 1);
    const position = merged.find(section => section.title === 'Position');
    assert.exists(position);
    assert.strictEqual(position?.rows[0].primary, 'Sticky');
    assert.deepEqual(position?.rows[0].controls.map(control => control.text), ['top: 8px contributes']);
    assert.deepEqual(position?.rows[0].details, []);
    assert.strictEqual(position?.rows[1].label, 'Vertical');
    assert.strictEqual(position?.rows[1].controls[0].text, 'Constrained by div#axis-outer-y');
  });

  it('does not highlight the selected element', () => {
    const rows = presentLayoutRelationships(
        elementRelationships({containingBlock: {kind: 'element', nodeId: 9, fixedToViewport: false}}),
        new Map([[9, 'div.card']]), noInsets, 9);
    assert.isNull(rows[0].controls[0].actions.nodeId);
  });
});
