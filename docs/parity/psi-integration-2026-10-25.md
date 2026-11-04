# PSI training world integration

Date: 2026-10-25

## Verified original path

Target: `E:\pocketufo\pufobeta.exe.i64`; IDA reported analysis ready. The database was read-only.

- `Geoscape_CloseUI` case 19 scans signed `nBaseCount` from `abFacilityGrid + 0x618`, calls `Base_CalcPsiLabCap` for each base, and stops at the first positive capacity. ARM at `0x8BC20..0x8BC98` stores that index to `byte_158834` and `byte_1870F4`, writes screen id 17, then calls `PsiScreen_Init` with the original PSI block base `0x15EE78`. If no base qualifies, it writes screen id 0.
- `PsiScreen_Init @ 0x72B40` initializes the pad at block offset 0, positions the shared OK and Apply controls at `(1,300)` and `(120,300)`, installs `PsiScreen_OnOk` / `PsiScreen_ApplyAssignments`, allocates the two scroll controls in block slots +4/+8, installs the previous/next handlers, sets `byte_15EE84[0] = 1`, and calls `PsiScreen_BuildPanels`.
- `Geoscape_Draw` screen 17 at `0x94288..0x942E8` reads `byte_1870F4` as a signed byte at `0x94290`, passes it with the PSI block to `PsiScreen_Draw`, and blits the two PSI scroll controls only while `byte_1870F8` is clear and `byte_15EEF0` is set.
- `PsiScreen_OnOk @ 0x716A8` stores 1 to `byte_1870F8`, copies signed `byte_1870F4` to `byte_158834`, and returns the selected base. It opens the base picker; it does not close PSI.
- `PsiScreen_ApplyAssignments @ 0x72250` applies the four panel columns to `Soldier.bPsiAssigned` and `nPsiTrainDays`, updates each base's PSI-lab-needed state, then branches to `PsiScreen_BuildPanels` at `0x72394`. When `byte_15EEF0` is set, `PsiScreen_BuildPanels` stores 0 to the screen id at `0x7212C`, clears the built byte at `0x72134`, and frees the pad, panels and scroll controls. Apply is the screen's close action.

The `PsiScreen_Init` host env is not laid out like the original raw block. The world adapter owns a persistent `PsiScreenEnv` and maps its named fields to the original cells; it must not pass a cast block pointer.

## World-owned state and callbacks

`PufoWorldState` now owns `bPsiPanelOpen`, `bPsiRowSelected`, `nPsiScratchRow` and a persistent `PsiScreenEnv`. The env holds its own four per-base panel arrays, pad and scroll controls. PSI reuses the existing world cells `nBaseInfoWorkBase` (`byte_1870F4`), `nShownBaseIdx` (`byte_158834`) and `bBaseInfoOverlayOpen` (`byte_1870F8`); it does not add copies of those shared bytes.

`World_PsiWireEnv` maps the live base count, facility grid, assets, soldier roster, shortfall words, screen id, OK/Apply controls, pad loader and world click thunks. `World_PsiInitThunk` uses that env for the monthly-close call. The other click thunks run the existing PSI OK, select, apply and four-panel scroll functions through the active world.

`GeoscapeFrameState` now has `psiDraw` and `psiCtl` fields. `World_PsiWireEnv` fills the close env, pointer env, frame draw env and arm env in one feature-specific pass. It uses the signed work base for `nBaseIdx`, the shown base for `nWorkBaseIdx`, the four panel pointers at the work base, the screen pad, shared overlay/built flags, confirm/apply controls, text/color/animation sources, and `World_TransferDrawIconStripThunk` with `pW` as its user (PSI passes mark `-1`). The arm control env gets the two scroll controls and shared control blit callbacks.

