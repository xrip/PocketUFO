/* pufo_app.c - Win64 entry point and window procedure.
 *
 * Evidence: WinMain @ 0x9fcc4, InitInstance @ 0x9fd48,
 *           PocketUFO_WndProc @ 0xa03ec, App_QuitDisplay @ 0xa0388.
 *
 * This shell opens the window, loads the startup assets, opens the display
 * surface, and runs the message loop. The game state machine and the per-screen
 * render call belong to the UI/strategy tasks; until those land this shows the
 * loaded 240x320 surface so the data path is provably live.
 *
 * Ported from the original:
 *   - single instance via FindWindowW(class "POCKET_UFO", title "POCKET UFO")
 *   - RegisterClassW with CS_VREDRAW|CS_HREDRAW
 *   - WS_VISIBLE window sized to the display
 *   - accelerator table load + TranslateAcceleratorW
 *
 * Deliberately dropped (no Win64 equivalent), with reasons:
 *   - the CE idiom SetForegroundWindow(hWnd | 1)
 *   - ExtEscape(GETVFRAMEPHYSICAL) raw-framebuffer probe
 *   - GX display-mode / pitch-sign handling (the DIB is always top-down RGB565)
 *   - the unsupported-resolution early quit: that existed only because GAPI
 *     could not scale; Win64 letterboxes instead.
 */
#include "pufo_common.h"
#include "audio/pufo_audio.h"
#include "game/pufo_boot.h"
#include "game/pufo_clock.h"    /* GameTime, the scratch clock SaveSlot_FormatList uses */
#include "game/pufo_font.h"     /* PufoFont, the shell owns the fontSys block */
#include "game/pufo_input.h"    /* Input_OnPointer / Input_OnPointerUp, the menu click path */
#include "app/pufo_world.h"   /* PufoWorldState, the geoscape bring-up state */
#include "game/pufo_mainmenu.h" /* GameUI_InitStartup / MainMenu_Draw / MainMenu_TickRowKeys */
#include "game/pufo_options.h"  /* Options_LoadFromRegistry */
#include "game/pufo_renderframe.h" /* Render_Frame, the frame dispatcher */
#include "game/pufo_save.h"     /* SaveSlot_FormatList, the load-slot list labels */
#include "game/pufo_timer.h"    /* Timer_SetWindow, the ids the WM_TIMER branch dispatches on */
#include "game/pufo_titles.h"   /* Titles_Init / TitlesAnimation, the credits screen */
#include "platform/pufo_gx.h"
#include "render/pufo_render.h"
#include "ui/pufo_control.h"    /* UiControl_LoadPads, the start controls' pad loader */
#include "ui/pufo_menubtn.h"    /* MenuCtl*_OnClick / Menu_LoadSlot / StartUi_Shutdown */
#include "util/pufo_rand.h"
#include "util/pufo_str.h"

#include <stdio.h>
#include <stdlib.h> /* _wtoi, the --click coordinates */
#include <string.h>

#include <windows.h>

#define PUFO_CLASS_NAME  L"POCKET_UFO" /* ClassName @ 0xac6a8 */
#define PUFO_WINDOW_NAME L"POCKET UFO" /* WindowName @ 0xa5000 */

static HWND       g_hWndMain = NULL;
static GxSurface  g_gx;
static BootAssets g_assets;
static bool       g_bAssetsLoaded;
static PufoWorldState g_world;
static bool       g_bWorldReady;
static char       g_szLangPath[MAX_PATH];
static char       g_szMainPath[MAX_PATH];

/* The start-menu state GameUI_InitStartup builds and MainMenu_Draw reads. The shell owns the
 * blocks the original keeps in its global bank: the widget bank (pBank @ 0xAC6D0 — the 15 menu
 * controls are its widgets 113..127 at 0xAD4F0, which is what Input_OnPointerUp and
 * StartUi_Shutdown index), the standalone control (0x1571B4, heap-allocated like the original's
 * operator new), the backdrop glyph (0x1568D8), the empty-button pad (0x1568EC), the seven row
 * keys (0x1568F0), the seven hover bytes (0x156AB0 — modelled by the input world's abListHit),
 * the seven key sources (0x1571C8), the slot-label block (0x156938), the list mode (0x157188)
 * and the ui screen (Map_nUiScreen). The font is the module global g_fontSys (fontSys @ 0x158268):
 * Text_DrawString reaches it directly, so the shell must not keep a private copy. */
static PufoOptions      g_options;
static UiWidgetBank     g_bank;
static UiControl       *g_pStartCtl;        /* MainMenu_pStartCtl @ 0x1571B4 */
static u8              *g_pBgGlyph;
static u8              *g_pEmptyPad;
static i32              g_aRowKeys[7];
static i32              g_aRowKeySrc[7];
static i32              g_nListMode;
static i32              g_nUiScreen;
static i32              g_nAnimPhase;       /* unk_1139EF, the 0xC352 anim counter */
static i32              g_nPlaneStatRowCount; /* nPlaneStatRowCount @ 0x161A38. One global, two unrelated
                             * uses in the original: the start-menu flicker reuses it as scratch (0xA0AEC
                             * stores nMapPhase into it, 0xA0B14 adds a random roll), while the plane
                             * screen uses it as its stat-panel row count (written at 0x79D24 / 0x79DE4,
                             * read at 0x79E30..0x79E90). Named after the global, not after one of its uses. */
