# Strategy integration audit — 2026-10-24

> Parent verification (2026-10-24): the integrated tree passes 66/66 CTest suites in both Release and Debug. This supersedes build-status notes below; it does not close the listed untested gameplay paths. See [the completion plan](../PORT_COMPLETION_PLAN.md) for remaining gaps and image/ledger gates.

## Basis

This is a path audit, not a function-count audit. It follows player input through the app, world callbacks, state writes, paint, and save/load.

The starting commit was afd61d4a538c3f7a337471cc9c9061b6c1ad8a75. The worktree was already dirty. Parent edits arrived while this audit ran. This audit added the missing ship and loot event envs, a shared loot builder, and the shutdown base-list pointer in pufo_world.c. The status below separates the starting source from the current shared worktree.

IDA was read only. Claims about original behavior below use direct ARM instructions and xrefs in pufobeta.exe.i64; pseudocode was only a reading aid. I did not use IDA text search. I did not build or run tests.

## The live input and paint path

The real mouse path is present:

1. PocketUFO_WndProc receives WM_MOUSEMOVE, WM_LBUTTONDOWN, and WM_LBUTTONUP (pufo_app.c:2428, :2431, :2434).
2. App_PointerLogical calls Input_OnPointer for geoscape input (pufo_app.c:1112, :1130). The geoscape hook reaches World_GeoscapePointer, then Geoscape_OnPointer (pufo_app.c:817, pufo_world.c:11527).
3. On release, App_OnPointerUp calls Input_OnPointerUp, then the geoscape hook reaches World_GeoscapePointerUp and Geoscape_OnPointerUp (pufo_app.c:1144, :1159, :824, pufo_world.c:11535).
4. Screen 0 uses PointerWorld to mark a root control under the pointer; PointerUpWorld releases the same range of root controls (pufo_geopointer.c:81, :205, :691, :769). UiControl_HitTest calls that control's pfnClick on release (pufo_control.c:192).
5. The render timer and WM_PAINT call App_DrawFrame, which reaches World_DrawGeoscapeFrame (pufo_app.c:755, :811, :2406). Controls can blit on the input path; the next render timer draws the changed world state.

The test harness can drive the world pointer path: test_geoscapeframe.c:526 computes a control's center; :529 and :530 call World_GeoscapePointer and World_GeoscapePointerUp there. test_appshell.c has no WM_LBUTTONDOWN/WM_LBUTTONUP test, so it does not prove the full window input path.

## Reachability matrix

| Player path | Live callback path | State and output | Save path | Status |
|---|---|---|---|---|
| New base and building | Nav slot 0 opens the new-base screen; map and confirm input reach World_MapClick and World_ConfirmClick. Root slot 10 enters build mode; nav slot 4 opens base facilities (pufo_world.c:2370, :2374, :2493, :399, :444). | Map selection, base count, funds, facility grid, and construction state share PufoWorldState; the frame reads those same fields. | World_SaveSlot writes the live bases and facility data (pufo_world.c:2898, :3035). | Connected in source. |
| Research | Nav slot 5 opens research; its buttons reach World_ResearchStartThunk and the research env (pufo_world.c:2375, :908, :987). | The geoscape clock calls the research accumulation hook; it updates the shared research columns and topics (pufo_world.c:9046). | Research state is staged in World_SaveSlot. | Connected in source. |
| Manufacturing | Nav slot 6 opens manufacturing; confirm reaches World_ManufConfirmThunk and production commit (pufo_world.c:2376, :1055, :1227). | Clock hooks refresh workshops and tick facility construction (pufo_world.c:9047). Product, funds, and craft arrays are shared by the frame and save path. | Product and base state are written by World_SaveSlot. | Connected in source. |
| Buy, sell, and transfer | Nav slots 7–9 open transfer, buy, and sell. Their confirm handlers reach World_TransferCommitThunk and World_BuySellCommitThunk (pufo_world.c:2377, :2378, :2379, :1470, :1662). | These handlers update shared items, craft, base, and funds state. | The same state is written by World_SaveSlot. | Connected in source. |
| Geoscape clock and events | WM_TIMER C353 calls UiPulse_OnTimer; its geoscape hook calls Geoscape_PulseUpdate. Only screen 0 and an unpaused clock advance time (pufo_app.c:999, :1005; pufo_geoscape.c:4167). The clock runs ship, hourly, base, research, construction, and monthly hooks (pufo_world.c:9046, :9047, :9050). The strategy pass reaches World_ProcessEvents (pufo_world.c:2213, :2334). | Time, research, construction, monthly reports, and strategic events change the same world state. | Time and strategic items are staged by World_SaveSlot. | The clock path is connected. Some event side effects remain open below. |
| Save and load | Root slot 8 opens Options. The slot-row callbacks call World_OptionsSaveSlotRow or World_OptionsLoadSlotRow (pufo_world.c:2513, :1782, :1803). | Save stages the live world; load calls LoadGame and the real geoscape UI rebuild hook (pufo_world.c:2763, :2876, :2898, :3035). | SaveGame writes the slot; LoadGame reads it. | Connected in source. |

