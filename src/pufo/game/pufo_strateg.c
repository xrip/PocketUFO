/* pufo_strateg.c - see pufo_strateg.h for the evidence map. */
#include "game/pufo_strateg.h"
#include "util/pufo_rand.h"
#include "game/pufo_geoscape.h"   /* Base_FindFacilityCell */
#include "game/pufo_ship.h"      /* BaseRec */
#include "util/pufo_ptrarray.h"
#include "ui/pufo_linelist.h"  /* UiLineList_Remove, for Smap_ClearSelection */

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

StrategItem *StrategList_AddUfoAt(PtrArray *pList, i32 *pnSeed, i32 nZoomMode,
                                  i32 x, i32 y, i32 bCraftClass)
{
    return StrategList_AddItem(pList, pnSeed, nZoomMode, 1, 1, bCraftClass,
                               x, y, STRATEG_ID_UNASSIGNED, 0); /* 0x7E3AC */
}

/* The weighted walk both picks share: roll 1..100, add weights left to right, stop
 * when the running total reaches the roll. The loop covers only the first seven
 * columns, so an over-high roll leaves the index at 7. */
static i32 RollWeighted(const u8 *apRow, i32 nRoll)
{
    i32 nSum = 0;
    i32 nIdx = 0;

    for (;;) {
        nSum += apRow[nIdx];
        if (nRoll <= nSum) {
            break;
        }
        ++nIdx;
        if (nIdx >= 7) {
            break;
        }
    }
    return nIdx;
}

int StrategList_RollCraftStats(PtrArray *pList, i32 nIndex, i32 nDifficulty,
                               i32 nEarlyGate, const StrategRollTables *pTables,
                               StrategSpawnMissionFn pfnSpawn, void *pUser)
{
    StrategItem *pItem;
    i32 nMissionSub;

    if (pList == NULL || pTables == NULL) {
        return 0;
    }
    pItem = (StrategItem *)PtrArray_Get(pList, nIndex); /* 0x81AAC */
    if (pItem == NULL) {
        return 0;
    }

    if (nDifficulty > 5) { /* 0x81AC0 */
        nDifficulty = 5;
    }
    if (nDifficulty < 0) {
        nDifficulty = 0; /* deviation: the original would read before the tables */
    }

    pItem->bRace = (u8)RollWeighted(pTables->aRaceWeight[nDifficulty],
                                    (i32)Game_RandU16(100) + 1); /* 0x81B24 */

    pItem->bCraftClass = (u8)RollWeighted(pTables->aCraftWeight[nDifficulty],
                                          (i32)Game_RandU16(100) + 1); /* 0x81B90 */
    if (nEarlyGate < 15 && pItem->bCraftClass > 2) { /* 0x81B7C */
        pItem->bCraftClass = 2;
    }

    /* The mission row is chosen from the class that survived the cap, read back
     * off the item rather than from a local. */
    nMissionSub = RollWeighted(pTables->aMissionWeight[pItem->bCraftClass & 7],
                               (i32)Game_RandU16(100) + 1);

    return pfnSpawn(pItem, nMissionSub, pUser); /* 0x81BDC tail call */
}

bool StrategList_AddAlienSiteIfValid(PtrArray *pList, i32 *pnSeed, i32 nZoomMode,
                                     const PadImage *pMap, i32 x, i32 y,
                                     i32 bDiscovered)
{
    if (Tilemap_GetAt(pMap, x, y) == 255) { /* 0x7E3D8: ocean */
        return false;
    }
    /* 0x7E404: bCraftClass -1 becomes 255 through the byte store. */
    (void)StrategList_AddItem(pList, pnSeed, nZoomMode, 0, 1, 255, x, y,
                              STRATEG_ID_UNASSIGNED, bDiscovered);
    return true;
}

/* Per-class speed bases, recovered from the subd immediates at 0x7F7BC, 0x7F890,
 * 0x7F95C and 0x7F9B4, and the integer bases at 0x7F804, 0x7F83C, 0x7F8D0 and 0x7F908. */
