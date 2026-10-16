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
- Image map: `IDA_VA - 0x13000 == file offset`. Verify table bytes from the image, never by eye. Long
  byte runs go through a tool end to end — read them with a script and write them with a script. Copying
  an IDB byte dump through your own context is where part 40's wrong table came from.

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
    fixed 200-wide morale bar with `2*(99-morale)` remainder, and the `bPsiActive`-gated extra rows.
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

- [x] part 34, the daily build-timer countdown: `Base_TickFacilityConstruction (0x62824)`, 396 bytes
  — 29 more checks. `Geoscape_TickBases`' first act: every non-zero `buildDays` byte of every base
  counts down, and a byte reaching zero finishes the build — the six needed-flag passes for that
  base, the shortfall dword, message 38. Two things the decompiler had wrong. (1) A finished
  **hangar** clears three *more* days bytes — below, below-right and right (0x628C4 / 0x628CC /
  0x628D4) — which are the days block's own pointer arithmetic (`R8[j+7]`, `R8[j+1]`), not a
  facility layer, and at row 5 they land in the *next* base's days block. (2) The message payload:
  Hex-Rays shows `(38, base, row, col, 0)`, the ARM at 0x6292C..0x62948 computes the row and never
  stores it, and really passes `(38, base, col, 0, 6*base)` — the port passes what the ARM passes.
  The `pfnTickFacilityConstruction` hook is no longer "not ported". IDB: comments at 0x62824 and
  0x62948.

- [x] part 35, the manufacturing refresh: `ManufScreen_RefreshBaseTabs (0x70ebc)`, 500 bytes — 25
  more checks. The last unported callee of `Geoscape_TickHourly`, so the hourly tick is now
  completely ported apart from `ManufScreen_CommitProduction` (0x6f34c). Products 119..121 go into
  the accumulator with **no capacity test at all**; every other product is tested per product
  (storage used + item quantity across bases + that product's own `bCargoSize` against
  `Base_CalcStoresCapacity`, both capacity terms recomputed each time). Over budget: shortfall,
  message 32, the slot cleared, and the function **returns** — 0x710A4 branches past the commit
  call, so one over-budget product ends the refresh for the remaining bases too. The message payload
  is again the ARM's, not Hex-Rays': `(32, base, product, 0, base)`. IDB: comment at 0x70ebc.

- [x] part 36, the craft slot map `Ship_ProductSlotToType (0x65a6c)`, 88 bytes — 18 more checks, and
  the last missing dependency of `ManufScreen_CommitProduction`. Renamed in the IDB from
  `BaseFac_RaceCharToIndex`: it is not a race, it maps the five craft-count product slots to
  `CraftType` (117->0, 118->3, 119->4, 120->1, 121->2, else -1) after `AND R0,R1,#0xFF`, and TEXTS.DAT
  names those five slots after the five craft, so the map is a bijection over 0..4 — the test asserts
  that. `Smap_GrantSiteRecovery` inlines the same map at 0x99360..0x993A8, so the port now shares one
  copy. IDB: renamed at 0x65a6c with a comment; `docs/idb/functions.tsv` refreshed and the inventory
  regenerated, so `audit_names` is back to 0 unresolved.

- [x] part 37, a message-payload audit — no coverage change, two real bugs fixed. Decoding
  `ManufScreen_CommitProduction` needed the exact argument shape of `UiPanel_AppendLineWithSound`,
  and that turned out to be `(a1 panel, a2 msg, a3 R2, a4 R3, a5 [SP+0], a6 [SP+4])` — SIX
  arguments. Two earlier "Hex-Rays is wrong, the ARM says otherwise" corrections were themselves
  wrong: they read the caller's own frame locals as extra arguments (`var_8` at `[SP+8]` in a
  0x10-byte frame, `var_10` at `[SP+8]` in a 0x18-byte frame). Message 38 is
  `(38, base, row, col, 0)`, not `(38, base, col, 0, 6*nBase)`; message 32 is
  `(32, base, base, product, 0)`, not `(32, base, product, 0, base)`. Both ports now pass the
  correct payload, the tests assert all four slots with fixtures where the four values are
  different (the old ones passed a wrong payload because base 0 made two slots indistinguishable),
  and the IDB comments at 0x62948 and 0x70ebc carry the corrected shape. The other five ported
  message sites (39, 40, 41, 42, 36) were re-checked against the ARM and were already right; 39's
  third argument is the radar scratch byte at 0x15967C, which the port has no model for yet and now
  records instead of silently passing 0. **Rule going forward: a payload claim must be checked
  against the callee's parameter count before it is called a Hex-Rays error.**

- [x] part 38, the manufacturing commit pass: `ManufScreen_CommitProduction (0x6f34c)`, 1408 bytes —
  61 more checks. This is what `ManufScreen_RefreshBaseTabs` tail-calls, so the hourly manufacturing
  chain is now complete apart from the UI that fills the tables. The four per-base columns are
  allocated work (`auManufCol` @ 0x15E19C), accumulated work (0x159E9C), units requested (0x15B79C)
  and units committed (0x15D09C), and `ObjectRec+0xBA` is the **work one unit costs**: `_rt_sdiv`
  takes the divisor in R0 and the dividend in R1 (proved at 0x65BD0/0x65BDC, where
  `Base_UpdateWorkshopNeeded` divides by 50), so the quotient is the units the accumulated work has
  paid for. Three independent confirmations: the panel percent at 0x6E704/0x6F060 is
  `accumulated*100/(work*target)`; the shipped OBJECTS.DAT has a non-zero work cost on exactly the 35
  manufacturable records (Laser pistol 576 … AVENGER 55712), all with a zero price and all the only
  records with component bytes; and the two craft you never build (117, 118) are zero there while
  119..121 are the three the hangar branch covers. `ObjectRec+0xC0..+0xC3` is the component bill
  against Elerium-115 / Alien alloys / UFO power source / UFO navigation (FIRE STORM 65+1+1, AVENGER
  120+2+1). Gates in order funds → stores (skipped for 119..121, and here `Base_CountItemQtyAcrossBases`
  takes the SITE COUNT, not the -1 gate) → components; any blocked product clears its slot, posts 33
  only if no other shortfall was just posted, and **returns from the whole pass**. One unit per call
  even when several are paid for. Completion (`units == requested`) posts 34 and does not return.
  Also: the IDB's `ObjectRec` was redefined — it hid 0xb9..0xc7 behind one `char[15]`, which is why
  the work cost, price, components, readiness selector and cargo size had no names there. Open item
  recorded: `RefreshBaseTabs` tail-calls this function and the port keeps the two envs separate until
  the hourly tick is wired into the shell.

- [x] part 39, the manufacturing screen's bring-up: `ManufScreen_Init (0x710b0)`, 488 bytes — 50 more
  checks. Pad, six click controls, three pre-existing controls, then the panel build. Two things this
  settled. (1) The two stores Hex-Rays shows as `->x` and `->uWidth` at 0x710F8 / 0x7110C are one dword
  each at PadImage +0 and +4 — the slot holds a PadImage (new(0x14), PadImage_ctor) and Hex-Rays types
  it as `UiControl*`. Wrong struct, not a Hex-Rays bug; the same class of mistake as part 37. (2)
  `unk_186BB4` and `unk_186BB0` are **not** manufacturing controls: each has over 100 xrefs across
  OptionsScreen_*, ResearchScreen_*, Geoscape_DrawScreen, Geoscape_OnPointer/Up and Geoscape_Draw. They
  are shared geoscape controls each screen repositions on open, so they stay unnamed — checked before
  renaming, which is the point. `g_aManufCtlDesc @ 0xAB9F8` (6 × UiControlDesc, only xref here) is now
  typed and named in the IDB, and the test opens main.dat for real: manuf.pad is 240×320, up/sup/
  down/sdown are each 23×17, so the descriptor pad names are a checked claim and not a transcription.
  The panel build is the LAST thing this function does (0x71268), the opposite of `Ui_InitTransfer`
  (0x995A4); the build stub records the first control's x when it runs and the check wants 0xCD. The
  two origin stores are recorded rather than asserted — the port's `calloc` allocation already zeroes
  them, so a check there could not fail. The stale `stru_AC3B8` citation was corrected to the IDB's own
  `g_aTransferCtlDesc`.

- [x] part 40, the manufacturing panel build: `ManufScreen_BuildPanels (0x6e9e8)`, 2404 bytes — 49 more
  checks, and the manufacturing screen is now brought up end to end. The row model is research-driven:
  topics 1..n-1 with `bUnlocked` (+0xD) set, then the five object slot ids at +0x18..+0x1C filtered
  through `abManufRowFlag @ 0xAA548`. That table turned out to be **exactly** the 35 shipped slots with
  a non-zero `uManufWorkPerUnit` (all 149 checked, no disagreement), so the list is "unlocked by
  research, and buildable". The two divisions confirm the four-column model again: the percent is
  `100*apProductA / (workPerUnit*apProductB)` with the divisor in R0, and the unit column is
  `apProductA / workPerUnit`. No sort pass, unlike the buy/sell and transfer builds. Two IDB fixes came
  out of it: `aFacTypeTable @ 0x15EF08` was the **research topic table under a wrong name** (the ARM's
  `MUL #34` + `ADD =0x15EF08` at 0x72CC0 and `ADD #0x22` here settle base and stride; the second
  `aResearchTopics` label at 0x15EF0C had zero xrefs and was four bytes off a record boundary), so it is
  now `aResearchTopics` typed `ResearchTopicLive[144]`, with `bUnlocked` at +0xD and `abItemIds[5]` at
  +0x18 named for the first time. `byte_AA548` is now `abManufRowFlag`. **A process note worth keeping:
  my first transcription of those 172 bytes from the IDB was wrong in the 106..125 region, and I had
  written a whole paragraph claiming the two sets differed on six slots. `audit_tables.py` caught it
  because it compares the port's table against the image byte for byte.** Long byte runs are transcribed
  by a tool, not by eye.

- [x] part 41, the manufacturing draw click: `ManufScreen_Draw (0x6e3e0)`, 1332 bytes — 42 more checks,
  and the manufacturing family is complete except `ManufScreen_DrawFrame (0x6f9a4)`. It is the single-row
  twin of `BuildPanels`: same percent, same asked/units column, same allocated-work column, one row
  instead of the list. Two things it has that BuildPanels does not.
  **The seed rule** (0x6E4E4..0x6E51C): a product with a quantity and allocated engineers but no
  accumulated work gets `apProductA[idx] = 1`. And **the reload after it** — 0x6E6D4 reloads `apProductA`
  from memory, so a seeded row takes the percent path with that one unit in it. My first version read the
  accumulated work once and sent the seeded row down the empty-string path; the test caught it as two
  failures, and that is the reload's evidence.
  **A dead branch found.** 0x6E52C reads the dword at `pManufProductPanel + 0` and 0x6E530/0x6E538 test it
  for `> 118 && < 122` — plainly "the selected product is one of the three hangar products 119..121" — but
  nothing in the binary writes a product id there. `BuildPanels` leaves panel A's style bytes at
  B4 B4 B4 32 32 FA (0x32B4B4B4), and `UiPanel_ClickRow` / `UiScrollList_TrackHold` store the clicked row
  at panel offset **12**. So the hangar byte at 0x159DBC is never raised from this screen. Recorded in
  `bugs.tsv`, and the test forces the dword into range to show what the branch would do: 118 and 122 stay
  out, 120 and 121 take it, and 250 + 5 wraps to 255 because the store is a byte. `Sound_Play @ 0x9e0c0`
  takes ONE argument — its body reads only R0 — so the `MOV R1,#4` at 0x6E428 is residue, not a second
  argument. IDB: the whole reading is now a comment at `0x6e3e0`, the dead branch included.

- [x] part 42, the manufacturing frame: `ManufScreen_DrawFrame (0x6f9a4)`, 5400 bytes — 68 more checks,
  and the manufacturing family is now complete. This is the first port tested by **drawing it for real**:
  a 240 x 320 16 bpp surface, the real `FONT.SPR` out of lang.dat loaded the way `MainMenu_Init` loads it
  (glyph base = the running frame count, before the load), and the real 205-byte TEXTS layout. Every check
  is a pixel at a coordinate the original names, a control found at a position, or a gate byte.
  **The rails are a ramp, not a constant.** Twenty-four steps of 7 at x 5 / x 234 and twenty-four steps of
  5 at y 5 / y 315, with the colour index climbing `5*(k+20)` and falling `0xE6-5k` on the sides and
  `0xFA-5k` on top and bottom. The two halves of a side rail overlap at y 151..168 and the climbing half is
  issued later, so it wins there — the first version of the test assumed otherwise and failed at (5,151),
  which is what proved the draw order.
  **The band is capped by `nStyle`, not by `nLineCount`** (`16 * min(nLineCount, nStyle)`, 0x6FC78), and the
  separators are `band + 2` tall: (130,118) is the last drawn row and (130,119) is empty, which is only
  true for the capped value.
  **The map-cursor row's name comes from panel D's row cursor** (+0xBD8) indexing panel A's aux column
  (+0x13B4) — not from panel A's own cursor. Checked by drawing it and then removing one gate condition at
  a time (cursor y 70, cursor y 70+band, preview not ok).
  **The price factor is 0.7, pinned by pixels.** 0x6FFBC/0x6FFC4 build the double
  `0x3FE66666_66666666`; `CommitProduction` uses 1.4 for the same `anObjectPriceInit`, so the screen quotes
  half of what committing costs. The test rebuilds the price band from a reference (the same detail-panel
  frame, then the label) and compares it pixel-for-pixel against "700$", then shows it is not "701$", not
  "699$", and that the "$" is part of it; then 1428 -> "999$" and 1429 -> "1000$", which pins the factor to
  0.7 within 1/1428.
  **A zero component advances nothing.** The four component rows stack only where something is asked for:
  a three-component product and a four-component product are identical in the first two rows and differ only
  in the fourth. The short-component colour is `20*(nMapPhase+5)`, checked by comparing the same row at
  phase 0 and phase 5.
  **The two quantity controls are moved and re-pointed every frame** — `unk_186B94` to (94,251) with
  `ManufScreen_UpdateProgress`, `unk_186B90` to (94,271) with `ManufScreen_ReduceSelectedQtyToOne` — checked
  through the injected blit seam: eight blits in slot order, and the two controls found at those positions
  afterwards. `TEXTS[1711]`, the second button's caption, is past the 1650 records lang.dat loads, so it
  draws empty in the shipped build; the test leaves that record empty for the same reason.
  All 22 label addresses are TEXTS records (`addr - 0xB38D0 = 205 * n`); their lang.dat text confirmed the
  four component captions against the four component slots `CommitProduction` gates on. IDB: the whole
  reading is now a comment at `0x6f9a4`.

- [x] part 43, the research bring-up: `ResearchScreen_Init (0x749a8)`, 592 bytes — 54 more checks. It is
  the twin of `ManufScreen_Init` with three differences worth naming.
  **Four controls, not six** — research has no engineers pair — and the four descriptors are now
  `g_aResearchCtlDesc @ 0xABB10` (renamed from `stru_ABB10` in the IDB): scroll pair (205,18)/(205,38),
  quantity pair (10,163)/(10,183).
  **The two shared bottom controls point the other way round.** `unk_186BB4` gets
  `ResearchScreen_CancelSelectedAndClose` and `unk_186BB0` gets `ResearchScreen_StartSelected`, where
  `ManufScreen_Init` gives BB4 its OK button and BB0 its cancel. They are generic geoscape controls and
  each screen re-points them — which is also why `Ui_OpenResearch` parks BB4 at (1000,1000) and this
  Init moves it back to (165,188).
  **Handlers first, positions last.** BB4/BB0 get their `pfnClick` at 0x74AC4/0x74AD0 but their
  coordinates only at 0x74B98..0x74BB4, after the rebuild call and the topic walk. The test pins the
  order with three hooks that record the screen state when they ran: the rebuild hook sees the cancel
  control still parked where the test put it, and the build hook sees panel 1's `nLineCount` still at
  the preset 99, so the refresh provably runs after the build.
  **The topic walk** (0x74B08..0x74B8C) clears `auResearchCol[144*base + i]` for every topic whose
  `bActive` is zero, bounds only by the count byte, and sign-extends `nSelectedBaseIdx` through
  `LDRSB` + `LSL #24 / ASR #24`. `ResearchScreen_Init` is also the only research entry point that
  refreshes the three panels itself rather than leaving it to a handler.
  IDB: the whole reading is a comment at `0x749a8`, and the count byte at `0x160C4C` is now a named
  `u8 nResearchEntryCount`.

- [x] part 44, the research panels: `ResearchScreen_BuildPanels (0x733f4)`, 1600 bytes — 48 more checks,
  and one finding worth the unit on its own.
  **The progress percentage is computed two different ways in the same screen.** BuildPanels divides
  first (`_rt_sdiv(wTotal, wProgress)` then `MUL R0,R3,#0x64` on the quotient at 0x73870), RefreshPanels
  multiplies first (`MUL R1,R2,#0x64` at 0x73168, then the sdiv at 0x73184). A topic one quarter done
  reads **0%** when the panels are built and **25%** after any refresh — which is exactly what the player
  sees when the research screen updates under a running topic. Reproduced, in `bugs.tsv`, and the test
  demonstrates it directly: the same panels, the same topic, "0%" from the build and "25%" from the
  refresh.
  **The topic's name lives at record +0x4 as a TEXTS index** (0x7373C `LDRH [R10,#4]`, `MUL` by 0xCD
  against 0xB38D0) — the same u16 the weapon fire gate reads as a value. `ResearchTopicLive.wGate4` now
  carries both meanings in its comment.
  **The research screen's block IS the topic table.** `ppScreen` = 0x15EF08, so the pad sits in topic 0's
  first four bytes, the four controls at 0x15EF08 + 0x1D48 = 0x160C50..0x160C5C, and
  `pResearchTopicPanel` @ 0x160C68 is exactly one 34-byte record past the end of the 144-record table.
  That is what the teardown's `aResearchTopics[220].abUnkF[1] + 4*i` addressing means (220*34 + 0x10 =
  0x1D48).
  The filter is `bActive`, not `bUnlocked` plus `abManufRowFlag` — research lists topics, manufacturing
  lists items — and there is no object array in this function at all. Panels are constructed with the
  full 144 rows and only their `nLineCount` is set to the row count; `nStyle` is 15, not the
  manufacturing 13, and `bUnk7088` is 0x94, not 0x96. IDB: comments at 0x733f4, 0x7373C and 0x73870.

- [x] part 45, the research rebuild: `ResearchScreen_RebuildTopicList (0x74270)`, 1700 bytes — 160 more
  checks and three findings.
  **Completing a research topic is what unlocks a facility kind, and the map is a jumptable
  permutation, not an identity.** Both switches (0x74460, 0x74668) switch on the topic number with cases
  37..53 and write 1 into `aBaseFacFlags[kind].bFlag20` (0x186E48 + 28*kind + 0x14): 37→0 Access Lift,
  38→1, 39→2, 40→3, 41→4, 42→5, **43→13 Missile Defences**, 44→6, 45→7, **46→14 Laser Defence**,
  **47→no slot**, 48→15, 49→16, **50→12 Gravity Shield**, **51→10 Mind Shield**, **52→8 Psi Laboratory**,
  **53→9 Hyper Wave Decoder**. Case 10's jumptable entry *is* the default label, which is also where the
  other sixteen land — so the message append and the line rewrite run for every walked topic, not only
  for the mapped ones. **FAC_HANGAR (11, record 0x186F90) is the kind no topic ever writes.** The
  permutation is not inferred: `LoadGame @ 0x4539C` carries the same seventeen-entry table over the same
  0x186E48 base with the same `-37` offset (`MOV R3,#0xFFFFFFDB`) and the same unmapped case 10.
  **The two `(lo,hi)` topic ranges live in the record AFTER the one being tested** —
  `aTopics[n+1].abUnk0[0..1]` for the unlock roll (0x745AC `LDRB [R9,#0x22]`) and `[2..3]` for the
  availability scan (0x74868), while every flag read is `aTopics[n]`. For n == 143 that is 0x160228, the
  four-byte gap between the 144-record table and `auResearchCol` @ 0x16022C, which has no cross-reference
  anywhere in the IDB. No other function reads those four bytes of a record, so the pairing is visible
  only here. main.dat really loads 144 entries, so the last walked topic is 143 and the gap is reached in
  the real game.
  **Deviation found (0x74648), in `bugs.tsv`.** The column clear after the unlock roll is `MOV R0,R7`,
  and R7 still holds `&auResearchCol[nTopic]` from 0x74428; the rolled index was used only for the
  `bUnlocked` store and `nResearchTopicShown`. So the finished topic's eight per-base entries are cleared
  twice and the newly unlocked topic's never — and `ResearchScreen_AccumulateAllocated (0x74914)` books
  `auResearchCol[i]` into `wProgress` for locked topics too, so a locked topic really can arrive at its
  unlock carrying a non-zero allocation.
  Also pinned: the roll's 50-try cap (0x7460C — after 50 tries the original accepts whatever it landed
  on, tested by seeding the RNG so the first and fiftieth draws differ); the byte product gate on record
  143 (a prerequisite holding 2 gives 2 and the `== 1` test fails); the finished branch's item spend and
  `wProgress = 0` being a pair that is skipped together, so a topic needing no item keeps its progress;
  and the in-progress accumulator adding the prerequisite topic *numbers* rather than any count.
  Housekeeping the unit owed: the four 5-byte groups of the in-memory record are now named —
  `abNeedItems[5]` (+0x0E..0x12), `abPreTopics[5]` (+0x13..0x17), `abItemIds[5]` (+0x18..0x1C),
  `abActivateTopics[5]` (+0x1D..0x21) — in the header **and** in the IDB's `ResearchTopicLive`;
  `unk_15EEF8` → `nResearchFinishedTopic`, `unk_160C60` → `nResearchTopicShown`. IDB comments at 0x74270,
  0x74460 and 0x4539C.

- [x] part 46, the research frame: `ResearchScreen_Draw (0x73a34)`, 2108 bytes — 69 more checks and four
  findings.
  **The screen pad is blitted out of the topic table itself.** 0x73A3C is `LDR R0,[R0]` on the argument,
  and Geoscape_Draw's id-8 case (0x94114) passes 0x15EF08, so the `PadImage *` ResearchScreen_Init
  installed lives in `aResearchTopics[0]`'s first four bytes. It is also the only one of the three
  screen frames that blits a pad at all — `ManufScreen_DrawFrame` opens straight into the rails.
  research.pad really loads, 240x320, mode 6 (the `MOV R2,#6` at 0x749DC).
  **The close control is blitted only when no topic row is selected** (0x73D64 → 0x74260). With a row
  picked, this function never redraws `unk_186B00`; the flag is panel A's click handler's
  (PlaySample7_SetFlags_Cache0xBD8 @ 0x73328) and `ResearchScreen_StartSelected` clears it.
  **The two column bars are capped by `nStyle`, not by the row cursor** — height
  `16 * min(nLineCount, nStyle) + 2` of the TOPIC panel (0x73D00..0x73D20), at x 170 and x 200, y 69,
  index 0xB4. The cached cursor at 0x160C74 only picks which `anLineAux` cell names the topic whose
  `auResearchCol` entry is printed at (195, 146).
  **The rails were a fourth copy of the same 96 blits.** TransferScreen_Draw (0x97FB8..0x98150),
  ManufScreen_DrawFrame (0x6F9C4..0x6FB54) and this function (0x73A54..0x73BD8) carry the same loop
  bounds, the same coordinates and the same ramp formulas, and the ARM stores each index triple with
  STRB, so the falling ramps are the low bytes of `RSB #0xE6` and `RSB #0xFA`. They are now one
  `BaseUi_DrawFrameRails` helper with all three call sites using it.
  Housekeeping: `byte_160C7C` → `bResearchPanelsBuilt`, `unk_160C80` → `bUiRowSelected` (both u8), IDB
  comments at 0x73a34, 0x73D64, 0x7406C, 0x74184 and on the two bytes. The test also pins something the
  other screen tests should carry: the pen `Text_DrawString` paints with is the **second** triple of the
  packed pair, so the "white" pair 250,250,250 / 0,0,0 draws black text and the teal captions come out
  as 4,70,75.

