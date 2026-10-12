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
| Work tip before this handoff | `665943c` - parity: a rename ledger, and a bug in the citation audit it exposed |
| Handoff commit | this document and the `game-core.md` note, committed on top of `471a4ad` |
| Update 2026-10-11 | `Tact_ProcessMoveAction` is ported and now runs against a real context. `GameMap` (the IDB `Map` block, 152632 bytes, base `0x1139b8`) plus `Map_LookupCell` and the three real callbacks live in `src/pufo/game/pufo_mapctx.c`. Earlier in the same day: TactState layout proven, four flagged ARM ranges settled, both move tables pinned, and three wrong tables in the port corrected (unit-type rows 16 -> 24, `g_aUnitTypeIsLarge` types 11/13, shot-sound extent 180 -> 148). New `g_aUnitMuzzleOffset` and `g_aFireSoundBySortKey`. |
| Tracked modifications | none (`git diff --stat` empty) |
| Untracked files | listed in section 12, all intentional |
| Build | clean, exit 0, no warnings under `/W4 /WX` |
| Tests | `100% tests passed, 0 tests failed out of 47`, 1.42 s, **5701 checks** (see `PARITY.md`) |
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
| `ui-input` | **in progress** | Keyboard handlers, the confirm key `Input_OnConfirm @ 0x15f3c`, the secondary key `Input_OnSecondary @ 0x156e0`, the control press/release handler `UiControl_HitTest @ 0x1a3d8`, screen/modal routing, cancel and escape paths. Windows, menus and dialogs are still to do. |
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


### The commit table above is not exhaustive

It stops at `44982b1`; 160+ commits have landed since. The short version of that stretch, by
cluster, with details in `docs/parity/*.md`:

- **Save subsystem, 37 of 38 functions.** Every chunk writer and reader is ported: product, bases,
  ships, flobjs, strateg, map, teamunits (four roster chunks), tgamedata, smap, rmp, plus
  `SaveGame @ 0x44988` and `SaveSlot_FormatList @ 0x45904`. `LoadGame @ 0x44bbc` is the only one
  left. Each chunk's on-disk layout, version gates on the save-format byte `0x18655C`, and read
  order are recorded in `docs/parity/save-format.md`.
- **Geoscape UI lifecycle.** `GeoscapeUi_FreeAll @ 0x86d20` is ported as a function over an
  owned-object struct (`ui/pufo_geouimanage.{h,c}`), which also settles that 0x1864F8 is one
  struct holding the thirteen root widgets, the strateg PtrArray at +0x3C and the mission-site
  table at +0xC8. That leaves `LoadGame` blocked on three functions, not four:
  `Geoscape_InitUI` (0x857f4), `Tact_Shutdown` (0x3d568) and `Tact_InitMission` (0x3c550).
  `Tact_Shutdown`'s own five missing callees are down to one: `Base_TransferTacticalLoot`
  (0x68344). `Map_FreeRmpNodes`, `Soldier_RemoveById`, `Ship_RemoveWithCargo @ 0x36830`,
  `Craft_GetTypeCargoCap @ 0x65ac4`, `Base_CalcWeaponStorageUsed @ 0x65b30`,
  `Craft_LoadStoresFromBase @ 0x693ec` and `Base_StripAllCraftCargo @ 0x69a38` are now ported,
  and `Base_DemolishFacility @ 0x66b70` is fully mapped. That last port also fixed a real bug in
  `PtrArray_Remove`, which dropped the whole array whenever the removal hit the last slot.
- **Base facility layer.** All five `Base_Update*Needed` passes, their `Base_Calc*Cap` and
  `Base_Count*` helpers, `Base_FindFacilityCell @ 0x6ae54`, `Base_CountEmptyFacilityCells @ 0x6aee0`
  and `Base_ClearFacilityGridRow @ 0x6a0e0` (whose name is wrong: it classifies, it does not clear).
  The three per-base grids and their strides are in `docs/parity/game-core.md`.
- **Loadout weapon swap, whole.** `BasePlaneScreen_ApplyWeaponSwap @ 0x79748` with
  `BasePlane_Load/UnloadWeaponStores @ 0x793e0/0x790e8` and the `InvSel_*ItemQty` cargo pair. The
  key finding is that a weapon and its ammo are **separate object records**, which is why the swap's
  call order does not move ammo.
