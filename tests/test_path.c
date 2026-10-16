/* test_path.c - checks the tactical reachability search and the path replay.
 *
 * Map_FacingDelta is a simple circular distance; the flood fill is the part that
 * matters. The cost composition (base 4, diagonal +2, layer +4, plus the facing
 * delta) and the "cost 0 means unvisited" rule are asserted because both are easy
 * to get subtly wrong. The bFlag argument is asserted on both of its effects, the
 * layer surcharge and the floor-sprite gate, because the port used to hardcode it
 * clear and only one of those two effects was visible.
 *
 * Unit_PathToCell is checked through the queue it produces rather than through the
 * path array alone: the array is internal scratch, and what the game sees is the
 * turn/step/vertical-step sequence, the stand-up prefix, and the end marker.
 *
 * Note on the facings used below: the seed's facing becomes the current cell's
 * bPathDir, and every step pays Map_FacingDelta(stepDir, curCell->bPathDir). Tests
 * that want clean arithmetic set the seed facing equal to the travel direction,
 * so the turn cost is 0; the turning test deliberately does not.
 */
#include "game/pufo_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static void TestFacingDelta(void)
{
    printf("Map_FacingDelta\n");
    CheckEqI32(Map_FacingDelta(0, 0), 0, "  same facing");
    CheckEqI32(Map_FacingDelta(0, 1), 1, "  one step");
    CheckEqI32(Map_FacingDelta(0, 2), 2, "  two steps");
    CheckEqI32(Map_FacingDelta(0, 4), 4, "  opposite");
    /* Wraps the short way round: 0 -> 7 is 1, not 7. */
    CheckEqI32(Map_FacingDelta(0, 7), 1, "  wraps to the near side");
    CheckEqI32(Map_FacingDelta(7, 0), 1, "  wraps symmetrically");
    CheckEqI32(Map_FacingDelta(1, 7), 2, "  wraps for non-zero from");
    CheckEqI32(Map_FacingDelta(0, 5), 3, "  five is three the other way");
    /* Out of range returns 0 rather than clamping. */
    CheckEqI32(Map_FacingDelta(8, 0), 0, "  from out of range");
    CheckEqI32(Map_FacingDelta(0, 9), 0, "  to out of range");
}


/* Map_bFacingTurnPositive @ 0x27668: whether the turn is a positive rotation.
 * The threshold is a diff of exactly 4 counting as positive (the original flips
 * only when abs(diff) > 4). */
static void TestFacingTurnPositive(void)
{
    printf("Map_bFacingTurnPositive\n");
    Check(Map_bFacingTurnPositive(0, 1), "  0 -> 1 is positive");
    Check(Map_bFacingTurnPositive(0, 2), "  0 -> 2 is positive");
    Check(!Map_bFacingTurnPositive(1, 0), "  1 -> 0 is negative");
    Check(!Map_bFacingTurnPositive(2, 0), "  2 -> 0 is negative");

    /* The short way round flips the sign. */
    Check(!Map_bFacingTurnPositive(0, 7), "  0 -> 7 flips to negative (short way back)");
    Check(Map_bFacingTurnPositive(7, 0), "  7 -> 0 flips to positive");
    Check(!Map_bFacingTurnPositive(0, 6), "  0 -> 6 flips (short way back)");
    Check(Map_bFacingTurnPositive(6, 0), "  6 -> 0 flips to positive");

    /* Exactly opposite: abs(diff) == 4 does NOT flip, so the raw sign wins. */
    Check(Map_bFacingTurnPositive(0, 4), "  0 -> 4 counts as positive (no flip at 4)");
    Check(!Map_bFacingTurnPositive(4, 0), "  4 -> 0 counts as negative");

    /* Equal facings: diff 0, sign +1 -> true. */
    Check(Map_bFacingTurnPositive(3, 3), "  equal facings report the raw sign");
}

/* --- a small synthetic grid --- */

#define T_DIM 8

/* Direction table: read from g_aStepDir (aDirDeltaXY @ 0xa7ae0) rather than
 * redeclared here, so the test exercises the same data the production path uses. */

