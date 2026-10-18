/* test_titles.c - the credits screen (Titles_DrawBackdrop @ 0x1e630).
 *
 * What is asserted is what the function decides and what lands in the framebuffer: which rows are
 * in the visible window, whether the white fill or the fade blit happens, and whether an '@'
 * record draws a glyph. Text rows are not asserted: Text_DrawString writes through the font layer
 * and is not injected, so a string's pixels are not observable here. */
#include <stdio.h>
#include <string.h>

#include "game/pufo_titles.h"
#include "game/pufo_mainmenu.h" /* GameUI_InitStartup / GameUiStartupEnv, the shutdown's tail call */
#include "game/pufo_font.h"
#include "game/pufo_options.h"
#include "game/pufo_timer.h"
#include "asset/pufo_asset.h"
#include "audio/pufo_audio.h"
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

    /* The scroll<50 fill goes BlitRectProxy -> SoftSurf_BlitRect -> Blit_CopyToXY @ 0xa42e4, and that one
     * gates on the TARGET pixel mode (blitState+0x20), published only by Blit_SetTargetParams @ 0xa39ac.
     * A real frame arms it every present, GX_PresentFrame @ 0x9ea7c -> Render_SetTarget @ 0x9faa4, with the
     * original's own 240x320/2-byte constants; the suite has no window, so it arms the same descriptor
     * here. Without it the fill silently draws nothing, which is what let this suite read an untouched
     * surface as "no credit rows" -- 0 in Release, 0xCDCD in Debug. The rectangle and glyph paths write
     * into the loaded surface and read only the mode out of the target, never into pBits. */
    Render_SetTarget(240, 320, 2, 240 * 2, 320 * 2, g_surf.pPixels);

    g_fontSys.apSprIndex = NULL;
    memset(&g_color, 0, sizeof(g_color));

    /* The fills and the indexed glyph resolve their colours through g_aPalette, which is zero
     * until the palettes are loaded. Give the two slots this function uses a colour that is not the
     * scroll<50 fill's own value, so "the fill ran" and "a glyph ran" are separate observations:
     * an indexed 16bpp blit writes the low half of the palette entry straight through
     * (Blit_Indexed @ 0xa306c), so slot 21's glyph reads 0x5C5C and slot 1's reads 0x1111. */
    memset(g_aPalette[21], 0x5C, sizeof(g_aPalette[21]));
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

    /* Under 50 the original fills the whole screen: SoftSurf_BlitRectProxy(0, 0, 0, 240, 320, 0xFF, 0xFF, 0xFF)
     * at 0x1E6A0, and Blit_PackedColor turns those operands into ((0xFF>>3)<<11) + 32*(0xFF>>2) + (0xFF>>3)
     * = 0xFFFF. A scroll of 0 has an empty row window -- nLast = 0 / 20 = 0 -- so no credit row paints over
     * the fill, and the fill also covers the 4x2 backdrop glyph that 0x1E65C blits at (0, 0) on every path. */
    ClearScreen();
    env = MakeEnv(0);
    CheckEqI32("returns the row count", Titles_DrawBackdrop(&env), 3);
    CheckEqI32("a scroll of 0 leaves the white fill at the sample point", Sample(), 0xFFFF);
    CheckEqI32("  over the top of the backdrop glyph", SampleAt(1, 1), 0xFFFF);

    /* 50..279 with 3 * (135 - scroll) still positive: a keyed full-screen blit. The screen was cleared and
     * the backdrop glyph is 4x2 at (0, 0), so the only thing that can reach the sample point is that blit. */
    ClearScreen();
    env = MakeEnv(100);
    (void)Titles_DrawBackdrop(&env);
    Check(Sample() != 0, "a scroll of 100 runs the fade blit");

    /* Past 135 the fade key goes negative and the blit is skipped, but the row window is
     * 0 .. 200/20 - 1 clamped to 3, so the '@' record on row 1 still draws its glyph at
     * y = 20 * (1 + 17) - 200 = 160. It is centred (4 wide at (240 - 4) / 2 = 118, 2 tall), and
     * slot 1's colour is 0x1111, so the covered point reads that value exactly. */
    ClearScreen();
    env = MakeEnv(200);
    (void)Titles_DrawBackdrop(&env);
    CheckEqI32("a scroll of 200 draws the glyph row at y 160", SampleAt(120, 160), 0x1111);
    CheckEqI32("and nothing below it", SampleAt(120, 200), 0);

    /* The visible window below 320 is rows 0 .. scroll/20 - 1, clamped to the row count: at 20 that is
     * row 0 alone, at y = 20 * 17 - 20 = 320, one row past the surface. 20 is also under 50, so the
     * white fill runs and the sample point keeps the fill colour rather than a glyph's. */
    ClearScreen();
    env = MakeEnv(20);
    (void)Titles_DrawBackdrop(&env);
    CheckEqI32("a scroll of 20 puts row 0 past the surface and keeps the fill", Sample(), 0xFFFF);

    /* At 340 the window starts at row 0 and spans 36 rows, clamped to 3, so the '@' record on
     * row 1 draws its glyph at y = 20 * (1 + 17) - 340 = 20. 340 is past both branches, so the
     * field around it stays as cleared. */
    ClearScreen();
    env = MakeEnv(340);
    (void)Titles_DrawBackdrop(&env);
    CheckEqI32("a scroll of 340 draws the glyph row at y 20", SampleAt(119, 20), 0x1111);
    CheckEqI32("  and runs neither the fill nor the fade", Sample(), 0);

    /* Past the data: first row 3 is not below the clamped last row 3, so nothing is drawn. The
     * backdrop blit at 0x1E65C is before every branch, so it still lands. */
    ClearScreen();
    env = MakeEnv(400);
    CheckEqI32("still returns the row count", Titles_DrawBackdrop(&env), 3);
    CheckEqI32("a scroll of 400 draws no credit row", Sample(), 0);
    CheckEqI32("  while the backdrop glyph still blits at (0, 0)", SampleAt(1, 1), 0x5C5C);

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