- **The UFO mission machine.** `Geoscape_SpawnMissionSite @ 0x7e49c` (4460 bytes) plus its whole
  dependency chain: `StrategList_AddItem`, `RollCraftStats`, `AddUfoAt`, `AddAlienSiteIfValid`,
  `Entry_InitTravel`, `MaybeSpawnCraft_Process`, `RemoveAt`, `PurgeType5` (whose cascade bug is
  reproduced on purpose), `FindIdxForShip`, and `BasePlaneScreen_AddInterceptMission @ 0x7de14`.
- **Parity report integrity.** Three audit tools now run before every commit:
  `coverage.py --gaps`, `audit_credits.py` (a port claim on the same line as a port denial) and
  `audit_names.py` (credited addresses with no definition). Six passes caught false credits, the
  largest removing 19 functions and 14988 bytes at once. The trap and how to avoid it are in
  section 7.

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

- Configure, build, and a clean warning-free compile of all 47 test binaries plus the
  game executable.
- 47 test suites pass. They are behavior tests against values read out of the IDB, not
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

Coverage has **one agreed definition**: an original function counts as recreated when
the port cites it in a source comment of the form `@ 0xADDR`, with slash-runs like
`@ 0x17790/0x177fc/0x17868` split so every address in the run counts. That rule lives
in `docs/parity/coverage.py`, and `docs/parity/gen_report.py` imports it, so the two
cannot disagree.

That rule counts a citation, not an implementation, so three audits run beside it and each
catches a different way of being wrong:

- `audit_credits.py` flags a citation whose line also contains a denial ("not ported",
  "unported", "hook", "seam", "stub"). A comment can describe a function as missing and
  credit it in the same breath.
- `audit_names.py` cross-checks every credited address against a definition in `src/pufo`.
  This is the one that catches the **hook-field trap**: a comment naming an original the host
  is expected to supply, written in the `Name @ 0xaddr` form on a field of a `Hooks` struct.
  coverage.py cannot tell that from a real port, so the seam gets credited as the recreation.
  Nineteen functions and 14988 bytes came out of that in one pass.
- Both audits produce flags, not verdicts. The port renames and merges families on purpose,
  so `MapView_FocusOnUnit @ 0x2c758` credited through `MapView_FocusOnCell` is correct and
  must survive the check.

The safe form for anything the port does not implement is `Name (0xaddr)`: the tool does not
give the parenthesized form any credit.

| Measure | Result |
|---|---|
| `python docs/parity/coverage.py` (agreed) | **606 / 1103 cited** (54.9%), bytes 191232 / 598792 (31.9%) |
| `python docs/parity/gen_report.py` | same numbers, plus the per-function map in `PARITY.md` |
| `python docs/parity/audit_credits.py` | citations that sit next to a denial, which must be zero |
| `python docs/parity/audit_names.py` | credited addresses whose original name has no definition; catches hook seams credited as recreations |
| `python tools/coverage.py` (different question) | 276 / 1103 original **names** matched by a definition in `src/pufo` |

`tools/coverage.py` is a name-match audit, not the coverage number: it asks "does a
function with this name exist in the port?", which misses a ported family cited once
and counts a renamed port function as unmatched. Use it to spot names the citation
rule missed, not as a headline.

Per-subsystem citation coverage from `docs/parity/coverage.py`:

| Subsystem | Cited | Total | Pct |
|---|---|---|---|
| audio | 6 | 6 | 100% |
| save | 34 | 35 | 97% |
| dat | 17 | 18 | 94% |
| app-shell | 22 | 24 | 92% |
| util | 18 | 21 | 86% |
| tact | 173 | 214 | 81% |
| ui-core | 65 | 85 | 76% |
| render | 32 | 51 | 63% |
| tact-ui | 33 | 52 | 63% |
| boot-assets | 2 | 4 | 50% |
| strat-ui | 103 | 243 | 42% |
| strat | 68 | 205 | 33% |
| titles | 0 | 13 | 0% |
| platform | 2 | 132 | 2% |

The two hard gaps are `platform` (WinCE API surface: 2 of 132) and `titles` (0 of 13),
followed by the two strategy layers (`strat` 13%, `strat-ui` 11%). `tact` is the most
complete large subsystem at 80%.

Specific unfinished items inside `game-core`:

- `Tact_ProcessMoveAction @ 0x4f3f0` — ported, wired to `GameMap`, and every
  handled command runs end to end in `test_mapctx`. See section 8 for the
  analysis and `docs/parity/game-core.md` for the wiring.
- The UI and inventory regions of the original `Map` block are not in `GameMap`
  yet; they are added when a consumer needs them.
