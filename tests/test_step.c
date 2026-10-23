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

/* Ten cells across and down, matching g_Geom below: Map_IsValidCell validates against
 * the same bounds, and Map_ProbeStepOccupants dereferences the candidate the way the
 * original does, so the fixture's lookup has to cover every coordinate the geometry
 * calls valid. */
#define S_DIM 10

typedef struct World {
    MapCell aCells[PUFO_MAP_LAYERS][S_DIM][S_DIM];
    Mc1Rec  aFloor[PUFO_MAP_LAYERS][S_DIM][S_DIM];
    Mc1Rec  aWallA[PUFO_MAP_LAYERS][S_DIM][S_DIM];
    Mc1Rec  aWallB[PUFO_MAP_LAYERS][S_DIM][S_DIM];
    Mc1Rec  aCeil[PUFO_MAP_LAYERS][S_DIM][S_DIM];
    Soldier aUnit[2];
    StepProbe probe;
} World;

static Soldier  g_Sel;    /* the mover: ctx pSelUnit, Map+0x15334 */
static Soldier  g_Swap;   /* the swap partner: Map_pPathDestUnit @ Map+0x150f4 */
static Soldier  g_Foreign;
static MapGeom  g_Geom = { 1, 1 }; /* ten cells across and down */

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
                pW->aCells[z][y][x].pFloorSpr = &pW->aFloor[z][y][x];
                pW->aCells[z][y][x].pWallSprA = &pW->aWallA[z][y][x];
                pW->aCells[z][y][x].pWallSprB = &pW->aWallB[z][y][x];
                pW->aCells[z][y][x].pCeilSpr  = NULL;
            }
        }
    }
    memset(pW->aUnit, 0, sizeof(pW->aUnit));
    pW->aUnit[0].nUnitType = 0;  /* not large */
    pW->aUnit[1].nUnitType = 4;  /* large */

    memset(&g_Sel, 0, sizeof(g_Sel));
    memset(&g_Swap, 0, sizeof(g_Swap));
    memset(&g_Foreign, 0, sizeof(g_Foreign));
    g_Sel.bStatus     = 0;   /* operative */
    g_Swap.bStatus    = 0;
    g_Foreign.bStatus = 0;   /* an operative stranger: the head gate refuses it */
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
    pCtx->pSwapUnit    = &g_Swap;
    pCtx->pGeom        = &g_Geom;
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

/* A floor sprite marked transparent makes the cell blank (MapCell_TestFlag 0). */
static void World_Blank(World *pW, i32 nLayer, i32 nX, i32 nY)
{
    pW->aFloor[nLayer][nY][nX].bTransparent = 255;
}

static void World_WallA(World *pW, i32 nLayer, i32 nX, i32 nY, u8 bTransparent, u8 bBlocks)
{
    pW->aWallA[nLayer][nY][nX].bTransparent = bTransparent;
    pW->aWallA[nLayer][nY][nX].bBlocksMove  = bBlocks;
}

static void World_WallB(World *pW, i32 nLayer, i32 nX, i32 nY, u8 bTransparent, u8 bBlocks)
{
    pW->aWallB[nLayer][nY][nX].bTransparent = bTransparent;
    pW->aWallB[nLayer][nY][nX].bBlocksMove  = bBlocks;
}

/* The floor byte MapCell_TestFlag 6 reads. */
static void World_FloorBlocks(World *pW, i32 nLayer, i32 nX, i32 nY, u8 bBlocks)
{
    pW->aFloor[nLayer][nY][nX].bBlocksSight = bBlocks;
}

static void World_Occupy(World *pW, i32 nLayer, i32 nX, i32 nY, Soldier *pUnit)
{
    pW->aCells[nLayer][nY][nX].pUnit = pUnit;
}