- [x] part 47, the buy/sell frame: `BuySellScreen_Draw (0x77e6c)`, 3524 bytes — 85 more checks and four
  findings.
  **The frame splits on `bBuySellPanelsBuilt` and the two halves are different drawings.** Same rails,
  same close control, same funds line, same site name, same five panels, same four separators, same
  "no row under the cursor" tail — but the total line is TEXTS[157] "Итого" in 255,120,120 on one half
  and TEXTS[411] "Выручка " in 120,120,255 on the other; the column captions swap sides (TEXTS[92]
  centred on 210 / TEXTS[158] on 155, against 145 / 212); the separators move from x 130/176/191/206 to
  130/160/175/190; and the shortfall panel exists only on the not-built half.
  **The close control is blitted unconditionally here**, the opposite of the research frame.
  **The row marker decodes three ways** (`panelA->anLineAux[cursor]`): 1000 is a soldier and its index
  comes from **panel B's** aux cell because panel A only carries the marker — the name is
  `apSoldiers[panelB->anLineAux[cursor]]->szName`, `Soldier.szName` being +0xBA — with TEXTS[412] under
  it; `> 2000` is an alien and 0x784E8's `LDR R2,=0xFFFFE1C8` (-7736 dwords off the 0x1139B8 root) is
  exactly `apObjects[marker - 2000]`, with TEXTS[1605] under it; anything else is a plain object, priced
  through TEXTS[413] + `ObjectRecFull.uPrice` (+0xBC) except markers 117..121, which get TEXTS[414]
  instead. Hex-Rays renders both array reads as offsets from `Map_nObjectCount`; the ARM
  (`LDR R2,=0x1198`, `LDR R2,=0xFFFFE1C8`) is what fixes them onto `Map+0x1198` = `apSoldiers` and
  `apObjects`.
  **Deviation found (0x78864), in `bugs.tsv`.** The not-built half dereferences all five panels after
  `BuySellScreen_BuildPanels`' teardown (0x77D60..0x77E38) has `operator delete`'d them without nulling
  `pBuySellPanelA..E` — the teardown's last write is the funds rollback at 0x77E50. It writes
  `bFlag7073`/`bUnk7080` into freed panels, reads panel B's `nLineCount` at 0x788E4 and panel A's aux at
  0x78B54, and at 0x78B80 uses the marker raw, so an alien row there would deref `apObjects[2077]`; the
  built half does subtract 2000, which is what makes the omission look unintended.
  **`bBuySellCursorOnRow` (was unk_161A30) is written only by the not-built half** — all five of its
  cross-references sit in that tail. Housekeeping: `unk_161F0C` → `bPanelFlagScratch` (shared with
  `BasePlaneScreen_DrawCraftDetail` and `TransferScreen_Draw`), IDB comments at 0x77e6c, 0x784E8,
  0x78864 and 0x78B80. Every one of the ten label records cross-checks against lang.dat.

- [x] part 48, the first radar-screen leaf: `RadarScreen_BlitMapBackdrop (0x5eb10)`, 508 bytes — 49
  more checks and four IDB corrections.
  **It is a phase toggle before it is a draw.** `bRadarBlinkPhase` (was `unk_159730`) zero means set
  it and touch nothing; set means clear it and draw — so the radar backdrop appears on alternate
  frames. `RadarScreen_Init` clears it (0x5F4CC) and `RadarScreen_Update` reads it twice: for the
  50-against-0 pick (0x60584) and for the flashing message colour block (0x61144).
  **`pSmapViewSize` @ 0x186BC0 was misread as the u16 world-size pair; it is a pointer to a glyph
  record.** `Geoscape_InitUI` allocates `0x4E204 = 800*400+4` bytes there and writes 800/400 into it
  (0x85A18..0x85A50); `Smap_ZoomIn` (0x81E9C) and `Smap_ZoomOut` only rewrite the u16 pair *through*
  the same pointer (600x300, then 472x236). `RadarScreen_BlitMapBackdrop` passes it as
  `SoftSurf_BlitFlip`'s third argument, which `SoftSurf_BlitFlipEx` forwards as
  `SoftSurf_BlitFlip_Inner`'s `pSrc` — the IDB's own parameter names `nW`/`nH` are the trap: the
  third argument is the glyph record and the fourth is the palette table. Renamed to
  `pSmapWorldBits`.
  **The palette is slot 29**, built as `aPalette + 0x7400` (0x5EC28) — the reason `0x14E6D0` has no
  xrefs of its own. Named `aPaletteSlot29`. The mask is `pSmapLayerPad->pData`, the layer pad's own
  byte plane used as per-pixel alpha.
  **`nSmapModeXBias` → `nSmapOverlayOriginX`**: subtracted from the tile x in `Smap_GetTileAtMode`
  (0x821D0) and from the scroll in all three geoscape map blits, and `Geoscape_Draw` (0x8C3D4) is its
  only writer.
  The half-extents are `nMapW / 2 - 120` and `nMapH / 2 - 160`, and the ARM's `MOVS` / `ADDMI #1` /
  `ASR #1` triple is C division by two, not an arithmetic shift — `-5` becomes `-2`, pinned by a
  check. The three world blits sit one map width apart (left of, on, and right of the viewport) and
  all three are skipped when `nGeoscapeScreen` is 2. `pSmapPadCur` and `pSmapLayerPad` are now typed
  `PadImage *` in the IDB.

- [x] part 49, two more radar leaves: `RadarScreen_DrawFuelGauge (0x5d918)` and
  `RadarScreen_DrawRangeBar (0x5d6e4)`, 1088 bytes — 66 more checks and five more IDB names.
  **The gauge reads the consumed percentage, not the remaining one**: `n = (int)(100.0 - 100.0 *
  (double)Ship.nFuel / (double)Ship.uFuelMax)`, built as two `_itod`, `_divd`, `_muld` by 100.0,
  `_subd` FROM 100.0 and `_dtoi`. Both halves are read with `LDRSH`, so `uFuelMax` is read **signed** —
  pinned by a check where a `0x8000` `uFuelMax` clamps to 100 where an unsigned read would give 99.
  Reaching 100 clamps it there and sets `bRadarCraftOutOfFuel`, the only thing the original keeps of the
  number and what `RadarScreen_Update` reads nine times.
  **The tens-digit rule is a branch shape, not a rounding trick.** 0x5DA9C jumps past the blit when the
  tens digit is non-zero and 0x5DAA4 jumps past it when the hundreds digit is not 1, so a zero tens
  digit is drawn only at 100%. A leading zero is dropped, except when a hundreds digit justifies it.
  Every one of the four pad origins is written whether or not the pad is drawn.
  **`_rt_sdiv`'s argument order re-confirmed**: divisor in R0, dividend in R1, quotient out in R0,
  remainder in R1 — cross-checked against `Inv_HandlePointer`'s `(nX - 23) / 38` at 0x11B0C before
  trusting it for the digit split here.
  **The range bar's level term is always negative**, so a higher craft class *loosens* the gate:
  `nRadarRangeCur + 560*(nRadarCraftLevel - 13)/114 <= nRadarRangeLimit`, with `nRadarCraftLevel` 8/6/4/2/0
  for the five arms that write it (0x5F6D8, 0x5F7A8, 0x5F878, 0x5F944, 0x5F9E8) and three arms that
  leave it at whatever the previous intercept left. `bCraftClass` is read with `LDRSB`, so the 255
  sentinel becomes -1 and passes the non-zero gate only to fall into the switch default and pay nothing.
  The 70% roll is pinned by fixed seeds (1..4 fire, 7013..7016 draw exactly 70 and do not) rather than
  by a probability.
  Named: `aRadarDigitPads` (11 `PadImage *`, loaded as `i0..i9` through palette slot 0x19),
  `pRadarPercentPad`, `bRadarCraftOutOfFuel` (u8), `nRadarRangeLimit`, `nRadarCraftLevel`.

- [x] part 50, the radar screen's session: `RadarScreen_Init (0x5f490)`, `RadarScreen_SetActive
  (0x6144c)` and `RadarScreen_RollDetection (0x602a0)`, 4396 bytes — 194 more checks, 18 more IDB
  names, and the first test suite in the project that loads the shipped archives and compares real pad
  bytes.
  **`RadarScreen_Init` is one function doing both directions**, toggled on `bRadarScreenActive`
  (LDRB at 0x5F494): flag 0 opens, flag 1 closes. That is why `RadarScreen_SetActive` calls it on both
  of its own branches — Init decides which half runs from the flag the caller just decided. SetActive
  then writes `bActive` into `GameTime.bPaused` (STRB at 0x159618+4) and `bUiFrameStyle` (0x186566):
  **opening the radar screen pauses the game clock** and picks the bright panel frame.
  **The eight-case class switch is one flat table.** Cases 1, 3 and 5 set the HP/palette/overlay/limit
  half and fall through into 2, 4 and 6 for the art and level half only, and the `if
  (nInterceptTargetHp == 0)` guard inside 2, 4 and 6 is *always* true because 0x5F614 zeroed that field
  moments earlier — so the fall-through never changes anything a preceding case set. The table is now
  `g_aRadarUfoClass`, and it carries RollDetection's per-class speed too, because both are indexed by
  the same `bCraftClass`. The doubles are the original's own literals (0x60324 stores
  0x40425553_2617C1BE, which is exactly `36.6666`).
  **The palette copy at 0x5FC34 overruns two slots** (bugs.tsv row 0x5FC34): 766 dwords written from a
  256-entry slot, so slot 1 (menu.pal, the target) is overwritten as intended and then all of slot 2
  and 254 of 256 of slot 3 too, reading from slot 26 through 27 and into 28. `base.pal` repairs slot 3
  at 0x5FC58 and the close path restores slot 1 at 0x60290, but **nothing anywhere in the binary
  reloads slot 2** — from the first radar screen onward it stays holding ufoflash.pal. Reproduced, and
  pinned by checks that compare slots 1, 2 and 3 against independently reloaded palettes both after the
  open and after the close.
  **`uFuelCap / 60` is the craft's speed.** RollDetection's comparison
  `uFuelCap * (1/60) < dSpeed` is literally "the tracked contact outruns our intercept craft", and it
  uses the same basis `StrategList_ProcessEvents` already uses for an inactive craft
  (pufo_strateg.c:504) and `Ship_BurnFuel` burns by — so the per-class constants 36.6666 … 83.3333 are
  contact speeds in the same units. The boundary is pinned: class 0 is 36.6666, and 2200 fuel gives
  36.666666…, which is *not* below it, while 2199 is.
  **TEXTS[1599] is Cyrillic МАК, not ASCII MAX.** lang.dat decodes it as cp1251 `D1 EB E5 E4 20 CD CB
  CE 20 EF EE F2 E5 F0 FF ED` = "След МАК потерян"; the middle word is Cyrillic М-А-К that reads like
  "MAX". The test compares against those exact bytes and asserts records 1598 and 1600 are different
  strings so the record index cannot be off by one.
  **`Game_RandU16(1)` is always 0** — `(u16)(x/32768 * 1)` with `x < 32768` truncates to 0 — which gives
  a deterministic way to force a roll's success branch without pinning seeds. For the failure branch,
  seed 12345 gives `Game_RandU16(100) == 23`. (An earlier probe that printed "seed 1 → 58" was reading
  unspecified `printf` argument evaluation order, not the first draw.)
  Named in the IDB: `bRadarScreenActive`, `nInterceptShipIdx`, `nInterceptStrategIdx`, `aRadarControls`
  (`UiControl *[5]`), `aRadarButtonPads` (`PadImage *[5]`), `aRadarCtlDesc` (`UiControlDesc[5]` at
  0xAB2A0), `bRadarTraceLost`, `bRadarFlip`, `nRadarAmmoL`, `nRadarAmmoR`, `nInterceptTargetHpMax`,
  `pRadarFramePad`, `pRadarUfoPad`, `pRadarUfoFlashPad`, `pRadarUfoMissilePad`, `nRadarCursorX`,
  `nRadarCursorY`, `aTextRadarTraceLost`.

- [x] part 51, the two weapon mounts on the radar screen: `RadarScreen_DrawCraftArmament (0x5edf4)`,
  1692 bytes — 69 more checks, 9 more IDB names, one new enum, and the second real-surface test suite
  in the project (this one on the shipped `font.dat` + `basebits.dat`).
  **The mount ids are an enum, and the data names them.** `Ship.bWeaponL/bWeaponR` hold 24..30, now
  `WeaponMount` in `pufo_ship.h` and in the IDB. The names come straight out of the shipped lang.dat
  TEXTS blob at record `1446 + id`: 1471 "Корабельная пушка", 1472 "Уст-ка Стингер", 1473 "Уст-ка
  Эваланш", 1474 "Лазерная пушка", 1475 "Плазменная пушка", 1476 "Термояд. рак. уст.". This also
  corrects an older note that called id 24 "Psi-arm": 1470 is "Пси-усилитель", a psi *amplifier*, and
  it is the one id no mount switch handles.
  **The mount's range is a literal in the switch**, 140/260/360/180/300/420 for ids 25..30, and it is
  what `nRadarBoundL/R` hold. The bar height is `_rt_sdiv(560, 114 * range)`, so the cannon's 140
  scales to 28 pixels and `28 - 34` is negative: **the cannon draws no range bar at all**, while its
  tick row — gated on `bWeaponL != 0`, not on the bar — is still drawn, at y 170. The laser's 180
  scales to 36 and draws 2 rows but still gets its 3x3 cap, because the cap gate is `range > 170`.
  **The engage gate is a distance test.** `nRadarRangeCur + _rt_sdiv(114, 560 * (nRadarCraftLevel -
  26))` is the current reach in the same 560/114 scale the range bar uses; when it is *above* the
  mount's range the target is out of gunshot and the ARM's predicated group (0x5EF88..0x5EFB0,
  0x5F2A8..0x5F2C4) clears `bRadarEngageL/R`, forces that mount's cooldown to 255 and drops the
  in-flight projectile. Hex-Rays shows the left test twice because the two store groups are separate
  predicated instructions.
  **The two cooldowns are the same kind of timer as the message and pulse timers.** `Geoscape_PulseUpdate
  @ 0x95B80..0x95BCC` counts 0xAC218, 0xAC21C, 0xAC220 and 0xAC224 up to 255 one per tick; a completed
  shot sets them to 255/240/235/251/249/230 by weapon. 0xAC218 is shared — BaseAtScreen, Smap_OnPointerUp
  and Geoscape_OnPointer reuse it as a generic screen animation timer — so its old name
  `nBaseAtAnimTimer` was renamed to `nRadarCooldownR` with that reuse written into the comment.
  **The halves are not mirror images in order**: left draws bar, cap, label, ticks; right draws label,
  bar, cap, ticks. The port keeps each half's own order.
  **`Map_nGlyphPalBase` is ONE global and the port had two copies.** `Font_DrawGlyph` picks palette
  slot `Map_nGlyphPalBase + nPal` and `Text_DrawString` forces it to 2 for its own duration, so the
  copy the shell kept in `PufoWorldState` never reached the font. That copy is removed and every env is
  wired to `g_nGlyphPalSlotBase`. A port modelling defect, not an original bug — no bugs.tsv row.
  Named in the IDB: `nRadarShotYL`, `nRadarShotYR`, `bRadarLastShotHit`, `bRadarEngageL`,
  `bRadarEngageR`, `nRadarBoundL`, `nRadarBoundR`, `nRadarCooldownL`, `nRadarCooldownR`, plus the
  `WeaponMount` enum.

- [x] part 52, the intercept shot animation: `RadarScreen_DrawOverlay (0x5dc84)`, 2788 bytes — 89 more
  checks and one more original bug found.
  **A half runs per mount, gated twice.** The mount is idle when its ammo is exactly 0 AND its
  projectile offset is 0, and an idle mount loses its engage byte (0x5DCA8..0x5DCEC);
  `bRadarLastShotHit` is cleared unconditionally at 0x5DCC0 before either half. A half then runs only
  when its engage byte is non-zero AND its cooldown is exactly 255. The halves are independent —
  skipping the right one does not gate the left one — and the idle test is `== 0`, so the -2 sentinel is
  *not* idle.
  **A round is taken only when a shot *starts*.** The decrement is gated on the projectile offset being
  0, not on the shot firing or landing.
  **ORIGINAL BUG (0x5DD1C / 0x5E258), reproduced, bugs.tsv row added.** The -2 "no weapon on this
  mount" sentinel is not exempt from that decrement. A mount id outside 25..30 — 24, the psi amplifier,
  which the shipped data does use — stores a *zero* weapon range in `RadarScreen_DrawCraftArmament`, so
  once the radar's reach falls to 0 the engage byte is set and the cooldown stays at 255 because nothing
  ever lands. This function's switch has no arm for that id, so the projectile offset never advances and
  the "a shot started" test stays true every frame: the counter walks off through -3, -4, -5 …, and
  `RadarScreen_DrawCraftArmament`'s label test is `!= -2`, so the radar screen prints the runaway number
  where an ammo count should be. Three consecutive calls pin it.
  **The flight gate is the target's distance in pixels.** `offset < nRadarCraftLevel +
  _rt_sdiv(560, 114 * nRadarRangeCur) - 33` for ids 25/26/27/30 and `offset < 2` for 28/29, with the
  steps +6/+2/+1 and `bRadarFlip` adding 2 to the four gated weapons (0x5E1BC `ADDNE`). Landing sets the
  cooldown to 255/240/235/251/249/230, zeroes the offset and stores `RadarScreen_RollWeaponHit`'s result.
  **The laser and plasma have a third case:** an offset that is neither below 2 nor exactly 2 does
  *nothing at all* — no cooldown, no roll, no draw (0x5DFEC `BNE`, 0x5E518 `return`). A beam past 2 never
  resolves.
  **The columns are not a constant offset.** The cannon is x 67/69 on *both* halves, the rockets are
  64/65/63 left against 70/71/69 right, and the laser is x 64 left against x 72 right. The avalanche and
  plasma draw the wide outer column first and the 1-wide inner column over it.

- [x] part 53, the radar frame driver: `RadarScreen_Update (0x60570)`, 3804 bytes — 84 more checks,
  one more original bug found, and the whole 16-function `RadarScreen_*` set is now ported.
  **The panel is drawn here, not in a Draw function.** Five frame blits (0x605EC..0x60690), five rails
  (0x60694..0x6076C), five signal bars whose mid colours are 0x55/0x5A/0x5C/0x5A/0x55, and the five nav
  speed controls. The panel is 140 tall normally and 190 tall in the blink phase, which is what makes the
  0x1F/0x4A/0x75 wash at (26, 176) reachable only while `bRadarBlinkPhase` is set. The middle bar row is
  14 tall, not 22 — 0x60D98 `MOV R8, #0xE`, which Hex-Rays merges into one loop.
  **`nRadarScanOffset @ 0x159724` (was `unk_159724`) is a scroll, not a counter.** It advances by 10 and
  wraps to 0 above 39 (0x61078 `ADD`, 0x61088 `CMP #0x27`, 0x61094/0x6109C `MOVGT/STRGT`), and
  `RadarScreen_Init` does not clear it, so the scroll carries from one radar screen to the next.
  **Only four of the five button pads are ever drawn** — 0x611A8 `MOV R8, #4`.
  **The damage ladder is only reachable through a same-frame hit.** `RadarScreen_DrawOverlay` clears
  `bRadarLastShotHit` at 0x5DCC0 before either mount runs, and the ladder's gate at 0x608C8 tests that
  byte, so a target already at 0 HP is not noticed until a shot lands in the same frame. Three branches:
  hp <= 0 → TEXTS[1600] + `StrategList_RemoveAt` + `Ship_ClearInterceptByStrategIdx`; 40..69 percent with
  a land tile and `Game_RandU16(4) == 0` → TEXTS[1601] + `nType = 4` + `nTimer = 7200` + the race funding
  bonus, item kept; below 40 percent → TEXTS[1602], with `bRadarInterceptDone` set *before* the tile test
  and the item removed instead of converted on a 255 tile.
  **Three tails, mutually exclusive.** done + timer > 30 closes and retargets the uncrewed chasers;
  out-of-fuel + timer > 30 closes, sets `byte_1870C8` as a DWORD, posts message 44 and calls
  `Ship_RemoveWithCargo`; otherwise the full-zoom exit runs on `bRadarFlip != 0 && nRadarRangeCur == 560`.
  **ORIGINAL BUG (0x61434), reproduced, bugs.tsv row added.** The `Ship_ClearInterceptByStrategIdx` in the
  full-zoom exit is unreachable: `BLEQ` needs the zero flag, and the only arrival that leaves it set is
  `bRadarTraceLost == 0 AND bRadarFlip == 0` (0x61428..0x61430) while the entry gate at 0x613BC already
  requires `bRadarFlip != 0`. An intercept that reaches full zoom with the trace held sends only
  `nInterceptShipIdx` home; the other craft chasing the same contact keep chasing it.
  **IDB fixed on the way:** `unk_159650` → `bRadarInterceptDone` (byte), `unk_159724` → `nRadarScanOffset`
  (int), `unk_186488` → `szScratch186488` (`char[85]`, the geoscape caption scratch shared with
  `BaseFac_BuildStatusMsg` and `Geoscape_Draw`); comments added at 0x60570, 0x611A8, 0x612BC, 0x61434 and
  the redundant re-read at 0x6089C.

- [x] part 54, the monthly funding report's arithmetic: `FundingScreen_Init (0x5c084)`, 1256 bytes, and
  `FundingScreen_OpenMonthly (0x5d160)`, 92 bytes — 70 more checks and 23 funding globals named in the IDB.
  **The five rows are five countries.** lang.dat TEXTS[1302..1306] are the row titles: США, Евро Альянс,
  Азиатский Альянс, Австралия, Африка. They are NOT in race-score order — the row → slot permutation is
  **0, 2, 3, 1, 4**, and both `FundingScreen_DrawExtended`'s five `Funding_DeltaToArrow` calls
  (0x5B804..0x5B8A4) and `FundingScreen_Init`'s five share stores (0x5C34C..0x5C38C) agree on it. The
  share, bar and arrow arrays are slot-indexed; the baseline array at 0x1595F0 is row-indexed.
  **The upkeep table has a trap.** `nMonthCounter @ 0x159648` (was `unk_159648`) is incremented at
  0x5D170 *before* the recompute, and `BLE` at 0x5C230 and `BNE` at 0x5C250 land in the *same* block —
  so month 0 is not "no upkeep" but the heaviest one (-1100/-300/-800), the same as month 6 and above.
  Months 1..3 give -300/-100/-200, month 4 -500/-200/-350, month 5 -700/-500/-300, one figure per row
  group (USA/Euro/Asia share one, Australia and Africa their own).
  **`g_nFundArrow*` is not an arrow.** It holds `Funding_ScoreToBarLevel`'s result — a money amount,
  ±25000 or ±50000 times the row's 3/5/4/1/3 factor. The draw re-derives the real -2..2 arrow from the
  share and the baseline and uses the stored dword only for its sign. `Funding_ScoreToBarLevel`'s first
  argument is the ROW, not a score (Init passes 0..4 in draw order at 0x5C39C..0x5C4A0), so the port's
  parameter is renamed `nRow`.
  **A negative bar wipes its arrow too** (0x5C4D4..0x5C53C), so a country in the red shows no arrow at
  all; separately, a negative bar *level* is zeroed before it is added when the month is below 3
  (0x5C3AC..0x5C3BC).
  **IDB fixed on the way:** `BaseRec.bUnk17` → `bKind` (the base kind 0x80..0x89, read at 0x5C1C8 as the
  region that selects the funding country), `unk_159648` → `nMonthCounter`, `unk_1595DC` → `pFundPad`,
  `unk_1865BC` → `g_nFundPending`, and the five shares / bars / levels / baselines named
  `g_nFundShare0..4`, `g_nFundBar0..4`, `g_nFundArrow0..4`, `g_nFundBase0..4`.
  **Recorded, not changed:** the 0x80..0x89 space is a region/country id in this evidence (the screen's
  own labels say countries), so the port's `PUFO_RACE_ID_*` naming is misleading. Renaming it touches
  the radar and the base-creation chain and is left as its own unit. → done in part 55.

- [x] part 55, the naming fix that unit 54 left open: the 0x80..0x89 id space is a **world region**.
  `Geoscape_DrawGlobeHover` prints `TEXTS[905 + tile]` as the tile's own name (0x67914) and the ten
  records TEXTS[1033..1042] name them: Северная Америка, Южная Америка, Европа, Азия, Австралия,
  Африка, Антарктика, Арктика, Канада, "Здесь нельзя строить". 0xFF is the ocean tile. Renamed in the
  port and in the IDB: `Funding_AddRaceBonus` → `Funding_AddRegionBonus`, `aNewBaseCostByRace` →
  `aNewBaseCostByRegion`, `bPendingBaseKind` → `bPendingRegionId`, `BaseRec.bKind` → `bRegionId`,
  `PUFO_RACE_ID_*` → `PUFO_REGION_ID_*`, plus a `PufoRegionId` enum on both sides.
  **The counter table was misread.** `BaseFac_FormatCostLabel` and `Base_CreateNamedEntry` load
  `0x1864DD + region` (0x6A46C, 0x66A64) and store at `0x18655D + region - 0x80` (0x6A488/0x6A4A0) —
  the same byte. The table is `g_anRegionBuildCount @ 0x18655D`, nine signed counters for regions
  0x80..0x88 (created in the IDB); 0x1864DD is only the −0x80 bias anchor. The label is the region's
  name plus its counter, e.g. "Северная Америка-0".
  **Two wrong records corrected.** The bugs.tsv row claiming kinds 15/16 overflow a 15-byte
  `abBaseKindTable` was a consequence of reading the index as a facility kind — removed. The port's
  17-byte `abKindTable` buffer, indexed by the raw region id, was an out-of-bounds read for every
  real region (index 128..137); it is replaced by the nine-counter model with `RegionBuildCountAt()`
  guarding the range, and the world's duplicate `abKindTable[17]` / `abFlags18655D[10]` pair is now
  one buffer.
  **The starting base's quirk kept.** `Game_StartNew` passes region 0 (`MOV R3,#0` at 0x94950), so the
  original reads the byte at 0x1864DD — the tail of `szScratch186488` — for `BaseRec.bUnk16`; the port
  reads 0 there, the same value in the shipped image. Region 0x89 has no counter of its own: the
  original's store lands on 0x186566 (`bUiFrameStyle`), which is also the tenth byte StartNew sweeps.
  4 more checks (11297 total), 5 renames on record (298).

