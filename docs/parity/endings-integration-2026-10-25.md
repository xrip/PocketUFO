# Ending screen integration — 2026-10-25

## Port changes

- `PufoWorldState.nMapTitlesScroll` owns the ending scroll cell used by screen 18.
- `WorldHooks.pfnEndScreenInit` carries the app callback. `World_MissionSeamInitEnd` now dispatches to it through the persistent `pFrameHooks` object. New-game and load-game setup both install the callback.
- The app callback calls `EndScreen_Init` with the app font, title rows, ending backdrop, world scroll, screen cell, and options. It copies screen 18 into the app's screen mirror.
- `App_DrawFrame` passes a `TitlesBackdrop` to `Render_Frame` for screen 18. `App_TickAnim` increments the world scroll. `App_OnPointerUp` reaches `Input_OnPointerUp` even after tactical shutdown; the existing screen-18 input arm calls the ending shutdown callback.
- The shutdown callback calls `EndScreen_Shutdown`, then the normal startup UI thunk. This returns both screen copies to the menu. App quit kills timer A while the ending is active and frees the app-owned backdrop before world teardown.
- `Geoscape_CloseUI` case 19 dispatches the same world ending seam after freeing the geoscape UI. The app callback handles the strategic bankruptcy path too.

## Original ARM proof

These targets were checked from instruction bytes and ARM branch arithmetic only. IDA comments and pseudocode were not used as proof.

ARM `BL` target calculation: `EA + 8 + sign_extend(imm24 << 2)`.

- `0x35014`, bytes `53 21 00 eb` (`BL`): target `0x3d568`, the tactical shutdown arm. `0x35018` loads `R0 = 1`; `0x3501c`, bytes `23 0e 00 eb`, targets `0x388b0`, EndScreen_Init with good-end flag 1.
- `0x35180`, bytes `f8 20 00 eb` (`BL`): target `0x3d568`. `0x35184` loads `R0 = 0`; `0x35188`, bytes `c8 0d 00 eb`, targets `0x388b0`, EndScreen_Init with good-end flag 0.
- Strategic ending call at `0x84230`, bytes `9e d1 fe eb`, targets `0x388b0` after the free-UI call at `0x84228`.
- EndScreen_Init stores screen 18 and zeroes `unk_138E88` at `0x389cc` and `0x389dc`. Its `BL` at `0x389c0`, bytes `43 97 01 eb`, targets timer setup `0x9e6d4`.
- Input release call at `0x14b04`, bytes `bb 8f 00 eb`, targets EndScreen_Shutdown `0x389f8`. Its tail branch at `0x38a1c`, bytes `01 00 00 ea`, targets startup init `0x38a28`.
- Timer C352's screen-18 branch at `0xa0c28` reads, increments, and stores the ending scroll across `0xa0c38..0xa0c48`. Render_Frame's screen-18 arm at `0x22354..0x22358` dispatches to Titles_DrawBackdrop `0x1e630`.
- The initial white frame is original behavior: `0x1e674` bytes `32 00 51 e3` compare scroll with `0x32`; `0x1e678` bytes `0c 00 00 aa` branch to `0x1e6b0` when scroll is at least 50. At scroll below 50, execution reaches the white-fill call; the fixture now captures at scroll 161.
- The lost-base fixture follows raw ARM at `0x3515c` (`LDRSB` base count), `0x35160` (`CMP #1`), `0x35168` (`CMP #3` mission type), and `0x35174` (`CMP #0` victory), then the calls at `0x35180` and `0x35188`.
- The good-end probe reads a different raw cell field than the old port name suggests. `Map_BuildMissionTerrain` writes `Map+0x15338` pointer slots at 4-byte stride, with `0x3840` bytes per layer and `0x3c` pointer slots per row. In `Tact_ScanVictoryTiles`, the base `Map+0x193f0` is slot 4142 = layer 1, row 9, column 2. Its `0x28` multiplier advances ten cells per spawn-tile step; the loop adds 4 bytes per column and `0xf0` per row. The four probes are therefore layer 1 at `(2 + 10*x, 9 + 10*y)` through the next row/column. Raw `MapCell_SetTileSprites` at `0x548d0` stores the fourth sprite at cell offset `0x0c`, while raw `MapCell_ListInsertByPri` at `0x54b14` uses the item-list head at `0x34`. The victory scan's `LDR [cell,#0x0c]` tests for a NULL ceiling sprite, not an empty item list.
- Bankruptcy close bytes: `0x8bbec` `01 00 a0 e3` loads mode 1; `0x8bbf0` `4a ec ff eb` branches to `0x86d20` (free geoscape UI); `0x8bbf4` `00 00 a0 e3` loads mode 0; `0x8bbf8` `2c b3 fe eb` branches to `0x388b0` (bad ending). Stores at `0x8bc04` and `0x8bc0c` set tick hold and battle ended.
- The real pulse gates are raw ARM: `0x954e8` `LDR R0,[R0]`, `0x954ec` `CMP R0,#1`, `0x954f0` `BEQ` out when action-fail is 1; `0x9550c` `CMP R3,#0` and `0x95510` `BNE` past clock work unless the screen is 0; `0x95610` loads the pause byte, `0x95614` tests it, and `0x95618` `BNE` skips the time step when paused. At `0x95624` the tick interval gets `+1`, and `0x9562c` calls the time advance.

Earlier branch arithmetic that named `0x3d470` was wrong and is withdrawn. The raw targets for both tactical branches are `0x3d568`.

## Verification

Added three prebuilt-app fixtures. `mindendcheck` starts a new game with its placed base, launches a live tactical frame, sets the existing one-base defense branch inputs, then calls `World_MissionEndCheck(1)`. `mindgoodcheck` sets mission type 4 and alternate-spawn 1 over a live map, checks the raw layer-1 2x2 ceiling slots, and calls `World_MissionEndCheck(0)`; it does not call EndScreen_Init directly. Both check screen setup, render and tick paths, and release through `App_OnPointerUp`. `mindbankreport` stages the clock at 2,678,394 seconds (Jan 31 23:59:54), aligns the last-tick stamps, and calls `App_TickPulse` once at 5 seconds per pulse. The real time advance should reach 2,678,400 seconds and open screen 19; the fixture then puts funds below the limit and closes through `World_CloseGeoscape`, which takes the bankruptcy hook. It checks screen 0, unpaused time, and no action-fail gate before the pulse.

Run both flows against a prebuilt exe with `sh tools/e2e_ending.sh [path-to-exe]`. The script does not build. It writes PPMs and checks the logged screen and resource transitions.

The parent Release exe passed the tactical flow after the fixture used one-word state labels and expected screen 22 after New Game. The screen-18 image check found 139 colors and 74,142 non-white pixels; the visible frame is `.workers/luna-parent/review/20_ending_revealed.png`. The runner's 900-pulse bankruptcy version did not open screen 19. The source now stages the calendar six seconds before February and uses one real app pulse. The new good-end callback and fixture also need a coordinated build and run.

These fixtures are meant to check the app-to-world callback paths, screen drawing and ticking, release return, and new-game click; they have not been run yet. They are shell fixtures, not a real player win. The good ending still needs a legal victory-tile run; the current scripted launch has no enemy roster, so the fixture does not force that state. A real input run through an actual mission win and failed-base ending remains open.

## Remaining limits

- The tactical fixture passed in the parent Release exe. The updated bankruptcy and good-end fixtures still need a coordinated Win64 build and execution.
- `EndScreen_Init` owns its original unchecked asset reads and allocation assumptions; this change preserves that module behavior.
- Real-input mission completion remains unverified.