static const double g_aCraftSpeed[8] = {
    36.6666, 40.0, 45.0, 66.6666, 71.0, 80.0, 53.6666, 83.3333
};
/* The extra jitter roll per class; 0 means the jitter is used as drawn. */
static const i32 g_aCraftJitter[8] = { 0, 3, 3, 5, 5, 5, 4, 5 };

#define STRATEG_SPEED_FLOOR 10.0
#define STRATEG_TRACKA_SCALE 0.02
#define STRATEG_FUEL_SPEED_SCALE (1.0 / 60.0)
#define STRATEG_MAP_WRAP_X 800

i32 StrategEntry_InitTravel(StrategItem *pEntry, i32 nX0, i32 nY0, i32 nX1, i32 nY1,
                            i32 nFlag, i32 bA, i32 bB, i32 bC, i32 bCopyStart,
                            const GeoscapeWorld *pWorld, i32 nGameTotalSec)
{
    i32 nDistWrapA, nDistWrapB, nDistPlain, nDx, nDy;

    if (pEntry == NULL) {
        return 0;
    }

    pEntry->nDestX      = nX1;    /* 0x7F618 */
    pEntry->nDestY      = nY1;    /* 0x7F628 */
    pEntry->dFracX      = 0.0;    /* 0x7F624, 0x7F630 */
    pEntry->dFracY      = 0.0;    /* 0x7F640, 0x7F650 */
    pEntry->bVisible    = (u8)bB; /* 0x7F638 */
    pEntry->nStartX     = nX0;    /* 0x7F644 */
    pEntry->nStartY     = nY0;    /* 0x7F654 */
    pEntry->nMissionSub = (u8)bC; /* 0x7F648 */
    pEntry->bFlagA      = (u8)bA; /* 0x7F658 */
    pEntry->nTimer      = nFlag;  /* 0x7F660 */

    if (bCopyStart != 0) { /* 0x7F66C */
        pEntry->nX = nX0;
        pEntry->nY = nY0;
    }
    pEntry->bTravelDir = 0; /* 0x7F674 */

    if (pEntry->nType == 1) { /* 0x7F67C: only craft get a speed */
        if (pEntry->bActive != 0) {
            i8  nJitter = (i8)Game_RandU16(0x12); /* 0x7F74C: 0..17 */
            u8  nClass  = pEntry->bCraftClass;

            pEntry->bRandA = (u8)nJitter;
            if (nClass <= 7) {
                if (g_aCraftJitter[nClass] != 0) {
                    nJitter = (i8)(nJitter * (i8)Game_RandU16((u16)g_aCraftJitter[nClass]));
                    pEntry->bRandA = (u8)nJitter;
                }
                pEntry->dSpeed = g_aCraftSpeed[nClass] - (double)nJitter;
            }
            /* A class outside 0..7 leaves dSpeed at whatever it already was. */
            if (pEntry->dSpeed < STRATEG_SPEED_FLOOR) { /* 0x7F9F4 */
                pEntry->dSpeed = STRATEG_SPEED_FLOOR;
            }
        } else {
            i32 nShip = 0;
            bool bFound = false;

            if (pWorld != NULL) {
                for (nShip = 0; nShip < (i32)pWorld->nShipCount; ++nShip) { /* 0x7F6C4 */
                    if (pWorld->apCraft[nShip] != NULL
                        && pWorld->apCraft[nShip]->bId == pEntry->nDisplayId) {
                        bFound = true;
                        break;
                    }
                }
            }
            if (bFound) {
                pEntry->dSpeed = (double)pWorld->apCraft[nShip]->uFuelCap
                               * STRATEG_FUEL_SPEED_SCALE; /* 0x7F714 */
            } else {
                /* Deviation: the original indexes apCraft at nShipCount, one past
                 * the end, when nothing matches. The port leaves the speed alone. */
            }
            if (nFlag > 0) { /* 0x7F720 */
                pEntry->dSpeed = 0.0;
            }
        }
    }

    if (pEntry->nTimer > 0) { /* 0x7FA1C: a dwelling item does not move */
        pEntry->dSpeed = 0.0;
    }

    pEntry->dTrackA    = pEntry->dSpeed * STRATEG_TRACKA_SCALE; /* 0x7FA3C */
    pEntry->bTravelDir = 1; /* 0x7FA50 */

    /* The shorter of the direct leg and the wrap-around one. */
    nDistWrapA = Abs32(pEntry->nDestX - pEntry->nStartX + STRATEG_MAP_WRAP_X); /* 0x7FA70 */
    pEntry->nTravelStartSec = nDistWrapA;
    nDistWrapB = Abs32(pEntry->nStartX - pEntry->nDestX + STRATEG_MAP_WRAP_X); /* 0x7FA88 */
    if (nDistWrapA > nDistWrapB) {
        pEntry->nTravelStartSec = nDistWrapB;
        pEntry->bTravelDir = -1;
    }
    nDistPlain = Abs32(pEntry->nStartX - pEntry->nDestX); /* 0x7FAA8 */
    if (pEntry->nTravelStartSec > nDistPlain) {
        pEntry->nTravelStartSec = nDistPlain;
        pEntry->bTravelDir = (pEntry->nStartX >= pEntry->nDestX) ? -1 : 1;
    }

    /* x is wrap-aware here and y is not. */
    nDx = pEntry->nTravelStartSec;
    nDy = pEntry->nDestY - pEntry->nStartY;
    if (pEntry->dTrackA != 0.0) {
        pEntry->uTravelMin =
            (u16)(sqrt((double)(nDy * nDy) + (double)(nDx * nDx)) / pEntry->dTrackA);
    } else {
        /* Deviation: the original divides by zero whenever the speed is zero. */
        pEntry->uTravelMin = 0;
    }

    pEntry->nTravelEpochSec = nGameTotalSec; /* 0x7FB6C, apLines[105] */
    return nGameTotalSec;
}

