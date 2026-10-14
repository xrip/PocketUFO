/* pufo_geoscape.c - see pufo_geoscape.h for the evidence map. */
#include "game/pufo_geoscape.h"
#include "audio/pufo_audio.h" /* Sound_Play, the click the speed buttons play */
#include "game/pufo_save.h"   /* SmapSite, for the monthly upkeep of a base */
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

/* BaseGrid_SubByte0x654 @ 0x6acec. Subtract from one base's grid byte, found by type byte.
 *
 * Two things the original does that are easy to miss. It only subtracts when the byte is
 * already non-zero -- so a zero stays zero -- but there is no floor, so a positive byte can go
 * negative. And it does not check the found index: Base_FindIndexByTypeByte answers -1 when no
 * base has the type, and 0x6AD1C then reads and writes abGrid[-1]. The port refuses.
 *
 * The return value is the constant 1620, which is register residue, not a status. The port
 * returns the found index instead, which is at least something a caller could use. */
i32 BaseGrid_SubTypeByte(BaseRec *const *apBases, i32 nBaseCount, u8 *abGrid,
                         u8 nType, i8 nAmount)
{
    const i32 nIdx = Base_FindIndexByTypeByte(apBases, nBaseCount, nType);

    if (nIdx < 0 || abGrid == NULL) {
        return nIdx; /* deviation: the original reads and writes abGrid[-1] here */
    }

    if (abGrid[nIdx] != 0) { /* 0x6AD1C */
        abGrid[nIdx] = (u8)(abGrid[nIdx] - nAmount); /* 0x6AD24: no floor, can go negative */
    }

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

/* aFacBuildSpec @ 0xAA8F8, 18 records of three bytes: the tileA byte, the tileB byte and the
 * build days. The days byte is read with LDRSB at 0x623EC, so it is signed. Record 11 is 0xFF,
 * 0xFF, 19 and is never used as tiles: type 11 is the hangar and takes the 2 by 2 path instead. */
typedef struct FacBuildSpec {
    u8 bTileA;
    u8 bTileB;
    i8 nBuildDays;
} FacBuildSpec;

static const FacBuildSpec g_aFacBuildSpec[PUFO_FAC_SPEC_COUNT] = {
    { 0x02, 0x11, 0x00 }, { 0x01, 0x12, 0x10 }, { 0x01, 0x13, 0x1A }, { 0x01, 0x14, 0x20 },
    { 0x02, 0x15, 0x0C }, { 0x01, 0x16, 0x19 }, { 0x01, 0x18, 0x0A }, { 0x01, 0x19, 0x12 },
    { 0x01, 0x1F, 0x18 }, { 0x02, 0x20, 0x1A }, { 0x02, 0x1E, 0x1E }, { 0xFF, 0xFF, 0x19 },
    { 0x02, 0x1D, 0x26 }, { 0x02, 0x17, 0x10 }, { 0x03, 0x1A, 0x18 }, { 0x03, 0x1B, 0x22 },
    { 0x03, 0x1C, 0x22 }, { 0x00, 0x00, 0x00 },
};

/* BaseFac_SetCellBuildState @ 0x623bc. The header has the two row bases and the hangar case.
 *
 * The offsets below are the ARM's own: 0x159768 is buildDays and 0x159768 + 0x174 is fac[], and
 * every store is (layer base) + 6*(row + k) + col, which the ARM forms as a MUL then an ADD. The
 * +1 and +7 stores are the same row one column right and the diagonal cell one row down. */
i32 BaseFac_SetCellBuildState(u8 *abFacilityGrid, i32 nBaseIndex, u8 nFacType,
                              i32 nX, i8 nY, u8 bClear)
{
    const i32 nFacRow  = 24 * nBaseIndex + nY; /* 0x62418 and 0x624A8 */
    const i32 nDaysRow = 6 * nBaseIndex + nY;  /* 0x62468 and 0x624E4 */
    const i32 nFac     = 0x174 + 6 * nFacRow + nX;
    const i32 nDays    = 6 * nDaysRow + nX;
    i8 nBuildDays;

    if (abFacilityGrid == NULL || nBaseIndex < 0 || nBaseIndex >= PUFO_FAC_BASE_COUNT
        || nFacType >= PUFO_FAC_SPEC_COUNT || nX < 0 || nX >= 6 || nY < 0 || nY >= 6) {
        return 0; /* deviation: the original bounds none of these */
    }

    nBuildDays = (bClear != 0) ? 0 : g_aFacBuildSpec[nFacType].nBuildDays;

    if (nFacType == PUFO_FAC_TYPE_HANGAR) {
        abFacilityGrid[nFac]     = 9;  /* 0x62438 */
        abFacilityGrid[nFac + 1] = 10; /* 0x62444 */
        abFacilityGrid[nFac + 6] = 11; /* 0x62458: 6*(row+1) + col */
        abFacilityGrid[nFac + 7] = 12; /* 0x62460: the diagonal quadrant */

        abFacilityGrid[nDays]     = (u8)nBuildDays; /* 0x6247C */
        abFacilityGrid[nDays + 1] = (u8)nBuildDays; /* 0x62480 */
        abFacilityGrid[nDays + 7] = (u8)nBuildDays; /* 0x62484: (nY+1,nX) is left alone */
    } else {
        abFacilityGrid[nFac]      = g_aFacBuildSpec[nFacType].bTileA; /* 0x624C4 */
        abFacilityGrid[nFac + 36] = g_aFacBuildSpec[nFacType].bTileB; /* 0x624DC: 6*(row+6) */
    }

    abFacilityGrid[nDays] = (u8)nBuildDays; /* 0x624F0, the tail store both paths share */
    abFacilityGrid[0x174 + 6 * (nFacRow + 18) + nX] = nFacType; /* 0x62504, anchor cell only */

    return 6 * (nFacRow + 18); /* 0x624F8: the facType address delta, returned as residue */
}

/* byte_A6568 @ 0xA6568: the placement code a facility type earns in the tactical tile-type grid.
 * Index 11 (the hangar) maps to 20 - the empty sentinel - because the hangar's own 2x2 stamp
 * (16, 17, 18, 19) overwrites the anchor right after. Facility types run 0..17 plus the 0xFF
 * sentinel, so entries 18..23 (zero) and 24..31 are never reached by Smap_InitCraftSelectState;
 * they are kept because the original's table is 32 bytes wide. */
const u8 g_aCraftPlacementCodeByFacType[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
    0x0E, 0x0D, 0x0F, 0x10, 0x0C, 0x06, 0x09, 0x0A,
    0x0B, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1C, 0x0E, 0x0F, 0x10, 0x12, 0x13, 0x14, 0x16
};

/* Smap_InitCraftSelectState @ 0x61b94. Build the 36-byte tactical tile-type grid (0x159D5C, read
 * by Map_BuildMissionTerrain at 0x22EA4 and Tact_DeployTeamAndHwps at 0x3A06C) for an assault on
 * base nBaseIndex.
 *
 * The grid starts as 36 copies of 20 ("empty"). For each cell of the base's 6x6 buildDays lane
 * (grid + 36*nBaseIndex, the same lane BaseFac_SetCellBuildState writes): a cell that is still
 * under construction (non-zero buildDays) contributes nothing; otherwise the facType layer byte
 * (lane offset +108 - the same 6*(row+18) offset BaseFac_SetCellBuildState returns) picks a
 * placement code through g_aCraftPlacementCodeByFacType, unless it is the 0xFF empty sentinel or
 * the grid cell was already stamped by an earlier hangar block (the `== 20` test).
 *
 * Facility 11 is the hangar: it stamps 16, 17, 18, 19 at byte offsets +0, +1, +6, +7 from the
 * anchor - the ARM's own displacements at 0x61C3C..0x61C60. Note these are byte offsets in a
 * 6-wide grid, so at column 5 the +1 wraps to the next row, exactly as the identical +1/+6/+7
 * arithmetic in BaseFac_SetCellBuildState does.
 *
 * Two deviations. The original reaches the tile grid through the facility grid base + 1524 (the
 * two blocks are adjacent globals 0x159768 and 0x159D5C); the port takes them as separate
 * pointers. And the stamp has no bound: an anchor at row 5 writes up to six bytes past the 36-byte
 * grid, into 0x159D80 - the base count byte. The port refuses the past-the-grid stores (the
 * original's own placement path bounds hangar anchors below row 5, so the spill is latent). The
 * original returns the grid base as register residue; the port returns void. */
void Smap_InitCraftSelectState(u8 *abFacilityGrid, u8 *abTileTypes, i32 nBaseIndex)
{
    for (i32 i = 0; i < 36; ++i) {
        abTileTypes[i] = 20; /* 0x61BB4 */
    }

    for (i32 nRow = 0; nRow < 6; ++nRow) {
        for (i32 nCol = 0; nCol < 6; ++nCol) {
            const i32 nCell = 6 * nRow + nCol;

            if (abFacilityGrid[36 * nBaseIndex + nCell] != 0) { /* 0x61BE8: buildDays lane */
                continue;
            }

            const u8 nFacType = abFacilityGrid[480 + 144 * nBaseIndex + nCell]; /* 0x61C0C */
            if (nFacType == 0xFF) { /* 0x61C20 */
                continue;
            }
            if (abTileTypes[nCell] != 20) { /* 0x61C20: already stamped */
                continue;
            }

            abTileTypes[nCell] = g_aCraftPlacementCodeByFacType[nFacType]; /* 0x61C28 */

            if (nFacType == PUFO_FAC_TYPE_HANGAR) { /* 0x61C34: the 2x2 stamp */
                abTileTypes[nCell] = 16; /* 0x61C3C */
                if (nCell + 1 < 36) { abTileTypes[nCell + 1] = 17; } /* 0x61C48 */
                if (nCell + 6 < 36) { abTileTypes[nCell + 6] = 18; } /* 0x61C54 */
                if (nCell + 7 < 36) { abTileTypes[nCell + 7] = 19; } /* 0x61C60 */
            }
        }
    }
}

/* Base_GrantStarterItems @ 0x95fa4. Four adds against one base, in the original's order. */
void Base_GrantStarterItems(BaseRec *pBase, ObjectRecFull *const *apObjects, u8 bGrant)
{
    if (bGrant == 0 || pBase == NULL || apObjects == NULL) {
        return; /* 0x95FB0: with the flag clear nothing is added at all */
    }
    BaseRec_AddItemQty(pBase, apObjects[105], 10); /* 0x95FCC, Map_apObjects105 */
    BaseRec_AddItemQty(pBase, apObjects[34], 2);   /* 0x95FE4, Map_apObjects34 */
    BaseRec_AddItemQty(pBase, apObjects[31], 1);   /* 0x95FFC, Map_apObjects31 */
    BaseRec_AddItemQty(pBase, apObjects[56], 33);  /* 0x96014, Map_apObjects56 */
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

/* The three facility passes the original carries as separate copies share one shape, so
 * the port keeps one implementation each and gives the originals their own names. Cell
 * kind is the only thing that differs between the workshop and lab versions.
 *
 * The original's own type names settle what the grids are. IDA carries BaseFacilityGrid,
 * size 1524, as buildDays u8[8][6][6] at +0x000, then fac BaseFacBlock[8] at +0x174; and
 * BaseFacBlock, size 144, as tileA, tileB, layer2 and facType, each a 36-byte
 * BaseFacLayer6. So the grid this port calls occupancy is the original's buildDays, and
 * "equals zero" means the cell is not under construction rather than that it is unowned. */
static i32 FacilityScoreKind(const u8 *abFacilityGrid, i32 nBaseIndex, u8 nKind, i32 nWeight,
                             bool bRequireFree)
{
    const u8 *pType;
    const u8 *pBuild;
    i32 nTotal = 0;
    i32 nRow;

    if (abFacilityGrid == NULL) {
        return 0;
    }

    pType  = &abFacilityGrid[480 + 144 * nBaseIndex]; /* fac[nBase].facType */
    pBuild = &abFacilityGrid[36 * nBaseIndex];        /* buildDays[nBase] */

    for (nRow = 0; nRow < 6; ++nRow) {
        i32 nCol;
        for (nCol = 0; nCol < 6; ++nCol) {
            /* The originals test the kind, then the build days, then add a weight. Only the
             * weight differs between the capacity passes and the site-score pass. */
            if (pType[6 * nCol + nRow] == nKind &&
                (!bRequireFree || pBuild[6 * nCol + nRow] == 0)) {
                nTotal += nWeight;
            }
        }
    }
    return nTotal;
}

/* Base_CalcWorkshopCap @ 0x65894. Counts free workshop cells and returns count * 50. */
i32 Base_CalcWorkshopCap(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, 3, 50, true); /* 0x6590C */
}

/* Base_CalcLabCap @ 0x65818. The same count over free lab cells. */
i32 Base_CalcLabCap(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, 2, 50, true); /* 0x65890 */
}

