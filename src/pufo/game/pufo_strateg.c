/* pufo_strateg.c - see pufo_strateg.h for the evidence map. */
#include "game/pufo_strateg.h"
#include "util/pufo_rand.h"
#include "util/pufo_ptrarray.h"

#include <math.h>
#include <stdlib.h>

static i32 Abs32(i32 n)
{
    return n < 0 ? -n : n;
}

i32 StrategList_CountActive(const StrategList *pList)
{
    i32 nHits = 0;
    for (i32 i = 0; i < pList->nCount; ++i) {
        if (pList->apItems[i]->nType == 1 && pList->apItems[i]->bActive == 1)
            ++nHits;
    }
    return nHits;
}

i32 StrategList_FindByShipId(const StrategList *pList,
                             const GeoscapeWorld *pWorld, i32 nShipIdx)
{
    for (i32 i = 0; i < pList->nCount; ++i) {
        const StrategItem *pItem = pList->apItems[i];
        if (pItem->nType == 1 && pItem->bActive == 0
            && pItem->nDisplayId == pWorld->apCraft[nShipIdx]->bId)
            return i;
    }
    return -1;
}

/* Proximity radius by craft type, from the dispatch at 0x8107c:
 * 0 -> 15, 1 -> 20, 2 -> 25, 3 -> 15, 4 -> 20, anything else -> no detect. */
static i32 DetectRadius(u8 bCraftType)
{
    switch (bCraftType) {
    case 0: return 15;
    case 1: return 20;
    case 2: return 25;
    case 3: return 15;
    case 4: return 20;
    default: return -1;
    }
}

void StrategList_UpdateMotion(StrategList *pList, GeoscapeWorld *pWorld)
{
    /* LOBYTE(nDetectThresh) = 120, or 1 when the tick is longer than 300 s. */
    i32 nThresh = pWorld->nTickSeconds > 300 ? 1 : 120;

    for (i32 nIdx = 0; nIdx < pList->nCount; ++nIdx) {
        StrategItem *pItem = pList->apItems[nIdx];
        if (pItem->bActive == 0)
            continue;

        u8 bNearDetect = 0;
        for (i32 nShip = 0; nShip < pWorld->nShipCount; ++nShip) {
            Ship *pShip = pWorld->apCraft[nShip];
            if (pShip->bAtBase != 0)
                continue;

            i32 nShipItemIdx = StrategList_FindByShipId(pList, pWorld, nShip);
            if (nShipItemIdx < 0)
                continue; /* deviation: the original reads PtrArray_Get(-1) */
            const StrategItem *pShipItem = pList->apItems[nShipItemIdx];

            i32 nDx = pShipItem->nX - pItem->nX;
            i32 nDy = pShipItem->nY - pItem->nY;
            /* The original adds the threshold INSIDE the square root:
             * nDist = (int)sqrt((double)dx*dx + (double)dy*dy + thresh). */
            double dDist = sqrt((double)(nDx * nDx) + (double)(nDy * nDy)
                                + (double)nThresh);
            i32 nDist = (i32)dDist;

            i32 nRadius = DetectRadius(pShip->bCraftType);
            if (nRadius < 0 || nDist >= nRadius)
                continue;

            if (pShip->bWeaponsHot != 0) {
                if (pItem->nType == 0) {
                    bNearDetect = 1;
                    ++pItem->nTimer;
                }
                if (pItem->nType == 0 && pItem->nTimer >= (i8)nThresh) {
                    pItem->bVisible = 1;
                    pItem->nTimer = 0;
                }
            }
            if (pItem->nType == 1)
                pItem->bVisible = 1;
        }

        if (!bNearDetect && pItem->nType == 0)
            pItem->nTimer = 0;

        if (pItem->bVisible != 0 && pItem->nDisplayId == STRATEG_ID_UNASSIGNED)
            pItem->nDisplayId = pList->nNextDisplayId++;
        if (pItem->bVisible != 0 && pItem->nSiteId == STRATEG_ID_UNASSIGNED)
            pItem->nSiteId = pList->nNextSiteId++;
    }
}

/* Distance to a base with the x axis wrapped over the 1600-wide map:
 * min(|bx - ux + 800|, |ux - bx + 800|, |ux - bx|), exactly as the original
 * computes v11 / v12 / v13 at 0x813d0. */
