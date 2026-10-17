# Style Studio — Checkpoint 2A: reversible CSS preview engine

Date: 2026-10-17. Base: `38f794a` (Checkpoint 2A.2).

**Status:** the engine is finalized and committed on its own. It has no UI, and nothing calls it in normal use. It writes to a page only when it is driven programmatically, as the verifier does.

| Area | Status | Where |
| --- | --- | --- |
| Inspector-stylesheet text corruption (one stored text per document) | **Resolved** in Blink, Checkpoint 2A.1 (`7aae6df`) | §8, `chromium-inspector-stylesheet-isolation.md` |
| Which inspector sheet is the Styles pane's default | **Resolved**, Blink plus frontend, Checkpoint 2A.2 (`38f794a`) | §8, same doc |
| Engine: ownership, targeting, replace and revert, async cancellation, lifecycle | **Verified**: unit tests, live in Avora | §1–§5, §7 |
| A preview left behind by a crashed or killed DevTools | **Remaining limitation**: bounded by the document's lifetime, with no automatic cleanup | §6 |
| Preview UI, AI generation, refinement, export, orphan ledger | **Deferred to 2B** or later | §9 |

---

## 1. Chromium APIs used (verified in this checkout)

| API | Use | Verified behaviour |
| --- | --- | --- |
| `CSS.createStyleSheet(frameId, force: true)` via `CSSModel.createInspectorStylesheet` | Studio's own sheet, one per (target, frame, document) | A forced sheet is always new and is never the document's default (2A.2, Blink unit and web tests). Studio never calls `force: false`. |
| `CSSStyleSheetHeader` `isViaInspector()`, `frameId`, `ownerNode` | Ownership contract (§2) | An inspector sheet's `ownerNode` is its document (`StyleEngine::CreateInspectorStyleSheet` constructs it with the document as owner). Live: the header's `ownerNode` equals the document's backend node id. |
| `CSSModel.isKnownDefaultInspectorStylesheet` (new, frontend only) | Ownership contract: never the default | Reads only the ids `requestViaInspectorStylesheet` already got from the backend. It never sends a request, so nothing is created. |
| `CSS.setStyleSheetText` / `CSS.getStyleSheetText` | Whole-sheet write, then read back | Per-sheet text since 2A.1, in every session. |
| `DOM.querySelectorAll` | Selector validity, match checks, exact single-element verification | Authoritative; errors identify invalid selectors. |
| `CSS.getMatchedStylesForNode` + `CSSMatchedStyles.propertyState`, `CSS.getComputedStyleForNode` | `inspectEffect`: whether each declaration is active or overridden, plus the computed value | Matches `getComputedStyle` in every live cascade case. |
| `CSSModel` events `StyleSheetRemoved`, `StyleSheetChanged`; `DOMModel` mutation events | Sheet gone with its document; external change; target re-check | Fire as expected. |

No protocol commands were added. Blink is unchanged by this checkpoint.

## 2. Ownership contract

Studio owns exactly the forced inspector stylesheets it created itself, in this DevTools session. It never writes:
- the page's authored stylesheets;
- the document's default inspector stylesheet, which the Styles pane uses;
- another feature's forced sheet, such as upstream AI assistance's.

**Enforced before every write, revert included:**
1. **Engine-created.** The engine writes only sheets in its own `#sheets` map. Entries come only from `createOwnedSheet`, which calls `createInspectorStylesheet(frameId, true)`. No sheet is ever adopted from the page, from text, or from report order.
2. **`PreviewHost.verifyOwnership`.** The backend's header for the sheet must:
   - exist and be an inspector sheet;
   - belong to the preview's frame;
   - have the preview's **live** document as `ownerNode`;
   - not be a known default inspector stylesheet.
3. **Failure.** The sheet is never written again and is no longer tracked. If it held the current preview, the state becomes `none`, and the operation reports `ownership-unsafe` (or `write-failed` for a revert).

**At creation,** `createOwnedSheet` checks again once the sheet exists. If the frame navigated while it was being created, the new sheet belongs to the new document. It is reported as `target-detached`, left empty, and never tracked.

