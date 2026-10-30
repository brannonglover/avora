import { DownloadButton } from "./DownloadButton";
import { notarized, sha256 } from "@/lib/release";

export function Download() {
  return (
    <section id="download" className="section download-section">
      <div className="aurora aurora-low" aria-hidden="true">
        <span />
        <span />
        <span />
      </div>

      <header className="section-head">
        <p className="eyebrow">Download</p>
        <h2>Move in. It takes a minute.</h2>
      </header>

      <DownloadButton />

      <ol className="install">
        <li>
          <strong>Open the download.</strong> Double-click the Avora DMG.
        </li>
        <li>
          <strong>Drag Avora to Applications.</strong> It installs like any Mac
          app.
        </li>
        <li>
          <strong>Launch it.</strong> Avora offers to bring your bookmarks with
          you the first time it opens.
        </li>
      </ol>

      {!notarized && (
        <p className="note">
          This preview build isn&apos;t notarized yet, so macOS will stop the
          first launch. Open System Settings, go to Privacy &amp; Security, and
          choose Open Anyway.
        </p>
      )}

      {sha256 && (
        <p className="checksum">
          SHA-256 <code>{sha256}</code>
        </p>
      )}
    </section>
  );
}
