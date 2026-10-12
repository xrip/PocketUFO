/* test_template.c - checks the .ma1 mission template reader.
 *
 * Ground truth was measured independently from the shipped main.dat: all 159
 * .ma1 members satisfy size == 3 + 4 * dimX * dimY * levels, and the header bytes
 * are dimX, dimY, levels. Specific files used below:
 *   AVENGER.MA1 2403 bytes, header 20 x 10 x 3
 *   CULTA0.MA1   403 bytes, header 10 x 10 x 1
 */
#include "asset/pufo_asset.h"
#include "game/pufo_template.h"

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

/* Destination grid. Must be large enough to hold AVENGER.MA1 (20 x 10 x 3) so
 * the traversal test can assert the full 600-cell walk; a separate case below
 * drives the clipping path with an offset that overflows the grid. */
#define T_DIM 24

typedef struct TplGrid {
    MapCell aCells[PUFO_MAP_LAYERS][T_DIM][T_DIM];
    i32     nVisited;
} TplGrid;

static MapCell *TplGrid_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    TplGrid *pG = (TplGrid *)pUser;

    if (nX < 0 || nY < 0 || nX >= T_DIM || nY >= T_DIM) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    pG->nVisited += 1;
    return &pG->aCells[nLayer][nY][nX];
}

static void TplGrid_Init(TplGrid *pG)
{
    memset(pG, 0, sizeof(*pG));
    for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (i32 y = 0; y < T_DIM; ++y) {
            for (i32 x = 0; x < T_DIM; ++x) {
                MapCell_Init(&pG->aCells[z][y][x], x, y, z);
                /* Seed the flags so MapCell_SetTileSprites is observable:
                 * it only CLEARS bits, so a zero seed would stay zero and the
                 * test could not tell whether it ran. 0xFFFFFFFF makes the mask
                 * visible as 0xFFFFFF80. */
                pG->aCells[z][y][x].nFlags32 = 0xFFFFFFFFu;
            }
        }
    }
}

static Mc1Rec g_aSprites[64];

static Mc1Rec *TplGrid_GetSprite(u8 nValue, void *pUser)
{
    (void)pUser;
    return &g_aSprites[nValue % 64];
}

static void TestLoad(void)
{
    MapTemplate tpl;

    printf("MapTemplate_Load\n");
    memset(&tpl, 0, sizeof(tpl));
    Check(MapTemplate_Load(&tpl, "avenger", 1), "  AVENGER.MA1 loaded");
    CheckEqI32(tpl.nDimX, 20, "  dimX 20");
    CheckEqI32(tpl.nDimY, 10, "  dimY 10");
    CheckEqI32(tpl.nLevels, 3, "  levels 3");
    CheckEqI32(tpl.nFlag, 1, "  flag stored");
    /* 3 + 4*20*10*3 == 2403, the shipped size. */
    CheckEqI32((i32)tpl.cbBlob, 2403, "  blob size matches 3 + 4*dimX*dimY*levels");
    CheckEqI32((i32)tpl.cbBlob,
               PUFO_MA1_HDR + PUFO_MA1_CELL_BYTES * tpl.nDimX * tpl.nDimY * tpl.nLevels,
               "  blob size matches the computed geometry");
    MapTemplate_Free(&tpl);
    Check(tpl.pBlob == NULL, "  freed");

    printf("MapTemplate_Load (single level)\n");
    Check(MapTemplate_Load(&tpl, "culta0", 2), "  CULTA0.MA1 loaded");
    CheckEqI32(tpl.nDimX, 10, "  dimX 10");
    CheckEqI32(tpl.nLevels, 1, "  levels 1");
    CheckEqI32((i32)tpl.cbBlob, 403, "  blob size 403");
    MapTemplate_Free(&tpl);

    printf("MapTemplate_Load (missing)\n");
    Check(!MapTemplate_Load(&tpl, "no_such_template", 0), "  missing member fails");
    Check(tpl.pBlob == NULL, "  blob stays NULL");

    /* Re-loading must free the previous blob, as the original does. */
    printf("MapTemplate_Load (reload frees the previous blob)\n");
    Check(MapTemplate_Load(&tpl, "avenger", 0), "  first load");
    u8 *pFirst = tpl.pBlob;
    Check(MapTemplate_Load(&tpl, "culta0", 0), "  second load");
    Check(tpl.pBlob != pFirst, "  pointer changed to the new blob");
    CheckEqI32((i32)tpl.cbBlob, 403, "  new size in effect");
    MapTemplate_Free(&tpl);
}

