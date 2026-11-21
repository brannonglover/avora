import posthog from "posthog-js";
import { posthogKey } from "@/lib/analytics";
import { posthogUiHost } from "@/lib/posthog-hosts";

// Opening any page with ?internal=1 marks this browser as the developer's, so
// its events carry internal: true and PostHog's internal-traffic filter leaves
// them out of the dashboard. ?internal=0 unmarks it. For browsers off the home
// and office networks, which the filter already knows by address.
const INTERNAL_KEY = "avora_internal";

function isInternalBrowser(): boolean {
  try {
    const flag = new URLSearchParams(location.search).get("internal");
    if (flag === "1") localStorage.setItem(INTERNAL_KEY, "1");
    if (flag === "0") localStorage.removeItem(INTERNAL_KEY);
    return localStorage.getItem(INTERNAL_KEY) === "1";
  } catch {
    return false;
  }
}

if (posthogKey) {
  const internal = isInternalBrowser();
  posthog.init(posthogKey, {
    api_host: "/ingest",
    ui_host: posthogUiHost,
    // Page views (including client-side navigations), plus only people we
    // identify get profiles, which on a marketing site means nobody.
    defaults: "2026-08-30",
    person_profiles: "identified_only",
    before_send: (event) => {
      if (internal && event) event.properties.internal = true;
      return event;
    },
  });
}
