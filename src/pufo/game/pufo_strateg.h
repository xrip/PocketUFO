/* pufo_strateg.h - the geoscape contact list and its per-minute updates.
 *
 * Evidence: StrategList_CountActive @ 0x81a2c, StrategList_FindByShipId @
 * 0x5a9b4, StrategList_UpdateMotion @ 0x80fd8, StrategList_UpdateDetect @
 * 0x81948, Strateg_CalcDetect @ 0x81378, Ship_ClearExpiredIntercepts @
 * 0x8128c. The list is the global `strategList` @ 0x186534 (a PtrArray of
 * StrategItem*, IDB type StrategItem, 136 bytes); the id counters are
 * nNextStrategDisplayId @ 0x186544 and nNextStrategSiteId @ 0x186548.
 *
 * Per-base side tables the detect code reads: byte_161F38 @ 0x161f38 is the
 * site state written by Geoscape_TickBases (1, 2, 11, 12 seen), and
 * unk_162008 @ 0x162008 is the per-base radar percentage.
 */
#ifndef PUFO_STRATEG_H
#define PUFO_STRATEG_H

#include "pufo_common.h"
#include "game/pufo_clock.h"   /* GameTime, for the pause byte */
#include "game/pufo_ship.h"
#include "render/pufo_render.h"   /* PadImage, for the tile grids */
#include "util/pufo_ptrarray.h"   /* PtrArray, the strateg list itself */

typedef struct StrategItem {
    u8   nType;          /* +0x00 0 base, 1 craft */
    u8   bActive;        /* +0x01 */
    u8   bCraftClass;    /* +0x02 */
    u8   bUnk3;          /* +0x03 */
    i32  nX;             /* +0x04 map x, wrapped at +/-800 */
    i32  nY;             /* +0x08 */
    i32  nSiteId;        /* +0x0c 50000 = not yet assigned */
    i32  nDisplayId;     /* +0x10 50000 = not yet assigned; equals Ship.bId for player craft */
    u8   bRandA;         /* +0x14 */
    u8   gap_15[3];
    i32  nStartX;        /* +0x18 */
    i32  nStartY;        /* +0x1c */
    i32  nDestX;         /* +0x20 */
    i32  nDestY;         /* +0x24 */
    i8   bTravelDir;     /* +0x28 */
    u8   gap_29[3];
    i32  nTimer;         /* +0x2c proximity-detect accumulation */
    u8   bFlagA;         /* +0x30 */
    u8   gap_31[3];
    i32  nSeed;          /* +0x34, seeded by StrategItem_ctor @ 0x82bd4 */
    u8   bVisible;       /* +0x38 */
    u8   nMissionSub;    /* +0x39 */
    u8   gap_3A[6];
    double dTrackA;      /* +0x40 */
    double dSpeed;       /* +0x48 */
    double dFracX;       /* +0x50 */
    double dFracY;       /* +0x58 */
    i32  nTravelStartSec;/* +0x60 */
    u8   bHyperDetect;   /* +0x64 */
    u8   bDetected;      /* +0x65 */
    u16  uTravelMin;     /* +0x66 */
    i32  nTravelEpochSec;/* +0x68 */
    i32  nHp;            /* +0x6c */
    u8   bRace;          /* +0x70 */
    u8   gap_71[3];
    i32  nPhase;         /* +0x74 */
    u8   bDone;          /* +0x78 */
    u8   gap_79[3];
    i32  nTargetX;       /* +0x7c */
    i32  nTargetY;       /* +0x80 */
    i32  nTargetIdx;     /* +0x84 */
} StrategItem;

_Static_assert(sizeof(StrategItem) == 136, "StrategItem must be 136 bytes");

#define STRATEG_ID_UNASSIGNED 50000

typedef struct StrategList {
    StrategItem **apItems;
    i32 nCount;
    i32 nNextDisplayId;  /* nNextStrategDisplayId @ 0x186544 */
    i32 nNextSiteId;     /* nNextStrategSiteId @ 0x186548 */
} StrategList;

/* The pieces of the geoscape world the contact updates reach into. The ship
 * array is pCtxBase.apCraft (count byte at pCtxBase+0x1194). */
/* The mission-site table at 0x1865C8: 24-byte records, indexed by craft class in
 * BasePlaneScreen_AddInterceptMission @ 0x7de14 and by a random roll of 56 in
 * Geoscape_SpawnMissionSite @ 0x7e49c. x and y are SIGNED 16-bit; both loads are LDRSH. The
 * remaining 20 bytes of each record are not touched by either caller. */
typedef struct MissionSiteRecord {
    i16 x;      /* +0x00 */
    i16 y;      /* +0x02 */
    i16 agap[10];
} MissionSiteRecord;   /* 24 bytes, the stride the original multiplies by 0x18 */

typedef struct GeoscapeWorld {
    Ship *const *apCraft;
    u8    nShipCount;
    u8   *apSiteState;   /* byte_161F38, one byte per base */
    u8   *apRadarPct;    /* unk_162008, one byte per base */
    i32   nTickSeconds;  /* dword_187070: seconds per game tick (5/60/300/...) */
    i32   nRadarBoostOff;/* apLines[117]: zero adds +7 to every radar pct */
    i8    nMinuteTicks;  /* apLines[118], signed byte counter */
    GameTime *pTime;     /* the clock: bPaused is apLines[106], the byte
                          * GameTime_Pause @ 0x5d6ac writes. Contact messages
                          * pause the game, so the pause byte has to be shared
                          * state, not a per-module flag. */
} GeoscapeWorld;

/* Hooks the contact updates need from the UI / clock layer. */
typedef struct StrategHooks {
    /* UiPanel_AppendLineWithSound @ 0x5a394 only. nMsg is the original message
     * id (10 base contact, 13 intercept lost). The GameTime_Pause that follows
     * a contact in the original is written directly through pWorld->pTime, not
     * through this hook: 10, 13 and 15 pause, 43 (low supply) does not. */
    void (*pfnContactMsg)(i32 nMsg, i32 nArg3, i32 nArg4, void *pUser);
    /* Base_FindIndexBySiteId @ 0x5a8d0 is a second copy of Base_FindIndexByTypeByte @ 0x6ac30;
     * the port keeps one implementation. The hook stays for the unported callers. */
    i32 (*pfnFindBaseBySiteId)(i32 nSiteId, void *pUser);
    void *pUser;
} StrategHooks;

/* StrategList_CountActive @ 0x81a2c: count of nType==1 && bActive==1. */
i32 StrategList_CountActive(const StrategList *pList);

/* StrategList_FindByShipId @ 0x5a9b4: index of the craft entry whose
 * nDisplayId equals apCraft[nShipIdx]->bId, or -1. */
i32 StrategList_FindByShipId(const StrategList *pList,
                             const GeoscapeWorld *pWorld, i32 nShipIdx);

