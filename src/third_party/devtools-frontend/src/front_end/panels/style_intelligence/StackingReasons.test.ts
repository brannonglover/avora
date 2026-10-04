// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {explainStackingContext, type StackingReasonInput} from './StackingReasons.js';

function input(overrides: Partial<StackingReasonInput> = {}): StackingReasonInput {
  return {
    display: 'block',
    position: 'static',
    zIndex: 'auto',
    opacity: '1',
    transform: 'none',
    translate: 'none',
    rotate: 'none',
    scale: 'none',
    offsetPath: 'none',
    offsetPosition: 'auto',
    transformStyle: 'flat',
    perspective: 'none',
    filter: 'none',
    backdropFilter: 'none',
    clipPath: 'none',
    maskImage: 'none',
    webkitMaskImage: 'none',
    isolation: 'auto',
    mixBlendMode: 'normal',
    contain: 'none',
    willChange: 'auto',
    viewTransitionName: 'none',
    webkitBoxReflect: 'none',
    layoutParentDisplay: 'block',
    isDocumentElement: false,
    ...overrides,
  };
}

function properties(reasons: ReturnType<typeof explainStackingContext>['reasons']): string[] {
  return reasons.map(reason => reason.property);
}

describe('StackingReasons', () => {
  it('does not create a context for an ordinary block', () => {
    const facts = explainStackingContext(input());
    assert.strictEqual(facts.createsStackingContext, false);
    assert.deepEqual(facts.reasons, []);
  });

  it('creates a context for a positioned element with a non-auto z-index', () => {
    const facts = explainStackingContext(input({
      position: 'relative',
      zIndex: '10',
      declarations: {'z-index': {id: 4, value: '10'}},
    }));
    assert.strictEqual(facts.createsStackingContext, true);
    assert.deepEqual(properties(facts.reasons), ['z-index']);
    assert.strictEqual(facts.reasons[0].computedValue, '10');
    assert.strictEqual(facts.reasons[0].authoredValue, '10');
    assert.strictEqual(facts.reasons[0].declarationId, 4);
    assert.strictEqual(facts.reasons[0].confidence, 'high');
  });

  it('does not create a context for position:relative with z-index:auto', () => {
    const facts = explainStackingContext(input({position: 'relative', zIndex: 'auto'}));
    assert.strictEqual(facts.createsStackingContext, false);
  });

  it('treats z-index:0 as creating a context on a positioned box', () => {
    const facts = explainStackingContext(input({position: 'absolute', zIndex: '0'}));
    assert.deepEqual(properties(facts.reasons), ['z-index']);
  });

  it('ignores z-index on a static block', () => {
    const facts = explainStackingContext(input({position: 'static', zIndex: '5', layoutParentDisplay: 'block'}));
    assert.strictEqual(facts.createsStackingContext, false);
  });

  it('creates a context for a static flex item with z-index', () => {
    const facts = explainStackingContext(input({position: 'static', zIndex: '3', layoutParentDisplay: 'flex'}));
    assert.deepEqual(properties(facts.reasons), ['z-index']);
  });

  it('creates a context for a static grid item with z-index', () => {
    const facts = explainStackingContext(input({
      position: 'static',
      zIndex: '4',
      layoutParentDisplay: 'inline-grid',
    }));
    assert.deepEqual(properties(facts.reasons), ['z-index']);
  });

  it('creates a context for fixed and sticky positioning without a z-index', () => {
    assert.deepEqual(properties(explainStackingContext(input({position: 'fixed'})).reasons), ['position']);
    assert.deepEqual(properties(explainStackingContext(input({position: 'sticky'})).reasons), ['position']);
  });

  it('keeps both fixed positioning and z-index when both apply', () => {
    const facts = explainStackingContext(input({position: 'fixed', zIndex: '2'}));
    assert.deepEqual(properties(facts.reasons), ['position', 'z-index']);
  });

  it('creates a context for opacity below 1 and prefers the authored value', () => {
    const facts = explainStackingContext(input({
      opacity: '0.99',
      declarations: {opacity: {id: 8, value: '0.99'}},
    }));
    assert.strictEqual(facts.reasons[0].property, 'opacity');
    assert.strictEqual(facts.reasons[0].authoredValue, '0.99');
    assert.strictEqual(facts.reasons[0].declarationId, 8);
  });

  it('treats an identity transform matrix as a transform context and keeps the authored function', () => {
    const facts = explainStackingContext(input({
      transform: 'matrix(1, 0, 0, 1, 0, 0)',
      declarations: {transform: {id: 2, value: 'translateZ(0)'}},
    }));
    assert.strictEqual(facts.createsStackingContext, true);
    assert.strictEqual(facts.reasons[0].property, 'transform');
    assert.strictEqual(facts.reasons[0].computedValue, 'matrix(1, 0, 0, 1, 0, 0)');
    assert.strictEqual(facts.reasons[0].authoredValue, 'translateZ(0)');
  });

  it('creates a context for isolation, filter, blend mode, clip-path, mask, and perspective', () => {
    assert.deepEqual(properties(explainStackingContext(input({isolation: 'isolate'})).reasons), ['isolation']);
    assert.deepEqual(properties(explainStackingContext(input({filter: 'blur(0px)'})).reasons), ['filter']);
    assert.deepEqual(
        properties(explainStackingContext(input({backdropFilter: 'blur(2px)'})).reasons), ['backdrop-filter']);
    assert.deepEqual(
        properties(explainStackingContext(input({mixBlendMode: 'multiply'})).reasons), ['mix-blend-mode']);
    assert.deepEqual(properties(explainStackingContext(input({clipPath: 'inset(0px)'})).reasons), ['clip-path']);
    assert.deepEqual(
        properties(explainStackingContext(input({webkitMaskImage: 'linear-gradient(black, black)'})).reasons),
        ['-webkit-mask-image']);
    assert.deepEqual(properties(explainStackingContext(input({perspective: '100px'})).reasons), ['perspective']);
  });

  it('creates a context for preserve-3d, individual transforms, and offset-path', () => {
    assert.deepEqual(
        properties(explainStackingContext(input({transformStyle: 'preserve-3d'})).reasons), ['transform-style']);
    assert.deepEqual(properties(explainStackingContext(input({translate: '10px'})).reasons), ['translate']);
    assert.deepEqual(properties(explainStackingContext(input({rotate: '45deg'})).reasons), ['rotate']);
    assert.deepEqual(properties(explainStackingContext(input({scale: '1.1'})).reasons), ['scale']);
    assert.deepEqual(properties(explainStackingContext(input({offsetPath: 'path("M0,0")'})).reasons), ['offset-path']);
  });

  it('creates a context for contain:paint and contain:layout, including strict and content', () => {
    assert.deepEqual(properties(explainStackingContext(input({contain: 'paint'})).reasons), ['contain']);
    assert.deepEqual(properties(explainStackingContext(input({contain: 'layout'})).reasons), ['contain']);
    assert.deepEqual(properties(explainStackingContext(input({contain: 'strict'})).reasons), ['contain']);
    assert.deepEqual(properties(explainStackingContext(input({contain: 'content'})).reasons), ['contain']);
  });

  it('does not treat contain:style or contain:size as a stacking context', () => {
    assert.strictEqual(explainStackingContext(input({contain: 'style'})).createsStackingContext, false);
    assert.strictEqual(explainStackingContext(input({contain: 'size'})).createsStackingContext, false);
  });

  it('does not treat paint containment on a table row as a stacking context', () => {
    const facts = explainStackingContext(input({display: 'table-row', contain: 'paint'}));
    assert.strictEqual(facts.createsStackingContext, false);
  });

  it('still treats opacity on a table row as a stacking context', () => {
    const facts = explainStackingContext(input({display: 'table-row', opacity: '0.5'}));
    assert.deepEqual(properties(facts.reasons), ['opacity']);
  });

  it('creates a context for a matching will-change, including z-index only when z-index applies', () => {
    const opacity = explainStackingContext(input({willChange: 'opacity'}));
    assert.strictEqual(opacity.reasons[0].property, 'will-change');
    assert.strictEqual(opacity.reasons[0].computedValue, 'opacity');

    const blocked = explainStackingContext(input({willChange: 'z-index', position: 'static'}));
    assert.strictEqual(blocked.createsStackingContext, false);

    const allowed = explainStackingContext(input({willChange: 'z-index', position: 'relative'}));
    assert.strictEqual(allowed.reasons[0].computedValue, 'z-index');

    const ignored = explainStackingContext(input({willChange: 'left'}));
    assert.strictEqual(ignored.createsStackingContext, false);
  });

  it('keeps every simultaneous reason and does not pick a primary one', () => {
    const facts = explainStackingContext(input({
      opacity: '0.9',
      transform: 'matrix(1, 0, 0, 1, 0, 0)',
      declarations: {
        opacity: {id: 1, value: '0.9'},
        transform: {id: 2, value: 'translateZ(0)'},
      },
    }));
    assert.deepEqual(properties(facts.reasons), ['opacity', 'transform']);
    assert.strictEqual(facts.reasons[0].authoredValue, '0.9');
    assert.strictEqual(facts.reasons[1].authoredValue, 'translateZ(0)');
  });

  it('creates a context for a non-none view-transition-name', () => {
    const facts = explainStackingContext(input({viewTransitionName: 'hero'}));
    assert.deepEqual(properties(facts.reasons), ['view-transition-name']);
  });

  it('always creates a context for the document element', () => {
    const facts = explainStackingContext(input({isDocumentElement: true, display: 'block'}));
    assert.strictEqual(facts.createsStackingContext, true);
    assert.deepEqual(properties(facts.reasons), ['root']);
  });

  it('does not list the document element’s automatic view-transition name unless the author set it', () => {
    const automatic = explainStackingContext(input({isDocumentElement: true, viewTransitionName: 'root'}));
    assert.deepEqual(properties(automatic.reasons), ['root']);
    const authored = explainStackingContext(input({
      isDocumentElement: true,
      viewTransitionName: 'hero',
      declarations: {'view-transition-name': {id: 3, value: 'hero', hasSource: true}},
    }));
    assert.deepEqual(properties(authored.reasons), ['root', 'view-transition-name']);
  });

  it('does not create a context for display:contents or display:none', () => {
    assert.strictEqual(
        explainStackingContext(input({display: 'contents', opacity: '0.2', transform: 'matrix(1, 0, 0, 1, 0, 0)'}))
            .createsStackingContext,
        false);
    assert.strictEqual(explainStackingContext(input({display: 'none', position: 'fixed'})).createsStackingContext, false);
  });
});
