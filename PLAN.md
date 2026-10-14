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
- [x] `LoadGame (0x44bbc, 3,400 B)` — the whole driver is ported in `src/pufo/game/pufo_save.c`
  (`LoadGame` + `LoadGameEnv` + `LoadGameHooks`), verified by a sav7 round-trip in `test_save`.
  The callees that belong to other subsystems arrive as hooks and remain unported:
  `Geoscape_InitUI (0x857f4, 5,420 B)` — now ported (see the next entry), so the LoadGame hook
  can be wired to it in the shell. The three budget helpers `Base_CountOutboundTransferSpace @
  0x68044`, `Base_CountItemQtyAcrossBases @ 0x67d68`, `Base_CountPsiSoldiers @ 0x65a08` are
  ported in `pufo_geoscape.c` and called directly. Two IDB errors
  this step fixed: the tactver value goes to slot-context + 0x118 (`MainMenu_pSlotCtx+0x118`,
  write-only residue), NOT `apObjects[4]`; and the "shot object" whose words 29.. are zeroed is
  `apObjects[0]` itself (Map+0x108), not a separate global. In Priority 2 the map frame, the
  minimap and the start screen are now drawn; what is left there is `Tact_DrawCommandPads
  (0x1fc8c)` and the caller `Render_Frame (0x22310)`, which also wants `Geoscape_Draw (0x8c180)`
  from Priority 3.
- [x] `Geoscape_InitUI (0x857f4, 5,420 B)` — ported in `src/pufo/ui/pufo_geoiinit.{h,c}`, tested by
  `tests/test_geoiinit.c` against the shipped archives (real pads, real sprites, real research.dat,
  all 56 .cts files). Hooked out: `Game_StartNew (0x94554)` and `Geoscape_OnRadarListClick (0x854c4)`;
  the 13 root click handlers are env function pointers. ARM corrections over Hex-Rays: the 0x186BC0
  world block is a u16 pair (STRH/STRH) inside a 0x4E204 allocation; facility 10's "pointer" literal
  is the number 1300000; the research zero loop stores at topic+6 not the save-order wProgress; root
  click slots [8..12] are OpenScreen18/CloseScreen18/EnterBuildMode/OpenBaseInfo/OpenFunding;
  `+0xB6` on object 1 is uFirePower=110; prices are the u32 at +188. Text-record tail fact: lang.dat
  loads only 1650 records, records 1655..1710 are zero .data, so mission-site names are empty in the
  original too — the boot blob is now allocated at the full 1912-record capacity.
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
- [x] `MapView_DrawMinimap @ 0x1ed4c` (2,112 B) — ported in the same file behind `IsoMinimapEnv`, with
  37 new checks in `tests/test_isodraw.c`. It is `Render_Frame`'s screen-2 branch and reuses
  `IsoDrawCtx` without touching the per-cell chain.
  - [x] the two loop-bound globals `Map_nMinimapSpanX/Y` (Map+0x30/0x32) have no writer anywhere in
    the binary: the shipped minimap never runs its cell passes and draws only the pad, the label and
    the three controls. The port models them as env inputs and reproduces the arithmetic; the test
    pins both the dormant case and an explicit span.
  - [x] the scroll clamp settled: max `40*(nMapDimX-3)/4` and `40*(nMapDimY-4)/4` first, then min
    -30 / -40, each comparing the value the previous clamp left; the ARM's `MOVS / ADDMI #3 / ASR #2`
    is truncating division, so C's `/` reproduces it.
  - [x] `IsoDrawCtx` corrected: the scratch dword the minimap stores per glyph sits at 0xFC, so
    `szText` is 80 bytes and not the 84 previously assumed; `IsoSpriteBases` gained `nScangBase`
    (fontSys+0x2E, the minimap glyph group).
  - [x] IDB: named `Map_nMinimapSpanX/Y` (0x1139E8/0x1139EA, u16), `Map_nMinimapScrollX/Y`
    (0x118864/0x118868), `pMinimapCursorPad` (0x1571F0, `PadImage *`, the 'minimcur' pad
    `Tact_LoadUiPads` loads), `aTextRec17` (0xB466D = aTextRecs + 205*17); comments at 0xAD310
    (pBank+0xC40 = widgets 98..100, the screen-2 controls), 0x16440 (look-mode scroll seeding), and
    line comments at 0x1EEA4 and 0x1F4DC.
  - [x] three rows added to `bugs.tsv` (78 total): the dead span globals, the unguarded
    `Font_GetSprByIndex` dereference at 0x1EEA4, and the unbounded label copy at 0x1F4DC.
