# HANDOFF — PocketUFO Win64 recreation

Written 2026-10-11. Everything below was re-derived from the repository, the build,
the tests, and Git history at this moment. No earlier handoff document was read or
reused (none exists in the tree and none is tracked).

---

## 1. What this project is

Recreate the game `pufobeta.exe` (PocketUFO beta, Windows CE / ARM32, ~641 KB,
1103 functions) as a modern **Win64** program in **C23 + Clang + CMake**, with
behavior parity against the original.

The source of truth is the IDA database `pufobeta.exe.i64` (tracked in this repo,
22.6 MB). Every recreated behavior must be traceable to a decompiled or disassembled
original function, and the port records that as an `@ 0xADDR` citation in a comment.
Guessing behavior is not allowed; ambiguous IDB output is documented and asked about.

Target state, from the project goal file
(`.pi/goals/active_goal_2026091719164895_mu5qflmn-o361c0.md`):

- CMake + Clang builds a Win64 executable with zero errors and zero warnings
  (warnings are errors).
- Every original subsystem is recreated: startup/config, data loading
  (`main.dat`, `lang.dat`, saves), map/world, entities, game logic, UI/input,
  rendering, audio.
- The binary launches on Win64 and the main playable flows work.
- Per-subsystem parity evidence with deviations listed explicitly, plus a final
  parity report mapping original functions to recreated code with no unexplained gaps.

Out of scope by the goal's own boundaries: `save_editor.exe` and companion tools,
original WinCE targets, mobile ports, new features, packaging/installer.

---

## 2. Snapshot of the repository right now

| Item | Value |
|---|---|
| Branch | `master` |
| Work tip before this handoff | `471a4ad` — "game-core: port Unit_RollChance at 0x25e6c (scatter triple at Map+0x15058) + 14 checks" |
| Handoff commit | this document and the `game-core.md` note, committed on top of `471a4ad` |
| Tracked modifications | none (`git diff --stat` empty) |
| Untracked files | listed in section 12, all intentional |
| Build | clean, exit 0, no warnings under `/W4 /WX` |
| Tests | `100% tests passed, 0 tests failed out of 39`, 1.35 s |
| Executable | `build/src/pufo/Release/pocketufo.exe`, 27648 bytes |

`pufobeta.exe.i64` is tracked on purpose. It is the analysis database, and commits
that only re-save it (for example `b4b875c`, `eaa79b0`) carry the IDB comments and
renames that the parity notes depend on.

---

## 3. What has been implemented, and why

Work is organized as the goal's task list. Four tasks are complete with recorded
evidence, one is in progress, two are not started.

| Task | Status | What it produced |
|---|---|---|
| `map-idb` | complete | `docs/idb/MAP.md`, `docs/idb/functions.tsv`, `docs/idb/imports.tsv`, `docs/idb/inventory-grouped.tsv` (all 1103 functions classified into 14 subsystems, zero unclassified). This is the subsystem map the whole source tree mirrors. |
| `scaffold` | complete | CMake + Clang Win64 skeleton, `pufo_options` warnings gate, `wWinMain` app shell, `PtrArray`, `Game_RandU16`, GAPI replacement seam. PE verified `machine=0x8664`, `magic=0x20b`, `subsystem=2`. |
| `data-layer` | complete | `pufo_dat.c` readers for the original archive layout, verified against the shipped `main.dat` / `lang.dat` (directory stride 21 confirmed from both file ends, lang-before-main archive order, case-insensitive lookup). |
| `render-audio` | complete | `pufo_render.c` blitters and `pufo_audio.c` thin interface over the original audio dependency. Two real bugs were found and fixed here: a missing `y2` row term in all five blitters, and an init retry loop. |
| `game-core` | **in progress** | Map/world, entities, unit records, pathing, LOS, turn/visibility, tactical state, combat, ballistics, saves, roster. See the port list below. |
| `ui-input` | not started | Windows, menus, dialogs, input mapping. |
| `integrate-parity` | not started | Full build + run of playable flows on Win64 + final parity report. |