- [x] part 56, the funding screen's own UI: `FundingScreen_Toggle (0x5abe4, 1560 B)`,
  `FundingScreen_Open (0x5b560, 60 B)`, `FundingScreen_Draw (0x5b1fc, 868 B)` and
  `FundingScreen_DrawExtended (0x5b59c, 2780 B)`.
  **The slot model confirmed from the other side.** The names panel copies the five country names in
  SLOT order — TEXTS[1302] США, TEXTS[1305] Австралия, TEXTS[1303] Евро Альянс, TEXTS[1304]
  Азиатский Альянс, TEXTS[1306] Африка (0x5AF5C..0x5B038) — which pins slot 0 USA, 1 Australia,
  2 Euro, 3 Asia, 4 Africa independently of the recompute's region chain. The report rows use the
  permutation {0, 2, 3, 1, 4} with titles TEXTS[1302 + row], so the panel order and the report order
  differ, and the test shows both.
  **The "$" line is rebuilt, not left over.** `nFundDisplayTotal @ 0x159604` is zeroed on open
  (0x5AC0C) and summed from the five possibly-zeroed bars (0x5B124..0x5B144); `FundingScreen_Draw`
  prints it and never recomputes it. A bar that is not positive zeroes its bar AND its arrow
  (0x5B0EC..0x5B11C) — the recompute's rule applied again at draw time.
  **The commentary table.** TEXTS[1307..1333], selected by [arrow + 2][group][line]; group 1 is rows
  1 and 2 (Euro and Asia, 0x5BA34, 0x5BB80, 0x5BD70, 0x5BEA4). Only the two negative arrows have a
  red variant of the third line (0x5BF60, 0x5BE34), and the red flag is `(arrow < 0) && bar[slot] < 0`
  (0x5B810..0x5B820) — the BAR decides, not the share, which the test proves by flipping only the bar
  and watching the tail record move 1318 → 1317. Below month 3 every negative arrow draws as 0
  (0x5B8CC..0x5B8F0).
  **Real-surface tests.** Four full-frame comparisons — the funding screen itself, and the report in
  three states (plain, Euro in the red, month 1) — each against a reference render of the expected
  TEXTS records at the original's positions and colours, plus the shared rails, the close-control pad
  following (224, 1), and the fact that `FundingScreen_DrawExtended` has no open gate and still draws
  after the screen is closed.
  IDB: `unk_1595EC` → `bFundScreenOpen`, `unk_159604` → `nFundDisplayTotal`, `dword_1595E4` /
  `dword_1595E0` / `dword_1595E8` → `pFundPanelNames` / `pFundPanelBars` / `pFundPanelArrows`, and
  function comments on all four. 103 more checks (11400 total), 5 renames on record (303).

- [x] part 57, `Inv_CloseAndCommit (0x15a48, 1268 B)` — **the `tact-ui` subsystem is now complete
  (52 of 52 functions, 43,764 of 43,764 bytes)**.
  **The loadout snapshot.** While the commit byte at Map+1 is set, every XCom unit that is *not*
  large gets its inventory written into two per-slot arrays: `Soldier+0x3C2` (u16[25]) takes the slot
  item's object sort key (`LDRH [pObj,#0xC8]`) and `Soldier+0x3F4` (u8[25]) takes the **low byte of
  `TactItem.nIndex`** — the ammo type the weapon has loaded, not a round count. An empty slot, a
  two-handed object, or an item whose `nAmmo` is 0 or 255 stores 0 in the ammo half. The walk covers
  24 slots in the jumptable's order (0x15AB0): the eight pocket items, the two hands, backpack[0..4]
  and **backpack[7]**, then belt[0..7] — backpack slots 5 and 6 and belt slot 8 have no case at all,
  so the original never looks at them. These are the same two arrays `Tact_EquipTeamFromCraft` reads
  back when it re-equips a unit from the craft.
  **The clip merge.** Whichever hand holds an object with `bTwoHanded == 0 && bListPri == 1` is the
  weapon; the other hand's item is the clip. The weapon's three `anAmmoTypeId` entries are resolved
  through `Map.apObjects` (Map+0x108) and compared with the clip's object. On a match the weapon takes
  the type id into `nIndex` and the clip's `nAmmo`, the clip leaves the hand, and its `TactItem` is
  dropped from the pool — hole closed with the **last** entry, `operator delete`, then the count
  decremented **unconditionally**, so an item that was never in the pool still costs one entry. The
  left-hand case stops at the first match (0x15D20); the right-hand case re-tests all three ids and a
  later match overwrites the earlier one (0x15E80..0x15E88) — the test pins both.
  64 new checks (11464 total). IDB: function comment at 0x15a48. The two Soldier arrays stay
  `auUnk962` / `abUnk1012` in the IDB — the MCP `rename` tool only takes globals, and struct members
  are not globals.

- [x] part 58, the four post-battle tallies: `Tact_TallyBattleStats (0x3348c, 756 B)`,
  `Tact_TallyUnitResults (0x33780, 1136 B)`, `Tact_TallyItemRecovery (0x33c80, 2100 B)` and
  `Tact_TallyCraftRecovery (0x344b4, 556 B)` — the mission debrief's whole data side.
  **Every counter got its meaning from the debrief labels.** `Tact_CheckBattleEnd` (0x34fbc, not
  ported) prints each one under a TEXTS record built as `LDR R0, =0xB38D0` + `MOV R3,#hi; ORR R3,#lo`
  = `aTextRecs + 205*n`. That is the in-memory stride (`PUFO_TEXT_REC_STRIDE`, five bytes of slack per
  record; the on-disk record is 200 bytes) and it names all fourteen counters: 162 operatives killed,
  163 tanks destroyed, 164/165 civilians killed/rescued, 166 aliens killed, 171 alloys, 172 Elerium,
  173 navigation, 174 power source, 175 reproduction, 176 food, 177 entertainment, 178 surgery, 198
  examination room. The IDB's `pnSurvived` / `pnKilled` / `pnMIA` were wrong — they described type
  ranges, and all three feed *loss* lines.
  **`Mc1Rec+0x1E` is the tile facility byte.** It is read exactly once in the binary (0x33618), which
  is why it had stayed a gap. Named `bFacilityKind` with a `TileFacilityKind` enum, cross-checked
  against the byte's distribution over the 1,163 records of the 27 shipped `.MC1` sets: 2 power
  source (1 record, U_WALL02), 3 navigation (6), 5 reproduction (4, U_DISEC2), 7 entertainment (4,
  U_PODS), 8 surgery (6), 9 examination (4, U_OPER2), 10 alloys (133). Elerium is not a room count —
  it adds the seeded `rand(0x28)+10` roll once per kind-2 sprite — and food is written only by the
  tail, gated on `nTerrainVariant > 3 || nMissionType == MissionType_AlienBase` (0x3373C). The sixth
  counter has no writer and no reader anywhere; it is kept as `nUnused`, and the test asserts the
  seven real counters total 11 out of 15 placed sprites instead of testing the dead one.
  **The alien type/rank slot table came out of the IDB's own names.** `Tact_TallyItemRecovery` credits
  one `apObjects` slot per `(nUnitType, bRankType)` out of the Map slot block; 37 of those slots were
  already named `Map_apObjectsNN`, so slot = (addr − 0x1139B8 − 0x108)/4 and each one is that race's
  soldier record in OBJECTS.DAT. That pins `UnitType` (9 Sectoid … 0x15 Sectopod) and `UnitRank`
  (2 soldier, 3 navigator, 4 leader, 5 engineer, 7 medic, 8 commander) from both sides — the debrief
  civilian/alien split confirms 0x10/0x11 independently. The Muton row has no rank 4 or 8 because the
  shipped Muton block (92..94) has no leader or commander at all. 37 `unk_113Bxx` → `Map_apObjectsNN`
  renames.
  **Two box conventions in one function.** The cell walk is exclusive at the top (0x33EB8, 0x33EC4 are
  CMP/BLT) while the unit, alien and HWP passes use the same four numbers inclusively.
  `Tact_TallyCraftRecovery`'s two blockers from an earlier session are resolved: the gate is
  `unk_128A8C || Map_bAltOutcomePath` at 0x34554, and the port now owns `SmapSite` / `nSmapSiteCount`,
  so the leftover-recovery branch (`aSmapSites[nSmapSiteCount].auItemQty[obj]`, `bHasLoot = 1`) is
  expressible.
  **Open question recorded, not guessed:** the polarity of `unk_128A8C`. With it clear every operative
  and tank counts as lost and no civilian can be rescued, which reads like "the team was extracted"
  rather than the port's `pbMissionAborted`. All eight xrefs are reads — no writer in the IDB — so the
  name stands and the question is in the parity doc for `Tact_CheckBattleEnd` to settle.
  91 new checks (11555 total), 37 renames on record (340). IDB: new signatures on all four tallies,
  function comments at 0x3348c, 0x33618, 0x33780, 0x33c80, 0x344b4, and 14 debrief-label comments
  inside `Tact_CheckBattleEnd`.

- [x] part 59, the naming fix part 58 left open: `unk_128A8C` @ Map+0x150D4 is the **victory flag**,
  not an abort flag. Renamed in the IDB to `Map_bMissionVictory`, and the port's
  `pbMissionAborted` → `pbMissionVictory` in `pufo_tactend.{h,c}` and `tests/test_tactend.c`.
  **`Tact_CheckBattleEnd` writes it**, which is what settles it: set at 0x350F0 when no enemy is left
  (`Map_nEnemyReserveCount` plus the live non-civilian `apEnemyUnits` entries is zero, and it is not
  an unfinished alien base), cleared at 0x35070 when `Map_nXcomUnitCount` is zero, cleared at 0x351CC
  when a `MissionType_XcomBase` mission is called off. And the debrief reads it as the win/lose
  switch: 0x353C4 copies TEXTS[160] "Victory" into `Map_szMissionCaption` @ Map+0x15094, 0x35400
  copies TEXTS[161] "Defeat", and `TactHud_DrawStatusBanner` (0x1E968) draws the caption green when
  set, red when clear. `Tact_Shutdown` already agreed — loot transferred on a held site when set,
  facility demolished when clear, the craft itself lost only when clear.
  **Why no writer looked to exist:** all eight IDB data xrefs are reads, because every write here
  builds the offset in a register (`MOV R8,#0x15000; ORR R8,R8,#0xD4; STRB R7,[R5,R8]`) — ARM cannot
  encode a 6684-byte displacement in one instruction, so IDA records no data xref. That is a general
  trap for any Map field past +0xFFF, and it is now recorded in the parity notes.
  **Also named:** `Map_szMissionCaption` (was `byte_128A4C`), `Map_szMissionScore` (was
  `byte_128A6A`, the `itoa(debrief score)` the banner draws at 200,260), `Map_nDebriefLine` @
  Map+0x150D0 (the debrief line cursor shared by `pUiPanel0 @ 0x1568E8` and `pUiPanel1 @ 0x1568E4`)
  and `Map_nMissionOutcome` @ Map+0x1A1C (written 1/0/2 for lost/won/aborted and **never read** — a
  dead store). 15 line comments and a function comment in `Tact_CheckBattleEnd`.
  **The debrief labels are TEXTS records after all**, at `aTextRecs + 205*n`: the label block
  0xBB8F0..0xBD75E is exactly stride 205 (160 Victory, 161 Defeat, 162..166, 167 live aliens
  recovered, 168 Craft Lost, 169 Base Destroyed, 170 Base Lost, 171..178, 198 Examination Room). IDA
  types none of them as strings, which is why `get_string` returns nothing there.
  Coverage unchanged (889/1103); 5 renames on record (345).

- [x] part 60, the post-mission soldier cleanup: `Tact_ClearSoldierBattleFlags (0x346e0, 2268 B)` and
  its callee `Soldier_PromoteByTypeScores (0x47280, 1096 B)`.
  **The rank ladder is five rungs, top-down, one step per rung per call** — 4→5 (gate `count(5) == 0`,
  threshold 375), 3→4 (`count(4) < 2`, 270), 2→3 (`count(3) < 2`, 125), 1→2 (`count(2) < count(1)/3`,
  that many attempts, 90), 0→1 (no gate, score `> 45`). Mission score is `5 * (uKills + 2 * uMissions)`.
  The engineer rung tests `== 0` where the two above it test `< 2`, so one rank-5 soldier closes it
  while one rank-4 soldier does not close the leader rung. The 1→2 rung is the odd one out: its two
  scans test `bRankType` alone — no alive test, no large-unit test — so a **dead** rank-1 soldier with
  the best score is the one that climbs, and it seeds its running score from `nPromoteBestPick`
  (0x47548, 0x47624), the previous rung's 0/1 pick rather than a score.
  **A real port bug found while testing the stat refresh.** The refresh is
  `auStat[k] = (u16)(g_aClassStatGrowth[9*nClass + j] * rate * uMissions + auStatBase[b])`, and *b is
  not k*. The ARM operand offsets say so: the j=8 rule reads `auStatBase+0x10` (index 8) and stores
  `auStat+0x12` (index 9); the j=7 rule reads `0xF2` (index 7) and stores `0xB2` (index 8). That is the
  auStatBase compaction Unit_ResetBattleState writes (auStatBase[7]=auStat[8], auStatBase[8]=auStat[9]).
  The port used the stat index for both, so the j=8 rule read `auStatBase[9]` — past the end of a
  nine-entry array, into the bytes before `uKills` (0x0FA). Fixed by giving `StatGrowthRule` a base
  index. Class column 6 is never read and auStat[6]/auStat[7] are never recomputed.
  **`g_aClassStatGrowth @ 0xAA930` (was `byte_AA930`)** is 6 classes × 9 weights of 1..3,
  `Tact_ClearSoldierBattleFlags` is its only reader in the binary, and audit_tables.py now byte-checks
  all 54 entries against the shipped image: exact match.
  **The promotion report** writes `Soldier.szName` into `pUiPanel2 @ 0x1568E0` and
  `aszTextE[20 * bRankType]` into `pUiPanel3 @ 0x1568DC` (0x34E34, 0x34E74), bumps both `nLineCount`,
  and sets `Map_bPromotionReport @ 0x1153D8` (0x34EDC). `aszTextE @ 0xB2200` is a loaded TEXTS block of
  six 20-byte entries — its text is not in the image, so the port takes the names through the env and
  the test checks the `20 * bRankType` indexing rather than the wording. Step 1 clears `bRankDirty`
  before the ladder runs, so a flag the caller left behind is not a report row, and the `bInZone` gate
  applies to the report too: a promoted soldier outside the zone changes rank and gets no row.
  106 new checks (11661 total), 4 renames on record (349), coverage 891/1103 (80.8%), 411568/598792
  (68.7%). IDB: `Map_bPromotionReport`, `g_aClassStatGrowth` (typed `unsigned __int8[54]`),
  `nPromoteBestScore`, `nPromoteBestPick`, and the function comment at 0x346e0 rewritten with the
  (j, k, b, rate) quadruples.

- [x] part 61, the battle-end and debrief emitter: `Tact_CheckBattleEnd (0x34fbc, 5748 B)`.
  **`bAbort` is `Map_bAltOutcomePath`.** The very first store, at 0x34FE4, writes the caller's abort
  request into Map+0x1A21 — the same flag `Tact_TallyUnitResults` gates its zone test on. So an abort
  does not just add a line, it changes *who counts as lost*: the marking loop at 0x3531C takes
  `bInZone == 0` soldiers instead of the tally's own lost set.
  **Four exits before the debrief.** Already-ended returns (0x34FEC). The victory-tile scan
  (`Tact_ScanVictoryTiles`) shuts down and opens the end screen (0x35004..0x35030). An abort sets
  `Map_nMissionOutcome = 2` and jumps **past both sides of the win/lose decision** (0x35104) — an abort
  leaves `Map_bMissionVictory` exactly as it was, which is why the port's abort test asserts "untouched"
  rather than "cleared". The two count tests: no Xcom unit → lost (0x35070); no live non-civilian enemy
  and no reserve → won (0x350F0), and only when it is not an unfinished alien base, because the gate is
  `nMissionType != 4 || Map_bSpawnAlt == 0`. Civilians are skipped by `nUnitType` 0x10/0x11 (0x350A0).
  **The fourth exit (0x35124..0x35198)** is the failed base defence: `(type == 4 || type == 9) &&
  bSpawnAlt`, or `abFacilityGrid[0x618] == 1 && type == 3`, with `Map_bMissionVictory == 0`, gives
  `Tact_Shutdown(1) + EndScreen_Init(0)`. The facility byte is read with **`LDRSB`** at 0x35160, so -1
  is not "set" — a new trap in HANDOFF §8.
  **19 debrief lines, three meta shapes.** Loss (255,0,0,90,0,0), gain (0,255,0,0,90,0) and the loot
  shape (0,255,255,0,90,90). TEXTS[198] Examination is the one line whose value column and score term
  differ: the column shows the room count (0x36218), the score pays 10 per room (0x36230). Only one of
  TEXTS[168] Craft Lost / [169] Base Destroyed / [170] Base Lost can appear — the abort of a
  `MissionType_XcomBase` clears the victory flag *and* the local abort copy (0x351CC, 0x351D0) so the
  base gets -700 instead of -400.
  **The loot commit (0x364A0..0x36510)** writes each count into `aSmapSites[nSmapSiteCount].auItemQty`
  and adds it to `apObjects[obj]->uRecovCount` (ObjectRec+0xCC) for obj 106/105/109/107/111/110/143/142/108,
  then commits the site (`bHasLoot = 1`, `bFlag0 = 8`, `bFlag1` from the home base's `bUnk4`,
  `nSmapSiteCount++`). Both debrief panels' `nStyle` take the line count (0x36514, 0x36524) — that is
  what makes the debrief draw exactly the rows that were emitted. The score lands in `g_nFundPending`
  (0x365B0) and as text in `Map_szMissionScore`. **The Mars tail (0x36594..0x365F8)** restarts a finished
  Mars mission in place as an alien base: type 4, mode 2, variant/layers/unk15115 cleared,
  `Tact_InitMission(0)`, `Map_bFrameScratch = 1` last.
  130 new checks (11791 total), 3 renames on record (352), coverage 892/1103 (80.9%), 417316/598792
  (69.7%). IDB: `Map_bEnemyTurnActive @ 0x113A13`, `Map_nBannerFadeTicks @ 0x113A1C`,
  `pTactCtx272 @ 0x157180`, comments at 0x113A13, 0x113A1C, 0x157180, 0x3618C and 0x364A0.
  **Naming fixed on the way:** `Tact_ScanVictoryTiles`' parameters were `bCraftTileA0NonZero` /
  `nCraftTileX` / `nCraftTileY`, which read as though the 2x2 probe started at the deployed craft; the
  three addresses it really reads are `Map_bSpawnAlt`, `Map_nSpawnTileX`, `Map_nSpawnTileY`.

- [x] part 62, the fall processor: `Unit_ApplyFallDamage (0x4a7b0, 1000 B)` + `Tact_ProcessFallAction (0x52adc, 5716 B)`.
  **Fall damage is soft-float, not fixed point.** `damage = (int)((float)(nZ - nLayer - 1) * (float)auStat[1] * 0.4f)`
  — the constant 1053609165 is `0x3ECCCCCD`, the IEEE-754 single **0.4f**, and `_itos` / `_muls` / `_stoi`
  are coredll's soft-float helpers. `nZ - nLayer - 1` counts **floors**, not layers, so a one-layer step is
  zero damage; both descent paths test the unit's own layer first, so the clamp at 0x4AA0C is unreachable.
  **A large unit descends only when all three footprint cells are open** (0x4A91C ANDs the first two tests
  and routes the third through the shared LABEL_14), and the landing sweep reuses the footprint cells the
  loop last computed, so they sit on the stopping layer. A small unit landing ON a large one dies with its
  HP untouched (0x4AAE4); a large occupant is HP-zeroed first (0x4AAFC).
  **`_rt_sdiv` takes R0 = divisor, R1 = dividend.** Re-proven here at 0x1B118 (`MOVS LR,R1` tests the
  remainder of `nTileY % 2`), and it decides the psi gate: `ratio = max(1600, victimProduct) / actorProduct`,
  so a weaker attacker **cannot fail** (ratio 1 or less skips the roll); above 1 the roll must land at or
  below `100 / ratio` out of 150. Morale left behind is `actorProduct / -150 - <600|1000> / reaction + morale`,
  clamped at zero, byte-truncated; above 50 the victim holds, at or below 50 mode 5 panics and mode 10 stuns.
  **The label graph is not a straight line.** LABEL_101 (the message-only exit, 0x5359C) and the two
  "morale survived" exits (0x52D48, 0x53620) jump straight to the tail LABEL_186, skipping LABEL_111's
  `Unit_UpdateVisibility(pActor)`. The port carries that as an explicit flag. `Map_bTickHold` skips the
  whole tail, cursor included.
  **The medikit sort key is tested before modes 10 and 13** (0x53058), so a medikit carried by a psi or
  alien-melee action heals instead of attacking.
  **Original bugs found (7 ledger rows).** `auStat[4]` is written twice at 0x531EC/0x531F4 and `auStat[5]`
  is never loaded, so the transformed victim keeps its old stat 5. The attacker-based `uTu` at 0x53194 is
  overwritten by `Unit_ResetBattleState` (0x49994). The chryssalid kill test uses the full half-HP figure
  while the damage dealt is half of it again (0x532D4). The door-span trace's two `bFlags29` writes both
  index with the entry's own `nZ`, so they hit one cell (0x53A54). The proximity marker tests validity at
  layer 0 and writes at the landing layer (0x54058). `Unit_bIsOperative` is called with its result dropped
  (0x4AB04). No fall cell access has a bounds test at all (0x4A820).
  **`Unit_ResetBattleState` clears `pCell`** (0x498E0), which is why the transform saves the cell into
  `pStepFromCell` first and places from it — the port's first version dereferenced the NULL and crashed.
  189 new checks (11980 total), 11 renames on record (363), coverage 896/1103 (81.2%), 426312/598792 (71.2%).
  IDB: `Map_bEnemySpotted @ 0x118800`, `Map_apXcomUnits @ 0x128B0C`, `Map_apEnemyUnits @ 0x128B84`,
  `Map_bFallDamagePending @ 0x113A0A`, `Map_bThrowPending @ 0x127A25`, `Map_nMeleeCellX/Y/Layer @
  0x1279F8/0x1279FC/0x127A00`, `Map_dThrowX/Y/Layer @ 0x1278A8/0x1278F8/0x127948` (typed `double`),
  comments at 0x52ADC, 0x4A7B0, 0x531EC, 0x53194, 0x532D4, 0x53A54, 0x54058.
  **Naming fixed on the way:** the three `PUFO_SPECIAL_SORT_*` values moved from the tactical HUD header to
  `game/pufo_objectrec.h`, next to the `uSortKey` field they name, because two subsystems now branch on them.

