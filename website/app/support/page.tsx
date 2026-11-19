import type { Metadata } from "next";
import type { ReactNode } from "react";
import { Footer } from "@/components/Footer";
import { Sidebar } from "@/components/Sidebar";
import { supportEmail } from "@/lib/site";

const title = "Avora support";
const description =
  "Get help with Avora for iPhone, iPad and Mac, or email the team.";

export const metadata: Metadata = {
  title,
  description,
  alternates: { canonical: "/support" },
  openGraph: { title, description, url: "/support" },
};

const mailto = `mailto:${supportEmail}?subject=${encodeURIComponent("Avora support")}`;

// Each answer describes what the app does today; keep them in step with it.
const QUESTIONS: { q: string; a: ReactNode }[] = [
  {
    q: "How do I make Avora my default browser on iPhone or iPad?",
    a: (
      <>
        Open Settings, go to <strong>Apps → Default Browser App</strong>, and
        choose Avora. Links you tap in Mail, Messages and other apps then open
        in a new Avora tab.
      </>
    ),
  },
  {
    q: "How do I sync my tabs with Avora on my Mac?",
    a: (
      <>
        Open the tab switcher, tap the person icon at the top, and sign in
        with your email. Tap the link in the email on the same device, or type
        the code from it. Sign in with the same email on every device and your
        Spaces, Favorites, pinned tabs and Today tabs stay the same everywhere.
      </>
    ),
  },
  {
    q: "How do I stop syncing?",
    a: (
      <>
        Tap the person icon in the tab switcher and choose{" "}
        <strong>Sign Out</strong>. Your tabs stay on that device; they just
        stop syncing with your others.
      </>
    ),
  },
  {
    q: "I closed a tab by mistake. Can I get it back?",
    a: (
      <>
        Tap <strong>Undo</strong> on the message that appears when you close
        it. Recently closed tabs are also listed on the start page and under
        the <strong>⋯</strong> menu&apos;s Reopen Closed Tab.
      </>
    ),
  },
  {
    q: "How do I move between tabs and Spaces?",
    a: (
      <>
        Swipe the address bar left or right to go to the next or previous tab,
        or swipe it up to see all your tabs. The Space button in the bottom
        bar opens the tab switcher, where the chips along the top switch
        between Spaces.
      </>
    ),
  },
  {
    q: "How do I delete my account and synced data?",
    a: (
      <>
        In the Avora app, open the tab switcher, tap the person icon at the
        top, then <strong>Delete Account</strong>. This deletes the
        account, the Spaces and tabs synced to it, and their page previews, on
        every device. Tabs already on your phone stay there but stop syncing.
        If you can&apos;t use the app, email{" "}
        <a href={mailto}>{supportEmail}</a> from the address you signed in
        with and we&apos;ll delete the account and confirm by email.
      </>
    ),
  },
];

export default function SupportPage() {
  return (
    <div className="shell">
      <Sidebar page="support" />
      <main className="content">
        <section className="section support">
          <header className="section-head">
            <p className="eyebrow">Support</p>
            <h1>How can we help?</h1>
            <p className="lede">
              Questions, problems or ideas about Avora on iPhone, iPad or Mac:
              email us and a person will get back to you.
            </p>
          </header>

          <div className="support-contact">
            <a className="support-email" href={mailto}>
              {supportEmail}
            </a>
            <p>
              It helps to say which device you&apos;re on and which version of
              Avora you have. On Mac, Avora → About Avora shows the version.
            </p>
          </div>

          <div className="faq">
            <h2>Common questions</h2>
            {QUESTIONS.map(({ q, a }) => (
              <details key={q} className="faq-item">
                <summary>{q}</summary>
                <p>{a}</p>
              </details>
            ))}
          </div>

          <p className="note">
            Looking for what changed in the Mac app? See the{" "}
            <a href="/releases">release notes</a>. How Avora handles your data
            is in the <a href="/privacy">privacy policy</a>.
          </p>
        </section>
        <Footer />
      </main>
    </div>
  );
}
