# Gear persistence audit — 2026-10-24

> Parent verification (2026-10-24): the integrated tree passes 66/66 CTest suites in both Release and Debug. This supersedes build-status notes below; it does not close the listed untested gameplay paths. See [the completion plan](../PORT_COMPLETION_PLAN.md) for remaining gaps and image/ledger gates.

## Result

The save codec and next-mission re-dress code already exist. A real gap was in
`Unit_ResetBattleState`: it left the last mission's item pointers in the Soldier
after `World_FreeTactical` freed their pool. The reset now clears those pointers,
restores the two empty backpack mirrors, and restores the empty-inventory burden.

Part 166 in `PLAN.md` remains open. This fix and the save codec tests do not prove
the live mission-end, recovery, save/load, next-mission path.

## Evidence from the original

- `Unit_ResetBattleState @ 0x498B0` is 348 bytes. Its ARM code clears both hands,
  the eight pocket items, nine belt items, and eight backpack items. It then stores
  `unk_138EA0` in backpack slots 5 and 6 at `0x49974` and `0x49978`, and calls
  `Unit_RecalcInventoryStat` at `0x49980`.
- The original reset reads the Soldier's own type and stats. Checked callers are
  `Tact_SelectTeamUnits @ 0x39744`, `Tact_DeployTeamAndHwps @ 0x39980`,
  `Tact_SpawnAliens @ 0x3A2AC`, `Tact_KillUnit @ 0x4E430`, and
  `Tact_ProcessFallAction @ 0x52ADC`. The port has extra type/stat arguments, and
  its checked callers pass the same Soldier fields the original reads. The reset
  uses typed `Soldier` members for pointers, so the Win64 pointer size is handled
  by the C layout rather than copied from ARM offsets.
- `Tact_EquipTeamFromCraft @ 0x3BE24` is called by `Tact_InitMission @ 0x3C550`
  at `0x3CCBC`. At `0x3BE60`, the original reads `Soldier+0x3C1`: a set flag enters
  the slot walk at `0x3BE8C`; a clear flag enters the default craft equipment path.
  The slot walk reads `auUnk962` and `abUnk1012`.
- One sentinel object is at numeric address `0x138EA0`. Init loads `R4` from
  literal `0x3C5E4`; bytes `B8 39 11 00` give base `0x1139B8`. Raw ARM at `0x3CB28`
  reads `[R4,#0x108]`, then `0x3CB30` stores it at `[unk_138EA0]`; `0x3CB34`
  loads `0x7D00` and `0x3CB38` stores it at `[unk_138EA0+0x14]`. The literal at
  `0x3CDC8` contains `A0 8E 13 00`. Reset at `0x49970..0x49978` loads a literal at
  `0x499F4` with the same bytes, then stores that pointer into backpack offsets
  `+0x150` and `+0x154`.
- The ARM readers use this same pointer. HUD code loads it at `0x2D15C` and compares
  the slot pointer at `0x2D160`; the literal at `0x2D56C` is `A0 8E 13 00`.
  Hit-test code loads it at `0x11C88` and compares at `0x11C94`; pointer handling
  loads it at `0x1245C` and stores it at `0x12464`. Save load sets `R10` to `0x7D00`
  at `0x417FC`; `0x41D84..0x41D88` loads the sentinel and stores it into the hand
  slot on that id. The literal words at `0x11CB4`, `0x124A8`, and `0x41D9C` are all
  `A0 8E 13 00`. Drop code loads the pointer at `0x4B504` and compares it at
  `0x4B520` before inserting an item into the cell; the literal at `0x4B6A0` is
  also `A0 8E 13 00`.
- A read-only parse of shipped `main.dat` found object 0's `bWeight` byte is zero at
  record offset `0xA5`; `bAmmoCap` at `0xAE` and all ammo ids at `0xA8..0xAD` are
  zero too. The sentinel's own ammo count remains zero, so its weight contribution
  stays zero after `Tact_InitMission` fills its `pObj` field.
- `Save_WriteSoldiers @ 0x42350` and `Save_ReadSoldiers @ 0x416BC` carry the
  tactical item links. The writer visits the hand slots at `0x429DC` and `0x429F8`,
  pocket pairs at `0x42A1C`, `0x42A50`, `0x42A84`, and `0x42AB8`, the backpack
  slots at `0x42AEC`, and the belt slots at `0x42B58`. This is 27 item links.
  The Soldier's `auUnk962` and `abUnk1012` arrays are also written and read as
  fields in `pufo_save.c`.

## Port change

`pufo_unit.c` now clears the 27 item pointers through typed `Soldier` members. It
uses `&g_itemSlotEmpty` for the two backpack mirrors. The world no longer has a
second sentinel record: mission init, drop, HUD, and hit-test contexts use this same
object. It leaves `bUnk961`, `auUnk962`, and `abUnk1012` intact for the next
mission's slot walk.

The original calls `Unit_RecalcInventoryStat` here. The ported helper needs
`InvStatHooks`, which this reset API does not own. Before mission init the sentinel's
`pObj` is null; after init it points at object 0, whose shipped weight is zero. Its
ammo count stays zero. The helper's result after reset is 1 in either state, so the
port stores that value directly and keeps the existing caller API.

`tests/test_unit.c` seeds all 27 physical slots with a stale pointer, then checks the
clear, the two sentinels, burden 1, and preservation of the loadout flag and both
saved slot arrays.

The existing save fixture in `tests/test_save.c` checks representative hand, pocket,
backpack, belt, sentinel, and meta values through `Save_WriteSoldiers` and
`Save_ReadSoldiers`. It is a codec test over constructed records, not a live mission
run.

## Open live-flow work

- `tools/e2e_flow_tact.txt` says its scripted launch skips the site and craft-select
   path. The parent measured eight XCOM entries, no enemy entries, and an empty action queue. It does not reach a mission
  result or recovery.
- In the audited `pufo_world.c` snapshot, `World_EndHookShutdown` clears and frees
  tactical state but does not run the original `Shutdown_RemoveUnits` and
  `Shutdown_MissionOutcome` path for `bKeepSome == 0`. The parent owns that shared
  app/world integration.
- The reset test proves that stale pointers are removed and the loadout template
  survives. It does not prove that the live app returns gear to a craft or rebuilds
  it after a save/load cycle.
- `docs/parity/bugs.tsv` row 405 still needs the parent to update after this reset
  change. No shared parity ledger or plan file was changed here.

## Validation

- `git diff --check`: passed; Git printed line-ending warnings for existing working
  files.
- `test_unit`: passed, 1/1.
- This unit check was run before the later canonical-world-sentinel edit. No build
  or test was run for that shared world and frame-test edit; the parent owns its
  integrated build.
- The build was configured under `.tmp/gear-persistence-build-20261024`, but CMake
  wrote the test executable to the shared `build/tests/Release/test_unit.exe` path.
  The parent was told about this output-path side effect.
