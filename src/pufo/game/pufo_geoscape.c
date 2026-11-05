/* pufo_geoscape.c - see pufo_geoscape.h for the evidence map. */
#include "game/pufo_geoscape.h"
#include "ui/pufo_uigeom.h" /* QoL wide-view shim: identity in portrait */
#include <stdio.h>             /* snprintf, the bounded stand-in for the original's j_itoa */
#include <stdlib.h>            /* malloc/free, RadarScreen_Init's pads and controls */
#include <stddef.h>            /* size_t, the bounded text copy */
#include "audio/pufo_audio.h"  /* Sound_Play, the click the speed buttons play */
#include "game/pufo_baseui.h"  /* Funding_AddRegionBonus and PUFO_REGION_ID_NONE, the radar contact bonus */
#include "game/pufo_boot.h"    /* PUFO_TEXT_REC_STRIDE, the TEXTS record stride */
#include "game/pufo_font.h"    /* Font_DrawGlyph, g_fontSys.nBaseBitsBase, Text_DrawString */
#include "game/pufo_plane.h"   /* BasePlaneScreen_Draw, the hangar arm's callee @ 0x7d374 */
#include "render/pufo_render.h" /* g_aPalette, the slot RadarScreen_CyclePalette rotates */
#include "game/pufo_save.h"    /* SmapSite, for the monthly upkeep of a base */
#include "ui/pufo_geouimanage.h" /* PUFO_GEOUI_CLOCK_DIGIT_PADS, the ten pads at 0x186BF0 */
#include "ui/pufo_modal.h"       /* UiModalAnim, UiModalLayout, the frame-end dialog */
#include "ui/pufo_panel.h"       /* UiPanel_TrackHoldRow, the buy/sell tail of screens 10, 11 and 12 */
#include "util/pufo_rand.h"
#include "util/pufo_str.h"     /* pufo_strncpy/pufo_strcat, the cost label */

static i32 Abs32(i32 n)
{
    return n < 0 ? -n : n;
}

/* The StrategList view of the state's list block (0x186534 plus the counters at +0x10 / +0x14).
 * StrategViewOf (pufo_strateg.h) is the search view, and it zeroes the two counters; the ticks need
 * them carried because StrategList_UpdateDetect assigns new ids through them, so this is the same
 * fresh-from-the-array view with the counters filled in from the state's cells. Every pass builds it
 * fresh: an add anywhere in the pass reallocates PtrArray's array, and a view held across a callee
 * that spawns would read freed memory. */
static StrategList GeoscapeListView(const GeoscapeState *pState)
{
    static const PtrArray s_Empty = {0, NULL};
    StrategList view = StrategViewOf((pState->pList != NULL) ? pState->pList : &s_Empty);

    if (pState->pnNextDisplayId != NULL) {
        view.nNextDisplayId = *pState->pnNextDisplayId;
    }
    if (pState->pnNextSiteId != NULL) {
        view.nNextSiteId = *pState->pnNextSiteId;
    }
    return view;
}

/* The counters are the original's two globals, so a pass that let a callee increment them has to put
 * them back (StrategList_UpdateDetect, 0x813d0's id assignment: pList->nNextDisplayId++). */
static void StrategView_Store(GeoscapeState *pState, const StrategList *pView)
{
    if (pState->pnNextDisplayId != NULL) {
        *pState->pnNextDisplayId = pView->nNextDisplayId;
    }
    if (pState->pnNextSiteId != NULL) {
        *pState->pnNextSiteId = pView->nNextSiteId;
    }
}