/* Base_FindFacilityCell @ 0x6ae54. Does this base have a free cell of facility kind nKind?
 *
 * It is the same 6 by 6 walk as the capacity passes -- cell kind at fac[nBase].facType, free
 * meaning buildDays is zero -- and it returns 1 at the first match and 0 after the last cell.
 * That is exactly a nonzero count, so the port reuses the shared walk with a weight of 1 rather
 * than writing the loop a fourth time.
 *
 * The original's arguments are (grid, kind, baseIndex); the port puts the base index second to
 * match every other function in this family. */
i32 Base_FindFacilityCell(const u8 *abFacilityGrid, i32 nBaseIndex, u8 nKind)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, nKind, 1, true) != 0 ? 1 : 0;
}

/* Base_CountEmptyFacilityCells @ 0x6aee0. The same walk with weights of 2 and 3 instead of
 * 50, summed over two kinds: free kind-4 cells count 2 each and free kind-5 cells count 3
 * each. Unlike the capacity passes this is not about a facility kind at all -- 4 and 5 are two
 * of the three states Base_ClearFacilityGridRow classifies, so this measures how much room a
 * base has left rather than what it can produce. */
i32 Base_CountEmptyFacilityCells(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, 4, 2, true) +
           FacilityScoreKind(abFacilityGrid, nBaseIndex, 5, 3, true); /* 0x6AF18..0x6AF9C */
}

