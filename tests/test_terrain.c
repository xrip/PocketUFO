/* test_terrain.c - Map_BuildMissionTerrain @ 0x22e10.
 *
 * The builder is 10,676 bytes and reaches everything through globals, so the fixture supplies the
 * whole world it needs: a caller-owned 14400-slot cell array, a stub font whose sprite records are
 * identifiable by their nSlot, the 36-byte aMissionTileTypes array, and hooks that record the two
 * calls the port does not own.
 *
 * What is asserted, and why these things:
 *   - the cell allocation and each cell's own coordinates, because the three copies of that loop in
 *     the original are the only part of the function that is fully deterministic;
 *   - the tail's nFlags32 mask, because MapCell_Init clears the low 7 bits (0xFFFFFF80) while the
 *     tail additionally clears bit 7 (0xFFFFFF87) -- the difference is observable on fresh malloc
 *     memory, so it proves the tail ran;
 *   - the two XCom-base stamping passes cell by cell, because they depend only on aMissionTileTypes
 *     and g_aTileSprIndex and not on any .ma1 file or on the RNG;
 *   - the guards on the 0x10/0x11/0x12/0x14 tile-type sentinels, including the fact that pass 1 looks
 *     one COLUMN to the right while pass 2 looks one ROW down.
 *
 * Game_RandU16 is the CRT rand(), so srand() pins a run. RNG-derived values are asserted as bounds
 * and shapes rather than pinned numbers, except where a value is not RNG-derived at all.
 */
#include "asset/pufo_asset.h"
#include "game/pufo_mapctx.h"   /* PUFO_MAP_CELL_DIM */
#include "game/pufo_terrain.h"
#include "game/pufo_template.h"
#include "pufo_common.h"

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
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, nWant);
        g_nFailures += 1;
    }
}

/* ------------------------------------------------------------------ stub font */

#define SPR_POOL 128

static Mc1Rec    g_aSprPool[SPR_POOL];
static Mc1Rec   *g_apSprIndex[SPR_POOL];
static PufoFont  g_font;

/* Every record carries its own index in nSlot, so an assertion can name the sprite it expects.
 * The font's per-flag bases stay zero, so a template slot value resolves as value - 2 and a
 * g_aTileSprIndex entry resolves as entry - 1. */
static void FontFixture_Reset(void)
{
    int i;

    memset(g_aSprPool, 0, sizeof(g_aSprPool));
    memset(&g_font, 0, sizeof(g_font));
    for (i = 0; i < SPR_POOL; ++i) {
        g_aSprPool[i].nSlot = (u16)i;
        g_apSprIndex[i] = &g_aSprPool[i];
    }
    g_font.apMc1Index = g_apSprIndex;
    g_font.nMc1IndexCount = SPR_POOL;

    /* The per-flag sprite bases are the font block's first five u16s, so flags 2, 3 and 4 are
     * anFlagSprBase[2..4] -- the terrain group, the craft group and the UFO-module group. Pushing
     * them past the pool makes every sprite a .ma1 template resolves come back NULL, so the only
     * sprites that ever land on a cell are the ones the stamping passes ask for by explicit index. */
    g_font.anFlagSprBase[2] = SPR_POOL;
    g_font.anFlagSprBase[3] = SPR_POOL;
    g_font.anFlagSprBase[4] = SPR_POOL;
}

static Mc1Rec *Spr(i32 nIndex)
{
    return (nIndex >= 0 && nIndex < SPR_POOL) ? &g_aSprPool[nIndex] : NULL;
}

/* ------------------------------------------------------------------ env fixture */

static MapCell   *g_apCells[PUFO_TERRAIN_CELL_SLOTS];
static ObjectRec  g_aShotRecs[64];
static const ObjectRec *g_apShotTable[64];
static RmpData    g_rmp;
static u8         g_aTileTypes[PUFO_TERRAIN_GRID_CELLS];

