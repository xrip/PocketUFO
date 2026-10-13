# Working plan

This is the agent's execution plan. Read this at the start of a turn, not the goal
JSON and not all of `HANDOFF.md`. `HANDOFF.md` holds the detail and the reasoning;
this file holds only what is next and what is already true.

Update it as work lands: flip a checkbox, move a line to Done, refresh the numbers.
Do not rewrite it from scratch unless the plan itself changes.

## Standing rules

- IDB (`pufobeta.exe.i64`, MCP session `f53a1851`) is the source of truth. ARM beats Hex-Rays.
- Every step also fixes the IDB: real names, real types, comments on non-obvious blocks and on
  original bugs. `idb_save` before committing the `.i64`.
- Build: `cmake --build build --config Release`. Tests: `ctest --test-dir build -C Release`.
  Kill a running `pocketufo.exe` first. `/W4 /WX`.
- Gates before a commit: `docs/parity/gen_report.py`, `coverage.py`, `audit_credits.py`,
  `audit_names.py`, `audit_bugs.py`, `audit_tables.py`.
- Citations: `Name @ 0xADDR` only for something actually ported. For a blocker or an unported
  function write `Name (0xADDR)`.
- Commit with explicit paths: `git add -A -- src/pufo tests docs tools pufobeta.exe.i64 PARITY.md HANDOFF.md PLAN.md`.
- Image map: `IDA_VA - 0x13000 == file offset`. Verify table bytes from the image, never by eye.

## Now: Priority 1, make a tactical mission start

Order is by what unblocks what. Each item ends with gates + commit.

- [x] `MapView_DrawUnit` tables inlined and byte-checked (`de8bbc4`)
- [x] `Tact_SpawnAliens @ 0x3a2ac` — ported, 3,516 B cited
  - [x] IDB: fixed the wrong comment at `0x3ab6c` (there is no overrun), deleted the three phantom
    frame variables and named the composition head `anComposition`, typed `aXcom0` as `char[23][9]`
    and the numeric tables, commented the latent bugs
  - [~] IDB: the 23 `aXcom0` name slots are one typed object plus a comment listing all 23 names.
    The MCP `rename` data form rejects unnamed addresses (`Global '0xA7C39' not found`), so per-slot
    IDB names are not reachable through this API. The port side is named: `g_aSpriteSetName[23][9]`.
  - [x] `tools/gen_alien_tables.py` + the nine tables inlined in `pufo_tactctl.c`
  - [x] registered in `audit_tables.py` (28 tables, 0 mismatched)
  - [x] `PufoFont.anAlienSprBase[4]`
  - [x] `Tact_SpawnAliens` + `TactSpawnAliensEnv` in `pufo_tactctl.{h,c}`
  - [x] tests in `tests/test_tactctl.c` (four new cases, 60 new checks)
  - [x] corrected the false "stack overrun at 0x3AB6C" claim in `HANDOFF.md` and `game-core.md`,
    and the false `0.2` scaling constant (it is 0.8) in both
  - [x] two genuine original defects recorded in `bugs.tsv`: unread `var_24` (0x3A8BC) and unread
    `var_48` (0x3A2F0); both enter the port as explicit env fields
- [x] `Tact_SetupCraftCargo @ 0x38f00` — ported, 2,116 B cited
  - [x] the "model 0x156CD0 / 0x157208" step dissolved: both have xrefs from this function and
    nothing else, so they are private scratch. They are still `.bss` state (entries are cleared per
    object reached, not wholesale), so they enter as caller-owned arrays in `TactCargoEnv`.
  - [x] IDB: named `Map_nMapLayers`, `Map_nObjectCount`, `Map_nCraftCount`, `Map_apObjects34`,
    `Map_apObjects105`, `Map_apCells0`, `TactCargo_aSplit`, `TactCargo_aWork`, `Tact_pDeployCell`;
    commented the sort-key stop, the `2*(base+21)` rendering trap, the split rebalance, the
    weapon/ammo boundary and the tail's read-after-add
  - [x] four tests in `tests/test_tactctl.c` (73 new checks, 6813 total), one of which calls the
    function twice to pin the scratch persistence