/* StrategList_UpdateMotion @ 0x80fd8. Despite the name it is the proximity
 * detect pass; see the .c file for the exact rules, including the odd
 * sqrt(dx^2 + dy^2 + threshold) distance. */
void StrategList_UpdateMotion(StrategList *pList, GeoscapeWorld *pWorld);

/* StrategList_UpdateDetect @ 0x81948: radar pass over active craft. */
void StrategList_UpdateDetect(StrategList *pList, GeoscapeWorld *pWorld,
                              const StrategHooks *pHooks);

/* Strateg_CalcDetect @ 0x81378. Returns true when the ufo is detected by at
 * least one base; sets bHyperDetect for the 11/12 site states. */
bool Strateg_CalcDetect(StrategItem *pUfo, i32 nIndex, i32 nForcePct,
                        const StrategList *pList, const GeoscapeWorld *pWorld,
                        const StrategHooks *pHooks);

/* Ship_ClearExpiredIntercepts @ 0x8128c: drop intercepts whose target went
 * invisible (but not the ones still waiting for a display id). */
void Ship_ClearExpiredIntercepts(GeoscapeWorld *pWorld, StrategList *pList,
                                 const StrategHooks *pHooks);

/* ------------------------------------------------------------ the strateg list
 *
 * The original's list global `strategList` @ 0x186534 is a plain PtrArray; the two
 * id counters sit beside it at 0x186544 / 0x186548. These two wrappers act on the
 * PtrArray itself, which is what the original calls. */

/* The port carries the strateg list as a PtrArray (the shape StrategList_RemoveAt wants) and
 * searches it as a StrategList (the shape StrategList_FindByShipId / FindIdxForShip want). The
 * two structs have their fields in the opposite order, so they cannot alias one another. This
 * builds the search view FRESH from the array each time, which is the only way to keep it from
 * going stale across a removal: StrategList_RemoveAt compacts the array, and a view built
 * before it would still report the old count. */
static inline StrategList StrategViewOf(const PtrArray *pArr)
{
    StrategList v;

    v.apItems = (StrategItem **)pArr->apItems;
    v.nCount = pArr->nCount;
    v.nNextDisplayId = 0;
    v.nNextSiteId = 0;
    return v;
}

/* StrategList_ctor @ 0x83890: PtrArray_ctor over strategList. */
void StrategList_ctor(PtrArray *pList);

/* StrategList_Clear @ 0x838ac: PtrArray_DeleteAll, then PtrArray_Clear. The items
 * are freed as well as the array. */
void StrategList_Clear(PtrArray *pList);

/* StrategItem_ctor @ 0x82bd4. It does one thing: writes the dword at +0x34 from the
 * global dword at 0x18640C. That global has exactly three xrefs in the IDB: read at
 * 0x82A7C and incremented at 0x82A84, both inside StrategList_AddItem @ 0x826e8, and
 * read here at 0x82BD8. So it is a per-item seed counter, written once per added item,
 * and it starts at zero because .bss is zeroed. The port passes it as a pointer so the
 * counter is visible instead of hidden in a global. */
StrategItem *StrategItem_ctor(StrategItem *pItem, i32 nSeed);

/* StrategItem_AllocAdd @ 0x415cc: operator new(0x88), StrategItem_ctor, then
 * PtrArray_Add. The add happens whether or not the allocation succeeded, so a failed
 * allocation appends a null entry -- reproduced as written. */
bool StrategItem_AllocAdd(PtrArray *pList, i32 nSeed);

/* ------------------------------------------------- adding a strateg item
 *
 * Evidence: StrategList_AddItem @ 0x826e8.
 *
 * Allocates through StrategItem_AllocAdd, then writes the LAST entry of the list field
 * by field. The original re-reads PtrArray_GetCount and PtrArray_Get(count-1) before
 * every single store, which is why the decompiler is full of v29..v48; nothing changes
 * the count in between, so the port takes the pointer once.
 *
 * pnSeed is the original's global at 0x18640C. It is read for the constructor and
 * incremented at 0x82A84 AFTER the item was built, so item N gets seed N counting from
 * whatever the counter held at the start.
 *
 * The nType == 5 coordinate conversion is the interesting part. A type-5 item's x and y
 * are multiplied by the world scale of the CURRENT zoom level before being stored, so a
 * type-5 item created while zoomed in lands at the right world position:
 *
 *   nZoomMode 1 -> 1.333   (the same constant Smap_ZoomOut @ 0x81c08 uses for 1 -> 0)
 *   nZoomMode 2 -> 1.694
 *   nZoomMode 0 -> no scaling
 *
 * Both factors were recovered from the `muld` immediates: 0x3FF5555555555556 and
 * 0x3FFB0A3D70A3D70A. `dtoi` truncates toward zero, so 300 at mode 1 is 399, not 400.
 *
 * The id fields split by type, and the two are mutually exclusive:
 *   nType 0 or 3 -> nSiteId = nId,     nDisplayId = -1
 *   nType 1      -> nDisplayId = nId, nSiteId = -1
 *   any other    -> NEITHER is written. operator new leaves them indeterminate, so a
 *                   type 2, 4 or 5 item starts life with a garbage site id and display
 *                   id. Nothing here fixes that; a later pass has to assign them.
 *
 * Deviations, both listed rather than hidden:
 *   - the original writes through PtrArray_Get(count-1) with no NULL check, so a failed
 *     allocation faults on the first field store. The port returns NULL instead.
 *   - the original returns &MEMORY[0x1863E8], the viewport, not the item it created. The
 *     port returns the item, which is the only value a caller can use.
 *   - the original mirrors the list count into the separate global 0x186530 at 0x82710.
 *     The port keeps one count, in the PtrArray. */
StrategItem *StrategList_AddItem(PtrArray *pList, i32 *pnSeed, i32 nZoomMode,
                                 i32 nType, i32 bActive, i32 bCraftClass,
                                 i32 x, i32 y, i32 nId, i32 bVisible);

/* StrategList_AddUfoAt @ 0x7e374: a tail call to StrategList_AddItem with the
 * alien-craft arguments filled in -- nType 1, bActive 1, nId 50000, bVisible 0.
 * The zoom mode is read live from the viewport's nMode, which is why it is a
 * parameter here. nId 50000 is STRATEG_ID_UNASSIGNED, and because nType is 1 it
 * lands in nDisplayId, so a freshly spawned craft is addressable but has no
 * display id yet. nSiteId becomes -1. */
StrategItem *StrategList_AddUfoAt(PtrArray *pList, i32 *pnSeed, i32 nZoomMode,
                                  i32 x, i32 y, i32 bCraftClass);

/* ------------------------------------------------- the spawn roll tables
 *
 * Evidence: byte_AAD08, byte_AACD8 and unk_AAC98, read straight out of .data.
 * They tile 0xAAC98..0xAAD26 with no gaps, which is what confirms the row widths.
 *
 * Row index is the difficulty byte at apLines[117], clamped to 5. The same slot is
 * the radar boost offset elsewhere in the port, so one byte drives two systems.
 */