Recent `game-core` commits, newest first, each with its reason:

| Commit | Change | Why |
|---|---|---|
| `471a4ad` | Port `Unit_RollChance @ 0x25e6c` + 14 checks | It was the last missing callee of `Tact_ProcessMoveAction`; the scatter triple lives at `Map+0x15058`. |
| `c6ad716` | Decode `Unit_RollChance` constants | `0.27` and `0.01` are exact; the function is now fully specified. |
| `6d4452f` | Resolve `Unit_RollChance` offsets from ARM | The base is `pCtxBase`, not `pUnit`; it reads the arc bound at `Map+0x14080`. |
| `354bdf7` | Analyze `Unit_RollChance` | Roll structure and the `0..6.28` angle draws settled; Soldier scratch offsets were left open pending ARM. |
| `1ca9a2f` | Dependency survey for `Tact_ProcessMoveAction` | Proved that of its callees only `Unit_RollChance` was missing. |
| `f63dadf` | Coverage script: strip C comments before matching | Comment mentions were inflating coverage by 168 functions; the honest figure was 275/1103, not 443. |
| `8569beb` | `Tact_KillUnit` corpse cell fix + test fix | The kill path moves `pUnit->pCell` to the landed corpse cell (`0x4eaa8`); a test had `pGeom` pointing at a dead compound literal, which collapsed the sweep bounds. |
| `b4b875c` | IDB re-save | Kill-table comment corrections. |
| `b7668b9` | Port `Tact_KillUnit @ 0x4e430` + 30 checks | Kill path, corpse placement, morale shock with both signs, chryssalid transform, fall damage. |
| `eaa79b0` | Correct a Hex-Rays base-register error in the IDB | The chryssalid name copy reads `Map+0x230`, not `0xB1370`; added a `pCtxBase` offset map proven from ARM. |
| `4211d96` | Analyze `Tact_TallyBattleStats` | Ordering decision: the tally subtree needs `Save_Read/WriteTGameData` and `Tact_InitMission` first. |
| `17b4f52`, `465d688` | Fix and brief the `Tact_CheckBattleEnd` helpers | Six small helpers with the `Map` fields each one needs. |
| `cb3cd9d` | Confirm the `Tact_KillUnit` fall-damage term from ARM at `0x4eefc` | Cell `nZ` byte minus the loop layer index, not two indices. |
| `42ea9e5` | Port `UnitList_ForEachRefresh @ 0x22b54`; name three kill tables in the IDB | `g_aDeathSoundByType`, `g_aCorpseObjByType`, `g_aCorpseObjQuadrant`. |
| `8755ada` | Port `Unit_DropAllItemsToCell @ 0x4b4f8` + 10 checks | Belt/backpack/pocket slot map proven from offsets; backpack slots 5 and 6 kept. |
| `44982b1` | `TestTryOpportunityFire` + parity notes | Facing cone excludes straight ahead; the duel is decided by a raw roll, not stats. |

---

## 4. Current architecture and the decisions that matter

### Build layout

- Root `CMakeLists.txt`: `CMAKE_C_STANDARD 23`, `CMAKE_C_EXTENSIONS OFF`, Win32-only
  with a 64-bit guard, `enable_testing()`, and an INTERFACE target `pufo_options`
  that carries `/W4 /WX`. Warnings are build errors, and the build is currently clean.
- `src/pufo/CMakeLists.txt`: static libraries `pufo_platform`, `pufo_util`,
  `pufo_dat`, `pufo_render`, `pufo_game`, `pufo_ui`, `pufo_audio`, then
  `add_executable(pocketufo ...)` with `WIN32_EXECUTABLE TRUE`. That property is
  required: the original entry point is `WinMain @ 0x9fcc4`, the port uses
  `wWinMain`, and without the property `lld-link` fails on `undefined symbol: main`.
