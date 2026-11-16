import { after } from "next/server";
import { renderAppcast } from "@/lib/appcast";
import { publishedReleases } from "@/lib/release";
import { recordUpdateCheck, updateCheckFrom } from "@/lib/update-check-analytics";

// What every installed copy of Avora checks. It's built from releases.json, so
// a release goes out, or comes back, with the deploy that changes that file.
// Reading the request's headers makes this run for every check rather than
// being served as a prerendered file, so each check from Avora can be counted.
export function GET(request: Request) {
  const check = updateCheckFrom(request.headers);
  if (check) after(() => recordUpdateCheck(check));

  return new Response(renderAppcast(publishedReleases), {
    headers: { "Content-Type": "application/rss+xml; charset=utf-8" },
  });
}
