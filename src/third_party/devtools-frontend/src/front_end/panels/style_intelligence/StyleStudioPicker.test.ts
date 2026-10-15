// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {PICK_SETTLE_DELAY, StudioPickTracker} from './StyleStudioPicker.js';

/** Stands in for a DOM node; identity is all the tracker compares. */
interface FakeNode {
  name: string;
}

function wait(ms: number): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

describe('StudioPickTracker', () => {
  const a: FakeNode = {name: 'a'};
  const b: FakeNode = {name: 'b'};
  let completions: number;
  let tracker: StudioPickTracker<FakeNode>;

  beforeEach(() => {
    completions = 0;
    tracker = new StudioPickTracker<FakeNode>(() => {
      completions++;
    });
  });

  afterEach(() => {
    tracker.dispose();
  });

  /** The order the protocol and DevTools produce for a click on the page. */
  function pickOnPage(picked: FakeNode, selected: FakeNode|null): void {
    tracker.inspectModeEnded();
    tracker.pickReceived();
    tracker.pickResolved(picked, selected);
  }

  it('completes a pick of the element that is already selected', () => {
    tracker.arm();
    pickOnPage(a, a);
    assert.strictEqual(completions, 1);
    assert.isFalse(tracker.isArmed());
  });

  it('completes repeated picks of the same selected element', () => {
    for (let i = 0; i < 3; i++) {
      tracker.arm();
      pickOnPage(a, a);
    }
    assert.strictEqual(completions, 3);
  });

  it('completes a first pick once the selection arrives', () => {
    tracker.arm();
    pickOnPage(a, null);
    assert.strictEqual(completions, 0);
    tracker.selectionChanged(a);
    assert.strictEqual(completions, 1);
  });

  it('completes when the selection arrives before the pick is resolved', () => {
    tracker.arm();
    tracker.inspectModeEnded();
    tracker.pickReceived();
    tracker.selectionChanged(a);
    assert.strictEqual(completions, 0);
    tracker.pickResolved(a, a);
    assert.strictEqual(completions, 1);
  });

  it('waits for the picked node, not any selection change', () => {
    tracker.arm();
    pickOnPage(a, b);
    tracker.selectionChanged(b);
    assert.strictEqual(completions, 0);
    tracker.selectionChanged(a);
    assert.strictEqual(completions, 1);
    tracker.selectionChanged(a);
    assert.strictEqual(completions, 1);
  });

  it('treats inspect mode ending without a pick as a cancel', async () => {
    tracker.arm();
    tracker.inspectModeEnded();
    await wait(PICK_SETTLE_DELAY + 50);
    assert.isFalse(tracker.isArmed());
    // A later pick, e.g. from the toolbar, is not Studio's.
    tracker.pickReceived();
    tracker.pickResolved(a, a);
    tracker.selectionChanged(a);
    assert.strictEqual(completions, 0);
  });

  it('keeps a pick reported right after inspect mode ends', async () => {
    tracker.arm();
    tracker.inspectModeEnded();
    await wait(PICK_SETTLE_DELAY / 2);
    tracker.pickReceived();
    await wait(PICK_SETTLE_DELAY);
    tracker.pickResolved(a, a);
    assert.strictEqual(completions, 1);
  });

  it('ignores picks it did not start', () => {
    pickOnPage(a, a);
    tracker.selectionChanged(a);
    assert.strictEqual(completions, 0);
  });

  it('stops tracking when disarmed, e.g. when the toolbar starts the picker', () => {
    tracker.arm();
    tracker.disarm();
    pickOnPage(a, a);
    assert.strictEqual(completions, 0);
  });

  it('does not complete a pick that could not be resolved', () => {
    tracker.arm();
    tracker.inspectModeEnded();
    tracker.pickReceived();
    tracker.pickResolved(null, a);
    tracker.selectionChanged(a);
    assert.strictEqual(completions, 0);
    assert.isFalse(tracker.isArmed());
  });

  it('starts over when armed again', () => {
    tracker.arm();
    pickOnPage(a, b);
    tracker.arm();
    tracker.selectionChanged(a);
    assert.strictEqual(completions, 0);
    pickOnPage(b, b);
    assert.strictEqual(completions, 1);
  });
});
