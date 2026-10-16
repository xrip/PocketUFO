/* pufo_path.c - tactical reachability search.
 *
 * Evidence: see pufo_path.h for the full address list.
 */
#include "game/pufo_path.h"

#include <string.h>

/* aDirDeltaXY @ 0xa7ae0, decoded from the 30 bytes of signed triples at that
 * address. This is the authoritative step table: the unit step legality test
 * (Unit_CanStepTo (0x27bb8)) dispatches on it, and so does the flood fill. */
const StepDir g_aStepDir[PUFO_PATH_DIR_COUNT] = {
    {  0, -1,  0 }, /* 0: N  */
    {  1, -1,  0 }, /* 1: NE (diagonal) */
    {  1,  0,  0 }, /* 2: E  */
    {  1,  1,  0 }, /* 3: SE (diagonal) */
    {  0,  1,  0 }, /* 4: S  */
    { -1,  1,  0 }, /* 5: SW (diagonal) */
    { -1,  0,  0 }, /* 6: W  */
    { -1, -1,  0 }, /* 7: NW (diagonal) */
    {  0,  0,  1 }, /* 8: up   */
    {  0,  0, -1 }  /* 9: down */
};

u8 Map_FacingDelta(u8 nFrom, u8 nTo)
{
    i32 nDiff;

    /* Out-of-range facings compare as equal, which is what the original does
     * (it returns 0 rather than clamping). */
    if (nFrom > 7u || nTo > 7u) {
        return 0;
    }
    if (nTo < nFrom) {
        u8 nTmp = nFrom;

        nFrom = nTo;
        nTo   = nTmp;
    }
    nDiff = (i32)nTo - (i32)nFrom;
    if (nDiff > 4) {
        nDiff = (i32)nFrom - (i32)nTo + 8;
    }
    return (u8)nDiff;
}

/* Map_bFacingTurnPositive @ 0x27668. The sign test, transcribed from the
 * original: a diff of exactly 4 is treated as positive. */
bool Map_bFacingTurnPositive(u8 nFrom, u8 nTo)
{
    i32 nDiff = (i32)nTo - (i32)nFrom;
    i32 nSign = 1;

    if (nDiff < 0) {
        nSign = -1;
    }
    /* abs32 on the raw difference, then flip when the long way round is shorter. */
    {
        i32 nAbs = nDiff < 0 ? -nDiff : nDiff;

        if (nAbs > 4) {
            nSign = -nSign;
        }
    }
    return nSign > 0;
}

bool Path_EnqueueCell(PathScratch *pScratch, MapCell *pCell)
{
    MapCell **apList;
    i32      *pnCount;
    i32       i;

    if (pScratch->bUseA) {
        apList  = pScratch->apListB;
        pnCount = &pScratch->nCountB;
    } else {
        apList  = pScratch->apListA;
        pnCount = &pScratch->nCountA;
    }

    for (i = 0; i < *pnCount; ++i) {
        if (apList[i] == pCell) {
            return false; /* already queued */
        }
    }

    if (*pnCount >= PUFO_PATH_LIST_CAP) {
        /* The original has no capacity check and would run past its array.
         * Dropping the enqueue is the safe behaviour; recorded as a deviation. */
        return false;
    }

    pCell->bFlags44 = (u8)(pCell->bFlags44 | 2u);
    apList[*pnCount] = pCell;
    *pnCount += 1;
    return true;
}

/* Path_ExpandCell @ 0x2b604.
 *
 * Two gates precede the direction loop, both from the disassembly:
 *     LDR   R3, [R2,#0x14]   ; pCur->nPathCost (accumulated cost)
 *     CMP   R3, #0 / BEQ    ; cost 0 means unvisited -> do not expand
 *     LDR   R3, [R2,#0x14]  ... followed by
 *     LDR   R3, [R2,#0x0]   ; pCur->pFloorSpr
 *     CMP   byte_10DB84,#0 / *v6 != 0   ; expand if bFlag or a floor exists
 * The first is what makes this a flood fill rather than a blind sweep: only
 * cells that have been assigned a cost are expanded. */
