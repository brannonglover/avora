// Where PostHog lives. Events go to /ingest on this site, which next.config.ts
// forwards here, so ad blockers that block PostHog's own domain don't hide
// visitors. For an EU project, swap "us" for "eu" in all three.
export const posthogIngestHost = "https://us.i.posthog.com";
export const posthogAssetsHost = "https://us-assets.i.posthog.com";
export const posthogUiHost = "https://us.posthog.com";