static i32              g_nPhaseSave112;
static u32              g_nUiSlot0;         /* apUiSlotsA[0] @ Map+0x60, cleared by UiControl_HitTest */
static GfxColorEnv      g_color;
static bool             g_bStartupUi;
static char             g_aSlotLabels[7][40]; /* MainMenu_pSlotCtx @ 0x156938 */
static GameTime         g_gameTime;
static u8               g_bGameState;       /* bGameState @ 0x15821C */
static char             g_szAppDir[MAX_PATH];
static TactInputState   g_tactState;
static TactInputHooks   g_tactHooks;
static TactPointerWorld g_pointerWorld;
static StartMenuHooks   g_startHooks;
static SaveSlotFormatCtx g_slotCtx;
/* The credits state block (g_pTitlesState @ 0x158804, 0x154C0 bytes in the original). The particle
 * array holds (84020 - 20) / 28 = 3000 records before the star array starts; 190 stars is the
 * ctor's own count. */
static TitlesAnimation  g_titlesAnim;
static TitlesParticle   g_aTitlesParticles[3000];
static TitlesStar       g_aTitlesStars[190];
static i32              g_nTitlesMaxRows;
static i32              g_aTitlesRowRec[44];   /* aTitlesRowRecs @ 0xB35D8 */
static u8              *g_apTitlesGlyphs[5];   /* unk_1571A0..0x1571B0 */

static void App_PresentLoadedSurface(void);

static bool App_LoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                         const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2); /* UiControl_LoadPads @ 0x1a604 */
    return true;
}

static void App_BlitPad(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    PadImage_Blit(pPad);
}

static void App_BlitPadEx(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    PadImage_BlitEx(pPad, 255u);
}

/* --- the menu-button hook blocks -------------------------------------------------------
 *
 * MenuCtl0..3 (0x1a09c/0x1a034/0x1a064/0x1a080) play sample 16, run StartUi_Shutdown, then
 * branch: new game (Geoscape_InitUI 0x857f4, not ported), load list (SaveSlot_FormatList,
 * ported), credits (Titles_Init 0x380d8, not ported), quit (App_QuitDisplay). The hooks the
 * ported functions already take are filled here; the two unported callees print a notice. */
static void App_HookPlaySample(u32 nSample, void *pUser)
{
    (void)pUser;
    Sound_Play((i32)nSample);
}

static void App_HookPlaySampleI(i32 nSample, void *pUser)
{
    (void)pUser;
    Sound_Play(nSample);
}

static void App_HookSoundShutdown(void *pUser)
{
    (void)pUser;
    Sound_Shutdown(); /* 0x9e3d4 */
}

static void App_HookKillTimersB(void *pUser)
{
    (void)pUser;
    Timer_KillB(); /* 0x9e754 */
}

static void App_HookFontFree(void *pFont, void *pUser)
{
    (void)pUser;
    PufoFont_Free((PufoFont *)pFont); /* 0x46144 */
}

static void *App_HookFree(void *pBlock, void *pUser)
{
    (void)pUser;
    free(pBlock); /* the original's operator delete */
    return NULL;
}

/* The new-game path: Geoscape_InitUI @ 0x857f4 mode 0, whose hook runs Game_StartNew
 * @ 0x94554 over the shell's PufoWorldState. The state line is the shell's own report (a
 * DEVIATION: the original shows the geoscape instead of printing); it is what makes the
 * bring-up observable from a headless run. */
static void App_HookNewGame(void *pUser)
{
    WorldHooks hooks;

    (void)pUser;
    if (!g_bWorldReady) {
        return;
    }

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadPads = App_LoadPads;
    hooks.pfnPlaySample = App_HookPlaySampleI; /* the message-list samples */

    (void)World_NewGame(&g_world, &g_assets, &g_fontSys, &g_options, &hooks);
    printf("pocketufo: new game: %d funds, %d bases, %d soldiers, %d craft, screen %d, zoom %d\n",
           (int)g_world.nFunds, (int)g_world.nBaseCount, (int)g_world.nSoldierCount,
           (int)g_world.bCraftCount, (int)g_world.screen.nGeoscapeScreen, (int)g_world.view.nMode);
    fflush(stdout);
}

static void App_HookFormatSlots(void *pUser)
{
    (void)pUser;
    SaveSlot_FormatList(&g_slotCtx); /* 0x45904 */
}

static void App_HookTitles(void *pUser)
{
    TitlesInitEnv env;
    u16           nScreen;

    (void)pUser;
    nScreen = 0;
    memset(&g_titlesAnim, 0, sizeof(g_titlesAnim));
    g_titlesAnim.aParticles      = g_aTitlesParticles;
    g_titlesAnim.aStars          = g_aTitlesStars;
    g_titlesAnim.nFillPalSlot    = 0;  /* the original fills with MEMORY[0x1472D0] */
    g_titlesAnim.nPresentPalSlot = 12; /* fade.pal, loaded by Titles_Init itself */

    memset(&env, 0, sizeof(env));
    env.pFont      = &g_fontSys;
    env.pAnim      = &g_titlesAnim;
    env.pnMaxRows  = &g_nTitlesMaxRows;
    env.anRowRec   = g_aTitlesRowRec;
    env.apGlyphs   = g_apTitlesGlyphs;
    env.puUiScreen = &nScreen;
    env.pOptions   = &g_options;
    Titles_Init(&env); /* Titles_Init @ 0x380d8 */
    g_nUiScreen = nScreen; /* 16 */

    /* The credit-lines env mirrors the globals the original's Titles_DrawCreditLines reads: the
     * row table, the glyph blobs, the row count and the text records. Its scroll is aliased from
     * the state block inside Titles_DrawAnimatedFrame. */
    g_titlesAnim.Credits.anRowRec   = g_aTitlesRowRec;
    g_titlesAnim.Credits.apGlyphs   = (const u8 *const *)g_apTitlesGlyphs;
    g_titlesAnim.Credits.pTextRecs  = g_assets.pTextRecs;
    g_titlesAnim.Credits.pColor     = &g_color;
    g_titlesAnim.Credits.nMaxRows   = g_nTitlesMaxRows;
}

