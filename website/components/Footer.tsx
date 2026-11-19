import { chromiumMajor, version } from "@/lib/release";

export function Footer() {
  return (
    <footer className="footer">
      <span>
        Avora {version}, built on Chromium {chromiumMajor}
      </span>
      <span>
        <a href="/support">Support</a> · <a href="/privacy">Privacy</a> · © 2026 Avora
      </span>
    </footer>
  );
}