/* --- EndScreen_Init --------------------------------------------------------------------------
 *
 * A recording audio backend (the built-in stub reports "not ready", which would skip the music
 * path) and a recording timer backend, so the ending's side effects are observable without audio
 * or a window. */
static i32 g_nMusicLoads;
static u32 g_cbMusic;
static i32 g_nTimerSets;
static u32 g_aTimerId[4];
static u32 g_aTimerMs[4];

static bool Fake_Init(u32 nRate, i32 nChannels, u32 nFlags)
{
    (void)nRate; (void)nChannels; (void)nFlags;
    return true;
}
static void Fake_SetWindow(void *hWnd) { (void)hWnd; }
static void *Fake_SampleLoad(const void *pData, u32 cb, u32 nMode)
{
    (void)pData; (void)cb; (void)nMode;
    return (void *)0x1;
}
static void Fake_SampleSetMode(void *pSample, u32 nMode) { (void)pSample; (void)nMode; }
static i32 g_nSampleFrees;
static i32 g_nMusicFrees;
static void Fake_SampleFree(void *pSample) { (void)pSample; ++g_nSampleFrees; }
static i32 Fake_PlaySound(void *pSample) { (void)pSample; return 0; }
static void Fake_SetSfxVolume(i32 nVolume) { (void)nVolume; }
static void Fake_SetSfxPaused(i32 nChannel, i32 bPaused) { (void)nChannel; (void)bPaused; }
static void *Fake_MusicLoad(const void *pData, u32 cb, u32 nMode)
{
    (void)pData; (void)nMode;
    ++g_nMusicLoads;
    g_cbMusic = cb;
    return (void *)0x2;
}
static void Fake_MusicLoop(void *pMod, i32 bLoop) { (void)pMod; (void)bLoop; }
static void Fake_MusicOptimize(void *pMod, i32 nA, i32 nB) { (void)pMod; (void)nA; (void)nB; }
static void Fake_MusicPlay(void *pMod) { (void)pMod; }
static void Fake_MusicVolume(void *pMod, i32 nVolume) { (void)pMod; (void)nVolume; }
static void Fake_MusicPaused(void *pMod, i32 bPaused) { (void)pMod; (void)bPaused; }
static void Fake_MusicFree(void *pMod) { (void)pMod; ++g_nMusicFrees; }
static void Fake_Close(void) { }

