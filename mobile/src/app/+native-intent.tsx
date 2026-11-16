import { isIncomingWebUrl, receiveWebUrl } from '../browser/incoming';

// When Avora is the default browser, iOS opens http and https links here.
// Left to the router, https://example.com/news would look for a /news screen,
// so web links go to the browser screen, which opens each in a new tab.
export function redirectSystemPath({ path }: { path: string; initial: boolean }): string {
  try {
    if (isIncomingWebUrl(path)) {
      receiveWebUrl(path);
      return '/';
    }
  } catch {
    // Throwing here can crash the app; let the router have the link instead.
  }
  return path;
}
