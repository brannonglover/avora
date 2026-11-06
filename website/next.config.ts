import type { NextConfig } from "next";
import { siteUrl } from "./lib/site";
import { posthogAssetsHost, posthogIngestHost } from "./lib/posthog-hosts";

const nextConfig: NextConfig = {
  // PostHog's API paths end in a slash, which Next would otherwise redirect away.
  skipTrailingSlashRedirect: true,
  // PostHog, proxied through this site. See lib/posthog-hosts.ts.
  async rewrites() {
    return [
      {
        source: "/ingest/static/:path*",
        destination: `${posthogAssetsHost}/static/:path*`,
      },
      {
        source: "/ingest/array/:path*",
        destination: `${posthogAssetsHost}/array/:path*`,
      },
      {
        source: "/ingest/:path*",
        destination: `${posthogIngestHost}/:path*`,
      },
    ];
  },
  cacheComponents: true,
  partialPrefetching: true,
  // The badge sits in the bottom-left corner, on top of the Spaces bar.
  devIndicators: false,
  // The vercel.app address serves the site too, and search engines shouldn't see
  // a second copy.
  async redirects() {
    return [
      {
        source: "/:path*",
        has: [{ type: "host", value: "avora-rouge-phi.vercel.app" }],
        destination: `${siteUrl}/:path*`,
        permanent: true,
      },
    ];
  },
};

export default nextConfig;
