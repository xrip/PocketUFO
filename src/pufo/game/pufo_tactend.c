/* pufo_tactend.c - Tact_Shutdown @ 0x3d568. See pufo_tactend.h for the decode. */
#include "game/pufo_tactend.h"

#include <stdio.h>  /* snprintf, the bounded stand-in for the original's j_itoa */
#include <string.h>

#include "game/pufo_boot.h"    /* PUFO_TEXT_REC_STRIDE, the debrief labels */
#include "game/pufo_geoscape.h"
#include "game/pufo_objectrec.h"
#include "game/pufo_roster.h" /* Soldier_PromoteByTypeScores, SideLists_MarkAllSoldiersSeen */
#include "game/pufo_ship.h"
#include "game/pufo_strateg.h"
#include "game/pufo_tactctl.h" /* Tact_ScanVictoryTiles, Tact_GetCameraBounds, mission types */
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

static void Shutdown_RemoveWorldSoldier(const TactShutdownEnv *pEnv, const Soldier *pUnit)
{
    if (pEnv->apStrategCraftCopy != NULL && pEnv->pnStrategCraftCopy != NULL) {
        *pEnv->pnStrategCraftCopy = Soldier_RemoveById(pEnv->apStrategCraftCopy,
                                                        *pEnv->pnStrategCraftCopy,
                                                        pUnit->nUnk440);
    }
}

/* 0x3DD0C .. 0x3DE10. The side lists select the units, but the original
 * Soldier_RemoveById ignores its first argument and frees from Map.apSoldiers. The side-list
 * counts are re-read and the index advances; neither side list shrinks here because Tact_Shutdown
 * frees the Map storage later. Civilians outside the zone are removed, all enemies are removed,
 * and XCOM units of type 4..8 or outside the zone are removed. */
static void Shutdown_RemoveUnits(const TactShutdownEnv *pEnv)
{
    i32 i;

    for (i = 0; i < *pEnv->pnCivilianCount; ++i) {          /* 0x3DD10 */
        Soldier *pUnit = (Soldier *)pEnv->apCivilianUnits[i];

        if (pUnit->bInZone == 0) {                         /* 0x3DD28 */
            Shutdown_RemoveWorldSoldier(pEnv, pUnit);      /* 0x3DD34 */
        }
    }

    for (i = 0; i < *pEnv->pnEnemyCount; ++i) {             /* 0x3DD6C, bound re-read */
        Soldier *pUnit = (Soldier *)pEnv->apEnemyUnits[i];

        Shutdown_RemoveWorldSoldier(pEnv, pUnit);
    }

    for (i = 0; i < *pEnv->pnXcomCount; ++i) {              /* 0x3DDC8 */
        Soldier *pUnit = (Soldier *)pEnv->apXcomUnits[i];
        u8 nType = pUnit->nUnitType;

        if ((nType > 3 && nType <= 8) || pUnit->bInZone == 0) {
            Shutdown_RemoveWorldSoldier(pEnv, pUnit);
        }
    }
}

/* 0x3DE24 .. 0x3E058. The mission outcome decision table. Map_bMissionMode 0, 2 and 3
 * mean the craft leaves; 1 means the site is held. Map_bMissionVictory @ Map+0x150D4 says
 * whether the mission was won: on a held site it transfers the loot or demolishes the
 * facility (0x3E00C / 0x3E058), on a leaving craft it loses the craft itself (0x3DFDC). */
