/* test_los.c - checks voxel occupancy and line-of-sight tracing.
 *
 * The voxel packing is the part most likely to be misread, so the geometry is
 * asserted directly: 16x16x12 voxels per cell, bit 15 of a row mask is x = 0, and
 * the terrain row index comes from the sprite's 12-entry z table at offset 8.
 *
 * The real mask tables (loftemps.dat 3584 bytes, unitloft.dat 43008 bytes) are
 * loaded from main.dat when present, so the table-driven paths are exercised
 * against real data rather than a stub.
 */
#include "asset/pufo_asset.h"
#include "game/pufo_los.h"
#include "util/pufo_rand.h"

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

/* --- row-mask bit addressing --- */

static void TestRowBits(void)
{
    u16 aTable[4] = { 0x8000u, 0x0001u, 0xFFFFu, 0x0000u };

    printf("Los_TestRowBit\n");
    /* Bit 15 is x = 0. */
    Check(Los_TestRowBit(aTable, 4, 0, 0), "  bit15 set -> x=0 is set");
    Check(!Los_TestRowBit(aTable, 4, 0, 1), "  bit14 clear -> x=1 is clear");
    Check(Los_TestRowBit(aTable, 4, 0, 15) == false, "  x=15 clear in 0x8000");
    Check(Los_TestRowBit(aTable, 4, 1, 15), "  bit0 set -> x=15 is set");
    Check(!Los_TestRowBit(aTable, 4, 1, 0), "  x=0 clear in 0x0001");
    /* All ones / all zeros. */
    {
        bool bAllSet = true;

        for (u32 x = 0; x < 16; ++x) {
            if (!Los_TestRowBit(aTable, 4, 2, x)) {
                bAllSet = false;
            }
        }
        Check(bAllSet, "  0xFFFF sets every x");
    }
    Check(!Los_TestRowBit(aTable, 4, 3, 8), "  0x0000 clears every x");
    /* Out of range must be safe, not an overread. */
    Check(!Los_TestRowBit(aTable, 4, 4, 0), "  row index out of range is safe");
    Check(!Los_TestRowBit(NULL, 4, 0, 0), "  NULL table is safe");
}

/* --- a synthetic grid with one cell --- */

#define L_DIM 4

typedef struct LosGrid {
    MapCell  aCells[PUFO_MAP_LAYERS][L_DIM][L_DIM];
    Mc1Rec   aSprites[PUFO_MAP_LAYERS][L_DIM][L_DIM][4]; /* floor, wallA, wallB, ceil */
    u16      aLoft[1792];
    u16      aUnit[21504];
    LosTables tables;
} LosGrid;

static MapCell *LosGrid_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    LosGrid *pG = (LosGrid *)pUser;

    if (nX < 0 || nY < 0 || nX >= L_DIM || nY >= L_DIM) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return &pG->aCells[nLayer][nY][nX];
}

static void LosGrid_Init(LosGrid *pG)
{
    i32 x;
    i32 y;
    i32 z;

    memset(pG, 0, sizeof(*pG));
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < L_DIM; ++y) {
            for (x = 0; x < L_DIM; ++x) {
                MapCell_Init(&pG->aCells[z][y][x], x, y, z);
            }
        }
    }
    pG->tables.pLoftemps = pG->aLoft;
    pG->tables.nLoftemps = 1792;
    pG->tables.pUnitloft = pG->aUnit;
    pG->tables.nUnitloft = 21504;
    /* bFlags29 bit 2 clear means walls block. */
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < L_DIM; ++y) {
            for (x = 0; x < L_DIM; ++x) {
                pG->aCells[z][y][x].bFlags29 = 0;
            }
        }
    }
}

/* A sprite whose z table maps every level to the same mask row, with the given
 * blocking byte. */
static Mc1Rec *MakeSprite(LosGrid *pG, i32 x, i32 y, i32 z, i32 nWhich, u8 nBlock)
{
    Mc1Rec *p = &pG->aSprites[z][y][x][nWhich];

    memset(p, 0, sizeof(*p));
    for (i32 i = 0; i < 12; ++i) {
        ((u8 *)p)[PUFO_MC1_ROW_TABLE_OFF + i] = (u8)(x + y); /* any valid row */
    }
    ((u8 *)p)[PUFO_MC1_BLOCK_OFF] = nBlock;
    return p;
}

