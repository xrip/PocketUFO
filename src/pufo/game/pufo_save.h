/* pufo_save.h - save/load chunk serialisation.
 *
 * Evidence: Save_WriteSoldiers @ 0x42350, Save_ReadSoldiers @ 0x416bc,
 *           Save_ReadStrategVer @ 0x3f1c8, Save_ReadGameState @ 0x44958.
 *
 * Both drivers are ported: the save driver SaveGame @ 0x44988 and the load driver
 * LoadGame @ 0x44bbc. This module recreates the chunk writers and readers they call, and
 * the drivers themselves. The whole file is described below because the format is needed
 * to read a save.
 *
 * ## Container
 *
 * A save file IS a `DatFile` (see pufo_dat.h). `SaveGame` opens the slot with
 * `DatFile_Open(mode 2, 100)` and writes chunks in this order:
 *
 *     tactver, gamestate, bases, product, research, ships, smap, strateg, time,
 *     soldiers
 *     then, only when pCtxBase.bTactFlag != 0:
 *     mcnt, tgamedata, objbefore, rmp
 *
 * The slot path is `szAppDir + "sav" + slot + ".sav"`.
 *
 * ## Soldier records
 *
 * `Save_WriteSoldiers` does NOT dump the struct: it writes every field
 * individually, with TWO different layouts depending on `bTactFlag`:
 *
 *   - tactical  (bTactFlag != 0): the multi-target list is 30 x u32 of save ids
 *     and the spotted list is 60 x u32, and a large block of inventory reference
 *     ids is written
 *   - non-tactical: the multi-target list is 120 raw bytes and the spotted list
 *     is 244 raw bytes, and the inventory block is a 150-byte zero fill
 *
 * The non-tactical record layout was derived field by field from the write order
 * and closes to exactly **740 bytes**, which is a useful check:
 *
 *   6 flags + 1 facing + 2 uTu + 2 stun + 2 hp + 1 morale + 3 cell + 1 pose
 *   + 1 pad + 1 offX + 1 offY + 1 status + 2 tuSpent + 1 multiTarget
 *   + 120 multi + 1 + 1 + 4 look + 4 female + 1 type + 20 stat
 *   + 1 kind + 1 unk183 + 2 missions + 40 name + 1 rank + 18 statBase
 *   + 1 kneel + 1 + 1 + 2 kills + 5 ready + 1 unk961 + 50 unk962 + 25 unk1012
 *   + 150 inventory + 1 humanoid + 2 burden + 1 unk422 + 4 armor + 4 class
 *   + 4 unk436 + 4 unk440 + 1 unk423 + 244 spotted  =  740
 *
 * Two chunks follow the whole array, each with one value per soldier:
 * `stactflg` (1 byte each) and `1_24scb` (4 bytes each).
 */
#ifndef PUFO_SAVE_H
#define PUFO_SAVE_H

#include "dat/pufo_dat.h"
#include "game/pufo_boot.h"
#include "game/pufo_clock.h"
#include "game/pufo_font.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_ship.h"
#include "game/pufo_strateg.h"
#include "game/pufo_rmp.h"
#include "game/pufo_unit.h"
#include "util/pufo_ptrarray.h"
#include "pufo_common.h"

/* The non-tactical soldier prefix size derived above. */
#define PUFO_SAVE_SOLDIER_PREFIX 740

/* Chunk tags, in SaveGame's write order. */
#define PUFO_SAVE_TAG_TACTVER   "tactver"
#define PUFO_SAVE_TAG_GAMESTATE "gamestate"
#define PUFO_SAVE_TAG_BASES     "bases"
#define PUFO_SAVE_TAG_PRODUCT   "product"
#define PUFO_SAVE_TAG_RESEARCH  "research"
#define PUFO_SAVE_TAG_SHIPS     "ships"
#define PUFO_SAVE_TAG_SMAP      "smap"
#define PUFO_SAVE_TAG_STRATEG   "strateg"
#define PUFO_SAVE_TAG_TIME      "time"
#define PUFO_SAVE_TAG_SOLDIERS  "soldiers"
#define PUFO_SAVE_TAG_MCNT      "mcnt"
#define PUFO_SAVE_TAG_MAP       "map"
#define PUFO_SAVE_TAG_TEAMUNITS "teamunits"
#define PUFO_SAVE_TAG_ALIENS    "aliens"
#define PUFO_SAVE_TAG_DIEDSOLDS "diedsolds"
#define PUFO_SAVE_TAG_HIDDENSOL "hiddensold"
#define PUFO_SAVE_TAG_V124      "v124.dat"
#define PUFO_SAVE_TAG_FLOBJS    "flobjs"
#define PUFO_SAVE_TAG_TGAMEDATA "tgamedata"
#define PUFO_SAVE_TAG_OBJBEFORE "objbefore"
#define PUFO_SAVE_TAG_RMP       "rmp"
/* Written after the soldier array. */
#define PUFO_SAVE_TAG_STACTFLG  "stactflg"
#define PUFO_SAVE_TAG_124SCB    "1_24scb"

/* `tactver` is written as a u16 whose value is 1, and `gamestate` as one byte.
 * Note Save_ReadGameState @ 0x44958 reads that byte into a DIFFERENT cell than
 * Save_WriteGameState writes (a save/load asymmetry in the original); the port
 * uses one field for both and records the discrepancy. */
#define PUFO_SAVE_TACTVER_VALUE 1u

/* Builds the slot path `szAppDir + "sav" + nSlot + ".sav"`. */
bool Save_BuildSlotPath(char *pszOut, size_t cchOut, const char *pszAppDir, i32 nSlot);

/* The two id values the soldier record's tactical arms use for "no item / no soldier here".
 * PUFO_SAVE_EMPTY_ITEM_ID is the record of the shared empty item itself: `unk_138EA0 @ Map+0x254E8`,
 * whose TactItem.nRefId the mission bring-up sets to 32000, so a slot pointing at it round-trips
 * (writer: the `ADD R1, R0, #0x14` id reads; reader: the `v38 == 32000` arms at 0x41E04 and 0x41EE4). */
#define PUFO_SAVE_ID_NONE 0x7FFFu
#define PUFO_SAVE_EMPTY_ITEM_ID 32000u

/* What the three bInTact arms of a soldier record resolve their ids against. Every field may be NULL:
 * a record whose soldier is not flagged into the mission never reads or writes them, and a geoscape
 * save flags none. apSoldiers / nSoldierCount are filled by Save_ReadSoldiers itself (the bound is the
 * count re-read at 0x417D4), so a caller only supplies the tables it has. */
typedef struct SoldierRecordEnv {
    MapCellLookup pfnCell;        /* the cell arm, 0x419DC */
    void         *pCellUser;
    Soldier     **apSoldiers;     /* Map.apSoldiers @ Map+4504: the multi-target and spotted ids */
    i32           nSoldierCount;
    TactItem    **apTactItems;    /* Map.apTactItems @ Map+0x23438, indexed by TactItem.nRefId */
    i32           nTactItemCount;
    TactItem     *pEmptyItem;     /* unk_138EA0, the item the 32000 id names */
} SoldierRecordEnv;

/* Save_WriteSoldiers @ 0x42350.
 *
 * Begins the `soldiers` chunk, writes the u16 count, then every soldier's record,
 * then the two trailing chunks. `DatFile_Write` requires an open chunk, so this is
 * the only correct entry point: calling the per-soldier writer directly without an
 * open chunk fails, exactly as the original would. */
struct TeamUnitsState;   /* the record block below defines it; this declaration only needs a pointer */

/* bTactFlag (Map+0x00) and the four rosters are not decoration: the writer computes each soldier's
 * bInTact from them (0x423A0..0x424C8) -- 0 with no tact map, else 1 unless the soldier is in none of
 * apXcomUnits/apEnemyUnits/apCivilians/apHiddenUnits -- and stores it into the soldier before writing
 * its record, because the record's cell arm reads it. NULL pRosters means "no scans", i.e. 0. */
bool Save_WriteSoldiers(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount,
                        u8 bTactFlag, const struct TeamUnitsState *pRosters);

/* Save_ReadSoldiers @ 0x416bc: the matching read side. Three things the original does in this order:
 * `stactflg` first (0x416D0, one byte per soldier into bInTact, because the body arms and the cell
 * resolve read that flag), then the bodies (0x417BC re-finds the `soldiers` chunk), then `1_24scb`
 * (0x422C4). `pEnv` is what the bInTact arms resolve against -- the cell lookup (0x419DC), the roster,
 * the tactical item pool and the empty item -- and NULL means "no arms resolve anything", which a
 * geoscape-state load never needs. `bInTactFallback` is the value the original takes from Map+0x00 when
 * the stactflg chunk is absent. */
bool Save_ReadSoldiers(DatFile *pDat, Soldier **apapSoldiers, i32 *pnCount,
                       i32 nCapacity, const SoldierRecordEnv *pEnv, u8 bInTactFallback);

/* Soldiers_Reload @ 0x430cc. Free the whole roster, find the "soldiers" chunk in an open save, read the
 * count out of it, and allocate that many empty soldiers.
 *
 * It reads only the 2-byte count. The soldier bodies are read by Save_ReadSoldiers, which runs after this.
 * So this is the allocation step of a load, not a load by itself: after it the roster is the right size and
 * every entry is a freshly constructed empty soldier.
 *
 * The original frees with operator delete and does not null the slots it freed, and it does not check that
 * the count it just read fits the roster array. The port bounds the allocation by nCapacity. */
bool Soldiers_Reload(DatFile *pDat, Soldier **apSoldiers, i32 *pnCount, i32 nCapacity);

/* Writes one soldier's record through pDat, choosing its three arms from `pUnit->bInTact` (the writer
 * computed that flag before calling, and passes bTactFlag = Map+0x00 because the multi-target arm also
 * tests it). The `soldiers` chunk must already be open. Each arm pair is the same size: the 30-id
 * multi-target block against 120 filler bytes, 27 item ids + 42 meta bytes against 150 filler, and
 * count + 60 ids against 244 filler -- so a record's size never depends on the flag, and the two
 * trailing per-soldier chunks stay aligned. */
