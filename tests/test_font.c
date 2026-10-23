/* test_font.c - checks the font layer against the shipped FONT.SPR / FONTSIZE.DAT.
 *
 * The RLE glyph format and the advance table were decoded independently from
 * lang.dat (see docs/parity/game-core.md), so a mismatch here means the ported
 * decoder is wrong.
 */
#include "asset/pufo_asset.h"
#include "game/pufo_font.h"
#include "game/pufo_mapctx.h"   /* PUFO_MAP_CELL_DIM */
#include "game/pufo_terrain.h"  /* g_aPlaneName */
#include "render/pufo_render.h"

#include <stdio.h>
#include <stdlib.h> /* malloc, the planted allocations the atexit thunk must release */
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

/* SoftSurf_BlitGlyph @ 0x9fc00 is the pair Font_DrawGlyph performs inline: Blit_LoadSurface on
 * g_pMainSurface, then SoftSurf_BlitGlyph_Inner @ 0xa41d4. The pair matters because the blitter
 * works entirely through the global blit state, so a surface loaded by an earlier call must not
 * survive into this one - the stride, the surface bounds and the pixel base all come from the state. */
static void TestBlitGlyphPair(void)
{
    SoftSurf surf;
    SoftSurf stale;
    u16     *pPix;
    u16     *pStale;
    static u8 *apSpr[1];
    u8        aGlyph[8];
    i32       i;

    printf("SoftSurf_BlitGlyph pair inside Font_DrawGlyph\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 8, 8, PIXMODE_16BPP);
    SoftSurf_ctor(&stale);
    SoftSurf_AllocPixels(&stale, 4, 4, PIXMODE_16BPP);
    pPix   = (u16 *)SoftSurf_GetPixels(&surf);
    pStale = (u16 *)SoftSurf_GetPixels(&stale);
    for (i = 0; i < 64; ++i) {
        pPix[i] = 0;
    }
    for (i = 0; i < 16; ++i) {
        pStale[i] = 0xDEAD;
    }

    /* startY 0, startX 0, three pixels, then the 0xFF end - the same record TestBlitIntoSurface uses. */
    aGlyph[0] = 0; aGlyph[1] = 0;
    aGlyph[2] = 5; aGlyph[3] = 6; aGlyph[4] = 7;
    aGlyph[5] = 0xFF;
    aGlyph[6] = 0; aGlyph[7] = 0;
    apSpr[0] = aGlyph;
    g_fontSys.apSprIndex     = apSpr;
    g_fontSys.nSprIndexCount = 1;
    g_nGlyphPalSlotBase      = 0;

    g_pMainSurface = &surf;
    Blit_LoadSurface(&stale);                     /* the state holds the wrong surface on purpose */
    Render_SetTarget(8, 8, 2, 2, 16, NULL);
    for (i = 0; i < 256; ++i) {
        g_aPalette[0][i] = (u32)(0x2000 + i);
    }

    Font_DrawGlyph(1, 1, 0, 0);
    CheckEqU32(pPix[1 * 8 + 1], 0x2005, "  Font_DrawGlyph loads g_pMainSurface @ 0x9fc00 before it blits");
    CheckEqU32(pPix[1 * 8 + 2], 0x2006, "  ... on the main surface's stride of 8, not the stale 4");
    CheckEqU32(pStale[0], 0xDEAD, "  ... the surface that was loaded before is not drawn on");
    CheckEqI32(g_blitState.nSurf0, 8, "  ... the state really was reloaded (nSurf0 is now the main width)");

    g_fontSys.apSprIndex     = NULL;
    g_fontSys.nSprIndexCount = 0;
    g_pMainSurface = NULL;
    SoftSurf_dtor(&surf);
    SoftSurf_dtor(&stale);
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

/* ------------------------------------------------------------------ Font_LoadTactSprites */

static PufoFont g_tacFont;

/* The shipped sprite sets, in the order the original loads them. Every one of these members exists
 * in lang.dat / main.dat, so the slot list is exact rather than "whatever the archives happened to
 * have". */
static const char *const g_aTacWantCulta[] = {
    "blanks", "cursor", "cultivat", "barn", "lightnin", "ufo1", "font",
    "xcom_0", "xcom_1", "xcom_2", "xcom_3",
    "tanks", "floorob", "handob", "bigobs", "bullets", "scang", "smoke", "expl"
};
static const char *const g_aTacWantUrban[] = {
    "blanks", "cursor", "roads", "urbits", "urban", "frniture", "plane",
    "u_ext02", "u_wall02", "u_oper2", "u_bits",
    "font", "xcom_0", "xcom_1", "xcom_2", "xcom_3", "civm", "civf",
    "tanks", "floorob", "handob", "bigobs", "bullets", "scang", "smoke", "expl"
};
static const char *const g_aTacWantNoGroups[] = {
    "blanks", "cursor", "cultivat", "barn", "font",
    "xcom_0", "xcom_1", "xcom_2", "xcom_3",
    "tanks", "floorob", "handob", "bigobs", "bullets", "scang", "smoke", "expl"
};

static void TacEnv(TactSpriteEnv *pEnv, i32 nMission, i32 nLayers, i32 nVariant, i32 nParam)
{
    memset(pEnv, 0, sizeof(*pEnv));
    pEnv->nMissionType     = (u8)nMission;
    pEnv->nMapLayers       = (u8)nLayers;
    pEnv->nTerrainVariant  = (u8)nVariant;
    pEnv->nTerrainParam    = nParam;
    pEnv->paTerrainSets    = g_aTactTerrainSetName;
    pEnv->paUfoSets        = g_aTactUfoSetName;
    pEnv->paPlaneNames     = g_aPlaneName;
    pEnv->paUfoClass       = g_abTactUfoClassByVariant;
    pEnv->nTerrainSetCount = 10;
    pEnv->nUfoClassCount   = 8;
    pEnv->nVariantCount    = 5;
}

static void TacFontReset(u16 nSprTotal, u16 nMc1Total)
{
    memset(&g_tacFont, 0, sizeof(g_tacFont));
    PufoFont_Init(&g_tacFont);
    /* Preset counters make the two running totals distinguishable: a group base that takes the
     * wrong counter shows up as 11 where 7 was wanted. */
    g_tacFont.nTotalSprFrames = nSprTotal;
    g_tacFont.nTotalMc1       = nMc1Total;
}

static void TacCheckNames(const char *const *apszWant, i32 nWant, const char *pszCase)
{
    char szGot[512];
    char szWant[512];
    size_t n;
    i32 i;

    szGot[0] = '\0';
    szWant[0] = '\0';
    n = 0;
    for (i = 0; i < (i32)g_tacFont.nSlotCount && n < sizeof(szGot) - 16; ++i) {
        n += (size_t)snprintf(szGot + n, sizeof(szGot) - n, "%s ", g_tacFont.aszName[i]);
    }
    n = 0;
    for (i = 0; i < nWant; ++i) {
        n += (size_t)snprintf(szWant + n, sizeof(szWant) - n, "%s ", apszWant[i]);
    }
    Check((i32)g_tacFont.nSlotCount == nWant && strcmp(szGot, szWant) == 0, pszCase);
    if ((i32)g_tacFont.nSlotCount != nWant || strcmp(szGot, szWant) != 0) {
        printf("       got:  %s\n       want: %s\n", szGot, szWant);
    }
}

/* Map_ApplyTemplateToCells @ 0x54cf4 resolves every template slot through *(u16 *)(fontSys + 2 * nFlag),
 * so the flag byte picks one of the font block's first five u16s. The shipped templates carry 2, 3 and 4. */
static void TestFlagSprBases(void)
{
    PufoFont f;

    memset(&f, 0, sizeof(f));
    f.anFlagSprBase[0] = 10;
    f.anFlagSprBase[1] = 20;
    f.anFlagSprBase[2] = 30;
    f.anFlagSprBase[3] = 40;
    f.anFlagSprBase[4] = 50;

    CheckEqI32(Font_GetFlagSprBase(&f, 0), 10, "flag 0 is the blanks base at fontSys + 0x00");
    CheckEqI32(Font_GetFlagSprBase(&f, 1), 20, "flag 1 is the cursor base at +0x02");
    CheckEqI32(Font_GetFlagSprBase(&f, 2), 30, "flag 2 is the terrain-module base at +0x04");
    CheckEqI32(Font_GetFlagSprBase(&f, 3), 40, "flag 3 is the craft base at +0x06");
    CheckEqI32(Font_GetFlagSprBase(&f, 4), 50, "flag 4 is the UFO-module base at +0x08");
    CheckEqI32(Font_GetFlagSprBase(&f, 5), 0, "a flag past the five slots resolves to nothing");
}

static void TestLoadTactSprites(void)
{
    TactSpriteEnv env;

    /* culta (mission 1), two map layers, terrain variant 1 with the tier dword 0. */
    TacFontReset(11u, 7u);
    TacEnv(&env, 1, 2, 1, 0);
    Font_LoadTactSprites(&g_tacFont, &env);

    TacCheckNames(g_aTacWantCulta, (i32)(sizeof(g_aTacWantCulta) / sizeof(g_aTacWantCulta[0])),
                  "culta loads the 19 shipped sets in the original's order (0x257C4)");

    /* The bases are the running total captured BEFORE their own group's load. */
    CheckEqI32(g_tacFont.anFlagSprBase[0], 11, "the blanks base is the spr total at entry (0x257DC)");
    CheckEqI32(g_tacFont.anFlagSprBase[1], 13, "the cursor base is the spr total after blanks (0x257F0)");
    CheckEqI32(g_tacFont.anFlagSprBase[2], 9, "the terrain base is the MC1 total, not the spr one (0x25814)");
    CheckEqI32(g_tacFont.anFlagSprBase[3], 75, "the craft base is the mc1 total the terrain loop left (0x25874)");
    CheckEqI32(g_tacFont.anFlagSprBase[4], 121, "the UFO base is the mc1 total the craft load left (0x258B8)");
    CheckEqI32(g_tacFont.nGlyphBase, 177, "the font group base is the spr total after the ufo1 set (0x25920)");
    CheckEqI32(g_tacFont.anXcomSprBase[0], 433, "xcom_0 base (0x25934)");
    CheckEqI32(g_tacFont.anXcomSprBase[1], 708, "xcom_1 base, 12 frames after xcom_0 (0x2594C)");
    CheckEqI32(g_tacFont.anXcomSprBase[2], 983, "xcom_2 base (0x25960)");
    CheckEqI32(g_tacFont.anXcomSprBase[3], 1266, "xcom_3 base (0x25974)");
    Check(g_tacFont.nCivmSprBase == 0 && g_tacFont.nCivfSprBase == 0,
          "a non-urban mission loads neither civilian set (0x2598C)");
    CheckEqI32(g_tacFont.nTanksBase, 1549, "tanks base (0x259C0)");
    CheckEqI32(g_tacFont.nFloorobBase, 1677, "floorob base, loaded second though its slot is lower (0x259D4)");
    CheckEqI32(g_tacFont.nHandobBase, 1750, "handob base (0x259E8)");
    CheckEqI32(g_tacFont.nBigObsBase, 1878, "bigobs base (0x259FC)");
    CheckEqI32(g_tacFont.nBulletsBase, 1935, "bullets base (0x25A10)");
    CheckEqI32(g_tacFont.nScangBase, 1956, "scang base (0x25A24)");
    CheckEqI32(g_tacFont.nSmokeBase, 2410, "smoke base (0x25A38)");
    CheckEqI32(g_tacFont.nExplBase, 2470, "expl base (0x25A4C)");
    /* BuildIndex counts what the slots actually hold, so the preset 11 / 7 are not in these two. */
    CheckEqI32(g_tacFont.nSprIndexCount, 2478 - 11, "the tail Font_BuildIndex rebuilt the glyph index (0x25A64)");
    CheckEqI32(g_tacFont.nMc1IndexCount, 141 - 7, "  and the record index");

    /* urban (mission 5), one layer, variant 3 with tier 1: the variant table says class 4, whose
     * four sets are u_ext02 / u_wall02 / u_oper2 / u_bits -- u_oper2 is what separates class 4 from
     * classes 1, 2, 5 and 6, and u_disec2 would mean classes 3 or 7. */
    TacFontReset(0u, 0u);
    TacEnv(&env, 5, 1, 3, 1);
    Font_LoadTactSprites(&g_tacFont, &env);
    TacCheckNames(g_aTacWantUrban, (i32)(sizeof(g_aTacWantUrban) / sizeof(g_aTacWantUrban[0])),
                  "urban adds the four terrain sets, the plane and the two civilian sets");
    Check(g_tacFont.nCivmSprBase != 0 && g_tacFont.nCivfSprBase > g_tacFont.nCivmSprBase,
          "civm loads before civf and civf's base is higher (0x25998, 0x259AC)");

    /* No layers and no terrain variant: the craft and UFO groups are skipped outright. */
    TacFontReset(0u, 0u);
    TacEnv(&env, 1, 0, 0, 0);
    Font_LoadTactSprites(&g_tacFont, &env);
    TacCheckNames(g_aTacWantNoGroups, (i32)(sizeof(g_aTacWantNoGroups) / sizeof(g_aTacWantNoGroups[0])),
                  "nMapLayers 0 and nTerrainVariant 0 skip both optional groups (0x25870, 0x258A8)");

    /* The craft name follows the layer count, not the mission type. */
    TacFontReset(0u, 0u);
    TacEnv(&env, 1, 3, 0, 0);
    Font_LoadTactSprites(&g_tacFont, &env);
    Check(strcmp(g_tacFont.aszName[4], "avenger") == 0,
          "three layers pick aPlane[2] \"avenger\", so the index is nMapLayers - 1 (0x25890)");

    /* Deviation: the original indexes both tables with no bound. The port skips the group instead. */
    {
        static const char *const apszNoTerrain[] = {
            "blanks", "cursor", "plane", "ufo1", "font",
            "xcom_0", "xcom_1", "xcom_2", "xcom_3",
            "tanks", "floorob", "handob", "bigobs", "bullets", "scang", "smoke", "expl"
        };
        static const char *const apszNoUfo[] = {
            "blanks", "cursor", "cultivat", "barn", "plane", "font",
            "xcom_0", "xcom_1", "xcom_2", "xcom_3",
            "tanks", "floorob", "handob", "bigobs", "bullets", "scang", "smoke", "expl"
        };

        TacFontReset(0u, 0u);
        TacEnv(&env, 10, 1, 1, 0);
        Font_LoadTactSprites(&g_tacFont, &env);
        TacCheckNames(apszNoTerrain, (i32)(sizeof(apszNoTerrain) / sizeof(apszNoTerrain[0])),
                      "mission type 10 loads no terrain set -- the original reads past aTactTerrainSetName");

        TacFontReset(0u, 0u);
        TacEnv(&env, 1, 1, 6, 0);
        Font_LoadTactSprites(&g_tacFont, &env);
        TacCheckNames(apszNoUfo, (i32)(sizeof(apszNoUfo) / sizeof(apszNoUfo[0])),
                      "terrain variant 6 loads no UFO set -- the original reads past abTactUfoClassByVariant");
    }
}

/* Font_Init_ReturnArg @ 0x46130: Font_Init on the block, then the block comes back. */
static void TestFontInitReturnArg(void)
{
    PufoFont font;
    PufoFont *pRet;

    printf("Font_Init_ReturnArg (0x46130)\n");

    memset(&font, 0xA5, sizeof(font)); /* dirty block: the init must clear it */
    pRet = Font_Init_ReturnArg(&font);
    Check(pRet == &font, "the same block is handed back (0x46140)");
    CheckEqI32(font.bInited, 1, "Font_Init ran: bInited is set");
    CheckEqI32(font.nSlotCount, 0, "Font_Init ran: slot count cleared");
    Check(font.apSprIndex == NULL, "Font_Init ran: apSprIndex cleared");
    Check(font.anFlagSprBase[4] == 0, "Font_Init ran: the flag bases cleared from 0xA5");
}

/* Font_RegisterAtexit @ 0x460d0 and its thunk j_Font_Free @ 0x460dc (Font_AtexitThunk). */
static void TestFontAtexit(void)
{
    printf("Font_RegisterAtexit / Font_AtexitThunk (0x460d0, 0x460dc)\n");

    CheckEqI32(Font_RegisterAtexit(), 0, "the registrar returns atexit's success value");

    /* The thunk names the global block, exactly as j_Font_Free does. Plant live allocations so the
     * release is observable, then run the thunk and check every one of them is gone. */
    g_fontSys.nSlotCount  = 1;
    g_fontSys.apSprData[0] = malloc(8);
    g_fontSys.apMc1Data[0] = malloc(8);
    g_fontSys.apMc1Index  = malloc(8);
    g_fontSys.apSprIndex  = malloc(8);
    g_fontSys.bInited     = 1;

    Font_AtexitThunk();

    Check(g_fontSys.apSprData[0] == NULL, "thunk released apSprData[0]");
    Check(g_fontSys.apMc1Data[0] == NULL, "thunk released apMc1Data[0]");
    Check(g_fontSys.apMc1Index == NULL, "thunk released apMc1Index");
    Check(g_fontSys.apSprIndex == NULL, "thunk released apSprIndex");
    CheckEqI32(g_fontSys.nSlotCount, 0, "thunk cleared nSlotCount");
    CheckEqI32(g_fontSys.bInited, 0, "thunk cleared bInited");

    /* PufoFont_Free NULLs what it releases, so a second thunk run is safe -- the registered atexit
     * entry will run this same thunk once more at process exit. */
    Font_AtexitThunk();
    CheckEqI32(g_fontSys.bInited, 0, "a second thunk run is idempotent");
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
    TestBlitGlyphPair();
    TestMetrics();
    TestPatchCraftSprites();
    TestFlagSprBases();
    TestLoadTactSprites();
    TestFontInitReturnArg();
    TestFontAtexit();

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