static i32 WrappedDx(i32 nBaseX, i32 nUfoX)
{
    i32 nDx = Abs32(nBaseX - nUfoX + 800);
    i32 nAlt = Abs32(nUfoX - nBaseX + 800);
    if (nDx > nAlt)
        nDx = nAlt;
    nAlt = Abs32(nUfoX - nBaseX);
    if (nDx > nAlt)
        nDx = nAlt;
    return nDx;
}

bool Strateg_CalcDetect(StrategItem *pUfo, i32 nIndex, i32 nForcePct,
                       const StrategList *pList, const GeoscapeWorld *pWorld,
                       const StrategHooks *pHooks)
{
    i8 nPct = (i8)nForcePct;
    bool bHit = false;
    pUfo->bHyperDetect = 0;

    for (i32 nIdx = 0; nIdx < pList->nCount; ++nIdx) {
        const StrategItem *pBase = pList->apItems[nIdx];
        if (pBase->nType != 0 || pBase->bActive != 0)
            continue;

        i32 nBaseIdx = pHooks->pfnFindBaseBySiteId(pBase->nSiteId, pHooks->pUser);
        i32 nDx = WrappedDx(pBase->nX, pUfo->nX);

        /* The radar pct is re-read per base unless it is pinned at 100. */
        if (nPct != 100)
            nPct = (i8)pWorld->apRadarPct[nBaseIdx];
        if (pWorld->nRadarBoostOff == 0 && nPct != 100)
            nPct = (i8)(nPct + 7);

        if (bHit)
            continue;

        u8 nState = pWorld->apSiteState[nBaseIdx];
        double dRadius;
        bool bHyper = false;

        switch (nState) {
        case 1:  dRadius = 24.0; break;
        case 2:  dRadius = 31.0; break;
        case 11: dRadius = 24.0; bHyper = true; break;
        case 12: dRadius = 31.0; bHyper = true; break;
        default: continue;
        }

        /* All four site states require the proximity timer to be idle. */
        if (pUfo->nTimer != 0)
            continue;

        i32 nDy = pUfo->nY - pBase->nY;
        double dDist = sqrt((double)(nDx * nDx) + (double)(nDy * nDy));
        if (!(dDist <= dRadius)) /* led() in the original */
            continue;

        if (pUfo->bVisible == 0 && Game_RandU16(1000u) < (u16)nPct) {
            pHooks->pfnContactMsg(10, nIndex, 0, pHooks->pUser);
            pWorld->pTime->bPaused = 1; /* GameTime_Pause @ 0x5d6ac, 0x818fc */
            bHit = true;
            if (bHyper)
                pUfo->bHyperDetect = 1;
        }
        if (nPct == 100) {
            bHit = true;
            if (bHyper)
                pUfo->bHyperDetect = 1;
        }
    }
    return bHit;
}

void StrategList_UpdateDetect(StrategList *pList, GeoscapeWorld *pWorld,
                              const StrategHooks *pHooks)
{
    for (i32 nIdx = 0; nIdx < pList->nCount; ++nIdx) {
        StrategItem *pItem = pList->apItems[nIdx];
        if (pItem->nType != 1 || pItem->bActive != 1)
            continue;

        i32 nForce = pItem->bDetected != 0 ? 100 : 0;
        bool bDet = Strateg_CalcDetect(pItem, nIdx, nForce, pList, pWorld, pHooks);
        pItem->bDetected = bDet ? 1 : 0;
        if (bDet) {
            pItem->bVisible = 1;
            if (pItem->nDisplayId == STRATEG_ID_UNASSIGNED)
                pItem->nDisplayId = pList->nNextDisplayId++;
        } else {
            pItem->bVisible = 0;
            pItem->bHyperDetect = 0;
        }
    }
}

void Ship_ClearExpiredIntercepts(GeoscapeWorld *pWorld, StrategList *pList,
                                 const StrategHooks *pHooks)
{
    for (i32 nShip = 0; nShip < pWorld->nShipCount; ++nShip) {
        Ship *pShip = pWorld->apCraft[nShip];
        if (pShip->bIntercepting == 0)
            continue;
        if (pShip->nStrategIdx < 0 || pShip->nStrategIdx >= pList->nCount)
            continue; /* deviation: the original does not bounds-check */
        const StrategItem *pTarget = pList->apItems[pShip->nStrategIdx];
        if (pTarget->bVisible == 0 && pTarget->nDisplayId != STRATEG_ID_UNASSIGNED) {
            pHooks->pfnContactMsg(13, pShip->nStrategIdx, nShip, pHooks->pUser);
            pShip->bIntercepting = 0;
            pWorld->pTime->bPaused = 1; /* GameTime_Pause @ 0x5d6ac, 0x81340 */
        }
    }
}