bool Save_WriteSoldier(DatFile *pDat, const Soldier *pUnit, u8 bTactFlag);

/* Reads one soldier's record back. bInTact is READ (the cell arm at 0x41968 and the three id arms test
 * it) but never written here -- it comes from Save_ReadInTactFlags, which runs first. The three arms
 * are modelled: multi-target at 0x41A70, inventory at 0x41D60, spotted at 0x42214, each taking the same
 * byte count as the filler arm it stands against (120, 150, 244). Deviations, all in the id tables:
 * the original indexes Map.apSoldiers and Map.apTactItems with no bound in the inventory and spotted
 * arms, the port bounds both, and the port refuses to dereference a NULL multi-target entry the writer
 * would have dereferenced. auStat[8] gets the original's 0 -> 10 default (0x4229C). */
bool Save_ReadSoldier(DatFile *pDat, Soldier *pUnit, const SoldierRecordEnv *pEnv);

/* The two per-soldier chunks. `stactflg` runs BEFORE the bodies in the original (0x416D0) and
 * `1_24scb` after them (0x422C4): the first fills bInTact, the second fills pLinked1040 from an
 * index into apSoldiers (0x7FFF = none). The original indexes without a bound; the port bounds it.
 * The writer stores the linked soldier's nSaveId (0x42E64), and every soldier's nSaveId IS its
 * roster index (Save_AllocSoldiers, pufo_save.c:652), which is why the index read round-trips. */
bool Save_WriteTrailingChunks(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount);
bool Save_ReadInTactFlags(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount, u8 bFallback);
bool Save_ReadLinkedSoldiers(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount);

/* The "tactver" chunk: a u16 written by the save driver at 0x44A10..0x44A28 and read back.
 * This is NOT the strateg version byte - see Save_ReadStrategVersion below. */
bool Save_WriteVersion(DatFile *pDat);
bool Save_ReadVersion(DatFile *pDat, u8 *pnOutVer);

/* Save_ReadStrategVer @ 0x3f1c8. FindChunk("strateg") then read ONE byte into the save format
 * version (0x18655C). LoadGame @ 0x44d84 calls this before any chunk body, so the version that
 * sizes the other chunks comes from the strateg chunk. The original checks neither result: a
 * missing strateg chunk leaves the previous version byte in place. */
bool Save_ReadStrategVersion(DatFile *pDat, i8 *pbVersion);

/* --- the small chunks -------------------------------------------------------
 *
 * These are the chunks whose bodies are a handful of field writes. Each is fully
 * transcribed; the address of its original is in the comment.
 */

/* Save_WriteGameState @ 0x44928 writes ONE byte from pCtx[0] (bTactFlag).
 *
 * Save_ReadGameState @ 0x44958 reads that byte into a DIFFERENT global
 * (`bGameState` @ 0x15821C), not back into pCtx[0]. So the save/load round trip is
 * asymmetric in the original: what is saved is the tactical flag, what is restored
 * is a slot-state byte. The port keeps both cells explicit rather than silently
 * unifying them; `Save_WriteGameState` writes the flag and `Save_ReadGameState`
 * stores into the caller's slot byte. */
bool Save_WriteGameState(DatFile *pDat, u8 bTactFlag);
bool Save_ReadGameState(DatFile *pDat, u8 *pbOutGameState);

/* The `objbefore` chunk: one u16 per object, read from `pObj->uRecovScore` @ 0xCA of
 * the 208-byte object record. Save_WriteObjBefore @ 0x3f158 / Read @ 0x3f0e8.
 *
 * `apObjects` is the parallel pointer array; only the u16 at 0xCA is serialised,
 * not the whole record. */
bool Save_WriteObjBefore(DatFile *pDat, ObjectRecFull *const *apObjects, i32 nObjectCount);
bool Save_ReadObjBefore(DatFile *pDat, ObjectRecFull *const *apObjects, i32 nObjectCount);

/* The `time` chunk. Save_WriteTime @ 0x40204 / Save_ReadTime @ 0x41600.
 *
 * The original reaches these through `gUiRootPanel.apLines[101..115]`, i.e. the
 * panel's line-pointer array is reused as scratch storage. The slots and their
 * widths, in the order written:
 *
 *     apLines[105] u32   apLines[106] u8    apLines[103] u32
 *     apLines[102] u32   apLines[101] u32   apLines[114] u32
 *     apLines[115] u32   word_1472C8 u16
 *
 * `apLines[105]` is the address the port already documents as `nGameTimeSec` and
 * `apLines[106]` as the geoscape flag, which is why the port keeps them as named
 * fields instead of through the panel. The remaining five slots have no recovered
 * name, so they are carried as named-but-opaque fields. Total 32 bytes. */
typedef struct SaveTimeRecord {
    u32 nSlot105;   /* the game clock */
    u8  nSlot106;   /* the geoscape flag */
    u32 nSlot103;
    u32 nSlot102;
    u32 nSlot101;
    u32 nSlot114;
    u32 nSlot115;
    u16 nWord1472C8;
} SaveTimeRecord;

bool Save_WriteTime(DatFile *pDat, const SaveTimeRecord *pTime);
bool Save_ReadTime(DatFile *pDat, SaveTimeRecord *pTime);

/* Save_AssignRosterIndices @ 0x42ea0.
 *
 * Only runs when `bTactFlag` is set. Assigns `nSaveId = 0, 1, 2, ...` to every
 * soldier in `apSoldiers`, then `nRefId = 0, 1, 2, ...` (the dword at +20) to every
 * entry in the tactical item list. The item counter RESTARTS at 0 rather than
 * continuing from the soldier count, so the two id spaces overlap -- which is fine
 * because they identify different record kinds.
 *
 * The soldier count is a u16 (the original reads it with LOWORD). */
bool Save_AssignRosterIndices(bool bTactFlag, Soldier **apSoldiers, i32 nSoldierCount,
                              TactItem **apTactItems, i32 nTactItemCount);

/* The `research` chunk. Save_WriteResearch @ 0x3f558 / Save_ReadResearch @ 0x406e8.
 *
 * Four opaque header blobs (0x900, 0x900, 0x90, 0x90 bytes) followed by 144
 * ResearchTopic records. Each record is written FIELD BY FIELD in the original's
 * order, which is NOT the in-memory field order, so the on-disk size is 34 bytes
 * while the struct is also 34 bytes: bComplete, bUnk31, wNameId, abUnk20, wUnk0,
 * abUnk10, bAvailable, bUnk30, bUnk33, abUnk25, abUnlock, wProgress, bUnk32,
 * wUnk2. */
#define PUFO_RESEARCH_TOPIC_COUNT 144
#define PUFO_RESEARCH_HEAD_A_BYTES 0x900
#define PUFO_RESEARCH_HEAD_B_BYTES 0x900
#define PUFO_RESEARCH_HEAD_C_BYTES 0x90
#define PUFO_RESEARCH_HEAD_D_BYTES 0x90

typedef struct ResearchTopic {
    u16 wUnk0;
    u16 wUnk2;
    u16 wNameId;
    u16 wProgress;
    u8  bAvailable;
    u8  bComplete;
    u8  abUnk10[5];
    u8  abUnlock[5];
    u8  abUnk20[5];
    u8  abUnk25[5];
    u8  bUnk30;
    u8  bUnk31;
    u8  bUnk32;
    u8  bUnk33;
} ResearchTopic;
_Static_assert(sizeof(ResearchTopic) == 34, "ResearchTopic is 34 bytes");

typedef struct SaveResearchState {
    u8 abHeadA[PUFO_RESEARCH_HEAD_A_BYTES];
    u8 abHeadB[PUFO_RESEARCH_HEAD_B_BYTES];
    u8 abHeadC[PUFO_RESEARCH_HEAD_C_BYTES];
    u8 abHeadD[PUFO_RESEARCH_HEAD_D_BYTES];
    ResearchTopic aTopics[PUFO_RESEARCH_TOPIC_COUNT];
} SaveResearchState;

bool Save_WriteResearch(DatFile *pDat, const SaveResearchState *pState);
bool Save_ReadResearch(DatFile *pDat, SaveResearchState *pState);

/* The product columns the LoadGame type-byte computation reads: 119..121 of the base's
 * 200-entry columns, proven from the ARM displacements 0x32F2/0x32F4/0x32F6 (C, u16,
 * +238/240/242) and 0x1AE0/0x1AE4/0x1AE8 (B, u32, +476/480/484). Game_StartNew @ 0x94554
 * clears the same 200-entry columns for all eight bases. */
#define PUFO_PRODUCT_TYPE_COL_A 119
#define PUFO_PRODUCT_TYPE_COL_B 120
#define PUFO_PRODUCT_TYPE_COL_C 121
#define PUFO_PRODUCT_ENTRIES_PER_BASE 200

/* The `smap` chunk. Save_WriteSmap @ 0x3fa28.
 *
 * Five header dwords, then 200 SmapSite records, then 5 + 8 + 8 trailing bytes.
 * Each site is written field by field in the original's order, giving 740 on-disk
 * bytes per site even though SmapSite is 742 bytes in memory (its `gap_519` and
 * `gap_741` bytes are not serialised). */
#define PUFO_SMAP_SITE_COUNT 200

typedef struct SmapSite {
    u8  bFlag0;                  /* 0x000 */
    u8  bFlag1;                  /* 0x001 */
    u16 auItemQty[256];          /* 0x002, 512 bytes */
    u16 uRecoveredSoldiers;      /* 0x202 */
    u8  bRecoveredSoldierAssign; /* 0x204 */
    u8  bRecoveredCraftCount;    /* 0x205 */
    u8  bUnk518;                 /* 0x206 */
    u8  gap_519;                 /* 0x207, not serialised */
    u16 anSoldierIds[100];       /* 0x208, 200 bytes */
    u16 anCraftIds[10];          /* 0x2d0, 20 bytes */
    u8  bHasLoot;                /* 0x2e4 */
    u8  gap_741;                 /* 0x2e5, not serialised */
} SmapSite;
_Static_assert(sizeof(SmapSite) == 742, "SmapSite is 742 bytes");

