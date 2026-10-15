# Style Studio — Checkpoint 0 architecture audit

Audit date: 2026-10-14. Scope: read-only inspection of this checkout. Nothing is implemented.

Labels used below:

- **[V] verified**: read directly in this checkout's source (file:line given).
- **[I] inferred**: follows from verified code but was not exercised on a running build.
- **[U] unverified**: plausible, not checked; must be checked before relying on it.

Path shorthands:

- `OVL/` = `src/third_party/devtools-frontend/src/` (Avora overlay, the source of truth for edits)
- `DT/` = `chromium/src/third_party/devtools-frontend/src/` (the build tree that `copy-sources.sh` writes into)
- `BLINK/` = `chromium/src/third_party/blink/`

---

## 1. Confirmed files and integration points

### Repository and overlay model

| Fact | Label |
| --- | --- |
| Chromium 155.0.8038.0 at `58de1c4e24` (`CHROMIUM_VERSION`). DevTools frontend checkout at `48b228da`. | V |
| Avora is an overlay: edits live in `src/` and `build-scripts/copy-sources.sh` copies files whose bytes differ into `chromium/src` with `cp -p` (mtime preserved). | V |
| `npm run build` → `build-scripts/incremental-build.sh` runs `copy-sources.sh` first (`set -euo pipefail`), then `autoninja -C out/Default chrome`. This is the Phase 2B safeguard and must stay the build entry point. | V |
| Every file under `src/` is currently byte-identical to its counterpart in `chromium/src` (ran `cmp` over the whole overlay: no differences). | V |

### Style Intelligence pane (`OVL/front_end/panels/style_intelligence/`)

| Piece | Role | Label |
| --- | --- | --- |
| `style_intelligence-meta.ts` | `UI.ViewManager.registerViewExtension` with `location: ELEMENTS_SIDEBAR`, `id: 'elements.style-intelligence'`, title "Intelligence", `order: 2`, `persistence: PERMANENT`; lazily imports the bundle and returns `StyleIntelligencePane.instance()`. | V |
| `style_intelligence.ts` | Bundle entrypoint; re-exports all modules. | V |
| `BUILD.gn` | `devtools_ui_module("style_intelligence")`, `devtools_entrypoint("bundle")`, `devtools_entrypoint("meta")`, `devtools_ui_module("unittests")` (14 test files). Deps include `core/sdk`, `models/bindings`, `models/computed_style`, `ui/legacy`, `ui/lit`, `panels/elements`. | V |
| `StyleIntelligencePane.ts` (944 lines) | `class StyleIntelligencePane extends UI.Widget.VBox` (`useShadowDom: true`), singleton via `instance()`. Lit `DEFAULT_VIEW(input, output, target)` pattern with `ViewInput` / `ViewOutput`; `performUpdate()` builds chain/facts/plate and calls the view. | V |
| `StyleIntelligenceModel.ts` | `describe(node, onPrimary)`: node-scoped reads (`getComputedStyleForNode`, `cachedMatchedCascadeForNode`, `DOM.getBoxModel`); `identityForNode(node)`. | V |
| `LayoutRelationshipsLoader.ts` | Calls the generated typed agent for `CSS.getLayoutRelationshipsForNode` (Phase 2B). | V |
| Others | `ElementChain`, `ElementFacts`, `Plate`, `LayoutExplanation`, `SpacingExplanation`, `Diagnostics`, `Stacking*`, `Presentation`, `LayeringPresentation`, `LayoutRelationshipsPresentation`, `styleIntelligencePane.css`. | V |

Registration-only edits to upstream DevTools files (all carried in the overlay) — `git -C DT diff --stat` shows exactly these plus generated protocol files: [V]

