import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  cacheComponents: true,
  partialPrefetching: true,
  // The badge sits in the bottom-left corner, on top of the Spaces bar.
  devIndicators: false,
};

export default nextConfig;