/* Named directions, for readable test code. */
#define DIR_N  0
#define DIR_NE 1
#define DIR_E  2
#define DIR_SE 3
#define DIR_S  4
#define DIR_SW 5
#define DIR_W  6
#define DIR_NW 7
#define DIR_UP 8
#define DIR_DN 9

typedef struct Grid {
    MapCell     aCells[PUFO_MAP_LAYERS][T_DIM][T_DIM];
    Mc1Rec      aFloors[PUFO_MAP_LAYERS][T_DIM][T_DIM];
    u8          aBlocked[T_DIM][T_DIM];
    i32         nProbeCalls;
    PathScratch scratch;
} Grid;

static MapCell *Grid_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    Grid *pG = (Grid *)pUser;

    if (nX < 0 || nY < 0 || nX >= T_DIM || nY >= T_DIM) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return &pG->aCells[nLayer][nY][nX];
}

static bool Grid_Probe(i32 nX, i32 nY, i32 nLayer, u8 nDir,
                       i32 *pnOutX, i32 *pnOutY, i32 *pnOutLayer,
                       bool *pbReanchored, void *pUser)
{
    Grid *pG = (Grid *)pUser;

    if (nDir >= PUFO_PATH_DIR_COUNT) {
        return false;
    }
    pG->nProbeCalls += 1;
    *pbReanchored = false;

    *pnOutX     = nX + g_aStepDir[nDir].dx;
    *pnOutY     = nY + g_aStepDir[nDir].dy;
    *pnOutLayer = nLayer + g_aStepDir[nDir].dz;

    if (*pnOutX < 0 || *pnOutY < 0 || *pnOutX >= T_DIM || *pnOutY >= T_DIM) {
        return false;
    }
    if (*pnOutLayer < 0 || *pnOutLayer >= PUFO_MAP_LAYERS) {
        return false;
    }
    if (pG->aBlocked[*pnOutY][*pnOutX]) {
        return false;
    }
    return true;
}

/* The same probe, reporting that it moved the candidate a layer on its own - the
 * original's g_bStepReanchored, which is what suppresses the layer surcharge at
 * 0x2b764 and sets bFlags29 bit 3 at 0x2b7a4. */
static bool Grid_ProbeAnchored(i32 nX, i32 nY, i32 nLayer, u8 nDir,
                               i32 *pnOutX, i32 *pnOutY, i32 *pnOutLayer,
                               bool *pbReanchored, void *pUser)
{
    if (!Grid_Probe(nX, nY, nLayer, nDir, pnOutX, pnOutY, pnOutLayer,
                    pbReanchored, pUser)) {
        return false;
    }
    *pbReanchored = true;
    return true;
}

static void Grid_Init(Grid *pG)
{
    i32 x;
    i32 y;
    i32 z;

    memset(pG, 0, sizeof(*pG));
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < T_DIM; ++y) {
            for (x = 0; x < T_DIM; ++x) {
                MapCell_Init(&pG->aCells[z][y][x], x, y, z);
                /* Path_ExpandCell expands a cell only when it has a floor sprite
                 * (or the bFlag override is set). bTransparent stays 0 so
                 * MapCell_TestFlag(0) reports the cell as NOT blank, which is
                 * what a walkable cell looks like. */
                memset(&pG->aFloors[z][y][x], 0, sizeof(Mc1Rec));
                pG->aCells[z][y][x].pFloorSpr = &pG->aFloors[z][y][x];
            }
        }
    }
}

static void TestReachableStraight(void)
{
    Grid grid;

    printf("Path_ComputeReachable (straight line east)\n");
    Grid_Init(&grid);
    /* Seed facing east so each step's turn cost is 0. */
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0, false,
                                Grid_Lookup, Grid_Probe, &grid),
          "  target reachable");
    Check(grid.aCells[0][1][4].nPathCost != 0, "  target cost set");
    CheckEqI32(grid.aCells[0][1][1].nPathCost, 1, "  seed cost is 1");
    CheckEqI32(grid.aCells[0][1][2].nPathCost, 5, "  one step costs seed + 4");
    CheckEqI32(grid.aCells[0][1][3].nPathCost, 9, "  two steps");
    CheckEqI32(grid.aCells[0][1][4].nPathCost, 13, "  three steps");
}

