/* pufo_tactend.c - Tact_Shutdown @ 0x3d568. See pufo_tactend.h for the decode. */
#include "game/pufo_tactend.h"

#include <string.h>

#include "game/pufo_geoscape.h"
#include "game/pufo_objectrec.h"
#include "game/pufo_ship.h"
#include "game/pufo_strateg.h"
#include "game/pufo_unit.h"
#include "render/pufo_render.h"
#include "util/pufo_ptrarray.h"

/* Bank pad groups, in the order the original walks them. Sizes 19, 13, 12, 29, 7, 9,
 * 9, 3, 3, 1, 8, 1, 1 cover widgets 0..112 and 128..129; StartUi_Shutdown (0x38d7c)
 * covers 113..127. */
const TactShutdownBankGroup g_aTactShutdownBankGroups[PUFO_SHUTDOWN_BANK_GROUPS] = {
    {  0, 19 }, { 19, 13 }, { 32, 12 }, { 60, 29 }, { 53,  7 }, { 44,  9 }, { 89,  9 },
    { 98,  3 }, { 101, 3 }, { 104, 1 }, { 105,  8 }, { 128, 1 }, { 129, 1 },
};

/* The cell array is 3600 slots per layer, four layers, row stride 60. */
#define PUFO_SHUTDOWN_CELLS_PER_LAYER 3600
#define PUFO_SHUTDOWN_CELLS_PER_ROW   60
#define PUFO_SHUTDOWN_CELL_LAYERS     4

static void Shutdown_FreePadSlot(PadImage **ppPad)
{
    PadImage *pPad = *ppPad;

    if (pPad == NULL) {
        return;
    }
    PadImage_FreeData(pPad);
    free(pPad);
    *ppPad = NULL; /* deviation 1: the original leaves the global dangling */
}

static void Shutdown_FreePadArray(PadImage **apPad, i32 nCount)
{
    i32 i;

    for (i = 0; i < nCount; ++i) {
        Shutdown_FreePadSlot(&apPad[i]);
    }
}

/* The thirteen bank groups. Each frees one widget's three pad slots and clears them. */
static void Shutdown_FreeBankPads(const TactShutdownEnv *pEnv)
{
    i32 g;

    for (g = 0; g < PUFO_SHUTDOWN_BANK_GROUPS; ++g) {
        const TactShutdownBankGroup *pGroup = &g_aTactShutdownBankGroups[g];
        u8 w;

        for (w = 0; w < pGroup->nCount; ++w) {
            UiControl *pCtl = &pEnv->pBank[pGroup->nFirst + w];
            i32 k;

            for (k = 0; k < PUFO_CONTROL_PADS; ++k) {
                if (pCtl->apPads[k] != NULL) {
                    PadImage_FreeData(pCtl->apPads[k]);
                    free(pCtl->apPads[k]);
                    pCtl->apPads[k] = NULL; /* the original zeroes the slot here */
                }
            }
        }
    }
}

static void Shutdown_FreePanel(const TactShutdownEnv *pEnv, UiPanel **ppPanel)
{
    UiPanel *pPanel = *ppPanel;

    if (pPanel == NULL) {
        return;
    }
    if (pEnv->hooks.pfnPanelFreeLines != NULL) {
        pEnv->hooks.pfnPanelFreeLines(pPanel, pEnv->hooks.pUser); /* 0x3E5E8 */
    }
    free(pPanel);
    *ppPanel = NULL;
}

static void Shutdown_FreeRoster(void **apEntries, i32 nCount)
{
    i32 i;

    for (i = 0; i < nCount; ++i) {
        free(apEntries[i]); /* 0x3E098, 0x3E0E4, 0x3E12C: no null test */
    }
}

/* 0x3DD0C .. 0x3DE10. Civilians go when Soldier.bInZone is zero, enemies all go with no
 * test at all, XCOM go when nUnitType is in 4..8 or bInZone is zero. The bound is
 * re-read after every removal because Soldier_RemoveById shrinks the roster while the
 * index keeps advancing -- the walk therefore skips entries, and that is reproduced. */