typedef struct SaveSmapState {
    u32 nCamParam0;
    u32 nCamScrollX;
    u32 nCamScrollY;
    u32 nParam3;
    u32 nParam4;
    SmapSite aSites[PUFO_SMAP_SITE_COUNT];
    u8  abTail5[5];
    u8  abTail8a[8];
    u8  abTail8b[8];
} SaveSmapState;

bool Save_WriteSmap(DatFile *pDat, const SaveSmapState *pState);

/* Save_ReadSmap @ 0x40c4c. It is NOT a plain mirror of the writer.
 *
 * After reading nMode, nScrollX and nScrollY into the viewport at 0x1863E8, and
 * only when unk_15821C is zero, it switches on the mode it just read and restores
 * the whole zoom level: the tile grid and layer globals are swapped to that
 * level's set, the world pair becomes 800x400, 600x300 or 472x236, and nMapW/nMapH
 * are taken from the level's grid PadImage. A mode outside 0..2 restores nothing
 * (the switch has a `default: break`). No scroll rescale and no vertical clamp run
 * here, unlike Smap_ZoomIn.
 *
 * The tail order is also not ascending: the 5 bytes go to unk_161F40, the next 8 to
 * unk_162000, and the last 8 to unk_161FF0, which sits BELOW unk_162000. */
bool Save_ReadSmap(DatFile *pDat, SaveSmapState *pState, SmapView *pView,
                   const SmapZoomSet aSets[3], SmapWorldSize *pWorld,
                   bool bSkipZoomRestore);

/* ------------------------------------------------- the product chunk
 *
 * Evidence: Save_WriteProduct @ 0x3f4f4, Save_ReadProduct @ 0x4062c.
 *
 * The product table is three parallel arrays that sit CONTIGUOUS in the original, and the
 * chunk addresses them by a fixed offset from the start of the first one:
 *
 *     A  32-bit, 1600 entries, 6400 bytes, at block +0      (0x159E9C)
 *     B  32-bit, 1600 entries, 6400 bytes, at block +6400   (0x15B79C)
 *     C  16-bit, 1600 entries, 3200 bytes, at block +12800  (0x15D09C)
 *     tail                       1152 bytes, at block +16000 (0x15DD1C)
 *
 * Writing is always the new layout: 6400, 6400, 3200, and the tail is not written at all.
 *
 * Reading depends on the save format version byte at 0x18655C, and this is the part that
 * matters. The OFFSETS stay the same in both branches; only the SIZES shrink:
 *
 *     version >= 3: A 6400, B 6400, then the shared tail read puts C (3200) at +12800
 *     version <  3: A 4608, B 4608, C 2304, then the tail read puts 1152 bytes at +16000
 *
 * So an old save fills entries 0..1151 of each array and leaves 1152..1599 holding whatever
 * was there before the load. The two branches are not two layouts over the same data; they
 * are the same layout read with different widths.
 *
 * The `| 4` in the original's last address is the compiler folding the four-byte gap between
 * &anLineAux[149] and &anLineAux[150]: 12800|4 lands on block +12800 and 16000|4 on block
 * +16000. The real offsets are 12800 and 16000. */
#define PUFO_PRODUCT_BLOCK_BYTES 17152  /* 16000 + 1152, the widest a read can reach */

bool Save_WriteProduct(DatFile *pDat, const u8 *pBlock);
bool Save_ReadProduct(DatFile *pDat, i32 nSaveFmtVer, u8 *pBlock);

/* ---- bases chunk ----
 *
 * `Save_WriteBases @ 0x3f1fc` and `Save_ReadBases @ 0x402c0`. The chunk is a flat
 * sequence of blocks and field-by-field records, not one contiguous table, so the
 * port carries the pieces in `SaveBasesState`.
 *
 * On-disk order, and the byte count each piece costs:
 *
 *   288  block at 0x159768
 *  1152  block at 0x1598DC
 *     1  base count, 0x159D80 (IDA: gUiRootPanel.anLineAux[79])
 *     1  0x159D81
 *     1  0x159D82
 *     4  0x159D90            (anLineAux[83])
 *   8/4  0x159DBC            (anLineAux[94]) -- 8 written, but only 4 READ when the
 *                             save format version byte at 0x18655C is below 3
 *     4  0x1598D8
 *   0/1  0x158848            -- written always, READ only when version >= 3
 *    17  BaseFacFlag[i].bFlag20, i = 0..16, stride 28, array at 0x186E48
 *     9  0x18655D
 *   488  8 facility records, 61 bytes each (see SaveBaseFacility)
 *  then for each base, while the count byte is > 0 as a SIGNED char:
 *    16  BaseRec: bSlot, bUnk4, nStoresTotal, nUnk8, nUnk12, bRegionId, bUnk16
 *   2n   one u16 per object: auPerBaseVal[baseIndex] of every object record
 *
 * Two things the shape of the loop hides.
 *
 * The facility record is serialised field by field in an order that is NOT the
 * in-memory order, and two gaps are skipped. In memory BaseFacility is 64 bytes;
 * on disk it is 61. gap_1 at +0x01 and gap_46 at +0x2e are never written.
 *
 * The per-base column index is the LOOP INDEX, not BaseRec.bSlot: 0x3f3CC seeds a
 * byte offset at 42 and 0x3F4C0 adds 2 per base, so base i writes
 * ObjectRecFull.auPerBaseVal[i] of every object. 42 is 0x2a, the offset of that
 * array.
 */
#define PUFO_BASE_FAC_COUNT   8
#define PUFO_BASE_FLAG_COUNT  17
#define PUFO_BASES_HEADER_BYTES 1974 /* everything before the per-base records */

/* One facility record as it appears on disk: 61 bytes, in the original's scattered
 * field order. The in-memory BaseFacility is 64 bytes and orders the fields
 * differently, so this is a wire type, not a mirror of it. */
typedef struct SaveBaseFacility {
    char szName[20];       /* abName,    +0x1a */
    u8   bType;            /* bType,     +0x00 */
    i32  nBuildState;      /* nUnkBuildState,   +0x38 */
    char szUnk6[20];       /* abUnk6,    +0x06 */
    i32  nFacType;         /* nFacType,  +0x3c */
    i32  nUnk48;           /* nUnk48,    +0x30 */
    u16  uUnk2;            /* uUnk2,     +0x02 */
    u16  uUnk4;            /* uUnk4,     +0x04 */
    i32  nDemolishState;   /* nUnkDemolishState, +0x34 */
} SaveBaseFacility;

typedef struct SaveBasesState {
    u8   abBlock288[288];
    u8   abBlock1152[1152];
    u8   bBaseCount;       /* signed char: gates the per-base loops on both sides */
    u8   bUnk159D81;
    u8   bUnk159D82;
    u8   abUnk159D90[4];
    u8   abUnk159DBC[8];   /* upper 4 bytes stay untouched when version < 3 */
    u8   abUnk1598D8[4];
    u8   bUnk158848;       /* stays untouched when version < 3 */
    u8   abFlag20[PUFO_BASE_FLAG_COUNT];
    u8   aRegionBuildCount[9]; /* 0x18655D: the nine region counters (0x80..0x88) */
    SaveBaseFacility aFac[PUFO_BASE_FAC_COUNT];
} SaveBasesState;

/* The bases chunk's owners in the ORIGINAL are the globals themselves: Save_WriteBases reads
 * 0x159768 (288 B), 0x1598DC (1152 B), 0x159D80, nSelectedBaseIdx @ 0x159D81, unk_159D82,
 * nSoldierHireCount @ 0x159D90, byte_159DBC, nFunds @ 0x1598D8, 0x158848, aBaseFacFlags[i].bFlag20
 * @ 0x186E48, g_anRegionBuildCount @ 0x18655D and MEMORY[0x186C40] straight off them
 * (0x3F21C..0x3F3A4), and Save_ReadBases writes that same list back (0x4032C..0x404C0). The port
 * keeps the same values a SECOND time in SaveBasesState, so the driver has to join the two at this
 * seam: Save_BasesLiveToRecord fills the record from the live cells before the writer runs, and
 * Save_BasesRecordToLive puts them back after the reader ran. Without the second join the readers
 * that follow the bases chunk -- the per-base Base_ClearFacilityGridRow classification at
 * 0x44E38..0x44E7C -- would read whatever the running game had instead of the loaded grid.
 *
 * The two byte blocks are one grid on both sides: 0x159768..0x159887 and 0x1598DC..0x159D5B are
 * grid[0..287] and grid[372..1523] of the port's 1524-byte grid, and the 84 bytes between the two
 * blocks is where the original's nFunds sits (0x1598D8 = grid + 368), which the port models as a
 * separate i32. unk_159D82 has no live cell in the shell yet, so the record is its only home and it
 * keeps what the record already held; nSoldierHireCount @ 0x159D90 gained one when the buy/sell
 * screen became live (the 1000-row hire tally) and now round-trips through it.
 *
 * The pointers are byte pointers because BaseFacFlag and BaseSiteRec live in pufo_geoscape.h, which
 * this header does not include; the offsets used below are the original's own addressing. */
typedef struct SaveBasesLive {
    u8  *abFacilityGrid;      /* abFacilityGrid @ 0x159768, 1524 bytes */
    i8  *pnBaseCount;         /* 0x159D80, signed */
    i8  *pnSelectedBase;      /* nSelectedBaseIdx @ 0x159D81 */
    u8  *abHangarBytes;       /* byte_159DBC, 8 bytes */
    i32 *pnFunds;             /* nFunds @ 0x1598D8 */
    i32 *pnSoldierHireCount;  /* nSoldierHireCount @ 0x159D90, the 4 bytes at 0x3F28C / 0x4039C */
    i8  *pnCraftHomeBase;     /* 0x158848 */
    u8  *abBaseFacRecords;    /* aBaseFacFlags @ 0x186E48: 17 records of 28, bFlag20 at +0x14 */
    u8  *aRegionBuildCount;   /* g_anRegionBuildCount @ 0x18655D, 9 bytes */
    u8  *abSiteRecords;       /* MEMORY[0x186C40]: 8 records of 64, the port's BaseSiteRec */
} SaveBasesLive;

