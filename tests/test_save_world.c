/* test_save_world.c - the world load path must use loaded TACTVER for FLOBJS allocation. */
#include "app/pufo_world.h"
#include "game/pufo_tactctl.h"
#include "game/pufo_template.h"
#include "game/pufo_terrain.h"
#include "ui/pufo_modal.h"
#include "util/pufo_rand.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum {
    TEST_SLOT = 79,
    TEST_MAP_SLOT = 80,
    TEST_OPTIONS_SLOT = 81,
    TEST_FLOBJS_MIN_COUNT = 257,
    TEST_FLOBJS_RECORD_SIZE = 17,
    TEST_FLOBJS_HEADER_SIZE = 4,
    TEST_MAP_DIM_X = 6,
    TEST_MAP_DIM_Y = 5,
    TEST_MAP_ACTIVE_ROWS = TEST_MAP_DIM_Y * PUFO_CELLS_PER_TILE,
    TEST_MAP_ACTIVE_COLS = TEST_MAP_DIM_X * PUFO_CELLS_PER_TILE,
    TEST_MAP_HEADER_SIZE = 2,
    TEST_MAP_RECORD_SIZE = 43,
    TEST_MAP_UNIT_ORDINAL_OFFSET = 15,
    TEST_MAP_RNG_SEED = 100 * PUFO_SHELL_CLOCK_SECOND + PUFO_SHELL_CLOCK_MS,
    TEST_MAP_RNG_WARMUP = PUFO_SHELL_CLOCK_SECOND
};

static i32 g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static bool TestLoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                         const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2);
    return true;
}

static i32 TestInitStartupUi(void *pUser)
{
    (void)pUser;
    return 1;
}

static void TestWorldNoSound(i32 nSample, void *pUser)
{
    (void)nSample;
    (void)pUser;
}

static void TestWorldModalEnd(void *pUser)
{
    PufoWorldState *pWorld = (PufoWorldState *)pUser;

    if (pWorld != NULL) {
        UiModal_End(&pWorld->sModal);
    }
}

static void TestWorldCountSlotFormat(void *pUser)
{
    i32 *pnCalls = (i32 *)pUser;

    if (pnCalls != NULL) {
        *pnCalls += 1;
    }
}

static void TestWorldInventoryConfirm(void *pUser)
{
    World_InvCloseAndCommit((PufoWorldState *)pUser);
}

static void PutLe16(u8 *pDst, u16 uValue)
{
    pDst[0] = (u8)uValue;
    pDst[1] = (u8)(uValue >> 8);
}

static void PutLe32(u8 *pDst, u32 uValue)
{
    pDst[0] = (u8)uValue;
    pDst[1] = (u8)(uValue >> 8);
    pDst[2] = (u8)(uValue >> 16);
    pDst[3] = (u8)(uValue >> 24);
}

static i32 FindObjectIndex(const BootAssets *pAssets, const ObjectRecFull *pObj)
{
    if (pAssets == NULL || pAssets->apObjects == NULL || pObj == NULL) {
        return -1;
    }
    for (i32 i = 0; i < (i32)pAssets->nObjectCount; ++i) {
        if (pAssets->apObjects[i] == pObj) {
            return i;
        }
    }
    return -1;
}

/* Replace only the FLOBJS bytes, leaving the SaveGame directory and every other
 * chunk intact. This uses direct little-endian writes rather than Save_WriteFlobjs. */
