import posthog from "posthog-js";

// PostHog's project key is public by design: it can only send events, not read
// them. Without it (local dev, previews without the variable) nothing is sent.
export const posthogKey = process.env.NEXT_PUBLIC_POSTHOG_KEY;

export type DownloadPlacement = "hero" | "download-section" | "sidebar";

// A click on a Download button. The DMG comes from GitHub Releases, so this is
// intent to download, which is as close as the site can see.
export function trackDownload(placement: DownloadPlacement, version: string) {
  if (!posthogKey) return;
  // A beacon survives the page being left, which a cross-origin download link can do.
  posthog.capture(
    "download_clicked",
    { placement, version },
    { transport: "sendBeacon" },
  );
}