/* apBases and apObjects are the caller's pointer arrays; nObjectCount is the object
 * table size (pCtxBase.nObjectCount). The reader allocates BaseRec records into
 * apBases and frees nOldBaseCount of them first -- the original frees with the count
 * it had BEFORE the chunk is read (0x402D0..0x40310), then reads the new count. */
bool Save_WriteBases(DatFile *pDat, const SaveBasesState *pState,
                     const BaseRec *const *apBases,
                     ObjectRecFull *const *apObjects, i32 nObjectCount);
bool Save_ReadBases(DatFile *pDat, i32 nSaveFmtVer, SaveBasesState *pState,
                    BaseRec **apBases, i32 nOldBaseCount, u8 bUnk186558,
                    ObjectRecFull *const *apObjects, i32 nObjectCount);

/* The two joins between SaveBasesState and the live cells, run by the drivers at the point where the
 * original reaches the globals: Save_BasesLiveToRecord just before Save_WriteBases (the writer's own
 * reads, 0x3F21C..0x3F3A4), Save_BasesRecordToLive just after Save_ReadBases (the reader's own writes,
 * 0x4032C..0x404C0). Both do nothing when pLive is NULL -- that is how the format test drives the chunk
 * with a record of its own and no live world. */
void Save_BasesLiveToRecord(const SaveBasesLive *pLive, SaveBasesState *pState);
void Save_BasesRecordToLive(const SaveBasesState *pState, const SaveBasesLive *pLive);

/* ---- ships chunk ----
 *
 * `Save_WriteShips @ 0x3f6dc` and `Save_ReadShips @ 0x4086c`. One byte ship count, then
 * per ship 61 bytes of fields and one u16 per object.
 *
 * The field order is neither the struct order nor any grouping: nFuel, bHomeBase,
 * nSlotIdx, bCraftType, bWeaponL, uRangeRemain, bHangarX, bHangarY, anCargoQty[16],
 * bNeedsSupply, bCanDogfight, uUnk4, uFuelMax, uRangeMax, bCrewSlots, bType, bAtBase,
 * bWeaponsHot, bAmmo27, bAmmo30, bAmmo29, bAmmo25, bAmmo26, bRefueling, bWeaponR,
 * bId, uFuelCap, uUnk14, uRangeRate, bIntercepting, nStrategIdx, bContactMsg.
 *
 * The per-object column starts at 58 (0x3a), which is ObjectRecFull.aItemQty, so the
 * bases chunk writes the auPerBaseVal columns and this chunk writes the aItemQty ones.
 * The column is again the LOOP INDEX.
 *
 * bType IS saved here (the 16th write, `pBuf: apCraft[0]`), and Ship_Ctor also sets it,
 * so the ctor's per-type counter only decides bType for ships that are created, not
 * loaded.
 *
 * The reader frees the old records AFTER FindChunk but BEFORE the count is read, using
 * the count it already had -- the opposite order from Save_ReadBases. Then 0x40C24:
 * once a ship is loaded, a craft type of 0 forces uRangeMax to 10000.
 *
 * pnShipCount is in/out: on entry it is the count the caller holds (used to free), on
 * exit it is the count from the file. The original uses one global for both.
 */
bool Save_WriteShips(DatFile *pDat, const Ship *const *apShips, i32 nShipCount,
                     ObjectRecFull *const *apObjects, i32 nObjectCount);
bool Save_ReadShips(DatFile *pDat, Ship **apShips, i32 *pnShipCount, ShipCtorEnv *pEnv);

/* Save_AllocFlobjs @ 0x431ac. FindChunk("flobjs"), read the tactical item count, then
 * allocate that many TactItem records (new(0x18) + TactItem_ctor @ 0x3e718) into the
 * caller's array. Unlike the bases and ships readers this one frees NOTHING first.
 *
 * The count read is 1 byte wide or 4 bytes wide, decided by the dword at Map+0x118
 * (0x113AD0): 4 when it is non-zero, 1 when it is zero. Its only two writers in the
 * IDB are Game_StartNew (0x94554) and Base_StripAllCraftCargo (0x69a38); its meaning is
 * NOT established, so the port takes it as an explicit argument rather than guessing.
 *
 * With the 1-byte width the original writes one byte into a 4-byte global, so the upper
 * three bytes of the count keep whatever they had. That is reproduced: the port stores
 * into the low byte and leaves the rest. */
bool Save_AllocFlobjs(DatFile *pDat, i32 nWideCountFlag, i32 *pnTactItemCount,
                      TactItem **apTactItems);

/* Save_WriteFlobjs @ 0x42f48 and Save_ReadFlobjs @ 0x43258 -- the body of the flobjs
 * chunk. 17 bytes per item: the object INDEX, nIndex(2), nAmmo(1), bUnk7(1), bUnk8(1),
 * the next item's pool index(4), the linked soldier's nSaveId(4).
 *
 * The object index is found by a linear search over apObjects for the item's pObj.
 * If nothing matches, the loop ends with the index equal to nObjectCount, so a NOT FOUND
 * object is saved as an out-of-range index and the reader dereferences apObjects[that].
 * Reproduced, with the port refusing instead of reading out of bounds.
 *
 * The two links are stored as INDICES with 0x7FFF as the null sentinel:
 *   next:  pNext == NULL -> 0x7FFF, else pNext->nRefId. Save_AssignRosterIndices @ 0x42ea0
 *          numbers nRefId 0,1,2,... over the same pool the reader indexes into.
 *   soldier: pLinkedSoldier == NULL -> 0x7FFF, else pLinkedSoldier->nSaveId (Soldier + 0x3bc).
 *
 * The writer always stores the count as 4 bytes; the reader uses the same 1-or-4 width gate
 * as Save_AllocFlobjs. Both readers FindChunk and read the count, so a load reads it twice:
 * Save_AllocFlobjs allocates the records and Save_ReadFlobjs fills them. */
bool Save_WriteFlobjs(DatFile *pDat, const TactItem *const *apTactItems, i32 nTactItemCount,
                      const ObjectRecFull *const *apObjects, i32 nObjectCount);
bool Save_ReadFlobjs(DatFile *pDat, i32 nWideCountFlag, i32 *pnTactItemCount,
                     TactItem *const *apTactItems,
                     ObjectRecFull *const *apObjects, i32 nObjectCount,
                     Soldier *const *apSoldiers);

/* ------------------------------------------------- the map chunk
 *
 * Save_WriteMap @ 0x438e0 and Save_ReadMap @ 0x433d8.
 *
 * The chunk is the whole tactical grid: two dimension bytes, then one 43-byte
 * record for every cell of every layer that the map dimensions cover.
 *
 * The grid is a flat pointer array at apCells @ 0x15338, 240 bytes per row and
 * 14400 bytes per layer, so it is 240 rows of 60 pointers no matter how big the
 * map is. Both functions build the same address with the same ARM sequence,
 * `ADD R3, layerCounter, rowCounter / MUL R2, R3, #0xF0 / LDR R4, [base, #0x15338]`,
 * where the layer counter is 0, 60, 120, 180. The port reaches the cells through
 * the MapCellLookup the other map algorithms already use.
 *
 * The dimension bytes are Map.nMapDimX @ +86299 and nMapDimY @ +86300, and the
 * loops run over 10*nMapDimY rows and 10*nMapDimX columns, so a map with dimX =
 * dimY = 6 covers the whole 60 x 60 x 4 grid and a smaller map only its corner.
 *
 * Per cell, in the original's write order:
 *   4 bytes at cell+0x10 (MapCell.abFaceDamage as one block),
 *   bFlags29 bits 0,1,2,3,4 each as their own byte,
 *   nFlags32 & 7, then (nFlags32 >> 3) & 0xF,
 *   bFlags36 & 1,
 *   nFlags40 & 7, then (nFlags40 >> 3) & 0xF,
 *   bFlags44 & 1,
 *   the occupant's nSaveId or 0x7FFF,
 *   the item list head's nRefId or 0x7FFF,
 *   bUnk56,
 *   bFlags57[0] bit 2 and bit 3 each as their own byte,
 *   bFlags57[1],
 *   and the four sprite records as their nGlobalIdx or 0x7FFF, 4 bytes each.
 *
 * Three things only the reader shows:
 *   - it writes nX, nY and nZ from the loop counters instead of reading them, and
 *     clears nPathCost, bPathDir and bPathFlag. Coordinates are derived, not stored.
 *   - restoring bFlags29 bit 4 uses the mask 0x8F, which also CLEARS bits 5 and 6.
 *     Bit 5 is the shot-trace flag Map_ClearShotTraceFlags sweeps, so a load wipes
 *     it for every cell as a side effect of restoring bit 4.
 *   - restoring bFlags44 is `bFlags44 & 0xFC ^ (value & 1)`, which clears bit 1 on
 *     the way in. The low three bits of nFlags32 and nFlags40 are merged with an
 *     XOR, the upper nibble with a mask; both end up as the stored value.
 *
 * What is NOT serialised, proven by pre-filling a cell with ones after the write
 * and reading it back:
 *   - nFlags32 and nFlags40 store only 7 bits each (3 + 4). Bits 7 and up keep
 *     whatever the cell already held in memory, so they are recomputed after a load.
 *   - bFlags57[0] stores only bits 2 and 3. Bits 0 and 1 keep their in-memory value.
 *   - bFlags29 stores bits 0..4 and clears bits 5 and 6; bit 7 survives.
 *   - bUnk17, bUnk18 and bUnk19 are stored as part of the 4-byte block at cell+0x10,
 *     but nPathCost, bPathDir and bPathFlag are cleared rather than read.
 *
 * Deviation: the original dereferences the grid slot unconditionally and indexes
 * apSoldiers / the item array with no bound check. The port skips a NULL slot and
 * refuses an out-of-range index. */
