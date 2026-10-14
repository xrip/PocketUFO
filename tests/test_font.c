/* test_font.c - checks the font layer against the shipped FONT.SPR / FONTSIZE.DAT.
 *
 * The RLE glyph format and the advance table were decoded independently from
 * lang.dat (see docs/parity/game-core.md), so a mismatch here means the ported
 * decoder is wrong.
 */
#include "asset/pufo_asset.h"
#include "game/pufo_font.h"
#include "game/pufo_mapctx.h"   /* PUFO_MAP_CELL_DIM */
#include "render/pufo_render.h"

#include <stdio.h>
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

static void CheckEqU32(u32 nGot, u32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%u)\n", pszWhat, (unsigned)nGot);
    } else {
        printf("  FAIL %s: got %u, want %u\n", pszWhat, (unsigned)nGot, (unsigned)nWant);
        g_nFailures += 1;
    }
}

/* .spr header: count then an offset table relative to the member start. */
static void TestSprHeader(void)
{
    AssetReader rdr;
    u8         *pSpr;
    u32         cb;
    u16         nCount;
    u16         anOff[256];

    printf("FONT.SPR header\n");
    AssetReader_Clear(&rdr);
    Check(AssetReader_Find(&rdr, "FONT.SPR"), "  FONT.SPR found");
    cb = AssetReader_Size(&rdr);
    pSpr = malloc(cb);
    if (pSpr == NULL) {
        g_nFailures += 1;
        return;
    }
    Check(AssetReader_Read(&rdr, pSpr, cb), "  readable");
    nCount = Spr_ParseHeader(pSpr, cb, anOff, 256);
    /* The shipped file starts with u16 256 and first offset 514 == 2 + 256*2. */
    CheckEqU32(nCount, 256, "  glyph count");
    CheckEqU32(anOff[0], 514, "  first offset == 2 + 256*2");
    Check(anOff[1] > anOff[0], "  offsets ascend");
    /* Every offset must land inside the member. */
    {
        bool bAllInside = true;

        for (u16 i = 0; i < nCount; ++i) {
            if (anOff[i] >= cb) {
                bAllInside = false;
            }
        }
        Check(bAllInside, "  all offsets inside the member");
    }
    free(pSpr);
}

/* Decode the RLE stream the same way the blitter does, but with no surface, and
 * compare against the counts decoded independently from the file. */
static void TestGlyphDecode(void)
{
    AssetReader rdr;
    u8         *pSpr;
    u32         cb;
    u16         anOff[256];
    u16         nCount;

    printf("Glyph RLE decode (char 65 'A')\n");
    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, "FONT.SPR")) {
        g_nFailures += 1;
        return;
    }
    cb = AssetReader_Size(&rdr);
    pSpr = malloc(cb);
    if (pSpr == NULL || !AssetReader_Read(&rdr, pSpr, cb)) {
        g_nFailures += 1;
        free(pSpr);
        return;
    }
    nCount = Spr_ParseHeader(pSpr, cb, anOff, 256);

    {
        const u8 *pG = pSpr + anOff[65];
        /* Independently measured: startX 3, startY 3, 10 rows, 68 pixels,
         * first pixel (3,3,idx27), last pixel (11,12,idx27). */
        i32 nPenX = pG[1];
        i32 nPenY = pG[0];
        i32 nRows = 1;
        i32 nPixels = 0;
        i32 anFirst[3] = { -1, -1, -1 };
        i32 anLast[3] = { -1, -1, -1 };
        const u8 *pCmd = pG + 1;

        for (;;) {
            u8 nRun = *++pCmd;

            if (nRun == 0xFF) {
                break;
            }
            if (nRun == 0xFE) {
                ++nPenY;
                nPenX = (i32)(*++pCmd) + pG[1];
                ++nRows;
                continue;
            }
            if (anFirst[0] < 0) {
                anFirst[0] = nPenX; anFirst[1] = nPenY; anFirst[2] = nRun;
            }
            anLast[0] = nPenX; anLast[1] = nPenY; anLast[2] = nRun;
            ++nPixels;
            ++nPenX;
        }

        CheckEqI32(pG[1], 3, "  start X");
        CheckEqI32(pG[0], 3, "  start Y");
        CheckEqI32(nRows, 10, "  row count");
        CheckEqI32(nPixels, 68, "  pixel count");
        Check(anFirst[0] == 3 && anFirst[1] == 3 && anFirst[2] == 27, "  first pixel (3,3,idx27)");
        Check(anLast[0] == 11 && anLast[1] == 12 && anLast[2] == 27, "  last pixel (11,12,idx27)");
    }
    (void)nCount;
    free(pSpr);
}