void Geoscape_TickShips(GeoscapeState *pState, const GeoscapeHooks *pHooks)
{
    GeoscapeWorld *pWorld = &pState->world;

    /* The pause byte is one global in the original (apLines[106]); the tick
     * owns the clock, so it hands the pointer to the contact layer. */
    pWorld->pTime = &pState->time;

    for (i32 i = 0; i < GeoscapeWorld_GetShipCount(pWorld); ++i)
        Ship_TickRangeRecover(pWorld->apCraft[i]);

    StrategHooks  detectHooks = { pHooks->pfnContactMsg,
                                  pHooks->pfnFindBaseBySiteId, pHooks->pUser };
    StrategList   list = GeoscapeListView(pState);

    /* SLOBYTE(apLines[118]) % 2 == 0 */
    if ((i8)pWorld->nMinuteTicks % 2 == 0)
        StrategList_UpdateDetect(&list, pWorld, &detectHooks);

    StrategList_UpdateMotion(&list, pWorld);
    StrategView_Store(pState, &list);

    if (pState->time.bPaused == 0) {
        pHooks->pfnProcessEvents(0, pHooks->pUser);
        list = GeoscapeListView(pState); /* the event pass adds entries, and an add reallocates the array */

        /* LOBYTE increment on the byte at apLines[118]. */
        pWorld->nMinuteTicks = (i8)(u8)(pWorld->nMinuteTicks + 1);
        if ((i8)pWorld->nMinuteTicks % 5 == 0)
            Ship_ClearExpiredIntercepts(pWorld, &list, &detectHooks);

        /* 0x5D5E4..0x5D680. The original's home-base filter is dead code: 0xFF falls through the
         * 0xFE test and 0xFE takes the BNE, so every craft away from base reaches the range gate. */
        SmapCraftRangeEnv rangeEnv = { pWorld, pState->apBases, &list };
        for (i32 i = 0; i < GeoscapeWorld_GetShipCount(pWorld); ++i) {
            Ship *pShip = pWorld->apCraft[i];
            if (pShip->bAtBase == 0
                && Smap_CraftRangeExhausted(&rangeEnv, i)
                && pShip->bContactMsg == 0) {
                pShip->bContactMsg = 1;
                *pState->pnSelectedShipIdx = i; /* nSelectedShipIdx @ 0x187054 */
                pState->nSelectedShipAux = i;
                pHooks->pfnContactMsg(15, i, 0, pHooks->pUser);
                GameTime_Pause(&pState->time); /* raw call at 0x5D664 */
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
    SupplyUser  supplyUser = { pHooks };
    StrategList list = GeoscapeListView(pState);
    GeoscapeWorld *pWorld = &pState->world;

    /* Missions that drifted past the map edge are teleported back and
     * re-seeded: x/y = 150/250, then SpawnMissionSite(sub 0). */
    for (i32 i = 0; i < list.nCount; ++i) {
        StrategItem *pItem = list.apItems[i];
        if (pItem->nType == 1 && pItem->bActive == 1
            && (Abs32(pItem->nX) > 1000 || Abs32(pItem->nY) > 1000)) {
            pItem->nX = 150;
            pItem->nY = 250;
            pHooks->pfnSpawnMissionSite(pItem, 0, pHooks->pUser);
            /* 0x5d250 re-reads the global with PtrArray_Get on every test, and the spawn can add
             * an entry through its intercept hook (BasePlaneScreen_AddInterceptMission 0x7de14 ->
             * StrategList_AddItem), so the view has to be fresh for the next iteration. */
            list = GeoscapeListView(pState);
        }
    }

    ShipSupplyHooks supply = {
        pState->apBases, pState->pSupplyItem, pState->pnLowSupplyFlag,
        Supply_LowMsg, &supplyUser
    };
    for (i32 i = 0; i < GeoscapeWorld_GetShipCount(pWorld); ++i) {
        Ship_TickRefuelAtBase(pWorld->apCraft[i]);
        Ship_TickRangeRecoverConsume(pWorld->apCraft[i], &supply);
    }

    pHooks->pfnGrantSiteRecovery(pHooks->pUser);
    pHooks->pfnRefreshBaseTabs(pHooks->pUser);
    pHooks->pfnAccumulateResearch(pHooks->pUser);

    /* The test re-reads the live container: 0x5d314 runs StrategList_CountActive AFTER
     * Smap_GrantSiteRecovery (0x5d2fc), and a delivered craft adds its entry through
     * Ship_SpawnAtSlot's StrategList_AddItem (0x2CBC8) -- an add reallocates the array
     * (PtrArray_Insert), so the view taken above would read freed memory here. */
    list = GeoscapeListView(pState);
    if (StrategList_CountActive(&list) < 2) {
        if (Game_RandU16(4u) == 1u)
            pHooks->pfnProcessEvents(1, pHooks->pUser);
    }

    for (i32 b = 0; b < GeoscapeState_GetBaseCount(pState); ++b)
        pState->apEmptyFacilityCells[b] =
            pHooks->pfnCountEmptyFacilityCells(b, pHooks->pUser);
}

void Geoscape_TickBases(GeoscapeState *pState, const GeoscapeHooks *pHooks)
{
    if (*pState->pnDaysSinceStart < 200u)
        ++*pState->pnDaysSinceStart; /* word_1472C8, the same cell the alien mission composition reads */

    pHooks->pfnTickFacilityConstruction(pHooks->pUser);

    for (i32 b = 0; b < GeoscapeState_GetBaseCount(pState); ++b)
        pState->world.apSiteState[b] =
            (u8)pHooks->pfnClearFacilityGridRow(b, pHooks->pUser);

    for (i32 s = 0; s < *pState->pnSoldierCount; ++s) {
        Soldier *pS = pState->apSoldiers[s];
        if ((i8)pS->nPsiTrainDays > 0) {
            ++pS->auStat[6];
            --pS->nPsiTrainDays;
        }
        if (pS->bUnk247 != 0)
            --pS->bUnk247;
        if (pS->bUnk247 == 0 && pS->bUnk183 == 254)
            pS->bUnk183 = 255; /* the original writes -1 into the byte */
    }
}

/* Adapter so Geoscape_AdvanceTime can call the three ticks. */
static void Tick_Ships(void *pUser)
{
    GeoscapeTickBundle *p = (GeoscapeTickBundle *)pUser;
    Geoscape_TickShips(p->pState, p->pHooks);
}

static void Tick_Hourly(void *pUser)
{
    GeoscapeTickBundle *p = (GeoscapeTickBundle *)pUser;
    Geoscape_TickHourly(p->pState, p->pHooks);
}

static void Tick_Bases(void *pUser)
{
    GeoscapeTickBundle *p = (GeoscapeTickBundle *)pUser;
    Geoscape_TickBases(p->pState, p->pHooks);
}

static void Tick_Monthly(void *pUser)
{
    GeoscapeTickBundle *p = (GeoscapeTickBundle *)pUser;
    p->pHooks->pfnOpenMonthly(p->pHooks->pUser);
}

void Geoscape_BuildClockHooks(GeoscapeTickBundle *pBundle, GeoscapeClockHooks *pOut)
{
    pOut->pfnTickShips  = Tick_Ships;
    pOut->pfnTickHourly = Tick_Hourly;
    pOut->pfnTickBases  = Tick_Bases;
    pOut->pfnOpenMonthly = Tick_Monthly;
    pOut->pUser         = pBundle;
}

void Geoscape_Advance(GeoscapeState *pState, const GeoscapeHooks *pHooks,
                      i32 nDeltaSec)
{
    GeoscapeTickBundle bundle = { pState, pHooks };
    GeoscapeClockHooks clockHooks;

    Geoscape_BuildClockHooks(&bundle, &clockHooks);
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

static i32 Base_FindIndexBySignedSiteKey(BaseRec *const *apBases, i32 nBaseCount, i32 nKey)
{
    if (apBases == NULL || nBaseCount <= 0) {
        return -1;
    }
    for (i32 i = 0; i < nBaseCount; ++i) {
        if (apBases[i] == NULL) {
            continue;
        }
        if ((i32)(i8)apBases[i]->bUnk4 == nKey) {
            return i;
        }
    }
    return -1;
}

i32 Base_FindIndexByTypeByte(BaseRec *const *apBases, i32 nBaseCount, u8 nType)
{
    /* The ARM masks its argument to a byte at 0x6AC58 before the signed field compare. */
    return Base_FindIndexBySignedSiteKey(apBases, nBaseCount, (i32)nType);
}

i32 Base_FindIndexBySiteId(BaseRec *const *apBases, i32 nBaseCount, i32 nSiteId)
{
    /* The ARM SiteId search passes its full, unmasked R1 value to the signed field compare. */
    return Base_FindIndexBySignedSiteKey(apBases, nBaseCount, nSiteId);
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

/* aFacBuildSpec @ 0xAA8F8. The FacBuildSpec type now lives in pufo_geoscape.h because
 * Ui_DrawBaseInfo @ 0x9B5F4 reads the two tile bytes as basebits glyph indices; the table itself
 * is exported for the same reason -- UiBaseInfoDrawEnv.paBuildSpec (the ufopaedia category-3
 * entry block) is filled by the shell in World_WireGeoscapeFrame. */
const FacBuildSpec g_aFacBuildSpec[PUFO_FAC_SPEC_COUNT] = {
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

/* Base_CountActiveManuf @ 0x68264. The manufacturing engineers column (auManufCol @ 0x15E19C). */
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

/* Destination indices are [nFirstBase,nLastBase); each takes the following row.
 * Manufacturing uses the original object-count bound and 200-word stride. */
static void ShiftColumnU16(u16 *apCol, i32 nFirstBase, i32 nLastBase, i32 nEntries, i32 nStride)
{
    for (i32 nBase = nFirstBase; nBase < nLastBase; ++nBase) {
        for (i32 k = 0; k < nEntries; ++k) {
            apCol[nStride * nBase + k] = apCol[nStride * (nBase + 1) + k];
        }
    }
}

static i32 BaseDemolishSiteCount(const BaseDemolishEnv *pEnv)
{
    return (pEnv->pnSiteCountLive != NULL) ? *pEnv->pnSiteCountLive : pEnv->nSiteCount;
}

i32 Base_DemolishFacility(const BaseDemolishEnv *pEnv, i8 nFacType)
{
    if (pEnv == NULL || pEnv->pbBaseCount == NULL || pEnv->apSiteRecs == NULL ||
        pEnv->abFacilityGrid == NULL) {
        return 0;
    }

    i32 nCount = (i32)(i8)*pEnv->pbBaseCount; /* 0x66B90, the signed byte at 0x159D80 */
    const i32 nSiteCount = BaseDemolishSiteCount(pEnv);

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
        while (nSoldier < *pEnv->pnSoldierCount) {
            Soldier *pSol = pEnv->apSoldiers[nSoldier];
            if (pSol != NULL && pSol->nKind == nBase && pSol->bUnk183 > 0xFD) {
                nSoldier = 0; /* the original re-tests the same slot after a removal */
                *pEnv->pnSoldierCount =
                    Soldier_RemoveById(pEnv->apSoldiers, *pEnv->pnSoldierCount, pSol->nUnk440);
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
        for (i32 nSite = 0; nSite < nSiteCount; ++nSite) {
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
        for (i32 nSoldier = 0; nSoldier < *pEnv->pnSoldierCount; ++nSoldier) {
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

            /* The four product-block columns, each moved one base earlier: 0x6706C/0x67078 (the
             * engineers u16), 0x67088/0x67090 (committed u16), 0x670A0/0x670A8 (ordered u32) and
             * 0x670B0/0x670BC (accumulator u32, `LDR R5,[R1,#4]` -> `STR R5,[R1,#-0x31C]`). The
             * accumulator was missing from the port until Part 117: the two dword pairs sit on the
             * same register with offsets 0x1904 and 4, so both columns move by one 800-byte column
             * and neither one is optional. */
            if (pEnv->apProductA != NULL) {
                for (i32 k = 0; k < nObjectCount; ++k) {
                    pEnv->apProductA[PUFO_BASE_COLUMN_ENTRIES * nDst + k] =
                        pEnv->apProductA[PUFO_BASE_COLUMN_ENTRIES * nSrc + k];
                }
            }
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

        /* Raw 0x67070/0x67078 copies source row s to s-1, starting at
         * s=removed+1. The discarded final row is not read as another source. */
        if (pEnv->apManufCols != NULL) {
            ShiftColumnU16(pEnv->apManufCols, nBase, nCount - 1, nObjectCount,
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
                /* 0x62920 calls Base_UpdateWorkshopNeeded, whose own body calls Base_CountActiveManuf
                 * @ 0x68264 -- and that function takes no count: it reads one byte at 0x160C4C (144,
                 * stored once at 0x859E4) and sums only that many u16 of the base's 200-entry column.
                 * The port hoists the call out, so the count has to be the shared one; charging the full
                 * 200 lets products 144..199 raise the workshop budget the original never sees. */
                (void)Base_UpdateWorkshopNeeded(pGrid, nBase,
                    Base_CountActiveManuf(pEnv->apManufCols + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase,
                                          pEnv->nManufEntries));                       /* 0x62920, 0x68278 */

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
 * also have bPsiAssigned @ 0x95 set — the psi marker. The budget of Base_UpdatePsiLabNeeded. */
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
        if ((i32)pSol->nKind == (i8)nBaseIndex && pSol->bPsiAssigned != 0) {
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
 * Needed-pass callers use nSiteIdx = -1. The raw gate address is 0x161D4C + 2*v,
 * inside carry-count words 159..229 at 0x161AD0. This PE zero-fill region stays zero:
 * the screen clears all 256 words, and its only nonzero writers use admitted row IDs
 * 1..24 or 31..51. Thus the port uses a zero gate without reading outside aSites.
 * Evidence: docs/parity/incoming-capacity-gate-2026-10-28.md. */
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

    /* One counter, three stores, in the original's order: the base record's site byte (BaseRec_ctor
     * 0x556C0 reads nNextBaseSiteId @ 0x186558 directly), the entry's nSiteId (0x669A8), and the site
     * record's nFacType (0x66A34). They agree because they are read from one cell before 0x66B50
     * raises it -- that agreement is what SmapHit_FindBaseBySiteId (0x82ED4) and OptionsScreen_Open
     * (0x6D12C) match on. */
    (void)BaseRec_Create(pEnv->apBases, nBaseCount, (u8)*pEnv->pnNextSiteId, pEnv->apObjects,
                         (i32)pEnv->nObjectCount);

    (void)StrategList_AddItem(pEnv->pStrategList, pEnv->pnSeed, pEnv->nZoomMode, 0, 0, 0,
                              x, y, *pEnv->pnNextSiteId, 1);

    /* 0x669B4: the strateg entry this base owns gets the region index. The ARM is
     * SUB R11, R10, #0x80; Hex-Rays renders it as + 0x80. */
    pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, nBaseCount);
    if (pItem != NULL) {
        pItem->bRandA = (u8)(nRegionId - 0x80);
    }

    pSite = &pEnv->apSiteRecs[nBaseCount];

    /* The ARM copies byte by byte through NUL at 0x669D0. Its production names are "new" or
     * TEXTS[905 + region] + '-' + a signed byte count. Shipped buildable region text is at most
     * 16 bytes and the count at most four, so 21 name bytes plus NUL fit this 22-byte live view.
     * The last two bytes are skipped by the unchanged 20-byte save field. */
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
        /* The ARM walks a u16 view and a dword view of this base's columns and clears entry [i] of
         * four of them (Part 116 measured the offsets; Part 117 names the columns). The u16 view
         * R3 is auManufCommitted, R3 + 0x1100 is auManufCol; the dword view LR is the accumulator,
         * LR + 0x1900 is aManufProductB. The port keeps the four columns in four arrays, so the
         * neighbour offsets become four pointers -- writing [i + 2176] / [i + 1600] inside one array
         * landed in that array's own filler and left the engineers and ordered-units columns
         * untouched. */
        for (i = 0; i < (i32)pEnv->nObjectCount; ++i) {
            pEnv->pManufCol[i] = 0;        /* 0x66B1C STRH [R3,#0x1100] */
            pEnv->pProductU16Col[i] = 0;   /* 0x66B24 STRH [R3] */
            pEnv->pProductReqCol[i] = 0;   /* 0x66B2C STR [LR,#0x1900] */
            pEnv->pProductU32Col[i] = 0;   /* 0x66B34 STR [LR] */
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
        PadImage_BlitShimmed(pOverlayPad); /* 0x5d6e0, QoL shimmed origin */
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

/* 0x6095C / 0x60ADC / 0x60BBC / 0x610B4 / 0x611E0: the original's strcpy-until-NUL loop out of a
 * TEXTS record, with no bound on the 100-byte stack buffer. Bounded here (deviation); the longest
 * of the records this file copies is well inside the buffer, so no real text is clipped. */
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

/* The four corner buttons share everything except the three differences listed in the header, so
 * they share one body. The store order is the originals' order in every case. */
static void RadarSetCursor(const RadarCursorEnv *pEnv, i32 nX, i32 nY, i32 nRange, u8 bFlip,
                           i32 nRecMsg)
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
    /* The shared radar message gets this corner's mode text: TEXTS[1595..1598], one record per setter
     * (0x10360F / 0x1036DC / 0x1037A9 / 0x103876 = 0xB38D0 + 205 * n). */
    if (pEnv->pszMsg != NULL && pEnv->cchMsg > 0 && pEnv->pTextRecs != NULL) {
        Radar_CopyTextRec(pEnv->pTextRecs, nRecMsg, pEnv->pszMsg, pEnv->cchMsg);
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
                   PUFO_RADAR_RANGE_FULL, 0, PUFO_RADAR_TEXT_CURSOR_NW);
}

void RadarScreen_SetCursorSE(const RadarCursorEnv *pEnv)
{
    RadarSetCursor(pEnv, PUFO_RADAR_CURSOR_EAST, PUFO_RADAR_CURSOR_SOUTH,
                   PUFO_RADAR_RANGE_FULL, 1, /* the only one of the four that sets the flip byte */
                   PUFO_RADAR_TEXT_CURSOR_SE);
}

void RadarScreen_SetCursorNE(const RadarCursorEnv *pEnv)
{
    if (pEnv != NULL && pEnv->pnInterceptA != NULL && pEnv->pnInterceptB != NULL &&
        *pEnv->pnInterceptA == PUFO_RADAR_NO_TARGET &&
        *pEnv->pnInterceptB == PUFO_RADAR_NO_TARGET) {
        return; /* 0x5E818 / 0x5E830: no intercept target, nothing to point at */
    }
    RadarSetCursor(pEnv, PUFO_RADAR_CURSOR_EAST, PUFO_RADAR_CURSOR_NORTH,
                   RadarScaleRange(pEnv, true), 0, PUFO_RADAR_TEXT_CURSOR_NE);
}

void RadarScreen_SetCursorSW(const RadarCursorEnv *pEnv)
{
    if (pEnv != NULL && pEnv->pnInterceptA != NULL && pEnv->pnInterceptB != NULL &&
        *pEnv->pnInterceptA == PUFO_RADAR_NO_TARGET &&
        *pEnv->pnInterceptB == PUFO_RADAR_NO_TARGET) {
        return; /* 0x5E94C / 0x5E964 */
    }
    RadarSetCursor(pEnv, PUFO_RADAR_CURSOR_WEST, PUFO_RADAR_CURSOR_SOUTH,
                   RadarScaleRange(pEnv, false), 0, PUFO_RADAR_TEXT_CURSOR_SW);
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

    /* 0x6723C compares R1 with 10, then branches to 0x671E8, the BLT after the normal
     * lower-bound compare. A set gate uses 10 as the lower x bound; it does not replace x. */
    const bool bGateOpen = (pEnv->pbGateTransfer != NULL && (*pEnv->pbGateTransfer & 0xFFu) != 0) ||
                           (pEnv->pbGateBaseInfo != NULL && (*pEnv->pbGateBaseInfo & 0xFFu) != 0) ||
                           (pEnv->pbGatePsi != NULL && (*pEnv->pbGatePsi & 0xFFu) != 0);
    const i32 nMinX = bGateOpen ? 10 : PUFO_BASE_STRIP_X_FIRST;

    const i32 nBaseCount = (pEnv->pbBaseCount != NULL) ? (i8)*pEnv->pbBaseCount : 0;   /* 0x67204 */
    const i8 bSelected = (pEnv->pbSelectedBase != NULL) ? (i8)*pEnv->pbSelectedBase : 0; /* 0x67224 */

    if (nX < nMinX || nX > PUFO_BASE_STRIP_X_LAST ||
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
                    SoftSurf_BlitRectProxy(0, PufoUi_MapX(nX), PufoUi_MapY(nY), 4, 4, 0x96u, 0u, 0u);
                    SoftSurf_BlitRectProxy(0, PufoUi_MapX(nX + 1), PufoUi_MapY(nY + 1), 2, 2, 0xFFu, 0u, 0u);
                } else {                          /* 0x62660: finished */
                    SoftSurf_BlitRectProxy(0, PufoUi_MapX(nX), PufoUi_MapY(nY), 4, 4, 0u, 0x96u, 0u);
                    SoftSurf_BlitRectProxy(0, PufoUi_MapX(nX + 1), PufoUi_MapY(nY + 1), 2, 2, 0u, 0xFFu, 0u);
                }

                if (abGrid[nFac + PUFO_GRID_TILEA_OFF + nCell] == 0) { /* 0x626B8: no tile */
                    SoftSurf_BlitRectProxy(0, PufoUi_MapX(nX), PufoUi_MapY(nY), 4, 4, 0x14u, 0x14u, 0x14u);
                    SoftSurf_BlitRectProxy(0, PufoUi_MapX(nX + 1), PufoUi_MapY(nY + 1), 2, 2, 0x28u, 0x28u, 0x28u);
                }

                /* 0x62720: the chevron, inside the per-cell loop, so the original paints it once
                 * per cell of the marked base. */
                if (nBase == pEnv->nHighlightRow) {
                    for (i32 k = 0; k < PUFO_STRIP_HILITE_STEPS; ++k) {
                        SoftSurf_BlitRectProxy(0, PufoUi_MapX(PUFO_STRIP_CELL_X_FIRST + k), PufoUi_MapY(nYBase + 4 + k),
                                               2, 2, 0xFFu, 0u, 0u);
                        SoftSurf_BlitRectProxy(0, PufoUi_MapX(PUFO_STRIP_CELL_X_FIRST + k), PufoUi_MapY(nYBase + 26 - k),
                                               2, 2, 0xFFu, 0u, 0u);
                    }
                }
            }
        }
    }
}

/* The wrapped map backdrop, written twice in the binary: 0x5EB44..0x5ECE0 inside
 * RadarScreen_BlitMapBackdrop and 0x6C1A4..0x6C340 inline in Geoscape_DrawScreen. The two are
 * instruction-for-instruction the same, so the port keeps one copy. The map pad is blitted twice
 * (at the scroll position, then one map width further on); the layer pad is blitted three times
 * (at the overlay origin, one map width left, one map width right), each of those three skipped
 * when the screen id is 2. */
static void Smap_BlitWrappedBackdrop(const SmapView *pView, PadImage *pMapPad,
                                     PadImage *pLayerPad, const u8 *pWorldBits,
                                     i32 nOverlayOriginX, i32 nScreenId)
{
    /* Map half-extent minus screen half-extent. The MOVS / ADDMI #1 / ASR #1 triple the ARM uses
     * is C division by two: ADDMI only fires on a negative value. */
    const i32 nHalfW = pView->nMapW / 2 - PUFO_RADAR_HALF_SCREEN_X; /* R7 */
    const i32 nHalfH = pView->nMapH / 2 - PUFO_RADAR_HALF_SCREEN_Y; /* R6 */

    /* The map pad centred on the scroll position, negated. */
    pMapPad->nOriginX = -(pView->nScrollX + nHalfW);
    pMapPad->nOriginY = -(pView->nScrollY + nHalfH);
    PadImage_Blit(pMapPad);

    /* The same pad one map width further on, which is the wrap copy. The original rewrites
     * nOriginY with the identical value here; reproduced as written. */
    pMapPad->nOriginX = -(pView->nScrollX + pView->nMapW + nHalfW);
    pMapPad->nOriginY = -(pView->nScrollY + nHalfH);
    PadImage_Blit(pMapPad);

    /* The layer pad takes the overlay origin, not the pad origin, on x; on y it copies whatever
     * the map pad ended with. */
    pLayerPad->nOriginX = nOverlayOriginX - pView->nScrollX - nHalfW;
    pLayerPad->nOriginY = pMapPad->nOriginY;
    if (nScreenId != PUFO_RADAR_SCREEN_NO_WRAP) {
        SoftSurf_BlitFlip(pLayerPad->nOriginX, pLayerPad->nOriginY, pWorldBits,
                          g_aPalette[PUFO_RADAR_MAP_PAL_SLOT], pLayerPad->pData);
    }

    /* One map width to the left. */
    pLayerPad->nOriginX = nOverlayOriginX - pView->nScrollX - pView->nMapW - nHalfW;
    pLayerPad->nOriginY = pMapPad->nOriginY;
    if (nScreenId != PUFO_RADAR_SCREEN_NO_WRAP) {
        SoftSurf_BlitFlip(pLayerPad->nOriginX, pLayerPad->nOriginY, pWorldBits,
                          g_aPalette[PUFO_RADAR_MAP_PAL_SLOT], pLayerPad->pData);
    }

    /* One map width to the right. The ARM's order is bias - scroll - halfW, then + nMapW. */
    pLayerPad->nOriginX = nOverlayOriginX - pView->nScrollX - nHalfW + pView->nMapW;
    pLayerPad->nOriginY = pMapPad->nOriginY;
    if (nScreenId != PUFO_RADAR_SCREEN_NO_WRAP) {
        SoftSurf_BlitFlip(pLayerPad->nOriginX, pLayerPad->nOriginY, pWorldBits,
                          g_aPalette[PUFO_RADAR_MAP_PAL_SLOT], pLayerPad->pData);
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

    Smap_BlitWrappedBackdrop(pEnv->pView, pEnv->pMapPad, pEnv->pLayerPad, pEnv->pWorldBits,
                             pEnv->nOverlayOriginX, pEnv->nScreenId);
    return true;
}

/* Pull one scroll axis into (-nMapSize - nHalf, -nHalf] by at most one map width per step.
 * The ARM does it as two conditional stores that re-read the value between them: CMP the
 * (value + half) sum against -nMapSize and ADDMI-free `ADDLT` it up by one map width, then CMP
 * the SAME sum against 0 and `SUBGT` it back down. */
static void Smap_WrapScrollAxis(i32 *pnScroll, i32 nMapSize, i32 nHalf)
{
    i32 nValue = *pnScroll;

    if (nValue + nHalf < -nMapSize) { /* 0x8C3F4..0x8C408, 0x8C478..0x8C488 */
        nValue += nMapSize;
        *pnScroll = nValue;
    }
    if (nValue + nHalf > 0) { /* 0x8C40C..0x8C414, 0x8C48C..0x8C494 */
        nValue -= nMapSize;
        *pnScroll = nValue;
    }
}

/* Geoscape_Draw @ 0x8c180's map-view chrome, 0x8C2B0..0x8C690. See the header for the
 * evidence map. */
void Geoscape_BlitMapView(GeoscapeMapViewEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pView == NULL || pEnv->pMapPad == NULL || pEnv->pLayerPad == NULL
        || pEnv->pWorldBits == NULL || pEnv->pMapPal == NULL || pEnv->pTime == NULL
        || pEnv->pnOverlayOriginX == NULL) {
        return;
    }

    SmapView *pView = pEnv->pView;
    PadImage *pMapPad = pEnv->pMapPad;
    PadImage *pLayerPad = pEnv->pLayerPad;
    const bool bWrapY = (pEnv->nScreenId == PUFO_RADAR_SCREEN_NO_WRAP);

    /* 0x8C2B8..0x8C2EC: map half-extent minus screen half-extent. MOVS / ADDMI #1 / ASR #1 is
     * C's division by two, so a negative extent rounds the C way. */
    const i32 nHalfW = pView->nMapW / 2 - PUFO_RADAR_HALF_SCREEN_X;
    const i32 nHalfH = pView->nMapH / 2 - PUFO_RADAR_HALF_SCREEN_Y;

    /* 0x8C2F4..0x8C3D4: the overlay drift. _rt_sdiv takes the DIVISOR in R0 (-24) and the
     * dividend in R1 (the map pad's width), so nDiv = uWidth / -24; the double chain is
     * (nHour * nDiv) - ((double)uWidth * (double)nMinute) * (1/1440), truncated by _dtoi. */
    const i32 nWidth = (i32)pMapPad->uWidth;
    const i32 nDiv = nWidth / PUFO_SMAP_OVERLAY_DIVISOR;
    const double fHour = (double)(pEnv->pTime->nHour * nDiv);
    const double fDrift = ((double)nWidth * (double)pEnv->pTime->nMinute)
                          * (1.0 / (double)PUFO_SMAP_OVERLAY_MINUTES_PER_DAY);
    *pEnv->pnOverlayOriginX = (i32)(fHour - fDrift);

    /* 0x8C3D8..0x8C494: the wrap. X for every screen id; Y only for id 2, because 0x8C3F0's
     * BNE sends every other id to a copy that clamps X and falls into the blit block. */
    Smap_WrapScrollAxis(&pView->nScrollX, pView->nMapW, nHalfW);
    if (bWrapY) {
        Smap_WrapScrollAxis(&pView->nScrollY, pEnv->pView->nMapH, nHalfH);
    }

    /* 0x8C498..0x8C548: the map pad. Screen 2 gets the two vertical wrap copies as well; every
     * id ends with the x + nMapW copy at the original y. */
    pMapPad->nOriginX = -(pView->nScrollX + nHalfW);
    pMapPad->nOriginY = -(pView->nScrollY + nHalfH);
    PadImage_Blit(pMapPad);

    if (bWrapY) {
        pMapPad->nOriginX = -(pView->nScrollX + nHalfW);
        pMapPad->nOriginY = -(pView->nScrollY + pView->nMapH + nHalfH);
        PadImage_Blit(pMapPad);

        pMapPad->nOriginX = -(pView->nScrollX + pView->nMapW + nHalfW);
        pMapPad->nOriginY = -(pView->nScrollY + pView->nMapH + nHalfH);
        PadImage_Blit(pMapPad);
    }

    pMapPad->nOriginX = -(pView->nScrollX + pView->nMapW + nHalfW);
    pMapPad->nOriginY = -(pView->nScrollY + nHalfH);
    PadImage_Blit(pMapPad);

    /* 0x8C548..0x8C690: the layer pad's three world flips, one map width apart, each skipped
     * for screen 2. Its y is copied out of the map pad after the last map blit, as the ARM
     * copies it. */
    const i32 nOverlay = *pEnv->pnOverlayOriginX;

    pLayerPad->nOriginX = nOverlay - pView->nScrollX - nHalfW;
    pLayerPad->nOriginY = pMapPad->nOriginY;
    if (!bWrapY) {
        SoftSurf_BlitFlip(pLayerPad->nOriginX, pLayerPad->nOriginY, pEnv->pWorldBits,
                          pEnv->pMapPal, pLayerPad->pData);
    }

    pLayerPad->nOriginX = nOverlay - pView->nScrollX - pView->nMapW - nHalfW;
    pLayerPad->nOriginY = pMapPad->nOriginY;
    if (!bWrapY) {
        SoftSurf_BlitFlip(pLayerPad->nOriginX, pLayerPad->nOriginY, pEnv->pWorldBits,
                          pEnv->pMapPal, pLayerPad->pData);
    }

    pLayerPad->nOriginX = nOverlay - pView->nScrollX - nHalfW + pView->nMapW;
    pLayerPad->nOriginY = pMapPad->nOriginY;
    if (!bWrapY) {
        SoftSurf_BlitFlip(pLayerPad->nOriginX, pLayerPad->nOriginY, pEnv->pWorldBits,
                          pEnv->pMapPal, pLayerPad->pData);
    }
}

/* Geoscape_Draw @ 0x8c180's crosshair tail, 0x8C69C..0x8C760. */
void Geoscape_BlitCrosshair(const GeoscapeCrosshairEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pChromePad == NULL || pEnv->pbPending == NULL
        || pEnv->pnCooldownR == NULL) {
        return;
    }

    if (*pEnv->pbPending != 0 && *pEnv->pnCooldownR < PUFO_SMAP_CROSSHAIR_COOLDOWN) {
        /* 0x8C6CC..0x8C700: the vertical line, 1 wide and screen-height minus
         * chrome tall; 0x8C704..0x8C72C: the horizontal line, screen-wide.
         * QoL: the lengths follow the live surface (portrait: 320/240 exact). */
        SoftSurf_BlitRectProxy(0, pEnv->nCrosshairX + PUFO_SMAP_CROSSHAIR_SHIFT, 0, 1,
                               g_pMainSurface->nHeight - (i32)pEnv->pChromePad->uHeight,
                               PUFO_SMAP_CROSSHAIR_INDEX, PUFO_SMAP_CROSSHAIR_MID,
                               PUFO_SMAP_CROSSHAIR_INDEX);
        SoftSurf_BlitRectProxy(0, 0, pEnv->nCrosshairY + PUFO_SMAP_CROSSHAIR_SHIFT,
                               g_pMainSurface->nWidth, 1,
                               PUFO_SMAP_CROSSHAIR_INDEX, PUFO_SMAP_CROSSHAIR_MID,
                               PUFO_SMAP_CROSSHAIR_INDEX);
        return;
    }

    /* 0x8C750..0x8C75C: the pending flag is cleared and the cooldown parked. */
    *pEnv->pbPending = 0;
    *pEnv->pnCooldownR = PUFO_RADAR_COOLDOWN_READY;
}

/* 0x822EC..0x82304 and 0x8230C..0x8231C: MOVS R3,Rn / ADDMI R3,R3,#1 / MOV R3,R3,ASR#1, bytes
 * 01 30 83 42 for the middle instruction. ADDMI fires only when the tested value is NEGATIVE, so a
 * non-negative extent is halved by plain `n >> 1` and the +1 exists only to give a negative extent
 * C's round-toward-zero ((-5 + 1) >> 1 == -2 == -5 / 2). This is C's `n / 2`, NOT a ceiling -- the
 * identical encoding sits at 0x5EB44 (the radar backdrop) and 0x846B4 (the map click). */
static i32 SmapIcon_HalfExtent(i32 nSize, i32 nSub)
{
    return nSize / 2 - nSub;
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

    Clock_BlitDigitPair(pEnv, pEnv->pTime->nHour, PufoUi_MapX(90), PufoUi_MapX(100), PufoUi_MapY(260));               /* 0x5C618..0x5C65C */
    Clock_BlitDigitPair(pEnv, pEnv->pTime->nMinute, PufoUi_MapX(115), PufoUi_MapX(125), PufoUi_MapY(260));            /* 0x5C69C..0x5C6E0 */
    Clock_BlitDigitPair(pEnv, pEnv->pTime->nSecond, PufoUi_MapX(140), PufoUi_MapX(150), PufoUi_MapY(260));            /* 0x5C720..0x5C764 */

    Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);                  /* 0x5C790 */
    Text_DrawString(PufoUi_MapX(110), PufoUi_MapY(258), ":");                                             /* 0x5C7A8, asc_AB078 */
    Text_DrawString(PufoUi_MapX(135), PufoUi_MapY(258), ":");                                             /* 0x5C7C0 */

    if (pEnv->pTime->nWeekday >= 1 && pEnv->pTime->nWeekday <= 7) {             /* 0x5C7D0 */
        Clock_CopyText(szText, pEnv->pTextRecs +
                       PUFO_TEXT_REC_STRIDE * (PUFO_TEXT_WEEKDAY_FIRST + pEnv->pTime->nWeekday - 1));
    }
    /* Outside 1..7 the original draws whatever its uninitialised buffer held; the port draws the
     * empty string it left there. */

    Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);                 /* 0x5C984 */
    nWidth = Text_MeasureWidth(szText);                                         /* 0x5C998 */
    Text_DrawString(PufoUi_MapX(124) - nWidth / 2, PufoUi_MapY(272), szText);                             /* 0x5C9B0 */

    nDayTens = pEnv->pTime->nDay / 10;                                          /* 0x5C9D4 */
    nDayOnes = pEnv->pTime->nDay % 10;                                          /* 0x5C9E8 */
    pTensPad = Clock_SetPad(pEnv, nDayTens, PufoUi_MapX(90), PufoUi_MapY(287));                           /* 0x5C9FC / 0x5CA10 */
    if (pTensPad != NULL && nDayTens != 0) {                                    /* 0x5CA00 / 0x5CA14 */
        PadImage_BlitEx(pTensPad, 0);                                           /* 0x5CA18 */
    }
    pOnesPad = Clock_SetPad(pEnv, nDayOnes, PufoUi_MapX(100), PufoUi_MapY(287));                          /* 0x5CA28 / 0x5CA30 */
    if (pOnesPad != NULL) {
        PadImage_BlitEx(pOnesPad, 0);                                           /* 0x5CA38 */
    }

    Clock_CopyText(szText, pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_CLOCK_SEP); /* 0x5CA48 */
    Text_DrawString(PufoUi_MapX(110), PufoUi_MapY(287), szText);                                          /* 0x5CA78 */

    if (pEnv->pTime->nMonth >= 1 && pEnv->pTime->nMonth <= 12) {               /* 0x5CA88 */
        Clock_CopyText(szText, pEnv->pTextRecs +
                       PUFO_TEXT_REC_STRIDE * (PUFO_TEXT_MONTH_FIRST + pEnv->pTime->nMonth - 1));
    }
    Text_DrawString(PufoUi_MapX(130), PufoUi_MapY(287), szText);                                          /* 0x5CCFC */

    Clock_BlitDigit(pEnv, pEnv->pTime->nYear / 1000, PufoUi_MapX(105), PufoUi_MapY(304));                /* 0x5CD30 */
    Clock_BlitDigit(pEnv, pEnv->pTime->nYear / 100 % 10, PufoUi_MapX(115), PufoUi_MapY(304));            /* 0x5CD88 */
    Clock_BlitDigit(pEnv, pEnv->pTime->nYear / 10 % 10, PufoUi_MapX(125), PufoUi_MapY(304));             /* 0x5CDDC */
    Clock_BlitDigit(pEnv, pEnv->pTime->nYear % 10, PufoUi_MapX(135), PufoUi_MapY(304));                  /* 0x5CE14..0x5CE38 */
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
        pHundreds->nOriginX = PufoUi_MapX(PUFO_RADAR_FUEL_X_HUNDREDS); /* 0x5DA5C..0x5DA60 */
        pHundreds->nOriginY = PufoUi_MapY(PUFO_RADAR_FUEL_Y); /* 0x5DA74 */
        if (nHundreds != 0) {                             /* 0x5DA6C */
            PadImage_BlitEx(pHundreds, PUFO_RADAR_PAD_KEY);
        }
    }

    PadImage *pTens = RadarDigitPad(pEnv, nTens);
    if (pTens != NULL) {
        pTens->nOriginX = PufoUi_MapX(PUFO_RADAR_FUEL_X_TENS); /* 0x5DA88..0x5DA90 */
        pTens->nOriginY = PufoUi_MapY(PUFO_RADAR_FUEL_Y); /* 0x5DA98 */
        /* 0x5DA9C skips the blit when the tens digit is non-zero and 0x5DAA4 skips it when the
         * hundreds digit is not 1, so a zero tens digit is drawn only at 100%. */
        if (nTens != 0 || nHundreds == 1) {
            PadImage_BlitEx(pTens, PUFO_RADAR_PAD_KEY);
        }
    }

    PadImage *pUnits = RadarDigitPad(pEnv, nUnits);
    if (pUnits != NULL) {
        pUnits->nOriginX = PufoUi_MapX(PUFO_RADAR_FUEL_X_UNITS); /* 0x5DABC..0x5DAC4 */
        pUnits->nOriginY = PufoUi_MapY(PUFO_RADAR_FUEL_Y); /* 0x5DACC */
        PadImage_BlitEx(pUnits, PUFO_RADAR_PAD_KEY); /* 0x5DAD4, unconditional */
    }

    PadImage *pPercent = RadarDigitPad(pEnv, PUFO_RADAR_DIGIT_PADS - 1);
    if (pPercent != NULL) {
        pPercent->nOriginX = PufoUi_MapX(PUFO_RADAR_FUEL_X_PERCENT); /* 0x5DADC..0x5DAE4 */
        pPercent->nOriginY = PufoUi_MapY(PUFO_RADAR_FUEL_Y); /* 0x5DAEC */
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
    SoftSurf_BlitRectProxy(0, PufoUi_MapX(PUFO_RADAR_RANGE_BAR_X), PufoUi_MapY(PUFO_RADAR_RANGE_BAR_Y - nBar),
                           3, nBar - PUFO_RADAR_RANGE_TRIM, 0x28u, 0x64u, 0u);
    SoftSurf_BlitRectProxy(0, PufoUi_MapX(PUFO_RADAR_RANGE_BAR_X + 1), PufoUi_MapY(PUFO_RADAR_RANGE_BAR_Y - nBar),
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

/* Radar_CopyTextRec lives above RadarSetCursor, which is its other user. */

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
    if (!Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(PUFO_RADAR_PANEL_X), PufoUi_MapY(PUFO_RADAR_PANEL_Y),
                           PUFO_RADAR_PANEL_W, nPanelH, 0x28, 0x50, 0x14, 0xFF, 4)) {
        return false; /* 0x605F0: the whole draw block is skipped, the tail gates still run */
    }
    for (i32 i = 1; i < 5; ++i) {
        const RadarPanelBlit *const pBlit = &g_aRadarPanelBlit[i];
        (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(pBlit->x), PufoUi_MapY(pBlit->y), pBlit->nW, pBlit->nH,
                                0x28, 0x50, 0x14, 0xFF, pBlit->nMode);
    }
    for (i32 i = 0; i < 5; ++i) {
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(g_aRadarPanelRail[i].x), PufoUi_MapY(g_aRadarPanelRail[i].y),
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
        const i32 x = PufoUi_MapX(n + PUFO_RADAR_BAR_X_FIRST + i * PUFO_RADAR_BAR_PITCH);
        SoftSurf_BlitRectProxy(0, x, PufoUi_MapY(PUFO_RADAR_BAR_Y_TOP), PUFO_RADAR_BAR_PITCH,
                               PUFO_RADAR_BAR_H_TOP, PUFO_RADAR_BAR_IDX,
                               g_anRadarBarMid[i], PUFO_RADAR_BAR_LOW);      /* 0x60CEC..0x60D94 */
        SoftSurf_BlitRectProxy(0, x, PufoUi_MapY(PUFO_RADAR_BAR_Y_MID), PUFO_RADAR_BAR_PITCH,
                               PUFO_RADAR_BAR_H_MID, PUFO_RADAR_BAR_IDX,
                               g_anRadarBarMid[i], PUFO_RADAR_BAR_LOW);      /* 0x60DC0..0x60E60 */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(2 * (n + PUFO_RADAR_BAR_X_DOUBLED + i * PUFO_RADAR_BAR_PITCH)),
                               PufoUi_MapY(PUFO_RADAR_BAR_Y_BOTTOM), 2 * PUFO_RADAR_BAR_PITCH,
                               PUFO_RADAR_BAR_H_BOTTOM, PUFO_RADAR_BAR_IDX,
                               g_anRadarBarMid[i], PUFO_RADAR_BAR_LOW);      /* 0x60E8C..0x60F3C */
        /* 0x60F64: MOVS / ADDMI / ASR #1 is C division by two, rounding toward zero. */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(*pS->pnCursorX + n / 2 + PUFO_RADAR_CURSOR_X_FIRST
                               + i * PUFO_RADAR_CURSOR_PITCH), PufoUi_MapY(*pS->pnCursorY + 1),
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
    Text_DrawString(PufoUi_MapX(120), PufoUi_MapY(155), szText); /* 0x610F8 */

    char szNum[52];
    (void)snprintf(szNum, sizeof(szNum), "%d", (int)*pS->pnRangeCur); /* 0x6110C: j_itoa, base 10 */
    Text_DrawString(PufoUi_MapX(180), PufoUi_MapY(155), szNum); /* 0x61120 */

    if (*pS->pnMsgTimer < PUFO_RADAR_TIMER_LIMIT) { /* 0x6112C */
        Text_DrawString(PufoUi_MapX(28), PufoUi_MapY(175), pEnv->pDetect->pszMsg);
    }

    if (*pS->pbBlinkPhase != 0) { /* 0x61150 */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(PUFO_RADAR_WASH_X), PufoUi_MapY(PUFO_RADAR_WASH_Y), PUFO_RADAR_WASH_W,
                               PUFO_RADAR_WASH_H, PUFO_RADAR_WASH_IDX,
                               PUFO_RADAR_WASH_MID, PUFO_RADAR_WASH_LOW);
        RadarScreen_BlitOverlay(pUi->pOverlayPad); /* 0x611A0 */
    }

    for (i32 i = 0; i < PUFO_RADAR_BUTTON_BLIT_COUNT; ++i) { /* 0x611A8: MOV R8, #4 */
        PadImage_BlitExShimmed(pUi->apButtonPad[i], PUFO_RADAR_BUTTON_KEY); /* QoL shimmed origin */
    }

    Radar_CopyTextRec(pEnv->pDetect->pTextRecs, PUFO_RADAR_TEXT_DAMAGE_LABEL,
                      szText, sizeof(szText));
    Gfx_PackRgb565Pair(pEnv->pColor, 120, 200, 40, 40, 80, 20); /* 0x61214 */
    Text_DrawString(PufoUi_MapX(117), PufoUi_MapY(59), szText); /* 0x61228 */
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

/* --- Geoscape_DrawScreen @ 0x6c16c ------------------------------------------------------- */

/* 0x6C40C and fourteen more: the original copies a TEXTS record into the 200-byte psz with an
 * unbounded LDRSB / STRB loop. A record is 200 bytes on disk and psz is 200 bytes, so a record
 * with no NUL inside its own 200 bytes would run past the buffer. The port caps the copy -- a
 * hardening deviation, unreachable with the shipped lang.dat. */
static void DrawScreen_CopyTextRec(char *pszDst, size_t cchDst, const char *pRec)
{
    size_t i = 0;

    while (i + 1 < cchDst && pRec[i] != '\0') {
        pszDst[i] = pRec[i];
        ++i;
    }
    pszDst[i] = '\0';
}

/* 120 - Text_MeasureWidth(psz) / 2, the centring every text site in this function uses. */
static void DrawScreen_DrawCentered(i32 nY, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz);

    Text_DrawString(120 - nWidth / 2, nY, psz);
}

/* The same centring anchored on a control: the label sits one pixel below the control's own y,
 * which the original reads as a signed halfword (LDRSH [R1,#2] at 0x6C5B4, 0x6CBE0, 0x6CED0). */
static void DrawScreen_DrawCtlLabel(const UiControl *pCtl, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz);

    Text_DrawString(120 - nWidth / 2, (i32)pCtl->y + 1, psz);
}

static void DrawScreen_BlitMenuFrame(i32 nY, i32 nHeight)
{
    SoftSurf_BlitSrcRect(20, nY, 200, nHeight, PUFO_GEOOPT_WASH_IDX,
                         PUFO_GEOOPT_WASH_MID, PUFO_GEOOPT_WASH_LOW, PUFO_GEOOPT_WASH_KEY);
}

/* Cases 1 and 2 are the same seven rows; only the title record differs. Each row blits its own
 * control and draws that control's slot label under it -- the original passes the label pointer
 * straight through, with no copy (0x6CBD4 MOV R3,R5). */
static void DrawScreen_DrawSlotList(const GeoscapeScreenDrawEnv *pEnv, i32 nTitleRec)
{
    i32 i;

    DrawScreen_BlitMenuFrame(10, 250);                                   /* 0x6CB50 / 0x6CA40 */
    DrawScreen_DrawCentered(18, pEnv->pTextRecs
                                + PUFO_TEXT_REC_STRIDE * nTitleRec);     /* 0x6CBAC / 0x6CA98 */
    for (i = 0; i < PUFO_GEOOPT_SLOT_COUNT; ++i) {
        UiControl *pCtl = pEnv->apOptCtl[i];

        (void)UiControl_BlitActivePad(pCtl, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);               /* 0x6CBC0 / 0x6CAAC */
        DrawScreen_DrawCtlLabel(pCtl, (pEnv->pSlotLabels != NULL)
                                          ? pEnv->pSlotLabels + PUFO_GEOOPT_LABEL_STRIDE * i
                                          : "");                          /* 0x6CBF4 / 0x6CAE0 */
    }
}

void Geoscape_DrawScreen(const GeoscapeScreenDrawEnv *pEnv)
{
    char szText[200]; /* psz @ sp+0x18 */
    char szNum[200];  /* v74 @ sp+0xE0, the itoa scratch */
    i32  i;

    /* Deviation: the original never initialises psz, and its switch default (0x6C6B8 BNE loc_6CEB4)
     * jumps straight to the tail, which then draws whatever the stack held at sp+0x18. Every id the
     * game actually uses (0..4) fills the buffer first, so the port initialises it instead: the
     * known paths are unchanged and an unknown id draws nothing rather than stack garbage. */
    szText[0] = '\0';
    szNum[0] = '\0';

    if (pEnv == NULL) {
        return;
    }

    /* 0x6C180: a pending map selection closes the geoscape UI and this frame draws nothing. */
    if (pEnv->pbMapSelection != NULL && *pEnv->pbMapSelection != 0) {
        if (pEnv->pfnCloseUi != NULL) {
            pEnv->pfnCloseUi(pEnv->pCloseUser); /* 0x6C188 */
        }
        return;                                 /* 0x6C194 B loc_6CEF0 */
    }

    /* 0x6C1A4..0x6C340: the wrapped backdrop, then the icons, the strip pad and the clock. The
     * port guards the four backdrop inputs; the original reaches them through live globals. */
    if (pEnv->pView != NULL && pEnv->pMapPad != NULL && pEnv->pLayerPad != NULL
        && pEnv->pWorldBits != NULL) {
        Smap_BlitWrappedBackdrop(pEnv->pView, pEnv->pMapPad, pEnv->pLayerPad, pEnv->pWorldBits,
                                 pEnv->nOverlayOriginX, pEnv->nScreenId);
    }
    /* The original passes only &gSmapCam and lets Smap_DrawStrategIcons read the animation phase
     * out of Map + 0x60 itself; the port hands it in through the icon environment. */
    const SmapIconDrawEnv iconEnv = { pEnv->pStrategList, pEnv->nAnimPhase, pEnv->nMapBitsBase };
    Smap_DrawStrategIcons(pEnv->pView, &iconEnv);                        /* 0x6C348 */
    if (pEnv->pStripPad != NULL) {
        PadImage_Blit(pEnv->pStripPad);                                   /* 0x6C354 */
    }
    if (pEnv->pTime != NULL) {
        const GeoscapeClockDrawEnv clockEnv = { pEnv->apClockPads, pEnv->pTextRecs, pEnv->pTime,
                                                pEnv->pColor };
        (void)GameTime_Decode(pEnv->pTime);                               /* 0x6C35C */
        Geoscape_DrawClockPads(&clockEnv);                                /* 0x6C364 */
    }

    /* Port-side guards. The original reaches the overlay state through globals that are always
     * live; the port takes them as arguments, so a missing one means nothing can be drawn on top
     * of the map and the frame ends after the map itself. */
    if (pEnv->pTextRecs == NULL || pEnv->apOptCtl == NULL || pEnv->pCtlA == NULL) {
        return;
    }

    /* 0x6C3B4: the options overlay. Ui_AnimExpandBlit returns false until the animation has
     * filled the rect, and then the whole overlay -- panels, column rules, labels -- is skipped
     * and the frame falls through to the menu branch. */
    if (pEnv->pbOptionsOpen != NULL && *pEnv->pbOptionsOpen != 0
        && Ui_AnimExpandBlit(pEnv->pAnim, PUFO_GEOOPT_ANIM_X, PUFO_GEOOPT_ANIM_Y,
                             PUFO_GEOOPT_ANIM_W, PUFO_GEOOPT_ANIM_H, 0, PUFO_GEOOPT_ANIM_SRC_X,
                             PUFO_GEOOPT_ANIM_SRC_Y, 0, PUFO_GEOOPT_ANIM_MODE)) {
        if (pEnv->pPanelShips == NULL || pEnv->pPanelMidA == NULL || pEnv->pPanelMidB == NULL
            || pEnv->pPanelRight == NULL) {
            return;
        }
        UiPanel_DrawText(pEnv->pPanelShips, pEnv->pColor);                             /* 0x6C3C8, 0x159E18 */
        UiPanel_DrawText(pEnv->pPanelMidA, pEnv->pColor);                              /* 0x6C3D4, 0x159E78 */
        UiPanel_DrawText(pEnv->pPanelMidB, pEnv->pColor);                              /* 0x6C3E0, 0x159E7C */
        UiPanel_DrawText(pEnv->pPanelRight, pEnv->pColor);                             /* 0x6C3EC, 0x159E1C */

        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_OPT_COLUMNS);
        Text_DrawString(8, 50, szText);                                  /* 0x6C430 */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_INTERCEPT);
        DrawScreen_DrawCentered(33, szText);                             /* 0x6C488 */

        /* 0x6C4D0..0x6C564: three column rules and the rule under them, all measured against
         * pOptPanelMidA's own font height. */
        const i32 nRuleH = PUFO_GEOOPT_RULE_FONT_MUL * pEnv->pPanelMidA->nFont;
        SoftSurf_BlitRectProxy(0, PUFO_GEOOPT_RULE_X0, PUFO_GEOOPT_RULE_Y, 1, nRuleH,
                               PUFO_GEOOPT_RULE_IDX, PUFO_GEOOPT_RULE_MID, PUFO_GEOOPT_RULE_LOW);
        SoftSurf_BlitRectProxy(0, PUFO_GEOOPT_RULE_X1, PUFO_GEOOPT_RULE_Y, 1, nRuleH,
                               PUFO_GEOOPT_RULE_IDX, PUFO_GEOOPT_RULE_MID, PUFO_GEOOPT_RULE_LOW);
        SoftSurf_BlitRectProxy(0, PUFO_GEOOPT_RULE_X2, PUFO_GEOOPT_RULE_Y, 1, nRuleH,
                               PUFO_GEOOPT_RULE_IDX, PUFO_GEOOPT_RULE_MID, PUFO_GEOOPT_RULE_LOW);
        SoftSurf_BlitRectProxy(0, 4, nRuleH + PUFO_GEOOPT_RULE_Y, PUFO_GEOOPT_RULE_WIDTH, 1,
                               PUFO_GEOOPT_RULE_IDX, PUFO_GEOOPT_RULE_MID, PUFO_GEOOPT_RULE_LOW);

        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_CANCEL);
        (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);               /* 0x6C59C */
        DrawScreen_DrawCtlLabel(pEnv->pCtlA, szText);                    /* 0x6C5D0 */
        for (i = 0; i < 2; ++i) {
            (void)UiControl_BlitActivePad(pEnv->apOptCtl[7 + i], pEnv->pfnCtlBlit,
                                          pEnv->pfnCtlBlitEx, pEnv->pCtlBlitUser); /* 0x6C5DC */
        }
        return;                                                          /* 0x6C5F4 */
    }

    /* 0x6C640: the geoscape menu is gated on a byte at 0x159E8C. */
    if (pEnv->abOptionsScratch == NULL || pEnv->abOptionsScratch[4] == 0) {
        return;                                                          /* 0x6C64C BEQ loc_6CEF0 */
    }

    SoftSurf_BlitSrcRect(0, 0, 240, 320, PUFO_GEOOPT_WASH_IDX, PUFO_GEOOPT_WASH_MID,
                         PUFO_GEOOPT_WASH_LOW, PUFO_GEOOPT_WASH_KEY);    /* 0x6C680 */

    switch (pEnv->abOptionsScratch[0]) {                                 /* LDRSB at 0x6C68C */
    case PUFO_GEOOPT_SAVE_LIST:
        DrawScreen_DrawSlotList(pEnv, PUFO_TEXT_REC_PICK_SAVE);
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_CANCEL);
        break;

    case PUFO_GEOOPT_LOAD_LIST:
        DrawScreen_DrawSlotList(pEnv, PUFO_TEXT_REC_PICK_LOAD);
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_CANCEL);
        break;

    case PUFO_GEOOPT_QUIT_ASK:
        DrawScreen_BlitMenuFrame(80, 100);                               /* 0x6C920 */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_QUIT_ASK);
        DrawScreen_DrawCentered(90, szText);                             /* 0x6C978 */
        (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);               /* 0x6C984 */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_YES);
        DrawScreen_DrawCtlLabel(pEnv->pCtlB, szText);                    /* 0x6C9E0, "Да" */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_NO);
        break;                                                           /* the tail draws "Нет" */

    case PUFO_GEOOPT_VOLUME:
        DrawScreen_BlitMenuFrame(80, 150);                               /* 0x6C6DC */
        SoftSurf_BlitRectProxy(0, 22, 82, 196, 1, PUFO_GEOOPT_BORDER_IDX,
                               PUFO_GEOOPT_BORDER_MID, PUFO_GEOOPT_BORDER_LOW);   /* 0x6C710 */
        SoftSurf_BlitRectProxy(0, 22, 228, 196, 1, PUFO_GEOOPT_BORDER_IDX,
                               PUFO_GEOOPT_BORDER_MID, PUFO_GEOOPT_BORDER_LOW);   /* 0x6C734 */
        SoftSurf_BlitRectProxy(0, 22, 82, 1, 146, PUFO_GEOOPT_BORDER_IDX,
                               PUFO_GEOOPT_BORDER_MID, PUFO_GEOOPT_BORDER_LOW);   /* 0x6C75C */
        SoftSurf_BlitRectProxy(0, 217, 82, 1, 146, PUFO_GEOOPT_BORDER_IDX,
                               PUFO_GEOOPT_BORDER_MID, PUFO_GEOOPT_BORDER_LOW);   /* 0x6C780 */
        for (i = 0; i < 2 * PUFO_GEOOPT_VOL_STEPS; ++i) {
            (void)UiControl_BlitActivePad(pEnv->apOptCtl[PUFO_GEOOPT_VOL_CONTROL0 + i],
                                          pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                          pEnv->pCtlBlitUser);           /* 0x6C78C, slots 9..20 */
        }
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_VOL_MUSIC);
        DrawScreen_DrawCentered(90, szText);                             /* 0x6C7F8 */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_VOL_SOUND);
        DrawScreen_DrawCentered(130, szText);                            /* 0x6C850 */
        /* 0x6C85C..0x6C8C0: the level scale, 0..5 on both rows. The original j_itoa's into the
         * same 200-byte scratch and copies it; snprintf is the bounded stand-in. */
        for (i = 0; i < PUFO_GEOOPT_VOL_STEPS; ++i) {
            (void)snprintf(szNum, sizeof(szNum), "%d", i);
            pufo_strncpy(szText, szNum, sizeof(szText));
            Text_DrawString(PUFO_GEOOPT_VOL_X0 + PUFO_GEOOPT_VOL_STEP * i, 111, szText);
            Text_DrawString(PUFO_GEOOPT_VOL_X0 + PUFO_GEOOPT_VOL_STEP * i, 151, szText);
        }
        /* Port-side (2026-11-04, user request): the music loop setting row at y 170.
         * The label is built in a LOCAL buffer in cp1251 (the font's encoding) --
         * not in szText, which the shared tail draws centred under the cancel
         * control. Not a TEXTS record: the shipped lang.dat is not modified. */
        if (pEnv->pbMusicLoop != NULL) {
            char szLoop[32];

            pufo_strncpy(szLoop, "\xC7\xE0\xF6\xE8\xAA\xEB\xE8\xF2\xE8\xF2\xFC: ",
                         sizeof(szLoop));
            (void)pufo_strcat(szLoop, (*pEnv->pbMusicLoop != 0u)
                                          ? "\xC2\xAA\xEB"   /* "Vkl" */
                                          : "\xC2\xF1\xAA\xEB", /* "Vykl" */
                                  sizeof(szLoop));
            DrawScreen_DrawCentered(170, szLoop);
        }
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_OK);
        break;

    case PUFO_GEOOPT_MENU:
        DrawScreen_BlitMenuFrame(50, 180);                               /* 0x6CC5C */
        for (i = 0; i < 4; ++i) {
            (void)UiControl_BlitActivePad(pEnv->apOptCtl[i], pEnv->pfnCtlBlit,
                                          pEnv->pfnCtlBlitEx, pEnv->pCtlBlitUser); /* 0x6CC6C */
        }
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75); /* 0x6CCA8 */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_OPT_HEAD);
        DrawScreen_DrawCentered(60, szText);                             /* 0x6CD04 */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_OPT_SAVE);
        DrawScreen_DrawCtlLabel(pEnv->apOptCtl[0], szText);              /* 0x6CD60 */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_OPT_LOAD);
        DrawScreen_DrawCtlLabel(pEnv->apOptCtl[1], szText);              /* 0x6CDBC */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_OPT_QUIT);
        DrawScreen_DrawCtlLabel(pEnv->apOptCtl[2], szText);              /* 0x6CE1C */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_OPTIONS);
        DrawScreen_DrawCtlLabel(pEnv->apOptCtl[3], szText);              /* 0x6CE7C */
        /* 0x6CE84: the menu blits the cancel control here, and the shared tail blits it again. */
        (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_CANCEL);
        break;

    default:
        break;                                                           /* 0x6C6B8 BNE loc_6CEB4 */
    }

    /* 0x6CEB4: the shared tail. Whatever string the case left in psz is drawn centred under the
     * cancel control. */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x6CEB8 */
    DrawScreen_DrawCtlLabel(pEnv->pCtlA, szText);                        /* 0x6CEEC */
}

/* ---- the geoscape pulse tick -----------------------------------------------------------------
 *
 * The header block in pufo_geoscape.h carries the seven steps and the per-screen control groups. */

/* 0x95B8C..0x95BCC: each counter steps by one and stops at 255. */
static void Pulse_Saturate(i32 *pn)
{
    if (pn != NULL && *pn < PUFO_PULSE_COOLDOWN_MAX) {
        ++*pn;
    }
}

