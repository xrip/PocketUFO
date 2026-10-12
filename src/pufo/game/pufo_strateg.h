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
    /* Base_FindIndexBySiteId @ 0x5a8d0. */
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

#endif /* PUFO_STRATEG_H */
