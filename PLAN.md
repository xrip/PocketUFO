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
- [x] `Tact_LoadLoftData @ 0x3bd6c` — ported, 184 B cited, in `src/pufo/game/pufo_los.c` as
  `Tact_LoadLoftData(LosTables *)`. It was the last genuinely unported callee of `Tact_InitMission`;
  `Font_Init`, `TactCtx272_ctor`, `StartTimers_BySpeed` and `Inv_OpenScreen` were already ported under
  other names. Deviation recorded: the original sizes each block as `2 * (size / 2)` and reads the full
  member size into it (a one-byte overflow on an odd member) and never frees the previous pair.
- [x] `Tact_InitMission @ 0x3c550` — ported, 4,120 B cited, in `src/pufo/game/pufo_tactinit.{h,c}`
  - [x] `TactInitEnv` carries the whole Map-block half of the function as caller-owned pointers, plus
    the five `UiPanel *`, the 14400-entry cell array, and the embedded contexts for every callee it
    fills (`terrain`, `select`, `teamSelect`, `spawnAliens`, `cargo`, `equip`, `deploy`, `focus`,
    `cursorPos`, `vis`, `invOpen`)
  - [x] hooks for the `pufo_ui` seam (`PanelCtor`, `PanelStyle6to11`, `PanelWrapText`, `LoadUiPads`,
    `InitUiControls`), the audio backend (`SoundInit`, `SoundPlayMusic`, both volume passes), the
    sprite loader and the inventory screen
  - [x] the flag block decoded from ARM in its own order: `Map+0x00` is set to **1**, not 0
    (`MOV R0,#1` at 0x3C678), and `apUiSlotsA[2]` @ `Map+0x68` is the sweep gate the tail arms
  - [x] the deploy-point sweep stride: `4*n + 104*(nMapLayers-1)` bytes into `aDeployCellOffsets
    @ 0xA66B8` — 26 four-byte records per 104-byte group, bytes 0/1/2 are dx, dy, layer. Its base is
    `Map_nCraftMapX/Y @ 0x15139/3A`, which `Map_BuildMissionTerrain` re-rolls, so the sweep follows
    the terrain pass rather than the caller's value
  - [x] the showship header order settled: word 0 is the entry count (outer), word 1 the entry length
    (inner), `0xFF` reveals nothing, and every revealed column runs all four layers
  - [x] the briefing text assembly settled, including the `nLineCount += 2` tail at `0x3D434` and the
    fact that `UiPanel_WrapText @ 0x37144` ends with `nLineCount = nLineStart + committed`
  - [x] `apUiSlotsA[11] @ Map+0x8C` is written by `LDRH Map_nUiScreen` at `0x3D29C` — and the prologue
    has already cleared `Map_nUiScreen`, so the saved value is always 0 on this path
  - [x] `tests/test_tactinit.c`: 8 cases. The prologue flag block, the weapon-select record and the
    object pointer copies, the five panel layouts, the deploy-point sweep, the `bFlags29` bit-4 rule,
    the showship reveal against the shipped `showship.dat`, the briefing, the briefing skip, and the
    save-load mode leaving `unk_138E8C` alone
  - [x] IDB: named `Map_bBattleEnded` (0x113A14), `Map_bAltOutcomePath` (0x1153D9),
    `Map_bPlayerInputEnabled` (0x1139F0) and `Map_nUiScreen` (0x1139F2, u16 — the briefing writes it
    with `STRH`); commented the function header, the deploy sweep at `0x3CC20`, the layer switch at
    `0x3CE70`, the reveal at `0x3CEA4`, the slot-11 save at `0x3D29C`, the pad global at `0x3D548`,
    the failed allocation at `0x3D2CC`, the weapon record at `0x3C720`, the drag sentinel at `0x3CB40`,
    and the globals `0x138E6C`, `0x138E70`, `0x138E8C`, `0x128AD6`, `0x10DB90`, `0x138EA0`,
    `0x113A1C`, `0x113A44`
  - [x] four rows added to `bugs.tsv` (58 total): the layer switch leaving `var_350`/`var_354`
    unwritten for a layer count of 0 or 4+, the failed `new(0x14)` storing NULL and then writing
    through it, the pad global overwritten without a free, and the untested `AssetReader_Find` for
    `showship.dat`
  - [x] a fixture bug this step exposed: `Tact_BuildVisRayTable` writes `PUFO_VIS_TABLE_BYTES`
    (14,880) step bytes, and the new fixture sized its step array at 256. It overran into the asset
    registry's statics and crashed the next `AssetReader_Find`. The ray table itself was correct.
