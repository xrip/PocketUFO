/* pufo_step.c - the step-legality probe.
 *
 * Evidence: see pufo_step.h for the address list.
 */
#include "game/pufo_step.h"

#include "game/pufo_path.h" /* g_aStepDir, PUFO_PATH_DIR_COUNT */

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

/* --- Unit_CanStepTo @ 0x27bb8 -------------------------------------------------
 *
 * The direction switch every mover goes through. Read as: a head test every direction
 * shares, then an arm that lists the cells this particular direction is about to
 * occupy and refuses the step when any of them is blank, blocked by a wall, or held by
 * somebody the mover is not allowed to share it with.
 *
 * The cell lists are the substance of the function, and they are not symmetric: a
 * normal unit tests the walls of the cell it leaves and the cell it enters, a large
 * unit tests the walls of all four cells of the 2x2 footprint it is about to cover.
 * The flags repeat across cells for the same reason - a wall belongs to the boundary
 * between two cells and each arm reaches it from whichever side it is testing.
 */

/* The two unit types the head's occupant gate still blocks a cell for even when the
 * occupant is not operative (0x27d0c, 0x27d14). Their meaning is not established in
 * the IDB; the values are what the compares use. */
#define PUFO_UNIT_TYPE_HEAD_BLOCK_A 16
#define PUFO_UNIT_TYPE_HEAD_BLOCK_B 17

static MapCell *Step_Cell(const StepCtx *pCtx, i32 nLayer, i32 nY, i32 nX)
{
    return pCtx->pfnLookup(nX, nY, nLayer, pCtx->pLookupUser);
}

static bool Step_CellFlag(MapCell *pCell, CellFlag nWhich)
{
    return pCell != NULL && MapCell_TestFlag(pCell, nWhich);
}

/* Deviation: the original indexes its fixed 60x60x4 grid with no bounds test inside
 * the arms, so an out-of-range coordinate reads a different cell of the same array.
 * A NULL from the injected lookup reads here as "no flag, no occupant". */
static bool Step_Flag(const StepCtx *pCtx, i32 nLayer, i32 nY, i32 nX, CellFlag nWhich)
{
    return Step_CellFlag(Step_Cell(pCtx, nLayer, nY, nX), nWhich);
}

static bool Step_Occupied(const StepCtx *pCtx, i32 nLayer, i32 nY, i32 nX)
{
    MapCell *pCell = Step_Cell(pCtx, nLayer, nY, nX);

    return pCell != NULL && MapCell_HasOccupant(pCell);
}

/* The head gate, 0x27cb0..0x27d18. */
static bool Step_HeadOccupantOk(const StepCtx *pCtx, MapCell *pCell)
{
    Soldier *pUnit;

    if (!MapCell_HasOccupant(pCell)) {
        return true;
    }
    pUnit = pCell->pUnit;
    if (pUnit == pCtx->pSwapUnit || pUnit == pCtx->pSelUnit) {
        return true;
    }
    if (pUnit->bUnk148 != 0) {
        return false;
    }
    if (Unit_bIsOperative(pUnit)) {
        return false;
    }
    return pUnit->nUnitType != PUFO_UNIT_TYPE_HEAD_BLOCK_A
        && pUnit->nUnitType != PUFO_UNIT_TYPE_HEAD_BLOCK_B;
}

/* The normal arms' gate (0x27dbc..0x27e20 and its nine twins).
 * ORIGINAL BUG (0x27e0c, residue): it is weaker than the head's gate - a foreign
 * occupant is accepted as soon as bUnk148 is clear, with no operative or unit-type
 * test. For a step the probe did not move, the head already tested this same cell and
 * refused an operative stranger, so the weaker test is only reachable through a
 * re-anchored candidate. The original reads bUnk148 through g_pStepDstCell, which by
 * then is the same cell the arm just tested - Map_ProbeStepOccupants keeps
 * g_pStepDstCell and g_nStepAdjLayer pointing at one cell. */
static bool Step_ArmOccupantOk(const StepCtx *pCtx, i32 nLayer, i32 nY, i32 nX)
{
    MapCell *pCell = Step_Cell(pCtx, nLayer, nY, nX);
    Soldier *pUnit;

    if (pCell == NULL || !MapCell_HasOccupant(pCell)) {
        return true;
    }
    pUnit = pCell->pUnit;
    if (pUnit == pCtx->pSwapUnit || pUnit == pCtx->pSelUnit) {
        return true;
    }
    return pUnit->bUnk148 == 0;
}

