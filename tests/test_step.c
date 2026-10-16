/* test_step.c - checks the step probe's vertical re-anchor.
 *
 * Map_ProbeStepOccupants is the part of the step test that is easy to lose: a step
 * is not always the cell the direction delta points at, and when the probe moves the
 * candidate it moves the cell and the layer bookkeeping separately - the down anchor
 * changes the cell and g_nStepAdjLayer but leaves g_nStepDstLayer alone, the up anchor
 * changes all three. Those asymmetries are what this file asserts, together with the
 * two ceiling thresholds (-15 to open the anchor, -8 to reject the step) and the
 * second column a large unit drags along.
 */
#include "game/pufo_path.h"  /* g_aStepDir, the direction deltas the probe is seeded with */
#include "game/pufo_step.h"

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

#define S_DIM 8

typedef struct World {
    MapCell aCells[PUFO_MAP_LAYERS][S_DIM][S_DIM];
    Mc1Rec  aFloor[S_DIM][S_DIM];      /* layer 0 floors, reused per layer */
    Mc1Rec  aCeil[PUFO_MAP_LAYERS][S_DIM][S_DIM];
    Soldier aUnit[2];
    StepProbe probe;
} World;

static MapCell *World_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    World *pW = (World *)pUser;

    if (nX < 0 || nY < 0 || nX >= S_DIM || nY >= S_DIM) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return &pW->aCells[nLayer][nY][nX];
}

/* Every cell gets a floor and its own coordinates; ceilings are attached only where
 * a test asks for one. */
static void World_Init(World *pW)
{
    i32 x;
    i32 y;
    i32 z;

    memset(pW, 0, sizeof(*pW));
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < S_DIM; ++y) {
            for (x = 0; x < S_DIM; ++x) {
                MapCell_Init(&pW->aCells[z][y][x], x, y, z);
                memset(&pW->aFloor[y][x], 0, sizeof(Mc1Rec));
                pW->aCells[z][y][x].pFloorSpr = &pW->aFloor[y][x];
            }
        }
    }
    memset(pW->aUnit, 0, sizeof(pW->aUnit));
    pW->aUnit[0].nUnitType = 0;  /* not large */
    pW->aUnit[1].nUnitType = 4;  /* large */
}

/* A candidate step from (2,2,1) in direction nDir, which is the shape both anchors
 * need: the current cell, the cell above it, and the cell above the candidate. */
static void World_Setup(World *pW, StepCtx *pCtx, i32 nUnit, bool bSearchFlag, u8 nDir)
{
    const StepDir *pDir = &g_aStepDir[nDir];

    World_Init(pW);
    memset(pCtx, 0, sizeof(*pCtx));
    pCtx->pfnLookup    = World_Lookup;
    pCtx->pLookupUser  = pW;
    pCtx->pSelUnit     = &pW->aUnit[nUnit];
    pCtx->pSwapUnit    = NULL;
    pCtx->bSearchFlag  = bSearchFlag;
    pCtx->pProbe       = &pW->probe;

    pW->probe.nCurX      = 2;
    pW->probe.nCurY      = 2;
    pW->probe.nCurLayer  = 1;
    pW->probe.nDir       = nDir;
    pW->probe.pCurCell   = &pW->aCells[1][2][2];
    pW->probe.pCellAbove = &pW->aCells[2][2][2];
    pW->probe.nDstX      = 2 + pDir->dx;
    pW->probe.nDstY      = 2 + pDir->dy;
    pW->probe.nDstLayer  = 1 + pDir->dz;
    pW->probe.pDstCell   = &pW->aCells[pW->probe.nDstLayer][pW->probe.nDstY][pW->probe.nDstX];
}

static Mc1Rec *World_Ceil(World *pW, i32 nLayer, i32 nX, i32 nY, i8 nParam)
{
    pW->aCeil[nLayer][nY][nX].iCeilParam = nParam;
    pW->aCells[nLayer][nY][nX].pCeilSpr  = &pW->aCeil[nLayer][nY][nX];
    return &pW->aCeil[nLayer][nY][nX];
}

static void World_NoFloor(World *pW, i32 nLayer, i32 nX, i32 nY)
{
    pW->aCells[nLayer][nY][nX].pFloorSpr = NULL;
}

/* Nothing wants moving: the candidate has a floor and the current cell has no
 * ceiling. The probe reports the candidate unchanged and leaves the layer
 * bookkeeping alone. */
static void TestNoAnchor(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (no anchor needed)\n");
    World_Setup(pW, &ctx, 0, false, 2);

    Check(Map_ProbeStepOccupants(&ctx), "  step accepted");
    CheckEqI32(pW->probe.bReanchored, 0, "  re-anchor flag stays clear");
    Check(pW->probe.pDstCell == &pW->aCells[1][2][3], "  candidate cell unchanged");
    CheckEqI32(pW->probe.nDstLayer, 1, "  candidate layer unchanged");
    CheckEqI32(pW->probe.nAdjLayer, 1, "  adjusted layer seeded from the candidate");
}

