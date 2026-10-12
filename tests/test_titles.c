/* test_titles.c - the credits screen (Titles_DrawBackdrop @ 0x1e630).
 *
 * What is asserted is what the function decides and what lands in the framebuffer: which rows are
 * in the visible window, whether the white fill or the fade blit happens, and whether an '@'
 * record draws a glyph. Text rows are not asserted: Text_DrawString writes through the font layer
 * and is not injected, so a string's pixels are not observable here. */
#include <stdio.h>
#include <string.h>

#include "game/pufo_titles.h"
#include "game/pufo_font.h"
#include "render/pufo_render.h"

static int g_nFailures;

static void Check(int bOk, const char *pszWhat)
{
    if (bOk) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(const char *pszWhat, i32 nGot, i32 nWant)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static SoftSurf g_surf;
static u8 g_aBackdrop[4 + 8];       /* a 4x2 glyph is enough: the blit clips to the surface */
static u8 g_aCreditGlyph[4 + 8];
static const u8 *g_apGlyphs[2];
static u8 g_aRecs[16 * PUFO_TEXT_REC_STRIDE];
static i32 g_anRowRec[3];
static GfxColorEnv g_color;

static void BuildData(void)
{
    SoftSurf_ctor(&g_surf);
    SoftSurf_AllocPixels(&g_surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_surf;
    g_fontSys.apSprIndex = NULL;
    memset(&g_color, 0, sizeof(g_color));

    /* The fills and the indexed glyph resolve their colours through g_aPalette, which is zero
     * until the palettes are loaded. Give the two slots this function uses a non-zero colour so
     * "something was drawn" is observable. */
    memset(g_aPalette[21], 0xFF, sizeof(g_aPalette[21]));
    memset(g_aPalette[1], 0x11, sizeof(g_aPalette[1]));
    memset(g_aPalette[63], 0xFF, sizeof(g_aPalette[63]));

    g_aBackdrop[0] = 4; g_aBackdrop[1] = 0;
    g_aBackdrop[2] = 2; g_aBackdrop[3] = 0;
    memset(g_aBackdrop + 4, 0x11, 8);

    g_aCreditGlyph[0] = 4; g_aCreditGlyph[1] = 0;
    g_aCreditGlyph[2] = 2; g_aCreditGlyph[3] = 0;
    memset(g_aCreditGlyph + 4, 0x37, 8);
    g_apGlyphs[0] = g_aBackdrop;
    g_apGlyphs[1] = g_aCreditGlyph;

    memset(g_aRecs, 0, sizeof(g_aRecs));
    memcpy(g_aRecs + 10 * PUFO_TEXT_REC_STRIDE, "HELLO", 6);
    memcpy(g_aRecs + 11 * PUFO_TEXT_REC_STRIDE, "@1", 3);
    memcpy(g_aRecs + 12 * PUFO_TEXT_REC_STRIDE, "BYE", 4);

    g_anRowRec[0] = 10;
    g_anRowRec[1] = 11;
    g_anRowRec[2] = 12;
}

static TitlesBackdrop MakeEnv(i32 nScroll)
{
    TitlesBackdrop env;

    memset(&env, 0, sizeof(env));
    env.nScroll = nScroll;
    env.nMaxRows = 3;
    env.pBackdrop = g_aBackdrop;
    env.anRowRec = g_anRowRec;
    env.apGlyphs = g_apGlyphs;
    env.pTextRecs = g_aRecs;
    env.pColor = &g_color;
    return env;
}

/* A pixel well inside the screen, away from the (0,0) backdrop glyph. The surface is 16bpp, so
 * the stride is two bytes, not four. */
static u32 SampleAt(i32 x, i32 y)
{
    return (u32)*((u16 *)(g_surf.pPixels + 2 * (y * 240 + x)));
}

static u32 Sample(void)
{
    return SampleAt(120, 160);
}

static void ClearScreen(void)
{
    memset(g_surf.pPixels, 0, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * (size_t)g_surf.nBytesPP);
}

static void TestBackdrop(void)
{
    TitlesBackdrop env;

    printf("Titles_DrawBackdrop @ 0x1e630\n");
    BuildData();

    /* Under 50 the original fills the screen white. That fill resolves its colour through the
     * palette layer and is not asserted here; what is asserted is the row rule, which at a scroll
     * of 0 admits no rows at all. */
    env = MakeEnv(0);
    CheckEqI32("returns the row count", Titles_DrawBackdrop(&env), 3);
    Check(Sample() == 0, "a scroll of 0 admits no credit rows");

    /* 50..279 with 3 * (135 - scroll) still positive: a keyed full-screen blit. */
    ClearScreen();
    env = MakeEnv(100);
    (void)Titles_DrawBackdrop(&env);
    Check(Sample() != 0, "a scroll of 100 runs the fade blit");

    /* Past 135 the fade key goes negative and the blit is skipped, but the row window is
     * 0 .. 200/20 - 1 clamped to 3, so the '@' record on row 1 still draws its glyph at
     * y = 20 * (1 + 17) - 200 = 160. */
    ClearScreen();
    env = MakeEnv(200);
    (void)Titles_DrawBackdrop(&env);
    Check(SampleAt(120, 160) != 0, "a scroll of 200 draws the glyph row at y 160");
    Check(SampleAt(120, 200) == 0, "and nothing below it");

    /* The visible window below 320 is rows 0 .. scroll/20 - 1, clamped to the row count. */
    ClearScreen();
    env = MakeEnv(20);
    (void)Titles_DrawBackdrop(&env);
    Check(Sample() == 0, "a scroll of 20 reaches only row 0, far from the sample point");

    /* At 340 the window starts at row 0 and spans 36 rows, clamped to 3, so the '@' record on
     * row 1 draws its glyph at y = 20 * (1 + 17) - 340 = 20. */
    ClearScreen();
    env = MakeEnv(340);
    (void)Titles_DrawBackdrop(&env);
    Check(SampleAt(119, 20) != 0, "a scroll of 340 draws the glyph row at y 20");

    /* Past the data: first row 3 is not below the clamped last row 3, so nothing is drawn. */
    ClearScreen();
    env = MakeEnv(400);
    CheckEqI32("still returns the row count", Titles_DrawBackdrop(&env), 3);
    Check(Sample() == 0, "a scroll of 400 draws no credit row");

    g_pMainSurface = NULL;
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestBackdrop();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