/* One screen's auto-repeat groups, in the order the original walks them. */
static void Pulse_RunGroups(const GeoscapePulseEnv *pEnv, const GeoscapePulseScreen *pScreen)
{
    for (i32 g = 0; g < pScreen->nGroupCount; ++g) {
        const GeoscapePulseCtlGroup *pGrp = &pScreen->pGroups[g];
        if (pGrp->pbGate != NULL && *pGrp->pbGate == 0) {
            continue;
        }
        if (pGrp->pbNotGate != NULL && *pGrp->pbNotGate != 0) {
            continue;
        }
        for (i32 i = 0; i < pGrp->nCount; ++i) {
            (void)UiControl_HitTest(pGrp->apCtl[i], 0, 0, false, true, pEnv->pHitCtx);
        }
    }
}

void Geoscape_PulseUpdate(const GeoscapePulseEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }

    /* 0x954E8/0x954F0: a failed action stops the tick before anything, cooldowns included. */
    if (pEnv->pnActionFail != NULL && *pEnv->pnActionFail == 1) {
        return;
    }

    if (pEnv->nScreenId == 0) {
        const i32 nCraft = (pEnv->pnCraftCount != NULL) ? *pEnv->pnCraftCount : 0;
        for (i32 c = 0; c < nCraft; ++c) {                                /* 0x9552C..0x955FC */
            Ship *pShip = (pEnv->apCraft != NULL) ? pEnv->apCraft[c] : NULL;
            if (pShip == NULL || pShip->bAtBase != 0) {                  /* 0x9554C */
                continue;
            }
            const i32 nItems = PtrArray_GetCount(pEnv->pStrategList);
            for (i32 k = 0; k < nItems; ++k) {                           /* 0x95560 */
                StrategItem *pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, k);
                if (pItem == NULL) {
                    continue;
                }
                if (pItem->nDisplayId == (i32)pShip->bId && pItem->bActive == 0
                    && pItem->nType == 1) {                              /* 0x955B0 */
                    pItem->bVisible = 1;                                 /* 0x955C0 */
                    pShip->bWeaponsHot = 1;                              /* 0x955C8 */
                }
            }
        }

        /* 0x95618: LABEL_13, which the empty-craft-list exit jumps straight into. */
        if (pEnv->pTime != NULL && pEnv->pTime->bPaused == 0) {
            Geoscape_AdvanceTime(pEnv->pTime, pEnv->nTickSeconds + 1, pEnv->pClockHooks);
            (void)GameTime_Decode(pEnv->pTime);
        }
    }

    if (pEnv->pnAnimPhase != NULL) {
        if (*pEnv->pnAnimPhase > 1) {
            /* 0x95644: an already-advanced phase skips the switch and resets here. */
            *pEnv->pnAnimPhase = 0;                                  /* 0x95B78 */
        } else if (++(*pEnv->pnAnimPhase) > 1) {                         /* 0x95654 */
            const GeoscapePulseScreen *pScreen = NULL;
            for (i32 s = 0; s < pEnv->nScreenCount; ++s) {
                if (pEnv->pScreens[s].nScreenId == pEnv->nScreenId) {
                    pScreen = &pEnv->pScreens[s];
                    break;
                }
            }

            if (pEnv->nScreenId == 0 || pEnv->nScreenId == 2) {
                const i32 nStep = (pEnv->nScreenId == 2) ? PUFO_PULSE_SCROLL_STEP_ZOOM
                                                         : PUFO_PULSE_SCROLL_STEP_MAP;
                if (pEnv->pabDragFlags != NULL && pEnv->pView != NULL
                    && (pEnv->pabDragFlags[PUFO_PULSE_FLAG_UP]
                        | pEnv->pabDragFlags[PUFO_PULSE_FLAG_RIGHT]
                        | pEnv->pabDragFlags[PUFO_PULSE_FLAG_DOWN]
                        | pEnv->pabDragFlags[PUFO_PULSE_FLAG_LEFT]) != 0) {
                    i32 nDx = 0;
                    i32 nDy = 0;
                    if (pEnv->pabDragFlags[PUFO_PULSE_FLAG_UP] != 0) {
                        nDy = nStep;                                     /* 0x95718 */
                    }
                    if (pEnv->pabDragFlags[PUFO_PULSE_FLAG_RIGHT] != 0) {
                        nDx = nStep;                                     /* 0x95724 */
                    }
                    if (pEnv->pabDragFlags[PUFO_PULSE_FLAG_DOWN] != 0) {
                        nDy -= nStep;                                    /* 0x9572C */
                    }
                    if (pEnv->pabDragFlags[PUFO_PULSE_FLAG_LEFT] != 0) {
                        nDx -= nStep;                                    /* 0x95734 */
                    }
                    pEnv->pView->nLastDx = PUFO_PULSE_SCROLL_BASE_X;     /* 0x95714 */
                    pEnv->pView->nLastDy = PUFO_PULSE_SCROLL_BASE_Y;     /* 0x9570C */
                    Smap_ScrollBy(pEnv->pView, nDx + PUFO_PULSE_SCROLL_BASE_X,
                                  nDy + PUFO_PULSE_SCROLL_BASE_Y, pEnv->nScreenId,
                                  pEnv->nStripHeight);                   /* 0x95864 */
                    pEnv->pabDragFlags[PUFO_PULSE_FLAG_UP] = 0;          /* 0x95874..0x95880 */
                    pEnv->pabDragFlags[PUFO_PULSE_FLAG_RIGHT] = 0;
                    pEnv->pabDragFlags[PUFO_PULSE_FLAG_DOWN] = 0;
                    pEnv->pabDragFlags[PUFO_PULSE_FLAG_LEFT] = 0;
                }
            } else if (pScreen != NULL) {
                Pulse_RunGroups(pEnv, pScreen);
            }
            *pEnv->pnAnimPhase = 0;                                  /* 0x95B78 */
        }
    }

    Pulse_Saturate(pEnv->pnCooldownR);                                   /* 0x95B8C */
    Pulse_Saturate(pEnv->pnCooldownL);                                   /* 0x95BA0 */
    Pulse_Saturate(pEnv->pnMsgTimer);                                    /* 0x95BB4 */
    Pulse_Saturate(pEnv->pnPulseTimer);                                  /* 0x95BC8 */
}

/* 0x676AC..0x67780 and 0x67784..0x67854: one readout line -- " " then three
 * itoa(Game_RandU16(900) + 100) joined by '.', drawn at x=5. The original reuses one scratch slot
 * for every itoa and appends with unbounded strcat into a 56-byte stack buffer (psz @ sp+0x20, with
 * the 92-byte scratch right behind it); the port bounds every append with pufo_strcat. */
static void GlobeHover_DrawReadout(i32 nY)
{
    char szText[64];
    char szNum[16];
    i32  i;

    szText[0] = ' ';                                                     /* asc_AADB8 */
    szText[1] = '\0';

    for (i = 0; i < PUFO_GLOBE_READOUT_PARTS; ++i) {
        if (i != 0) {
            (void)pufo_strcat(szText, ".", sizeof(szText));              /* asc_AB084, 0x67700 */
        }
        snprintf(szNum, sizeof(szNum), "%u",
                 (u32)Game_RandU16(PUFO_GLOBE_READOUT_SPAN) + PUFO_GLOBE_READOUT_BASE);
        (void)pufo_strcat(szText, szNum, sizeof(szText));
    }
    Text_DrawString(PufoUi_MapX(PUFO_GLOBE_READOUT_X), PufoUi_MapY(nY), szText);
}

i32 Geoscape_DrawGlobeHover(const GeoscapeGlobeHoverEnv *pEnv)
{
    char szText[200];
    char szNum[16];
    i32  nKey;
    i32  nStripH;
    i32  nStripTop;
    i32  nMidY;
    i32  nScanX;
    i32  nSweepV;
    i32  nSweepH;
    i32  nTile;
    i32  nWidth;
    i32  nLabelY;
    i32  i;

    nStripH   = (i32)pEnv->pStripPad->uHeight;
    nStripTop = 320 - nStripH;
    /* 0x6733C..0x6734C is the halving triple (CMP #0 / ADDMI #1 / ASR #1), which is C's division,
     * so the horizontal rule and the tile sample share one value. */
    nMidY     = nStripTop / 2;

    /* 1. 0x67290..0x672D8: the wash key. Only rolls 8 and 9 survive; everything below collapses to
     * 0, and the key is what lets the globe underneath show through the index-0xFF wash. */
    nKey = (i32)Game_RandU16(PUFO_GLOBE_WASH_KEY_SPAN);
    if (nKey < PUFO_GLOBE_WASH_KEY_MIN) {
        nKey = 0;                                                        /* 0x672B4 MOVLT */
    }
    const i32 nSurfW = (g_pMainSurface != NULL) ? (i32)g_pMainSurface->nWidth  : PUFO_SURF_W;
    const i32 nSurfH = (g_pMainSurface != NULL) ? (i32)g_pMainSurface->nHeight : PUFO_SURF_H;

    SoftSurf_BlitSrcRect(0, 0, nSurfW, nSurfH, PUFO_GLOBE_WASH_IDX, 255, 255, nKey); /* 0x672D8, QoL surface dims */

    /* 2. 0x672DC..0x67450: the crosshair, all in index 0x32 / 0xC8 / 0x32. The QoL shim centers
     * the 240-wide rule set on the wide face (identity on the portrait face). */
    SoftSurf_BlitRectProxy(0, PufoUi_MapX(120), 0, 1, PufoUi_MapY(nStripTop),
                           PUFO_GLOBE_RULE_IDX, PUFO_GLOBE_RULE_MID,
                           PUFO_GLOBE_RULE_LOW);                        /* 0x67314 */
    SoftSurf_BlitRectProxy(0, PufoUi_MapX(0), PufoUi_MapY(nMidY), nSurfW, 1,
                           PUFO_GLOBE_RULE_IDX, PUFO_GLOBE_RULE_MID,
                           PUFO_GLOBE_RULE_LOW);                        /* 0x67350 */
    for (i = 0; i < PUFO_GLOBE_TICK_COUNT; ++i) {
        const i32 nTick = (i + PUFO_GLOBE_TICK_BASE) * (i + PUFO_GLOBE_TICK_BASE); /* 0x6736C */

        SoftSurf_BlitRectProxy(0, PufoUi_MapX(nTick), PufoUi_MapY(nMidY - 1), 1, 3,
                               PUFO_GLOBE_RULE_IDX, PUFO_GLOBE_RULE_MID,
                               PUFO_GLOBE_RULE_LOW);                    /* 0x673A0 */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(240 - nTick), PufoUi_MapY(nMidY - 1), 1, 3,
                               PUFO_GLOBE_RULE_IDX, PUFO_GLOBE_RULE_MID,
                               PUFO_GLOBE_RULE_LOW);                    /* 0x673E0 */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(119), PufoUi_MapY(nTick), 3, 1,
                               PUFO_GLOBE_RULE_IDX, PUFO_GLOBE_RULE_MID,
                               PUFO_GLOBE_RULE_LOW);                    /* 0x67408 */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(119), PufoUi_MapY(nStripTop - nTick), 3, 1,
                               PUFO_GLOBE_RULE_IDX, PUFO_GLOBE_RULE_MID,
                               PUFO_GLOBE_RULE_LOW);                    /* 0x6743C */
    }

    /* 3. 0x67450..0x67494: the bottom panel. While it is still expanding the function stops here
     * and the scan animation, the readouts and the labels never run. */
    if (!Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0), PufoUi_MapY(nStripTop), PUFO_GLOBE_PANEL_WIDTH,
                           nStripH - PUFO_GLOBE_PANEL_INSET, PUFO_GLOBE_PANEL_IDX,
                           PUFO_GLOBE_PANEL_SRC_X, PUFO_GLOBE_PANEL_SRC_Y,
                           PUFO_GLOBE_PANEL_KEY, 3)) {
        return 0;                                                        /* 0x67490 BEQ loc_67B10 */
    }

    /* 4. 0x67498..0x67674: the scan animation. */
    SoftSurf_BlitSrcRect(PufoUi_MapX(160), PufoUi_MapY(200), 75, 30, PUFO_GLOBE_WASH_IDX, 255, 255, 100); /* 0x674BC */
    SoftSurf_BlitSrcRect(PufoUi_MapX(5), PufoUi_MapY(200), 75, 30, PUFO_GLOBE_WASH_IDX, 255, 255, 100);   /* 0x674E0 */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 10, 5 * (51 - nKey), 10, 10, 50, 10); /* 0x67510 */

    nScanX  = *pEnv->pnScanX;
    nSweepV = *pEnv->pnSweepV;
    nSweepH = *pEnv->pnSweepH;

    if (nScanX + PUFO_GLOBE_SCAN_X0 < PUFO_GLOBE_SCAN_SPAN) {           /* 0x6752C */
        SoftSurf_BlitSrcRect(PufoUi_MapX(nScanX + PUFO_GLOBE_SCAN_X0), PufoUi_MapY(PUFO_GLOBE_SCAN_Y),
                             PUFO_GLOBE_SCAN_W, PUFO_GLOBE_SCAN_H, PUFO_GLOBE_SCAN_IDX,
                             PUFO_GLOBE_SCAN_MID, PUFO_GLOBE_SCAN_LOW,
                             PUFO_GLOBE_SCAN_KEY);                       /* 0x67550 */
    }
    if (PUFO_GLOBE_SCAN_SPAN - nScanX > PUFO_GLOBE_SCAN_X0) {           /* 0x67560 */
        SoftSurf_BlitSrcRect(PufoUi_MapX(PUFO_GLOBE_SCAN_SPAN - nScanX), PufoUi_MapY(PUFO_GLOBE_SCAN_Y),
                             PUFO_GLOBE_SCAN_W, PUFO_GLOBE_SCAN_H, PUFO_GLOBE_SCAN_IDX,
                             PUFO_GLOBE_SCAN_MID, PUFO_GLOBE_SCAN_LOW,
                             PUFO_GLOBE_SCAN_KEY);                       /* 0x67584 */
    }
    if (nSweepV < PUFO_GLOBE_SWEEP_V_GATE) {                             /* 0x67594 */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(2 * (nSweepV + PUFO_GLOBE_SWEEP_V_BASE)), PufoUi_MapY(PUFO_GLOBE_SWEEP_Y),
                               1, PUFO_GLOBE_SWEEP_V_H, PUFO_GLOBE_SWEEP_IDX,
                               PUFO_GLOBE_SWEEP_MID, PUFO_GLOBE_SWEEP_LOW); /* 0x675C0 */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(PUFO_GLOBE_SWEEP_V_SPAN - 2 * nSweepV), PufoUi_MapY(PUFO_GLOBE_SWEEP_Y),
                               1, PUFO_GLOBE_SWEEP_V_H, PUFO_GLOBE_SWEEP_IDX,
                               PUFO_GLOBE_SWEEP_MID, PUFO_GLOBE_SWEEP_LOW); /* 0x675EC */
    }
    if (nSweepH < PUFO_GLOBE_SWEEP_H_GATE) {                             /* 0x67600 */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(PUFO_GLOBE_SWEEP_X),
                               PufoUi_MapY(2 * (nSweepH + PUFO_GLOBE_SWEEP_H_BASE)),
                               PUFO_GLOBE_SWEEP_H_W, 1, PUFO_GLOBE_SWEEP_IDX,
                               PUFO_GLOBE_SWEEP_MID, PUFO_GLOBE_SWEEP_LOW); /* 0x6762C */
        SoftSurf_BlitRectProxy(0, PufoUi_MapX(PUFO_GLOBE_SWEEP_X),
                               PufoUi_MapY(2 * (PUFO_GLOBE_SWEEP_H_SPAN - nSweepH)),
                               PUFO_GLOBE_SWEEP_H_W, 1, PUFO_GLOBE_SWEEP_IDX,
                               PUFO_GLOBE_SWEEP_MID, PUFO_GLOBE_SWEEP_LOW); /* 0x67658 */
    }

    /* 0x67664..0x676A8: all three counters advance after the drawing, the scan wraps once it
     * passes 320, and the sweep pair zeroes together -- nGlobeSweepV + 1 > 40 clears both. */
    ++nSweepV;
    ++nSweepH;
    ++nScanX;
    if (nScanX > PUFO_GLOBE_SCAN_WRAP) {                                 /* 0x67684 */
        nScanX = 0;
    }
    if (nSweepV > PUFO_GLOBE_SWEEP_V_WRAP) {                             /* 0x67694 CMP #0x28 */
        nSweepV = 0;
        nSweepH = 0;
    }
    *pEnv->pnScanX  = nScanX;
    *pEnv->pnSweepV = nSweepV;
    *pEnv->pnSweepH = nSweepH;

    /* 5. 0x676AC..0x67854: the two readout lines. Six rolls per frame, none buffered. */
    GlobeHover_DrawReadout(PUFO_GLOBE_READOUT_Y0);
    GlobeHover_DrawReadout(PUFO_GLOBE_READOUT_Y1);

    /* 6. 0x67858..0x67894: the pending pick and the hovered tile. */
    *pEnv->pnPendingPickX = pEnv->pView->nPickX;                         /* 0x6786C */
    *pEnv->pnPendingPickY = pEnv->pView->nPickY;                         /* 0x67874 */
    nTile = (i32)Smap_GetTileAtScreen(pEnv->pView, pEnv->pTiles, PufoUi_MapX(120),
                                   PufoUi_MapY(nMidY)); /* 0x67894 */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 10, 200, 10, 10, 50, 10);    /* 0x678B8 */

    if (nTile == PUFO_REGION_ID_NONE) {
        /* 0x67AC0..0x67AEC: ocean gets the no-build label and nothing else. It is the same TEXTS
         * record the region arm would reach for tile 0x89 -- 905 + 0x89 = 1042 -- which is why the
         * ocean and the unbuildable region read identically. */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_GLOBE_TEXT_OCEAN);
        nWidth  = Text_MeasureWidth(szText);                             /* 0x67AE8 */
        nLabelY = PUFO_GLOBE_LABEL_Y_NAME;                               /* 0x67AEC */
    } else {
        /* 0x678DC..0x67940: "Terrain: " + the region's own name, centred at y=250. */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_GLOBE_TEXT_TERRAIN);
        (void)pufo_strcat(szText,
                          pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE
                              * (PUFO_GLOBE_TEXT_REGION0 + nTile), sizeof(szText)); /* 0x67914 */
        nWidth = Text_MeasureWidth(szText);                              /* 0x67928 */
        Text_DrawString(PufoUi_MapX(120) - nWidth / 2, PufoUi_MapY(PUFO_GLOBE_LABEL_Y_NAME), szText); /* 0x67940 */

        /* 0x67950..0x679D8: "Base cost: " + the region's price + "$", centred at y=270, and only
         * while the placement-pending byte is clear.
         *
         * Deviation: 0x67980 indexes aNewBaseCostByRegion[tile - 0x80] with no range test, so a tile
         * outside 0x80..0x89 reads past the ten-entry table. The shipped tile grid carries only
         * 0x80..0x89 and 0xFF, and 0xFF takes the other arm; the port skips the cost line for an
         * out-of-range tile instead of reading out of bounds. */
        if (nTile >= PUFO_REGION_ID_FIRST
            && nTile < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT) {
            DrawScreen_CopyTextRec(szText, sizeof(szText),
                                   pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_GLOBE_TEXT_COST);
            snprintf(szNum, sizeof(szNum), "%u",
                     pEnv->paRegionCost[nTile - PUFO_REGION_ID_FIRST]); /* 0x67980 */
            (void)pufo_strcat(szText, szNum, sizeof(szText));           /* 0x6798C */
            (void)pufo_strcat(szText, "$", sizeof(szText));             /* 0x67998 asc_AB1D8 */
            if (pEnv->pbPlacementPending == NULL || *pEnv->pbPlacementPending == 0) { /* 0x679A8 */
                nWidth = Text_MeasureWidth(szText);                     /* 0x679BC */
                Text_DrawString(PufoUi_MapX(120) - nWidth / 2, PufoUi_MapY(PUFO_GLOBE_LABEL_Y_COST), szText); /* 0x679D8 */
            }
        }

        /* 0x679F0..0x67A08: the confirm control, centred on 120 and pinned to y=290. */
        if (pEnv->pCtlConfirm != NULL) {
            pEnv->pCtlConfirm->x = (i16)(120 - (pEnv->pCtlConfirm->uWidth >> 1)); /* 0x679F0 */
            pEnv->pCtlConfirm->y = PUFO_GLOBE_LABEL_Y_BUILD;            /* 0x67A00 */
            (void)UiControl_BlitActivePad(pEnv->pCtlConfirm, pEnv->pfnCtlBlit,
                                          pEnv->pfnCtlBlitEx, pEnv->pCtlBlitUser); /* 0x67A08 */
        }

        /* 0x67A34..0x67A6C: the caption over it, drawn by the shared tail below. */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75); /* 0x67A34 */
        DrawScreen_CopyTextRec(szText, sizeof(szText),
                               pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_GLOBE_TEXT_BUILD);
        nWidth  = Text_MeasureWidth(szText);                             /* 0x67A68 */
        nLabelY = PUFO_GLOBE_LABEL_Y_BUILD;                              /* 0x67A6C */
    }

    Text_DrawString(PufoUi_MapX(120) - nWidth / 2, PufoUi_MapY(nLabelY), szText); /* 0x67B08, both arms land here */
    return nTile;                                                        /* 0x67B0C, the caller's bPendingRegionId */
}

/* --- Geoscape_Draw @ 0x8c180 status panel (0x8C760..0x8E658) ------------------------ */

/* The panel's own centring: the main column anchors on 120, the footer's two labels on 28 and
 * 209 (0x8CA8C `RSB R1,R1,#0x1C`, 0x8E3D8 `RSB R1,R1,#0xD1`). */
static void StatusPanel_DrawAt(i32 nCentreX, i32 nY, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz);

    Text_DrawString(PufoUi_MapX(nCentreX) - nWidth / 2, PufoUi_MapY(nY), psz);
}

static void StatusPanel_CopyRec(const GeoscapeStatusPanelEnv *pEnv, char *pszDst, i32 nRec)
{
    /* Deviation: the finished-research row label at 0x8D5DC feeds aTopics[nTopicId].wGate4 straight
     * in, and the shipped data has such indices past the 1912-record table; the port reads only
     * inside it and leaves the label empty. */
    if (pEnv->pTextRecs == NULL || nRec < 0 || nRec >= PUFO_TEXT_REC_CAPACITY) {
        pszDst[0] = '\0';
        return;
    }
    DrawScreen_CopyTextRec(pszDst, PUFO_TEXT_REC_STRIDE,
                           pEnv->pTextRecs + (size_t)PUFO_TEXT_REC_STRIDE * (size_t)nRec);
}

/* 0x44,0xF4,0xFC / 4,0x46,0x4B: the status pair every arm packs at least once. */
static void StatusPanel_PackColour(const GeoscapeStatusPanelEnv *pEnv)
{
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 0x44, 0xF4, 0xFC, 4, 0x46, 0x4B);
}

static void StatusPanel_Anim(const GeoscapeStatusPanelEnv *pEnv, i32 nY, i32 nH)
{
    (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, PufoUi_MapX(0), PufoUi_MapY(nY), PUFO_STATUS_ANIM_W, nH, 1);
}

/* The two bottom controls: x is fixed per control, y per arm, then both are blitted. */
static void StatusPanel_SetControls(const GeoscapeStatusPanelEnv *pEnv, i32 nCtlY)
{
    UiControl *const pSfx = pEnv->pCtlSfx;
    UiControl *const pOpen = pEnv->pCtlOpen;

    pSfx->x = (i16)PUFO_STATUS_CTL_X_SFX;                              /* 0x8C9C8 */
    pSfx->y = (i16)nCtlY;                                              /* 0x8C9D8 */
    pSfx->pfnClick = pEnv->pfnCtlSfx;                                  /* 0x8C9E0 */
    pOpen->x = (i16)PUFO_STATUS_CTL_X_OPEN;                            /* 0x8C9EC */
    pOpen->y = (i16)nCtlY;                                             /* 0x8C9F8 */
    pOpen->pfnClick = pEnv->pfnCtlOpen;                                /* 0x8CA04 */
    (void)UiControl_BlitActivePad(pSfx, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                /* 0x8CA0C */
    (void)UiControl_BlitActivePad(pOpen, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                /* 0x8CA14 */
}

/* The shared footer: TEXTS[1193] "OK" centred on 28, TEXTS[1194] centred on 209, both at the
 * arm's footer y. The colour pack is NOT here -- every arm runs one before this point, and
 * '"', '&', ')' and '%' run an extra one before their main lines. Arms '\'' and '(' copy 1193
 * before that pack, which is not observable. */
static void StatusPanel_DrawFooter(const GeoscapeStatusPanelEnv *pEnv, char *psz, i32 nFootY)
{
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_OK);
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_LEFT, nFootY, psz);
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_OK_RIGHT);
    /* Shipped Russian TEXTS[1194] abbreviates "Slow" as "OKмдл".
     * Use a readable caption for this exact record; keep other languages intact. */
    if (strcmp(psz, "OK\xEC\xE4\xEB") == 0) {
        DrawScreen_CopyTextRec(psz, PUFO_TEXT_REC_STRIDE,
                              "\xCC\xE5\xE4\xEB."); /* "Медл." in CP1251 */
    }
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_RIGHT, nFootY, psz);
}

/* R8 for the whole status panel: the map half-extent the map chrome leaves behind at 0x8C2D0
 * (`SUB R8, R3, #0x78` on nMapW / 2). Three anim frames use it as their height. */
static i32 StatusPanel_MapHalfW(const GeoscapeStatusPanelEnv *pEnv)
{
    return pEnv->pView->nMapW / 2 - 120;
}

/* The list entry behind one colour slot of message line 0. The original re-reads the slot and
 * re-resolves the entry at every use and never null-checks it; the port checks once per call. */
static StrategItem *StatusPanel_ListEntry(const GeoscapeStatusPanelEnv *pEnv, i32 nSlot)
{
    return (StrategItem *)PtrArray_Get(pEnv->pStrategList,
                                       UiLineList_GetColor(pEnv->pLines, 0, nSlot));
}

/* 0x8E658: the action-fail line. Both the closed and the open panel-draw gate reach it --
 * 0x8E5E0's BEQ lands at 0x8E658 -- so it runs for flag 0x1E too. */
static void StatusPanel_DrawActionFail(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pnActionFail == NULL || *pEnv->pnActionFail != 1) {
        return;                                                        /* 0x8E664 */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) >= 0x1E) {
        return;                                                        /* 0x8E67C BGE */
    }
    if (pEnv->pnActionFailReason == NULL || *pEnv->pnActionFailReason != 0
        || pEnv->pView == NULL) {
        return;                                                        /* 0x8E68C BNE */
    }
    /* bEnabled is LDRSB of dwActionFail: 1, which the gate above has just proved. */
    (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, PufoUi_MapX(0x14), PufoUi_MapY(0x82), 0xC8, StatusPanel_MapHalfW(pEnv),
                                      (i8)(u8)*pEnv->pnActionFail);    /* 0x8E6A8 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_NO_MONEY);         /* 0x8E6B0 record 406 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_FAIL_TEXT, psz);
}

/* bases and labels of the size line, by the SECOND entry's bCraftClass (+0x02, 0x8E8DC LDRSB
 * [R0,#2] -- not nType). The five labels come from three
 * different registers (1360 via R10, 1361 via R11, 1362 via the 0x8EB4C pair), which is why the
 * table repeats rather than running. */
static const i32 g_anStatusSizeBase[8] = { 2200, 2400, 2700, 4000, 4300, 4800, 3200, 5000 };
static const i32 g_anStatusSizeRec[8]  = { 1359, 1360, 1360, 1361, 1361, 1362, 1362, 1363 };

/* nMissionSub -> record, in the jumptable's own order: sub 5 takes 1386 and subs 6/7 take
 * 1384/1385, so the mapping is not monotonic. */
static const i32 g_anStatusMissionRec[8] = { 1379, 1380, 1381, 1382, 1383, 1386, 1384, 1385 };

/* The x the original gives every bottom control: centred on 120 from the control's own width.
 * 0x8ECC0 and the six repeats at 0x8F38C, 0x8F528, 0x8F650, 0x8F664, 0x8F8DC and 0x8F8F0. */
static void StatusPanel_CentredCtlX(UiControl *pCtl)
{
    pCtl->x = (i16)(PUFO_STATUS_TEXT_X_CENTRE - (pCtl->uWidth >> 1));
}

