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

## Known, not changed here

- **Report order on reconnect.** `InspectorCSSAgent::SetActiveStyleSheets` reports newly bound sheets by iterating a `HeapHashSet`, so a reconnecting session receives inspector sheets in no particular order. The frontend's `CSSModel.requestViaInspectorStylesheet` (the Styles pane's "New Style Rule") takes the first one reported, so after a reconnect it can pick any inspector sheet rather than the default.
  - With this patch nothing is lost either way: every sheet keeps its own text.
  - Making that order follow the active stylesheet order (iterating `all_sheets_vector` instead of the set) is a separate one-line change, not made here.
- **Default sheet per session.** `default_inspector_stylesheets_` is per CSS agent, so a reconnecting session's `createStyleSheet(force: false)` makes a new default sheet instead of returning the existing one. Unchanged.
