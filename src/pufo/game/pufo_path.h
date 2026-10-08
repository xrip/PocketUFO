/* pufo_path.h - tactical reachability search.
 *
 * Evidence: Map_FacingDelta @ 0x276a0, Path_EnqueueCell @ 0x2b534,
 *           Path_ExpandCell @ 0x2b604, Map_ComputeReachable @ 0x2b820,
 *           Unit_CanStepTo @ 0x27bb8 (predicate only).
 *
 * The original runs a cost-based flood fill over the cell grid. Two frontier
 * lists are used alternately (double buffering), selected by byte_10DB80:
 *
 *   list A: dword_110AA0 @ 0x110aa0, count dword_1139A8 @ 0x1139a8
 *   list B: dword_10DBC0 @ 0x10dbc0, count dword_11398C @ 0x11398c
 *
 * Map_ComputeReachable @ 0x2b820:
 *   1. clear every cell's accumulated cost (nUnk20) and two flag bits
 *   2. bail out if the TARGET cell tests as blank (MapCell_TestFlag selector 0)
 *   3. seed the selected unit's own cell with cost 1
 *   4. expand list A, then list B, then A again, ... until either both lists are
 *      empty, or the target cell has a non-zero cost (a path exists)
 *
 * Path_ExpandCell @ 0x2b604, for one frontier cell, tries ten directions and
 * relaxes the neighbour's cost:
 *
 *   cost = curCell->nUnk20                (truncated to u16)
 *        + Map_FacingDelta(dir, curCell->bUnk24)   (turn cost, 0..4)
 *        + 4
 *        + (dir < 8 && dir odd) ? 2 : 0    (diagonal surcharge)
 *        + (layer != seedLayer && !bFlag) ? 4 : 0
 *
 * and updates the neighbour only when its cost is 0 or greater than the new
 * value, then enqueues it when cost < 140. The 140 cap is the original's bound on
 * how far the flood is allowed to spread.
 */
#ifndef PUFO_PATH_H
#define PUFO_PATH_H

#include "game/pufo_mapcell.h"
#include "pufo_common.h"

/* Frontier list capacities. Measured from the original's arrays:
 *   dword_110AA0 @ 0x110aa0  -> 12024 bytes to the next named symbol = 3006 u32
 *   dword_10DBC0 @ 0x10dbc0  -> 24024 bytes                                = 6006 u32
 * Both lists need at most the cell count, and the smaller one (3006) is what
 * actually bounds a frontier, so that is the capacity used for both. The cost
 * limit of 140 is the stricter bound in practice. */
#define PUFO_PATH_LIST_CAP_ORIGINAL_A 3006
#define PUFO_PATH_LIST_CAP_ORIGINAL_B 6006
#define PUFO_PATH_LIST_CAP 3006

/* Directions tried per cell. 0..7 are the compass, 8 and 9 are the vertical
 * steps (which is why the odd-direction surcharge is gated on dir < 8). */
#define PUFO_PATH_DIR_COUNT 10

/* Step deltas per direction, from aDirDeltaXY @ 0xa7ae0. The table is 30 bytes
 * of signed triples; decoded:
 *     0: ( 0,-1, 0)   1: ( 1,-1, 0)   2: ( 1, 0, 0)   3: ( 1, 1, 0)
 *     4: ( 0, 1, 0)   5: (-1, 1, 0)   6: (-1, 0, 0)   7: (-1,-1, 0)
 *     8: ( 0, 0, 1)   9: ( 0, 0,-1)
 * Even compass directions are orthogonal, odd ones are the diagonals that take
 * the +2 surcharge. 8 and 9 are the vertical steps. This is the authoritative
 * copy; the unit step legality test and the tests both read it. */
typedef struct StepDir {
    i8 dx;
    i8 dy;
    i8 dz;
} StepDir;

extern const StepDir g_aStepDir[PUFO_PATH_DIR_COUNT];

/* Maximum accumulated cost that still gets enqueued, from Path_ExpandCell. */
#define PUFO_PATH_COST_LIMIT 140

/* Base cost added per step, and the two surcharges. */
#define PUFO_PATH_BASE_COST     4
#define PUFO_PATH_DIAGONAL_COST 2
#define PUFO_PATH_LAYER_COST    4

/* Circular distance between two facings, Map_FacingDelta @ 0x276a0.
 * Returns 0 when either facing is out of range 0..7. */
u8 Map_FacingDelta(u8 nFrom, u8 nTo);

/* Map_bFacingTurnPositive @ 0x27668: whether turning from nFrom to nTo is a
 * positive (clockwise) rotation.
 *
 * Verified from the disassembly:
 *
 *     v3 = nTo - nFrom;
 *     v4 = 1;
 *     if (v3 < 0) v4 = -1;
 *     if (abs32(v3) > 4) v4 = -v4;   // the short way round flips the sign
 *     return v4 > 0;
 *
 * Note the threshold is `> 4`, matching Map_FacingDelta's substitution point, so
 * the two agree on which way round is shorter. A diff of exactly 4 is treated as
 * positive (the original does not flip at 4, only above it). */
bool Map_bFacingTurnPositive(u8 nFrom, u8 nTo);

/* A step probe. Mirrors Unit_CanStepTo @ 0x27bb8, which reads the cell grid and
 * the selected unit through globals. It is injected here because the real
 * function is ~14.7 KB and is its own body of work; the flood fill above it is
 * what this module owns.
 *
 * Implementations must fill the neighbour coordinates when the step is allowed
 * and return true; on false the outputs are ignored. */
typedef bool (*PathStepProbe)(i32 nX, i32 nY, i32 nLayer, u8 nDir,
                              i32 *pnOutX, i32 *pnOutY, i32 *pnOutLayer,
                              void *pUser);

typedef struct PathScratch {
    MapCell *apListA[PUFO_PATH_LIST_CAP];
    MapCell *apListB[PUFO_PATH_LIST_CAP];
    i32      nCountA;
    i32      nCountB;
    bool     bUseA; /* byte_10DB80: which list is the current frontier */
} PathScratch;

/* Path_EnqueueCell @ 0x2b534. Appends pCell to the active list unless it is
 * already present, and sets flag bit 1 in the cell's bFlags44. Returns true when
 * the cell was appended (the original returns the index or the cell pointer; the
 * boolean is what callers need). */
bool Path_EnqueueCell(PathScratch *pScratch, MapCell *pCell);

/* Map_ComputeReachable @ 0x2b820. Clears costs across the grid, seeds the start
 * cell, and floods until the target is reached or the frontier is exhausted.
 * Returns true when the target cell ended up with a non-zero cost.
 *
 * pfnCells maps (x, y, layer) to a cell; the original indexes its own grid. */
typedef MapCell *(*PathCellLookup)(i32 nX, i32 nY, i32 nLayer, void *pUser);

bool Path_ComputeReachable(PathScratch *pScratch,
                           i32 nSeedX, i32 nSeedY, i32 nSeedLayer, u8 nSeedFacing,
                           i32 nTargetX, i32 nTargetY, i32 nTargetLayer,
                           PathCellLookup pfnLookup, PathStepProbe pfnProbe,
                           void *pUser);

#endif /* PUFO_PATH_H */
