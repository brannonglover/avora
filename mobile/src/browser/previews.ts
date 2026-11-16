import AsyncStorage from '@react-native-async-storage/async-storage';
import { useSyncExternalStore } from 'react';
import { findNodeHandle, PixelRatio, type ViewInstance } from 'react-native';
import { captureRef } from 'react-native-view-shot';

// Tab previews (docs/SYNC.md, "Previews"): a small JPEG of the visible page,
// shown while a tab's page loads on any device.

const MAX_WIDTH_PX = 640;
const MAX_BYTES = 256 * 1024;
// Tried in order until the JPEG fits.
const QUALITIES = [0.6, 0.45, 0.3];
export const PREVIEW_INTERVAL_MS = 60_000;

// Injected into every page: reports whether it has a password field, then
// again whenever that changes. A page with one never gets a preview.
export const PASSWORD_PROBE = `(function () {
  var last = null;
  function check() {
    var has = !!document.querySelector('input[type="password"]');
    if (has === last) return;
    last = has;
    window.ReactNativeWebView.postMessage(JSON.stringify({ avoraPasswordField: has }));
  }
  check();
  var timer;
  new MutationObserver(function () {
    clearTimeout(timer);
    timer = setTimeout(check, 400);
  }).observe(document.documentElement, { childList: true, subtree: true, attributes: true, attributeFilter: ['type'] });
})();
true;`;

export function parsePasswordProbe(data: string): boolean | null {
  try {
    const message = JSON.parse(data) as { avoraPasswordField?: unknown };
    return typeof message.avoraPasswordField === 'boolean' ? message.avoraPasswordField : null;
  } catch {
    return null;
  }
}

export type Capture = { bytes: ArrayBuffer; dataUri: string };

// |width| and |height| are the view's size in points. The renderer draws at
// the screen's scale, so the target size is given in points too.
export async function captureJpeg(view: ViewInstance, width: number, height: number): Promise<Capture | null> {
  const tag = findNodeHandle(view);
  if (tag == null || !(width > 0 && height > 0)) return null;
  const scale = PixelRatio.get();
  const w = Math.min(MAX_WIDTH_PX, width * scale) / scale;
  const h = (w * height) / width;
  for (const quality of QUALITIES) {
    const base64 = await captureRef(tag, { format: 'jpg', quality, width: w, height: h, result: 'base64' });
    const bytes = decodeBase64(base64);
    if (bytes.byteLength <= MAX_BYTES) return { bytes, dataUri: `data:image/jpeg;base64,${base64}` };
  }
  return null;
}

function decodeBase64(base64: string): ArrayBuffer {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes.buffer;
}

// --- The settings switch ----------------------------------------------------

const ENABLED_KEY = 'avora.previews.enabled';

let enabled = true;
const listeners = new Set<() => void>();

AsyncStorage.getItem(ENABLED_KEY)
  .then((stored) => {
    if (stored === null) return;
    enabled = stored === 'true';
    listeners.forEach((l) => l());
  })
  .catch(() => {});

export function setPreviewsEnabled(value: boolean) {
  enabled = value;
  listeners.forEach((l) => l());
  AsyncStorage.setItem(ENABLED_KEY, String(value)).catch(() => {});
}

function subscribe(listener: () => void) {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

export function usePreviewsEnabled(): boolean {
  return useSyncExternalStore(subscribe, () => enabled);
}