static u8  g_nMapLayers, g_nMissionType, g_nVariant, g_nTier, g_bUnk15115;
static u8  g_nDimX, g_nDimY, g_planeX, g_planeY, g_bSpawnAlt;
static i32 g_nTierSource, g_nShotX, g_nShotY, g_nSpawnX, g_nSpawnY;
static MapTerrainEnv g_env;

static void Hook_LoadTactSprites(void *pUser);
static void Hook_ResolveShot(i32 nTgtX, i32 nTgtY, i32 nTgtZ, i32 nSlot,
                             const ObjectRec *pObj, void *pUser);

static void EnvFixture_Reset(void)
{
    FontFixture_Reset();
    memset(g_apCells, 0, sizeof(g_apCells));
    memset(g_aTileTypes, 0, sizeof(g_aTileTypes));
    memset(&g_rmp, 0, sizeof(g_rmp));

    g_nMapLayers = 2;
    g_nMissionType = 0;      /* jungle */
    g_nVariant = 0;
    g_nTier = 0;
    g_bUnk15115 = 0;
    g_nDimX = 6;
    g_nDimY = 6;
    g_planeX = 0xFF;
    g_planeY = 0xFF;
    g_bSpawnAlt = 0;
    g_nTierSource = 0;
    g_nShotX = -1;
    g_nShotY = -1;
    g_nSpawnX = -1;
    g_nSpawnY = -1;

    /* Map+0x108 is an array of object POINTERS, so the fixture needs a table as well as records. */
    for (int i = 0; i < 64; ++i) {
        g_apShotTable[i] = &g_aShotRecs[i];
    }

    memset(&g_env, 0, sizeof(g_env));
    g_env.pnMapLayers      = &g_nMapLayers;
    g_env.pnMissionType    = &g_nMissionType;
    g_env.pnTerrainVariant = &g_nVariant;
    g_env.pnTier           = &g_nTier;
    g_env.pnTierSource     = &g_nTierSource;
    g_env.pbUnk15115       = &g_bUnk15115;
    g_env.pnMapDimX        = &g_nDimX;
    g_env.pnMapDimY        = &g_nDimY;
    g_env.pPlaneTileX      = &g_planeX;
    g_env.pPlaneTileY      = &g_planeY;
    g_env.pnShotX          = &g_nShotX;
    g_env.pnShotY          = &g_nShotY;
    g_env.pnSpawnTileX     = &g_nSpawnX;
    g_env.pnSpawnTileY     = &g_nSpawnY;
    g_env.pbSpawnAlt       = &g_bSpawnAlt;
    g_env.apCells          = g_apCells;
    g_env.apShotWeapons    = g_apShotTable;
    g_env.paTileTypes      = g_aTileTypes;
    g_env.pRmp             = &g_rmp;
    g_env.pFont            = &g_font;
    g_env.pfnLoadTactSprites = Hook_LoadTactSprites;
    g_env.pfnResolveShot     = Hook_ResolveShot;
}

static void EnvFreeCells(void)
{
    size_t i;

    for (i = 0; i < PUFO_TERRAIN_CELL_SLOTS; ++i) {
        free(g_apCells[i]);
        g_apCells[i] = NULL;
    }
}

/* ------------------------------------------------------------------ hooks */

static int          g_nTactSpriteLoads;
static int          g_nShots;
static const ObjectRec *g_apShotObjs[64];
static i32          g_aShotTgtX[64];

static void Hook_LoadTactSprites(void *pUser)
{
    (void)pUser;
    g_nTactSpriteLoads += 1;
}

static void Hook_ResolveShot(i32 nTgtX, i32 nTgtY, i32 nTgtZ, i32 nSlot,
                             const ObjectRec *pObj, void *pUser)
{
    (void)nTgtY;
    (void)nTgtZ;
    (void)nSlot;
    (void)pUser;
    if (g_nShots < 64) {
        g_apShotObjs[g_nShots] = pObj;
        g_aShotTgtX[g_nShots] = nTgtX;
    }
    g_nShots += 1;
}

static void HookFixture_Reset(void)
{
    g_nTactSpriteLoads = 0;
    g_nShots = 0;
    memset(g_apShotObjs, 0, sizeof(g_apShotObjs));
}

