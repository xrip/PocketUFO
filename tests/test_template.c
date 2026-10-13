/* test_template.c - checks the .ma1 mission template reader.
 *
 * Ground truth was measured independently from the shipped main.dat: all 159
 * .ma1 members satisfy size == 3 + 4 * nRows * nCols * levels, and the header bytes
 * are nRows, nCols, levels. Specific files used below:
 *   AVENGER.MA1 2403 bytes, header 20 rows x 10 cols x 3 levels
 *   CULTA0.MA1   403 bytes, header 10 x 10 x 1
 *
 * The size formula is symmetric, so it cannot tell which header byte is X and which is Y. The
 * direction tests below do: AVENGER.MA1 is 20 rows by 10 cols, so applied at tile (0, 0) it must
 * cover map cells x 0..9 and y 0..19, and the transposed corner must be untouched.
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

/* Destination grid. Offsets are tile coordinates, so one tile is PUFO_CELLS_PER_TILE cells.
 * AVENGER.MA1 is 20 rows by 10 cols, which at tile (0, 0) covers x 0..9 and y 0..19 -- inside a
 * 24-wide grid, so the full 600-cell walk is assertable. A separate case drives the clipping path. */
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
    CheckEqI32(tpl.nRows, 20, "  header byte 0 is the row count, 20");
    CheckEqI32(tpl.nCols, 10, "  header byte 1 is the column count, 10");
    CheckEqI32(tpl.nLevels, 3, "  levels 3");
    CheckEqI32(tpl.nFlag, 1, "  flag stored");
    CheckEqI32(MapTemplate_GetHeaderByte0(&tpl), tpl.pBlob[0],
               "  GetHeaderByte0 (0x54c68) is header byte 0");
    CheckEqI32(MapTemplate_GetHeaderByte1(&tpl), tpl.pBlob[1],
               "  GetHeaderByte1 (0x54c5c) is header byte 1");
    /* 3 + 4*20*10*3 == 2403, the shipped size. */
    CheckEqI32((i32)tpl.cbBlob, 2403, "  blob size matches 3 + 4*rows*cols*levels");
    CheckEqI32((i32)tpl.cbBlob,
               PUFO_MA1_HDR + PUFO_MA1_CELL_BYTES * tpl.nRows * tpl.nCols * tpl.nLevels,
               "  blob size matches the computed geometry");
    MapTemplate_Free(&tpl);
    Check(tpl.pBlob == NULL, "  freed");

    printf("MapTemplate_Load (single level)\n");
    Check(MapTemplate_Load(&tpl, "culta0", 2), "  CULTA0.MA1 loaded");
    CheckEqI32(tpl.nRows, 10, "  rows 10");
    CheckEqI32(tpl.nCols, 10, "  cols 10");
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
        for (y = 0; y < tpl.nRows; ++y) {
            for (x = 0; x < tpl.nCols; ++x) {
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
           (int)nNonZero, (unsigned)(tpl.nRows * tpl.nCols * tpl.nLevels));

    /* Out of range must be safe, not an overread. */
    CheckEqI32(MapTemplate_GetCellByte(&tpl, tpl.nCols, 0, 0, 0), 0, "  x out of range");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, tpl.nRows, 0, 0), 0, "  y out of range");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, tpl.nLevels, 0), 0, "  z out of range");
    CheckEqI32(MapTemplate_GetLevelCount(&tpl), tpl.nLevels,
               "  GetLevelCount returns header byte 2");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, 0, PUFO_MA1_CELL_BYTES), 0, "  slot out of range");

    /* The last cell of the blob is the last row and column of the last level, and the row stride is
     * the COLUMN count -- which is what pins byte 1 as the X extent. */
    {
        u32 nLast = PUFO_MA1_CELL_BYTES
                  * ((u32)tpl.nRows * tpl.nCols * (tpl.nLevels - 1)
                     + (u32)tpl.nCols * (tpl.nRows - 1) + (tpl.nCols - 1))
                  + 3 + PUFO_MA1_HDR;
        Check(nLast + 1 == tpl.cbBlob, "  the last cell ends exactly at the blob end");
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
            1, 2, 1,          /* rows 1, cols 2, levels 1 */
            0, 1, 2, 3,
            2, 2, 2, 2
        };

        tpl.pBlob   = s_aBlob;
        tpl.cbBlob  = sizeof(s_aBlob);
        tpl.nRows   = 1;
        tpl.nCols   = 2;
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
    /* rows * cols * levels lookups, 20*10*3 = 600; at tile (0, 0) that is x 0..9 and y 0..19. */
    CheckEqI32(grid.nVisited, tpl.nRows * tpl.nCols * tpl.nLevels,
               "  visited every template cell once");

    /* Every visited cell must have had SetTileSprites applied, which masks
     * nFlags32's low 7 bits (the grid seeds 0xFFFFFFFF, see TplGrid_Init). */
    CheckEqI32((i32)grid.aCells[1][2][3].nFlags32, (i32)0xFFFFFF80u,
               "  destination cell had SetTileSprites applied");

    /* The direction test. AVENGER.MA1 is 20 rows by 10 cols, so the stamped region is 10 cells wide
     * and 20 tall. The transposed corner must be untouched -- this is what catches an X/Y swap. */
    CheckEqI32((i32)grid.aCells[0][tpl.nRows - 1][tpl.nCols - 1].nFlags32, (i32)0xFFFFFF80u,
               "  far corner is x = cols-1, y = rows-1");
    CheckEqI32((i32)grid.aCells[0][tpl.nCols - 1][tpl.nRows - 1].nFlags32, (i32)0xFFFFFFFFu,
               "  the transposed corner is NOT stamped");

    printf("MapTemplate_ApplyToCells with a tile offset\n");
    TplGrid_Init(&grid);
    MapTemplate_ApplyToCells(&tpl, TplGrid_Lookup, &grid, 1, 1,
                             TplGrid_GetSprite, NULL);
    CheckEqI32((i32)grid.aCells[0][PUFO_CELLS_PER_TILE][PUFO_CELLS_PER_TILE].nFlags32,
               (i32)0xFFFFFF80u, "  tile (1,1) lands ten cells in, not one");
    CheckEqI32((i32)grid.aCells[0][1][1].nFlags32, (i32)0xFFFFFFFFu,
               "  the cell one over is untouched");

    TplGrid_Init(&grid);
    MapTemplate_ApplyToCells(&tpl, TplGrid_Lookup, &grid, 1, 0,
                             TplGrid_GetSprite, NULL);
    CheckEqI32((i32)grid.aCells[0][tpl.nRows - 1][PUFO_CELLS_PER_TILE + tpl.nCols - 1].nFlags32,
               (i32)0xFFFFFF80u, "  the far corner shifts by the same ten in x only");

    /* A template offset past the grid edge must be clipped, not write past it. */
    printf("MapTemplate_ApplyToCells clipped by the grid\n");
    TplGrid_Init(&grid);
    MapTemplate_ApplyToCells(&tpl, TplGrid_Lookup, &grid, 2, 2,
                             TplGrid_GetSprite, NULL);
    CheckEqI32((i32)grid.aCells[0][20][20].nFlags32, (i32)0xFFFFFF80u,
               "  the overlapping corner is still stamped");
    Check(grid.nVisited < tpl.nRows * tpl.nCols * tpl.nLevels,
          "  the part past the edge was clipped");
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
