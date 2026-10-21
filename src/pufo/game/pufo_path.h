/* pufo_path.h - tactical reachability search.
 *
 * Evidence: Map_FacingDelta @ 0x276a0, Path_EnqueueCell @ 0x2b534,
 *           Path_ExpandCell @ 0x2b604, Map_ComputeReachable @ 0x2b820,
 *           Unit_PathToCell @ 0x2c2a8, byte_A7B00 @ 0xa7b00,
 *           Unit_CanStepTo (0x27bb8) -- ported in pufo_step.c; not wired into the path seam yet.
 *
 * The original runs a cost-based flood fill over the cell grid. Two frontier
 * lists are used alternately (double buffering), selected by byte_10DB80:
 *
 *   list A: dword_110AA0 @ 0x110aa0, count dword_1139A8 @ 0x1139a8
 *   list B: dword_10DBC0 @ 0x10dbc0, count dword_11398C @ 0x11398c
 *
 * Map_ComputeReachable @ 0x2b820:
 *   1. clear every cell's accumulated cost (nPathCost) and two flag bits
 *   2. bail out if the TARGET cell tests as blank (MapCell_TestFlag selector 0)
 *   3. seed the selected unit's own cell with cost 1
 *   4. expand list A, then list B, then A again, ... until either both lists are
 *      empty, or the target cell has a non-zero cost (a path exists)
 *
 * Its sixth argument, bFlag, is not used in the body: it is stored to the global
 * byte_10DB84 (0x10db84) at 0x2b860 and Path_ExpandCell reads it back from there
 * (0x2b6ac). The port passes it as an explicit argument instead of a global.
 *
 * Path_ExpandCell @ 0x2b604, for one frontier cell, tries ten directions and
 * relaxes the neighbour's cost:
 *
 *   cost = curCell->nPathCost                (truncated to u16)
 *        + Map_FacingDelta(dir, curCell->bPathDir)   (turn cost, 0..4)
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
#include "game/pufo_tactstate.h" /* TactState, Map_QueueDoorAction, DoorActionCtx */
#include "game/pufo_unit.h"      /* Soldier, g_aUnitTypeFlag0, Unit_bIsLarge */
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

/* A step probe: Unit_CanStepTo (0x27bb8) behind a seam. The original reads the grid,
 * the selected unit and the swap partner through globals and writes the settled
 * candidate back into globals, which is what Path_ExpandCell reads at
 * 0x2b748..0x2b7bc; the port passes the candidate and the re-anchor report out
 * through arguments instead.
 *
 * Implementations must fill the neighbour coordinates and *pbReanchored when the step
 * is allowed; on false the outputs are ignored. *pbReanchored is the original's
 * g_bStepReanchored: true when the probe moved the candidate a layer on its own
 * (Map_ProbeStepOccupants). That flag, not the search flag, is what suppresses the
 * +4 layer surcharge at 0x2b764 and what Path_ExpandCell stores into bFlags29 bit 3
 * at 0x2b7a4. */
typedef bool (*PathStepProbe)(i32 nX, i32 nY, i32 nLayer, u8 nDir,
                              i32 *pnOutX, i32 *pnOutY, i32 *pnOutLayer,
                              bool *pbReanchored, void *pUser);