static void Shutdown_RemoveUnits(const TactShutdownEnv *pEnv)
{
    i32 nBound;
    i32 i;

    nBound = *pEnv->pnCivilianCount;
    i = 0;
    while (i < nBound) {                                   /* 0x3DD10 */
        Soldier *pUnit = (Soldier *)pEnv->apCivilianUnits[i];

        if (pUnit->bInZone == 0) {                         /* 0x3DD28 */
            *pEnv->pnCivilianCount =
                Soldier_RemoveById(pEnv->apCivilianUnits, *pEnv->pnCivilianCount,
                                   pUnit->nUnk440);       /* 0x3DD34 */
            nBound = *pEnv->pnCivilianCount;              /* 0x3DD40 */
        }
        ++i;
    }

    for (i = 0; i < *pEnv->pnEnemyCount; ++i) {            /* 0x3DD6C, bound re-read */
        Soldier *pUnit = (Soldier *)pEnv->apEnemyUnits[i];

        *pEnv->pnEnemyCount =
            (u8)Soldier_RemoveById(pEnv->apEnemyUnits, *pEnv->pnEnemyCount, pUnit->nUnk440);
    }

    nBound = *pEnv->pnXcomCount;
    i = 0;
    while (i < nBound) {                                   /* 0x3DDC8 */
        Soldier *pUnit = (Soldier *)pEnv->apXcomUnits[i];
        u8 nType = pUnit->nUnitType;

        if ((nType > 3 && nType <= 8) || pUnit->bInZone == 0) {
            *pEnv->pnXcomCount =
                (u8)Soldier_RemoveById(pEnv->apXcomUnits, *pEnv->pnXcomCount, pUnit->nUnk440);
            nBound = *pEnv->pnXcomCount;
        }
        ++i;
    }
}

/* 0x3DE24 .. 0x3E058. The mission outcome decision table. Map_bMissionMode 0, 2 and 3
 * mean the craft leaves; 1 means the site is held. unk_128A8C says whether the mission
 * ended in a retreat rather than a completed landing. */
static void Shutdown_MissionOutcome(const TactShutdownEnv *pEnv)
{
    i32 nStrategIdx = *pEnv->pnStrategIndex;
    u8 nMode = *pEnv->pbMissionMode;
    Ship *pCraft;
    Ship *pStrategCraft;

    if (*pEnv->pbAlternateOutcomePath != 0) {
        if (nMode == 1) {
            BaseRec *pBase = (BaseRec *)pEnv->apBaseCopy[(i8)*pEnv->pbFacilityType];

            Base_DemolishFacility(pEnv->pDemolishEnv, pBase->bUnk4); /* 0x3DE60 */
            nMode = *pEnv->pbMissionMode;
        }
        pStrategCraft = (Ship *)pEnv->apCraftCopy[nStrategIdx];
        if (nMode == 0 && ((StrategItem *)PtrArray_Get(pEnv->pStrategList,
                                                       pStrategCraft->nStrategIdx))->nType == 4) {
            StrategList_RemoveAt(pEnv->pStrategList, pEnv->pShipEnv->pWorld, nStrategIdx,
                                 pEnv->pbSpawnOnceFlag, pEnv->pShipEnv->pSpawnCtx);
        }
        return;
    }

    if (nMode == 0 || nMode == 3 || nMode == 2) {
        *pEnv->pbSpawnOnceFlag = 1;                        /* 0x3DF48 */
        if (*pEnv->pbMissionAborted != 0) {
            StrategList_RemoveAt(pEnv->pStrategList, pEnv->pShipEnv->pWorld, nStrategIdx,
                                 pEnv->pbSpawnOnceFlag, pEnv->pShipEnv->pSpawnCtx);
        } else {
            pStrategCraft = (Ship *)pEnv->apCraftCopy[nStrategIdx];
            if (((StrategItem *)PtrArray_Get(pEnv->pStrategList,
                                             pStrategCraft->nStrategIdx))->nType == 4) {
                StrategList_RemoveAt(pEnv->pStrategList, pEnv->pShipEnv->pWorld, nStrategIdx,
                                     pEnv->pbSpawnOnceFlag, pEnv->pShipEnv->pSpawnCtx);
            }
            pCraft = (Ship *)pEnv->apCraftCopy[*pEnv->pnCraftIndex];
            Ship_RemoveWithCargo(pEnv->pShipEnv, pCraft->bId); /* 0x3DFDC */
        }
    }
    if (nMode == 1 && *pEnv->pbMissionAborted != 0) {
        Base_TransferTacticalLoot(pEnv->pLootEnv, true);   /* 0x3E00C */
    }
    if (nMode == 1 && *pEnv->pbMissionAborted == 0) {
        BaseRec *pBase = (BaseRec *)pEnv->apBaseCopy[(i8)*pEnv->pbFacilityType];

        Base_DemolishFacility(pEnv->pDemolishEnv, pBase->bUnk4); /* 0x3E058 */
    }
}

/* 0x3E328 .. 0x3E3CC. Bounds are 10 * nMapDimY rows by 10 * nMapDimX columns, four
 * layers each, so they are in tenths of a map tile. */