/* The candidate has no floor, the current cell does, and the candidate is above
 * layer 0: the candidate becomes the cell below it. Note what does NOT move - the
 * candidate layer stays 1 while the cell and the adjusted layer go to 0. */
static void TestAnchorDown(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (anchor down)\n");
    World_Setup(pW, &ctx, 0, false, 2);
    World_NoFloor(pW, 1, 3, 2);

    Check(Map_ProbeStepOccupants(&ctx), "  step accepted after the anchor");
    CheckEqI32(pW->probe.bReanchored, 1, "  re-anchor flag set");
    Check(pW->probe.pDstCell == &pW->aCells[0][2][3], "  candidate is now the cell below");
    CheckEqI32(pW->probe.nAdjLayer, 0, "  adjusted layer dropped by one");
    CheckEqI32(pW->probe.nDstLayer, 1, "  candidate layer was NOT changed");
}

/* The same shape, but the cell below has no floor either: the anchor has nowhere to
 * go and the step is refused outright, not merely left unanchored. */
static void TestAnchorDownRefused(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (anchor down, nowhere to go)\n");
    World_Setup(pW, &ctx, 0, false, 2);
    World_NoFloor(pW, 1, 3, 2);
    World_NoFloor(pW, 0, 3, 2);

    Check(!Map_ProbeStepOccupants(&ctx), "  step refused");
}

/* The search flag lifts the down anchor entirely, so the floorless candidate is
 * accepted where it would otherwise have been moved or refused. */
static void TestAnchorDownNeedsFlagClear(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (the search flag lifts the down anchor)\n");
    World_Setup(pW, &ctx, 0, true, 2);
    World_NoFloor(pW, 1, 3, 2);

    Check(Map_ProbeStepOccupants(&ctx), "  floorless candidate accepted with the flag set");
    CheckEqI32(pW->probe.bReanchored, 0, "  no anchor attempted");
    Check(pW->probe.pDstCell == &pW->aCells[1][2][3], "  candidate cell unchanged");
}

/* The current cell has a ceiling that breaks below -15, the cell above the current
 * one has no floor, and the cell above the candidate has one: the candidate moves up,
 * and here both layer fields follow it. */
static void TestAnchorUp(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (anchor up)\n");
    World_Setup(pW, &ctx, 0, false, 2);
    World_Ceil(pW, 1, 2, 2, -20);
    World_NoFloor(pW, 2, 2, 2);

    Check(Map_ProbeStepOccupants(&ctx), "  step accepted after the anchor");
    CheckEqI32(pW->probe.bReanchored, 1, "  re-anchor flag set");
    Check(pW->probe.pDstCell == &pW->aCells[2][2][3], "  candidate is now the cell above");
    CheckEqI32(pW->probe.nDstLayer, 2, "  candidate layer rose by one");
    CheckEqI32(pW->probe.nAdjLayer, 2, "  adjusted layer rose by one");
}

/* A large unit has to find the same breakable ceiling over the SECOND column of its
 * footprint (0x27ac4..0x27ad8), the cell above that column has to be floorless
 * (0x27adc..0x27ae8) and the cell above the candidate's second column has to have a
 * floor (0x27aec..0x27af8). Direction 3 (north-east) is the smallest shape that keeps
 * those three cells distinct. */
static void TestAnchorUpNeedsSecondColumn(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (a large unit needs the second column too)\n");

    /* Without the ceiling over the footprint's second column the anchor is refused,
     * and the -8 rejection test that follows then rejects the step outright. */
    World_Setup(pW, &ctx, 1, false, 3);
    World_Ceil(pW, 1, 2, 2, -20);
    World_NoFloor(pW, 2, 2, 2);
    Check(!Map_ProbeStepOccupants(&ctx),
          "  refused: no ceiling over the footprint's second column");

    /* Give the second column its ceiling, its cell above no floor, and keep a floor
     * above the candidate's second column: the candidate moves up. */
    World_Setup(pW, &ctx, 1, false, 3);
    World_Ceil(pW, 1, 2, 2, -20);
    World_Ceil(pW, 1, 3, 2, -20);   /* cell (3,2,1), the current cell's second column */
    World_NoFloor(pW, 2, 2, 2);     /* cell (2,2,2), above the current cell */
    World_NoFloor(pW, 2, 3, 2);     /* cell (3,2,2), above that second column */
    Check(Map_ProbeStepOccupants(&ctx), "  accepted once both columns break");
    Check(pW->probe.pDstCell == &pW->aCells[2][3][3], "  candidate moved up");
    CheckEqI32(pW->probe.nDstLayer, 2, "  candidate layer rose by one");
    CheckEqI32(pW->probe.nAdjLayer, 2, "  adjusted layer rose by one");
}

/* Stepping due east, a large unit's footprint second column IS the candidate's column,
 * so "the cell above the current cell's second column" (0x27adc, tested for NO floor at
 * 0x27ae4) and "the cell above the candidate" (tested for a FLOOR at 0x27a94) are the
 * same cell. The two tests contradict, so the up anchor can never fire in this
 * direction - where a small unit with the identical geometry does re-anchor up. */
