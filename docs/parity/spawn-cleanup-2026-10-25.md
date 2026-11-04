# Spawn-time kill cleanup — 2026-10-25

## Result

The new mission path now routes `Tact_SpawnAliens`' flagged-unit cleanup through the port's full `Tact_KillUnit` path. The battle-end callback remains synchronous. `World_MissionEndCheck` admits the pre-live call only while this exact spawn kill callback is active; it rejects other pre-live calls. `World_InitTactical` clears both temporary spawn callback fields as soon as `Tact_InitMission` returns, before it handles success or failure.

The raw reachability proof is static. `TestSpawnCleanupWorld` now reaches the spawn hook through a fresh world, real base-placement input, an intercept order, a flag-23 card press, and `World_InitTactical`. It fixture-seeds the UFO's HP and race. After one real geoscape pulse, it calls the world's `pfnProcessEvents` directly; it does not wait for the scheduled minute-event path or generate UFO damage through combat. The latest parent run passed the launch/spawn assertions before the end-turn fire regression was added; that new block has not yet been built or run.

## Original behavior from raw ARM

- `Tact_InitMission` at `0x3c550` builds terrain, selects the Xcom team, calls `Tact_SpawnAliens` at `0x3ccb4`, then sets up craft cargo and equips the team. Spawn cleanup runs before that call returns.
- `Tact_SpawnAliens` at `0x3a2ac` checks `Soldier+0x1b3` (`bUnk435`) in its tail. For each flagged enemy it stores HP zero at `0x3b048`, then passes Map and Soldier to `Tact_KillUnit` at `0x3b054`.
- `Tact_KillUnit` at `0x4e430` rejects units already carrying death flag `0x20`. Its normal path updates death state, removes the unit from its side roster, may append it to the civilian roster, updates mission kill counts, runs morale/status effects, and calls `Tact_CheckBattleEnd` synchronously at `0x4efc8`. The type-18 branch can also run an explosion. A roster-only deletion or delayed check would omit original effects or change their timing.
- The corpse append writes `Map.nCivilianCount` at `Map+0x15120` directly at `0x4e96c..0x4e994`. `SideLists` contains only the Xcom and enemy arrays/counts; side removal does not touch the civilian count. `KillCtx` carries the live Map count pointer, and the corpse append writes it immediately after the civilian roster append. Both the spawn helper and `World_TactTickActiveUnit` bind it to `pMap->nCivilianCount`.
- `Tact_CheckBattleEnd` first checks the alternate extraction path. `Tact_ScanVictoryTiles` can select that ending only for mission type 4 with alternate spawn enabled; its terminal path calls `Tact_Shutdown(1)` / `EndScreen_Init(1)` at `0x35014` and sets tick-hold/battle-ended state. The normal empty-enemy ending excludes unit types 16 and 17, adds reserve count, and calls `Tact_Shutdown(1)` / `EndScreen_Init(0)` at `0x35180` when the count is zero. The Xcom-empty branch also ends the mission.

## Valid-launch reachability for the init-time check

The synchronous check must not be replayed after initialization: an original terminal branch can call shutdown and end-screen callbacks before `Tact_InitMission` continues. The current static proof shows the flagged spawn callback cannot reach those terminal cases for a valid launch:

- `Smap_BeginCraftSelect` at `0x838dc` initializes `R9` to zero. Its special and base branches store that value to `Map_bUnk15115`: `0x83a78`, `0x83e48/0x83e4c`, `0x83f40`, `0x83fdc/0x83fe0`, and `0x84108/0x8410c`. The Xcom-base type-3 branch at `0x840ec..0x84118` clears this scale too. A zero scale produces no `nStealth` spawn flags.
- Only the UFO-derived branch stores a computed scale at `0x83ecc/0x83ed0`. Raw ARM caps it at `0x46` (70) at `0x83e88/0x83e8c`. `Tact_SpawnAliens` computes the stealth budget from `(scale - 20) * nPrimary / 100` only above 20. Therefore, at the maximum valid scale, `nStealth < nPrimary`.
- The spawn loop sets `bUnk435` only for eligible small units, excluding types 16 and 17 (`0x3aa88..0x3aa94`). Primary groups use sprite slots 0 through 2, which do not use the 16/17 fallback types. If any primary units are flagged and cleaned up, at least one primary unit remains in the battle-end-counted enemy list. The empty-enemy victory cannot fire during this callback.
- The other early end route needs mission type 4 plus alternate spawn. The type-4 branch sets scale zero (`0x83fdc/0x83fe0`), so it cannot enter the flagged cleanup callback.
- The Xcom-empty loss cannot coincide with a positive-scale flagged craft launch. At `0x915ec` and `0x91868`, the UI calls `Craft_CountSoldiersAboard` and diverts to `Smap_ResolveInterceptUI` when it returns zero. The routine at `0x6649c` walks the Soldier pointer array in four-byte steps, compares `Soldier+0xb7` (`bUnk183`) with the selected craft ID, and returns the number of matches. Base branches that can have zero Xcom soldiers also store zero spawn scale, including Xcom base type 3 at `0x84108/0x8410c`.

