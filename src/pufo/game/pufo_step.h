/* pufo_step.h - the step-legality probe the path flood fill runs.
 *
 * Evidence: Map_ProbeStepOccupants @ 0x2778c, Path_ExpandCell @ 0x2b604 (which seeds
 *           the probe), MapCell_TestFlag @ 0x549c8, MapCell_HasOccupant @ 0x5487c,
 *           Unit_CanStepTo (0x27bb8, ported in pieces - see the note at the bottom).
 *
 * ## The probe is a global-state machine
 *
 * Unit_CanStepTo takes one argument, the Map block, and reads everything else from
 * globals that Path_ExpandCell seeds before each call:
 *
 *   g_nStepCurX          @ 0x10dba8  the cell being expanded
 *   g_nStepCurY          @ 0x10dbac
 *   g_nCurLayer          @ 0x113980
 *   g_nStepDir           @ 0x10dbb4  the direction under test
 *   g_pStepCurCell       @ 0x138e90  apCells[curLayer][curY][curX]        (0x2b678)
 *   g_pStepCellAbove     @ 0x11399c  apCells[curLayer+1][curY][curX], and only when
 *                                    curLayer < 3 - the pair is LDRLT at 0x2b658/0x2b67c
 *   g_nStepCeilParamNeg  @ 0x113988  -(i8)g_pStepCurCell->pCeilSpr->iCeilParam, or 0
 *                                    when there is no ceiling sprite (0x2b684..0x2b69c)
 *
 * and it writes the candidate back into globals:
 *
 *   g_nStepDstX          @ 0x113990  curX + dx
 *   g_nStepDstY          @ 0x113994  curY + dy
 *   g_nStepDstLayer      @ 0x138e94  curLayer + dz
 *   g_pStepDstCell       @ 0x113998  the candidate cell
 *   g_nStepAdjLayer      @ 0x1139a4  a second layer the direction arms index with
 *   g_bStepReanchored    @ 0x113984  set when the candidate moved a layer
 *
 * Four call sites seed the inputs: Path_ExpandCell (the flood), Map_ComputeFov
 * @ 0x2deac, Map_ScanBlastFootprint @ 0x32cd4 and GameTick_Step @ 0x27324. The port
 * keeps the same shape as an explicit struct so the probe is testable and the four
 * callers do not have to share mutable globals.
 *
 * ## The vertical re-anchor
 *
 * A step is not always the cell the direction delta points at. Map_ProbeStepOccupants
 * runs first in every direction arm and can move the candidate one layer down or one
 * layer up:
 *
 *   down (0x27990..0x27a54): the candidate has no floor sprite, the current cell has
 *     one, the search flag is clear and the candidate is above layer 0 - the candidate
 *     becomes apCells[dstLayer-1][dstY][dstX] and g_nStepAdjLayer drops by one.
 *   up   (0x27a6c..0x27b1c): the current cell has a ceiling whose break parameter is
 *     below -15, the cell above the current one has no floor, and the cell above the
 *     candidate has a floor - the candidate becomes apCells[dstLayer+1][dstY][dstX],
 *     g_nStepDstLayer and g_nStepAdjLayer each rise by one.
 *
 * For a large unit (a 2x2 footprint) both tests gain a second column: the cell at
 * x+1 of the cell being moved to has to pass the same floor/ceiling test, and the
 * cell at x+1 of the current cell has to have a breakable ceiling.
 *
 * Two asymmetries are the original's and are reproduced (see docs/parity/bugs.tsv):
 * the downward anchor moves the candidate cell and g_nStepAdjLayer but leaves
 * g_nStepDstLayer alone (0x27a48), and for a large unit stepping due east the upward
 * anchor is unreachable because the cell it wants floorless is the same cell the
 * preceding test wants floored (0x27ae4 against 0x27a94).
 *
 * ## Unit_CanStepTo
 *
 * Unit_CanStepTo @ 0x27bb8 (14,716 B) is the direction switch on top of this probe:
 * ten arms for a normal unit and ten for a large one, each checking the destination
 * footprint for blank cells, blocking walls and foreign occupants. It seeds
 * g_nStepDstX/Y/Layer itself from aDirDeltaXY[g_nStepDir] (0x27bd8..0x27c24), runs a
 * head test common to every direction, then dispatches.
 *
 * The head. Normal unit: the candidate must be in range, must not be blank
 * (MapCell_TestFlag 0), and its occupant must be the swap partner, the mover, or a
 * foreign unit that is not flagged at bUnk148, is not operative, and is not unit type
 * 16 or 17. Large unit: only the range test, and g_pStepDstCell is set before the
 * switch.
 *
 * The arms. Each one runs Map_ProbeStepOccupants first (except the two vertical
 * directions, 8 and 9, which never call it) and then a direction-specific list of
 * blank-cell, occupant and wall tests over the footprint it is about to occupy. The
 * wall tests are the interesting part: a normal unit tests the walls of the cells it
 * leaves and the cell it enters, and a large unit tests the walls of all four cells of
 * its 2x2 footprint, which is why the same flag shows up on three or four cells at
 * once.
 *
 * The occupant gate is weaker inside the arms than in the head: an arm accepts a
 * foreign occupant as soon as its bUnk148 is clear, without the operative or unit-type
 * tests the head runs.
 *
 * Deviations: the original indexes its fixed 60x60x4 grid with no bounds test inside
 * the arms, so an out-of-range coordinate reads a different cell of the same array;
 * the port's injected lookup returns NULL there and every helper treats NULL as "no
 * flag, no occupant". The one cell the port still dereferences blindly is
 * Map_ProbeStepOccupants' candidate, because the original does - an injected lookup
 * has to cover every coordinate Map_IsValidCell calls valid. The original also passes
 * nLayer = 0 to the large arms' second Map_IsValidCell call (0x28c90), which makes
 * that call a pure x/y test - the layer was already validated by the head.
 */