**Replaced from the pre-2A.2 engine:**

| Before (obsolete) | Now |
| --- | --- |
| Created the default with `force: false` first, so first-header lookup would find it. This created the user's default as a side effect. | No `force: false` call. Since 2A.2 the Styles pane asks Blink for its default explicitly. |
| `isSharedTarget`: refused if Studio's sheet was the first inspector header of the frame (order-based) | `verifyOwnership`: frame, owner document, and known-default checks. No order or text involved. |
| `liveOwnedSheets` and a marker-text startup sweeper | Removed. The marker comment is a label for people reading the sheet, never an identity. |

**External changes.** Every write first checks that the sheet still holds exactly what Studio wrote:
- **While a preview is applied,** a mismatch is `conflict`: Studio refuses to write, and only a revert, which empties Studio's own sheet, leaves that state.
- **While idle,** a mismatch means the sheet is left alone and a new one is used.

If the write and the restoring write both fail, the sheet stays tracked in `conflict`, so the next revert still empties it.

## 3. Single-element targeting

- **Rule shape.** The rule is `:where(:root > :nth-child(i) > … > :nth-child(k)):is(<authoring selector>)`. Element-only positions from `:root`, joined by child combinators, match at most one element. `:where()` adds no specificity and `:is()` adds exactly the authoring selector's, so the preview cascades the way the authored CSS will. No `!important` is added.
- **Re-checked twice before every write.** Once when the operation starts, and again immediately before `setText`. Each time, `DOM.querySelectorAll` with the rule must return exactly the target node, and the authoring selector must match it. The target must also be live, an element outside shadow trees, and in the expected frame and document. If the second check yields a different rule (the target moved meanwhile) or fails, nothing is written.
- **Identity is the node.** The target is the DevTools node (`nodeId`), never "whatever is at that position":
  - **Siblings come or go:** the rule is rewritten for the same node's new position.
  - **The element is gone, replaced, or moved:** Blink's DOM agent unbinds a removed node, so a moved element returns under a new id. Studio can't prove it's the same element, and reverts.
  - **The element no longer matches the authoring selector** (class or id change): Studio reverts.
  - **Never:** the preview moves to another element.
- **Scopes.**
  - **Selected only** (`node`): the single-element rule. New elements that match the selector are not styled.
  - **All matches:** the authoring selector itself. It styles every current match, including elements added later, and stops styling one that stops matching. That's CSS semantics, by design.
- **Remaining window.** Between a DOM change and the re-check (one protocol round trip), an element moved into the exact old position *and* matching the authoring selector could match the rule. It's corrected or reverted by the re-check, and was not observed live.

**Live results** (`verify-style-studio-preview.mjs`, checked with `getComputedStyle`):

| Change | Result |
| --- | --- |
| Earlier sibling removed | Same element keeps the preview; rule rewritten to its new position |
| Sibling inserted before the target | Target keeps it; the inserted element is not styled |
| New element matching the selector (Selected only) | Not styled |
| Element moved to another parent | Reverted; no other element styled |
| Element replaced by an identical copy | Reverted; the copy is not styled |
| Class change, so the selector no longer matches | Reverted |
| Id change, so the selector no longer matches | Reverted |
| Element removed | Reverted |
| All matches, then an element added and one changed | New match styled; the one that stopped matching is not |

## 4. Replace and revert

- **Replace, never append.** `apply` and `replace` rewrite Studio's sheet whole: the marker plus one rule.
- **Revert** writes the marker only. It empties every sheet the engine tracks that may hold a preview: the current one, plus any whose emptying failed earlier. Computed styles return exactly to the originals.
- **Injection-proof input.** CSS is parsed with a constructed `CSSStyleSheet`. Anything that leaves the rule (`}`, another rule, nesting, at-rules) is rejected. Dropped declarations are reported, and only the browser's serialisation is written.
- **Measured effect.** `inspectEffect` reports each declaration as `active`, `overridden` or `unknown`, with the computed value.

**Cascade, live:**