static void Shutdown_FreeCells(const TactShutdownEnv *pEnv)
{
    i32 nY = 10 * (i32)*pEnv->pnMapDimY;
    i32 nX = 10 * (i32)*pEnv->pnMapDimX;
    i32 y;
    i32 x;

    for (y = 0; y < nY; ++y) {
        for (x = 0; x < nX; ++x) {
            i32 nLayer;

            for (nLayer = 0; nLayer < PUFO_SHUTDOWN_CELL_LAYERS; ++nLayer) {
                MapCell **ppCell = &pEnv->apCells[PUFO_SHUTDOWN_CELLS_PER_LAYER * nLayer
                                                  + PUFO_SHUTDOWN_CELLS_PER_ROW * y + x];

                free(*ppCell);
                *ppCell = NULL;
            }
        }
    }
}

void Tact_Shutdown(const TactShutdownEnv *pEnv, u8 bKeepSome)
{
    i32 i;

    if (pEnv == NULL) {
        return;
    }

    if (pEnv->hooks.pfnSoundShutdown != NULL) {
        pEnv->hooks.pfnSoundShutdown(pEnv->hooks.pUser); /* 0x3D570 */
    }
    if (pEnv->hooks.pfnKillTimers != NULL) {
        pEnv->hooks.pfnKillTimers(pEnv->hooks.pUser);    /* 0x3D574 */
    }
    *pEnv->pbActionLatch = 0;  /* 0x3D588 */

    /* The four message panels and the wide one get their line count set BEFORE the
     * null test -- the original assumes the four exist. */
    for (i = 0; i < PUFO_SHUTDOWN_MSG_PANELS; ++i) {
        pEnv->appMsgPanel[i]->nLineCount = PUFO_SHUTDOWN_LINE_COUNT; /* 0x3D5A0..0x3D5C4 */
    }
    (*pEnv->appLinePanel)->nLineCount = PUFO_SHUTDOWN_MSG_PANEL_28;  /* 0x3D5D4 */

    Shutdown_FreePanel(pEnv, pEnv->appLinePanel);                    /* 0x3D5E8 */
    for (i = PUFO_SHUTDOWN_MSG_PANELS - 1; i >= 0; --i) {
        Shutdown_FreePanel(pEnv, &pEnv->appMsgPanel[i]);             /* 0x3D604..0x3D658 */
    }

    Shutdown_FreePadSlot(&pEnv->appPadEarly[0]);                     /* 0x3D680 */
    if (*pEnv->appRmp != NULL) {
        Rmp_FreeNodes(*pEnv->appRmp);                                /* 0x3D6A0 */
        free(*pEnv->appRmp);
        *pEnv->appRmp = NULL;
    }
    free(*pEnv->appBlock156A78);                                     /* 0x3D6B4, no test */
    *pEnv->appBlock156A78 = NULL;
    Shutdown_FreePadSlot(&pEnv->appPadEarly[1]);                     /* 0x3D6CC */
    PufoFont_Free(pEnv->pFont);                                      /* 0x3D6DC */

    if (pEnv->pBank != NULL) {
        Shutdown_FreeBankPads(pEnv);                                 /* 0x3D6E4..0x3DC74 */
    }

    if (*pEnv->appLookCtl != NULL) {
        if (pEnv->hooks.pfnControlFreePads != NULL) {
            pEnv->hooks.pfnControlFreePads(*pEnv->appLookCtl, pEnv->hooks.pUser); /* 0x3DC9C */
        }
        free(*pEnv->appLookCtl);
        *pEnv->appLookCtl = NULL;
    }

    for (i = 0; i < *pEnv->pnTactItemCount; ++i) {                   /* 0x3DCD0 */
        free(pEnv->apTactItems[i]);
    }

    if (bKeepSome != 0) {
        free(*pEnv->appBlock187050);                                 /* 0x3E06C, no test */
        *pEnv->appBlock187050 = NULL;
        Shutdown_FreeRoster(pEnv->apStrategCraftCopy, (i32)*pEnv->pnStrategCraftCopy);
        if (*pEnv->pnBaseCount > 0) {
            Shutdown_FreeRoster(pEnv->apBaseCopy, (i32)*pEnv->pnBaseCount);
        }
        if (*pEnv->pnCraftCount != 0) {
            Shutdown_FreeRoster(pEnv->apCraftCopy, (i32)*pEnv->pnCraftCount);
        }
        PtrArray_DeleteAll(pEnv->pStrategList);                      /* 0x3E154 */
    } else {
        Shutdown_RemoveUnits(pEnv);
        Shutdown_MissionOutcome(pEnv);
    }

    Shutdown_FreePadArray(pEnv->appSpriteSet, PUFO_SHUTDOWN_SPRITE_SET); /* 0x3E158 */
    Shutdown_FreePadArray(pEnv->appPadLate, PUFO_SHUTDOWN_PAD_LATE);
    Shutdown_FreePadArray(pEnv->appMapPads, PUFO_SHUTDOWN_MAP_PADS);     /* 0x3E294 */
    Shutdown_FreePadArray(pEnv->appPadTail, PUFO_SHUTDOWN_PAD_TAIL);

    Shutdown_FreeCells(pEnv);                                          /* 0x3E328 */

    free(*pEnv->appLoftemps);      /* 0x3E3D8 */
    *pEnv->appLoftemps = NULL;
    free(*pEnv->appUnitloft);      /* 0x3E3E4 */
    *pEnv->appUnitloft = NULL;
    free(*pEnv->appMapTactical);   /* 0x3E3F0 */
    *pEnv->appMapTactical = NULL;
    free(*pEnv->appMapCtx);        /* 0x3E400 */
    *pEnv->appMapCtx = NULL;
}