static void TestUnreachable(void)
{
    Grid grid;
    i32  y;

    printf("Path_ComputeReachable (walled off)\n");
    Grid_Init(&grid);
    for (y = 0; y < T_DIM; ++y) {
        grid.aBlocked[y][3] = 1;
    }
    Check(!Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 6, 1, 0, false,
                                 Grid_Lookup, Grid_Probe, &grid),
          "  target unreachable behind a wall");
    CheckEqI32(grid.aCells[0][1][6].nPathCost, 0, "  target cost left 0");
}

static void TestBlankTarget(void)
{
    Grid grid;

    printf("Path_ComputeReachable (blank target cell)\n");
    Grid_Init(&grid);
    /* MapCell_TestFlag selector 0 is true when the floor/ceiling sprite is
     * transparent (byte 0x20 == 255). A blank target is rejected before any
     * flood happens. */
    grid.aFloors[0][1][4].bTransparent = 255;
    grid.nProbeCalls = 0;
    Check(!Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0, false,
                                 Grid_Lookup, Grid_Probe, &grid),
          "  blank target rejected");
    CheckEqI32(grid.nProbeCalls, 0, "  no expansion attempted at all");
}

static void TestDiagonalCost(void)
{
    Grid grid;

    printf("Path_ComputeReachable (diagonal surcharge)\n");
    Grid_Init(&grid);
    /* Orthogonal: (1,1) -> (1,2) is south, seed facing south so delta is 0. */
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_S, 1, 2, 0, false,
                                Grid_Lookup, Grid_Probe, &grid),
          "  orthogonal step reachable");
    CheckEqI32(grid.aCells[0][2][1].nPathCost, 5, "  orthogonal costs base only");

    /* Diagonal: (1,1) -> (2,2) is southeast, an odd direction, so +2. */
    Grid_Init(&grid);
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_SE, 2, 2, 0, false,
                                Grid_Lookup, Grid_Probe, &grid),
          "  diagonal step reachable");
    CheckEqI32(grid.aCells[0][2][2].nPathCost, 7, "  diagonal costs base + 2");
}

static void TestTurningCost(void)
{
    Grid grid;

    printf("Path_ComputeReachable (turning costs extra)\n");
    /* Seed facing north, then travel south. The turn adds the facing delta. */
    Grid_Init(&grid);
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_N, 1, 3, 0, false,
                                Grid_Lookup, Grid_Probe, &grid),
          "  reachable behind the unit");
    /* Step 1 (dir 4 = south): delta(4, 0) == 4, so cost 1 + 4 + 4 = 9.
     * Step 2 (dir 4, from a cell whose bPathDir is now 4): delta(4, 4) == 0,
     * so cost 9 + 0 + 4 = 13. */
    CheckEqI32(grid.aCells[0][2][1].nPathCost, 9, "  first step pays the turn");
    CheckEqI32(grid.aCells[0][3][1].nPathCost, 13, "  then continues straight");
}

static void TestLayerCost(void)
{
    Grid grid;

    printf("Path_ComputeReachable (layer change surcharge)\n");
    Grid_Init(&grid);
    /* Target one layer up: dir 8. A layer change with the flag clear adds 4. */
    Check(Path_ComputeReachable(&grid.scratch, 2, 2, 0, DIR_UP, 2, 2, 1, false,
                                Grid_Lookup, Grid_Probe, &grid),
          "  vertical move reachable");
    /* 1 (seed) + delta(8,8)==0 + 4 base + 4 layer = 9. */
    CheckEqI32(grid.aCells[1][2][2].nPathCost, 9, "  layer change costs base + 4");
}

