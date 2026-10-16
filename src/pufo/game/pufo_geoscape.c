/* pufo_geoscape.c - see pufo_geoscape.h for the evidence map. */
#include "game/pufo_geoscape.h"
#include <stdio.h>             /* snprintf, the bounded stand-in for the original's j_itoa */
#include <stdlib.h>            /* malloc/free, RadarScreen_Init's pads and controls */
#include <stddef.h>            /* size_t, the bounded text copy */
#include "audio/pufo_audio.h"  /* Sound_Play, the click the speed buttons play */
#include "game/pufo_baseui.h"  /* Funding_AddRegionBonus and PUFO_REGION_ID_NONE, the radar contact bonus */
#include "game/pufo_boot.h"    /* PUFO_TEXT_REC_STRIDE, the TEXTS record stride */
#include "game/pufo_font.h"    /* Font_DrawGlyph, g_fontSys.nBaseBitsBase, Text_DrawString */
#include "render/pufo_render.h" /* g_aPalette, the slot RadarScreen_CyclePalette rotates */
#include "game/pufo_save.h"    /* SmapSite, for the monthly upkeep of a base */
#include "ui/pufo_geouimanage.h" /* PUFO_GEOUI_CLOCK_DIGIT_PADS, the ten pads at 0x186BF0 */
#include "util/pufo_rand.h"
#include "util/pufo_str.h"     /* pufo_strncpy/pufo_strcat, the cost label */

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

        /* 0x5D5E4..0x5D680. The original's home-base filter is dead code: 0xFF falls through the
         * 0xFE test and 0xFE takes the BNE, so every craft away from base reaches the range gate. */
        SmapCraftRangeEnv rangeEnv = { pWorld, pState->apBases, pState->pList };
        for (i32 i = 0; i < pWorld->nShipCount; ++i) {
            Ship *pShip = pWorld->apCraft[i];
            if (pShip->bAtBase == 0
                && Smap_CraftRangeExhausted(&rangeEnv, i)
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
        /* 0x6AC68 loads the field with LDRSB and 0x6AC58 masks the argument with `AND R1,R1,#0xFF`,
         * so the original compares a SIGNED field against an unsigned byte: a field of 0x80 or more
         * never matches any argument in 0..255. The port compared both as u8, which matched them. */
        if ((i32)(i8)apBases[i]->bUnk4 == (i32)nType) {
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

i8 FacBuildSpec_GetDays(u8 nKind)
{
    if (nKind >= PUFO_FAC_SPEC_COUNT) {
        return 0; /* deviation: 0x59E14 and 0x623EC index the 18 records unguarded */
    }
    return g_aFacBuildSpec[nKind].nBuildDays;
}

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
    if (nBaseIndex < 0 || nBaseIndex >= PUFO_FAC_BASE_COUNT) {
        return; /* deviation: the original bounds nothing */
    }

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
    BaseRec_AddItemQty(pBase, apObjects[OBJ_ELERIUM_115], 10);       /* 0x95FCC, Map_apObjects105 */
    BaseRec_AddItemQty(pBase, apObjects[OBJ_HOVERTANK_PLASMA], 2);   /* 0x95FE4, Map_apObjects34 */
    BaseRec_AddItemQty(pBase, apObjects[OBJ_TANK_CANNON], 1);        /* 0x95FFC, Map_apObjects31 */
    BaseRec_AddItemQty(pBase, apObjects[OBJ_TANK_CANNON_SHELLS], 33); /* 0x96014, Map_apObjects56 */
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

/* The crew availability expression the two getters share: one per-base total minus the
 * already allocated column sum. No floor, exactly as the original's SUB leaves it. */
static i32 CrewAvailable(const ObjectRecFull *pCrewRec, const u16 *apBaseCol,
                         i32 nBaseIndex, i32 nEntries)
{
    return Base_GetItemQty(pCrewRec, nBaseIndex) - QuantitySumColumn(apBaseCol, nEntries);
}

/* Base_GetScientistCount @ 0x6812c. Slot 113 of the map object array (0x113C84), and the
 * research column of the base being asked about. */
i32 Base_GetScientistCount(const ObjectRecFull *pScientistRec, const u16 *apResearchCol,
                           i32 nBaseIndex, i32 nEntries)
{
    return CrewAvailable(pScientistRec, apResearchCol, nBaseIndex, nEntries); /* 0x68148 - 0x68188 */
}

/* Base_GetEngineerCount @ 0x681f4. Slot 112 (0x113C80), and the manufacturing column. */
i32 Base_GetEngineerCount(const ObjectRecFull *pEngineerRec, const u16 *apManufCol,
                          i32 nBaseIndex, i32 nEntries)
{
    return CrewAvailable(pEngineerRec, apManufCol, nBaseIndex, nEntries); /* 0x68218 - 0x68250 */
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

/* Base_TickFacilityConstruction @ 0x62824. The header block has the countdown, the three extra
 * days stores a completed hangar makes, and the message payload the ARM actually passes. */
void Base_TickFacilityConstruction(const BaseTickConstructionEnv *pEnv)
{
    if (pEnv == NULL || pEnv->abFacilityGrid == NULL || pEnv->pbBaseCount == NULL) {
        return;
    }

    const i32 nBaseCount = (i32)(i8)*pEnv->pbBaseCount; /* 0x62854: LDRSB of abFacilityGrid + 0x618 */
    if (nBaseCount <= 0) {
        return;
    }

    u8 *pGrid = pEnv->abFacilityGrid;

    for (i32 nBase = 0; nBase < nBaseCount; ++nBase) {
        u8 *pDays = pGrid + PUFO_GRID_DAYS_STRIDE * nBase; /* 0x62860: R8 walks the days block, +6 a row */

        for (i32 nRow = 0; nRow < 6; ++nRow) {
            for (i32 nCol = 0; nCol < 6; ++nCol) {
                u8 nDay = pDays[6 * nRow + nCol];          /* 0x62878 */

                if (nDay == 0) {
                    continue;                              /* 0x62880 */
                }
                nDay = (u8)(nDay - 1);                     /* 0x62888 */
                pDays[6 * nRow + nCol] = nDay;
                if (nDay != 0) {
                    continue;                              /* 0x62890 */
                }

                const i32 nKindOff = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_TYPE_OFF +
                                     PUFO_GRID_FAC_STRIDE * nBase;
                if (pGrid[nKindOff + 6 * nRow + nCol] == (u8)FAC_HANGAR) { /* 0x628B4 */
                    pDays[6 * nRow + nCol + 6] = 0;   /* 0x628C4: the cell directly below */
                    pDays[6 * nRow + nCol + 7] = 0;   /* 0x628CC: R8[j+7], below-right */
                    pDays[6 * nRow + nCol + 1] = 0;   /* 0x628D4: R8[j+1], to the right */
                }

                (void)Base_UpdateAlienContainmentNeeded(pGrid, nBase,
                    Base_CountAmmoItems(pEnv->apObjects, pEnv->nObjectCount, nBase)); /* 0x628E4 */
                (void)Base_UpdateLabNeeded(pGrid, nBase,
                    Base_CountActiveResearch(pEnv->apResearchCols + 144 * nBase,
                                             pEnv->nResearchEntries));                 /* 0x628F0 */
                (void)Base_UpdateLivingQuartersNeeded(pGrid, nBase,
                    Base_CalcOccupiedSpace(pEnv->apSoldiers, *pEnv->pnSoldierCount,
                                           pEnv->apObjects, pEnv->nObjectCount, nBase) +
                    Base_CountOutboundTransferSpace(pEnv->aSites, pEnv->nSiteCount, -1,
                                                    pEnv->apBases, nBase, true));      /* 0x628FC */
                (void)Base_UpdatePsiLabNeeded(pGrid, nBase,
                    Base_CountPsiSoldiers(pEnv->apSoldiers, *pEnv->pnSoldierCount, nBase)); /* 0x62908 */
                (void)Base_UpdateStoresNeeded(pGrid, nBase,
                    Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nBase) +
                    Base_CountItemQtyAcrossBases(pEnv->aSites, pEnv->nSiteCount, -1,
                                                 pEnv->apBases, nBase, pEnv->apObjects,
                                                 pEnv->nObjectCount, true));           /* 0x62914 */
                (void)Base_UpdateWorkshopNeeded(pGrid, nBase,
                    Base_CountActiveManuf(pEnv->apManufCols + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase,
                                          pEnv->nManufEntries));                       /* 0x62920 */

                *pEnv->pdwShortfall = 1;                                             /* 0x62934 */
                /* The callee takes six arguments: a3 in R2, a4 in R3, a5 at [SP+0] and a6 at
                 * [SP+4]. 0x62938 puts the base in R2, 0x6292C puts the ROW in R3, 0x62944 stores
                 * the COLUMN at [SP+0] and 0x62930 stores R9 -- the zero the countdown completion
                 * writes with -- at [SP+4]. So the payload is (38, base, row, col, 0). */
                UiLineList_AppendWithSound(pEnv->pLines, 38, (u32)nBase, (u32)nRow, (u32)nCol, 0u,
                                           pEnv->pfnPlaySample, pEnv->pSoundUser);   /* 0x62948 */
            }
        }
    }
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

/* Base_CountPsiSoldiers @ 0x65a08. Soldiers assigned to this base (nKind @ 0xB6 equals the base
 * index, compared as the original does: the byte against the sign-extended char argument) that
 * also have bUnk149 @ 0x95 set — the psi marker. The budget of Base_UpdatePsiLabNeeded. */
i32 Base_CountPsiSoldiers(Soldier *const *apSoldiers, i32 nSoldierCount, i32 nBaseIndex)
{
    i32 nCount = 0;

    if (apSoldiers == NULL) {
        return 0;
    }

    for (i32 i = 0; i < nSoldierCount; ++i) {
        Soldier *pSol = apSoldiers[i];
        if (pSol == NULL) {
            continue;
        }
        if ((i32)pSol->nKind == (i8)nBaseIndex && pSol->bUnk149 != 0) {
            ++nCount; /* 0x65A2C */
        }
    }

    return nCount;
}

/* One site's recovery load, the quantity Base_CountOutboundTransferSpace sums: the u16 soldier
 * count, the signed-char assignment byte, and the two item columns 112 and 113 (0x68078). */
static i32 SmapSite_RecoveryLoad(const SmapSite *pSite)
{
    return (i32)pSite->uRecoveredSoldiers
         + (i32)(i8)pSite->bRecoveredSoldierAssign
         + (i32)pSite->auItemQty[113]
         + (i32)pSite->auItemQty[112];
}

/* Base_CountOutboundTransferSpace @ 0x68044. The recovery load of site nSiteIdx (when it is not
 * -1) plus, when bScanSites, the same load of every OTHER site whose bFlag1 equals this base's
 * type byte apBases[nBaseIndex]->bUnk4 — the sites that hand their recovery to this base type.
 * The first argument of the original (&0x159768, the product block) is never read. */
i32 Base_CountOutboundTransferSpace(const SmapSite *aSites, i32 nSiteCount, i32 nSiteIdx,
                                    BaseRec *const *apBases, i32 nBaseIndex, bool bScanSites)
{
    i32 nTotal = 0;

    if (aSites == NULL || apBases == NULL) {
        return 0;
    }
    /* Bounded against the array, not the count: the buy/sell and transfer screens pass the site
     * COUNT (0x75E34, 0x9666C), i.e. the working record, and the original bounds nothing. */
    if (nSiteIdx >= 0 && nSiteIdx < PUFO_SMAP_SITE_COUNT) {
        nTotal += SmapSite_RecoveryLoad(&aSites[nSiteIdx]); /* 0x68068 */
    }
    if (bScanSites) {
        for (i32 i = 0; i < nSiteCount; ++i) {
            if (i == nSiteIdx) {
                continue;
            }
            if ((i8)apBases[nBaseIndex]->bUnk4 == (i8)aSites[i].bFlag1) {
                nTotal += SmapSite_RecoveryLoad(&aSites[i]); /* 0x680B4 */
            }
        }
    }
    return nTotal;
}

/* The object-index filter of Base_CountItemQtyAcrossBases (0x67E00..0x67E20): the first clause
 * `v6 > &Map` is always true (the slot walk starts at Map+4), so what remains is the index set
 * 1..69, 105..111, 122..124 and 140. Object 0 is never counted. */
static bool ItemQtyObjIndexInFilter(i32 nObj)
{
    return nObj < 70 || (nObj > 104 && nObj < 112) || (nObj > 121 && nObj < 125) || nObj == 140;
}

/* Base_CountItemQtyAcrossBases @ 0x67d68. Cargo volume, not item count: every term is
 * ObjectRecFull.bCargoSize (0xC6) times a site's auItemQty column.
 *
 * Block 1 (nSiteIdx != -1): the filtered columns of site nSiteIdx itself.
 * Block 2 (bScanSites): for every OTHER site whose bFlag1 equals this base's type byte, the
 * filtered columns of THAT site, but gated on site nSiteIdx having a non-zero quantity in the
 * same column (0x67EE0: the gate reads [0x173*nSiteIdx + v]*2+2, the contribution reads the
 * scanned site).
 *
 * Deviation: the needed-pass callers pass nSiteIdx = -1, and the original then gates on
 * aSmapSites[-1] — 742 bytes below the array, which is the buy/sell screen scratch (zero in the
 * image, written only by screens). The port treats a negative index as a zero gate, which is the
 * same value at every load-time call site. */
i32 Base_CountItemQtyAcrossBases(const SmapSite *aSites, i32 nSiteCount, i32 nSiteIdx,
                                 BaseRec *const *apBases, i32 nBaseIndex,
                                 ObjectRecFull *const *apObjects, i32 nObjectCount,
                                 bool bScanSites)
{
    i32 nTotal = 0;

    if (aSites == NULL || apBases == NULL || apObjects == NULL) {
        return 0;
    }

    /* Bounded against the array, not the count: five of the ten call sites pass the site COUNT as
     * the site index -- 0x750B0, 0x75CC0, 0x9641C, 0x9692C, 0x97D30 -- because that is where the
     * transfer flow keeps the record it is working on, and the original bounds nothing. */
    if (nSiteIdx >= 0 && nSiteIdx < PUFO_SMAP_SITE_COUNT && nObjectCount > 1) {
        for (i32 v = 1; v < nObjectCount; ++v) {
            if (!ItemQtyObjIndexInFilter(v)) {
                continue;
            }
            if (aSites[nSiteIdx].auItemQty[v] != 0 && apObjects[v] != NULL) {
                nTotal += (i32)apObjects[v]->bCargoSize * (i32)aSites[nSiteIdx].auItemQty[v];
            }
        }
    }

    if (bScanSites && nSiteIdx >= 0 && nSiteIdx < PUFO_SMAP_SITE_COUNT && nObjectCount > 1) {
        for (i32 i = 0; i < nSiteCount; ++i) {
            if (i == nSiteIdx) {
                continue;
            }
            if ((i8)apBases[nBaseIndex]->bUnk4 != (i8)aSites[i].bFlag1) {
                continue;
            }
            for (i32 v = 1; v < nObjectCount; ++v) {
                if (!ItemQtyObjIndexInFilter(v)) {
                    continue;
                }
                if (aSites[nSiteIdx].auItemQty[v] != 0 && apObjects[v] != NULL) {
                    nTotal += (i32)apObjects[v]->bCargoSize * (i32)aSites[i].auItemQty[v];
                }
            }
        }
    }

    return nTotal;
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

/* Smap_SumSiteScores @ 0x96894. The working transfer site is the slot AT the site count, which is
 * the same record Transfer_ResetSite (0x9690c) clears out. */
i32 Smap_SumSiteScores(const SmapSite *aSites, i32 nSiteCount)
{
    if (aSites == NULL || nSiteCount < 0 || nSiteCount >= PUFO_SMAP_SITE_COUNT) {
        return 0; /* the original reads one record past the array here */
    }

    const SmapSite *pSite = &aSites[nSiteCount];
    i32 nTotal = 0;

    for (i32 i = PUFO_SITE_SCORE_A_FIRST; i <= PUFO_SITE_SCORE_A_LAST; ++i) {
        nTotal += pSite->auItemQty[i]; /* 0x968C0..0x968D0, 28 halfwords */
    }
    nTotal += pSite->auItemQty[PUFO_SITE_SCORE_SINGLE]; /* 0x968D4 */
    for (i32 i = PUFO_SITE_SCORE_B_FIRST; i <= PUFO_SITE_SCORE_B_LAST; ++i) {
        nTotal += pSite->auItemQty[i]; /* 0x968EC..0x968F8, 8 halfwords */
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

/* The region counter load (0x66A64 and 0x6A46C): the original computes 0x1864DD + region, which
 * for the ten regions 0x80..0x89 is g_anRegionBuildCount @ 0x18655D -- nine counters for
 * 0x80..0x88, and region 0x89's slot is the byte at 0x186566 that IDA names bUiFrameStyle (the
 * same byte StartNew's ten-byte sweep at 0x94650 writes). A region below 0x80 -- Game_StartNew
 * passes 0 for the starting base -- reads the byte at 0x1864DD, the tail of szScratch186488; the
 * port reads 0 there (deviation: the shipped image holds 0 at that address, so the value matches). */
static i8 RegionBuildCountAt(const u8 *paRegionBuildCount, i32 nRegionId)
{
    if (paRegionBuildCount == NULL || nRegionId < PUFO_REGION_ID_FIRST) {
        return 0;
    }
    return (i8)paRegionBuildCount[nRegionId - PUFO_REGION_ID_FIRST];
}

i32 Base_CreateNamedEntry(const BaseCreateEnv *pEnv, i32 x, i32 y, i32 nRegionId, const char *pszName)
{
    i32 nBaseCount = (i32)*pEnv->pbBaseCount;
    BaseRec *pBase;
    BaseSiteRec *pSite;
    StrategItem *pItem;
    i32 i;

    (void)BaseRec_Create(pEnv->apBases, nBaseCount, pEnv->apObjects, (i32)pEnv->nObjectCount);

    (void)StrategList_AddItem(pEnv->pStrategList, pEnv->pnSeed, pEnv->nZoomMode, 0, 0, 0,
                              x, y, *pEnv->pnNextSiteId, 1);

    /* 0x669B4: the strateg entry this base owns gets the region index. The ARM is
     * SUB R11, R10, #0x80; Hex-Rays renders it as + 0x80. */
    pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, nBaseCount);
    if (pItem != NULL) {
        pItem->bRandA = (u8)(nRegionId - 0x80);
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
        pBase->bRegionId = (u8)nRegionId;
    }

    pSite->bType = (u8)(nRegionId - 0x80);
    pSite->nUnk48 = 0;        /* 0x66A28 */
    pSite->nDemolishState = 0;
    pSite->nBuildState = 0;
    pSite->nFacType = *pEnv->pnNextSiteId; /* 0x66A34 */

    if (pBase != NULL) {
        pBase->nUnk8 = x;     /* 0x66A38 */
        pBase->nUnk12 = y;    /* 0x66A50 */
        pBase->bUnk16 = (u8)(RegionBuildCountAt(pEnv->paRegionBuildCount, nRegionId) - 1);
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

/* Base_CanPlaceHangarAt @ 0x629b0. Whether one cell of the selected base can host a hangar; the
 * header carries the evidence map and the seven-step decision. */
i32 Base_CanPlaceHangarAt(const HangarPlaceEnv *pEnv, i32 nX, i32 nY)
{
    u8 abScratch[PUFO_GRID_CELL_COUNT];
    const u8 *abFacilityGrid;
    i32 nBase, nFac, nDays, nFacType;
    i32 nRow, nCol, nIdx, bChanged;
    bool bSeeded = false;

    if (pEnv == NULL || pEnv->abFacilityGrid == NULL) {
        return 0;
    }
    abFacilityGrid = pEnv->abFacilityGrid;
    nBase = (i32)pEnv->nSelectedBaseIdx;

    if (nBase < 0 || nBase >= PUFO_FAC_BASE_COUNT || nX < 0 || nX >= 6 || nY < 0 || nY >= 6) {
        return 0; /* deviation: the original bounds none of these */
    }

    nFac     = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase + 6 * nY + nX;
    nDays    = PUFO_GRID_DAYS_STRIDE * nBase + 6 * nY + nX;
    nFacType = abFacilityGrid[nFac + PUFO_GRID_FAC_TYPE_OFF]; /* 0x62A04: [R1,#0x1E0] */

    /* 0x62A1C..0x62A50. The original tests this twice - once gated on facType == 11 at 0x62A30
     * and once ungated at 0x62A50 - and both jump to the `MOV R0,#1` tail at 0x62D44, so the
     * facType gate is dead and one compare covers both. An unfinished cell is accepted outright
     * and none of the connectivity work below runs. */
    if (abFacilityGrid[nDays] != 0) {
        return 1;
    }

    /* 0x62A5C..0x62B34: the scratch map. A cell is 1 when it carries a tileA stamp AND its build
     * days are finished; everything else is 0. A hangar cell marks its whole 2 by 2 footprint. */
    for (nRow = 0; nRow < 6; ++nRow) {
        for (nCol = 0; nCol < 6; ++nCol) {
            const i32 nCell = 6 * nRow + nCol;
            const i32 nFacCell = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase + nCell;
            const i32 nDayCell = PUFO_GRID_DAYS_STRIDE * nBase + nCell;

            abScratch[nCell] = (abFacilityGrid[nFacCell] != 0
                                && abFacilityGrid[nDayCell] == 0) ? 1 : 0;

            /* ORIGINAL BUG @ 0x62AD8: the three footprint stores below land on cells this same
             * loop visits in a later iteration, and each one's own store above replaces the mark,
             * so the marking does nothing. At row 5 they are worse: scratch is 36 dwords and
             * scratch[36], scratch[37] and scratch[42] are the function's own saved registers.
             * The marking is reproduced as written; the stores are clamped. */
            if (abFacilityGrid[nFacCell + PUFO_GRID_FAC_TYPE_OFF] == PUFO_FAC_TYPE_HANGAR
                && abFacilityGrid[nDayCell] == 0) {
                abScratch[nCell] = 1;                                    /* 0x62AC8 */
                if (nCell + 6 < PUFO_GRID_CELL_COUNT) abScratch[nCell + 6] = 1; /* 0x62AD8 */
                if (nCell + 1 < PUFO_GRID_CELL_COUNT) abScratch[nCell + 1] = 1; /* 0x62AE8 */
                if (nCell + 7 < PUFO_GRID_CELL_COUNT) abScratch[nCell + 7] = 1; /* 0x62B0C: +7, the diagonal */
            }
        }
    }

    /* 0x62B3C..0x62B60: the candidate footprint is cleared, so the site being tested for does
     * not block the flood below. */
    nIdx = 6 * nY + nX;
    abScratch[nIdx] = 0;
    if (nFacType == PUFO_FAC_TYPE_HANGAR) {
        /* ORIGINAL BUG @ 0x62B80: the +6, +1 and +7 stores have no bound test either, so a
         * footprint anchored on row 5 writes scratch[36..42] - the original's saved registers -
         * past its 36 dword scratch array. The port clamps all three. */
        if (nIdx + 6 < PUFO_GRID_CELL_COUNT) abScratch[nIdx + 6] = 0;
        if (nIdx + 1 < PUFO_GRID_CELL_COUNT) abScratch[nIdx + 1] = 0;
        if (nIdx + 7 < PUFO_GRID_CELL_COUNT) abScratch[nIdx + 7] = 0;
    }

    /* 0x62B8C..0x62BD0: the first 1 in row-major order seeds the flood. */
    for (nIdx = 0; nIdx < PUFO_GRID_CELL_COUNT && !bSeeded; ++nIdx) {
        if (abScratch[nIdx] == 1) {
            abScratch[nIdx] = 2;
            bSeeded = true;
        }
    }

    /* 0x62BE0..0x62CC8: the flood. A 2 spreads to every 4-neighbour that is neither 0 nor 3, and
     * the source becomes 3 once anything in the sweep has changed - the flag is shared across
     * the sweep in the original (R5 is only cleared at 0x62BE0), and a neighbour that is already
     * 2 counts as a change. Both are reproduced. */
    do {
        bChanged = 0;
        for (nRow = 0; nRow < 6; ++nRow) {
            for (nCol = 0; nCol < 6; ++nCol) {
                const i32 nCell = 6 * nRow + nCol;

                if (abScratch[nCell] != 2) {
                    continue;
                }
                if (nRow > 0 && abScratch[nCell - 6] != 0 && abScratch[nCell - 6] != 3) {
                    abScratch[nCell - 6] = 2; /* 0x62C14 */
                    bChanged = 1;
                }
                if (nRow < 5 && abScratch[nCell + 6] != 0 && abScratch[nCell + 6] != 3) {
                    abScratch[nCell + 6] = 2; /* 0x62C44 */
                    bChanged = 1;
                }
                if (nCol > 0 && abScratch[nCell - 1] != 0 && abScratch[nCell - 1] != 3) {
                    abScratch[nCell - 1] = 2; /* 0x62C68 */
                    bChanged = 1;
                }
                if (nCol < 5 && abScratch[nCell + 1] != 0 && abScratch[nCell + 1] != 3) {
                    abScratch[nCell + 1] = 2; /* 0x62C90 */
                    bChanged = 1;
                }
                if (bChanged != 0) {
                    abScratch[nCell] = 3; /* 0x62CA0 */
                }
            }
        }
    } while (bChanged != 0);

    /* 0x62CE0..0x62D0C: a 1 the flood never reached means the footprint would cut that cell off. */
    for (nIdx = 0; nIdx < PUFO_GRID_CELL_COUNT; ++nIdx) {
        if (abScratch[nIdx] == 1) {
            return 0;
        }
    }

    if (nFacType == PUFO_FAC_TYPE_HANGAR) {
        /* 0x62D14..0x62D40: a hangar cell needs a free hangar slot. Both callees get the
         * selected base index, which is what the original passes (0x62D14 and 0x62D30). */
        const i32 nCap = Base_CalcHangarCap(abFacilityGrid, nBase);
        const i32 nShips = Base_CountShipsAtBase(pEnv->apCraft, pEnv->nCraftCount,
                                                 pEnv->apBases, pEnv->nBaseCount,
                                                 pEnv->abGridBytes, nBase);
        return (nCap - nShips) > 0 ? 1 : 0;
    }

    if (abFacilityGrid[nFac + PUFO_GRID_LAYER2_OFF] != 0) {
        return 0; /* 0x62D54: [R3,#0x1BC], the needed-flag layer */
    }
    return (nFacType != 0) ? 1 : 0; /* 0x62D60..0x62D6C */
}

/* BaseFac_FormatCostLabel @ 0x6a404. "TEXTS[905 + region]-<region counter>" into the shared
 * scratch label -- the region's own name, e.g. "Северная Америка-0" -- then the region's counter
 * is incremented. See the header for the evidence. */
char *BaseFac_FormatCostLabel(char *pszLabel, size_t cchLabel, const u8 *pTextRecs,
                              u8 nRegionId, i8 *paRegionBuildCount)
{
    char szNum[100]; /* 0x6A40C: var_64, the itoa scratch */
    const char *pszName;
    i8 nCount;

    if (pszLabel == NULL || cchLabel == 0 || pTextRecs == NULL || paRegionBuildCount == NULL) {
        return pszLabel;
    }

    /* 0x6A42C..0x6A450: the copy loop walks the TEXTS record from its first byte, NUL included.
     * The original has no bound; pufo_strncpy is the port's bounded form. */
    pszName = (const char *)(pTextRecs
                             + (size_t)PUFO_TEXT_REC_STRIDE * (PUFO_REGION_NAME_REC_BASE + nRegionId));
    pufo_strncpy(pszLabel, pszName, cchLabel);

    (void)pufo_strcat(pszLabel, "-", cchLabel); /* 0x6A45C, asc_AB340 */

    nCount = RegionBuildCountAt((const u8 *)paRegionBuildCount, nRegionId); /* 0x6A46C */
    (void)snprintf(szNum, sizeof(szNum), "%d", (int)nCount); /* 0x6A474: j_itoa, base 10 */
    (void)pufo_strcat(pszLabel, szNum, cchLabel); /* 0x6A480 */

    /* 0x6A490..0x6A49C: +1, sign-extended through LSL/ASR #24, stored as a byte at
     * 0x18655D + region - 0x80. Region 0x89 ("Здесь нельзя строить") lands on 0x186566, the byte
     * IDA names bUiFrameStyle; the shipped placement flow never reaches it with this call, and a
     * region below 0x80 would store below the table, which the port skips (deviation). */
    if (nRegionId >= PUFO_REGION_ID_FIRST) {
        paRegionBuildCount[nRegionId - PUFO_REGION_ID_FIRST] = (i8)(nCount + 1);
    }

    return pszLabel; /* 0x6A4A8 */
}

/* --- RadarScreen_BlitOverlay @ 0x5d6d4 --- */

void RadarScreen_BlitOverlay(PadImage *pOverlayPad)
{
    if (pOverlayPad != NULL) {
        PadImage_Blit(pOverlayPad); /* 0x5d6e0 */
    }
}

/* --- RadarScreen_RollWeaponHit @ 0x5db24 --- */

bool RadarScreen_RollWeaponHit(const RadarHitRollEnv *pEnv, i32 nEventType)
{
    StrategItem *pItem;
    u16 nRoll;
    i32 nHp;

    if (pEnv == NULL || pEnv->pStrategList == NULL || pEnv->pnStrategIdx == NULL
        || pEnv->pnTargetHp == NULL) {
        return false;
    }

    nRoll = Game_RandU16(100);                                                       /* 0x5DB3C */

    switch (nEventType) {
        case 25:
            if (nRoll >= 50) {
                return false;                                                        /* 0x5DC6C */
            }
            nHp = *pEnv->pnTargetHp - 50;
            break;
        case 26:
            if (nRoll >= 80) {
                return false;                                                        /* 0x5DC44 */
            }
            nHp = *pEnv->pnTargetHp + (i32)Game_RandU16(20) - 100;                   /* 0x5DC60 */
            break;
        case 27:
            if (nRoll >= 90) {
                return false;                                                        /* 0x5DC1C */
            }
            nHp = *pEnv->pnTargetHp + (i32)Game_RandU16(20) - 120;                   /* 0x5DC38 */
            break;
        case 28:
            if (nRoll >= 70) {
                return false;                                                        /* 0x5DBF4 */
            }
            nHp = *pEnv->pnTargetHp + (i32)Game_RandU16(30) - 90;                    /* 0x5DC10 */
            break;
        case 29:
            if (nRoll >= 70) {
                return false;                                                        /* 0x5DBCC */
            }
            nHp = *pEnv->pnTargetHp + (i32)Game_RandU16(50) - 140;                   /* 0x5DBE8 */
            break;
        case 30:
            if (nRoll >= 95) {
                return false;                                                        /* 0x5DB70 */
            }
            nHp = *pEnv->pnTargetHp + (i32)Game_RandU16(30) - 230;                   /* 0x5DB8C */
            break;
        default:
            return false;                                                            /* 0x5DC80 */
    }

    *pEnv->pnTargetHp = nHp;                                                         /* 0x5DB90 */
    pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, *pEnv->pnStrategIdx);
    if (pItem != NULL) {
        pItem->nHp = nHp;                                                            /* 0x5DBB0 */
    }
    return true;                                                                     /* 0x5DBB8 */
}

/* The four corner buttons share everything except the three differences listed in the header, so
 * they share one body. The store order is the originals' order in every case. */
static void RadarSetCursor(const RadarCursorEnv *pEnv, i32 nX, i32 nY, i32 nRange, u8 bFlip)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pbGate != NULL && (*pEnv->pbGate & 0xFFu) != 0) {
        return; /* 0x5E774 / 0x5E840 / 0x5E974 / 0x5EA7C */
    }
    if (pEnv->pnCursorX != NULL) {
        *pEnv->pnCursorX = nX;
    }
    if (pEnv->pnCursorY != NULL) {
        *pEnv->pnCursorY = nY;
    }
    if (pEnv->pnRangeTarget != NULL) {
        *pEnv->pnRangeTarget = nRange;
    }
    if (pEnv->pbFlip != NULL) {
        *pEnv->pbFlip = bFlip;
    }
    /* The copy loop runs from an all-zero source, so the only byte it writes is the terminator. */
    if (pEnv->pszMsg != NULL && pEnv->cchMsg > 0) {
        pEnv->pszMsg[0] = 0;
    }
    if (pEnv->pnMsgTimer != NULL) {
        *pEnv->pnMsgTimer = 0;
    }
}

/* NE and SW take the range target from the map bounds and the level. The divide is signed and
 * truncates toward zero, which is what C's / does for signed operands. */
static i32 RadarScaleRange(const RadarCursorEnv *pEnv, bool bTakeMax)
{
    if (pEnv == NULL) {
        return 0;
    }
    const i32 nLevel = (pEnv->pnLevel != NULL) ? *pEnv->pnLevel : 0;
    const i32 nA = (pEnv->pnBoundA != NULL) ? *pEnv->pnBoundA : 0;
    const i32 nB = (pEnv->pnBoundB != NULL) ? *pEnv->pnBoundB : 0;
    const i32 nBound = bTakeMax ? ((nA < nB) ? nB : nA) : ((nA > nB) ? nB : nA);
    const i32 nScaled = PUFO_RADAR_RANGE_FULL * (PUFO_RADAR_LEVEL_MAX - nLevel);

    return nBound + nScaled / PUFO_RADAR_RANGE_DIVISOR;
}

void RadarScreen_SetCursorNW(const RadarCursorEnv *pEnv)
{
    RadarSetCursor(pEnv, PUFO_RADAR_CURSOR_WEST, PUFO_RADAR_CURSOR_NORTH,
                   PUFO_RADAR_RANGE_FULL, 0);
}

void RadarScreen_SetCursorSE(const RadarCursorEnv *pEnv)
{
    RadarSetCursor(pEnv, PUFO_RADAR_CURSOR_EAST, PUFO_RADAR_CURSOR_SOUTH,
                   PUFO_RADAR_RANGE_FULL, 1); /* the only one of the four that sets the flip byte */
}

void RadarScreen_SetCursorNE(const RadarCursorEnv *pEnv)
{
    if (pEnv != NULL && pEnv->pnInterceptA != NULL && pEnv->pnInterceptB != NULL &&
        *pEnv->pnInterceptA == PUFO_RADAR_NO_TARGET &&
        *pEnv->pnInterceptB == PUFO_RADAR_NO_TARGET) {
        return; /* 0x5E818 / 0x5E830: no intercept target, nothing to point at */
    }
    RadarSetCursor(pEnv, PUFO_RADAR_CURSOR_EAST, PUFO_RADAR_CURSOR_NORTH,
                   RadarScaleRange(pEnv, true), 0);
}

void RadarScreen_SetCursorSW(const RadarCursorEnv *pEnv)
{
    if (pEnv != NULL && pEnv->pnInterceptA != NULL && pEnv->pnInterceptB != NULL &&
        *pEnv->pnInterceptA == PUFO_RADAR_NO_TARGET &&
        *pEnv->pnInterceptB == PUFO_RADAR_NO_TARGET) {
        return; /* 0x5E94C / 0x5E964 */
    }
    RadarSetCursor(pEnv, PUFO_RADAR_CURSOR_WEST, PUFO_RADAR_CURSOR_SOUTH,
                   RadarScaleRange(pEnv, false), 0);
}

/* RadarScreen_CyclePalette @ 0x5ed0c. Rotate the 25 animated palette entries of aPalette slot 22,
 * then nudge the animated radar range toward its target. */
void RadarScreen_CyclePalette(const RadarPaletteEnv *pEnv)
{
    /* 0x5ED14..0x5ED50: the ARM walks v1 from aPalette+0x60 down to aPalette+0x4 copying
     * [v1+0x57FC] over [v1+0x5800], which shifts slot 22 entries 0..23 up one place, then stores the
     * saved entry 24 into entry 0. */
    const u32 nTop = g_aPalette[PUFO_RADAR_PAL_SLOT][PUFO_RADAR_PAL_CYCLE - 1];
    for (i32 i = PUFO_RADAR_PAL_CYCLE - 1; i >= 1; --i) {
        g_aPalette[PUFO_RADAR_PAL_SLOT][i] = g_aPalette[PUFO_RADAR_PAL_SLOT][i - 1];
    }
    g_aPalette[PUFO_RADAR_PAL_SLOT][0] = nTop; /* 0x5ED50 */

    if (pEnv == NULL || pEnv->pnRangeCur == NULL || pEnv->pnRangeTarget == NULL) {
        return;
    }

    i32 nCur = *pEnv->pnRangeCur;
    const i32 nTgt = *pEnv->pnRangeTarget;

    /* 0x5ED5C..0x5ED98: four conditional steps, each storing. The pairs are an overshoot-and-correct,
     * so a value two or more away moves by two and a value already at the target does not move. */
    if (nCur < nTgt) { nCur += 1; *pEnv->pnRangeCur = nCur; } /* 0x5ED64 */
    if (nCur > nTgt) { nCur -= 1; *pEnv->pnRangeCur = nCur; } /* 0x5ED74 */
    if (nCur < nTgt) { nCur += 1; *pEnv->pnRangeCur = nCur; } /* 0x5ED84 */
    if (nCur > nTgt) { nCur -= 1; *pEnv->pnRangeCur = nCur; } /* 0x5ED94 */

    /* 0x5ED90..0x5EDDC: with the flip byte set and the value still short of the target, up to four
     * more single steps. There is no matching branch for a value still above the target. */
    if (pEnv->pbFlip != NULL && (*pEnv->pbFlip & 0xFFu) != 0 && nCur < nTgt) {
        nCur += 1; *pEnv->pnRangeCur = nCur; /* 0x5EDB0 */
        if (nCur < nTgt) {
            nCur += 1; *pEnv->pnRangeCur = nCur; /* 0x5EDC0 */
            if (nCur < nTgt) { nCur += 1; *pEnv->pnRangeCur = nCur; } /* 0x5EDCC */
            if (nCur < nTgt) { nCur += 1; *pEnv->pnRangeCur = nCur; } /* 0x5EDD8 */
        }
    }
}

/* One site's inbound craft pair, the term Base_CountInboundTransfers sums. Both bytes are read with
 * LDRSB and added as signed bytes (0x67FB8 / 0x67FC0, and 0x68014 / 0x6801C in the scan). */
static i32 SmapSite_CraftLoad(const SmapSite *pSite)
{
    return (i32)(i8)pSite->bRecoveredCraftCount + (i32)(i8)pSite->bUnk518;
}

/* Base_CountInboundTransfers @ 0x67F78. The craft-side mirror of Base_CountOutboundTransferSpace:
 * same shape, same type-match rule against apBases[nBaseIndex]->bUnk4, different per-site term. The
 * original's first argument (&0x159768 again) is never read. */
i32 Base_CountInboundTransfers(const SmapSite *aSites, i32 nSiteCount, i32 nSiteIdx,
                               BaseRec *const *apBases, i32 nBaseIndex, bool bScanSites)
{
    i32 nTotal = 0;

    if (aSites == NULL || apBases == NULL) {
        return 0;
    }
    if (nSiteIdx >= 0 && nSiteIdx < PUFO_SMAP_SITE_COUNT) {
        nTotal += SmapSite_CraftLoad(&aSites[nSiteIdx]); /* 0x67FB0 */
    }
    if (bScanSites && nSiteCount > 0) { /* 0x67FDC: CMP R8,#0 / BLE */
        if (nBaseIndex >= 0 && nBaseIndex < PUFO_FAC_BASE_COUNT) {
            const i8 bBaseType = (i8)apBases[nBaseIndex]->bUnk4;
            for (i32 i = 0; i < nSiteCount; ++i) {
                if (i == nSiteIdx) {
                    continue; /* 0x67FEC: CMP R4,R1 / BEQ */
                }
                if (bBaseType == (i8)aSites[i].bFlag1) {
                    nTotal += SmapSite_CraftLoad(&aSites[i]); /* 0x68020 */
                }
            }
        }
    }
    return nTotal;
}

/* Base_HitTestFacilityStrip @ 0x671B0. The vertical base-tab strip down the right edge of the
 * geoscape; see the header for the geometry and for the pitch/divisor mismatch the original has. */
i32 Base_HitTestFacilityStrip(const BaseStripHitEnv *pEnv, i32 nX, i32 nY)
{
    if (pEnv == NULL) {
        return 0;
    }

    /* 0x6723C: any of the three screens being open overwrites the x argument with 10 and re-enters
     * the compare chain at 0x671E8, so the x range test always misses. */
    const bool bGateOpen = (pEnv->pbGateTransfer != NULL && (*pEnv->pbGateTransfer & 0xFFu) != 0) ||
                           (pEnv->pbGateBaseInfo != NULL && (*pEnv->pbGateBaseInfo & 0xFFu) != 0) ||
                           (pEnv->pbGatePsi != NULL && (*pEnv->pbGatePsi & 0xFFu) != 0);
    const i32 nTestX = bGateOpen ? 10 : nX;

    const i32 nBaseCount = (pEnv->pbBaseCount != NULL) ? (i8)*pEnv->pbBaseCount : 0;   /* 0x67204 */
    const i8 bSelected = (pEnv->pbSelectedBase != NULL) ? (i8)*pEnv->pbSelectedBase : 0; /* 0x67224 */

    if (nTestX < PUFO_BASE_STRIP_X_FIRST || nTestX > PUFO_BASE_STRIP_X_LAST ||
        nY < PUFO_BASE_STRIP_Y_FIRST || nY > PUFO_BASE_STRIP_PITCH * nBaseCount) {
        return bSelected;
    }

    if (pEnv->pbTapFlag != NULL) {
        *pEnv->pbTapFlag = 0; /* 0x67250 STRB */
    }
    /* The LSL#24 / ASR#24 pair at 0x67268 sign-extends the quotient to a char. */
    return (i8)(nY / PUFO_BASE_STRIP_DIVISOR); /* 0x67264 */
}

/* Base_DrawFacilityIconStrip @ 0x6250c. The drawer of the same strip; see the header for the
 * geometry, the two byte arrays it reads and the chevron. */
void Base_DrawFacilityIconStrip(const BaseStripDrawEnv *pEnv)
{
    if (pEnv == NULL || pEnv->abFacilityGrid == NULL) {
        return;
    }

    const u8 *const abGrid = pEnv->abFacilityGrid;
    const i32 nBaseCount = (pEnv->pbBaseCount != NULL) ? (i8)*pEnv->pbBaseCount : 0;
    const i8 bSelected   = (pEnv->pbSelectedBase != NULL) ? (i8)*pEnv->pbSelectedBase : 0;

    /* 0x62520..0x6257C: the erase rect over the selected cell, only when the selection is not the
     * count itself. */
    if ((i32)bSelected != nBaseCount) {
        SoftSurf_BlitRectProxy(0, PUFO_BASE_STRIP_X_FIRST,
                               PUFO_BASE_STRIP_PITCH * (i32)bSelected + PUFO_BASE_STRIP_Y_FIRST,
                               26, 26, 0xFFu, 0xFFu, 0x64u);
    }

    if (nBaseCount <= 0) {
        return; /* 0x62590 BLE */
    }

    for (i32 nBase = 0; nBase < nBaseCount; ++nBase) {
        const i32 nYBase = PUFO_BASE_STRIP_PITCH * nBase;
        const i32 nDays  = PUFO_GRID_DAYS_STRIDE * nBase;
        const i32 nFac   = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase;

        for (i32 nRow = 0; nRow < PUFO_STRIP_CELL_COUNT; ++nRow) {
            const i32 nY = nYBase + PUFO_STRIP_ROW_Y_FIRST + 4 * nRow;

            for (i32 nCol = 0; nCol < PUFO_STRIP_CELL_COUNT; ++nCol) {
                const i32 nCell = 6 * nRow + nCol;
                const i32 nX = PUFO_STRIP_CELL_X_FIRST + PUFO_STRIP_CELL_PITCH * nCol;

                if (abGrid[nDays + nCell] != 0) { /* 0x6260C: under construction */
                    SoftSurf_BlitRectProxy(0, nX, nY, 4, 4, 0x96u, 0u, 0u);
                    SoftSurf_BlitRectProxy(0, nX + 1, nY + 1, 2, 2, 0xFFu, 0u, 0u);
                } else {                          /* 0x62660: finished */
                    SoftSurf_BlitRectProxy(0, nX, nY, 4, 4, 0u, 0x96u, 0u);
                    SoftSurf_BlitRectProxy(0, nX + 1, nY + 1, 2, 2, 0u, 0xFFu, 0u);
                }

                if (abGrid[nFac + PUFO_GRID_TILEA_OFF + nCell] == 0) { /* 0x626B8: no tile */
                    SoftSurf_BlitRectProxy(0, nX, nY, 4, 4, 0x14u, 0x14u, 0x14u);
                    SoftSurf_BlitRectProxy(0, nX + 1, nY + 1, 2, 2, 0x28u, 0x28u, 0x28u);
                }

                /* 0x62720: the chevron, inside the per-cell loop, so the original paints it once
                 * per cell of the marked base. */
                if (nBase == pEnv->nHighlightRow) {
                    for (i32 k = 0; k < PUFO_STRIP_HILITE_STEPS; ++k) {
                        SoftSurf_BlitRectProxy(0, PUFO_STRIP_CELL_X_FIRST + k, nYBase + 4 + k,
                                               2, 2, 0xFFu, 0u, 0u);
                        SoftSurf_BlitRectProxy(0, PUFO_STRIP_CELL_X_FIRST + k, nYBase + 26 - k,
                                               2, 2, 0xFFu, 0u, 0u);
                    }
                }
            }
        }
    }
}

/* RadarScreen_BlitMapBackdrop @ 0x5eb10. See the header for the evidence map. */
bool RadarScreen_BlitMapBackdrop(const RadarBackdropEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pbBlinkPhase == NULL) {
        return false;
    }
    if (*pEnv->pbBlinkPhase == 0) {
        *pEnv->pbBlinkPhase = 1; /* 0x5EB28 */
        return false;
    }
    *pEnv->pbBlinkPhase = 0; /* 0x5EB38 */

    /* The original reaches all five through globals that are always live. The port takes them
     * as arguments, so a missing one is a port-side guard, not original behaviour: nothing is
     * drawn and the call reports that. */
    if (pEnv->pView == NULL || pEnv->pMapPad == NULL || pEnv->pLayerPad == NULL
        || pEnv->pWorldBits == NULL) {
        return false;
    }

    /* 0x5EB44..0x5EB68: map half-extent minus screen half-extent. The ADDMI/ASR pair the ARM
     * uses is C division by two, so a negative extent rounds toward zero. */
    const i32 nHalfW = pEnv->pView->nMapW / 2 - PUFO_RADAR_HALF_SCREEN_X; /* R7 */
    const i32 nHalfH = pEnv->pView->nMapH / 2 - PUFO_RADAR_HALF_SCREEN_Y; /* R6 */

    /* 0x5EB6C..0x5EB94: the map pad centred on the scroll position, negated. */
    pEnv->pMapPad->nOriginX = -(pEnv->pView->nScrollX + nHalfW);
    pEnv->pMapPad->nOriginY = -(pEnv->pView->nScrollY + nHalfH);
    PadImage_Blit(pEnv->pMapPad);

    /* 0x5EB98..0x5EBCC: the same pad one map width further on, which is the wrap copy. The
     * original rewrites nOriginY with the identical value here; reproduced as written. */
    pEnv->pMapPad->nOriginX = -(pEnv->pView->nScrollX + pEnv->pView->nMapW + nHalfW);
    pEnv->pMapPad->nOriginY = -(pEnv->pView->nScrollY + nHalfH);
    PadImage_Blit(pEnv->pMapPad);

    /* 0x5EBD0..0x5EC08: the layer pad takes the overlay origin, not the pad origin, on x; on y
     * it copies whatever the map pad ended with. */
    pEnv->pLayerPad->nOriginX = pEnv->nOverlayOriginX - pEnv->pView->nScrollX - nHalfW;
    pEnv->pLayerPad->nOriginY = pEnv->pMapPad->nOriginY;
    if (pEnv->nScreenId != PUFO_RADAR_SCREEN_NO_WRAP) { /* 0x5EC14 */
        SoftSurf_BlitFlip(pEnv->pLayerPad->nOriginX, pEnv->pLayerPad->nOriginY,
                          pEnv->pWorldBits, g_aPalette[PUFO_RADAR_MAP_PAL_SLOT],
                          pEnv->pLayerPad->pData);
    }

    /* 0x5EC50..0x5EC88: one map width to the left. */
    pEnv->pLayerPad->nOriginX =
        pEnv->nOverlayOriginX - pEnv->pView->nScrollX - pEnv->pView->nMapW - nHalfW;
    pEnv->pLayerPad->nOriginY = pEnv->pMapPad->nOriginY;
    if (pEnv->nScreenId != PUFO_RADAR_SCREEN_NO_WRAP) { /* 0x5EC6C */
        SoftSurf_BlitFlip(pEnv->pLayerPad->nOriginX, pEnv->pLayerPad->nOriginY,
                          pEnv->pWorldBits, g_aPalette[PUFO_RADAR_MAP_PAL_SLOT],
                          pEnv->pLayerPad->pData);
    }

    /* 0x5ECA8..0x5ECE0: one map width to the right. The ARM's order is bias - scroll - halfW,
     * then + nMapW (0x5ECA0 ADD R3, R3, R0). */
    pEnv->pLayerPad->nOriginX =
        pEnv->nOverlayOriginX - pEnv->pView->nScrollX - nHalfW + pEnv->pView->nMapW;
    pEnv->pLayerPad->nOriginY = pEnv->pMapPad->nOriginY;
    if (pEnv->nScreenId != PUFO_RADAR_SCREEN_NO_WRAP) { /* 0x5ECC4 */
        SoftSurf_BlitFlip(pEnv->pLayerPad->nOriginX, pEnv->pLayerPad->nOriginY,
                          pEnv->pWorldBits, g_aPalette[PUFO_RADAR_MAP_PAL_SLOT],
                          pEnv->pLayerPad->pData);
    }

    return true;
}

/* 0x822EC..0x82304 and 0x8230C..0x8231C: MOVS / ADMI #1 / ASR #1. The +1 is skipped only when the
 * size is 0, so this is ceil(n / 2) for a positive size and C's n / 2 for a negative one. */
static i32 SmapIcon_HalfExtent(i32 nSize, i32 nSub)
{
    return (nSize != 0 ? nSize + 1 : 0) / 2 - nSub;
}

/* Smap_DrawStrategIcons @ 0x822c4. */
void Smap_DrawStrategIcons(const SmapView *pView, const SmapIconDrawEnv *pEnv)
{
    i32 nHalfW;
    i32 nHalfH;
    i32 i;

    if (pView == NULL || pEnv == NULL || pEnv->pStrategList == NULL) {
        return;
    }

    nHalfW = SmapIcon_HalfExtent(pView->nMapW, PUFO_RADAR_HALF_SCREEN_X);  /* 0x82304 */
    nHalfH = SmapIcon_HalfExtent(pView->nMapH, PUFO_RADAR_HALF_SCREEN_Y);  /* 0x8231C */
    g_nGlyphPalSlotBase = PUFO_GLYPH_PAL_STRATEG_ICONS;                    /* 0x82318 / 0x82328 */

    if (PtrArray_GetCount(pEnv->pStrategList) <= 0) {                      /* 0x82334 / 0x8233C */
        return;
    }

    /* 0x826D0..0x826DC re-reads the count every step and tests (index + 1) against it, so the bound
     * is taken fresh each iteration exactly as the original does. */
    for (i = 0; i < PtrArray_GetCount(pEnv->pStrategList); ++i) {
        const StrategItem *pItem = (const StrategItem *)PtrArray_Get(pEnv->pStrategList, i);
        i32 nX;
        i32 nY;
        i32 nScreenX;
        i32 nScreenY;
        i32 nGlyph = 0;
        bool bDraw = false;
        i32 nAnim = (i32)pEnv->nAnimPhase;
        i32 nBase = (i32)pEnv->nMapBitsBase;

        if (pItem == NULL) {
            continue; /* deviation: the original would dereference the null entry at 0x8236C */
        }

        nX = pItem->nX;                                                    /* 0x8236C */
        nY = pItem->nY;                                                    /* 0x8237C */
        if (pView->nMode == 1) {                                           /* 0x82388 */
            nX = (i32)((double)nX * PUFO_SMAP_ITEM_SCALE_L1);              /* 0x823B4, dtoi truncates */
            nY = (i32)((double)nY * PUFO_SMAP_ITEM_SCALE_L1);              /* 0x823F4 */
        } else if (pView->nMode == 2) {                                    /* 0x82410 */
            nX = (i32)((double)nX * PUFO_SMAP_ITEM_SCALE_L2);              /* 0x8243C */
            nY = (i32)((double)nY * PUFO_SMAP_ITEM_SCALE_L2);              /* 0x8247C */
        }

        nScreenX = nX - pView->nScrollX - nHalfW;                          /* 0x824A4 */
        nScreenY = nY - pView->nScrollY - nHalfH;                          /* 0x824B4 */
        if (nScreenY > pView->nMapH) {                                     /* 0x824BC / 0x824C0 */
            nScreenY -= pView->nMapH;
        }
        if (nScreenX > pView->nMapW) {                                     /* 0x824CC / 0x824D0 */
            nScreenX -= pView->nMapW;
        }

        switch ((i8)pItem->nType) {                                        /* 0x824DC LDRSB / 0x824EC */
        case 1:
            if (pItem->bActive != 0) {
                if (pItem->bVisible != 0) {
                    if (pItem->nTimer != 0) {                              /* 0x82618 */
                        nGlyph = nBase + nAnim + 12;                       /* 0x8263C..0x82640 */
                    } else {
                        nGlyph = nBase + 4;                                /* 0x82620, no anim phase */
                    }
                    bDraw = true;
                }
            } else if (pItem->bVisible != 0) {                             /* 0x825C8 */
                nGlyph = nBase + 2;                                        /* 0x825DC, no anim phase */
                bDraw = true;
            }
            break;
        case 3:
            nGlyph = nBase + nAnim + 8;                                    /* 0x82594 */
            bDraw = true;
            break;
        case 4:
            nGlyph = nBase + nAnim + 10;                                   /* 0x8256C */
            bDraw = true;
            break;
        case 5:
            nGlyph = nBase + nAnim + 14;                                   /* 0x82528 */
            bDraw = true;
            break;
        case 0:
            if (pItem->bActive != 0) {
                if (pItem->bVisible != 0) {                                /* 0x826A0 */
                    nGlyph = nBase + nAnim + 6;                            /* 0x826C4 */
                    bDraw = true;
                }
            } else {
                nGlyph = nBase + nAnim;                                    /* 0x82684 */
                bDraw = true;
            }
            break;
        default:
            break;                                                         /* 0x824EC default */
        }

        if (bDraw) {
            Font_DrawGlyph(nScreenX, nScreenY, (u16)nGlyph, 0);            /* nPal is R9 = 0 */
        }
    }
}

/* One digit pad of the clock strip: reposition, then blit with bFlag 0. The original indexes
 * apClockDigitPads with the digit straight out of the division, so a digit outside the ten is
 * undefined there; the port refuses it, as RadarDigitPad @ 0x5d918 does. */
static PadImage *Clock_SetPad(const GeoscapeClockDrawEnv *pEnv, i32 nDigit, i32 nX, i32 nY)
{
    PadImage *pPad;

    if (nDigit < 0 || nDigit >= PUFO_GEOUI_CLOCK_DIGIT_PADS) {
        return NULL;
    }
    pPad = pEnv->apDigitPads[nDigit];
    if (pPad == NULL) {
        return NULL;
    }
    pPad->nOriginX = nX;
    pPad->nOriginY = nY;
    return pPad;
}

static void Clock_BlitDigit(const GeoscapeClockDrawEnv *pEnv, i32 nDigit, i32 nX, i32 nY)
{
    PadImage *pPad = Clock_SetPad(pEnv, nDigit, nX, nY);

    if (pPad != NULL) {
        PadImage_BlitEx(pPad, 0);
    }
}

/* 0x5C5F8..0x5C6E0: one two-digit group of the clock. The two halves come apart with _rt_sdiv,
 * divisor 10 in R0. */
static void Clock_BlitDigitPair(const GeoscapeClockDrawEnv *pEnv, i32 nValue,
                                i32 nTensX, i32 nOnesX, i32 nY)
{
    Clock_BlitDigit(pEnv, nValue / 10, nTensX, nY);
    Clock_BlitDigit(pEnv, nValue % 10, nOnesX, nY);
}

/* The original copies the record with an unbounded byte loop into a 52-byte stack buffer
 * (psz[52] at 0x5C5AC). The port caps the copy at that same 52 bytes. */
static void Clock_CopyText(char *pszDst, const char *pRec)
{
    i32 i = 0;

    while (i < 51 && pRec[i] != 0) {
        pszDst[i] = pRec[i];
        ++i;
    }
    pszDst[i] = 0;
}

/* Geoscape_DrawClockPads @ 0x5c5c8. */
void Geoscape_DrawClockPads(const GeoscapeClockDrawEnv *pEnv)
{
    char szText[52];
    PadImage *pTensPad;
    PadImage *pOnesPad;
    i32 nWidth;
    i32 nDayTens;
    i32 nDayOnes;

    if (pEnv == NULL || pEnv->apDigitPads == NULL || pEnv->pTime == NULL || pEnv->pTextRecs == NULL) {
        return;
    }

    szText[0] = 0;

    Clock_BlitDigitPair(pEnv, pEnv->pTime->nHour, 90, 100, 260);               /* 0x5C618..0x5C65C */
    Clock_BlitDigitPair(pEnv, pEnv->pTime->nMinute, 115, 125, 260);            /* 0x5C69C..0x5C6E0 */
    Clock_BlitDigitPair(pEnv, pEnv->pTime->nSecond, 140, 150, 260);            /* 0x5C720..0x5C764 */

    Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);                  /* 0x5C790 */
    Text_DrawString(110, 258, ":");                                             /* 0x5C7A8, asc_AB078 */
    Text_DrawString(135, 258, ":");                                             /* 0x5C7C0 */

    if (pEnv->pTime->nWeekday >= 1 && pEnv->pTime->nWeekday <= 7) {             /* 0x5C7D0 */
        Clock_CopyText(szText, pEnv->pTextRecs +
                       PUFO_TEXT_REC_STRIDE * (PUFO_TEXT_WEEKDAY_FIRST + pEnv->pTime->nWeekday - 1));
    }
    /* Outside 1..7 the original draws whatever its uninitialised buffer held; the port draws the
     * empty string it left there. */

    Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);                 /* 0x5C984 */
    nWidth = Text_MeasureWidth(szText);                                         /* 0x5C998 */
    Text_DrawString(124 - nWidth / 2, 272, szText);                             /* 0x5C9B0 */

    nDayTens = pEnv->pTime->nDay / 10;                                          /* 0x5C9D4 */
    nDayOnes = pEnv->pTime->nDay % 10;                                          /* 0x5C9E8 */
    pTensPad = Clock_SetPad(pEnv, nDayTens, 90, 287);                           /* 0x5C9FC / 0x5CA10 */
    if (pTensPad != NULL && nDayTens != 0) {                                    /* 0x5CA00 / 0x5CA14 */
        PadImage_BlitEx(pTensPad, 0);                                           /* 0x5CA18 */
    }
    pOnesPad = Clock_SetPad(pEnv, nDayOnes, 100, 287);                          /* 0x5CA28 / 0x5CA30 */
    if (pOnesPad != NULL) {
        PadImage_BlitEx(pOnesPad, 0);                                           /* 0x5CA38 */
    }

    Clock_CopyText(szText, pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_CLOCK_SEP); /* 0x5CA48 */
    Text_DrawString(110, 287, szText);                                          /* 0x5CA78 */

    if (pEnv->pTime->nMonth >= 1 && pEnv->pTime->nMonth <= 12) {               /* 0x5CA88 */
        Clock_CopyText(szText, pEnv->pTextRecs +
                       PUFO_TEXT_REC_STRIDE * (PUFO_TEXT_MONTH_FIRST + pEnv->pTime->nMonth - 1));
    }
    Text_DrawString(130, 287, szText);                                          /* 0x5CCFC */

    Clock_BlitDigit(pEnv, pEnv->pTime->nYear / 1000, 105, 304);                /* 0x5CD30 */
    Clock_BlitDigit(pEnv, pEnv->pTime->nYear / 100 % 10, 115, 304);            /* 0x5CD88 */
    Clock_BlitDigit(pEnv, pEnv->pTime->nYear / 10 % 10, 125, 304);             /* 0x5CDDC */
    Clock_BlitDigit(pEnv, pEnv->pTime->nYear % 10, 135, 304);                  /* 0x5CE14..0x5CE38 */
}

/* The original indexes aRadarDigitPads with the digit straight out of the division, so a digit
 * outside the table is undefined there. The port refuses that digit; see the note in the header. */
static PadImage *RadarDigitPad(const RadarFuelEnv *pEnv, i32 nDigit)
{
    if (nDigit < 0 || nDigit >= PUFO_RADAR_DIGIT_PADS) {
        return NULL;
    }
    return pEnv->apDigitPads[nDigit];
}

/* RadarScreen_DrawFuelGauge @ 0x5d918. */
i32 RadarScreen_DrawFuelGauge(const RadarFuelEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCraft == NULL || pEnv->pbOutOfFuel == NULL) {
        return 0;
    }

    /* 0x5D950..0x5D9D8: two _itod, _divd, _muld by 100.0, then _subd FROM 100.0, then _dtoi. Both
     * halves are read signed (LDRSH), uFuelMax included. A zero uFuelMax divides by zero in the
     * original; the port takes the ratio as zero, which lands on 100 and the out-of-fuel flag. */
    double dRatio = 0.0;
    if (pEnv->pCraft->uFuelMax != 0) {
        dRatio = (double)pEnv->pCraft->nFuel / (double)(i16)pEnv->pCraft->uFuelMax;
    }
    i32 n = (i32)(100.0 - 100.0 * dRatio);

    if (n >= 100) { /* 0x5D9E4..0x5D9F4 */
        n = 100;
        *pEnv->pbOutOfFuel = 1;
    }

    const i32 nHundreds = n / 100;                       /* 0x5DA08 */
    const i32 nTens = (n - 100 * nHundreds) / 10;        /* 0x5DA24..0x5DA34 */
    const i32 nUnits = n % 10;                           /* 0x5DA38..0x5DA48 */

    PadImage *pHundreds = RadarDigitPad(pEnv, nHundreds);
    if (pHundreds != NULL) {
        pHundreds->nOriginX = PUFO_RADAR_FUEL_X_HUNDREDS; /* 0x5DA5C..0x5DA60 */
        pHundreds->nOriginY = PUFO_RADAR_FUEL_Y;          /* 0x5DA74 */
        if (nHundreds != 0) {                             /* 0x5DA6C */
            PadImage_BlitEx(pHundreds, PUFO_RADAR_PAD_KEY);
        }
    }

    PadImage *pTens = RadarDigitPad(pEnv, nTens);
    if (pTens != NULL) {
        pTens->nOriginX = PUFO_RADAR_FUEL_X_TENS; /* 0x5DA88..0x5DA90 */
        pTens->nOriginY = PUFO_RADAR_FUEL_Y;      /* 0x5DA98 */
        /* 0x5DA9C skips the blit when the tens digit is non-zero and 0x5DAA4 skips it when the
         * hundreds digit is not 1, so a zero tens digit is drawn only at 100%. */
        if (nTens != 0 || nHundreds == 1) {
            PadImage_BlitEx(pTens, PUFO_RADAR_PAD_KEY);
        }
    }

    PadImage *pUnits = RadarDigitPad(pEnv, nUnits);
    if (pUnits != NULL) {
        pUnits->nOriginX = PUFO_RADAR_FUEL_X_UNITS; /* 0x5DABC..0x5DAC4 */
        pUnits->nOriginY = PUFO_RADAR_FUEL_Y;       /* 0x5DACC */
        PadImage_BlitEx(pUnits, PUFO_RADAR_PAD_KEY); /* 0x5DAD4, unconditional */
    }

    PadImage *pPercent = RadarDigitPad(pEnv, PUFO_RADAR_DIGIT_PADS - 1);
    if (pPercent != NULL) {
        pPercent->nOriginX = PUFO_RADAR_FUEL_X_PERCENT; /* 0x5DADC..0x5DAE4 */
        pPercent->nOriginY = PUFO_RADAR_FUEL_Y;         /* 0x5DAEC */
        PadImage_BlitEx(pPercent, PUFO_RADAR_PAD_KEY);  /* tail call */
    }

    return n;
}

/* RadarScreen_DrawRangeBar @ 0x5d6e4. */
bool RadarScreen_DrawRangeBar(const RadarRangeBarEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pInterceptItem == NULL || pEnv->pCraft == NULL
        || pEnv->pnRangeCur == NULL || pEnv->pnPulseTimer == NULL) {
        return false;
    }

    const i32 nClass = (i8)pEnv->pInterceptItem->bCraftClass; /* 0x5D704 LDRSB */
    if (nClass == 0) {
        return false; /* 0x5D708 */
    }

    const i32 nRangeCur = *pEnv->pnRangeCur;
    /* 0x5D70C..0x5D74C: the level term is always negative for the levels Init stores, so a higher
     * class loosens the gate. C division matches _rt_sdiv's truncation toward zero. */
    const i32 nLevelAdj =
        (PUFO_RADAR_RANGE_SCALE * (pEnv->nCraftLevel - PUFO_RADAR_LEVEL_BIAS))
        / PUFO_RADAR_RANGE_PITCH;
    if (nRangeCur + nLevelAdj > pEnv->nRangeLimit) {
        return false; /* 0x5D74C BGT */
    }
    if (*pEnv->pnPulseTimer != PUFO_RADAR_PULSE_ARMED) {
        return false; /* 0x5D75C */
    }

    /* 0x5D764..0x5D78C: the bar's height is the range scaled to the 114-pixel pitch. */
    const i32 nBar = (PUFO_RADAR_RANGE_PITCH * nRangeCur) / PUFO_RADAR_RANGE_SCALE;
    SoftSurf_BlitRectProxy(0, PUFO_RADAR_RANGE_BAR_X, PUFO_RADAR_RANGE_BAR_Y - nBar,
                           3, nBar - PUFO_RADAR_RANGE_TRIM, 0x28u, 0x64u, 0u);
    SoftSurf_BlitRectProxy(0, PUFO_RADAR_RANGE_BAR_X + 1, PUFO_RADAR_RANGE_BAR_Y - nBar,
                           1, nBar - PUFO_RADAR_RANGE_TRIM, 0x50u, 0xDCu, 0u);

    /* 0x5D808..0x5D814: the roll is taken when it lands below 70, so 70% of pulses hit the craft. */
    if (Game_RandU16(100u) < PUFO_RADAR_FIRE_CHANCE) {
        i32 nDelta = 0;
        bool bFired = true;
        switch (nClass) {
            case 1: case 2: nDelta = (i32)Game_RandU16(8u)   - 20;  break; /* 0x5D8D0 */
            case 3: case 4: nDelta = (i32)Game_RandU16(10u)  - 40;  break; /* 0x5D8BC */
            case 5:         nDelta = (i32)Game_RandU16(30u)  - 120; break; /* 0x5D8A8 */
            case 6:         nDelta = (i32)Game_RandU16(20u)  - 80;  break; /* 0x5D894 */
            case 7:         nDelta = (i32)Game_RandU16(30u)  - 140; break; /* 0x5D86C */
            default:        bFired = false;                                  break; /* 0x5D85C */
        }
        if (bFired) {
            pEnv->pCraft->nFuel = (i16)(pEnv->pCraft->nFuel + nDelta); /* 0x5D8E0..0x5D8FC */
        }
    }

    /* 0x5D900..0x5D90C: reached on every path that got past the pulse gate. */
    *pEnv->pnPulseTimer = PUFO_RADAR_PULSE_NEXT;
    return true;
}

