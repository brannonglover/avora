# Style Intelligence phase boundary

Phase 1 is the explanation layer: inspect a selected element, read measured layout, and say why that box looks the way it does when the evidence is deterministic.

Phase 1 is complete at the foundation commit for this work. Phase 2A adds Layering for the selected element: the CSS reason list, the ancestor context chain, a lazy document snapshot, and a sibling-order boundary. Phase 2B adds node-scoped Blink layout relationships: absolute and fixed containing blocks, the non-axis scroll container, and sticky physical-axis scroll containers. Clipping, connectors, and another phase are not part of that work.

The documents next to this file are the foundation for later work:

- `architecture.md` — ownership, data flow, models, presentation, and invalidation
- `correctness.md` — declines that later work must not weaken
- `limitations.md` — current limits that are not those declines
- `fixtures/phase1.html` — the Phase 1 live page
- `fixtures/phase2-investigation.html` — the Layering live page
- `fixtures/phase2a-boundary-order.html` — sibling paint-order measurements
- `fixtures/phase2b-relationships.html` — containing-block, scroll-container, and sticky-axis cases
- `chromium-layout-relationships.md` — the Chromium patch to reapply on upgrades
- `panels/style_intelligence/*.test.ts` — the regression suite

## Outside Phase 1

The following are not part of Phase 1 and are not implied by the current pane:

- a page-wide stacking explorer
- visual constraint connectors, containing-block inference from CSS, clipping parents, and clip chains
- responsive intelligence
- page-wide token discovery
- AI
- Goal Mode
- preview / apply
- impact analysis

Also outside Phase 1: Blink changes, DevTools protocol changes, `DOMSnapshot` on the selection path, and any broadening of the attributions listed in `correctness.md` without stronger deterministic evidence.

A later phase starts by keeping this contract green. New analysis is additional structured facts, run after the current summary when it is expensive, and worded by the presentation layer. It does not replace a Phase 1 decline with a guess.
