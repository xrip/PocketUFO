/* pufo_save.h - save/load chunk serialisation.
 *
 * Evidence: Save_WriteSoldiers @ 0x42350, Save_ReadSoldiers @ 0x416bc,
 *           Save_WriteStrategVer @ 0x3f1c8, Save_ReadGameState @ 0x44958.
 *
 * NOT ported, and deliberately not cited here: the slot drivers SaveGame (0x44988),
 * LoadGame (0x44bbc) and the slot list formatter SaveSlot_FormatList (0x45904). This module
 * recreates the chunk writers and readers they call, not the drivers themselves. The whole
 * file is described below because the format is needed to read a save, but describing a
 * function is not the same as recreating it.
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
#include "game/pufo_mapcell.h"
#include "game/pufo_ship.h"
#include "game/pufo_unit.h"
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

/* Save_WriteSoldiers @ 0x42350.
 *
 * Begins the `soldiers` chunk, writes the u16 count, then every soldier's record,
 * then the two trailing chunks. `DatFile_Write` requires an open chunk, so this is
 * the only correct entry point: calling the per-soldier writer directly without an
 * open chunk fails, exactly as the original would. */
bool Save_WriteSoldiers(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount);

/* Save_ReadSoldiers @ 0x416bc: the matching read side. */
bool Save_ReadSoldiers(DatFile *pDat, Soldier **apapSoldiers, i32 *pnCount,
                       i32 nCapacity);

/* Writes one soldier's non-tactical record prefix (740 bytes) through pDat.
 * The `soldiers` chunk must already be open. */
bool Save_WriteSoldier(DatFile *pDat, const Soldier *pUnit);

/* Reads one soldier's non-tactical record prefix back. Fields the port does not
 * model (the opaque inventory block) are skipped rather than read. */
bool Save_ReadSoldier(DatFile *pDat, Soldier *pUnit);

/* The two trailing per-soldier chunks. */
bool Save_WriteTrailingChunks(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount);
bool Save_ReadTrailingChunks(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount);

/* Save_WriteStrategVer @ 0x3f1c8 / the matching read. */
bool Save_WriteVersion(DatFile *pDat);
bool Save_ReadVersion(DatFile *pDat, u8 *pnOutVer);

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
bool Save_ReadSmap(DatFile *pDat, SaveSmapState *pState);

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
 *    16  BaseRec: bSlot, bUnk4, nStoresTotal, nUnk8, nUnk12, bUnk17, bUnk16
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
    u8   abUnk18655D[9];
    SaveBaseFacility aFac[PUFO_BASE_FAC_COUNT];
} SaveBasesState;

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

#endif /* PUFO_SAVE_H */
