import { Sidebar } from "@/components/Sidebar";
import { Hero } from "@/components/Hero";
import { Tour } from "@/components/Tour";
import { Intelligence } from "@/components/Intelligence";
import { Download } from "@/components/Download";
import { Footer } from "@/components/Footer";
import { StructuredData } from "@/components/StructuredData";

export default function Home() {
  return (
    <div className="shell">
      <Sidebar />
      <main className="content">
        <Hero />
        <Tour />
        <Intelligence />
        <Download />
        <Footer />
      </main>
      <StructuredData />
    </div>
  );
}