- [ ] `LoadGame (0x44bbc)` — blocked by exactly one callee now: `Geoscape_InitUI (0x857f4, 5,420 B)`,
  which is itself blocked by `Game_StartNew (0x94554, 3,976 B)` and `GameTime_Set5Sec (0x84f00, 100 B)`.
  Everything else in `LoadGame` is ported (`DatFile_*`, `Save_ReadGameState/Research/Time`,
  `GameTime_Decode`, `PtrArray_*`). So the Priority 1 tail is really the geoscape chain. In Priority 2
  the map frame is now assembled; what is left there is `Tact_DrawCommandPads (0x1fc8c)` and the
  caller `Render_Frame (0x22310)`.
- [x] The first two links of that chain: `BaseFac_SetCellBuildState @ 0x623bc` (336 B) and
  `Base_GrantStarterItems @ 0x95fa4` (124 B), both leaves, both in `src/pufo/game/pufo_geoscape.c`.
  - [x] `aFacBuildSpec @ 0xAA8F8` inlined as `g_aFacBuildSpec` (18 records of tileA, tileB, signed
    build days) and registered in `audit_tables.py` — 35 tables, 0 mismatched. IDA declares 56 bytes;
    the real extent is 54.
  - [x] the grid model settled from the ARM's own arithmetic: `fac[]` is `gBaseFacGrid + 0x174` and a
    store is `6*(24*nBase + nY) + nX` into it, while `buildDays` is `6*(6*nBase + nY) + nX`. The ARM
    multiplies `nY`, which is the coordinate the other readers of this grid call the column.
  - [x] facility type 11 settled as the hangar (the kind `Base_CalcHangarCap` counts): a 2×2
    footprint marking four tileA quadrants 9/10/11/12, build days at only three of the four cells,
    no tileB, and a `facType` byte in the anchor cell only — which is why it counts once.
  - [x] IDB: named `Map_apObjects31` (0x113B3C) and `Map_apObjects56` (0x113BA0), commented
    `aFacBuildSpec`, both function headers and the diagonal `buildDays` store at 0x62484
  - [x] five rows added to `bugs.tsv` (61 total)
  - [x] `tests/test_geoscape.c`: 37 new checks
- [x] `Ship_SpawnAtSlot @ 0x2cb14` (664 B) — ported in `src/pufo/game/pufo_ship.c` behind
  `ShipSpawnEnv`, with 19 new checks in `tests/test_ship.c`.
  - [x] the hangar scan settled: `facType[v10][result] == 11` AND `layer2[1][result+1] == 0` AND
    `buildDays[v10][result] == 0`, over 0..4 in both axes — a 5×5 search of a 6×6 grid.
  - [x] the stamp offset settled as `fac[nBase] + 6*v10 + result + 79`, which is
    `layer2[v10+1][result+1]`. The first pass of the port dropped the 72 and wrote into tileA; the
    test caught it.
  - [x] three original quirks recorded in `bugs.tsv` (66 rows total): the `layer2` test pinned to
    row 1 while the stamp follows `v10`, the 5×5 corner search, and a craft type outside 0..4
    writing the hangar coordinates and then falling through the switch without stamping.
  - [x] IDB: function header comment at 0x2cb14