/* ========================================================================== the four tallies
 * See pufo_tactend.h for the decode, the debrief line each counter feeds, and the tile facility
 * byte that drives the first of them. */

/* The cell grid: 3600 slots per layer, row stride 60. ARM 0x33DC4 and 0x33F50 both build the index
 * with two MULs by 60 (R11 = 0x3C at 0x33D88). */
#define TALLY_CELLS_PER_LAYER 3600
#define TALLY_CELLS_PER_ROW   60

static MapCell *Tally_Cell(MapCell *const *apCells, i32 nLayer, i32 nY, i32 nX)
{
    return apCells[TALLY_CELLS_PER_LAYER * nLayer + TALLY_CELLS_PER_ROW * nY + nX];
}

static bool Tally_bInBox(const MapCell *pCell, i32 nX0, i32 nY0, i32 nX1, i32 nY1)
{
    return pCell->nX >= nX0 && pCell->nX <= nX1 && pCell->nY >= nY0 && pCell->nY <= nY1;
}

/* The four sprite slots of one cell, in the order the original tests them (0x335C8: v14 == 0 takes
 * pFloorSpr, switch cases 1..3 take the wall pair and the ceiling). */
static const Mc1Rec *Tally_CellSprite(const MapCell *pCell, i32 nSlot)
{
    switch (nSlot) {
        case 0:  return pCell->pFloorSpr;
        case 1:  return pCell->pWallSprA;
        case 2:  return pCell->pWallSprB;
        default: return pCell->pCeilSpr;
    }
}

/* Tact_TallyBattleStats @ 0x3348c. */
void Tact_TallyBattleStats(const TactTileTallyEnv *pEnv, TactBattleStats *pStats)
{
    i32 nLayer;
    i32 nY;
    i32 nX;

    /* 0x33490..0x334B0: all ten counters are zeroed before the gate, so a gated-out mission still
     * gets a clean set. */
    memset(pStats, 0, sizeof(*pStats));

    /* 0x334B4: the walk runs only with unk_128A8C set and the alternate outcome path clear. */
    if (*pEnv->pbAltOutcomePath != 0 || *pEnv->pbMissionAborted == 0) {
        return;
    }

    for (nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (nY = 0; nY < 10 * (i32)*pEnv->pnMapDimY; ++nY) {
            for (nX = 0; nX < 10 * (i32)*pEnv->pnMapDimX; ++nX) {
                const MapCell *pCell = Tally_Cell(pEnv->apCells, nLayer, nY, nX);
                i32 nSlot;

                for (nSlot = 0; nSlot < 4; ++nSlot) {
                    const Mc1Rec *pSpr = Tally_CellSprite(pCell, nSlot);

                    if (pSpr == NULL) {
                        continue;
                    }
                    switch (pSpr->bFacilityKind) {
                        case TILE_FAC_ELERIUM:                            /* 0x33624 */
                            ++pStats->nPowerSource;
                            pStats->nElerium += *pEnv->pnEleriumRoll;
                            break;
                        case TILE_FAC_NAVIGATION:   ++pStats->nNavigation;   break;
                        case TILE_FAC_REPRODUCTION: ++pStats->nReproduction; break;
                        case TILE_FAC_ENTERTAINMENT:++pStats->nEntertainment;break;
                        case TILE_FAC_SURGERY:      ++pStats->nSurgery;      break;
                        case TILE_FAC_EXAMINATION:  ++pStats->nExamination;  break;
                        case TILE_FAC_ALLOYS:       ++pStats->nAlloys;       break;
                        default: break;                                   /* 0, 1, 12, 13, 14 */
                    }
                }
            }
        }
    }

    /* 0x3373C: the food line is a seeded roll, not a tile count. */
    if (*pEnv->pnTerrainVariant > 3u || *pEnv->pnMissionType == PUFO_MISSION_TYPE_ALIEN_BASE) {
        pStats->nFood = *pEnv->pnFoodRoll;
    }
}