/* The large arms' gate (0x28b24..0x28b7c and its twins): no bUnk148 escape at all. */
static bool Step_LargeOccupantOk(const StepCtx *pCtx, i32 nLayer, i32 nY, i32 nX)
{
    MapCell *pCell = Step_Cell(pCtx, nLayer, nY, nX);
    Soldier *pUnit;

    if (pCell == NULL || !MapCell_HasOccupant(pCell)) {
        return true;
    }
    pUnit = pCell->pUnit;
    return pUnit == pCtx->pSwapUnit || pUnit == pCtx->pSelUnit;
}

/* The vertical pair for a normal unit (0x28868..0x289AC). Neither direction calls
 * Map_ProbeStepOccupants: the step is vertical already.
 *
 * With the search flag clear both the cell being moved to and the cell being left have
 * to have a floor that blocks (flag 6). With the flag set the pair is only tested when
 * the cell being moved to has a floor at all (0x288b0), and for direction 9 the flag
 * makes the original test the cell being left instead of the one being entered
 * (0x289AC..0x289F0). Then the cell directly above or below the current one has to be
 * empty and not blank. */
static bool Step_NormalVertical(const StepCtx *pCtx)
{
    const StepProbe *p = pCtx->pProbe;
    MapCell *pCur  = Step_Cell(pCtx, p->nCurLayer, p->nCurY, p->nCurX);
    MapCell *pTest;

    if (pCtx->bSearchFlag) {
        pTest = (p->nDir == 8) ? p->pDstCell : pCur;
        if (Step_CellFlag(pTest, CELLFLAG_HAS_FLOOR)
            && (!Step_CellFlag(pTest, CELLFLAG_FLOOR_BLOCKS)
                || !Step_CellFlag(pCur, CELLFLAG_FLOOR_BLOCKS))) {
            return false;
        }
    } else {
        if (!Step_CellFlag(p->pDstCell, CELLFLAG_FLOOR_BLOCKS)
            || !Step_CellFlag(pCur, CELLFLAG_FLOOR_BLOCKS)) {
            return false;
        }
    }

    if (Step_Occupied(pCtx, p->nDstLayer, p->nCurY, p->nCurX)) {
        return false;
    }
    return !Step_Flag(pCtx, p->nDstLayer, p->nCurY, p->nCurX, CELLFLAG_IS_BLANK);
}

/* The vertical pair for a large unit (0x2A6E8..0x2ADFC and 0x2ADFC..0x2B534). Here the
 * footprint is four cells and each one is tested against its own floor-below.
 *
 * ORIGINAL BUG (0x2ab80, residue): the non-flag branch repeats the same two cells four
 * times over (0x2ab80..0x2abf8) and seven times over (0x2b4f8..0x2b520). The port tests
 * each once, which is the same decision. */
