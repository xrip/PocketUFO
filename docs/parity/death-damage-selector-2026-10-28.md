# Type-18 death blast: object selector

Status: narrow source repair, full Debug/Release compile passed; assertions unrun.

## Original contract

The [raw capture](death-damage-selector-raw-2026-10-28.json) records bytes,
instructions and the old IDB comments. In `Tact_KillUnit @ 0x4E430`, R4 is
the Map base `0x1139B8`, loaded at `0x4E500` or `0x4E66C`.
The shot call at `0x4F124` and own-cell damage at `0x4F138` use object slot17.
The latter loads R3 from Map+`0x14C` at `0x4F128`.
The three neighbor calls at `0x4F170`, `0x4F1A8` and `0x4F1E0` instead load
R3 from Map+`0x108` at `0x4F144`, `0x4F17C` and `0x4F1B4`.
That is slot0, since `0x14C - 0x108 = 17*4`. There is no intervening slot copy.
These are raw load operands, not conclusions from IDA names or comments.

## Repair and fixture purpose

`src/pufo/game/pufo_unit.c::Tact_KillUnit` now passes `apObjects[0]` for
exactly the three neighbors. The shot and own-cell calls retain slot17.
`World_TactKillCtxFor` supplies the live asset object table.

`tests/test_unit.c::TestType18BlastAtLargeFootprintEdge` places a linked Soldier
in the +1X cell's item list, leaving the large footprint intact. Slot0 uses
valid fire type1; slot17 uses valid fire type2. The linked Soldier has HP180,
resistance100 and readiness bytes `[2]=100`, `[0]=0`. At power255, slot0
causes damage193..255, while the former slot17 choice causes118..155.
Thus the HP-zero and item-removal assertions distinguish the repaired selector
without depending on one exact random result. `Unit_ctor` zeros armor.
The fixture also retains the fixed 60-cell row-edge lookup checks.

This is a rule/footprint fixture. It does not prove native input, the complete
casualty queue, a mission result, or the shipped object records' visible effect.
Unsupported readiness-selector reachability remains unchecked.

A later [static shipped-data read](reset-inventory-object-data-2026-10-28.json)
found object0 and object17 both have fire type2 and ready selector0. These are
the selector fields used by the cell-damage helper, so this table does not
demonstrate a different neighboring damage result from the former reference.
The synthetic fixture deliberately makes those fields distinct to detect the
wrong source slot. The raw selector repair is valid; shipped gameplay impact
has not been established and is not claimed.

## Verification

Both full default builds returned exit0 after the repair:

- `.workers/luna-mission-init-audit/debug-death-name-full-compile.log`
- `.workers/luna-mission-init-audit/release-death-name-full-compile.log`

No test, game, decoder, debugger or audio device was run. The
[current snapshot](death-name-build-artifacts-2026-10-28.json) now includes both
this damage repair and the subsequent name-width repair; both full builds passed.

The [IDB readback](death-damage-selector-idb-after-2026-10-28.json) records the saved notes.
IDB comments at the four cell-damage calls were guarded against the captured
old values, updated with the raw selector facts, read back and saved with
`DBFL_BAK`. No type or instruction was changed.