static void App_HookQuit(void *pUser)
{
    (void)pUser;
    (void)PostQuitMessage(0); /* the shell's App_QuitDisplay @ 0xa0388 equivalent */
}

static void App_DrawFrame(void);        /* defined below with the render path */
static void App_ShutdownStartUi(void *pUser); /* defined below with the start-UI teardown */

static void App_DrawFrameHook(void *pUser)
{
    (void)pUser;
    App_DrawFrame();
}

/* LoadGame @ 0x44bbc over the shell's world state. The slot path is szAppDir + "sav" + slot +
 * ".sav"; the shell uses the working directory (DEVIATION: the original resolves szAppDir from
 * the exe path in InitInstance). The state line is the shell's own report, as in App_HookNewGame. */
static i32 App_HookLoadGame(i32 nSlot, void *pUser)
{
    WorldHooks hooks;

    (void)pUser;
    if (!g_bWorldReady) {
        return 0;
    }

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadPads = App_LoadPads;
    hooks.pfnPlaySample = App_HookPlaySampleI;
    hooks.pfnRenderFrame = App_DrawFrameHook;
    hooks.pfnStartUiShutdown = App_ShutdownStartUi;

    {
        i32 nRet = World_LoadSlot(&g_world, "", nSlot, &g_assets, &g_options, &hooks);
        printf("pocketufo: load slot %d: %s (%d funds, %d bases, %d soldiers, %d craft, ui screen %d)\n",
               (int)nSlot, nRet != 0 ? "loaded" : "no save", (int)g_world.nFunds,
               (int)g_world.saveBases.bBaseCount, (int)g_world.nSoldierCount,
               (int)g_world.bCraftCount, (int)g_world.nUiScreen);
        fflush(stdout);
        return nRet;
    }
}

static void App_HookClearDrawMode(void *pUser)
{
    (void)pUser; /* EmptyBtn_OnClick's geoscape draw-mode clear has no shell state yet */
}

/* StartUi_Shutdown @ 0x38d7c for the shell: frees the bank tail pads, the font, the standalone
 * control and the two blocks, then sets the ui screen to 22. */
static void App_ShutdownStartUi(void *pUser)
{
    StartUiShutdownEnv env;

    (void)pUser;
    memset(&env, 0, sizeof(env));
    env.pBank             = &g_bank.aCtl[0];
    env.pStartCtl         = g_pStartCtl;
    env.pFont             = &g_fontSys;
    env.pBlockA           = g_pBgGlyph;
    env.pBlockB           = g_pEmptyPad;
    env.pnUiScreen        = &g_nUiScreen;
    env.hooks.pfnSoundShutdown = App_HookSoundShutdown;
    env.hooks.pfnKillTimers    = App_HookKillTimersB;
    env.hooks.pfnFontFree      = App_HookFontFree;
    env.hooks.pfnFree          = App_HookFree;
    StartUi_Shutdown(&env);

    g_pStartCtl = NULL;
    g_pBgGlyph  = NULL;
    g_pEmptyPad = NULL;
    g_bStartupUi = false;
}

static void App_MenuCtl0Click(void) { MenuCtl0_OnClick(&g_startHooks); }
static void App_MenuCtl1Click(void) { MenuCtl1_OnClick(&g_nListMode, &g_startHooks); }
static void App_MenuCtl2Click(void) { MenuCtl2_OnClick(&g_startHooks); }
static void App_MenuCtl3Click(void) { MenuCtl3_OnClick(&g_startHooks); }

/* MenuCtl8..14 (0x1a0bc..0x1a1f4): the seven load slots. */
static void App_Slot0Click(void) { (void)Menu_LoadSlot(&g_nListMode, 0, App_HookLoadGame, NULL); }
static void App_Slot1Click(void) { (void)Menu_LoadSlot(&g_nListMode, 1, App_HookLoadGame, NULL); }
static void App_Slot2Click(void) { (void)Menu_LoadSlot(&g_nListMode, 2, App_HookLoadGame, NULL); }
static void App_Slot3Click(void) { (void)Menu_LoadSlot(&g_nListMode, 3, App_HookLoadGame, NULL); }
static void App_Slot4Click(void) { (void)Menu_LoadSlot(&g_nListMode, 4, App_HookLoadGame, NULL); }
static void App_Slot5Click(void) { (void)Menu_LoadSlot(&g_nListMode, 5, App_HookLoadGame, NULL); }
static void App_Slot6Click(void) { (void)Menu_LoadSlot(&g_nListMode, 6, App_HookLoadGame, NULL); }

static void App_EmptyBtnClick(void)
{
    EmptyBtn_OnClick(App_HookPlaySampleI, App_HookClearDrawMode, NULL); /* 0x1a228 */
}