- [x] `MapView_DrawUnitFacingArrow @ 0x1b7bc` (2,692 B) — ported in `src/pufo/game/pufo_isodraw.c`
  behind `IsoArrowGate`, with 46 new checks in `tests/test_isodraw.c`.
  - [x] the three branches settled: `TactState+0x111E1` (fire animation running) → muzzle smoke at the
    unit's facing offset; `bFireStatus1` → the ten-entry roster at `Map+0x13EE8/0x13F38/0x13F88`;
    `bTrajHitStop` → the thrown item's floor-object glyph at the end of entry 0. All behind the one
    cell gate `bFlags29 & 0x20`.
  - [x] `_rt_sdiv` argument order pinned from the ARM: `MOV R1,R0` is the **dividend** and `MOV R0,#12`
    the divisor, so `nPhaseCounter % 4` and `(int)Z / 12` both read the way Hex-Rays renders them. The
    first reading of the smoke branch had it backwards.
  - [x] `byte_A9290` / `byte_A92D0` named `aArrowGlyphOffX` / `aArrowGlyphOffY` (64 signed bytes each)
    and inlined as one `g_abArrowGlyphOff`, with **both** image addresses registered in
    `audit_tables.py` — 37 tables, 0 mismatched. The audit caught a transcription slip in the first
    draft (11 zeros where the image has 10), which is exactly what it exists for.
  - [x] `unk_127A28` named `pArrowThrownItem`: the `TactItem *` `Tact_SimulateThrowArc` stores at
    Map+0x14070 (0x319F4), read here as `pItem->pObj->aItemQty[50]` — the same ObjectRec+0x9E field
    `TactUnit_DrawHpGlyphs` already reads.
  - [x] five rows added to `bugs.tsv` (71 total): the unguarded dereference at 0x1C1F4, the unbounded
    nudge index at 0x1BD18, the duplicated nudge table, the residue return, and the naming finding
    below.
  - [x] IDB: function header comment at 0x1b7bc, line comments at 0x1b7dc (the `TactState+0x111E1`
    flag and its writers), 0x1b888 (the entry test), 0x1bd18 (the nudge index) and 0x1c1f4 (the null
    dereference)
- [x] `MapView_DrawIsoLayer @ 0x21e2c` (1,252 B) — ported in the same file behind `IsoLayerEnv`, with
  25 new checks in `tests/test_isodraw.c`. It is the only caller of the per-cell chain, so it is where
  the seven per-cell gates get assembled; `Render_Frame` will own that env.
  - [x] the two-pass shape settled: pass 1 draws floors only, pass 2 draws the other eleven, and the
    unresolved-cell fallback (blank group glyph, `fontSys+0x00`, palette 0) runs on layer 0 only.
  - [x] the frame tail settled: viewport guide, selected-unit panel, up to five multi-target bars at
    column 207 with row `300 - 18*i - (deploy panel ? 17 : pHudPad->uHeight)`, the status stack, the
    active pad and its centred label, and a 240×320 cover of index 55 at key 128 unless
    `Map_nUiScreen` is 0 or 13.
  - [x] `IsoDrawCtx.szText` modelled at 0xAC — the scratch string the itoa output and the pad label
    copy share.
  - [x] IDB: `nTileCountX` (Map+0x34), `nTileCountY` (Map+0x35), `bDeployPanelOpen` (Map+0x15110) and
    `pActiveLookCtl` (`dword_1571E4`) named; function header at 0x21e2c and line comments at 0x22080,
    0x220E8 and 0x22248
  - [x] four rows added to `bugs.tsv` (75 total): the unguarded `Unit_bIsOperative(Map.pSelUnit)`, the
    unbounded label copy, the STRB-truncated bar index, and `StatusMsg_DrawStack` being handed the
    draw context instead of the Map
  - [x] `test_isodraw` now links `pufo_ui`, because `pufo_isodraw.obj` reaches
    `UiControl_BlitActivePad` through `pufo_tacthud.obj`. That is the known cycle, not a new one.
