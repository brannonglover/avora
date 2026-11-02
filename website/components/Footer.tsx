import { chromiumMajor, version } from "@/lib/release";

export function Footer() {
  return (
    <footer className="footer">
      <span>
        Avora {version}, built on Chromium {chromiumMajor}
      </span>
      <span>© 2026 Avora</span>
    </footer>
  );
}
