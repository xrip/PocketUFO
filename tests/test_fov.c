/* test_fov.c - parity checks for the field-of-view field and its fold.
 * Evidence: Map_ComputeFov @ 0x2deac, byte_AD720 @ 0xad720. */
#include "game/pufo_vistry.h"
#include "game/pufo_mapcell.h"

#include <stdio.h>
#include <string.h>

#include "pufo_test.h"

static inline void Check(const char *pWhat, int bOk) { TestCheck(pWhat, bOk); }
static inline void CheckU16(const char *pWhat, u16 nGot, u16 nWant)
{
    TestEqU16(pWhat, nGot, nWant);
}

static VisRay g_aRays[PUFO_VIS_RAY_COUNT];
static u8 g_aSteps[PUFO_VIS_TABLE_BYTES];
static FovField g_Fov;
static u32 g_aWindow[PUFO_HIT_WINDOW_SIDE * PUFO_HIT_WINDOW_SIDE * 4];

/* 40 x 40 cells, 4 layers. The origin sits at (20, 20) so offsets -15..15 stay
 * inside the grid. */
#define GRID 40
static MapCell g_aCells[4][GRID][GRID];

static MapCell *CellLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nY < 0 || nLayer < 0 || nX >= GRID || nY >= GRID) {
        return NULL;
    }
    return &g_aCells[nLayer][nY][nX];
}

static u16 Slot(i32 nOffX, i32 nOffY, i32 nSlot)
{
    return g_Fov.aSlot[Fov_EntryIndex(nOffX, nOffY)][nSlot];
}

static u16 Win(i32 nRow, i32 nCol, i32 nSlot)
{
    return (u16)(g_aWindow[4 * (PUFO_HIT_WINDOW_SIDE * nRow + nCol) + nSlot] & 0xFFFFu);
}

static void Run(const MapGeom *pGeom, i32 nRange, i32 nCostMode)
{
    memset(g_aCells, 0, sizeof(g_aCells));
    memset(g_aWindow, 0, sizeof(g_aWindow));
    Map_ComputeFov(&g_Fov, g_aWindow, pGeom, CellLookup, NULL, g_aSteps,
                   0, 0, 20, 20, 0, nRange, nCostMode, 0);
}

