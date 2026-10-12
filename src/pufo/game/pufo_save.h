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
#include "game/pufo_font.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_ship.h"
#include "game/pufo_strateg.h"
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
 *   4 bytes at cell+0x10 (bUnk16..bUnk19 as one block),
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
 *     clears nUnk20, bUnk24 and gap_25. Coordinates are derived, not stored.
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
 *     but nUnk20, bUnk24 and gap_25 are cleared rather than read.
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
 * Save_WriteTGameData @ 0x446a8. One call writes FIVE chunks in this order:
 *
 *     tgamedata (43 bytes), teamunits, aliens, diedsolds, hiddensold,
 *     map, v124.dat (8 bytes)
 *
 * so it is the tactical half of a save, and it calls the three writers above
 * rather than duplicating them.
 *
 * The tgamedata payload is 23 fields in a fixed order. The offsets come from the
 * IDB's Map struct gaps, and two of them are already known fields, which is what
 * pins the arithmetic: gap_86272[20] is 0x15114, the port's nMapLayers, and
 * gap_86301[1] is 0x1517E, the light-enable gate Map_RefreshCellMeta reads.
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
    u8   bUnk1517D;       /* 0x1517D, gap_86301[0] */
    u8   bLightEnabled;   /* 0x1517E, gap_86301[1] */
    i32  anUnk15188[4];   /* 0x15188, 0x1518C, 0x15190, 0x15194, pad86309a[3,7,11,15] */
    u8   abUnk15198[3];   /* 0x15198..0x1519A, pad86309a[19,20,21] */
    i32  nUnk151A0;       /* 0x151A0, gap_86336 */
    i32  anCraftTile[3];  /* nCraftTileA, stride 4, written one byte each */
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

#endif /* PUFO_SAVE_H */
