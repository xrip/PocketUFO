# Radar and interception integration — 2026-10-26

## Scope

This report follows one live strategy path: craft arrival, radar open, draw and input, outcome, then
return to the geoscape. It does not count functions or claim full game parity. Raw ARM instructions
and literal values are the evidence for the original call path below; IDA comments are not used as
proof.

## Strategic radar contact range correction

Raw `Strateg_CalcDetect @ 0x81378` uses a limit of 50.0 for site states 1/11 and 65.0 for states
2/12. The previous port constants and `TestCalcDetect` assertions were 24.0/31.0; those values did
not match the ARM operands.

- The state dispatch at `0x814C8..0x814E4` selects 1 -> `0x817E4`, 2 -> `0x816EC`, 11 -> `0x8160C`,
  and 12 -> `0x814E8`. Each selected arm first requires the timer word at `+0x2C` to equal zero.
- The 1/11 arms set `R2=0`, then build `R3=0x40490000` with `MOV R3,#0x40000000` and
  `ORR R3,R3,#0x490000` (`0x81850/0x8185C` and `0x81680/0x8168C`). This is the high word of
  double 50.0.
- The 2/12 arms set `R2=0` and load `R3=0x40504000` (`0x81760/0x81764` and `0x8155C/0x81560`).
  Its literal at `0x815F4` is `00 40 50 40`, the high word of double 65.0.
- The wrapped X delta uses the original 800-wide map constant `0x320`; Y is direct. The code squares
  and adds the deltas, calls `sqrt`, then passes that result in `R0:R1` and the second double in
  `R2:R3` to the COREDLL `_led` import at IAT slot `0xA6074`. `MOVS` and `BEQ` skip the contact
  branch when `_led` returns zero. There is no coordinate scale between the deltas and compare.
- The port now uses the raw 50.0/65.0 limits. `TestCalcDetect` checks 50/51 wrapped distance for
  states 1/11 and 65/66 for states 2/12, including hyper flags. It marks the item visible and uses
  the forced 100 result so these boundary checks do not consume RNG or depend on message-roll order.
  The unrestricted Release build is clean in
  [phase3-final-merge-release-build.log](../../.workers/luna-verifier/phase3-final-merge-release-build.log),
  and `test_strateg` passes in
  [the focused CTest log](../../.workers/luna-verifier/phase3-post-radar-los-focused-release-ctest.log).
  That CTest log also has a separate `test_geoscapeframe` failure in Sections 162/163; Part 100.6's
  UFO landing checks pass. This is not a full-suite green result, and no full suite has verified the
  new boundaries.

The old 24/31 statements in `docs/parity/coverage.md` and `docs/parity/game-core.md` are stale. The
saved natural-contact sweep at 18.4 tiles lies within both old and raw limits, so it does not prove
the disputed outer boundary. Raw ARM and current World wiring now show the normal input route from
a base's local craft list through flag 27, UFO flag 10, and the contact confirm callback to
Ship_LaunchIntercept. Details are in
[the natural encounter flow proposal](../../.workers/luna-strategy/natural-encounter-flow-proposal.md).
No fresh live run has exercised that route from New Game. The older contact sweep stops at flag 10,
so natural launch and arrival remain open.

## Raw ARM evidence