/* Base_CalcLivingQuartersCap @ 0x6579c. Kind 1, 50 per free cell. */
i32 Base_CalcLivingQuartersCap(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, 1, 50, true); /* 0x657F8 */
}

/* Base_CalcStoresCapacity @ 0x65720. Kind 6, 300 per free cell. */
i32 Base_CalcStoresCapacity(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, 6, 300, true); /* 0x6577C */
}

/* Base_CalcAlienContainmentCap @ 0x65910. Kind 7, 10 per free cell. */
i32 Base_CalcAlienContainmentCap(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, 7, 10, true); /* 0x6596C */
}

/* Base_CalcHangarCap @ 0x6611c. Kind 11, one hangar slot per free cell. */
i32 Base_CalcHangarCap(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, 11, 1, true); /* 0x66178 */
}

/* Base_HasResearchSlotFree @ 0x662bc. Computes the whole lab capacity and returns
 * nNeeded <= capacity. It is Base_CalcLabCap with a threshold compare, not a search. */
bool Base_HasResearchSlotFree(const u8 *abFacilityGrid, i32 nBaseIndex, i32 nNeeded)
{
    return nNeeded <= Base_CalcLabCap(abFacilityGrid, nBaseIndex); /* 0x6633C */
}

/* Base_HasWorkshopSlotFree @ 0x66340. The same over workshop cells. */
bool Base_HasWorkshopSlotFree(const u8 *abFacilityGrid, i32 nBaseIndex, i32 nNeeded)
{
    return nNeeded <= Base_CalcWorkshopCap(abFacilityGrid, nBaseIndex); /* 0x663C0 */
}

/* Base_CountBuiltFacilities @ 0x682c8. The only member of the family that does not test build
 * days: kinds 13..16 are weighted 500, 600, 900 and 1500 whether or not they are finished. */
i32 Base_CountBuiltFacilities(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, 13, 500, false) +
           FacilityScoreKind(abFacilityGrid, nBaseIndex, 14, 600, false) +
           FacilityScoreKind(abFacilityGrid, nBaseIndex, 15, 900, false) +
           FacilityScoreKind(abFacilityGrid, nBaseIndex, 16, 1500, false); /* 0x68308..0x68328 */
}

static i32 QuantitySumColumn(const u16 *apColumn, i32 nEntries)
{
    i32 nTotal = 0;
    i32 i;

    if (apColumn == NULL || nEntries <= 0) {
        return 0; /* both functions skip the array entirely on a zero count */
    }
    if (nEntries > 255) {
        nEntries = 255; /* both read the count as an unsigned byte */
    }

    for (i = 0; i < nEntries; ++i) {
        nTotal += apColumn[i]; /* a u16 read, and the pointer advances by 2 */
    }
    return nTotal;
}

/* Base_CountActiveManuf @ 0x68264. Manufacturing quantity column. */
i32 Base_CountActiveManuf(const u16 *apManufQty, i32 nEntries)
{
    return QuantitySumColumn(apManufQty, nEntries); /* 0x682BC */
}

/* Base_CountActiveResearch @ 0x68198. Research quantity column, same count byte. */
i32 Base_CountActiveResearch(const u16 *apResearchQty, i32 nEntries)
{
    return QuantitySumColumn(apResearchQty, nEntries); /* 0x681EC */
}

/* The needed-marking pass, shared by the workshop and lab versions. It walks row 0 across
 * all six columns, then row 1, and so on, spending a budget of 1 per matching cell. */