/* ------------------------------------------------------------------ assertions over the grid */

static MapCell *Cell(i32 nIdx)
{
    return g_apCells[nIdx];
}

/* Walk the cells the builder is supposed to have produced for a map of nDimX x nDimY tiles. */
static void CheckAllocatedGrid(i32 nDimX, i32 nDimY, const char *pszWhat)
{
    i32 nLayer;
    i32 nY;
    i32 nX;
    bool bAll = true;
    bool bFlags = true;

    for (nLayer = 0; nLayer < PUFO_TERRAIN_LAYER_COUNT && bAll; ++nLayer) {
        for (nY = 0; nY < PUFO_CELLS_PER_TILE * nDimY && bAll; ++nY) {
            for (nX = 0; nX < PUFO_CELLS_PER_TILE * nDimX && bAll; ++nX) {
                MapCell *pCell = g_apCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer
                                           + PUFO_MAP_CELL_DIM * nY + nX];
                if (pCell == NULL || pCell->nX != (u8)nX || pCell->nY != (u8)nY
                    || pCell->nZ != (u8)nLayer) {
                    bAll = false;
                } else if (pCell->nFlags32 & 0x78u) {
                    /* The tail masks with 0xFFFFFF87, which clears bits 3..6 and keeps 0, 1, 2, 7. */
                    bFlags = false;
                }
            }
        }
    }
    Check(bAll, pszWhat);
    Check(bFlags, "the tail cleared bits 3..6 of nFlags32 on every cell");
}

/* Map_PatchCraftSprites runs after the flag mask and clears uUnk20 on the four craft sprites at
 * nCraftSprBase + 42..45. With the base at 0 those are pool entries 42..45, so priming them proves
 * the whole tail ran -- and that it does not run when the map does not have exactly two layers. */
static void PrimeCraftSprites(void)
{
    for (int i = 42; i < 46; ++i) {
        g_aSprPool[i].uUnk20 = 0xABCD;
    }
}

static void CheckCraftPatchRan(bool bShouldRun, const char *pszWhat)
{
    bool bRan = true;

    for (int i = 42; i < 46; ++i) {
        if (g_aSprPool[i].uUnk20 != 0) {
            bRan = false;
        }
    }
    Check(bRan == bShouldRun, pszWhat);
}

/* ------------------------------------------------------------------ tests */

static void TestNormalPathAllocatesTheGrid(void)
{
    printf("Map_BuildMissionTerrain: cell allocation (normal path)\n");
    EnvFixture_Reset();
    HookFixture_Reset();
    PrimeCraftSprites();
    srand(1);

    Map_BuildMissionTerrain(&g_env);

    CheckEqI32(g_nTactSpriteLoads, 1, "Font_LoadTactSprites hook ran once");
    CheckAllocatedGrid(6, 6, "every cell of 4 x 60 x 60 exists with its own coordinates");
    Check(g_planeX < 6 && g_planeY < 6, "the craft pad tile landed inside the map");
    CheckCraftPatchRan(true, "the tail ran Map_PatchCraftSprites for a two-layer map");
    EnvFreeCells();

    /* Map_PatchCraftSprites bails out unless the map has exactly two layers. */
    EnvFixture_Reset();
    HookFixture_Reset();
    PrimeCraftSprites();
    g_nMapLayers = 3;
    srand(1);
    Map_BuildMissionTerrain(&g_env);
    CheckCraftPatchRan(false, "Map_PatchCraftSprites does not run for a three-layer map");
    EnvFreeCells();
}

