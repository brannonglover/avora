import { Sidebar } from "@/components/Sidebar";
import { Hero } from "@/components/Hero";
import { Tour } from "@/components/Tour";
import { Intelligence } from "@/components/Intelligence";
import { Download } from "@/components/Download";
import { StructuredData } from "@/components/StructuredData";
import { chromiumMajor, version } from "@/lib/release";

export default function Home() {
  return (
    <div className="shell">
      <Sidebar />
      <main className="content">
        <Hero />
        <Tour />
        <Intelligence />
        <Download />
        <footer className="footer">
          <span>
            Avora {version}, built on Chromium {chromiumMajor}
          </span>
          <span>© 2026 Avora</span>
        </footer>
      </main>
      <StructuredData />
    </div>
  );
}
