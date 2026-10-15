/* pufo_tactctl.c - tactical cursor, game speed, and clock control.
 *
 * Evidence: MapCursor_SetFlags @ 0x26f78, MapCursor_IsValid @ 0x275fc,
 *           GameSpeed_Set @ 0x260c0, GameTime_Pause @ 0x5d6ac.
 */
#include "game/pufo_tactctl.h"
#include "game/pufo_rmp.h"   /* Rmp_PickSpawnForUnit, RmpData */
#include "game/pufo_tactui.h"
#include "game/pufo_unit.h"
#include "game/pufo_ship.h"
#include "game/pufo_statusmsg.h" /* StatusMsg_Push */
#include "audio/pufo_audio.h"
#include "util/pufo_rand.h"   /* Game_RandU16 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* The original's timer backend, injected so the speed logic is testable without
 * a window. Defaults to no-ops: KillTimers_BySpeed @ 0x9e8b4 and
 * StartTimers_BySpeed @ 0x9e904 need the Win32 timer set, which the app shell
 * owns. */
static GameTimers s_timers;

void GameSpeed_SetTimers(const GameTimers *pTimers)
{
    if (pTimers != NULL) {
        s_timers = *pTimers;
    } else {
        memset(&s_timers, 0, sizeof(s_timers));
    }
}

/* MapCursor_SetFlags @ 0x26f78. The original writes through pad86309a[19],
 * which resolves to nLayerScratch @ 0x15138. */
void MapCursor_SetFlags(TactCursor *pCursor, u8 nFlags)
{
    pCursor->nFlags = nFlags;
}

/* MapCursor_IsValid @ 0x275fc.
 *
 * Bounds are half-open on the cell grid, the same shape as Map_IsValidCell, but
 * there is a real difference: this function does NOT validate the layer, because
 * the cursor carries no layer of its own. Kept as-is. */
bool MapCursor_IsValid(const TactCursor *pCursor, const MapGeom *pMap)
{
    if (pCursor->nX < 0) {
        return false;
    }
    if (pCursor->nY < 0) {
        return false;
    }
    if (pCursor->nX >= PUFO_CELL_SIZE * (i32)pMap->nDimX) {
        return false;
    }
    return pCursor->nY < PUFO_CELL_SIZE * (i32)pMap->nDimY;
}

/* GameSpeed_Set @ 0x260c0.
 *
 * The speed is stored unconditionally; only values 0..4 restart the timers. A
 * speed of 5 or more therefore stores but leaves the timer set alone, which is
 * the original's behaviour (the options screen offers 0..4, so the branch exists
 * for out-of-range callers). */
bool GameSpeed_Set(GameClock *pClock, u32 nSpeed)
{
    pClock->nSpeed = nSpeed;
    if (nSpeed <= PUFO_SPEED_MAX) {
        if (s_timers.pfnKillAll != NULL) {
            s_timers.pfnKillAll(s_timers.pUser);
        }
        if (s_timers.pfnStartAll != NULL) {
            s_timers.pfnStartAll(s_timers.pUser);
        }
        return true;
    }
    return false;
}

/* GameTime_Pause @ 0x5d6ac.
 *
 * The original writes LOBYTE(gUiRootPanel.apLines[106]) = 1. apLines is the
 * panel's 500-entry line-pointer array, and slots 105/106 are reused as a clock
 * value and a pause flag rather than as line pointers -- SaveSlot_FormatList
 * (0x45904) reads slot 105 the same way as a packed timestamp. Modelling that as
 * a named field on the clock keeps the aliasing out of the port. */
void GameTime_Pause(GameClock *pClock)
{
    pClock->bPaused = true;
}

/* ----------------------------------------------------------------- view maths */

/* Map_IsoOffsetA/B and Map_CenterOffsetX/Y are already implemented in
 * pufo_mapcell.c; only the camera-offset writer is new here. */

/* MapView_SetCamOffset @ 0x276f0. */
void MapView_SetCamOffset(const MapGeom *pMap, i32 nA, i32 nB, bool bAdjustForLayer,
                          u8 nLayerScratch, CamOffset *pOut)
{
    i32 nHeight = nB;
    i32 v4;
    i32 v5;
    i32 nBias;

    (void)pMap;
    if (pOut == NULL) {
        return;
    }

    if (bAdjustForLayer) {
        nHeight = 24 * (i32)nLayerScratch + nB - 20;
    }
    v4 = nA + 2 * (nHeight + 8);
    v5 = nA / 2 - nHeight;
    /* The bias boundary is `v5 <= 0`, so exactly 0 takes the NEGATIVE bias. */
    nBias = (v5 <= 0) ? -8 : 8;

    pOut->bScratch40 = (u8)(v4 / 32);
    pOut->bScratch41 = (u8)((nBias + v5) / -16);
}

/* ------------------------------------------------------ facing scratch / tiles */

bool Map_InitFacingScratch(MapFacingScratch *pScratch, MapTileSize *pSize, u8 nFacing,
                          bool bTactFlag, u8 nLayerScratch)
{
    if (pScratch == NULL) {
        return false;
    }

    /* The guard is the facing CHANGING, so a repeat call with the same facing is a
     * no-op -- including the tactical tile-size reset below. */
    if (nFacing == pScratch->bStoredFacing) {
        return false;
    }
    pScratch->bStoredFacing = nFacing;

    if (!bTactFlag) {
        return true;
    }

    if (pSize != NULL) {
        pSize->nTileW = PUFO_TILE_W;
        pSize->nTileH = PUFO_TILE_H;
    }
    pScratch->bScratch34 = 9;
    /* Keep the original's operation order: multiply, then divide, then add. */
    pScratch->bScratch35 = (u8)(24 * (i32)nLayerScratch / 16 + 47);
    return true;
}

bool Tact_ScanVictoryTiles(bool bSpawnAlt, u8 nMissionType, i32 nSpawnTileX,
                           i32 nSpawnTileY,
                           bool (*pfnCellHasNoItems)(i32 nX, i32 nY, void *pUser),
                           void *pUser)
{
    /* Both gates must hold: the alternate spawn and the alien-base mission type (0x3328C). */
    if (!bSpawnAlt || nMissionType != PUFO_MISSION_TYPE_ALIEN_BASE) {
        return false;
    }
    if (pfnCellHasNoItems == NULL) {
        return false;
    }

    /* The original's loop order: two columns within a row, then the next row. */
    for (i32 dy = 0; dy < 2; ++dy) {
        for (i32 dx = 0; dx < 2; ++dx) {
            if (pfnCellHasNoItems(nSpawnTileX + dx, nSpawnTileY + dy, pUser)) {
                return true;
            }
        }
    }
    return false;
}

/* Set or clear bFlags57 bit 3 on a looked-up cell. */
static void MapCursor_TouchCell(MapCellLookup pfnLookup, void *pUser,
                                i32 nX, i32 nY, i32 nLayer, bool bSet)
{
    MapCell *pCell;

    if (pfnLookup == NULL) {
        return;
    }
    pCell = pfnLookup(nX, nY, nLayer, pUser);
    if (pCell != NULL) {
        MapCell_SetBFlags57_3(pCell, bSet);
    }
}

/* MapCursor_SetPos @ 0x27014. */
void MapCursor_SetPos(MapCursorPos *pCtx, i32 nX, i32 nY,
                      MapCellLookup pfnLookup, void *pUser)
{
    MapGeom geom;
    i32     nRight;
    i32     nBottom;

    if (pCtx == NULL || pCtx->pCursor == NULL) {
        return;
    }
    geom = pCtx->geom;
    nBottom = pCtx->pCursor->nY;

    if (pCtx->bBusy && Map_IsValidCell(&geom, nX, nY, 0)) {
        return;
    }
    nRight = PUFO_CELL_SIZE * (i32)geom.nDimX;
    nBottom = PUFO_CELL_SIZE * (i32)geom.nDimY;
    {
        i32 nCursorX = pCtx->pCursor->nX;
        i32 nCursorY = pCtx->pCursor->nY;

        for (i32 i = 0; i < 4; ++i) {
            MapCursor_TouchCell(pfnLookup, pUser, nCursorX, nCursorY, i, false);
            if (pCtx->bSelUnitLarge && nCursorX + 1 < PUFO_CELL_SIZE * (i32)geom.nDimX) {
                MapCursor_TouchCell(pfnLookup, pUser, nCursorX + 1, nCursorY, i, false);
                if (nCursorY + 1 < nBottom) {
                    MapCursor_TouchCell(pfnLookup, pUser, nCursorX + 1, nCursorY + 1, i, false);
                }
            }
            if (nCursorY + 1 < nBottom) {
                MapCursor_TouchCell(pfnLookup, pUser, nCursorX, nCursorY + 1, i, false);
            }
        }
    }
    if (nX >= 0 && nY >= 0 && nX <= nRight - 1 && nY <= nBottom - 1) {
        pCtx->pCursor->nX = nX;
        pCtx->pCursor->nY = nY;
        for (i32 v9 = 0; v9 < 4; ++v9) {
            MapCursor_TouchCell(pfnLookup, pUser, nX, nY, v9, true);
            if (pCtx->bSelUnitLarge && !pCtx->bSuppressLarge) {
                if (nX + 1 < nRight) {
                    MapCursor_TouchCell(pfnLookup, pUser, nX + 1, nY, v9, true);
                    if (nY + 1 < nBottom) {
                        MapCursor_TouchCell(pfnLookup, pUser, nX + 1, nY + 1, v9, true);
                    }
                }
                if (nY + 1 < nBottom) {
                    MapCursor_TouchCell(pfnLookup, pUser, nX, nY + 1, v9, true);
                }
            }
        }
    }
}

/* MapView_EnterLookMode @ 0x163d4. */
void MapView_EnterLookMode(LookModeCtx *pCtx,
                           void (*pfnPlaySample)(u32 nSample, void *pUser),
                           void *pUser)
{
    MapGeom geom;

    if (pCtx == NULL) {
        return;
    }
    if (pfnPlaySample != NULL) {
        pfnPlaySample(16, pUser);
    }
    pCtx->nUiScreen = 2;
    geom.nDimX = pCtx->nDimX;
    geom.nDimY = pCtx->nDimY;
    MapView_SetCamOffset(&geom, (i32)pCtx->nMapTileW >> 1, (i32)pCtx->nMapTileH >> 1,
                         false, 0, &pCtx->camOffset);
    pCtx->nCamWorldOff0 = Map_IsoOffsetA((i32)pCtx->nDimX, pCtx->nCamScrollX,
                                         pCtx->nCamScrollY)
                        + pCtx->nTrajFront4 - 30;
    pCtx->nCamWorldOff1 = Map_IsoOffsetB((i32)pCtx->nDimY, pCtx->nCamScrollX,
                                         pCtx->nCamScrollY)
                        + pCtx->nTrajFront5 - 40;
}

