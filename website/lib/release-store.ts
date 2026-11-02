import { execFile } from "node:child_process";
import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { promisify } from "node:util";
import {
  compareVersions,
  releaseDownloadUrl,
  type Release,
  type ReleaseStatus,
} from "./release";

// Only the local admin uses this. Vercel's filesystem is read-only, and the
// site itself reads releases.json at build time through lib/release.ts.
const RELEASES_PATH = path.join(process.cwd(), "releases.json");

const run = promisify(execFile);

export async function readReleases(): Promise<Release[]> {
  const releases = JSON.parse(
    await readFile(RELEASES_PATH, "utf8"),
  ) as Release[];
  return releases.sort((a, b) => compareVersions(b.version, a.version));
}

// Every installed copy acts on a published release, so this stops anything that
// would fail on people's Macs rather than warning about it.
async function publishBlocker(release: Release): Promise<string | null> {
  if (release.bullets.length === 0) return "Add at least one bullet.";
  if (!release.notarized) return "This build isn't notarized.";
  if (!release.edSignature) return "This build has no update signature.";

  const url = releaseDownloadUrl(release);
  const response = await fetch(url, { method: "HEAD" }).catch(() => null);
  if (!response?.ok) return `The DMG isn't at ${url} yet.`;
  const size = Number(response.headers.get("content-length"));
  if (size !== release.sizeBytes) {
    return `The uploaded DMG is ${size} bytes, not this build's ${release.sizeBytes}.`;
  }
  return null;
}

// Pushing main deploys the site, and the deploy is what changes the feed. Only
// this file is committed, whatever else is in the working tree or the index.
async function shipReleases(message: string) {
  try {
    await run("git", ["commit", "-m", message, "--", "releases.json"]);
    await run("git", ["push"]);
    return {
      notice:
        "Pushed. The site deploys in about a minute, and updates go out from there.",
    };
  } catch (error) {
    const detail = (error as { stderr?: string }).stderr?.trim();
    return {
      warning: `Saved here, but not sent: ${detail || "git failed"}. Commit and push releases.json to send it.`,
    };
  }
}

function shipMessage(before: Release, after: Release): string {
  if (before.status !== "published") return `Publish Avora ${after.version}.`;
  if (after.status !== "published") return `Pull Avora ${after.version}.`;
  return `Update the notes for Avora ${after.version}.`;
}

type ReleaseEdit = {
  title: string | null;
  bullets: string[];
  status?: ReleaseStatus;
};

export async function updateRelease(version: string, edit: ReleaseEdit) {
  const releases = await readReleases();
  const release = releases.find((row) => row.version === version);
  if (!release) return { ok: false as const, error: "Release not found" };

  const next: Release = {
    ...release,
    title: edit.title?.trim() || null,
    bullets: edit.bullets.map((bullet) => bullet.trim()).filter(Boolean),
    status: edit.status ?? release.status,
  };

  if (next.status !== "published") {
    next.publishedAt = null;
  } else if (release.status !== "published") {
    const blocker = await publishBlocker(next);
    if (blocker) return { ok: false as const, error: blocker };
    next.publishedAt = new Date().toISOString();
  }

  const updated = releases.map((row) => (row === release ? next : row));
  if (!updated.some((row) => row.status === "published")) {
    return { ok: false as const, error: "The site needs a published release." };
  }

  const text = `${JSON.stringify(updated, null, 2)}\n`;
  const changed = text !== (await readFile(RELEASES_PATH, "utf8"));
  await writeFile(RELEASES_PATH, text);

  const live = release.status === "published" || next.status === "published";
  const shipped =
    live && changed ? await shipReleases(shipMessage(release, next)) : {};
  return { ok: true as const, release: next, ...shipped };
}
