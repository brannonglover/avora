# Style Intelligence architecture

Style Intelligence is an Elements sidebar explanation of the selected element. Chromium measurements are the source of truth. Inferences explain those measurements and never replace them. An incomplete explanation is preferable to a confident wrong one.

Analysis produces structured facts. Presentation turns those structures into short developer-facing language. The pane does not store preformatted explanation strings as the model.

## Module ownership

The feature lives in `src/third_party/devtools-frontend/src/front_end/panels/style_intelligence/`.

| Piece | Role |
| --- | --- |
| `style_intelligence-meta.ts` | Registers the Elements sidebar view `elements.style-intelligence` (“Intelligence”, order 2, permanent). |
| `StyleIntelligencePane.ts` | Selected-node tracking, loading states, Highlight / Inspect / Source, and rendering. |
| `StyleIntelligenceModel.ts` | Node-scoped reads from CSSModel and DOMModel. No document snapshot. |
| `ElementSummary.ts` | Identity, measured geometry, layout statements, directly used custom properties. |
| `LayoutExplanation.ts` | Structured width, height, X, and Y explanations. |
| `SpacingExplanation.ts` | Measured border-box gaps and their contributors. |
| `Diagnostics.ts` | Contextual behavior explanations placed next to a layout concept. |
| `Presentation.ts` | The default scan. It does not change analysis results. |

Registration, and only registration, touches these existing DevTools files:

- `front_end/entrypoints/devtools_app/devtools_app.ts` imports the meta module. `ElementsPanel.ts` is not modified.
- `front_end/entrypoints/devtools_app/BUILD.gn` depends on `panels/style_intelligence:meta`.
- `front_end/BUILD.gn` includes the module’s unit tests.
- `front_end/panels/elements/BUILD.gn` exposes the elements bundle to this module so existing `CSSRuleValidator` hints can be reused.
- `front_end/models/computed_style/BUILD.gn` and `front_end/models/bindings/BUILD.gn` allow this module to use `ComputedStyleModel` and `CSSWorkspaceBinding`.
- `config/gni/devtools_grd_files.gni` lists the module’s scripts and stylesheet.

Phase 1 does not change Blink or the DevTools protocol.

## Data flow from the selected DOMNode

1. The pane listens for flavor changes of `SDK.DOMModel.DOMNode`.
2. It assigns that node to a pane-owned `ComputedStyleModel`.
3. `StyleIntelligenceModel.describe(node, onPrimary)` reads, for that node only:
   - `CSS.getComputedStyleForNode`
   - `CSSModel.cachedMatchedCascadeForNode` (`CSS.getMatchedStylesForNode`, including `parentLayoutNodeId`)
   - `DOM.getBoxModel`
4. The layout parent, when `parentLayoutNodeId` is present, is resolved with `DOMModel.nodeForId` in the same document. Its computed style, matched styles, and box are read the same way.
5. `buildSummary` and `explainLayout` run on those facts. Declaration diagnostics (percentage height, `CSSRuleValidator`, animation override, cascade override) run before any sibling walk.
6. `onPrimary` delivers that result with empty spacing so the pane can paint identity, size, and position first.
7. `collectSpacing` and the flex free-space diagnostic then run. The final result replaces the partial one only if it is still the current request.

`describe` returns `unavailable` when the node is not an element, has no id, has no CSS model, or computed style cannot be read. It does not call `DOMSnapshot.captureSnapshot`, walk stylesheets for the page, or sweep viewport sizes.

`parentLayoutNodeId` is the flat-tree layout parent (`LayoutTreeBuilderTraversal::LayoutParent`). It skips `display: contents`. It is not the CSS containing block of an absolute or fixed box.

## ComputedStyleModel lifecycle

The pane owns one `ComputedStyleModel` and points it at the selected node. It listens to that model’s existing events:

- `CSS_MODEL_CHANGED` for stylesheet, font, media-query, and pseudo-state changes that the model already treats as relevant.
- `COMPUTED_STYLE_CHANGED` when computed style updates for this node.

Those events are the invalidation source. Style Intelligence does not add document-wide invalidation. A style event reloads after a 50ms debounce. A selection change reloads immediately and cancels the debounce.

Each load increments a request id. A result whose id is no longer current is dropped, so a slower read cannot overwrite a newer selection.

While a new element is loading, the pane clears the previous explanation and shows the new identity. `aria-busy` is set. “Select an element.” is only for the case with no element. A refresh of the same node keeps the previous summary on screen until the new one arrives.

`wasShown` reloads. `willHide` and `onDetach` clear any temporary highlight.

## Measured and inferred information

`EvidenceKind` is `measured` or `inferred`.

Measured facts come from computed style, the box model, or an active author declaration. Used width, height, and viewport position are measured. Border-box geometry is what the header shows.

Inferred facts are high-confidence readings of those measurements. The only layout-role inference in Phase 1 is “flex item” or “grid item”, and only when all of these hold:

- the layout parent’s computed display is flex or grid
- the selected element’s position is `static`, `relative`, or `sticky`
- the selected element’s display is not `none` or `contents`

If the parent display or the position was not measured, the role is not inferred. Inference never invents a pixel size or a computed value.

## Structured layout explanation

`explainLayout` returns one `LayoutExplanation`: a list of axis records, not a paragraph.

Each `AxisExplanation` has:

- `axis`: `width`, `height`, `x`, or `y`
- `usedValue`: measured border-box size, or the border-box origin in viewport coordinates
- `mechanism`: how that used value is explained
- `confidence`: `high` or `limited`
- `controller`: the node and declaration that control the axis, when one can be named
- `contributions`: author declarations that specify, contribute to, constrain, or fail to affect the axis
- flags for padding and border included in the used size, for an author length that matches the content box rather than the border box, and for an unnamed containing block