/* MapView_FocusOnUnit @ 0x2c758. */
void MapView_FocusOnCell(FocusCtx *pCtx, const MapCell *pCell, TactCursor *pCursor)
{
    MapGeom geom;
    i32 nA;
    i32 nB;

    if (pCtx == NULL || pCell == NULL) {
        return;
    }
    geom.nDimX = pCtx->nDimX;
    geom.nDimY = pCtx->nDimY;
    MapView_SetCamOffset(&geom, (i32)pCtx->nMapTileW >> 1, (i32)pCtx->nMapTileH >> 1,
                         false, 0, &pCtx->camOffset);

    /* The scratch bytes are read back SIGNED, and the subtraction is the other way
     * round from the look-mode handler: cell minus scratch (RSB at 0x2C790). */
    nA = (i32)pCell->nX - (i32)(i8)pCtx->camOffset.bScratch40;
    nB = (i32)pCell->nY - (i32)(i8)pCtx->camOffset.bScratch41;

    pCtx->nCamScrollX = Map_CenterOffsetX((i32)pCtx->nDimX, nA, nB);
    pCtx->nCamScrollY = Map_CenterOffsetY(nA, nB);
    if (pCursor != NULL) {
        MapCursor_SetFlags(pCursor, (u8)(pCell->nZ + 1));
    }
}

/* MapView_FocusSelected @ 0x16710.
 *
 * Plays sample 17 when the gate byte is set, then centres on the selected unit's CELL.
 * The gate is the low byte of Map.apUiSlotsA[2]; the port takes it as an argument because it
 * does not own that slot array. The cell is what the callee takes, not the unit - see the note
 * on MapView_FocusOnCell. */
void MapView_FocusSelected(FocusCtx *pCtx, const MapCell *pSelCell, u8 bGateByte,
                           TactCursor *pCursor)
{
    if (bGateByte != 0) {
        Sound_Play(17);
    }
    MapView_FocusOnCell(pCtx, pSelCell, pCursor);
}

/* Tact_GetCameraBounds @ 0x3331c. */
void Tact_GetCameraBounds(const CameraBoundsCtx *pCtx, i32 *pnX0, i32 *pnY0,
                          i32 *pnX1, i32 *pnY1)
{
    if (pCtx == NULL || pnX0 == NULL || pnY0 == NULL || pnX1 == NULL || pnY1 == NULL) {
        return;
    }
    if (!pCtx->bUseCraft) {
        *pnX0 = 0;
        *pnY0 = 0;
        *pnX1 = PUFO_CELLS_PER_TILE * (i32)pCtx->nMapDimX;
        *pnY1 = PUFO_CELLS_PER_TILE * (i32)pCtx->nMapDimY;
        return;
    }
    if (pCtx->nMissionType == 4) {
        i32 nBase = PUFO_CELLS_PER_TILE * (i32)pCtx->nMapDimY;
        *pnY0 = nBase - 10;
        *pnY1 = nBase;
        *pnX0 = 10 * ((i32)pCtx->nMapDimX - 3);
        *pnX1 = 10 * ((i32)pCtx->nMapDimX - 3) + 30;
        return;
    }
    *pnX0 = PUFO_CELLS_PER_TILE * (i32)pCtx->nCraftMapX;
    *pnY0 = PUFO_CELLS_PER_TILE * (i32)pCtx->nCraftMapY;
    *pnX1 = PUFO_CELLS_PER_TILE * (i32)pCtx->nCraftMapX;
    *pnY1 = PUFO_CELLS_PER_TILE * (i32)pCtx->nCraftMapY;
    if (pCtx->bCraftType == 1) {
        *pnX1 += 20;
        *pnY1 += 10;
    } else if (pCtx->bCraftType == 0 || pCtx->bCraftType == 2) {
        *pnX1 += 10;
        *pnY1 += 20;
    }
}

/* --------------------------------------------------------------------------
 * Map_BuildDeployConfirmYesNo @ 0x18a68 - the deploy confirm dialog.
 * -------------------------------------------------------------------------- */

/* 0x18B5C..0x18CA0: the two passes share the bucket rule. The rect test is
 * inclusive on all four edges, and a type above 8 falls out of every bucket. */
static void Deploy_Bucket(const Soldier *pUnit, i32 nX0, i32 nY0, i32 nX1, i32 nY1,
                          DeployCounts *pOut)
{
    const MapCell *pCell = pUnit->pCell;
    bool bInside;
    u8 nType;

    if (pCell == NULL) {
        return;
    }
    bInside = (i32)pCell->nX >= nX0 && (i32)pCell->nX <= nX1 &&
              (i32)pCell->nY >= nY0 && (i32)pCell->nY <= nY1;
    nType = pUnit->nUnitType;
    if (nType < 4u) {
        if (bInside) {
            pOut->nSmallIn += 1;
        } else {
            pOut->nSmallOut += 1;
        }
    } else if (nType <= 8u) {
        if (bInside) {
            pOut->nBigIn += 1;
        } else {
            pOut->nBigOut += 1;
        }
    }
}

void Tact_CountDeployUnits(const DeployCountCtx *pCtx, DeployCounts *pOut)
{
    i32 nX0, nY0, nX1, nY1;
    CameraBoundsCtx Bounds;

    if (pCtx == NULL || pOut == NULL) {
        return;
    }
    pOut->nSmallIn = 0;
    pOut->nSmallOut = 0;
    pOut->nBigIn = 0;
    pOut->nBigOut = 0;

    /* The dialog always uses the craft rectangle: the original has no whole-map
     * branch, so bUseCraft is forced on. */
    Bounds = pCtx->Bounds;
    Bounds.bUseCraft = true;
    Tact_GetCameraBounds(&Bounds, &nX0, &nY0, &nX1, &nY1);

    if (pCtx->apXcom != NULL) {
        for (i32 i = 0; i < pCtx->nXcomCount; ++i) {
            if (pCtx->apXcom[i] != NULL) {
                Deploy_Bucket(pCtx->apXcom[i], nX0, nY0, nX1, nY1, pOut);
            }
        }
    }
    if (pCtx->apCiv != NULL) {
        for (i32 i = 0; i < pCtx->nCivCount; ++i) {
            const Soldier *pCiv = pCtx->apCiv[i];
            /* 0x18C24: a civilian with no hit points is not counted at all. */
            if (pCiv != NULL && pCiv->uHp != 0) {
                Deploy_Bucket(pCiv, nX0, nY0, nX1, nY1, pOut);
            }
        }
    }
}

static const char *Deploy_Text(const DeployConfirmHooks *pHooks, i32 nRecord)
{
    const char *sz = (pHooks->pfnTextRecord != NULL)
        ? pHooks->pfnTextRecord(nRecord, pHooks->pUser) : NULL;

    return (sz != NULL) ? sz : "";
}

i32 Map_BuildDeployConfirmYesNo(const DeployConfirmCtx *pCtx)
{
    DeployCounts Counts;

    if (pCtx == NULL || pCtx->pHooks == NULL) {
        return 9;
    }
    Tact_CountDeployUnits(&pCtx->Count, &Counts);

    /* 0x18CB8..0x18D6C: itoa + text 28 + itoa + text 29, then the tail copy. The
     * first message appends text record 30; the second appends unk_E900C, which
     * nothing ever writes, so it contributes nothing. */
    if (pCtx->aszMsg1 != NULL && pCtx->nMsgCap > 0) {
        snprintf(pCtx->aszMsg1, (size_t)pCtx->nMsgCap, "%d%s%d%s%s",
                 (int)Counts.nSmallIn, Deploy_Text(pCtx->pHooks, PUFO_TXTREC_DEPLOY_IN),
                 (int)Counts.nBigIn, Deploy_Text(pCtx->pHooks, PUFO_TXTREC_DEPLOY_JOIN),
                 Deploy_Text(pCtx->pHooks, PUFO_TXTREC_DEPLOY_TAIL));
    }
    if (pCtx->aszMsg2 != NULL && pCtx->nMsgCap > 0) {
        snprintf(pCtx->aszMsg2, (size_t)pCtx->nMsgCap, "%d%s%d%s",
                 (int)Counts.nSmallOut, Deploy_Text(pCtx->pHooks, PUFO_TXTREC_DEPLOY_IN),
                 (int)Counts.nBigOut, Deploy_Text(pCtx->pHooks, PUFO_TXTREC_DEPLOY_JOIN));
    }

    if (pCtx->pHooks->pfnPlaySample != NULL) {
        pCtx->pHooks->pfnPlaySample(16, pCtx->pHooks->pUser);
    }
    if (pCtx->pHooks->pfnModal != NULL) {
        pCtx->pHooks->pfnModal(170, 1069, 5, "YES", "NO", pCtx->pHooks->pUser);
    }
    /* 0x18DE0. */
    if (pCtx->pUi != NULL) {
        pCtx->pUi->nUiScreen = 9;
    }
    return 9;
}

void Tact_SelectUnit(TactSelectCtx *pCtx, Soldier *pUnit)
{
    /* One slot, read twice then written. The comparison and the park test are the same field; only
     * Hex-Rays renders them as two. */
    if (*pCtx->ppSelUnit != pUnit && Unit_bIsOperative(pUnit)) {
        (void)StatusMsg_Push(pCtx->pStatus,
                             Unit_FormatDisplayName(pUnit, 0, pCtx->panRankLow,
                                                    pCtx->panRankOther),
                             false, pCtx->pStatusHooks);
    }

    /* The slot is null only before the first selection of a mission, so that one selection does not
     * park the cursor. */
    if (*pCtx->ppSelUnit != NULL) {
        MapCursor_SetPos(pCtx->pCursorPos, -2, -2, pCtx->pfnLookup, pCtx->pLookupUser);
    }

    *pCtx->ppSelUnit = pUnit;

    MapCursor_SetPos(pCtx->pCursorPos, pUnit->pCell->nX, pUnit->pCell->nY,
                     pCtx->pfnLookup, pCtx->pLookupUser);
}