typedef struct StrategRollTables {
    /* byte_AAC98, 8 rows of 8, indexed by bCraftClass: mission subtype weights. */
    u8 aMissionWeight[8][8];
    /* byte_AACD8, 6 rows of 8, indexed by difficulty: craft class weights. */
    u8 aCraftWeight[6][8];
    /* byte_AAD08, 6 rows of 5, indexed by difficulty: race weights. */
    u8 aRaceWeight[6][5];
} StrategRollTables;

/* StrategList_RollCraftStats @ 0x81a9c: pick race, craft class and mission
 * subtype for the item at nIndex, then hand it to Geoscape_SpawnMissionSite.
 *
 * Each pick is the same weighted walk: roll Game_RandU16(100) + 1, so 1..100,
 * then add weights left to right until the running total reaches the roll.
 * The walk stops after 7 columns, so a roll above the row total leaves the
 * index at 7 -- and the eighth column of every weight row is therefore never
 * added. It is dead data, and 7 is a reachable result:
 *
 *   craft weights, difficulty 5: 0+10+15+15+15+8+22 = 85, so rolls 86..100 give class 7
 *   mission weights, class 7:    0+0+60+0+0+15+0    = 75, so rolls 76..100 give subtype 7
 *
 * The race walk cannot overflow: every difficulty row totals exactly 100.
 *
 * The craft class is then capped at 2 while the gate value is below 15, which is
 * the original's early-game restriction -- no heavy craft before that point.
 *
 * Deviation: the original clamps the difficulty only at the top (`> 5`) and then
 * uses it as a row index, so a negative byte reads before the tables. The port
 * clamps to 0..5. */
typedef int (*StrategSpawnMissionFn)(StrategItem *pUfo, i32 nMissionSub, void *pUser);

int StrategList_RollCraftStats(PtrArray *pList, i32 nIndex, i32 nDifficulty,
                               i32 nEarlyGate, const StrategRollTables *pTables,
                               StrategSpawnMissionFn pfnSpawn, void *pUser);

/* StrategList_AddAlienSiteIfValid @ 0x7e3b4: the land-tile gate in front of the
 * item builder. If the tile at x,y is 255 -- ocean -- nothing is added and the
 * result is false. Otherwise it is an AddItem with nType 0, bActive 1, nId 50000
 * and bVisible set to the caller's discovered flag.
 *
 * bCraftClass is passed as -1. AddItem stores it through a byte, so the item ends
 * up with bCraftClass 255. That is not a craft class at all; it is what makes a
 * site item distinguishable from a craft by that field, and it is also why a site
 * item never matches any row of the craft weight or speed tables.
 *
 * Because nType is 0, 50000 lands in nSiteId and nDisplayId becomes -1 -- the
 * mirror image of a spawned craft. Its only callers are the subtype-4 branch of
 * Geoscape_SpawnMissionSite. */
bool StrategList_AddAlienSiteIfValid(PtrArray *pList, i32 *pnSeed, i32 nZoomMode,
                                     const PadImage *pMap, i32 x, i32 y,
                                     i32 bDiscovered);

/* ------------------------------------------------- the flight path initialiser
 *
 * Evidence: StrategEntry_InitTravel @ 0x7f60c.
 *
 * Sets start and destination, resets the fractional position, picks a speed, and
 * works out how long the leg takes. Every branch of the mission machine ends by
 * calling it, so this is where a strateg item's motion actually begins.
 *
 * Speed for an ACTIVE craft is a per-class base minus a random jitter:
 *
 *   class  base      jitter roll
 *   0      36.6666   none
 *   1      40        rand(3)
 *   2      45        rand(3)
 *   3      66.6666   rand(5)
 *   4      71        rand(5)
 *   5      80        rand(5)
 *   6      53.6666   rand(4)
 *   7      83.3333   rand(5)
 *
 * The bases 36.6666, 66.6666, 53.6666 and 83.3333 were recovered from the subd
 * immediates; 40, 45, 71 and 80 are written as integers. The jitter starts as
 * Game_RandU16(0x12), so 0..17, and is then multiplied by the class roll before
 * being subtracted -- so a class 3 craft can be up to 68 slower than its base.
 * A class outside 0..7 leaves dSpeed untouched, and any speed below 10.0 is
 * raised to exactly 10.0.
 *
 * For an INACTIVE craft the speed is the matching ship's uFuelCap times 1/60.
 * Hazard: the original's ship search runs to nShipCount when nothing matches and
 * then indexes apCraft at that count, one past the end. The port leaves the speed
 * untouched in that case.
 *
 * Two overrides follow, and both win over whatever was chosen above:
 *   - an inactive craft with nFlag above 0 gets speed 0
 *   - ANY item with nTimer above 0 gets speed 0
 *
 * dTrackA is always dSpeed times 0.02.
 *
 * The leg length is the shorter of the direct x distance and the wrap-around one,
 * which is the map's 800-wide cylinder:
 *   a = |destX - startX + 800|,  b = |startX - destX + 800|
 * and the plain |startX - destX| is taken if it is smaller than both. bTravelDir
 * records which way that shorter leg runs.
 *
 * Note the asymmetry: x is wrap-aware and y is not. uTravelMin is
 * sqrt(dy*dy + dx*dx) / dTrackA, with dy the plain difference. If dSpeed is 0 then
 * dTrackA is 0 and that division has no defined result; the original has no guard
 * and the port sets uTravelMin to 0 instead.
 *
 * nTravelEpochSec is the clock at apLines[105], which is also the return value. */
i32 StrategEntry_InitTravel(StrategItem *pEntry, i32 nX0, i32 nY0, i32 nX1, i32 nY1,
                            i32 nFlag, i32 bA, i32 bB, i32 bC, i32 bCopyStart,
                            const GeoscapeWorld *pWorld, i32 nGameTotalSec);



/* StrategList_MaybeSpawnCraft_Process @ 0x7e420: optionally put one alien craft
 * at a random land position and roll its stats, then always run the geoscape
 * event pass.
 *
 * The gate is `bSpawn == 1` exactly, not merely non-zero, so any other value
 * skips the spawn and still runs the event pass.
 *
 * The position is drawn as Game_RandU16(0x2F8) + 20 for x and Game_RandU16(0x168)
 * + 20 for y, so 20..780 by 20..380 -- the playable map with a 20-tile margin, and
 * note that x is drawn first. The craft class is 0 here; RollCraftStats then
 * replaces it with the class its own weight table picks, capped at 2 while the
 * early gate is below 15.
 *
 * RollCraftStats is called with the index the new item landed at, count - 1, read
 * back from the list rather than remembered. Because RollCraftStats ends by
 * tail-calling Geoscape_SpawnMissionSite, that hand-off has to be passed through
 * here as well.
 *
 * StrategList_ProcessEvents is a hook: it is the geoscape's per-tick event pass and
 * it is not ported yet. */
