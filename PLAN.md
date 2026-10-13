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
- [x] `Map_BuildMissionTerrain @ 0x22e10` — ported, 10,676 B cited, in `src/pufo/game/pufo_terrain.c`
  - [x] `MapTerrainEnv` replaces the eleven `Map`-block offsets and the font block the original reaches
    through. Two hooks: `pfnLoadTactSprites` for `Font_LoadTactSprites (0x257c4)`, and `pfnResolveShot`
    for `Combat_ResolveShot @ 0x2f12c` — that one IS ported, but its `CombatCtx` (TactState, the three
    rosters, the 41×41 hit windows) belongs to the caller that owns the tactical context
  - [x] the builder allocates the 14400 `MapCell` objects into a caller-owned `MapCell *[14400]`,
    reproducing the allocation-failure path (`MapCell_Init(NULL, x, y, layer)`) and the tail's untested
    cell dereference
  - [x] `tests/test_terrain.c`: 59 checks. Cell allocation and per-cell coordinates, the tail's mask,
    the craft-patch side effect as proof the tail ran, the two XCom-base stamping passes cell by cell,
    and the four tile-type guards in both neighbour directions
  - [x] the four module-name tables inlined and audited — 34 tables, 0 mismatched — emitted by the new
    `tools/gen_terrain_tables.py`
  - [x] `Font_GetFlagSprBase @ fontSys + 2*nFlag` added: a template's slots resolve through the base
    chosen by the flag it was loaded with, and the flag-3 base is the same u16
    `Map_PatchCraftSprites` reads as the craft sprite group
  - [x] the craft pad tile and `Map_nCraftMapX/Y` are the same two bytes (`Map+0x15139/0x1513A`), so
    the builder writes the craft's tile and the tail reads it back. `CraftSpritePatchEnv`'s comment in
    `pufo_font.h` said `0x15199/0x1519A` — corrected
  - [x] IDB: `aUfo1a` re-declared `[135]` (IDA had `[216]`, 81 bytes into the alien tables); named
    `Map_nMissionTier` (was `Map_bUnk15112`), `Map_nMissionTierRaw`, `Map_bSpawnAlt`,
    `Map_nSpawnTileX/Y`, `Map_nFireOrigX/Y`; corrected my own wrong slot-order comment at `0x24150`
    (wall A is 0x04, wall B is 0x08); comments at `0x24174`, `0x24194`, `0x2436c`, `0x24394`,
    `0x25338`, `0x246A0`, `0x25740`, `0x23170` and the function header
  - [x] four rows added to `bugs.tsv` (51 total): the null `MapCell_Init`, the untested tail deref,
    `aPlane[-1]` for mission type 0, and the unbounded `aUfo1a` index
  - [x] the last open questions closed: both kind-array passes read `kind[row][col+1]`; pass 1's tile
    guard looks one COLUMN right and pass 2's one ROW down; the fallback appends the literal `"4"`
    (`pDefault @ 0xAAD4C`), not the per-type suffix; the shot's object argument is
    `*(Map + 0x108 + 4*selector)`, an entry of the object-pointer array, not a mode
  - [~] still open: what the `aMissionTileTypes` sentinels `0x10`, `0x11` and `0x12` mean. Only their
    effect is proven — they suppress the stamping passes, and `0x14` also suppresses the `.rmp` load
  - Decode facts the port rests on, all settled earlier: the cell array is inline in `Map` at
    `Map + 0x15338 + 4*(60*nY + nX) + 0x3840*nLayer`, 14400 dwords ending at `Map+0x23438`; the 6×6
    module grid lives at `&pTpl.pDim1` and overruns the 20-byte `CDialogTemplate`; every template
    dimension becomes tiles by `/10`; the module grid means 0 empty, 1 generic module, 2/3/4 Urban
    module numbers 0/1/2; the `var_90` dword array is a separate per-tile kind; the stamping bases
    `189`, `190`, `543`, `543+60` and `3050` are cell indices into the same array
    (`189` → y 3+k+10·tileRow, x 9+10·tileCol; `543` → y 9+10·tileRow, x 3+j+10·tileCol)
  - [x] every callee is now ported except `Font_LoadTactSprites (0x257c4)`, which is a hook
  - [x] reading aid: the full Hex-Rays text is in `.tmp/bmt/bmt1.c` (scratch, untracked). The MCP
    `decompile` truncates at ~52 KB; `curl` to `http://127.0.0.1:13337/mcp` truncates too. Do not
    trust its offsets — ARM wins, and it did: the compact `disasm` listing and the pseudocode both
    hid the `kind[row][col+1]` read and the guard-neighbour directions