i32 Tact_SelectTeamUnits(TactTeamSelectEnv *pEnv)
{
    i32 nX = 0;
    i32 nY = 0;
    i32 nLayer = 0;

    /* 0x3975c: the record index is the mission layer count, and a zero count skips the table entirely. */
    if (pEnv->nMapLayers != 0) {
        const u8 *pRec = pEnv->pDeployOffsets + PUFO_DEPLOY_RECORD_SIZE * (i32)pEnv->nMapLayers;
        nX     = PUFO_CELLS_PER_TILE * (i32)pEnv->nCraftMapX + (i32)pRec[0];
        nY     = PUFO_CELLS_PER_TILE * (i32)pEnv->nCraftMapY + (i32)pRec[1];
        nLayer = (i32)pRec[2];
    }

    *pEnv->pnXcomCount = 0;

    for (i32 i = 0; i < pEnv->nSoldierCount; ++i) {
        Soldier *pUnit = pEnv->apSoldiers[i];
        bool bAccept;

        /* 0x397f8: Soldier+0x1B4 is cleared before any test, so a rejected soldier is still cleared. */
        pUnit->nUnk436 = 0;

        switch (pEnv->bMissionMode) {
        case 1: {
            /* ORIGINAL BUG (0x398B0), reproduced: the selected base byte is loaded with LDRSB and then
             * compared against Soldier.nKind, an unsigned byte. A base byte of 0x80 or more sign-extends
             * to a negative int and matches no nKind at all, so the team comes out empty. Base indices are
             * small in practice, which is why this has never been seen. */
            const u8 bCraft = pUnit->bUnk183;
            bAccept = ((i32)pUnit->nKind == (i32)pEnv->nSelectedBase) &&
                      bCraft != 254u &&
                      (bCraft == 255u || pEnv->apCraft[bCraft]->bAtBase != 0);
            break;
        }
        case 0:
        case 2:
        case 3:
            /* 0x3982c: the craft the mission launched from. */
            bAccept = (pUnit->bUnk183 == pEnv->nCraftIndex);
            break;
        default:
            /* 0x39844: anything above 3 accepts without testing the soldier. */
            bAccept = true;
            break;
        }

        if (!bAccept) {
            /* ORIGINAL BEHAVIOUR, reproduced: rejection ends the walk rather than skipping the soldier,
             * so one unsuitable soldier leaves every later soldier unprocessed. */
            break;
        }

        Unit_ResetBattleState(pUnit, 0, pUnit->nUnitType, pEnv->auStat, pEnv->nStatCount);
        pUnit->bStatus = 0;                                    /* STRB R2, [R3,#0x17] */
        Unit_PlaceOnMap(pUnit, nX, nY, nLayer, pEnv->pfnLookup, pEnv->pLookupUser);

        const u8 nCount = *pEnv->pnXcomCount;
        pEnv->apXcomUnits[nCount] = pUnit;
        *pEnv->pnXcomCount = (u8)(nCount + 1);
    }

    if (*pEnv->pnXcomCount != 0) {
        Tact_SelectUnit(pEnv->pSelectCtx, pEnv->apXcomUnits[0]);

        for (i32 i = 0; i < (i32)*pEnv->pnXcomCount; ++i) {
            (void)Unit_RecalcInventoryStat(pEnv->apXcomUnits[i], pEnv->pInvHooks);
        }
    }

    return (i32)*pEnv->pnXcomCount;
}

/* -------------------------------------------------- Tact_DeployTeamAndHwps @ 0x39980 */

/* The two hardware stat presets, ARM 0x39B94..0x39C0C. Objects 31, 32 and 33 take the first;
 * 34 and 35 take the second. The original reaches them with two ifs and a goto into the tail of
 * the second, which is just a way of covering 31..35 with no third case. */
static void Deploy_ApplyHwpsPreset(Soldier *pUnit, i32 nObjectIdx)
{
    u8 bUnk248;

    if (nObjectIdx <= 33) {
        pUnit->auStat[0] = 70;
        pUnit->auStat[1] = 90;
        pUnit->auStat[2] = 110;
        pUnit->auStat[3] = 20;
        pUnit->auStat[9] = 70;
        bUnk248 = 40;
    } else {
        pUnit->auStat[0] = 100;
        pUnit->auStat[1] = 90;
        pUnit->auStat[2] = 110;
        pUnit->auStat[3] = 30;
        pUnit->auStat[9] = 100;
        bUnk248 = 55;
    }

    pUnit->bMorale = 100;
    pUnit->auStat[4] = 60;
    pUnit->auStat[8] = 60;
    pUnit->bUnk422 = 0;
    pUnit->bUnk248 = bUnk248;
}

/* A hardware soldier is named after the object it carries: the name is the ObjectRec's szName,
 * which starts at record offset 0. The original copies until the NUL with no bound, and
 * Soldier.szName is 40 bytes, so an object name of 40 bytes or more runs off the end.
 * Reproduced as-is. */
static void Deploy_NameFromObject(Soldier *pUnit, const ObjectRecFull *pObj)
{
    const char *pSrc = pObj->szName;
    char *pDst = pUnit->szName;
    u8 bByte;

    do {
        bByte = *pSrc++;
        *pDst++ = (char)bByte;
    } while (bByte != 0);
}

/* The four 2x2 blocks phase 5 writes per matching tile, as offsets from the tile's own cell
 * anchor. Read straight off the store sequence at 0x3A0A4..0x3A184, which fills 16 pairs in the
 * order low-X/low-Y, low-X/high-Y, high-X/low-Y, high-X/high-Y for each block. */
static const i8 s_aScatterXOff[4][2] = { { -1, 0 }, { -1, 0 }, { 3, 4 }, { 3, 4 } };
static const i8 s_aScatterYOff[4][2] = { { 1, 2 }, { 5, 6 }, { 1, 2 }, { 5, 6 } };

