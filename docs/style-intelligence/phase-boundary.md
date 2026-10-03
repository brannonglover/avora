# Style Intelligence phase boundary

Phase 1 is the explanation layer: inspect a selected element, read measured layout, and say why that box looks the way it does when the evidence is deterministic.

Phase 1 is complete. The documents next to this file are the foundation for later work:

- `architecture.md` — ownership, data flow, models, presentation, and invalidation
- `correctness.md` — declines that later work must not weaken
- `limitations.md` — current limits that are not those declines
- `fixtures/phase1.html` — the live page
- `panels/style_intelligence/*.test.ts` — the regression suite

## Outside Phase 1

The following are not part of Phase 1 and are not implied by the current pane:

- stacking-context explorer
- visual constraint connectors
- responsive intelligence
- page-wide token discovery
- AI
- Goal Mode
- preview / apply
- impact analysis

Also outside Phase 1: Blink changes, DevTools protocol changes, `DOMSnapshot` on the selection path, and any broadening of the attributions listed in `correctness.md` without stronger deterministic evidence.

A later phase starts by keeping this contract green. New analysis is additional structured facts, run after the current summary when it is expensive, and worded by the presentation layer. It does not replace a Phase 1 decline with a guess.
