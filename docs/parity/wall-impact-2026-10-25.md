# Wall-impact firing — 2026-10-25

## Scope

This note follows the original shot from the fire control through the tactical queue and the impact resolver. It focuses on wall, floor, and ceiling impacts. Evidence is raw ARM disassembly and literal bytes; IDA comments and pseudocode are not evidence. The live IDB is read-only.

## Input and queue

The fire confirm function at `0x18774` calls `TactState_EnqueueFireBurst` at `0x18844` (`0D CA 00 EB`), then kicks the queue at `0x1884C` (`B8 DA 00 EB`). The queue write stores command `0xE` at entry `+0x18` at `0x4B178` (`18 E0 C2 E5`) and the item pointer at `+0x1C` at `0x4B18C` (`1C 50 82 E5`).

`Tact_TickActiveUnit @ 0x54130` calls `Tact_ProcessMoveAction` at `0x54234` (`6D EC FF EB`), then `Tact_ProcessFallAction` at `0x54488` (`93 F9 FF EB`), then `Tact_ProcessFireAction` at `0x54588` (`53 F1 FF EB`). The move processor calls `Tact_TraceProjectileArc` at `0x50184` (`AB 7E FF EB`) and `0x5023C` (`7D 7E FF EB`).

## Impact flag and hit reason

`Tact_TraceProjectileArc @ 0x2FC38` clears the byte at Map offset `0x1406C` at `0x2FC54`. The raw bytes there are `0B E0 E0 E7`: the prologue builds `R11 = 0x1406C`, sets `LR = 0`, and stores the byte.

The tracer stores one to that byte at `0x3010C` (`00 30 C2 E5`) only in the separate range-limit exit block. It calls `Map_TraceVoxelOccupancy` at `0x3017C` (`AA 08 00 EB`), masks the return at `0x30180`, and branches on it at `0x30184` (`3E 00 00 1A`).

The voxel test returns zero for a blocked voxel and one for a clear voxel. The raw immediate instructions set reason values 1 (`01 20 A0 E3` at `0x3256C`), 2 (`02 20 A0 E3` at `0x3261C`), 0 (`00 20 A0 13` at `0x326A8`), 3 (`03 20 A0 13` at `0x32734`), and 4 (`04 20 A0 E3` at `0x32C68`). The blocked cases converge on the store at `0x32C74` (`03 20 88 E7`) through offset `0x1404C`. The blocked return at `0x32C78` is `00 00 A0 E3`; the clear return at `0x32C80` is `01 00 A0 E3`. This slot is the impact kind that the resolver reads.

## Fire phase and resolver

`Tact_ProcessFireAction @ 0x50ADC` loads the impact flag at `0x51C3C`. For mode 7, `ANDS` (`FF 60 15 E2`) and `BNE` (`11 00 00 1A`) at `0x51C74/0x51C78` test it, then the code checks saved travel and burst count before calling `Combat_ResolveShot` at `0x51D1C` (`02 75 FF EB`). For the other fire mode, `ANDS` (`FF 30 15 E2`) and `BNE` (`15 00 00 1A`) at `0x51EF4/0x51EF8` skip the call only when the flag is set; the clear-flag path calls `Combat_ResolveShot` at `0x51F44` (`78 74 FF EB`).

The resolver compares impact slot 4 at `0x2F3E4` (`04 00 51 E3`). The following `BNE loc_2F328` at `0x2F404` (`C7 FF FF 1A`) routes reasons 0–3 through the splash path. Slot 4 takes direct-unit damage and branches to the epilogue at `0x2F584` (`A3 01 00 EA`). That path skips the item-drop loop that starts at `0x2FA9C`.

## Port wiring checked

The normal port path is connected at source level. `WndProc` sends mouse move/down/up to `App_OnPointer` and `App_OnPointerUp` (`pufo_app.c:2434-2441`); the active-mission branch routes these to `World_TactPointerMove` and `World_TactPointerUp` (`pufo_app.c:1116-1121`, `1146-1150`). `Tact_InitUiControls` sets `ctx.pfnLookClick` to `World_TactLookClickThunk` (`pufo_world.c:8329`). That thunk calls `Ui_ConfirmFireBurst` (`pufo_world.c:6786`), which enqueues the selected weapon and queued aim.

The confirm pad starts off-screen. Raw `Tact_HandleMapClick` instructions at `0x152F8..0x15318` read its width, negate it into `pLookCtl.x`, then set `bEnabled`. Raw `UiControl_HitTest` at `0x1A43C..0x1A464` accepts x through `x+width-1`; at `x=-width`, no screen pixel can hit it yet. The raw C352 timer at `0xA0A78..0xA0ABC` advances x toward zero by `max(1,x/-3)` each timer call. The port now connects `App_TickAnim` to `World_TickLookControl` (`pufo_app.c:1054`, `pufo_world.c:5489-5503`).