- [x] `MainMenu_Draw @ 0x1dbb8` (2,680 B) — ported as `src/pufo/game/pufo_mainmenu.c` behind
  `MainMenuEnv`, with 25 new checks in `tests/test_mainmenu.c`. It is `Render_Frame`'s start-screen
  branch.
  - [x] the mode dispatch on `MainMenu_nListMode` (0x157188): 0/1 draw four menu rows at x 50,
    w 140, y 115 + 22*i with labels from `aTextRecs` records 1076-1079 (mode 0) or 1080-1083
    (mode 1); 2/3 draw seven slot rows at x 25, w 190, y 65 + 22*i with labels from the slot
    context (`MainMenu_pSlotCtx` at 0x156938, 40 bytes each) plus the standalone control; mode 3
    additionally writes 40 into `anActionPhaseSave0[112]` and opens `UiModal_DrawDialog(150,
    record 1102, 1 line, no buttons)`.
  - [x] row frame = body (index 0, colours 87/94) + four 1-pixel strips (index 1, colours 159/171)
    via `SoftSurf_BlitSrcRect`, keyed by `MainMenu_anRowKeys[i]` forced to 255 by
    `MainMenu_abRowDisabled[i]`; the row text colour is the cyan pair (26,244,252)/(13,122,126)
    scaled by key/256 — the six WinCE float constants decode exactly to those fractions.
  - [x] IDB: named `MainMenu_nListMode` (int), `MainMenu_pBgGlyph`, `MainMenu_anRowKeys` (int[7]),
    `MainMenu_abRowDisabled` (u8[7]), `MainMenu_pStartCtl` (UiControl*), `MainMenu_pSlotCtx`,
    `anActionPhaseSave0_112` (int), `aTextRec1076/1080/1083`, `pszGameVersion` ("V1.26" @ 0xAAD28);
    function header at 0x1dbb8 and line comments on the mode branches.
- [x] `Tact_DrawCommandPads @ 0x1fc8c` (4,044 B) — ported in `pufo_isodraw.c` behind `TactCmdPadEnv`,
  with 42 new checks in `tests/test_cmdpads.c`. It is `Render_Frame`'s screen-4 branch, the selected
  unit's status panel.
  - [x] the unit-type gate (craft family 4..8 / 14..20 / 22 skips the pad slots), the `uMissions <= 3`
    empty-slot frames, and the class 0..5 -> cs pad pairs (0=(cs0,cs0) 1=(cs1,cs1) 2=(cs2,cs2)
    3=(cs0,cs1) 4=(cs1,cs2) 5=(cs0,cs2)); the panel rewrites the pads' own origins to (5,2)/(39,2)
    before every blit.
  - [x] eleven label/value rows from `aTextRecs` records 56..66 with current/max bars
    (`max>>1`, remainder `(max>>1)-(cur>>1)-2`, negative dropped by the clip), the kneel blocks, the
    fixed 200-wide morale bar with `2*(99-morale)` remainder, and the `bUnk422`-gated extra rows.
  - [x] the ruler at 0x20AD4 is an original bug: 12x24 5x1 colour-ramp ticks land exactly on the
    label rows just drawn and cut through them. Hex-Rays' `-6 - 5*col` right index is a sign-extension
    artifact; the ARM is `RSB R0,R2,#0xFA` = 250 - 5*col. Reproduced, logged in `bugs.tsv` (79 rows).
  - [x] ARM corrected Hex-Rays here: the loop scratch dwords are at 0x124/0x128 (now
    `IsoDrawCtx.nCmdTickCol/nCmdTickRow`), not what the pseudocode rendered.
  - [x] IDB: named `pPadSoldier` (0x128AC0, `PadImage *`), `pCmdPadCs0/1/2` (0x186C30/34/38,
    `PadImage *`, the 'cs0'/'cs1'/'cs2' pads loaded by `Assets_LoadStartup`), `aTextRec56..66`;
    function header at 0x1fc8c and line comments at 0x1FCEC, 0x1FE84, 0x1FFB4, 0x20AD4, 0x20C00.