| Page rule | Result |
| --- | --- |
| Same specificity (`.pv-box`) | Preview wins; effect `active` |
| Id rule | Page wins; effect `overridden` |
| Inline style | Page wins; effect `overridden` |
| Page `!important` | Page wins. No `!important` is added, and an author `!important` keeps its meaning. |
| Layered id rule | Unlayered preview wins |

Inherited properties reach descendants, and custom properties flow through `var()`. The page DOM is never mutated by a preview. The authored stylesheets are unchanged, and Studio's sheet is not in `document.styleSheets`.

## 5. Asynchronous cancellation and cleanup

**How it works:**
- **One queue.** Operations run one at a time.
- **Epoch.** Each operation captures the epoch when it is requested. `invalidate`, `targetChanged` and `dispose` advance it; `dispose` also sets `disposed` synchronously.
- **Checkpoints within an operation.** It checks the epoch, the disposed flag, ownership, tracking and target resolution:
  - after resolving the target;
  - after getting its sheet;
  - after re-resolving the target;
  - immediately before the write;
  - again after it.
- **Superseded before writing:** nothing is written.
- **Superseded during the write** (the reply arrives after the epoch moved): the operation records the write truthfully, reverts it at once, and reports `superseded` or `disposed`. It doesn't wait for the queued revert.
- **Sheet gone during the write** (`StyleSheetRemoved` with its document): reported as `target-detached`, with nothing tracked and nothing restored.

**Deterministic unit tests** (`StyleStudioPreview.test.ts`). They gate creation, writes and target resolution by hand to force each interleaving. Every test also asserts afterwards that Studio wrote only sheets it created, and that the user's default sheet text is byte-identical.

| Scenario | Outcome |
| --- | --- |
| Apply, then immediate revert | No preview; Studio's sheet empty |
| Target change while the target is checked | `superseded`; zero writes |
| Target change while the write is in flight | Write taken back; `superseded`; sheet empty |
| Replace, then navigation during the write | `target-detached`; nothing restored; the new document's sheets never written |
| Ten rapid replacements | In order; one sheet; last wins |
| Sheet creation completes after cancellation | `superseded`; the created sheet is never written |
| Frame destruction during a write | `target-detached`; nothing tracked |
| Pane disposal during an in-flight apply (and one queued) | Both `disposed`; write taken back; later calls refused |
| Old operation completes after a new one started | Old is `superseded`; only the new target is styled |
| Target moves between the first check and the write | `superseded`; zero writes |
| Target stops resolving just before the write | `target-detached`; zero writes |
| Revalidation queued before a target change | Does not rewrite the old target; reverted |
| Sheet later fails the ownership check | Never written again: not by apply, revert, or close release |
| Ownership check fails at revert time | Not written; `write-failed`; state `none` |

**Live:**
- an apply overtaken by a selection change;
- an apply racing a reload;
- frame destruction;
- pane disposal.

## 6. Lifecycle and the crash-orphan policy

| Event | Behaviour | Evidence |
| --- | --- | --- |
| Revert | Studio's sheet emptied; computed style restored | live, unit |
| Target change | Epoch advanced; preview reverted, including an apply in flight | live, unit |
| Navigation or reload | Sheet dies with the document; a racing apply ends with no preview | live, unit |
| Frame destruction | Sheet removed; preview dropped | live, unit |
| Pane disposal | Preview reverted | live, unit |
| Normal DevTools close | Synchronous best-effort emptying write on `pagehide`/`beforeunload`, after the same ownership check. It worked live, but delivery is not guaranteed. | live |
| Crash or forced termination of DevTools | No release runs. The preview stays applied until the document goes away. | live (simulated orphan) |
| Reconnect to a page with a leftover preview | The orphan stays applied. The new session reports it with its own text and its original id, and nothing in Studio touches it. The user's default and new Styles-pane rules are unaffected. | live |

**An orphan is bounded.** It is an inspector stylesheet: author origin, document scope only, not in `document.styleSheets`, never persisted. It goes away with its document on reload, navigation, or frame removal. It affects that one document's rendering and nothing else.

**Can Chromium identify Studio's sheets across sessions, without CSS text or creation order?**