/* One of the panel's two bottom controls: centred on 120 at the arm's y, then blitted. */
static void StatusPanel_BlitSelControl(const GeoscapeStatusPanelEnv *pEnv, UiControl *pCtl, i32 nY)
{
    StatusPanel_CentredCtlX(pCtl);                                     /* 0x8ECC0 */
    pCtl->y = (i16)nY;                                                 /* 0x8ECCC / 0x8ECF4 */
    (void)UiControl_BlitActivePad(pCtl, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                /* 0x8ED04 */
}

/* 0x8E708..0x8F24C: the contact the cursor is on. Line 0's colour slot 0 picks the entry; slot 1
 * picks the one whose nType sizes the size line. pszRadarMsg is the original's dword_186B08 + 8
 * (szRadarMsg @ 0x186B10) and pszHyper is unk_186420. */
static void StatusPanel_DrawSelectedContact(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    char szNum[16], szLabel[PUFO_TEXT_REC_STRIDE];

    if (pEnv->pView == NULL || pEnv->pStrategList == NULL || pEnv->pszRadarMsg == NULL
        || pEnv->pszHyper == NULL || pEnv->pCtlA == NULL || pEnv->pCtlConfirm == NULL
        || pEnv->pbMapSelectionFlag == NULL) {
        return;
    }
    StrategItem *const pEntry = StatusPanel_ListEntry(pEnv, 0);
    const bool bHyper = pEntry != NULL && pEntry->bHyperDetect != 0;
    const i32 nCtlYConfirm = bHyper ? PUFO_STATUS_SELCTL_Y_HYP_CONF : PUFO_STATUS_SELCTL_Y_STD_CONF;
    const i32 nCtlYClear   = bHyper ? PUFO_STATUS_SELCTL_Y_HYP_CLEAR : PUFO_STATUS_SELCTL_Y_STD_CLEAR;
    const i32 nLabelY      = bHyper ? PUFO_STATUS_Y_SEL_LABEL_HYP : PUFO_STATUS_Y_SEL_LABEL_STD;
    const i32 nCancelY     = bHyper ? PUFO_STATUS_Y_SEL_CANCEL_HYP : PUFO_STATUS_Y_SEL_CANCEL_STD;

    if (bHyper) {
        (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0), PufoUi_MapY(0x32), 0xEC, 0xB4, 0, 0x28, 0x32, 0xB4, 0);
    } else {                                                          /* 0x8E728 BNE */
        (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(0x46), 0xD0, StatusPanel_MapHalfW(pEnv),
                                0, 0x28, 0x32, 0xB4, 0);
    }
    if (pEntry == NULL) {
        return;
    }

    /* "НЛО" + the display id, centred at 80. j_itoa writes straight into psz in the original. */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_UFO);             /* 0x8E7C8 record 1210 */
    snprintf(szNum, sizeof(szNum), "%d", pEntry->nDisplayId);         /* 0x8E818 */
    (void)pufo_strcat(psz, szNum, PUFO_TEXT_REC_STRIDE);               /* 0x8E824 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_SEL_TITLE, psz);

    /* The size line, built in szRadarMsg: the label, then itoa(abs(base - 61 * bRandA)) is left in
     * psz and only the label is appended here. bCraftClass comes from the SLOT 1 entry, bRandA from
     * slot 0. Outside 0..7 the switch jumps straight to 0x8EBB8, past the strcat: psz keeps the
     * size LABEL and szRadarMsg keeps the label alone. */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_SIZE_LABEL);       /* 0x8E854 record 1211 */
    DrawScreen_CopyTextRec(pEnv->pszRadarMsg, PUFO_TEXT_REC_STRIDE, psz); /* 0x8E87C..0x8E898 */
    const StrategItem *const pKindEntry = StatusPanel_ListEntry(pEnv, 1);
    const i32 nKind = pKindEntry != NULL ? (i32)(i8)pKindEntry->bCraftClass : -1;
    if (nKind >= 0 && nKind < 8) {
        const i32 nRaw = g_anStatusSizeBase[nKind]
                       - PUFO_STATUS_SIZE_BASE_STEP * (i32)(i8)pEntry->bRandA;
        snprintf(psz, PUFO_TEXT_REC_STRIDE, "%d", nRaw < 0 ? -nRaw : nRaw);
        StatusPanel_CopyRec(pEnv, szLabel, g_anStatusSizeRec[nKind]);
        (void)pufo_strcat(pEnv->pszRadarMsg, szLabel, PUFO_TEXT_REC_STRIDE); /* 0x8EBB4 */
    }
    Text_DrawString(PUFO_STATUS_TEXT_X_LEFT_PANEL, PUFO_STATUS_Y_SEL_SIZE,
                    pEnv->pszRadarMsg);                               /* 0x8EBBC..0x8EBC8, x 40 */

    /* The speed line. psz keeps the size number unless nTimer > 0 replaces it with "0", so a
     * contact with no accumulated timer shows its SIZE number as its speed -- recorded as a
     * deviation, faithful to 0x8EBF4's BLE. */
    if (pEntry->nTimer > 0) {
        pufo_strncpy(psz, "0", PUFO_TEXT_REC_STRIDE);                 /* 0x8EBF8 pszValue */
    }
    StatusPanel_CopyRec(pEnv, pEnv->pszRadarMsg, PUFO_STATUS_TEXT_SPEED_LABEL); /* 0x8EC18 record 1364 */
    (void)pufo_strcat(pEnv->pszRadarMsg, psz, PUFO_TEXT_REC_STRIDE);                              /* 0x8EC44 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_KNOTS);            /* 0x8EC48 record 1365 */
    (void)pufo_strcat(pEnv->pszRadarMsg, psz, PUFO_TEXT_REC_STRIDE);                              /* 0x8EC54 */
    Text_DrawString(PUFO_STATUS_TEXT_X_LEFT_PANEL, PUFO_STATUS_Y_SEL_SPEED,
                    pEnv->pszRadarMsg);                                /* 0x8EC58..0x8EC68, x 40 */

    pEnv->pCtlConfirm->pfnClick = pEnv->pfnSelectTarget;              /* 0x8EC84 Smap_OnPointerUp */
    pEnv->pCtlA->pfnClick = pEnv->pfnClearSelection;                  /* 0x8EC90 Smap_ClearSelection */
    if (!bHyper) {
        StatusPanel_BlitSelControl(pEnv, pEnv->pCtlConfirm, nCtlYConfirm);
        StatusPanel_BlitSelControl(pEnv, pEnv->pCtlA, nCtlYClear);    /* 0x8ECB8..0x8ED04 */
    } else {
        /* 0x8EE00: the hyperwave contact's four lines, built in unk_186420. The type line reads the
         * SLOT 0 entry's bCraftClass (0x8EE98 LDRSB [R0,#2]) where the size line above used the
         * SLOT 1 entry's. Race is [R0,#0x70] and mission is [R0,#0x39], both from SLOT 0 too. */
        StatusPanel_CopyRec(pEnv, pEnv->pszHyper, PUFO_STATUS_TEXT_HYPERWAVE); /* 0x8EE00 record 1345 */
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_SEL_HYPER, pEnv->pszHyper);

        StatusPanel_CopyRec(pEnv, pEnv->pszHyper, PUFO_STATUS_TEXT_TYPE_LABEL); /* 0x8EE54 record 1346 */
        const i32 nSelfKind = (i32)(i8)pEntry->bCraftClass;           /* 0x8EE9C CMP #7, 8 cases */
        if (nSelfKind >= 0 && nSelfKind < 8) {
            StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_TYPE_FIRST + nSelfKind);
            (void)pufo_strcat(pEnv->pszHyper, psz, PUFO_TEXT_REC_STRIDE); /* 0x8EF34 */
        }
        Text_DrawString(PUFO_STATUS_TEXT_X_LEFT_PANEL, PUFO_STATUS_Y_SEL_TYPE, pEnv->pszHyper);

        StatusPanel_CopyRec(pEnv, pEnv->pszHyper, PUFO_STATUS_TEXT_RACE_LABEL); /* 0x8EF4C record 1347 */
        const i32 nRace = (i32)(i8)pEntry->bRace;                     /* 0x8EF90, a CMP chain: race 0
                                                                       * has its own BEQ at 0x8EF98 and
                                                                       * still appends (record 1374);
                                                                       * anything above 4 falls out at
                                                                       * 0x8EFB8 and appends nothing */
        if (nRace >= 0 && nRace <= 4) {
            StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_RACE_FIRST + nRace);
            (void)pufo_strcat(pEnv->pszHyper, psz, PUFO_TEXT_REC_STRIDE); /* 0x8F000 */
        }
        Text_DrawString(PUFO_STATUS_TEXT_X_LEFT_PANEL, PUFO_STATUS_Y_SEL_RACE, pEnv->pszHyper);

        StatusPanel_CopyRec(pEnv, pEnv->pszHyper, PUFO_STATUS_TEXT_MISSION_LABEL); /* 0x8F018 record 1348 */
        const i32 nMission = (i32)(i8)pEntry->nMissionSub;            /* 0x8F05C LDRSB [R0,#0x39] */
        if (nMission >= 0 && nMission < 8) {
            StatusPanel_CopyRec(pEnv, psz, g_anStatusMissionRec[nMission]);
            (void)pufo_strcat(pEnv->pszHyper, psz, PUFO_TEXT_REC_STRIDE); /* 0x8F0F4 */
        }
        Text_DrawString(PUFO_STATUS_TEXT_X_LEFT_PANEL, PUFO_STATUS_Y_SEL_MISSION, pEnv->pszHyper);

        /* The controls come after the lines on this path (0x8F10C..0x8F160). */
        StatusPanel_BlitSelControl(pEnv, pEnv->pCtlConfirm, nCtlYConfirm);
        StatusPanel_BlitSelControl(pEnv, pEnv->pCtlA, nCtlYClear);
    }

    /* 0x8ED08..0x8EDF8 and 0x8F164..0x8F24C: the same two labels on both paths, only lower. */
    StatusPanel_CopyRec(pEnv, psz, *pEnv->pbMapSelectionFlag != 0 ? PUFO_STATUS_TEXT_ATTACK
                                                                  : PUFO_STATUS_TEXT_CENTRE);
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, nLabelY, psz);      /* 0x8EDAC / 0x8F1F4 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);          /* 0x8EDBC / 0x8F1F8, record 1144 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, nCancelY, psz);     /* 0x8F234, the shared tail */
}

/* 0x8F270: flag 0x11, the base the message names is gone. The line-0 colour value IS the base
 * record index (0x8F2E8 `ADD R3, R1, R0,LSL#6` on aSiteRecs @ 0x186C40), unbounded as everywhere
 * else in the binary. */
static void StatusPanel_DrawBaseLost(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->apSiteRecs == NULL || pEnv->pCtlA == NULL || pEnv->pfnResolveInterceptUI == NULL) {
        return;
    }
    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(0x3C), 0xD0, PUFO_STATUS_ORDER_NH_SHORT,
                            0, 0x28, 0x32, 0xB4, 0);                      /* 0x8F278..0x8F2B0 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_BASE_LABEL);         /* 0x8F2B4 record 1214 */
    (void)pufo_strcat(psz, pEnv->apSiteRecs[(i32)UiLineList_GetColor(pEnv->pLines, 0, 0)].abName,
                      PUFO_TEXT_REC_STRIDE);                             /* 0x8F2E4..0x8F2F8 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_LINE1, psz); /* 0x8F324 y 70 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_BASE_LOST);          /* 0x8F328 record 1215 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_LINE2, psz); /* 0x8F37C y 90 */
    pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;                 /* 0x8F39C / 0x8F3A8 */
    StatusPanel_BlitSelControl(pEnv, pEnv->pCtlA, PUFO_STATUS_ORDERCTL_Y_ONE); /* 0x8F380..0x8F3B0 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);           /* 0x8F3B8 aTextOk, 1127 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_OK_ONE, psz); /* 0x8F400 y 116 */
}

/* The ship-name line the 0x0F and 0x0E arms share. Ship_FormatDisplayName returns szRadarMsg @
 * 0x186B10, so calling it also leaves the craft name in that buffer -- the same 108 bytes the
 * original has before nSmapTapX @ 0x186B7C. */
static void StatusPanel_AppendShipName(const GeoscapeStatusPanelEnv *pEnv, char *psz, i32 nShipIdx)
{
    (void)Ship_FormatDisplayName(pEnv->apCraft, nShipIdx, (const u8 *)pEnv->pTextRecs,
                                 pEnv->pszRadarMsg, PUFO_STATUS_RADAR_MSG_BYTES);
    (void)pufo_strcat(psz, pEnv->pszRadarMsg, PUFO_TEXT_REC_STRIDE);     /* 0x8F49C / 0x8F7F4 */
}

/* 0x8F41C: flag 0x0F, a craft is short of range and heading home. The craft comes from the global
 * selected index at 0x187054, NOT from the colour slot. */
static void StatusPanel_DrawFuelLow(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->apCraft == NULL || pEnv->pszRadarMsg == NULL || pEnv->pnSelectedShipIdx == NULL
        || pEnv->pCtlA == NULL || pEnv->pfnDismissSelected == NULL) {
        return;
    }
    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(0x3C), 0xD0, PUFO_STATUS_ORDER_NH_SHORT,
                            0, 0x28, 0x32, 0xB4, 0);                      /* 0x8F428..0x8F45C */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_SHIP_LABEL);         /* 0x8F460 record 1216 */
    StatusPanel_AppendShipName(pEnv, psz, *pEnv->pnSelectedShipIdx);     /* 0x8F484..0x8F49C */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_LINE1, psz); /* 0x8F4C8 y 70 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_FUEL_LOW);           /* 0x8F4CC record 1217 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_LINE2, psz); /* 0x8F51C y 90 */
    pEnv->pCtlA->pfnClick = pEnv->pfnDismissSelected;                    /* 0x8F53C / 0x8F548 */
    StatusPanel_BlitSelControl(pEnv, pEnv->pCtlA, PUFO_STATUS_ORDERCTL_Y_ONE); /* 0x8F520..0x8F550 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);           /* 0x8F554 aTextOk */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_OK_ONE, psz); /* 0x8F5A0 y 116 */
}

/* 0x8F5B4: flag 0x0C, "fly to the chosen point?" -- the only one of the four with two controls. */
static void StatusPanel_DrawFlyToPoint(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pCtlA == NULL || pEnv->pCtlB == NULL || pEnv->pfnConfirmCraftTarget == NULL
        || pEnv->pfnResolveInterceptUI == NULL) {
        return;
    }
    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(0x3C), 0xD0, PUFO_STATUS_ORDER_NH_SHORT,
                            0, 0x28, 0x32, 0xB4, 0);                      /* 0x8F5BC..0x8F5F0 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_FLY_TO_POINT);       /* 0x8F5F4 record 1218 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_LINE1, psz); /* 0x8F644 y 70 */
    pEnv->pCtlA->pfnClick = pEnv->pfnConfirmCraftTarget;                 /* 0x8F688 / 0x8F694 */
    pEnv->pCtlB->pfnClick = pEnv->pfnResolveInterceptUI;                 /* 0x8F69C / 0x8F6A0 */
    StatusPanel_CentredCtlX(pEnv->pCtlA);                                /* 0x8F650..0x8F660 */
    StatusPanel_CentredCtlX(pEnv->pCtlB);                                /* 0x8F664..0x8F674 */
    pEnv->pCtlA->y = (i16)PUFO_STATUS_ORDERCTL_Y_TOP;                    /* 0x8F678/0x8F680: 100 */
    pEnv->pCtlB->y = (i16)PUFO_STATUS_ORDERCTL_Y_BOTTOM;                 /* 0x8F68C: 124, R6 */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x8F6A8 */
    (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x8F6B0 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);           /* 0x8F6B4 aTextOk */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_OK_TOP, psz); /* 0x8F700 y 101 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);             /* 0x8F704 record 1144 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_CANCEL, psz); /* 0x8F758 y 125 */
}

/* 0x8F768: flag 0x0E, the point was reached and a new patrol is offered. Here the line-0 colour
 * value IS the ship index (0x8F7E0), where the 0x0F arm read 0x187054; and the two controls are
 * blitted before their handlers are installed (0x8F91C/0x8F924, then 0x8F934/0x8F940). */
static void StatusPanel_DrawPointReached(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->apCraft == NULL || pEnv->pszRadarMsg == NULL || pEnv->pCtlA == NULL
        || pEnv->pCtlConfirm == NULL || pEnv->pfnConfirmSelectedAlt == NULL
        || pEnv->pfnSetWaypoint == NULL) {
        return;
    }
    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(0x3C), 0xD0, PUFO_STATUS_ORDER_NH_TALL,
                            0, 0x28, 0x32, 0xB4, 0);                      /* 0x8F770..0x8F7A8 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_SHIP_LABEL);         /* 0x8F7AC record 1216 */
    StatusPanel_AppendShipName(pEnv, psz, (i32)UiLineList_GetColor(pEnv->pLines, 0, 0)); /* 0x8F7DC */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_LINE1, psz); /* 0x8F820 y 70 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_POINT_REACHED);      /* 0x8F824 record 1219 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_0E_LINE2, psz); /* 0x8F878 y 86 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_NEW_PATROL);         /* 0x8F87C record 1220 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_0E_LINE3, psz); /* 0x8F8D0 y 102 */
    StatusPanel_CentredCtlX(pEnv->pCtlA);                                /* 0x8F8DC..0x8F8EC */
    StatusPanel_CentredCtlX(pEnv->pCtlConfirm);                          /* 0x8F8F0..0x8F900 */
    pEnv->pCtlA->y = (i16)PUFO_STATUS_ORDERCTL_Y_BOTTOM;                 /* 0x8F90C: 124, R6 */
    pEnv->pCtlConfirm->y = (i16)PUFO_STATUS_ORDERCTL_Y_0E_CONF;          /* 0x8F914: 150 */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x8F91C */
    (void)UiControl_BlitActivePad(pEnv->pCtlConfirm, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x8F924 */
    pEnv->pCtlA->pfnClick = pEnv->pfnConfirmSelectedAlt;                 /* 0x8F930 / 0x8F934 */
    pEnv->pCtlConfirm->pfnClick = pEnv->pfnSetWaypoint;                  /* 0x8F938 / 0x8F940 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);           /* 0x8F944 aTextOk */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_CANCEL, psz); /* 0x8F98C y 125 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_RETARGET);           /* 0x8F990 record 1221 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_ORDER_RETARGET, psz); /* 0x8F9E0 y 150 */
}

/* The two halves the card applies to a shared weapon pool. Both add 1 first when the count is
 * negative (0x8FF0C/0x8FF28 MOVS + ADDMI), which only the -1 "N.A." sentinel could reach and that one
 * takes the other exit, so in practice these are the round-up and round-down halves. */
static i32 StatusPanel_HalveAmmo(i32 nAmmo, bool bRoundUp)
{
    const i32 nAdj = nAmmo < 0 ? nAmmo + 1 : nAmmo;
    return bRoundUp ? (nAdj >> 1) + 1 : (nAdj >> 1);
}

/* The count byte the card reads for one of the six weapon ids it knows; id 28 has none and shows
 * "N.A." (0x8FDF8 MOV R11, #-1). */
static i32 StatusPanel_WeaponAmmo(const Ship *pShip, i32 nWeapon)
{
    switch (nWeapon) {
        case 0x19: return (i32)pShip->bAmmo25;        /* 0x8FE4C LDRB [R7,#0x3D] */
        case 0x1A: return (i32)pShip->bAmmo26;        /* 0x8FE30 LDRB [R7,#0x3E] */
        case 0x1B: return (i32)pShip->bAmmo27;        /* 0x8FE14 LDRB [R7,#0x3F] */
        case PUFO_STATUS_WEAPON_ID_NO_AMMO: return -1;                /* 0x8FDF8 */
        case 0x1D: return (i32)pShip->bAmmo29;        /* 0x8FDDC LDRB [R7,#0x40] */
        default:   return (i32)pShip->bAmmo30;        /* 0x8FDC0 LDRB [R7,#0x41] */
    }
}

/* 0x8F9E4: flag 0x0B, the selected craft's detail card. This is the one arm of the chain that WRITES
 * 0x187054: 0x8FA90 stores line 0's colour slot 0 there and every read after it uses the global. */
static void StatusPanel_DrawCraftDetail(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    char szNum[16], szName[PUFO_TEXT_REC_STRIDE];

    if (pEnv->pStripPad == NULL || pEnv->apCraft == NULL || pEnv->apSiteRecs == NULL
        || pEnv->pszRadarMsg == NULL || pEnv->pnSelectedShipIdx == NULL || pEnv->pbInterceptDone == NULL
        || pEnv->pCtlReturnBase == NULL || pEnv->pCtlNewTarget == NULL || pEnv->pCtlPatrol == NULL
        || pEnv->pCtlB == NULL) {
        return;
    }

    const PadImage *const pStrip = pEnv->pStripPad;                      /* 0x8F9FC..0x8FA2C */
    SoftSurf_BlitSrcRect(pStrip->nOriginX, pStrip->nOriginY, pStrip->uWidth, pStrip->uHeight,
                         0x3C, 0x78, 0x82, 0x96);                        /* 0x8FA30 */
    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(5), 0xD0, PUFO_STATUS_DETAIL_NH,
                            0, 0x28, 0x32, 0xB4, 0);                     /* 0x8FA34..0x8FA70 */

    *pEnv->pnSelectedShipIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x8FA80 / 0x8FA90 */
    const i32 nShipIdx = *pEnv->pnSelectedShipIdx;
    Ship *const pShip = nShipIdx < 0 ? NULL : pEnv->apCraft[nShipIdx];
    if (pShip == NULL) {
        return;
    }

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_SHIP_LABEL);         /* 0x8FA94 record 1216 */
    StatusPanel_AppendShipName(pEnv, psz, nShipIdx);                     /* 0x8FAC0..0x8FACC */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_DETAIL_SHIP, psz); /* 0x8FAF8 y 15 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_BASE_LABEL);         /* 0x8FAFC record 1214 */
    (void)pufo_strcat(psz, pEnv->apSiteRecs[(i32)pShip->bHomeBase].abName,
                      PUFO_TEXT_REC_STRIDE);                             /* 0x8FB40..0x8FB54, home base */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_DETAIL_BASE, psz); /* 0x8FB80 y 30 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_SPEED_VALUE);        /* 0x8FB84 record 1222 */
    snprintf(szNum, sizeof(szNum), "%d", (i32)pShip->uFuelCap);          /* 0x8FBC4 LDRH [R3,#0x10] */
    (void)pufo_strcat(psz, szNum, PUFO_TEXT_REC_STRIDE);
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_DETAIL_SPEED, psz); /* 0x8FC00 y 45 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_FUEL_VALUE);         /* 0x8FC04 record 1223 */
    const i32 nPct = pShip->uRangeMax != 0u
                   ? (100 * (i32)pShip->uRangeRemain) / (i32)pShip->uRangeMax /* 0x8FC50..0x8FC60, no
                                                                               * zero test in the
                                                                               * original */
                   : 0;
    snprintf(szNum, sizeof(szNum), "%d", nPct);                          /* 0x8FC6C */
    (void)pufo_strcat(psz, szNum, PUFO_TEXT_REC_STRIDE);
    (void)pufo_strcat(psz, "%", PUFO_TEXT_REC_STRIDE);                   /* 0x8FC7C asc_ABAB8 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_DETAIL_FUEL, psz); /* 0x8FCB0 y 60 */

    /* 0x8FA54 MOV R8, #0 ... 0x8FFA4 BLT loc_8FCBC: the two weapon slots, left then right. */
    for (i32 nSlot = 0; nSlot < 2; ++nSlot) {
        const i32 nWeapon = nSlot == 0 ? (i32)pShip->bWeaponL : (i32)pShip->bWeaponR; /* 0x8FCFC /
                                                                                        * 0x8FD7C */
        StatusPanel_CopyRec(pEnv, psz, nSlot == 0 ? PUFO_STATUS_TEXT_WEAPON1
                                                   : PUFO_STATUS_TEXT_WEAPON2);        /* 0x8FCD0 /
                                                                                         * 0x8FD50 */
        i32 nAmmo = 0;                                                    /* 0x8FCBC MOV R11, #0 */
        if (nWeapon >= PUFO_STATUS_WEAPON_ID_FIRST && nWeapon <= PUFO_STATUS_WEAPON_ID_LAST) {
            StatusPanel_CopyRec(pEnv, szName, PUFO_STATUS_TEXT_WEAPON_NAME_FIRST
                                  + (nWeapon - PUFO_STATUS_WEAPON_ID_FIRST));           /* 1350..1355 */
            (void)pufo_strcat(psz, szName, PUFO_TEXT_REC_STRIDE);
            nAmmo = StatusPanel_WeaponAmmo(pShip, nWeapon);
        } else if (nWeapon == 0) {
            continue;                                                     /* 0x8FE50 BEQ loc_8FF9C */
        }
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE,
                           PUFO_STATUS_DETAIL_Y_WEAPON0 + PUFO_STATUS_DETAIL_ROW_STEP * nSlot, psz);
                                                                           /* 0x8FE8C, y 75 / 105 */
        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_AMMO);            /* 0x8FE90 record 1226 */
        if (nAmmo != -1) {                                                /* 0x8FEBE BEQ loc_8FF3C */
            bool bRoundUp = false;
            if ((i32)pShip->bWeaponL == (i32)pShip->bWeaponR) {           /* 0x8FED8 CMP R3, R2 */
                bRoundUp = nSlot == 0 && nAmmo / 2 == 1;                 /* 0x8FEE4, 0x8FF04 */
                if (!bRoundUp) {
                    nAmmo = StatusPanel_HalveAmmo(nAmmo, false);          /* 0x8FF28 */
                } else {
                    nAmmo = StatusPanel_HalveAmmo(nAmmo, true);           /* 0x8FF0C */
                }
            }
        }
        if (nAmmo == -1) {                                                /* 0x8FF34 CMP R11, R7 */
            (void)pufo_strcat(psz, "N.A.", PUFO_TEXT_REC_STRIDE);         /* 0x8FF3C aNA */
        } else {
            snprintf(szNum, sizeof(szNum), "%d", nAmmo);                  /* 0x8FF54 */
            (void)pufo_strcat(psz, szNum, PUFO_TEXT_REC_STRIDE);
        }
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE,
                           PUFO_STATUS_DETAIL_Y_AMMO0 + PUFO_STATUS_DETAIL_ROW_STEP * nSlot, psz);
                                                                           /* 0x8FF90, y 90 / 120 */
    }

    /* 0x8FFB8: the craft's "contact message already posted" byte decides the four handlers. */
    const bool bContact = pShip->bContactMsg != 0;
    if (bContact) {
        pEnv->pCtlReturnBase->pfnClick = pEnv->pfnResolveInterceptUI;     /* 0x8FFC8 / 0x8FFD0 */
        pEnv->pCtlNewTarget->pfnClick  = pEnv->pfnResolveInterceptUI;     /* 0x8FFD4 / 0x8FFD8 */
        pEnv->pCtlPatrol->pfnClick     = pEnv->pfnResolveInterceptUI;     /* 0x8FFF8, unconditional STR */
    } else {
        pEnv->pCtlReturnBase->pfnClick = pEnv->pfnDismissSelected;        /* 0x8FFDC / 0x8FFE0 */
        pEnv->pCtlNewTarget->pfnClick  = pEnv->pfnSelectMapTarget;        /* 0x8FFE8 / 0x8FFEC, which
                                                                           * overwrites the Dismiss
                                                                           * store two instructions
                                                                           * earlier */
        pEnv->pCtlPatrol->pfnClick     = pEnv->pfnConfirmSelected;        /* 0x8FFF4 / 0x8FFF8 */
    }
    pEnv->pCtlB->pfnClick = pEnv->pfnResolveInterceptUI;                  /* 0x8FFFC..0x90008 */

    UiControl *const apCtrls[4] = { pEnv->pCtlReturnBase, pEnv->pCtlNewTarget,
                                    pEnv->pCtlPatrol, pEnv->pCtlB };
    static const i16 anCtrlY[4] = { PUFO_STATUS_DETAILCTL_Y_RETURN, PUFO_STATUS_DETAILCTL_Y_TARGET,
                                    PUFO_STATUS_DETAILCTL_Y_PATROL, PUFO_STATUS_DETAILCTL_Y_CANCEL };
    for (i32 i = 0; i < 4; ++i) {
        StatusPanel_CentredCtlX(apCtrls[i]);                              /* 0x9000C..0x90058 */
        apCtrls[i]->y = anCtrlY[i];                                       /* 0x9005C..0x90088 */
        (void)UiControl_BlitActivePad(apCtrls[i], pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);                /* 0x9008C..0x900A4 */
    }
    if (*pEnv->pbInterceptDone == 0) {                                    /* 0x900A8..0x900B0 */
        (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);                /* 0x900B4: the second blit */
    }

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_RETURN_BASE);         /* 0x900B8 record 1227 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_DETAILCTL_Y_RETURN, psz);
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_NEW_TARGET);          /* 0x9010C record 1228 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_DETAILCTL_Y_TARGET, psz);
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_PATROL);              /* 0x90164 record 1229 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_DETAILCTL_Y_PATROL, psz);
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL_ORDER);        /* 0x901BC record 1230 */
    if (*pEnv->pbInterceptDone == 0) {                                    /* 0x901EC BNE loc_90220 */
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_DETAIL_CANCEL_LBL, psz);
    }
}

/* 0x90220: flag 0x0D, the trace-lost panel. It is the only arm of this chain that touches the clock:
 * 0x90234 loads 0x159618 and 0x90238 calls GameTime_Pause @ 0x5d6ac, which stores 1 into 0x15961C. */
static void StatusPanel_DrawInterceptPause(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    char szNum[16], szName[PUFO_TEXT_REC_STRIDE];

    if (pEnv->pTime == NULL || pEnv->pStripPad == NULL || pEnv->pCtlB == NULL
        || pEnv->pStrategList == NULL || pEnv->pszRadarMsg == NULL) {
        return;
    }

    pEnv->pTime->bPaused = 1;                                          /* 0x90234..0x90238 */

    const PadImage *const pStrip = pEnv->pStripPad;                    /* 0x9023C..0x90274: the same
                                                                        * pad reads and the same four
                                                                        * constants as the 0x0B card */
    SoftSurf_BlitSrcRect(pStrip->nOriginX, pStrip->nOriginY, pStrip->uWidth, pStrip->uHeight,
                         0x3C, 0x78, 0x82, 0x96);
    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(PUFO_STATUS_PAUSE_ANIM_Y), 0xD0, PUFO_STATUS_PAUSE_NH,
                            0, 0x28, 0x32, 0xB4, 0);                   /* 0x90278..0x902B0, box
                                                                        * x 14..225, y 70..195 */

    const i32 nSlot0 = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0);   /* 0x902C0 */
    if (nSlot0 == PUFO_STATUS_COLOUR_NO_TARGET) {                       /* 0x902C8 CMP R0, #0xFF */
        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_TARGET_LOST);   /* 0x902D0 record 1231 */
    } else {
        const StrategItem *const pEntry = StatusPanel_ListEntry(pEnv, 0);
        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_UFO);           /* 0x9031C record 1210 */
        if (pEntry != NULL) {                                           /* the original null-checks
                                                                         * nothing */
            snprintf(szNum, sizeof(szNum), "%d", pEntry->nDisplayId);   /* 0x90368 LDR [R0,#0x10] */
            (void)pufo_strcat(psz, szNum, PUFO_TEXT_REC_STRIDE);
        }
        StatusPanel_CopyRec(pEnv, szName, PUFO_STATUS_TEXT_TRACE_LOST); /* 0x90380 record 1356, with its
                                                                         * own leading space */
        (void)pufo_strcat(psz, szName, PUFO_TEXT_REC_STRIDE);
    }
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_PAUSE_Y_LINE1, psz); /* 0x90390 y 90 */

    /* 0x903F4: the craft here is colour SLOT 1, not the 0x187054 global the 0x0F arm used. */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_SHIP_LABEL);        /* 0x903BC record 1216 */
    StatusPanel_AppendShipName(pEnv, psz, (i32)UiLineList_GetColor(pEnv->pLines, 0, 1));
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_PAUSE_Y_LINE2, psz); /* 0x90410 y 118 */

    StatusPanel_CentredCtlX(pEnv->pCtlB);                               /* 0x90440..0x90450 */
    pEnv->pCtlB->y = (i16)PUFO_STATUS_PAUSECTL_Y;                      /* 0x90454: 155 */
    (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                 /* 0x90464, BEFORE the handler */
    pEnv->pCtlB->pfnClick = pEnv->pfnResolveInterceptUI;               /* 0x90468 / 0x9047C */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);           /* 0x90478, record 1144 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_PAUSE_Y_LABEL, psz); /* 0x904A0 y 156 */
}

/* 0x904D8: flag 0x10, the attacked-city panel. The only arm of the three big panels that does not
 * re-blit the strip pad: the body anim comes first (0x9050C..0x90538). Both controls are blitted
 * before their handler is installed, and both labels come after both controls. */
static void StatusPanel_DrawCityAttacked(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pCtlConfirm == NULL || pEnv->pCtlA == NULL || pEnv->paMissionSites == NULL) {
        return;
    }

    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(PUFO_STATUS_CITY_ANIM_Y), 0xD0, PUFO_STATUS_CITY_NH,
                            0, 0x28, 0x32, 0xB4, 0);                   /* 0x9050C..0x90538, box
                                                                        * x 14..225, y 70..195 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CITY);             /* 0x90540, record 1232 */
    const i32 nSite = (i32)UiLineList_GetColor(pEnv->pLines, 0, 1);    /* 0x9056C: colour slot 1 */
    if (nSite >= 0 && nSite < PUFO_MISSION_SITE_COUNT) {               /* the original bounds-checks
                                                                        * nothing (bugs.tsv) */
        (void)pufo_strcat(psz, pEnv->paMissionSites[nSite].szName, PUFO_TEXT_REC_STRIDE);
    }                                                                  /* 0x90574 MUL by 0x18, 0x9057C
                                                                        * base 0x1864F8 + 0xC8, the name
                                                                        * at +4 (0x90584) */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_CITY_Y_LINE1, psz); /* 0x905B8, y 86 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CITY_ATTACKED);    /* 0x905BC, record 1233, whose
                                                                        * shipped text ends in a space */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_CITY_Y_LINE2, psz); /* 0x9060C, y 104 */

    StatusPanel_CentredCtlX(pEnv->pCtlConfirm);                        /* 0x90614..0x90624 */
    pEnv->pCtlConfirm->y = (i16)PUFO_STATUS_CITYCTL_Y_CONFIRM;         /* 0x9062C: 136 */
    (void)UiControl_BlitActivePad(pEnv->pCtlConfirm, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                /* 0x90638, BEFORE the handler */
    pEnv->pCtlConfirm->pfnClick = pEnv->pfnSelectTarget;              /* 0x90640 / 0x90648, Smap_OnPointerUp */

    StatusPanel_CentredCtlX(pEnv->pCtlA);                              /* 0x9064C..0x9065C */
    pEnv->pCtlA->y = (i16)PUFO_STATUS_CITYCTL_Y_ACTION;                /* 0x90664: 160 */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                /* 0x90670, BEFORE the handler */
    pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;              /* 0x90674 / 0x9067C */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_GO_TO_ZONE);       /* 0x90680, record 1234 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_CITY_Y_CONFIRM_LBL, psz);
                                                                       /* 0x906D4, y 136 -- the same row
                                                                        * as its control */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);           /* 0x906D8, R9 = 1144 * 205 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_CITY_Y_ACTION_LBL, psz);
                                                                       /* 0x90724, y 161 */
}

/* 0x90728: flag 0x1B, the intercept list. The only arm of the chain whose every exit ends the WHOLE
 * default arm: 0x90798, 0x907A8, 0x907B8, 0x90904 and 0x9097C all branch to the jumptable's default
 * label, whose body is 0x944B4 (BaseAtScreen_Draw, the list-mode dialogs, GX_Present, the epilogue) --
 * never to the next flag test and never to the shared tail at 0x91CEC. So the caller returns after it. */
