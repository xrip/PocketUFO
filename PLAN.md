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
- [x] `Geoscape_OnMapClick @ 0x8b4a8` (1184 B) + `BaseFacScreen_Init @ 0x5a198` (252 B) — ported in
  `pufo_baseui.{h,c}` behind `MapClickEnv` / `BaseFacInitEnv`, 74 new checks in `tests/test_baseui.c`,
  and the shell runs the map click for real through the confirm control (`app/pufo_world.c:
  World_TryConfirmClick`). The click branches on `nGeoscapeScreen`. Screen 1 runs the hangar test with
  `nSmapTapX/nSmapTapY` (0x186B7C/0x186B80) and clears the tapped cell: tileA 9..12 takes the whole
  2x2 footprint, whose four garbled stores decode to `144*nBase + 36*i + 6*nY + nX + {0,1,6,7}` with
  `i` walking tileA/tileB/layer2, the footprint starting at the tapped cell (the anchor quadrant
  survives). Screen 2 commits the pending facility — the base scalars, `bType = kind + 0x80`, the cost
  label into the record's `abName` at +26, the strateg entry found by the SIGNED `bUnk4` — or, without
  `bBaseFacConfirm`, sets `nSelectedBaseIdx` to the base COUNT and tail-calls `BaseFacScreen_Init`,
  which builds the dialog's up/down pair from the `stru_AB220` descriptors and installs their +12
  handlers BEFORE `UiControl_Setup`; Setup does not overwrite them (verified). IDB: function comments
  at 0x8b4a8 / 0x5a198, `unk_114B2C` renamed `apBases` (Map + 0x1174, the `BaseRec *` roster), line
  comments at 0x159DF4 (`bPendingBaseKind`: 0xFF is the "nothing pending" sentinel, the .data value is
  0, nothing in the new-game path writes it, and the writer is `Geoscape_Draw` 0x91D90), 0x186B00,
  0x158874, 0x158888. One ledger row (87 total): the strateg write past the last entry on a search
  miss (0x8B5E4). Deviations: the port bounds that write and returns an explicit status where the
  original returns register residue.
- [x] The options screen's save and load slots — `SaveGame_Slot0..6_CloseUI @ 0x6af80..0x6b148`
  (76 B each) and `LoadGame_Slot0..6_InitOptions @ 0x6b194..0x6b404` (104 B each). 14 functions,
  1260 bytes, 15 new checks in `tests/test_baseui.c`. The save body sets list mode 4 (the value
  `MainMenu_Draw` has no layout for, now `PUFO_MAINMENU_MODE_SAVING`), writes the slot, clears the tab
  byte, restores mode 1 and tail-calls `Geoscape_CloseUI`. The load body clears the tab byte and the
  geoscape screen id, sets mode 3 and the "controls exist" flag — so `OptionsScreen_Init` takes its
  teardown branch before the world is replaced — runs `LoadGame`, and treats a zero LOW BYTE of the
  result as failure (mode back to 1, return 1). Both drivers were already ported, so the test runs
  them for real against a slot path that cannot exist. `pufo_save.h`'s stale "LoadGame is NOT ported"
  header line is corrected. IDB comments on 0x6af80 and 0x6b194.
- [x] The options screen's audio tab — `OptionsScreen_Init @ 0x6be74` (760 B),
  `OptionsScreen_LayoutButtons @ 0x6b6c4` (232 B), `OptionsScreen_BindSaveSlots @ 0x6b49c` (236 B),
  `OptionsScreen_BindLoadSlots @ 0x6b588` (236 B), `OptionsScreen_RefreshVolumeHighlight @ 0x6b8fc`
  (152 B) and the twelve radio handlers `OptionsScreen_SetMusicVol0..5 @ 0x6ba14..0x6bbd0` /
  `OptionsScreen_SetSfxVol0..5 @ 0x6bc1c..0x6be10` (1128 B). 17 functions, 2744 bytes, 48 new checks
  in `tests/test_baseui.c`. Each handler clears its radio group, lights its own button, writes the
  step, and mutes only at step 0; sfx then tail-calls `Sound_Play(17)` so the player hears the new
  level; the level is `4 * (muted ? 0 : 12 * step + 4) - 1`. IDB: the control block is typed as
  `aOptionsListCtl[7]` @ 0x159E20, `aOptionsMusicBtn[6]` @ 0x159E44, `aOptionsSfxBtn[6]` @ 0x159E5C
  (`UiControl *`), the two descriptor tables at 0xAB750 / 0xAB830 are labelled and added to
  `tools/gen_geoiinit_tables.py`, and the stale "NOT ported" comment on 0x6be74 is gone. Two ledger
  rows: the unguarded radio lookup (0x6B93C / 0x6B984, not reproduced) and the return hotspot centred
  on the save control's width (0x6B764, reproduced).
- [x] `RadarScreen_RollWeaponHit @ 0x5db24` (352 B) + `RadarScreen_BlitOverlay @ 0x5d6d4` (16 B) —
  ported in `game/pufo_geoscape.{h,c}` behind `RadarHitRollEnv`, 21 new checks in
  `tests/test_geoscape.c`. The intercept weapon roll: one `Game_RandU16(100)` gates the event and a
  second picks the band (25 flat -50 under 50; 26 `+RandU16(20)-100` under 80; 27 `+RandU16(20)-120`
  under 90; 28 `+RandU16(30)-90` under 70; 29 `+RandU16(50)-140` under 70; 30 `+RandU16(30)-230`
  under 95), the HP accumulator written first and then mirrored into `StrategItem.nHp` of
  `strategList[nInterceptStrategIdx]`, unclamped. IDB: `unk_159698` → `nInterceptTargetHp`,
  `unk_1596E4` → `pRadarOverlayPad`, comments at 0x5d6d4 and 0x5db24. Citation fix: `GameTime_Unpause`
  was cited at 0x5d6c4, four bytes inside the function; the original starts at 0x5d6c0.
- [x] `Soldier_FormatDisplayName @ 0x6654c` (384 B) + `Ship_FormatDisplayName @ 0x666cc` (424 B) —
  ported in `game/pufo_baseui.{h,c}` and `game/pufo_ship.{h,c}`, 18 new checks in
  `tests/test_baseui.c` and `tests/test_ship.c`. The two craft-name formatters: `Ship.bCraftType`
  picks `aTextRecs` records 137/138/139 for types 0..2 and 418/419 for types 3/4 (those two also put
  a "-" before the number), and the number is `Ship.bType` @ +0, the per-craft-type ordinal, not the
  craft id. The soldier side keys off `Soldier.bUnk183`: 0xFF is record 93, 0xFE is record 140, and
  below 0xFE only craft types 0..2 get named — types 3 and 4 write nothing and the original returns
  the scratch string as the previous call left it. Both copy into `szRadarMsg @ 0x186B10` (108 bytes
  before `nSmapTapX @ 0x186B7C`) with unbounded loops, and the ship one copies an uninitialised local
  for a type outside 0..4 (three new bugs.tsv rows). IDB: seven labels renamed to
  `aTextRec93/137/138/139/140/418/419`, function comments at 0x6654c and 0x666cc. Shell: the soldier
  screen's craft column now runs the real formatter instead of `World_SoldierNameStub`.
- [x] `BaseFacScreen_DrawBuildDialog @ 0x59b60` (1592 B) — ported in `game/pufo_baseui.{h,c}` behind
  `BuildDialogEnv`, 23 new checks in `tests/test_baseui.c`. The whole build-dialog frame and the only
  caller of `BaseFacScreen_DrawPlacementOverlay`: the funds line, the animated panel frame, the
  selected `aBaseFacFlags` record name centred at y = 236, the three row labels, the cost, the literal
  "50000", the build-days line, the title, the pad blit and the overlay; the unarmed branch clears
  `bBaseFacOverlayShown`, draws the panel's own rows and swaps the title; both branches end on the
  shared shortfall tail (0x5A138). IDB: function comment at 0x59b60, and the seven runtime labels
  turned out to be `aTextRecs @ 0xB38D0` records at stride 205 — `unk_BB5BC/B7B47/B7C14/B7CE1/B7DAE/
  C7DEE/ED01C` → `aTextRec156/83/84/85/86/406/1148`, with `unk_BB422` → `aFacDaysSuffix` (record 154
  + 4, the only slice). New `FacBuildSpec_GetDays` in `game/pufo_geoscape.{h,c}` for the days byte.
- [x] `Ui_AnimExpandBlit @ 0x95bf0` (948 B) + `Ui_AnimExpandBlit_IfEnabled @ 0x663c4` (88 B) — ported
  in `ui/pufo_panel.{h,c}` behind `UiAnimBlitEnv`, 19 new checks in `tests/test_panel.c`. The animated
  panel frame every geoscape screen draws: a started byte, a signed 0..100 progress byte, five growth
  modes, width/height clamped down and origin clamped back up, the progress byte set to 100 only when
  the clamped rect is exactly the requested one, then the body blit plus the eight-bar bevelled frame
  in one of two colour sets picked by the byte at root + 0x6E. IDB: function comments at 0x95bf0 and
  0x663c4, `byte_187080` → `bUiAnimStarted`, the progress byte at 0x187058 → `cUiAnimProgress`
  (`__int8`, LDRSB — Hex-Rays renders it as `MEMORY[0x187054][1]`, a dword-scaled index that reads as
  0x187055), the frame-style byte at root + 0x6E → `bUiFrameStyle`.
