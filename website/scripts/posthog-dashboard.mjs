// Creates, or brings up to date, the "Avora downloads" dashboard in PostHog.
// Run it again after changing an insight below: insights are matched by name,
// so it updates them in place rather than adding copies.
//
//   npm run dashboard
//
// Needs a personal API key (not the phc_ project key the site uses) with the
// dashboard:write, insight:write and project:read scopes, made at
// https://us.posthog.com/settings/user-api-keys, kept in ~/.config/posthog/personal-key
// or in .env.local:
//
//   POSTHOG_PERSONAL_API_KEY=phx_...
//   POSTHOG_PROJECT_ID=12345        # optional; see below
//   POSTHOG_HOST=https://eu.posthog.com   # only for an EU project, as in lib/posthog-hosts.ts

import { existsSync, readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const envFile = fileURLToPath(new URL("../.env.local", import.meta.url));
if (existsSync(envFile)) process.loadEnvFile(envFile);

// The same key file other projects' PostHog scripts on this Mac read.
const keyFile = join(homedir(), ".config", "posthog", "personal-key");
const key =
  process.env.POSTHOG_PERSONAL_API_KEY?.trim() ||
  (existsSync(keyFile) ? readFileSync(keyFile, "utf8").trim() : "");
if (!key) {
  console.error("POSTHOG_PERSONAL_API_KEY isn't set. See the top of this file.");
  process.exit(1);
}
const posthogUiHost = process.env.POSTHOG_HOST || "https://us.posthog.com";
// Avora shares Glover Labs' one PostHog project with Cavaro, since the plan
// allows only one. Every series below is limited to the live site's host, so
// Cavaro's events, and visits to localhost, never count.
const project = process.env.POSTHOG_PROJECT_ID || "350420";
const SITE_HOST = "www.avorabrowser.com";
const base = `${posthogUiHost}/api/projects/${project}`;

const DASHBOARD = "Avora downloads";
const DOWNLOAD = "download_clicked";

const events = (event, math = "total", name = event) => ({
  kind: "EventsNode",
  event,
  name,
  custom_name: name === event ? undefined : name,
  math,
  properties: [
    { key: "$host", value: [SITE_HOST], operator: "exact", type: "event" },
  ],
});

const trends = (source) => ({
  kind: "InsightVizNode",
  source: { kind: "TrendsQuery", interval: "day", ...source },
});

const INSIGHTS = [
  {
    name: "Download clicks, last 30 days",
    description: "Clicks on any Download button.",
    query: trends({
      series: [events(DOWNLOAD)],
      dateRange: { date_from: "-30d" },
      trendsFilter: { display: "BoldNumber" },
    }),
  },
  {
    name: "Download clicks, all time",
    description: "Every click on a Download button since tracking started.",
    query: trends({
      series: [events(DOWNLOAD)],
      dateRange: { date_from: "all" },
      trendsFilter: { display: "BoldNumber" },
    }),
  },
  {
    name: "Download clicks per day",
    description: "Clicks, and the number of different visitors who clicked.",
    query: trends({
      series: [
        events(DOWNLOAD, "total", "Clicks"),
        events(DOWNLOAD, "dau", "Visitors who clicked"),
      ],
      dateRange: { date_from: "-90d" },
      trendsFilter: { display: "ActionsLineGraph" },
    }),
  },
  {
    name: "Download clicks by button",
    description: "hero, download-section or sidebar.",
    query: trends({
      series: [events(DOWNLOAD)],
      dateRange: { date_from: "-30d" },
      breakdownFilter: { breakdown: "placement", breakdown_type: "event" },
      trendsFilter: { display: "ActionsBarValue" },
    }),
  },
  {
    name: "Download clicks by version",
    description: "Which release the button offered.",
    query: trends({
      series: [events(DOWNLOAD)],
      dateRange: { date_from: "-90d" },
      breakdownFilter: { breakdown: "version", breakdown_type: "event" },
      trendsFilter: { display: "ActionsBarValue" },
    }),
  },
  {
    name: "Visitors per day",
    description: "Different visitors who loaded the site.",
    query: trends({
      series: [events("$pageview", "dau", "Visitors")],
      dateRange: { date_from: "-90d" },
      trendsFilter: { display: "ActionsLineGraph" },
    }),
  },
  {
    name: "Visitor to download",
    description: "Of the visitors in the last 30 days, how many clicked Download.",
    query: {
      kind: "InsightVizNode",
      source: {
        kind: "FunnelsQuery",
        series: [
          events("$pageview", "total", "Visited the site"),
          events(DOWNLOAD, "total", "Clicked Download"),
        ],
        dateRange: { date_from: "-30d" },
        funnelsFilter: {
          funnelWindowInterval: 14,
          funnelWindowIntervalUnit: "day",
          funnelVizType: "steps",
        },
      },
    },
  },
];

async function api(method, path, body) {
  const response = await fetch(`${base}${path}`, {
    method,
    headers: {
      Authorization: `Bearer ${key}`,
      "Content-Type": "application/json",
    },
    body: body && JSON.stringify(body),
  });
  const text = await response.text();
  if (!response.ok) {
    throw new Error(`${method} ${path}: ${response.status} ${text.slice(0, 1000)}`);
  }
  return text ? JSON.parse(text) : null;
}

async function findDashboard() {
  const page = await api(
    "GET",
    `/dashboards/?search=${encodeURIComponent(DASHBOARD)}&limit=100`,
  );
  return page.results.find((d) => d.name === DASHBOARD && !d.deleted);
}

let dashboard = await findDashboard();
if (dashboard) {
  console.log(`Updating "${DASHBOARD}" (${dashboard.id})`);
} else {
  dashboard = await api("POST", "/dashboards/", {
    name: DASHBOARD,
    description:
      "Clicks on the website's Download buttons. A click is intent: the DMG " +
      "itself comes from GitHub Releases, which keeps its own download count.",
  });
  console.log(`Created "${DASHBOARD}" (${dashboard.id})`);
}

const detail = await api("GET", `/dashboards/${dashboard.id}/`);
const existing = new Map(
  (detail.tiles ?? [])
    .map((tile) => tile.insight)
    .filter((insight) => insight && !insight.deleted)
    .map((insight) => [insight.name, insight]),
);

for (const insight of INSIGHTS) {
  const found = existing.get(insight.name);
  if (found) {
    await api("PATCH", `/insights/${found.id}/`, insight);
    console.log(`  updated  ${insight.name}`);
  } else {
    await api("POST", "/insights/", { ...insight, dashboards: [dashboard.id] });
    console.log(`  added    ${insight.name}`);
  }
}

console.log(`${posthogUiHost}/project/${detail.team_id ?? project}/dashboard/${dashboard.id}`);