static bool Step_LargeVertical(const StepCtx *pCtx)
{
    const StepProbe *p = pCtx->pProbe;
    const i32 nCurL = p->nCurLayer, nCurX = p->nCurX, nCurY = p->nCurY;
    const i32 nDstL = p->nDstLayer, nDstX = p->nDstX, nDstY = p->nDstY;

#define FL(l, y, x, w) Step_Flag(pCtx, (l), (y), (x), (w))
#define OCC(l, y, x)   Step_Occupied(pCtx, (l), (y), (x))

    if (p->nDir == 8) {
        if (pCtx->bSearchFlag) {
            if ((FL(nDstL, nCurY, nCurX, CELLFLAG_HAS_FLOOR)
                 && (!FL(nDstL, nDstY, nDstX, CELLFLAG_FLOOR_BLOCKS)
                     || !FL(nCurL, nCurY, nCurX, CELLFLAG_FLOOR_BLOCKS)))
                || (FL(nDstL, nCurY, nCurX + 1, CELLFLAG_HAS_FLOOR)
                    && (!FL(nDstL, nDstY, nDstX + 1, CELLFLAG_FLOOR_BLOCKS)
                        || !FL(nCurL, nCurY, nCurX + 1, CELLFLAG_FLOOR_BLOCKS)))
                || (FL(nDstL, nCurY + 1, nCurX, CELLFLAG_HAS_FLOOR)
                    && (!FL(nDstL, nDstY + 1, nDstX, CELLFLAG_FLOOR_BLOCKS)
                        || !FL(nCurL, nCurY + 1, nCurX, CELLFLAG_FLOOR_BLOCKS)))
                || (FL(nDstL, nCurY + 1, nCurX + 1, CELLFLAG_HAS_FLOOR)
                    && (!FL(nDstL, nDstY + 1, nDstX + 1, CELLFLAG_FLOOR_BLOCKS)
                        || !FL(nCurL, nCurY + 1, nCurX + 1, CELLFLAG_FLOOR_BLOCKS)))) {
                return false;
            }
        } else {
            if (!FL(nDstL, nDstY, nDstX, CELLFLAG_FLOOR_BLOCKS)
                || !FL(nCurL, nCurY, nCurX, CELLFLAG_FLOOR_BLOCKS)) {
                return false;
            }
        }

        if (!FL(nDstL, nCurY, nCurX, CELLFLAG_IS_BLANK)
            && !FL(nDstL, nCurY, nCurX + 1, CELLFLAG_IS_BLANK)
            && !FL(nDstL, nCurY + 1, nCurX, CELLFLAG_IS_BLANK)
            && !FL(nDstL, nCurY + 1, nCurX + 1, CELLFLAG_IS_BLANK)
            && !OCC(nDstL, nCurY, nCurX)
            && !OCC(nDstL, nCurY, nCurX + 1)
            && !OCC(nDstL, nCurY + 1, nCurX)
            && !OCC(nDstL, nCurY + 1, nCurX + 1)
            && !FL(nDstL, nCurY, nCurX + 1, CELLFLAG_WALL_A_BLANK)
            && !FL(nDstL, nCurY + 1, nCurX, CELLFLAG_WALL_B_BLANK)
            && !FL(nDstL, nCurY + 1, nCurX + 1, CELLFLAG_WALL_A_BLANK)) {
            return !FL(nDstL, nCurY + 1, nCurX + 1, CELLFLAG_WALL_B_BLANK);
        }
        return false;
    }

    /* Direction 9. The four cells below the footprint are the ones that have to hold:
     * the original reaches them as apCells[curLayer - 1][curY + dy][curX + dx] through
     * the 0x11AF8 base (0x2b2c8..0x2b384). */
    if (!pCtx->bSearchFlag) {
        if (!FL(nDstL, nDstY, nDstX, CELLFLAG_FLOOR_BLOCKS)
            || !FL(nCurL, nCurY, nCurX, CELLFLAG_FLOOR_BLOCKS)) {
            return false;
        }
    } else {
        if ((FL(nCurL, nCurY, nCurX, CELLFLAG_HAS_FLOOR)
             && (!FL(nDstL, nDstY, nDstX, CELLFLAG_FLOOR_BLOCKS)
                 || !FL(nCurL, nCurY, nCurX, CELLFLAG_FLOOR_BLOCKS)))
            || (FL(nCurL, nCurY, nCurX + 1, CELLFLAG_HAS_FLOOR)
                && (!FL(nDstL, nDstY, nDstX + 1, CELLFLAG_FLOOR_BLOCKS)
                    || !FL(nCurL, nCurY, nCurX + 1, CELLFLAG_FLOOR_BLOCKS)))
            || (FL(nCurL, nCurY + 1, nCurX, CELLFLAG_HAS_FLOOR)
                && (!FL(nDstL, nDstY + 1, nDstX, CELLFLAG_FLOOR_BLOCKS)
                    || !FL(nCurL, nCurY + 1, nCurX, CELLFLAG_FLOOR_BLOCKS)))) {
            return false;
        }
        if (FL(nCurL, nCurY + 1, nCurX + 1, CELLFLAG_HAS_FLOOR)) {
            if (!FL(nDstL, nDstY + 1, nDstX + 1, CELLFLAG_FLOOR_BLOCKS)) {
                return false;
            }
            if (!FL(nCurL, nCurY + 1, nCurX + 1, CELLFLAG_FLOOR_BLOCKS)) {
                return false;
            }
        }
    }

    if (OCC(nDstL, nCurY, nCurX)
        || OCC(nDstL, nCurY, nCurX + 1)
        || OCC(nDstL, nCurY + 1, nCurX)
        || OCC(nDstL, nCurY + 1, nCurX + 1)
        || FL(nCurL - 1, nCurY, nCurX, CELLFLAG_IS_BLANK)
        || FL(nCurL - 1, nCurY, nCurX + 1, CELLFLAG_IS_BLANK)
        || FL(nCurL - 1, nCurY + 1, nCurX, CELLFLAG_IS_BLANK)
        || FL(nCurL - 1, nCurY + 1, nCurX + 1, CELLFLAG_IS_BLANK)
        || FL(nCurL - 1, nCurY, nCurX + 1, CELLFLAG_WALL_A_BLANK)
        || FL(nCurL - 1, nCurY + 1, nCurX, CELLFLAG_WALL_B_BLANK)
        || FL(nCurL - 1, nCurY + 1, nCurX + 1, CELLFLAG_WALL_A_BLANK)) {
        return false;
    }
    return !FL(nCurL - 1, nCurY + 1, nCurX + 1, CELLFLAG_WALL_B_BLANK);

#undef OCC
#undef FL
}

