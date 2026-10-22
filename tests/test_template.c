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
    /* CDialogTemplate_GetTemplateByte0 @ 0x54c68 is `LDR R3,[R0]` + `LDRB R0,[R3]` (the load is at
     * 0x54C70): header byte 0 read through the template pointer, not from a cached field.
     * CDialogTemplate_GetTemplateByte1 @ 0x54c5c is the same with `LDRB R0,[R3,#1]` at 0x54C64. The
     * port caches both on load, so the check is against the blob byte itself. */
    CheckEqI32(MapTemplate_GetHeaderByte0(&tpl), tpl.pBlob[0],
               "  GetHeaderByte0 reads byte 0 (0x54c68 -> 0x54C70)");
    CheckEqI32(MapTemplate_GetHeaderByte1(&tpl), tpl.pBlob[1],
               "  GetHeaderByte1 reads byte 1 (0x54c5c -> 0x54C64)");
    /* 3 + 4*20*10*3 == 2403, the shipped size. */
    CheckEqI32((i32)tpl.cbBlob, 2403, "  blob size matches 3 + 4*rows*cols*levels");
    CheckEqI32((i32)tpl.cbBlob,
               PUFO_MA1_HDR + PUFO_MA1_CELL_BYTES * tpl.nRows * tpl.nCols * tpl.nLevels,
               "  blob size matches the computed geometry");
    /* ??1CDialogTemplate@@QAA@XZ @ 0x54c48 is the dtor: it loads pTemplate at 0x54C4C and calls
     * operator delete only when it is non-null (the guard and the call share 0x54C54). A second free
     * of an already-freed template is therefore safe in the original for the same reason it is safe
     * here -- the port's free(NULL) is a no-op -- and that is what the next two checks pin. */
    MapTemplate_Free(&tpl);
    Check(tpl.pBlob == NULL, "  freed");
    MapTemplate_Free(&tpl);
    Check(tpl.pBlob == NULL, "  freeing an already-freed template is safe (the 0x54c48 guard)");

    printf("MapTemplate_Load (single level)\n");
    Check(MapTemplate_Load(&tpl, "culta0", 2), "  CULTA0.MA1 loaded");
    CheckEqI32(tpl.nRows, 10, "  rows 10");
    CheckEqI32(tpl.nCols, 10, "  cols 10");
    CheckEqI32(tpl.nLevels, 1, "  levels 1");
    CheckEqI32((i32)tpl.cbBlob, 403, "  blob size 403");
    MapTemplate_Free(&tpl);

    printf("MapTemplate_Load (missing)\n");
    /* ??0CDialogTemplate@@QAA@XZ @ 0x54c3c is 12 bytes with one store: `pTemplate = 0` at 0x54C40.
     * MapTemplate_Load reproduces it with its leading MapTemplate_Free (pufo_template.c), which is also
     * what Map_LoadMa1's own guard-and-delete at 0x54BC8 does before it allocates. So a load that
     * fails must leave a NULL blob -- the state the ctor+dtor pair leaves. */
    Check(!MapTemplate_Load(&tpl, "no_such_template", 0), "  missing member fails");
    Check(tpl.pBlob == NULL, "  blob stays NULL (the ctor's one store, 0x54C40)");

    /* Where the port differs on purpose: Map_LoadMa1 stores the flag byte at pOut+4 at 0x54BD0, BEFORE
     * the name copy, AssetReader_Find at 0x54C04 and the read at 0x54C34, so in the original a failed
     * load still records the flag -- and its Find result is never tested, so a miss answers with a
     * 0-byte `operator new` (0x54C14) and AssetReader_Read's refusal value. The port stores the flag
     * only when the load worked. A 12-character base name is the other half of that: the original
     * copies into a 16-byte pszName[16] by an unbounded loop (0x54BDC..0x54BEC) and then strcat's
     * ".ma1" at 0x54BF8, writing 17 bytes; the port bounds it. */
    Check(MapTemplate_Load(&tpl, "culta0", 5), "  loaded with flag 5");
    Check(!MapTemplate_Load(&tpl, "no_such_template", 9), "  a failed load is refused");
    CheckEqI32(tpl.nFlag, 5, "  the failed load did not write the flag (the original stores it at 0x54BD0)");
    MapTemplate_Free(&tpl);

    /* Re-loading must free the previous blob, as the original does -- MapTemplate_Load calls MapTemplate_Free
     * before it allocates (pufo_template.c:22). This used to assert that the new blob lands at a DIFFERENT address
     * from the one just freed, and that is not the port's behaviour to guarantee: a heap may hand back the block it
     * just released, which is why the check read red in Debug and green in Release over five runs of a binary that
     * had not changed. What the free does guarantee is visible in a load that fails: the previous blob is gone. */
    printf("MapTemplate_Load (reload frees the previous blob)\n");
    Check(MapTemplate_Load(&tpl, "avenger", 0), "  first load");
    Check(MapTemplate_Load(&tpl, "culta0", 0), "  second load");
    CheckEqI32((i32)tpl.cbBlob, 403, "  new size in effect");
    Check(!MapTemplate_Load(&tpl, "no_such_template", 0), "  a load of a missing member fails");
    Check(tpl.pBlob == NULL, "  and that load left the previous blob freed");
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

    /* CDialogTemplate_GetLevelCount @ 0x54c74 is 12 bytes: `LDR R3,[R0]` then `LDRB R0,[R3,#2]`, the
     * load at 0x54C7C -- header byte 2, nothing else. AVENGER.MA1 stores 3 there. */
    CheckEqI32(MapTemplate_GetLevelCount(&tpl), 3, "  GetLevelCount is header byte 2, literal 3 (0x54c74)");
    CheckEqI32(MapTemplate_GetLevelCount(&tpl), tpl.pBlob[2], "  and it is the byte the loader read");

    /* Out of range must be safe, not an overread. CDialogTemplate_GetCellByte @ 0x54c80 has no bound
     * check at all, so these four are port behaviour standing in for undefined original behaviour. */
    CheckEqI32(MapTemplate_GetCellByte(&tpl, tpl.nCols, 0, 0, 0), 0, "  x out of range");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, tpl.nRows, 0, 0), 0, "  y out of range");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, tpl.nLevels, 0), 0, "  z out of range");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, 0, PUFO_MA1_CELL_BYTES), 0, "  slot out of range");

    /* The original truncates every index to eight bits (AND #0xFF at 0x54CB0..0x54CD0), so a caller
     * that passed 259 reached cell 3. The port refuses instead -- 259 is past byte1 = 10 -- and that
     * refusal is what distinguishes "masked" from "range-checked" here. */
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 259, 0, 0, 0), 0, "  index 259 is refused, not masked to 3");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 3, 0, 0, 0), tpl.pBlob[15],
               "  (and cell 3 itself is readable, so the refusal above is about the index)");

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