`World_InitTactical` fills the live shot hook table with FOV and area-fire callbacks (`pufo_world.c:4600-4603`). Each walker tick builds the move context (`pufo_world.c:5971`); `Map_BuildMoveActionCtx` binds `pMap->aim.pbFireHitResult` and `pMap->aim.pnFireArg5` to the actual Map fields (`pufo_mapctx.c:71-84`). The arc writes the range flag and collision reason through those pointers (`pufo_ballist.c:145-146`, `190-224`). `Fire_ResolveShot` passes the same impact slot and weapon to `Combat_ResolveShot` (`pufo_tactstate.c:2257-2265`); the fire path calls it from both gates (`pufo_tactstate.c:2538`, `2546`). The walker also fills `TactFireCtx` with the live Map flag, impact slot, hooks, hit windows, and `ShotOutcome` (`pufo_world.c:5975-6009`), then calls `Tact_TickActiveUnit` with it (`pufo_world.c:6071-6088`).

This confirms the source-level Win64 event and fire hooks are connected. The previous frame run found the bounded surface cell and exact view layer, but failed before command 14 because the look control was still off-screen. The app init comment at `pufo_app.c:669` says tactical pointer dispatch is a separate open issue; the later `App_OnPointer` route above makes that comment stale.

The earlier Part 163 shot probe called `pfnClick` directly, so it skipped the C352 slide and final pointer hit-test. The updated probe advances `World_TickLookControl` with real frame draws until the control reaches x=0, then releases the pointer inside its visible rectangle (`test_geoscapeframe.c:16015-16027`). The older Part 162 unit-hit probe still calls `pfnClick` directly and is not evidence for this wall case.

## Finding

The original has a live resolver path for a bounded wall, floor, or ceiling impact. The flag that suppresses resolution is set on the separate range-limit exit. No mismatch in the tracer or resolver itself is confirmed.

The look-control slide was one real integration gap: the control stayed at x=-uWidth after a target was added, so the real pointer could not hit it. `App_TickAnim` now calls `World_TickLookControl`; the test advances that shared helper and frames before pointer release.

The queue mode gap was real: `TactState_EnqueueFireBurst` filled each shot's actor, coordinates, weapon, argument, and TU cost, but left `nMode` zero. The original producer stores mode R6 into each shot record at `0x4B28C` (`40 60 82 E5`); the processor loads it at `0x501B0` (`40 30 93 E5`) and compares it with 7 at `0x501B4` (`07 00 53 E3`). The port now copies `pSpec->nMode` into each shot entry, and the unit test checks every mode-0, mode-1, and mode-7 shot.

The live test also showed that the cargo rifle is not a legal mode-7 weapon. Raw `Ui_FillActionCosts @ 0x26104`, case 7 at `0x262B0..0x262D0`, reads `uSortKey` at object offset `+0xC8`; `CMP R1,#13` at `0x262BC` is `0D 00 51 E3`, `BEQ` at `0x262C0` is `01 00 00 0A`, `CMP R1,#35` at `0x262C4` is `23 00 51 E3`, and the reject branch at `0x262C8` is `3D 00 00 1A`. Only the accepted arm loads the mode percentage at `0x262CC` (`B5 40 D0 E5`) and sets the enabled value. The final bank write at `0x2641C` (`09 20 C3 E5`) stores that enabled flag in `UiControl.bEnabled`. The shipped rifle record 4 has sort key 4, so its mode-7 row cannot be clicked in normal play. The old test called that handler directly; with the former missing queue-mode copy, its zeroed shot mode made the invalid call behave like mode 0. That was a false pass.

The loaded `main.dat` has a usable mode-7 fixture: object 13 is the Blaster launcher (sort 13, ammo object 51), and object 51 is the Blaster bomb. Raw ammo bytes show the sort-13/35 gate at `0x502FC..0x50308` (`0D 00 51 E3 01 00 00 0A 23 00 51 E3 02 00 00 1A`); those keys reach the `nShotCount` test at `0x5030C..0x50314`. The ammo decrement is at `0x50318..0x5031C` (`FF 30 82 E2 06 30 C0 E5`), and the counter advances at `0x50320..0x50328`, including for later waypoints. Other weapon sort keys branch to the ammo decrement on every shot. The separate `TestGuidedLauncherWorld` helper in `tests/test_geoscapeframe.c` stages this real weapon/ammo pair and its research gate in a fresh world, uses EndTurn for a real TU budget, selects the enabled mode-7 row by pointer, and uses the C352 slide plus a real look-control release. It expects one bomb charge for the guided burst. The parent has added this helper as the final frame-test case; an exact run of that isolated case is still pending.

