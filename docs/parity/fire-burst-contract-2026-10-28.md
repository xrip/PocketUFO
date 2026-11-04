# Fire enqueue resets and reaction-fire parameter

Checked 2026-10-28. Parent raw analysis found two source contract faults;
Luna xhigh implemented the repair and regression. No executable was run.

## Shared resets

Original0x4B0A4 clears Map+0x150F0, the throw-target pointer. Original0x4B0BC
clears the byte flag at TactState+0x0C. Both precede the TU comparison
at0x4B0D0, so they happen even when TU is insufficient. At the successful
tail, clear-aim is queued at0x4B2E4 and Map+0x2C shot count is cleared at0x4B2EC.

The port omitted these effects as caller/renderer scratch, but its callers
did not perform the shot reset. The counter is consumed by ammo charging
at0x5030C and incremented at0x50328. For sort keys13/35 a nonzero count skips
the ammo decrement, so stale progress has behavior beyond rendering.

FireBurstSpec now carries optional live shot-count and throw-target owner cells.
All five production callers bind them: panic AI, normal AI, map click, confirmed
multi-target/UI fire and reaction fire. Standalone fixtures may leave them null.
The later width audit identifies the state field as unsigned-byte
bSkipFireShots, not a signed step delta or a four-byte value. The common
enqueue clears the throw owner and this flag before the TU gate. Only successful
enqueue resets shot count after clear-aim. The existing capacity guard remains
a separate port deviation; this review does not change it.

## Reaction argument correction

An earlier source interpretation that reaction fire should queue the burst
limit was false. The original mode-indexed value comes from Map+0x4E7C:
loads at0x4DAC4/0x4DC98 feed caller stack+0xC at0x4DACC/0x4DCA0.
The common callee loads that eighth argument at0x4B254 and stores it to
entry+0x20 at0x4B258 via the `(index+1)*56` address. Register R3 at the call
holds X, not this argument. The burst limit at Map+0x28 is a different value.

The original selector stores the corresponding bank at0x47C00. The port
selector already exposes it as ActionCostTable.nAccuracy[mode]; the World
reaction wrapper now queues that value. Other callers already passed their
per-mode parameter banks and retain that contract. This supersedes older
source comments claiming the bank was absent or the burst count was required.

## Verification scope

test_tactstate checks TU refusal versus success reset timing and flag1->0.
The old upper-word preservation assertion described a wider port model and
has been removed. Its two successive burst cases use the real ammo helper for
sort keys13 and35 without manually resetting shot count between bursts;
the test drain handles shot entries, not the full projectile/timer simulation.
Reaction parameter correctness has raw/source evidence; no new World reaction
scene was added. Native repeated fire, hit behavior and opportunity fire remain
required checks. No assertion pass is claimed.

[Raw entry/tail/queue/caller/consumer trace](fire-burst-contract-raw-2026-10-28.json).
[Prior IDB notes](fire-burst-contract-idb-before-2026-10-28.json).
[Luna implementation map](../../.workers/luna-fire-burst-reset/RESULT.md).
Parent guarded the IDB comment changes, checked readback and saved with backup.

Both full incremental default builds completed cleanly. [Current source/image
identities](fire-finance-ai-build-artifacts-2026-10-28.json). The new regressions
are compiled, not executed.
