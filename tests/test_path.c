/* test_path.c - checks the tactical reachability search.
 *
 * Map_FacingDelta is a simple circular distance; the flood fill is the part that
 * matters. The cost composition (base 4, diagonal +2, layer +4, plus the facing
 * delta) and the "cost 0 means unvisited" rule are asserted because both are easy
 * to get subtly wrong.
 *
 * Note on the facings used below: the seed's facing becomes the current cell's
 * bUnk24, and every step pays Map_FacingDelta(stepDir, curCell->bUnk24). Tests
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
                       i32 *pnOutX, i32 *pnOutY, i32 *pnOutLayer, void *pUser)
{
    Grid *pG = (Grid *)pUser;

    if (nDir >= PUFO_PATH_DIR_COUNT) {
        return false;
    }
    pG->nProbeCalls += 1;

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
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0,
                                Grid_Lookup, Grid_Probe, &grid),
          "  target reachable");
    Check(grid.aCells[0][1][4].nUnk20 != 0, "  target cost set");
    CheckEqI32(grid.aCells[0][1][1].nUnk20, 1, "  seed cost is 1");
    CheckEqI32(grid.aCells[0][1][2].nUnk20, 5, "  one step costs seed + 4");
    CheckEqI32(grid.aCells[0][1][3].nUnk20, 9, "  two steps");
    CheckEqI32(grid.aCells[0][1][4].nUnk20, 13, "  three steps");
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
    Check(!Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 6, 1, 0,
                                 Grid_Lookup, Grid_Probe, &grid),
          "  target unreachable behind a wall");
    CheckEqI32(grid.aCells[0][1][6].nUnk20, 0, "  target cost left 0");
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
    Check(!Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0,
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
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_S, 1, 2, 0,
                                Grid_Lookup, Grid_Probe, &grid),
          "  orthogonal step reachable");
    CheckEqI32(grid.aCells[0][2][1].nUnk20, 5, "  orthogonal costs base only");

    /* Diagonal: (1,1) -> (2,2) is southeast, an odd direction, so +2. */
    Grid_Init(&grid);
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_SE, 2, 2, 0,
                                Grid_Lookup, Grid_Probe, &grid),
          "  diagonal step reachable");
    CheckEqI32(grid.aCells[0][2][2].nUnk20, 7, "  diagonal costs base + 2");
}

static void TestTurningCost(void)
{
    Grid grid;

    printf("Path_ComputeReachable (turning costs extra)\n");
    /* Seed facing north, then travel south. The turn adds the facing delta. */
    Grid_Init(&grid);
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_N, 1, 3, 0,
                                Grid_Lookup, Grid_Probe, &grid),
          "  reachable behind the unit");
    /* Step 1 (dir 4 = south): delta(4, 0) == 4, so cost 1 + 4 + 4 = 9.
     * Step 2 (dir 4, from a cell whose bUnk24 is now 4): delta(4, 4) == 0,
     * so cost 9 + 0 + 4 = 13. */
    CheckEqI32(grid.aCells[0][2][1].nUnk20, 9, "  first step pays the turn");
    CheckEqI32(grid.aCells[0][3][1].nUnk20, 13, "  then continues straight");
}

static void TestLayerCost(void)
{
    Grid grid;

    printf("Path_ComputeReachable (layer change surcharge)\n");
    Grid_Init(&grid);
    /* Target one layer up: dir 8. A layer change with the flag clear adds 4. */
    Check(Path_ComputeReachable(&grid.scratch, 2, 2, 0, DIR_UP, 2, 2, 1,
                                Grid_Lookup, Grid_Probe, &grid),
          "  vertical move reachable");
    /* 1 (seed) + delta(8,8)==0 + 4 base + 4 layer = 9. */
    CheckEqI32(grid.aCells[1][2][2].nUnk20, 9, "  layer change costs base + 4");
}

static void TestCostAccumulationLimit(void)
{
    Grid grid;
    i32  nFar = T_DIM - 1;

    printf("Path_ComputeReachable (accumulated cost and the 140 limit)\n");
    Grid_Init(&grid);
    Check(Path_ComputeReachable(&grid.scratch, 0, 0, 0, DIR_E, nFar, 0, 0,
                                Grid_Lookup, Grid_Probe, &grid),
          "  far corner reachable");
    /* 7 steps of base cost 4, plus the seed of 1 == 29, under the limit. */
    CheckEqI32(grid.aCells[0][0][nFar].nUnk20, 1 + nFar * 4,
               "  accumulated cost is seed + 4 per step");
    Check(grid.aCells[0][0][nFar].nUnk20 < PUFO_PATH_COST_LIMIT,
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
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0,
                                Grid_Lookup, Grid_Probe, &grid),
          "  a single hole is routed around diagonally");

    /* A column blocked on ONE layer is bypassed through another layer, because
     * the flood can step vertically. Worth asserting: a 2D wall is not a wall. */
    Grid_Init(&grid);
    for (y = 0; y < T_DIM; ++y) {
        grid.aCells[0][y][2].pFloorSpr = NULL;
    }
    Check(Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0,
                                Grid_Lookup, Grid_Probe, &grid),
          "  a wall on one layer is bypassed via another layer");

    /* Blocking the column on EVERY layer does disconnect the grid. */
    Grid_Init(&grid);
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < T_DIM; ++y) {
            grid.aCells[z][y][2].pFloorSpr = NULL;
        }
    }
    Check(!Path_ComputeReachable(&grid.scratch, 1, 1, 0, DIR_E, 4, 1, 0,
                                 Grid_Lookup, Grid_Probe, &grid),
          "  a floorless column on every layer blocks the flood");
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

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