typedef int (*StrategProcessEventsFn)(void *pUser);

i32 StrategList_MaybeSpawnCraft_Process(PtrArray *pList, i32 *pnSeed, i32 nZoomMode,
                                        const StrategRollTables *pTables, i32 nDifficulty,
                                        i32 nEarlyGate, i32 bSpawn,
                                        StrategSpawnMissionFn pfnSpawnMission,
                                        StrategProcessEventsFn pfnProcessEvents,
                                        void *pUser);

/* The arguments StrategList_RemoveAt has to hand to StrategList_MaybeSpawnCraft_Process
 * when a removal triggers a replacement spawn. Bundled because there are eight of them
 * and none of them belong to the removal itself. */
typedef struct StrategSpawnCtx {
    const StrategRollTables *pTables;
    i32 nDifficulty;
    i32 nEarlyGate;
    i32 nZoomMode;
    i32 *pnSeed;
    StrategSpawnMissionFn pfnSpawnMission;
    StrategProcessEventsFn pfnProcessEvents;
    void *pUser;
} StrategSpawnCtx;

/* ------------------------------------------------- removing a strateg item
 *
 * Evidence: StrategList_RemoveAt @ 0x82ab0.
 *
 * Two jobs, and the first one is the reason removals are not just a container call.
 *
 * 1. Keep intercept references valid. Every ship that is bIntercepting and whose
 *    nStrategIdx is GREATER than nIndex has it decremented by one. Ships at or below
 *    nIndex are left alone. Without this, removing a contact makes every later
 *    intercept point at the wrong item.
 *
 * 2. A removal can spawn a replacement. If the one-shot byte at 0x162028 is set, the
 *    original computes 3 minus the number of active craft, and for each missing one
 *    rolls Game_RandU16(3), calling StrategList_MaybeSpawnCraft_Process with bSpawn 1
 *    only on a roll of exactly 1. It then clears the byte and returns 0. If the byte
 *    was already 0 it is returned unchanged. So the return value is that byte either
 *    way -- 0 when a replacement pass ran.
 *
 * Note that each MaybeSpawnCraft_Process call runs the geoscape event pass too, so a
 * removal that spawns two replacements runs the event pass twice. */
i32 StrategList_RemoveAt(PtrArray *pList, const GeoscapeWorld *pWorld, i32 nIndex,
                         u8 *pnSpawnOnceFlag, const StrategSpawnCtx *pCtx);

/* StrategList_PurgeType5 @ 0x6ab58: drop every type-5 item no ship is claiming.
 *
 * For an item at index i with nType 5 it looks for any ship that is bIntercepting
 * with nStrategIdx equal to i. If none claims it, the item is removed.
 *
 * THIS FUNCTION HAS A BUG IN THE ORIGINAL, AND THE PORT REPRODUCES IT.
 * The keep-flag is set to 1 only before the loop, at 0x6AB60, and inside the ship
 * scan. It is NOT reset per iteration. So once one unclaimed type-5 item is removed
 * the flag stays 0, and the item now sitting at that same index is removed too,
 * whatever its type -- and this cascades until only the last item of the list is
 * left. With five items and an unclaimed type-5 item first, the result is one item.
 *
 * That matters because LoadGame (0x44bbc) calls this whenever the save version byte
 * 0x18655C is below 3, so loading an old save that holds an unclaimed type-5 item
 * wipes the geoscape down to a single contact. It is reproduced rather than fixed so
 * the port behaves like the original; the bug is recorded in the IDB comment too. */
i32 StrategList_PurgeType5(PtrArray *pList, const GeoscapeWorld *pWorld,
                           u8 *pnSpawnOnceFlag, const StrategSpawnCtx *pCtx);

/* ------------------------------------------------------- the geoscape viewport
 *
 * One 36-byte block at 0x1863E8. IDA carries two names for it because different
 * functions use it for different jobs: `SmapCamera` (36 bytes: nMode, nScrollX,
 * nScrollY, nMapW, nMapH, nUnk14, nUnk18, nPickX, nPickY) for the zoom and pick
 * state, and `SmapView` (28 bytes: nUnk0, nPosX, nPosY, nStride, nUnk16, nLastDx,
 * nLastDy) for the drag. The two views agree offset by offset:
 *
 *     0x00 nMode   == nUnk0
 *     0x04 nScrollX == nPosX
 *     0x08 nScrollY == nPosY
 *     0x0C nMapW   == nStride
 *     0x10 nMapH   == nUnk16
 *     0x14 nUnk14  == nLastDx
 *     0x18 nUnk18  == nLastDy
 *
 * So the scroll position IS the view position, and the tile grid stride IS the map
 * width: Smap_GetTileAtScreen @ 0x82244 wraps x with nMapW and then indexes the tile
 * bytes with nStride, which is the same field. One struct, no aliasing. */
typedef struct SmapView {
    i32 nMode;    /* 0x00 zoom level: 0 widest, 1 middle, 2 closest */
    i32 nScrollX; /* 0x04 */
    i32 nScrollY; /* 0x08 */
    i32 nMapW;    /* 0x0C also the stride of the tile byte grid */
    i32 nMapH;    /* 0x10 */
    i32 nLastDx;  /* 0x14 the previous drag delta */
    i32 nLastDy;  /* 0x18 */
    i32 nPickX;   /* 0x1C last tile Smap_GetTileAtScreen resolved to */
    i32 nPickY;   /* 0x20 */
} SmapView;

_Static_assert(sizeof(SmapView) == 36, "the geoscape viewport is 36 bytes");

/* Smap_ScrollBy @ 0x820d0. Drag the view by (nDx, nDy).
 *
 * The move is `pos = pos - delta + last_delta`, so the previous delta is added back:
 * the view tracks absolute pointer positions, not per-event deltas.
 *
 * The vertical clamp only runs when nScreenMode (the dword at 0x186BC4, the screen id
 * the geoscape pointer handlers switch on) is not 2. With halfH = nMapH/2 - 160:
 *
 *   - if nScrollY + halfH < 0  -> nScrollY = -halfH
 *   - if nScrollY + halfH > nStripHeight + nMapH - 320 -> nScrollY =
 *       nStripHeight - halfH + nMapH - 320
 *
 * nStripHeight is the uHeight of the PadImage-shaped object the original reads at
 * 0x186BBC. The lower clamp updates the value the upper clamp then tests, so the two
 * are not independent. The deltas are stored last, after any clamping. */
void Smap_ScrollBy(SmapView *pView, i32 nDx, i32 nDy, i32 nScreenMode,
                   i32 nStripHeight);

