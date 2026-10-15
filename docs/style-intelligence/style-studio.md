# Style Studio

Style Studio is the **Studio** tab of the Intelligence pane, next to **Explain**. It is where a developer chooses which element(s) a styling change will target. Checkpoint 0 (`style-studio-checkpoint-0-audit.md`) recorded the architecture audit. This file holds the contract for each checkpoint as it lands.

## Checkpoint 1A — element targeting

Targeting only. Nothing in this checkpoint generates CSS, creates or edits a stylesheet, previews, exports, or calls an AI provider. There are no Blink or DevTools protocol changes.

### Modules

All modules live in `src/third_party/devtools-frontend/src/front_end/panels/style_intelligence/`.

| Module | Role |
| --- | --- |
| `StyleStudioTarget.ts` | Pure model: `StudioNode`, `StudioSource`, `StudioScope`, `SelectorResolution`, `StudioTarget`, and `deriveTarget(state)`. |
| `StyleStudioTargeting.ts` | Controller. Holds the state, follows the selection it is given, debounces selector queries, and drops stale responses. It depends only on the `StudioSelection` port, not on the SDK. |
| `StyleStudioPicker.ts` | `StudioPickTracker`: decides when a pick started from Studio has completed. Pure; it observes the existing picker and never runs an inspect mode. |
| `StyleStudioDom.ts` | SDK adapter. `studioNodeFor`, `selectionForNode` (read-only `DOM.querySelectorAll` in the node's own document, DOM-change observation, and a selector suggestion: `readableSelector` first, then `DOMPath.cssPath`), and `bindStudioToSelection`, which reads the `UI.Context` flavor and never sets it. |
| `StyleStudioView.ts` | Lit rendering of the Studio tab (`renderStudioPanel`): the followed node, then the Targeting section and its status wording. |
| `StyleIntelligencePane.ts` | Renders the **Explain** / **Studio** tab strip (a WAI-ARIA tablist; arrow keys, Home, and End move between tabs). The selected tab persists in the `style-intelligence-tab` setting. The pane owns one `StyleStudioTargeting`, binds it to `SDK.DOMModel.DOMNode`, and draws the overlay on hover. Explain holds the existing explanation; `describe()`, Layering, relationships, and diagnostics are unchanged and keep loading while Studio is open, so switching back is instant. |

### Identity

- A **node target** is a DOM node id inside a `documentKey` (`<target id>:<document node id>`) with its `frameId`. It is never re-identified by a selector.
- A **selector target** is the selector text, plus the node ids it matched, plus the document it was resolved in.
- A resolution counts only for the exact current selector in the current node's document. Anything else is `pending` and gets queried again. A navigation replaces the document node, so the key changes and old matches cannot be reused.

### Source and scope

- Source: **Selected element** (the Elements selection) or **Selector**.
- Scope: **Selected only** or **All matches**. Changing the source always resets the scope to one node, so the target is only ever widened to all matches by an explicit click.
- With one-node scope and a selector, the target is the selected element and only when the selector matches it. A selector that matches exactly one *other* element is reported as `node-not-matched`; it is not substituted for the selection.
- Switching to Selector with an empty input pre-fills a readable selector for the node: `#id` when it has one, otherwise its tag and up to three classes (`div.card.shared`). Only an element with neither falls back to `DOMPath.cssPath(node, true)`. The status then shows how many elements that selector actually matches; it is not assumed to be unique.

### Picking on the page

- The picker button next to **Target** runs the existing `elements.toggle-element-search` action, which is the same inspect mode as the Elements toolbar button and Cmd/Ctrl+Shift+C. There is no second inspect-mode controller. The button shows that action's pressed state, and a hint says to press Esc to cancel.
- A picked element becomes the Elements selection through the normal inspect path, and Studio follows the selection as it always does.
- In **Selector** mode, a pick started from Studio's button also replaces the selector with a readable selector for the picked element and resets the scope to one element. Picks started from the toolbar change the selection but never overwrite a typed selector.
- A completed pick is detected from the pick itself, not from a selection change, so picking the element that is already selected works. `StyleStudioDom.observeInspectPicks` adds a listen-only Overlay dispatcher to each page target. The protocol client keeps a list of dispatchers per domain, and OverlayModel's own stays first. It sees `Overlay.inspectNodeRequested`, the event the existing picker completes with. `StudioPickTracker` (`StyleStudioPicker.ts`) then completes the pick as soon as the Elements selection is the picked node (immediately when it already was), mirroring `ElementsPanel.revealAndSelectNode`'s user-agent shadow DOM handling. Inspect mode ending with no pick within 500 ms is a cancel. A pick that never becomes the selection is dropped after 3 s.
- The Studio tab always offers the picker when a page target can be inspected, including when nothing is selected ("No element selected"). The first pick establishes the target and, in Selector mode, the selector.
- Picking an unsupported element (inside a shadow root, for example) selects it as usual. Studio leaves the selector unchanged and the status gives the reason it cannot be targeted.
- Esc, or clicking the button again, cancels without changing the selection or the selector. Clicks made while picking are consumed by inspect mode and never reach the page.

### Synchronization

- The target follows the Elements selection through `UI.Context` flavor changes. Studio never sets a flavor, never reveals a node, and never changes the selection.
- Moving the selection within the same document keeps the current matches; moving to another document (an iframe or a navigation) drops them and queries again.
- DOM mutations in the node's document (insert, remove, attribute change) and document replacement mark the matches pending and query again after 250 ms. Typing waits 150 ms; Enter queries immediately.
- Hovering the status draws the existing overlay: the node, or all matches through the overlay's own selector highlight. Blink resolves that highlight in the owner document of the first match, which is the same document the matches came from.

### Status and declines

| Target | Status |
| --- | --- |
| No selection | Select an element in the Elements tree. |
| Text, comment, document node | Not an element; only element nodes can be targeted. |
| Pseudo-element node | Cannot be targeted as a node; select the generating element. |
| Element inside a shadow root | Not supported yet. Inspector stylesheets are not applied to shadow-tree scopes (`style_sheet_collection.cc`, `if (!is_shadow_tree_)`). |
| Empty selector | Enter a CSS selector. |
| Invalid selector | `Invalid selector: <protocol error>`. In this build Blink returns `DOM Error while querying` for any syntax error. |
| Zero matches | No elements in this document match this selector. |
| One-node scope, selection not matched | The selector matches N elements, but not the selected one. |

A shadow-tree selection still provides its owner document as context, so **All matches** works for a document selector while a shadow node is selected. The matches never include shadow-tree elements, because `querySelectorAll` on a document does not pierce shadow roots.

### Not supported in 1A

- Shadow DOM targets (declined as above), including `::part()` and `:host` workflows.
- Pseudo-element targets as nodes.
- Selectors that cross documents. Each iframe is its own document; select a node inside the frame to resolve there. Out-of-process iframes resolve through their own target's DOM model. That follows from the API but was not exercised live; the fixture iframe is same-origin `srcdoc`.
- Picking one specific match from a selector's match list. One-node scope always means the Elements selection.
- A precise syntax error position: Blink's protocol message is generic.
- Studio state is per pane instance and is not persisted across DevTools sessions.
- Studio's pick observer stays registered on a target for that target's lifetime. The protocol client has no public way to remove an Overlay dispatcher, and the observer does nothing unless a pick was started from Studio.

## Verification

### Unit tests

`StyleStudioTarget.test.ts` covers the pure derivation, and `StyleStudioTargeting.test.ts` covers the controller wired to a real `UI.Context` instance with fake DOM documents. Run with the rest of the module:

```bash
cd chromium/src/third_party/devtools-frontend/src
PATH="$PATH:$HOME/depot_tools" npm test -- --target=Default \
  --chrome-binary="$PWD/../../../out/Default/Avora.app/Contents/MacOS/Avora" \
  front_end/panels/style_intelligence
```

`--chrome-binary` is required. Without it, the runner defaults to `out/Default/Chromium.app`, a leftover from before the Avora rebrand that fails in dyld (`Library not loaded: @rpath/libchrome_dll.dylib`). Karma then reports only `Target.setDiscoverTargets: Target closed`. That was the Checkpoint 0 launch failure.

### Live pass

`fixtures/style-studio-targeting.html` contains a unique `#unique`, three `.card.shared` elements, a `::before`, a same-origin iframe, and an open shadow root. It records page DOM mutations. `fixtures/verify-style-studio-targeting.mjs` launches the built Avora with DevTools docked, drives the real Elements selection (`Common.Revealer`) and the Intelligence pane, and fails if any check fails:

```bash
npm run build
chromium/src/third_party/node/mac_arm64/node-darwin-arm64/bin/node \
  docs/style-intelligence/fixtures/verify-style-studio-targeting.mjs /tmp/style-studio-verify
```

Manual steps for the same pass:

1. `npm run build && npm run start`, open `docs/style-intelligence/fixtures/style-studio-targeting.html`, open DevTools → Elements → Intelligence, and choose the **Studio** tab.
2. Select `#unique`: the status reads `div#unique.card · This element only`. Select another card: the status follows.
3. Choose **Selector**: the input is pre-filled and the scope stays **Selected only**.
   Click the picker button next to **Target**, then click a card on the page: the card becomes the selection, the input reads `div.card.shared`, and the status says the selector matches 3 elements. Pick `#unique` the same way to get `#unique`. Press Esc while picking to cancel.
4. Type `.card` with a `.shared` card selected: `This element only · the selector matches 4 elements`. Choose **All matches**: `4 matching elements` (the shadow card is not counted). Hover the status: four cards highlight.
5. Type `#unique` with one-node scope while a `.shared` card is selected: “matches 1 element, but not the selected one”.
6. Type `.nothing-here`: zero matches. Type `a[`: an error in red.
7. Switch to **Selected element** and back: the scope is reset to one node.
8. Select a paragraph inside the iframe, type `p`, and choose **All matches**: `2 matching elements` (the iframe document, not the main one).
9. Select the span inside `#host`'s shadow root: the explicit shadow-root decline.
10. In the page console, `__studioMutationLog` shows only parser entries from load, and `document.styleSheets.length` is still 1.
