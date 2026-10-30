import type { Metadata } from "next";
import { notFound } from "next/navigation";
import { connection } from "next/server";
import { Suspense } from "react";
import { readReleases } from "@/lib/release-store";
import { AdminReleases } from "./AdminReleases";
import styles from "./admin.module.css";

export const metadata: Metadata = {
  title: "Releases",
  robots: { index: false },
};

async function ReleaseList() {
  await connection();
  return <AdminReleases initialReleases={await readReleases()} />;
}

// Releases are signed on the Mac that builds them and saved to releases.json in
// the repo, so this only runs under `npm run dev`. There's no login because a
// deployed site has no page here to log in to.
export default function AdminReleasesPage() {
  if (process.env.NODE_ENV !== "development") notFound();

  return (
    <main className={styles.page}>
      <header className={styles.header}>
        <h1>Releases</h1>
        <p>
          Edit the notes for a build, then publish it. Publishing saves{" "}
          <code>website/releases.json</code>. Commit and deploy that file to send
          the update to everyone.
        </p>
      </header>
      <Suspense fallback={<p className={styles.empty}>Loading releases…</p>}>
        <ReleaseList />
      </Suspense>
    </main>
  );
}