/* The seam call, from the fixture's fixed current cell. */
static bool CanStep(World *pW, StepCtx *pCtx, i32 nDir,
                    i32 *pnX, i32 *pnY, i32 *pnLayer, bool *pbRe)
{
    return Unit_CanStepTo(pW->probe.nCurX, pW->probe.nCurY, pW->probe.nCurLayer,
                          (u8)nDir, pnX, pnY, pnLayer, pbRe, pCtx);
}

/* Nothing wants moving: the candidate has a floor and the current cell has no
 * ceiling. The probe reports the candidate unchanged and leaves the layer
 * bookkeeping alone. */
static void TestNoAnchor(World *pW)
{
    StepCtx ctx;

    printf("Map_ProbeStepOccupants (no anchor needed)\n");
    World_Setup(pW, &ctx, 0, false, 2);

    Check(Map_ProbeStepOccupants(&ctx), "  Map_ProbeStepOccupants @ 0x2778c accepts the step");
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

/* --- Unit_CanStepTo --------------------------------------------------------- */

static void TestCanStepPlain(World *pW)
{
    StepCtx ctx;
    i32 x, y, z;
    bool re;

    printf("Unit_CanStepTo (a plain step)\n");
    World_Setup(pW, &ctx, 0, false, 2);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  east step accepted");
    CheckEqI32(x, 3, "  output x");
    CheckEqI32(y, 2, "  output y");
    CheckEqI32(z, 1, "  output layer");
    Check(!re, "  no re-anchor reported");

    /* 0x27d2c: a direction outside the switch takes the default, which accepts. */
    World_Setup(pW, &ctx, 0, false, 2);
    Check(Unit_CanStepTo(2, 2, 1, 10, &x, &y, &z, &re, &ctx),
          "  direction 10 falls to the switch default");
}

static void TestCanStepBlank(World *pW)
{
    StepCtx ctx;
    i32 x, y, z;
    bool re;

    printf("Unit_CanStepTo (the head's blank test)\n");
    World_Setup(pW, &ctx, 0, false, 2);
    World_Blank(pW, 1, 3, 2);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  a blank candidate is refused");

    /* A large unit has no head blank test; its footprint is tested per arm instead. */
    World_Setup(pW, &ctx, 1, false, 2);
    World_Blank(pW, 1, 3, 2);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re),
          "  a large unit's own cell is not blank-tested in the head");
}

static void TestCanStepOccupant(World *pW)
{
    StepCtx ctx;
    i32 x, y, z;
    bool re;

    printf("Unit_CanStepTo (the head's occupant gate)\n");

    World_Setup(pW, &ctx, 0, false, 2);
    World_Occupy(pW, 1, 3, 2, &g_Foreign);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  an operative stranger blocks the cell");

    World_Setup(pW, &ctx, 0, false, 2);
    World_Occupy(pW, 1, 3, 2, &pW->aUnit[0]);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  the mover itself does not");

    World_Setup(pW, &ctx, 0, false, 2);
    World_Occupy(pW, 1, 3, 2, &g_Swap);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  the swap partner does not");

    /* 0x27cf0..0x27d18: a stranger that is not operative, not flagged at bUnk148 and
     * not one of the two special types does not block the cell. */
    World_Setup(pW, &ctx, 0, false, 2);
    g_Foreign.bStatus = 1;
    World_Occupy(pW, 1, 3, 2, &g_Foreign);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  a downed stranger does not block");

    World_Setup(pW, &ctx, 0, false, 2);
    g_Foreign.bStatus = 1;
    g_Foreign.bUnk148 = 1;
    World_Occupy(pW, 1, 3, 2, &g_Foreign);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  bUnk148 set blocks even when downed");

    World_Setup(pW, &ctx, 0, false, 2);
    g_Foreign.bStatus   = 1;
    g_Foreign.nUnitType = 16;
    World_Occupy(pW, 1, 3, 2, &g_Foreign);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  unit type 16 blocks even when downed");

    World_Setup(pW, &ctx, 0, false, 2);
    g_Foreign.bStatus   = 1;
    g_Foreign.nUnitType = 17;
    World_Occupy(pW, 1, 3, 2, &g_Foreign);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  unit type 17 blocks even when downed");
}

