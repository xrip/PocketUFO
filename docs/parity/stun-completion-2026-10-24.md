# Part 168: stun, step, and throw audit

> Parent verification (2026-10-24): the integrated tree passes 66/66 CTest suites in both Release and Debug. This supersedes build-status notes below; it does not close the listed untested gameplay paths. See [the completion plan](../PORT_COMPLETION_PLAN.md) for remaining gaps and image/ledger gates.

Date: 2026-10-24

## Result

This was a bounded source and IDA audit. I made no source or test changes, and made no IDB edits. The reviewed C paths match the ARM evidence below. I found no missing production wire in the app/world path. A live check of low-stun movement and throw limits is still open.

## Soldier field at +8

In Unit_ResetBattleState at 0x498b0, ARM loads Soldier.auStat[9] from +0xb4 at 0x4999c and stores it to Soldier+8 at 0x499a8. The source and unit tests model the same initial value.

BeforeEnemyStep writes auStat[9] to Soldier+8 at 0x4c3bc-0x4c3c0. AfterEnemyStep adds the stamina and strength recovery term to the current value at 0x4c7f0-0x4c800, then caps it at auStat[9] at 0x4c80c-0x4c818. The turn source follows these operations.

## Armor-time warning

Unit_bArmorTuWarn at 0x4e254 reads the three radio options. Option 1 returns without a warning; options 2 and 3 choose armour-TU slots 1 and 2; otherwise it chooses slot 0. For each hand, ARM reads Soldier.auStat[0] at +0xa2 and ObjectRec.abArmorTu at +0xb3. A non-large unit ignores a two-handed item at ObjectRec+0x28. Slot 0 falls back to slot 1 only when its computed cost is zero. The greater hand cost is used. The warning is sent only when the unit can pay that cost now but the requested action would leave less than the cost; both status messages are then pushed.

The move-step path calls Unit_bArmorTuWarn at 0x4f62c with cost 4. The C helper and map callback are already present. The gear agent owns pufo_unit.c, pufo_unit.h, and tests/test_unit.c; I made no edits to them.

## Throw range

Tact_ResolveThrowLanding at 0x31a18 uses the selected soldier from Map+0x15334 at 0x31a54. It reads the thrown object's weight at 0x31a58, multiplies auStat[9] by that weight at 0x31a60, reads auStat[8] at 0x31a64 and Soldier+8 at 0x31a68, then multiplies those at 0x31a6c. The signed divide starts at 0x31a70.

Throw_RangeBudget in pufo_ballist.c uses the same products and clamp. tests/test_ballist.c supplies ThrowBudget values directly and checks the division, half quotient, clamp, and landing loop. Those checks do not prove that a live click copies the intended soldier and item into the budget.

## Step gate and drain

Tact_ProcessMoveAction at 0x4f3f0 gates a step before it starts. ARM loads burden at Soldier+0x1a4 and strength at Soldier+0xb2, divides four times burden by strength, adds 2, and compares the result with Soldier+8 at 0x4f5e4-0x4f60c. The same path calls the armour-TU warning at 0x4f62c.

The actual drain at 0x536a0 is inside Tact_ProcessFallAction at 0x52adc. When the actor's 0x10 flag is clear, the ARM divides four times burden by strength, takes the low 16 bits of the quotient, and stores current Soldier+8 minus 2 minus that quotient at 0x53688-0x536bc. Tact_TickActiveUnit calls Tact_ProcessFallAction when its fall flag is set at 0x54478-0x54488.

tests/test_moveaction.c drives the step gate with a fixture-built command. tests/test_tactstate.c drives the fall step with a fixture-built queue entry and checks 50 becomes 38 for strength 4 and burden 10. These tests exercise the C processors, but they do not show a player click, world tick, and resulting unit value in one live run.

The fall-tail helper returns 0 for a zero divisor, while ARM calls _rt_sdiv without a zero check. The normal move gate also divides by strength before the fall tail. The direct zero-strength fall test therefore does not establish a reachable live result; behavior for zero strength remains open. No code change was made for this uncertain input.

## Incapacitation and recovery are separate

In the inspected knockout paths, incapacitation uses Soldier.uTuSpent versus uHp, not Soldier+8. Weapon class 50 adds fire power to uTuSpent and re-rolls it after it reaches HP. Fall-action melee and psi modes also raise uTuSpent and call Unit_EnqueueIfApSpent. Tact_UpdateUnitControl at 0x4a444 later checks uTuSpent < uHp, the cannot-act flag, cell occupancy, HP, and unit size before it queues recovery. It does not test Soldier+8.

Current Soldier+8 is instead read by the step gate, step drain, throw-range formula, and status view; turn code refills it. Do not treat the current Soldier+8 value and the uTuSpent knockout counter as one path.

## App and world reachability

The source path is connected. Player throw input calls TactState_EnqueueThrow in pufo_input.c. The world tick builds MoveActionCtx with Map_BuildMoveActionCtx, wires the armor callback with Map_WireMoveActionCtx, refreshes the radio options, and calls Tact_TickActiveUnit. MoveThrow composes its budget from pTraceExcludeSel (Map.pSelUnit) and the queued item's object weight. The tick dispatcher runs move and fall processors. World_GameTickOnce installs World_TactTickActiveUnit in GameTick_OnTimer.

The Part 163 frame-test source has a click-driven grenade throw and prints the thrower's Soldier+8 value. The source search found no assertion that varies that value or checks the resulting throw budget. The app comment at pufo_app.c:1059 says the walker stays unhooked; that conflicts with World_GameTickOnce's hook at pufo_world.c:5708. The parent owns that comment correction.

## Open checks

- Run one player-path step where Soldier+8 is just above the gate, then check the visible warning or block, the charged TU, and the post-step Soldier+8 value.
- Run one player-path throw twice with the same target and item but different Soldier+8 values that fall on opposite sides of the range limit. Check the displayed result and the queued throw outcome.
- Record the Win64 run and compare it with the cited IDA paths. Existing helper tests do not close these live checks.
- For a coordinated build pass, use cmake --build build --config Release, then ctest --test-dir build -C Release. No build or test command was run in this audit.

## Files unchanged

pufo_combat.c/.h, pufo_ballist.c/.h, test_combat.c, and test_ballist.c need no edit from this audit. The class-50 combat test covers uTuSpent knockouts, not the current Soldier+8 step/throw system.