bool Save_WriteMap(DatFile *pDat, const MapGeom *pMap,
                   MapCellLookup pfnLookup, void *pUser);
bool Save_ReadMap(DatFile *pDat, MapGeom *pMap, MapCellLookup pfnLookup, void *pUser,
                  Soldier *const *apSoldiers, i32 nSoldierCount,
                  TactItem *const *apItems, i32 nItemCount,
                  const PufoFont *pFont);
/* The chunk's two dim bytes (0x43400 / 0x43418) read ahead of the shell's grid allocation, so the
 * walk in Save_ReadMap meets the same slots Save_WriteMap wrote. The chunk is positional full
 * coverage -- one record per walk slot -- so both maps functions REFUSE a NULL slot inside the range
 * (Part 103 seam guard, the note in pufo_save.c) instead of skipping and desyncing the streams. */
bool Save_ReadMapDims(DatFile *pDat, MapGeom *pMap);

/* ------------------------------------------------- the four roster chunks
 *
 * Save_WriteTeamUnits @ 0x43c84 and Save_ReadTeamUnits @ 0x44020. One function
 * writes FOUR chunks and one function reads all four back.
 *
 *     teamunits   1 byte count, the selected unit's id, then 30 ids
 *     aliens      1 byte count, then 30 ids
 *     diedsolds   4 byte count, then 60 ids
 *     hiddensold  4 byte count, then 30 ids
 *
 * Every id is the soldier's nSaveId, or 0x7FFF for an empty slot.
 *
 * Four details the disassembly is the only source for:
 *
 *   - The counts are NOT the same width. teamunits and aliens store one byte
 *     (Map.nXcomUnitCount @ 0x1511F and nEnemyUnitCount @ 0x15124 are bytes),
 *     diedsolds and hiddensold store four (nCivilianUnitCount @ 0x15120 is a
 *     dword). A save is 125 + 121 + 244 + 124 bytes of payload.
 *
 *   - Only teamunits stores a selected unit, and it stores it BEFORE the roster,
 *     as the second field of the chunk (0x43CE0), not after the count of any list.
 *
 *   - The writer gates each slot on `index < count AND slot != NULL`, so a slot
 *     past the count is written as 0x7FFF even when the array holds a pointer
 *     there. The reader always consumes the full 30 / 30 / 60 / 30 and ignores
 *     the count it just read, so a stale pointer beyond the count is dropped.
 *
 *   - The hidden reserve is not part of the Map block. It is unk_138DF0, the 30
 *     dwords that start exactly where Map ends (0x1139B8 + 152632 = 0x138DF0),
 *     which is why Hex-Rays reaches it as `&v10[1].gap_TrajRest[45476]`.
 *
 * Deviation: the reader indexes apSoldiers with no bound check; the port refuses
 * an out-of-range id. */
#define PUFO_SAVE_ROSTER_MAX 30 /* the teamunits, aliens and hiddensold slot count */
#define PUFO_SAVE_CIV_MAX    60 /* the diedsolds slot count */
typedef struct TeamUnitsState {
    u8      nXcomCount;      /* Map.nXcomUnitCount @ 0x1511F, one byte */
    u8      nEnemyCount;     /* Map.nEnemyUnitCount @ 0x15124, one byte */
    i32     nCivilianCount;  /* Map.nCivilianUnitCount @ 0x15120, four bytes */
    i32     nHiddenCount;    /* Map.nEnemyReserveCount */
    Soldier **apXcomUnits;   /* Map.apXcomUnits @ 0x15154, 30 entries */
    Soldier **apEnemyUnits;  /* Map.apEnemyUnits @ 0x151CC, 30 entries */
    Soldier **apCivilians;   /* Map.apCivilians @ 0x15244, 60 entries */
    Soldier **apHiddenUnits; /* unk_138DF0, 30 entries, the block right after Map */
    Soldier  *pSelUnit;      /* Map.pSelUnit @ 0x15334 */
} TeamUnitsState;

bool Save_WriteTeamUnits(DatFile *pDat, const TeamUnitsState *pState);
bool Save_ReadTeamUnits(DatFile *pDat, TeamUnitsState *pState,
                        Soldier *const *apSoldiers, i32 nSoldierCount);

/* ------------------------------------------------- the tactical session block
 *
 * Save_WriteTGameData @ 0x446a8. One call writes EIGHT chunks in this order:
 *
 *     tgamedata (43 bytes), teamunits, aliens, diedsolds, hiddensold,
 *     flobjs, map, v124.dat (8 bytes)
 *
 * so it is the tactical half of a save, and it calls the three writers above
 * rather than duplicating them.
 *
 * The tgamedata payload is 23 fields in a fixed order. The offsets come from the
 * IDB's Map struct gaps, and two of them are already known fields, which is what
 * pins the arithmetic: gap_86272[20] is 0x15114, the port's nMapLayers, and
 * Map_nMissionType is 0x1511A.
 *
 * The two bytes after the mission type are Map+0x1511D and Map+0x1511E. The writer
 * loads them as (unk_128AD5 - 0x1139B8) and (unk_128AD6 - 0x1139B8), and 0x128AD5 -
 * 0x1139B8 is 0x1511D. An earlier note here called them 0x1517D and 0x1517E, which
 * was wrong: those are different bytes. unk_128AD5 is the mode byte that
 * Tact_SelectTeamUnits (0x39744) dispatches on, so it is named bMissionMode here.
 *
 * The dword at Map+0x15116 is written as four bytes and Save_ReadTGameData (0x44248, NOT ported)
 * consumes it as four sprite-set ids: for each byte that is not 23 it loads the
 * sprite set named `xcom_<byte>` (aXcom0 @ 0xa7be8, stride 9) through Font_LoadSpr,
 * then rebuilds the font index. 23 means "no set for this slot". That is the only
 * place the four faction sprite sets of a mission are recorded.
 *
 * The three craft-tile fields are dwords in memory and are written one byte each.
 *
 * v124.dat is two dwords at unk_138E6C and unk_138E70. On the read side they are
 * optional: if the chunk is missing the original rolls them with Game_RandU16 as
 * `rand(40) + 10` and `rand(20)`. */
typedef struct TactSaveState {
    u8   bUnk15110;       /* 0x15110, gap_86272[16] */
    i32  nUnk1510C;       /* 0x1510C, gap_86272[12] */
    u8   abUnk15111[3];   /* 0x15111..0x15113, gap_86272[17..19] */
    u8   nMapLayers;      /* 0x15114, gap_86272[20] */
    u8   bUnk15115;       /* 0x15115, gap_86272[21] */
    u8   abSpriteSet[4];  /* 0x15116..0x15119, gap_86272[22]; 23 means no set */
    u8   nMissionType;    /* 0x1511A */
    u8   bMissionMode;    /* 0x1511D, unk_128AD5: the mode Tact_SelectTeamUnits dispatches on */
    u8   bUnk1511E;       /* 0x1511E, unk_128AD6 */
    /* An earlier note here used 0x1517D/0x1517E for the mode bytes and 0x15188..0x151A0 for these
     * three groups; a bad Map-base subtraction. The writer's data refs are the ones below. */
    i32  anUnk15188[4];   /* Map_nCursorX @0x15128, Map_nCursorY @0x1512C, Map_nCamScrollX @0x15130,
                           * Map_nCamScrollY @0x15134 (0x128AE0/04/08/0C) */
    u8   abUnk15198[3];   /* nLayerScratch @0x15138, Map_nCraftMapX @0x15139, Map_nCraftMapY @0x1513A
                           * (0x128AF0/01/02) */
    i32  nUnk151A0;       /* Map_nDeployedHwpsCount @ Map+0x15140 (0x128AF8) */
    i32  anCraftTile[3];  /* Map_bSpawnAlt @0x15148, Map_nSpawnTileX @0x1514C, Map_nSpawnTileY @0x15150,
                           * stride 4, written one byte each */
    i32  nUnk138E6C;      /* v124.dat, first dword */
    i32  nUnk138E70;      /* v124.dat, second dword */
} TactSaveState;

/* The flobjs chunk's inputs, bundled so Save_WriteTGameData can pass them on. */
typedef struct FlobjsState {
    const TactItem *const *apTactItems;
    i32 nTactItemCount;
    const ObjectRecFull *const *apObjects;
    i32 nObjectCount;
} FlobjsState;

bool Save_WriteTGameData(DatFile *pDat, const TactSaveState *pSt,
                         const TeamUnitsState *pRosters, const FlobjsState *pFlobjs,
                         const MapGeom *pMap, MapCellLookup pfnLookup, void *pUser);

/* ------------------------------------------------- the rmp chunk
 *
 * Save_ReadRmp @ 0x43ee0. A count, then SIX blocks, then one Map_LoadRmp call per
 * module. There is no writer for this chunk: it records the random-map modules a
 * mission was built from, and Tact_InitMission (0x3c550) is the other caller.
 *
 * The six blocks are 60 dwords each (240 bytes) except the module names, which are
 * 60 * 20 = 1200 bytes. The order they are READ in is not the order the loader
 * takes them in:
 *
 *     read 1 -> nTileX   read 2 -> nTileY   read 3 -> nSizeX   read 4 -> nSizeY
 *     read 5 -> the 20-byte names          read 6 -> nLevel
 *
 * and the call is Map_LoadRmp(rmp, nTileX[i], nTileY[i], nSizeX[i], nSizeY[i],
 * name + 20*i, nLevel[i]). Getting that order wrong silently builds the map with
 * sizes where levels should be, so the port takes the loader as a callback and the
 * test records the arguments.
 *
 * The loop only runs when the count is positive. Deviation: the original indexes
 * its six fixed 60-entry arrays with no bound check; the port refuses a count
 * above 60. */
#define PUFO_SAVE_RMP_MODULE_MAX 60

typedef i32 (*RmpModuleLoad)(RmpData *pRmp, const char *pszBase,
                             i32 nTileX, i32 nTileY, i32 nSizeX, i32 nSizeY,
                             i32 nLevel, void *pUser);