/* The eight horizontal arms for a normal unit (0x27D7C..0x28868). Every one of them
 * runs the probe, then the arm's own occupant gate on the settled candidate, then a
 * list of wall and blank tests over the cells this direction reaches, and ends with one
 * more test whose result is the answer.
 *
 * The lists are direction-specific and deliberately uneven: a diagonal direction tests
 * the walls of the two cells it passes between as well as the cell it enters, which is
 * how the original keeps a mover from cutting a corner through a wall. */
static bool Step_NormalArm(const StepCtx *pCtx)
{
    const StepProbe *p = pCtx->pProbe;
    const i32 nCurX = p->nCurX, nCurY = p->nCurY;
    const i32 nDstL = p->nDstLayer, nDstX = p->nDstX, nDstY = p->nDstY;

#define F(l, y, x, w) Step_Flag(pCtx, (l), (y), (x), (w))

    if (!Map_ProbeStepOccupants(pCtx)) {
        return false;
    }
    if (!Step_ArmOccupantOk(pCtx, p->nAdjLayer, nDstY, nDstX)) {
        return false;
    }

    switch (p->nDir) {
    case 0:
        return !F(nDstL, nCurY, nCurX, CELLFLAG_WALL_B_BLANK);

    case 1:
        if (F(nDstL, nCurY, nCurX, CELLFLAG_WALL_B_BLOCKS)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_A_BLOCKS)
            || F(nDstL, nCurY, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_IS_BLANK)) {
            return false;
        }
        return !F(nDstL, nDstY, nCurX, CELLFLAG_IS_BLANK);

    case 2:
        return !F(nDstL, nDstY, nDstX, CELLFLAG_WALL_A_BLANK);

    case 3:
        if (F(nDstL, nDstY, nDstX, CELLFLAG_WALL_A_BLOCKS)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_B_BLOCKS)
            || F(nDstL, nDstY, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_IS_BLANK)) {
            return false;
        }
        return !F(nDstL, nDstY, nCurX, CELLFLAG_IS_BLANK);

    case 4:
        return !F(nDstL, nDstY, nDstX, CELLFLAG_WALL_B_BLANK);

    case 5:
        if (F(nDstL, nCurY, nCurX, CELLFLAG_WALL_A_BLOCKS)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_B_BLOCKS)
            || F(nDstL, nCurY, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_IS_BLANK)) {
            return false;
        }
        return !F(nDstL, nDstY, nCurX, CELLFLAG_IS_BLANK);

    case 6:
        return !F(nDstL, nCurY, nCurX, CELLFLAG_WALL_A_BLANK);

    case 7:
        if (F(nDstL, nCurY, nCurX, CELLFLAG_WALL_A_BLOCKS)
            || F(nDstL, nCurY, nCurX, CELLFLAG_WALL_B_BLOCKS)
            || F(nDstL, nCurY, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_IS_BLANK)) {
            return false;
        }
        return !F(nDstL, nDstY, nCurX, CELLFLAG_IS_BLANK);

    default:
        return true; /* 0x28998: the switch default accepts */
    }

#undef F
}

/* The eight horizontal arms for a large unit (0x28A4C..0x2A6E8). A large unit covers a
 * 2x2 footprint, so each arm tests the four cells it is about to cover - the candidate
 * and the three cells that complete the square - for blank tiles, occupants it may not
 * share, and the walls between them.
 *
 * Each arm also re-runs Map_IsValidCell for the far corner of the footprint.
 * ORIGINAL BUG (0x28c90, residue): it passes nLayer = 0 there, and Map_IsValidCell
 * does test the layer, so the call is a pure x/y test - the layer was already
 * validated by the head, which makes it harmless but not what the call says. */
