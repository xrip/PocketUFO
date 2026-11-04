# Crew removal count contract — 2026-10-25

## Port change

The shell owns `PufoWorldState.nSoldierCount` as one `i32` cell. The removal envs
now use that same count type:

- `ShipRemovalEnv.pnSoldierCount`
- `BaseDemolishEnv.pnSoldierCount`
- `TactShutdownEnv.pnStrategCraftCopy`
- `TacticalLootEnv.pnSoldierCount`

`Ship_RemoveWithCargo`, `Base_DemolishFacility`, `Tact_ShutdownOutcome`, and
`Base_TransferTacticalLoot` now read and update an `i32` count. The valid roster is
bounded by `PUFO_SOLDIER_CAPACITY == 512`, so it remains within the original 16-bit
range. Save record widths remain unchanged; the soldier count on disk is still
written and read as two bytes.

The component fixtures in `tests/test_ship.c`, `tests/test_geoscape.c`,
`tests/test_baseui.c`, and `tests/test_tactend.c` now use `i32` counters for these
mutable removal envs. Other read-only 16-bit views remain unchanged.

## Original ARM evidence

These facts use instruction operands and raw bytes, not IDA comments.

- `Ship_RemoveWithCargo` loads the Map base from the literal at `0x369A8`; the bytes
  `B8 39 11 00` are `0x1139B8`. Raw `MOV`/`ORR` at `0x36938/0x3693C` form offset
  `0x1998`, and `LDRH` at `0x36940` reads the old soldier count. The routine calls
  `Soldier_RemoveById` at `0x36988` and reloads the count at `0x36994`.
- `Soldier_RemoveById` loads the same Map base from `0x36C54` (`B8 39 11 00`). It
  reads the count with `LDRH` at `0x36BA4`, compacts the pointer array, and stores
  the reduced count with `STRH` at `0x36C4C`.
- `Base_DemolishFacility` loads the same Map base from `0x66CC4`. Raw `MOV`/`ORR` at
  `0x66C64/0x66C68` form `0x1998`; `LDRH` at `0x66C6C/0x66C84` reads the soldier
  count around `Soldier_RemoveById` at `0x66CBC`. Later loops reread it at
  `0x66DF4/0x66E30`.
- Direct callers found from code xrefs: `Ship_RemoveWithCargo` is called by
  `Tact_Shutdown`, `RadarScreen_Update`, `Base_RemoveAllShips`, and
  `Base_DemolishFacility`. `Base_DemolishFacility` is called by `Tact_Shutdown` and
  `Smap_BeginCraftSelect`.

## World wiring and live-world regression

`World_WireMissionSeams` now binds ship removal and base demolition to the same
`&pW->nSoldierCount` cell. Tactical shutdown and loot use that live cell too. The
former `u16` temporary count mirror is gone; save record widths are unchanged.
The ship-removal hangar flag pointer now uses `pW->abFacilityGrid + 0x174`, with
capacity `PUFO_GRID_SIZE - 0x174`.

`TestCrewLossWorld(const WorldHooks *)` was added before `main` in
`tests/test_geoscapeframe.c`. It resets and starts a real world, uses the normal
base-placement input path, and removes craft/base through `g_world.shipEnv` and
`g_world.demolishEnv`. Its assertions cover live count-pointer identity, crew removal
by logical Soldier ID, survivor order/count, craft slot and cargo compaction, assigned
craft indices, the 254/255 markers, hangar bytes, and a map redraw after each loss.
`main` calls this helper after the radar-world test and before its next fresh-world
fixtures.

The ARM at `0x368C8` loads the hangar table address from literal `0x369A0`; raw bytes
`68 97 15 00` encode `0x159768`. `0x368D0` adds `0x174`, and `0x368D8` stores zero
to the selected byte. With the routine's index expression, the world fixture checks
`abFacilityGrid[444 + 144*home + 6*y + x]` for the removed craft and its survivors.

The helper is wired but has not yet been built or run. It proves neither the complete
tactical mission-end adapter nor mission save/load and next-mission persistence. The
component tests still use constructed envs and remain fixture-only evidence.

## Validation

No build or test was run for the new helper. The parent owns the shared build slot.
No commit or IDB write was made.