static void StatusPanel_DrawInterceptList(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pStripPad == NULL || pEnv->apRootCtl == NULL || pEnv->apClockDigitPads == NULL
        || pEnv->pbUfoCrashPanel == NULL || pEnv->pbSitePanel == NULL
        || pEnv->pbAlienBasePanel == NULL || pEnv->pCtlB == NULL || pEnv->pCtlConfirm == NULL
        || pEnv->pFinalMissionCtl == NULL || pEnv->pbFinalMissionCtl == NULL
        || pEnv->pTime == NULL) {
        return;
    }

    *pEnv->pnSelectedShipIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x90748 / 0x90760 */

    const PadImage *const pStrip = pEnv->pStripPad;                        /* 0x9076C..0x90788 */
    SoftSurf_BlitSrcRect(pStrip->nOriginX, pStrip->nOriginY, pStrip->uWidth, pStrip->uHeight,
                         0x3C, 0x78, 0x82, 0x96);                          /* nKey built at 0x9074C */

    /* 0x9078C..0x907B8: three gates, all AFTER the strip rect is already painted. */
    if (*pEnv->pbUfoCrashPanel != 0 || *pEnv->pbSitePanel != 0
        || *pEnv->pbAlienBasePanel != 0) {
        return;
    }

    PadImage_Blit(pStrip);                                                 /* 0x907BC / 0x907C0 */

    /* 0x907C4 / 0x907C8. No GameTime_Decode here: the original decodes in Geoscape_AdvanceTime,
     * Geoscape_PulseUpdate, LoadGame and Geoscape_InitUI, and the only draw-time decode is
     * 0x6C35C in Geoscape_DrawScreen. */
    const GeoscapeClockDrawEnv clockEnv = { pEnv->apClockDigitPads, pEnv->pTextRecs,
                                            pEnv->pTime, pEnv->pColor };
    Geoscape_DrawClockPads(&clockEnv);

    /* 0x907D0..0x907F4: the root block's own thirteen widgets. The original keeps the loop counter in
     * dword_186B08; the port keeps it local, like every other dword_186B08 user. */
    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        (void)UiControl_BlitActivePad(pEnv->apRootCtl[i], pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);
    }

    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(PUFO_STATUS_LIST_ANIM_Y), 0xD0, PUFO_STATUS_LIST_NH,
                            0, 0x1E, 0x28, 0xFF, 0);                       /* 0x907F8..0x90828, box
                                                                            * x 14..225, y 260..305:
                                                                            * over the clock it drew */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CHOOSE_TARGET);        /* 0x9082C, record 1235 */
    Text_DrawString(PUFO_STATUS_LIST_X_LABEL, PUFO_STATUS_LIST_Y_LINE, psz);
                                                                           /* 0x90858..0x9086C: no
                                                                            * Text_MeasureWidth here at
                                                                            * all -- x is fixed at 0x28 */

    pEnv->pCtlB->x = (i16)PUFO_STATUS_LISTCTL_X;                           /* 0x90874 / 0x9087C: 140,
                                                                            * the un-centred control */
    pEnv->pCtlB->y = (i16)PUFO_STATUS_LISTCTL_Y;                           /* 0x90888: 274 */
    (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                    /* 0x90894, BEFORE the handler */
    pEnv->pCtlB->pfnClick = pEnv->pfnResolveInterceptUI;                  /* 0x90898 / 0x908A4 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);              /* 0x9089C / 0x908AC, R9 = 1144 * 205 */
    StatusPanel_DrawAt(PUFO_STATUS_LIST_CANCEL_ANCHOR, PUFO_STATUS_LIST_Y_LABEL, psz);
                                                                           /* 0x908EC centred on 173 */

    if (*pEnv->pbFinalMissionCtl == 0) {                                   /* 0x908F8..0x90904 */
        return;
    }
    (void)UiControl_BlitActivePad(pEnv->pFinalMissionCtl, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                    /* 0x9090C / 0x90910 */
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_FINAL_MISSION);        /* 0x90914, record 1236 */
    const i32 nWidth = Text_MeasureWidth(psz);                             /* 0x90948 */
    Text_DrawString((i32)(pEnv->pCtlConfirm->uWidth >> 1) - (nWidth >> 1) + 1,
                    PUFO_STATUS_FINAL_MISSION_Y, psz);                     /* 0x90964..0x90970: the
                                                                            * control's x is never read,
                                                                            * and y is 1 */
}                                                                        /* 0x9097C B def_8C1B0 */

/* 0x90A30..0x90A4C, 0x90BF8..0x90C14, 0x911F4..0x91210 and 0x91320..0x9133C: the same read four times
 * over -- colour SLOT 0 of message line 0 indexes the status panel's own row buffer, apLines at panel +
 * 0xBE4 (R10 = 0xBE0|0x4, built at 0x909AC/0x909B4). Unbounded in the original: this panel has 20 rows,
 * the slot is a byte and apLines[20..499] is NULL, so 20..255 hands a NULL pointer to the strcat or
 * strcpy. The port skips a row it cannot read (recorded in bugs.tsv). */
static const char *StatusPanel_ColourRowText(const GeoscapeStatusPanelEnv *pEnv)
{
    const UiPanel *const pPanel = pEnv->pStatusPanel;
    const i32 nRow = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0);

    if (pPanel == NULL || nRow < 0 || nRow >= PUFO_PANEL_LINES || pPanel->apLines[nRow] == NULL) {
        return NULL;
    }
    return (const char *)pPanel->apLines[nRow];
}

/* 0x909A8..0x90D60: the map-target panel. bMapSelectionFlag branches at 0x909DC between two blocks
 * that build the SAME Ui_AnimExpandBlit call and differ only in the record they start from, the
 * controls they install and the rows they use. */
static void StatusPanel_DrawTargetPanel(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pbMapSelectionFlag == NULL || pEnv->pStrategList == NULL || pEnv->pCtlA == NULL
        || pEnv->pCtlB == NULL || pEnv->pfnResolveInterceptUI == NULL) {
        return;
    }
    const bool bSelecting = *pEnv->pbMapSelectionFlag != 0;

    /* 0x909E0..0x90A04 and 0x90BAC..0x90BD0 store the same six stack values and the same nW. nIdx and
     * nMode come from R8 and nKey from R11 (0xB4), all left behind by the flag-0x10 TEST prologue
     * (0x904EC, 0x904F0, 0x904F4), which runs before this arm's test whether or not flag 0x10 was
     * taken; srcY 0x32, srcX 0x28 and nH 0x5A are immediates. Box x 14..225, y 60..155. */
    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(PUFO_STATUS_TARGET_ANIM_Y), 0xD0, PUFO_STATUS_TARGET_NH,
                            0, 0x28, 0x32, 0xB4, 0);

    StatusPanel_CopyRec(pEnv, psz, bSelecting ? PUFO_STATUS_TEXT_MISSION_PREFIX
                                              : PUFO_STATUS_TEXT_CRASH);   /* 0x90BD4 via R7 / 0x90A08 */

    /* 0x90A30..0x90A54 and 0x90BF8..0x90C1C: the status panel's own row, by colour SLOT 0 -- see
     * StatusPanel_ColourRowText for the read and its guard. */
    const char *const pszRow = StatusPanel_ColourRowText(pEnv);
    if (pszRow != NULL) {
        (void)pufo_strcat(psz, pszRow, PUFO_TEXT_REC_STRIDE);
    }

    if (!bSelecting) {
        (void)pufo_strcat(psz, " ", PUFO_TEXT_REC_STRIDE);                /* 0x90A58 asc_AADB8 */

        /* 0x90A64..0x90A8C: colour SLOT 1 -- not slot 0 -- picks the strateg entry, and that entry's
         * bCraftClass byte at +0x02 picks the size word (0→1359, 1/2→1360, 3/4→1361, 5/6→1362,
         * 7→1363). Outside 0..7 the jumptable's ADDLS does not fire, the default case at 0x90B08 runs,
         * and nothing is appended. The record is strcat'd straight off the aTextRecs block at 0x90B04,
         * with no intermediate copy. */
        const StrategItem *const pKindEntry = StatusPanel_ListEntry(pEnv, 1);
        const i32 nClass = pKindEntry != NULL ? (i32)(i8)pKindEntry->bCraftClass : -1;
        if (nClass >= 0 && nClass < 8) {
            (void)pufo_strcat(psz,
                              pEnv->pTextRecs + (size_t)PUFO_TEXT_REC_STRIDE * (size_t)g_anStatusSizeRec[nClass],
                              PUFO_TEXT_REC_STRIDE);                      /* 0x90AFC..0x90B04 */
        }
    }

    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_LINE, psz);
                                                                           /* 0x90B30 / 0x90C48, y 70 */

    if (bSelecting) {
        /* 0x90C4C..0x90C80: *pCtlB centred on 120, y 102, with Smap_CancelTarget installed at 0x90C78
         * BEFORE the 0x90C80 blit -- the order the 0x0B, 0x0D, 0x10 and 0x1B arms have the other way. */
        StatusPanel_CentredCtlX(pEnv->pCtlB);                              /* 0x90C54..0x90C60 */
        pEnv->pCtlB->y = (i16)PUFO_STATUS_TARGETCTL_Y_CANCEL;             /* 0x90C64 / 0x90C6C */
        pEnv->pCtlB->pfnClick = pEnv->pfnCancelTarget;                    /* 0x90C74 / 0x90C78 */
        (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);                /* 0x90C80 */

        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);        /* 0x90C84, record 1127 */
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_CANCEL_LBL, psz);
                                                                          /* 0x90CD4, y 103 */

        StatusPanel_CentredCtlX(pEnv->pCtlA);                             /* 0x90CE0..0x90CEC */
        pEnv->pCtlA->y = (i16)PUFO_STATUS_TARGETCTL_Y_OK2;                /* 0x90CF0 / 0x90CF8 */
        pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;              /* 0x90D00 / 0x90D04 */
        (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);                /* 0x90D0C */

        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);          /* 0x90D10, R9 = 1144 * 205 */
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_CANCELLED_LBL, psz);
                                                                          /* 0x90D5C, the shared tail,
                                                                           * y 126 */
        return;
    }

    /* 0x90B34..0x90B68: *pCtlA centred, y 115, handler installed at 0x90B60 before the 0x90B68 blit. */
    StatusPanel_CentredCtlX(pEnv->pCtlA);                                  /* 0x90B3C..0x90B48 */
    pEnv->pCtlA->y = (i16)PUFO_STATUS_TARGETCTL_Y_OK;                     /* 0x90B4C / 0x90B54 */
    pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;                  /* 0x90B5C / 0x90B60 */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                    /* 0x90B68 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);            /* 0x90B6C, record 1127 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_OK_LBL, psz);
                                                                          /* 0x90D5C via 0x90BA4, y 116 */
}

/* 0x90DE8..0x90DFC / 0x90FA0..0x90FB0 / 0x91504..0x91514: the status panel's own SELECTED row, reached as
 * apLines[ *pStatusPanel + 0xBD8 ] -- the nUnk3032 dword UiPanel_TrackHoldRow writes as cursor + row. The
 * read is always the same three instructions: LDR R3,[R0,R11] for the row index (0x90DF4, 0x90FA8,
 * 0x9150C), ADD R3,R0,R3,LSL#2 to step the apLines table, and LDR [R3,R10] (R10 = 0xBE4) for the buffer
 * (0x90DFC, 0x90FB0, 0x91514). The original bounds nothing and never null-checks what it gets: the clear
 * arms feed it to an INLINE copy loop (LDRSB/STRB/MOVS/BNE at 0x90E00..0x90E14 and 0x91218..0x9122C -- not
 * a strcpy call) and the set arms to the strcat at 0x90FB8 / 0x91344. The port skips a row it cannot read
 * (recorded in bugs.tsv). */
static const char *StatusPanel_SelectedRowText(const GeoscapeStatusPanelEnv *pEnv)
{
    const UiPanel *const pPanel = pEnv->pStatusPanel;
    const i32 nRow = (pPanel != NULL) ? pPanel->nUnk3032 : -1;

    if (nRow < 0 || nRow >= PUFO_PANEL_LINES || pPanel->apLines[nRow] == NULL) {
        return NULL;
    }
    return (const char *)pPanel->apLines[nRow];
}

/* 0x90E44..0x90ED8 and 0x90FE8..0x9107C, run identically in both branches of the 0x13 arm: psz is RESET
 * to TEXTS[1232] "Город: " and then the mission-site name is strcat on -- the index being the SLOT 1
 * strateg entry's byte at +0x02 (0x90E94 LDRSB), scaled by 0x18 (the MissionSiteRec stride) off
 * 0x1864F8 + 0xC8 = 0x1865C0 and then +4 for the name. That byte carries two roles: a size class 0..7
 * on a spawned contact, and the mission-site index on an intercept item, because
 * BasePlaneScreen_AddInterceptMission @ 0x7DE14 indexes this very table with the value it stores at
 * +0x02. The original bounds neither the byte nor the resulting address. */
static void StatusPanel_DrawCityLineIdx(const GeoscapeStatusPanelEnv *pEnv, char *psz, i32 nSite)
{
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CITY);               /* 0x90E44 unk_F1360, record 1232 */
    if (nSite >= 0 && nSite < PUFO_MISSION_SITE_COUNT) {
        (void)pufo_strcat(psz, pEnv->paMissionSites[nSite].szName, PUFO_TEXT_REC_STRIDE);
    }
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_SITE_Y_LINE2, psz);
                                                                         /* 0x90ED8 / 0x9107C / 0x917DC, y 86 */
}

static void StatusPanel_DrawCityLine(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    const StrategItem *const pEntry = StatusPanel_ListEntry(pEnv, 1);
    StatusPanel_DrawCityLineIdx(pEnv, psz, (pEntry != NULL) ? (i32)(i8)pEntry->bCraftClass : -1);
}

/* 0x915D4..0x91604 and 0x91868..0x91898, the 0x17 and 0x15 arms' shared tail: the craft is the one colour
 * SLOT 0 names -- a full byte, not the 0..7 class the 0x14 arm reads the same slot as -- and an empty one
 * makes CtlB resolve the intercept instead of starting the craft-select. AND R1,R0,#0xFF (0x915E4, 0x91878)
 * changes nothing observable: UiPanel_InsertLine writes a colour slot with LDRB of a byte argument into the
 * record's dword slot (0x5A314 / 0x5A318), so the value is already 0..255. The first argument of the count
 * call is abFacilityGrid (0x915E8, 0x9187C) and Craft_CountSoldiersAboard never reads it -- 0x664A0 loads its
 * own roster base and 0x664B0 the count. The overwrite lands AFTER the control's blit (0x91580, 0x91814). */
static void StatusPanel_CraftSelectGate(const GeoscapeStatusPanelEnv *pEnv)
{
    const i32 nCraft = (i32)(u8)UiLineList_GetColor(pEnv->pLines, 0, 0);
    const i32 nAboard = (pEnv->apSoldiers != NULL && pEnv->pnSoldierCount != NULL)
                        ? Craft_CountSoldiersAboard(pEnv->apSoldiers, (i32)*pEnv->pnSoldierCount, nCraft)
                        : 0;
    if (nAboard == 0) {
        pEnv->pCtlB->pfnClick = pEnv->pfnResolveInterceptUI;
    }
}

/* 0x90D78..0x9118C: the intercept-mission panel, flag 0x13. Its test prologue (0x90D88) loads
 * R11 = 0xBD8 because this arm reads the panel's own selected row rather than a colour slot. */
static void StatusPanel_DrawMissionSite(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pbMapSelectionFlag == NULL || pEnv->pStrategList == NULL || pEnv->paMissionSites == NULL
        || pEnv->pCtlA == NULL || pEnv->pCtlB == NULL || pEnv->pfnResolveInterceptUI == NULL) {
        return;
    }
    const bool bSelecting = *pEnv->pbMapSelectionFlag != 0;

    /* 0x90DB8..0x90DE4 and 0x90F4C..0x90F78 store the same values as the 0x14 arm. The two zeros are
     * `MOV R0,#0` at 0x90DB0 and the key is the literal 0xB4 at 0x90DAC -- both before the 0x90DB4
     * branch -- because R11 is holding 0xBD8 here and cannot carry the key. Box x 14..225, y 60..155. */
    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(PUFO_STATUS_TARGET_ANIM_Y), 0xD0, PUFO_STATUS_TARGET_NH,
                            0, 0x28, 0x32, 0xB4, 0);

    const char *const pszRow = StatusPanel_SelectedRowText(pEnv);
    if (!bSelecting) {
        if (pszRow != NULL) {
            pufo_strcpy(psz, pszRow);                                    /* 0x90E00..0x90E14: an inline LDRSB/STRB copy loop, not a call */
        } else {
            psz[0] = 0;
        }
    } else {
        /* 0x90F7C..0x90F98: the record itself goes in through an inline LDRSB/STRB loop off R5+R7,
         * with R7 = TEXTS[1238]. */
        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_MISSION_PREFIX);
        if (pszRow != NULL) {
            (void)pufo_strcat(psz, pszRow, PUFO_TEXT_REC_STRIDE);        /* 0x90FB8: the row APPENDED */
        }
    }
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_LINE, psz);
                                                                         /* 0x90E40 / 0x90FE4, y 70 */

    StatusPanel_DrawCityLine(pEnv, psz);

    if (bSelecting) {
        StatusPanel_CentredCtlX(pEnv->pCtlB);                             /* 0x91088..0x91094 */
        pEnv->pCtlB->y = (i16)PUFO_STATUS_SITECTL_Y_CANCEL;              /* 0x91098 / 0x910A0: y 105 */
        pEnv->pCtlB->pfnClick = pEnv->pfnCancelTarget;                   /* 0x910A8 / 0x910AC */
        (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);               /* 0x910B4 */

        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);       /* 0x910B8, record 1127 */
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_SITE_Y_CANCEL_LBL, psz);
                                                                         /* 0x91104, y 106 */

        StatusPanel_CentredCtlX(pEnv->pCtlA);                             /* 0x91110..0x9111C */
        pEnv->pCtlA->y = (i16)PUFO_STATUS_SITECTL_Y_OK2;                 /* 0x91120 / 0x91128: y 128 */
        pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;             /* 0x91130 / 0x91134 */
        (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);               /* 0x9113C */

        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);         /* 0x91140, R9 = 1144 * 205 */
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_SITE_Y_CANCELLED_LBL, psz);
                                                                         /* 0x9118C, y 129 */
        return;
    }

    StatusPanel_CentredCtlX(pEnv->pCtlA);                                 /* 0x90EE0..0x90EF0 */
    pEnv->pCtlA->y = (i16)PUFO_STATUS_SITECTL_Y_OK;                      /* 0x90EF4 / 0x90EFC: y 115 */
    pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;                 /* 0x90F04 / 0x90F08 */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x90F10 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);           /* 0x90F14, record 1127 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_SITE_Y_OK_LBL, psz);
                                                                         /* 0x9118C via 0x90F48, y 116 */
}

/* 0x91190..0x91480: the travel panel, flag 0x12. The flag-0x14 arm with one thing taken away: this
 * clear arm appends neither the space nor the size word, and its copy of the panel row is a COPY
 * (0x91218..0x9122C) rather than an append. Every anim argument, every row and both control pairs are
 * the same immediates as 0x909E0..0x90D40 -- the duplication is recorded in bugs.tsv. */
static void StatusPanel_DrawTravel(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pbMapSelectionFlag == NULL || pEnv->pStrategList == NULL || pEnv->pCtlA == NULL
        || pEnv->pCtlB == NULL || pEnv->pfnResolveInterceptUI == NULL) {
        return;
    }
    const bool bSelecting = *pEnv->pbMapSelectionFlag != 0;

    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(PUFO_STATUS_TARGET_ANIM_Y), 0xD0, PUFO_STATUS_TARGET_NH,
                            0, 0x28, 0x32, 0xB4, 0);                      /* 0x911C4..0x911F0 / 0x912CC..0x912F8 */

    /* 0x911F4..0x9122C clears the flag: colour SLOT 0 (UiPanel_GetLineColor at 0x91200) gives the row index
     * and that row is COPIED, alone. 0x912FC..0x91344 sets it:
     * TEXTS[1238] first, then the same row strcats on. One draw follows either way -- 0x91258 and
     * 0x91370 are the same centring. */
    const char *const pszRow = StatusPanel_ColourRowText(pEnv);
    if (!bSelecting) {
        if (pszRow != NULL) {
            pufo_strcpy(psz, pszRow);                                    /* 0x91218..0x9122C: the same inline copy loop (tail BNE loc_91218) */
        } else {
            psz[0] = 0;
        }
    } else {
        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_MISSION_PREFIX); /* 0x912FC..0x91314: same inline record copy */
        if (pszRow != NULL) {
            (void)pufo_strcat(psz, pszRow, PUFO_TEXT_REC_STRIDE);        /* 0x91344 */
        }
    }
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_LINE, psz);
                                                                         /* 0x91258 / 0x91370, y 70 */

    if (bSelecting) {
        StatusPanel_CentredCtlX(pEnv->pCtlB);                             /* 0x9137C..0x91388 */
        pEnv->pCtlB->y = (i16)PUFO_STATUS_TARGETCTL_Y_CANCEL;            /* 0x9138C / 0x91394: y 102 */
        pEnv->pCtlB->pfnClick = pEnv->pfnCancelTarget;                   /* 0x9139C / 0x913A0 */
        (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);               /* 0x913A8 */

        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);       /* 0x913AC, record 1127 */
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_CANCEL_LBL, psz);
                                                                         /* 0x913F8, y 103 */

        StatusPanel_CentredCtlX(pEnv->pCtlA);                             /* 0x91404..0x91410 */
        pEnv->pCtlA->y = (i16)PUFO_STATUS_TARGETCTL_Y_OK2;               /* 0x91414 / 0x9141C: y 125 */
        pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;             /* 0x91424 / 0x91428 */
        (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);               /* 0x91430 */

        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);         /* 0x91434, R9 = 1144 * 205 */
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_CANCELLED_LBL, psz);
                                                                         /* 0x91480, y 126 */
        return;
    }

    StatusPanel_CentredCtlX(pEnv->pCtlA);                                 /* 0x91260..0x91270 */
    pEnv->pCtlA->y = (i16)PUFO_STATUS_TARGETCTL_Y_OK;                     /* 0x91274 / 0x9127C: y 115 */
    pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;                  /* 0x91284 / 0x91288 */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                    /* 0x91290 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);            /* 0x91294, record 1127 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_OK_LBL, psz);
                                                                          /* 0x91480 via 0x912C8, y 116 */
}

/* 0x9148C..0x91688: the descent panel, flag 0x17. ONE path -- there is no bMapSelectionFlag branch
 * here. The line is TEXTS[1239] "Снижение: " (R6, loaded by the test prologue at 0x91490) with the
 * panel's SELECTED row appended by the BL strcat at 0x9151C; the row read is the 0x13 arm's
 * three-instruction shape again (0x9150C LDR R3,[R0,R11] with R11 = 0xBD8, sticky from that arm's test
 * prologue at 0x90D88) and the width comes from Text_MeasureWidth at 0x91528 before the centring.
 *
 * The anim is the same nine values as 0x14/0x13 (0x914A4..0x914DC) except that the two zero stack
 * args come from R7 (`MOV R7,#0` at 0x9149C) instead of R8. CtlB is centred from its own pad and put
 * at y 102 with Smap_BeginCraftSelect @ 0x838dc stored at 0x91578 BEFORE the 0x91580 blit, TEXTS[1127]
 * (R8 = aTextOk) centred at y 103.
 *
 * 0x915D4..0x915EC then asks how many soldiers are aboard the craft that colour SLOT 0 names:
 * UiPanel_GetLineColor(pPanel, nLine 0, nSlot 0), AND #0xFF, then Craft_CountSoldiersAboard. The
 * original hands abFacilityGrid as the first argument and the callee never reads it -- 0x664A0 loads
 * its own roster base and 0x664B0 the count. If the count is ZERO the 0x915F4 MOVS feeds four LDREQ /
 * STREQ instructions at 0x915F8..0x91604 that OVERWRITE CtlB's handler with Smap_ResolveInterceptUI,
 * after its blit has already happened. CtlA then follows at y 125 with the resolve handler stored at
 * 0x91630 before the 0x91638 blit, and TEXTS[1144] (R9) centres at y 126 (0x91688). Every row here is
 * the 0x14 arm's set-arm row -- the third copy of this body in the chain. */
static void StatusPanel_DrawDescentPanel(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pStrategList == NULL || pEnv->pCtlA == NULL || pEnv->pCtlB == NULL
        || pEnv->pfnResolveInterceptUI == NULL || pEnv->pfnBeginCraftSelect == NULL) {
        return;
    }

    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(PUFO_STATUS_TARGET_ANIM_Y), 0xD0, PUFO_STATUS_TARGET_NH,
                            0, 0x28, 0x32, 0xB4, 0);                      /* 0x914A4..0x914DC */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_DESCENT);            /* 0x914E0..0x91500, R6 = TEXTS[1239] */
    const char *const pszRow = StatusPanel_SelectedRowText(pEnv);        /* 0x91504..0x91514 */
    if (pszRow != NULL) {
        (void)pufo_strcat(psz, pszRow, PUFO_TEXT_REC_STRIDE);            /* 0x9151C */
    }
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_LINE, psz);
                                                                         /* 0x91528, 0x91548, y 70 */

    StatusPanel_CentredCtlX(pEnv->pCtlB);                                 /* 0x91554..0x91560 */
    pEnv->pCtlB->y = (i16)PUFO_STATUS_TARGETCTL_Y_CANCEL;                /* 0x9156C: y 102 */
    pEnv->pCtlB->pfnClick = pEnv->pfnBeginCraftSelect;                   /* 0x91578 */
    (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x91580 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);           /* 0x91584..0x915A0, R8 = aTextOk */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_CANCEL_LBL, psz);
                                                                         /* 0x915D0, y 103 */

    StatusPanel_CraftSelectGate(pEnv);                                   /* 0x915D4..0x91604 */

    StatusPanel_CentredCtlX(pEnv->pCtlA);                                 /* 0x9160C..0x91618 */
    pEnv->pCtlA->y = (i16)PUFO_STATUS_TARGETCTL_Y_OK2;                   /* 0x91624: y 125 */
    pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;                 /* 0x91630 */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x91638 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);             /* 0x9163C..0x9165C, R9 = 1144 * 205 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_CANCELLED_LBL, psz);
                                                                         /* 0x91688, y 126 */
}

/* 0x916A0..0x9191C: the descent-target panel, flag 0x15. Flag 0x17's shape with the 0x13 arm's control rows
 * (105, 106, 128, 129) and one extra line. Three values here are sticky registers left by the flag-0x17 test
 * prologue: R6 = TEXTS[1239] (0x91490) for the first line, R7 = 0 (0x9149C) for the anim's two zero stack
 * args, and R11 = 0xBD8 (0x90D88) for the selected-row read.
 *
 * The second line is the 0x13 arm's city line with a DIFFERENT index source: the entry still comes from
 * colour SLOT 1 (0x91778 MOV R2,#1) but the site index is the entry's nTargetIdx dword at +0x84 (0x91798
 * LDR R3,[R0,#0x84]), not the byte at +0x02. Neither the list lookup nor the index is checked -- 0x91798
 * dereferences whatever PtrArray_Get returns and 0x917A0 MULs the index by 0x18 with no bound. */
static void StatusPanel_DrawDescentTarget(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pStrategList == NULL || pEnv->paMissionSites == NULL || pEnv->pCtlA == NULL
        || pEnv->pCtlB == NULL || pEnv->pfnResolveInterceptUI == NULL || pEnv->pfnBeginCraftSelect == NULL) {
        return;
    }

    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(0xE), PufoUi_MapY(PUFO_STATUS_TARGET_ANIM_Y), 0xD0, PUFO_STATUS_TARGET_NH,
                            0, 0x28, 0x32, 0xB4, 0);                      /* 0x916A0..0x916D8 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_DESCENT);            /* 0x916DC..0x916FC, R6 sticky */
    const char *const pszRow = StatusPanel_SelectedRowText(pEnv);        /* 0x91700..0x91710 */
    if (pszRow != NULL) {
        (void)pufo_strcat(psz, pszRow, PUFO_TEXT_REC_STRIDE);            /* 0x91718 */
    }
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_LINE, psz);
                                                                         /* 0x91724, 0x91744, y 70 */

    const StrategItem *const pEntry = StatusPanel_ListEntry(pEnv, 1);    /* 0x91774..0x91790 */
    StatusPanel_DrawCityLineIdx(pEnv, psz, (pEntry != NULL) ? pEntry->nTargetIdx : -1);
                                                                         /* 0x91794..0x917DC */

    StatusPanel_CentredCtlX(pEnv->pCtlB);                                 /* 0x917E8..0x917F4 */
    pEnv->pCtlB->y = (i16)PUFO_STATUS_SITECTL_Y_CANCEL;                  /* 0x917F8: y 105 */
    pEnv->pCtlB->pfnClick = pEnv->pfnBeginCraftSelect;                   /* 0x9180C */
    (void)UiControl_BlitActivePad(pEnv->pCtlB, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x91814 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ORDER_OK);           /* 0x91818..0x91838, R8 = aTextOk */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_SITE_Y_CANCEL_LBL, psz);
                                                                         /* 0x91864, y 106 */

    StatusPanel_CraftSelectGate(pEnv);                                   /* 0x91868..0x91898 */

    StatusPanel_CentredCtlX(pEnv->pCtlA);                                 /* 0x918A0..0x918AC */
    pEnv->pCtlA->y = (i16)PUFO_STATUS_SITECTL_Y_OK2;                     /* 0x918B0: y 128 */
    pEnv->pCtlA->pfnClick = pEnv->pfnResolveInterceptUI;                 /* 0x918C4 */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x918CC */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);             /* 0x918D0..0x918F8, R9 = 1144 * 205 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_SITE_Y_CANCELLED_LBL, psz);
                                                                         /* 0x91910, 0x9191C, y 129 */
}

/* 0x91B34..0x91CE8: the alien-base panel, flag 0x3C (test 0x91B20..0x91B30). The widest panel of the chain:
 * x 2 and nW 0xE8 instead of the 0xE / 0xD0 every other arm uses. It reads no colour slot, no strateg entry,
 * no panel row and no gate -- three records and two controls. Both handlers are stored AFTER both blits, the
 * opposite order from the 0x17 / 0x16 / 0x15 arms. */
static void StatusPanel_DrawAlienBasePanel(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    if (pEnv->pCtlConfirm == NULL || pEnv->pCtlA == NULL
        || pEnv->pfnSelectTarget == NULL || pEnv->pfnClearSelection == NULL) {
        return;
    }

    (void)Ui_AnimExpandBlit(pEnv->pAnim, PufoUi_MapX(2), PufoUi_MapY(PUFO_STATUS_TARGET_ANIM_Y), 0xE8, PUFO_STATUS_TARGET_NH,
                            0, 0x28, 0x32, 0xB4, 0);                      /* 0x91B34..0x91B70: x 2 (0x91B64),
                                                                          * nW 0xE8 (0x91B6C), nIdx and nMode
                                                                          * zeroed locally at 0x91B48 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_ALIEN_BASE);         /* 0x91B74..0x91B98, record 1445 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_LINE, psz);
                                                                         /* 0x91BA4, 0x91BC4, y 70 */

    StatusPanel_CentredCtlX(pEnv->pCtlConfirm);                           /* 0x91BD0..0x91BDC */
    pEnv->pCtlConfirm->y = (i16)PUFO_STATUS_BASECTL_Y_CONFIRM;           /* 0x91BE0: y 100 */
    (void)UiControl_BlitActivePad(pEnv->pCtlConfirm, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x91BF0 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CENTRE_BASE);        /* 0x91BF4..0x91C18, record 1446 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_BASE_Y_CONFIRM_LBL, psz);
                                                                         /* 0x91C24, 0x91C44, y 100: the label
                                                                          * sits on the control's own row */

    StatusPanel_CentredCtlX(pEnv->pCtlA);                                 /* 0x91C50..0x91C5C */
    pEnv->pCtlA->y = (i16)PUFO_STATUS_TARGETCTL_Y_OK2;                   /* 0x91C60: y 125 */
    (void)UiControl_BlitActivePad(pEnv->pCtlA, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlBlitUser);                   /* 0x91C70 */

    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_CANCEL);             /* 0x91C74..0x91C9C, 0x39418 = 1144 * 205 */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_TARGET_Y_CANCELLED_LBL, psz);
                                                                         /* 0x91CA8, 0x91CC8, y 126 */

    pEnv->pCtlConfirm->pfnClick = pEnv->pfnSelectTarget;                 /* 0x91CD0..0x91CD8, Smap_OnPointerUp @
                                                                          * 0x84688 -- stored after the 0x91BF0
                                                                          * blit */
    pEnv->pCtlA->pfnClick = pEnv->pfnClearSelection;                     /* 0x91CE0..0x91CE8, Smap_ClearSelection
                                                                          * @ 0x84648 */
}

/* 0x91CEC..0x91DB0: the tail every arm of the chain lands on -- the 0x3C body by falling through, every other
 * arm by the BNE at 0x91B30, and the jumptable case for flag 0x2B by the B at 0x8E3E4. The arms that jump
 * straight to the function's default label (the 0x1B arm's gates) skip it. */
static void StatusPanel_DrawChainTail(const GeoscapeStatusPanelEnv *pEnv)
{
    if (pEnv->pnScreenId == NULL || pEnv->pStripPad == NULL || pEnv->apRootCtl == NULL
        || pEnv->apClockDigitPads == NULL || pEnv->pTime == NULL) {
        return;
    }

    if (*pEnv->pnScreenId != PUFO_STATUS_SCREEN_ID_GLOBE_HOVER) {        /* 0x91CF4 CMP R0, #2 */
        PadImage_Blit(pEnv->pStripPad);   /* 0x91CFC / 0x91D04: the pad *pGeoscapeStripPad points at */
        const GeoscapeClockDrawEnv clockEnv = { pEnv->apClockDigitPads, pEnv->pTextRecs,
                                                pEnv->pTime, pEnv->pColor };
        Geoscape_DrawClockPads(&clockEnv);                               /* 0x91D08 / 0x91D0C */
        /* 0x91D10..0x91D38: thirteen blits over the root block's own widgets at 0x1864F8 + 4*i. The original
         * keeps the counter in the spilled global dword_186B08; the port keeps it local, like every other
         * dword_186B08 user. */
        for (i32 n = 0; n < PUFO_GEOUI_ROOT_CTLS; ++n) {
            (void)UiControl_BlitActivePad(pEnv->apRootCtl[n], pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                          pEnv->pCtlBlitUser);
        }
        return;
    }

    /* 0x91D78..0x91D8C: the new-base globe hover. The caller sets three arguments, but only R0 survives its
     * own call site: Geoscape_DrawGlobeHover saves R0 (0x67280 PUSH {R0}, 0x6728C STR to var_s28) and reads it
     * back once, at 0x67860, as the base for the +0x62C / +0x630 pending-pick stores. R1 and R2 are overwritten
     * by the callee's first call at 0x67294, so 0x91D7C and 0x91D84 feed nothing. The hovered tile comes back in
     * R0 and is stored with STRB (0x91D94), so a tile above 0xFF wraps. */
    if (pEnv->pGlobeEnv == NULL || pEnv->pbPendingRegionId == NULL) {
        return;
    }
    *pEnv->pbPendingRegionId = (u8)Geoscape_DrawGlobeHover(pEnv->pGlobeEnv);
    if (pEnv->pbPlacementPending != NULL && *pEnv->pbPlacementPending != 0) { /* 0x91D98..0x91DA4 BNE */
        return;
    }
    if (pEnv->pCloseCtl != NULL) {
        (void)UiControl_BlitActivePad(pEnv->pCloseCtl, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);               /* 0x91DA8..0x91DB0, then B loc_93058 */
    }
}

