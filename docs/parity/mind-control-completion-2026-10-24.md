# Mind-control path audit (Part 169)

> Parent verification (2026-10-24): the integrated tree passes 66/66 CTest suites in both Release and Debug. This supersedes build-status notes below; it does not close the listed untested gameplay paths. See [the completion plan](../PORT_COMPLETION_PLAN.md) for remaining gaps and image/ledger gates.

Date: 2026-10-24

## Status

The static path is now traced from the live end-turn hook through enemy PSI action, side transfer, and turn-boundary return. Two focused changes are ready for the coordinated test run:

- `src/pufo/app/pufo_world.c` now gives fall-action visibility a callback that reads current side counts.
- `tests/test_enemyai.c` now covers the `rand(2) == 1` PSI handoff mode.

No build or test run was done. The app-level PSI flow still needs a real input test.

## Original path checked against IDA

IDA was ready on `pufobeta.exe.i64` (ARM32, 1103 functions). The checked functions are `Tact_EnemyAiTick` at `0x4cbe0`, `Tact_UpdateUnitControl` at `0x4a444`, and `Tact_ProcessFallAction` at `0x52adc`.

`Tact_EnemyAiTick` first updates visibility for the acting enemy. The live callback reaches `Unit_UpdateVisibility`, which fills that unit's multi-target list from the mission rosters. The AI then picks a small, non-spotted target. At `0x4cd20`, ARM loads 5 and calls `Game_RandU16`; the branch at `0x4cd34` rejects non-zero results. On a zero result, it looks for another enemy with `bPsiActive != 0`, not the acting unit, and `uTu > 15` (`0x4cd60..0x4cd84`).

That PSI actor gets a second random draw with maximum 2 at `0x4d6a4..0x4d6a8`. ARM selects mode 4 for zero and mode 5 for non-zero (`0x4d6c0..0x4d6c4`), then queues command 18 with `TactItem_156910` at `0x4d718`. The enqueue has zero TU cost. The `nMode == 13` enqueue at `0x4d7bc` is a separate alien melee path with `TactItem_156A58`; it is not the PSI control path.

In `Tact_ProcessFallAction`, command 18 mode 5 runs the PSI hit and morale checks (`0x52c64..0x52d48`). If the victim reaches the panic threshold, the code removes it from its current side list (`0x52d54`) and checks `TactCtx272.bBusy` (`0x52d70`). During the enemy turn, it appends the victim to the enemy roster and sets status 1 (`0x52dfc..0x52e18`). Outside the busy turn, it appends the victim to the XCOM roster and sets status 0 (`0x52d80..0x52d94`). The original then refreshes XCOM visibility (`0x52ec8..0x52ee0`).

`Tact_UpdateUnitControl` is called by `BeforeEnemyStep` at `0x4bb88`, and by `AfterEnemyStep` at `0x4c848` and `0x4c89c`. Its side-return arms are gated by `bFlags & 0x20`: status 1 with unit type at most 8 returns to XCOM; status 0 with unit type above 8 returns to the enemy roster. It clears TU and updates the side lists. `BeforeEnemyStep` runs for enemies before their AI phase; `AfterEnemyStep` runs after that phase. This is the turn-boundary return path.

## Live app wiring

The end-turn button calls `World_ClickEndTurn`, which calls `Tact_BeginEnemyTurn`. `TurnHooks.pfnEnemyAiTick` points to `World_TactEnemyAiTickThunk`, which calls `Tact_EnemyAiTick`. The app timer calls `App_GameTickOnce`, then `World_GameTickOnce`, then `GameTick_OnTimer`; its active-unit callback builds `TactTickCtx` and calls `Tact_TickActiveUnit`. That tick context carries the live `FallActionCtx` into queue resolution.

The fall context uses live map roster arrays and counts, `&pMap->tact`, and `MoveActionWiring` as `pCallbackUser`. The status callback requires that user type, so it remains unchanged. Before this change, the fall context left `pfnUpdateVisibility` null and used a `VisCtx` whose roster counts were cached. A PSI side switch changes the map counts before the immediate XCOM visibility sweep. The unit visibility scan divides the roster arrays using those counts, so the immediate sweep could read the old side boundary.