- [x] `BaseFacScreen_DrawPlacementOverlay @ 0x58e10` (3396 B) — ported in `pufo_baseui.{h,c}` behind
  `FacOverlayEnv`, 35 new checks in `tests/test_baseui.c`. It is the preview and the commit at once:
  the tap comes from the pending pair or (with `bBaseFacPreviewActive`) from the cursor, the score is
  the 2x2-or-1x1 footprint plus the four-side walk gated by `buildDays == 0`, the outline is four
  rects in the phase colour or its inverse, and a legal tap with enough funds runs
  `BaseFac_SetCellBuildState` and spends `aBaseFacFlags[kind].uCost` — in new-base mode also
  `Base_CreateNamedEntry` plus `aNewBaseCostByRace[kind - 0x80]`, then closes the dialog and puts
  `nGeoscapeScreen` back to 1. The facility's grid type is `aBaseFacFlags[kind].bFlag21`, which InitUI
  fills with the record index. Two Hex-Rays renderings were corrected against the ARM: the "up"
  neighbour guard is the ROW (0x592C8), and the span reads below it are +384 and +385, not two +385.
  IDB: function comment at 0x58e10, line comment at 0x592C8, `qword_15887C` → `nPendingPlaceX` /
  `nPendingPlaceY`, `unk_15888C` → `bBaseFacOverlayShown`, `unk_159DC4` / `unk_159DC8` →
  `nSmapOriginX` / `nSmapOriginY`, `word_186BD0` / `word_186BD4` → `nSmapCursorX` / `nSmapCursorY`
  (`__int16`, LDRSH), `unk_18652C` → `bBaseFacPreviewActive`, `unk_18652D` → `bBaseFacPreviewOk`,
  `unk_1139EF` → `nMapPhase`. One ledger row (93 total).