The parent must call `World_PsiWireEnv(pW)` once at the end of `World_WireGeoscapeFrame` after clock wiring and once at the end of `World_RefreshGeoscapeFrame`. `World_WireGeoscapeClock` clears the close env, so that helper restores `pnShownBaseIdx`, `pnPsiBaseIdx`, `pfnPsiInit` and the world user. It also installs PSI pointer fields and screen-17 draw links. Existing `pnTransferBaseIdx` and `pbTransferRowSel` are set to the same `nBaseInfoWorkBase` and `bBaseInfoOverlayOpen` aliases by the helper.

`World_FreePsiScreen(pW)` is the world teardown hook. Call it after `World_FreeTactical` and before `GeoscapeUi_FreeAll(..., 1)` frees the shared roster. When the PSI built flag is set, it refreshes the env and calls `PsiScreen_BuildPanels` directly. The original release arm frees the pad, panels and scroll controls and sets the screen id to 0; it does not apply the checkbox edits.

An existing comment in `pufo_baseui.h` said `PsiScreenDrawEnv.nBaseIdx` came from `abFacilityGrid + 0x619`; ARM at `0x94290` proves it is the signed byte `byte_1870F4`. The comment now names that signed byte, and the world wiring uses `nBaseInfoWorkBase` accordingly. No baseui behavior changed.

## Regression helper

`TestPsiWorld(const WorldHooks *)` is defined before `main` in `tests/test_geoscapeframe.c` and called from `main`. It creates a fresh world, commits the first base through the live hover/confirm pointer path, adds base 1 through `Base_CreateNamedEntry`, writes a completed PSI-lab cell into each base through `BaseFac_SetCellBuildState`, and gives soldier 0 a valid PSI stat row at base 1. It selects base 1 through the screen-1 pointer path, stages the clock at Jan 31 23:59:54, and calls `World_PulseGeoscape` once with the normal five-second speed interval. This crosses into February and opens report screen 19 through the actual monthly hook.

The test closes the report with the live close control, checks screen 17 and its pad, panels and scroll controls, draws the PSI body, opens the base picker with OK, selects base 1 through the world pointer path, and draws again to adopt that base. It then taps the selected base strip row once more to rearm row selection, clicks the trainee row, and presses Apply. It checks the assignment flag, 15 training days, screen id 0 and freed PSI UI, then uses `World_SaveSlot` / `World_LoadSlot` to check that the assignment and countdown persist. Parent-owned `main` calls this helper after the other independent-world checks.

The parent reports the Release build and `--world-controls` run both exited 0 (`.workers/luna-parent/phase2-integrated-build.log` and `.workers/luna-parent/phase2-integrated-controls.log`). The control log shows the PSI path checks pass and ends with `all geoscapeframe checks passed`. Base 0 placement uses live hover and confirm pointer input; the test creates base 1 and finished PSI labs through production APIs. To keep the case focused, it seeds the trainee's PSI eligibility fields and final-minute clock directly. It does not test natural PSI-stat gain or a full month of organic time progression.

## Read-only UI no-op audit

A bounded source scan found one empty production callback, `World_NoopClick`, used as an initial value in world UI wiring. All 10 nav callback slots and all 13 root callback slots are then assigned concrete handlers. Raw ARM confirms the 10 nav handler installs at `Geoscape_BuildNavControls` (`0x67B74..0x67BE0`) and the root handlers at `Geoscape_InitUI` (`0x8652C..0x865C4`): zoom, six speed controls, options open/close, build mode, base info and funding. No reachable no-op was confirmed in this bounded pass. The IDB remained read-only.

The extra base-strip tap is a real pointer down/up because the original release path requires it after adopting a different base from the overlay. ARM returns early at `0x8ABA0` while the overlay remains set. After Draw adopts the base and clears the overlay at `0x72A28`, the `0x8AC6C..0x8ACA4` release path rearms row selection only when the strip base matches and cursor Y is below `28 * baseCount`; a PSI row is below that bound. The test does not write the row-selection byte directly.

## Limits

The parent added the two `World_PsiWireEnv` calls, the `World_FreePsiScreen` call, and the `main` call for the regression helper. The parent ran the documented Release build and control test. This worker made no IDB writes, builds, test runs or commits. Manual funding entry remains with the funding worker.