`World_TactFallVisibilityThunk` fixes that path. It receives the existing `MoveActionWiring *`, copies its `VisCtx` and const `UnitRosters`, refreshes the copy's counts from the wiring's live `GameMap`, and then calls `Unit_UpdateVisibility`. `fall.pCallbackUser` stays `&pW->sTactMoveWiring`, so `Map_PushStatusCb` still receives its required owner.

## Legacy frame movement comparison

The failing frame assertion is at `tests/test_geoscapeframe.c:9917`; the test pump calls `World_GameTickOnce` and then `World_TactFrame` (`:9336`). At the start of `World_TactTickActiveUnit`, the shell refreshes the roster counts from the same `pMap` used by `MoveActionWiring.pMap`, then copies the whole `tactEnv.vis` into `sTactMoveWiring.visCtx` (`pufo_world.c:5590..5597`). The move wiring is created from that same `pMap` during tact setup (`:3821`, `:4121`). The new fall thunk copies that `VisCtx`, keeps its other fields and shared pointers, and changes only the `pRosters` pointer and counts. With no side-list change during the tick, those counts equal the cached counts the old `fall.pVisCtx` path used.

`Tact_TickActiveUnit` calls `Tact_ProcessMoveAction` before it calls `Tact_ProcessFallAction`, and only enters the fall processor when `bFlag1` is set (`pufo_tactstate.c:2775`, `:2831..2832`). The thunk itself writes no cursor coordinates, move queue entries, or path context. It calls the same `Unit_UpdateVisibility` as the old fallback, and both paths share the same `SpotCtx`; that function can still request the existing focus callback. `Unit_UpdateVisibility` marks `bFlags29` bit `0x10` and `bFlags44` bit `0x01`; the path wall gate reads `bFlags29 & 0x04`, while path scratch uses `bFlags29` bit `0x08` and `bFlags44` bit `0x02`. These flag writes do not overlap those path gates. If side counts change during the tick, the updated scan can change target lists or focus and may affect later fire targeting. Without a runtime trace, this check cannot rule out that indirect effect, but it shows no direct change to the clicked path coordinates or path gates.

Alien PSI inputs are created by mission setup: alien records with `nStat6 == -1` set `bPsiActive`; the mission rank filter keeps PSI on ranks 4/8 outside Urban and XCOM-base missions, and keeps up to three PSI units in those two mission types. The AI branch therefore has a real mission source for its `bPsiActive` actors.

## Raw ARM recheck

I rechecked the mode, side-write, and visibility claims from ARM instructions and branch bytes. This section does not use pseudocode or inline comments.

At `0x4cd20`, the code loads 5, calls the random routine at `0x4cd28`, narrows the result at `0x4cd2c..0x4cd30`, then branches away on non-zero at `0x4cd34`. At `0x4d6a4`, it loads 2 and calls the random routine. It sets 4 at `0x4d6c0`, conditionally sets 5 at `0x4d6c4`, and stores that value to the stack argument at `0x4d6d8`. The call at `0x4d718` uses that argument. The separate arm at `0x4d7a4` loads `0xD`, stores it to the stack argument at `0x4d7ac`, and calls from `0x4d7bc`.

The candidate scan sets `R11` to `0x1A4` at `0x4cd5c`, adds 2 at `0x4cd64`, and reads a byte from the candidate pointer plus that offset at `0x4cd68`. It skips zero bytes, skips the current pointer (`0x4cd74..0x4cd78`), then reads a halfword at candidate offset `+6` and branches on values above `0xF` (`0x4cd7c..0x4cd84`). The project layout maps offsets `0x1A6` and `+6` to `bPsiActive` and TU; the raw proof itself is the offsets and comparisons.

Raw bytes at `0x4d718` (`40 f7 ff eb`) and `0x4d7bc` (`17 f7 ff eb`) both decode to target `0x4b420`. At that target, `0x4b470` loads `0x12` and `0x4b474` stores it at queue-entry offset `+0x18`; `0x4b488..0x4b48c` copies the saved mode to offset `+0x40`. Thus the raw code proves two mode values, a separate `0xD` arm, and the same enqueue target. The label “alien melee” for mode `0xD` is not proven by these instructions alone.