/* Smap_GetTileAtScreen @ 0x82244. Screen pixel to tile coordinate, wrapping once.
 *
 *   tileX = nScrollX + nMapW/2 + x - 120, plus nMapW once if still negative
 *   tileY = nScrollY + nMapH/2 + y - 160, plus nMapH once if still negative
 *
 * One wrap only: a value below -nMapW stays negative and the port reads out of the
 * grid exactly as the original does, so the port checks the range and returns 0
 * instead (deviation 1). The tile grid is nMapH rows of nMapW bytes. The resolved
 * coordinates are written to nPickX/nPickY even when the lookup is refused. */
u8 Smap_GetTileAtScreen(SmapView *pView, const u8 *pTiles, i32 nX, i32 nY);

/* Tilemap_GetAt @ 0x8217c and Tilemap_GetAtAlt @ 0x821a0. Byte lookup in a
 * PadImage-shaped tile grid: pData + uWidth * y + x. The two differ only in which
 * global grid they use (0x186B74 and 0x186C18), so the port takes the grid and keeps
 * one implementation. The original's first argument is unused. */
u8 Tilemap_GetAt(const PadImage *pMap, i32 nX, i32 nY);

/* Smap_GetTileAtMode @ 0x821c4. One byte of the OVERLAY layer of the tile set the current zoom
 * level selects -- not the terrain layer Smap_GetTileAtScreen reads. The three globals it picks
 * from (0x186C28, 0x186C1C, 0x186BB8) are the third PadImage of each zoom set, so the port takes
 * the three overlays as an array.
 *
 * The x coordinate is offset by a global origin (0x186C20) before the lookup, and the stride is
 * the PadImage's own uWidth rather than the viewport's nMapW.
 *
 * Deviation: for a mode outside 0..2 the original returns an uninitialised stack byte. The port
 * returns 0. */
u8 Smap_GetOverlayAtMode(const SmapView *pView, const PadImage *const aOverlay[3],
                         i32 nOriginX, i32 nX, i32 nY);

/* Ship_FindIndexByStrategType @ 0x5a92c. The mirror of StrategList_FindIdxForShip: this one
 * takes a strateg index and returns the index of the craft whose bId equals that item's
 * nDisplayId. Like its mirror it gates on nothing else. */
i32 Ship_FindIndexByStrategType(const GeoscapeWorld *pWorld, const StrategList *pList,
                                i32 nStrategIdx);

/* Smap_ClearSelection @ 0x84648. Drop the first line of the geoscape message list and unpause the
 * game only if that leaves the list empty - a contact message pauses the game, and clearing the
 * selection may resume it only when no message is still on screen. */
struct UiLineList;
u8 Smap_ClearSelection(struct UiLineList *pLines, GameTime *pTime, u8 *pbSelectionFlag);

/* Smap_DismissSelected @ 0x84428. The dismiss half of the map selection: retarget the selected
 * craft home first (its index is colour slot 0 of line 0), then drop line 0, unpause only when the
 * removal empties the list, and clear the selection flag. The original hands Ship_RetargetTravel
 * the unused root panel pointer; the port passes the removal env it actually needs. */
u8 Smap_DismissSelected(const ShipRemovalEnv *pShipEnv, struct UiLineList *pLines,
                        GameTime *pTime, u8 *pbSelectionFlag);

/* ------------------------------------------------- craft select / mission launch
 *
 * Evidence: Smap_BeginCraftSelect @ 0x838dc (2,424 B) and its button wrapper
 * Smap_BeginCraftSelectBtn @ 0x84634 (sets bCraftSelectAssaultFlag and tail-calls it).
 *
 * The function is the geoscape's decision tree for "the selected line means launch".
 *
 * 1. Visibility pass (0x838FC): every craft that is at base OR has no hot weapons gets
 *    bWeaponsHot = 0, bAtBase = 1, and its strateg entry's bVisible cleared.
 *
 * 2. When Map_bMissionMode != 1 (0x83994): unk_128AD6 = 1; the selected craft index is line 0
 *    colour slot 0 (stored to 0x187054, 0x159E74 and Map_nCraftIndex) and the selected strateg
 *    index is colour slot 1 (stored to 0x186BE4). If the assault flag is set the whole selection
 *    is skipped and the tail runs. Otherwise: the overlay tile under the selected item below 0x32
 *    clears unk_128AD6 (0x83B54), and the selected craft's own strateg entry's bRace picks the
 *    mission sprite set (five races, anything else leaves the set alone).
 *
 * 3. The line flag branch (LABEL_31, 0x83C60): flag 21 -> urban mission (type 5, mode 3, the
 *    target item is removed from the strateg list); flag 22 -> alien base (type 4, mode 2);
 *    flag 23 -> crash/intercept landing: the alt tilemap tile under the item picks the mission
 *    type (240 desert 6, 241 jungle 0, 242 polar 8, 243 forest 2, 244 culta 1 - the ARM
 *    immediates, cross-checked against g_aTerrainModuleCount), the item's bCraftClass picks the
 *    tier budget (50/200/250/500/500/1200/2200/3000 - the last two are 0x890|8 and 0xBB0|8) and
 *    the terrain variant, and the damage band is min(70, 100*(budget - nHp)/budget) divided by 3
 *    (the utod/muld/dtoi triple at 0x83EA0 is the magic multiply by 0x55555555 >> 32), or 0 when
 *    nHp is -1.
 *
 * 4. Mode gate (LABEL_67, 0x84004): mode != 1 goes to the tail. Mode == 1 is the Xcom base
 *    assault: sprite set by nAssaultBaseRace, type 3, Smap_InitCraftSelectState for
 *    nCraftHomeBase, nSelectedBaseIdx = nCraftHomeBase. Soldiers assigned at the base -> tail
 *    (launch). None -> message 17, radar pulse 250, funding 0x1865BC loses 800 + 400 per craft
 *    homed at the base + 30 * Base_CalcOccupiedSpace, 0x15961C = 1, unk_187084 = 0, and
 *    Base_DemolishFacility(apBases[nCraftHomeBase]->bUnk4). If the base count then hits zero:
 *    GeoscapeUi_FreeAll(1), EndScreen_Init(0), Map_bTickHold = Map_bBattleEnded = 1, return 1;
 *    otherwise return the base count.
 *
 * 5. Tail (LABEL_10, 0x83A0C): if the assault flag is set, mission type 9 with sprite set
 *    13/21/23/23, mode 0, spawnAlt 1, and nMapLayers from the selected craft's bCraftType. Then
 *    Smap_DismissSelected runs unless mode is still 1 (the Xcom launch keeps mode 1 and skips
 *    it), bMapSelectionFlag = 0, GeoscapeUi_FreeAll(0), and the return is Tact_InitMission(0).
 *
 * Deviations: the original dereferences apCraft / strateg entries / apBases with no bounds or
 * null tests (a negative selected index faults); the port refuses those steps instead. The
 * default craft-class arm leaves the tier budget at 0, which makes the band division a divide by
 * zero in the original; the port skips the band. */