/* GameUI_InitStartup @ 0x38a28 for the shell. The menu controls live in the widget bank at
 * 113..127 so Input_OnPointerUp (0x148d0) and StartUi_Shutdown reach the same controls the
 * original's 0xAD4F0 block is. */
static void App_InitStartupUi(void)
{
    GameUiStartupEnv env;

    Options_LoadFromRegistry(&g_options);
    Timer_SetWindow(g_hWndMain);

    g_pStartCtl = (UiControl *)malloc(sizeof(UiControl)); /* the original's operator new(0x20) */

    memset(&env, 0, sizeof(env));
    memset(&g_bank, 0, sizeof(g_bank));
    env.pFont          = &g_fontSys; /* fontSys @ 0x158268 — the same object Text_DrawString reads */
    env.paMenuControls = &g_bank.aCtl[113]; /* 0xAD4F0 = pBank + 32*113 */
    env.pStartCtl      = g_pStartCtl;
    env.ppBgGlyph      = &g_pBgGlyph;
    env.ppEmptyBtnPad  = &g_pEmptyPad;
    env.anRowKeys      = g_aRowKeys;
    env.abRowDisabled  = g_pointerWorld.abListHit; /* unk_156AB0, shared with the input path */
    env.anRowKeySrc    = g_aRowKeySrc;
    env.pnListMode     = &g_nListMode;
    env.pnUiScreen     = &g_nUiScreen;
    env.pOptions       = &g_options;
    env.pfnLoadPads    = App_LoadPads;
    env.apfnMenuClick[0]  = App_MenuCtl0Click;
    env.apfnMenuClick[1]  = App_MenuCtl1Click;
    env.apfnMenuClick[2]  = App_MenuCtl2Click;
    env.apfnMenuClick[3]  = App_MenuCtl3Click;
    env.apfnMenuClick[8]  = App_Slot0Click;
    env.apfnMenuClick[9]  = App_Slot1Click;
    env.apfnMenuClick[10] = App_Slot2Click;
    env.apfnMenuClick[11] = App_Slot3Click;
    env.apfnMenuClick[12] = App_Slot4Click;
    env.apfnMenuClick[13] = App_Slot5Click;
    env.apfnMenuClick[14] = App_Slot6Click;
    env.pfnEmptyClick  = App_EmptyBtnClick;
    GameUI_InitStartup(&env);
    g_bStartupUi = (g_nUiScreen == 17);

    /* The input world: the screen-17 list state the pointer handlers read and write. */
    memset(&g_pointerWorld, 0, sizeof(g_pointerWorld));
    g_pointerWorld.pBank          = &g_bank;
    g_pointerWorld.pListCtl       = g_pStartCtl;
    g_pointerWorld.nListMode      = g_nListMode;
    g_pointerWorld.bListRowSticky = false; /* unk_188D4C */
    g_pointerWorld.hit.pnUiSlot0  = &g_nUiSlot0;
    g_pointerWorld.hit.pfnBlit    = App_BlitPad;
    g_pointerWorld.hit.pfnBlitEx  = App_BlitPadEx;

    memset(&g_tactState, 0, sizeof(g_tactState));
    memset(&g_tactHooks, 0, sizeof(g_tactHooks));

    /* The menu-button hooks. */
    memset(&g_startHooks, 0, sizeof(g_startHooks));
    g_startHooks.pfnPlaySample      = App_HookPlaySample;
    g_startHooks.pfnShutdownStartUi = App_ShutdownStartUi;
    g_startHooks.pfnNewGame         = App_HookNewGame;
    g_startHooks.pfnFormatSlots     = App_HookFormatSlots;
    g_startHooks.pfnTitles          = App_HookTitles;
    g_startHooks.pfnQuit            = App_HookQuit;

    /* The load-slot label context (SaveSlot_FormatList @ 0x45904). */
    memset(&g_slotCtx, 0, sizeof(g_slotCtx));
    g_slotCtx.apLines      = g_aSlotLabels;
    g_slotCtx.pszSaveDir   = g_szAppDir;
    g_slotCtx.pTime        = &g_gameTime;
    g_slotCtx.pbGameState  = &g_bGameState;
    g_slotCtx.pszPrefix    = (const char *)(g_assets.pTextRecs + PUFO_TEXT_REC_STRIDE * 1800);
    g_slotCtx.pszNotSaved  = (const char *)(g_assets.pTextRecs + PUFO_TEXT_REC_STRIDE * 1074);
    g_slotCtx.pszMission   = (const char *)(g_assets.pTextRecs + PUFO_TEXT_REC_STRIDE * 1075);
}

/* The shell's stand-in for GX_Present @ 0x27788 (the thunk to GX_PresentFrame @ 0x9ea7c):
 * copy the logical surface to the window DIB and present it. */
static i32 App_PresentHook(void *pUser)
{
    (void)pUser;
    App_PresentLoadedSurface();
    (void)Gx_Present(&g_gx);
    return 0;
}

/* Render_Frame @ 0x22310 for the shell. The shell only ever reaches the start-menu screen
 * (17); the tactical branches need the subsystem envs the shell does not own yet. */
