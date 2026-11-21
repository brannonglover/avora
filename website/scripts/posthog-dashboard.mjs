// Creates, or brings up to date, the "Avora Overview" dashboard in PostHog: the website,
// its Download buttons, how many downloads become copies in use, and how those
// copies are used.  Run it again after changing an insight or section below.
// Insights and headings are matched by name, so it updates them in place
// rather than adding copies, and it lays the tiles out in SECTIONS order.
//
//   npm run dashboard
//   npm run dashboard -- --check   # runs every query, changes nothing
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

const DOWNLOAD = "download_clicked";
// Sent by the app once a day (src/chrome/browser/avora/avora_telemetry.cc).
const ACTIVE = "app_active";
// Recorded by the website for each Sparkle feed request
// (lib/update-check-analytics.ts).
const UPDATE_CHECK = "avora_update_check";

// Website events are limited to the live site's host; the app's and the feed's
// events have no host and are Avora's alone by name.
const events = (event, math = "total", name = event) => ({
  kind: "EventsNode",
  event,
  name,
  custom_name: name === event ? undefined : name,
  math,
  ...(event === "$pageview" || event === DOWNLOAD
    ? {
        properties: [
          { key: "$host", value: [SITE_HOST], operator: "exact", type: "event" },
        ],
      }
    : {}),
});

const breakdown = (property) => ({
  breakdownFilter: { breakdown: property, breakdown_type: "event" },
});

const trends = (source) => ({
  kind: "InsightVizNode",
  source: { kind: "TrendsQuery", interval: "day", ...source },
});

