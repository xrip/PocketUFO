/* pufo_geoscape.c - see pufo_geoscape.h for the evidence map. */
#include "game/pufo_geoscape.h"
#include "util/pufo_rand.h"

static i32 Abs32(i32 n)
{
    return n < 0 ? -n : n;
}

void Geoscape_TickShips(GeoscapeState *pState, const GeoscapeHooks *pHooks)
{
    GeoscapeWorld *pWorld = &pState->world;

    /* The pause byte is one global in the original (apLines[106]); the tick
     * owns the clock, so it hands the pointer to the contact layer. */
    pWorld->pTime = &pState->time;

    for (i32 i = 0; i < pWorld->nShipCount; ++i)
        Ship_TickRangeRecover(pWorld->apCraft[i]);

    StrategHooks detectHooks = { pHooks->pfnContactMsg,
                                 pHooks->pfnFindBaseBySiteId, pHooks->pUser };

    /* SLOBYTE(apLines[118]) % 2 == 0 */
    if ((i8)pWorld->nMinuteTicks % 2 == 0)
        StrategList_UpdateDetect(pState->pList, pWorld, &detectHooks);

    StrategList_UpdateMotion(pState->pList, pWorld);

    if (pState->time.bPaused == 0) {
        pHooks->pfnProcessEvents(0, pHooks->pUser);

        /* LOBYTE increment on the byte at apLines[118]. */
        pWorld->nMinuteTicks = (i8)(u8)(pWorld->nMinuteTicks + 1);
        if ((i8)pWorld->nMinuteTicks % 5 == 0)
            Ship_ClearExpiredIntercepts(pWorld, pState->pList, &detectHooks);

        for (i32 i = 0; i < pWorld->nShipCount; ++i) {
            Ship *pShip = pWorld->apCraft[i];
            if (pShip->bAtBase == 0
                && pHooks->pfnCanInterceptReach(i, pHooks->pUser)
                && pShip->bContactMsg == 0) {
                pShip->bContactMsg = 1;
                pState->nSelectedShipIdx = i;
                pState->nSelectedShipAux = i;
                pHooks->pfnContactMsg(15, i, 0, pHooks->pUser);
                pState->time.bPaused = 1; /* GameTime_Pause @ 0x5d6ac, 0x5d664 */
            }
        }
    }
}

/* The low-supply message is AppendLine(43, home base, slot). */
typedef struct SupplyUser {
    const GeoscapeHooks *pHooks;
} SupplyUser;

static void Supply_LowMsg(i32 nHomeBase, i32 nSlotIdx, void *pUser)
{
    SupplyUser *p = (SupplyUser *)pUser;
    p->pHooks->pfnContactMsg(43, nHomeBase, nSlotIdx, p->pHooks->pUser);
}

void Geoscape_TickHourly(GeoscapeState *pState, const GeoscapeHooks *pHooks)
{
    SupplyUser supplyUser = { pHooks };
    StrategList *pList = pState->pList;
    GeoscapeWorld *pWorld = &pState->world;

    /* Missions that drifted past the map edge are teleported back and
     * re-seeded: x/y = 150/250, then SpawnMissionSite(sub 0). */
    for (i32 i = 0; i < pList->nCount; ++i) {
        StrategItem *pItem = pList->apItems[i];
        if (pItem->nType == 1 && pItem->bActive == 1
            && (Abs32(pItem->nX) > 1000 || Abs32(pItem->nY) > 1000)) {
            pItem->nX = 150;
            pItem->nY = 250;
            pHooks->pfnSpawnMissionSite(pItem, 0, pHooks->pUser);
        }
    }

    ShipSupplyHooks supply = {
        pState->apBases, pState->pSupplyItem, &pState->bLowSupplyFlag,
        Supply_LowMsg, &supplyUser
    };
    for (i32 i = 0; i < pWorld->nShipCount; ++i) {
        Ship_TickRefuelAtBase(pWorld->apCraft[i]);
        Ship_TickRangeRecoverConsume(pWorld->apCraft[i], &supply);
    }

    pHooks->pfnGrantSiteRecovery(pHooks->pUser);
    pHooks->pfnRefreshBaseTabs(pHooks->pUser);
    pHooks->pfnAccumulateResearch(pHooks->pUser);

    if (StrategList_CountActive(pList) < 2) {
        if (Game_RandU16(4u) == 1u)
            pHooks->pfnProcessEvents(1, pHooks->pUser);
    }

    for (i32 b = 0; b < pState->nBaseCount; ++b)
        pState->apEmptyFacilityCells[b] =
            pHooks->pfnCountEmptyFacilityCells(b, pHooks->pUser);
}