i32 StrategList_MaybeSpawnCraft_Process(PtrArray *pList, i32 *pnSeed, i32 nZoomMode,
                                        const StrategRollTables *pTables, i32 nDifficulty,
                                        i32 nEarlyGate, i32 bSpawn,
                                        StrategSpawnMissionFn pfnSpawnMission,
                                        StrategProcessEventsFn pfnProcessEvents,
                                        void *pUser)
{
    if (pList == NULL || pfnProcessEvents == NULL) {
        return 0;
    }
    if (bSpawn == 1) { /* 0x7E434: exactly 1, not merely non-zero */
        i32 nX = (i32)Game_RandU16(0x2F8) + 20; /* 0x7E440, drawn first */
        i32 nY = (i32)Game_RandU16(0x168) + 20; /* 0x7E448 */

        (void)StrategList_AddUfoAt(pList, pnSeed, nZoomMode, nX, nY, 0); /* 0x7E46C */
        (void)StrategList_RollCraftStats(pList, pList->nCount - 1, nDifficulty,
                                         nEarlyGate, pTables, pfnSpawnMission, pUser);
    }
    return pfnProcessEvents(pUser); /* 0x7E488 tail call, runs either way */
}

/* The same rule as StrategList_CountActive @ 0x81a2c, over the real container.
 * The port carries two views of the one global strateg list -- PtrArray and
 * StrategList -- and they are not castable. That is a design problem to settle
 * separately; this is the smallest honest bridge. */
static i32 CountActiveCraft(const PtrArray *pList)
{
    i32 nHits = 0;
    i32 i;

    for (i = 0; i < pList->nCount; ++i) {
        const StrategItem *pItem = (const StrategItem *)pList->apItems[i];
        if (pItem != NULL && pItem->nType == 1 && pItem->bActive == 1) {
            ++nHits;
        }
    }
    return nHits;
}