static const PufoAudioBackend g_fakeAudio = {
    Fake_Init, Fake_SetWindow, Fake_SampleLoad, Fake_SampleSetMode, Fake_SampleFree,
    Fake_PlaySound, Fake_SetSfxVolume, Fake_SetSfxPaused, Fake_MusicLoad,
    Fake_MusicLoop, Fake_MusicOptimize, Fake_MusicPlay, Fake_MusicVolume,
    Fake_MusicPaused, Fake_MusicFree, Fake_Close
};

static void TimerRecSet(HWND hWnd, u32 nId, u32 nMs, void *pUser)
{
    (void)hWnd; (void)pUser;
    if (g_nTimerSets < 4) {
        g_aTimerId[g_nTimerSets] = nId;
        g_aTimerMs[g_nTimerSets] = nMs;
    }
    ++g_nTimerSets;
}
static void TimerRecKill(HWND hWnd, u32 nId, void *pUser) { (void)hWnd; (void)nId; (void)pUser; }

static u32 ArchiveMemberSize(const char *pszName)
{
    AssetReader rdr;
    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, pszName)) {
        return 0u;
    }
    return AssetReader_Size(&rdr);
}

static void TestEndScreen(void)
{
    static PufoOptions opts;
    static i32 aRowRec[16];
    static i32 nMaxRows, nScroll;
    static u16 nUiScreen;
    static u8 *pBackdrop;
    EndScreenInitEnv env;
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    TimerBackend tb;
    i32 nPalInk;

    printf("EndScreen_Init @ 0x388b0\n");

    if (!Asset_OpenArchives(apszPaths)) {
        Check(false, "Asset_OpenArchives(lang.dat, main.dat)");
        return;
    }

    Sound_SetBackend(&g_fakeAudio);
    tb.pfnSet = TimerRecSet;
    tb.pfnKill = TimerRecKill;
    tb.pUser = NULL;
    Timer_SetBackend(&tb);
    Options_InitDefaults(&opts);

    memset(&g_fontSys, 0, sizeof(g_fontSys));
    memset(aRowRec, 0xEE, sizeof(aRowRec));
    memset(g_aPalette[21], 0, sizeof(g_aPalette[21]));
    nScroll = 999;
    nUiScreen = 0;
    pBackdrop = NULL;
    g_nMusicLoads = 0;
    g_cbMusic = 0;
    g_nTimerSets = 0;

    env.pFont      = &g_fontSys;
    env.pnMaxRows  = &nMaxRows;
    env.anRowRec   = aRowRec;
    env.ppBackdrop = &pBackdrop;
    env.pnScroll   = &nScroll;
    env.puUiScreen = &nUiScreen;
    env.pOptions   = &opts;

    EndScreen_Init(&env, 1);

    CheckEqI32("the good end has 10 credit rows", nMaxRows, 10);
    CheckEqI32("its first row record is 0x56B", aRowRec[0], 0x56B);
    CheckEqI32("and its last 0x574", aRowRec[9], 0x574);
    Check(aRowRec[10] == (i32)0xEEEEEEEE, "slots past the ten are left at whatever was there");
    CheckEqI32("the screen id becomes 18", (i32)nUiScreen, 18);
    CheckEqI32("and the scroll is zeroed", nScroll, 0);

    Check(g_nTimerSets == 2 && g_aTimerId[0] == PUFO_TIMER_RENDER &&
          g_aTimerMs[0] == PUFO_TIMER_MS_RENDER && g_aTimerId[1] == PUFO_TIMER_ANIM &&
          g_aTimerMs[1] == PUFO_TIMER_MS_ANIM,
          "StartTimers_A re-arms the render (40 ms) and animation (25 ms) timers");

    Check(pBackdrop != NULL, "the backdrop glyph was allocated");
    Check(pBackdrop != NULL && (pBackdrop[0] != 0u || pBackdrop[1] != 0u),
          "and goodend.pad was read into it");

    Check(Sound_IsInitialised(), "the z samples went through the audio init");
    CheckEqI32("one music track was loaded", g_nMusicLoads, 1);
    CheckEqI32("and it is track 4, the good ending", (i32)g_cbMusic,
               (i32)ArchiveMemberSize("back4.it"));
    Check(Sound_HasMusic(), "which is now playing");

    nPalInk = 0;
    for (size_t i = 0; i < sizeof(g_aPalette[21]); ++i) {
        if (g_aPalette[21][i] != 0u) { nPalInk = 1; break; }
    }
    Check(nPalInk != 0, "goodend.pal filled palette slot 0x15");

    CheckEqI32("the glyph base is the frame count recorded BEFORE the font group loads",
               (i32)g_fontSys.nGlyphBase, 0);
    Check(g_fontSys.nTotalSprFrames > 0, "and the font group loaded after it");

    free(pBackdrop);
    pBackdrop = NULL;
    memset(aRowRec, 0xEE, sizeof(aRowRec));
    g_nMusicLoads = 0;
    g_cbMusic = 0;

    EndScreen_Init(&env, 0);

    CheckEqI32("the bad end has 16 credit rows", nMaxRows, 16);
    CheckEqI32("starting at record 0x575", aRowRec[0], 0x575);
    CheckEqI32("and ending at 0x584", aRowRec[15], 0x584);
    Check(pBackdrop != NULL, "badend.pad was allocated and read too");
    CheckEqI32("on track 3, the bad ending", (i32)g_cbMusic, (i32)ArchiveMemberSize("back3.it"));

    free(pBackdrop);
    Sound_Shutdown();
    Asset_CloseArchives();
}