static bool Path_ExpandCell(i32 nCellX, i32 nCellY, i32 nCellLayer,
                            i32 nSeedLayer, bool bFlag, u8 nInFacing,
                            PathScratch *pScratch,
                            PathCellLookup pfnLookup, PathStepProbe pfnProbe,
                            void *pUser)
{
    MapCell *pCur;
    bool     bAny = false;
    u8       nDir;

    pCur = pfnLookup(nCellX, nCellY, nCellLayer, pUser);
    if (pCur == NULL) {
        return false;
    }
    /* Cost 0 is the "unvisited" marker. */
    if (pCur->nPathCost == 0) {
        return false;
    }
    /* No floor sprite and no flag: nothing to expand across. */
    if (!bFlag && pCur->pFloorSpr == NULL) {
        return false;
    }

    for (nDir = 0; nDir < PUFO_PATH_DIR_COUNT; ++nDir) {
        i32 nNx = nCellX;
        i32 nNy = nCellY;
        i32 nNz = nCellLayer;

        if (!pfnProbe(nCellX, nCellY, nCellLayer, nDir, &nNx, &nNy, &nNz, pUser)) {
            continue;
        }
        {
            MapCell *pNext = pfnLookup(nNx, nNy, nNz, pUser);
            u16      nCost;
            i32      nNew;

            if (pNext == NULL) {
                continue;
            }

            /* Cost, exactly as the original composes it:
             *   cur->nPathCost (u16) + facing delta + 4
             *   + 2 when the direction is diagonal (odd, and below 8)
             *   + 4 when changing layer and the flag is not set */
            nCost = (u16)pCur->nPathCost;
            nNew  = (i32)nCost
                  + (i32)Map_FacingDelta(nDir, nInFacing)
                  + PUFO_PATH_BASE_COST;
            if (nDir < 8u && (nDir % 2) == 1) {
                nNew += PUFO_PATH_DIAGONAL_COST;
            }
            if (nNz != nSeedLayer && !bFlag) {
                nNew += PUFO_PATH_LAYER_COST;
            }

            /* Relax only when unset or strictly cheaper. */
            if (pNext->nPathCost != 0 && pNext->nPathCost <= nNew) {
                continue;
            }
            pNext->nPathCost   = nNew;
            pNext->bFlags29 = (u8)((pNext->bFlags29 & 0xF7) | (8 * (bFlag ? 0 : 1)));
            pNext->bPathDir   = nDir;
            bAny = true;

            if (pNext->nPathCost < PUFO_PATH_COST_LIMIT) {
                (void)Path_EnqueueCell(pScratch, pNext);
            }
        }
    }
    return bAny;
}

bool Path_ComputeReachable(PathScratch *pScratch,
                           i32 nSeedX, i32 nSeedY, i32 nSeedLayer, u8 nSeedFacing,
                           i32 nTargetX, i32 nTargetY, i32 nTargetLayer, bool bFlag,
                           PathCellLookup pfnLookup, PathStepProbe pfnProbe,
                           void *pUser)
{
    MapCell *pSeed;
    MapCell *pTarget;
    i32      nX;
    i32      nY;
    i32      nLayer;

    /* bFlag is the original's sixth argument, and Map_ComputeReachable never reads
     * it itself: it stores it to the global byte_10DB84 @ 0x10db84 (LDRB/STRB at
     * 0x2b850/0x2b860) and Path_ExpandCell reads it back from there at 0x2b6ac.
     * Carrying it as an argument is the same data flow without the global. */
    pScratch->nCountA = 0;
    pScratch->nCountB = 0;
    pScratch->bUseA   = false;

    /* Step 1: clear the accumulated cost and the two flag bits on every cell the
     * lookup can reach. The original walks the whole grid unconditionally. */
    for (nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (nY = 0; nY < PUFO_CELL_GRID_MAX; ++nY) {
            for (nX = 0; nX < PUFO_CELL_GRID_MAX; ++nX) {
                MapCell *pCell = pfnLookup(nX, nY, nLayer, pUser);

                if (pCell == NULL) {
                    continue;
                }
                pCell->nPathCost   = 0;
                pCell->bFlags29 = (u8)(pCell->bFlags29 & ~8u);
                pCell->bFlags44 = (u8)(pCell->bFlags44 & ~2u);
            }
        }
    }

    pTarget = pfnLookup(nTargetX, nTargetY, nTargetLayer, pUser);
    if (pTarget == NULL) {
        return false;
    }
    /* Step 2: a blank target cell has no path by definition. */
    if (MapCell_TestFlag(pTarget, CELLFLAG_IS_BLANK)) {
        return false;
    }

    /* Step 3: seed the source cell. Cost starts at 1, not 0, because 0 is the
     * "unvisited" marker the relaxation test relies on. */
    pSeed = pfnLookup(nSeedX, nSeedY, nSeedLayer, pUser);
    if (pSeed == NULL) {
        return false;
    }
    pSeed->bPathDir   = nSeedFacing;
    pSeed->bPathFlag   = 0;
    pSeed->nPathCost   = 1;
    (void)Path_EnqueueCell(pScratch, pSeed);

    /* Step 4: alternate the two frontier lists.
     *
     * The original double-buffers: while one list is being expanded, new entries
     * are appended to the OTHER one, and the consumed list is cleared. bUseA
     * selects the enqueue target (true -> list B), so expanding list A requires
     * bUseA true. Verified from Path_EnqueueCell @ 0x2b534, which appends to
     * dword_10DBC0 (list B) when byte_10DB80 is set, and from the two expansion
     * arms of Map_ComputeReachable @ 0x2b820, which read dword_110AA0 (list A)
     * when byte_10DB80 is set. */
    for (;;) {
        i32 i;

        /* Expand list A; new entries land in list B. */
        pScratch->bUseA = true;
        for (i = 0; i < pScratch->nCountA; ++i) {
            MapCell *pCell = pScratch->apListA[i];

            (void)Path_ExpandCell(pCell->nX, pCell->nY, pCell->nZ,
                                  nSeedLayer, bFlag, pCell->bPathDir,
                                  pScratch, pfnLookup, pfnProbe, pUser);
        }
        pScratch->nCountA = 0;
        if (pTarget->nPathCost != 0) {
            return true;
        }
        if (pScratch->nCountB == 0) {
            return false;
        }

        /* Expand list B; new entries land in list A. */
        pScratch->bUseA = false;
        for (i = 0; i < pScratch->nCountB; ++i) {
            MapCell *pCell = pScratch->apListB[i];

            (void)Path_ExpandCell(pCell->nX, pCell->nY, pCell->nZ,
                                  nSeedLayer, bFlag, pCell->bPathDir,
                                  pScratch, pfnLookup, pfnProbe, pUser);
        }
        pScratch->nCountB = 0;
        if (pTarget->nPathCost != 0) {
            return true;
        }
        if (pScratch->nCountA == 0) {
            return false;
        }
    }
}