- `front_end/entrypoints/devtools_app/devtools_app.ts` (+1 import of the meta module)
- `front_end/entrypoints/devtools_app/BUILD.gn` (+1 dep)
- `front_end/BUILD.gn` (+1: `panels/style_intelligence:unittests`, line 202)
- `front_end/panels/elements/BUILD.gn`, `front_end/models/computed_style/BUILD.gn`, `front_end/models/bindings/BUILD.gn` (+1 visibility each)
- `config/gni/devtools_grd_files.gni` (+19)
- `third_party/blink/public/devtools_protocol/domains/CSS.pdl` (+39, vendored copy) and `browser_protocol.json` (+80)
- `front_end/generated/{InspectorBackendCommands.ts, protocol.ts, protocol-mapping.d.ts, protocol-proxy-api.d.ts}` — regenerated, **deliberately not in the overlay**.

### How the selected node reaches the pane [V]

1. Constructor: `UI.Context.Context.instance().addFlavorChangeListener(SDK.DOMModel.DOMNode, this.#onSelectedNodeChanged, this)` (`StyleIntelligencePane.ts:387`).
2. `#onSelectedNodeChanged` (`:554`) hides highlight, clears relationships, sets `this.#computedStyleModel.node = UI.Context.Context.instance().flavor(SDK.DOMModel.DOMNode)`, binds that node's `DOMModel` (`DocumentUpdated`, `NodeRemoved` listeners, `:839–857`), schedules `describe`, schedules Layering after 120 ms.
3. A pane-owned `ComputedStyle.ComputedStyleModel.ComputedStyleModel(null)` provides `CSS_MODEL_CHANGED` and `COMPUTED_STYLE_CHANGED`; both go to `#onStyleChanged` (50 ms debounce, bumps relationship generation, invalidates Layering snapshot).
4. Stale results are dropped by `#requestId` comparison.
5. Lifecycle: `wasShown` → reload; `willHide` / `onDetach` → hide highlight; `onDetach` → unbind DOM model.

### Existing model integrations [V]

| Model | Use today |
| --- | --- |
| `DOMModel` | `nodeForId`, `DocumentUpdated` / `NodeRemoved` events, `overlayModel()`, `cssModel()`. |
| `CSSModel` | `cachedMatchedCascadeForNode`, computed style, `getAgent()` for the Phase 2B command; `isViaInspector()` filtering. |
| `OverlayModel` | `node.domModel().overlayModel().highlightInOverlay({node}, 'all')` (`StyleIntelligencePane.ts:708`). |
| `Common.Revealer` | Inspect (reveal node) and Source (reveal UI location). |
| Target lifecycle | Indirect only: via the selected node's `DOMModel` and `ComputedStyleModel`. The pane does **not** use `TargetManager.observeModels` or listen to `ResourceTreeModel.PrimaryPageChanged`. |

---

## 2. Relevant APIs and their actual signatures (this checkout)

### DevTools protocol (Blink PDL: `BLINK/public/devtools_protocol/domains/CSS.pdl`) [V]

| Command | Signature | Note |
| --- | --- | --- |
| `CSS.createStyleSheet` | `(Page.FrameId frameId, optional boolean force) → DOM.StyleSheetId` (PDL:678) | `force: true` creates a new sheet per call; `false` reuses the frame document's default inspector sheet. |
| `CSS.setStyleSheetText` | `(StyleSheetId, string text) → optional sourceMapURL` (PDL:1048) | Whole-sheet replace. |
| `CSS.addRule` | `(StyleSheetId, ruleText, SourceRange location, optional nodeForPropertySyntaxValidation) → CSSRule` | |
| `CSS.getStyleSheetText`, `CSS.getMatchedStylesForNode`, `CSS.getComputedStyleForNode`, `CSS.setStyleTexts` | present | |
| `CSS.getLayoutRelationshipsForNode` | `(DOM.NodeId) → LayoutRelationships` (PDL:870, experimental) | Avora Phase 2B; preserved. |
| **No** `CSS.removeStyleSheet` / delete command | — | Confirmed by listing every `command` in CSS.pdl. Inspector sheets cannot be removed through CDP; only emptied. |
| `DOM.querySelectorAll` | `(NodeId nodeId, string selector) → array of NodeId` (DOM.pdl:564) | |
| `Overlay.highlightNode` | accepts `selector` (used by `OverlayModel` `HighlightData.selectorList`) | Highlights all matches of a selector. |

