# Detached tactical item links

Raw ARM instructions are the evidence; IDA comments and names are navigation only.
Full audit: [raw removal/save report](../../.workers/luna-cold-next-raw-audit/RESULT.md).

`MapCell_ListRemove` at `0x54B58` leaves the removed item's own `pNext`
unchanged. Its head arm reads item `+0xC` at `0x54B6C` and updates the cell
head at `0x54B70`; its other arm reads that field at `0x54B98` and updates
the prior node at `0x54B9C`. Neither arm clears the removed node's field.

The live auto-equip route calls inventory handling at `0x3BF60`, passes the
same item from R5 to R1 at `0x3BF64`, then calls removal at `0x3BF70`.
A unit-held item can therefore retain a pointer into its former ground list.
Insertion sets a new link when the item returns to a cell list.

The save writer reads each active pool item's `+0xC` at `0x43058` and writes
its target ordinal, or null code `0x7FFF`, at `0x43060..0x43070`. There is no
owner test. The reader restores each item's link at `0x43350..0x43378`.
Unit-held links are thus serialized data, even when they are not ground
ownership edges. Pool ordinal zero remains a real item.

The cold ownership observer now checks every non-null link against the active
pool and emits its ordinal for exact pre/post comparison. Ground-chain cycle,
predecessor and same-cell rules apply to chains reached from actual cell heads.
It does not clear or rewrite game links. App source hash for this correction:
`A9346D0F0DB5A0C82F82914B52ABAA62CDFDCDAFF32B957B0479E2562509D30B`.
[Separate static review](../../.workers/luna-integration-review-current/TACT-RESUME-NEXT-REVIEW.md)
passed. The [corrected cached A934 cold roundtrip](../../.workers/luna-runtime-coldowners/cached-check-201804-A042/RESULT.md)
also passes; the original run completed both save/load processes. Its first
checker failure compared different stage labels as game data. Stage labels are
now validated separately, and the full item-link comparison remains intact.
