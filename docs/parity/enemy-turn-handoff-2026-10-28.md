# End Turn dispatch and live roster visibility

Checked 2026-10-28 by Luna xhigh from raw ARM and source. No production
mismatch was confirmed in this bounded chain. No executable was run.

MenuBtn_EndTurn branches to Tact_BeginEnemyTurn at0x164B0. The latter calls
BeforeEnemyStep at0x4CA48 before action-phase processing at0x4CB54. The port
installs the corresponding World hooks and queues movement/fire for timer-driven
active-unit processing; turn completion restores the same Map input owner and
selection. This is a source/dispatch review, not evidence of an observed action.

The initial visibility-roster count copies are zero before mission construction.
The checked path refreshes them before AI: BeforeEnemyStep ends with the World
visibility refresh, which copies current Map counts and runs UnitList_ForEachRefresh
(world.c:7812..7821). Side transfers and reinforcements publish their counts before
that refresh. Later active-unit ticks refresh the same roster view before dispatch
(world.c:6309..6311). No first-turn stale-zero gap was found. The enemy visibility
thunk uses that shared context; callback presence alone was not the proof.

The earlier source fixture checks End Turn and player-input return but does
not require an ordinary enemy command. Its diagnostic throw count is not an
assertion. New TestOrdinaryEnemyActionWorld stages an eligible rank0 type9
enemy, a far XCOM unit, real visibility yielding no target, and one open adjacent
role0 RMP node. It requires an actor-owned TURN or STEP before pumping the
World queue, then requires queue drain and input return.

Raw0x4AB98 reaches the unconditional turn append at0x49B84; the alternative
calls path at0x4D5F8, whose valid path emits STEP at0x2C684. Same-facing turns
are still queued, so the expected command does not rely on a chosen RNG seed.
The fixture borrows a temporary RMP view and restores the World-owned pointer.
This is a prepared game-layer scene. The assertions compile but have not run;
native movement, firing and full campaign acceptance remain open.

[Luna's raw/source map and test limits](../../.workers/luna-enemy-turn/RESULT.md).
Whole-body tactical AI parity, scene behavior and current runtime acceptance
remain open. No new production or test code is claimed by this review.
