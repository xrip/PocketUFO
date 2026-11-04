# Current save-format audit

Date: 2026-10-26  
Status: bounded format review; original-save compatibility remains unproved.

This note records the client save chunks, their fixed or count-based sizes, the
version gates seen in ARM instructions, the current owners in the Win64 port,
and what the tests exercise. It does not mark save/load parity complete.

## Driver and format rules

The save driver is SaveGame at 0x44988; the load driver is LoadGame at 0x44BBC.
Their current port code is in
[pufo_save.c](../../src/pufo/game/pufo_save.c#L2718) and
[pufo_world.c](../../src/pufo/app/pufo_world.c#L3278).

Raw ARM confirms the common order. SaveGame writes TACTVER, GAMESTATE, BASES,
PRODUCT, RESEARCH, SHIPS, SMAP, STRATEG, TIME, and SOLDIERS. The call sequence
is 0x44A2C through 0x44A50. It then reads Map.bTactFlag: 0x44A58 is LDRB,
0x44A5C is ANDS, and 0x44A60 is BEQ. A clear flag skips the tactical payload.
The set branch writes MCNT, calls the tactical-data writer, writes OBJBEFORE,
then writes RMP inline. The 0x44A14 word is E3A03001 (MOV R3,#1); 0x44A1C is
E3A02002 (MOV R2,#2); and 0x44A28 calls DatFile_Write for the two-byte
TACTVER value.

LoadGame reads the common chunks in the same order at 0x44D84-0x44DA0. At
0x44DA4 it reads the loaded state byte, 0x44DA8 masks it, and 0x44DAC branches
to the tactical arm when nonzero. The zero arm calls Save_ReadSoldiers at
0x44DB4. The tactical arm reads MCNT and calls Tact_InitMission at 0x44E24.
That mission path reads the soldier records, tactical data, RMP, and OBJBEFORE
from the still-open save.

TACTVER is not the STRATEG format version. SaveGame writes its two-byte value 1.
LoadGame reads it into the slot context at `0x156938+0x118`; the allocator uses
that loaded value to choose its FLOBJS count width. The one-byte signed format
version is the first field of STRATEG at address 0x18655C: Save_ReadStrategVer
at 0x3F1C8 reads one byte before the body readers run.

The raw version branches are:

- PRODUCT: 0x40644 is E1D036D4 (LDRSB of the format byte), 0x40650 is
  E3530003 (CMP with 3), and 0x40654 is AA000014 (BGE). Versions below 3 use
  the shorter product layout.
- BASES: 0x403AC is E1D636D4, 0x403B0 is E3530003, and 0x403C0 is E3A02008.
  The reader consumes eight hangar bytes at version 3 or newer and four before
  version 3. A separate version branch skips one more header byte for old
  saves. Thus the old header is five bytes shorter.
- STRATEG: 0x4154C is E1D436D4, 0x41550 is E3530002, and 0x41554 is
  DA00000D (BLE). Versions 2 and below omit the two final four-byte target
  fields from each record.

The other variable extents also have instruction-level bounds:

- BASES writes 288 bytes at 0x3F210 and 1152 at 0x3F224. Its 17-byte flag
  loop starts with MOV R5,#0x11 at 0x3F2D0, advances the source by 0x1C at
  0x3F2E8, then loops at 0x3F2F0. The facility loop starts with MOV R5,#8
  at 0x3F30C.
- SHIPS writes its one-byte count with MOV R2,#1 at 0x3F6FC and the call at
  0x3F704. It reloads the live craft count at 0x3F710 before the craft loop.
  The per-craft field writes run from 0x3F740 through 0x3F9A8 and sum to
  61 bytes; the following object loop adds two bytes per object.
- SMAP sets R5 to 0xC8 (200 records) at 0x3FA90. It sets the in-memory
  record stride to 0x2E6 at 0x3FB4C and adds that stride at 0x3FB58. The
  serialized fields total 740 bytes; the remaining 2 bytes are not written.
- MAP loads 10 at 0x43940 and multiplies each saved dimension by 10 at
  0x4394C and 0x43960. The layer loop starts at zero and advances by 0x3C at
  0x43C60 until 0xF0 at 0x43C64: four layers. The per-cell writer from
  0x43990 through 0x43C1C emits 43 bytes. Thus the map size is
  2 + 4×43×(10X)×(10Y) = 2 + 17200XY.
- TEAMUNITS has a fixed 30-ID pass: 0x43D34 compares its slot index to
  0x1E after adding 4 to the ID pointer at 0x43D30. The writer uses fixed
  30- or 60-slot passes for the other three roster chunks too.
- The MAP cell-record calls run from 0x43990 through 0x43C1C. They emit
  four face bytes, five one-byte flags, two split flag bytes, one byte,
  two split flag bytes, one byte, two four-byte IDs, one byte, two flag
  bytes, one byte, and four four-byte sprite IDs: 43 bytes per cell. The
  layer and dimension bounds above are read from the raw loop, not inferred
  from a struct size.
- The RMP writer is inline in SaveGame. It writes a count dword, five 0xF0
  dword arrays, a 0x4B0 name block, and the final 0xF0 level array.

## Chunk matrix

Let B be the saved base count, O the object count, S the ship count, U the
soldier count, I the tactical-item count, X/Y the map dimensions in map
sections, and N the strategic-list count. Counts in formulas describe payload
bytes, not the 21-byte directory entry.

| Chunk | Original ARM writer / reader and payload | Current port owner and join | Tests and limit |
|---|---|---|---|
| TACTVER | SaveGame 0x44988 / LoadGame 0x44BBC. Fixed 2 bytes; writer stores 1. It controls FLOBJS allocation width, separately from STRATEG version. | LoadGame exports the loaded slot-context value to World; Tact_InitMission uses that value for allocation. | TestSaveGame checks 2 bytes. Debug `test_save_world` passes with loaded TACTVER 1 and 257 items. No original-save sample is available. |
| GAMESTATE | Save_WriteGameState 0x44928 / Save_ReadGameState 0x44958. Fixed 1 byte. The writer stores the tactical flag; the reader puts it in the separate load-state byte. | World_SaveSlot supplies bTactFlag; LoadGame uses the loaded byte to choose geoscape or tactical restore. | TestGameStateChunk checks the byte. TestSaveGame checks both branches. |
| BASES | Save_WriteBases 0x3F1FC / Save_ReadBases 0x402C0. Current header 1974 bytes: 288 + 1152 + 3 + 4 + 8 + 4 + 1 + 17 + 9 + 8×61. Then B×(16 + 2O). Before version 3, the reader consumes a 1969-byte header, then the same per-base form. | World_StageSaveRecords and World_WireBasesLive stage fields; Save_BasesLiveToRecord / Save_BasesRecordToLive bridge the native globals and fixed records; records rebuild pW->apBases on load. | TestBasesChunk covers versions 2 and 3. TestLegacyProductWorldLoad loads seven base records through World_LoadSlot. No original BASES chunk sample exists. |
| PRODUCT | Save_WriteProduct 0x3F4F4 writes 0x1900, 0x1900, and 0xC80 bytes: 16000 total. Save_ReadProduct 0x4062C reads the same for version 3+. For older versions it reads 0x1200 + 0x1200 + 0x900 + 0x480 = 12672; the last 0x480 bytes are the 144×8-byte topic-to-item map. | World_StageProductState stages the live A/B/C arrays before save or load. Load_HookJoinProductState publishes the bytes read. Old-version migration remaps A/B/C/D and publishes converted product arrays again before lab/workshop flags are derived. | TestProductChunk checks both sizes. TestLoadGameOldVersionMissionSeam checks conversion with a staged buffer. TestLegacyProductWorldLoad now exercises World_SaveSlot, a synthetic version-2 file, and World_LoadSlot; the current frame log passes eight remap checks and observes disk version 2 then in-memory version 3. This is not an original version-2 file. |
| RESEARCH | Save_WriteResearch 0x3F558 / Save_ReadResearch 0x406E8. Fixed 0x900 + 0x900 + 0x90 + 0x90 bytes, then 144 records × 34 bytes: 9792 total. No save-version branch. | World_StageResearchState / Load_HookJoinResearchState copy the four raw spans and map topic fields one by one. The final topic’s four range bytes use the separate live gap at 0x160228-0x16022B. | TestResearchChunk checks the codec. TestProgressionWorld checks an active project and rebuild behavior after a poisoned same-world load. It is not a cold-process ongoing-project check. |
| SHIPS | Save_WriteShips 0x3F6DC / Save_ReadShips 0x4086C. One-byte count, then S×(61 + 2O) bytes: 61 fixed bytes per craft and one u16 object quantity per object. No format-version branch. | World_SaveSlot passes the live craft array and object table. World_LoadSlot rebuilds live craft records. Save_ReadShips copies the just-read count into ShipCtorEnv before each constructor, so constructors do not clear the saved cargo column using a stale count. | TestShipsChunk covers multiple crafts and object columns. Fresh-process campaign checks loaded craft cargo; it proves that route, not every craft record. |
| SMAP | Save_WriteSmap 0x3FA28 / Save_ReadSmap 0x40C4C. Five dwords (20), 200 site records × 740, then 5 + 8 + 8 bytes: 148041 total. The raw site loop runs 200 entries and advances 0x2E6 bytes in memory; 2 gap bytes per site are not written. | World_StageSaveRecords stages view and sites. Load_HookJoinSmapState publishes the seed, site count, and serialized site fields to the live world. | TestSmapChunk checks the codec; TestProgressionWorld poisons and restores sites and camera state in one world. |
| STRATEG | Save_WriteStrateg 0x3FBB4 / Save_ReadStrateg 0x40F4C. Header 134 bytes plus N×113 bytes for current writes. A version 2-or-lower reader consumes 105 bytes per record. The first header byte is the signed save-format version. | World_StageSaveRecords stages IDs, counters, selections, economy, and list count. Load_HookJoinStrategState restores those live fields after the reader. | TestStrategChunk covers records and the reader gate. TestProgressionWorld checks selected IDs, counters, economy, and append behavior after load. |
| TIME | Save_WriteTime 0x40204 / Save_ReadTime 0x41600. Fixed 27 bytes: 4 + 1 + five dwords + 2. No format branch. | World_SaveSlot stages the eight source cells into SaveTimeRecord; LoadGame restores time fields and calls GameTime_Decode. | TestTimeChunk checks all 27 bytes and their order. The header comment in pufo_save.h says 32 bytes; the explicit write widths and this test establish 27. |
| SOLDIERS | Save_WriteSoldiers 0x42350 / Save_ReadSoldiers 0x416BC. Two-byte count plus U×740 bytes. Each record is fixed size: six flag bytes; three fixed 120/150/244-byte arms; and the scalar/name/stat fields between them. | The world owns apSoldiers and nSoldierCount. Tactical Tact_InitMission reloads the roster and resolves cells/items against the world map and pools. Geoscape loads use the same record format without tactical cell resolution. | TestSoldierRoundTrip, TestSoldiersReload, TestSoldierTacticalArms, and TestSoldiersCellLink cover codec rules. Tactical World_LoadSlot is exercised in the frame test in the same process. |
| STACTFLG | Save_WriteTrailingChunks / Save_ReadInTactFlags, called from the soldier writer/reader. U bytes, one per soldier. | Same live soldier roster as SOLDIERS; read before the record bodies so the body reader can choose its fixed arms. | Soldier tests cover flags and the missing-chunk fallback. TestSaveGame checks its position after SOLDIERS. |
| 1_24SCB | Save_WriteTrailingChunks / Save_ReadLinkedSoldiers. U×4 bytes, one linked-soldier save ID per roster entry. | Same live soldier roster; links resolve after all soldier records are present. | Soldier link tests cover the sentinel and roster resolution. TestSaveGame checks the four-byte one-soldier case. |
| MCNT | SaveGame inline at 0x44A88-0x44A98 / LoadGame tactical branch at 0x44DD0 onward. Fixed 1 byte. The writer increments the counter before writing. | World_SaveSlot stages the mission byte through a local mirror. World_LoadSlot restores it before Tact_InitMission. | TestSaveGame checks increment and one-byte size. Geoscape saves omit the chunk. |
| TGAMEDATA | Save_WriteTGameData 0x446A8 / Save_ReadTGameData 0x44248. Fixed 43 bytes in 22 write calls; some calls write arrays. | World_SaveSlot builds the record from current tactical input, map, cursor, focus/camera, mission, and craft fields. World_InitTactical reads it from the open slot file. | TestTGameDataWrite and TestReadTGameData cover the codec and derived calls. TestSaveGame checks it is tactical-only. |
| TEAMUNITS | Save_WriteTeamUnits 0x43C84 / Save_ReadTeamUnits 0x44020. Fixed 125 bytes: one count byte, one selected-unit dword, and 30 dword slots. | World builds the record from live XCOM roster, selected unit, and soldier IDs; the tactical reader resolves IDs to the reloaded soldier roster. | TestTeamUnitsChunk checks selected and roster links. TestSaveGame checks its branch and order. |
| ALIENS | Save_WriteTeamUnits / Save_ReadTeamUnits. Fixed 121 bytes: one count byte and 30 dword slots. | Live enemy roster and the same soldier-ID table as TEAMUNITS. | TestTeamUnitsChunk checks the shared roster codec; not a full cold-load mission. |
| DIEDSOLDS | Save_WriteTeamUnits / Save_ReadTeamUnits. Fixed 244 bytes: one count dword and 60 dword slots. | Live civilian roster and the same soldier-ID table. | TestTeamUnitsChunk checks fixed padding and links. |
| HIDDENSOLD | Save_WriteTeamUnits / Save_ReadTeamUnits. Fixed 124 bytes: one count dword and 30 dword slots. | World-owned hidden tactical roster; resolved against the reloaded soldier table. | TestTeamUnitsChunk checks fixed padding and links. |
| FLOBJS | Writer 0x42F48 emits a 4-byte count then I×17 bytes: object dword, item-index word, three flag/ammo bytes, next-item dword, linked-soldier dword. Allocator 0x431AC gates 1/4-byte count from slot-context TACTVER at +0x118. Body reader 0x43258 receives original pointer 0xAAF70, whose +0x118 is nonzero, selecting 4 bytes. | World tactical pool, objects and soldiers. Allocation uses loaded TACTVER; body remains wide, matching its separate raw caller. | TestFlobjsChunk checks writer, explicit narrow/wide reader arms and links; these do not prove a particular old save version. Debug `test_save_world` passes count 257, exact tail fields and link 255→256 through actual World_LoadSlot: [regression evidence](save-flobjs-count-2026-10-27.md). |
| MAP | Save_WriteMap 0x438E0 / Save_ReadMap 0x433D8. Two dimension bytes, then 4 layers × (10X) × (10Y) cells × 43 bytes per cell: 2 + 17200XY. | World tactical grid and cell lookup. Tact_InitMission restores cell state, occupants, items, and sprites into its live map. | TestMapChunk checks cell fields, dimensions, IDs, and bounds. The full frame test saves and reloads a tactical map in the same process. |
| V124.DAT | Save_WriteTGameData writes it at 0x448B8 / Save_ReadTGameData reads it. Fixed 8 bytes: two dwords. | World tactical state owns both dwords; missing-chunk fallback remains in the reader. | TestTGameDataWrite / TestReadTGameData cover the state. TestSaveGame checks its tactical branch. |
| OBJBEFORE | Save_WriteObjBefore 0x3F158 / Save_ReadObjBefore 0x3F0E8. 2O bytes, one u16 recovery score per object. | Reads and writes ObjectRecFull.uRecovScore; Tact_InitMission reads it for a tactical resume. | TestObjBeforeChunk checks the object-count loop. TestSaveGame checks tactical ordering. |
| RMP | SaveGame writes inline at 0x44AB0-0x44B78; Save_ReadRmp is 0x43EE0. Fixed 2404 bytes: count dword, five 60-entry dword arrays, and 60 names of 20 bytes. The count controls module loads, not the fixed array reads. | World_InitTactical reads the fields into the RMP context, then World_TactLoadRmpThunk loads the counted modules. | TestReadRmp checks order, count, and bounds. TestSaveGame checks the 2404-byte writer result and level-after-names order. |

## Owners, helper behavior, and clock

The port does not treat the record structs as the original global memory. The
world stages and joins the split owners:

- BASES, SMAP, and STRATEG use World_StageSaveRecords and their record adapters.
- PRODUCT uses World_StageProductState and Load_HookJoinProductState. The v2
  conversion reads its 144×8-byte map at product-block offset 16000. The file
  offset of that map is 0x2D00. Raw migration uses 0x90 map bytes per base and
  0xC8 product entries per base; after conversion, the second product join
  publishes all converted columns before derived staffing flags are computed.
- RESEARCH uses World_StageResearchState and Load_HookJoinResearchState. Its
  0x900-byte D header ends at 0x160C4B. The research count byte at 0x160C4C
  and the UI dword at 0x160C60 are separate owners. Raw immediate construction
  gives 0x1D44 for the first and 0x1D58 for the second. They are not aliases.
  The last topic's four range bytes at 0x160228-0x16022B are a different gap
  before the research columns.
- SHIPS reads every object cargo column. Save_ReadShips first stores the
  one-byte craft count at 0x408E0, then the original Ship_ctor reads the live
  count at 0x45C94/0x45C98 before clearing its column. The native reader now
  updates ShipCtorEnv from the count before constructors run. This is the
  concrete fix for early craft cargo erased by later constructors.
- TIME stores game time, pause state, tick marks, and one extra word. It does
  not store GameClock.nSpeed or the UI clock-step/timer periods. World_LoadSlot
  sets game speed from the current options; Timer_StartBySpeed chooses the
  C350/C351 periods, while UI pulse and animation timers have their own
  periods. SaveTime's eight explicit writes total 27 bytes, not the 32 claimed
  by a stale header comment.

The DatFile directory is also part of the format. SaveGame passes 100 to
DatFile_Open for create mode (raw 0x449F8 is E3A03064). Each entry is 21 bytes,
after a four-byte count. The geoscape branch writes 12 entries, so 88 are
unused. The tactical branch writes 23, so 77 are unused. The current reader
sets its active count to the first empty name; the world v2 fixture checks that
the current writer leaves that empty row after its used entries. This helper
behavior has only been tested on port-written directories.

## What the checks prove

- TestSaveGame writes both branches. It checks 23 chunks in tactical order,
  then 12 common chunks for a geoscape save; it checks selected payload sizes
  and verifies that MCNT, TGAMEDATA, and RMP are absent from the geoscape file.
- The per-chunk tests in tests/test_save.c check field order, fixed widths,
  padding, bounds, and both old/current reader branches. They use port-created
  files and synthetic records.
- TestLegacyProductWorldLoad uses the real World_NewGame, World_SaveSlot, and
  World_LoadSlot path. It modifies the port-created slot into a synthetic v2
  layout. The earlier full-frame log records disk version 2, a 12672-byte
  PRODUCT payload, eight remaps, seven restored bases, and in-memory version
  3. See
  [the earlier full-frame run](../../.workers/luna-verifier/phase3-postmanufacture-full-frame.log#L3810).
  This proves the tested v2 port path, not byte compatibility with a file made
  by the original game.
- The tactical save/load case in tests/test_geoscapeframe.c writes a live
  mission and calls World_LoadSlot on the same process/world. It does not prove
  a cold-process tactical resume.
- The current Release tactical-resume E2E stages a declared contact and loadout,
  launches, arrives, deploys eight XCOM units, moves, fires, and saves an idle
  live tactical mission. A second process loads the 812106-byte port-produced
  slot through the real menu. The loaded mission matches the tested unit,
  rosters, hand/items, dimensions, cursor, and mission setup; it then accepts a
  new move, Rifle action, and End Turn, returns to idle input, and aborts through
  the debrief to the geoscape. The selected-unit focus after `Tact_InitMission`
  still recalculates camera scroll and sets the layer to selected-cell Z+1.
  The all-cell terrain check reports the same 32-bit digest `EF62466D` for the
  6x6 map before save and immediately after load. It covers all saved terrain
  IDs, but a matching 32-bit signature is not byte-by-byte proof of every cell
  and is not an original WinCE save comparison. See the [outer checker log](../../.workers/luna-runtime-gate-v2/20261026T122355812Z-e4f892a3/outer-20261026T122355812Z-e4f892a3.log),
  [save process log](../../.workers/luna-runtime-gate-v2/20261026T122355812Z-e4f892a3/output/tactical_resume_save-20261026T122355812Z-e4f892a3.log),
  [load process log](../../.workers/luna-runtime-gate-v2/20261026T122355812Z-e4f892a3/output/tactical_resume_load-20261026T122355812Z-e4f892a3.log),
  and the [view proof](../../.workers/luna-tactical/tactical-resume-view-proof.md).
- The campaign E2E saves after returning to the geoscape, starts a fresh
  process, loads that geoscape slot, and reaches a later contact and deployment.
  Its load does not include the tactical-only chunks.
- TestProgressionWorld poisons live product/research owners before a
  same-world load and checks ongoing work. It is not a cold-process
  ongoing-project test.

## Remaining compatibility evidence

No original-game .sav sample is present in the workspace, and the distribution
CAB carries no game save. Current fixtures use saves written by the port. The
available world v2 fixture begins with a slot written by this
port, then edits that file into the older layout. That fixture can catch errors
in the tested version gate, suffix staging, remap, and join; it cannot catch an
encoder and decoder sharing the same wrong field mapping.

The next format check needs at least original-game saves that cover each
format branch: a geoscape save with version 2, a geoscape save with version 3
or later, and a tactical save with the mission payload. Compare each directory
entry and payload against the raw reader/writer widths above, then load them in
the port. Cold-process tactical resume is proved only for a port-produced save;
original .sav compatibility remains unproved because no original save sample is
available.

The direct raw-byte and mapping notes used for the owner rows are
[the product/research/migration record](../../.workers/luna-gear/phase3-idb-evidence.md)
and [the Smap/Strateg record](../../.workers/luna-gear/save-join-review.md).
Those notes provide the raw word addresses; this audit states the bounded
format and test claims supported by them and the recorded test logs.