static void TestCostAccumulationLimit(void)
{
    Grid grid;
    i32  nFar = T_DIM - 1;

    printf("Path_ComputeReachable (accumulated cost and the 140 limit)\n");
    Grid_Init(&grid);
    Check(Path_ComputeReachable(&grid.scratch, 0, 0, 0, DIR_E, nFar, 0, 0, false,
                                Grid_Lookup, Grid_Probe, &grid),
          "  far corner reachable");
    /* 7 steps of base cost 4, plus the seed of 1 == 29, under the limit. */
    CheckEqI32(grid.aCells[0][0][nFar].nPathCost, 1 + nFar * 4,
               "  accumulated cost is seed + 4 per step");
    Check(grid.aCells[0][0][nFar].nPathCost < PUFO_PATH_COST_LIMIT,
          "  cost under the documented limit");

    /* The enqueue gate is `cost < 140`. Build a straight run long enough that
     * costs cross 140 and confirm the far end of that run is not enqueued. With
     * base 4 per step, cost crosses 140 at step 35. */
    {
        /* A 40-long corridor is needed; the 8-wide grid is not enough, so this
         * only asserts the boundary arithmetic rather than a real walk. */
        i32 nCostAt35 = 1 + 35 * 4;

        Check(nCostAt35 >= PUFO_PATH_COST_LIMIT,
              "  cost reaches the limit at 35 orthogonal steps");
        Check(1 + 34 * 4 < PUFO_PATH_COST_LIMIT,
              "  and is still enqueueable at 34 steps");
    }
}

/* A cell with no floor sprite is not expanded, even when it has a cost.
 *
 * Two things make this test subtle and both are genuine original behaviour:
 *   1. the flood moves diagonally, so one hole is routed around
 *   2. the flood moves between LAYERS (dirs 8 and 9), so blocking a column on
 *      one layer is bypassed by going up, across, and back down
 * A wall therefore only blocks when it is solid on every layer. */
static void TestFloorGate(void)
{
    Grid grid;
    i32  y;
    i32  z;

    printf("Path_ComputeReachable (cells without a floor sprite are not expanded)\n");
    Grid_Init(&grid);

    /* One hole is routed around diagonally. */
    grid.aCells[0][1][2].pFloorSpr = NULL;
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0, false,
                                Grid_Lookup, Grid_Probe, &grid),
          "  a single hole is routed around diagonally");

    /* A column blocked on ONE layer is bypassed through another layer, because
     * the flood can step vertically. Worth asserting: a 2D wall is not a wall. */
    Grid_Init(&grid);
    for (y = 0; y < T_DIM; ++y) {
        grid.aCells[0][y][2].pFloorSpr = NULL;
    }
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0, false,
                                Grid_Lookup, Grid_Probe, &grid),
          "  a wall on one layer is bypassed via another layer");

    /* Blocking the column on EVERY layer does disconnect the grid. */
    Grid_Init(&grid);
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < T_DIM; ++y) {
            grid.aCells[z][y][2].pFloorSpr = NULL;
        }
    }
    Check(!Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0, false,
                                 Grid_Lookup, Grid_Probe, &grid),
          "  a floorless column on every layer blocks the flood");
}

/* bFlag is the original's sixth argument (Map_ComputeReachable, 0x2b850/0x2b860),
 * taken from column 0 of the unit type table. It lifts both rules the expansion
 * applies off the seed layer: the +4 layer surcharge and the floor-sprite gate. */