- [x] part 63, the fire-phase processor: `Tact_ProcessFireAction (0x50adc, 8192 B)` plus the two Map writes
  the phase reads but nothing in the port produced yet (`Map_dTravelSave` and the aim sample arrays).
  **The phase has no state of its own — the Map block is its register file.** `Map_dTravel @ 0x14020` is this
  FSM's own counter and the aim setup zeroes it (`Tact_ComputeAimVector` 0x2f9c0); `Map_dTravelSave @ 0x14038`
  is the distance the arc *walk* took (written at 0x302D0 and 0x323BC, never by the FSM). A shot lands when
  travel passes travelSave — that is the whole flight model, and it is why the port had to extend
  `AimState`/`Tact_TraceProjectileArc` before the fire phase could be tested at all.
  **The samples are measured from the muzzle, not from the head.** Each of the ten `Map_adMtX/Y/Z` samples sits
  at `dBack = dTravel - i * 1.41` from `Map_nAimOriginX/Y/Z`; a sample whose `dBack` is not positive is parked
  at -1000.0 on all three axes. At travel 10 the first eight samples are still inside the muzzle cell, which is
  the opposite of what a "one sample per step" reading suggests.
  **Two conversion forms for the same height, and they disagree.** The shot loop uses `(int)(z * (1/12))`
  (0x51264); the three resolve loops use `(int)z / 12` (0x51E5C, 0x52124, 0x527B8). 1/12 is not exact in
  binary, so at a height that is an exact multiple of 12 the two land a layer apart — and the muzzle heights
  the aim setup builds are `12*nZ + 6`, so the case is reachable. The port keeps both forms in separate helpers
  rather than "fixing" one into the other.
  **`Map_bFireHitResult @ 0x1406C` is the arc walk's verdict handed to the fire phase.** `Tact_TraceProjectileArc`
  clears it at 0x2FC54 on every call and sets it to 1 at 0x3010C in the branch that also clears
  `Map.bFireStatus1` and ends the walk — the branch taken when the walked sample leaves the map. The fire phase
  reads it at five places and never writes it, so a shot whose arc ran off the map resolves on its first pass
  instead of stamping the muzzle layer. **This unit's own first draft had it wrong:** the field was named
  `Map_bFireInFlight`, recorded as "written nowhere in the whole image", and a ledger row was written for that
  claim. Both writes are register-built (`MOV R11,#0x14000; ORR R11,#0x6C; STRB R3,[R0,R11]!` at 0x2FC54, and a
  stack-held pointer at 0x3010C), so `xrefs_to` finds neither — the exact blind spot HANDOFF §8 already warns
  about. The IDB name, the ledger row and the port's field are corrected here.
  **cmd 0xE's two paths.** The shot keeps flying unless `dTravel > dTravelSave`, or the weapon's sort key is one
  of the two multi-target keys (13, 35) and the burst still owes shots while `dTravel >= dTrajBound`. Inside the
  resolve path, mode 7 resolves only when the arc is exhausted or the burst is spent, and it is the only path
  that touches `nStepZDelta` (0x51F5C: `(x & ~0xFF) | 1`). The 0x20 trace mark is cleared off the OLD sample
  cell before the new one is computed — the order matters, and the port had it backwards in the first draft.
  **cmd 1's eight draw-offset ladders** are built from `nHalf = (t<=3 ? t+1 : t-7)` and `nNeg = (t<=3 ? -1-t :
  7-t)`, truncated to `i8`, with the final term adding or subtracting `nStepFrameDiv * t` on Y; TU is spent on
  even ticks only, the unit is re-placed at tick 4, and the kneel/stand toggle runs only at tick 4 and only for
  unit types whose `g_aUnitTypeFlag0` entry is 1, keyed on whether the destination cell has a floor sprite.
  **Original bugs found (7 ledger rows).** The throw's in-bounds test uses `160 * Map_nMapDimX` for Y as well
  as X (0x52718) — `Map_nMapDimY` is never read there. cmd 0x12 ends its phase on the phase *counter* while cmd
  0x10 ends on the phase *tick*, although both jump to the same LABEL_155 (0x52ABC vs 0x529C4). Three sites
  dereference without a test: the grid slot behind the kneel toggle (0x50E60), the door case's cell and its
  wall-sprite pointer (0x51058), and `pWeapon->pObj->uSortKey` in the landing test (0x5171C). Plus the two
  conversion forms above.
  **`Map_adMtX/Y/Z @ 0x13EE8/0x13F38/0x13F88` are the same arrays the throw calls `Map_dThrowX/Y/Layer` at slot
  1** (0x13EF0/0x13F40/0x13F90) — the earlier ledger rows had those three at 0x140F0/0x14140/0x14190, which is
  wrong by 0x200 and is corrected here.
  150 new checks (12130 total), 20 renames on record (380 per `audit_names.py`), 7 ledger rows (136), coverage
  898/1103 (81.4%), 435608/598792 (72.7%). IDB: `Map_nAimOriginX/Y/Z`, `Map_nAimTargetZ`, `Map_dTravel`,
  `Map_dAngleYaw`, `Map_dAnglePitch`, `Map_dTravelSave`, `Map_nFireArg5`, `Map_pFireWeapon`,
  `Map_bFireHitResult`, `Map_dArcDenom`, `Map_dTrajBound`, `Map_adMtX/Y/Z`, `Map_nBurstShots` renamed and typed;
  `Map_bThrowPending` renamed to `Map_bTrajHitStop` to match the Map struct member the decompiler already
  showed; comments at 0x127A24, 0x127A30, 0x127A38, 0x14038, 0x1406C, 0x1406D, 0x1139E0, 0x1139B8, 0x2FC54,
  0x3010C, 0x31A18 and a function comment at 0x50adc. **`GameMap`'s `Soldier *apTraceExclude[2]` at
  0x1406C/0x14070 was a wrong model and is removed:** `Map_TraceVoxelOccupancy @ 0x3242C` skips
  `Map.pSelUnit @ 0x15334` and `Map.pThrowTargetUnit @ 0x150F0`, while 0x1406C is a byte and 0x14070 is
  `Map.pArrowThrownItem`, a `TactItem *` written by `Tact_SimulateThrowArc` at 0x319FC.
  **Naming fixed on the way:** the two multi-target weapon kinds got `PUFO_SORT_KEY_MULTI_TARGET_A/B` in
  `game/pufo_objectrec.h` — four sites compare the raw 13 and 35, and none of them says which weapons they are,
  so the names say what the code does instead of guessing.

- [x] **part 64, the queue walker: `Tact_TickActiveUnit (0x54130, 1868 B)`** — the link between the three
  phase processors and `GameTick_OnTimer`, and the last thing `GameTickHooks.pfnTickActiveUnit` was waiting for.
  **`bFlag0` picks the processor, it does not gate one of them.** Set runs the fire phase, clear runs the move
  phase — alternatives, never both. That is why the move processor's prologue raising `bFlag0` matters: it hands
  the next tick to the fire phase.
  **The camera follow is per command, and the order of the two halves differs.** Commands 0/1/8 follow
  `pUnit->pCell` and adopt the tracked unit *before* the focus; command 0xE follows the cell the last aim sample
  fell into and adopts `Map_apXcomUnits[0]` *after*; command 0x12 is gated on `pTarget->pCell`'s
  `bFlags44` and adopts `pTarget` *after*. With nothing tracked the camera always follows; with a unit tracked a
  shot re-centres only when `(int)Map_dTravel % 60 == 0` (0x54750).
  **`TactState+0x111C8` and `+0x111E2` are one pair, and neither was named.** `+0x111E2` was `bUnk70114`,
  `+0x111C8` was the first 12 bytes of `gap_70088`. `EnemyAi_TryFireOrWander (0x4ABC8, unported)` stores the acting
  unit at +0x111C8 (0x4AC04) and raises +0x111E2 (0x4AC1C) together — **unconditionally, before** the branch at
  0x4ACA0 that picks between its random fire burst and its weapon-drop-and-wander. So the flag is "an AI action is
  in progress", not "a wander is running", and that is the reading its other two sites need: the move processor
  suppresses the "not enough time" record while it is set, the fire processor skips the enemy-spotted reaction
  while it is set. It is cleared on the wander path's failure (0x4B024) — the fire path never clears it — by
  `Tact_InitMission` and by the walker at 0x544D8, which also zeroes the acting unit's `uTu` through the pointer
  (0x544E0). Found by searching the disassembly for the `#0x1C8` offset, not by an xref — the field has no xref
  either. Now `pAiActionUnit` / `bAiActionActive`; the first draft called them `pAiWanderUnit` / `bAiWanderActive`,
  which was too narrow and is corrected here.
  **The kill credit is collected over `aQueue[0 .. nQueueCursor-1]` measured *after* `Fall_Tail` moved the
  cursor.** So the range is every entry that already ran, not "everything but the last". The accumulator at
  `TactState+0x111D0` is a u16 added to the first shooter with `nUnitType <= 8` that is operative, then zeroed as
  a dword (0x54790). The first test for this asserted the wrong bound and was rewritten as a bound test.
  **Hex-Rays double-counts the queue index.** ARM computes `R0 = pState + 56*nQueueCursor` and then `[R0,#0x18]`;
  Hex-Rays renders that as `v5->aQueue[offsetof(TactState,bFlag0)]`, adding the 0x18 twice. Recorded as an IDB
  comment at 0x54258 so the next reader does not re-derive a 0x18-entry stride.
  **Original bugs found (6 ledger rows).** Five unguarded dereferences — `pUnit->pCell->bFlags44` (0x54558),
  `pTarget->pCell` (0x54288), the AI action-unit pointer (0x544D8), `Map_pSelUnit` (0x54208), `pScanUnit`
  (0x547E0) — and one asymmetry: the move path's command-0xE camera follow indexes the cell grid straight off the
  raw aim sample with no bounds test (0x54390), while the fire path calls `Map_IsValidCell` first at 0x54654.
  96 new checks (12226 total), 2 renames on record (380 per `audit_names.py`), 6 ledger rows (142), coverage
  **unchanged at 898/1103 (81.4%) and 435608/598792 (72.7%)** — `0x54130` was already credited by the comments
  the move and fall processors carry, so porting it moves no coverage number, only the `audit_names` flag list
  (3 → 2). A transient 899/436816 seen mid-unit was inflation from citing `EnemyAi_TryFireOrWander @ 0x4ABC8`
  (1208 B) in a comment; that citation is now the `Name (0xADDR, unported)` form and is not counted, which is
  exactly the metric weakness already recorded under "Separate issues". IDB: `TactState` redeclared with
  `pAiActionUnit` (`Soldier *` @ +0x111C8), `uKillCredit` (u16 @ +0x111D0) and `bAiActionActive` (@ +0x111E2,
  was `bUnk70114`); comments at 0x544D8, 0x4AC04, 0x4AC1C, 0x4B024, 0x54788, 0x54750, 0x54258 and a function
  comment at 0x54130. **`GameMap`'s `apTraceExclude` pair is gone for good:** the two exclusion sources are
  `Map.pSelUnit @ 0x15334` and `Map.pThrowTargetUnit @ 0x150F0`, and `Map_BuildMoveActionCtx` now wires them.
  **Deviation, recorded not hidden:** no app-layer `TactTickCtx` yet, so `GameTick_OnTimer`'s seam stays unfilled.

- [x] **part 65, the low-morale AI: `EnemyAi_TryFireOrWander (0x4abc8, 1208 B)`** — the function that writes the
  `pAiActionUnit` / `bAiActionActive` pair part 64 named.
  **A large unit does nothing at all** — 0x4abdc jumps to the epilogue, so it skips the RNG, the `pSelWeapon`
  clear, the queue and the status message.
  **The AI-action pair is raised before the path is chosen** (0x4AC04, 0x4AC1C), which is the direct proof that
  the flag means "an AI action is in progress": a unit that ends up wandering still raises it and still leaves
  its chosen weapon in `Map.pSelWeapon`.
  **Hex-Rays actively misleads on the burst's target rule.** When the drawn column is the shooter's own, the
  drawn row is replaced by the shooter's row and then 0x4ada4 `BNE loc_4ACE0` jumps back to the *draw*, not to
  the shot decrement at 0x4ae0c — so a same-column draw spends no shot, and a shooter on row 0 fires at its own
  cell. The pseudocode's fall-through-to-decrement would fire zero shots where the ARM fires one.
  `TestSameColumnRedraw` pins it: one scripted shot, a rejected draw, the shot still lands, 7 draws consumed.
  **Two floor walks in one function, different depths.** The weapon-drop descent (`SUB R7,#1; CMP R7,#0; BGT`,
  0x4ae84-0x4ae90) tests layers nZ..1 and reaches layer 0 untested; the wander descent (`SUBS / BPL`,
  0x4afcc-0x4afd0) tests nZ..0 inclusive. Recorded as a bug row rather than harmonised.
  **The burst reads the same two action tables the UI writes**: `nTuCost = Map.anActionCost[mode]` (Map+0x4E4C)
  and `nArg = Map.anActionEnabled[mode]` (Map+0x4E7C), the pair `Ui_FillActionCosts` fills and
  `Tact_TryFireMode4`/`Input_OnPointerUp` read. The AI ignores the enqueue's result — an unaffordable burst
  queues nothing and the latch clear, queue kick and name push still happen.
  **`unk_104078` is another never-written scratch buffer**: the AI's status message is `name + unk_104078`, and
  that buffer is all zeros with its only three references in the whole binary being this one call site.
  **Original bugs found (7 ledger rows).** Four unguarded dereferences (the two floor walks' cell loads at
  0x4ae74 and 0x4afbc, the `MapCell_ListInsertByPri` cell at 0x4aec4, the unbounded `byte_A7B80` index at
  0x4af8c) plus the floor-depth asymmetry (0x4ae8c), the same-column redraw (0x4ada4) and `Map.pSelWeapon`
  left pointing at an item the wander path just dropped (0x4ac90).
  79 new checks (12305 total), 7 ledger rows (149), coverage 899/1103 (81.5%), 436816/598792 (72.9%). IDB:
  `Map_nAiFireX/Y/Layer` named at 0x128A20/0x128A24/0x128A28 (all six/eight/four xrefs are inside this one
  function, so they are private scratch, not a Map contract); comments at 0x104078, 0x4AC04, 0x4AC1C, 0x4ADA4
  (the Hex-Rays correction), 0x4B024. **Two gates caught my own mistake:** the first draft cited
  `Unit_PathToCell @ 0x2c2a8` and `Unit_CanStepTo @ 0x27bb8` in a comment, which credited 16,976 bytes of
  unported code and produced a NEGATED citation; both are now the `Name (0xADDR, unported)` form and the
  numbers went back to the true ones.
  **Deviation:** `Unit_PathToCell` (0x2c2a8) is unported — it needs `Unit_CanStepTo` (0x27bb8) — so it is a hook
  on `EnemyAiActCtx`, as are `Ui_FillActionCosts` and `StatusMsg_Push`.

- [x] **part 66, the enemy AI driver: `Tact_EnemyAiTick (0x4cbe0, 3440 B)`** — the phase-3 arm of
  `Tact_ProcessActionPhase`, and the caller part 65's function was waiting for.
  **`pTactCtx272+0x100` is a byte, not a pointer.** Hex-Rays types it `Soldier *pSavedFocus` because every read
  is `LOBYTE(pCtx->pSavedFocus)`, but all seven accesses are 0/1 byte stores and tests (0x4CC00, 0x4D688,
  0x4D84C, 0x4D86C, 0x4D878, 0x4D888, 0x4D920) and nothing loads it as an address; the unit the enemy turn
  restores is at +0x104. Renamed `bAiFocusSaved` in the IDB and in `TactCtx272`, with a layout check that the
  field is one byte and `pSavedSelUnit` sits four bytes after it.
  **The tick gate skips without advancing.** `nAiTick > 15` (0x4CC34 `BGT`) leaves the counter alone, so a unit
  past the limit stays past it forever; `bAiDone` is skipped *after* `++nAiTick`. `TestTickUnitGates` pins both.
  **`g_aUnitTypeRecord` column +3 was an unnamed column with exactly one reader** (`LDRB R3,[R3,#3]` at 0x4CDBC,
  the only `+3` read among all 26 xrefs to the table). Its six set rows are Chryssalid, Celacid, both civilians,
  Silacoid and Riper — the units with no weapon to fire — and they are what selects the command-13 melee branch.
  Added as `g_aUnitTypeFlag3`. Re-dumping the table also found a **wrong row in this port's own documentation**:
  `pufo_enemyai.h` had copied flag0's 1s at types 11 and 13 into the `large` row; the binary says 0 there.
  **The neighbour-melee hand-off is an original bug** (0x4d660). A usable multi-target sends the tick to a path
  that builds the whole action around a *different* enemy — `Tact_SelectUnit(neighbour)`, the command-18 enqueue
  and `Map_pSelUnit->uTu >>= 1` (0x4d728..0x4d730) all address the neighbour — while the unit this tick was
  picked for spends no TU, is not marked done, and is picked again next tick. Reproduced, with a test that
  asserts the queue entry's `pUnit` is the neighbour and the actor's TU is untouched.
  **The fire gate is soft-float and charges less than it tests.** `uTu >= anActionCost[mode] * 2/3 + 8.0`
  (0x4d448 `0x3FE5555555555555`, 0x4d458 `0x4020000000000000`) but the entry carries `(int)(cost * 2/3)`: a
  26-cost mode needs 26 TU and charges 17. Pinned at both sides of the boundary.
  **The RMP walk's starting draw is never a candidate** — the index is incremented before use (0x4D8F8) — and
  its distance test uses the *untruncated* sqrt (`_led` against 19.875) where every reach test in the same
  function truncates with `_dtoi`. `TestRmpWalkOrder` asserts the visit order (node 2, 0, 1 for a drawn index
  of 1) rather than just the count.
  **Two unguarded cell reads** (0x4d09c, 0x4ce50): the eight-direction melee scan and the large unit's footprint
  loads index `apCells` with no `Map_IsValidCell` call at all, unlike the facing-delta lookup at 0x4ce8c which
  does call it. The port bounds-checks.
  **`unk_156910` / `unk_156A58` / `unk_156A88` are `TactItem` records, not pointer variables** — each has a
  `TactItem_ctor` thunk (0x37124, 0x37134, 0x37088) and `Tact_InitMission` stores an `apObjects` entry into the
  record's `pObj` at +0. Retyped and named `TactItem_156910` etc. in the IDB; the port's `ObjectRec **ppObj…`
  fields became `TactItem *pItem…`, which is what let the AI hand them to the queue as weapons.
  99 new checks (12404 total), 3 ledger rows (152), coverage 900/1103 (81.6%), 440256/598792 (73.5%) — the
  delta is exactly the function's 3440 bytes. **The gates caught a second inflation of mine:** citing the three
  constructor thunks with `@ 0xADDR` credited 36 bytes of code the port does not own; the citations are now
  prose, and `audit_names` went back to its two known flags.
  **Deviation:** `Unit_PathToCell` (0x2c2a8, unported — it needs `Unit_CanStepTo`, 0x27bb8), `AfterEnemyStep`
  (0x4c4a0, unported), `Ui_FillActionCosts`, `Unit_UpdateVisibility` (ported, but it needs a whole `VisCtx`) and
  the clip lookup inside `TactItem_bTryReloadClip` are hooks on `EnemyAiTickCtx`. The RMP node's role byte is
  passed to a shared role-based predicate instead of through the original's `Soldier *` cast, which would read
  the wrong byte once `Soldier` shifts on Win64.

- [x] **part 67, the enemy-turn bracket: `BeforeEnemyStep (0x4b9b8, 2792 B)`, `Tact_UpdateUnitControl
  (0x4a444, 788 B)`, `AfterEnemyStep (0x4c4a0, 1368 B)`** — the bracket around the enemy turn, both halves
  calling `Tact_UpdateUnitControl` once per unit.
  **The face loop makes six passes over four counters, not six counters.** The two neighbour passes rewrite
  the slot index (`0x4C09C MOV R1, #1`, `0x4C0EC MOV R1, #2`), so the west-neighbour pass charges the cell's
  own `abFaceDamage[wall A]` through `pWallSprA` and the north pass likewise for wall B: a wall between two
  burning cells takes double damage, and nothing in the loop reaches `nPathCost` at +0x14. My first draft
  modelled slots 4 and 5 as separate counters aliasing `nPathCost` and wrote that into `MapCell`'s comment —
  `TestBeforeEnemyStepFaceHeat` caught it, and the IDB comment is corrected.
  **The heat term is `counter + step`, and the store adds it twice.** 0x4C10C/0x4C110 build `R2 = counter +
  20` (or +5), 0x4C15C compares *that* against `Mc1Rec.bStrength`, 0x4C160 zeroes it when the sprite covers
  it, 0x4C1F0 stores `counter + R2`. So a sprite absorbs the hit whenever `bStrength >= counter + step` (255
  always does) and the only counters that ever grow are faces with no sprite at all, growing to
  `2 * counter + step`.
  **The burn run probes against its own axis.** 0x4BC78 multiplies the axis delta by a counter running
  0, -1, -2, so with the west axis it probes EAST and the forward pass starts two cells east of the burning
  cell and walks back over it. `TestBeforeEnemyStepBurnRun` pins the geometry with three layouts that differ
  only in where the wall gap is.
  **`MapCell.abFaceDamage` cannot be reached through the original's byte offset on Win64.** `MapCell`'s four
  pointers widen, so `pCell + 0x10` lands on `pWallSprB`; the first `Cell_FaceDamage` wrote heat into a
  sprite pointer and then dereferenced it. The port now indexes the field, the same widened-offset problem
  `PUFO_SOLDIER_BSTATUS_OFF` already solves for `Soldier`.
  **The countdowns are gated by the burning state**, not just by their own values: state 0 takes neither
  countdown (0x4C038), state 2 skips the face loop but still counts down (0x4C040), and the `0x78` field
  holds at 0xF (0x4C014) — a guard `GameTick_Step` (0x27324) does not have.
  **The recovery arm and the side transfers want opposite polarities** of `bFlags` bit 0x20 (0x4A500 needs it
  set, 0x4A5C8 and 0x4A66C need it clear), so no unit takes both arms in one call.
  **`SideLists` carries its counts by value**, so `Tact_UpdateUnitControl` had to write the removal back
  before the append read the target count — the `Fall_SyncCounts` pattern. Without it the source roster keeps
  a unit it no longer holds.
  109 new checks (12513 total), 12 ledger rows (164), coverage 903/1103 (81.9%), 445204/598792 (74.4%) — the
  delta is exactly the three functions' 4948 bytes.
  **Deviation:** the nine callees the bracket drives (`MapCell_TickProximityItems`, `Map_RefreshCellMeta`,
  `UnitList_ForEachRefresh`, `Unit_UpdateVisibility`, `Game_RandU16`, `Tact_SelectUnit`, `StatusMsg_Push`,
  `EnemyAi_TryFireOrWander`, `Rmp_PickSpawnForUnit`) are seams on `EnemyStepCtx`, and nothing in the app
  layer builds one yet. The port bounds-checks the burn walk's neighbour cell reads, truncates the 52-byte
  status buffer, and skips both burden terms when the divisor is zero.

- [x] **part 68, the tactical map click and the mode-6 confirm: `Tact_HandleMapClick (0x1512c, 1104 B)`,
  `TactAction_ConfirmMeleeMode6 (0x17bb8, 412 B)`** — the last two addresses `audit_names` still flagged, so
  **every credited address in the port now has a function defined for it (0 flags, 0 bytes outstanding).**
  **The cursor cell is one layer below `nLayerScratch`.** 0x1519C indexes with base `0x11AF8`, which is
  `apCells @ 0x15338` minus one 14400-byte layer, so the cell is `apCells[nLayerScratch-1][nCursorY][nCursorX]`
  — the same convention `Input_OnSecondary` already uses. `MapCursor_IsValid` checks x and y only, so an
  out-of-range layer still reaches the grid read.
  **The latch switch is not symmetric.** Latches 1 and 2 fall through to the shared `TactState_KickQueue` at
  0x153A0; latch 3 returns without kicking (selecting targets is not an action yet); latch 4 clears the latch;
  latch 0 has three separate early exits before its move tail. Latch 3 also arms the look control *before*
  testing the eight-target cap.
  **The two melee gates disagree.** The map-click gate (0x151D8..0x15200) needs occupied AND **not operative**
  AND not large, while `TactAction_ConfirmMeleeMode6` (0x17C6C..0x17C8C) tests only occupied and not large.
  Two entry points to the same mode-4/6 attack family with different acceptance rules — recorded in the ledger,
  reproduced as-is. `Map_pMeleeTarget` (renamed from `unk_128A48`, all six references inside the one function)
  is stored *before* the occupant test, so it keeps a possibly NULL occupant on a failed gate.
  **The ceiling threshold is -23, strictly.** 0x15478 reads the ceiling sprite's signed byte at +0x24
  (`Mc1Rec.iCeilParam`) and skips when it is `>= -23`; the enemy-AI climb uses -15 on the unit's own ceiling,
  so these are two different gates. An occupant under a breakable ceiling that is *not* operative stops the
  click outright rather than falling through to the move.
  **`Unit_CanStepExitDir` probes from the already stepped-to coordinate**, so its wall probes land one cell
  past the candidate — the mode-6 test had to put the blocking wall at (4,2,0), not (3,2,0).
  **Two text-record indices in the existing port were wrong.** `PUFO_TXT_OUT_OF_AMMO` was 0; the original's
  pointer is `aTextRecs + 1230` = record 6 ("weapon not charged"). And `Ui_ConfirmSpecial9` pushed that
  constant twice where the original pushes records **11 and 12** (`unk_B419F`, `unk_B426C`). Both corrected,
  both now asserted, and the eight text globals the tactical handlers name are renamed in the IDB
  (`aTextRec1_OutOfTime`, `aTextRec6_OutOfAmmo`, `aTextRec11/12_MedkitNotNeeded`, `aTextRec35/36_ParalyzerAim`).
  **The enqueue helpers' out-of-TU message belongs to the call site.** `TactState_EnqueueThrow` (0x4B318),
  `TactState_EnqueueFireBurst` (0x4B0D8) and `Tact_EnqueueActionOrStatus205` (0x4B420) each push record 1
  themselves in the original; the port returns false instead, so every call site has to push it. The three new
  call sites do. `Ui_ConfirmFireBurst` (0x18774) still does not — recorded under "Separate issues".
  **`MapCell_FloorCellBelow` is now shared.** The inclusive `SUBS / BPL` descent appears twice in the binary
  (0x4AFD0 in the enemy-AI wander, 0x15500 here) and the port had its own copy in `pufo_enemyai.c`; both now
  call the one helper in `pufo_mapcell.c`. The *exclusive* variant at 0x4AE8C (`EnemyAi_FloorLayerBelow`,
  ledger row 0x4ae8c) is deliberately left separate — it is a different bound, and an original bug.
  138 new checks (12651 total), 5 ledger rows (169), coverage unchanged at 903/1103 (81.9%) and 445204/598792
  (74.4%) — both functions were already cited, so this unit converts cited bytes into implemented ones.
  **Deviation:** `Unit_PathToCell` (0x2c2a8) is still unported, so the latch-0 move tail runs through a
  `pfnPathToCell` seam on `MapClickCtx`; `Ui_ConfirmMeleeMode6` takes the grid lookup and `MapGeom` directly.
  The port NULL-guards `Map.pSelUnit`, its cell, the cursor grid slot and the walked cell, all of which the
  original dereferences, and bounds the cost/enabled index to 0..11 where the original indexes freely.