/* byte_A7B00 @ 0xa7b00, decoded from the 27 bytes at that address. Three bands of
 * nine, indexed 3 * (3 * dz + dy) + dx + 13. The middle band (dz == 0) is the
 * compass facing for a horizontal delta; the other two are identical to it except
 * at the pure vertical delta, where they report 8 (up) and 9 (down). */
const u8 g_aFacingFromDelta[PUFO_FACING_FROM_DELTA_COUNT] = {
    /* dz = -1 */ 7, 0, 1, 6, 9, 2, 5, 4, 3,
    /* dz =  0 */ 7, 0, 1, 6, 0, 2, 5, 4, 3,
    /* dz = +1 */ 7, 0, 1, 6, 8, 2, 5, 4, 3
};

u8 Path_FacingFromDelta(i32 nDx, i32 nDy, i32 nDz)
{
    i32 nIndex;

    /* The original applies no clamp: every pair it looks at is one recorded step
     * apart, so each component is already -1..1. The clamp only keeps a caller that
     * breaks that promise inside the table. */
    if (nDx < -1) { nDx = -1; } else if (nDx > 1) { nDx = 1; }
    if (nDy < -1) { nDy = -1; } else if (nDy > 1) { nDy = 1; }
    if (nDz < -1) { nDz = -1; } else if (nDz > 1) { nDz = 1; }

    nIndex = 3 * (3 * nDz + nDy) + nDx + PUFO_FACING_FROM_DELTA_BIAS;
    return g_aFacingFromDelta[nIndex];
}

/* The walk-back bail-out, from `CMP R3, #0xC8 / BGT loc_2C674` at 0x2c54c. */
#define PUFO_PATH_BAIL_COUNT 200

/* The ceiling-break threshold the walk-back uses to decide that a step actually
 * dropped through a ceiling. `LDRSB R2, [R2,#0x24]` against -15 at 0x2c4d8..0x2c4e4.
 * The map click tests the same sprite byte against -23 (see Tact_HandleMapClick),
 * so the two call sites genuinely disagree. */
#define PUFO_PATH_CEIL_DROP_LIMIT (-15)

