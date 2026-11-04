# Post-scatter throw landing walk

Raw instructions are the proof. Names and comments are navigation only.

Original `Tact_ProcessMoveAction` calls `0x31A18` at `0x507F8`
(`86 84 FF EB`) and tests that first result. After XY scatter it calls the
same function again at `0x50908` (`42 84 FF EB`), using scattered XY and the
original Z. It does not test the second return. The final arc call at
`0x50960` then uses scattered XYZ.

An accepted landing walk stores saved travel at Map `+0x14038` at `0x323C4`;
the early range rejection at `0x31CD4` skips that store. The final arc routine
writes transient travel at `+0x14020`, so it does not replace the missing walk.
The old C path skipped the second call and could retain the first distance.
The [current move action](../../src/pufo/game/pufo_tactstate.c) restores it,
including the ignored return and original argument order.

[Raw audit and frozen preimages](../../.workers/luna-stun-throw-raw-audit/RESULT.md)
and [separate static review](../../.workers/luna-integration-review-current/THROW-SECOND-RESOLVE-REVIEW.md)
record the full branch and field evidence. Parent independently read the two
BL words in the active `pufobeta.exe.i64` and saved navigation notes after
backing up their old comment slots.

The [isolated RED/GREEN run](../../.workers/luna-stun-throw-raw-audit/runtime-redgreen-20261027T234616Z/RESULT.md)
uses the same new action test with both cores. All 136 scoped inputs match
except the production state module. Both old-core builds compile cleanly;
Debug and Release fail only the assertion that the action saves the second
walk's distance. Both repaired builds and tests pass. The fixture also checks
aim, TU, queue, hand and item state. It is an action/map fixture, not a played
mission or native mouse test.

Core hashes: old `5F0D93E3C79108DDDE031D846C0F7825F232F5543E5425660C35C7ECBF997574`,
new `D451FF3BE61F2F1B0670B6C720B61C7A0100C36BC7D2E6063B0494F2D290757B`.
Shared test: `6B2BAF57098F91F2199DCC79D604A98655DA83D5E0BF28751F5EDD1C4D925393`.

The test does not separately pin a non-rejected second walk returning zero;
ignored-return behavior is checked in the source/raw review. Zero-divisor and
overflow edges of the budget formula remain outside this repair's proof.