void Geoscape_TickBases(GeoscapeState *pState, const GeoscapeHooks *pHooks)
{
    if (pState->nDaysSinceStart < 200u)
        ++pState->nDaysSinceStart;

    pHooks->pfnTickFacilityConstruction(pHooks->pUser);

    for (i32 b = 0; b < pState->nBaseCount; ++b)
        pState->world.apSiteState[b] =
            (u8)pHooks->pfnClearFacilityGridRow(b, pHooks->pUser);

    for (i32 s = 0; s < pState->nSoldierCount; ++s) {
        Soldier *pS = pState->apSoldiers[s];
        if ((i8)pS->bUnk423 > 0) {
            ++pS->auStat[6];
            --pS->bUnk423;
        }
        if (pS->bUnk247 != 0)
            --pS->bUnk247;
        if (pS->bUnk247 == 0 && pS->bUnk183 == 254)
            pS->bUnk183 = 255; /* the original writes -1 into the byte */
    }
}

/* Adapter so Geoscape_AdvanceTime can call the three ticks. */
typedef struct TickBundle {
    GeoscapeState *pState;
    const GeoscapeHooks *pHooks;
} TickBundle;

static void Tick_Ships(void *pUser)
{
    TickBundle *p = (TickBundle *)pUser;
    Geoscape_TickShips(p->pState, p->pHooks);
}

static void Tick_Hourly(void *pUser)
{
    TickBundle *p = (TickBundle *)pUser;
    Geoscape_TickHourly(p->pState, p->pHooks);
}

static void Tick_Bases(void *pUser)
{
    TickBundle *p = (TickBundle *)pUser;
    Geoscape_TickBases(p->pState, p->pHooks);
}

static void Tick_Monthly(void *pUser)
{
    TickBundle *p = (TickBundle *)pUser;
    p->pHooks->pfnOpenMonthly(p->pHooks->pUser);
}

void Geoscape_Advance(GeoscapeState *pState, const GeoscapeHooks *pHooks,
                      i32 nDeltaSec)
{
    TickBundle bundle = { pState, pHooks };
    GeoscapeClockHooks clockHooks = {
        Tick_Ships, Tick_Hourly, Tick_Bases, Tick_Monthly, &bundle
    };
    Geoscape_AdvanceTime(&pState->time, nDeltaSec, &clockHooks);
}

/* --------------------------------------------------------------------------
 * Small strategic accessors.
 * -------------------------------------------------------------------------- */

i32 Base_GetItemQty(const ObjectRecFull *pItem, i32 nBaseIdx)
{
    if (pItem == NULL || nBaseIdx < 0 ||
        nBaseIdx >= (i32)(sizeof(pItem->auPerBaseVal) / sizeof(pItem->auPerBaseVal[0]))) {
        return 0; /* the original reads past the array here; the port does not */
    }
    return (i32)pItem->auPerBaseVal[nBaseIdx];
}

static bool Geoscape_PanelApplies(i32 nScreenMode)
{
    /* 0x8b45c / 0x8b48c: only the two screens 0 and 2 arm these flags. */
    return nScreenMode == 0 || nScreenMode == 2;
}

bool Geoscape_SetPanelFlagA(i32 nScreenMode, GeoscapePanelFlags *pFlags)
{
    if (pFlags == NULL || !Geoscape_PanelApplies(nScreenMode)) {
        return false;
    }
    pFlags->bPanelA = 1; /* unk_1864F5, 0x8b468 */
    return true;
}

bool Geoscape_SetPanelFlagB(i32 nScreenMode, GeoscapePanelFlags *pFlags)
{
    if (pFlags == NULL || !Geoscape_PanelApplies(nScreenMode)) {
        return false;
    }
    pFlags->bPanelB = 1; /* unk_1864F7, 0x8b498 */
    return true;
}

i32 Base_FindIndexByTypeByte(BaseRec *const *apBases, i32 nBaseCount, u8 nType)
{
    if (apBases == NULL || nBaseCount <= 0) {
        return -1; /* 0x6ac50: an empty base list answers -1 without searching */
    }
    for (i32 i = 0; i < nBaseCount; ++i) {
        if (apBases[i] == NULL) {
            continue;
        }
        if (apBases[i]->bUnk4 == nType) {
            return i;
        }
    }
    /* The original has no bound here and keeps walking until something matches. */
    return -1;
}

i32 BaseGrid_GetTypeByte(BaseRec *const *apBases, i32 nBaseCount, const u8 *abGrid,
                         u8 nType)
{
    const i32 nIdx = Base_FindIndexByTypeByte(apBases, nBaseCount, nType);

    if (nIdx < 0 || abGrid == NULL) {
        return 0;
    }
    return (i32)abGrid[nIdx]; /* 0x6ace4: gUiRootPanel.anLineAux[94] + index */
}