typedef struct PathScratch {
    MapCell *apListA[PUFO_PATH_LIST_CAP];
    MapCell *apListB[PUFO_PATH_LIST_CAP];
    i32      nCountA;
    i32      nCountB;
    bool     bUseA; /* byte_10DB80: which list is the current frontier */
    bool     bSearchFlag; /* byte_10DB84 @ 0x10db84: Map_ComputeReachable stores the flood
                           * flag here (0x2b850/0x2b860); Unit_CanStepTo reads it in the
                           * vertical re-anchor arms, so the shell's probe thunk reads it
                           * from here too */
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

/* bFlag is the original's sixth argument, taken by Unit_PathToCell from
 * g_aUnitTypeRecord[4 * nUnitType] (0x2c33c). It reaches Path_ExpandCell through
 * the global g_bPathSearchFlag in the original; here it is an argument. Inside the
 * expansion it lifts exactly one rule: the "no floor sprite, do not expand" gate at
 * 0x2b6ac. It does NOT touch the +4 layer surcharge - that is gated on the probe's
 * own g_bStepReanchored report (0x2b75c), not on this flag. */
bool Path_ComputeReachable(PathScratch *pScratch,
                           i32 nSeedX, i32 nSeedY, i32 nSeedLayer, u8 nSeedFacing,
                           i32 nTargetX, i32 nTargetY, i32 nTargetLayer, bool bFlag,
                           PathCellLookup pfnLookup, PathStepProbe pfnProbe,
                           void *pUser);

/* --- Unit_PathToCell @ 0x2c2a8 --- */

/* The reconstructed path is written into a Map array the original keeps at
 * Map+0x4A44 (0x118400), with the step count in the dword right after it at
 * Map+0x4E44 (0x1187fc). Slot 0 is the destination and slots 1..nCount walk back
 * to the start, so the replay reads them in reverse. The walk gives up when the
 * count passes 200 (0x2c54c), and because the store happens before the test slot
 * 201 is the last one the original ever writes. */
#define PUFO_PATH_MAX_CELLS 202

/* Facing for a step delta, byte_A7B00 @ 0xa7b00: 27 bytes indexed
 *     3 * (3 * dz + dy) + dx + 13
 * over dx, dy, dz in {-1, 0, 1}. The dz == 0 band is the plain compass facing -
 * (0,-1) gives 0, (1,0) gives 2, (-1,-1) gives 7 - and it agrees with the other
 * two bands for every delta that has a horizontal component. The bands differ only
 * at the pure vertical delta: dz == 0 reports 0, dz == +1 reports 8 (up) and
 * dz == -1 reports 9 (down), matching g_aStepDir's directions 8 and 9.
 * Unit_PathToCell @ 0x2c2a8 is the table's only reader in the whole binary. */
#define PUFO_FACING_FROM_DELTA_COUNT 27
#define PUFO_FACING_FROM_DELTA_BIAS  13
extern const u8 g_aFacingFromDelta[PUFO_FACING_FROM_DELTA_COUNT];

/* The table lookup, with the index written out. Out-of-range deltas are clamped
 * to -1..1; the original's caller only ever produces -1..1 because every entry of
 * the path array is one step from the next. */
u8 Path_FacingFromDelta(i32 nDx, i32 nDy, i32 nDz);

/* Everything Unit_PathToCell reaches through the context base pointer. The Map
 * fields are pointers rather than offsets because the port's GameMap does not
 * keep the original's field order. */
typedef struct PathToCellCtx {
    TactState    *pState;          /* pMapTactical @ 0x157198 */
    Soldier     **ppSelUnit;       /* Map.pSelUnit @ Map+0x15334 (0x128cec) */
    Soldier     **ppPathDestUnit;  /* Map+0x150f4 (0x128ab0), see below */
    MapCell     **apPathCells;     /* Map_apPathCells @ Map+0x4A48 (0x118400), PUFO_PATH_MAX_CELLS slots */
    i32          *pnPathCount;     /* Map+0x4E44 */
    PathScratch  *pScratch;        /* the flood fill's frontier lists */
    MapCellLookup pfnLookup;
    PathStepProbe  pfnProbe;       /* Unit_CanStepTo (0x27bb8, ported in pufo_step.c) -- the shell
                                    * wires its own probe thunk over the live cells (Part 106);
                                    * a NULL seam still steps permissively by the delta table */
    void          *pProbeUser;       /* the user Path_ComputeReachable passes to BOTH readers
                                      * (the original's flood fill is one shared context) */
    void          *pLookupUser;      /* the user Unit_PathToCell's own lookups use */
    const MapGeom *pGeom;
} PathToCellCtx;

/* Unit_PathToCell @ 0x2c2a8. Flood-fills from the selected unit's own cell to
 * (nX, nY, nLayer), walks the result back through MapCell.bPathDir into the
 * context's path array, then replays that array as queue entries: an optional
 * stand-from-kneel, a turn whenever the facing changes, the door test for the
 * step, and either a step or a vertical step. Ends with the end marker and a
 * queue kick.
 *
 * bFlag picks the Map+0x150f4 bookkeeping: clear, the destination cell's occupant
 * is stored there; set, the slot is cleared. The same byte also feeds the flood
 * fill (see Path_ComputeReachable). Returns false when the target has no cost
 * (unreachable) or when the walk back exceeds PUFO_PATH_MAX_CELLS - 2.
 *
 * pUser is the PathToCellCtx, which is what lets this function drop straight into
 * the pfnPathToCell seams in the enemy AI and map-click contexts. */
bool Unit_PathToCell(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser);

#endif /* PUFO_PATH_H */
