/* test_renderframe.c - Render_Frame @ 0x22310, the frame dispatcher.
 *
 * The dispatcher is verified through its observable consequences: which hook ran (geoscape,
 * present), the glyph palette base the default branch writes (34 at 0x22370), the cursor tick
 * that only the default branch performs, the modal layout the dialog fills, the fade ink on the
 * surface, and - for the menu branch - the mode-3 cursor wrap and layout output, which prove the
 * branch reached MainMenu_Draw through the dispatcher.
 *
 * The iso-layer-gated steps (MapView_DrawIsoLayer and the status stack) need the full tactical
 * fixture and are exercised by the subsystem tests that already cover those functions; here
 * bDrawIsoLayer stays 0, which is the original's own skip path at 0x2240c / 0x225cc. */
#include <stdio.h>
#include <string.h>

#include "game/pufo_renderframe.h"
#include "render/pufo_render.h"
#include "util/pufo_str.h"

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
        /* The counter was missing here: a FAIL printed but the suite still exited 0, so no
         * mutation or regression in this suite could ever fail the build. Fixed in unit 105. */
        g_nFailures += 1;
    }
}

static SoftSurf g_surf;
static GfxColorEnv g_color;
static i32 g_nGeoscapeCalls;
static i32 g_nPresentCalls;
static i32 g_nBannerPadBlits;

static i32 Hook_Geoscape(void *pUser)
{
    (void)pUser;
    ++g_nGeoscapeCalls;
    return 7;
}

static i32 Hook_Present(void *pUser)
{
    (void)pUser;
    ++g_nPresentCalls;
    return 3;
}

static void Hook_BannerPad(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
    ++g_nBannerPadBlits;
}

static i32 InkAt(i32 x, i32 y)
{
    const u16 *p = (const u16 *)g_surf.pPixels;

    return (i32)p[y * g_surf.nWidth + x];
}

/* The menu fixture, shaped like test_mainmenu's: a 1x1 backdrop in the zeroed palette slot 20
 * lays no ink, and the font is not installed, so Text_DrawString is a no-op. Mode 3 is chosen
 * because its action-phase write is observable proof that MainMenu_Draw ran. */
#define RF_REC_COUNT 1103
static u8        g_aTextRecs[RF_REC_COUNT * PUFO_TEXT_REC_STRIDE];
static i32       g_aKeys[7];
static u8        g_aDisabled[7];
static char      g_aSlotLabels[7][40];
static u8        g_aBgBlob[5];
static UiControl g_startCtl;
static i32       g_nPhaseSave112;
static UiModalAnim g_modalAnim;
static UiModalLayout g_modalLayout;
static MainMenuEnv g_mm;

static void PutRec(i32 nRec, const char *psz)
{
    pufo_strcpy((char *)(g_aTextRecs + PUFO_TEXT_REC_STRIDE * nRec), psz);
}

static void MenuFixture(void)
{
    i32 i;

    memset(g_aTextRecs, 0, sizeof(g_aTextRecs));
    memset(g_aKeys, 0, sizeof(g_aKeys));
    memset(g_aDisabled, 0, sizeof(g_aDisabled));
    memset(&g_startCtl, 0, sizeof(g_startCtl));
    memset(&g_modalAnim, 0, sizeof(g_modalAnim));
    memset(&g_modalLayout, 0, sizeof(g_modalLayout));
    memset(&g_mm, 0, sizeof(g_mm));

    PutRec(PUFO_MAINMENU_REC_MODAL, "AB");
    for (i = 0; i < 7; ++i) {
        (void)snprintf(g_aSlotLabels[i], sizeof(g_aSlotLabels[i]), "slot%d", i);
    }

    g_aBgBlob[0] = 1; g_aBgBlob[1] = 0;
    g_aBgBlob[2] = 1; g_aBgBlob[3] = 0;
    g_aBgBlob[4] = 0; /* index 0 is transparent */

    g_nPhaseSave112 = -1;
    g_modalAnim.pColor = &g_color;
    g_modalLayout.nButtonX[0] = 17;
    g_modalLayout.nButtonX[1] = 23;
    g_modalLayout.nButtonY = 29;
    g_modalLayout.nButtonCount = 99;

    g_mm.pBgGlyph             = g_aBgBlob;
    g_mm.nListMode            = 3;
    g_mm.anRowKeys            = g_aKeys;
    g_mm.abRowDisabled        = g_aDisabled;
    g_mm.pSlotLabels          = &g_aSlotLabels[0][0];
    g_mm.pStartCtl            = &g_startCtl;
    g_mm.pTextRecs            = g_aTextRecs;
    g_mm.pnActionPhaseSave112 = &g_nPhaseSave112;
    g_mm.pModalAnim           = &g_modalAnim;
    g_mm.pModalLayout         = &g_modalLayout;
    g_mm.pColor               = &g_color;
}