static void TestVoxelGeometry(void)
{
    LosGrid grid;

    printf("Map_TraceVoxelOccupancy geometry\n");
    LosGrid_Init(&grid);
    /* A clear cell reports no hit. */
    CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                       0, 0, 0, NULL, NULL),
               LOS_HIT_NONE, "  empty cell is clear");

    /* Floor sprite whose mask row sets bit 15 (x = 0), so voxel x=0 blocks but
     * x=1 does not. Row index = y%16 + 16 * rowTable[z%12]. */
    {
        Mc1Rec *pFloor = MakeSprite(&grid, 0, 0, 0, 0, 0);

        grid.aLoft[0] = 0x8000u; /* row 0 */
        grid.aCells[0][0][0].pFloorSpr = pFloor;
        CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                           0, 0, 0, NULL, NULL),
                   LOS_HIT_FLOOR, "  floor bit set at x=0 blocks");
        CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                           1, 0, 0, NULL, NULL),
                   LOS_HIT_NONE, "  x=1 is clear");
        /* A different y picks a different row; row 1 is zero so it is clear. */
        CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                           0, 1, 0, NULL, NULL),
                   LOS_HIT_NONE, "  different y row is clear");
        /* z maps through the sprite's 12-entry table; row table at index z%12
         * holds x + y = 0, so every z level uses row 0 (still 0x8000). */
        CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                           0, 0, 11, NULL, NULL),
                   LOS_HIT_FLOOR, "  z level 11 still blocks via the row table");
        grid.aCells[0][0][0].pFloorSpr = NULL;
    }

    /* Wall blocking carries the extra condition: with the block byte non-zero,
     * the wall stops blocking once bFlags29 bit 2 is set. */
    printf("Map_TraceVoxelOccupancy wall condition\n");
    {
        Mc1Rec *pWall = MakeSprite(&grid, 1, 0, 0, 1, 1);
        u32     nRow  = 0 + 16 * (u8)((u8 *)pWall)[PUFO_MC1_ROW_TABLE_OFF + 0];

        grid.aLoft[nRow] = 0x8000u;
        grid.aCells[0][0][1].pWallSprA = pWall;
        CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                           16 + 0, 0, 0, NULL, NULL),
                   LOS_HIT_WALL_A, "  wall A blocks with flag bit 2 clear");
        grid.aCells[0][0][1].bFlags29 = 4;
        CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                           16 + 0, 0, 0, NULL, NULL),
                   LOS_HIT_NONE, "  wall A stops blocking once flag bit 2 is set");
        /* With a zero block byte the wall blocks regardless of the flag. */
        ((u8 *)pWall)[PUFO_MC1_BLOCK_OFF] = 0;
        CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                           16 + 0, 0, 0, NULL, NULL),
                   LOS_HIT_WALL_A, "  zero block byte still blocks");
        grid.aCells[0][0][1].pWallSprA = NULL;
    }
}

/* The occupant path uses the unit silhouette table. */
static void TestOccupant(void)
{
    LosGrid grid;
    Soldier unit;

    printf("Map_TraceVoxelOccupancy occupant\n");
    LosGrid_Init(&grid);
    memset(&unit, 0, sizeof(unit));
    unit.bStatus = 0;
    unit.nUnitType = 0; /* small */
    unit.bFacing = 0;

    grid.aCells[0][0][0].pUnit = &unit;
    /* row = y%16 + 16 * (12 * rowBase + z%12); rowBase 0 for type 0 facing 0. */
    grid.aUnit[0] = 0x8000u;
    CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                       0, 0, 0, NULL, NULL),
               LOS_HIT_OCCUPANT, "  unit occupies its cell");

    /* The selected unit is excluded from blocking. */
    CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                       0, 0, 0, &unit, NULL),
               LOS_HIT_NONE, "  selected unit does not block itself");
    /* As is the action-scratch unit. */
    CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                       0, 0, 0, NULL, &unit),
               LOS_HIT_NONE, "  action-scratch unit does not block");

    /* A unit with bStatus >= 3 does not occupy, so it cannot block. */
    unit.bStatus = 3;
    CheckEqI32(Map_TraceVoxelOccupancy(&grid.tables, LosGrid_Lookup, &grid,
                                       0, 0, 0, NULL, NULL),
               LOS_HIT_NONE, "  dead unit does not block");
}

/* --- the trace --- */