| Address | Raw instruction facts | What this proves |
| --- | --- | --- |
| `0x7FC9C–0x7FCA4` | `MOV R1,#1`, `MOV R0,R4`, `BL RadarScreen_SetActive` | The arrival branch opens radar with active value 1. |
| `0x61458–0x6148C` | Both branches call `RadarScreen_Init`; active 1 stores pause 1, active 0 stores pause 0; the value is also stored to the frame-style byte. | Open and close own the pause and root frame state. |
| `0x5FCB4–0x5FCF4` | Five handler addresses are stored at control `+0x0C`, in order: NW, NE, SW, SE, map backdrop. | Radar controls are command callbacks, not ordinary root controls. |
| `0x8C214–0x8C298` | The active byte gates the radar draw arm; it draws the strip and root controls, then calls `RadarScreen_Update` at `0x8C298`. | The radar update runs from render, once per drawn frame. No extra timer hook is needed. |
| `0x87A48–0x87A8C` | The active branch indexes the radar control table and calls `UiControl_HitTest` with press state. | Pointer press dispatch goes to radar controls while the session is active. |
| `0x89AEC–0x89B44` | The active branch plays sample 17, calls `UiControl_HitTest` with release state, and loops while the control index is below 5. | Pointer release checks the same five controls. The normal tap path cannot open the ordinary options/save/load controls while radar is active. This says nothing about other input paths. |
| `0x6125C`, `0x6132C`, `0x613E0` | Outcome branches call `RadarScreen_SetActive`; the same range calls `Ship_RemoveWithCargo`, `Ship_RetargetTravel`, and `PtrArray_Get`, then writes a strateg-item field. | The original update has close and strategy-return branches. They require the radar update env to be reachable. |
| `0x5DB30`, `0x5DB90`, `0x5DBA4–0x5DBB0` | Weapon-hit code calls `Game_RandU16`, stores the changed radar HP, looks up the target, then stores to target `+0x6C`. | A successful live weapon roll writes target HP back to the strateg item. |
| `0x6097C–0x609A0` | The lethal damage branch stores the spawn gate, calls `StrategList_RemoveAt` and `Ship_ClearInterceptByStrategIdx`, then stores the intercept-done byte. | A real kill removes the target and marks the radar encounter done before the normal message close. |
| `0x803B8–0x803BC`, `0x807A0`, `0x807D4–0x807D8` | Target type 4 branches to `0x80788`, stores pause 1, loads flag 23 and calls the message append. | A type-4 contact is the ground-assault offer, not the dogfight opener. |
| `0x8040C–0x80410`, `0x80EE4–0x80EEC`, `0x80FCC` | Target type 1 branches to `0x80EAC`; when its timer is `<= 0`, it calls `Strateg_ConfirmInterceptTarget`. | The radar test must target a type-1 UFO item to reach `RadarScreen_SetActive(1)`. |

These addresses were read with `idc.generate_disasm_line` at explicit addresses. The output was
limited to instruction text with semicolon comments removed.

## Port state now present

**Starting point:** the world owned only the radar-active byte and the general geoscape draw arm.
It had no persistent `RadarUiState`, `RadarSessionEnv`, `RadarUpdateEnv`, or world radar command
callbacks. The event env had no `pRadarEnv`, the frame had no `pRadarEnv`, and the pointer env had no
`apRadarCtl` binding.

`WorldRadarState` in `src/pufo/app/pufo_world.h:161` owns the radar UI, session/update envs, the
needed child envs, and radar-only scalar cells. `bRadarInterceptDone @ 0x159650` has its own byte;
it does not alias the existing geoscape `bInterceptDone @ 0x18709C`. Existing world cells remain the
owners for the active byte, intercept indices, time, message timer, cooldowns, frame style, animation,
craft, strateg list, text buffer, and ship-removal env.

`World_WireRadarSession` in `src/pufo/app/pufo_world.c:11748` refreshes the env pointers and by-value
inputs without clearing radar scalars or UI pads. It binds the five verified no-argument callbacks
to the port's env-taking corner/backdrop routines. `World_FreeRadarSession` at `:11933` rebinds then
calls `RadarScreen_SetActive(..., 0)` only when radar is active, so the close path can release its
pads before shared UI and world records are freed.

No radar game-module function was changed. The existing radar init, update, cursor commands,
backdrop, and encounter-outcome code are present in `src/pufo/game/pufo_geoscape.c`.

## Reachability matrix after parent wiring

The world links now exist. Parent's `phase2-fullframe.log` shows the armed retreat case reaches a real
hit and closes. The overall full-frame run still has unrelated failures in other sections; this is
evidence for the radar case only.

