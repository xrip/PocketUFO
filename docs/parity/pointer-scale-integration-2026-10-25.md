# Tactical pointer scale and focus

## Raw evidence

`Map_InitFacingScratch` writes its argument to Map+0x36 at `0x26F28`.
The mission initialization uses value 1 for the normal view. The nearby
`0x26F34..0x26F40` stores set the screen size to 240 by 320.

The pointer camera arm reads that same byte at `0x143D0`. A nonzero value
branches at `0x143D8` past the coordinate divisions. With zero, the instructions
at `0x143DC..0x14400` divide both pen coordinates by two before the camera-offset
call at `0x1441C`. These claims come from instructions and operand bytes.

## Confirmed port gap

The live byte belongs to `tactEnv.facing.bStoredFacing`. The input snapshot's
`bNoHalfCursor` was never set by `World_TactInputSyncIn`, so it remained zero.
Normal-view mouse coordinates were therefore halved. The fix copies the live
byte into the input snapshot on each dispatch; input only reads it.

This was not a fault in the focus callback or camera algebra. A recording wrapper
proved that a real bank-9 release called the original handler once and produced
the same camera result as the focus function on a copied context. Yet the
subsequent pen scan could not see the staged actor or nearby wall because it
used the wrong scale mode.

## Regression and limits

Part 163 of `test_geoscapeframe` now checks the live byte and input mirror after
real pointer input, proves that the focused actor is under a matching pointer
pixel, and selects a visible wall whose real trace stays bounded across the
snap-shot scatter range. The real menu, click, queue, TU/ammo spend, resolver and
FOV assertions remain. The wall and guided-fire sections pass in
`.workers/luna-parent/phase2-final-release-ctest.log`.

A helper which searches the current cursor mapping can reach a requested game
cell even when that mapping disagrees with the displayed view. State changes
alone did not expose this gap. The focused-actor pixel check and shared-byte
assertion now cover the missing connection.

The actor, weapon and nearby-wall conditions are fixture setup. This is a world
input test, not a complete mission played with physical mouse input. The same
full test run still has separate inventory-fixture and armour failures under
review; it is not a complete suite pass.
