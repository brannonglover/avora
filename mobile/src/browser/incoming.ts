// Web links handed to the app by iOS, when Avora is the default browser.
// They can arrive before the tabs have loaded from storage, so they wait here
// until the browser screen takes them.

type Listener = (url: string) => void;

const pending: string[] = [];
let listener: Listener | null = null;

export function isIncomingWebUrl(url: string): boolean {
  return /^https?:\/\//i.test(url);
}

export function receiveWebUrl(url: string) {
  if (listener) listener(url);
  else pending.push(url);
}

// Takes any links that arrived early, then each new one. Returns the
// unsubscribe.
export function onWebUrl(fn: Listener): () => void {
  listener = fn;
  for (const url of pending.splice(0)) fn(url);
  return () => {
    if (listener === fn) listener = null;
  };
}