static void TestSmallerMapLeavesTheRestUntouched(void)
{
    i32 i;
    bool bClean = true;

    printf("Map_BuildMissionTerrain: a 3 x 4 tile map\n");
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nDimX = 3;
    g_nDimY = 4;
    srand(2);

    Map_BuildMissionTerrain(&g_env);

    CheckAllocatedGrid(3, 4, "the 30 x 40 cell region is allocated and positioned");

    /* Everything outside 10*dimX columns of rows 0..39 must still be the NULL we started with. */
    for (i = 0; i < PUFO_TERRAIN_CELL_SLOTS; ++i) {
        i32 nLayer = i / PUFO_TERRAIN_CELLS_PER_LAYER;
        i32 nRem   = i % PUFO_TERRAIN_CELLS_PER_LAYER;
        i32 nY     = nRem / PUFO_MAP_CELL_DIM;
        i32 nX     = nRem % PUFO_MAP_CELL_DIM;
        bool bInside = nY < 40 && nX < 30;

        (void)nLayer;
        if (!bInside && g_apCells[i] != NULL) {
            bClean = false;
        }
    }
    Check(bClean, "no cell was written outside the map's own region");
    EnvFreeCells();
}

static void TestTierAndShots(void)
{
    int nLastSweep;
    bool bSlotZero;

    printf("Map_BuildMissionTerrain: tier byte and the ceiling-break shots\n");

    /* Variant 0 never fires the sweep, whatever Map_bUnk15115 says. */
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nVariant = 0;
    g_bUnk15115 = 0x64;
    srand(3);
    Map_BuildMissionTerrain(&g_env);
    CheckEqI32(g_nShots, 0, "variant 0 resolves no shots");
    EnvFreeCells();

    /* Variant 2 with Map_bUnk15115 below 0x32: the count is forced to zero. */
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nVariant = 2;
    g_nTierSource = 1;
    g_bUnk15115 = 0x10;
    srand(4);
    Map_BuildMissionTerrain(&g_env);
    CheckEqI32(g_nTier, 1, "Map_nTier is written from the tier dword");
    CheckEqI32(g_nShots, 0, "Map_bUnk15115 below 0x32 forces the shot count to zero");
    EnvFreeCells();

    /* Variant 2 with the gate open. The randomised first sweep is gated on Map_IsValidCell, the
     * second one is not, and its count is (Map_bUnk15115 - 30) / 10 = 7. */
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nVariant = 2;
    g_nTierSource = 2;
    g_bUnk15115 = 0x64;
    srand(5);
    Map_BuildMissionTerrain(&g_env);
    Check(g_nShots >= 7, "variant 2 with the gate open resolves at least the 7 ungated shots");
    nLastSweep = g_nShots - 7;
    bSlotZero = true;
    for (int k = nLastSweep; k < g_nShots; ++k) {
        if (g_apShotObjs[k] != &g_aShotRecs[0]) {
            bSlotZero = false;
        }
    }
    Check(bSlotZero, "the ungated sweep always uses object slot 0");
    EnvFreeCells();

    /* Variant 3 selects object slot 51 in the gated sweep. */
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nVariant = 3;
    g_bUnk15115 = 0x64;
    srand(6);
    Map_BuildMissionTerrain(&g_env);
    Check(g_nShots > 7, "variant 3 also runs the gated sweep");
    Check(g_apShotObjs[0] == &g_aShotRecs[51],
          "variant 3 takes its shot object from Map+0x108 + 4*51");
    EnvFreeCells();
}