static void ResetHooks(void)
{
    g_nGeoscapeCalls = 0;
    g_nPresentCalls  = 0;
}

int main(void)
{
    RenderFrameEnv env;

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    SoftSurf_ctor(&g_surf);
    SoftSurf_AllocPixels(&g_surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_surf;
    memset(&g_color, 0, sizeof(g_color));
    MenuFixture();

    printf("Render_Frame @ 0x22310 dispatch\n");

    /* 1. The geoscape branch: hook called, present NOT called, return value passed through
     * (0x22328 / 0x22330 - Geoscape_Draw presents itself). */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.bGeoscapeMode  = 1;
    env.nUiScreen      = 17;
    env.pfnGeoscapeDraw = Hook_Geoscape;
    env.pfnPresent     = Hook_Present;
    CheckEqI32(Render_Frame(&env), 7, "geoscape branch returns the hook's value");
    CheckEqI32(g_nGeoscapeCalls, 1, "geoscape hook ran once");
    CheckEqI32(g_nPresentCalls, 0, "geoscape branch skips GX_Present");

    /* 2. Screen 0x11: mode 3 wraps the modal cursor and fills its caller-owned layout. */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen  = 0x11;
    env.pMainMenu  = &g_mm;
    env.pfnPresent = Hook_Present;
    g_nGlyphPalSlotBase = 0;
    memset(g_surf.pPixels, 0, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);
    (void)Render_Frame(&env);
    CheckEqI32(g_nPresentCalls, 1, "menu branch presents");
    CheckEqI32(g_nGeoscapeCalls, 0, "menu branch is not geoscape");
    CheckEqI32(g_nPhaseSave112, 1, "MainMenu_Draw mode 3 wraps the modal phase through the dispatcher");
    CheckEqI32(g_modalAnim.nCursor, 1, "mode 3 advances the modal cursor from 40 to 1");
    CheckEqI32(g_modalAnim.bProgressive, 0, "mode 3 clears progressive draw at the cursor wrap");
    CheckEqI32(g_modalLayout.nButtonCount, 0, "the loading modal reports no buttons");
    CheckEqI32(g_modalLayout.nButtonX[0], 17, "the no-button layout leaves X0 alone");
    CheckEqI32(g_modalLayout.nButtonX[1], 23, "the no-button layout leaves X1 alone");
    CheckEqI32(g_modalLayout.nButtonY, 29, "the no-button layout leaves Y alone");
    Check(InkAt(120, 150) != 0, "the loading modal body draws through Render_Frame");
    CheckEqI32(g_nGlyphPalSlotBase, 0, "menu branch leaves the glyph base alone");

    /* 3. Default branch (screen 0): cursor tick, base 34, fade-out dark blit, present. */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen  = 0;
    env.pfnPresent = Hook_Present;
    {
        CursorDirState cur;
        memset(&cur, 0, sizeof(cur));
        cur.bSlot8Byte2 = 1;
        cur.nSlot9      = 1;
        cur.nSlot10     = 5;
        env.pCursorDir  = &cur;
        env.bCursorBusy = true; /* new byte2 = 1, old = 1 -> no recompute */
        env.nFadeCount  = 1;    /* the whole-screen dark blit at 0x22538 */
        /* The fade blit's colour is Blit_PackedColor(0, mid 0, low 0) = black, blended with
         * key 20: it darkens an inked surface rather than inking a blank one. */
        memset(g_surf.pPixels, 0xFF, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);
        g_nGlyphPalSlotBase = 0;
        (void)Render_Frame(&env);
        CheckEqI32(cur.nSlot10, 6, "cursor countdown advanced by the step");
        CheckEqI32(cur.bSlot8Byte3, 1, "old byte2 shifted into byte3");
        CheckEqI32(cur.bSlot8Byte2, 1, "busy tick keeps byte2 set");
        CheckEqI32(g_nGlyphPalSlotBase, 34, "default branch writes glyph base 34 (0x22370)");
        Check(InkAt(120, 160) != 0 && InkAt(120, 160) < 0xFFFF, "fade-out darkens the whole surface");
    }
    CheckEqI32(g_nPresentCalls, 1, "default branch presents");

    /* 4. Fade-in path: counter 5 -> alpha 24*(10-5) = 120 blit of the fade pad (0x225b8). */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen  = 0;
    env.pfnPresent = Hook_Present;
    {
        CursorDirState cur;
        static u8 aFadeBlob[5];
        PadImage fadePad;

        memset(&cur, 0, sizeof(cur));
        cur.nSlot10 = 5;
        aFadeBlob[0] = 1; aFadeBlob[1] = 0;
        aFadeBlob[2] = 1; aFadeBlob[3] = 0;
        aFadeBlob[4] = 1; /* palette index 1 in the pad's slot */
        memset(&fadePad, 0, sizeof(fadePad));
        fadePad.pData  = aFadeBlob;
        fadePad.uWidth = 1;
        fadePad.uHeight = 1;
        fadePad.nMode  = 20;

        g_aPalette[20][1] = 0xFFFFu;
        memset(g_surf.pPixels, 0, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);
        env.pCursorDir = &cur;
        env.nFadeCount = 0;
        env.nFadeIn    = 5;
        env.pFadePad   = &fadePad;
        (void)Render_Frame(&env);
        Check(InkAt(0, 0) > 0 && InkAt(0, 0) < 0xFFFF,
              "fade-in blits the fade pad with partial alpha");
    }

    /* 5. The modal dialog: apUiSlotsA[19] set fills the layout the dialog computes. */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen  = 0;
    env.pfnPresent = Hook_Present;
    {
        CursorDirState cur;
        UiModalAnim anim;
        UiModalLayout layout;

        memset(&cur, 0, sizeof(cur));
        memset(&anim, 0, sizeof(anim));
        anim.pColor = &g_color;
        anim.nAnimPhase = 4;
        memset(&layout, -1, sizeof(layout));

        env.pCursorDir   = &cur;
        env.bModalActive = 1;
        env.nModalWidth  = 150;
        env.nModalRec    = PUFO_MAINMENU_REC_MODAL;
        env.nModalLines  = 1;
        env.pszModalBtn1 = "OK";
        env.pszModalBtn2 = "Cancel";
        env.pModalAnim   = &anim;
        env.pTextRecs    = g_aTextRecs;
        env.pModalLayout = &layout;
        (void)Render_Frame(&env);
        Check(layout.nButtonY != -1, "modal dialog filled the layout");
        CheckEqI32(layout.nButtonCount, 2, "two buttons laid out");
        /* The cursor is the per-frame typewriter step: 0 -> 1, not the line length. */
        CheckEqI32(anim.nCursor, 1, "dialog stepped the typewriter cursor");
    }

    /* 6. Unknown screen (0x16): default branch runs, inner switch no-ops, still presents. */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen  = 0x16;
    env.pfnPresent = Hook_Present;
    {
        CursorDirState cur;
        memset(&cur, 0, sizeof(cur));
        env.pCursorDir = &cur;
        (void)Render_Frame(&env);
    }
    CheckEqI32(g_nPresentCalls, 1, "unknown screen still presents");

    /* 7. Render_Frame_DecRef @ 0x36818: decrement the banner-fade counter (Map+0x64), then render
     * one frame -- the timer path PocketUFO_WndProc @ 0xA09AC takes while nUiScreen is 13. */
    printf("Render_Frame_DecRef @ 0x36818\n");
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen  = 0;
    env.pfnPresent = Hook_Present;
    env.nFadeCount = 10; /* the value Tact_EndMission @ 0x351FC seeds into Map+0x64 */
    {
        CursorDirState cur;
        memset(&cur, 0, sizeof(cur));
        env.pCursorDir = &cur;
        Render_Frame_DecRef(&env.nFadeCount, &env);
    }
    CheckEqI32(env.nFadeCount, 9, "counter decremented (0x3681C..0x36820)");
    CheckEqI32(g_nPresentCalls, 1, "the frame was rendered (0x36828)");

    /* The port guards the counter pointer; the original always has the Map context. */
    env.pCursorDir = NULL; /* The prior block's cursor has left scope. */
    Render_Frame_DecRef(NULL, &env);
    CheckEqI32(env.nFadeCount, 9, "NULL counter leaves the value alone");
    CheckEqI32(g_nPresentCalls, 2, "NULL counter still renders the frame");

    /* The order matters at the boundary: with counter 1 the decrement must land BEFORE the frame,
     * so the frame takes the fade-in branch (no pad -> nothing drawn), not the dark-blit branch
     * that a counter of 1 would take if the render came first. */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen  = 0;
    env.pfnPresent = Hook_Present;
    env.nFadeCount = 1;
    {
        CursorDirState cur;
        memset(&cur, 0, sizeof(cur));
        env.pCursorDir = &cur;
        memset(g_surf.pPixels, 0xFF, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);
        Render_Frame_DecRef(&env.nFadeCount, &env);
    }
    CheckEqI32(env.nFadeCount, 0, "counter 1 reaches 0 before the frame runs");
    Check(InkAt(120, 160) == (i32)0xFFFF, "the frame took the fade-in branch, not the dark blit");

    /* Keep the owner counter separate from RenderFrameEnv's snapshot. The helper must refresh
     * the frame view after the decrement; the live World path is checked separately by its owner. */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen = 0;
    env.nFadeCount = 1;
    env.pfnPresent = Hook_Present;
    {
        i32 nBannerFadeCount = 1;

        memset(g_surf.pPixels, 0xFF, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);
        Render_Frame_DecRef(&nBannerFadeCount, &env);
        CheckEqI32(nBannerFadeCount, 0, "separate banner owner reaches 0 before drawing");
        Check(InkAt(120, 160) == (i32)0xFFFF,
              "the refreshed frame snapshot skips the dark blit at counter 0");
    }

    /* Screen 13 also carries a banner-state snapshot. The count owner is separate from both
     * snapshots; a one-pixel pad hook proves the banner passed its positive-count gate. */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen = 13;
    env.nFadeCount = 1;
    env.nFadeIn = 10;
    env.pfnPresent = Hook_Present;
    {
        static u8 aBannerBlob[5];
        static PadImage bannerBackdrop;
        static UiPanel bannerPanelA, bannerPanelB;
        PadImage *pBackdrop = &bannerBackdrop;
        UiControl bannerPad;
        TactHudBannerState banner;
        i32 nBannerFadeCount = 1;

        aBannerBlob[0] = 1; aBannerBlob[1] = 0;
        aBannerBlob[2] = 1; aBannerBlob[3] = 0;
        aBannerBlob[4] = 1;
        memset(pBackdrop, 0, sizeof(*pBackdrop));
        pBackdrop->pData = aBannerBlob;
        pBackdrop->uWidth = 1;
        pBackdrop->uHeight = 1;
        pBackdrop->nMode = 20;
        memset(&bannerPad, 0, sizeof(bannerPad));
        bannerPad.uWidth = 1;
        bannerPad.uHeight = 1;
        bannerPad.bEnabled = 1;
        bannerPad.apPads[0] = pBackdrop;
        memset(&banner, 0, sizeof(banner));
        banner.nFadeCount = 1;
        banner.nReadyFlag = 1;
        (void)snprintf(banner.szBanner, sizeof(banner.szBanner), "READY");
        (void)snprintf(banner.szTimer, sizeof(banner.szTimer), "1");
        memset(&bannerPanelA, 0, sizeof(bannerPanelA));
        memset(&bannerPanelB, 0, sizeof(bannerPanelB));

        env.pBannerPad = &bannerPad;
        env.pBannerBackdrop = pBackdrop;
        env.pBannerState = &banner;
        env.pszBannerFooter = "FOOTER";
        env.pszBannerPadLabel = "READY";
        env.pBannerPanelA = &bannerPanelA;
        env.pBannerPanelB = &bannerPanelB;
        env.pColor = &g_color;
        env.pfnBlit = Hook_BannerPad;
        memset(g_surf.pPixels, 0xFF, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);
        g_aPalette[20][1] = 0xFFFFu;
        g_nBannerPadBlits = 0;

        Render_Frame_DecRef(&nBannerFadeCount, &env);
        CheckEqI32(nBannerFadeCount, 0, "screen 13 count owner reaches 0 before drawing");
        CheckEqI32(g_nBannerPadBlits, 1,
                   "the refreshed screen 13 banner snapshot passes its fade gate");
        Check(InkAt(120, 160) == (i32)0xFFFF,
              "the refreshed screen 13 frame snapshot skips the dark blit");
    }

    /* This direct Render_Frame case is not the live World path: World_TactFrameDraw ticks the
     * cursor before it fills RenderFrameEnv and passes pCursorDir as NULL. */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen = 0;
    env.nFadeCount = 0;
    env.nFadeIn = 9;
    env.pfnPresent = Hook_Present;
    {
        CursorDirState cur;
        static u8 aTickFadeBlob[5];
        PadImage fadePad;

        memset(&cur, 0, sizeof(cur));
        cur.bSlot8Byte2 = 1;
        cur.nSlot9 = 1;
        cur.nSlot10 = 9;
        aTickFadeBlob[0] = 1; aTickFadeBlob[1] = 0;
        aTickFadeBlob[2] = 1; aTickFadeBlob[3] = 0;
        aTickFadeBlob[4] = 1;
        memset(&fadePad, 0, sizeof(fadePad));
        fadePad.pData = aTickFadeBlob;
        fadePad.uWidth = 1;
        fadePad.uHeight = 1;
        fadePad.nMode = 20;
        env.pCursorDir = &cur;
        env.bCursorBusy = true;
        env.pFadePad = &fadePad;
        g_aPalette[20][1] = 0xFFFFu;
        memset(g_surf.pPixels, 0, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);

        (void)Render_Frame(&env);
        CheckEqI32(cur.nSlot10, 10, "the frame cursor tick advances 9 to 10");
        Check(InkAt(0, 0) == 0,
              "the post-tick value 10 suppresses the fade pad for this direct frame call");
    }

    printf("%s\n", g_nFailures == 0 ? "all ok" : "FAILURES");
    return g_nFailures == 0 ? 0 : 1;
}