static bool WriteRawFlobjs(const char *pszPath, const PufoWorldState *pW)
{
    DatFile dat;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    u8 *pRaw = NULL;
    u32 nFileOff = 0;
    u32 nChunkSize = 0;
    u32 nCount;
    u32 nRawSize;
    DWORD nWritten = 0;
    bool bOpened = false;
    bool bOk = false;

    if (pszPath == NULL || pW == NULL || pW->pAssets == NULL
        || pW->nTactItemCount < TEST_FLOBJS_MIN_COUNT) {
        return false;
    }
    nCount = (u32)pW->nTactItemCount;
    nRawSize = TEST_FLOBJS_HEADER_SIZE + nCount * TEST_FLOBJS_RECORD_SIZE;
    pRaw = (u8 *)malloc(nRawSize);
    if (pRaw == NULL) {
        return false;
    }
    PutLe32(pRaw, nCount);

    for (u32 i = 0; i < nCount; ++i) {
        const TactItem *pItem = pW->apTactItems[i];
        const size_t nOff = TEST_FLOBJS_HEADER_SIZE + (size_t)i * TEST_FLOBJS_RECORD_SIZE;
        const i32 nObjIndex = (pItem != NULL) ? FindObjectIndex(pW->pAssets, pItem->pObj) : -1;
        const u32 nNextId = (pItem != NULL && pItem->pNext != NULL)
                          ? (u32)pItem->pNext->nRefId : 0x7FFFu;
        const u32 nLinkedSoldierId = (pItem != NULL && pItem->pLinkedSoldier != NULL)
                                   ? (u32)pItem->pLinkedSoldier->nSaveId : 0x7FFFu;

        if (pItem == NULL || nObjIndex < 0) {
            goto cleanup;
        }
        PutLe32(pRaw + nOff, (u32)nObjIndex);
        PutLe16(pRaw + nOff + 4, pItem->nIndex);
        pRaw[nOff + 6] = pItem->nAmmo;
        pRaw[nOff + 7] = pItem->bUnk7;
        pRaw[nOff + 8] = pItem->bUnk8;
        PutLe32(pRaw + nOff + 9, nNextId);
        PutLe32(pRaw + nOff + 13, nLinkedSoldierId);
    }

    DatFile_ctor(&dat);
    bOpened = DatFile_Open(&dat, pszPath, PUFO_DAT_READ, 0);
    if (!bOpened || !DatFile_FindChunk(&dat, PUFO_SAVE_TAG_FLOBJS)) {
        goto cleanup;
    }
    nFileOff = dat.nChunkFileOff;
    nChunkSize = dat.nChunkSize;
    DatFile_Close(&dat);
    bOpened = false;
    if (nChunkSize != nRawSize) {
        goto cleanup;
    }

    hFile = CreateFileA(pszPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        goto cleanup;
    }
    if (SetFilePointer(hFile, (LONG)nFileOff, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        goto cleanup;
    }
    bOk = WriteFile(hFile, pRaw, nRawSize, &nWritten, NULL) != 0 && nWritten == nRawSize;

cleanup:
    if (hFile != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(hFile);
    }
    if (bOpened) {
        DatFile_Close(&dat);
    }
    free(pRaw);
    return bOk;
}

static MapCell *TestWorldCellAt(PufoWorldState *pW, i32 nX, i32 nY, i32 nLayer)
{
    if (pW == NULL || nX < 0 || nX >= PUFO_MAP_CELL_DIM
        || nY < 0 || nY >= PUFO_MAP_CELL_DIM
        || nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return pW->apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer
                           + PUFO_MAP_CELL_DIM * nY + nX];
}

static i32 TestWorldSoldierPoolIndex(const PufoWorldState *pW, const Soldier *pUnit,
                                    i32 *pnMatches)
{
    i32 nIndex = -1;
    i32 nMatches = 0;
    const i32 nPoolCap = (i32)(sizeof(pW->apSoldiers) / sizeof(pW->apSoldiers[0]));

    if (pnMatches != NULL) {
        *pnMatches = 0;
    }
    if (pW == NULL || pUnit == NULL || pW->nSoldierCount < 0 || pW->nSoldierCount > nPoolCap) {
        return -1;
    }
    for (i32 i = 0; i < pW->nSoldierCount; ++i) {
        if (pW->apSoldiers[i] == pUnit) {
            nIndex = i;
            ++nMatches;
        }
    }
    if (pnMatches != NULL) {
        *pnMatches = nMatches;
    }
    return nIndex;
}

static i32 TestWorldCellPointerMatches(const PufoWorldState *pW, const MapCell *pCell,
                                      i32 *pnIndex)
{
    i32 nMatches = 0;
    const i32 nSlots = (i32)(sizeof(pW->apTactCells) / sizeof(pW->apTactCells[0]));

    if (pnIndex != NULL) {
        *pnIndex = -1;
    }
    if (pW == NULL || pCell == NULL) {
        return 0;
    }
    for (i32 i = 0; i < nSlots; ++i) {
        if (pW->apTactCells[i] == pCell) {
            if (nMatches == 0 && pnIndex != NULL) {
                *pnIndex = i;
            }
            ++nMatches;
        }
    }
    return nMatches;
}

static i32 TestWorldTactItemPoolMatches(const PufoWorldState *pW, const TactItem *pItem,
                                       i32 *pnIndex)
{
    i32 nMatches = 0;
    const i32 nCapacity = (i32)(sizeof(pW->apTactItems) / sizeof(pW->apTactItems[0]));
    i32 nCount;

    if (pnIndex != NULL) {
        *pnIndex = -1;
    }
    if (pW == NULL || pItem == NULL || pW->nTactItemCount < 0) {
        return 0;
    }
    nCount = (pW->nTactItemCount < nCapacity) ? pW->nTactItemCount : nCapacity;
    for (i32 i = 0; i < nCount; ++i) {
        if (pW->apTactItems[i] == pItem) {
            if (nMatches == 0 && pnIndex != NULL) {
                *pnIndex = i;
            }
            ++nMatches;
        }
    }
    return nMatches;
}

static void TestWorldRosterMatches(const GameMap *pMap, const Soldier *pUnit,
                                   i32 *pnXcom, i32 *pnEnemy, i32 *pnCivilian)
{
    i32 nXcom = 0, nEnemy = 0, nCivilian = 0;

    if (pMap != NULL && pUnit != NULL) {
        for (i32 i = 0; i < (i32)pMap->nXcomCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
            if (pMap->apXcomUnits[i] == pUnit) { ++nXcom; }
        }
        for (i32 i = 0; i < (i32)pMap->nEnemyCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
            if (pMap->apEnemyUnits[i] == pUnit) { ++nEnemy; }
        }
        for (i32 i = 0; i < (i32)pMap->nCivilianCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
            if (pMap->apCivilians[i] == pUnit) { ++nCivilian; }
        }
    }
    if (pnXcom != NULL) { *pnXcom = nXcom; }
    if (pnEnemy != NULL) { *pnEnemy = nEnemy; }
    if (pnCivilian != NULL) { *pnCivilian = nCivilian; }
}

static i32 TestWorldHiddenRosterMatches(const PufoWorldState *pW, const Soldier *pUnit)
{
    i32 nMatches = 0;
    const i32 nCapacity = (i32)(sizeof(pW->apTactHidden) / sizeof(pW->apTactHidden[0]));
    const i32 nCount = (pW != NULL && pW->nMapEnemyReserveCount > 0)
        ? ((pW->nMapEnemyReserveCount < nCapacity) ? pW->nMapEnemyReserveCount : nCapacity) : 0;

    if (pW == NULL || pUnit == NULL) {
        return 0;
    }
    for (i32 i = 0; i < nCount; ++i) {
        if (pW->apTactHidden[i] == pUnit) { ++nMatches; }
    }
    return nMatches;
}

static void TestWorldPrintSoldierRef(const PufoWorldState *pW, const Soldier *pUnit,
                                     i32 nPoolIndex, i32 nDimX, i32 nDimY,
                                     const char *pszLabel)
{
    const GameMap *pMap = (pW != NULL) ? pW->pTactMap : NULL;
    i32 nXcom = 0, nEnemy = 0, nCivilian = 0, nHidden = 0;
    i32 nCellIndex = -1;
    i32 nCellMatches = 0;
    i32 nGridX = -1, nGridY = -1, nGridZ = -1;
    bool bOwns = false;
    bool bOutOfBounds = false;
    const MapCell *pCell = (pUnit != NULL) ? pUnit->pCell : NULL;

    if (pW == NULL || pUnit == NULL) {
        printf("    %s soldier: null world/unit\n", pszLabel);
        return;
    }
    TestWorldRosterMatches(pMap, pUnit, &nXcom, &nEnemy, &nCivilian);
    nHidden = TestWorldHiddenRosterMatches(pW, pUnit);
    nCellMatches = TestWorldCellPointerMatches(pW, pCell, &nCellIndex);
    if (nCellMatches > 0 && nCellIndex >= 0) {
        const i32 nPerLayer = PUFO_TERRAIN_CELLS_PER_LAYER;
        const i32 nWithinLayer = nCellIndex % nPerLayer;
        nGridZ = nCellIndex / nPerLayer;
        nGridY = nWithinLayer / PUFO_MAP_CELL_DIM;
        nGridX = nWithinLayer % PUFO_MAP_CELL_DIM;
        bOutOfBounds = nGridX >= PUFO_CELLS_PER_TILE * nDimX
                    || nGridY >= PUFO_CELLS_PER_TILE * nDimY
                    || nGridZ >= PUFO_MAP_LAYERS;
        bOwns = pCell->pUnit == pUnit;
    }
    printf("    %s soldier pool=%d ptr=%p roster(xcom/enemy/civ/hidden)=%d/%d/%d/%d "
           "type=%u hp=%u status=%u flags=%02X bInTact=%u cell=%p gridMatches=%d "
           "grid=%d/%d/%d stored=%d/%d/%d owns=%d outOfBounds=%d state=%s\n",
           pszLabel, nPoolIndex, (const void *)pUnit, nXcom, nEnemy, nCivilian, nHidden,
           (unsigned)pUnit->nUnitType, (unsigned)pUnit->uHp, (unsigned)pUnit->bStatus,
           (unsigned)pUnit->bFlags, (unsigned)pUnit->bInTact,
           (const void *)pCell, nCellMatches,
           nGridX, nGridY, nGridZ,
           (nCellMatches > 0) ? (i32)pCell->nX : -1,
           (nCellMatches > 0) ? (i32)pCell->nY : -1,
           (nCellMatches > 0) ? (i32)pCell->nZ : -1,
           bOwns, bOutOfBounds,
           (pCell == NULL) ? "parked/no-cell"
             : (nCellMatches == 0) ? "invalid/foreign-cell-ref"
             : bOwns ? "live-owned-cell"
             : (pCell->pUnit == NULL) ? "grid-cell/no-owner"
             : "grid-cell/other-owner");
}

static void TestWorldPrintCellRef(const PufoWorldState *pW, const MapCell *pCell,
                                  i32 nDimX, i32 nDimY, i32 nExpectedX, i32 nExpectedY,
                                  i32 nExpectedZ, const char *pszLabel)
{
    i32 nCellIndex = -1;
    const i32 nCellMatches = TestWorldCellPointerMatches(pW, pCell, &nCellIndex);

    if (pCell == NULL) {
        printf("    %s expected=%d/%d/%d cell=NULL\n", pszLabel,
               nExpectedX, nExpectedY, nExpectedZ);
        return;
    }
    if (nCellMatches == 0 || nCellIndex < 0) {
        printf("    %s cell=%p gridMatches=%d fields=not-read expected=%d/%d/%d\n",
               pszLabel, (const void *)pCell, nCellMatches,
               nExpectedX, nExpectedY, nExpectedZ);
        return;
    }
    {
        const i32 nPerLayer = PUFO_TERRAIN_CELLS_PER_LAYER;
        const i32 nWithinLayer = nCellIndex % nPerLayer;
        const i32 nGridZ = nCellIndex / nPerLayer;
        const i32 nGridY = nWithinLayer / PUFO_MAP_CELL_DIM;
        const i32 nGridX = nWithinLayer % PUFO_MAP_CELL_DIM;
        const Soldier *pOwner = pCell->pUnit;
        i32 nOwnerIndex = -1, nOwnerMatches = 0;
        const TactItem *pItem = pCell->pItemList;
        i32 nItemIndex = -1;
        const i32 nItemMatches = TestWorldTactItemPoolMatches(pW, pItem, &nItemIndex);
        const char *pszItemKind = (pItem == NULL) ? "NULL/empty"
            : (pItem == &g_itemSlotEmpty) ? "known-empty-sentinel"
            : (nItemMatches == 1) ? "pool-item"
            : "foreign-item-pointer";
        const bool bOutOfBounds = nGridX >= PUFO_CELLS_PER_TILE * nDimX
                               || nGridY >= PUFO_CELLS_PER_TILE * nDimY
                               || nGridZ >= PUFO_MAP_LAYERS;
        const bool bCoordMatch = pCell->nX == nGridX && pCell->nY == nGridY
                              && pCell->nZ == nGridZ;
        bool bOwnerBackref = false;

        if (pOwner != NULL) {
            nOwnerIndex = TestWorldSoldierPoolIndex(pW, pOwner, &nOwnerMatches);
            if (nOwnerMatches == 1) {
                bOwnerBackref = pOwner->pCell == pCell;
            }
        }
        printf("    %s cell=%p gridMatches=%d grid=%d/%d/%d stored=%d/%d/%d "
               "outOfBounds=%d coordMatch=%d owner=%p ownerPool=%d/%d ownerBackref=%d "
               "itemList=%p itemKind=%s itemPoolIndex=%d itemPoolMatches=%d\n",
               pszLabel, (const void *)pCell, nCellMatches, nGridX, nGridY, nGridZ,
               (i32)pCell->nX, (i32)pCell->nY, (i32)pCell->nZ,
               bOutOfBounds, bCoordMatch, (const void *)pOwner,
               nOwnerIndex, nOwnerMatches, bOwnerBackref,
               (const void *)pItem, pszItemKind, nItemIndex, nItemMatches);
    }
}

static void TestWorldPreflightFailure(PufoWorldState *pW, const char *pszStage,
                                      const char *pszReason, i32 nDimX, i32 nDimY,
                                      const MapCell *pBadCell, const Soldier *pBadUnit,
                                      i32 nSlotX, i32 nSlotY, i32 nSlotZ)
{
    GameMap *pMap = (pW != NULL) ? pW->pTactMap : NULL;
    i32 nFirstPool = -1, nFirstWithCell = -1;
    const i32 nCapacity = (pW != NULL)
        ? (i32)(sizeof(pW->apSoldiers) / sizeof(pW->apSoldiers[0])) : 0;
    const i32 nCount = (pW != NULL && pW->nSoldierCount > 0)
        ? ((pW->nSoldierCount < nCapacity) ? pW->nSoldierCount : nCapacity) : 0;

    printf("  6x5 preflight detail stage=%s reason=%s requested=%dx%d ",
           pszStage, pszReason, nDimX, nDimY);
    if (pW == NULL) {
        printf("world=NULL\n");
        return;
    }
    printf("actual=%dx%d layers=%d tactLive=%u tactFlag=%u xcom=%d enemy=%d civ=%d reserve=%d "
           "soldiers=%d items=%d\n",
           (pMap != NULL) ? (i32)pMap->geom.nDimX : -1,
           (pMap != NULL) ? (i32)pMap->geom.nDimY : -1,
           (pMap != NULL) ? (i32)pMap->nMapLayers : -1,
           (unsigned)pW->bTactLive, (unsigned)pW->bTactFlag,
           (pMap != NULL) ? (i32)pMap->nXcomCount : -1,
           (pMap != NULL) ? (i32)pMap->nEnemyCount : -1,
           (pMap != NULL) ? (i32)pMap->nCivilianCount : -1,
           pW->nMapEnemyReserveCount, pW->nSoldierCount, pW->nTactItemCount);

    for (i32 i = 0; i < nCount; ++i) {
        const Soldier *pUnit = pW->apSoldiers[i];
        if (pUnit != NULL) {
            if (nFirstPool < 0) { nFirstPool = i; }
            if (nFirstWithCell < 0 && pUnit->pCell != NULL) { nFirstWithCell = i; }
        }
    }
    if (nFirstPool >= 0) {
        TestWorldPrintSoldierRef(pW, pW->apSoldiers[nFirstPool], nFirstPool,
                                 nDimX, nDimY, "first-pool");
    } else {
        printf("    first-pool soldier: none in %d slots\n", nCount);
    }
    if (nFirstWithCell >= 0 && nFirstWithCell != nFirstPool) {
        TestWorldPrintSoldierRef(pW, pW->apSoldiers[nFirstWithCell], nFirstWithCell,
                                 nDimX, nDimY, "first-with-cell");
    }
    if (pW->pTactDeployCell == NULL) {
        printf("    deployCell=NULL state=none\n");
    } else {
        TestWorldPrintCellRef(pW, pW->pTactDeployCell, nDimX, nDimY,
                              -1, -1, -1, "deployCell");
    }
    if (pBadCell != NULL || pBadUnit != NULL || nSlotX >= 0 || nSlotY >= 0 || nSlotZ >= 0) {
        if (pBadUnit != NULL) {
            i32 nMatches = 0;
            const i32 nIndex = TestWorldSoldierPoolIndex(pW, pBadUnit, &nMatches);
            if (nMatches == 1) {
                TestWorldPrintSoldierRef(pW, pBadUnit, nIndex, nDimX, nDimY, "failing-unit");
            } else {
                printf("    failing-unit ptr=%p poolMatches=%d fields=not-read\n",
                       (const void *)pBadUnit, nMatches);
            }
        }
        TestWorldPrintCellRef(pW, pBadCell, nDimX, nDimY,
                              nSlotX, nSlotY, nSlotZ, "failing-cell");
    } else {
        const i32 nActiveY = (nDimY > 0) ? PUFO_CELLS_PER_TILE * nDimY : 0;
        for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
            bool bFound = false;
            for (i32 y = nActiveY; y < PUFO_MAP_CELL_DIM && !bFound; ++y) {
                for (i32 x = 0; x < PUFO_MAP_CELL_DIM; ++x) {
                    MapCell *const pCell = TestWorldCellAt(pW, x, y, z);
                    if (pCell != NULL && (pCell->pUnit != NULL || pCell->pItemList != NULL)) {
                        TestWorldPrintCellRef(pW, pCell, nDimX, nDimY,
                                              x, y, z, "first-cropped-cell-ref");
                        bFound = true;
                        break;
                    }
                }
            }
            if (bFound) { break; }
        }
    }
}