- Generator on this machine: `Visual Studio 17 2022`, `-A x64`, `-T ClangCL`.
- Each test is a five-line block: `add_executable`, `target_link_libraries(... PRIVATE
  pufo_options pufo_game pufo_util)`, `RUNTIME_OUTPUT_DIRECTORY` set to
  `build/tests`, and `add_test(... WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`. The
  working directory matters: data tests read `main.dat` and `lang.dat` from the repo
  root. 39 suites are registered.

### Source layout mirrors the IDB subsystem map

`src/pufo/{platform,util,dat,render,audio,game,ui,asset,app}`. The `game/` group is
the bulk of current work: `pufo_unit.c` (63 KB), `pufo_combat.c`, `pufo_tactui.c`,
`pufo_tactstate.c`, `pufo_ballist.c`, `pufo_mapcell.c`, `pufo_los.c`, `pufo_path.c`,
`pufo_turn.c`, `pufo_vistry.c`, `pufo_roster.c`, `pufo_save.c`, `pufo_tactctl.c`,
`pufo_strateg.c`, `pufo_geoscape.c`, `pufo_clock.c`, `pufo_ship.c`, `pufo_enemyai.c`,
`pufo_isodraw.c`, `pufo_font.c`, `pufo_rmp.c`, `pufo_template.c`, `pufo_boot.c`.

### Context injection instead of global state

The originals read `pCtxBase` and a dozen globals directly. The port passes that state
in explicitly so the algorithms stay testable without a live game context:

| Seam | File |
|---|---|
| `CombatCtx` + `ShotHooks` | `game/pufo_combat.h` |
| `DoorActionCtx`, `FireBurstSpec` | `game/pufo_tactstate.h` |
| `IsoAnimGlobals` | `game/pufo_isodraw.h` |
| `MapCellLookup` function pointer | `game/pufo_mapcell.h` |
| `TurnUpdateVisibilityFn` | `game/pufo_turn.h` |
| `RosterHooks`, `StatusHooks` | `game/pufo_roster.h`, `game/pufo_statusmsg.h` |
| `KillCtx`, `KillHooks` | `game/pufo_unit.h` |

Arithmetic is never changed by injection. Where the port had to deviate from the
original, the deviation and its reason are written down in `docs/parity/*.md` — for
example `Unit_RemoveAt` does not reproduce the original's out-of-range free, and
`Path_EnqueueCell` refuses to enqueue when the frontier is full because the original
has no capacity check.

### ILP32 fidelity

The original is 32-bit. Struct layouts are pinned with `_Static_assert` so a Win64
pointer widening cannot silently change a field offset: `Soldier` at
`PUFO_SOLDIER_SIZE_ILP32` = 1044 bytes, `MapCell` 60 bytes, `Mc1Rec` 52 bytes,
`TactItem` 24 bytes, `TactQueueEntry` 56 bytes. `TactQueueEntry` is the one place
where the Win64 offsets deliberately differ from ILP32, because that queue is never
serialised.

### Soft-FPU thunks

The original calls ARM soft-FPU helpers through `.idata`. The port maps them:

| Thunk | Address | Note |
|---|---|---|
| `_itod` | `0xa6014` | int to double |
| `_muld` | `0xa6024` | double multiply |
| `_dtoi` | `0xa602c` | double to int, truncates toward zero (documented assumption) |
| `_rt_sdiv` | `0xa600c` | divisor in R0, dividend in R1 |

### Random numbers

`Game_RandU16` (`src/pufo/util/pufo_rand.c`) reproduces
`(u16)((float)(rand() % 32768) * (1/32768f) * (float)nMax)`, range `[0, nMax)`.
This depends on the CRT: MSVC's `rand()` returns `0..32767`, other CRTs do not. The
modulo 32768 keeps the original distribution on MSVC. This is a real portability
constraint, recorded in `docs/BUILD.md`.

### Audio seam

`Sound_Play(i32 nSample)` takes **one** argument in the original. `Sound_SetSfxMuted`
is a port seam that deliberately keeps that shape rather than changing the signature.