/* Unit_PathToCell @ 0x2c2a8. */
bool Unit_PathToCell(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pVoid)
{
    const PathToCellCtx *pCtx = (const PathToCellCtx *)pVoid;
    DoorActionCtx        door;
    MapCell             *pDest;
    MapCell             *pCell;
    i32                  nWalkX = nX;
    i32                  nWalkY = nY;
    i32                  nCurLayer = nLayer;
    i32                  nCount;
    i32                  nDir;
    i32                  nOpp;
    i32                  nDelta;
    i32                  k;
    u8                   nFacing;
    u8                   nWant;

    /* 0x2c324: the selected unit, read straight out of Map.pSelUnit with no test.
     * 0x2b868: Map_ComputeReachable reads the same slot to find the seed cell. */
    if (pCtx->ppSelUnit == NULL || *pCtx->ppSelUnit == NULL) {
        return false; /* deviation: the original would fault */
    }
    {
        Soldier *pUnit = *pCtx->ppSelUnit;

        if (pUnit->pCell == NULL) {
            return false; /* deviation: 0x2b874 dereferences it */
        }

        /* 0x2c2e8..0x2c314: with the flag clear the destination cell's occupant is
         * parked in Map+0x150f4; with it set the slot is cleared instead. Nothing
         * else in the binary touches that slot. */
        pDest = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pLookupUser);
        if (pDest == NULL) {
            return false; /* deviation: 0x2c304 indexes the grid blind */
        }
        if (pCtx->ppPathDestUnit != NULL) {
            *pCtx->ppPathDestUnit = (bFlag != 0) ? NULL : pDest->pUnit;
        }

        /* 0x2c334..0x2c358: the flood fill's seed is the unit's own cell, its seed
         * facing is the unit's facing, and its flag is column 0 of the unit type
         * table. */
        {
            bool bTypeFlag = pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT
                           && g_aUnitTypeFlag0[pUnit->nUnitType] != 0;

            /* The original ignores Map_ComputeReachable's return value and tests the
             * destination cell's cost instead (0x2c388). */
            (void)Path_ComputeReachable(pCtx->pScratch,
                                        pUnit->pCell->nX, pUnit->pCell->nY,
                                        pUnit->pCell->nZ, pUnit->bFacing,
                                        nX, nY, nLayer, bTypeFlag,
                                        pCtx->pfnLookup, pCtx->pfnProbe,
                                        pCtx->pProbeUser);
        }

        /* 0x2c364..0x2c390: dword_113980 @ 0x113980 is set to the target layer, and
         * a destination still at cost 0 means there is no path. */
        if (pDest->nPathCost == 0) {
            return false;
        }

        /* 0x2c39c/0x2c3ac: slot 0 is the destination, the count starts at zero. */
        pCtx->apPathCells[0] = pDest;
        *pCtx->pnPathCount   = 0;
        nCount = 0;

        /* The walk back, undoing one recorded step per turn. bPathDir records the
         * step that reached this cell, so the previous cell sits at the opposite
         * compass direction. */
        for (;;) {
            i32 nOldX = nWalkX;
            i32 nOldY = nWalkY;

            pCell = pCtx->pfnLookup(nWalkX, nWalkY, nCurLayer, pCtx->pLookupUser);
            if (pCell == NULL) {
                return false; /* deviation: 0x2c3d8 indexes blind */
            }
            nDir = pCell->bPathDir;
            if (nDir >= PUFO_PATH_DIR_COUNT) {
                return false; /* deviation: 0x2c3e8 indexes aDirDeltaXY blind */
            }

            nDelta = g_aStepDir[nDir].dz;
            if (nDelta != 0) {
                /* 0x2c3fc..0x2c404: a recorded vertical step. Undo it by taking the
                 * layer delta off; the column does not move. */
                nCurLayer -= nDelta;
            } else {
                /* 0x2c408..0x2c414: the opposite compass direction, (dir + 4) % 8. */
                nOpp = nDir + 4;
                if (nOpp > 7) {
                    nOpp -= 8;
                }
                nWalkX += g_aStepDir[nOpp].dx;
                nWalkY += g_aStepDir[nOpp].dy;

                /* 0x2c420: bFlags29 bit 3 is set by Path_ExpandCell whenever the
                 * flood ran without its flag. It marks the cell as one whose layer
                 * bookkeeping has to be re-checked, because a horizontal step here
                 * may really have been taken one layer up or one layer down. */
                if ((pCell->bFlags29 & 8u) != 0) {
                    bool bMoved = false;

                    /* 0x2c438..0x2c494: try the layer above. The cell the step came
                     * from must have no floor sprite there, and the cell it went to
                     * must have been reached by the flood. Both lookups use layer
                     * nCurLayer + 1, one at the old coordinates and one at the new. */
                    if (nCurLayer < 3) {
                        MapCell *pAboveOld = pCtx->pfnLookup(nOldX, nOldY,
                                                             nCurLayer + 1,
                                                             pCtx->pLookupUser);
                        MapCell *pAboveNew = pCtx->pfnLookup(nWalkX, nWalkY,
                                                             nCurLayer + 1,
                                                             pCtx->pLookupUser);

                        if (pAboveOld != NULL && pAboveOld->pFloorSpr == NULL
                            && pAboveNew != NULL && pAboveNew->nPathCost != 0) {
                            nCurLayer += 1;
                            bMoved = true;
                        }
                    }
                    /* 0x2c4a8..0x2c4f8: otherwise try the layer below, at the NEW
                     * coordinates only. The cell there needs a ceiling sprite whose
                     * break parameter says it is breakable, and it needs a cost. */
                    if (!bMoved && nCurLayer > 0) {
                        MapCell *pBelow = pCtx->pfnLookup(nWalkX, nWalkY,
                                                          nCurLayer - 1,
                                                          pCtx->pLookupUser);

                        if (pBelow != NULL && pBelow->pCeilSpr != NULL
                            && pBelow->pCeilSpr->iCeilParam < PUFO_PATH_CEIL_DROP_LIMIT
                            && pBelow->nPathCost != 0) {
                            nCurLayer -= 1;
                        }
                    }
                }
            }

            /* 0x2c508..0x2c550: append the cell the undo landed on, then stop when
             * the walk has run too long or reached the seed (cost 1). */
            nCount += 1;
            *pCtx->pnPathCount = nCount;

            pCell = pCtx->pfnLookup(nWalkX, nWalkY, nCurLayer, pCtx->pLookupUser);
            if (pCell == NULL) {
                return false; /* deviation: 0x2c53c indexes blind */
            }
            pCtx->apPathCells[nCount] = pCell;

            if (nCount > PUFO_PATH_BAIL_COUNT) {
                return false;
            }
            if (pCell->nPathCost == 1) {
                break;
            }
        }

        /* 0x2c588: a unit that still has to stand up pays for it before it moves. */
        if ((pUnit->bFlags & PUFO_UNITFLAG_STAND_UP) != 0) {
            TactState_EnqueueStandFromKneel(pCtx->pState, pUnit);
        }

        nFacing = pUnit->bFacing; /* 0x2c598 */

        door.pSelUnit       = pUnit;
        door.bSelUnitLarge  = Unit_bIsLarge(pUnit);
        door.geom           = *pCtx->pGeom;
        door.pfnLookup      = pCtx->pfnLookup;
        door.pUser          = pCtx->pLookupUser;

        /* The replay. The array runs destination-first, so walking it from the last
         * slot down replays the walk in order: pFrom is the cell being left, pTo the
         * cell being entered. */
        for (k = nCount; k >= 1; --k) {
            MapCell *pFrom = pCtx->apPathCells[k];
            MapCell *pTo   = pCtx->apPathCells[k - 1];

            nDelta = (i32)pTo->nZ - (i32)pFrom->nZ; /* 0x2c5d8..0x2c5e4 */
            nWant = Path_FacingFromDelta((i32)pTo->nX - (i32)pFrom->nX,
                                         (i32)pTo->nY - (i32)pFrom->nY, nDelta);

            /* 0x2c620..0x2c630: turn only when the facing actually changes. */
            if (nFacing != nWant) {
                TactState_EnqueueTurn(pCtx->pState, pUnit, nWant);
            }
            nFacing = nWant; /* 0x2c644 */

            (void)Map_QueueDoorAction(pCtx->pState, pFrom, pTo, nWant, &door); /* 0x2c648 */

            /* 0x2c64c..0x2c684: the entered cell's recorded direction decides the
             * command. A direction above 7 is a vertical step, and the original
             * puts the layer delta into the entry's nFacing slot for that command
             * (TactState_EnqueueVertStep @ 0x49ddc), which is the slot the vertical
             * step's draw offset reads. */
            if (pTo->bPathDir > 7) {
                TactState_EnqueueVertStep(pCtx->pState, pUnit, (u8)nDelta);
            } else {
                TactState_EnqueueStep(pCtx->pState, pUnit, nWant, nDelta);
            }
        }
    }

    /* 0x2c6a0..0x2c6b4: the end marker, the queue kick, and success. */
    TactState_EnqueueEndMark(pCtx->pState);
    TactState_KickQueue(pCtx->pState);
    return true;
}