### DevTools frontend SDK (`DT/front_end/core/sdk/`) [V]

```ts
// CSSModel.ts
async createInspectorStylesheet(frameId: Protocol.Page.FrameId, force = false): Promise<CSSStyleSheetHeader|null>   // :687, throws on protocol error
async requestViaInspectorStylesheet(maybeFrameId?: Protocol.Page.FrameId|null): Promise<CSSStyleSheetHeader|null> // :667
async setStyleSheetText(styleSheetId, newText: string, majorChange: boolean): Promise<string|null>                 // :831, returns error string or null; calls markUndoableState + fireStyleSheetChanged
async getStyleSheetText(styleSheetId): Promise<string|null>                                                       // :857
async addRule(styleSheetId, ruleText: string, ruleLocation: TextRange): Promise<CSSStyleRule|null>                 // :648
styleSheetHeaderForId(id): CSSStyleSheetHeader|null; styleSheetHeaders(): CSSStyleSheetHeader[]
cachedMatchedCascadeForNode(node: DOMNode): Promise<CSSMatchedStyles|null>; discardCachedMatchedCascade(): void
getAgent(): ProtocolProxyApi.CSSApi
Events: StyleSheetAdded | StyleSheetChanged | StyleSheetRemoved | ModelWasEnabled | ModelDisposed (…:999–1005)
// PrimaryPageChanged → resetStyleSheets() dispatches StyleSheetRemoved for every header (:880–892)

// CSSStyleSheetHeader.ts
isViaInspector(): boolean  // origin === 'inspector' (:181)

// CSSMatchedStyles.ts
propertyState(property: CSSProperty): PropertyState|null   // :1028

// DOMModel.ts
agent: ProtocolProxyApi.DOMApi                                         // :1625 (public)
querySelector(nodeId, selector): Promise<NodeId|null>                  // :2137
querySelectorAll(nodeId, selector): Promise<NodeId[]|null>             // :2141 — swallows the protocol error (returns undefined nodeIds)
requestDocument(): Promise<DOMDocument|null>; existingDocument(): DOMDocument|null; nodeForId(id): DOMNode|null
pushNodesByBackendIdsToFrontend(Set<BackendNodeId>)
markUndoableState(minorChange?: boolean): void; undo(): Promise<void>
DOMNode: frameId(): FrameId|null; isInShadowTree(): boolean; ancestorShadowRoot(): DOMNode|null

// OverlayModel.ts
highlightInOverlay(data: HighlightData, mode?: string, showInfo?: boolean): void   // :339
HighlightData = HighlightNodeData | HighlightDeferredNode | HighlightObjectData | {clear: boolean}  // :1084; node data accepts selectorList
static hideDOMNodeHighlight(targetManager)

// panels/elements/DOMPath.ts (already a dep of style_intelligence via ../elements:bundle)
cssPath(node: DOMNode, optimized?: boolean): string   // :14
```

### Blink behaviour behind the inspector stylesheet [V unless marked]