i32 BaseGrid_SetTypeByte(BaseRec *const *apBases, i32 nBaseCount, u8 *abGrid,
                         u8 nType, i8 bValue)
{
    const i32 nIdx = Base_FindIndexByTypeByte(apBases, nBaseCount, nType);

    if (nIdx < 0 || abGrid == NULL) {
        return nIdx; /* 0x6acb8: the found index is the return value, -1 writes nothing */
    }
    abGrid[nIdx] = (u8)bValue;
    return nIdx;
}

void BaseRec_AddItemQty(BaseRec *pBase, ObjectRecFull *pItem, i32 nDelta)
{
    i32 nSlot;

    if (pBase == NULL || pItem == NULL) {
        return;
    }
    nSlot = (i8)pBase->bSlot; /* 0x55708 widens the slot byte as a char */
    if (nSlot < 0 ||
        nSlot >= (i32)(sizeof(pItem->auPerBaseVal) / sizeof(pItem->auPerBaseVal[0]))) {
        return;
    }
    /* Neither add clamps, so a negative delta can drive both below zero. Reproduced. */
    pItem->auPerBaseVal[nSlot] = (u16)((i32)pItem->auPerBaseVal[nSlot] + nDelta);
    pBase->nStoresTotal += nDelta; /* 0x55714 */
}

/* Base_ClearFacilityGridRow @ 0x6a0e0. Despite the name it clears nothing; see the
 * header for the three passes and what state byte each one produces. */
i32 Base_ClearFacilityGridRow(u8 *abFacilityGrid, i32 nBaseIndex, u8 *apSiteState)
{
    const u8 *pType;
    const u8 *pOcc;
    i8 nState = 0;
    i32 nRow;

    if (abFacilityGrid == NULL || apSiteState == NULL) {
        return 0;
    }

    pType = &abFacilityGrid[480 + 144 * nBaseIndex];
    pOcc  = &abFacilityGrid[36 * nBaseIndex];

    for (nRow = 0; nRow < 6; ++nRow) { /* 0x6A0EC: any free type-4 slot */
        i32 nCol;
        for (nCol = 0; nCol < 6; ++nCol) {
            if (pType[6 * nCol + nRow] == 4 && pOcc[6 * nCol + nRow] == 0) {
                nState = 1;
            }
        }
    }
    for (nRow = 0; nRow < 6; ++nRow) { /* 0x6A154: any free type-5 slot, overwrites the 1 */
        i32 nCol;
        for (nCol = 0; nCol < 6; ++nCol) {
            if (pType[6 * nCol + nRow] == 5 && pOcc[6 * nCol + nRow] == 0) {
                nState = 2;
            }
        }
    }
    for (nRow = 0; nRow < 6; ++nRow) { /* 0x6A1B0: a free type-9 slot, once */
        i32 nCol;
        for (nCol = 0; nCol < 6; ++nCol) {
            if (pType[6 * nCol + nRow] == 9 && pOcc[6 * nCol + nRow] == 0) {
                nState = (i8)(nState + 10);
                goto store; /* 0x6A214: exits both loops */
            }
        }
    }

store:
    apSiteState[nBaseIndex] = (u8)nState; /* 0x6A218, unk_161F38 */
    return nState;
}

/* Base_CalcWorkshopCap @ 0x65894. Counts free workshop cells and returns count * 50. */
i32 Base_CalcWorkshopCap(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    const u8 *pType;
    const u8 *pOcc;
    i32 nCap = 0;
    i32 nRow;

    if (abFacilityGrid == NULL) {
        return 0;
    }

    pType = &abFacilityGrid[480 + 144 * nBaseIndex];
    pOcc  = &abFacilityGrid[36 * nBaseIndex];

    for (nRow = 0; nRow < 6; ++nRow) {
        i32 nCol;
        for (nCol = 0; nCol < 6; ++nCol) {
            /* 0x658D0 tests the type, 0x658F0 the occupancy, and adds 50 to the total. */
            if (pType[6 * nCol + nRow] == 3 && pOcc[6 * nCol + nRow] == 0) {
                nCap += 50;
            }
        }
    }
    return nCap; /* 0x6590C */
}

/* Base_CountActiveManuf @ 0x68264. Sums the first nEntries u16 entries of one base's
 * manufacturing quantity column. See the header for where that column sits. */
i32 Base_CountActiveManuf(const u16 *apManufQty, i32 nEntries)
{
    i32 nTotal = 0;
    i32 i;

    if (apManufQty == NULL || nEntries <= 0) {
        return 0; /* 0x68280: a zero count never touches the array */
    }
    if (nEntries > 255) {
        nEntries = 255; /* the original reads the count as an unsigned byte */
    }

    for (i = 0; i < nEntries; ++i) {
        nTotal += apManufQty[i]; /* 0x682A4 reads a u16, 0x682AC advances by 2 */
    }
    return nTotal; /* 0x682BC */
}
