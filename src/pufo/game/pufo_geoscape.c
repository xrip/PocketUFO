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