- [~] `Map_BuildMissionTerrain (0x22e10)` — 10,676 B, 313 blocks. Read end to end in ARM; the six
  phases and the cell-array extent are settled and written to `docs/parity/game-core.md`, and the phase
  boundaries carry IDB comments. Not ported yet.
  - [x] the cell array is inline in `Map`: `Map + 0x15338 + 4*(60*nY + nX) + 0x3840*nLayer`, 3600 cells
    per layer, 4 layers, 14400 dwords = `0xE100` B, ending exactly at `Map+0x23438` = `apTactItems`
  - [x] the 6×6 module grid lives at `&pTpl.pDim1` and overruns the 20-byte `CDialogTemplate`
  - [x] every template dimension is read as `_rt_sdiv(headerByte, 10)` — cells to tiles
  - [x] `byte_A6AF0` resolved: ten-entry per-mission-type module-count table (jungle 12, culta 19,
    forest 12, xbase 21, ubase 16, urban 15, desert 12, mount 13, polar 14, mars 11); urban is
    `rand(12)+3` so its module numbers are 3..14 and every other terrain starts at 0
  - [x] `aMissionTileTypes @ 0x159D5C` is runtime `.bss` state, not a shipped table; `0x14` skips the
    tile's `Map_LoadRmp`
  - [x] the shot-count selector is a Map dword-index offset: the shot's object-array argument is
    `*(Map + 0x108 + 4*selector)`, selector 18 for variant 2 and 51 for variant 3
  - [ ] open: the module-grid values `0..4`, the `var_276` kind codes, `unk_128B00`,
    `word_128B04/128B08`, the `0x10/0x11/0x12` tile-type guards, and the three near-duplicate
    stamping copies
  - [ ] `CDialogTemplate_GetTemplateByte0 @ 0x54c68` and `GetTemplateByte1 @ 0x54c5c` (12 B each) are
    the only unported callees; both are `pBlob[0]` / `pBlob[1]` accessors
  - [x] name tables verified from the image, nine bytes per entry:
    `aTerrainNameByMissionType @ 0xA67F8` = jungle, culta, forest, xbase_, ubase_, urban, desert,
    mount, polar, mars (indexed by `Map_nMissionType`); `aTerrainNameSuffix0 @ 0xA7B20` = `"0"` for
    every type except urban which is `"4"`; `aPlane @ 0xA6668` = plane, lightnin, avenger (indexed by
    `Map_nMissionType - 1`); the UFO module table at `0xA6590` = ufo1a, ufo_110, ufo_120, ufo_130,
    ufo_140, ufo_150, ufo_170, ufo_160 (indexed by `3*(variant-1) + tier`)
  - [x] IDB: named `Map_nMapDimX @ 0x128AD3` and `Map_nMapDimY @ 0x128AD4` (the tile bounds the cell
    grid loops use), commented the four name tables, and recorded that Hex-Rays renders the
    `Map_nTerrainVariant` read as `Map+0x15110` while the ARM literal at `0x23170` is `0x15111`
  - [ ] reading aid: the full Hex-Rays text is in `.tmp/bmt/bmt1.c` (scratch, untracked). The MCP
    `decompile` truncates at ~52 KB; `curl` to `http://127.0.0.1:13337/mcp` truncates too, so the
    dump has to come from a session that already holds it. Do not trust its offsets — ARM wins.
- [x] `Tact_Shutdown @ 0x3d568` — ported, 3,788 B cited, in `src/pufo/game/pufo_tactend.{h,c}`
  - [x] it was the only Priority 1 item whose callees were all already ported, so it went first;
    `Tact_InitMission` is still blocked by `Map_BuildMissionTerrain`
  - [x] the bank pad walk settled: thirteen groups cover widgets 0..112 and 128..129, and
    `StartUi_Shutdown (0x38d7c)` covers 113..127. Together 130, each slot exactly once. The inner
    step is four BYTES, so the three pointers are one widget's `pPad0/pPad1/pPad2`
  - [x] hooks for `Sound_Shutdown`, `KillTimers_BySpeed`, `UiPanel_FreeLines` and
    `UiControl_FreePads` — the first two need a caller-owned backend, the last two keep
    `pufo_game` from depending on `pufo_ui`
  - [x] `tests/test_tactend.c`: 100 checks. Pins the group coverage, the line counts set before the
    null test, the cell-array bounds, and the shrinking-roster walk (three enemies leave one
    survivor)
  - [x] IDB: the function header no longer claims unported callees, and the pad-group comment now
    records the 115 + 15 = 130 cross-check
- [ ] `Tact_InitMission (0x3c550)`
- [ ] `LoadGame (0x44bbc)`

## Later priorities

2. Tactical renderer leftovers. 3. Geoscape. 4. Screens. 5. Housekeeping. See `HANDOFF.md` §10.

## Separate issues, not folded into the porting steps

- [ ] `pufo_game` and `pufo_ui` depend on each other. `pufo_strateg.c` (in `pufo_game`) calls
  `UiLineList_Remove` (in `pufo_ui`), and `pufo_ui` links `pufo_game`. Test targets that link
  `pufo_game` without `pufo_ui` only link because nothing pulls `pufo_strateg.obj` out of the
  archive; one new shared symbol pulled it in and 19 targets failed to link while ctest still
  reported 48/48. Until this is fixed, share a value across modules as a header define, not an
  `extern` symbol.
  - It fired again when `test_tactend` was added: `Tact_Shutdown` calls `StrategList_RemoveAt`,
    which pulls `pufo_strateg.obj`, which reaches `UiLineList_Remove` in `pufo_ui`. The test target
    now links `pufo_ui` like the other fifteen that already had to.

## Measured state

Refresh after each commit. A coverage delta must match the function size in
`docs/idb/inventory-grouped.tsv`.

| HEAD | tests | checks | functions | bytes | tables |
|---|---|---|---|---|---|
| `de8bbc4` | 48/48 | 6680 | 665/1103 (60.3%) | 227292/598792 (38.0%) | 18 checked, 0 mismatched |
| `d4c74f1` | 48/48 | 6740 | 666/1103 (60.4%) | 230808/598792 (38.5%) | 28 checked, 0 mismatched |
| `8043c2b` | 48/48 | 6813 | 667/1103 (60.5%) | 232924/598792 (38.9%) | 28 checked, 0 mismatched |
| `89022a5`, `887fed8`, `36addee` — decode + IDB only | 48/48 | 6813 | 667/1103 (60.5%) | 232924/598792 (38.9%) | 28 checked, 0 mismatched |
| this commit (parent `36addee`) | 49/49 | 6913 | 668/1103 (60.6%) | 236712/598792 (39.5%) | 28 checked, 0 mismatched |