- [x] `SoldierScreen_EquipSelected @ 0x619cc` (448 B) — ported in `pufo_baseui.{h,c}` behind
  `SoldierEquipEnv`, 28 new checks in `tests/test_baseui.c`, and the soldier screen's equip panel now
  runs for real in the shell (`World_SoldierEquipThunk`). The gate is `Soldier.bUnk183 >= 0xFE ||
  apCraft[bUnk183].bAtBase` (a soldier on a craft that is off-base cannot be re-equipped); the row
  comes from `pSolPanelEquip->anLineAux[nUnk3032]` and the equipment is `apObjects[121 + nUnitType]`,
  so row 0 returns the equipped item to the base and rows 1..3 swap objects 122..124 against it, and
  the tail clears `bTransferFlag @ 0x159E10`. IDB: function comments at 0x619cc and 0x62d78,
  `Ui_DrawTextInputDialog` renamed `SoldierScreen_DrawEquipPanels` (its body fills and draws the
  equip/stores panels from the selected base's stock — nothing to do with text input),
  `unk_113CA4/113CA8/113CAC/113CB0` → `Map_apObjects121..124`, `dword_159D84/88/8C` →
  `nEquipStock122/123/124`, `byte_AB710` → `nEquipRowCount`. One ledger row (92 total).
- [x] citation hygiene: the two NEGATED citations `audit_credits` flagged are gone — `pufo_baseui.h`
  no longer claims `SoldierScreen_Toggle @ 0x64af0` in the same breath as denying it, and
  `pufo_world.c` writes `StrategList_ProcessEvents (0x7fcb0)`. Dropping the unported 4904-byte event
  driver makes the headline honest rather than bigger: 727/1103 (65.9%) and 322856 bytes, not
  728 / 327760.
- [x] `SoldierScreen_Toggle @ 0x64af0` (1952 B) + `SoldierScreen_SelectListed @ 0x64a9c` (72 B)
  — ported in `pufo_baseui.{h,c}` behind `SoldierScreenEnv` / `SoldierSelectEnv`, 53 new checks in
  `tests/test_baseui.c`, and nav control slot 2 now opens the soldier screen for real
  (`World_SoldierToggleThunk`). Toggle is a byte switch on `bSoldierScreenOpen @ 0x159DE0`: sound 17
  and `nGeoscapeScreen = 4` on either direction; the open branch builds the pad
  (`PadImage_Load("soldier", 8)`) and four `UiPanel`s — equip (4 lines) and stores (4 lines) fixed,
  names and craft one row per soldier whose `Soldier+0xB6` byte equals `nSelectedBaseIdx @ 0x159D81`
  (that byte is the assigned base, not a kind) — then the row pass copies the name into the names
  panel with `anLineAux` = the **roster** index, the `Soldier_FormatDisplayName` result into the
  craft panel, and the 254 → (250,150,150) / 255 → (150,150,250) line colours; the close branch
  restores screen 1 and releases five objects. `SoldierScreen_SelectListed` sets screen 5 and
  `nSoldierCursor @ 0x159E08` from that aux. IDB: function comments at 0x64af0 / 0x64a9c, line
  comments at 0x651E4 and 0x651EC, `unk_1598D4` → `pSoldierPad` (`PadImage *`), `unk_115350` →
  `nSoldierCount` (`u16`, LDRH at every read), the panels renamed `pSolPanelStores` / `pSolPanelNames`
  / `pSolPanelCraft` (`pSolPanelEquip` already matched), and the 512-entry `Soldier *apSoldiers[512]`
  array created at Map + 0x1198 (0x114B50 — it ends exactly at `nSoldierCount`). No coverage delta:
  both addresses were already `@`-cited at HEAD in `pufo_baseui.h` while unported, so this unit turns
  two false credits into honest ones (2024 B). One ledger row (91 total): the unguarded `nLineCount`
  stores (0x651E4 / 0x651EC).
- [x] `BaseFacScreen_Toggle @ 0x589ec` (1060 B) + `BaseFacScreen_TryBuySelected @ 0x58910` (176 B)
  — ported in `pufo_baseui.{h,c}` behind `BaseFacToggleEnv` / `BaseFacBuyEnv`, 55 new checks in
  `tests/test_baseui.c`, and the shell's `pfnToggle` now builds the dialog for real. Toggle is a
  one-byte switch on `bBaseFacDialogOpen @ 0x158884`: the close branch releases the two panel
  controls and the `UiPanel`; the build branch counts the flagged `aBaseFacFlags` records, allocates
  the panel (0x1BB4 + `UiPanel_ctor`), sets the twelve style fields, writes one row per flagged
  record 1..16 with `anLineAux` = the record number (the facility kind the buy handler looks up),
  adds a 17th row unconditionally, drops `nLineCount` by 1, and in new-base mode wipes the selected
  base — buildDays/tileA/tileB/layer2 to 0 and facType to 0xFF (Hex-Rays'
  `unk_1598DC[0].tileA.cell[0][6*(24*base+row+18)+col]` is the facType array at grid + 0x174 +
  144*base + 108). IDB: function comments at 0x589ec / 0x58910, line comments at 0x58C18 and
  0x58A7C, and the four dialog globals typed and named — `unk_158884` → `bBaseFacDialogOpen` (byte),
  `unk_158888` → `bBaseFacPlaceArmed` (byte, STRB at 0x5A274 — the port had it as an i32),
  `unk_15886C` → `pBaseFacPanel`, `unk_158868` → `nBaseFacPanelSel`. `BaseFacFlag` moved from
  `ui/pufo_geoiinit.h` to `game/pufo_geoscape.h` so the map click and the dialog can both name it
  without an include cycle. Three ledger rows (90 total): the 17th row read out of `aBaseFacFlags`
  (0x58C18), the untested panel store (0x58A7C), the unchecked facility-kind index (0x58944).
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
- [x] the research allocation family, part 1: `ResearchScreen_IncSelectedQty (0x72ea0)`,
  `ResearchScreen_DecSelectedQty (0x72f70)`, `Base_GetScientistCount (0x6812c)` and
  `Base_GetEngineerCount (0x681f4)` — ported in `pufo_baseui.c` / `pufo_geoscape.c`, 42 new checks.
  The two crew getters are one expression (a crew item's per-base total minus that base's allocated
  column sum) with no floor. IDB: `unk_113C80/84` → `Map_apObjects112/113` (`ObjectRec *`),
  `Map_apObjects105` retyped 1 → 4 bytes, `unk_160C68` → `pResearchTopicPanel`, `unk_160C74` →
  `nResearchTopicRowCache` (a cached `nUnk3032`, written by 0x73328), `unk_16022C` → `auResearchCol`
  (`u16[1152]`), and the misnamed `abBaseCraftLoot` at 0x160C4C → `nResearchEntryCount`.
- [x] part 2: `ResearchScreen_UpdateProgress (0x72d14)`, `ResearchScreen_CancelSelectedAndClose
  (0x73380)` and `ResearchScreen_AccumulateAllocated (0x74914)` — 34 more checks. ARM facts: the
  allocate button's two cross-references are `ResearchScreen_Draw` storing it as `pfnClick` of the
  control at 0x186B94 (0x7417C), not calls; the loop count is the scientist TOTAL while both gates
  sum only the first `nResearchEntryCount` entries, so a count that does not cover the topic being
  written makes both gates blind to the loop's own stores; the hourly accumulate walks a fixed eight
  base slots and its inner loop bound is re-read from the count byte on each non-zero iteration
  (`LDRBHI` 0x7497C). `unk_186B94` was left unnamed — `ManufScreen_DrawFrame` uses the same control.

- [x] part 3, the manufacturing mirror: `ManufScreen_IncSelectedQty (0x6dddc)`,
  `ManufScreen_ReduceSelectedQtyToOne (0x6dec0)`, `ManufScreen_DecSelectedQty (0x6e09c)`,
  `ManufScreen_DecEngineers (0x6e330)` and `ManufScreen_UpdateProgress (0x6df6c)` — 44 more checks.
  Engineers for scientists, workshops for labs, `auManufCol` @ 0x15E19C for `auResearchCol`, and the
  same 17156 address residue in place of 4900. IDB: `unk_15EE44` → `pManufProductPanel`,
  `unk_15EE3C` → `nManufProductRowCache`, `unk_15EE34` → `bManufProductSelected`, `byte_15EE50` →
  `bManufQtyGate`, `unk_15E19C` → `auManufCol`, `unk_15B79C` → `aManufProductB`, and
  `ManufScreen_GetSelectedProductId` renamed — it never returns a product id, it clamps the selected
  quantity to 1. `docs/idb/functions.tsv` and the regenerated `inventory-grouped.tsv` follow it.
  New ledger row: `bManufQtyGate` is a **dead gate** — no code in the binary writes it non-zero, the
  only store anywhere is `ManufScreen_SelectListed` writing 0 at 0x6E970.

- [x] part 4, the selection side: `ManufScreen_SelectListed (0x6e914)` and
  `ResearchScreen_StartSelected (0x73240)` — 27 more checks. The row click clears the panel's own
  `nUnk3032` with a **byte** store (`STRB [R0,#0xBD8]` at 0x6E958) into a dword field, so only the
  low byte goes to zero (0x123456 → 0x123400, asserted). The start button begins a topic only when
  `wProgress == 0` AND `auResearchCol[topic + 144*base] != 0`, and it reaches `wProgress` through
  both anchors (0x15EF08 + 34*topic + 0xA at 0x732C8, 0x15EF0C + 34*topic + 6 at 0x732D8) — the
  second confirmation of the live record layout. It also needs the manufacturing column, because
  `Base_UpdateWorkshopNeeded` takes its budget from `Base_CountActiveManuf`.

- [x] part 5, three strategic/geoscape leaves: `Smap_SumSiteScores (0x96894)`,
  `Base_EnterBuildMode (0x84e20)`, `Smap_SetWaypoint (0x85440)` — 61 more checks. The site score
  reads `aSites[nSiteCount]`, the transfer flow's **working** slot: `Transfer_ResetSite (0x9690c)`
  clears exactly that record, so it is not an off-by-one. Three LDRH chains give
  `auItemQty[77..104]`, `[114]`, `[141..148]` — 37 halfwords, dword accumulator, max 2424795.
  `UiPanel_InsertLine` and `UiPanel_SetLineColors` mask colour slot 0 with `AND R3,R3,#0xFF` and
  byte-load slots 1..3 (`LDRB [SP,#arg_N]`), so **no store through either can exceed 255** even
  though the record holds four dwords; `Smap_SetWaypoint` reloads the ship index with `LDRB` at
  0x85490 and the port casts to match (300 → colour 44, globals keep 300). `unk_186BB4` is a
  scratch `UiControl *` shared by the Options/Manuf/Research/Geoscape screens, not one named
  control — parking it at 1000,1000 is how the original hides a widget it has no slot for.

- [x] part 6, the manufacturing pair: `ManufScreen_ClearProductSlot (0x6f8cc)`,
  `ManufScreen_ConfirmAndClose (0x6e98c)`, `ManufScreen_IncEngineers (0x6e13c)` — 58 more checks.
  Clearing a slot zeroes `idx = 200*base + product` in all four parallel columns (0x159E9C u32,
  0x15B79C u32, 0x15D09C u16, 0x15E19C u16). Products 119..121 then clear the base's byte in the
  0x159DBC table by reading it and subtracting that same amount back, and the byte is found by
  **type** — two bases of one type means the earlier base is the one cleared. In
  `IncEngineers` the two halves write different residues into the same two globals: a full hangar
  writes 1 into `dword_1870B4[0]`, an over-capacity stores block writes 0. And the cargo size that
  gates the stores path comes from `apObjects[PRODUCT]` (0x6E214), not the base. `apBases` @ 0x114B2C
  was a 1-byte global in the IDB; it is `BaseRec *[8]`.

- [x] part 7, six small handlers: `BaseInfoScreen_TogglePanelMode (0x55754)`,
  `Base_RemoveAllShips (0x66874)`, `RadarScreen_SetCursorNW/NE/SW/SE (0x5e768 / 0x5e808 / 0x5e93c /
  0x5ea70)` — 59 more checks. The toggle writes a cursor that is *not* the flag it just set: a flag
  that was zero becomes 1 while the cursor written is 0, because the flags from `ANDS` at 0x55764
  survive to the `MOVEQ` at 0x55784. `Base_RemoveAllShips` re-reads its list entry and its count every
  iteration because `Ship_RemoveWithCargo` decrements the stored indices as it compacts `apCraft`. The
  four radar corners are two pairs differing in three unrelated ways (fixed 560 versus
  `bound + 560*(26-level)/114` with max/min, the -2 intercept guard on NE/SW only, and SE alone
  setting the flip byte), and all four "copy" a string whose source is all zero and referenced nowhere
  else — they clear `szRadarMsg`, they do not write it. Port bug this unit caught and fixed:
  `RadarScaleRange` dereferenced its env before `RadarSetCursor`'s null check.

- [x] part 8, four more small handlers: `Base_HitTestFacilityStrip (0x671B0)`,
  `Base_CountInboundTransfers (0x67F78)`, `BuySellScreen_ConfirmSiteAndClose (0x75090)`,
  `Funding_AddRaceBonus (0x7debc)` — 87 more checks. The base-tab strip is a COLUMN, not a bar: the
  drawer (`Base_DrawFacilityIconStrip`, 0x6250c) steps 28 pixels down the right edge while the hit test
  divides the y by 30, so from the third cell down the top of a cell reports the previous base
  (bugs.tsv). Three gate bytes disable it by overwriting the x argument with 10 and re-entering the
  compare chain mid-way. `Funding_AddRaceBonus` maps race ids to funding slots non-monotonically
  (0x84 → slot 1 while 0x82/0x83 → slots 2/3, and 0x86/0x87/0x89 give nothing). Port bug this unit
  caught and fixed: `Base_CountItemQtyAcrossBases` and `Base_CountOutboundTransferSpace` bounded
  `nSiteIdx` against `nSiteCount`, but five of the ten cargo call sites and both extra outbound call
  sites pass the site COUNT — the transfer flow's working record — so the bound silently dropped the
  whole term at the buy/sell and transfer screens. Both now bound against `PUFO_SMAP_SITE_COUNT`.

- [x] part 9, three openers that also reset the clock: `ResearchScreen_Open (0x616ac)`,
  `Ui_OpenBaseInfo (0x84AF8)`, `Geoscape_PrepScreenOpen (0x85194)` — 42 more checks. All three run the
  same four-step prologue (clear `szRadarMsg`, clear `unk_186420`, promote the research "active" bytes,
  clear `byte_1870C8` and run `GameTime_Set5Sec`) and then tail-call their screen's Init. Two ARM
  details a source reading misses: the walk's count byte at 0x160C4C is re-read only after a store, and
  the loop is a do/while, so a non-zero count always examines entry 0. `ResearchScreen_Open`'s
  `LDRB R3,[R0,R1]!` writeback looks like a running pointer but is not — the back edge recomputes the
  base. `Geoscape_PrepScreenOpen` pauses the clock when the message line's flag is >30 and <50, read
  twice. `0x10DA38`, the strcpy source all six of these openers use, is a block of zeros: it is the
  empty string, so every one of those copies is a clear.

- [x] part 10, the three siblings of that prologue: `Geoscape_PrepScreenOpenSfx (0x852b8)`,
  `Ui_OpenBaseInfoAlt (0x84d18)`, `Ui_OpenResearch (0x84bf8)` — 31 more checks. The Sfx variant is the
  only one of the six that never calls `GameTime_Set5Sec` — 0x8537C falls straight through to the line
  removal, so it leaves the tick as it found it. `Ui_OpenBaseInfoAlt` runs the whole prep and then the
  four prologue steps again, but the line removal and the pause test live only in the prep's tail, so
  they happen once. `Ui_OpenResearch` runs the prologue *without* the tick, then the prep, then the
  tick, then rebuilds the fifteen nav controls and parks `unk_186BB4` at (1000, 1000). `byte_18706C`
  renamed `Ufopaedia_bReselectPending` in the IDB.

- [x] part 11, the intercept launcher and the target dismiss: `Ship_LaunchIntercept (0x5a64c)`,
  `Smap_CancelTarget (0x84254)` — 35 more checks. IDA called 0x5a64c `Ship_AbortCourse`; it aborts
  nothing and all three of its callers are target-selection handlers, so it is renamed
  `Ship_LaunchIntercept` in the IDB and in `renames.tsv`. It is the mirror of `Ship_RetargetTravel`:
  the craft goes OUT to a strateg item instead of HOME to its base, and the branch the "send home"
  version lacks is `bAtBase` — a craft at its base departs from the base, with `nX/nY` snapped there
  too. It is the only caller that sets `bCopyStart`. `Smap_CancelTarget` reads the target from colour
  SLOT 1 of message line 0 while `Smap_SetWaypoint` writes the ship index into slot 0, and its clock
  test is on the root panel's line-list count byte at 0x158890+0xD48 — renamed
  `gUiRootPanel_nLineCount`.

- [x] part 12, the two "keep the course" selection answers: `Smap_ConfirmSelected (0x845d8)`,
  `Smap_ConfirmSelectedAlt (0x853e4)` — 17 more checks. Both are eight instructions: ship index from
  colour slot 0 of message line 0, `Ship_ClearWaypointIntercept`, then the `Smap_ClearSelection` tail.
  The Alt variant differs only in tail order — the selection flag goes before the line-count test —
  which nothing outside the two functions can observe. The dismiss half of the pair,
  `Smap_DismissSelected (0x84428)`, was already ported in `game/pufo_strateg.c` and is not re-ported.

- [x] part 13, `RadarScreen_CyclePalette (0x5ed0c)` — 14 more checks. Two jobs in one function: it
  rotates `aPalette` slot 22 entries 0..24 (0x14CAD0..0x14CB30) up one place with entry 24 wrapping to
  entry 0, and it animates the radar range with four conditional steps (up, down, up, down — an
  overshoot-and-correct, so a value two or more away moves by two and one already at the target does
  not move), plus up to four more upward steps when the SE corner's flip byte is set. `unk_15965C`
  renamed `nRadarRangeTarget` and `unk_15966C` renamed `nRadarRangeCur`. Open question recorded rather
  than asserted: which radar-draw function actually reads slot 22.

- [x] part 14, `Smap_ConfirmCraftTarget (0x842ec)` — 26 more checks. Replace the waypoint a craft is
  chasing with a fresh one at the pending click and launch the craft at it. The `bIntercepting` clear
  sits inside the type-5 branch, so a craft chasing a real contact keeps the flag. The click pair
  0x186414 / 0x186418 is 0x2C / 0x30 past the 36-byte viewport block and outside it, with one xref
  each — recorded as inputs rather than invented as `SmapView` fields. `StrategList_RemoveAt` ignores
  its first register argument, so the `MOV R0, R6` before that call is dead.

- [x] part 15, the wrapped distance and the craft range gate: `Smap_DistWrapped (0x7df80)`,
  `Smap_CraftRangeExhausted (0x7e074)`, `Ship_FindIndexById (0x69ffc)`, `Base_RemoveAllSoldiers
  (0x668e8)` and the merged copy `Base_FindIndexBySiteId (0x5a8d0)` — 56 more checks. The gate was
  `Smap_CanInterceptReach` in the IDB and is renamed: it drains the craft's range budget and
  answers whether its distance from home exceeds what is left, which is what makes
  `Geoscape_TickShips` post message 15. Its five craft-class multipliers are exact reciprocals
  (1/30000, 1/40000, 1/64000, 1/12000, 1/32000), so the port carries them as a divisor table.
  `Geoscape_TickShips`' 0xFF/0xFE home-base filter is dead code and the hook
  `pfnCanInterceptReach` is gone — the gate is a direct call now, as in the original.
  **Open question, resolved in part 20 and part 21:** `g_aSoldierIdList` (was `dword_160C90`) is a
  dword count followed by dword ids, and its only writers are `BuySellScreen_BuildPanels` (0x76de0),
  which zeroes the count, and `BuySellScreen_CommitTransaction` (0x74bf8), which fills it with the
  roster id (`Soldier + 0x1B8`) of every selected soldier row. Both are ported now, so
  `Base_RemoveAllSoldiers` no longer needs the list as a blind input.

- [x] part 16, the panel scroll family and the staged stores commit: `ManufScreen_PanelPrev/Next
  (0x6dd4c / 0x6dd94)`, `PsiScreen_PanelPrev/Next (0x715e8 / 0x71648)`, `BuySellScreen_PanelPrev/
  Next (0x76088 / 0x760e0)` merged with `Transfer_ScrollPrevAll/ScrollNextAll (0x967e4 / 0x9683c)`,
  `BuySellScreen_DetailPanelPrev/Next (0x79a70 / 0x79ac8)`, `BuySell_RoleToItemId (0x76d88)` and
  `BuySellScreen_ApplyQtyChanges (0x78d90)` — 37 more checks. All ten scroll bodies are the same
  straight line over their panel globals, and the buy/sell and transfer pairs are byte-identical
  apart from the BL displacements, so the port keeps one implementation of each (the merge is on
  record in `docs/parity/renames.tsv`). The psi set is four parallel arrays indexed by the signed
  byte `byte_1870F4`, one entry per base. `BuySellScreen_ApplyQtyChanges` is the commit of the
  hangar's staged `anSlotCarryCount` delta: negative adds to the base and subtracts from the craft
  (with the delta re-read as a halfword and negated twice), zero-or-positive is the mirror, and it
  ends in a tail call to `Geoscape_CloseUI`. The two unbounded indexes there and the missing NULL
  test in the scroll family are recorded in `docs/parity/bugs.tsv`.
  **Coverage integrity:** two new mentions of unported functions were first written in the `@`
  form — `Geoscape_CloseUI (0x8b94c)` and `PsiScreen_ApplyAssignments (0x72250)` — and both are
  the paren form now, so neither is credited. `audit_names.py` is back to 0 unresolved.

- [x] part 17, the alien race ids: `BuySell_ItemIdToAlienRaceId (0x6ad30)` — 31 more checks, and
  a wrong IDB name fixed. The function was `BuySell_ItemIdToCategory`; it does not read
  `ObjectRec.nCategory @ 0xB8` (that field is 0 for every slot it maps, 255 for 145..147), it
  answers the alien RACE id of the item in that object slot, or -1. Two independent checks pin
  the ids to races: the shipped OBJECTS.DAT names cut exactly along the function's groups, and
  its only reader `BuySellScreen_CommitTransaction (0x74bf8)` switches the id into
  `apObjects[59..69]` — the eleven alien-corpse records — which is the same pairing
  `Smap_GrantSiteRecovery (0x98b50)` hard-codes per slot. The eleven globals there were
  `unk_113BAC..unk_113BD4` and are now `Map_apObjects59..Map_apObjects69` typed `ObjectRec *`;
  the enum `AlienRaceId` is in the IDB type library, `Map_apObjects56` was re-typed from a
  1-byte global to `ObjectRec *`, and `docs/idb/functions.tsv` / `inventory-grouped.tsv` carry
  the renamed function.

- [x] part 18, the object name field and the name loop: `ObjectRecFull.szName` moved to record
  offset 0 as `char szName[36]`, and `Geoscape_InitUI`'s name loop now writes text record
  `1447 + counter` into `apObjects[counter + 1]` (0x86A64 loads `[R3,#0x10C]` where the price
  loop uses `[R1,#0x108]`), so slot j is named by record 1446+j and slot 0 keeps the name
  OBJECTS.DAT carries at record+0x00 — 3 more checks, and every `ObjectSlot` enum label fixed
  (the values were always right, the names were one slot high). `Deploy_NameFromObject` and the
  tactical HUD item label, which both read the record's first bytes, now read `szName`. IDB
  comments added at 0x86A58 / 0x86A64 and at the eleven `Map_apObjectsN` globals the port uses by
  number, naming the shipped item at each slot.

- [x] part 19, the screen-id scroll dispatcher: `Geoscape_UiFocusPrev (0x8afa0)` and
  `Geoscape_UiFocusNext (0x8b1f4)`, two 596-byte mirrors that switch on `nGeoscapeScreen` and run
  the mover over that screen's panel set — 43 more checks. The screen 6 case keeps its two gates
  (`bPlaneStoresCommitted` for the five store panels, `bPlaneListPanelsBuilt` for the crew pair),
  and the 0/2 case keeps its quirk: Prev writes `unk_1864F4`, Next writes `unk_1864F6`, and both
  hand back the address of `unk_1864F4`. IDB: function comments at 0x8afa0 / 0x8b1f4, and the ten
  panel globals the dispatcher loads (`unk_15EE38/40/48`, `unk_15EEFC/EF00`, `unk_160C88`,
  `unk_1615F8`, `unk_161A00/1A1C/1A20`) typed `UiPanel *` and named `pManufPanel*`,
  `pResearchPanel*`, `pBuySellPanel*`.

- [x] part 20, the buy/sell list panels: `BuySellScreen_BuildPanels (0x76de0)`, 4236 bytes — 33
  more checks. This is the function that fills the five panel globals part 19 mapped, and it is
  what the deferred `BuySellScreen_CommitTransaction (0x74bf8)` was waiting on: panel A's
  `anLineAux` is the row-kind marker (1000 soldier, 117..121 craft, slot id object, slot + 2000
  alien), panel D's is the available count and panel E's is the pending quantity. It also settles
  the craft naming — `TEXTS[415 + Ship.bCraftType] + "-" + Ship.bType`, records 415..419 being the
  five craft names in both shipped archives — and the sort table `g_aBuySellRowOrder @ 0xAAA04`,
  whose 34 alien entries never match because alien rows carry slot + 2000. IDB: `unk_161A28` →
  `bBuySellPanelsBuilt`, `unk_161A0C` → `g_aBuySellCtl`, `unk_AAA04` → `g_aBuySellRowOrder`, the
  enum `PufoBuySellRowMarker`, and a function comment at 0x76de0.

- [x] part 21, the buy/sell commit: `BuySellScreen_CommitTransaction (0x74bf8)` — 17 more checks.
  Six phases: apply `g_anBuySellSlotDelta` per slot and move the race corpse by the opposite delta;
  collect the selected craft rows into the assigned-ship list and unload each one's
  `aItemQty[craftIndex]` column into the base; collect the selected soldier rows into
  `g_aSoldierIdList`; give each sold soldier's armour (`apObjects[121 + Soldier.nUnitType]`) back to
  the base; take the funds snapshot and close the screen. IDB: `unk_161600` →
  `g_anBuySellSlotDelta`, comments at 0x74bf8 and at `BuySellScreen_Init` (0x78c30) recording that
  screen 11 wires `BuySellScreen_ConfirmSiteAndClose` + `BuildControls` while screen 12 wires
  `BuySellScreen_CommitTransaction` + `BuildPanels`.

- [x] part 22, the buy/sell row editors: `BuySellScreen_FillSellPanel (0x75464)`, 1044 bytes, and
  `BuySellScreen_FillBuyPanel (0x75160)`, 772 bytes — 54 more checks. These are the "<" and ">"
  clicks BuildPanels wires onto panels B and C, and they are what makes the row-marker encoding
  usable: panel E's aux is a selection FLAG (0x755F4 writes `#1`, it does not add) on soldier and
  craft rows, a marked-unit count on alien rows, and the pending money on plain object rows. Two
  original defects came out of them and both are in `docs/parity/bugs.tsv`: the plain-object repeat
  loop is bounded by `nRadarPulseTimer / 10` (0xAC224), the radar screen's pulse countdown, so one
  click moves between 1 and 26 units depending on a timer from another screen; and the two loops are
  not symmetric — the sell guard breaks out, the buy guard only skips the body, and the buy side's
  `panel E aux -= price` has no floor. IDB: `unk_161A2C` → `BuySell_nTransTotal`, comments at
  0x75464 and 0x75160.

- [x] part 23, the buy screen's quantity controls: `BuySellScreen_ValidateCapacity (0x75b88)`, 1280
  bytes, and `BuySellScreen_UpdatePrices (0x75878)`, 784 bytes — 61 more checks. These are the ">"
  and "<" click handlers `BuySellScreen_BuildControls` installs onto panels C and B, and they work on
  the transfer flow's working site `aSmapSites[nSiteCount]` rather than on the base: buying checks
  `Base_CalcHangarCap` (117/118), `Base_CalcStoresCapacity` (every other product) or
  `Base_CalcLivingQuartersCap` (1000/112/113) and reports a failure as a reason code in
  `dword_1870B4` — 0 living quarters, 1 funds, 2 hangar, 3 stores — while selling has no capacity
  test at all. Crew rows cost a fixed 50000 / 60000 and return out of the function, so one click
  queues at most one engineer or scientist. `unk_159D90` turned out to be a counter the buy/sell
  screen owns (Init clears it, buying increments it, selling decrements it, saved with the bases) and
  is now `nSoldierHireCount`. The row-marker magic numbers across the buy/sell family are now the
  `PufoBuySellRowMarker` enum, in the port and in the IDB.

- [x] part 24, the buy/sell screen bring-up: `BuySellScreen_Init (0x78c30)`, 352 bytes — 25 more
  checks. The tail call of both openers: the funds snapshot, the `buy_sell` pad and `buy_sell.pal`
  palette at slot 5, the transaction total and `nSoldierHireCount` cleared, the close control at
  224,1 with the close thunk, the confirm control centred at `120 - (uWidth >> 1)`, 297, the whole
  slot-delta array cleared over `Map_nObjectCount`, and then by screen id 11 →
  `ConfirmSiteAndClose` + `BuildControls`, 12 → `CommitTransaction` + `BuildPanels`. Only
  `BuySellScreen_BuildControls (0x76160)` is left before the buy/sell screen is complete.

- [x] part 25, the buy screen's object block: `BuySellScreen_BuildControls (0x76160)`, 3112 bytes — 51
  more checks, and the buy/sell screen is complete. The buy mode's counterpart of `BuildPanels`: the
  same five panel globals, controls, `stru_ABBA0` descriptors, `g_aBuySellRowOrder` sort and funds
  rollback, but the shop's catalogue rows (slots 1..58, 112, 113, 117, 118 with a price and a zero
  `ObjectRecFull+0xBA`), `nStyle` 14, x 10/174/180/196/232, panel E carrying the price, and the
  soldier row (aux 1000, price 40000, label TEXTS[159]) written inside the slot-113 branch. Its
  teardown resets the transfer flow's working site `aSmapSites[nSiteCount]` — everything except
  `bUnk518`, which only the build branch clears. IDB: `unk_161A24` → `bBuySellControlsBuilt`,
  `unk_161A08` → `pBuySellScreenPad`, `unk_ABC20`/`unk_ABC24` → `aRowQtyInc`/`aRowQtyDec`, a comment
  at 0x76318 naming the only read of `ObjectRecFull+0xBA` in the binary, and the shipped-data counts
  corrected (35 records with a non-zero +0xBA, all unpriced; 36 buyable slots).

