# Style Intelligence known limitations

These are limits of the Phase 1 data and of what the live pass established. They are not the correctness declines in `correctness.md`. A limitation is not automatically a bug, and closing one is not permission to weaken a decline.

## HTML presentational attributes

Style Intelligence explains CSS. An HTML attribute such as the `width` on Hacker News’ main table is not a declaration the model can see. The used size is still measured. The controller stays unidentified.

## Unnamed containing blocks

Absolute and fixed boxes do not get a named containing block, because `parentLayoutNodeId` is not that block. The root element’s containing block is likewise unnamed. Insets can still be listed when they participate. See the corresponding guarantee in `correctness.md`.

## Non-horizontal writing modes

Gap attribution runs for `writing-mode: horizontal-tb` and for a missing writing mode. Other writing modes do not receive a gap contributor. The measured distance can still be declined or left unexplained rather than assigned to the wrong axis.

## Anonymous and pseudo boxes

Sibling search uses element boxes from the layout parent’s element children. Anonymous boxes and pseudo-elements are not siblings in this pass. A parent inset caused only by a pseudo, with no padding or margin on the elements involved, is omitted.

## Source navigation

Source is available when the declaration is a regular stylesheet rule with a stylesheet id and a value range. Inline style declarations have no range, so they have no Source action. Constructed and adopted stylesheets that do have ranges do show Source.

When a longhand is implicit in a shorthand, Source opens the shorthand. A longhand that was authored on its own keeps its own range.

## Cross-document highlight

Highlight and Inspect resolve the target with `nodeForId` on the selected node’s `DOMModel`. A node id that belongs to another document is not highlighted. The action hides the overlay instead of pointing at the wrong box.

## Shadow and iframe selection

The model follows whatever element DevTools has selected, including a node whose `DOMModel` is an iframe or a shadow tree, once that node is the selection.

The Phase 1 live pass inserted an open shadow host and an iframe into the fixture page. After expanding the Elements tree, the inner nodes were not exposed as selectable rows, so shadow and iframe selection were not established on a running build. That is a gap in the live evidence, not a tested failure of the explanation rules.
