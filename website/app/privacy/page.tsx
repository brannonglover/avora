import type { Metadata } from "next";
import { Footer } from "@/components/Footer";
import { Sidebar } from "@/components/Sidebar";
import { supportEmail } from "@/lib/site";

const title = "Avora privacy policy";
const description =
  "What Avora for Mac, iPhone and iPad collects, where it goes, and how to delete it.";

export const metadata: Metadata = {
  title,
  description,
  alternates: { canonical: "/privacy" },
  openGraph: { title, description, url: "/privacy" },
};

// Change this whenever the policy changes, and keep the App Store's App
// Privacy answers in step with what this page says.
const EFFECTIVE = "November 19, 2026";

const mail = <a href={`mailto:${supportEmail}`}>{supportEmail}</a>;

// Every statement here describes what the apps and site do today. When a
// feature starts sending something new, update the matching section.
export default function PrivacyPage() {
  return (
    <div className="shell">
      <Sidebar page="privacy" />
      <main className="content">
        <section className="section policy">
          <header className="section-head">
            <p className="eyebrow">Privacy</p>
            <h1>Privacy policy</h1>
            <p className="lede">
              Avora is a browser, so it sees a lot. This page explains the
              little that leaves your device, why, and how to remove it.
            </p>
            <p className="policy-date">Effective {EFFECTIVE}</p>
          </header>

          <div className="policy-body">
            <h2>The short version</h2>
            <ul>
              <li>
                Without an account, your tabs, history and browsing data stay on
                your device.
              </li>
              <li>
                If you sign in to sync, we store your email address and your
                synced tabs so they can appear on your other devices.
              </li>
              <li>
                We don&apos;t sell your data, show ads, or use your browsing to
                build a profile of you.
              </li>
              <li>
                You can stop syncing at any time, and delete your account and
                everything synced to it from the app.
              </li>
            </ul>

            <h2>Signing in and sync</h2>
            <p>
              Signing in is optional. If you do, Avora stores the following so
              your devices can share one set of tabs:
            </p>
            <ul>
              <li>
                <strong>Your email address</strong>, used to sign you in with a
                one-time link or code. Those emails are sent through our email
                provider.
              </li>
              <li>
                <strong>Your Spaces and tabs</strong>: Space names and colours,
                and the address and title of your Favorites, pinned tabs,
                folders and Today tabs.
              </li>
              <li>
                <strong>Your devices</strong>: a random ID for each device, its
                type (for example &ldquo;iPhone&rdquo; or &ldquo;Mac&rdquo;),
                and when it last synced.
              </li>
              <li>
                <strong>Page previews</strong>: small snapshots of open tabs so
                your other devices can show what each tab looks like. A device
                never takes a preview of a page that has a password field. On
                iPhone and iPad you can turn previews off under Account →
                Share page previews.
              </li>
            </ul>
            <p>
              This data is stored with our database provider and protected so
              that only your signed-in devices can read it. Your history,
              back/forward lists, cookies, saved passwords, and which tab is
              open stay on each device and are not synced.
            </p>

            <h2>Searching and site icons</h2>
            <p>
              Avora uses Google as its search engine. When you search from the
              address bar, your search goes to Google, and on Mac, what you type
              in the address bar is sent to Google for search suggestions.
              Avora for iPhone and iPad shows each site&apos;s icon by asking
              Google&apos;s icon service for it, which tells Google the domain
              (not the full address) of the sites in your tabs. Google handles
              this under its own privacy policy.
            </p>

            <h2>Usage statistics (Mac)</h2>
            <p>
              Once a day while it&apos;s open, Avora for Mac sends one anonymous
              &ldquo;active&rdquo; event so we can tell how many copies are in
              use. It contains a random install ID; the versions of Avora,
              Chromium and macOS; your Mac&apos;s chip type and interface
              language; and rough counts of windows, tabs and Spaces. It never
              includes a page address, title, search, or anything you type. You
              can turn it off in Avora&apos;s settings, which also forgets the
              install ID. Avora for iPhone and iPad doesn&apos;t send usage
              statistics.
            </p>

            <h2>Updates (Mac)</h2>
            <p>
              Avora for Mac checks avorabrowser.com for new versions. Like any
              request to a website, the check reveals your IP address and the
              version you have.
            </p>

            <h2>Features that use your own accounts (Mac)</h2>
            <p>
              Some optional Mac features connect to services with an account or
              key you provide. Avora talks to those services directly from your
              Mac; nothing passes through us.
            </p>
            <ul>
              <li>
                <strong>Style Studio</strong>: if you add your own Anthropic API
                key, the parts of a page you ask about are sent to Anthropic to
                answer your question, under your Anthropic account. The key is
                stored encrypted on your Mac.
              </li>
              <li>
                <strong>Live Folders</strong>: if you connect GitHub, Avora
                fetches the items you choose from GitHub using your sign-in,
                which is stored encrypted on your Mac.
              </li>
            </ul>

            <h2>This website</h2>
            <p>
              avorabrowser.com counts page views and downloads with PostHog. It
              stores an anonymous identifier in your browser and doesn&apos;t
              build profiles of visitors.
            </p>

            <h2>Who processes data for us</h2>
            <ul>
              <li>Supabase: accounts, sync data and page previews.</li>
              <li>Resend: sign-in emails.</li>
              <li>PostHog: website analytics and Mac usage statistics.</li>
              <li>Vercel: hosting for avorabrowser.com and update checks.</li>
            </ul>
            <p>
              They process data only to provide these services to Avora.
            </p>

            <h2>Keeping and deleting your data</h2>
            <p>
              Synced data is kept while you have an account. When you close a
              tab, it&apos;s removed from sync; a record that it was deleted is
              kept for up to 30 days so your other devices catch up. Signing
              out stops a device from syncing and removes it from your account;
              its tabs stay on that device.
            </p>
            <p>
              To delete your account, open the tab switcher in Avora for iPhone
              or iPad, tap the person icon at the top, and choose Delete Account. This
              deletes your account, synced Spaces and tabs, devices and page
              previews straight away. If you can&apos;t use the app, email{" "}
              {mail} from the address you signed in with and we&apos;ll do it
              for you and confirm by email.
            </p>

            <h2>Children</h2>
            <p>
              Avora isn&apos;t directed at children under 13, and we don&apos;t
              knowingly collect personal information from them.
            </p>

            <h2>Changes and contact</h2>
            <p>
              If this policy changes, we&apos;ll update it here and change the
              date at the top. Questions or requests about your data: {mail}.
            </p>
          </div>
        </section>
        <Footer />
      </main>
    </div>
  );
}