- `Tact_TallyBattleStats @ 0x3348c` — analysed, deliberately not ported. It needs
  `Save_Read/WriteTGameData` and `Tact_InitMission` first, because `unk_138E6C` and
  `unk_138E70` are saved mission state.
- `Tact_KillUnit @ 0x4e430` — ported, and the spotted-flag sweep is now
  verified from ARM and asserted in `test_vis` (window `nX±8` / `nY±8` clamped to
  `10 * nMapDim - 1`, four layers, `MapCell+0x2C` bit 0 cleared, gated on
  `Map_bSweepArmed` @ `0x113A20`).
- `Input_OnConfirm @ 0x15f3c` — ported (the confirm key: a 16-case `nUiScreen` switch),
  asserted by `test_input` (94 checks). `Geoscape_CloseUI` and `Inv_CloseAndCommit` are
  still injected hooks because neither is ported. One open IDB question came out of it:
  the view-cell unit array at `Map+0x140F0` overlaps `Map.apCells @ 0x15338` in the `Map`
  type, so the type's `aszStatusMsg` / `gap_TrajRest` split is suspect. See
  `docs/parity/game-core.md`.
- `PARITY.md` lists 132 citations that are not function starts, split by the image-base
  rule into 31 struct-field offsets and 101 entries that mix global data symbols with
  block-relative offsets. That split needs IDB triage before the final parity report can
  claim "no unexplained gaps".

---

## 7. Known issues, assumptions, risks, and technical debt

Reported so they can be handled as separate issues. Items 1-3 were open at the
handoff and are now closed; the rest are still open.

1. ~~**`docs/BUILD.md` is stale.**~~ **Closed.** Its "Source layout" section now lists
   the real tree (`app`, `platform`, `util`, `dat`, `asset`, `render`, `audio`, `ui`,
   `game`, `tests`) and a new "Test harness" section records the output format.

2. ~~**Three coverage numbers, two parsers.**~~ **Closed.** The citation rule in
   `docs/parity/coverage.py` is the single agreed definition (see section 6), and
   `gen_report.py` now imports `CITE_RE`, `ADDR_RE`, `INVENTORY`, and `load_inventory`
   from it instead of keeping its own single-address regex. Both report
   **429 / 1103 (38.9%)** and 198444 / 598792 bytes. `tools/coverage.py` is kept as a
   name-match audit and is labelled as a different question.

3. ~~**`PARITY.md` under-reports test checks for 9 suites.**~~ **Closed.**
   `tests/pufo_test.h` is the shared harness: `TestCheck`, `TestEqI32`, `TestEqU16`,
   `TestNear`, `TestSummary`, and the fixed output format
   `  ok   <what>` / `  FAIL <what>` / `<suite>: N checks, M failures`. The 11 suites
   that printed nothing on a pass (`ballist`, `blast`, `combat`, `cursordir`, `fov`,
   `input`, `mapctx`, `moveaction`, `soldiername`, `vis`, `vistry`) now use it, keeping
   their local helper names as thin wrappers so no call site changed. `gen_report.py`
   reads the summary line (`count_checks`) and falls back to counting `ok`/`FAIL` lines.
   All 41 suites report a real count. Side effect: `test_vis`'s old `CheckEqI32` printed
   `FAIL` twice and counted the check once; the shared helper does both correctly.

4. **Hex-Rays base-register errors are a recurring hazard in this IDB.** Two confirmed
   so far (`eaa79b0`, `b4b875c`). The `Map` type now has **156 members** (size 152632,
   verified by `type_inspect`), so pseudocode renders named fields at the named offsets
   — but about 40 members are still `gap_*` / `pad*` blobs, and some named members
   disagree with ARM-proven reads. Example: the move FSM reads its shot-history cell
   array at `Map+0x30` (`nStatusDrawY @ 0x2C` plus 4, indexed by the shot count) while
   the type calls `Map+0x30` `nMapTileW` / `nMapTileH`. The working method is still
   to prove offsets from ARM disassembly, not from pseudocode. A comment at `0x1139B8`
   (`pCtxBase`) lists the offsets proven that way, including `+0x230`, `+0x51
   bFireStatus1`, `+0x5A bGameTickHold`, the `+0x4EBC` hit windows, and the counter at
   `+0x4EBC + 4*49940`.

5. **`Map` member edits still need the IDA GUI.** IDA 9.4 has no `ida_struct` module and
   no `put_udt_details`, so the remaining `gap_*` regions inside `Map` cannot be split
   from a script. The type exists and is usable; only new or renamed members require the
   GUI. Until those gaps are filled, expect pseudocode to keep lying about `Map` field
   names in the unnamed regions.

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

