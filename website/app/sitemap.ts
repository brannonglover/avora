import type { MetadataRoute } from "next";
import { screenshots, siteUrl } from "@/lib/site";

export default function sitemap(): MetadataRoute.Sitemap {
  return [
    {
      url: siteUrl,
      images: screenshots.map((path) => `${siteUrl}${path}`),
    },
  ];
}
