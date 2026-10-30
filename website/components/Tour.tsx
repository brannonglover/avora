"use client";

import { useRef, useState, type KeyboardEvent } from "react";
import Image from "next/image";
import {
  AppWindow,
  FolderGit2,
  Import,
  Layers,
  PanelTop,
  PictureInPicture2,
  RefreshCw,
  type LucideIcon,
} from "lucide-react";

const STOPS: {
  id: string;
  label: string;
  icon: LucideIcon;
  title: string;
  body: string;
  src: string;
  alt: string;
}[] = [
  {
    id: "spaces",
    label: "Spaces",
    icon: Layers,
    title: "Work and life, signed in separately.",
    body: "Every Space has its own tabs, Favorites and colour, and its own cookies and logins. Stay signed in to two accounts on one site, one Space each, and switch with a click.",
    src: "/shots/spaces.png",
    alt: "Avora's Work Space, with its own Favorites, pinned pages and today's tabs in a blue accent.",
  },
  {
    id: "tabs",
    label: "Pinned and Today",
    icon: PanelTop,
    title: "Keep what matters. Work on the rest.",
    body: "Favorites and pinned pages stay put. Today holds what you're doing now, and it survives a quit, a crash or a closed window.",
    src: "/shots/personal.png",
    alt: "Avora's Personal Space, with Favorites at the top of the sidebar, a pinned page below them and today's tabs under the Space name.",
  },
  {
    id: "preview",
    label: "Link previews",
    icon: PictureInPicture2,
    title: "Look at a link without leaving your page.",
    body: "Links that would open a new tab open in a card over the page instead. Close it to carry on, or expand it into a real tab with your scroll position and form state intact.",
    src: "/shots/preview.png",
    alt: "A Wikipedia article open in a preview card over another page, with close and expand buttons at its corner.",
  },
  {
    id: "mini",
    label: "Mini windows",
    icon: AppWindow,
    title: "A link from Mail is a glance, not a commitment.",
    body: "Links from other apps land in a small window of their own instead of joining the tabs you're working in. Keep one by choosing a Space from its toolbar.",
    src: "/shots/mini.png",
    alt: "A mini window with its own address bar and a Research Space selector, floating over the main Avora window.",
  },
];

const ALSO: { icon: LucideIcon; title: string; body: string }[] = [
  {
    icon: FolderGit2,
    title: "Live Folders",
    body: "A sidebar folder that fills itself with your GitHub pull requests, using the account signed in to that Space. Each one opens as its own tab.",
  },
  {
    icon: Import,
    title: "Bring everything",
    body: "Import Arc's Spaces and pinned tabs, plus bookmarks from Chrome, Safari, Firefox and other Chromium browsers.",
  },
  {
    icon: RefreshCw,
    title: "Sync",
    body: "Sign in to an Avora account to share your Spaces, Favorites, pinned items and today's tabs across devices.",
  },
];

export function Tour() {
  const [activeId, setActiveId] = useState(STOPS[0].id);
  const tabs = useRef<Record<string, HTMLButtonElement | null>>({});

  function onKeyDown(event: KeyboardEvent, index: number) {
    const last = STOPS.length - 1;
    const next = {
      ArrowDown: index === last ? 0 : index + 1,
      ArrowRight: index === last ? 0 : index + 1,
      ArrowUp: index === 0 ? last : index - 1,
      ArrowLeft: index === 0 ? last : index - 1,
      Home: 0,
      End: last,
    }[event.key];
    if (next === undefined) return;
    event.preventDefault();
    setActiveId(STOPS[next].id);
    tabs.current[STOPS[next].id]?.focus();
  }

  return (
    <section id="tour" className="section">
      <header className="section-head">
        <p className="eyebrow">Screenshots</p>
        <h2>Pick a tab. This is the real thing.</h2>
        <p className="lede">
          Every image here is a capture of Avora itself, not a mockup.
        </p>
      </header>

      <div className="tour">
        <div
          className="tour-tabs"
          role="tablist"
          aria-label="Avora features"
          aria-orientation="vertical"
        >
          {STOPS.map(({ id, label, icon: Icon }, index) => (
            <button
              key={id}
              ref={(el) => {
                tabs.current[id] = el;
              }}
              id={`tour-tab-${id}`}
              className="tab"
              type="button"
              role="tab"
              aria-selected={activeId === id}
              aria-controls={`tour-panel-${id}`}
              tabIndex={activeId === id ? 0 : -1}
              onClick={() => setActiveId(id)}
              onKeyDown={(event) => onKeyDown(event, index)}
            >
              <Icon size={16} aria-hidden="true" />
              {label}
            </button>
          ))}
        </div>

        {STOPS.map(({ id, title, body, src, alt }) => (
          <div
            key={id}
            id={`tour-panel-${id}`}
            className="tour-panel"
            role="tabpanel"
            aria-labelledby={`tour-tab-${id}`}
            hidden={activeId !== id}
          >
            <h3>{title}</h3>
            <p>{body}</p>
            <Image
              className="shot"
              src={src}
              alt={alt}
              width={1600}
              height={1000}
              sizes="(max-width: 900px) 100vw, 860px"
            />
          </div>
        ))}
      </div>

      <ul className="also">
        {ALSO.map(({ icon: Icon, title, body }) => (
          <li key={title}>
            <Icon size={18} aria-hidden="true" />
            <h3>{title}</h3>
            <p>{body}</p>
          </li>
        ))}
      </ul>
    </section>
  );
}