/* ------------------------------------------------------------ the strateg list */

void StrategList_ctor(PtrArray *pList)
{
    if (pList == NULL) {
        return;
    }
    PtrArray_ctor(pList); /* 0x83890 calls it on strategList @ 0x186534 */
}

void StrategList_Clear(PtrArray *pList)
{
    if (pList == NULL) {
        return;
    }
    PtrArray_DeleteAll(pList); /* 0x838bc */
    PtrArray_Clear(pList);     /* 0x838c8 */
}

StrategItem *StrategItem_ctor(StrategItem *pItem, i32 nSeed)
{
    if (pItem == NULL) {
        return NULL;
    }
    /* 0x82bdc: the only write. The original reads the global dword at 0x18640C, which
     * StrategList_AddItem @ 0x826e8 reads at 0x82A7C and increments at 0x82A84, so it is
     * a per-item seed counter that starts at zero. Nothing else in the record is
     * initialised -- operator new leaves the other 132 bytes as they were. */
    pItem->nSeed = nSeed;
    return pItem;
}

bool StrategItem_AllocAdd(PtrArray *pList, i32 nSeed)
{
    StrategItem *pItem;

    if (pList == NULL) {
        return false;
    }
    pItem = (StrategItem *)malloc(sizeof(StrategItem)); /* operator new(0x88), 0x415d8 */
    if (pItem != NULL) {
        StrategItem_ctor(pItem, nSeed);
    }
    /* 0x415fc: the add is unconditional, so a failed allocation appends a null entry.
     * Reproduced rather than fixed, because PtrArray_Get callers then see the null the
     * original would have stored. */
    PtrArray_Add(pList, pItem);
    return pItem != NULL;
}

/* World scale of each zoom level, recovered from the muld immediates at 0x82790 and
 * 0x82818: 0x3FF5555555555556 and 0x3FFB0A3D70A3D70A. */
#define STRATEG_SCALE_MODE1 1.333
#define STRATEG_SCALE_MODE2 1.694

StrategItem *StrategList_AddItem(PtrArray *pList, i32 *pnSeed, i32 nZoomMode,
                                 i32 nType, i32 bActive, i32 bCraftClass,
                                 i32 x, i32 y, i32 nId, i32 bVisible)
{
    StrategItem *pItem;
    i32 nx = x;
    i32 ny = y;

    if (pList == NULL || pnSeed == NULL) {
        return NULL;
    }

    if (!StrategItem_AllocAdd(pList, *pnSeed)) { /* 0x82704 */
        return NULL; /* deviation: the original faults on the first field store */
    }
    *pnSeed += 1; /* 0x82A84 */

    pItem = (StrategItem *)PtrArray_Get(pList, pList->nCount - 1);
    if (pItem == NULL) {
        return NULL;
    }

    pItem->nType   = (u8)nType;   /* 0x82728 */
    pItem->bActive = (u8)bActive; /* 0x82748 */

    if (nType == 5) { /* 0x8275C: a type-5 item is placed in level-0 world units */
        if (nZoomMode == 1) {
            nx = (i32)((double)nx * STRATEG_SCALE_MODE1);
            ny = (i32)((double)ny * STRATEG_SCALE_MODE1);
        }
        if (nZoomMode == 2) {
            nx = (i32)((double)nx * STRATEG_SCALE_MODE2);
            ny = (i32)((double)ny * STRATEG_SCALE_MODE2);
        }
    }

    pItem->bHyperDetect = 0;  /* 0x82888 */
    pItem->bDetected    = 0;  /* 0x828A0 */
    pItem->bRace        = 255; /* 0x828BC: written as -1 */
    pItem->nPhase       = 0;  /* 0x828D4 */
    pItem->bDone        = 0;  /* 0x828EC */
    pItem->nX           = nx; /* 0x82904 */
    pItem->nY           = ny; /* 0x8291C */
    pItem->nStartX      = nx; /* 0x82934 */
    pItem->nStartY      = ny; /* 0x8294C */
    pItem->nDestX       = nx; /* 0x82964 */
    pItem->nDestY       = ny; /* 0x82984 */

    if (nType == 0 || nType == 3) { /* 0x82990 */
        pItem->nSiteId    = nId; /* 0x829A8 */
        pItem->nDisplayId = -1;  /* 0x829C0 */
    }
    if (nType == 1) { /* 0x829C8 */
        pItem->nDisplayId = nId; /* 0x829E0 */
        pItem->nSiteId    = -1;  /* 0x829F8 */
    }
    /* Any other nType leaves nSiteId and nDisplayId as whatever operator new left. */

    pItem->bVisible     = (u8)bVisible; /* 0x82A14 */
    pItem->nTimer       = 0;            /* 0x82A2C */
    pItem->bFlagA       = 0;            /* 0x82A44 */
    pItem->bCraftClass  = (u8)bCraftClass; /* 0x82A5C */
    pItem->nHp          = -1;           /* 0x82A74 */

    return pItem;
}

