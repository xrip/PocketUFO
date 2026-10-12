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
 * global dword at 0x18640C. That global has exactly three xrefs in the IDB -- two in
 * StrategList_AddItem @ 0x826e8 and this one -- and no writer, so it is zero for the
 * whole run. The port takes the value as a parameter instead of hiding a global that
 * is never written. */
StrategItem *StrategItem_ctor(StrategItem *pItem, i32 nSeed);

/* StrategItem_AllocAdd @ 0x415cc: operator new(0x88), StrategItem_ctor, then
 * PtrArray_Add. The add happens whether or not the allocation succeeded, so a failed
 * allocation appends a null entry -- reproduced as written. */
bool StrategItem_AllocAdd(PtrArray *pList, i32 nSeed);

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

#endif /* PUFO_STRATEG_H */