static void App_DrawFrame(void)
{
    MainMenuEnv    mm;
    RenderFrameEnv rf;

    /* Screen 17 is the start menu (GameUI_InitStartup); 0x10 is the credits (Titles_Init). No
     * other screen has a shell env yet. */
    if (g_nUiScreen != 17 && g_nUiScreen != 0x10) {
        return;
    }
    memset(&mm, 0, sizeof(mm));
    mm.pBgGlyph             = g_pBgGlyph;
    mm.nListMode            = g_nListMode;
    mm.anRowKeys            = g_aRowKeys;
    mm.abRowDisabled        = g_pointerWorld.abListHit; /* the hover flags the pointer path sets */
    mm.pSlotLabels          = &g_aSlotLabels[0][0];     /* modes 2/3 only */
    mm.pStartCtl            = g_pStartCtl;
    mm.pTextRecs            = g_assets.pTextRecs;
    mm.pnActionPhaseSave112 = &g_nPhaseSave112;
    mm.pModalAnim           = NULL; /* mode 3 only */
    mm.pColor               = &g_color;
    mm.pfnCtlBlit           = App_BlitPad;
    mm.pfnCtlBlitEx         = App_BlitPadEx;

    memset(&rf, 0, sizeof(rf));
    rf.nUiScreen   = g_nUiScreen;
    rf.pMainMenu   = &mm;
    rf.pTitlesAnim = &g_titlesAnim;
    rf.pfnPresent  = App_PresentHook;
    (void)Render_Frame(&rf);
}

/* The WM_TIMER 0xC352 body (PocketUFO_WndProc @ 0xA0944): advance the anim phase counter
 * (0xA0978..0xA098C, >=7 wraps to 0), then per screen: 17 runs the row-key flicker (0xA0AC8),
 * 0x10 advances the credits scroll by one and wraps it past 20 * (nTitlesMaxRows + 48) (0xA0C58). */
static void App_TickAnim(void)
{
    if (g_nAnimPhase >= 7) {
        g_nAnimPhase = 0; /* 0xA0988 */
    } else {
        ++g_nAnimPhase;   /* 0xA098C */
    }
    if (g_nUiScreen == 17) {
        MainMenu_TickRowKeys(g_aRowKeys, g_aRowKeySrc, g_nAnimPhase, &g_nPlaneStatRowCount);
    } else if (g_nUiScreen == 0x10) {
        if (++g_titlesAnim.nScroll > 20 * (g_nTitlesMaxRows + 48)) {
            g_titlesAnim.nScroll = 0;
        }
    }
}

LRESULT CALLBACK PocketUFO_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static void App_PresentLoadedSurface(void);

/* Client pixels to the 240x320 logical surface, inverting Gx_Present's integer-scale
 * letterbox. The original's WinCE pointers were already in logical space (WM_LBUTTONDOWN
 * feeds Input_OnPointer straight through, 0xa03ec). */
static void App_MapPointer(LPARAM lParam, i32 *pnX, i32 *pnY)
{
    RECT  rc;
    int   nWinW, nWinH, nScale, nOX, nOY;

    if (!GetClientRect(g_hWndMain, &rc)) {
        *pnX = 0;
        *pnY = 0;
        return;
    }
    nWinW = rc.right - rc.left;
    nWinH = rc.bottom - rc.top;
    nScale = nWinW / 240;
    if (nWinH / 320 < nScale) {
        nScale = nWinH / 320;
    }
    if (nScale < 1) {
        nScale = 1;
    }
    nOX = (nWinW - 240 * nScale) / 2;
    nOY = (nWinH - 320 * nScale) / 2;
    *pnX = ((i32)(short)LOWORD(lParam) - nOX) / nScale;
    *pnY = ((i32)(short)HIWORD(lParam) - nOY) / nScale;
}

/* Input_OnPointer @ 0x13c6c for the shell, in logical 240x320 coordinates. On screen 17 this
 * is the hover path: it marks the list rows under the pointer in abListHit, which both
 * brightens the row (key forced to 255) and, on release, selects which row's click handler
 * fires. */
static void App_PointerLogical(i32 nX, i32 nY, bool bDown)
{
    if (!g_bStartupUi) {
        return;
    }
    g_tactState.nUiScreen  = (u16)g_nUiScreen;
    g_tactState.bGeoscapeUi = false;
    g_pointerWorld.nListMode = g_nListMode;
    Input_OnPointer(&g_tactState, &g_tactHooks, &g_pointerWorld, nX, nY, bDown);
}

/* WM_MOUSEMOVE (bDown false) and WM_LBUTTONDOWN (bDown true). */
static void App_OnPointer(LPARAM lParam, bool bDown)
{
    i32 nX, nY;

    App_MapPointer(lParam, &nX, &nY);
    App_PointerLogical(nX, nY, bDown);
}

/* Input_OnPointerUp @ 0x148d0 for the shell: WM_LBUTTONUP. It takes no coordinates; the
 * row marked under the pointer by the hover path is the one whose handler runs. */
static void App_OnPointerUp(void)
{
    if (!g_bStartupUi) {
        return;
    }
    g_tactState.nUiScreen   = (u16)g_nUiScreen;
    g_tactState.bGeoscapeUi = false;
    g_pointerWorld.nListMode = g_nListMode;
    Input_OnPointerUp(&g_tactState, &g_tactHooks, &g_pointerWorld);
}

/* Builds the exe-directory-relative asset paths, as InitInstance @ 0x9fd48 does:
 * GetModuleFileNameW, strip to the last backslash, then append lang.dat and
 * main.dat. The originals are szAppDir @ 0x1883b0 and szPathBuf @ 0x188538. */