“Connected in source” means a player control reaches the state-changing code. It does not mean each action has a live test from input through saved state.

## Ranked open flows

### 1. Intercept arrival still cannot open the dogfight screen

The current World_ProcessEvents fills pShipEnv, pMissionCtx, and pLootEnv, but leaves StrategEventsEnv.pRadarEnv null (pufo_world.c:2213, :2243, :2245, :2247). The port only calls RadarScreen_SetActive when pRadarEnv is present (pufo_strateg.c:1052, :1054). Raw ARM at 0x7fb98–0x7fca4 tests the target timer; on the non-positive branch it sets R1 to 1, moves the radar state into R0, and calls RadarScreen_SetActive at 0x7fca4. That call is skipped in the current shell.

There is no RadarUiState or RadarSessionEnv owner in PufoWorldState; its frame.radar is only the geoscape draw arm. A working dogfight path needs persistent radar state plus the session fields, click handlers, pad loader, and draw/input wiring described by RadarSessionEnv (pufo_geoscape.h:3345). Do not pass the frame draw env as a substitute.

### 2. The monthly report can select the PSI screen without initializing it

When screen 19 closes, Geoscape_CloseUI searches for a base with a PSI lab. If it finds one, it writes screen 17, writes the PSI base indexes, and calls pfnPsiInit (pufo_baseui.c:3385, :3416, :3426, :3429, :3430). Raw ARM at 0x8bc34–0x8bc98 scans lab capacity, stores the found index to byte_158834 and byte_1870F4, writes screen 17, and calls PsiScreen_Init at 0x8bc98.

World_WireGeoscapeClock starts the close env with memset, but does not set pnPsiBaseIdx or pfnPsiInit (pufo_world.c:8961, :8963). The ported PsiScreen_Init has no shell caller in pufo_world.c. A base with a PSI lab can therefore send the world to screen 17 without building its panels or click handlers. The current PufoWorldState has no PSI screen env owner. This blocks PSI assignment even though the screen functions exist.

### 3. Manual funding screen 15 has no root callback

The geoscape root control [12] is left at the World_NoopClick default (pufo_world.c:2496; the root setup note at :2516 names the open gap). Raw ARM at 0x85164–0x85188 plays sample 17, writes screen 15, and branches to FundingScreen_Open. In Geoscape_InitUI ARM, root [12] is loaded at 0x86598 and Ui_OpenFunding is stored as its click function at 0x8659c–0x865a0.

The clock's monthly report screen 19 is wired through FundingScreen_OpenMonthly (pufo_world.c:3324). That is not the manual screen 15. The shell owns the monthly init and draw envs, but it has no manual funding opener env. The root funding button stays inert.

### 4. Mission launch is wired in the current worktree, but no live test reaches it

At the start, pfnCraftSelectClick pointed to World_NoopClick. Geoscape_InitUI ARM loads Smap_BeginCraftSelectBtn at 0x865cc and stores it on the control at 0x865d0. Raw ARM at 0x84634–0x84640 loads bCraftSelectAssaultFlag, stores 1, then branches to Smap_BeginCraftSelect at 0x838dc.

The current shared worktree assigns World_FinalAssaultClick to that control; it calls the ported button handler with pW->craftEnv (pufo_world.c:74, :2521). This is a parent edit and is not tested here. tests/test_geoscape.c:2263 calls Smap_BeginCraftSelectBtn directly. It does not click GEOUI_CTL_186B8C.

