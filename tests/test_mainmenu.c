/* test_mainmenu.c - MainMenu_Draw @ 0x1dbb8.
 *
 * The decisions worth pinning are the mode dispatch (4 rows, 7 rows, or none), the row key
 * selection (table value, forced to 255 when the disabled byte is set), the row geometry, the
 * mode 3 side effects (the modal cursor wrap, progressive flag and output layout), and the control
 * blit. Ink is the observable: a row blitted with key 0 leaves the surface alone, and a row
 * blitted with key 255 writes the frame colour exactly. The backdrop blob is drawn into palette
 * slot 20, which this fixture leaves zero, so it never inks. */
#include <stdio.h>
#include <string.h>

#include "game/pufo_font.h"
#include "game/pufo_mainmenu.h"
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
        g_nFailures += 1;
    }
}

/* Records up to 1102 are addressed (the modal line), so the table spans 1103 records. */
#define MM_REC_COUNT 1103

static u8        g_aTextRecs[MM_REC_COUNT * PUFO_TEXT_REC_STRIDE];
static SoftSurf  g_surf;
static i32       g_aKeys[7];
static u8        g_aDisabled[7];
static char      g_aSlotLabels[7][40];
static u8        g_aBgBlob[5];
static UiControl g_startCtl;
static PadImage  g_ctlPad;
static u8        g_aCtlPadBlob[5];
static i32       g_nPhaseSave112;
static UiModalAnim g_modalAnim;
static UiModalLayout g_modalLayout;
static GfxColorEnv g_color;
static MainMenuEnv g_env;
static i32       g_nCtlBlits;

static void CtlHook_BlitPad(const PadImage *pPad, void *pUser)
{
    (void)pPad; (void)pUser;
    ++g_nCtlBlits;
}

static void PutRec(i32 nRec, const char *psz)
{
    pufo_strcpy((char *)(g_aTextRecs + PUFO_TEXT_REC_STRIDE * nRec), psz);
}

static void Fixture_Reset(void)
{
    i32 i;

    memset(g_aKeys, 0, sizeof(g_aKeys));
    memset(g_aDisabled, 0, sizeof(g_aDisabled));
    memset(g_aSlotLabels, 0, sizeof(g_aSlotLabels));
    memset(&g_startCtl, 0, sizeof(g_startCtl));
    memset(&g_ctlPad, 0, sizeof(g_ctlPad));
    memset(&g_modalAnim, 0, sizeof(g_modalAnim));
    memset(&g_modalLayout, 0, sizeof(g_modalLayout));
    memset(&g_color, 0, sizeof(g_color));
    memset(&g_env, 0, sizeof(g_env));

    g_nPhaseSave112 = -1;
    g_modalLayout.nButtonX[0] = 17;
    g_modalLayout.nButtonX[1] = 23;
    g_modalLayout.nButtonY = 29;
    g_modalLayout.nButtonCount = 99;
    g_nCtlBlits = 0;

    for (i = 0; i < 7; ++i) {
        (void)snprintf(g_aSlotLabels[i], sizeof(g_aSlotLabels[i]), "slot%d", i);
    }

    /* 1 by 1 backdrop glyph; palette slot 20 is zero, so it lays no ink either way. */
    g_aBgBlob[0] = 1; g_aBgBlob[1] = 0;
    g_aBgBlob[2] = 1; g_aBgBlob[3] = 0;
    g_aBgBlob[4] = 0x30;

    /* The standalone control, made drawable: mode 0, state 1, a pad in slot 1. */
    g_aCtlPadBlob[0] = 1; g_aCtlPadBlob[1] = 0;
    g_aCtlPadBlob[2] = 1; g_aCtlPadBlob[3] = 0;
    g_aCtlPadBlob[4] = 0xFF;   /* the key skips it, so the control blits without inking */
    PadImage_ctor(&g_ctlPad);
    g_ctlPad.pData   = g_aCtlPadBlob;
    g_ctlPad.uWidth  = 1;
    g_ctlPad.uHeight = 1;
    g_startCtl.x        = 10;
    g_startCtl.y        = 200;
    g_startCtl.uWidth   = 50;
    g_startCtl.nMode    = 0;
    g_startCtl.bEnabled = 1;
    g_startCtl.nState   = 1;
    g_startCtl.apPads[1] = &g_ctlPad;

    g_modalAnim.pColor = &g_color;

    g_env.pBgGlyph            = g_aBgBlob;
    g_env.anRowKeys           = g_aKeys;
    g_env.abRowDisabled       = g_aDisabled;
    g_env.pSlotLabels         = &g_aSlotLabels[0][0];
    g_env.pStartCtl           = &g_startCtl;
    g_env.pTextRecs           = g_aTextRecs;
    g_env.pnActionPhaseSave112 = &g_nPhaseSave112;
    g_env.pModalAnim          = &g_modalAnim;
    g_env.pModalLayout        = &g_modalLayout;
    g_env.pColor              = &g_color;
    g_env.pfnCtlBlit          = CtlHook_BlitPad;
    g_env.pfnCtlBlitEx        = CtlHook_BlitPad;

    memset(g_surf.pPixels, 0, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);
}