static i32 FacilityMarkNeeded(u8 *abFacilityGrid, i32 nBaseIndex, u8 nKind, i32 nBudget)
{
    u8 *pType;
    u8 *pNeed;
    i32 nRow;
    i32 nCol = 0;

    if (abFacilityGrid == NULL) {
        return 0;
    }

    pType = &abFacilityGrid[480 + 144 * nBaseIndex]; /* fac[nBase].facType */
    pNeed = &abFacilityGrid[444 + 144 * nBaseIndex]; /* fac[nBase].layer2 */

    for (nRow = 0; nRow < 6; ++nRow) {
        nCol = 0; /* the counter these functions eventually return */
        do {
            if (pType[6 * nCol + nRow] == nKind) { /* only cells of this kind */
                u8 bNeed;
                if (nBudget <= 0) {
                    bNeed = 0;
                } else {
                    bNeed = 1;
                    --nBudget;
                }
                pNeed[6 * nCol + nRow] = bNeed;
            }
            ++nCol;
        } while (nCol < 6);
    }

    return nCol; /* always 6, the inner counter's final value */
}

/* A budget of one per nPer units, plus one, and nothing at all when the total is zero.
 * All five needed passes compute it the same way and only the divisor differs. */
static i32 FacilityBudget(i32 nTotal, i32 nPer)
{
    return (nTotal != 0) ? (nTotal / nPer + 1) : 0;
}

/* Base_UpdateWorkshopNeeded @ 0x65bb4. Marks the first N workshop cells as needed.
 * The original's call to Base_CalcWorkshopCap at 0x65BC0 discards a pure function's
 * result, so the port omits it; see the header. */
i32 Base_UpdateWorkshopNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nActiveManuf)
{
    return FacilityMarkNeeded(abFacilityGrid, nBaseIndex, 3,
                              FacilityBudget(nActiveManuf, 50)); /* 0x65C78 */
}

/* Base_UpdateLabNeeded @ 0x65c7c. The same pass over lab cells. It discards
 * Base_CalcLabCap at 0x65C88 for the same reason. */
i32 Base_UpdateLabNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nActiveResearch)
{
    return FacilityMarkNeeded(abFacilityGrid, nBaseIndex, 2,
                              FacilityBudget(nActiveResearch, 50)); /* 0x65D40 */
}

/* Base_UpdateLivingQuartersNeeded @ 0x65e24. Kind 1, divisor 50. It discards
 * Base_CalcLivingQuartersCap at 0x65E30, and its total is Base_CalcOccupiedSpace plus
 * Base_CountOutboundTransferSpace, neither of which is ported, so the sum arrives here. */
i32 Base_UpdateLivingQuartersNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nSpaceNeeded)
{
    return FacilityMarkNeeded(abFacilityGrid, nBaseIndex, 1,
                              FacilityBudget(nSpaceNeeded, 50)); /* 0x65F00 */
}

/* Base_UpdateStoresNeeded @ 0x65d44. Kind 6, and the only pass with a divisor of 300 rather
 * than 50, so storage needs six times as much pressure before a cell is marked. It discards
 * Base_CalcStoresCapacity at 0x65D50, and its total is Base_CalcStorageUsed plus
 * Base_CountItemQtyAcrossBases. */
i32 Base_UpdateStoresNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nStoresNeeded)
{
    return FacilityMarkNeeded(abFacilityGrid, nBaseIndex, 6,
                              FacilityBudget(nStoresNeeded, 300)); /* 0x65E20 */
}

/* Base_CalcStorageUsed @ 0x6555c. See the header for the index filter. The original's first guard
 * is `(int)p > (int)&Map`, which is true for every iteration because the running pointer starts
 * past the Map base, so the filter reduces to the four index ranges. */
i32 Base_CalcStorageUsed(ObjectRecFull *const *apObjects, i32 nObjectCount, i32 nBase)
{
    i32 nTotal = 0;

    /* DEVIATION: the original indexes auPerBaseVal with the char it was given and does not check
     * it, so an out-of-range base reads past the eight columns. The port returns 0 instead. */
    if (apObjects == NULL || nBase < 0 ||
        nBase >= (i32)(sizeof(((ObjectRecFull *)0)->auPerBaseVal) /
                     sizeof(((ObjectRecFull *)0)->auPerBaseVal[0]))) {
        return 0;
    }

    for (i32 nIdx = 1; nIdx < nObjectCount; ++nIdx) {
        bool bCounted = (nIdx < 70) || (nIdx > 104 && nIdx < 112) || (nIdx > 121 && nIdx < 125) ||
                        nIdx == 140; /* 0x655B8 */
        if (!bCounted || apObjects[nIdx] == NULL) {
            continue;
        }
        ObjectRecFull *pObj = apObjects[nIdx];
        if (pObj->auPerBaseVal[nBase] == 0) {
            continue; /* 0x655D4 */
        }
        nTotal += (i32)pObj->bCargoSize * (i32)pObj->auPerBaseVal[nBase]; /* 0x655DC */
    }
    return nTotal;
}

/* ---- Base_DemolishFacility @ 0x66b70 ------------------------------------------- */

/* Shift one per-base column left by one base, over the bases above the one being removed.
 * nEntries is the original's loop count, which is Map.nObjectCount for the object-shaped columns
 * and the shared byte at 0x160C4C for the research column. The columns themselves are 200 entries
 * wide, so when nObjectCount is larger the original runs on into the next column; that is what
 * this reproduces. */
static void ShiftColumnU16(u16 *apCol, i32 nFirstBase, i32 nLastBase, i32 nEntries, i32 nStride)
{
    for (i32 nBase = nFirstBase; nBase < nLastBase; ++nBase) {
        for (i32 k = 0; k < nEntries; ++k) {
            apCol[nStride * nBase + k] = apCol[nStride * (nBase + 1) + k];
        }
    }
}