static void App_BuildAssetPaths(void)
{
    WCHAR  awcPath[MAX_PATH];
    char   szDir[MAX_PATH];
    DWORD  cch = GetModuleFileNameW(NULL, awcPath, MAX_PATH);
    DWORD  i;
    size_t nDir = 0;

    g_szLangPath[0] = '\0';
    g_szMainPath[0] = '\0';
    if (cch == 0 || cch >= MAX_PATH) {
        return;
    }

    /* Keep everything up to and including the last backslash. */
    for (i = 0; i < cch; ++i) {
        if (awcPath[i] == L'\\') {
            nDir = i + 1u;
        }
    }
    awcPath[nDir] = L'\0';
    szDir[0] = '\0';
    (void)WideCharToMultiByte(CP_ACP, 0, awcPath, -1, szDir,
                              (int)sizeof(szDir), NULL, NULL);
    /* Same directory, two file names. snprintf keeps the bounds explicit. */
    (void)snprintf(g_szLangPath, sizeof(g_szLangPath), "%s%s", szDir, "lang.dat");
    (void)snprintf(g_szMainPath, sizeof(g_szMainPath), "%s%s", szDir, "main.dat");
    pufo_strcpy(g_szAppDir, szDir); /* szAppDir @ 0x1883b0, the save-file directory */
}

/* Copy the loaded 240x320 logical surface onto the window DIB so the data path
 * is visible: the original blits the same surface in GX_PresentFrame @ 0x9ea7c. */
static void App_PresentLoadedSurface(void)
{
    SoftSurf *pSurface = g_assets.pSurface;
    int       y;

    if (pSurface == NULL || pSurface->pPixels == NULL || g_gx.pBits == NULL) {
        return;
    }
    /* GX_PresentFrame @ 0x9ea7c arms the blit target (Render_SetTarget @ 0x9faa4 ->
     * Blit_SetTargetParams @ 0xa39ac) before blitting the soft surface. The target's pixel
     * mode is what SoftSurf_BlitGlyph_Inner @ 0xa41d4 gates on (blitState+0x20), so without
     * this call no glyph ever draws. The 240x320/2bpp constants are the original's own. */
    Render_SetTarget(240, 320, 2, g_gx.cbxPitch, g_gx.cbyPitch, g_gx.pBits);
    for (y = 0; y < (int)g_gx.cyHeight && y < pSurface->nHeight; ++y) {
        const u8 *pSrc = pSurface->pPixels + (size_t)y * (size_t)pSurface->nWidth * 2u;
        u8       *pDst = g_gx.pBits + (size_t)y * (size_t)g_gx.cbyPitch;
        size_t    cb   = (size_t)pSurface->nWidth * 2u;

        if (cb > (size_t)g_gx.cxWidth * 2u) {
            cb = (size_t)g_gx.cxWidth * 2u;
        }
        memcpy(pDst, pSrc, cb);
    }
}

static WCHAR g_wszDumpPath[MAX_PATH];

/* `--dump-frame <path>` on the command line. Returns the path or NULL. */
static LPCWSTR App_FindDumpFlag(LPCWSTR pwszCmd)
{
    static const WCHAR awcFlag[] = L"--dump-frame";
    int i = 0;

    if (pwszCmd == NULL) {
        return NULL;
    }
    pwszCmd = wcsstr(pwszCmd, awcFlag);
    if (pwszCmd == NULL) {
        return NULL;
    }
    pwszCmd += (int)(sizeof(awcFlag) / sizeof(WCHAR)) - 1;
    while (*pwszCmd == L' ') {
        ++pwszCmd;
    }
    while (*pwszCmd != L'\0' && *pwszCmd != L' ' && i < MAX_PATH - 1) {
        g_wszDumpPath[i++] = *pwszCmd++;
    }
    g_wszDumpPath[i] = L'\0';
    return (i == 0) ? NULL : g_wszDumpPath;
}

static bool g_bClickAt;
static i32  g_nClickX;
static i32  g_nClickY;

/* `--new-game` on the command line: run the new-game bring-up (Geoscape_InitUI mode 0 +
 * Game_StartNew) once before the frame dump, so the geoscape state is checkable headlessly.
 * Not part of the original. */
static bool g_bNewGameFlag;

static void App_FindNewGameFlag(LPCWSTR pwszCmd)
{
    static const WCHAR awcFlag[] = L"--new-game";

    g_bNewGameFlag = (pwszCmd != NULL && wcsstr(pwszCmd, awcFlag) != NULL);
}

/* `--load-slot <n>` on the command line: run the load path (LoadGame @ 0x44bbc) once before the
 * frame dump. Not part of the original. */
static i32 g_nLoadSlot = -1;

static void App_FindLoadSlotFlag(LPCWSTR pwszCmd)
{
    static const WCHAR awcFlag[] = L"--load-slot";
    WCHAR awcArg[16];
    int i = 0;

    if (pwszCmd == NULL) {
        return;
    }
    pwszCmd = wcsstr(pwszCmd, awcFlag);
    if (pwszCmd == NULL) {
        return;
    }
    pwszCmd += (int)(sizeof(awcFlag) / sizeof(WCHAR)) - 1;
    while (*pwszCmd == L' ') {
        ++pwszCmd;
    }
    while (*pwszCmd != L'\0' && *pwszCmd != L' ' && i < 15) {
        awcArg[i++] = *pwszCmd++;
    }
    awcArg[i] = L'\0';
    if (i != 0) {
        g_nLoadSlot = (i32)_wtoi(awcArg);
    }
}

/* `--click <x> <y>` on the command line: with --dump-frame this performs one press+release at
 * logical 240x320 coordinates before the dump, so the menu's click path (hover mark, then release
 * fires the row's handler) can be checked headlessly. Not part of the original. */
