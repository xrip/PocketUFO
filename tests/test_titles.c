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
#include "asset/pufo_asset.h"
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

static void TestBorderAndBlur(void)
{
    SoftSurf titles;

    printf("SoftSurf_DrawBorderAndBlur @ 0x9e5c8\n");

    SoftSurf_ctor(&titles);
    SoftSurf_AllocPixels(&titles, 240, 320, PIXMODE_8BPP);
    g_pTitlesSurface = &titles;
    memset(g_aPalette[0], 0, sizeof(g_aPalette[0]));

    /* Everything non-zero first, so a border pixel going to zero can only come from the fill and
     * an interior pixel changing can only come from the blur. */
    memset(titles.pPixels, 0x40, (size_t)titles.nWidth * (size_t)titles.nHeight);

    (void)SoftSurf_DrawBorderAndBlur();

    CheckEqI32("the top-left corner is filled to palette entry 0",
               (i32)titles.pPixels[0], 0);
    CheckEqI32("the top-right corner too",
               (i32)titles.pPixels[239], 0);
    CheckEqI32("and the bottom-left",
               (i32)titles.pPixels[319 * 240], 0);
    CheckEqI32("the blur is interior-only, so (1,1) sees two zero neighbours",
               (i32)titles.pPixels[1 * 240 + 1], 0x1F);
    /* The blur is an in-place cascade, so a pixel's own neighbours may already have been blurred
     * by the time it is reached and the exact value depends on position. What is position-independent
     * is that an interior pixel of a uniform field loses value and stays non-zero. */
    Check(titles.pPixels[160 * 240 + 120] != 0x40 &&
          titles.pPixels[160 * 240 + 120] != 0,
          "a pixel with four non-zero neighbours is blurred but not erased");

    g_pTitlesSurface = NULL;
}

static SoftSurf g_titlesSurf;
static TitlesParticle g_aParticles[4096];
static TitlesStar g_aStars[4];

static void ClearTitles(void)
{
    memset(g_titlesSurf.pPixels, 0, (size_t)g_titlesSurf.nWidth * (size_t)g_titlesSurf.nHeight);
}

static i32 TitlesPix(i32 x, i32 y)
{
    return (i32)g_titlesSurf.pPixels[y * 240 + x];
}

static TitlesAnimation MakeAnimEnv(void)
{
    TitlesAnimation env;

    memset(&env, 0, sizeof(env));
    env.aParticles       = g_aParticles;
    env.nParticleCount   = 0;
    env.aStars           = g_aStars;
    env.nStarCount       = 0;
    env.pTitlesSurface   = &g_titlesSurf;
    env.nFillPalSlot     = 0;
    env.nPresentPalSlot  = 12;
    env.Credits          = MakeEnv(0);
    return env;
}