i32 Base_DemolishFacility(const BaseDemolishEnv *pEnv, i8 nFacType)
{
    if (pEnv == NULL || pEnv->pbBaseCount == NULL || pEnv->apSiteRecs == NULL ||
        pEnv->abFacilityGrid == NULL) {
        return 0;
    }

    i32 nCount = (i32)(i8)*pEnv->pbBaseCount; /* 0x66B90, the signed byte at 0x159D80 */

    /* 1. Which base, by facility type. 0x66BA4. A miss leaves the index on the count. */
    i32 nBase = 0;
    while (nBase < nCount && pEnv->apSiteRecs[nBase].nFacType != (i32)nFacType) {
        ++nBase;
    }

    /* 2. The strateg entry, and here the same argument means a SITE ID. 0x66BE4. An unmatched
     * search leaves nStrat on the count, and the original calls the removal anyway. */
    if (pEnv->pShipEnv != NULL && pEnv->pShipEnv->pStrategList != NULL) {
        i32 nStrat = 0;
        i32 nStratCount = PtrArray_GetCount(pEnv->pShipEnv->pStrategList);
        while (nStrat < nStratCount) {
            StrategItem *pItem = (StrategItem *)PtrArray_Get(pEnv->pShipEnv->pStrategList, nStrat);
            if (pItem != NULL && pItem->nSiteId == (i32)nFacType && pItem->nType == 0 &&
                pItem->bActive == 0) {
                break;
            }
            ++nStrat;
        }
        (void)StrategList_RemoveAt(pEnv->pShipEnv->pStrategList, pEnv->pShipEnv->pWorld, nStrat,
                                   pEnv->pShipEnv->pnSpawnOnceFlag, pEnv->pShipEnv->pSpawnCtx);
    }

    /* 3. Delete the soldiers stationed here. 0x66CB0. nKind is the base index and bUnk183 above
     * 0xFD means the soldier is not aboard a craft. The loop does not advance on a delete, so it
     * re-tests the slot the removal moved into, which is what makes the cascade work. */
    if (pEnv->apSoldiers != NULL && pEnv->pnSoldierCount != NULL) {
        i32 nSoldier = 0;
        while (nSoldier < (i32)*pEnv->pnSoldierCount) {
            Soldier *pSol = pEnv->apSoldiers[nSoldier];
            if (pSol != NULL && pSol->nKind == nBase && pSol->bUnk183 > 0xFD) {
                nSoldier = 0; /* the original re-tests the same slot after a removal */
                *pEnv->pnSoldierCount =
                    (u16)Soldier_RemoveById(pEnv->apSoldiers, (i32)*pEnv->pnSoldierCount,
                                            pSol->nUnk440);
            } else {
                ++nSoldier;
            }
        }
    }

    /* 4. Remove every craft homed here. 0x66D18. The count is re-read through the removal because
     * the array shrinks under it. */
    if (pEnv->apCraft != NULL && pEnv->pnShipCount != NULL && pEnv->pShipEnv != NULL) {
        i32 nShip = 0;
        while (nShip < (i32)*pEnv->pnShipCount) {
            Ship *pShip = pEnv->apCraft[nShip];
            if (pShip != NULL && (i32)pShip->bHomeBase == nBase) {
                (void)Ship_RemoveWithCargo(pEnv->pShipEnv, (i32)pShip->bId);
            } else {
                ++nShip;
            }
        }
    }

    /* 5. Mark this base's map sites unassigned. 0x66D70. bFlag0 = -1 is the not-assigned marker. */
    if (pEnv->apSites != NULL) {
        for (i32 nSite = 0; nSite < pEnv->nSiteCount; ++nSite) {
            if ((i8)pEnv->apSites[nSite].bFlag1 == (i8)nFacType) {
                pEnv->apSites[nSite].bFlag0 = 0xFF;
            }
        }
    }

    /* 6. Renumber the home bases above it. 0x66DC0. */
    if (pEnv->apCraft != NULL && pEnv->pnShipCount != NULL) {
        for (i32 nShip = 0; nShip < (i32)*pEnv->pnShipCount; ++nShip) {
            Ship *pShip = pEnv->apCraft[nShip];
            if (pShip != NULL && (i32)pShip->bHomeBase > nBase) {
                pShip->bHomeBase = (u8)((i32)pShip->bHomeBase - 1);
            }
        }
    }

    /* 7. Renumber the base every soldier is stationed at. 0x66E20. */
    if (pEnv->apSoldiers != NULL && pEnv->pnSoldierCount != NULL) {
        for (i32 nSoldier = 0; nSoldier < (i32)*pEnv->pnSoldierCount; ++nSoldier) {
            Soldier *pSol = pEnv->apSoldiers[nSoldier];
            if (pSol != NULL && pSol->nKind > nBase) {
                pSol->nKind = pSol->nKind - 1;
            }
        }
    }

    i32 nNewCount = nCount - 1;

    /* 8. Compact every per-base array. 0x66E4C: only when the removed base is not the last one. */
    if (nBase < nCount - 1) {
        i32 nObjectCount = (pEnv->pnObjectCount != NULL) ? (i32)*pEnv->pnObjectCount : 0;

        for (i32 nSrc = nBase + 1; nSrc < nCount; ++nSrc) {
            i32 nDst = nSrc - 1;

            /* ObjectRec.auPerBaseVal, one u16 per object per base. 0x66F0C. */
            if (pEnv->apObjects != NULL) {
                for (int k = 0; k < nObjectCount; ++k) {
                    if (pEnv->apObjects[k] != NULL) {
                        pEnv->apObjects[k]->auPerBaseVal[nDst] = pEnv->apObjects[k]->auPerBaseVal[nSrc];
                    }
                }
            }

            /* The BaseRec records themselves, moved by 20 bytes through the pointers. 0x66F44. */
            if (pEnv->apBases != NULL && nDst < PUFO_BASE_FAC_COUNT && nSrc < PUFO_BASE_FAC_COUNT &&
                pEnv->apBases[nDst] != NULL && pEnv->apBases[nSrc] != NULL) {
                memmove(pEnv->apBases[nDst], pEnv->apBases[nSrc], sizeof(BaseRec));
            }

            /* The four 36-byte grids of the facility block, 144 bytes in all. 0x66F4C. */
            memmove(pEnv->abFacilityGrid + 372 + 144 * nDst,
                    pEnv->abFacilityGrid + 372 + 144 * nSrc, 144);

            /* buildDays, 36 bytes. 0x66FA8. */
            memmove(pEnv->abFacilityGrid + 36 * nDst, pEnv->abFacilityGrid + 36 * nSrc, 36);

            /* The research column, 144 u16 per base, gated on the shared count. 0x67008. */
            if (pEnv->apResearchCols != NULL && pEnv->pnResearchEntries != NULL) {
                for (i32 k = 0; k < (i32)*pEnv->pnResearchEntries; ++k) {
                    pEnv->apResearchCols[144 * nDst + k] = pEnv->apResearchCols[144 * nSrc + k];
                }
            }

            /* Product array C (u16) and B (u32). 0x67078 and 0x67090. */
            if (pEnv->apProductC != NULL) {
                for (i32 k = 0; k < nObjectCount; ++k) {
                    pEnv->apProductC[PUFO_BASE_COLUMN_ENTRIES * nDst + k] =
                        pEnv->apProductC[PUFO_BASE_COLUMN_ENTRIES * nSrc + k];
                }
            }
            if (pEnv->apProductB != NULL) {
                for (i32 k = 0; k < nObjectCount; ++k) {
                    pEnv->apProductB[PUFO_BASE_COLUMN_ENTRIES * nDst + k] =
                        pEnv->apProductB[PUFO_BASE_COLUMN_ENTRIES * nSrc + k];
                }
            }

            /* The 64-byte facility records. 0x670D4. */
            memmove(&pEnv->apSiteRecs[nDst], &pEnv->apSiteRecs[nSrc], sizeof(BaseSiteRec));
        }

        /* ORIGINAL BUG (0x67048), reproduced: the manufacturing quantity column is shifted one
         * base LATER than every other array. Its fixed offset 17156 lands on 0x15E19C with the loop
         * index itself, while product C at 12404 and product B at 5604 land one column earlier. So
         * column nBase keeps its value and column nBase+1 is overwritten from nBase+2. */
        if (pEnv->apManufCols != NULL) {
            ShiftColumnU16(pEnv->apManufCols, nBase + 1, nCount, nObjectCount,
                           PUFO_BASE_COLUMN_ENTRIES);
        }
    }

    /* 9. Drop the count, delete the pointer the shift left duplicated, renumber the slots. */
    *pEnv->pbBaseCount = (u8)nNewCount; /* 0x6714C */
    if (nNewCount >= 0 && nNewCount < PUFO_BASE_FAC_COUNT && pEnv->apBases != NULL &&
        pEnv->apBases[nNewCount] != NULL) {
        free(pEnv->apBases[nNewCount]); /* 0x6715C, operator delete */
        pEnv->apBases[nNewCount] = NULL;
    }

    for (i32 nSlot = 0; nSlot < nNewCount; ++nSlot) {
        if (pEnv->apBases != NULL && pEnv->apBases[nSlot] != NULL) {
            pEnv->apBases[nSlot]->bSlot = (u8)nSlot; /* 0x67180 */
        }
    }
    return nNewCount; /* the original returns the loop counter, which is the new count */
}