static void App_FindClickFlag(LPCWSTR pwszCmd)
{
    static const WCHAR awcFlag[] = L"--click";
    WCHAR awcArg[32];
    int   i;

    g_bClickAt = false;
    if (pwszCmd == NULL) {
        return;
    }
    pwszCmd = wcsstr(pwszCmd, awcFlag);
    if (pwszCmd == NULL) {
        return;
    }
    pwszCmd += (int)(sizeof(awcFlag) / sizeof(WCHAR)) - 1;
    for (int n = 0; n < 2; ++n) {
        while (*pwszCmd == L' ') {
            ++pwszCmd;
        }
        i = 0;
        while (*pwszCmd != L'\0' && *pwszCmd != L' ' && i < 31) {
            awcArg[i++] = *pwszCmd++;
        }
        awcArg[i] = L'\0';
        if (i == 0) {
            return;
        }
        if (n == 0) {
            g_nClickX = (i32)_wtoi(awcArg);
        } else {
            g_nClickY = (i32)_wtoi(awcArg);
        }
    }
    g_bClickAt = true;
}

/* Writes the logical 240x320 RGB565 surface as a binary PPM (P6). A debug hook for
 * `pocketufo --dump-frame <path>`: the rendered frame can then be inspected or compared without
 * a screen grab. Not part of the original — the original presents through GX. */
static void App_DumpFramePpm(LPCWSTR pwszPath)
{
    const SoftSurf *p = g_assets.pSurface;
    HANDLE hFile;
    char   szHeader[64];
    char   aRow[240 * 3]; /* the logical surface is always 240 wide (SoftSurf_AllocPixels(240,320,2)) */
    int    nHeader;
    DWORD  cbWritten;

    if (p == NULL || p->pPixels == NULL) {
        return;
    }
    hFile = CreateFileW(pwszPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }
    nHeader = snprintf(szHeader, sizeof(szHeader), "P6\n%d %d\n255\n", p->nWidth, p->nHeight);
    if (nHeader > 0) {
        (void)WriteFile(hFile, szHeader, (DWORD)nHeader, &cbWritten, NULL);
    }
    for (int y = 0; y < p->nHeight; ++y) {
        const u16 *pRow = (const u16 *)(p->pPixels + (size_t)y * (size_t)p->nWidth * 2u);
        for (int x = 0; x < p->nWidth; ++x) {
            const u16 v = pRow[x];
            aRow[3 * x + 0] = (char)(((v >> 11) & 0x1Fu) * 255 / 31);
            aRow[3 * x + 1] = (char)(((v >> 5)  & 0x3Fu) * 255 / 63);
            aRow[3 * x + 2] = (char)((v & 0x1Fu) * 255 / 31);
        }
        (void)WriteFile(hFile, aRow, (DWORD)(p->nWidth * 3), &cbWritten, NULL);
    }
    (void)CloseHandle(hFile);
}

/* Fill the logical surface with a single RGB565 colour. Fallback when the assets
 * could not be loaded, so the window still proves the display path. */
static void App_ClearSurface(u16 colour)
{
    int y;

    for (y = 0; y < (int)g_gx.cyHeight; ++y) {
        u16 *pRow = (u16 *)(g_gx.pBits + (size_t)y * (size_t)g_gx.cbyPitch);
        int  x;

        for (x = 0; x < (int)g_gx.cxWidth; ++x) {
            pRow[x] = colour;
        }
    }
}

static bool App_InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSW wc;
    HWND      hWnd;

    /* Single instance, matching InitInstance @ 0x9fd48. The original raises
     * the existing window; that is safe to keep on Win64. */
    hWnd = FindWindowW(PUFO_CLASS_NAME, PUFO_WINDOW_NAME);
    if (hWnd != NULL) {
        (void)SetForegroundWindow(hWnd);
        return false;
    }

    memset(&wc, 0, sizeof(wc));
    wc.style         = CS_VREDRAW | CS_HREDRAW; /* original style == 3 */
    wc.lpfnWndProc   = PocketUFO_WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor       = NULL;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = PUFO_CLASS_NAME;
    if (RegisterClassW(&wc) == 0) {
        return false;
    }

    /* The original sizes the WINDOW so the client matches the display. CreateWindowExW's
     * width/height are the window size, so the frame has to be added first; without this the
     * 240x320 surface is cropped by the title bar and borders. */
    {
        RECT rc = { 0, 0, PUFO_SURF_W, PUFO_SURF_H };
        DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        (void)AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);
        hWnd = CreateWindowExW(0, PUFO_CLASS_NAME, PUFO_WINDOW_NAME,
                               dwStyle,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               rc.right - rc.left, rc.bottom - rc.top,
                               NULL, NULL, hInstance, NULL);
    }
    if (hWnd == NULL) {
        return false;
    }
    g_hWndMain = hWnd;

    if (!Gx_Open(&g_gx, hWnd, PUFO_SURF_W, PUFO_SURF_H)) {
        printf("pocketufo: display surface failed to open\n");
        fflush(stdout);
        DestroyWindow(hWnd);
        g_hWndMain = NULL;
        return false;
    }

    Game_SeedRng();

    /* Assets_LoadStartup @ 0x382d0. The original resolves the paths from the exe
     * directory in InitInstance and loads here; a failure is not fatal there
     * either (it leaves the game with no assets), but it is reported here. */
    App_BuildAssetPaths();
    {
        const char *const apszArchives[PUFO_ARCHIVE_COUNT] = {
            g_szLangPath, g_szMainPath
        };

        g_bAssetsLoaded = Boot_LoadStartup(&g_assets, apszArchives);

        /* DEVIATION: the original's InitInstance @ 0x9fd48 reports nothing, and on WinCE that was fine
         * because a missing asset pack still left a visible window. On Win64 a build with no assets and a
         * build that is merely between frames look the same from the outside, so the port states what it
         * found. This is the only way to tell a good launch from a broken one without looking at the
         * screen. */
        printf("pocketufo: assets %s (lang.dat=%s, main.dat=%s)\n",
               g_bAssetsLoaded ? "loaded" : "NOT FOUND", g_szLangPath, g_szMainPath);
        fflush(stdout);
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return true;
}