/* ------------------------------------------------- RadarScreen_Init @ 0x5f490 and friends
 * See the header for the open/close map and for the class table's provenance. */

const RadarUfoClass g_aRadarUfoClass[PUFO_RADAR_CLASS_COUNT] = {
    /* art     flash   missile  lvl  hp   palette  overlay  limit  speed */
    { "ufo_4", "ufo_4f", "ufo_4m", 8,    50, "85.pal", "85", 240, 36.6666 },
    { "ufo_3", "ufo_3f", "ufo_3m", 6,   200, "86.pal", "86", 360, 40.0    },
    { "ufo_3", "ufo_3f", "ufo_3m", 6,   250, "87.pal", "87", 360, 45.0    },
    { "ufo_2", "ufo_2f", "ufo_2m", 4,   500, "88.pal", "88", 400, 66.6666 },
    { "ufo_2", "ufo_2f", "ufo_2m", 4,   500, "89.pal", "89", 400, 71.0    },
    { "ufo_1", "ufo_1f", "ufo_1m", 2,  1200, "90.pal", "90", 480, 80.0    },
    { "ufo_1", "ufo_1f", "ufo_1m", 2,  2200, "92.pal", "92", 480, 53.6666 },
    { "ufo_0", "ufo_0f", "ufo_0m", 0,  3000, "91.pal", "91", 480, 83.3333 },
};

