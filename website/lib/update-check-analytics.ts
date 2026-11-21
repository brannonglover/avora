import { createHash, randomUUID } from "node:crypto";
import { posthogIngestHost } from "./posthog-hosts";

// Every running copy of Avora fetches /appcast.xml on Sparkle's schedule (every
// six hours while it's open), naming itself in the User-Agent. Each check is
// recorded as one PostHog event, so the dashboard can show roughly how many
// copies are in use and on which version.
//
// Sparkle names the app by CFBundleShortVersionString, which in Avora is
// Chromium's version, so 1.4.0 and earlier say "Avora/155.0.8038.0 Sparkle/…".
// Later versions set the User-Agent to "Avora/1.5.0 Sparkle/…" themselves.
//
// Sparkle sends no install ID. The event's ID is a hash of the address, the
// User-Agent and the day, with a secret salt, so the same Mac counts once a day
// without its address being stored or recoverable. Several Macs behind one
// network on the same version count as one, so the number is a floor.

const SPARKLE_UA = /^([^/\s]+)\/(\S+).*\bSparkle\/(\S+)/;

// Chromium's versions have four parts; Avora's have three.
const CHROMIUM_VERSION = /^\d+\.\d+\.\d+\.\d+$/;

// The developer's own networks (comma-separated, in Vercel's environment).
// A check from one is marked internal: true for PostHog's internal-traffic
// filter. The address is only compared, never recorded.
function internalAddresses(): string[] {
  return (process.env.ANALYTICS_INTERNAL_IPS ?? "")
    .split(",")
    .map((address) => address.trim())
    .filter(Boolean);
}

export type UpdateCheck = {
  // Avora's version, or "1.4.0 or earlier" when the app only gave Chromium's.
  appVersion: string;
  chromiumVersion: string | null;
  sparkleVersion: string;
  distinctId: string;
  internal: boolean;
};

export function updateCheckFrom(headers: Headers): UpdateCheck | null {
  const userAgent = headers.get("user-agent") ?? "";
  const match = SPARKLE_UA.exec(userAgent);
  if (!match || match[1] !== "Avora") return null;

  const salt = process.env.ANALYTICS_SALT;
  const address = headers.get("x-forwarded-for")?.split(",")[0]?.trim() ?? "";
  const day = new Date().toISOString().slice(0, 10);
  const distinctId = salt
    ? createHash("sha256")
        .update(`${salt}\n${day}\n${address}\n${userAgent}`)
        .digest("hex")
        .slice(0, 32)
    : randomUUID();

  const reportedChromium = CHROMIUM_VERSION.test(match[2]);
  return {
    appVersion: reportedChromium ? "1.4.0 or earlier" : match[2],
    chromiumVersion: reportedChromium ? match[2] : null,
    sparkleVersion: match[3],
    distinctId,
    internal: address !== "" && internalAddresses().includes(address),
  };
}

export async function recordUpdateCheck(check: UpdateCheck) {
  const apiKey = process.env.NEXT_PUBLIC_POSTHOG_KEY;
  if (!apiKey) return;
  try {
    await fetch(`${posthogIngestHost}/i/v0/e/`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        api_key: apiKey,
        event: "avora_update_check",
        distinct_id: check.distinctId,
        properties: {
          app_version: check.appVersion,
          chromium_version: check.chromiumVersion,
          sparkle_version: check.sparkleVersion,
          ...(check.internal ? { internal: true } : {}),
          $process_person_profile: false,
          $lib: "avora-website",
        },
      }),
    });
  } catch {
    // A lost count never matters more than the feed itself.
  }
}