static void TestWorldPrintTargetCandidates(const PufoWorldState *pW,
                                          i32 nDimX, i32 nDimY)
{
    const GameMap *pMap = (pW != NULL) ? pW->pTactMap : NULL;
    const i32 nRosterCount = (pMap != NULL && pMap->nXcomCount > 0)
        ? ((pMap->nXcomCount < PUFO_MAP_ROSTER_MAX) ? pMap->nXcomCount : PUFO_MAP_ROSTER_MAX) : 0;

    printf("    target-selection: xcomRoster=%d soldierPool=%d required=alive,operative,owned,z>=1,ordinal>0\n",
           (pMap != NULL) ? (i32)pMap->nXcomCount : -1,
           (pW != NULL) ? pW->nSoldierCount : -1);
    for (i32 i = 0; i < nRosterCount; ++i) {
        const Soldier *pUnit = pMap->apXcomUnits[i];
        i32 nMatches = 0;
        const i32 nPoolIndex = TestWorldSoldierPoolIndex(pW, pUnit, &nMatches);

        if (pUnit == NULL) {
            printf("      xcom[%d] ptr=NULL result=empty-roster-slot\n", i);
            continue;
        }
        if (nMatches != 1) {
            printf("      xcom[%d] ptr=%p poolIndex=%d poolMatches=%d fields=not-read result=noncanonical-pool-pointer\n",
                   i, (const void *)pUnit, nPoolIndex, nMatches);
            continue;
        }
        {
            const MapCell *pCell = pUnit->pCell;
            i32 nCellIndex = -1;
            const i32 nCellMatches = TestWorldCellPointerMatches(pW, pCell, &nCellIndex);
            i32 nGridX = -1, nGridY = -1, nGridZ = -1;
            bool bGridCell = false;
            bool bOwner = false;
            bool bOutOfBounds = false;
            const bool bAlive = pUnit->uHp != 0;
            const bool bOperative = Unit_bIsOperative(pUnit);
            const bool bPositiveOrdinal = nPoolIndex > 0;
            bool bZAboveZero = false;

            if (nCellMatches > 0 && nCellIndex >= 0) {
                const i32 nWithin = nCellIndex % PUFO_TERRAIN_CELLS_PER_LAYER;
                nGridZ = nCellIndex / PUFO_TERRAIN_CELLS_PER_LAYER;
                nGridY = nWithin / PUFO_MAP_CELL_DIM;
                nGridX = nWithin % PUFO_MAP_CELL_DIM;
                bGridCell = true;
                bOwner = pCell->pUnit == pUnit;
                bOutOfBounds = nGridX >= PUFO_CELLS_PER_TILE * nDimX
                            || nGridY >= PUFO_CELLS_PER_TILE * nDimY
                            || nGridZ >= PUFO_MAP_LAYERS;
                bZAboveZero = nGridZ >= 1;
            }
            TestWorldPrintSoldierRef(pW, pUnit, nPoolIndex, nDimX, nDimY, "target-candidate");
            printf("      xcom[%d] eligibility alive=%d operative=%d poolOrdinal=%d positive=%d "
                   "cellRef=%p gridMatches=%d grid=%d/%d/%d owned=%d z>=1=%d outOfBounds=%d\n",
                   i, bAlive, bOperative, nPoolIndex, bPositiveOrdinal,
                   (const void *)pCell, nCellMatches, nGridX, nGridY, nGridZ,
                   bGridCell && bOwner, bZAboveZero, bOutOfBounds);
        }
    }
}

/* The 6x5 save omits rows 50..59. Refuse the fixture unless every live owner and ground-item
 * list remains inside that geometry. The backing grid still has four fixed 60x60 layers. */
static bool TestWorldMapRefsFit(PufoWorldState *pW, i32 nDimX, i32 nDimY,
                                const u8 *pUnplacedByWireOrdinal,
                                bool bOriginWireSentinel, const char *pszStage)
{
    const i32 nActiveX = PUFO_CELLS_PER_TILE * nDimX;
    const i32 nActiveY = PUFO_CELLS_PER_TILE * nDimY;
    const i32 nSoldierPoolCap = (i32)(sizeof(pW->apSoldiers) / sizeof(pW->apSoldiers[0]));

    if (pW == NULL || pW->pTactMap == NULL || nDimX <= 0 || nDimY <= 0
        || nActiveX > PUFO_MAP_CELL_DIM || nActiveY > PUFO_MAP_CELL_DIM
        || pW->nSoldierCount < 0 || pW->nSoldierCount > nSoldierPoolCap) {
        TestWorldPreflightFailure(pW, pszStage, "invalid world/map/bounds/pool count",
                                  nDimX, nDimY, NULL, NULL, -1, -1, -1);
        return false;
    }
    if (pW->pTactDeployCell != NULL) {
        const i32 nCellMatches = TestWorldCellPointerMatches(pW, pW->pTactDeployCell,
                                                              NULL);
        if (nCellMatches == 0) {
            TestWorldPreflightFailure(pW, pszStage,
                                      "deploy cell pointer is not in current grid",
                                      nDimX, nDimY, pW->pTactDeployCell, NULL, -1, -1, -1);
            return false;
        }
        if (pW->pTactDeployCell->nX >= nActiveX || pW->pTactDeployCell->nY >= nActiveY
            || pW->pTactDeployCell->nZ >= PUFO_MAP_LAYERS
            || TestWorldCellAt(pW, pW->pTactDeployCell->nX, pW->pTactDeployCell->nY,
                               pW->pTactDeployCell->nZ) != pW->pTactDeployCell) {
            TestWorldPreflightFailure(pW, pszStage,
                                      "deploy cell is outside crop or not its current grid slot",
                                      nDimX, nDimY, pW->pTactDeployCell, NULL, -1, -1, -1);
            return false;
        }
    }
    for (i32 i = 0; i < pW->nSoldierCount; ++i) {
        Soldier *const pUnit = pW->apSoldiers[i];
        MapCell *const pCell = (pUnit != NULL) ? pUnit->pCell : NULL;
        i32 nPoolMatches = 0;

        if (pCell == NULL) {
            if (pUnplacedByWireOrdinal != NULL && pUnit != NULL && pUnit->bInTact != 0) {
                TestWorldPreflightFailure(pW, pszStage,
                                          "flagged tactical Soldier did not resolve a cell pointer",
                                          nDimX, nDimY, NULL, pUnit, -1, -1, -1);
                return false;
            }
            continue;
        }
        if (TestWorldCellPointerMatches(pW, pCell, NULL) == 0) {
            TestWorldPreflightFailure(pW, pszStage,
                                      "Soldier cell pointer is not in current grid",
                                      nDimX, nDimY, pCell, pUnit, -1, -1, -1);
            return false;
        }
        if (pCell->nX >= nActiveX || pCell->nY >= nActiveY
            || pCell->nZ >= PUFO_MAP_LAYERS) {
            TestWorldPreflightFailure(pW, pszStage, "Soldier cell is outside crop/layers",
                                      nDimX, nDimY, pCell, pUnit, -1, -1, -1);
            return false;
        }
        if (TestWorldCellAt(pW, pCell->nX, pCell->nY, pCell->nZ) != pCell) {
            TestWorldPreflightFailure(pW, pszStage,
                                      "Soldier cell coordinates do not point back to its grid slot",
                                      nDimX, nDimY, pCell, pUnit, -1, -1, -1);
            return false;
        }
        if (TestWorldSoldierPoolIndex(pW, pUnit, &nPoolMatches) != i || nPoolMatches != 1) {
            TestWorldPreflightFailure(pW, pszStage,
                                      "Soldier pool pointer is not unique at its wire ordinal",
                                      nDimX, nDimY, pCell, pUnit, -1, -1, -1);
            return false;
        }
        if (pCell->pUnit != pUnit) {
            /* Raw Save_AssignRosterIndices writes the pre-save pool index as this wire ordinal;
             * Save_ReadSoldiers restores record i into apSoldiers[i], not into loaded nSaveId. */
            const bool bKnownUnplacedOrigin = pUnplacedByWireOrdinal != NULL
                && i < PUFO_SOLDIER_CAPACITY && pUnplacedByWireOrdinal[i] != 0
                && bOriginWireSentinel && pCell == TestWorldCellAt(pW, 0, 0, 0)
                && pCell->pUnit == NULL && pUnit->bInTact != 0;

            if (bKnownUnplacedOrigin) {
                printf("  known unplaced Soldier wireOrdinal=%d pool=%d type=%u hp=%u status=%u bInTact=%u "
                       "preSaveCell=NULL wireCell=0/0/0 MAPordinal=0x7FFF cellOwner=NULL\n",
                       i, i, (unsigned)pUnit->nUnitType, (unsigned)pUnit->uHp,
                       (unsigned)pUnit->bStatus, (unsigned)pUnit->bInTact);
                continue;
            }
            TestWorldPreflightFailure(pW, pszStage,
                                      (pCell->pUnit == NULL)
                                          ? "Soldier has a grid cell with no owner (parked/no-owner)"
                                          : "Soldier cell is owned by a different unit",
                                      nDimX, nDimY, pCell, pUnit, -1, -1, -1);
            return false;
        }
    }
    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nY = 0; nY < PUFO_MAP_CELL_DIM; ++nY) {
            for (i32 nX = 0; nX < PUFO_MAP_CELL_DIM; ++nX) {
                MapCell *const pCell = TestWorldCellAt(pW, nX, nY, nLayer);

                if (nX < nActiveX && nY < nActiveY
                    && (pCell == NULL || pCell->nX != nX || pCell->nY != nY
                        || pCell->nZ != nLayer)) {
                    TestWorldPreflightFailure(pW, pszStage,
                                              (pCell == NULL)
                                                  ? "active grid slot is NULL"
                                                  : "active grid slot cell coordinates disagree",
                                              nDimX, nDimY, pCell, NULL,
                                              nX, nY, nLayer);
                    return false;
                }
                if (pCell == NULL) {
                    continue;
                }
                if (pCell->pUnit != NULL) {
                    i32 nPoolMatches = 0;
                    const i32 nOwnerIndex = TestWorldSoldierPoolIndex(
                        pW, pCell->pUnit, &nPoolMatches);
                    if (nX >= nActiveX || nY >= nActiveY) {
                        TestWorldPreflightFailure(pW, pszStage,
                                                  "cell owner lies outside crop",
                                                  nDimX, nDimY, pCell, pCell->pUnit,
                                                  nX, nY, nLayer);
                        return false;
                    }
                    if (nOwnerIndex < 0 || nPoolMatches != 1) {
                        TestWorldPreflightFailure(pW, pszStage,
                                                  "cell owner pointer is not a unique Soldier-pool entry",
                                                  nDimX, nDimY, pCell, NULL,
                                                  nX, nY, nLayer);
                        return false;
                    }
                    if (pCell->pUnit->pCell != pCell) {
                        TestWorldPreflightFailure(pW, pszStage,
                                                  "cell owner back-reference points elsewhere",
                                                  nDimX, nDimY, pCell, pCell->pUnit,
                                                  nX, nY, nLayer);
                        return false;
                    }
                }
                if (pCell->pItemList != NULL && (nX >= nActiveX || nY >= nActiveY)) {
                    TestWorldPreflightFailure(pW, pszStage,
                                              "ground item list lies outside crop",
                                              nDimX, nDimY, pCell, NULL,
                                              nX, nY, nLayer);
                    return false;
                }
            }
        }
    }
    return true;
}

static bool ReadMapChunk(const char *pszPath, u8 **ppRaw, u32 *pnRawSize)
{
    DatFile dat;
    u8 *pRaw = NULL;
    bool bOpen = false;
    bool bOk = false;

    if (pszPath == NULL || ppRaw == NULL || pnRawSize == NULL) {
        return false;
    }
    *ppRaw = NULL;
    *pnRawSize = 0;
    DatFile_ctor(&dat);
    bOpen = DatFile_Open(&dat, pszPath, PUFO_DAT_READ, 0);
    if (!bOpen || !DatFile_FindChunk(&dat, PUFO_SAVE_TAG_MAP)) {
        goto cleanup;
    }
    pRaw = (u8 *)malloc(dat.nChunkSize);
    if (pRaw == NULL || !DatFile_Read(&dat, pRaw, dat.nChunkSize)) {
        goto cleanup;
    }
    *ppRaw = pRaw;
    *pnRawSize = dat.nChunkSize;
    pRaw = NULL;
    bOk = true;

cleanup:
    if (bOpen) {
        DatFile_Close(&dat);
    }
    free(pRaw);
    return bOk;
}

static bool TestWorldNormalPlaneFits6x5(PufoWorldState *pW)
{
    MapTemplate plane = {0};
    bool bFits = false;

    if (pW == NULL || pW->pTactMap == NULL || pW->pTactMap->nMapLayers != 1) {
        printf("  plane footprint: invalid world or craft layout; need layout 1\n");
        return false;
    }
    if (!MapTemplate_Load(&plane, g_aPlaneName[pW->pTactMap->nMapLayers - 1], 3)) {
        printf("  plane footprint: template %s could not be loaded\n",
               g_aPlaneName[pW->pTactMap->nMapLayers - 1]);
        MapTemplate_Free(&plane);
        return false;
    }
    {
        const i32 nPlaneRows = (i32)MapTemplate_GetHeaderByte0(&plane);
        const i32 nPlaneCols = (i32)MapTemplate_GetHeaderByte1(&plane);
        const i32 nPlaneTileY = nPlaneRows / PUFO_CELLS_PER_TILE;
        const i32 nPlaneTileX = nPlaneCols / PUFO_CELLS_PER_TILE;
        const i32 nCraftTileX = (i32)pW->bMapCraftMapX;
        const i32 nCraftTileY = (i32)pW->bMapCraftMapY;

        bFits = nPlaneTileX > 0 && nPlaneTileY > 0
             && nCraftTileX + nPlaneTileX <= TEST_MAP_DIM_X
             && nCraftTileY + nPlaneTileY <= TEST_MAP_DIM_Y;
        printf("  plane footprint: name=%s cells=%dx%d tiles=%dx%d origin=%d/%d "
               "retained=6x5 fits=%d\n",
               g_aPlaneName[pW->pTactMap->nMapLayers - 1], nPlaneCols, nPlaneRows,
               nPlaneTileX, nPlaneTileY, nCraftTileX, nCraftTileY, bFits);
    }
    MapTemplate_Free(&plane);
    return bFits;
}

