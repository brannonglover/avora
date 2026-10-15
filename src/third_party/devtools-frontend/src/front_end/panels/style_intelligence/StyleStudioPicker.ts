// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * How long after inspect mode ends a pick may still be reported. The protocol
 * reports the pick right after inspect mode is torn down, so ending is not by
 * itself a cancel.
 */
export const PICK_SETTLE_DELAY = 500;
/** How long a reported pick may take to become the Elements selection. */
export const PICK_SELECTION_TIMEOUT = 3000;

/**
 * Decides when a pick started from Studio has completed. It does not run an
 * inspect mode: the existing Elements picker does, and this only observes it.
 *
 * Completion is driven by the pick itself (`Overlay.inspectNodeRequested`), not
 * by a selection change, because picking the element that is already selected
 * changes nothing. A reported pick completes as soon as the Elements selection
 * is the picked node: immediately when it already was, otherwise when the
 * selection arrives.
 *
 * Only picks started from Studio are tracked, so a pick started from the
 * Elements toolbar never acts on Studio.
 */
export class StudioPickTracker<Node> {
  #armed = false;
  #received = false;
  #expected: Node|null = null;
  #timer = 0;
  /** Bumped on every reset, so a timer from an earlier pick can never touch a later one. */
  #generation = 0;
  readonly #onComplete: () => void;

  constructor(onComplete: () => void) {
    this.#onComplete = onComplete;
  }

  /** True from Studio starting the picker until its pick completes or is cancelled. */
  isArmed(): boolean {
    return this.#armed;
  }

  /** Studio started the picker. */
  arm(): void {
    this.#reset();
    this.#armed = true;
  }

  /** Forget any pick in progress, e.g. when a different control started the picker. */
  disarm(): void {
    this.#reset();
  }

  /**
   * Inspect mode ended. A pick reported shortly afterwards still counts; if none
   * is, the pick was cancelled and nothing changes.
   */
  inspectModeEnded(): void {
    if (!this.#armed || this.#received) {
      return;
    }
    this.#schedule(PICK_SETTLE_DELAY);
  }

  /** The page reported a pick. Its node is still being resolved. */
  pickReceived(): void {
    if (!this.#armed) {
      return;
    }
    this.#received = true;
    this.#schedule(PICK_SELECTION_TIMEOUT);
  }

  /**
   * The pick resolved to `expected`, the node the Elements panel will select,
   * while `selected` is the current selection. A repeated pick of the selected
   * element completes here.
   */
  pickResolved(expected: Node|null, selected: Node|null): void {
    if (!this.#armed || !this.#received) {
      return;
    }
    if (expected === null) {
      // The node could not be resolved, so the picker selects nothing.
      this.#reset();
      return;
    }
    if (expected === selected) {
      this.#complete();
      return;
    }
    this.#expected = expected;
  }

  /** The Elements selection changed. Completes a pick waiting for this node. */
  selectionChanged(selected: Node|null): void {
    if (!this.#armed || this.#expected === null || selected !== this.#expected) {
      return;
    }
    this.#complete();
  }

  dispose(): void {
    this.#reset();
  }

  #complete(): void {
    this.#reset();
    this.#onComplete();
  }

  #schedule(delay: number): void {
    window.clearTimeout(this.#timer);
    const generation = this.#generation;
    this.#timer = window.setTimeout(() => {
      if (generation === this.#generation) {
        this.#reset();
      }
    }, delay);
  }

  #reset(): void {
    window.clearTimeout(this.#timer);
    this.#generation++;
    this.#armed = false;
    this.#received = false;
    this.#expected = null;
  }
}

/** Receives picks made with the existing Elements picker, on any page target. */
export interface InspectPickObserver<Node> {
  /** The page reported a pick; its node is being resolved. */
  received(): void;
  /** The node the Elements panel selects for that pick, or null if it could not be resolved. */
  resolved(node: Node|null): void;
  /** A page target that can be picked from was added or removed. */
  availabilityChanged(): void;
}

/** How the hub reaches DevTools: target discovery and the per-target protocol listener. */
export interface InspectPickPorts<Target, Node> {
  observeTargets(added: (target: Target) => void, removed: (target: Target) => void): void;
  /** Adds the pick listener to `target`. The hub calls this at most once per target. */
  listen(target: Target, onPick: (resolve: () => Promise<Node|null>) => void): void;
}

/**
 * Fans the existing picker's pick event out to subscribers. The protocol client
 * offers no way to remove an Overlay dispatcher, so the hub adds exactly one per
 * target, for that target's lifetime, and subscribers come and go above it.
 * Subscribing again, unsubscribing, or a target being reported twice never adds
 * another listener, and an unsubscribed observer is never called again.
 */
export class InspectPickHub<Target extends object, Node> {
  readonly #ports: InspectPickPorts<Target, Node>;
  readonly #subscribers = new Set<InspectPickObserver<Node>>();
  readonly #listening = new WeakSet<Target>();
  #started = false;

  constructor(ports: InspectPickPorts<Target, Node>) {
    this.#ports = ports;
  }

  subscribe(observer: InspectPickObserver<Node>): () => void {
    this.#subscribers.add(observer);
    if (!this.#started) {
      this.#started = true;
      this.#ports.observeTargets(target => this.#targetAdded(target), () => this.#notifyAvailability());
    }
    return () => {
      this.#subscribers.delete(observer);
    };
  }

  subscriberCount(): number {
    return this.#subscribers.size;
  }

  #targetAdded(target: Target): void {
    if (!this.#listening.has(target)) {
      this.#listening.add(target);
      this.#ports.listen(target, resolve => this.#pick(resolve));
    }
    this.#notifyAvailability();
  }

  #notifyAvailability(): void {
    for (const observer of [...this.#subscribers]) {
      observer.availabilityChanged();
    }
  }

  /** One pick event, resolved once, delivered once to each subscriber still subscribed. */
  #pick(resolve: () => Promise<Node|null>): void {
    const observers = [...this.#subscribers];
    if (!observers.length) {
      return;
    }
    for (const observer of observers) {
      observer.received();
    }
    const deliver = (node: Node|null): void => {
      for (const observer of observers) {
        if (this.#subscribers.has(observer)) {
          observer.resolved(node);
        }
      }
    };
    resolve().then(deliver, () => deliver(null));
  }
}