- `InspectorCSSAgent::createStyleSheet` → `CreateViaInspectorStyleSheet(document, force)` → `document->GetStyleEngine().CreateInspectorStyleSheet()` (`inspector_css_agent.cc:3534`, `:4457`). Only HTML or SVG documents. [V]
- `StyleEngine::CreateInspectorStyleSheet` creates a `CSSStyleSheet` owned by the engine's `inspector_style_sheet_list_` (`style_engine.cc:363`). **No DOM node is inserted.** [V]
- Cascade placement (`style_sheet_collection.cc:194–199`): for the document scope only (`if (!is_shadow_tree_)`), inspector sheets are appended **after** injected author sheets, document `<style>`/`<link>` candidates, and adopted sheets. Origin: author. Not in a cascade layer. [V]
- Not exposed to page script: `document.styleSheets` is built only from DOM candidate nodes (`style_sheet_collection.cc:140–151`). [V]
- CSP: the creation path performs no Content Security Policy check, and no `<style>` element exists for `style-src` to block. [V for absence of a check in this path; I that strict-CSP pages accept the preview — needs a live check.]
- Lifetime: the list is cleared only in `StyleEngine::DidDetach` (document teardown, `style_engine.cc:1058–1080`). `InspectorCSSAgent::disable()` calls `Reset()` and does **not** remove inspector sheets (`inspector_css_agent.cc:970`). So an inspector sheet keeps applying after DevTools closes or detaches, until the document is replaced. [V]

### Existing AI infrastructure [V]

- Upstream DevTools AI assistance is present: `DT/front_end/models/ai_assistance/` (`agents/StylingAgent.ts`, `ChangeManager.ts`, `ExtensionScope.ts`, `injected.ts`, `BuiltInAi.ts`, `AiConversation.ts`, `AiHistoryStorage.ts`) and `core/host/AidaClient.ts`.
- `ChangeManager` keeps one `createInspectorStylesheet(frameId, /*force*/ true)` sheet per (CSSModel, frame), rewrites it wholesale with `setStyleSheetText`, and empties it on `PrimaryPageChanged` and `ModelDisposed`. This is the closest existing precedent for a reversible preview.
- **But** its rules are scoped by a class the page script adds to the element: `injected.ts:122–124` does `el.classList.add(className)` (`ai-style-change-N`). That is a DOM mutation visible to the page (MutationObservers, `className` reads, framework reconciliation). Style Studio should not reuse that scoping.
- The AIDA backend requires a Google identity: `AidaClient.checkAccessPreconditions()` returns `NO_ACCOUNT_EMAIL` / `SYNC_IS_PAUSED`, and `chrome/browser/devtools/aida_client.cc` uses `IdentityManager`. `build-scripts/args.gn` sets `google_api_key = "no"` and leaves OAuth unset on purpose. **[I]** AIDA (and therefore the upstream Styling agent) is not usable in Avora. **[U]** whether `BuiltInAi` (on-device model) is available in this build.
- Avora itself has no AI-provider code: a search of `src/` for anthropic/openai/LLM matched only upstream `OpenAiMode` (omnibox AI Mode). [V]

---

## 3. Recommended preview implementation strategy

**A frontend-only `StylePreviewSession`, backed by a dedicated forced inspector stylesheet per frame document, scoped by selector, never by DOM mutation.** No Blink or CDP change is needed for Checkpoints 1–4. [I]