i32 Tact_DeployTeamAndHwps(TactDeployEnv *pEnv)
{
    if (pEnv == NULL || pEnv->create.apSoldiers == NULL || pEnv->create.pnSoldierCount == NULL ||
        pEnv->apObjects == NULL || pEnv->apTactItems == NULL || pEnv->pnTactItemCount == NULL ||
        pEnv->apXcomUnits == NULL || pEnv->pnXcomUnitCount == NULL || pEnv->pnDeployedHwps == NULL ||
        pEnv->pDeployOffsets == NULL || pEnv->pHwpsOffsets == NULL ||
        pEnv->pDeployPointCounts == NULL || pEnv->pfnLookupCell == NULL) {
        return 0;
    }

    /* Phase 1, 0x3999C: park every unit already in the list at cell (1,1,0). */
    for (i32 i = 0; i < *pEnv->pnXcomUnitCount; ++i) {
        Unit_PlaceOnMap(pEnv->apXcomUnits[i], 1, 1, 0, pEnv->pfnLookupCell, pEnv->pLookupUser);
    }

    *pEnv->pnDeployedHwps = 0;                       /* 0x399E8 */

    /* Deliberately outside the loop: object index 33 never assigns it, so the split below reuses
     * whatever the previous object index left. */
    i32 nAmmoTotal = 0;

    for (i32 nObjectIdx = PUFO_CRAFT_STORE_FIRST; nObjectIdx <= PUFO_CRAFT_STORE_LAST; ++nObjectIdx) {
        ObjectRecFull *pObj = pEnv->apObjects[nObjectIdx];
        i32 nSpawn = (i32)pObj->uRecovScore;         /* 0x39A24: LDRH R11, [R3,#0xCA] */

        /* 0x39A14. Hex-Rays renders this as a base-record byte, but ARM is LDRH count /
         * LDR apSoldiers[count - 1] / LDRB [R3,#0xA0] / CMP #6: the last soldier in the roster has
         * nUnitType 6. That is the soldier the previous object index created, because
         * nUnitType = object index - 27 and 33 - 27 = 6. */
        i32 nSoldierCount = *pEnv->create.pnSoldierCount;
        Soldier *pLast = (nSoldierCount > 0) ? pEnv->create.apSoldiers[nSoldierCount - 1] : NULL;

        if (pLast != NULL && pLast->nUnitType == 6) {
            /* ORIGINAL BUG (0x39A5C), reproduced: this writes into apTactItems[nTactItemCount], a
             * slot this iteration has not created yet. */
            pEnv->apTactItems[*pEnv->pnTactItemCount]->nAmmo = 0;
        } else {
            switch (nObjectIdx) {
            case 31: {
                i32 nScore = (i32)pEnv->apObjects[56]->uRecovScore;
                nAmmoTotal = (nScore < 30 * nSpawn) ? nScore : 30 * nSpawn;
                break;
            }
            case 32: {
                i32 nScore = (i32)pEnv->apObjects[57]->uRecovScore;
                nAmmoTotal = (nScore < 8 * nSpawn) ? nScore : 8 * nSpawn;
                break;
            }
            case 34: {
                /* The score is read from object 105 and the deduction is taken from object 58. */
                i32 nScore = (i32)pEnv->apObjects[105]->uRecovScore;
                nAmmoTotal = (nScore < 5 * nSpawn) ? 10 * nScore : 50 * nSpawn;
                pEnv->apObjects[58]->uRecovScore =
                    (u16)(pEnv->apObjects[58]->uRecovScore - nAmmoTotal / 10);
                break;
            }
            case 35: {
                i32 nScore = (i32)pEnv->apObjects[58]->uRecovScore;
                nAmmoTotal = (nScore < 8 * nSpawn) ? nScore : 8 * nSpawn;
                break;
            }
            default:
                /* ORIGINAL BUG (0x39B20), reproduced: object index 33 has no case, so the ammo
                 * total carried into this iteration is the one used below. */
                break;
            }
        }

        for (i32 nCopy = 0; nCopy < nSpawn; ++nCopy) {
            Unit_CreateAndClassify(&pEnv->create, 0);

            i32 nCount = *pEnv->create.pnSoldierCount;
            Soldier *pUnit = pEnv->create.apSoldiers[nCount - 1];
            if (pUnit == NULL) {
                /* Deviation: the original stores whatever operator new returned and dereferences
                 * it here without a test. */
                continue;
            }

            pUnit->nUnitType = (u8)(nObjectIdx - 27);
            /* The original passes only the unit and facing 0; the port's signature also carries the
             * unit type and stat table for the armour tail, which types 2 and 3 alone reach. */
            Unit_ResetBattleState(pUnit, 0, pUnit->nUnitType, pUnit->auStat, PUFO_UNIT_STAT_COUNT);
            pUnit->bStatus = 0;

            Deploy_ApplyHwpsPreset(pUnit, nObjectIdx);
            pUnit->uHp = pUnit->auStat[1];
            pUnit->uTu = pUnit->auStat[0];
            pUnit->uStunOrArmor = pUnit->auStat[9];
            Deploy_NameFromObject(pUnit, pObj);

            i32 nDeployed = *pEnv->pnDeployedHwps;

            if (pEnv->nMapLayers != 0) {
                const u8 *pHw = pEnv->pHwpsOffsets + 4 * nDeployed + 16 * (pEnv->nMapLayers - 1);
                const u8 *pDep = pEnv->pDeployOffsets + 4 * nDeployed + 104 * (pEnv->nMapLayers - 1);

                Unit_PlaceOnMap(pUnit,
                                PUFO_CELLS_PER_TILE * (i32)pEnv->nCraftMapX + (i32)pHw[0],
                                PUFO_CELLS_PER_TILE * (i32)pEnv->nCraftMapY + (i32)pHw[1],
                                (i32)pDep[2],
                                pEnv->pfnLookupCell, pEnv->pLookupUser);
            } else {
                (void)Rmp_PickSpawnForUnit(pEnv->pRmp, pUnit, pEnv->pfnLookupCell,
                                           pEnv->pLookupUser,
                                           pEnv->nMissionType == PUFO_MISSION_TYPE_XCOM_BASE);
            }

            TactItem *pItem = (TactItem *)malloc(sizeof(TactItem));
            if (pItem != NULL) {
                TactItem_ctor(pItem);
            }

            i32 nItemIdx = *pEnv->pnTactItemCount;
            pEnv->apTactItems[nItemIdx] = pItem;
            pEnv->apTactItems[nItemIdx]->pObj = pObj;

            /* Every copy gets the even share; the last one takes the whole remainder. */
            i32 nAmmo = (nCopy == nSpawn - 1) ? (nAmmoTotal % nSpawn + nAmmoTotal / nSpawn)
                                              : (nAmmoTotal / nSpawn);
            pEnv->apTactItems[nItemIdx]->nAmmo = (u8)nAmmo;
            pEnv->apTactItems[nItemIdx]->nIndex = pObj->uSortKey;

            /* Both hands are the same item. */
            pUnit->pHandL = pEnv->apTactItems[nItemIdx];
            pUnit->pHandR = pEnv->apTactItems[nItemIdx];
            *pEnv->pnTactItemCount = nItemIdx + 1;

            pEnv->apXcomUnits[*pEnv->pnXcomUnitCount] = pUnit;
            *pEnv->pnXcomUnitCount = (u8)(*pEnv->pnXcomUnitCount + 1);
            *pEnv->pnDeployedHwps = nDeployed + 1;
        }
    }

    /* Phase 3, 0x39E9C: re-place the units that phase 1 parked. The bound is the roster minus the
     * deployed count, so only the pre-existing team is moved. */
    i32 nToPlace = *pEnv->pnXcomUnitCount - *pEnv->pnDeployedHwps;

    for (i32 i = 0; i < nToPlace; ++i) {
        if (pEnv->nMapLayers == 0) {
            (void)Rmp_PickSpawnForUnit(pEnv->pRmp, pEnv->apXcomUnits[i], pEnv->pfnLookupCell,
                                       pEnv->pLookupUser,
                                       pEnv->nMissionType == PUFO_MISSION_TYPE_XCOM_BASE);
            continue;
        }

        i32 nPoints = (i32)pEnv->pDeployPointCounts[pEnv->nMapLayers - 1];
        i32 nPoint = 0;
        i32 nCellX = 0;
        i32 nCellY = 0;
        i32 nLayer = 0;
        bool bFound = false;

        while (nPoint < nPoints) {
            const u8 *pRec = pEnv->pDeployOffsets + 4 * nPoint + 104 * pEnv->nMapLayers;

            nCellX = PUFO_CELLS_PER_TILE * (i32)pEnv->nCraftMapX + (i32)pRec[-104 + 0];
            nCellY = PUFO_CELLS_PER_TILE * (i32)pEnv->nCraftMapY + (i32)pRec[-104 + 1];
            nLayer = (i32)pRec[-104 + 2];

            MapCell *pCell = pEnv->pfnLookupCell(nCellX, nCellY, nLayer, pEnv->pLookupUser);
            if (pCell == NULL || !MapCell_HasOccupant(pCell)) {
                bFound = true;
                break;
            }
            ++nPoint;
        }

        if (bFound) {
            /* 0x39FBC: the facing comes from the fourth byte of the same entry. */
            pEnv->apXcomUnits[i]->bFacing =
                pEnv->pDeployOffsets[4 * nPoint + 104 * pEnv->nMapLayers - 104 + 3];
            Unit_PlaceOnMap(pEnv->apXcomUnits[i], nCellX, nCellY, nLayer,
                            pEnv->pfnLookupCell, pEnv->pLookupUser);
        }
    }

    /* Phase 4, 0x3A024. */
    if (*pEnv->pnXcomUnitCount != 0 && pEnv->pSelect != NULL) {
        Tact_SelectUnit(pEnv->pSelect, pEnv->apXcomUnits[0]);
    }

    /* Phase 5, 0x3A038: only a mission with no stacked layers scatters its items. */
    if (pEnv->nMapLayers != 0 || pEnv->panScatterMid == NULL || pEnv->panScatterLast == NULL ||
        pEnv->pnScatterCount == NULL) {
        return *pEnv->pnTactItemCount;
    }

    *pEnv->pnScatterCount = 0;

    if (pEnv->nMissionType == PUFO_MISSION_TYPE_XCOM_BASE && pEnv->pTileTypes != NULL) {
        /* Six tile rows by six tile columns. The cell anchor is tile * 10 + 2 across, and tile * 10
         * down, and every matching tile contributes four 2x2 blocks. */
        const u8 *pTile = pEnv->pTileTypes;
        i32 nMid = 2;

        for (i32 nRow = 0; nRow < 6; ++nRow) {
            i32 nLast = 0;

            for (i32 nCol = 0; nCol < 6; ++nCol) {
                if (*pTile == PUFO_SCATTER_TILE_TYPE) {
                    i32 nBase = *pEnv->pnScatterCount;

                    for (i32 b = 0; b < 4; ++b) {
                        for (i32 k = 0; k < 2; ++k) {
                            for (i32 m = 0; m < 2; ++m) {
                                pEnv->panScatterMid[nBase + b * 4 + k * 2 + m] =
                                    nMid + (i32)s_aScatterXOff[b][k];
                                pEnv->panScatterLast[nBase + b * 4 + k * 2 + m] =
                                    nLast + (i32)s_aScatterYOff[b][m];
                            }
                        }
                    }
                    *pEnv->pnScatterCount = nBase + 16;
                }
                ++pTile;
                nLast += 10;
            }
            nMid += 10;
        }
    } else {
        /* Three cells in a column near the map edge. */
        pEnv->panScatterMid[0] = 55; pEnv->panScatterLast[0] = 55;
        pEnv->panScatterMid[1] = 55; pEnv->panScatterLast[1] = 45;
        pEnv->panScatterMid[2] = 55; pEnv->panScatterLast[2] = 35;
        *pEnv->pnScatterCount = 3;
    }

    /* Every tactical item is taken off the head of cell (0,0,0) and inserted by priority onto a
     * layer-1 cell, cycling through the table. The walk stops as soon as the source cell runs dry,
     * which is what makes the item count and the scatter count need not agree. */
    i32 nSlot = 0;

    for (i32 j = 0; j < *pEnv->pnTactItemCount; ++j) {
        MapCell *pFrom = pEnv->pfnLookupCell(0, 0, 0, pEnv->pLookupUser);

        if (pFrom == NULL || pFrom->pItemList == NULL) {
            break;
        }

        TactItem *pItem = pFrom->pItemList;
        MapCell_ListRemove(pFrom, pItem);

        MapCell *pTo = pEnv->pfnLookupCell(pEnv->panScatterLast[nSlot], pEnv->panScatterMid[nSlot], 1,
                                           pEnv->pLookupUser);
        if (pTo != NULL) {
            MapCell_ListInsertByPri(pTo, pItem);
        }

        ++nSlot;
        if (nSlot == *pEnv->pnScatterCount) {
            nSlot = 0;
        }
    }

    return *pEnv->pnTactItemCount;
}

/* ------------------------------------------------------------ alien mission tables
 *
 * The static data Tact_SpawnAliens (0x3a2ac) reads. Dumped from pufobeta.exe by
 * tools/gen_alien_tables.py and checked byte for byte by docs/parity/audit_tables.py. Extents come
 * from the image, not from the pseudocode; the reasoning is in docs/parity/game-core.md.
 *
 * Index shapes, all proven from ARM in Tact_SpawnAliens:
 *   s_aSpriteSetName            9 * nUnitType                              0x3AB04
 *   s_aAlienRaceCounts          3 * nTerrainVariant + nAlienTier - 3       0x3A47C
 *   s_aAlienSecondaryCounts     same index                                 0x3A484
 *   s_aAlienRaceComposition     5 * nAlienSetIndex + i, i = 0..4           0x3A34C
 *   s_aAlienRankTypes           i, the composition slot that was spent     0x3A83C
 *   s_aAlienMissionTotals       nAlienSetIndex                             0x3AB44
 *   s_aAlienCompositionDivisor  i, 0..7                                    0x3AB6C
 *   s_aAlienBeltObjects         7 - k, k = 0..7                            0x3AD5C
 *   s_aAlienUnitStats           32 * (nUnitType - 9)                       0x3A934
 */

/* aXcom0 @ 0xa7be8. One nine-byte sprite-set name per unit type, 23 entries. Entry 23 does not
 * exist: the byte at 0xA7CB7 is 00 and the bytes after it are 2E 00 22 01 "push". Types 4..8 all
 * read "tanks", which is why the deploy code can treat them as one group. */
