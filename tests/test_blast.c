/* test_blast.c - parity checks for the blast footprint scan.
 * Evidence: Map_ScanBlastFootprint @ 0x32cd4. */
#include "game/pufo_vistry.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_unit.h"   /* the full Soldier type */

#include <stdio.h>
#include <string.h>

static int g_nChecks = 0;
static int g_nFails = 0;

static void Check(const char *pWhat, int bOk)
{
    ++g_nChecks;
    if (!bOk) {
        ++g_nFails;
        printf("  FAIL %s\n", pWhat);
    }
}

static VisRay g_aRays[PUFO_VIS_RAY_COUNT];
static u8 g_aSteps[PUFO_VIS_TABLE_BYTES];

/* 40 x 40 cells, 4 layers. The origin sits at (20, 20) so offsets -15..15 stay
 * inside the grid. */
#define GRID 40
static MapCell g_aCells[4][GRID][GRID];
static Soldier g_aUnits[8];
static Mc1Rec g_aSpr[8];

static MapCell *CellLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nY < 0 || nLayer < 0 || nX >= GRID || nY >= GRID) {
        return NULL;
    }
    return &g_aCells[nLayer][nY][nX];
}

static void Reset(void)
{
    memset(g_aCells, 0, sizeof(g_aCells));
    memset(g_aUnits, 0, sizeof(g_aUnits));
    memset(g_aSpr, 0, sizeof(g_aSpr));
    for (i32 i = 0; i < 8; ++i) {
        g_aUnits[i].bStatus = 1;   /* bStatus < 3 occupies */
    }
    /* The scan reads the origin cell's own nX/nY, exactly as the original reads
     * a2+26 and a2+27, so the grid cells must carry their coordinates. */
    for (i32 l = 0; l < 4; ++l) {
        for (i32 y = 0; y < GRID; ++y) {
            for (i32 x = 0; x < GRID; ++x) {
                g_aCells[l][y][x].nX = (u8)x;
                g_aCells[l][y][x].nY = (u8)y;
                g_aCells[l][y][x].nZ = (u8)l;
            }
        }
    }
}

static MapCell *At(i32 nOffX, i32 nOffY)
{
    return &g_aCells[0][20 + nOffY][20 + nOffX];
}

static void Occupy(i32 nOffX, i32 nOffY, i32 nWhich)
{
    MapCell *pCell = At(nOffX, nOffY);

    pCell->pUnit = &g_aUnits[nWhich];
    g_aUnits[nWhich].pCell = pCell;
}

static int AnyFlagSet(void)
{
    for (i32 l = 0; l < 4; ++l) {
        for (i32 y = 0; y < GRID; ++y) {
            for (i32 x = 0; x < GRID; ++x) {
                if (g_aCells[l][y][x].bFlags57[0] != 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

int main(void)
{
    MapGeom geom = { 4, 4 };   /* 40 x 40 cells, 4 layers */
    MapCell *pOrigin;

    memset(g_aRays, 0, sizeof(g_aRays));
    memset(g_aSteps, 0, sizeof(g_aSteps));
    Tact_BuildVisRayTable(g_aRays, g_aSteps);

    printf("Map_ScanBlastFootprint @ 0x32cd4\n");

    /* An empty map finds nothing across all eight passes and four layers, but the
     * wall flags are still written: the flag write does not depend on a sprite
     * being present. */
    Reset();
    pOrigin = At(0, 0);
    Check("empty map returns false",
          !Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, pOrigin));
    Check("wall flags are written even with no sprites in the way", AnyFlagSet());

    /* One cell out is inside the radius-two footprint. */
    Reset();
    Occupy(1, 0, 0);
    Check("an occupant one cell out is found",
          Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* Exactly two cells out is still inside: the gate is <= 2.0. */
    Reset();
    Occupy(2, 0, 0);
    Check("an occupant two cells out is found",
          Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* Three cells out is past the gate, so the ray ends before it. */
    Reset();
    Occupy(3, 0, 0);
    Occupy(0, 3, 1);
    Occupy(-3, 0, 2);
    Occupy(0, -3, 3);
    Check("an occupant three cells out is never reached",
          !Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* The gate is a distance, not a Chebyshev span: (2,1) is 2.236 away. */
    Reset();
    Occupy(2, 1, 0);
    Occupy(-2, 2, 1);
    Check("a diagonal at distance 2.24 is outside",
          !Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* (2,2) is 2.83 away, but (1,2) is 2.24 and (2,0) is 2.0. */
    Reset();
    Occupy(1, 2, 0);
    Check("an occupant at (1,2) is outside",
          !Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));
    Reset();
    Occupy(2, 0, 0);
    Occupy(0, 2, 1);
    Occupy(-2, 0, 2);
    Occupy(0, -2, 3);
    Check("all four axial cells at distance 2 are inside",
          Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* Step 0 of every row is the origin cell itself, so an occupant standing on
     * the origin is reported. */
    Reset();
    Occupy(0, 0, 0);
    Check("an occupant on the origin cell is reported",
          Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* A ceiling sprite whose byte 0 is non-zero ends the ray, so it shields
     * anything further out along that ray. */
    Reset();
    At(1, 0)->pCeilSpr = &g_aSpr[0];
    g_aSpr[0].gap_0[0] = 1;
    Occupy(2, 0, 0);
    Check("a blocking ceiling at (1,0) shields (2,0)",
          !Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* The same ceiling with byte 0 clear does not block. */
    Reset();
    At(1, 0)->pCeilSpr = &g_aSpr[0];
    g_aSpr[0].gap_0[0] = 0;
    Occupy(2, 0, 0);
    Check("a ceiling with byte 0 clear does not block",
          Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* A wall sprite whose 0x2b byte is non-zero ends the ray. Wall A on the cell
     * being left is the flag-2 case; the flag side is not asserted here, only
     * that some wall on the first step stops the scan. */
    Reset();
    At(1, 0)->pWallSprA = &g_aSpr[0];
    g_aSpr[0].bUnk2B = 1;
    Occupy(2, 0, 0);
    Check("a wall with byte 0x2b set at (1,0) shields (2,0)",
          !Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* The same wall with 0x2b clear blocks nothing, even though 0x2a is set:
     * this scan tests 0x2b, not the byte Map_ComputeFov tests. */
    Reset();
    At(1, 0)->pWallSprA = &g_aSpr[0];
    g_aSpr[0].bUnk2B = 0;
    g_aSpr[0].bBlocksMove = 1;
    Occupy(2, 0, 0);
    Check("0x2a alone does not block a blast ray",
          Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0)));

    /* Wall flags are written on the cells the rays cross. */
    Reset();
    At(1, 0)->pWallSprA = &g_aSpr[0];
    At(1, 0)->pWallSprB = &g_aSpr[1];
    Map_ScanBlastFootprint(&geom, CellLookup, NULL, g_aSteps, At(0, 0));
    Check("wall flags are recorded along the rays", AnyFlagSet());

    printf("\n%s: %d check(s), %d failure(s)\n",
           g_nFails == 0 ? "PASS" : "FAIL", g_nChecks, g_nFails);
    return g_nFails == 0 ? 0 : 1;
}