- [x] part 26, the transfer screen's bring-up: `Ui_InitTransfer (0x9955c)`, 392 bytes, and
  `Transfer_ResetSite (0x9690c)`, 420 bytes — 55 more checks. The pad `"transfer"` at palette slot
  13, the four controls in the root array at `0x1870D8` (scroll pair, reset, commit), the confirm
  button closing the screen instead of committing and sitting at `28 * nBaseCount + 10`, and the
  reset's blocked-transfer gate — cargo on the working site, the two unread scratch bytes, or a site
  that still owes recovery — writing the fifth shortfall reason, 5. Its site clear takes `bHasLoot`
  and leaves `bFlag0`/`bFlag1`, the exact inverse of the buy screen's teardown. `Transfer_RoleToIconId
  (0x96ad8)` is `BuySell_RoleToItemId` byte for byte and is merged. IDB: `stru_AC3B8` →
  `g_aTransferCtlDesc`, comments at 0x9955c / 0x9690c / 0x96ad8 / 0x1870B4 / 0x1870D8 / 0x1870F8 /
  0x1870D0 / 0x1870F0.

- [x] part 27, the transfer screen's row list: `TransferScreen_BuildPanels (0x96b30)`, 3956 bytes, and
  `Transfer_MoveOneItem (0x9602c)`, 604 bytes — 47 more checks. The transfer mode's counterpart of
  `BuySellScreen_BuildPanels`: same five panels and the same sort, but the slot filter is the sell
  mode's stocked and alien sets merged minus 142, the B/C/D columns sit eight pixels further right,
  panel E holds no price, and panel A aux has no `+2000` alien offset so an alien row sorts by its own
  slot. `Transfer_MoveOneItem` is the fifth handler on the timer-bound repeat loop, and a craft row
  moves at most one unit per click. New ledger entry: the craft count pass at 0x96C94 omits the
  `bAtBase` test the fill pass at 0x97550 applies, so an away craft adds a row that is never written.
  IDB: `unk_1870EC` → `bTransferPanelsBuilt`, comments at 0x96b30, 0x9602c, 0x96C94 and 0x97550.

- [x] part 28, the transfer screen's capacity gate: `TransferScreen_ValidateCapacity (0x96288)`, 1372
  bytes — 43 more checks. The mirror of `BuySellScreen_ValidateCapacity`: it moves units out of the
  base onto the working site and spends no money. Slots 1000 / 112 / 113 never enter the timer-bound
  repeat loop (0x96358) — the living-quarters gate runs once, and a soldier row queues exactly one per
  click. Alien slots 77..104 / 114 add the containment gate (reason 4, `byte_1870C8` written
  unconditionally). The capacity gates measure `byte_1870F4` (the transfer destination) while the
  availability test reads `auPerBaseVal` at `nSelectedBaseIdx` — items leave the selected base and are
  checked against the destination. IDB: comment at 0x96288, local enum `PufoShortfallReason` declared
  for the reason codes `dword_1870B4[0]` holds.

- [x] part 29, the transfer screen's commit: `TransferScreen_CommitTransfer (0x97aa4)`, 1280 bytes —
  31 more checks. Queued rows (panel E aux exactly 1) write their roster / craft id into the working
  site's `anSoldierIds` / `anCraftIds` and unhome the unit: `Soldier.nKind` (0x0B6) = 255, or the
  hangar cell cleared and `Ship.bHomeBase` = 255. The commit only runs when the transfer carries
  something; it stamps the completed record with `bFlag0 = 24`, subtracts the pending quantities from
  the selected base, raises `nSmapSiteCount`, reports stock above 60000 through `Transfer_WriteErrorDump`
  (0x996e4, not ported -- a debug dump writer, not a UI display) and clears the fresh working record. IDB: comments at 0x97aa4 and 0x186410,
  `nSmapParam4` → `nSmapSiteCount`.

- [x] part 30, the transfer screen's frame: `TransferScreen_Draw (0x97fa4)`, 2988 bytes — 47 more
  checks. The ramped border (24 steps of 5 on the top and
  bottom rails, 24 steps of 7 on the sides, the ARM's `RSB #0xE6` / `RSB #0xFA` falling halves), the
  four separators at x 130 / 168 / 183 / 198 from y 69, the five panels through `UiPanelMsg_Draw`,
  the labels from TEXTS 1264..1269 and 159, the shortfall panel for reasons 0..5 (TEXTS 405..408,
  1443, 1444), and the destination overlay: full-screen blend, one bevelled row per base, the tap
  through `Base_HitTestFacilityStrip` writing `byte_1870F4` and the working site's `bFlag1`,
  "Stop Transfer", "Select destination base" with its red channel at `250 - 40 * nMapPhase`.
  IDB: comment at 0x97fa4, `aTextRec1264..aTextRec1269` and `aTextRec159` named.
  A first attempt to eyeball this screen by dumping the harness surface as a BMP was tried and
  dropped: it showed only the frame primitives the function itself paints, with synthetic panels and
  no text, so it verified the transcription and nothing else. Visual verification belongs to the app's
  own `--dump-frame` path, which renders the real screen from real data — and that path is blocked
  until `Geoscape_Draw (0x8c180)` is ported, because the port has no geoscape screen at all. See
  "Separate issues".

