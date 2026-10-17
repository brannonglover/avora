# Chromium patch: per-sheet text for inspector stylesheets

Reapply this patch when the Chromium tree is upgraded, unless upstream has fixed the same defect. It changes no protocol interface and no author-stylesheet behaviour.

## Defect

`CSS.createStyleSheet` makes an *inspector stylesheet*. A document gets one default sheet (`force: false`) and any number of forced ones (`force: true`); upstream's AI assistance `ChangeManager` and Style Studio both create forced ones. Every inspector stylesheet's owner node is its document.

Blink keeps edited stylesheet text in `InspectorResourceContainer`. That container belongs to `WebDevToolsAgentImpl`, one per inspected local root, not to a DevTools session. It outlives sessions so that a reconnecting session can recover text the CSSOM cannot reproduce (comments, formatting), and it is cleared on a main-frame commit. Inspector-sheet text was stored there **keyed by the owner document's node id**, so all inspector stylesheets of a document shared one slot:

- `InspectorStyleSheet::InnerSetText` wrote the slot on every edit.
- `InspectorStyleSheet::InspectorStyleSheetText`, called from the constructor's `UpdateText()`, read it whenever a sheet was bound: on creation, and for every sheet when a session reconnects or re-enables CSS.

**Consequences:**
- A new forced sheet reported the text of whichever sheet was written last, though its real contents were empty.
- After DevTools was reopened, every inspector sheet reported that same text.
- A "New Style Rule" in the reopened session then wrote that text back as the user's sheet and **deleted the user's earlier rules from the page**.

With only the default sheet the slot always mirrored it, which is why the defect stayed hidden until forced sheets existed.

## Change

`third_party/blink/renderer/core/inspector/inspector_resource_container.h`
`third_party/blink/renderer/core/inspector/inspector_resource_container.cc`
`third_party/blink/renderer/core/inspector/inspector_style_sheet.cc`

- **Storage.** `InspectorResourceContainer` gains `StoreInspectorStyleSheetContent` and `LoadInspectorStyleSheetContent`, backed by `HeapHashMap<WeakMember<const CSSStyleSheet>, String>`. It is traced, and cleared with the other maps in `DidCommitLoadForLocalFrame`.
- **Write.** `InnerSetText` stores inspector-origin text with the new call, keyed by `page_style_sheet_`.
- **Read.** `InspectorStyleSheetText` loads it the same way. A sheet with no entry reports `""`, which matches its empty contents.

`<style>` elements (`StoreStyleElementContent` by element node id) and URL sheets (`StoreStyleSheetContent`) are untouched. The old document-keyed entry was written and read only by these two inspector-origin paths; nothing else read a document's node id from `style_element_contents_`.

**Storage semantics before and after:**

| | Before | After |
| --- | --- | --- |
| Key for inspector-sheet text | owner document's `DOMNodeId` (one per document) | the `CSSStyleSheet` (one per sheet), held weakly |
| A new inspector sheet reports | the last text written to any inspector sheet of the document | `""` |
| Reconnect, single default sheet | its own text | its own text (unchanged) |
| Reconnect, several sheets | all report the last-written text | each reports its own |
| Main-frame navigation | cleared | cleared |
| Frame or document destroyed | entry lingers under a dead id until root commit | entry goes with the sheet |

Inspector stylesheets are never recreated for the same document, and they die with it, so no restoration path needed text to move from one sheet object to another.

These sources live in the Chromium tree. Copies are in `avora/src` at the same relative paths, so `build-scripts/copy-sources.sh` reapplies them after a sync.

## Tests

`third_party/blink/renderer/core/inspector/inspector_css_agent_test.cc`, run with `blink_unittests`:

- **Fixture.** `InspectorStyleSheetIsolationTest` drives real DOM and CSS agent sessions that share one container, the way sessions share `WebDevToolsAgentImpl`'s. Closing one and opening another is a reconnect.
- **Main-document tests:**
  - `SingleDefaultSheetKeepsItsText` (compatibility)
  - `NewSheetDoesNotInheritText`
  - `SheetsUpdateIndependently`
  - `ReconnectRestoresEachSheetsOwnText`
  - `NewStyleRuleAfterReconnectKeepsUserRules`
  - `ReverseCreationOrder`
  - `ClearingOneSheetLeavesTheOthers`
  - `SheetCreatedInLaterSessionStartsEmpty`
  - `MainFrameNavigationClearsStoredText`
- **Child frames.** `InspectorCSSAgentSubframeTest.InspectorSheetsOfChildDocumentAreIndependent` covers a child document's sheet and destroying its frame.
- **Assertions.** Tests check stored text, real CSSOM rule counts, and computed styles.
- **Against upstream.** The five isolation tests (`NewSheetDoesNotInheritText`, `ReconnectRestoresEachSheetsOwnText`, `NewStyleRuleAfterReconnectKeepsUserRules`, `ReverseCreationOrder`, `SheetCreatedInLaterSessionStartsEmpty`) fail, and the compatibility tests pass.

`third_party/blink/web_tests/inspector-protocol/css/css-inspector-stylesheet-text-isolation.js`, run with `run_web_tests.py`:
- Two sheets, a disconnect and a reattach through `page.createSession()`, then a Styles-pane-style append, clearing the second sheet, and a sheet created after the reattach.
- Texts reported on reattach are sorted, because their order is not defined (see below).