### Evidence rule

Every recreated behavior names `@ 0xADDR` in a comment. That is not decoration: both
coverage scripts measure coverage by finding those citations. A port without a
citation is invisible to the parity report.

---

## 5. What currently works

- Configure, build, and a clean warning-free compile of all 39 test binaries plus the
  game executable.
- 39 test suites pass. They are behavior tests against values read out of the IDB, not
  smoke tests. `PARITY.md` reports 377 checks for `test_tactui`, 199 for `test_unit`,
  152 for `test_tactstate`, 112 for `test_mapcell`. Suites that print their own summary
  line instead report 0 there even though they do count checks — see issue 3 in
  section 7.
- Data tests run against the real shipped `main.dat` and `lang.dat` in the repo root.
- The executable launches, creates its window, and stays alive (no startup crash).
- The IDB itself is being improved as part of the work: functions renamed, comments
  added, and two Hex-Rays misreadings corrected in place.

What is **not** working yet: there is no end-to-end playable flow. The app shell does
not yet drive the map, tactical, or strategic screens. That is exactly what the
`ui-input` and `integrate-parity` tasks are for.

---

## 6. What remains incomplete

Coverage, measured three ways (see section 7 for why they disagree):

| Measure | Result |
|---|---|
| `python tools/coverage.py` | 1103 functions, **276 recreated**, 791 gaps, 36 artifacts |
| `python docs/parity/coverage.py` | **419 / 1103 cited** (38.0%), bytes 174692 / 598792 (29.2%) |
| `python docs/parity/gen_report.py` | **382 / 1103** (34.6%), bytes 171112 / 598792 (28.6%) |

Per-subsystem citation coverage from `docs/parity/coverage.py`:

| Subsystem | Cited | Total | Pct |
|---|---|---|---|
| audio | 6 | 6 | 100% |
| dat | 17 | 18 | 94% |
| app-shell | 21 | 24 | 88% |
| util | 17 | 21 | 81% |
| tact | 172 | 214 | 80% |
| ui-core | 59 | 85 | 69% |
| render | 33 | 51 | 65% |
| tact-ui | 20 | 52 | 38% |
| save | 16 | 35 | 46% |
| boot-assets | 2 | 4 | 50% |
| strat-ui | 27 | 243 | 11% |
| strat | 27 | 205 | 13% |
| platform | 2 | 132 | 2% |
| titles | 0 | 13 | 0% |

The two hard gaps are `platform` (WinCE API surface: 2 of 132) and `titles` (0 of 13),
followed by the two strategy layers (`strat` 13%, `strat-ui` 11%). `tact` is the most
complete large subsystem at 80%.

Specific unfinished items inside `game-core`:

- `Tact_ProcessMoveAction @ 0x4f3f0` — not ported. Its dependency survey is done and
  every callee is now available. See section 8 for the analysis carried over from the
  previous session, which is **not verified**.
- `Tact_TallyBattleStats @ 0x3348c` — analysed, deliberately not ported. It needs
  `Save_Read/WriteTGameData` and `Tact_InitMission` first, because `unk_138E6C` and
  `unk_138E70` are saved mission state.
- `Tact_KillUnit @ 0x4e430` — ported, but the spotted-flag sweep tail is deliberately
  unasserted: it was ported without being verified, and no test claims it
  (commits `b7668b9`, `8569beb`).
- `docs/parity/coverage.md` lists 126 cited addresses that are not in the function
  inventory (thunks, data symbols, or typos). Those need triage before the final
  parity report can claim "no unexplained gaps".

---

## 7. Known issues, assumptions, risks, and technical debt

Reported so they can be handled as separate issues; none of them were fixed during
this handoff.

1. **`docs/BUILD.md` is stale.** Its "Source layout" section lists only the scaffold
   files and states that `dat`, `save`, `ui-core`, `tact`, `tact-ui`, `strat`,
   `strat-ui`, `render`, `audio`, `titles`, `boot-assets` are "still to be added".
   All of them exist now. The toolchain table and the build commands in that file are
   still accurate.