At `0x52c5c..0x52c64`, the fall resolver loads the queued value at `+0x40`, compares it with 5, and branches away when it differs. The resolver sets `R11` to zero at `0x52b00`. After the morale compare at `0x52d44`, it calls target `0x33148` from `0x52d54`. It then reads a byte at a context pointer plus `0xF8`; `BNE` at `0x52d70` selects the other side arm.

The fall resolver’s non-busy arm writes the victim pointer at the current first-list count (`0x52d74..0x52d80`), writes `R11` (zero) to victim offset `+0x17` at `0x52d94`, then increments and stores that list count (`0x52d98..0x52da8`). The busy arm writes the victim pointer at the current second-list count (`0x52de8..0x52dfc`), loads 1 and writes it to victim offset `+0x17` (`0x52e00..0x52e18`), then increments and stores the second count (`0x52e1c..0x52e34`).

The removal target at `0x33148` scans a first pointer array using count byte offset `0x1511F` and array offset `0x15154`. If it finds the pointer, it shifts later pointers left (`0x331d8..0x33210`). Otherwise it scans a second array using count offset `0x15124` and array offset `0x151CC`, then shifts (`0x33218..0x33250`). Both paths reach the count decrement at `0x33254..0x3325c`. The raw `BL` bytes at `0x52d54`, `0x4a5e0`, and `0x4a684` all decode to `0x33148`.

After either side arm, the resolver reaches a loop at `0x52ec4..0x52ee0`: it loads a pointer, calls target `0x48580` at `0x52ec8`, loads the current first-list count at `0x52ed0`, advances the pointer, and loops while the index is below that count. The call bytes at `0x52ec8` decode to `0x48580`. The side arms also call target `0x22b54` at `0x52db8` and `0x52e44`.

At `0x4a444`, raw instructions set `R7` to 1 and `R6` to 0. The first return arm checks byte `+0x17 == 0`, byte `+0xA0 > 8`, and bit `0x20` of byte `+4` clear (`0x4a5a8..0x4a5c8`). After calling `0x33148`, it stores `R7` to byte `+0x17`, stores `R6` to halfword `+6`, appends the pointer at array offset `0x151CC`, and increments its count (`0x4a5e0..0x4a620`). The other arm checks byte `+0x17 != 0`, byte `+0xA0 <= 8`, and that same bit clear (`0x4a64c..0x4a66c`). After the same removal call, it stores `R6` to byte `+0x17` and halfword `+6`, appends at array offset `0x15154`, and increments that count (`0x4a684..0x4a6bc`).

The raw branch bytes at `0x4bb88`, `0x4c848`, and `0x4c89c` each resolve to `0x4a444`. The bytes at `0x54488` resolve to `0x52adc`, linking the active-unit tick to the fall resolver. The call bytes at `0x52db8` and `0x52e44` resolve to `0x22b54`.

## Limits of the raw proof

The instructions prove constants, branches, memory offsets, pointer moves, count updates, and direct call targets. Names such as `bStatus`, `nUnitType`, `bPsiActive`, TU, and the side-list field names still depend on the project's structure mapping for those offsets. The mode `0xD` branch is distinct in the instructions, but its gameplay label is not established by this raw pass. The new world callback is port source code, so IDA cannot prove its runtime result. No app input run was made.

## Tests and remaining proof

`TestMeleeNeighbourHandoff` already covers the accepting 1-in-5 gate and mode 4. The new `TestPsiModeFiveHandoff` scripts draws `{0, 0, 1}` and checks command 18 mode 5, actor, target, item, action mode, and half-TU payment.

`tests/test_tactstate.c::TestFallActionPsiGate` already covers mode-5 hit/miss, both busy-state side-switch arms, status bytes, and the immediate visibility callback count. `tests/test_turn.c` covers `Tact_UpdateUnitControl`. Those fixtures do not test the new world callback.

The remaining check must use a real tactical app path: start a mission with an active PSI alien, end the player turn through the button, let the timer and queue run, and record a mode-5 PSI success that moves the victim to the enemy roster. Then advance to the next applicable turn boundary and verify the victim returns to the expected roster. This input path must also prove that the world thunk sees the post-switch counts. No such run was made here.

The shared IDB was read-only for this worker; no IDB changes were made.
