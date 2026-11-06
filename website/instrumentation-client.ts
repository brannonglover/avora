import posthog from "posthog-js";
import { posthogKey } from "@/lib/analytics";
import { posthogUiHost } from "@/lib/posthog-hosts";

if (posthogKey) {
  posthog.init(posthogKey, {
    api_host: "/ingest",
    ui_host: posthogUiHost,
    // Page views (including client-side navigations), plus only people we
    // identify get profiles, which on a marketing site means nobody.
    defaults: "2026-08-30",
    person_profiles: "identified_only",
  });
}
