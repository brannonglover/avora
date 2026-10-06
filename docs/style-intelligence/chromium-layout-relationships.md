# Chromium patch: CSS.getLayoutRelationshipsForNode

Reapply this patch when the Chromium tree or the DevTools frontend protocol bindings are upgraded. The patch does not change Blink’s containing-block or scroll-container resolution.

## Protocol

`third_party/blink/public/devtools_protocol/domains/CSS.pdl`

Experimental command `CSS.getLayoutRelationshipsForNode` with `nodeId`. The return value is `LayoutRelationships`:

- `containingBlock` — `element` with `nodeId`, `initial-containing-block`, or `anonymous`. Present only for absolute or fixed boxes.
- `scrollContainer` — `element` with `nodeId`, or `document`. `fixedToViewport` is Blink’s bit from `PaintLayer::ContainingScrollContainerLayer`.
- `stickyScrollContainers.horizontal` / `vertical` — the same scroll target shape. Only axes `StickyConstrainedAxes` reports are present. Both are present when they name the same container.

## Agent

`third_party/blink/renderer/core/inspector/inspector_css_agent.h`
`third_party/blink/renderer/core/inspector/inspector_css_agent.cc`

`InspectorCSSAgent::getLayoutRelationshipsForNode` resolves the element, calls `Document::UpdateStyleAndLayoutForNode`, and then:

- absolute → `LayoutObject::ContainerForAbsolutePosition()`, then `InclusiveContainingBlock()`
- fixed → `LayoutObject::ContainerForFixedPosition()`, then `InclusiveContainingBlock()`
- scroll container → `LayoutObject::ContainingScrollContainer()`
- sticky axes → `LayoutBoxModelObject::StickyConstrainedAxes()`, then `LayoutObject::ContainingScrollContainerLayer(PhysicalAxis)`

`ContainingBlockForAbsolutePosition()` and `ContainingBlockForFixedPosition()` are protected, so the agent calls the public container accessor followed by `InclusiveContainingBlock()` — the same two steps those methods take.

`fixedToViewport` is not an out param at the `LayoutObject` level. The agent calls each scroll walk a second time with `ignore_layout_view_for_fixed_pos`, which returns nothing only where Blink set the bit, and reports the difference. Do not reintroduce a copy of Blink's "enclosing box is already the scroll container" shortcut in the agent; that shortcut lives in `LayoutObject::ContainingScrollContainerLayer` and the agent must keep following it rather than restating it.

`LayoutView` is the initial containing block or the document scroll container, with no node id. An anonymous box has no node id. Element ids go through `InspectorDOMAgent::PushNodePathToFrontend`. The method does not call `ScrollParent()` and does not force pre-paint.

These inspector sources live in the Chromium tree. Copy them into `avora/src` at the same relative paths so `build-scripts/copy-sources.sh` reapplies them after a sync. The overlay currently carries both PDLs and all three inspector files.

## Tests

`third_party/blink/renderer/core/inspector/inspector_css_agent_test.cc`

`LayoutRelationshipsFollowBlink` enables `SingleAxisScrollContainers` and checks the protocol node ids against the Blink methods above. `LayoutRelationshipsFollowLegacySingleAxis` disables the feature and checks the legacy adjustment: a clip axis becomes a scroll container, and a single sticky inset constrains both physical axes.

## DevTools frontend protocol copy

The DevTools frontend does not build against Blink's PDL. It keeps its own copy, and `scripts/deps/generate_protocol_resources.py` reads that copy, not `third_party/blink/public/devtools_protocol`. So the same block has to be added twice:

`third_party/devtools-frontend/src/third_party/blink/public/devtools_protocol/domains/CSS.pdl`

Apart from this command, that file differs from Blink's `domains/CSS.pdl` only in two upstream `LINT_SKIP` markers, so the block can be copied across verbatim.

## DevTools frontend bindings

Do not hand-edit these:

- `front_end/generated/protocol.ts`
- `front_end/generated/protocol-proxy-api.d.ts`
- `front_end/generated/protocol-mapping.d.ts`
- `front_end/generated/InspectorBackendCommands.ts`
- `third_party/blink/public/devtools_protocol/browser_protocol.json`

They are generated. Patch the vendored PDL above and then run, from `third_party/devtools-frontend/src`:

```
vpython3 scripts/deps/generate_protocol_resources.py
```

Running the generator against an unpatched vendored PDL silently deletes the command from all of these, which is the failure mode to expect after a Chromium or DevTools roll. Hand-editing them instead is not equivalent: the generator also emits three `inspectorBackend.registerType` entries in `InspectorBackendCommands.ts` for `CSS.LayoutRelationshipTarget`, `CSS.StickyScrollContainers`, and `CSS.LayoutRelationships`, which a hand-edit is likely to miss.

These generated files are deliberately **not** carried in `avora/src`. Overlaying them would let a stale copy overwrite unrelated upstream protocol changes after a roll. Only the two PDL sources are overlaid; regenerate after syncing.

`front_end/panels/style_intelligence/LayoutRelationshipsLoader.ts` calls the generated typed agent rather than casting, so if this patch is lost the DevTools build fails instead of the pane going quiet.

The command name registered at runtime is `CSS.getLayoutRelationshipsForNode`.
