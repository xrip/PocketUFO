/* test_font.c - checks the font layer against the shipped FONT.SPR / FONTSIZE.DAT.
 *
 * The RLE glyph format and the advance table were decoded independently from
 * lang.dat (see docs/parity/game-core.md), so a mismatch here means the ported
 * decoder is wrong.
 */
#include "asset/pufo_asset.h"
#include "game/pufo_font.h"
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

    /* Index 0 is a real pixel in this format, not transparency. */
    for (i = 0; i < 64; ++i) {
        pPix[i] = 0xFFFF;
    }
    aGlyph[0] = 0; aGlyph[1] = 0;
    aGlyph[2] = 0;
    aGlyph[3] = 0xFF;
    Font_BlitGlyph(aGlyph, 0, 0, g_aPalette[0]);
    CheckEqU32(pPix[0], 0x2000, "  index 0 draws palette entry 0");

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

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