/* Base_UpdatePsiLabNeeded @ 0x65fc8. Kind 8, and the only pass with a divisor of 10: one
 * psi-soldier per lab cell is enough to claim it. It discards Base_CalcPsiLabCap at 0x65FD4
 * and takes its total from Base_CountPsiSoldiers. */
i32 Base_UpdatePsiLabNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nPsiSoldiers)
{
    return FacilityMarkNeeded(abFacilityGrid, nBaseIndex, 8,
                              FacilityBudget(nPsiSoldiers, 10)); /* 0x6608C */
}

/* Base_CountAmmoItems @ 0x656ac. */
i32 Base_CountAmmoItems(ObjectRecFull *const *apObjects, i32 nObjectCount, i32 nBaseIndex)
{
    i32 nTotal = 0;
    i8 nSlot = (i8)nBaseIndex;                    /* 0x656AC: the argument is a char */

    for (i32 i = 1; i < nObjectCount; ++i) {
        /* 0x656E8: 77..104 inclusive, plus object 114. */
        if ((i > 76 && i < 105) || i == 114) {
            if (apObjects[i] != NULL) {
                nTotal += apObjects[i]->auPerBaseVal[nSlot];
            }
        }
    }
    return nTotal;
}

/* Base_UpdateAlienContainmentNeeded @ 0x65f04.
 *
 * The original calls Base_CalcAlienContainmentCap at 0x65F10 and then overwrites the return
 * register with `MOV R0, R4` before calling Base_CountAmmoItems, so the capacity is computed and
 * discarded. It is pure, so the port does not call it. */
i32 Base_UpdateAlienContainmentNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nAmmoItems)
{
    return FacilityMarkNeeded(abFacilityGrid, nBaseIndex, 7,
                              FacilityBudget(nAmmoItems, 10));   /* 0x65F3C */
}

/* Base_CalcOccupiedSpace @ 0x655f8. */
i32 Base_CalcOccupiedSpace(Soldier *const *apSoldiers, i32 nSoldierCount,
                           ObjectRecFull *const *apObjects, i32 nObjectCount,
                           i32 nBaseIndex)
{
    i32 nUsed = 0;
    i8 nSlot = (i8)nBaseIndex;                    /* 0x655FC: the argument is a char */

    /* 0x6560C: the count is read as a u16, so a larger value wraps. */
    for (i32 i = 0; i < (i32)(u16)nSoldierCount; ++i) {
        if (apSoldiers[i] != NULL && (i32)apSoldiers[i]->nKind == (i32)nSlot) {
            ++nUsed;
        }
    }

    for (i32 i = 1; i < nObjectCount; ++i) {
        /* 0x65674: two compares against 0x70 and 0x71, not a range test. */
        if (i == 112 || i == 113) {
            if (apObjects[i] != NULL) {
                nUsed += apObjects[i]->auPerBaseVal[nSlot];
            }
        }
    }
    return nUsed;
}

/* Base_CountSoldiersAssigned @ 0x66230. Soldiers actually available at this base: their base
 * slot matches, they are not away in a craft (bUnk183 != 254), and either they are not aboard
 * anything (bUnk183 == 255) or the craft they are aboard is currently at base. */