static bool Step_LargeArm(const StepCtx *pCtx)
{
    const StepProbe *p = pCtx->pProbe;
    const i32 nCurX = p->nCurX, nCurY = p->nCurY;
    const i32 nDstL = p->nDstLayer, nDstX = p->nDstX, nDstY = p->nDstY;

#define F(l, y, x, w) Step_Flag(pCtx, (l), (y), (x), (w))
#define O(l, y, x)    Step_Occupied(pCtx, (l), (y), (x))
#define OK(l, y, x)   Step_LargeOccupantOk(pCtx, (l), (y), (x))

    if (!Map_ProbeStepOccupants(pCtx)) {
        return false;
    }

    switch (p->nDir) {
    case 0:
        if (F(nDstL, nDstY, nCurX + 1, CELLFLAG_IS_BLANK)
            || F(nDstL, nDstY, nCurX, CELLFLAG_IS_BLANK)) {
            return false;
        }
        if (!OK(nDstL, nDstY, nCurX + 1) || !OK(nDstL, nDstY, nCurX)) {
            return false;
        }
        if (F(nDstL, nCurY, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY, nCurX + 1, CELLFLAG_WALL_B_BLANK)) {
            return false;
        }
        if (F(nDstL, nDstY, nCurX + 1, CELLFLAG_WALL_A_BLANK)) {
            return false;
        }
        return !F(nDstL, nDstY, nCurX + 1, CELLFLAG_WALL_A_BLOCKS);

    case 1:
        if (!Map_IsValidCell(pCtx->pGeom, nDstX + 1, nDstY, 0)) {
            return false;
        }
        if (F(nDstL, nDstY, nDstX, CELLFLAG_IS_BLANK)
            || F(nDstL, nDstY, nDstX + 1, CELLFLAG_IS_BLANK)
            || F(nDstL, nCurY, nDstX + 1, CELLFLAG_IS_BLANK)) {
            return false;
        }
        if (!OK(nDstL, nDstY, nDstX) || !OK(nDstL, nDstY, nDstX + 1)
            || !OK(nDstL, nCurY, nDstX + 1)) {
            return false;
        }
        if (F(nDstL, nCurY, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY, nDstX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY + 1, nDstX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nDstX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY, nDstX + 1, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY + 1, nDstX + 1, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_A_BLOCKS)
            || F(nDstL, nDstY, nDstX + 1, CELLFLAG_WALL_A_BLOCKS)
            || F(nDstL, nCurY, nDstX + 1, CELLFLAG_WALL_B_BLOCKS)) {
            return false;
        }
        return !F(nDstL, nCurY + 1, nDstX + 1, CELLFLAG_WALL_B_BLOCKS);

    case 2:
        if (!Map_IsValidCell(pCtx->pGeom, nDstX + 1, nDstY, 0)) {
            return false;
        }
        if (F(nDstL, nCurY, nDstX + 1, CELLFLAG_IS_BLANK)
            || F(nDstL, nCurY + 1, nDstX + 1, CELLFLAG_IS_BLANK)) {
            return false;
        }
        if (!OK(nDstL, nCurY, nDstX + 1) || !OK(nDstL, nCurY + 1, nDstX + 1)) {
            return false;
        }
        if (F(nDstL, nCurY, nDstX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY + 1, nDstX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY + 1, nDstX + 1, CELLFLAG_WALL_B_BLANK)) {
            return false;
        }
        return !F(nDstL, nCurY + 1, nDstX + 1, CELLFLAG_WALL_B_BLOCKS);

    case 3:
        if (!Map_IsValidCell(pCtx->pGeom, nDstX + 1, nDstY + 1, 0)) {
            return false;
        }
        if (F(nDstL, nCurY, nDstX + 1, CELLFLAG_IS_BLANK)
            || F(nDstL, nDstY, nDstX + 1, CELLFLAG_IS_BLANK)
            || F(nDstL, nDstY + 1, nDstX + 1, CELLFLAG_IS_BLANK)
            || F(nDstL, nDstY + 1, nDstX, CELLFLAG_IS_BLANK)) {
            return false;
        }
        if (!OK(nDstL, nCurY, nDstX + 1) || !OK(nDstL, nDstY, nDstX + 1)
            || !OK(nDstL, nDstY + 1, nDstX + 1) || !OK(nDstL, nDstY + 1, nDstX)) {
            return false;
        }
        if (F(nDstL, nCurY, nDstX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nDstX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY + 1, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY + 1, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY, nDstX + 1, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY + 1, nDstX + 1, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY + 1, nDstX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY + 1, nDstX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nDstX + 1, CELLFLAG_WALL_B_BLOCKS)
            || F(nDstL, nDstY + 1, nDstX + 1, CELLFLAG_WALL_B_BLOCKS)
            || F(nDstL, nDstY + 1, nDstX + 1, CELLFLAG_WALL_A_BLOCKS)) {
            return false;
        }
        return !F(nDstL, nDstY + 1, nDstX, CELLFLAG_WALL_A_BLOCKS);

    case 4:
        if (!Map_IsValidCell(pCtx->pGeom, nDstX, nDstY + 1, 0)) {
            return false;
        }
        if (F(nDstL, nDstY + 1, nCurX, CELLFLAG_IS_BLANK)
            || F(nDstL, nDstY + 1, nCurX + 1, CELLFLAG_IS_BLANK)) {
            return false;
        }
        if (!OK(nDstL, nDstY + 1, nCurX) || !OK(nDstL, nDstY + 1, nCurX + 1)) {
            return false;
        }
        if (F(nDstL, nDstY + 1, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY + 1, nCurX + 1, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY + 1, nCurX + 1, CELLFLAG_WALL_A_BLANK)) {
            return false;
        }
        return !F(nDstL, nDstY, nCurX, CELLFLAG_WALL_A_BLOCKS);

    case 5:
        if (!Map_IsValidCell(pCtx->pGeom, nDstX, nDstY + 1, 0)) {
            return false;
        }
        if (F(nDstL, nDstY, nDstX, CELLFLAG_IS_BLANK)
            || F(nDstL, nDstY + 1, nDstX, CELLFLAG_IS_BLANK)
            || F(nDstL, nDstY + 1, nCurX, CELLFLAG_IS_BLANK)) {
            return false;
        }
        if (!OK(nDstL, nDstY, nDstX) || !OK(nDstL, nDstY + 1, nDstX)
            || !OK(nDstL, nDstY + 1, nCurX)) {
            return false;
        }
        if (F(nDstL, nCurY, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY + 1, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY + 1, nCurX + 1, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY + 1, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY + 1, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY + 1, nCurX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nDstX, CELLFLAG_WALL_B_BLOCKS)
            || F(nDstL, nDstY + 1, nDstX, CELLFLAG_WALL_B_BLOCKS)
            || F(nDstL, nDstY + 1, nCurX, CELLFLAG_WALL_A_BLOCKS)) {
            return false;
        }
        return !F(nDstL, nDstY + 1, nCurX + 1, CELLFLAG_WALL_A_BLOCKS);

    case 6:
        if (F(nDstL, nCurY, nDstX, CELLFLAG_IS_BLANK)
            || F(nDstL, nCurY + 1, nDstX, CELLFLAG_IS_BLANK)) {
            return false;
        }
        if (!OK(nDstL, nCurY, nDstX) || !OK(nDstL, nCurY + 1, nDstX)) {
            return false;
        }
        if (F(nDstL, nCurY, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY + 1, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY + 1, nDstX, CELLFLAG_WALL_B_BLANK)) {
            return false;
        }
        return !F(nDstL, nCurY + 1, nDstX, CELLFLAG_WALL_B_BLOCKS);

    case 7:
        if (F(nDstL, nDstY, nDstX, CELLFLAG_IS_BLANK)
            || F(nDstL, nDstY, nCurX, CELLFLAG_IS_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_IS_BLANK)) {
            return false;
        }
        if (!OK(nDstL, nDstY, nDstX) || !OK(nDstL, nDstY, nCurX)
            || !OK(nDstL, nCurY, nDstX)) {
            return false;
        }
        if (F(nDstL, nCurY, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY, nCurX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY, nCurX + 1, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY + 1, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nCurY + 1, nDstX, CELLFLAG_WALL_B_BLANK)
            || F(nDstL, nDstY, nCurX, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nDstY, nCurX + 1, CELLFLAG_WALL_A_BLANK)
            || F(nDstL, nCurY, nDstX, CELLFLAG_WALL_B_BLOCKS)
            || F(nDstL, nCurY + 1, nDstX, CELLFLAG_WALL_B_BLOCKS)) {
            return false;
        }
        if (F(nDstL, nDstY, nCurX, CELLFLAG_WALL_A_BLOCKS)) {
            return false;
        }
        return !F(nDstL, nDstY, nCurX + 1, CELLFLAG_WALL_A_BLOCKS);

    default:
        return true; /* 0x28a48: the switch default accepts */
    }

#undef OK
#undef O
#undef F
}

