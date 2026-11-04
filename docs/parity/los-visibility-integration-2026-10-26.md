# LOS and visibility integration — 2026-10-26

## Result

The native LOS calls now preserve the original viewer/target exclusions, and
`Unit_UpdateVisibility`'s special facing-2 gate matches the raw ARM branch.
Visibility still requires a clear LOS trace after the distance and angle gates.

## Original endpoint exclusions and native API

At both `Unit_ScanVisibleUnits @ 0x4834C` and the operative branch of
`Unit_UpdateVisibility @ 0x48580`, the original stores the viewer pointer in
`Map+0x15334` and the current target pointer in `Map+0x150F0` before calling
`Map_TraceLos @ 0x30890`. `Map_TraceVoxelOccupancy @ 0x3242C` reads the cell's
occupant and skips its unit-silhouette test when that pointer equals either
temporary Map pointer (`0x32768..0x32788`). Both callers restore or clear their
temporary state after the trace. The raw instruction evidence is in the
[endpoint-exclusion proof](../../.workers/luna-mind/los-exclusion-raw-proof.md).

The port carries those two live values as explicit
`pUnitSelExclude`/`pUnitExtraExclude` arguments to `Map_TraceLos` and
`Map_TraceVoxelOccupancy`. The implementation compares the occupant pointer to
both arguments before testing the unit mask. The two visibility callers pass
their actual viewer and target: `Unit_ScanVisibleUnits` in
[`pufo_unit.c`](../../src/pufo/game/pufo_unit.c#L1107) and
`Unit_UpdateVisibility` in the same file. The shared LOS API and voxel test are
in [`pufo_los.h`](../../src/pufo/game/pufo_los.h#L145) and
[`pufo_los.c`](../../src/pufo/game/pufo_los.c#L142).

This excludes only the two endpoints. Other occupants and terrain retain their
blocking effect. `TestTraceUnitEndpointExclusions` in
[`test_los.c`](../../tests/test_los.c#L306) uses occupied viewer and target
cells with intersecting unit masks. It checks the clear ray when both are
excluded, blocking when either endpoint is left in, blocking by a third unit
between them, and blocking by a floor sprite between them.

## `Unit_UpdateVisibility` facing gate

Raw ARM at `Unit_UpdateVisibility @ 0x48580` computes
`atan2(enemyY-ownY, enemyX-ownX)` at `0x490CC..0x49114`. It adds the raw
`6.283` double (`0x401921CAC083126F`) only for a negative bearing. The
`bFacing==2` arm compares that normalized bearing directly:

- `_ged` compares bearing with `0.785375` (`0x3FE921CAC083126F`) at
  `0x4926C..0x49290`. A false result branches to the accepted LOS path.
- If that comparison is true, `_gtd` compares bearing with `5.497625`
  (`0x4015FD916872B021`) at `0x49294..0x492B8`. A false result branches to
  the target-skip path; a true result reaches the accepted LOS path.

Therefore facing 2 accepts `bearing < 0.785375 || bearing > 5.497625`. Both
endpoints are strict. This is the pair of sectors around the +X direction after
the original's 6.283 normalization. The former native condition accepted the
middle interval. [`pufo_unit.c`](../../src/pufo/game/pufo_unit.c#L1451) now uses
the raw polarity. The ordinary-facing path remains the original truncated
integer absolute difference, `abs((i32)(bearing-facing)) <= 0.785375`; the
distance gate remains the truncated Euclidean distance from 1 through 15 cells.
The full register, branch, and constant analysis is in the
[facing audit](../../.workers/luna-stun/visibility-facing-angle-audit.md).

`TestUpdateVisibilityFacing2` in
[`test_unit.c`](../../tests/test_unit.c#L1173) calls the actual visibility
function with occupied viewer and enemy cells. The zeroed ray fan isolates its
enemy-roster facing gate; each accepted case still reaches `Map_TraceLos` and
asserts the target cell's spotted flag. Cases cover the +X/front sector,
near-zero and near-wrap bearings, rear and side rejection, and the two diagonal
edges around the game's approximate constants. This catches a reversed gate
that would hide the player's front targets and reveal targets across the
middle/rear sectors.

## Verification and scope

- Before the facing change, the coordinated focused Release records show
  `test_unit`, `test_los`, and `test_strateg` passing in the
  [LOS-focused run](../../.workers/luna-verifier/phase3-post-radar-los-focused-release-ctest.log).
  The separate [seven-suite focused run](../../.workers/luna-verifier/phase3-release-focused7.log)
  passed 7/7.
- After the facing change, the fresh Release `test_unit` target build and its
  CTest run passed; see the
  [build log](../../.workers/luna-verifier/phase3-fov-unit-release-build.log)
  and [CTest log](../../.workers/luna-verifier/phase3-fov-unit-release-ctest.log)
  (1/1).
- The broader frame lane remains outside this verification. The current frame
  [CTest record](../../.workers/luna-verifier/phase3-current-frame-full-ctest.log)
  has a failed `test_geoscapeframe`; the parent tracks its Part 162 hand-menu
  failure separately. This note makes no claim about a passing full suite,
  current app behavior, enemy AI, or a defeat flow.