- [x] **part 69, the path replay: `Unit_PathToCell (0x2c2a8, 1052 B)`** — the move tail the map click and both
  enemy-AI paths were waiting on. **`Map_ComputeReachable`'s sixth argument was being dropped.** The original
  stores it to the global `g_bPathSearchFlag` @ 0x10db84 (0x2b850/0x2b860) and `Path_ExpandCell` reads it back
  (0x2b6ac); the port hardcoded it clear. It comes from `g_aUnitTypeRecord[4 * nUnitType]`, so it is set for
  types 3, 7, 8, 11, 13 and 18, and it is an argument now. **Corrected in part 71:** this part said the flag
  lifts two rules, the +4 layer surcharge and the "no floor sprite, do not expand" gate. Only the second one is
  the flag's — the surcharge is gated on the probe's own `g_bStepReanchored` (0x2b75c), and the port had been
  using the flag for it and for `bFlags29` bit 3.
  **The walk back re-checks the layer.** `bFlags29` bit 3 (set by `Path_ExpandCell` whenever the search flag was
  clear) opens two probes: the layer above needs no floor at the old column and a cost at the new one
  (0x2c458/0x2c478), the layer below needs a ceiling with `iCeilParam < -15` and a cost (0x2c4d8). The two
  probes use different columns — the first mixes old and new, the second uses only the new.
  **The ceiling threshold is -15 here and -23 in the map click**, on the same sprite byte: two gates, recorded.
  **`g_aFacingFromDelta` @ 0xa7b00** (renamed from `byte_A7B00`) is a 27-byte facing table indexed
  `3 * (3 * dz + dy) + dx + 13`. It is the plain compass facing for every delta with a horizontal component and
  differs only at the pure vertical delta, where it reports 8 up and 9 down; `Unit_PathToCell` is its only reader
  in the binary. A turn is queued only when the facing actually changes, and the facing carries across the walk.
  **`TactState_EnqueueVertStep`'s second argument is a layer delta** stored in the queue entry's `nFacing` slot
  (0x49ddc), which is the slot the vertical step's draw offset reads back at 0x50FBC. Renamed to `nLayerDelta` in
  the port and in the IDB.
  **`Map_pPathDestUnit` (Map+0x150f4) is the swap partner.** `Unit_PathToCell` parks the destination cell's
  occupant there (or clears the slot), and `Unit_CanStepTo` reads it back as the one occupant a mover may share a
  cell with — nine times over, each one built as `MOV #0x15000 / ORR #0xF4` (0x27cbc, 0x28b3c, 0x28d98, 0x29218,
  0x294d0, 0x299cc, 0x29c44, 0x2a0b0, 0x2a314) rather than as the `0x150F4` immediate that 0x2c308 uses. An
  earlier pass called the slot dead state because Hex-Rays renders the reader as `pMap->pad86260`; it is not.
  Named in the IDB along with `Map_apPathCells` (Map+0x4a44), `Map_nPathCount` (Map+0x4e44) and
  `g_nCurLayer` (0x113980).
  50 new checks (12701 total), 6 ledger rows (175), coverage 904/1103 (82.0%) and 446256/598792 (74.5%).
  **Deviation:** the real step predicate the flood still needs, `Unit_CanStepTo` (0x27bb8, 14716 B), remains
  unported, so the flood runs through the `PathStepProbe` seam; `Unit_PathToCell` itself matches the
  `pfnPathToCell` seam signatures in `MapClickCtx` and the two enemy-AI contexts, so the real function drops
  straight into them. Its helper `Map_ProbeStepOccupants` landed in the next part.

- [x] **part 70, the step probe: `Map_ProbeStepOccupants (0x2778c, 1068 B)`** — the half of the step test that
  decides *which cell* a step even is, before any wall or occupant is looked at. **A step is not always the cell
  the direction delta points at.** The probe tries two vertical re-anchors on the candidate:
  **down** (0x27990..0x27a54) when the candidate has no floor sprite, the current cell has one, the search flag is
  clear and the candidate is above layer 0 — the candidate becomes `apCells[dstLayer-1][dstY][dstX]`;
  **up** (0x27a6c..0x27b1c) when the current cell has a ceiling with `iCeilParam < -15`, the cell above the
  current one has no floor and the cell above the candidate has one — the candidate becomes
  `apCells[dstLayer+1][dstY][dstX]`. If the candidate wants moving and the cell it would move into has no floor,
  the step is refused outright, not merely left unanchored (0x27a10).
  **The two anchors are not symmetric** (0x27a48): the downward one stores the new cell and drops
  `g_nStepAdjLayer` but never touches `g_nStepDstLayer`, while the upward one updates the cell and both counters.
  The direction arms index with `g_nStepAdjLayer`, so a downward-anchored step keeps its pre-anchor candidate
  layer. Reproduced, and asserted.
  **A large unit drags a second column.** Both anchors gain the cell at `x + 1` of the cell being moved into and
  of the current cell, reached by adding one to the `apCells` index rather than to the coordinate — the
  `R7 = 0x1533C` base (apCells @ 0x15338 plus one dword) at 0x27840, 0x278ac, 0x278fc, 0x27930, 0x27958.
  **ORIGINAL BUG (0x27ae4):** for a large unit stepping due east the cell the upward anchor wants floorless
  (`pAboveOldX1`, 0x27adc/0x27ae4) *is* the cell the preceding test requires to be floored (`var_14`, 0x27a94),
  so the upward anchor can never fire in that direction — a small unit with the same geometry does re-anchor.
  **Three unguarded reads of layer-gated state:** `var_14` is stored only when the candidate layer is below 3
  (0x27848) yet dereferenced at 0x27a94; `g_pStepCellAbove` is written by `Path_ExpandCell` only when the current
  layer is below 3 (0x2b658/0x2b67c) yet dereferenced at 0x27a88; `var_8` is stored only when `g_nCurLayer < 3`
  (0x2790c) yet dereferenced at 0x27ae0. Two dead leftovers: a third `Unit_bIsLarge` call whose `BEQ` to the
  return-zero path is unreachable (0x27a24), and the non-large path loading `R11`/`R6` from a stack slot only the
  large path writes (0x27988).
  **The probe is a global-state machine** in the original: `Path_ExpandCell` seeds `g_nStepCurX/Y`, `g_nCurLayer`,
  `g_nStepDir`, `g_pStepCurCell`, `g_pStepCellAbove` and `g_nStepCeilParamNeg`, and the probe writes the candidate
  back into `g_nStepDstX/Y`, `g_nStepDstLayer`, `g_pStepDstCell`, `g_nStepAdjLayer` and `g_bStepReanchored`. Four
  call sites seed it: `Path_ExpandCell`, `Map_ComputeFov` (0x2deac), `Map_ScanBlastFootprint` (0x32cd4) and
  `GameTick_Step` (0x27324). The port keeps the same shape as one explicit `StepProbe` struct so the four callers
  do not share mutable state. Eleven globals named in the IDB (`g_nStepCurX`, `g_nStepCurY`, `g_nStepDir`,
  `g_nStepDstX`, `g_nStepDstY`, `g_nStepDstLayer`, `g_pStepCurCell`, `g_pStepDstCell`, `g_pStepCellAbove`,
  `g_nStepAdjLayer`, `g_bStepReanchored`, `g_nStepCeilParamNeg`) and the function given the `Map *` signature.
  38 new checks (12739 total), 7 ledger rows (182), coverage 905/1103 (82.0%) and 447324/598792 (74.7%).
  **Deviation:** the original dereferences the layer-gated pointers blindly; the port tests them for NULL first,
  which is why the three bugs above are recorded as not reproduced.

- [x] **part 71, the step legality: `Unit_CanStepTo (0x27bb8, 14,716 B)`** — the last blocker in the tact
  subsystem, and the biggest single function ported so far. It seeds `g_nStepDstX/Y/Layer` from
  `aDirDeltaXY[3 * g_nStepDir]` itself (0x27bd8..0x27c24), runs a head every direction shares, then dispatches
  through a ten-way switch, with a second ten-way switch for a large unit at 0x28A44.
  **The head.** Normal unit: the candidate must be in range, must not be blank (`MapCell_TestFlag` 0), and its
  occupant must be the swap partner, the mover, or a foreign unit that is not flagged at `bUnk148`, is not
  operative, and is not unit type 16 or 17 (0x27cb0..0x27d18). Large unit: only the range test, and
  `g_pStepDstCell` is settled before the switch — a large unit's blank tests are per-arm, over its whole
  footprint.
  **The arms.** Each runs `Map_ProbeStepOccupants` first — except directions 8 and 9, which never call it,
  because the step is already vertical — then the arm's own occupant gate, then a direction-specific list of
  blank, wall and occupant tests. The lists are uneven on purpose: a diagonal direction tests the walls of the
  cells it passes between as well as the cell it enters, which is how the original stops a mover cutting a
  corner through a wall. A large unit's arms test the four cells of its 2x2 footprint.
  **The seam had to carry the settled cell, not the candidate.** `Path_ExpandCell` takes the neighbour through
  `g_pStepDstCell` (0x2b6d4, read at 0x2b768), and a downward re-anchor moves that cell without moving
  `g_nStepDstLayer`. The port reports `g_nStepAdjLayer` for the horizontal directions — the layer the settled
  cell actually lives in — and `g_nStepDstLayer` for the vertical pair, which never calls the probe.
  **ORIGINAL BUG (0x28c90):** every large arm re-runs `Map_IsValidCell` for the footprint's far corner and
  passes `nLayer = 0`, although the function does test the layer, so the call is really only an x/y test.
  Harmless because the head validated the layer, but not what the call says.
  **ORIGINAL BUG (0x27e0c):** the arms' occupant gate is weaker than the head's — `bUnk148` alone lets a foreign
  unit through, with no operative or unit-type test. For a step the probe did not move the head already refused
  that cell, so the weaker gate is only reachable through a re-anchored candidate.
  **ORIGINAL BUG (0x2ab80):** the large unit's vertical arms chain the same two floor tests four times over in
  direction 8 and seven times over in direction 9, always the same two cells.
  **The correction this unit forced, 0x2b75c.** `Path_ExpandCell` gates the +4 layer surcharge on
  `g_bStepReanchored`, not on `g_bPathSearchFlag`, and stores that same byte into `bFlags29` bit 3 at 0x2b7a4.
  The port used the search flag for both. Since a horizontal direction only changes layer through the probe's own
  anchor — which sets the flag — the surcharge is really the vertical directions' price, and the port was
  charging it to re-anchored horizontal steps and setting bit 3 on the wrong condition. The `PathStepProbe` seam
  now carries a re-anchor report, and `tests/test_path.c` pins both halves.
  48 new checks (12787 total), 4 ledger rows (186), coverage 906/1103 (82.1%) and 462040/598792 (77.2%). The
  tact subsystem is at 211 of 214 functions and 1308 bytes, all three of them the atexit registry glue and
  `Map_InitAndLoadRegistry`.

- [x] **part 72, the geoscape pointer dispatcher: `Geoscape_OnPointer (0x872d0, 8,576 B)`** — the largest UI
  function in the binary and the only reader of the 13-slot `GeoscapeScreenBlock` control array
  (`apObj[13] @ 0x1864F8`). Ported in `game/pufo_geopointer.{c,h}` behind `GeoscapePointerEnv`.
  **The entry (0x872F8 / 0x8731C)** stores the cursor through `nSmapCursorX @ 0x186BD0` / `nSmapCursorY @ 0x186BD4`,
  which are **16-bit** globals (STRH), so the caller's int is truncated before any arm reads it back. The hit tests
  use the arguments; the panel tracking, the map arithmetic and the area gates all use the stored halfwords.
  **The screen-0 menu band (0x87324):** a status-line flag with `30 < flag < 50` hands the pointer to the menu
  controls `18704C` / `187024`, and flag 37 adds the confirm pair `186BA4` / `186BAC`. **The switch** covers ids
  0..12 and 15..20; ids 13 and 14 and anything else fall to the default arm, which hands back the pointer x
  unchanged. **The world arm** (screens 0 and 2, `LABEL_13 @ 0x87530`) is an eleven-test chain over the status-line
  flag — values 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 and 60 — with the target-select, radar-panel
  and multi-select-panel gates interleaved.
  **The drag block (0x87C48..0x87D84)** is the part that was not obvious from the decompilation. The strip height is
  the current zoom overlay pad's `uHeight` (`PadImage+0x0A`), so the globe area is `320 - nStripH`. A press with
  `nRadarCooldownR @ 0xAC218 < 6` inside that area resolves the tile under the pointer through `Smap_GetTileAtScreen`
  and sets the cooldown to 255 — the same byte the radar screen uses as its pulse timer is the globe tap gate here.
  Otherwise the drag starts: `bGeoscapeDragActive @ 0x187074` set, cooldown cleared, `SmapView.nLastDx / nLastDy`
  parked at the cursor. A message on the status line (`nLineCount != 0`) stops the globe from scrolling (0x87CF8).
  **The tap division (0x87E94..0x87EAC)** is `(v + 31) >> 5` for a negative v, not floor division — the ARM adds 31
  before the arithmetic shift, so −31 gives 0, not −1. Outside the map area (y ≥ 232, x ≥ 192 or y ≤ 36) the stored
  tap pair is left alone and the stored tap flag decides.
  **Five original defects**, each with a marker at its port site: `Smap_HitTestStrategSelect` called twice with the
  same arguments on one press (0x87C94); the world tail returning the drag flag or the screen id itself, so screen 0
  returns 0 and screen 2 returns 2 from the same path and no caller reads it (0x87D84); a `<<16 / >>16` sign-extend on
  a value that is already a halfword (0x88F4C); the options page-2 arm being byte-for-byte the page-1 arm
  (0x89210 / 0x89270); a `CMP #0 / BLS` on a u8 (0x88524).
  **Three deviations, recorded not fixed:** only the ten base-strip slots are null-checked in the original (0x87E38),
  every other slot is dereferenced blind; `Smap_HitTestStrategSelect @ 0x82be8` (3,088 B) is unported, so the select
  test is injected through `GeoscapePointerEnv.pfnHitTestStrategSelect` (0x87C2C); screen 10 tracks and clicks
  `pBuySellPanelA` — the panel screens 11 and 12 use — while its own site-info controls sit in a separate array
  (0x889F0).
  **Two ownership problems found, reported as separate issues** (see HANDOFF §7): `PufoWorldState` carries two fields
  for the single byte at 0x187078, and the IDB declares `bBasePlaceFail @ 0x1870B8` 16 bytes wide, overlapping
  `bBasePlaceActive @ 0x1870BC`.
  235 new checks (13022 total), 8 ledger rows (194), coverage 907/1103 (82.2%) and 470616/598792 (78.6%); `strat`
  moves from 86,792 to 95,368 of 160,496 bytes (59.4%). IDB: `byte_187074` → `bGeoscapeDragActive`, comments on the
  entry, the drag gate, the tap division and the five defects.

- [x] **part 73, the pointer release: `Geoscape_OnPointerUp (0x89450, 6,992 B)`** — the release half of the
  same dispatcher, in the same module and on the same `GeoscapePointerEnv`. **It is not the press with
  bDown flipped.** It takes only the 0x1864F8 block: every coordinate is the cursor pair the press stored
  (read back with LDRSH), and every `UiControl_HitTest` is a release — x = y = 0, bDown = 0, bForceClick = 0
  at every call site, because a release ignores the hit box.
  **What only the release has.** The shortfall gate (0x89578..0x89600): when the dword at 0x1870C8 is exactly
  1 and the screen is not 0, the report is cleared here unless the screen is 6, 9, 10, 11 or 12 — the screens
  that draw the shortfall panel; screen 9 also drops both popup bytes when no product is selected. `Sound_Play(17)`
  in almost every flag arm (the press plays it in two). `UiPanel_ClickRow` where the press only tracked the
  hovered row — and the press's `pPlaneCraftStatusPanel` / `pPlaneSlotPanel` clicks have no release counterpart
  at all. A tail shared by two arms (0x89B64..0x89B90): click the row, then set the panel's `bFlag7073` — the
  multi-select gate reaches it with the status panel, the options confirm tail jumps to it with
  `pOptPanelShips` (0x8AF1C). A fall-through (0x89CD4..0x89D20) that clears the target-select flag, returns 2 on
  screen 2, and releases all thirteen root controls on any other screen.
  **What only the press has**: the drag block, the strateg-select test, the tap arithmetic, the
  placement-fail clear. The release clears the drag flag instead (0x896BC) and never calls
  `Smap_HitTestStrategSelect`.
  **Where the two halves disagree on one screen**: the menu band releases the flag-37 pair *before* the two
  menu controls; flags 17/15 and 21/22/23/12 and 10/60 and 13/27 are merged arms here and separate ones in the
  press; screen 10 tracks `pBuySellPanelA` on the press and clicks `pBuySellPanelB` / `pBuySellPanelC` on the
  release (0x8A65C / 0x8A670); screen 7's second hot corner uses the arguments in the press and the stored
  cursor here.
  **Three original findings, 6 ledger rows (200 total):** 0x8ac9c (the screen-17 tail's conditional clear of
  `byte_1870F8` can only fire on a byte that is already 0 — the arm returns at 0x8ABA8 whenever the selection
  is non-zero, so it only matters if the psi panel's own click handler re-armed it at 0x89B78); 0x8a188
  (`bPopupVisibleB` cleared through a conditional store); 0x895c8 / 0x89b90 (three arms hand back an address —
  `&byte_1870C8`, or the `UiPanel *` the shared tail clicked — and `Input_OnPointerUp` discards it at 0x14910).
  Plus the null-check asymmetry (0x89cf4) and the width finding (0x89584: the shortfall byte is read as a DWORD
  here, the only reader in the image that does, which is why the env carries it as `u32 *`).
  **A naming error this function caught in the port:** the soldier-screen slots were recorded as
  `gBaseFacGrid + 0x1C0` (0x159928) and `+ 0x1E0` (0x159948). The ARM loads `[base,#0x148]` and `[base,#0x150]`
  — **0x1598B0** (`apSoldierPanelCtl`, 2) and **0x1598B8** (`apSoldierFindCtl`, 3), right after the ten nav slots
  at 0x159888. 0x159928 and 0x159948 have no xrefs at all. `pufo_geopointer.h` corrected; `pufo_baseui.h` already
  had them right.
  245 new checks (13267 total), coverage 908/1103 (82.3%) and 477608/598792 (79.8%); `strat` moves from 95,368
  to 102,360 of 160,496 bytes (63.8%). IDB: comments on the shortfall gate, the three residues, the null-check
  asymmetry, the slot immediates and the signed base-count product.

- [x] **part 74, the map select test: `Smap_HitTestStrategSelect (0x82be8, 3,088 B)`** — the test the press
  dispatcher calls twice on one press (0x87C10, 0x87C94), ported in `pufo_baseui.c` beside
  `Geoscape_OnRadarListClick @ 0x854c4` on its own `SmapHitEnv`. A strict strip gate (0x82C18, before the line
  clear), a **16x16 cell window** (dx, dy over -8..7) walked outside the item loop, item coordinates scaled by
  **1/1.333** at level 1 and **1/1.694** at level 2, one status-panel line per matching item with the item index
  in `anLineAux`, the six-branch dispatch for exactly one line, the multi-select layout for more, and the
  flag-27 tail that writes the pending click.
  **What the ARM showed and the pseudocode did not.** 0x82ED4 loads `BaseRec.bUnk4` with **LDRSB** against the
  full i32 `nSiteId`, while `Base_FindIndexByTypeByte @ 0x6ac30` masks its argument to a byte (0x6AC58) and
  loads the same field signed (0x6AC68) — the port compared that field as u8 and matched values of 0x80 or more
  the original can never match. Fixed, with two checks in `test_geoscape.c`. 0x82FE4: the type-3 label copy
  restarts at index 0, so record 1146 **replaces** record 1145. 0x83250 sets the select answer on any
  coordinate match even when no line was added. 0x83784 / 0x83788 are the writer of 0x186414 / 0x186418, the
  pending click `Smap_ConfirmCraftTarget @ 0x842ec` reads — the note there had called this writer unported.
  0xECDB5 / 0xECE82 / 0xECF4F are **TEXTS records 1145, 1146, 1147** (`aTextRecs + 205 * n`), never written
  anywhere in the image; renamed in the IDB to `aTextRec1145/1146/1147`.
  **A shared-leaf fix this unit forced:** 0x5A2FC masks colour slot 0 with `AND R3,R3,#0xFF` and 0x5A30C /
  0x5A314 / 0x5A31C byte-load slots 1..3, so no colour slot can hold more than 255. `Line_Write` stored them
  as full dwords; 0x836A0 stores -1 into 0x187054 and 0x836D0 appends it, so the original writes 255 and the
  port wrote 0xFFFFFFFF. Masked now, asserted in `test_panel.c`.
  **10 ledger rows (210 total):** the two past-the-end search indexes (0x82f1c, 0x83164), the two scaler
  inconsistencies (0x82d48, 0x82dd0), the answer-without-a-line rule (0x83250), the prefix replacement
  (0x82fe4), the duplicated craft search (0x836a0), the `bFlag7073` asymmetry against the radar click (0x83494),
  and the two write-boundary fixes (0x6ac68, 0x5a2fc).
  132 new checks (13405 total, 59 suites), coverage 909/1103 (82.4%) and 480696/598792 (80.3%); `strat` moves
  from 102,360 to 105,448 of 160,496 bytes (65.7%). IDB: 3 renames and 16 comments.

- [x] **part 75, the screen close dispatcher: `Geoscape_CloseUI (0x8b94c, 2,100 B)`** — the target of the 13
  screen-close thunks that were hook seams until now, ported in `pufo_baseui.c` on one `GeoscapeCloseEnv`: a
  head (`Sound_Play(17, 4)` — the second argument is 4 at all 7 call sites, so `Sound_Play` takes it — then
  `dwActionFail = 0`, with both popup bytes cleared through the shared tail), the speed re-apply chain, a
  per-base pass that re-reads `nBaseCount` on every iteration (0x8BA70) and rebuilds the needed-flag grid at
  `grid + 444 + 144 * base`, and a switch on `nScreenId` with **17 live cases** (1..12, 15, 16, 18, 19, 20) out
  of the 20 screen values; 13, 14 and 17 take the default return, which is why a radar-list close leaves the
  screen id alone.
  **What the ARM showed and the pseudocode did not.** The chain tests 5, 60, 300, 1800, 3600 and 43200 while the
  table its setters write is `g_aGeoscapeSpeedSec = {5, 60, 300, 1380, 2460, 3600}` — 1800 and 43200 are values
  no speed control produces, and every arm that does fire rewrites the tick, so closing a screen is not a no-op
  for game speed. **0x8B9E0** is the sharper half: the 3600 arm calls `GameTime_SetSpeedIdx4` and then branches
  into the 43200 arm's `SetSpeedIdx5`, so a 3600 tick lands on index 5 and re-arms the sixth control. Case 6
  calls `BasePlaneScreen_BuildControls` with R0 left as the `bPlaneStoresCommitted` byte that just passed its own
  test, and `BasePlaneScreen_BuildPanels` with the literal 1 (neither callee reads a1). **0x8BF74** decrements
  `nSelectedBaseIdx` with LDRSB / SUB #1 / STRB and no floor, so a zero index wraps to 255. Case 11 sets
  `bPopupVisibleB` at 0x8C0C4 and the shared tail clears it at 0x8BFAC. Case 19 adds the five funding-bar slots
  in bar order and tests `BGE` against 0xFFD23940, so bankruptcy is funds **strictly** below -3000000
  (`PUFO_FUNDS_BANKRUPT_LIMIT`). Case 18 and `Options_SaveAudioSettings @ 0x8bd00` are **mutually recursive**
  (0x6B7F0 calls back into the close), and the recursion ends only because case 18 cleared `bOptionsScreenOpen`
  first.
  **A wrong port field this unit removed:** 0x186594..0x1865A4 is a five-slot int bar, not four money values
  plus a pointer. The third slot is filled at 0x9469C / 0x946A4 from a pool entry IDA names `unk_14B4C8`, so it
  holds **1357832** among four round money values (1625000, 383000, 1668000, 968000), and case 19 adds all five
  as plain ints. The port's `void *pvFundTable` was the wrong width for a 32-bit block and was never read;
  `GeoscapeEconomy` now has `anFundBar[5]` and the slot is `PUFO_FUND_BAR2_INIT`. Whether the original meant an
  address is unresolved and recorded as unresolved.
  **11 ledger rows (221 on record):** the 3600 double call (0x8b9e0), the chain-vs-table mismatch (0x8b9a0), the
  garbage first arguments (0x8c044), the 0→255 wrap (0x8bf74), the residue third argument (0x8bd00), the
  set-then-cleared popup (0x8c0c4), the 0x14B4C8 bar slot (0x946a4), and four IDB naming corrections (0x8bc74,
  0x1870b8, 0x158828, 0x1870c8).
  174 new checks (13583 total, 60 suites), coverage 910/1103 (82.5%) and 482796/598792 (80.6%); `strat` moves
  from 105,448 to 107,548 of 160,496 bytes (67.0%). IDB: 7 renames (393 on record), 4 size/type fixes — the
  16-byte `bBasePlaceFail` item overlapping `bBasePlaceActive`, `byte_1870C8` typed as a byte though all three
  writers store a dword — 14 comments, and one wrong comment corrected at `BaseInfoScreen_OnOk @ 0x557c8`.

- [x] **part 76, the audio-save callee: `Options_SaveAudioSettings (0x6b7ac, 336 B)`** — the options screen's
  save-hotspot handler (installed on `pGeoscapeCtlA @ 0x186BB4` by `OptionsScreen_Init` and again by
  `OptionsScreen_Open` at 0x6D24C) and the function case 18 reaches at 0x8BD00, so the mutual recursion the
  previous unit opened now runs between two ported functions. Three steps: close while `bOptionsScreenOpen`
  is set (0x6B7C4); while `gOptionsScratch[4]` is set, read `gOptionsScratch[0]` signed and, if non-zero,
  write the four audio values to the registry **only when the tab is exactly 4** (soundvolume, musicvolume,
  musicoff, soundoff — the last two as the shared "1" @ 0xA62A8 / "0" @ 0xA62A4), then clear the tab and run
  `OptionsScreen_LayoutButtons`; if the tab is zero, close again at 0x6B7F0. Tail: a clear
  `bMapSelectionFlag` clears 0x15961C (`GameTime.bPaused`) — a close with no pending map selection resumes
  the clock — and the flag clears whatever happened.
  **What the ARM showed and the pseudocode did not.** **All three arguments are dead**: 0x6B824 overwrites
  R0 with `Map_nSoundVolume` before the first call and R1/R2 are never read, so the block, the signed byte
  and the base-loop residue all reach a callee that ignores them — the port takes one env. And the recursion
  is not the two-level shape Hex-Rays suggests: this function's own close at 0x6B7C4 runs **first** (gated on
  `bOptionsScreenOpen`, which case 18 never touches), that pass runs `OptionsScreen_RebuildWithSound` whose
  `OptionsScreen_Init` clears `gOptionsScratch[4]` at 0x6C0FC, so the re-test at 0x6B7D4 **fails** and 0x6B7F0
  is never reached. The 0x6B7F0 arm runs only when `bOptionsScreenOpen` was already clear on entry. The tab
  byte's sign is not observable here (both tests answer the same for 0xFD signed or unsigned) and is recorded
  rather than tested.
  **IDB naming, including a mistake caught in the same unit:** `byte_159E88` → `gOptionsScratch` ([0] tab byte
  0..4, [4] @ 0x159E8C controls-exist; only offsets 0 and 4 are used anywhere), its setters
  `Set_byte_159E88_4` / `Set_byte_159E88_3_SetUiY` → `Options_SetTab4` / `Options_SetTab3_AndUiY`, and
  `unk_186BB4` / `unk_186BB0` → `pGeoscapeCtlA` / `pGeoscapeCtlB`. The first attempt at the pair —
  `pOptionsSaveCtl` / `pOptionsBackCtl` — was wrong: the slots are reused per screen (manuf and research panel
  controls, research OK/cancel, the park control, the options hotspots), so it was renamed again before the
  commit. The port carried the mirror error: `ScreenScratch.nLineAux145` was commented as
  `gUiRootPanel.anLineAux[145] (0x159E88+145)`, but nothing addresses 0x159F19 and no `anLineAux` owner exists
  in the IDB — the byte is `gOptionsScratch[0]` at 0x159E88, offset 0.
  **1 ledger row amended (221 on record):** 0x8bd00 now says all three arguments are dead, not just the third.
  49 new checks (13632 total, 60 suites), coverage 911/1103 (82.6%) and 483132/598792 (80.7%); `strat-ui` moves
  from 80,516 to 80,852 of 121,164 bytes (66.7%). IDB: 5 renames (396 on record) and 7 comments.