#ifndef PUFO_STEP_H
#define PUFO_STEP_H

#include "game/pufo_mapcell.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

/* The ceiling-break thresholds the probe uses. -15 opens the re-anchor upward,
 * -8 is the final rejection test, and the tactical map click uses -23 on the same
 * sprite byte (see Tact_HandleMapClick). All three read Mc1Rec.iCeilParam. */
#define PUFO_STEP_CEIL_REANCHOR (-15)
#define PUFO_STEP_CEIL_REJECT    (-8)

/* The probe's own state: the inputs the caller seeds and the outputs the probe
 * writes. One struct per probe run; the original shares one set of globals. */
typedef struct StepProbe {
    /* seeded by the caller */
    i32     nCurX;        /* g_nStepCurX @ 0x10dba8 */
    i32     nCurY;        /* g_nStepCurY @ 0x10dbac */
    i32     nCurLayer;    /* g_nCurLayer @ 0x113980 */
    u8      nDir;         /* g_nStepDir @ 0x10dbb4 */
    MapCell *pCurCell;    /* g_pStepCurCell @ 0x138e90 */
    MapCell *pCellAbove;  /* g_pStepCellAbove @ 0x11399c, NULL when curLayer >= 3 */
    i32     nCeilParamNeg;/* g_nStepCeilParamNeg @ 0x113988 */

    /* written by the probe */
    i32     nDstX;        /* g_nStepDstX @ 0x113990 */
    i32     nDstY;        /* g_nStepDstY @ 0x113994 */
    i32     nDstLayer;    /* g_nStepDstLayer @ 0x138e94 */
    i32     nAdjLayer;    /* g_nStepAdjLayer @ 0x1139a4 */
    MapCell *pDstCell;    /* g_pStepDstCell @ 0x113998 */
    u8      bReanchored;  /* g_bStepReanchored @ 0x113984 */
} StepProbe;

typedef struct StepCtx {
    MapCellLookup  pfnLookup;
    void          *pLookupUser;
    const MapGeom *pGeom;           /* the grid bounds Map_IsValidCell tests */
    Soldier       *pSelUnit;        /* Map.pSelUnit @ Map+0x15334 */
    Soldier       *pSwapUnit;       /* Map_pPathDestUnit @ Map+0x150f4 */
    bool           bSearchFlag;     /* g_bPathSearchFlag @ 0x10db84 */
    StepProbe     *pProbe;
} StepCtx;

/* Map_ProbeStepOccupants @ 0x2778c. Seeds g_nStepAdjLayer from the candidate layer,
 * clears g_bStepReanchored, and then tries the two vertical re-anchors in order.
 * Returns false when the candidate is rejected outright, which is what every
 * direction arm of Unit_CanStepTo tests first.
 *
 * The original recomputes Unit_bIsLarge(pSelUnit) eight times in this function; the
 * port computes it once, which is the same value every time. */
bool Map_ProbeStepOccupants(const StepCtx *pCtx);

/* Unit_CanStepTo @ 0x27bb8, behind Path_ComputeReachable's PathStepProbe seam; pUser
 * is a StepCtx *. It seeds the probe's candidate from g_aStepDir[nDir] the way the
 * original seeds its globals (0x27bd8..0x27c24), runs the head test every direction
 * shares, then the direction arm. On true the outputs carry the settled candidate -
 * g_nStepDstX, g_nStepDstY, g_nStepDstLayer - and the probe's re-anchor report, which
 * is what Path_ExpandCell reads at 0x2b748..0x2b7bc. */
bool Unit_CanStepTo(i32 nX, i32 nY, i32 nLayer, u8 nDir,
                    i32 *pnOutX, i32 *pnOutY, i32 *pnOutLayer,
                    bool *pbReanchored, void *pUser);

#endif /* PUFO_STEP_H */
