"use client";

import { useEffect, useState } from "react";
import Image from "next/image";
import {
  ArrowDownToLine,
  Brain,
  Briefcase,
  House,
  Images,
  Microscope,
  Palette,
  Sparkles,
  type LucideIcon,
} from "lucide-react";
import { downloadUrl } from "@/lib/release";

const SECTIONS: { id: string; label: string; icon: LucideIcon }[] = [
  { id: "overview", label: "Overview", icon: Sparkles },
  { id: "tour", label: "Screenshots", icon: Images },
  { id: "intelligence", label: "Avora Intelligence", icon: Brain },
  { id: "download", label: "Download", icon: ArrowDownToLine },
];

// The same Spaces and accent colours as the screenshots.
const SPACES: { name: string; color: string; icon: LucideIcon }[] = [
  { name: "Personal", color: "#4fd1a5", icon: House },
  { name: "Work", color: "#7c93ff", icon: Briefcase },
  { name: "Research", color: "#d98cff", icon: Microscope },
  { name: "Design", color: "#f78fd0", icon: Palette },
];

export function Sidebar() {
  const [active, setActive] = useState(SECTIONS[0].id);
  const [space, setSpace] = useState(SPACES[2].name);

  useEffect(() => {
    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (entry.isIntersecting) setActive(entry.target.id);
        }
      },
      // A section is current once it crosses the middle of the viewport.
      { rootMargin: "-50% 0px -50% 0px" },
    );
    for (const { id } of SECTIONS) {
      const section = document.getElementById(id);
      if (section) observer.observe(section);
    }
    return () => observer.disconnect();
  }, []);

  function chooseSpace(name: string, color: string) {
    setSpace(name);
    document.documentElement.style.setProperty("--accent", color);
  }

  return (
    <aside className="sidebar">
      <div className="traffic" aria-hidden="true">
        <span />
        <span />
        <span />
      </div>

      <a className="address" href="#overview">
        <Image src="/avora-mark.svg" alt="" width={20} height={20} />
        Avora
      </a>

      <nav aria-label="Sections">
        <ul className="tabs">
          {SECTIONS.map(({ id, label, icon: Icon }) => (
            <li key={id}>
              <a
                className="tab"
                href={`#${id}`}
                aria-current={active === id ? "true" : undefined}
              >
                <Icon size={16} aria-hidden="true" />
                {label}
              </a>
            </li>
          ))}
        </ul>
      </nav>

      <a className="sidebar-download" href={downloadUrl} download>
        <ArrowDownToLine size={16} aria-hidden="true" />
        Download
      </a>

      <div className="spaces" role="group" aria-label="Page colour, by Space">
        {SPACES.map(({ name, color, icon: Icon }) => (
          <button
            key={name}
            type="button"
            className="space"
            title={name}
            aria-label={name}
            aria-pressed={space === name}
            style={{ color }}
            onClick={() => chooseSpace(name, color)}
          >
            <Icon size={18} aria-hidden="true" />
          </button>
        ))}
      </div>
    </aside>
  );
}