static void TestTitlesInit(void)
{
    static PufoOptions opts;
    static TitlesAnimation anim;
    static TitlesParticle  aParts[3000];
    static TitlesStar      aStars[190];
    static i32             aRowRec[44];
    static u8             *apGlyphs[5];
    static i32             nMaxRows;
    static u16             nUiScreen;
    TitlesInitEnv env;
    TimerBackend tb;
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    i32 nPalInk;

    printf("Titles_Init @ 0x380d8\n");

    if (!Asset_OpenArchives(apszPaths)) {
        Check(false, "Asset_OpenArchives(lang.dat, main.dat)");
        return;
    }

    Sound_SetBackend(&g_fakeAudio);
    tb.pfnSet = TimerRecSet;
    tb.pfnKill = TimerRecKill;
    tb.pUser = NULL;
    Timer_SetBackend(&tb);
    Options_InitDefaults(&opts);

    memset(&g_fontSys, 0, sizeof(g_fontSys));
    memset(&anim, 0, sizeof(anim));
    anim.aParticles      = aParts;
    anim.aStars          = aStars;
    anim.nFillPalSlot    = 0;
    anim.nPresentPalSlot = 12;
    memset(aRowRec, 0xEE, sizeof(aRowRec));
    memset(apGlyphs, 0, sizeof(apGlyphs));
    memset(g_aPalette[12], 0, sizeof(g_aPalette[12]));
    memset(g_aPalette[31], 0, sizeof(g_aPalette[31]));
    nMaxRows = 0;
    nUiScreen = 0;
    g_nMusicLoads = 0;
    g_cbMusic = 0;
    g_nTimerSets = 0;
    g_pTitlesSurface = NULL;

    env.pFont      = &g_fontSys;
    env.pAnim      = &anim;
    env.pnMaxRows  = &nMaxRows;
    env.anRowRec   = aRowRec;
    env.apGlyphs   = apGlyphs;
    env.puUiScreen = &nUiScreen;
    env.pOptions   = &opts;

    Titles_Init(&env);

    CheckEqI32("the opening credits have 44 rows", nMaxRows, 44);
    CheckEqI32("starting at record 0x587", aRowRec[0], 0x587);
    CheckEqI32("the 22nd is 0x59C", aRowRec[21], 0x59C);
    CheckEqI32("then come the two 0x65x rows", aRowRec[22], 0x655);
    CheckEqI32("and the last is 0x5A2", aRowRec[43], 0x5A2);
    CheckEqI32("the screen id becomes 16", (i32)nUiScreen, 16);
    CheckEqI32("the credits scroll is zeroed", anim.nScroll, 0);

    CheckEqI32("190 stars are seeded", anim.nStarCount, 190);
    {
        bool bOk = true;
        for (int i = 0; i < 190; ++i) {
            if (aStars[i].nBaseX < 0 || aStars[i].nBaseX > 239) bOk = false;
            if (aStars[i].nSpeed < 1 || aStars[i].nSpeed > 4) bOk = false;
            if (aStars[i].nAmplitude < 1 || aStars[i].nAmplitude > 101) bOk = false;
            /* seeded at -320..-1, then Titles_UpdateStars adds the speed once */
            if (aStars[i].nY < -319 || aStars[i].nY > 3) bOk = false;
        }
        Check(bOk, "x in 0..239, speed 1..4, amplitude 1..101, y in -319..3 after one step");
    }

    CheckEqI32("scene 0 was loaded and the index advanced to 1", anim.nSceneIndex, 1);
    CheckEqI32("the frame counter was reset", anim.nFrame, 0);
    CheckEqI32("the particle count is the pair count of title0.dat",
               anim.nParticleCount, (i32)((ArchiveMemberSize("title0.dat") - 2u) / 2u));

    {
        bool bOk = (apGlyphs[0] != NULL) && (apGlyphs[4] != NULL);
        Check(bOk, "the five titles0..4.pad glyph blobs were allocated and read");
        Check(bOk && *(const u16 *)apGlyphs[0] > 0u,
              "and the first blob carries a real u16 width header");
    }

    nPalInk = 0;
    for (size_t i = 0; i < sizeof(g_aPalette[12]); ++i) {
        if (g_aPalette[12][i] != 0u) { nPalInk = 1; break; }
    }
    Check(nPalInk != 0, "fade.pal filled palette slot 12");
    nPalInk = 0;
    for (size_t i = 0; i < sizeof(g_aPalette[31]); ++i) {
        if (g_aPalette[31][i] != 0u) { nPalInk = 1; break; }
    }
    Check(nPalInk != 0, "titles.pal filled palette slot 31");

    Check(g_pTitlesSurface != NULL, "Titles_ctor created the titles surface");
    Check(g_pTitlesSurface != NULL && g_pTitlesSurface->nWidth == 240 &&
          g_pTitlesSurface->nHeight == 320,
          "it is 240x320");
    Check(g_pTitlesSurface != NULL && SoftSurf_GetPixels(g_pTitlesSurface)[0] == 0u &&
          SoftSurf_GetPixels(g_pTitlesSurface)[240 * 320 - 1] == 0u,
          "and the whole 0x12C00-byte surface was memset to 0");

    CheckEqI32("one music track was loaded", g_nMusicLoads, 1);
    CheckEqI32("it is track 3, the credits track", (i32)g_cbMusic,
               (i32)ArchiveMemberSize("back3.it"));

    Check(g_nTimerSets == 2 && g_aTimerId[0] == PUFO_TIMER_RENDER &&
          g_aTimerMs[0] == PUFO_TIMER_MS_RENDER && g_aTimerId[1] == PUFO_TIMER_ANIM &&
          g_aTimerMs[1] == PUFO_TIMER_MS_ANIM,
          "StartTimers_C re-arms the render (40 ms) and animation (25 ms) timers");

    CheckEqI32("the glyph base is the frame count recorded BEFORE the font group loads",
               (i32)g_fontSys.nGlyphBase, 0);
    Check(g_fontSys.nTotalSprFrames > 0, "and the font group loaded after it");

    if (g_pTitlesSurface != NULL) {
        SoftSurf_dtor(g_pTitlesSurface);
        free(g_pTitlesSurface);
        g_pTitlesSurface = NULL;
    }
    for (int i = 0; i < 5; ++i) {
        free(apGlyphs[i]);
    }
    Sound_Shutdown();
    Asset_CloseArchives();
}