| Stage | Current source path | State |
| --- | --- | --- |
| Craft arrival | `World_PulseGeoscape` advances the real clock; `Geoscape_TickShips` calls `World_ProcessEvents`, which refreshes `WorldRadarState` and sets `env.pRadarEnv` before `StrategList_ProcessEvents`. | Wired in `pufo_world.c:2473–2508`; parent verified arrival in the earlier focused run. |
| Open and pause | `Strateg_ConfirmInterceptTarget` reaches `RadarScreen_SetActive(1)` with the persistent session env. | Wired; raw ARM at `0x61458–0x6148C` confirms Init, pause, and frame-style writes. |
| Render and update | `Geoscape_Draw` reaches `RadarScreen_Update` at `0x8C298`; world frame and refresh bind `pF->radar.pRadarEnv`. | Wired at `pufo_world.c:11694–11696` and `:12171–12173`; the update remains on render, with no added timer. |
| Pointer and commands | Pointer env `apRadarCtl` points at `radar.ui.apCtl`; `RadarScreen_Init` installs the five world thunks in ARM order. | Wired on frame setup and refresh; the earlier focused run verified NW/SE input through the world pointer path. The armed extension also uses SW. |
| Encounter outcome | SE sets the full-range/flip command; the next radar draw reaches the full-zoom exit and calls `Ship_RetargetTravel`. | Wired in existing game code; the earlier run verified close, clock resume, craft state, and pad release. |
| Armed fire | Starter interceptor mounts cannon 25 and Stingray 26 with stock ammo; SW reduces range, then rendered updates animate shots and resolve damage. | Retreat passed with ammo 10→2 and HP 250→152. Terminal HP reached 0 and seed 5 was removed; a duplicate display-ID oracle failed and is now seed-based. The oracle fix needs rerun. |
| Forced teardown | `World_FreeRadarSession` closes only when active. | It is at `pufo_world.c:11933`; calls are present before world/tactical UI release and before LoadGame's UI-free hook. |
| Save/load | Radar cells remain in-memory state; this task adds no radar save record or session restore. Normal radar pointer input visits its five controls only. | Saved active-session persistence and other input paths remain unverified. Do not clear state during env refresh. |

## Focused live-flow regression

`TestRadarWorldCase` is in `tests/test_geoscapeframe.c:1132`; `TestRadarWorld` wraps both cases at
`:1610`, and `main` calls it at `:19755`.

The initial focused run found that two direct close calls left screen 2 active; the helper now places
the first base through the same hover, pen drag, and confirm path used elsewhere in this test. A later
run showed that a type-4 target takes the flag-23 assault offer, while raw ARM at `0x803B8` and
`0x80EAC–0x80FCC` shows a type-1 target reaches `Strateg_ConfirmInterceptTarget`. The fixture now
creates an active UFO through `StrategList_AddUfoAt`, initializes its short travel leg with
`StrategEntry_InitTravel`, and launches the first interceptor with `Ship_LaunchIntercept`. Arrival
still runs through `World_PulseGeoscape` and production `World_ProcessEvents` only.

The wrapper runs two fresh-world cases with the starter interceptor's real cannon 25 (10 rounds) and
Stingray 26 (3 rounds). The Release full-frame run records the retreat case: after 67 frames,
ammo is 10/2, target HP is 152/250, Stingray cooldown is 240, then SE closes and the craft returns.

The terminal case uses a class-0 type-1 UFO and waits for the real lethal hit, target removal, message
timer, and close. Neither case writes HP or ammo directly. Both draw at long range, send SW through
the pointer path, and use pulse/draw updates. They check ammo spent, cooldown, strateg HP writeback,
paused time, craft count, hangar state, craft ammo writeback, retarget, freed radar storage, clock
resume, and a final map draw. The earlier assertion used a duplicate display ID: seed 5 was removed
while `FindStrategById(display 1)` found the friendly marker at seed 2. The test now uses the saved
unique strateg seed. The latest full Release and Debug CTest suites pass 66/66 and include the fixed
oracle; those runs predate the detector-range change above. After that change, an unrestricted
Release build is clean and `test_strateg` passes, but the focused CTest command also reports a
separate Section 162/163 `test_geoscapeframe` failure. Its Part 100.6 UFO landing checks pass.

The test seeds RNG before new-game setup. First-base placement, screen close, and animation progress
are fixture setup; arrival, radar commands, shots, outcome, and map redraw use production world paths.
Retreat is bounded at 500 rendered frames. Terminal combat is bounded at 2000 frames and then waits
up to 60 pulses for the normal close. Failure to reach terminal state fails that case; it does not
send SE or force-close the encounter. The latest full-suite passes include the seed-based oracle fix.
The focused `test_strateg` pass covers the corrected detection limits; the normal current encounter
to intercept/offer flow remains unverified.
