import { downloadUrl, minimumMacOS, sizeLabel, version } from "@/lib/release";
import { screenshots, siteDescription, siteUrl } from "@/lib/site";

const organizationId = `${siteUrl}/#organization`;

const data = {
  "@context": "https://schema.org",
  "@graph": [
    {
      "@type": "WebSite",
      "@id": `${siteUrl}/#website`,
      url: `${siteUrl}/`,
      name: "Avora",
      publisher: { "@id": organizationId },
    },
    {
      "@type": "Organization",
      "@id": organizationId,
      name: "Avora",
      url: `${siteUrl}/`,
      logo: `${siteUrl}/avora-mark.svg`,
      sameAs: ["https://github.com/brannonglover/avora"],
    },
    {
      "@type": "SoftwareApplication",
      "@id": `${siteUrl}/#software`,
      name: "Avora",
      url: `${siteUrl}/`,
      description: siteDescription,
      applicationCategory: "BrowserApplication",
      operatingSystem: `macOS ${minimumMacOS} or later`,
      processorRequirements: "Apple silicon",
      softwareVersion: version,
      fileSize: sizeLabel,
      downloadUrl: new URL(downloadUrl, siteUrl).href,
      image: `${siteUrl}${screenshots[0]}`,
      screenshot: screenshots.map((path) => `${siteUrl}${path}`),
      featureList: [
        "Vertical tabs",
        "Spaces with separate cookies and logins",
        "Link previews",
        "Mini windows for links from other apps",
        "Live Folders for GitHub pull requests",
        "Import from Arc, Chrome, Safari and Firefox",
        "Intelligence in DevTools that explains a page's layout and CSS",
      ],
      offers: { "@type": "Offer", price: "0", priceCurrency: "USD" },
      publisher: { "@id": organizationId },
    },
  ],
};

export function StructuredData() {
  return (
    <script
      type="application/ld+json"
      dangerouslySetInnerHTML={{
        __html: JSON.stringify(data).replace(/</g, "\\u003c"),
      }}
    />
  );
}
