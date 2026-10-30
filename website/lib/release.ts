import data from "../releases.json";

export type ReleaseStatus = "draft" | "published" | "discarded";

// One row of releases.json. `npm run dmg` writes a draft, and the admin at
// /admin/releases edits its notes and publishes it. Nothing here reaches the
// download buttons or the update feed until its status is "published".
export type Release = {
  version: string;
  chromium: string;
  file: string;
  sizeBytes: number;
  sha256: string;
  notarized: boolean;
  // The DMG's EdDSA signature. Null for builds made without Sparkle, which the
  // update feed has to leave out.
  edSignature: string | null;
  title: string | null;
  bullets: string[];
  status: ReleaseStatus;
  createdAt: string;
  publishedAt: string | null;
};

// Newest first when used with Array.sort((a, b) => compareVersions(b, a)).
export function compareVersions(a: string, b: string): number {
  const left = a.split(".").map(Number);
  const right = b.split(".").map(Number);
  for (let i = 0; i < Math.max(left.length, right.length); i++) {
    const difference = (left[i] ?? 0) - (right[i] ?? 0);
    if (difference !== 0) return difference;
  }
  return 0;
}

// The DMG is too big for Vercel's public/, so it lives on GitHub Releases.
export function releaseDownloadUrl(release: Release): string {
  return `https://github.com/brannonglover/avora/releases/download/v${release.version}/${release.file}`;
}

export const publishedReleases = (data as Release[])
  .filter((release) => release.status === "published")
  .sort((a, b) => compareVersions(b.version, a.version));

const latest = publishedReleases[0];
if (!latest) throw new Error("releases.json has no published release.");

export const downloadUrl = releaseDownloadUrl(latest);
export const version = latest.version;
export const chromiumMajor = latest.chromium.split(".")[0];
export const sizeLabel = `${Math.round(latest.sizeBytes / 1_000_000)} MB`;
export const sha256 = latest.sha256;
export const minimumMacOS = "13";

// False until the DMG is signed with a Developer ID and notarized. Until then
// macOS refuses the first launch, so the install steps have to say how to get
// past that.
export const notarized = latest.notarized;
