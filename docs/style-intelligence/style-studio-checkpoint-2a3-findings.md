# Style Studio — Checkpoint 2A.3: preview safety closure

Date: 2026-10-17. Base: `99974d8` (Checkpoint 2A).

| Question | Answer |
| --- | --- |
| Can transient target leakage be eliminated with existing mechanisms? | **No.** It is now **bounded**: sibling insertion, removal, and different-element reorders fail closed (nothing is styled). Only an element identical in tag (at every level), `id`, and `class` that takes exactly the target's place can be styled, until Studio's re-check reverts it. |
| Can crash-orphan ownership be recovered reliably? | **Yes, with a frontend ledger plus checks**, in every case measured. Stylesheet ids are stable across reconnects and never reissued within a renderer process. A new process gets a new id prefix. The one theoretical gap (an OS process id reused within one browser session) is closed by the text and owner checks and by user confirmation. No browser-side mechanism is required. |

No Blink or protocol changes. No sweeping or deletion was added.

---

## 1. Transient target leakage

### What leaked before

Selected-only previews used `:where(:root > :nth-child(i) > … > :nth-child(k)):is(<authoring selector>)`. Between a DOM change and Studio's re-check, which takes one protocol round trip and spans one or more painted frames, the rule matched **whichever element now stood at those positions**, if it also matched the authoring selector. A sibling inserted before a `.card` target, for example, was styled until the re-check rewrote the rule.

### Mechanisms investigated

The goal was to style one DOM element without mutating the DOM, adding an identifying class or attribute, changing authored stylesheets, or adding a protocol command.

| Mechanism | Per node? | Why it doesn't work |
| --- | --- | --- |
| `CSS.forcePseudoState` | yes | It can only force real pseudo-classes: hover, focus, focus-within, focus-visible, target, target-current, active, visited, link, enabled, disabled, valid, invalid, user-valid, user-invalid, required, optional, read-only, read-write, in-range, out-of-range, checked, indeterminate, placeholder-shown, autofill, open (`inspector_css_agent.cc`). Each one also makes the page's own and the UA's rules for that state match. Some propagate to ancestors (`:focus-within`, `:has()`). It also takes over the Styles pane's `:hov` toggles for that node. |
| `CSS.forceStartingStyle` | yes | It applies **all** `@starting-style` rules to the node, including the page's. A rule inside `@starting-style` still needs a selector, and would also give starting styles, and so transitions, to newly inserted elements that match it. |
| `CSS.setStyleTexts`, `setEffectivePropertyValueForNode` | yes | Write the inline `style` attribute (a DOM mutation) or the page's matched rules. |
| `CSS.addRule`, `setRuleSelector`, `@scope`, `:nth-child(… of S)` | no | Selector-based, so no better than a structural selector. |
| Overlay, highlight, `DOM.setInspectedNode` | n/a | No styling. |

Nothing in the protocol or Blink attaches a style to a node identity without a DOM attribute. Eliminating the leak would need a new Blink feature: an element-keyed, inspector-only declaration block applied at author origin with a chosen specificity, exposed by a new protocol command. That's a broad change, and is not proposed for this checkpoint.

### Implemented: a selector that fails closed

`singleElementSelector(path, identity, authoringSelector)` now produces:

```
:where(:root > tag:nth-child(i):nth-last-child(j) > … > tag:nth-child(k):nth-last-child(l)[id="…"][class="…"]):is(<authoring selector>)
```

How each part works:
- **Both ends at every step.** A step matches only while its parent has exactly `i + j - 1` element children. Inserting or removing an element sibling at any level on the path, before or after, makes the rule match **nothing**.
- **Tag at every step.** A reorder that puts a different element type on the path matches nothing.
- **Exact `id` and `class` on the target** (`:not([id])` / `:not([class])` when absent). A different element reordered into the target's place matches nothing, and so does a class or id change on the target itself.
- **Specificity unchanged.** Everything structural is inside `:where()`, so the rule still has exactly the authoring selector's specificity.
- **Text and comment nodes** don't count. `:nth-child` counts elements only.

The re-check after each DOM change is unchanged. It rewrites the rule for the same node, which brings the preview back after a fail-closed gap, or reverts it.

### Remaining limit (measured)

An element that matches the authoring selector, has the same tag at every step and the same `id` and `class`, and lands at exactly the target's path is still styled until the re-check. That happens with:
- identical siblings swapped (list items without ids, reordered);
- an element replaced by an identical copy (`replaceWith(cloneNode(true))`, common in re-rendering frameworks).

No selector can tell such elements apart. Measured live: the other element was styled for **1 frame, 7–11 ms**, until the re-check. After the re-check the original element was gone or rebound, so the preview was reverted. It is never transferred permanently.

### Recommended policy

- Keep previews **reversible and labelled** as previews. A one-round-trip mis-style is a transient visual only: no page data, DOM, or stylesheet is touched.
- **In 2B,** when a preview is reverted because its target was replaced or moved, show "Target changed — preview removed" rather than failing silently.
- **Prefer All matches** when the user's intent is a class or component. That's plain CSS semantics, so there's nothing to leak.

## 2. Crash-orphan recovery: the ledger

Measured with `fixtures/measure-studio-orphan-ledger.mjs` (raw CDP, 14 checks) and the preview verifier (real frontend).