static void TestXcomBaseStamping(void)
{
    printf("Map_BuildMissionTerrain: XCom base, the two tile-sprite stamping passes\n");

    EnvFixture_Reset();
    HookFixture_Reset();
    g_nMapLayers = 0;
    g_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    /* 0x15 is not one of the guard sentinels, and xbase_21.ma1 does not exist (the shipped set is
     * XBASE_0..XBASE_20), so no template contributes a sprite and every sprite in the grid comes
     * from the stamping passes. */
    memset(g_aTileTypes, 0x15, sizeof(g_aTileTypes));
    srand(7);

    Map_BuildMissionTerrain(&g_env);

    CheckEqI32(g_nDimX, 6, "the XCom base path forces Map_nMapDimX to 6");
    CheckEqI32(g_nDimY, 6, "and Map_nMapDimY to 6");
    CheckAllocatedGrid(6, 6, "the 6 x 6 base allocates all 14400 cells");

    /* Pass 1, row k = 0 is g_aTileSprIndex row 4 = {10, 0, 15, 0}: floor and wall B only, at
     * index 189 + 600*tileRow + 60*k + 10*tileCol. */
    {
        MapCell *pCell = Cell(189);
        Check(pCell->pFloorSpr == Spr(9), "pass 1 k=0 stamps the floor from tile row 4");
        Check(pCell->pWallSprA == NULL, "pass 1 k=0 leaves wall A empty");
        Check(pCell->pWallSprB == Spr(14), "pass 1 k=0 stamps wall B from tile row 4");
        Check(pCell->pCeilSpr == NULL, "pass 1 k=0 leaves the ceiling empty");
    }

    /* Pass 1, k = 3 is row 7 = {10, 16, 15, 4}: all four slots, and no wall-A clear on the neighbour
     * because the original only does that while its row counter is below 28. */
    {
        MapCell *pCell = Cell(189 + 60 * 3);
        Check(pCell->pFloorSpr == Spr(9), "pass 1 k=3 stamps the floor from tile row 7");
        Check(pCell->pWallSprA == Spr(15), "pass 1 k=3 stamps wall A from tile row 7");
        Check(pCell->pWallSprB == Spr(14), "pass 1 k=3 stamps wall B from tile row 7");
        Check(pCell->pCeilSpr == Spr(3), "pass 1 k=3 stamps the ceiling from tile row 7");
    }

    /* Pass 1 clears the wall A of the cell to the RIGHT while k < 3. The stamped column is
     * x = 9 + 10*tileCol, so the cell to its right is never itself stamped -- its floor stays empty
     * and its wall A is explicitly cleared. */
    {
        MapCell *pCell = Cell(190);
        Check(pCell->pFloorSpr == NULL, "pass 1 never stamps the column between its own");
        Check(pCell->pWallSprA == NULL, "pass 1 clears the wall A of the cell to its right");
    }

    Check(Cell(189 + 10 * 5)->pFloorSpr == NULL, "pass 1 stops one tile column short of the right");
    Check(Cell(189 + 600 * 5)->pFloorSpr == Spr(9), "pass 1 runs for every tile row");

    /* Pass 2, j = 3 is g_aTileSprIndex row 3 = {10, 16, 15, 4} at index 543 + 600*tileRow +
     * 10*tileCol + 3, and j == 0 stamps the ceiling of the band's base cell with sprite 0x14. */
    {
        MapCell *pCell = Cell(543 + 3);
        Check(pCell->pFloorSpr == Spr(9), "pass 2 j=3 stamps the floor from tile row 3");
        Check(pCell->pWallSprA == Spr(15), "pass 2 j=3 stamps wall A from tile row 3");
        Check(pCell->pWallSprB == Spr(14), "pass 2 j=3 stamps wall B from tile row 3");
        Check(pCell->pCeilSpr == Spr(3), "pass 2 j=3 stamps the ceiling from tile row 3");
        Check(Cell(543)->pCeilSpr == Spr(0x14),
              "pass 2 j=0 stamps the band base cell's ceiling with sprite 0x14");
        Check(Cell(543 + 60)->pWallSprB == NULL,
              "pass 2 j=0 clears the wall B of the cell below");
    }

    /* Pass 2 runs for tileRow 0..4 only, so the last row's band is untouched. */
    Check(Cell(543 + 600 * 5)->pFloorSpr == NULL,
          "pass 2 stops one tile row short of the bottom");

    EnvFreeCells();
}