/* The counting rule behind the debrief's "Operatives killed" and "Tanks Destroyed" lines: a unit is
 * lost when it is dead, or when unk_128A8C is clear and the alternate path is clear, or when the
 * alternate path is set and the unit is outside the camera box. ARM 0x33B50..0x33B98. */
static bool UnitTally_bUnitLost(const Soldier *pUnit, u8 bAborted, u8 bAlt)
{
    return pUnit->uHp == 0
        || (bAborted == 0 && bAlt == 0)
        || (bAlt != 0 && pUnit->bInZone == 0);
}

/* Tact_TallyUnitResults @ 0x33780. */
void Tact_TallyUnitResults(const TactUnitTallyEnv *pEnv, i32 nX0, i32 nY0, i32 nX1, i32 nY1,
                           TactUnitResults *pRes)
{
    const u8 bAborted = *pEnv->pbMissionAborted;
    const u8 bAlt = *pEnv->pbAltOutcomePath;
    const i32 nCiv = *pEnv->pnCivilianCount;
    const i32 nXcom = (i32)*pEnv->pnXcomCount;
    i32 i;

    pRes->nOperativesLost   = 0;
    pRes->nTanksLost        = 0;
    pRes->nCiviliansKilled  = 0;
    pRes->nCiviliansRescued = 0;
    pRes->nAliensKilled     = 0;

    /* 0x337E0: the whole geoscape roster loses its zone mark first. */
    for (i = 0; i < (i32)*pEnv->pnSoldierCount; ++i) {
        pEnv->apSoldiers[i]->bInZone = 0;
    }

    /* 0x3381C: the Xcom roster. The mark is cleared per unit even when the gate below fails. */
    for (i = 0; i < nXcom; ++i) {
        Soldier *pUnit = pEnv->apXcomUnits[i];

        pUnit->bInZone = 0;
        if (bAborted == 0 && bAlt == 0) {          /* 0x33870 */
            continue;
        }
        if (pUnit->nUnitType > PUFO_UNITTYPE_HUMANOID_MAX) {
            continue;
        }
        if (!Tally_bInBox(pUnit->pCell, nX0, nY0, nX1, nY1)) {
            continue;
        }
        pUnit->bInZone = 1;
        ++pUnit->uMissions;                        /* 0x338C4 */
    }

    /* 0x338F0: the civilian roster, the same pass but a live unit only. */
    for (i = 0; i < nCiv; ++i) {
        Soldier *pUnit = pEnv->apCivilianUnits[i];

        pUnit->bInZone = 0;
        if (bAborted == 0 && bAlt == 0) {          /* 0x33940 */
            continue;
        }
        if (pUnit->uHp == 0 || pUnit->nUnitType > PUFO_UNITTYPE_HUMANOID_MAX) {
            continue;
        }
        if (!Tally_bInBox(pUnit->pCell, nX0, nY0, nX1, nY1)) {
            continue;
        }
        pUnit->bInZone = 1;
        ++pUnit->uMissions;                        /* 0x339A0 */
    }

    /* 0x339CC: one walk over the three rosters in the order Xcom, enemy, civilian. The enemy roster
     * is never marked, so an enemy's bInZone is whatever the earlier passes left on it. */
    for (i = 0; i < nXcom + (i32)*pEnv->pnEnemyCount + nCiv; ++i) {
        const Soldier *pUnit;

        if (i < nXcom) {
            pUnit = pEnv->apXcomUnits[i];
        } else if (i < nXcom + (i32)*pEnv->pnEnemyCount) {
            pUnit = pEnv->apEnemyUnits[i - nXcom];
        } else {
            pUnit = pEnv->apCivilianUnits[i - nXcom - (i32)*pEnv->pnEnemyCount];
        }

        if (pUnit->nUnitType <= 3u) {                                   /* 0x33A68 */
            if (UnitTally_bUnitLost(pUnit, bAborted, bAlt)) {
                ++pRes->nOperativesLost;
            }
        } else if (pUnit->nUnitType <= PUFO_UNITTYPE_HUMANOID_MAX) {   /* 0x33A70 */
            if (UnitTally_bUnitLost(pUnit, bAborted, bAlt)) {
                ++pRes->nTanksLost;
            }
        } else if (pUnit->nUnitType <= 0xFu || pUnit->nUnitType > 0x11u) { /* 0x33A80: the aliens */
            if (pUnit->uHp == 0) {
                ++pRes->nAliensKilled;
            }
        } else if (pUnit->uHp != 0 && bAborted != 0 && bAlt == 0) {    /* 0x33AB4 */
            ++pRes->nCiviliansRescued;
        } else {
            ++pRes->nCiviliansKilled;                                   /* 0x33AD0 */
        }
    }
}

