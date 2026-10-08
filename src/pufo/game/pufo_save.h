/* pufo_save.h - save/load chunk serialisation.
 *
 * Evidence: Save_WriteSoldiers @ 0x42350, Save_ReadSoldiers @ 0x416bc,
 *           SaveGame @ 0x44988, LoadGame @ 0x44bbc, SaveSlot_FormatList @ 0x45904,
 *           Save_WriteStrategVer @ 0x3f1c8, Save_ReadGameState @ 0x44958.
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

#endif /* PUFO_SAVE_H */
