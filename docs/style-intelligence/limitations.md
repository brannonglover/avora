# Style Intelligence known limitations

These are limits of the Phase 1 data and of what the live pass established. They are not the correctness declines in `correctness.md`. A limitation is not automatically a bug, and closing one is not permission to weaken a decline.

## HTML presentational attributes

Style Intelligence explains CSS. An HTML attribute such as the `width` on Hacker News’ main table is not a declaration the model can see. The used size is still measured. The controller stays unidentified.

## Unnamed containing blocks

Phase 1 still does not treat `parentLayoutNodeId` as a containing block. When `CSS.getLayoutRelationshipsForNode` returns a relationship, the Position section names an element, the viewport, or an anonymous layout container. A missing command leaves the Phase 1 caveat. Insets can still be listed when they participate. See `correctness.md`.

## Non-horizontal writing modes

Gap attribution runs for `writing-mode: horizontal-tb` and for a missing writing mode. Other writing modes do not receive a gap contributor. The measured distance can still be declined or left unexplained rather than assigned to the wrong axis.

## Anonymous and pseudo boxes

Sibling search uses element boxes from the layout parent’s element children. Anonymous boxes and pseudo-elements are not siblings in this pass. A parent inset caused only by a pseudo, with no padding or margin on the elements involved, is omitted.

## Source navigation

Source is available when the declaration is a regular stylesheet rule with a stylesheet id and a value range. Inline style declarations have no range, so they have no Source action. Constructed and adopted stylesheets that do have ranges do show Source.

When a longhand is implicit in a shorthand, Source opens the shorthand. A longhand that was authored on its own keeps its own range.

## Cross-document highlight

Highlight and Inspect resolve the target with `nodeForId` on the selected node’s `DOMModel`. A node id that belongs to another document is not highlighted. The action hides the overlay instead of pointing at the wrong box.

## Layering

Layering explains the selected element’s stacking context, why that context exists, and when a sibling context is ordered later. It does not detect overlap, hit-test the point under the cursor, or say which element covers the selected box.

An internal context is one Blink reports and the CSS predicate cannot name. The section says so and does not guess a property. The live fixture did not contain one of these; the unit test covers the contract.

Highlight draws the existing overlay on the boundary node. There are no connector lines. Iframe and shadow chains are covered by unit tests. The Phase 1 live pass did not establish those rows as selectable, and the Phase 2A live pass selected elements in the main document.

## Inferred scrollport row identification

When Blink names the sticky scroll containers, presentation drops Phase 1's inferred nearest-scrollport row. `isInferredScrollportControl` identifies that row by its label text, because the Phase 1 control no longer carries a node id once Highlight has been consumed. This is string-based coupling to Phase 1 copy and is known technical debt. It is covered by unit tests. Changing it means changing Phase 1 presentation or model contracts, which is outside the Phase 2B scope that closed this work.

## Shadow and iframe selection

The model follows whatever element DevTools has selected, including a node whose `DOMModel` is an iframe or a shadow tree, once that node is the selection.

The Phase 1 live pass inserted an open shadow host and an iframe into the fixture page. After expanding the Elements tree, the inner nodes were not exposed as selectable rows, so shadow and iframe selection were not established on a running build. That is a gap in the live evidence, not a tested failure of the explanation rules.
