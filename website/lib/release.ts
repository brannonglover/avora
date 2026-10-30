import release from "../release.json";

// release.json is written by `npm run dmg`. When the DMG moves to real hosting,
// set NEXT_PUBLIC_DOWNLOAD_URL and the buttons follow; the file itself is far
// too big to deploy from public/.
export const downloadUrl =
  process.env.NEXT_PUBLIC_DOWNLOAD_URL ?? `/downloads/${release.file}`;

export const version = release.version;
export const chromiumMajor = release.chromium.split(".")[0];
export const sizeLabel = `${Math.round(release.sizeBytes / 1_000_000)} MB`;
export const sha256 = release.sha256;
export const minimumMacOS = "13";

// False until the DMG is signed with a Developer ID and notarized. Until then
// macOS refuses the first launch, so the install steps have to say how to get
// past that.
export const notarized = release.notarized;
