# Style Studio — Checkpoint 2B: manual CSS preview

Date: 2026-10-17. Base: `738d735` (Checkpoint 2A.3).

The Studio tab of the Elements → Intelligence pane now has a **Preview** section. It applies temporary CSS through the 2A engine, with crash recovery through a persisted ownership ledger. There's no AI generation, refinement or export yet.

## 1. User workflow

1. **Choose a target** with the existing Studio targeting: the picker button, the Elements selection, or a typed selector with **Selected only** or **All matches**. There is no second selection system.
2. **Type declarations** in **CSS declarations** (`textarea`, labelled, with a hint). Editing the draft never touches the page.
3. **Apply preview.** It's disabled until there is a valid target and a non-empty draft. Ctrl+Enter or Cmd+Enter does the same.
4. **Edit and update.** The status says "Draft changes not applied" while the page still shows the earlier preview. The button becomes **Update preview**, which replaces the preview whole.
5. **Revert** removes the preview. The draft stays in the editor.

**What is applied is shown apart from the draft.** "Applied to the page" shows the browser's serialisation of the applied declarations, its scope (selected element only, or every match), and any declarations ignored as invalid. Under it, each declaration's measured effect on the selected element: *takes effect · computed …*, *overridden by the cascade*, or *effect unknown*.

**States and messages:**

| State | Message (summary) | How it is decided |
| --- | --- | --- |
| No preview | "No preview applied." | engine state `none`, empty draft |
| Draft not applied | "Draft not applied yet. The page is unchanged." / "Draft changes not applied…" | draft differs from the last applied draft |
| Applied / updated | "Preview applied." / "Preview updated." plus the measured effect | apply succeeded, then `CSS.getMatchedStylesForNode` per declaration. Insertion alone never counts as an effect. |
| Overridden by the cascade | "…N of M declarations are overridden by the cascade on the selected element." | `CSSMatchedStyles.propertyState` is not ACTIVE |
| Effect not measurable | "…could not be measured." | All matches without the selected element among them |
| Reverted | "Preview reverted. The page's original styles are back." | revert confirmed by read-back |
| Invalid CSS | "Invalid CSS… Nothing was applied, and your draft is kept." | the browser's parser rejects the draft; nothing is sent |
| Target changed | "The target or scope changed, so the preview was removed…" | selection, selector text, scope or source changed |
| Target detached | "The previewed element left the page… or stopped matching its selector…" | removal, replacement, move, class or id change (engine re-check) |
| Navigated | "The page or frame navigated, so the preview was removed with the old document." | the document was replaced, or Studio's sheet was removed with it |
| Failed | "The preview was not applied. *reason* Your draft is kept." | any engine failure: detached, ownership, record not saved, superseded, write failed |
| Conflict | "Studio's preview stylesheet was changed elsewhere…" | engine `conflict`; only Revert is allowed |

**Safety:**
- **No duplicate submissions.** Apply is disabled while an operation runs, and the pane ignores a second request: a double click produced one engine call.
- **Validated at every write.** Target, document, frame and scope are checked before each write: the 2A.3 fail-closed selector, two resolutions, epoch checks, and ownership checks.
- **Scope is never widened silently.** Changing scope removes the preview and asks for a new Apply.
- **Unsupported targets are refused.** Shadow DOM and pseudo-element targets are rejected by targeting, so Apply stays disabled.

**Accessibility.** Native `textarea`, radios and `devtools-button`s are all reachable by keyboard. The editor has a `<label for>`; status lines are `role="status"` with `aria-live="polite"`; the effects list is labelled.

## 2. Crash recovery: storage and ownership

**Storage investigation:**

| Store | Lifetime | Verdict |
| --- | --- | --- |
| `Common.Settings` session storage | dies with the DevTools window | no: lost on a crash |
| Frontend `localStorage` (docked) | profile, per DevTools origin | possible, but not what DevTools uses for host-backed settings |
| **Host preferences** (`InspectorFrontendHost.setPreference` / `getPreference`) | docked and undocked DevTools: the browser process's profile prefs, in the **non-synced** `devtools.preferences` dictionary (`DevToolsSettings::Set`). Written when the message arrives, before the frontend can crash; a read after a write sees it. | **chosen** |
| Same API from a remote-debugging frontend | `InspectorFrontendHostStub`: that frontend origin's `localStorage` | works within that frontend kind only (see limitations) |

**Ledger** (`StyleStudioLedger.ts`):
- **Key.** One preference per page target, `avora-style-studio-ledger:<targetId>`. The `targetId` comes from `Target.getTargetInfo`, never `SDK.Target.id()`, which is `main` in a remote frontend.
- **Entry.** `{sheetId, frameId, documentBackendNodeId, texts, updatedAt}`.
- **Writes.** Serialised; each is read back before it counts.
- **Record before visible.** Before any write that would put preview text on the page, the engine records the sheet with both the current and the incoming text. If that record can't be confirmed, nothing is written (`record-failed`). After the write, the record is narrowed to the new text.
- **Forget.** Only after the sheet is confirmed empty, or gone with its document. A close release (`pagehide`) can't be confirmed, so the record stays; a later session finds the sheet empty and drops it without writing.
- **Prune.** Records older than 30 days are dropped unread.