- [ ] `Game_StartNew (0x94554, 3,976 B)` — its callees are `GameTime_Reset` (ported),
  `BaseFac_SetCellBuildState`, `Base_GrantStarterItems` and `Ship_SpawnAtSlot` (all now ported),
  plus `InvSel_AddItemQty`, `Smap_ZoomOut`, `StrategList_MaybeSpawnCraft_Process` and
  `Soldier_CreateAndClassify` (ported), leaving exactly two unported: `Base_CreateNamedEntry`
  (0x66930, 576 B) and `Geoscape_BuildNavControls` (0x67b1c, 588 B), both leaves.
- [ ] `GameTime_Set5Sec (0x84f00, 100 B)` — needs a model for the six UI control pointers at
  0x186500..0x186514 and the byte at 0x187070.
- [ ] `Geoscape_BuildNavControls (0x67b1c, 588 B)` — decoded, NOT ported, and the reason the
  Priority 1 tail is no longer a short chain. It allocates 15 `UiControl` objects into the pointer
  array at 0x159888 (15 slots: 10 + 2 + 3) and installs a click handler in each:
  `NewBaseScreen_Open`, `BaseInfoScreen_Open`, `SoldierScreen_Toggle`, `BasePlaneScreen_Open`,
  `BaseFacScreen_Open`, `ResearchScreen_Open`, `ManufScreen_Open`, `TransferScreen_Open`,
  `BuySellScreen_OpenBuy`, `BuySellScreen_OpenSell`, `SoldierScreen_PanelPrev`,
  `SoldierScreen_PanelNext`, `SoldierScreen_FindNextAtBase`, `SoldierScreen_FindPrevAtBase`,
  `Ui_SetTransferFlag`. Every one of those is an unported screen. So `LoadGame` does not end at the
  geoscape — it runs through Priority 4.
  - Useful by-product: the 84-byte gap between `buildDays` (288 bytes) and `fac` (at +0x174) inside
    the original's `BaseFacilityGrid` is this control-pointer array, not padding.
- **Consequence for the ordering.** Priority 1's remaining tail is `Game_StartNew` →
  `Geoscape_InitUI` → `LoadGame`, and both of the first two want the screens subsystem. The workable
  items are therefore in Priority 2, where `MapView_DrawUnitFacingArrow` and `MapView_DrawIsoLayer` are
  both done and only `Tact_DrawCommandPads` and `Render_Frame` remain. This is a change of order, not
  of goal: the geoscape chain stays open and is listed above.

## Later priorities

2. Tactical renderer leftovers. `MapView_DrawIsoLayer` is done, so what is left in this priority is
   `Tact_DrawCommandPads (0x1fc8c)` and `Render_Frame (0x22310)` itself — the caller that will own the
   `IsoLayerEnv` this step introduced. 3. Geoscape. 4. Screens. 5. Housekeeping. See `HANDOFF.md` §10.

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
  - And again with `MapView_DrawIsoLayer`: `pufo_isodraw.obj` now reaches
    `UiControl_BlitActivePad` through `pufo_tacthud.obj`, so `test_isodraw` links `pufo_ui` too —
    eighteen of the fifty-one test targets now have to.
- [ ] Three test targets fail in the **Debug** configuration and pass in Release. All three already
  fail at `04f1155` — verified by building that commit in a clean worktree with the same ClangCL
  toolset — so none of them came from the `Tact_InitMission` step. They are Debug-CRT artefacts:
  uninitialised memory and allocator block reuse, both of which the tests assume away.
  - `test_titles`: `Titles_DrawBackdrop @ 0x1e630` returns 3 at a scroll of 0 but still draws a
    credit row, while the test asserts it draws none. Either the row rule or the test is wrong;
    needs a fresh ARM read of `0x1e630`.
  - `test_save`: "the first item is unaffected" reads `0xCDCDCDCD` — the Debug CRT's uninitialised
    heap marker. Something on that path is read before it is written.
  - `test_template`: asserts that `MapTemplate_Load` produced a *different* `pBlob` pointer than the
    one it freed. The allocator is free to reuse the block, so the check passes or fails with heap
    state; it passed on rerun. This is the pointer-identity trap already noted in `HANDOFF.md`.
  These are not folded into the porting steps. They need their own pass.