- [x] `Game_StartNew @ 0x94554` (3,976 B) — ported in `pufo_geoiinit.{h,c}` behind `StartNewEnv`,
  107 checks in `tests/test_startnew.c` run over the real objects.dat and TEXTS records. All its
  callees were already ported (`Geoscape_BuildNavControls` landed in the previous commit). Found by
  the test: `Ship_ctor` reads the LIVE `Map_nCraftCount` for the cargo column (0x45C98), so
  `Ship_SpawnAtSlot` now syncs `pCtorEnv->nShipCount` before calling the ctor — without it every
  craft shares column 0. The starter facilities are placed with bClear 1, i.e. zero build days.
  Magic numbers replaced by IDB-verified enums: `FacilityKind` (0..16, main.dat TEXTS 106..118/463/
  120..122), `ObjectSlot` (28 slots, TEXTS 1447+slot), `CraftType` (mapped 1:1 onto TEXTS 522..526,
  recorded as an assumption), the `PUFO_GRID_*` layout macros, `PUFO_ITEM_QTY_COUNT`,
  `PUFO_CRAFT_TYPE_COUNT`, `PUFO_STARTNEW_SOLDIER_COUNT`, `PUFO_PRODUCT_ENTRIES_PER_BASE` promoted
  to `pufo_save.h`. Same enums added to the IDB and applied to `Ship_ctor` / `BaseFac_SetCellBuildState`.
- [x] shell wiring — `app/pufo_world.{h,c}` owns the geoscape state (`PufoWorldState`: rosters, grid,
  columns, sites, economy, UI state, viewport, nav controls) and `App_HookNewGame` runs
  `Geoscape_InitUI(0)` + `Game_StartNew` for real. `pocketufo --new-game --dump-frame <png>` reports
  4147000 funds, 1 base, 8 soldiers, 3 craft, screen 2, zoom 0. The geoscape click handlers and the
  two spawn hooks are no-op thunks until those screens are ported. `World_LoadSlot` wires
  `LoadGame @ 0x44bbc` over the same state (the chunk blobs live in `PufoWorldState`);
  `pocketufo --load-slot 7 --dump-frame <png>` loads the test save and reports
  "load slot 7: loaded (0 funds, 1 bases, 0 soldiers, 1 craft, ui screen 0)". The three teardown
  hooks the load path can reach (`Geoscape_InitUI` re-entry, `Tact_Shutdown`, `Tact_InitMission`)
  are stubs until those paths are mapped.
- [x] `Geoscape_OnRadarListClick @ 0x854c4` — the status panel's click, ported in `pufo_baseui.{h,c}`
  behind `RadarClickEnv`, 36 checks in `tests/test_baseui.c`. The panel row's `anLineAux` value IS the
  strateg item index (stored to 0x186BE4); six (nType, bActive) branches pause the clock and edit the
  root message list through the ported `UiLineList` leaves; `GameTime_Pause` is a plain store of 1 to
  0x15961C. The shell wires `pfnRadarClick` to it. IDB comments at 0x854c4, 0x186BE4, 0x1870A0, 0xAC224.