/* aRadarCtlDesc @ 0xAB2A0, stride 0x20. The offsets are the same five origins the button pads
 * get at 0x5FDB8..0x5FE3C; entries 0..3 name pads "i_but0"/"i_but1" and entry 4 names
 * "i_but7"/"i_but8". UiControl_Setup loads those into the control's own pad slots in mode 1. */
static const UiControlDesc g_aRadarCtlDesc[PUFO_RADAR_CONTROLS] = {
    { 117,  86, "i_but0", "i_but1", "", 0 },
    { 161,  86, "i_but0", "i_but1", "", 0 },
    { 117, 107, "i_but0", "i_but1", "", 0 },
    { 161, 107, "i_but0", "i_but1", "", 0 },
    { 117, 128, "i_but7", "i_but8", "", 0 },
};

/* The five button pads in the order RadarScreen_Init loads them (0x5FD4C..0x5FDB4) and the origins
 * it writes straight after (0x5FDB8..0x5FE3C). Note the name order is 3, 4, 2, 5, 6. */
static const struct {
    const char *pszName;
    i32         nOriginX;
    i32         nOriginY;
} g_aRadarButtonLoad[PUFO_RADAR_BUTTON_PADS] = {
    { "i_but3", 117,  86 }, { "i_but4", 161,  86 }, { "i_but2", 117, 107 },
    { "i_but5", 161, 107 }, { "i_but6", 117, 128 },
};