The first real pointer probe on disabled rifle widget 39 landed on an enabled neighboring mode-11 row at the same screen point and armed latch 1. The test now records that state without treating it as mode-7 access, then cancels the armed action with the original screen-0 confirm key. It does not assume that a disabled row leaves the whole menu untouched.

The map action also follows the shipped double-tap option. Raw `Input_OnPointer` at `0x144D0..0x14530` reads `bRunByDblClick`, checks the down event and positive slot3 window, compares the saved cursor x/y, then calls `Input_OnAction` at `0x14520`; the first unmatched press stores 8 at `0x1452C..0x14530`. Part 162 and Part 163 now hover the chosen cell, make a real press/release, and repeat the same pixel only if the first tap did not append a waypoint or queue a shot. They log the live option, slot3 value, cursor/layer, game-tick hold, queue, map-click hook and each pointer transition.

## Regression proposal

The isolated mode-7 fixture focuses the selected unit and sets its view layer through real controls, then scans actual pen pixels for a bounded target. For each pixel it checks the same `Map_IsoOffsetA/B + CamOffset` projection used by `Input_OnPointer`, and accepts it only when the projected cell is in bounds and matches the live cursor. Raw `Input_OnUp @ 0x118B0` has live-state return branches before its screen-0 camera-scroll store at `0x119A4`; the fixture also logs the live gate bytes, input mirror and scroll delta for its key probe.

The late Part 163 wall test uses the sort-4 rifle's supported snap mode 1 through the real screen-3 button 33, then selects the surface cell through a real map pointer press. The fixture finds an interior seed wall beside a clear floor cell and stages the existing shooter there through `Unit_PlaceOnMap`. After focusing the shooter and setting the view layer through real controls, it selects the fired wall from actual pen pixels whose in-bounds projection matches the live cursor. It checks every endpoint in the raw worst-case snap-scatter window with the real tracer before arming snap mode. It then checks the real queued action's map flag, bounded impact reason, resolver/FOV, ammo, and TU. It does not set the hit result or alter the rifle. After the wall bracket it restores the shooter cell/facing and the prior camera/layer before the independent grenade fixture. The cargo assertion follows the source cell's floor geometry. This updated fixture still needs an exact run.

The current cargo item starts on a cell with a floor, so the regression checks that it stays there and does not claim to exercise a floorless-column drop. A dedicated floorless-column item-drop case remains unverified; resolver/FOV evidence is still required from the real queued shot.

The earlier run stopped before command 14 because the C352 slide had not been advanced. After that timer path was added, the live test exposed the missing queue-mode copy. Once fixed, the next run showed that the rifle mode-7 test itself was invalid: its row was disabled, and the direct callback bypassed the menu. The current source keeps the rifle's disabled-mode-7 and supported snap checks in Part 162 and runs a separate fresh-world launcher helper for mode 7 as the final frame-test case. No resolver patch is justified by the earlier mode-0 result.

## Guided-launcher identity and origin wiring

The earlier full-frame report of alien HP changing from 93 to 39 is withdrawn. The test matched units by `Soldier.nSaveId`, which is assigned for saves and is zero for these new-game units; the repeated zero matched the first roster entry each time. The same run looked up the launcher by `TactItem.nRefId`, which is also zero for newly created items, so its reported ammo change was from a different pooled item. The focused run's reported XCOM HP increases and launcher ammo `1 -> 25` are the same bad-identity error, not game outcomes.

The helper now tracks soldiers by runtime `nUnk440` (the ID used by `Soldier_RemoveById`) and re-finds the one launcher by its shipped object pointer plus ammo object index, asserting that this pair is unique. The withdrawn HP/ammo readings cannot be used as game outcomes; a fresh run with these corrected identities is still needed.

The area-origin producer is now confirmed. In raw `Tact_TraceProjectileArc @ 0x2FC38`, the code builds pointers to `Map+0x14054/0x14058` at `0x2FCAC/0x2FCBC`, then stores the previous projectile sample at `0x2FD88/0x2FDBC`. Fire-type-2 `Combat_ResolveShot` reads those Map cells as its area center at `0x2F2C0`. This is part of the original shot path, not a stale origin left over from an earlier event. The native tracer had kept the values only in local `trOrig` when callers passed `pOut == NULL`, so the side effect was lost. `AimState.pnShotOriginX/Y` now publish each sample through the live Map owner, and tactical initialization binds them to `pW->nMapShotOrigX/Y` (`pufo_ballist.c:178-182`, `pufo_world.c:4494-4497`).

The isolated fixture now checks both live pointer owners and snapshots the first resolver result before later events can replace it. It compares `ShotOutcome.nImpactX/Y` to the actual trace-stop cell in `AimState.nTgtX/Y`, allowing one cell in each axis for the previous projectile sample. It does not require the resolver center to equal the clicked waypoint. The updated fixture has not yet been run.