static void TestCanStepWalls(World *pW)
{
    StepCtx ctx;
    i32 x, y, z;
    bool re;

    printf("Unit_CanStepTo (the wall tests)\n");

    /* Direction 2 ends on the candidate's wall A having to be a real wall (0x28138). */
    World_Setup(pW, &ctx, 0, false, 2);
    World_WallA(pW, 1, 3, 2, 255, 0);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re),
          "  direction 2 refuses a candidate with no wall A");

    /* Direction 0 ends on the current cell's wall B (0x27e24). */
    World_Setup(pW, &ctx, 0, false, 0);
    World_WallB(pW, 1, 2, 2, 255, 0);
    Check(!CanStep(pW, &ctx, 0, &x, &y, &z, &re),
          "  direction 0 refuses a current cell with no wall B");

    /* Direction 1 tests the candidate's wall A as a blocking wall (0x27f78). */
    World_Setup(pW, &ctx, 0, false, 1);
    Check(CanStep(pW, &ctx, 1, &x, &y, &z, &re), "  direction 1 accepted with clear walls");
    World_Setup(pW, &ctx, 0, false, 1);
    World_WallA(pW, 1, 3, 1, 0, 1);
    Check(!CanStep(pW, &ctx, 1, &x, &y, &z, &re),
          "  direction 1 refuses a blocking wall A on the candidate");

    /* Direction 1 also refuses when the cell it passes between is blank (0x2807c). */
    World_Setup(pW, &ctx, 0, false, 1);
    World_Blank(pW, 1, 3, 1);
    Check(!CanStep(pW, &ctx, 1, &x, &y, &z, &re),
          "  direction 1 refuses a blank cell on the way round");
}

static void TestCanStepVertical(World *pW)
{
    StepCtx ctx;
    i32 x, y, z;
    bool re;

    printf("Unit_CanStepTo (the vertical pair)\n");

    /* Up: the cell entered and the cell left both need a floor that blocks (0x28918). */
    World_Setup(pW, &ctx, 0, false, 8);
    World_FloorBlocks(pW, 2, 2, 2, 1);
    World_FloorBlocks(pW, 1, 2, 2, 1);
    Check(CanStep(pW, &ctx, 8, &x, &y, &z, &re), "  up accepted when both floors block");
    CheckEqI32(z, 2, "  output layer is the cell entered");
    Check(!re, "  a vertical step reports no re-anchor");

    World_Setup(pW, &ctx, 0, false, 8);
    World_FloorBlocks(pW, 2, 2, 2, 1);
    Check(!CanStep(pW, &ctx, 8, &x, &y, &z, &re),
          "  refused when the cell left has no blocking floor");

    World_Setup(pW, &ctx, 0, false, 8);
    World_FloorBlocks(pW, 2, 2, 2, 1);
    World_FloorBlocks(pW, 1, 2, 2, 1);
    World_Occupy(pW, 2, 2, 2, &g_Foreign);
    Check(!CanStep(pW, &ctx, 8, &x, &y, &z, &re),
          "  refused when the cell above the current one is held");

    /* Down. */
    World_Setup(pW, &ctx, 0, false, 9);
    World_FloorBlocks(pW, 0, 2, 2, 1);
    World_FloorBlocks(pW, 1, 2, 2, 1);
    Check(CanStep(pW, &ctx, 9, &x, &y, &z, &re), "  down accepted");
    CheckEqI32(z, 0, "  output layer is the cell entered");

    /* With the search flag set, direction 9 tests the cell being left instead of the
     * one being entered (0x289AC..0x289F0). */
    World_Setup(pW, &ctx, 0, true, 9);
    World_FloorBlocks(pW, 1, 2, 2, 1);
    Check(CanStep(pW, &ctx, 9, &x, &y, &z, &re),
          "  with the flag set the cell left is what has to have a floor");
}