static void App_QuitDisplay(void)
{
    if (g_bWorldReady) {
        World_Free(&g_world);
        g_bWorldReady = false;
    }
    if (g_bAssetsLoaded) {
        Boot_Shutdown(&g_assets);
        g_bAssetsLoaded = false;
    }
    Gx_Close(&g_gx);
    PostQuitMessage(0);
}

LRESULT CALLBACK PocketUFO_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;

        (void)BeginPaint(hWnd, &ps);
        (void)Gx_Present(&g_gx);
        (void)EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; /* the surface covers the client area */
    case WM_SIZE:
        if (g_gx.pBits != NULL) {
            (void)Gx_Present(&g_gx);
        }
        return 0;
    case WM_TIMER:
        /* PocketUFO_WndProc @ 0xA0944's timer dispatch. 0xC351 is the render tick (the original
         * calls Render_Frame); 0xC352 is the anim tick, which advances the phase counter
         * (0xA0978..0xA098C: >=7 wraps to 0) and runs the screen-17 row-key flicker (0xA0AC8). */
        if (wParam == PUFO_TIMER_ANIM) {
            App_TickAnim();
        } else if (wParam == PUFO_TIMER_RENDER) {
            App_DrawFrame();
        }
        return 0;
    case WM_MOUSEMOVE:
        App_OnPointer(lParam, false); /* 0x13c6c, hover marks */
        return 0;
    case WM_LBUTTONDOWN:
        App_OnPointer(lParam, true);
        return 0;
    case WM_LBUTTONUP:
        App_OnPointerUp(); /* 0x148d0, fires the hovered row's handler */
        return 0;
    case WM_KEYDOWN:
        /* Scaffold: Esc quits, mirroring the original's escape path. */
        if (wParam == VK_ESCAPE) {
            App_QuitDisplay();
            return 0;
        }
        break;
    case WM_DESTROY:
        App_QuitDisplay();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    MSG            msg;
    HACCEL         hAccel;
    unsigned short colour = 0x0000u; /* black in RGB565 */

    LPCWSTR pszDumpPath;

    (void)hPrevInstance;
    pszDumpPath = App_FindDumpFlag(lpCmdLine);
    App_FindClickFlag(lpCmdLine);
    App_FindNewGameFlag(lpCmdLine);
    App_FindLoadSlotFlag(lpCmdLine);

    if (!App_InitInstance(hInstance, nCmdShow)) {
        return 0;
    }

    /* Match WinMain @ 0x9fcc4: accelerators are loaded unconditionally. A
     * missing table is not fatal there either. */
    hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(1));

    /* GameUI_InitStartup @ 0x38a28 runs after the assets are in (the original reaches it from
     * the title/menu path once Boot_LoadStartup's archives are open). It loads the fonts, the
     * startg/menu palettes, the backdrop glyph and the "tact" samples, sets the ui screen to 17,
     * and arms the 0xC351/0xC352 timers through StartTimers_B. */
    if (g_bAssetsLoaded) {
        App_InitStartupUi();
        g_bWorldReady = World_Init(&g_world);
        if (g_bNewGameFlag) {
            App_HookNewGame(NULL); /* the menu's new-game row runs the same path on a click */
        }
        if (g_nLoadSlot >= 0) {
            (void)App_HookLoadGame(g_nLoadSlot, NULL); /* the load-slot rows run the same path */
        }
        App_DrawFrame();
        if (pszDumpPath != NULL) {
            if (g_bClickAt) {
                /* Debug path: one press+release at logical coordinates. Input_OnPointer marks the
                 * row under the pointer, Input_OnPointerUp fires that row's handler (a MenuCtl
                 * thunk), which is how the real window delivers a menu click. */
                App_PointerLogical(g_nClickX, g_nClickY, true);
                App_OnPointerUp();
            }
            /* Debug path: run the anim clock for about a second (40 x 25 ms ticks, the same
             * body WM_TIMER 0xC352 runs) so the row keys reach their steady value, render one
             * frame, write it out, and leave. */
            for (int i = 0; i < 40; ++i) {
                App_TickAnim();
            }
            App_DrawFrame();
            App_DumpFramePpm(pszDumpPath);
            if (g_bWorldReady) {
                World_Free(&g_world);
                g_bWorldReady = false;
            }
            Boot_Shutdown(&g_assets);
            g_bAssetsLoaded = false;
            Gx_Close(&g_gx);
            return 0;
        }
    } else {
        App_ClearSurface(colour);
        (void)Gx_Present(&g_gx);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (hAccel == NULL || TranslateAcceleratorW(msg.hwnd, hAccel, &msg) == 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