- [x] `Base_CanPlaceHangarAt @ 0x629b0` (968 B) + `BaseFac_FormatCostLabel @ 0x6a404` (184 B)
  — ported in `pufo_geoscape.{h,c}` (the first behind `HangarPlaceEnv`), 39 new checks in
  `tests/test_geoscape.c`. The placement test is a connectivity flood over the base's finished
  stamped cells: clearing the tapped 2x2 footprint must not cut any of them off; a hangar cell
  answers `Base_CalcHangarCap - Base_CountShipsAtBase > 0` instead, and `buildDays > 0` is
  accepted outright (both branches land on the `MOV R0,#1` tail at 0x62D44, not the layer2 tail
  Hex-Rays suggests). IDA's signature was wrong — the base is `nSelectedBaseIdx` at grid + 0x619,
  not an argument, and the fourth argument is dead — so the arguments are renamed and the function
  commented, with line comments at 0x629C8, 0x62A30, 0x62AD8, 0x62BE0, 0x62CE0, 0x62D44, 0x62D50.
  The stale "Not ported / OPEN QUESTION" comment on `Base_CountShipsAtBase` is corrected (its grid
  argument is dead; the byte table is the global at 0x159DBC), `byte_186B80` is named `nSmapTapY`,
  and `abBaseKindTable @ 0x1864DD` is described (15 bytes, no xrefs, indexed by facility kind).
  Three ledger rows (86 total): the dead 2x2 marking (0x62AD8), the scratch overrun at row 5
  (0x62B80), the kind 15/16 spill past `abBaseKindTable` (0x6A49C).
- [x] `Base_CreateNamedEntry @ 0x66930` (576 B) — ported in `pufo_geoscape.c` behind `BaseCreateEnv`,
  33 new checks in `tests/test_geoscape.c`. Chains `BaseRec_Create` + `StrategList_AddItem`, fills the
  `BaseRec` (bUnk17/nUnk8/nUnk12/bUnk16 from `abBaseKindTable`), the `aBaseFacilities` record (name,
  bType = kind - 0x80, nFacType = site id), the strateg entry's bRandA, and clears the base's
  research/product columns; ++site id, ++base count, returns the 1560 residue. Two ledger rows added
  (81 total): the unbounded name copy (0x669D0, port truncates) and the research-column spill
  (0x66B0C, reproduced). ARM fixed Hex-Rays again: `SUB R11,R10,#0x80`, not `+ 0x80`.