The existing tactical script still uses launch (tools/e2e_flow_tact.txt:34). App_RunDebugScript dispatches that word directly to World_ScriptLaunchMissionTail, then calls App_ScriptTact (pufo_app.c:2057, :2058; pufo_world.c:5886). Source proves that this skips the craft-selection tree and its required message line and craft choice. The script's recorded comments report no enemy roster and an empty queue, but that result was not rerun in this audit. The script proves tactical setup and input after setup; it does not prove a playable strategic launch. This does not show that every mission type fails.

### 5. Root controls and nav slots, current tree

The starting source left root slots 0, 1, 11–12 and the craft-select control on no-op callbacks. The current shared tree wires root [0]/[1] to zoom, [11] to the Ufopaedia, and the craft-select control to World_FinalAssaultClick (pufo_world.c:2498, :2499, :2520, :2521). Geoscape_InitUI ARM loads and stores Smap_OnZoomOut at 0x8652c–0x86534, Smap_OnZoomIn at 0x8653c–0x86540, Ui_OpenBaseInfo at 0x865b0–0x865b8, and Smap_BeginCraftSelectBtn at 0x865c8–0x865d0. The root [11] function's ARM at 0x84af8–0x84bd4 plays sample 17, writes screen 16, and branches to Ui_InitBaseInfo. The zoom handler ARM at 0x84ab8–0x84ad0 and 0x84ad8–0x84af0 plays sample 17, loads g_smapCamera, and branches to Smap_ZoomOut and Smap_ZoomIn.

Root [12] remains the only root no-op. All ten main nav slots are overwritten with callbacks (pufo_world.c:2370, :2379); the loop's initial World_NoopClick values do not leave any of those ten slots inert.

## Event env change in this audit

At afd61d4, World_ProcessEvents filled pMissionCtx but omitted pShipEnv and pLootEnv. This audit now assigns pW->shipEnv and uses World_FillTacticalLootEnv for the same world-owned arrays used by tactical shutdown (pufo_world.c:2179, :2213, :2243, :2247). The shared builder is also called by World_CommitTactShutdownOutcome; its Tact_ShutdownOutcome call remains.

Raw ARM at 0x805bc directly calls Smap_DismissSelected from StrategList_ProcessEvents. In Smap_DismissSelected ARM at 0x84428–0x84448, the original reads line 0 color slot 0 as the ship index, passes it in R1 to Ship_RetargetTravel, then removes the line. In the port, a null pShipEnv makes Ship_RetargetTravel return -1 before changing the craft, while the line and done event item are still removed; the craft is left targeting a missing item. The new pShipEnv restores that homeward retarget.

At 0x80e74–0x80e78, the original loads abFacilityGrid into R0 and calls Base_TransferTacticalLoot with R1 already set to 0 at 0x80e58. The new pLootEnv restores that loot transfer on the return path.

The tactical shutdown env also now sets apBaseCopy from pW->apBases (pufo_world.c:8114). Raw ARM at 0x3de50–0x3de60 reads the selected base pointer and passes its facility type to Base_DemolishFacility. The shared loot builder keeps the existing Tact_ShutdownOutcome call. These source changes have not been built or tested.

pRadarEnv remains open, as described above. StrategEventsEnv.pMissionCtx is already filled at World_ProcessEvents; the comment in pufo_strateg.h:519 that lists it among null envs is stale. World_FillMissionCtx also sets World_ContactScreen (pufo_world.c:2172), so the clock comment at pufo_world.c:3025 claiming that contact hook does nothing is stale too. Do not treat those two comments as current gaps.

## Reproducible live-input check

Use the existing test_geoscapeframe.c control-center helper at line 526, but stage a real screen-0 craft-selection line and valid craft first. Click the world-owned GEOUI_CTL_186B8C through World_GeoscapePointer and World_GeoscapePointerUp at lines 529 and 530; assert the assault flag, mission header, and mission-init seam. Do not call Smap_BeginCraftSelectBtn, Tact_InitMission, or World_ScriptLaunchMissionTail directly in this check.

After the radar session env is added, a separate live clock fixture should advance an intercept target to timer zero through World_ProcessEvents and assert that the radar session opens. A return fixture should also assert homeward craft retarget and recovered items. The current test_clock.c and test_strateg.c build their hook envs by hand, so they cannot prove this world wiring.

## Work not done

No test, build, or commit was run. The code and root-button fixes in the shared worktree remain unverified. The current open items are the radar session owner, PSI init/base-index wiring, and the manual funding screen owner.
