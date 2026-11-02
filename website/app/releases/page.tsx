import type { Metadata } from "next";
import { Footer } from "@/components/Footer";
import { Sidebar } from "@/components/Sidebar";
import { publishedReleases, releaseAnchor, type Release } from "@/lib/release";

const title = "Avora release notes";
const description = "What changed in each version of Avora.";

export const metadata: Metadata = {
  title,
  description,
  alternates: { canonical: "/releases" },
  openGraph: { title, description, url: "/releases" },
};

const dateFormat = new Intl.DateTimeFormat("en-US", {
  dateStyle: "long",
  timeZone: "America/New_York",
});

// The version stays beside its notes as they scroll, so a long list is never
// separated from the release it belongs to.
function ReleaseEntry({ release, latest }: { release: Release; latest: boolean }) {
  const published = release.publishedAt ?? release.createdAt;
  return (
    <article id={releaseAnchor(release.version)} className="release">
      <header className="release-head">
        <h2>
          <span className="release-kicker">Version</span> {release.version}
        </h2>
        {latest && <span className="release-latest">Latest</span>}
        <p className="release-meta">
          <time dateTime={published}>
            {dateFormat.format(new Date(published))}
          </time>
          <br />
          Chromium {release.chromium.split(".")[0]}
        </p>
      </header>
      <div className="release-body">
        {release.title && <h3>{release.title}</h3>}
        {release.bullets.length > 0 ? (
          <ul>
            {release.bullets.map((bullet) => (
              <li key={bullet}>{bullet}</li>
            ))}
          </ul>
        ) : (
          <p className="release-empty">No notes were written for this release.</p>
        )}
      </div>
    </article>
  );
}

// The same published rows as the update feed, so the notes here are the ones
// people read in Avora's update window.
export default function ReleasesPage() {
  return (
    <div className="shell">
      <Sidebar page="releases" />
      <main className="content">
        <section className="section releases">
          <header className="section-head">
            <p className="eyebrow">Release notes</p>
            <h1>What&apos;s new in Avora</h1>
          </header>
          <div className="release-list">
            {publishedReleases.map((release, index) => (
              <ReleaseEntry
                key={release.version}
                release={release}
                latest={index === 0}
              />
            ))}
          </div>
        </section>
        <Footer />
      </main>
    </div>
  );
}
