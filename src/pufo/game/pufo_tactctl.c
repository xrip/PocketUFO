/* pufo_tactctl.c - tactical cursor, game speed, and clock control.
 *
 * Evidence: MapCursor_SetFlags @ 0x26f78, MapCursor_IsValid @ 0x275fc,
 *           GameSpeed_Set @ 0x260c0, GameTime_Pause @ 0x5d6ac.
 */
#include "game/pufo_tactctl.h"

#include <string.h>

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
 * @ 0x45904 reads slot 105 the same way as a packed timestamp. Modelling that as
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