| Question | Finding |
| --- | --- |
| Header identity | A forced sheet's header has `origin: inspector`, its `frameId`, and `ownerNode` set to its document's backend node id. That holds for child frames too. |
| Ids across reconnects | The same id and the same page target id across three reconnects, abrupt and clean. Every other sheet keeps its id too. |
| Target id in the real frontend | `Target.getTargetInfo` returns the browser's page target id in both the docked frontend and a reopened remote frontend. `SDK.Target.id()` does not: a frontend attached straight to the page's websocket calls it `main`. |
| Same-site reload | Main frame id unchanged; new document backend node id; the recorded sheet is no longer reported. |
| Ids reissued? | Never within a process. `WeakIdentifierMap` is a monotonic counter: 20 new sheets after the reload reused no recorded id. |
| Cross-site navigation (process swap) | Recorded sheet gone; new ids carry a new process prefix (`style-sheet-<unique process id>-<n>`). Frame id and target id unchanged. **The document backend node id restarts in the new process** (1, was 24), so owner-document ids alone are not unique across processes. |
| Renderer crash, then reload | Recorded sheet gone; new process prefix. |
| Frame removal | The child frame's sheet is removed; its entry is stale. |
| Changed by another client | Not a candidate. Its text no longer equals what Studio wrote. |
| Another forced sheet with Studio-like text | Never matched by Studio's entry. Identity rests on the recorded id; the text only confirms. |
| Cleanup confirmed before dropping the entry | Yes. Write the marker, read it back, and (where the element still exists) see it in computed style; only then drop the entry. If the element is gone, read-back alone confirms. |

**Why this is reliable.** For an entry to match the wrong sheet, a live inspector sheet in the same page target would need:
- the same id string, which requires the OS to reuse the renderer's process id within the browser session *and* the counter to reach the same value;
- the same frame;
- an owner document with the same backend node id;
- exactly the text Studio last wrote.

Even then, the user must confirm. Nothing browser-side needs to be added.

### Proposed user-confirmed recovery (Checkpoint 2B)

1. **Record.** When Studio creates a sheet, persist `{targetId, frameId, sheetId, documentBackendNodeId, text}`, where `targetId` comes from `Target.getTargetInfo` (`SDK.Target.id()` is `main` in a frontend attached directly to a page), in a `Common.Settings` setting backed by the host's preferences, which survive a DevTools crash. Update `text` on every write; drop the entry on revert-to-empty, navigation, frame removal, and confirmed close release.
2. **Discard.** At startup, drop entries whose target id is not a current page target. Nothing is written for them.
3. **Find.** When CSS is enabled for a target, an entry is a candidate only if:
   - its id is reported as an inspector sheet;
   - its frame and owner document match;
   - its text equals the recorded text.
   Entries whose sheet is not reported are dropped without writing.
4. **Ask.** Show "A Style Studio preview from an earlier DevTools session is still applied on this page. [Remove] [Keep]".
5. **Remove.** Only on Remove: re-check the candidate, write the marker, read it back, then drop the entry. A candidate whose text no longer matches is reported as "changed elsewhere" and left alone.
6. **Never sweep** automatically, and never touch a sheet without an entry.

## 3. Verification

Fresh builds: the frontend test bundle was rebuilt by `npm test` (no `--skip-ninja`), and Avora by `npm run build`. Blink is unchanged.

| Suite | Result |
| --- | --- |
| `npm test -- --target=Default --chrome-binary=…/Avora front_end/panels/style_intelligence` | 295/295 (8 new selector tests, real DOM) |
| `verify-style-studio-preview.mjs` | 58/58 (6 new: transient gap and target id), plus notes |
| `verify-style-studio-targeting.mjs` | 46/46 |
| `measure-studio-orphan-ledger.mjs` (new) | 14/14 |
| `repro-inspector-stylesheet-text-slot.mjs` / `measure-inspector-stylesheet-default.mjs` / `verify-inspector-default-stylesheet-devtools.mjs` | 19/19, 13/13, 7/7 |
| ESLint on `StyleStudioPreview.ts`, `StyleStudioPreview.test.ts`, `StyleStudioDom.ts` | clean (the test now imports through `style_intelligence.js`) |

**Live transient checks** (computed style read synchronously after each mutation, then frame by frame):

| Mutation while a `.pv-g`-authored preview is on `#pv-g2` | Styled at once | Then |
| --- | --- | --- |
| Sibling inserted before the target | nothing | target again after 8 ms (1 frame) |
| Earlier sibling removed | nothing | target again after 17 ms (2 frames) |
| Different sibling moved into the target's place | nothing | target again after 13 ms (2 frames) |
| Identical twins swapped | the other twin | nothing after 7 ms (1 frame); reverted |
| Target replaced by an identical copy | the copy | nothing after 11 ms (1 frame); reverted |

**Unit tests** (`StyleStudioPreview.test.ts`, `single-element selector in a real cascade`). The selector:
- matches exactly the target;
- keeps the authoring selector's specificity;
- matches nothing after a sibling is inserted before or after the target, or an earlier or later sibling is removed;
- matches nothing after an ancestor gains or loses a sibling;
- ignores text and comment nodes;
- matches nothing after a different sibling is reordered into the target's place;
- matches nothing after the target's class or id changes;
- escapes attribute values exactly;
- and still matches an identical copy at the same path. That test pins the known limit.

**Unchanged guarantees, re-verified:**
- Studio writes only its own forced sheets.
- The default and user rules survive Studio operations and a reopen.
- Revert restores computed style.
- Async cancellation is unchanged: the engine tests pass.

**Not tested:** a real OS process-id reuse (not reproducible on demand), and cross-origin iframes.