1. **Ownership.** A `StylePreviewController` (new module in `panels/style_intelligence/`) owns a map `(CSSModel, FrameId) → StyleSheetId` it created with `createInspectorStylesheet(frameId, true)`. It never writes to any sheet it did not create, and it never calls `requestViaInspectorStylesheet` for its own writes (that returns the user's shared "inspector-stylesheet").
2. **Protect the user's default inspector sheet.** `requestViaInspectorStylesheet` returns the *first* via-inspector header for a frame (`CSSModel.ts:671`). If Style Studio's forced sheet were created first, the Styles pane "+ New Style Rule" (`StylesSidebarPane.ts:1467`, `ElementsPanel.ts:459`) would write the user's rule into Style Studio's sheet, and the next preview rewrite would erase it. Mitigation: call `requestViaInspectorStylesheet(frameId)` once before creating the forced sheet so the default exists first. [V for the lookup order; I for the consequence; the same hazard exists upstream with `ChangeManager`.]
3. **Write model.** Keep the full preview as structured data (target selector + declarations) and render it to text; apply with `setStyleSheetText(id, text, true)` (whole-sheet replace, idempotent). Revert = `setStyleSheetText(id, '', true)`. "Apply" in Style Studio means "export", never writing into an authored sheet.
4. **Scoping.** Use the resolved selector the user chose (Checkpoint 1). For "this element only", derive a selector with `DOMPath.cssPath(node, true)` and verify with `DOM.querySelectorAll` that it resolves to exactly the selected node; if it does not, say so rather than silently widening.
5. **Cascade.** The sheet is unlayered author CSS placed last, so it beats equal-or-lower-specificity unlayered rules and every layered normal rule, but loses to higher-specificity rules, inline `style=""`, and page `!important`. Recommendation: emit preview declarations with `!important` by default and keep the authored form (without `!important`) for export. After every apply, read `getMatchedStylesForNode` for the target and report per declaration whether it is `ACTIVE` via `propertyState` — a measured fact, not an assumption. Declarations that lose (inline `!important`, layered `!important`, transitions) are shown as not taking effect.
6. **Facts vs proposals.** Style Intelligence filters via-inspector rules out of attribution (`StyleIntelligenceModel.ts:290`, `:679`; `StackingChainLoader.ts:218`). While a preview is live, computed and box facts *will* reflect it, but the cascade-override diagnostic can describe a page declaration as overridden "by another author declaration" when the winner is the preview. The pane must mark facts as "measured with preview applied" and suppress or reword that diagnostic for properties the preview owns. AI output is stored and rendered in a separate proposal model and never fed into `ElementFacts` / `ElementChain`.
7. **Cleanup triggers.** Empty owned sheets on: explicit Revert; pane `onDetach`; `ResourceTreeModel.PrimaryPageChanged` (via `TargetManager.addModelListener`, as `ChangeManager` does); `CSSModel.Events.ModelDisposed`; `StyleSheetRemoved` for an owned id (drop the mapping, the document is gone); DevTools window `beforeunload` (best effort). Re-create lazily on the next apply.
8. **Undo stack.** `setStyleSheetText` marks an undoable state and Blink records it in inspector history [I], so Elements' Cmd+Z can revert preview text behind the controller's back. On `StyleSheetChanged` for an owned id, compare `getStyleSheetText` to the controller's last-written text and treat a mismatch as "preview changed externally" rather than overwriting silently.

---

## 4. Known limitations and risks

| # | Risk / limitation | Label | Consequence |
| --- | --- | --- | --- |
| 1 | No CDP command removes an inspector sheet; revert leaves an empty sheet until navigation. | V | Harmless (empty sheet), but the "inspector-stylesheet" entry persists in Sources/Styles until reload. |
| 2 | Inspector sheets survive DevTools close/crash and `CSS.disable` (`DidDetach` is the only clear). | V | If DevTools closes without cleanup, the preview stays on the page until reload. Best-effort cleanup only; a Blink change (drop agent-created sheets on disable) is the only full fix and would be Checkpoint 5 material **only if** the live pass shows this matters. |
| 3 | Inspector sheets are not added to shadow-tree scopes (`if (!is_shadow_tree_)`). | V | Elements inside a shadow root cannot be previewed by selector, except via `::part()` / host selectors. Checkpoint 1 must detect `node.isInShadowTree()` and decline explicitly. Inline-style fallback would mutate the DOM and is not recommended. |
| 4 | Frames: one sheet per frame document. Same-process iframes share a target CSSModel with distinct `frameId`; OOPIFs are separate targets with their own CSSModel. | V (API) / I (OOPIF behaviour) | Controller keys by `(CSSModel, frameId)`; selection outside the main frame needs a live check. Prior live passes never established iframe selection (`limitations.md`). |
| 5 | Cascade: preview loses to inline `!important`, layered `!important`, and running transitions; `!important` previews beat page animations. | I (from cascade rules + verified placement) | Must be reported per declaration using `propertyState`, not assumed. |
| 6 | Cascade-override diagnostic misattributes the winner while a preview is live. | I | Wording bug if not handled (see §3.6). |
| 7 | `requestViaInspectorStylesheet` can return Style Studio's sheet to the Styles pane. | V/I | User rules erased on next preview write (see §3.2). |
| 8 | Elements undo can rewrite owned sheets. | I | Detect and reconcile (§3.8). |
| 9 | `DOMModel.querySelectorAll` drops the protocol error message. | V | For "invalid selector" feedback, call `domModel.agent.invoke_querySelectorAll` and read `getError()`. |
| 10 | `querySelectorAll` on a document does not pierce shadow roots or iframes. | I | Selector input is per-document; say so in the UI. |
| 11 | CSP on strict pages. | I | No CSP check on the creation path; confirm with a live fixture served with `Content-Security-Policy: style-src 'none'`. |
| 12 | AI provider: AIDA is unusable without Google identity/API keys; no Avora provider exists. | I | Checkpoint 3 needs a provider decision (see §6 note). |
| 13 | Preview refreshes the pane: owned-sheet edits fire `StyleSheetChanged` → `CSS_MODEL_CHANGED` → `describe` + Layering snapshot invalidation per keystroke. | I | Debounce preview writes (≥150 ms) so the 50 ms pane debounce does not run a full describe and snapshot recapture per keypress. |
| 14 | Pseudo-element targets (`::before`, etc.). | U | `cssPath` on a pseudo node and selector round-trip need checking in Checkpoint 1. |

---

## 5. Proposed test locations and commands

### Unit tests (Mocha/Karma, DevTools runner) [V for the runner and wiring]

- Location: new `*.test.ts` files beside the sources in `OVL/front_end/panels/style_intelligence/`, added to `devtools_ui_module("unittests")` in that `BUILD.gn` (already wired into `front_end/BUILD.gn:202`).
- Pattern: existing tests are pure-function tests (e.g. `ElementChain.test.ts`). Style Studio logic should be split the same way: selector resolution result shaping, preview text rendering, `!important`/export formatting, and per-declaration effectiveness classification are pure functions; the controller gets a fake CSSModel/DOMModel.
- Command (from the build tree, after `copy-sources.sh`):

  ```bash
  cd chromium/src/third_party/devtools-frontend/src
  PATH="$PATH:$HOME/depot_tools" npm test -- --target=Default front_end/panels/style_intelligence
  ```

  `scripts/run_on_target.mjs` runs ninja for the target first unless `--skip-ninja`.
- Baseline result for the existing suite: not established in this audit (browser launch failure; see §7).

### Protocol-level behaviour (Blink web tests) [V that the harness and neighbours exist]

- `BLINK/web_tests/http/tests/inspector-protocol/css/css-create-stylesheet-and-add-rule.js` and `css-create-stylesheet-multiple.js` already exist. A Style Studio isolation test (forced sheet changes computed style; `document.styleSheets.length` unchanged; empty text restores computed style; a shadow-internal element is unaffected) can be written using only existing commands. Placing it in `src/third_party/blink/web_tests/...` keeps it in the overlay.
- Command (requires building `blink_tests`, a large build):

  ```bash
  cd chromium/src && autoninja -C out/Default blink_tests && third_party/blink/tools/run_web_tests.py -t Default http/tests/inspector-protocol/css/
  ```

### Integration / live verification

- Same method prior phases used: a fixture page under `docs/style-intelligence/fixtures/` (new `style-studio.html` with specificity, inline-style, `!important`, `@layer`, iframe, open shadow root, and a CSP-protected variant) exercised on a built `Avora.app` (`npm run build && npm run start`).
- DevTools e2e (`DT/test/e2e/`) exists, but there is no Style Intelligence e2e today. [V] Adding one is possible but not required by previous phases.

---

## 6. Minimal Checkpoint 1 file-change plan (element targeting and selector resolution)

No Blink, no PDL, no generated-file changes. All edits in `OVL/front_end/panels/style_intelligence/` plus one GRD list.

| File | Change |
| --- | --- |
| `StyleStudioTarget.ts` (new) | Pure: `type StudioTarget = {kind: 'selected-node', nodeId} | {kind: 'selector', selector, matches: NodeId[]}`; `classifyResolution(result)` → `ok | no-match | invalid-selector | shadow-scoped | not-element`; `nodeSelector(node)` wraps `DOMPath.cssPath(node, true)`. |
| `StyleStudioTargetResolver.ts` (new) | Resolves a selector in the selected node's document via `domModel.agent.invoke_querySelectorAll({nodeId: document.id, selector})`, keeps `getError()`; verifies a derived selector round-trips to exactly the selected node; request-id guarded like the pane. |
| `StyleStudioTarget.test.ts` (new) | Classification, round-trip mismatch, invalid selector, shadow-tree decline, stale-response drop (fake DOMModel). |
| `StyleIntelligencePane.ts` | Add a collapsed "Studio" section below the existing content: target line (selected element / selector input), match count, Highlight-all via `highlightInOverlay({node: document, selectorList})`. Existing sections, ordering, and `describe` flow untouched. |
| `styleIntelligencePane.css` | Styles for the section. |
| `BUILD.gn` | Add the two sources and the test. |
| `style_intelligence.ts` | Export the new modules. |
| `OVL/config/gni/devtools_grd_files.gni` | List the new `.js` outputs. |
| `docs/style-intelligence/style-studio.md` (new) | Checkpoint 1 contract, declines, and live-pass notes. |

Checkpoint 1 writes no CSS anywhere; it only reads DOM and draws the overlay.

**Decision needed before Checkpoint 3 (not Checkpoint 1):** which AI provider Style Studio calls. AIDA is not available to Avora (§2). Options are a browser-side provider bridge (Avora would need to own an API-key/auth surface in `chrome/browser/devtools`, which is browser code outside the pane) or the on-device `BuiltInAi` path if this build exposes it (unverified).

---

## 7. Git status and branch information

Taken 2026-10-14 during this audit.

**Avora repo** (`/Users/bglover/projects/avora`) [V]

- Branch `main` at `fdc3a53` ("Rebuild the mini window on a real popup Browser."), **ahead of `origin/main` by 1 commit** (unpushed). Working tree clean before this audit; the only new file is this document (uncommitted).
- Other local branches: `style-intelligence-phase-2b` at `89db72c`; worktrees `claude/exciting-hamilton-80b3f7` (`c958e09`) and `claude/fervent-yonath-ba7aa7` (`0ef1378`); several `cursor/*` branches and `docs/macos-auth-sessions`. None are touched by this audit.
- Recent Style Intelligence history: `6eb05f4` Phase 1, `c958e09` Phase 2A, `4037fff` Phase 2B (layout relationships), `0e8c1c3` incremental-build source copy, `2b7922d` Measured Chain rebuild.

**Chromium build tree** (`chromium/src`, `58de1c4e24`) [V]

- 160 modified/untracked paths — these are the applied Avora overlay (branding, browser UI, Blink inspector agent, etc.), not in-flight work. All match `src/` byte-for-byte.
- DevTools frontend (`48b228da`): 13 modified files + untracked `front_end/panels/style_intelligence/` (listed in §1). The four `front_end/generated/*` diffs exist only in the build tree by design.

**Baseline test run (not established).** `npm test -- --target=Default front_end/panels/style_intelligence` built the target, but then Karma's headless browser closed during launch: `TargetCloseError: Protocol error (Target.setDiscoverTargets): Target closed`. No tests executed, so this audit has no pass/fail baseline for the existing suite. The likely cause is that the agent's sandboxed shell cannot launch the test browser [U]. Before Checkpoint 1, re-run the command from a normal terminal and record the baseline.
