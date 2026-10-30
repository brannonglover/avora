import type { NextConfig } from "next";
import { siteUrl } from "./lib/site";

const nextConfig: NextConfig = {
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