static i32 InkAt(i32 x, i32 y)
{
    const u16 *p = (const u16 *)g_surf.pPixels;

    return (i32)p[y * g_surf.nWidth + x];
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    SoftSurf_ctor(&g_surf);
    SoftSurf_AllocPixels(&g_surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_surf;
    Render_SetTarget(240, 320, 2, 2, 480, g_surf.pPixels);

    PutRec(PUFO_MAINMENU_REC_MENU0, "new");
    PutRec(PUFO_MAINMENU_REC_MENU0 + 1, "load");
    PutRec(PUFO_MAINMENU_REC_MENU0 + 2, "credits");
    PutRec(PUFO_MAINMENU_REC_MENU0 + 3, "quit");
    PutRec(PUFO_MAINMENU_REC_MENU1, "really?");
    PutRec(PUFO_MAINMENU_REC_CTL, "back");
    PutRec(PUFO_MAINMENU_REC_MODAL, "AB");

    printf("MainMenu_Draw @ 0x1dbb8\n");

    /* --- key 0 rows blend nothing: the surface keeps its contents --- */
    Fixture_Reset();
    g_env.nListMode = PUFO_MAINMENU_MODE_MAIN;
    MainMenu_Draw(&g_env);
    CheckEqI32(InkAt(50, 115), 0, "a row keyed 0 leaves the body pixel alone");
    CheckEqI32(InkAt(49, 115), 0, "and so its strips");

    /* --- the disabled byte forces the key to 255, which writes the frame colour exactly --- */
    Fixture_Reset();
    g_env.nListMode = PUFO_MAINMENU_MODE_MAIN;
    g_aDisabled[1] = 1;
    MainMenu_Draw(&g_env);
    Check(InkAt(50, 137) != 0, "row 1 disabled: body inked at (50, 115 + 22)");
    Check(InkAt(50, 136) != 0, "top strip at y - 1");
    Check(InkAt(50, 155) != 0, "bottom strip at y + 18");
    Check(InkAt(49, 137) != 0, "left strip at x - 1");
    Check(InkAt(190, 137) != 0, "right strip at x + 140");
    CheckEqI32(InkAt(50, 115), 0, "row 0, not disabled and keyed 0, still draws nothing");

    /* --- the key table is used when the disabled byte is clear --- */
    Fixture_Reset();
    g_env.nListMode = PUFO_MAINMENU_MODE_MAIN;
    g_aKeys[0] = 128;
    MainMenu_Draw(&g_env);
    Check(InkAt(50, 115) != 0, "a table key of 128 blends half the body colour in");
    CheckEqI32(InkAt(50, 137), 0, "while the keyed-0 row below stays clean");

    /* --- mode 1 keeps the geometry but uses the other label records --- */
    Fixture_Reset();
    g_env.nListMode = PUFO_MAINMENU_MODE_SLOTCLICK;
    g_aDisabled[3] = 1;
    MainMenu_Draw(&g_env);
    Check(InkAt(50, 181) != 0, "mode 1 still draws four rows at 50 / 115 + 22 * i");
    CheckEqI32(g_nCtlBlits, 0, "and no control, which is the slot layout's job");

    /* --- modes 2 and 3 draw seven slot rows and the control --- */
    Fixture_Reset();
    g_env.nListMode = PUFO_MAINMENU_MODE_LOADLIST;
    g_aDisabled[6] = 1;
    MainMenu_Draw(&g_env);
    Check(InkAt(25, 197) != 0, "row 6 at (25, 65 + 22 * 6)");
    Check(InkAt(24, 197) != 0, "its left strip at x - 1");
    Check(InkAt(215, 197) != 0, "its right strip at x + 190");
    CheckEqI32(g_nCtlBlits, 1, "the standalone control blits once");
    CheckEqI32(g_nPhaseSave112, -1, "mode 2 leaves anActionPhaseSave0[112] alone");
    CheckEqI32(g_modalAnim.nCursor, 0, "and never enters the modal");

    /* --- mode 3 loads the cursor from phase 40; UiModal_DrawDialog wraps it to 1. --- */
    {
        static u8 aTestGlyph[4] = { 0, 0, 27, 0xFF };
        static u8 *apTestGlyphs[256];
        PufoFont savedFont = g_fontSys;
        FontMetrics savedMetrics = g_fontMetrics;
        const i32 nSavedGlyphPalBase = g_nGlyphPalSlotBase;

        memset(&g_fontSys, 0, sizeof(g_fontSys));
        memset(&g_fontMetrics, 0, sizeof(g_fontMetrics));
        for (i32 i = 0; i < 256; ++i) {
            apTestGlyphs[i] = aTestGlyph;
            g_fontMetrics.aAdvance[i] = 1;
        }
        g_fontSys.apSprIndex = apTestGlyphs;
        g_fontSys.nSprIndexCount = 256;
        g_fontSys.nGlyphBase = 0;

        Fixture_Reset();
        g_env.nListMode = PUFO_MAINMENU_MODE_LOADING;
        MainMenu_Draw(&g_env);
        CheckEqI32(g_nPhaseSave112, 1,
                   "mode 3 stores the modal cursor after its 40-to-1 wrap");
        CheckEqI32(g_modalAnim.nCursor, 1, "the modal cursor wraps 40 to 1");
        CheckEqI32(g_modalAnim.bProgressive, 0, "the cursor wrap clears progressive draw");
        CheckEqI32(g_modalLayout.nButtonCount, 0, "the loading modal has no button layout");
        CheckEqI32(g_modalLayout.nButtonX[0], 17, "no-button layout leaves button X0 alone");
        CheckEqI32(g_modalLayout.nButtonX[1], 23, "no-button layout leaves button X1 alone");
        CheckEqI32(g_modalLayout.nButtonY, 29, "no-button layout leaves button Y alone");
        Check(InkAt(120, 150) != 0, "the loading modal body draws on the surface");
        Check(InkAt(52, 151) != InkAt(51, 151),
              "the loading text draws a glyph at its first text position");

        g_fontSys = savedFont;
        g_fontMetrics = savedMetrics;
        g_nGlyphPalSlotBase = nSavedGlyphPalBase;
    }

    /* --- an unknown mode draws no rows at all --- */
    Fixture_Reset();
    g_env.nListMode = 4;
    g_aDisabled[0] = 1;   /* would ink if any row ran */
    MainMenu_Draw(&g_env);
    CheckEqI32(InkAt(50, 115), 0, "mode 4 draws no menu rows");
    CheckEqI32(InkAt(25, 65), 0, "and no slot rows");
    CheckEqI32(g_nCtlBlits, 0, "and no control");
    CheckEqI32(g_nPhaseSave112, -1, "and no modal");

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
