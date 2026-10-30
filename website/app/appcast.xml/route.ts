import { renderAppcast } from "@/lib/appcast";
import { publishedReleases } from "@/lib/release";

// What every installed copy of Avora checks. It's built from releases.json, so
// a release goes out, or comes back, with the deploy that changes that file.
export function GET() {
  return new Response(renderAppcast(publishedReleases), {
    headers: { "Content-Type": "application/rss+xml; charset=utf-8" },
  });
}