static void TestCellByte(void)
{
    MapTemplate tpl;
    i32         nNonZero = 0;
    u32         x;
    u32         y;
    u32         z;

    printf("MapTemplate_GetCellByte\n");
    Check(MapTemplate_Load(&tpl, "avenger", 0), "  template loaded");
    if (tpl.pBlob == NULL) {
        return;
    }

    /* The accessor must be total over the template's own extent without
     * overrunning the blob. Walk every cell and every slot. */
    for (z = 0; z < tpl.nLevels; ++z) {
        for (y = 0; y < tpl.nDimY; ++y) {
            for (x = 0; x < tpl.nDimX; ++x) {
                for (u32 s = 0; s < PUFO_MA1_CELL_BYTES; ++s) {
                    if (MapTemplate_GetCellByte(&tpl, x, y, z, s) != 0) {
                        nNonZero += 1;
                    }
                }
            }
        }
    }
    Check(nNonZero > 0, "  the template holds non-zero slot data");
    printf("       (%d non-zero slot bytes across %u cells)\n",
           (int)nNonZero, (unsigned)(tpl.nDimX * tpl.nDimY * tpl.nLevels));

    /* Out of range must be safe, not an overread. */
    CheckEqI32(MapTemplate_GetCellByte(&tpl, tpl.nDimX, 0, 0, 0), 0, "  x out of range");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, tpl.nDimY, 0, 0), 0, "  y out of range");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, tpl.nLevels, 0), 0, "  z out of range");
    CheckEqI32(MapTemplate_GetLevelCount(&tpl), tpl.nLevels,
               "  GetLevelCount returns header byte 2");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, 0, PUFO_MA1_CELL_BYTES), 0, "  slot out of range");

    /* The last cell must be inside the blob: 4*(dimX*dimY*(levels-1) + ...) */
    {
        u32 nLast = PUFO_MA1_CELL_BYTES
                  * ((u32)tpl.nDimX * tpl.nDimY * (tpl.nLevels - 1)
                     + (u32)tpl.nDimY * (tpl.nDimY - 1) + (tpl.nDimX - 1))
                  + 3 + PUFO_MA1_HDR;
        Check(nLast <= tpl.cbBlob, "  last cell index is inside the blob");
    }
    MapTemplate_Free(&tpl);
}