/* The eleven digit pads: "i0".."i9" then the percent pad (0x5FE88..0x5FEFC). */
static const char *const g_aRadarDigitName[PUFO_RADAR_DIGIT_PADS] = {
    "i0", "i1", "i2", "i3", "i4", "i5", "i6", "i7", "i8", "i9", "perc"
};

/* operator new(0x14) + PadImage_ctor, storing NULL when the allocation fails (0x5F508). */
static PadImage *Radar_NewPad(void)
{
    PadImage *pPad = (PadImage *)malloc(sizeof(PadImage));
    if (pPad == NULL) {
        return NULL;
    }
    PadImage_ctor(pPad);
    return pPad;
}

/* operator new(0x20) + UiControl_ctor (0x5FC6C). */
static UiControl *Radar_NewCtl(void)
{
    UiControl *pCtl = (UiControl *)malloc(sizeof(UiControl));
    if (pCtl == NULL) {
        return NULL;
    }
    UiControl_ctor(pCtl);
    return pCtl;
}

/* PadImage_FreeData then operator delete, only when the pointer is non-NULL (0x60158..0x6028C).
 * Deviation: the original leaves the freed pointer in its global; the port NULLs it. */
static void Radar_FreePad(PadImage **ppPad)
{
    if (*ppPad != NULL) {
        PadImage_FreeData(*ppPad);
        free(*ppPad);
        *ppPad = NULL;
    }
}