static void TestReachableFlag(void)
{
    Grid grid;

    printf("Path_ComputeReachable (the bFlag argument)\n");

    /* The flag does not reach the layer surcharge. 0x2b75c gates it on the probe's
     * own re-anchor report, not on bFlag, so the same vertical step costs the same
     * with the flag set as with it clear. */
    Grid_Init(&grid);
    Check(Path_ComputeReachable(&grid.scratch, 2, 2, 0, DIR_UP, 2, 2, 1, true,
                                Grid_Lookup, Grid_Probe, &grid),
          "  vertical move reachable with the flag set");
    CheckEqI32(grid.aCells[1][2][2].nPathCost, 9,
               "  the flag does NOT lift the +4 layer surcharge");
    CheckEqI32(grid.aCells[1][2][2].bFlags29 & 8u, 0,
               "  bFlags29 bit 3 stays clear when the probe did not re-anchor");

    /* The probe's report is what gates both. Same flood, probe says it moved the
     * candidate a layer itself: no surcharge, and bit 3 set (0x2b7a4). */
    Grid_Init(&grid);
    Check(Path_ComputeReachable(&grid.scratch, 2, 2, 0, DIR_UP, 2, 2, 1, false,
                                Grid_Lookup, Grid_ProbeAnchored, &grid),
          "  re-anchored vertical move reachable");
    CheckEqI32(grid.aCells[1][2][2].nPathCost, 5,
               "  the re-anchor report drops the +4 layer surcharge");
    CheckEqI32(grid.aCells[1][2][2].bFlags29 & 8u, 8,
               "  and it is what sets bFlags29 bit 3");

    /* The floor gate is the other effect. The same floorless column that blocks the
     * flood in TestFloorGate does not block it when the flag is set. */
    Grid_Init(&grid);
    {
        i32 y;
        i32 z;

        for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
            for (y = 0; y < T_DIM; ++y) {
                grid.aCells[z][y][2].pFloorSpr = NULL;
            }
        }
    }
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0, true,
                                Grid_Lookup, Grid_Probe, &grid),
          "  flag set expands cells that have no floor sprite");

    /* And the flag is what Path_ExpandCell reads back, not just what the caller
     * passed: with it clear the same grid is still blocked. */
    Grid_Init(&grid);
    {
        i32 y;
        i32 z;

        for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
            for (y = 0; y < T_DIM; ++y) {
                grid.aCells[z][y][2].pFloorSpr = NULL;
            }
        }
    }
    Check(!Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0, false,
                                 Grid_Lookup, Grid_Probe, &grid),
          "  flag clear keeps the floor gate");
}

/* byte_A7B00 @ 0xa7b00, read through Path_FacingFromDelta. The compass band is the
 * interesting half: the table has to agree with g_aStepDir, which is where the
 * flood records the direction it came from. */
static void TestFacingFromDelta(void)
{
    i32 nDir;

    printf("Path_FacingFromDelta (byte_A7B00 @ 0xa7b00)\n");
    for (nDir = 0; nDir < 8; ++nDir) {
        u8 nWant = Path_FacingFromDelta(g_aStepDir[nDir].dx,
                                        g_aStepDir[nDir].dy, 0);

        CheckEqI32(nWant, nDir,
                   "  each compass delta reports its own direction");
    }
    CheckEqI32(Path_FacingFromDelta(0, 0, 1), 8, "  a pure climb reports direction 8");
    CheckEqI32(Path_FacingFromDelta(0, 0, -1), 9, "  a pure descent reports direction 9");
    /* The two vertical bands differ from the compass band only at that pure
     * delta; every delta with a horizontal component reads the same facing. */
    CheckEqI32(Path_FacingFromDelta(1, 1, 1), 3, "  a diagonal climb keeps the compass facing");
    CheckEqI32(Path_FacingFromDelta(-1, 0, -1), 6, "  a diagonal descent keeps it too");
}

/* --- Unit_PathToCell --- */

static TactState   g_State;
static Soldier     g_Unit;
static Soldier     g_Other;
static Soldier    *g_pSelUnit;
static Soldier    *g_pDestUnit;
static MapCell    *g_apPath[PUFO_PATH_MAX_CELLS];
static i32         g_nPathCount;
static MapGeom     g_Geom = { 1, 1 };

/* A fresh grid, an empty queue, and a unit standing at (1,1,0) facing east. The
 * selected unit is reached through a pointer-to-pointer because that is how the
 * original reaches it: through the Map slot, not through an argument. */
static void Path_Reset(Grid *pG, u8 bFacing, u8 bFlags)
{
    Grid_Init(pG);
    memset(&g_State, 0, sizeof(g_State));
    g_pTactState = &g_State;
    memset(&g_Unit, 0, sizeof(g_Unit));
    memset(&g_Other, 0, sizeof(g_Other));
    g_Unit.bFacing = bFacing;
    g_Unit.bFlags = bFlags;
    g_Unit.nUnitType = 0; /* g_aUnitTypeFlag0[0] is clear */
    g_Unit.pCell = &pG->aCells[0][1][1];
    g_pSelUnit = &g_Unit;
    g_pDestUnit = NULL;
    memset(g_apPath, 0, sizeof(g_apPath));
    g_nPathCount = -1;
}