/* The strides, pinned with a blob whose every byte is different.
 *
 * The shipped templates are sparse -- only 117 of AVENGER.MA1's 2400 slot bytes are non-zero -- so a
 * check that compares the accessor against the blob byte at a guessed offset can be satisfied by two
 * zeros. Here the blob is built as blob[i] = (u8)(i + 1), so each offset I want to prove has a value
 * no other offset has: 3 -> 4, 43 -> 44, 82 -> 83, 805 -> 38, 763 -> 252, 2402 -> 99.
 *
 * 0x54c80's arithmetic is 4*(byte0*byte1*nZ) + 4*(byte1*nY) + 4*nX + nSlot + 3 (MUL at 0x54CB8,
 * 0x54CC0 and 0x54CCC, ADD at 0x54CD4 and 0x54CD8, `LDRB R0,[R3,#3]` at 0x54CEC), so with byte0 = 20
 * and byte1 = 10 one row forward is 4*10 = 40 bytes and one level forward is 4*20*10 = 800. Using
 * byte0 for the row stride instead would land on 83 -> 84, and using byte1 for the level stride would
 * land on 43 -> 44 for z = 1; both are checked below as explicit negatives. */
#define TPLF_ROWS  20
#define TPLF_COLS  10
#define TPLF_LEVEL 3
#define TPLF_CB    (3 + 4 * TPLF_ROWS * TPLF_COLS * TPLF_LEVEL)

static u8 g_aTplFill[TPLF_CB];

static void TplFillInit(MapTemplate *pTpl)
{
    u32 i;

    for (i = 0; i < TPLF_CB; ++i) {
        g_aTplFill[i] = (u8)(i + 1u);
    }
    pTpl->pBlob   = g_aTplFill;
    pTpl->cbBlob  = TPLF_CB;
    pTpl->nRows   = TPLF_ROWS;
    pTpl->nCols   = TPLF_COLS;
    pTpl->nLevels = TPLF_LEVEL;
}

static void TestCellIndexArithmetic(void)
{
    MapTemplate tpl;

    printf("MapTemplate_GetCellByte @ 0x54c80 -- the strides, on a blob with distinct bytes\n");
    memset(&tpl, 0, sizeof(tpl));
    TplFillInit(&tpl);

    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, 0, 0), 4, "  cell (0,0,0) slot 0 is blob[3]");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 1, 0, 0), 44, "  one row forward is 40 bytes: blob[43]");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 9, 1, 0, 3), 83, "  the row's last cell/slot is blob[82]");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 19, 0, 0), 252, "  the last row is 19 * 40 in: blob[763]");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, 1, 2), 38, "  one level forward is 800 bytes: blob[805]");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 9, 19, 2, 3), 99, "  the last byte of the blob is the last slot: blob[2402]");

    /* The two wrong strides, stated as negatives so a swap fails here and not only above. */
    Check(MapTemplate_GetCellByte(&tpl, 0, 1, 0, 0) != 84, "  the row stride is NOT 4 * byte0 = 80");
    Check(MapTemplate_GetCellByte(&tpl, 0, 0, 1, 0) != 44, "  the level stride is NOT 4 * byte1 = 40");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 0, 0, 0, 4), 0, "  slot 4 is refused (4 bytes per cell)");
    CheckEqI32(MapTemplate_GetCellByte(&tpl, 10, 0, 0, 0), 0, "  x = 10 is refused, so the row cannot run into the next");
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
    TestCellIndexArithmetic();
    TestSpriteIndexRule();
    TestApply();

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