static Ship *Radar_InterceptCraft(const RadarSessionEnv *pEnv)
{
    const i32 nIdx = *pEnv->pnShipIdx;
    if (nIdx < 0 || nIdx >= pEnv->nCraftCount) {
        return NULL;
    }
    return pEnv->apCraft[nIdx];
}

/* 0x5FB18..0x5FB40 and 0x5FB88..0x5FBB0: the mount kind picks the ammo byte. Kind 28 is the
 * unlimited mount and gives -1; any other kind leaves the -2 that was stored before the switch. */
static i32 Radar_MountAmmo(const Ship *pShip, i8 nMount)
{
    switch (nMount) {
        case 25: return (i32)pShip->bAmmo25;
        case 26: return (i32)pShip->bAmmo26;
        case 27: return (i32)pShip->bAmmo27;
        case 28: return PUFO_RADAR_AMMO_UNLIMITED;
        case 29: return (i32)pShip->bAmmo29;
        case 30: return (i32)pShip->bAmmo30;
        default: return PUFO_RADAR_AMMO_NONE;
    }
}

/* 0x5FFF4..0x6005C and 0x60098..0x60144: the write-back on close. The store is STRB, so the value
 * truncates to a byte. Kind 28 and any other kind write nothing. */
static void Radar_WriteBackAmmo(Ship *pShip, i8 nMount, i32 nValue)
{
    switch (nMount) {
        case 25: pShip->bAmmo25 = (u8)nValue; break;
        case 26: pShip->bAmmo26 = (u8)nValue; break;
        case 27: pShip->bAmmo27 = (u8)nValue; break;
        case 29: pShip->bAmmo29 = (u8)nValue; break;
        case 30: pShip->bAmmo30 = (u8)nValue; break;
        default: break;
    }
}

void RadarScreen_Init(const RadarSessionEnv *pEnv)
{
    RadarUiState *pUi;
    Ship *pCraft;
    StrategItem *pItem;
    i32 i;

    /* Port-side guard: the original reaches every member through a live global address. */
    if (pEnv == NULL || pEnv->pUi == NULL || pEnv->pbActive == NULL || pEnv->pbOutOfFuel == NULL
        || pEnv->pbBlinkPhase == NULL || pEnv->pbFlip == NULL || pEnv->pbTraceLost == NULL
        || pEnv->pbInterceptDone == NULL || pEnv->pbAnimStarted == NULL || pEnv->pnMsgTimer == NULL
        || pEnv->pnTargetHp == NULL || pEnv->pnTargetHpMax == NULL || pEnv->pnCraftLevel == NULL
        || pEnv->pnRangeLimit == NULL || pEnv->pnRangeTarget == NULL || pEnv->pnRangeCur == NULL
        || pEnv->pnAmmoL == NULL || pEnv->pnAmmoR == NULL || pEnv->pnCursorX == NULL
        || pEnv->pnCursorY == NULL || pEnv->pnShipIdx == NULL || pEnv->pnStrategIdx == NULL
        || pEnv->apCraft == NULL || pEnv->pStrategList == NULL) {
        return;
    }
    pUi = pEnv->pUi;

    if (*pEnv->pbActive != 0) {
        /* ------------------------------------------------ the close path, 0x5FF88..0x60298 */
        pCraft = Radar_InterceptCraft(pEnv);
        if (pCraft != NULL) {
            const i8 nMountL = pCraft->bWeaponL;
            const i8 nMountR = pCraft->bWeaponR;
            if (nMountL == nMountR) {
                /* 0x5FFA8: one pool shared by two identical mounts, so the split sums back. */
                Radar_WriteBackAmmo(pCraft, nMountL, *pEnv->pnAmmoL + *pEnv->pnAmmoR);
            } else {
                Radar_WriteBackAmmo(pCraft, nMountL, *pEnv->pnAmmoL); /* 0x60060..0x600D0 */
                Radar_WriteBackAmmo(pCraft, nMountR, *pEnv->pnAmmoR); /* 0x600D4..0x60144 */
            }
        }
        *pEnv->pbActive = 0; /* 0x60148 */

        for (i = 0; i < PUFO_RADAR_DIGIT_PADS; ++i) {
            Radar_FreePad(&pUi->apDigitPad[i]); /* 0x60158 */
        }
        for (i = 0; i < PUFO_RADAR_CONTROLS; ++i) {
            if (pUi->apCtl[i] != NULL) {
                UiControl_FreePads(pUi->apCtl[i]); /* 0x6018C */
                free(pUi->apCtl[i]);
                pUi->apCtl[i] = NULL;
            }
        }
        for (i = 0; i < PUFO_RADAR_BUTTON_PADS; ++i) {
            Radar_FreePad(&pUi->apButtonPad[i]); /* 0x601C0 */
        }
        Radar_FreePad(&pUi->pFramePad);      /* 0x6020C */
        Radar_FreePad(&pUi->pUfoPad);        /* 0x6022C */
        Radar_FreePad(&pUi->pUfoFlashPad);   /* 0x6024C */
        Radar_FreePad(&pUi->pUfoMissilePad); /* 0x6026C */
        Radar_FreePad(&pUi->pOverlayPad);    /* 0x6028C */

        /* 0x60290: only slot 1 is restored. Slot 2 keeps whatever the copy wrote over it. */
        Palette_Load("menu.pal", 1);
        return;
    }

    /* ------------------------------------------------ the open path, 0x5F498..0x5FE80 */
    Sound_Play(7); /* 0x5F49C; the MOV R1, #4 before it is dead */
    *pEnv->pbActive = 1;
    *pEnv->pbOutOfFuel = 0;   /* 0x5F4B0 */
    *pEnv->pbBlinkPhase = 0;  /* 0x5F4BC */
    *pEnv->pbFlip = 0;        /* 0x5F4C8 */
    *pEnv->pbTraceLost = 0;   /* 0x5F4D4 */
    *pEnv->pbInterceptDone = 0;  /* 0x5F4E0 */

    pUi->pFramePad = Radar_NewPad(); /* 0x5F508 */
    (void)PadImage_Load(pUi->pFramePad, "radar", PUFO_RADAR_FRAME_MODE);
    if (pUi->pFramePad != NULL) {
        pUi->pFramePad->nOriginX = PUFO_RADAR_FRAME_X; /* 0x5F54C */
        pUi->pFramePad->nOriginY = PUFO_RADAR_FRAME_Y; /* 0x5F554 */
    }
    pUi->pUfoPad = Radar_NewPad();         /* 0x5F560 */
    pUi->pUfoMissilePad = Radar_NewPad();  /* 0x5F5A8 */
    pUi->pUfoFlashPad = Radar_NewPad();    /* 0x5F5D8 */
    pUi->pOverlayPad = Radar_NewPad();     /* 0x5F5F8 */

    *pEnv->pnMsgTimer = PUFO_RADAR_MSG_TIMER_INIT; /* 0x5F60C */
    *pEnv->pnTargetHp = 0;                         /* 0x5F614 */

    /* 0x5F658: the eight-case switch, which is one flat table (see the header). */
    pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, *pEnv->pnStrategIdx);
    if (pItem != NULL) {
        const i32 nClass = (i8)pItem->bCraftClass;
        if (nClass >= 0 && nClass < PUFO_RADAR_CLASS_COUNT) {
            const RadarUfoClass *pRow = &g_aRadarUfoClass[nClass];
            (void)PadImage_Load(pUi->pUfoPad, pRow->pszArt, PUFO_RADAR_UFO_MODE);
            (void)PadImage_Load(pUi->pUfoFlashPad, pRow->pszFlash, PUFO_RADAR_FLASH_MODE);
            (void)PadImage_Load(pUi->pUfoMissilePad, pRow->pszMissile, PUFO_RADAR_FLASH_MODE);
            *pEnv->pnCraftLevel = pRow->nLevel;
            *pEnv->pnTargetHp = pRow->nHp;
            *pEnv->pnTargetHpMax = pRow->nHp;
            Palette_Load(pRow->pszPalette, PUFO_RADAR_OVERLAY_MODE);
            (void)PadImage_Load(pUi->pOverlayPad, pRow->pszOverlay, PUFO_RADAR_OVERLAY_MODE);
            *pEnv->pnRangeLimit = pRow->nRangeLimit;
        }
    }

    /* 0x5FA44..0x5FA68: the overlay pad is centred on x 115, whatever the class did. */
    if (pUi->pOverlayPad != NULL) {
        pUi->pOverlayPad->nOriginX =
            PUFO_RADAR_OVERLAY_X_CENTRE - (i32)(pUi->pOverlayPad->uWidth >> 1);
        pUi->pOverlayPad->nOriginY = PUFO_RADAR_OVERLAY_Y;
    }

    /* 0x5FA74..0x5FAB8: -1 in StrategItem.nHp means this contact has not fought yet. */
    if (pItem != NULL) {
        if (pItem->nHp == PUFO_STRATEG_HP_FRESH) {
            pItem->nHp = *pEnv->pnTargetHp; /* 0x5FAB4 */
        } else {
            *pEnv->pnTargetHp = pItem->nHp;    /* 0x5FA9C */
            *pEnv->pnTargetHpMax = pItem->nHp; /* 0x5FAA8 */
        }
    }

    /* 0x5FAC8..0x5FC04: the two mounts' ammo, then the twin-mount split. */
    *pEnv->pnAmmoL = PUFO_RADAR_AMMO_NONE; /* 0x5FAD8 */
    *pEnv->pnAmmoR = PUFO_RADAR_AMMO_NONE; /* 0x5FB48 */
    pCraft = Radar_InterceptCraft(pEnv);
    if (pCraft != NULL) {
        *pEnv->pnAmmoL = Radar_MountAmmo(pCraft, pCraft->bWeaponL);
        *pEnv->pnAmmoR = Radar_MountAmmo(pCraft, pCraft->bWeaponR);
        if (pCraft->bWeaponR == pCraft->bWeaponL
            && *pEnv->pnAmmoL != PUFO_RADAR_AMMO_UNLIMITED
            && *pEnv->pnAmmoL != PUFO_RADAR_AMMO_NONE) {
            const i32 nPool = *pEnv->pnAmmoL;
            *pEnv->pnAmmoR = nPool / 2;                    /* 0x5FBE8 */
            *pEnv->pnAmmoL = nPool / 2 + (nPool % 2);      /* 0x5FBFC..0x5FC04 */
        }
    }

    *pEnv->pnRangeTarget = PUFO_RADAR_RANGE_SCALE; /* 0x5FC10 */
    *pEnv->pnRangeCur = PUFO_RADAR_RANGE_SCALE;    /* 0x5FC18 */
    if (pUi->pUfoPad != NULL) {
        pUi->pUfoPad->nOriginX = PUFO_RADAR_UFO_X; /* 0x5FC2C */
    }

    /* 0x5FC34..0x5FC4C: the overrun copy, reproduced at the original's count. The flat indexing is
     * how the ARM reaches it: one base pointer, +0x400 for the destination, +0x6800 for the source,
     * both advanced by 4. */
    {
        u32 *pFlat = &g_aPalette[0][0];
        for (i = 0; i < PUFO_RADAR_PAL_COPY_COUNT; ++i) {
            pFlat[PUFO_RADAR_PAL_DST_SLOT * PUFO_PAL_SIZE + i] =
                pFlat[PUFO_RADAR_PAL_SRC_SLOT * PUFO_PAL_SIZE + i];
        }
    }
    Palette_Load("base.pal", 3); /* 0x5FC58 */

    for (i = 0; i < PUFO_RADAR_CONTROLS; ++i) {
        pUi->apCtl[i] = Radar_NewCtl(); /* 0x5FC68 */
    }
    for (i = 0; i < PUFO_RADAR_CONTROLS; ++i) {
        if (pUi->apCtl[i] != NULL) {
            pUi->apCtl[i]->pfnClick = pEnv->apfnClick[i]; /* 0x5FCB8..0x5FCF4 */
        }
    }
    for (i = 0; i < PUFO_RADAR_BUTTON_PADS; ++i) {
        pUi->apButtonPad[i] = Radar_NewPad(); /* 0x5FCF8 */
    }
    for (i = 0; i < PUFO_RADAR_CONTROLS; ++i) {
        if (pUi->apCtl[i] != NULL) {
            UiControl_Setup(pUi->apCtl[i], 0, 0, &g_aRadarCtlDesc[i],
                            pEnv->pfnLoadPads, pEnv->pLoadPadsUser); /* 0x5FD4C */
        }
    }
    for (i = 0; i < PUFO_RADAR_BUTTON_PADS; ++i) {
        if (pUi->apButtonPad[i] != NULL) {
            (void)PadImage_Load(pUi->apButtonPad[i], g_aRadarButtonLoad[i].pszName,
                                PUFO_RADAR_BUTTON_MODE);
            pUi->apButtonPad[i]->nOriginX = g_aRadarButtonLoad[i].nOriginX;
            pUi->apButtonPad[i]->nOriginY = g_aRadarButtonLoad[i].nOriginY;
        }
    }

    *pEnv->pnCursorX = PUFO_RADAR_CURSOR_WEST;  /* 0x5FE28: MOV R0, #0x75 */
    *pEnv->pnCursorY = PUFO_RADAR_CURSOR_NORTH; /* 0x5FE30: MOV LR, #0x56 */

    for (i = 0; i < PUFO_RADAR_DIGIT_PADS; ++i) {
        pUi->apDigitPad[i] = Radar_NewPad(); /* 0x5FE44 */
    }
    for (i = 0; i < PUFO_RADAR_DIGIT_PADS; ++i) {
        if (pUi->apDigitPad[i] != NULL) {
            (void)PadImage_Load(pUi->apDigitPad[i], g_aRadarDigitName[i], PUFO_RADAR_DIGIT_MODE);
        }
    }
    *pEnv->pbAnimStarted = 0; /* 0x5FEFC */
}