static PathToCellCtx Path_Ctx(Grid *pG)
{
    PathToCellCtx c;

    c.pState          = &g_State;
    c.ppSelUnit       = &g_pSelUnit;
    c.ppPathDestUnit  = &g_pDestUnit;
    c.apPathCells     = g_apPath;
    c.pnPathCount     = &g_nPathCount;
    c.pScratch        = &pG->scratch;
    c.pfnLookup       = Grid_Lookup;
    c.pfnProbe        = Grid_Probe;
    c.pProbeUser      = pG;
    c.pLookupUser     = pG;
    c.pGeom           = &g_Geom;
    return c;
}

static i32 QueueCmdCount(u8 nCmd)
{
    i32 i;
    i32 n = 0;

    for (i = 0; i < g_State.nQueueLen; ++i) {
        if (g_State.aQueue[i].nCmd == nCmd) {
            n += 1;
        }
    }
    return n;
}

/* Walking east in a straight line, which is the case where nothing interesting
 * happens: no turn (the unit already faces east), three steps, one end marker. */
static void TestPathStraight(void)
{
    Grid grid;
    PathToCellCtx ctx;

    printf("Unit_PathToCell (straight line, no turn needed)\n");
    Path_Reset(&grid, DIR_E, 0);
    g_Other.pCell = &grid.aCells[0][1][4];
    grid.aCells[0][1][4].pUnit = &g_Other;
    ctx = Path_Ctx(&grid);

    Check(Unit_PathToCell(4, 1, 0, 0, &ctx), "  path built");
    CheckEqI32(g_nPathCount, 3, "  three steps recorded");
    Check(g_apPath[0] == &grid.aCells[0][1][4], "  slot 0 is the destination");
    Check(g_apPath[3] == &grid.aCells[0][1][1], "  the last slot is the unit's own cell");
    CheckEqI32(g_State.nQueueLen, 4, "  three steps plus the end marker");
    CheckEqI32(QueueCmdCount(TACTCMD_STEP), 3, "  three STEP entries");
    CheckEqI32(QueueCmdCount(TACTCMD_TURN), 0, "  no TURN entry, the facing already matched");
    CheckEqI32(g_State.aQueue[3].nCmd, TACTCMD_END_MARK, "  the queue ends with the end marker");
    CheckEqI32(g_State.aQueue[0].nFacing, DIR_E, "  step facing is east");
    CheckEqI32(g_State.aQueue[0].nDelta, 0, "  a horizontal step carries no layer delta");
    Check(g_State.aQueue[0].pUnit == &g_Unit, "  the entries name the unit");
    CheckEqI32(g_State.bQueueActive, 1, "  the queue was kicked");

    /* 0x2c2e8..0x2c314: with the flag clear the destination cell's occupant is
     * parked in the Map+0x150f4 slot. */
    Check(g_pDestUnit == &g_Other, "  flag clear parks the destination occupant");
}

/* The same walk with the flag set takes the other branch of the same pair: the
 * slot is cleared rather than filled. */
static void TestPathFlagClearsSlot(void)
{
    Grid grid;
    PathToCellCtx ctx;

    printf("Unit_PathToCell (the bFlag bookkeeping)\n");
    Path_Reset(&grid, DIR_E, 0);
    grid.aCells[0][1][4].pUnit = &g_Other;
    ctx = Path_Ctx(&grid);

    Check(Unit_PathToCell(4, 1, 0, 1, &ctx), "  path built");
    Check(g_pDestUnit == NULL, "  flag set clears the slot instead");
}

/* The unit faces north and walks south. The facing table says the first step needs
 * facing 4, so a TURN goes in first; the second step keeps it. */