typedef struct SmapCraftSelectEnv {
    /* Map block */
    Ship *const *apCraft;        /* Map.apCraft @ Map+0x10AC */
    u8   *pnCraftCount;          /* Map_nCraftCount @ Map+0x1194 */
    u8   *pbMissionMode;         /* Map_bMissionMode @ Map+0x1511D */
    u8   *pbUnk1511E;            /* unk_128AD6 @ Map+0x1511E */
    u8   *pnMissionType;         /* Map_nMissionType @ Map+0x1511A */
    u8   *abSpriteSet;           /* Map_abSpriteSet @ Map+0x15116..19 */
    u8   *pbSpawnAlt;            /* Map_bSpawnAlt @ Map+0x15148 */
    u8   *pbUnk15115;            /* Map_bUnk15115 @ Map+0x15115, the stealth-budget scale */
    u8   *pnTerrainVariant;      /* Map_nTerrainVariant @ Map+0x15111 */
    u8   *pnMapLayers;           /* Map_nMapLayers @ Map+0x15114 */
    i32  *pnTierRaw;             /* Map_nMissionTierRaw @ Map+0x1510C */
    u8   *pnCraftIndex;          /* Map_nCraftIndex @ Map+0x15113 */
    u8   *pbTickHold;            /* Map_bTickHold @ Map+0x5A */
    u8   *pbBattleEnded;         /* unk_113A14 @ Map+0x5C */
    BaseRec *const *apBases;     /* Map.apBases @ Map+0x1174 */
    i8   *pbHomeBase;            /* nCraftHomeBase @ 0x158848, loaded with LDRSB */
    i8   *pbBaseRace;            /* nAssaultBaseRace @ 0x158849, loaded with LDRSB */

    /* Geoscape state */
    PtrArray   *pStrategList;    /* 0x186534 */
    struct UiLineList *pLines;   /* the root line list @ 0x158890 */
    GameTime   *pTime;           /* 0x159618 */
    i32 *pnSelectedShipIdx;      /* 0x187054 */
    i32 *pnStrategIndex;         /* 0x159E74 */
    i32 *pnSelectedStrategIdx;   /* 0x186BE4 */
    u8  *pbAssaultFlag;          /* bCraftSelectAssaultFlag @ 0x1870A8 */
    u8  *pbSelectionFlag;        /* bMapSelectionFlag @ 0x159E84 */
    u8  *pbFlag187084;           /* unk_187084: write-only in the whole image */
    u8  *pbGeoscapeFlag;         /* 0x15961C */
    i32 *pnRadarPulseTimer;      /* nRadarPulseTimer @ 0xAC224 */
    i32 *pnFunding;              /* 0x1865BC */
    u8  *pbBaseCount;            /* 0x159D80, read with LDRSB */
    u8  *pbSelectedBaseIdx;      /* nSelectedBaseIdx @ 0x159D81 */
    u8  *abFacilityGrid;         /* gBaseFacGrid @ 0x159768 */
    u8  *abTileTypes;            /* abTactTileTypes @ 0x159D5C */
    Soldier *const *apSoldiers;
    i32 nSoldierCount;           /* the u16 at Map+0x1998 */
    ObjectRecFull *const *apObjects;
    i32 nObjectCount;

    /* Map view for the overlay test */
    const SmapView *pView;
    const PadImage *apOverlay[3]; /* 0x186C28 / 0x186C1C / 0x186BB8 */
    i32 nOverlayOriginX;                /* 0x186C20 */
    const PadImage *pTilemapAlt;        /* 0x186C18 */

    /* Plumbing the callees need */
    const ShipRemovalEnv *pShipEnv;     /* DismissSelected and RemoveAt */
    u8 *pbSpawnOnceFlag;                /* 0x162028 */
    const StrategSpawnCtx *pSpawnCtx;
    const struct BaseDemolishEnv *pDemolishEnv;

    /* Hooks into the layers this function hands over to */
    void (*pfnFreeGeoscapeUi)(i32 bFreeWorld, void *pUser); /* GeoscapeUi_FreeAll @ 0x86d20 */
    i32  (*pfnInitMission)(i32 nMode, void *pUser);         /* Tact_InitMission @ 0x3c550 */
    void (*pfnInitEndScreen)(u8 bGoodEnd, void *pUser);     /* EndScreen_Init @ 0x388b0 */
    void *pHookUser;
} SmapCraftSelectEnv;

/* Smap_BeginCraftSelect @ 0x838dc. Returns Tact_InitMission's value on the launch paths, the
 * base count on the Xcom-assault-without-soldiers path, and 1 on the bad-end path. */
i32 Smap_BeginCraftSelect(const SmapCraftSelectEnv *pEnv);

/* Smap_BeginCraftSelectBtn @ 0x84634. Set the assault flag and run it. */
i32 Smap_BeginCraftSelectBtn(const SmapCraftSelectEnv *pEnv);

/* ------------------------------------------------------------- zoom levels
 *
 * Each zoom step swaps three layer globals and takes the map size from the second
 * one:
 *
 *     level  tiles      size source   third layer   the u16 pair at 0x186BC0
 *     0      0x186B74   0x186B78      0x186C28       800 x 400
 *     1      0x187064   0x1864F0      0x186C1C       600 x 300
 *     2      0x18705C   0x1864EC      0x186BB8       472 x 236
 *
 * The port keeps the table as data the caller owns. After a zoom the app reads
 * aSets[pView->nMode] to get the layers it has to draw, instead of the original
 * writing them into globals (deviation 2). */
typedef struct SmapZoomSet {
    PadImage *pTiles;   /* 0x186B04: the byte grid Smap_GetTileAtScreen reads */
    PadImage *pGrid;    /* 0x186E40: its uWidth/uHeight become nMapW/nMapH */
    PadImage *pOverlay; /* 0x186BE0 */
    u16  uWorldW;       /* the u16 at 0x186BC0 */
    u16  uWorldH;       /* the u16 at 0x186BC0 + 2 */
} SmapZoomSet;

/* The world size pair the zoom steps publish at 0x186BC0. It is not part of the
 * 36-byte view block, so it travels separately. */
typedef struct SmapWorldSize {
    u16 uW;
    u16 uH;
} SmapWorldSize;

/* The scroll rescale factors, read out of the muld immediates. Zooming in from
 * level 0 to 1 is NOT the reciprocal of zooming out from 1 to 0: 0.7501875468867217
 * against 1/1.33 = 0.7518796992481203. Zooming in from 1 to 2 IS exactly 1/1.27.
 * The asymmetry is in the original; a round trip does not restore the scroll. */
#define PUFO_SMAP_ZOOM_IN_0_1  0.7501875468867217
#define PUFO_SMAP_ZOOM_IN_1_2  0.7874015748031495
#define PUFO_SMAP_ZOOM_OUT_2_1 1.27
#define PUFO_SMAP_ZOOM_OUT_1_0 1.33