/* --- the two shutdowns ----------------------------------------------------------------------- */

static u32 g_aKillIds[8];
static i32 g_nKills;

static void RecKill(HWND hWnd, u32 nId, void *pUser)
{
    (void)hWnd; (void)pUser;
    if (g_nKills < 8) {
        g_aKillIds[g_nKills] = nId;
    }
    ++g_nKills;
}

/* GameUI_InitStartup's pad loader, wired the way the shell wires it. */
static bool StubLoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                         const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2); /* UiControl_LoadPads @ 0x1a604 */
    return true;
}

static void TestTitlesShutdown(void)
{
    static PufoFont font;
    static u8 *apGlyphs[5];
    static TitlesAnimation anim;
    static TitlesParticle aParts[4];
    static TitlesStar aStars[4];
    static u16 nUiScreen;
    static TimerBackend tb;
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    TitlesShutdownEnv env;
    i32 i;

    printf("Titles_Shutdown @ 0x3826c and Titles_dtor @ 0x46fa4\n");

    if (!Asset_OpenArchives(apszPaths)) {
        Check(false, "Asset_OpenArchives(lang.dat, main.dat)");
        return;
    }

    Sound_SetBackend(&g_fakeAudio);
    tb.pfnSet = TimerRecSet;
    tb.pfnKill = RecKill;
    tb.pUser = NULL;
    Timer_SetBackend(&tb);

    /* A real font with a real group loaded, so Font_Free has something to release and its effect is
     * observable. */
    memset(&font, 0, sizeof(font));
    PufoFont_Init(&font);
    font.nGlyphBase = font.nTotalSprFrames;
    (void)Font_LoadSpr(&font, "font");
    Font_BuildIndex(&font);
    Check(font.apSprIndex != NULL, "the fixture font has a built index before the shutdown frees it");

    for (i = 0; i < 5; ++i) {
        apGlyphs[i] = (u8 *)malloc(16u);
    }
    g_pTitlesSurface = (SoftSurf *)malloc(sizeof(SoftSurf));
    SoftSurf_ctor(g_pTitlesSurface);
    (void)SoftSurf_AllocPixels(g_pTitlesSurface, 240, 320, 0);

    memset(&anim, 0, sizeof(anim));
    anim.aParticles = aParts;
    anim.aStars = aStars;
    anim.nParticleCount = 4;
    anim.nStarCount = 4;
    nUiScreen = 16;
    g_nKills = 0;

    /* Load the audio the credits screen itself loads, so Sound_Shutdown has something to release and
     * its presence is observable rather than assumed. */
    Sound_InitAndLoadSamples("tact");
    Sound_LoadAndPlayMusic(3);
    g_nSampleFrees = 0;
    g_nMusicFrees = 0;

    memset(&env, 0, sizeof(env));
    env.pFont = &font;
    env.apGlyphs = apGlyphs;
    env.pAnim = &anim;
    env.puUiScreen = &nUiScreen;
    Titles_Shutdown(&env);

    Check(g_nSampleFrees > 0, "Sound_Shutdown releases the loaded samples (0x38270)");
    CheckEqI32("  and the loaded music module", g_nMusicFrees, 1);
    CheckEqI32("KillTimers_C runs (0x38274)", g_nKills, 2);
    Check(g_aKillIds[0] == PUFO_TIMER_RENDER && g_aKillIds[1] == PUFO_TIMER_ANIM,
          "  the render timer 0xC351 first, then the anim timer 0xC352");
    Check(font.apSprIndex == NULL, "Font_Free releases the font's sprite index (0x3827C)");
    Check(apGlyphs[0] == NULL && apGlyphs[1] == NULL && apGlyphs[2] == NULL
          && apGlyphs[3] == NULL && apGlyphs[4] == NULL,
          "all five titlesN.pad glyph blobs are released (0x38280..0x3828C)");
    CheckEqI32("Titles_dtor writes screen id 22, not the 16 the credits ran under (0x46FB0)", (i32)nUiScreen, 22);
    Check(g_pTitlesSurface == NULL, "  and the titles surface is released (0x46FC8..0x46FD0)");

    /* The state gate at 0x382AC: with no state block, Titles_dtor never runs, so neither the screen
     * id nor the surface changes. */
    g_pTitlesSurface = (SoftSurf *)malloc(sizeof(SoftSurf));
    SoftSurf_ctor(g_pTitlesSurface);
    (void)SoftSurf_AllocPixels(g_pTitlesSurface, 240, 320, 0);
    nUiScreen = 16;
    env.pAnim = NULL;
    Titles_Shutdown(&env);
    CheckEqI32("a null state block leaves the screen id alone -- the 0x382AC gate", (i32)nUiScreen, 16);
    Check(g_pTitlesSurface != NULL, "  and the surface survives with it");

    /* Titles_dtor on its own: the screen id is written whether or not a surface exists. */
    free(g_pTitlesSurface);
    g_pTitlesSurface = NULL;
    nUiScreen = 16;
    Titles_dtor(&nUiScreen);
    CheckEqI32("Titles_dtor writes the screen id even with no surface", (i32)nUiScreen, 22);
    Check(g_pTitlesSurface == NULL, "  and a missing surface is not dereferenced (0x46FC0)");

    nUiScreen = 16;
    Titles_dtor(NULL);
    CheckEqI32("a null screen pointer is bounded, not written through", (i32)nUiScreen, 16);

    g_nKills = 0;
    nUiScreen = 16;
    Titles_Shutdown(NULL);
    CheckEqI32("a null env kills no timers", g_nKills, 0);
    CheckEqI32("  and writes no screen id", (i32)nUiScreen, 16);
}

