# Debrief close and the player return order

Checked 2026-10-28 by Luna xhigh with a parent raw-evidence capture. No executable
was run. No port-only missing home-leg call was found in this checked path.

The debrief-close chain calls shutdown(0) at0x162C0 and rebuilds the geoscape
with init(1) at0x162C8. Normal victory removes the mission target at0x3DF70.
Target compaction at0x82AF4..0x82AFC decrements indices only when greater than
the removed one; it leaves an equal target index and intercept byte unchanged.
These calls do not themselves issue a home order.

The checked original has a separate flag27 UI order. Its handler tests the
flag at0x8498C, reads the ship index from the message and calls the home-travel
helper at0x849AC. The port rebinds that handler after geoscape rebuild.
Adding an automatic return based on the old fixture's missing user action
would not follow this checked original sequence.

The existing World debrief fixture checks removal and rebuild only. It does
not prove return-order input, a scheduled home leg or arrival. Required native
acceptance must explicitly exercise the player order after debrief, then
advance the flight and check one-time loot delivery. The lower-layer return
fixture now invokes production target removal (with replacement spawning disabled),
keeps the surviving intercept state, feeds flag27 to the production return handler,
and advances to one minute before and at the helper's computed arrival time.
It then checks loot delivery and no repeat. Craft0, target1 and home base2
remain distinct. This does not run a real battle, shutdown/rebuild, replacement
spawning or World/native input, and does not replace that UI check.

[Raw branches and backed-up IDB comment](return-order-scheduling-raw-2026-10-28.json).
[Luna's full scoped call/source map](../../.workers/luna-mission-return-audit/RETURN_SCHEDULING.md).
Parent updated the note at0x162C8, checked readback and saved with backup.
This is a bounded connection review, not full campaign or subsystem parity.

Both full incremental default builds passed with68 executable outputs and zero
compiler diagnostics. [Current source and image identities](return-research-build-artifacts-2026-10-28.json).
No test assertions or game flows were executed.