void RadarScreen_SetActive(const RadarSessionEnv *pEnv, u8 bActive)
{
    const u8 b = (bActive != 0) ? 1 : 0; /* 0x61454: ANDS R3, R1, #0xFF */

    RadarScreen_Init(pEnv); /* 0x61458 and 0x61474: Init decides which half runs */
    if (pEnv != NULL && pEnv->pTime != NULL) {
        pEnv->pTime->bPaused = b; /* 0x61464 / 0x61480: STRB at 0x159618+4 */
    }
    if (pEnv != NULL && pEnv->pnFrameStyle != NULL) {
        *pEnv->pnFrameStyle = (i8)b; /* 0x6148C: STRB at pObj + 0x6E */
    }
}

u8 RadarScreen_RollDetection(const RadarDetectEnv *pEnv, i32 nChance)
{
    StrategItem *pItem;
    const Ship *pCraft;
    u8 bResult;

    /* Port-side guard: the original dereferences both lookups without a test. */
    if (pEnv == NULL || pEnv->pStrategList == NULL || pEnv->pnStrategIdx == NULL
        || pEnv->pnShipIdx == NULL || pEnv->apCraft == NULL || pEnv->pbTraceLost == NULL
        || pEnv->pbFlip == NULL || pEnv->pnRangeTarget == NULL || pEnv->pnMsgTimer == NULL
        || pEnv->pTextRecs == NULL || pEnv->pszMsg == NULL || pEnv->cchMsg == 0) {
        return 0;
    }

    if ((u16)Game_RandU16((u16)nChance) != 0) {
        bResult = *pEnv->pbTraceLost; /* 0x602C0 -> LABEL_14 */
    } else {
        pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, *pEnv->pnStrategIdx);
        if (pItem == NULL) {
            bResult = *pEnv->pbTraceLost;
        } else {
            const i32 nClass = (i8)pItem->bCraftClass; /* 0x602E0: LDRSB at +2 */
            if (nClass >= 0 && nClass < PUFO_RADAR_CLASS_COUNT) {
                pItem->dSpeed = g_aRadarUfoClass[nClass].dSpeed; /* 0x602E0..0x60404 */
            }
            pItem->dTrackA = pItem->dSpeed * PUFO_RADAR_SPEED_TRACK_A; /* 0x60448 */

            /* 0x60488..0x604C4: uFuelCap / 60 is the craft's own speed -- the same basis
             * StrategList_ProcessEvents uses for an inactive craft and Ship_BurnFuel burns by. */
            const i32 nShip = *pEnv->pnShipIdx;
            pCraft = (nShip >= 0 && nShip < pEnv->nCraftCount) ? pEnv->apCraft[nShip] : NULL;
            const double dCraftSpeed =
                (pCraft != NULL ? (double)pCraft->uFuelCap : 0.0) * PUFO_RADAR_SPEED_SCALE;

            if (!(dCraftSpeed < pItem->dSpeed)) {
                bResult = *pEnv->pbTraceLost; /* 0x604C4: ltd() == 0 */
            } else {
                *pEnv->pbFlip = 1;                              /* 0x604D4 */
                *pEnv->pbTraceLost = 1;                         /* 0x604E0 */
                *pEnv->pnRangeTarget = PUFO_RADAR_RANGE_SCALE;  /* 0x604E4 */
                bResult = 1;
            }
        }
    }

    if (bResult != 0) {
        *pEnv->pnMsgTimer = 0; /* 0x60530 */
        const char *pszSrc = (const char *)pEnv->pTextRecs
                           + PUFO_TEXT_REC_STRIDE * PUFO_RADAR_TEXT_TRACE_LOST;
        size_t i;
        for (i = 0; i + 1 < pEnv->cchMsg; ++i) { /* 0x6054C: bounded here, the original has no bound */
            pEnv->pszMsg[i] = pszSrc[i];
            if (pszSrc[i] == '\0') {
                return bResult;
            }
        }
        pEnv->pszMsg[pEnv->cchMsg - 1] = '\0';
    }
    return bResult;
}

/* ------------------------------------------------- the two weapon mounts on the radar screen
 * See the evidence block in pufo_geoscape.h for RadarScreen_DrawCraftArmament @ 0x5edf4. */

const RadarMountDraw g_aRadarMountDraw[PUFO_RADAR_MOUNT_COUNT] = {
    { WM_CANNON,    50, 140 }, /* 0x5EF20..0x5EF3C */
    { WM_STINGRAY,  48, 260 }, /* 0x5EF00..0x5EF18 */
    { WM_AVALANCHE, 49, 360 }, /* 0x5EECC..0x5EEF4 */
    { WM_LASER,     52, 180 }, /* 0x5EEA8..0x5EED0 */
    { WM_PLASMA,    53, 300 }, /* 0x5EE84..0x5EEAC */
    { WM_FUSION,    51, 420 }, /* 0x5EE5C..0x5EE80 */
};

/* 0x5EE34..0x5EE38 and 0x5F15C..0x5F160: the switch tests the id against 25..30 and everything else,
 * WM_PSI_AMP included, takes no row. */
static const RadarMountDraw *Radar_MountRow(i8 nMount)
{
    const i32 n = (i32)nMount - (i32)WM_CANNON;
    if (n < 0 || n >= PUFO_RADAR_MOUNT_COUNT) {
        return NULL;
    }
    return &g_aRadarMountDraw[n];
}

/* 0x5EF4C..0x5EF7C and 0x5F26C..0x5F29C: how far the radar reaches right now. The divide is
 * _rt_sdiv(divisor 114, dividend 560 * (nLevel - 26)) -- the dividend is zero or negative for every
 * level the class table produces, and C's truncation toward zero is the same one. */
static i32 Radar_RadarReach(i32 nRangeCur, i32 nLevel)
{
    return nRangeCur
         + (PUFO_RADAR_RANGE_SCALE * (nLevel - PUFO_RADAR_LEVEL_MAX)) / PUFO_RADAR_RANGE_PITCH;
}

/* 0x5EF84..0x5EFB0 and 0x5F2A4..0x5F2C4: the predicated store group. Out of gunshot clears the engage
 * byte (R8 is the function's zero register, MOV at 0x5EE24), forces the cooldown to 255 and drops the
 * in-flight projectile. In gunshot only sets the engage byte and leaves the cooldown alone. */
static void Radar_DrawMountGate(i32 nReach, i32 nBound, u8 *pbEngage,
                                i32 *pnCooldown, i32 *pnShotY)
{
    if (nReach > nBound) {
        *pbEngage = 0;
        *pnCooldown = PUFO_RADAR_COOLDOWN_READY;
        *pnShotY = 0;
    } else {
        *pbEngage = 1;
    }
}

/* 0x5F044..0x5F0D4 and 0x5F2D4..0x5F354: the ammo count, centred on the mount's bar. -2 draws nothing,
 * -1 draws the single '-' of asc_AB340, anything else the decimal digits.
 * Deviation: the original's j_itoa writes into the 52-byte buffer at sp+0x18 with no bound. */
static void Radar_DrawAmmoLabel(i32 nAmmo, i32 nCentreX, char *pszBuf, size_t cchBuf)
{
    if (nAmmo == PUFO_RADAR_AMMO_NONE) {
        return;
    }
    if (nAmmo == PUFO_RADAR_AMMO_UNLIMITED) {
        pszBuf[0] = '-';
        pszBuf[1] = '\0';
    } else {
        (void)snprintf(pszBuf, cchBuf, "%d", nAmmo);
    }
    const i32 nWidth = Text_MeasureWidth(pszBuf);
    Text_DrawString(nCentreX - nWidth / 2, PUFO_RADAR_AMMO_LABEL_Y, pszBuf);
}

void RadarScreen_DrawCraftArmament(const RadarArmamentEnv *pEnv)
{
    Ship *pCraft;
    const RadarMountDraw *pRow;
    i32 nBound, nBar, nReach, nX;
    char szNum[52]; /* var_34 @ sp+0x18, the j_itoa buffer */

    /* Port-side guard: the original reaches the craft through apCraft[nShipIdx] with no test at all
     * (LDRSB [R3, #0x22] at 0x5EE2C), so an out-of-range index faults there. */
    if (pEnv == NULL || pEnv->pnGlyphPalBase == NULL || pEnv->pnShipIdx == NULL
        || pEnv->apCraft == NULL || pEnv->pnRangeCur == NULL || pEnv->pnCraftLevel == NULL
        || pEnv->pnBoundL == NULL || pEnv->pnBoundR == NULL || pEnv->pbEngageL == NULL
        || pEnv->pbEngageR == NULL || pEnv->pnCooldownL == NULL || pEnv->pnCooldownR == NULL
        || pEnv->pnShotYL == NULL || pEnv->pnShotYR == NULL || pEnv->pnAmmoL == NULL
        || pEnv->pnAmmoR == NULL) {
        return;
    }

    *pEnv->pnGlyphPalBase = PUFO_RADAR_MOUNT_PAL_BASE; /* 0x5EE1C */

    const i32 nShip = *pEnv->pnShipIdx;
    pCraft = (nShip >= 0 && nShip < pEnv->nCraftCount) ? pEnv->apCraft[nShip] : NULL;
    if (pCraft == NULL) {
        return;
    }

    nReach = Radar_RadarReach(*pEnv->pnRangeCur, *pEnv->pnCraftLevel);

    /* ------------------------------------------------ the left mount, 0x5EE28..0x5F14C */
    pRow = Radar_MountRow(pCraft->bWeaponL);
    nBound = 0;
    if (pRow != NULL) {
        Font_DrawGlyph(PUFO_RADAR_MOUNT_GLYPH_X_L, PUFO_RADAR_MOUNT_GLYPH_Y,
                       (u16)(g_fontSys.nBaseBitsBase + pRow->nGlyphOff), 0);
        nBound = pRow->nRange;
    }
    *pEnv->pnBoundL = nBound; /* 0x5EF48 */

    Radar_DrawMountGate(nReach, nBound, pEnv->pbEngageL, pEnv->pnCooldownL, pEnv->pnShotYL);

    nBar = (PUFO_RADAR_RANGE_PITCH * nBound) / PUFO_RADAR_RANGE_SCALE; /* 0x5EFC4 */
    SoftSurf_BlitRectProxy(0, PUFO_RADAR_BAR_X_L, PUFO_RADAR_BAR_BASE_Y - nBar, 1,
                           nBar - PUFO_RADAR_BAR_TRIM,
                           PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                           PUFO_RADAR_BAR_COLOR_LOW);
    if (nBound > PUFO_RADAR_BAR_CAP_MIN_RANGE) { /* 0x5F004 */
        SoftSurf_BlitRectProxy(0, PUFO_RADAR_BAR_CAP_X_L, PUFO_RADAR_BAR_CAP_Y - nBar, 3, 3,
                               PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                               PUFO_RADAR_BAR_COLOR_LOW);
    }
    Radar_DrawAmmoLabel(*pEnv->pnAmmoL, PUFO_RADAR_BAR_X_L, szNum, sizeof(szNum));

    if (pCraft->bWeaponL != 0) { /* 0x5F0E8 */
        for (nX = PUFO_RADAR_TICK_X_FIRST_L; nX < PUFO_RADAR_TICK_LAST_X_L;
             nX += PUFO_RADAR_TICK_PITCH) {
            SoftSurf_BlitRectProxy(0, nX, PUFO_RADAR_BAR_BASE_Y - nBar, 2, 1,
                                   PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                                   PUFO_RADAR_BAR_COLOR_LOW);
        }
        SoftSurf_BlitRectProxy(0, PUFO_RADAR_TICK_CAP_X_L, PUFO_RADAR_BAR_CAP_Y - nBar, 3, 3,
                               PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                               PUFO_RADAR_BAR_COLOR_LOW);
    }

    /* ------------------------------------------------ the right mount, 0x5F150..0x5F44C
     * The label comes BEFORE the bar here, the opposite of the left half. */
    pRow = Radar_MountRow(pCraft->bWeaponR);
    nBound = 0;
    if (pRow != NULL) {
        Font_DrawGlyph(PUFO_RADAR_MOUNT_GLYPH_X_R, PUFO_RADAR_MOUNT_GLYPH_Y,
                       (u16)(g_fontSys.nBaseBitsBase + pRow->nGlyphOff), 0);
        nBound = pRow->nRange;
    }
    *pEnv->pnBoundR = nBound; /* 0x5F268 */

    Radar_DrawMountGate(nReach, nBound, pEnv->pbEngageR, pEnv->pnCooldownR, pEnv->pnShotYR);

    Radar_DrawAmmoLabel(*pEnv->pnAmmoR, PUFO_RADAR_BAR_X_R, szNum, sizeof(szNum));

    nBar = (PUFO_RADAR_RANGE_PITCH * nBound) / PUFO_RADAR_RANGE_SCALE; /* 0x5F374 */
    SoftSurf_BlitRectProxy(0, PUFO_RADAR_BAR_X_R, PUFO_RADAR_BAR_BASE_Y - nBar, 1,
                           nBar - PUFO_RADAR_BAR_TRIM,
                           PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                           PUFO_RADAR_BAR_COLOR_LOW);
    if (nBound > PUFO_RADAR_BAR_CAP_MIN_RANGE) { /* 0x5F3B0 */
        SoftSurf_BlitRectProxy(0, PUFO_RADAR_BAR_CAP_X_R, PUFO_RADAR_BAR_CAP_Y - nBar, 3, 3,
                               PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                               PUFO_RADAR_BAR_COLOR_LOW);
    }
    if (pCraft->bWeaponR != 0) { /* 0x5F3EC */
        for (nX = PUFO_RADAR_TICK_X_FIRST_R; nX < PUFO_RADAR_TICK_LAST_X_R;
             nX += PUFO_RADAR_TICK_PITCH) {
            SoftSurf_BlitRectProxy(0, nX, PUFO_RADAR_BAR_BASE_Y - nBar, 2, 1,
                                   PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                                   PUFO_RADAR_BAR_COLOR_LOW);
        }
        SoftSurf_BlitRectProxy(0, PUFO_RADAR_TICK_CAP_X_R, PUFO_RADAR_BAR_CAP_Y - nBar, 3, 3,
                               PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                               PUFO_RADAR_BAR_COLOR_LOW);
    }

    /* ------------------------------------------------ 0x5F45C..0x5F484: an empty mount inherits the
     * other mount's bound. The original reads the surviving bound into R0 first, so with both empty
     * the second copy just moves the first copy's value and both end at 0. */
    const i32 nInherited = (*pEnv->pnAmmoL == PUFO_RADAR_AMMO_NONE)
                             ? *pEnv->pnBoundR : *pEnv->pnBoundL;
    if (*pEnv->pnAmmoL == PUFO_RADAR_AMMO_NONE) {
        *pEnv->pnBoundL = *pEnv->pnBoundR;
    }
    if (*pEnv->pnAmmoR == PUFO_RADAR_AMMO_NONE) {
        *pEnv->pnBoundR = nInherited;
    }
}

/* ------------------------------------------------- the intercept shot animation
 * See the evidence block in pufo_geoscape.h for RadarScreen_DrawOverlay @ 0x5dc84. */

static const RadarShotSide g_aRadarShotSide[2] = {
    { 67, 69, 64, 65, 63, 64 }, /* left  */
    { 67, 69, 70, 71, 69, 72 }, /* right */
};

/* 0x5E160..0x5E17C and 0x5DF8C..0x5DFA8: _rt_sdiv(divisor 560, dividend 114 * nRadarRangeCur) -- the
 * current distance in pixels, the same scale the range bar and the mount bars use. */
static i32 Radar_ShotBarHeight(i32 nRangeCur)
{
    return (PUFO_RADAR_RANGE_PITCH * nRangeCur) / PUFO_RADAR_RANGE_SCALE;
}

/* 0x5E20C..0x5E224 and 0x5E488..0x5E49C: the shot landed. The ARM writes the cooldown, zeroes the
 * projectile and stores the roll's result; the left half's ids 29/30 run the roll before zeroing the
 * projectile, which is not observable because RollWeaponHit reads neither offset. */
static void Radar_ShotLanded(const RadarOverlayEnv *pEnv, i32 *pnShot, i32 *pnCooldown,
                             i32 nCooldown, i32 nMount)
{
    *pnCooldown = nCooldown;
    *pnShot = 0;
    *pEnv->pbLastShotHit = RadarScreen_RollWeaponHit(pEnv->pHitEnv, nMount) ? 1u : 0u;
}

