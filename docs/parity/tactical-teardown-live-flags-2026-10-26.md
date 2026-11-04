# Tactical teardown and live flags

Date: 2026-10-26

Status: **PROVED for the captured exposed base-defense loss path.** This is not
proof of passive enemy pursuit, a full campaign, or full parity.

## Original raw order

The fixed original `Map` block is at `0x1139B8`. Its tick-hold byte is at
`Map+0x5A`; the battle-ended byte is at `Map+0x5C`. The raw ARM bytes and
literal values are recorded in
[STATUS-TEARDOWN-AUDIT.md](../../.workers/luna-status-teardown-audit/STATUS-TEARDOWN-AUDIT.md).

In `Tact_KillUnit`, the call to `Tact_CheckBattleEnd` is at `0x4EFC8`. The code
then reloads `Map+0x5A` at `0x4EFCC`, tests it at `0x4EFD0`, and returns at
`0x4EFD4` if it is set. Frame scratch is checked only after that gate. The
status push is later, at `0x4F0A0..0x4F0AC`.

On the no-XCOM branch, raw ARM stores tick-hold `1` at `0x35078` and outcome
`1` at `0x3507C`. The base-loss tail calls `Tact_Shutdown` at `0x35180`, runs
the end-screen init at `0x35188`, then stores tick-hold again at `0x3518C` and
battle-ended at `0x35190`. These stores happen after shutdown. The kill path
must read the live flag after the callback, not a copy made before it.

## Current port lifetime fix

The active kill context now holds pointers to the live world flags at
[pufo_world.c](../../src/pufo/app/pufo_world.c#L6329). After the battle-end
callback, [Tact_KillUnit](../../src/pufo/game/pufo_unit.c#L2018) reads the live
tick-hold and frame-scratch bytes. This follows the raw order above.

The shutdown hook stops tactical timers and clears the tact flag at `Map+0` in
[pufo_world.c](../../src/pufo/app/pufo_world.c#L8787). If shutdown occurs
inside a timer callback, it marks tactical free as pending. The full
`World_FreeTactical` call waits until the outer
[World_GameTickOnce](../../src/pufo/app/pufo_world.c#L6476) returns at outer
dispatch depth zero. This keeps the map, cell links, and visibility backing
available to the remaining `Fall_Action18` visibility call. Shutdown outside a
timer callback still frees synchronously.

The earlier CDB first-chance capture stopped in `StatusMsg_Push` after the loss
callback: [capture](../../.workers/luna-runtime-verifier/20261026-exposed-defeat-debug-cdb-firstchance.log),
[dump analysis](../../.workers/luna-runtime-verifier/20261026-exposed-defeat-dump-analysis.log).
The failure was the port's `GameMap.status` pointer into a tactical map freed
inside the callback, while the original status stack is in the fixed `Map`
block. The current live-flag gate prevents the later status push on this branch;
the delayed free also preserves the fall action's remaining cell and visibility
reads.

## Live loss and return

The private captured run used a controlled class-2, race-0 UFO factory record
and legally exposed defenders. It records 16 End Turn inputs and 15 completed
enemy turns, XCOM HP loss/removal, then screen 18 with 16 ending rows, backdrop
present, and tactical owners cleared. A logical script click returned to
startup screen 17; the normal New Game click made a fresh world with one placed
base and live troops.

The first checker pass returned 1 on its stale terminal-turn phrase
`afterenemyturnturn`. The corrected checker was run on the same saved capture;
the game was not replayed. The [checker-only pass](../../.workers/luna-parent/verified-natural-base-defeat-20261026/20261026-teardown-defeat-checker-fixed.log)
and [seven-frame sanity check](../../.workers/luna-parent/verified-natural-base-defeat-20261026/20261026-teardown-defeat-images.log)
are in the private capture. The PPM check proves file size and visible content,
not image parity. The [SHA-256 manifest](../../.workers/luna-parent/verified-natural-base-defeat-20261026/SHA256.txt)
pins the binary (`1F339192D9B3DDA16735FC579F45B8529CD4FCF7F46B721EA1FA0C14148A0F3B`),
flow, runtime log, checker, and seven frames.

This is a bounded assault after legal player exposure. It does not prove passive
AI hunting, ordinary UFO discovery, all defeat paths, or a whole campaign. The
entry fixture and its limits are in [base-defense-live-entry-2026-10-26.md](base-defense-live-entry-2026-10-26.md).

## Current build and test limits

The recorded full Release build exits 0:
[build log](../../.workers/luna-runtime-verifier/20261026-teardown-all-release-build.log).
The Release CTest run passes all 65 tests outside `test_geoscapeframe`, including
the corrected `test_vis`:
[CTest log](../../.workers/luna-runtime-verifier/20261026-core-ctest-except-frame.log).
The frame target still fails at Part 163 after accumulated synthetic combat
kills the squad:
[frame CTest log](../../.workers/luna-runtime-verifier/20261026-teardown-frame-ctest.log).
Fresh-world boundaries are being added; no passing frame run is recorded. Full
current Debug gates remain open. The [current checkpoint](../../.workers/luna-parent/20261026-verified-grenade-retirement-checkpoint.md)
records the isolation work. Older 66/66 results are historical. The earlier
tactical-resume pass predates this lifetime fix and was not rerun after it.

## Separate grenade probe

The focused CDB v3 probe checks the first grenade key and first matching
`MapCell_ListRemove` call only:
[CDB v3 log](../../.workers/luna-runtime-verifier/20261026-grenade-cdb-probe-v3.log).
It shows the floor grenade in pool slot 28 removed by
`Unit_ApplyCellDamage` with power 72 through Splash, Resolve, and FireAction.
List unlink keeps the allocation under the original `>40` retention rule. This
does not prove a general game-ownership bug. The throw test now has an explicit
fresh-item fixture; the original cargo item's lifecycle is separate. Earlier
v1/v2 CDB tries had invalid CDB syntax and are not proof. The v3 probe's later
frame failures are not a green full-frame result.