static void Shutdown_MissionOutcome(const TactShutdownEnv *pEnv)
{
    /* 0x159E74 is an apCraft INDEX, not a strateg index: 0x3DE80/0x3DF5C/0x3DF88 all load it,
     * index Map.apCraft with it (offset 0x10AC) and only then take Ship.nStrategIdx @ +0x38,
     * which is what StrategList_RemoveAt actually receives. */
    i32 nShipIdx = *pEnv->pnMsgShipIdx;
    u8 nMode = *pEnv->pbMissionMode;
    Ship *pCraft;
    Ship *pStrategCraft;

    if (*pEnv->pbAlternateOutcomePath != 0) {
        if (nMode == 1) {
            BaseRec *pBase = (BaseRec *)pEnv->apBaseCopy[(i8)*pEnv->pbFacilityType];

            Base_DemolishFacility(pEnv->pDemolishEnv, pBase->bUnk4); /* 0x3DE60 */
            nMode = *pEnv->pbMissionMode;
        }
        pStrategCraft = (Ship *)pEnv->apCraftCopy[nShipIdx];         /* 0x3DE80 .. 0x3DE8C */
        if (nMode == 0 && ((StrategItem *)PtrArray_Get(pEnv->pStrategList,
                                                       pStrategCraft->nStrategIdx))->nType == 4) {
            StrategList_RemoveAt(pEnv->pStrategList, pEnv->pShipEnv->pWorld,
                                 pStrategCraft->nStrategIdx,         /* 0x3DEB8 LDR R1,[R3,#0x38] */
                                 pEnv->pbSpawnOnceFlag, pEnv->pShipEnv->pSpawnCtx);
        }
        return;
    }

    if (nMode == 0 || nMode == 3 || nMode == 2) {
        *pEnv->pbSpawnOnceFlag = 1;                        /* 0x3DF48 */
        if (*pEnv->pbMissionVictory != 0) {
            pStrategCraft = (Ship *)pEnv->apCraftCopy[nShipIdx];    /* 0x3DF5C .. 0x3DF68 */
            StrategList_RemoveAt(pEnv->pStrategList, pEnv->pShipEnv->pWorld,
                                 pStrategCraft->nStrategIdx,        /* 0x3DF6C LDR R1,[R3,#0x38] */
                                 pEnv->pbSpawnOnceFlag, pEnv->pShipEnv->pSpawnCtx);
        } else {
            pStrategCraft = (Ship *)pEnv->apCraftCopy[nShipIdx];    /* 0x3DF88 .. 0x3DF94 */
            if (((StrategItem *)PtrArray_Get(pEnv->pStrategList,
                                             pStrategCraft->nStrategIdx))->nType == 4) {
                StrategList_RemoveAt(pEnv->pStrategList, pEnv->pShipEnv->pWorld,
                                     pStrategCraft->nStrategIdx,    /* 0x3DFBC LDR R1,[R3,#0x38] */
                                     pEnv->pbSpawnOnceFlag, pEnv->pShipEnv->pSpawnCtx);
            }
            pCraft = (Ship *)pEnv->apCraftCopy[*pEnv->pnCraftIndex];
            Ship_RemoveWithCargo(pEnv->pShipEnv, pCraft->bId); /* 0x3DFDC */
        }
    }
    if (nMode == 1 && *pEnv->pbMissionVictory != 0) {
        Base_TransferTacticalLoot(pEnv->pLootEnv, true);   /* 0x3E00C */
    }
    if (nMode == 1 && *pEnv->pbMissionVictory == 0) {
        BaseRec *pBase = (BaseRec *)pEnv->apBaseCopy[(i8)*pEnv->pbFacilityType];

        Base_DemolishFacility(pEnv->pDemolishEnv, pBase->bUnk4); /* 0x3E058 */
    }
}