/* The blitter must place pixels exactly where the decoder says. */
static void TestBlitIntoSurface(void)
{
    SoftSurf surf;
    u16     *pPix;
    u8       aGlyph[16];
    i32      i;

    printf("Font_BlitGlyph into a surface\n");
    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 8, 8, PIXMODE_16BPP);
    pPix = (u16 *)SoftSurf_GetPixels(&surf);
    for (i = 0; i < 64; ++i) {
        pPix[i] = 0;
    }
    g_pMainSurface = &surf;
    /* Font_BlitGlyph writes through the blitter state, so the surface has to be
     * loaded first -- Font_DrawGlyph does exactly this via Blit_LoadSurface. */
    Blit_LoadSurface(&surf);
    Render_SetTarget(8, 8, 2, 2, 16, NULL);
    for (i = 0; i < 256; ++i) {
        g_aPalette[0][i] = (u32)(0x2000 + i);
    }

    /* startX 0, startY 0; three pixels then end. */
    aGlyph[0] = 0;  /* start Y */
    aGlyph[1] = 0;  /* start X */
    aGlyph[2] = 5;
    aGlyph[3] = 6;
    aGlyph[4] = 7;
    aGlyph[5] = 0xFF;
    Font_BlitGlyph(aGlyph, 1, 1, g_aPalette[0]);
    CheckEqU32(pPix[1 * 8 + 1], 0x2005, "  pixel 0 drawn");
    CheckEqU32(pPix[1 * 8 + 2], 0x2006, "  pixel 1 drawn");
    CheckEqU32(pPix[1 * 8 + 3], 0x2007, "  pixel 2 drawn");
    CheckEqU32(pPix[1 * 8 + 4], 0, "  nothing past the run");

    /* 0xFE starts a new row whose X is a literal, not a delta. */
    for (i = 0; i < 64; ++i) {
        pPix[i] = 0;
    }
    aGlyph[0] = 0; aGlyph[1] = 0;
    aGlyph[2] = 5;
    aGlyph[3] = 0xFE; aGlyph[4] = 3; /* row 1 starts at x = 3 + pen x */
    aGlyph[5] = 6;
    aGlyph[6] = 0xFF;
    Font_BlitGlyph(aGlyph, 1, 1, g_aPalette[0]);
    CheckEqU32(pPix[1 * 8 + 1], 0x2005, "  row 0 pixel");
    CheckEqU32(pPix[2 * 8 + 4], 0x2006, "  row 1 pixel at new X");
    CheckEqU32(pPix[2 * 8 + 5], 0, "  row 1 not continued");

    /* Index 0 is transparency: the ARM at 0xa4288 tests `*v5 != 0` and writes only when the
     * run byte is non-zero, so the pen advances and the pixel is left alone. */
    for (i = 0; i < 64; ++i) {
        pPix[i] = 0xFFFF;
    }
    aGlyph[0] = 0; aGlyph[1] = 0;
    aGlyph[2] = 0;
    aGlyph[3] = 0xFF;
    Font_BlitGlyph(aGlyph, 0, 0, g_aPalette[0]);
    CheckEqU32(pPix[0], 0xFFFF, "  index 0 leaves the pixel alone");

    /* Clipping: a glyph entirely off-surface must not fault or wrap. */
    for (i = 0; i < 64; ++i) {
        pPix[i] = 0;
    }
    aGlyph[0] = 0; aGlyph[1] = 0;
    aGlyph[2] = 5; aGlyph[3] = 0xFF;
    Font_BlitGlyph(aGlyph, -100, -100, g_aPalette[0]);
    Font_BlitGlyph(aGlyph, 100, 100, g_aPalette[0]);
    {
        bool bUntouched = true;

        for (i = 0; i < 64; ++i) {
            if (pPix[i] != 0) {
                bUntouched = false;
            }
        }
        Check(bUntouched, "  off-surface glyph is a no-op");
    }

    SoftSurf_dtor(&surf);
    g_pMainSurface = NULL;
}

