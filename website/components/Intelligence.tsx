import Image from "next/image";
import {
  KeyRound,
  Lock,
  MousePointerClick,
  ScanLine,
  Sparkles,
  SquarePen,
} from "lucide-react";

const STEPS = [
  {
    icon: MousePointerClick,
    label: "Select",
    title: "Pick any element",
    body: "Open DevTools, go to Elements, and choose the Intelligence tab beside Styles and Computed. It follows whatever you select.",
  },
  {
    icon: ScanLine,
    label: "Measure",
    title: "Chromium reports the facts",
    body: "Avora reads the computed style, the matched CSS and the box model for that one element, along with its layout and stacking relationships, straight from the engine that drew it.",
  },
  {
    icon: Sparkles,
    label: "Explain",
    title: "You get the reason",
    body: "Why it's this wide, what decides where it sits, where a gap comes from, and which stacking context holds it, each traced back to the declaration responsible.",
  },
  {
    icon: SquarePen,
    label: "Try",
    title: "Change it in Studio",
    body: "Target the element or a selector, write CSS and preview it live. Studio writes to a stylesheet of its own, never the page's, and Revert puts everything back.",
  },
];

export function Intelligence() {
  return (
    <section id="intelligence" className="section">
      <header className="section-head">
        <p className="eyebrow">Avora Intelligence</p>
        <h2>Ask why a page looks the way it does.</h2>
        <p className="lede">
          Intelligence is built into DevTools for anyone who works on the web.
          It isn&apos;t a chatbot guessing at your CSS. It reads what the
          browser actually did, then explains it.
        </p>
      </header>

      <ol className="steps">
        {STEPS.map(({ icon: Icon, label, title, body }) => (
          <li key={label} className="step">
            <span className="step-label">
              <Icon size={16} aria-hidden="true" />
              {label}
            </span>
            <h3>{title}</h3>
            <p>{body}</p>
          </li>
        ))}
      </ol>

      <Image
        className="shot"
        src="/shots/intelligence-explain.png"
        alt="Avora's DevTools with the Intelligence pane explaining the size and position of the selected element on the Avora website."
        width={1600}
        height={1000}
        sizes="(max-width: 900px) 100vw, 1100px"
      />

      <div className="principle">
        <blockquote>
          Measurements are the source of truth. An incomplete explanation beats
          a confident wrong one.
        </blockquote>
        <p>
          Avora keeps what Chromium measured apart from what it can only
          infer, and an inference never invents a value. When it can&apos;t
          prove what controls something, it shows you the measured number and
          leaves the cause unnamed.
        </p>
      </div>

      <div className="studio">
        <h3>From understanding to changing it</h3>
        <p>
          Switch from Explain to Studio in the same pane. Your draft stays
          apart from what&apos;s applied, invalid CSS is refused, and a preview
          left behind by a crash gets cleaned up. Here, Studio has just given
          this page&apos;s own headline a gradient.
        </p>
        <Image
          className="shot"
          src="/shots/intelligence-studio.png"
          alt="The Studio tab of the Intelligence pane with CSS declarations applied as a preview to the Avora website's headline, which now shows a gradient. Studio reports that every declaration takes effect."
          width={1600}
          height={1000}
          sizes="(max-width: 900px) 100vw, 1100px"
        />
      </div>

      <div className="generate">
        <div>
          <span className="badge">In development</span>
          <h3>Generate</h3>
          <p>
            Soon you&apos;ll be able to describe a change and let Claude draft
            the CSS. It&apos;s designed so you stay in charge at every step.
          </p>
        </div>
        <ul>
          <li>
            <KeyRound size={16} aria-hidden="true" />
            <span>
              You bring your own Anthropic API key. It&apos;s encrypted with a
              key held in your Mac&apos;s Keychain, and it&apos;s never shown
              again or synced. You can already add one in Avora Settings.
            </span>
          </li>
          <li>
            <Sparkles size={16} aria-hidden="true" />
            <span>
              The model sees a small, size-capped description of one element
              and returns CSS declarations only, checked by the same parser
              as your own drafts.
            </span>
          </li>
          <li>
            <SquarePen size={16} aria-hidden="true" />
            <span>
              The result lands in your draft. Nothing reaches the page until
              you apply it.
            </span>
          </li>
          <li>
            <Lock size={16} aria-hidden="true" />
            <span>
              Requests are only made when you press Generate. Explain and
              Studio run entirely on your Mac.
            </span>
          </li>
        </ul>
      </div>
    </section>
  );
}