/* ------------------------------------------------------- the geoscape viewport */

/* The vertical clamp both Smap_ScrollBy @ 0x82124..0x82160 and Smap_ZoomIn
 * @ 0x82080..0x820B0 apply. Identical in both, so it lives here once. The upper test
 * uses the value the lower clamp produced, not the one it started from. */
void Smap_ClampScrollY(SmapView *pView, i32 nStripHeight)
{
    const i32 nHalfH = pView->nMapH / 2 - 160;
    i32 nY = pView->nScrollY;

    if (nY + nHalfH < 0) {
        nY = -nHalfH;
        pView->nScrollY = nY;
    }
    if (nY + nHalfH > nStripHeight + pView->nMapH - 320) {
        pView->nScrollY = nStripHeight - nHalfH + pView->nMapH - 320;
    }
}

void Smap_ScrollBy(SmapView *pView, i32 nDx, i32 nDy, i32 nScreenMode,
                   i32 nStripHeight)
{
    if (pView == NULL) {
        return;
    }

    /* 0x82104 / 0x82114: pos - delta + last_delta. The view tracks absolute positions,
     * so the delta it applied last time is added back before the new one is taken off. */    pView->nScrollX = pView->nScrollX - nDx + pView->nLastDx;
    pView->nScrollY = pView->nScrollY - nDy + pView->nLastDy;

    if (nScreenMode != 2) {
        Smap_ClampScrollY(pView, nStripHeight);
    }

    /* 0x82164 / 0x82168: stored after any clamping. */
    pView->nLastDx = nDx;
    pView->nLastDy = nDy;
}

u8 Smap_GetTileAtScreen(SmapView *pView, const u8 *pTiles, i32 nX, i32 nY)
{
    i32 nTileX;
    i32 nTileY;

    if (pView == NULL) {
        return 0;
    }

    /* 0x8227C / 0x82288: the view centre is screen (120, 160). */
    nTileX = pView->nScrollX + pView->nMapW / 2 + nX - 120;
    nTileY = pView->nScrollY + pView->nMapH / 2 + nY - 160;

    /* 0x8228C / 0x82294: one wrap for a negative coordinate, no wrap for an
     * out-of-range positive one. The map is a cylinder only on the low side. */
    if (nTileX < 0) {
        nTileX += pView->nMapW;
    }
    if (nTileY < 0) {
        nTileY += pView->nMapH;
    }

    pView->nPickX = nTileX; /* 0x822B0 / 0x822B4 */
    pView->nPickY = nTileY;

    /* Deviation 1: the original indexes pData[nStride * tileY + tileX] with no range
     * check at all. The port refuses the read and returns 0. */
    if (pTiles == NULL || nTileX < 0 || nTileX >= pView->nMapW ||
        nTileY < 0 || nTileY >= pView->nMapH) {
        return 0;
    }
    return pTiles[pView->nMapW * nTileY + nTileX];
}

u8 Tilemap_GetAt(const PadImage *pMap, i32 nX, i32 nY)
{
    if (pMap == NULL || pMap->pData == NULL) {
        return 0;
    }
    /* 0x82198 and 0x821BC are the same expression over two different globals
     * (0x186B74 and 0x186C18), so one implementation covers both. */
    return pMap->pData[(i32)pMap->uWidth * nY + nX];
}

/* Publish the level's map size, the way 0x81DA0..0x81DB0 and 0x81EC0..0x81FF0 do it:
 * nMapW/nMapH come from the level's grid PadImage, and the world pair goes to the
 * separate 0x186BC0 global. */
void Smap_ApplyZoomSet(SmapView *pView, const SmapZoomSet *pSet,
                       SmapWorldSize *pOut)
{
    if (pSet->pGrid != NULL) {
        pView->nMapW = (i32)pSet->pGrid->uWidth;
        pView->nMapH = (i32)pSet->pGrid->uHeight;
    }
    if (pOut != NULL) {
        pOut->uW = pSet->uWorldW;
        pOut->uH = pSet->uWorldH;
    }
}