2. **Three coverage numbers, two parsers.** `tools/coverage.py` matches function names
   against `docs/idb/functions.tsv` in the source (comments stripped, per `f63dadf`).
   `docs/parity/coverage.py` counts addresses cited in the parity docs and also accepts
   slash-runs like `@ 0xA/0xB`. `docs/parity/gen_report.py` uses a single-address
   regex only. The 382 vs 419 difference is a parser rule, not a code change. Pick one
   definition before publishing a headline number.

3. **`PARITY.md` under-reports test checks for 9 suites.** `gen_report.py` runs each
   test binary and counts the literal string `ok  ` in its stdout
   (`docs/parity/gen_report.py`, `checks = out.count("ok  ")`). Suites that print one
   `ok  ` line per assertion get a real count; suites that print a summary line instead
   report 0. Actual counts, measured by running each binary directly:

   | Suite | `PARITY.md` says | Actual |
   |---|---|---|
   | `test_ballist` | 0 | 77 checks, 0 failures |
   | `test_combat` | 0 | 102 checks, 0 failures |
   | `test_fov` | 0 | 31 checks, 0 failures |
   | `test_vistry` | 0 | 42 checks, 0 failures |
   | `test_blast` | 0 | 14 check(s), 0 failure(s) |
   | `test_vis` | 0 | 70 check(s), 0 failure(s) |
   | `test_cursordir` | 0 | prints `PASS: 0 failure(s)`, no count |
   | `test_input` | 0 | prints `PASS: 0 failure(s)`, no count |
   | `test_soldiername` | 0 | prints `PASS: 0 failure(s)`, no count |

   `test_clock`, `test_geoscape`, `test_ship`, and `test_strateg` print
   `PASS (0 failures)` and are reported correctly as 0 by the same rule. The clean fix
   is one uniform output format across all suites.

4. **Hex-Rays base-register errors are a recurring hazard in this IDB.** Two confirmed
   so far (`eaa79b0`, `b4b875c`). The `Map` type in the database has **one member**, so
   every field access renders as `gap_*` or `MEMORY[0x1139B8]`, and several generated
   names are actively misleading. The working method is to prove offsets from ARM
   disassembly, not from pseudocode. A comment at `0x1139B8` (`pCtxBase`) now lists the
   offsets proven that way, including `+0x230`, `+0x51 bFireStatus1`,
   `+0x5A bGameTickHold`, the `+0x4EBC` hit windows, and the counter at
   `+0x4EBC + 4*49940`.

5. **The `Map` struct cannot be rebuilt headlessly.** IDA 9.4 has no `ida_struct`
   module and no `put_udt_details`, so the offsets live as listing comments until the
   struct is rebuilt in the GUI. Until then, expect pseudocode to keep lying about
   `Map` field names.

6. **`docs/parity/game-core.md` is 3977 lines / 207 KB.** Append with `Add-Content`;
   read a window with `Get-Content ... | Select-Object -Skip N -First M`. Never rewrite
   the file in one pass.

7. **Assumption on record:** `_dtoi` truncates toward zero. It is used across the
   combat and ballistics ports and has not been disproved, but it has not been proven
   against the ARM either.

8. **Deliberate deviations** are listed with reasons in `docs/parity/*.md` (the
   "Deviations, each with its reason" tables). Do not "fix" those back toward the
   original without reading the reason first — several reproduce a real bug in the
   original, and the port chose not to.

---

## 8. Analysis carried over from the previous session (UNVERIFIED)

This block is here so the work is not lost. It was produced in the previous session and
**nothing in it was committed then**. Treat it as leads to re-derive, not as fact.

Target: `Tact_ProcessMoveAction @ 0x4f3f0` — 5868 bytes, 236 blocks, cyclomatic
complexity 173, 875 pseudocode lines. The prior session claims a 19-case breakdown of
the move FSM, plus these `TactState` scratch offsets:

| Name | Offset |
|---|---|
| `nStepFrameDiv` | `0x04` |
| `pStepFromCell` | `0x08` |
| `nStepZDelta` | `0x0C` |
| `nStepDrawOffX` / `nStepDrawOffY` | `0x1119C` / `0x111A0` |
| `nStepHeightSrc` / `nStepHeightDst` | `0x111B0` / `0x111B4` |
| `nTraceX` / `nTraceY` / `nTraceZ` | `0x111BC` / `0x111C0` / `0x111C4` |
| `nQueueLen` | `0x111D4` |
| `bQueueActive` | `0x111D6` |
| `nPhaseTick` / `nPhaseMax` | `0x111D8` / `0x111DC` |
| `bUnk70114` / `bUnk70115` | `0x111E2` / `0x111E3` |

Four ARM ranges were flagged as still needing verification:

1. `0x4F718..0x4F794` — the large-unit 2x2 occupancy probe.
2. `0x4F8B0..0x4F8EC` — the `nStepHeightDst` +/-40 comparison.
3. `0x4FB20` together with `TactState_EnqueueVertStep @ 0x49ddc` — `entry.nTuCost`
   apparently reused as a `MapCell *`.
4. `0x50A40..0x50AD0` — the civilian swap path.

Two data tables are used by this function and **their extents are still unknown**:
`0xA9768` and `0xAA168`. What was checked in the current session, and only this:
`xrefs_to 0xA9768` returns `0x5008c` and `0x50358`, and `xrefs_to 0xAA168` returns
`0x50338` and `0x50354` — all four inside `Tact_ProcessMoveAction`. A 2048-byte window
at `0xA9768` runs past the end of its table; the word table at `0xAA168` ends before
+0x1A0, where dword data begins. Name lookups over `0xA9700..0xAA400` returned nothing.

Do not confuse `0xAA168` with a neighbouring table that is already pinned elsewhere:
`pufo_combat.h` defines `off_AA040 @ 0xaa040`, 360 bytes = 180 u16, as
`PUFO_SHOT_SOUND_COUNT 180` / `g_aShotSound`.

---