/* 0x944B4..0x94550: the frame end every arm of the screen switch at 0x8C1B0 lands on. Case 20 falls
 * through into it from 0x944B0 with a dead R0 (BaseAtScreen_Draw takes no argument), and the default
 * case -- screens 13, 14 and any id outside 0..20 -- arrives from the jumptable. */
i32 Geoscape_DrawFrameEnd(const GeoscapeFrameEndEnv *pEnv)
{
    if (pEnv->pBaseAtEnv != NULL) {
        BaseAtScreen_Draw(pEnv->pBaseAtEnv);                           /* 0x944B4 */
    }

    /* 0x944C0 / 0x944C4: the mode is read from memory here, and read AGAIN at 0x944FC before the
     * second test, so the port re-reads it rather than caching it. */
    const i32 nModeA = pEnv->pnListMode != NULL ? *pEnv->pnListMode : -1;
    if (nModeA == PUFO_GEOFRAME_LIST_MODE_SAVING) {
        if (pEnv->pnActionPhaseSave112 != NULL) {
            *pEnv->pnActionPhaseSave112 = PUFO_GEOFRAME_PHASE_SAVE;   /* 0x944E0 */
        }
        (void)UiModal_DrawDialog(pEnv->pModalAnim, pEnv->pTextRecs, PUFO_GEOFRAME_MODAL_WIDTH,
                                 PUFO_GEOFRAME_TEXT_SAVING, PUFO_GEOFRAME_MODAL_LINES,
                                 NULL, NULL, pEnv->pModalLayout);     /* 0x944F8, no buttons */
    }

    const i32 nModeB = pEnv->pnListMode != NULL ? *pEnv->pnListMode : -1; /* 0x944FC LDR R0,[R6] */
    if (nModeB == PUFO_GEOFRAME_LIST_MODE_LOADING) {
        if (pEnv->pnActionPhaseSave112 != NULL) {
            *pEnv->pnActionPhaseSave112 = PUFO_GEOFRAME_PHASE_SAVE;   /* 0x9451C */
        }
        (void)UiModal_DrawDialog(pEnv->pModalAnim, pEnv->pTextRecs, PUFO_GEOFRAME_MODAL_WIDTH,
                                 PUFO_GEOFRAME_TEXT_LOADING, PUFO_GEOFRAME_MODAL_LINES,
                                 NULL, NULL, pEnv->pModalLayout);     /* 0x94534, no buttons */
    }

    if (pEnv->pfnPresent == NULL) {
        return 0;
    }
    return pEnv->pfnPresent(pEnv->pPresentUser);                      /* 0x94538 MOV R0,R4 / 0x9453C */
}

/* 0x94120..0x94150 and the eight copies of the same loop (0x94168..0x94198 for screen 9, 0x93DE0..0x93E08 and
 * 0x93E2C..0x93E54 for 11 and 12, 0x93E68..0x93E90 for 10, 0x941E4..0x94200 / 0x94228..0x94244 /
 * 0x94264..0x94280 for 16, 0x942C8..0x942E4 for 17): blit the active pad of a run of control pointers read out
 * of the screen's own block -- `ADD R3, R6, R0, LSL#2` then `LDR R0, [R3, #off]`. The original spills the loop
 * counter into the shared scratch dword_186B08 (the same global the UI open paths and the flag chain's tail
 * use) and re-reads it from that global on every iteration. The port keeps it local. The bounds are half-open,
 * [nFirst, nEnd), because that is the shape the ARM writes: MOV R0, #first, body, CMP R0, #end, BLT back. */
static void Geoscape_BlitCtlRun(const GeoscapeScreenArmEnv *pEnv, i32 nFirst, i32 nEnd)
{
    if (pEnv->apCtl == NULL) {
        return;
    }
    for (i32 n = nFirst; n < nEnd; ++n) {
        (void)UiControl_BlitActivePad(pEnv->apCtl[n], pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlBlitUser);
    }
}

/* 0x93EB4..0x93F04: the tail screens 10, 11 and 12 all fall into. The gate is the geoscape UI root block's
 * own byte at +0x35, read through R7 -- Geoscape_Draw's argument (0x8C190 MOV R7,R0), which Render_Frame
 * sets to 0x1864F8 at 0x2232C. Both panel pointers are re-read from their globals at 0x93ED4 / 0x93EE8, and
 * both calls get the SAME cursor position (R5/R6 hold nSmapCursorY / nSmapCursorX from 0x93EC0 / 0x93EC4). */
static void Geoscape_TrackBuySellRows(const GeoscapeBuySellTrackEnv *pEnv)
{
    if (pEnv->pbPreviewOk == NULL || *pEnv->pbPreviewOk == 0) {
        return;                                                     /* 0x93EB8 BEQ def_8C1B0 */
    }
    const i32 nX = pEnv->pnCursorX != NULL ? (i32)*pEnv->pnCursorX : 0;
    const i32 nY = pEnv->pnCursorY != NULL ? (i32)*pEnv->pnCursorY : 0;
    (void)UiPanel_TrackHoldRow(pEnv->ppPanelB != NULL ? *pEnv->ppPanelB : NULL, nX, nY); /* 0x93ED8 */
    (void)UiPanel_TrackHoldRow(pEnv->ppPanelC != NULL ? *pEnv->ppPanelC : NULL, nX, nY); /* 0x93EEC */
}

/* 0x94114..0x94154: screen 8, the research screen. The draw comes FIRST here -- the other arms blit their
 * pad before calling -- and the callee blits the pad itself (0x73A40), so this arm has no pad blit. The two
 * controls are the words at aResearchTopics + 0x1D48 / + 0x1D4C (0x160C50, 0x160C54). */
void Geoscape_DrawResearchArm(const ResearchDrawEnv *pResearchEnv, const GeoscapeScreenArmEnv *pArmEnv)
{
    ResearchScreen_Draw(pResearchEnv);                                /* 0x9411C, R0 = &aResearchTopics */
    Geoscape_BlitCtlRun(pArmEnv, 0, PUFO_GEOARM_CTLS_2);             /* 0x94120..0x94150, CMP #2 at 0x9414C */
}

/* 0x9415C..0x941A4: screen 9, the manufacturing screen. The pad is the first word of the product block at
 * 0x159E98; the two controls sit at 0x15EE1C / 0x15EE20 (R6 + 0x4F84, 0x4F88 built as 0x4F00|0x84 at
 * 0x94174 / 0x9417C); the frame draw comes last and gets the block base in R0 (0x9419C). */
void Geoscape_DrawManufArm(PadImage *pPad, const ManufFrameEnv *pManufEnv,
                           const GeoscapeScreenArmEnv *pArmEnv)
{
    PadImage_Blit(pPad);                                              /* 0x94160 LDR R0,[R6] / 0x94164 */
    Geoscape_BlitCtlRun(pArmEnv, 0, PUFO_GEOARM_CTLS_2);             /* 0x94168..0x94198, CMP #2 at 0x94194 */
    ManufScreen_DrawFrame(pManufEnv);                                 /* 0x941A0 MOV R0,R6 */
}

/* 0x93E5C..0x93EB4: screen 10, the transfer screen. Four controls here (CMP #4 at 0x93E8C), the transfer
 * root array's own slots 0x1870DC..0x1870E8. TransferScreen_Draw takes the array in R0, nSelectedBaseIdx
 * read SIGNED in R1 (0x93EA0..0x93EA8) and byte_1870F4 read SIGNED in R2 (0x93E94..0x93E98) -- the base the
 * psi screen selected -- and its value is dropped. Then the buy/sell tail. */
void Geoscape_DrawTransferArm(PadImage *pPad, const TransferDrawEnv *pTransferEnv,
                              const GeoscapeScreenArmEnv *pArmEnv,
                              const GeoscapeBuySellTrackEnv *pTrackEnv)
{
    PadImage_Blit(pPad);                                              /* 0x93E60 LDR R0,[R5] / 0x93E64 */
    Geoscape_BlitCtlRun(pArmEnv, 0, PUFO_GEOARM_CTLS_4);             /* 0x93E68..0x93E90, CMP #4 at 0x93E8C */
    (void)TransferScreen_Draw(pTransferEnv);                          /* 0x93EB0 */
    Geoscape_TrackBuySellRows(pTrackEnv);                             /* 0x93EB4..0x93F04 */
}

/* 0x93DD4..0x93E14 (screen 11) and 0x93E20..0x93E58 (screen 12): two instruction-for-instruction identical
 * prefixes -- *pBuySellScreenPad, then g_aBuySellCtl[0] and [1] -- and ONE shared BuySellScreen_Draw call at
 * 0x93E10, which screen 11 reaches by falling through and screen 12 by the B at 0x93E58. So the two screens
 * draw the same frame, and one function covers both. The pad is 0x161A08; the controls are 0x161A0C and
 * 0x161A10. Both then fall into the same buy/sell tail. */
void Geoscape_DrawBuySellArm(PadImage *pPad, const BuySellDrawEnv *pDrawEnv,
                             const GeoscapeScreenArmEnv *pArmEnv,
                             const GeoscapeBuySellTrackEnv *pTrackEnv)
{
    PadImage_Blit(pPad);                                              /* 0x93DD8 / 0x93E24 */
    Geoscape_BlitCtlRun(pArmEnv, 0, PUFO_GEOARM_CTLS_2);             /* 0x93DE0 / 0x93E2C, CMP #2 */
    BuySellScreen_Draw(pDrawEnv);                                     /* 0x93E10, the shared call site */
    Geoscape_TrackBuySellRows(pTrackEnv);                             /* 0x93EB4..0x93F04 */
}

/* 0x941AC..0x94288: screen 16, the ufopaedia. The callee gets the BLOCK BASE (0x941AC LDR R6,
 * =g_pUfopaediaScreenPad / 0x941B0 MOV R0,R6), so Ui_DrawBaseInfo is the one that blits the screen's own pad
 * (0x9B600) and its close control (0x9B7C0); this arm blits no pad of its own. The ranges are three copies of
 * the same loop over the block's six control slots, apCtl[i] == the word at the base + 8 + 4*i. */
void Geoscape_DrawUfopaediaArm(UiBaseInfoDrawEnv *pBaseInfoEnv, const GeoscapeUfopaediaArmEnv *pGateEnv,
                               const GeoscapeScreenArmEnv *pArmEnv)
{
    Ui_DrawBaseInfo(pBaseInfoEnv);                                    /* 0x941B4 */

    const bool bOpen  = (pGateEnv->pbOpen != NULL) && *pGateEnv->pbOpen != 0;
    const bool bDirty = (pGateEnv->pbEntryDirty != NULL) && *pGateEnv->pbEntryDirty != 0;

    if (bOpen && !bDirty) {                                           /* 0x941C8 BEQ / 0x941D8 BNE */
        Geoscape_BlitCtlRun(pArmEnv, PUFO_GEOARM_UFO_CAT_FIRST, PUFO_GEOARM_CTLS_6);   /* 0x941DC..0x94200 */
    }
    if (bDirty)                                                       /* 0x94204 BEQ def_8C1B0 */
    {
        const bool bWide = (pGateEnv->pbWidePanel != NULL) && *pGateEnv->pbWidePanel != 0;  /* 0x94218 */
        if (bWide) {
            Geoscape_BlitCtlRun(pArmEnv, 0, PUFO_GEOARM_CTLS_4);     /* 0x94220..0x94244, CMP #4 */
        } else {
            Geoscape_BlitCtlRun(pArmEnv, PUFO_GEOARM_UFO_ENTRY_FIRST, PUFO_GEOARM_CTLS_4); /* 0x9425C..0x94280 */
        }
    }
}

/* 0x94288..0x942E8: screen 17, the psi screen. R1 is byte_1870F4 read SIGNED (LDRSB at 0x94290), which the
 * port carries as PsiScreenDrawEnv.nBaseIdx, so the callee needs no extra argument. The two gates are the same
 * globals the callee itself reads, and the two controls are the psi block's own slots +4 / +8. */
void Geoscape_DrawPsiArm(const PsiScreenDrawEnv *pPsiEnv, const GeoscapeScreenArmEnv *pArmEnv)
{
    PsiScreen_Draw(pPsiEnv);                                          /* 0x94294 MOV R0,R6 / 0x94298 */
    if (pPsiEnv->pbOverlayOpen != NULL && *pPsiEnv->pbOverlayOpen != 0) {
        return;                                                       /* 0x942A8 BNE def_8C1B0 */
    }
    if (pPsiEnv->pbPanelOpen == NULL || *pPsiEnv->pbPanelOpen == 0) {
        return;                                                       /* 0x942B8 BEQ def_8C1B0 */
    }
    Geoscape_BlitCtlRun(pArmEnv, 0, PUFO_GEOARM_CTLS_2);             /* 0x942C0..0x942E4, CMP #2 at 0x942E0 */
}

/* 0x92E48..0x92E58, 0x92FB0..0x92FC0 and the shared tail 0x92ACC..0x92ADC: the centring the original writes
 * inline. MOVS R1,R0 / ADDMI R1,R1,#1 / MOV R1,R1,ASR#1 is "if (n < 0) ++n; then n >> 1", so the x the ARM
 * draws at is RSB #centre, i.e. nCentre - ((nWidth + (nWidth < 0)) >> 1). */
static i32 Geoscape_CentredX(i32 nWidth, i32 nCentre)
{
    if (nWidth < 0) {
        ++nWidth;
    }
    return nCentre - (nWidth >> 1);
}

/* 0x92DAC..0x92EE4: screen 4, the soldier list. The close control is positioned BEFORE the draw (the original
 * writes through the g_pUiCloseCtl global at 0x92DB8 / 0x92DC4; the port takes the control and null-guards it,
 * as everywhere else in this port). SoldierScreen_Draw gets the block base abFacilityGrid in R0 (0x92DCC). */
void Geoscape_DrawSoldierArm(const SoldierDrawEnv *pSoldierEnv, const GeoscapeSoldierArmEnv *pArmEnv,
                             const GeoscapeScreenArmEnv *pCtlEnv)
{
    if (pArmEnv->pCloseCtl != NULL) {
        pArmEnv->pCloseCtl->x = PUFO_SOLARM_CLOSE_X;                  /* 0x92DB0 STRH #0xE0 */
        pArmEnv->pCloseCtl->y = PUFO_SOLARM_CLOSE_Y;                  /* 0x92DC8 STRH #1 */
    }
    (void)SoldierScreen_Draw(pSoldierEnv);                            /* 0x92DD0 */
    Geoscape_BlitCtlRun(pCtlEnv, 0, PUFO_SOLARM_CTLS_2);              /* 0x92DE0..0x92DFC, CMP #2 at 0x92DF8 */
    (void)UiControl_BlitActivePad(pArmEnv->pCloseCtl, pCtlEnv->pfnCtlBlit,
                                  pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser); /* 0x92E00 / 0x92E04 */

    const char *const pszText = pArmEnv->pTextRecs;                   /* aTextRecs @ 0xB38D0 */
    (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 0xFF, 0xFF, 0xFF, 10, 10, 10);  /* 0x92E08..0x92E28 */
    const char *const pszTitle = pszText + PUFO_TEXT_REC_STRIDE * PUFO_SOLARM_TEXT_TITLE;
    (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(pszTitle), PUFO_SOLARM_CAP_CENTRE), 26,
                          pszTitle);                                  /* 0x92E40..0x92E60, y 0x1A */
    (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 0xFF, 0xFF, 0xFF, 10, 10, 10);  /* 0x92E64..0x92E80 */
    (void)Text_DrawString(38, 50, pszText + PUFO_TEXT_REC_STRIDE * PUFO_SOLARM_TEXT_COL1);  /* 0x92E9C */
    (void)Text_DrawString(170, 50, pszText + PUFO_TEXT_REC_STRIDE * PUFO_SOLARM_TEXT_COL2); /* 0x92EB8 */
    (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 0xFF, 0xC8, 0x14, 10, 10, 10);  /* 0x92EBC..0x92ED8: the arm's
                                                                               * last act sets a pair no text in
                                                                               * the arm draws with */
}

/* 0x92EE4..0x9309C: screen 5, the soldier name editor. The only arm whose control/draw ORDER depends on a
 * gate: armed (bTransferFlag set) draws first (0x92F38) and runs its controls after; unarmed runs the controls
 * first (0x93070..0x9308C) and draws after them (0x93094), skipping the captions entirely. Both paths end at
 * 0x93054, the close-control blit. */
void Geoscape_DrawSoldierEditArm(const SoldierDrawEnv *pSoldierEnv, const GeoscapeSoldierArmEnv *pArmEnv,
                                 const GeoscapeScreenArmEnv *pCtlEnv)
{
    if (pArmEnv->pPad != NULL) {
        PadImage_Blit(pArmEnv->pPad);                                 /* 0x92EE8 / 0x92EEC */
    }
    if (pArmEnv->pCloseCtl != NULL) {
        pArmEnv->pCloseCtl->x = PUFO_SOLARM_CLOSE_X;                  /* 0x92EF4 STRH #0xE0 */
        pArmEnv->pCloseCtl->y = PUFO_SOLARM_CLOSE_Y;                  /* 0x92F00 STRH #1 */
    }

    if (pArmEnv->pbTransferFlag != NULL && *pArmEnv->pbTransferFlag != 0) {  /* 0x92F18 BEQ loc_93064 */
        if (pArmEnv->pCloseCtl != NULL) {
            pArmEnv->pCloseCtl->x = PUFO_SOLTR_CLOSE_X;               /* 0x92F20 STRH #0xC6 */
            pArmEnv->pCloseCtl->y = PUFO_SOLTR_CLOSE_Y;               /* 0x92F28 STRH #0x4D */
        }
        (void)SoldierScreen_Draw(pSoldierEnv);                        /* 0x92F38: BEFORE the controls */
        Geoscape_BlitCtlRun(pCtlEnv, 0, PUFO_SOLARM_CTLS_3);          /* 0x92F48..0x92F64, CMP #3 */

        const char *const pszText = pArmEnv->pTextRecs;
        (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 0x44, 0xF4, 0xFC, 4, 70, 75);  /* 0x92F68..0x92F90 */
        const char *const pszHead = pszText + PUFO_TEXT_REC_STRIDE * PUFO_SOLTR_TEXT_HEAD;
        (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(pszHead), PUFO_SOLTR_HEAD_CENTRE), 38,
                              pszHead);                               /* 0x92FA8..0x92FC8, y 0x26 */
        (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 0x44, 0xF4, 0xFC, 4, 70, 75);  /* 0x92FCC..0x92FE8: the same
                                                                                  * pair packed twice */
        const char *const pszRow1 = pszText + PUFO_TEXT_REC_STRIDE * PUFO_SOLTR_TEXT_ROW1;
        (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(pszRow1), PUFO_SOLTR_ROW1_CENTRE), 80,
                              pszRow1);                               /* 0x92FFC..0x9301C, y 0x50 */
        const char *const pszRow2 = pszText + PUFO_TEXT_REC_STRIDE * PUFO_SOLTR_TEXT_ROW2;
        (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(pszRow2), PUFO_SOLTR_ROW2_CENTRE), 80,
                              pszRow2);                               /* 0x93030..0x93050, y 0x50 */
    } else {
        Geoscape_BlitCtlRun(pCtlEnv, 0, PUFO_SOLARM_CTLS_3);          /* 0x93070..0x9308C, CMP #3 */
        (void)SoldierScreen_Draw(pSoldierEnv);                        /* 0x93094: AFTER the controls */
    }
    (void)UiControl_BlitActivePad(pArmEnv->pCloseCtl, pCtlEnv->pfnCtlBlit,
                                  pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser); /* 0x93054 / 0x93058 */
}

/* 0x92AF0..0x92DAC: screen 7, the base-facility panel (BaseFacScreen_Open @ 0x61858 puts 7 in
 * nGeoscapeScreen), and the same pad screen 1 blits at 0x91DCC. Rails, one separator, the glyph palette
 * base, three callees, then two page controls that an armed placement cancels. The ramp colours are the
 * shared idiom the soldier screen uses too -- climbing 5*(k+20), falling 0xE6-5*k and 0xFA-5*k -- so the
 * PUFO_SOL_RAMP_* constants of pufo_baseui.h are reused rather than restated. */
void Geoscape_DrawBaseFacArm(const GeoscapeBaseFacArmEnv *pArmEnv, const FacNameGridEnv *pGridEnv,
                             const BaseStripDrawEnv *pStripEnv, const BuildDialogEnv *pDialogEnv)
{
    if (pArmEnv->pPad != NULL) {
        PadImage_Blit(pArmEnv->pPad);                     /* 0x92AF0 LDR / 0x92AF4 LDR / 0x92AF8 */
    }

    /* The side rails (0x92B08..0x92C18): 24 steps of 7, 1x7 rects at x 2 and 237 -- climbing at y 7k+1,
     * falling at y 7k+151. */
    for (i32 k = 0; k < PUFO_BFA_RAIL_STEPS; ++k) {
        const u8 nRise = (u8)(PUFO_SOL_RAMP_STEP * (k + PUFO_SOL_RAMP_BASE));   /* 100..215 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_SIDE - PUFO_SOL_RAMP_STEP * k);     /* 230..115 */
        const i32 nYUp = PUFO_BFA_SIDE_Y0 + PUFO_BFA_SIDE_STEP * k;             /* 1..162 */
        const i32 nYDn = PUFO_BFA_SIDE_Y1 + PUFO_BFA_SIDE_STEP * k;             /* 151..312 */

        SoftSurf_BlitRectProxy(0, PUFO_BFA_SIDE_X0, nYUp, 1, PUFO_BFA_SIDE_H, nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, PUFO_BFA_SIDE_X0, nYDn, 1, PUFO_BFA_SIDE_H, nFall, nFall, nFall);
        SoftSurf_BlitRectProxy(0, PUFO_BFA_SIDE_X1, nYUp, 1, PUFO_BFA_SIDE_H, nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, PUFO_BFA_SIDE_X1, nYDn, 1, PUFO_BFA_SIDE_H, nFall, nFall, nFall);
    }

    /* The top and bottom rails (0x92C1C..0x92CE4): 24 steps of 5, 5x1 rects at y 315 and y 2. Both rows
     * start their climbing ramp at x k+1, but the falling one starts at k+128 on top and k+120 on the
     * bottom. ORIGINAL BUG (0x92C80 / 0x92CC4), reproduced: the two immediates are #0x80 and #0x78 where
     * the climbing pair are both #1, so the top row leaves a 7-pixel gap at x 121..127 and its last two
     * rects (k 22 / 23, x 238..247) fall past the 240-pixel edge, while the bottom row's ramps meet at
     * x 120 and end exactly at x 239. */
    for (i32 k = 0; k < PUFO_BFA_RAIL_STEPS; ++k) {
        const u8 nRise = (u8)(PUFO_SOL_RAMP_STEP * (k + PUFO_SOL_RAMP_BASE));   /* 100..215 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_EDGE - PUFO_SOL_RAMP_STEP * k);     /* 250..135 */
        const i32 nXLo = PUFO_BFA_EDGE_X_RISE + PUFO_BFA_EDGE_STEP * k;         /* 1..116 */

        SoftSurf_BlitRectProxy(0, nXLo, PUFO_BFA_EDGE_Y_TOP, PUFO_BFA_EDGE_W, 1, nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, PUFO_BFA_EDGE_X_FALL + PUFO_BFA_EDGE_STEP * k, PUFO_BFA_EDGE_Y_TOP,
                               PUFO_BFA_EDGE_W, 1, nFall, nFall, nFall);
        SoftSurf_BlitRectProxy(0, nXLo, PUFO_BFA_EDGE_Y_BOT, PUFO_BFA_EDGE_W, 1, nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, PUFO_BFA_EDGE_X_BOT + PUFO_BFA_EDGE_STEP * k, PUFO_BFA_EDGE_Y_BOT,
                               PUFO_BFA_EDGE_W, 1, nFall, nFall, nFall);
    }

    SoftSurf_BlitRectProxy(0, PUFO_BFA_SEP_X, PUFO_BFA_SEP_Y, PUFO_BFA_SEP_W, 1,         /* 0x92CE8..0x92D0C */
                           PUFO_BFA_SEP_COLOR, PUFO_BFA_SEP_COLOR, PUFO_BFA_SEP_COLOR);

    g_nGlyphPalSlotBase = PUFO_BFA_GLYPH_PAL;             /* 0x92D18 MOV R0,#3 / 0x92D20 STR: the arm leaves
                                                           * it at 3 for whatever the frame draws next */

    BaseFacScreen_DrawNameGrid(pGridEnv);                 /* 0x92D2C LDRSB nSelectedBaseIdx, 0x92D34 */
    if (pStripEnv != NULL) {
        BaseStripDrawEnv strip = *pStripEnv;              /* 0x92D38 MOV R1,#0xFFFFFFFF: the strip's mark is
                                                           * THIS arm's argument, so it overwrites whatever the
                                                           * env carried */
        strip.nHighlightRow = PUFO_BFA_STRIP_MARK;
        Base_DrawFacilityIconStrip(&strip);               /* 0x92D40 */
    }
    (void)BaseFacScreen_DrawBuildDialog(pDialogEnv);      /* 0x92D44 LDRSB the same byte a second time */

    if (pArmEnv->pbPlaceArmed != NULL && *pArmEnv->pbPlaceArmed != 0) {
        return;                                           /* 0x92D64 ANDS / 0x92D68 BNE def_8C1B0: the
                                                           * placement overlay is up, nothing is blitted */
    }
    if (pArmEnv->apPanelCtl == NULL) {
        return;                                           /* deviation: the original indexes the block blindly */
    }
    for (i32 n = 0; n < PUFO_BFA_CTLS_2; ++n) {           /* 0x92D78..0x92D94, CMP #2 at 0x92D90. The original
                                                           * runs this with its index in the scratch counter
                                                           * dword_186B08 and leaves it at 2; every user of that
                                                           * global initialises it first, so a local counter is
                                                           * the same loop. */
        (void)UiControl_BlitActivePad(pArmEnv->apPanelCtl[n], pArmEnv->pfnCtlBlit,
                                      pArmEnv->pfnCtlBlitEx, pArmEnv->pCtlBlitUser);
    }
}

/* 0x93F08..0x94104: screen 3, the base-info screen (BaseInfoScreen_Open @ 0x6167C stores 3 at 0x6169C).
 * Case 7's rails inset by three, one control run over the two slots that follow the pad pointer in its own
 * block, a close control the shared byte 0x1870F8 can suppress, then BaseInfoScreen_Draw -- its only call
 * site in the binary. */
void Geoscape_DrawBaseInfoArm(const GeoscapeBaseInfoArmEnv *pArmEnv, const GeoscapeScreenArmEnv *pCtlEnv,
                              const BaseInfoDrawEnv *pBaseInfoEnv)
{
    if (pArmEnv->ppPad != NULL && *pArmEnv->ppPad != NULL) {
        PadImage_Blit(*pArmEnv->ppPad);                     /* 0x93F08 LDR / 0x93F0C LDR [R0] / 0x93F10.
                                                             * Deviation: PadImage_Blit @ 0x110DC dereferences
                                                             * +0x10 with no test, so the original faults on a
                                                             * null pad word rather than skipping the blit. */
    }

    for (i32 k = 0; k < PUFO_BI_RAIL_STEPS; ++k) {           /* 0x93F24..0x93FD8, CMP #0xA8 at 0x93FD4 */
        const u8 nRise = (u8)(PUFO_SOL_RAMP_STEP * (k + PUFO_SOL_RAMP_BASE));   /* 100..215 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_SIDE - PUFO_SOL_RAMP_STEP * k);     /* 230..115 */
        const i32 nYUp = PUFO_BI_SIDE_Y0 + PUFO_BI_SIDE_STEP * k;               /* 1..162 */
        const i32 nYDn = PUFO_BI_SIDE_Y1 + PUFO_BI_SIDE_STEP * k;               /* 151..312 */

        SoftSurf_BlitRectProxy(0, PUFO_BI_SIDE_X0, nYUp, 1, PUFO_BI_SIDE_H, nRise, nRise, nRise);   /* 0x93F54 */
        SoftSurf_BlitRectProxy(0, PUFO_BI_SIDE_X0, nYDn, 1, PUFO_BI_SIDE_H, nFall, nFall, nFall);   /* 0x93F80 */
        SoftSurf_BlitRectProxy(0, PUFO_BI_SIDE_X1, nYUp, 1, PUFO_BI_SIDE_H, nRise, nRise, nRise);   /* 0x93FA4, x 0xED */
        SoftSurf_BlitRectProxy(0, PUFO_BI_SIDE_X1, nYDn, 1, PUFO_BI_SIDE_H, nFall, nFall, nFall);   /* 0x93FC8 */
    }

    /* 0x93FE8..0x940A8, CMP #0x78 at 0x940A4. The arm draws both climbing rects first and both falling ones
     * after, and the falling x comes from ONE immediate (0x9404C ADD R9,R8,#0x77) used by both rows -- so the
     * ramps overlap at x 119..120 instead of leaving case 7's gap. */
    for (i32 k = 0; k < PUFO_BI_RAIL_STEPS; ++k) {
        const u8 nRise = (u8)(PUFO_SOL_RAMP_STEP * (k + PUFO_SOL_RAMP_BASE));   /* 100..215 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_EDGE - PUFO_SOL_RAMP_STEP * k);     /* 250..135 */
        const i32 nXLo = PUFO_BI_EDGE_X_RISE + PUFO_BI_EDGE_STEP * k;           /* 1..116 */
        const i32 nXHi = PUFO_BI_EDGE_X_FALL + PUFO_BI_EDGE_STEP * k;           /* 119..234 */

        SoftSurf_BlitRectProxy(0, nXLo, PUFO_BI_EDGE_Y_LO, PUFO_BI_EDGE_W, 1, nRise, nRise, nRise); /* 0x94018, y 5 */
        SoftSurf_BlitRectProxy(0, nXLo, PUFO_BI_EDGE_Y_HI, PUFO_BI_EDGE_W, 1, nRise, nRise, nRise); /* 0x94040, y 315 */
        SoftSurf_BlitRectProxy(0, nXHi, PUFO_BI_EDGE_Y_LO, PUFO_BI_EDGE_W, 1, nFall, nFall, nFall); /* 0x94070 */
        SoftSurf_BlitRectProxy(0, nXHi, PUFO_BI_EDGE_Y_HI, PUFO_BI_EDGE_W, 1, nFall, nFall, nFall); /* 0x94098 */
    }

    Geoscape_BlitCtlRun(pCtlEnv, 0, PUFO_BI_CTLS_2);         /* 0x940B8..0x940D4: the block's +4 / +8 slots,
                                                              * CMP #2 at 0x940D4, index in dword_186B08 */

    if (pArmEnv->pbOverlayShown != NULL && *pArmEnv->pbOverlayShown == 0) {
        /* 0x940DC LDRB the gate byte, 0x940E4 ANDS #0xFF, then the predicated LDREQ at 0x940EC and BLEQ at
         * 0x940F0 make the whole blit conditional -- not just the call, also the load of the control. */
        (void)UiControl_BlitActivePad(pArmEnv->pCloseCtl, pCtlEnv->pfnCtlBlit,
                                      pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser);
    }
    (void)BaseInfoScreen_Draw(pBaseInfoEnv);                  /* 0x940F4 LDRSB byte_1870F4 / 0x94100 */
}

/* 0x94314..0x9449C: screen 19, the funding screen. The monthly report first -- the callee reads pFundPad
 * itself, so the address the arm hands it is dead -- then the upkeep sum, the -3,000,000 gate, and the
 * panel plus three centred captions that only appear once the frame animation reports itself complete. */