bool Unit_CanStepTo(i32 nX, i32 nY, i32 nLayer, u8 nDir,
                    i32 *pnOutX, i32 *pnOutY, i32 *pnOutLayer,
                    bool *pbReanchored, void *pUser)
{
    StepCtx *pCtx = (StepCtx *)pUser;
    StepProbe *p = pCtx->pProbe;
    const StepDir *pDir;
    bool bLarge;
    bool bOk;

    if (nDir >= PUFO_PATH_DIR_COUNT) {
        return true; /* 0x27d2c / 0x28a48: the switch default accepts */
    }

    /* 0x27bd8..0x27c24: the candidate comes from the direction table, and the probe's
     * inputs come from the caller's cell. The original has Path_ExpandCell seed the
     * first half and seeds this half itself. */
    pDir = &g_aStepDir[nDir];
    p->nCurX      = nX;
    p->nCurY      = nY;
    p->nCurLayer  = nLayer;
    p->nDir       = nDir;
    p->pCurCell   = Step_Cell(pCtx, nLayer, nY, nX);
    p->pCellAbove = (nLayer < 3) ? Step_Cell(pCtx, nLayer + 1, nY, nX) : NULL;
    p->nCeilParamNeg = (p->pCurCell != NULL && p->pCurCell->pCeilSpr != NULL)
                     ? -(i32)p->pCurCell->pCeilSpr->iCeilParam : 0;
    p->nDstX     = nX + pDir->dx;
    p->nDstY     = nY + pDir->dy;
    p->nDstLayer = nLayer + pDir->dz;

    bLarge = Unit_bIsLarge(pCtx->pSelUnit);

    /* The head, 0x27c50..0x27ca0 for a normal unit and 0x289f4..0x28a38 for a large
     * one. A normal unit's candidate must also not be a blank cell; a large unit's
     * blank test is per-arm, over its whole footprint. */
    if (!Map_IsValidCell(pCtx->pGeom, p->nDstX, p->nDstY, p->nDstLayer)) {
        return false;
    }
    p->pDstCell = Step_Cell(pCtx, p->nDstLayer, p->nDstY, p->nDstX);

    if (!bLarge) {
        if (p->pDstCell == NULL
            || Step_CellFlag(p->pDstCell, CELLFLAG_IS_BLANK)) {
            return false;
        }
        if (!Step_HeadOccupantOk(pCtx, p->pDstCell)) {
            return false;
        }
        bOk = (nDir >= 8) ? Step_NormalVertical(pCtx) : Step_NormalArm(pCtx);
    } else {
        bOk = (nDir >= 8) ? Step_LargeVertical(pCtx) : Step_LargeArm(pCtx);
    }

    if (!bOk) {
        return false;
    }

    /* What Path_ExpandCell consumes. The original hands it the settled cell through
     * g_pStepDstCell (0x2b6d4, read at 0x2b768) and the layer through
     * g_nStepDstLayer (0x2b74c); a seam has to express the cell as a layer, and the
     * layer the settled cell lives in is g_nStepAdjLayer - the two are the same cell
     * because Map_ProbeStepOccupants keeps them in step. The vertical pair never calls
     * the probe, so for those two directions the settled cell is the head's candidate
     * and its layer is g_nStepDstLayer.
     *
     * 0x2898c..0x28998: the vertical pair clears the re-anchor report on success; the
     * horizontal arms leave whatever Map_ProbeStepOccupants wrote. */
    *pnOutX       = p->nDstX;
    *pnOutY       = p->nDstY;
    *pnOutLayer   = (nDir >= 8) ? p->nDstLayer : p->nAdjLayer;
    *pbReanchored = (nDir >= 8) ? false : (p->bReanchored != 0);
    return true;
}