const WEBSITE_INSIGHTS = [
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

const APP_INSIGHTS = [
  {
    name: "Copies used per day",
    description: "Different copies of Avora opened each day.",
    query: trends({
      series: [events(ACTIVE, "dau", "Daily active copies")],
      dateRange: { date_from: "-90d" },
      trendsFilter: { display: "ActionsLineGraph" },
    }),
  },
  {
    name: "Weekly and monthly active copies",
    description: "Copies opened at least once in the last 7 and 30 days.",
    query: trends({
      series: [
        events(ACTIVE, "weekly_active", "Weekly active"),
        events(ACTIVE, "monthly_active", "Monthly active"),
      ],
      dateRange: { date_from: "-90d" },
      trendsFilter: { display: "ActionsLineGraph" },
    }),
  },
  {
    name: "Weekly retention",
    description:
      "Of the copies first opened in a week, how many were opened again in " +
      "each week after.",
    query: {
      kind: "InsightVizNode",
      source: {
        kind: "RetentionQuery",
        dateRange: { date_from: "-8w" },
        retentionFilter: {
          period: "Week",
          totalIntervals: 8,
          retentionType: "retention_first_time",
          targetEntity: { id: ACTIVE, name: ACTIVE, type: "events" },
          returningEntity: { id: ACTIVE, name: ACTIVE, type: "events" },
        },
      },
    },
  },
  {
    name: "Active copies by version",
    description: "Which Avora version each day's copies were running.",
    query: trends({
      series: [events(ACTIVE, "dau")],
      dateRange: { date_from: "-30d" },
      ...breakdown("app_version"),
      trendsFilter: { display: "ActionsLineGraph" },
    }),
  },
  {
    name: "macOS versions",
    query: trends({
      series: [events(ACTIVE, "dau")],
      dateRange: { date_from: "-30d" },
      ...breakdown("os_version"),
      trendsFilter: { display: "ActionsBarValue" },
    }),
  },
  {
    name: "Tabs open",
    description: "Open tabs across a copy's windows when it reported in.",
    query: trends({
      series: [events(ACTIVE, "dau")],
      dateRange: { date_from: "-30d" },
      ...breakdown("tabs"),
      trendsFilter: { display: "ActionsBarValue" },
    }),
  },
  {
    name: "Spaces per copy",
    query: trends({
      series: [events(ACTIVE, "dau")],
      dateRange: { date_from: "-30d" },
      ...breakdown("spaces"),
      trendsFilter: { display: "ActionsBarValue" },
    }),
  },
  {
    name: "Update checks: copies per day",
    description:
      "Copies that asked the update feed each day, counted by network and " +
      "version, so a floor. Covers 1.4.0 and earlier, which don't send app_active.",
    query: trends({
      series: [events(UPDATE_CHECK, "dau", "Copies checking")],
      dateRange: { date_from: "-90d" },
      trendsFilter: { display: "ActionsLineGraph" },
    }),
  },
  {
    name: "Update checks by version",
    query: trends({
      series: [events(UPDATE_CHECK, "dau")],
      dateRange: { date_from: "-30d" },
      ...breakdown("app_version"),
      trendsFilter: { display: "ActionsBarValue" },
    }),
  },
];

// Totals only: everyone downloads the same DMG, so a copy can't be traced back
// to the click that fetched it.  Until a release sends app_active, every copy
// already installed reports its "first" open that week, so the rate spikes then
// and is meaningful from the week after.
const FIRST_OPEN_SERIES = [
  events(DOWNLOAD, "total", "Download clicks"),
  events(ACTIVE, "first_time_for_user", "Copies opened for the first time"),
];

const FIRST_OPEN_INSIGHTS = [
  {
    name: "Downloads and first opens per week",
    description:
      "Download clicks on the website, and copies of Avora opened for the " +
      "first time, each week.",
    query: trends({
      series: FIRST_OPEN_SERIES,
      interval: "week",
      dateRange: { date_from: "-12w" },
      trendsFilter: { display: "ActionsBar" },
    }),
  },
  {
    name: "Download to first open, %",
    description:
      "Copies opened for the first time per 100 download clicks, each week.",
    query: trends({
      series: FIRST_OPEN_SERIES,
      interval: "week",
      dateRange: { date_from: "-12w" },
      trendsFilter: { display: "ActionsLineGraph", formula: "B / A * 100" },
    }),
  },
];

// Found by ID, so renaming it in PostHog is fine; the name is only used if it
// ever has to be created again.
const DASHBOARD = {
  id: 2364175,
  name: "Avora Overview",
  description: "The website, downloads and how many copies of Avora are in use.",
};

// The dashboard, top to bottom.  Each heading is a text tile across the full
// width; its insights follow two to a row.
const SECTIONS = [
  {
    heading: "## Website",
    note: "Visits to www.avorabrowser.com.",
    insights: ["Visitors per day", "Visitor to download"],
  },
  {
    heading: "## Downloads",
    note:
      "Clicks on the Download buttons. A click is intent: the DMG comes from " +
      "GitHub Releases, which keeps its own count.",
    insights: [
      "Download clicks, last 30 days",
      "Download clicks, all time",
      "Download clicks per day",
      "Download clicks by button",
      "Download clicks by version",
    ],
  },
  {
    heading: "## From download to use",
    note:
      "Totals, not people: a copy can't be traced to the click that fetched " +
      "it. The first week app_active ships counts every existing copy as new.",
    insights: ["Downloads and first opens per week", "Download to first open, %"],
  },
  {
    heading: "## App usage",
    note:
      "From the app's daily app_active event (packaged copies that haven't " +
      "turned off Usage statistics) and the update feed's checks.",
    insights: [
      "Copies used per day",
      "Weekly and monthly active copies",
      "Weekly retention",
      "Active copies by version",
      "macOS versions",
      "Tabs open",
      "Spaces per copy",
      "Update checks: copies per day",
      "Update checks by version",
    ],
  },
];

const INSIGHTS = [...WEBSITE_INSIGHTS, ...FIRST_OPEN_INSIGHTS, ...APP_INSIGHTS];

// Every chart leaves out the project's internal and test traffic, the filter
// under Project settings > "Filter out internal and test users": the
// developer's networks, their Avora install, browsers marked with ?internal=1,
// update checks from their networks, and Claude's browser.
for (const insight of INSIGHTS) insight.query.source.filterTestAccounts = true;
for (const section of SECTIONS) {
  section.insights = section.insights.map((name) => {
    const insight = INSIGHTS.find((i) => i.name === name);
    if (!insight) throw new Error(`No insight named "${name}"`);
    return insight;
  });
}

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

const textBody = ({ heading, note }) => `${heading}\n\n${note}`;

// Tiles with a heading, by the heading's first line.
const headingOf = (tile) => tile.text?.body?.split("\n")[0];

async function syncDashboard() {
  const { id, name, description } = DASHBOARD;
  let dashboard = await api("GET", `/dashboards/${id}/`).catch(() => null);
  if (dashboard && !dashboard.deleted) {
    await api("PATCH", `/dashboards/${dashboard.id}/`, { description });
    console.log(`Updating "${dashboard.name}" (${dashboard.id})`);
  } else {
    dashboard = await api("POST", "/dashboards/", { name, description });
    console.log(
      `Created "${name}" (${dashboard.id}). Put its ID in DASHBOARD.id.`,
    );
  }

  let detail = await api("GET", `/dashboards/${dashboard.id}/`);
  const existing = new Map(
    detail.tiles
      .map((tile) => tile.insight)
      .filter((insight) => insight && !insight.deleted)
      .map((insight) => [insight.name, insight]),
  );

  for (const insight of INSIGHTS) {
    const body = { description: "", ...insight };
    const found = existing.get(insight.name);
    if (found) {
      await api("PATCH", `/insights/${found.id}/`, body);
      console.log(`  updated  ${insight.name}`);
    } else {
      await api("POST", "/insights/", { ...body, dashboards: [dashboard.id] });
      console.log(`  added    ${insight.name}`);
    }
  }

  // Headings: update the text of ones that exist, add the rest, and drop text
  // tiles that no longer head a section.
  detail = await api("GET", `/dashboards/${dashboard.id}/`);
  const headings = new Set(SECTIONS.map((section) => section.heading));
  const textTiles = detail.tiles.filter((tile) => tile.text && !tile.deleted);
  const tileChanges = [];
  for (const section of SECTIONS) {
    const tile = textTiles.find((t) => headingOf(t) === section.heading);
    tileChanges.push(
      tile
        ? { id: tile.id, text: { ...tile.text, body: textBody(section) } }
        : { text: { body: textBody(section) } },
    );
  }
  for (const tile of textTiles) {
    if (!headings.has(headingOf(tile))) tileChanges.push({ id: tile.id, deleted: true });
  }
  detail = await api("PATCH", `/dashboards/${dashboard.id}/`, { tiles: tileChanges });

  // Layout, top to bottom in SECTIONS order, on PostHog's 12-column grid.
  const live = detail.tiles.filter((tile) => !tile.deleted);
  const layout = [];
  let y = 0;
  for (const section of SECTIONS) {
    const heading = live.find((t) => headingOf(t) === section.heading);
    layout.push({ id: heading.id, layouts: { sm: { x: 0, y, w: 12, h: 2 } } });
    y += 2;
    section.insights.forEach((insight, index) => {
      const tile = live.find((t) => t.insight?.name === insight.name);
      if (!tile) return;
      layout.push({
        id: tile.id,
        layouts: { sm: { x: (index % 2) * 6, y: y + Math.floor(index / 2) * 5, w: 6, h: 5 } },
      });
    });
    y += Math.ceil(section.insights.length / 2) * 5;
  }
  await api("PATCH", `/dashboards/${dashboard.id}/`, { tiles: layout });
  console.log(`  laid out ${layout.length} tiles in ${SECTIONS.length} sections`);

  console.log(
    `${posthogUiHost}/project/${detail.team_id ?? project}/dashboard/${dashboard.id}`,
  );
}

async function checkQueries() {
  let failed = 0;
  for (const { name, query } of INSIGHTS) {
    try {
      const result = await api("POST", "/query/", { query: query.source });
      if (result.error) throw new Error(result.error);
      console.log(`  ok    ${name}`);
    } catch (error) {
      failed += 1;
      console.log(`  FAIL  ${name}: ${error.message}`);
    }
  }
  process.exit(failed ? 1 : 0);
}

if (process.argv.includes("--check")) {
  await checkQueries();
}
await syncDashboard();