- [x] The six geoscape speed buttons — `GameTime_Set5Sec (0x84f00)`, `GameTime_Set1Min (0x84f64)`,
  `GameTime_Set5Min (0x84fc8)`, `GameTime_SetSpeedIdx3/4/5 (0x8502c/0x85094/0x850fc)`, 612 B total —
  ported in `pufo_geoscape.c` as six wrappers over one `GameTime_SetSpeedIdx`, 13 new checks in
  `tests/test_geoscape.c`. Each: `Sound_Play(17)`, store the interval (5/60/300/1380/2460/3600) into
  `nGeoscapeTickSeconds` (the port's `GeoscapeWorld.nTickSeconds`), and light exactly one of the six
  nav controls (`apGeoscapeSpeedCtl`, 0x186500..0x186514, typed this step).
- [x] The three leaves of the craft-select chain — `Smap_DismissSelected (0x84428, 92 B)`,
  `Smap_InitCraftSelectState (0x61b94, 248 B)`, `EndScreen_Init (0x388b0, 328 B)` — ported in
  `pufo_strateg.c` (beside `Smap_ClearSelection`), `pufo_geoscape.c` (beside
  `BaseFac_SetCellBuildState`) and `pufo_titles.c`, 42 new checks in `tests/test_geoscape.c` and
  `tests/test_titles.c`. These were the last unported callees of `Smap_BeginCraftSelect (0x838dc)`,
  so that function and its 20-byte button wrapper are now fully unblocked. ARM pinned the
  tile→mission-type map Hex-Rays only named: 240→desert 6, 241→jungle 0, 242→polar 8, 243→forest 2,
  244→culta 1, cross-checked against `g_aTerrainModuleCount`'s index order. One ledger row added
  (82 total): the hangar stamp's past-the-grid stores (0x61C34, latent, port guards). IDB: typed
  `abTactTileTypes` (0x159D5C), `aCraftPlacementCodeByFacType` (0xA6568), `aGoodEndRowRecs` /
  `aBadEndRowRecs` (0xA6458/0xA6480), `aTitlesRowRecs` (0xB35D8); renamed `nTitlesMaxRows`,
  `pTitlesBackdrop`, `nTitlesScroll`, `fontSys_nGlyphBase` (0x158290 = fontSys + 0x28, the glyph
  base set before "font" loads), `nSelectedBaseIdx`, `nAssaultBaseRace`, `bCraftSelectAssaultFlag`,
  `bMapSelectionFlag`; function headers at 0x84428, 0x61b94, 0x388b0, 0x84634 and 0x838dc.
- [x] `Smap_BeginCraftSelect (0x838dc, 2424 B)` + `Smap_BeginCraftSelectBtn (0x84634, 20 B)` —
  the whole craft-select decision tree, ported in `pufo_strateg.c` behind a `SmapCraftSelectEnv`
  that models the Map-block globals, the geoscape state and the three injected hooks
  (`GeoscapeUi_FreeAll`, `Tact_InitMission`, `EndScreen_Init`); 46 new checks in
  `tests/test_geoscape.c` (urban/alien-base/crash flags, the Xcom launch with and without
  assigned soldiers, the bad-end path). ARM pinned the decision order Hex-Rays garbled: the
  visibility pass runs before the mode gate; the Xcom launch keeps mode 1 so the tail's
  `Smap_DismissSelected` is skipped and the line survives; the funding deduction is
  `800 + 400*(crafts homed at the base) + 30*Base_CalcOccupiedSpace`; the retarget inside
  `DismissSelected` re-raises the strateg entry's `bVisible` over the visibility clear. Deviations
  recorded in the header: the default craft-class arm's divide-by-zero budget, the unchecked
  `apBases[base]->bUnk4` dereference. IDB: call edges from 0x838dc verified against
  `Smap_InitCraftSelectState`, `Base_CountSoldiersAssigned`, `Base_CalcOccupiedSpace`,
  `Base_DemolishFacility`, `UiPanel_AppendLineWithSound` (port name recorded in renames.tsv).
- [x] The first two pieces of the research screen chain — `Research_TopicNeedsItem (0x72c8c, 136 B)`
  and `ResearchScreen_RefreshPanels (0x73008, 568 B)` — ported in `pufo_baseui.c`, 24 new checks in
  `tests/test_baseui.c`. This opened the in-memory research topic table (144 × 34 bytes at
  0x15EF08, `ResearchTopicLive`): +4 fire gate, +8 progress total, +10 progress value, +12
  active-on-list flag, +13 menu gate, +14 required item id. ARM corrected two Hex-Rays artifacts:
  the topic gate is two bands (55..65, 118..142 minus 135/140), not the reconstructed boolean; and
  the research quantity entry is `column + 2*topic` — the "+4" the pseudocode showed is the 0xBE4
  apLines offset leaking through R4, not a column header. One ledger row added (83 total): the
  progress division tests only the numerator (0x73184). IDB: function comments at 0x72c8c/0x73008
  and the record layout at 0x15EF08.
- [x] The start-menu render pipeline runs end to end on Win64 — `GameUI_InitStartup (0x38a28, 852 B)`
  and `UiControl_LoadPads (0x1a604, 344 B)` ported (`pufo_mainmenu.c`, `pufo_control.c`), plus the
  WndProc 0xC352 screen-11 fragment ported as `MainMenu_TickRowKeys`. The shell reaches the
  screen-17 branch of `Render_Frame (0x22310, dispatcher still unported)` and the first visual
  result is confirmed through a `--dump-frame` PPM dump: backdrop, four teal rows with black
  Cyrillic labels, SMK logo. Four font-path facts came out of it: glyph run byte 0 is
  **transparency** (ARM 0xa4288 gates the write on `*v5 != 0` — the port wrote it as a real pixel);
  `Font_BlitGlyphToSoft @ 0x9ec48` picks the palette as slot `Map_nGlyphPalBase + nPal` (renamed
  from unk_1183F4); `Text_DrawString @ 0x2d798` forces that base to **2** while drawing, because
  `Gfx_PackRgb565Pair @ 0x2d82c` writes the caller's colour pair into slot 2 entries 27/33
  (unk_147B3C/unk_147B54 — not scratch, palette entries) and the font glyphs use exactly those
  indices (measured over FONT.SPR: only 0/27/33) — that is the text colour mechanism the port had
  missed (text came out black); and `GX_PresentFrame @ 0x9ea7c` arms the blit target through
  `Render_SetTarget` — without that call `blitState+0x20` stays 0 and no glyph ever draws, so the
  shell now calls it in `App_PresentLoadedSurface`. IDB: comments at 0x38a28/0x1a604/0x9ec48/
  0x2d798/0x2d82c/0xa41d4/0xa0ac8/0x1183f4, rename unk_1183F4 → Map_nGlyphPalBase.
- [x] `Render_Frame (0x22310, 756 B)` — the frame dispatcher itself — ported as
  `src/pufo/game/pufo_renderframe.c` behind `RenderFrameEnv`, tested by `tests/test_renderframe.c`
  (18 new checks). Every callee except `Geoscape_Draw (0x8c180)` was already ported, so the env
  carries the dispatch scalars (`unk_1139FC`, `Map_nUiScreen`, `unk_138E68`, the modal
  `apUiSlotsA[12..19]` cells, the fade `apUiSlotsA[1]/[10]`) plus the per-branch sub-envs, and
  `Geoscape_Draw` / the `GX_Present (0x27788)` thunk are injected hooks. The geoscape branch
  returns `Geoscape_Draw`'s value and skips the present (0x22328/0x22330) — reproduced. The shell
  now reaches the menu through the dispatcher instead of calling `MainMenu_Draw` directly. IDB:
  comment at 0x22310.
- [x] The start menu is clickable end to end. `Input_OnPointer (0x13c6c)` / `Input_OnPointerUp
  (0x148d0)` were already ported; the shell now feeds them from `WM_MOUSEMOVE` / `WM_LBUTTONDOWN` /
  `WM_LBUTTONUP` through the letterbox inverse (`App_MapPointer`, the exact inverse of
  `Gx_Present`'s scale-and-center), with a `TactPointerWorld` whose `pBank` is the same bank slice
  `GameUI_InitStartup` installed the menu handlers into (widgets 113..127). Press marks the rows
  under the pointer in `abRowDisabled` (mode 0/1: 4 rows x[50,190] y[115+22i..]; mode 2: 7 rows
  x[24,216] y[65+22i..83+22i], plus the start-control hit test); release fires the first marked
  row's handler and clears the marks (mode 0 clears only the rows BEFORE the hit row; the hit row
  itself only when the row-sticky byte unk_188D4C is 0). A new `--click <x> <y>` debug flag drives
  the same path headlessly: `--dump-frame out.ppm --click 120 148` clicks mode-0 row 1
  (`MenuCtl1_OnClick @ 0x1a034`: sample 16, `SaveSlot_FormatList`, list mode 2) and the dump shows
  the seven-slot load list (all labels from text record 1074 — no `.sav` files in the run directory)
  plus the "Отмена" back control (record 1083) at y 230. No new functions credited; IDB comments at
  0x13c6c / 0x148d0.
- [x] The credits screen opens. `Titles_Init (0x380d8, 404 B)` and `Titles_ctor (0x46e88, 284 B)`
  ported in `pufo_titles.c` (24 new checks in `tests/test_titles.c`), wiring the already-ported
  scene loader / star / particle / credit-lines chain. The titles surface is 240x320 at **mode 0 =
  one byte per pixel** (hence the 0x12C00 memset and the indexed-glyph present); 190 stars seed at
  y −320..−1 and walk in; the 44-row table comes from `unk_A63A8` with record 0x5A0 as a recurring
  spacer. The credits scroll is state +87228 (not unk_138E88): the WndProc 0xC352 screen-0x10 branch
  advances it by 1 per tick wrapping past 20*(rows+48) (0xA0C58) — ported as `App_TickAnim`, and
  `Titles_DrawAnimatedFrame` aliases it into the credit-lines env. `unk_156930` confirmed
  write-only residue (cleared here and in `Map_ComputeReachable`, never read). Verified: `--click
  120 170` (MenuCtl2) dumps the first credit line at y 300 with stars entering. IDB: comments at
  0x380d8/0x46e88/0x1571A0/0x156930, rename unk_1571A0 → Titles_apGlyphs.
- [x] `Geoscape_BuildNavControls (0x67b1c, 588 B)` — ported in `src/pufo/game/pufo_baseui.{h,c}`,
  tested by `tests/test_baseui.c`. It allocates 15 `UiControl` objects into the pointer array at
  0x159888 (15 slots: 10 + 2 + 3) and installs a click handler in each. Nine handlers were already
  ported; this unit adds `SoldierScreen_FindNextAtBase (0x6192c)`, `SoldierScreen_FindPrevAtBase
  (0x618c4)`, `Ui_SetTransferFlag (0x619a8)`, `NewBaseScreen_Open (0x615d0)` and
  `NewBaseScreen_TryConfirm (0x6152c)`. Still hooked: `SoldierScreen_Toggle (0x64af0)`,
  `ResearchScreen_Open (0x616ac)` (env click thunks) and `Geoscape_OnMapClick (0x8b4a8)`
  (TryConfirm's tail call). The descriptors are `aGeodescNav/Panel/Find` (0xAB4B0/0xAB5F0/0xAB630),
  printed by `tools/gen_geoiinit_tables.py --check`. ARM facts: the cost read is
  `ADD base+byte*4` then `LDR [R3,#-0x200]` (index = `byte_159DF4 - 128`, literal pool confirms the
  0xAB488 base); 0x1870AC is a byte read; the shortfall store at 0x1870C8 is a dword while every
  reader loads a byte; FindNext/Prev compare LDRB against a sign-extended LDRSB, so a negative
  selected base matches nothing; FindPrev never reads the soldier count.
  - By-product kept: the 84-byte gap between `buildDays` (288 bytes) and `fac` (at +0x174) inside
    the original's `BaseFacilityGrid` is this control-pointer array, not padding.
- **Consequence for the ordering.** Priority 1's remaining tail is `LoadGame` — `Game_StartNew` and
  `Geoscape_InitUI` are ported and wired into the shell (`app/pufo_world.c`), and they want the
  screens subsystem for anything visible. The workable
  items are therefore in Priority 2, where `MapView_DrawUnitFacingArrow`, `MapView_DrawIsoLayer`,
  `MapView_DrawMinimap`, `MainMenu_Draw`, `Tact_DrawCommandPads`, `Render_Frame` itself and the
  screen-17 click wiring are done. This is a change of order, not of goal: the geoscape chain stays
  open and is listed above.

## Later priorities

2. Tactical renderer leftovers. `MapView_DrawIsoLayer`, `MapView_DrawMinimap`, `MainMenu_Draw`,
   `Tact_DrawCommandPads` and `Render_Frame (0x22310)` itself are done — the dispatcher owns the
   `IsoLayerEnv` / `IsoMinimapEnv` those steps introduced and injects `Geoscape_Draw (0x8c180)` from
   Priority 3. This priority is closed.
   3. Geoscape. 4. Screens. 5. Housekeeping. See `HANDOFF.md` §10.

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
| `MapView_DrawMinimap` | 51/51 (Release) | 7297 | 678/1103 (61.5%) | 262092/598792 (43.8%) | 37 checked, 0 mismatched |
| `MainMenu_Draw` | 52/52 (Release) | 7322 | 679/1103 (61.6%) | 264772/598792 (44.2%) | 37 checked, 0 mismatched |
| `Tact_DrawCommandPads` | 53/53 (Release) | 7364 | 680/1103 (61.7%) | 268816/598792 (44.9%) | 37 checked, 0 mismatched |
| `GameTime_Set5Sec..SetSpeedIdx5` | 53/53 (Release) | 7377 | 686/1103 (62.2%) | 269428/598792 (45.0%) | 37 checked, 0 mismatched |
| `Base_CreateNamedEntry` | 53/53 (Release) | 7410 | 687/1103 (62.3%) | 270004/598792 (45.1%) | 37 checked, 0 mismatched |
| craft-select leaves (`Smap_DismissSelected`, `Smap_InitCraftSelectState`, `EndScreen_Init`) | 53/53 (Release) | 7452 | 690/1103 (62.6%) | 270672/598792 (45.2%) | 37 checked, 0 mismatched |
| `Smap_BeginCraftSelect` + `Smap_BeginCraftSelectBtn` | 53/53 (Release) | 7498 | 692/1103 (62.7%) | 273116/598792 (45.6%) | 37 checked, 0 mismatched |
| `Research_TopicNeedsItem` + `ResearchScreen_RefreshPanels` | 53/53 (Release) | 7522 | 694/1103 (62.9%) | 273820/598792 (45.7%) | 37 checked, 0 mismatched |
| start menu pipeline (`GameUI_InitStartup`, `UiControl_LoadPads`) | 53/53 (Release) | 7522 | 696/1103 (63.1%) | 275016/598792 (45.9%) | 37 checked, 0 mismatched |
| `Render_Frame` dispatcher (`pufo_renderframe.c`) | 54/54 (Release) | 7540 | 698/1103 (63.3%) | 275776/598792 (46.1%) | 37 checked, 0 mismatched |
| start menu click wiring (`--click` dump verified) | 54/54 (Release) | 7540 | 698/1103 (63.3%) | 275776/598792 (46.1%) | 37 checked, 0 mismatched |
| credits opener (`Titles_Init`, `Titles_ctor`) | 54/54 (Release) | 7564 | 700/1103 (63.5%) | 276464/598792 (46.2%) | 37 checked, 0 mismatched |
| `LoadGame` driver (`pufo_save.c`, sav7 round-trip) | 54/54 (Release) | 7599 | 701/1103 (63.6%) | 279864/598792 (46.7%) | 37 checked, 0 mismatched |
| budget leaves (`Base_CountOutboundTransferSpace`, `Base_CountItemQtyAcrossBases`, `Base_CountPsiSoldiers`) | 54/54 (Release) | 7608 | 704/1103 (63.8%) | 280724/598792 (46.9%) | 37 checked, 0 mismatched |
| `Geoscape_InitUI` (`pufo_geoiinit.c`, real-archive test) | 55/55 (Release) | 7673 | 708/1103 (64.2%) | 291100/598792 (48.6%) | 37 checked, 0 mismatched |
| nav controls + new-base screen (`pufo_baseui.c`) | 55/55 (Release) | 7773 | 722/1103 (65.5%) | 319208/598792 (53.3%) | 38 checked, 0 mismatched |
| `Game_StartNew` + the verified enums (`pufo_geoiinit.c`) | 56/56 (Release) | 7880 | 722/1103 (65.5%) | 319208/598792 (53.3%) | 38 checked, 0 mismatched |
| radar-list click + shell wiring (`pufo_baseui.c`, `app/pufo_world.c`) | 56/56 (Release) | 7916 | 723/1103 (65.5%) | 324112/598792 (54.1%) | 38 checked, 0 mismatched |
| hangar placement + the facility cost label (`pufo_geoscape.c`) | 56/56 (Release) | 7955 | 725/1103 (65.7%) | 325264/598792 (54.3%) | 38 checked, 0 mismatched |

Release is the documented configuration and it is green. In **Debug**, `test_titles` and `test_save`
fail and `test_template` is flaky; all three already fail at `04f1155` (verified by building that
commit in a clean worktree with the same ClangCL toolset), so none came from this step. See "Separate
issues".