- [x] `.ma1` header direction fixed — **header byte 0 is the row count, byte 1 the column count**
  - Proved three ways: `Map_ApplyTemplateToCells (0x54cf4)` builds `10*(6*(10*nOffY + row) + nOffX) + col`
    with `row` looping to byte 0; `GetCellByte (0x54c80)` uses byte 1 as the row stride; every
    `Map_LoadRmp` call passes `nSizeX = byte1/10` and `nSizeY = byte0/10`.
  - `MapTemplate_ApplyToCells` had it backwards, so a non-square template read past the end of every
    row and landed transposed, and its offsets were cells rather than tiles. Fields renamed
    `nRows`/`nCols`, nesting matches the ARM, offsets are tiles scaled by `PUFO_CELLS_PER_TILE`, and
    `test_template.c` asserts the direction outright. No coverage change — a correctness fix.
  - [x] `CDialogTemplate_GetTemplateByte0 @ 0x54c68` and `GetTemplateByte1 @ 0x54c5c` ported as
    `MapTemplate_GetHeaderByte0/1`. Both were already credited by the evidence header, so coverage
    did not move.
  - [x] `src/pufo/game/pufo_terrain.{h,c}` added with `g_aTileSprIndex` (64 u16, 16 rows of 4) and
    `g_aTerrainModuleCount` (10 bytes), both audited — 30 tables, 0 mismatched
  - [x] IDB: `aTileSprIndex` re-declared `[64]` (IDA had `[128]`, 128 bytes past the real end), the 40
    bytes at `0xA6568` named as `Smap_InitCraftSelectState (0x61b94)`'s table, `aUfo1a` restored, and
    comments added at `0x54cf4`, `0x54c68` and `0x54c5c` recording that byte 0 is Y
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
- [x] `MapView_DrawUnit @ 0x1c958` — ported, 3,220 B cited, in `src/pufo/game/pufo_isodraw.c` behind
  `IsoUnitDrawGate`
  - [x] every branch re-read from ARM rather than pseudocode; the compact `disasm` listing dropped an
    instruction at `0x1CE34`, so the remaining regions were verified through the address-anchored
    listing instead
  - [x] the pose-frame branch settled: pose outside `0xF..0x11` takes `5*(pose+base)+kind`; inside it,
    no two-hand weapon AND walk kind 3 takes `5*base+33`, which is row `base+6` column 3. Both paths
    converge on the store at `0x1CFF8`, which happens BEFORE the zero test — a zero entry is stored as
    0, not decremented
  - [x] the direction classification compares `nX` first (`0x1CB48`), so equal coordinates with a
    different cell object fall through to the diagonal set at `0x1CB90`
  - [x] `PUFO_ISO_GROUP_BASE_COUNT` corrected 18 → 28: `Tact_MapTypeToGlyphIdx` is unclamped and
    returns 18 for types 5..8 and `nType + 5` up to 27, so the base block must reach fontSys + 0x36
  - [x] `bAnimStep` now ends at the step count, not the last index used — both loop tails store the
    incremented value before the test
  - [x] `tests/test_isodraw.c`: 200 checks total, eight new groups. Most assertions read the context
    back; ink is used only for the two cursor markers
  - [x] three findings added to `bugs.tsv` (the `nPal` restore asymmetry, the null dereference at
    `0x1D384`, the 14-type facing table overrun) and seven IDB comments, one of which replaces a
    comment there that claimed every return path restores `nPal`
- [ ] `Tact_InitMission (0x3c550)`
- [ ] `LoadGame (0x44bbc)`

## Later priorities

2. Tactical renderer leftovers. `MapView_DrawIsoLayer (0x21e2c, 1,252 B)` is now blocked only by
   `MapView_DrawUnitFacingArrow (0x1b7bc, 2,692 B)`. 3. Geoscape. 4. Screens. 5. Housekeeping.
   See `HANDOFF.md` §10.

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
| `36addee` + `Tact_Shutdown` | 49/49 | 6913 | 668/1103 (60.6%) | 236712/598792 (39.5%) | 28 checked, 0 mismatched |
| `7c29217` + `MapView_DrawUnit` | 49/49 | 6977 | 669/1103 (60.7%) | 239932/598792 (40.1%) | 28 checked, 0 mismatched |
| `.ma1` direction fix | 49/49 | 6984 | 669/1103 (60.7%) | 239932/598792 (40.1%) | 30 checked, 0 mismatched |
| this commit (`Map_BuildMissionTerrain`) | 50/50 | 7043 | 670/1103 (60.7%) | 250608/598792 (41.9%) | 34 checked, 0 mismatched |