i32 Base_CountSoldiersAssigned(Soldier *const *apSoldiers, i32 nSoldierCount,
                               Ship *const *apCraft, i32 nCraftCount, i32 nBaseIndex)
{
    i32 nCount = 0;

    if (apSoldiers == NULL) {
        return 0;
    }

    for (i32 i = 0; i < nSoldierCount; ++i) {
        const Soldier *pSol = apSoldiers[i];
        if (pSol == NULL) {
            continue;
        }

        i32 nCraft = (i32)pSol->bUnk183;
        if (nCraft == 254) {
            continue;                              /* away in a craft */
        }
        if ((i32)pSol->nKind != nBaseIndex) {
            continue;
        }

        if (nCraft == 255) {
            ++nCount;                              /* not aboard anything */
        } else if (nCraft >= 0 && nCraft < nCraftCount && apCraft != NULL &&
                   apCraft[nCraft] != NULL && apCraft[nCraft]->bAtBase != 0) {
            ++nCount;                              /* aboard, and that craft is home */
        }
    }

    return nCount;
}

/* Base_CountShipsAtBase @ 0x66090. Craft homed at this base, plus the shared grid byte for
 * this base's type. The grid half is why this is not just a count: 0x66110 adds
 * BaseGrid_GetByte0x654(grid, apBases[nBaseIndex]->bUnk4) to the craft total. */
i32 Base_CountShipsAtBase(Ship *const *apCraft, i32 nCraftCount, BaseRec *const *apBases,
                          i32 nBaseCount, const u8 *abGrid, i32 nBaseIndex)
{
    i32 nShips = 0;

    for (i32 i = 0; i < nCraftCount; ++i) {
        if (apCraft != NULL && apCraft[i] != NULL &&
            (i32)apCraft[i]->bHomeBase == nBaseIndex) {
            ++nShips; /* 0x660D8 */
        }
    }

    if (nBaseIndex < 0 || nBaseIndex >= nBaseCount || apBases == NULL ||
        apBases[nBaseIndex] == NULL) {
        return nShips; /* the original indexes apBases with an unbounded base index */
    }

    return nShips + BaseGrid_GetTypeByte(apBases, nBaseCount, abGrid,
                                         apBases[nBaseIndex]->bUnk4); /* 0x66110 */
}

/* Base_CalcPsiLabCap @ 0x6598c. Kind 8, 10 per free cell -- the same walk as the rest of the
 * capacity family. Hex-Rays scrambles it into a do/while with a running pointer, but the two
 * grids it touches are the usual pair: the type byte at grid + 480 + 144*nBase and the build days
 * at grid + 36*nBase. */
i32 Base_CalcPsiLabCap(const u8 *abFacilityGrid, i32 nBaseIndex)
{
    return FacilityScoreKind(abFacilityGrid, nBaseIndex, 8, 10, true); /* 0x659E8 */
}

/* Base_CountSoldiers @ 0x661d8. Soldiers whose base slot (Soldier.nKind) is this base. This is
 * the plain assignment count; Base_CountSoldiersAssigned (0x66230) is the stricter one that also
 * asks whether the soldier is reachable. */
i32 Base_CountSoldiers(Soldier *const *apSoldiers, i32 nSoldierCount, i32 nBaseIndex)
{
    i32 nCount = 0;

    if (apSoldiers == NULL) {
        return 0;
    }

    for (i32 i = 0; i < nSoldierCount; ++i) {
        if (apSoldiers[i] != NULL && (i32)apSoldiers[i]->nKind == nBaseIndex) {
            ++nCount; /* 0x6621C */
        }
    }

    return nCount;
}

/* Base_SumCraftAtBase @ 0x6a230, renamed Base_CalcMonthlyUpkeep: the IDB name describes one of
 * the five terms. What it returns is the monthly cost of one base.
 *
 *   craft homed here   type 0 or 1 -> 500,000   2 -> 700,000   3 -> 600,000   4 -> 400,000
 *   soldiers assigned  20,000 each
 *   object 112 stock   20,000 per unit at this base
 *   object 113 stock   30,000 per unit at this base
 *   geoscape sites     20,000 * (auItemQty[224] + bRecoveredSoldierAssign)
 *                      + 30,000 * auItemQty[226], for sites whose bFlag1 is this base
 *   facility cells     50,000 for every cell whose type byte is not 255, built or not
 *
 * The last term is the one that bites: an unfinished facility is charged the same 50,000 as a
 * finished one, because this walk reads the facility TYPE grid and never looks at build days. */
i32 Base_CalcMonthlyUpkeep(Ship *const *apCraft, i32 nCraftCount,
                           Soldier *const *apSoldiers, i32 nSoldierCount,
                           const SmapSite *aSites, i32 nSiteCount,
                           ObjectRecFull *const *apObjects, const u8 *abFacilityGrid,
                           i32 nBaseIndex)
{
    static const i32 anCraftCost[5] = { 500000, 500000, 700000, 600000, 400000 }; /* 0x6A294..0x6A2D4 */

    i32 nTotal = 0;

    if (apCraft != NULL) {
        for (i32 i = 0; i < nCraftCount; ++i) {
            const Ship *pShip = apCraft[i];
            if (pShip == NULL || (i32)pShip->bHomeBase != nBaseIndex) {
                continue;
            }
            if (pShip->bCraftType < 2) {
                nTotal += anCraftCost[0];
            } else if (pShip->bCraftType <= 4) {
                nTotal += anCraftCost[pShip->bCraftType];
            }
            /* any other type contributes nothing, as in the original's switch */
        }
    }

    nTotal += 20000 * (Base_CountSoldiers(apSoldiers, nSoldierCount, nBaseIndex)
                       + Base_GetItemQty(apObjects != NULL ? apObjects[112] : NULL, nBaseIndex));
    nTotal += 30000 * Base_GetItemQty(apObjects != NULL ? apObjects[113] : NULL, nBaseIndex);

    for (i32 i = 0; i < nSiteCount; ++i) {
        if ((i32)(signed char)aSites[i].bFlag1 != nBaseIndex) {
            continue; /* 0x6A364 */
        }
        nTotal += 20000 * ((i32)aSites[i].auItemQty[224] + (i32)(signed char)aSites[i].bRecoveredSoldierAssign);
        nTotal += 30000 * (i32)aSites[i].auItemQty[226]; /* 0x6A384 */
    }

    if (abFacilityGrid != NULL) {
        const u8 *pType = abFacilityGrid + 480 + 144 * nBaseIndex; /* 0x6A3BC + 372 */
        for (i32 nRow = 0; nRow < 6; ++nRow) {
            for (i32 nCol = 0; nCol < 6; ++nCol) {
                if (pType[6 * nCol + nRow] != 255) {
                    nTotal += 50000; /* 0x6A3DC */
                }
            }
        }
    }

    return nTotal;
}