bool Smap_ZoomIn(SmapView *pView, const SmapZoomSet aSets[3],
                 SmapWorldSize *pOut, i32 nStripHeight)
{
    double dScale;

    if (pView == NULL || aSets == NULL) {
        return false;
    }
    if (pView->nMode == 0) {
        dScale = PUFO_SMAP_ZOOM_IN_0_1; /* 0x82014: muld immediates */
        pView->nMode = 1;
    } else if (pView->nMode == 1) {
        dScale = PUFO_SMAP_ZOOM_IN_1_2; /* 0x81EF4 */
        pView->nMode = 2;
    } else {
        return false; /* 0x81E5C: level 2 has no step, nothing is touched */
    }

    Smap_ApplyZoomSet(pView, &aSets[pView->nMode], pOut);

    /* itod / muld / dtoi is a plain int -> double -> int, so the product truncates
     * toward zero. The new map size is already in place when the clamp runs. */
    pView->nScrollX = (i32)((double)pView->nScrollX * dScale);
    pView->nScrollY = (i32)((double)pView->nScrollY * dScale);

    Smap_ClampScrollY(pView, nStripHeight);
    return true;
}

bool Smap_ZoomOut(SmapView *pView, const SmapZoomSet aSets[3], SmapWorldSize *pOut)
{
    double dScale;

    if (pView == NULL || aSets == NULL) {
        return false;
    }
    if (pView->nMode == 1) {
        dScale = PUFO_SMAP_ZOOM_OUT_1_0; /* 0x81DD4 */
        pView->nMode = 0;
    } else if (pView->nMode == 2) {
        dScale = PUFO_SMAP_ZOOM_OUT_2_1; /* 0x81CB4 */
        pView->nMode = 1;
    } else {
        return false; /* 0x81C14 / 0x81C1C: level 0 has no step */
    }

    Smap_ApplyZoomSet(pView, &aSets[pView->nMode], pOut);

    pView->nScrollX = (i32)((double)pView->nScrollX * dScale);
    pView->nScrollY = (i32)((double)pView->nScrollY * dScale);

    /* Smap_ZoomOut applies no clamp, unlike Smap_ZoomIn. */
    return true;
}

/* Smap_OnZoomOut @ 0x84ab8 and Smap_OnZoomIn @ 0x84ad8: the two zoom buttons.
 * Each is Sound_Play(17) followed by a tail call on the one global view at 0x1863E8,
 * and each returns the zoom result straight through (0x84AC4/0x84AC8, 0x84AE4/0x84AE8).
 * The sample is a hook because the audio backend is a seam in this port. */
bool Smap_OnZoomOut(SmapView *pView, const SmapZoomSet aSets[3], SmapWorldSize *pOut,
                    SmapSampleFn pfnPlay, void *pUser)
{
    if (pfnPlay != NULL) {
        pfnPlay(17, pUser); /* 0x84AC4 */
    }
    return Smap_ZoomOut(pView, aSets, pOut);
}

bool Smap_OnZoomIn(SmapView *pView, const SmapZoomSet aSets[3], SmapWorldSize *pOut,
                   i32 nStripHeight, SmapSampleFn pfnPlay, void *pUser)
{
    if (pfnPlay != NULL) {
        pfnPlay(17, pUser); /* 0x84AE4 */
    }
    return Smap_ZoomIn(pView, aSets, pOut, nStripHeight);
}

/* StrategList_FindIdxForShip @ 0x6a05c. No type gate, no active gate: the first item
 * whose nDisplayId equals the craft's bId wins. */
i32 StrategList_FindIdxForShip(const StrategList *pList,
                               const GeoscapeWorld *pWorld, i32 nShipIdx)
{
    const Ship *pShip;
    i32 i;

    if (pList == NULL || pWorld == NULL || pList->nCount <= 0) {
        return -1; /* 0x6A078 */
    }
    if (nShipIdx < 0 || nShipIdx >= (i32)pWorld->nShipCount
        || pWorld->apCraft == NULL || pWorld->apCraft[nShipIdx] == NULL) {
        return -1; /* deviation: the original faults here */
    }
    pShip = pWorld->apCraft[nShipIdx];

    for (i = 0; i < pList->nCount; ++i) {
        const StrategItem *pItem = pList->apItems[i];

        if (pItem == NULL) {
            continue;
        }
        if (pShip->bId == pItem->nDisplayId) { /* 0x6A0AC */
            return (i32)(signed char)i;       /* 0x6A0C8: the index is a signed char */
        }
    }
    return -1;
}
