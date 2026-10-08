/* pufo_path.c - tactical reachability search.
 *
 * Evidence: see pufo_path.h for the full address list.
 */
#include "game/pufo_path.h"

#include <string.h>

/* aDirDeltaXY @ 0xa7ae0, decoded from the 30 bytes of signed triples at that
 * address. This is the authoritative step table: the unit step legality test
 * (Unit_CanStepTo @ 0x27bb8) dispatches on it, and so does the flood fill. */
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
 *     LDR   R3, [R2,#0x14]   ; pCur->nUnk20 (accumulated cost)
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
    if (pCur->nUnk20 == 0) {
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
             *   cur->nUnk20 (u16) + facing delta + 4
             *   + 2 when the direction is diagonal (odd, and below 8)
             *   + 4 when changing layer and the flag is not set */
            nCost = (u16)pCur->nUnk20;
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
            if (pNext->nUnk20 != 0 && pNext->nUnk20 <= nNew) {
                continue;
            }
            pNext->nUnk20   = nNew;
            pNext->bFlags29 = (u8)((pNext->bFlags29 & 0xF7) | (8 * (bFlag ? 0 : 1)));
            pNext->bUnk24   = nDir;
            bAny = true;

            if (pNext->nUnk20 < PUFO_PATH_COST_LIMIT) {
                (void)Path_EnqueueCell(pScratch, pNext);
            }
        }
    }
    return bAny;
}

bool Path_ComputeReachable(PathScratch *pScratch,
                           i32 nSeedX, i32 nSeedY, i32 nSeedLayer, u8 nSeedFacing,
                           i32 nTargetX, i32 nTargetY, i32 nTargetLayer,
                           PathCellLookup pfnLookup, PathStepProbe pfnProbe,
                           void *pUser)
{
    MapCell *pSeed;
    MapCell *pTarget;
    i32      nX;
    i32      nY;
    i32      nLayer;

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
                pCell->nUnk20   = 0;
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
    pSeed->bUnk24   = nSeedFacing;
    pSeed->bUnk25   = 0;
    pSeed->nUnk20   = 1;
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
                                  nSeedLayer, false, pCell->bUnk24,
                                  pScratch, pfnLookup, pfnProbe, pUser);
        }
        pScratch->nCountA = 0;
        if (pTarget->nUnk20 != 0) {
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
                                  nSeedLayer, false, pCell->bUnk24,
                                  pScratch, pfnLookup, pfnProbe, pUser);
        }
        pScratch->nCountB = 0;
        if (pTarget->nUnk20 != 0) {
            return true;
        }
        if (pScratch->nCountA == 0) {
            return false;
        }
    }
}