static void Radar_DrawMountShot(const RadarOverlayEnv *pEnv, const Ship *pCraft, bool bRight)
{
    const RadarShotSide *pSide = &g_aRadarShotSide[bRight ? 1 : 0];
    i32 *pnShot     = bRight ? pEnv->pnShotYR    : pEnv->pnShotYL;
    i32 *pnCooldown = bRight ? pEnv->pnCooldownR : pEnv->pnCooldownL;
    i32 *pnAmmo     = bRight ? pEnv->pnAmmoR     : pEnv->pnAmmoL;
    const i8   nMount = bRight ? pCraft->bWeaponR : pCraft->bWeaponL;
    const i32  nShot  = *pnShot;
    const i32  nBar   = Radar_ShotBarHeight(*pEnv->pnRangeCur);
    const i32  nLevel = *pEnv->pnCraftLevel;
    const i32  nGate  = nLevel + nBar - PUFO_RADAR_SHOT_GATE_TRIM;
    const bool bFlip  = (*pEnv->pbFlip != 0);

    /* 0x5DD10..0x5DD24 and 0x5E24C..0x5E26C: a round is taken only when a shot STARTS, i.e. when the
     * projectile offset is 0. Only the -1 "unlimited" sentinel is exempt.
     *
     * ORIGINAL BUG (0x5DD1C / 0x5E258), reproduced: the -2 "no weapon on this mount" sentinel is NOT
     * exempt. A mount whose id is outside 25..30 -- WM_PSI_AMP is the one the game ships -- stores a
     * zero bound in RadarScreen_DrawCraftArmament, so once the radar's reach drops to 0 the engage
     * byte is set, and this decrement then runs every single frame: the mount's switch has no arm, so
     * the projectile offset is never advanced and the "a shot started" test stays true forever. The
     * counter walks off through -3, -4, -5 ... and RadarScreen_DrawCraftArmament's label test is
     * `!= -2`, so the radar screen starts printing it. */
    if (nShot == 0 && *pnAmmo != PUFO_RADAR_AMMO_UNLIMITED) {
        *pnAmmo -= 1;
    }

    switch (nMount) {
        case WM_CANNON:
            if (nShot < nGate) {
                const i32 nNew = nShot + PUFO_RADAR_STEP_CANNON
                               + (bFlip ? PUFO_RADAR_STEP_FLIP_EXTRA : 0);
                *pnShot = nNew;
                SoftSurf_BlitRectProxy(0, pSide->xCannonA, PUFO_RADAR_SHOT_TOP_Y - nNew, 1, 2,
                                       PUFO_RADAR_SHOT_COLOR_IDX, PUFO_RADAR_SHOT_COLOR_MID, 0);
                SoftSurf_BlitRectProxy(0, pSide->xCannonB, PUFO_RADAR_SHOT_TOP_Y - *pnShot, 1, 2,
                                       PUFO_RADAR_SHOT_COLOR_IDX, PUFO_RADAR_SHOT_COLOR_MID, 0);
            } else {
                Radar_ShotLanded(pEnv, pnShot, pnCooldown,
                                 PUFO_RADAR_COOLDOWN_CANNON, WM_CANNON);
            }
            break;

        case WM_STINGRAY:
            if (nShot < nGate) {
                const i32 nNew = nShot + PUFO_RADAR_STEP_WIDE
                               + (bFlip ? PUFO_RADAR_STEP_FLIP_EXTRA : 0);
                *pnShot = nNew;
                SoftSurf_BlitRectProxy(0, pSide->xBody, PUFO_RADAR_SHOT_TOP_Y - nNew, 2, 4,
                                       PUFO_RADAR_SHOT_COLOR_IDX, PUFO_RADAR_WIDE_COLOR_MID, 0);
            } else {
                Radar_ShotLanded(pEnv, pnShot, pnCooldown,
                                 PUFO_RADAR_COOLDOWN_STINGRAY, WM_STINGRAY);
            }
            break;

        case WM_AVALANCHE:
            if (nShot < nGate) {
                const i32 nNew = nShot + PUFO_RADAR_STEP_WIDE
                               + (bFlip ? PUFO_RADAR_STEP_FLIP_EXTRA : 0);
                *pnShot = nNew;
                SoftSurf_BlitRectProxy(0, pSide->xBody, PUFO_RADAR_SHOT_TOP_Y - nNew, 3, 4,
                                       PUFO_RADAR_SHOT_COLOR_IDX, PUFO_RADAR_WIDE_COLOR_MID, 0);
                SoftSurf_BlitRectProxy(0, pSide->xBodyAlt, PUFO_RADAR_SHOT_TOP_Y_ALT - *pnShot, 1, 5,
                                       PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                                       PUFO_RADAR_BAR_COLOR_LOW);
            } else {
                Radar_ShotLanded(pEnv, pnShot, pnCooldown,
                                 PUFO_RADAR_COOLDOWN_AVALANCHE, WM_AVALANCHE);
            }
            break;

        case WM_LASER:
            if (nShot < PUFO_RADAR_BEAM_FLIGHT) {
                *pnShot = nShot + PUFO_RADAR_STEP_BEAM;
                SoftSurf_BlitRectProxy(0, pSide->xBeam,
                                       PUFO_RADAR_BEAM_TOP_Y - nBar - nLevel, 1,
                                       nBar + nLevel - PUFO_RADAR_BEAM_HEIGHT_TRIM,
                                       PUFO_RADAR_SHOT_COLOR_IDX, PUFO_RADAR_SHOT_COLOR_MID, 0);
            } else if (nShot == PUFO_RADAR_BEAM_FLIGHT) {
                Radar_ShotLanded(pEnv, pnShot, pnCooldown,
                                 PUFO_RADAR_COOLDOWN_LASER, WM_LASER);
            }
            /* Neither branch: 0x5DFEC BNE and 0x5E518 return. A beam past 2 never resolves. */
            break;

        case WM_PLASMA:
            if (nShot < PUFO_RADAR_BEAM_FLIGHT) {
                *pnShot = nShot + PUFO_RADAR_STEP_BEAM;
                SoftSurf_BlitRectProxy(0, pSide->xBody,
                                       PUFO_RADAR_BEAM_TOP_Y - nBar - nLevel, 3,
                                       nBar + nLevel - PUFO_RADAR_BEAM_HEIGHT_TRIM,
                                       PUFO_RADAR_PLASMA_COLOR_IDX, PUFO_RADAR_PLASMA_COLOR_MID, 0);
                SoftSurf_BlitRectProxy(0, pSide->xBodyAlt,
                                       PUFO_RADAR_BEAM_TOP_Y - nBar - nLevel, 1,
                                       nBar + nLevel - PUFO_RADAR_BEAM_HEIGHT_TRIM,
                                       PUFO_RADAR_SHOT_COLOR_IDX, PUFO_RADAR_PLASMA_CORE_MID, 0);
            } else if (nShot == PUFO_RADAR_BEAM_FLIGHT) {
                Radar_ShotLanded(pEnv, pnShot, pnCooldown,
                                 PUFO_RADAR_COOLDOWN_PLASMA, WM_PLASMA);
            }
            break;

        case WM_FUSION:
            if (nShot < nGate) {
                const i32 nNew = nShot + PUFO_RADAR_STEP_WIDE
                               + (bFlip ? PUFO_RADAR_STEP_FLIP_EXTRA : 0);
                *pnShot = nNew;
                SoftSurf_BlitRectProxy(0, pSide->xBody, PUFO_RADAR_SHOT_TOP_Y - nNew, 2, 6,
                                       PUFO_RADAR_SHOT_COLOR_IDX, PUFO_RADAR_WIDE_COLOR_MID, 0);
                SoftSurf_BlitRectProxy(0, pSide->xBody, PUFO_RADAR_SHOT_TOP_Y - *pnShot, 2, 4,
                                       PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                                       PUFO_RADAR_BAR_COLOR_LOW);
                SoftSurf_BlitRectProxy(0, pSide->xCross, PUFO_RADAR_SHOT_CROSS_Y - *pnShot, 4, 2,
                                       PUFO_RADAR_BAR_COLOR_IDX, PUFO_RADAR_BAR_COLOR_MID,
                                       PUFO_RADAR_BAR_COLOR_LOW);
            } else {
                Radar_ShotLanded(pEnv, pnShot, pnCooldown,
                                 PUFO_RADAR_COOLDOWN_FUSION, WM_FUSION);
            }
            break;

        default:
            /* WM_PSI_AMP and anything else: the switch has no arm, so the round taken above is lost. */
            break;
    }
}

void RadarScreen_DrawOverlay(const RadarOverlayEnv *pEnv)
{
    Ship *pCraft;

    /* Port-side guard: the original dereferences apCraft[nShipIdx] with no test (LDRSB [R3, #0x23] at
     * 0x5DD3C). */
    if (pEnv == NULL || pEnv->pnShipIdx == NULL || pEnv->apCraft == NULL
        || pEnv->pnRangeCur == NULL || pEnv->pnCraftLevel == NULL || pEnv->pbFlip == NULL
        || pEnv->pbEngageL == NULL || pEnv->pbEngageR == NULL || pEnv->pnCooldownL == NULL
        || pEnv->pnCooldownR == NULL || pEnv->pnShotYL == NULL || pEnv->pnShotYR == NULL
        || pEnv->pnAmmoL == NULL || pEnv->pnAmmoR == NULL || pEnv->pbLastShotHit == NULL
        || pEnv->pHitEnv == NULL) {
        return;
    }

    const i32 nShip = *pEnv->pnShipIdx;
    pCraft = (nShip >= 0 && nShip < pEnv->nCraftCount) ? pEnv->apCraft[nShip] : NULL;
    if (pCraft == NULL) {
        return;
    }

    /* 0x5DCA8..0x5DCEC: an idle mount -- ammo exactly 0 and the projectile at 0 -- loses its engage
     * byte, and the last-shot result is cleared for the frame. The test is == 0, so the -2 sentinel is
     * NOT idle. */
    if (*pEnv->pnAmmoR == 0 && *pEnv->pnShotYR == 0) {
        *pEnv->pbEngageR = 0;
    }
    *pEnv->pbLastShotHit = 0; /* 0x5DCC0 */
    if (*pEnv->pnAmmoL == 0 && *pEnv->pnShotYL == 0) {
        *pEnv->pbEngageL = 0;
    }

    /* Right half first (0x5DCFC / 0x5DD0C), then the left (0x5E238 / 0x5E248). The two are
     * independent: skipping one does not gate the other. */
    if (*pEnv->pbEngageR != 0 && *pEnv->pnCooldownR == PUFO_RADAR_COOLDOWN_READY) {
        Radar_DrawMountShot(pEnv, pCraft, true);
    }
    if (*pEnv->pbEngageL != 0 && *pEnv->pnCooldownL == PUFO_RADAR_COOLDOWN_READY) {
        Radar_DrawMountShot(pEnv, pCraft, false);
    }
}

/* ------------------------------------------------- RadarScreen_Update @ 0x60570
 * See the evidence block in pufo_geoscape.h for the whole map. */

const RadarPanelBlit g_aRadarPanelBlit[5] = {
    { PUFO_RADAR_PANEL_X, PUFO_RADAR_PANEL_Y, PUFO_RADAR_PANEL_W, 0, 4 }, /* nH filled at run time */
    { 111, 50,  93, 120, 0 }, /* 0x60620 */
    { 111, 50,  97, 120, 0 }, /* 0x6064C */
    { 111, 50,  97, 100, 0 }, /* 0x6067C */
    { 111, 50,  97,  30, 0 }  /* 0x606AC */
};

const RadarPanelRail g_aRadarPanelRail[5] = {
    {  22,  60,   2, 120 }, /* 0x606DC */
    { 113,  52,   2, 120 }, /* 0x60700 */
    { 208,  60,   2, 120 }, /* 0x60724 */
    {  22,  52, 120,   2 }, /* 0x6074C */
    {  22, 172, 120,   2 }  /* 0x60770 */
};

/* The symmetric colour ramps of the signal bars and the cursor bars (0x60CC0..0x60D94 and
 * 0x60F54..0x6106C). Both peak in the middle column. */
static const u8 g_anRadarBarMid[PUFO_RADAR_BAR_COUNT]    = { 0x55, 0x5A, 0x5C, 0x5A, 0x55 };
static const u8 g_anRadarCursorMid[PUFO_RADAR_BAR_COUNT] = { 0x55, 0x5F, 0x69, 0x5F, 0x55 };

/* 0x6095C / 0x60ADC / 0x60BBC / 0x610B4 / 0x611E0: the original's strcpy-until-NUL loop out of a
 * TEXTS record, with no bound on the 100-byte stack buffer. Bounded here (deviation); the longest
 * of the five records this function uses is 21 bytes, so no real text is clipped. */
static void Radar_CopyTextRec(const u8 *pTextRecs, i32 nRec, char *pszOut, size_t cchOut)
{
    const char *const pszSrc = (const char *)pTextRecs + PUFO_TEXT_REC_STRIDE * nRec;
    for (size_t i = 0; i + 1 < cchOut; ++i) {
        pszOut[i] = pszSrc[i];
        if (pszSrc[i] == '\0') {
            return;
        }
    }
    pszOut[cchOut - 1] = '\0';
}

/* The contact the intercept is chasing. The original calls PtrArray_Get twice per branch on the
 * same index (once for the x, once for the y of the tile lookup, and again for the nType/nTimer
 * stores); every call returns the same object, so the port looks it up once. */
static StrategItem *Radar_ContactItem(const RadarUpdateEnv *pEnv)
{
    return (StrategItem *)PtrArray_Get(pEnv->pSession->pStrategList,
                                       *pEnv->pSession->pnStrategIdx);
}

/* 0x60A88 / 0x60B10 / 0x60C00 / 0x60C44 / 0x60CA0: the contact's map tile, which is also the race
 * id Funding_AddRegionBonus maps. */
static i32 Radar_ContactTile(const RadarUpdateEnv *pEnv, const StrategItem *pItem)
{
    return (pItem != NULL) ? Tilemap_GetAt(pEnv->pTilemap, pItem->nX, pItem->nY) : 0;
}

/* 0x605A0..0x60770: the five animated panel rects and the five rails that square them off. Only
 * the FIRST anim blit's result matters (0x605EC); the other four are not tested. Returns false
 * when the animation has not reached the full rect, which skips the rest of the draw block. */
static bool Radar_DrawPanelFrame(const RadarUpdateEnv *pEnv, i32 nPanelH)
{
    if (!Ui_AnimExpandBlit(pEnv->pAnim, PUFO_RADAR_PANEL_X, PUFO_RADAR_PANEL_Y,
                           PUFO_RADAR_PANEL_W, nPanelH, 0x28, 0x50, 0x14, 0xFF, 4)) {
        return false; /* 0x605F0: the whole draw block is skipped, the tail gates still run */
    }
    for (i32 i = 1; i < 5; ++i) {
        const RadarPanelBlit *const pBlit = &g_aRadarPanelBlit[i];
        (void)Ui_AnimExpandBlit(pEnv->pAnim, pBlit->x, pBlit->y, pBlit->nW, pBlit->nH,
                                0x28, 0x50, 0x14, 0xFF, pBlit->nMode);
    }
    for (i32 i = 0; i < 5; ++i) {
        SoftSurf_BlitRectProxy(0, g_aRadarPanelRail[i].x, g_aRadarPanelRail[i].y,
                               g_aRadarPanelRail[i].nW, g_aRadarPanelRail[i].nH,
                               PUFO_RADAR_PANEL_RAIL_IDX, PUFO_RADAR_PANEL_RAIL_MID,
                               PUFO_RADAR_PANEL_RAIL_LOW);
    }
    return true;
}

/* 0x60CBC..0x6107C: the twenty signal bars and the five cursor bars, every x offset by the scroll. */
static void Radar_DrawSignalBars(const RadarUpdateEnv *pEnv)
{
    const RadarSessionEnv *const pS = pEnv->pSession;
    const i32 n = *pS->pnScanOffset;

    for (i32 i = 0; i < PUFO_RADAR_BAR_COUNT; ++i) {
        const i32 x = n + PUFO_RADAR_BAR_X_FIRST + i * PUFO_RADAR_BAR_PITCH;
        SoftSurf_BlitRectProxy(0, x, PUFO_RADAR_BAR_Y_TOP, PUFO_RADAR_BAR_PITCH,
                               PUFO_RADAR_BAR_H_TOP, PUFO_RADAR_BAR_IDX,
                               g_anRadarBarMid[i], PUFO_RADAR_BAR_LOW);      /* 0x60CEC..0x60D94 */
        SoftSurf_BlitRectProxy(0, x, PUFO_RADAR_BAR_Y_MID, PUFO_RADAR_BAR_PITCH,
                               PUFO_RADAR_BAR_H_MID, PUFO_RADAR_BAR_IDX,
                               g_anRadarBarMid[i], PUFO_RADAR_BAR_LOW);      /* 0x60DC0..0x60E60 */
        SoftSurf_BlitRectProxy(0, 2 * (n + PUFO_RADAR_BAR_X_DOUBLED + i * PUFO_RADAR_BAR_PITCH),
                               PUFO_RADAR_BAR_Y_BOTTOM, 2 * PUFO_RADAR_BAR_PITCH,
                               PUFO_RADAR_BAR_H_BOTTOM, PUFO_RADAR_BAR_IDX,
                               g_anRadarBarMid[i], PUFO_RADAR_BAR_LOW);      /* 0x60E8C..0x60F3C */
        /* 0x60F64: MOVS / ADDMI / ASR #1 is C division by two, rounding toward zero. */
        SoftSurf_BlitRectProxy(0, *pS->pnCursorX + n / 2 + PUFO_RADAR_CURSOR_X_FIRST
                               + i * PUFO_RADAR_CURSOR_PITCH, *pS->pnCursorY + 1,
                               PUFO_RADAR_CURSOR_W, PUFO_RADAR_CURSOR_H, PUFO_RADAR_CURSOR_IDX,
                               g_anRadarCursorMid[i], PUFO_RADAR_BAR_LOW);   /* 0x60F80..0x6107C */
    }

    /* 0x61080..0x610A0: advance by ten, and wrap to zero when the NEW value is above 39. The
     * cycle is therefore 10, 20, 30, 0. */
    i32 nNext = n + PUFO_RADAR_SCAN_STEP;
    if (nNext > PUFO_RADAR_SCAN_WRAP) {
        nNext = 0;
    }
    *pS->pnScanOffset = nNext;
}