/* ------------------------------------------------------------ the geoscape speed buttons */

const i32 g_aGeoscapeSpeedSec[PUFO_GEOSCAPE_SPEED_COUNT] = { 5, 60, 300, 1380, 2460, 3600 };

/* The shared body of the six handlers (0x84F00..0x85158): Sound_Play(17), store the interval,
 * then one store per nav control - the button's own control gets 1, the other five get 0. The
 * originals write the six stores unrolled in order; the loop is the same six stores. */
static void GameTime_SetSpeedIdx(GeoscapeWorld *pWorld, UiControl *const *apSpeedCtl, i32 nIdx)
{
    i32 i;

    Sound_Play(17);
    pWorld->nTickSeconds = g_aGeoscapeSpeedSec[nIdx];
    for (i = 0; i < PUFO_GEOSCAPE_SPEED_COUNT; ++i) {
        apSpeedCtl[i]->nState = (u8)(i == nIdx ? 1 : 0);
    }
}

void GameTime_Set5Sec(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT])
{
    GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 0);
}

void GameTime_Set1Min(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT])
{
    GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 1);
}

void GameTime_Set5Min(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT])
{
    GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 2);
}

void GameTime_SetSpeedIdx3(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT])
{
    GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 3);
}

void GameTime_SetSpeedIdx4(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT])
{
    GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 4);
}

void GameTime_SetSpeedIdx5(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT])
{
    GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 5);
}

/* ------------------------------------------------------------ creating a new base entry */

i32 Base_CreateNamedEntry(const BaseCreateEnv *pEnv, i32 x, i32 y, i32 nKind, const char *pszName)
{
    i32 nBaseCount = (i32)*pEnv->pbBaseCount;
    BaseRec *pBase;
    BaseSiteRec *pSite;
    StrategItem *pItem;
    i32 i;

    (void)BaseRec_Create(pEnv->apBases, nBaseCount, pEnv->apObjects, (i32)pEnv->nObjectCount);

    (void)StrategList_AddItem(pEnv->pStrategList, pEnv->pnSeed, pEnv->nZoomMode, 0, 0, 0,
                              x, y, *pEnv->pnNextSiteId, 1);

    /* 0x669B4: the strateg entry this base owns gets bRandA. The ARM is SUB R11, R10, #0x80;
     * Hex-Rays renders it as + 0x80. */
    pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, nBaseCount);
    if (pItem != NULL) {
        pItem->bRandA = (u8)(nKind - 0x80);
    }

    pSite = &pEnv->apSiteRecs[nBaseCount];

    /* ORIGINAL BUG (0x669D0): the name is copied byte by byte to its NUL with no bound into a
     * 20-byte field; the overflow tail lands in the four dwords below (which the next stores
     * then overwrite) and past the end of the record. The port bounds the copy. */
    {
        i32 n = 0;
        while (n < (i32)sizeof(pSite->abName) - 1 && pszName[n] != 0) {
            pSite->abName[n] = pszName[n];
            ++n;
        }
        pSite->abName[n] = 0;
    }

    /* The original re-reads apBases[nBaseCount] before every store and never tests it for
     * null (0x66A08); the port reads it once and guards. */
    pBase = (pEnv->apBases != NULL && nBaseCount >= 0) ? pEnv->apBases[nBaseCount] : NULL;

    if (pBase != NULL) {
        pBase->bUnk17 = (u8)nKind;
    }

    pSite->bType = (u8)(nKind - 0x80);
    pSite->nUnk48 = 0;        /* 0x66A28 */
    pSite->nDemolishState = 0;
    pSite->nBuildState = 0;
    pSite->nFacType = *pEnv->pnNextSiteId; /* 0x66A34 */

    if (pBase != NULL) {
        pBase->nUnk8 = x;     /* 0x66A38 */
        pBase->nUnk12 = y;    /* 0x66A50 */
        pBase->bUnk16 = (u8)((i8)pEnv->abKindTable[nKind] - 1); /* 0x66A74: LDRSB then SUB #1 */
    }

    if (pEnv->nObjectCount != 0) {
        /* 0x66B0C: the research column is 144 entries per base, and the loop runs
         * nObjectCount (149) times, so five entries spill into the next base. Reproduced. */
        for (i = 0; i < (i32)pEnv->nObjectCount; ++i) {
            pEnv->pResearchCol[i] = 0;
        }
        for (i = 0; i < (i32)pEnv->nObjectCount; ++i) {
            pEnv->pProductU16Col[i] = 0;         /* 0x66B24 */
            pEnv->pProductU16Col[i + 2176] = 0;  /* 0x66B1C: + 0x1100 bytes */
            pEnv->pProductU32Col[i] = 0;         /* 0x66B34 */
            pEnv->pProductU32Col[i + 1600] = 0;  /* 0x66B2C: + 0x1900 bytes */
        }
    }

    *pEnv->pnNextSiteId += 1;                        /* 0x66B50 */
    *pEnv->pbBaseCount = (i8)(*pEnv->pbBaseCount + 1); /* 0x66B64 */

    return 1560; /* 0x66B6C: the 0x618 offset the count store used - register residue */
}