- [ ] `Tact_LoadPalettes(i32 nGamma)` / `Tact_BuildPaletteLuts(i32 nGamma)` take an argument the
  original does not: in ARM both take none, and `Tact_BuildPaletteLuts` reads `Map_nGamma @ 0x128AB8`
  itself. `Tact_InitMission` passes `*pEnv->pnGamma` to keep the existing signature. Harmless, but it
  is a signature deviation carried in already-ported code.
- [ ] `TactShutdownEnv.pbActionLatch /* Map+0x00 */` conflicts with `Map_bActionLatch @ Map+0x02`;
  `Tact_InitMission` sets `Map+0x00` to 1 and `Map+0x02` to 0, so the two are different fields and one
  of the two comments is wrong.
- [ ] The cell grid is modelled twice: `GameMap.aCells` and `TactInitEnv.apCells` are the same
  14400-entry array reached through two different fields. One owner should be picked.
- [ ] `Ball_FloorDiv16` is misnamed. The ARM idiom it reproduces (`MOVS` / `ADDMI #0xF` / `ASR #4`) is
  **truncating** division, not floor: for `v < 0`, `(v + 15) >> 4` equals `v / 16` exactly, so -17 gives
  -1 and not -2. The arithmetic is right and matches the ARM; only the name and the older comments say
  "floor". Renaming touches three call sites in `pufo_ballist.c` plus the parity notes, so it is left as
  its own step. Recorded at `0x1B8EC` in `bugs.tsv`; the comments now state the truth.
- [ ] Map+0x1406C..0x14070 has three competing models. `pufo_mapctx.h` calls it
  `Soldier *apTraceExclude[2]`, `pufo_ballist.h` calls 0x1406C `bFireHitResult` and 0x1406D
  `bTrajHitStop`, and `MapView_DrawUnitFacingArrow` proves 0x14070 holds a `TactItem *` written by
  `Tact_SimulateThrowArc` (0x319F4). A byte pair and a pointer cannot both own the same four bytes, so
  one of the three is wrong. Not folded into this step — the arrow port takes the pointer through
  `IsoArrowGate.pThrownItem` and leaves the Map model alone.

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
| `Map_BuildMissionTerrain` | 50/50 | 7043 | 670/1103 (60.7%) | 250608/598792 (41.9%) | 34 checked, 0 mismatched |
| this commit (`Tact_LoadLoftData`) | 50/50 | 7048 | 671/1103 (60.8%) | 250792/598792 (41.9%) | 34 checked, 0 mismatched |
| `Tact_InitMission` | 51/51 (Release) | 7133 | 672/1103 (60.9%) | 254912/598792 (42.6%) | 34 checked, 0 mismatched |

| `BaseFac_SetCellBuildState` + `Base_GrantStarterItems` | 51/51 (Release) | 7170 | 674/1103 (61.1%) | 255372/598792 (42.6%) | 35 checked, 0 mismatched |
| `Ship_SpawnAtSlot` | 51/51 (Release) | 7189 | 675/1103 (61.2%) | 256036/598792 (42.8%) | 35 checked, 0 mismatched |
| `MapView_DrawUnitFacingArrow` | 51/51 (Release) | 7235 | 676/1103 (61.3%) | 258728/598792 (43.2%) | 37 checked, 0 mismatched |
| `MapView_DrawIsoLayer` | 51/51 (Release) | 7260 | 677/1103 (61.4%) | 259980/598792 (43.4%) | 37 checked, 0 mismatched |

Release is the documented configuration and it is green. In **Debug**, `test_titles` and `test_save`
fail and `test_template` is flaky; all three already fail at `04f1155` (verified by building that
commit in a clean worktree with the same ClangCL toolset), so none came from this step. See "Separate
issues".
