"use client";

import { useEffect, useRef, useState, useSyncExternalStore } from "react";
import Image from "next/image";
import { Pause, Play } from "lucide-react";

const REDUCED_MOTION = "(prefers-reduced-motion: reduce)";

function subscribe(onChange: () => void) {
  const query = window.matchMedia(REDUCED_MOTION);
  query.addEventListener("change", onChange);
  return () => query.removeEventListener("change", onChange);
}

function useReducedMotion() {
  return useSyncExternalStore(
    subscribe,
    () => window.matchMedia(REDUCED_MOTION).matches,
    () => false,
  );
}

// A looping recording of one feature, played while its tab is showing. People
// who ask for reduced motion get the still screenshot instead, and anyone can
// pause the clip.
export function TourClip({
  active,
  video,
  poster,
  still,
  label,
  alt,
}: {
  active: boolean;
  video: string;
  poster: string;
  still: string;
  label: string;
  alt: string;
}) {
  const reduced = useReducedMotion();
  const ref = useRef<HTMLVideoElement>(null);
  const [paused, setPaused] = useState(false);
  const [onScreen, setOnScreen] = useState(false);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const observer = new IntersectionObserver(
      ([entry]) => setOnScreen(entry.isIntersecting),
      { threshold: 0.25 },
    );
    observer.observe(el);
    return () => observer.disconnect();
  }, [reduced]);

  // Each time a tab is chosen its clip starts from the beginning.
  useEffect(() => {
    const el = ref.current;
    if (!el || active) return;
    el.pause();
    el.currentTime = 0;
  }, [active]);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    if (active && onScreen && !paused) {
      el.play().catch(() => {});
    } else {
      el.pause();
    }
  }, [active, onScreen, paused, reduced]);

  if (reduced) {
    return (
      <Image
        className="shot"
        src={still}
        alt={alt}
        width={1600}
        height={1000}
        sizes="(max-width: 900px) 100vw, 860px"
      />
    );
  }

  return (
    <div className="clip">
      <video
        ref={ref}
        className="shot"
        width={1600}
        height={1000}
        poster={poster}
        preload={active ? "auto" : "none"}
        muted
        loop
        playsInline
        aria-label={label}
      >
        <source src={video} type="video/mp4" />
      </video>
      <button
        type="button"
        className="clip-toggle"
        aria-label={paused ? "Play recording" : "Pause recording"}
        onClick={() => setPaused((value) => !value)}
      >
        {paused ? (
          <Play size={14} aria-hidden="true" />
        ) : (
          <Pause size={14} aria-hidden="true" />
        )}
      </button>
    </div>
  );
}
