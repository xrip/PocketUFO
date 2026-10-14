/* test_renderframe.c - Render_Frame @ 0x22310, the frame dispatcher.
 *
 * The dispatcher is verified through its observable consequences: which hook ran (geoscape,
 * present), the glyph palette base the default branch writes (34 at 0x22370), the cursor tick
 * that only the default branch performs, the modal layout the dialog fills, the fade ink on the
 * surface, and - for the menu branch - the mode-3 side effect MainMenu_Draw owns (action phase
 * 112 = 40), which proves the branch reached MainMenu_Draw through the dispatcher.
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
    }
}

static SoftSurf g_surf;
static GfxColorEnv g_color;
static i32 g_nGeoscapeCalls;
static i32 g_nPresentCalls;

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

    g_mm.pBgGlyph             = g_aBgBlob;
    g_mm.nListMode            = 3;
    g_mm.anRowKeys            = g_aKeys;
    g_mm.abRowDisabled        = g_aDisabled;
    g_mm.pSlotLabels          = &g_aSlotLabels[0][0];
    g_mm.pStartCtl            = &g_startCtl;
    g_mm.pTextRecs            = g_aTextRecs;
    g_mm.pnActionPhaseSave112 = &g_nPhaseSave112;
    g_mm.pModalAnim           = &g_modalAnim;
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

    /* 2. Screen 0x11: the menu branch. Mode 3's action-phase write proves MainMenu_Draw ran;
     * the glyph base must NOT be 34 (only the default branch writes it). */
    ResetHooks();
    memset(&env, 0, sizeof(env));
    env.nUiScreen  = 0x11;
    env.pMainMenu  = &g_mm;
    env.pfnPresent = Hook_Present;
    g_nGlyphPalSlotBase = 0;
    (void)Render_Frame(&env);
    CheckEqI32(g_nPresentCalls, 1, "menu branch presents");
    CheckEqI32(g_nGeoscapeCalls, 0, "menu branch is not geoscape");
    CheckEqI32(g_nPhaseSave112, 40, "MainMenu_Draw mode 3 ran through the dispatcher");
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
        Check(InkAt(0, 0) != 0, "fade-in blits the fade pad with partial alpha");
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

    printf("%s\n", g_nFailures == 0 ? "all ok" : "FAILURES");
    return g_nFailures == 0 ? 0 : 1;
}