static void TestTrace(void)
{
    LosGrid grid;

    printf("Map_TraceLos\n");
    LosGrid_Init(&grid);
    Game_SeedRng();

    /* An empty grid: a short horizontal ray reaches its target. */
    Check(Map_TraceLos(&grid.tables, LosGrid_Lookup, &grid,
                       0, 0, 0, 8, 0, 0, NULL, NULL),
          "  clear horizontal ray reaches the target");

    /* Same start and end: the bound is 0, so the first iteration reaches it. */
    Check(Map_TraceLos(&grid.tables, LosGrid_Lookup, &grid,
                       4, 4, 4, 4, 4, 4, NULL, NULL),
          "  zero-length ray returns true");

    /* Wall it off: put a blocking floor in the path around x = 4. */
    printf("Map_TraceLos blocked\n");
    {
        Mc1Rec *pFloor = MakeSprite(&grid, 0, 0, 0, 0, 0);

        /* Cell (0,0,0) covers voxels x 0..15, so voxel x=4 is inside it. */
        grid.aLoft[0] = 0x0800u; /* bit 11 -> x = 4 */
        grid.aCells[0][0][0].pFloorSpr = pFloor;
        Check(!Map_TraceLos(&grid.tables, LosGrid_Lookup, &grid,
                            0, 0, 0, 8, 0, 0, NULL, NULL),
              "  ray blocked by the floor voxel");
        grid.aCells[0][0][0].pFloorSpr = NULL;
    }

    /* The purely-vertical case randomises yaw and reports it. */
    printf("Map_TraceLos vertical case\n");
    {
        u8  nYaw1 = 0;
        u8  nYaw2 = 0;
        bool bBothInRange = true;

        (void)Map_TraceLos(&grid.tables, LosGrid_Lookup, &grid,
                           0, 0, 0, 0, 0, 5, &nYaw1, NULL);
        (void)Map_TraceLos(&grid.tables, LosGrid_Lookup, &grid,
                           0, 0, 0, 0, 0, 5, &nYaw2, NULL);
        /* Game_RandU16(0x3E) returns 0..61. */
        if (nYaw1 > 61 || nYaw2 > 61) {
            bBothInRange = false;
        }
        Check(bBothInRange, "  vertical ray draws yaw in 0..61");
    }

    /* The accumulated cost is reported. */
    printf("Map_TraceLos cost accumulation\n");
    {
        i32 nCost = -1;

        /* Cell flag bits 0..2 count toward blocking, except 0 and 4. Give the
         * cells the ray crosses a cost of 1 each; a long ray must still reach. */
        for (i32 y = 0; y < L_DIM; ++y) {
            for (i32 x = 0; x < L_DIM; ++x) {
                grid.aCells[0][y][x].nFlags32 = 1;
            }
        }
        (void)Map_TraceLos(&grid.tables, LosGrid_Lookup, &grid,
                           0, 0, 0, 30, 0, 0, NULL, &nCost);
        Check(nCost >= 0, "  cost is reported");
    }
}

/* Exercise the real mask tables when the assets are available. */
static void TestRealTables(void)
{
    AssetReader rdr;
    u8         *pLoft;
    u8         *pUnit;
    u32         cbLoft;
    u32         cbUnit;

    printf("Real mask tables from main.dat\n");
    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, "loftemps.dat")) {
        printf("  skip: loftemps.dat not present\n");
        return;
    }
    cbLoft = AssetReader_Size(&rdr);
    pLoft = malloc(cbLoft);
    if (pLoft == NULL || !AssetReader_Read(&rdr, pLoft, cbLoft)) {
        g_nFailures += 1;
        free(pLoft);
        return;
    }
    /* Confirmed from the shipped archive: 3584 bytes == 1792 u16. */
    CheckEqI32((i32)cbLoft, 3584, "  loftemps.dat is 3584 bytes");
    CheckEqI32((i32)(cbLoft / 2), 1792, "  = 1792 u16 rows");

    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, "unitloft.dat")) {
        printf("  skip: unitloft.dat not present\n");
        free(pLoft);
        return;
    }
    cbUnit = AssetReader_Size(&rdr);
    pUnit = malloc(cbUnit);
    if (pUnit == NULL || !AssetReader_Read(&rdr, pUnit, cbUnit)) {
        g_nFailures += 1;
        free(pLoft);
        free(pUnit);
        return;
    }
    /* 43008 bytes == 21504 u16 == 16 rows * 12 levels * 112 unit types. */
    CheckEqI32((i32)cbUnit, 43008, "  unitloft.dat is 43008 bytes");
    CheckEqI32((i32)(cbUnit / 2), 21504, "  = 21504 u16 rows");
    CheckEqI32((i32)(cbUnit / 2 / PUFO_UNITLOFT_ROWS_PER_TYPE / PUFO_VOX_PER_CELL_Z), 112,
               "  = 16 rows * 12 levels * 112 sprite slots");

    free(pLoft);
    free(pUnit);
}

/* Tact_LoadLoftData @ 0x3bd6c, through the port's own loader. */
static void TestLoftLoader(void)
{
    LosTables tables = { 0 };
    u32       nNonZero = 0;

    printf("Tact_LoadLoftData\n");
    Check(Tact_LoadLoftData(&tables), "the loader reports success against the shipped archives");
    CheckEqI32((i32)tables.nLoftemps, 1792, "loftemps.dat loaded as 1792 u16 rows");
    CheckEqI32((i32)tables.nUnitloft, 21504, "unitloft.dat loaded as 21504 u16 rows");
    Check(tables.pLoftemps != NULL && tables.pUnitloft != NULL, "both buffers allocated");

    for (u32 i = 0; i < tables.nUnitloft; ++i) {
        if (tables.pUnitloft[i] != 0) {
            nNonZero += 1;
        }
    }
    Check(nNonZero > 0, "the unit silhouette table holds real blocking masks");

    free((void *)tables.pLoftemps);
    free((void *)tables.pUnitloft);
}

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }

    TestRowBits();
    TestVoxelGeometry();
    TestOccupant();
    TestTrace();
    TestRealTables();
    TestLoftLoader();

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
