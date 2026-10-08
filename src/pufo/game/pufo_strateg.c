/* pufo_strateg.c - see pufo_strateg.h for the evidence map. */
#include "game/pufo_strateg.h"
#include "util/pufo_rand.h"

#include <math.h>

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
