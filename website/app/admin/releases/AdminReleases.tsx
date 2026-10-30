"use client";

import { useState } from "react";
import type { Release, ReleaseStatus } from "@/lib/release";
import styles from "./admin.module.css";

const STATUS_LABEL: Record<ReleaseStatus, string> = {
  draft: "Draft",
  published: "Published",
  discarded: "Discarded",
};

function parseBullets(text: string): string[] {
  return text
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean);
}

export function AdminReleases({
  initialReleases,
}: {
  initialReleases: Release[];
}) {
  const [releases, setReleases] = useState(initialReleases);
  const [selectedVersion, setSelectedVersion] = useState(
    initialReleases.find((release) => release.status === "draft")?.version ??
      initialReleases[0]?.version,
  );
  const selected = releases.find(
    (release) => release.version === selectedVersion,
  );

  if (!selected) {
    return (
      <p className={styles.empty}>
        No releases yet. <code>npm run dmg</code> creates a draft.
      </p>
    );
  }

  return (
    <div className={styles.layout}>
      <ul className={styles.list}>
        {releases.map((release) => (
          <li key={release.version}>
            <button
              type="button"
              className={styles.row}
              aria-current={release === selected}
              onClick={() => setSelectedVersion(release.version)}
            >
              <span className={styles.rowVersion}>{release.version}</span>
              <span className={styles.badge} data-status={release.status}>
                {STATUS_LABEL[release.status]}
              </span>
            </button>
          </li>
        ))}
      </ul>

      <ReleaseEditor
        key={selected.version}
        release={selected}
        onSaved={(saved) =>
          setReleases((rows) =>
            rows.map((row) => (row.version === saved.version ? saved : row)),
          )
        }
      />
    </div>
  );
}

function ReleaseEditor({
  release,
  onSaved,
}: {
  release: Release;
  onSaved: (release: Release) => void;
}) {
  const [title, setTitle] = useState(release.title ?? "");
  const [bulletsText, setBulletsText] = useState(release.bullets.join("\n"));
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const bullets = parseBullets(bulletsText);

  const save = async (status?: ReleaseStatus) => {
    setSaving(true);
    setError(null);
    try {
      const response = await fetch(`/api/admin/releases/${release.version}`, {
        method: "PATCH",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ title: title.trim() || null, bullets, status }),
      });
      const data = (await response.json().catch(() => null)) as {
        release?: Release;
        error?: string;
      } | null;
      if (response.ok && data?.release) {
        onSaved(data.release);
      } else {
        setError(data?.error ?? "Could not save the release.");
      }
    } catch {
      setError("Could not save the release.");
    } finally {
      setSaving(false);
    }
  };

  return (
    <section className={styles.editor}>
      <div className={styles.editorHead}>
        <div>
          <h2>{release.version}</h2>
          <p className={styles.file}>{release.file}</p>
        </div>
        <span className={styles.badge} data-status={release.status}>
          {STATUS_LABEL[release.status]}
        </span>
      </div>

      <ul className={styles.checks}>
        <li data-ok={release.notarized}>
          {release.notarized ? "Notarized" : "Not notarized"}
        </li>
        <li data-ok={Boolean(release.edSignature)}>
          {release.edSignature ? "Signed for updates" : "No update signature"}
        </li>
        <li>{Math.round(release.sizeBytes / 1_000_000)} MB</li>
        <li>Chromium {release.chromium}</li>
      </ul>

      {error && (
        <p className={styles.error} role="alert">
          {error}
        </p>
      )}

      <label className={styles.field}>
        <span>Title (optional)</span>
        <input
          type="text"
          value={title}
          onChange={(event) => setTitle(event.target.value)}
          placeholder="Short headline for this release"
        />
      </label>

      <label className={styles.field}>
        <span>Bullets (one per line)</span>
        <textarea
          value={bulletsText}
          onChange={(event) => setBulletsText(event.target.value)}
          rows={10}
          placeholder={"Added …\nImproved …\nFixed …"}
        />
      </label>

      <div className={styles.preview}>
        <p>What people see in the update window</p>
        {title.trim() && <h3>{title.trim()}</h3>}
        {bullets.length === 0 ? (
          <p>Add one change per line above.</p>
        ) : (
          <ul>
            {bullets.map((bullet, index) => (
              <li key={`${index}-${bullet}`}>{bullet}</li>
            ))}
          </ul>
        )}
      </div>

      <div className={styles.actions}>
        <button type="button" disabled={saving} onClick={() => void save()}>
          {saving ? "Saving…" : "Save changes"}
        </button>
        {release.status !== "published" && (
          <button
            type="button"
            className={styles.primary}
            disabled={saving}
            onClick={() => void save("published")}
          >
            Publish
          </button>
        )}
        {release.status === "discarded" ? (
          <button
            type="button"
            disabled={saving}
            onClick={() => void save("draft")}
          >
            Restore to draft
          </button>
        ) : (
          <button
            type="button"
            className={styles.danger}
            disabled={saving}
            onClick={() => void save("discarded")}
          >
            Discard
          </button>
        )}
      </div>
    </section>
  );
}