i32 StrategList_RemoveAt(PtrArray *pList, const GeoscapeWorld *pWorld, i32 nIndex,
                         u8 *pnSpawnOnceFlag, const StrategSpawnCtx *pCtx)
{
    i32 nShip;
    i32 nOnce;

    if (pList == NULL || nIndex < 0) {
        return 0;
    }

    /* 0x82AC4: re-point every intercept that sits after the hole being made. */
    if (pWorld != NULL) {
        for (nShip = 0; nShip < (i32)pWorld->nShipCount; ++nShip) {
            Ship *pShip = pWorld->apCraft[nShip];
            if (pShip != NULL && pShip->bIntercepting != 0 && pShip->nStrategIdx > nIndex) {
                pShip->nStrategIdx -= 1; /* 0x82AFC */
            }
        }
    }

    PtrArray_DeleteAt(pList, nIndex); /* 0x82B28 */

    nOnce = (pnSpawnOnceFlag != NULL) ? (i32)*pnSpawnOnceFlag : 0; /* 0x82B3C */
    if (nOnce == 0) {
        return nOnce;
    }

    if (pCtx != NULL) {
        i32 nMissing = 3 - CountActiveCraft(pList); /* 0x82B50 */
        while (nMissing > 0) {
            if (Game_RandU16(3) == 1) { /* 0x82B70: exactly 1 */
                (void)StrategList_MaybeSpawnCraft_Process(pList, pCtx->pnSeed,
                                                          pCtx->nZoomMode, pCtx->pTables,
                                                          pCtx->nDifficulty, pCtx->nEarlyGate,
                                                          1, pCtx->pfnSpawnMission,
                                                          pCtx->pfnProcessEvents, pCtx->pUser);
            }
            --nMissing;
        }
        *pnSpawnOnceFlag = 0; /* 0x82B94 */
    }
    return 0;
}