- [x] **part 77, the options overlay: `OptionsScreen_Open (0x6d0ac, 3,160 B)`** — the last callee case 18
  reached through a hook, so the whole case-18 chain is now ported code. A toggle on `bOptionsScreenOpen`:
  the close half (0x6DC30) clears `nGeoscapeScreen` and the flag and releases four panels
  (0x159E18 / 0x159E78 / 0x159E7C / 0x159E1C) plus the two scroll controls (0x159E3C / 0x159E40); the open
  half sets the flag, clears `bUiAnimStarted`, resolves a line count, allocates the scroll pair, installs
  `Options_SaveAudioSettings` on `pGeoscapeCtlA` at y = 179, builds four lists, and writes one row per craft
  across all four. Line count: `Map_nCraftCount` when `nMode == -1`, else the craft homed at the first base
  whose `BaseRec.bUnk4` (LDRSB) equals `nMode`.
  **What the ARM showed and the pseudocode did not.** The base-walk bound is the signed byte at **0x159D80**,
  reached as `[abFacilityGrid,#0x618]` — R0 is `abFacilityGrid` there because the `LDREQ` of `0x1139B8` at
  0x6D0F0 is conditional on `nMode == -1` and did not run; Hex-Rays hides that both walks are the same block
  with offsets 0x1174 (bases, 0x114B2C) and 0x10AC (craft, 0x114A64). `stru_AB9B0` is **not** two identical
  records: "up"/"sup" at (20, 180) and "down"/"sdown" at (50, 180). The stores labels are TEXTS 149–152 and
  the region labels are TEXTS 1165, 1166, 1167, 1036, 1168–1172 — the switch is on `BaseRec.bRegionId`, the
  same 0x80..0x89 byte `FundingScreen_Init` groups. The stores aux dword starts at -1, rises to 0 only when
  range AND fuel are both full, and returns to -1 whenever the craft is not at base; the fuel test reads
  `uFuelMax` signed. **ORIGINAL BUG (0x6D884):** the region switch has no default copy and 0x6DA68 then
  `strcat`s onto the uninitialised 104-byte local — any region outside 0x80..0x88 prints stack garbage.
  **1 ledger row (222 on record), 85 markers.** 51 new checks (13683 total, 60 suites), coverage 912/1103
  (82.7%) and 486292/598792 (81.2%); `strat-ui` moves from 80,852 to 84,012 of 121,164 bytes (69.3%). IDB:
  13 renames (409 on record) and 5 comments.

- [x] **part 78, the overlay row confirm: `OptionsScreen_OnConfirm (0x6cf18, 260 B)`** — the names panel's
  click handler, installed at 0x6D2E8 by `OptionsScreen_Open`. The row is the names panel's `nUnk3032` and
  the craft is `pPanelName->anLineAux[nUnk3032]`, written into both `nStrategIndex` @ 0x159E74 and
  `nSelectedShipIdx` @ 0x187054. The gate is the **stores** panel's aux dword at the **same** index
  (0x6CF5C reloads `nUnk3032` from the names panel): non-zero means not ready and the confirm does nothing
  at all — no flag, no sound, no line, no pause. Ready: `bMapSelectionFlag = 1`, `Sound_Play(7, 4)`, a
  flag-27 message line carrying the craft index (what `Smap_ResolveInterceptUI` answers with
  `Ship_RetargetTravel`), the final-mission arm, then `GameTime_Pause` on both arms of the topic test.
  **The two constants are pinned against the shipped data, not guessed.** lang.dat `TEXTS.DAT` is a 2-byte
  header plus 200-byte records (330002 = 2 + 200 × 1650) against the 205-byte in-memory stride; read that
  way TEXTS[415..419] are the five CraftType names, so `Ship` +2 == 2 is the **Avenger**, not the
  Interceptor. main.dat `RESEARCH.DAT` is 144 records of 34 whose first u16 is the topic's TEXTS name index;
  record 28 is 448 = "Цидония или разорение" at cost 6000 — the Cydonia topic. `Geoscape_Draw` @ 0x90908
  blits `pFinalMissionCtl` and draws TEXTS[1236] "Финальная миссия!" while the byte is set, and both pointer
  dispatchers hit-test that control first. So `byte_187068` is the final-mission button's armed flag. The
  same read confirms unit 77's thirteen labels and they are now IDB comments.
  **1 ledger row (223 on record), 85 markers** — a latent out-of-bounds `apCraft[-1]` at 0x6CFD8. 29 new
  checks (13712 total, 60 suites), coverage 913/1103 (82.8%) and 486552/598792 (81.3%); `strat-ui` moves to
  224/245 functions and 84,272 of 121,164 bytes (69.5%). IDB: 3 renames (412 on record) and 21 comments.

