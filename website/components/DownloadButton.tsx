"use client";

import { useEffect, useState } from "react";
import { ArrowDownToLine } from "lucide-react";
import { downloadUrl, minimumMacOS, sizeLabel, version } from "@/lib/release";
import { trackDownload, type DownloadPlacement } from "@/lib/analytics";

type Fit = "ok" | "not-mac" | "intel";

// navigator.userAgentData is Chromium-only and missing from lib.dom.
type UserAgentData = {
  getHighEntropyValues(hints: string[]): Promise<{ architecture?: string }>;
};

async function detectFit(): Promise<Fit> {
  // iPadOS reports itself as a Mac, so touch has to rule it out.
  const isMac =
    /Macintosh/.test(navigator.userAgent) && navigator.maxTouchPoints < 2;
  if (!isMac) return "not-mac";

  // Every Mac's user agent says Intel, so only this API can actually tell.
  // Browsers without it get the button and the requirements line.
  const data = (navigator as Navigator & { userAgentData?: UserAgentData })
    .userAgentData;
  const hints = await data?.getHighEntropyValues(["architecture"]);
  return hints?.architecture === "x86" ? "intel" : "ok";
}

const WARNINGS: Record<Exclude<Fit, "ok">, string> = {
  "not-mac": "Avora is only available for Mac right now.",
  intel: "This Mac looks Intel-based. Avora needs Apple silicon for now.",
};

export function DownloadButton({ placement }: { placement: DownloadPlacement }) {
  const [fit, setFit] = useState<Fit>("ok");

  useEffect(() => {
    detectFit().then(setFit, () => {});
  }, []);

  return (
    <div className="download">
      <a
        className="download-button"
        href={downloadUrl}
        download
        onClick={() => trackDownload(placement, version)}
      >
        <ArrowDownToLine size={20} aria-hidden="true" />
        Download for Mac
      </a>
      <p className="download-meta">
        Version {version} · Apple silicon · macOS {minimumMacOS}+ · {sizeLabel}
      </p>
      {fit !== "ok" && (
        <p className="download-warning" role="status">
          {WARNINGS[fit]}
        </p>
      )}
    </div>
  );
}