void Geoscape_DrawFundingArm(FundingDrawEnv *pFundingEnv, const GeoscapeFundingArmEnv *pArmEnv)
{
    (void)FundingScreen_DrawExtended(pFundingEnv);            /* 0x94314 LDR R0,=pFundPad / 0x94318 BL, no LDR
                                                              * [R0]: FundingScreen_DrawExtended loads the global
                                                              * itself at 0x5B5CC / 0x5B5D0 */

    i32 nBalance = (pArmEnv->pnFunds != NULL) ? *pArmEnv->pnFunds : 0;   /* 0x94334, read ONCE */
    i32 nBaseCount = (pArmEnv->pbBaseCount != NULL)
                         ? (i32)(signed char)*pArmEnv->pbBaseCount : 0;   /* 0x94330 LDRSB */
    for (i32 n = 0; nBaseCount > 0 && n < nBaseCount; ++n) {  /* 0x94340 BLE past the loop */
        /* 0x94344/0x94348 MOV R3,R0,LSL#24 then MOV R1,R3,ASR#24 truncates the counter to a signed byte for
         * R1, 0x9434C MOV R0,R5 hands the block base, and 0x94360 SUB R6,R6,R0 subtracts what the callee returns. */
        nBalance -= Base_CalcMonthlyUpkeep(pArmEnv->apCraft, pArmEnv->nCraftCount,
                                           pArmEnv->apSoldiers, pArmEnv->nSoldierCount,
                                           pArmEnv->apSites, pArmEnv->nSiteCount,
                                           pArmEnv->apObjects, pArmEnv->abFacilityGrid, n);
        nBaseCount = (pArmEnv->pbBaseCount != NULL)
                         ? (i32)(signed char)*pArmEnv->pbBaseCount : 0;   /* 0x9435C: the count is re-read on
                                                              * every step, exactly as Geoscape_CloseUI case 19
                                                              * re-reads it at 0x8BB90 */
    }

    if (pArmEnv->anFundBar != NULL) {
        nBalance += pArmEnv->anFundBar[2] + pArmEnv->anFundBar[1] + pArmEnv->anFundBar[3]
                  + pArmEnv->anFundBar[0] + pArmEnv->anFundBar[4];          /* 0x9437C..0x943A0, that order */
    }
    if (nBalance >= PUFO_FUNDS_BANKRUPT_LIMIT) {
        return;                                                             /* 0x943AC BGE def_8C1B0 against
                                                                              * 0xFFD23940 = -3,000,000 */
    }

    if (!Ui_AnimExpandBlit(pArmEnv->pAnim, PUFO_FUND_PANEL_X, PUFO_FUND_PANEL_Y,   /* 0x943B0..0x943EC */
                           PUFO_FUND_PANEL_W, PUFO_FUND_PANEL_H, PUFO_FUND_PANEL_IDX,
                           PUFO_FUND_PANEL_SRC_X, PUFO_FUND_PANEL_SRC_Y, PUFO_FUND_PANEL_KEY,
                           PUFO_FUND_PANEL_MODE)) {
        return;                                                             /* 0x943F0 ANDS R3,R0,#0xFF / BEQ:
                                                                             * progress != 100, no captions */
    }

    (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 150, 50, 50, 50, 1, 1);   /* 0x943F8..0x94418, packed ONCE for all
                                                              * three lines */
    const char *const pszText = pArmEnv->pTextRecs;
    const char *const pszCap0 = pszText + PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_CAP0;
    (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(pszCap0), PUFO_FUND_CAP_CENTRE),  /* 0x9441C..0x94450 */
                          PUFO_FUND_CAP_Y0, pszCap0);
    const char *const pszCap1 = pszText + PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_CAP1;
    (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(pszCap1), PUFO_FUND_CAP_CENTRE),  /* 0x94454..0x94480 */
                          PUFO_FUND_CAP_Y1, pszCap1);
    const char *const pszCap2 = pszText + PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_CAP2;
    /* 0x94484..0x9449C measures the third record, sets y 140 and then B loc_92ACC -- the shared centring tail
     * at 0x92ACC centres R0 and falls through to MOV R0,R4 / BL Text_DrawString at 0x90974. Same call. */
    (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(pszCap2), PUFO_FUND_CAP_CENTRE),
                          PUFO_FUND_CAP_Y2, pszCap2);
}

/* ---- Geoscape_Draw case 6, 0x9309C..0x93DD4: the hangar ---------------------------------------- */

/* Every centring in this arm runs on the measured width TRUNCATED TO A SIGNED BYTE: 0x9345C STRB into the
 * scratch byte at 0x186C24 then 0x93468 LDRSB back out for the header, 0x934E0 MOV R3,R0,LSL#24 / 0x934E4
 * MOV R0,R3,ASR#24 for every row label and value. A label wider than 127 pixels therefore centres on a
 * negative half-width. */
static i32 PlaneCentredX(i32 nWidth, i32 nCentre)
{
    return Geoscape_CentredX((i32)(i8)(u8)nWidth, nCentre);
}

/* The craft the arm indexes, apCraft[nSelectedCraftIdx] @ Map + 0x10AC. 0x931DC reads the raw dword index
 * with no test; the port bounds it and resolves anything else to "no craft". */
static const Ship *PlaneArmCraft(const GeoscapePlaneArmEnv *pArmEnv)
{
    if (pArmEnv->apCraft == NULL || pArmEnv->pnSelCraftIdx == NULL) {
        return NULL;
    }
    const i32 nIdx = *pArmEnv->pnSelCraftIdx;
    if (nIdx < 0 || nIdx >= pArmEnv->nCraftCount) {
        return NULL;
    }
    return pArmEnv->apCraft[nIdx];
}

/* The stock column: apObjects[slot]->auPerBaseVal[(char)nSelectedBaseIdx], the LDRH at +0x2A (0x9359C). The
 * slot is the weapon's own ammo object -- 52, 53, 54, none, 105 (Elerium-115) and 55 -- the same mapping
 * Ship_ResupplyCraft arms (pufo_ship.h:488). 0x93598 indexes auPerBaseVal with 2 * (char)base with no test
 * at all; the port skips anything out of range. */
static i32 PlaneArmStock(const GeoscapePlaneArmEnv *pArmEnv, i32 nSlot)
{
    if (pArmEnv->apObjects == NULL || nSlot < 0 || nSlot >= pArmEnv->nObjectCount
        || pArmEnv->apObjects[nSlot] == NULL) {
        return 0;
    }
    const i32 nBase = (pArmEnv->pnSelBaseIdx != NULL) ? (i32)*pArmEnv->pnSelBaseIdx : 0;
    if (nBase < 0 || nBase >= PUFO_FAC_BASE_COUNT) {
        return 0;
    }
    return pArmEnv->apObjects[nSlot]->auPerBaseVal[nBase];
}

/* The six stat rows, in the original's order: the control slot (block +0x44..+0x58), the TEXTS record
 * (100..105) and the ammo object whose per-base count fills the third column. */
static const i16 g_anPlaneStatSlot[PUFO_PLANE_STAT_ROWS] = { 1, 2, 3, 4, 5, 6 };
static const i16 g_anPlaneAmmoSlot[PUFO_PLANE_STAT_ROWS] = { 52, 53, 54, PUFO_PLANE_AMMO_SLOT_NONE, 105, 55 };

void Geoscape_DrawPlaneArm(const GeoscapePlaneArmEnv *pArmEnv)
{
    const GeoscapeScreenArmEnv *const pCtlEnv = pArmEnv->pCtlEnv;
    const i32 nStores = (pArmEnv->pbStoresCommitted   != NULL) ? *pArmEnv->pbStoresCommitted   : 0;
    const i32 nList   = (pArmEnv->pbListPanelsBuilt   != NULL) ? *pArmEnv->pbListPanelsBuilt   : 0;
    const i32 nDetail = (pArmEnv->pbShowCraftDetail   != NULL) ? *pArmEnv->pbShowCraftDetail   : 0;
    const i32 nStatA  = (pArmEnv->pbStatPanelAOpen    != NULL) ? *pArmEnv->pbStatPanelAOpen    : 0;
    const i32 nStatB  = (pArmEnv->pbStatPanelBOpen    != NULL) ? *pArmEnv->pbStatPanelBOpen    : 0;

    if (pArmEnv->ppBackdropPad != NULL && *pArmEnv->ppBackdropPad != NULL) {
        (void)PadImage_Blit(*pArmEnv->ppBackdropPad);   /* 0x930A0 LDR [R10] / 0x930A4 */
    }

    /* 0x930A8..0x930F0: the two store-list panels follow the map cursor, but only with the stores committed
     * AND the root block's +0x35 byte set (0x930B8 LDRB [R7,#0x35] / CMP #0 / BEQ). Both cursors LDRSH. */
    if (nStores != 0 && pArmEnv->pbPreviewOk != NULL && *pArmEnv->pbPreviewOk != 0) {
        const i32 nX = (pArmEnv->pnCursorX != NULL) ? (i32)*pArmEnv->pnCursorX : 0;
        const i32 nY = (pArmEnv->pnCursorY != NULL) ? (i32)*pArmEnv->pnCursorY : 0;
        if (pArmEnv->ppSelRowPanel != NULL && *pArmEnv->ppSelRowPanel != NULL) {
            (void)UiPanel_TrackHoldRow(*pArmEnv->ppSelRowPanel, nX, nY);   /* 0x930DC */
        }
        if (pArmEnv->ppCommitRowPanel != NULL && *pArmEnv->ppCommitRowPanel != NULL) {
            (void)UiPanel_TrackHoldRow(*pArmEnv->ppCommitRowPanel, nX, nY); /* 0x930F0 */
        }
    }

    (void)BasePlaneScreen_Draw(pArmEnv->pPlaneEnv);      /* 0x930F8, R0 = the block base itself */

    if (nDetail != 0) {                                  /* 0x93110 LDRB / 0x93114 BEQ loc_93380: with the detail
                                                         * view on screen this whole prefix is skipped */
        if (nStores != 0) {                              /* 0x93118 LDRB [R9] / 0x93120 BEQ loc_9314C */
            for (i32 i = 0; i < 2; ++i) {                /* 0x93124..0x93148, CMP #2 */
                (void)UiControl_BlitActivePad(pArmEnv->apCtl[PUFO_PLANE_ARM_STORE_FIRST + i],
                                              pCtlEnv->pfnCtlBlit, pCtlEnv->pfnCtlBlitEx,
                                              pCtlEnv->pCtlBlitUser);
            }
        }
        if (nList != 0) {                                /* 0x9314C..0x93180, CMP #2 */
            for (i32 i = 0; i < 2; ++i) {
                (void)UiControl_BlitActivePad(pArmEnv->apCtl[PUFO_PLANE_ARM_LIST_FIRST + i],
                                              pCtlEnv->pfnCtlBlit, pCtlEnv->pfnCtlBlitEx,
                                              pCtlEnv->pCtlBlitUser);
            }
        }

        const Ship *const pCraft = PlaneArmCraft(pArmEnv);
        const u8 nCrewSlots = (pCraft != NULL) ? pCraft->bCrewSlots : 0;    /* +0x14, 0x931E0 LDRB */

        /* 0x93198..0x93290: one crew control per slot, with all four flags clear. Deviation: the control
         * index is +0x30 + 4*i with the UNBOUNDED byte count, so a craft claiming more slots than the block
         * has walks into the weapon controls and past them; the port stops at the block's own words. */
        if (nStatA == 0 && nStatB == 0 && nList == 0 && nStores == 0 && nCrewSlots > 0) {
            for (i32 i = 0; i < nCrewSlots && i < PUFO_PLANE_ARM_CTLS; ++i) {
                (void)UiControl_BlitActivePad(pArmEnv->apCtl[PUFO_PLANE_ARM_MAIN_FIRST + i],  /* 0x931F0 */
                                              pCtlEnv->pfnCtlBlit, pCtlEnv->pfnCtlBlitEx,
                                              pCtlEnv->pCtlBlitUser);
                (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 68, 244, 252, 4, 70, 75);           /* 0x931F8..0x9321C */
                /* Both markers are predicated HI on a FRESH read of the same byte (0x93220..0x9326C), so a
                 * 2-slot craft paints "1" and "2" on every iteration -- the same pixels twice. */
                if (nCrewSlots > 0) {
                    (void)Text_DrawString(PUFO_PLANE_CREW_ONE_X, PUFO_PLANE_CREW_Y, "1");     /* 0x93234..0x93244 */
                }
                if (nCrewSlots > 1) {
                    (void)Text_DrawString(PUFO_PLANE_CREW_TWO_X, PUFO_PLANE_CREW_Y, "2");     /* 0x9325C..0x9326C */
                }
            }
        }

        /* 0x93294..0x9337C: the two captions, gated on Ship.bCanDogfight @ +0x21 and on both panel flags
         * being clear. The captions are measured and drawn INSIDE the two-step loop (0x932E0..0x9337C), so
         * both are drawn twice. */
        if ((pCraft != NULL ? pCraft->bCanDogfight : 0) != 0 && nList == 0 && nStores == 0) {
            const char *const pszCrew = pArmEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_PLANE_ARM_TEXT_CREW;
            const char *const pszEquip = pArmEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_PLANE_TEXT_EQUIP;
            for (i32 i = PUFO_PLANE_ARM_MAIN_FIRST + 2; i < PUFO_PLANE_ARM_MAIN_LAST; ++i) {  /* 0x932C8 MOV #2, CMP #4 */
                (void)UiControl_BlitActivePad(pArmEnv->apCtl[i], pCtlEnv->pfnCtlBlit,
                                              pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser);
                (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 68, 244, 252, 4, 70, 75);           /* 0x932EC..0x93310 */
                (void)Text_DrawString(PlaneCentredX(Text_MeasureWidth(pszCrew), PUFO_PLANE_CAP_CENTRE),
                                      PUFO_PLANE_CAP_CREW_Y, pszCrew);                        /* 0x93314..0x9333C */
                (void)Text_DrawString(PlaneCentredX(Text_MeasureWidth(pszEquip), PUFO_PLANE_CAP_CENTRE),
                                      PUFO_PLANE_CAP_EQUIP_Y, pszEquip);                      /* 0x93340..0x93368 */
            }
        }
    }

    /* 0x93380: the gate every path above falls into. Neither stat panel open -- the arm jumps to the shared
     * tail at 0x91DA8, which loads *g_pUiCloseCtl (0x91DAC) and blits it at the shared site 0x93058. The hangar
     * Init seated that control at (224,1) @ 0x7DDB4 with BasePlaneScreen_Close @ 0x7C9E8 as its click: the exit
     * pad is part of every hangar frame, list view and detail view alike. */
    if (nStatA == 0 && nStatB == 0) {                                       /* 0x9339C BEQ loc_91DA8 */
        if (pArmEnv->pCloseCtl != NULL) {
            (void)UiControl_BlitActivePad(pArmEnv->pCloseCtl, pCtlEnv->pfnCtlBlit,
                                          pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser);
        }
        return;
    }

    /* 0x933A0..0x93438: 226x1 bars at x 7, two per row of the row count. */
    i32 nCounter = 0;
    const i32 nRowCount = (pArmEnv->pnRowCount != NULL) ? *pArmEnv->pnRowCount : 0;
    for (nCounter = 0; nCounter < nRowCount; ++nCounter) {
        (void)SoftSurf_BlitRectProxy(0, PUFO_PLANE_STAT_X,
                                     PUFO_PLANE_STAT_Y0 + PUFO_PLANE_ROW_STEP * nCounter,
                                     PUFO_PLANE_STAT_W, 1, 4, 70, 75);      /* 0x933EC */
        (void)SoftSurf_BlitRectProxy(0, PUFO_PLANE_STAT_X,
                                     PUFO_PLANE_STAT_Y1 + PUFO_PLANE_ROW_STEP * nCounter,
                                     PUFO_PLANE_STAT_W, 1, 4, 70, 75);      /* 0x93420 */
    }

    /* 0x9343C..0x93488: the header. The control at block +0x40 blits BETWEEN the measure and the draw. */
    nCounter = 0;                                                           /* 0x93448 STR 0 */
    const char *const pszHeader = pArmEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_PLANE_TEXT_STAT_HDR;
    const i32 nHeaderWidth = Text_MeasureWidth(pszHeader);                  /* 0x93454 */
    (void)UiControl_BlitActivePad(pArmEnv->apCtl[PUFO_PLANE_ARM_WEAPON_FIRST], pCtlEnv->pfnCtlBlit,
                                  pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser);  /* 0x93460 */
    (void)Text_DrawString(PlaneCentredX(nHeaderWidth, PUFO_PLANE_COL_NAME),
                          PUFO_PLANE_STAT_Y0, pszHeader);                   /* 0x93468..0x93488 */

    char szValue[16];
    for (i32 nRow = 0; nRow < PUFO_PLANE_STAT_ROWS; ++nRow) {
        ++nCounter;                                /* 0x93490..0x9349C: once per row, whatever its count says */
        const i32 nCount = (pArmEnv->pnStats != NULL) ? pArmEnv->pnStats[nRow] : 0;
        if (nCount <= 0) {
            continue;                              /* 0x934A4 BLE: the row's tail increment is skipped as well */
        }
        const i32 nY = PUFO_PLANE_STAT_Y0 + PUFO_PLANE_ROW_STEP * nCounter;
        UiControl *const pRowCtl = pArmEnv->apCtl[PUFO_PLANE_ARM_WEAPON_FIRST + g_anPlaneStatSlot[nRow]];
        pRowCtl->y = (i16)(PUFO_PLANE_STAT_Y0 + PUFO_PLANE_ROW_STEP * (i16)nCounter);  /* 0x934A8 LSL#16/ASR#16,
                                                                             * 0x934BC STRH [R0,#2] */
        (void)UiControl_BlitActivePad(pRowCtl, pCtlEnv->pfnCtlBlit,
                                      pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser);    /* 0x934C4 */
        const char *const pszLabel = pArmEnv->pTextRecs
                                   + PUFO_TEXT_REC_STRIDE * (PUFO_PLANE_TEXT_STAT0 + nRow);
        (void)Text_DrawString(PlaneCentredX(Text_MeasureWidth(pszLabel), PUFO_PLANE_COL_NAME),
                              nY, pszLabel);                                                    /* 0x934D0..0x9350C */
        (void)snprintf(szValue, sizeof(szValue), "%d", nCount);             /* j_itoa at 0x9351C, copied to psz */
        (void)Text_DrawString(PlaneCentredX(Text_MeasureWidth(szValue), PUFO_PLANE_COL_COUNT),
                              nY, szValue);                                                     /* 0x9353C..0x93578 */
        const i16 nAmmoSlot = g_anPlaneAmmoSlot[nRow];
        if (nAmmoSlot == PUFO_PLANE_AMMO_SLOT_NONE) {
            (void)Text_DrawString(PUFO_PLANE_COL_DASH, nY, "-");            /* 0x939D8..0x939F0: no measure,
                                                                              * the laser never runs out */
        } else {
            (void)snprintf(szValue, sizeof(szValue), "%d",
                           PlaneArmStock(pArmEnv, (i32)nAmmoSlot));          /* 0x93AE4..0x93B08 */
            (void)Text_DrawString(PlaneCentredX(Text_MeasureWidth(szValue), PUFO_PLANE_COL_STOCK),
                                  nY, szValue);                                                  /* 0x93B28..0x93B64 */
        }
        ++nCounter;                                /* 0x93600 / 0x93604 / 0x93608: ORIGINAL BUG, reproduced -- the row
                                                    * advances the shared scratch counter a SECOND time, so the
                                                    * three rails below are 20*(6+matched) tall */
    }

    /* 0x93CE8..0x93D74: the three column rails, 1 wide and 20*counter tall at y 93. */
    const i32 nRailH = PUFO_PLANE_ROW_STEP * nCounter;
    (void)SoftSurf_BlitRectProxy(0, PUFO_PLANE_RAIL_X0, PUFO_PLANE_RAIL_Y, 1, nRailH, 4, 70, 75);  /* 0x93D18 */
    (void)SoftSurf_BlitRectProxy(0, PUFO_PLANE_RAIL_X1, PUFO_PLANE_RAIL_Y, 1, nRailH, 4, 70, 75);  /* 0x93D44 */
    (void)SoftSurf_BlitRectProxy(0, PUFO_PLANE_RAIL_X2, PUFO_PLANE_RAIL_Y, 1, nRailH, 4, 70, 75);  /* 0x93D70 */

    if (pArmEnv->pCloseCtl != NULL) {                                       /* 0x93D74 B loc_91DA8: the stat-open
                                                                             * path lands on the same close blit */
        (void)UiControl_BlitActivePad(pArmEnv->pCloseCtl, pCtlEnv->pfnCtlBlit,
                                      pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser);
    }
}

/* 0x91DC8..0x92ADC: screen 1, the placement screen -- Base_EnterBuildMode @ 0x84E60 sets the id. Rails,
 * separator, the ten nav controls, the close control, the ten action captions, the two grid callees, the
 * base name and money line, then EITHER the cursor block (the only arm in the switch that moves the map
 * cursor) or the demolish panel. */
void Geoscape_DrawPlacementArm(const GeoscapePlacementArmEnv *pArmEnv,
                               const GeoscapeScreenArmEnv *pCtlEnv,
                               const FacNameGridEnv *pGridEnv,
                               const BaseStripDrawEnv *pStripEnv)
{
    if (pArmEnv->pPad != NULL) {
        PadImage_Blit(pArmEnv->pPad);                          /* 0x91DCC LDR [R0] / 0x91DD0 */
    }

    /* 0x91DD4..0x91EEC: 24 steps of 7, 1x7 rects at x 2, 237 AND 210 -- case 7's rail idiom with a third
     * rail, the divider between the map area and the action captions. */
    for (i32 k = 0; k < PUFO_PLA_RAIL_STEPS; ++k) {            /* CMP #0xA8 at 0x91EE0 */
        const u8 nRise = (u8)(PUFO_SOL_RAMP_STEP * (k + PUFO_SOL_RAMP_BASE));   /* 100..215 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_SIDE - PUFO_SOL_RAMP_STEP * k);     /* 230..115 */
        const i32 nYUp = PUFO_PLA_SIDE_Y0 + PUFO_PLA_SIDE_STEP * k;             /* 1..162 */
        const i32 nYDn = PUFO_PLA_SIDE_Y1 + PUFO_PLA_SIDE_STEP * k;             /* 151..312 */

        SoftSurf_BlitRectProxy(0, PUFO_PLA_SIDE_X0, nYUp, 1, PUFO_PLA_SIDE_H, nRise, nRise, nRise);  /* 0x91E18 */
        SoftSurf_BlitRectProxy(0, PUFO_PLA_SIDE_X0, nYDn, 1, PUFO_PLA_SIDE_H, nFall, nFall, nFall);  /* 0x91E44 */
        SoftSurf_BlitRectProxy(0, PUFO_PLA_SIDE_X1, nYUp, 1, PUFO_PLA_SIDE_H, nRise, nRise, nRise);  /* 0x91E68 */
        SoftSurf_BlitRectProxy(0, PUFO_PLA_SIDE_X1, nYDn, 1, PUFO_PLA_SIDE_H, nFall, nFall, nFall);  /* 0x91E8C */
        SoftSurf_BlitRectProxy(0, PUFO_PLA_SIDE_X2, nYUp, 1, PUFO_PLA_SIDE_H, nRise, nRise, nRise);  /* 0x91EB0 */
        SoftSurf_BlitRectProxy(0, PUFO_PLA_SIDE_X2, nYDn, 1, PUFO_PLA_SIDE_H, nFall, nFall, nFall);  /* 0x91ED4 */
    }

    /* 0x91EF0..0x91FB4: 24 steps of 5, 5x1 rects at y 315 and y 2. The LEFT half of both rows is the flat
     * colour 7, and 7 is not a colour decision -- it is the side rails' height constant (0x91DE4 MOV R5,#7)
     * stored into the three colour slots at 0x91F14. Only the right half ramps, and its two immediates
     * differ per row: #0x80 on top, #0x78 on the bottom. */
    for (i32 k = 0; k < PUFO_PLA_RAIL_STEPS; ++k) {            /* CMP #0x78 at 0x91FB0 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_EDGE - PUFO_SOL_RAMP_STEP * k);     /* 250..135 */
        const i32 nXLo = PUFO_PLA_EDGE_X_RISE + PUFO_PLA_EDGE_STEP * k;         /* 1..116 */

        SoftSurf_BlitRectProxy(0, nXLo, PUFO_PLA_EDGE_Y_TOP, PUFO_PLA_EDGE_W, 1,
                               PUFO_PLA_SIDE_H, PUFO_PLA_SIDE_H, PUFO_PLA_SIDE_H);               /* 0x91F2C */
        SoftSurf_BlitRectProxy(0, PUFO_PLA_EDGE_X_TOP + PUFO_PLA_EDGE_STEP * k, PUFO_PLA_EDGE_Y_TOP,
                               PUFO_PLA_EDGE_W, 1, nFall, nFall, nFall);                         /* 0x91F5C */
        SoftSurf_BlitRectProxy(0, nXLo, PUFO_PLA_EDGE_Y_BOT, PUFO_PLA_EDGE_W, 1,
                               PUFO_PLA_SIDE_H, PUFO_PLA_SIDE_H, PUFO_PLA_SIDE_H);               /* 0x91F80 */
        SoftSurf_BlitRectProxy(0, PUFO_PLA_EDGE_X_BOT + PUFO_PLA_EDGE_STEP * k, PUFO_PLA_EDGE_Y_BOT,
                               PUFO_PLA_EDGE_W, 1, nFall, nFall, nFall);                         /* 0x91FA4 */
    }

    /* 0x91FB8..0x91FDC: the same 208x1 separator bar case 7 draws, in flat colour 160. */
    SoftSurf_BlitRectProxy(0, PUFO_BFA_SEP_X, PUFO_BFA_SEP_Y, PUFO_BFA_SEP_W, 1,
                           PUFO_BFA_SEP_COLOR, PUFO_BFA_SEP_COLOR, PUFO_BFA_SEP_COLOR);

    /* 0x91FE0..0x92014: the ten nav controls at 0x159888..0x1598AC, which the arm reaches through the grid
     * block at +0x120. The original runs the loop with its index in the scratch counter dword_186B08 and
     * re-reads it after each blit (0x92004); a local counter is the same loop. The null test is the
     * original's own (0x91FF8 CMP #0), so a null slot is skipped, not a fault. */
    if (pArmEnv->apNavCtl != NULL) {
        for (i32 i = 0; i < PUFO_PLA_NAV_CTLS; ++i) {          /* CMP #0xA at 0x92010 */
            UiControl *const pCtl = pArmEnv->apNavCtl[i];
            if (pCtl == NULL) {
                continue;                                      /* 0x91FFC BEQ */
            }
            (void)UiControl_BlitActivePad(pCtl, pCtlEnv->pfnCtlBlit,
                                          pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser);
        }
    }

    /* 0x92018..0x92044: the close control to (194, 4), the close thunk installed at +0xC, then blitted. */
    if (pArmEnv->pCloseCtl != NULL) {
        pArmEnv->pCloseCtl->x = (i16)PUFO_PLA_CLOSE_X;         /* 0x92028 STRH [R1] */
        pArmEnv->pCloseCtl->y = (i16)PUFO_PLA_CLOSE_Y;         /* 0x92034 STRH [R1,#2] */
        pArmEnv->pCloseCtl->pfnClick = pArmEnv->pfnCloseClick; /* 0x9203C STR [R1,#0xC] */
        (void)UiControl_BlitActivePad(pArmEnv->pCloseCtl, pCtlEnv->pfnCtlBlit,
                                      pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser);
    }

    (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 68, 244, 252, 4, 70, 75);   /* 0x92048..0x9206C */

    /* 0x92070..0x92290: the ten action captions -- records 69..73 centred on 60, then 74..78 centred on
     * 180, on rows 229, 247, 265, 283 and 301. Each is measured from the record in place and drawn with the
     * house centring; nothing here truncates the width the way the hangar arm's stat panel does. */
    for (i32 i = 0; i < 5; ++i) {
        const char *const psz = pArmEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * (PUFO_PLA_TEXT_ACTION_0 + i);
        (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(psz), PUFO_PLA_CAP_L),
                              PUFO_PLA_CAP_Y0 + PUFO_PLA_CAP_STEP * i, psz);
    }
    for (i32 i = 0; i < 5; ++i) {
        const char *const psz = pArmEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * (PUFO_PLA_TEXT_ACTION_5 + i);
        (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(psz), PUFO_PLA_CAP_R),
                              PUFO_PLA_CAP_Y0 + PUFO_PLA_CAP_STEP * i, psz);
    }

    g_nGlyphPalSlotBase = PUFO_PLA_GLYPH_PAL;                  /* 0x92294 MOV R0,#3 / 0x922AC STR: the arm
                                                                * leaves it at 3, exactly as case 7 does */
    BaseFacScreen_DrawNameGrid(pGridEnv);                      /* 0x922B0 MOV R0,R5 / 0x922B4 */
    if (pStripEnv != NULL) {
        BaseStripDrawEnv strip = *pStripEnv;                   /* 0x922B8 MOV R1,#0xFFFFFFFF: the mark is this
                                                                * arm's argument, as case 7's is */
        strip.nHighlightRow = PUFO_PLA_STRIP_MARK;
        Base_DrawFacilityIconStrip(&strip);                    /* 0x922C0 */
    }

    (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 68, 244, 252, 4, 70, 75);   /* 0x922C4..0x922E4, packed again */

    const i32 nBase = (pArmEnv->pnSelBaseIdx != NULL) ? (i32)*pArmEnv->pnSelBaseIdx : 0;   /* LDRSB, always */

    if (pArmEnv->pbPlaceActive == NULL || *pArmEnv->pbPlaceActive == 0) {
        /* 0x922E8..0x923D4, reached only with the placement flag CLEAR (0x922F4 BNE jumps the whole block):
         * the base name centred on 120 at y 1, then the money line. */
        if (pArmEnv->apSiteRecs != NULL && nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT) {
            const char *const pszName = pArmEnv->apSiteRecs[nBase].abName;   /* 0x9230C ADD R7,R3,LSL#6 and
                                                                              * 0x92310 ADD #0x1A */
            (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(pszName), PUFO_PLA_NAME_CENTRE),
                                  PUFO_PLA_NAME_Y, pszName);                 /* 0x92314 measure, 0x92344 draw */
        }
        (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 130, 130, 250, 20, 20, 20);  /* 0x92348..0x92368 */

        /* 0x92370..0x923D4: TEXTS[156] copied into the frame's psz, then itoa(nFunds), then "$". The copy
         * loop is unbounded in the original and so are the two strcats; the port builds the same string in
         * one bounded snprintf. The draw is at a FIXED (5, 20) -- the one string in this arm that is not
         * centred. */
        char szMoney[64];
        (void)snprintf(szMoney, sizeof(szMoney), "%s%d$",
                       pArmEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_PLA_TEXT_MONEY,
                       (pArmEnv->pnFunds != NULL) ? *pArmEnv->pnFunds : 0);
        (void)Text_DrawString(PUFO_PLA_MONEY_X, PUFO_PLA_MONEY_Y, szMoney);   /* 0x923CC / 0x923D0 */
    } else {
        /* 0x923D8..0x92848: the cursor block, reached only with the placement flag SET. */
        i32 nTapX = (pArmEnv->pnTapX != NULL) ? *pArmEnv->pnTapX : 0;   /* 0x92404 LDR [=nSmapTapX] */
        i32 nTapY = (pArmEnv->pnTapY != NULL) ? *pArmEnv->pnTapY : 0;   /* 0x92400 LDR [=nSmapTapY] */
        i32 nCell = 0;
        if (pArmEnv->abFacilityGrid != NULL) {
            /* 0x923F0 MOV R2,#0x18 / 0x923F8 MUL / 0x9240C ADD #0x3E / 0x92414 MUL #6 / 0x92418 ADD
             * [=nSmapTapX] / 0x9241C LDRB [R3,R5]: the byte is fac[nBase].tileA[tapY][tapX], and the
             * arm's three immediates (24, 6, 62) are PUFO_GRID_FAC_STRIDE, the 6-wide layer and a bias
             * whose 24*62 = 372 is PUFO_GRID_FAC_OFF. Deviation: the original's address is unbounded, so a
             * negative or oversized tap reads outside the block; the port keeps the index inside it. */
            i32 nIdx = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase
                     + PUFO_PLA_TAP_CELL_STRIDE * nTapY + nTapX;
            if (nIdx < 0) {
                nIdx = 0;
            } else if (nIdx >= PUFO_GRID_SIZE) {
                nIdx = PUFO_GRID_SIZE - 1;
            }
            nCell = pArmEnv->abFacilityGrid[nIdx];
        }

        if (nCell == 0) {
            if (pArmEnv->pbPlaceActive != NULL) {
                *pArmEnv->pbPlaceActive = 0;                   /* 0x92424 BEQ loc_92844 / 0x92844 STRB R11,[R10]
                                                                * with R11 = 0: an empty cell ends the placement */
            }
        } else {
            const i32 nOriginX = (pArmEnv->pnSmapOriginX != NULL) ? *pArmEnv->pnSmapOriginX : 0;
            const i32 nOriginY = (pArmEnv->pnSmapOriginY != NULL) ? *pArmEnv->pnSmapOriginY : 0;
            const u8 nPhase = (u8)(PUFO_PLA_TAP_COLOR_STEP
                                   * (((pArmEnv->pnMapPhase != NULL) ? (i32)*pArmEnv->pnMapPhase : 0)
                                      + PUFO_PLA_TAP_COLOR_BASE));            /* 0x92478 MUL R2,R3,R7 */
            const i32 nX = nOriginX + PUFO_PLA_TAP_CELL * nTapX;              /* ADD R1,R0,R1,LSL#5 */
            const i32 nY = nOriginY + PUFO_PLA_TAP_CELL * nTapY + PUFO_PLA_TAP_Y0;   /* ADD #0x24 */

            if (nCell >= PUFO_PLA_TAP_KIND_FIRST && nCell <= PUFO_PLA_TAP_KIND_LAST) {
                /* 0x92428..0x9245C, 0x925A0: kinds 9..12 (Hyper Wave Decoder, Mind Shield, Hangar, Gravity
                 * shield) take the two-cell cursor AND move the tap first -- code 12 steps tapY then falls
                 * into the tapX step, 11 only tapY, 10 only tapX, 9 neither. The rects are then positioned
                 * from the MOVED tap. ORIGINAL behaviour, reproduced: a draw that moves the map cursor. */
                if (nCell == 12 || nCell == 11) {
                    nTapY -= 1;                                /* 0x92450/0x92454 and 0x925A0/0x925A4 */
                }
                if (nCell == 12 || nCell == 10) {
                    nTapX -= 1;                                /* 0x92458/0x9245C */
                }
                if (pArmEnv->pnTapX != NULL) {
                    *pArmEnv->pnTapX = nTapX;
                }
                if (pArmEnv->pnTapY != NULL) {
                    *pArmEnv->pnTapY = nTapY;
                }
                const i32 nBigX = nOriginX + PUFO_PLA_TAP_CELL * nTapX;
                const i32 nBigY = nOriginY + PUFO_PLA_TAP_CELL * nTapY + PUFO_PLA_TAP_Y0;

                SoftSurf_BlitRectProxy(0, nBigX, nBigY, PUFO_PLA_TAP_BIG_W, 2, 0, nPhase, 0);   /* 0x924A0 */
                SoftSurf_BlitRectProxy(0, nBigX, nBigY, 2, PUFO_PLA_TAP_BIG_W, 0, nPhase, 0);   /* 0x924E8 */
                SoftSurf_BlitRectProxy(0, nBigX + PUFO_PLA_TAP_BIG_X, nBigY, 2,
                                       PUFO_PLA_TAP_BIG_W, 0, nPhase, 0);                       /* 0x92534 */
                SoftSurf_BlitRectProxy(0, nBigX, nOriginY + PUFO_PLA_TAP_CELL * nTapY
                                       + PUFO_PLA_TAP_BIG_Y, PUFO_PLA_TAP_BIG_W, 2, 0, nPhase, 0); /* 0x92578 */
            } else {
                /* 0x925AC..0x926CC: every other cell byte -- one-cell cursor, same corner, same colours. */
                SoftSurf_BlitRectProxy(0, nX, nY, PUFO_PLA_TAP_SMALL_W, 2, 0, nPhase, 0);        /* 0x925F0 */
                SoftSurf_BlitRectProxy(0, nX, nY, 2, PUFO_PLA_TAP_SMALL_W, 0, nPhase, 0);        /* 0x92638 */
                SoftSurf_BlitRectProxy(0, nX + PUFO_PLA_TAP_SMALL_X, nY, 2,
                                       PUFO_PLA_TAP_SMALL_W, 0, nPhase, 0);                      /* 0x92684 */
                SoftSurf_BlitRectProxy(0, nX, nOriginY + PUFO_PLA_TAP_CELL * nTapY
                                       + PUFO_PLA_TAP_SMALL_Y, PUFO_PLA_TAP_SMALL_W, 2, 0, nPhase, 0); /* 0x926CC */
            }

            /* 0x926D0..0x92ADC: the tail both cursor styles share. */
            (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 68, 244, 252, 4, 70, 75);   /* 0x926D0..0x926F8 */

            const BaseFacFlag *pFlag = NULL;
            if (pArmEnv->paFacFlags != NULL && nCell < PUFO_BASEFAC_COUNT) {
                pFlag = &pArmEnv->paFacFlags[nCell];           /* 0x92710 MOV R11,#0x1C / 0x92740 MUL /
                                                                * 0x92744 ADD [=0x186E48]. Deviation: the
                                                                * original indexes the 17 records with the
                                                                * cell byte whatever it says, so a 0xFF cell
                                                                * reads 2,380 bytes past the table. */
                (void)Text_DrawString(PUFO_PLA_FAC_NAME_X, PUFO_PLA_FAC_NAME_Y, pFlag->szName);  /* 0x92748 */
            }
            if (pArmEnv->pMapCtl != NULL) {
                pArmEnv->pMapCtl->pfnClick = pArmEnv->pfnMapClick;             /* 0x92764 STR [R0,#0xC] */
                const i32 nLabelW = (pFlag != NULL)
                                        ? (i32)(i16)(u16)Text_MeasureWidth(pFlag->szName) : 0;   /* 0x927A4
                                                                                                   * LSL#16 /
                                                                                                   * ASR#16 */
                pArmEnv->pMapCtl->x = (i16)(nLabelW + PUFO_PLA_MAPCTL_X_ADD);  /* 0x927B0 ADD #0xA / 0x927B4 STRH */
                pArmEnv->pMapCtl->y = (i16)PUFO_PLA_MAPCTL_Y;                  /* 0x927BC MOV R3,#0xF / STRH */
                (void)UiControl_BlitActivePad(pArmEnv->pMapCtl, pCtlEnv->pfnCtlBlit,
                                              pCtlEnv->pfnCtlBlitEx, pCtlEnv->pCtlBlitUser);      /* 0x927C8 */

                /* 0x927CC..0x92834: TEXTS[1240] "Демонтаж", centred ON the control -- its own width read
                 * back as an unsigned half, the label's half subtracted, one pixel below its y. */
                const char *const pszHint =
                    pArmEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_PLA_TEXT_DEMOLISH;
                (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(pszHint),
                                                        (i32)pArmEnv->pMapCtl->x
                                                        + ((i32)pArmEnv->pMapCtl->uWidth >> 1)),
                                      (i32)pArmEnv->pMapCtl->y + 1, pszHint);
            }
        }
    }

    if (pArmEnv->pbPlaceFail != NULL && *pArmEnv->pbPlaceFail != 0) {
        /* 0x92848..0x92ADC: the demolish-failure panel. */
        (void)Ui_AnimExpandBlit(pArmEnv->pAnim, PUFO_PLA_ANIM_X, PUFO_PLA_ANIM_Y, PUFO_PLA_ANIM_W,
                                PUFO_PLA_ANIM_H, PUFO_PLA_ANIM_IDX, PUFO_PLA_ANIM_SRCX,
                                PUFO_PLA_ANIM_SRCY, PUFO_PLA_ANIM_KEY, PUFO_PLA_ANIM_MODE);     /* 0x92898 */
        (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 0xDC, 0x50, 0x50, 0x32, 0x1E, 0x1E);          /* 0x928A0..0x928BC */
        for (i32 i = 0; i < 3; ++i) {
            const char *const psz = pArmEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * (PUFO_PLA_TEXT_FAIL_0 + i);
            (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(psz), PUFO_PLA_PANEL_CENTRE),
                                  PUFO_PLA_FAIL_Y0 + PUFO_PLA_FAIL_STEP * i, psz);              /* 0x928C0..0x929B8 */
        }
        /* 0x929BC..0x929D8: white over (104,104,104) -- the low channel is the grid-block pointer's own low
         * byte (0x929C0 STR R5,[g2] stores 0x159768 into a colour slot), not a chosen colour. */
        (void)Gfx_PackRgb565Pair(pArmEnv->pColor, 0xFF, 0xFF, 0xFF,
                                 PUFO_PLA_WHITE_LOW, PUFO_PLA_WHITE_LOW, PUFO_PLA_WHITE_LOW);
        for (i32 i = 0; i < 3; ++i) {
            const char *const psz = pArmEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * (PUFO_PLA_TEXT_EMPTY_0 + i);
            (void)Text_DrawString(Geoscape_CentredX(Text_MeasureWidth(psz), PUFO_PLA_PANEL_CENTRE),
                                  PUFO_PLA_EMPTY_Y0 + PUFO_PLA_EMPTY_STEP * i, psz);            /* 0x929DC..0x92ADC:
                                                                                                 * records 80, 81
                                                                                                 * and 82 are empty
                                                                                                 * in the shipped
                                                                                                 * lang.dat */
        }
    }
    /* 0x92ADC B loc_90974 and 0x9097C B def_8C1B0: case 1 ends in the shared frame end, as every arm does. */
}