- [x] **part 79, the map-order answer: `Smap_ResolveInterceptUI (0x84964, 340 B)`** — the click handler
  `Geoscape_Draw` installs, which reads the flag on message line 0 and decides what the pending order
  becomes. `GameTime_Set5Sec` and a cleared `bInterceptDone` first; flag 27 sends the craft in colour slot 0
  home via `Ship_RetargetTravel`; flag 13 rewrites the line (insert a flag-11 line at index 1 carrying line
  0's colour **slot 1** masked to a byte, drop line 0), sets `bInterceptDone`, pauses and returns; anything
  else clears the four selection bytes, runs three dismiss tests on 23/21/22, removes line 0 again, unpauses
  only if the list is empty, and clears `bFinalMissionCtlArmed`.
  **Three things only the ARM showed.** 0x84A10 branches to 0x84A9C, *past* the armed-byte clear at 0x84A94,
  so the flag-13 path is the only one that leaves the final-mission control armed. The three dismiss tests
  each re-read line 0 and `Smap_DismissSelected` removes line 0 itself (0x84454), so they are not three
  tests of one flag — a match shifts the list and the next test sees the line underneath (`[23, 21]`
  dismisses two different craft). **ORIGINAL BUG (0x84A80):** the tail removal is unconditional, so after a
  dismiss it eats the NEXT message line and the clock unpauses a step early.
  **Naming follow-through:** the port's `GeoscapePointerEnv` field for 0x187068 was `pbTargetSelectActive`, a
  name from before unit 78 showed what that byte gates; it is `pbFinalMissionCtlArmed` in the env, both
  handlers and the test fixture now, matching the IDB. `byte_18709C` → `bInterceptDone`.
  **1 ledger row (224 on record), 87 markers.** 38 new checks (13750 total, 60 suites), coverage
  914/1103 (82.9%) and 486892/598792 (81.3%); `strat` moves to 189/204 functions and 107,888 of 160,496
  bytes (67.2%). IDB: 1 rename (413 on record) and 5 comments.

- [x] **part 80, the map click that recentres: `Smap_OnPointerUp (0x84688, 732 B)`** — the other
  `Geoscape_Draw` click handler. `bMapSelectionFlag` picks the arm and both arms share the tail.
  Selection arm (0x8490C): line 0 colour slot 0 is the strateg index, `[0x187054]` the ship index,
  `Ship_LaunchIntercept`, then the flag is cleared — no clock work. Recentre arm: `GameTime_Set5Sec`,
  then the view is centred on the strateg entry the line names.
  **Four things only the ARM showed.** The half-extents are plain C halves: 0x846B4..0x846D8 is
  MOVS / ADDMI #1 / ASR #1, and ADDMI fires only on a NEGATIVE value, so a 401-wide map centres at 200 —
  the same half a 400-wide map gives. (This unit first read that triple as a ceiling and the port carried
  the ceil; unit 82 corrected it.) 0x846DC and 0x84700 read colour slot 0 **twice**, so nX and nY come from the
  SAME entry. nMode 1 unscales both coordinates by 1/1.333 and nMode 2 by 1/1.694 — the exact
  reciprocals of the `Geoscape_Draw` zoom factors, so stored item coordinates are map coordinates.
  `nScrollY` has **two** replace arms: the RSBMI at 0x8484C (whose N flag comes from the `ADDS` at
  0x84838 computing `nY - 160`, not from the value just stored) and the zoom-strip height test at
  0x84870 (`LDRH [R1,#0xA]` of the PadImage at `[0x186BBC]`). The crosshair x is algebraically always
  120 because `nScrollX` is exactly `nX - vW - 120` and has no replace arm.
  **ORIGINAL BUG (0x84894):** the crosshair y wraps by `nMapH` with ONE conditional subtraction, so a
  value more than nMapH above the range stays out of range (200 on a 100x100 view). Unreachable on the
  shipped map; reproduced by a small-view test.
  **Naming:** `unk_186BC8` → `nSmapCrosshairX`, `unk_186BCC` → `nSmapCrosshairY`, `unk_1870B0` →
  `bSmapCrosshairPending` — all three are written only here and read only by `Geoscape_Draw` (0x8C6A8,
  which blits the 1 x (320 - uHeight) vertical and 240 x 1 horizontal pair while `nRadarCooldownR < 5`).
  **1 ledger row (225 on record), 88 markers.** 35 new checks (13785 total, 60 suites), coverage
  915/1103 (83.0%) and 487624/598792 (81.4%); `strat` moves to 190/204 functions and 108,620 of
  160,496 bytes (67.7%). IDB: 3 renames (416 on record) and 5 comments.

- [x] **part 81, the geoscape event pass: `StrategList_ProcessEvents (0x7fcb0, 4,904 B)` +
  `Strateg_ConfirmInterceptTarget (0x7fb98, 280 B)`** — the per-tick update of every strateg entry,
  wired into the shell as `World_ProcessEvents` so the spawn hooks reach it back. A removal does not
  advance the loop index (0x805FC jumps back to 0x7FCF0 with the counter unchanged, because the tail
  shifted into the hole), and a craft-to-craft arrival tail-calls the epilogue at 0x80FCC, ending the
  whole pass there.
  **Three things only the ARM showed.** `_rt_udiv` is called with the **DIVISOR in R0 and the DIVIDEND
  in R1** — `Geoscape_AdvanceTime` loads 60 / 3600 / 86400 into R0 against an elapsed-seconds R1 — so
  travel completion is `elapsed / 60 >= uTravelMin` and never `60 / elapsed`. The interpolation constant
  at 0x80A80 / 0x80B98 / 0x80C70 / 0x80D70 is the double 0x3F911111_11111111, **exactly 1/60** (exponent
  field 0x3F9 = 2^-6, mantissa 1 + 1/15 = 16/15). The arrival dispatch **re-reads** `Ship.nStrategIdx`
  and calls `PtrArray_Get` for each of its six type tests (0x8036C..0x803F8); the pseudocode's single
  `pTargetItem` hides that the original would follow a list that shifted in between.
  The x leg keeps the winner of |dx+800| / |-dx+800| / |dx| (a target 500 columns east is reached by
  going 300 WEST), `dFracX` / `dFracY` accumulate over the true diagonal, and an inactive visible
  type-1 entry is the craft's OWN map marker, matched by `Ship.bId == StrategItem.nDisplayId`.
  **ORIGINAL BUG (0x7FBE8):** the assault offer asks `Craft_CountSoldiersAboard` with `Ship.bId`, while
  that callee matches its argument against `Soldier.bUnk183`, an apCraft INDEX; the two agree only while
  no craft has ever been removed. Reproduced.
  **Naming, and the defect it exposed.** 0x159E74 is an apCraft INDEX: 0x3DE84, 0x3DF5C and 0x3DF88 all
  index `Map.apCraft` with it and only then read `Ship.nStrategIdx @ +0x38`. The port carried this field
  as `nStrategIndex` in nine environments; it is `pnMsgShipIdx` / `nMsgShipIdx` now, and the rename
  showed that all three `Tact_Shutdown` removals passed the 0x159E74 value as the **list** index instead
  of the craft's `nStrategIdx` — a mission ended by a craft whose list position and craft index differ
  removed the wrong contact. Fixed on both paths, with two tests that fail on the old code.
  **1 ledger row (226 on record), 89 markers.** 105 new checks (13890 total, 60 suites), coverage
  917/1103 (83.1%) and 492808/598792 (82.3%); `strat` moves to 192/204 functions and 113,804 of
  160,496 bytes (70.9%). IDB: 2 renames (418 on record), 1 size fix (0x159E74, 1 byte → dword; both
  save paths move 4 bytes) and 3 comments.

- [x] **part 82, the strateg icons and the clock strip: `Smap_DrawStrategIcons (0x822c4, 1,060 B)` +
  `Geoscape_DrawClockPads (0x5c5c8, 2,168 B)`** — the two remaining unported leaves of
  `Geoscape_DrawScreen`, ported before their caller so the caller has nothing left to stub.
  **A wrong reading corrected, in this port and in the IDB.** 0x822EC..0x8231C is `MOVS` /
  `ADDMI R3,R3,#1` / `ASR #1` (the middle word is `0x42833001`), and ADDMI is **add-if-minus**: the +1 is
  skipped for every non-negative value, so a positive extent is halved by plain `n >> 1` and the +1 only
  gives a negative extent C's round-toward-zero (`(-5 + 1) >> 1 == -2 == -5 / 2`). Hex-Rays' `/ 2` is
  faithful here. Three earlier units recorded this triple as a CEILING half and the map click (unit 80)
  and this icon pass both implemented the ceil, so on an odd map size the view and every icon sat one
  pixel off. Both now use `n / 2`; the 401x401 recentre test asserts 200 instead of 201, a new 401x401
  icon check pins the reading in the draw, and the IDB comments at 0x822c4 / 0x6c348 / 0x8c698 are
  corrected with a line comment at each of the four halving sites (0x5EB44, 0x6C1A4, 0x822EC, 0x846B4).
  **What the ARM pins down that the pseudocode flattens.** 0x82318 `MOV R0, #0x20` forces palette slot 32
  into `Map_nGlyphPalBase` (repeated at 0x82328 after the empty-list test) while the `Font_DrawGlyph` nPal
  argument stays R9 = 0 — and `mapbits.pal` is palette slot 32 in main.dat, which cross-confirms it. Zoom
  scales **both** axes (mode 1 by 1/1.333, mode 2 by 1/1.694). The world→screen wrap is one conditional
  subtract per axis with a `>` test (0x824BC/0x824C0, 0x824CC/0x824D0), so a coordinate exactly equal to the
  map size is not wrapped. The animation phase is the **low halfword** of `nUiSlot0AnimPhase @ 0x113A18`
  (LDRH at 0x82634), added to the glyph index for every case except the two no-animation type-1 forms.
  **Measured, not assumed.** mapbits has 16 frames (nMapBitsBase 256, nTotalSprFrames 272) and frame **+6 is
  blank in the shipped main.dat** — lit-pixel counts for base+0..+15 are 32,32,20,20,20,20,**0**,32,32,32,
  28,28,28,28,28,28. The test asserts the blank frame and that each checked frame paints pixels, so no hash
  comparison can pass on an empty box.
  **The clock strip is pads, not glyphs.** The ten digit pads are `apClockDigitPads @ 0x186BF0`, indexed by
  digit value with the origin written before each blit; the literal pool at 0x8662C pairs 0x186BF0 with the
  names "b0".."b9". That forced the port-side rename `apLayerPad` → `apClockDigitPads` (and
  `PUFO_GEOUI_LAYER_PADS` → `PUFO_GEOUI_CLOCK_DIGIT_PADS`) in `pufo_geouimanage.{c,h}` and
  `pufo_geoiinit.{c,h}` — the port had documented a clock array as layer pads. The day tens digit is SET
  always but BLITTED only when non-zero (0x5CA00 / 0x5CA14 / 0x5CA18); the digit split calls `_rt_udiv` with
  `MOV R0, #0xA` first (0x5C9D0 / 0x5C9D8), confirming the divisor-first convention a fourth time. Text
  records are TEXTS[1244..1250] weekdays, TEXTS[1251] the day/month separator and TEXTS[1252..1263] months;
  the weekday is centred on 124 (`124 - width / 2` at 0x5C9B0) while the month sits at a fixed 130,287.
  **1 deviation recorded, not hidden:** the original copies a TEXTS record into a 52-byte stack buffer
  (`psz[52]` at 0x5C5AC) with an unbounded byte loop; the port caps it at 51 + NUL. Not a ledger row — the
  overflow is unreachable with the shipped data.
  **0 new ledger rows (226 on record), 89 markers.** 52 new checks (13942 total, 60 suites), coverage
  919/1103 (83.3%) and 496036/598792 (82.8%); `strat` moves to 194/204 functions and 117,032 of
  160,496 bytes (72.9%). IDB: 8 renames (426 on record), 6 size fixes (0x186BBC, 0x186BF0, 0x186C1C,
  0x186C20, 0x186E40, 0x159618), 3 function comments and 4 halving-site comments. The clock block base is corrected in the port too: it is
  `gUiRootPanel(0x158890) + 0xBE4 + 4 * 101 = 0x159608`, not the 0x158A14 `pufo_clock.h` claimed.

- [x] **part 83, the whole geoscape frame: `Geoscape_DrawScreen (0x6c16c, 3,476 B)`** — the caller of the
  two leaves part 82 ported. **The backdrop is the radar backdrop.** 0x6C1A4..0x6C340 is instruction-for-
  instruction the body of `RadarScreen_BlitMapBackdrop`: the same half-extents, the two map-pad blits, the
  three layer copies at `bias - scroll - halfW`, that minus `nMapW` and that plus `nMapW`. The port factors
  it into one `Smap_BlitWrappedBackdrop` helper rather than transcribing it twice.
  **`gOptionsScratch[0]` is the geoscape options menu id**, decoded from the shipped lang.dat strings rather
  than guessed: 0 options menu (TEXTS[1156] "Опции" + 1157/1158/1159 rows), 1 save slots (TEXTS[1160]
  "Выбрать запись"), 2 load slots (TEXTS[1161] "Выбрать загрузку"), 3 quit confirm (TEXTS[1162] + 1163 "Да"
  + 1164 "Нет"), 4 volume (TEXTS[1031]/[1032] captions with six snprintf'd digits at x 54 + 25*i); any other
  value reaches the tail only. `gOptionsScratch[4]` gates the whole layer — when it is zero the wash, the menu
  **and** the tail label are all skipped (0x6C64C `BEQ loc_6CEF0`). A new IDB enum `PufoOptionsMenuId` records
  the dispatch and `gOptionsScratch @ 0x159E88` now carries its layout.
  **The copy loops name their record through their offset.** The byte offsets are exactly `record * 205`:
  0x39CE7 = 1155, 0x39E81 = 1157, 0x39F4E = 1158, 0x33A68 = 1032, 0x3A41C = 1164. That is what pins each
  label to a record without reading the strings out of the IDB (the TEXTS labels there are zero-filled).
  **Hex-Rays mislabels a stack slot again.** 0x6C39C stores 0xB4 into the dword Hex-Rays calls `nKey`; it is
  `nH = 180`. The real `nKey` is the byte at 0x6C398, which `MOV R3, #0` sets to 0.
  **The shared tail.** Every case leaves its last string in `psz` and 0x6CEB4/0x6CEEC blits `pGeoscapeCtlA`
  and draws it centred at `120 - Text_MeasureWidth / 2` on `pGeoscapeCtlA->y + 1`. Cases 0..3 leave "Отмена",
  case 4 leaves "OK", and case 0 is the only case that blits `pGeoscapeCtlA` twice (0x6CE84 and the tail). The
  seven slot rows are **not** TEXTS: they come from `MainMenu_pSlotCtx @ 0x156938`, seven 40-byte labels.
  **1 deviation recorded, not hidden:** the original never initialises `psz`, and its switch default jumps
  straight to the tail, which then draws whatever the stack held at sp+0x18. The port initialises it, so the
  ids the game uses (0..4) are unchanged and an unknown id draws nothing instead of stack garbage.
  **How the new tests avoid testing nothing.** Every menu paints a solid wash behind its labels, so a lit-
  pixel count is satisfied by the panel alone — the first draft of this group had exactly that defect and a
  "no seventh digit column" check that the panel background made red. Each case is now run twice, once with
  the TEXTS record intact and once with its first byte zeroed, and the assertions are on the pixels that
  DIFFER. A mutation (title band moved from y 63 to y 80) turns the title check red, confirming the checks are
  position-sensitive. Measured along the way: `Text_DrawString(x, y)` inks rows y+3..y+12 with this font.
  **0 new ledger rows (226 on record), 89 markers, 0 renames (426 on record).** 77 new checks (14019 total,
  60 suites), coverage 920/1103 (83.4%) and 499512/598792 (83.4%); `strat` moves to 195/204 functions and
  120,508 of 160,496 bytes (75.1%). IDB: 14 line comments on the label sites, one on `gOptionsScratch`, one
  function comment at 0x6c16c, and the `PufoOptionsMenuId` enum.

- [x] **part 84, the geoscape pulse tick and the map-target arm: `Geoscape_PulseUpdate (0x954dc, 1,812 B)`
  + `Smap_SelectMapTarget (0x84484, 340 B)`** — **The tick has three halves that do not depend on each
  other**, so the tests drive each alone: the map-screen craft pass, the phase-gated screen switch, and
  four saturating counters. **What the ARM fixes that the pseudocode flattens.** `Map_nCraftCount` is read
  with **LDRB** (0x95524, 0x955F4) — an unsigned byte, not a dword. And the empty-craft exit is
  `CMP R3,#0 / BLS loc_9560C` at 0x9552C, which jumps **into** the clock block rather than past it, so an
  empty craft list still advances the clock; the pause byte 0x15961C gates the clock, not the craft pass.
  **The phase gate is not a counter.** If `nUiSlot0AnimPhase` is already above 1 the whole switch is
  **skipped** (0x95644); otherwise it is incremented and the switch runs only when that lands above 1 — so
  the switch fires exactly when the phase arrives as 1, and LABEL_88 resets it on every path. **The held-
  direction scroll is absolute, not incremental.** The four flag bytes `unk_1864F4..F7` — renamed
  `bGeoscapeScrollUp / Right / Down / Left` — give +25 on dy, +25 on dx, −25 on dy, −25 on dx (5 on screen
  2, 0x95844); `nLastDx / nLastDy` are preset 120 / 160 and the call is `Smap_ScrollBy(dx + 120, dy + 160)`,
  which is what `Smap_ScrollBy`'s `pos - delta + last_delta` form expects. `Smap_ScrollBy` skips its
  vertical clamp when the screen id is 2, so the same starting scroll clamps on the map screen and does not
  on the zoom screen — both directions asserted. **A forced click only fires on a lit control**: the
  auto-repeat path needs mode 0 and state 1, so an unlit control in the group is silent. **The craft pass
  is not the alien-site discovery pass** — it makes an INACTIVE type-1 entry visible, and type 1 is the
  player's own craft record; the active entry with the same id and the inactive type-0 entry are both
  asserted untouched. **`Smap_SelectMapTarget` is the other end of unit 83's wire**: it SETS
  `bMapSelectionFlag` (0x84498), the flag `Geoscape_DrawScreen` tests at 0x6C180, and it is the map-target
  arm that PAUSES the geoscape with no unpause at all (0x84588). Its tail reads the line colour a FOURTH
  time, after the insert and the removal, so line 0 is the flag-27 line the insert just put there and both
  0x187054 and 0x159E74 receive the value the insert carried. **1 deviation recorded:** the original
  indexes `apCraft` with the line colour with no bounds test (0x844BC); the port checks it and skips the
  two ship steps while the line rewrite and the pause still run. **And a fixture lesson worth recording:**
  `PtrArray_DeleteAt` frees the item it drops and `PtrArray_Remove` frees the slot array and installs a
  freshly malloc'd one — a test that hands in a static array crashes, and one that keeps a second handle on
  the items double-frees after a removal. 65 new checks; mutating the port's zoom step from 5 to 25 turns
  two of them red. **0 new ledger rows (226 on record), 89 markers.** 4 IDB renames (430 on record), 2
  function comments, 9 line comments. 14084 checks (60 suites), coverage 922/1103 (83.6%) and 501664/598792
  (83.8%); `strat` moves to 197/204 functions and 122,660 of 160,496 bytes (76.4%).

- [x] **part 85, the new-base globe hover frame: `Geoscape_DrawGlobeHover (0x6727c, 2,208 B)`** — **The
  crosshair, the scan animation, the two random readout lines and the terrain / cost / Build base labels
  over the hovered region.** The wash key is `Game_RandU16(10)` with every roll below 8 collapsed to 0
  (0x672B4), so only 8 and 9 survive and key 0 leaves the surface alone — that is how the globe shows
  through. The bottom panel gates the rest of the frame: while `Ui_AnimExpandBlit` mode 3 is incomplete the
  function returns at 0x67490 and the scan animation, the readouts, the labels AND the counter advance
  never run. The three counters step after the drawing, `nGlobeScanX` zeroes past 320, and
  `nGlobeSweepV + 1 > 40` zeroes both sweep counters from one CMP at 0x67694. **The four label sources are
  TEXTS records, not literals:** 0xE7B3A / 0xEC680 / 0xEC74D / 0x103C77 sit past `.data`'s initialised span
  (929,136 virtual bytes, 26,624 raw — everything from 0xAC800 up is zero-filled at load) and each is an
  exact multiple of 205 past `aTextRecs`, i.e. records 1042, 1136, 1137 and 1603; out of lang.dat they read
  "Can`t build here", "Terrain: ", "Base cost: " and "Build base". TEXTS[1042] is also what
  `TEXTS[905 + 0x89]` names, which is why ocean and region 0x89 read identically and
  `aNewBaseCostByRegion[9] == 0`. **The halving triple appears twice more** (0x6733C, 0x67884) and both are
  plain C `(320 - uHeight) / 2`; Hex-Rays' `321 - uHeight` for a tall strip is the same truncation, not a
  special case. **1 deviation recorded:** 0x67980 indexes `aNewBaseCostByRegion[tile - 0x80]` with no range
  test; the port skips the cost line for an out-of-range tile. 108 new checks; two mutations were run — the
  tick base 3 → 4 turns five red, the horizontal sweep gate 15 → 16 turns two red. Two helper bugs found
  while writing them are recorded in the parity doc because either would have made a whole group vacuous.
  **0 new ledger rows (226 on record), 89 markers.** 9 IDB renames (439 on record), `aNewBaseCostByRegion`
  retyped to `u32[10]`, 1 function comment, 9 line comments. 14192 checks (60 suites), coverage 923/1103
  (83.7%) and 503872/598792 (84.1%); `strat` moves to 198/204 functions and 124,868 of 160,496 bytes
  (77.8%).

- [x] **part 86, the plane screen's stat rows: `BasePlane_ResetDetailState (0x79cb8, 328 B)`** — **Six
  craft-weapon counts rebuilt for the selected base, and a row count that starts at 1.** 0x79D20 stores 1
  into `nPlaneStatRowCount`, not 0 — the panel always has its column-header row, which
  `BasePlaneScreen_DrawStatLabel` (0x79e00 / 0x79fc4) sizes its three nested rects with. **The loop reads
  every slot and keeps six:** 0x79D48 walks 0..`Map_nObjectCount`-1 and evaluates
  `apObjects[i]->auPerBaseVal[nSelectedBaseIdx]` for all of them (the LDRH at +0x2A is outside the
  switch), but only slots 25..30 have an arm and the default arm does not bump the row count. The shipped
  OBJECTS.DAT makes the window concrete — 25 Craft cannon … 30 Fusion ball launcher, their names exactly
  208 bytes apart in main.dat, with Psi-arm at 24 on one side and the five tank/hovertank weapons at
  31..35 on the other. **The six counts are dwords** (every store STR, every read LDR) and each gates its
  row's control in `Geoscape_OnPointer` (0x88674..0x88730) and `Geoscape_OnPointerUp` (0x8a38c..0x8a460).
  **One global, two unrelated uses:** `nPlaneStatRowCount @ 0x161A38` is also the start-menu flicker's
  scratch (0xA0AEC stores `nMapPhase` into it, 0xA0B20 writes `it * 32 - 1` into `MainMenu_anRowKeys[i]`);
  the port's `g_nKeyAccum` is renamed `g_nPlaneStatRowCount` so the alias is visible. **1 deviation
  recorded:** 0x79D5C indexes `auPerBaseVal` with a LDRSB'd signed byte and no range test, so an
  out-of-range index reads inside the record (`auPerBaseVal[-1]` is the u16 at +0x28, `[8]` is
  `aItemQty[0]`); the port refuses and reaches the same six-zeroes-one-row result. 49 new checks; two
  mutations — first weapon slot 25 → 24 turns 13 red, dropping the non-zero filter turns 4 red. The guard
  is tested by planting values exactly where the unchecked read would land, and the last case re-runs the
  same object with a valid index to prove those zeroes came from the guard. 7 IDB renames (446 on record),
  new enum `PufoCraftWeaponSlot` (25..30 plus 31..35), 1 function comment, 6 line comments. 14241 checks
  (60 suites), coverage 924/1103 (83.8%) and 504200/598792 (84.2%); `strat` moves to 199/204 functions and
  125,196 of 160,496 bytes (78.0%), leaving `Geoscape_Draw`, `BaseFac_BuildStatusMsg` and three 12-byte
  atexit registrars.

- [x] **part 87, the geoscape status line: `BaseFac_BuildStatusMsg (0x6a4bc, 1,520 B)`** — **Thirteen arms
  on a character code, 0x20..0x2C.** `GameTime_Pause` runs at 0x6A4D4, *ahead of* the switch, so it
  happens on all thirteen arms and on the default. The only call site (0x8C82C, inside the unported
  geoscape dispatcher) builds every argument out of **line 0 of the root message list**: the case byte is
  that line's flag, the base index is its colour dword slot 2, the row index slot 0 and the column index
  slot 1; the dispatcher switches on the same flag again at 0x8C83C and draws the two lines. `'!'`
  (0x6A5A0) and `'"'` (0x6A610) are the same arm twice, and `'$'`/`')'`/`'*'` all jump to one tail.
  **The six label prefixes are TEXTS records** 1334/1339/1340/1341/1342/1343 — "Base: ", the same word
  with a space before the colon, "Production: ", "Research: ", "Module: ", "Craft: ". **Getting the
  record-0 offset right mattered:** the two candidates in lang.dat are 0x102 and 0x1CA, exactly one
  record apart, and the wrong one still yields a plausible string at every index. Settled against the
  shipped facility names — 106 Access Lift … 117 Hangar, 118 Gravity shield, 463 Missile Defences, 122
  Fusion Ball Defence, the exact `g_anFacNameRec` order `Geoscape_InitUI` copies into `aBaseFacFlags`.
  **`'&'` reads the facility grid** at `abFacilityGrid[480 + 6*(24*base + row) + col]` — +480 is row 18 of
  the eight 144-byte 24×6 blocks that start at +372, a different window from the hangar arm's rows
  12..17. **`'%'` reads the topic byte one record in** (`LDRB [R7,#0x22]` on a 34-byte record), the same
  off-by-one-record pairing `ResearchScreen_RebuildTopicList` uses. `szScratch186488` carries a name
  between calls (`'\'`/`'('` write it, `','` reads it), and 0x10DA38 is inside `.data`'s zero-filled span,
  so the 255 sentinel copies a single NUL — a clear, not a message. **1 deviation recorded:** unbounded
  copies and untested indices; the port bounds both. 54 new checks; three mutations — prefix indices
  shifted by one record (the exact mistake made while writing this unit) turns 31 red, the hangar grid
  offset turns 4 red, reading the topic byte in place turns 1 red. 6 IDB renames (452 on record), the
  function's five arguments named, 1 function comment, 7 line comments, plus a comment at `aTextRecs`
  recording the record-0 offset and its proof. 14295 checks (60 suites), coverage 925/1103 (83.9%) and
  505720/598792 (84.5%); `strat` moves to 200/204 functions and 126,716 of 160,496 bytes (79.0%), leaving
  `Geoscape_Draw` and three 12-byte atexit registrars.

- [x] **part 88, the ufopaedia's paging and entry layout: `Ufopaedia_RefreshEntry (0x9a014, 1,528 B)`,
  `Ufopaedia_PrevEntry (0x9a60c, 328 B)`, `Ufopaedia_NextEntry (0x9a754, 364 B)`** — **Two tables drive
  the screen.** `aUfopaediaCatTopics @ 0xAA830` (IDA's `aFghijklmnop`, named after row 0's bytes happening
  to be ASCII `'f'..'p'`) is 9 rows of 22 research topic numbers, and row 5 is the only row that fills all
  22 slots — which is the only reason the `++idx == 22` test exists. `anUfopaediaEntryNum @ 0xAA5F0` maps
  topic → entry number, with topics 47/94/95/96/143 unmapped and the race topics 118..142 collapsing onto
  the five race entries 54..58. **The category panels reuse UiPanel's own storage**: `panel + 4*(n+1261)` is
  `anLineAux[n]` (4×1261 = 5044, anLineAux's offset), so apLines holds the entry name, anLineAux the topic,
  nLineCount the count and nUnk3032 the selected row — no new structure. `dword_1871D0` is nine panels plus
  slot 9 (0x1871F4, the entry index, previously unnamed) plus slot 10 (the category byte). **PrevEntry and
  NextEntry are not symmetric:** NextEntry stops on the table's zero terminator (0x9A7A8), PrevEntry never
  consults the table at all and walks the index to −1 (0x9A640), relying on the panel's line count. **The
  unlock gate's second read is `abResearchZero[topic]`, not the decompiler's claim:** `LDRB R1,[R1,#0x1C24]`
  off the `aResearchTopics` anchor is 0x160B2C + topic, while Hex-Rays renders it as
  `aResearchTopics[211].abActivateTopics[topic+1]` — the same address, one byte late, reading the *next*
  topic's mirror. **The wrap loop at 0x9A308..0x9A4D0 is a byte-for-byte inline clone of
  `UiPanel_WrapText @ 0x37144`**, so the port calls the shared function rather than duplicating it. **The
  layout cascade reduces to four bands** (≤1000 → 220 px/style 7/y 230; 1000..2000 → 220 px + flag;
  2000..3000 → 180 px/style 13/y 50; >2999 → 200 px/style 8/y 110), with entry 1000 and entry 2000 both
  keeping the default and entry 3000 landing in the 200 px band; four further tests set only the flag, and
  93..97 also indents the text by seven lines. The description is `TEXTS[4*topic + 534..537]`, and the pad
  is a toggle — paging clears `bUfopaediaEntryDirty` so its refresh builds the pad, any other refresh frees
  it. **4 deviations recorded:** the four description records can need 816 bytes of an 800-byte buffer, the
  pad pointer is left dangling after delete, `PadImage_Load` is called on a possibly-null pad, and neither
  paging function bounds its indices. 107 new checks; seven mutations — dropping NextEntry's terminator test
  (3 red), giving PrevEntry that test (9 red), reading the mirror byte one late exactly as Hex-Rays claims
  (2 red), making the >2999 boundary exclusive (3 red), shifting the description window one record early
  (6 red), removing the indent (2 red), wrapping the category the wrong way (3 red). 11 IDB renames (463 on
  record) including two retyped tables and a new symbol at 0x1871F4, 3 function comments, 7 line comments.
  14402 checks (60 suites), coverage 928/1103 (84.1%) and 507940/598792 (84.8%).

- [x] **part 89, the ufopaedia's builder and teardown: `Ufopaedia_Open (0x9a8cc, 3,368 B)`** — **One
  function, two jobs, on a toggle.** The build arm sets `g_bUfopaediaScreenBuilt` and *returns from inside
  its last loop* (0x9B4FC), so the release code that follows the whole build block only runs on the next
  call — calling it twice is open-then-close. **The nStyle clamp lands on nStyle only:** each category panel
  is constructed with the full shown count and only `nStyle` is clamped to 13 (0x9AE44), so category 5 keeps
  all 22 line buffers and pages through 22 entries while showing 13 — and that full count is what unit 88's
  paging filter compares against. **The shown test is evaluated twice** (a counting pass at 0x9AB04 that
  sizes the panel, a filling pass at 0x9B0C4 that writes the names): `(bUnlocked && topic != 0) ||
  raceOverride`. **The race lists are hand-written and two are incomplete:** the 25 encounter topics
  118..142 form five families by residue mod 5, but Sectoid (118/123/128/133 + 138), Snakeman (121/126/131/136
  + 141) and Floater (124/129/134/139 + 119) are complete while Ethereal never mentions 142 and Muton never
  mentions 135 or 140 — so researching 142, 135 or 140 shows nothing. **Opening the screen writes the
  research mirror:** 0x9AD68 stores 1 into `abResearchZero[topic]` for a met race, which is *why* the paging
  gate at 0x9A6D4 accepts on that byte at all — a race met in the field becomes pageable without its own
  topic being unlocked. Row names come from `TEXTS[aResearchTopics[topic].wGate4]` and the five race rows are
  then overwritten with literal names; the selector's nine rows are *cleared*, not labelled (the nine sources
  at 0x105EE6..0x10654E sit in `.data`'s zero-filled span). **1 original bug found:** the teardown's control
  loop starts at 0x1872E0 and reads slot +2 six times, releasing 0x1872E8..0x1872FC and never 0x1872E4 — the
  live entry's PadImage — so closing the ufopaedia after any entry has been shown leaks it; ledger row added,
  not reproduced. **4 further deviations:** unguarded dereferences of freshly allocated panels, an unbounded
  name copy into a 50-byte row, an unbounded row index, and `nLineCount = 50` written before the null test at
  0x9B550. 125 new checks; nine mutations — completing Ethereal's family (1 red), completing Muton's (2),
  removing the mirror write (1), raising the clamp to 22 (2), 16 instead of 8 px per visible row (10),
  skipping the race-name overwrite (9), not freeing the entry pad (1), building the panel with the clamped
  count (4), labelling the selector rows (1). 3 IDB renames (466 on record) including `byte_160B2C` →
  `abResearchZero`, 1 function comment, 9 line comments, 1 new ledger row (227), 90 bug markers. 14527 checks
  (60 suites), coverage 929/1103 (84.2%) and 511308/598792 (85.4%); `strat-ui` to 228/245 and 89,860 of
  121,164 bytes (74.2%), with the ufopaedia family now complete.

- [x] **part 90, the psi screen: `PsiScreen_BuildPanels (0x716d4, 2,940 B)`, `PsiScreen_SelectListed
  (0x71298, 848 B)`, `PsiScreen_Init (0x72b40, 332 B)`, `PsiScreen_ApplyAssignments (0x72250, 328 B)`** —
  **Four parallel per-base columns**, each an array of eight `UiPanel *`: name (`g_apPsiNamePanel`),
  "auStat[6]\auStat[7]" (`g_apPsiStatPanel`), the training delta (`g_apPsiDeltaPanel`) and the assignment
  box (`g_apPsiCheckPanel`). The middle two are the dword runs at `byte_15EE84+4` and `+36`, and that
  72-byte block also holds the row-selected flag at +0 and a scratch row index at +68 — the port splits
  it into four named arrays plus two scalars. **BuildPanels is a build/release toggle on `g_bPsiPanelOpen`
  like `Ufopaedia_Open`**, in three passes: count each base's soldiers (0x71734), build four panels sized
  to that count (0x7178C), fill the rows reusing the count array as the cursor (0x71BA8). **Assigning a
  soldier is what makes them psi-capable**: 0x71D00 sets `Soldier.bPsiActive` for every assigned row
  before the stat columns and the countdown read it. **The stat columns are gated on `bPsiActive` alone**,
  not on the assignment (0x71E2C). **The training countdown** arms 30% of `auStat[7]` days when
  `auStat[6]` is below it and exactly **one** day when it is already at or above (0x7209C), which is what
  `Geoscape_TickBases` spends one per day — the gain the "+N" column advertises. **The checkbox rows are
  cleared, not labelled** (both label sources sit in `.data`'s zero-filled span). **The assigned counter
  compares against 1 exactly** (0x71314). **ApplyAssignments closes the screen**: its tail call at 0x72394
  reaches BuildPanels with the flag still set. **1 original bug found, reproduced and pinned:** 0x72370
  calls `Base_UpdatePsiLabNeeded` without ever loading its third argument, so the budget is the inner
  loop's exit value — the base's whole row count, not `Base_CountPsiSoldiers` as at the two other call
  sites. **6 deviations:** unguarded dereferences of the freshly built panels, unguarded and un-null-checked
  `Map.apSoldiers` indexing, unbounded `itoa`/`strcat`/`strcpy` into 50-byte rows, an unbounded row cursor
  at 0x712EC, the port's 500-line clamp, and a return value no caller reads. 121 new checks; fourteen
  mutations turn 8, 6, 11, 3, 4, 1, 5, 4, 5, 2, 1, 6, 4 and 1 red. 8 IDB renames plus 5 new symbols and the
  three `Soldier` psi members renamed (466 → 476 on record), 4 function comments, 11 line comments, 1 new
  ledger row (228). 14648 checks (60 suites), coverage 934/1103 (84.7%) and 515852/598792 bytes (86.1%).

- [x] **part 91, the psi screen's frame: `PsiScreen_Draw (0x72398, 1,960 B)`** — the whole body sits behind
  `g_bPsiPanelOpen` (0x723B8), so a closed screen draws nothing at all. **The rails are the shared
  `BaseUi_DrawFrameRails`** (0x723C8..0x72558), the same block Transfer, Manuf and Research each carry.
  **Four captions, and an earlier IDB note about them was wrong**: it called them scratch buffers nothing
  writes, but all four are TEXTS records and the shipped lang.dat has text in every one — TEXTS[1188] the
  column captions at (7, 54), TEXTS[1189] centred at (60, 300), TEXTS[1127] "ОК" centred at (180, 300),
  TEXTS[1190] centred at (120, 158). Centring is `centre - (width >> 1)`: the `ADDMI` at 0x7290C never
  fires on a positive width, so the `ASR #1` at 0x72910 is a plain floor. **Overlay** (byte_1870F8): the
  full-screen blend, a 32-wide erase strip at x 208 of height `28*nBaseCount + 4`, `Base_DrawFacilityIconStrip`
  with the mark argument **-1** so the drawer paints no chevron of its own, the 210-wide panel, then one
  bevelled row per base with its name centred at (110, 28*i + 7); the row count is the signed byte at
  `abFacilityGrid + 0x618`. **A base whose `Base_CalcPsiLabCap` is 0 gets the 23-step white bracket** at
  x 212..234 — the same bracket the strip drawer paints for a marked base, here marking the ABSENCE of a
  psi lab. **Tail**: `byte_158834` against `byte_1870F4` signed; equal or overlay closed packs the tail
  colour pair and, with `dwActionFail == 1` and `dword_1870B4[0] == 0`, runs the shortfall panel and its
  caption; different, `Base_CalcPsiLabCap` either adopts the base (0x72A28 / 0x72A2C) or does nothing at
  all. **5 deviations:** caption and name sources passed in rather than reached as globals, a bounded base
  index for the title, two seams where the original calls directly, and four guards the original lacks.
  82 new checks; 32 mutations turn at least one red, and one (the 210-wide panel rect narrowed to 200) is
  provably invisible because the 32-wide erase strip covers exactly the difference. 3 new IDB symbols
  (476 → 501 renames on record), 10 line comments, and the corrected function comment at 0x72398. 14730
  checks (60 suites), coverage 935/1103 (84.8%) and 517812/598792 bytes (86.5%); `strat-ui` to 234/245.

- [x] **part 92, the base-info screen's panels: `BaseInfoScreen_Init (0x57688, 312 B)` and `BaseInfoScreen_BuildPanels (0x557f4, 6,084 B)`** — Init takes the address of the three-slot block at 0x158828: the screen pad (`"baseinfo"`, mode 14, origins 0), the OK control and the panel-mode toggle, then `unk_186B00` to (224, 1), `byte_1870F4 = nSelectedBaseIdx`, `byte_1870F8 = 0` and the build as a tail call. **Every label on this screen is a clear, not a string**: the sources 0x10DA38, 0x104C7B..0x105C7F, 0x105D4C and 0x105E19 all sit above 0xAC800 — past the end of the `.data` file image (vsz 0xE2D70 against rsz 0x6800) — and none has a write xref, so each copy writes an empty string. **Hex-Rays is wrong about the craft cost loop**: it renders the aux-0 case as leaving the loop, but the `break` only leaves the switch — 0x56810 writes "500000$", 0x56838 adds 500000 and the test at 0x56858 continues, so aux 0 is priced like aux 1 and only the total column's marker ending stops the run. **`Craft_CountSoldiersAboard` here is fed the craft ID** (the free column's aux holds `Ship.bId`), the only call site in the binary that does not pass the roster index. The row map is reproduced in full: the free column's capacity deltas at 6..10, the fixed unit prices at `nShips+19/20/21`, the facility total at `nShips+23` (50000 per type byte != 0xFF over the same 36 cells every capacity walk uses) and the grand total at `nShips+25`; the total column's crew, capacities and money breakdown. The tail rows move with the craft count, which the no-craft case pins. **6 deviations:** the port nulls the freed panel and control slots the original leaves dangling, guards the three panel pointers and bounds every row index, and substitutes `snprintf` for `j_itoa`. 135 new checks; 60 mutations turn at least one red, and four (the clear of name row `nShips+17`, the eight-row clear count, the 42-row clear count and the kind-4/kind-5 re-clear) are provably invisible because each writes an empty string over a row that is already empty. 5 new IDB symbols (501 → 506 renames on record), 4 function/line comments including the aux-0 correction at 0x56810 and the id-vs-index note at 0x6649c. 14865 checks (60 suites), coverage 937/1103 (85.0%) and 524208/598792 bytes (87.5%); `strat-ui` to 236/245.

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

- [ ] **The dormant `sizeof(MapCell)` assert is wrong, and the original's cell is 62 bytes.**
  `src/pufo/game/pufo_mapcell.h` asserts `sizeof(MapCell) == 60` inside
  `#if UINTPTR_MAX == 0xFFFFFFFFu`, so it never runs on the Win64 build. It would fail if the port were ever
  built 32-bit: the port's own layout ends at `bFlags57[4]` (0x39..0x3c), which rounds to 64 with 4-byte
  pointers. The IDB's `MapCell` has the same 23 members and reports size 62, i.e. two bytes past the last
  named field. Nothing is hurt today — `Map.apCells @ 0x15338` is a `MapCell *[4][60][60]` pointer table and
  cells are never serialised — but the assert states an invariant that is not true, and the two unnamed bytes
  at 0x3d/0x3e have never been accounted for. Worth a pass that either finds what reads them or deletes the
  assert.

- [ ] **`Tact_KillUnit` removes a dead unit from the side lists without writing the counts back.**
  `src/pufo/game/pufo_unit.c` builds `SideLists lists = Kill_SideLists(pCtx->pRosters)` twice (the chryssalid
  transform at ~1758 and the death path at ~1795) and calls `SideLists_RemoveUnit(&lists, pUnit)`, but
  `SideLists` carries `nXcom` / `nEnemy` **by value** and nothing writes them back to the canonical
  `nXcomUnitCount` / `nEnemyUnitCount`. The array is compacted while the count stays put, so the roster keeps
  a duplicated pointer in its last slot and the count is one too high. `Tact_ProcessFallAction` gets this
  right through `Fall_SyncCounts`, and `Tact_UpdateUnitControl` now does the same through
  `Turn_SyncSideCounts`; the kill path does not. Fixing it means deciding whether `KillCtx.pRosters` should
  own the counts or whether `Tact_KillUnit` should sync them, so it is its own step.

- [ ] **`Ui_ConfirmFireBurst` (0x18774) drops the out-of-TU message its callee pushes.** Found while wiring
  the three new enqueue call sites in part 68. In the original the message is the callee's own work:
  `TactState_EnqueueFireBurst @ 0x4B0D8`, `TactState_EnqueueThrow @ 0x4B318` and
  `Tact_EnqueueActionOrStatus205 @ 0x4B420` each push `aTextRec1_OutOfTime` (record 1) when the unit cannot
  pay. The port's three helpers return false instead and the header says the push belongs to the caller, so a
  call site that ignores the return loses the message. `Tact_HandleMapClick`'s latch-1 and latch-2 arms and
  `Ui_ConfirmMeleeMode6` / `Ui_ConfirmSpecial9` now push it; `Ui_ConfirmFireBurst` still returns false with no
  push. Fixing it is a one-line change plus a test, but it changes what `test_tactui` asserts about the
  refusal, so it is its own step.

- [ ] **`PUFO_MAP_MULTI_TARGET_MAX` is 10 but `Map.apMultiTargets` is eight wide.** `src/pufo/game/pufo_mapctx.h:46`
  defines it as `PUFO_AIM_SAMPLE_COUNT` (10, from `pufo_ballist.h:77`), while the Map block gives the array
  eight entries (`Map.apMultiTargets @ Map+0x08`, `TactInputState.apMultiTargets[8]`) and
  `Tact_HandleMapClick @ 0x15328` refuses the ninth with `CMP #8 / BGE`. The two constants describe different
  things — the ballistician's ten arc samples and the multi-target cell list — and sharing a value makes one
  of them wrong. `pufo_input.h` now carries its own `PUFO_MULTI_TARGET_MAX 8` rather than propagate the
  mismatch; resolving it means checking every reader of `PUFO_MAP_MULTI_TARGET_MAX`.

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
- [ ] **The same product column is spelled two ways across the manufacturing envs.** `aManufProductB
  @ 0x15B79C` is `u32 *apProductB` in `ManufBuildEnv` and `i32 *apProductB` in `ManufClearEnv`,
  `ManufCommitEnv`, `ManufQtyEnv` and now `ManufDrawEnv`. The IDB says the column is u32. Nothing is
  wrong today — the values are small unit counts and the tests keep one buffer per spelling — but the
  day the shell builds one buffer and hands it to both families it needs a cast, and `TestManufDraw`
  already carries that cast. Worth settling once, in one direction, rather than at every call site.

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
- [ ] **The research entry count is modelled as `u16` where the original has a byte.** Found in part 43:
  the ARM reads the count at `0x160C4C` with `LDRB` (`LDRB R3, [R2, #0x1D44]` over the 0x15EF08 anchor,
  and `LDRB` again at 0x74B14), and `Geoscape_InitUI` writes it with `STRB R1, #0x90` — the port's own
  comment at `pufo_geoiinit.c:243` already says STRB. But `ResearchQtyEnv`, `ResearchAccumulateEnv`,
  `SaveGameEnv` and `GeoiinitEnv` all type it `u16 *`, and `pufo_world.h`'s `nResearchEntries` is a u16,
  so the port reads two bytes where the original reads one and writes two where it writes one. It is a
  modelling inaccuracy rather than a behavioural bug today — the value is 144 and the port's variable is
  its own — but it is wrong, and `ScreenResetOpenEnv.pbResearchCount` already models it correctly as
  `const u8 *`, so the two spellings disagree inside the same header. Fixing it is a type change across
  four envs, `pufo_world.h` and the tests that fill them; it is its own step, not part of a port.
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
  - **Part 42 found the middle path, and it is worth reusing.** `TestManufDrawFrame` draws
    `ManufScreen_DrawFrame` onto a real 240 x 320 16 bpp surface with the real `FONT.SPR` and the real
    TEXTS layout, and asserts exact 565 values at the coordinates the original names — including a
    pixel-for-pixel comparison of a label band against a reference built from the same frame plus the
    expected string. That is not the game UI and it does not lift the blocker above, but it is a real
    check of a drawing function rather than a dump of synthetic primitives, and it caught two wrong
    expectations of mine (the rail overlap at y 151, and the `nStyle` cap on the separator height) that
    no amount of reading would have caught. Any ported draw function whose callees are already ported
    can be tested this way.
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
  an unported callee inflates the headline number. `audit_names.py` reports the live list, and it is
  currently empty: 0 credited addresses whose original name is not defined. The five that used to be on
  it — `Geoscape_OnPointer @ 0x872d0`, `Geoscape_OnPointerUp @ 0x89450`, `Ui_TextInputDialog @ 0x632cc`,
  `Geoscape_CloseUI @ 0x8b94c` and `SoldierScreen_Draw @ 0x65290` — are either ported now or cited only
  through a named seam. New code writes unported callees
  as `Name (0xADDR)` so they stay out of the count, and `audit_credits.py` now reports zero negated
  citations. The
  `pufo_uibank.c` handler-name table is the same class of credit: it quotes the original bank entry's
  name and address as data, which counts even when only the name string is recreated.
- [ ] **The on-disk `ResearchTopic` field order still has no mapping onto `ResearchTopicLive`.** Part 45
  narrowed it without closing it. The in-memory record now has four evidenced 5-byte groups —
  `abNeedItems` (+0x0E), `abPreTopics` (+0x13), `abItemIds` (+0x18), `abActivateTopics` (+0x1D) — and the
  save chunk has exactly four 5-byte arrays (`abUnk10`, `abUnlock`, `abUnk20`, `abUnk25`), so the counts
  match; which on-disk array becomes which in-memory group is still unproven, and `wProgress` sits at
  on-disk +0x1D but in-memory +0x0A. Closing it needs `LoadGame @ 0x44bbc` (0x45364/0x453EC are its two
  `aResearchTopics` references), which is not ported. Until then the port keeps the two layouts as
  separate types and does not pretend one converts into the other.
- [ ] **`Text_DrawString` paints with the SECOND triple of the packed colour pair.** Part 46 pinned it on
  the real surface: the "white" pair 250,250,250 / 0,0,0 draws black glyphs, and the research screen's
  teal captions come out as 4,70,75 rather than 68,244,252. The research frame test builds every
  reference with all six values; `TestManufDrawFrame` and `TestTransferDraw` still pass a bare triple, so
  their colour claims are weaker than they look. Worth its own pass over those two older real-surface
  tests rather than a fix folded into a porting step.

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
| the daily build-timer countdown (`Base_TickFacilityConstruction`, the hangar's three extra clears, the message payload corrected) | 56/56 (Release) | 10016 | 857/1103 (77.7%) | 360352/598792 (60.2%) | 38 checked, 0 mismatched |
| the manufacturing refresh (`ManufScreen_RefreshBaseTabs`, the untested hangar products, the early return) | 56/56 (Release) | 10041 | 858/1103 (77.8%) | 360852/598792 (60.3%) | 38 checked, 0 mismatched |
| the craft slot -> CraftType map (`Ship_ProductSlotToType`, renamed in the IDB, shared with the recovery pass) | 56/56 (Release) | 10059 | 859/1103 (77.9%) | 360940/598792 (60.3%) | 38 checked, 0 mismatched |
| the message-payload audit (38 and 32 corrected, five other sites re-checked, 39's radar byte recorded) | 56/56 (Release) | 10062 | 859/1103 (77.9%) | 360940/598792 (60.3%) | 38 checked, 0 mismatched |
| the manufacturing commit pass (`ManufScreen_CommitProduction`, the work-per-unit reading, the blocked early return) | 56/56 (Release) | 10123 | 860/1103 (78.0%) | 362348/598792 (60.5%) | 38 checked, 0 mismatched |
| the manufacturing bring-up (`ManufScreen_Init`, the six control descriptors, the shared geoscape controls) | 56/56 (Release) | 10173 | 861/1103 (78.1%) | 362836/598792 (60.6%) | 38 checked, 0 mismatched |
| the manufacturing panel build (`ManufScreen_BuildPanels`, the research-driven row filter, `aFacTypeTable` found to be the research table) | 56/56 (Release) | 10222 | 862/1103 (78.2%) | 365240/598792 (61.0%) | 39 checked, 0 mismatched |
| the manufacturing draw click (`ManufScreen_Draw`, the seed rule, the dead hangar branch found) | 56/56 (Release) | 10264 | 863/1103 (78.2%) | 366572/598792 (61.2%) | 39 checked, 0 mismatched |
| the manufacturing frame (`ManufScreen_DrawFrame`, the 0.7 price factor, the first real-surface pixel test) | 56/56 (Release) | 10332 | 864/1103 (78.3%) | 371972/598792 (62.1%) | 39 checked, 0 mismatched |
| the research bring-up (`ResearchScreen_Init`, the four descriptors, the handlers-before-positions order) | 56/56 (Release) | 10386 | 865/1103 (78.4%) | 372564/598792 (62.2%) | 39 checked, 0 mismatched |
| the research panels (`ResearchScreen_BuildPanels`, the divide-first percentage found, the topic table as the screen block) | 56/56 (Release) | 10434 | 866/1103 (78.5%) | 374164/598792 (62.5%) | 39 checked, 0 mismatched |
| the research rebuild (`ResearchScreen_RebuildTopicList`, the topic→facility-kind permutation, the wrong-column clear found) | 56/56 (Release) | 10520 | 867/1103 (78.6%) | 375864/598792 (62.8%) | 39 checked, 0 mismatched |
| the research frame (`ResearchScreen_Draw`, the pad inside the topic table, the close-control-only-when-no-row quirk, the rails deduped) | 56/56 (Release) | 10589 | 868/1103 (78.7%) | 377972/598792 (63.1%) | 39 checked, 0 mismatched |
| the buy/sell frame (`BuySellScreen_Draw`, the two halves that draw differently, the freed-panel deref found) | 56/56 (Release) | 10674 | 869/1103 (78.8%) | 381496/598792 (63.7%) | 39 checked, 0 mismatched |
| the radar backdrop (`RadarScreen_BlitMapBackdrop`, the phase toggle, the world-bits pointer misreading fixed) | 56/56 (Release) | 10723 | 870/1103 (78.9%) | 382004/598792 (63.8%) | 39 checked, 0 mismatched |
| the radar gauge and range bar (the consumed percentage, the tens-digit branch shape, the level term that loosens the gate) | 56/56 (Release) | 10789 | 872/1103 (79.1%) | 383092/598792 (64.0%) | 39 checked, 0 mismatched |
| the radar screen session (`RadarScreen_Init`, `RadarScreen_SetActive`, `RadarScreen_RollDetection`; the eight-row class table, the ammo draw and write-back, the slot-2 overrun found) | 56/56 (Release) | 10983 | 875/1103 (79.3%) | 387488/598792 (64.7%) | 39 checked, 0 mismatched |
| the radar weapon mounts (`RadarScreen_DrawCraftArmament`, the mount-id enum, the cannon that draws no bar, the duplicated glyph-palette global fixed) | 56/56 (Release) | 11052 | 876/1103 (79.4%) | 389180/598792 (65.0%) | 39 checked, 0 mismatched |
| the intercept shot animation (`RadarScreen_DrawOverlay`, the per-weapon flight gate and step, the laser that never resolves past 2, the runaway -2 ammo counter found) | 56/56 (Release) | 11141 | 877/1103 (79.5%) | 391968/598792 (65.5%) | 39 checked, 0 mismatched |
| the radar frame driver (`RadarScreen_Update`, the same-frame-only damage ladder, the unreachable `Ship_ClearInterceptByStrategIdx` found) | 56/56 (Release) | 11223 | 878/1103 (79.6%) | 395772/598792 (66.1%) | 39 checked, 0 mismatched |
| the monthly funding report (`FundingScreen_Init`, `FundingScreen_OpenMonthly`, the month-0 upkeep trap, 23 funding globals named) | 56/56 (Release) | 11293 | 880/1103 (79.8%) | 397120/598792 (66.3%) | 39 checked, 0 mismatched |
| the region naming fix (`PufoRegionId`, `g_anRegionBuildCount @ 0x18655D` found, the false 15-byte-overflow bug row removed, the port's out-of-bounds counter index fixed) | 56/56 (Release) | 11297 | 880/1103 (79.8%) | 397120/598792 (66.3%) | 39 checked, 0 mismatched |
| the funding screen's own UI (`FundingScreen_Toggle`, `FundingScreen_Open`, `FundingScreen_Draw`, `FundingScreen_DrawExtended`, the TEXTS[1307..1333] commentary table, the slot order confirmed by the names panel) | 56/56 (Release) | 11400 | 884/1103 (80.1%) | 402388/598792 (67.2%) | 39 checked, 0 mismatched |
| the inventory close (`Inv_CloseAndCommit`, the 24-slot loadout snapshot, the clip merge) — **`tact-ui` complete at 52/52** | 56/56 (Release) | 11464 | 885/1103 (80.2%) | 403656/598792 (67.4%) | 39 checked, 0 mismatched |
| the four post-battle tallies (`Tact_TallyBattleStats`, `Tact_TallyUnitResults`, `Tact_TallyItemRecovery`, `Tact_TallyCraftRecovery`; the debrief label table, `Mc1Rec+0x1E` named, the alien type/rank slot table) | 56/56 (Release) | 11555 | 889/1103 (80.6%) | 408204/598792 (68.2%) | 39 checked, 0 mismatched |
| the victory-flag naming fix (`unk_128A8C` → `Map_bMissionVictory`, `Map_szMissionCaption`, `Map_szMissionScore`, `Map_nDebriefLine`, `Map_nMissionOutcome` named; the register-built-offset trap recorded) | 56/56 (Release) | 11555 | 889/1103 (80.6%) | 408204/598792 (68.2%) | 39 checked, 0 mismatched |
| the post-mission soldier cleanup (`Tact_ClearSoldierBattleFlags`, `Soldier_PromoteByTypeScores`; the five-rung rank ladder, `g_aClassStatGrowth` byte-checked, the auStatBase index bug found) | 56/56 (Release) | 11661 | 891/1103 (80.8%) | 411568/598792 (68.7%) | 40 checked, 0 mismatched |
| the battle-end and debrief emitter (`Tact_CheckBattleEnd`; the four exits, the 19 debrief lines and their three meta shapes, the loot commit into `aSmapSites`, the Mars restart tail, the signed `abFacilityGrid` byte trap found) | 56/56 (Release) | 11791 | 892/1103 (80.9%) | 417316/598792 (69.7%) | 40 checked, 0 mismatched |
| the fall processor (`Unit_ApplyFallDamage` + `Tact_ProcessFallAction`; the 0.4f soft-float damage formula, the `_rt_sdiv` operand order behind the psi gate, the LABEL_101/LABEL_111 split, the medikit-before-modes order, 7 original bugs found) | 56/56 (Release) | 11980 | 896/1103 (81.2%) | 426312/598792 (71.2%) | 40 checked, 0 mismatched |
| the fire-phase processor (`Tact_ProcessFireAction` + the aim's `dTravelSave`/sample-array writes; the muzzle-relative samples, the two disagreeing 1/12 forms, the arc-off-map flag whose register-built writes an xref search misses, 7 original bugs found) | 56/56 (Release) | 12130 | 898/1103 (81.4%) | 435608/598792 (72.7%) | 40 checked, 0 mismatched |
| the queue walker (`Tact_TickActiveUnit`; the `bFlag0` fire-or-move choice, the per-command camera rules and their adopt-before/after ordering, the 60-voxel recentre stride, the AI-action pair named, the kill-credit bound, 6 original bugs found; coverage unchanged — the walker was already credited by comment citations) | 56/56 (Release) | 12226 | 898/1103 (81.4%) | 435608/598792 (72.7%) | 40 checked, 0 mismatched |
| the low-morale AI (`EnemyAi_TryFireOrWander`; the large-unit early exit, the AI-action pair raised before the path choice, the same-column redraw Hex-Rays gets wrong, the two floor walks of different depth, 7 original bugs found) | 56/56 (Release) | 12305 | 899/1103 (81.5%) | 436816/598792 (72.9%) | 40 checked, 0 mismatched |
| the enemy AI driver (`Tact_EnemyAiTick`; the byte-not-pointer at `TactCtx272+0x100`, the skip-without-advancing tick gate, `g_aUnitTypeFlag3` found, the neighbour-melee hand-off, the `cost * 2/3 + 8.0` fire gate, the RMP walk that never uses its drawn index, 3 original bugs found) | 56/56 (Release) | 12404 | 900/1103 (81.6%) | 440256/598792 (73.5%) | 40 checked, 0 mismatched |
| the enemy-turn bracket (`BeforeEnemyStep`, `Tact_UpdateUnitControl`, `AfterEnemyStep`; six passes over four face counters, the `counter + step` term added twice, the burn run probing against its axis, the Win64 `abFaceDamage` offset fixed, 12 original findings recorded) | 56/56 (Release) | 12513 | 903/1103 (81.9%) | 445204/598792 (74.4%) | 40 checked, 0 mismatched |
| the tactical map click and the mode-6 confirm (`Tact_HandleMapClick`, `TactAction_ConfirmMeleeMode6`; the one-layer-down cursor cell, the four asymmetric latch arms, the two disagreeing melee gates, two wrong text-record indices in the existing port fixed, `audit_names` now at zero flags) | 56/56 (Release) | 12651 | 903/1103 (81.9%) | 445204/598792 (74.4%) | 40 checked, 0 mismatched |
| the path replay (`Unit_PathToCell`; the dropped `Map_ComputeReachable` flag that hid half its effect, the layer re-check on `bFlags29` bit 3 with its two disagreeing ceiling thresholds, the 27-byte facing table, the layer delta that lives in the `nFacing` slot, 6 original findings recorded) | 56/56 (Release) | 12701 | 904/1103 (82.0%) | 446256/598792 (74.5%) | 40 checked, 0 mismatched |
| the step probe (`Map_ProbeStepOccupants`; the two vertical re-anchors and their asymmetric layer bookkeeping, the large unit's second column, the up anchor that cannot fire for a large unit going east, three unguarded reads of layer-gated state, 7 original findings recorded) | 57/57 (Release) | 12739 | 905/1103 (82.0%) | 447324/598792 (74.7%) | 40 checked, 0 mismatched |
| the step legality (`Unit_CanStepTo`; the head and twenty direction arms, the seam carrying the settled cell instead of the candidate, the surcharge and `bFlags29` bit 3 corrected to the probe's re-anchor report, 4 original findings recorded) | 57/57 (Release) | 12787 | 906/1103 (82.1%) | 462040/598792 (77.2%) | 40 checked, 0 mismatched |
| the geoscape pointer dispatcher (`Geoscape_OnPointer`; the 16-bit cursor globals, the screen-0 menu band, the 19-arm screen switch, the drag gate and its tap cooldown, the `(v + 31) >> 5` tap division, 5 original findings and 3 deviations recorded) | 58/58 (Release) | 13022 | 907/1103 (82.2%) | 470616/598792 (78.6%) | 40 checked, 0 mismatched |
| the pointer release (`Geoscape_OnPointerUp`; the shortfall gate on 0x1870C8 read as a dword, the list clicks where the press only tracked rows, the panel tail shared by two arms, the press/release disagreements on screens 0, 7, 10 and 18, 3 original findings and 6 ledger rows, one wrong slot address in the port corrected) | 58/58 (Release) | 13267 | 908/1103 (82.3%) | 477608/598792 (79.8%) | 40 checked, 0 mismatched |
| the map select test (`Smap_HitTestStrategSelect`; the strict strip gate, the 16x16 cell window, the 1/1.333 and 1/1.694 item scalers that disagree with the scroll factors, the signed base-type byte, the two past-the-end search indexes, the flag-27 tail that writes the pending click, the colour-slot byte mask `Line_Write` was missing, 10 ledger rows) | 59/59 (Release) | 13405 | 909/1103 (82.4%) | 480696/598792 (80.3%) | 40 checked, 0 mismatched |
| the screen close dispatcher (`Geoscape_CloseUI`; the head, the speed chain against `g_aGeoscapeSpeedSec` and its 3600 double call, the 17-case switch, the per-base needed-flag rebuild, the mutual recursion with `Options_SaveAudioSettings`, the five-slot funding bar and the strict -3000000 limit, 11 ledger rows) | 60/60 (Release) | 13583 | 910/1103 (82.5%) | 482796/598792 (80.6%) | 40 checked, 0 mismatched |
| the audio-save callee (`Options_SaveAudioSettings`; all three arguments dead, the recursion's real two-pass shape via `OptionsScreen_Init` clearing the controls byte, the tab-4-only registry write, the pause-resume tail, 5 IDB renames with one corrected in the same unit) | 60/60 (Release) | 13632 | 911/1103 (82.6%) | 483132/598792 (80.7%) | 40 checked, 0 mismatched |
| the options overlay (`OptionsScreen_Open`; the toggle, the four-panel release, the signed base match against `BaseRec.bUnk4`, the four-column craft rows, the TEXTS 149–152 / 1165–1172 labels, the uninitialised region label found) | 60/60 (Release) | 13683 | 912/1103 (82.7%) | 486292/598792 (81.2%) | 40 checked, 0 mismatched |
| the overlay row confirm (`OptionsScreen_OnConfirm`; the shared aux index behind the readiness gate, the flag-27 travel-order line, the final-mission arm pinned to the Cydonia topic and the Avenger through the shipped data files, 1 latent out-of-bounds row) | 60/60 (Release) | 13712 | 913/1103 (82.8%) | 486552/598792 (81.3%) | 40 checked, 0 mismatched |
| the map-order answer (`Smap_ResolveInterceptUI`; the flag-13 path that skips the armed-byte clear, the three dismiss tests that each re-read a shifting line 0, the unconditional tail removal found, the `0x187068` naming follow-through) | 60/60 (Release) | 13750 | 914/1103 (82.9%) | 486892/598792 (81.3%) | 40 checked, 0 mismatched |
| the map recentre click (`Smap_OnPointerUp`; the half-extents — read as ceilings here and corrected in unit 82, the twice-read colour slot, the 1/1.333 and 1/1.694 unscale, the two `nScrollY` replace arms, the always-120 crosshair x, the single-wrap crosshair y found) | 60/60 (Release) | 13785 | 915/1103 (83.0%) | 487624/598792 (81.4%) | 40 checked, 0 mismatched |
| the geoscape event pass (`StrategList_ProcessEvents`, `Strateg_ConfirmInterceptTarget`; the `_rt_udiv` divisor-first convention, the exact 1/60 interpolation constant, the six re-reads behind the arrival dispatch, the wrapped-leg winner, the fractional diagonal, 1 original bug found, the 0x159E74 naming fix and the `Tact_Shutdown` wrong-index removal it exposed) | 60/60 (Release) | 13890 | 917/1103 (83.1%) | 492808/598792 (82.3%) | 40 checked, 0 mismatched |
| the strateg icons and the clock strip (`Smap_DrawStrategIcons`, `Geoscape_DrawClockPads`; the halving triple corrected — ADDMI is add-if-minus, so Hex-Rays' `/ 2` was faithful and the port's ceil was not, the palette-slot-32 forcing cross-confirmed by `mapbits.pal`, zoom scaling both axes, the `>` wrap that leaves the map-size coordinate alone, the low-halfword animation phase, the blank `mapbits +6` frame measured in main.dat, the day-tens blit gate, the divisor-first `_rt_udiv`, the `apLayerPad` → `apClockDigitPads` misnomer and the corrected 0x159608 clock base) | 60/60 (Release) | 13942 | 919/1103 (83.3%) | 496036/598792 (82.8%) | 40 checked, 0 mismatched |
| the whole geoscape frame (`Geoscape_DrawScreen`; the backdrop proven identical to the radar backdrop and factored into one helper, `gOptionsScratch[0]` decoded as the five-way options menu id from the shipped strings and `gOptionsScratch[4]` as its gate, the record * 205 copy offsets naming each label's record, the `nKey`/`nH` stack-slot mislabel corrected, the shared tail and case 0's double cancel blit, the slot rows proven to come from `MainMenu_pSlotCtx` rather than TEXTS, the uninitialised-`psz` default path recorded as a deviation) | 60/60 (Release) | 14019 | 920/1103 (83.4%) | 499512/598792 (83.4%) | 40 checked, 0 mismatched |
| the geoscape pulse tick and the map-target arm (`Geoscape_PulseUpdate`, `Smap_SelectMapTarget`; the LDRB `Map_nCraftCount` and the empty-list jump into the clock block, the phase gate that skips the switch when the phase is already above 1, the absolute 120/160-anchored direction scroll with its 25/5 pair and the screen-2 clamp skip, the lit-control-only forced click, the four saturating counters, the selection flag that unit 83's frame reads, the unconditional pause and the fourth line-colour read) | 60/60 (Release) | 14084 | 922/1103 (83.6%) | 501664/598792 (83.8%) | 40 checked, 0 mismatched |

Release is the documented configuration and it is green. In **Debug**, `test_titles` and `test_save`
fail and `test_template` is flaky; all three already fail at `04f1155` (verified by building that
commit in a clean worktree with the same ClangCL toolset), so none came from this step. See "Separate
issues".