static const char s_aSpriteSetName[PUFO_ALIEN_SPRITE_SET_NAME_COUNT][9] = {
    /*  0 xcom_0   */ { 120,  99, 111, 109,  95,  48,   0,   0,   0 },
    /*  1 xcom_1   */ { 120,  99, 111, 109,  95,  49,   0,   0,   0 },
    /*  2 xcom_2   */ { 120,  99, 111, 109,  95,  50,   0,   0,   0 },
    /*  3 xcom_3   */ { 120,  99, 111, 109,  95,  51,   0,   0,   0 },
    /*  4 tanks    */ { 116,  97, 110, 107, 115,   0,   0,   0,   0 },
    /*  5 tanks    */ { 116,  97, 110, 107, 115,   0,   0,   0,   0 },
    /*  6 tanks    */ { 116,  97, 110, 107, 115,   0,   0,   0,   0 },
    /*  7 tanks    */ { 116,  97, 110, 107, 115,   0,   0,   0,   0 },
    /*  8 tanks    */ { 116,  97, 110, 107, 115,   0,   0,   0,   0 },
    /*  9 sectoid  */ { 115, 101,  99, 116, 111, 105, 100,   0,   0 },
    /* 10 muton    */ { 109, 117, 116, 111, 110,   0,   0,   0,   0 },
    /* 11 floater  */ { 102, 108, 111,  97, 116, 101, 114,   0,   0 },
    /* 12 snakeman */ { 115, 110,  97, 107, 101, 109,  97, 110,   0 },
    /* 13 ethereal */ { 101, 116, 104, 101, 114, 101,  97, 108,   0 },
    /* 14 chrys    */ {  99, 104, 114, 121, 115,   0,   0,   0,   0 },
    /* 15 celatid  */ {  99, 101, 108,  97, 116, 105, 100,   0,   0 },
    /* 16 civf     */ {  99, 105, 118, 102,   0,   0,   0,   0,   0 },
    /* 17 civm     */ {  99, 105, 118, 109,   0,   0,   0,   0,   0 },
    /* 18 cyber    */ {  99, 121,  98, 101, 114,   0,   0,   0,   0 },
    /* 19 silacoid */ { 115, 105, 108,  97,  99, 111, 105, 100,   0 },
    /* 20 x_reap   */ { 120,  95, 114, 101,  97, 112,   0,   0,   0 },
    /* 21 x_rob    */ { 120,  95, 114, 111,  98,   0,   0,   0,   0 },
    /* 22 zombie   */ { 122, 111, 109,  98, 105, 101,   0,   0,   0 }
};

/* aAlienRaceCounts @ 0xa6618 and aAlienSecondaryCounts @ 0xa6658. Both are 16 bytes and both are
 * indexed 3 * nTerrainVariant + nAlienTier - 3, so five terrain rows of three tiers. The secondary
 * column is only read when the lead race is neither 10 nor 12. The two are parts of one 80-byte
 * block: 0xa6618 + 16 = s_aAlienRaceComposition, + 56 = s_aAlienRankTypes, + 64 =
 * s_aAlienSecondaryCounts, and the block ends at 0xa6668 where "plane" begins. Because they are
 * contiguous in the image, an index past the end of one reads the other; the port keeps them as
 * separate arrays, so that only differs for an index the game's own data never produces. */
static const u8 s_aAlienRaceCounts[16] = {
       1,    0,    0,
       3,    5,    0,
       7,   10,    0,
      13,   15,    0,
      20,    0,    0,
       0
};

static const u8 s_aAlienSecondaryCounts[16] = {
       0,    0,    0,
       0,    0,    0,
       0,    0,    0,
       1,    1,    0,
       2,    0,    0,
       0
};

/* aAlienRaceComposition @ 0xa6628. Eight rows of five, selected by the alien set index. Row 0 and
 * row 7 are all zero, so those mission sets spawn no aliens through this path. The five slots are
 * the race groups; s_aAlienRankTypes gives the bRankType each group gets. */
static const u8 s_aAlienRaceComposition[8][5] = {
    {     0,     0,     0,     0,     0 },
    {     1,     0,     0,     0,     0 },
    {     2,     2,     1,     0,     1 },
    {     2,     2,     0,     1,     1 },
    {     3,     3,     0,     1,     1 },
    {     0,     0,     0,     0,     0 },
    {     0,     0,     0,     1,     1 },
    {     0,     0,     0,     0,     0 },
};

static const u8 s_aAlienRankTypes[5] = {
       3,    5,    7,    4,    8
};

/* aAlienMissionTotals @ 0xaa020 and aAlienCompositionDivisor @ 0xaa030, both eight entries. The
 * recomposed count is (totals[setIndex] + nAlienPoolAddend) / divisor[i]. No divisor is zero. */
static const u16 s_aAlienMissionTotals[8] = {
       1,   35,  100,  300,  400,  350,  400,  300
};

static const u16 s_aAlienCompositionDivisor[8] = {
       1,  100,    1,    1,   50,  100,  250,  400
};

/* aAlienBeltObjects @ 0xaa008. Eight signed halfwords walked backwards, paired with a counter
 * running 14, 12, 10, 8, 6, 4, 2, 0. The -1 sentinel at index 2 is skipped by the counter reaching
 * 4, not by a test on the value. */
static const i16 s_aAlienBeltObjects[8] = {
      18,   23,   -1,    3,    6,   11,   12,   13
};

/* aAlienUnitStats @ 0xa8f38. Fourteen 32-byte records, index nUnitType - 9, so unit types 9 through
 * 22. 14 * 32 = 448 ends at 0xa90f8, where the ASCII "MAN_0M0" name block begins, which closes the
 * extent at both ends. */
static const AlienUnitStatRec s_aAlienUnitStats[14] = {
    {    45,    30,    50,    30,    20,    20,    -1,    -1,     5, {     0,     0,     0,     0,     0 },    30,    40 },
    {    55,    70,   120,    30,   150,    30,     0,     0,    20, {    50,    30,    20,    40,    50 },    40,    50 },
    {    60,    50,    70,    40,   100,    40,     0,     0,    10, {    10,    10,     0,    20,    10 },    50,    50 },
    {    55,    45,    80,    40,    90,    40,     0,     0,    10, {     0,     0,    50,    10,    10 },    55,    40 },
    {    40,    30,    40,    60,    50,    45,    -1,    -1,    10, {    10,    10,    10,    40,    10 },    45,    35 },
    {    80,    60,   100,    40,   110,     0,     0,     0,    15, {     5,     5,    20,     5,     5 },     0,     0 },
    {    40,    12,    40,    20,    40,     0,    10,    10,    40, {     0,     0,     0,     0,     0 },     0,     0 },
    {    45,    30,    50,    30,    40,    20,     0,     0,     5, {     0,     0,     0,     0,     0 },     0,     0 },
    {    45,    30,    50,    30,    40,    20,     0,     0,     5, {     0,     0,     0,     0,     0 },     0,     0 },
    {    90,   100,    -1,  1000,   200,    60,     0,     0,    30, {    10,    10,    50,    50,    50 },    60,     0 },
    {    40,    12,    40,    20,    40,     0,    10,    10,     0, {     0,     0,   100,     0,     0 },     0,     0 },
    {    80,    80,   160,  1000,   180,    -1,     0,     0,    15, {    10,    10,     0,    10,    10 },     0,     0 },
    {    90,   100,    -1,  1000,   200,    60,     0,     0,    40, {    30,    30,    70,    50,    50 },    60,     0 },
    {    45,    30,    50,    30,   150,    20,     0,     0,     5, {     0,     0,     0,     0,     0 },     0,     0 },
};

/* --- Tact_SpawnAliens helpers --- */

/* The gate on the hand-weapon branch at 0x3AE68. ARM is
 *   LDRH R3, [R9,#0x1A4]   pUnit->uInvBurden
 *   LDRB R2, [R6,#0xA5]    pObj->bWeight
 *   LDRH R0, [R9,#0xB2]    pUnit->auStat[8]
 *   ADD  R3, R2, R3 / MOV R1, R3, LSL#2
 *   BL _rt_sdiv            R0 = divisor, R1 = dividend, quotient back in R0
 * and the item is given only when the quotient is zero. _rt_sdiv returns 0 for a zero divisor;
 * C would trap, so the zero case is written out. */
static i32 Spawn_BurdenQuotient(const Soldier *pUnit, const ObjectRecFull *pObj)
{
    i32 nNumerator = 4 * ((i32)pObj->bWeight + (i32)pUnit->uInvBurden);
    i32 nDivisor = (i32)pUnit->auStat[8];

    return (nDivisor == 0) ? 0 : nNumerator / nDivisor;
}

/* The name copy at 0x3A8FC: a NUL-terminated copy out of the 40-byte-per-unit-type name block at
 * 0xB1140, which is .bss and filled at load time, so it is injected rather than dumped. */
static void Spawn_CopyUnitName(Soldier *pUnit, const char *panUnitName)
{
    const char *pSrc = panUnitName + 40 * (i32)pUnit->nUnitType;
    char *pDst = pUnit->szName;
    char nByte;

    do {
        nByte = *pSrc++;
        *pDst++ = nByte;
    } while (nByte != 0);
}

/* Unit_UpdateAnimPose @ 0x4978c needs the two hands' object fields, which TactItem.pObj carries on
 * the full record. Rebuilt from the unit's own hands after each item is handed out. */
static void Spawn_UpdatePose(Soldier *pUnit)
{
    AnimHandInfo handL = { false, 0, 0 };
    AnimHandInfo handR = { false, 0, 0 };

    if (pUnit->pHandL != NULL) {
        handL.bPresent = true;
        if (pUnit->pHandL->pObj != NULL) {
            handL.bListPri = pUnit->pHandL->pObj->bListPri;
            handL.uSortKey = pUnit->pHandL->pObj->uSortKey;
        }
    }
    if (pUnit->pHandR != NULL) {
        handR.bPresent = true;
        if (pUnit->pHandR->pObj != NULL) {
            handR.bListPri = pUnit->pHandR->pObj->bListPri;
            handR.uSortKey = pUnit->pHandR->pObj->uSortKey;
        }
    }

    Unit_UpdateAnimPose(pUnit, &handL, &handR);
}