`third_party/blink/web_tests/inspector-protocol/css/css-create-stylesheet-multiple-expected.txt`:
- **Baseline corrected.** The old baseline recorded the defect: the text of the new forced sheets `s2` and `s3` included `s1`'s rules, which were never in their contents. The test's purpose, adding rules to several new sheets, is unchanged.

`docs/style-intelligence/fixtures/repro-inspector-stylesheet-text-slot.mjs` performs the same verification over raw CDP against a built Avora, with computed styles checked in the page.

## Canonical default inspector stylesheet (Checkpoint 2A.2)

**Before.**
- **Frontend.** `CSSModel.requestViaInspectorStylesheet` (the Styles pane's "New Style Rule" and the Elements view-transition helper) took the first inspector header reported for the frame. If a forced sheet was created before the default, "New Style Rule" wrote into it. That was true within one session, and after reconnects too: reconnect reports come in hash order (`SetActiveStyleSheets`).
- **Blink.** Blink kept the document's default per CSS agent (`InspectorCSSAgent::default_inspector_stylesheets_`). After a reconnect, `CSS.createStyleSheet(force: false)` created a second, empty default.
- **Measured.** Making the report order deterministic does not fix this: with a forced sheet created first, the forced sheet stays first (`fixtures/measure-inspector-stylesheet-default.mjs` history; `style-studio-checkpoint-2a-findings.md` §9).

**After.**

`third_party/blink/renderer/core/inspector/inspector_resource_container.{h,cc}`, `inspector_css_agent.{h,cc}`:
- **Where the default lives.** `InspectorResourceContainer` holds each document's default inspector stylesheet in `HeapHashMap<WeakMember<Document>, WeakMember<CSSStyleSheet>>`. That's the container that already holds per-sheet text: one per inspected local root, outliving CSS agents. The agent's own map is removed.
- **What `CreateViaInspectorStyleSheet(force: false)` does.** It returns the container's default in every session, creating and recording one only if there is none.
- **Forced sheets.** `force: true` always creates a separate sheet and never records it.
- **Validation.** `DefaultInspectorStyleSheet(document)` returns the sheet only while it is still one of the document's live inspector stylesheets (`StyleEngine::InspectorStyleSheets()`), so a detached document's default is never returned.
- **Identity, not ids.** The reference is the `CSSStyleSheet` object, never a protocol id; ids are re-issued per session.
- **Unbound defaults.** A default this agent has not bound yet is bound on demand.

`third_party/devtools-frontend/src/front_end/core/sdk/CSSModel.ts`:
- **How the frontend picks.** `requestViaInspectorStylesheet` asks the backend with `CSS.createStyleSheet(frameId)` (no `force`) instead of taking the first inspector header.
- **Caching.** It remembers the returned id per frame and asks again only once that header is gone. Upstream's existing test, that a second request does not call the backend, still passes.
- **Invalidation.** The memory is cleared with the headers on a main-frame reset or a CSS suspend.
- **Failures.** A failed request (frame gone, target disposed) returns `null` as before.
- **Callers.** "New Style Rule" and the view-transition helper both want the user's default. Neither relied on the first-header semantics beyond that. Upstream's AI `ChangeManager` and Style Studio call `createInspectorStylesheet(frameId, true)` directly and are unaffected.

**Default-sheet identity and lifetime:**

| Event | Default |
| --- | --- |
| First `force: false` in a document | created and recorded |
| Later `force: false`, any session | the same sheet |
| `force: true` | a new sheet; never the default |
| DevTools closed and reopened, including abruptly | the same sheet, with its rules, comments, and formatting |
| Main-frame navigation | container cleared; the new document gets a new default on first request |
| Frame or document destroyed | never returned once detached; the weak entry goes away with the document |
| A sheet removed from the document | never returned; a new default is created on request |

**Tests:**
- **`blink_unittests` (`inspector_css_agent_test.cc`).** `DefaultCreatedBeforeForcedSheets`, `ForcedSheetCreatedBeforeDefault`, `MultipleForcedSheetsBeforeDefault`, `MultipleForcedSheetsAfterDefault`, `ForcedSheetNeverBecomesDefault`, `DefaultIsReusedAcrossReconnects` (three reconnects; no sheet created; text byte-identical), `NewRulesAppendToCanonicalDefault`, `MainFrameNavigationForgetsTheDefault`, and `InspectorCSSAgentSubframeTest.DefaultOfDestroyedFrameIsReleased` (the weak entry is gone after GC). Against the previous Blink, the two reconnect tests fail; the same-session ordering tests pass on both, because that half was the frontend's.
- **Web test.** `inspector-protocol/css/css-inspector-stylesheet-default-identity.js`: a forced sheet first, three reattaches with no new sheet created, an append, then a reload.
- **Frontend.** `core/sdk/CSSModel.test.ts`: asks the backend despite an earlier forced header; asks again once the remembered header is removed; returns `null` on failure.
- **Raw CDP.** `fixtures/measure-inspector-stylesheet-default.mjs`: the 13 required cases, against a built Avora.
- **Real DevTools frontend.** `fixtures/verify-inspector-default-stylesheet-devtools.mjs`: a forced sheet first, concurrent requests, and three reopened sessions over remote debugging.

The overlay carries `CSSModel.ts` and `CSSModel.test.ts` in full, so `copy-sources.sh` reapplies them. Recheck them against upstream on every DevTools roll.

## Known, not changed here

- **Report order on reconnect.** `InspectorCSSAgent::SetActiveStyleSheets` still reports newly bound sheets in hash order. Since 2A.2 nothing depends on that order: the frontend asks for the default explicitly. Making the order deterministic is optional and is not done.
