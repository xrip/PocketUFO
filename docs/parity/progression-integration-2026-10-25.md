# Geoscape research and manufacturing progression

Date: 2026-10-25

## Original progression path

IDA target: `E:\pocketufo\pufobeta.exe.i64`. The database was read-only.

- `Geoscape_TickHourly @ 0x5D1BC` calls `ManufScreen_RefreshBaseTabs` at `0x5D304`, then `ResearchScreen_AccumulateAllocated` at `0x5D30C`. The order matters: a product unlocked by a research completion becomes available to manufacturing on a later hourly tick.
- `ResearchScreen_Init @ 0x749A8` calls `ResearchScreen_RebuildTopicList` at `0x74AE0` before `ResearchScreen_BuildPanels` at `0x74BB8`. The panel fill at `0x73734` lists only records whose `bActive` byte is 1. The rebuild at `0x74270` derives that byte from prerequisite unlocks, needed-item stock and the next-record range bytes; a zero gate sum alone does not create a visible row.
- `ResearchScreen_AccumulateAllocated @ 0x74914` walks eight base columns, adds each non-zero allocation into the topic's 16-bit `wProgress`, then calls `ResearchScreen_RebuildTopicList`.
- `ResearchScreen_RebuildTopicList @ 0x74270` takes the completed branch when progress reaches the topic total and stores `bUnlocked = 1` at `0x7440C` (after the completion comparison near `0x743A4`). The manufacturing list reads that unlock flag and the topic's five item slots at `0x6EA30..0x6EA50`.
- `ManufScreen_BuildPanels @ 0x6E9E8` is a separate filter: it scans unlocked topics (`bUnlocked == 1`, `0x6EA30`) and then their item slots, admitting only IDs whose `aManufRowFlag[item] == 1` (`0x6EA50`). A topic's product slot and `uManufWorkPerUnit` alone do not prove the item will be visible.
- `ManufScreen_RefreshBaseTabs @ 0x70EBC` adds the allocated engineer count to accumulated product work each hour, then calls `ManufScreen_CommitProduction @ 0x6F34C` at `0x71044`. The commit checks funds, storage capacity, and components; it adds the finished item to the base and removes recipe components. The funds gate compares `nFunds - price*0.7`, then the commit converts that remaining balance to an integer before storing it. The raw constant is the double `0x3FE6666666666666`.
- `Geoscape_PulseUpdate @ 0x954DC` advances the clock only on screen 0, by `nTickSeconds + 1` (`0x95618`). The regression enters screen 0, stages the tick stamps at hour boundaries, and calls the full `World_PulseGeoscape` path for both hourly callbacks.
- `Geoscape_CloseUI` case 1 writes screen 0 and frees the 10 nav, 2 panel and 3 find controls (`0x8BE94..0x8BF2C`). The geoscape root's BuildMode control re-enters screen 1 and rebuilds those controls: `Base_EnterBuildMode @ 0x84E20` stores screen 1 at `0x84E64` and calls `Geoscape_BuildNavControls` at `0x84E6C`. Screen-0 pointer release with a status flag from 31 through 49 is handled before root controls (`0x8946C..0x89568`); research-completion flag 37 reaches the right status-panel open control. Its callback `Geoscape_PrepScreenOpen @ 0x85194` clears `dwActionFail` in the shared prologue before dropping line 0. The regression acknowledges that status control, then uses root 10, draws the base view, selects base 1 then base 0 through their row centers, and presses the manufacturing nav control.
- The save path writes research at `Save_WriteResearch @ 0x3F558` and base/object records through the bases and object chunks. The regression checks the research unlock, item quantity and funds after `World_SaveSlot` / `World_LoadSlot`.

## Regression fixture

`TestProgressionWorld(const WorldHooks *)` is added before `main` in `tests/test_geoscapeframe.c`; the parent has added its `main` call.

The earlier picker required a gate-free topic that also named a manufacturable product with a non-empty recipe. The parent run showed no such record matched. The fixture now opens research first and selects a locked, running project row from the actual panel after the original init/rebuild path applied its gates; it does not set the topic's active flag. After completion, it chooses a normal work-bearing item from the actual manufacturing panel, which may belong to another unlocked topic.

The fixture starts a fresh world, places base 0 through live hover and confirm input, and adds an empty base 1 with the production base writer so base-strip input can be checked as a real change. It adds finished lab, workshop and stores rooms with `BaseFac_SetCellBuildState`, then seeds two scientist and engineer counts at base 0. It allocates one scientist through the research UI and starts the selected project. After the research-completion status acknowledgement and close, it rebuilds the freed nav controls through root 10, draws screen 1, selects base 1 then base 0, and opens manufacturing through the live nav control.

To keep the check short, it sets the running topic to one point below its total and the manufacturing accumulator to one allocation below a unit's work cost. Both completion transitions still run through `World_PulseGeoscape`, the world clock hooks and the original hourly callee path. After the research unlock, it chooses a normal item from the real manufacturing panel; that item may come from a different unlocked topic. It adds the item's recipe stock with `BaseRec_AddItemQty`, asks for one unit, assigns engineers through the live manufacturing controls, and checks the unlock, visible product row, stock increase, component quantities and funds charge. The successful commit writes the completion line (flag 34) and `dwActionFail = 1`; the fixture acknowledges it through the status-panel Open control, which runs `Geoscape_PrepScreenOpen` and clears the gate before saving. It then saves and reloads the idle world and checks the unlock, stock and funds again, including that the product remains visible in the manufacturing list.

## Fixture limits and status

The fixture seeds staff counts, research progress, accumulated manufacturing work and the clock stamps. It takes the topic offer from the real rebuilt panel and does not alter prerequisite, item-gate or offer fields. It does not test natural staffing/hiring or the many-week elapsed time of a full project. Room completion uses the production room writer with its finished-state input.

The helper is written but has not been built or run by this worker. IDB changes and commits were not made. The parent owns the coordinated run.
