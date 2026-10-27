// Same engine the desktop omnibox falls back to (avora_search_engine.cc).
const SEARCH_URL = 'https://www.google.com/search?q=';

const SCHEME_RE = /^[a-z][a-z0-9+.-]*:/i;
const HOST_RE = /^(localhost|\[[0-9a-f:]+\]|(\d{1,3}\.){3}\d{1,3}|([a-z0-9-]+\.)+[a-z]{2,})(:\d+)?(\/.*)?$/i;

// Turns whatever was typed into the address bar into something loadable:
// a URL when it looks like one, otherwise a search.
export function resolveInput(raw: string): string {
  const input = raw.trim();
  if (!input) return '';

  if (SCHEME_RE.test(input) && !/\s/.test(input)) {
    // "localhost:8081" parses as scheme "localhost"; treat it as a host.
    if (/^localhost:\d+/i.test(input)) return `http://${input}`;
    return input;
  }

  if (!/\s/.test(input) && HOST_RE.test(input)) {
    const isLocal = /^(localhost|(\d{1,3}\.){3}\d{1,3}|\[)/i.test(input);
    return `${isLocal ? 'http' : 'https'}://${input}`;
  }

  return SEARCH_URL + encodeURIComponent(input);
}

export function hostOf(url: string | null | undefined): string {
  if (!url) return '';
  const match = url.match(/^[a-z][a-z0-9+.-]*:\/\/([^/?#]+)/i);
  if (!match) return url;
  return match[1].replace(/^www\./i, '').replace(/:\d+$/, '');
}

export function isSecure(url: string | null | undefined): boolean {
  return !!url && url.startsWith('https://');
}

export function isSearchUrl(url: string | null | undefined): boolean {
  return !!url && url.startsWith(SEARCH_URL);
}

// Plain http to a real host. Local dev servers don't count.
export function isInsecure(url: string | null | undefined): boolean {
  if (!url?.startsWith('http://')) return false;
  return !/^(localhost|(\d{1,3}\.){3}\d{1,3}|\[)/i.test(hostOf(url));
}

const SECOND_LEVEL = new Set(['co', 'com', 'org', 'net', 'ac', 'gov', 'edu']);

// Splits a host into its subdomains and the registrable domain, so the address
// bar can dim "maps." and emphasize "google.com".
export function splitHost(host: string): { sub: string; domain: string } {
  if (/^[\d.]+$/.test(host) || host.startsWith('[')) return { sub: '', domain: host };
  const labels = host.split('.');
  const tld = labels[labels.length - 1];
  const keep =
    labels.length > 2 && tld.length === 2 && SECOND_LEVEL.has(labels[labels.length - 2]) ? 3 : 2;
  if (labels.length <= keep) return { sub: '', domain: host };
  return {
    sub: `${labels.slice(0, -keep).join('.')}.`,
    domain: labels.slice(-keep).join('.'),
  };
}

// What the address bar shows when it isn't being edited. Searches read as the
// query rather than a google.com URL.
export function displayText(url: string | null | undefined): string {
  if (!url) return '';
  if (isSearchUrl(url)) {
    const q = url.slice(SEARCH_URL.length).split(/[&#]/)[0];
    try {
      return decodeURIComponent(q.replace(/\+/g, ' '));
    } catch {
      return q;
    }
  }
  return hostOf(url);
}

export function isWebUrl(url: string): boolean {
  return /^(https?|about|data|blob|file):/i.test(url);
}

export function faviconUrl(url: string | null | undefined): string | null {
  const host = hostOf(url);
  if (!host || host === url) return null;
  return `https://www.google.com/s2/favicons?domain=${encodeURIComponent(host)}&sz=64`;
}