i32 StrategList_PurgeType5(PtrArray *pList, const GeoscapeWorld *pWorld,
                           u8 *pnSpawnOnceFlag, const StrategSpawnCtx *pCtx)
{
    i32 i = 0;
    int bKeep = 1; /* 0x6AB60: set once, before the loop, and never reset per iteration */

    if (pList == NULL) {
        return 0;
    }
    if (pList->nCount <= 0) {
        return pList->nCount;
    }

    for (;;) {
        const StrategItem *pItem = (const StrategItem *)pList->apItems[i];

        if (pItem != NULL && pItem->nType == 5) {
            bKeep = 0; /* 0x6ABA0 */
            if (pWorld != NULL) {
                i32 nShip;
                for (nShip = 0; nShip < (i32)pWorld->nShipCount; ++nShip) {
                    const Ship *pShip = pWorld->apCraft[nShip];
                    if (pShip != NULL && pShip->bIntercepting != 0
                        && pShip->nStrategIdx == i) {
                        bKeep = 1; /* 0x6ABD0: a ship claims it */
                    }
                }
            }
        }

        if (bKeep == 0) {
            (void)StrategList_RemoveAt(pList, pWorld, i, pnSpawnOnceFlag, pCtx);
            if (i < pList->nCount - 1) {
                continue; /* 0x6AC08: re-tests the SAME index, with bKeep still 0 */
            }
        }
        ++i;
        if (i >= pList->nCount) {
            break;
        }
    }
    return pList->nCount;
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

/* Ship_FindIndexByStrategType @ 0x5a92c. The mirror of the function above: that one takes a
 * ship index and returns the strateg index, this one takes a strateg index and returns the
 * ship index. It compares the item's nDisplayId against each craft's bId, and like its mirror
 * it gates on nothing else -- not item type, not active state.
 *
 * Deviation: the original calls PtrArray_Get(nStrategIdx) and dereferences the result with no
 * bounds test, so an out of range strateg index faults. The port returns -1. */
/* Smap_ClearSelection @ 0x84648. Drop the first line of the geoscape message list, and unpause the
 * game only if that leaves the list EMPTY. That conditional is the whole point of the function: a
 * contact message pauses the game, and clearing the selection is allowed to resume it only once no
 * message is still on screen. With two messages left it clears the selection and leaves the game
 * paused.
 *
 * It then clears the selection flag and returns its address, which is what the original hands back. */
u8 Smap_ClearSelection(UiLineList *pLines, GameTime *pTime, u8 *pbSelectionFlag)
{
    if (pLines != NULL) {
        UiLineList_Remove(pLines, 0); /* 0x84658 */

        if (pLines->nLineCount == 0 && pTime != NULL) { /* 0x84664 */
            GameTime_Unpause(pTime); /* 0x84668 */
        }
    }

    if (pbSelectionFlag != NULL) {
        *pbSelectionFlag = 0; /* 0x84674 */
    }

    return pbSelectionFlag != NULL ? *pbSelectionFlag : 0;
}

/* Smap_DismissSelected @ 0x84428. The dismiss half of the map selection, the same shape as
 * Smap_ClearSelection plus one step: the selected craft is sent home first.
 *
 * The craft index is COLOUR SLOT 0 of line 0 - the contact message stores the ship index in its
 * own record, which is also how Smap_BeginCraftSelect picks the craft (0x839CC). Then, in the
 * original's order: Ship_RetargetTravel (0x84448), drop line 0 (0x84454), unpause only if the
 * removal left the list empty (0x84460, the count byte at +3400), clear the selection flag
 * (0x84470).
 *
 * Deviation: the original passes the root panel pointer 0x158890 as Ship_RetargetTravel's first
 * argument, which that function never reads; the port passes the removal env the retarget
 * actually needs. Like Smap_ClearSelection the port returns the flag's value where the original
 * hands back the address of byte_159E84. */
u8 Smap_DismissSelected(const ShipRemovalEnv *pShipEnv, UiLineList *pLines,
                        GameTime *pTime, u8 *pbSelectionFlag)
{
    const i32 nShipIdx = (i32)UiLineList_GetColor(pLines, 0, 0); /* 0x8443C */

    (void)Ship_RetargetTravel(pShipEnv, nShipIdx); /* 0x84448 */
    UiLineList_Remove(pLines, 0);                  /* 0x84454 */

    if (pLines != NULL && pLines->nLineCount == 0 && pTime != NULL) {
        GameTime_Unpause(pTime); /* 0x84464 */
    }

    if (pbSelectionFlag != NULL) {
        *pbSelectionFlag = 0; /* 0x84470 */
        return *pbSelectionFlag;
    }
    return 0;
}

i32 Ship_FindIndexByStrategType(const GeoscapeWorld *pWorld, const StrategList *pList,
                                i32 nStrategIdx)
{
    const StrategItem *pItem;

    if (pWorld == NULL || pList == NULL || pWorld->nShipCount <= 0) {
        return -1; /* 0x5A94C */
    }
    if (nStrategIdx < 0 || nStrategIdx >= pList->nCount || pList->apItems == NULL ||
        pList->apItems[nStrategIdx] == NULL) {
        return -1; /* deviation: the original faults here */
    }
    pItem = pList->apItems[nStrategIdx];

    for (i32 i = 0; i < (i32)pWorld->nShipCount; ++i) {
        if (pWorld->apCraft != NULL && pWorld->apCraft[i] != NULL &&
            pItem->nDisplayId == pWorld->apCraft[i]->bId) {
            return i; /* 0x5A9A0 */
        }
    }

    return -1; /* 0x5A998 */
}



/* ------------------------------------------------- the UFO mission machine */

/* The dwell each subtype sets when it arrives: gate 2 writes it and drops nPhase to 2. */
static const i32 g_anMissionDwell[8] = { 0, 200, 200, 720, 2880, 60, 1, 720 };

/* Move the craft to a fresh random point. In this path the y roll is drawn FIRST; the
 * land-search loop draws x first, and the original keeps that difference, so the port does too. */
static void MissionRollTravel(StrategItem *pUfo, const SpawnMissionCtx *pCtx, i32 bC)
{
    i32 nY = (i32)Game_RandU16(0x168) + 20;
    i32 nX = (i32)Game_RandU16(0x2F8) + 20;

    StrategEntry_InitTravel(pUfo, pUfo->nX, pUfo->nY, nX, nY, 0, 0, pUfo->bVisible, bC, 0,
                            pCtx->pWorld, pCtx->nGameTotalSec);
}

static void MissionTravelToTarget(StrategItem *pUfo, const SpawnMissionCtx *pCtx, i32 bC)
{
    StrategEntry_InitTravel(pUfo, pUfo->nX, pUfo->nY, pUfo->nTargetX, pUfo->nTargetY,
                            0, 0, pUfo->bVisible, bC, 0, pCtx->pWorld, pCtx->nGameTotalSec);
}

/* Subtypes 1, 2 and 4 re-roll until the tile is not 255, so their target is always dry ground. */
static void MissionPickLandTarget(StrategItem *pUfo, const SpawnMissionCtx *pCtx)
{
    i32 nX, nY;

    do {
        nX = (i32)Game_RandU16(0x2F8) + 20;
        nY = (i32)Game_RandU16(0x168) + 20;
    } while (Tilemap_GetAt(pCtx->pTilemap, nX, nY) == 255);

    pUfo->nTargetX = nX;
    pUfo->nTargetY = nY;
}

/* Subtypes 3 and 5 take a random entry from the 56-record mission-site table. Subtype 5 also
 * remembers which one, because its landing effect reports that index to the radar message. */
static void MissionPickSiteTarget(StrategItem *pUfo, const SpawnMissionCtx *pCtx, i32 bKeepIdx)
{
    i32 nIdx = (i32)Game_RandU16(0x38);

    if (nIdx >= pCtx->nSiteCount) {
        /* Deviation: the original never bounds the roll against its 56 records. */
        nIdx = pCtx->nSiteCount - 1;
    }
    if (bKeepIdx) {
        pUfo->nTargetIdx = nIdx;
    }
    pUfo->nTargetX = pCtx->apSites[nIdx].x;
    pUfo->nTargetY = pCtx->apSites[nIdx].y;
}

/* Gate 2: teleport onto the target when it lies inside the current viewport, then set the dwell. */
static void MissionDwell(StrategItem *pUfo, const SpawnMissionCtx *pCtx, u8 bSub)
{
    if (Abs32(pUfo->nTargetX) <= pCtx->pView->nMapW &&
        Abs32(pUfo->nTargetY) <= pCtx->pView->nMapH) {
        pUfo->nX = pUfo->nTargetX;
        pUfo->nY = pUfo->nTargetY;
    }
    pUfo->nTimer = g_anMissionDwell[bSub];
    pUfo->nPhase = 2;
}

/* Subtypes 4 and 5 both stamp the strateg item they just created with the UFO's race and target
 * index. The original re-reads the list count before each store; one read is enough here. */
static void MissionTagLastItem(StrategItem *pUfo, const SpawnMissionCtx *pCtx)
{
    StrategItem *pSite;
    i32 nCount = PtrArray_GetCount(pCtx->pList);

    if (nCount <= 0) {
        return;
    }
    pSite = (StrategItem *)PtrArray_Get(pCtx->pList, nCount - 1);
    if (pSite == NULL) {
        return;
    }
    pSite->bRace = pUfo->bRace;            /* 0x7EB48, byte +112 */
    pSite->nTargetIdx = pUfo->nTargetIdx;  /* 0x7EB64, dword +33 */
}

/* Gate 3's landing effect, which only runs at nPhase 2. */
static void MissionLanding(StrategItem *pUfo, const SpawnMissionCtx *pCtx, u8 bSub)
{
    if (pUfo->nPhase != 2) {
        return;
    }

    if (bSub == 4) {
        i32 nRoll = (i32)Game_RandU16(0x14) + 1;

        if (nRoll <= 16) {
            StrategList_AddAlienSiteIfValid(pCtx->pList, pCtx->pnSeed, pCtx->pView->nMode,
                                            pCtx->pTilemap, pUfo->nX, pUfo->nY, 0);
        } else {
            StrategList_AddAlienSiteIfValid(pCtx->pList, pCtx->pnSeed, pCtx->pView->nMode,
                                            pCtx->pTilemap, pUfo->nX, pUfo->nY, 1);
            if (pCtx->hooks.pfnAppendLine != NULL) {
                pCtx->hooks.pfnAppendLine(60, PtrArray_GetCount(pCtx->pList) - 1, 0,
                                          pCtx->hooks.pUser);
            }
            pCtx->pWorld->pTime->bPaused = 1; /* GameTime_Pause @ 0x5d6ac, 0x7EB14 */
        }
        MissionTagLastItem(pUfo, pCtx);
        return;
    }

    if (bSub == 5) {
        if (pCtx->hooks.pfnAddIntercept != NULL) {
            pCtx->hooks.pfnAddIntercept(pUfo->bCraftClass, pCtx->hooks.pUser);
        }
        MissionTagLastItem(pUfo, pCtx);
        if (pCtx->hooks.pfnAppendLine != NULL) {
            pCtx->hooks.pfnAppendLine(16, pUfo->nTargetIdx, 0, pCtx->hooks.pUser);
        }
        if (pCtx->pnRadarPulseTimer != NULL) {
            *pCtx->pnRadarPulseTimer = 250; /* 0x7ED34 */
        }
        pCtx->pWorld->pTime->bPaused = 1; /* 0x7ED3C */
        return;
    }

    if (bSub == 6 && pCtx->hooks.pfnContactScreen != NULL) {
        pCtx->hooks.pfnContactScreen(pUfo, pCtx->hooks.pUser);
    }
}

/* Gates 2 and 3, shared by every subtype that reaches them. */
static void MissionPhaseGates(StrategItem *pUfo, const SpawnMissionCtx *pCtx, u8 bSub)
{
    if (pUfo->nPhase == 3) {
        MissionDwell(pUfo, pCtx, bSub);
    }
    if (pUfo->nPhase < 3 && pUfo->nTimer == 0) {
        MissionLanding(pUfo, pCtx, bSub);
        MissionRollTravel(pUfo, pCtx, bSub);
    }
}

/* Subtype 6 wants a base with NO free cell of facility kind 10. It picks a base, and if that one
 * has a free cell it tries again, up to 20 times. On the twentieth attempt it gives up on bases
 * entirely and travels to a random point instead. */
static bool MissionPickBaseTarget(StrategItem *pUfo, const SpawnMissionCtx *pCtx)
{
    i32 nAttempt = 0;

    for (;;) {
        BaseRec *pBase;
        i32 nBase = (i32)Game_RandU16((u16)(i8)pCtx->nBaseCount);

        if (nBase < 0 || nBase >= pCtx->nBaseCount) {
            return false; /* deviation: the original indexes apBases with no bound */
        }
        pBase = pCtx->apBases[nBase];
        if (pBase == NULL) {
            return false;
        }

        pUfo->nTargetIdx = nBase;
        pUfo->nTargetX = pBase->nUnk8;
        pUfo->nTargetY = pBase->nUnk12;

        if (Base_FindFacilityCell(pCtx->abFacilityGrid, nBase, 10) == 0) {
            return true;
        }
        ++nAttempt;
        if (nAttempt == 20) {
            return false;
        }
    }
}

/* Subtype 7 targets a random ACTIVE type-0 item. The original collects candidate indices into a
 * 100-entry stack array, so a list with more than 100 candidates silently truncates. */
static bool MissionPickActiveSiteTarget(StrategItem *pUfo, const SpawnMissionCtx *pCtx)
{
    i32 aCandidates[100];
    i32 nFound = 0;
    i32 nLimit = 100;
    i32 nPick;
    StrategItem *pTarget;
    i32 i;

    for (i = 0; i < PtrArray_GetCount(pCtx->pList); ++i) {
        StrategItem *pItem = (StrategItem *)PtrArray_Get(pCtx->pList, i);

        if (pItem != NULL && pItem->nType == 0 && pItem->bActive == 1) {
            if (nFound < nLimit) {
                aCandidates[nFound] = i;
            }
            ++nFound;
        }
    }

    if (nFound == 0) {
        return false;
    }
    if (nFound < nLimit) {
        nLimit = nFound;
    }

    nPick = aCandidates[Game_RandU16((u16)nLimit)];
    pTarget = (StrategItem *)PtrArray_Get(pCtx->pList, nPick);
    if (pTarget == NULL) {
        return false;
    }

    pUfo->nTargetIdx = nPick;
    pUfo->nTargetX = pTarget->nX;
    pUfo->nTargetY = pTarget->nY;
    return true;
}

i32 Geoscape_SpawnMissionSite(StrategItem *pUfo, i32 nMissionSub, const SpawnMissionCtx *pCtx)
{
    u8 bSub;
    i32 nResult;

    if (pUfo == NULL || pCtx == NULL || pCtx->pList == NULL || pCtx->pView == NULL ||
        pCtx->pTilemap == NULL || pCtx->apSites == NULL || pCtx->pWorld == NULL ||
        pCtx->pWorld->pTime == NULL) {
        return 0;
    }

    /* ---- start a subtype: the argument is 0..7 */
    if (nMissionSub >= 0 && nMissionSub <= 7) {
        MissionRollTravel(pUfo, pCtx, nMissionSub);
        pUfo->nMissionSub = (u8)nMissionSub;
        pUfo->nPhase = (nMissionSub == 0) ? 3 : 4;
        pUfo->nTargetX = 0;
        pUfo->nTargetY = 0;
        return 0; /* the original returns StrategEntry_InitTravel's value */
    }

    if (nMissionSub != -1) {
        return nMissionSub + 1; /* 0x7E4B0 with nothing done */
    }

    /* ---- advance the mission already stored in the item */
    bSub = pUfo->nMissionSub;
    nResult = (i32)(char)pUfo->nMissionSub; /* 0x7E4EC */

    switch (bSub) {
    case 0:
        MissionRollTravel(pUfo, pCtx, 0);
        nResult = pUfo->nPhase;
        if (nResult > 0) {
            pUfo->nPhase = nResult - 1; /* this path skips the nTimer test the tail applies */
        }
        goto LABEL_102;

    case 1:
    case 2:
    case 4:
        if (pUfo->nPhase > 3) {
            MissionPickLandTarget(pUfo, pCtx);
            MissionTravelToTarget(pUfo, pCtx, bSub);
        }
        MissionPhaseGates(pUfo, pCtx, bSub);
        goto LABEL_99;

    case 3:
    case 5:
        if (pUfo->nPhase > 3) {
            MissionPickSiteTarget(pUfo, pCtx, bSub == 5);
            MissionTravelToTarget(pUfo, pCtx, bSub);
        }
        MissionPhaseGates(pUfo, pCtx, bSub);
        goto LABEL_99;

    case 6:
        if (pUfo->nPhase > 3) {
            if (MissionPickBaseTarget(pUfo, pCtx)) {
                MissionTravelToTarget(pUfo, pCtx, 6);
            } else {
                MissionRollTravel(pUfo, pCtx, 0);
                return pUfo->nPhase; /* the give-up path returns before the gates */
            }
        }
        MissionPhaseGates(pUfo, pCtx, bSub);
        goto LABEL_99;

    case 7:
        if (pUfo->nPhase > 3) {
            if (MissionPickActiveSiteTarget(pUfo, pCtx)) {
                MissionTravelToTarget(pUfo, pCtx, 7);
            } else {
                MissionRollTravel(pUfo, pCtx, 0);
            }
        }
        MissionPhaseGates(pUfo, pCtx, bSub);
        goto LABEL_99;

    default:
        goto LABEL_102; /* an unknown stored subtype skips the decrement entirely */
    }

LABEL_99:
    /* The returned value is nPhase as it was BEFORE the decrement. */
    nResult = pUfo->nPhase;
    if (nResult > 0 && pUfo->nTimer == 0) {
        pUfo->nPhase = nResult - 1;
    }

LABEL_102:
    if (pUfo->nPhase == 0 && pUfo->bVisible == 0) {
        pUfo->bDone = 1;
    }
    return nResult;
}

/* Smap_GetTileAtMode @ 0x821c4. Read one byte of the OVERLAY layer of the tile set the current
 * zoom level selects -- not the terrain layer Smap_GetTileAtScreen reads. The three globals
 * 0x186C28, 0x186C1C and 0x186BB8 are the third PadImage of each zoom set.
 *
 * The x coordinate is offset by a global origin (0x186C20) before the lookup, and the stride is
 * the PadImage's own uWidth, not the viewport's nMapW.
 *
 * Deviation: for a mode outside 0..2 the original returns an uninitialised stack byte. The port
 * returns 0. */
u8 Smap_GetOverlayAtMode(const SmapView *pView, const PadImage *const aOverlay[3],
                         i32 nOriginX, i32 nX, i32 nY)
{
    if (pView == NULL || pView->nMode < 0 || pView->nMode > 2 || aOverlay == NULL) {
        return 0;
    }

    return Tilemap_GetAt(aOverlay[pView->nMode], nX - nOriginX, nY); /* 0x821D0, 0x82214 */
}