bool Save_ReadRmp(DatFile *pDat, RmpData *pRmp, RmpModuleLoad pfnLoad, void *pUser);

/* ------------------------------------------------- the save/load slot list
 *
 * SaveSlot_FormatList @ 0x45904 builds the seven lines of the save and load screens.
 * It is a reader of seven SEPARATE files: for slot n it opens `<dir>sav<n>.sav`, so the
 * list is built by opening each save in turn, not from one file.
 *
 * The path is built as `MEMORY[0x1883B0] + "sav" + itoa(n) + ".sav"` (0x45944..0x45994),
 * and the same 296-byte buffer is then reused as the scratch for every itoa after the
 * file is open. The first itoa uses a separate 52-byte buffer because the path is still
 * needed at that point.
 *
 * A missing file gives the whole line from one text record:
 *
 *     aTextRecs[205 * 1074] = "Game not saved"
 *
 * A present file is opened, and the line is built from the `time` chunk:
 *
 *     read 4 bytes into GameTime.nTotalSec, GameTime_Decode, then
 *     (day   < 10 ? "0"  : "")  + day
 *     (month >= 10 ? "."  : ".0") + month
 *     "." + year
 *     (hour  >= 10 ? " "  : " 0") + hour
 *     (minute>= 10 ? ":"  : ":0") + minute
 *
 * The padding is the interesting part: the separator doubles as the pad character, so a
 * single-digit month renders as ".03" and a single-digit hour as " 09". The prefix copied
 * at 0x459E4 is aTextRecs[205 * 1800], which is PAST the end of TEXTS.DAT (1721 records),
 * so in this data set it is an empty string and only terminates the line. That is the same
 * out-of-range record UiControl_LoadPads reads as an empty pad name.
 *
 * Then the `gamestate` chunk is read into bGameState @ 0x15821C, which is the flag that
 * says a tactical mission is stored in the file. When it is set the line gets
 *
 *     aTextRecs[205 * 1075] = " (mission)"
 *
 * and, only if a `mcnt` chunk is present, " " plus that byte as a decimal number.
 *
 * Two side effects worth knowing:
 *
 *   - the decoded date is written into the LIVE clock. Only nTotalSec is restored at
 *     0x45C10; year, month, day, hour and minute are left holding the values decoded from
 *     the LAST save file that was inspected.
 *   - bGameState @ 0x15821C is overwritten from the last file that had a gamestate chunk.
 *
 * FindChunk and Read results are ignored by the original: a save without a time chunk
 * simply formats whatever nTotalSec already held.
 *
 * Deviation: the original concatenates with unbounded strcat into a 40-byte line; the port
 * bounds every append to 39 characters plus the terminator. */
#define PUFO_SAVE_SLOT_COUNT 7
#define PUFO_SAVE_SLOT_LINE_SIZE 40
#define PUFO_SAVE_TEXT_NOT_SAVED 1074 /* "Game not saved" */
#define PUFO_SAVE_TEXT_MISSION   1075 /* " (mission)"     */
#define PUFO_SAVE_TEXT_LINE_PREFIX 1800 /* past the end of TEXTS.DAT: empty */

typedef struct SaveSlotFormatCtx {
    char (*apLines)[PUFO_SAVE_SLOT_LINE_SIZE]; /* 7 lines, stride 40 (0x45BFC) */
    const char *pszSaveDir;                    /* MEMORY[0x1883B0] */
    GameTime *pTime;                           /* the live clock, used as scratch */
    u8 *pbGameState;                           /* bGameState @ 0x15821C */
    const char *pszPrefix;                     /* aTextRecs[205 * 1800] */
    const char *pszNotSaved;                   /* aTextRecs[205 * 1074] */
    const char *pszMission;                    /* aTextRecs[205 * 1075] */
} SaveSlotFormatCtx;

void SaveSlot_FormatList(SaveSlotFormatCtx *pCtx);

/* ------------------------------------------------- the strateg chunk
 *
 * Save_WriteStrateg @ 0x3fbb4 and Save_ReadStrateg @ 0x40f4c. The chunk is a 134-byte
 * header followed by the strateg list, 113 bytes per item.
 *
 * The header is 38 writes in a fixed order that is NOT address order. Most of these globals
 * have no known meaning yet, so each field is named for the address it comes from; the ones
 * that are known are named for what they are:
 *
 *   bSaveVersion   0x18655C  the save format version. It is SAVED here, and Save_ReadProduct,
 *                            Save_ReadBases and Save_ReadShips all branch on it.
 *   nStrategCount  0x186530  the strateg item count. It is NOT the PtrArray's own count
 *                            field: the list at strategList @ 0x186534 keeps its count at
 *                            0x186534 and its item array at 0x186538, so 0x186530 is a
 *                            separate global that mirrors the count and is also the loop
 *                            bound in both the writer and the reader.
 *   abTypeCounters 0x18653C..0x186540  one byte per craft type. Ship_ctor @ 0x45c20 bumps
 *                            them and stores the result in Ship.bType, so saving them is what
 *                            lets bType survive a load.
 *   nSelectedShipIdx 0x187054  the geoscape's selected ship.
 *
 * Two header entries are used as POINTERS, not values: 0x1865A8 and 0x186594 hold addresses
 * that DatFile_Write is given directly, so the saved value is what they point at. The port
 * carries them as pointer fields for the same reason.
 *
 * The block 0x186568 .. 0x1865BC is 22 dwords and 21 of them are written, in a scattered
 * column-by-column order. 0x18657C is never touched, so it is the one header dword a save
 * does not carry.
 *
 * The item fields are written in an order that is neither address order nor size order, and
 * the entry at +0 is written TWELVE fields into the record rather than first. The port keeps
 * that order exactly, because it is what the byte layout on disk is.
 *
 * Reader differences from the writer, all of them load-bearing:
 *
 *   - PtrArray_DeleteAll runs BEFORE the header is read, so a failed load still empties the
 *     strateg list. FindChunk's result is not checked either.
 *   - the version byte is read into a scratch and DISCARDED. The per-item gate at 0x41554 then
 *     tests the build's own 0x18655C, not the value that came from the file. A save written by
 *     an older build is therefore read with the CURRENT build's record size.
 *   - nTargetX and nTargetY are only read when that version is above 2. An older record is 105
 *     bytes, not 113, and those two fields keep whatever StrategItem_AllocAdd left in them.
 *   - items are created by StrategItem_AllocAdd as the loop runs, and the loop is a do/while on
 *     the count that was just read, so a count of zero or less allocates nothing.
 *
 * Deviation: the original reads and writes through fixed globals and never checks a DatFile
 * result; the port takes the state and the list as arguments and returns false when the chunk
 * is missing, while still emptying the list the way the original does. */
typedef struct StrategSaveState {
    i8   bSaveVersion;        /* 0x18655C */
    i32  nUnk186558;          /* 0x186558 */
    i32  nUnk186550;          /* 0x186550 */
    i32  nUnk186554;          /* 0x186554 */
    i32  nStrategCount;       /* 0x186530, a separate count global, not the PtrArray's own */
    u8   abTypeCounters[5];   /* 0x18653C .. 0x186540 */
    i32  nUnk186544;          /* 0x186544 */
    i32  nUnk186548;          /* 0x186548 */
    i32  nUnk18654C;          /* 0x18654C */
    i32  nUnkMap6691p4845;    /* Map + 0x6691 + 4845 */
    i32  nUnk1865BC;          /* 0x1865BC */
    i32  nUnk1865B4;          /* 0x1865B4 */
    i32  nUnk1865A0;          /* 0x1865A0 */
    i32  nUnk18658C;          /* 0x18658C */
    i32  nUnk186574;          /* 0x186574 */
    i32  nUnk1865B8;          /* 0x1865B8 */
    i32  nUnk1865A4;          /* 0x1865A4 */
    i32  nUnk186590;          /* 0x186590 */
    i32  nUnk186578;          /* 0x186578 */
    i32 *pnUnk1865A8;         /* 0x1865A8, used as a POINTER */
    i32 *pnUnk186594;         /* 0x186594, used as a POINTER */
    i32  nUnk186580;          /* 0x186580 */
    i32  nUnk186568;          /* 0x186568 */
    i32  nUnk1865B0;          /* 0x1865B0 */
    i32  nUnk18659C;          /* 0x18659C */
    i32  nUnk186588;          /* 0x186588 */
    i32  nUnk186570;          /* 0x186570 */
    i32  nUnk1865AC;          /* 0x1865AC */
    i32  nUnk186598;          /* 0x186598 */
    i32  nUnk186584;          /* 0x186584 */
    i32  nUnk18656C;          /* 0x18656C */
    i32  nUnk186BE4;          /* 0x186BE4 */
    i32  nUnkMap6691p6937;    /* Map + 0x6691 + 6937 */
    i32  nSelectedShipIdx;    /* 0x187054 */
} StrategSaveState;

/* 134 header bytes: 1 + five counters + 32 dwords. */
#define PUFO_SAVE_STRATEG_HEADER_SIZE 134
/* 113 bytes per item in the current format, 105 when the version is 2 or lower. */
#define PUFO_SAVE_STRATEG_ITEM_SIZE     113
#define PUFO_SAVE_STRATEG_ITEM_SIZE_OLD 105

bool Save_WriteStrateg(DatFile *pDat, const StrategSaveState *pSt, const PtrArray *pList);
bool Save_ReadStrateg(DatFile *pDat, StrategSaveState *pSt, PtrArray *pList);

