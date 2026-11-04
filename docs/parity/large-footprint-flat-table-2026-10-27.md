# Large-unit footprint addressing

Status: source fixed; component checks and the real World purge pass.
The [full core gate](../../.workers/luna-runtime-footprint-gate/green-fullgate-f57e5f5bfb89486c9390088c9bc525ed/RESULT.md)
passes 67/67 in Debug and Release with clean builds.

## Raw rule

The original cell-pointer table starts at state offset `0x15338`, with a
fixed row stride of 60. The large-unit paths use pointer slots at base,
base+1, base+60 and base+61, rather than checking each X/Y axis separately.

- Placement at `0x49A40` sets stride 60 (`E3A0503C`). Its neighbor tables
  use offsets `0x1533C`, `0x15428`, `0x1542C`.
- Kill at `0x4E4E8` sets stride 60 (`E3A0703C`); `0x4E4DC..0x4E4F4`
  constructs the same three offsets.
- `0x4E8CC..0x4E8E8` computes `((z*60+y)*60+x)` and adds the table
  base. `0x4E8EC: E793000B` loads the +60 pointer without a Y-axis test.
- The setter at `0x548B8: E5801030` stores through its pointer directly.

The [bounded raw audit](../../.workers/luna-hook-wiring-audit/LARGE-FOOTPRINT-RAW-AUDIT.md)
also maps movement collision probes, old-cell clears, corpse clears, and
type-18 blast probes. IDA names and comments were not behavior evidence.

## Native failure and change

The [live native capture](../../.workers/luna-runtime-geoscapeframe-marsdiag/job-19cb37ff30f44238abf8ce325cffcb14/live-repro-d174e8755dfe43798851c26cb0549c55/RESULT.md)
observed a naturally placed large type 21 at `(35,59,0)`. The fixture set
its HP to zero to queue death, without moving it or truncating side lists.
The old lookup rejected `(35,60,0)` and the kill setter dereferenced null.
The capture report records a duplicate-process overlap; it is not claimed
to be an isolated run.

For this anchor the raw flat slots are 3575, 3576, 3635, 3636, corresponding
to `(35,59,0)`, `(36,59,0)`, `(35,0,1)`, `(36,0,1)`. The port now converts
only footprint neighbors to those canonical coordinates before calling its
strict lookup. Ordinary movement targets, path and LOS lookup bounds stay
unchanged. Placement, movement collision, death/corpse clears and type-18
blast probes share the coordinate rule.

The helper returns null outside the native four-layer table. Original
off-table memory access is not emulated. Original pointer contents at the
edge slots remain unproved; matching their address calculation does not
establish that the original edge kill ran successfully.

## Checks

[Debug component result](../../.workers/luna-runtime-footprint-gate/ctest-3578b9ebb2b545f6885d325adc2c5436/RESULT.md):
`test_unit` and `test_tactstate` pass 2/2. Controlled fixtures check the four
literal expected cells, placement/death/corpse cleanup, type-18 blast targets,
and a move blocked by an occupied canonical neighbor. They do not prove a
complete tactical mission.

The World regression also removes 19 naturally placed enemies through the
real queue/tick path without the prior edge-cell crash. Its later follow-up
mission checks pass after the separate deferred-restart repair.