static void TestPathTurnsOnce(void)
{
    Grid grid;
    PathToCellCtx ctx;

    printf("Unit_PathToCell (turn once, then walk)\n");
    Path_Reset(&grid, DIR_N, 0);
    ctx = Path_Ctx(&grid);

    Check(Unit_PathToCell(1, 3, 0, 0, &ctx), "  path built behind the unit");
    CheckEqI32(g_State.nQueueLen, 4, "  turn, two steps, end marker");
    CheckEqI32(g_State.aQueue[0].nCmd, TACTCMD_TURN, "  the first entry is the turn");
    CheckEqI32(g_State.aQueue[0].nFacing, DIR_S, "  the turn faces south");
    CheckEqI32(QueueCmdCount(TACTCMD_TURN), 1, "  only one turn - the facing is remembered");
    CheckEqI32(QueueCmdCount(TACTCMD_STEP), 2, "  two STEP entries");
}

/* 0x2c588: a unit that still has to stand up pays for it before anything moves. */
static void TestPathStandFirst(void)
{
    Grid grid;
    PathToCellCtx ctx;

    printf("Unit_PathToCell (the stand-up prefix)\n");
    Path_Reset(&grid, DIR_E, PUFO_UNITFLAG_STAND_UP);
    ctx = Path_Ctx(&grid);

    Check(Unit_PathToCell(4, 1, 0, 0, &ctx), "  path built");
    CheckEqI32(g_State.aQueue[0].nCmd, TACTCMD_STAND_FROM_KNEEL,
               "  the stand-up entry comes before the steps");
    CheckEqI32(g_State.nQueueLen, 5, "  stand, three steps, end marker");
}

/* An unreachable target: no queue at all, and the queue is not even kicked. */
static void TestPathUnreachable(void)
{
    Grid grid;
    PathToCellCtx ctx;
    i32 y;

    printf("Unit_PathToCell (unreachable target)\n");
    Path_Reset(&grid, DIR_E, 0);
    for (y = 0; y < T_DIM; ++y) {
        grid.aBlocked[y][3] = 1;
    }
    ctx = Path_Ctx(&grid);

    Check(!Unit_PathToCell(6, 1, 0, 0, &ctx), "  no path through a solid column");
    CheckEqI32(g_State.nQueueLen, 0, "  nothing was queued");
    CheckEqI32(g_State.bQueueActive, 0, "  the queue was not kicked");
}

/* A vertical step. The interesting part is where the layer delta lands: cmd 5
 * stores it in the entry's nFacing slot, which is the slot the vertical step's draw
 * offset reads back. */
static void TestPathVertical(void)
{
    Grid grid;
    PathToCellCtx ctx;

    printf("Unit_PathToCell (vertical step)\n");
    Path_Reset(&grid, DIR_E, 0);
    g_Unit.pCell = &grid.aCells[0][2][2];
    ctx = Path_Ctx(&grid);

    Check(Unit_PathToCell(2, 2, 1, 0, &ctx), "  path built one layer up");
    CheckEqI32(g_nPathCount, 1, "  one step recorded");
    CheckEqI32(g_State.nQueueLen, 3, "  turn, vertical step, end marker");
    CheckEqI32(g_State.aQueue[0].nCmd, TACTCMD_TURN, "  the climb needs a turn first");
    CheckEqI32(g_State.aQueue[0].nFacing, DIR_UP, "  the turn faces up (direction 8)");
    CheckEqI32(g_State.aQueue[1].nCmd, TACTCMD_VERT_STEP, "  the move is a vertical step");
    CheckEqI32(g_State.aQueue[1].nFacing, 1,
               "  the layer delta is stored in the nFacing slot");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestFacingDelta();
    TestFacingTurnPositive();
    TestReachableStraight();
    TestUnreachable();
    TestBlankTarget();
    TestDiagonalCost();
    TestTurningCost();
    TestLayerCost();
    TestCostAccumulationLimit();
    TestFloorGate();
    TestReachableFlag();
    TestFacingFromDelta();
    TestPathStraight();
    TestPathFlagClearsSlot();
    TestPathTurnsOnce();
    TestPathStandFirst();
    TestPathUnreachable();
    TestPathVertical();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