/* ------------------------------------------------- the save driver
 *
 * Evidence: SaveGame @ 0x44988.
 *
 * One call writes a whole slot file. It renders a frame first, builds
 * `<dir>sav<n>.sav`, opens the slot as a 100-entry container on the global handle
 * unk_158220, writes the chunks in the order below, and closes.
 *
 *     tactver, gamestate, bases, product, research, ships, smap, strateg, time,
 *     soldiers            (always)
 *     mcnt, tgamedata, objbefore, rmp   (only when Map.bTactFlag is set)
 *
 * Three things here are not obvious from the chunk list:
 *
 *   - `Save_AssignRosterIndices` runs BETWEEN the gamestate chunk and the bases
 *     chunk, not at the end. It is what gives every soldier its nSaveId and every
 *     tactical item its nRefId, so the flobjs links written later are valid.
 *   - The mission counter unk_138E8C is INCREMENTED at save time, before the mcnt
 *     chunk is written. Saving a mission advances the counter; it does not record
 *     a counter someone else set.
 *   - The rmp chunk has no writer function. Its seven writes are inline here:
 *     count as 4 bytes, then tileX, tileY, sizeX, sizeY (240 each), the 1200-byte
 *     name block, and level LAST. That is the same order Save_ReadRmp reads them
 *     in, and the same order Map_LoadRmp does NOT take its arguments in.
 *
 * nUiScreen is cleared only in the tactical branch, so saving from the geoscape
 * leaves the screen id alone while saving from a mission closes the screen.
 *
 * Deviation: the original returns whatever DatFile_Close left in R0, and ignores
 * every write failure. The port returns false as soon as a step fails, and it
 * still closes the file, because a half-written slot is worse than a failed one.
 * `Save_AssignRosterIndices` is the one call whose result is deliberately ignored,
 * because it reports false whenever the tactical flag is clear and the original
 * carries on regardless. The caller must have run DatFile_ctor on pDat first; the
 * original's handle is a zero-initialised global. */
typedef struct SaveGameInput {
    const char *pszAppDir;        /* MEMORY[0x1883B0] */
    i32         nSlot;            /* 0..6 in practice, unbounded in the original */
    u8          bTactFlag;        /* Map.bTactFlag @ 0x1139B8 */
    i32        *pnMissionCount;   /* unk_138E8C, incremented in the tactical branch */
    u16        *pnUiScreen;       /* Map.nUiScreen, cleared in the tactical branch */

    /* Not const: Save_BasesLiveToRecord stages the live bases cells into this record right before
     * Save_WriteBases reads it, which is the original reading its globals (0x3F21C..0x3F3A4). */
    SaveBasesState           *pBases;
    const SaveBasesLive      *pBasesLive;   /* the cells the original owns (NULL: no live world) */
    const BaseRec *const     *apBases;
    const u8                 *pProductBlock;
    const SaveResearchState  *pResearch;
    const Ship *const        *apShips;
    i32                       nShipCount;
    const SaveSmapState      *pSmap;
    const StrategSaveState   *pStrateg;
    const PtrArray           *pStrategList;
    const SaveTimeRecord     *pTime;

    Soldier **apSoldiers;         /* non-const: AssignRosterIndices writes nSaveId */
    i32       nSoldierCount;
    TactItem **apTactItems;       /* non-const: AssignRosterIndices writes nRefId */
    i32       nTactItemCount;
    ObjectRecFull *const     *apObjects;
    i32                       nObjectCount;

    const TactSaveState      *pTact;
    const TeamUnitsState     *pRosters;
    const FlobjsState        *pFlobjs;
    const MapGeom            *pMap;
    MapCellLookup             pfnLookup;
    void                     *pLookupUser;
    const RmpData            *pRmp;

    void (*pfnRenderFrame)(void *pUser);   /* Render_Frame (0x22310), NOT ported */
    void  *pRenderUser;
} SaveGameInput;

bool SaveGame(DatFile *pDat, const SaveGameInput *pIn);

/* ------------------------------------------------- the load driver's tactical half
 *
 * Evidence: Save_ReadTGameData @ 0x44248.
 *
 * This is the reader for everything Save_WriteTGameData writes, and it does more than
 * read fields back:
 *
 *   1. the 23 tgamedata fields;
 *   2. Font_LoadTactSprites (0x257c4, NOT ported, injected as a hook), then for each of
 *      the four sprite-set bytes that is not 23 it sets unk_15827A[i] from unk_15829E
 *      and loads the sprite set named `xcom_<byte>` (aXcom0 @ 0xa7be8, stride 9) through
 *      Font_LoadSpr @ 0x461d8, then rebuilds the font index with Font_BuildIndex @ 0x46480;
 *   3. Save_ReadTeamUnits, Save_ReadFlobjs and Save_ReadMap, in that order;
 *   4. a tactical reset of about thirty fields in the Map block, which is where a
 *      loaded mission is made playable again;
 *   5. v124.dat, which is OPTIONAL: when the chunk is absent the two dwords are ROLLED
 *      with Game_RandU16 as `rand(40) + 10` and `rand(20)`.
 *
 * The reset is a mix of clears and sets. `bPlayerInputEnabled` is SET to 1, and
 * apUiSlotsA[4] and apUiSlotsA[10] are set to the values 200 and 10, not to pointers.
 * apUiSlotsA is `void *[28]` at Map+0x60, so those slots hold small integers.
 *
 * Deviation: the original checks none of the tgamedata reads, so a truncated chunk
 * leaves the fields at whatever they were. The port reports false when a read fails but
 * still runs the rest of the sequence, because the font and roster work is what makes
 * the mission usable. */

/* The tactical fields the load resets, named by the original's Map offsets (the Map
 * struct @ 0x1139B8). Only the fields the original actually touches are listed. */
typedef struct TactResetState {
    u8   abGap01[2];          /* Map+0x01 and Map+0x02, both cleared */
    i32  nMultiTargetCount;   /* Map+0x04 */
    i32  nUnk2C;              /* Map+0x2C, reached as a dword through nUnk40+4 */
    u8   nAnimPhase;          /* Map+0x37 */
    u8   bPlayerInputEnabled; /* Map+0x38, SET to 1 rather than cleared */
    u8   bUnk3C;              /* Map+0x3C, gap_60[0] */
    u8   abFire[10];          /* Map+0x50..0x59: 0,1,2 cleared, 4..7 as one dword, 8,9 cleared */
    u8   bGameTickHold;       /* Map+0x5A */
    u8   bEnemyTurnActive;    /* Map+0x5B */
    u8   bBattleEnded;        /* Map+0x5C */
    u32  aUiSlot[11];         /* Map+0x60 apUiSlotsA: 0,3,5,6,7,8,9 cleared, 4 = 200, 10 = 10 */
    u16  nUiScreen;           /* Map+0x3A */
} TactResetState;

/* The flobjs chunk's read side, bundled the same way FlobjsState bundles the write side. */
typedef struct FlobjsReadState {
    i32                  nWideCountFlag; /* Map+0x118: the count is 4 bytes wide when non-zero */
    i32                 *pnTactItemCount;
    TactItem *const     *apTactItems;
    ObjectRecFull *const *apObjects;
    i32                  nObjectCount;
    Soldier *const      *apSoldiers;
    /* Pointer rather than copy (Part 106): the original reads nSoldierCount (Map + 0x1998 @ 0x115350)
     * live from the global in every chunk reader, and the SOLDIERS reader stores the file's count into
     * that global first (0x417D4 and 0x43138). A value copy taken before the load holds the pre-load
     * roster size, so an occupant id from the saved roster can fail the guard and abort the MAP read. */
    i32                 *pnSoldierCount;
} FlobjsReadState;

typedef struct TactLoadHooks {
    /* Font_LoadTactSprites @ 0x257c4 is ported in pufo_font.c; the caller installs it here, built
     * from the Map scalars it owns, for the same reason the terrain builder's copy is a hook: the
     * load ends with Font_BuildIndex, which rebuilds the index the sprite-set loads then read. */
    void (*pfnLoadTactSprites)(void *pUser);
    bool (*pfnLoadSpr)(i32 nSpriteSet, void *pUser); /* Font_LoadSpr(font, "xcom_<n>") */
    void (*pfnBuildIndex)(void *pUser);        /* Font_BuildIndex @ 0x46480 */
    u32  (*pfnRandU16)(u32 nMax, void *pUser); /* Game_RandU16 @ 0x3e59c */
    /* The 23 tgamedata fields land live in the original's Map block (Map+0x15110..0x151A0), and
     * 0x4444C's Font_LoadTactSprites reads them from there -- Map_nMapLayers @ 0x15114 among them.
     * The port stages them in TactSaveState, so this hook is what makes them live at that point. */
    void (*pfnApplyState)(const TactSaveState *pSt, void *pUser);
    /* Same for the ~30-field tactical reset at 0x444C0..0x445CC: the original clears those cells in
     * place, before Map_PatchCraftSprites and MapView_FocusSelected run. The staged copy needs the
     * same moment. */
    void (*pfnApplyReset)(const TactResetState *pReset, void *pUser);
    /* Save_ReadTeamUnits writes its four counts and Map_pSelUnit straight into the live cells (0x44048,
     * 0x4409C, 0x44100, 0x4416C, 0x441D8); the port stages them in TeamUnitsState, so the caller gets
     * them back at the same moment. */
    void (*pfnApplyRosters)(const TeamUnitsState *pSt, void *pUser);
    void            *pUser;
    PufoFont        *pFont;                    /* fontSys @ 0x158268: read at 0x44474 (nTotalSprFrames) and written at 0x4447C (anAlienSprBase) */
    u8              *pbUnk157198Flag;          /* unk_157198->bUnk70115, cleared through a pointer */
} TactLoadHooks;

/* 23 means "no sprite set for this slot". The constant itself is in game/pufo_mapcell.h because
 * Tact_SpawnAliens needs it too. */

bool Save_ReadTGameData(DatFile *pDat, TactSaveState *pSt,
                        TeamUnitsState *pRosters, const FlobjsReadState *pFlobjs,
                        MapGeom *pMap, MapCellLookup pfnLookup, void *pLookupUser,
                        TactResetState *pReset, const TactLoadHooks *pHooks);