static void TestAnimation(void)
{
    TitlesAnimation env = MakeAnimEnv();
    TitlesParticle *p = &g_aParticles[0];
    TitlesStar *pStar = &g_aStars[0];

    printf("titles animation state\n");

    SoftSurf_ctor(&g_titlesSurf);
    SoftSurf_AllocPixels(&g_titlesSurf, 240, 320, PIXMODE_8BPP);
    /* TestBorderAndBlur leaves g_pMainSurface NULL, and the titles surface is 8bpp indexed in the
     * original, which is what lets Titles_PresentToSoftSurf blit it as a glyph. */
    g_pMainSurface = &g_surf;
    g_pTitlesSurface = &g_titlesSurf;
    memset(g_aPalette[0], 0x33, sizeof(g_aPalette[0]));
    memset(g_aPalette[12], 0x22, sizeof(g_aPalette[12]));

    /* The step is (target - current) / 15, with a sign fallback so a gap under 15 still moves one
     * pixel per frame instead of stalling. */
    env.nParticleCount = 1;
    p->nX = 100; p->nY = 100; p->nTargetX = 250; p->nTargetY = 40;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("a gap of 150 moves 10", p->nX, 110);
    CheckEqI32("and a gap of -60 moves -4", p->nY, 96);

    p->nX = 100; p->nY = 100; p->nTargetX = 107; p->nTargetY = 94;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("a gap of 7 is under 15, so the fallback moves +1", p->nX, 101);
    CheckEqI32("and a gap of -6 moves -1", p->nY, 99);

    p->nX = 100; p->nY = 100; p->nTargetX = 100; p->nTargetY = 100;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("a particle already at its target does not move", p->nX, 100);
    CheckEqI32("in either axis", p->nY, 100);

    /* The timer: positive counts down, zero re-arms and goes negative, negative never counts down and
     * only moves the target. */
    env.nFrame = 5;
    p->nTimer = 5; p->nTargetX = 100; p->nTargetY = 100;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("a positive timer counts down by one", p->nTimer, 4);

    p->nTimer = 0; p->nTargetX = 100; p->nTargetY = 100;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("a zero timer sends the target to the right edge", p->nTargetX, 230);
    Check(p->nTargetY >= 200 && p->nTargetY <= 299, "and picks a target between 200 and 299");
    CheckEqI32("which makes the timer negative", p->nTimer, -1);

    p->nTimer = -1; p->nTargetX = 100; p->nTargetY = 305;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("a negative timer with the target low moves the target left", p->nTargetX, 90);
    CheckEqI32("and never counts down further", p->nTimer, -1);

    p->nTimer = -1; p->nTargetX = 100; p->nTargetY = 250;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("with the target high it raises the target instead", p->nTargetY, 260);
    CheckEqI32("leaving x alone", p->nTargetX, 100);

    /* Frame 21 restores the targets from the home pair and re-arms the timer. */
    env.nFrame = 21;
    p->nX = 10; p->nY = 10; p->nTargetX = 200; p->nTargetY = 200;
    p->nHomeX = 40; p->nHomeY = 60; p->nTimer = 7;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("frame 21 restores the target x from the home pair", p->nTargetX, 40);
    CheckEqI32("and the target y", p->nTargetY, 60);
    Check(p->nTimer >= 0 && p->nTimer <= 29, "and re-arms the timer in 0..29");

    /* Frame 20 and 22 through 500 run the step but not the timer. */
    env.nFrame = 20;
    p->nX = 100; p->nY = 100; p->nTargetX = 115; p->nTargetY = 100; p->nTimer = 9;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("frame 20 still steps the particle", p->nX, 101);
    CheckEqI32("but leaves the timer alone", p->nTimer, 9);

    env.nFrame = 300;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("a mid-sequence frame leaves the timer alone too", p->nTimer, 9);

    /* 681 or more hands over to the scene loader. With no archives open this exercises the guarded
     * path: nothing is loaded, but the frame counter still resets and the index still advances. */
    env.nFrame = 681;
    env.nSceneIndex = 0;
    (void)Titles_AnimateParticles(&env);
    CheckEqI32("a late frame resets the frame counter", env.nFrame, 0);
    CheckEqI32("and advances the scene index even when nothing could be loaded", env.nSceneIndex, 1);
    env.nSceneIndex = 0;

    /* Stars advance by speed and are reborn past the bottom. */
    env.nStarCount = 1;
    pStar->nBaseX = 100; pStar->nY = 318; pStar->nSpeed = 3; pStar->nAmplitude = 50;
    Titles_UpdateStars(&env);
    CheckEqI32("318 + 3 passes 320, so the star is reborn in the same call", pStar->nY, 0);
    Check(pStar->nBaseX >= 0 && pStar->nBaseX <= 239, "at a fresh x inside the screen");
    Check(pStar->nSpeed >= 1 && pStar->nSpeed <= 3, "with speed 1..3");
    Check(pStar->nAmplitude >= 1 && pStar->nAmplitude <= 100, "and amplitude 1..100");

    pStar->nBaseX = 100; pStar->nY = 100; pStar->nSpeed = 2; pStar->nAmplitude = 50;
    Titles_UpdateStars(&env);
    CheckEqI32("below the wrap it just advances", pStar->nY, 102);
    CheckEqI32("without touching the amplitude", pStar->nAmplitude, 50);

    /* The frame driver: a 1x1 particle fill, a 3x3 star fill, and the frame counter. With speed 0 the
     * angle is 0 and cos is 1, so the star lands exactly at baseX + amplitude.
     *
     * The driver runs the border fill and the blur AFTER the fills, so the top row is erased and ink
     * bleeds one pixel outwards. Everything here is therefore placed in the interior and asserted by
     * comparison rather than at the exact edge of a block. */
    ClearTitles();
    env.nFrame = 0;
    env.nParticleCount = 1;
    p->nX = 100; p->nY = 100;
    env.nStarCount = 1;
    pStar->nBaseX = 100; pStar->nY = 160; pStar->nSpeed = 0; pStar->nAmplitude = 5;
    env.Credits.nMaxRows = 0;
    env.Credits.nScroll = 0;
    (void)Titles_DrawAnimatedFrame(&env);
    CheckEqI32("the frame counter advances once", env.nFrame, 1);
    Check(TitlesPix(106, 161) != 0, "a zero speed puts the star at baseX + amplitude = 105");
    Check(TitlesPix(121, 161) == 0, "so nothing is drawn where amplitude 20 would land");

    ClearTitles();
    env.nFrame = 0;
    pStar->nAmplitude = 20;
    (void)Titles_DrawAnimatedFrame(&env);
    Check(TitlesPix(121, 161) != 0, "amplitude 20 moves the star to 120");
    Check(TitlesPix(106, 161) == 0, "and leaves the old place");

    ClearTitles();
    env.nFrame = 0;
    pStar->nSpeed = 100;   /* angle = 100 * 160 * 0.01 = 160 rad, cos is not 1 */
    (void)Titles_DrawAnimatedFrame(&env);
    Check(TitlesPix(121, 161) == 0,
          "a non-zero speed swings the star off baseX + amplitude");

    {
        i32 nFound = 0;
        for (i32 y = 100; y <= 105 && !nFound; ++y) {
            for (i32 x = 100; x <= 105; ++x) {
                if (TitlesPix(x, y) != 0) { nFound = 1; break; }
            }
        }
        Check(nFound != 0, "the particle is drawn as a 1x1 fill within its 0..4 jitter box");
    }
    Check(TitlesPix(112, 100) == 0 && TitlesPix(100, 112) == 0,
          "and the jitter never reaches past 4");

    /* The credit-line window, which is the same rule Titles_DrawBackdrop uses. */
    env.nStarCount = 0;
    env.nParticleCount = 0;
    env.Credits = MakeEnv(340);
    env.Credits.nMaxRows = 3;
    CheckEqI32("a scroll of 340 draws from row 0", Titles_DrawCreditLines(&env.Credits), 3);

    env.Credits = MakeEnv(400);
    env.Credits.nMaxRows = 3;
    CheckEqI32("a scroll of 400 starts at row 3 and is clamped by the row count",
               Titles_DrawCreditLines(&env.Credits), 3);

    env.Credits = MakeEnv(400);
    env.Credits.nMaxRows = 2;
    CheckEqI32("with only two rows the window is empty but the count is still returned",
               Titles_DrawCreditLines(&env.Credits), 2);
}

