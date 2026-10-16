/* pufo_step.c - the step-legality probe.
 *
 * Evidence: see pufo_step.h for the address list.
 */
#include "game/pufo_step.h"

/* The second column of a footprint. The original reaches it by adding one to the
 * apCells index rather than to the coordinate - base 0x1533C is apCells @ 0x15338
 * plus one dword (0x27840, 0x278ac, 0x278fc, 0x27930, 0x27958). */
static MapCell *Step_CellX1(const StepCtx *pCtx, const MapCell *pCell)
{
    if (pCell == NULL) {
        return NULL;
    }
    return pCtx->pfnLookup((i32)pCell->nX + 1, (i32)pCell->nY, (i32)pCell->nZ,
                           pCtx->pLookupUser);
}

/* Mc1Rec.iCeilParam (byte 0x24) below a limit, with the NULL ceiling sprite the
 * original tests separately folded in. */
static bool Step_CeilBreaks(const MapCell *pCell, i32 nLimit)
{
    return pCell != NULL
        && pCell->pCeilSpr != NULL
        && (i32)pCell->pCeilSpr->iCeilParam < nLimit;
}

bool Map_ProbeStepOccupants(const StepCtx *pCtx)
{
    StepProbe *p = pCtx->pProbe;
    MapCell   *pBelow;          /* var_10, 0x27808: apCells[dstLayer-1][dstY][dstX] */
    MapCell   *pAbove;          /* var_14, 0x2787c: apCells[dstLayer+1][dstY][dstX] */
    MapCell   *pBelowX1 = NULL; /* var_C,  0x27844 */
    MapCell   *pAboveX1 = NULL; /* var_4,  0x278b0 */
    MapCell   *pCurX1 = NULL;   /* 0x278fc: the current cell's second column */
    MapCell   *pAboveOldX1 = NULL; /* var_8, 0x27934 */
    MapCell   *pDstX1 = NULL;   /* 0x27958: the candidate's second column */
    bool       bLarge;

    /* 0x277b4 / 0x277d4: the adjusted layer starts as the candidate layer and the
     * re-anchor flag starts clear. */
    p->nAdjLayer   = p->nDstLayer;
    p->bReanchored = 0;
    bLarge         = Unit_bIsLarge(pCtx->pSelUnit);

    pBelow = NULL;
    pAbove = NULL;

    /* 0x277d0..0x27844: the cell below the candidate, base 0x11AF8 = apCells minus
     * one 14400-byte layer. A large unit also takes that cell's second column. */
    if (p->nDstLayer > 0) {
        pBelow = pCtx->pfnLookup(p->nDstX, p->nDstY, p->nDstLayer - 1,
                                 pCtx->pLookupUser);
        if (bLarge) {
            pBelowX1 = Step_CellX1(pCtx, pBelow);
        }
    }
    /* 0x27848..0x278b0: the cell above the candidate, base 0x18B78 = apCells plus
     * one layer. */
    if (p->nDstLayer < 3) {
        pAbove = pCtx->pfnLookup(p->nDstX, p->nDstY, p->nDstLayer + 1,
                                 pCtx->pLookupUser);
        if (bLarge) {
            pAboveX1 = Step_CellX1(pCtx, pAbove);
        }
    }

    /* 0x278b4..0x2795c: the large-unit footprint columns.
     *
     * ORIGINAL BUG (0x27988): the non-large path loads R11 and R6 from var_4, which
     * only the large path ever writes (0x278b0), and then uses neither register. The
     * port leaves them NULL instead of reading an unwritten slot.
     * ORIGINAL BUG (0x27ae0): var_8 is stored only when g_nCurLayer < 3 (0x2790c
     * BGE loc_27938) but dereferenced at 0x27ae0 with no guard, so a large unit on
     * layer 3 reads a stale pointer. The port's guard below is what keeps that read
     * from happening. */
    if (bLarge) {
        pCurX1 = Step_CellX1(pCtx, p->pCurCell);
        if (p->nCurLayer < 3) {
            pAboveOldX1 = Step_CellX1(pCtx, p->pCellAbove);
        }
        pDstX1 = Step_CellX1(pCtx, p->pDstCell);
    }

    /* 0x27990..0x27a54: the downward re-anchor. The candidate has no floor, the
     * current cell does, the search flag is clear and the candidate is not on
     * layer 0. For a large unit the candidate's second column must also be
     * floorless and above layer 0, and the current cell's second column must have
     * a floor. */
    {
        bool bDown = p->pDstCell->pFloorSpr == NULL
                  && p->pCurCell->pFloorSpr != NULL
                  && !pCtx->bSearchFlag
                  && (i32)p->pDstCell->nZ > 0;

        if (bDown && bLarge) {
            bDown = pDstX1 != NULL && pDstX1->pFloorSpr == NULL
                 && pCurX1 != NULL && pCurX1->pFloorSpr != NULL
                 && !pCtx->bSearchFlag
                 && (i32)pDstX1->nZ > 0;
        }

        if (bDown) {
            /* 0x27a04 / 0x27a34: the cell being moved into needs a floor.
             * ORIGINAL BUG (0x27a24): Unit_bIsLarge is called a third time here and
             * its BEQ branches to the return-zero path, but control only reaches
             * 0x27a24 when the unit is already known to be large, so that branch is
             * dead. The port keeps the one test it should have been. */
            if (pBelow == NULL || pBelow->pFloorSpr == NULL) {
                return false;
            }
            if (bLarge && (pBelowX1 == NULL || pBelowX1->pFloorSpr == NULL)) {
                return false;
            }
            /* ORIGINAL BUG (0x27a48): the downward anchor stores the new cell and
             * drops g_nStepAdjLayer but never touches g_nStepDstLayer, while the
             * upward anchor below updates the cell and both counters. The direction
             * arms index with g_nStepAdjLayer, so the candidate layer keeps its
             * pre-anchor value. Reproduced as written. */
            p->pDstCell    = pBelow;      /* 0x27a48 */
            p->nAdjLayer  -= 1;           /* 0x27a50 */
            p->bReanchored = 1;           /* 0x27b28 */
            return true;
        }
    }

    /* 0x27a6c..0x27b1c: the upward re-anchor. The current cell needs a ceiling that
     * breaks below -15, the cell above the current one must have no floor, and the
     * cell above the candidate must have one. For a large unit the current cell's
     * second column needs the same breakable ceiling, the cell above it must be
     * floorless and the cell above the candidate's second column must have a floor. */
    {
        /* ORIGINAL BUG (0x27a88): g_pStepCellAbove is dereferenced here with no
         * guard, but Path_ExpandCell only writes it when the current layer is below
         * 3 (the LDRLT pair at 0x2b658 / 0x2b67c), so on layer 3 it is a stale
         * pointer from an earlier probe.
         * ORIGINAL BUG (0x27a94): the same shape for var_14, the cell above the
         * candidate - stored only when the candidate layer is below 3 (0x27848 BGE
         * loc_278B4) and dereferenced here regardless. */
        bool bUp = Step_CeilBreaks(p->pCurCell, PUFO_STEP_CEIL_REANCHOR)
                && p->pCellAbove != NULL
                && p->pCellAbove->pFloorSpr == NULL;

        if (bUp) {
            if (pAbove == NULL || pAbove->pFloorSpr == NULL) {
                goto reject; /* 0x27aa0 */
            }
            if (bLarge) {
                /* ORIGINAL BUG (0x27ae4): the cell this test wants floorless is
                 * pAboveOldX1, the cell above the current cell's footprint second
                 * column, while 0x27a94 above wants the cell above the candidate to
                 * have a floor. Stepping due east (dx=+1, dy=0) those are the same
                 * cell, so the two tests contradict and the upward anchor can never
                 * fire for a large unit in that direction - a small unit with the
                 * same geometry does re-anchor. Reproduced; tests/test_step.c
                 * TestAnchorUpDeadForLargeEast pins it. */
                bUp = Step_CeilBreaks(pCurX1, PUFO_STEP_CEIL_REANCHOR)
                   && pAboveOldX1 != NULL && pAboveOldX1->pFloorSpr == NULL
                   && pAboveX1 != NULL && pAboveX1->pFloorSpr != NULL;
            }
            if (bUp) {
                p->pDstCell    = pAbove;   /* 0x27b00 */
                p->nDstLayer  += 1;        /* 0x27b08 */
                p->nAdjLayer  += 1;        /* 0x27b18 */
                p->bReanchored = 1;
                return true;
            }
        }
    }

reject:
    /* 0x27b44..0x27b7c: the rejection test. Below layer 3, a current cell whose
     * ceiling breaks below -8 and a cell above the candidate that HAS a floor means
     * the step is not allowed. */
    if (p->nDstLayer < 3
        && Step_CeilBreaks(p->pCurCell, PUFO_STEP_CEIL_REJECT)
        && pAbove != NULL && pAbove->pFloorSpr != NULL) {
        return false;
    }

    /* 0x27b84..0x27ba8: with no ceiling on the current cell, a candidate ceiling
     * that breaks below -8 rejects the step. */
    if (p->pCurCell->pCeilSpr != NULL) {
        return true;
    }
    if (p->pDstCell->pCeilSpr == NULL) {
        return true;
    }
    return (i32)p->pDstCell->pCeilSpr->iCeilParam >= PUFO_STEP_CEIL_REJECT;
}