static void TestXcomBaseTileGuards(void)
{
    MapCell *pA;
    MapCell *pB;

    printf("Map_BuildMissionTerrain: XCom base, the tile-type guards\n");

    /* Every case below starts from 0x15, whose template does not ship, so a stamped cell can only
     * mean the pass ran for that tile. */

    /* 0x14 blocks both passes. */
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nMapLayers = 0;
    g_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    memset(g_aTileTypes, 0x15, sizeof(g_aTileTypes));
    g_aTileTypes[0] = 0x14;              /* row 0, column 0 */
    srand(8);
    Map_BuildMissionTerrain(&g_env);
    pA = Cell(189);
    pB = Cell(543);
    Check(pA->pFloorSpr == NULL, "tile type 0x14 blocks pass 1");
    Check(pB->pFloorSpr == NULL, "tile type 0x14 blocks pass 2");
    Check(Cell(199)->pFloorSpr == Spr(9), "the guard only blocks its own tile");
    EnvFreeCells();

    /* 0x11 blocks pass 2 only. */
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nMapLayers = 0;
    g_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    memset(g_aTileTypes, 0x15, sizeof(g_aTileTypes));
    g_aTileTypes[0] = 0x11;
    srand(9);
    Map_BuildMissionTerrain(&g_env);
    Check(Cell(189)->pFloorSpr == Spr(9), "tile type 0x11 is NOT a pass 1 guard");
    Check(Cell(543)->pFloorSpr == NULL, "tile type 0x11 IS a pass 2 guard");
    EnvFreeCells();

    /* 0x12 blocks pass 1 only. */
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nMapLayers = 0;
    g_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    memset(g_aTileTypes, 0x15, sizeof(g_aTileTypes));
    g_aTileTypes[0] = 0x12;
    srand(10);
    Map_BuildMissionTerrain(&g_env);
    Check(Cell(189)->pFloorSpr == NULL, "tile type 0x12 IS a pass 1 guard");
    Check(Cell(543)->pFloorSpr == Spr(9), "tile type 0x12 is NOT a pass 2 guard");
    EnvFreeCells();

    /* 0x10 blocks both, like 0x14. */
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nMapLayers = 0;
    g_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    memset(g_aTileTypes, 0x15, sizeof(g_aTileTypes));
    g_aTileTypes[0] = 0x10;
    srand(11);
    Map_BuildMissionTerrain(&g_env);
    Check(Cell(189)->pFloorSpr == NULL, "tile type 0x10 IS a pass 1 guard");
    Check(Cell(543)->pFloorSpr == NULL, "tile type 0x10 IS a pass 2 guard");
    EnvFreeCells();

    /* The neighbour each pass looks at differs: pass 1 reads the tile one COLUMN right, pass 2 the
     * tile one ROW down. */
    EnvFixture_Reset();
    HookFixture_Reset();
    g_nMapLayers = 0;
    g_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    memset(g_aTileTypes, 0x15, sizeof(g_aTileTypes));
    g_aTileTypes[1] = 0x14;              /* row 0, column 1 */
    srand(12);
    Map_BuildMissionTerrain(&g_env);
    Check(Cell(189)->pFloorSpr == NULL,
          "pass 1 is blocked by the tile one column to the right");
    Check(Cell(543)->pFloorSpr == Spr(9),
          "pass 2 is not blocked by a tile to the right");
    EnvFreeCells();

    EnvFixture_Reset();
    HookFixture_Reset();
    g_nMapLayers = 0;
    g_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    memset(g_aTileTypes, 0x15, sizeof(g_aTileTypes));
    g_aTileTypes[6] = 0x14;              /* row 1, column 0 */
    srand(13);
    Map_BuildMissionTerrain(&g_env);
    Check(Cell(543)->pFloorSpr == NULL,
          "pass 2 is blocked by the tile one row below");
    Check(Cell(189)->pFloorSpr == Spr(9),
          "pass 1 is not blocked by a tile below");
    EnvFreeCells();
}