| Mechanism | Finding |
| --- | --- |
| Creator recorded on the sheet | None. `CSSStyleSheetHeader` has origin `inspector`, frame, owner node (the document) and text, the same for every feature's forced sheet. |
| Default vs forced | Distinguishable since 2A.2, by asking for the default (`force: false`), but that *creates* a default if none exists. Telling Studio's sheet from another feature's forced sheet is not possible. |
| Stylesheet ids | **Stable across sessions.** `IdentifiersFactory::IdForCSSStyleSheet` uses `WeakIdentifierMap<CSSStyleSheet>` (a per-renderer, never-reused counter) prefixed with the unique process id. Live: a reopened session reports Studio's and the orphan's sheets under the same ids. Unique among live sheets; an id can recur only after its renderer process has exited and its process id is reused. |
| Sheet text, marker comment | Not an identity: any feature or user can write any text. Rejected as a basis for cleanup. |

**Conclusion.** No existing mechanism identifies Studio's sheets on its own. Stylesheet ids, which are stable, plus the contract checks are enough for a safe, opt-in cleanup, as long as the frontend remembers which ids it created. That ledger would be new frontend state, not a protocol or Blink change. It is not implemented here; per the checkpoint, no sweeper is added.

**Proposed release policy** (before any preview UI ships, in 2B):
1. **Keep today's guarantees:**
   - revert on every target change, navigation, frame loss and pane disposal;
   - a best-effort release on close;
   - never write a sheet Studio didn't create in this session.
2. **Ledger.** When Studio creates a sheet, record its id, `frameId`, the owner document's backend node id and the inspected URL in DevTools session storage, keyed by page target. Remove entries on revert, navigation, and close release.
3. **On a later session,** a ledger entry is a *candidate* orphan only if all of these hold:
   - the id is reported as an inspector sheet of that frame;
   - its `ownerNode` equals the recorded document;
   - it is not a known default.
4. **User-confirmed cleanup.** Show the candidate in Studio ("A preview from an earlier session is still applied — Remove") and empty it only on confirmation. Never sweep at startup.
5. **Without a ledger entry,** nothing is touched. The user can reload, and that is documented.

## 7. Verification

Every command below ran on fresh builds:
- **Avora.** `npm run build` (copy-sources, then `autoninja -C out/Default chrome`).
- **Frontend test bundle.** Rebuilt by `npm test` without `--skip-ninja`.
- **Blink test binaries.** `autoninja -C out/Default blink_unittests content_shell`.

The overlay and the build tree were byte-identical after copy-sources.

| Suite | Command (from `chromium/src`, or `devtools-frontend/src` for `npm test`) | Result |
| --- | --- | --- |
| Engine + CSSModel unit tests (fresh bundle) | `npm test -- --target=Default --chrome-binary=…/Avora front_end/panels/style_intelligence/StyleStudioPreview.test.ts front_end/core/sdk/CSSModel.test.ts` | 55/55 pass |
| Full Style Intelligence unit suite | `npm test -- --target=Default --skip-ninja --chrome-binary=… front_end/panels/style_intelligence` | 287/287 pass |
| CSS and DOM SDK, AI `ChangeManager` | `npm test -- … CSSModel CSSMatchedStyles CSSStyleSheetHeader CSSStyleDeclaration CSSProperty DOMModel .test.ts, models/ai_assistance/ChangeManager.test.ts` | 143/143 pass |
| Elements panel | `npm test -- … front_end/panels/elements` | 594/594 pass |
| Blink inspector and style unit tests | `out/Default/blink_unittests --gtest_filter='Inspector*:StyleEngineTest.*:*CSSStyleSheet*:*StyleSheetContents*:*ActiveStyleSheets*'` | 436/436 pass, including all 20 isolation and default-identity tests |
| CSS protocol web tests | `vpython3 third_party/blink/tools/run_web_tests.py -t Default inspector-protocol/css` | 138 found, 6 skipped, 132 ran as expected: 130 passed, and 2 failed as `TestExpectations` expects. The 2A.1 and 2A.2 tests (`css-create-stylesheet-multiple`, `css-inspector-stylesheet-text-isolation`, `css-inspector-stylesheet-default-identity`) pass. |
| Canonical default, raw CDP | `node docs/style-intelligence/fixtures/measure-inspector-stylesheet-default.mjs` | 13/13 |
| Text isolation, raw CDP | `node …/repro-inspector-stylesheet-text-slot.mjs` | 19/19 |
| Canonical default, real frontend | `node …/verify-inspector-default-stylesheet-devtools.mjs` | 7/7 |
| Preview engine, live | `node …/verify-style-studio-preview.mjs` | 52/52, plus 3 notes (below) |
| Targeting, live | `node …/verify-style-studio-targeting.mjs` | 46/46 |