/* The apObjects slot each alien unit is credited with, exactly the slots the original's switch picks
 * out of the Map slot block at Map+0x23C..Map+0x2AC, Map+0x2D0 and Map+0x348..Map+0x358. Row order
 * is the unit type (9, 0xA, 0xB, 0xC, 0xD); column order is bRankType 2, 3, 4, 5, 6, 7, 8. A -1 is
 * a rank the race has no record for: the debrief score still rises, no object is credited. The
 * Muton row has no rank 4 or 8 because the shipped Muton block (92..94) has no leader or commander. */
static const i16 s_anAlienRankSlot[5][7] = {
    /* UNIT_TYPE_SECTOID  */ {   77,  79,  80,  78,  -1, 148,  81 },
    /* UNIT_TYPE_MUTON    */ {   92,  94,  -1,  93,  -1, 145,  -1 },
    /* UNIT_TYPE_FLOATER  */ {   87,  89,  90,  88,  -1, 146,  91 },
    /* UNIT_TYPE_SNAKEMAN */ {   82,  84,  85,  83,  -1, 144,  86 },
    /* UNIT_TYPE_ETHEREAL */ {   95, 114,  97,  96,  -1, 147,  98 }
};

/* Types 0xE..0x15 credit one slot whatever the rank. The two -1 entries are 0x10 and 0x11, the
 * civilians: the original's switch has no case for them at all, so they get neither credit nor score. */
static const i16 s_anAlienSingletonSlot[8] = { 100, 103, -1, -1, 99, 102, 101, 104 };

#define ALIEN_SLOT_NO_OBJECT  ((i16)-1) /* score only */
#define ALIEN_SLOT_NOT_ALIEN  ((i16)-2) /* neither score nor object */

static i16 ItemTally_AlienSlot(u8 nType, u8 nRank)
{
    if (nType >= (u8)UNIT_TYPE_SECTOID && nType <= (u8)UNIT_TYPE_ETHEREAL) {
        if (nRank < (u8)UNIT_RANK_SOLDIER || nRank > (u8)UNIT_RANK_COMMANDER) {
            return ALIEN_SLOT_NO_OBJECT;
        }
        return s_anAlienRankSlot[nType - (u8)UNIT_TYPE_SECTOID][nRank - (u8)UNIT_RANK_SOLDIER];
    }
    if (nType == (u8)UNIT_TYPE_CHRYSSALID || nType == (u8)UNIT_TYPE_CELACID
        || (nType >= (u8)UNIT_TYPE_CYBERDISK && nType <= (u8)UNIT_TYPE_SECTOPOD)) {
        return s_anAlienSingletonSlot[nType - (u8)UNIT_TYPE_CHRYSSALID];
    }
    return ALIEN_SLOT_NOT_ALIEN;
}

/* One live HWP inside the box: its weapon object is recovered, and its loaded clip goes into the
 * craft cargo column that weapon's cargo row owns. Type 6 is the Tank laser, whose bAmmoCap is 255,
 * so it has no cargo row and no column. */
static void ItemTally_CreditHwp(const TactItemTallyEnv *pEnv, const Soldier *pUnit,
                                i32 nObjSlot, i32 nCargoCol)
{
    ++pEnv->apObjects[nObjSlot]->uRecovCount;

    if (nCargoCol < 0) {
        return;
    }
    if (pUnit->pHandL != NULL) { /* deviation: the original dereferences pHandL with no test */
        pEnv->apCraft[*pEnv->pnCraftIndex]->anCargoQty[nCargoCol] += pUnit->pHandL->nAmmo;
    }
}