Mechanisms include explicit length, min and max size, percentage, viewport, intrinsic, block available space, content, flex layout, flex stretch, grid layout, grid stretch, inset-derived size, block flow, flex placement, grid placement, relative, absolute, fixed, sticky, auto margins, and unknown.

`unknown` is a result. It means the used value was measured and the controller was not identified.

## Contribution roles

Every contribution is a measured declaration plus a role:

| Role | Meaning |
| --- | --- |
| `specifies` | The author value is the used size on that axis. |
| `contributes` | The declaration participates. It does not by itself claim the resulting pixel size. `flex: 1` is a contribution to a flex-layout size. |
| `constrains` | A min or max is what disagrees with the specified size and matches the used size. |
| `ineffective` | The declaration is present and does not determine the used value. A percentage height against an auto parent that cannot be shown to be definite is the Phase 1 case. |

## Spacing model

`explainSpacing` returns relationships and declines. A relationship is a measured border-box distance plus the contributors that can be shown to participate. Contributors are not forced to sum to the distance. The difference is `unexplained`.

A relationship names its axis (`horizontal` or `vertical`) and its kind (`previous-sibling`, `next-sibling`, `parent-start`, `parent-end`). Contributors are `gap`, `margin`, or `padding`, each with a used pixel value. A limited-confidence contributor can remain visible and still not reduce `unexplained`.

Sibling identity is geometric. The neighbor is the nearest in-flow, untransformed box that shares the cross axis. DOM order is not visual order. `flex order` and grid placement are handled by that rule. Grid cells that only meet at a corner share no axis, so they get no invented sibling gap.

Declined relationships stay in the model and are omitted from the pane. Reasons: out of flow, transformed, transformed sibling, out-of-flow box between neighbors, incomplete sibling set, ambiguous neighbor, and unknown position. A layout parent with more than 24 element children declines spacing for that parent rather than guessing from a partial set.

Block vertical margins collapse. Computed style exposes used margin pixels, not the shared collapsed contribution, so those relationships set `marginCollapseUnknown` and do not list the authored margins as used pixels. Flex and grid margins do not collapse and can be counted. The far edge of a narrow box is omitted when the unexplained remainder is larger than the contributors that were counted: that distance is unused containing-block space, not spacing.

## Diagnostic model

Diagnostics explain behavior next to a property or layout concept. They are not a CSS linter and they do not use error or warning severity unless Chromium already supplied one.

Categories are `no-visible-effect`, `constrained-by`, `does-not-apply`, and `potential-issue`.

Kinds:

- `percentage-height`, when a percentage height is on the axis as an ineffective contribution
- `zero-free-space`, when the selected element is a nowrap flex container and measured free space on the main axis is zero
- `validator`, from the existing `CSSRuleValidator` (`z-index` on an inapplicable position is included; stacking-context analysis is not)
- `animation-override`, when an animation overrides the declaration
- `cascade-override`, when another author declaration wins

`placement` attaches a diagnostic to `width`, `height`, `x`, `y`, `layout`, or `spacing`. Anything that cannot be attached is listed under Potential issues. The validator’s “Try setting…” hint is not shown.

## Presentation layer

`presentPane` builds the scan from the summary, layout explanation, spacing explanation, and diagnostics. The structured model stays detailed. The default view does not show every supporting declaration, confidence field, or measurement.

The order is identity, measured size (`width × height`), then Layout, Size, Position, Spacing, Tokens, and Potential issues. Empty sections are omitted.

Secondary evidence goes under Details: the viewport offset, the unnamed-containing-block caveat, and an authored value that differs from the used pixels. Margin-collapse and unexplained-remainder sentences stay visible, because they are the explanation rather than extra evidence.

Presentation may hide a cascade-override longhand of `flex`, `margin`, `padding`, or `gap` when the shorthand is also a cascade override. That filter is display-only. The model still produces the longhands.

Copy is produced by `presentAxis`, `presentSpacing`, and `presentDiagnostic` at render time.

## Highlight, Inspect, and Source

All three actions use the same controls wherever they appear.

- **Highlight** calls `OverlayModel.highlightInOverlay` for a node that is not the selection. Hover and focus show it. Mouseleave and blur hide it. It does not change the selected node.
- **Inspect** hides the highlight, then `Revealer.reveal`s the node so Elements selects it and Intelligence reloads for that node.
- **Source** resolves the declaration through `CSSWorkspaceBinding.propertyUILocation` and reveals that UI location. It is shown only when the declaration has an author range. Inline styles do not. A longhand that came from a shorthand prefers the shorthand range, so `flex: 1` can open the `flex` declaration.

Within one pane, Highlight and Inspect for a given node id appear once, and Source for a given declaration id appears once. The text that names the node or declaration can still appear on each row.

A style refresh does not clear a highlight whose node still exists. If the node can no longer be resolved, the highlight is hidden. Leaving the sidebar, detaching the widget, changing selection, and Inspect also hide it.

## Cache and invalidation

There is no separate Style Intelligence cache. Reads go through `CSSModel.cachedMatchedCascadeForNode` and `ComputedStyleModel`, which already scope updates to the selected node, its siblings, and its ancestors for DOM mutations, and to frame resize for viewport changes.

A class change, parent class change, inline style edit, stylesheet edit, flex or grid property change, pseudo-state change, or removal of the selected node arrives as one of those existing events and schedules a new `describe`. Node-scoped invalidation is sufficient. Phase 1 does not invalidate the whole document to refresh one element.

The live page used to verify this behavior is `docs/style-intelligence/fixtures/phase1.html`. The regression suite is the `*.test.ts` files in the module.