/* Before the real confirm deploy, XCOM crew may share the inventory/deploy cell without owning it.
 * This pass checks only their cell references and already-live owners/items; full ownership is
 * checked after World_InvCloseAndCommit runs through the normal key hook. */
static bool TestWorldNormalPreDeployRefsFit6x5(PufoWorldState *pW)
{
    const i32 nCellX = PUFO_CELLS_PER_TILE * TEST_MAP_DIM_X;
    const i32 nCellY = PUFO_CELLS_PER_TILE * TEST_MAP_DIM_Y;
    const i32 nSoldierCap = (pW != NULL)
        ? (i32)(sizeof(pW->apSoldiers) / sizeof(pW->apSoldiers[0])) : 0;

    if (pW == NULL || pW->pTactMap == NULL || pW->nSoldierCount < 0
        || pW->nSoldierCount > nSoldierCap) {
        TestWorldPreflightFailure(pW, "pre-deploy-spatial", "invalid world or Soldier pool count",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        return false;
    }
    if (pW->pTactDeployCell != NULL) {
        MapCell *const pCell = pW->pTactDeployCell;
        if (TestWorldCellPointerMatches(pW, pCell, NULL) == 0
            || pCell->nX >= nCellX || pCell->nY >= nCellY
            || pCell->nZ >= PUFO_MAP_LAYERS
            || TestWorldCellAt(pW, pCell->nX, pCell->nY, pCell->nZ) != pCell) {
            TestWorldPreflightFailure(pW, "pre-deploy-spatial",
                                      "deploy cell is outside 6x5 or not its current grid slot",
                                      TEST_MAP_DIM_X, TEST_MAP_DIM_Y, pCell, NULL, -1, -1, -1);
            return false;
        }
    }
    for (i32 i = 0; i < pW->nSoldierCount; ++i) {
        Soldier *const pUnit = pW->apSoldiers[i];
        MapCell *const pCell = (pUnit != NULL) ? pUnit->pCell : NULL;

        if (pCell == NULL) {
            continue;
        }
        if (TestWorldCellPointerMatches(pW, pCell, NULL) == 0) {
            TestWorldPreflightFailure(pW, "pre-deploy-spatial",
                                      "parked/deploying Soldier cell pointer is not in current grid",
                                      TEST_MAP_DIM_X, TEST_MAP_DIM_Y, pCell, pUnit, -1, -1, -1);
            return false;
        }
        if (pCell->nX >= nCellX || pCell->nY >= nCellY
            || pCell->nZ >= PUFO_MAP_LAYERS
            || TestWorldCellAt(pW, pCell->nX, pCell->nY, pCell->nZ) != pCell) {
            TestWorldPreflightFailure(pW, "pre-deploy-spatial",
                                      "parked/deploying Soldier cell ref is outside 6x5",
                                      TEST_MAP_DIM_X, TEST_MAP_DIM_Y, pCell, pUnit, -1, -1, -1);
            return false;
        }
    }
    for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (i32 y = nCellY; y < PUFO_MAP_CELL_DIM; ++y) {
            for (i32 x = 0; x < PUFO_MAP_CELL_DIM; ++x) {
                MapCell *const pCell = TestWorldCellAt(pW, x, y, z);
                if (pCell != NULL && (pCell->pUnit != NULL || pCell->pItemList != NULL)) {
                    TestWorldPreflightFailure(pW, "pre-deploy-spatial",
                                              "live unit owner or ground-item list is outside 6x5",
                                              TEST_MAP_DIM_X, TEST_MAP_DIM_Y, pCell,
                                              pCell->pUnit, x, y, z);
                    return false;
                }
            }
        }
    }
    return true;
}

static u32 ReadLe32(const u8 *pBytes)
{
    return (u32)pBytes[0] | ((u32)pBytes[1] << 8)
         | ((u32)pBytes[2] << 16) | ((u32)pBytes[3] << 24);
}

/* A normal Forest/layout-1 mission is built, the existing inventory-confirm hook deploys its
 * crew, and only then is the test-only 6x5 MapGeom saved and loaded. */