static int TestLoadScene(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    static u8 aRaw[16384];
    TitlesAnimation env = MakeAnimEnv();
    AssetReader rdr;
    u32 cb;
    i32 nBad = 0;

    printf("Titles_LoadScene @ 0x468cc\n");

    if (!Asset_OpenArchives(apszPaths)) {
        Check(false, "Asset_OpenArchives(lang.dat, main.dat)");
        return 1;
    }

    /* Ground truth first: read title0.dat directly, so every derived field can be checked against the
     * bytes rather than against the port's own arithmetic. */
    AssetReader_Clear(&rdr);
    Check(AssetReader_Find(&rdr, "title0.dat"), "title0.dat is in the archives");
    cb = AssetReader_Size(&rdr);
    Check(cb >= 2u && cb <= sizeof(aRaw), "and its size fits the fixture buffer");
    Check(AssetReader_Read(&rdr, aRaw, cb), "read it");

    env.nSceneIndex = 0;
    (void)Titles_LoadScene(&env);

    Check(env.nParticleCount > 0 && env.nParticleCount <= 4096, "the scene has particles");
    CheckEqI32("and the count is (size - 2) / 2 byte pairs", env.nParticleCount,
               (i32)((cb - 2u) / 2u));
    CheckEqI32("the header bytes become the two scale factors", env.nSceneScaleX, (i32)aRaw[0]);
    CheckEqI32("both of them", env.nSceneScaleY, (i32)aRaw[1]);
    CheckEqI32("the frame counter is reset", env.nFrame, 0);
    CheckEqI32("and the scene index advances to 1", env.nSceneIndex, 1);

    for (i32 i = 0; i < env.nParticleCount; ++i) {
        const TitlesParticle *p = &g_aParticles[i];
        i32 nWantX = 3 * (i32)aRaw[2 + 2 * i] + 3 * (80 - (i32)aRaw[0]) / 2;
        i32 nWantY = 3 * (i32)aRaw[3 + 2 * i] + (320 - 3 * (i32)aRaw[1]) / 2;

        if (p->nTargetX != 120 || p->nTargetY != 160) { nBad = 1; }
        if (p->nHomeX != nWantX || p->nHomeY != nWantY) { nBad = 1; }
        if (p->nX < -500 || p->nX > 499) { nBad = 1; }
        if (p->nY < -350 || p->nY > -1) { nBad = 1; }
    }
    Check(nBad == 0,
          "every particle has target (120, 160), a home pair derived from the file bytes, and a start in range");

    (void)Titles_LoadScene(&env);
    CheckEqI32("loading again takes title1.dat and wraps the index back to 0", env.nSceneIndex, 0);
    Check(env.nParticleCount > 0, "and title1.dat has particles too");

    Asset_CloseArchives();
    return 0;
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestBackdrop();
    TestBorderAndBlur();
    TestAnimation();
    if (TestLoadScene() != 0) {
        printf("\n%s: %d failure(s)\n", "FAIL", g_nFailures);
        return 1;
    }

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
