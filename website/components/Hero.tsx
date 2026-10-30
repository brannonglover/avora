import Image from "next/image";
import { DownloadButton } from "./DownloadButton";

export function Hero() {
  return (
    <section id="overview" className="hero">
      <div className="aurora" aria-hidden="true">
        <span />
        <span />
        <span />
      </div>

      <div className="hero-copy">
        <p className="eyebrow">A Chromium browser for the Mac</p>
        <h1>A place for every tab.</h1>
        <p className="lede">
          Avora runs your tabs down the side, gives work and life their own
          Spaces and sign-ins, and tells you exactly why a page looks the way
          it does.
        </p>
        <DownloadButton />
      </div>

      <Image
        className="shot hero-shot"
        src="/shots/hero.png"
        alt="Avora with the Research Space open: Favorites, a Pull Requests Live Folder, pinned pages and today's tabs in the sidebar, and a Wikipedia article on auroras in the main window."
        width={1600}
        height={1000}
        sizes="(max-width: 900px) 100vw, 1100px"
        preload
      />
    </section>
  );
}