/* 0x8E5A4: the default arm. 0x8E5E0's BEQ lands at 0x8E658, PAST both calls, so a clear
 * byte_18709C[4] draws no panel -- but it still falls through into the block at 0x8E658. */
static void StatusPanel_DrawDefault(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    StatusPanel_PackColour(pEnv);                                      /* 0x8E5CC */
    if (pEnv->pbMultiSelectPanel != NULL && *pEnv->pbMultiSelectPanel != 0
        && pEnv->pStatusPanel != NULL) {
        const UiPanel *const pPanel = pEnv->pStatusPanel;
        (void)Ui_AnimExpandBlit(pEnv->pAnim, 0xE, pPanel->nPosY - 6,   /* 0x8E62C: panel[0x1B88] - 6 */
                                0xD0, pPanel->nFont * pPanel->nLineCount + 6, /* 0x8E628: panel[0x1B9C] * panel[0xBDC] */
                                0, 0x28, 0x32, 0xB4, 0);               /* 0x8E5E4..0x8E608 */
        UiPanel_DrawText(pEnv->pStatusPanel, pEnv->pColor);                          /* 0x8E654 */
    }
    StatusPanel_DrawActionFail(pEnv, psz);                             /* 0x8E658 */
    /* The chain re-reads line 0's flag at every test, so these are independent tests and not a
     * switch: an arm's body falls straight into the next test at 0x8F404, 0x8F5A4, 0x8F75C. */
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_SELFLAG) {
        StatusPanel_DrawSelectedContact(pEnv, psz);                    /* 0x8E708..0x8F24C */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_BASE_LOST) {
        StatusPanel_DrawBaseLost(pEnv, psz);                           /* 0x8F250..0x8F400 */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_FUEL_LOW) {
        StatusPanel_DrawFuelLow(pEnv, psz);                            /* 0x8F404..0x8F5A0 */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_FLY_TO_POINT) {
        StatusPanel_DrawFlyToPoint(pEnv, psz);                         /* 0x8F5A4..0x8F758 */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_POINT_REACHED) {
        StatusPanel_DrawPointReached(pEnv, psz);                       /* 0x8F75C..0x8F9E0 */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_CRAFT_DETAIL) {
        StatusPanel_DrawCraftDetail(pEnv, psz);                        /* 0x8F9E4..0x9021C */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_INTERCEPT_PAUSE) {
        StatusPanel_DrawInterceptPause(pEnv, psz);                     /* 0x90220..0x904CC */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_CITY_ATTACKED) {
        StatusPanel_DrawCityAttacked(pEnv, psz);                       /* 0x904D8..0x90724 */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_INTERCEPT_LIST) {
        StatusPanel_DrawInterceptList(pEnv, psz);
        return;                           /* 0x90798 / 0x907A8 / 0x907B8 / 0x90904 / 0x9097C: every exit
                                           * of this arm branches to the jumptable default label, whose
                                           * body is 0x944B4 -- the frame ends, the shared tail at 0x91CEC
                                           * and every later flag test are skipped */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_TARGET_PANEL) {
        StatusPanel_DrawTargetPanel(pEnv, psz);                          /* 0x909A8..0x90D60 */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_MISSION_SITE) {
        StatusPanel_DrawMissionSite(pEnv, psz);                          /* 0x90D78..0x9118C */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_TRAVEL_PANEL) {
        StatusPanel_DrawTravel(pEnv, psz);                               /* 0x91190..0x91480 */
    }
    /* Flags 0x17 and 0x16 run the same body twice in the original: 0x91938..0x91B1C is a clone of
     * 0x914A4..0x91688 -- 121 instructions each, in the same order, with the same frame slots and the same
     * callees. 117 of the 121 pairs disassemble identically; the 4 that do not are three BNEs naming their
     * own arm's copy-loop head (0x91500/0x91994, 0x915A4/0x91A38, 0x9165C/0x91AF0) and one `STRB R2,[R0,R1]`
     * (0x91AE4) where the first copy writes `STRB R2,[R1,R0]` (0x91650) -- the same additive address.
     * One port function serves both flags. */
    const i32 nDescentFlag = (i32)UiLineList_GetFlag(pEnv->pLines, 0);
    if (nDescentFlag == PUFO_STATUS_FLAG_DESCENT_PANEL || nDescentFlag == PUFO_STATUS_FLAG_DESCENT_ALT) {
        StatusPanel_DrawDescentPanel(pEnv, psz);                         /* 0x914A4..0x91688, cloned at 0x91938..0x91B1C */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_DESCENT_TARGET) {
        StatusPanel_DrawDescentTarget(pEnv, psz);                        /* 0x916A0..0x9191C */
    }
    if ((i32)UiLineList_GetFlag(pEnv->pLines, 0) == PUFO_STATUS_FLAG_ALIEN_BASE_PANEL) {
        StatusPanel_DrawAlienBasePanel(pEnv, psz);                       /* 0x91B34..0x91CE8 */
    }
    StatusPanel_DrawChainTail(pEnv);                                     /* 0x91CEC..0x91DB0 */
    /* Geoscape_Draw's screen arms from 0x91DC8 are ported as separate Geoscape_Draw*Arm
     * functions, not from here; case 1 (0x91DC8..0x92AC0) is the last one still missing. */
}

/* 0x8D2F4..0x8D838: the '%' arm, the research panel. */
static void StatusPanel_DrawResearch(const GeoscapeStatusPanelEnv *pEnv, char *psz)
{
    StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y_RESEARCH, PUFO_STATUS_ANIM_H_RESEARCH);
    StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_RESEARCH_DONE);
    StatusPanel_PackColour(pEnv);                                      /* 0x8D36C: before the lines */
    StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, PUFO_STATUS_Y_RESEARCH_TITLE, psz);
    DrawScreen_DrawCentered(PUFO_STATUS_Y_RESEARCH_MSG, pEnv->pFac->pszMsg);

    UiControl *const pConfirm = pEnv->pCtlConfirm;
    pConfirm->x = (i16)PUFO_STATUS_CTL_PARKED;                         /* 0x8D3DC */
    pConfirm->y = (i16)PUFO_STATUS_CTL_PARKED;                         /* 0x8D3E4 */
    if (g_anUfopaediaEntryNum[*pEnv->pnShownTopic] != 0) {            /* 0x8D3F8 */
        pConfirm->x = (i16)(PUFO_STATUS_TEXT_X_CENTRE - (pConfirm->uWidth >> 1)); /* 0x8D40C */
        pConfirm->y = (i16)PUFO_STATUS_Y_RESEARCH_REPORT;             /* 0x8D420 */
        pConfirm->pfnClick = pEnv->pfnConfirmReport;                  /* 0x8D428 */
        (void)UiControl_BlitActivePad(pConfirm, pEnv->pfnCtlBlit,
                                      pEnv->pfnCtlBlitEx, pEnv->pCtlBlitUser); /* 0x8D430 */
        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_REPORT);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_RESEARCH_REPORT, psz);  /* 0x8D484 */
    }

    /* 0x8D488..0x8D620: the finished-topic walk. The counter is the spilled global dword_186B08
     * in the original; the port keeps it local, like every other dword_186B08 user. */
    const struct ResearchTopicLive *const pTopic = &pEnv->aTopics[*pEnv->pbFinishedTopic];
    i32 nRow = 0;
    bool bDrawn = false;
    while (nRow < (i32)(sizeof(pTopic->abActivateTopics) / sizeof(pTopic->abActivateTopics[0]))
           && pTopic->abActivateTopics[nRow] != 0) {
        const u8 nTopicId = pTopic->abActivateTopics[nRow];
        if (nTopicId < PUFO_RESEARCH_TOPIC_COUNT && pEnv->abResearchActive[nTopicId] < 2) {
            StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_RESEARCH_NEW);
            bDrawn = true;                                            /* var_1A4, 0x8D4DC */
            DrawScreen_DrawCentered(PUFO_STATUS_Y_RESEARCH_HEAD, psz); /* 0x8D528 */
            StatusPanel_CopyRec(pEnv, psz, pEnv->aTopics[nTopicId].wGate4);
            StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE,
                               PUFO_STATUS_Y_RESEARCH_ROW
                                   + PUFO_STATUS_RESEARCH_ROW_STEP * nRow, psz); /* 0x8D5DC */
            pEnv->abResearchActive[nTopicId] = 1;                     /* 0x8D600 */
        }
        ++nRow;
    }

    UiControl *const pApply = pEnv->pCtlApply;
    pApply->x = (i16)PUFO_STATUS_CTL_PARKED;                          /* 0x8D634 */
    pApply->y = (i16)PUFO_STATUS_CTL_PARKED;                          /* 0x8D63C */
    if (bDrawn) {
        const i32 nApplyY = PUFO_STATUS_Y_RESEARCH_APPLY
                            + PUFO_STATUS_RESEARCH_ROW_STEP * nRow;
        pConfirm->x = (i16)(PUFO_STATUS_TEXT_X_CENTRE - (pConfirm->uWidth >> 1)); /* 0x8D654 */
        pApply->y = (i16)nApplyY;                                     /* 0x8D674 */
        pApply->pfnClick = pEnv->pfnApplyResearch;                    /* 0x8D680 */
        StatusPanel_CopyRec(pEnv, psz, PUFO_STATUS_TEXT_RESEARCH);
        (void)UiControl_BlitActivePad(pApply, pEnv->pfnCtlBlit,
                                      pEnv->pfnCtlBlitEx, pEnv->pCtlBlitUser); /* 0x8D6B0 */
        StatusPanel_DrawAt(PUFO_STATUS_TEXT_X_CENTRE, nApplyY, psz);  /* 0x8D6EC */
    }

    StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_RESEARCH);        /* 0x8D6F0..0x8D758 */
    StatusPanel_PackColour(pEnv);
    StatusPanel_DrawFooter(pEnv, psz, PUFO_STATUS_Y_FOOT_RESEARCH);
}

void Geoscape_DrawStatusPanel(const GeoscapeStatusPanelEnv *pEnv)
{
    char szText[PUFO_TEXT_REC_STRIDE];                                /* the original's psz, which IDA's
                                                                       * frame gives as char[200] at sp+0x20;
                                                                       * the port over-allocates by five so
                                                                       * the unbounded strcat cannot spill */

    if (pEnv == NULL || pEnv->pFac == NULL || pEnv->pLines == NULL || pEnv->pTextRecs == NULL
        || pEnv->pAnim == NULL || pEnv->pColor == NULL || pEnv->pCtlSfx == NULL
        || pEnv->pCtlOpen == NULL) {
        return;
    }

    /* 0x8C760: the head pair. Its second red is R5, still the crosshair's 50 from 0x8C6A0. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 0xDC, 0x50, 0x50,
                             PUFO_SMAP_CROSSHAIR_INDEX, 0x1E, 0x1E);

    const u8 nFlag = UiLineList_GetFlag(pEnv->pLines, 0);             /* 0x8C78C, read 4x */
    if (nFlag < PUFO_STATUS_FLAG_MIN || nFlag > PUFO_STATUS_FLAG_MAX) {
        StatusPanel_DrawDefault(pEnv, szText);
        return;
    }

    /* 0x8C7C0..0x8C804: line 0's colour dwords, read slot 3,2,1,0. Slot 3 is pushed at 0x8C814
     * and BaseFac_BuildStatusMsg never reads it. */
    (void)UiLineList_GetColor(pEnv->pLines, 0, 3);
    const u32 uCol2 = UiLineList_GetColor(pEnv->pLines, 0, 2);
    const u32 uCol1 = UiLineList_GetColor(pEnv->pLines, 0, 1);
    const u32 uCol0 = UiLineList_GetColor(pEnv->pLines, 0, 0);

    (void)BaseFac_BuildStatusMsg(pEnv->pFac, (i32)nFlag, (i32)uCol0, (i32)uCol1, (i32)uCol2);

    switch (nFlag) {
    case ' ':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_TALL);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_STORAGE);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8C8FC */
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_PRODUCTION);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT2, szText);         /* 0x8C954 */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszMsg);   /* 0x8C984 */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_EXTRA, pEnv->pFac->pszExtra); /* 0x8C9B4 */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_TALL);
        StatusPanel_PackColour(pEnv);                                 /* 0x8CA40 */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_TALL);
        break;

    case '!':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_STD);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_MATERIALS);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszMsg);   /* 0x8CB68 */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_EXTRA, pEnv->pFac->pszExtra); /* 0x8CB98 */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8CBC4 */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_STD);
        StatusPanel_PackColour(pEnv);                                 /* 0x8CC50 */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_STD);
        break;

    case '"':
    case '&':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_STD);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_DONE);
        StatusPanel_PackColour(pEnv);                                 /* 0x8CD50 / 0x8D8B0 */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszMsg);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_EXTRA, pEnv->pFac->pszExtra);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_STD);
        StatusPanel_PackColour(pEnv);                                 /* 0x8CE5C / 0x8D9BC */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_STD);
        break;

    case '#':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_STD);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_MONEY);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszMsg);   /* 0x8CF5C */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_EXTRA, pEnv->pFac->pszExtra); /* 0x8CF8C */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8CFB8 */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_STD);
        StatusPanel_PackColour(pEnv);                                 /* 0x8D044 */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_STD);
        break;

    case '$':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_TALL);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_NOWHERE);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8D144 */
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_DIED);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT2, szText);         /* 0x8D19C */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszMsg);   /* 0x8D1CC -- no 138 line */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_TALL);
        StatusPanel_PackColour(pEnv);                                 /* 0x8D258 */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_TALL);
        break;

    case '%':
        StatusPanel_DrawResearch(pEnv, szText);
        break;

    case '\'':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_STD);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_EQUIP_CREW);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszExtra);   /* 0x8DAC0 */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_EXTRA, pEnv->pFac->pszScratch); /* 0x8DAEC */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8DB18 */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_STD);
        StatusPanel_PackColour(pEnv);                                 /* 0x8DBCC */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_STD);
        break;

    case '(':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_STD);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_EQUIP_CRAFT);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszMsg);   /* 0x8DCC8 */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_EXTRA, pEnv->pFac->pszScratch); /* 0x8DCF4 */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8DD20 */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_STD);
        StatusPanel_PackColour(pEnv);                                 /* 0x8DDD4 */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_STD);
        break;

    case ')':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_STD);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_ARRIVED);
        StatusPanel_PackColour(pEnv);                                 /* 0x8DEAC */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszMsg);   /* 0x8DEDC */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8DF08 -- no 138 line */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_STD);
        StatusPanel_PackColour(pEnv);                                 /* 0x8DF88 */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_STD);
        break;

    case '*':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_STD);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_STORAGE2);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszMsg);   /* 0x8E08C */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8E0B8 -- no 138 line */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_STD);
        StatusPanel_PackColour(pEnv);                                 /* 0x8E144 */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_STD);
        break;

    case '+':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_STD);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_ELERIUM);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszMsg);   /* 0x8E24C */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_EXTRA, pEnv->pFac->pszExtra); /* 0x8E27C */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8E2A8 */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_STD);
        StatusPanel_PackColour(pEnv);                                 /* 0x8E334 */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_STD);
        /* 0x8E3E4 is the one jumptable case that jumps to the chain's tail instead of the function's default
         * label, so flag 0x2B draws the strip, the clock and the thirteen root widgets too. */
        StatusPanel_DrawChainTail(pEnv);                              /* 0x91CEC..0x91DB0 */
        break;

    case ',':
        StatusPanel_Anim(pEnv, PUFO_STATUS_ANIM_Y, PUFO_STATUS_ANIM_H_STD);
        StatusPanel_CopyRec(pEnv, szText, PUFO_STATUS_TEXT_DESTROYED);
        DrawScreen_DrawCentered(PUFO_STATUS_Y_MSG, pEnv->pFac->pszExtra); /* 0x8E464 -- pszExtra, not pszMsg */
        DrawScreen_DrawCentered(PUFO_STATUS_Y_TEXT, szText);          /* 0x8E490 */
        StatusPanel_SetControls(pEnv, PUFO_STATUS_CTL_Y_STD);
        StatusPanel_PackColour(pEnv);                                 /* 0x8E51C */
        StatusPanel_DrawFooter(pEnv, szText, PUFO_STATUS_Y_FOOT_STD);
        break;

    default:
        StatusPanel_DrawDefault(pEnv, szText);
        break;
    }
}

/* ------------------------------------------------- Geoscape_Draw @ 0x8c180's own head ------------- */

/* 0x8C214..0x8C29C: the cases 0/2 arm taken while the radar screen is open. The strip pad, the root
 * block's thirteen widgets, the strip pad's own rectangle, then the radar session -- and no map at
 * all: 0x8C29C goes straight to the frame end, so the chrome, the crosshair and the status panel are
 * skipped whole. */
void Geoscape_DrawRadarArm(const GeoscapeRadarArmEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pbRadarActive == NULL || *pEnv->pbRadarActive == 0) {
        return;                                                       /* 0x8C218 / 0x8C220 BEQ loc_8C2B0 */
    }

    PadImage_Blit(pEnv->pStripPad);                                   /* 0x8C224 LDR R6,=pGeoscapeStripPad */

    /* 0x8C240..0x8C258: thirteen blits, `LDR R0,[R7,R0,LSL#2]` with R7 re-loaded from the literal
     * 0x1864F8 at 0x8C238, and the counter spilled into dword_186B08 (0x8C23C, re-read every pass). */
    if (pEnv->apRootCtl != NULL) {
        for (i32 n = 0; n < PUFO_GEO_RADAR_CTLS; ++n) {
            (void)UiControl_BlitActivePad(pEnv->apRootCtl[n], pEnv->pfnCtlBlit,
                                          pEnv->pfnCtlBlitEx, pEnv->pCtlBlitUser);
        }
    }

    /* 0x8C25C..0x8C290: the pad's own rectangle, out of the loaded surface. 0x8C260 re-reads the pad
     * pointer instead of reusing the one it blitted at 0x8C228, and the four immediates land in the
     * stack words SoftSurf_BlitSrcRect @ 0x9ebf8 copies back as nIdx/nColorMid/nColorLow/nKey. */
    const PadImage *const pPad = pEnv->pStripPad;
    SoftSurf_BlitSrcRect(pPad->nOriginX, pPad->nOriginY, (i32)pPad->uWidth, (i32)pPad->uHeight,
                         PUFO_GEO_RADAR_IDX, PUFO_GEO_RADAR_MID, PUFO_GEO_RADAR_LOW,
                         PUFO_GEO_RADAR_KEY);

    RadarScreen_Update(pEnv->pRadarEnv);                              /* 0x8C294 MOV R0,R8 / 0x8C298 */
}

/* 0x8C180..0x8C1B0 plus the twenty-one table words at 0x8C1B4..0x8C20C: the geoscape frame
 * dispatcher. The prologue's [SP,#0x1C0+pObj] is the env's arm wiring, and its var_1A4 byte -- set
 * only at 0x8D4DC and read only at 0x8D628 -- is a local in the blocks that need it. */
i32 Geoscape_Draw(const GeoscapeDrawEnv *pEnv)
{
    if (pEnv == NULL) {
        return 0;
    }
    const i32 nScreenId = (pEnv->pnScreenId != NULL) ? *pEnv->pnScreenId : -1;

    switch (nScreenId) {
    case 0:
    case 2: {                                                          /* 0x8C1B8 / 0x8C1C0 -> 0x8C214 */
        const bool bRadar = (pEnv->pRadarArm != NULL && pEnv->pRadarArm->pbRadarActive != NULL
                             && *pEnv->pRadarArm->pbRadarActive != 0);
        if (bRadar) {
            Geoscape_DrawRadarArm(pEnv->pRadarArm);                   /* 0x8C224..0x8C29C */
        } else {
            Geoscape_BlitMapView(pEnv->pMapView);                     /* 0x8C2B0..0x8C690 */
            /* 0x8C694 MOV R0,R5 / 0x8C698 BL Smap_DrawStrategIcons: the glyphs go onto the map
             * after the chrome block and before the crosshair. Unlike 0x8C690's last layer flip,
             * which the BNE at 0x8C674 skips for screen 2, this call runs for both ids. */
            Smap_DrawStrategIcons((pEnv->pMapView != NULL) ? pEnv->pMapView->pView : NULL,
                                  pEnv->pIconEnv);                    /* 0x8C694..0x8C698 */
            Geoscape_BlitCrosshair(pEnv->pCrosshair);                 /* 0x8C69C..0x8C760 */
            Geoscape_DrawStatusPanel(pEnv->pStatusPanel);             /* 0x8C760..0x91DB0, tail included */
        }
        break;
    }

    case 1:                                                           /* 0x91DC8 */
        Geoscape_DrawPlacementArm(pEnv->pPlacementArm, pEnv->pPlacementCtl,
                                  pEnv->pPlacementGrid, pEnv->pPlacementStrip);
        break;

    case 3:                                                           /* 0x93F08 */
        Geoscape_DrawBaseInfoArm(pEnv->pBaseInfoArm, pEnv->pBaseInfoCtl, pEnv->pBaseInfoDraw);
        break;

    case 4:                                                           /* 0x92DAC */
        Geoscape_DrawSoldierArm(pEnv->pSoldierDraw, pEnv->pSoldierArm, pEnv->pSoldierPanelCtl);
        break;

    case 5:                                                           /* 0x92EE4 */
        Geoscape_DrawSoldierEditArm(pEnv->pSoldierDraw, pEnv->pSoldierArm, pEnv->pSoldierFindCtl);
        break;

    case 6:                                                           /* 0x9309C */
        Geoscape_DrawPlaneArm(pEnv->pPlaneArm);
        break;

    case 7:                                                           /* 0x92AF0 */
        Geoscape_DrawBaseFacArm(pEnv->pBaseFacArm, pEnv->pBaseFacGrid,
                                pEnv->pBaseFacStrip, pEnv->pBaseFacDialog);
        break;

    case 8:                                                           /* 0x94114 */
        Geoscape_DrawResearchArm(pEnv->pResearchDraw, pEnv->pResearchCtl);
        break;

    case 9:                                                           /* 0x9415C */
        Geoscape_DrawManufArm(pEnv->pManufPad, pEnv->pManufDraw, pEnv->pManufCtl);
        break;

    case 10:                                                          /* 0x93E5C */
        Geoscape_DrawTransferArm(pEnv->pTransferPad, pEnv->pTransferDraw,
                                 pEnv->pTransferCtl, pEnv->pBuySellTrack);
        break;

    case 11:                                                          /* 0x93DD4, falling into 0x93E10 */
    case 12:                                                          /* 0x93E20, the B at 0x93E58 */
        Geoscape_DrawBuySellArm(pEnv->pBuySellPad, pEnv->pBuySellDraw,
                                pEnv->pBuySellCtl, pEnv->pBuySellTrack);
        break;

    case 13:
    case 14:                                                          /* 0x8C1EC / 0x8C1F0 */
        break;

    case 15:                                                          /* 0x94304 LDR R0,=pFundPad */
        FundingScreen_Draw(pEnv->pFundEnv);
        break;

    case 16:                                                          /* 0x941AC */
        Geoscape_DrawUfopaediaArm(pEnv->pUfopaediaDraw, pEnv->pUfopaediaGates, pEnv->pUfopaediaCtl);
        break;

    case 17:                                                          /* 0x94288 */
        Geoscape_DrawPsiArm(pEnv->pPsiDraw, pEnv->pPsiCtl);
        break;

    case 18:                                                          /* 0x942F4 LDR R0,=aOptionsListCtl */
        Geoscape_DrawScreen(pEnv->pOptionsScreen);
        break;

    case 19:                                                          /* 0x94314 */
        Geoscape_DrawFundingArm(pEnv->pFundEnv, pEnv->pFundingArm);
        break;

    case 20:                                                          /* 0x944B0: the callee call is the
                                                                       * frame end's own 0x944B4 */
        break;

    default:                                                          /* 0x8C1B4, every id over 20 */
        break;
    }

    /* Every arm ends here -- 0x944B8, the block case 20 falls into at 0x944B4. BaseAtScreen_Draw is
     * inside that fall-through, so only id 20 gets the BaseAt env; the ids that branch to 0x944B8
     * skip it, exactly as the ARM does. */
    GeoscapeFrameEndEnv fe = {0};
    if (pEnv->pFrameEnd != NULL) {
        fe = *pEnv->pFrameEnd;
    }
    fe.pBaseAtEnv = (nScreenId == 20) ? pEnv->pBaseAtEnv : NULL;
    return Geoscape_DrawFrameEnd(&fe);                                 /* 0x944B8..0x94550 */
}