This is a static path argument, not proof from a live game run. It relies on the original selected-craft launch gates and the bounded scale values above. If a real launch reaches `World_TactSpawnKillBattleEndThunk` with zero Xcom count or with no counted primary/reserve left, the guard must be revisited before claiming parity.

## Port changes

- `World_TactSpawnKillUnit` constructs the same combat, status, roster, kill, and shot-hook context used by `Tact_KillUnit`; it does not approximate cleanup by removing a pointer.
- `WorldTactSpawnKillState.bKillActive` is set only around `Tact_KillUnit`. `World_TactSpawnKillBattleEndThunk` invokes `World_MissionEndCheck` immediately while the marker is active.
- `World_MissionEndCheck` preserves its normal live-mission path. Before `bTactLive`, it proceeds only if `pTactMap`, `pTactState`, and assets exist, the env's `pfnKillUnit` is exactly `World_TactSpawnKillUnit`, and its user points to the same world with `bKillActive` set.
- Immediately after `Tact_InitMission`, `World_InitTactical` clears `pfnKillUnit` and `pKillUser` on both success and failure paths. This prevents the stack state pointer from escaping the init call.
- `KillCtx.pnCivilianCount` writes the Map count only when original corpse cleanup appends to the civilian roster. Xcom/enemy side removal leaves the civilian count unchanged.

## Regression purpose and limits

`TestKillSyncsMapCivilianCount` in `tests/test_unit.c` sets a zero-HP alien directly in a floorless map cell and calls `Tact_KillUnit`. It checks that the player-visible enemy count drops, the corpse remains on the civilian side, the Map civilian count follows that append, the unit is marked dead and removed from its cell, the kill count rises, and one battle-end callback is requested. It catches the copied-roster-only count fault; by itself it does not exercise the spawn path.

`TestSpawnCleanupWorld` in `tests/test_geoscapeframe.c` checks the actual launch and spawn wiring with a crew from the fresh new game. It observes the HP-zero flagged aliens on the civilian side, their absence from the enemy side, the count mirrors, and a normal battle-end scan while counted enemies remain. The damaged UFO state (`nHp=50`, race 0) is fixture-seeded. The test does not use a firing action to create that damage, and it does not kill the remaining enemies through a combat action to verify a final live victory. Those behaviors remain open.

The same helper adds a linked Soldier item to a real empty map cell and sets its
fire state. It closes deployment inventory with the normal confirm key, then
sends the installed End Turn control through tactical pointer down/release.
This drives `BeforeEnemyStep` through the production world callback and checks
`pFireWeapon`, HP, kneel/stun state, accumulated damage and unchanged TU. Its
setup is fixture state; it does not create the burning cell or linked item
through combat.

Raw `0x4CA40` bytes `32 56 FF EB` call `Render_Frame` before the enemy-step call
at `0x4CA48`. `World_ClickEndTurn` previously passed NULL for that frame hook.
It now uses `World_TactRenderThunk`. A temporary recording present hook observes
the real frame builder and requires its first presented frame to precede the
linked unit's fire damage.

Release build and this section pass in
`.workers/luna-parent/phase2-live-actions-focused.log` (2026-10-25). The complete
focused run still fails in its separate guided-launcher pointer fixture.

The linked-item fire fixture and spawn fixture still do not prove that the player's combat action creates the fire/link state used by them.