/* Tact_TallyItemRecovery @ 0x33c80. */
void Tact_TallyItemRecovery(const TactItemTallyEnv *pEnv, i32 nX0, i32 nY0, i32 nX1, i32 nY1,
                            i32 *pnScore)
{
    const u8 bAborted = *pEnv->pbMissionAborted;
    const u8 bAlt = *pEnv->pbAltOutcomePath;
    const i32 nObjCount = *pEnv->pnObjectCount;
    const i32 nCiv = *pEnv->pnCivilianCount;
    const i32 nXcom = (i32)*pEnv->pnXcomCount;
    i32 i;

    *pnScore = 0;

    /* 0x33CA8: every object starts with no recovered count. */
    for (i = 0; i < nObjCount; ++i) {
        pEnv->apObjects[i]->uRecovCount = 0;
    }

    /* 0x33CEC then 0x33D34: every carried item of every enemy, then of the Xcom team, lands on the
     * unit's cell. Large units keep their two hand slots, which is what the HWP pass below reads. */
    for (i = 0; i < (i32)*pEnv->pnEnemyCount; ++i) {
        Unit_DropAllItemsToCell(pEnv->apEnemyUnits[i]);
    }
    for (i = 0; i < nXcom; ++i) {
        Unit_DropAllItemsToCell(pEnv->apXcomUnits[i]);
    }

    /* 0x33D74: the four layers, the box only, and the upper bound is EXCLUSIVE here (0x33EB8 and
     * 0x33EC4 are CMP/BLT), unlike the inclusive box tests everywhere else in this family. */
    for (i = 0; i < PUFO_MAP_LAYERS; ++i) {
        i32 nY;
        i32 nX;

        for (nY = nY0; nY < nY1; ++nY) {
            for (nX = nX0; nX < nX1; ++nX) {
                TactItem *pItem;

                for (pItem = Tally_Cell(pEnv->apCells, i, nY, nX)->pItemList;
                     pItem != NULL; pItem = pItem->pNext) {
                    ObjectRec *pObj = pItem->pObj;

                    switch (pObj->bTwoHanded) {
                        case 0: {
                            /* 0x33E5C: a single-handed weapon counts only if it is carrying a full
                             * clip, and the clip object itself is credited separately. */
                            u8 nAmmo = pItem->nAmmo;

                            if (nAmmo != PUFO_AMMO_CAP_UNLIMITED && nAmmo != 0) {
                                i32 nClip = pItem->nIndex;

                                if (nClip < nObjCount) { /* deviation: the original bounds nothing */
                                    ObjectRec *pClip = pEnv->apObjects[nClip];

                                    if (nAmmo == pClip->bAmmoCap) {
                                        ++pClip->uRecovCount;
                                    }
                                }
                            }
                            ++pObj->uRecovCount;
                            break;
                        }
                        case 1: /* 0x33E48: a clip, credited when it is full */
                            if (pItem->nAmmo == pObj->bAmmoCap) {
                                ++pObj->uRecovCount;
                            }
                            break;
                        case 2: /* 0x33E18: an alien corpse, credited when nothing is holding it */
                            if (pObj->uSortKey >= (u16)OBJ_SECTOID_CORPSE
                                && pObj->uSortKey <= (u16)OBJ_SECTOPOD_REMAINS
                                && pItem->pLinkedSoldier == NULL) {
                                ++pObj->uRecovCount;
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }

    /* 0x33F08: civilians first, then the Xcom team. Only the civilians need the cell test: the unit
     * counts when it is alive and one item on its cell is linked back to it. */
    for (i = 0; i < nCiv + nXcom; ++i) {
        Soldier *pUnit;

        if (i >= nCiv) {
            pUnit = pEnv->apXcomUnits[i - nCiv];
        } else {
            MapCell *pCell;
            TactItem *pItem;
            bool bCounted = false;

            pUnit = pEnv->apCivilianUnits[i];
            if (bAborted == 0 && bAlt == 0) {              /* 0x33F78 */
                continue;
            }
            pCell = Tally_Cell(pEnv->apCells, pUnit->pCell->nZ, pUnit->pCell->nY, pUnit->pCell->nX);
            if (!Tally_bInBox(pCell, nX0, nY0, nX1, nY1)) {
                continue;
            }
            for (pItem = pCell->pItemList; pItem != NULL; pItem = pItem->pNext) {
                if (pItem->pLinkedSoldier == pUnit) {
                    bCounted = pUnit->uHp != 0;            /* 0x33FE8: a dead one is skipped */
                    break;
                }
            }
            if (!bCounted) {
                continue;
            }
        }

        {
            i16 nSlot = ItemTally_AlienSlot(pUnit->nUnitType, pUnit->bRankType);

            if (nSlot == ALIEN_SLOT_NOT_ALIEN) {
                continue;
            }
            if (nSlot >= 0) {
                ++pEnv->apObjects[nSlot]->uRecovCount;     /* 0x34264 */
            }
            ++*pnScore;                                    /* 0x34278 */
        }
    }

    /* 0x342A8: the deployed craft starts with an empty cargo hold. */
    for (i = 0; i < PUFO_SHIP_CARGO_COLUMNS; ++i) {
        pEnv->apCraft[*pEnv->pnCraftIndex]->anCargoQty[i] = 0;
    }

    /* 0x342D8: every HWP of the team inside the box. */
    for (i = 0; i < nXcom; ++i) {
        Soldier *pUnit = pEnv->apXcomUnits[i];

        if (pUnit->nUnitType < (u8)UNIT_TYPE_TANK_CANNON
            || pUnit->nUnitType > (u8)UNIT_TYPE_HOVERTANK_BLASTER) {
            continue;
        }
        if (bAborted == 0 && bAlt == 0) {
            return;                                        /* 0x34330: the whole pass aborts */
        }
        if (!Tally_bInBox(pUnit->pCell, nX0, nY0, nX1, nY1)) {
            continue;
        }
        switch (pUnit->nUnitType) {
            case UNIT_TYPE_TANK_CANNON:
                ItemTally_CreditHwp(pEnv, pUnit, OBJ_TANK_CANNON, 0);
                break;
            case UNIT_TYPE_TANK_ROCKET:
                ItemTally_CreditHwp(pEnv, pUnit, OBJ_TANK_ROCKET_LAUNCHER, 1);
                break;
            case UNIT_TYPE_TANK_LASER:
                ItemTally_CreditHwp(pEnv, pUnit, OBJ_TANK_LASER_CANNON, -1);
                break;
            case UNIT_TYPE_HOVERTANK_PLASMA:
                ItemTally_CreditHwp(pEnv, pUnit, OBJ_HOVERTANK_PLASMA, 2);
                break;
            default:
                ItemTally_CreditHwp(pEnv, pUnit, OBJ_HOVERTANK_BLASTER, 3);
                break;
        }
    }
}

/* Tact_TallyCraftRecovery @ 0x344b4. */
void Tact_TallyCraftRecovery(const TactCraftTallyEnv *pEnv, i32 *pbLootRecorded)
{
    const i32 nHomeBase = (i32)*pEnv->pnHomeBase;
    const bool bDefendBase = (*pEnv->pnMissionType == PUFO_MISSION_TYPE_XCOM_BASE);
    i32 anCraftList[PUFO_TALLY_CRAFT_LIST_MAX];
    i32 nListCount = 0;
    i32 i;

    /* 0x344D4: an Xcom base defence spreads the load over every craft home at the base being
     * defended; any other mission has exactly one craft, the one that deployed. */
    if (bDefendBase) {
        for (i = 0; i < *pEnv->pnCraftCount; ++i) {
            if ((i32)pEnv->apCraft[i]->bHomeBase == nHomeBase) {
                if (nListCount < PUFO_TALLY_CRAFT_LIST_MAX) {
                    anCraftList[nListCount++] = i;
                }
            }
        }
    } else {
        anCraftList[0] = *pEnv->pnCraftIndex;
        nListCount = 1;
    }

    /* 0x34554: the tally runs when either outcome flag is set. */
    if (*pEnv->pbMissionAborted == 0 && *pEnv->pbAltOutcomePath == 0) {
        return;
    }

    for (i = 0; i < *pEnv->pnObjectCount; ++i) {
        ObjectRec *pObj = pEnv->apObjects[i];
        i32 k;

        /* 0x34588: each craft that went out already holds a quantity of this object; recovery is
         * capped against it, and what does not fit carries on to the next craft. */
        for (k = 0; k < nListCount; ++k) {
            i32 nCraft = anCraftList[k];
            u16 uCap;

            if (nCraft < 0 || nCraft >= PUFO_ITEM_QTY_COUNT) { /* deviation: unbounded in the original */
                continue;
            }
            uCap = pObj->aItemQty[nCraft];
            if (uCap == 0) {
                continue;
            }
            if (pObj->uRecovCount <= uCap) {
                pObj->aItemQty[nCraft] = pObj->uRecovCount;
                pObj->uRecovCount = 0;
            } else {
                pObj->uRecovCount = (u16)(pObj->uRecovCount - uCap);
            }
        }

        if (bDefendBase) {
            /* 0x3446C: the remainder goes straight into the defended base's own quantity, and
             * uRecovCount is deliberately left in place. */
            if (nHomeBase >= 0 && nHomeBase < (i32)(sizeof(pObj->auPerBaseVal)
                                                    / sizeof(pObj->auPerBaseVal[0]))) {
                pObj->auPerBaseVal[nHomeBase] = (u16)(pObj->auPerBaseVal[nHomeBase]
                                                      + pObj->uRecovCount);
            }
        } else if (pObj->uRecovCount != 0) {
            /* 0x3447C: otherwise the transfer flow's working site record takes it, at the slot AT the
             * site count, indexed by the object index. */
            SmapSite *pSite = &pEnv->aSites[*pEnv->pnSiteCount];

            if (i < (i32)(sizeof(pSite->auItemQty) / sizeof(pSite->auItemQty[0]))) {
                pSite->auItemQty[i] = pObj->uRecovCount;
            }
            pObj->uRecovCount = 0;
            pSite->bHasLoot = 1;
            *pbLootRecorded = 1;
        }
    }
}