**Expected failures and skips** (upstream `TestExpectations`, unrelated to inspector stylesheets):

| Test | Expectation | Actual |
| --- | --- | --- |
| `css-get-navigation-queries.js` | `[ Failure ]`, crbug.com/436805487 | Failure |
| `css-get-animated-styles.js` | `[ Failure Pass ]`, crbug.com/383855460 | Failure |
| `css-get-position-try.js` | `[ Failure Skip ]` | Skipped |
| Five `*view-transition*` tests | Skipped by expectations | Skipped |

**Computed styles and user rules.**
- **Every live check uses `getComputedStyle` in the page.** That covers single-element and all-matches previews, the cascade table, mutation revalidation, revert, and reopen.
- **User rules survive.** A Styles-pane rule added in the default sheet survives:
  - every Studio apply, replace and revert;
  - a DevTools close and reopen;
  - a further "New Style Rule" in the reopened session.
- **The page is untouched.** Its authored stylesheets are byte-identical at the end, and previews never mutated its DOM.

**Notes from the live run** (observations, not checks):
- After the element moved to another parent, the preview was reverted.
- On close, Studio's own preview was released. The simulated orphan stayed applied.
- After reopening, the orphan was still applied; that is the §6 limitation.

**Harness fixes made while running these:**
- The preview verifier's reopen step now waits for the reopened frontend's inspector headers before reading them.
- `verify-inspector-default-stylesheet-devtools.mjs` now polls for the reopened frontend's target instead of a fixed sleep.

Under load, both had read too early. Neither changes what is checked.

**Not tested:**
- a real renderer or DevTools crash (simulated by writing a forced sheet no engine owns);
- cross-origin (out-of-process) iframes;
- shadow-tree targets, which are refused before any write;
- the engine on a UI path, since there is none yet.

## 8. History: the two blockers, resolved

**Text corruption (resolved in 2A.1).**
- **Defect.** Blink stored inspector-sheet text keyed by the owner document, so every inspector sheet of a document shared one slot. A new sheet reported the last text written to any of them. After a reconnect every sheet reported that text, and a "New Style Rule" then deleted the user's earlier rules from the page.
- **Fix.** Text is stored per sheet in `InspectorResourceContainer`, held weakly.

**Default ownership (resolved in 2A.2).**
- **Defect.** "New Style Rule" took the first inspector header reported for the frame. That could be a forced sheet, in the same session or after a reconnect. Blink also remembered the default per CSS agent, so a reconnect created a second default.
- **Ruled out.** Making the report order deterministic was measured and was not enough.
- **Fix.** Blink records each document's default in `InspectorResourceContainer`, and the frontend asks for it with `force: false`.

Both are documented, with tests, in `chromium-inspector-stylesheet-isolation.md`.

## 9. Deferred to 2B and later

- Preview UI in the Studio tab: apply, replace, revert, conflict and effect display.
- AI-generated CSS, conversational refinement, CSS export.
- The orphan ledger and user-confirmed cleanup (§6).
- Shadow-tree and pseudo-element targets, which are refused today as `target-unsupported` or `selector-mismatch`.
- Upstream AI assistance `ChangeManager`: it now benefits from 2A.1 and 2A.2 but is otherwise unchanged.

**Rejected alternatives:**
- writing previews into the default inspector sheet, which belongs to the user;
- inline styles through `CSS.setStyleTexts`, which mutate the DOM;
- forced pseudo-classes, which trigger the page's own rules;
- class-based scoping like `ChangeManager`'s, which mutates the DOM.
