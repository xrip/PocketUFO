# Shot-origin state integration — 2026-10-25

## Result

The port now models the original shared Map origin cells for shot and area-fire work. `CombatCtx` keeps its scalar origin values as a fallback and can also bind to live `Map_nFireOrigX/Y` owner cells. Native origin writers update both the context and its owner; the resolver reads the live owner when it is present.

The coordinated `test_combat` run passed 162 checks with zero failures (`.workers/luna-parent/phase2-test_combat.log`). This run covered the writer-to-fresh-context regression and the old scalar-only path.

## Raw Map contract

The original stores shot-origin coordinates in `Map_nFireOrigX/Y` at `Map+0x14054/0x14058`. The raw writers pass cell coordinates from the source cell:

- `Map_BuildMissionTerrain` writes its terrain-break target at `0x23C20/0x23C28` and `0x23D24/0x23D2C`.
- `Combat_DetonateNearbyItems` writes the detonation cell at `0x330B8/0x330C0`.
- `MapCell_TickProximityItems` writes the current cell at `0x4B7CC/0x4B7D4` and `0x4B858/0x4B860`.
- The type-18 explosion path in `Tact_KillUnit` writes the unit's cell at `0x4F0E0/0x4F0E8` and `0x4F0FC/0x4F104`.

Raw `Combat_ResolveShot` reads those Map cells in the type-1/type-2 area-FOV path (`0x2F2C0..0x2F31C`). `Map_ComputeFov` adds its ray offsets directly to the origin cells before cell validation and lookup. This path uses cell coordinates, not voxel coordinates.

## Port changes

- `CombatCtx` in `pufo_combat.h` adds trailing optional `pnShotOriginX/Y` pointers. `CombatCtx_GetShotOriginX/Y` read those live cells or fall back to the existing scalar fields. `CombatCtx_SetShotOrigin` updates the scalar fields and any bound owner cells.
- `pufo_combat.c` uses the setter for proximity detonation and proximity tick writes, and the resolver snapshots the getter values for FOV and area impact.
- `pufo_unit.c` uses the setter for the type-18 death explosion origin.
- The world binds its spawn-kill context, active-unit fire context, and persistent enemy-step proximity context to `&pW->nMapShotOrigX/Y` in `pufo_world.c`.

This closes the stale-copy path: a proximity or type-18 explosion can update the original Map owner, and a newly built combat context reads that same value.

## Regression purpose

`TestShotOriginOwnerPersists` in `tests/test_combat.c` triggers `Combat_DetonateNearbyItems` at fixture cell `(30,30)`. The writer updates shared owner cells. The test then creates a fresh `CombatCtx` with stale scalar values `(-81,-82)`, binds it to the same owner cells, and calls `Combat_ResolveShot`. Its FOV hook must see `(30,30)`. The test also checks that the writer updates its scalar fallback.

The existing scalar-only shot fixture leaves the owner pointers null and confirms the resolver still reads its scalar origin. The parent run passed both paths.

## Arc sample producer correction

The earlier caller-only raw pass missed `Tact_TraceProjectileArc`'s indirect Map writes. Raw ARM at `0x2FCA0..0x2FCC0` builds pointers to `Map+0x14054/0x14058`; the sample loop stores the pre-step cell coordinates through them at `0x2FD88` and `0x2FDBC`, on every arc step. The mode-7 caller itself has no such store, but its callee is a real origin producer.

The native `Tact_TraceProjectileArc` now writes those per-sample cell values through optional `AimState.pnShotOriginX/Y`, even when the caller passes `pOut == NULL`. `World_InitTactical` must bind `pMap->aim.pnShotOriginX/Y` to `pW->nMapShotOrigX/Y` after `Tact_InitMission` succeeds; that one seam runs for both a launched mission and a mission loaded from a save. I added `TestArcOriginOwnerToFov` in `tests/test_ballist.c`: a NULL-output arc must leave its last sample in the owner cells, and a fresh resolver context bound to those same cells must pass them to its FOV hook. This new test has not been run yet; the parent owns the coordinated build.

Standalone AimState probes continue to use null owners. The local probes in `test_geoscapeframe.c` are zeroed before tracing at lines 894, 3092, and 17098; `test_tactstate.c` resets its shared probe at line 2290, and `test_vis.c` at line 151.

## Old launcher observation

Do not use the earlier Alien7 HP delta as evidence about shot radius or origin. The old fixture matched unit snapshots by `Soldier.nSaveId` at `+0x3BC`, which is a serialization index and is zero for newly generated Soldiers until save. The live ID is `Soldier.nUnk440` at `+0x1B8`. Its item lookup also used `TactItem.nRefId`, which was zero for new items. The old HP delta did not identify the affected Soldier or ammo item reliably.

Raw ARM shows mode-7 fire passes its waypoint cell to `Combat_ResolveShot`, but type-2 area fire centers its FOV on the separate Map origin cells. A later isolated run must log the Map origin immediately before the resolver and identify Soldiers by `nUnk440` before it can attribute any distant HP change.