static void TestMetrics(void)
{
    printf("FONTSIZE.DAT advance table\n");
    FontMetrics_Load();
    Check(g_fontMetrics.bLoaded, "  loaded");
    /* Measured from the shipped file: byte[0] == 0, byte[32] (space) == 3,
     * byte[65] ('A') == 9. */
    CheckEqI32(g_fontMetrics.aAdvance[0], 0, "  advance[0]");
    CheckEqI32(g_fontMetrics.aAdvance[32], 3, "  advance[space]");
    CheckEqI32(g_fontMetrics.aAdvance[65], 9, "  advance['A']");

    /* Text_MeasureWidth is a plain sum over the table. */
    CheckEqI32(Text_MeasureWidth("A"), 9, "  measure \"A\"");
    CheckEqI32(Text_MeasureWidth("AA"), 18, "  measure \"AA\"");
    CheckEqI32(Text_MeasureWidth("A A"), 9 + 3 + 9, "  measure \"A A\"");
    CheckEqI32(Text_MeasureWidth(""), 0, "  measure empty");
}

/* Map_PatchCraftSprites @ 0x25ab4.
 *
 * The fixture is built so a wrong cell index cannot hide. Twelve distinctive cells sit at the indices the
 * decoded specification says the original stamps, twelve decoy cells sit at the same twelve pairs with the
 * two coordinates the other way round, and every other slot in the grid points at one shared sink. The two
 * index sets are disjoint, so if the port put the craft's X into the nMapX slot instead of its Y, the
 * writes would land on decoys and the sink and this test would fail loudly.
 *
 * Craft coordinates are (2, 3) and the sprite group base is 0, so the sprite indices the original uses are
 * the plain numbers in the stamp table. */
