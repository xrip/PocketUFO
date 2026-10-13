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

#include <string.h>
#include <stdio.h>

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

bool Tact_ScanVictoryTiles(bool bCraftTileA0NonZero, u8 nMissionType, i32 nCraftTileX,
                           i32 nCraftTileY,
                           bool (*pfnCellHasNoItems)(i32 nX, i32 nY, void *pUser),
                           void *pUser)
{
    /* Both gates must hold: a craft origin and the alien-base mission type. */
    if (!bCraftTileA0NonZero || nMissionType != PUFO_MISSION_TYPE_ALIEN_BASE) {
        return false;
    }
    if (pfnCellHasNoItems == NULL) {
        return false;
    }

    /* The original's loop order: two columns within a row, then the next row. */
    for (i32 dy = 0; dy < 2; ++dy) {
        for (i32 dx = 0; dx < 2; ++dx) {
            if (pfnCellHasNoItems(nCraftTileX + dx, nCraftTileY + dy, pUser)) {
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
        *pnX1 = 10 * (i32)pCtx->nMapDimX;
        *pnY1 = 10 * (i32)pCtx->nMapDimY;
        return;
    }
    if (pCtx->nMissionType == 4) {
        i32 nBase = 10 * (i32)pCtx->nMapDimY;
        *pnY0 = nBase - 10;
        *pnY1 = nBase;
        *pnX0 = 10 * ((i32)pCtx->nMapDimX - 3);
        *pnX1 = 10 * ((i32)pCtx->nMapDimX - 3) + 30;
        return;
    }
    *pnX0 = 10 * (i32)pCtx->nCraftMapX;
    *pnY0 = 10 * (i32)pCtx->nCraftMapY;
    *pnX1 = 10 * (i32)pCtx->nCraftMapX;
    *pnY1 = 10 * (i32)pCtx->nCraftMapY;
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
        nX     = 10 * (i32)pEnv->nCraftMapX + (i32)pRec[0];
        nY     = 10 * (i32)pEnv->nCraftMapY + (i32)pRec[1];
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

/* A hardware soldier is named after the object it carries: the name is the ObjectRec's own bytes
 * at offset 0. The original copies until the NUL with no bound, and Soldier.szName is 40 bytes, so
 * an object name of 40 bytes or more runs off the end. Reproduced as-is. */
static void Deploy_NameFromObject(Soldier *pUnit, const ObjectRecFull *pObj)
{
    const u8 *pSrc = pObj->gap_0;
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

    for (i32 nObjectIdx = PUFO_HWPS_OBJECT_FIRST; nObjectIdx <= PUFO_HWPS_OBJECT_LAST; ++nObjectIdx) {
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
                                10 * (i32)pEnv->nCraftMapX + (i32)pHw[0],
                                10 * (i32)pEnv->nCraftMapY + (i32)pHw[1],
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
            *pEnv->pnXcomUnitCount = *pEnv->pnXcomUnitCount + 1;
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

            nCellX = 10 * (i32)pEnv->nCraftMapX + (i32)pRec[-104 + 0];
            nCellY = 10 * (i32)pEnv->nCraftMapY + (i32)pRec[-104 + 1];
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
