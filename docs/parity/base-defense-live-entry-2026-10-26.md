# Base-defense live entry

Date: 2026-10-26

Status: **PROVED for the entry and first enemy turn only.** The port remains
incomplete.

## Recorded path

The [Release entry log](../../.workers/luna-verifier/phase3-e2e-base-defense-entry-release-6.log)
records a class-2, race-0 UFO with factory HP `-1`. The script adds this record
through the native UFO factory, then waits for the normal event, clock, and draw
path to reach Base At. This is a controlled fixture, not ordinary campaign
discovery. The [flow](../../tools/e2e_flow_base_defense_entry.txt) and
[entry checker](../../tools/check_e2e_base_defense_entry.py) state that limit.

The recorded run proves:

- Base 0 had eight available defenders before event travel.
- The Base At click opened a type-3 inventory with eight XCOM defenders and 16
  hidden reserve soldiers.
- Before placement, the captured type-3 screen is screen 1 with eight XCOM, zero
  active enemies, and 16 reserve soldiers. Its placeholder cells are valid at
  this stage.
- The map confirm placed eight XCOM units on distinct cells and returned player
  input.
- The first enemy turn completed with four enemies on four cells and returned
  idle. The observed XCOM count changed from eight to five; the reserve count
  changed from 16 to 11.

The initial inventory shows placeholder cells before placement. They are not
evidence that the eight units started on those cells. The five captured PPMs
only pass size/content checks; they are not visual parity proof.

## Input and pointer scope

Startup setup uses the script `pick` helper and two `close` helper actions. The
`pick` helper automates hover and confirm; the close actions bypass those initial
screens. The later Base At click and map-confirm command reach the app input
handlers. The confirm goes through the key dispatcher. These are logical script
inputs, not physical Windows mouse or keyboard messages.

The current pointer environment binds `pbBaseAtDone` to
`&pW->bBaseAtConfirmShown` at
[pufo_world.c](../../src/pufo/app/pufo_world.c#L10791). The screen-20 pointer
press and release read that field in
[pufo_geopointer.c](../../src/pufo/game/pufo_geopointer.c#L621) and
[pufo_geopointer.c](../../src/pufo/game/pufo_geopointer.c#L1112). The latest
recorded run reaches the confirm and completes the placement path.
The original press and release gates are at `0x89414` and `0x8AF70`; they read
the confirm byte at `0x158854`. The raw-address read-back is noted in the
[current checkpoint](../../.workers/luna-parent/20261026-verified-grenade-retirement-checkpoint.md).
The earlier CDB trace showed `pbBaseAtDone` was null on the real screen-20 press
at `(120, 290)`. The current binding fixes that blocker:
[pointer trace](../../.workers/luna-verifier/base-defense-debug-cdb.log).

## Saved run

The private entry capture is kept under
[`verified-base-defense-entry-20261026`](../../.workers/luna-parent/verified-base-defense-entry-20261026/).
Its [SHA-256 manifest](../../.workers/luna-parent/verified-base-defense-entry-20261026/SHA256.txt)
pins the executable, flow output, log, and five frames. The saved
`release-5` summary log is byte-identical to the named `release-6` log above.

A separate normal-intercept run now proves startup, New Game, visible contact
after 42 clock pulses, and Base Options entry with craft 0 ready, eight crew, and
flag 27 selected. Four animation frames do not change the clock. Its runner exits
at a paused-map helper guard before a real option click; this does not prove an
original game fault. See the [captured app log](../../build/e2e_normal_intercept/run_20261026T115932Z_3023/normal_intercept.log)
and [runner log](../../.workers/luna-runtime-verifier/20261026-normal-intercept-e2e-3.log).

Still open: the real option click and craft selection, flag 10, launch, arrival,
tactical entry through the normal contact route, a full native-window mission,
and original-screen image comparison.
