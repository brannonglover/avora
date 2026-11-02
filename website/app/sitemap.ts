import type { MetadataRoute } from "next";
import { publishedReleases } from "@/lib/release";
import { screenshots, siteUrl } from "@/lib/site";

export default function sitemap(): MetadataRoute.Sitemap {
  return [
    {
      url: siteUrl,
      images: screenshots.map((path) => `${siteUrl}${path}`),
    },
    {
      url: `${siteUrl}/releases`,
      lastModified: publishedReleases[0].publishedAt ?? undefined,
    },
  ];
}