static void TestEmptyLayersFallback(void)
{
    MapTemplate probe = { 0 };
    i32 nRows;
    i32 nCols;
    i32 nLevels;
    i32 nBadIdx;
    bool bStamped;

    printf("Map_BuildMissionTerrain: the empty-layers fallback\n");

    /* The fallback's fixed block is <terrain> + "4", stamped at cell index
     * 3050 + 3600*level + 60*row + col, that is y = 50 + row, x = 50 + col. Probe the shipped
     * member so the expectation comes from the file rather than from a hard-coded number. */
    MapTemplate_Load(&probe, "jungle4", 2);
    nRows = probe.nRows;
    nCols = probe.nCols;
    nLevels = probe.nLevels;
    MapTemplate_Free(&probe);
    printf("  info jungle4.ma1: %d rows x %d cols x %d levels\n", nRows, nCols, nLevels);

    /* The fallback's fixed block sits at y 50..59, which only tile row 5 covers, and tiles (3,5),
     * (4,5) and (5,5) are grid-marked so no random module is placed there. That argument only holds
     * while a jungle module is one tile wide and tall, so print the shipped extents. */
    for (int i = 0; i < 12; ++i) {
        char szMod[16];
        MapTemplate t = { 0 };

        snprintf(szMod, sizeof szMod, "jungle%d", i);
        if (MapTemplate_Load(&t, szMod, 2)) {
            printf("  info %s.ma1: %d x %d x %d\n", szMod, t.nRows, t.nCols, t.nLevels);
        } else {
            printf("  info %s.ma1: MISSING\n", szMod);
        }
        MapTemplate_Free(&t);
    }

    EnvFixture_Reset();
    HookFixture_Reset();
    g_nMapLayers = 0;
    g_nMissionType = 0;
    srand(14);
    Map_BuildMissionTerrain(&g_env);

    CheckEqI32(g_nDimX, 6, "the fallback forces Map_nMapDimX to 6");
    CheckEqI32(g_nDimY, 6, "and Map_nMapDimY to 6");
    CheckAllocatedGrid(6, 6, "the fallback allocates all 14400 cells");
    Check(g_nSpawnX >= 0 && g_nSpawnX <= 4 && g_nSpawnY >= 0 && g_nSpawnY <= 3,
          "the random module tile is rand(5) x rand(4)");

    if (nLevels <= 3 && nRows > 0 && nCols > 0) {
        bStamped = true;
        nBadIdx = -1;
        /* The original runs the level loop from 3 down to the template's own level count. */
        for (i32 nLevel = 3; nLevel >= nLevels; --nLevel) {
            for (i32 nRow = 0; nRow < nRows; ++nRow) {
                for (i32 nCol = 0; nCol < nCols; ++nCol) {
                    i32     nIdx  = 3050 + 3600 * nLevel + 60 * nRow + nCol;
                    MapCell *pCell = Cell(nIdx);
                    if (pCell->pFloorSpr != Spr(0x3F) || pCell->pCeilSpr != Spr(0x3E)) {
                        bStamped = false;
                        if (nBadIdx < 0) {
                            nBadIdx = nIdx;
                        }
                    }
                }
            }
        }
        if (nBadIdx >= 0) {
            printf("  info first bad cell: idx %d (y %d, x %d, layer %d), floor %d, ceil %d\n",
                   nBadIdx, nBadIdx / 60 % 60, nBadIdx % 60, nBadIdx / 3600,
                   Cell(nBadIdx)->pFloorSpr ? (int)Cell(nBadIdx)->pFloorSpr->nSlot : -1,
                   Cell(nBadIdx)->pCeilSpr ? (int)Cell(nBadIdx)->pCeilSpr->nSlot : -1);
        }
        Check(bStamped, "the fixed block stamps 0x3F floor and 0x3E ceiling at y=50+row, x=50+col");
        Check(Cell(3050 + 10800 - 10)->pFloorSpr == Spr(0x3F)
              && Cell(3050 + 10800 - 20)->pFloorSpr == Spr(0x3F),
              "the same stamps repeat at x-10 and x-20");
    } else {
        Check(false, "jungle4.ma1 did not load in a form the fallback can stamp");
    }
    EnvFreeCells();
}

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }

    FontFixture_Reset();

    TestNormalPathAllocatesTheGrid();
    TestSmallerMapLeavesTheRestUntouched();
    TestTierAndShots();
    TestXcomBaseStamping();
    TestXcomBaseTileGuards();
    TestEmptyLayersFallback();

    Asset_CloseArchives();

    if (g_nFailures != 0) {
        printf("\n%d check(s) FAILED\n", g_nFailures);
        return 1;
    }
    printf("\nall terrain checks passed\n");
    return 0;
}
