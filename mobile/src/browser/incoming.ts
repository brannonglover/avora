// Web links handed to the app by iOS, when Avora is the default browser.
// They can arrive before the tabs have loaded from storage, so they wait here
// until the browser screen takes them.

type Listener = (url: string) => void;

const pending: string[] = [];
let listener: Listener | null = null;

export function isIncomingWebUrl(url: string): boolean {
  return /^https?:\/\//i.test(url);
}

export function receiveWebUrl(raw: string) {
  const url = unwrapRedirector(raw);
  if (listener) listener(url);
  else pending.push(url);
}

// Gmail and other Google apps hand over links wrapped in google.com/url, a
// page whose only job is to send you on. Going straight there saves a load
// and leaves no google.com tab behind.
const REDIRECTOR_RE = /^https?:\/\/(www\.)?google\.com\/url\?(?:[^#]*?&)?(?:q|url)=([^&#]*)/i;

export function unwrapRedirector(url: string): string {
  const match = url.match(REDIRECTOR_RE);
  if (!match) return url;
  try {
    const target = decodeURIComponent(match[2].replace(/\+/g, ' '));
    return isIncomingWebUrl(target) ? target : url;
  } catch {
    return url;
  }
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