static void TestPatchCraftSprites(void)
{
    static Mc1Rec aSpr[64];
    static Mc1Rec *apSprIdx[64];
    static MapCell aSink;
    static MapCell aWant[12];
    static MapCell aDecoy[12];
    static MapCell *apCells[PUFO_MAP_CELL_DIM * PUFO_MAP_CELL_DIM * 2];
    static const i8 aRow[12] = { 2, 4, 3, 8, 7, 6, 6, 7, 8, 2, 3, 4 };
    static const i8 aCol[12] = { 7, 5, 6, 9, 10, 11, 5, 6, 7, 9, 10, 11 };
    static const u8 aSprOff[12] = { 42, 42, 42, 43, 43, 43, 44, 44, 44, 45, 45, 45 };
    PufoFont font;
    CraftSpritePatchEnv env;
    i32 i;

    memset(aSpr, 0, sizeof(aSpr));
    memset(&aSink, 0, sizeof(aSink));
    memset(aWant, 0, sizeof(aWant));
    memset(aDecoy, 0, sizeof(aDecoy));
    memset(&font, 0, sizeof(font));

    for (i = 0; i < 64; ++i) {
        apSprIdx[i] = &aSpr[i];
    }
    font.apMc1Index = apSprIdx;
    font.nMc1IndexCount = 64;

    /* Sprite state the original overwrites, pre-filled so the writes are visible. */
    for (i = 0; i < 12; ++i) {
        aSpr[13].abPal[i] = 0xAA;
        aSpr[3].abPal[i] = 0xAA;
        aSpr[21].abPal[i] = 0xAA;
    }
    aSpr[42].uUnk20 = 0x1234;
    aSpr[43].uUnk20 = 0x1234;
    aSpr[44].uUnk20 = 0x1234;
    aSpr[45].uUnk20 = 0x1234;

    for (i = 0; i < (i32)(sizeof(apCells) / sizeof(apCells[0])); ++i) {
        apCells[i] = &aSink;
    }
    for (i = 0; i < 12; ++i) {
        const i32 nWant = PUFO_MAP_CELL_DIM * (PUFO_MAP_CELL_DIM + 10 * 3 + aRow[i]) + 10 * 2 + aCol[i];
        const i32 nDecoy = PUFO_MAP_CELL_DIM * (PUFO_MAP_CELL_DIM + 10 * 2 + aCol[i]) + 10 * 3 + aRow[i];
        apCells[nWant] = &aWant[i];
        apCells[nDecoy] = &aDecoy[i];
    }

    env.nCraftSprBase = 0;
    env.apCells = apCells;
    env.nMapLayers = 2;
    env.nCraftMapX = 2;
    env.nCraftMapY = 3;

    /* A map that is not two layers deep must leave every byte alone. */
    env.nMapLayers = 1;
    Map_PatchCraftSprites(&font, &env);
    Check(aSpr[13].abPal[0] == 0xAA, "a one layer map does not recolour craft sprites");
    Check(aSpr[42].uUnk20 == 0x1234, "a one layer map does not clear the sprite flag");
    Check(aSink.pWallSprA == NULL, "a one layer map stamps nothing");

    env.nMapLayers = 2;
    Map_PatchCraftSprites(&font, &env);

    /* Twelve bytes for sprites +13 and +3, only five for +21. */
    {
        bool bOk = true;
        for (i = 0; i < 12; ++i) {
            if (aSpr[13].abPal[i] != 6 || aSpr[3].abPal[i] != 6) {
                bOk = false;
            }
        }
        Check(bOk, "craft sprites +13 and +3 get all twelve palette bytes set to 6");
    }
    {
        bool bOk = true;
        for (i = 0; i < 5; ++i) {
            if (aSpr[21].abPal[i] != 6) {
                bOk = false;
            }
        }
        for (i = 5; i < 12; ++i) {
            if (aSpr[21].abPal[i] != 0xAA) {
                bOk = false;
            }
        }
        Check(bOk, "craft sprite +21 gets only its first five palette bytes");
    }
    {
        bool bOk = (aSpr[42].uUnk20 == 0) && (aSpr[43].uUnk20 == 0) &&
                   (aSpr[44].uUnk20 == 0) && (aSpr[45].uUnk20 == 0);
        Check(bOk, "craft sprites +42 through +45 have the 0x14 flag cleared");
    }

    /* The stamps, and the proof that the craft's Y goes into the nMapX slot. */
    {
        bool bOk = true;
        for (i = 0; i < 12; ++i) {
            if (aWant[i].pWallSprA != &aSpr[aSprOff[i]]) {
                bOk = false;
            }
        }
        Check(bOk, "all twelve cells of layer 1 get the expected wall sprite");
    }
    {
        bool bClean = true;
        for (i = 0; i < 12; ++i) {
            if (aDecoy[i].pWallSprA != NULL) {
                bClean = false;
            }
        }
        Check(bClean, "the swapped-coordinate positions are NOT stamped, the original swaps them");
    }
    Check(aSink.pWallSprA == NULL, "nothing outside the twelve stamped cells is touched");
}

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }

    PufoFont_Init(&g_fontSys);

    TestSprHeader();
    TestGlyphDecode();
    TestBlitIntoSurface();
    TestMetrics();
    TestPatchCraftSprites();

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
