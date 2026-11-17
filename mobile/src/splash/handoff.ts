import { useSyncExternalStore } from 'react';

// The splash logo lands on the start page's logo. The start page reports where
// that logo sits on screen and keeps it hidden until the splash is done, so
// the hand-off is a single logo moving rather than two crossfading.

export type Rect = { x: number; y: number; width: number; height: number };

let target: Rect | null = null;
let done = false;
const listeners = new Set<() => void>();

export function setLogoTarget(rect: Rect | null) {
  target = rect;
}

export function logoTarget() {
  return target;
}

export function markSplashDone() {
  if (done) return;
  done = true;
  listeners.forEach((l) => l());
}

function subscribe(listener: () => void) {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

export function useSplashDone() {
  return useSyncExternalStore(subscribe, () => done);
}