## 9. How to build, run, test, and verify

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build --config Release
ctest --test-dir build -C Release
```

Run the game: `build\src\pufo\Release\pocketufo.exe`. Run one suite directly when you
need its output, from the repo root (data tests need `main.dat` / `lang.dat` there):

```powershell
build\tests\Release\test_unit.exe
```

Regenerate the parity report:

```powershell
python docs/parity/gen_report.py     # writes PARITY.md
python docs/parity/coverage.py       # cited functions/bytes per subsystem
python tools/coverage.py             # recreated functions by name match
```

Verification gates that must stay green: clean build under `/W4 /WX`, 39/39 tests, and
`PARITY.md` regenerated byte-identical when nothing meaningful changed.

IDA: the live MCP session for this project is `pufobeta.exe.i64`. Use
`decompile` with `include_addresses: true` for large functions (875 pseudocode lines
fit); `analyze_function` truncates on them. Prefer xrefs, operand/immediate queries,
call graphs, and byte patterns over plain text search.

---

## 10. Current objective

The active goal is `game-core` — "Recreate core game: map/world, entities, and game
logic" — with `blockCompletion: true`, so the goal cannot be marked done while any task
is open. Goal file: `.pi/goals/active_goal_2026091719164895_mu5qflmn-o361c0.md`
(revision 3521).

Note on state: the goal file itself still reads `status: active` / `phase: running`,
but the user paused the goal for this handoff. No new feature work was done while
preparing this document.

---

## 11. Next milestones, in priority order

1. **Port `Tact_ProcessMoveAction @ 0x4f3f0`.** All callees now exist. Re-derive the
   scratch offsets and the 19-case breakdown from ARM rather than trusting section 8;
   verify the four flagged ARM ranges first (`0x4F718..0x4F794`,
   `0x4F8B0..0x4F8EC`, `0x4FB20` + `TactState_EnqueueVertStep @ 0x49ddc`,
   `0x50A40..0x50AD0`).
2. **Dump the two move tables at `0xA9768` and `0xAA168`.** Determine exact extents and
   element types from ARM, name them in the IDB, and add them to the port as pinned
   arrays the way `g_aShotSound` is pinned in `pufo_combat.h`.
3. **Assert the `Tact_KillUnit` spotted-flag sweep.** It is ported but unverified;
   prove the sweep bounds and semantics from ARM, then add checks.
4. **Fix the reporting debt** before it distorts decisions: one uniform test output
   format so `PARITY.md` stops reporting 0 checks for 9 suites; one agreed coverage
   definition; refresh `docs/BUILD.md` source layout.
5. **`ui-input` task.** The `UiPanel` type conflict is already resolved — the
   7092-byte layout is the real one, and the two views (16-byte lines with colour
   slots at `+0xC8..+0xD4` versus 4-byte `apLines[500]` at `+0xBE4`) are mutually
   exclusive. Start from `src/pufo/ui/` and `docs/parity/ui-core.md`.
6. **`Tact_TallyBattleStats @ 0x3348c` subtree**, once `Save_Read/WriteTGameData` and
   `Tact_InitMission` are in place.
7. **`integrate-parity` task.** Wire the app shell into a real playable flow on Win64,
   triage the 126 uncited/not-in-inventory addresses, and produce the final parity
   report with no unexplained gaps.

---

## 12. Important files and entry points

| Path | Why it matters |
|---|---|
| `pufobeta.exe.i64` | The source of truth. Tracked. |
| `AGENTS.md` | Project rules: C23/Clang/CMake, ask don't assume, KISS/DRY/YAGNI, no unrelated edits. |
| `.pi/goals/active_goal_2026091719164895_mu5qflmn-o361c0.md` | Objective, success criteria, boundaries, verification contract, task list. |
| `CMakeLists.txt`, `src/pufo/CMakeLists.txt` | Build gates, library split, 39 test registrations. |
| `src/pufo/app/pufo_app.c` | `wWinMain` + `WndProc`. The place where integration will happen. |
| `src/pufo/game/pufo_unit.c` / `.h` | Largest module (63 KB). Units, kill path, `Unit_RollChance`. |
| `src/pufo/game/pufo_tactstate.c` / `.h` | Tactical queue and action state; where `Tact_ProcessMoveAction` will land. |
| `src/pufo/game/pufo_combat.c` / `.h` | Shot resolution, pinned tables such as `g_aShotSound`. |
| `src/pufo/game/pufo_mapcell.c` / `.h` | `MapCell` layout and cell lookup seam. |
| `docs/idb/inventory-grouped.tsv` | The 1103-function subsystem map that the tree mirrors. |
| `docs/parity/game-core.md` | 3977 lines of current-task evidence. Append-only. |
| `docs/parity/ui-core.md` | Starting point for the `ui-input` task. |
| `PARITY.md` | Generated report. Do not hand-edit; regenerate with `gen_report.py`. |
| `docs/BUILD.md` | Toolchain and commands (source layout section is stale). |

### Deliberately uncommitted files

These are original game assets, tools, and local scratch. They are not ignored by
`.gitignore`, and they must stay untracked:

```
.agents/  .claude/  .ida-mcp/  .pi/
0000SKKV.000  1  7z.exe  7za.dll  7zxa.dll  _setup.xml  asd
fmodce.dll  gx.dll  lang.dat  main.dat  pUFO1.26RUS.cab  pufobeta.exe
save_editor.exe  wcecabinfo.exe
```

`lang.dat` and `main.dat` are needed at the repo root by the data tests, so they must
not be deleted even though they are untracked. `build/`, `.tmp/`, `__pycache__/`,
`*.id0/1/2`, `*.nam`, `*.til`, and `.tmp_analyze*.json` are ignored.
