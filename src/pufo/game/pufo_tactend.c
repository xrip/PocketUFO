/* pufo_tactend.c - Tact_Shutdown @ 0x3d568. See pufo_tactend.h for the decode. */
#include "game/pufo_tactend.h"

#include "game/pufo_geoscape.h"
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
            Soldier_RemoveById(pEnv->apEnemyUnits, *pEnv->pnEnemyCount, pUnit->nUnk440);
    }

    nBound = *pEnv->pnXcomCount;
    i = 0;
    while (i < nBound) {                                   /* 0x3DDC8 */
        Soldier *pUnit = (Soldier *)pEnv->apXcomUnits[i];
        u8 nType = pUnit->nUnitType;

        if ((nType > 3 && nType <= 8) || pUnit->bInZone == 0) {
            *pEnv->pnXcomCount =
                Soldier_RemoveById(pEnv->apXcomUnits, *pEnv->pnXcomCount, pUnit->nUnk440);
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