- [x] part 31, the base-tab strip drawer: `Base_DrawFacilityIconStrip (0x6250c)`, 792 bytes — 33 more
  checks. The strip `Base_HitTestFacilityStrip` hit-tests, now drawn: the 26x26 erase rect over the
  selected cell, one 6x6 block per base 28 pixels apart at x 212 + 4*column and y 28*base + 4 + 4*row,
  reading `buildDays` for the status colour (0x96 in the red field while a cell is under construction,
  in the green field once it is finished) and `fac[base].tileA` for the grey "no tile" pair, which is
  drawn last and wins; plus the 23-step chevron that marks one base row. Porting it exposed a dead
  branch: `TransferScreen_Draw`'s destination-overlay close at 0x989D0 can never fire, because the
  draw returns at 0x98224 unless `bTransferPanelsBuilt` is set and `Base_HitTestFacilityStrip`
  disables the strip on that same byte, forcing a miss that returns `nSelectedBaseIdx` — the very
  value being compared. The overlay is never closed by this code and `byte_1870F4` is pinned to the
  source base on every overlay frame (bugs.tsv, 39 markers now). The transfer draw's base index is
  renamed `nSourceBaseIdx` → `nSelectedBaseIdx`: the original receives it as a second argument that
  `Geoscape_Draw` loads straight out of `abFacilityGrid + 0x619` (0x93EA8), and it is the same byte
  the hit test reads back. IDB: comments at 0x6250c and 0x989D0.

