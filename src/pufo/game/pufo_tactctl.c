/* pufo_tactctl.c - tactical cursor, game speed, and clock control.
 *
 * Evidence: MapCursor_SetFlags @ 0x26f78, MapCursor_IsValid @ 0x275fc,
 *           GameSpeed_Set @ 0x260c0, GameTime_Pause @ 0x5d6ac.
 */
#include "game/pufo_tactctl.h"
#include "game/pufo_tactui.h"
#include "game/pufo_unit.h"
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