/* 0x610A4..0x6122C: the two labels, the range number, the message line, the blink wash and the
 * overlay sprite, the four button pads, and the fuel gauge. */
static void Radar_DrawReadout(const RadarUpdateEnv *pEnv)
{
    const RadarSessionEnv *const pS = pEnv->pSession;
    RadarUiState *const pUi = pS->pUi;
    char szText[100]; /* psz @ sp+0x20 */

    Radar_CopyTextRec(pEnv->pDetect->pTextRecs, PUFO_RADAR_TEXT_DIST_LABEL,
                      szText, sizeof(szText));
    Gfx_PackRgb565Pair(pEnv->pColor, 80, 200, 20, 40, 90, 20); /* 0x610E4 */
    Text_DrawString(120, 155, szText);                         /* 0x610F8 */

    char szNum[52];
    (void)snprintf(szNum, sizeof(szNum), "%d", (int)*pS->pnRangeCur); /* 0x6110C: j_itoa, base 10 */
    Text_DrawString(180, 155, szNum); /* 0x61120 */

    if (*pS->pnMsgTimer < PUFO_RADAR_TIMER_LIMIT) { /* 0x6112C */
        Text_DrawString(28, 175, pEnv->pDetect->pszMsg);
    }

    if (*pS->pbBlinkPhase != 0) { /* 0x61150 */
        SoftSurf_BlitRectProxy(0, PUFO_RADAR_WASH_X, PUFO_RADAR_WASH_Y, PUFO_RADAR_WASH_W,
                               PUFO_RADAR_WASH_H, PUFO_RADAR_WASH_IDX,
                               PUFO_RADAR_WASH_MID, PUFO_RADAR_WASH_LOW);
        RadarScreen_BlitOverlay(pUi->pOverlayPad); /* 0x611A0 */
    }

    for (i32 i = 0; i < PUFO_RADAR_BUTTON_BLIT_COUNT; ++i) { /* 0x611A8: MOV R8, #4 */
        PadImage_BlitEx(pUi->apButtonPad[i], PUFO_RADAR_BUTTON_KEY);
    }

    Radar_CopyTextRec(pEnv->pDetect->pTextRecs, PUFO_RADAR_TEXT_DAMAGE_LABEL,
                      szText, sizeof(szText));
    Gfx_PackRgb565Pair(pEnv->pColor, 120, 200, 40, 40, 80, 20); /* 0x61214 */
    Text_DrawString(117, 59, szText);                           /* 0x61228 */
    (void)RadarScreen_DrawFuelGauge(pEnv->pFuel);              /* 0x6122C */
}

/* 0x6091C..0x60C78: the damage ladder, reached only when the last shot hit. */
static void Radar_ResolveDamage(const RadarUpdateEnv *pEnv)
{
    const RadarSessionEnv *const pS = pEnv->pSession;
    const i32 nHpMax = *pS->pnTargetHpMax;

    /* Port-side guard, not original behaviour: the original divides by nInterceptTargetHpMax with
     * no test, and RadarScreen_Init only ever fills it from the class table. */
    const i32 nPct = (nHpMax != 0) ? (100 * *pS->pnTargetHp) / nHpMax : 0;

    if (*pS->pnTargetHp > 0 || *pS->pbInterceptDone != 0) { /* 0x60924 / 0x60934 */
        if (nPct < PUFO_RADAR_HP_ESCAPED_PCT && nPct >= PUFO_RADAR_HP_CRASH_PCT) { /* 0x60A28, 0x60A30 */
            (void)RadarScreen_RollDetection(pEnv->pDetect, PUFO_RADAR_DETECT_CHANCE);

            StrategItem *const pItem = Radar_ContactItem(pEnv);
            const i32 nTile = Radar_ContactTile(pEnv, pItem);

            /* 0x60A48..0x60AA8, in the ARM's short-circuit order: Game_RandU16(4) is only rolled
             * when the trace is still held AND the contact is not sitting on the 255 ocean tile. */
            if (*pS->pbTraceLost == 0 && nTile != PUFO_REGION_ID_NONE
                && (u16)Game_RandU16((u16)PUFO_RADAR_ESCAPE_ROLL) == 0) {
                *pS->pnMsgTimer = 0;                                /* 0x60AB4 */
                Radar_CopyTextRec(pEnv->pDetect->pTextRecs, PUFO_RADAR_TEXT_TARGET_LANDED,
                                  pEnv->pDetect->pszMsg, pEnv->pDetect->cchMsg);
                Funding_AddRegionBonus(pEnv->anFundingBlock, nTile);  /* 0x60B1C */
                if (pItem != NULL) {
                    pItem->nType = 4;                               /* 0x60B30 */
                    pItem->nTimer = PUFO_RADAR_TIMER_LANDED;        /* 0x60B44 */
                }
                *pS->pbInterceptDone = 1;                           /* 0x60B4C */
            }
        }

        /* 0x60B6C..0x60B94: the percentage is recomputed from a re-read nInterceptTargetHp, so a
         * contact that sat in the 40..69 band and did NOT escape can still fall in here. */
        const i32 nPct2 = (nHpMax != 0) ? (100 * *pS->pnTargetHp) / nHpMax : 0;
        if (nPct2 < PUFO_RADAR_HP_CRASH_PCT) {
            StrategItem *const pItem = Radar_ContactItem(pEnv);
            const i32 nTile = Radar_ContactTile(pEnv, pItem);

            *pS->pnMsgTimer = 0;                                    /* 0x60BA0 */
            Radar_CopyTextRec(pEnv->pDetect->pTextRecs, PUFO_RADAR_TEXT_TARGET_CRASHED,
                              pEnv->pDetect->pszMsg, pEnv->pDetect->cchMsg);
            *pS->pbInterceptDone = 1;                               /* 0x60C10, set before the tile test */
            Funding_AddRegionBonus(pEnv->anFundingBlock, nTile);      /* 0x60C50 / 0x60CAC */
            if (nTile == PUFO_REGION_ID_NONE) {
                (void)StrategList_RemoveAt(pS->pStrategList, pEnv->pShipEnv->pWorld,
                                           *pS->pnStrategIdx, pEnv->pShipEnv->pnSpawnOnceFlag,
                                           pEnv->pShipEnv->pSpawnCtx); /* 0x60CB8: it went into the sea */
            } else if (pItem != NULL) {
                pItem->nType = 4;                                   /* 0x60C64 */
                pItem->nTimer = PUFO_RADAR_TIMER_LANDED;            /* 0x60C74 */
            }
        }
    } else {
        *pS->pnMsgTimer = 0;                                        /* 0x60940 */
        Radar_CopyTextRec(pEnv->pDetect->pTextRecs, PUFO_RADAR_TEXT_TARGET_DEAD,
                          pEnv->pDetect->pszMsg, pEnv->pDetect->cchMsg);
        *pEnv->pShipEnv->pnSpawnOnceFlag = 1;                       /* 0x6097C */
        (void)StrategList_RemoveAt(pS->pStrategList, pEnv->pShipEnv->pWorld, *pS->pnStrategIdx,
                                   pEnv->pShipEnv->pnSpawnOnceFlag, pEnv->pShipEnv->pSpawnCtx);
        (void)Ship_ClearInterceptByStrategIdx(pEnv->pShipEnv, *pS->pnStrategIdx); /* 0x60994 */
        *pS->pbInterceptDone = 1;                                   /* 0x609A0 */
    }
}

/* 0x61254..0x612E8: close the screen, then send every empty craft that was chasing the finished
 * contact home. */
static void Radar_CloseAndRetarget(const RadarUpdateEnv *pEnv)
{
    const RadarSessionEnv *const pS = pEnv->pSession;
    const ShipRemovalEnv *const pRem = pEnv->pShipEnv;

    RadarScreen_SetActive(pS, 0); /* 0x6125C */

    /* 0x612D8: Map_nCraftCount is re-read on every iteration, because a replacement spawn raised
     * by StrategList_RemoveAt can grow the roster mid-loop. */
    for (i32 i = 0; i < (i32)*pRem->pnShipCount; ++i) {
        const Ship *const pShip = pS->apCraft[i];
        if (pShip == NULL) {
            continue;
        }
        if (pShip->bHomeBase >= 0xFE || pShip->bIntercepting == 0
            || pShip->nStrategIdx != *pS->pnStrategIdx) { /* 0x6128C..0x612B0 */
            continue;
        }
        /* 0x612BC: the second argument is the LOOP INDEX masked to a byte -- a craft index, not a
         * base index. The IDB's own note on this call site says so. */
        if (Craft_CountSoldiersAboard((Soldier *const *)pRem->apSoldiers,
                                      (i32)*pRem->pnSoldierCount, i) == 0) {
            (void)Ship_RetargetTravel(pRem, i); /* 0x612CC */
        }
    }
}

/* 0x61320..0x613A0: the intercept craft ran out of fuel on the way in. It is deleted, not sent
 * home -- the only place in the game that removes a craft outside a battle. */
static void Radar_RemoveStrandedCraft(const RadarUpdateEnv *pEnv)
{
    const RadarSessionEnv *const pS = pEnv->pSession;

    RadarScreen_SetActive(pS, 0);                                   /* 0x6132C */
    *pEnv->pdwShortfall = 1;                                        /* 0x61340, a DWORD store */
    UiLineList_AppendWithSound(pEnv->pMsgList, PUFO_RADAR_MSG_SHIP_LOST,
                               (u32)*pS->pnShipIdx, 0, 0, 0,
                               pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x61354 */
    (void)Ship_FormatDisplayName(pS->apCraft, *pS->pnShipIdx, pEnv->pDetect->pTextRecs,
                                 pEnv->pszNameBuf, pEnv->cchNameBuf);

    const i32 nShipIdx = *pS->pnShipIdx;
    if (nShipIdx >= 0 && nShipIdx < (i32)*pEnv->pShipEnv->pnShipCount) {
        const Ship *const pCraft = pS->apCraft[nShipIdx];
        if (pCraft != NULL) {
            (void)Ship_RemoveWithCargo(pEnv->pShipEnv, (i32)pCraft->bId); /* 0x613A0, tail call */
        }
    }
}

/* 0x613B0..0x6143C: the range animation reached full zoom. */
static void Radar_FullZoomExit(const RadarUpdateEnv *pEnv)
{
    const RadarSessionEnv *const pS = pEnv->pSession;

    if (*pS->pbFlip == 0 || *pS->pnRangeCur != PUFO_RADAR_FULL_RANGE) { /* 0x613BC, 0x613CC */
        return;
    }
    RadarScreen_SetActive(pS, 0);                                     /* 0x613E0 */
    (void)Ship_RetargetTravel(pEnv->pShipEnv, *pS->pnShipIdx);        /* 0x613EC */
    if (*pS->pbTraceLost != 0) {
        const StrategList view = StrategViewOf(pS->pStrategList);
        StrategItem *const pEntry = (StrategItem *)PtrArray_Get(
            pS->pStrategList,
            StrategList_FindByShipId(&view, pEnv->pShipEnv->pWorld, *pS->pnShipIdx));
        if (pEntry != NULL) {
            pEntry->nTimer = PUFO_RADAR_TIMER_TRACE_LOST;             /* 0x61420 */
        }
    }
    /* ORIGINAL BUG (0x61434), reproduced as dead code: the Ship_ClearInterceptByStrategIdx the ARM
     * reaches with BLEQ needs ANDSEQ to have left the zero flag set, and the only way there is
     * bRadarTraceLost == 0 AND bRadarFlip == 0 -- while the gate at 0x613BC already requires
     * bRadarFlip to be non-zero. So it can never run, and the OTHER craft still chasing this
     * contact are never sent home. The port does not call it either. See bugs.tsv. */
}

/* RadarScreen_Update @ 0x60570. */
void RadarScreen_Update(const RadarUpdateEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pSession == NULL || pEnv->pAnim == NULL || pEnv->pColor == NULL
        || pEnv->pPalette == NULL || pEnv->pRangeBar == NULL || pEnv->pFuel == NULL
        || pEnv->pArmament == NULL || pEnv->pOverlay == NULL || pEnv->pDetect == NULL
        || pEnv->pShipEnv == NULL || pEnv->pTilemap == NULL || pEnv->pMsgList == NULL
        || pEnv->pszNameBuf == NULL || pEnv->cchNameBuf == 0
        || pEnv->pDetect->pTextRecs == NULL || pEnv->pDetect->pszMsg == NULL
        || pEnv->pDetect->cchMsg == 0
        || pEnv->pShipEnv->pnSpawnOnceFlag == NULL || pEnv->pShipEnv->pnShipCount == NULL
        || pEnv->pShipEnv->pnSoldierCount == NULL) {
        return;
    }
    const RadarSessionEnv *const pS = pEnv->pSession;
    if (pS->pUi == NULL || pS->apCraft == NULL || pS->pStrategList == NULL) {
        return;
    }
    RadarUiState *const pUi = pS->pUi;

    /* 0x60584..0x60594: the blink phase moves only the first panel rect, by 50. */
    const i32 nPanelH = PUFO_RADAR_PANEL_H_BASE
        + ((*pS->pbBlinkPhase != 0) ? PUFO_RADAR_PANEL_H_BLINK : 0);

    if (*pS->pbActive == 0) { /* 0x6059C */
        return;
    }

    if (Radar_DrawPanelFrame(pEnv, nPanelH)) {
        RadarScreen_CyclePalette(pEnv->pPalette); /* 0x60774 */
        RadarScreen_CyclePalette(pEnv->pPalette); /* 0x60778: twice, so the range moves two a frame */
        if (pUi->pFramePad != NULL) {
            PadImage_BlitEx(pUi->pFramePad, PUFO_RADAR_FRAME_KEY);
        }

        if (*pS->pbInterceptDone == 0 && *pS->pbOutOfFuel == 0 && *pS->pnTargetHp > 0) {
            (void)RadarScreen_DrawRangeBar(pEnv->pRangeBar); /* 0x607B8 */
        }

        if (pUi->pUfoPad != NULL) {
            /* 0x607C0..0x607F0: the same range quotient the range bar uses, from 170 not 183. */
            pUi->pUfoPad->nOriginY = PUFO_RADAR_UFO_Y_BASE
                - (PUFO_RADAR_RANGE_PITCH * *pS->pnRangeCur) / PUFO_RADAR_RANGE_SCALE;
            if (*pS->pbInterceptDone == 0) { /* 0x607F4 */
                SoftSurf_BlitFlip(PUFO_RADAR_UFO_X, pUi->pUfoPad->nOriginY, pUi->pUfoPad->pData,
                                  g_aPalette[PUFO_RADAR_UFO_PAL_SLOT],
                                  (pUi->pUfoMissilePad != NULL) ? pUi->pUfoMissilePad->pData : NULL);
            }
        }

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 120, 200, 40, 40, 80, 20); /* 0x60844 */
        for (i32 i = 0; i < PUFO_RADAR_CONTROLS; ++i) { /* 0x6084C..0x60864 */
            if (pUi->apCtl[i] != NULL) {
                (void)UiControl_BlitActivePad(pUi->apCtl[i], pEnv->pfnCtlBlit,
                                              pEnv->pfnCtlBlitEx, pEnv->pCtlBlitUser);
            }
        }

        RadarScreen_DrawCraftArmament(pEnv->pArmament); /* 0x60870, unconditional */
        if (*pS->pbInterceptDone == 0 && *pS->pbOutOfFuel == 0) {
            RadarScreen_DrawOverlay(pEnv->pOverlay); /* 0x60898 */
        }

        /* 0x608B0..0x608C8. The original re-reads bRadarInterceptDone at 0x6089C after DrawOverlay
         * even though nothing DrawOverlay reaches can write it; reproduced as a re-read. */
        if (*pS->pbOutOfFuel == 0 && *pS->pbInterceptDone == 0
            && pEnv->pOverlay->pbLastShotHit != NULL && *pEnv->pOverlay->pbLastShotHit != 0) {
            if (pUi->pUfoPad != NULL && pUi->pUfoFlashPad != NULL) {
                pUi->pUfoFlashPad->nOriginX = pUi->pUfoPad->nOriginX; /* 0x608DC */
                pUi->pUfoFlashPad->nOriginY = pUi->pUfoPad->nOriginY; /* 0x608EC */
                SoftSurf_BlitFlip(PUFO_RADAR_UFO_X, pUi->pUfoPad->nOriginY,
                                  pUi->pUfoFlashPad->pData,
                                  g_aPalette[PUFO_RADAR_UFO_PAL_SLOT],
                                  (pUi->pUfoMissilePad != NULL) ? pUi->pUfoMissilePad->pData : NULL);
            }
            Radar_ResolveDamage(pEnv);
        }

        Radar_DrawSignalBars(pEnv); /* 0x60CBC..0x610A0 */
        Radar_DrawReadout(pEnv);    /* 0x610A4..0x6122C */
    }

    /* 0x61234..0x6143C: the three tail gates run whether or not the panel was drawn, and only the
     * first matching one runs. */
    if (*pS->pbInterceptDone != 0 && *pS->pnMsgTimer > PUFO_RADAR_TIMER_LIMIT) {
        Radar_CloseAndRetarget(pEnv);
    } else if (*pS->pbOutOfFuel != 0 && *pS->pnMsgTimer > PUFO_RADAR_TIMER_LIMIT) {
        Radar_RemoveStrandedCraft(pEnv);
    } else {
        Radar_FullZoomExit(pEnv);
    }
}