static void TestAnchorUpDeadForLargeEast(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (the up anchor cannot fire for a large unit going east)\n");

    /* Floor above the candidate's column: 0x27ae4 rejects the anchor and the -8
     * rejection test then rejects the step. A small unit here re-anchors up instead. */
    World_Setup(pW, &ctx, 1, false, 2);
    World_Ceil(pW, 1, 2, 2, -20);
    World_Ceil(pW, 1, 3, 2, -20);
    World_NoFloor(pW, 2, 2, 2);
    Check(!Map_ProbeStepOccupants(&ctx), "  refused: the two floor tests contradict");
    CheckEqI32(pW->probe.bReanchored, 0, "  no re-anchor attempted");

    World_Setup(pW, &ctx, 0, false, 2);
    World_Ceil(pW, 1, 2, 2, -20);
    World_NoFloor(pW, 2, 2, 2);
    Check(Map_ProbeStepOccupants(&ctx), "  the same step for a small unit is accepted");
    CheckEqI32(pW->probe.bReanchored, 1, "  and it does re-anchor up");

    /* No floor there: 0x27a94 jumps straight to the rejection test, which passes
     * because the current cell has a ceiling. Still no anchor. */
    World_Setup(pW, &ctx, 1, false, 2);
    World_Ceil(pW, 1, 2, 2, -20);
    World_Ceil(pW, 1, 3, 2, -20);
    World_NoFloor(pW, 2, 2, 2);
    World_NoFloor(pW, 2, 3, 2);
    Check(Map_ProbeStepOccupants(&ctx), "  accepted when the cell above has no floor");
    CheckEqI32(pW->probe.bReanchored, 0, "  still no re-anchor");
    Check(pW->probe.pDstCell == &pW->aCells[1][2][3], "  candidate stayed put");
    CheckEqI32(pW->probe.nDstLayer, 1, "  candidate layer unchanged");
}

/* -15 opens the anchor; -8 is a different gate. A ceiling at -10 opens neither, and
 * below layer 3 a cell above the candidate that HAS a floor rejects the step. */
static void TestCeilingReject(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (the -8 rejection)\n");
    World_Setup(pW, &ctx, 0, false, 2);
    World_Ceil(pW, 1, 2, 2, -10);   /* breaks below -8 but not below -15 */
    /* pCellAbove keeps its floor, so the up anchor's second condition fails too. */
    Check(!Map_ProbeStepOccupants(&ctx), "  refused: ceiling below -8 with a floor above the candidate");

    /* Remove that floor and the same ceiling no longer rejects. */
    World_Setup(pW, &ctx, 0, false, 2);
    World_Ceil(pW, 1, 2, 2, -10);
    World_NoFloor(pW, 2, 3, 2);
    Check(Map_ProbeStepOccupants(&ctx), "  accepted: no floor above the candidate");
}

/* The rejection test is skipped at layer 3 and above. Same geometry as the refused
 * case, one layer up, and the step goes through. */
static void TestRejectSkippedAtLayer3(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (the rejection is skipped at layer 3)\n");
    World_Setup(pW, &ctx, 0, false, 2);
    pW->probe.nCurLayer = 3;
    pW->probe.pCurCell  = &pW->aCells[3][2][2];
    pW->probe.nDstLayer = 3;
    pW->probe.pDstCell  = &pW->aCells[3][2][3];
    pW->probe.pCellAbove = NULL;      /* the original leaves the global stale here */
    World_Ceil(pW, 3, 2, 2, -10);
    Check(Map_ProbeStepOccupants(&ctx), "  accepted at layer 3");
}

/* With no ceiling over the current cell, the candidate's own ceiling decides: below
 * -8 rejects, anything else passes. */
static void TestCandidateCeiling(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (the candidate's own ceiling)\n");
    World_Setup(pW, &ctx, 0, false, 2);
    World_Ceil(pW, 1, 3, 2, -20);
    Check(!Map_ProbeStepOccupants(&ctx), "  candidate ceiling below -8 refuses");

    World_Setup(pW, &ctx, 0, false, 2);
    World_Ceil(pW, 1, 3, 2, -5);
    Check(Map_ProbeStepOccupants(&ctx), "  candidate ceiling at -5 passes");

    World_Setup(pW, &ctx, 0, false, 2);
    World_Ceil(pW, 1, 3, 2, -8);
    Check(Map_ProbeStepOccupants(&ctx), "  -8 exactly is the boundary and passes");
}

int main(void)
{
    static World world;

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestNoAnchor(&world);
    TestAnchorDown(&world);
    TestAnchorDownRefused(&world);
    TestAnchorDownNeedsFlagClear(&world);
    TestAnchorUp(&world);
    TestAnchorUpNeedsSecondColumn(&world);
    TestAnchorUpDeadForLargeEast(&world);
    TestCeilingReject(&world);
    TestRejectSkippedAtLayer3(&world);
    TestCandidateCeiling(&world);

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