static void TestEndScreenShutdown(void)
{
    static PufoFont font;
    static PufoOptions opts;
    static u8 *pBackdrop;
    static UiControl aMenuCtl[15];
    static UiControl *pStartCtl;
    static i32 anRowKeys[7];
    static i32 anRowKeySrc[7];
    static u8 abRowDisabled[7];
    static i32 nListMode;
    static i32 nUiScreen;
    static u8 *pBgGlyph;
    static u8 *pEmptyPad;
    static GameUiStartupEnv stEnv;
    static TimerBackend tb;
    EndScreenShutdownEnv env;
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    printf("EndScreen_Shutdown @ 0x389f8\n");

    if (!Asset_OpenArchives(apszPaths)) {
        Check(false, "Asset_OpenArchives(lang.dat, main.dat)");
        return;
    }

    Sound_SetBackend(&g_fakeAudio);
    tb.pfnSet = TimerRecSet;
    tb.pfnKill = RecKill;
    tb.pUser = NULL;
    Timer_SetBackend(&tb);
    Options_InitDefaults(&opts);

    memset(&font, 0, sizeof(font));
    PufoFont_Init(&font);
    font.nGlyphBase = font.nTotalSprFrames;
    (void)Font_LoadSpr(&font, "font");
    Font_BuildIndex(&font);

    pBackdrop = (u8 *)malloc(64u); /* what EndScreen_Init allocates for the ending backdrop */
    pStartCtl = (UiControl *)malloc(sizeof(UiControl));
    nUiScreen = 18;
    g_nKills = 0;

    memset(&stEnv, 0, sizeof(stEnv));
    stEnv.pFont = &font;
    stEnv.paMenuControls = aMenuCtl;
    stEnv.pStartCtl = pStartCtl;
    stEnv.ppBgGlyph = &pBgGlyph;
    stEnv.ppEmptyBtnPad = &pEmptyPad;
    stEnv.anRowKeys = anRowKeys;
    stEnv.abRowDisabled = abRowDisabled;
    stEnv.anRowKeySrc = anRowKeySrc;
    stEnv.pnListMode = &nListMode;
    stEnv.pnUiScreen = &nUiScreen;
    stEnv.pOptions = &opts;
    stEnv.pfnLoadPads = StubLoadPads;

    /* The ending's own prefix is "z", but the shipped main.dat carries no Z*.WAV at all -- only
     * TACT*.WAV and BACK0..4.IT -- so that prefix loads zero samples. Load the tact set as the
     * fixture's audio instead, so Sound_Shutdown has something to release and its presence is
     * observable. */
    Sound_InitAndLoadSamples("tact");
    Sound_LoadAndPlayMusic(4);
    g_nSampleFrees = 0;
    g_nMusicFrees = 0;

    memset(&env, 0, sizeof(env));
    env.pFont = &font;
    env.ppBackdrop = &pBackdrop;
    env.pStartup = NULL; /* no tail call, so what Font_Free leaves behind is still visible here */
    EndScreen_Shutdown(&env);

    Check(g_nSampleFrees > 0, "Sound_Shutdown releases the loaded samples (0x389FC)");
    CheckEqI32("  and the loaded music module", g_nMusicFrees, 1);
    CheckEqI32("KillTimers_A runs (0x38A00)", g_nKills, 2);
    Check(font.apSprIndex == NULL, "Font_Free releases the font (0x38A08)");
    Check(pBackdrop == NULL, "the ending backdrop is released and nulled (0x38A0C..0x38A14)");
    CheckEqI32("with no startup env the screen id is left where the ending put it", nUiScreen, 18);

    /* With the startup env wired, the tail call runs and the start menu re-creates the font the
     * shutdown just released -- so a non-null index afterwards is the tail call's own fingerprint. */
    PufoFont_Init(&font);
    font.nGlyphBase = font.nTotalSprFrames;
    (void)Font_LoadSpr(&font, "font");
    Font_BuildIndex(&font);
    nUiScreen = 18;
    env.pStartup = &stEnv;
    EndScreen_Shutdown(&env);
    CheckEqI32("the tail call into GameUI_InitStartup @ 0x38a28 brings the start menu up", nUiScreen, 17);
    Check(font.apSprIndex != NULL, "  and that call is what re-loads the font the shutdown freed");

    g_nKills = 0;
    nUiScreen = 18;
    EndScreen_Shutdown(NULL);
    CheckEqI32("a null env kills no timers", g_nKills, 0);
    CheckEqI32("  and touches no screen id", nUiScreen, 18);

    free(pStartCtl);
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestBackdrop();
    TestBorderAndBlur();
    TestAnimation();
    TestEndScreen();
    TestTitlesInit();
    TestTitlesShutdown();
    TestEndScreenShutdown();
    if (TestLoadScene() != 0) {
        printf("\n%s: %d failure(s)\n", "FAIL", g_nFailures);
        return 1;
    }

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