i32 Tact_SpawnAliens(TactSpawnAliensEnv *pEnv)
{
    if (pEnv == NULL || pEnv->create.apSoldiers == NULL || pEnv->create.pnSoldierCount == NULL ||
        pEnv->create.pnNextSoldierId == NULL || pEnv->apEnemyUnits == NULL ||
        pEnv->pnEnemyUnitCount == NULL || pEnv->apHiddenSoldiers == NULL ||
        pEnv->pnEnemyReserveCount == NULL || pEnv->apTactItems == NULL ||
        pEnv->pnTactItemCount == NULL || pEnv->apObjects == NULL || pEnv->pFont == NULL ||
        pEnv->panUnitName == NULL) {
        return 0;
    }

    /* The eight-slot composition array, at SP+0x70 in the original. Slots 0..4 hold the race groups
     * seeded here; all eight are overwritten at 0x3AB6C before the equipment walk. */
    i32 anComposition[8];

    /* --- 1. which alien set, and the composition seed -------------------------------------- */

    *pEnv->pnEnemyReserveCount = 0;                                       /* 0x3A2C8 */

    i32 nSetIndex;
    switch (pEnv->nMissionMode) {
    case 0: nSetIndex = (i32)pEnv->nTerrainVariant - 1; break;           /* 0x3A31C */
    case 1: nSetIndex = 7; break;                                        /* 0x3A30C */
    case 2: nSetIndex = 6; break;                                        /* 0x3A304 */
    case 3: nSetIndex = 5; break;                                        /* 0x3A2F4, R2 preloaded to 5 */
    default: nSetIndex = pEnv->nAlienSetIndex; break;                    /* 0x3A2F0: never written */
    }
    if (pEnv->nMissionType == PUFO_MISSION_TYPE_MARS_SPECIAL) {
        nSetIndex = 5;                                                   /* 0x3A338 */
    }

    for (i32 i = 0; i < 5; ++i) {
        anComposition[i] = s_aAlienRaceComposition[nSetIndex][i];        /* 0x3A34C */
    }

    *pEnv->pnEnemyUnitCount = 0;                                         /* 0x3A374 */
    if (pEnv->abSpriteSet[0] == 10) {
        anComposition[3] = 0;                                            /* 0x3A37C */
        anComposition[4] = 0;
    }

    /* --- 2. the three counts --------------------------------------------------------------- */

    i32 nPrimary;      /* v5 */
    i32 nSecondary;    /* v6 */
    i32 nReserve;      /* v7 */

    if (pEnv->nMissionType == PUFO_MISSION_TYPE_URBAN) {                 /* 0x3A388 */
        nPrimary   = 15 - (i32)Game_RandU16(5);
        nSecondary = 5  - (i32)Game_RandU16(3);
        nReserve   = (i32)Game_RandU16(5) + 5;
    } else if (pEnv->nMissionType == PUFO_MISSION_TYPE_XCOM_BASE) {      /* 0x3A3E0 */
        nSecondary = 3  - (i32)Game_RandU16(3);                          /* this order, not the one above */
        nPrimary   = 15 - (i32)Game_RandU16(5);
        nReserve   = 0;
    } else if (pEnv->nMissionType == PUFO_MISSION_TYPE_ALIEN_BASE ||
               pEnv->nMissionType == PUFO_MISSION_TYPE_MARS_SPECIAL) {   /* 0x3A41C */
        nSecondary = 4  - (i32)Game_RandU16(3);
        nPrimary   = 20 - (i32)Game_RandU16(5);
        nReserve   = 0;
    } else {                                                             /* 0x3A44C */
        bool bLeadFixed = (pEnv->abSpriteSet[0] == 10 || pEnv->abSpriteSet[0] == 12);
        i32 nRow = 3 * (i32)pEnv->nTerrainVariant + (i32)pEnv->nAlienTier - 3;
        nPrimary   = s_aAlienRaceCounts[nRow];
        nSecondary = bLeadFixed ? 0 : s_aAlienSecondaryCounts[nRow];
        nReserve   = 0;
    }

    if (pEnv->abSpriteSet[0] == 10 || pEnv->abSpriteSet[0] == 12) {
        nSecondary = 0;                                                  /* 0x3A4A4 */
    }

    /* --- 3. split the primary count into the three race groups ----------------------------- */

    u8 nRace1 = pEnv->abSpriteSet[1];
    u8 nRace2 = pEnv->abSpriteSet[2];
    bool bRace1Open = (nRace1 == PUFO_SPRITE_SET_NONE || g_aUnitTypeIsLarge[nRace1] == 1);
    bool bRace2Open = (nRace2 == PUFO_SPRITE_SET_NONE || g_aUnitTypeIsLarge[nRace2] == 1);
    i32 nGroup1, nGroup2, nGroup3;

    nGroup3 = 0;                                                         /* 0x3A4D4 */

    if ((pEnv->nTerrainVariant < 4 && pEnv->nTerrainVariant != 0) ||
        (bRace1Open && bRace2Open)) {
        /* 0x3A4F0: no scaling at all, the whole primary count goes to the first group. */
        nGroup1 = nPrimary;
    } else {
        /* 0x3A538..0x3A574. The double is 0x3FE99999_9999999A = 0.8, not 0.2: the itos/stod round
         * trip is numerically a no-op and only costs the text path. */
        nGroup1 = (i32)((double)nPrimary * 0.8);
        if (nGroup1 < 1) {
            nGroup1 = 1;                                                 /* 0x3A584 */
        }
    }

    if (bRace1Open || bRace2Open) {
        nGroup2 = nPrimary - nGroup1;                                    /* 0x3A620 */
    } else {
        nGroup2 = (nPrimary - nGroup1) / 2;                              /* 0x3A60C */
        nGroup3 = nPrimary - nGroup2 - nGroup1;                          /* 0x3A614 */
    }

    if (nRace1 != PUFO_SPRITE_SET_NONE && g_aUnitTypeIsLarge[nRace1] == 1) {
        nGroup2 = nSecondary;                                            /* 0x3A638 */
    }

    /* --- 4. the stealth budget ------------------------------------------------------------- */

    i32 nStealth = 0;
    if ((i32)pEnv->nAlienPoolScale > 20) {                               /* 0x3A648 */
        nStealth = (i32)(((double)((i32)pEnv->nAlienPoolScale - 20) * (double)nPrimary) * 0.01);
    }
    if (nStealth == nPrimary) {
        nStealth = nPrimary - 1;                                         /* 0x3A6E4 */
    }

    /* --- 5. spawn the squad ---------------------------------------------------------------- */

    i32 nTotal = nReserve + nSecondary + nPrimary;                       /* 0x3A6F8 */

    for (i32 i = 0; i < nTotal; ++i) {
        Unit_CreateAndClassify(&pEnv->create, 0);

        i32 nRoster = *pEnv->create.pnSoldierCount;
        Soldier *pUnit = (nRoster > 0) ? pEnv->create.apSoldiers[nRoster - 1] : NULL;
        if (pUnit == NULL) {
            /* Deviation: the original stores and dereferences whatever operator new returned. */
            continue;
        }

        if (pEnv->nMissionType == PUFO_MISSION_TYPE_XCOM_BASE) {
            pEnv->apHiddenSoldiers[*pEnv->pnEnemyReserveCount] = pUnit;  /* 0x3A7B8 */
            *pEnv->pnEnemyReserveCount = *pEnv->pnEnemyReserveCount + 1;
        } else {
            pEnv->apEnemyUnits[*pEnv->pnEnemyUnitCount] = pUnit;         /* 0x3A760 */
            *pEnv->pnEnemyUnitCount = (u8)(*pEnv->pnEnemyUnitCount + 1);
        }

        pUnit->nLook = 0;                                                /* 0x3A7E4 */
        u8 bRank = 2;
        pUnit->bFemale = 0;                                              /* 0x3A7EC */

        if (!Unit_bIsLarge(pUnit)) {
            i32 k = 0;
            while (anComposition[k] <= 0) {
                ++k;
                if (k >= 5) {
                    goto NoRank;                                         /* 0x3A820: rank stays 2 */
                }
            }
            --anComposition[k];                                          /* 0x3A834 */
            bRank = s_aAlienRankTypes[k];                                /* 0x3A83C */
        }
    NoRank:
        pUnit->bRankType = bRank;
        pUnit->bStatus = 1;                                              /* 0x3A84C */

        /* Race by position. Group 4 tests race 2, not race 3 - ARM keeps race[2] in R0 through
         * 0x3A8D4, and only loads Map_abSpriteSet+3 at 0x3A8DC when that test passes. */
        u8 nRace;
        if (i < nGroup1) {
            nRace = pEnv->abSpriteSet[0];                                /* 0x3A858 */
        } else if (i < nGroup1 + nGroup2 && pEnv->abSpriteSet[1] != PUFO_SPRITE_SET_NONE) {
            nRace = pEnv->abSpriteSet[1];                                /* 0x3A87C */
        } else if (i < nGroup1 + nGroup2 + nGroup3 &&
                   pEnv->abSpriteSet[2] != PUFO_SPRITE_SET_NONE) {
            nRace = pEnv->abSpriteSet[2];                                /* 0x3A8A0 */
        } else if (i < nGroup1 + nGroup2 + nGroup3 + pEnv->nFourthRaceCount &&
                   pEnv->abSpriteSet[2] != PUFO_SPRITE_SET_NONE) {
            nRace = pEnv->abSpriteSet[3];                                /* 0x3A8DC */
        } else {
            nRace = (u8)(Game_RandU16(2) + 16);                          /* 0x3A8F8 */
        }
        pUnit->nUnitType = nRace;

        Spawn_CopyUnitName(pUnit, pEnv->panUnitName);                    /* 0x3A8FC */

        /* Stats. The original computes aAlienUnitStats + 32 * nUnitType - 288, so a unit type below
         * 9 or above 22 reads outside the table (below it lies aInvHitRects). The port applies the
         * record only for the 14 types the table actually covers. */
        if (pUnit->nUnitType >= 9 && pUnit->nUnitType <= 22) {
            const AlienUnitStatRec *pRec = &s_aAlienUnitStats[pUnit->nUnitType - 9];

            pUnit->auStat[0] = (u16)pRec->nTu;                           /* 0x3A93C */
            pUnit->uTu       = (u16)pRec->nTu;                           /* 0x3A940 */
            pUnit->auStat[9] = (u16)pRec->nStat9;                       /* 0x3A94C */
            pUnit->auStat[1] = (u16)(pRec->nHp    + (i32)Game_RandU16(20) - 10);  /* 0x3A964 */
            pUnit->auStat[2] = (u16)(pRec->nStat2 + (i32)Game_RandU16(20) - 10);  /* 0x3A97C */
            pUnit->auStat[3] = (u16)(pRec->nStat3 + (i32)Game_RandU16(20) - 10);  /* 0x3A994 */
            pUnit->auStat[4] = (u16)pRec->nStat4;                       /* 0x3A99C */
            pUnit->auStat[5] = (u16)pRec->nStat5;                       /* 0x3A9A4 */
            pUnit->auStat[8] = (u16)(pRec->nStat8 + (i32)Game_RandU16(20) - 10);  /* 0x3A9C0 */
            pUnit->bUnk248   = (u8)pRec->nUnk248;                       /* 0x3A9CC */
            for (i32 k = 0; k < 5; ++k) {
                pUnit->abReady252[k] = (u8)pRec->nReady[k];             /* 0x3A9D8 */
            }

            if (pRec->nStat6 == -1) {
                pUnit->auStat[6] = (u16)(Game_RandU16(50) + 30);        /* 0x3AA08 */
                pUnit->auStat[7] = (u16)(Game_RandU16(50) + 30);        /* 0x3AA14 */
                pUnit->bUnk422   = 1;                                   /* 0x3AA40 */
            } else {
                pUnit->auStat[6] = (u16)pRec->nStat6;                   /* 0x3AA24 */
                pUnit->auStat[7] = (u16)pRec->nStat7;                   /* 0x3AA30 */
                pUnit->bUnk422   = 0;
            }
        }

        Unit_ResetBattleState(pUnit, (u8)Game_RandU16(8), pUnit->nUnitType,
                              pUnit->auStat, PUFO_UNIT_STAT_COUNT);      /* 0x3AA50 */

        if (!Unit_bIsLarge(pUnit) && pUnit->nUnitType != 16 && pUnit->nUnitType != 17 &&
            nStealth > 0) {
            --nStealth;                                                  /* 0x3AA84 */
            pUnit->bUnk435 = 1;                                          /* 0x3AA94 */
        }

        (void)Unit_RecalcInventoryStat(pUnit, pEnv->pInvHooks);          /* 0x3AA9C */

        if (pEnv->nMissionType != PUFO_MISSION_TYPE_XCOM_BASE && pEnv->pRmp != NULL) {
            (void)Rmp_PickSpawnForUnit(pEnv->pRmp, pUnit, pEnv->pfnLookupCell,
                                       pEnv->pLookupUser, false);        /* 0x3AAB8 */
        }
    }

    /* --- 6. load the four sprite sets ------------------------------------------------------ */

    for (i32 k = 0; k < 4; ++k) {                                        /* 0x3AAE8 */
        if (pEnv->abSpriteSet[k] != PUFO_SPRITE_SET_NONE) {
            pEnv->pFont->anAlienSprBase[k] = pEnv->pFont->nTotalSprFrames;   /* 0x3AAFC */
            (void)Font_LoadSpr(pEnv->pFont, s_aSpriteSetName[pEnv->abSpriteSet[k]]);
        }
    }
    Font_BuildIndex(pEnv->pFont);                                        /* 0x3AB24 */

    /* --- 7. recompose the eight slots ------------------------------------------------------ */

    for (i32 i = 0; i < 8; ++i) {                                        /* 0x3AB50 */
        anComposition[i] = ((i32)s_aAlienMissionTotals[nSetIndex] + (i32)pEnv->nAlienPoolAddend)
                           / (i32)s_aAlienCompositionDivisor[i];
    }

    /* --- 8. hand out the weapons ------------------------------------------------------------ */

    i32 nKeep = 0;                                                       /* var_44 */
    i32 nSquad = *pEnv->pnEnemyUnitCount + *pEnv->pnEnemyReserveCount;

    for (i32 i = 0; i < nSquad; ++i) {
        Soldier *pUnit = (pEnv->nMissionType == PUFO_MISSION_TYPE_XCOM_BASE)
                             ? pEnv->apHiddenSoldiers[i]
                             : pEnv->apEnemyUnits[i];

        if (pUnit->bUnk422 != 0) {                                       /* 0x3ABF0 */
            bool bKeep = false;
            if (pEnv->nMissionType == PUFO_MISSION_TYPE_URBAN ||
                pEnv->nMissionType == PUFO_MISSION_TYPE_XCOM_BASE) {
                bKeep = (nKeep < 3);                                     /* 0x3AC4C */
            } else {
                bKeep = (pUnit->bRankType == 4 || pUnit->bRankType == 8); /* 0x3AC14 */
            }
            if (bKeep) {
                ++nKeep;
            } else {
                pUnit->bUnk422 = 0;                                      /* 0x3AC60 */
            }
        }

        if (pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT &&
            g_aUnitTypeFlag2[pUnit->nUnitType] == 0) {
            continue;                                                    /* 0x3AD48 */
        }

        /* 0x3AD50. Walk the belt table backwards with a counter running 14 down to 0. The counter
         * skips the -1 sentinel (it equals 4 exactly where the sentinel sits) and decides which of
         * the two item branches may fire. */
        i32 nState = 0;
        i32 nCounter = 14;

        for (i32 k = 7; k >= 0; --k) {
            i32 nCount = anComposition[k];

            if (nCount != 0 && nCounter != 4) {
                ObjectRecFull *pObj = pEnv->apObjects[s_aAlienBeltObjects[k]];

                if (nState != 0) {
                    if (nState == 1 && nCounter <= 4) {                  /* 0x3AD94, 0x3AD9C */
                        TactItem *pItem = (TactItem *)malloc(sizeof(TactItem));
                        if (pItem != NULL) {
                            TactItem_ctor(pItem);
                        }
                        i32 nItemIdx = *pEnv->pnTactItemCount;
                        pEnv->apTactItems[nItemIdx] = pItem;
                        pEnv->apTactItems[nItemIdx]->pObj = pObj;        /* 0x3ADF0 */
                        /* The ammo cap comes from the object the weapon's own ammo type points at,
                         * read straight off the record at 0x3AE14. This branch never sets nIndex. */
                        pEnv->apTactItems[nItemIdx]->nAmmo =
                            pEnv->apObjects[pObj->anAmmoTypeId[0]]->bAmmoCap;
                        pUnit->apBelt[k] = pEnv->apTactItems[nItemIdx];  /* 0x3AE30 */
                        nState = 2;
                        ++*pEnv->pnTactItemCount;
                        if (nCount != -1) {
                            anComposition[k] = nCount - 1;               /* 0x3AE60 */
                        }
                    }
                } else if (Spawn_BurdenQuotient(pUnit, pObj) == 0) {     /* 0x3AE94 */
                    TactItem *pItem = (TactItem *)malloc(sizeof(TactItem));
                    if (pItem != NULL) {
                        TactItem_ctor(pItem);
                    }
                    i32 nItemIdx = *pEnv->pnTactItemCount;
                    pEnv->apTactItems[nItemIdx] = pItem;
                    pEnv->apTactItems[nItemIdx]->pObj = pObj;            /* 0x3AEE0 */
                    if (nCounter > 4) {                                  /* 0x3AEE4 */
                        pEnv->apTactItems[nItemIdx]->nAmmo =
                            pEnv->apObjects[pObj->anAmmoTypeId[0]]->bAmmoCap;  /* 0x3AF0C */
                        pEnv->apTactItems[nItemIdx]->nIndex = pObj->anAmmoTypeId[0]; /* 0x3AF28 */
                    }
                    nState = 1;                                          /* 0x3AF38 */
                    pUnit->pHandL = pEnv->apTactItems[nItemIdx];         /* 0x3AF44 */
                    ++*pEnv->pnTactItemCount;
                    if (nCount != -1) {
                        anComposition[k] = nCount - 1;                   /* 0x3AF74 */
                    }
                }

                Spawn_UpdatePose(pUnit);                                 /* 0x3AF7C */
            }

            nCounter -= 2;
        }
    }

    /* --- 9. kill the flagged units --------------------------------------------------------- */

    for (i32 i = 0; i < *pEnv->pnEnemyUnitCount; ) {                     /* 0x3B004 */
        Soldier *pUnit = pEnv->apEnemyUnits[i];
        if (pUnit->bUnk435 != 0) {
            pUnit->uHp = 0;                                              /* 0x3B048 */
            if (pEnv->pfnKillUnit != NULL) {
                pEnv->pfnKillUnit(pUnit, pEnv->pKillUser);               /* Tact_KillUnit (0x4e430) */
            }
            i = -1;   /* the original jumps back and restarts the scan from index 0 */
        }
        ++i;
    }

    return *pEnv->pnEnemyUnitCount;
}