static void TestWorldTactMapNonSquareRoundTrip(PufoWorldState *pW, BootAssets *pAssets,
                                              PufoFont *pFont, const PufoOptions *pOptions,
                                              const WorldHooks *pHooks, const char *pszAppDir)
{
    const u32 nExpectedRawSize = TEST_MAP_HEADER_SIZE
        + (u32)(PUFO_MAP_LAYERS * TEST_MAP_ACTIVE_ROWS * TEST_MAP_ACTIVE_COLS
               * TEST_MAP_RECORD_SIZE);
    GameMap *pMap = (pW != NULL) ? pW->pTactMap : NULL;
    Soldier *pTarget = NULL;
    i32 nWireOrdinal = -1;
    i32 nRosterMatches = 0;
    i32 nTargetX = -1, nTargetY = -1, nTargetZ = -1;
    i32 nTargetRosterIndex = -1;
    i32 nPreSaveSoldierCount = 0;
    i32 nUnplacedTacticalCount = 0;
    i32 nSaveRet;
    i32 nLoadRet;
    char szSlotPath[MAX_PATH] = {0};
    u8 *pMapRaw = NULL;
    u32 nMapRawSize = 0;
    u32 nOriginWireOrdinal = 0xFFFFFFFFu;
    bool bOriginWireSentinel = false;
    bool bOriginCellUnownedBeforeSave = false;
    u8 abPreSaveCellNullByWireOrdinal[PUFO_SOLDIER_CAPACITY] = {0};
    u8 abUnplacedTacticalByWire[PUFO_SOLDIER_CAPACITY] = {0};

    printf("World non-square tactical save: valid normal 6x5 MAP records and owner reload\n");
    Check(pMap != NULL && pMap->geom.nDimX == TEST_MAP_DIM_X
              && pMap->geom.nDimY == TEST_MAP_DIM_Y,
          "  the normal crew deploys under the declared 6x5 geometry");
    if (pMap == NULL || pMap->geom.nDimX != TEST_MAP_DIM_X
        || pMap->geom.nDimY != TEST_MAP_DIM_Y) {
        TestWorldPreflightFailure(pW, "source-geometry", "normal deployed map is not 6x5",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        return;
    }
    Check(pMap->nMissionType == PUFO_MISSION_TYPE_FOREST && pMap->nMapLayers == 1
              && pW->bMapMissionMode == 0 && pW->nMapMissionTierRaw == 1
              && pW->nMapTerrainVariant == 2 && pW->nMapCraftIndex == 0,
          "  Forest/mode0/tier1/variant2 metadata keeps craft layout 1");
    if (pMap->nMissionType != PUFO_MISSION_TYPE_FOREST || pMap->nMapLayers != 1
        || pW->bMapMissionMode != 0 || pW->nMapMissionTierRaw != 1
        || pW->nMapTerrainVariant != 2 || pW->nMapCraftIndex != 0) {
        TestWorldPreflightFailure(pW, "source-metadata",
                                  "normal mission metadata differs from Forest/layout-1 setup",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        return;
    }
    {
        const bool bRefsFit = TestWorldMapRefsFit(pW, TEST_MAP_DIM_X, TEST_MAP_DIM_Y,
                                                   NULL, false, "post-deploy/pre-save");
        Check(bRefsFit,
          "  every Soldier, deploy-cell, and ground-item reference fits rows 0..49 before the 6x5 save");
        if (!bRefsFit) {
            return;
        }
    }
    for (i32 i = 0; i < (i32)pMap->nXcomCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
        Soldier *const pUnit = pMap->apXcomUnits[i];
        i32 nMatches = 0;
        const i32 nIndex = TestWorldSoldierPoolIndex(pW, pUnit, &nMatches);

        if (pUnit == NULL || nMatches != 1 || nIndex <= 0) {
            continue;
        }
        if (pUnit != NULL && pUnit->uHp != 0 && Unit_bIsOperative(pUnit)
            && pUnit->pCell != NULL && pUnit->pCell->nZ >= 1
            && pUnit->pCell->pUnit == pUnit
            && TestWorldCellAt(pW, pUnit->pCell->nX, pUnit->pCell->nY,
                               pUnit->pCell->nZ) == pUnit->pCell) {
            pTarget = pUnit;
            nWireOrdinal = nIndex;
            nTargetX = pUnit->pCell->nX;
            nTargetY = pUnit->pCell->nY;
            nTargetZ = pUnit->pCell->nZ;
            nTargetRosterIndex = i;
            break;
        }
    }
    Check(pTarget != NULL && nWireOrdinal > 0,
          "  a living operative XCOM already owns a z>=1 cell at a positive Soldier-pool ordinal");
    if (pTarget == NULL || nWireOrdinal <= 0) {
        TestWorldPreflightFailure(pW, "target-selection",
                                  "no XCOM meets living/operative/owned/z>=1/positive-ordinal conditions",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        TestWorldPrintTargetCandidates(pW, TEST_MAP_DIM_X, TEST_MAP_DIM_Y);
        return;
    }
    printf("  accepted tactical-map seed=%d after deployed refs and target fit\n",
           TEST_MAP_RNG_SEED);
    Check(Save_BuildSlotPath(szSlotPath, sizeof(szSlotPath), pszAppDir, TEST_MAP_SLOT),
          "  the second save uses a path inside this test's private save directory");
    if (szSlotPath[0] == '\0') {
        TestWorldPreflightFailure(pW, "private-slot-path", "Save_BuildSlotPath failed",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, pTarget, -1, -1, -1);
        return;
    }

    nPreSaveSoldierCount = pW->nSoldierCount;
    for (i32 i = 0; i < nPreSaveSoldierCount; ++i) {
        Soldier *const pUnit = pW->apSoldiers[i];
        /* Raw Save_AssignRosterIndices makes this pre-save pool index the MAP/SOLDIER wire ordinal. */
        abPreSaveCellNullByWireOrdinal[i] = pUnit != NULL && pUnit->pCell == NULL;
    }
    {
        MapCell *const pOrigin = TestWorldCellAt(pW, 0, 0, 0);
        bOriginCellUnownedBeforeSave = pOrigin != NULL && pOrigin->pUnit == NULL;
        printf("  pre-save MAP origin cell=%p owner=%p knownUnowned=%d\n",
               (void *)pOrigin, (pOrigin != NULL) ? (void *)pOrigin->pUnit : NULL,
               bOriginCellUnownedBeforeSave);
    }

    pMap->geom.nDimX = TEST_MAP_DIM_X;
    pMap->geom.nDimY = TEST_MAP_DIM_Y;
    nSaveRet = World_SaveSlot(pW, pszAppDir, TEST_MAP_SLOT, pHooks);
    Check(nSaveRet != 0, "  World_SaveSlot writes the 6x5 geometry and its active cell records");
    if (nSaveRet == 0) {
        pMap->geom.nDimY = 6;
        return;
    }
    Check(pTarget->nSaveId == nWireOrdinal,
          "  SaveGame assigns the live Soldier-pool index as the wire ordinal");
    if (pTarget->nSaveId != nWireOrdinal) {
        pMap->geom.nDimY = 6;
        return;
    }
    {
        bool bPoolOrdinalMapping = true;

        for (i32 i = 0; i < nPreSaveSoldierCount; ++i) {
            Soldier *const pUnit = pW->apSoldiers[i];
            i32 nXcom = 0, nEnemy = 0, nCivilian = 0;

            if (pUnit == NULL) {
                continue;
            }
            if (pUnit->nSaveId != i || pUnit->nSaveId < 0
                || pUnit->nSaveId >= PUFO_SOLDIER_CAPACITY) {
                bPoolOrdinalMapping = false;
                printf("  Soldier ordinal snapshot mismatch: pool=%d wireId=%d\n",
                       i, (int)pUnit->nSaveId);
                continue;
            }
            TestWorldRosterMatches(pMap, pUnit, &nXcom, &nEnemy, &nCivilian);
            if (abPreSaveCellNullByWireOrdinal[i] != 0 && pUnit->pCell == NULL
                && pUnit->bInTact != 0) {
                const i32 nWire = pUnit->nSaveId;
                const i32 nHidden = TestWorldHiddenRosterMatches(pW, pUnit);
                abUnplacedTacticalByWire[nWire] = 1;
                ++nUnplacedTacticalCount;
                Check(nXcom + nEnemy + nCivilian + nHidden > 0,
                      "  flagged unplaced Soldier belongs to a tactical roster or hidden reserve");
                printf("  pre-save unplaced Soldier wireOrdinal=%d pool=%d bInTact=%u "
                       "cell=NULL roster(xcom/enemy/civ/hidden)=%d/%d/%d/%d type=%u hp=%u status=%u\n",
                       nWire, i, (unsigned)pUnit->bInTact, nXcom, nEnemy, nCivilian, nHidden,
                       (unsigned)pUnit->nUnitType, (unsigned)pUnit->uHp,
                       (unsigned)pUnit->bStatus);
                TestWorldPrintSoldierRef(pW, pUnit, nWire, TEST_MAP_DIM_X, TEST_MAP_DIM_Y,
                                         "pre-save-unplaced");
            }
        }
        Check(bPoolOrdinalMapping,
              "  Save_AssignRosterIndices maps every live pool slot to that wire ordinal");
        Check(nUnplacedTacticalCount > 0,
              "  the fixed normal fixture has a tactical Soldier with pre-save NULL cell and bInTact=1");
    }

    Check(ReadMapChunk(szSlotPath, &pMapRaw, &nMapRawSize),
          "  the private save MAP chunk can be read back as raw bytes");
    if (pMapRaw == NULL) {
        pMap->geom.nDimY = 6;
        goto cleanup;
    }
    CheckEqI32((i32)nMapRawSize, (i32)nExpectedRawSize,
               "  MAP is a 2-byte header plus 43 bytes for each of 12,000 active cells");
    Check(nMapRawSize == nExpectedRawSize
              && pMapRaw[0] == TEST_MAP_DIM_X && pMapRaw[1] == TEST_MAP_DIM_Y,
          "  MAP metadata stores dimensions 6x5 before its positional cell records");
    if (nMapRawSize != nExpectedRawSize
        || pMapRaw[0] != TEST_MAP_DIM_X || pMapRaw[1] != TEST_MAP_DIM_Y) {
        pMap->geom.nDimX = TEST_MAP_DIM_X;
        pMap->geom.nDimY = 6;
        goto cleanup;
    }
    {
        const size_t nOriginOrdinalOff = TEST_MAP_HEADER_SIZE + TEST_MAP_UNIT_ORDINAL_OFFSET;

        nOriginWireOrdinal = ReadLe32(pMapRaw + nOriginOrdinalOff);
        bOriginWireSentinel = nOriginWireOrdinal == 0x7FFFu;
        printf("  MAP origin wire ordinal=0x%04X nullSentinel=%d preSaveUnowned=%d "
               "unplacedTacticalSoldiers=%d\n",
               (unsigned)nOriginWireOrdinal, bOriginWireSentinel,
               bOriginCellUnownedBeforeSave, nUnplacedTacticalCount);
        if (bOriginCellUnownedBeforeSave) {
            Check(bOriginWireSentinel,
                  "  the unoccupied origin cell writes the raw 0x7FFF unit sentinel");
        }
        if (nUnplacedTacticalCount > 0) {
            Check(bOriginWireSentinel,
                  "  roster-flagged Soldiers with pre-save NULL cells share an unoccupied wire origin");
        }
    }
    {
        const u32 nCellIndex = (u32)((nTargetZ * TEST_MAP_ACTIVE_ROWS + nTargetY)
                                     * TEST_MAP_ACTIVE_COLS + nTargetX);
        const size_t nOrdinalOff = TEST_MAP_HEADER_SIZE
                                 + (size_t)nCellIndex * TEST_MAP_RECORD_SIZE
                                 + TEST_MAP_UNIT_ORDINAL_OFFSET;
        const u32 nWireAtCell = ReadLe32(pMapRaw + nOrdinalOff);

        Check(nWireAtCell == (u32)nWireOrdinal,
              "  the selected cell's 4-byte MAP ordinal equals the live source pool index");
        printf("  6x5 MAP: %u bytes; cell %d/%d/%d record %u ordinal %u (XCOM slot %d)\n",
               (unsigned)nMapRawSize, nTargetX, nTargetY, nTargetZ,
               (unsigned)nCellIndex, (unsigned)nWireAtCell, nTargetRosterIndex);
    }
    pMap->geom.nDimX = TEST_MAP_DIM_X;
    pMap->geom.nDimY = 6;
    Check(pMap->geom.nDimX == TEST_MAP_DIM_X && pMap->geom.nDimY == 6,
          "  the in-memory source geometry returns to 6x6 before the load");

    nLoadRet = World_LoadSlot(pW, pszAppDir, TEST_MAP_SLOT, pAssets, pFont, pOptions, pHooks);
    Check(nLoadRet != 0, "  World_LoadSlot reads the valid non-square MAP chunk");
    pMap = pW->pTactMap;
    Check(pMap != NULL && pMap->geom.nDimX == TEST_MAP_DIM_X
              && pMap->geom.nDimY == TEST_MAP_DIM_Y,
          "  saved 6x5 dimensions replace the loader's default 6x6 geometry");
    if (pMap == NULL || pMap->geom.nDimX != TEST_MAP_DIM_X
        || pMap->geom.nDimY != TEST_MAP_DIM_Y) {
        goto cleanup;
    }

    {
        i32 nActiveCells = 0;
        bool bInactiveRowsNull = true;
        bool bActiveCoordinatesMatch = true;

        for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
            for (i32 nY = 0; nY < PUFO_MAP_CELL_DIM; ++nY) {
                for (i32 nX = 0; nX < PUFO_MAP_CELL_DIM; ++nX) {
                    MapCell *const pCell = TestWorldCellAt(pW, nX, nY, nLayer);
                    if (nX < TEST_MAP_ACTIVE_COLS && nY < TEST_MAP_ACTIVE_ROWS) {
                        if (pCell != NULL) {
                            ++nActiveCells;
                            if (pCell->nX != nX || pCell->nY != nY || pCell->nZ != nLayer) {
                                bActiveCoordinatesMatch = false;
                            }
                        } else {
                            bActiveCoordinatesMatch = false;
                        }
                    } else if (pCell != NULL) {
                        bInactiveRowsNull = false;
                    }
                }
            }
        }
        CheckEqI32(nActiveCells, PUFO_MAP_LAYERS * TEST_MAP_ACTIVE_COLS * TEST_MAP_ACTIVE_ROWS,
                   "  the 6x5 load allocates all 12,000 active cells across four backing layers");
        Check(bActiveCoordinatesMatch,
              "  each allocated slot keeps its layer, row, and column coordinate");
        Check(bInactiveRowsNull, "  cells outside the 6x5 geometry remain unallocated");
    }

    if (nWireOrdinal >= 0 && nWireOrdinal < pW->nSoldierCount
        && nWireOrdinal < PUFO_SOLDIER_CAPACITY) {
        Soldier *const pLoadedUnit = pW->apSoldiers[nWireOrdinal];
        MapCell *const pLoadedCell = TestWorldCellAt(pW, nTargetX, nTargetY, nTargetZ);
        i32 nLoadedPoolMatches = 0;

        nRosterMatches = 0;
        for (i32 i = 0; i < (i32)pMap->nXcomCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
            if (pMap->apXcomUnits[i] == pLoadedUnit) {
                ++nRosterMatches;
            }
        }
        (void)TestWorldSoldierPoolIndex(pW, pLoadedUnit, &nLoadedPoolMatches);
        Check(pLoadedUnit != NULL && nLoadedPoolMatches == 1 && nRosterMatches == 1
                  && pLoadedUnit->uHp != 0 && Unit_bIsOperative(pLoadedUnit)
                  && pLoadedCell != NULL && pLoadedCell->pUnit == pLoadedUnit
                  && pLoadedUnit->pCell == pLoadedCell
                  && pLoadedCell->nX == nTargetX && pLoadedCell->nY == nTargetY
                  && pLoadedCell->nZ == nTargetZ,
              "  the positive wire ordinal resolves a living XCOM that owns its loaded z>=1 cell");
        {
            i32 nRestoredOriginPlaceholders = 0;
            MapCell *const pOrigin = TestWorldCellAt(pW, 0, 0, 0);

            for (i32 i = 0; i < nPreSaveSoldierCount; ++i) {
                Soldier *pUnplaced;
                i32 nMatches = 0, nXcom = 0, nEnemy = 0, nCivilian = 0;
                i32 nLoadedPoolIndex = -1;
                i32 nHidden = 0;
                bool bExactOriginRestore;

                if (abUnplacedTacticalByWire[i] == 0) {
                    continue;
                }
                pUnplaced = (i < pW->nSoldierCount && i < PUFO_SOLDIER_CAPACITY)
                          ? pW->apSoldiers[i] : NULL;
                if (pUnplaced != NULL) {
                    nLoadedPoolIndex = TestWorldSoldierPoolIndex(pW, pUnplaced, &nMatches);
                    TestWorldRosterMatches(pMap, pUnplaced, &nXcom, &nEnemy, &nCivilian);
                    nHidden = TestWorldHiddenRosterMatches(pW, pUnplaced);
                }
                bExactOriginRestore = bOriginWireSentinel && nOriginWireOrdinal == 0x7FFFu
                    && pUnplaced != NULL && nLoadedPoolIndex == i && nMatches == 1
                    && pUnplaced->bInTact != 0
                    && pOrigin != NULL && pOrigin->nX == 0 && pOrigin->nY == 0
                    && pOrigin->nZ == 0 && pOrigin->pUnit == NULL
                    && pUnplaced->pCell == pOrigin;
                Check(bExactOriginRestore,
                      "  captured unplaced wire ordinal restores to the zero-coordinate cell without a MAP owner");
                printf("  origin placeholder wireOrdinal=%d loadedPoolIndex=%d restored=%d bInTact=%u cell=%p "
                       "origin=%p owner=%p roster(xcom/enemy/civ/hidden)=%d/%d/%d/%d type=%u hp=%u status=%u "
                       "MAPordinal=0x%04X\n",
                       i, nLoadedPoolIndex, bExactOriginRestore,
                       (pUnplaced != NULL) ? (unsigned)pUnplaced->bInTact : 0u,
                       (pUnplaced != NULL) ? (void *)pUnplaced->pCell : NULL,
                       (void *)pOrigin, (pOrigin != NULL) ? (void *)pOrigin->pUnit : NULL,
                       nXcom, nEnemy, nCivilian, nHidden,
                       (pUnplaced != NULL) ? (unsigned)pUnplaced->nUnitType : 0u,
                       (pUnplaced != NULL) ? (unsigned)pUnplaced->uHp : 0u,
                       (pUnplaced != NULL) ? (unsigned)pUnplaced->bStatus : 0u,
                       (unsigned)nOriginWireOrdinal);
                if (bExactOriginRestore) {
                    ++nRestoredOriginPlaceholders;
                }
            }
            CheckEqI32(nRestoredOriginPlaceholders, nUnplacedTacticalCount,
                       "  each saved-null tactical Soldier has the original origin-placeholder roundtrip");
        }
        Check(TestWorldMapRefsFit(pW, TEST_MAP_DIM_X, TEST_MAP_DIM_Y,
                                  abUnplacedTacticalByWire, bOriginWireSentinel, "post-load"),
              "  every loaded Soldier, deploy-cell, and ground-item reference fits the 6x5 geometry");
    } else {
        Check(false, "  the saved positive wire ordinal is within the loaded Soldier pool");
    }

cleanup:
    free(pMapRaw);
}

static void TestWorldTactMapNonSquare(void)
{
    static const char *const apszArchives[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    static BootAssets assets;
    static PufoWorldState world;
    static PufoFont font;
    static PufoOptions options;
    static WorldHooks hooks;
    static GfxColorEnv color;
    static UiControlHitCtx hit;
    static UiWidgetBank bank;
    static TactInputHooks tactInput;
    static SaveLoadSlotHooks slotHooks;
    static char aSlotLabels[7][40];
    static bool bBankAction;
    static u32 nUiSlot0;
    static i32 nAnimPhase, nListMode;
    static u8 nMapPhase;
    char szTempLeaf[MAX_PATH] = {0};
    char szAppDir[MAX_PATH] = {0};
    char szSlotPath[MAX_PATH] = {0};
    bool bTempFileOwned = false;
    bool bTempDirOwned = false;
    bool bWorldInit = false;
    i32 nStartRet;

    printf("World non-square tactical save: fresh normal Forest mission\n");
    if (GetTempFileNameA("build\\", "nsf", 0, szTempLeaf) == 0) {
        Check(false, "  private non-square save directory name created");
        return;
    }
    bTempFileOwned = true;
    if (!DeleteFileA(szTempLeaf)) {
        Check(false, "  private temporary file removed before directory creation");
        goto cleanup;
    }
    bTempFileOwned = false;
    if (!CreateDirectoryA(szTempLeaf, NULL)) {
        Check(false, "  private non-square save directory created");
        goto cleanup;
    }
    bTempDirOwned = true;
    {
        const int nLen = snprintf(szAppDir, sizeof(szAppDir), "%s\\", szTempLeaf);
        if (nLen <= 0 || (size_t)nLen >= sizeof(szAppDir)
            || !Save_BuildSlotPath(szSlotPath, sizeof(szSlotPath), szAppDir, TEST_MAP_SLOT)) {
            Check(false, "  private non-square slot path fits");
            goto cleanup;
        }
    }

    Check(Boot_LoadStartup(&assets, apszArchives), "  shipped assets load for the fresh mission");
    if (assets.apObjects == NULL || assets.nObjectCount == 0) {
        goto cleanup;
    }

    memset(&options, 0, sizeof(options));
    options.nGameSpeed = 4;
    options.bScrollByPen = 1;
    options.bRunByDblClick = 1;
    options.bMenuByPen = 1;
    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadPads = TestLoadPads;
    hooks.pfnInitStartupUi = TestInitStartupUi;
    hooks.pHit = &hit;
    hooks.pColor = &color;
    hooks.pBank = &bank;
    hooks.pbBankActionFlag = &bBankAction;
    tactInput.pfnInvCloseAndCommit = TestWorldInventoryConfirm;
    tactInput.pUser = &world;
    hooks.pTactInputHooks = &tactInput;
    hooks.pSaveLoadHooks = &slotHooks;
    hooks.pSlotLabels = &aSlotLabels[0][0];
    hooks.pnListMode = &nListMode;
    hooks.pTactOptions = &options;
    hooks.pszSaveDir = szAppDir;
    hooks.pnAnimPhase = &nAnimPhase;
    hooks.pnMapPhase = &nMapPhase;
    hit.pnUiSlot0 = &nUiSlot0;
    (void)UiBank_CtorAll(&bank, &bBankAction);

    bWorldInit = World_Init(&world);
    Check(bWorldInit, "  fresh world owner initializes");
    if (!bWorldInit) {
        TestWorldPreflightFailure(&world, "world-init", "World_Init failed before map setup",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        goto cleanup;
    }
    nStartRet = World_NewGame(&world, &assets, &font, &options, &hooks);
    Check(nStartRet != 0, "  World_NewGame supplies the live world and Soldier pool");
    if (nStartRet == 0) {
        TestWorldPreflightFailure(&world, "new-game", "World_NewGame failed before map setup",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        goto cleanup;
    }
    for (i32 i = 0; i < 16; ++i) {
        if (world.geoClock.pfnTickBases != NULL) {
            world.geoClock.pfnTickBases(world.geoClock.pUser);
        }
    }
    {
        const i32 nCrew = Craft_CountSoldiersAboard(world.apSoldiers,
                                                   world.nSoldierCount, 0);
        CheckEqI32(nCrew, 8, "  New Game craft 0 retains its eight natural crew");
        if (nCrew != 8) {
            TestWorldPreflightFailure(&world, "starting-crew",
                                      "New Game craft 0 does not have its expected crew",
                                      TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
            goto cleanup;
        }
    }
    world.nMapMissionType = PUFO_MISSION_TYPE_FOREST;
    world.bMapMissionMode = 0;
    world.nSelectedBase = 0;
    world.nCraftHomeBase = 0;
    world.nMapCraftIndex = 0;
    world.nMapMissionTierRaw = 1;
    world.nMapTerrainVariant = 2;
    world.nMapMapLayers = 1;
    world.bGeoscapeUi = 0;
    Game_SeedRngFixed(); /* test-only map seed, with the original 30-draw warm-up */
    printf("  deterministic tactical-map seed: %d (warm-up draws %d)\n",
           TEST_MAP_RNG_SEED, TEST_MAP_RNG_WARMUP);
    Check(World_InitTactical(&world, 0),
          "  World_InitTactical builds the normal Forest/layout-1 mission");
    if (!world.bTactLive || world.pTactMap == NULL || world.pTactState == NULL) {
        TestWorldPreflightFailure(&world, "tactical-init",
                                  "World_InitTactical returned without a live map/state",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        goto cleanup;
    }
    Check(world.pTactMap->geom.nDimX == TEST_MAP_DIM_X && world.pTactMap->geom.nDimY == 6
              && world.pTactMap->nMapLayers == 1
              && world.pTactMap->nMissionType == PUFO_MISSION_TYPE_FOREST
              && world.bMapMissionMode == 0 && world.nMapCraftIndex == 0
              && world.nMapMissionTierRaw == 1 && world.nMapTerrainVariant == 2,
          "  normal terrain starts at 6x6 with Forest/mode0/craft0/tier1/variant2/layout1 metadata");
    if (world.pTactMap->geom.nDimX != TEST_MAP_DIM_X || world.pTactMap->geom.nDimY != 6
        || world.pTactMap->nMapLayers != 1
        || world.pTactMap->nMissionType != PUFO_MISSION_TYPE_FOREST
        || world.bMapMissionMode != 0 || world.nMapCraftIndex != 0
        || world.nMapMissionTierRaw != 1 || world.nMapTerrainVariant != 2) {
        TestWorldPreflightFailure(&world, "normal-mission-metadata",
                                  "normal mission tuple is not 6x6/Forest/mode0/craft0/tier1/variant2/layout1",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        goto cleanup;
    }
    if (!TestWorldNormalPlaneFits6x5(&world)) {
        TestWorldPreflightFailure(&world, "plane-footprint",
                                  "layout-1 plane template does not fit retained 6x5 geometry",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        goto cleanup;
    }
    if (!TestWorldNormalPreDeployRefsFit6x5(&world)) {
        goto cleanup;
    }
    Check(world.nUiScreen == 1u,
          "  the normal mission is at the live inventory screen before deploy confirm");
    if (world.nUiScreen != 1u) {
        TestWorldPreflightFailure(&world, "pre-deploy-input",
                                  "normal mission did not reach inventory screen 1",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        goto cleanup;
    }
    world.pTactMap->geom.nDimY = TEST_MAP_DIM_Y;
    Check(world.pTactMap->geom.nDimX == TEST_MAP_DIM_X
              && world.pTactMap->geom.nDimY == TEST_MAP_DIM_Y,
          "  test-only 6x5 geometry is set before normal deployment");
    World_TactKeyDown(&world, PUFO_KEY_CONFIRM);
    World_TactKeyUp(&world, PUFO_KEY_CONFIRM);
    Check(world.bTactLive && world.pTactMap != NULL && world.pTactMap->nXcomCount > 0
              && world.nUiScreen == 0u,
          "  real inventory confirm runs World_InvCloseAndCommit and deploys the crew");
    if (!world.bTactLive || world.pTactMap == NULL || world.pTactMap->nXcomCount <= 0
        || world.nUiScreen != 0u) {
        TestWorldPreflightFailure(&world, "normal-deploy-confirm",
                                  "confirm did not complete the live normal deployment",
                                  TEST_MAP_DIM_X, TEST_MAP_DIM_Y, NULL, NULL, -1, -1, -1);
        goto cleanup;
    }
    TestWorldTactMapNonSquareRoundTrip(&world, &assets, &font, &options, &hooks, szAppDir);

cleanup:
    if (bWorldInit) {
        World_Free(&world);
    }
    if (szAppDir[0] != '\0'
        && Save_BuildSlotPath(szSlotPath, sizeof(szSlotPath), szAppDir, TEST_MAP_SLOT)) {
        (void)DeleteFileA(szSlotPath);
    }
    if (bTempDirOwned) {
        (void)RemoveDirectoryA(szTempLeaf);
    } else if (bTempFileOwned) {
        (void)DeleteFileA(szTempLeaf);
    }
}

static void TestWorldTactVerFlobjs257(void)
{
    static const char *const apszArchives[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    static BootAssets assets;
    static PufoWorldState world;
    static PufoFont font;
    static PufoOptions options;
    static WorldHooks hooks;
    static GfxColorEnv color;
    static UiControlHitCtx hit;
    static UiWidgetBank bank;
    static TactInputHooks tactInput;
    static SaveLoadSlotHooks slotHooks;
    static char aSlotLabels[7][40];
    static bool bBankAction;
    static u32 nUiSlot0;
    static i32 nAnimPhase, nListMode;
    static u8 nMapPhase;
    char szTempLeaf[MAX_PATH] = {0};
    char szAppDir[MAX_PATH] = {0};
    char szSlotPath[MAX_PATH] = {0};
    bool bTempFileOwned = false;
    bool bTempDirOwned = false;
    bool bWorldInit = false;
    i32 nStartRet, nSaveRet, nLoadRet;
    i32 nStartItems, nTargetItems, nRootItem, nMarkerItem;
    i32 nSavedDeployedHwps;
    u8 bSavedDeployPanelOpen;
    i32 nCraftSlot;
    const i32 nItemQtySlots = (i32)(sizeof(((ObjectRecFull *)0)->aItemQty)
                                   / sizeof(((ObjectRecFull *)0)->aItemQty[0]));
    Ship *pCraft;
    TactPointerWorld modalPointer = {0};

    printf("World tactical load: TACTVER selects the wide FLOBJS allocation\n");
    if (GetTempFileNameA("build\\", "wfv", 0, szTempLeaf) == 0) {
        Check(false, "  private save directory name created");
        return;
    }
    bTempFileOwned = true;
    if (!DeleteFileA(szTempLeaf)) {
        Check(false, "  private temporary file removed before directory creation");
        goto cleanup;
    }
    bTempFileOwned = false;
    if (!CreateDirectoryA(szTempLeaf, NULL)) {
        Check(false, "  private save directory created");
        goto cleanup;
    }
    bTempDirOwned = true;
    {
        const int nLen = snprintf(szAppDir, sizeof(szAppDir), "%s\\", szTempLeaf);
        if (nLen <= 0 || (size_t)nLen >= sizeof(szAppDir)
            || !Save_BuildSlotPath(szSlotPath, sizeof(szSlotPath), szAppDir, TEST_SLOT)) {
            Check(false, "  private slot path fits");
            goto cleanup;
        }
    }

    Check(Boot_LoadStartup(&assets, apszArchives), "  shipped assets load");
    if (assets.apObjects == NULL || assets.nObjectCount == 0) {
        goto cleanup;
    }

    memset(&options, 0, sizeof(options));
    options.nGameSpeed = 4;
    options.bScrollByPen = 1;
    options.bRunByDblClick = 1;
    options.bMenuByPen = 1;
    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadPads = TestLoadPads;
    hooks.pfnInitStartupUi = TestInitStartupUi;
    hooks.pHit = &hit;
    hooks.pColor = &color;
    hooks.pBank = &bank;
    hooks.pbBankActionFlag = &bBankAction;
    hooks.pTactInputHooks = &tactInput;
    hooks.pSaveLoadHooks = &slotHooks;
    hooks.pSlotLabels = &aSlotLabels[0][0];
    hooks.pnListMode = &nListMode;
    hooks.pTactOptions = &options;
    hooks.pszSaveDir = szAppDir;
    hooks.pnAnimPhase = &nAnimPhase;
    hooks.pnMapPhase = &nMapPhase;
    hit.pnUiSlot0 = &nUiSlot0;
    (void)UiBank_CtorAll(&bank, &bBankAction);

    bWorldInit = World_Init(&world);
    Check(bWorldInit, "  world owner initializes");
    if (!bWorldInit) {
        goto cleanup;
    }
    nStartRet = World_NewGame(&world, &assets, &font, &options, &hooks);
    Check(nStartRet != 0, "  World_NewGame seeds a live tactical launch state");
    if (nStartRet == 0) {
        goto cleanup;
    }
    Check(color.pnFadeCount == &world.nMapUiSlot1,
          "  World_NewGame binds the shared color env to the live Map+0x64 fade cell");
    for (i32 i = 0; i < 16; ++i) {
        if (world.geoClock.pfnTickBases != NULL) {
            world.geoClock.pfnTickBases(world.geoClock.pUser);
        }
    }
    pCraft = (world.bCraftCount > 0) ? world.apCraft[0] : NULL;
    Check(pCraft != NULL, "  fresh world has a craft slot for mission setup");
    if (pCraft == NULL) {
        goto cleanup;
    }
    nCraftSlot = pCraft->nSlotIdx;
    Check(nCraftSlot >= 0 && nCraftSlot < nItemQtySlots,
          "  fresh craft has a valid cargo column");
    if (nCraftSlot < 0 || nCraftSlot >= nItemQtySlots) {
        goto cleanup;
    }
    for (i32 i = 0; i < (i32)assets.nObjectCount; ++i) {
        if (assets.apObjects[i] != NULL) {
            ((ObjectRecFull *)assets.apObjects[i])->aItemQty[nCraftSlot] = 0;
        }
    }
    for (i32 i = 0; i < 36; ++i) {
        world.abMapTileTypes[i] = (u8)(i % 8);
    }
    world.nMapMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    world.bMapMissionMode = 1;
    world.nSelectedBase = 0;
    world.nCraftHomeBase = 0;
    world.nMapCraftIndex = 0;
    world.nMapMapLayers = 0;
    world.bGeoscapeUi = 0;
    Check(World_InitTactical(&world, 0), "  World_InitTactical builds a live mission");
    if (!world.bTactLive || world.pTactMap == NULL || world.pTactState == NULL) {
        goto cleanup;
    }
    World_InvCloseAndCommit(&world);

    /* The raw dialog writes only the coordinates for buttons it draws. Keep the prior second-button
     * X through a one-button frame, then check the real World pointer route against that hit region. */
    modalPointer.pBank = &bank;
    modalPointer.hit = hit;
    tactInput.pUser = &world;
    tactInput.pfnModalEnd = TestWorldModalEnd;
    UiModal_Begin(&world.sModal, 140, PUFO_TEXT_REC_OK, 1, (void *)"YES", NULL,
                  (void *)"NO", NULL);
    World_TactFrame(&world);
    CheckEqI32(world.sTactInput.nSlot20X, 63, "  two-button frame publishes the first button X");
    CheckEqI32(world.sTactInput.nSlot21X, 126, "  two-button frame publishes the second button X");
    CheckEqI32(world.sTactInput.nSlot22Y, 159, "  two-button frame publishes its button Y");
    CheckEqI32(world.sTactInput.nSlot23H, 2, "  two-button frame publishes both buttons");

    UiModal_End(&world.sModal);
    UiModal_Begin(&world.sModal, 160, PUFO_TEXT_REC_OK, 1, (void *)"OK", NULL, NULL, NULL);
    World_TactFrame(&world);
    CheckEqI32(world.sTactInput.nSlot20X, 95, "  one-button frame updates the visible button X");
    CheckEqI32(world.sTactInput.nSlot21X, 126, "  one-button frame preserves the unwritten second X");
    CheckEqI32(world.sTactInput.nSlot22Y, 159, "  one-button frame updates its button Y");
    CheckEqI32(world.sTactInput.nSlot23H, 1, "  one-button frame publishes one button");
    Check(150 > world.sTactInput.nSlot20X + 50
              && 150 >= world.sTactInput.nSlot21X
              && 150 <= world.sTactInput.nSlot21X + 50,
          "  the click is outside OK but inside the old second-button range");

    World_TactPointerMove(&world, &modalPointer, 150, world.sTactInput.nSlot22Y + 1, true);
    CheckEqI32(world.sModal.nField48, 2,
               "  pointer over the old second-button range records hit 2");
    World_TactPointerUp(&world, &modalPointer);
    Check(!world.sModal.bOpen,
          "  the null second callback still ends the modal through the world input hook");
    UiModal_End(&world.sModal);
    tactInput.pfnModalEnd = NULL;
    tactInput.pUser = NULL;

    nStartItems = world.nTactItemCount;
    if (nStartItems < 0 || nStartItems > TEST_FLOBJS_MIN_COUNT - 2) {
        Check(false, "  the fresh mission starts below the 256-item width boundary");
        goto cleanup;
    }
    nTargetItems = TEST_FLOBJS_MIN_COUNT;
    if (nTargetItems > (i32)(sizeof(world.apTactItems) / sizeof(world.apTactItems[0]))) {
        Check(false, "  the high-index FLOBJS fixture fits the item table");
        goto cleanup;
    }
    for (i32 i = 0; i < nStartItems; ++i) {
        if (world.apTactItems[i] == NULL) {
            Check(false, "  the existing item pool is a dense prefix");
            goto cleanup;
        }
    }
    for (i32 i = nStartItems; i < nTargetItems; ++i) {
        TactItem *pItem = world.apTactItems[i];
        if (pItem == NULL) {
            pItem = (TactItem *)calloc(1, sizeof(*pItem));
            if (pItem == NULL) {
                Check(false, "  a new tactical item allocates");
                goto cleanup;
            }
            TactItem_ctor(pItem);
            world.apTactItems[i] = pItem;
        }
        pItem->pObj = assets.apObjects[0];
        pItem->nIndex = 0;
        pItem->nAmmo = 0;
        pItem->bUnk7 = 0;
        pItem->bUnk8 = 0;
        pItem->pNext = NULL;
        pItem->pLinkedSoldier = NULL;
        world.nTactItemCount = i + 1;
    }
    world.nTactItemCount = nTargetItems;
    nRootItem = TEST_FLOBJS_MIN_COUNT - 2;
    nMarkerItem = TEST_FLOBJS_MIN_COUNT - 1;
    world.apTactItems[nRootItem]->pNext = world.apTactItems[nMarkerItem];
    world.apTactItems[nMarkerItem]->nIndex = 0x1234;
    world.apTactItems[nMarkerItem]->nAmmo = 0xA5;
    world.apTactItems[nMarkerItem]->bUnk7 = 0x5A;
    world.apTactItems[nMarkerItem]->bUnk8 = 0xC3;
    CheckEqI32(nTargetItems, 257, "  the fixture count is exactly 257");
    Check(nMarkerItem == 256, "  the marked item is at index 256");

    /* The saved deploy flag is 1; the reset must leave it. */
    world.sTactInput.bDeployPanelOpen = 1;

    nSaveRet = World_SaveSlot(&world, szAppDir, TEST_SLOT, &hooks);
    Check(nSaveRet != 0, "  World_SaveSlot writes the tactical mission");
    if (nSaveRet == 0) {
        goto cleanup;
    }
    Check(WriteRawFlobjs(szSlotPath, &world),
          "  the FLOBJS chunk is hand-encoded at its existing raw size");

    nSavedDeployedHwps = world.nMapDeployedHwps;
    bSavedDeployPanelOpen = world.sTactInput.bDeployPanelOpen;

    /* These live input values are still there after World_FreeTactical. The load reset must clear them. */
    world.sTactInput.bInvDragActive = 1;
    world.sTactInput.nUiSlot0 = 3;
    world.sTactInput.nUiSlot3 = 8;
    world.sTactInput.nUiSlot4 = 15;
    world.sTactInput.nUiSlot5 = 1u; /* The left-edge repeat flag. */
    world.sTactInput.nUiSlot7 = 1;
    world.sTactInput.bUiSlot8Byte0 = 1;

    color.pnFadeCount = NULL; /* The load must bind it, independently of NewGame. */
    nLoadRet = World_LoadSlot(&world, szAppDir, TEST_SLOT, &assets, &font, &options, &hooks);
    Check(nLoadRet != 0, "  World_LoadSlot reloads the tactical mission");
    Check(color.pnFadeCount == &world.nMapUiSlot1,
          "  World_LoadSlot rebinds the shared color env to the live Map+0x64 fade cell");
    Check(world.sTactInput.bInvDragActive == 0
              && world.sTactInput.nUiSlot0 == 0
              && world.sTactInput.nUiSlot3 == 0
              && world.sTactInput.nUiSlot5 == 0
              && world.sTactInput.nUiSlot7 == 0
              && world.sTactInput.bUiSlot8Byte0 == 0,
          "  the loaded reset clears live drag, phase, edge, slot 7, and slot 8 input state");
    CheckEqI32(world.sTactInput.nUiSlot4, 200,
               "  the loaded reset sets slot 4 to 200, not zero");
    Check(world.pTactMap != NULL && world.pTactMap->nShotCount == 0
              && world.pTactMap->bFireStatus9 == 0,
          "  the loaded tactical map has the reset shot count and fire-status byte");
    Check(world.sTactInput.bDeployPanelOpen == bSavedDeployPanelOpen
              && bSavedDeployPanelOpen == 1
              && world.nMapDeployedHwps == nSavedDeployedHwps,
          "  saved deploy state is still there after the tactical reset");
    CheckEqI32((i32)world.nTactVerOut, 1,
               "  LoadGame publishes TACTVER 1 before tactical init");
    CheckEqI32(world.nTactItemCount, nTargetItems,
               "  the world loads the full FLOBJS count");
    Check(nMarkerItem < (i32)(sizeof(world.apTactItems) / sizeof(world.apTactItems[0]))
              && world.apTactItems[nMarkerItem] != NULL,
          "  the loaded pool contains the high-index FLOBJS record");
    if (world.apTactItems[nMarkerItem] != NULL) {
        Check(world.apTactItems[nMarkerItem]->nIndex == 0x1234
                  && world.apTactItems[nMarkerItem]->nAmmo == 0xA5
                  && world.apTactItems[nMarkerItem]->bUnk7 == 0x5A
                  && world.apTactItems[nMarkerItem]->bUnk8 == 0xC3,
              "  the hand-encoded high record fields survive load");
    }
    if (world.apTactItems[nRootItem] != NULL && world.apTactItems[nMarkerItem] != NULL) {
        Check(world.apTactItems[nRootItem]->pNext == world.apTactItems[nMarkerItem],
              "  the high item link resolves to its array entry");
    }

cleanup:
    if (bWorldInit) {
        World_Free(&world);
    }
    if (szAppDir[0] != '\0' && Save_BuildSlotPath(szSlotPath, sizeof(szSlotPath), szAppDir, TEST_SLOT)) {
        (void)DeleteFileA(szSlotPath);
    }
    if (bTempDirOwned) {
        (void)RemoveDirectoryA(szTempLeaf);
    } else if (bTempFileOwned) {
        (void)DeleteFileA(szTempLeaf);
    }
}

static void TestWorldColdLoadOptions(void)
{
    static const char *const apszArchives[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    static BootAssets assets;
    static PufoWorldState writer;
    static PufoWorldState cold;
    static PufoFont font;
    static PufoOptions options;
    static WorldHooks hooks;
    static GfxColorEnv color;
    static UiControlHitCtx hit;
    static UiWidgetBank bank;
    static TactInputHooks tactInput;
    static SaveLoadSlotHooks slotHooks;
    static char aSlotLabels[7][40];
    static bool bBankAction;
    static u32 nUiSlot0;
    static i32 nAnimPhase, nListMode, nFormatCalls;
    static u8 nMapPhase;
    char szTempLeaf[MAX_PATH] = {0};
    char szAppDir[MAX_PATH] = {0};
    char szSlotPath[MAX_PATH] = {0};
    bool bTempFileOwned = false;
    bool bTempDirOwned = false;
    bool bWriterInit = false;
    bool bColdInit = false;
    bool bAssetsLoaded = false;
    i32 nStartRet, nSaveRet, nLoadRet;
    i32 nRootX, nRootY, nTabX, nTabY;
    bool bOptionsReady;
    bool bSaveRowsBound;
    bool bLoadRowsBound;
    UiControl *pOptionsRoot;
    UiControl *pSaveTab;
    void (*pfnLoadTab)(void);

    printf("World cold load: options controls use the load-side world owner\n");
    if (GetTempFileNameA("build\\", "clo", 0, szTempLeaf) == 0) {
        Check(false, "  private options save directory name created");
        return;
    }
    bTempFileOwned = true;
    if (!DeleteFileA(szTempLeaf)) {
        Check(false, "  private temporary file removed before directory creation");
        goto cleanup;
    }
    bTempFileOwned = false;
    if (!CreateDirectoryA(szTempLeaf, NULL)) {
        Check(false, "  private options save directory created");
        goto cleanup;
    }
    bTempDirOwned = true;
    {
        const int nLen = snprintf(szAppDir, sizeof(szAppDir), "%s\\", szTempLeaf);
        if (nLen <= 0 || (size_t)nLen >= sizeof(szAppDir)
            || !Save_BuildSlotPath(szSlotPath, sizeof(szSlotPath), szAppDir, TEST_OPTIONS_SLOT)) {
            Check(false, "  private geoscape slot path fits");
            goto cleanup;
        }
    }

    bAssetsLoaded = Boot_LoadStartup(&assets, apszArchives);
    Check(bAssetsLoaded, "  shipped assets load for the geoscape save");
    if (!bAssetsLoaded || assets.apObjects == NULL || assets.nObjectCount == 0) {
        goto cleanup;
    }

    memset(&options, 0, sizeof(options));
    options.nGameSpeed = 4;
    options.bScrollByPen = 1;
    options.bRunByDblClick = 1;
    options.bMenuByPen = 1;
    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadPads = TestLoadPads;
    hooks.pfnPlaySample = TestWorldNoSound;
    hooks.pfnInitStartupUi = TestInitStartupUi;
    hooks.pHit = &hit;
    hooks.pColor = &color;
    hooks.pBank = &bank;
    hooks.pbBankActionFlag = &bBankAction;
    hooks.pTactInputHooks = &tactInput;
    hooks.pSaveLoadHooks = &slotHooks;
    hooks.pfnFormatSlotList = TestWorldCountSlotFormat;
    hooks.pFormatSlotListUser = &nFormatCalls;
    hooks.pSlotLabels = &aSlotLabels[0][0];
    hooks.pnListMode = &nListMode;
    hooks.pTactOptions = &options;
    hooks.pszSaveDir = szAppDir;
    hooks.pnAnimPhase = &nAnimPhase;
    hooks.pnMapPhase = &nMapPhase;
    hit.pnUiSlot0 = &nUiSlot0;
    (void)UiBank_CtorAll(&bank, &bBankAction);

    bWriterInit = World_Init(&writer);
    Check(bWriterInit, "  geoscape save owner initializes");
    if (!bWriterInit) {
        goto cleanup;
    }
    nStartRet = World_NewGame(&writer, &assets, &font, &options, &hooks);
    Check(nStartRet != 0, "  writer starts a game for the geoscape save");
    if (nStartRet == 0) {
        goto cleanup;
    }
    Check(!writer.bTactLive && writer.pTactMap == NULL,
          "  writer has no tactical mission in the saved slot");
    nSaveRet = World_SaveSlot(&writer, szAppDir, TEST_OPTIONS_SLOT, &hooks);
    Check(nSaveRet != 0, "  World_SaveSlot writes a geoscape save");
    if (nSaveRet == 0) {
        goto cleanup;
    }

    World_Free(&writer);
    bWriterInit = false;

    bColdInit = World_Init(&cold);
    Check(bColdInit, "  a separate cold consumer initializes");
    if (!bColdInit) {
        goto cleanup;
    }
    Check(cold.optionsHooks.pfnInit == NULL && cold.optionsHooks.pUser == NULL,
          "  the cold consumer has not run World_NewGame options setup");

    /* App_HookLoadGame copies the startup menu's screen17 before World_LoadSlot. */
    cold.nUiScreen = 17;
    nLoadRet = World_LoadSlot(&cold, szAppDir, TEST_OPTIONS_SLOT,
                              &assets, &font, &options, &hooks);
    Check(nLoadRet != 0, "  World_LoadSlot loads the geoscape save into the cold consumer");
    if (nLoadRet == 0) {
        goto cleanup;
    }
    Check(!cold.bTactLive && cold.pTactMap == NULL,
          "  the cold consumer stays on the geoscape path");
    Check(cold.optionsHooks.pfnInit != NULL && cold.optionsHooks.pUser == &cold,
          "  the load-side options hooks name the cold world owner");

    pOptionsRoot = cold.ui.apRootCtl[8];
    Check(pOptionsRoot != NULL && pOptionsRoot->pfnClick != NULL
              && pOptionsRoot->bEnabled != 0
              && pOptionsRoot->uWidth != 0 && pOptionsRoot->uHeight != 0,
          "  the loaded geoscape has its enabled options control");
    if (pOptionsRoot == NULL || pOptionsRoot->pfnClick == NULL
        || pOptionsRoot->bEnabled == 0
        || pOptionsRoot->uWidth == 0 || pOptionsRoot->uHeight == 0) {
        goto cleanup;
    }
    nRootX = (i32)pOptionsRoot->x + (i32)(pOptionsRoot->uWidth / 2u);
    nRootY = (i32)pOptionsRoot->y + (i32)(pOptionsRoot->uHeight / 2u);
    World_GeoscapePointer(&cold, nRootX, nRootY, true);
    World_GeoscapePointerUp(&cold);
    CheckEqI32(cold.screen.nGeoscapeScreen, 18,
               "  pointer dispatch opens the options screen");

    bOptionsReady = cold.abOptionsScratch[4] != 0
        && cold.options.screenHooks.pUser == &cold
        && cold.options.init.pCtl == &cold.options.ctl
        && cold.options.init.pVolEnv == &cold.options.vol
        && cold.options.init.pTabEnv == &cold.options.tab
        && cold.options.init.pbControlsExist == (u8 *)&cold.abOptionsScratch[4]
        && cold.options.tab.pUser == &nFormatCalls
        && cold.options.tab.pfnBindSaveSlots != NULL
        && cold.options.tab.pfnBindLoadSlots != NULL;
    for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {
        if (cold.options.ctl.apList[i] != &cold.options.aListCtl[i]) {
            bOptionsReady = false;
        }
    }
    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        if (cold.options.ctl.apMusic[i] != &cold.options.aMusicCtl[i]
            || cold.options.ctl.apSfx[i] != &cold.options.aSfxCtl[i]) {
            bOptionsReady = false;
        }
    }
    for (i32 i = 0; i < 4; ++i) {
        if (cold.options.aListCtl[i].pfnClick == NULL
            || cold.options.aListCtl[i].uWidth == 0
            || cold.options.aListCtl[i].uHeight == 0) {
            bOptionsReady = false;
        }
    }
    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        if (cold.options.aMusicCtl[i].pfnClick == NULL
            || cold.options.aSfxCtl[i].pfnClick == NULL) {
            bOptionsReady = false;
        }
    }
    Check(bOptionsReady,
          "  options init owners, seven rows, and twelve volume controls are ready");
    if (!bOptionsReady) {
        goto cleanup;
    }

    pSaveTab = &cold.options.aListCtl[0];
    pfnLoadTab = cold.options.aListCtl[1].pfnClick;
    Check(pSaveTab->pfnClick != NULL && pfnLoadTab != NULL
              && pSaveTab->uWidth != 0 && pSaveTab->uHeight != 0,
          "  the installed save and load tab calls are present");
    if (pSaveTab->pfnClick == NULL || pfnLoadTab == NULL
        || pSaveTab->uWidth == 0 || pSaveTab->uHeight == 0) {
        goto cleanup;
    }
    nTabX = (i32)pSaveTab->x + (i32)(pSaveTab->uWidth / 2u);
    nTabY = (i32)pSaveTab->y + (i32)(pSaveTab->uHeight / 2u);
    World_GeoscapePointer(&cold, nTabX, nTabY, true);
    World_GeoscapePointerUp(&cold);
    CheckEqI32(cold.abOptionsScratch[0], 1,
               "  the installed save tab binds the save rows");
    CheckEqI32(nFormatCalls, 1,
               "  the save tab calls the slot formatter with its owner");
    bSaveRowsBound = true;
    for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {
        if (cold.options.aListCtl[i].pfnClick != cold.options.tab.apfnSaveSlot[i]) {
            bSaveRowsBound = false;
        }
    }
    Check(bSaveRowsBound, "  all seven installed rows use the save-slot handlers");

    /* The save tab reuses all seven rows; call the load-tab function kept from its real row. */
    pfnLoadTab();
    CheckEqI32(cold.abOptionsScratch[0], 2,
               "  the installed load-tab call binds the load rows");
    CheckEqI32(nFormatCalls, 2,
               "  the load tab calls the slot formatter with its owner");
    bLoadRowsBound = true;
    for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {
        if (cold.options.aListCtl[i].pfnClick != cold.options.tab.apfnLoadSlot[i]) {
            bLoadRowsBound = false;
        }
    }
    Check(bLoadRowsBound, "  all seven installed rows use the load-slot handlers");

cleanup:
    if (bColdInit) {
        World_Free(&cold);
    }
    if (bWriterInit) {
        World_Free(&writer);
    }
    if (bAssetsLoaded) {
        Boot_Shutdown(&assets);
    }
    if (szAppDir[0] != '\0'
        && Save_BuildSlotPath(szSlotPath, sizeof(szSlotPath), szAppDir, TEST_OPTIONS_SLOT)) {
        (void)DeleteFileA(szSlotPath);
    }
    if (bTempDirOwned) {
        (void)RemoveDirectoryA(szTempLeaf);
    } else if (bTempFileOwned) {
        (void)DeleteFileA(szTempLeaf);
    }
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    TestWorldTactVerFlobjs257();
    TestWorldTactMapNonSquare();
    TestWorldColdLoadOptions();
    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
