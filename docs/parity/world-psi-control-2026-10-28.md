# World PSI control and restoration

Current fixture update: the [terrain-hook repair](terrain-construction-resolver-2026-10-28.md)
exposed dependence on the prior damaged mission's survivor count. Mind control
now starts its own full-HP UFO mission. That setup compiles but has not been run;
the runtime proofs below describe the earlier source/image only.

Raw ARM is the behavior evidence; comments and names are navigation only.
The [branch audit](../../.workers/luna-mind-control-raw-audit/RESULT.md) distinguishes
the mode-4/5 PSI enqueue at `0x4D718` from mode-13 melee at `0x4D7BC`.

End Turn calls the action phase synchronously: `0x4CB54`, ARM word
`0xEB00037D` (little-endian bytes `7D 03 00 EB`), targets `0x4D950`.
Its pre-render call is earlier at `0x4CA40`.
The first fixture seeded after the click and watched only that pre-render
boundary. Both clean builds succeeded, but those three new outcome assertions
failed in the [initial combined gate](../../.workers/luna-runtime-final-combined-20261027T210214Z/RESULT.md).
That failure did not establish a production binding gap.

The corrected test forwards to the real World AI callback with unchanged
arguments, then observes its queue. The present callback observes later fall
processing. It never sets an attack result, side, link, TU, or RNG inside either
observer. Temporary hooks are restored. Initial unit placement/stats and seed9
are controlled fixture inputs; this is not a naturally played PSI mission or
native HWND mouse proof.

The [actual Debug trace](../../.workers/luna-runtime-final-combined-20261027T210214Z/psi-diagnostic-20261027T212834Z/RESULT.md)
records command18/mode5/cost0 with the expected actor, target and PSI item.
Actor TU is15 when queued, then13 after later normal AI work. The target changes
from XCOM to enemy with its actor link: counts7/2 become6/3. The next real End
Turn returns the controlled human to XCOM and publishes both roster counts.
The half-TU assertion measures the queue boundary rather than the end of later
AI actions.

Test source SHA256:
`1A994EDB241DAEA4EC75EA60417ECFCFF33AA8B1EEE3D0E3FECCA3E4829C41CB`.
[Independent timing review](../../.workers/luna-integration-review-current/PSI-TIMING-REVIEW.md)
passed, followed by [full current Debug and Release suites](../../.workers/luna-runtime-final-combined-20261027T210214Z/psi-diagnostic-20261027T212834Z/FINAL-CLOSURE.md),
67/67 each. All production inputs remained unchanged; only this test file
differed from the first combined gate. No production repair was made for this
fixture timing error.
