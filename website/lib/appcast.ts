import { minimumMacOS, releaseDownloadUrl, type Release } from "./release";
import { siteUrl } from "./site";

function escapeXml(value: string): string {
  return value
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

// Sparkle shows this in its update window. Escaping also rules out a "]]>" in a
// bullet ending the CDATA section it goes into.
function releaseNotes(release: Release): string {
  const heading = release.title ? `<h3>${escapeXml(release.title)}</h3>` : "";
  const bullets = release.bullets
    .map((bullet) => `<li>${escapeXml(bullet)}</li>`)
    .join("");
  return `${heading}<ul>${bullets}</ul>`;
}

function renderItem(release: Release): string {
  const version = escapeXml(release.version);
  const url = escapeXml(releaseDownloadUrl(release));
  const signature = escapeXml(release.edSignature ?? "");
  const published = new Date(release.publishedAt ?? release.createdAt);

  return `    <item>
      <title>Version ${version}</title>
      <pubDate>${published.toUTCString()}</pubDate>
      <sparkle:version>${version}</sparkle:version>
      <sparkle:shortVersionString>${version}</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>${minimumMacOS}.0.0</sparkle:minimumSystemVersion>
      <sparkle:hardwareRequirements>arm64</sparkle:hardwareRequirements>
      <description><![CDATA[${releaseNotes(release)}]]></description>
      <enclosure url="${url}" length="${release.sizeBytes}" type="application/octet-stream" sparkle:edSignature="${signature}"/>
    </item>`;
}

// The Sparkle feed. A release without a signature can't be installed by the
// updater, so it stays out rather than failing on every Mac that tries it.
export function renderAppcast(releases: Release[]): string {
  const items = releases.filter((release) => release.edSignature);
  return `<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>Avora</title>
    <link>${siteUrl}</link>
    <description>Updates to Avora</description>
    <language>en</language>
${items.map(renderItem).join("\n")}
  </channel>
</rss>
`;
}