**Finding orphans** (`scanForOrphans`) runs when the pane is created or shown, when CSS is enabled, and when an inspector sheet is added. It only looks at targets whose CSS model is enabled, so the header list is complete. Each entry is classified (`classifyEntry`):

| Verdict | Condition | Action |
| --- | --- | --- |
| `orphan` | header reported, inspector origin, same frame, `ownerNode` = recorded document, not a known default, not owned by the live engine, text one of the recorded texts | show the banner: **Remove** / **Keep** |
| `changed` | as above, but the text is not what Studio wrote | show "changed by something else… reloading the page clears it": **Forget** (drops the record; the sheet is not written) |
| `gone` / `clean` / `mismatch` | not reported; marker-only or empty; or not the recorded sheet (another frame or document, the default, not an inspector sheet) | drop the record without writing |
| `live` / `unknown` | owned by this session's engine; text unreadable | leave |

**Identity rests on the id.** It is never decided by CSS text, URL or creation order. Text only confirms that nothing else wrote to the sheet.

**Remove** checks the entry again, writes the marker into exactly that sheet, reads it back, and only then forgets the record. **Keep** leaves the sheet and its record untouched; it isn't offered again in this session, but a later session asks again.

## 3. Lifecycle

| Event | Behaviour | Evidence |
| --- | --- | --- |
| Selected element changes | preview reverted; "target or scope changed"; draft kept | live |
| Selector text changes | preview reverted; message | live |
| Selected only → All matches | preview reverted; Apply needed again; then every match styled | live |
| Page reload | preview gone with the document; "navigated"; draft kept; record dropped | live |
| Frame destroyed or navigated | engine drops the sheet; "navigated" | live (engine verifier) |
| Target removed | reverted; "left the page" | live |
| DevTools closed normally | best-effort release; the reopened session drops the clean record and shows nothing | live |
| DevTools killed (renderer crash, window closed) | preview stays; the reopened docked DevTools shows the banner for Studio's sheet only | live |
| Apply superseded or cancelled | nothing left on either element; failure message | live, unit |

## 4. Tests

**Unit tests** (`npm test -- --target=Default front_end/panels/style_intelligence`, on a fresh bundle): **328/328**.
- `StyleStudioLedger.test.ts`, new: store, read-back failures, per-target keys, pruning, concurrency, and every verdict.
- `StyleStudioView.test.ts`, new: every status, `canSubmitPreview`, and the rendered controls (label, live region, Apply via button and Ctrl/Cmd+Enter only when allowed, Update label, Revert disabled, recovery buttons).
- `StyleStudioPreview.test.ts`: ownership record before the write, both texts recorded while a write is in flight, no write when the record fails, forget only after a confirmed empty, forget on removal, kept on close, and a failed revert keeps the preview revertable.

`CSSModel.test.ts`: 13/13.

**Live** (fresh `npm run build`):

| Verifier | Result |
| --- | --- |
| `verify-style-studio-editor.mjs` (new; real UI, real picker, keyboard typing) | 29/29 |
| `verify-style-studio-preview.mjs` | 58/58 |
| `verify-style-studio-targeting.mjs` | 46/46 |
| `measure-studio-orphan-ledger.mjs` | 14/14 |
| `repro-inspector-stylesheet-text-slot.mjs` / `measure-inspector-stylesheet-default.mjs` / `verify-inspector-default-stylesheet-devtools.mjs` | 19/19, 13/13, 7/7 |

**`verify-style-studio-editor.mjs` covers:**
- the full workflow: pick, type, Apply, check computed style, edit, Ctrl+Enter update, Revert, original computed styles back;
- the ownership record persisting with the browser target id;
- invalid CSS;
- a cascade override (page `!important`);
- Selected only vs All matches;
- selector and element changes;
- target removal;
- a double click and a stale apply;
- reload;
- a normal close and reopen;
- DevTools killed with `Page.crash` and its window closed, then reopened with `Target.openDevTools`: the banner shows Studio's sheet only (not the default with the user's rule, nor another feature's forced sheet with Studio-like text); Keep; asked again next session; Remove, then computed style restored and the record deleted.

**Bugs the new tests found and fixed:**
- A revert retried a failed write in the same call.
- A failed revert reported `none` while the preview was still on the page. It now stays `applied`, so Revert can be tried again.

## 5. Limitations

- **Recovery is per frontend kind.** Docked and undocked DevTools share the profile's host preferences. A remote-debugging frontend keeps its own records in its origin's `localStorage`. A preview left by one kind is not offered by the other; it lasts until its document goes away.
- **The banner appears in the Studio tab only.**
- **Effects are measured on the selected element only.** With All matches, when the selected element is not a match, the effect is reported as unmeasured.
- **One-frame leak (accepted, 2A.3).** An element identical in tag, `id` and `class` that takes the target's exact place is styled for one frame, until the re-check.
- **Not covered:**
  - shadow-tree and pseudo-element targets (refused);
  - cross-origin iframes (out-of-process targets are scanned for orphans, but the editor was not tested there);
  - visual-logging (`jslog`) contexts on the new controls.
- **Remove and Forget only clear the sheet or drop the record.** When the sheet is gone or no longer the recorded one, reloading the page is the remedy.
