import type { Metadata, Viewport } from "next";
import { Sora } from "next/font/google";
import "./globals.css";

const sora = Sora({ variable: "--font-display", subsets: ["latin"] });

const description =
  "Avora is a Chromium browser for the Mac with vertical tabs, Spaces that keep their own sign-ins, and Intelligence that explains why a page looks the way it does.";

export const metadata: Metadata = {
  metadataBase: new URL(
    process.env.NEXT_PUBLIC_SITE_URL ?? "http://localhost:3000",
  ),
  title: "Avora: a place for every tab",
  description,
  openGraph: {
    title: "Avora",
    description,
    images: [{ url: "/shots/spaces.png", width: 1600, height: 1000 }],
    type: "website",
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