int main(void)
{
    MapGeom geom = {4, 4};

    Tact_BuildVisRayTable(g_aRays, g_aSteps);

    printf("an axial ray spends 20 range per step when the cost scale is 20.0\n");
    Run(&geom, 100, 0);
    CheckU16("origin entry slot 3", Slot(0, 0, 3), 100);
    CheckU16("origin entry slot 0 mirrors slot 3", Slot(0, 0, 0), 100);
    CheckU16("one step out", Slot(0, -1, 3), 80);
    CheckU16("two steps out", Slot(0, -2, 3), 60);
    CheckU16("three steps out", Slot(0, -3, 3), 40);
    CheckU16("four steps out", Slot(0, -4, 3), 20);
    CheckU16("the fifth step never lands (cost would reach 0)", Slot(0, -5, 3), 0);
    Check("no wall costs recorded on an empty map", Slot(0, 0, 1) == 0 && Slot(0, 0, 2) == 0);

    printf("the field is cleared before it is filled\n");
    g_Fov.aSlot[0][0] = 0xFFFF;
    Run(&geom, 100, 0);
    Check("a stale value does not survive", Slot(0, 0, 3) == 100);

    printf("the fold lands entry (offX, offY) on row 20+offY, col 20+offX\n");
    CheckU16("window (20, 20) slot 0", Win(20, 20, 0), 100);
    CheckU16("window (20, 20) slot 3", Win(20, 20, 3), 100);
    CheckU16("window (19, 20) slot 0", Win(19, 20, 0), 80);
    CheckU16("window (16, 20) slot 0", Win(16, 20, 0), 20);
    CheckU16("window (15, 20) stays empty", Win(15, 20, 0), 0);
    CheckU16("the column beside it holds the +X axial ray of pass 2", Win(20, 21, 0), 80);

    printf("the fold clamps at 255 and leaves the upper half of the record alone\n");
    g_aWindow[4 * (PUFO_HIT_WINDOW_SIDE * 20 + 20)] = 0xAB000000u | 200u;
    g_aWindow[4 * (PUFO_HIT_WINDOW_SIDE * 19 + 20) + 1] = 0xCD000000u;
    Map_ComputeFov(&g_Fov, g_aWindow, &geom, CellLookup, NULL, g_aSteps,
                   0, 0, 20, 20, 0, 100, 0, 0);
    CheckU16("200 + 100 clamps to 255", Win(20, 20, 0), 255);
    Check("the upper half survives the u16 write",
          (g_aWindow[4 * (PUFO_HIT_WINDOW_SIDE * 20 + 20)] & 0xFFFF0000u) == 0xAB000000u);
    CheckU16("an empty field slot adds nothing", Win(19, 20, 1), 0);
    Check("its upper half survives too",
          (g_aWindow[4 * (PUFO_HIT_WINDOW_SIDE * 19 + 20) + 1] & 0xFFFF0000u) == 0xCD000000u);

    printf("a8 == 1 selects the 8.0 cost scale\n");
    Run(&geom, 100, 1);
    CheckU16("one step out", Slot(0, -1, 3), 92);
    CheckU16("two steps out", Slot(0, -2, 3), 84);
    CheckU16("twelve steps out", Slot(0, -12, 3), 4);
    CheckU16("the thirteenth step never lands", Slot(0, -13, 3), 0);

    printf("a weapon of class 50 also selects 8.0\n");
    memset(g_aCells, 0, sizeof(g_aCells));
    memset(g_aWindow, 0, sizeof(g_aWindow));
    Map_ComputeFov(&g_Fov, g_aWindow, &geom, CellLookup, NULL, g_aSteps,
                   0, 0, 20, 20, 0, 100, 0, PUFO_FOV_WEAPON_CLASS_FAST);
    CheckU16("class 50 uses the fast scale", Slot(0, -1, 3), 92);

    printf("wall A on the cell being left: flag 2 records before it charges\n");
    {
        Mc1Rec spr;

        memset(&spr, 0, sizeof(spr));
        spr.bStrength = 5;        /* byte 22 */
        spr.bWallRangeCost = 7;   /* byte 23 */
        memset(g_aCells, 0, sizeof(g_aCells));
        memset(g_aWindow, 0, sizeof(g_aWindow));
        g_aCells[0][20][20].pWallSprA = &spr;
        Map_ComputeFov(&g_Fov, g_aWindow, &geom, CellLookup, NULL, g_aSteps,
                       0, 0, 20, 20, 0, 100, 0, 0);

        /* Ray 0 is the diagonal (-15, -15) and its first step carries flag 2, but
         * the -X axial ray of pass 6 also carries flag 2 and spends only 20 per
         * step, so the recorded value is the larger of the two. */
        CheckU16("slot 1 holds the best cost after the step, before the wall", Slot(0, 0, 1), 80);
        Check("the cell is marked as seen through wall A",
              ((u8)g_aCells[0][20][20].bFlags57[0] & 2u) != 0);
        CheckU16("the next entry pays the 28 step plus the 7 wall", Slot(-1, -1, 3), 65);
    }

    printf("a wall stops charging once the cell flag bit 2 is set\n");
    {
        Mc1Rec spr;

        memset(&spr, 0, sizeof(spr));
        spr.bStrength = 5;
        spr.bWallRangeCost = 7;
        spr.bBlocksMove = 1;      /* byte 42: non-zero, so the cell flag can open it */
        memset(g_aCells, 0, sizeof(g_aCells));
        memset(g_aWindow, 0, sizeof(g_aWindow));
        g_aCells[0][20][20].pWallSprA = &spr;
        g_aCells[0][20][20].bFlags29 = 4;   /* the wall is broken open */
        Map_ComputeFov(&g_Fov, g_aWindow, &geom, CellLookup, NULL, g_aSteps,
                       0, 0, 20, 20, 0, 100, 0, 0);
        CheckU16("no wall cost recorded", Slot(0, 0, 1), 0);
        CheckU16("the next entry pays only the step", Slot(-1, -1, 3), 72);
    }

    printf("a ceiling is charged on every cell but the origin\n");
    {
        Mc1Rec spr;

        memset(&spr, 0, sizeof(spr));
        spr.bStrength = 5;
        spr.bWallRangeCost = 3;
        memset(g_aCells, 0, sizeof(g_aCells));
        memset(g_aWindow, 0, sizeof(g_aWindow));
        g_aCells[0][20][20].pCeilSpr = &spr;      /* origin: free */
        g_aCells[0][19][20].pCeilSpr = &spr;      /* one step out: charged */
        Map_ComputeFov(&g_Fov, g_aWindow, &geom, CellLookup, NULL, g_aSteps,
                       0, 0, 20, 20, 0, 100, 0, 0);
        CheckU16("the entry is written before its own ceiling is charged", Slot(0, -1, 3), 80);
        CheckU16("two steps out carries the 3 charged one step earlier", Slot(0, -2, 3), 57);
    }

    return TestSummary("test_fov");
}
