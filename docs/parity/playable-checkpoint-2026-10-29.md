# Playable build checkpoint

Superseded for current build/test status by the
[launch-first follow-up](launch-first-followup-2026-10-29.md): both suites now
pass67/67, with a repaired enemy-turn callback crash and more game-flow results.
Image-scoped records below remain unchanged.

The user authorized isolated-desktop tests and asked to put launch and play first.
All runs below used a private `PufoTest_*` desktop; input desktop stayed `Default`.
The port is still incomplete. This checkpoint supersedes earlier compile-only status.

## Working flows

- **Native New Game, base placement, Options Save/Load and globe return:**
  [native result](../../.workers/luna-options-driver-fix/current-debug/options-row-save-load-20261029T051455Z-55caec83/OPTIONS-PROBE-RESULT.json).
  Actual window messages, timer/render callbacks and file I/O passed; App exit0,
  normal cleanup. Debug image `70A3F0C3020D70ED33188F5671F687681B0D5913AD2A612CDD34087029D71800`.
- **Interception, air combat, landing, eight-soldier deployment, move, rifle fire,
  enemy turn and debrief return:** [driver result](../../.workers/port-goal/natural-game.log),
  [game log](../../build/e2e_normal_ground_play/run_20261029T051352Z_1520/normal_ground_play.log).
  Same Debug image. This uses the existing scripted contact/mission driver,
  not a claim of an unaided whole campaign or native mouse-only mission.
- **Paid Engineer/Rifle purchase, cold load, hourly delivery, second cold load:**
  [result](../../.workers/port-goal/purchase-game.log),
  [private inputs and saves](../../.workers/port-goal/purchase-2665418c0e074f62ade6dd0bcb89c482).
  Exact debit53000; staff10->11 and Rifle delivery persist.
- **Tactical cold load, move, fire, enemy turn, abort and globe return:**
  [checker result](../../.workers/port-goal/tactical-replay.log),
  [replay log](../../.workers/port-goal/resume-load-20261029T052627Z-211/load.log).
  The same811817-byte save from the [initial failed run](../../.workers/luna-runtime-victory-current/tactical-resume-20261029T051858Z-1083)
  was copied unchanged. No mission or save creation was replayed to get a better input.
  The existing cold-load checker was unchanged. It verifies rosters, terrain,
  items, inventory, loaded player actions and the completed enemy turn.

## Return observer correction

The initial cold-load driver stopped after debrief because its observer required
the assigned craft crew to stay unchanged. A further full-roster trace disproved
the initial suspicion of missing soldiers: all eight survived.

Raw `Tact_Shutdown @0x3D568`, specifically0x3DDC8..0x3DE0C, removes XCOM
types4..8 or units whose byte+0x3C0 is zero. Soldiers left outside the return
zone can be lost on abort; that game behavior is retained.

Raw `Base_TransferTacticalLoot @0x68344`,0x685CC..0x6861C, separately changes
assignment to254 on landing when byte+0xF7 is zero and dword+0x1B4 is nonzero;
it writes byte+0xF7 =40*dword+0x1B4/100. In this run four surviving soldiers
take this branch with values8,18,18,22 and resulting bytes3,7,7,8.

The scripted observer now tracks the original Soldier identities through
compaction and reassignment. It permits only the recorded original removal or
landing rule, requires each expected survivor, rejects unexpected survivors,
and compares all25 persistent item/ammo pairs. `crew` is still the assigned
count; `crewRows` also includes tracked survivors who changed assignment.
No gameplay removal, assignment, movement, combat or save logic changed here.

The stale IDB instruction and Hex-Rays user comments claiming shrinking tactical
side lists were corrected at0x3DD0C, re-decompiled, read back and saved. The raw
global-pool removal was already recorded in the function comment. No new names,
types, calling conventions or debug-string claims were needed.

## Build and test state

Final Debug and Release full builds exit0,68 executable outputs each, no compiler
diagnostics: [Debug](../../.workers/port-goal/final-debug-build.log),
[Release](../../.workers/port-goal/final-release-build.log).

- Source fingerprint: `DDB7C78328E66CCCB4A05E7D15AC48CD2F6D25F6563A29F9DDF55C0B45F6B259`.
  SHA256 of sorted relative POSIX path + space + lowercase file SHA256 + LF;
  all434 files under src/tests/cmake plus root CMakeLists.txt.
- Final Release: `317ACF1DE59FD952C27079EDC2E1D98074C105A506C585F35FC85DCE83BA766E`.
  This is the image used in the successful tactical replay.
- Final Debug: `45AB77A70414F7AED2884D3037F4086D749BFE72102621B0813465C9A1D4FDC2`.
  Native and natural-mission proofs above predate the script-observer-only change.

The full suites initially passed64/67 in each configuration. Sixteen old fixture
fade fields were corrected to the new pointer contract. Two remaining fixture
defects were then repaired: main-menu glyph checks lacked Render_SetTarget;
the storage fixture used base index0 rather than working site index1.
Both affected suites now pass in Debug and Release:
[Debug](../../.workers/port-goal/focused-Debug-tests.log),
[Release](../../.workers/port-goal/focused-Release-tests.log).

`test_geoscapeframe` still has three failed assertions: two depend on one UFO
flight entering the camera hover band, and one PSI fixture fails its initial
idle-input check. No whole-suite pass is claimed. Investigate these through
their game flows; do not restart unrelated body audits first.

The old `e2e_strategy.sh` checker expects zero starting staff, disproved by the
restored original10/10 grants; its game run delivered both staff to11/11.
The newer paid-purchase flow above passes without changing that old checker.
The old window smoke driver also timed out reading a CDB marker that was in its
log; its exact owned processes were cleaned up. The native Options driver above
then passed. Those driver failures are not game-launch failures.

Next: play the remaining research/manufacturing and mission return paths, address
observed faults and the remaining geoscape/PSI fixture failures. Final full parity
and independent original-save interoperability are not yet proved.
