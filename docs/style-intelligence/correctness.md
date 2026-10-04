# Style Intelligence correctness contract

These declines are Phase 1 guarantees. They are not open work. Future analysis must not attribute these cases more strongly unless it has deterministic evidence that Phase 1 does not have. A matching number, a nearby node, or a wish to show more text is not that evidence.

The unit tests in `panels/style_intelligence/*.test.ts` and the live page `docs/style-intelligence/fixtures/phase1.html` lock this contract in.

## Absolute and fixed containing blocks

`parentLayoutNodeId` is the flat-tree layout parent. It skips `display: contents`. It is not the CSS containing block of an absolute or fixed box.

Phase 1 may say that insets participate in the used position or size. It does not name a containing block for `position: absolute` or `position: fixed`. The caveat is that the containing block could not be identified confidently.

## Percentage sizing

A percentage is attributed only when the measured used value matches the resolved basis that Chromium data can support, such as the layout parent’s content box when that parent’s size is definite.

A numerical match by itself is not attribution. If the basis cannot be established, the percentage is not described as the mechanism that produced the used size.

## Circular percentage height

`height: 100%` on an in-flow box whose parent’s height is `auto` does not determine the used height when the parent’s content height equals the child’s. The parent may have sized to the child. The mechanism stays content, the percentage is an ineffective contribution, and a percentage-height diagnostic is emitted.

The exception is narrow: if the auto-height parent is itself an in-flow flex or grid item, its used height may already be definite through stretch. A percentage that matches that content box is left as a percentage and is not diagnosed. Absolute percentages are not diagnosed, because their containing block cannot be named.

This rule is not a general theory of percentage sizing. It applies where the evidence of circularity exists.

## Collapsed margins

Computed margin is the used margin on the element. For collapsing block margins, Chromium does not expose the shared collapsed contribution through the data Phase 1 reads.

Those relationships may say that margins participate and that the collapsed contribution cannot be determined. They must not list the authored margin as the used pixel contribution. Flex and grid margins do not collapse, and those used margins can be counted.

## Visual adjacency

Sibling spacing uses border-box geometry. The neighbor is the nearest in-flow, untransformed box that overlaps the cross axis. DOM previous and next siblings are not assumed to be visual neighbors.

`order` on a flex item and explicit grid placement are covered by this rule. A relationship that cannot be described this way is declined or omitted.

## Transformed, out-of-flow, and ambiguous spacing

Spacing is declined, and omitted from the pane, when:

- the selected box is out of flow or transformed
- a transformed sibling shares the axis
- an out-of-flow box sits in the measured gap
- two neighbors are equally close
- position was not measured
- the sibling set is incomplete, including a layout parent with more than 24 element children

Declining is the result. Filling the gap with a plausible margin or gap is not allowed.

## Sparse grid cells

Two grid items that only meet at a corner do not share a horizontal or vertical side. They do not get a previous or next sibling relationship, and no gap is invented between them. A grid placement property can still explain the item’s position on its own axis.

## Unused containing-block space

A parent-edge distance is omitted when nothing counted explains it, and when the unexplained remainder is larger than the contributors that were counted. A 16px margin inside a much larger distance to the parent’s far content edge is unused space in the containing block. It is not reported as spacing with a large unexplained remainder.

## Flex contributions and pixel size

A flex formatting context can be what sets the used size. `flex`, `flex-grow`, `flex-basis`, and `flex-shrink` then contribute. Contribution does not mean the declaration’s token is the used pixel value. A `flex: 1 1 0` item can measure 0×something. The explanation stays “set by the parent flex layout” plus the contributing declaration. It does not say the item fills free space unless the mechanism is actually that.

## Stacking context

Phase 1 does not explain stacking contexts, paint order, or which element created a stacking context. The Layering section below is additional analysis. It does not change Phase 1’s layout, spacing, or diagnostic results.

`z-index` may be reported as having no effect when the existing `CSSRuleValidator` already says so, for example on a `position: static` box. That sentence is the validator hint. It is not a stacking analysis.

## Layering

These declines belong to the Layering section. They were checked against Blink’s paint-layer walk and `docs/style-intelligence/fixtures/phase2a-boundary-order.html`.

A boundary names a sibling stacking context only when the selected element is inside a confirmed non-root stacking context and that sibling belongs to the same parent context in the same document. The sentence describes stacking order. It does not say the sibling covers the selected box.

Effective z-index is used when one sibling is strictly greater. `auto` compares as 0 where z-index applies. Several greater siblings resolve to the greatest value. A tie at that value declines, even when paint order could sequence those siblings.

Paint order is the enclosing PaintLayer’s index from a pre-order walk of negative z-order, then in-flow layers, then positive z-order. Equal effective z-index keeps tree order, and flex or grid `order` is part of that walk. On the fixture, both `auto`, both `0`, equal positive values, `auto` beside `0`, opacity beside `transform`, and a flex `order` swap all followed that index. A descendant’s index sits between its trapping context and the next sibling context.

Paint order may say a same-level sibling context is ordered later only when both indexes are non-zero and different. Index 0 is also what the snapshot stores when a layer is missing, so it is not “painted first.” A shared index is one layer, not an order between two contexts. A higher index on a lower z-index sibling does not promote that sibling. A top-layer box (`overlay: auto`) is excluded from this comparison. On the fixture an open dialog’s index was after `z-index: 999999`, a fixed box, a transform context, and a negative z-index.

`position: relative` with `z-index: auto` is stacked and is not a stacking context. `display: contents` and `display: none` do not create one. Containing block, clipping parent, scroll container, and occlusion stay out of this explanation.