/* ---------------------------------------------------- pre-mission craft cargo */

/* The mission base index is a byte the original never bounds before indexing auPerBaseVal with it.
 * The port clamps, matching what it already does for the same column in pufo_geoscape.c and
 * pufo_ship.c. */
static bool Cargo_BaseColValid(i32 nBase)
{
    return nBase >= 0 &&
           nBase < (i32)(sizeof(((ObjectRecFull *)0)->auPerBaseVal) /
                        sizeof(((ObjectRecFull *)0)->auPerBaseVal[0]));
}

/* 0x3907C and 0x39184: the same filter in both XCom-base passes. bTwoHanded == 2 keeps an object
 * out of the tally, except the craft store slots and the base store records, which always join. */
static bool Cargo_InTally(const ObjectRecFull *pObj, i32 nObjIdx)
{
    if (pObj->bTwoHanded != 2) {
        return true;
    }
    if (nObjIdx >= PUFO_CRAFT_STORE_FIRST && nObjIdx <= PUFO_CRAFT_STORE_LAST) {
        return true;
    }
    return nObjIdx >= PUFO_CARGO_BASE_FIRST && nObjIdx <= PUFO_CARGO_BASE_LAST;
}

/* Both scratch walks carry a byte counter that advances 12 per object and skip the range 372..420.
 * 372 is 12*31 and 420 is 12*35, so what they skip is object indices 31 through 35. */
static bool Cargo_IsStoreSlot(i32 nObjIdx)
{
    return nObjIdx >= PUFO_CRAFT_STORE_FIRST && nObjIdx <= PUFO_CRAFT_STORE_LAST;
}