## 8. Tact_ProcessMoveAction @ 0x4f3f0 — ported and wired to `GameMap`

Everything the previous session left as a lead has now been re-derived from ARM and
recorded in `docs/parity/game-core.md` (section "2026-10-11: Tact_ProcessMoveAction
@ 0x4f3f0 pre-port verification + three table fixes"). Summary:

- `TactState` layout proven end to end: header at 0x00..0x17 (bFlag0, bFlag1,
  nStepFrameDiv 0x04, pStepFromCell 0x08, nStepZDelta 0x0C, nQueueCursor 0x10),
  `aQueue[1250]` at 0x18, tail scratch from 0x11188 (nPhaseCounter 0x1118C,
  nQueueLen 0x111D4, bQueueActive 0x111D6, nPhaseTick 0x111D8, nPhaseMax 0x111DC,
  bUnk70114 0x111E2, bUnk70115 0x111E3). Total 0x111E8 = 70120, which matches the size
  IDA reports for the type. The previous session's scratch offsets were right.
- The entry address is folded as `(pState + 56*idx) + 0x18`, so ARM displacements are
  entry offset + 0x18. The port's `TactQueueEntry` offsets are confirmed correct.
- All four flagged ARM ranges are settled: the 2x2 probe (deltas +0/+1/+60/+61,
  occupant at MapCell+0x30), the step-height pair (sprite height is the negated signed
  byte at Mc1Rec+0x24, not Hex-Rays' `gap_22[14]`), the climb target cell (cell grid at
  ctx+0x11AF8, index 3600*z + 60*y + x; the entry field at +0x3C really is reused as a
  `MapCell *`), and the civilian swap (apCivilians ctx+0x15244, apEnemyUnits ctx+0x15240,
  count ctx+0x15120; the real semantics are `apCivilians[idx] =
  apCivilians[nCivilianUnitCount-1]` then decrement — Hex-Rays' `+29` is a base artifact).
- OPEN, found while verifying range 3: the cell-pointer array the ARM actually uses
  (ctx+0x11AF8, at 0x4FB4C and again in the map-wide `bFlags29 &= ~0x20` sweep at
  0x4FE38) sits exactly one layer (14400 bytes) below the `apCells` base recorded in
  `docs/parity/game-core.md` and `pufo_mapcell.h` (ctx+0x15338). Both cannot be
  14400-dword arrays. Settle this before wiring the port's `MapCellLookup` to the real
  context; the injected-lookup tests are unaffected either way.
- Both move tables are pinned in the port and named in the IDB: `g_aUnitMuzzleOffset`
  @ 0xA9768 (23 type blocks x 2 groups x 8 facings x 3 i16) and `g_aFireSoundBySortKey`
  @ 0xAA168 (148 u16 by `ObjectRec.uSortKey`).
- `tools/pe_off.py` and `tools/dump_muzzle.py` now dump pinned tables straight out of
  `pufobeta.exe`. IDA addresses in this database are RVA + 0x10000.

What is left: nothing structural. All 15 handled commands now run end to end
against `GameMap` in `test_mapctx`. The context fields the FSM reads are all
present in `GameMap` (`nLayerScratch`, `nMaxLayerClamp`, `nAimOffX/Y/Z`,
`nShotCount`, the three rosters and their counts, `bFireStatus1`,
`nFireTickProgress`, `adMtX/Y/Z`, `pSelWeapon`); `Map_BuildMoveActionCtx` points a
`MoveActionCtx` at them.

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

1. ~~Wire `Tact_ProcessMoveAction @ 0x4f3f0` to the real context~~ — done. `GameMap`
   (the IDB `Map` block) plus `Map_LookupCell`, `Map_BuildMoveActionCtx` and the
   real `Unit_bArmorTuWarn` / `StatusMsg_Push` / `Unit_UpdateVisibility` callbacks
   are in `src/pufo/game/pufo_mapctx.c`, with `test_mapctx` (83 checks) driving
   commands 1, 3, 0xE, 0x11 and 0x12 against live state.
2. ~~Dump the two move tables~~ — done. `g_aUnitMuzzleOffset` @ 0xA9768 and
   `g_aFireSoundBySortKey` @ 0xAA168 are pinned in the port and named in the IDB.
   Three existing tables were found wrong and corrected in the same commit.
3. ~~Assert the `Tact_KillUnit` spotted-flag sweep~~ — done. The bounds are proven
   from the ARM at `0x4F24C..0x4F2B8` and `test_vis` now asserts them (window,
   clamp, all four layers, bit-only clear, and the gate off). The three gate bytes
   are named in the IDB: `Map_bTickHold` @ `0x113A12`, `Map_bSweepArmed` @
   `0x113A20`, `Map_bFrameScratch` @ `0x138E74`.
4. ~~**Fix the reporting debt** before it distorts decisions~~ — done. One shared test
   harness (`tests/pufo_test.h`) gives every suite the same output format; the agreed
   coverage definition is the citation rule in `docs/parity/coverage.py`, which
   `gen_report.py` now imports; `docs/BUILD.md` lists the real source layout. See
   section 7 items 1-3.
5. **`ui-input` task.** The whole pointer chain is ported and asserted:
   `Input_OnConfirm @ 0x15f3c`, `Input_OnSecondary @ 0x156e0`,
   `UiControl_HitTest @ 0x1a3d8`, `Input_OnPointer @ 0x13c6c` and the release
   pass `Input_OnPointerUp @ 0x148d0` (`test_input` 246 checks, `test_control`
   `TestHitTest`), including the door action, the face-and-kick tail, the widget
   bank `pBank @ 0xAC6D0`, the camera drag and the action-cost table at
   `Map+0x4E4C`. The HUD action buttons the pads call are ported too:
   `TactHud_ToggleKneel @ 0x16570`, `TactHud_ClimbUp @ 0x1674c` and
   `TactHud_ClimbDown @ 0x16af0` in `src/pufo/game/pufo_tacthud.c`, asserted by
   `test_tacthud` (88 checks). `Unit_FaceToward @ 0x49bf0` is ported for real:
   it enqueues a turn command, it does not write `Soldier.bFacing`.
   Closed items: `MapCursor_SetPos @ 0x27014` reads **`Map+0x02`**, not `Map+0x22`
   (`LDRB R3, [R4,#2]` at `0x2723C`); the wrong offset in the port header and in
   `docs/parity/game-core.md` is corrected, and the field is now
   `bActionLatch` / `pbActionLatch`. `anActionPhaseSave0` is at **`Map+0x15078`**,
   not `Map+0x74` — the port's `0xD1/0xD7/0xD8/0xD9/0xE4` claims were wrong and are
   now `0x150D5..0x150E4`, and the duplicate `bPhaseSave20` field (a second name for
   `bLookCommitFlag @ 0x1508C`) is removed. The action tables are settled by literal
   immediates at `0x170D0`: cost `Map+0x4E4C + 4*m`, enabled `Map+0x4E7C + 4*m`
   (`Map+0x4EBC` is the unrelated combat hit-window array). The hand action menus
   `Tact_OpenLeftHandMenu @ 0x17030` and `Tact_OpenRightHandMenu @ 0x17148` are ported
   as one `Ui_OpenHandMenu`. So are the inventory button `Inv_OpenScreen @ 0x16ef4`
   (`TactHud_OpenInventory`) and the special action pad `TactHud_UseSpecialItem20 @
   0x17d54` (`TactHud_UseSpecialItem`), which covers the mark-nearby branch, the
   medikit target search and its two refusal pairs, and the latch-4 branch. That
   empties the tactical HUD handler block `0x163d4..0x17d54`. `MapView_FocusOnUnit @
   0x2c758` is ported for real as `MapView_FocusOnCell` (it takes a `MapCell *`, not a
   unit). `Unit_CanStepExitDir @
   0x36d68` is retyped: both of its callers pass `Map.pSelUnit->pCell`, so the
   argument is a `MapCell *` and the bytes it reads are `nX/nY/nZ`, not the Soldier
   path triple Hex-Rays named. The widget bank itself is now ported too:
   `Tact_InitUiControls @ 0x3b498` and `Tact_LoadUiPads @ 0x3b068` in
   `src/pufo/ui/pufo_uibank.c`, with all thirteen `UiControlDesc` tables read out of
   the executable (32-byte record: `xOff +0, yOff +2, szPad0 +4, szPad1 +13,
   szPad2 +22, nMode +31`), 115 widgets laid out, 115 click handlers bound by widget
   index, and the 54 pads (`MAN_0M0..MAN_3F3`, eleven named pads, `b0..b10`) named
   and mode-checked. `test_uibank` asserts it (388 checks). Widgets 113..127 are
   confirmed never laid out. The handler names for the slots whose handlers are not
   ported yet are recorded in `g_aUiClickName`, which is the work list for the rest
   of this task. That work list is now shorter: the fourteen `MenuBtn_*` click
   adapters (`Confirm0..5 @ 0x19650..0x1a014`, `ConfirmSnd15 @ 0x198f0`,
   `ConfirmClear @ 0x194c8`, `Secondary @ 0x19498`, `SecondaryDismiss @ 0x195e0`,
   `ActionBack0/1 @ 0x195a8/0x19618`, `Cancel0/1 @ 0x1a24c/0x1a26c`) are ported in
   `src/pufo/ui/pufo_menubtn.c` as six functions, together with
   `Menu_OpenLoadSlots @ 0x18864` / `Menu_OpenSaveSlots @ 0x188a4` (screens 11 and
   10) and `Menu_ConfirmQuitDialog @ 0x18e2c` / `Menu_CancelQuitDialog @ 0x18e04`.
   `Options_SyncUiFromMap_GotoScreen12 @ 0x188e4` is `Menu_SyncOptionPads`, and it
   proved the volume options are stored 1..5 (`Options_SetMusicVol1` writes the
   immediate 1), which the port had wrong. `test_menubtn` adds 67 checks. The ground
   item list `Inv_BuildVisibleList @ 0x2d580` is ported in `pufo_tactui.c` and now
   replaces the `pfnRebuildVisibleList` hook in `TactUiHooks`, `RosterHooks` and
   `TactHudInvOpenCtx`: merge by `ObjectRec` identity keeping the larger `nAmmo`,
   skip `bTwoHanded == 2`, filter on `nCategory` against `nInvListSel` (0xFF = all),
   then selection-sort ascending by `uSortKey @ 0xC8` with the `u16` count array
   swapped in step. `apInvList @ 0x1A24` and `anInvCount @ 0x3A24` are 2048 entries
   each. The inventory hit test `Ui_HitTestTactItem @ 0x11ac0` is ported in the new
   module `src/pufo/game/pufo_invdrag.c` with the 25-record `aInvHitRects @ 0xA8DD8`
   table read out of the executable: the 4-column ground window, the first-match
   table rule, and the `unk_138EA0` slot mirror that redirects a hit through the
   meta byte beside the slot (`test_invdrag` 75 checks). `Inv_ClearHitSlot @ 0x11f40`
   is ported beside it, and its raw dword writes (`4*(sub0 + 67/70/73/76)`,
   `4*(sub0 + 4*sub1 + 79)`, `4*(sub0 + 3*sub1 + 91)`) confirm the `aPockets` 12-byte
   records, the 4-slot backpack rows and the 3-slot belt rows. The drop half
   `Inv_HandlePointer @ 0x12260` is ported beside them: the anchor is the stored drag
   offset plus the pen position at `pBank+0x1044/0x1048` plus 10, the per-target time
   costs are 4/4, 5/5, 6, 7, 8, 5 and 6 with a latch and same-region exemption, the
   clip load deletes the dragged item and closes the pool hole with `pool[count-1]`
   (ARM `0x12DB4` builds it as `Map + 4*count + 0x23434`), and a failed drop restores
   the item to the region the hit test recorded (`test_invdrag` 281 checks). The last
   pBank widget handler is ported too: `Map_BuildDeployConfirmYesNo @ 0x18a68` reuses
   the already-ported `Tact_GetCameraBounds` for its rectangle and adds the four
   counting buckets, where `nUnitType > 8` is not counted at all and a civilian with
   `uHp == 0` is skipped (`test_tactctl`). What is
   left of this task is `LoadGame @ 0x44bbc`. The rest of the save/load slot screen is
   ported: `SaveSlot_FormatList @ 0x45904`, `SaveGame @ 0x44988`,
   `SaveLoad_Slot0..6 @ 0x19de4..0x19fc4` and `StartUi_Shutdown @ 0x38d7c`. What remains
   there is the geoscape screens. See
   `docs/parity/ui-core.md`, "The inventory hit test" and "Dropping the item", and
   `docs/parity/game-core.md`, "The deploy confirm dialog". The `UiPanel` type
   conflict is already resolved — the
   7092-byte layout is the real one, and the two views (16-byte lines with colour slots at
   `+0xC8..+0xD4` versus 4-byte `apLines[500]` at `+0xBE4`) are mutually exclusive. Start
   from `src/pufo/ui/` and `docs/parity/ui-core.md`.
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
| `docs/BUILD.md` | Toolchain, commands, the real source layout, and the test harness format. |

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