/* Smap_ZoomIn @ 0x81e44. Level 0 goes to 1, level 1 goes to 2, level 2 does nothing
 * (the original returns its argument unchanged). The new map size is taken from the
 * level's grid PadImage BEFORE the scroll is rescaled, and the rescale truncates:
 * dtoi() is a plain double-to-int. ZoomIn then clamps nScrollY with the NEW map
 * height; ZoomOut does not clamp at all. Returns false when the level has no step. */
bool Smap_ZoomIn(SmapView *pView, const SmapZoomSet aSets[3],
                 SmapWorldSize *pOut, i32 nStripHeight);

/* Smap_ZoomOut @ 0x81c08. Level 2 goes to 1 (scale 1.27), level 1 goes to 0 with the
 * map pair 800 x 400 (scale 1.33), level 0 does nothing. No clamping. */
bool Smap_ZoomOut(SmapView *pView, const SmapZoomSet aSets[3], SmapWorldSize *pOut);

/* Publish one zoom level's world size and map size. This is the piece both
 * Smap_ZoomIn @ 0x81e44 and Smap_ZoomOut @ 0x81c08 apply after changing nMode, and
 * Save_ReadSmap @ 0x40c4c applies to the mode it just read out of the smap chunk.
 * nMapW/nMapH come from the grid PadImage's own uWidth/uHeight; the world pair is
 * the fixed 800x400 / 600x300 / 472x236. */
void Smap_ApplyZoomSet(SmapView *pView, const SmapZoomSet *pSet, SmapWorldSize *pOut);

/* Smap_ClampScrollY @ 0x837f8. The original keeps the vertical clamp as its own
 * function: it gates on the screen id at 0x186BC4 not being 2, and takes the strip
 * height from [0x186BBC]->uHeight. Smap_ScrollBy @ 0x820d0 (0x82124..0x82160) and
 * Smap_ZoomIn @ 0x81e44 (0x82080..0x820B0) both INLINE the same code instead of
 * calling it, which is why the port has one helper and three call sites. The screen-id
 * gate lives here, not in the callers.
 *
 * 0x83830 writes `160 - nMapH/2`, the same value as -(nMapH/2 - 160). */
void Smap_ClampScrollY(SmapView *pView, i32 nStripHeight);

/* The zoom buttons. Smap_OnZoomOut @ 0x84ab8 and Smap_OnZoomIn @ 0x84ad8 are two
 * instructions each: play sample 17, then apply the zoom to the one global view.
 * The port injects the sample because the audio backend is a seam here.
 *
 * Note the asymmetry the buttons inherit: zoom out never clamps, zoom in always does,
 * with the strip height read from [0x186BBC]->uHeight in the original. */
typedef void (*SmapSampleFn)(i32 nSample, void *pUser);

bool Smap_OnZoomIn(SmapView *pView, const SmapZoomSet aSets[3], SmapWorldSize *pOut,
                   i32 nStripHeight, SmapSampleFn pfnPlay, void *pUser);
bool Smap_OnZoomOut(SmapView *pView, const SmapZoomSet aSets[3], SmapWorldSize *pOut,
                    SmapSampleFn pfnPlay, void *pUser);

/* ------------------------------------------------- ship to strateg-item lookup
 *
 * Evidence: StrategList_FindIdxForShip @ 0x6a05c.
 *
 * Finds the strateg item that represents one craft, by comparing `Ship.bId @ +0x01`
 * against `StrategItem.nDisplayId @ +0x10`. That is the only place the two id spaces
 * are shown to be the same number, and it is what `LoadGame` uses to decide whether a
 * loaded craft already has an item or needs one added.
 *
 * Two details the disassembly settles:
 *
 *   - the first argument the original is called with (`a1`, the base-block pointer at
 *     `&MEMORY[0x158890].apLines[189]`) is NEVER read. The lookup uses the craft array
 *     and the list globals only.
 *   - the index is returned through `(char)v3` at 0x6A0C8, so it is a SIGNED CHAR. A
 *     list longer than 127 items would return a negative index for a real match.
 *
 * This is NOT the same function as `StrategList_FindByShipId @ 0x5a9b4` above, even
 * though both compare `Ship.bId` with `StrategItem.nDisplayId`. That one also requires
 * `nType == 1` and `bActive == 0`; this one has no type or active gate at all and would
 * match a base, a site or an inactive craft. The original carries both, and `LoadGame`
 * calls this one. Keeping them separate is what makes the difference visible.
 *
 * Deviation: the original dereferences `apCraft[nShipIdx]` with no bound check and no
 * NULL check, so an out-of-range ship index or a NULL slot faults. The port returns -1
 * for both. */
i32 StrategList_FindIdxForShip(const StrategList *pList,
                               const GeoscapeWorld *pWorld, i32 nShipIdx);

/* ------------------------------------------------- the wrapped map distance and the range gate
 *
 * Evidence: Smap_DistWrapped @ 0x7df80, Smap_CraftRangeExhausted @ 0x7e074.
 *
 * Smap_DistWrapped @ 0x7df80 is the geoscape distance between two map points, in the scaled
 * units the range gates use. The x leg is the map cylinder -- the same
 * min(|x2-x1+800|, |x1-x2+800|, |x1-x2|) that Strateg_CalcDetect (0x813D0) and
 * StrategEntry_InitTravel already compute, here at 0x7DF84..0x7DFC4 -- and the y leg is the
 * plain |y1-y2| (0x7DFC8..0x7DFD8), because the map wraps sideways only.
 *
 * The value is (int)(sqrt((double)(dx*dx) + (double)(dy*dy)) * 50.0). Each square is an integer
 * product converted on its own through _itod and the two doubles are added through _addd; the
 * CRT sqrt scales nothing; the _muld second operand is 50.0, whose high word 0x40490000 sits at
 * 0x7E018; _dtoi truncates. So the 50 is a distance-to-range-unit scale applied before the
 * truncation, not after. */
i32 Smap_DistWrapped(i32 nX1, i32 nY1, i32 nX2, i32 nY2);