void Tact_SetupCraftCargo(TactCargoEnv *pEnv)
{
    if (pEnv == NULL || pEnv->apObjects == NULL || pEnv->pnObjectCount == NULL ||
        pEnv->apCraft == NULL || pEnv->pnCraftCount == NULL ||
        pEnv->apTactItems == NULL || pEnv->pnTactItemCount == NULL ||
        pEnv->ppDeployCell == NULL || pEnv->panCargoSplit == NULL ||
        pEnv->panCargoWork == NULL || pEnv->pfnLookupCell == NULL ||
        pEnv->pDeployOffsets == NULL) {
        return;
    }

    const i32 nBase    = (i32)pEnv->nBaseIndex;
    const bool bBaseOk = Cargo_BaseColValid(nBase);
    const i32 nObjects = (i32)*pEnv->pnObjectCount;

    /* 0x38F30: the seed column, read before anything moves. Step 9 needs it as it was on entry. */
    i32 nSeedCol = 0;
    if (bBaseOk) {
        nSeedCol = (i32)pEnv->apObjects[PUFO_CARGO_SEED_OBJECT]->auPerBaseVal[nBase];
    }

    /* 0x38F34..0x38FCC: the deploy cell. The original folds the index as
     * 3600*rec[2] + 60*(10*nCraftMapY + rec[1]) + (10*nCraftMapX + rec[0]) over the cell array at
     * Map+0x15338, which is the same anchor Tact_SelectTeamUnits builds at 0x3975C. */
    i32 nX = 0;
    i32 nY = 0;
    i32 nLayer = 0;
    if (pEnv->nMapLayers != 0) {
        const u8 *pRec = pEnv->pDeployOffsets + PUFO_DEPLOY_RECORD_SIZE * (i32)pEnv->nMapLayers;
        nX     = PUFO_CELLS_PER_TILE * (i32)pEnv->nCraftMapX + (i32)pRec[0];
        nY     = PUFO_CELLS_PER_TILE * (i32)pEnv->nCraftMapY + (i32)pRec[1];
        nLayer = (i32)pRec[2];
    }
    *pEnv->ppDeployCell = pEnv->pfnLookupCell(nX, nY, nLayer, pEnv->pLookupUser);

    if (pEnv->nMissionType != PUFO_MISSION_TYPE_XCOM_BASE) {
        for (i32 i = 0; i < nObjects; ++i) {                                          /* 0x39018 */
            pEnv->apObjects[i]->uRecovScore =
                pEnv->apObjects[i]->aItemQty[(i32)pEnv->nCraftIndex];
        }
    } else {
        for (i32 i = 0; i < nObjects; ++i) {
            /* 0x3906C: the walk ENDS at the first object with uSortKey >= 59, it does not skip it. */
            if (pEnv->apObjects[i]->uSortKey >= 59) {
                break;
            }
            pEnv->apObjects[i]->uRecovScore = 0;                                     /* 0x39070 */
            if (!Cargo_InTally(pEnv->apObjects[i], i)) {
                continue;
            }
            if (bBaseOk) {
                /* 0x390B4 then 0x390C8: the base column moves into the score and is then cleared.
                 * Both writes reach the same u16 -- 2*(base+21) is 0x2a + 2*base. */
                pEnv->apObjects[i]->uRecovScore   = pEnv->apObjects[i]->auPerBaseVal[nBase];
                pEnv->apObjects[i]->auPerBaseVal[nBase] = 0;
            }
        }

        for (i32 j = 0; j < (i32)*pEnv->pnCraftCount; ++j) {                         /* 0x39108 */
            Ship *pShip = pEnv->apCraft[j];
            /* bHomeBase is loaded unsigned and the base index signed, exactly as the ARM does. */
            if ((i32)pShip->bHomeBase != nBase || pShip->bAtBase == 0) {
                continue;
            }
            pShip->bWeaponsHot = 1;                                                  /* 0x39138 */
            for (i32 i = 0; i < nObjects; ++i) {
                if (!Cargo_InTally(pEnv->apObjects[i], i)) {
                    continue;
                }
                pEnv->apObjects[i]->uRecovScore =
                    (u16)((i32)pEnv->apObjects[i]->uRecovScore
                          + (i32)pEnv->apObjects[i]->aItemQty[j]);                   /* 0x391A8 */
            }
        }
    }

    /* 0x391F4: operator new(4 * nObjectCount) then a zero loop. This one IS cleared wholesale; the
     * two .bss scratch blocks below are not. */
    i32 *anAlloc = (i32 *)calloc((size_t)nObjects, sizeof(i32));

    for (i32 i = 0; i < nObjects; ++i) {
        if (Cargo_IsStoreSlot(i)) {                                                  /* 0x39284 */
            continue;
        }
        ObjectRecFull *pObj = pEnv->apObjects[i];
        if (pObj->bAmmoCap == PUFO_AMMO_CAP_UNLIMITED) {                            /* 0x3929C */
            continue;
        }
        pEnv->panCargoSplit[i] = 0;                                                  /* 0x392AC */
        if (pObj->uRecovScore == 0 || pObj->bTwoHanded != 0) {                       /* 0x392C4 */
            continue;
        }
        for (i32 k = 0; k < PUFO_CARGO_WORK_PER_OBJ; ++k) {
            u16 nAmmoId = pObj->anAmmoTypeId[k];
            if (nAmmoId != 0) {
                i32 nCur   = pEnv->panCargoSplit[i];                                 /* 0x392A4 */
                i32 nHave  = anAlloc[nAmmoId];                                       /* 0x392EC */
                i32 nScore = (i32)pEnv->apObjects[nAmmoId]->uRecovScore;             /* 0x39304 */
                i32 nDiff  = nCur - nHave + nScore;
                if ((i32)pObj->uRecovScore < nDiff) {                                /* 0x3930C */
                    anAlloc[nAmmoId]       = nHave - nCur + (i32)pObj->uRecovScore;  /* 0x39318 */
                    pEnv->panCargoSplit[i] = (i32)pObj->uRecovScore;                 /* 0x39324 */
                } else {
                    pEnv->panCargoSplit[i] = nDiff;                                  /* 0x39328 */
                    anAlloc[nAmmoId]       = nScore;                                 /* 0x39344 */
                }
            }
            /* 0x39360: every slot of the triple gets the split, including the ones whose ammo id
             * was zero. All three end up holding the same value. */
            pEnv->panCargoWork[i * PUFO_CARGO_WORK_PER_OBJ + k] = pEnv->panCargoSplit[i];
        }
    }

    for (i32 i = 0; i < nObjects; ++i) {
        if (Cargo_IsStoreSlot(i)) {                                                  /* 0x393DC */
            continue;
        }
        ObjectRecFull *pObj = pEnv->apObjects[i];
        i32 nBuild = (i32)pObj->uRecovScore - anAlloc[i];                            /* 0x39400 */
        if (nBuild <= 0) {
            continue;
        }
        for (i32 k = 0; k < nBuild; ++k) {
            TactItem *pItem = (TactItem *)malloc(sizeof(TactItem));                  /* 0x39414 */
            if (pItem != NULL) {
                TactItem_ctor(pItem);
            }
            i32 nItemIdx = *pEnv->pnTactItemCount;
            pEnv->apTactItems[nItemIdx]      = pItem;                                /* 0x39444 */
            pEnv->apTactItems[nItemIdx]->pObj = pObj;                                /* 0x39460 */
            MapCell_ListInsertByPri(*pEnv->ppDeployCell,
                                    pEnv->apTactItems[nItemIdx]);                    /* 0x39480 */

            if (k >= pEnv->panCargoSplit[i]) {                                       /* 0x3948C */
                /* Past the split the item is the ammo itself. The original tests bAmmoCap against
                 * 255 and then stores either 255 or the cap, which is the same byte either way. */
                pEnv->apTactItems[nItemIdx]->nAmmo  = pObj->bAmmoCap;               /* 0x39530 */
                pEnv->apTactItems[nItemIdx]->nIndex  = (u16)i;                      /* 0x39538 */
            } else {
                /* Before the split it is a weapon, and the work triple says which of the three
                 * ammo types this copy is loaded with. */
                i32 nSlot = 0;                                                       /* 0x3949C */
                if (k >= pEnv->panCargoWork[i * PUFO_CARGO_WORK_PER_OBJ]) {
                    nSlot = (k >= pEnv->panCargoWork[i * PUFO_CARGO_WORK_PER_OBJ + 1]) ? 2 : 1;
                }
                u16 nAmmoId = pObj->anAmmoTypeId[nSlot];                             /* 0x394EC */
                pEnv->apTactItems[nItemIdx]->nAmmo =
                    pEnv->apObjects[nAmmoId]->bAmmoCap;                              /* 0x394E0 */
                pEnv->apTactItems[nItemIdx]->nIndex = nAmmoId;
            }
            ++*pEnv->pnTactItemCount;                                                /* 0x39570 */
        }
    }

    for (i32 i = 0; i < *pEnv->pnTactItemCount; ++i) {                               /* 0x395C4 */
        TactItem *pItem = pEnv->apTactItems[i];
        if (pItem->pObj->bTwoHanded == 1) {
            pItem->nAmmo = pItem->pObj->bAmmoCap;
        }
    }

    free(anAlloc);                                                                   /* 0x395FC */

    /* 0x39600: the launching craft's four cargo counts go to the base store records, in the order
     * 56, 57, 105, 58. 105 takes cargo[2] / 10 through _rt_sdiv; the other three take the count
     * whole. Each count is then zeroed. */
    static const i32 s_anCargoStore[PUFO_SHIP_CARGO_COLUMNS] = {
        PUFO_CARGO_STORE_0, PUFO_CARGO_STORE_1, PUFO_CARGO_STORE_2, PUFO_CARGO_STORE_3
    };
    Ship *pCraft = pEnv->apCraft[(i32)pEnv->nCraftIndex];
    for (i32 j = 0; j < PUFO_SHIP_CARGO_COLUMNS; ++j) {
        i32 nDelta = (j == PUFO_CARGO_TENS_COLUMN)
                       ? pCraft->anCargoQty[PUFO_CARGO_TENS_COLUMN] / 10
                       : pCraft->anCargoQty[j];
        ObjectRecFull *pObj = pEnv->apObjects[s_anCargoStore[j]];
        /* 0x39674 truncates the delta to 16 bits before the add, so a negative count adds as its
         * unsigned half. */
        pObj->uRecovScore = (u16)((i32)pObj->uRecovScore + (i32)(u16)nDelta);        /* 0x39680 */
        pCraft->anCargoQty[j] = 0;                                                   /* 0x396A8 */
    }

    if (pEnv->nMissionType == PUFO_MISSION_TYPE_XCOM_BASE) {
        ObjectRecFull *pBase = pEnv->apObjects[PUFO_CARGO_BASE_OBJECT];
        if (bBaseOk) {
            pBase->uRecovScore =
                (u16)((i32)pBase->uRecovScore + (i32)pBase->auPerBaseVal[nBase]);    /* 0x396F0 */
            /* 0x39700 re-reads the column AFTER the add, so the deduction is against the grown
             * value. It clamps at zero instead of wrapping. */
            i32 nCol = (i32)pBase->auPerBaseVal[nBase];
            pBase->auPerBaseVal[nBase] =
                (u16)((nCol < 5 * nSeedCol) ? 0 : nCol - 5 * nSeedCol);              /* 0x39728 */
        }
    }
}
