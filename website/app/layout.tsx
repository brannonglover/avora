import type { Metadata, Viewport } from "next";
import { Sora } from "next/font/google";
import { siteDescription as description, siteUrl } from "@/lib/site";
import "./globals.css";

const sora = Sora({ variable: "--font-display", subsets: ["latin"] });

const title = "Avora: the Mac browser with vertical tabs and Spaces";

export const metadata: Metadata = {
  metadataBase: new URL(siteUrl),
  title,
  description,
  alternates: { canonical: "/" },
  openGraph: {
    title,
    description,
    url: "/",
    siteName: "Avora",
    type: "website",
  },
  twitter: {
    card: "summary_large_image",
    title,
    description,
  },
};

export const viewport: Viewport = {
  themeColor: "#181c20",
  colorScheme: "dark",
};

export default function RootLayout({ children }: LayoutProps<"/">) {
  return (
    <html lang="en" className={sora.variable}>
      <body>{children}</body>
    </html>
  );
}