/* The sprite-index rule: 0 and 1 mean "no sprite", 2 and up are real. */
static void TestSpriteIndexRule(void)
{
    MapTemplate tpl;

    printf("MapTemplate_GetSpriteIndex (0 and 1 mean no sprite)\n");
    memset(&tpl, 0, sizeof(tpl));
    {
        /* A hand-built 2x1x1 template: cell 0 slots {0,1,2,3}, cell 1 slots all 4. */
        static u8 s_aBlob[PUFO_MA1_HDR + PUFO_MA1_CELL_BYTES * 2] = {
            2, 1, 1,          /* dimX 2, dimY 1, levels 1 */
            0, 1, 2, 3,
            2, 2, 2, 2
        };

        tpl.pBlob   = s_aBlob;
        tpl.cbBlob  = sizeof(s_aBlob);
        tpl.nDimX   = 2;
        tpl.nDimY   = 1;
        tpl.nLevels = 1;
    }
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, 0, PUFO_MA1_SLOT_FLOOR), 0, "  raw slot 0 is 0");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, 0, PUFO_MA1_SLOT_WALL_A), 1, "  raw slot 1 is 1");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, 0, PUFO_MA1_SLOT_WALL_B), 2, "  raw slot 2 is 2");
    /* 0 and 1 both resolve to "no sprite". */
    CheckEqI32(MapTemplate_GetSpriteIndex(&tpl, 0, 0, 0, PUFO_MA1_SLOT_FLOOR), 0, "  slot value 0 -> no sprite");
    CheckEqI32(MapTemplate_GetSpriteIndex(&tpl, 0, 0, 0, PUFO_MA1_SLOT_WALL_A), 0, "  slot value 1 -> no sprite");
    CheckEqI32(MapTemplate_GetSpriteIndex(&tpl, 0, 0, 0, PUFO_MA1_SLOT_WALL_B), 2, "  slot value 2 -> sprite 2");
    CheckEqI32(MapTemplate_GetSpriteIndex(&tpl, 0, 0, 0, PUFO_MA1_SLOT_CEIL), 3, "  slot value 3 -> sprite 3");
    CheckEqI32(MapTemplate_GetSpriteIndex(&tpl, 1, 0, 0, PUFO_MA1_SLOT_FLOOR), 2, "  second cell floor -> sprite 2");
}

/* ApplyToCells must visit every template cell once, at the right coordinates. */
static void TestApply(void)
{
    MapTemplate tpl;
    TplGrid     grid;

    printf("MapTemplate_ApplyToCells\n");
    Check(MapTemplate_Load(&tpl, "avenger", 0), "  template loaded");
    if (tpl.pBlob == NULL) {
        return;
    }
    TplGrid_Init(&grid);

    MapTemplate_ApplyToCells(&tpl, TplGrid_Lookup, &grid, 0, 0,
                             TplGrid_GetSprite, NULL);
    /* dimX * dimY * levels lookups, 20*10*3 = 600; the grid is 24 wide so nothing
     * is clipped here. */
    CheckEqI32(grid.nVisited, tpl.nDimX * tpl.nDimY * tpl.nLevels,
               "  visited every template cell once");

    /* Every visited cell must have had SetTileSprites applied, which masks
     * nFlags32's low 7 bits (the grid seeds 0xFFFFFFFF, see TplGrid_Init). */
    CheckEqI32((i32)grid.aCells[1][2][3].nFlags32, (i32)0xFFFFFF80u,
               "  destination cell had SetTileSprites applied");

    /* An offset shifts the landing area. The first template cell (0,0,0) lands
     * on layer 0 at (1,1), so the marker must appear at aCells[0][1][1]. */
    printf("MapTemplate_ApplyToCells with an offset\n");
    TplGrid_Init(&grid);
    MapTemplate_ApplyToCells(&tpl, TplGrid_Lookup, &grid, 1, 1,
                             TplGrid_GetSprite, NULL);
    CheckEqI32((i32)grid.aCells[0][1][1].nFlags32, (i32)0xFFFFFF80u,
               "  offset cell 0,0 landed at layer 0, (1,1)");
    /* Every template cell shifts by the same offset. */
    CheckEqI32((i32)grid.aCells[0][1 + tpl.nDimY - 1][1 + tpl.nDimX - 1].nFlags32,
               (i32)0xFFFFFF80u, "  far template corner shifted with the offset");

    /* A template offset past the grid edge must be clipped, not write past it. */
    printf("MapTemplate_ApplyToCells clipped by the grid\n");
    TplGrid_Init(&grid);
    MapTemplate_ApplyToCells(&tpl, TplGrid_Lookup, &grid, T_DIM - 2, T_DIM - 2,
                             TplGrid_GetSprite, NULL);
    CheckEqI32((i32)grid.aCells[0][T_DIM - 2][T_DIM - 2].nFlags32, (i32)0xFFFFFF80u,
               "  overlapping corner still stamped");
    Check(true, "  offset past the edge did not fault");
    MapTemplate_Free(&tpl);
}

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }

    TestLoad();
    TestCellByte();
    TestSpriteIndexRule();
    TestApply();

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