static void TestCanStepLarge(World *pW)
{
    StepCtx ctx;
    i32 x, y, z;
    bool re;

    printf("Unit_CanStepTo (a large unit's footprint)\n");

    World_Setup(pW, &ctx, 1, false, 2);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  accepted with a clear footprint");

    /* Direction 2 covers (3,2) and (4,2) on the current row and (3,3)/(4,3) behind it;
     * the far column is blank-tested at 0x29194 and 0x291d0. */
    World_Setup(pW, &ctx, 1, false, 2);
    World_Blank(pW, 1, 4, 2);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re),
          "  a blank cell in the footprint refuses the step");

    World_Setup(pW, &ctx, 1, false, 2);
    World_Occupy(pW, 1, 4, 2, &g_Foreign);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re),
          "  a stranger in the footprint refuses it, downed or not");

    World_Setup(pW, &ctx, 1, false, 2);
    g_Foreign.bStatus = 1;
    World_Occupy(pW, 1, 4, 2, &g_Foreign);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re),
          "  the large gate has no bUnk148 escape");

    World_Setup(pW, &ctx, 1, false, 2);
    World_Occupy(pW, 1, 4, 2, &g_Swap);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  the swap partner does not");

    /* The footprint's far corner gets its own bounds test (0x2915c). With the mover at
     * x = 8 the candidate at x = 9 is inside a ten-cell map and passes the head, but
     * the corner at x = 10 is not, and that is what refuses the step. */
    World_Setup(pW, &ctx, 1, false, 2);
    pW->probe.nCurX = 8;
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re),
          "  a footprint hanging off the map edge is refused");

    /* A normal unit has no second bounds test, so the same step goes through. */
    World_Setup(pW, &ctx, 0, false, 2);
    pW->probe.nCurX = 8;
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re),
          "  the same step for a normal unit is accepted");
}

static void TestCanStepAnchored(World *pW)
{
    StepCtx ctx;
    i32 x, y, z;
    bool re;

    printf("Unit_CanStepTo (the re-anchor reaches the caller)\n");

    /* Down: the candidate has no floor, so the step is really taken one layer lower,
     * and the seam reports the layer the settled cell lives in. */
    World_Setup(pW, &ctx, 0, false, 2);
    World_NoFloor(pW, 1, 3, 2);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  accepted");
    CheckEqI32(z, 0, "  output layer follows the anchored cell");
    CheckEqI32(x, 3, "  the column does not move");
    Check(re, "  the re-anchor is reported");

    /* Up. */
    World_Setup(pW, &ctx, 0, false, 2);
    World_Ceil(pW, 1, 2, 2, -20);
    World_NoFloor(pW, 2, 2, 2);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re), "  accepted");
    CheckEqI32(z, 2, "  output layer follows the anchored cell");
    Check(re, "  reported");

    /* The arm's occupant gate is weaker than the head's: it accepts an operative
     * stranger as soon as bUnk148 is clear (0x27e0c..0x27e20). The head only ever sees
     * the pre-anchor candidate, so here it never sees this unit at all. */
    World_Setup(pW, &ctx, 0, false, 2);
    World_NoFloor(pW, 1, 3, 2);
    World_Occupy(pW, 0, 3, 2, &g_Foreign);
    Check(CanStep(pW, &ctx, 2, &x, &y, &z, &re),
          "  the arm gate accepts an operative stranger the head would refuse");

    World_Setup(pW, &ctx, 0, false, 2);
    World_NoFloor(pW, 1, 3, 2);
    g_Foreign.bUnk148 = 1;
    World_Occupy(pW, 0, 3, 2, &g_Foreign);
    Check(!CanStep(pW, &ctx, 2, &x, &y, &z, &re),
          "  and refuses it once bUnk148 is set");
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

    TestCanStepPlain(&world);
    TestCanStepBlank(&world);
    TestCanStepOccupant(&world);
    TestCanStepWalls(&world);
    TestCanStepVertical(&world);
    TestCanStepLarge(&world);
    TestCanStepAnchored(&world);

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
