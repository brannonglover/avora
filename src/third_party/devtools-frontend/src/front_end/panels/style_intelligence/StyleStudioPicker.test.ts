// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {
  InspectPickHub,
  type InspectPickObserver,
  type InspectPickPorts,
  PICK_SETTLE_DELAY,
  StudioPickTracker,
} from './StyleStudioPicker.js';

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

  it('cannot be cancelled by a timer left over from an earlier pick', async () => {
    tracker.arm();
    tracker.inspectModeEnded();
    // A new pick starts before the first one's cancel timer fires.
    await wait(PICK_SETTLE_DELAY / 2);
    tracker.arm();
    await wait(PICK_SETTLE_DELAY / 2 + 100);
    assert.isTrue(tracker.isArmed());
    tracker.pickReceived();
    tracker.pickResolved(a, a);
    assert.strictEqual(completions, 1);
  });
});

describe('InspectPickHub', () => {
  interface FakeTarget {
    name: string;
  }

  /** Records protocol listeners per target and lets the test fire picks on them. */
  class FakePorts implements InspectPickPorts<FakeTarget, string> {
    added: ((target: FakeTarget) => void)|null = null;
    removed: ((target: FakeTarget) => void)|null = null;
    observeCalls = 0;
    readonly listeners = new Map<FakeTarget, Array<(resolve: () => Promise<string|null>) => void>>();

    observeTargets(added: (target: FakeTarget) => void, removed: (target: FakeTarget) => void): void {
      this.observeCalls++;
      this.added = added;
      this.removed = removed;
    }

    listen(target: FakeTarget, onPick: (resolve: () => Promise<string|null>) => void): void {
      const list = this.listeners.get(target) ?? [];
      list.push(onPick);
      this.listeners.set(target, list);
    }

    /** The protocol delivers one inspectNodeRequested to every listener on the target. */
    pick(target: FakeTarget, node: string|null): number {
      let resolutions = 0;
      for (const listener of this.listeners.get(target) ?? []) {
        listener(() => {
          resolutions++;
          return Promise.resolve(node);
        });
      }
      return resolutions;
    }
  }

  function recorder(): InspectPickObserver<string>&{events: string[]} {
    const events: string[] = [];
    return {
      events,
      received: () => events.push('received'),
      resolved: node => events.push(`resolved:${node}`),
      availabilityChanged: () => events.push('availability'),
    };
  }

  const page: FakeTarget = {name: 'page'};
  const frame: FakeTarget = {name: 'frame'};

  it('adds exactly one protocol listener per target, however often it is subscribed to', () => {
    const ports = new FakePorts();
    const hub = new InspectPickHub<FakeTarget, string>(ports);
    const unsubscribers: Array<() => void> = [];
    for (let i = 0; i < 5; i++) {
      unsubscribers.push(hub.subscribe(recorder()));
    }
    ports.added?.(page);
    ports.added?.(page);  // A target reported again, e.g. after a scope change.
    ports.added?.(frame);
    for (const unsubscribe of unsubscribers) {
      unsubscribe();
    }
    hub.subscribe(recorder());
    ports.added?.(page);
    assert.strictEqual(ports.observeCalls, 1);
    assert.strictEqual(ports.listeners.get(page)?.length, 1);
    assert.strictEqual(ports.listeners.get(frame)?.length, 1);
  });

  it('delivers one pick once to each subscriber and resolves it once', async () => {
    const ports = new FakePorts();
    const hub = new InspectPickHub<FakeTarget, string>(ports);
    const first = recorder();
    const second = recorder();
    hub.subscribe(first);
    hub.subscribe(second);
    ports.added?.(page);
    first.events.length = 0;
    second.events.length = 0;

    const resolutions = ports.pick(page, 'div#a');
    await wait(0);
    assert.strictEqual(resolutions, 1);
    assert.deepEqual(first.events, ['received', 'resolved:div#a']);
    assert.deepEqual(second.events, ['received', 'resolved:div#a']);
  });

  it('does not accumulate callbacks across repeated picks and resubscriptions', async () => {
    const ports = new FakePorts();
    const hub = new InspectPickHub<FakeTarget, string>(ports);
    ports.added?.(page);
    let current = recorder();
    let unsubscribe = hub.subscribe(current);
    ports.added?.(page);
    for (let i = 0; i < 4; i++) {
      unsubscribe();
      current = recorder();
      unsubscribe = hub.subscribe(current);
      current.events.length = 0;
      ports.pick(page, `n${i}`);
      await wait(0);
      assert.deepEqual(current.events, ['received', `resolved:n${i}`]);
    }
    assert.strictEqual(hub.subscriberCount(), 1);
  });

  it('never calls an observer after it unsubscribes, even mid-pick', async () => {
    const ports = new FakePorts();
    const hub = new InspectPickHub<FakeTarget, string>(ports);
    const observer = recorder();
    const unsubscribe = hub.subscribe(observer);
    ports.added?.(page);
    observer.events.length = 0;
    ports.pick(page, 'div#a');
    unsubscribe();
    await wait(0);
    ports.pick(page, 'div#b');
    await wait(0);
    assert.deepEqual(observer.events, ['received']);
  });

  it('does not resolve picks nobody is listening for', () => {
    const ports = new FakePorts();
    const hub = new InspectPickHub<FakeTarget, string>(ports);
    const unsubscribe = hub.subscribe(recorder());
    ports.added?.(page);
    unsubscribe();
    assert.strictEqual(ports.pick(page, 'div#a'), 0);
  });

  it('reports targets coming and going as availability changes', () => {
    const ports = new FakePorts();
    const hub = new InspectPickHub<FakeTarget, string>(ports);
    const observer = recorder();
    hub.subscribe(observer);
    ports.added?.(page);
    ports.removed?.(page);
    assert.deepEqual(observer.events, ['availability', 'availability']);
  });
});
