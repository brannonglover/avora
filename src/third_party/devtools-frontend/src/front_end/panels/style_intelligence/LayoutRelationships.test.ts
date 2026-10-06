// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {positionNeedsLayoutRelationships, readLayoutRelationships} from './LayoutRelationships.js';

describe('LayoutRelationships', () => {
  it('keeps scroll and sticky containers when they name the same element', () => {
    const relationships = readLayoutRelationships({
      scrollContainer: {kind: 'element', nodeId: 4, fixedToViewport: false},
      stickyScrollContainers: {
        horizontal: {kind: 'element', nodeId: 4, fixedToViewport: false},
        vertical: {kind: 'element', nodeId: 4, fixedToViewport: false},
      },
    });
    assert.strictEqual(relationships?.scrollContainer?.nodeId, 4);
    assert.strictEqual(relationships?.stickyScrollContainers?.horizontal?.nodeId, 4);
    assert.strictEqual(relationships?.stickyScrollContainers?.vertical?.nodeId, 4);
    assert.notStrictEqual(relationships?.scrollContainer, relationships?.stickyScrollContainers?.horizontal);
  });

  it('preserves different physical sticky axes and drops an unconstrained axis', () => {
    const relationships = readLayoutRelationships({
      stickyScrollContainers: {
        horizontal: {kind: 'element', nodeId: 2, fixedToViewport: false},
        vertical: {kind: 'element', nodeId: 3, fixedToViewport: false},
      },
    });
    assert.strictEqual(relationships?.stickyScrollContainers?.horizontal?.nodeId, 2);
    assert.strictEqual(relationships?.stickyScrollContainers?.vertical?.nodeId, 3);

    const topOnly = readLayoutRelationships({
      stickyScrollContainers: {
        vertical: {kind: 'element', nodeId: 8, fixedToViewport: false},
      },
    });
    assert.isNull(topOnly?.stickyScrollContainers?.horizontal ?? null);
    assert.strictEqual(topOnly?.stickyScrollContainers?.vertical?.nodeId, 8);
  });

  it('does not invent a node for the initial containing block, an anonymous box, or the document', () => {
    const relationships = readLayoutRelationships({
      containingBlock: {kind: 'initial-containing-block'},
      scrollContainer: {kind: 'document', fixedToViewport: true},
    });
    assert.strictEqual(relationships?.containingBlock?.kind, 'initial-containing-block');
    assert.isNull(relationships?.containingBlock?.nodeId ?? null);
    assert.strictEqual(relationships?.scrollContainer?.kind, 'document');
    assert.isNull(relationships?.scrollContainer?.nodeId ?? null);
    assert.isTrue(relationships?.scrollContainer?.fixedToViewport);

    const anonymous = readLayoutRelationships({containingBlock: {kind: 'anonymous'}});
    assert.strictEqual(anonymous?.containingBlock?.kind, 'anonymous');
    assert.isNull(anonymous?.containingBlock?.nodeId ?? null);
  });

  it('drops an element relationship that has no node id', () => {
    assert.isNull(readLayoutRelationships({containingBlock: {kind: 'element'}}));
  });

  it('asks for relationships only for absolute, fixed, and sticky boxes', () => {
    assert.isTrue(positionNeedsLayoutRelationships('absolute'));
    assert.isTrue(positionNeedsLayoutRelationships('fixed'));
    assert.isTrue(positionNeedsLayoutRelationships('sticky'));
    assert.isFalse(positionNeedsLayoutRelationships('relative'));
    assert.isFalse(positionNeedsLayoutRelationships('static'));
    assert.isFalse(positionNeedsLayoutRelationships(null));
  });
});