/* ------------------------------------------------- the load driver
 *
 * Evidence: LoadGame @ 0x44bbc. The mirror of SaveGame for `sav<n>.sav`, and the keystone
 * of a cold start: it reads every chunk, tears down whatever UI generation the save
 * replaces, and re-derives the geoscape state the readers do not carry themselves.
 *
 * Order, all of it load-bearing:
 *
 *   1. build `<dir>sav<n>.sav`; if the file does not exist return 0 immediately;
 *   2. one Render_Frame, then open the slot as a 100-entry container (mode 1 = read);
 *   3. `tactver`: if the chunk is present read 2 bytes into a zero-initialised dword and
 *      store the VALUE at slot-context + 0x118 (`STR R3,[R7,#0x118]`, 0x44C94). R7 is the
 *      first argument, which every caller passes as &MainMenu_pSlotCtx @ 0x156938 — the
 *      7x40-byte save/load line buffer — so the dword lands right after the lines at
 *      0x156A50. No code ever reads it (the only 0x118 access on that base is this store),
 *      so it is write-only residue; the port hands the value to the caller through
 *      pnTactVerOut. It is NOT the object table: apObjects is Map+0x108, a different block.
 *   4. Save_ReadGameState — this is what fills bGameState (0x15821C), "the save was made
 *      inside a mission", and every gate below reads it after this point;
 *   5. when the current screen is 17 (the start menu): StartUi_Shutdown, Geoscape_InitUI(1),
 *      and zero the soldier count low half, the base count byte, the craft count byte and
 *      the strateg count — the start-menu load starts from an empty world;
 *   6. the three-way teardown on bTactFlag / bGeoscapeUi / bGameState:
 *        bTactFlag && !bGameState -> Tact_Shutdown(0) then Geoscape_InitUI(1)
 *        bGeoscapeUi && bGameState -> GeoscapeUi_FreeAll(0)
 *        bGeoscapeUi && !bGameState -> NOTHING (the one combination that frees no UI)
 *        !bGeoscapeUi -> Tact_Shutdown(0)
 *   7. Save_ReadStrategVer FIRST (the version byte that sizes the other chunks lives in
 *      the strateg chunk), then bases, product, research, ships, smap, strateg, time,
 *      and soldiers only when bGameState is clear;
 *   8. MainMenu_nListMode = 1; when bGameState is set, read the 1-byte `mcnt` into the
 *      mission counter (0 when the chunk is absent) and run Tact_InitMission(1);
 *   9. close, GameTime_Decode, then the re-derivation passes below.
 *
 * The re-derivation passes, in order:
 *
 *   - per base: Base_ClearFacilityGridRow into the site-state array;
 *   - version < 3 only: StrategList_PurgeType5, then every active craft item whose
 *     coordinates exceed +/-1000 is teleported to (150, 250) and gets a mission site;
 *   - per craft: find its strateg item by nDisplayId; when there is none, add one from the
 *     craft's home base (inactive, invisible, id = Ship.bId) and take the new last index;
 *     then if Ship.bWeaponsHot is set and the item is beyond +/-1000, clear bWeaponsHot and
 *     set Ship.bAtBase; then if bAtBase and the item is visible, clear bVisible;
 *   - version < 3 only: every active craft item whose target exceeds (800, 600) gets its
 *     target reset to its destination;
 *   - per base: the product-C/manuf-cols byte at grid+1620 (the type-byte array) is set to
 *     -(C[119]) & 0xFF - C[120] - C[121] + lowbyte(B[121]) + lowbyte(B[120]) + lowbyte(B[119])
 *     of that base's product columns, looked up by the base's own bUnk4 type byte;
 *   - the 6x6 facType fill: for every cell whose tileA byte is 0 AND whose facType byte is 0,
 *     facType becomes 255;
 *   - version < 3 only: the research-unlock switch — topics 0..16 (10 excluded) with the
 *     permuted map {0..5,13,6,7,14,-,15,16,12,10,8,9} set that base-facility flag byte to 1
 *     when the topic record's byte +0x0D is non-zero;
 *   - version < 3 only: the product rebuild — the four arrays are copied to scratch, zeroed,
 *     and re-spread through the facility->item map at block+16000 (144 bytes per base, one
 *     byte per research topic, the byte is the item index);
 *   - the shot object is apObjects[0] itself (0x45750 reads [Map+0x108]): nCraftCount words
 *     starting at word 29 (byte 58) and word 101 (byte 0xCA) are zeroed;
 *   - every active craft item: bDetected = bVisible;
 *   - per base: the radar byte = Base_CountEmptyFacilityCells, then the five
 *     Base_Update*Needed passes with budgets computed the original's way (occupied space +
 *     outbound transfer space; storage used + item quantity across bases; psi soldiers;
 *     active research; active manufacturing);
 *   - bCraftSelectAssaultFlag = 0, the save version byte is stamped 3, return 1.
 *
 * Deviations: the original checks no DatFile result after the existence test — a missing
 * chunk leaves the state at whatever the previous load left; the port reports false as soon
 * as a chunk read fails but still runs the re-derivation passes, because they are what make
 * a partially loaded world consistent. The original's unchecked pointer dereferences
 * (apObjects[0], the shot object, the craft slots) are guarded. The three budget helpers
 * Base_CountOutboundTransferSpace @ 0x68044, Base_CountItemQtyAcrossBases @ 0x67d68 and
 * Base_CountPsiSoldiers @ 0x65a08 are ported in pufo_geoscape.c and called directly; only
 * the four UI teardown/init calls whose envs belong to the caller remain hooks. */
typedef struct LoadGameHooks {
    void (*pfnRenderFrame)(void *pUser);               /* Render_Frame (0x22310), caller's frame env */
    void (*pfnStartUiShutdown)(void *pUser);           /* StartUi_Shutdown (0x38d7c), caller's shutdown env */
    void (*pfnGeoscapeInitUi)(i32 nMode, void *pUser); /* Geoscape_InitUI (0x857f4), NOT ported */
    void (*pfnTactShutdown)(void *pUser);              /* Tact_Shutdown(0) (0x3d568), caller's env */
    void (*pfnGeoscapeUiFreeAll)(void *pUser);         /* GeoscapeUi_FreeAll(0) (0x86d20), caller's env */
    bool (*pfnTactInitMission)(i32 nMode, void *pUser);/* Tact_InitMission(1) (0x3c550), caller's env */
    void *pUser;
} LoadGameHooks;

typedef struct LoadGameEnv {
    const char *pszAppDir;             /* szAppDir @ 0x1883B0 */
    i32         nSlot;

    u8   *pbTactFlag;                  /* Map.bTactFlag @ 0x1139B8 */
    u8   *pbGeoscapeUi;                /* Map_bGeoscapeUi */
    u8   *pbGameState;                 /* unk_15821C, written by Save_ReadGameState */
    u16  *puUiScreen;                  /* Map_nUiScreen */
    i32  *pnSoldierCount;              /* unk_115350 @ Map+0x1998; the start-menu clear hits its LOW half */
    i32  *pnListMode;                  /* MainMenu_nListMode @ 0x157188, set to 1 */
    u8   *pbMissionCount;              /* byte_138E8C, from the mcnt chunk */
    u8   *pbCraftSelectAssaultFlag;    /* bCraftSelectAssaultFlag @ 0x1870A8 */

    SaveBasesState    *pBases;         /* includes the base count byte and the type-byte array */
    const SaveBasesLive *pBasesLive;   /* the cells the original owns; Save_BasesRecordToLive writes the
                                        * chunk back into them right after Save_ReadBases (0x4032C..0x404C0),
                                        * so the readers that follow see the loaded grid (NULL: no live world) */
    BaseRec          **apBases;        /* Map+0x1174 */
    i32               nOldBaseCount;   /* the count BEFORE Save_ReadBases frees */
    u8               *pProductBlock;   /* 17152 bytes: A u32, B u32, C u16, facility->item map */
    SaveResearchState *pResearch;
    Ship             **apCraft;        /* Map+0x10AC */
    u8               *pbCraftCount;    /* Map_nCraftCount @ Map+0x1194 */
    ShipCtorEnv      *pShipEnv;
    SaveSmapState    *pSmap;
    SmapView         *pView;
    const SmapZoomSet *paZoomSets;     /* the three zoom levels Save_ReadSmap restores */
    SmapWorldSize    *pWorldSize;
    StrategSaveState *pStrateg;
    PtrArray         *pStrategList;    /* the list at 0x186534 */
    i32              *pnSeed;          /* 0x18640C, the per-item seed counter */
    Soldier         **apSoldiers;
    i32              nSoldierCapacity;
    ObjectRecFull *const *apObjects;     /* the object table Map+0x108, for the per-base/per-ship u16 columns */
    i32              nObjectCount;
    u32             *pnTactVerOut;       /* the tactver value, original slot: MainMenu_pSlotCtx+0x118 (write-only) */
    GeoscapeWorld    *pWorld;          /* apCraft / nShipCount / apSiteState / apRadarPct / pTime */
    u8              *abFacilityGrid;   /* the 1524-byte grid */
    const u16       *apResearchCols;   /* 0x16022C, 144 u16 per base */
    u16             *apManufCols;      /* 0x15E19C, 200 u16 per base. NOT read-only: LoadGame's version-<3
                                        * tail uses it as the fourth scratch array -- copy out @ 0x455F8
                                        * (0xC80 bytes), clear @ 0x45638 (0xC80 bytes), re-spread writes @
                                        * 0x45714 (STRH). In the original it sits at anchor+0x4304, right
                                        * after the 17152-byte product block, which is why the block's own
                                        * offsets reach it there; in the shell it is a separate array, so
                                        * the writes MUST go through this pointer. */
    u16             *puResearchEntries;/* 0x160C4C, shared by every base */
    u16             *puShotObjectWords;/* the word array of the shot object = apObjects[0] */
    SaveTimeRecord  *pTimeRec;         /* the time chunk's wire record */
    GameTime        *pTime;            /* 0x159618, the live clock the record is copied into */
    const StrategSpawnCtx  *pSpawnCtx; /* StrategList_PurgeType5 */
    const SpawnMissionCtx  *pMissionCtx; /* Geoscape_SpawnMissionSite */
    u8              *pbSpawnOnceFlag;  /* PurgeType5's spawn-once gate */
    const LoadGameHooks *pHooks;
} LoadGameEnv;

i32 LoadGame(DatFile *pDat, LoadGameEnv *pEnv);

#endif /* PUFO_SAVE_H */