void Tact_ShutdownOutcome(const TactShutdownEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }

    Shutdown_RemoveUnits(pEnv);
    Shutdown_MissionOutcome(pEnv);
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
    *pEnv->pbTactFlag = 0;     /* 0x3D588: Map+0x00 */

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
        Shutdown_FreeRoster((void **)pEnv->apStrategCraftCopy, *pEnv->pnStrategCraftCopy);
        if (*pEnv->pnBaseCount > 0) {
            Shutdown_FreeRoster(pEnv->apBaseCopy, (i32)*pEnv->pnBaseCount);
        }
        if (*pEnv->pnCraftCount != 0) {
            Shutdown_FreeRoster(pEnv->apCraftCopy, (i32)*pEnv->pnCraftCount);
        }
        PtrArray_DeleteAll(pEnv->pStrategList);                      /* 0x3E154 */
    } else {
        Tact_ShutdownOutcome(pEnv);
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

    /* 0x334B4: the walk runs only with Map_bMissionVictory set and the alternate outcome path clear,
     * i.e. only after a mission that was won and not aborted. */
    if (*pEnv->pbAltOutcomePath != 0 || *pEnv->pbMissionVictory == 0) {
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
 * lost when it is dead, or when Map_bMissionVictory is clear and the alternate path is clear (the
 * mission was simply lost, so everyone is), or when the alternate path is set and the unit is
 * outside the camera box (aborted, so whoever missed the extraction box is lost).
 * ARM 0x33B50..0x33B98. */
static bool UnitTally_bUnitLost(const Soldier *pUnit, u8 bVictory, u8 bAlt)
{
    return pUnit->uHp == 0
        || (bVictory == 0 && bAlt == 0)
        || (bAlt != 0 && pUnit->bInZone == 0);
}

/* Tact_TallyUnitResults @ 0x33780. */
void Tact_TallyUnitResults(const TactUnitTallyEnv *pEnv, i32 nX0, i32 nY0, i32 nX1, i32 nY1,
                           TactUnitResults *pRes)
{
    const u8 bVictory = *pEnv->pbMissionVictory;
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
        if (bVictory == 0 && bAlt == 0) {          /* 0x33870 */
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
        if (bVictory == 0 && bAlt == 0) {          /* 0x33940 */
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
            if (UnitTally_bUnitLost(pUnit, bVictory, bAlt)) {
                ++pRes->nOperativesLost;
            }
        } else if (pUnit->nUnitType <= PUFO_UNITTYPE_HUMANOID_MAX) {   /* 0x33A70 */
            if (UnitTally_bUnitLost(pUnit, bVictory, bAlt)) {
                ++pRes->nTanksLost;
            }
        } else if (pUnit->nUnitType <= 0xFu || pUnit->nUnitType > 0x11u) { /* 0x33A80: the aliens */
            if (pUnit->uHp == 0) {
                ++pRes->nAliensKilled;
            }
        } else if (pUnit->uHp != 0 && bVictory != 0 && bAlt == 0) {    /* 0x33AB4 */
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
    const u8 bVictory = *pEnv->pbMissionVictory;
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
            if (bVictory == 0 && bAlt == 0) {              /* 0x33F78 */
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
        if (bVictory == 0 && bAlt == 0) {
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
    if (*pEnv->pbMissionVictory == 0 && *pEnv->pbAltOutcomePath == 0) {
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

/* --- Tact_ClearSoldierBattleFlags @ 0x346e0 -------------------------------- */

/* g_aClassStatGrowth @ 0xAA930 (was byte_AA930): six soldier classes x nine growth weights,
 * values 1..3. Tact_ClearSoldierBattleFlags is its only reader in the binary, and the class
 * index is Soldier.nClass, which Unit_ClassFromStats bounds to 0..5. Byte-checked against the
 * image by docs/parity/audit_tables.py. */
const u8 g_aClassStatGrowth[PUFO_SOLDIER_CLASS_COUNT * PUFO_CLASS_STAT_COUNT] = {
    /* class 0 */ 3, 3, 2, 2, 2, 3, 1, 3, 3,
    /* class 1 */ 2, 2, 1, 3, 3, 1, 1, 2, 2,
    /* class 2 */ 2, 1, 3, 2, 1, 1, 3, 1, 2,
    /* class 3 */ 3, 2, 2, 3, 2, 3, 1, 2, 3,
    /* class 4 */ 2, 2, 2, 3, 2, 1, 2, 2, 2,
    /* class 5 */ 3, 2, 2, 2, 2, 2, 2, 2, 3
};

/* The (class column, auStat index, auStatBase index, per-mission rate) quadruples, in the order
 * the ARM emits them. Each rate is a literal double written as two dword halves, e.g. at 0x347D8:
 * `LDR R2,=0x22222222` / `LDR R3,=0x3FEA2222`. Class column 6 is never read and auStat[6] and
 * auStat[7] are never recomputed.
 *
 * The base index is not always the stat index, and the ARM spells the difference out in operand
 * offsets: the class-8 rule reads `LDRH R0,[R8,#auStatBase+0x10]` (auStatBase[8]) and stores
 * `STRH R0,[R8,#auStat+0x12]` (auStat[9]); the class-7 rule reads `[R8,#0xF2]` (auStatBase[7])
 * and stores `[R8,#0xB2]` (auStat[8]). That is exactly the compaction Unit_ResetBattleState
 * writes -- auStatBase[7] holds auStat[8] and auStatBase[8] holds auStat[9] -- so each rule reads
 * the base of the stat it is growing. auStatBase has nine entries, so the class-8 rule reading
 * index 9 would run off the end of the array. */
typedef struct StatGrowthRule {
    u8     nClassCol;
    u8     nStatIdx;
    u8     nBaseIdx;
    double dRate;
} StatGrowthRule;

static const StatGrowthRule s_aStatGrowth[8] = {
    { 0, 0, 0, 0.8166666666666667  }, /* 0x347D8 */
    { 8, 9, 8, 0.31666666666666665 }, /* 0x348AC */
    { 1, 1, 1, 0.4                   }, /* 0x34988 */
    { 2, 2, 2, 0.3833333333333333  }, /* 0x34A54 */
    { 3, 3, 3, 0.35                  }, /* 0x34B20 */
    { 4, 4, 4, 0.31666666666666665 }, /* 0x34BEA */
    { 5, 5, 5, 0.2833333333333333  }, /* 0x34CC8 */
    { 7, 8, 7, 0.4                   }  /* 0x34D98 */
};

#define STAT_GROWTH_RULE_COUNT ((i32)(sizeof(s_aStatGrowth) / sizeof(s_aStatGrowth[0])))
#define RANK_NAME_STRIDE 20 /* aszTextE @ 0xB2200, six 20-byte Xcom rank names */

/* The original's byte copy, terminator included (0x34E34, 0x34E74). Both destinations are
 * 50-byte line buffers (PUFO_PANEL_LINE_BYTES) and both sources are shorter than that, so no
 * bound is needed here. */
static void ClearFlags_CopyLine(UiPanel *pPanel, i32 nLine, const char *pszSrc)
{
    u8 *pDst = pPanel->apLines[nLine];
    i32 i = 0;

    for (;;) {
        pDst[i] = (u8)pszSrc[i];
        if (pszSrc[i] == 0) {
            break;
        }
        ++i;
    }
}

/* Tact_ClearSoldierBattleFlags @ 0x346e0. */
void Tact_ClearSoldierBattleFlags(const TactBattleFlagsEnv *pEnv)
{
    Soldier **apSoldiers = pEnv->apSoldiers;
    const i32 nCount = (i32)*pEnv->pnSoldierCount;
    i32 nLine = 0;
    i32 i;

    for (i = 0; i < nCount; ++i) {
        apSoldiers[i]->bRankDirty = 0;                        /* 0x34724 */
    }

    {
        SoldierPromoteEnv promote;

        promote.apSoldiers = apSoldiers;
        promote.pnSoldierCount = pEnv->pnSoldierCount;
        Soldier_PromoteByTypeScores(&promote);                /* 0x3473C */
    }

    for (i = 0; i < nCount; ++i) {
        Soldier *pUnit = apSoldiers[i];
        const u8 *pClass;

        if (pUnit->bInZone == 0) {
            continue;
        }
        if (!Unit_bIsOperative(pUnit)) {
            continue;
        }
        if (pUnit->nClass < 0 || pUnit->nClass >= PUFO_SOLDIER_CLASS_COUNT) {
            /* Deviation: the original indexes g_aClassStatGrowth with no test. Unit_ClassFromStats
             * only ever produces 0..5, so this branch is unreachable in practice. */
            continue;
        }
        pClass = &g_aClassStatGrowth[PUFO_CLASS_STAT_COUNT * pUnit->nClass];
        for (i32 r = 0; r < STAT_GROWTH_RULE_COUNT; ++r) {
            const StatGrowthRule *pRule = &s_aStatGrowth[r];
            double dValue = (double)pClass[pRule->nClassCol] * pRule->dRate
                          * (double)pUnit->uMissions
                          + (double)pUnit->auStatBase[pRule->nBaseIdx];

            pUnit->auStat[pRule->nStatIdx] = (u16)dValue;
        }
        if (pUnit->bRankDirty == 0) {
            continue;
        }
        ClearFlags_CopyLine(pEnv->pPanelName, nLine, pUnit->szName);                  /* 0x34E34 */
        ClearFlags_CopyLine(pEnv->pPanelRank, nLine,
                            &pEnv->panRankName[RANK_NAME_STRIDE * pUnit->bRankType]); /* 0x34E74 */
        pEnv->pPanelName->nLineCount += 1;                                            /* 0x34EAC */
        pEnv->pPanelRank->nLineCount += 1;                                            /* 0x34EC0 */
        ++nLine;
        *pEnv->pbPromotionReport = 1;                                                 /* 0x34EDC */
    }

    for (i = 0; i < nCount; ++i) {
        apSoldiers[i]->bRankDirty = 0;                        /* 0x34F9C */
    }
}

/* --- Tact_CheckBattleEnd @ 0x34fbc ----------------------------------------- */

/* Map_szMissionCaption @ Map+0x15094 and Map_szMissionScore @ Map+0x150B2 sit 30 bytes apart in
 * the Map block (0x150B2 - 0x15094, and 0x150D0 - 0x150B2 -- Map_nDebriefLine is the next field),
 * so each debrief string has 30 bytes. The original's copies are terminator-inclusive walks with
 * no bound; the score is snprintf'd because itoa of an int cannot exceed that. */
#define PUFO_DEBRIEF_TEXT_BYTES 30

/* The original's terminator-inclusive byte copy: the caption (0x353D8, 0x35414) and every debrief
 * label (0x35450, 0x3550C, ...). */
static void Debrief_CopyText(char *pszDst, const char *pszSrc)
{
    i32 i = 0;

    for (;;) {
        pszDst[i] = pszSrc[i];
        if (pszSrc[i] == 0) {
            break;
        }
        ++i;
    }
}

/* The three UiPanel_SetLineMeta colour shapes the debrief applies -- to the label column and the
 * value column alike. Red for a loss line (0x35488), green for a gain line (0x35608), and the loot
 * lines' third shape, which is the only one whose third meta byte is non-zero (0x35B98). */
typedef enum DebriefMeta {
    DEBRIEF_META_LOSS = 0, /* 255, 0, 0, 90, 0, 0 */
    DEBRIEF_META_GAIN = 1, /* 0, 255, 0, 0, 90, 0 */
    DEBRIEF_META_LOOT = 2  /* 0, 255, 255, 0, 90, 90 */
} DebriefMeta;

static void Debrief_SetMeta(UiPanel *pPanel, i32 nLine, DebriefMeta meta)
{
    switch (meta) {
    case DEBRIEF_META_GAIN:
        UiPanel_SetLineMeta(pPanel, nLine, 0, 255, 0, 0, 90, 0);
        break;
    case DEBRIEF_META_LOOT:
        UiPanel_SetLineMeta(pPanel, nLine, 0, 255, 0xFF, 0, 90, 90);
        break;
    default:
        UiPanel_SetLineMeta(pPanel, nLine, 255, 0, 0, 90, 0, 0);
        break;
    }
}

/* One debrief line: the TEXTS label into the label column, the value into the value column's
 * anLineAux, and the same colour shape on both panels. */
static void Debrief_EmitLine(const TactBattleEndEnv *pEnv, i32 nLine, i32 nTextRec,
                             i32 nValue, DebriefMeta meta)
{
    Debrief_CopyText((char *)pEnv->pPanelLabel->apLines[nLine],
                     pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * nTextRec);
    Debrief_SetMeta(pEnv->pPanelLabel, nLine, meta);
    UiPanel_SetLineAux(pEnv->pPanelValue, nLine, nValue);
    Debrief_SetMeta(pEnv->pPanelValue, nLine, meta);
}

/* Emit a line, add its score term, advance the cursor. The original advances Map_nDebriefLine
 * through a pointer, so the cursor is written on every line rather than once at the end. */
#define DEBRIEF_LINE(nTextRec, nValue, nTerm, meta)                                       \
    do {                                                                                  \
        Debrief_EmitLine(pEnv, *pnLine, (nTextRec), (nValue), (meta));                    \
        nScore += (nTerm);                                                                \
        *pnLine += 1;                                                                     \
    } while (0)

void Tact_CheckBattleEnd(const TactBattleEndEnv *pEnv, u8 bAbort)
{
    TactBattleStats stats;
    TactUnitResults units;
    i32 nX0 = 0, nY0 = 0, nX1 = 0, nY1 = 0;
    i32 nItemScore = 0;
    i32 nLootRecorded = 0;
    i32 nScore = 0;
    i32 nLiveEnemy = 0;
    i32 nLive = 0;
    i32 i;
    u8 bLocalAbort = bAbort;
    u8 bEndScreen = 0;
    i32 *pnLine;
    SmapSite *pSite;

    *pEnv->pbAltOutcomePath = bAbort;                            /* 0x34FE4 */

    if (*pEnv->pbBattleEnded != 0) {                             /* 0x34FEC */
        return;
    }

    if (Tact_ScanVictoryTiles(*pEnv->pbSpawnAlt != 0, *pEnv->pnMissionType,
                              (i32)*pEnv->pnSpawnTileX, (i32)*pEnv->pnSpawnTileY,
                              pEnv->hooks.pfnCellHasNoItems, pEnv->hooks.pUser)) { /* 0x35004 */
        pEnv->hooks.pfnShutdown(1, pEnv->hooks.pUser);           /* 0x35014 */
        pEnv->hooks.pfnEndScreenInit(1, pEnv->hooks.pUser);      /* 0x3501C */
        *pEnv->pbTickHold = 1;                                   /* 0x35028 */
        *pEnv->pbBattleEnded = 1;                                /* 0x3502C */
        return;
    }

    *pEnv->pbTickHold = 0;                                       /* 0x3503C */

    if (bAbort != 0) {
        *pEnv->pnMissionOutcome = 2;                             /* 0x3510C */
        *pEnv->pbTickHold = 1;                                   /* 0x35114 */
    } else {
        if (*pEnv->pnXcomCount == 0) {
            *pEnv->pbMissionVictory = 0;                         /* 0x35070 */
            *pEnv->pbTickHold = 1;                               /* 0x35078 */
            *pEnv->pnMissionOutcome = 1;                         /* 0x3507C */
        }
        while (nLiveEnemy < (i32)*pEnv->pnEnemyCount) {         /* 0x35088 */
            i32 nType = pEnv->apEnemyUnits[nLiveEnemy]->nUnitType;

            if (nType != UNIT_TYPE_CIVILIAN && nType != UNIT_TYPE_CIVILIAN_B) {
                ++nLive;
            }
            ++nLiveEnemy;
        }
        if (*pEnv->pnEnemyReserve + nLive == 0 &&
            (*pEnv->pnMissionType != PUFO_MISSION_TYPE_ALIEN_BASE || *pEnv->pbSpawnAlt == 0)) {
            *pEnv->pbMissionVictory = 1;                         /* 0x350F0 */
            *pEnv->pnMissionOutcome = 0;                         /* 0x350F4 */
            *pEnv->pbTickHold = 1;                               /* 0x35114 */
        }
    }

    if (*pEnv->pbTickHold == 0) {                                /* 0x35118 */
        return;
    }

    /* The mission cannot be completed: an alien base or the Mars mission with the alternate spawn,
     * or a base defence whose facility grid says the base is already gone. Both arms then need the
     * mission to be lost. The second arm is only reachable when the type is neither 4 nor 9, which
     * is why its `== 3` test is the base-defence one. */
    if (*pEnv->pnMissionType == PUFO_MISSION_TYPE_ALIEN_BASE ||
        *pEnv->pnMissionType == PUFO_MISSION_TYPE_MARS_SPECIAL) {
        bEndScreen = (*pEnv->pbSpawnAlt != 0) ? (*pEnv->pbMissionVictory == 0) : 0; /* 0x3513C */
    } else if (*pEnv->pnFacilityEndFlag == PUFO_FACILITY_END_FLAG_SET &&
               *pEnv->pnMissionType == PUFO_MISSION_TYPE_XCOM_BASE) {
        bEndScreen = (*pEnv->pbMissionVictory == 0);             /* 0x35150 */
    }
    if (bEndScreen) {
        pEnv->hooks.pfnShutdown(1, pEnv->hooks.pUser);           /* 0x3517C */
        pEnv->hooks.pfnEndScreenInit(0, pEnv->hooks.pUser);      /* 0x35184 */
        *pEnv->pbTickHold = 1;                                   /* 0x3518C */
        *pEnv->pbBattleEnded = 1;                                /* 0x35194 */
        return;
    }

    Tact_GetCameraBounds(&pEnv->camera, &nX0, &nY0, &nX1, &nY1);  /* 0x3519C */

    if (bAbort != 0 && *pEnv->pnMissionType == PUFO_MISSION_TYPE_XCOM_BASE) {
        *pEnv->pbMissionVictory = 0;                             /* 0x351CC */
        bLocalAbort = 0;                                         /* 0x351D0: no "Craft Lost" line */
    }

    *pEnv->puUiScreen = PUFO_DEBRIEF_UI_SCREEN;                  /* 0x351EC, a STRH */
    *pEnv->pbBattleEnded = 1;                                    /* 0x351F4 */
    *pEnv->pnBannerFadeTicks = PUFO_BANNER_FADE_TICKS;           /* 0x351FC */
    *pEnv->pbCtxBusy = 0;                                        /* 0x35228 */
    *pEnv->pbEnemyTurnActive = 0;                                /* 0x3523C */

    Tact_TallyBattleStats(pEnv->pTiles, &stats);                 /* 0x35240 */
    Tact_TallyUnitResults(pEnv->pUnits, nX0, nY0, nX1, nY1, &units);  /* 0x35284 */
    SideLists_MarkAllSoldiersSeen(pEnv->flags.apSoldiers,
                                  (i32)*pEnv->flags.pnSoldierCount);   /* 0x352B0 */
    Tact_TallyItemRecovery(pEnv->pItems, nX0, nY0, nX1, nY1, &nItemScore);  /* 0x352BC */
    Tact_TallyCraftRecovery(pEnv->pCraft, &nLootRecorded);       /* 0x352E4 */

    *pEnv->flags.pbPromotionReport = 0;                          /* 0x352CC */
    pEnv->flags.pPanelName->nLineCount = 0;                      /* 0x352C4, pUiPanel2 */
    pEnv->flags.pPanelRank->nLineCount = 0;                      /* 0x352D8, pUiPanel3 */

    for (i = 0; i < (i32)*pEnv->flags.pnSoldierCount; ++i) {     /* 0x3531C */
        Soldier *pUnit = pEnv->flags.apSoldiers[i];

        if ((*pEnv->pbAltOutcomePath != 0 && pUnit->bInZone == 0) ||
            (*pEnv->pbAltOutcomePath == 0 && *pEnv->pbMissionVictory == 0)) {
            pUnit->bFlags |= PUFO_SOLDIER_FLAG_LOST;             /* 0x3535C */
        }
    }

    Tact_ClearSoldierBattleFlags(&pEnv->flags);                  /* 0x35380 */

    pnLine = pEnv->pnDebriefLine;
    *pnLine = 0;                                                 /* 0x3538C */
    Debrief_CopyText(pEnv->pszCaption,
                     pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE *
                         (*pEnv->pbMissionVictory != 0 ? PUFO_DEBRIEF_TEXT_VICTORY
                                                       : PUFO_DEBRIEF_TEXT_DEFEAT)); /* 0x353C4 */

    if (units.nOperativesLost > 0) {
        DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_OPERATIVES, -30 * units.nOperativesLost,
                     -30 * units.nOperativesLost, DEBRIEF_META_LOSS);            /* 0x35428 */
    }
    if (units.nTanksLost > 0) {
        DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_TANKS, -80 * units.nTanksLost,
                     -80 * units.nTanksLost, DEBRIEF_META_LOSS);                 /* 0x35500 */
    }
    if (units.nCiviliansKilled > 0) {
        DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_CIV_KILLED, -30 * units.nCiviliansKilled,
                     -30 * units.nCiviliansKilled, DEBRIEF_META_LOSS);           /* 0x355C4 */
    }
    if (units.nCiviliansRescued > 0 && *pEnv->pbMissionVictory != 0) {
        DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_CIV_RESCUED, 30 * units.nCiviliansRescued,
                     30 * units.nCiviliansRescued, DEBRIEF_META_GAIN);           /* 0x35688 */
    }
    if (units.nAliensKilled > 0 && *pEnv->pbMissionVictory != 0) {
        DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_ALIENS_KILLED, 20 * units.nAliensKilled,
                     20 * units.nAliensKilled, DEBRIEF_META_GAIN);               /* 0x3575C */
    }
    if (nItemScore > 0 && *pEnv->pbMissionVictory != 0) {
        DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_ALIENS_LIVE, 30 * nItemScore,
                     30 * nItemScore, DEBRIEF_META_GAIN);                        /* 0x35830 */
    }

    /* Exactly one of the three base/craft lines can appear. */
    if (*pEnv->pbMissionVictory != 0) {
        if (*pEnv->pnMissionType == PUFO_MISSION_TYPE_ALIEN_BASE) {
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_BASE_DESTROYED, 500, 500, DEBRIEF_META_GAIN); /* 0x35A7C */
        }
    } else if (*pEnv->pnMissionType == PUFO_MISSION_TYPE_XCOM_BASE) {
        DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_BASE_LOST, -700, -700, DEBRIEF_META_LOSS);        /* 0x35928 */
    } else if (bLocalAbort == 0) {
        DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_CRAFT_LOST, -400, -400, DEBRIEF_META_LOSS);       /* 0x359DC */
    }

    /* The nine loot lines. Each writes its count into the working site record and into the
     * object's recovery tally as well as onto the debrief. */
    if (*pEnv->pbMissionVictory != 0) {
        pSite = &pEnv->pCraft->aSites[*pEnv->pnSiteCount];

        if (stats.nAlloys / 8 > 0) {                                                /* 0x35B40 */
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_ALLOYS, stats.nAlloys / 8,
                         stats.nAlloys / 8, DEBRIEF_META_LOOT);
            pSite->auItemQty[PUFO_LOOT_OBJ_ALLOYS] = (u16)(stats.nAlloys / 8);
            nLootRecorded = 1;
            pEnv->pItems->apObjects[PUFO_LOOT_OBJ_ALLOYS]->uRecovCount
                += (u16)(stats.nAlloys / 8);
        }
        if (stats.nElerium > 0) {                                                   /* 0x35C6C */
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_ELERIUM, stats.nElerium,
                         stats.nElerium, DEBRIEF_META_LOOT);
            pSite->auItemQty[PUFO_LOOT_OBJ_ELERIUM] = (u16)stats.nElerium;
            nLootRecorded = 1;
            pEnv->pItems->apObjects[PUFO_LOOT_OBJ_ELERIUM]->uRecovCount
                += (u16)stats.nElerium;
        }
        if (stats.nNavigation > 0) {                                                /* 0x35D70 */
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_NAVIGATION, 15 * stats.nNavigation,
                         15 * stats.nNavigation, DEBRIEF_META_LOOT);
            pSite->auItemQty[PUFO_LOOT_OBJ_NAVIGATION] = (u16)stats.nNavigation;
            nLootRecorded = 1;
            pEnv->pItems->apObjects[PUFO_LOOT_OBJ_NAVIGATION]->uRecovCount
                += (u16)stats.nNavigation;
        }
        if (stats.nPowerSource > 0) {                                               /* 0x35E80 */
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_POWER_SOURCE, 20 * stats.nPowerSource,
                         20 * stats.nPowerSource, DEBRIEF_META_LOOT);
            pSite->auItemQty[PUFO_LOOT_OBJ_POWER_SOURCE] = (u16)stats.nPowerSource;
            nLootRecorded = 1;
            pEnv->pItems->apObjects[PUFO_LOOT_OBJ_POWER_SOURCE]->uRecovCount
                += (u16)stats.nPowerSource;
        }
        if (stats.nReproduction > 0) {                                              /* 0x35F90 */
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_REPRODUCTION, 2 * stats.nReproduction,
                         2 * stats.nReproduction, DEBRIEF_META_LOOT);
            pSite->auItemQty[PUFO_LOOT_OBJ_REPRODUCTION] = (u16)stats.nReproduction;
            nLootRecorded = 1;
            pEnv->pItems->apObjects[PUFO_LOOT_OBJ_REPRODUCTION]->uRecovCount
                += (u16)stats.nReproduction;
        }
        if (stats.nFood > 0) {                                                      /* 0x36090 */
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_FOOD, stats.nFood, stats.nFood, DEBRIEF_META_LOOT);
            pSite->auItemQty[PUFO_LOOT_OBJ_FOOD] = (u16)stats.nFood;
            nLootRecorded = 1;
            pEnv->pItems->apObjects[PUFO_LOOT_OBJ_FOOD]->uRecovCount += (u16)stats.nFood;
        }
        if (stats.nExamination > 0) {                                               /* 0x3618C */
            /* The only line whose value column and score term differ: the column shows the room
             * count (0x36218) and the score pays 10 per room (0x36230). */
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_EXAMINATION, stats.nExamination,
                         10 * stats.nExamination, DEBRIEF_META_LOOT);
            pSite->auItemQty[PUFO_LOOT_OBJ_EXAMINATION] = (u16)stats.nExamination;
            nLootRecorded = 1;
            pEnv->pItems->apObjects[PUFO_LOOT_OBJ_EXAMINATION]->uRecovCount
                += (u16)stats.nExamination;
        }
        if (stats.nEntertainment > 0) {                                             /* 0x36298 */
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_ENTERTAINMENT, 2 * stats.nEntertainment,
                         2 * stats.nEntertainment, DEBRIEF_META_LOOT);
            pSite->auItemQty[PUFO_LOOT_OBJ_ENTERTAINMENT] = (u16)stats.nEntertainment;
            nLootRecorded = 1;
            pEnv->pItems->apObjects[PUFO_LOOT_OBJ_ENTERTAINMENT]->uRecovCount
                += (u16)stats.nEntertainment;
        }
        if (stats.nSurgery > 0) {                                                   /* 0x363A0 */
            DEBRIEF_LINE(PUFO_DEBRIEF_TEXT_SURGERY, 2 * stats.nSurgery,
                         2 * stats.nSurgery, DEBRIEF_META_LOOT);
            pSite->auItemQty[PUFO_LOOT_OBJ_SURGERY] = (u16)stats.nSurgery;
            nLootRecorded = 1;
            pEnv->pItems->apObjects[PUFO_LOOT_OBJ_SURGERY]->uRecovCount
                += (u16)stats.nSurgery;
        }
    }

    if (nLootRecorded > 0) {                                                        /* 0x364A0 */
        pSite = &pEnv->pCraft->aSites[*pEnv->pnSiteCount];

        pSite->bHasLoot = 1;                                                        /* 0x364C8 */
        pSite->bFlag1 = pEnv->apBases[pEnv->pCraft->apCraft[*pEnv->pCraft->pnCraftIndex]
                                          ->bHomeBase]->bUnk4;                       /* 0x36504 */
        pSite->bFlag0 = PUFO_DEBRIEF_SITE_FLAG0;                                    /* 0x36508 */
        *pEnv->pnSiteCount += 1;                                                    /* 0x36510 */
    }

    pEnv->pPanelLabel->nStyle = *pnLine;                                            /* 0x3651C */
    pEnv->pPanelValue->nStyle = *pnLine;                                            /* 0x36528 */

    snprintf(pEnv->pszScore, PUFO_DEBRIEF_TEXT_BYTES, "%d", nScore);                /* 0x36534 */
    *pEnv->pnFundPending += nScore;                                                 /* 0x365B0 */
    *pEnv->pbSpawnOnceFlag = 1;                                                     /* 0x36584 */

    if (*pEnv->pnMissionType == PUFO_MISSION_TYPE_MARS_SPECIAL && *pEnv->pbSpawnAlt != 0) {
        pEnv->hooks.pfnShutdown(0, pEnv->hooks.pUser);                              /* 0x36598 */
        *pEnv->pnMissionType = PUFO_MISSION_TYPE_ALIEN_BASE;                        /* 0x365AC */
        *pEnv->pbMissionMode = 2;                                                   /* 0x365BC */
        *pEnv->pnTerrainVariant = 0;                                                /* 0x365C8 */
        *pEnv->pnMapLayers = 0;                                                     /* 0x365D4 */
        *pEnv->pbUnk15115 = 0;                                                      /* 0x365E0 */
        pEnv->hooks.pfnInitMission(0, pEnv->hooks.pUser);                           /* 0x365E8 */
        *pEnv->pbFrameScratch = 1;                                                  /* 0x365F0 */
    }
}