- [x] part 32, the craft re-arm pass: `Base_DistributeRecoveredItems (0x68f2c)`, 1216 bytes — 26 more
  checks. Every craft sitting at the base being processed has its two weapon mounts
  (`Ship.bWeaponL` / `bWeaponR` @ 0x22 / 0x23, which hold weapon object slots) re-supplied from that
  base: the craft's own quantity byte at 0x3d..0x41 is deposited, cleared, and the craft takes back
  `perMount * load`, where `load` is 2 when both mounts hold the same weapon and 1 otherwise. Five
  mounts have a rule — Craft cannon → rounds ×10, Stingray launcher → rocket ×3, Avalanche launcher →
  rocket ×2, Plasma cannon → Elerium-115 ×10, Fusion ball launcher → Fusion ball ×2 — and the two
  names that were unnamed in the port are now pinned from main.dat TEXTS.DAT (`apObjects[52]` "Craft
  cannon rounds[x50]", 53 "Stingray rocket", 54 "Avalanche rocket", 55 "Fusion ball"). Laser cannon
  (28) and Psi-arm (24) fall through. Elerium's byte is in tenths: the base gets `bAmmo29 / 10` and
  the byte written back is `10 *` what the craft carries. When both mounts hold the same weapon the
  second pass hands the byte the first pass just refilled back to the base as well — reproduced.
  IDB: comment at 0x68f2c.

- [x] part 33, the mission-recovery pass: `Smap_GrantSiteRecovery (0x98b50)`, 2572 bytes — 57 more
  checks. The hourly tick's delivery (`Geoscape_TickHourly @ 0x5d1bc`): age every site countdown,
  deliver the sites that reach zero, drop them. The delivery is the whole mission payoff — cargo into
  the base's stores behind the `Base_CalcStoresCapacity` gate (message 42, or 41 when the site has no
  loot at all), alien cargo behind a containment gate whose counter is **function-wide and counts
  non-zero slots rather than quantity** (0x98B70, so a later site sees what earlier sites already put
  into containment), and an over-cap alien is *moved* to its corpse slot with the source zeroed — the
  17-arm remap at 0x98DBC..0x98F98, now named end to end (Sectoid soldier 77 → Sectoid corpse 59, the
  medics 144..148 onto their own race's corpse, 114 Ethereal navigator → 63). Then recovered soldiers
  through `Unit_CreateAndClassify`, recovered craft re-homed with a hangar scan, new craft built from
  slots 117..121 (the five slots carry the shipped craft names and the type map {0,3,4,1,2} lands each
  name on its own `CraftType`), roster soldiers re-assigned by roster id, and the three needed-flag
  passes. Two scan quirks found against `Ship_SpawnAtSlot`'s own: here the layer2 test follows the row
  (`fac + 79 + row + col`, not `79 + col`) and a craft type outside 0..4 *continues* the scan instead
  of returning unstamped. And the compaction's shift copies one record more than the live tail — that
  extra record (the unused slot after the last live site) is what ends the loop. 26 object slots named
  from main.dat TEXTS.DAT (the eight corpses 59..66, the race-block bounds 77/81/86/87/91/94/98, the
  craft-count slots 117..121, the medics 144..148). IDB: comment at 0x98b50.

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

- [x] **The object name field is at record+0x00, not +0x04, and `Geoscape_InitUI`'s name loop is
  one slot short — fixed in part 18.** Found while verifying the alien race ids (part 17); it also explains the older
  "names read short" observation. Three pieces of evidence. (1) The shipped OBJECTS.DAT records
  carry a readable name at record+0x00 — slot 59 is "Sectoid corpse", slot 77 "Sectoid soldier" —
  and the IDB's `ObjectRec` has `char gap_0[36]` exactly there. (2) `Geoscape_InitUI @ 0x86A64`
  loads `LDR R0,[R3,#0x10C]`, i.e. `apObjects[i+1]` and not `apObjects[i]` (the price loop four
  instructions earlier uses `#0x108`), and `STRB R2,[R0]` stores at offset 0 of the record.
  (3) Matching against main.dat, the text record that names object slot j is 1446+j, not 1447+j.
  The port has `gap_0[4] + szName[32]` and copies record 1447+i into slot i, so every object name
  is one slot off and four bytes late — reading a name straight after `Boot_LoadStartup` gives
  "oid soldier" for "Sectoid soldier". The fix touches the `ObjectRecFull` layout, the
  `Geoscape_InitUI` loop, the `ObjectSlot` enum names (shifted by one from slot 67 up) and the
  tests that assert them, so it is its own step.
  **Fixed in part 18.** `szName` is now `char szName[36]` at offset 0, the name loop writes
  record `1447 + counter` into `apObjects[counter + 1]`, and every `ObjectSlot` name carries the
  item that slot really holds (slot 105 is Elerium-115, not alien alloys; slot 43 is the [AP]
  clip, not [HE]; slot 68 is the Silacoid corpse, not Sectopod remains; and so on). The values
  are untouched — they came from the IDB's own `Map_apObjectsN` — only the labels were one slot
  high. `test_geoiinit` now checks the whole mapping: all 148 slots 1..148 carry text record
  1446+slot, read back out of the loaded table, and slot 0 is never written. The names are the
  shipped Russian set from lang.dat TEXTS.DAT (slot 59 "Труп Сектоида", slot 77 "Сектоид солдат",
  slot 117 "Рэйнджер"), which is the table the original actually reads.
- [ ] `pufo_game` and `pufo_ui` depend on each other. `pufo_strateg.c` (in `pufo_game`) calls
  `UiLineList_Remove` (in `pufo_ui`), and `pufo_ui` links `pufo_game`. Test targets that link
  `pufo_game` without `pufo_ui` only link because nothing pulls `pufo_strateg.obj` out of the
  archive; one new shared symbol pulled it in and 19 targets failed to link while ctest still
  reported 48/48. Until this is fixed, share a value across modules as a header define, not an
  `extern` symbol.
- [ ] **Four caption buffers have no writer anywhere in the binary.** `PsiScreen_Draw` copies three
  strings and draws them — `unk_EF024` at (7, 54), `unk_EF0F1` centred at (60, 300) and
  `szBaseAtDone @ 0xEBF4B` centred at (180, 300) — and `unk_EF1BE` for its shortfall panel. All four
  are `.data` / BSS scratch whose only cross-references in the whole binary are these reads; the same
  four screens repeat the pattern (`BaseAtScreen_Draw 0x58728`, `ManufScreen_DrawFrame 0x6f9a4`,
  `ResearchScreen_Draw 0x73a34`, `BuySellScreen_Draw 0x77e6c`). As shipped they are empty strings, so
  those caption lines draw nothing. Until the writer is found — a table-driven copy from lang.dat is
  the likeliest candidate — the port cannot say what those captions are supposed to say, and the
  screen draws that read them stay unported. Recorded rather than guessed.
- [ ] **Screen-level visual verification is blocked on `Geoscape_Draw (0x8c180)`.** The app renders
  the titles and the main menu for real (`--dump-frame` proves it), but the geoscape screen — the
  screen every other screen sits on — is a hook the port does not implement
  (`RenderFrameEnv.pfnGeoscapeDraw`, `pufo_renderframe.h:66`). So the only way to look at a screen
  like the transfer screen today is a unit-test harness that paints that one function's own
  primitives onto a blank surface with synthetic panels. That picture is a transcription check, not
  the game UI, and it was dropped from `test_baseui` for exactly that reason. Real visual checks
  need the geoscape frame in the app: `Geoscape_Draw` is 33744 bytes, a jump-table dispatcher over
  screen ids, and it is the largest single uncovered function in the binary (5.6% of it). Its
  branches call the screens still queued below, so the screens come first and the dispatcher after
  them; the shared frame block (`0x8C214..0x8C760`) is the part that would make the main screen
  visible and is the natural first slice.
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
- [ ] The coverage metric counts **every** `@ 0xADDR` citation in `src/pufo`, so a comment that cites
  an unported callee inflates the headline number. `audit_names.py` reports the live list: 5 credited
  addresses whose original name is not defined (24480 bytes, 4.1% of the binary) — `Geoscape_OnPointer
  @ 0x872d0`, `Geoscape_OnPointerUp @ 0x89450`, `Ui_TextInputDialog @ 0x632cc`,
  `Geoscape_CloseUI @ 0x8b94c` and `SoldierScreen_Draw @ 0x65290`. New code writes unported callees
  as `Name (0xADDR)` so they stay out of the count, and `audit_credits.py` now reports zero negated
  citations. The
  `pufo_uibank.c` handler-name table is the same class of credit: it quotes the original bank entry's
  name and address as data, which counts even when only the name string is recreated.

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
| the geoscape map click + the build-dialog init (`pufo_baseui.c`, shell-wired) | 56/56 (Release) | 8029 | 727/1103 (65.9%) | 326700/598792 (54.6%) | 38 checked, 0 mismatched |
| the build dialog itself (`BaseFacScreen_Toggle`, `BaseFacScreen_TryBuySelected`) | 56/56 (Release) | 8084 | 728/1103 (66.0%) | 327760/598792 (54.7%) | 38 checked, 0 mismatched |
| the soldier screen (`SoldierScreen_Toggle`, `SoldierScreen_SelectListed`), shell-wired | 56/56 (Release) | 8137 | 728/1103 (66.0%) | 327760/598792 (54.7%) | 38 checked, 0 mismatched |
| the soldier equip rows (`SoldierScreen_EquipSelected`) + the two false citations removed | 56/56 (Release) | 8165 | 727/1103 (65.9%) | 322856/598792 (53.9%) | 38 checked, 0 mismatched |
| the placement preview and its commit (`BaseFacScreen_DrawPlacementOverlay`) | 56/56 (Release) | 8200 | 728/1103 (66.0%) | 326252/598792 (54.5%) | 38 checked, 0 mismatched |
| the animated panel frame (`Ui_AnimExpandBlit`, `Ui_AnimExpandBlit_IfEnabled`) | 56/56 (Release) | 8219 | 730/1103 (66.2%) | 327288/598792 (54.7%) | 38 checked, 0 mismatched |
| the build dialog frame (`BaseFacScreen_DrawBuildDialog`) | 56/56 (Release) | 8242 | 731/1103 (66.3%) | 328880/598792 (54.9%) | 38 checked, 0 mismatched |
| the two craft-name formatters (`Soldier_FormatDisplayName`, `Ship_FormatDisplayName`) | 56/56 (Release) | 8260 | 733/1103 (66.5%) | 329688/598792 (55.1%) | 38 checked, 0 mismatched |
| radar intercept roll + overlay blit (`RadarScreen_RollWeaponHit`, `RadarScreen_BlitOverlay`) | 56/56 (Release) | 8281 | 736/1103 (66.7%) | 330076/598792 (55.1%) | 38 checked, 0 mismatched |
| options screen audio tab (17 functions: Init, LayoutButtons, Bind*Slots, RefreshVolumeHighlight, 12 radio handlers) | 56/56 (Release) | 8329 | 753/1103 (68.3%) | 332812/598792 (55.6%) | 38 checked, 0 mismatched |
| options screen save/load slots (14 handlers) | 56/56 (Release) | 8344 | 767/1103 (69.5%) | 334072/598792 (55.8%) | 38 checked, 0 mismatched |
| loadout stat panels + rearm + close (`BasePlaneScreen_DrawStatLabelA/B`, `BasePlane_RearmFromStores`, `BasePlaneScreen_Close`) | 56/56 (Release) | 8388 | 772/1103 (70.0%) | 336940/598792 (56.3%) | 38 checked, 0 mismatched |
| hangar stores take/undo (`BasePlaneScreen_CommitStores`, `BasePlaneScreen_AdjustSelectedQty`) | 56/56 (Release) | 8454 | 773/1103 (70.1%) | 337584/598792 (56.4%) | 38 checked, 0 mismatched |
| crew list click + weapon-slot count (`BasePlaneScreen_FillCrewPanel`, `Craft_CountWeaponSlotsCarried`) | 56/56 (Release) | 8478 | 775/1103 (70.3%) | 338296/598792 (56.5%) | 38 checked, 0 mismatched |
| crew list panel builder (`BasePlaneScreen_BuildListPanels`) | 56/56 (Release) | 8521 | 776/1103 (70.4%) | 339768/598792 (56.7%) | 38 checked, 0 mismatched |
| five false citations removed (`Geoscape_OnPointer/OnPointerUp`, `Ui_TextInputDialog`, `Geoscape_CloseUI`, `SoldierScreen_Draw`) | 56/56 (Release) | 8521 | 771/1103 (69.9%) | 315288/598792 (52.7%) | 38 checked, 0 mismatched |
| soldier screen frame (`SoldierScreen_Draw`) | 56/56 (Release) | 8566 | 772/1103 (70.0%) | 316004/598792 (52.8%) | 38 checked, 0 mismatched |
| hangar craft list + its teardown (`BasePlaneScreen_BuildPanels`, `BasePlaneScreen_SelectCraft`, `BasePlaneScreen_SelectListed`) | 56/56 (Release) | 8624 | 775/1103 (70.3%) | 317968/598792 (53.1%) | 38 checked, 0 mismatched |
| hangar object block (`BasePlaneScreen_Init`) | 56/56 (Release) | 8660 | 776/1103 (70.4%) | 319040/598792 (53.3%) | 38 checked, 0 mismatched |
| research quantity buttons + crew getters (`ResearchScreen_IncSelectedQty`, `ResearchScreen_DecSelectedQty`, `Base_GetScientistCount`, `Base_GetEngineerCount`) | 56/56 (Release) | 8702 | 780/1103 (70.7%) | 319608/598792 (53.4%) | 38 checked, 0 mismatched |
| research allocate / cancel / hourly accumulate (`ResearchScreen_UpdateProgress`, `ResearchScreen_CancelSelectedAndClose`, `ResearchScreen_AccumulateAllocated`) | 56/56 (Release) | 8736 | 783/1103 (71.0%) | 320156/598792 (53.5%) | 38 checked, 0 mismatched |
| manufacturing quantity buttons (`ManufScreen_IncSelectedQty`, `DecSelectedQty`, `ReduceSelectedQtyToOne`, `DecEngineers`, `UpdateProgress`) | 56/56 (Release) | 8780 | 788/1103 (71.4%) | 321184/598792 (53.6%) | 38 checked, 0 mismatched |
| the two row clicks + the research start button (`ManufScreen_SelectListed`, `ResearchScreen_StartSelected`) | 56/56 (Release) | 8807 | 790/1103 (71.6%) | 321536/598792 (53.7%) | 38 checked, 0 mismatched |
| three strategic/geoscape leaves (`Smap_SumSiteScores`, `Base_EnterBuildMode`, `Smap_SetWaypoint`) | 56/56 (Release) | 8868 | 793/1103 (71.9%) | 321912/598792 (53.8%) | 38 checked, 0 mismatched |
| the product-slot clear and the engineer "+" button (`ManufScreen_ClearProductSlot`, `ConfirmAndClose`, `IncEngineers`) | 56/56 (Release) | 8926 | 796/1103 (72.2%) | 322720/598792 (53.9%) | 38 checked, 0 mismatched |
| the base-info toggle, the assigned-ship sweep, the four radar corners (`BaseInfoScreen_TogglePanelMode`, `Base_RemoveAllShips`, `RadarScreen_SetCursorNW/NE/SW/SE`) | 56/56 (Release) | 8985 | 802/1103 (72.7%) | 323876/598792 (54.1%) | 38 checked, 0 mismatched |
| the base-tab strip, the inbound craft count, the buy/sell confirm, the race funding bonus (`Base_HitTestFacilityStrip`, `Base_CountInboundTransfers`, `BuySellScreen_ConfirmSiteAndClose`, `Funding_AddRaceBonus`) | 56/56 (Release) | 9072 | 806/1103 (73.1%) | 324676/598792 (54.2%) | 38 checked, 0 mismatched |
| three openers that also reset the clock (`ResearchScreen_Open`, `Ui_OpenBaseInfo`, `Geoscape_PrepScreenOpen`) | 56/56 (Release) | 9114 | 809/1103 (73.3%) | 325456/598792 (54.4%) | 38 checked, 0 mismatched |
| the three siblings of the clock-resetting prologue (`Geoscape_PrepScreenOpenSfx`, `Ui_OpenBaseInfoAlt`, `Ui_OpenResearch`) | 56/56 (Release) | 9145 | 812/1103 (73.6%) | 326308/598792 (54.5%) | 38 checked, 0 mismatched |
| the intercept launcher and the target dismiss (`Ship_LaunchIntercept`, `Smap_CancelTarget`) | 56/56 (Release) | 9180 | 814/1103 (73.8%) | 326944/598792 (54.6%) | 38 checked, 0 mismatched |
| the two "keep the course" selection answers (`Smap_ConfirmSelected`, `Smap_ConfirmSelectedAlt`) | 56/56 (Release) | 9197 | 816/1103 (74.0%) | 327128/598792 (54.6%) | 38 checked, 0 mismatched |
| the radar palette cycle and the range animation (`RadarScreen_CyclePalette`) | 56/56 (Release) | 9211 | 817/1103 (74.1%) | 327360/598792 (54.7%) | 38 checked, 0 mismatched |
| the map-target confirm (`Smap_ConfirmCraftTarget`) | 56/56 (Release) | 9237 | 818/1103 (74.2%) | 327676/598792 (54.7%) | 38 checked, 0 mismatched |
| the wrapped distance and the craft range gate (`Smap_DistWrapped`, `Smap_CraftRangeExhausted`, `Ship_FindIndexById`, `Base_RemoveAllSoldiers`) | 56/56 (Release) | 9293 | 823/1103 (74.6%) | 328948/598792 (54.9%) | 38 checked, 0 mismatched |
| the panel scroll family, the role map and the stores commit (10 scroll handlers, `BuySell_RoleToItemId`, `BuySellScreen_ApplyQtyChanges`) | 56/56 (Release) | 9330 | 834/1103 (75.6%) | 330036/598792 (55.1%) | 38 checked, 0 mismatched |
| the alien race ids (`BuySell_ItemIdToAlienRaceId`, IDB name fixed) | 56/56 (Release) | 9361 | 835/1103 (75.7%) | 330328/598792 (55.2%) | 38 checked, 0 mismatched |
| the object name field and the name loop (`ObjectRecFull.szName` at +0, `ObjectSlot` labels fixed) | 56/56 (Release) | 9364 | 835/1103 (75.7%) | 330328/598792 (55.2%) | 38 checked, 0 mismatched |
| the screen-id scroll dispatcher (`Geoscape_UiFocusPrev`, `Geoscape_UiFocusNext`) | 56/56 (Release) | 9407 | 837/1103 (75.9%) | 331520/598792 (55.4%) | 38 checked, 0 mismatched |
| the buy/sell list panels (`BuySellScreen_BuildPanels`, the row-marker encoding, the sort table) | 56/56 (Release) | 9440 | 838/1103 (76.0%) | 335756/598792 (56.1%) | 38 checked, 0 mismatched |
| the buy/sell commit (`BuySellScreen_CommitTransaction`, the delta array, the corpse pairing) | 56/56 (Release) | 9457 | 839/1103 (76.1%) | 336932/598792 (56.3%) | 38 checked, 0 mismatched |
| the buy/sell row editors (`BuySellScreen_FillSellPanel`, `BuySellScreen_FillBuyPanel`, the timer-bound repeat loop) | 56/56 (Release) | 9511 | 841/1103 (76.2%) | 338748/598792 (56.6%) | 38 checked, 0 mismatched |
| the buy screen's quantity controls (`BuySellScreen_ValidateCapacity`, `BuySellScreen_UpdatePrices`, the shortfall codes) | 56/56 (Release) | 9572 | 843/1103 (76.4%) | 340812/598792 (56.9%) | 38 checked, 0 mismatched |
| the buy/sell screen bring-up (`BuySellScreen_Init`, the pad and palette load, the two confirm wirings) | 56/56 (Release) | 9597 | 844/1103 (76.5%) | 341164/598792 (57.0%) | 38 checked, 0 mismatched |
| the buy screen's object block (`BuySellScreen_BuildControls`, the catalogue include tests, the soldier row) | 56/56 (Release) | 9648 | 845/1103 (76.6%) | 344276/598792 (57.5%) | 38 checked, 0 mismatched |
| the transfer screen's bring-up (`Ui_InitTransfer`, `Transfer_ResetSite`, the blocked-transfer gate) | 56/56 (Release) | 9703 | 848/1103 (76.9%) | 345176/598792 (57.6%) | 38 checked, 0 mismatched |
| the transfer screen's row list (`TransferScreen_BuildPanels`, `Transfer_MoveOneItem`, the count/fill mismatch) | 56/56 (Release) | 9750 | 850/1103 (77.1%) | 349736/598792 (58.4%) | 38 checked, 0 mismatched |
| the transfer screen's capacity gate (`TransferScreen_ValidateCapacity`, the containment reason code) | 56/56 (Release) | 9793 | 851/1103 (77.2%) | 351108/598792 (58.6%) | 38 checked, 0 mismatched |
| the transfer screen's commit (`TransferScreen_CommitTransfer`, the completed-record flag 24) | 56/56 (Release) | 9824 | 852/1103 (77.2%) | 352388/598792 (58.8%) | 38 checked, 0 mismatched |
| the transfer screen's frame (`TransferScreen_Draw`, the destination overlay) | 56/56 (Release) | 9871 | 853/1103 (77.3%) | 355376/598792 (59.3%) | 38 checked, 0 mismatched |
| the base-tab strip drawer (`Base_DrawFacilityIconStrip`, the dead overlay-close branch found) | 56/56 (Release) | 9904 | 854/1103 (77.4%) | 356168/598792 (59.5%) | 38 checked, 0 mismatched |
| the craft re-arm pass (`Base_DistributeRecoveredItems`, the five weapon mounts named) | 56/56 (Release) | 9930 | 855/1103 (77.5%) | 357384/598792 (59.7%) | 38 checked, 0 mismatched |
| the mission-recovery pass (`Smap_GrantSiteRecovery`, the function-wide containment counter, the corpse remap) | 56/56 (Release) | 9987 | 856/1103 (77.6%) | 359956/598792 (60.1%) | 38 checked, 0 mismatched |

Release is the documented configuration and it is green. In **Debug**, `test_titles` and `test_save`
fail and `test_template` is flaky; all three already fail at `04f1155` (verified by building that
commit in a clean worktree with the same ClangCL toolset), so none came from this step. See "Separate
issues".