/* Smap_CraftRangeExhausted @ 0x7e074. The IDB called this Smap_CanInterceptReach; it is renamed
 * because it tests nothing about intercepting -- it is the per-minute range drain of a craft
 * away from base, plus the gate that makes Geoscape_TickShips @ 0x5d4e4 post the ship-contact
 * message (15) once and pause the clock. See docs/parity/renames.tsv.
 *
 * 1. StrategList_FindByShipId picks the strateg item that carries this craft (0x7E084) and
 *    PtrArray_Get reads it out of the strateg list at 0x1864F8+0x3C (0x7E094).
 * 2. apCraft[nShipIdx]->bHomeBase indexes apBases (0x7E0AC..0x7E0C0), and the distance is
 *    Smap_DistWrapped(base x, base y, item x, item y) at 0x7E0D4. Both points come from the
 *    craft itself: the item found in step 1 is the craft's own position entry, so this is the
 *    distance from the craft's HOME BASE to where the craft now is -- not to any target.
 * 3. The per-minute burn is (int)(uFuelCap / 60): _rt_sdiv at 0x7E0EC with 60 in R0 (the
 *    divisor) and uFuelCap in R1. It is scaled by the craft-class factor uRangeMax / N, where N
 *    is 30000, 40000, 64000, 12000 and 32000 for craft class 0..4 -- the _muld second operands
 *    at 0x7E120, 0x7E23C, 0x7E1C4, 0x7E188 and 0x7E200, each of which is exactly 1/N. The class
 *    dispatch is the test chain at 0x7E168..0x7E254 (class 0 is the fall-through at 0x7E104).
 * 4. When uRangeRemain is ABOVE that burn it is decremented by it (0x7E2A0..0x7E2C4). A budget
 *    already at or below the burn is left alone, so a craft parked away from base decays to the
 *    burn and stops there; Ship_TickRangeRecover @ 0x5d514 pulls it back up only while bAtBase.
 * 5. The answer is (double)dist * factor >= (double)uRangeRemain, with uRangeRemain read back
 *    AFTER the decrement (0x7E2F4 reloads the ship slot, 0x7E300 reloads the u16). Non-zero
 *    means the craft is further from home than its remaining range covers.
 *
 * The comparison direction is the binary's own and it is worth stating plainly: _ged takes the
 * scaled distance as its first operand (R0:R1) and uRangeRemain as its second (R2:R3), the same
 * operand order the _gtd at 0x37E04 in Tact_BuildVisRayTable uses against its 15.0 constant.
 *
 * Deviations, all listed in docs/parity/bugs.tsv: the original indexes apBases with the raw u8
 * bHomeBase -- Geoscape_TickShips' own 0xFF/0xFE filter in front of the call is dead code, since
 * 0xFF falls through the 0xFE test and 0xFE takes the BNE, so both reach it -- it hands the -1
 * that StrategList_FindByShipId answers to PtrArray_Get when the craft has no item, and a craft
 * class above 4 leaves the multiplier pair in whatever the stack held (0x7E160 reloads R8:R9
 * from var_8/var_4, which no path ever writes). The port answers false for all three. */
typedef struct SmapCraftRangeEnv {
    const GeoscapeWorld *pWorld;  /* apCraft @ Map+0x10AC, nShipCount @ Map+0x1194 */
    BaseRec *const *apBases;      /* apBases @ Map+0x1174 */
    const StrategList *pList;     /* the strateg list @ 0x1864F8+0x3C */
} SmapCraftRangeEnv;

bool Smap_CraftRangeExhausted(const SmapCraftRangeEnv *pEnv, i32 nShipIdx);

/* ------------------------------------------------- the UFO mission machine
 *
 * Evidence: Geoscape_SpawnMissionSite @ 0x7e49c.
 *
 * The outer switch is on the ARGUMENT, not on the item. -1 means "advance the mission already
 * stored in pUfo->nMissionSub". 0..7 means "start that subtype". Anything else returns
 * nMissionSub + 1 having changed nothing.
 *
 * START PATH, argument 0..7. Roll a random point, travel the craft there with bC set to the
 * subtype, then set nPhase. Subtype 0 sets it to 3; every other subtype sets it to 4. All eight
 * then zero nTargetX and nTargetY and return the travel result.
 *
 * ADVANCE PATH, argument -1. Every subtype has the same three gates in the same order:
 *
 *   nPhase > 3          pick a new target and travel to it, with bC set to the subtype
 *   nPhase == 3         teleport onto the target if it lies inside the current viewport, then
 *                       set a per-subtype dwell in nTimer and drop nPhase to 2
 *   nPhase < 3 and      run the subtype's landing effect, then move to a fresh random point
 *   nTimer == 0
 *
 * The dwells are 200, 200, 720, 2880, 60, 1 and 720 for subtypes 1 through 7.
 *
 * The tail is shared: result = nPhase, and if that is above zero and nTimer is zero then nPhase
 * drops by one. If nPhase ends at 0 and the craft is not visible, bDone is set. The returned
 * value is nPhase as it was BEFORE that decrement.
 *
 * Where the target comes from is what actually differs between subtypes:
 *
 *   1, 2, 4   roll a point and re-roll while the tile is 255, so they always land on dry ground
 *   3, 5      take a random entry from the mission-site table, 56 of them
 *   6         a random base, and it wants a base with NO free cell of facility kind 10; it
 *             retries up to 20 times and on the 20th gives up and travels to a random point
 *   7         a random ACTIVE type-0 item from the strateg list, or a random point if none
 *
 * Deviations, both recorded: the original indexes the mission-site table and the base array with
 * unbounded values, and its 100-entry scratch array for subtype 7 silently truncates a list
 * longer than 100 candidates. The port bounds the first two and caps the third.
 */
typedef struct SpawnMissionHooks {
    /* UiPanel_AppendLineWithSound @ 0x5a394 on the root panel. Subtype 4 appends flag 60 with
     * the new item's index, subtype 5 appends flag 16 with pUfo->nTargetIdx. */
    void (*pfnAppendLine)(i32 nFlag, i32 nArg2, i32 nArg3, void *pUser);
    /* BasePlaneScreen_AddInterceptMission @ 0x7de14, called with pUfo->bCraftClass. */
    i32  (*pfnAddIntercept)(i32 bCraftClass, void *pUser);
    /* BaseAtScreen_Open @ 0x588cc, the base-contact screen. The original fills the block at
     * 0x158840..0x158849 and sets the screen id 0x186BC4 to 20 before calling it. */
    void (*pfnContactScreen)(StrategItem *pUfo, void *pUser);
    void *pUser;
} SpawnMissionHooks;

typedef struct SpawnMissionCtx {
    PtrArray *pList;                    /* the strateg list, 0x186534 */
    i32 *pnSeed;                        /* the per-item seed counter, 0x18640C */
    SmapView *pView;                    /* nMapW and nMapH gate the teleport */
    const PadImage *pTilemap;           /* the land test, 255 is ocean */
    BaseRec *const *apBases;            /* subtype 6's targets */
    i32 nBaseCount;                     /* the SIGNED byte at anLineAux[79] */
    const MissionSiteRecord *apSites;   /* the table at 0x1865C8 */
    i32 nSiteCount;
    const u8 *abFacilityGrid;           /* subtype 6 asks for a free cell of kind 10 */
    i32 *pnRadarPulseTimer;             /* nRadarPulseTimer @ 0xAC224, subtype 5 sets 250 */
    GeoscapeWorld *pWorld;              /* shared by StrategEntry_InitTravel; its pTime->bPaused
                                         * is the byte GameTime_Pause @ 0x5d6ac writes */
    i32 nGameTotalSec;
    SpawnMissionHooks hooks;
} SpawnMissionCtx;

i32 Geoscape_SpawnMissionSite(StrategItem *pUfo, i32 nMissionSub, const SpawnMissionCtx *pCtx);

#endif /* PUFO_STRATEG_H */
