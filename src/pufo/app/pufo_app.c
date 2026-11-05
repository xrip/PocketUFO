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
#include "audio/pufo_audio_winmm.h"  /* the winmm backend at the Sound_SetBackend seam */
#include "game/pufo_boot.h"
#include "game/pufo_clock.h"    /* GameTime, the scratch clock SaveSlot_FormatList uses */
#include "game/pufo_font.h"     /* PufoFont, the shell owns the fontSys block */
#include "game/pufo_input.h"    /* Input_OnPointer / Input_OnPointerUp, the menu click path */
#include "game/pufo_ballist.h"   /* Throw_RangeBudget and the live throw range constants */
#include "game/pufo_los.h"       /* Map_TraceLos, the native sight ray */
#include "game/pufo_path.h"     /* Path_ComputeReachable, the live move rule */
#include "app/pufo_app.h"     /* this file's own entry points, shared with tests/test_appshell.c */
#include "app/pufo_world.h"   /* PufoWorldState, the geoscape bring-up state */
#include "game/pufo_mainmenu.h" /* GameUI_InitStartup / MainMenu_Draw / MainMenu_TickRowKeys */
#include "game/pufo_options.h"  /* Options_LoadFromRegistry */
#include "game/pufo_renderframe.h" /* Render_Frame, the frame dispatcher */
#include "game/pufo_save.h"     /* SaveSlot_FormatList, the load-slot list labels */
#include "game/pufo_step.h"     /* Unit_CanStepTo, the live move planner probe */
#include "game/pufo_tactend.h"  /* Tact_Shutdown, the shutdown half of App_QuitFromTact */
#include "game/pufo_tactctl.h"  /* MapView_FocusOnCell @ 0x2c758 */
#include "game/pufo_turn.h"     /* PUFO_PHASE_ENEMY_AI, the live End Turn phase */
#include "game/pufo_terrain.h"  /* PUFO_TERRAIN_CELLS_PER_LAYER, live cell-table stride */
#include "game/pufo_timer.h"    /* Timer_SetWindow, the ids the WM_TIMER branch dispatches on */
#include "game/pufo_titles.h"   /* Titles_Init / TitlesAnimation, the credits screen */
#include "game/pufo_invdrag.h"  /* the live inventory drag scale */
#include "platform/pufo_gx.h"
#include "platform/pufo_aygshell.h"  /* the WM_CREATE / WM_ACTIVATE / WM_SETTINGCHANGE arms */
#include "render/pufo_render.h"
#include "ui/pufo_control.h"    /* UiControl_LoadPads, the start controls' pad loader */
#include "ui/pufo_uigeom.h"     /* QoL wide-view shim: the shell sets it on geometry switch */
#include "ui/pufo_menubtn.h"    /* MenuCtl*_OnClick / Menu_LoadSlot / StartUi_Shutdown */
#include "util/pufo_rand.h"
#include "util/pufo_str.h"

#include <stdio.h>
#include <stdlib.h> /* _wtoi, the --click coordinates */
#include <string.h>
#include <math.h>

#include <windows.h>

#define PUFO_CLASS_NAME  L"POCKET_UFO" /* ClassName @ 0xac6a8 */
#define PUFO_WINDOW_NAME L"POCKET UFO" /* WindowName @ 0xa5000 */

/* Read-only Soldier record observers span the stored 25-slot arrays. The distinct
 * inventory commit path intentionally remains PUFO_INV_COMMIT_SLOTS (24). */
#define APP_PERSISTENT_LOADOUT_SLOTS 25

static HWND       g_hWndMain = NULL;
/* nScreenW @ 0x188D40 and nScreenH @ 0x188D50: the size the app sizes its window to. InitInstance stores them
 * (0x9FE44 from GetSystemMetrics(0) at 0x9FE3C, 0x9FE58 from GetSystemMetrics(1) at 0x9FE4C) and every SetWindowPos in
 * the image passes the pair straight back -- InitInstance's own at 0x9FE94, PocketUFO_WndProc's four suspend/resume
 * sites (0xA04AC, 0xA0514, 0xA05B8, 0xA0610) and App_ResizeMainWindow at 0x9ECFC. ADAPTATION: the original read the
 * device's own metrics because the game ran full screen on it; GetSystemMetrics on Win64 is the desktop, so the port
 * stores the size it actually created the window with -- the display size plus the frame -- which is the size the
 * window already has. */
static i32        g_nScreenW;
static i32        g_nScreenH;
/* Map_nGameSpeed @ 0x128AB4 (Map + 0x150FC): the game speed, a SIGNED dword -- LDR at 0x9E918 (StartTimers_BySpeed)
 * and at 0xA08C0 (the 0xC350 dispatch, then CMP #0/#2/#3/#4 with BMI 0xA08C8), so the value stays signed at the
 * call sites. The registry default is 4, which is the 10 ms interval AND three ticks per firing; its writers in
 * the image are GameSpeed_Set @ 0x260c0 and the tactical options' speed buttons. The live cell is now
 * g_world.gameClock.nSpeed (Map+0x150FC's owner, see pufo_world.h): Map_InitAndLoadRegistry loads the registry
 * value into g_options.nGameSpeed, and World_NewGame / World_LoadSlot copy it into the clock, so the shell's
 * startup speed is the option value -- 4 by default -- exactly as the original's startup is. */
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
static u8               g_nAnimPhase;       /* unk_1139EF @ Map+0x37, the 0xC352 anim counter.
                             * A byte in the original -- Map_InitAndLoadRegistry @ 0x226A8 stores a
                             * byte there -- and the shell's counter only runs 0..6. */
static i32              g_nPlaneStatRowCount; /* nPlaneStatRowCount @ 0x161A38. One global, two unrelated
                             * uses in the original: the start-menu flicker reuses it as scratch (0xA0AEC
                             * stores nMapPhase into it, 0xA0B14 adds a random roll), while the plane
                             * screen uses it as its stat-panel row count (written at 0x79D24 / 0x79DE4,
                             * read at 0x79E30..0x79E90). Named after the global, not after one of its uses. */
static i32              g_nPhaseSave112;
static u32              g_nUiSlot0;         /* apUiSlotsA[0] @ Map+0x60, cleared by UiControl_HitTest */
/* World_NewGame and World_LoadSlot point this env's pnGamma at the world's Map_nGamma mirror
 * (Map+0x15100), so the packed colours divide by the gamma the game is actually running with --
 * 0x2d860 re-reads that global on every call in the original. */
static GfxColorEnv      g_color;
static bool             g_bStartupUi;
static char             g_aSlotLabels[7][40]; /* MainMenu_pSlotCtx @ 0x156938 */
static GameTime         g_gameTime;
static u8               g_bGameState;       /* bGameState @ 0x15821C */
static char             g_szAppDir[MAX_PATH];
/* Part 54: the tact input state moved onto the world (PufoWorldState.sTactInput). The tact MenuBtn
 * handlers Tact_InitUiControls installs call Input_OnConfirm @ 0x15f3c on it directly (pufo_menubtn.c),
 * so the app's pointer/confirm paths and the world's bank clicks must share ONE object. The alias
 * keeps every in-file reference (including the initEnv cell pointers in App_InitStartupUi) intact. */
#define g_tactState (g_world.sTactInput)
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
static u8              *g_pEndScreenBackdrop;  /* unk_1571C0: ending backdrop owned by EndScreen_Init */

static void App_PresentLoadedSurface(void);
static i32  App_PresentHook(void *pUser); /* GX_Present @ 0x27788; WorldHooks.pfnPresentFrame's copy */
static void App_DrawFrameHook(void *pUser);
static void App_TitlesToStartup(void *pUser);
static i32 App_HookLoadGame(i32 nSlot, void *pUser);
static i32 App_HookSaveGame(i32 nSlot, void *pUser);
static void App_HookFormatSlots(void *pUser);
static i32 App_InitStartupUiThunk(void *pUser); /* WorldHooks.pfnInitStartupUi: OptionsScreen_ReturnToTitle's tail callee, 0x6B490 (Part 102) */
static void App_HookEndScreenInit(u8 bGoodEnd, void *pUser);
static void App_HookEndScreenShutdown(void *pUser);

/* SaveLoad_Slot0..6 @ 0x19de4's pair. Both arms are wired since Part 62: the save arm reaches
 * World_SaveSlot (SaveGame @ 0x44988) and the load arm reaches the same loader the start menu uses. */
static const SaveLoadSlotHooks g_saveLoadHooks = { App_HookSaveGame, App_HookLoadGame, NULL };

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
    PadImage_BlitEx(pPad, 0u); /* UiControl_BlitActivePad @ 0x1A3C4 passes key 0. */
}

/* EndScreen_Init @ 0x388b0 owns the title globals and the screen-18 transition. */
static void App_HookEndScreenInit(u8 bGoodEnd, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    EndScreenInitEnv env;

    if (pW == NULL || pW->pFont == NULL || pW->pOptions == NULL || pW->pAssets == NULL) {
        return;
    }

    memset(&env, 0, sizeof(env));
    env.pFont       = pW->pFont;
    env.pnMaxRows   = &g_nTitlesMaxRows;
    env.anRowRec    = g_aTitlesRowRec;
    env.ppBackdrop  = &g_pEndScreenBackdrop;
    env.pnScroll    = &pW->nMapTitlesScroll;
    env.puUiScreen  = &pW->nUiScreen;
    env.pOptions    = pW->pOptions;
    EndScreen_Init(&env, bGoodEnd);

    g_nUiScreen = (i32)pW->nUiScreen;
    g_bStartupUi = false;
}

/* EndScreen_Shutdown @ 0x389f8 tails into GameUI_InitStartup @ 0x38a28. */
static void App_HookEndScreenShutdown(void *pUser)
{
    PufoWorldState *pW = &g_world;
    EndScreenShutdownEnv env;

    (void)pUser;
    if (pW->nUiScreen != 18u) {
        return;
    }

    memset(&env, 0, sizeof(env));
    env.pFont = pW->pFont;
    env.ppBackdrop = &g_pEndScreenBackdrop;
    EndScreen_Shutdown(&env);
    pW->nMapTitlesScroll = 0;
    (void)App_InitStartupUiThunk(pW);
}

/* --- the menu-button hook blocks -------------------------------------------------------
 *
 * MenuCtl0..3 (0x1a09c/0x1a034/0x1a064/0x1a080) play sample 16, run StartUi_Shutdown, then
 * branch: new game (Geoscape_InitUI @ 0x857f4 mode 0, which App_HookNewGame runs through
 * World_NewGame), load list (SaveSlot_FormatList), credits (Titles_Init @ 0x380d8), quit
 * (App_QuitDisplay). All four are ported; the hooks the ported functions take are filled here. */
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

/* The widget-58 seam (Part 61): MenuBtn_ResizeWindow @ 0x19930 tails into App_ResizeMainWindow
 * @ 0x9ecb8, ported below; the world reaches it through WorldHooks.pfnResizeMainWindow. */
void App_ResizeMainWindow(void);

static void App_ResizeMainWindowThunk(void *pUser)
{
    (void)pUser;
    App_ResizeMainWindow();
}

/* The new-game path: Geoscape_InitUI @ 0x857f4 mode 0, whose hook runs Game_StartNew
 * @ 0x94554 over the shell's PufoWorldState. The state line is the shell's own report (a
 * DEVIATION: the original shows the geoscape instead of printing); it is what makes the
 * bring-up observable from a headless run. */
static void App_HookNewGame(void *pUser)
{
    static WorldHooks hooks; /* Part 54: the world keeps this pointer for the whole run, so it must not live on the hook's stack */

    (void)pUser;
    if (!g_bWorldReady) {
        return;
    }

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadPads = App_LoadPads;
    hooks.pfnPlaySample = App_HookPlaySampleI; /* the message-list samples */
    hooks.pfnRenderFrame = App_DrawFrameHook;
    /* The geoscape frame's control plumbing: the same pad blits and the Map+0x60 word the start-menu
     * pointer path uses, plus the colour pair the menu draws with. */
    hooks.pHit  = &g_pointerWorld.hit;
    hooks.pColor = &g_color;
    hooks.pfnPresentFrame = App_PresentHook; /* GX_Present @ 0x27788, the tail the tact frame reaches */
    hooks.pPresentUser    = NULL;
    hooks.pnAnimPhase = (i32 *)&g_nUiSlot0; /* Map + 0x60 @ 0x113A18: the one cell the hit-test clear
                                             * and the geoscape pulse's phase gate share */
    hooks.pnMapPhase = &g_nAnimPhase;       /* nMapPhase @ 0x1139EF (Map + 0x37), the byte the 0xC352
                                             * handler wraps: the placement arm's cursor colour */
    /* The tact bank bring-up wiring Tact_InitUiControls and the installed handlers read back (Part 54). */
    hooks.pBank               = &g_bank;    /* the 130-widget bank the world installs handlers/pads on */
    hooks.pTactInputHooks     = &g_tactHooks;   /* MenuBtn_Confirm hands these to Input_OnConfirm */
    hooks.pfnResizeMainWindow = App_ResizeMainWindowThunk; /* widget 58, 0x19930 -> 0x9ecb8 (Part 61) */
    hooks.pResizeUser         = NULL;
    hooks.pbBankActionFlag    = &g_pointerWorld.bActionFlag; /* UiBank_bActionFlag @ pBank+0x1041 = 0xAD711 */
    hooks.pSaveLoadHooks      = &g_saveLoadHooks;
    hooks.pfnFormatSlotList   = App_HookFormatSlots;         /* SaveSlot_FormatList @ 0x45904 */
    hooks.pFormatSlotListUser = NULL;                        /* it reads the app's own g_slotCtx */
    hooks.pSlotLabels         = &g_aSlotLabels[0][0];        /* MainMenu_pSlotCtx @ 0x156938, the block
                                                              * that hook writes and screens 10/11 draw */
    hooks.pnListMode          = &g_nListMode;                /* *0x157188 */
    hooks.pTactOptions        = &g_options;                  /* writable alias: the menu handlers write
                                                              * speed/gamma/volumes/toggles here */
    hooks.pfnInitStartupUi    = App_InitStartupUiThunk;      /* the quit page's Да tail callee, 0x6B490 (Part 102) */
    hooks.pfnEndScreenInit    = App_HookEndScreenInit;
    hooks.pszSaveDir          = g_szAppDir;                  /* szAppDir @ 0x1883b0: the options save/load rows open slot files (Part 102) */

    (void)World_NewGame(&g_world, &g_assets, &g_fontSys, &g_options, &hooks);
    printf("pocketufo: new game: %d funds, %d bases, %d soldiers, %d craft, screen %d, zoom %d\n",
           (int)g_world.nFunds, (int)g_world.nBaseCount, (int)g_world.nSoldierCount,
           (int)g_world.bCraftCount, (int)g_world.screen.nGeoscapeScreen, (int)g_world.view.nMode);
    fflush(stdout);
}

static void App_HookFormatSlots(void *pUser)
{
    (void)pUser;
    /* Raw 0x45904 restores only total seconds, leaving decoded fields and GAMESTATE live. */
    g_slotCtx.pTime = g_bWorldReady ? &g_world.geo.time : &g_gameTime;
    g_slotCtx.pbGameState = g_bWorldReady ? &g_world.bGameState : &g_bGameState;
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
    g_world.nUiScreen = nScreen; /* 0x38174 writes the same Map screen word LoadGame reads. */

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
static i32  App_GeoscapeDrawThunk(void *pUser); /* Geoscape_Draw, defined with the render path */
static void App_GeoscapePointerThunk(i32 nX, i32 nY, bool bDown, void *pUser);
static void App_GeoscapePointerUpThunk(void *pUser);
static void App_HookInvCloseAndCommit(void *pUser); /* Inv_CloseAndCommit @ 0x15a48 (Part 66) */
/* The keyboard arms' seams (Part 67), defined with the other hook bodies. */
static void App_HookNoop(void *pUser);                   /* nullsub_3 @ 0x22b50 */
static void App_HookTactMapClick(void *pUser);           /* Tact_HandleMapClick @ 0x1512c */
static void App_HookTactSelectNextUnit(void *pUser);     /* Tact_SelectNextUnit @ 0x17304 */
static void App_HookTactSelectPrevUnit(void *pUser);     /* Tact_SelectPrevUnit @ 0x17260 */
static void App_HookTactInvScrollUp(void *pUser);        /* Inv_ScrollUp @ 0x173e4 */
static void App_HookTactInvScrollDown(void *pUser);      /* Inv_ScrollDown @ 0x173a8 */
static bool App_HookTactInvHitTest(i32 nX, i32 nY, void *pUser);   /* Ui_HitTestTactItem @ 0x11ac0 */
static void App_HookTactInvClearHitSlot(void *pUser);    /* Inv_ClearHitSlot @ 0x11f40 */
static void App_HookTactInvHandlePointer(UiWidgetBank *pBank, void *pUser); /* Inv_HandlePointer @ 0x12260 */
static void App_HookTactOnConfirm(u16 nUiScreen, void *pUser);   /* the 0x15660 / 0x163C8 arms' callee */
static void App_HookTactSetCursorPos(i32 nX, i32 nY, void *pUser); /* MapCursor_SetPos @ 0x27014 */
static void App_HookTactPushTextRecord(i32 nRecIndex, void *pUser); /* StatusMsg_Push @ 0x2dda4 */
static void App_HookSaveOptions(void *pUser);            /* Options_SaveToRegistry @ 0x114d8 */
static void App_HookTactEscapeGeoscape(void *pUser);     /* Tact_Shutdown(0) + Geoscape_InitUI(1) */
static void App_HookGeoscapeCloseUi(void *pUser);        /* Geoscape_CloseUI @ 0x8b94c */
static void App_HookTactOnCancel(void *pUser);            /* UiScreen_OnCancel @ 0x16200 */
static void App_HookTactFocusOnCell(MapCell *pCell, void *pUser); /* MapView_FocusOnCell @ 0x2c758 */
static void App_HookTactFocusOnViewCell(i32 nLayer, i32 nViewY, i32 nViewX,
                                        void *pUser); /* Input_OnConfirm screen 2 @ 0x16158 */
static void App_HookGeoscapePanelA(void *pUser);         /* Geoscape_SetPanelFlagA @ 0x8b448 */
static void App_HookGeoscapePanelB(void *pUser);         /* Geoscape_SetPanelFlagB @ 0x8b478 */
static void App_HookGeoscapeFocusPrev(void *pUser);      /* Geoscape_UiFocusPrev @ 0x8afa0 */
static void App_HookGeoscapeFocusNext(void *pUser);      /* Geoscape_UiFocusNext @ 0x8b1f4 */
static void App_HookGeoscapeMapClick(void *pUser);       /* Geoscape_OnMapClick @ 0x8b4a8 */
static void App_ShutdownStartUi(void *pUser); /* defined below with the start-UI teardown */
static void App_QuitDisplay(void);      /* defined below with the window teardown */

/* App_QuitFromTact @ 0x18e18, wired to the screen-8 escape seam (TactInputHooks.pfnShutdownQuit): the
 * YES arm passes bKeepSome 1 (the store at 0x18E20), whose tail at 0x3E14C..0x3E154 empties the LIVE
 * geoscape strateg list. The shell answers with the world's own Tact_Shutdown mirror
 * (World_EndHookShutdown -- the Part 47 stand-in the debrief and battle-end arms already run), so the
 * quit-Да now reaches the DeleteAll it always had. The old call reached the engine mirror with a NULL
 * env, which is a no-op (Part 103 closes that). */
static void App_ShutdownTactNoMission(u8 bKeepSome, void *pUser)
{
    (void)pUser;
    World_EndHookShutdown(bKeepSome, &g_world);
}

static void App_QuitDisplayThunk(void *pUser)
{
    (void)pUser;
    App_QuitDisplay();
}

static void App_HookShutdownQuit(void *pUser)
{
    (void)pUser;
    App_QuitFromTact(App_ShutdownTactNoMission, App_QuitDisplayThunk, NULL);
}

/* Part 51 modal seams. The modal bank is the world's sModal (Map's apUiSlotsA[12..19] plus the two
 * scratch cells UiModal_Begin clears). TactInputHooks.pfnModalEnd stands for the UiModal_End @
 * 0x2648C at its five modal-ending call sites (Input_OnPointerUp 0x14B94/0x14BC0, UiScreen_OnBack
 * 0x156BC, UiScreen_DismissHigh 0x15A38, Input_OnConfirm 0x161E8, Input_OnMenuEscape 0x163B0);
 * pfnModalEndCheckBattle is the screen-9 arm at 0x156C8 (End + Tact_CheckBattleEnd(abort=1), and
 * World_MissionEndCheck returns false without a live frame -- the original's CheckBattleEnd needs
 * the mission context too). The slot 16/18 thunks run the bank's stored button callbacks, the
 * calls at 0x14B94/0x14BE4 that follow the end. */
static void App_TactModalEndThunk(void *pUser)
{
    (void)pUser;
    UiModal_End(&g_world.sModal);
}

static void App_TactModalEndCheckBattleThunk(void *pUser)
{
    (void)pUser;
    UiModal_End(&g_world.sModal);
    (void)World_MissionEndCheck(&g_world, 1);
}

static void App_Slot16Thunk(void)
{
    if (g_world.sModal.pfn16 != NULL) {
        g_world.sModal.pfn16();
    }
}

static void App_Slot18Thunk(void)
{
    if (g_world.sModal.pfn18 != NULL) {
        g_world.sModal.pfn18();
    }
}

static void App_DrawFrameHook(void *pUser)
{
    (void)pUser;
    App_DrawFrame();
}

/* LoadGame @ 0x44bbc over the shell's world state. The slot path is szAppDir + "sav" + slot +
 * ".sav" and szAppDir @ 0x1883b0 is the exe directory App_BuildAssetPaths derived from
 * GetModuleFileNameW -- the same string SaveSlot_FormatList @ 0x45904 builds its seven labels from, so
 * the list and the loader look at one directory. The state line is the shell's own report, as in
 * App_HookNewGame. */
static i32 App_HookLoadGame(i32 nSlot, void *pUser)
{
    static WorldHooks hooks; /* Part 54: the world keeps this pointer for the whole run, so it must not live on the hook's stack */

    (void)pUser;
    if (!g_bWorldReady) {
        return 0;
    }

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadPads = App_LoadPads;
    hooks.pfnPlaySample = App_HookPlaySampleI;
    hooks.pfnRenderFrame = App_DrawFrameHook;
    hooks.pfnStartUiShutdown = App_ShutdownStartUi;
    hooks.pHit  = &g_pointerWorld.hit;
    hooks.pColor = &g_color;
    hooks.pfnPresentFrame = App_PresentHook; /* GX_Present @ 0x27788, the tail the tact frame reaches */
    hooks.pPresentUser    = NULL;
    hooks.pnAnimPhase = (i32 *)&g_nUiSlot0; /* the same Map + 0x60 cell, as in App_HookNewGame */
    hooks.pnMapPhase = &g_nAnimPhase;       /* nMapPhase @ 0x1139EF; Save_ReadTGameData 0x444E0 is its
                                             * other writer, the save reader's own copy */
    /* The tact bank bring-up wiring, the same fields App_HookNewGame fills (Part 54). */
    hooks.pBank               = &g_bank;
    hooks.pTactInputHooks     = &g_tactHooks;
    hooks.pfnResizeMainWindow = App_ResizeMainWindowThunk; /* widget 58 (Part 61) */
    hooks.pResizeUser         = NULL;
    hooks.pbBankActionFlag    = &g_pointerWorld.bActionFlag;
    hooks.pSaveLoadHooks      = &g_saveLoadHooks;
    hooks.pfnFormatSlotList   = App_HookFormatSlots;
    hooks.pFormatSlotListUser = NULL;
    hooks.pSlotLabels         = &g_aSlotLabels[0][0]; /* MainMenu_pSlotCtx @ 0x156938 */
    hooks.pnListMode          = &g_nListMode;
    hooks.pTactOptions        = &g_options;

    hooks.pfnInitStartupUi    = App_InitStartupUiThunk; /* the quit page's Да tail callee, 0x6B490 (Part 102) */
    hooks.pfnEndScreenInit    = App_HookEndScreenInit;
    hooks.pszSaveDir          = g_szAppDir;             /* szAppDir @ 0x1883b0: the options save/load rows open slot files (Part 102) */

    {
        i32 nRet = World_LoadSlot(&g_world, g_szAppDir, nSlot, &g_assets, &g_fontSys, &g_options, &hooks);
        printf("pocketufo: load slot %d: %s (%d funds, %d bases, %d soldiers, %d craft, ui screen %d)\n",
               (int)nSlot, nRet != 0 ? "loaded" : "no save", (int)g_world.nFunds,
               (int)g_world.saveBases.bBaseCount, (int)g_world.nSoldierCount,
               (int)g_world.bCraftCount, (int)g_world.nUiScreen);
        fflush(stdout);
        return nRet;
    }
}

/* SaveGame @ 0x44988 for the shell (Part 62). SaveLoad_Slot reaches it from the save list
 * (MenuBtn_Confirm's SaveLoad_Slot(nSlot, 10) arm); World_SaveSlot reads every chunk source straight
 * off the world, so the frame seam is the only WorldHooks field this path needs. The state line is
 * the shell's own report, as in App_HookLoadGame. */
static i32 App_HookSaveGame(i32 nSlot, void *pUser)
{
    static WorldHooks hooks; /* the same reason App_HookLoadGame's hooks is static */

    (void)pUser;
    if (!g_bWorldReady) {
        return 0;
    }

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnRenderFrame = App_DrawFrameHook;

    {
        i32 nRet = World_SaveSlot(&g_world, g_szAppDir, nSlot, &hooks);
        printf("pocketufo: save slot %d: %s (%d soldiers, %d craft, %d tact items, ui screen %d)\n",
               (int)nSlot, nRet != 0 ? "written" : "failed", (int)g_world.nSoldierCount,
               (int)g_world.bCraftCount, (int)g_world.nTactItemCount, (int)g_world.nUiScreen);
        fflush(stdout);
        return nRet;
    }
}

static void App_HookClearListMode(void *pUser)
{
    (void)pUser;
    /* Raw 0x1A240 clears the one global menu list mode at 0x157188. */
    g_nListMode = 0;
    g_world.nListMode = 0;
    g_pointerWorld.nListMode = 0;
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
    /* 0x38E2C writes the shared Map screen word. LoadGame reads the world's
     * copy at 0x44CB0, so retiring the startup UI must publish the same value. */
    g_world.nUiScreen = (u16)g_nUiScreen;

    g_pStartCtl = NULL;
    g_pBgGlyph  = NULL;
    g_pEmptyPad = NULL;
    g_bStartupUi = false;
}

static void App_StartNewGameFromShortcut(void)
{
    if (g_bStartupUi) {
        MenuCtl0_OnClick(&g_startHooks);
    } else {
        App_HookNewGame(NULL);
    }
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
    EmptyBtn_OnClick(App_HookPlaySampleI, App_HookClearListMode, NULL); /* 0x1a228 */
}

/* GameUI_InitStartup @ 0x38a28 for the shell. The menu controls live in the widget bank at
 * 113..127 so Input_OnPointerUp (0x148d0) and StartUi_Shutdown reach the same controls the
 * original's 0xAD4F0 block is. */
static void App_InitStartupGlobals(void)
{
    static bool bInitialized;
    MapInitEnv initEnv;

    if (bInitialized) {
        return;
    }

    /* InitTerm_UiBankEntry @ 0x11148, the UiBank CRT init-term entry (table slot 0x189030):
     * UiBank_CtorAll @ 0x11320 builds all 130 bank widgets -- bEnabled 1 at +0x09 is what makes
     * them hit-testable at all -- and Bank_RegisterAtexit @ 0x11164 arms the pad teardown. The
     * shell never ran this entry until Part 60 armed the pen path. */
    (void)UiBank_CtorAll(&g_bank, &g_pointerWorld.bActionFlag);
    Bank_RegisterAtexit(&g_bank);

    /* Map_InitAndLoadRegistry @ 0x22648, the Map subsystem's startup entry (the CRT
     * thunk at 0x22604 runs it before Map_RegisterAtexit): clear the tactical
     * scratch, then load the option block. The shell owns the scratch it can own, and
     * since the Map mission header moved onto PufoWorldState the battle-ended byte at
     * 0x113A14 reaches 0x226A8's clear for real. The throw-target slot (0x150F0) is
     * tactical state and still arrives as NULL. */
    memset(&g_tactState, 0, sizeof(g_tactState));
    memset(&initEnv, 0, sizeof(initEnv));
    initEnv.pOptions          = &g_options;
    initEnv.pbDeployPanelOpen = &g_tactState.bDeployPanelOpen; /* 0x15110 */
    initEnv.pbBattleEnded     = &g_world.bMapBattleEnded;      /* 0x113A14: the world owns this Map byte
                                                                 * now, so 0x226A8's startup clear reaches
                                                                 * it instead of landing on a NULL */
    initEnv.pbMapPhase        = &g_nAnimPhase;                 /* 0x0037 */
    initEnv.pbSlot2Byte0      = &g_world.bMapSweepArmed;       /* 0x0068: the world owns this byte now --
                                                                 * Tact_InitMission's tail arms it at 0x3D268,
                                                                 * so one instance serves both */
    initEnv.pbSlot8Byte0      = &g_tactState.bUiSlot8Byte0;    /* 0x0080 */
    initEnv.pnUiSlot3         = &g_tactState.nUiSlot3;         /* 0x006C */
    initEnv.pnUiSlot4         = &g_tactState.nUiSlot4;         /* 0x0070 */
    initEnv.pnUiSlot5         = &g_tactState.nUiSlot5;         /* 0x0074 */
    initEnv.pnUiSlot7         = &g_tactState.nUiSlot7;         /* 0x007C */
    initEnv.pnUiSlot25        = &g_tactState.nUiSlot25;        /* 0x00C4 */
    initEnv.pnUiSlot26        = &g_tactState.nUiSlot26;        /* 0x00C8 */
    initEnv.pnPhaseSave112    = &g_nPhaseSave112;              /* 0x150E8 */
    Map_InitAndLoadRegistry(&initEnv);
    (void)Font_RegisterAtexit(); /* raw startup entry 0x460B4 -> 0x460D0 */
    bInitialized = true;
}

static void App_InitStartupUi(void)
{
    GameUiStartupEnv env;

    /* These are CRT startup effects in the original, not title-menu effects. */
    App_InitStartupGlobals();
    g_color.pnGamma = &g_options.nGamma; /* Startup menu before the world Map exists. */
    Timer_SetWindow(g_hWndMain);

    g_pStartCtl = (UiControl *)malloc(sizeof(UiControl)); /* the original's operator new(0x20) */

    memset(&env, 0, sizeof(env));
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
    g_world.nUiScreen = (u16)g_nUiScreen; /* 0x38D40: every startup entry publishes the shared screen. */

    /* The input world: the screen-17 list state the pointer handlers read and write. */
    memset(&g_pointerWorld, 0, sizeof(g_pointerWorld));
    g_pointerWorld.pBank          = &g_bank;
    g_pointerWorld.pListCtl       = g_pStartCtl;
    /* unk_1571E4, the screen-0 look control hit before the tact widgets: the original allocates it
     * inside Tact_InitUiControls; the world embeds it (Part 54), so the pointer is stable from
     * startup. It is only reached while a tactical mission owns the input (bTactFlag), which the
     * tact pointer dispatch -- a separate open issue -- will route. */
    g_pointerWorld.pLookCtl       = &g_world.sTactLookCtl;
    g_pointerWorld.nListMode      = g_nListMode;
    g_pointerWorld.bListRowSticky = false; /* unk_188D4C */
    /* Modal bank slots 16/18 (Map+0xA0/0xA8): the button callbacks Input_OnPointerUp runs after
     * ending the modal (0x14B94 / 0x14BE4). The bank stores them, the thunks dispatch them. */
    g_pointerWorld.pfnSlot16 = App_Slot16Thunk;
    g_pointerWorld.pfnSlot18 = App_Slot18Thunk;
    g_pointerWorld.hit.pnUiSlot0  = &g_nUiSlot0;
    g_pointerWorld.hit.pfnBlit    = App_BlitPad;
    g_pointerWorld.hit.pfnBlitEx  = App_BlitPadEx;

    /* g_tactState was cleared by Map_InitAndLoadRegistry above, which also wrote the
     * two non-zero slots (0x70 = 200 and the 0xC8 roll). */
    (void)App_InitTactInputHooks();

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

const TactInputHooks *App_InitTactInputHooks(void)
{
    memset(&g_tactHooks, 0, sizeof(g_tactHooks));
    /* Screen 16 (the credits) and screen 18 (an ending) both return to the start menu on release. */
    g_tactHooks.pfnTitlesToStartup = App_TitlesToStartup;
    g_tactHooks.pfnEndScreenShutdown = App_HookEndScreenShutdown;
    /* Screen 8 (the quit confirmation) answers the back action with App_QuitFromTact @ 0x18e18. The shell
     * never opens the quit dialog, but the seam is wired so the port reproduces the original wiring. */
    g_tactHooks.pfnShutdownQuit = App_HookShutdownQuit;
    /* Modal bank seams (Part 51): the input arms that end the bank's modal, and the screen-9
     * escape arm that ends it and aborts the mission check. */
    g_tactHooks.pfnModalEnd            = App_TactModalEndThunk;
    g_tactHooks.pfnModalEndCheckBattle = App_TactModalEndCheckBattleThunk;
    /* Input_OnPointer / Input_OnPointerUp take the geoscape branch when Map.bGeoscapeUi is set
     * (0x13C98 gate at 0x13CE0, 0x148FC at 0x14908) and hand the pen over to these two seams; the
     * shell runs them over the world's wired GeoscapePointerEnv. */
    g_tactHooks.pfnGeoscapePointer   = App_GeoscapePointerThunk;
    g_tactHooks.pfnGeoscapePointerUp = App_GeoscapePointerUpThunk;
    /* Input_OnConfirm's screen-1 arm (0x160FC plays sample 16, 0x16104 calls) closes the inventory
     * with Inv_CloseAndCommit @ 0x15a48 -- the call that deploys the team (Part 66). */
    g_tactHooks.pfnInvCloseAndCommit = App_HookInvCloseAndCommit;
    /* The keyboard arms (Part 67). PocketUFO_WndProc's WM_KEYDOWN chain (0xA0DC8..0xA0F74) runs
     * Input_OnLeft/Right/Up/Down, Input_OnAction, Input_OnSecondary and Input_OnConfirm; each of those
     * reaches the world over one of the seams below -- the geoscape branch through the geoscape seams
     * (0x116D8/0x117C8 -> SetPanelFlagA/B, 0x118C8/0x119C8 -> UiFocusPrev/Next, 0x15594 ->
     * Geoscape_OnMapClick), the tactical branch through the mission wrappers. The pen path already ran
     * Input_OnPointer/Input_OnPointerUp over this same table, so installing them here changes the pen's
     * arms too: the seams that were NULL (SetCursorPos, PushTextRecord, FocusOnCell-free) now run. */
    g_tactHooks.pfnPlaySample        = App_HookPlaySample;   /* Sound_Play @ 0x9e0c0 */
    g_tactHooks.pfnCursorChanged     = App_HookNoop;         /* nullsub_3 @ 0x22b50 */
    g_tactHooks.pfnHandleMapClick    = App_HookTactMapClick;
    g_tactHooks.pfnSelectNextUnit    = App_HookTactSelectNextUnit;
    g_tactHooks.pfnSelectPrevUnit    = App_HookTactSelectPrevUnit;
    g_tactHooks.pfnInvScrollUp       = App_HookTactInvScrollUp;
    g_tactHooks.pfnInvScrollDown     = App_HookTactInvScrollDown;
    /* The screen-1 item transfer (Part 161): press/tap hit-tests the ground list or a slot, the clear
     * empties what was hit, and the release (or the tap-to-place second tap) runs the drop. */
    g_tactHooks.pfnHitTestTactItem   = App_HookTactInvHitTest;
    g_tactHooks.pfnInvClearHitSlot   = App_HookTactInvClearHitSlot;
    g_tactHooks.pfnInvHandlePointer  = App_HookTactInvHandlePointer;
    g_tactHooks.pfnOnConfirm         = App_HookTactOnConfirm;
    g_tactHooks.pfnOnCancel          = App_HookTactOnCancel;
    g_tactHooks.pfnGeoscapeCloseUi   = App_HookGeoscapeCloseUi;
    g_tactHooks.pfnFocusOnCell       = App_HookTactFocusOnCell;
    g_tactHooks.pfnFocusOnViewCell   = App_HookTactFocusOnViewCell;
    g_tactHooks.pfnSetCursorPos      = App_HookTactSetCursorPos;
    g_tactHooks.pfnPushTextRecord    = App_HookTactPushTextRecord;
    g_tactHooks.pfnSaveOptions       = App_HookSaveOptions;         /* Input_OnConfirm's screen-12 arm */
    g_tactHooks.pfnShutdownReinit    = App_HookTactEscapeGeoscape;  /* UiScreen_OnCancel 13/19 escape arm */
    g_tactHooks.pfnGeoscapePanelA    = App_HookGeoscapePanelA;
    g_tactHooks.pfnGeoscapePanelB    = App_HookGeoscapePanelB;
    g_tactHooks.pfnGeoscapeFocusPrev = App_HookGeoscapeFocusPrev;
    g_tactHooks.pfnGeoscapeFocusNext = App_HookGeoscapeFocusNext;
    g_tactHooks.pfnGeoscapeMapClick  = App_HookGeoscapeMapClick;
    return &g_tactHooks;
}

/* WorldHooks.pfnInitStartupUi (Part 102): OptionsScreen_ReturnToTitle @ 0x6b46c -- the quit page's Да --
 * tails into GameUI_InitStartup (the store at 0x6B490), the same bring-up App_InitStartupUi runs at
 * startup. App_InitStartupUi publishes the same screen word to both native owners; LoadGame
 * @ 0x44CB0 tests the World copy (== 17) to select its startup-to-geoscape rebuild. */
static i32 App_InitStartupUiThunk(void *pUser)
{
    (void)pUser;
    App_InitStartupUi();
    return g_bStartupUi ? 1 : 0;
}

/* The screen-16 arm of Input_OnPointerUp @ 0x14AE8: release the credits screen, then bring the
 * start menu back up. Titles_dtor writes 22 into the screen id on the way out; GameUI_InitStartup
 * immediately replaces it with 17, which is why the original's 22 is never seen on screen. */
static void App_TitlesToStartup(void *pUser)
{
    TitlesShutdownEnv env;
    u16 nScreen;

    (void)pUser;
    nScreen = (u16)g_nUiScreen;
    memset(&env, 0, sizeof(env));
    env.pFont = &g_fontSys;         /* fontSys @ 0x158268 */
    env.apGlyphs = g_apTitlesGlyphs; /* unk_1571A0..0x1571B0 */
    env.pAnim = &g_titlesAnim;      /* g_pTitlesState @ 0x158804 */
    env.puUiScreen = &nScreen;      /* Map_nUiScreen @ Map+0x3A */
    Titles_Shutdown(&env);          /* Titles_Shutdown @ 0x3826c */
    g_nUiScreen = (i32)nScreen;

    App_InitStartupUi();            /* GameUI_InitStartup @ 0x38a28, the tail of the original's arm */
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

/* Render_Frame @ 0x22310 for the shell. The original tests Map.bGeoscapeMode (Map_bGeoscapeUi @
 * Map+0x44) at 0x2231c, BEFORE Map_nUiScreen, so a world in geoscape mode takes the geoscape branch
 * whatever the ui screen cell says. A live mission on a screen the world's frame builder covers
 * (0, 8, 9) draws through World_TactFrame (Part 63); screen 18 uses Titles_DrawBackdrop with the
 * app-owned ending state, and the other shell screens are the start menu (17) and credits (0x10). */
static void App_UpdateViewGeometry(void); /* defined with the QoL view state below */

static void App_DrawFrame(void)
{
    MainMenuEnv    mm;
    RenderFrameEnv rf;
    TitlesBackdrop endBackdrop;
    UiModalAnim loadingAnim;
    UiModalLayout loadingLayout;

    App_UpdateViewGeometry();

    if (g_bWorldReady && g_world.bGeoscapeUi != 0) {
        memset(&rf, 0, sizeof(rf));
        rf.nUiScreen       = g_nUiScreen;
        rf.bGeoscapeMode   = 1;
        rf.pfnGeoscapeDraw = App_GeoscapeDrawThunk;
        rf.pfnPresent      = App_PresentHook; /* the frame end's own thunk, not this one (0x22328) */
        (void)Render_Frame(&rf);
        return;
    }

    /* The tactical frame (Part 63): the world builds Render_Frame's env over its own live mission
     * and presents through WorldHooks.pfnPresentFrame, the shell's copy of GX_Present @ 0x27788. */
    if (g_bWorldReady && World_bTactFrameLive(&g_world)) {
        World_TactFrame(&g_world);
        return;
    }

    if (g_bWorldReady && g_world.nUiScreen == 18u) {
        memset(&endBackdrop, 0, sizeof(endBackdrop));
        endBackdrop.nScroll   = g_world.nMapTitlesScroll;
        endBackdrop.nMaxRows  = g_nTitlesMaxRows;
        endBackdrop.pBackdrop = g_pEndScreenBackdrop;
        endBackdrop.anRowRec  = g_aTitlesRowRec;
        endBackdrop.apGlyphs  = (const u8 *const *)g_apTitlesGlyphs;
        endBackdrop.pTextRecs = g_assets.pTextRecs;
        endBackdrop.pColor    = &g_color;

        memset(&rf, 0, sizeof(rf));
        rf.nUiScreen       = 18;
        rf.pTitlesBackdrop = &endBackdrop;
        rf.pfnPresent      = App_PresentHook;
        (void)Render_Frame(&rf);
        return;
    }

    /* Screen 17 is the start menu (GameUI_InitStartup); 0x10 is the credits (Titles_Init). */
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
    if (g_nUiScreen == 17 && mm.nListMode == PUFO_MAINMENU_MODE_LOADING) {
        loadingAnim.nCursor = g_nPhaseSave112;
        loadingAnim.bProgressive = g_world.sModal.nActionPhaseSave113;
        loadingAnim.nAnimPhase = g_nAnimPhase;
        loadingAnim.nBand = g_nAnimPhase;
        loadingAnim.nSelButton = g_world.sModal.nField48;
        loadingAnim.pColor = &g_color;
        loadingLayout.nButtonX[0] = g_world.sTactInput.nSlot20X;
        loadingLayout.nButtonX[1] = g_world.sTactInput.nSlot21X;
        loadingLayout.nButtonY = g_world.sTactInput.nSlot22Y;
        loadingLayout.nButtonCount = g_world.sTactInput.nSlot23H;
        mm.pModalAnim = &loadingAnim;
        mm.pModalLayout = &loadingLayout;
    }
    mm.pColor               = &g_color;
    mm.pfnCtlBlit           = App_BlitPad;
    mm.pfnCtlBlitEx         = App_BlitPadEx;

    memset(&rf, 0, sizeof(rf));
    rf.nUiScreen   = g_nUiScreen;
    rf.pMainMenu   = &mm;
    rf.pTitlesAnim = &g_titlesAnim;
    rf.pfnPresent  = App_PresentHook;
    (void)Render_Frame(&rf);
    if (mm.pModalAnim != NULL) {
        g_world.sModal.nActionPhaseSave112 = loadingAnim.nCursor;
        g_world.sModal.nActionPhaseSave113 = loadingAnim.bProgressive;
        g_tactState.nSlot20X = g_world.sTactInput.nSlot20X = loadingLayout.nButtonX[0];
        g_tactState.nSlot21X = g_world.sTactInput.nSlot21X = loadingLayout.nButtonX[1];
        g_tactState.nSlot22Y = g_world.sTactInput.nSlot22Y = loadingLayout.nButtonY;
        g_tactState.nSlot23H = g_world.sTactInput.nSlot23H = loadingLayout.nButtonCount;
    }
}

/* Render_Frame's geoscape hook (0x2232C hands over the 0x1864F8 screen block; the port hands the
 * shell's world instead). It runs Geoscape_Draw @ 0x8C180 over the wired frame envs, and the frame
 * end presents through App_PresentHook -- which is why Render_Frame returns this value without
 * presenting anything itself (0x22328 / 0x22330). */
static i32 App_GeoscapeDrawThunk(void *pUser)
{
    (void)pUser;
    return World_DrawGeoscapeFrame(&g_world, App_PresentHook, NULL);
}

/* The geoscape pointer path Input_OnPointer calls: Geoscape_OnPointer @ 0x872d0 with the pen in logical
 * 240x320 coordinates (WM_LBUTTONDOWN feeds them straight through, 0xa03ec), and Geoscape_OnPointerUp @
 * 0x89450, which re-reads the pair OnPointer stored rather than taking coordinates. */
static void App_GeoscapePointerThunk(i32 nX, i32 nY, bool bDown, void *pUser)
{
    (void)pUser;
    World_GeoscapePointer(&g_world, nX, nY, bDown);
}

static void App_GeoscapePointerUpThunk(void *pUser)
{
    (void)pUser;
    World_GeoscapePointerUp(&g_world);
}

/* The seam Input_OnConfirm's screen-1 arm calls (0x16104). It runs on the live world, which owns
 * every cell the callee reaches through the Map block. */
static void App_HookInvCloseAndCommit(void *pUser)
{
    (void)pUser;
    World_InvCloseAndCommit(&g_world);
}

/* --- the keyboard arms' hook bodies (Part 67) -----------------------------------------------
 *
 * TactInputHooks.pUser is the shell's own (NULL): the seams that need the mission reach the world
 * directly, exactly as App_HookInvCloseAndCommit does. None of them re-stages the input state -- they
 * run while a key or pen dispatch already holds that window (World_TactKeyDown / World_TactPointerMove). */

static void App_HookNoop(void *pUser)
{
    (void)pUser; /* nullsub_3 @ 0x22b50: the cursor-changed callee does nothing in the original */
}

static void App_HookTactMapClick(void *pUser)
{
    (void)pUser;
    World_TactMapClick();
}

static void App_HookTactSelectNextUnit(void *pUser)
{
    (void)pUser;
    World_TactSelectNextUnit();
}

static void App_HookTactSelectPrevUnit(void *pUser)
{
    (void)pUser;
    World_TactSelectPrevUnit();
}

static void App_HookTactInvScrollUp(void *pUser)
{
    (void)pUser;
    World_TactInvScrollUp();
}

static void App_HookTactInvScrollDown(void *pUser)
{
    (void)pUser;
    World_TactInvScrollDown();
}

/* The inventory screen's item-transfer arms. In the original these three are plain calls from
 * Input_OnPointer (0x146F0 press/tap, 0x146FC and 0x146A8 clear-the-slot) and Input_OnPointerUp
 * (0x1511C, the drag's release); the port routes them through TactInputHooks, so an uninstalled seam
 * means the screen-1 arm calls nothing and no item can be picked up, worn or moved in a live mission.
 * The mission's own drag state is the world's (World_TactInv* in pufo_world.c); without a live mission
 * they answer the original's no-mission state (false / nothing). */
static bool App_HookTactInvHitTest(i32 nX, i32 nY, void *pUser)
{
    (void)pUser;
    return World_TactInvHitTest(nX, nY);
}

static void App_HookTactInvClearHitSlot(void *pUser)
{
    (void)pUser;
    World_TactInvClearHitSlot();
}

static void App_HookTactInvHandlePointer(UiWidgetBank *pBank, void *pUser)
{
    (void)pUser;
    /* Inv_HandlePointer's own return is the constant 1 (0x12260); its internal success flag is what the
     * port returns, and the original discards it here -- the held flag clears either way (0x146B4/0x15120). */
    (void)World_TactInvDropPointer(pBank);
}

/* The original's callee is Input_OnConfirm with a3 = Map_nUiScreen (the BL at 0x15660); the port keeps
 * the screen id in the staged state, so the seam's argument is the same value the callee already reads. */
static void App_HookTactOnConfirm(u16 nUiScreen, void *pUser)
{
    (void)nUiScreen;
    (void)pUser;
    World_TactConfirmInPlace();
}

static void App_HookTactSetCursorPos(i32 nX, i32 nY, void *pUser)
{
    (void)pUser;
    World_TactSetCursorPos(nX, nY);
}

static void App_HookTactPushTextRecord(i32 nRecIndex, void *pUser)
{
    (void)pUser;
    World_TactPushTextRecord(nRecIndex);
}

static void App_HookSaveOptions(void *pUser)
{
    (void)pUser;
    Options_SaveToRegistry(&g_options); /* 0x114d8, over the shell's own option block */
}

static void App_HookTactEscapeGeoscape(void *pUser)
{
    (void)pUser;
    World_TactEscapeToGeoscape();
}

static void App_HookGeoscapeCloseUi(void *pUser)
{
    (void)pUser;
    World_CloseGeoscape(&g_world);
}

static void App_HookTactOnCancel(void *pUser)
{
    (void)pUser;
    (void)World_MissionCancelEnd(&g_world);
}

static void App_HookTactFocusOnCell(MapCell *pCell, void *pUser)
{
    (void)pUser;
    if (pCell == NULL) {
        return;
    }
    MapView_FocusOnCell(&g_world.tactEnv.focus, pCell, &g_world.tactEnv.cursor);
}

static void App_HookTactFocusOnViewCell(i32 nLayer, i32 nViewY, i32 nViewX,
                                        void *pUser)
{
    int64_t nViewCellIndex;
    int64_t nCellIndex;
    MapCell *pCell;

    (void)pUser;
    if (!World_bTactPointerLive(&g_world)) {
        return;
    }
    /* Raw Input_OnConfirm indexes Map+0x140F0 while physical cells start at Map+0x15338;
     * their 0x1248-byte ARM offset is exactly 1170 four-byte cell-pointer slots. */
    nViewCellIndex = ((int64_t)nLayer * PUFO_MAP_CELL_DIM + (int64_t)nViewY)
        * PUFO_MAP_CELL_DIM + (int64_t)nViewX;
    nCellIndex = nViewCellIndex - 1170;
    if (nCellIndex < 0
        || nCellIndex >= (int64_t)PUFO_MAP_LAYERS * PUFO_TERRAIN_CELLS_PER_LAYER) {
        return;
    }
    pCell = g_world.apTactCells[nCellIndex];
    if (pCell == NULL) {
        return;
    }
    MapView_FocusOnCell(&g_world.tactEnv.focus, pCell, &g_world.tactEnv.cursor);
}

static void App_HookGeoscapePanelA(void *pUser)
{
    (void)pUser;
    World_GeoscapePanelFlag(&g_world, true);
}

static void App_HookGeoscapePanelB(void *pUser)
{
    (void)pUser;
    World_GeoscapePanelFlag(&g_world, false);
}

static void App_HookGeoscapeFocusPrev(void *pUser)
{
    (void)pUser;
    World_GeoscapeFocusScroll(&g_world, true);
}

static void App_HookGeoscapeFocusNext(void *pUser)
{
    (void)pUser;
    World_GeoscapeFocusScroll(&g_world, false);
}

static void App_HookGeoscapeMapClick(void *pUser)
{
    (void)pUser;
    (void)World_MapClick(&g_world);
}

/* The WM_TIMER 0xC352 body (dispatch @ 0xA0958): FIRST it arms panel auto-repeat for the
 * next draw -- `MOV R1,#1 / STRB R1,[bPanelFlagScratch]` at 0xA0968..0xA0978, bPanelFlagScratch
 * @ 0x161F0C. The consumer is per screen: BuySellScreen_Draw copies it into panels B and C and
 * clears it (0x78874..0x788A4), TransferScreen_Draw copies it into panels B and C (0x9824C, 0x9825C),
 * BasePlaneScreen_DrawCraftDetail copies it into the store row panels and clears it
 * (0x7BE58, 0x7BE68, 0x7BE70). UiPanel_TrackHoldRow @ 0x3ed44 only counts while the panel's
 * bRepeatArmed stands, so this store is what makes a held pen repeat at all. Then the anim phase
 * counter (0xA0978..0xA098C, >=7 wraps to 0), the debrief fade arm (0xA0990..0xA09AC: banner counter
 * up AND screen 13 -> Render_Frame_DecRef), and per screen: 17 runs the row-key flicker (0xA0AC8),
 * 0x10 advances the credits scroll by one and wraps it past 20 * (nTitlesMaxRows + 48) (0xA0C58);
 * screen 18 advances the ending scroll by one (0xA0C38..0xA0C48). */
static void App_TickAnim(void)
{
    g_world.bPlaneRowFlag = 1; /* 0xA0968 MOV R1,#1 / 0xA0978 STRB [bPanelFlagScratch] -- the arm,
                                * unconditional in the original's timer body: bPanelFlagScratch
                                * @ 0x161F0C, consumed by the panel draws (0x788A4, 0x7BE70) */
    if (g_nAnimPhase >= 7) {
        g_nAnimPhase = 0; /* 0xA0988 */
    } else {
        ++g_nAnimPhase;   /* 0xA098C */
    }
    if (g_bWorldReady) {
        World_TickBannerFade(&g_world); /* 0xA0990 gate + 0xA09AC BLEQ Render_Frame_DecRef */
        World_TickLookControl(&g_world);
    }
    if (g_nUiScreen == 17) {
        MainMenu_TickRowKeys(g_aRowKeys, g_aRowKeySrc, g_nAnimPhase, &g_nPlaneStatRowCount);
    } else if (g_nUiScreen == 0x10) {
        if (++g_titlesAnim.nScroll > 20 * (g_nTitlesMaxRows + 48)) {
            g_titlesAnim.nScroll = 0;
        }
    } else if (g_nUiScreen == 18) {
        ++g_world.nMapTitlesScroll;
    }
}

/* The WM_TIMER 0xC353 body (0xA0C64): UiPulse_OnTimer @ 0x36630 over the Map block. Its geoscape
 * branch (0x36644) returns after one hook call, and that hook is Geoscape_PulseUpdate @ 0x954dc --
 * the geoscape clock tick. The tact branch is the held-key repeat over the live cells; during a
 * mission App_TickPulse routes it to World_PulseTact (Part 105), and only the geoscape/no-mission
 * case runs the plain call below -- with no mission the original's own gate at 0x36660 (LDRB
 * Map_bTactFlag, BEQ loc_36810) keeps the body from doing anything. */
static void App_GeoscapePulseThunk(void *pUser)
{
    (void)pUser;
    if (g_bWorldReady) {
        World_PulseGeoscape(&g_world);
    }
}

static void App_TickPulse(void)
{
    UiPulseHooks hooks;
    UiPulseCtx   ctx;

    /* Port-side (2026-11-04, user request): with the music loop setting OFF,
     * the frame tick hands a finished track over to the next one. A no-op
     * while the setting is ON, which is the original's only behaviour. */
    Sound_MusicProgress();

    if (g_bWorldReady && World_bTactPointerLive(&g_world)) {
        World_PulseTact(&g_world);   /* 0xC353's tact half, over the live mission cells */
        return;
    }

    /* The same gate Input_OnPointer / Input_OnPointerUp use at 0x13CE0 / 0x14908. */
    g_tactState.bGeoscapeUi = (g_bWorldReady && g_world.bGeoscapeUi != 0) ? 1 : 0;

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnPulseUpdate = App_GeoscapePulseThunk;

    memset(&ctx, 0, sizeof(ctx));
    ctx.pState  = &g_tactState;
    ctx.pTimers = &hooks;

    UiPulse_OnTimer(&ctx);
}

/* The WM_TIMER default branch (0xA0C88): `wParam == 50004 && cUiAnimProgress < 101` then the byte
 * increment at 0xA0C90. This is the timer that finishes the panel animation the geoscape screens gate
 * their captions on (Ui_AnimExpandBlit tests progress == 100), and only StartTimers_D arms it. */
static void App_TickAnimProgress(void)
{
    if (g_world.cUiAnimProgress < 101) {
        ++g_world.cUiAnimProgress;
    }
}

/* One firing's tick, i.e. one GameTick_OnTimer @ 0x274f0 call. In this shell it is the original's own
 * no-op, and that is not an omission: 0x27508 tests Map_bGeoscapeUi @ Map+0x44 first, and with it set --
 * which is the state the shell is always in, the world standing at a geoscape screen -- the handler loads
 * sGeoscapeScreenBlock at 0x27514, calls Geoscape_GameTickStub @ 0x94550 at 0x27518 (four bytes, `BX LR`,
 * no body) and returns at 0x2751C. The geoscape clock is 0xC353's job and App_TickPulse runs it.
 *
 *
 * The tactical path is no longer blocked on ownership: World_InitTactical (Part 47) allocates the
 * TactState, World owns the apUiSlotsA[10] word, the launch seam's GeoscapeUi_FreeAll clears
 * Map_bGeoscapeUi at 0x86D34, and the TTL stack lives in the frame's GameMap.status.
 * World_GameTickOnce builds the GameTickCtx from those and calls GameTick_OnTimer, so the
 * geoscape case still lands on the 0x27518 stub return and a live mission takes the tactical
 * arm (TTL decay, GameTick_Step, the 0x275E8 frame-scratch clear). World_GameTickOnce installs
 * World_TactTickActiveUnit, which builds the live move/fall contexts and runs Tact_TickActiveUnit. */
static void App_GameTickOnce(void *pUser)
{
    (void)pUser;
    (void)World_GameTickOnce(&g_world);
}

LRESULT CALLBACK PocketUFO_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static void App_PresentLoadedSurface(void);

/* QoL `--landscape` shell mode: the portrait 240x320 viewport shown UPRIGHT
 * (no rotation) in a 320x240 screen, fit to height and centered. Set by
 * App_FindLandscapeFlag; read by the window size, Gx_Open and App_MapPointer.
 * See docs/parity/landscape-qol-2026-11-04.md. */
static bool g_bLandscapeFlag;

/* P1 wide view (docs/parity/landscape-qol-2026-11-04.md): geoscape-map and
 * tactical screens take the full 320x240 surface in landscape mode; every
 * other screen keeps the portrait 240x320 surface pillarboxed. The geometry
 * is recomputed at the draw entry; the hidden/scripted runs are ALWAYS
 * portrait so every dump, e2e reference and test sees exactly 240x320. */
static bool g_bHiddenRun;
static int  g_nViewW   = 240; /* surface dims, data-driven everywhere */
static int  g_nViewH   = 320;
static int  g_nUiOffX  = 0;   /* native-size UI centered on the wide face */
static int  g_nUiOffY  = 0;   /* bottom-anchored UI (y >= 160) shifts -80 */

static void App_UpdateViewGeometry(void)
{
    const bool bWideScreen =
        g_bWorldReady
        && ((g_world.bGeoscapeUi != 0
             && (g_world.screen.nGeoscapeScreen == 0  /* the map */
                 || g_world.screen.nGeoscapeScreen == 2)) /* new-base placement: the map + confirm */
            || World_bTactPointerLive(&g_world));
    int nW, nH, nX, nY;

    /* Geometry keys on the shell mode only. Scripted runs stay hidden (no
     * window pump, deterministic clock); with `--landscape` they render and
     * dump WIDE frames, which is how the wide layouts are checked by script.
     * The e2e gate never passes the flag, so every dump and reference stays
     * 240x320. */
    Gx_ViewGeometry(g_bLandscapeFlag ? 1 : 0,
                    bWideScreen ? 1 : 0, &nW, &nH, &nX, &nY);
    if (nW == g_nViewW && nH == g_nViewH) {
        return;
    }
    if (g_assets.pSurface == NULL || g_assets.pSurface->pPixels == NULL
        || g_gx.hWnd == NULL) {
        return;
    }
    if (SoftSurf_AllocPixels(g_assets.pSurface, nW, nH, PIXMODE_16BPP) == NULL) {
        return; /* keep the old geometry on allocation failure: a QoL fallback */
    }
    if (!Gx_Resize(&g_gx, (u32)nW, (u32)nH)) {
        return;
    }
    g_nViewW  = nW;
    g_nViewH  = nH;
    g_nUiOffX = nX;
    g_nUiOffY = nY;
    /* The shared UI shim (draw + hit in one place) and the iso column bound. */
    PufoUi_SetGeometry(nX, nY);
    g_world.nViewW = g_bLandscapeFlag ? nW : 0;
}

/* Client pixels to the 240x320 logical surface, inverting Gx_Present's integer-scale letterbox.
 *
 * ADAPTATION, and the older comment here was wrong: the record does NOT feed the raw client point
 * through. 0xA0768..0xA080C unpacks lParam into (x,y) with `MOV R3,R2,LSL#16` / `MOV R1,R2,LSR#16` /
 * `MOV R0,R3,LSR#16` and branches on the DisplayMode held at 0x187F48: mode 0 (240x320) jumps to 0xA07E0
 * with both values untouched; mode 1 (320x240) swaps them at 0xA07D4 (`MOV R2,R0` / `MOV R0,R1` /
 * `MOV R1,R2`); mode 2 (480x640) halves both at 0xA07B8, with the ARM negative-divide idiom
 * (`MOVS` / `ADDMI #1` / `ASR#1` -- see memory `arm-negative-div-idiom`); mode 3 (640x480) halves and
 * rotates at 0xA077C: R2 = x/2, then 0xA07DC moves it into the second argument while `RSB R0,R3,#0xF0`
 * makes the first 240 - y/2. WM_MOUSEMOVE runs the same table with bDown 0 at 0xA0810..0xA0884, and
 * WM_LBUTTONDOWN stores the pressed byte unk_AD710 (pBank+0x1040) at 0xA0804..0xA080C. The Win64 shell has
 * one display geometry -- a letterboxed 240x320 surface, the record's mode 0, its identity row -- so this
 * is the inverse of the shell's own letterbox rather than the mode table. Recorded as a deviation in
 * docs/parity/bugs.tsv; modes 1..3 have no Win64 path that selects them. The QoL `--landscape` shell
 * mode (2026-11-04) shows the same portrait surface UPRIGHT in a 320x240 screen, fitted to the window
 * height and centered; both present and this map share Gx_ViewportRect, so the pointer can never
 * drift from the picture. */
static void App_MapPointer(LPARAM lParam, i32 *pnX, i32 *pnY)
{
    RECT rc;
    int  nWinW, nWinH, nVpX, nVpY, nVpW, nVpH;

    if (!GetClientRect(g_hWndMain, &rc)) {
        *pnX = 0;
        *pnY = 0;
        return;
    }
    nWinW = rc.right - rc.left;
    nWinH = rc.bottom - rc.top;
    Gx_ViewportRect(nWinW, nWinH, g_nViewW, g_nViewH,
                    g_bLandscapeFlag ? 1 : 0, &nVpX, &nVpY, &nVpW, &nVpH);
    Gx_PointerToLogical(nVpX, nVpY, nVpW, nVpH, g_nViewW, g_nViewH,
                        (i32)(short)LOWORD(lParam), (i32)(short)HIWORD(lParam),
                        pnX, pnY);
}

/* Input_OnPointer @ 0x13c6c for the shell, in logical 240x320 coordinates. On screen 17 this
 * is the hover path: it marks the list rows under the pointer in abListHit, which both
 * brightens the row (key forced to 255) and, on release, selects which row's click handler
 * fires. */
static void App_PointerLogical(i32 nX, i32 nY, bool bDown)
{
    const bool bGeoscape = (g_bWorldReady && g_world.bGeoscapeUi != 0);

    if (World_bTactPointerLive(&g_world)) {
        /* The mission owns the pen (Part 60): the launch cleared Map.bGeoscapeUi (0x86D34), so the
         * original's own gate (0x13CE0) would take the tactical branch. The live cells are the
         * world's; World_TactPointerMove mirrors them in, runs Input_OnPointer and flushes back. */
        World_TactPointerMove(&g_world, &g_pointerWorld, nX, nY, bDown);
        return;
    }
    if (!g_bStartupUi && !bGeoscape) {
        return;
    }
    g_tactState.nUiScreen  = (u16)g_nUiScreen;
    g_tactState.bGeoscapeUi = bGeoscape; /* Map.bGeoscapeUi @ Map+0x44, the gate at 0x13CE0 */
    g_tactState.bSlot19Active = g_world.sModal.bOpen ? 1u : 0u; /* Modal_bOpen @ Map+0xAC, the live read at 0x13E58 */
    g_pointerWorld.nListMode = g_nListMode;
    Input_OnPointer(&g_tactState, &g_tactHooks, &g_pointerWorld, nX, nY, bDown);
}

/* SCRIPT coordinate space: the flows are authored in the portrait design
 * coordinates. The QoL wide view shifts lines and controls to the centered
 * face, so a scripted feed maps through the shim. The interactive path
 * (App_OnPointer) is NOT routed here: its pointer arrives in surface coords. */
static void App_ScriptPointerLogical(i32 nX, i32 nY, bool bDown)
{
    App_PointerLogical(PufoUi_MapX(nX), PufoUi_MapY(nY), bDown);
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
    const bool bGeoscape = (g_bWorldReady && g_world.bGeoscapeUi != 0);

    if (g_bWorldReady && g_world.nUiScreen == 18u) {
        g_nUiScreen = 18;
        g_tactState.nUiScreen = 18;
        g_tactState.bGeoscapeUi = 0;
        g_pointerWorld.nListMode = g_nListMode;
        Input_OnPointerUp(&g_tactState, &g_tactHooks, &g_pointerWorld);
        return;
    }

    if (World_bTactPointerLive(&g_world)) {
        World_TactPointerUp(&g_world, &g_pointerWorld); /* Input_OnPointerUp @ 0x148d0 (Part 60) */
        return;
    }
    if (!g_bStartupUi && !bGeoscape && g_nUiScreen != 16) {
        return;
    }
    g_tactState.nUiScreen   = (u16)g_nUiScreen;
    g_tactState.bGeoscapeUi = bGeoscape; /* the same gate at 0x14908 */
    g_tactState.bSlot19Active = g_world.sModal.bOpen ? 1u : 0u; /* Modal_bOpen, the live read at 0x14B68 */
    g_pointerWorld.nListMode = g_nListMode;
    Input_OnPointerUp(&g_tactState, &g_tactHooks, &g_pointerWorld);
}

/* ---- the keyboard dispatch (Part 67) --------------------------------------------------------
 *
 * PocketUFO_WndProc compares WM_KEYDOWN/WM_KEYUP's wParam against the eight slots of the key map at
 * 0x1884D8 (+0 up, +12 down, +24 left, +36 right, +48 action, +60 secondary, +72 confirm, +84 escape,
 * each read with LDRSH) and runs the arm; the chains are ported as Input_OnKeyDown/Input_OnKeyUp in
 * pufo_input.c. The original fills that record with the 96 bytes GXGetDefaultKeys(2) returns
 * (0x9FF78..0x9FFB0) after memsetting it (0x9FEB8) -- the WinCE gx layer's device defaults, which have
 * no Win64 equivalent -- so the shell owns the eight codes as a fixed PC mapping.
 *
 * The route is the pen's route: a mission that owns the pen owns the keys, and its state is the
 * world's staged sTactInput; otherwise the shell's own input state carries the geoscape and start-menu
 * arms, filled the way App_PointerLogical fills it. */
static const WORD g_anKeyMap[PUFO_KEY_COUNT] = {
    VK_UP,     /* key map +0  */
    VK_DOWN,   /* +12  */
    VK_LEFT,   /* +24  */
    VK_RIGHT,  /* +36  */
    VK_RETURN, /* +48  Action: Input_OnAction @ 0xA0F2C / UiScreen_OnBack @ 0xA0D74 */
    VK_SPACE,  /* +60  Secondary: Input_OnSecondary @ 0xA0F44 / UiScreen_DismissHigh @ 0xA0D90 */
    VK_BACK,   /* +72  Confirm: Input_OnConfirm @ 0xA0F5C / UiScreen_OnCancel @ 0xA0DA8 */
    VK_ESCAPE, /* +84  Escape: nullsub_1 @ 0xA0F70 / Input_OnMenuEscape @ 0xA0DC0 */
};

static i32 App_KeySlotOf(WPARAM wParam)
{
    i32 i;

    for (i = 0; i < (i32)PUFO_KEY_COUNT; ++i) {
        if (g_anKeyMap[i] == (WORD)wParam) {
            return i;
        }
    }
    return -1; /* no arm: the original's chain falls through and still returns 0 */
}

/* The shell-side KeyDispatchCtx for the non-mission route: the held bytes are the bank's own
 * (UiBank_bHeldUp/Down/Left/Right @ pBank+0x104C..0x104F), the two scratch bytes are UiBank_bActionFlag
 * @ 0xAD711 and bUiEscapeFlag @ 0xAD712. */
static void App_TactKeyCtxFill(KeyDispatchCtx *pCtx)
{
    memset(pCtx, 0, sizeof(*pCtx));
    g_tactState.nUiScreen   = (u16)g_nUiScreen;                              /* Map_nUiScreen @ Map+0x3A */
    g_tactState.bGeoscapeUi = (g_bWorldReady && g_world.bGeoscapeUi != 0);  /* Map.bGeoscapeUi @ Map+0x44 */
    pCtx->pState        = &g_tactState;
    pCtx->pHooks        = &g_tactHooks;
    pCtx->pWorld        = &g_pointerWorld.tact;   /* the queue is the mission's; no mission, no queue use */
    pCtx->pbHeldDir     = g_bank.abHeldDir;
    pCtx->pbActionFlag  = &g_pointerWorld.bActionFlag;
    pCtx->pbEscapeFlag  = &g_bank.bEscapeFlag;
}

static void App_OnKeyDown(PufoKeySlot eSlot)
{
    KeyDispatchCtx ctx;

    if (World_bTactPointerLive(&g_world)) {
        World_TactKeyDown(&g_world, eSlot); /* the mission's own staging (0xA0DC8 chain) */
        return;
    }
    App_TactKeyCtxFill(&ctx);
    Input_OnKeyDown(&ctx, eSlot);
    g_nUiScreen = (i32)g_tactState.nUiScreen; /* Input_OnMenuEscape's screen-0 arm opens 7 */
}

static void App_OnKeyUp(PufoKeySlot eSlot)
{
    KeyDispatchCtx ctx;

    if (World_bTactPointerLive(&g_world)) {
        World_TactKeyUp(&g_world, eSlot); /* the mission's own staging (0xA0CE0 chain) */
        return;
    }
    App_TactKeyCtxFill(&ctx);
    Input_OnKeyUp(&ctx, eSlot);
    g_nUiScreen = (i32)g_tactState.nUiScreen; /* UiScreen_OnCancel / Input_OnMenuEscape write the id */
}

/* ---- shell-side scaffolding for the --new-game dump. Nothing below is ported behaviour: no function in the image
 * drags the camera to a region, a player does. Game_StartNew leaves the pick pending (0x945E0) and the globe hover
 * samples ONE fixed screen point -- (120, (320 - strip height) / 2), 0x67328 / 0x6734C -- through
 * Smap_GetTileAtScreen @ 0x82244, so what it reports follows the view scroll, and the pick cannot be committed
 * until a buildable tile sits under that point. The steps, in the game's own order:
 *
 *   1. the bottom panel's wipe, stepped by the 0xC354 branch (0xA0C88..0xA0C90) App_TickAnimProgress reproduces:
 *      Geoscape_DrawGlobeHover stops at 0x67490 and hands back 0xFF while it is still running;
 *   2. the drag: a press inside the strip band arms bGeoscapeDragActive @ 0x187074 (0x87D60) and records the pen at
 *      0x87D74/0x87D7C while moving nothing (0x82104/0x82114 add it straight back), and it is the event after it
 *      with bDown clear that reaches Smap_ScrollBy @ 0x820d0 at 0x87D14. A stroke carries at most 100 tiles so the
 *      pen never leaves the pen screen, and the short way round is taken because the map wraps in x.
 *   3. one press and one release on the control the hover seats at (120 - uWidth/2, 290) (0x679F0 / 0x67A00): the
 *      press leaves it in its pressed state (0x874EC) and the release's own preamble (0x89694) releases it wherever
 *      the pen lifted. Its handler is NewBaseScreen_TryConfirm @ 0x6152c -- Sound_Play(17, 4) at 0x61538, funds gate
 *      at 0x6156C skipped while a pick is pending (0x6153C), tail call Geoscape_OnMapClick @ 0x8b4a8.
 *
 * The pointer events go through App_PointerLogical / App_OnPointerUp, i.e. Input_OnPointer @ 0x13c6c and
 * Input_OnPointerUp @ 0x148d0 -- the same wrappers WndProc uses. test_geoscapeframe carries the same two steps as
 * RunFramesUntilHoverRegion and DragCameraOntoRegion and they are deliberately not shared: those drive
 * World_GeoscapePointer directly, this one drives the shell's message-order wrappers. */

static bool App_HoverOnRegion(void)
{
    return g_world.bPendingRegionId >= PUFO_REGION_ID_FIRST
        && g_world.bPendingRegionId < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT;
}

/* The tile under the hover's fixed sample point, resolved the way 0x82244 resolves it (one wrap per axis). A mirror
 * rather than a call on purpose: Smap_GetTileAtScreen also writes view.nPickX/nPickY, and the draw owns that pair. */
static u8 App_HoverSampleTile(i32 nMidY, i32 *pnTileX, i32 *pnTileY)
{
    const SmapView *pView  = &g_world.view;
    const u8       *pTiles = g_world.frame.hover.pTiles;
    i32 nX = pView->nScrollX + pView->nMapW / 2;
    i32 nY = pView->nScrollY + pView->nMapH / 2 + nMidY - 160;

    if (nX < 0) { nX += pView->nMapW; }
    if (nY < 0) { nY += pView->nMapH; }
    *pnTileX = nX;
    *pnTileY = nY;
    if (pTiles == NULL || nX >= pView->nMapW || nY >= pView->nMapH) {
        return 0xFF;
    }
    return pTiles[nY * pView->nMapW + nX];
}

static void App_DragCameraOntoRegion(i32 nMidY)
{
    const SmapView *pView  = &g_world.view;
    const u8       *pTiles = g_world.frame.hover.pTiles;
    i32 nTileX, nTileY, nRegX = -1, nRegY = -1, nBest = 0x7FFFFFFF;
    i32 nTile = App_HoverSampleTile(nMidY, &nTileX, &nTileY);
    i32 nX, nY, nDx, nDy;

    if (nTile >= PUFO_REGION_ID_FIRST && nTile < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT) {
        return;
    }
    if (pTiles == NULL) {
        return;
    }
    for (nY = 0; nY < pView->nMapH; ++nY) {
        for (nX = 0; nX < pView->nMapW; ++nX) {
            const u8 nCand = pTiles[nY * pView->nMapW + nX];
            i32 nDist;
            if (nCand < PUFO_REGION_ID_FIRST || nCand >= PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT) {
                continue;
            }
            nDist = (nX - nTileX) * (nX - nTileX) + (nY - nTileY) * (nY - nTileY);
            if (nDist < nBest) {
                nBest = nDist;
                nRegX = nX;
                nRegY = nY;
            }
        }
    }
    if (nRegX < 0) {
        return;
    }
    for (int i = 0; i < 12 && (nTile < PUFO_REGION_ID_FIRST || nTile >= PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT);
         ++i) {
        nDx = nRegX - nTileX;
        nDy = nRegY - nTileY;
        if (nDx >  pView->nMapW / 2) { nDx -= pView->nMapW; }
        if (nDx < -pView->nMapW / 2) { nDx += pView->nMapW; }
        if (nDx >  100) { nDx =  100; } else if (nDx < -100) { nDx = -100; }
        if (nDy >  100) { nDy =  100; } else if (nDy < -100) { nDy = -100; }
        App_ScriptPointerLogical(120, nMidY, true);                          /* the press arm moves nothing */
        App_ScriptPointerLogical(120 - nDx, nMidY - nDy, false);            /* this one scrolls */
        App_OnPointerUp();                                            /* 0x896BC */
        App_DrawFrame();
        nTile = App_HoverSampleTile(nMidY, &nTileX, &nTileY);
    }
}

static void App_NewGamePickThroughPen(void)
{
    UiControl      *pConfirm = g_world.ui.apCtl[GEOUI_CTL_186BA4];
    const PadImage *pStrip   = g_world.ui.apPad[GEOUI_PAD_186BBC];
    i32 nMidY;

    if (pConfirm == NULL || pStrip == NULL) {
        return;
    }
    nMidY = (320 - (i32)pStrip->uHeight) / 2; /* 0x67328 / 0x6734C */

    for (int i = 0; i < 12 && !App_HoverOnRegion(); ++i) {
        App_TickAnimProgress();
        App_DrawFrame();
    }
    App_DragCameraOntoRegion(nMidY);
    (void)App_DrawFrame(); /* 0x91D94 stores the hovered tile and 0x679F0 seats the control on the same draw */
    if (!App_HoverOnRegion()) {
        return;
    }
    App_ScriptPointerLogical(pConfirm->x + 2, pConfirm->y + 2, true);
    App_OnPointerUp();
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
     * this call no glyph ever draws. The 2bpp is the original's own; the dimensions follow the
     * live surface (QoL wide view 320x240 in landscape, 240x320 everywhere else). */
    Render_SetTarget(g_nViewW, g_nViewH, 2, g_gx.cbxPitch, g_gx.cbyPitch, g_gx.pBits);
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

/* `--landscape` on the command line: QoL shell mode, a 320x240 screen that
 * keeps the portrait viewport upright and centered (fit to height, side
 * bands). Not part of the original; see docs/parity/landscape-qol-2026-11-04.md.
 * The original's own mode table (0xA0768..0xA080C, DisplayMode @ 0x187F48)
 * knows wider screens too, and the Win64 shell had no path selecting them --
 * a gap recorded in docs/parity/bugs.tsv. */
static void App_FindLandscapeFlag(LPCWSTR pwszCmd)
{
    static const WCHAR awcFlag[] = L"--landscape";

    g_bLandscapeFlag = (pwszCmd != NULL && wcsstr(pwszCmd, awcFlag) != NULL);
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

static WCHAR g_awcScriptPath[MAX_PATH];

/* `--native-loop` keeps a scripted native-window run alive after its prep steps. It is a
 * separate token from `--script`, so it cannot change the normal script-only teardown. */
static bool App_HasNativeLoopFlag(LPCWSTR pwszCmd)
{
    static const WCHAR awcFlag[] = L"--native-loop";
    LPCWSTR pwszArg;
    const size_t cchFlag = (sizeof(awcFlag) / sizeof(awcFlag[0])) - 1u;

    if (pwszCmd == NULL) {
        return false;
    }
    pwszArg = wcsstr(pwszCmd, awcFlag);
    while (pwszArg != NULL) {
        const bool bLeftBoundary = pwszArg == pwszCmd || pwszArg[-1] == L' ';
        const bool bRightBoundary = pwszArg[cchFlag] == L' ' || pwszArg[cchFlag] == L'\0';

        if (bLeftBoundary && bRightBoundary) {
            return true;
        }
        pwszArg = wcsstr(pwszArg + 1, awcFlag);
    }
    return false;
}

/* `--script <path>` on the command line: run the step list at App_RunDebugScript and leave.
 * Not part of the original. */
static LPCWSTR App_FindScriptFlag(LPCWSTR pwszCmd)
{
    static const WCHAR awcFlag[] = L"--script";
    LPCWSTR            pwszArg;
    int                i = 0;

    if (pwszCmd == NULL) {
        return NULL;
    }
    pwszArg = wcsstr(pwszCmd, awcFlag);
    if (pwszArg == NULL) {
        return NULL;
    }
    pwszArg += (int)(sizeof(awcFlag) / sizeof(WCHAR)) - 1;
    while (*pwszArg == L' ') {
        ++pwszArg;
    }
    while (*pwszArg != L'\0' && *pwszArg != L' ' && i < MAX_PATH - 1) {
        g_awcScriptPath[i++] = *pwszArg++;
    }
    g_awcScriptPath[i] = L'\0';
    return (i == 0) ? NULL : g_awcScriptPath;
}

/* Writes the logical 240x320 RGB565 surface as a binary PPM (P6). A debug hook for
 * `pocketufo --dump-frame <path>`: the rendered frame can then be inspected or compared without
 * a screen grab. Not part of the original — the original presents through GX. */
static void App_DumpFramePpm(LPCWSTR pwszPath)
{
    const SoftSurf *p = g_assets.pSurface;
    HANDLE hFile;
    char   szHeader[64];
    char   aRow[320 * 3]; /* the logical surface: 240 wide in portrait, 320 in the QoL wide view */
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

/* `--script <path>`: the end-to-end driver (the shell's own scaffolding, not the original).
 *
 * One step per line; `#` starts a comment. `click`, `press`, and `release` use the app's
 * logical pen path (Input_OnPointer / Input_OnPointerUp); they do not post OS mouse messages.
 * The native-window smoke covers OS-posted input separately:
 *
 *   dump <path>   render one frame, write it as a PPM, print the step
 *   click x y     press+release at logical 240x320 coordinates
 *   press x y     press at logical coordinates and keep the pen down across later steps
 *   release       send the app's pointer-up input with no new coordinates
 *   tick n        n anim ticks (0xC352) plus n pulses (0xC353) and n anim-progress ticks (0xC354),
 *                 each step followed by a paint, as WndProc's timers and WM_PAINT interleave
 *   fasttick n    the same three timer bodies with the paint dropped -- SHELL SCAFFOLDING for running
 *                 geoscape clock time in seconds instead of minutes; it cannot open or advance a panel,
 *                 so every press still goes through `tick` or `click` with a real frame
 *   gametick n    n firings of 0xC350 through Timer_TickBySpeed, the same dispatch the
 *                 WM_TIMER switch runs at 0xA08B0/0xA08C0 -- the mission clock, which walks
 *                 the tactical queue (Tact_TickActiveUnit @ 0x54130) and the phases
 *   tact          print the live mission state: queue, phase, cursor, rosters, units
 *   mindendcheck  SHELL FIXTURE: run the live mission's lost last-base check and app ending path
 *   mindgoodcheck SHELL FIXTURE: run the live alien-base scan through World_MissionEndCheck(0)
 *   mindbankreport advance a live new game to its monthly report with funds below the bankruptcy limit
 *   mindendstate   print both screen cells and the ending resources without reading freed roster entries
 *   newgame       App_HookNewGame -- what the menu's new-game row runs
 *   pick          App_NewGamePickThroughPen -- the placement screen's confirm, through the pen
 *   close         World_CloseGeoscape -- the placement screen's close control
 *   save n        App_HookSaveGame -- World_SaveSlot, what a save-slot row runs
 *   load n        App_HookLoadGame -- LoadGame @ 0x44bbc, what a load-slot row runs
 *   state         print the shell's own state line
 *   grid n        print base n's 6x6 facility grid, the layer2 occupancy bytes and the build days
 *   geoframe      print the geoscape frame env: which slots are wired, which are NULL
 *   strateg       print the strateg list as the geoscape clock left it (the mission path's state)
 *   strategystate label print live base, staff, job, stock, site and control state without changing it
 *   campaign approach n use live path costs and pen input to move toward live enemy n
 *   campaign natural-fight n bounded live approach/fire/turn inputs; only a real screen-13 win passes
 *   campaign natural-fight-outside-plane n bounded type-1 fight with every shooter outside the live craft footprint
 *   campaign cargo label print measured per-craft object quantities and site overflow
 *   campaign month-boundary load slot 3, reach report 19 by clock pulses, close its live control, save slot 4
 *   campaign month-stamps label print clock stamps and fresh-load scene readiness
 *   campaign close-debrief close only an already-won screen-13 debrief through its OK input
 *   lines         print the root line list: the flags and colour slots the mission launch reads
 *   equiparmor    SHELL SCAFFOLDING (Part 168): stock one Power suit at the selected base, then wear it
 *                 through the soldier screen's own controls -- nav slot 2 (SoldierScreen_Toggle 0x64af0),
 *                 find button 3 (Ui_SetTransferFlag 0x619a8), SoldierScreen_SelectListed 0x64a9c,
 *                 SoldierScreen_EquipSelected 0x619cc. Only the stocking is scaffolding: main.dat holds no
 *                 armour at any base, and the economy path to a suit (research plus manufacture) is not a
 *                 step these flows run.
 *
 * The point is one process walking the whole path -- menu, new game, placement, geoscape, save,
 * load -- writing a picture at each step so each step is checkable as a picture. */
static void App_ScriptDump(const char *pszName)
{
    WCHAR awcPath[MAX_PATH];

    (void)MultiByteToWideChar(CP_ACP, 0, pszName, -1, awcPath, MAX_PATH - 1);
    awcPath[MAX_PATH - 1] = L'\0';
    App_DrawFrame();
    App_DumpFramePpm(awcPath);
    printf("pocketufo: step dump %s\n", pszName);
    fflush(stdout);
}

static void App_ScriptState(const char *pszLabel)
{
    /* The three cells the status panel's chain tail reads (the line-0 flag decides whether the tail runs at
     * all, 0x8C798/0x8C7AC; the screen id decides map bar vs globe hover, 0x91CF4; the sweep counters are the
     * hover's own animation state). Printed because a picture with no bottom bar is only explainable by them. */
    printf("pocketufo: state %s: start screen %d, list mode %d, geoscape screen %d, geo ui byte %d, tact flag %d, "
           "bases %d, base 0 at %d/%d, soldiers %d, funds %d, status flag %d, pending region %d, "
           "globe sweep %d/%d, anim prog %d/%d/%d, mission type %d, map 0x1511E %d\n",
           pszLabel, g_nUiScreen, g_nListMode, (int)g_world.screen.nGeoscapeScreen, (int)g_world.bGeoscapeUi,
           (int)g_world.bTactFlag, (int)g_world.nBaseCount,
           (g_world.nBaseCount > 0 && g_world.apBases[0] != NULL) ? g_world.apBases[0]->nUnk8 : 0,
           (g_world.nBaseCount > 0 && g_world.apBases[0] != NULL) ? g_world.apBases[0]->nUnk12 : 0,
           (int)g_world.nSoldierCount, (int)g_world.nFunds,
           (int)UiLineList_GetFlag(&g_world.msgLines, 0), (int)g_world.bPendingRegionId,
           g_world.nGlobeSweepV, g_world.nGlobeSweepH,
           (int)g_world.cUiAnimProgress, (int)g_world.bUiAnimStarted, (int)g_world.aRegionBuildCount[9],
           (int)g_world.nMapMissionType, (int)g_world.bMapUnk1511E);
    fflush(stdout);
}

/* Read-only handoff for the native tactical-window probe. The smoke runner uses this live bank
 * rectangle to post one real OS mouse click at End Turn, without guessing a control offset. */
static void App_ScriptNativeTactInfo(void)
{
    const GameMap *pMap = g_world.pTactMap;
    const Soldier *pSel = (pMap != NULL) ? pMap->pSelUnit : NULL;
    const UiControl *pEndTurn = &g_bank.aCtl[5];
    const i32 nXcom = (pMap != NULL) ? (i32)pMap->nXcomCount : 0;
    const i32 nEnemy = (pMap != NULL) ? (i32)pMap->nEnemyCount : 0;
    const i32 nQueue = (g_world.pTactState != NULL) ? (i32)g_world.pTactState->nQueueLen : -1;
    const i32 nQueueActive = (g_world.pTactState != NULL) ? (i32)g_world.pTactState->bQueueActive : 1;
    const i32 nPhase = (pMap != NULL) ? (i32)pMap->tact.nPhase : -1;
    const i32 nSelectedType = (pSel != NULL) ? (i32)pSel->nUnitType : 0;
    const bool bSelectedXcom = pSel != NULL && pSel->nUnitType <= 8u;
    const i32 nCenterX = (i32)pEndTurn->x + (i32)pEndTurn->uWidth / 2;
    const i32 nCenterY = (i32)pEndTurn->y + (i32)pEndTurn->uHeight / 2;
    const bool bReady = g_bWorldReady && g_world.bTactLive && g_world.bTactFlag != 0
        && pMap != NULL && g_world.pTactState != NULL && g_world.nUiScreen == 0u
        && g_world.bMapPlayerInput != 0 && g_world.bMapEnemyTurnActive == 0
        && pMap->tact.bBusy == 0 && nPhase == 0 && nQueue == 0 && nQueueActive == 0
        && nXcom > 0 && nEnemy > 0 && bSelectedXcom
        && pEndTurn->bEnabled != 0 && pEndTurn->pfnClick != NULL;

    printf("pocketufo: native_tact_ready=%d screen=%u tact=%u live=%u input=%u enemyturn=%u busy=%u "
           "phase=%d queue=%d queueactive=%d xcom=%d enemy=%d selected=%d endturn_enabled=%u endturn_click=%u "
           "logical=%d,%d size=%u,%u center=%d,%d\n",
           bReady ? 1 : 0, (unsigned)g_world.nUiScreen, (unsigned)g_world.bTactFlag,
           g_world.bTactLive ? 1u : 0u, (unsigned)g_world.bMapPlayerInput,
           (unsigned)g_world.bMapEnemyTurnActive, pMap != NULL ? (unsigned)pMap->tact.bBusy : 0u,
           nPhase, nQueue, nQueueActive, nXcom, nEnemy, nSelectedType,
           (unsigned)pEndTurn->bEnabled, pEndTurn->pfnClick != NULL ? 1u : 0u,
           (int)pEndTurn->x, (int)pEndTurn->y, (unsigned)pEndTurn->uWidth,
           (unsigned)pEndTurn->uHeight, nCenterX, nCenterY);
    fflush(stdout);
}

/* DEVIATION (shell scaffolding, not part of the original): the base layout the placement screen draws,
 * printed as numbers. tileA/tileB/layer2 are the three stamped layers, facTyp the facility each cell
 * belongs to, days the build countdown. The craft rows are what BaseFacScreen_DrawNameGrid's hangar pass
 * compares against (Ship.bHangarX/Y @ +0x12/+0x13 against the cell's column and row, with bAtBase and
 * bHomeBase as the other two gates at 0x6216C..0x6219C). */
static void App_ScriptGrid(i32 nBase)
{
    static const char *const azLayer[4] = { "tileA", "tileB", "layer2", "facTyp" };
    static const i32 aOff[4] = { PUFO_GRID_TILEA_OFF, PUFO_GRID_TILEB_OFF,
                                 PUFO_GRID_LAYER2_OFF, PUFO_GRID_FAC_TYPE_OFF };
    const i32 nFac = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase;

    printf("pocketufo: grid base %d\n", nBase);
    for (i32 L = 0; L < 4; ++L) {
        for (i32 r = 0; r < 6; ++r) {
            printf("  %s row %d:", azLayer[L], r);
            for (i32 c = 0; c < 6; ++c) {
                printf(" %3u", (unsigned)g_world.abFacilityGrid[nFac + aOff[L] + 6 * r + c]);
            }
            printf("\n");
        }
    }
    for (i32 r = 0; r < 6; ++r) {
        printf("  days  row %d:", r);
        for (i32 c = 0; c < 6; ++c) {
            printf(" %3u", (unsigned)g_world.abFacilityGrid[PUFO_GRID_DAYS_STRIDE * nBase + 6 * r + c]);
        }
        printf("\n");
    }
    for (i32 i = 0; i < (i32)g_world.bCraftCount; ++i) {
        const Ship *pShip = g_world.apCraft[i];

        if (pShip == NULL) {
            continue;
        }
        printf("  craft %d: type %d home %d atBase %d hangar %d/%d\n", i,
               (i32)pShip->bCraftType, (i32)pShip->bHomeBase, (i32)pShip->bAtBase,
               (i32)pShip->bHangarX, (i32)pShip->bHangarY);
    }
    fflush(stdout);
}

/* DEVIATION (shell scaffolding, not part of the original): the geoscape frame's own wiring, printed.
 * Geoscape_BlitMapView returns without drawing when any one of its seven inputs is missing, so this is
 * the quickest way to see why a frame comes out as whatever the surface held before. */
static void App_ScriptGeoFrame(void)
{
    const GeoscapeFrameState *pF = &g_world.frame;

    printf("pocketufo: geo frame: ui byte %d, screen %d, zoom mode %d, scroll %d/%d, map %dx%d\n",
           (int)g_world.bGeoscapeUi, (int)g_world.screen.nGeoscapeScreen, (int)g_world.view.nMode,
           (int)g_world.view.nScrollX, (int)g_world.view.nScrollY,
           (int)g_world.view.nMapW, (int)g_world.view.nMapH);
    for (i32 z = 0; z < 3; ++z) {
        printf("  set %d: grid %d overlay %d tiles %d\n", z,
               g_world.sets[z].pGrid != NULL, g_world.sets[z].pOverlay != NULL,
               g_world.sets[z].pTiles != NULL);
    }
    printf("  map env: view %d mapPad %d layerPad %d worldBits %d mapPal %d time %d overlayX %d\n",
           pF->map.pView != NULL, pF->map.pMapPad != NULL, pF->map.pLayerPad != NULL,
           pF->map.pWorldBits != NULL, pF->map.pMapPal != NULL, pF->map.pTime != NULL,
           pF->map.pnOverlayOriginX != NULL);
    printf("  draw env: mapView %d crosshair %d status %d radar %d worldSize %d\n",
           pF->draw.pMapView != NULL, pF->draw.pCrosshair != NULL, pF->draw.pStatusPanel != NULL,
           pF->draw.pRadarArm != NULL, g_world.ui.pWorldSize != NULL);
    fflush(stdout);
}

/* SHELL SCAFFOLDING, not original behaviour: the strateg list as the geoscape clock leaves it. The
 * mission path is the one thing the shell cannot yet check with a picture, and every step of it moves
 * these cells -- the event pass that spawns a craft, the travel that rolls a mission subtype, the
 * spawner that sets nPhase, the panel that offers the order -- so a run has to be able to say what the
 * list holds. The fields are StrategItem's own (game/pufo_strateg.h). */
static void App_ScriptStrateg(void)
{
    const PtrArray *pList = &g_world.strategList;

    printf("pocketufo: strateg: %d items, count mirror 0x186530 %d, seed %d, selected idx %d, clock %d s\n",
           pList->nCount, (int)g_world.saveStrateg.nStrategCount, (int)g_world.nStrategSeed,
           (int)g_world.nSelectedStrategIdx, (int)g_world.geo.time.nTotalSec);
    for (int i = 0; i < pList->nCount; ++i) {
        const StrategItem *p = (const StrategItem *)pList->apItems[i];

        if (p == NULL) {
            printf("  [%d] (null)\n", i);
            continue;
        }
        printf("  [%d] type %d active %d class %d at %d/%d site %d disp %d sub %d phase %d "
               "detected %d race %d done %d targetIdx %d travel %u min\n",
               i, (int)p->nType, (int)p->bActive, (int)p->bCraftClass, p->nX, p->nY, p->nSiteId,
               p->nDisplayId, (int)p->nMissionSub, p->nPhase, (int)p->bDetected, (int)p->bRace,
               (int)p->bDone, p->nTargetIdx, (unsigned)p->uTravelMin);
    }
    fflush(stdout);
}

static void App_ScriptStrategyControl(const char *pszName, i32 nIndex, const UiControl *pCtl)
{
    printf("pocketufo: strategy control %s %d present %d enabled %d rect %d/%d/%u/%u mode %u state %u callback %d\n",
           pszName, (int)nIndex, pCtl != NULL, pCtl != NULL ? (int)pCtl->bEnabled : 0,
           pCtl != NULL ? (int)pCtl->x : -1, pCtl != NULL ? (int)pCtl->y : -1,
           pCtl != NULL ? (unsigned)pCtl->uWidth : 0u,
           pCtl != NULL ? (unsigned)pCtl->uHeight : 0u,
           pCtl != NULL ? (unsigned)pCtl->nMode : 0u,
           pCtl != NULL ? (unsigned)pCtl->nState : 0u,
           pCtl != NULL && pCtl->pfnClick != NULL);
}

/* Read-only state for the real strategy UI regression. It prints the records the UI and hourly
 * callbacks already own; it does not open screens, assign staff, add stock, or advance time. */
static void App_ScriptStrategyState(const char *pszLabel)
{
    const i32 nBase = (i32)g_world.nSelectedBase;
    const bool bBaseValid = nBase >= 0 && nBase < (i32)g_world.nBaseCount
        && g_world.apBases[nBase] != NULL;
    const ObjectRecFull *pEngineer = (g_assets.apObjects != NULL && g_assets.nObjectCount > 112)
        ? g_assets.apObjects[112] : NULL;
    const ObjectRecFull *pScientist = (g_assets.apObjects != NULL && g_assets.nObjectCount > 113)
        ? g_assets.apObjects[113] : NULL;
    const i32 nEngineerTotal = bBaseValid && pEngineer != NULL
        ? Base_GetItemQty(pEngineer, nBase) : 0;
    const i32 nScientistTotal = bBaseValid && pScientist != NULL
        ? Base_GetItemQty(pScientist, nBase) : 0;
    const i32 nEngineerFree = bBaseValid && pEngineer != NULL
        ? Base_GetEngineerCount(pEngineer,
                                g_world.auManufCol + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase, nBase,
                                (i32)g_world.nResearchEntries) : 0;
    const i32 nScientistFree = bBaseValid && pScientist != NULL
        ? Base_GetScientistCount(pScientist,
                                 g_world.auResearchCol + PUFO_RESEARCH_TOPIC_COUNT * nBase, nBase,
                                 (i32)g_world.nResearchEntries) : 0;
    const i32 nResearchAssigned = bBaseValid ? Base_CountActiveResearch(
        g_world.auResearchCol + PUFO_RESEARCH_TOPIC_COUNT * nBase,
        (i32)g_world.nResearchEntries) : 0;
    const i32 nEngineerAssigned = bBaseValid ? Base_CountActiveManuf(
        g_world.auManufCol + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase,
        (i32)g_world.nResearchEntries) : 0;

    printf("pocketufo: strategy tuple %s: screen %d/%d baseCount %d selectedBase %d base %d coords %d/%d "
           "funds %d sec %d date %d/%d/%d %d:%02d tickSec %d pause %u sites %d soldiers %d craft %u "
           "researchAssigned %d engineerAssigned %d engineersAvailable/Total %d/%d "
           "scientistsAvailable/Total %d/%d lab %d workshop %d stores %d quarters %d occupied %d\n",
           (pszLabel != NULL) ? pszLabel : "requested", (int)g_nUiScreen,
           (int)g_world.screen.nGeoscapeScreen, (int)g_world.nBaseCount, nBase, bBaseValid ? 1 : 0,
           bBaseValid ? g_world.apBases[nBase]->nUnk8 : 0,
           bBaseValid ? g_world.apBases[nBase]->nUnk12 : 0,
           (int)g_world.nFunds, (int)g_world.geo.time.nTotalSec,
           (int)g_world.geo.time.nYear, (int)g_world.geo.time.nMonth, (int)g_world.geo.time.nDay,
           (int)g_world.geo.time.nHour, (int)g_world.geo.time.nMinute,
           (int)g_world.geo.world.nTickSeconds, (unsigned)g_world.geo.time.bPaused,
           (int)g_world.nSiteCount, (int)g_world.nSoldierCount, (unsigned)g_world.bCraftCount,
           nResearchAssigned, nEngineerAssigned, nEngineerFree, nEngineerTotal,
           nScientistFree, nScientistTotal,
           bBaseValid ? Base_CalcLabCap(g_world.abFacilityGrid, nBase) : 0,
           bBaseValid ? Base_CalcWorkshopCap(g_world.abFacilityGrid, nBase) : 0,
           bBaseValid ? Base_CalcStoresCapacity(g_world.abFacilityGrid, nBase) : 0,
           bBaseValid ? Base_CalcLivingQuartersCap(g_world.abFacilityGrid, nBase) : 0,
           bBaseValid ? Base_CalcOccupiedSpace(g_world.apSoldiers, g_world.nSoldierCount,
                                               g_assets.apObjects, (i32)g_assets.nObjectCount, nBase) : 0);

    if (bBaseValid && g_assets.apObjects != NULL) {
        for (i32 nSlot = 1; nSlot < (i32)g_assets.nObjectCount && nSlot < 256; ++nSlot) {
            const ObjectRecFull *pObj = g_assets.apObjects[nSlot];
            const i32 nStock = (pObj != NULL) ? Base_GetItemQty(pObj, nBase) : 0;
            if (nStock != 0) {
                printf("pocketufo: strategy stock %s: slot %d qty %d\n",
                       (pszLabel != NULL) ? pszLabel : "requested", (int)nSlot, (int)nStock);
            }
        }

        for (i32 nTopic = 1; nTopic < (i32)g_world.nResearchEntries
                             && nTopic < PUFO_RESEARCH_TOPIC_COUNT; ++nTopic) {
            const ResearchTopicLive *pTopic = &g_world.aTopics[nTopic];
            const u16 nAssigned = g_world.auResearchCol[PUFO_RESEARCH_TOPIC_COUNT * nBase + nTopic];
            if (pTopic->bActive != 0 || pTopic->bUnlocked != 0 || pTopic->wProgress != 0
                || nAssigned != 0) {
                printf("pocketufo: strategy topic %s: id %d active %u unlocked %u progress %u/%u scientists %u\n",
                       (pszLabel != NULL) ? pszLabel : "requested", (int)nTopic,
                       (unsigned)pTopic->bActive, (unsigned)pTopic->bUnlocked,
                       (unsigned)pTopic->wProgress, (unsigned)pTopic->wTotal, (unsigned)nAssigned);
            }
            if (pTopic->bUnlocked != 0) {
                for (i32 nItem = 0; nItem < 5; ++nItem) {
                    const i32 nSlot = (i32)pTopic->abItemIds[nItem];
                    const ObjectRecFull *pObj = nSlot > 0 && nSlot < (i32)g_assets.nObjectCount
                        ? g_assets.apObjects[nSlot] : NULL;
                    if (pObj != NULL && pObj->uManufWorkPerUnit != 0) {
                        printf("pocketufo: strategy unlocked-product %s: topic %d item %d work %u comps %u/%u/%u/%u stock %u/%u/%u/%u\n",
                               (pszLabel != NULL) ? pszLabel : "requested", (int)nTopic, (int)nSlot,
                               (unsigned)pObj->uManufWorkPerUnit,
                               (unsigned)pObj->abManufComponents[0], (unsigned)pObj->abManufComponents[1],
                               (unsigned)pObj->abManufComponents[2], (unsigned)pObj->abManufComponents[3],
                               g_assets.apObjects[105] != NULL ? (unsigned)Base_GetItemQty(g_assets.apObjects[105], nBase) : 0u,
                               g_assets.apObjects[106] != NULL ? (unsigned)Base_GetItemQty(g_assets.apObjects[106], nBase) : 0u,
                               g_assets.apObjects[107] != NULL ? (unsigned)Base_GetItemQty(g_assets.apObjects[107], nBase) : 0u,
                               g_assets.apObjects[109] != NULL ? (unsigned)Base_GetItemQty(g_assets.apObjects[109], nBase) : 0u);
                    }
                }
            }
        }

        for (i32 nSlot = 1; nSlot < (i32)g_assets.nObjectCount
                            && nSlot < PUFO_PRODUCT_ENTRIES_PER_BASE; ++nSlot) {
            const i32 nProductIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nBase + nSlot;
            const ObjectRecFull *pObj = g_assets.apObjects[nSlot];
            const i32 nRequest = g_world.anProductReq[nProductIdx];
            const u16 nEngineers = g_world.auManufCol[nProductIdx];
            const u16 nCommitted = g_world.auProductU16[nProductIdx];
            const u32 nWork = g_world.auProductU32[nProductIdx];
            if (nRequest != 0 || nEngineers != 0 || nCommitted != 0 || nWork != 0) {
                printf("pocketufo: strategy product %s: item %d work %u comps %u/%u/%u/%u stock %d request %d engineers %u committed %u accumulated %u\n",
                       (pszLabel != NULL) ? pszLabel : "requested", (int)nSlot,
                       pObj != NULL ? (unsigned)pObj->uManufWorkPerUnit : 0u,
                       pObj != NULL ? (unsigned)pObj->abManufComponents[0] : 0u,
                       pObj != NULL ? (unsigned)pObj->abManufComponents[1] : 0u,
                       pObj != NULL ? (unsigned)pObj->abManufComponents[2] : 0u,
                       pObj != NULL ? (unsigned)pObj->abManufComponents[3] : 0u,
                       pObj != NULL ? Base_GetItemQty(pObj, nBase) : 0,
                       nRequest, (unsigned)nEngineers, (unsigned)nCommitted, (unsigned)nWork);
            }
        }
    }

    if (g_world.apSites != NULL && g_world.nSiteCount > 0
        && g_world.nSiteCount <= PUFO_SMAP_SITE_COUNT) {
        for (i32 i = 0; i < g_world.nSiteCount; ++i) {
            const SmapSite *pSite = &g_world.apSites[i];
            printf("pocketufo: strategy site %s: index %d flag %u target %u soldiers %u soldierAssign %u craftCount %u hasLoot %u\n",
                   (pszLabel != NULL) ? pszLabel : "requested", (int)i,
                   (unsigned)pSite->bFlag0, (unsigned)pSite->bFlag1,
                   (unsigned)pSite->uRecoveredSoldiers, (unsigned)pSite->bRecoveredSoldierAssign,
                   (unsigned)pSite->bRecoveredCraftCount, (unsigned)pSite->bHasLoot);
            for (i32 nSlot = 1; nSlot < (i32)g_assets.nObjectCount && nSlot < 256; ++nSlot) {
                if (pSite->auItemQty[nSlot] != 0) {
                    printf("pocketufo: strategy site-item %s: site %d slot %d qty %u\n",
                           (pszLabel != NULL) ? pszLabel : "requested", (int)i, (int)nSlot,
                           (unsigned)pSite->auItemQty[nSlot]);
                }
            }
        }
    }

    for (i32 i = 0; i < PUFO_NAV_CTLS; ++i) {
        App_ScriptStrategyControl("nav", i, g_world.nav.apNav[i]);
    }
    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        App_ScriptStrategyControl("geo", i, g_world.ui.apCtl[i]);
    }
    for (i32 i = 2; i < 8; ++i) {
        App_ScriptStrategyControl("speed", i, g_world.ui.apRootCtl[i]);
    }
    App_ScriptStrategyControl("close", GEOUI_CTL_186B00, g_world.ui.apCtl[GEOUI_CTL_186B00]);
    App_ScriptStrategyControl("confirm", GEOUI_CTL_186BA4, g_world.ui.apCtl[GEOUI_CTL_186BA4]);

    if (g_world.screen.nGeoscapeScreen == 8) {
        for (i32 i = 0; i < 4; ++i) {
            App_ScriptStrategyControl("research", i, &g_world.aResearchCtl[i]);
        }
    } else if (g_world.screen.nGeoscapeScreen == 9) {
        for (i32 i = 0; i < 6; ++i) {
            App_ScriptStrategyControl("manuf", i, &g_world.aManufCtl[i]);
        }
    } else if (g_world.screen.nGeoscapeScreen == 10) {
        for (i32 i = 0; i < 4; ++i) {
            App_ScriptStrategyControl("transfer", i, &g_world.aTransferCtl[i]);
        }
    } else if (g_world.screen.nGeoscapeScreen == 11) {
        for (i32 i = 0; i < 4; ++i) {
            App_ScriptStrategyControl("shop", i, &g_world.aBuySellCtl[i]);
        }
    }

    if (g_world.pResearchTopicPanel != NULL) {
        const UiPanel *pPanel = g_world.pResearchTopicPanel;
        printf("pocketufo: strategy panel %s: research rows %d pos %d/%d font %d\n",
               (pszLabel != NULL) ? pszLabel : "requested", (int)pPanel->nLineCount,
               (int)pPanel->nPosX, (int)pPanel->nPosY, (int)pPanel->nFont);
        for (i32 i = 0; i < pPanel->nLineCount; ++i) {
            printf("pocketufo: strategy row %s: research %d aux %d\n",
                   (pszLabel != NULL) ? pszLabel : "requested", (int)i,
                   UiPanel_GetLineAux((UiPanel *)pPanel, i));
        }
    }
    if (g_world.pManufProductPanel != NULL) {
        const UiPanel *pPanel = g_world.pManufProductPanel;
        printf("pocketufo: strategy panel %s: manuf rows %d pos %d/%d font %d\n",
               (pszLabel != NULL) ? pszLabel : "requested", (int)pPanel->nLineCount,
               (int)pPanel->nPosX, (int)pPanel->nPosY, (int)pPanel->nFont);
        for (i32 i = 0; i < pPanel->nLineCount; ++i) {
            printf("pocketufo: strategy row %s: manuf %d aux %d\n",
                   (pszLabel != NULL) ? pszLabel : "requested", (int)i,
                   UiPanel_GetLineAux((UiPanel *)pPanel, i));
        }
    }
    if (g_world.screen.nGeoscapeScreen == 10 && g_world.pBuySellPanelA != NULL) {
        const UiPanel *pNames = g_world.pBuySellPanelA;
        const UiPanel *pSource = g_world.pBuySellPanelD;
        const UiPanel *pPending = g_world.pBuySellPanelE;
        printf("pocketufo: strategy transfer panel %s: rows %d pos %d/%d style %d scroll %d "
               "sourceRows %d pendingRows %d\n",
               (pszLabel != NULL) ? pszLabel : "requested", (int)pNames->nLineCount,
               (int)pNames->nPosX, (int)pNames->nPosY, (int)pNames->nStyle,
               (int)pNames->nUnk3028, pSource != NULL ? (int)pSource->nLineCount : -1,
               pPending != NULL ? (int)pPending->nLineCount : -1);
        for (i32 i = 0; i < pNames->nLineCount; ++i) {
            const i32 nSlot = UiPanel_GetLineAux((UiPanel *)pNames, i);
            const i32 nSource = pSource != NULL && i < pSource->nLineCount
                ? UiPanel_GetLineAux((UiPanel *)pSource, i) : -1;
            const i32 nPending = pPending != NULL && i < pPending->nLineCount
                ? UiPanel_GetLineAux((UiPanel *)pPending, i) : -1;
            printf("pocketufo: strategy transfer-row %s: row %d slot %d source %d pending %d\n",
                   (pszLabel != NULL) ? pszLabel : "requested", (int)i, nSlot, nSource, nPending);
        }
    }
    if (g_world.pBuySellPanelA != NULL) {
        const UiPanel *pPanel = g_world.pBuySellPanelA;
        printf("pocketufo: strategy panel %s: shop rows %d pos %d/%d font %d style %d scroll %d\n",
               (pszLabel != NULL) ? pszLabel : "requested", (int)pPanel->nLineCount,
               (int)pPanel->nPosX, (int)pPanel->nPosY, (int)pPanel->nFont,
               (int)pPanel->nStyle, (int)pPanel->nUnk3028);
        for (i32 i = 0; i < pPanel->nLineCount; ++i) {
            printf("pocketufo: strategy row %s: shop %d aux %d\n",
                   (pszLabel != NULL) ? pszLabel : "requested", (int)i,
                   UiPanel_GetLineAux((UiPanel *)pPanel, i));
        }
    }
    printf("pocketufo: strategy transfer %s: built %u picker %u workBase %d siteCount %d\n",
           (pszLabel != NULL) ? pszLabel : "requested", (unsigned)g_world.bTransferBuilt,
            (unsigned)g_world.bBaseInfoOverlayOpen, (int)g_world.nBaseInfoWorkBase,
            (int)g_world.nSiteCount);
    fflush(stdout);
}

/* Read-only assignment trace. The native craft-selection path filters on
 * Soldier.bUnk183, so print each saved soldier's assignment before launch. */
static i32 App_ScriptCampaignTraceRoster(const char *pszLabel, i32 nCraftIdx)
{
    i32 nCraftMatch = 0, nCraftOther = 0, nCraftOut = 0, nNoCraft = 0;

    for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
        const Soldier *pSoldier = g_world.apSoldiers[i];
        if (pSoldier == NULL) { continue; }
        if (pSoldier->bUnk183 == (u8)nCraftIdx) {
            ++nCraftMatch;
        } else if (pSoldier->bUnk183 < 254u) {
            ++nCraftOther;
        } else if (pSoldier->bUnk183 == 254u) {
            ++nCraftOut;
        } else {
            ++nNoCraft;
        }
    }
    printf("pocketufo: campaign roster %s: mode %u craftIdx %d selectedBase %d soldiers %d craftMatch %d craftOther %d craftOut %d noCraft %d\n",
           (pszLabel != NULL) ? pszLabel : "requested", (unsigned)g_world.bMapMissionMode,
           (int)nCraftIdx, (int)g_world.nSelectedBase, (int)g_world.nSoldierCount,
           (int)nCraftMatch, (int)nCraftOther, (int)nCraftOut, (int)nNoCraft);
    for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
        const Soldier *pSoldier = g_world.apSoldiers[i];
        if (pSoldier == NULL) { continue; }
        printf("  roster soldier %d id %d base %u craft %u status %u unit %u\n",
               (int)i, (int)pSoldier->nSaveId, (unsigned)pSoldier->nKind,
               (unsigned)pSoldier->bUnk183, (unsigned)pSoldier->bStatus,
               (unsigned)pSoldier->nUnitType);
    }
    fflush(stdout);
    return nCraftMatch;
}

/* Read-only cold-load trace: the clock, its hooks, ship identifiers, the two
 * ship-marker lookup results, the intercept target, and the pending flag-23 line. */
static void App_ScriptCampaignTrace(const char *pszLabel)
{
    const GameTime *pTime = &g_world.geo.time;
    const GeoscapeWorld *pGeo = &g_world.geo.world;
    const PtrArray *pArray = &g_world.strategList;
    const StrategList listView = {
        (StrategItem **)pArray->apItems, pArray->nCount,
        g_world.nNextDisplayId, g_world.nNextSiteId
    };
    const StrategList *pList = &listView;
    const i32 nShipCount = GeoscapeWorld_GetShipCount(pGeo);
    Ship *pCraft = (nShipCount > 0 && pGeo->apCraft != NULL) ? pGeo->apCraft[0] : NULL;
    i32 nMarker = -1, nRawMarker = -1, nFlag23 = -1;
    const StrategItem *pMarker = NULL, *pRawMarker = NULL, *pTarget = NULL;

    if (pCraft != NULL) {
        nMarker = StrategList_FindByShipId(pList, pGeo, 0);
        nRawMarker = StrategList_FindIdxForShip(pList, pGeo, 0);
        if (nMarker >= 0 && nMarker < pList->nCount) { pMarker = pList->apItems[nMarker]; }
        if (nRawMarker >= 0 && nRawMarker < pList->nCount) { pRawMarker = pList->apItems[nRawMarker]; }
        if (pCraft->nStrategIdx >= 0 && pCraft->nStrategIdx < pList->nCount) {
            pTarget = pList->apItems[pCraft->nStrategIdx];
        }
    }
    for (i32 i = 0; i < (i32)g_world.msgLines.nLineCount; ++i) {
        if (g_world.msgLines.abLineFlags[i] == 23) { nFlag23 = i; break; }
    }

    printf("pocketufo: campaign trace %s: ready %d UI app/world %d/%d geo %d screen %d shortfall %d "
           "anim %d/%d clock pause %u total %d lastMin %d tickSec %d epoch %d clockUser %d clockHooks %d/%d/%d/%d "
           "geoUser %d processEvents %d contactHook %d spawnFns %d/%d user %d zoom %d/%d "
           "ships %d/%d selected %d list %d seed %d nextDisplay %d lines %d flag23 %d\n",
           (pszLabel != NULL) ? pszLabel : "requested", g_bWorldReady ? 1 : 0,
           (int)g_nUiScreen, (int)g_world.nUiScreen, (int)g_world.bGeoscapeUi,
           (int)g_world.screen.nGeoscapeScreen, (int)g_world.nShortfall1870C8,
           (int)g_world.cUiAnimProgress, (int)g_world.bUiAnimStarted,
           (unsigned)pTime->bPaused, (int)pTime->nTotalSec, (int)pTime->nLastTickMinSec,
           (int)pGeo->nTickSeconds, (int)pTime->nMonthEpochSec,
           g_world.geoClock.pUser != NULL,
           g_world.geoClock.pfnTickShips != NULL, g_world.geoClock.pfnTickHourly != NULL,
           g_world.geoClock.pfnTickBases != NULL, g_world.geoClock.pfnOpenMonthly != NULL,
           g_world.geoHooks.pUser != NULL, g_world.geoHooks.pfnProcessEvents != NULL,
           g_world.geoHooks.pfnContactMsg != NULL,
           g_world.spawnCtx.pfnSpawnMission != NULL, g_world.spawnCtx.pfnProcessEvents != NULL,
           g_world.spawnCtx.pUser != NULL, (int)g_world.spawnCtx.nZoomMode, (int)g_world.view.nMode,
           (int)nShipCount, (int)g_world.bCraftCount,
           (int)g_world.nSelectedShipIdx, (int)pList->nCount, (int)g_world.nStrategSeed,
           (int)g_world.nNextDisplayId, (int)g_world.msgLines.nLineCount, (int)nFlag23);
    if (pCraft != NULL) {
        printf("  craft0 id %u home %u atBase %u intercept %u targetIdx %d crew %d "
               "ownMarkerLookup %d rawDisplayLookup %d rawDisplayMatch %d\n",
               (unsigned)pCraft->bId, (unsigned)pCraft->bHomeBase, (unsigned)pCraft->bAtBase,
               (unsigned)pCraft->bIntercepting, (int)pCraft->nStrategIdx,
               Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, 0),
               (int)nMarker, (int)nRawMarker,
               pRawMarker != NULL && pRawMarker->nDisplayId == (i32)pCraft->bId);
    } else {
        printf("  craft0 absent\n");
    }
    (void)App_ScriptCampaignTraceRoster(pszLabel, (i32)g_world.nMapCraftIndex);
    if (pMarker != NULL) {
        printf("  own marker %d seed %d type %u active %u display %d at %d/%d timer %d "
               "target %d/%d targetIdx %d phase %d sub %u visible %u\n",
               (int)nMarker, (int)pMarker->nSeed, (unsigned)pMarker->nType,
               (unsigned)pMarker->bActive, (int)pMarker->nDisplayId,
               (int)pMarker->nX, (int)pMarker->nY, (int)pMarker->nTimer,
               (int)pMarker->nTargetX, (int)pMarker->nTargetY, (int)pMarker->nTargetIdx,
               (int)pMarker->nPhase, (unsigned)pMarker->nMissionSub, (unsigned)pMarker->bVisible);
    }
    if (pTarget != NULL) {
        printf("  target record %d seed %d type %u active %u display %d at %d/%d timer %d "
               "target %d/%d targetIdx %d phase %d sub %u visible %u\n",
               (int)pCraft->nStrategIdx, (int)pTarget->nSeed, (unsigned)pTarget->nType,
               (unsigned)pTarget->bActive, (int)pTarget->nDisplayId,
               (int)pTarget->nX, (int)pTarget->nY, (int)pTarget->nTimer,
               (int)pTarget->nTargetX, (int)pTarget->nTargetY, (int)pTarget->nTargetIdx,
               (int)pTarget->nPhase, (unsigned)pTarget->nMissionSub, (unsigned)pTarget->bVisible);
    }
    fflush(stdout);
}

/* SHELL SCAFFOLDING, not original behaviour: the root line list @ 0x158890. The mission launch reads
 * this list, not the map -- Smap_BeginCraftSelect takes the selected line's FLAG as the mission kind
 * (21 urban, 22 alien base, 23 intercept landing) and takes the craft index and the strateg index from
 * that line's colour slots 0 and 1 (0x83994..0x83B54). So the list is where "a mission is waiting for an
 * order" is visible at all. UiLineList's own fields (ui/pufo_linelist.h). */
static void App_ScriptLines(void)
{
    const UiLineList *pList = &g_world.msgLines;

    printf("pocketufo: lines: %d of %d, clock %d s\n",
           (int)pList->nLineCount, PUFO_LINELIST_CAPACITY, (int)g_world.geo.time.nTotalSec);
    for (int i = 0; i < (int)pList->nLineCount; ++i) {
        printf("  [%d] flag %d slots %u %u %u %u\n", i, (int)pList->abLineFlags[i],
               (unsigned)pList->anColor[i][0], (unsigned)pList->anColor[i][1],
               (unsigned)pList->anColor[i][2], (unsigned)pList->anColor[i][3]);
    }
    fflush(stdout);
}

/* Use the same press and release path as a logical mouse click. Never call pfnClick. */
static void App_ScriptTicks(i32 nCount);

typedef struct AppSmapItemPoint {
    i32 nBaseX;
    i32 nY;
    i32 anTryX[3];
    i32 nX;
    u32 nStripHeight;
} AppSmapItemPoint;

/* Read-only inverse of Smap_HitTestStrategSelect @ 0x82C84..0x82CAC for mode 0. */
static bool App_ScriptSmapItemPoint(const SmapView *pView, const PadImage *pStrip,
                                    const StrategItem *pItem, AppSmapItemPoint *pPoint)
{
    if (pPoint == NULL) {
        return false;
    }
    pPoint->nBaseX = -1;
    pPoint->nY = -1;
    pPoint->anTryX[0] = -1;
    pPoint->anTryX[1] = -1;
    pPoint->anTryX[2] = -1;
    pPoint->nX = -1;
    pPoint->nStripHeight = pStrip != NULL ? (u32)pStrip->uHeight : 0u;

    if (pView == NULL || pStrip == NULL || pItem == NULL || pView->nMode != 0
        || pView->nMapW <= 0 || pView->nMapH <= 0) {
        return false;
    }

    pPoint->nBaseX = pItem->nX - pView->nScrollX - pView->nMapW / 2 + 120;
    pPoint->nY = pItem->nY - pView->nScrollY - pView->nMapH / 2 + 160;
    pPoint->anTryX[0] = pPoint->nBaseX;
    pPoint->anTryX[1] = pPoint->nBaseX + pView->nMapW;
    pPoint->anTryX[2] = pPoint->nBaseX - pView->nMapW;
    for (i32 i = 0; i < 3; ++i) {
        if (pPoint->anTryX[i] >= 0 && pPoint->anTryX[i] < 240) {
            pPoint->nX = pPoint->anTryX[i];
            break;
        }
    }
    return pPoint->nX >= 0 && pPoint->nY >= 0 && pPoint->nY < 320
        && pPoint->nY <= 320 - (i32)pPoint->nStripHeight;
}

static bool App_ScriptNormalClickControl(UiControl *pCtl)
{
    i32 nX, nY;

    if (pCtl == NULL || pCtl->bEnabled == 0 || pCtl->pfnClick == NULL
        || pCtl->uWidth == 0 || pCtl->uHeight == 0) {
        return false;
    }
    nX = (i32)pCtl->x + (i32)pCtl->uWidth / 2;
    nY = (i32)pCtl->y + (i32)pCtl->uHeight / 2;
    if (nX < 0 || nX >= 240 || nY < 0 || nY >= 320) {
        return false;
    }
    App_ScriptPointerLogical(nX, nY, true);
    App_OnPointerUp();
    return true;
}

/* Inverse of the mode-0 Smap_HitTestStrategSelect cell transform at 0x82C84..0x82CAC.
 * The item and view members below are the current StrategItem and SmapView fields. */
static bool App_ScriptNormalClickStrategItem(i32 nItemIdx, const StrategItem *pItem,
                                             i32 *pnClickX, i32 *pnClickY)
{
    const SmapView *pView = &g_world.view;
    const PadImage *pStrip = g_world.ui.apPad[GEOUI_PAD_186BBC];
    AppSmapItemPoint point;
    i32 nBaseX, nY;
    i32 anTryX[3];
    i32 nX = -1;
    const i32 nMode = pView->nMode;
    const i32 nScrollX = pView->nScrollX;
    const i32 nScrollY = pView->nScrollY;
    const i32 nMapW = pView->nMapW;
    const i32 nMapH = pView->nMapH;
    const i32 nItemX = pItem != NULL ? pItem->nX : -1;
    const i32 nItemY = pItem != NULL ? pItem->nY : -1;
    const u32 nStripHeight = pStrip != NULL ? (u32)pStrip->uHeight : 0u;

    if (pItem == NULL) {
        printf("pocketufo: normal-order map-click blocked index=%d why=null-item screen=%d geoUi=%u modal=%u paused=%u mode=%d map=%d/%d strip=%u\n",
               (int)nItemIdx, (int)g_world.screen.nGeoscapeScreen,
               (unsigned)g_world.bGeoscapeUi, (unsigned)g_world.sModal.bOpen,
               (unsigned)g_world.geo.time.bPaused, (int)nMode, (int)nMapW,
               (int)nMapH, (unsigned)nStripHeight);
        return false;
    }
    if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
        || g_world.sModal.bOpen != 0 || nMode != 0 || nMapW <= 0 || nMapH <= 0
        || pStrip == NULL) {
        printf("pocketufo: normal-order map-click blocked index=%d why=state screen=%d geoUi=%u modal=%u paused=%u mode=%d map=%d/%d strip=%d stripHeight=%u flag=%d selection=%u tapCooldown=%d\n",
               (int)nItemIdx, (int)g_world.screen.nGeoscapeScreen,
               (unsigned)g_world.bGeoscapeUi, (unsigned)g_world.sModal.bOpen,
               (unsigned)g_world.geo.time.bPaused, (int)nMode, (int)nMapW,
               (int)nMapH, pStrip != NULL, (unsigned)nStripHeight,
               (int)(g_world.msgLines.nLineCount > 0
                         ? g_world.msgLines.abLineFlags[0] : 0),
               (unsigned)g_world.bMapSelectionFlag,
               (int)g_world.frame.g.nTapCooldown);
        return false;
    }
    if (!App_ScriptSmapItemPoint(pView, pStrip, pItem, &point)) {
        nBaseX = point.nBaseX;
        nY = point.nY;
        anTryX[0] = point.anTryX[0];
        anTryX[1] = point.anTryX[1];
        anTryX[2] = point.anTryX[2];
        nX = point.nX;
        printf("pocketufo: normal-order map-click blocked index=%d why=offscreen base=%d/%d candidates=%d/%d/%d point=%d/%d stripHeight=%u mode=%d scroll=%d/%d map=%d/%d item=%d/%d\n",
               (int)nItemIdx, (int)nBaseX, (int)nY, (int)anTryX[0],
               (int)anTryX[1], (int)anTryX[2], (int)nX, (int)nY,
               (unsigned)nStripHeight, (int)nMode, (int)nScrollX, (int)nScrollY,
               (int)nMapW, (int)nMapH, (int)nItemX, (int)nItemY);
        return false;
    }
    nX = point.nX;
    nY = point.nY;
    const u8 bPausedBefore = g_world.geo.time.bPaused;
    const u8 bSelectionBefore = g_world.bMapSelectionFlag;
    const i32 nFlagBefore = g_world.msgLines.nLineCount > 0
        ? (i32)g_world.msgLines.abLineFlags[0] : 0;
    const i32 nCooldownBefore = g_world.frame.g.nTapCooldown;
    App_ScriptPointerLogical(nX, nY, true);
    App_OnPointerUp();
    if (pnClickX != NULL) { *pnClickX = nX; }
    if (pnClickY != NULL) { *pnClickY = nY; }
    printf("pocketufo: normal-order map-click index=%d mode=%d scroll=%d/%d map=%d/%d point=%d/%d item=%d/%d stripHeight=%u pausedBefore=%u pausedAfter=%u flagBefore=%d flagAfter=%d selectionBefore=%u selectionAfter=%u cooldownBefore=%d cooldownAfter=%d\n",
           (int)nItemIdx, (int)nMode, (int)nScrollX, (int)nScrollY,
           (int)nMapW, (int)nMapH, (int)nX, (int)nY, (int)nItemX,
           (int)nItemY, (unsigned)nStripHeight, (unsigned)bPausedBefore,
           (unsigned)g_world.geo.time.bPaused, (int)nFlagBefore,
           (int)(g_world.msgLines.nLineCount > 0
                     ? g_world.msgLines.abLineFlags[0] : 0),
           (unsigned)bSelectionBefore, (unsigned)g_world.bMapSelectionFlag,
           (int)nCooldownBefore, (int)g_world.frame.g.nTapCooldown);
    return true;
}

static i32 App_ScriptNormalLine0Flag(void)
{
    return g_world.msgLines.nLineCount > 0 ? (i32)g_world.msgLines.abLineFlags[0] : 0;
}

static bool App_ScriptNormalOrderFail(const char *pszStage, const char *pszWhy)
{
    printf("pocketufo: normal-order FAIL stage=%s why=%s screen=%d clock=%d pause=%u flag=%d selection=%u selected=%d\n",
           pszStage, pszWhy, (int)g_world.screen.nGeoscapeScreen,
           (int)g_world.geo.time.nTotalSec, (unsigned)g_world.geo.time.bPaused,
           (int)App_ScriptNormalLine0Flag(), (unsigned)g_world.bMapSelectionFlag,
           (int)g_world.nSelectedShipIdx);
    App_ScriptState(pszStage);
    App_ScriptGeoFrame();
    App_ScriptLines();
    App_ScriptStrateg();
    return false;
}

/* Read-only detail for a blocked ready-craft scan. The option builder's aux
 * values remain the authority; this census shows each parallel row and ship. */
static void App_ScriptNormalCraftCensus(const UiPanel *pName, const UiPanel *pStores,
                                        i32 nScanStart, i32 nScanEnd)
{
    if (pName == NULL || pStores == NULL) {
        return;
    }
    printf("pocketufo: normal-order row-census nameCount=%d storesCount=%d craftCount=%u soldierCount=%d scan=%d/%d scroll=%d visible=%d font=%d animProgress=%d animStarted=%u\n",
           (int)pName->nLineCount, (int)pStores->nLineCount,
           (unsigned)g_world.bCraftCount, (int)g_world.nSoldierCount, (int)nScanStart,
           (int)nScanEnd, (int)pName->nUnk3028, (int)pName->nStyle,
           (int)pName->nFont, (int)g_world.cUiAnimProgress,
           (unsigned)g_world.bUiAnimStarted);
    for (i32 nRow = 0; nRow < pName->nLineCount && nRow < PUFO_PANEL_LINES; ++nRow) {
        const i32 nCandidate = pName->anLineAux[nRow];
        const i32 nReadyAux = nRow < pStores->nLineCount
            ? pStores->anLineAux[nRow] : -999999;
        const bool bVisible = nRow >= pName->nUnk3028
            && nRow < pName->nUnk3028 + pName->nStyle;
        const Ship *pShip = (nCandidate >= 0 && nCandidate < (i32)g_world.bCraftCount)
            ? g_world.apCraft[nCandidate] : NULL;
        const i32 nCrew = pShip != NULL
            ? Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, nCandidate)
            : 0;
        const i32 nFieldsReady = pShip != NULL && pShip->bAtBase != 0
            && pShip->uRangeRemain == pShip->uRangeMax
            && pShip->nFuel == (i16)pShip->uFuelMax;

        printf("pocketufo: normal-order row-census row=%d visible=%d nameAux=%d readyAux=%d craftIndex=%d id=%d type=%d home=%d atBase=%d range=%d/%d fuel=%d/%d crewSlots=%d crew=%d fieldsReady=%d\n",
               (int)nRow, bVisible ? 1 : 0, (int)nCandidate, (int)nReadyAux,
               (int)nCandidate, pShip != NULL ? (int)pShip->bId : -1,
               pShip != NULL ? (int)pShip->bCraftType : -1,
               pShip != NULL ? (int)pShip->bHomeBase : -1,
               pShip != NULL ? (int)pShip->bAtBase : -1,
               pShip != NULL ? (int)pShip->uRangeRemain : -1,
               pShip != NULL ? (int)pShip->uRangeMax : -1,
               pShip != NULL ? (int)pShip->nFuel : -1,
               pShip != NULL ? (int)pShip->uFuelMax : -1,
               pShip != NULL ? (int)pShip->bCrewSlots : -1, (int)nCrew,
               nFieldsReady ? 1 : 0);
    }
    fflush(stdout);
}

/* One scenario command for the natural New Game route. It reads state, then
 * advances only through real controls and real pointer input. It ends after the
 * normal intercept order; it does not wait for arrival or make a mission. */
static bool App_ScriptNormalInterceptOrder(void)
{
    enum { kFirstWaitBatch = 120, kMaxWaitPulses = 120 };
    i32 nPulses = 0;
    i32 nUfoIdx = -1;
    i32 nTargetSeed = 0;
    i32 nBaseIdx = -1;
    i32 nShipIdx = -1;
    i32 nReadyRow = -1;
    i32 nCrewCount = 0;
    i32 nOptionsAnimFrames = 0;
    i32 nSelectedBefore = g_world.nSelectedShipIdx;
    i32 nClockStart;
    i32 nOptionsClockStart;
    const u8 bSelectionBefore = g_world.bMapSelectionFlag;
    StrategItem *pUfo;
    Ship *pShip;

    if (!g_bWorldReady || !g_assets.apObjects || World_bTactPointerLive(&g_world)
        || g_world.nBaseCount <= 0 || g_world.apBases[0] == NULL) {
        return App_ScriptNormalOrderFail("start", "need_fresh_new_game_and_base");
    }

    /* New Game can leave screens 2 then 1 open. Use their live close control. */
    App_DrawFrame();
    if (g_world.sModal.bOpen != 0) {
        return App_ScriptNormalOrderFail("startup_close", "unexpected_modal");
    }
    for (i32 i = 0; i < 2 && (g_world.screen.nGeoscapeScreen == 2
                               || g_world.screen.nGeoscapeScreen == 1); ++i) {
        if (g_world.sModal.bOpen != 0) {
            return App_ScriptNormalOrderFail("startup_close", "unexpected_modal");
        }
        if (!App_ScriptNormalClickControl(g_world.ui.apCtl[GEOUI_CTL_186B00])) {
            return App_ScriptNormalOrderFail("startup_close", "visible_close_not_ready");
        }
        App_DrawFrame();
    }
    if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
        || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
        || App_ScriptNormalLine0Flag() != 0 || g_world.view.nMode != 0) {
        return App_ScriptNormalOrderFail("startup_close", "not_on_clear_running_map");
    }

    /* Root control 7 is speed button 5: the verified 3600-second interval. */
    if (!App_ScriptNormalClickControl(g_world.ui.apRootCtl[7])
        || g_world.geo.world.nTickSeconds != 3600) {
        return App_ScriptNormalOrderFail("speed", "one_hour_button_not_live");
    }
    nClockStart = g_world.geo.time.nTotalSec;
    printf("pocketufo: normal-order speed root=7 tickSec=%d clockStart=%d\n",
           (int)g_world.geo.world.nTickSeconds, (int)nClockStart);

    /* This is the only time sweep. Every pulse uses App_ScriptTicks -> C353,
     * with a paint. Unknown messages, pauses, screens, or modals stop the run. */
    while (nPulses < kMaxWaitPulses && App_ScriptNormalLine0Flag() != 10) {
        const i32 nFlag = App_ScriptNormalLine0Flag();
        if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
            || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
            || (nFlag != 0 && nFlag != 10)) {
            return App_ScriptNormalOrderFail("natural_contact", "unknown_screen_modal_pause_or_message");
        }
        App_ScriptTicks(1);
        ++nPulses;
        if (nPulses % kFirstWaitBatch == 0 && App_ScriptNormalLine0Flag() != 10) {
            printf("pocketufo: normal-order wait pulses=%d seconds=%d flag=%d pause=%u\n",
                   (int)nPulses, (int)g_world.geo.time.nTotalSec,
                   (int)App_ScriptNormalLine0Flag(), (unsigned)g_world.geo.time.bPaused);
        }
    }
    if (App_ScriptNormalLine0Flag() != 10 || g_world.geo.time.bPaused == 0
        || g_world.msgLines.nLineCount <= 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bGeoscapeUi == 0 || g_world.sModal.bOpen != 0) {
        return App_ScriptNormalOrderFail("natural_contact", "bounded_wait_no_flag10");
    }
    nUfoIdx = (i32)g_world.msgLines.anColor[0][0];
    if (nUfoIdx < 0 || nUfoIdx >= g_world.strategList.nCount
        || g_world.strategList.apItems[nUfoIdx] == NULL) {
        return App_ScriptNormalOrderFail("natural_contact", "flag10_has_bad_target_index");
    }
    pUfo = (StrategItem *)g_world.strategList.apItems[nUfoIdx];
    if (pUfo->nType != 1 || pUfo->bActive == 0 || pUfo->bVisible == 0) {
        return App_ScriptNormalOrderFail("natural_contact", "flag10_target_not_visible_active_ufo");
    }
    nTargetSeed = pUfo->nSeed;
    printf("pocketufo: normal-order contact source=c353 pulses=%d seconds=%d clockStart=%d tickSec=%d paused=%u lineFlag=%d target=%d seed=%d targetType=%d targetActive=%u targetVisible=%u\n",
           (int)nPulses, (int)g_world.geo.time.nTotalSec, (int)nClockStart,
           (int)g_world.geo.world.nTickSeconds,
           (unsigned)g_world.geo.time.bPaused, (int)App_ScriptNormalLine0Flag(),
           (int)nUfoIdx, (int)nTargetSeed, (int)pUfo->nType,
           (unsigned)pUfo->bActive, (unsigned)pUfo->bVisible);

    /* geo[2] is pGeoscapeCtlA. Its live pointer route clears the contact. */
    if (!App_ScriptNormalClickControl(g_world.ui.apCtl[GEOUI_CTL_186BB4])) {
        return App_ScriptNormalOrderFail("contact_clear", "geo2_not_live");
    }
    App_DrawFrame();
    if (App_ScriptNormalLine0Flag() != 0 || g_world.geo.time.bPaused != 0
        || g_world.bMapSelectionFlag != 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bGeoscapeUi == 0 || g_world.sModal.bOpen != 0
        || g_world.view.nMode != 0) {
        return App_ScriptNormalOrderFail("contact_clear", "clear_click_did_not_resume_map");
    }
    printf("pocketufo: normal-order cleared flag=%d paused=%u selection=%u\n",
           (int)App_ScriptNormalLine0Flag(), (unsigned)g_world.geo.time.bPaused,
           (unsigned)g_world.bMapSelectionFlag);

    /* Find the first base's inactive marker by its current site id. */
    for (i32 i = 0; i < g_world.strategList.nCount; ++i) {
        StrategItem *p = (StrategItem *)g_world.strategList.apItems[i];
        if (p != NULL && p->nType == 0 && p->bActive == 0
            && p->nSiteId == (i32)(i8)g_world.apBases[0]->bUnk4) {
            nBaseIdx = i;
            break;
        }
    }
    if (nBaseIdx < 0) {
        return App_ScriptNormalOrderFail("base_click", "first_base_marker_missing");
    }
    if (!App_ScriptNormalClickStrategItem(nBaseIdx,
            (StrategItem *)g_world.strategList.apItems[nBaseIdx], NULL, NULL)) {
        return App_ScriptNormalOrderFail("base_click", "first_base_offscreen_or_wrong_map_mode");
    }
    App_DrawFrame();
    if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
        || g_world.sModal.bOpen != 0 || g_world.options.apPanel[0] == NULL
        || g_world.options.apPanel[1] == NULL) {
        return App_ScriptNormalOrderFail("base_click", "screen18_local_craft_list_not_open");
    }
    printf("pocketufo: normal-order screen18 screen=%d options=%u namePanel=%d storesPanel=%d\n",
           (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bOptionsScreenOpen,
           g_world.options.apPanel[0] != NULL, g_world.options.apPanel[1] != NULL);

    if (g_world.geo.time.bPaused == 0 || g_world.bUiAnimStarted == 0) {
        return App_ScriptNormalOrderFail("craft_row", "options_animation_not_started_or_not_paused");
    }
    nOptionsClockStart = g_world.geo.time.nTotalSec;
    while (g_world.cUiAnimProgress < 100 && nOptionsAnimFrames < 101) {
        if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
            || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused == 0
            || g_world.options.apPanel[0] == NULL || g_world.options.apPanel[1] == NULL) {
            return App_ScriptNormalOrderFail("craft_row", "options_animation_state_changed");
        }
        /* C350 is the geoscape stub here; run C354, C352 and paint, never C353. */
        App_GameTickOnce(NULL);
        App_TickAnimProgress();
        App_TickAnim();
        App_DrawFrame();
        ++nOptionsAnimFrames;
    }
    if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
        || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused == 0
        || g_world.options.apPanel[0] == NULL || g_world.options.apPanel[1] == NULL
        || g_world.cUiAnimProgress < 100 || g_world.geo.time.nTotalSec != nOptionsClockStart) {
        return App_ScriptNormalOrderFail("craft_row", "options_animation_did_not_finish_cleanly");
    }
    printf("pocketufo: normal-order options-animation frames=%d progress=%d started=%u clockBefore=%d clockAfter=%d paused=%u\n",
           (int)nOptionsAnimFrames, (int)g_world.cUiAnimProgress,
           (unsigned)g_world.bUiAnimStarted, (int)nOptionsClockStart,
           (int)g_world.geo.time.nTotalSec, (unsigned)g_world.geo.time.bPaused);
    printf("pocketufo: normal-order base index=%d site=%d\n", (int)nBaseIdx,
           (int)g_world.apBases[0]->bUnk4);

    /* Pick a row whose real name aux points at a crew-bearing craft. The stores
     * aux is the original ready gate: zero means full range, full fuel, at base. */
    {
        const UiPanel *pName = g_world.options.apPanel[0];
        const UiPanel *pStores = g_world.options.apPanel[1];
        const i32 nRowStart = pName->nUnk3028;
        i32 nRowEnd = pName->nLineCount;
        if (nRowStart < 0 || nRowStart >= PUFO_PANEL_LINES || pName->nFont <= 0) {
            return App_ScriptNormalOrderFail("craft_row", "name_rows_not_live");
        }
        if (nRowEnd > nRowStart + pName->nStyle) {
            nRowEnd = nRowStart + pName->nStyle;
        }
        if (nRowEnd > PUFO_PANEL_LINES) {
            nRowEnd = PUFO_PANEL_LINES;
        }
        if (nRowEnd > pStores->nLineCount) {
            nRowEnd = pStores->nLineCount;
        }
        for (i32 nRow = nRowStart; nRow < nRowEnd; ++nRow) {
            const i32 nCandidate = pName->anLineAux[nRow];
            const Ship *pCandidate;
            i32 nCrew;
            if (nCandidate < 0 || nCandidate >= (i32)g_world.bCraftCount
                || pStores->anLineAux[nRow] != 0) {
                continue;
            }
            pCandidate = g_world.apCraft[nCandidate];
            if (pCandidate == NULL || pCandidate->bAtBase == 0
                || pCandidate->uRangeRemain != pCandidate->uRangeMax
                || pCandidate->nFuel != (i16)pCandidate->uFuelMax) {
                continue;
            }
            nCrew = Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, nCandidate);
            if (nCrew <= 0) {
                continue;
            }
            nShipIdx = nCandidate;
            nReadyRow = nRow;
            nCrewCount = nCrew;
            break;
        }
        if (nShipIdx < 0) {
            App_ScriptNormalCraftCensus(pName, pStores, nRowStart, nRowEnd);
            return App_ScriptNormalOrderFail("craft_row", "no_ready_crew_bearing_craft");
        }
        pShip = g_world.apCraft[nShipIdx];
        {
            const i32 nX = pName->nRectX + 45; /* observed name column x=50 */
            const i32 nY = pName->nPosY + (nReadyRow - pName->nUnk3028) * pName->nFont
                + pName->nFont / 2;
            if (nX <= pName->nRectX || nX >= pName->nRectX + pName->nViewW
                || nY <= pName->nRectY || nY >= pName->nRectY + pName->nViewH
                || nX >= 240 || nY >= 320) {
                return App_ScriptNormalOrderFail("craft_row", "ready_row_not_visible");
            }
            printf("pocketufo: normal-order row row=%d craft=%d rowReadyAux=%d crew=%d crewSlots=%u atBase=%u range=%u/%u fuel=%d/%u\n",
                   (int)nReadyRow, (int)nShipIdx, (int)pStores->anLineAux[nReadyRow],
                   (int)nCrewCount, (unsigned)pShip->bCrewSlots, (unsigned)pShip->bAtBase,
                   (unsigned)pShip->uRangeRemain, (unsigned)pShip->uRangeMax,
                   (int)pShip->nFuel, (unsigned)pShip->uFuelMax);
            App_ScriptPointerLogical(nX, nY, true);
            App_OnPointerUp();
        }
    }

    if (g_world.nSelectedShipIdx != nShipIdx || g_world.bMapSelectionFlag == 0
        || App_ScriptNormalLine0Flag() != 27 || g_world.msgLines.anColor[0][0] != (u32)nShipIdx
        || g_world.sModal.bOpen != 0) {
        return App_ScriptNormalOrderFail("craft_select", "real_row_did_not_arm_flag27");
    }
    printf("pocketufo: normal-order selected before=%d after=%d craft=%d flag27=%d slot0=%u selectionBefore=%u selection=%u\n",
           (int)nSelectedBefore, (int)g_world.nSelectedShipIdx, (int)nShipIdx,
           (int)App_ScriptNormalLine0Flag(), (unsigned)g_world.msgLines.anColor[0][0],
           (unsigned)bSelectionBefore, (unsigned)g_world.bMapSelectionFlag);

    /* This draw takes the ordinary flag-27 close path. It has no C353 clock pulse. */
    App_DrawFrame();
    if (g_world.screen.nGeoscapeScreen != 0 || g_world.bOptionsScreenOpen != 0
        || g_world.nSelectedShipIdx != nShipIdx || g_world.bMapSelectionFlag == 0
        || g_world.sModal.bOpen != 0 || g_world.view.nMode != 0) {
        return App_ScriptNormalOrderFail("craft_select", "screen18_did_not_close_on_draw");
    }
    if (g_world.strategList.apItems[nUfoIdx] == NULL) {
        return App_ScriptNormalOrderFail("ufo_click", "contact_item_gone");
    }
    pUfo = (StrategItem *)g_world.strategList.apItems[nUfoIdx];
    if (pUfo->nType != 1 || pUfo->bActive == 0 || pUfo->bVisible == 0
        || pUfo->nSeed != nTargetSeed) {
        return App_ScriptNormalOrderFail("ufo_click", "contact_item_changed");
    }
    if (!App_ScriptNormalClickStrategItem(nUfoIdx, pUfo, NULL, NULL)) {
        return App_ScriptNormalOrderFail("ufo_click", "contact_offscreen_or_wrong_map_mode");
    }
    App_DrawFrame();
    if (App_ScriptNormalLine0Flag() != 10 || g_world.bMapSelectionFlag == 0
        || g_world.msgLines.anColor[0][0] != (u32)nUfoIdx || g_world.sModal.bOpen != 0) {
        return App_ScriptNormalOrderFail("ufo_select", "real_ufo_click_did_not_reach_flag10");
    }
    printf("pocketufo: normal-order target index=%d seed=%d lineFlag=%d slot0=%u selection=%u\n",
           (int)nUfoIdx, (int)nTargetSeed, (int)App_ScriptNormalLine0Flag(),
           (unsigned)g_world.msgLines.anColor[0][0], (unsigned)g_world.bMapSelectionFlag);

    pShip = g_world.apCraft[nShipIdx];
    if (pShip == NULL || pShip->bAtBase == 0 || pShip->bIntercepting != 0) {
        return App_ScriptNormalOrderFail("intercept_confirm", "selected_craft_not_ready_at_base");
    }
    {
        UiControl *pConfirm = g_world.ui.apCtl[GEOUI_CTL_186BA4];
        const i32 nConfirmX = pConfirm != NULL
            ? (i32)pConfirm->x + (i32)pConfirm->uWidth / 2 : -1;
        const i32 nConfirmY = pConfirm != NULL
            ? (i32)pConfirm->y + (i32)pConfirm->uHeight / 2 : -1;
        const u8 bAtBaseBefore = pShip->bAtBase;
        const u8 bInterceptBefore = pShip->bIntercepting;
        const i32 nShipTargetBefore = pShip->nStrategIdx;
        const i32 nLineFlagBefore = App_ScriptNormalLine0Flag();
        if (!App_ScriptNormalClickControl(pConfirm)) {
            return App_ScriptNormalOrderFail("intercept_confirm", "geo1_confirm_not_live");
        }
        pShip = g_world.apCraft[nShipIdx];
        pUfo = (nUfoIdx < g_world.strategList.nCount)
            ? (StrategItem *)g_world.strategList.apItems[nUfoIdx] : NULL;
        printf("pocketufo: normal-order launch confirm=geo1 confirmCenter=%d/%d beforeSelected=%d selected=%d craft=%d target=%d seed=%d "
               "atBaseBefore=%u atBaseAfter=%u interceptBefore=%u interceptAfter=%u "
               "shipTargetBefore=%d shipTargetAfter=%d targetStill=%d clock=%d pause=%u lineFlagBefore=%d lineFlagAfter=%d selection=%u\n",
               (int)nConfirmX, (int)nConfirmY, (int)nSelectedBefore,
               (int)g_world.nSelectedShipIdx, (int)nShipIdx,
               (int)nUfoIdx, (int)nTargetSeed, (unsigned)bAtBaseBefore,
               pShip != NULL ? (unsigned)pShip->bAtBase : 255u,
               (unsigned)bInterceptBefore, pShip != NULL ? (unsigned)pShip->bIntercepting : 255u,
               (int)nShipTargetBefore, pShip != NULL ? (int)pShip->nStrategIdx : -1,
               pUfo != NULL && pUfo->nSeed == nTargetSeed,
               (int)g_world.geo.time.nTotalSec, (unsigned)g_world.geo.time.bPaused,
               (int)nLineFlagBefore, (int)App_ScriptNormalLine0Flag(),
               (unsigned)g_world.bMapSelectionFlag);
        if (pShip == NULL || pShip->bAtBase != 0 || pShip->bIntercepting == 0
            || pShip->nStrategIdx != nUfoIdx || pUfo == NULL || pUfo->nSeed != nTargetSeed) {
            return App_ScriptNormalOrderFail("intercept_confirm", "real_confirm_did_not_launch_order");
        }
    }

    printf("pocketufo: normal-order PASS stage=real_order pulses=%d clock=%d\n",
           (int)nPulses, (int)g_world.geo.time.nTotalSec);
    fflush(stdout);
    return true;
}

/* Radar_MountAmmo's read-only mount mapping, used only to choose a loaded air craft. */
static i32 App_ScriptNormalAirAmmo(const Ship *pShip, i32 nMount)
{
    if (pShip == NULL) {
        return PUFO_RADAR_AMMO_NONE;
    }
    switch (nMount) {
        case 25: return (i32)pShip->bAmmo25;
        case 26: return (i32)pShip->bAmmo26;
        case 27: return (i32)pShip->bAmmo27;
        case 28: return PUFO_RADAR_AMMO_UNLIMITED;
        case 29: return (i32)pShip->bAmmo29;
        case 30: return (i32)pShip->bAmmo30;
        default: return PUFO_RADAR_AMMO_NONE;
    }
}

static i32 App_ScriptNormalAirCrewCount(i32 nCraftIdx)
{
    i32 nCrew = 0;
    for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
        const Soldier *pSoldier = g_world.apSoldiers[i];
        if (pSoldier != NULL && (i32)pSoldier->bUnk183 == nCraftIdx) {
            ++nCrew;
        }
    }
    return nCrew;
}

/* Read-only owner snapshot around the air-arrival pulse. */
static void App_ScriptNormalAirObserve(const char *pszStage, i32 nPulses,
                                       i32 nShipIdx, i32 nTargetIdx, i32 nTargetSeed)
{
    const Ship *pShip = (nShipIdx >= 0 && nShipIdx < (i32)g_world.bCraftCount)
        ? g_world.apCraft[nShipIdx] : NULL;
    const StrategItem *pOwn = NULL;
    const StrategItem *pTarget = (nTargetIdx >= 0 && nTargetIdx < g_world.strategList.nCount
                                  && g_world.strategList.apItems != NULL)
        ? (const StrategItem *)g_world.strategList.apItems[nTargetIdx] : NULL;
    i32 nOwnIdx = -1;

    if (pShip != NULL && g_world.strategList.apItems != NULL) {
        for (i32 i = 0; i < g_world.strategList.nCount; ++i) {
            const StrategItem *p = (const StrategItem *)g_world.strategList.apItems[i];
            if (p != NULL && p->nType == 1 && p->bActive == 0
                && p->nDisplayId == (i32)pShip->bId) {
                pOwn = p;
                nOwnIdx = i;
                break;
            }
        }
    }

    printf("pocketufo: normal-air state stage=%s pulses=%d sec=%d tickSec=%d paused=%u screen=%d geoUi=%u modal=%u radarActive=%u lines=%u flag=%d slot0=%u slot1=%u selection=%u selected=%d tapCooldown=%d radarPulse=%d tact=%u tactLive=%u\n",
           pszStage, (int)nPulses, (int)g_world.geo.time.nTotalSec,
           (int)g_world.geo.world.nTickSeconds, (unsigned)g_world.geo.time.bPaused,
           (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bGeoscapeUi,
           (unsigned)g_world.sModal.bOpen, (unsigned)g_world.bRadarScreenActive,
           (unsigned)g_world.msgLines.nLineCount, (int)App_ScriptNormalLine0Flag(),
           (unsigned)g_world.msgLines.anColor[0][0],
           (unsigned)g_world.msgLines.anColor[0][1],
           (unsigned)g_world.bMapSelectionFlag, (int)g_world.nSelectedShipIdx,
           (int)g_world.frame.g.nTapCooldown, (int)g_world.nRadarPulseTimer,
           (unsigned)g_world.bTactFlag, g_world.bTactLive ? 1u : 0u);

    if (pShip == NULL) {
        printf("pocketufo: normal-air craft stage=%s index=%d absent\n",
               pszStage, (int)nShipIdx);
    } else {
        printf("pocketufo: normal-air craft stage=%s index=%d id=%u type=%u home=%u atBase=%u intercept=%u target=%d weaponsHot=%u canDogfight=%u weaponL=%d ammoL=%d weaponR=%d ammoR=%d range=%u/%u fuel=%d/%u crew=%d\n",
               pszStage, (int)nShipIdx, (unsigned)pShip->bId,
               (unsigned)pShip->bCraftType, (unsigned)pShip->bHomeBase,
               (unsigned)pShip->bAtBase, (unsigned)pShip->bIntercepting,
               (int)pShip->nStrategIdx, (unsigned)pShip->bWeaponsHot,
               (unsigned)pShip->bCanDogfight, (int)pShip->bWeaponL,
               App_ScriptNormalAirAmmo(pShip, pShip->bWeaponL),
               (int)pShip->bWeaponR,
               App_ScriptNormalAirAmmo(pShip, pShip->bWeaponR),
               (unsigned)pShip->uRangeRemain, (unsigned)pShip->uRangeMax,
               (int)pShip->nFuel, (unsigned)pShip->uFuelMax,
               (int)App_ScriptNormalAirCrewCount(nShipIdx));
    }

    if (pOwn == NULL) {
        printf("pocketufo: normal-air own-marker stage=%s craft=%d index=%d absent\n",
               pszStage, (int)nShipIdx, (int)nOwnIdx);
    } else {
        printf("pocketufo: normal-air own-marker stage=%s craft=%d index=%d type=%u active=%u visible=%u display=%d at=%d/%d start=%d/%d dest=%d/%d target=%d/%d timer=%d phase=%d sub=%u travel=%u epoch=%d speed=%.3f\n",
               pszStage, (int)nShipIdx, (int)nOwnIdx, (unsigned)pOwn->nType,
               (unsigned)pOwn->bActive, (unsigned)pOwn->bVisible,
               (int)pOwn->nDisplayId, (int)pOwn->nX, (int)pOwn->nY,
               (int)pOwn->nStartX, (int)pOwn->nStartY,
               (int)pOwn->nDestX, (int)pOwn->nDestY,
               (int)pOwn->nTargetX, (int)pOwn->nTargetY,
               (int)pOwn->nTimer, (int)pOwn->nPhase,
               (unsigned)pOwn->nMissionSub, (unsigned)pOwn->uTravelMin,
               (int)pOwn->nTravelEpochSec, pOwn->dSpeed);
    }

    if (pTarget == NULL) {
        printf("pocketufo: normal-air target expected=%d seed=%d absent\n",
               (int)nTargetIdx, (int)nTargetSeed);
    } else {
        printf("pocketufo: normal-air target expected=%d actualSeed=%d type=%u active=%u visible=%u display=%d at=%d/%d start=%d/%d dest=%d/%d target=%d/%d timer=%d phase=%d sub=%u travel=%u epoch=%d targetIdx=%d done=%u hp=%d\n",
               (int)nTargetIdx, (int)pTarget->nSeed, (unsigned)pTarget->nType,
               (unsigned)pTarget->bActive, (unsigned)pTarget->bVisible,
               (int)pTarget->nDisplayId, (int)pTarget->nX, (int)pTarget->nY,
               (int)pTarget->nStartX, (int)pTarget->nStartY,
               (int)pTarget->nDestX, (int)pTarget->nDestY,
               (int)pTarget->nTargetX, (int)pTarget->nTargetY,
               (int)pTarget->nTimer, (int)pTarget->nPhase,
               (unsigned)pTarget->nMissionSub, (unsigned)pTarget->uTravelMin,
               (int)pTarget->nTravelEpochSec, (int)pTarget->nTargetIdx,
               (unsigned)pTarget->bDone, (int)pTarget->nHp);
    }
    fflush(stdout);
}

static bool App_ScriptNormalAirFail(const char *pszStage, const char *pszWhy,
                                    i32 nPulses, i32 nShipIdx, i32 nTargetIdx,
                                    i32 nTargetSeed)
{
    printf("pocketufo: normal-air FAIL stage=%s why=%s\n", pszStage, pszWhy);
    App_ScriptNormalAirObserve(pszStage, nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    App_ScriptState(pszStage);
    App_ScriptGeoFrame();
    App_ScriptLines();
    App_ScriptStrateg();
    return false;
}

static bool App_ScriptNormalAirHasLoadedWeapon(const Ship *pShip)
{
    const i32 nAmmoL = App_ScriptNormalAirAmmo(pShip, pShip != NULL ? pShip->bWeaponL : 0);
    const i32 nAmmoR = App_ScriptNormalAirAmmo(pShip, pShip != NULL ? pShip->bWeaponR : 0);
    return (nAmmoL == PUFO_RADAR_AMMO_UNLIMITED || nAmmoL > 0)
        || (nAmmoR == PUFO_RADAR_AMMO_UNLIMITED || nAmmoR > 0);
}

/* Read-only rows show the real Options readiness aux and the actual craft fields. */
static void App_ScriptNormalAirRowCensus(const UiPanel *pName, const UiPanel *pStores,
                                         i32 nRowStart, i32 nRowEnd)
{
    if (pName == NULL || pStores == NULL) {
        return;
    }
    printf("pocketufo: normal-air row-census nameCount=%d storesCount=%d craftCount=%u scan=%d/%d scroll=%d visible=%d font=%d animProgress=%d animStarted=%u\n",
           (int)pName->nLineCount, (int)pStores->nLineCount,
           (unsigned)g_world.bCraftCount, (int)nRowStart, (int)nRowEnd,
           (int)pName->nUnk3028, (int)pName->nStyle, (int)pName->nFont,
           (int)g_world.cUiAnimProgress, (unsigned)g_world.bUiAnimStarted);
    for (i32 nRow = 0; nRow < pName->nLineCount && nRow < PUFO_PANEL_LINES; ++nRow) {
        const i32 nCraftIdx = pName->anLineAux[nRow];
        const i32 nReadyAux = nRow < pStores->nLineCount
            ? pStores->anLineAux[nRow] : -999999;
        const bool bVisible = nRow >= pName->nUnk3028
            && nRow < pName->nUnk3028 + pName->nStyle;
        const Ship *pShip = nCraftIdx >= 0 && nCraftIdx < (i32)g_world.bCraftCount
            ? g_world.apCraft[nCraftIdx] : NULL;
        const i32 nAmmoL = App_ScriptNormalAirAmmo(pShip, pShip != NULL ? pShip->bWeaponL : 0);
        const i32 nAmmoR = App_ScriptNormalAirAmmo(pShip, pShip != NULL ? pShip->bWeaponR : 0);
        printf("pocketufo: normal-air row row=%d visible=%d nameAux=%d readyAux=%d craft=%d craftId=%d type=%d canDogfight=%d weaponL=%d ammoL=%d weaponR=%d ammoR=%d atBase=%d range=%d/%d fuel=%d/%d loadedWeapon=%d crew=%d\n",
               (int)nRow, bVisible ? 1 : 0, (int)nCraftIdx, (int)nReadyAux,
               (int)nCraftIdx, pShip != NULL ? (int)pShip->bId : -1,
               pShip != NULL ? (int)pShip->bCraftType : -1,
               pShip != NULL ? (int)pShip->bCanDogfight : -1,
               pShip != NULL ? (int)pShip->bWeaponL : -1, (int)nAmmoL,
               pShip != NULL ? (int)pShip->bWeaponR : -1, (int)nAmmoR,
               pShip != NULL ? (int)pShip->bAtBase : -1,
               pShip != NULL ? (int)pShip->uRangeRemain : -1,
               pShip != NULL ? (int)pShip->uRangeMax : -1,
               pShip != NULL ? (int)pShip->nFuel : -1,
               pShip != NULL ? (int)pShip->uFuelMax : -1,
               App_ScriptNormalAirHasLoadedWeapon(pShip) ? 1 : 0,
               pShip != NULL ? Craft_CountSoldiersAboard(g_world.apSoldiers,
                                                         g_world.nSoldierCount,
                                                         nCraftIdx) : 0);
    }
    fflush(stdout);
}

/* Follow a natural New Game flying contact through a real interceptor order to RadarScreen_SetActive. */
static bool App_ScriptNormalAirIntercept(void)
{
    enum { kMaxContactPulses = 2016, kMaxArrivalPulses = 600 };
    i32 nPulses = 0;
    i32 nArrivalPulses = 0;
    i32 nTargetIdx = -1;
    i32 nTargetSeed = 0;
    i32 nBaseIdx = -1;
    i32 nShipIdx = -1;
    i32 nReadyRow = -1;
    i32 nOptionsFrames = 0;
    i32 nClockStart;
    i32 nFlightClockStart;
    u8 bShipId = 0;
    u8 bCraftType = 0;
    u8 bTargetVisibleLast = 0;
    i32 nCraftTargetBefore;
    StrategItem *pTarget;
    Ship *pShip;

    if (!g_bWorldReady || !g_assets.apObjects || World_bTactPointerLive(&g_world)
        || g_world.nBaseCount <= 0 || g_world.apBases[0] == NULL) {
        return App_ScriptNormalAirFail("start", "need_fresh_new_game_and_base",
                                       0, -1, -1, 0);
    }
    if (g_world.bCraftCount == 0
        || g_world.strategList.apItems == NULL || g_world.strategList.nCount <= 0) {
        return App_ScriptNormalAirFail("start", "craft_or_strateg_list_not_ready",
                                       0, -1, -1, 0);
    }

    App_DrawFrame();
    if (g_world.sModal.bOpen != 0) {
        return App_ScriptNormalAirFail("startup_close", "unexpected_modal", 0, -1, -1, 0);
    }
    for (i32 i = 0; i < 2 && (g_world.screen.nGeoscapeScreen == 2
                               || g_world.screen.nGeoscapeScreen == 1); ++i) {
        if (g_world.sModal.bOpen != 0
            || !App_ScriptNormalClickControl(g_world.ui.apCtl[GEOUI_CTL_186B00])) {
            return App_ScriptNormalAirFail("startup_close", "visible_close_not_ready",
                                           0, -1, -1, 0);
        }
        App_DrawFrame();
    }
    if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
        || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
        || App_ScriptNormalLine0Flag() != 0 || g_world.view.nMode != 0) {
        return App_ScriptNormalAirFail("startup_close", "not_on_clear_running_map",
                                       0, -1, -1, 0);
    }

    if (!App_ScriptNormalClickControl(g_world.ui.apRootCtl[4])
        || g_world.geo.world.nTickSeconds != 300) {
        return App_ScriptNormalAirFail("contact_speed", "five_minute_control_not_live",
                                       0, -1, -1, 0);
    }
    nClockStart = g_world.geo.time.nTotalSec;
    printf("pocketufo: normal-air speed root=4 tickSec=%d clockStart=%d\n",
           (int)g_world.geo.world.nTickSeconds, (int)nClockStart);

    while (nPulses < kMaxContactPulses && App_ScriptNormalLine0Flag() != 10) {
        const i32 nFlag = App_ScriptNormalLine0Flag();
        if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
            || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
            || (nFlag != 0 && nFlag != 10)) {
            return App_ScriptNormalAirFail("natural_contact", "unknown_screen_modal_pause_or_message",
                                           nPulses, -1, -1, 0);
        }
        App_ScriptTicks(1);
        ++nPulses;
    }
    if (App_ScriptNormalLine0Flag() != 10 || g_world.geo.time.bPaused == 0
        || g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
        || g_world.sModal.bOpen != 0 || g_world.msgLines.nLineCount <= 0) {
        return App_ScriptNormalAirFail("natural_contact", "bounded_wait_no_flag10",
                                       nPulses, -1, -1, 0);
    }
    nTargetIdx = (i32)g_world.msgLines.anColor[0][0];
    if (nTargetIdx < 0 || nTargetIdx >= g_world.strategList.nCount
        || g_world.strategList.apItems == NULL || g_world.strategList.apItems[nTargetIdx] == NULL) {
        return App_ScriptNormalAirFail("natural_contact", "flag10_has_bad_target_index",
                                       nPulses, -1, nTargetIdx, 0);
    }
    pTarget = (StrategItem *)g_world.strategList.apItems[nTargetIdx];
    if (pTarget->nType != 1 || pTarget->bActive == 0 || pTarget->bVisible == 0) {
        return App_ScriptNormalAirFail("natural_contact", "flag10_target_not_visible_active_ufo",
                                       nPulses, -1, nTargetIdx, pTarget->nSeed);
    }
    nTargetSeed = pTarget->nSeed;
    printf("pocketufo: normal-air contact source=c353 pulses=%d seconds=%d clockStart=%d tickSec=%d paused=%u lineFlag=%d target=%d seed=%d targetType=%u targetActive=%u targetVisible=%u targetTimer=%d targetPhase=%d targetSub=%u targetTravel=%u\n",
           (int)nPulses, (int)g_world.geo.time.nTotalSec, (int)nClockStart,
           (int)g_world.geo.world.nTickSeconds, (unsigned)g_world.geo.time.bPaused,
           (int)App_ScriptNormalLine0Flag(), (int)nTargetIdx, (int)nTargetSeed,
           (unsigned)pTarget->nType, (unsigned)pTarget->bActive, (unsigned)pTarget->bVisible,
           (int)pTarget->nTimer, (int)pTarget->nPhase, (unsigned)pTarget->nMissionSub,
           (unsigned)pTarget->uTravelMin);

    if (!App_ScriptNormalClickControl(g_world.ui.apCtl[GEOUI_CTL_186BB4])) {
        return App_ScriptNormalAirFail("contact_clear", "geo2_not_live",
                                       nPulses, -1, nTargetIdx, nTargetSeed);
    }
    App_DrawFrame();
    if (App_ScriptNormalLine0Flag() != 0 || g_world.geo.time.bPaused != 0
        || g_world.bMapSelectionFlag != 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bGeoscapeUi == 0 || g_world.sModal.bOpen != 0 || g_world.view.nMode != 0) {
        return App_ScriptNormalAirFail("contact_clear", "clear_click_did_not_resume_map",
                                       nPulses, -1, nTargetIdx, nTargetSeed);
    }
    printf("pocketufo: normal-air cleared flag=%d paused=%u selection=%u\n",
           (int)App_ScriptNormalLine0Flag(), (unsigned)g_world.geo.time.bPaused,
           (unsigned)g_world.bMapSelectionFlag);

    for (i32 i = 0; i < g_world.strategList.nCount; ++i) {
        StrategItem *pItem = (StrategItem *)g_world.strategList.apItems[i];
        if (pItem != NULL && pItem->nType == 0 && pItem->bActive == 0
            && pItem->nSiteId == (i32)(i8)g_world.apBases[0]->bUnk4) {
            nBaseIdx = i;
            break;
        }
    }
    if (nBaseIdx < 0 || !App_ScriptNormalClickStrategItem(nBaseIdx,
            (const StrategItem *)g_world.strategList.apItems[nBaseIdx], NULL, NULL)) {
        return App_ScriptNormalAirFail("base_click", "first_base_marker_missing_or_not_clickable",
                                       nPulses, -1, nTargetIdx, nTargetSeed);
    }
    App_DrawFrame();
    if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
        || g_world.sModal.bOpen != 0 || g_world.options.apPanel[0] == NULL
        || g_world.options.apPanel[1] == NULL) {
        return App_ScriptNormalAirFail("screen18", "craft_list_not_open",
                                       nPulses, -1, nTargetIdx, nTargetSeed);
    }
    printf("pocketufo: normal-air base index=%d site=%d\n", (int)nBaseIdx,
           (int)g_world.apBases[0]->bUnk4);
    printf("pocketufo: normal-air screen18 screen=%d options=%u namePanel=%d storesPanel=%d\n",
           (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bOptionsScreenOpen,
           g_world.options.apPanel[0] != NULL, g_world.options.apPanel[1] != NULL);

    if (g_world.geo.time.bPaused == 0 || g_world.bUiAnimStarted == 0) {
        return App_ScriptNormalAirFail("options_animation", "animation_not_started_or_not_paused",
                                       nPulses, -1, nTargetIdx, nTargetSeed);
    }
    {
        const i32 nAnimationClock = g_world.geo.time.nTotalSec;
        while (g_world.cUiAnimProgress < 100 && nOptionsFrames < 101) {
            if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
                || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused == 0
                || g_world.options.apPanel[0] == NULL || g_world.options.apPanel[1] == NULL) {
                return App_ScriptNormalAirFail("options_animation", "state_changed",
                                               nPulses, -1, nTargetIdx, nTargetSeed);
            }
            App_GameTickOnce(NULL);
            App_TickAnimProgress();
            App_TickAnim();
            App_DrawFrame();
            ++nOptionsFrames;
        }
        if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
            || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused == 0
            || g_world.options.apPanel[0] == NULL || g_world.options.apPanel[1] == NULL
            || g_world.cUiAnimProgress < 100 || g_world.geo.time.nTotalSec != nAnimationClock) {
            return App_ScriptNormalAirFail("options_animation", "did_not_finish_cleanly",
                                           nPulses, -1, nTargetIdx, nTargetSeed);
        }
        printf("pocketufo: normal-air options-animation frames=%d progress=%d started=%u clock=%d paused=%u\n",
               (int)nOptionsFrames, (int)g_world.cUiAnimProgress,
               (unsigned)g_world.bUiAnimStarted, (int)g_world.geo.time.nTotalSec,
               (unsigned)g_world.geo.time.bPaused);
    }

    {
        const UiPanel *pName = g_world.options.apPanel[0];
        const UiPanel *pStores = g_world.options.apPanel[1];
        const i32 nRowStart = pName->nUnk3028;
        i32 nRowEnd = pName->nLineCount;
        if (nRowEnd > nRowStart + pName->nStyle) { nRowEnd = nRowStart + pName->nStyle; }
        if (nRowEnd > PUFO_PANEL_LINES) { nRowEnd = PUFO_PANEL_LINES; }
        if (nRowEnd > pStores->nLineCount) { nRowEnd = pStores->nLineCount; }
        if (nRowStart < 0 || nRowStart >= PUFO_PANEL_LINES || pName->nFont <= 0) {
            return App_ScriptNormalAirFail("fighter_row", "name_rows_not_live",
                                           nPulses, -1, nTargetIdx, nTargetSeed);
        }
        App_ScriptNormalAirRowCensus(pName, pStores, nRowStart, nRowEnd);
        for (i32 nRow = nRowStart; nRow < nRowEnd; ++nRow) {
            const i32 nCandidate = pName->anLineAux[nRow];
            const Ship *pCandidate;
            if (nCandidate < 0 || nCandidate >= (i32)g_world.bCraftCount
                || pStores->anLineAux[nRow] != 0) {
                continue;
            }
            pCandidate = g_world.apCraft[nCandidate];
            if (pCandidate == NULL || pCandidate->bCraftType != CRAFT_TYPE_INTERCEPTOR
                || pCandidate->bAtBase == 0
                || pCandidate->uRangeRemain != pCandidate->uRangeMax
                || pCandidate->nFuel != (i16)pCandidate->uFuelMax
                || !App_ScriptNormalAirHasLoadedWeapon(pCandidate)) {
                continue;
            }
            nShipIdx = nCandidate;
            nReadyRow = nRow;
            break;
        }
        if (nShipIdx < 0) {
            return App_ScriptNormalAirFail("fighter_row", "no_ready_armed_interceptor_row",
                                           nPulses, -1, nTargetIdx, nTargetSeed);
        }
        pShip = g_world.apCraft[nShipIdx];
        {
            const i32 nX = pName->nRectX + 45;
            const i32 nY = pName->nPosY + (nReadyRow - pName->nUnk3028) * pName->nFont
                + pName->nFont / 2;
            if (nX <= pName->nRectX || nX >= pName->nRectX + pName->nViewW
                || nY <= pName->nRectY || nY >= pName->nRectY + pName->nViewH
                || nX >= 240 || nY >= 320) {
                return App_ScriptNormalAirFail("fighter_row", "ready_row_not_visible",
                                               nPulses, nShipIdx, nTargetIdx, nTargetSeed);
            }
            printf("pocketufo: normal-air selected-row row=%d craft=%d craftId=%u type=%u rowReadyAux=%d canDogfight=%u weaponL=%d ammoL=%d weaponR=%d ammoR=%d atBase=%u range=%u/%u fuel=%d/%u loadedWeapon=%d point=%d/%d\n",
                   (int)nReadyRow, (int)nShipIdx, (unsigned)pShip->bId,
                   (unsigned)pShip->bCraftType, (int)pStores->anLineAux[nReadyRow],
                   (unsigned)pShip->bCanDogfight, (int)pShip->bWeaponL,
                   App_ScriptNormalAirAmmo(pShip, pShip->bWeaponL),
                   (int)pShip->bWeaponR, App_ScriptNormalAirAmmo(pShip, pShip->bWeaponR),
                   (unsigned)pShip->bAtBase, (unsigned)pShip->uRangeRemain,
                   (unsigned)pShip->uRangeMax, (int)pShip->nFuel,
                   (unsigned)pShip->uFuelMax,
                   App_ScriptNormalAirHasLoadedWeapon(pShip) ? 1 : 0,
                   (int)nX, (int)nY);
            App_ScriptPointerLogical(nX, nY, true);
            App_OnPointerUp();
        }
    }
    pShip = g_world.apCraft[nShipIdx];
    if (pShip == NULL || g_world.nSelectedShipIdx != nShipIdx
        || g_world.bMapSelectionFlag == 0 || App_ScriptNormalLine0Flag() != 27
        || g_world.msgLines.anColor[0][0] != (u32)nShipIdx || g_world.sModal.bOpen != 0) {
        return App_ScriptNormalAirFail("fighter_select", "real_row_did_not_arm_flag27",
                                       nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    }
    bShipId = pShip->bId;
    bCraftType = pShip->bCraftType;
    printf("pocketufo: normal-air selected craft=%d craftId=%u type=%u flag27=%d slot0=%u selection=%u selected=%d\n",
           (int)nShipIdx, (unsigned)bShipId, (unsigned)bCraftType,
           (int)App_ScriptNormalLine0Flag(), (unsigned)g_world.msgLines.anColor[0][0],
           (unsigned)g_world.bMapSelectionFlag, (int)g_world.nSelectedShipIdx);
    App_DrawFrame();
    if (g_world.screen.nGeoscapeScreen != 0 || g_world.bOptionsScreenOpen != 0
        || g_world.nSelectedShipIdx != nShipIdx || g_world.bMapSelectionFlag == 0
        || g_world.sModal.bOpen != 0 || g_world.view.nMode != 0) {
        return App_ScriptNormalAirFail("fighter_select", "screen18_did_not_close_on_draw",
                                       nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    }

    if (nTargetIdx >= g_world.strategList.nCount || g_world.strategList.apItems[nTargetIdx] == NULL) {
        return App_ScriptNormalAirFail("target_click", "natural_contact_gone",
                                       nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    }
    pTarget = (StrategItem *)g_world.strategList.apItems[nTargetIdx];
    if (pTarget->nType != 1 || pTarget->bActive == 0 || pTarget->bVisible == 0
        || pTarget->nSeed != nTargetSeed) {
        return App_ScriptNormalAirFail("target_click", "natural_contact_changed",
                                       nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    }
    if (!App_ScriptNormalClickStrategItem(nTargetIdx, pTarget, NULL, NULL)) {
        return App_ScriptNormalAirFail("target_click", "contact_offscreen_or_wrong_map_mode",
                                       nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    }
    App_DrawFrame();
    if (App_ScriptNormalLine0Flag() != 10 || g_world.bMapSelectionFlag == 0
        || g_world.msgLines.anColor[0][0] != (u32)nTargetIdx || g_world.sModal.bOpen != 0) {
        return App_ScriptNormalAirFail("target_select", "real_ufo_click_did_not_reach_flag10",
                                       nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    }
    printf("pocketufo: normal-air target index=%d seed=%d lineFlag=%d slot0=%u selection=%u\n",
           (int)nTargetIdx, (int)nTargetSeed, (int)App_ScriptNormalLine0Flag(),
           (unsigned)g_world.msgLines.anColor[0][0], (unsigned)g_world.bMapSelectionFlag);

    pShip = g_world.apCraft[nShipIdx];
    if (pShip == NULL || pShip->bAtBase == 0 || pShip->bIntercepting != 0) {
        return App_ScriptNormalAirFail("launch", "selected_interceptor_not_ready_at_base",
                                       nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    }
    nCraftTargetBefore = pShip->nStrategIdx;
    {
        UiControl *pConfirm = g_world.ui.apCtl[GEOUI_CTL_186BA4];
        const i32 nX = pConfirm != NULL ? (i32)pConfirm->x + (i32)pConfirm->uWidth / 2 : -1;
        const i32 nY = pConfirm != NULL ? (i32)pConfirm->y + (i32)pConfirm->uHeight / 2 : -1;
        const u8 bAtBaseBefore = pShip->bAtBase;
        const u8 bInterceptBefore = pShip->bIntercepting;
        if (!App_ScriptNormalClickControl(pConfirm)) {
            return App_ScriptNormalAirFail("launch", "geo1_confirm_not_live",
                                           nPulses, nShipIdx, nTargetIdx, nTargetSeed);
        }
        pShip = g_world.apCraft[nShipIdx];
        pTarget = nTargetIdx < g_world.strategList.nCount
            ? (StrategItem *)g_world.strategList.apItems[nTargetIdx] : NULL;
        printf("pocketufo: normal-air launch confirm=geo1 confirmCenter=%d/%d craft=%d craftId=%u type=%u canDogfight=%u target=%d seed=%d atBaseBefore=%u atBaseAfter=%u interceptBefore=%u interceptAfter=%u shipTargetBefore=%d shipTargetAfter=%d targetSeedAfter=%d targetTimer=%d clock=%d pause=%u lineFlag=%d selection=%u\n",
               (int)nX, (int)nY, (int)nShipIdx, (unsigned)bShipId, (unsigned)bCraftType,
               pShip != NULL ? (unsigned)pShip->bCanDogfight : 255u,
               (int)nTargetIdx, (int)nTargetSeed, (unsigned)bAtBaseBefore,
               pShip != NULL ? (unsigned)pShip->bAtBase : 255u,
               (unsigned)bInterceptBefore, pShip != NULL ? (unsigned)pShip->bIntercepting : 255u,
               (int)nCraftTargetBefore, pShip != NULL ? (int)pShip->nStrategIdx : -1,
               pTarget != NULL ? (int)pTarget->nSeed : -1,
               pTarget != NULL ? (int)pTarget->nTimer : -999999,
               (int)g_world.geo.time.nTotalSec, (unsigned)g_world.geo.time.bPaused,
               (int)App_ScriptNormalLine0Flag(), (unsigned)g_world.bMapSelectionFlag);
        if (pShip == NULL || pShip->bId != bShipId || pShip->bCraftType != bCraftType
            || pShip->bAtBase != 0 || pShip->bIntercepting == 0
            || pShip->nStrategIdx != nTargetIdx || pTarget == NULL
            || pTarget->nSeed != nTargetSeed) {
            return App_ScriptNormalAirFail("launch", "real_confirm_did_not_order_selected_fighter",
                                           nPulses, nShipIdx, nTargetIdx, nTargetSeed);
        }
    }
    if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
        || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
        || App_ScriptNormalLine0Flag() != 0 || g_world.bMapSelectionFlag != 0) {
        return App_ScriptNormalAirFail("launch", "order_did_not_leave_clear_running_map",
                                       nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    }
    bTargetVisibleLast = pTarget->bVisible;
    App_ScriptNormalAirObserve("real_order", 0, nShipIdx, nTargetIdx, nTargetSeed);

    if (!App_ScriptNormalClickControl(g_world.ui.apRootCtl[3])
        || g_world.geo.world.nTickSeconds != 60) {
        return App_ScriptNormalAirFail("minute_speed", "one_minute_control_not_live",
                                       0, nShipIdx, nTargetIdx, nTargetSeed);
    }
    nFlightClockStart = g_world.geo.time.nTotalSec;
    printf("pocketufo: normal-air minute-speed root=3 tickSec=%d clockStart=%d\n",
           (int)g_world.geo.world.nTickSeconds, (int)nFlightClockStart);
    App_ScriptNormalAirObserve("flight_start", 0, nShipIdx, nTargetIdx, nTargetSeed);

    for (nArrivalPulses = 1; nArrivalPulses <= kMaxArrivalPulses; ++nArrivalPulses) {
        if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
            || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
            || App_ScriptNormalLine0Flag() != 0 || g_world.bRadarScreenActive != 0) {
            return App_ScriptNormalAirFail("arrival", "state_changed_before_radar_boundary",
                                           nArrivalPulses - 1, nShipIdx, nTargetIdx, nTargetSeed);
        }
        App_ScriptTicks(1);
        pShip = nShipIdx < (i32)g_world.bCraftCount ? g_world.apCraft[nShipIdx] : NULL;
        pTarget = nTargetIdx < g_world.strategList.nCount
            ? (StrategItem *)g_world.strategList.apItems[nTargetIdx] : NULL;
        if (pTarget != NULL && pTarget->nType == 1 && pTarget->nSeed == nTargetSeed
            && pTarget->bVisible != bTargetVisibleLast) {
            printf("pocketufo: normal-air visibility stage=flight pulses=%d target=%d seed=%d previous=%u visible=%u craft=%d intercept=%u pause=%u flag=%d\n",
                   (int)nArrivalPulses, (int)nTargetIdx, (int)nTargetSeed,
                   (unsigned)bTargetVisibleLast, (unsigned)pTarget->bVisible,
                   (int)nShipIdx, pShip != NULL ? (unsigned)pShip->bIntercepting : 255u,
                   (unsigned)g_world.geo.time.bPaused, (int)App_ScriptNormalLine0Flag());
            bTargetVisibleLast = pTarget->bVisible;
        }
        if (g_world.bRadarScreenActive != 0) {
            const StrategItem *pOwn = NULL;
            i32 nOwnIdx = -1;
            if (pShip != NULL && g_world.strategList.apItems != NULL) {
                for (i32 i = 0; i < g_world.strategList.nCount; ++i) {
                    const StrategItem *pItem = (const StrategItem *)g_world.strategList.apItems[i];
                    if (pItem != NULL && pItem->nType == 1 && pItem->bActive == 0
                        && pItem->nDisplayId == (i32)pShip->bId) {
                        pOwn = pItem;
                        nOwnIdx = i;
                        break;
                    }
                }
            }
            printf("pocketufo: normal-air boundary kind=radar_active pulses=%d seconds=%d clockStart=%d tickSec=%d radarActive=%u paused=%u screen=%d modal=%u lineFlag=%d target=%d seedExpected=%d seed=%d targetType=%d targetActive=%u targetVisible=%u targetTimer=%d targetPhase=%d targetSub=%u targetTravel=%u craft=%d craftId=%d craftType=%d craftTarget=%d craftIntercept=%u craftAtBase=%u craftFuel=%d craftRange=%u ownIndex=%d ownX=%d ownY=%d\n",
                   (int)nArrivalPulses, (int)g_world.geo.time.nTotalSec,
                   (int)nFlightClockStart, (int)g_world.geo.world.nTickSeconds,
                   (unsigned)g_world.bRadarScreenActive, (unsigned)g_world.geo.time.bPaused,
                   (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.sModal.bOpen,
                   (int)App_ScriptNormalLine0Flag(), (int)nTargetIdx, (int)nTargetSeed,
                   pTarget != NULL ? (int)pTarget->nSeed : -1,
                   pTarget != NULL ? (int)pTarget->nType : -1,
                   pTarget != NULL ? (unsigned)pTarget->bActive : 255u,
                   pTarget != NULL ? (unsigned)pTarget->bVisible : 255u,
                   pTarget != NULL ? (int)pTarget->nTimer : -999999,
                   pTarget != NULL ? (int)pTarget->nPhase : -1,
                   pTarget != NULL ? (unsigned)pTarget->nMissionSub : 255u,
                   pTarget != NULL ? (unsigned)pTarget->uTravelMin : 0u,
                   (int)nShipIdx, pShip != NULL ? (int)pShip->bId : -1,
                   pShip != NULL ? (int)pShip->bCraftType : -1,
                   pShip != NULL ? (int)pShip->nStrategIdx : -1,
                   pShip != NULL ? (unsigned)pShip->bIntercepting : 255u,
                   pShip != NULL ? (unsigned)pShip->bAtBase : 255u,
                   pShip != NULL ? (int)pShip->nFuel : -1,
                   pShip != NULL ? (unsigned)pShip->uRangeRemain : 0u,
                   (int)nOwnIdx, pOwn != NULL ? (int)pOwn->nX : -1,
                   pOwn != NULL ? (int)pOwn->nY : -1);
            App_ScriptNormalAirObserve("radar_boundary", nArrivalPulses,
                                       nShipIdx, nTargetIdx, nTargetSeed);
            if (pTarget == NULL || pTarget->nType != 1 || pTarget->bActive == 0
                || pTarget->nSeed != nTargetSeed
                || pTarget->nTimer > 0 || pShip == NULL || pShip->bId != bShipId
                || pShip->bCraftType != bCraftType || pShip->nStrategIdx != nTargetIdx
                || pShip->bAtBase != 0 || pShip->bIntercepting == 0
                || pShip->nFuel <= 0 || pShip->uRangeRemain == 0
                || g_world.geo.time.bPaused == 0 || App_ScriptNormalLine0Flag() != 0
                || g_world.sModal.bOpen != 0) {
                return App_ScriptNormalAirFail("radar_boundary", "radar_state_not_same_flying_target_arrival",
                                               nArrivalPulses, nShipIdx, nTargetIdx, nTargetSeed);
            }
            printf("pocketufo: normal-air PASS stage=radar_boundary pulses=%d clock=%d target=%d seed=%d craft=%d craftId=%u\n",
                   (int)nArrivalPulses, (int)g_world.geo.time.nTotalSec,
                   (int)nTargetIdx, (int)nTargetSeed, (int)nShipIdx, (unsigned)bShipId);
            fflush(stdout);
            return true;
        }
        if (pTarget == NULL || pTarget->nType != 1 || pTarget->bActive == 0
            || pTarget->nSeed != nTargetSeed) {
            return App_ScriptNormalAirFail("arrival", "natural_target_gone_or_changed",
                                           nArrivalPulses, nShipIdx, nTargetIdx, nTargetSeed);
        }
        if (pShip == NULL || pShip->bId != bShipId || pShip->bCraftType != bCraftType
            || pShip->bAtBase != 0 || pShip->bIntercepting == 0
            || pShip->nStrategIdx != nTargetIdx) {
            return App_ScriptNormalAirFail("arrival", "fighter_order_or_identity_changed",
                                           nArrivalPulses, nShipIdx, nTargetIdx, nTargetSeed);
        }
        if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
            || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
            || App_ScriptNormalLine0Flag() != 0) {
            return App_ScriptNormalAirFail("arrival", "other_screen_modal_pause_or_message",
                                           nArrivalPulses, nShipIdx, nTargetIdx, nTargetSeed);
        }
        if (pShip->nFuel <= 0 || pShip->uRangeRemain == 0) {
            return App_ScriptNormalAirFail("arrival", "fighter_fuel_or_range_exhausted",
                                           nArrivalPulses, nShipIdx, nTargetIdx, nTargetSeed);
        }
        if (nArrivalPulses % 60 == 0) {
            App_ScriptNormalAirObserve("flight_wait", nArrivalPulses,
                                       nShipIdx, nTargetIdx, nTargetSeed);
            printf("pocketufo: normal-air wait pulses=%d seconds=%d targetTimer=%d targetPhase=%d targetSub=%u fuel=%d range=%u flag=%d pause=%u\n",
                   (int)nArrivalPulses, (int)g_world.geo.time.nTotalSec,
                   (int)pTarget->nTimer, (int)pTarget->nPhase,
                   (unsigned)pTarget->nMissionSub, (int)pShip->nFuel,
                   (unsigned)pShip->uRangeRemain, (int)App_ScriptNormalLine0Flag(),
                   (unsigned)g_world.geo.time.bPaused);
        }
    }
    return App_ScriptNormalAirFail("arrival", "minute_pulse_bound_600",
                                   kMaxArrivalPulses, nShipIdx, nTargetIdx, nTargetSeed);
}

static i32 App_ScriptNormalAirCombatFindCraft(u8 bId)
{
    for (i32 i = 0; i < (i32)g_world.bCraftCount; ++i) {
        const Ship *pShip = g_world.apCraft[i];
        if (pShip != NULL && pShip->bId == bId) {
            return i;
        }
    }
    return -1;
}

static i32 App_ScriptNormalAirCombatFindTarget(i32 nSeed)
{
    if (g_world.strategList.apItems == NULL) {
        return -1;
    }
    for (i32 i = 0; i < g_world.strategList.nCount; ++i) {
        const StrategItem *pItem = (const StrategItem *)g_world.strategList.apItems[i];
        if (pItem != NULL && pItem->nSeed == nSeed) {
            return i;
        }
    }
    return -1;
}

static bool App_ScriptNormalAirCombatPairMatches(i32 nExpectedTarget, i32 nTargetSeed,
                                                 u8 bShipId, u8 bCraftType)
{
    const i32 nShipIdx = App_ScriptNormalAirCombatFindCraft(bShipId);
    const i32 nTargetIdx = App_ScriptNormalAirCombatFindTarget(nTargetSeed);
    const Ship *pShip = nShipIdx >= 0 ? g_world.apCraft[nShipIdx] : NULL;
    const StrategItem *pTarget = nTargetIdx >= 0
        ? (const StrategItem *)g_world.strategList.apItems[nTargetIdx] : NULL;

    return nShipIdx >= 0 && nTargetIdx == nExpectedTarget
        && pShip != NULL && pShip->bId == bShipId && pShip->bCraftType == bCraftType
        && pShip->bAtBase == 0 && pShip->bIntercepting != 0
        && pShip->nStrategIdx == nExpectedTarget
        && pTarget != NULL && pTarget->nSeed == nTargetSeed
        && pTarget->bActive != 0 && pTarget->nType == 1;
}

/* Match the live radar message to the source TEXTS record without naming an outcome by guess. */
static i32 App_ScriptNormalAirCombatMessageRecord(void)
{
    if (g_assets.pTextRecs == NULL) {
        return -1;
    }
    for (i32 nId = PUFO_RADAR_TEXT_CURSOR_NW; nId <= PUFO_RADAR_TEXT_TARGET_CRASHED; ++nId) {
        const char *pRecord = (const char *)g_assets.pTextRecs
            + (size_t)nId * PUFO_TEXT_REC_STRIDE;
        size_t i = 0;
        while (i < sizeof(g_world.szLabelScratch)
               && g_world.szLabelScratch[i] == pRecord[i]) {
            if (g_world.szLabelScratch[i] == '\0') {
                return nId;
            }
            ++i;
        }
    }
    return -1;
}

/* Read-only combat census. Craft and target pointers are looked up again on every call. */
static void App_ScriptNormalAirCombatObserve(const char *pszStage, i32 nPulses,
                                            i32 nExpectedTarget, i32 nTargetSeed,
                                            u8 bShipId)
{
    const i32 nShipIdx = App_ScriptNormalAirCombatFindCraft(bShipId);
    const i32 nTargetIdx = App_ScriptNormalAirCombatFindTarget(nTargetSeed);
    const Ship *pShip = nShipIdx >= 0 ? g_world.apCraft[nShipIdx] : NULL;
    const StrategItem *pTarget = nTargetIdx >= 0
        ? (const StrategItem *)g_world.strategList.apItems[nTargetIdx] : NULL;

    App_ScriptNormalAirObserve(pszStage, nPulses, nShipIdx, nTargetIdx, nTargetSeed);
    printf("pocketufo: normal-air-combat state stage=%s pulses=%d sec=%d tickSec=%d paused=%u screen=%d geoUi=%u modal=%u radarActive=%u done=%u outOfFuel=%u traceLost=%u flip=%u lineCount=%u lineFlag=%d shortfall=%u spawnOnce=%u\n",
           pszStage, (int)nPulses, (int)g_world.geo.time.nTotalSec,
           (int)g_world.geo.world.nTickSeconds, (unsigned)g_world.geo.time.bPaused,
           (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bGeoscapeUi,
           (unsigned)g_world.sModal.bOpen, (unsigned)g_world.bRadarScreenActive,
           (unsigned)g_world.radar.bInterceptDone, (unsigned)g_world.radar.bCraftOutOfFuel,
           (unsigned)g_world.radar.bTraceLost, (unsigned)g_world.radar.bFlip,
           (unsigned)g_world.msgLines.nLineCount, (int)App_ScriptNormalLine0Flag(),
           (unsigned)g_world.nShortfall1870C8, (unsigned)g_world.bSpawnOnce);
    printf("pocketufo: normal-air-combat radar stage=%s hp=%d/%d ammoL=%d ammoR=%d engageL=%u engageR=%u cooldownL=%d cooldownR=%d shotYL=%d shotYR=%d lastShotHit=%u cursor=%d/%d rangeCur=%d rangeTarget=%d rangeLimit=%d boundL=%d boundR=%d msgTimer=%d pulseTimer=%d messageRec=%d\n",
           pszStage, (int)g_world.radar.nTargetHp, (int)g_world.radar.nTargetHpMax,
           (int)g_world.radar.nAmmoL, (int)g_world.radar.nAmmoR,
           (unsigned)g_world.radar.bEngageL, (unsigned)g_world.radar.bEngageR,
           (int)g_world.nRadarCooldownL, (int)g_world.frame.g.nTapCooldown,
           (int)g_world.radar.nShotYL, (int)g_world.radar.nShotYR,
           (unsigned)g_world.radar.bLastShotHit, (int)g_world.radar.nCursorX,
           (int)g_world.radar.nCursorY, (int)g_world.radar.nRangeCur,
           (int)g_world.radar.nRangeTarget, (int)g_world.radar.nRangeLimit,
           (int)g_world.radar.nBoundL, (int)g_world.radar.nBoundR,
           (int)g_world.nRadarMsgTimer, (int)g_world.nRadarPulseTimer,
           (int)App_ScriptNormalAirCombatMessageRecord());
    printf("pocketufo: normal-air-combat identity stage=%s targetExpected=%d targetCurrent=%d seedExpected=%d seed=%d targetPresent=%d type=%d active=%u visible=%u at=%d/%d timer=%d phase=%d sub=%u travel=%u epoch=%d hp=%d done=%u craftExpectedId=%u craftCurrent=%d craftId=%d craftType=%d atBase=%u intercept=%u shipTarget=%d weaponL=%d ammoL=%d weaponR=%d ammoR=%d fuel=%d/%u range=%u/%u\n",
           pszStage, (int)nExpectedTarget, (int)nTargetIdx, (int)nTargetSeed,
           pTarget != NULL ? (int)pTarget->nSeed : -1, pTarget != NULL ? 1 : 0,
           pTarget != NULL ? (int)pTarget->nType : -1,
           pTarget != NULL ? (unsigned)pTarget->bActive : 255u,
           pTarget != NULL ? (unsigned)pTarget->bVisible : 255u,
           pTarget != NULL ? (int)pTarget->nX : -1,
           pTarget != NULL ? (int)pTarget->nY : -1,
           pTarget != NULL ? (int)pTarget->nTimer : -999999,
           pTarget != NULL ? (int)pTarget->nPhase : -1,
           pTarget != NULL ? (unsigned)pTarget->nMissionSub : 255u,
           pTarget != NULL ? (unsigned)pTarget->uTravelMin : 0u,
           pTarget != NULL ? (int)pTarget->nTravelEpochSec : -1,
           pTarget != NULL ? (int)pTarget->nHp : -999999,
           pTarget != NULL ? (unsigned)pTarget->bDone : 255u,
           (unsigned)bShipId, (int)nShipIdx,
           pShip != NULL ? (int)pShip->bId : -1,
           pShip != NULL ? (int)pShip->bCraftType : -1,
           pShip != NULL ? (unsigned)pShip->bAtBase : 255u,
           pShip != NULL ? (unsigned)pShip->bIntercepting : 255u,
           pShip != NULL ? (int)pShip->nStrategIdx : -1,
           pShip != NULL ? (int)pShip->bWeaponL : -1,
           pShip != NULL ? App_ScriptNormalAirAmmo(pShip, pShip->bWeaponL) : PUFO_RADAR_AMMO_NONE,
           pShip != NULL ? (int)pShip->bWeaponR : -1,
           pShip != NULL ? App_ScriptNormalAirAmmo(pShip, pShip->bWeaponR) : PUFO_RADAR_AMMO_NONE,
           pShip != NULL ? (int)pShip->nFuel : -1,
           pShip != NULL ? (unsigned)pShip->uFuelMax : 0u,
           pShip != NULL ? (unsigned)pShip->uRangeRemain : 0u,
           pShip != NULL ? (unsigned)pShip->uRangeMax : 0u);
    fflush(stdout);
}

static bool App_ScriptNormalAirCombatFail(const char *pszStage, const char *pszWhy,
                                          i32 nPulses, i32 nExpectedTarget,
                                          i32 nTargetSeed, u8 bShipId)
{
    printf("pocketufo: normal-air-combat FAIL stage=%s why=%s\n", pszStage, pszWhy);
    App_ScriptNormalAirCombatObserve(pszStage, nPulses, nExpectedTarget,
                                     nTargetSeed, bShipId);
    App_ScriptState(pszStage);
    App_ScriptLines();
    App_ScriptStrateg();
    fflush(stdout);
    return false;
}

static bool App_ScriptNormalAirCombatMountAmmoMatches(const Ship *pShip,
                                                       i32 nMount, i32 nSessionAmmo)
{
    if (pShip == NULL || nMount == 28 || nSessionAmmo == PUFO_RADAR_AMMO_NONE
        || nSessionAmmo == PUFO_RADAR_AMMO_UNLIMITED) {
        return true;
    }
    return App_ScriptNormalAirAmmo(pShip, nMount) == nSessionAmmo;
}

static bool App_ScriptNormalAirCombatAmmoWritebackMatches(const Ship *pShip,
                                                          i32 nAmmoL, i32 nAmmoR)
{
    if (pShip == NULL) {
        return false;
    }
    if (pShip->bWeaponL == pShip->bWeaponR
        && pShip->bWeaponL != 28 && pShip->bWeaponL != 0) {
        const i32 nAmmo = App_ScriptNormalAirAmmo(pShip, pShip->bWeaponL);
        return nAmmo == (i32)(u8)(nAmmoL + nAmmoR);
    }
    return App_ScriptNormalAirCombatMountAmmoMatches(pShip, pShip->bWeaponL, nAmmoL)
        && App_ScriptNormalAirCombatMountAmmoMatches(pShip, pShip->bWeaponR, nAmmoR);
}

/* Debug-script handoff only: the ground step must follow the same live landed item. */
static bool g_bNormalAirGroundSiteReady;
static i32 g_nNormalAirGroundSiteSeed;

static bool App_ScriptNormalAirCombat(void)
{
    enum { kMaxCombatPulses = 4096, kMaxRadarAnimFrames = 101 };
    i32 nPulses = 0;
    i32 nTargetIdx;
    i32 nTargetSeed;
    i32 nShipIdx;
    i32 nClockStart;
    i32 nHpStart;
    i32 nAmmoLStart;
    i32 nAmmoRStart;
    i32 nAnimFrames = 0;
    i32 nOutcomeTextRec = -1;
    u8 bShipId;
    u8 bCraftType;
    bool bSawAmmo = false;
    bool bSawDamage = false;
    bool bSawPersistentHp = false;
    bool bDamageSnapshot = false;
    bool bFiringSnapshot = false;
    bool bTerminalSnapshot = false;
    bool bSwClicked = false;
    Ship *pShip;
    StrategItem *pTarget;

    g_bNormalAirGroundSiteReady = false;
    g_nNormalAirGroundSiteSeed = 0;

    enum {
        kOutcomeLandedSite = 1,
        kOutcomeCrashedSite = 2,
        kOutcomeDestroyed = 3,
        kOutcomeSeaRemoval = 4,
        kOutcomeTraceLoss = 5,
        kOutcomeCraftLoss = 6,
        kOutcomeUnknown = 7
    };

    if (!g_bWorldReady || g_world.bTactLive || g_world.bRadarScreenActive == 0
        || g_world.geo.time.bPaused == 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.sModal.bOpen != 0 || g_world.bGeoscapeUi == 0
        || App_ScriptNormalLine0Flag() != 0) {
        return App_ScriptNormalAirCombatFail("start", "need_live_paused_radar_boundary",
                                             0, -1, 0, 0);
    }
    nShipIdx = g_world.nInterceptShipIdx;
    nTargetIdx = g_world.nInterceptStrategIdx;
    if (g_world.strategList.apItems == NULL
        || nShipIdx < 0 || nShipIdx >= (i32)g_world.bCraftCount
        || nTargetIdx < 0 || nTargetIdx >= g_world.strategList.nCount) {
        return App_ScriptNormalAirCombatFail("start", "bad_live_intercept_pair",
                                             0, nTargetIdx, 0, 0);
    }
    pShip = g_world.apCraft[nShipIdx];
    pTarget = (StrategItem *)g_world.strategList.apItems[nTargetIdx];
    if (pShip == NULL || pTarget == NULL || pShip->bAtBase != 0
        || pShip->bIntercepting == 0 || pShip->nStrategIdx != nTargetIdx
        || pShip->bCraftType != CRAFT_TYPE_INTERCEPTOR
        || pTarget->nType != 1 || pTarget->bActive == 0 || pTarget->nTimer > 0
        || g_world.radar.bInterceptDone != 0 || g_world.radar.bCraftOutOfFuel != 0
        || g_world.radar.bTraceLost != 0
        || g_world.msgLines.nLineCount != 0 || !App_ScriptNormalAirHasLoadedWeapon(pShip)) {
        return App_ScriptNormalAirCombatFail("start", "radar_owner_state_not_live_flying_target",
                                             0, nTargetIdx, pTarget != NULL ? pTarget->nSeed : 0,
                                             pShip != NULL ? pShip->bId : 0);
    }
    nTargetSeed = pTarget->nSeed;
    bShipId = pShip->bId;
    bCraftType = pShip->bCraftType;
    nClockStart = g_world.geo.time.nTotalSec;
    nHpStart = g_world.radar.nTargetHp;
    nAmmoLStart = g_world.radar.nAmmoL;
    nAmmoRStart = g_world.radar.nAmmoR;

    printf("pocketufo: normal-air-combat start ship=%d shipId=%u craftType=%u target=%d seed=%d hp=%d hpMax=%d ammoL=%d ammoR=%d clock=%d\n",
           (int)nShipIdx, (unsigned)bShipId, (unsigned)bCraftType,
           (int)nTargetIdx, (int)nTargetSeed, (int)nHpStart,
           (int)g_world.radar.nTargetHpMax, (int)nAmmoLStart, (int)nAmmoRStart,
           (int)nClockStart);
    App_ScriptNormalAirCombatObserve("start", 0, nTargetIdx, nTargetSeed, bShipId);
    if (g_world.bUiAnimStarted == 0) {
        return App_ScriptNormalAirCombatFail("radar_animation", "panel_animation_not_started",
                                             0, nTargetIdx, nTargetSeed, bShipId);
    }
    while (g_world.cUiAnimProgress < 100 && nAnimFrames < kMaxRadarAnimFrames) {
        if (g_world.bRadarScreenActive == 0 || g_world.geo.time.bPaused == 0
            || g_world.screen.nGeoscapeScreen != 0 || g_world.sModal.bOpen != 0
            || g_world.geo.time.nTotalSec != nClockStart
            || !App_ScriptNormalAirCombatPairMatches(nTargetIdx, nTargetSeed,
                                                       bShipId, bCraftType)) {
            return App_ScriptNormalAirCombatFail("radar_animation", "state_changed_while_paused",
                                                 0, nTargetIdx, nTargetSeed, bShipId);
        }
        App_GameTickOnce(NULL);
        App_TickAnimProgress();
        App_TickAnim();
        App_DrawFrame();
        ++nAnimFrames;
    }
    if (g_world.bRadarScreenActive == 0 || g_world.geo.time.bPaused == 0
        || g_world.screen.nGeoscapeScreen != 0 || g_world.sModal.bOpen != 0
        || g_world.cUiAnimProgress < 100 || g_world.geo.time.nTotalSec != nClockStart
        || !App_ScriptNormalAirCombatPairMatches(nTargetIdx, nTargetSeed,
                                                   bShipId, bCraftType)) {
        return App_ScriptNormalAirCombatFail("radar_animation", "panel_animation_not_complete",
                                             0, nTargetIdx, nTargetSeed, bShipId);
    }
    printf("pocketufo: normal-air-combat animation frames=%d progress=%d clock=%d paused=%u\n",
           (int)nAnimFrames, (int)g_world.cUiAnimProgress, (int)g_world.geo.time.nTotalSec,
           (unsigned)g_world.geo.time.bPaused);
    App_ScriptDump("radar_combat_before.ppm");
    if (g_world.bRadarScreenActive == 0 || g_world.geo.time.bPaused == 0
        || g_world.geo.time.nTotalSec != nClockStart
        || !App_ScriptNormalAirCombatPairMatches(nTargetIdx, nTargetSeed,
                                                   bShipId, bCraftType)) {
        return App_ScriptNormalAirCombatFail("snapshot_before", "radar_changed_during_snapshot",
                                             0, nTargetIdx, nTargetSeed, bShipId);
    }
    printf("pocketufo: normal-air-combat snapshot stage=before file=radar_combat_before.ppm\n");
    App_ScriptNormalAirCombatObserve("before", 0, nTargetIdx, nTargetSeed, bShipId);

    {
        UiControl *pAttackMax = g_world.radar.ui.apCtl[1];
        const i32 nX = pAttackMax != NULL ? (i32)pAttackMax->x + (i32)pAttackMax->uWidth / 2 : -1;
        const i32 nY = pAttackMax != NULL ? (i32)pAttackMax->y + (i32)pAttackMax->uHeight / 2 : -1;
        const i32 nRangeBefore = g_world.radar.nRangeTarget;
        const i32 nAmmoLBefore = g_world.radar.nAmmoL;
        const i32 nAmmoRBefore = g_world.radar.nAmmoR;
        if (!App_ScriptNormalClickControl(pAttackMax)) {
            return App_ScriptNormalAirCombatFail("attack_max", "live_NE_control_not_ready",
                                                 0, nTargetIdx, nTargetSeed, bShipId);
        }
        printf("pocketufo: normal-air-combat input control=attack_max button=NE index=1 center=%d/%d rangeBefore=%d rangeAfter=%d ammoLBefore=%d ammoLAfter=%d ammoRBefore=%d ammoRAfter=%d messageRec=%d radarActive=%u paused=%u\n",
               (int)nX, (int)nY, (int)nRangeBefore, (int)g_world.radar.nRangeTarget,
               (int)nAmmoLBefore, (int)g_world.radar.nAmmoL,
               (int)nAmmoRBefore, (int)g_world.radar.nAmmoR,
               (int)App_ScriptNormalAirCombatMessageRecord(),
               (unsigned)g_world.bRadarScreenActive, (unsigned)g_world.geo.time.bPaused);
    }
    if (g_world.bRadarScreenActive == 0 || g_world.geo.time.bPaused == 0
        || App_ScriptNormalLine0Flag() != 0
        || App_ScriptNormalAirCombatMessageRecord() != PUFO_RADAR_TEXT_CURSOR_NE
        || !App_ScriptNormalAirCombatPairMatches(nTargetIdx, nTargetSeed,
                                                   bShipId, bCraftType)) {
        return App_ScriptNormalAirCombatFail("attack_max", "NE_input_changed_radar_state",
                                             0, nTargetIdx, nTargetSeed, bShipId);
    }
    App_ScriptNormalAirCombatObserve("attack_armed", 0, nTargetIdx, nTargetSeed, bShipId);

    for (nPulses = 1; nPulses <= kMaxCombatPulses; ++nPulses) {
        i32 nCurrentTargetIdx;
        const StrategItem *pCurrentTarget;
        const i32 nHpBeforePulse = g_world.radar.nTargetHp;
        const i32 nAmmoLBeforePulse = g_world.radar.nAmmoL;
        const i32 nAmmoRBeforePulse = g_world.radar.nAmmoR;

        if (g_world.bRadarScreenActive == 0) {
            break;
        }
        if (g_world.geo.time.bPaused == 0 || g_world.geo.time.nTotalSec != nClockStart
            || g_world.screen.nGeoscapeScreen != 0 || g_world.sModal.bOpen != 0
            || App_ScriptNormalLine0Flag() != 0) {
            return App_ScriptNormalAirCombatFail("combat", "paused_radar_clock_or_ui_changed",
                                                 nPulses - 1, nTargetIdx, nTargetSeed, bShipId);
        }
        App_ScriptTicks(1);
        nCurrentTargetIdx = App_ScriptNormalAirCombatFindTarget(nTargetSeed);
        pCurrentTarget = nCurrentTargetIdx >= 0
            ? (const StrategItem *)g_world.strategList.apItems[nCurrentTargetIdx] : NULL;
        if (g_world.bRadarScreenActive != 0
            && (g_world.geo.time.bPaused == 0 || g_world.geo.time.nTotalSec != nClockStart
                || g_world.screen.nGeoscapeScreen != 0 || g_world.sModal.bOpen != 0)) {
            return App_ScriptNormalAirCombatFail("combat", "timer_or_screen_changed_after_paint",
                                                 nPulses, nTargetIdx, nTargetSeed, bShipId);
        }
        if (g_world.bRadarScreenActive != 0 && g_world.radar.bInterceptDone == 0
            && g_world.radar.bCraftOutOfFuel == 0 && g_world.radar.bTraceLost == 0
            && !App_ScriptNormalAirCombatPairMatches(nTargetIdx, nTargetSeed,
                                                       bShipId, bCraftType)) {
            return App_ScriptNormalAirCombatFail("combat", "live_pair_changed_without_terminal_owner",
                                                 nPulses, nTargetIdx, nTargetSeed, bShipId);
        }
        if (g_world.radar.nAmmoL < nAmmoLStart || g_world.radar.nAmmoR < nAmmoRStart) {
            bSawAmmo = true;
        }
        if (g_world.radar.nAmmoL < nAmmoLBeforePulse
            || g_world.radar.nAmmoR < nAmmoRBeforePulse) {
            bSawAmmo = true;
            printf("pocketufo: normal-air-combat evidence stage=shot pulse=%d ammoLBefore=%d ammoLAfter=%d ammoRBefore=%d ammoRAfter=%d hp=%d rangeCur=%d rangeTarget=%d engageL=%u engageR=%u cooldownL=%d cooldownR=%d shotYL=%d shotYR=%d target=%d seed=%d craftId=%u\n",
                   (int)nPulses, (int)nAmmoLBeforePulse, (int)g_world.radar.nAmmoL,
                   (int)nAmmoRBeforePulse, (int)g_world.radar.nAmmoR,
                   (int)g_world.radar.nTargetHp, (int)g_world.radar.nRangeCur,
                   (int)g_world.radar.nRangeTarget, (unsigned)g_world.radar.bEngageL,
                   (unsigned)g_world.radar.bEngageR, (int)g_world.nRadarCooldownL,
                   (int)g_world.frame.g.nTapCooldown, (int)g_world.radar.nShotYL,
                   (int)g_world.radar.nShotYR, (int)nTargetIdx, (int)nTargetSeed,
                   (unsigned)bShipId);
            if (!bFiringSnapshot) {
                App_ScriptDump("radar_combat_firing.ppm");
                printf("pocketufo: normal-air-combat snapshot stage=firing file=radar_combat_firing.ppm\n");
                bFiringSnapshot = true;
                nCurrentTargetIdx = App_ScriptNormalAirCombatFindTarget(nTargetSeed);
                pCurrentTarget = nCurrentTargetIdx >= 0
                    ? (const StrategItem *)g_world.strategList.apItems[nCurrentTargetIdx] : NULL;
                if (g_world.bRadarScreenActive != 0
                    && (g_world.geo.time.bPaused == 0 || g_world.geo.time.nTotalSec != nClockStart
                        || g_world.screen.nGeoscapeScreen != 0 || g_world.sModal.bOpen != 0)) {
                    return App_ScriptNormalAirCombatFail("firing_snapshot", "timer_or_screen_changed_after_paint",
                                                         nPulses, nTargetIdx, nTargetSeed, bShipId);
                }
                if (g_world.bRadarScreenActive != 0 && g_world.radar.bInterceptDone == 0
                    && g_world.radar.bCraftOutOfFuel == 0 && g_world.radar.bTraceLost == 0
                    && !App_ScriptNormalAirCombatPairMatches(nTargetIdx, nTargetSeed,
                                                               bShipId, bCraftType)) {
                    return App_ScriptNormalAirCombatFail("firing_snapshot", "live_pair_changed_without_terminal_owner",
                                                         nPulses, nTargetIdx, nTargetSeed, bShipId);
                }
            }
        }
        if (g_world.radar.nTargetHp < nHpBeforePulse) {
            bSawDamage = g_world.radar.nTargetHp < nHpStart;
            if (pCurrentTarget != NULL && pCurrentTarget->nHp == g_world.radar.nTargetHp) {
                bSawPersistentHp = true;
            }
            printf("pocketufo: normal-air-combat evidence stage=damage pulse=%d hpBefore=%d hpAfter=%d itemHp=%d itemPresent=%d targetExpected=%d targetCurrent=%d seedExpected=%d seed=%d craftId=%u ammoLStart=%d ammoLNow=%d ammoRStart=%d ammoRNow=%d ammoSpent=%d hpWriteback=%d\n",
                   (int)nPulses, (int)nHpBeforePulse, (int)g_world.radar.nTargetHp,
                   pCurrentTarget != NULL ? (int)pCurrentTarget->nHp : -999999,
                   pCurrentTarget != NULL ? 1 : 0, (int)nTargetIdx,
                   (int)nCurrentTargetIdx, (int)nTargetSeed,
                   pCurrentTarget != NULL ? (int)pCurrentTarget->nSeed : -1,
                   (unsigned)bShipId, (int)nAmmoLStart,
                   (int)g_world.radar.nAmmoL, (int)nAmmoRStart,
                   (int)g_world.radar.nAmmoR, bSawAmmo ? 1 : 0,
                   bSawPersistentHp ? 1 : 0);
            if (!bDamageSnapshot) {
                App_ScriptDump("radar_combat_damage.ppm");
                printf("pocketufo: normal-air-combat snapshot stage=damage file=radar_combat_damage.ppm\n");
                bDamageSnapshot = true;
                nCurrentTargetIdx = App_ScriptNormalAirCombatFindTarget(nTargetSeed);
                pCurrentTarget = nCurrentTargetIdx >= 0
                    ? (const StrategItem *)g_world.strategList.apItems[nCurrentTargetIdx] : NULL;
                if (g_world.radar.nTargetHp < nHpStart) {
                    bSawDamage = true;
                    if (pCurrentTarget != NULL
                        && pCurrentTarget->nHp == g_world.radar.nTargetHp) {
                        bSawPersistentHp = true;
                    }
                }
                if (g_world.bRadarScreenActive != 0
                    && (g_world.geo.time.bPaused == 0 || g_world.geo.time.nTotalSec != nClockStart
                        || g_world.screen.nGeoscapeScreen != 0 || g_world.sModal.bOpen != 0)) {
                    return App_ScriptNormalAirCombatFail("damage_snapshot", "timer_or_screen_changed_after_paint",
                                                         nPulses, nTargetIdx, nTargetSeed, bShipId);
                }
                if (g_world.bRadarScreenActive != 0 && g_world.radar.bInterceptDone == 0
                    && g_world.radar.bCraftOutOfFuel == 0 && g_world.radar.bTraceLost == 0
                    && !App_ScriptNormalAirCombatPairMatches(nTargetIdx, nTargetSeed,
                                                               bShipId, bCraftType)) {
                    return App_ScriptNormalAirCombatFail("damage_snapshot", "live_pair_changed_without_terminal_owner",
                                                         nPulses, nTargetIdx, nTargetSeed, bShipId);
                }
            }
        }

        if ((g_world.radar.bInterceptDone != 0 || g_world.radar.bCraftOutOfFuel != 0
             || g_world.radar.bTraceLost != 0)
            && !bTerminalSnapshot) {
            nOutcomeTextRec = App_ScriptNormalAirCombatMessageRecord();
            App_ScriptNormalAirCombatObserve("terminal_detected", nPulses,
                                             nTargetIdx, nTargetSeed, bShipId);
            App_ScriptDump("radar_combat_terminal.ppm");
            printf("pocketufo: normal-air-combat snapshot stage=terminal file=radar_combat_terminal.ppm\n");
            App_ScriptNormalAirCombatObserve("terminal_after_snapshot", nPulses,
                                             nTargetIdx, nTargetSeed, bShipId);
            {
                const i32 nCurrentShipIdx = App_ScriptNormalAirCombatFindCraft(bShipId);
                const i32 nCurrentTargetIdx = App_ScriptNormalAirCombatFindTarget(nTargetSeed);
                const Ship *pCurrentShip = nCurrentShipIdx >= 0
                    ? g_world.apCraft[nCurrentShipIdx] : NULL;
                const StrategItem *pCurrentTarget = nCurrentTargetIdx >= 0
                    ? (const StrategItem *)g_world.strategList.apItems[nCurrentTargetIdx] : NULL;
                i32 nOutcomeKind = kOutcomeUnknown;

                if (g_world.radar.bTraceLost != 0 && g_world.radar.bInterceptDone == 0
                    && g_world.radar.bCraftOutOfFuel == 0
                    && nOutcomeTextRec == PUFO_RADAR_TEXT_TRACE_LOST) {
                    nOutcomeKind = kOutcomeTraceLoss;
                } else if (g_world.radar.bCraftOutOfFuel != 0
                           && g_world.radar.bInterceptDone == 0
                           && App_ScriptNormalLine0Flag() == PUFO_RADAR_MSG_SHIP_LOST) {
                    nOutcomeKind = kOutcomeCraftLoss;
                } else if (g_world.radar.bInterceptDone != 0 && pCurrentTarget != NULL
                           && pCurrentTarget->nType == 4 && pCurrentTarget->bActive != 0
                           && pCurrentTarget->nTimer == PUFO_RADAR_TIMER_LANDED
                           && nOutcomeTextRec == PUFO_RADAR_TEXT_TARGET_LANDED) {
                    nOutcomeKind = kOutcomeLandedSite;
                } else if (g_world.radar.bInterceptDone != 0 && pCurrentTarget != NULL
                           && pCurrentTarget->nType == 4 && pCurrentTarget->bActive != 0
                           && pCurrentTarget->nTimer == PUFO_RADAR_TIMER_LANDED
                           && nOutcomeTextRec == PUFO_RADAR_TEXT_TARGET_CRASHED) {
                    nOutcomeKind = kOutcomeCrashedSite;
                } else if (g_world.radar.bInterceptDone != 0 && pCurrentTarget == NULL
                           && g_world.radar.nTargetHp <= 0
                           && nOutcomeTextRec == PUFO_RADAR_TEXT_TARGET_DEAD) {
                    nOutcomeKind = kOutcomeDestroyed;
                } else if (g_world.radar.bInterceptDone != 0 && pCurrentTarget == NULL
                           && g_world.radar.nTargetHp > 0
                           && nOutcomeTextRec == PUFO_RADAR_TEXT_TARGET_CRASHED) {
                    nOutcomeKind = kOutcomeSeaRemoval;
                }
                printf("pocketufo: normal-air-combat outcome-observed pulse=%d kind=%d textRec=%d target=%d targetCurrent=%d seedExpected=%d seed=%d targetPresent=%d targetType=%d targetActive=%u targetTimer=%d targetHp=%d craft=%d craftPresent=%d craftId=%d craftType=%d craftAtBase=%u craftIntercept=%u craftTarget=%d outOfFuel=%u traceLost=%u flip=%u lineFlag=%d\n",
                       (int)nPulses, (int)nOutcomeKind, (int)nOutcomeTextRec,
                       (int)nTargetIdx, (int)nCurrentTargetIdx, (int)nTargetSeed,
                       pCurrentTarget != NULL ? (int)pCurrentTarget->nSeed : -1,
                       pCurrentTarget != NULL ? 1 : 0,
                       pCurrentTarget != NULL ? (int)pCurrentTarget->nType : -1,
                       pCurrentTarget != NULL ? (unsigned)pCurrentTarget->bActive : 255u,
                       pCurrentTarget != NULL ? (int)pCurrentTarget->nTimer : -999999,
                       pCurrentTarget != NULL ? (int)pCurrentTarget->nHp : -999999,
                       (int)nCurrentShipIdx, pCurrentShip != NULL ? 1 : 0,
                       pCurrentShip != NULL ? (int)pCurrentShip->bId : -1,
                       pCurrentShip != NULL ? (int)pCurrentShip->bCraftType : -1,
                       pCurrentShip != NULL ? (unsigned)pCurrentShip->bAtBase : 255u,
                       pCurrentShip != NULL ? (unsigned)pCurrentShip->bIntercepting : 255u,
                       pCurrentShip != NULL ? (int)pCurrentShip->nStrategIdx : -1,
                       (unsigned)g_world.radar.bCraftOutOfFuel,
                       (unsigned)g_world.radar.bTraceLost,
                       (unsigned)g_world.radar.bFlip,
                       (int)App_ScriptNormalLine0Flag());
            }
            bTerminalSnapshot = true;
        }

        if (g_world.bRadarScreenActive == 0) {
            if (!bTerminalSnapshot) {
                App_ScriptDump("radar_combat_terminal.ppm");
                printf("pocketufo: normal-air-combat snapshot stage=terminal file=radar_combat_terminal.ppm\n");
                App_ScriptNormalAirCombatObserve("terminal_after_snapshot", nPulses,
                                                 nTargetIdx, nTargetSeed, bShipId);
                bTerminalSnapshot = true;
            }
            App_ScriptDump("radar_combat_after_cleanup.ppm");
            printf("pocketufo: normal-air-combat snapshot stage=after_cleanup file=radar_combat_after_cleanup.ppm\n");
            App_ScriptNormalAirCombatObserve("after_cleanup", nPulses,
                                             nTargetIdx, nTargetSeed, bShipId);
            const i32 nCurrentHp = g_world.radar.nTargetHp;
            const i32 nLabelRecNow = App_ScriptNormalAirCombatMessageRecord();
            const i32 nMessageRec = nOutcomeTextRec >= 0 ? nOutcomeTextRec : nLabelRecNow;
            const i32 nFinalShipIdx = App_ScriptNormalAirCombatFindCraft(bShipId);
            const i32 nFinalTargetIdx = App_ScriptNormalAirCombatFindTarget(nTargetSeed);
            const Ship *pFinalShip = nFinalShipIdx >= 0 ? g_world.apCraft[nFinalShipIdx] : NULL;
            const StrategItem *pFinalTarget = nFinalTargetIdx >= 0
                ? (const StrategItem *)g_world.strategList.apItems[nFinalTargetIdx] : NULL;
            bool bAmmoWriteback = false;
            const char *pszOutcome = "unknown";

            if (g_world.msgLines.nLineCount > 0 && App_ScriptNormalLine0Flag() == PUFO_RADAR_MSG_SHIP_LOST) {
                pszOutcome = "craft_loss";
            } else if (g_world.radar.bCraftOutOfFuel != 0) {
                pszOutcome = "craft_loss";
            } else if (g_world.radar.bTraceLost != 0
                       && nMessageRec == PUFO_RADAR_TEXT_TRACE_LOST) {
                pszOutcome = "trace_loss_retreat";
            } else if (g_world.radar.bInterceptDone != 0 && pFinalTarget != NULL
                       && pFinalTarget->nType == 4 && pFinalTarget->bActive != 0
                       && pFinalTarget->nTimer == PUFO_RADAR_TIMER_LANDED
                       && nMessageRec == PUFO_RADAR_TEXT_TARGET_LANDED) {
                pszOutcome = "landed_site";
            } else if (g_world.radar.bInterceptDone != 0 && pFinalTarget != NULL
                       && pFinalTarget->nType == 4 && pFinalTarget->bActive != 0
                       && pFinalTarget->nTimer == PUFO_RADAR_TIMER_LANDED
                       && nMessageRec == PUFO_RADAR_TEXT_TARGET_CRASHED) {
                pszOutcome = "crashed_site";
            } else if (g_world.radar.bInterceptDone != 0 && pFinalTarget == NULL
                       && nCurrentHp <= 0 && nMessageRec == PUFO_RADAR_TEXT_TARGET_DEAD) {
                pszOutcome = "destroyed_target";
            } else if (g_world.radar.bInterceptDone != 0 && pFinalTarget == NULL
                       && nCurrentHp > 0 && nMessageRec == PUFO_RADAR_TEXT_TARGET_CRASHED) {
                pszOutcome = "sea_removal";
            }
            if (pFinalShip != NULL) {
                bAmmoWriteback = App_ScriptNormalAirCombatAmmoWritebackMatches(
                    pFinalShip, g_world.radar.nAmmoL, g_world.radar.nAmmoR);
            }
            printf("pocketufo: normal-air-combat cleanup pulses=%d sec=%d radarActive=%u paused=%u done=%u outOfFuel=%u traceLost=%u flip=%u lineCount=%u lineFlag=%d shortfall=%u messageRec=%d labelRecNow=%d hp=%d hpMax=%d targetExpected=%d targetCurrent=%d targetPresent=%d targetSeed=%d targetType=%d targetActive=%u targetVisible=%u targetTimer=%d targetHp=%d craftExpectedId=%u craftCurrent=%d craftPresent=%d craftId=%d craftAtBase=%u craftIntercept=%u craftTarget=%d fuel=%d range=%u sessionAmmoL=%d sessionAmmoR=%d craftAmmoL=%d craftAmmoR=%d ammoWriteback=%d ammoSpent=%d damage=%d hpWriteback=%d\n",
                   (int)nPulses, (int)g_world.geo.time.nTotalSec,
                   (unsigned)g_world.bRadarScreenActive, (unsigned)g_world.geo.time.bPaused,
                   (unsigned)g_world.radar.bInterceptDone, (unsigned)g_world.radar.bCraftOutOfFuel,
                   (unsigned)g_world.radar.bTraceLost, (unsigned)g_world.radar.bFlip,
                   (unsigned)g_world.msgLines.nLineCount, (int)App_ScriptNormalLine0Flag(),
                   (unsigned)g_world.nShortfall1870C8, (int)nMessageRec, (int)nLabelRecNow,
                   (int)nCurrentHp, (int)g_world.radar.nTargetHpMax,
                   (int)nTargetIdx, (int)nFinalTargetIdx, pFinalTarget != NULL ? 1 : 0,
                   pFinalTarget != NULL ? (int)pFinalTarget->nSeed : -1,
                   pFinalTarget != NULL ? (int)pFinalTarget->nType : -1,
                   pFinalTarget != NULL ? (unsigned)pFinalTarget->bActive : 255u,
                   pFinalTarget != NULL ? (unsigned)pFinalTarget->bVisible : 255u,
                   pFinalTarget != NULL ? (int)pFinalTarget->nTimer : -999999,
                   pFinalTarget != NULL ? (int)pFinalTarget->nHp : -999999,
                   (unsigned)bShipId, (int)nFinalShipIdx, pFinalShip != NULL ? 1 : 0,
                   pFinalShip != NULL ? (int)pFinalShip->bId : -1,
                   pFinalShip != NULL ? (unsigned)pFinalShip->bAtBase : 255u,
                   pFinalShip != NULL ? (unsigned)pFinalShip->bIntercepting : 255u,
                   pFinalShip != NULL ? (int)pFinalShip->nStrategIdx : -1,
                   pFinalShip != NULL ? (int)pFinalShip->nFuel : -1,
                   pFinalShip != NULL ? (unsigned)pFinalShip->uRangeRemain : 0u,
                   (int)g_world.radar.nAmmoL, (int)g_world.radar.nAmmoR,
                   pFinalShip != NULL ? App_ScriptNormalAirAmmo(pFinalShip, pFinalShip->bWeaponL) : -1,
                   pFinalShip != NULL ? App_ScriptNormalAirAmmo(pFinalShip, pFinalShip->bWeaponR) : -1,
                   bAmmoWriteback ? 1 : 0, bSawAmmo ? 1 : 0, bSawDamage ? 1 : 0,
                   bSawPersistentHp ? 1 : 0);
            if (strcmp(pszOutcome, "landed_site") == 0
                || strcmp(pszOutcome, "crashed_site") == 0
                || strcmp(pszOutcome, "destroyed_target") == 0
                || strcmp(pszOutcome, "sea_removal") == 0) {
                if (!bSawAmmo || !bSawDamage || !bSawPersistentHp || !bAmmoWriteback
                    || g_world.bRadarScreenActive != 0 || g_world.radar.bInterceptDone == 0
                    || g_world.radar.bCraftOutOfFuel != 0 || g_world.radar.bTraceLost != 0
                    || g_world.geo.time.bPaused != 0 || g_world.sModal.bOpen != 0
                    || g_world.geo.time.nTotalSec != nClockStart) {
                    return App_ScriptNormalAirCombatFail("terminal", "damage_or_persistent_writeback_not_verified",
                                                         nPulses, nTargetIdx, nTargetSeed, bShipId);
                }
                if ((strcmp(pszOutcome, "landed_site") == 0
                     || strcmp(pszOutcome, "crashed_site") == 0)
                    && pFinalTarget != NULL && pFinalTarget->nType == 4
                    && pFinalTarget->bActive != 0) {
                    g_bNormalAirGroundSiteReady = true;
                    g_nNormalAirGroundSiteSeed = pFinalTarget->nSeed;
                }
                printf("pocketufo: normal-air-combat PASS stage=terminal outcome=%s pulses=%d clock=%d target=%d seed=%d craft=%d craftId=%u hpStart=%d hpEnd=%d ammoLStart=%d ammoL=%d ammoRStart=%d ammoR=%d\n",
                       pszOutcome, (int)nPulses, (int)g_world.geo.time.nTotalSec,
                       (int)nTargetIdx, (int)nTargetSeed, (int)nFinalShipIdx,
                       (unsigned)bShipId, (int)nHpStart, (int)nCurrentHp,
                       (int)nAmmoLStart, (int)g_world.radar.nAmmoL,
                       (int)nAmmoRStart, (int)g_world.radar.nAmmoR);
                fflush(stdout);
                return true;
            }
            return App_ScriptNormalAirCombatFail("terminal", pszOutcome,
                                                 nPulses, nTargetIdx, nTargetSeed, bShipId);
        }

        if (g_world.bRadarScreenActive != 0
            && App_ScriptNormalLine0Flag() != 0) {
            return App_ScriptNormalAirCombatFail("combat", "unexpected_line_flag",
                                                 nPulses, nTargetIdx, nTargetSeed, bShipId);
        }
        if (g_world.bRadarScreenActive != 0 && g_world.sModal.bOpen != 0) {
            return App_ScriptNormalAirCombatFail("combat", "unexpected_modal",
                                                 nPulses, nTargetIdx, nTargetSeed, bShipId);
        }

        if (!bSwClicked && g_world.bRadarScreenActive != 0
            && g_world.radar.bInterceptDone == 0 && g_world.radar.bCraftOutOfFuel == 0
            && g_world.radar.bTraceLost == 0
            && g_world.radar.nAmmoR == 0 && g_world.radar.nAmmoL > 0) {
            const i32 nCurrentShipIdx = App_ScriptNormalAirCombatFindCraft(bShipId);
            const Ship *pCurrentShip = nCurrentShipIdx >= 0 ? g_world.apCraft[nCurrentShipIdx] : NULL;
            if (pCurrentShip != NULL && pCurrentShip->bWeaponR == 26) {
                UiControl *pAttackMin = g_world.radar.ui.apCtl[2];
                const i32 nX = pAttackMin != NULL
                    ? (i32)pAttackMin->x + (i32)pAttackMin->uWidth / 2 : -1;
                const i32 nY = pAttackMin != NULL
                    ? (i32)pAttackMin->y + (i32)pAttackMin->uHeight / 2 : -1;
                const i32 nRangeBefore = g_world.radar.nRangeTarget;
                if (!App_ScriptNormalAirCombatPairMatches(nTargetIdx, nTargetSeed,
                                                          bShipId, bCraftType)
                    || !App_ScriptNormalClickControl(pAttackMin)) {
                    return App_ScriptNormalAirCombatFail("attack_min", "live_SW_control_not_ready",
                                                         nPulses, nTargetIdx, nTargetSeed, bShipId);
                }
                bSwClicked = true;
                printf("pocketufo: normal-air-combat input control=attack_min button=SW index=2 center=%d/%d rangeBefore=%d rangeAfter=%d ammoL=%d ammoR=%d messageRec=%d\n",
                       (int)nX, (int)nY, (int)nRangeBefore,
                       (int)g_world.radar.nRangeTarget,
                       (int)g_world.radar.nAmmoL, (int)g_world.radar.nAmmoR,
                       (int)App_ScriptNormalAirCombatMessageRecord());
                if (g_world.bRadarScreenActive == 0 || g_world.geo.time.bPaused == 0
                    || App_ScriptNormalAirCombatMessageRecord() != PUFO_RADAR_TEXT_CURSOR_SW
                    || !App_ScriptNormalAirCombatPairMatches(nTargetIdx, nTargetSeed,
                                                               bShipId, bCraftType)) {
                    return App_ScriptNormalAirCombatFail("attack_min", "SW_input_changed_radar_state",
                                                         nPulses, nTargetIdx, nTargetSeed, bShipId);
                }
            }
        }

        if (nPulses % 128 == 0 && g_world.bRadarScreenActive != 0) {
            App_ScriptNormalAirCombatObserve("progress", nPulses,
                                             nTargetIdx, nTargetSeed, bShipId);
        }
    }

    if (!bSawDamage) {
        return App_ScriptNormalAirCombatFail("combat", "no_damage_after_4096_radar_pulses",
                                             kMaxCombatPulses, nTargetIdx, nTargetSeed, bShipId);
    }
    return App_ScriptNormalAirCombatFail("combat", "no_natural_terminal_after_4096_radar_pulses",
                                         kMaxCombatPulses, nTargetIdx, nTargetSeed, bShipId);
}

/* n firings of each of the three timer bodies WndProc dispatch (0xC352 anim, 0xC353 pulse,
 * 0xC354 anim progress), each followed by a paint.
 *
 * The paint belongs inside the loop, and that is the whole point of the shape. WndProc's timers and its
 * WM_PAINT arrive interleaved, and the first paint of a screen is what STARTS its panel animation:
 * Ui_AnimExpandBlit @ 0x95bf0 tests byte_187080 (0x95C24) and on the first call sets it and zeroes the
 * progress byte 0x187058 (0x95C14..0x95C20). Stepping the progress first therefore throws every step away
 * -- the game's own first call resets it to 0 -- and a dump taken after such a run catches the screen on
 * the frame where nothing has opened yet. That is why the new-game picture used to show the world map with
 * no bottom panel at all: the new-base scan panel (0x67450..0x67494, and behind it the scan bars, the
 * readout lines and the TEXTS[1042]/[1136]/[1137] labels) only runs once Ui_AnimExpandBlit returns true,
 * i.e. once the progress byte has climbed to the point where the clamped rect equals the full rect
 * (0x95D60..0x95D74 sets it to 100, which mode 3 reaches at progress 5 for the 236-wide panel). */
static void App_ScriptTicks(i32 nCount)
{
    for (i32 i = 0; i < nCount; ++i) {
        App_TickPulse();
        App_TickAnimProgress();
        App_TickAnim();
        App_DrawFrame();
    }
}

/* SHELL SCAFFOLDING, not original behaviour, and a recorded deviation: the same three timer bodies with
 * the paint dropped, so a many-day geoscape sweep costs seconds instead of minutes. Nothing in the three
 * bodies reads a picture; the paint is what starts and advances the panel animations (Ui_AnimExpandBlit
 * @ 0x95bf0 and the progress byte 0x187058), so a step that skips it cannot open or advance a panel --
 * which is why this is only used to run clock time, and any press sequence still runs through App_ScriptTicks
 * or App_PointerLogical with a real frame. Proven a pure accelerator: 60000 fast steps reproduce the painted
 * 60000-step sweep exactly -- the same clock reached, the same contact detected at 357300 s, the same
 * `[0] flag 10 slots 4` line (PLAN 122a, Part 154). */
static void App_ScriptFastTicks(i32 nCount)
{
    for (i32 i = 0; i < nCount; ++i) {
        App_TickPulse();
        App_TickAnimProgress();
        App_TickAnim();
    }
}

/* n firings of 0xC350 exactly as PocketUFO_WndProc dispatches them (pufo_app.c's own
 * WM_TIMER arm at the `wParam != 50000` gate 0xA08B0): the speed decides how many
 * GameTick_OnTimer calls one firing runs (Timer_TickBySpeed 0xA08C0..0xA0934), and each
 * firing is followed by a paint, which is how the live window path interleaves them.
 * SHELL SCAFFOLDING, not original behaviour: in the original only WM_TIMER reaches this. */
static void App_ScriptGameTick(i32 nCount)
{
    for (i32 i = 0; i < nCount; ++i) {
        Timer_TickBySpeed((i32)g_world.gameClock.nSpeed, App_GameTickOnce, NULL);
        App_DrawFrame();
    }
}

/* SHELL SCAFFOLDING (Part 168), not original behaviour: put one armour object on the selected base's
 * shelf and wear it, walking the soldier screen's own control handlers -- SoldierScreen_Toggle @ 0x64af0
 * (nav slot 2, 0x67B8C), Ui_SetTransferFlag @ 0x619a8 (find button 3), SoldierScreen_SelectListed @
 * 0x64a9c, SoldierScreen_EquipSelected @ 0x619cc. Only the stocking is scaffolding: main.dat stocks no
 * armour at any base (slots 122/123/124 are 0 everywhere) and the economy path to a suit is research
 * plus manufacture, which these flows do not run. The equip itself is the game's own chain. */
static void App_ScriptEquipArmour(void)
{
    const i32 nSuitSlot = 123;   /* "Энергоскафандр" / Power suit: the record Unit_ResetBattleState reads as type 2 */
    BaseRec  *pBase;
    const Soldier *pSol;
    i32 nRow = -1;

    if (g_world.nSelectedBase < 0 || g_world.nSelectedBase >= PUFO_FAC_BASE_COUNT
        || g_world.apBases[g_world.nSelectedBase] == NULL
        || g_assets.apObjects == NULL || g_assets.apObjects[nSuitSlot] == NULL) {
        printf("pocketufo: equiparmor: no selected base or no armour record\n");
        fflush(stdout);
        return;
    }
    pBase = g_world.apBases[g_world.nSelectedBase];
    printf("pocketufo: equiparmor: before -- ui %d, screen %d, sol screen %d, nav slots %d/%d, panel slots "
           "%d/%d, find slots %d/%d/%d, names panel %d, equip panel %d\n",
           (int)g_world.bGeoscapeUi, (int)g_world.screen.nGeoscapeScreen, (int)g_world.bSolScreenOpen,
           (g_world.nav.apNav[0] != NULL), (g_world.nav.apNav[2] != NULL),
           (g_world.nav.apPanel[0] != NULL), (g_world.nav.apPanel[1] != NULL),
           (g_world.nav.apFind[0] != NULL), (g_world.nav.apFind[1] != NULL), (g_world.nav.apFind[2] != NULL),
           (g_world.pSolPanelNames != NULL), (g_world.pSolPanelEquip != NULL));
    BaseRec_AddItemQty(pBase, g_assets.apObjects[nSuitSlot], 1);   /* 0x556e4 */

    /* Closing the base view released its nav controls. Reopen that view before
     * using the soldier button, as a normal visit from the geoscape does. */
    if (g_world.ui.apRootCtl[10] != NULL) {
        g_world.ui.apRootCtl[10]->pfnClick(); /* Base_EnterBuildMode @ 0x84e20 */
    }
    if (g_world.nav.apNav[2] != NULL) {
        g_world.nav.apNav[2]->pfnClick();       /* SoldierScreen_Toggle @ 0x64af0 */
    }
    if (g_world.nav.apFind[2] != NULL) {
        g_world.nav.apFind[2]->pfnClick();      /* Ui_SetTransferFlag @ 0x619a8 */
    }
    printf("pocketufo: equiparmor: after the toggle and the transfer button -- ui %d, screen %d, sol screen "
           "%d, cursor %d, transfer %d, equip panel %d rows %d\n",
           (int)g_world.bGeoscapeUi, (int)g_world.screen.nGeoscapeScreen, (int)g_world.bSolScreenOpen,
           g_world.nSoldierCursor, (i32)g_world.bTransferFlag,
           (g_world.pSolPanelEquip != NULL) ? 1 : 0,
           (g_world.pSolPanelEquip != NULL) ? g_world.pSolPanelEquip->nLineCount : -1);

    if (g_world.pSolPanelNames != NULL && g_world.solEnv.pfnSelectThunk != NULL) {
        g_world.pSolPanelNames->nUnk3032 = 0;
        g_world.solEnv.pfnSelectThunk();        /* SoldierScreen_SelectListed @ 0x64a9c */
    }
    (void)App_DrawFrame(); /* the detail draw fills the equipment rows from stock */
    if (g_world.pSolPanelEquip != NULL) {
        for (i32 i = 0; i < g_world.pSolPanelEquip->nLineCount; ++i) {
            if (g_world.pSolPanelEquip->anLineAux[i] == 2) {
                nRow = i;   /* the armour row the panel built from the base stock (0x62DC0..0x63058) */
                break;
            }
        }
        if (nRow >= 0 && g_world.solEnv.pfnEquipThunk != NULL) {
            g_world.pSolPanelEquip->nUnk3032 = nRow;
            g_world.solEnv.pfnEquipThunk();     /* SoldierScreen_EquipSelected @ 0x619cc */
        }
    }

    pSol = (g_world.nSoldierCursor >= 0 && g_world.nSoldierCursor < g_world.nSoldierCount)
                 ? g_world.apSoldiers[g_world.nSoldierCursor] : NULL;
    printf("pocketufo: equiparmor: base %d, screen %d, transfer %d, panel rows %d, equip row %d, "
           "soldier \"%s\" type %u, suit stock %d, stores %d\n",
           (i32)g_world.nSelectedBase, (i32)g_world.screen.nGeoscapeScreen, (i32)g_world.bTransferFlag,
           (g_world.pSolPanelEquip != NULL) ? g_world.pSolPanelEquip->nLineCount : -1, nRow,
           (pSol != NULL) ? pSol->szName : "(none)",
           (pSol != NULL) ? (u32)pSol->nUnitType : 0u,
           (i32)g_assets.apObjects[nSuitSlot]->auPerBaseVal[g_world.nSelectedBase],
           (i32)pBase->nStoresTotal);
    fflush(stdout);
}

/* SHELL SCAFFOLDING, not original behaviour: the live mission's own cells, printed. The
 * mission is the one state the --script path cannot be trusted about from pictures alone
 * -- the queue, the phase machine and the rosters move every tick and no picture shows a
 * time unit. The fields are the originals' (GameMap @ pufo_mapctx.h, TactState @
 * pufo_tactstate.h, Soldier @ pufo_unit.h). */
/* Read-only signature of the terrain IDs emitted by Save_WriteMap. */
static u32 App_TerrainDigestByte(u32 nHash, u8 nByte)
{
    return (nHash ^ (u32)nByte) * 16777619u;
}

static u32 App_TerrainDigestWord(u32 nHash, u32 nWord)
{
    for (u32 i = 0; i < 4; ++i) {
        nHash = App_TerrainDigestByte(nHash, (u8)(nWord & 0xFFu));
        nWord >>= 8;
    }
    return nHash;
}

static u32 App_TactTerrainDigest(const GameMap *pMap)
{
    u32 nHash = App_TerrainDigestByte(2166136261u, pMap->geom.nDimX);
    nHash = App_TerrainDigestByte(nHash, pMap->geom.nDimY);
    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nRow = 0; nRow < PUFO_CELL_SIZE * (i32)pMap->geom.nDimY; ++nRow) {
            for (i32 nCol = 0; nCol < PUFO_CELL_SIZE * (i32)pMap->geom.nDimX; ++nCol) {
                const MapCell *pCell = &pMap->aCells[nLayer][nRow][nCol];
                const Mc1Rec *const apSpr[4] = {
                    pCell->pFloorSpr, pCell->pWallSprA, pCell->pWallSprB, pCell->pCeilSpr
                };
                for (u32 nWhich = 0; nWhich < 4; ++nWhich) {
                    nHash = App_TerrainDigestWord(nHash, apSpr[nWhich] != NULL
                        ? (u32)apSpr[nWhich]->nGlobalIdx : 0x7FFFu);
                }
            }
        }
    }
    return nHash;
}

static void App_ScriptTact(void)
{
    const GameMap   *pMap = g_world.pTactMap;
    const TactState *pQ   = g_world.pTactState;
    const Soldier   *pSel;

    if (pMap == NULL || pQ == NULL) {
        printf("pocketufo: tact: no live mission\n");
        fflush(stdout);
        return;
    }
    pSel = pMap->pSelUnit;
    printf("pocketufo: tact: busy %d, phase %d, enemy turn %d, player input %d, battle ended %d, "
           "ui screen %d, latch %d, mode %d, reserve %d\n",
           (int)pMap->tact.bBusy, (int)pMap->tact.nPhase, (int)g_world.bMapEnemyTurnActive,
           (int)g_world.bMapPlayerInput, (int)g_world.bMapBattleEnded, (int)g_world.nUiScreen,
           (int)pMap->bActionLatch, (int)g_world.nMapActionMode, (int)g_world.nMapEnemyReserveCount);
    printf("  queue: len %d, cursor %d, active %d, flag0 %d, flag1 %d, phase max %d, phase tick %d\n",
           (int)pQ->nQueueLen, (int)pQ->nQueueCursor, (int)pQ->bQueueActive,
           (int)pQ->bFlag0, (int)pQ->bFlag1, (int)pQ->nPhaseMax, (int)pQ->nPhaseTick);
    printf("pocketufo: tact view: dims %u/%u scroll %d/%d cursor %d/%d layer %u\n",
           (unsigned)pMap->geom.nDimX, (unsigned)pMap->geom.nDimY,
           (int)g_world.tactEnv.focus.nCamScrollX, (int)g_world.tactEnv.focus.nCamScrollY,
           (int)g_world.tactEnv.cursor.nX, (int)g_world.tactEnv.cursor.nY,
           (unsigned)g_world.tactEnv.cursor.nFlags);
    printf("pocketufo: tact terrain: dims %u/%u ids %08X\n",
           (unsigned)pMap->geom.nDimX, (unsigned)pMap->geom.nDimY,
           (unsigned)App_TactTerrainDigest(pMap));
    printf("  cursor %d/%d layer %d; selected at %d/%d/%d, uTu %u, hp %u, status %u, facing %u, kills %u\n",
           (int)g_world.tactEnv.cursor.nX, (int)g_world.tactEnv.cursor.nY,
           (int)g_world.tactEnv.cursor.nFlags,
           pSel != NULL && pSel->pCell != NULL ? pSel->pCell->nX : -999,
           pSel != NULL && pSel->pCell != NULL ? pSel->pCell->nY : -999,
           pSel != NULL && pSel->pCell != NULL ? pSel->pCell->nZ : -999,
           pSel != NULL ? (unsigned)pSel->uTu : 0u,
           pSel != NULL ? (unsigned)pSel->uHp : 0u,
           pSel != NULL ? (unsigned)pSel->bStatus : 0u,
           pSel != NULL ? (unsigned)pSel->bFacing : 0u,
           pSel != NULL ? (unsigned)pSel->uKills : 0u);
    for (i32 i = 0; i < (i32)pMap->nXcomCount; ++i) {
        const Soldier *pU = pMap->apXcomUnits[i];

        if (pU == NULL) {
            continue;
        }
        printf("  xcom %d: at %d/%d/%d, uTu %u, hp %u, status %u, large %d\n", i,
               pU->pCell != NULL ? pU->pCell->nX : -999, pU->pCell != NULL ? pU->pCell->nY : -999,
               pU->pCell != NULL ? pU->pCell->nZ : -999,
               (unsigned)pU->uTu, (unsigned)pU->uHp, (unsigned)pU->bStatus,
               Unit_bIsLarge(pU) ? 1 : 0);
    }
    for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
        const Soldier *pE = pMap->apEnemyUnits[i];

        if (pE == NULL) {
            continue;
        }
        printf("  enemy %d: type %u at %d/%d/%d, uTu %u, hp %u, status %u, aiTick %d, aiDone %d, large %d\n",
               i, (unsigned)pE->nUnitType,
               pE->pCell != NULL ? pE->pCell->nX : -999, pE->pCell != NULL ? pE->pCell->nY : -999,
               pE->pCell != NULL ? pE->pCell->nZ : -999,
               (unsigned)pE->uTu, (unsigned)pE->uHp, (unsigned)pE->bStatus,
               pE->nAiTick, (int)pE->bAiDone, Unit_bIsLarge(pE) ? 1 : 0);
    }
    fflush(stdout);
}

/* Campaign checks use the app's pointer and key dispatchers. These helpers only
 * place legal inputs and model prerequisites; they do not change combat results. */
static i32 App_ScriptCampaignObjectSlot(const ObjectRecFull *pObj)
{
    if (pObj == NULL || g_assets.apObjects == NULL) {
        return -1;
    }
    for (i32 i = 0; i < (i32)g_assets.nObjectCount; ++i) {
        if (g_assets.apObjects[i] == pObj) {
            return i;
        }
    }
    return -1;
}

static i32 App_ScriptCampaignLiveLatch(void)
{
    const GameMap *pMap = g_world.pTactMap;

    return (pMap != NULL) ? (i32)pMap->bActionLatch : -1;
}

static bool App_ScriptCampaignClickBank(i32 nWidget)
{
    UiControl *pCtl;
    UiControl *pInputCtl;
    const UiWidgetBank *pWorldBank = (g_world.pFrameHooks != NULL) ? g_world.pFrameHooks->pBank : NULL;
    i32 nX, nY;
    u8 bPressed;

    if (nWidget < 0 || nWidget >= PUFO_PBANK_WIDGET_COUNT) {
        printf("pocketufo: campaign input: FAIL widget %d is out of range\n", nWidget);
        fflush(stdout);
        return false;
    }
    pCtl = &g_bank.aCtl[nWidget];
    pInputCtl = (g_pointerWorld.pBank != NULL) ? &g_pointerWorld.pBank->aCtl[nWidget] : NULL;
    if (pCtl->pfnClick == NULL) {
        printf("pocketufo: campaign input: FAIL widget %d is not live\n", (int)nWidget);
        fflush(stdout);
        return false;
    }
    (void)App_DrawFrame();
    nX = (i32)pCtl->x + (i32)pCtl->uWidth / 2;
    nY = (i32)pCtl->y + (i32)pCtl->uHeight / 2;
    printf("pocketufo: campaign pointer trace widget %d before: appBank %p worldBank %p inputBank %p same %d "
           "rect %d/%d/%u/%u enabled %u mode %u state %u click %d inputCtl %u/%u/%u "
           "screens %d/%d flags %d/%d player %d/%d "
           "latch %d/%u HUD %d/%d look %d/%d/%u/%u enabled %u point %d/%d\n",
           (int)nWidget, (void *)&g_bank, (void *)pWorldBank, (void *)g_pointerWorld.pBank,
           pWorldBank == &g_bank && g_pointerWorld.pBank == &g_bank,
           (int)pCtl->x, (int)pCtl->y, (unsigned)pCtl->uWidth, (unsigned)pCtl->uHeight,
           (unsigned)pCtl->bEnabled, (unsigned)pCtl->nMode, (unsigned)pCtl->nState,
           pCtl->pfnClick != NULL,
           (pInputCtl != NULL) ? (unsigned)pInputCtl->bEnabled : 0u,
           (pInputCtl != NULL) ? (unsigned)pInputCtl->nMode : 0u,
           (pInputCtl != NULL) ? (unsigned)pInputCtl->nState : 0u,
           (int)g_world.nUiScreen, (int)g_world.sTactInput.nUiScreen,
           (int)g_world.bTactFlag, (int)g_world.sTactInput.bTactFlag,
           (int)g_world.bMapPlayerInput, (int)g_world.sTactInput.bPlayerInputOn,
           App_ScriptCampaignLiveLatch(), (unsigned)g_world.sTactInput.nActionLatch,
           g_world.apTactPads[PUFO_UIPAD_HUD] != NULL
               ? (int)g_world.apTactPads[PUFO_UIPAD_HUD]->nOriginY : -1,
           g_world.apTactPads[PUFO_UIPAD_HUD] != NULL
               ? (int)g_world.apTactPads[PUFO_UIPAD_HUD]->uHeight : -1,
           (int)g_world.sTactLookCtl.x, (int)g_world.sTactLookCtl.y,
           (unsigned)g_world.sTactLookCtl.uWidth, (unsigned)g_world.sTactLookCtl.uHeight,
           (unsigned)g_world.sTactLookCtl.bEnabled, (int)nX, (int)nY);
    fflush(stdout);
    App_ScriptPointerLogical(nX, nY, true);
    bPressed = pCtl->nState;
    printf("pocketufo: campaign pointer trace widget %d after down: app/input state %u/%u "
           "world/input screen %d/%d "
           "player %d/%d latch %d/%u pointer %d/%d\n",
           (int)nWidget, (unsigned)pCtl->nState,
           (pInputCtl != NULL) ? (unsigned)pInputCtl->nState : 0u,
           (int)g_world.nUiScreen, (int)g_world.sTactInput.nUiScreen,
           (int)g_world.bMapPlayerInput, (int)g_world.sTactInput.bPlayerInputOn,
           App_ScriptCampaignLiveLatch(), (unsigned)g_world.sTactInput.nActionLatch,
           g_pointerWorld.pBank != NULL ? (int)g_pointerWorld.pBank->nPointerX : -1,
           g_pointerWorld.pBank != NULL ? (int)g_pointerWorld.pBank->nPointerY : -1);
    fflush(stdout);
    App_DrawFrame();
    printf("pocketufo: campaign pointer trace widget %d after down draw: app/input state %u/%u "
           "world/input screen %d/%d\n",
           (int)nWidget, (unsigned)pCtl->nState,
           (pInputCtl != NULL) ? (unsigned)pInputCtl->nState : 0u,
           (int)g_world.nUiScreen, (int)g_world.sTactInput.nUiScreen);
    fflush(stdout);
    App_OnPointerUp();
    printf("pocketufo: campaign pointer trace widget %d after up: press %u app/input state %u/%u "
           "world/input screen %d/%d "
           "player %d/%d latch %d/%u\n",
           (int)nWidget, (unsigned)bPressed, (unsigned)pCtl->nState,
           (pInputCtl != NULL) ? (unsigned)pInputCtl->nState : 0u,
           (int)g_world.nUiScreen, (int)g_world.sTactInput.nUiScreen,
           (int)g_world.bMapPlayerInput, (int)g_world.sTactInput.bPlayerInputOn,
           App_ScriptCampaignLiveLatch(), (unsigned)g_world.sTactInput.nActionLatch);
    fflush(stdout);
    (void)App_DrawFrame();
    printf("pocketufo: campaign pointer trace widget %d after draw: app/input state %u/%u "
           "world/input screen %d/%d "
           "player %d/%d latch %d/%u\n",
           (int)nWidget, (unsigned)pCtl->nState,
           (pInputCtl != NULL) ? (unsigned)pInputCtl->nState : 0u,
           (int)g_world.nUiScreen, (int)g_world.sTactInput.nUiScreen,
           (int)g_world.bMapPlayerInput, (int)g_world.sTactInput.bPlayerInputOn,
           App_ScriptCampaignLiveLatch(), (unsigned)g_world.sTactInput.nActionLatch);
    printf("pocketufo: campaign input: pointer widget %d at %d/%d (pressed %u, released %u)\n",
           (int)nWidget, (int)nX, (int)nY, (unsigned)bPressed, (unsigned)pCtl->nState);
    fflush(stdout);
    return bPressed != 0;
}

static bool App_ScriptCampaignKey(PufoKeySlot eKey)
{
    static const char *const apszName[PUFO_KEY_COUNT] = {
        "up", "down", "left", "right", "action", "secondary", "confirm", "escape"
    };

    if ((i32)eKey < 0 || (i32)eKey >= (i32)PUFO_KEY_COUNT || g_hWndMain == NULL) {
        printf("pocketufo: campaign input: FAIL key slot %d is not live\n", (int)eKey);
        fflush(stdout);
        return false;
    }
    (void)PocketUFO_WndProc(g_hWndMain, WM_KEYDOWN, g_anKeyMap[(i32)eKey], 0);
    (void)PocketUFO_WndProc(g_hWndMain, WM_KEYUP, g_anKeyMap[(i32)eKey], 0);
    printf("pocketufo: campaign input: key %s\n", apszName[(i32)eKey]);
    fflush(stdout);
    return true;
}

static MapCell *App_ScriptCampaignCellAt(i32 nX, i32 nY, i32 nZ)
{
    i32 nIndex;

    if (nX < 0 || nX >= PUFO_MAP_CELL_DIM || nY < 0 || nY >= PUFO_MAP_CELL_DIM
        || nZ < 0 || nZ >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    nIndex = PUFO_TERRAIN_CELLS_PER_LAYER * nZ + PUFO_MAP_CELL_DIM * nY + nX;
    return g_world.apTactCells[nIndex];
}

static i32 App_ScriptCampaignTactCellSlot(const MapCell *pWantedCell)
{
    i32 nFound = -1;

    if (pWantedCell == NULL) { return -1; }
    for (i32 i = 0; i < PUFO_TERRAIN_CELL_SLOTS; ++i) {
        if (g_world.apTactCells[i] != pWantedCell) { continue; }
        if (nFound >= 0) { return -2; }
        nFound = i;
    }
    return nFound;
}

/* A proximity detonation unlinks its item without keeping it in the cell list.
 * Hold an integer address token across that tick and compare it only with live
 * list entries; do not read through the old TactItem pointer afterward. */
static MapCell *App_ScriptCampaignFindItemCellKey(uintptr_t nItemKey,
                                                  const ObjectRecFull *pExpectedObj)
{
    if (nItemKey == 0 || pExpectedObj == NULL) {
        return NULL;
    }
    for (i32 i = 0; i < PUFO_TERRAIN_CELL_SLOTS; ++i) {
        MapCell *pCell = g_world.apTactCells[i];

        for (TactItem *pItem = (pCell != NULL) ? pCell->pItemList : NULL;
             pItem != NULL; pItem = pItem->pNext) {
            if ((uintptr_t)(void *)pItem == nItemKey && pItem->pObj == pExpectedObj) {
                return pCell;
            }
        }
    }
    return NULL;
}

static i32 App_ScriptCampaignFindPrimedGrenades(const ObjectRecFull *pGrenadeObj,
                                                uintptr_t *pnItemKey, MapCell **ppCell)
{
    i32 nCount = 0;

    if (pnItemKey != NULL) { *pnItemKey = 0; }
    if (ppCell != NULL) { *ppCell = NULL; }
    if (pGrenadeObj == NULL) {
        return 0;
    }
    for (i32 i = 0; i < PUFO_TERRAIN_CELL_SLOTS; ++i) {
        MapCell *pCell = g_world.apTactCells[i];

        for (TactItem *pItem = (pCell != NULL) ? pCell->pItemList : NULL;
             pItem != NULL; pItem = pItem->pNext) {
            if (pItem->pObj == pGrenadeObj && pItem->bUnk8 != 0 && pItem->bUnk7 == 0) {
                if (nCount == 0) {
                    if (pnItemKey != NULL) { *pnItemKey = (uintptr_t)(void *)pItem; }
                    if (ppCell != NULL) { *ppCell = pCell; }
                }
                ++nCount;
            }
        }
    }
    return nCount;
}

/* Keep integer owner keys across callbacks. Reacquire these pointers before each live read. */
static bool App_ScriptCampaignTactOwnersMatch(uintptr_t nMapKey, uintptr_t nQueueKey,
                                              GameMap **ppMap, TactState **ppQueue)
{
    GameMap *pMap;
    TactState *pQueue;

    if (g_world.nUiScreen == 18u
        || (g_world.nUiScreen == 13u && g_world.bMapBattleEnded != 0)
        || g_world.nUiScreen != 0u || g_world.bMapBattleEnded != 0) {
        return false;
    }
    pMap = g_world.pTactMap;
    pQueue = g_world.pTactState;
    if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
        || (uintptr_t)(void *)pMap != nMapKey
        || (uintptr_t)(void *)pQueue != nQueueKey) {
        return false;
    }
    if (ppMap != NULL) { *ppMap = pMap; }
    if (ppQueue != NULL) { *ppQueue = pQueue; }
    return true;
}

static Soldier *App_ScriptCampaignFindActiveXcom(GameMap *pMap, uintptr_t nUnitKey)
{
    if (pMap == NULL || nUnitKey == 0) {
        return NULL;
    }
    for (i32 i = 0; i < (i32)pMap->nXcomCount; ++i) {
        Soldier *pUnit = pMap->apXcomUnits[i];
        if (pUnit == NULL || (uintptr_t)(void *)pUnit != nUnitKey) {
            continue;
        }
        return pUnit->uHp != 0 && pUnit->bStatus < UNIT_STATUS_INACTIVE
            && pUnit->pCell != NULL ? pUnit : NULL;
    }
    return NULL;
}

typedef enum AppCampaignGrenadeSource {
    APP_CAMPAIGN_GRENADE_RIGHT_HAND = 1,
    APP_CAMPAIGN_GRENADE_BACKPACK = 2,
    APP_CAMPAIGN_GRENADE_GROUND = 3
} AppCampaignGrenadeSource;

typedef enum AppCampaignGrenadeStatus {
    APP_CAMPAIGN_GRENADE_MISSING = 0,
    APP_CAMPAIGN_GRENADE_FOUND,
    APP_CAMPAIGN_GRENADE_ARMED_OR_FUSED,
    APP_CAMPAIGN_GRENADE_UNREACHABLE_BACKPACK,
    APP_CAMPAIGN_GRENADE_HAND_OCCUPIED,
    APP_CAMPAIGN_GRENADE_INVALID
} AppCampaignGrenadeStatus;

typedef struct AppCampaignGrenadeRef {
    uintptr_t nItemKey;
    uintptr_t nActorKey;
    i32 nPoolIndex;
    i32 nRosterSlot;
    i32 nSourceKind;
    i32 nSourceSlot;
    u16 nIndex;
    u8 nAmmo;
    u8 bArmed;
    u8 bFuse;
} AppCampaignGrenadeRef;

static uintptr_t g_nCampaignGrenadeItemKey;
static uintptr_t g_nCampaignGrenadeActorKey;
static i32 g_nCampaignGrenadePoolIndex = -1;
static i32 g_nCampaignGrenadeRosterSlot = -1;
static u16 g_uCampaignGrenadeItemIndex;
static u8 g_uCampaignGrenadeAmmo;

/* Menu screens keep the same tactical owners, but TactOwnersMatch is limited to
 * screen 0. Compare the global keys first before using a saved map or queue. */
static bool App_ScriptCampaignTactOwnerKeysMatch(uintptr_t nMapKey, uintptr_t nQueueKey,
                                                 GameMap **ppMap, TactState **ppQueue)
{
    GameMap *pMap;
    TactState *pQueue;

    if (nMapKey == 0 || nQueueKey == 0 || !World_bTactPointerLive(&g_world)) {
        return false;
    }
    pMap = g_world.pTactMap;
    pQueue = g_world.pTactState;
    if (pMap == NULL || pQueue == NULL
        || (uintptr_t)(void *)pMap != nMapKey
        || (uintptr_t)(void *)pQueue != nQueueKey) {
        return false;
    }
    if (ppMap != NULL) { *ppMap = pMap; }
    if (ppQueue != NULL) { *ppQueue = pQueue; }
    return true;
}

static i32 App_ScriptCampaignGrenadePoolIndex(uintptr_t nItemKey)
{
    const i32 nCapacity = (i32)(sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0]));
    i32 nFound = -1;

    if (nItemKey == 0 || g_world.nTactItemCount < 0
        || g_world.nTactItemCount > nCapacity) {
        return -1;
    }
    for (i32 i = 0; i < g_world.nTactItemCount; ++i) {
        if ((uintptr_t)(void *)g_world.apTactItems[i] != nItemKey) {
            continue;
        }
        if (nFound >= 0) {
            return -2;
        }
        nFound = i;
    }
    return nFound;
}

static i32 App_ScriptCampaignGrenadeOwnerSlot(GameMap *pMap, Soldier *pUnit)
{
    const uintptr_t nUnitKey = (uintptr_t)(void *)pUnit;
    i32 nFound = -1;

    if (pMap == NULL || pUnit == NULL || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX
        || App_ScriptCampaignFindActiveXcom(pMap, nUnitKey) != pUnit) {
        return -1;
    }
    for (i32 i = 0; i < (i32)pMap->nXcomCount; ++i) {
        if (pMap->apXcomUnits[i] != pUnit) {
            continue;
        }
        if (nFound >= 0) {
            return -1;
        }
        nFound = i;
    }
    return nFound;
}

static AppCampaignGrenadeStatus App_ScriptCampaignGrenadeDescribeLink(
    uintptr_t nItemKey, uintptr_t nWantedKey, uintptr_t nActorKey,
    i32 nRosterSlot, i32 nSourceKind, i32 nSourceSlot,
    const ObjectRecFull *pGrenadeObj, AppCampaignGrenadeRef *pOut)
{
    const i32 nPool = App_ScriptCampaignGrenadePoolIndex(nItemKey);
    TactItem *pItem;

    if (nWantedKey != 0 && nItemKey != nWantedKey) {
        return APP_CAMPAIGN_GRENADE_MISSING;
    }
    if (nPool == -2) {
        return APP_CAMPAIGN_GRENADE_INVALID;
    }
    if (nPool < 0) {
        return (nWantedKey != 0 && nItemKey == nWantedKey)
            ? APP_CAMPAIGN_GRENADE_INVALID : APP_CAMPAIGN_GRENADE_MISSING;
    }
    pItem = g_world.apTactItems[nPool];
    if (pItem == NULL || (uintptr_t)(void *)pItem != nItemKey) {
        return APP_CAMPAIGN_GRENADE_INVALID;
    }
    if (pItem->pObj != pGrenadeObj) {
        return APP_CAMPAIGN_GRENADE_MISSING;
    }
    *pOut = (AppCampaignGrenadeRef){ 0 };
    pOut->nItemKey = nItemKey;
    pOut->nActorKey = nActorKey;
    pOut->nPoolIndex = nPool;
    pOut->nRosterSlot = nRosterSlot;
    pOut->nSourceKind = nSourceKind;
    pOut->nSourceSlot = nSourceSlot;
    pOut->nIndex = pItem->nIndex;
    pOut->nAmmo = pItem->nAmmo;
    pOut->bArmed = pItem->bUnk8;
    pOut->bFuse = pItem->bUnk7;
    return (pItem->bUnk8 != 0 || pItem->bUnk7 != 0)
        ? APP_CAMPAIGN_GRENADE_ARMED_OR_FUSED : APP_CAMPAIGN_GRENADE_FOUND;
}

static i32 App_ScriptCampaignGrenadeOwnerLinkCount(GameMap *pMap, Soldier *pUnit,
                                                   uintptr_t nItemKey)
{
    i32 nCount = 0;
    TactItem *pItem;
    i32 nWalk = 0;

    if (pMap == NULL || pUnit == NULL || pUnit->pCell == NULL || nItemKey == 0) {
        return -1;
    }
    if ((uintptr_t)(void *)pUnit->pHandR == nItemKey) {
        ++nCount;
    }
    if ((uintptr_t)(void *)pUnit->pHandL == nItemKey) {
        ++nCount;
    }
    for (i32 i = 0; i < PUFO_UNIT_BACKPACK_SLOTS; ++i) {
        if ((uintptr_t)(void *)pUnit->apBackpack[i] == nItemKey) {
            ++nCount;
        }
    }
    pItem = pUnit->pCell->pItemList;
    while (pItem != NULL && nWalk < g_world.nTactItemCount) {
        const uintptr_t nKey = (uintptr_t)(void *)pItem;
        const i32 nPool = App_ScriptCampaignGrenadePoolIndex(nKey);

        if (nPool < 0 || g_world.apTactItems[nPool] != pItem) {
            return -1;
        }
        if (nKey == nItemKey) {
            ++nCount;
        }
        pItem = g_world.apTactItems[nPool]->pNext;
        ++nWalk;
    }
    return pItem == NULL ? nCount : -1;
}

static AppCampaignGrenadeStatus App_ScriptCampaignGrenadeFindLinked(
    GameMap *pMap, Soldier *pUnit, uintptr_t nWantedKey, AppCampaignGrenadeRef *pOut)
{
    const i32 nCapacity = (i32)(sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0]));
    const ObjectRecFull *pGrenadeObj;
    AppCampaignGrenadeRef sFound = { 0 }, sInaccessible = { 0 }, sBlocked = { 0 };
    AppCampaignGrenadeStatus eFound = APP_CAMPAIGN_GRENADE_MISSING;
    bool bHasFound = false, bHasInaccessible = false, bHasBlocked = false;
    bool bHandOccupied = false;
    i32 nRosterSlot;

    if (pOut != NULL) { *pOut = (AppCampaignGrenadeRef){ 0 }; }
    if (pMap == NULL || pUnit == NULL || pOut == NULL || g_assets.apObjects == NULL
        || g_assets.nObjectCount <= 17 || g_assets.apObjects[17] == NULL
        || g_world.nTactItemCount < 0 || g_world.nTactItemCount > nCapacity
        || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX) {
        return APP_CAMPAIGN_GRENADE_INVALID;
    }
    nRosterSlot = App_ScriptCampaignGrenadeOwnerSlot(pMap, pUnit);
    if (nRosterSlot < 0 || pUnit->pCell == NULL) {
        return APP_CAMPAIGN_GRENADE_INVALID;
    }
    pGrenadeObj = g_assets.apObjects[17];

    if (pUnit->pHandR != NULL) {
        eFound = App_ScriptCampaignGrenadeDescribeLink(
            (uintptr_t)(void *)pUnit->pHandR, nWantedKey,
            (uintptr_t)(void *)pUnit, nRosterSlot, APP_CAMPAIGN_GRENADE_RIGHT_HAND, -1,
            pGrenadeObj, &sFound);
        if (eFound == APP_CAMPAIGN_GRENADE_INVALID) { return eFound; }
        bHasFound = eFound == APP_CAMPAIGN_GRENADE_FOUND
            || eFound == APP_CAMPAIGN_GRENADE_ARMED_OR_FUSED;
    }
    bHandOccupied = pUnit->pHandR != NULL;

    for (i32 i = 0; i < PUFO_UNIT_BACKPACK_SLOTS; ++i) {
        TactItem *pLink = pUnit->apBackpack[i];
        AppCampaignGrenadeRef sCandidate = { 0 };
        AppCampaignGrenadeStatus eCandidate;

        if (pLink == NULL || (nWantedKey != 0
                              && (uintptr_t)(void *)pLink != nWantedKey)) {
            continue;
        }
        eCandidate = App_ScriptCampaignGrenadeDescribeLink(
            (uintptr_t)(void *)pLink, nWantedKey, (uintptr_t)(void *)pUnit,
            nRosterSlot, APP_CAMPAIGN_GRENADE_BACKPACK, i, pGrenadeObj, &sCandidate);
        if (eCandidate == APP_CAMPAIGN_GRENADE_INVALID) { return eCandidate; }
        if (eCandidate != APP_CAMPAIGN_GRENADE_FOUND
            && eCandidate != APP_CAMPAIGN_GRENADE_ARMED_OR_FUSED) {
            continue;
        }
        if (i == 5 || i == 6) {
            if (!bHasInaccessible) {
                sInaccessible = sCandidate;
                bHasInaccessible = true;
            }
            continue;
        }
        if (bHandOccupied) {
            if (!bHasBlocked) { sBlocked = sCandidate; bHasBlocked = true; }
            continue;
        }
        if (!bHasFound || eFound == APP_CAMPAIGN_GRENADE_ARMED_OR_FUSED) {
            sFound = sCandidate;
            eFound = eCandidate;
            bHasFound = true;
        }
    }

    {
        TactItem *pLink = pUnit->pCell->pItemList;
        i32 nWalk = 0;

        while (pLink != NULL && nWalk < g_world.nTactItemCount) {
            const uintptr_t nKey = (uintptr_t)(void *)pLink;
            const i32 nPool = App_ScriptCampaignGrenadePoolIndex(nKey);
            TactItem *pItem;

            if (nPool < 0 || g_world.apTactItems[nPool] != pLink) {
                return APP_CAMPAIGN_GRENADE_INVALID;
            }
            pItem = g_world.apTactItems[nPool];
            if ((nWantedKey == 0 || nKey == nWantedKey)
                && pItem->pObj == pGrenadeObj) {
                AppCampaignGrenadeRef sCandidate = { 0 };
                const AppCampaignGrenadeStatus eCandidate =
                    App_ScriptCampaignGrenadeDescribeLink(
                        nKey, nWantedKey, (uintptr_t)(void *)pUnit, nRosterSlot,
                        APP_CAMPAIGN_GRENADE_GROUND, -1, pGrenadeObj, &sCandidate);

                if (eCandidate == APP_CAMPAIGN_GRENADE_INVALID) { return eCandidate; }
                if (eCandidate == APP_CAMPAIGN_GRENADE_FOUND
                    || eCandidate == APP_CAMPAIGN_GRENADE_ARMED_OR_FUSED) {
                    if (!bHandOccupied
                        && (!bHasFound || eFound == APP_CAMPAIGN_GRENADE_ARMED_OR_FUSED)) {
                        sFound = sCandidate;
                        eFound = eCandidate;
                        bHasFound = true;
                    } else if (bHandOccupied && !bHasBlocked) {
                        sBlocked = sCandidate;
                        bHasBlocked = true;
                    }
                }
            }
            pLink = pItem->pNext;
            ++nWalk;
        }
        if (pLink != NULL) { return APP_CAMPAIGN_GRENADE_INVALID; }
    }

    if (bHasFound) {
        if (App_ScriptCampaignGrenadeOwnerLinkCount(pMap, pUnit, sFound.nItemKey) != 1) {
            return APP_CAMPAIGN_GRENADE_INVALID;
        }
        *pOut = sFound;
        return eFound;
    }
    if (bHasInaccessible) {
        *pOut = sInaccessible;
        return APP_CAMPAIGN_GRENADE_UNREACHABLE_BACKPACK;
    }
    if (bHasBlocked) {
        *pOut = sBlocked;
        return APP_CAMPAIGN_GRENADE_HAND_OCCUPIED;
    }
    return APP_CAMPAIGN_GRENADE_MISSING;
}

static bool App_ScriptCampaignGrenadeRefresh(uintptr_t nMapKey, uintptr_t nQueueKey,
                                             uintptr_t nActorKey, uintptr_t nItemKey,
                                             i32 nPoolIndex, i32 nRosterSlot,
                                             u16 nItemIndex, u8 nAmmo,
                                             GameMap **ppMap, TactState **ppQueue,
                                             Soldier **ppUnit, TactItem **ppGrenade)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit;
    TactItem *pGrenade;
    i32 nPool;

    if (!App_ScriptCampaignTactOwnerKeysMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX
        || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nActorKey)) == NULL
        || App_ScriptCampaignGrenadeOwnerSlot(pMap, pUnit) != nRosterSlot) {
        return false;
    }
    nPool = App_ScriptCampaignGrenadePoolIndex(nItemKey);
    if (nPool != nPoolIndex || nPool < 0
        || g_world.apTactItems[nPool] == NULL
        || (uintptr_t)(void *)g_world.apTactItems[nPool] != nItemKey) {
        return false;
    }
    pGrenade = g_world.apTactItems[nPool];
    if (g_assets.apObjects == NULL || g_assets.nObjectCount <= 17
        || pGrenade->pObj != g_assets.apObjects[17]
        || pGrenade->nIndex != nItemIndex || pGrenade->nAmmo != nAmmo) {
        return false;
    }
    if (ppMap != NULL) { *ppMap = pMap; }
    if (ppQueue != NULL) { *ppQueue = pQueue; }
    if (ppUnit != NULL) { *ppUnit = pUnit; }
    if (ppGrenade != NULL) { *ppGrenade = pGrenade; }
    return true;
}

static bool App_ScriptCampaignGrenadeHeldBy(GameMap *pMap, Soldier *pUnit,
                                            TactItem *pGrenade, uintptr_t nItemKey)
{
    return pMap != NULL && pUnit != NULL && pGrenade != NULL
        && (uintptr_t)(void *)pUnit->pHandR == nItemKey
        && pGrenade->bUnk8 == 1 && pGrenade->bUnk7 == 0;
}

static Soldier *App_ScriptCampaignFindActiveEnemy(GameMap *pMap, uintptr_t nUnitKey)
{
    if (pMap == NULL || nUnitKey == 0) {
        return NULL;
    }
    for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
        Soldier *pUnit = pMap->apEnemyUnits[i];
        if (pUnit == NULL || (uintptr_t)(void *)pUnit != nUnitKey) {
            continue;
        }
        return pUnit->uHp != 0 && pUnit->bStatus < UNIT_STATUS_INACTIVE
            && pUnit->pCell != NULL ? pUnit : NULL;
    }
    return NULL;
}

/* The frame test measures these two real camera-key vectors and solves the cell
 * projection with Cramer's rule. Keep the same method here, but send every key via
 * the window path used by the game. */
static bool App_ScriptCampaignPanToCell(i32 nTargetX, i32 nTargetY)
{
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    i32 nBaseX, nBaseY, nAfterX, nAfterY;
    i32 nDownX, nDownY, nRightX, nRightY, nDet;
    i32 nDown, nRight;
    i32 nStartX, nStartY, nCamX0, nCamY0, nCamXDown, nCamYDown, nCamXRight, nCamYRight;

    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    App_ScriptPointerLogical(120, 120, false);
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    nBaseX = g_world.tactEnv.cursor.nX;
    nBaseY = g_world.tactEnv.cursor.nY;
    nStartX = nBaseX;
    nStartY = nBaseY;
    nCamX0 = g_world.tactEnv.focus.nCamScrollX;
    nCamY0 = g_world.tactEnv.focus.nCamScrollY;
    if (!App_ScriptCampaignKey(PUFO_KEY_DOWN)
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    nCamXDown = g_world.tactEnv.focus.nCamScrollX;
    nCamYDown = g_world.tactEnv.focus.nCamScrollY;
    App_ScriptPointerLogical(120, 120, false);
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    nDownX = g_world.tactEnv.cursor.nX - nBaseX;
    nDownY = g_world.tactEnv.cursor.nY - nBaseY;
    if (!App_ScriptCampaignKey(PUFO_KEY_UP)
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }

    App_ScriptPointerLogical(120, 120, false);
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    nBaseX = g_world.tactEnv.cursor.nX;
    nBaseY = g_world.tactEnv.cursor.nY;
    if (!App_ScriptCampaignKey(PUFO_KEY_RIGHT)
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    nCamXRight = g_world.tactEnv.focus.nCamScrollX;
    nCamYRight = g_world.tactEnv.focus.nCamScrollY;
    App_ScriptPointerLogical(120, 120, false);
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    nRightX = g_world.tactEnv.cursor.nX - nBaseX;
    nRightY = g_world.tactEnv.cursor.nY - nBaseY;
    if (!App_ScriptCampaignKey(PUFO_KEY_LEFT)
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    App_ScriptPointerLogical(120, 120, false);
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }

    nDet = nDownX * nRightY - nDownY * nRightX;
    if (nDet == 0) {
        printf("pocketufo: campaign pan: FAIL parallel key vectors %d/%d %d/%d\n",
               (int)nDownX, (int)nDownY, (int)nRightX, (int)nRightY);
        fflush(stdout);
        return false;
    }
    nAfterX = nTargetX - g_world.tactEnv.cursor.nX;
    nAfterY = nTargetY - g_world.tactEnv.cursor.nY;
    nDown = (nAfterX * nRightY - nAfterY * nRightX) / nDet;
    nRight = (nDownX * nAfterY - nDownY * nAfterX) / nDet;
    if (nDown > 80) { nDown = 80; }
    if (nDown < -80) { nDown = -80; }
    if (nRight > 80) { nRight = 80; }
    if (nRight < -80) { nRight = -80; }
    for (i32 i = 0; i < nDown; ++i) {
        if (!App_ScriptCampaignKey(PUFO_KEY_DOWN)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    }
    for (i32 i = 0; i > nDown; --i) {
        if (!App_ScriptCampaignKey(PUFO_KEY_UP)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    }
    for (i32 i = 0; i < nRight; ++i) {
        if (!App_ScriptCampaignKey(PUFO_KEY_RIGHT)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    }
    for (i32 i = 0; i > nRight; --i) {
        if (!App_ScriptCampaignKey(PUFO_KEY_LEFT)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    }
    App_ScriptPointerLogical(120, 120, false);
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    printf("pocketufo: campaign pan: start %d/%d scroll %d/%d down %d/%d right %d/%d end %d/%d scroll %d/%d "
           "target %d/%d steps %d/%d vectors %d/%d %d/%d\n",
           (int)nStartX, (int)nStartY, (int)nCamX0, (int)nCamY0,
           (int)nCamXDown, (int)nCamYDown, (int)nCamXRight, (int)nCamYRight,
           (int)g_world.tactEnv.cursor.nX, (int)g_world.tactEnv.cursor.nY,
           (int)g_world.tactEnv.focus.nCamScrollX, (int)g_world.tactEnv.focus.nCamScrollY,
           (int)nTargetX, (int)nTargetY, (int)nDown, (int)nRight,
           (int)nDownX, (int)nDownY, (int)nRightX, (int)nRightY);
    fflush(stdout);
    return true;
}

static bool App_ScriptCampaignFindCellPixel(i32 nCellX, i32 nCellY, i32 nCellZ,
                                             i32 *pnScreenX, i32 *pnScreenY)
{
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    const PadImage *pHud;
    i32 nMapBottom = 240;

    if (g_world.nUiScreen != 0 || pnScreenX == NULL || pnScreenY == NULL) {
        return false;
    }
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    pHud = g_world.apTactPads[PUFO_UIPAD_HUD];
    if (pHud != NULL) { nMapBottom = (i32)pHud->nOriginY - 20; }
    while ((i32)g_world.tactEnv.cursor.nFlags < nCellZ + 1) {
        if (!App_ScriptCampaignClickBank(2)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    }
    while ((i32)g_world.tactEnv.cursor.nFlags > nCellZ + 1) {
        if (!App_ScriptCampaignClickBank(7)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    }
    if (!App_ScriptCampaignPanToCell(nCellX, nCellY)) {
        return false;
    }
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    for (i32 y = 20; y <= nMapBottom; y += 4) {
        for (i32 x = 24; x <= 216; x += 4) {
            App_ScriptPointerLogical(x, y, false);
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
            if (g_world.tactEnv.cursor.nX == nCellX
                && g_world.tactEnv.cursor.nY == nCellY
                && (i32)g_world.tactEnv.cursor.nFlags == nCellZ + 1) {
                *pnScreenX = x;
                *pnScreenY = y;
                printf("pocketufo: campaign target cell %d/%d/%d at %d/%d\n",
                       (int)nCellX, (int)nCellY, (int)nCellZ, (int)x, (int)y);
                fflush(stdout);
                return true;
            }
        }
    }
    printf("pocketufo: campaign target scan: cell %d/%d/%d missed current pen band; cursor %d/%d layer %d\n",
           (int)nCellX, (int)nCellY, (int)nCellZ,
           (int)g_world.tactEnv.cursor.nX, (int)g_world.tactEnv.cursor.nY,
           (int)g_world.tactEnv.cursor.nFlags);
    fflush(stdout);
    return false;
}

static bool App_ScriptCampaignDrainQueue(i32 nTickLimit)
{
    GameMap *pMap = g_world.pTactMap;
    TactState *pQueue = g_world.pTactState;
    uintptr_t nMapKey, nQueueKey;
    i32 nTick = 0;

    if (pMap == NULL || pQueue == NULL) {
        return false;
    }
    nMapKey = (uintptr_t)(void *)pMap;
    nQueueKey = (uintptr_t)(void *)pQueue;
    while (nTick < nTickLimit) {
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
            printf("pocketufo: campaign tick drain: stopped at terminal screen %u or changed tactical owners\n",
                   (unsigned)g_world.nUiScreen);
            fflush(stdout);
            return false;
        }
        if (pQueue->nQueueLen == 0 && pQueue->bQueueActive == 0 && pMap->tact.bBusy == 0) {
            break;
        }
        pMap = NULL;
        pQueue = NULL;
        Timer_TickBySpeed((i32)g_world.gameClock.nSpeed, App_GameTickOnce, NULL);
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
            printf("pocketufo: campaign tick drain: stopped after tick at terminal screen %u or changed tactical owners\n",
                   (unsigned)g_world.nUiScreen);
            fflush(stdout);
            return false;
        }
        pMap = NULL;
        pQueue = NULL;
        App_DrawFrame();
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
            printf("pocketufo: campaign tick drain: stopped after draw at terminal screen %u or changed tactical owners\n",
                   (unsigned)g_world.nUiScreen);
            fflush(stdout);
            return false;
        }
        ++nTick;
    }
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
        return false;
    }
    printf("pocketufo: campaign tick drain: %d, queue %d/%d active %d busy %d\n",
           (int)nTick, (int)pQueue->nQueueCursor, (int)pQueue->nQueueLen,
           (int)pQueue->bQueueActive, (int)pMap->tact.bBusy);
    fflush(stdout);
    return pQueue->nQueueLen == 0 && pQueue->bQueueActive == 0 && pMap->tact.bBusy == 0;
}

static void App_ScriptCampaignState(const char *pszLabel)
{
    const GameMap *pMap = g_world.pTactMap;
    const TactState *pQueue = g_world.pTactState;
    const Soldier *pSel = (pMap != NULL) ? pMap->pSelUnit : NULL;
    const StrategItem *pContact = NULL;
    i32 nEnemyCells = 0, nPrimary9 = 0;

    if (pMap != NULL) {
        for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
            const Soldier *pEnemy = pMap->apEnemyUnits[i];
            if (pEnemy != NULL && pEnemy->pCell != NULL) { ++nEnemyCells; }
            if (pEnemy != NULL && pEnemy->nUnitType == 9u && pEnemy->pCell != NULL) { ++nPrimary9; }
        }
    }

    if (g_world.nSelectedStrategIdx >= 0
        && g_world.nSelectedStrategIdx < PtrArray_GetCount(&g_world.strategList)) {
        pContact = (const StrategItem *)PtrArray_Get(&g_world.strategList,
                                                     g_world.nSelectedStrategIdx);
    }
    printf("pocketufo: campaign state %s: screen %d tact %d/%d mission %d tier %d variant %d damageBand %u "
           "sprites %u/%u/%u/%u craft %d "
           "contact %d/%d hp %d crew %d xcom %d enemy %d reserve %d items %d queue %d/%d active %d "
           "phase %d busy %d enemyturn %d input %d battle %d enemyCells %d primary9 %d\n",
           pszLabel, (int)g_world.nUiScreen, (int)g_world.bTactLive, (int)g_world.bTactFlag,
           (pMap != NULL) ? (int)pMap->nMissionType : (int)g_world.nMapMissionType,
           (int)g_world.nMapMissionTierRaw, (int)g_world.nMapTerrainVariant,
           (unsigned)g_world.bMapUnk15115,
           (unsigned)g_world.abMapSpriteSet[0], (unsigned)g_world.abMapSpriteSet[1],
           (unsigned)g_world.abMapSpriteSet[2], (unsigned)g_world.abMapSpriteSet[3],
           (int)g_world.nMapCraftIndex,
           (pContact != NULL) ? pContact->nDisplayId : -1,
           (int)g_world.nSelectedStrategIdx, (pContact != NULL) ? pContact->nHp : -1,
           (int)g_world.nSoldierCount, (pMap != NULL) ? (int)pMap->nXcomCount : -1,
           (pMap != NULL) ? (int)pMap->nEnemyCount : -1,
           (int)g_world.nMapEnemyReserveCount, (int)g_world.nTactItemCount,
           (pQueue != NULL) ? (int)pQueue->nQueueCursor : -1,
           (pQueue != NULL) ? (int)pQueue->nQueueLen : -1,
           (pQueue != NULL) ? (int)pQueue->bQueueActive : 0,
           (pMap != NULL) ? (int)pMap->tact.nPhase : -1,
           (pMap != NULL) ? (int)pMap->tact.bBusy : 0,
           (int)g_world.bMapEnemyTurnActive, (int)g_world.bMapPlayerInput,
           (int)g_world.bMapBattleEnded, (int)nEnemyCells, (int)nPrimary9);
    if (pMap != NULL) {
        for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
            const Soldier *pEnemy = pMap->apEnemyUnits[i];
            const MapCell *pCell;
            const TactItem *apHands[2];

            if (pEnemy == NULL) {
                continue;
            }
            pCell = pEnemy->pCell;
            apHands[0] = pEnemy->pHandL;
            apHands[1] = pEnemy->pHandR;
            printf("pocketufo: campaign enemy-snapshot unit index=%d type=%u cell=%d/%d/%d "
                   "hp=%u stunOrArmor=%u tu=%u status=%u aiTick=%d aiDone=%u targets=%d\n",
                   (int)i, (unsigned)pEnemy->nUnitType,
                   pCell != NULL ? pCell->nX : -999, pCell != NULL ? pCell->nY : -999,
                   pCell != NULL ? pCell->nZ : -999, (unsigned)pEnemy->uHp,
                   (unsigned)pEnemy->uStunOrArmor, (unsigned)pEnemy->uTu,
                   (unsigned)pEnemy->bStatus, pEnemy->nAiTick, (unsigned)pEnemy->bAiDone,
                   (int)pEnemy->nMultiTarget);
            for (i32 nHand = 0; nHand < 2; ++nHand) {
                const TactItem *pItem = apHands[nHand];
                const ObjectRecFull *pObj;
                i32 nSortKey, nCategory;

                if (pItem == NULL || pItem == &g_itemSlotEmpty) {
                    printf("pocketufo: campaign enemy-snapshot hand unit=%d hand=%d empty=1\n",
                           (int)i, (int)nHand);
                    continue;
                }
                pObj = pItem->pObj;
                nSortKey = (pObj != NULL) ? (i32)pObj->uSortKey : -1;
                nCategory = (pObj != NULL) ? (i32)pObj->nCategory : -1;
                printf("pocketufo: campaign enemy-snapshot hand unit=%d hand=%d objectId=%d "
                       "sort=%d category=%d clipId=%u ammo=%u itemRef=%d\n",
                       (int)i, (int)nHand, App_ScriptCampaignObjectSlot(pObj),
                       (int)nSortKey, (int)nCategory, (unsigned)pItem->nIndex,
                       (unsigned)pItem->nAmmo, (int)pItem->nRefId);
            }
        }
    }
    if (pSel != NULL) {
        printf("  selected id %d cell %d/%d/%d tu %u hp %u hands %d/%d\n",
               (int)pSel->nSaveId,
               pSel->pCell != NULL ? pSel->pCell->nX : -999,
               pSel->pCell != NULL ? pSel->pCell->nY : -999,
               pSel->pCell != NULL ? pSel->pCell->nZ : -999,
               (unsigned)pSel->uTu, (unsigned)pSel->uHp,
               App_ScriptCampaignObjectSlot(pSel->pHandL != NULL ? pSel->pHandL->pObj : NULL),
               App_ScriptCampaignObjectSlot(pSel->pHandR != NULL ? pSel->pHandR->pObj : NULL));
    }
    for (i32 i = 0; i < (i32)g_world.nTactItemCount; ++i) {
        const TactItem *pItem = g_world.apTactItems[i];
        if (pItem == NULL) {
            continue;
        }
        printf("  item %d ref %d object %d ammo %u magazine %u armed %u fuse %u\n",
               (int)i, (int)pItem->nRefId,
               App_ScriptCampaignObjectSlot(pItem->pObj), (unsigned)pItem->nAmmo,
               (unsigned)pItem->nIndex, (unsigned)pItem->bUnk8, (unsigned)pItem->bUnk7);
    }
    for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
        const Soldier *pSoldier = g_world.apSoldiers[i];
        i32 nLoadout = 0;

        if (pSoldier == NULL) { continue; }
        for (i32 slot = 0; slot < APP_PERSISTENT_LOADOUT_SLOTS; ++slot) {
            if (pSoldier->auUnk962[slot] != 0) { ++nLoadout; }
        }
        printf("  roster %d id %d name %.*s type %u armor %d craft %u loadout %d\n",
               (int)i, (int)pSoldier->nUnk440, 39, pSoldier->szName,
               (unsigned)pSoldier->nUnitType, (int)pSoldier->nArmorPoints,
               (unsigned)pSoldier->bUnk183, (int)nLoadout);
        for (i32 slot = 0; slot < APP_PERSISTENT_LOADOUT_SLOTS; ++slot) {
            if (pSoldier->auUnk962[slot] != 0) {
                printf("    loadout slot %d sort %u ammo type %u\n", (int)slot,
                       (unsigned)pSoldier->auUnk962[slot],
                       (unsigned)pSoldier->abUnk1012[slot]);
            }
        }
    }
    if (g_world.nSelectedBase >= 0 && g_world.nSelectedBase < PUFO_FAC_BASE_COUNT
        && g_world.apBases[g_world.nSelectedBase] != NULL && g_assets.apObjects != NULL
        && g_assets.nObjectCount > 38 && g_assets.apObjects[4] != NULL
        && g_assets.apObjects[17] != NULL && g_assets.apObjects[38] != NULL
        && g_world.bCraftCount > 0) {
        const i32 nBase = (i32)g_world.nSelectedBase;
        const i32 nCraft = (g_world.nMapCraftIndex >= 0 && g_world.nMapCraftIndex < (i32)g_world.bCraftCount)
                               ? (i32)g_world.nMapCraftIndex : 0;
        const Ship *pCraft = g_world.apCraft[nCraft];
        if (pCraft != NULL && pCraft->nSlotIdx >= 0
            && pCraft->nSlotIdx < (i32)(sizeof(g_assets.apObjects[4]->aItemQty)
                                        / sizeof(g_assets.apObjects[4]->aItemQty[0]))) {
            const i32 nCraftSlot = pCraft->nSlotIdx;
            printf("  stores base %d rifle %u clip %u grenade %u craft %d cargo %u/%u/%u\n",
                   (int)nBase,
                   (unsigned)g_assets.apObjects[4]->auPerBaseVal[nBase],
                   (unsigned)g_assets.apObjects[38]->auPerBaseVal[nBase],
                   (unsigned)g_assets.apObjects[17]->auPerBaseVal[nBase],
                   (int)nCraft,
                   (unsigned)g_assets.apObjects[4]->aItemQty[nCraftSlot],
                   (unsigned)g_assets.apObjects[38]->aItemQty[nCraftSlot],
                   (unsigned)g_assets.apObjects[17]->aItemQty[nCraftSlot]);
            printf("  cargo detail craftIndex %d slotIdx %d objectAtIndex %u/%u/%u objectAtSlot %u/%u/%u shipColumns",
                   (int)nCraft, (int)nCraftSlot,
                   (unsigned)g_assets.apObjects[4]->aItemQty[nCraft],
                   (unsigned)g_assets.apObjects[38]->aItemQty[nCraft],
                   (unsigned)g_assets.apObjects[17]->aItemQty[nCraft],
                   (unsigned)g_assets.apObjects[4]->aItemQty[nCraftSlot],
                   (unsigned)g_assets.apObjects[38]->aItemQty[nCraftSlot],
                   (unsigned)g_assets.apObjects[17]->aItemQty[nCraftSlot]);
            for (i32 i = 0; i < PUFO_SHIP_CARGO_COLUMNS; ++i) {
                printf(" %d", (int)pCraft->anCargoQty[i]);
            }
            printf("\n");
        }
    }
    fflush(stdout);
}

/* Read-only post-mission inventory view. ObjectRec.aItemQty is indexed by the
 * map's craft index during tactical recovery; the ship slot index is printed
 * beside it because both arrays are visible after a return. */
static bool App_ScriptCampaignCargo(const char *pszLabel)
{
    const char *pszState = (pszLabel != NULL) ? pszLabel : "requested";
    const i32 nCraftIndex = (i32)g_world.nMapCraftIndex;
    const Ship *pCraft = NULL;
    i32 nCraftSlot = -1;
    i32 nCraftObjects = 0;
    i32 nSiteObjects = 0;

    if (!g_bWorldReady || g_assets.apObjects == NULL || g_assets.nObjectCount == 0
        || nCraftIndex < 0 || nCraftIndex >= PUFO_ITEM_QTY_COUNT) {
        printf("pocketufo: campaign cargo %s: FAIL (craft index %d or object table is not live)\n",
               pszState, (int)nCraftIndex);
        fflush(stdout);
        return false;
    }
    if (nCraftIndex < (i32)g_world.bCraftCount) {
        pCraft = g_world.apCraft[nCraftIndex];
        if (pCraft != NULL) {
            nCraftSlot = pCraft->nSlotIdx;
        }
    }

    printf("pocketufo: campaign cargo %s: craftIndex %d craftSlot %d objects %u sites %u\n",
           pszState, (int)nCraftIndex, (int)nCraftSlot,
           (unsigned)g_assets.nObjectCount, (unsigned)g_world.nSiteCount);
    for (i32 nSlot = 1; nSlot < (i32)g_assets.nObjectCount && nSlot < 256; ++nSlot) {
        const ObjectRecFull *pObj = g_assets.apObjects[nSlot];
        const u16 uByCraftIndex = (pObj != NULL) ? pObj->aItemQty[nCraftIndex] : 0;
        const u16 uByShipSlot = (pObj != NULL && nCraftSlot >= 0
                                && nCraftSlot < PUFO_ITEM_QTY_COUNT)
                                    ? pObj->aItemQty[nCraftSlot] : 0;

        if (uByCraftIndex == 0 && uByShipSlot == 0
            && nSlot != 4 && nSlot != 17 && nSlot != 38) {
            continue;
        }
        printf("pocketufo: campaign cargo item %s: object %d craftIndexQty %u shipSlotQty %u\n",
               pszState, (int)nSlot, (unsigned)uByCraftIndex, (unsigned)uByShipSlot);
        if (uByCraftIndex != 0 || uByShipSlot != 0) {
            ++nCraftObjects;
        }
    }
    if (g_world.apSites != NULL && g_world.nSiteCount > 0
        && g_world.nSiteCount <= PUFO_SMAP_SITE_COUNT) {
        for (i32 nSite = 0; nSite < (i32)g_world.nSiteCount; ++nSite) {
            const SmapSite *pSite = &g_world.apSites[nSite];
            for (i32 nSlot = 1; nSlot < (i32)g_assets.nObjectCount && nSlot < 256; ++nSlot) {
                const u16 uQty = pSite->auItemQty[nSlot];
                if (uQty == 0) { continue; }
                printf("pocketufo: campaign cargo site %s: site %d object %d qty %u\n",
                       pszState, (int)nSite, (int)nSlot, (unsigned)uQty);
                ++nSiteObjects;
            }
        }
    }
    printf("pocketufo: campaign cargo summary %s: craftObjects %d siteObjects %d\n",
           pszState, (int)nCraftObjects, (int)nSiteObjects);
    fflush(stdout);
    return true;
}

static bool App_ScriptCampaignStageGear(void)
{
    static const i32 anCargoSlot[] = { 4, 38, 17 }; /* original starter rifle, clip and grenade */
    static const i32 anExpectedQty[] = { 6, 10, 4 };
    i32 nBase, nCraft = -1, nCrew = 0;
    BaseRec *pBase;
    Ship *pCraft;

    if (!g_bWorldReady || g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0) {
        printf("pocketufo: campaign gear: FAIL (need the placed base on the geoscape)\n");
        fflush(stdout);
        return false;
    }
    for (i32 c = 0; c < (i32)g_world.bCraftCount; ++c) {
        i32 nAboard = Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, c);
        if (nAboard > 0 && g_world.apCraft[c] != NULL && g_world.apCraft[c]->bAtBase != 0) {
            nCraft = c;
            nCrew = nAboard;
            break;
        }
    }
    if (nCraft < 0) {
        printf("pocketufo: campaign gear: FAIL (no home craft carries the crew)\n");
        fflush(stdout);
        return false;
    }
    pCraft = g_world.apCraft[nCraft];
    nBase = (g_world.nSelectedBase >= 0 && g_world.nSelectedBase < PUFO_FAC_BASE_COUNT)
                ? (i32)g_world.nSelectedBase : (i32)pCraft->bHomeBase;
    if (nBase < 0 || nBase >= PUFO_FAC_BASE_COUNT || g_world.apBases[nBase] == NULL) {
        printf("pocketufo: campaign gear: FAIL (no home base)\n");
        fflush(stdout);
        return false;
    }
    if (pCraft->nSlotIdx < 0
        || pCraft->nSlotIdx >= (i32)(sizeof(g_assets.apObjects[4]->aItemQty)
                                     / sizeof(g_assets.apObjects[4]->aItemQty[0]))) {
        printf("pocketufo: campaign gear: FAIL (craft cargo slot %d is out of range)\n",
               (int)pCraft->nSlotIdx);
        fflush(stdout);
        return false;
    }
    pBase = g_world.apBases[nBase];
    for (i32 k = 0; k < (i32)(sizeof(anCargoSlot) / sizeof(anCargoSlot[0])); ++k) {
        const i32 nSlot = anCargoSlot[k];
        ObjectRecFull *pObj;
        i32 nGate;

        if (nSlot < 0 || nSlot >= (i32)g_assets.nObjectCount
            || g_assets.apObjects == NULL || g_assets.apObjects[nSlot] == NULL) {
            printf("pocketufo: campaign gear: FAIL (missing object slot %d)\n", (int)nSlot);
            fflush(stdout);
            return false;
        }
        pObj = g_assets.apObjects[nSlot];
        nGate = (nSlot < PUFO_SLOT_TOPIC_GATE_COUNT) ? (i32)g_abSlotTopicGate[nSlot] : -1;
        if (nGate < 0 || nGate >= (i32)g_world.nResearchEntries
            || g_world.aTopics[nGate].bUnlocked == 0) {
            printf("pocketufo: campaign gear: FAIL (object %d is not unlocked)\n", (int)nSlot);
            fflush(stdout);
            return false;
        }
        if ((i32)pObj->aItemQty[pCraft->nSlotIdx] != anExpectedQty[k]) {
            printf("pocketufo: campaign gear: FAIL (raw starter cargo object %d is %u, expected %d)\n",
                   (int)nSlot, (unsigned)pObj->aItemQty[pCraft->nSlotIdx],
                   (int)anExpectedQty[k]);
            fflush(stdout);
            return false;
        }
    }
    printf("pocketufo: campaign gear: craft %d crew %d cargo rifle %d clip %d grenade %d stores %d\n",
           (int)nCraft, (int)nCrew,
           (int)g_assets.apObjects[anCargoSlot[0]]->aItemQty[pCraft->nSlotIdx],
           (int)g_assets.apObjects[anCargoSlot[1]]->aItemQty[pCraft->nSlotIdx],
           (int)g_assets.apObjects[anCargoSlot[2]]->aItemQty[pCraft->nSlotIdx],
           (int)pBase->nStoresTotal);
    fflush(stdout);
    return g_assets.apObjects[anCargoSlot[0]]->aItemQty[pCraft->nSlotIdx] == anExpectedQty[0]
        && g_assets.apObjects[anCargoSlot[1]]->aItemQty[pCraft->nSlotIdx] == anExpectedQty[1]
        && g_assets.apObjects[anCargoSlot[2]]->aItemQty[pCraft->nSlotIdx] == anExpectedQty[2];
}

static bool App_ScriptCampaignCreateContact(void)
{
    Ship *pCraft = NULL;
    BaseRec *pBase;
    StrategItem *pUfo;
    i32 nCraft = -1, nContact;
    i32 nLaunch;

    if (!g_bWorldReady || g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0) {
        printf("pocketufo: campaign contact: FAIL (need the geoscape map)\n");
        fflush(stdout);
        return false;
    }
    for (i32 c = 0; c < (i32)g_world.bCraftCount; ++c) {
        if (g_world.apCraft[c] != NULL && g_world.apCraft[c]->bAtBase != 0
            && g_world.apCraft[c]->bIntercepting == 0
            && Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, c) > 0) {
            pCraft = g_world.apCraft[c];
            nCraft = c;
            break;
        }
    }
    if (nCraft < 0 || pCraft == NULL || pCraft->bHomeBase >= PUFO_FAC_BASE_COUNT
        || g_world.apBases[pCraft->bHomeBase] == NULL) {
        printf("pocketufo: campaign contact: FAIL (no ready crew craft)\n");
        fflush(stdout);
        return false;
    }
    pBase = g_world.apBases[pCraft->bHomeBase];
    pUfo = StrategList_AddItem(&g_world.strategList, &g_world.nStrategSeed, g_world.view.nMode,
                               4, 0, 2, pBase->nUnk8, pBase->nUnk12, 4250, 1);
    if (pUfo == NULL) {
        printf("pocketufo: campaign contact: FAIL (contact factory returned NULL)\n");
        fflush(stdout);
        return false;
    }
    nContact = PtrArray_GetCount(&g_world.strategList) - 1;
    pUfo->nDisplayId = 3;
    pUfo->nHp = 50; /* class 2's 250-point budget: the damaged-UFO band reaches 70 */
    pUfo->bRace = 0; /* the real race-0 launch row selects sprite set 9 for the primary unit type */
    nLaunch = Ship_LaunchIntercept(&g_world.shipEnv, nCraft, nContact);
    if (nLaunch < 0 || pCraft->bIntercepting == 0 || pCraft->nStrategIdx != nContact) {
        printf("pocketufo: campaign contact: FAIL (intercept factory refused craft %d, contact %d)\n",
               (int)nCraft, (int)nContact);
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign contact created: id %d class %u race %u hp %d at %d/%d; craft %d crew %d intercept %d\n",
           (int)pUfo->nDisplayId, (unsigned)pUfo->bCraftClass, (unsigned)pUfo->bRace, (int)pUfo->nHp,
           (int)pUfo->nX, (int)pUfo->nY, (int)nCraft,
           Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, nCraft),
           (int)pCraft->nStrategIdx);
    fflush(stdout);
    return true;
}

/* Read-only timeout trace. Re-find the fixture item by seed; never keep the
 * original StrategItem pointer across a geoscape pulse. */
static void App_ScriptCampaignBaseDefenseTrace(PufoWorldState *pW, i32 nEventSeed,
                                                i32 nPulse, i32 nTimeStart,
                                                const char *pszReason)
{
    const i32 nTotal = PtrArray_GetCount(&pW->strategList);
    StrategItem *pEvent = NULL;
    i32 nActive = 0;

    for (i32 i = 0; i < nTotal; ++i) {
        StrategItem *pItem = (StrategItem *)PtrArray_Get(&pW->strategList, i);
        if (pItem == NULL) {
            continue;
        }
        if (pItem->nType == 1 && pItem->bActive == 1) {
            ++nActive;
        }
        if (pItem->nSeed == nEventSeed) {
            pEvent = pItem;
        }
    }

    printf("pocketufo: campaign base-defense-trace %s: seconds %d->%d tickSec %d paused %u "
           "screen %d pulses %d baseCount %d list %d activeType1 %d seed %d found %d\n",
           (pszReason != NULL) ? pszReason : "timeout", (int)nTimeStart,
           (int)pW->geo.time.nTotalSec, (int)pW->geo.world.nTickSeconds,
           (unsigned)pW->geo.time.bPaused, (int)pW->screen.nGeoscapeScreen,
           (int)nPulse, (int)pW->nBaseCount, (int)nTotal, (int)nActive,
           (int)nEventSeed, pEvent != NULL ? 1 : 0);
    if (pEvent != NULL) {
        const i32 nElapsedSec = pW->geo.time.nTotalSec - pEvent->nTravelEpochSec;
        const i32 nElapsedMin = (nElapsedSec >= 0) ? nElapsedSec / 60 : -1;

        printf("pocketufo: campaign base-defense-event: type %u active %u class %u race %u hp %d "
               "visible %u subtype %u phase %d timer %d pos %d/%d start %d/%d dest %d/%d "
               "travelMin %u epoch %d elapsedMin %d",
               (unsigned)pEvent->nType, (unsigned)pEvent->bActive,
               (unsigned)pEvent->bCraftClass, (unsigned)pEvent->bRace,
               (int)pEvent->nHp, (unsigned)pEvent->bVisible,
               (unsigned)pEvent->nMissionSub, (int)pEvent->nPhase,
               (int)pEvent->nTimer, (int)pEvent->nX, (int)pEvent->nY,
               (int)pEvent->nStartX, (int)pEvent->nStartY,
               (int)pEvent->nDestX, (int)pEvent->nDestY,
               (unsigned)pEvent->uTravelMin, (int)pEvent->nTravelEpochSec,
               (int)nElapsedMin);
        if (pEvent->nPhase < 4) {
            printf(" target %d/%d/%d\n", (int)pEvent->nTargetX,
                   (int)pEvent->nTargetY, (int)pEvent->nTargetIdx);
        } else {
            printf(" target unset\n");
        }
    }
    fflush(stdout);
}

/* SHELL FIXTURE: create one strategic UFO through the native factory, then enter
 * the real subtype-6 mission path through the live World callback. The event
 * advance, base choice, Base At screen, player confirm, and type-3 launch stay
 * on the normal clock and UI path. This helper does not set a target, tactical
 * field, defender value, result flag, or ending state. */
static bool App_ScriptCampaignStageBaseDefense(void)
{
    const i32 nPulseLimit = 1200;
    PufoWorldState *pW = &g_world;
    StrategItem *pUfo;
    i32 nItemIndex;
    i32 nAssigned;
    i32 nSpawnResult;
    i32 nEventSeed;
    i32 nPulse;
    i32 nTimeStart;

    if (!g_bWorldReady || pW->bGeoscapeUi == 0
        || pW->screen.nGeoscapeScreen != 0 || pW->geo.time.bPaused != 0
        || pW->geo.world.nTickSeconds != 3600
        || pW->bTactLive != 0 || pW->nBaseCount != 1 || pW->apBases[0] == NULL
        || pW->spawnCtx.pfnSpawnMission == NULL
        || pW->spawnCtx.pfnProcessEvents == NULL
        || pW->spawnCtx.pUser != pW
        || pW->spawnCtx.pnSeed != &pW->nStrategSeed) {
        printf("pocketufo: campaign base-defense-entry: FAIL (need one live base, screen 0, unpaused clock, one-hour speed, and wired event context)\n");
        fflush(stdout);
        return false;
    }

    nAssigned = Base_CountSoldiersAssigned(pW->apSoldiers, pW->nSoldierCount,
                                          pW->apCraft, pW->bCraftCount, 0);
    if (nAssigned <= 0) {
        printf("pocketufo: campaign base-defense-entry: FAIL (base 0 has no available defenders)\n");
        fflush(stdout);
        return false;
    }

    /* Class and origin are declared fixture inputs. The factory owns the UFO
     * type, active state, ID, initial HP sentinel, and other record fields. */
    pUfo = StrategList_AddUfoAt(&pW->strategList, &pW->nStrategSeed,
                                pW->view.nMode, 150, 250, 2);
    if (pUfo == NULL) {
        printf("pocketufo: campaign base-defense-entry: FAIL (UFO factory returned NULL)\n");
        fflush(stdout);
        return false;
    }

    /* Race 0 is the chosen fixture race. Keep the native AddUfoAt nHp == -1
     * value: ordinary RollCraftStats also leaves HP alone. */
    pUfo->bRace = 0;
    nItemIndex = PtrArray_GetCount(&pW->strategList) - 1;
    nEventSeed = pUfo->nSeed;
    nTimeStart = pW->geo.time.nTotalSec;
    if (pUfo->nType != 1 || pUfo->bActive == 0 || pUfo->bCraftClass != 2
        || pUfo->bRace != 0 || pUfo->nHp != -1) {
        printf("pocketufo: campaign base-defense-entry: FAIL (factory record does not match class 2, race 0, HP sentinel -1)\n");
        fflush(stdout);
        return false;
    }

    /* This live callback is also used by StrategList_RollCraftStats. Its World
     * thunk builds SpawnMissionCtx and calls Geoscape_SpawnMissionSite(item, 6). */
    nSpawnResult = pW->spawnCtx.pfnSpawnMission(pUfo, 6, pW->spawnCtx.pUser);
    if (pUfo->nType != 1 || pUfo->bActive == 0 || pUfo->bCraftClass != 2
        || pUfo->bRace != 0 || pUfo->nHp != -1
        || pUfo->nMissionSub != 6 || pUfo->nPhase != 4) {
        printf("pocketufo: campaign base-defense-entry: FAIL (native subtype-6 start did not leave the active phase-4 event)\n");
        fflush(stdout);
        return false;
    }

    printf("pocketufo: campaign base-defense-entry: staged fixture item %d seed %d origin %d/%d "
           "class %u race %u strategicHp %d subtype %u phase %d result %d baseCount %d defenders %d\n",
           (int)nItemIndex, (int)pUfo->nSeed, (int)pUfo->nX, (int)pUfo->nY,
           (unsigned)pUfo->bCraftClass, (unsigned)pUfo->bRace, (int)pUfo->nHp,
           (unsigned)pUfo->nMissionSub, (int)pUfo->nPhase,
           (int)nSpawnResult, (int)pW->nBaseCount, (int)nAssigned);
    fflush(stdout);

    /* Let the original hourly/event path choose the base, travel there, and
     * run the real Base At update. Do not click until its normal confirm and
     * mission-follows state are both live. */
    for (nPulse = 0; nPulse < nPulseLimit; ++nPulse) {
        if (pW->screen.nGeoscapeScreen == 20
            && pW->bBaseAtConfirmShown != 0 && pW->bBaseAtMissionFollows != 0) {
            break;
        }
        if (pW->screen.nGeoscapeScreen == 0 && pW->geo.time.bPaused != 0) {
            printf("pocketufo: campaign base-defense-entry: FAIL (screen 0 clock paused before Base At)\n");
            App_ScriptCampaignBaseDefenseTrace(pW, nEventSeed, nPulse, nTimeStart,
                                                "paused-screen0");
            App_ScriptLines();
            return false;
        }
        if (!g_bWorldReady || pW->bGeoscapeUi == 0
            || (pW->screen.nGeoscapeScreen != 0 && pW->screen.nGeoscapeScreen != 20)
            || pW->nBaseCount != 1) {
            printf("pocketufo: campaign base-defense-entry: FAIL (event left the geoscape/base-attack route)\n");
            App_ScriptCampaignBaseDefenseTrace(pW, nEventSeed, nPulse, nTimeStart,
                                                "left-base-attack-route");
            fflush(stdout);
            return false;
        }
        App_ScriptTicks(1); /* real C353 geoscape pulse, then its normal animation/draw path */
    }
    if (pW->screen.nGeoscapeScreen != 20 || pW->bBaseAtConfirmShown == 0
        || pW->bBaseAtMissionFollows == 0 || pW->nBaseAtClass != 2
        || pW->nAssaultBaseRace != 0 || pW->nCraftHomeBase != 0
        || pW->nBaseAtStrategIdx != nEventSeed) {
        printf("pocketufo: campaign base-defense-entry: FAIL (no confirmed Base At mission after %d real pulses; "
               "screen %d confirm %u follows %u class %d race %d base %d seed %d/%d)\n",
               (int)nPulse, (int)pW->screen.nGeoscapeScreen,
               (unsigned)pW->bBaseAtConfirmShown, (unsigned)pW->bBaseAtMissionFollows,
               (int)pW->nBaseAtClass, (int)pW->nAssaultBaseRace, (int)pW->nCraftHomeBase,
               (int)pW->nBaseAtStrategIdx, (int)nEventSeed);
        App_ScriptCampaignBaseDefenseTrace(pW, nEventSeed, nPulse, nTimeStart,
                                            "base-at-timeout");
        fflush(stdout);
        return false;
    }

    printf("pocketufo: campaign base-defense-entry: PASS Base At screen %d confirm %u follows %u "
           "class %d race %d targetBase %d seed %d pulses %d tickSec %d seconds %d->%d\n",
           (int)pW->screen.nGeoscapeScreen, (unsigned)pW->bBaseAtConfirmShown,
           (unsigned)pW->bBaseAtMissionFollows, (int)pW->nBaseAtClass,
           (int)pW->nAssaultBaseRace, (int)pW->nCraftHomeBase,
           (int)pW->nBaseAtStrategIdx, (int)nPulse, (int)pW->geo.world.nTickSeconds,
           (int)nTimeStart,
           (int)pW->geo.time.nTotalSec);
    fflush(stdout);
    return true;
}

/* Wait only by running the app's real pulse, animation, and paint path. The fixed
 * click is issued only after the real flag-23 line is present and its craft/target
 * slots still identify the active intercept. */
static bool App_ScriptCampaignLaunchContact(void)
{
    const i32 nPulseLimit = 1200;
    i32 nPulse;

    if (!g_bWorldReady || g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0) {
        printf("pocketufo: campaign arrival: FAIL (need geoscape screen 0)\n");
        App_ScriptCampaignTrace("launch-precondition");
        fflush(stdout);
        return false;
    }
    if (g_world.spawnCtx.pfnSpawnMission == NULL || g_world.spawnCtx.pfnProcessEvents == NULL
        || g_world.spawnCtx.pUser != &g_world || g_world.spawnCtx.pnSeed != &g_world.nStrategSeed) {
        printf("pocketufo: campaign arrival: FAIL (event context is not wired)\n");
        App_ScriptCampaignTrace("launch-event-context");
        fflush(stdout);
        return false;
    }
    for (nPulse = 0; nPulse <= nPulseLimit; ++nPulse) {
        i32 nOffer = -1;
        for (i32 i = 0; i < (i32)g_world.msgLines.nLineCount; ++i) {
            if (g_world.msgLines.abLineFlags[i] == 23) {
                nOffer = i;
                break;
            }
        }
        if (nOffer >= 0) {
            const i32 nCraftIdx = (i32)g_world.msgLines.anColor[nOffer][0];
            const i32 nTargetIdx = (i32)g_world.msgLines.anColor[nOffer][1];
            Ship *pCraft = (nCraftIdx >= 0 && nCraftIdx < (i32)g_world.bCraftCount)
                               ? g_world.apCraft[nCraftIdx] : NULL;
            const i32 nExpectedCrew = (nCraftIdx >= 0 && nCraftIdx < (i32)g_world.bCraftCount)
                ? App_ScriptCampaignTraceRoster("before_launch", nCraftIdx) : 0;
            const bool bPairMatches = pCraft != NULL && pCraft->bIntercepting != 0
                && pCraft->nStrategIdx == nTargetIdx && nTargetIdx >= 0
                && nTargetIdx < PtrArray_GetCount(&g_world.strategList)
                && PtrArray_Get(&g_world.strategList, nTargetIdx) != NULL && nExpectedCrew > 0;

            printf("pocketufo: campaign arrival: flag23 line %d slots craft %d target %d pairMatch %d assignedCrew %d missionMode %u after %d real pulses\n",
                   (int)nOffer, (int)nCraftIdx, (int)nTargetIdx, bPairMatches ? 1 : 0,
                   (int)nExpectedCrew, (unsigned)g_world.bMapMissionMode, (int)nPulse);
            fflush(stdout);
            if (!bPairMatches) {
                App_ScriptCampaignTrace("stale-or-wrong-offer");
                return false;
            }
            App_ScriptPointerLogical(120, 102, true);
            App_OnPointerUp();
            (void)App_DrawFrame();
            GameMap *pMap = g_world.pTactMap;
            if (g_world.bTactLive != 0 && pMap != NULL && pMap->nEnemyCount > 0
                && g_world.bMapPlayerInput != 0 && pMap->nXcomCount == nExpectedCrew
                && g_world.nUiScreen == 1u) {
                printf("pocketufo: campaign arrival: PASS inventory screen with xcom %d assignedCrew %d enemy %d input %d\n",
                       (int)pMap->nXcomCount, (int)nExpectedCrew,
                       (int)pMap->nEnemyCount, (int)g_world.bMapPlayerInput);
                fflush(stdout);
                return true;
            }
            if (g_world.bTactLive != 0 && pMap != NULL && pMap->nXcomCount == nExpectedCrew
                && pMap->nEnemyCount > 0 && g_world.bMapPlayerInput != 0
                && pMap->pSelUnit != NULL && pMap->pSelUnit->nUnitType > 3u
                && g_world.nUiScreen == 0u) {
                printf("pocketufo: campaign arrival: PASS native screen0 path selected type %u, xcom %d assignedCrew %d enemy %d input %d\n",
                       (unsigned)pMap->pSelUnit->nUnitType, (int)pMap->nXcomCount,
                       (int)nExpectedCrew, (int)pMap->nEnemyCount, (int)g_world.bMapPlayerInput);
                fflush(stdout);
                return true;
            }
            printf("pocketufo: campaign arrival: FAIL launched state screen %u tact %d map %d xcom %d assignedCrew %d enemy %d input %d\n",
                   (unsigned)g_world.nUiScreen, (int)g_world.bTactLive, pMap != NULL,
                   pMap != NULL ? (int)pMap->nXcomCount : -1,
                   (int)nExpectedCrew,
                   pMap != NULL ? (int)pMap->nEnemyCount : -1, (int)g_world.bMapPlayerInput);
            App_ScriptCampaignTrace("launched-state-fail");
            fflush(stdout);
            return false;
        }
        if (nPulse == nPulseLimit) {
            break;
        }
        App_ScriptTicks(1);
        if ((nPulse + 1) % 120 == 0) {
            App_ScriptCampaignTrace("arrival-wait");
        }
    }
    printf("pocketufo: campaign arrival: FAIL no flag23 after %d real pulses\n", (int)nPulseLimit);
    App_ScriptCampaignTrace("arrival-timeout");
    fflush(stdout);
    return false;
}

/* Hit a live screen control through the same pointer and release path as a user. */
static bool App_ScriptCampaignClickGeoControl(UiControl *pCtl, const char *pszName)
{
    i32 nX, nY;

    if (pCtl == NULL || pCtl->bEnabled == 0 || pCtl->uWidth == 0 || pCtl->uHeight == 0) {
        printf("pocketufo: campaign control click: FAIL %s control %d enabled %d\n",
               (pszName != NULL) ? pszName : "requested", pCtl != NULL,
               pCtl != NULL ? (int)pCtl->bEnabled : 0);
        fflush(stdout);
        return false;
    }
    nX = (i32)pCtl->x + (i32)pCtl->uWidth / 2;
    nY = (i32)pCtl->y + (i32)pCtl->uHeight / 2;
    printf("pocketufo: campaign control click: %s at %d/%d rect %d/%d/%u/%u enabled %u mode %u state %u callback %d\n",
           (pszName != NULL) ? pszName : "requested", (int)nX, (int)nY,
           (int)pCtl->x, (int)pCtl->y, (unsigned)pCtl->uWidth, (unsigned)pCtl->uHeight,
           (unsigned)pCtl->bEnabled, (unsigned)pCtl->nMode, (unsigned)pCtl->nState,
           pCtl->pfnClick != NULL ? 1 : 0);
    fflush(stdout);
    App_ScriptPointerLogical(nX, nY, true);
    printf("pocketufo: campaign control click: %s after down state %u app/world screen %d/%d geo %d pointer screen %d inputGeo %u lineFlag %u\n",
           (pszName != NULL) ? pszName : "requested", (unsigned)pCtl->nState,
           (int)g_nUiScreen, (int)g_world.nUiScreen, (int)g_world.screen.nGeoscapeScreen,
           g_world.ptrEnv.pnScreenId != NULL ? (int)*g_world.ptrEnv.pnScreenId : -1,
           (unsigned)g_tactState.bGeoscapeUi,
           g_world.msgLines.nLineCount != 0 ? (unsigned)g_world.msgLines.abLineFlags[0] : 0u);
    fflush(stdout);
    App_OnPointerUp();
    (void)App_DrawFrame();
    printf("pocketufo: campaign control click: %s after up state %u app/world screen %d/%d geo %d\n",
           (pszName != NULL) ? pszName : "requested", (unsigned)pCtl->nState,
           (int)g_nUiScreen, (int)g_world.nUiScreen, (int)g_world.screen.nGeoscapeScreen);
    fflush(stdout);
    return true;
}

/* Cold-load input regression: open the base control at root slot 10, open the
 * soldier screen through real nav slot 2, then close it through its screen control. */
static bool App_ScriptCampaignNavCycle(void)
{
    UiControl *pBuild = g_world.ui.apRootCtl[10];

    if (!g_bWorldReady || g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.nSelectedBase != 0 || g_world.bTactLive != 0) {
        printf("pocketufo: campaign navcycle: FAIL (need the loaded base-0 geoscape)\n");
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign navcycle pre: app/world screen %d/%d geo %u screen %d base %d/%d lines %d flag0 %u "
           "anim %d/%u root10 %d/%u mode %u state %u callback %d ptrRoot %d ptrHit %d nav2fn %d nav2ctl %d\n",
           (int)g_nUiScreen, (int)g_world.nUiScreen, (unsigned)g_world.bGeoscapeUi,
           (int)g_world.screen.nGeoscapeScreen, (int)g_world.nSelectedBase, (int)g_world.nBaseCount,
           (int)g_world.msgLines.nLineCount,
           g_world.msgLines.nLineCount != 0 ? (unsigned)g_world.msgLines.abLineFlags[0] : 0u,
           (int)g_world.cUiAnimProgress, (unsigned)g_world.bUiAnimStarted,
           pBuild != NULL, pBuild != NULL ? (unsigned)pBuild->bEnabled : 0u,
           pBuild != NULL ? (unsigned)pBuild->nMode : 0u,
           pBuild != NULL ? (unsigned)pBuild->nState : 0u,
           pBuild != NULL && pBuild->pfnClick != NULL,
           g_world.ptrEnv.apRootCtl != NULL,
           g_world.ptrEnv.pHit != NULL,
           g_world.navEnv.apfnNavClick[2] != NULL, g_world.nav.apNav[2] != NULL);
    fflush(stdout);
    (void)App_DrawFrame();
    if (!App_ScriptCampaignClickGeoControl(pBuild, "root10")
        || g_world.screen.nGeoscapeScreen != 1 || g_world.nav.apNav[2] == NULL) {
        printf("pocketufo: campaign navcycle: FAIL root10 screen %d soldierNav %d\n",
               (int)g_world.screen.nGeoscapeScreen, g_world.nav.apNav[2] != NULL);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignClickGeoControl(g_world.nav.apNav[2], "soldier-nav2")
        || g_world.screen.nGeoscapeScreen != 4) {
        printf("pocketufo: campaign navcycle: FAIL soldier screen %d\n",
               (int)g_world.screen.nGeoscapeScreen);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignClickGeoControl(g_world.ui.apCtl[GEOUI_CTL_186B00], "soldier-close")
        || g_world.screen.nGeoscapeScreen != 1) {
        printf("pocketufo: campaign navcycle: FAIL first close screen %d\n",
               (int)g_world.screen.nGeoscapeScreen);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignClickGeoControl(g_world.ui.apCtl[GEOUI_CTL_186B00], "base-close")
        || g_world.screen.nGeoscapeScreen != 0) {
        printf("pocketufo: campaign navcycle: FAIL second close screen %d\n",
               (int)g_world.screen.nGeoscapeScreen);
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign navcycle: PASS root10 0->1 nav2 1->4 close 4->1->0\n");
    fflush(stdout);
    return true;
}

static bool App_ScriptCampaignDeploy(void)
{
    GameMap *pMap = g_world.pTactMap;

    if (!World_bTactPointerLive(&g_world) || pMap == NULL) {
        printf("pocketufo: campaign deploy: FAIL (need the live mission inventory screen)\n");
        fflush(stdout);
        return false;
    }
    if (g_world.nUiScreen == 0u && pMap->nXcomCount > 0 && pMap->nEnemyCount > 0
        && g_world.bMapPlayerInput != 0 && pMap->pSelUnit != NULL
        && pMap->pSelUnit->nUnitType > 3u) {
        printf("pocketufo: campaign deploy: already active screen 0, xcom %d enemies %d selected type %u input %d\n",
               (int)pMap->nXcomCount, (int)pMap->nEnemyCount,
               (unsigned)pMap->pSelUnit->nUnitType, (int)g_world.bMapPlayerInput);
        fflush(stdout);
        return true;
    }
    if (g_world.nUiScreen != 1u) {
        printf("pocketufo: campaign deploy: FAIL (need screen 1 or the native active screen 0 path)\n");
        fflush(stdout);
        return false;
    }
    (void)App_ScriptCampaignKey(PUFO_KEY_CONFIRM);
    (void)App_DrawFrame();
    pMap = g_world.pTactMap;
    if (g_world.nUiScreen != 0u || pMap == NULL || pMap->nXcomCount == 0 || pMap->nEnemyCount == 0) {
        printf("pocketufo: campaign deploy: FAIL (screen %d xcom %d enemy %d)\n",
               (int)g_world.nUiScreen, pMap != NULL ? (int)pMap->nXcomCount : -1,
               pMap != NULL ? (int)pMap->nEnemyCount : -1);
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign deploy: committed %d XCOM against %d enemies, queue %d, selected id %d\n",
           (int)pMap->nXcomCount, (int)pMap->nEnemyCount,
           g_world.pTactState != NULL ? (int)g_world.pTactState->nQueueLen : -1,
           pMap->pSelUnit != NULL ? (int)pMap->pSelUnit->nSaveId : -1);
    fflush(stdout);
    return true;
}

enum { kNormalGroundExpectedCrew = 8, kNormalGroundMaxFlightPulses = 600 };

static void App_ScriptNormalGroundGeoObserve(const char *pszStage, i32 nPulses,
                                              i32 nSiteSeed, u8 bCarrierId)
{
    const i32 nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    const i32 nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    const StrategItem *pSite = nSiteIdx >= 0
        ? (const StrategItem *)g_world.strategList.apItems[nSiteIdx] : NULL;
    const Ship *pCarrier = nCarrierIdx >= 0 ? g_world.apCraft[nCarrierIdx] : NULL;

    printf("pocketufo: normal-ground state stage=%s pulses=%d clock=%d tickSec=%d paused=%u screen=%d mapMode=%d geoUi=%u modal=%u radarActive=%u lineCount=%u lineFlag=%d slot0=%u slot1=%u selection=%u selectedShip=%d tactLive=%u tactFlag=%u\n",
           pszStage, (int)nPulses, (int)g_world.geo.time.nTotalSec,
           (int)g_world.geo.world.nTickSeconds, (unsigned)g_world.geo.time.bPaused,
           (int)g_world.screen.nGeoscapeScreen, (int)g_world.view.nMode,
           (unsigned)g_world.bGeoscapeUi,
           (unsigned)g_world.sModal.bOpen, (unsigned)g_world.bRadarScreenActive,
           (unsigned)g_world.msgLines.nLineCount, (int)App_ScriptNormalLine0Flag(),
           g_world.msgLines.nLineCount > 0 ? (unsigned)g_world.msgLines.anColor[0][0] : 0u,
           g_world.msgLines.nLineCount > 0 ? (unsigned)g_world.msgLines.anColor[0][1] : 0u,
           (unsigned)g_world.bMapSelectionFlag, (int)g_world.nSelectedShipIdx,
           g_world.bTactLive ? 1u : 0u, (unsigned)g_world.bTactFlag);
    printf("pocketufo: normal-ground site stage=%s seedExpected=%d currentIndex=%d seed=%d present=%d type=%d active=%u visible=%u x=%d y=%d timer=%d phase=%d sub=%u hp=%d\n",
           pszStage, (int)nSiteSeed, (int)nSiteIdx,
           pSite != NULL ? (int)pSite->nSeed : -1, pSite != NULL ? 1 : 0,
           pSite != NULL ? (int)pSite->nType : -1,
           pSite != NULL ? (unsigned)pSite->bActive : 255u,
           pSite != NULL ? (unsigned)pSite->bVisible : 255u,
           pSite != NULL ? (int)pSite->nX : -1,
           pSite != NULL ? (int)pSite->nY : -1,
           pSite != NULL ? (int)pSite->nTimer : -999999,
           pSite != NULL ? (int)pSite->nPhase : -1,
           pSite != NULL ? (unsigned)pSite->nMissionSub : 255u,
           pSite != NULL ? (int)pSite->nHp : -999999);
    printf("pocketufo: normal-ground carrier stage=%s expectedId=%u currentIndex=%d id=%d type=%d atBase=%u intercept=%u targetIndex=%d fuel=%d fuelMax=%u rangeRemain=%u rangeMax=%u assignedCrew=%d\n",
           pszStage, (unsigned)bCarrierId, (int)nCarrierIdx,
           pCarrier != NULL ? (int)pCarrier->bId : -1,
           pCarrier != NULL ? (int)pCarrier->bCraftType : -1,
           pCarrier != NULL ? (unsigned)pCarrier->bAtBase : 255u,
           pCarrier != NULL ? (unsigned)pCarrier->bIntercepting : 255u,
           pCarrier != NULL ? (int)pCarrier->nStrategIdx : -1,
           pCarrier != NULL ? (int)pCarrier->nFuel : -1,
           pCarrier != NULL ? (unsigned)pCarrier->uFuelMax : 0u,
           pCarrier != NULL ? (unsigned)pCarrier->uRangeRemain : 0u,
           pCarrier != NULL ? (unsigned)pCarrier->uRangeMax : 0u,
           pCarrier != NULL
               ? (int)Craft_CountSoldiersAboard(g_world.apSoldiers,
                                                g_world.nSoldierCount, nCarrierIdx)
               : 0);
    fflush(stdout);
}

static bool App_ScriptNormalGroundTactOwnersMatch(uintptr_t nMapKey,
                                                   uintptr_t nQueueKey,
                                                   GameMap **ppMap,
                                                   TactState **ppQueue)
{
    GameMap *pMap = g_world.pTactMap;
    TactState *pQueue = g_world.pTactState;

    if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
        || (uintptr_t)(void *)pMap != nMapKey
        || (uintptr_t)(void *)pQueue != nQueueKey) {
        return false;
    }
    if (ppMap != NULL) { *ppMap = pMap; }
    if (ppQueue != NULL) { *ppQueue = pQueue; }
    return true;
}

static bool App_ScriptNormalGroundTacticalIdentityMatches(i32 nSiteSeed,
                                                            u8 bCarrierId,
                                                            i32 *pnSiteIdx,
                                                            i32 *pnCarrierIdx)
{
    const i32 nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    const i32 nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    const StrategItem *pSite = nSiteIdx >= 0
        ? (const StrategItem *)g_world.strategList.apItems[nSiteIdx] : NULL;
    const Ship *pCarrier = nCarrierIdx >= 0 ? g_world.apCraft[nCarrierIdx] : NULL;

    if (pSite == NULL || pSite->nSeed != nSiteSeed || pSite->nType != 4
        || pSite->bActive == 0 || pCarrier == NULL || pCarrier->bId != bCarrierId
        || pCarrier->bCraftType != CRAFT_TYPE_SKYRANGER
        || g_world.nMapCraftIndex != (u8)nCarrierIdx) {
        return false;
    }
    if (pnSiteIdx != NULL) { *pnSiteIdx = nSiteIdx; }
    if (pnCarrierIdx != NULL) { *pnCarrierIdx = nCarrierIdx; }
    return true;
}

static void App_ScriptNormalGroundTactObserve(const char *pszStage,
                                              i32 nSiteSeed, u8 bCarrierId,
                                              i32 nExpectedCrew)
{
    const GameMap *pMap = g_world.pTactMap;
    const TactState *pQueue = g_world.pTactState;
    const Soldier *pSelected = pMap != NULL ? pMap->pSelUnit : NULL;
    const i32 nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    const i32 nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    const Ship *pCarrier = nCarrierIdx >= 0 ? g_world.apCraft[nCarrierIdx] : NULL;
    const StrategItem *pSite = nSiteIdx >= 0
        ? (const StrategItem *)g_world.strategList.apItems[nSiteIdx] : NULL;

    printf("pocketufo: normal-ground tactical stage=%s screen=%u tactLive=%u tactFlag=%u mapPresent=%d queuePresent=%d mapKey=%llu queueKey=%llu lineCount=%u lineFlag=%d modal=%u expectedSiteSeed=%d siteCurrentIndex=%d siteSeed=%d siteType=%d siteActive=%u siteVisible=%u siteTimer=%d carrierExpectedId=%u carrierCurrentIndex=%d carrierId=%d carrierType=%d carrierAtBase=%u carrierIntercept=%u carrierTargetIndex=%d mapCraftIndex=%u selectedStrategIndex=%d missionType=%u mapMissionType=%u missionMode=%u metadataLayers=%u backingLayers=%u tierRaw=%d terrainVariant=%u xcomCount=%u enemyCount=%u expectedCrew=%d queueLen=%d queueCursor=%d queueActive=%u phase=%d busy=%u playerInput=%u enemyTurn=%u battleEnded=%u selectedUnitSaveId=%d selectedUnitType=%u selectedCellX=%d selectedCellY=%d selectedCellZ=%d\n",
           pszStage, (unsigned)g_world.nUiScreen, g_world.bTactLive ? 1u : 0u,
           (unsigned)g_world.bTactFlag, pMap != NULL, pQueue != NULL,
           (unsigned long long)(uintptr_t)(void *)pMap,
           (unsigned long long)(uintptr_t)(void *)pQueue,
           (unsigned)g_world.msgLines.nLineCount,
           (int)App_ScriptNormalLine0Flag(), (unsigned)g_world.sModal.bOpen,
           (int)nSiteSeed, (int)nSiteIdx,
           pSite != NULL ? (int)pSite->nSeed : -1,
           pSite != NULL ? (int)pSite->nType : -1,
           pSite != NULL ? (unsigned)pSite->bActive : 255u,
           pSite != NULL ? (unsigned)pSite->bVisible : 255u,
           pSite != NULL ? (int)pSite->nTimer : -999999,
           (unsigned)bCarrierId, (int)nCarrierIdx,
           pCarrier != NULL ? (int)pCarrier->bId : -1,
           pCarrier != NULL ? (int)pCarrier->bCraftType : -1,
           pCarrier != NULL ? (unsigned)pCarrier->bAtBase : 255u,
           pCarrier != NULL ? (unsigned)pCarrier->bIntercepting : 255u,
           pCarrier != NULL ? (int)pCarrier->nStrategIdx : -1,
           (unsigned)g_world.nMapCraftIndex, (int)g_world.nSelectedStrategIdx,
           pMap != NULL ? (unsigned)pMap->nMissionType : 255u,
           (unsigned)g_world.nMapMissionType, (unsigned)g_world.bMapMissionMode,
           pMap != NULL ? (unsigned)pMap->nMapLayers : 0u,
           (unsigned)PUFO_MAP_LAYERS,
           (int)g_world.nMapMissionTierRaw, (unsigned)g_world.nMapTerrainVariant,
           pMap != NULL ? (unsigned)pMap->nXcomCount : 255u,
           pMap != NULL ? (unsigned)pMap->nEnemyCount : 255u,
           (int)nExpectedCrew,
           pQueue != NULL ? (int)pQueue->nQueueLen : -1,
           pQueue != NULL ? (int)pQueue->nQueueCursor : -1,
           pQueue != NULL ? (unsigned)pQueue->bQueueActive : 255u,
           pMap != NULL ? (int)pMap->tact.nPhase : -1,
           pMap != NULL ? (unsigned)pMap->tact.bBusy : 255u,
           (unsigned)g_world.bMapPlayerInput,
           (unsigned)g_world.bMapEnemyTurnActive,
           (unsigned)g_world.bMapBattleEnded,
           pSelected != NULL ? (int)pSelected->nSaveId : -1,
           pSelected != NULL ? (unsigned)pSelected->nUnitType : 255u,
           pSelected != NULL && pSelected->pCell != NULL ? (int)pSelected->pCell->nX : -1,
           pSelected != NULL && pSelected->pCell != NULL ? (int)pSelected->pCell->nY : -1,
           pSelected != NULL && pSelected->pCell != NULL ? (int)pSelected->pCell->nZ : -1);
    if (pMap != NULL) {
        for (i32 i = 0; i < (i32)pMap->nXcomCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
            const Soldier *pUnit = pMap->apXcomUnits[i];
            const MapCell *pCell = pUnit != NULL ? pUnit->pCell : NULL;
            const bool bCellXYBounds = pCell != NULL
                && pCell->nX < PUFO_CELLS_PER_TILE * pMap->geom.nDimX
                && pCell->nY < PUFO_CELLS_PER_TILE * pMap->geom.nDimY
                && pCell->nX < PUFO_MAP_CELL_DIM && pCell->nY < PUFO_MAP_CELL_DIM;
            const bool bCellBackingZ = pCell != NULL && pCell->nZ < PUFO_MAP_LAYERS;
            const bool bCellOwner = bCellXYBounds && bCellBackingZ
                && App_ScriptCampaignCellAt(pCell->nX, pCell->nY, pCell->nZ) == pCell;
            const bool bCellOccupant = pCell != NULL && pCell->pUnit == pUnit;
            const bool bCellValid = bCellXYBounds && bCellBackingZ && bCellOwner;
            bool bCellUnique = pCell != NULL;
            for (i32 j = 0; j < i && bCellUnique; ++j) {
                const Soldier *pEarlier = pMap->apXcomUnits[j];
                const MapCell *pEarlierCell = pEarlier != NULL ? pEarlier->pCell : NULL;
                if (pEarlierCell == pCell
                    || (pEarlierCell != NULL && pEarlierCell->nX == pCell->nX
                        && pEarlierCell->nY == pCell->nY
                        && pEarlierCell->nZ == pCell->nZ)) {
                    bCellUnique = false;
                }
            }
            printf("pocketufo: normal-ground unit stage=%s index=%d unitPresent=%d saveId=%d unitKey=%llu unitType=%u hp=%u status=%u cellPresent=%d cellX=%d cellY=%d cellZ=%d cellXYBounds=%d cellBackingZ=%d cellOwner=%d cellOccupant=%d cellValid=%d cellUnique=%d\n",
                   pszStage, (int)i, pUnit != NULL,
                   pUnit != NULL ? (int)pUnit->nSaveId : -1,
                   (unsigned long long)(uintptr_t)(void *)pUnit,
                   pUnit != NULL ? (unsigned)pUnit->nUnitType : 255u,
                   pUnit != NULL ? (unsigned)pUnit->uHp : 0u,
                   pUnit != NULL ? (unsigned)pUnit->bStatus : 255u,
                   pCell != NULL,
                   pCell != NULL ? (int)pCell->nX : -1,
                   pCell != NULL ? (int)pCell->nY : -1,
                   pCell != NULL ? (int)pCell->nZ : -1,
                   bCellXYBounds ? 1 : 0, bCellBackingZ ? 1 : 0, bCellOwner ? 1 : 0,
                   bCellOccupant ? 1 : 0, bCellValid ? 1 : 0,
                   bCellUnique ? 1 : 0);
        }
    }
    fflush(stdout);
}

/* The live 4x60x60 apTactCells table bounds cell ownership. The original craft
 * deploy record at 0xA66B8 has z=1 even with Map.nMapLayers=1, so that field is
 * not a valid maximum cell z. Keep it as metadata; check the backing table and
 * actual cell owner instead. Original deploy phase 3 @ 0x39F50 checks only
 * MapCell_HasOccupant; Unit_PlaceOnMap stores the occupant through
 * MapCell_SetOccupant @ 0x548B8, with no floor-sprite test.
 *
 * The original selector at 0x3983C tests Soldier.bUnk183 against Map.nCraftIndex,
 * then stores the same Soldier pointer in Map.apXcomUnits at 0x39880. SaveGame's
 * only call to Save_AssignRosterIndices is at 0x44A30; that routine writes nSaveId
 * only behind its Map.bTactFlag gate at 0x42EAC. Track the live roster slot and
 * pointer instead of treating that save field as a New Game identity. */
static bool App_ScriptNormalGroundVerifyCrew(GameMap *pMap,
                                             const Soldier *const *apExpectedCrew,
                                             const i32 *panExpectedRosterIndices,
                                             i32 nExpectedCount, i32 nCarrierIdx,
                                             const char *pszStage)
{
    bool abSeen[kNormalGroundExpectedCrew] = { false };
    i32 nValid = 0;

    if (pMap == NULL || apExpectedCrew == NULL || panExpectedRosterIndices == NULL
        || nExpectedCount != kNormalGroundExpectedCrew
        || pMap->nXcomCount != (u8)kNormalGroundExpectedCrew) {
        return false;
    }
    for (i32 i = 0; i < kNormalGroundExpectedCrew; ++i) {
        const Soldier *pUnit = pMap->apXcomUnits[i];
        const MapCell *pCell = pUnit != NULL ? pUnit->pCell : NULL;
        i32 nExpectedIndex = -1;
        bool bCellUnique = pCell != NULL;
        const bool bCellXYBounds = pCell != NULL
            && pCell->nX < PUFO_CELLS_PER_TILE * pMap->geom.nDimX
            && pCell->nY < PUFO_CELLS_PER_TILE * pMap->geom.nDimY
            && pCell->nX < PUFO_MAP_CELL_DIM && pCell->nY < PUFO_MAP_CELL_DIM;
        const bool bCellBackingZ = pCell != NULL && pCell->nZ < PUFO_MAP_LAYERS;
        const bool bCellOwner = bCellXYBounds && bCellBackingZ
            && App_ScriptCampaignCellAt(pCell->nX, pCell->nY, pCell->nZ) == pCell;
        const bool bCellValid = bCellXYBounds && bCellBackingZ && bCellOwner;
        const bool bCellOccupant = pUnit != NULL && pCell != NULL && pCell->pUnit == pUnit;

        if (pUnit != NULL) {
            for (i32 j = 0; j < nExpectedCount; ++j) {
                if (pUnit == apExpectedCrew[j]) {
                    nExpectedIndex = j;
                    break;
                }
            }
        }
        if (nExpectedIndex >= 0) {
            if (abSeen[nExpectedIndex]) {
                nExpectedIndex = -1;
            } else {
                abSeen[nExpectedIndex] = true;
            }
        }
        for (i32 j = 0; j < i && bCellUnique; ++j) {
            const Soldier *pEarlier = pMap->apXcomUnits[j];
            if (pEarlier != NULL && pEarlier->pCell != NULL
                && (pEarlier->pCell == pCell
                    || (pEarlier->pCell->nX == pCell->nX
                        && pEarlier->pCell->nY == pCell->nY
                        && pEarlier->pCell->nZ == pCell->nZ))) {
                bCellUnique = false;
            }
        }
        const bool bExpected = nExpectedIndex >= 0;
        const i32 nRosterIndex = bExpected ? panExpectedRosterIndices[nExpectedIndex] : -1;
        const bool bSourceMatch = bExpected && nRosterIndex >= 0
            && nRosterIndex < g_world.nSoldierCount
            && g_world.apSoldiers[nRosterIndex] == apExpectedCrew[nExpectedIndex];
        const bool bCraftMatch = pUnit != NULL && nCarrierIdx >= 0
            && nCarrierIdx < (i32)g_world.bCraftCount
            && pUnit->bUnk183 == (u8)nCarrierIdx;
        const bool bValid = pUnit != NULL && bExpected && bSourceMatch
            && bCraftMatch && bCellValid && bCellOccupant && bCellUnique;
        nValid += bValid ? 1 : 0;
        printf("pocketufo: normal-ground unit-check stage=%s index=%d unitPresent=%d saveId=%d unitKey=%llu expectedRosterIndex=%d expected=%d sourceMatch=%d assignedCraft=%u craftMatch=%d hp=%u status=%u cellX=%d cellY=%d cellZ=%d cellXYBounds=%d cellBackingZ=%d cellOwner=%d cellOccupant=%d cellValid=%d cellUnique=%d valid=%d\n",
               pszStage, (int)i, pUnit != NULL,
               pUnit != NULL ? (int)pUnit->nSaveId : -1,
               (unsigned long long)(uintptr_t)(void *)pUnit,
               nRosterIndex, bExpected ? 1 : 0, bSourceMatch ? 1 : 0,
               pUnit != NULL ? (unsigned)pUnit->bUnk183 : 255u, bCraftMatch ? 1 : 0,
               pUnit != NULL ? (unsigned)pUnit->uHp : 0u,
               pUnit != NULL ? (unsigned)pUnit->bStatus : 255u,
               pCell != NULL ? (int)pCell->nX : -1,
               pCell != NULL ? (int)pCell->nY : -1,
               pCell != NULL ? (int)pCell->nZ : -1,
               bCellXYBounds ? 1 : 0, bCellBackingZ ? 1 : 0, bCellOwner ? 1 : 0,
               bCellOccupant ? 1 : 0, bCellValid ? 1 : 0,
               bCellUnique ? 1 : 0, bValid ? 1 : 0);
    }
    if (nValid != kNormalGroundExpectedCrew) {
        return false;
    }
    for (i32 i = 0; i < kNormalGroundExpectedCrew; ++i) {
        if (!abSeen[i]) {
            return false;
        }
    }
    return true;
}

static bool App_ScriptNormalGroundFail(const char *pszStage, const char *pszWhy,
                                       i32 nPulses, i32 nSiteSeed, u8 bCarrierId,
                                       i32 nExpectedCrew)
{
    printf("pocketufo: normal-ground FAIL stage=%s why=%s pulses=%d siteSeed=%d carrierId=%u\n",
           pszStage, pszWhy, (int)nPulses, (int)nSiteSeed, (unsigned)bCarrierId);
    App_ScriptNormalGroundGeoObserve(pszStage, nPulses, nSiteSeed, bCarrierId);
    if (World_bTactPointerLive(&g_world)) {
        App_ScriptNormalGroundTactObserve(pszStage, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptState(pszStage);
    App_ScriptLines();
    App_ScriptStrateg();
    fflush(stdout);
    return false;
}

/* Continue only from the real site outcome saved by normalaircombat. Every world
 * click goes through the App pointer path; the helper never writes game state. */
static bool App_ScriptNormalGroundEntry(void)
{
    enum { kMaxOptionsAnimFrames = 101, kMaxDeployQueueTicks = 128 };
    const i32 nSiteSeed = g_nNormalAirGroundSiteSeed;
    i32 nSiteIdx = -1;
    i32 nBaseIdx = -1;
    i32 nCarrierIdx = -1;
    i32 nCarrierBeforeIdx = -1;
    i32 nReadyRow = -1;
    i32 nExpectedCrew = 0;
    i32 nOptionsFrames = 0;
    i32 nClockOptionsStart;
    i32 nFlightClockStart;
    i32 nShipTargetBefore = -1;
    i32 nFlightPulses = 0;
    i32 nArrivalPulse = 0;
    u8 bAtBaseBefore = 255u;
    u8 bInterceptBefore = 255u;
    const Soldier *apExpectedCrew[kNormalGroundExpectedCrew] = { NULL };
    i32 anExpectedCrewRosterIndices[kNormalGroundExpectedCrew] = { 0 };
    u8 bCarrierId = 0xFFu;

    if (!g_bNormalAirGroundSiteReady) {
        printf("pocketufo: normal-ground FAIL stage=start why=normal-air-combat-did-not-land\n");
        fflush(stdout);
        return false;
    }
    if (!g_bWorldReady || g_world.bTactLive || g_world.bRadarScreenActive != 0
        || g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
        || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
        || g_world.msgLines.nLineCount != 0 || App_ScriptNormalLine0Flag() != 0
        || g_world.view.nMode != 0
        || g_world.bMapSelectionFlag != 0 || g_world.nBaseCount <= 0
        || g_world.apBases[0] == NULL || g_world.strategList.apItems == NULL) {
        return App_ScriptNormalGroundFail("start", "need_clear_map_after_landed_site",
                                          0, nSiteSeed, bCarrierId, 0);
    }
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (nSiteIdx < 0) {
        return App_ScriptNormalGroundFail("site_start", "landed_site_item_missing",
                                          0, nSiteSeed, bCarrierId, 0);
    }
    {
        const StrategItem *pSite = (const StrategItem *)g_world.strategList.apItems[nSiteIdx];
        if (pSite == NULL || pSite->nSeed != nSiteSeed || pSite->nType != 4
            || pSite->bActive == 0 || pSite->bVisible == 0 || pSite->nTimer <= 0) {
            return App_ScriptNormalGroundFail("site_start", "landed_site_not_live_visible_type4",
                                              0, nSiteSeed, bCarrierId, 0);
        }
    }
    printf("pocketufo: normal-ground origin source=normalaircombat siteKind=type4 siteSeed=%d siteIndex=%d\n",
           (int)nSiteSeed, (int)nSiteIdx);
    App_ScriptNormalGroundGeoObserve("site_start", 0, nSiteSeed, bCarrierId);
    App_ScriptDump("normal_ground_site_before.ppm");
    printf("pocketufo: normal-ground snapshot stage=site_before file=normal_ground_site_before.ppm\n");
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
        || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
        || nSiteIdx < 0 || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nType != 4
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nSeed != nSiteSeed) {
        return App_ScriptNormalGroundFail("site_snapshot", "site_or_map_changed_after_paint",
                                          0, nSiteSeed, bCarrierId, 0);
    }
    App_ScriptNormalGroundGeoObserve("site_after_snapshot", 0, nSiteSeed, bCarrierId);

    for (i32 i = 0; i < g_world.strategList.nCount; ++i) {
        const StrategItem *pItem = (const StrategItem *)g_world.strategList.apItems[i];
        if (pItem != NULL && pItem->nType == 0 && pItem->bActive == 0
            && pItem->nSiteId == (i32)(i8)g_world.apBases[0]->bUnk4) {
            nBaseIdx = i;
            break;
        }
    }
    if (nBaseIdx < 0) {
        return App_ScriptNormalGroundFail("base_click", "first_base_marker_missing_or_not_clickable",
                                          0, nSiteSeed, bCarrierId, 0);
    }
    {
        const StrategItem *pBaseMarker =
            (const StrategItem *)g_world.strategList.apItems[nBaseIdx];
        const i32 nBaseSiteId = pBaseMarker != NULL ? pBaseMarker->nSiteId : -1;
        i32 nClickX = -1, nClickY = -1;
        if (pBaseMarker == NULL
            || !App_ScriptNormalClickStrategItem(nBaseIdx, pBaseMarker,
                                                 &nClickX, &nClickY)) {
            return App_ScriptNormalGroundFail("base_click", "first_base_marker_not_clickable",
                                              0, nSiteSeed, bCarrierId, 0);
        }
        printf("pocketufo: normal-ground input control=base_marker index=%d siteId=%d pointX=%d pointY=%d\n",
               (int)nBaseIdx, (int)nBaseSiteId, (int)nClickX, (int)nClickY);
    }
    App_DrawFrame();
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
        || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused == 0
        || g_world.msgLines.nLineCount != 0 || App_ScriptNormalLine0Flag() != 0
        || g_world.options.apPanel[0] == NULL || g_world.options.apPanel[1] == NULL
        || nSiteIdx < 0 || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nSeed != nSiteSeed
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nType != 4) {
        return App_ScriptNormalGroundFail("options_open", "real_base_click_did_not_open_screen18",
                                          0, nSiteSeed, bCarrierId, 0);
    }
    if (g_world.bUiAnimStarted == 0) {
        return App_ScriptNormalGroundFail("options_animation", "animation_not_started",
                                          0, nSiteSeed, bCarrierId, 0);
    }
    nClockOptionsStart = g_world.geo.time.nTotalSec;
    while (g_world.cUiAnimProgress < 100 && nOptionsFrames < kMaxOptionsAnimFrames) {
        if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
            || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused == 0
            || g_world.msgLines.nLineCount != 0 || App_ScriptNormalLine0Flag() != 0
            || g_world.geo.time.nTotalSec != nClockOptionsStart
            || g_world.options.apPanel[0] == NULL || g_world.options.apPanel[1] == NULL) {
            return App_ScriptNormalGroundFail("options_animation", "state_changed_while_paused",
                                              0, nSiteSeed, bCarrierId, 0);
        }
        App_GameTickOnce(NULL);
        App_TickAnimProgress();
        App_TickAnim();
        App_DrawFrame();
        ++nOptionsFrames;
        nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
        if (nSiteIdx < 0 || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nSeed != nSiteSeed
            || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nType != 4) {
            return App_ScriptNormalGroundFail("options_animation", "site_changed_during_animation",
                                              0, nSiteSeed, bCarrierId, 0);
        }
    }
    if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
        || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused == 0
        || g_world.msgLines.nLineCount != 0 || App_ScriptNormalLine0Flag() != 0
        || g_world.geo.time.nTotalSec != nClockOptionsStart
        || g_world.options.apPanel[0] == NULL || g_world.options.apPanel[1] == NULL
        || g_world.cUiAnimProgress < 100) {
        return App_ScriptNormalGroundFail("options_animation", "animation_not_complete",
                                          0, nSiteSeed, bCarrierId, 0);
    }
    printf("pocketufo: normal-ground options-animation frames=%d progress=%d clock=%d paused=%u\n",
           (int)nOptionsFrames, (int)g_world.cUiAnimProgress,
           (int)g_world.geo.time.nTotalSec, (unsigned)g_world.geo.time.bPaused);
    App_ScriptDump("normal_ground_options.ppm");
    printf("pocketufo: normal-ground snapshot stage=options file=normal_ground_options.ppm\n");
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (g_world.screen.nGeoscapeScreen != 18 || g_world.bOptionsScreenOpen == 0
        || g_world.geo.time.nTotalSec != nClockOptionsStart
        || g_world.sModal.bOpen != 0 || g_world.msgLines.nLineCount != 0
        || App_ScriptNormalLine0Flag() != 0
        || g_world.options.apPanel[0] == NULL || g_world.options.apPanel[1] == NULL
        || nSiteIdx < 0 || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nSeed != nSiteSeed
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nType != 4
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->bActive == 0) {
        return App_ScriptNormalGroundFail("options_snapshot", "options_or_clock_changed_after_paint",
                                          0, nSiteSeed, bCarrierId, 0);
    }
    App_ScriptNormalGroundGeoObserve("options_ready", 0, nSiteSeed, bCarrierId);

    {
        const UiPanel *pName = g_world.options.apPanel[0];
        const UiPanel *pStores = g_world.options.apPanel[1];
        const i32 nRowStart = pName->nUnk3028;
        i32 nRowEnd = pName->nLineCount;
        if (nRowStart < 0 || nRowStart >= PUFO_PANEL_LINES || pName->nFont <= 0) {
            return App_ScriptNormalGroundFail("carrier_row", "options_rows_not_live",
                                              0, nSiteSeed, bCarrierId, 0);
        }
        if (nRowEnd > nRowStart + pName->nStyle) { nRowEnd = nRowStart + pName->nStyle; }
        if (nRowEnd > PUFO_PANEL_LINES) { nRowEnd = PUFO_PANEL_LINES; }
        if (nRowEnd > pStores->nLineCount) { nRowEnd = pStores->nLineCount; }
        for (i32 nRow = nRowStart; nRow < nRowEnd; ++nRow) {
            const i32 nCandidate = pName->anLineAux[nRow];
            const i32 nReadyAux = pStores->anLineAux[nRow];
            const Ship *pCandidate = nCandidate >= 0 && nCandidate < (i32)g_world.bCraftCount
                ? g_world.apCraft[nCandidate] : NULL;
            const i32 nCrew = pCandidate != NULL
                ? Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, nCandidate)
                : 0;
            const bool bVisible = nRow >= pName->nUnk3028
                && nRow < pName->nUnk3028 + pName->nStyle;
            printf("pocketufo: normal-ground options-row row=%d visible=%d nameAux=%d readyAux=%d craftIndex=%d craftId=%d type=%d atBase=%d rangeRemain=%d rangeMax=%d fuel=%d fuelMax=%u assignedCrew=%d\n",
                   (int)nRow, bVisible ? 1 : 0, (int)nCandidate, (int)nReadyAux,
                   (int)nCandidate, pCandidate != NULL ? (int)pCandidate->bId : -1,
                   pCandidate != NULL ? (int)pCandidate->bCraftType : -1,
                   pCandidate != NULL ? (int)pCandidate->bAtBase : -1,
                   pCandidate != NULL ? (int)pCandidate->uRangeRemain : -1,
                   pCandidate != NULL ? (int)pCandidate->uRangeMax : -1,
                   pCandidate != NULL ? (int)pCandidate->nFuel : -1,
                   pCandidate != NULL ? (unsigned)pCandidate->uFuelMax : 0u,
                   (int)nCrew);
            if (!bVisible || pCandidate == NULL || nReadyAux != 0
                || pCandidate->bCraftType != CRAFT_TYPE_SKYRANGER
                || pCandidate->bAtBase == 0
                || pCandidate->uRangeRemain != pCandidate->uRangeMax
                || pCandidate->nFuel != (i16)pCandidate->uFuelMax
                || nCrew != kNormalGroundExpectedCrew) {
                continue;
            }
            nCarrierIdx = nCandidate;
            nReadyRow = nRow;
            nExpectedCrew = nCrew;
            bCarrierId = pCandidate->bId;
            break;
        }
        if (nCarrierIdx < 0) {
            return App_ScriptNormalGroundFail("carrier_row", "no_ready_type0_transport_with_eight_assigned_troops",
                                              0, nSiteSeed, bCarrierId, kNormalGroundExpectedCrew);
        }
        i32 nAssignedCrew = 0;
        for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
            const Soldier *pSoldier = g_world.apSoldiers[i];
            if (pSoldier == NULL || (i32)pSoldier->bUnk183 != nCarrierIdx) {
                continue;
            }
            if (nAssignedCrew >= kNormalGroundExpectedCrew) {
                return App_ScriptNormalGroundFail("carrier_roster", "too_many_assigned_troops",
                                                  0, nSiteSeed, bCarrierId, nExpectedCrew);
            }
            for (i32 j = 0; j < nAssignedCrew; ++j) {
                if (apExpectedCrew[j] == pSoldier) {
                    return App_ScriptNormalGroundFail("carrier_roster", "duplicate_assigned_soldier_pointer",
                                                      0, nSiteSeed, bCarrierId, nExpectedCrew);
                }
            }
            apExpectedCrew[nAssignedCrew] = pSoldier;
            anExpectedCrewRosterIndices[nAssignedCrew] = i;
            printf("pocketufo: normal-ground assigned-crew index=%d carrierIndex=%d carrierId=%u rosterIndex=%d soldierKey=%llu saveId=%d assignedCraft=%u\n",
                   (int)nAssignedCrew, (int)nCarrierIdx, (unsigned)bCarrierId,
                   (int)i, (unsigned long long)(uintptr_t)(const void *)pSoldier,
                   (int)pSoldier->nSaveId, (unsigned)pSoldier->bUnk183);
            ++nAssignedCrew;
        }
        if (nAssignedCrew != nExpectedCrew) {
            return App_ScriptNormalGroundFail("carrier_roster", "assigned_troop_count_changed",
                                              0, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        pName = g_world.options.apPanel[0];
        {
            const i32 nX = pName->nRectX + 45;
            const i32 nY = pName->nPosY + (nReadyRow - pName->nUnk3028) * pName->nFont
                + pName->nFont / 2;
            const Ship *pCandidate = nCarrierIdx < (i32)g_world.bCraftCount
                ? g_world.apCraft[nCarrierIdx] : NULL;
            if (pName == NULL || pCandidate == NULL
                || nX <= pName->nRectX || nX >= pName->nRectX + pName->nViewW
                || nY <= pName->nRectY || nY >= pName->nRectY + pName->nViewH
                || nX < 0 || nX >= 240 || nY < 0 || nY >= 320) {
                return App_ScriptNormalGroundFail("carrier_row", "ready_transport_row_not_visible",
                                                  0, nSiteSeed, bCarrierId, nExpectedCrew);
            }
            printf("pocketufo: normal-ground input control=options_row row=%d pointX=%d pointY=%d craftIndex=%d craftId=%u type=%u readyAux=%d assignedCrew=%d\n",
                   (int)nReadyRow, (int)nX, (int)nY, (int)nCarrierIdx,
                   (unsigned)bCarrierId, (unsigned)pCandidate->bCraftType,
                   (int)pStores->anLineAux[nReadyRow], (int)nExpectedCrew);
            App_ScriptPointerLogical(nX, nY, true);
            App_OnPointerUp();
        }
    }

    nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (nCarrierIdx < 0 || nSiteIdx < 0 || g_world.nSelectedShipIdx != nCarrierIdx
        || g_world.bMapSelectionFlag == 0 || App_ScriptNormalLine0Flag() != 27
        || g_world.msgLines.nLineCount == 0 || g_world.geo.time.bPaused == 0
        || g_world.msgLines.anColor[0][0] != (u32)nCarrierIdx
        || g_world.sModal.bOpen != 0) {
        return App_ScriptNormalGroundFail("carrier_select", "real_options_row_did_not_arm_flag27",
                                          0, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptNormalGroundGeoObserve("carrier_selected", 0, nSiteSeed, bCarrierId);
    App_DrawFrame();
    nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (g_world.screen.nGeoscapeScreen != 0 || g_world.bOptionsScreenOpen != 0
        || g_world.sModal.bOpen != 0 || g_world.geo.time.nTotalSec != nClockOptionsStart
        || g_world.geo.time.bPaused == 0 || g_world.nSelectedShipIdx != nCarrierIdx
        || g_world.bMapSelectionFlag == 0
        || App_ScriptNormalLine0Flag() != 27 || nCarrierIdx < 0 || nSiteIdx < 0) {
        return App_ScriptNormalGroundFail("carrier_select", "screen18_did_not_close_cleanly",
                                          0, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptDump("normal_ground_carrier_selected.ppm");
    printf("pocketufo: normal-ground snapshot stage=carrier_selected file=normal_ground_carrier_selected.ppm\n");
    nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (nCarrierIdx < 0 || nSiteIdx < 0 || g_world.nSelectedShipIdx != nCarrierIdx
        || App_ScriptNormalLine0Flag() != 27 || g_world.geo.time.bPaused == 0
        || g_world.geo.time.nTotalSec != nClockOptionsStart
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nType != 4
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nSeed != nSiteSeed
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->bActive == 0) {
        return App_ScriptNormalGroundFail("carrier_snapshot", "selection_or_site_changed_after_paint",
                                          0, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptNormalGroundGeoObserve("carrier_selected_after_snapshot", 0,
                                     nSiteSeed, bCarrierId);

    {
        const StrategItem *pSite = (const StrategItem *)g_world.strategList.apItems[nSiteIdx];
        i32 nClickX = -1, nClickY = -1;
        if (pSite == NULL || pSite->nSeed != nSiteSeed || pSite->nType != 4
            || pSite->bActive == 0 || pSite->bVisible == 0 || pSite->nTimer <= 0
            || !App_ScriptNormalClickStrategItem(nSiteIdx, pSite, &nClickX, &nClickY)) {
            return App_ScriptNormalGroundFail("site_click", "same_land_site_not_clickable",
                                              0, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        printf("pocketufo: normal-ground input control=site_marker pointX=%d pointY=%d siteIndex=%d siteSeed=%d\n",
               (int)nClickX, (int)nClickY, (int)nSiteIdx, (int)nSiteSeed);
    }
    App_DrawFrame();
    nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (nCarrierIdx < 0 || nSiteIdx < 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bGeoscapeUi == 0 || g_world.sModal.bOpen != 0
        || App_ScriptNormalLine0Flag() != 20 || g_world.geo.time.bPaused == 0
        || g_world.geo.time.nTotalSec != nClockOptionsStart || g_world.msgLines.nLineCount == 0
        || g_world.nSelectedShipIdx != nCarrierIdx
        || g_world.msgLines.anColor[0][0] != 0u
        || g_world.msgLines.anColor[0][1] != (u32)nSiteIdx) {
        return App_ScriptNormalGroundFail("flag20", "real_site_click_did_not_arm_same_flag20_slots",
                                          0, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptNormalGroundGeoObserve("flag20", 0, nSiteSeed, bCarrierId);
    App_ScriptDump("normal_ground_flag20.ppm");
    printf("pocketufo: normal-ground snapshot stage=flag20 file=normal_ground_flag20.ppm\n");
    nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (nCarrierIdx < 0 || nSiteIdx < 0 || App_ScriptNormalLine0Flag() != 20
        || g_world.nSelectedShipIdx != nCarrierIdx || g_world.geo.time.bPaused == 0
        || g_world.geo.time.nTotalSec != nClockOptionsStart
        || g_world.msgLines.anColor[0][0] != 0u
        || g_world.msgLines.anColor[0][1] != (u32)nSiteIdx
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nType != 4
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nSeed != nSiteSeed
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->bActive == 0) {
        return App_ScriptNormalGroundFail("flag20_snapshot", "flag20_slots_changed_after_paint",
                                          0, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptNormalGroundGeoObserve("flag20_after_snapshot", 0, nSiteSeed, bCarrierId);

    {
        nCarrierBeforeIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
        const Ship *pCarrierBefore = nCarrierBeforeIdx >= 0
            ? g_world.apCraft[nCarrierBeforeIdx] : NULL;
        UiControl *pFlag20B = g_world.ui.apCtl[GEOUI_CTL_186BB0];
        const i32 nX = pFlag20B != NULL ? (i32)pFlag20B->x + (i32)pFlag20B->uWidth / 2 : -1;
        const i32 nY = pFlag20B != NULL ? (i32)pFlag20B->y + (i32)pFlag20B->uHeight / 2 : -1;
        if (pCarrierBefore == NULL || pCarrierBefore->bId != bCarrierId
            || pCarrierBefore->bCraftType != CRAFT_TYPE_SKYRANGER
            || pCarrierBefore->bAtBase == 0 || pCarrierBefore->bIntercepting != 0) {
            return App_ScriptNormalGroundFail("site_order", "selected_transport_not_ready_before_control_b",
                                              0, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        bAtBaseBefore = pCarrierBefore->bAtBase;
        bInterceptBefore = pCarrierBefore->bIntercepting;
        nShipTargetBefore = pCarrierBefore->nStrategIdx;
        if (!App_ScriptNormalClickControl(pFlag20B)) {
            return App_ScriptNormalGroundFail("site_order", "live_flag20_control_b_not_ready",
                                              0, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        printf("pocketufo: normal-ground input control=flag20_b pointX=%d pointY=%d siteIndex=%d siteSeed=%d\n",
               (int)nX, (int)nY, (int)nSiteIdx, (int)nSiteSeed);
    }
    App_DrawFrame();
    nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    {
        const Ship *pCarrier = nCarrierIdx >= 0 ? g_world.apCraft[nCarrierIdx] : NULL;
        const StrategItem *pSite = nSiteIdx >= 0
            ? (const StrategItem *)g_world.strategList.apItems[nSiteIdx] : NULL;
        printf("pocketufo: normal-ground order siteSeed=%d siteIndex=%d carrierId=%u carrierIndexBefore=%d carrierIndexAfter=%d atBaseBefore=%u atBaseAfter=%u interceptBefore=%u interceptAfter=%u shipTargetBefore=%d shipTargetAfter=%d clock=%d paused=%u flag=%d\n",
               (int)nSiteSeed, (int)nSiteIdx, (unsigned)bCarrierId,
               (int)nCarrierBeforeIdx, (int)nCarrierIdx,
               (unsigned)bAtBaseBefore, pCarrier != NULL ? (unsigned)pCarrier->bAtBase : 255u,
               (unsigned)bInterceptBefore, pCarrier != NULL ? (unsigned)pCarrier->bIntercepting : 255u,
               (int)nShipTargetBefore, pCarrier != NULL ? (int)pCarrier->nStrategIdx : -1,
               (int)g_world.geo.time.nTotalSec, (unsigned)g_world.geo.time.bPaused,
               (int)App_ScriptNormalLine0Flag());
        if (pCarrier == NULL || pSite == NULL || pCarrier->bId != bCarrierId
            || pCarrier->bCraftType != CRAFT_TYPE_SKYRANGER || pCarrier->bAtBase != 0
            || pCarrier->bIntercepting == 0 || pCarrier->nStrategIdx != nSiteIdx
            || pSite->nSeed != nSiteSeed || pSite->nType != 4 || pSite->bActive == 0
            || g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
            || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0
            || g_world.geo.time.nTotalSec != nClockOptionsStart
            || App_ScriptNormalLine0Flag() != 0 || g_world.msgLines.nLineCount != 0
            || g_world.bMapSelectionFlag != 0) {
            return App_ScriptNormalGroundFail("site_order", "live_flag20_control_did_not_order_same_carrier_site",
                                              0, nSiteSeed, bCarrierId, nExpectedCrew);
        }
    }
    App_ScriptNormalGroundGeoObserve("ordered", 0, nSiteSeed, bCarrierId);
    App_ScriptDump("normal_ground_ordered.ppm");
    printf("pocketufo: normal-ground snapshot stage=ordered file=normal_ground_ordered.ppm\n");
    nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (nCarrierIdx < 0 || nSiteIdx < 0 || g_world.geo.time.bPaused != 0
        || g_world.geo.time.nTotalSec != nClockOptionsStart
        || g_world.msgLines.nLineCount != 0 || App_ScriptNormalLine0Flag() != 0
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nSeed != nSiteSeed
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nType != 4
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->bActive == 0
        || g_world.apCraft[nCarrierIdx] == NULL
        || g_world.apCraft[nCarrierIdx]->bId != bCarrierId
        || g_world.apCraft[nCarrierIdx]->bIntercepting == 0
        || g_world.apCraft[nCarrierIdx]->nStrategIdx != nSiteIdx) {
        return App_ScriptNormalGroundFail("ordered_snapshot", "carrier_or_site_changed_after_paint",
                                          0, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptNormalGroundGeoObserve("ordered_after_snapshot", 0, nSiteSeed, bCarrierId);
    {
        UiControl *pMinute = g_world.ui.apRootCtl[3];
        const i32 nX = pMinute != NULL ? (i32)pMinute->x + (i32)pMinute->uWidth / 2 : -1;
        const i32 nY = pMinute != NULL ? (i32)pMinute->y + (i32)pMinute->uHeight / 2 : -1;
        if (!App_ScriptNormalClickControl(pMinute) || g_world.geo.world.nTickSeconds != 60) {
            return App_ScriptNormalGroundFail("flight_speed", "live_root3_minute_control_not_ready",
                                              0, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        printf("pocketufo: normal-ground input control=speed root=3 pointX=%d pointY=%d tickSec=%d\n",
               (int)nX, (int)nY, (int)g_world.geo.world.nTickSeconds);
    }
    nCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
    nSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
    if (nCarrierIdx < 0 || nSiteIdx < 0
        || g_world.bMapSelectionFlag != 0 || App_ScriptNormalLine0Flag() != 0
        || g_world.geo.time.nTotalSec != nClockOptionsStart
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nSeed != nSiteSeed
        || ((const StrategItem *)g_world.strategList.apItems[nSiteIdx])->nType != 4
        || g_world.apCraft[nCarrierIdx] == NULL
        || g_world.apCraft[nCarrierIdx]->bId != bCarrierId
        || g_world.apCraft[nCarrierIdx]->bIntercepting == 0
        || g_world.apCraft[nCarrierIdx]->nStrategIdx != nSiteIdx) {
        return App_ScriptNormalGroundFail("flight_speed", "site_or_carrier_changed_after_speed_input",
                                          0, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    nFlightClockStart = g_world.geo.time.nTotalSec;
    if (nFlightClockStart != nClockOptionsStart
        || g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
        || g_world.geo.time.bPaused != 0 || g_world.sModal.bOpen != 0
        || App_ScriptNormalLine0Flag() != 0 || g_world.geo.world.nTickSeconds != 60) {
        return App_ScriptNormalGroundFail("flight_start", "one_minute_map_state_not_live",
                                          0, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    printf("pocketufo: normal-ground flight-start clock=%d tickSec=%d maxPulses=%d\n",
           (int)nFlightClockStart, (int)g_world.geo.world.nTickSeconds,
           (int)kNormalGroundMaxFlightPulses);

    for (nFlightPulses = 1; nFlightPulses <= kNormalGroundMaxFlightPulses; ++nFlightPulses) {
        i32 nCurrentSiteIdx;
        i32 nCurrentCarrierIdx;
        const StrategItem *pSite;
        const Ship *pCarrier;
        const i32 nFlagBefore = App_ScriptNormalLine0Flag();
        if (nFlagBefore != 0 || g_world.screen.nGeoscapeScreen != 0
            || g_world.bGeoscapeUi == 0 || g_world.sModal.bOpen != 0
            || g_world.geo.time.bPaused != 0 || g_world.bRadarScreenActive != 0
            || g_world.geo.world.nTickSeconds != 60) {
            return App_ScriptNormalGroundFail("flight", "unexpected_flag_modal_pause_or_screen",
                                              nFlightPulses - 1, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        App_ScriptTicks(1);
        nCurrentSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
        nCurrentCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
        pSite = nCurrentSiteIdx >= 0
            ? (const StrategItem *)g_world.strategList.apItems[nCurrentSiteIdx] : NULL;
        pCarrier = nCurrentCarrierIdx >= 0 ? g_world.apCraft[nCurrentCarrierIdx] : NULL;
        if (App_ScriptNormalLine0Flag() == 23) {
            nArrivalPulse = nFlightPulses;
            if (pSite == NULL || pCarrier == NULL || pSite->nSeed != nSiteSeed
                || pSite->nType != 4 || pSite->bActive == 0 || pSite->bVisible == 0
                || pSite->nTimer <= 0 || pCarrier->bId != bCarrierId
                || pCarrier->bCraftType != CRAFT_TYPE_SKYRANGER || pCarrier->bAtBase != 0
                || pCarrier->bIntercepting == 0 || pCarrier->nStrategIdx != nCurrentSiteIdx
                || g_world.geo.time.bPaused == 0 || g_world.sModal.bOpen != 0
                || g_world.bRadarScreenActive != 0 || g_world.msgLines.nLineCount == 0
                || g_world.msgLines.anColor[0][0] != (u32)nCurrentCarrierIdx
                || g_world.msgLines.anColor[0][1] != (u32)nCurrentSiteIdx
                || g_world.geo.time.nTotalSec != nFlightClockStart + nArrivalPulse * 61) {
                return App_ScriptNormalGroundFail("flag23", "arrival_slots_or_same_site_order_invalid",
                                                  nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
            }
            App_ScriptNormalGroundGeoObserve("flag23", nFlightPulses, nSiteSeed, bCarrierId);
            App_ScriptDump("normal_ground_flag23.ppm");
            printf("pocketufo: normal-ground snapshot stage=flag23 file=normal_ground_flag23.ppm\n");
            nCurrentSiteIdx = App_ScriptNormalAirCombatFindTarget(nSiteSeed);
            nCurrentCarrierIdx = App_ScriptNormalAirCombatFindCraft(bCarrierId);
            const StrategItem *pCurrentSite = nCurrentSiteIdx >= 0
                ? (const StrategItem *)g_world.strategList.apItems[nCurrentSiteIdx] : NULL;
            const Ship *pCurrentCarrier = nCurrentCarrierIdx >= 0
                ? g_world.apCraft[nCurrentCarrierIdx] : NULL;
            if (pCurrentSite == NULL || pCurrentCarrier == NULL
                || pCurrentSite->nSeed != nSiteSeed || pCurrentSite->nType != 4
                || pCurrentSite->bActive == 0 || pCurrentSite->bVisible == 0
                || pCurrentCarrier->bId != bCarrierId
                || pCurrentCarrier->bCraftType != CRAFT_TYPE_SKYRANGER
                || pCurrentCarrier->bIntercepting == 0
                || pCurrentCarrier->nStrategIdx != nCurrentSiteIdx
                || App_ScriptNormalLine0Flag() != 23 || g_world.geo.time.bPaused == 0
                || g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
                || g_world.sModal.bOpen != 0 || g_world.bRadarScreenActive != 0
                || g_world.msgLines.nLineCount == 0
                || g_world.geo.time.nTotalSec != nFlightClockStart + nArrivalPulse * 61
                || g_world.msgLines.anColor[0][0] != (u32)nCurrentCarrierIdx
                || g_world.msgLines.anColor[0][1] != (u32)nCurrentSiteIdx) {
                return App_ScriptNormalGroundFail("flag23_snapshot", "arrival_record_changed_after_paint",
                                                  nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
            }
            App_ScriptNormalGroundGeoObserve("flag23_after_snapshot", nFlightPulses,
                                             nSiteSeed, bCarrierId);
            break;
        }
        if (App_ScriptNormalLine0Flag() != 0 || g_world.screen.nGeoscapeScreen != 0
            || g_world.bGeoscapeUi == 0 || g_world.sModal.bOpen != 0
            || g_world.geo.time.bPaused != 0 || g_world.bRadarScreenActive != 0
            || g_world.geo.world.nTickSeconds != 60
            || g_world.geo.time.nTotalSec != nFlightClockStart + nFlightPulses * 61) {
            return App_ScriptNormalGroundFail("flight", "unexpected_state_after_minute_pulse",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        if (pSite == NULL || pSite->nSeed != nSiteSeed || pSite->nType != 4
            || pSite->bActive == 0 || pSite->bVisible == 0 || pSite->nTimer <= 0) {
            return App_ScriptNormalGroundFail("flight", "same_visible_site_expired_or_changed",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        if (pCarrier == NULL || pCarrier->bId != bCarrierId
            || pCarrier->bCraftType != CRAFT_TYPE_SKYRANGER || pCarrier->bAtBase != 0
            || pCarrier->bIntercepting == 0 || pCarrier->nStrategIdx != nCurrentSiteIdx
            || pCarrier->nFuel <= 0 || pCarrier->uRangeRemain == 0) {
            return App_ScriptNormalGroundFail("flight", "same_transport_order_or_fuel_changed",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        if (nFlightPulses % 60 == 0) {
            App_ScriptNormalGroundGeoObserve("flight_progress", nFlightPulses,
                                             nSiteSeed, bCarrierId);
        }
    }
    if (nArrivalPulse == 0 || App_ScriptNormalLine0Flag() != 23) {
        return App_ScriptNormalGroundFail("flight", "minute_pulse_bound_600_without_flag23",
                                          kNormalGroundMaxFlightPulses, nSiteSeed,
                                          bCarrierId, nExpectedCrew);
    }

    {
        UiControl *pFlag23B = g_world.ui.apCtl[GEOUI_CTL_186BB0];
        const i32 nX = pFlag23B != NULL ? (i32)pFlag23B->x + (i32)pFlag23B->uWidth / 2 : -1;
        const i32 nY = pFlag23B != NULL ? (i32)pFlag23B->y + (i32)pFlag23B->uHeight / 2 : -1;
        if (!App_ScriptNormalClickControl(pFlag23B)) {
            return App_ScriptNormalGroundFail("begin_craft_select", "live_flag23_control_b_not_ready",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        printf("pocketufo: normal-ground input control=flag23_b pointX=%d pointY=%d siteSeed=%d\n",
               (int)nX, (int)nY, (int)nSiteSeed);
    }
    if (!World_bTactPointerLive(&g_world) || g_world.pTactMap == NULL
        || g_world.pTactState == NULL || g_world.bTactLive == 0) {
        return App_ScriptNormalGroundFail("tactical_entry", "real_flag23_control_did_not_create_tactical_owners",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    if (!App_ScriptNormalGroundTacticalIdentityMatches(nSiteSeed, bCarrierId,
                                                        &nSiteIdx, &nCarrierIdx)
        || g_world.nUiScreen > 1u || g_world.pTactMap->nXcomCount > PUFO_MAP_ROSTER_MAX
        || g_world.sModal.bOpen != 0 || g_world.msgLines.nLineCount != 0
        || App_ScriptNormalLine0Flag() != 0) {
        return App_ScriptNormalGroundFail("tactical_entry", "unexpected_screen_or_mission_owner",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptNormalGroundTactObserve("inventory_entry", nSiteSeed,
                                      bCarrierId, nExpectedCrew);
    App_ScriptDump("normal_ground_inventory.ppm");
    printf("pocketufo: normal-ground snapshot stage=inventory_entry file=normal_ground_inventory.ppm\n");
    if (!App_ScriptNormalGroundTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) {
        return App_ScriptNormalGroundFail("inventory_snapshot", "tactical_owner_changed_after_paint",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    if (!App_ScriptNormalGroundTacticalIdentityMatches(nSiteSeed, bCarrierId,
                                                        &nSiteIdx, &nCarrierIdx)) {
        return App_ScriptNormalGroundFail("inventory_snapshot", "site_or_carrier_changed_after_paint",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    if (g_world.sModal.bOpen != 0 || g_world.msgLines.nLineCount != 0
        || App_ScriptNormalLine0Flag() != 0) {
        return App_ScriptNormalGroundFail("inventory_snapshot", "unknown_line_or_modal_after_paint",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptNormalGroundTactObserve("inventory_after_snapshot", nSiteSeed,
                                      bCarrierId, nExpectedCrew);

    if (g_world.nUiScreen == 1u) {
        if (!App_ScriptCampaignKey(PUFO_KEY_CONFIRM)) {
            return App_ScriptNormalGroundFail("deploy_input", "real_confirm_key_not_live",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        printf("pocketufo: normal-ground input key=confirm phase=inventory\n");
        if (!App_ScriptNormalGroundTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) {
            return App_ScriptNormalGroundFail("deploy_input", "tactical_owner_changed_after_confirm",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        if (!App_ScriptNormalGroundTacticalIdentityMatches(nSiteSeed, bCarrierId,
                                                            &nSiteIdx, &nCarrierIdx)) {
            return App_ScriptNormalGroundFail("deploy_input", "site_or_carrier_changed_after_confirm",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        if (g_world.sModal.bOpen != 0 || g_world.msgLines.nLineCount != 0
            || App_ScriptNormalLine0Flag() != 0) {
            return App_ScriptNormalGroundFail("deploy_input", "unknown_line_or_modal_after_confirm",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        App_ScriptNormalGroundTactObserve("after_deploy_key", nSiteSeed,
                                          bCarrierId, nExpectedCrew);
        App_DrawFrame();
        if (!App_ScriptNormalGroundTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) {
            return App_ScriptNormalGroundFail("deploy_input", "tactical_owner_changed_after_confirm_paint",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        if (!App_ScriptNormalGroundTacticalIdentityMatches(nSiteSeed, bCarrierId,
                                                            &nSiteIdx, &nCarrierIdx)) {
            return App_ScriptNormalGroundFail("deploy_input", "site_or_carrier_changed_after_confirm_paint",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        if (g_world.sModal.bOpen != 0 || g_world.msgLines.nLineCount != 0
            || App_ScriptNormalLine0Flag() != 0) {
            return App_ScriptNormalGroundFail("deploy_input", "unknown_line_or_modal_after_confirm_paint",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        App_ScriptNormalGroundTactObserve("after_deploy_paint", nSiteSeed,
                                          bCarrierId, nExpectedCrew);
    } else if (g_world.nUiScreen == 0u && g_world.bMapPlayerInput != 0) {
        printf("pocketufo: normal-ground input deploy=already_live_player_screen\n");
    } else {
        return App_ScriptNormalGroundFail("deploy_input", "unknown_inventory_or_player_screen",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }

    if (g_world.nUiScreen == 0u && g_world.bMapEnemyTurnActive == 0
        && g_world.pTactState != NULL
        && (g_world.pTactState->nQueueLen != 0 || g_world.pTactState->bQueueActive != 0
            || (g_world.pTactMap != NULL && g_world.pTactMap->tact.bBusy != 0))) {
        if (!App_ScriptCampaignDrainQueue(kMaxDeployQueueTicks)
            || !App_ScriptNormalGroundTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) {
            return App_ScriptNormalGroundFail("deploy_wait", "bounded_real_queue_drain_failed",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
        if (!App_ScriptNormalGroundTacticalIdentityMatches(nSiteSeed, bCarrierId,
                                                            &nSiteIdx, &nCarrierIdx)) {
            return App_ScriptNormalGroundFail("deploy_wait", "site_or_carrier_changed_during_queue_drain",
                                              nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
        }
    }
    if (!App_ScriptNormalGroundTacticalIdentityMatches(nSiteSeed, bCarrierId,
                                                        &nSiteIdx, &nCarrierIdx)) {
        return App_ScriptNormalGroundFail("deployed", "site_or_carrier_identity_changed_before_deploy_check",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    GameMap *pDeployedMap = NULL;
    TactState *pDeployedQueue = NULL;
    if (!App_ScriptNormalGroundTactOwnersMatch(nMapKey, nQueueKey,
                                                &pDeployedMap, &pDeployedQueue)) {
        return App_ScriptNormalGroundFail("deployed", "tactical_owners_not_live",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    if (g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
        || App_ScriptNormalLine0Flag() != 0 || g_world.bMapBattleEnded != 0
        || g_world.bMapPlayerInput == 0 || g_world.bMapEnemyTurnActive != 0
        || pDeployedMap->nXcomCount != kNormalGroundExpectedCrew
        || pDeployedMap->nEnemyCount == 0 || pDeployedMap->pSelUnit == NULL
        || pDeployedMap->tact.bBusy != 0 || pDeployedQueue->nQueueLen != 0
        || pDeployedQueue->nQueueCursor != 0 || pDeployedQueue->bQueueActive != 0
        || nExpectedCrew != kNormalGroundExpectedCrew) {
        return App_ScriptNormalGroundFail("deployed", "eight_troops_or_live_player_input_not_ready",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    if (!App_ScriptNormalGroundVerifyCrew(pDeployedMap, apExpectedCrew,
                                          anExpectedCrewRosterIndices, nExpectedCrew,
                                          nCarrierIdx, "deployed_before_snapshot")) {
        return App_ScriptNormalGroundFail("deployed", "assigned_troops_not_placed_in_distinct_live_cells",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptDump("normal_ground_deployed.ppm");
    printf("pocketufo: normal-ground snapshot stage=deployed file=normal_ground_deployed.ppm\n");
    pDeployedMap = NULL;
    pDeployedQueue = NULL;
    if (!App_ScriptNormalGroundTacticalIdentityMatches(nSiteSeed, bCarrierId,
                                                        &nSiteIdx, &nCarrierIdx)
        || !App_ScriptNormalGroundTactOwnersMatch(nMapKey, nQueueKey,
                                                &pDeployedMap, &pDeployedQueue)
        || g_world.nUiScreen != 0u || g_world.bMapPlayerInput == 0
        || g_world.bMapEnemyTurnActive != 0 || g_world.bMapBattleEnded != 0
        || g_world.sModal.bOpen != 0 || App_ScriptNormalLine0Flag() != 0
        || pDeployedMap->nXcomCount != kNormalGroundExpectedCrew
        || pDeployedMap->nEnemyCount == 0 || pDeployedMap->tact.bBusy != 0
        || pDeployedQueue->nQueueLen != 0 || pDeployedQueue->nQueueCursor != 0
        || pDeployedQueue->bQueueActive != 0
        || !App_ScriptNormalGroundVerifyCrew(pDeployedMap, apExpectedCrew,
                                             anExpectedCrewRosterIndices, nExpectedCrew,
                                             nCarrierIdx, "deployed")) {
        return App_ScriptNormalGroundFail("deployed_snapshot", "crew_or_player_input_changed_after_paint",
                                          nFlightPulses, nSiteSeed, bCarrierId, nExpectedCrew);
    }
    App_ScriptNormalGroundTactObserve("deployed", nSiteSeed,
                                      bCarrierId, nExpectedCrew);
    printf("pocketufo: normal-ground PASS stage=crew_deployed pulses=%d arrivalPulse=%d clockStart=%d clockEnd=%d siteSeed=%d carrierId=%u carrierIndex=%d expectedCrew=%d xcomCount=%u enemyCount=%u playerInput=%u queueLen=%d queueCursor=%d\n",
           (int)nFlightPulses, (int)nArrivalPulse, (int)nFlightClockStart,
           (int)g_world.geo.time.nTotalSec, (int)nSiteSeed, (unsigned)bCarrierId,
           (int)App_ScriptNormalAirCombatFindCraft(bCarrierId), (int)nExpectedCrew,
           (unsigned)pDeployedMap->nXcomCount, (unsigned)pDeployedMap->nEnemyCount,
           (unsigned)g_world.bMapPlayerInput, (int)pDeployedQueue->nQueueLen,
           (int)pDeployedQueue->nQueueCursor);
    fflush(stdout);
    return true;
}

static bool App_ScriptCampaignPickupGrenade(void)
{
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit = NULL;
    TactItem *pGrenade = NULL;
    AppCampaignGrenadeRef sGrenade = { 0 }, sInaccessible = { 0 }, sBlocked = { 0 }, sArmed = { 0 };
    AppCampaignGrenadeStatus eFound = APP_CAMPAIGN_GRENADE_MISSING;
    uintptr_t nSelectedKey = 0;
    i32 nSourceX = -1, nSourceY = -1, nListIndex = -1;
    i32 nDragX, nDragY;
    u16 uTuBefore, uTuAfter;
    bool bHasInaccessible = false, bHasBlocked = false, bHasArmed = false;

    g_nCampaignGrenadeItemKey = 0;
    g_nCampaignGrenadeActorKey = 0;
    g_nCampaignGrenadePoolIndex = -1;
    g_nCampaignGrenadeRosterSlot = -1;
    if (g_world.nUiScreen != 0u
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || g_assets.apObjects == NULL || g_assets.nObjectCount <= 17
        || g_assets.apObjects[17] == NULL || pMap->nXcomCount == 0
        || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX) {
        printf("pocketufo: campaign grenade: FAIL (need a live tactical screen, crew, and grenade record)\n");
        fflush(stdout);
        return false;
    }
    nSelectedKey = (uintptr_t)(void *)pMap->pSelUnit;
    for (i32 nPass = 0; nPass <= (i32)pMap->nXcomCount; ++nPass) {
        uintptr_t nActorKey;

        if (nPass == 0) {
            nActorKey = nSelectedKey;
        } else {
            Soldier *pCandidate = pMap->apXcomUnits[nPass - 1];
            nActorKey = (uintptr_t)(void *)pCandidate;
            if (nActorKey == nSelectedKey) { continue; }
        }
        if (nActorKey == 0
            || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nActorKey)) == NULL) {
            continue;
        }
        AppCampaignGrenadeRef sCandidate = { 0 };
        const AppCampaignGrenadeStatus eCandidate =
            App_ScriptCampaignGrenadeFindLinked(pMap, pUnit, 0, &sCandidate);
        if (eCandidate == APP_CAMPAIGN_GRENADE_INVALID) {
            printf("pocketufo: campaign grenade: FAIL (invalid live owner or item link)\n");
            fflush(stdout);
            return false;
        }
        if (eCandidate == APP_CAMPAIGN_GRENADE_FOUND) {
            sGrenade = sCandidate;
            eFound = eCandidate;
            break;
        }
        if (eCandidate == APP_CAMPAIGN_GRENADE_UNREACHABLE_BACKPACK && !bHasInaccessible) {
            sInaccessible = sCandidate;
            bHasInaccessible = true;
        } else if (eCandidate == APP_CAMPAIGN_GRENADE_HAND_OCCUPIED && !bHasBlocked) {
            sBlocked = sCandidate;
            bHasBlocked = true;
        } else if (eCandidate == APP_CAMPAIGN_GRENADE_ARMED_OR_FUSED && !bHasArmed) {
            sArmed = sCandidate;
            bHasArmed = true;
        }
    }
    if (eFound != APP_CAMPAIGN_GRENADE_FOUND) {
        if (bHasInaccessible) {
            printf("pocketufo: campaign grenade: FAIL (item 17 only has an unsupported owner slot %d backpack[%d])\n",
                   (int)sInaccessible.nRosterSlot, (int)sInaccessible.nSourceSlot);
        } else if (bHasBlocked) {
            printf("pocketufo: campaign grenade: FAIL (item 17 owner slot %d has a full right hand)\n",
                   (int)sBlocked.nRosterSlot);
        } else if (bHasArmed) {
            printf("pocketufo: campaign grenade: FAIL (item 17 is already armed or fused on owner slot %d)\n",
                   (int)sArmed.nRosterSlot);
        } else {
            printf("pocketufo: campaign grenade: FAIL (no clean item 17 is linked to a live usable owner)\n");
        }
        fflush(stdout);
        return false;
    }

    pUnit = App_ScriptCampaignFindActiveXcom(pMap, sGrenade.nActorKey);
    if (pUnit == NULL || sGrenade.nRosterSlot < 0
        || sGrenade.nRosterSlot >= (i32)pMap->nXcomCount
        || pMap->apXcomUnits[sGrenade.nRosterSlot] != pUnit) {
        return false;
    }
    for (i32 nTry = 0; nTry < (i32)pMap->nXcomCount
                       && (uintptr_t)(void *)pMap->pSelUnit != sGrenade.nActorKey; ++nTry) {
        AppCampaignGrenadeRef sCheck = { 0 };

        if (!App_ScriptCampaignClickBank(0)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, sGrenade.nActorKey)) == NULL) {
            printf("pocketufo: campaign grenade: FAIL (owner changed while selecting its live row)\n");
            fflush(stdout);
            return false;
        }
        if (App_ScriptCampaignGrenadeFindLinked(pMap, pUnit, sGrenade.nItemKey, &sCheck)
                != APP_CAMPAIGN_GRENADE_FOUND
            || sCheck.nPoolIndex != sGrenade.nPoolIndex
            || sCheck.nSourceKind != sGrenade.nSourceKind
            || sCheck.nSourceSlot != sGrenade.nSourceSlot
            || sCheck.nIndex != sGrenade.nIndex || sCheck.nAmmo != sGrenade.nAmmo) {
            printf("pocketufo: campaign grenade: FAIL (owner item changed while selecting its row)\n");
            fflush(stdout);
            return false;
        }
    }
    if ((uintptr_t)(void *)pMap->pSelUnit != sGrenade.nActorKey
        || sGrenade.nRosterSlot >= (i32)pMap->nXcomCount
        || pMap->apXcomUnits[sGrenade.nRosterSlot] != pUnit) {
        printf("pocketufo: campaign grenade: FAIL (the live roster control did not select the grenade owner)\n");
        fflush(stdout);
        return false;
    }
    {
        AppCampaignGrenadeRef sCheck = { 0 };

        if (App_ScriptCampaignGrenadeFindLinked(pMap, pUnit, sGrenade.nItemKey, &sCheck)
                != APP_CAMPAIGN_GRENADE_FOUND
            || sCheck.nPoolIndex != sGrenade.nPoolIndex
            || sCheck.nSourceKind != sGrenade.nSourceKind
            || sCheck.nSourceSlot != sGrenade.nSourceSlot
            || sCheck.nIndex != sGrenade.nIndex || sCheck.nAmmo != sGrenade.nAmmo) {
            printf("pocketufo: campaign grenade: FAIL (selected owner no longer links the chosen item)\n");
            fflush(stdout);
            return false;
        }
    }
    printf("pocketufo: campaign grenade source: key 0x%llx ownerSlot %d kind %d slot %d index %u ammo %u armed %u fuse %u\n",
           (unsigned long long)sGrenade.nItemKey, (int)sGrenade.nRosterSlot,
           (int)sGrenade.nSourceKind, (int)sGrenade.nSourceSlot,
           (unsigned)sGrenade.nIndex, (unsigned)sGrenade.nAmmo,
           (unsigned)sGrenade.bArmed, (unsigned)sGrenade.bFuse);
    fflush(stdout);
    if (sGrenade.nSourceKind == APP_CAMPAIGN_GRENADE_RIGHT_HAND) {
        g_nCampaignGrenadeItemKey = sGrenade.nItemKey;
        g_nCampaignGrenadeActorKey = sGrenade.nActorKey;
        g_nCampaignGrenadePoolIndex = sGrenade.nPoolIndex;
        g_nCampaignGrenadeRosterSlot = sGrenade.nRosterSlot;
        g_uCampaignGrenadeItemIndex = sGrenade.nIndex;
        g_uCampaignGrenadeAmmo = sGrenade.nAmmo;
        printf("pocketufo: campaign grenade: object 17 is already in the right hand\n");
        fflush(stdout);
        return true;
    }
    if (pUnit->pHandR != NULL) {
        printf("pocketufo: campaign grenade: FAIL (chosen owner's right hand is occupied)\n");
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign inventory: widget4 enabled %u at %d/%d/%u/%u\n",
           (unsigned)g_bank.aCtl[4].bEnabled, (int)g_bank.aCtl[4].x, (int)g_bank.aCtl[4].y,
           (unsigned)g_bank.aCtl[4].uWidth, (unsigned)g_bank.aCtl[4].uHeight);
    fflush(stdout);
    if (g_bank.aCtl[4].bEnabled == 0 || !App_ScriptCampaignClickBank(4)) {
        return false;
    }
    if (g_world.nUiScreen != 1u
        || !App_ScriptCampaignTactOwnerKeysMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, sGrenade.nActorKey)) == NULL
        || pMap->pSelUnit != pUnit) {
        printf("pocketufo: campaign grenade: FAIL (inventory screen or live owner changed)\n");
        fflush(stdout);
        return false;
    }
    (void)App_DrawFrame();
    if (!App_ScriptCampaignTactOwnerKeysMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || g_world.nUiScreen != 1u
        || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, sGrenade.nActorKey)) == NULL
        || pMap->pSelUnit != pUnit
        || !App_ScriptCampaignGrenadeRefresh(
            nMapKey, nQueueKey, sGrenade.nActorKey, sGrenade.nItemKey,
            sGrenade.nPoolIndex, sGrenade.nRosterSlot, sGrenade.nIndex, sGrenade.nAmmo,
            &pMap, &pQueue, &pUnit, &pGrenade)) {
        printf("pocketufo: campaign grenade: FAIL (inventory paint changed a live owner or item)\n");
        fflush(stdout);
        return false;
    }
    {
        AppCampaignGrenadeRef sCheck = { 0 };

        if (App_ScriptCampaignGrenadeFindLinked(pMap, pUnit, sGrenade.nItemKey, &sCheck)
                != APP_CAMPAIGN_GRENADE_FOUND
            || sCheck.nPoolIndex != sGrenade.nPoolIndex
            || sCheck.nSourceKind != sGrenade.nSourceKind
            || sCheck.nSourceSlot != sGrenade.nSourceSlot) {
            printf("pocketufo: campaign grenade: FAIL (inventory did not keep the same item source)\n");
            fflush(stdout);
            return false;
        }
    }
    if (sGrenade.nSourceKind == APP_CAMPAIGN_GRENADE_BACKPACK) {
        const i32 nSlot = sGrenade.nSourceSlot;
        if (nSlot < 0 || nSlot >= PUFO_UNIT_BACKPACK_SLOTS || nSlot == 5 || nSlot == 6
            || (uintptr_t)(void *)pUnit->apBackpack[nSlot] != sGrenade.nItemKey) {
            printf("pocketufo: campaign grenade: FAIL (owner backpack slot %d has no live hit rectangle)\n",
                   (int)nSlot);
            fflush(stdout);
            return false;
        }
        nSourceX = 170 + 19 * (nSlot % 4);
        nSourceY = 170 + 19 * (nSlot / 4);
    } else if (sGrenade.nSourceKind == APP_CAMPAIGN_GRENADE_GROUND) {
        const i32 nCapacity = (i32)(sizeof(g_world.sTactUi.apInvList)
                                    / sizeof(g_world.sTactUi.apInvList[0]));
        const i32 nFilter = (i32)g_world.sTactUi.nInvFilterType;

        if (g_world.sTactUi.nInvListCount > nCapacity) {
            printf("pocketufo: campaign grenade: FAIL (ground list count is out of range)\n");
            fflush(stdout);
            return false;
        }
        for (i32 i = 0; i < (i32)g_world.sTactUi.nInvListCount; ++i) {
            if ((uintptr_t)(void *)g_world.sTactUi.apInvList[i] == sGrenade.nItemKey) {
                nListIndex = i;
                break;
            }
        }
        if (nListIndex < 0 || nListIndex < nFilter || nListIndex - nFilter > 3) {
            printf("pocketufo: campaign grenade: FAIL (item is not a visible ground representative; index %d filter %d)\n",
                   (int)nListIndex, (int)nFilter);
            fflush(stdout);
            return false;
        }
        nSourceX = 40 + 38 * (nListIndex - nFilter);
        nSourceY = 288;
    } else {
        return false;
    }
    nDragX = PUFO_INV_DRAG_SCALE * (i32)pGrenade->pObj->nFootW / 2;
    nDragY = PUFO_INV_DRAG_SCALE * (i32)pGrenade->pObj->nFootH / 2;
    uTuBefore = pUnit->uTu;

    App_ScriptPointerLogical(nSourceX, nSourceY, true);
    if (!App_ScriptCampaignGrenadeRefresh(
            nMapKey, nQueueKey, sGrenade.nActorKey, sGrenade.nItemKey,
            sGrenade.nPoolIndex, sGrenade.nRosterSlot, sGrenade.nIndex, sGrenade.nAmmo,
            &pMap, &pQueue, &pUnit, &pGrenade)
        || g_world.nUiScreen != 1u) {
        printf("pocketufo: campaign grenade: FAIL (owners changed after drag start)\n");
        fflush(stdout);
        return false;
    }
    App_ScriptPointerLogical(140 - nDragX - 10, 90 - nDragY - 10, false);
    if (!App_ScriptCampaignGrenadeRefresh(
            nMapKey, nQueueKey, sGrenade.nActorKey, sGrenade.nItemKey,
            sGrenade.nPoolIndex, sGrenade.nRosterSlot, sGrenade.nIndex, sGrenade.nAmmo,
            &pMap, &pQueue, &pUnit, &pGrenade)
        || g_world.nUiScreen != 1u) {
        printf("pocketufo: campaign grenade: FAIL (owners changed during drag)\n");
        fflush(stdout);
        return false;
    }
    App_OnPointerUp();
    if (!App_ScriptCampaignGrenadeRefresh(
            nMapKey, nQueueKey, sGrenade.nActorKey, sGrenade.nItemKey,
            sGrenade.nPoolIndex, sGrenade.nRosterSlot, sGrenade.nIndex, sGrenade.nAmmo,
            &pMap, &pQueue, &pUnit, &pGrenade)
        || g_world.nUiScreen != 1u
        || (uintptr_t)(void *)pUnit->pHandR != sGrenade.nItemKey) {
        printf("pocketufo: campaign grenade: FAIL (real drag did not put the same item in the right hand)\n");
        fflush(stdout);
        return false;
    }
    uTuAfter = pUnit->uTu;
    if (uTuBefore < uTuAfter || (i32)uTuBefore - (i32)uTuAfter != PUFO_INV_TU_HAND) {
        printf("pocketufo: campaign grenade: FAIL (real hand transfer did not spend %d TU)\n",
               (int)PUFO_INV_TU_HAND);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignKey(PUFO_KEY_CONFIRM)
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || g_world.nUiScreen != 0u
        || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, sGrenade.nActorKey)) == NULL
        || pMap->pSelUnit != pUnit) {
        printf("pocketufo: campaign grenade: FAIL (real inventory close did not return to live tactical input)\n");
        fflush(stdout);
        return false;
    }
    (void)App_DrawFrame();
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, sGrenade.nActorKey)) == NULL
        || pMap->pSelUnit != pUnit
        || !App_ScriptCampaignGrenadeRefresh(
            nMapKey, nQueueKey, sGrenade.nActorKey, sGrenade.nItemKey,
            sGrenade.nPoolIndex, sGrenade.nRosterSlot, sGrenade.nIndex, sGrenade.nAmmo,
            &pMap, &pQueue, &pUnit, &pGrenade)
        || (uintptr_t)(void *)pUnit->pHandR != sGrenade.nItemKey
        || pGrenade->bUnk8 != 0 || pGrenade->bUnk7 != 0) {
        printf("pocketufo: campaign grenade: FAIL (post-close owner or right-hand item changed)\n");
        fflush(stdout);
        return false;
    }
    g_nCampaignGrenadeItemKey = sGrenade.nItemKey;
    g_nCampaignGrenadeActorKey = sGrenade.nActorKey;
    g_nCampaignGrenadePoolIndex = sGrenade.nPoolIndex;
    g_nCampaignGrenadeRosterSlot = sGrenade.nRosterSlot;
    g_uCampaignGrenadeItemIndex = sGrenade.nIndex;
    g_uCampaignGrenadeAmmo = sGrenade.nAmmo;
    printf("pocketufo: campaign grenade: object 17 moved from %d/%d to right hand, TU %u->%u cost %d screen %d\n",
           (int)nSourceX, (int)nSourceY, (unsigned)uTuBefore, (unsigned)uTuAfter,
           (int)PUFO_INV_TU_HAND, (int)g_world.nUiScreen);
    fflush(stdout);
    return true;
}

typedef struct AppCampaignCellPick {
    i32 nX, nY, nZ;
    i32 nScreenX, nScreenY;
    i32 nDistance;
    i32 nPathCost;
} AppCampaignCellPick;

typedef struct AppCampaignOutsidePrepMove {
    i32 nSequence;
    uintptr_t nActorKey;
    i32 nRosterSlot;
    i32 nStartX, nStartY, nStartZ;
    i32 nGoalX, nGoalY, nGoalZ;
    i32 nGoalPathCost;
    i32 nPathSteps;
    i32 nPrefixX, nPrefixY, nPrefixZ;
    i32 nPrefixPathCost;
    i32 nPrefixSteps;
    u16 uTuBefore, uTuAfter;
} AppCampaignOutsidePrepMove;

typedef struct AppCampaignPlaneRect {
    i32 nMinX, nMaxX;
    i32 nMinY, nMaxY;
} AppCampaignPlaneRect;

static bool App_ScriptCampaignCellInsidePlane(const AppCampaignPlaneRect *pPlane,
                                              i32 nX, i32 nY)
{
    return pPlane != NULL && nX >= pPlane->nMinX && nX <= pPlane->nMaxX
        && nY >= pPlane->nMinY && nY <= pPlane->nMaxY;
}

static bool App_ScriptCampaignCheckOutsidePlaneLayout(GameMap *pMap,
                                                       AppCampaignPlaneRect *pOutPlane)
{
    /* The raw aDeployCellOffsets group 0 at 0xA66B8 is the group selected by nMapLayers 1. */
    static const i8 aGroup0[8][3] = {
        { 4, 9, 1 }, { 5, 9, 1 }, { 4, 10, 1 }, { 5, 10, 1 },
        { 4, 11, 1 }, { 5, 11, 1 }, { 4, 12, 1 }, { 5, 12, 1 }
    };
    i32 nAnchorX, nAnchorY;
    AppCampaignPlaneRect sPlane;

    if (pMap == NULL || pOutPlane == NULL) { return false; }
    nAnchorX = PUFO_CELLS_PER_TILE * (i32)g_world.bMapCraftMapX;
    nAnchorY = PUFO_CELLS_PER_TILE * (i32)g_world.bMapCraftMapY;
    /* PLANE.MA1 has 10 columns and 20 rows. */
    sPlane = (AppCampaignPlaneRect){ nAnchorX, nAnchorX + 9,
                                     nAnchorY, nAnchorY + 19 };
    printf("pocketufo: campaign natural-fight-outside-plane layout: mission %u layers %u "
           "geometry %u/%u craftTile %u/%u footprint %d..%d/%d..%d crew %u deployGroup 0\n",
           (unsigned)pMap->nMissionType, (unsigned)pMap->nMapLayers,
           (unsigned)pMap->geom.nDimX, (unsigned)pMap->geom.nDimY,
           (unsigned)g_world.bMapCraftMapX, (unsigned)g_world.bMapCraftMapY,
           (int)sPlane.nMinX, (int)sPlane.nMaxX, (int)sPlane.nMinY, (int)sPlane.nMaxY,
           (unsigned)pMap->nXcomCount);
    fflush(stdout);

    if (pMap->nMissionType != 1u || pMap->nMapLayers != 1u
        || pMap->geom.nDimX != 6u || pMap->geom.nDimY != 6u
        || pMap->nXcomCount != (u8)(sizeof(aGroup0) / sizeof(aGroup0[0]))
        || g_world.nMapCraftIndex >= g_world.bCraftCount
        || g_world.bMapCraftMapX >= pMap->geom.nDimX
        || g_world.bMapCraftMapY >= pMap->geom.nDimY
        || sPlane.nMaxX >= PUFO_CELLS_PER_TILE * (i32)pMap->geom.nDimX
        || sPlane.nMaxY >= PUFO_CELLS_PER_TILE * (i32)pMap->geom.nDimY
        || sPlane.nMaxX >= PUFO_MAP_CELL_DIM || sPlane.nMaxY >= PUFO_MAP_CELL_DIM) {
        printf("pocketufo: campaign natural-fight-outside-plane: FAIL unproved tactical layout\n");
        fflush(stdout);
        return false;
    }

    for (i32 i = 0; i < (i32)(sizeof(aGroup0) / sizeof(aGroup0[0])); ++i) {
        Soldier *pUnit = pMap->apXcomUnits[i];
        MapCell *pCell = pUnit != NULL ? pUnit->pCell : NULL;
        const i32 nExpectedX = nAnchorX + (i32)aGroup0[i][0];
        const i32 nExpectedY = nAnchorY + (i32)aGroup0[i][1];
        const i32 nCellSlot = App_ScriptCampaignTactCellSlot(pCell);
        const bool bValid = pUnit != NULL
            && App_ScriptCampaignFindActiveXcom(pMap, (uintptr_t)(void *)pUnit) == pUnit
            && pUnit->bUnk183 == g_world.nMapCraftIndex
            && pCell != NULL && pCell->nX == nExpectedX && pCell->nY == nExpectedY
            && pCell->nZ == (u8)aGroup0[i][2] && nCellSlot >= 0
            && App_ScriptCampaignCellAt(nExpectedX, nExpectedY, (i32)aGroup0[i][2]) == pCell
            && pCell->pUnit == pUnit;

        printf("pocketufo: campaign natural-fight-outside-plane crew: slot %d actor 0x%llX "
               "expected %d/%d/%d actual %d/%d/%d cellSlot %d assignedCraft %u valid %d\n",
               (int)i, (unsigned long long)(uintptr_t)(void *)pUnit,
               (int)nExpectedX, (int)nExpectedY, (int)aGroup0[i][2],
               pCell != NULL ? (int)pCell->nX : -1,
               pCell != NULL ? (int)pCell->nY : -1,
               pCell != NULL ? (int)pCell->nZ : -1, (int)nCellSlot,
               pUnit != NULL ? (unsigned)pUnit->bUnk183 : 255u, bValid ? 1 : 0);
        fflush(stdout);
        if (!bValid) {
            printf("pocketufo: campaign natural-fight-outside-plane: FAIL deploy group 0 mismatch\n");
            fflush(stdout);
            return false;
        }
    }

    *pOutPlane = sPlane;
    return true;
}

static i32 g_nCampaignPathRejectShown;
static bool g_bCampaignStartStepTraced;

static void App_ScriptCampaignTraceMoveProbeEvent(const char *pszEvent,
                                                   i32 nPenX, i32 nPenY,
                                                   uintptr_t nMapKey,
                                                   uintptr_t nQueueKey,
                                                   uintptr_t nUnitKey,
                                                   i32 nTargetX, i32 nTargetY,
                                                   i32 nTargetZ)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit = NULL;
    MapCell *pSource = NULL;
    MapCell *pCandidate = NULL;
    MapCell *pDest = NULL;
    MapCell *pCursorCell = NULL;
    const PufoOptions *pOpt = (g_world.pFrameHooks != NULL)
        ? g_world.pFrameHooks->pTactOptions : NULL;
    i32 nCursorLayer;
    i32 nActualPathFlag = -1;
    i32 nPreviewPathFlag = -1;
    i32 nRunOption = (pOpt != NULL) ? (i32)pOpt->bRunByDblClick : -1;
    bool bOwners = App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey,
                                                      &pMap, &pQueue);

    if (bOwners) {
        pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey);
        pSource = (pUnit != NULL) ? pUnit->pCell : NULL;
        pCandidate = App_ScriptCampaignCellAt(nTargetX, nTargetY, nTargetZ);
        pDest = pCandidate;
        if (pUnit != NULL && pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT) {
            nActualPathFlag = (i32)g_aUnitTypeFlag0[pUnit->nUnitType];
        }
        if (pCandidate != NULL && pSource != NULL) {
            nPreviewPathFlag = pCandidate != pSource ? 1 : 0;
            if (pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT
                && g_aUnitTypeFlag0[pUnit->nUnitType] == 0) {
                pDest = MapCell_FloorCellBelow(g_world.sTactPath.pfnLookup,
                                               g_world.sTactPath.pLookupUser,
                                               nTargetX, nTargetY, nTargetZ);
            }
        }
        nCursorLayer = (i32)g_world.tactEnv.cursor.nFlags - 1;
        pCursorCell = App_ScriptCampaignCellAt(g_world.tactEnv.cursor.nX,
                                                g_world.tactEnv.cursor.nY,
                                                nCursorLayer);
    } else {
        nCursorLayer = -1;
    }

    printf("pocketufo: campaign moveprobe event=%s pen=%d/%d owners=%d "
           "screen=%u tact=%u geoscape=%u optionRun=%d clickCommits=%u slot3=%d "
           "actionFlag=%u actionMode=%d inputLatch=%u mapLatch=%u "
           "mapInput=%u inputOn=%u canScroll=%d fire=%u/%u traj=%u busy=%u "
           "queue=%d/%d active=%u selected=%d type=%u typePathFlag=%d previewPathFlag=%d "
           "source=%d/%d/%d sourceFloor=%d sourceFloorPtr=0x%llX sourcePtr=0x%llX "
           "target=%d/%d/%d candidateFloor=%d candidatePtr=0x%llX "
           "cursor=%d/%d/%d cursorPtr=0x%llX cursorMatch=%d "
           "dest=%d/%d/%d destFloor=%d destCost=%d destPtr=0x%llX\n",
           pszEvent, (int)nPenX, (int)nPenY, bOwners ? 1 : 0,
           (unsigned)g_world.nUiScreen, (unsigned)g_world.bTactFlag,
           (unsigned)g_world.bGeoscapeUi, nRunOption,
           (unsigned)g_world.sTactInput.bClickCommits,
           (int)g_world.sTactInput.nUiSlot3, g_pointerWorld.bActionFlag ? 1u : 0u,
           (int)g_world.nMapActionMode, (unsigned)g_world.sTactInput.nActionLatch,
           pMap != NULL ? (unsigned)pMap->bActionLatch : 255u,
           (unsigned)g_world.bMapPlayerInput,
           (unsigned)g_world.sTactInput.bPlayerInputOn,
           bOwners ? (TactInput_CanScroll(&g_world.sTactInput) ? 1 : 0) : 0,
           pMap != NULL ? (unsigned)pMap->bFireStatus1 : 255u,
           (unsigned)g_world.bMapFire2,
           pMap != NULL ? (unsigned)pMap->bTrajHitStop : 255u,
           pMap != NULL ? (unsigned)pMap->tact.bBusy : 255u,
           pQueue != NULL ? (int)pQueue->nQueueLen : -1,
           pQueue != NULL ? (int)pQueue->nQueueCursor : -1,
           pQueue != NULL ? (unsigned)pQueue->bQueueActive : 255u,
           pUnit != NULL && pMap != NULL && pMap->pSelUnit == pUnit ? 1 : 0,
           pUnit != NULL ? (unsigned)pUnit->nUnitType : 255u,
           nActualPathFlag, nPreviewPathFlag,
           pSource != NULL ? (int)pSource->nX : -1,
           pSource != NULL ? (int)pSource->nY : -1,
           pSource != NULL ? (int)pSource->nZ : -1,
           pSource != NULL && pSource->pFloorSpr != NULL ? 1 : 0,
           (unsigned long long)(uintptr_t)(pSource != NULL ? (void *)pSource->pFloorSpr : NULL),
           (unsigned long long)(uintptr_t)(void *)pSource,
           (int)nTargetX, (int)nTargetY, (int)nTargetZ,
           pCandidate != NULL && pCandidate->pFloorSpr != NULL ? 1 : 0,
           (unsigned long long)(uintptr_t)(void *)pCandidate,
           (int)g_world.tactEnv.cursor.nX, (int)g_world.tactEnv.cursor.nY,
           nCursorLayer,
           (unsigned long long)(uintptr_t)(void *)pCursorCell,
           pCursorCell != NULL && pCursorCell == pCandidate ? 1 : 0,
           pDest != NULL ? (int)pDest->nX : -1,
           pDest != NULL ? (int)pDest->nY : -1,
           pDest != NULL ? (int)pDest->nZ : -1,
           pDest != NULL && pDest->pFloorSpr != NULL ? 1 : 0,
           pDest != NULL ? (int)pDest->nPathCost : -1,
           (unsigned long long)(uintptr_t)(void *)pDest);
    fflush(stdout);
}

static bool App_ScriptCampaignPressMoveProbePixel(i32 nX, i32 nY,
                                                   uintptr_t nMapKey,
                                                   uintptr_t nQueueKey,
                                                   uintptr_t nUnitKey,
                                                   i32 nTargetX, i32 nTargetY,
                                                   i32 nTargetZ)
{
    for (i32 i = 0; i < 2; ++i) {
        App_ScriptPointerLogical(nX, nY, true);
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) {
            printf("pocketufo: campaign moveprobe event=owner_lost_down%d\n", (int)(i + 1));
            fflush(stdout);
            return false;
        }
        App_ScriptCampaignTraceMoveProbeEvent(i == 0 ? "down1" : "down2",
                                               nX, nY, nMapKey, nQueueKey, nUnitKey,
                                               nTargetX, nTargetY, nTargetZ);
        App_OnPointerUp();
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) {
            printf("pocketufo: campaign moveprobe event=owner_lost_up%d\n", (int)(i + 1));
            fflush(stdout);
            return false;
        }
        App_ScriptCampaignTraceMoveProbeEvent(i == 0 ? "up1" : "up2",
                                               nX, nY, nMapKey, nQueueKey, nUnitKey,
                                               nTargetX, nTargetY, nTargetZ);
    }
    return true;
}

static void App_ScriptCampaignTraceStartSteps(const Soldier *pUnit)
{
    if (pUnit == NULL || pUnit->pCell == NULL || g_world.sTactPath.pfnProbe == NULL
        || g_world.pTactMap == NULL) {
        return;
    }
    for (u8 nDir = 0; nDir < PUFO_PATH_DIR_COUNT; ++nDir) {
        i32 nX = -1, nY = -1, nZ = -1;
        bool bReanchored = false;
        const bool bAllowed = g_world.sTactPath.pfnProbe(pUnit->pCell->nX, pUnit->pCell->nY,
                                                         pUnit->pCell->nZ, nDir,
                                                         &nX, &nY, &nZ, &bReanchored,
                                                         g_world.sTactPath.pProbeUser);
        const MapCell *pNext = App_ScriptCampaignCellAt(nX, nY, nZ);

        printf("pocketufo: campaign step probe: dir %u allowed %d dst %d/%d/%d reanchor %d "
               "cell %p floor %d blank %d unit %p wallAB %d/%d flags57 %02X\n",
               (unsigned)nDir, (int)bAllowed, (int)nX, (int)nY, (int)nZ,
               (int)bReanchored, (const void *)pNext,
               pNext != NULL && pNext->pFloorSpr != NULL,
               pNext != NULL ? (int)MapCell_TestFlag((MapCell *)pNext, CELLFLAG_IS_BLANK) : -1,
               pNext != NULL ? (void *)pNext->pUnit : NULL,
               pNext != NULL ? (int)MapCell_TestFlag((MapCell *)pNext, CELLFLAG_WALL_A_BLOCKS) : -1,
               pNext != NULL ? (int)MapCell_TestFlag((MapCell *)pNext, CELLFLAG_WALL_B_BLOCKS) : -1,
               pNext != NULL ? (unsigned)pNext->bFlags57[0] : 0u);
    }
    fflush(stdout);
}

static i32 App_ScriptCampaignCollectNearbyCells(Soldier *pUnit,
                                                 AppCampaignCellPick *aPick,
                                                 i32 nCapacity, i32 nRadius,
                                                 i32 nTargetLayer,
                                                 bool bRequirePath,
                                                 const AppCampaignPlaneRect *pPlane,
                                                 i32 nMaxPathCost,
                                                 AppCampaignCellPick *pBestPath)
{
    const PadImage *pHud;
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    const uintptr_t nUnitKey = (uintptr_t)(void *)pUnit;
    i32 nCount = 0, nPixels = 0, nNear = 0, nSurface = 0, nReachable = 0;
    i32 nBudgetRejected = 0, nMinPathCost = -1, nMinPathX = -1, nMinPathY = -1, nMinPathZ = -1;
    i32 nSearchFlagOne = 0, nSearchFlagZero = 0;
    i32 nMapBottom;
    bool bLoggedFirstPath = false;

    if (pUnit == NULL || aPick == NULL || nCapacity <= 0
        || nTargetLayer < 0 || nTargetLayer >= PUFO_MAP_LAYERS
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL) {
        return 0;
    }
    while ((i32)g_world.tactEnv.cursor.nFlags < nTargetLayer + 1) {
        if (!App_ScriptCampaignClickBank(2)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL) {
            return nCount;
        }
    }
    while ((i32)g_world.tactEnv.cursor.nFlags > nTargetLayer + 1) {
        if (!App_ScriptCampaignClickBank(7)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL) {
            return nCount;
        }
    }
    pHud = g_world.apTactPads[PUFO_UIPAD_HUD];
    nMapBottom = (pHud != NULL) ? (i32)pHud->nOriginY - 20 : 240;
    for (i32 y = 20; y <= nMapBottom; y += 4) {
        for (i32 x = 24; x <= 216; x += 4) {
            i32 nCellX, nCellY, nCellZ, nDx, nDy, nDistance, nPathCost = 0;
            MapCell *pCell;
            bool bSeen = false;

            App_ScriptPointerLogical(x, y, false);
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL) {
                return nCount;
            }
            ++nPixels;
            nCellX = g_world.tactEnv.cursor.nX;
            nCellY = g_world.tactEnv.cursor.nY;
            nCellZ = (i32)g_world.tactEnv.cursor.nFlags - 1;
            nDx = nCellX - pUnit->pCell->nX;
            nDy = nCellY - pUnit->pCell->nY;
            if (nDx < 0) { nDx = -nDx; }
            if (nDy < 0) { nDy = -nDy; }
            nDistance = (nDx > nDy) ? nDx : nDy;
            if (nCellZ != nTargetLayer
                || (nDistance == 0 && nCellZ == (i32)pUnit->pCell->nZ)
                || nDistance > nRadius
                || App_ScriptCampaignCellInsidePlane(pPlane, nCellX, nCellY)) {
                continue;
            }
            ++nNear;
            pCell = App_ScriptCampaignCellAt(nCellX, nCellY, nCellZ);
            if (pCell == NULL || pCell->pFloorSpr == NULL || pCell->pUnit != NULL
                || MapCell_TestFlag(pCell, CELLFLAG_IS_BLANK)) {
                continue;
            }
            ++nSurface;
            for (i32 k = 0; k < nCount; ++k) {
                if (aPick[k].nX == nCellX && aPick[k].nY == nCellY && aPick[k].nZ == nCellZ) {
                    bSeen = true;
                    break;
                }
            }
            if (bSeen || nCount >= nCapacity) {
                continue;
            }
            if (bRequirePath) {
                /* Unit_PathToCell @ 0x2c2a8 uses the selected unit's type byte
                 * for Map_ComputeReachable, not its bFlag argument (which only
                 * clears Map_pPathDestUnit when set). Keep this preview on that
                 * same path rule. */
                const bool bPathSearchFlag = pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT
                    && g_aUnitTypeFlag0[pUnit->nUnitType] != 0;
                if (bPathSearchFlag) {
                    ++nSearchFlagOne;
                } else {
                    ++nSearchFlagZero;
                }
                const bool bReachable = Path_ComputeReachable(&g_world.sTactPathScratch,
                                                              pUnit->pCell->nX, pUnit->pCell->nY,
                                                              pUnit->pCell->nZ, pUnit->bFacing,
                                                              nCellX, nCellY, nCellZ, bPathSearchFlag,
                                                              g_world.sTactPath.pfnLookup,
                                                              g_world.sTactPath.pfnProbe,
                                                              g_world.sTactPath.pProbeUser);

                if (!bReachable) {
                    if (g_nCampaignPathRejectShown < 4) {
                        printf("pocketufo: campaign path reject: start %d/%d/%d floor %p blank %u unit %p; "
                               "target %d/%d/%d floor %p blank %u cost %d searchFlag %d type %u facing %u "
                               "selmatch %d swap %p probe %d/%d scratch %d/%d\n",
                               (int)pUnit->pCell->nX, (int)pUnit->pCell->nY, (int)pUnit->pCell->nZ,
                               (void *)pUnit->pCell->pFloorSpr,
                               (unsigned)MapCell_TestFlag(pUnit->pCell, CELLFLAG_IS_BLANK),
                               (void *)pUnit->pCell->pUnit,
                               (int)nCellX, (int)nCellY, (int)nCellZ, (void *)pCell->pFloorSpr,
                               (unsigned)MapCell_TestFlag(pCell, CELLFLAG_IS_BLANK),
                               (int)pCell->nPathCost, (int)bPathSearchFlag,
                               (unsigned)pUnit->nUnitType, (unsigned)pUnit->bFacing,
                               g_world.pTactMap->pSelUnit == pUnit,
                               (void *)g_world.pTactMap->pPathDestUnit,
                               g_world.sTactPath.pfnLookup != NULL,
                               g_world.sTactPath.pfnProbe != NULL,
                               (int)g_world.sTactPathScratch.nCountA,
                               (int)g_world.sTactPathScratch.nCountB);
                        fflush(stdout);
                        ++g_nCampaignPathRejectShown;
                        if (!g_bCampaignStartStepTraced) {
                            App_ScriptCampaignTraceStartSteps(pUnit);
                            g_bCampaignStartStepTraced = true;
                        }
                    }
                    continue;
                }
                nPathCost = (i32)pCell->nPathCost;
                if (pPlane != NULL) {
                    ++nReachable;
                    if (nMinPathCost < 0 || nPathCost < nMinPathCost) {
                        nMinPathCost = nPathCost;
                        nMinPathX = nCellX;
                        nMinPathY = nCellY;
                        nMinPathZ = nCellZ;
                    }
                    if (pBestPath != NULL
                        && (pBestPath->nPathCost <= 0 || nPathCost < pBestPath->nPathCost)) {
                        *pBestPath = (AppCampaignCellPick){
                            nCellX, nCellY, nCellZ, x, y, nDistance, nPathCost
                        };
                    }
                    if (!bLoggedFirstPath) {
                        printf("pocketufo: campaign outside-plane path sample: layer %d source %d/%d/%d "
                               "target %d/%d/%d pathCost %d maxPathCost %d\n",
                               (int)nTargetLayer, (int)pUnit->pCell->nX,
                               (int)pUnit->pCell->nY, (int)pUnit->pCell->nZ,
                               (int)nCellX, (int)nCellY, (int)nCellZ,
                               (int)nPathCost, (int)nMaxPathCost);
                        fflush(stdout);
                        bLoggedFirstPath = true;
                    }
                    if (nPathCost <= 0 || nPathCost > nMaxPathCost) {
                        ++nBudgetRejected;
                        continue;
                    }
                } else {
                    ++nReachable;
                }
            }
            aPick[nCount].nX = nCellX;
            aPick[nCount].nY = nCellY;
            aPick[nCount].nZ = nCellZ;
            aPick[nCount].nScreenX = x;
            aPick[nCount].nScreenY = y;
            aPick[nCount].nDistance = nDistance;
            aPick[nCount].nPathCost = nPathCost;
            ++nCount;
        }
    }
    printf("pocketufo: campaign cell scan: layer %d pixels %d near %d floor %d reachable %d picks %d radius %d path %d searchFlagOne %d searchFlagZero %d\n",
           (int)nTargetLayer,
           (int)nPixels, (int)nNear, (int)nSurface, (int)nReachable,
           (int)nCount, (int)nRadius, (int)bRequirePath,
           (int)nSearchFlagOne, (int)nSearchFlagZero);
    fflush(stdout);
    if (pPlane != NULL) {
        printf("pocketufo: campaign outside-plane layer scan: layer %d pixels %d near %d floor %d "
               "reachable %d minPathCost %d minAt %d/%d/%d budgetRejected %d maxPathCost %d picks %d\n",
               (int)nTargetLayer, (int)nPixels, (int)nNear, (int)nSurface,
               (int)nReachable, (int)nMinPathCost, (int)nMinPathX, (int)nMinPathY,
               (int)nMinPathZ, (int)nBudgetRejected, (int)nMaxPathCost, (int)nCount);
        fflush(stdout);
    }
    return nCount;
}

static bool App_ScriptCampaignPressCellPixel(i32 nX, i32 nY)
{
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;

    for (i32 i = 0; i < 2; ++i) {
        App_ScriptPointerLogical(nX, nY, true);
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
        App_OnPointerUp();
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) { return false; }
    }
    return true;
}

static bool App_ScriptCampaignTryMoveUnit(Soldier *pUnit, bool bProbe,
                                           bool *pbProbeAttempted,
                                           const AppCampaignPlaneRect *pPlane,
                                           i32 nSnapCost, i32 *pnPathCost)
{
    static const i8 aViewOffset[9][2] = {
        { 0, 0 }, { -8, 0 }, { 8, 0 }, { 0, -8 }, { 0, 8 },
        { -8, -8 }, { 8, -8 }, { -8, 8 }, { 8, 8 }
    };
    AppCampaignCellPick aPick[256];
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    uintptr_t nUnitKey;
    i32 nX0, nY0, nZ0;
    u16 uTu0;
    u16 uHp0;

    if (pnPathCost != NULL) { *pnPathCost = 0; }

    if (pbProbeAttempted != NULL) {
        *pbProbeAttempted = false;
    }

    if (pUnit == NULL || pUnit->pCell == NULL
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
        return false;
    }
    nUnitKey = (uintptr_t)(void *)pUnit;
    pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey);
    if (pUnit == NULL) { return false; }
    if (pPlane != NULL && pMap->pSelUnit != pUnit) {
        printf("pocketufo: campaign outside-plane move: FAIL requested owner is not selected\n");
        fflush(stdout);
        return false;
    }
    nX0 = pUnit->pCell->nX;
    nY0 = pUnit->pCell->nY;
    nZ0 = pUnit->pCell->nZ;
    uTu0 = pUnit->uTu;
    uHp0 = pUnit->uHp;
    if (pPlane != NULL && (nSnapCost <= 0 || nSnapCost >= (i32)uTu0)) {
        printf("pocketufo: campaign outside-plane move: FAIL quoted Snap cost cannot fit a live path "
               "actor 0x%llX snapCost %d tu %u\n",
               (unsigned long long)nUnitKey, (int)nSnapCost, (unsigned)uTu0);
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign move actor: selected id %d at %d/%d/%d tu %u through widget0\n",
           (int)pUnit->nSaveId, (int)nX0, (int)nY0, (int)nZ0, (unsigned)uTu0);
    fflush(stdout);

    for (i32 nView = 0; nView < (bProbe ? 1 : 9); ++nView) {
        const i32 nViewX = nX0 + (i32)aViewOffset[nView][0];
        const i32 nViewY = nY0 + (i32)aViewOffset[nView][1];

        while ((i32)g_world.tactEnv.cursor.nFlags < nZ0 + 1) {
            if (!App_ScriptCampaignClickBank(2)
                || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)
                || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL
                || (pPlane != NULL && pMap->pSelUnit != pUnit)) {
                return false;
            }
        }
        while ((i32)g_world.tactEnv.cursor.nFlags > nZ0 + 1) {
            if (!App_ScriptCampaignClickBank(7)
                || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)
                || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL
                || (pPlane != NULL && pMap->pSelUnit != pUnit)) {
                return false;
            }
        }
        if (!App_ScriptCampaignPanToCell(nViewX, nViewY)) {
            printf("pocketufo: campaign move: FAIL (two-axis key pan to %d/%d failed)\n",
                   (int)nViewX, (int)nViewY);
            fflush(stdout);
            return false;
        }
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)
            || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL
            || (pPlane != NULL && pMap->pSelUnit != pUnit)) {
            return false;
        }
        for (i32 nLayerScan = 0;
             nLayerScan < ((pPlane != NULL) ? PUFO_MAP_LAYERS : 1); ++nLayerScan) {
            const i32 nTargetLayer = (pPlane != NULL) ? nLayerScan : nZ0;
            const i32 nPickCount = App_ScriptCampaignCollectNearbyCells(
                pUnit, aPick, (i32)(sizeof(aPick) / sizeof(aPick[0])),
                20, nTargetLayer, true, pPlane,
                (pPlane != NULL) ? (i32)uTu0 - nSnapCost : 0, NULL);
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)
                || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL
                || (pPlane != NULL && pMap->pSelUnit != pUnit)) {
                return false;
            }
            u8 abTried[256] = { 0 };

            for (i32 k = 0; k < nPickCount; ++k) {
            i32 nBest = -1;
            for (i32 j = 0; j < nPickCount; ++j) {
                if (abTried[j] != 0) { continue; }
                if (nBest < 0 || aPick[j].nDistance < aPick[nBest].nDistance) { nBest = j; }
            }
            if (nBest < 0) { break; }
            abTried[nBest] = 1;
            printf("pocketufo: campaign move candidate: %d/%d/%d via pen %d/%d distance %d\n",
                   (int)aPick[nBest].nX, (int)aPick[nBest].nY, (int)aPick[nBest].nZ,
                   (int)aPick[nBest].nScreenX, (int)aPick[nBest].nScreenY,
                   (int)aPick[nBest].nDistance);
            fflush(stdout);
            if (bProbe) {
                if (pbProbeAttempted != NULL) {
                    *pbProbeAttempted = true;
                }
                App_ScriptCampaignTraceMoveProbeEvent("before",
                                                       aPick[nBest].nScreenX,
                                                       aPick[nBest].nScreenY,
                                                       nMapKey, nQueueKey, nUnitKey,
                                                       aPick[nBest].nX,
                                                       aPick[nBest].nY,
                                                       aPick[nBest].nZ);
                if (!App_ScriptCampaignPressMoveProbePixel(aPick[nBest].nScreenX,
                                                            aPick[nBest].nScreenY,
                                                            nMapKey, nQueueKey, nUnitKey,
                                                            aPick[nBest].nX,
                                                            aPick[nBest].nY,
                                                            aPick[nBest].nZ)) {
                    return false;
                }
                App_ScriptCampaignTraceMoveProbeEvent("after",
                                                       aPick[nBest].nScreenX,
                                                       aPick[nBest].nScreenY,
                                                       nMapKey, nQueueKey, nUnitKey,
                                                       aPick[nBest].nX,
                                                       aPick[nBest].nY,
                                                       aPick[nBest].nZ);
                (void)App_ScriptCampaignState("move_probe_after");
                App_ScriptTact();
                App_ScriptDump("normal_ground_move_probe_after.ppm");
                if (App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
                    printf("pocketufo: campaign moveprobe: STOP after one candidate, "
                           "queue %u/%d active %u; no tick or further candidate followed\n",
                           (unsigned)pQueue->nQueueCursor, (int)pQueue->nQueueLen,
                           (unsigned)pQueue->bQueueActive);
                } else {
                    printf("pocketufo: campaign moveprobe: STOP after one candidate, "
                           "tactical owners changed during the real pointer path\n");
                }
                fflush(stdout);
                return false;
            }
            if (pPlane != NULL) {
                i32 nRosterSlot = -1;
                for (i32 i = 0; i < (i32)pMap->nXcomCount; ++i) {
                    if (pMap->apXcomUnits[i] == pUnit) { nRosterSlot = i; break; }
                }
                printf("pocketufo: campaign outside-plane move quote: actor 0x%llX roster %d targetLayer %d "
                       "from %d/%d/%d to %d/%d/%d plane %d..%d/%d..%d pathCost %d snapCost %d "
                       "tuBefore %u hpBefore %u\n",
                        (unsigned long long)nUnitKey, (int)nRosterSlot, (int)aPick[nBest].nZ,
                       (int)nX0, (int)nY0, (int)nZ0,
                       (int)aPick[nBest].nX, (int)aPick[nBest].nY, (int)aPick[nBest].nZ,
                       (int)pPlane->nMinX, (int)pPlane->nMaxX,
                       (int)pPlane->nMinY, (int)pPlane->nMaxY,
                       (int)aPick[nBest].nPathCost, (int)nSnapCost,
                       (unsigned)uTu0, (unsigned)uHp0);
                fflush(stdout);
                if (pnPathCost != NULL) { *pnPathCost = aPick[nBest].nPathCost; }
            }
            if (pPlane != NULL) {
                u8 bFallGate, bFireGate, bThrowGate, bQueueGate;
                if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                    || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL
                    || pMap->pSelUnit != pUnit || pUnit->pCell == NULL
                    || pUnit->uTu != uTu0 || pUnit->uHp != uHp0) {
                    printf("pocketufo: campaign outside-plane move: FAIL owner changed before pointer input\n");
                    fflush(stdout);
                    return false;
                }
                bFallGate = g_world.bMapFire2;
                bFireGate = pMap->bFireStatus1;
                bThrowGate = pMap->bTrajHitStop;
                bQueueGate = pQueue->bQueueActive;
                printf("pocketufo: campaign outside-plane move input gate: actor 0x%llX "
                       "target %d/%d/%d fall %u fire %u throw %u queueActive %u queueLen %d "
                       "screen %u modal %u player %u latch %u busy %u\n",
                       (unsigned long long)nUnitKey,
                       (int)aPick[nBest].nX, (int)aPick[nBest].nY, (int)aPick[nBest].nZ,
                       (unsigned)bFallGate, (unsigned)bFireGate, (unsigned)bThrowGate,
                       (unsigned)bQueueGate, (int)pQueue->nQueueLen,
                       (unsigned)g_world.nUiScreen, (unsigned)g_world.sModal.bOpen,
                       (unsigned)g_world.bMapPlayerInput, (unsigned)pMap->bActionLatch,
                       (unsigned)pMap->tact.bBusy);
                fflush(stdout);
                if (bFallGate != 0 || bFireGate != 0 || bThrowGate != 0
                    || bQueueGate != 0 || pQueue->nQueueLen != 0
                    || g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
                    || g_world.bMapPlayerInput == 0
                    || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                    || pMap->tact.bBusy != 0) {
                    printf("pocketufo: campaign outside-plane move: FAIL Input_OnAction gate is not clear\n");
                    fflush(stdout);
                    return false;
                }
            }
            if (!App_ScriptCampaignPressCellPixel(aPick[nBest].nScreenX, aPick[nBest].nScreenY)) {
                return false;
            }
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL
                || (pPlane != NULL && pMap->pSelUnit != pUnit)) {
                return false;
            }
            if (pQueue->nQueueLen == 0 && pQueue->bQueueActive == 0) {
                printf("pocketufo: campaign move candidate: no path command entered the live queue\n");
                fflush(stdout);
                continue;
            }
            if (!App_ScriptCampaignDrainQueue(1600)) {
                printf("pocketufo: campaign move candidate: live queue did not drain\n");
                fflush(stdout);
                return false;
            }
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)
                || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL
                || (pPlane != NULL && pMap->pSelUnit != pUnit)) {
                return false;
            }
            if (pUnit->pCell != NULL && (pUnit->pCell->nX != nX0 || pUnit->pCell->nY != nY0
                                         || pUnit->pCell->nZ != nZ0)) {
                if (pPlane != NULL) {
                    const bool bOutside = !App_ScriptCampaignCellInsidePlane(
                        pPlane, (i32)pUnit->pCell->nX, (i32)pUnit->pCell->nY);
                    if (!bOutside || pMap->pSelUnit != pUnit || pUnit->uTu >= uTu0
                        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
                        || pMap->tact.bBusy != 0) {
                        printf("pocketufo: campaign outside-plane move: FAIL post-move actor/queue/TU "
                               "selected %d outside %d tu %u->%u queue %d/%u busy %u\n",
                               pMap->pSelUnit == pUnit ? 1 : 0, bOutside ? 1 : 0,
                               (unsigned)uTu0, (unsigned)pUnit->uTu,
                               (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive,
                               (unsigned)pMap->tact.bBusy);
                        fflush(stdout);
                        return false;
                    }
                    printf("pocketufo: campaign outside-plane move: actor 0x%llX "
                           "%d/%d/%d -> %d/%d/%d pathCost %d snapCost %d "
                           "tu %u -> %u hp %u -> %u queue %d/%u outside 1\n",
                           (unsigned long long)nUnitKey,
                           (int)nX0, (int)nY0, (int)nZ0,
                           (int)pUnit->pCell->nX, (int)pUnit->pCell->nY, (int)pUnit->pCell->nZ,
                           pnPathCost != NULL ? (int)*pnPathCost : (int)aPick[nBest].nPathCost,
                           (int)nSnapCost, (unsigned)uTu0, (unsigned)pUnit->uTu,
                           (unsigned)uHp0, (unsigned)pUnit->uHp,
                           (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive);
                    fflush(stdout);
                }
                printf("pocketufo: campaign move: %d/%d/%d -> %d/%d/%d, tu %u -> %u, queue drained\n",
                       (int)nX0, (int)nY0, (int)nZ0, (int)pUnit->pCell->nX,
                       (int)pUnit->pCell->nY, (int)pUnit->pCell->nZ,
                       (unsigned)uTu0, (unsigned)pUnit->uTu);
                fflush(stdout);
                return true;
            }
            }
        }
    }
    return false;
}

static bool App_ScriptCampaignMove(bool bProbe)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    uintptr_t nMapKey, nQueueKey;
    uintptr_t nPreviousKey;
    i32 nRosterCount;

    nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || g_world.nUiScreen != 0 || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0
        || pQueue->bQueueActive != 0 || pMap->pSelUnit == NULL
        || App_ScriptCampaignFindActiveXcom(pMap, (uintptr_t)(void *)pMap->pSelUnit) == NULL) {
        printf("pocketufo: campaign move: FAIL (need an idle selected unit)\n");
        fflush(stdout);
        return false;
    }

    g_nCampaignPathRejectShown = 0;
    g_bCampaignStartStepTraced = false;
    nRosterCount = (i32)pMap->nXcomCount;
    if (bProbe) {
        bool bProbeAttempted = false;
        (void)App_ScriptCampaignTryMoveUnit(pMap->pSelUnit, true, &bProbeAttempted,
                                            NULL, 0, NULL);
        if (!bProbeAttempted) {
            printf("pocketufo: campaign moveprobe: STOP without input, no first candidate "
                   "was available for the selected actor\n");
            fflush(stdout);
        }
        return false;
    }
    for (i32 nTry = 0; nTry < nRosterCount; ++nTry) {
        uintptr_t nUnitKey;
        Soldier *pUnit;
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
            return false;
        }
        pUnit = pMap->pSelUnit;
        nUnitKey = (uintptr_t)(void *)pUnit;
        pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey);

        if (pUnit == NULL || pUnit->pCell == NULL) {
            printf("pocketufo: campaign move: FAIL (roster cycle selected no placed unit)\n");
            fflush(stdout);
            return false;
        }
        if (App_ScriptCampaignTryMoveUnit(pUnit, false, NULL, NULL, 0, NULL)) {
            return true;
        }
        if (g_world.nUiScreen == 18u || g_world.nUiScreen == 13u
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
            return false;
        }

        if (nTry + 1 < nRosterCount) {
            if (!App_ScriptCampaignClickBank(0)) {
                printf("pocketufo: campaign move: FAIL (live roster cycle click failed)\n");
                fflush(stdout);
                return false;
            }
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
                return false;
            }
            nPreviousKey = (uintptr_t)(void *)pMap->pSelUnit;
            if (nPreviousKey == nUnitKey) {
                printf("pocketufo: campaign move: FAIL (live roster cycle did not change unit)\n");
                fflush(stdout);
                return false;
            }
        }
    }

    printf("pocketufo: campaign move: FAIL (no reachable visible floor cell for any live XCOM unit)\n");
    fflush(stdout);
    return false;
}

/* Test-only target knowledge: use the real cell lookup, unit step probe and
 * original reachability costs to choose a closer empty cell, then click it
 * through the normal tactical input path. This helper never edits the unit or
 * mission state directly. */
static i32 App_ScriptCampaignCellDistance(const MapCell *pFrom, const MapCell *pTo)
{
    i32 nDx, nDy, nDz, nDistance;

    if (pFrom == NULL || pTo == NULL) { return INT32_MAX; }
    nDx = (i32)pFrom->nX - (i32)pTo->nX;
    nDy = (i32)pFrom->nY - (i32)pTo->nY;
    nDz = (i32)pFrom->nZ - (i32)pTo->nZ;
    if (nDx < 0) { nDx = -nDx; }
    if (nDy < 0) { nDy = -nDy; }
    if (nDz < 0) { nDz = -nDz; }
    nDistance = (nDx > nDy) ? nDx : nDy;
    return nDz > nDistance ? nDz : nDistance;
}

static i32 App_ScriptCampaignClosestLiveEnemyIndex(const GameMap *pMap,
                                                   const Soldier *pUnit)
{
    i32 nBestIndex = -1;
    i32 nBestDistance = INT32_MAX;

    if (pMap == NULL || pUnit == NULL || pUnit->pCell == NULL) { return -1; }
    for (i32 i = 0; i < (i32)pMap->nEnemyCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
        const Soldier *pEnemy = pMap->apEnemyUnits[i];
        i32 nDistance;

        if (pEnemy == NULL || pEnemy->pCell == NULL || pEnemy->uHp == 0
            || pEnemy->bStatus >= UNIT_STATUS_INACTIVE) {
            continue;
        }
        nDistance = App_ScriptCampaignCellDistance(pUnit->pCell, pEnemy->pCell);
        if (nDistance < nBestDistance) {
            nBestDistance = nDistance;
            nBestIndex = i;
        }
    }
    return nBestIndex;
}

typedef enum AppCampaignApproachResult {
    APP_CAMPAIGN_APPROACH_FAILED = 0,
    APP_CAMPAIGN_APPROACH_MOVED = 1,
    APP_CAMPAIGN_APPROACH_PARTIAL = 2,
    APP_CAMPAIGN_APPROACH_INTERRUPTED = 3
} AppCampaignApproachResult;

static bool App_ScriptCampaignApproach(
    i32 nEnemyIndex, bool bSameFloor, const AppCampaignPlaneRect *pOutsidePlane,
    bool *pbActionSpent, AppCampaignApproachResult *peOutsideResult)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit;
    Soldier *pEnemy;
    MapCell *pStart, *pEnemyCell, *pBestCell = NULL, *pPlannedEndCell = NULL;
    i32 nStartX, nStartY, nStartZ, nStartDistance;
    i32 nEnemyX, nEnemyY, nEnemyZ;
    i32 nTargetX, nTargetY, nTargetZ;
    i32 nBestDistance = INT32_MAX, nBestCost = INT32_MAX;
    i32 nScreenX = -1, nScreenY = -1;
    u16 uTu0, uEnemyHp0;
    uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    uintptr_t nUnitKey, nEnemyKey;
    bool bTypeFlag;

    if (pbActionSpent != NULL) { *pbActionSpent = false; }
    if (peOutsideResult != NULL) { *peOutsideResult = APP_CAMPAIGN_APPROACH_FAILED; }

    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
        printf("pocketufo: campaign approach: FAIL (tactical owners are not live)\n");
        fflush(stdout);
        return false;
    }
    pUnit = pMap->pSelUnit;
    nUnitKey = (uintptr_t)(void *)pUnit;
    pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey);
    if (g_world.nUiScreen != 0 || g_world.bMapPlayerInput == 0
        || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
        || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE || pUnit == NULL
        || pUnit->nUnitType > 8u || pUnit->uHp == 0
        || pUnit->bStatus >= UNIT_STATUS_INACTIVE || pUnit->pCell == NULL
        || (pOutsidePlane != NULL
            && (g_world.bMapFire2 != 0 || pMap->bFireStatus1 != 0
                || pMap->bTrajHitStop != 0))
        || nEnemyIndex < 0 || nEnemyIndex >= (i32)pMap->nEnemyCount
        || g_world.sTactPath.pfnLookup == NULL || g_world.sTactPath.pfnProbe == NULL) {
        printf("pocketufo: campaign approach: FAIL (need an idle live selected XCOM and path input)\n");
        fflush(stdout);
        return false;
    }
    pEnemy = pMap->apEnemyUnits[nEnemyIndex];
    nEnemyKey = (uintptr_t)(void *)pEnemy;
    pEnemy = App_ScriptCampaignFindActiveEnemy(pMap, nEnemyKey);
    if (pEnemy == NULL || pEnemy->uHp == 0 || pEnemy->bStatus >= UNIT_STATUS_INACTIVE
        || pEnemy->pCell == NULL) {
        printf("pocketufo: campaign approach: FAIL (enemy %d is not live)\n", (int)nEnemyIndex);
        fflush(stdout);
        return false;
    }
    if (bSameFloor && pUnit->pCell->nZ != pEnemy->pCell->nZ) {
        printf("pocketufo: campaign approach: enemy %d is on another floor %u/%u\n",
               (int)nEnemyIndex, (unsigned)pUnit->pCell->nZ, (unsigned)pEnemy->pCell->nZ);
        fflush(stdout);
        return false;
    }

    pStart = pUnit->pCell;
    pEnemyCell = pEnemy->pCell;
    nStartX = (i32)pStart->nX;
    nStartY = (i32)pStart->nY;
    nStartZ = (i32)pStart->nZ;
    nEnemyX = (i32)pEnemyCell->nX;
    nEnemyY = (i32)pEnemyCell->nY;
    nEnemyZ = (i32)pEnemyCell->nZ;
    {
        i32 nDx = nStartX - (i32)pEnemyCell->nX;
        i32 nDy = nStartY - (i32)pEnemyCell->nY;
        i32 nDz = nStartZ - (i32)pEnemyCell->nZ;
        if (nDx < 0) { nDx = -nDx; }
        if (nDy < 0) { nDy = -nDy; }
        if (nDz < 0) { nDz = -nDz; }
        nStartDistance = (nDx > nDy) ? nDx : nDy;
        if (nDz > nStartDistance) { nStartDistance = nDz; }
    }
    if (nStartDistance <= 1) {
        printf("pocketufo: campaign approach: enemy %d is already adjacent at %d/%d/%d\n",
               (int)nEnemyIndex, (int)pEnemyCell->nX, (int)pEnemyCell->nY,
               (int)pEnemyCell->nZ);
        fflush(stdout);
        return false;
    }

    uTu0 = pUnit->uTu;
    uEnemyHp0 = pEnemy->uHp;
    bTypeFlag = pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT
        && g_aUnitTypeFlag0[pUnit->nUnitType] != 0;
    /* The original pathfinder leaves real nPathCost values on every cell it
     * reaches before the occupied target; score only those legal, empty cells. */
    (void)Path_ComputeReachable(&g_world.sTactPathScratch,
                                nStartX, nStartY, nStartZ, pUnit->bFacing,
                                (i32)pEnemyCell->nX, (i32)pEnemyCell->nY,
                                (i32)pEnemyCell->nZ, bTypeFlag,
                                g_world.sTactPath.pfnLookup, g_world.sTactPath.pfnProbe,
                                g_world.sTactPath.pProbeUser);
    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        if (bSameFloor && nLayer != nStartZ) { continue; }
        for (i32 nY = 0; nY < PUFO_MAP_CELL_DIM; ++nY) {
            for (i32 nX = 0; nX < PUFO_MAP_CELL_DIM; ++nX) {
                MapCell *pCell = App_ScriptCampaignCellAt(nX, nY, nLayer);
                i32 nDx, nDy, nDz, nDistance, nCost;

                if (pCell == NULL || pCell == pStart || pCell->pUnit != NULL
                    || pCell->pFloorSpr == NULL || MapCell_TestFlag(pCell, CELLFLAG_IS_BLANK)
                    || pCell->nPathCost == 0
                    || (pOutsidePlane != NULL
                        && App_ScriptCampaignCellInsidePlane(pOutsidePlane, nX, nY))) {
                    continue;
                }
                nCost = (i32)pCell->nPathCost;
                if (nCost > (i32)uTu0) {
                    continue;
                }
                nDx = nX - (i32)pEnemyCell->nX;
                nDy = nY - (i32)pEnemyCell->nY;
                nDz = nLayer - (i32)pEnemyCell->nZ;
                if (nDx < 0) { nDx = -nDx; }
                if (nDy < 0) { nDy = -nDy; }
                if (nDz < 0) { nDz = -nDz; }
                nDistance = (nDx > nDy) ? nDx : nDy;
                if (nDz > nDistance) { nDistance = nDz; }
                if (nDistance >= nStartDistance) {
                    continue;
                }
                if (pBestCell == NULL
                    || (pOutsidePlane != NULL
                        ? (nDistance < nBestDistance
                           || (nDistance == nBestDistance && nCost < nBestCost))
                        : (nCost < nBestCost
                           || (nCost == nBestCost && nDistance < nBestDistance)))) {
                    pBestCell = pCell;
                    nBestDistance = nDistance;
                    nBestCost = nCost;
                }
            }
        }
    }
    if (pBestCell == NULL) {
        printf("pocketufo: campaign approach: no closer path cell for enemy %d within TU %u\n",
               (int)nEnemyIndex, (unsigned)uTu0);
        fflush(stdout);
        return false;
    }
    nTargetX = (i32)pBestCell->nX;
    nTargetY = (i32)pBestCell->nY;
    nTargetZ = (i32)pBestCell->nZ;
    pPlannedEndCell = pBestCell;
    pBestCell = NULL;
    if (!App_ScriptCampaignFindCellPixel(nTargetX, nTargetY, nTargetZ, &nScreenX, &nScreenY)) {
        printf("pocketufo: campaign approach: FAIL (destination %d/%d/%d is not in the live pen view)\n",
               (int)nTargetX, (int)nTargetY, (int)nTargetZ);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || (pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey)) == NULL
        || (pEnemy = App_ScriptCampaignFindActiveEnemy(pMap, nEnemyKey)) == NULL
        || pMap->pSelUnit != pUnit || pUnit->pCell->nX != nStartX
        || pUnit->pCell->nY != nStartY || pUnit->pCell->nZ != nStartZ
        || pEnemy->pCell->nX != nEnemyX
        || pEnemy->pCell->nY != nEnemyY || pEnemy->pCell->nZ != nEnemyZ) {
        printf("pocketufo: campaign approach: FAIL (live actor or target changed during target scan)\n");
        fflush(stdout);
        return false;
    }
    if (pOutsidePlane != NULL
        && (g_world.bMapFire2 != 0 || pMap->bFireStatus1 != 0
            || pMap->bTrajHitStop != 0 || pMap->tact.bBusy != 0
            || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
            || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE)) {
        printf("pocketufo: campaign outside-plane approach: skip actor 0x%llX target 0x%llX "
               "reason input_gate fall %u fire %u throw %u queue %d/%u latch %u\n",
               (unsigned long long)nUnitKey, (unsigned long long)nEnemyKey,
               (unsigned)g_world.bMapFire2, (unsigned)pMap->bFireStatus1,
               (unsigned)pMap->bTrajHitStop, (int)pQueue->nQueueLen,
               (unsigned)pQueue->bQueueActive, (unsigned)pMap->bActionLatch);
        fflush(stdout);
        return false;
    }
    if (pOutsidePlane != NULL) {
        printf("pocketufo: campaign outside-plane approach input gate: actor 0x%llX "
               "target 0x%llX goal %d/%d/%d fall %u fire %u throw %u queueActive %u "
               "queueLen %d screen %u modal %u player %u latch %u busy %u\n",
               (unsigned long long)nUnitKey, (unsigned long long)nEnemyKey,
               nTargetX, nTargetY, nTargetZ, (unsigned)g_world.bMapFire2,
               (unsigned)pMap->bFireStatus1, (unsigned)pMap->bTrajHitStop,
               (unsigned)pQueue->bQueueActive, (int)pQueue->nQueueLen,
               (unsigned)g_world.nUiScreen, (unsigned)g_world.sModal.bOpen,
               (unsigned)g_world.bMapPlayerInput, (unsigned)pMap->bActionLatch,
               (unsigned)pMap->tact.bBusy);
        fflush(stdout);
    }
    if (!App_ScriptCampaignPressCellPixel(nScreenX, nScreenY)) {
        printf("pocketufo: campaign approach: stopped during map input at terminal screen %u or changed owners\n",
               (unsigned)g_world.nUiScreen);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
        printf("pocketufo: campaign approach: stopped after map input at terminal screen %u or changed owners\n",
               (unsigned)g_world.nUiScreen);
        fflush(stdout);
        return false;
    }
    pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey);
    pEnemy = App_ScriptCampaignFindActiveEnemy(pMap, nEnemyKey);
    if (pUnit == NULL || pEnemy == NULL || pMap->pSelUnit != pUnit) {
        printf("pocketufo: campaign approach: FAIL (actor or target left the live roster after map input)\n");
        fflush(stdout);
        return false;
    }
    if (pQueue->nQueueLen == 0 && pQueue->bQueueActive == 0) {
        printf("pocketufo: campaign approach: FAIL (pointer did not queue a real path command)\n");
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignDrainQueue(1600)) {
        if (pOutsidePlane != NULL) {
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
                printf("pocketufo: campaign outside-plane approach partial: actor 0x%llX "
                       "targetPlan 0x%llX targetPlanCell %d/%d/%d targetNow 0x0 "
                       "targetNowLive 0 targetNowCell -1/-1/-1 goal %d/%d/%d "
                       "goalPointerMatch -1 "
                       "start %d/%d/%d end -1/-1/-1 planDistance %d->%d pathCost %d "
                       "tu %u->-1 debit -1 queue -1/-1 outsideEndpoint 0 reason owner_changed\n",
                       (unsigned long long)nUnitKey, (unsigned long long)nEnemyKey,
                       nEnemyX, nEnemyY, nEnemyZ, nTargetX, nTargetY, nTargetZ,
                       nStartX, nStartY, nStartZ, nStartDistance, nBestDistance,
                       (int)nBestCost, (unsigned)uTu0);
                fflush(stdout);
                if (peOutsideResult != NULL) {
                    *peOutsideResult = APP_CAMPAIGN_APPROACH_INTERRUPTED;
                }
                return false;
            }
            pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey);
            pEnemy = App_ScriptCampaignFindActiveEnemy(pMap, nEnemyKey);
            {
                const bool bActorLive = pUnit != NULL && pUnit->pCell != NULL;
                const bool bTargetLive = pEnemy != NULL && pEnemy->pCell != NULL
                    && pEnemy->uHp != 0 && pEnemy->bStatus < UNIT_STATUS_INACTIVE;
                const i32 nEndX = bActorLive ? (i32)pUnit->pCell->nX : -1;
                const i32 nEndY = bActorLive ? (i32)pUnit->pCell->nY : -1;
                const i32 nEndZ = bActorLive ? (i32)pUnit->pCell->nZ : -1;
                const i32 nTuAfter = bActorLive ? (i32)pUnit->uTu : -1;
                const i32 nDebit = bActorLive ? (i32)uTu0 - nTuAfter : -1;
                const bool bPlannedCellPointer = bActorLive
                    && pUnit->pCell == pPlannedEndCell;
                const bool bActorMoved = bActorLive
                    && (nEndX != nStartX || nEndY != nStartY || nEndZ != nStartZ);

                if (pbActionSpent != NULL) {
                    *pbActionSpent = bActorLive && (nDebit > 0 || bActorMoved);
                }

                printf("pocketufo: campaign outside-plane approach partial: actor 0x%llX "
                       "targetPlan 0x%llX targetPlanCell %d/%d/%d "
                       "targetNow 0x%llX targetNowLive %d targetNowCell %d/%d/%d "
                       "goal %d/%d/%d goalPointerMatch %d start %d/%d/%d end %d/%d/%d "
                       "planDistance %d->%d "
                       "pathCost %d tu %u->%d debit %d queue %d/%u outsideEndpoint %d "
                       "reason queue_not_drained\n",
                       (unsigned long long)nUnitKey, (unsigned long long)nEnemyKey,
                       nEnemyX, nEnemyY, nEnemyZ,
                       (unsigned long long)(bTargetLive ? (uintptr_t)(void *)pEnemy : 0),
                       bTargetLive ? 1 : 0,
                       bTargetLive ? (int)pEnemy->pCell->nX : -1,
                       bTargetLive ? (int)pEnemy->pCell->nY : -1,
                       bTargetLive ? (int)pEnemy->pCell->nZ : -1,
                       nTargetX, nTargetY, nTargetZ, bPlannedCellPointer ? 1 : 0,
                       nStartX, nStartY, nStartZ,
                       nEndX, nEndY, nEndZ, nStartDistance, nBestDistance, (int)nBestCost,
                       (unsigned)uTu0, nTuAfter, nDebit,
                       (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive,
                       bActorLive && !App_ScriptCampaignCellInsidePlane(
                           pOutsidePlane, nEndX, nEndY) ? 1 : 0);
                fflush(stdout);
                if (peOutsideResult != NULL) {
                    *peOutsideResult = APP_CAMPAIGN_APPROACH_INTERRUPTED;
                }
                return false;
            }
        }
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) {
            printf("pocketufo: campaign approach: stopped after movement reaction ended tactical ownership\n");
            fflush(stdout);
            return false;
        }
        printf("pocketufo: campaign approach: FAIL (real path queue did not drain)\n");
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
        if (pOutsidePlane != NULL) {
            printf("pocketufo: campaign outside-plane approach partial: actor 0x%llX "
                   "targetPlan 0x%llX targetPlanCell %d/%d/%d targetNow 0x0 targetNowLive 0 "
                   "targetNowCell -1/-1/-1 goal %d/%d/%d goalPointerMatch -1 "
                   "start %d/%d/%d end -1/-1/-1 "
                   "planDistance %d->%d pathCost %d tu %u->-1 debit -1 queue -1/-1 "
                   "outsideEndpoint 0 reason owner_changed\n",
                   (unsigned long long)nUnitKey, (unsigned long long)nEnemyKey,
                   nEnemyX, nEnemyY, nEnemyZ, nTargetX, nTargetY, nTargetZ,
                   nStartX, nStartY, nStartZ, nStartDistance, nBestDistance,
                   (int)nBestCost, (unsigned)uTu0);
            fflush(stdout);
            if (peOutsideResult != NULL) {
                *peOutsideResult = APP_CAMPAIGN_APPROACH_INTERRUPTED;
            }
            return false;
        }
        printf("pocketufo: campaign approach: FAIL (tactical owner changed during the move)\n");
        fflush(stdout);
        return false;
    }
    pUnit = App_ScriptCampaignFindActiveXcom(pMap, nUnitKey);
    pEnemy = App_ScriptCampaignFindActiveEnemy(pMap, nEnemyKey);
    if (pOutsidePlane != NULL) {
        const bool bActorLive = pUnit != NULL && pUnit->pCell != NULL;
        const bool bTargetLive = pEnemy != NULL && pEnemy->pCell != NULL
            && pEnemy->uHp != 0 && pEnemy->bStatus < UNIT_STATUS_INACTIVE;
        const i32 nEndX = bActorLive ? (i32)pUnit->pCell->nX : -1;
        const i32 nEndY = bActorLive ? (i32)pUnit->pCell->nY : -1;
        const i32 nEndZ = bActorLive ? (i32)pUnit->pCell->nZ : -1;
        const i32 nTuAfter = bActorLive ? (i32)pUnit->uTu : -1;
        const i32 nDebit = bActorLive ? (i32)uTu0 - nTuAfter : -1;
        const bool bQueueIdle = pQueue->nQueueLen == 0 && pQueue->bQueueActive == 0
            && pMap->tact.bBusy == 0;
        const bool bPlannedCellPointer = bActorLive
            && pUnit->pCell == pPlannedEndCell;
        const bool bActorMoved = bActorLive
            && (nEndX != nStartX || nEndY != nStartY || nEndZ != nStartZ);
        const bool bEndpointMatched = bActorLive && pUnit->pCell == pPlannedEndCell
            && nEndX == nTargetX && nEndY == nTargetY && nEndZ == nTargetZ;
        const uintptr_t nTargetNowKey = bTargetLive ? (uintptr_t)(void *)pEnemy : 0;
        const i32 nTargetNowX = bTargetLive ? (i32)pEnemy->pCell->nX : -1;
        const i32 nTargetNowY = bTargetLive ? (i32)pEnemy->pCell->nY : -1;
        const i32 nTargetNowZ = bTargetLive ? (i32)pEnemy->pCell->nZ : -1;
        const i32 nTargetHpAfter = bTargetLive ? (i32)pEnemy->uHp : -1;

        if (pbActionSpent != NULL) {
            *pbActionSpent = bActorLive && (nDebit > 0 || bActorMoved);
        }

        if (bEndpointMatched && bQueueIdle && nDebit > 0
            && !App_ScriptCampaignCellInsidePlane(pOutsidePlane, nEndX, nEndY)) {
            printf("pocketufo: campaign outside-plane approach move: actor 0x%llX "
                   "targetPlan 0x%llX targetPlanCell %d/%d/%d "
                   "targetNow 0x%llX targetNowLive %d targetNowCell %d/%d/%d goal %d/%d/%d "
                   "goalPointerMatch 1 start %d/%d/%d end %d/%d/%d distance %d->%d pathCost %d "
                   "tu %u->%u debit %u targetHp %u->%d queue %d/%u outside 1\n",
                   (unsigned long long)nUnitKey, (unsigned long long)nEnemyKey,
                   nEnemyX, nEnemyY, nEnemyZ, (unsigned long long)nTargetNowKey,
                   bTargetLive ? 1 : 0, nTargetNowX, nTargetNowY, nTargetNowZ,
                   nTargetX, nTargetY, nTargetZ, nStartX, nStartY, nStartZ,
                   nEndX, nEndY, nEndZ, nStartDistance, nBestDistance, (int)nBestCost,
                   (unsigned)uTu0, (unsigned)pUnit->uTu, (unsigned)nDebit,
                   (unsigned)uEnemyHp0, nTargetHpAfter,
                   (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive);
            fflush(stdout);
            if (peOutsideResult != NULL) {
                *peOutsideResult = APP_CAMPAIGN_APPROACH_MOVED;
            }
            return true;
        }

        if (!bActorLive || !bQueueIdle || nDebit <= 0) {
            const char *pszPartialReason = !bActorLive ? "actor_lost" :
                (!bQueueIdle ? "queue_not_idle" : "no_tu_debit");

            printf("pocketufo: campaign outside-plane approach partial: actor 0x%llX "
                   "targetPlan 0x%llX targetPlanCell %d/%d/%d "
                   "targetNow 0x%llX targetNowLive %d targetNowCell %d/%d/%d goal %d/%d/%d "
                   "goalPointerMatch %d start %d/%d/%d end %d/%d/%d planDistance %d->%d pathCost %d "
                   "tu %u->%d debit %d queue %d/%u outsideEndpoint %d reason %s\n",
                   (unsigned long long)nUnitKey, (unsigned long long)nEnemyKey,
                   nEnemyX, nEnemyY, nEnemyZ, (unsigned long long)nTargetNowKey,
                   bTargetLive ? 1 : 0, nTargetNowX, nTargetNowY, nTargetNowZ,
                   nTargetX, nTargetY, nTargetZ, bPlannedCellPointer ? 1 : 0,
                   nStartX, nStartY, nStartZ,
                   nEndX, nEndY, nEndZ, nStartDistance, nBestDistance, (int)nBestCost,
                   (unsigned)uTu0, nTuAfter, nDebit,
                   (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive,
                   bActorLive && !App_ScriptCampaignCellInsidePlane(pOutsidePlane, nEndX, nEndY)
                       ? 1 : 0,
                   pszPartialReason);
            fflush(stdout);
            if (peOutsideResult != NULL) {
                *peOutsideResult = APP_CAMPAIGN_APPROACH_INTERRUPTED;
            }
            return false;
        }

        printf("pocketufo: campaign outside-plane approach partial: actor 0x%llX "
               "targetPlan 0x%llX targetPlanCell %d/%d/%d "
               "targetNow 0x%llX targetNowLive %d targetNowCell %d/%d/%d goal %d/%d/%d "
               "goalPointerMatch %d start %d/%d/%d end %d/%d/%d planDistance %d->%d pathCost %d "
               "tu %u->%u debit %u queue %d/%u outsideEndpoint %d reason endpoint_mismatch\n",
               (unsigned long long)nUnitKey, (unsigned long long)nEnemyKey,
               nEnemyX, nEnemyY, nEnemyZ, (unsigned long long)nTargetNowKey,
               bTargetLive ? 1 : 0, nTargetNowX, nTargetNowY, nTargetNowZ,
               nTargetX, nTargetY, nTargetZ, bPlannedCellPointer ? 1 : 0,
               nStartX, nStartY, nStartZ,
               nEndX, nEndY, nEndZ, nStartDistance, nBestDistance, (int)nBestCost,
               (unsigned)uTu0, (unsigned)pUnit->uTu, (unsigned)nDebit,
               (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive,
               !App_ScriptCampaignCellInsidePlane(pOutsidePlane, nEndX, nEndY) ? 1 : 0);
        fflush(stdout);
        if (peOutsideResult != NULL) {
            *peOutsideResult = APP_CAMPAIGN_APPROACH_PARTIAL;
        }
        return false;
    }
    if (pUnit == NULL || pEnemy == NULL || pMap->pSelUnit != pUnit) {
        printf("pocketufo: campaign approach: FAIL (actor or target left its live roster during movement)\n");
        fflush(stdout);
        return false;
    }
    pEnemyCell = pEnemy->pCell;
    if (pUnit->uTu >= uTu0
        || ((i32)pUnit->pCell->nX == nStartX && (i32)pUnit->pCell->nY == nStartY
            && (i32)pUnit->pCell->nZ == nStartZ)
        || pEnemy->uHp != uEnemyHp0) {
        printf("pocketufo: campaign approach: FAIL (live input did not move the XCOM closer without a shot)\n");
        fflush(stdout);
        return false;
    }
    if (pOutsidePlane != NULL
        && App_ScriptCampaignCellInsidePlane(pOutsidePlane,
                                              (i32)pUnit->pCell->nX,
                                              (i32)pUnit->pCell->nY)) {
        printf("pocketufo: campaign outside-plane approach: FAIL actor 0x%llX reached inside endpoint\n",
               (unsigned long long)nUnitKey);
        fflush(stdout);
        return false;
    }
    {
        i32 nDx = (i32)pUnit->pCell->nX - (i32)pEnemyCell->nX;
        i32 nDy = (i32)pUnit->pCell->nY - (i32)pEnemyCell->nY;
        i32 nDz = (i32)pUnit->pCell->nZ - (i32)pEnemyCell->nZ;
        i32 nAfterDistance;
        if (nDx < 0) { nDx = -nDx; }
        if (nDy < 0) { nDy = -nDy; }
        if (nDz < 0) { nDz = -nDz; }
        nAfterDistance = (nDx > nDy) ? nDx : nDy;
        if (nDz > nAfterDistance) { nAfterDistance = nDz; }
        if (nAfterDistance >= nStartDistance) {
            printf("pocketufo: campaign approach: FAIL (real path did not reduce enemy distance)\n");
            fflush(stdout);
            return false;
        }
        if (pOutsidePlane != NULL) {
            printf("pocketufo: campaign outside-plane approach move: actor 0x%llX target 0x%llX "
                   "targetCell %d/%d/%d goal %d/%d/%d start %d/%d/%d end %d/%d/%d "
                   "distance %d->%d pathCost %d tu %u->%u debit %u targetHp %u->%u "
                   "queue %d/%u outside 1\n",
                   (unsigned long long)nUnitKey, (unsigned long long)nEnemyKey,
                   nEnemyX, nEnemyY, nEnemyZ, nTargetX, nTargetY, nTargetZ,
                   nStartX, nStartY, nStartZ,
                   (int)pUnit->pCell->nX, (int)pUnit->pCell->nY, (int)pUnit->pCell->nZ,
                   (int)nStartDistance, (int)nAfterDistance, (int)nBestCost,
                   (unsigned)uTu0, (unsigned)pUnit->uTu,
                   (unsigned)(uTu0 - pUnit->uTu), (unsigned)uEnemyHp0,
                   (unsigned)pEnemy->uHp, (int)pQueue->nQueueLen,
                   (unsigned)pQueue->bQueueActive);
        } else {
            printf("pocketufo: campaign approach: enemy %d from %d/%d/%d to %d/%d/%d, distance %d->%d, "
                   "pathCost %d TU %u->%u targetHP %u\n",
                   (int)nEnemyIndex, nStartX, nStartY, nStartZ,
                   (int)pUnit->pCell->nX, (int)pUnit->pCell->nY, (int)pUnit->pCell->nZ,
                   (int)nStartDistance, (int)nAfterDistance, (int)nBestCost,
                   (unsigned)uTu0, (unsigned)pUnit->uTu, (unsigned)pEnemy->uHp);
        }
        fflush(stdout);
    }
    return true;
}

typedef struct AppCampaignFireWeapon {
    uintptr_t nActorKey;
    uintptr_t nWeaponKey;
    i32 nRosterSlot;
    i32 nPoolIndex;
    i32 nHandWidget;
    u16 nItemIndex;
    u8 nAmmo;
} AppCampaignFireWeapon;

typedef enum AppCampaignFireFindResult {
    APP_CAMPAIGN_FIRE_FIND_INVALID = -1,
    APP_CAMPAIGN_FIRE_FIND_NONE = 0,
    APP_CAMPAIGN_FIRE_FIND_FOUND = 1
} AppCampaignFireFindResult;

typedef enum AppCampaignSnapQuoteResult {
    APP_CAMPAIGN_SNAP_QUOTE_FAILED = -1,
    APP_CAMPAIGN_SNAP_QUOTE_UNAVAILABLE = 0,
    APP_CAMPAIGN_SNAP_QUOTE_READY = 1
} AppCampaignSnapQuoteResult;

static i32 App_ScriptCampaignFirePoolIndex(uintptr_t nItemKey)
{
    const i32 nCapacity = (i32)(sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0]));
    i32 nFound = -1;

    if (nItemKey == 0 || g_world.nTactItemCount < 0
        || g_world.nTactItemCount > nCapacity) {
        return -1;
    }
    for (i32 i = 0; i < g_world.nTactItemCount; ++i) {
        if ((uintptr_t)(void *)g_world.apTactItems[i] != nItemKey) { continue; }
        if (nFound >= 0) { return -2; }
        nFound = i;
    }
    return nFound;
}

static i32 App_ScriptCampaignFireRosterSlot(GameMap *pMap, uintptr_t nActorKey)
{
    i32 nFound = -1;

    if (pMap == NULL || nActorKey == 0 || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX) {
        return -1;
    }
    for (i32 i = 0; i < (i32)pMap->nXcomCount; ++i) {
        if ((uintptr_t)(void *)pMap->apXcomUnits[i] != nActorKey) { continue; }
        if (nFound >= 0) { return -1; }
        nFound = i;
    }
    return nFound;
}

static i32 App_ScriptCampaignFireRosterMatches(GameMap *pMap, uintptr_t nActorKey,
                                              i32 *pnRosterSlot)
{
    i32 nMatches = 0;

    if (pnRosterSlot != NULL) { *pnRosterSlot = -1; }
    if (pMap == NULL || nActorKey == 0 || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX) {
        return -1;
    }
    for (i32 i = 0; i < (i32)pMap->nXcomCount; ++i) {
        if ((uintptr_t)(void *)pMap->apXcomUnits[i] != nActorKey) { continue; }
        ++nMatches;
        if (nMatches == 1 && pnRosterSlot != NULL) { *pnRosterSlot = i; }
    }
    return nMatches;
}

static const char *App_ScriptCampaignFireOwnerFailure(uintptr_t nMapKey,
                                                      uintptr_t nQueueKey)
{
    if (nMapKey == 0) { return "expected_map_key_missing"; }
    if (nQueueKey == 0) { return "expected_queue_key_missing"; }
    if (!World_bTactPointerLive(&g_world)) { return "tactical_owner_not_live"; }
    if (g_world.pTactMap == NULL) { return "current_map_pointer_missing"; }
    if ((uintptr_t)(void *)g_world.pTactMap != nMapKey) { return "map_pointer_changed"; }
    if (g_world.pTactState == NULL) { return "current_queue_pointer_missing"; }
    if ((uintptr_t)(void *)g_world.pTactState != nQueueKey) { return "queue_pointer_changed"; }
    return "owner_key_validation_failed";
}

static AppCampaignFireFindResult App_ScriptCampaignFireFindLoadedRifle(
    GameMap *pMap, Soldier *pUnit, AppCampaignFireWeapon *pOut)
{
    const i32 nCapacity = (i32)(sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0]));
    const uintptr_t nActorKey = (uintptr_t)(void *)pUnit;
    const i32 nRosterSlot = App_ScriptCampaignFireRosterSlot(pMap, nActorKey);

    if (pMap == NULL || pUnit == NULL || pOut == NULL
        || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX || nRosterSlot < 0
        || App_ScriptCampaignFindActiveXcom(pMap, nActorKey) != pUnit
        || g_assets.apObjects == NULL || g_assets.nObjectCount <= 4
        || g_assets.apObjects[4] == NULL
        || g_world.nTactItemCount < 0 || g_world.nTactItemCount > nCapacity) {
        return APP_CAMPAIGN_FIRE_FIND_INVALID;
    }
    *pOut = (AppCampaignFireWeapon){ 0 };
    for (i32 nHandWidget = 17; nHandWidget <= 18; ++nHandWidget) {
        TactItem *pLink = (nHandWidget == 17) ? pUnit->pHandL : pUnit->pHandR;
        uintptr_t nWeaponKey;
        i32 nPool;
        TactItem *pWeapon;

        if (pLink == NULL || pLink == &g_itemSlotEmpty) { continue; }
        nWeaponKey = (uintptr_t)(void *)pLink;
        nPool = App_ScriptCampaignFirePoolIndex(nWeaponKey);
        if (nPool == -2) { return APP_CAMPAIGN_FIRE_FIND_INVALID; }
        if (nPool < 0) { return APP_CAMPAIGN_FIRE_FIND_INVALID; }
        pWeapon = g_world.apTactItems[nPool];
        if (pWeapon == NULL || pWeapon != pLink) { return APP_CAMPAIGN_FIRE_FIND_INVALID; }
        if (pWeapon->pObj != g_assets.apObjects[4] || pWeapon->nAmmo == 0) { continue; }
        *pOut = (AppCampaignFireWeapon){
            nActorKey, nWeaponKey, nRosterSlot, nPool, nHandWidget,
            pWeapon->nIndex, pWeapon->nAmmo
        };
        return APP_CAMPAIGN_FIRE_FIND_FOUND;
    }
    return APP_CAMPAIGN_FIRE_FIND_NONE;
}

static bool App_ScriptCampaignFireRefresh(uintptr_t nMapKey, uintptr_t nQueueKey,
                                          AppCampaignFireWeapon *pRef,
                                          bool bRequireSameAmmo,
                                          GameMap **ppMap, TactState **ppQueue,
                                          Soldier **ppUnit, TactItem **ppWeapon);
static AppCampaignSnapQuoteResult App_ScriptCampaignQuoteSnap(
    uintptr_t nMapKey, uintptr_t nQueueKey,
    AppCampaignFireWeapon *pRef, i32 *pnSnapCost);

static bool App_ScriptCampaignSelectLiveXcom(uintptr_t nMapKey, uintptr_t nQueueKey,
                                             uintptr_t nPreferredActorKey,
                                             i32 nRosterCount,
                                             GameMap **ppMap, TactState **ppQueue)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    uintptr_t nCurrentActorKey;
    const bool bHavePreferred = nPreferredActorKey != 0
        && App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        && App_ScriptCampaignFindActiveXcom(pMap, nPreferredActorKey) != NULL;

    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
        || g_world.bMapPlayerInput == 0 || pMap->nXcomCount != nRosterCount
        || pMap->tact.bBusy != 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
        return false;
    }
    nCurrentActorKey = (uintptr_t)(void *)pMap->pSelUnit;
    if (bHavePreferred && nCurrentActorKey == nPreferredActorKey) {
        if (ppMap != NULL) { *ppMap = pMap; }
        if (ppQueue != NULL) { *ppQueue = pQueue; }
        printf("pocketufo: campaign outside-plane resume: kept live actor 0x%llX by widget state\n",
               (unsigned long long)nPreferredActorKey);
        fflush(stdout);
        return true;
    }
    for (i32 i = 0; i < ((bHavePreferred) ? nRosterCount : 1); ++i) {
        const uintptr_t nPreviousActorKey = (uintptr_t)(void *)pMap->pSelUnit;
        if (!App_ScriptCampaignClickBank(0)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
            || g_world.bMapPlayerInput == 0 || pMap->nXcomCount != nRosterCount
            || pMap->tact.bBusy != 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
            || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
            || (uintptr_t)(void *)pMap->pSelUnit == 0
            || (uintptr_t)(void *)pMap->pSelUnit == nPreviousActorKey) {
            return false;
        }
        if (bHavePreferred && (uintptr_t)(void *)pMap->pSelUnit != nPreferredActorKey) {
            continue;
        }
        printf("pocketufo: campaign outside-plane move: selected by live widget 0 actor "
               "0x%llX -> 0x%llX\n",
               (unsigned long long)nPreviousActorKey,
               (unsigned long long)(uintptr_t)(void *)pMap->pSelUnit);
        fflush(stdout);
        if (ppMap != NULL) { *ppMap = pMap; }
        if (ppQueue != NULL) { *ppQueue = pQueue; }
        return true;
    }
    printf("pocketufo: campaign outside-plane resume: live actor 0x%llX was lost; "
           "widget 0 could not restore it\n",
           (unsigned long long)nPreferredActorKey);
    fflush(stdout);
    return false;
}

static bool App_ScriptCampaignMoveOutsideRifle(const AppCampaignPlaneRect *pPlane,
                                               uintptr_t nPreferredActorKey,
                                               AppCampaignFireWeapon *pOutFire,
                                               i32 *pnPathCost,
                                               i32 *pnSnapCost)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    uintptr_t aTriedActorKeys[PUFO_MAP_ROSTER_MAX] = { 0 };
    i32 nRosterCount, nTriedActors = 0;

    if (pOutFire != NULL) { *pOutFire = (AppCampaignFireWeapon){ 0 }; }
    if (pnPathCost != NULL) { *pnPathCost = 0; }
    if (pnSnapCost != NULL) { *pnSnapCost = 0; }
    if (pPlane == NULL || g_world.nUiScreen != 0u
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
        || pMap->tact.bBusy != 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
        || pMap->nMissionType != 1u || pMap->nXcomCount == 0
        || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX) {
        printf("pocketufo: campaign outside-plane move: FAIL live idle state is not ready\n");
        fflush(stdout);
        return false;
    }
    nRosterCount = (i32)pMap->nXcomCount;
    if (!App_ScriptCampaignSelectLiveXcom(nMapKey, nQueueKey, nPreferredActorKey,
                                          nRosterCount, &pMap, &pQueue)) {
        printf("pocketufo: campaign outside-plane move: FAIL widget 0 did not select a live owner\n");
        fflush(stdout);
        return false;
    }

    for (i32 nTry = 0; nTry < nRosterCount; ++nTry) {
        uintptr_t nActorKey;
        i32 nRosterSlot;
        Soldier *pCandidate;
        AppCampaignFireWeapon sCandidate = { 0 };
        AppCampaignFireFindResult eFind;
        bool bTryNext = false;

        if (g_world.nUiScreen != 0u
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
            || pMap->nXcomCount != nRosterCount || pMap->tact.bBusy != 0
            || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
            || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
            printf("pocketufo: campaign outside-plane move: FAIL live state changed during roster scan\n");
            fflush(stdout);
            return false;
        }
        nActorKey = (uintptr_t)(void *)pMap->pSelUnit;
        nRosterSlot = App_ScriptCampaignFireRosterSlot(pMap, nActorKey);
        if (nActorKey == 0 || nRosterSlot < 0) {
            printf("pocketufo: campaign outside-plane move: FAIL selected actor is not a unique roster entry\n");
            fflush(stdout);
            return false;
        }
        for (i32 i = 0; i < nTriedActors; ++i) {
            if (aTriedActorKeys[i] == nActorKey) {
                printf("pocketufo: campaign outside-plane move: FAIL no loaded Rifle 4 owner "
                       "has an affordable live path outside the footprint (roster cycle)\n");
                fflush(stdout);
                return false;
            }
        }
        aTriedActorKeys[nTriedActors++] = nActorKey;
        pCandidate = App_ScriptCampaignFindActiveXcom(pMap, nActorKey);
        if (pCandidate == NULL || pCandidate->pCell == NULL) {
            printf("pocketufo: campaign outside-plane move skip: slot %d actor 0x%llX reason inactive\n",
                   (int)nRosterSlot, (unsigned long long)nActorKey);
            fflush(stdout);
            bTryNext = true;
        } else if (!App_ScriptCampaignCellInsidePlane(pPlane,
                                                       (i32)pCandidate->pCell->nX,
                                                       (i32)pCandidate->pCell->nY)) {
            printf("pocketufo: campaign outside-plane move skip: slot %d actor 0x%llX "
                   "reason already_outside cell %d/%d/%d\n",
                   (int)nRosterSlot, (unsigned long long)nActorKey,
                   (int)pCandidate->pCell->nX, (int)pCandidate->pCell->nY,
                   (int)pCandidate->pCell->nZ);
            fflush(stdout);
            bTryNext = true;
        } else {
            eFind = App_ScriptCampaignFireFindLoadedRifle(pMap, pCandidate, &sCandidate);
            if (eFind == APP_CAMPAIGN_FIRE_FIND_INVALID) {
                printf("pocketufo: campaign outside-plane move: FAIL invalid live Rifle 4 link at slot %d\n",
                       (int)nRosterSlot);
                fflush(stdout);
                return false;
            }
            if (eFind == APP_CAMPAIGN_FIRE_FIND_NONE) {
                printf("pocketufo: campaign outside-plane move skip: slot %d actor 0x%llX "
                       "reason no_loaded_rifle tu %u\n",
                       (int)nRosterSlot, (unsigned long long)nActorKey,
                       (unsigned)pCandidate->uTu);
                fflush(stdout);
                bTryNext = true;
            } else {
                i32 nSnapCost = 0;
                i32 nPathCost = 0;
                const u16 uTuBefore = pCandidate->uTu;
                const u16 uHpBefore = pCandidate->uHp;
                const i32 nXBefore = (i32)pCandidate->pCell->nX;
                const i32 nYBefore = (i32)pCandidate->pCell->nY;
                const i32 nZBefore = (i32)pCandidate->pCell->nZ;
                const AppCampaignSnapQuoteResult eQuote = App_ScriptCampaignQuoteSnap(
                    nMapKey, nQueueKey, &sCandidate, &nSnapCost);

                if (eQuote == APP_CAMPAIGN_SNAP_QUOTE_FAILED) {
                    printf("pocketufo: campaign outside-plane move: FAIL live Snap quote input changed owner state\n");
                    fflush(stdout);
                    return false;
                } else if (eQuote == APP_CAMPAIGN_SNAP_QUOTE_UNAVAILABLE) {
                    printf("pocketufo: campaign outside-plane move skip: slot %d actor 0x%llX "
                           "reason no live affordable Snap quote\n",
                           (int)nRosterSlot, (unsigned long long)nActorKey);
                    fflush(stdout);
                    bTryNext = true;
                } else if (pMap->pSelUnit != pCandidate
                           || pCandidate->uTu != uTuBefore
                           || pCandidate->uHp != uHpBefore) {
                    printf("pocketufo: campaign outside-plane move: FAIL actor, HP, or TU changed during Snap quote\n");
                    fflush(stdout);
                    return false;
                } else if (nSnapCost >= (i32)uTuBefore) {
                    printf("pocketufo: campaign outside-plane move skip: slot %d actor 0x%llX "
                           "reason Snap cost leaves no TU for a path cost %d tu %u\n",
                           (int)nRosterSlot, (unsigned long long)nActorKey,
                           (int)nSnapCost, (unsigned)uTuBefore);
                    fflush(stdout);
                    bTryNext = true;
                } else if (App_ScriptCampaignTryMoveUnit(pCandidate, false, NULL,
                                                          pPlane, nSnapCost, &nPathCost)) {
                    if (!App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, &sCandidate, true,
                                                       &pMap, &pQueue, &pCandidate, NULL)
                        || pMap->pSelUnit != pCandidate || pCandidate->pCell == NULL
                        || App_ScriptCampaignCellInsidePlane(pPlane,
                                                              (i32)pCandidate->pCell->nX,
                                                              (i32)pCandidate->pCell->nY)
                        || pCandidate->uTu >= uTuBefore
                        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
                        || pMap->tact.bBusy != 0) {
                        printf("pocketufo: campaign outside-plane move: FAIL owner, rifle, debit, or queue changed after path\n");
                        fflush(stdout);
                        return false;
                    }
                    printf("pocketufo: campaign outside-plane owner: actor 0x%llX roster %d "
                           "weaponKey 0x%llX itemIndex %u ammo %u start %d/%d/%d "
                           "end %d/%d/%d pathCost %d snapCost %d tu %u->%u hp %u->%u\n",
                           (unsigned long long)sCandidate.nActorKey,
                           (int)sCandidate.nRosterSlot,
                           (unsigned long long)sCandidate.nWeaponKey,
                           (unsigned)sCandidate.nItemIndex, (unsigned)sCandidate.nAmmo,
                           (int)nXBefore, (int)nYBefore, (int)nZBefore,
                           (int)pCandidate->pCell->nX, (int)pCandidate->pCell->nY,
                           (int)pCandidate->pCell->nZ, (int)nPathCost, (int)nSnapCost,
                           (unsigned)uTuBefore, (unsigned)pCandidate->uTu,
                           (unsigned)uHpBefore, (unsigned)pCandidate->uHp);
                    fflush(stdout);
                    if (pnPathCost != NULL) { *pnPathCost = nPathCost; }
                    if (pnSnapCost != NULL) { *pnSnapCost = nSnapCost; }
                    if (pOutFire != NULL) { *pOutFire = sCandidate; }
                    return true;
                } else {
                    GameMap *pCheckMap = NULL;
                    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey,
                                                            &pCheckMap, NULL)
                        || pCheckMap->pSelUnit != pCandidate
                        || App_ScriptCampaignFindActiveXcom(pCheckMap, nActorKey) != pCandidate
                        || pCandidate->pCell == NULL
                        || pCandidate->pCell->nX != nXBefore
                        || pCandidate->pCell->nY != nYBefore
                        || pCandidate->pCell->nZ != nZBefore
                        || pCandidate->uTu != uTuBefore) {
                        printf("pocketufo: campaign outside-plane move: FAIL actor changed during failed path attempt\n");
                        fflush(stdout);
                        return false;
                    }
                    bTryNext = true;
                }
            }
        }

        if (!bTryNext || nTriedActors >= nRosterCount) { break; }
        {
            const uintptr_t nPreviousActorKey = nActorKey;
            if (g_world.nUiScreen != 0u
                || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
                || pMap->nXcomCount != nRosterCount || pMap->tact.bBusy != 0
                || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
                || !App_ScriptCampaignClickBank(0)
                || g_world.nUiScreen != 0u
                || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
                || pMap->nXcomCount != nRosterCount || pMap->tact.bBusy != 0
                || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
                || (uintptr_t)(void *)pMap->pSelUnit == nPreviousActorKey) {
                printf("pocketufo: campaign outside-plane move: FAIL live widget 0 could not cycle a rifle owner\n");
                fflush(stdout);
                return false;
            }
            printf("pocketufo: campaign outside-plane move: selected by live widget 0 actor 0x%llX -> 0x%llX\n",
                   (unsigned long long)nPreviousActorKey,
                   (unsigned long long)(uintptr_t)(void *)pMap->pSelUnit);
            fflush(stdout);
        }
    }
    printf("pocketufo: campaign outside-plane move: FAIL no loaded Rifle 4 owner has an affordable live path outside the footprint\n");
    fflush(stdout);
    return false;
}

typedef enum AppCampaignOutsidePrepResult {
    APP_CAMPAIGN_OUTSIDE_PREP_FAIL = -1,
    APP_CAMPAIGN_OUTSIDE_PREP_NONE = 0,
    APP_CAMPAIGN_OUTSIDE_PREP_MOVED = 1
} AppCampaignOutsidePrepResult;

/* Unit_PathToCell @ 0x2c2a8 uses this private path.c threshold when it restores a
 * horizontal step's layer from bFlags29 bit 3. Keep this walk byte-for-byte aligned
 * with that backwalk; the resulting endpoint is then sent through the real map input. */
#define APP_CAMPAIGN_PATH_CEIL_DROP_LIMIT (-15)

static bool App_ScriptCampaignRebuildGoalPath(Soldier *pUnit,
                                               const AppCampaignCellPick *pGoal,
                                               MapCell **apGoalPath,
                                               i32 *pnPathSteps)
{
    MapCell *pGoalCell;
    i32 nWalkX, nWalkY, nCurLayer;
    i32 nPathSteps = 0;
    bool bSearchFlag;

    if (pUnit == NULL || pUnit->pCell == NULL || pGoal == NULL || apGoalPath == NULL
        || pnPathSteps == NULL || g_world.sTactPath.pfnLookup == NULL
        || g_world.sTactPath.pfnProbe == NULL) {
        return false;
    }

    bSearchFlag = pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT
        && g_aUnitTypeFlag0[pUnit->nUnitType] != 0;
    pGoalCell = App_ScriptCampaignCellAt(pGoal->nX, pGoal->nY, pGoal->nZ);
    if (pGoalCell == NULL || !Path_ComputeReachable(
            &g_world.sTactPathScratch,
            pUnit->pCell->nX, pUnit->pCell->nY, pUnit->pCell->nZ, pUnit->bFacing,
            pGoal->nX, pGoal->nY, pGoal->nZ, bSearchFlag,
            g_world.sTactPath.pfnLookup, g_world.sTactPath.pfnProbe,
            g_world.sTactPath.pProbeUser)
        || pGoalCell->nPathCost != pGoal->nPathCost) {
        return false;
    }

    nWalkX = pGoal->nX;
    nWalkY = pGoal->nY;
    nCurLayer = pGoal->nZ;
    apGoalPath[0] = pGoalCell;
    for (;;) {
        const i32 nOldX = nWalkX;
        const i32 nOldY = nWalkY;
        MapCell *pCell = g_world.sTactPath.pfnLookup(
            nWalkX, nWalkY, nCurLayer, g_world.sTactPath.pLookupUser);
        i32 nDir;
        i32 nDelta;

        if (pCell == NULL) { return false; }
        nDir = pCell->bPathDir;
        if (nDir >= PUFO_PATH_DIR_COUNT) { return false; }
        nDelta = (i32)g_aStepDir[nDir].dz;
        if (nDelta != 0) {
            nCurLayer -= nDelta;
        } else {
            i32 nOpp = nDir + 4;
            if (nOpp > 7) { nOpp -= 8; }
            nWalkX += (i32)g_aStepDir[nOpp].dx;
            nWalkY += (i32)g_aStepDir[nOpp].dy;

            if ((pCell->bFlags29 & 8u) != 0) {
                bool bMoved = false;
                if (nCurLayer < PUFO_MAP_LAYERS - 1) {
                    MapCell *pAboveOld = g_world.sTactPath.pfnLookup(
                        nOldX, nOldY, nCurLayer + 1, g_world.sTactPath.pLookupUser);
                    MapCell *pAboveNew = g_world.sTactPath.pfnLookup(
                        nWalkX, nWalkY, nCurLayer + 1, g_world.sTactPath.pLookupUser);
                    if (pAboveOld != NULL && pAboveOld->pFloorSpr == NULL
                        && pAboveNew != NULL && pAboveNew->nPathCost != 0) {
                        ++nCurLayer;
                        bMoved = true;
                    }
                }
                if (!bMoved && nCurLayer > 0) {
                    MapCell *pBelow = g_world.sTactPath.pfnLookup(
                        nWalkX, nWalkY, nCurLayer - 1, g_world.sTactPath.pLookupUser);
                    if (pBelow != NULL && pBelow->pCeilSpr != NULL
                        && pBelow->pCeilSpr->iCeilParam < APP_CAMPAIGN_PATH_CEIL_DROP_LIMIT
                        && pBelow->nPathCost != 0) {
                        --nCurLayer;
                    }
                }
            }
        }

        ++nPathSteps;
        if (nPathSteps >= PUFO_PATH_MAX_CELLS) { return false; }
        pCell = g_world.sTactPath.pfnLookup(
            nWalkX, nWalkY, nCurLayer, g_world.sTactPath.pLookupUser);
        if (pCell == NULL) { return false; }
        apGoalPath[nPathSteps] = pCell;
        if (nPathSteps > PUFO_PATH_MAX_CELLS - 2) { return false; }
        if (pCell->nPathCost == 1) { break; }
    }

    *pnPathSteps = nPathSteps;
    return apGoalPath[nPathSteps] == pUnit->pCell;
}

static AppCampaignOutsidePrepResult App_ScriptCampaignMoveOutsidePrefix(
    const AppCampaignPlaneRect *pPlane, uintptr_t nPreferredActorKey,
    i32 nTurn, i32 nActions,
    AppCampaignOutsidePrepMove *pOutMove)
{
    static const i8 aViewOffset[9][2] = {
        { 0, 0 }, { -8, 0 }, { 8, 0 }, { 0, -8 }, { 0, 8 },
        { -8, -8 }, { 8, -8 }, { -8, 8 }, { 8, 8 }
    };
    uintptr_t aTriedActorKeys[PUFO_MAP_ROSTER_MAX] = { 0 };
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    i32 nRosterCount, nTriedActors = 0;

    if (pOutMove != NULL) { memset(pOutMove, 0, sizeof(*pOutMove)); }
    if (pPlane == NULL || nTurn >= 12 || nActions >= 158) {
        return APP_CAMPAIGN_OUTSIDE_PREP_NONE;
    }
    if (g_world.nUiScreen != 0u
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
        || pMap->tact.bBusy != 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
        || pMap->nMissionType != 1u || pMap->nXcomCount == 0
        || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX) {
        printf("pocketufo: campaign outside-plane prep: FAIL live idle state is not ready\n");
        fflush(stdout);
        return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
    }
    nRosterCount = (i32)pMap->nXcomCount;
    if (!App_ScriptCampaignSelectLiveXcom(nMapKey, nQueueKey, nPreferredActorKey,
                                          nRosterCount, &pMap, &pQueue)) {
        printf("pocketufo: campaign outside-plane prep: FAIL widget 0 did not select a live owner\n");
        fflush(stdout);
        return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
    }

    for (i32 nTry = 0; nTry < nRosterCount; ++nTry) {
        uintptr_t nActorKey;
        i32 nRosterSlot;
        Soldier *pCandidate;
        AppCampaignFireWeapon sFire = { 0 };
        AppCampaignFireFindResult eFind;
        bool bTryNext = false;

        if (g_world.nUiScreen != 0u
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
            || pMap->nXcomCount != nRosterCount || pMap->tact.bBusy != 0
            || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
            || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
            printf("pocketufo: campaign outside-plane prep: FAIL live state changed during roster scan\n");
            fflush(stdout);
            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
        }
        nActorKey = (uintptr_t)(void *)pMap->pSelUnit;
        nRosterSlot = App_ScriptCampaignFireRosterSlot(pMap, nActorKey);
        if (nActorKey == 0 || nRosterSlot < 0) {
            printf("pocketufo: campaign outside-plane prep: FAIL selected actor is not a roster entry\n");
            fflush(stdout);
            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
        }
        for (i32 i = 0; i < nTriedActors; ++i) {
            if (aTriedActorKeys[i] == nActorKey) {
                return APP_CAMPAIGN_OUTSIDE_PREP_NONE;
            }
        }
        aTriedActorKeys[nTriedActors++] = nActorKey;
        pCandidate = App_ScriptCampaignFindActiveXcom(pMap, nActorKey);
        if (pCandidate == NULL || pCandidate->pCell == NULL) {
            printf("pocketufo: campaign outside-plane prep skip: slot %d actor 0x%llX reason inactive\n",
                   (int)nRosterSlot, (unsigned long long)nActorKey);
            fflush(stdout);
            bTryNext = true;
        } else if (!App_ScriptCampaignCellInsidePlane(
                       pPlane, (i32)pCandidate->pCell->nX, (i32)pCandidate->pCell->nY)) {
            printf("pocketufo: campaign outside-plane prep skip: slot %d actor 0x%llX "
                   "reason already_outside cell %d/%d/%d\n",
                   (int)nRosterSlot, (unsigned long long)nActorKey,
                   (int)pCandidate->pCell->nX, (int)pCandidate->pCell->nY,
                   (int)pCandidate->pCell->nZ);
            fflush(stdout);
            bTryNext = true;
        } else {
            eFind = App_ScriptCampaignFireFindLoadedRifle(pMap, pCandidate, &sFire);
            if (eFind == APP_CAMPAIGN_FIRE_FIND_INVALID) {
                printf("pocketufo: campaign outside-plane prep: FAIL invalid live Rifle 4 link at slot %d\n",
                       (int)nRosterSlot);
                fflush(stdout);
                return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
            }
            if (eFind == APP_CAMPAIGN_FIRE_FIND_NONE) {
                printf("pocketufo: campaign outside-plane prep skip: slot %d actor 0x%llX "
                       "reason no_loaded_rifle\n",
                       (int)nRosterSlot, (unsigned long long)nActorKey);
                fflush(stdout);
                bTryNext = true;
            } else {
                AppCampaignCellPick aDiscard[1] = { { 0 } };
                AppCampaignCellPick sGoal = { 0 };
                MapCell *aGoalPath[PUFO_PATH_MAX_CELLS] = { 0 };
                MapCell *pStartCell = pCandidate->pCell;
                const i32 nStartX = (i32)pStartCell->nX;
                const i32 nStartY = (i32)pStartCell->nY;
                const i32 nStartZ = (i32)pStartCell->nZ;
                const u16 uTuBefore = pCandidate->uTu;
                const u16 uHpBefore = pCandidate->uHp;
                const u8 bFlagsBefore = pCandidate->bFlags;
                i32 nGoalPathSteps = 0;
                i32 nPrefixIndex = -1;

                for (i32 nView = 0; nView < (i32)(sizeof(aViewOffset) / sizeof(aViewOffset[0]));
                     ++nView) {
                    const i32 nViewX = nStartX + (i32)aViewOffset[nView][0];
                    const i32 nViewY = nStartY + (i32)aViewOffset[nView][1];

                    if (!App_ScriptCampaignPanToCell(nViewX, nViewY)
                        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                        || (pCandidate = App_ScriptCampaignFindActiveXcom(pMap, nActorKey)) == NULL
                        || pMap->pSelUnit != pCandidate || pCandidate->pCell != pStartCell
                        || pCandidate->uTu != uTuBefore || pCandidate->uHp != uHpBefore) {
                        printf("pocketufo: campaign outside-plane prep: FAIL owner changed during path-goal scan\n");
                        fflush(stdout);
                        return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                    }
                    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
                        (void)App_ScriptCampaignCollectNearbyCells(
                            pCandidate, aDiscard, (i32)(sizeof(aDiscard) / sizeof(aDiscard[0])),
                            20, nLayer, true, pPlane, 0, &sGoal);
                        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey,
                                                               &pMap, &pQueue)
                            || (pCandidate = App_ScriptCampaignFindActiveXcom(
                                    pMap, nActorKey)) == NULL
                            || pMap->pSelUnit != pCandidate || pCandidate->pCell != pStartCell
                            || pCandidate->uTu != uTuBefore || pCandidate->uHp != uHpBefore) {
                            printf("pocketufo: campaign outside-plane prep: FAIL owner changed during layer scan\n");
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                    }
                }
                if (sGoal.nPathCost <= 0) {
                    printf("pocketufo: campaign outside-plane prep skip: slot %d actor 0x%llX "
                           "reason no reachable outside goal\n",
                           (int)nRosterSlot, (unsigned long long)nActorKey);
                    fflush(stdout);
                    bTryNext = true;
                } else if (!App_ScriptCampaignRebuildGoalPath(pCandidate, &sGoal,
                                                               aGoalPath, &nGoalPathSteps)) {
                    printf("pocketufo: campaign outside-plane prep: FAIL stored path parent chain "
                           "did not rebuild goal %d/%d/%d cost %d\n",
                           (int)sGoal.nX, (int)sGoal.nY, (int)sGoal.nZ,
                           (int)sGoal.nPathCost);
                    fflush(stdout);
                    return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                } else {
                    printf("pocketufo: campaign outside-plane prep route: seq %d actor 0x%llX "
                           "roster %d start %d/%d/%d goal %d/%d/%d goalCost %d pathSteps %d "
                           "tu %u snapReserve 0\n",
                           (int)(nActions + 1), (unsigned long long)nActorKey,
                           (int)nRosterSlot, nStartX, nStartY, nStartZ,
                           (int)sGoal.nX, (int)sGoal.nY, (int)sGoal.nZ,
                           (int)sGoal.nPathCost, (int)nGoalPathSteps,
                           (unsigned)uTuBefore);
                    fflush(stdout);
                    for (i32 i = 1; i < nGoalPathSteps; ++i) {
                        MapCell *pPrefix = aGoalPath[i];
                        const i32 nSafeDebitBound = (i32)pPrefix->nPathCost - 1
                            + (((bFlagsBefore & PUFO_UNITFLAG_STAND_UP) != 0) ? 2 : 0);
                        bool bPrefixStaysInside = true;

                        for (i32 j = i; j <= nGoalPathSteps; ++j) {
                            MapCell *pStepCell = aGoalPath[j];
                            if (pStepCell == NULL || !App_ScriptCampaignCellInsidePlane(
                                    pPlane, (i32)pStepCell->nX, (i32)pStepCell->nY)) {
                                bPrefixStaysInside = false;
                                break;
                            }
                        }

                        if (bPrefixStaysInside && pPrefix != NULL && pPrefix != pStartCell
                            && App_ScriptCampaignCellInsidePlane(
                                pPlane, (i32)pPrefix->nX, (i32)pPrefix->nY)
                            && pPrefix->nPathCost > 1
                            && pPrefix->nPathCost <= (i32)uTuBefore
                            && nSafeDebitBound > 0
                            && nSafeDebitBound <= (i32)uTuBefore) {
                            nPrefixIndex = i;
                            break;
                        }
                    }
                    if (nPrefixIndex < 0) {
                        printf("pocketufo: campaign outside-plane prep skip: slot %d actor 0x%llX "
                               "goal %d/%d/%d cost %d has no affordable inside-plane prefix "
                               "tu %u\n",
                               (int)nRosterSlot, (unsigned long long)nActorKey,
                               (int)sGoal.nX, (int)sGoal.nY, (int)sGoal.nZ,
                               (int)sGoal.nPathCost, (unsigned)uTuBefore);
                        fflush(stdout);
                        bTryNext = true;
                    } else {
                        MapCell *pPrefix = aGoalPath[nPrefixIndex];
                        const i32 nPrefixPathCost = (i32)pPrefix->nPathCost;
                        const i32 nPrefixPathSteps = nGoalPathSteps - nPrefixIndex;
                        i32 nScreenX = -1, nScreenY = -1;
                        i32 nTuAfter;
                        i32 nActualDebit;
                        i32 nActionSequence = nActions + 1;
                        u8 bFallGate, bFireGate, bThrowGate, bQueueGate;

                        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey,
                                                               &pMap, &pQueue)
                            || (pCandidate = App_ScriptCampaignFindActiveXcom(
                                    pMap, nActorKey)) == NULL
                            || pMap->pSelUnit != pCandidate || pCandidate->pCell != pStartCell
                            || pCandidate->uTu != uTuBefore || pCandidate->uHp != uHpBefore
                            || App_ScriptCampaignCellAt((i32)pPrefix->nX,
                                                        (i32)pPrefix->nY,
                                                        (i32)pPrefix->nZ) != pPrefix
                            || pPrefix->pFloorSpr == NULL || pPrefix->pUnit != NULL
                            || MapCell_TestFlag(pPrefix, CELLFLAG_IS_BLANK)
                            || !App_ScriptCampaignFindCellPixel(
                                (i32)pPrefix->nX, (i32)pPrefix->nY, (i32)pPrefix->nZ,
                                &nScreenX, &nScreenY)) {
                            printf("pocketufo: campaign outside-plane prep: FAIL could not issue "
                                   "the projected prefix input\n");
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey,
                                                               &pMap, &pQueue)
                            || (pCandidate = App_ScriptCampaignFindActiveXcom(
                                    pMap, nActorKey)) == NULL
                            || pMap->pSelUnit != pCandidate || pCandidate->pCell != pStartCell
                            || pCandidate->uTu != uTuBefore || pCandidate->uHp != uHpBefore) {
                            printf("pocketufo: campaign outside-plane prep: FAIL owner changed "
                                   "during prefix projection\n");
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                        printf("pocketufo: campaign outside-plane prep projection layer: seq %d "
                               "actor 0x%llX cell %d/%d/%d viewLayer %d pen %d/%d\n",
                               (int)nActionSequence, (unsigned long long)nActorKey,
                               (int)pPrefix->nX, (int)pPrefix->nY, (int)pPrefix->nZ,
                               (int)g_world.tactEnv.cursor.nFlags - 1,
                               (int)nScreenX, (int)nScreenY);
                        fflush(stdout);
                        if ((i32)g_world.tactEnv.cursor.nFlags - 1 != (i32)pPrefix->nZ
                            || g_world.tactEnv.cursor.nX != pPrefix->nX
                            || g_world.tactEnv.cursor.nY != pPrefix->nY) {
                            printf("pocketufo: campaign outside-plane prep: FAIL live projection layer or cell changed\n");
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                        bFallGate = g_world.bMapFire2;
                        bFireGate = pMap->bFireStatus1;
                        bThrowGate = pMap->bTrajHitStop;
                        bQueueGate = pQueue->bQueueActive;
                        printf("pocketufo: campaign outside-plane prep input gate: seq %d actor 0x%llX "
                               "fall %u fire %u throw %u queueActive %u queueLen %d "
                               "screen %u modal %u player %u latch %u busy %u\n",
                               (int)nActionSequence, (unsigned long long)nActorKey,
                               (unsigned)bFallGate, (unsigned)bFireGate,
                               (unsigned)bThrowGate, (unsigned)bQueueGate,
                               (int)pQueue->nQueueLen, (unsigned)g_world.nUiScreen,
                               (unsigned)g_world.sModal.bOpen,
                               (unsigned)g_world.bMapPlayerInput,
                               (unsigned)pMap->bActionLatch,
                               (unsigned)pMap->tact.bBusy);
                        fflush(stdout);
                        if (bFallGate != 0 || bFireGate != 0 || bThrowGate != 0
                            || bQueueGate != 0 || pQueue->nQueueLen != 0
                            || g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
                            || g_world.bMapPlayerInput == 0
                            || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                            || pMap->tact.bBusy != 0) {
                            printf("pocketufo: campaign outside-plane prep: FAIL Input_OnAction gate is not clear\n");
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                        if (!App_ScriptCampaignPressCellPixel(nScreenX, nScreenY)) {
                            printf("pocketufo: campaign outside-plane prep: FAIL projected prefix click failed\n");
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey,
                                                               &pMap, &pQueue)
                            || (pCandidate = App_ScriptCampaignFindActiveXcom(
                                    pMap, nActorKey)) == NULL
                            || pMap->pSelUnit != pCandidate || pCandidate->pCell != pStartCell
                            || (pQueue->nQueueLen == 0 && pQueue->bQueueActive == 0)) {
                            printf("pocketufo: campaign outside-plane prep: FAIL projected input "
                                   "did not queue a live path\n");
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                        if (g_world.sTactPath.apPathCells == NULL
                            || g_world.sTactPath.pnPathCount == NULL
                            || *g_world.sTactPath.pnPathCount != nPrefixPathSteps) {
                            (void)App_ScriptCampaignDrainQueue(1600);
                            printf("pocketufo: campaign outside-plane prep: FAIL queued path length "
                                   "does not match the chosen parent prefix\n");
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                        for (i32 i = 0; i <= nPrefixPathSteps; ++i) {
                            if (g_world.sTactPath.apPathCells[i]
                                != aGoalPath[nPrefixIndex + i]) {
                                (void)App_ScriptCampaignDrainQueue(1600);
                                printf("pocketufo: campaign outside-plane prep: FAIL queued route "
                                       "differs from stored goal prefix at step %d\n", (int)i);
                                fflush(stdout);
                                return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                            }
                        }
                        if (!App_ScriptCampaignDrainQueue(1600)
                            || !App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, &sFire, true,
                                                              &pMap, &pQueue, &pCandidate, NULL)
                            || pMap->pSelUnit != pCandidate || pCandidate->pCell != pPrefix
                            || !App_ScriptCampaignCellInsidePlane(
                                pPlane, (i32)pCandidate->pCell->nX,
                                (i32)pCandidate->pCell->nY)
                            || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
                            || pMap->tact.bBusy != 0 || pCandidate->uTu >= uTuBefore) {
                            printf("pocketufo: campaign outside-plane prep: FAIL prefix owner, "
                                   "cell, TU, or queue did not settle\n");
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                        if (g_world.bMapFire2 != 0 || pMap->bFireStatus1 != 0
                            || pMap->bTrajHitStop != 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                            || g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
                            || g_world.bMapPlayerInput == 0) {
                            printf("pocketufo: campaign outside-plane prep: FAIL post-move input gate "
                                   "fall %u fire %u throw %u screen %u modal %u player %u\n",
                                   (unsigned)g_world.bMapFire2,
                                   (unsigned)pMap->bFireStatus1,
                                   (unsigned)pMap->bTrajHitStop,
                                   (unsigned)g_world.nUiScreen,
                                   (unsigned)g_world.sModal.bOpen,
                                   (unsigned)g_world.bMapPlayerInput);
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }
                        nTuAfter = (i32)pCandidate->uTu;
                        nActualDebit = (i32)uTuBefore - nTuAfter;
                        if (nActualDebit <= 0 || nActualDebit > (i32)uTuBefore) {
                            printf("pocketufo: campaign outside-plane prep: FAIL actual TU debit %d "
                                   "is outside the live budget %u\n",
                                   (int)nActualDebit, (unsigned)uTuBefore);
                            fflush(stdout);
                            return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
                        }

                        if (pOutMove != NULL) {
                            *pOutMove = (AppCampaignOutsidePrepMove){
                                nActionSequence, nActorKey, nRosterSlot,
                                nStartX, nStartY, nStartZ,
                                sGoal.nX, sGoal.nY, sGoal.nZ, sGoal.nPathCost,
                                nGoalPathSteps,
                                (i32)pPrefix->nX, (i32)pPrefix->nY, (i32)pPrefix->nZ,
                                nPrefixPathCost, nPrefixPathSteps,
                                uTuBefore, (u16)nTuAfter
                            };
                        }
                        printf("pocketufo: campaign outside-plane prep move: seq %d actor 0x%llX "
                               "roster %d goal %d/%d/%d goalCost %d pathSteps %d "
                               "prefix %d/%d/%d prefixCost %d prefixSteps %d snapReserve 0 "
                               "start %d/%d/%d end %d/%d/%d tu %u->%u debit %d "
                               "pathMatch 1 queue %d/%d busy %d inside 1 outside 0 "
                               "postGates %u/%u/%u postScreen %u postModal %u postPlayer %u "
                               "turn %d actionsBefore %d/160 turnsRemain %d actionsRemain %d\n",
                               (int)nActionSequence, (unsigned long long)nActorKey,
                               (int)nRosterSlot,
                               (int)sGoal.nX, (int)sGoal.nY, (int)sGoal.nZ,
                               (int)sGoal.nPathCost, (int)nGoalPathSteps,
                               (int)pPrefix->nX, (int)pPrefix->nY, (int)pPrefix->nZ,
                               (int)nPrefixPathCost, (int)nPrefixPathSteps,
                               nStartX, nStartY, nStartZ,
                               (int)pCandidate->pCell->nX, (int)pCandidate->pCell->nY,
                               (int)pCandidate->pCell->nZ,
                               (unsigned)uTuBefore, (unsigned)pCandidate->uTu,
                               (int)nActualDebit,
                               (int)pQueue->nQueueLen, (int)pQueue->bQueueActive,
                               (int)pMap->tact.bBusy,
                               (unsigned)g_world.bMapFire2, (unsigned)pMap->bFireStatus1,
                               (unsigned)pMap->bTrajHitStop, (unsigned)g_world.nUiScreen,
                               (unsigned)g_world.sModal.bOpen, (unsigned)g_world.bMapPlayerInput,
                               (int)nTurn, (int)nActions,
                               12 - nTurn, 160 - nActions);
                        fflush(stdout);
                        return APP_CAMPAIGN_OUTSIDE_PREP_MOVED;
                    }
                }
            }
        }

        if (nPreferredActorKey != 0 && nActorKey == nPreferredActorKey) {
            printf("pocketufo: campaign outside-plane prep: NONE resumed owner has no "
                   "staged prefix candidate this turn\n");
            fflush(stdout);
            return APP_CAMPAIGN_OUTSIDE_PREP_NONE;
        }
        if (!bTryNext || nTriedActors >= nRosterCount) { break; }
        {
            const uintptr_t nPreviousActorKey = nActorKey;
            if (g_world.nUiScreen != 0u
                || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
                || pMap->nXcomCount != nRosterCount || pMap->tact.bBusy != 0
                || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
                || !App_ScriptCampaignClickBank(0)
                || g_world.nUiScreen != 0u
                || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
                || pMap->nXcomCount != nRosterCount || pMap->tact.bBusy != 0
                || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
                || (uintptr_t)(void *)pMap->pSelUnit == nPreviousActorKey) {
                printf("pocketufo: campaign outside-plane prep: FAIL live widget 0 could not cycle a rifle owner\n");
                fflush(stdout);
                return APP_CAMPAIGN_OUTSIDE_PREP_FAIL;
            }
        }
    }
    printf("pocketufo: campaign outside-plane prep: NONE no affordable inside-plane path prefix\n");
    fflush(stdout);
    return APP_CAMPAIGN_OUTSIDE_PREP_NONE;
}

static bool App_ScriptCampaignLogOutsidePrepTurn(const AppCampaignOutsidePrepMove *pMove,
                                                 i32 nTurn, i32 nActions,
                                                 i32 nMaxTurns)
{
    const GameMap *pMap = g_world.pTactMap;
    const TactState *pQueue = g_world.pTactState;
    const Soldier *pResume = (pMap != NULL)
        ? App_ScriptCampaignFindActiveXcom((GameMap *)pMap, pMove->nActorKey) : NULL;
    const bool bResumeActive = pResume != NULL && pResume->pCell != NULL;
    const bool bTerminal = g_world.nUiScreen == 13u || g_world.nUiScreen == 18u;
    const i32 nResumeSlot = (bResumeActive && pMap != NULL)
        ? App_ScriptCampaignFireRosterSlot((GameMap *)pMap, pMove->nActorKey) : -1;

    printf("pocketufo: campaign outside-plane prep turn: seq %d turn %d actions %d/160 "
           "turnsRemain %d actionsRemain %d witness 1 playerInput %d enemyActive %d "
           "screen %u terminal %d resumeActor 0x%llX priorTuAfter %u resumeSlot %d "
           "resumeActive %d resumeCell %d/%d/%d resumeTu %u resumeHp %u resumeStatus %u "
           "queue %d/%d\n",
           (int)pMove->nSequence, (int)nTurn, (int)nActions,
           (int)(nMaxTurns - nTurn), (int)(160 - nActions),
           (int)g_world.bMapPlayerInput, (int)g_world.bMapEnemyTurnActive,
           (unsigned)g_world.nUiScreen, bTerminal ? 1 : 0,
           (unsigned long long)pMove->nActorKey, (unsigned)pMove->uTuAfter,
           (int)nResumeSlot, bResumeActive ? 1 : 0,
           bResumeActive ? (int)pResume->pCell->nX : -1,
           bResumeActive ? (int)pResume->pCell->nY : -1,
           bResumeActive ? (int)pResume->pCell->nZ : -1,
           bResumeActive ? (unsigned)pResume->uTu : 0u,
           bResumeActive ? (unsigned)pResume->uHp : 0u,
           bResumeActive ? (unsigned)pResume->bStatus : 255u,
           pQueue != NULL ? (int)pQueue->nQueueLen : -1,
           pQueue != NULL ? (int)pQueue->bQueueActive : -1);
    fflush(stdout);
    if (bTerminal) { return true; }
    return g_world.bMapPlayerInput != 0 && g_world.bMapEnemyTurnActive == 0
        && g_world.nUiScreen == 0u && World_bTactPointerLive(&g_world)
        && pMap != NULL && pQueue != NULL && pMap->tact.bBusy == 0
        && pQueue->nQueueLen == 0 && pQueue->bQueueActive == 0;
}

static bool App_ScriptCampaignFireRefresh(uintptr_t nMapKey, uintptr_t nQueueKey,
                                          AppCampaignFireWeapon *pRef,
                                          bool bRequireSameAmmo,
                                          GameMap **ppMap, TactState **ppQueue,
                                          Soldier **ppUnit, TactItem **ppWeapon)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit = NULL;
    TactItem *pWeapon = NULL;
    i32 nPool = -1, nRosterSlot = -1, nRosterMatches;
    i32 nCellSlot = -1;
    const i32 nPoolCapacity = (i32)(sizeof(g_world.apTactItems)
                                    / sizeof(g_world.apTactItems[0]));
    const char *pszFailure = NULL;

    if (ppMap != NULL) { *ppMap = NULL; }
    if (ppQueue != NULL) { *ppQueue = NULL; }
    if (ppUnit != NULL) { *ppUnit = NULL; }
    if (ppWeapon != NULL) { *ppWeapon = NULL; }
    if (pRef == NULL) {
        pszFailure = "missing_ref";
    } else if (!App_ScriptCampaignTactOwnerKeysMatch(nMapKey, nQueueKey,
                                                     &pMap, &pQueue)) {
        pszFailure = App_ScriptCampaignFireOwnerFailure(nMapKey, nQueueKey);
    } else if (pMap->nXcomCount > PUFO_MAP_ROSTER_MAX
               || pMap->nEnemyCount > PUFO_MAP_ROSTER_MAX) {
        pszFailure = "roster_count_bounds";
    } else {
        nRosterMatches = App_ScriptCampaignFireRosterMatches(
            pMap, pRef->nActorKey, &nRosterSlot);
        if (nRosterMatches < 0) {
            pszFailure = "actor_roster_bounds";
        } else if (nRosterMatches == 0) {
            pszFailure = "actor_not_in_live_roster";
        } else if (nRosterMatches != 1) {
            pszFailure = "actor_not_unique_in_roster";
        } else {
            pUnit = App_ScriptCampaignFindActiveXcom(pMap, pRef->nActorKey);
            if (pUnit == NULL) {
                pszFailure = "actor_not_alive_or_active";
            } else if (pMap->pSelUnit != pUnit) {
                pszFailure = "selected_actor_changed";
            } else {
                nCellSlot = App_ScriptCampaignTactCellSlot(pUnit->pCell);
                if (nCellSlot < 0) {
                    pszFailure = (nCellSlot == -2)
                        ? "actor_cell_not_unique_in_grid" : "actor_cell_not_in_grid";
                } else if (g_world.apTactCells[nCellSlot]->pUnit != pUnit) {
                    pszFailure = "actor_cell_owner_changed";
                }
            }
        }
    }

    if (pszFailure == NULL) {
        if (g_world.nTactItemCount < 0 || g_world.nTactItemCount > nPoolCapacity) {
            pszFailure = "item_pool_count_bounds";
        } else {
            nPool = App_ScriptCampaignFirePoolIndex(pRef->nWeaponKey);
            if (nPool == -2) {
                pszFailure = "weapon_not_unique_in_pool";
            } else if (nPool < 0) {
                pszFailure = "weapon_missing_from_pool";
            } else if (g_world.apTactItems[nPool] == NULL
                       || (uintptr_t)(void *)g_world.apTactItems[nPool] != pRef->nWeaponKey) {
                pszFailure = "weapon_pool_key_changed";
            } else {
                pWeapon = g_world.apTactItems[nPool];
                if (g_assets.apObjects == NULL || g_assets.nObjectCount <= 4
                    || g_assets.apObjects[4] == NULL) {
                    pszFailure = "rifle_object_unavailable";
                } else if (pWeapon->pObj != g_assets.apObjects[4]) {
                    pszFailure = "weapon_object_changed";
                } else if (pWeapon->nIndex != pRef->nItemIndex) {
                    pszFailure = "weapon_item_index_changed";
                } else if (bRequireSameAmmo && pWeapon->nAmmo != pRef->nAmmo) {
                    pszFailure = "weapon_ammo_changed_before_action";
                } else if (pRef->nHandWidget == 17
                           ? (uintptr_t)(void *)pUnit->pHandL != pRef->nWeaponKey
                           : (pRef->nHandWidget == 18
                              ? (uintptr_t)(void *)pUnit->pHandR != pRef->nWeaponKey
                              : true)) {
                    pszFailure = "held_weapon_key_changed";
                }
            }
        }
    }

    if (pszFailure != NULL) {
        const i32 nMatchesForLog = (pMap != NULL && pRef != NULL)
            ? App_ScriptCampaignFireRosterMatches(pMap, pRef->nActorKey, &nRosterSlot) : -1;
        const i32 nCurrentItemIndex = pWeapon != NULL ? (i32)pWeapon->nIndex : -1;
        const i32 nCurrentAmmo = pWeapon != NULL ? (i32)pWeapon->nAmmo : -1;
        printf("pocketufo: campaign fire refresh: FAIL check=%s actor=0x%llx "
               "oldSlot=%d currentSlot=%d rosterMatches=%d oldPool=%d currentPool=%d "
               "weapon=0x%llx expectedItemIndex=%u currentItemIndex=%d expectedAmmo=%u "
               "currentAmmo=%d sameAmmoRequired=%d actorCellSlot=%d\n",
               pszFailure,
               (unsigned long long)(pRef != NULL ? pRef->nActorKey : 0u),
               pRef != NULL ? (int)pRef->nRosterSlot : -1, (int)nRosterSlot,
               (int)nMatchesForLog, pRef != NULL ? (int)pRef->nPoolIndex : -1,
               (int)nPool,
               (unsigned long long)(pRef != NULL ? pRef->nWeaponKey : 0u),
               pRef != NULL ? (unsigned)pRef->nItemIndex : 0u, (int)nCurrentItemIndex,
               pRef != NULL ? (unsigned)pRef->nAmmo : 0u, (int)nCurrentAmmo,
               bRequireSameAmmo ? 1 : 0, (int)nCellSlot);
        fflush(stdout);
        return false;
    }
    if (nRosterSlot != pRef->nRosterSlot || nPool != pRef->nPoolIndex) {
        printf("pocketufo: campaign fire refresh: relocated actor=0x%llx "
               "oldSlot=%d currentSlot=%d weapon=0x%llx oldPool=%d currentPool=%d "
               "itemIndex=%u ammo=%u sameAmmoRequired=%d actorCellSlot=%d\n",
               (unsigned long long)pRef->nActorKey, (int)pRef->nRosterSlot,
               (int)nRosterSlot, (unsigned long long)pRef->nWeaponKey,
               (int)pRef->nPoolIndex, (int)nPool, (unsigned)pWeapon->nIndex,
               (unsigned)pWeapon->nAmmo, bRequireSameAmmo ? 1 : 0, (int)nCellSlot);
        fflush(stdout);
    }
    pRef->nRosterSlot = nRosterSlot;
    pRef->nPoolIndex = nPool;
    if (ppMap != NULL) { *ppMap = pMap; }
    if (ppQueue != NULL) { *ppQueue = pQueue; }
    if (ppUnit != NULL) { *ppUnit = pUnit; }
    if (ppWeapon != NULL) { *ppWeapon = pWeapon; }
    return true;
}

static void App_ScriptCampaignFireWitness(uintptr_t nMapKey, uintptr_t nQueueKey,
                                          const char *pszPhase,
                                          const AppCampaignFireWeapon *pRef,
                                          uintptr_t nTargetKey,
                                          i32 nRequestedX, i32 nRequestedY, i32 nRequestedZ)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit = NULL, *pTarget = NULL;
    TactItem *pWeapon = NULL;
    i32 nActorSlot = -1, nActorMatches = -1, nWeaponPool = -1;
    i32 nTargetSlot = -1, nTargetMatches = 0, nTargetType = -1;
    i32 nTargetHp = -1, nTargetArmorPoints = -1;
    i32 nTargetCellSlot = -1, nTargetX = -1, nTargetY = -1, nTargetZ = -1;
    i32 nTargetCellOwner = -1, nActorCellSlot = -1, nActorCellOwner = -1;
    u16 uWeaponItemIndex = 0;
    u16 uTu = 0;
    u8 uAmmo = 0;
    bool bActorLive = false, bWeaponLive = false, bWeaponIdentity = false;
    bool bEnemyRosterValid = false;

    if (pRef == NULL) {
        printf("pocketufo: campaign fire witness: phase=%s ownerState=missing_ref "
               "actorKey=0x0 weaponKey=0x0 targetKey=0x%llx "
               "targetStateSource=unavailable sharedTargetArg=SHARED_LAST_RESOLVED "
               "targetActorAttribution=unproven\n",
               pszPhase != NULL ? pszPhase : "unknown",
               (unsigned long long)nTargetKey);
        fflush(stdout);
        return;
    }
    if (!App_ScriptCampaignTactOwnerKeysMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
        printf("pocketufo: campaign fire witness: phase=%s ownerState=%s "
               "actorKey=0x%llx weaponKey=0x%llx targetKey=0x%llx "
               "targetStateSource=unavailable sharedTargetArg=SHARED_LAST_RESOLVED "
               "targetActorAttribution=unproven\n",
               pszPhase != NULL ? pszPhase : "unknown",
               App_ScriptCampaignFireOwnerFailure(nMapKey, nQueueKey),
               (unsigned long long)pRef->nActorKey,
               (unsigned long long)pRef->nWeaponKey,
               (unsigned long long)nTargetKey);
        fflush(stdout);
        return;
    }
    bEnemyRosterValid = pMap->nEnemyCount <= PUFO_MAP_ROSTER_MAX;

    if (pMap->nXcomCount <= PUFO_MAP_ROSTER_MAX) {
        nActorMatches = App_ScriptCampaignFireRosterMatches(
            pMap, pRef->nActorKey, &nActorSlot);
        if (nActorMatches == 1) {
            pUnit = App_ScriptCampaignFindActiveXcom(pMap, pRef->nActorKey);
            bActorLive = pUnit != NULL && pMap->pSelUnit == pUnit;
            if (bActorLive) {
                uTu = pUnit->uTu;
                nActorCellSlot = App_ScriptCampaignTactCellSlot(pUnit->pCell);
                if (nActorCellSlot >= 0) {
                    nActorCellOwner = g_world.apTactCells[nActorCellSlot]->pUnit == pUnit ? 1 : 0;
                }
            }
        }
    }
    nWeaponPool = App_ScriptCampaignFirePoolIndex(pRef->nWeaponKey);
    if (nWeaponPool >= 0
        && g_world.apTactItems[nWeaponPool] != NULL
        && (uintptr_t)(void *)g_world.apTactItems[nWeaponPool] == pRef->nWeaponKey) {
        pWeapon = g_world.apTactItems[nWeaponPool];
        bWeaponLive = true;
        uWeaponItemIndex = pWeapon->nIndex;
        uAmmo = pWeapon->nAmmo;
        bWeaponIdentity = g_assets.apObjects != NULL && g_assets.nObjectCount > 4
            && g_assets.apObjects[4] != NULL && pWeapon->pObj == g_assets.apObjects[4]
            && pWeapon->nIndex == pRef->nItemIndex;
    }

    if (bEnemyRosterValid) {
        for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
            Soldier *pCandidate = pMap->apEnemyUnits[i];
            if ((uintptr_t)(void *)pCandidate != nTargetKey) { continue; }
            ++nTargetMatches;
            if (nTargetMatches == 1) {
                pTarget = pCandidate;
                nTargetSlot = i;
            }
        }
        if (nTargetMatches == 1 && pTarget != NULL) {
            const i32 nCellSlot = App_ScriptCampaignTactCellSlot(pTarget->pCell);
            nTargetType = (i32)pTarget->nUnitType;
            nTargetHp = (i32)pTarget->uHp;
            /* Soldier.nArmorPoints is the raw +0x1A8 damage pool; uStunOrArmor is not armor. */
            nTargetArmorPoints = pTarget->nArmorPoints;
            if (nCellSlot >= 0) {
                const MapCell *pCell = g_world.apTactCells[nCellSlot];
                nTargetCellSlot = nCellSlot;
                nTargetX = (i32)pCell->nX;
                nTargetY = (i32)pCell->nY;
                nTargetZ = (i32)pCell->nZ;
                nTargetCellOwner = pCell->pUnit == pTarget ? 1 : 0;
            }
        }
    }

    printf("pocketufo: campaign fire witness: phase=%s ownerState=live "
           "actorKey=0x%llx rosterCounts=%u/%u actorMatches=%d actorSlot=%d actorLiveSelected=%d "
           "actorCellSlot=%d actorCellOwner=%d tu=%u weaponKey=0x%llx "
           "weaponPool=%d weaponLive=%d weaponIdentity=%d expectedItemIndex=%u "
           "currentItemIndex=%u ammo=%u "
           "targetKey=0x%llx enemyRosterValid=%d targetMatches=%d targetSlot=%d "
           "targetStateSource=live_enemy_roster sharedTargetArg=SHARED_LAST_RESOLVED "
           "targetActorAttribution=unproven requestedCell=%d/%d/%d targetType=%d targetHp=%d "
           "targetArmorPoints=%d targetCellSlot=%d targetCell=%d/%d/%d targetCellOwner=%d "
           "queue=%d/%d active=%u\n",
           pszPhase != NULL ? pszPhase : "unknown",
           (unsigned long long)pRef->nActorKey, (unsigned)pMap->nXcomCount,
           (unsigned)pMap->nEnemyCount, (int)nActorMatches, (int)nActorSlot,
           bActorLive ? 1 : 0, (int)nActorCellSlot, (int)nActorCellOwner,
           (unsigned)uTu, (unsigned long long)pRef->nWeaponKey, (int)nWeaponPool,
           bWeaponLive ? 1 : 0, bWeaponIdentity ? 1 : 0, (unsigned)pRef->nItemIndex,
           (unsigned)uWeaponItemIndex, (unsigned)uAmmo,
           (unsigned long long)nTargetKey, bEnemyRosterValid ? 1 : 0,
           (int)nTargetMatches, (int)nTargetSlot,
           nRequestedX, nRequestedY, nRequestedZ, (int)nTargetType, (int)nTargetHp,
           (int)nTargetArmorPoints, (int)nTargetCellSlot, (int)nTargetX, (int)nTargetY,
           (int)nTargetZ, (int)nTargetCellOwner, (int)pQueue->nQueueCursor,
           (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive);
    fflush(stdout);
}

typedef struct AppCampaignFireCensusCounts {
    i32 nNullLinks;
    i32 nEmptySentinelLinks;
    i32 nPoolEmptySentinels;
    i32 nOtherCanonicalLinks;
    i32 nUnresolvedLinks;
    i32 nRifleOwnerLinks;
    i32 nClipOwnerLinks;
    i32 nGroundNodes;
    i32 nGroundErrors;
    i32 anForwardOwners[2048];
} AppCampaignFireCensusCounts;

static void App_ScriptCampaignFireCensusLink(const char *pszStage,
                                             const char *pszTeam, i32 nRosterSlot,
                                             Soldier *pUnit, const char *pszOwnerKind,
                                             i32 nOwnerSlot, TactItem *pLink,
                                             const ObjectRecFull *pRifleObj,
                                             const ObjectRecFull *pClipObj,
                                             AppCampaignFireCensusCounts *pCounts)
{
    const uintptr_t nLinkKey = (uintptr_t)(void *)pLink;
    i32 nPool;
    TactItem *pItem;
    i32 nObjectId;

    if (pLink == NULL) {
        ++pCounts->nNullLinks;
        return;
    }
    if (pLink == &g_itemSlotEmpty) {
        ++pCounts->nEmptySentinelLinks;
        return;
    }
    nPool = App_ScriptCampaignFirePoolIndex(nLinkKey);
    if (nPool < 0 || g_world.apTactItems[nPool] != pLink) {
        ++pCounts->nUnresolvedLinks;
        return;
    }
    pItem = g_world.apTactItems[nPool];
    if (pItem->pObj == pRifleObj) {
        nObjectId = 4;
        ++pCounts->nRifleOwnerLinks;
    } else if (pItem->pObj == pClipObj) {
        nObjectId = 38;
        ++pCounts->nClipOwnerLinks;
    } else {
        ++pCounts->nOtherCanonicalLinks;
        return;
    }
    ++pCounts->anForwardOwners[nPool];
    printf("pocketufo: campaign loadout owner: stage=%s ownerKind=%s ownerSlot=%d "
           "team=%s roster=%d actorKey=0x%llx actorType=%u objectId=%d pool=%d "
           "itemIndex=%u rawAmmo=%u\n",
           pszStage, pszOwnerKind, (int)nOwnerSlot, pszTeam, (int)nRosterSlot,
           (unsigned long long)(uintptr_t)(void *)pUnit, (unsigned)pUnit->nUnitType,
           (int)nObjectId, (int)nPool, (unsigned)pItem->nIndex, (unsigned)pItem->nAmmo);
}

static void App_ScriptCampaignFireCensusUnit(const char *pszStage, const char *pszTeam,
                                             i32 nRosterSlot, Soldier *pUnit,
                                             const ObjectRecFull *pRifleObj,
                                             const ObjectRecFull *pClipObj,
                                             AppCampaignFireCensusCounts *pCounts)
{
    if (pUnit == NULL) { return; }
    App_ScriptCampaignFireCensusLink(pszStage, pszTeam, nRosterSlot, pUnit,
                                     "handL", -1, pUnit->pHandL,
                                     pRifleObj, pClipObj, pCounts);
    App_ScriptCampaignFireCensusLink(pszStage, pszTeam, nRosterSlot, pUnit,
                                     "handR", -1, pUnit->pHandR,
                                     pRifleObj, pClipObj, pCounts);
    for (i32 i = 0; i < PUFO_UNIT_POCKETS; ++i) {
        for (i32 j = 0; j < 2; ++j) {
            App_ScriptCampaignFireCensusLink(pszStage, pszTeam, nRosterSlot, pUnit,
                                             "pocket", i * 2 + j,
                                             pUnit->aPockets[i].apItem[j],
                                             pRifleObj, pClipObj, pCounts);
        }
    }
    for (i32 i = 0; i < PUFO_UNIT_BACKPACK_SLOTS; ++i) {
        App_ScriptCampaignFireCensusLink(pszStage, pszTeam, nRosterSlot, pUnit,
                                         "backpack", i, pUnit->apBackpack[i],
                                         pRifleObj, pClipObj, pCounts);
    }
    for (i32 i = 0; i < PUFO_UNIT_BELT_SLOTS; ++i) {
        App_ScriptCampaignFireCensusLink(pszStage, pszTeam, nRosterSlot, pUnit,
                                         "belt", i, pUnit->apBelt[i],
                                         pRifleObj, pClipObj, pCounts);
    }
}

static void App_ScriptCampaignFireLoadoutCensus(uintptr_t nMapKey, uintptr_t nQueueKey,
                                                const char *pszStage)
{
    const i32 nPoolCapacity = (i32)(sizeof(g_world.apTactItems)
                                    / sizeof(g_world.apTactItems[0]));
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    const ObjectRecFull *pRifleObj;
    const ObjectRecFull *pClipObj;
    AppCampaignFireCensusCounts sCounts = { 0 };
    i32 nRiflePoolItems = 0, nClipPoolItems = 0;
    bool bGroundBudgetHit = false;

    if (!App_ScriptCampaignTactOwnerKeysMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
        printf("pocketufo: campaign loadout census: stage=%s ownerState=map_or_queue_missing\n",
               pszStage);
        fflush(stdout);
        return;
    }
    (void)pQueue;
    if (pMap->nXcomCount > PUFO_MAP_ROSTER_MAX
        || pMap->nEnemyCount > PUFO_MAP_ROSTER_MAX
        || g_world.nTactItemCount < 0 || g_world.nTactItemCount > nPoolCapacity) {
        printf("pocketufo: campaign loadout census: stage=%s ownerState=bad_bounds "
               "xcom=%u enemy=%u itemPool=%d\n",
               pszStage, (unsigned)pMap->nXcomCount, (unsigned)pMap->nEnemyCount,
               (int)g_world.nTactItemCount);
        fflush(stdout);
        return;
    }
    if (g_assets.apObjects == NULL || g_assets.nObjectCount <= 38
        || g_assets.apObjects[4] == NULL || g_assets.apObjects[38] == NULL) {
        printf("pocketufo: campaign loadout census: stage=%s ownerState=object_table_missing\n",
               pszStage);
        fflush(stdout);
        return;
    }
    pRifleObj = g_assets.apObjects[4];
    pClipObj = g_assets.apObjects[38];

    for (i32 i = 0; i < (i32)pMap->nXcomCount; ++i) {
        App_ScriptCampaignFireCensusUnit(pszStage, "xcom", i, pMap->apXcomUnits[i],
                                         pRifleObj, pClipObj, &sCounts);
    }
    for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
        App_ScriptCampaignFireCensusUnit(pszStage, "enemy", i, pMap->apEnemyUnits[i],
                                         pRifleObj, pClipObj, &sCounts);
    }

    for (i32 nCellSlot = 0; nCellSlot < PUFO_TERRAIN_CELL_SLOTS; ++nCellSlot) {
        MapCell *pCell = g_world.apTactCells[nCellSlot];
        TactItem *pItem = pCell != NULL ? pCell->pItemList : NULL;
        while (pItem != NULL) {
            const uintptr_t nItemKey = (uintptr_t)(void *)pItem;
            i32 nPool = -1;
            const char *pszGroundError = NULL;

            if (pItem == &g_itemSlotEmpty) {
                pszGroundError = "empty_sentinel";
            } else if (sCounts.nGroundNodes >= g_world.nTactItemCount) {
                pszGroundError = "node_budget";
                bGroundBudgetHit = true;
            } else {
                nPool = App_ScriptCampaignFirePoolIndex(nItemKey);
                if (nPool < 0 || g_world.apTactItems[nPool] != pItem) {
                    pszGroundError = "noncanonical_item";
                }
            }
            if (pszGroundError != NULL) {
                ++sCounts.nGroundErrors;
                printf("pocketufo: campaign loadout ground: stage=%s cellSlot=%d "
                       "nodeKey=0x%llx pool=%d reason=%s\n",
                       pszStage, (int)nCellSlot, (unsigned long long)nItemKey, (int)nPool,
                       pszGroundError);
                fflush(stdout);
                break;
            }
            {
                TactItem *pLiveItem = g_world.apTactItems[nPool];
                i32 nObjectId = 0;
                if (pLiveItem->pObj == pRifleObj) {
                    nObjectId = 4;
                    ++sCounts.nRifleOwnerLinks;
                    ++sCounts.anForwardOwners[nPool];
                } else if (pLiveItem->pObj == pClipObj) {
                    nObjectId = 38;
                    ++sCounts.nClipOwnerLinks;
                    ++sCounts.anForwardOwners[nPool];
                }
                if (nObjectId != 0) {
                    printf("pocketufo: campaign loadout owner: stage=%s ownerKind=groundCell "
                           "team=none roster=-1 actorKey=0x0 actorType=-1 objectId=%d pool=%d "
                           "itemIndex=%u rawAmmo=%u cellSlot=%d cell=%u/%u/%u\n",
                           pszStage, nObjectId, (int)nPool, (unsigned)pLiveItem->nIndex,
                           (unsigned)pLiveItem->nAmmo, (int)nCellSlot, (unsigned)pCell->nX,
                           (unsigned)pCell->nY, (unsigned)pCell->nZ);
                }
                pItem = pLiveItem->pNext;
            }
            ++sCounts.nGroundNodes;
        }
        if (bGroundBudgetHit) { break; }
    }

    for (i32 i = 0; i < g_world.nTactItemCount; ++i) {
        TactItem *pItem = g_world.apTactItems[i];
        i32 nObjectId = 0;
        if (pItem == NULL) { continue; }
        if (pItem == &g_itemSlotEmpty) {
            ++sCounts.nPoolEmptySentinels;
            continue;
        }
        if (pItem->pObj == pRifleObj) {
            nObjectId = 4;
            ++nRiflePoolItems;
        } else if (pItem->pObj == pClipObj) {
            nObjectId = 38;
            ++nClipPoolItems;
        }
        if (nObjectId != 0) {
            printf("pocketufo: campaign loadout item: stage=%s objectId=%d pool=%d "
                   "key=0x%llx itemIndex=%u rawAmmo=%u forwardOwners=%d\n",
                   pszStage, nObjectId, (int)i, (unsigned long long)(uintptr_t)(void *)pItem,
                   (unsigned)pItem->nIndex, (unsigned)pItem->nAmmo,
                   (int)sCounts.anForwardOwners[i]);
        }
    }
    printf("pocketufo: campaign loadout census: stage=%s xcom=%u enemy=%u itemPool=%d "
           "rifleItems=%d clipItems=%d rifleOwnerLinks=%d clipOwnerLinks=%d "
           "nullLinks=%d emptySentinels=%d poolEmptySentinels=%d otherCanonicalLinks=%d "
           "unresolvedLinks=%d groundNodes=%d groundErrors=%d\n",
           pszStage, (unsigned)pMap->nXcomCount, (unsigned)pMap->nEnemyCount,
           (int)g_world.nTactItemCount, (int)nRiflePoolItems, (int)nClipPoolItems,
           (int)sCounts.nRifleOwnerLinks, (int)sCounts.nClipOwnerLinks,
           (int)sCounts.nNullLinks, (int)sCounts.nEmptySentinelLinks,
           (int)sCounts.nPoolEmptySentinels, (int)sCounts.nOtherCanonicalLinks,
           (int)sCounts.nUnresolvedLinks,
           (int)sCounts.nGroundNodes, (int)sCounts.nGroundErrors);
    fflush(stdout);
}

static bool App_ScriptCampaignFireNativeMenuGate(GameMap *pMap, Soldier *pUnit,
                                                 TactItem *pWeapon, const char **ppReason)
{
    const u16 nSortKey = (pWeapon != NULL && pWeapon->pObj != NULL)
        ? pWeapon->pObj->uSortKey : 0;
    i32 nGate;

    if (ppReason != NULL) { *ppReason = NULL; }
    if (pMap == NULL || pUnit == NULL || pWeapon == NULL || pWeapon->pObj == NULL
        || pMap->pSelUnit != pUnit || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE) {
        if (ppReason != NULL) { *ppReason = "selected actor, held rifle, or action latch is invalid"; }
        return false;
    }
    if (nSortKey >= PUFO_SLOT_TOPIC_GATE_COUNT) {
        if (ppReason != NULL) { *ppReason = "rifle sort key is outside the menu topic table"; }
        return false;
    }
    nGate = (i32)g_abSlotTopicGate[nSortKey];
    if (nGate < 0 || nGate >= (i32)g_world.nResearchEntries
        || g_world.aTopics[nGate].bUnlocked == 0) {
        if (ppReason != NULL) { *ppReason = "rifle menu research gate is closed"; }
        return false;
    }
    if (nSortKey == 24 && pUnit->bPsiActive == 0) {
        if (ppReason != NULL) { *ppReason = "sort-key-24 menu requires an active psi unit"; }
        return false;
    }
    return true;
}

static AppCampaignSnapQuoteResult App_ScriptCampaignQuoteSnap(
    uintptr_t nMapKey, uintptr_t nQueueKey,
    AppCampaignFireWeapon *pRef, i32 *pnSnapCost)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit = NULL;
    TactItem *pWeapon = NULL;
    const char *pszGateReason = NULL;
    u16 uTuBefore, uAmmoBefore;
    i32 nSnapCost, nSnapEnabled;
    u8 bSnapWidget;
    bool bSnapWidgetCallback;
    bool bMenuOpened;
    bool bQuoteReady;

    if (pnSnapCost != NULL) { *pnSnapCost = 0; }
    if (pRef == NULL || g_world.nUiScreen != 0u
        || !App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, pRef, true,
                                          &pMap, &pQueue, &pUnit, &pWeapon)
        || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
        || pMap->tact.bBusy != 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
        || pMap->pSelUnit != pUnit) {
        printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=pre-menu live owner or idle gate\n");
        fflush(stdout);
        return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
    }
    if (!App_ScriptCampaignFireNativeMenuGate(pMap, pUnit, pWeapon, &pszGateReason)) {
        printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=menu-gate reason=%s\n",
               pszGateReason != NULL ? pszGateReason : "unknown gate");
        fflush(stdout);
        return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
    }
    if (pRef->nHandWidget != 17 && pRef->nHandWidget != 18) {
        printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=hand-widget value=%d\n",
               (int)pRef->nHandWidget);
        fflush(stdout);
        return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
    }
    if (g_bank.aCtl[pRef->nHandWidget].bEnabled == 0
        || g_bank.aCtl[pRef->nHandWidget].pfnClick == NULL) {
        printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=hand-widget-control "
               "widget=%d enabled=%u callback=%d\n",
               (int)pRef->nHandWidget, (unsigned)g_bank.aCtl[pRef->nHandWidget].bEnabled,
               g_bank.aCtl[pRef->nHandWidget].pfnClick != NULL ? 1 : 0);
        fflush(stdout);
        return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
    }

    uTuBefore = pUnit->uTu;
    uAmmoBefore = pWeapon->nAmmo;
    if (!App_ScriptCampaignClickBank(pRef->nHandWidget)) {
        if (g_world.nUiScreen == 3u) {
            (void)App_ScriptCampaignKey(PUFO_KEY_CONFIRM);
            (void)App_DrawFrame();
        }
        printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=hand-input actor 0x%llX\n",
               (unsigned long long)pRef->nActorKey);
        fflush(stdout);
        return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
    }
    if (!App_ScriptCampaignTactOwnerKeysMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || !App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, pRef, true,
                                          &pMap, &pQueue, &pUnit, &pWeapon)
        || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
        || pMap->tact.bBusy != 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
        || pUnit->uTu != uTuBefore || pWeapon->nAmmo != uAmmoBefore
        || (g_world.nUiScreen != 0u && g_world.nUiScreen != 3u)) {
        if (g_world.nUiScreen == 3u) {
            (void)App_ScriptCampaignKey(PUFO_KEY_CONFIRM);
            (void)App_DrawFrame();
        }
        printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=menu-open owner, ammo, TU, or screen changed\n");
        fflush(stdout);
        return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
    }
    bMenuOpened = g_world.nUiScreen == 3u;
    if (bMenuOpened) {
        if (pMap->pSelWeapon != pWeapon) {
            (void)App_ScriptCampaignKey(PUFO_KEY_CONFIRM);
            (void)App_DrawFrame();
            printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=menu-weapon owner changed\n");
            fflush(stdout);
            return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
        }
        /* App_ScriptCampaignClickBank drew the real hand menu before returning. */
        if (!App_ScriptCampaignTactOwnerKeysMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || !App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, pRef, true,
                                              &pMap, &pQueue, &pUnit, &pWeapon)
            || g_world.nUiScreen != 3u || pMap->pSelWeapon != pWeapon
            || pUnit->uTu != uTuBefore || pWeapon->nAmmo != uAmmoBefore
            || pMap->tact.bBusy != 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
            || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
            if (g_world.nUiScreen == 3u) {
                (void)App_ScriptCampaignKey(PUFO_KEY_CONFIRM);
                (void)App_DrawFrame();
            }
            printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=menu-draw owner, ammo, or TU changed\n");
            fflush(stdout);
            return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
        }
    }

    if (!App_ScriptCampaignFireNativeMenuGate(pMap, pUnit, pWeapon, &pszGateReason)) {
        if (bMenuOpened) {
            (void)App_ScriptCampaignKey(PUFO_KEY_CONFIRM);
            (void)App_DrawFrame();
        }
        printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=menu-cost gate reason=%s\n",
               pszGateReason != NULL ? pszGateReason : "unknown gate");
        fflush(stdout);
        return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
    }
    nSnapCost = g_world.sTactUi.anActionCost[PUFO_ACTION_MODE_SNAP];
    nSnapEnabled = g_world.sTactUi.anActionEnabled[PUFO_ACTION_MODE_SNAP];
    bSnapWidget = g_bank.aCtl[33].bEnabled;
    bSnapWidgetCallback = g_bank.aCtl[33].pfnClick != NULL;
    bQuoteReady = bMenuOpened && nSnapCost > 0 && nSnapEnabled != 0
        && bSnapWidget != 0 && bSnapWidgetCallback && nSnapCost <= (i32)uTuBefore;

    if (bMenuOpened) {
        if (!App_ScriptCampaignKey(PUFO_KEY_CONFIRM)) {
            printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=menu-confirm-close input\n");
            fflush(stdout);
            return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
        }
        (void)App_DrawFrame();
    }
    if (g_world.nUiScreen != 0u
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || !App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, pRef, true,
                                          &pMap, &pQueue, &pUnit, &pWeapon)
        || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
        || pMap->pSelUnit != pUnit || pMap->pSelWeapon != pWeapon || pMap->tact.bBusy != 0
        || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
        || pUnit->uTu != uTuBefore || pWeapon->nAmmo != uAmmoBefore) {
        printf("pocketufo: campaign outside-plane Snap quote: FAIL stage=post-menu-close owner, ammo, TU, or idle state changed\n");
        fflush(stdout);
        return APP_CAMPAIGN_SNAP_QUOTE_FAILED;
    }
    printf("pocketufo: campaign outside-plane Snap quote: actorKey 0x%llX roster %d "
           "weaponKey 0x%llX itemIndex %u handWidget %d snapWidget 33 "
           "snapCost %d snapEnabled %d widgetEnabled %u widgetCallback %d menuOpened %d "
           "menuCloseKey %s menuClosed %d selectedWeapon 1 "
           "screen %u ammo %u->%u tu %u->%u queue %d/%u quoteReady %d\n",
           (unsigned long long)pRef->nActorKey, (int)pRef->nRosterSlot,
           (unsigned long long)pRef->nWeaponKey, (unsigned)pRef->nItemIndex,
           (int)pRef->nHandWidget, (int)nSnapCost, (int)nSnapEnabled,
           (unsigned)bSnapWidget, bSnapWidgetCallback ? 1 : 0,
           bMenuOpened ? 1 : 0, bMenuOpened ? "confirm" : "none", bMenuOpened ? 1 : 0,
           (unsigned)g_world.nUiScreen, (unsigned)uAmmoBefore, (unsigned)pWeapon->nAmmo,
           (unsigned)uTuBefore, (unsigned)pUnit->uTu,
           (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive, bQuoteReady ? 1 : 0);
    fflush(stdout);

    if (!bQuoteReady) { return APP_CAMPAIGN_SNAP_QUOTE_UNAVAILABLE; }
    if (pnSnapCost != NULL) { *pnSnapCost = nSnapCost; }
    return APP_CAMPAIGN_SNAP_QUOTE_READY;
}

static void App_ScriptCampaignFireCostSummary(i32 nTu, i32 *pnOffered,
                                              i32 *pnAffordable, i32 *pnMinOfferedCost)
{
    i32 nOffered = 0, nAffordable = 0, nMinCost = 0;
    bool bHasMinCost = false;

    for (i32 m = 0; m < PUFO_ACTION_MODE_COUNT; ++m) {
        const i32 nEnabled = g_world.sTactUi.anActionEnabled[m];
        const i32 nCost = g_world.sTactUi.anActionCost[m];

        if (nEnabled == 0) { continue; }
        ++nOffered;
        if (!bHasMinCost || nCost < nMinCost) {
            nMinCost = nCost;
            bHasMinCost = true;
        }
        if (nCost <= nTu) { ++nAffordable; }
    }
    if (pnOffered != NULL) { *pnOffered = nOffered; }
    if (pnAffordable != NULL) { *pnAffordable = nAffordable; }
    if (pnMinOfferedCost != NULL) { *pnMinOfferedCost = bHasMinCost ? nMinCost : -1; }
}

typedef struct AppCampaignOutsideTarget {
    Soldier *pUnit;
    uintptr_t nKey;
    i32 nRosterSlot;
    i32 nX;
    i32 nY;
    i32 nZ;
    i32 nDistance;
    bool bInNativeList;
    bool bInRange;
    bool bLosChecked;
    bool bLosClear;
    bool bSelected;
} AppCampaignOutsideTarget;

typedef enum AppCampaignOutsideTargetResult {
    APP_CAMPAIGN_OUTSIDE_TARGET_FAILED = -1,
    APP_CAMPAIGN_OUTSIDE_TARGET_BLOCKED = 0,
    APP_CAMPAIGN_OUTSIDE_TARGET_NONE = 1,
    APP_CAMPAIGN_OUTSIDE_TARGET_READY = 2
} AppCampaignOutsideTargetResult;

static bool App_ScriptCampaignOutsideInputGateClear(const GameMap *pMap,
                                                    const TactState *pQueue)
{
    return pMap != NULL && pQueue != NULL && g_world.nUiScreen == 0u
        && g_world.sModal.bOpen == 0 && g_world.bMapPlayerInput != 0
        && pMap->tact.bBusy == 0 && pQueue->nQueueLen == 0
        && pQueue->bQueueActive == 0 && g_world.bMapFire2 == 0
        && pMap->bFireStatus1 == 0 && pMap->bTrajHitStop == 0;
}

static i32 App_ScriptCampaignUnitLosZ(const MapCell *pCell)
{
    i32 nZ = 12 * (i32)pCell->nZ;

    if (pCell->pCeilSpr != NULL) {
        nZ -= pCell->pCeilSpr->iCeilParam;
    }
    return nZ + 8;
}

static bool App_ScriptCampaignUnitLosClear(const Soldier *pShooter,
                                           const Soldier *pTarget,
                                           bool *pbChecked)
{
    const MapCell *pFrom = pShooter->pCell;
    const MapCell *pTo = pTarget->pCell;
    const VisCtx *pVis = &g_world.tactEnv.vis;

    if (pbChecked != NULL) { *pbChecked = false; }
    if (pFrom == NULL || pTo == NULL || pVis->pTables == NULL
        || pVis->pfnLookup == NULL || pVis->pUser == NULL) {
        return false;
    }
    /* The raw helper draws a random yaw for a vertical-only ray. Native
     * visibility rejects a zero planar distance, so leave RNG alone here. */
    if (pFrom->nX == pTo->nX && pFrom->nY == pTo->nY) {
        return false;
    }
    if (pbChecked != NULL) { *pbChecked = true; }
    return Map_TraceLos(pVis->pTables, pVis->pfnLookup, pVis->pUser,
                        16 * (i32)pFrom->nX + 8, 16 * (i32)pFrom->nY + 8,
                        App_ScriptCampaignUnitLosZ(pFrom),
                        16 * (i32)pTo->nX + 8, 16 * (i32)pTo->nY + 8,
                        App_ScriptCampaignUnitLosZ(pTo),
                        pShooter, pTarget, NULL, NULL);
}

static AppCampaignOutsideTargetResult App_ScriptCampaignPickOutsideTarget(
    GameMap *pMap, TactState *pQueue, Soldier *pShooter, i32 nRosterSlot,
    const char *pszPhase, AppCampaignOutsideTarget *pOut)
{
    AppCampaignOutsideTarget aTargets[PUFO_MAP_ROSTER_MAX];
    i32 nCount = 0;
    i32 nBest = -1;
    const bool bActionGate = App_ScriptCampaignOutsideInputGateClear(pMap, pQueue);

    if (pOut != NULL) { memset(pOut, 0, sizeof(*pOut)); }
    if (pMap == NULL || pQueue == NULL || pShooter == NULL || pShooter->pCell == NULL
        || pMap->nEnemyCount > PUFO_MAP_ROSTER_MAX
        || g_world.tactEnv.vis.pTables == NULL
        || g_world.tactEnv.vis.pfnLookup == NULL
        || g_world.tactEnv.vis.pUser == NULL
        || pShooter->nMultiTarget < 0
        || pShooter->nMultiTarget > PUFO_UNIT_MULTITARGET_MAX) {
        printf("pocketufo: campaign outside-plane fire decision: phase %s actor 0x%llX "
               "roster %d target 0x0 cell -1/-1/-1 distance -1 range 0 los 0 visible 0 "
               "selected 0 actionGate %d reason visibility_context_unavailable\n",
               pszPhase != NULL ? pszPhase : "unknown",
               (unsigned long long)(uintptr_t)(void *)pShooter, (int)nRosterSlot,
               bActionGate ? 1 : 0);
        fflush(stdout);
        return APP_CAMPAIGN_OUTSIDE_TARGET_FAILED;
    }
    if (!bActionGate) {
        printf("pocketufo: campaign outside-plane fire decision: phase %s actor 0x%llX "
               "roster %d target 0x0 cell -1/-1/-1 distance -1 range 0 los 0 visible 0 "
               "selected 0 actionGate 0 reason native_input_gate\n",
               pszPhase != NULL ? pszPhase : "unknown",
               (unsigned long long)(uintptr_t)(void *)pShooter, (int)nRosterSlot);
        fflush(stdout);
        return APP_CAMPAIGN_OUTSIDE_TARGET_BLOCKED;
    }

    /* Read the target list produced by normal tactical visibility hooks. */
    for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
        Soldier *pEnemy = pMap->apEnemyUnits[i];
        AppCampaignOutsideTarget *pTarget;
        i32 nDx, nDy;

        if (pEnemy == NULL || pEnemy->pCell == NULL || pEnemy->uHp == 0
            || pEnemy->bStatus >= UNIT_STATUS_INACTIVE) {
            continue;
        }
        pTarget = &aTargets[nCount++];
        memset(pTarget, 0, sizeof(*pTarget));
        pTarget->pUnit = pEnemy;
        pTarget->nKey = (uintptr_t)(void *)pEnemy;
        pTarget->nRosterSlot = i;
        pTarget->nX = (i32)pEnemy->pCell->nX;
        pTarget->nY = (i32)pEnemy->pCell->nY;
        pTarget->nZ = (i32)pEnemy->pCell->nZ;
        nDx = (i32)pShooter->pCell->nX - pTarget->nX;
        nDy = (i32)pShooter->pCell->nY - pTarget->nY;
        pTarget->nDistance = (i32)sqrt((double)nDx * nDx + (double)nDy * nDy);
        pTarget->bInRange = pTarget->nDistance >= 1
            && pTarget->nDistance <= PUFO_VIS_SIGHT_RANGE;
        pTarget->bLosClear = App_ScriptCampaignUnitLosClear(
            pShooter, pEnemy, &pTarget->bLosChecked);
        for (i32 j = 0; j < (i32)pShooter->nMultiTarget; ++j) {
            if (pShooter->multiOrAnim.apMultiTargets[j] == pEnemy) {
                pTarget->bInNativeList = true;
                break;
            }
        }
        if (pTarget->bInRange && pTarget->bLosChecked && pTarget->bLosClear
            && pTarget->bInNativeList
            && (nBest < 0 || pTarget->nDistance < aTargets[nBest].nDistance)) {
            nBest = nCount - 1;
        }
    }

    for (i32 i = 0; i < nCount; ++i) {
        AppCampaignOutsideTarget *pTarget = &aTargets[i];
        const bool bSelected = i == nBest;
        const char *pszReason = bSelected ? "native_visible" :
            (!pTarget->bInRange ? "outside_native_range" :
             (!pTarget->bLosChecked ? "vertical_los_not_checked" :
              (!pTarget->bLosClear ? "los_blocked" :
               (!pTarget->bInNativeList ? "not_visible_to_shooter" : "farther_visible_target"))));

        pTarget->bSelected = bSelected;
        printf("pocketufo: campaign outside-plane fire decision: phase %s actor 0x%llX "
               "roster %d target 0x%llX targetRoster %d cell %d/%d/%d distance %d "
               "range %d los %d visible %d selected %d actionGate 1 reason %s\n",
               pszPhase != NULL ? pszPhase : "unknown",
               (unsigned long long)(uintptr_t)(void *)pShooter, (int)nRosterSlot,
               (unsigned long long)pTarget->nKey, (int)pTarget->nRosterSlot,
               pTarget->nX, pTarget->nY, pTarget->nZ, (int)pTarget->nDistance,
               pTarget->bInRange ? 1 : 0,
               pTarget->bLosChecked ? (pTarget->bLosClear ? 1 : 0) : -1,
               pTarget->bInNativeList ? 1 : 0, bSelected ? 1 : 0, pszReason);
    }
    fflush(stdout);
    if (nBest < 0) { return APP_CAMPAIGN_OUTSIDE_TARGET_NONE; }
    if (pOut != NULL) { *pOut = aTargets[nBest]; }
    return APP_CAMPAIGN_OUTSIDE_TARGET_READY;
}

static bool App_ScriptCampaignFire(const AppCampaignPlaneRect *pPlane,
                                  const AppCampaignFireWeapon *pExpectedFire,
                                  i32 nExpectedSnapCost,
                                  const char **ppszOutsideApproachReason)
{
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit = NULL;
    TactItem *pWeapon = NULL;
    AppCampaignFireWeapon sFire = { 0 };
    uintptr_t aTriedActorKeys[PUFO_MAP_ROSTER_MAX] = { 0 };
    i32 nRosterCount, nTriedActors = 0, nNoRifle = 0, nNoAffordable = 0, nNoVisible = 0;
    i32 nTargetX = 0, nTargetY = 0, nTargetZ = 0;
    i32 nTargetHpBefore = -1;
    i32 nPixelX = -1, nPixelY = -1, nQueueBefore = 0, nShotsQueued = 0;
    u16 uAmmo0 = 0, uTu0 = 0;
    uintptr_t nTargetKey = 0;
    bool bMenuOpen = false, bRosterWrapped = false;

    if (ppszOutsideApproachReason != NULL) { *ppszOutsideApproachReason = NULL; }

    if (g_world.nUiScreen != 0u
        || (pExpectedFire != NULL && pExpectedFire->nActorKey == 0)
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
        || pMap->tact.bBusy != 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
        || pMap->nEnemyCount <= 0 || pMap->nEnemyCount > PUFO_MAP_ROSTER_MAX
        || pMap->nXcomCount == 0 || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX) {
        printf("pocketufo: campaign fire: FAIL (need live player input, enemies, and an idle queue)\n");
        fflush(stdout);
        return false;
    }
    nRosterCount = (i32)pMap->nXcomCount;
    printf("pocketufo: campaign fire search: start roster %d\n", (int)nRosterCount);
    fflush(stdout);

    for (i32 nTry = 0; nTry < nRosterCount; ++nTry) {
        uintptr_t nActorKey;
        i32 nRosterSlot;
        Soldier *pCandidate;
        AppCampaignFireWeapon sCandidate = { 0 };
        AppCampaignFireFindResult eFind;
        bool bTryNext = false;

        if (g_world.nUiScreen != 0u
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
            || pMap->nXcomCount != nRosterCount || pMap->tact.bBusy != 0
            || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
            || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
            printf("pocketufo: campaign fire: FAIL live state changed during bounded rifle search\n");
            fflush(stdout);
            return false;
        }
        nActorKey = (uintptr_t)(void *)pMap->pSelUnit;
        nRosterSlot = App_ScriptCampaignFireRosterSlot(pMap, nActorKey);
        if (nActorKey == 0 || nRosterSlot < 0) {
            printf("pocketufo: campaign fire: FAIL selected actor is not a unique live roster entry\n");
            fflush(stdout);
            return false;
        }
        if (pExpectedFire != NULL && nActorKey != pExpectedFire->nActorKey) {
            printf("pocketufo: campaign fire: FAIL selected actor changed before the pinned outside-plane shot\n");
            fflush(stdout);
            return false;
        }
        for (i32 i = 0; i < nTriedActors; ++i) {
            if (aTriedActorKeys[i] == nActorKey) {
                bRosterWrapped = true;
                break;
            }
        }
        if (bRosterWrapped) { break; }
        aTriedActorKeys[nTriedActors++] = nActorKey;

        pCandidate = App_ScriptCampaignFindActiveXcom(pMap, nActorKey);
        if (pCandidate == NULL) {
            printf("pocketufo: campaign fire skip: slot %d actor 0x%llx reason inactive tu -1 "
                   "minOfferedCost -1\n",
                   (int)nRosterSlot, (unsigned long long)nActorKey);
            fflush(stdout);
            bTryNext = true;
        } else if (pPlane != NULL
                   && (pCandidate->pCell == NULL
                       || App_ScriptCampaignCellInsidePlane(
                           pPlane, (i32)pCandidate->pCell->nX,
                           (i32)pCandidate->pCell->nY))) {
            if (pExpectedFire != NULL) {
                printf("pocketufo: campaign fire: FAIL pinned owner is inside the plane after its move\n");
                fflush(stdout);
                return false;
            }
            printf("pocketufo: campaign fire skip: slot %d actor 0x%llX reason inside_plane "
                   "cell %d/%d/%d footprint %d..%d/%d..%d before hand input\n",
                   (int)nRosterSlot, (unsigned long long)nActorKey,
                   pCandidate->pCell != NULL ? (int)pCandidate->pCell->nX : -1,
                   pCandidate->pCell != NULL ? (int)pCandidate->pCell->nY : -1,
                   pCandidate->pCell != NULL ? (int)pCandidate->pCell->nZ : -1,
                   (int)pPlane->nMinX, (int)pPlane->nMaxX,
                   (int)pPlane->nMinY, (int)pPlane->nMaxY);
            fflush(stdout);
            bTryNext = true;
        } else {
            eFind = App_ScriptCampaignFireFindLoadedRifle(pMap, pCandidate, &sCandidate);
            if (eFind == APP_CAMPAIGN_FIRE_FIND_INVALID) {
                printf("pocketufo: campaign fire: FAIL invalid live rifle or XCOM link at slot %d\n",
                       (int)nRosterSlot);
                fflush(stdout);
                return false;
            }
            if (eFind == APP_CAMPAIGN_FIRE_FIND_NONE) {
                if (pExpectedFire != NULL) {
                    printf("pocketufo: campaign fire: FAIL pinned actor lost its loaded Rifle 4\n");
                    fflush(stdout);
                    return false;
                }
                printf("pocketufo: campaign fire skip: slot %d actor 0x%llx reason no_loaded_rifle "
                       "tu %u minOfferedCost -1\n",
                       (int)nRosterSlot, (unsigned long long)nActorKey,
                       (unsigned)pCandidate->uTu);
                fflush(stdout);
                ++nNoRifle;
                bTryNext = true;
            } else {
                if (pPlane != NULL) {
                    AppCampaignOutsideTarget sPrecheck = { 0 };
                    const AppCampaignOutsideTargetResult eTarget =
                        App_ScriptCampaignPickOutsideTarget(pMap, pQueue, pCandidate,
                                                            nRosterSlot, "precheck",
                                                            &sPrecheck);

                    if (eTarget == APP_CAMPAIGN_OUTSIDE_TARGET_BLOCKED
                        || eTarget == APP_CAMPAIGN_OUTSIDE_TARGET_FAILED) {
                        return false;
                    }
                    if (eTarget == APP_CAMPAIGN_OUTSIDE_TARGET_NONE) {
                        ++nNoVisible;
                        if (pExpectedFire != NULL) {
                            if (ppszOutsideApproachReason != NULL) {
                                *ppszOutsideApproachReason = "no_visible_target";
                            }
                            printf("pocketufo: campaign fire: no usable outside action after pinned owner search "
                                   "tried %d/%d actors noLoadedRifle %d noAffordableMode %d "
                                   "noVisibleTarget %d reason no_visible_target\n",
                                   (int)nTriedActors, (int)nRosterCount, (int)nNoRifle,
                                   (int)nNoAffordable, (int)nNoVisible);
                            fflush(stdout);
                            return false;
                        }
                        bTryNext = true;
                    }
                }
                if (!bTryNext) {
                    const char *pszGateReason = NULL;
                    const u16 uTuBeforeMenu = pCandidate->uTu;

                    if (pExpectedFire != NULL
                        && (sCandidate.nActorKey != pExpectedFire->nActorKey
                            || sCandidate.nWeaponKey != pExpectedFire->nWeaponKey
                            || sCandidate.nRosterSlot != pExpectedFire->nRosterSlot
                            || sCandidate.nPoolIndex != pExpectedFire->nPoolIndex
                            || sCandidate.nHandWidget != pExpectedFire->nHandWidget
                            || sCandidate.nItemIndex != pExpectedFire->nItemIndex
                            || sCandidate.nAmmo != pExpectedFire->nAmmo)) {
                        printf("pocketufo: campaign fire: FAIL pinned owner or Rifle 4 key changed after move\n");
                        fflush(stdout);
                        return false;
                    }

                    if (!App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, &sCandidate, true,
                                                       &pMap, &pQueue, &pUnit, &pWeapon)
                        || g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
                        || g_world.bMapPlayerInput == 0 || pMap->tact.bBusy != 0
                        || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
                        printf("pocketufo: campaign fire: FAIL owner or idle gates changed before hand input\n");
                        fflush(stdout);
                        return false;
                    }
                    if (pPlane != NULL && (pUnit->pCell == NULL
                        || App_ScriptCampaignCellInsidePlane(
                            pPlane, (i32)pUnit->pCell->nX, (i32)pUnit->pCell->nY))) {
                        printf("pocketufo: campaign fire: FAIL actor entered plane before hand input\n");
                        fflush(stdout);
                        return false;
                    }
                    if (!App_ScriptCampaignFireNativeMenuGate(pMap, pUnit, pWeapon,
                                                              &pszGateReason)) {
                    if (pPlane != NULL && ppszOutsideApproachReason != NULL) {
                        *ppszOutsideApproachReason = "native_menu_gate";
                        printf("pocketufo: campaign fire: no usable outside action after bounded rifle search "
                               "tried %d/%d actors noLoadedRifle %d noAffordableMode %d "
                               "noVisibleTarget %d reason native_menu_gate\n",
                               (int)nTriedActors, (int)nRosterCount, (int)nNoRifle,
                               (int)nNoAffordable, (int)nNoVisible);
                        fflush(stdout);
                        return false;
                    }
                        printf("pocketufo: campaign fire: FAIL native hand-menu gate before input: %s\n",
                               (pszGateReason != NULL) ? pszGateReason : "unknown gate");
                        fflush(stdout);
                        return false;
                    }
                    if (g_bank.aCtl[sCandidate.nHandWidget].bEnabled == 0
                        || g_bank.aCtl[sCandidate.nHandWidget].pfnClick == NULL) {
                        printf("pocketufo: campaign fire: FAIL hand widget %d is not a live control\n",
                               (int)sCandidate.nHandWidget);
                        fflush(stdout);
                        return false;
                    }
                    printf("pocketufo: campaign fire candidate: try %d/%d slot %d actor 0x%llx "
                           "handWidget %d weapon 0x%llx pool %d itemIndex %u ammo %u tu %u\n",
                           (int)nTriedActors, (int)nRosterCount, (int)sCandidate.nRosterSlot,
                           (unsigned long long)sCandidate.nActorKey, (int)sCandidate.nHandWidget,
                           (unsigned long long)sCandidate.nWeaponKey, (int)sCandidate.nPoolIndex,
                           (unsigned)sCandidate.nItemIndex, (unsigned)sCandidate.nAmmo,
                           (unsigned)pUnit->uTu);
                    fflush(stdout);
                    if (!App_ScriptCampaignClickBank(sCandidate.nHandWidget)) {
                        printf("pocketufo: campaign fire: FAIL hand widget %d did not accept pointer input\n",
                               (int)sCandidate.nHandWidget);
                        fflush(stdout);
                        return false;
                    }
                    if (g_world.nUiScreen == 3u) {
                        if (!App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, &sCandidate, true,
                                                           &pMap, &pQueue, &pUnit, &pWeapon)
                            || g_world.nUiScreen != 3u || pMap->pSelWeapon != pWeapon
                            || pUnit->uTu != uTuBeforeMenu) {
                            printf("pocketufo: campaign fire: FAIL hand menu opened with changed owners or weapon\n");
                            fflush(stdout);
                            return false;
                        }
                        sFire = sCandidate;
                        bMenuOpen = true;
                        printf("pocketufo: campaign fire menu: slot %d actor 0x%llx widget %d screen 3\n",
                               (int)sFire.nRosterSlot, (unsigned long long)sFire.nActorKey,
                               (int)sFire.nHandWidget);
                        fflush(stdout);
                        break;
                    }
                    if (g_world.nUiScreen != 0u
                        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                        || !App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, &sCandidate, true,
                                                          &pMap, &pQueue, &pUnit, &pWeapon)
                        || pMap->pSelWeapon != pWeapon || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                        || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0
                        || pQueue->bQueueActive != 0 || g_world.sModal.bOpen != 0
                        || g_world.bMapPlayerInput == 0 || pUnit->uTu != uTuBeforeMenu) {
                        printf("pocketufo: campaign fire: FAIL unexpected no-menu state after hand click "
                               "(screen %u)\n", (unsigned)g_world.nUiScreen);
                        fflush(stdout);
                        return false;
                    }
                    if (!App_ScriptCampaignFireNativeMenuGate(pMap, pUnit, pWeapon,
                                                              &pszGateReason)) {
                        if (pPlane != NULL && ppszOutsideApproachReason != NULL) {
                            *ppszOutsideApproachReason = "native_menu_gate";
                            printf("pocketufo: campaign fire: no usable outside action after bounded rifle search "
                                   "tried %d/%d actors noLoadedRifle %d noAffordableMode %d "
                                   "noVisibleTarget %d reason native_menu_gate\n",
                                   (int)nTriedActors, (int)nRosterCount, (int)nNoRifle,
                                   (int)nNoAffordable, (int)nNoVisible);
                            fflush(stdout);
                            return false;
                        }
                        printf("pocketufo: campaign fire: FAIL no-menu hand gate changed after input: %s\n",
                               (pszGateReason != NULL) ? pszGateReason : "unknown gate");
                        fflush(stdout);
                        return false;
                    }
                    {
                        i32 nOffered = 0, nAffordable = 0, nMinOfferedCost = -1;

                        /* The callback passed every raw hand-menu gate above, calls
                         * Ui_FillActionCosts, and syncs its tables out even when it
                         * returns screen 0. Read that current table; do not guess TU. */
                        App_ScriptCampaignFireCostSummary((i32)pUnit->uTu, &nOffered,
                                                          &nAffordable, &nMinOfferedCost);

                        if (nOffered == 0) {
                            printf("pocketufo: campaign fire: FAIL live hand callback offered no action modes "
                                   "(slot %d actor 0x%llx tu %u)\n",
                                   (int)sCandidate.nRosterSlot,
                                   (unsigned long long)sCandidate.nActorKey, (unsigned)pUnit->uTu);
                            fflush(stdout);
                            return false;
                        }
                        if (nAffordable != 0 || nMinOfferedCost <= (i32)pUnit->uTu) {
                            printf("pocketufo: campaign fire: FAIL hand menu stayed closed despite affordable "
                                   "mode (slot %d actor 0x%llx tu %u offered %d affordable %d minCost %d)\n",
                                   (int)sCandidate.nRosterSlot,
                                   (unsigned long long)sCandidate.nActorKey, (unsigned)pUnit->uTu,
                                   (int)nOffered, (int)nAffordable, (int)nMinOfferedCost);
                            fflush(stdout);
                            return false;
                        }
                        printf("pocketufo: campaign fire skip: no_affordable_mode try %d/%d slot %d "
                               "actor 0x%llx handWidget %d weapon 0x%llx pool %d itemIndex %u "
                               "ammo %u tu %u offered %d affordable %d minOfferedCost %d\n",
                               (int)nTriedActors, (int)nRosterCount, (int)sCandidate.nRosterSlot,
                               (unsigned long long)sCandidate.nActorKey, (int)sCandidate.nHandWidget,
                               (unsigned long long)sCandidate.nWeaponKey, (int)sCandidate.nPoolIndex,
                               (unsigned)sCandidate.nItemIndex, (unsigned)pWeapon->nAmmo,
                               (unsigned)pUnit->uTu, (int)nOffered, (int)nAffordable,
                               (int)nMinOfferedCost);
                        fflush(stdout);
                        ++nNoAffordable;
                        bTryNext = true;
                    }
                }
            }
        }

        if (!bTryNext) { break; }
        if (pExpectedFire != NULL) {
            if (pPlane != NULL && nNoAffordable > 0) {
                if (ppszOutsideApproachReason != NULL) {
                    *ppszOutsideApproachReason = "unaffordable";
                }
                printf("pocketufo: campaign fire: no usable outside action after pinned owner search "
                       "tried %d/%d actors noLoadedRifle %d noAffordableMode %d "
                       "noVisibleTarget %d reason unaffordable\n",
                       (int)nTriedActors, (int)nRosterCount, (int)nNoRifle,
                       (int)nNoAffordable, (int)nNoVisible);
                fflush(stdout);
                return false;
            }
            printf("pocketufo: campaign fire: FAIL pinned outside-plane owner cannot start Snap fire\n");
            fflush(stdout);
            return false;
        }
        if (nTriedActors >= nRosterCount) { break; }
        {
            const uintptr_t nPreviousActorKey = nActorKey;

            if (g_world.nUiScreen != 0u
                || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
                || pMap->nXcomCount != nRosterCount || pMap->tact.bBusy != 0
                || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
                printf("pocketufo: campaign fire: FAIL cannot cycle roster from a live idle state\n");
                fflush(stdout);
                return false;
            }
            if (!App_ScriptCampaignClickBank(0)) {
                printf("pocketufo: campaign fire: FAIL live widget 0 could not cycle to another rifle owner\n");
                fflush(stdout);
                return false;
            }
            if (g_world.nUiScreen != 0u
                || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || g_world.sModal.bOpen != 0 || g_world.bMapPlayerInput == 0
                || pMap->nXcomCount != nRosterCount || pMap->tact.bBusy != 0
                || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
                || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
                printf("pocketufo: campaign fire: FAIL tactical owners or phase changed after widget 0\n");
                fflush(stdout);
                return false;
            }
            if ((uintptr_t)(void *)pMap->pSelUnit == nPreviousActorKey) {
                printf("pocketufo: campaign fire: FAIL widget 0 did not select a new roster actor\n");
                fflush(stdout);
                return false;
            }
        }
    }
    if (!bMenuOpen) {
        if (pPlane != NULL) {
            const char *pszReason = (nNoVisible > 0 && nNoAffordable > 0)
                ? "no_visible_and_unaffordable"
                : (nNoVisible > 0 ? "no_visible_target"
                                  : (nNoAffordable > 0 ? "unaffordable" : NULL));

            if (ppszOutsideApproachReason != NULL) {
                *ppszOutsideApproachReason = pszReason;
            }
            printf("pocketufo: campaign fire: no usable outside action after bounded rifle search "
                   "tried %d/%d actors noLoadedRifle %d noAffordableMode %d "
                   "noVisibleTarget %d reason %s\n",
                   (int)nTriedActors, (int)nRosterCount, (int)nNoRifle,
                   (int)nNoAffordable, (int)nNoVisible,
                   pszReason != NULL ? pszReason : "no_target_or_action");
        } else {
            printf("pocketufo: campaign fire: FAIL bounded rifle search tried %d/%d actors, "
                   "noLoadedRifle %d noAffordableMode %d\n",
                   (int)nTriedActors, (int)nRosterCount, (int)nNoRifle, (int)nNoAffordable);
        }
        fflush(stdout);
        return false;
    }

    if (g_world.sTactUi.anActionCost[PUFO_ACTION_MODE_SNAP] <= 0
        || g_world.sTactUi.anActionCost[PUFO_ACTION_MODE_SNAP] > (i32)pUnit->uTu
        || (pExpectedFire != NULL
            && (nExpectedSnapCost <= 0
                || g_world.sTactUi.anActionCost[PUFO_ACTION_MODE_SNAP] != nExpectedSnapCost))
        || g_bank.aCtl[33].bEnabled == 0) {
        printf("pocketufo: campaign fire: FAIL snap mode or quoted cost changed: cost %d expected %d enabled %u tu %u\n",
               (int)g_world.sTactUi.anActionCost[PUFO_ACTION_MODE_SNAP],
               (int)nExpectedSnapCost,
               (unsigned)g_bank.aCtl[33].bEnabled, (unsigned)pUnit->uTu);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignClickBank(33)) {
        printf("pocketufo: campaign fire: FAIL live snap-mode control rejected pointer input\n");
        fflush(stdout);
        return false;
    }
    if (g_world.nUiScreen != 0u
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || !App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, &sFire, true,
                                          &pMap, &pQueue, &pUnit, &pWeapon)
        || g_world.nMapActionMode != PUFO_ACTION_MODE_SNAP
        || pMap->bActionLatch != PUFO_ACTION_LATCH_BURST) {
        printf("pocketufo: campaign fire: FAIL snap input changed owners, mode, latch, or screen %u\n",
               (unsigned)g_world.nUiScreen);
        fflush(stdout);
        return false;
    }
    if (pPlane != NULL && (pUnit->pCell == NULL
        || App_ScriptCampaignCellInsidePlane(pPlane,
                                              (i32)pUnit->pCell->nX,
                                              (i32)pUnit->pCell->nY))) {
        printf("pocketufo: campaign fire: FAIL actor is inside plane after selecting Snap\n");
        fflush(stdout);
        return false;
    }

    if (pPlane != NULL) {
        AppCampaignOutsideTarget sTarget = { 0 };
        const AppCampaignOutsideTargetResult eTarget =
            App_ScriptCampaignPickOutsideTarget(pMap, pQueue, pUnit, sFire.nRosterSlot,
                                                "commit", &sTarget);

        if (eTarget != APP_CAMPAIGN_OUTSIDE_TARGET_READY) {
            return false;
        }
        nTargetKey = sTarget.nKey;
        nTargetX = sTarget.nX;
        nTargetY = sTarget.nY;
        nTargetZ = sTarget.nZ;
        if (!App_ScriptCampaignFindCellPixel(nTargetX, nTargetY, nTargetZ,
                                             &nPixelX, &nPixelY)) {
            printf("pocketufo: campaign outside-plane fire decision: phase commit actor 0x%llX "
                   "roster %d target 0x%llX targetRoster %d cell %d/%d/%d distance %d "
                   "range 1 los 1 visible 1 selected 0 actionGate 1 reason not_in_pen_view\n",
                   (unsigned long long)sFire.nActorKey, (int)sFire.nRosterSlot,
                   (unsigned long long)sTarget.nKey, (int)sTarget.nRosterSlot,
                   sTarget.nX, sTarget.nY, sTarget.nZ, (int)sTarget.nDistance);
            fflush(stdout);
            return false;
        }
    } else {
        const i32 nEnemyCount = (i32)pMap->nEnemyCount;

        for (i32 i = 0; i < nEnemyCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
            Soldier *pCandidate = pMap->apEnemyUnits[i];
            uintptr_t nCandidateKey;
            i32 nCandidateX, nCandidateY, nCandidateZ;
            i32 nCandidatePixelX = -1, nCandidatePixelY = -1;
            bool bInPen;

            if (pCandidate == NULL || pCandidate->pCell == NULL || pCandidate->uHp == 0
                || pCandidate->bStatus >= UNIT_STATUS_INACTIVE) {
                continue;
            }
            nCandidateKey = (uintptr_t)(void *)pCandidate;
            nCandidateX = (i32)pCandidate->pCell->nX;
            nCandidateY = (i32)pCandidate->pCell->nY;
            nCandidateZ = (i32)pCandidate->pCell->nZ;
            bInPen = App_ScriptCampaignFindCellPixel(nCandidateX, nCandidateY, nCandidateZ,
                                                      &nCandidatePixelX, &nCandidatePixelY);
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || !App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, &sFire, true,
                                                  &pMap, &pQueue, &pUnit, &pWeapon)) {
                printf("pocketufo: campaign fire: FAIL owner or rifle changed during target projection\n");
                fflush(stdout);
                return false;
            }
            if (!bInPen) { continue; }
            pCandidate = App_ScriptCampaignFindActiveEnemy(pMap, nCandidateKey);
            if (pCandidate == NULL || pCandidate->pCell == NULL
                || pCandidate->pCell->nX != nCandidateX || pCandidate->pCell->nY != nCandidateY
                || pCandidate->pCell->nZ != nCandidateZ) {
                continue;
            }
            nTargetKey = nCandidateKey;
            nTargetX = nCandidateX;
            nTargetY = nCandidateY;
            nTargetZ = nCandidateZ;
            nPixelX = nCandidatePixelX;
            nPixelY = nCandidatePixelY;
            break;
        }
    }
    if (nTargetKey == 0 || nPixelX < 0) {
        printf("pocketufo: campaign fire: FAIL (no live enemy cell entered the pen view)\n");
        fflush(stdout);
        return false;
    }
    if (pPlane != NULL) {
        Soldier *pTarget = App_ScriptCampaignFindActiveEnemy(pMap, nTargetKey);

        if (!App_ScriptCampaignOutsideInputGateClear(pMap, pQueue)
            || pTarget == NULL || pTarget->pCell == NULL
            || pTarget->pCell->nX != nTargetX || pTarget->pCell->nY != nTargetY
            || pTarget->pCell->nZ != nTargetZ) {
            printf("pocketufo: campaign outside-plane fire decision: phase commit actor 0x%llX "
                   "roster %d target 0x%llX targetRoster %d cell %d/%d/%d distance -1 "
                   "range 0 los -1 visible 0 selected 0 actionGate %d reason live_gate_or_target_changed\n",
                   (unsigned long long)sFire.nActorKey, (int)sFire.nRosterSlot,
                   (unsigned long long)nTargetKey, -1, nTargetX, nTargetY, nTargetZ,
                   App_ScriptCampaignOutsideInputGateClear(pMap, pQueue) ? 1 : 0);
            fflush(stdout);
            return false;
        }
    }
    {
        Soldier *pTarget = App_ScriptCampaignFindActiveEnemy(pMap, nTargetKey);
        nTargetHpBefore = pTarget != NULL ? (i32)pTarget->uHp : -1;
    }
    uAmmo0 = pWeapon->nAmmo;
    uTu0 = pUnit->uTu;
    nQueueBefore = (i32)pQueue->nQueueLen;
    if (pPlane != NULL) {
        printf("pocketufo: campaign fire outside-plane before: actorKey 0x%llX roster %d "
               "cell %d/%d/%d weaponKey 0x%llX rifle 4 itemIndex %u footprint %d..%d/%d..%d "
               "targetKey 0x%llX requested %d/%d/%d queue %d ammo %u tu %u targetHp %d\n",
               (unsigned long long)sFire.nActorKey, (int)sFire.nRosterSlot,
               (int)pUnit->pCell->nX, (int)pUnit->pCell->nY, (int)pUnit->pCell->nZ,
               (unsigned long long)sFire.nWeaponKey, (unsigned)sFire.nItemIndex,
               (int)pPlane->nMinX, (int)pPlane->nMaxX,
               (int)pPlane->nMinY, (int)pPlane->nMaxY,
               (unsigned long long)nTargetKey, (int)nTargetX, (int)nTargetY, (int)nTargetZ,
               nQueueBefore, (unsigned)uAmmo0, (unsigned)uTu0, nTargetHpBefore);
        fflush(stdout);
    }
    App_ScriptCampaignFireWitness(nMapKey, nQueueKey, "before_pointer_input", &sFire,
                                  nTargetKey, nTargetX, nTargetY, nTargetZ);
    if (!App_ScriptCampaignPressCellPixel(nPixelX, nPixelY)) {
        printf("pocketufo: campaign fire: stopped during map input at terminal screen %u or changed owners\n",
               (unsigned)g_world.nUiScreen);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || !App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, &sFire, true,
                                          &pMap, &pQueue, &pUnit, &pWeapon)) {
        printf("pocketufo: campaign fire: FAIL owner or rifle changed after map target input\n");
        fflush(stdout);
        return false;
    }
    {
        const i32 nQueueCapacity = (i32)(sizeof(pQueue->aQueue) / sizeof(pQueue->aQueue[0]));
        if (pQueue->nQueueLen < 0 || pQueue->nQueueLen > nQueueCapacity) {
            printf("pocketufo: campaign fire: FAIL live queue length is out of range\n");
            fflush(stdout);
            return false;
        }
    }
    for (i32 i = 0; i < (i32)pQueue->nQueueLen; ++i) {
        if (pQueue->aQueue[i].nCmd == TACTCMD_FIRE_SHOT
            && pQueue->aQueue[i].pUnit == pUnit && pQueue->aQueue[i].pWeapon == pWeapon) {
            ++nShotsQueued;
        }
    }
    if (nShotsQueued == 0) {
        printf("pocketufo: campaign fire: FAIL (pointer input queued no shot for the live actor and rifle)\n");
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignDrainQueue(1600)) {
        printf("pocketufo: campaign fire: FAIL (no completed shot command)\n");
        fflush(stdout);
        return false;
    }
    App_ScriptCampaignFireWitness(nMapKey, nQueueKey, "after_queue_drain", &sFire,
                                  nTargetKey, nTargetX, nTargetY, nTargetZ);
    if (!App_ScriptCampaignFireRefresh(nMapKey, nQueueKey, &sFire, false,
                                       &pMap, &pQueue, &pUnit, &pWeapon)) {
        printf("pocketufo: campaign fire: FAIL owner or rifle changed after shot queue drain\n");
        fflush(stdout);
        return false;
    }
    {
        Soldier *pTarget = App_ScriptCampaignFindActiveEnemy(pMap, nTargetKey);
        const u32 uTargetHp = (pTarget != NULL) ? (u32)pTarget->uHp : 0u;
        const bool bSpent = pWeapon->nAmmo < uAmmo0 && pUnit->uTu < uTu0;

        if (pPlane != NULL) {
            printf("pocketufo: campaign fire outside-plane after: actorKey 0x%llX cell %d/%d/%d "
                   "targetKey 0x%llX targetHpBefore %d targetHpAfter %d targetLive %d "
                   "ammo %u->%u tu %u->%u queue %d/%u result %s\n",
                   (unsigned long long)sFire.nActorKey,
                   pUnit->pCell != NULL ? (int)pUnit->pCell->nX : -1,
                   pUnit->pCell != NULL ? (int)pUnit->pCell->nY : -1,
                   pUnit->pCell != NULL ? (int)pUnit->pCell->nZ : -1,
                   (unsigned long long)nTargetKey, nTargetHpBefore,
                   pTarget != NULL ? (int)pTarget->uHp : -1, pTarget != NULL ? 1 : 0,
                   (unsigned)uAmmo0, (unsigned)pWeapon->nAmmo,
                   (unsigned)uTu0, (unsigned)pUnit->uTu,
                   (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive,
                   pTarget != NULL && nTargetHpBefore >= 0
                       && (i32)pTarget->uHp < nTargetHpBefore ? "hp-loss" : "no-hp-loss");
            fflush(stdout);
        }

        printf("pocketufo: campaign fire: rifle 4 ammo %u -> %u, tu %u -> %u, target %d/%d/%d, "
               "queue %d -> %d, shots %d, target hp %u\n",
               (unsigned)uAmmo0, (unsigned)pWeapon->nAmmo, (unsigned)uTu0, (unsigned)pUnit->uTu,
               (int)nTargetX, (int)nTargetY, (int)nTargetZ, (int)nQueueBefore,
               (int)pQueue->nQueueLen, (int)nShotsQueued, (unsigned)uTargetHp);
        fflush(stdout);
        if (!bSpent || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0
            || pMap->tact.bBusy != 0) {
            printf("pocketufo: campaign fire: FAIL (live shot did not drain and spend ammo/TU)\n");
            fflush(stdout);
            return false;
        }
    }
    return true;
}

static bool App_ScriptCampaignArmThrowTargeting(uintptr_t nMapKey, uintptr_t nQueueKey,
                                                uintptr_t nActorKey, uintptr_t nGrenadeKey,
                                                i32 nPoolIndex, i32 nRosterSlot,
                                                u16 nItemIndex, u8 nAmmo)
{
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit = NULL;
    TactItem *pGrenade = NULL;

    if (g_world.nUiScreen != 0u
        || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                             nPoolIndex, nRosterSlot, nItemIndex, nAmmo,
                                             &pMap, &pQueue, &pUnit, &pGrenade)
        || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)) {
        return false;
    }
    if (pMap->bActionLatch == PUFO_ACTION_LATCH_THROW
        && g_world.nMapActionMode == PUFO_ACTION_MODE_MENU
        && pMap->pSelWeapon == pGrenade) {
        return true;
    }
    if (pMap->bActionLatch != PUFO_ACTION_LATCH_NONE) {
        if (!App_ScriptCampaignKey(PUFO_KEY_CONFIRM)
            || g_world.nUiScreen != 0u
            || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                                 nPoolIndex, nRosterSlot, nItemIndex, nAmmo,
                                                 &pMap, &pQueue, &pUnit, &pGrenade)
            || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)
            || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE) {
            return false;
        }
    }
    if (!App_ScriptCampaignClickBank(18)
        || g_world.nUiScreen != 3u
        || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                             nPoolIndex, nRosterSlot, nItemIndex, nAmmo,
                                             &pMap, &pQueue, &pUnit, &pGrenade)
        || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)
        || pMap->pSelWeapon != pGrenade) {
        return false;
    }
    if (!App_ScriptCampaignClickBank(43) || g_world.nUiScreen != 0u
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                             nPoolIndex, nRosterSlot, nItemIndex, nAmmo,
                                             &pMap, &pQueue, &pUnit, &pGrenade)) {
        return false;
    }
    if (!App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)
        || pMap->bActionLatch != PUFO_ACTION_LATCH_THROW) {
        return false;
    }
    printf("pocketufo: campaign throw: targeting rearmed through widget43, mode %d latch %u\n",
           (int)g_world.nMapActionMode, (unsigned)pMap->bActionLatch);
    fflush(stdout);
    return true;
}

static bool App_ScriptCampaignThrow(void)
{
    static const i8 aViewOffset[10][2] = {
        { 0, 0 }, { 0, 2 }, { 0, 4 }, { -8, 0 }, { 8, 0 },
        { -8, 4 }, { 8, 4 }, { -8, -8 }, { 8, -8 }, { 0, 8 }
    };
    AppCampaignCellPick aPick[256];
    u8 abTriedCell[PUFO_TERRAIN_CELL_SLOTS] = { 0 };
    const uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    const uintptr_t nActorKey = g_nCampaignGrenadeActorKey;
    const uintptr_t nGrenadeKey = g_nCampaignGrenadeItemKey;
    const i32 nGrenadePoolIndex = g_nCampaignGrenadePoolIndex;
    const i32 nGrenadeRosterSlot = g_nCampaignGrenadeRosterSlot;
    const u16 nGrenadeItemIndex = g_uCampaignGrenadeItemIndex;
    const u8 nGrenadeAmmo = g_uCampaignGrenadeAmmo;
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    Soldier *pUnit = NULL;
    TactItem *pGrenade = NULL;
    i32 nPickCount;
    i32 nTargetX = 0, nTargetY = 0, nTargetZ = 0;
    i32 nPrimeCost, nThrowCost, nThrowArg, nTriedCount = 0;
    i32 nAttempts = 0, nRangeSkips = 0, nViewCount = 0;
    u16 uTuBeforePrime, uTuAfterPrime, uTuBeforeThrow;
    ThrowBudget sBudget;
    double dBudget;
    bool bThrown = false;

    if (nActorKey == 0 || nGrenadeKey == 0 || nGrenadePoolIndex < 0
        || g_world.nUiScreen != 0
        || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                             nGrenadePoolIndex, nGrenadeRosterSlot,
                                             nGrenadeItemIndex, nGrenadeAmmo,
                                             &pMap, &pQueue, &pUnit, &pGrenade)
        || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0
        || pQueue->bQueueActive != 0) {
        printf("pocketufo: campaign throw: FAIL (saved grenade, owner, or idle queue is not live)\n");
        fflush(stdout);
        return false;
    }
    if (pMap->nXcomCount > PUFO_MAP_ROSTER_MAX) {
        printf("pocketufo: campaign throw: FAIL (live XCOM roster count is out of range)\n");
        fflush(stdout);
        return false;
    }
    for (i32 nTry = 0; nTry < (i32)pMap->nXcomCount
                       && pMap->pSelUnit != pUnit; ++nTry) {
        if (!App_ScriptCampaignClickBank(0)
            || g_world.nUiScreen != 0u
            || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                                 nGrenadePoolIndex, nGrenadeRosterSlot,
                                                 nGrenadeItemIndex, nGrenadeAmmo,
                                                 &pMap, &pQueue, &pUnit, &pGrenade)) {
            printf("pocketufo: campaign throw: FAIL (live grenade owner changed while cycling rows)\n");
            fflush(stdout);
            return false;
        }
    }
    if (pMap->pSelUnit != pUnit || (uintptr_t)(void *)pUnit->pHandR != nGrenadeKey
        || pGrenade->bUnk8 != 0 || pGrenade->bUnk7 != 0) {
        printf("pocketufo: campaign throw: FAIL (selected owner does not hold the same clean grenade)\n");
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign throw source: key 0x%llx pool %d ownerSlot %d index %u ammo %u armed %u fuse %u\n",
           (unsigned long long)nGrenadeKey, (int)nGrenadePoolIndex,
           (int)nGrenadeRosterSlot, (unsigned)nGrenadeItemIndex,
           (unsigned)nGrenadeAmmo, (unsigned)pGrenade->bUnk8, (unsigned)pGrenade->bUnk7);
    fflush(stdout);
    uTuBeforePrime = pUnit->uTu;
    if (!App_ScriptCampaignClickBank(18) || g_world.nUiScreen != 3u
        || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                             nGrenadePoolIndex, nGrenadeRosterSlot,
                                             nGrenadeItemIndex, nGrenadeAmmo,
                                             &pMap, &pQueue, &pUnit, &pGrenade)
        || pMap->pSelUnit != pUnit || pMap->pSelWeapon != pGrenade
        || (uintptr_t)(void *)pUnit->pHandR != nGrenadeKey
        || pGrenade->bUnk8 != 0 || pGrenade->bUnk7 != 0) {
        printf("pocketufo: campaign throw: FAIL hand menu did not select the saved grenade (screen %u)\n",
               (unsigned)g_world.nUiScreen);
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign throw: menu costs mode3 %d enabled %d mode11 %d enabled %d tu %u armed %u fuse %u\n",
           (int)g_world.sTactUi.anActionCost[3], (int)g_world.sTactUi.anActionEnabled[3],
           (int)g_world.sTactUi.anActionCost[11], (int)g_world.sTactUi.anActionEnabled[11],
           (unsigned)pUnit->uTu, (unsigned)pGrenade->bUnk8, (unsigned)pGrenade->bUnk7);
    fflush(stdout);
    nPrimeCost = g_world.sTactUi.anActionCost[3];
    if (!App_ScriptCampaignClickBank(35) || g_world.nUiScreen != 5u
        || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                             nGrenadePoolIndex, nGrenadeRosterSlot,
                                             nGrenadeItemIndex, nGrenadeAmmo,
                                             &pMap, &pQueue, &pUnit, &pGrenade)
        || pMap->pSelUnit != pUnit
        || (uintptr_t)(void *)pUnit->pHandR != nGrenadeKey
        || pGrenade->bUnk8 != 0 || pGrenade->bUnk7 != 0) {
        printf("pocketufo: campaign throw: FAIL item action click did not fire\n");
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign throw: after mode3 screen %u mode %d cost %d enabled %d tu %u armed %u fuse %u\n",
           (unsigned)g_world.nUiScreen, (int)g_world.nMapActionMode,
           (int)g_world.sTactUi.anActionCost[3], (int)g_world.sTactUi.anActionEnabled[3],
           (unsigned)pUnit->uTu, (unsigned)pGrenade->bUnk8, (unsigned)pGrenade->bUnk7);
    fflush(stdout);
    if (g_world.nUiScreen != 5u) {
        printf("pocketufo: campaign throw: FAIL mode3 did not open the native item sub-screen\n");
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignClickBank(44)) {
        printf("pocketufo: campaign throw: FAIL real prime control did not accept pointer input\n");
        fflush(stdout);
        return false;
    }
    if (g_world.nUiScreen != 0u
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
        printf("pocketufo: campaign throw: FAIL prime callback did not return to the live tactical screen (screen %u)\n",
               (unsigned)g_world.nUiScreen);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                          nGrenadePoolIndex, nGrenadeRosterSlot,
                                          nGrenadeItemIndex, nGrenadeAmmo,
                                          &pMap, &pQueue, &pUnit, &pGrenade)) {
        printf("pocketufo: campaign throw: FAIL owner or grenade identity changed after real prime callback\n");
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)) {
        printf("pocketufo: campaign throw: FAIL prime did not change the same held grenade from unarmed to armed (armed %u fuse %u handMatch %d)\n",
               (unsigned)pGrenade->bUnk8, (unsigned)pGrenade->bUnk7,
               (uintptr_t)(void *)pUnit->pHandR == nGrenadeKey ? 1 : 0);
        fflush(stdout);
        return false;
    }
    uTuAfterPrime = pUnit->uTu;
    printf("pocketufo: campaign throw: after arm screen %u mode %d tu %u armed %u fuse %u primeCost %d spent %d\n",
           (unsigned)g_world.nUiScreen, (int)g_world.nMapActionMode,
           (unsigned)uTuAfterPrime, (unsigned)pGrenade->bUnk8, (unsigned)pGrenade->bUnk7,
           (int)nPrimeCost, (int)(uTuBeforePrime - uTuAfterPrime));
    fflush(stdout);
    if (nPrimeCost <= 0 || uTuBeforePrime < uTuAfterPrime
        || (i32)uTuBeforePrime - (i32)uTuAfterPrime != nPrimeCost) {
        printf("pocketufo: campaign throw: FAIL prime TU mismatch\n");
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignClickBank(18) || g_world.nUiScreen != 3u
        || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                             nGrenadePoolIndex, nGrenadeRosterSlot,
                                             nGrenadeItemIndex, nGrenadeAmmo,
                                             &pMap, &pQueue, &pUnit, &pGrenade)
        || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)
        || pMap->pSelWeapon != pGrenade) {
        printf("pocketufo: campaign throw: FAIL reopened hand menu (screen %u)\n",
               (unsigned)g_world.nUiScreen);
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign throw: armed menu costs mode3 %d enabled %d mode11 %d enabled %d tu %u\n",
           (int)g_world.sTactUi.anActionCost[3], (int)g_world.sTactUi.anActionEnabled[3],
           (int)g_world.sTactUi.anActionCost[11], (int)g_world.sTactUi.anActionEnabled[11],
           (unsigned)pUnit->uTu);
    fflush(stdout);
    nThrowCost = g_world.sTactUi.anActionCost[11];
    nThrowArg = g_world.sTactUi.anActionEnabled[11];
    if (nThrowCost <= 0 || nThrowCost > (i32)pUnit->uTu || nThrowArg <= 0) {
        printf("pocketufo: campaign throw: FAIL mode11 cost/eligibility %d/%d with TU %u\n",
               (int)nThrowCost, (int)nThrowArg, (unsigned)pUnit->uTu);
        fflush(stdout);
        return false;
    }
    sBudget.nStatStrength = pMap->pSelUnit->auStat[PUFO_STAT_STRENGTH];
    sBudget.nStatStamina = pMap->pSelUnit->auStat[PUFO_STAT_STAMINA];
    sBudget.nStunOrArmor = pMap->pSelUnit->uStunOrArmor;
    sBudget.nWeight = pGrenade->pObj->bWeight;
    dBudget = Throw_RangeBudget(&sBudget, NULL, NULL);
    printf("pocketufo: campaign throw: holder %d at %d/%d/%d selectedMatch %d stats %d/%d stunArmor %d weight %d "
           "budget %.2f mode11 cost %d enabled %d\n",
           (int)pUnit->nSaveId, (int)pUnit->pCell->nX, (int)pUnit->pCell->nY,
           (int)pUnit->pCell->nZ, pMap->pSelUnit == pUnit,
           (int)sBudget.nStatStrength, (int)sBudget.nStatStamina, (int)sBudget.nStunOrArmor,
           (int)sBudget.nWeight, dBudget, (int)nThrowCost, (int)nThrowArg);
    fflush(stdout);
    if (!App_ScriptCampaignClickBank(43) || g_world.nUiScreen != 0u
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                             nGrenadePoolIndex, nGrenadeRosterSlot,
                                             nGrenadeItemIndex, nGrenadeAmmo,
                                             &pMap, &pQueue, &pUnit, &pGrenade)
        || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)
        || pMap->bActionLatch != PUFO_ACTION_LATCH_THROW) {
        printf("pocketufo: campaign throw: FAIL mode11 release or owner refresh (screen %u)\n",
               (unsigned)g_world.nUiScreen);
        fflush(stdout);
        return false;
    }
    if (!App_ScriptCampaignClickBank(9) || g_world.nUiScreen != 0u
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey, nGrenadeKey,
                                             nGrenadePoolIndex, nGrenadeRosterSlot,
                                             nGrenadeItemIndex, nGrenadeAmmo,
                                             &pMap, &pQueue, &pUnit, &pGrenade)
        || pMap->pSelUnit != pUnit
        || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)) {
        printf("pocketufo: campaign throw: FAIL selected-unit focus did not accept input\n");
        fflush(stdout);
        return false;
    }
    {
        const i32 aTargetLayer[2] = { (i32)pUnit->pCell->nZ, 0 };

        for (i32 l = 0; l < 2 && !bThrown; ++l) {
            const i32 nTargetLayer = aTargetLayer[l];

            if (l != 0 && nTargetLayer == aTargetLayer[0]) {
                continue;
            }
            while ((i32)g_world.tactEnv.cursor.nFlags < nTargetLayer + 1) {
                if (!App_ScriptCampaignClickBank(2) || g_world.nUiScreen != 0u
                    || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                    || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                         nGrenadeKey, nGrenadePoolIndex,
                                                         nGrenadeRosterSlot, nGrenadeItemIndex,
                                                         nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                         &pGrenade)
                    || pMap->pSelUnit != pUnit
                    || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)) {
                    printf("pocketufo: campaign throw: FAIL after live layer-up input\n");
                    fflush(stdout);
                    return false;
                }
            }
            while ((i32)g_world.tactEnv.cursor.nFlags > nTargetLayer + 1) {
                if (!App_ScriptCampaignClickBank(7) || g_world.nUiScreen != 0u
                    || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                    || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                         nGrenadeKey, nGrenadePoolIndex,
                                                         nGrenadeRosterSlot, nGrenadeItemIndex,
                                                         nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                         &pGrenade)
                    || pMap->pSelUnit != pUnit
                    || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)) {
                    printf("pocketufo: campaign throw: FAIL after live layer-down input\n");
                    fflush(stdout);
                    return false;
                }
            }
            for (i32 v = 0; v < 10 && !bThrown; ++v) {
                const i32 nViewX = (i32)pUnit->pCell->nX + (i32)aViewOffset[v][0];
                const i32 nViewY = (i32)pUnit->pCell->nY + (i32)aViewOffset[v][1];

                if (!App_ScriptCampaignArmThrowTargeting(nMapKey, nQueueKey, nActorKey,
                                                         nGrenadeKey, nGrenadePoolIndex,
                                                         nGrenadeRosterSlot, nGrenadeItemIndex,
                                                         nGrenadeAmmo)
                    || !App_ScriptCampaignPanToCell(nViewX, nViewY)
                    || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                         nGrenadeKey, nGrenadePoolIndex,
                                                         nGrenadeRosterSlot, nGrenadeItemIndex,
                                                         nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                         &pGrenade)
                    || pMap->pSelUnit != pUnit
                    || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)) {
                    printf("pocketufo: campaign throw: FAIL two-axis layer %d view %d at %d/%d\n",
                           (int)nTargetLayer, (int)v, (int)nViewX, (int)nViewY);
                    fflush(stdout);
                    return false;
                }
                ++nViewCount;
                nPickCount = App_ScriptCampaignCollectNearbyCells(pUnit, aPick,
                                                                   (i32)(sizeof(aPick) / sizeof(aPick[0])),
                                                                   20, nTargetLayer, false,
                                                                   NULL, 0, NULL);
                if (!App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                       nGrenadeKey, nGrenadePoolIndex,
                                                       nGrenadeRosterSlot, nGrenadeItemIndex,
                                                       nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                       &pGrenade)
                    || pMap->pSelUnit != pUnit
                    || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)) {
                    printf("pocketufo: campaign throw: FAIL after live target scan\n");
                    fflush(stdout);
                    return false;
                }

                for (i32 k = 0; k < nPickCount && !bThrown; ++k) {
            for (i32 j = k + 1; j < nPickCount; ++j) {
                if (aPick[j].nDistance < aPick[k].nDistance) {
                    const AppCampaignCellPick t = aPick[k];
                    aPick[k] = aPick[j];
                    aPick[j] = t;
                }
            }
            nTargetX = aPick[k].nX;
            nTargetY = aPick[k].nY;
            nTargetZ = aPick[k].nZ;
            {
                const i32 nCellIndex = nTargetZ * PUFO_TERRAIN_CELLS_PER_LAYER
                                     + nTargetY * PUFO_MAP_CELL_DIM + nTargetX;

                if (nCellIndex < 0 || nCellIndex >= PUFO_TERRAIN_CELL_SLOTS) {
                    continue;
                }
                if (abTriedCell[nCellIndex] != 0) {
                    continue;
                }
                abTriedCell[nCellIndex] = 1;
                ++nTriedCount;
            }
            {
                const i32 nX0 = Map_CellCentreX(pUnit->pCell);
                const i32 nY0 = Map_CellCentreY(pUnit->pCell);
                const i32 nZ0 = 12 * (i32)pUnit->pCell->nZ
                              + Map_CeilingLift(pUnit->pCell) + 6;
                const i32 nX1 = 16 * nTargetX + 8;
                const i32 nY1 = 16 * nTargetY + 8;
                const i32 nZ1 = 12 * nTargetZ;
                const bool bNearJitter = abs(nX1 - nX0) < 3 && abs(nY1 - nY0) < 3;
                double dRange = sqrt((double)(nX1 - nX0) * (double)(nX1 - nX0)
                                   + (double)(nY1 - nY0) * (double)(nY1 - nY0))
                              * PUFO_THROW_RANGE_SCALE;

                /* Raw 0x31CD0..0x31CD8 rejects a range over the selected unit's budget
                 * before solving the arc. Skip only that deterministic case here. A
                 * same-column close target keeps the original random jitter and is sent
                 * through the actual pointer path below. */
                if (nZ1 >= nZ0) {
                    dRange *= (double)((nZ1 - nZ0) / PUFO_THROW_Z_DIV);
                }
                if (!bNearJitter && dRange > dBudget) {
                    if (nRangeSkips < 12) {
                        printf("pocketufo: campaign throw range skip: target %d/%d/%d range %.2f budget %.2f\n",
                               (int)nTargetX, (int)nTargetY, (int)nTargetZ, dRange, dBudget);
                        fflush(stdout);
                    }
                    ++nRangeSkips;
                    continue;
                }
                printf("pocketufo: campaign throw target: %d/%d/%d pen %d/%d distance %d range %.2f budget %.2f%s\n",
                       (int)nTargetX, (int)nTargetY, (int)nTargetZ,
                       (int)aPick[k].nScreenX, (int)aPick[k].nScreenY,
                       (int)aPick[k].nDistance, dRange, dBudget,
                       bNearJitter ? " close-jitter" : "");
                fflush(stdout);
            }

            if (!App_ScriptCampaignArmThrowTargeting(nMapKey, nQueueKey, nActorKey,
                                                     nGrenadeKey, nGrenadePoolIndex,
                                                     nGrenadeRosterSlot, nGrenadeItemIndex,
                                                     nGrenadeAmmo)
                || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                     nGrenadeKey, nGrenadePoolIndex,
                                                     nGrenadeRosterSlot, nGrenadeItemIndex,
                                                     nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                     &pGrenade)
                || pMap->pSelUnit != pUnit
                || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)) {
                printf("pocketufo: campaign throw: FAIL latch could not be armed for target %d/%d/%d\n",
                       (int)nTargetX, (int)nTargetY, (int)nTargetZ);
                fflush(stdout);
                return false;
            }
            if (!App_ScriptCampaignPressCellPixel(aPick[k].nScreenX, aPick[k].nScreenY)) {
                printf("pocketufo: campaign throw: stopped during map input at terminal screen %u or changed owners\n",
                       (unsigned)g_world.nUiScreen);
                fflush(stdout);
                return false;
            }
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || !App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                     nGrenadeKey, nGrenadePoolIndex,
                                                     nGrenadeRosterSlot, nGrenadeItemIndex,
                                                     nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                     &pGrenade)
                || pMap->pSelUnit != pUnit
                || !App_ScriptCampaignGrenadeHeldBy(pMap, pUnit, pGrenade, nGrenadeKey)) {
                printf("pocketufo: campaign throw: FAIL after map target input\n");
                fflush(stdout);
                return false;
            }
            ++nAttempts;
            {
                TactQueueEntry *pEntry = NULL;
                i32 nEntryIndex = -1;
                const i32 nQueueCapacity = (i32)(sizeof(pQueue->aQueue)
                                                  / sizeof(pQueue->aQueue[0]));

                if (pQueue->nQueueLen < 0 || pQueue->nQueueLen > nQueueCapacity) {
                    printf("pocketufo: campaign throw: FAIL live queue length is out of range\n");
                    fflush(stdout);
                    return false;
                }

                for (i32 q = 0; q < (i32)pQueue->nQueueLen; ++q) {
                    if (pQueue->aQueue[q].nCmd == TACTCMD_THROW
                        && pQueue->aQueue[q].pWeapon == pGrenade
                        && pQueue->aQueue[q].pUnit == pUnit) {
                        pEntry = &pQueue->aQueue[q];
                        nEntryIndex = q;
                        break;
                    }
                }
                if (pEntry == NULL) {
                    printf("pocketufo: campaign throw target: no CMD17 entry at %d/%d/%d latch %u screen %u\n",
                           (int)nTargetX, (int)nTargetY, (int)nTargetZ,
                           (unsigned)pMap->bActionLatch, (unsigned)g_world.nUiScreen);
                    fflush(stdout);
                    continue;
                }
                printf("pocketufo: campaign throw queued: entry %d target %d/%d/%d cost %d arg %d expected %d/%d actor %d weapon %d\n",
                       (int)nEntryIndex, (int)pEntry->nX, (int)pEntry->nY, (int)pEntry->nZ,
                       (int)pEntry->nTuCost, (int)pEntry->nArg, (int)nThrowCost, (int)nThrowArg,
                       pEntry->pUnit == pUnit, pEntry->pWeapon == pGrenade);
                fflush(stdout);
                if (pEntry->nTuCost != nThrowCost || pEntry->nArg != nThrowArg) {
                    printf("pocketufo: campaign throw: FAIL live CMD17 cost/arg mismatch\n");
                    fflush(stdout);
                    return false;
                }
            }

            uTuBeforeThrow = pUnit->uTu;
            if (!App_ScriptCampaignDrainQueue(1600)) {
                printf("pocketufo: campaign throw: FAIL queue did not drain after target %d/%d/%d\n",
                       (int)nTargetX, (int)nTargetY, (int)nTargetZ);
                fflush(stdout);
                return false;
            }
            if (!App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                  nGrenadeKey, nGrenadePoolIndex,
                                                  nGrenadeRosterSlot, nGrenadeItemIndex,
                                                  nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                  &pGrenade)) {
                printf("pocketufo: campaign throw: FAIL owner or grenade changed after queue drain\n");
                fflush(stdout);
                return false;
            }
            {
                const bool bHandReleased = pUnit->pHandL != pGrenade && pUnit->pHandR != pGrenade;
                const i32 nThrowSpent = (i32)uTuBeforeThrow - (i32)pUnit->uTu;
                MapCell *pLanding = App_ScriptCampaignFindItemCellKey(nGrenadeKey,
                                                                       g_assets.apObjects[17]);

                printf("pocketufo: campaign throw result: target %d/%d/%d attempt %d cost %d tu %u->%u spent %d "
                       "handReleased %d landing %d/%d/%d rejected %d landed %d hitStop %d travel %.2f bound %.2f "
                       "fireArg5 %d latch %u mode %d arrow %d pending %u\n",
                       (int)nTargetX, (int)nTargetY, (int)nTargetZ, (int)nAttempts, (int)nThrowCost,
                       (unsigned)uTuBeforeThrow, (unsigned)pUnit->uTu, (int)nThrowSpent,
                       bHandReleased,
                       pLanding != NULL ? (int)pLanding->nX : -1,
                       pLanding != NULL ? (int)pLanding->nY : -1,
                       pLanding != NULL ? (int)pLanding->nZ : -1,
                       (int)pMap->throwLanding.bRejected, (int)pMap->throwLanding.bLanded,
                       (int)pMap->throwLanding.bHitStop, pMap->throwLanding.dTravel,
                       pMap->throwArc.dBound, (int)pMap->nFireArg5,
                       (unsigned)pMap->bActionLatch, (int)g_world.nMapActionMode,
                       pMap->pArrowThrownItem == pGrenade, (unsigned)pMap->bTrajHitStop);
                fflush(stdout);

                if (bHandReleased && nThrowSpent >= nThrowCost) {
                    for (i32 nTick = 0; nTick < 1600 && pLanding == NULL; ++nTick) {
                        if (!App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                              nGrenadeKey, nGrenadePoolIndex,
                                                              nGrenadeRosterSlot, nGrenadeItemIndex,
                                                              nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                              &pGrenade)) {
                            printf("pocketufo: campaign throw: FAIL owner or grenade changed before landing tick\n");
                            fflush(stdout);
                            return false;
                        }
                        Timer_TickBySpeed((i32)g_world.gameClock.nSpeed, App_GameTickOnce, NULL);
                        if (!App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                              nGrenadeKey, nGrenadePoolIndex,
                                                              nGrenadeRosterSlot, nGrenadeItemIndex,
                                                              nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                              &pGrenade)) {
                            printf("pocketufo: campaign throw: FAIL owner or grenade changed after landing tick\n");
                            fflush(stdout);
                            return false;
                        }
                        App_DrawFrame();
                        if (!App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                                              nGrenadeKey, nGrenadePoolIndex,
                                                              nGrenadeRosterSlot, nGrenadeItemIndex,
                                                              nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                                              &pGrenade)) {
                            printf("pocketufo: campaign throw: FAIL owner or grenade changed after landing paint\n");
                            fflush(stdout);
                            return false;
                        }
                        pLanding = App_ScriptCampaignFindItemCellKey(nGrenadeKey,
                                                                     g_assets.apObjects[17]);
                    }
                    if (pLanding != NULL) {
                        i32 nHandRSlot = -1;

                        if (pUnit->pHandR != NULL) {
                            const uintptr_t nHandKey = (uintptr_t)(void *)pUnit->pHandR;
                            const i32 nHandPool = App_ScriptCampaignGrenadePoolIndex(nHandKey);
                            if (nHandPool < 0 || g_world.apTactItems[nHandPool] != pUnit->pHandR) {
                                printf("pocketufo: campaign throw: FAIL right-hand item is not a live pool member\n");
                                fflush(stdout);
                                return false;
                            }
                            nHandRSlot = App_ScriptCampaignObjectSlot(
                                g_world.apTactItems[nHandPool]->pObj);
                        }
                        if (pGrenade->bUnk8 != 1 || pGrenade->bUnk7 != 0
                            || pLanding == NULL) {
                            printf("pocketufo: campaign throw: FAIL landed item is not armed and live\n");
                            fflush(stdout);
                            return false;
                        }
                        printf("pocketufo: campaign throw: command 17 grenade 17 at %d/%d/%d, armed %d fuse %u, "
                               "handR %d, prime %u->%u throw %u->%u cost %d landed cell %d/%d/%d\n",
                               (int)nTargetX, (int)nTargetY, (int)nTargetZ,
                               (int)pGrenade->bUnk8, (unsigned)pGrenade->bUnk7,
                               nHandRSlot,
                               (unsigned)uTuBeforePrime, (unsigned)uTuAfterPrime,
                               (unsigned)uTuBeforeThrow, (unsigned)pUnit->uTu, (int)nThrowCost,
                               (int)pLanding->nX, (int)pLanding->nY, (int)pLanding->nZ);
                        fflush(stdout);
                        printf("pocketufo: campaign throw identity: key 0x%llx pool %d index %u ammo %u->%u armed %u fuse %u cell %d/%d/%d\n",
                               (unsigned long long)nGrenadeKey, (int)nGrenadePoolIndex,
                               (unsigned)pGrenade->nIndex, (unsigned)nGrenadeAmmo,
                               (unsigned)pGrenade->nAmmo, (unsigned)pGrenade->bUnk8,
                               (unsigned)pGrenade->bUnk7, (int)pLanding->nX,
                               (int)pLanding->nY, (int)pLanding->nZ);
                        fflush(stdout);
                        g_nCampaignGrenadeItemKey = 0;
                        g_nCampaignGrenadeActorKey = 0;
                        g_nCampaignGrenadePoolIndex = -1;
                        g_nCampaignGrenadeRosterSlot = -1;
                        g_uCampaignGrenadeItemIndex = 0;
                        g_uCampaignGrenadeAmmo = 0;
                        bThrown = true;
                        break;
                    }
                    printf("pocketufo: campaign throw: FAIL item left hand and cost was paid but no ground-list cell appeared\n");
                    fflush(stdout);
                    return false;
                }
            }
        }
    }
    }
    }

    if (!bThrown) {
        if (!App_ScriptCampaignGrenadeRefresh(nMapKey, nQueueKey, nActorKey,
                                              nGrenadeKey, nGrenadePoolIndex,
                                              nGrenadeRosterSlot, nGrenadeItemIndex,
                                              nGrenadeAmmo, &pMap, &pQueue, &pUnit,
                                              &pGrenade)) {
            printf("pocketufo: campaign throw: FAIL owner or grenade changed before terminal report\n");
            fflush(stdout);
            return false;
        }
        printf("pocketufo: campaign throw: FAIL after %d views %d unique targets %d range skips; "
               "holder %d/%d/%d TU after prime %u, hand %d, latch %u mode %d\n",
               (int)nViewCount, (int)nTriedCount, (int)nRangeSkips,
               (int)pUnit->pCell->nX, (int)pUnit->pCell->nY, (int)pUnit->pCell->nZ,
               (unsigned)pUnit->uTu,
               pUnit->pHandR == pGrenade || pUnit->pHandL == pGrenade,
               (unsigned)pMap->bActionLatch, (int)g_world.nMapActionMode);
        fflush(stdout);
    }
    return bThrown;
}

static void App_ScriptCaptureCurrentFrame(const char *pszName)
{
    WCHAR awcPath[MAX_PATH];

    (void)MultiByteToWideChar(CP_ACP, 0, pszName, -1, awcPath, MAX_PATH - 1);
    awcPath[MAX_PATH - 1] = L'\0';
    App_DumpFramePpm(awcPath);
    printf("pocketufo: campaign frame capture %s\n", pszName);
    fflush(stdout);
}

static bool App_ScriptCampaignSettleEnemyTurnFade(uintptr_t nMapKey,
                                                   uintptr_t nQueueKey)
{
    const i32 nFadeLimit = 12;
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    i32 nFrames = 0;

    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
        || g_world.nUiScreen != 0 || g_world.bMapPlayerInput == 0
        || g_world.bMapEnemyTurnActive != 0 || pMap->tact.bBusy != 0
        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
        printf("pocketufo: campaign enemyturn fade: FAIL (need settled live player input)\n");
        fflush(stdout);
        return false;
    }

    printf("pocketufo: campaign enemyturn fade start counter=%d step=%d dir=%u/%u "
           "busy=%u track=%u phase=%u input=%u queue=%u/%u active=%u\n",
           (int)g_world.nMapUiSlot10, (int)g_world.nMapUiSlot9,
           (unsigned)g_world.bMapSlot8Byte2, (unsigned)g_world.bMapSlot8Byte3,
           (unsigned)pMap->tact.bBusy, pMap->tact.pTrackUnit != NULL ? 1u : 0u,
           (unsigned)pQueue->bPhaseActive, (unsigned)g_world.bMapPlayerInput,
           (unsigned)pQueue->nQueueCursor, (unsigned)pQueue->nQueueLen,
           (unsigned)pQueue->bQueueActive);
    App_ScriptCaptureCurrentFrame("normal_ground_play_after_enemyturn_fade_before.ppm");

    while (g_world.nMapUiSlot10 != 10 && nFrames < nFadeLimit) {
        const i32 nCounterBefore = (i32)g_world.nMapUiSlot10;
        const i32 nStepBefore = (i32)g_world.nMapUiSlot9;
        const u8 bDirection2Before = g_world.bMapSlot8Byte2;
        const u8 bDirection3Before = g_world.bMapSlot8Byte3;

        App_DrawFrame(); /* World_TactFrameDraw runs only the original cursor-direction paint tick. */
        ++nFrames;
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || g_world.nUiScreen != 0 || g_world.bMapPlayerInput == 0
            || g_world.bMapEnemyTurnActive != 0 || pMap->tact.bBusy != 0
            || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
            printf("pocketufo: campaign enemyturn fade: FAIL (paint changed owners or player gates)\n");
            fflush(stdout);
            return false;
        }
        printf("pocketufo: campaign enemyturn fade frame=%d counter=%d->%d step=%d->%d "
               "dir=%u/%u->%u/%u timer=0 gameTick=0 enemyAI=0\n",
               (int)nFrames, (int)nCounterBefore, (int)g_world.nMapUiSlot10,
               (int)nStepBefore, (int)g_world.nMapUiSlot9,
               (unsigned)bDirection2Before, (unsigned)bDirection3Before,
               (unsigned)g_world.bMapSlot8Byte2, (unsigned)g_world.bMapSlot8Byte3);
        fflush(stdout);
    }

    if (g_world.nMapUiSlot10 != 10) {
        printf("pocketufo: campaign enemyturn fade: FAIL counter=%d step=%d dir=%u/%u "
               "frames=%d bound=%d\n",
               (int)g_world.nMapUiSlot10, (int)g_world.nMapUiSlot9,
               (unsigned)g_world.bMapSlot8Byte2, (unsigned)g_world.bMapSlot8Byte3,
               (int)nFrames, (int)nFadeLimit);
        fflush(stdout);
        return false;
    }
    App_ScriptCaptureCurrentFrame("normal_ground_play_after_enemyturn_fade_settled.ppm");
    printf("pocketufo: campaign enemyturn fade: SETTLED frames=%d counter=%d step=%d "
           "dir=%u/%u atPlayerInput=1 homeArrival=not-applicable\n",
           (int)nFrames, (int)g_world.nMapUiSlot10, (int)g_world.nMapUiSlot9,
           (unsigned)g_world.bMapSlot8Byte2, (unsigned)g_world.bMapSlot8Byte3);
    fflush(stdout);
    return true;
}

static void App_ScriptCampaignCountEnemyTurnGameTick(void *pUser)
{
    i32 *pnCallbackCount = (i32 *)pUser;

    if (pnCallbackCount != NULL) { ++*pnCallbackCount; }
    App_GameTickOnce(NULL);
}

/* Tact_EnemyAiTick advances Soldier.nAiTick before its no-TU/no-move branches. */
static bool App_ScriptCampaignEnemyAiTickChanged(const GameMap *pMap,
                                                  const uintptr_t *aEnemyKeys,
                                                  const i32 *anAiTickBefore,
                                                  i32 nEnemyBefore)
{
    if (pMap == NULL || aEnemyKeys == NULL || anAiTickBefore == NULL
        || pMap->nEnemyCount > PUFO_MAP_ROSTER_MAX) {
        return false;
    }
    for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
        const Soldier *pEnemy = pMap->apEnemyUnits[i];
        const uintptr_t nEnemyKey = (uintptr_t)(const void *)pEnemy;
        bool bFound = false;

        if (pEnemy == NULL) { continue; }
        for (i32 j = 0; j < nEnemyBefore; ++j) {
            if (aEnemyKeys[j] != nEnemyKey) { continue; }
            bFound = true;
            if (pEnemy->nAiTick != anAiTickBefore[j]) { return true; }
            break;
        }
        if (!bFound && pEnemy->nAiTick != 0) { return true; }
    }
    return false;
}

static bool App_ScriptCampaignEnemyTurn(bool bRequireDetonation,
                                        bool bRequireTurnWitness)
{
    GameMap *pMap = g_world.pTactMap;
    TactState *pQueue = g_world.pTactState;
    const ObjectRecFull *pGrenadeObj = (g_assets.apObjects != NULL && g_assets.nObjectCount > 17)
                                           ? g_assets.apObjects[17] : NULL;
    uintptr_t nMapKey = 0, nQueueKey = 0;
    uintptr_t nArmedItemKey = 0;
    MapCell *pArmedCell = NULL;
    i32 nPrimedBefore = 0, nPrimedAfter = 0;
    i32 nEnemyMoves = 0, nEnemyHpChanged = 0, nXcomHpChanged = 0, nTick = 0;
    i32 aX[PUFO_MAP_ROSTER_MAX], aY[PUFO_MAP_ROSTER_MAX];
    i32 aEnemyAiTick[PUFO_MAP_ROSTER_MAX];
    u16 aHp[PUFO_MAP_ROSTER_MAX];
    uintptr_t aEnemyKey[PUFO_MAP_ROSTER_MAX];
    i32 nEnemyBefore = 0;
    uintptr_t aXcomKey[PUFO_MAP_ROSTER_MAX];
    u16 aXcomHp[PUFO_MAP_ROSTER_MAX];
    i32 nXcomBefore = 0, nGameTickCallbacks = 0;
    bool bTerminalScreen13 = false;
    bool bTurnStartObserved = false, bEnemyAiTickChanged = false;
    bool bTurnProgressObserved = false, bTickWitnessLogged = false;
    i32 nPhaseCounterStart = -1;

    if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
        || g_world.nUiScreen != 0 || g_world.bMapPlayerInput == 0
        || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
        printf("pocketufo: campaign enemyturn: FAIL (need idle player input)\n");
        fflush(stdout);
        return false;
    }
    nMapKey = (uintptr_t)(void *)pMap;
    nQueueKey = (uintptr_t)(void *)pQueue;
    if (bRequireDetonation) {
        nPrimedBefore = App_ScriptCampaignFindPrimedGrenades(pGrenadeObj,
                                                             &nArmedItemKey, &pArmedCell);
        if (nPrimedBefore != 1 || nArmedItemKey == 0 || pArmedCell == NULL) {
            printf("pocketufo: campaign detonation: FAIL expected one armed ground object17, found %d\n",
                   (int)nPrimedBefore);
            fflush(stdout);
            return false;
        }
        printf("pocketufo: campaign detonation: armed object17 fuse0 at %d/%d/%d, unique %d\n",
               (int)pArmedCell->nX, (int)pArmedCell->nY, (int)pArmedCell->nZ,
               (int)nPrimedBefore);
        fflush(stdout);
    }
    for (i32 i = 0; i < (i32)pMap->nEnemyCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
        const Soldier *pE = pMap->apEnemyUnits[i];
        if (pE == NULL) { continue; }
        aEnemyKey[nEnemyBefore] = (uintptr_t)(const void *)pE;
        aX[nEnemyBefore] = (pE->pCell != NULL) ? pE->pCell->nX : -999;
        aY[nEnemyBefore] = (pE->pCell != NULL) ? pE->pCell->nY : -999;
        aHp[nEnemyBefore] = pE->uHp;
        aEnemyAiTick[nEnemyBefore] = pE->nAiTick;
        ++nEnemyBefore;
    }
    for (i32 i = 0; i < (i32)pMap->nXcomCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
        const Soldier *pX = pMap->apXcomUnits[i];
        if (pX == NULL) { continue; }
        aXcomKey[nXcomBefore] = (uintptr_t)(const void *)pX;
        aXcomHp[nXcomBefore] = pX->uHp;
        ++nXcomBefore;
    }
    if (!App_ScriptCampaignClickBank(5)) { return false; }
    printf("pocketufo: campaign enemyturn: started enemy %d input %d\n",
           (int)g_world.bMapEnemyTurnActive, (int)g_world.bMapPlayerInput);
    if (bRequireTurnWitness) {
        pMap = g_world.pTactMap;
        pQueue = g_world.pTactState;
        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
            printf("pocketufo: campaign enemyturn witness: FAIL stage=click owner changed\n");
            fflush(stdout);
            return false;
        }
        bTurnStartObserved = g_world.bMapEnemyTurnActive != 0
            && pMap->tact.bBusy != 0 && g_world.bMapPlayerInput == 0
            && pMap->tact.nPhase == PUFO_PHASE_ENEMY_AI;
        bEnemyAiTickChanged = App_ScriptCampaignEnemyAiTickChanged(
            pMap, aEnemyKey, aEnemyAiTick, nEnemyBefore);
        bTurnProgressObserved = bEnemyAiTickChanged;
        if (!bTurnStartObserved && !bEnemyAiTickChanged) {
            printf("pocketufo: campaign enemyturn witness: FAIL stage=click-no-turn-edge "
                   "enemy %u input %u busy %u phase %d queue %d/%u screen %u aiTickChanged 0\n",
                   (unsigned)g_world.bMapEnemyTurnActive,
                   (unsigned)g_world.bMapPlayerInput, (unsigned)pMap->tact.bBusy,
                   (int)pMap->tact.nPhase, (int)pQueue->nQueueLen,
                   (unsigned)pQueue->bQueueActive, (unsigned)g_world.nUiScreen);
            fflush(stdout);
            return false;
        }
        nPhaseCounterStart = pQueue->nPhaseCounter;
        printf("pocketufo: campaign enemyturn witness: start active %u busy %u input %u "
               "phase %d phaseCounter %d aiTickChanged %d\n",
               (unsigned)g_world.bMapEnemyTurnActive, (unsigned)pMap->tact.bBusy,
               (unsigned)g_world.bMapPlayerInput, (int)pMap->tact.nPhase,
               (int)nPhaseCounterStart, bEnemyAiTickChanged ? 1 : 0);
        fflush(stdout);
    }

    /* The click and every timer/draw callback may end a mission. Do not carry
     * either live owner through one of those callbacks. */
    pMap = NULL;
    pQueue = NULL;
    for (;;) {
        if (g_world.nUiScreen == 18u) {
            if (g_world.bTactLive != 0 || g_world.pTactMap != NULL || g_world.pTactState != NULL) {
                printf("pocketufo: campaign enemyturn: FAIL (screen 18 left unexpected tactical owners live)\n");
                fflush(stdout);
                return false;
            }
            if (bRequireTurnWitness && !bTurnProgressObserved) {
                printf("pocketufo: campaign enemyturn witness: FAIL stage=screen18-before-game-tick "
                       "steps %d callbacks %d startEdge %d aiTickChanged %d\n",
                       (int)nTick, (int)nGameTickCallbacks, bTurnStartObserved ? 1 : 0,
                       bEnemyAiTickChanged ? 1 : 0);
                fflush(stdout);
                return false;
            }
            printf("pocketufo: campaign enemyturn: terminal screen 18 battle %u victory %u ticks %d\n",
                   (unsigned)g_world.bMapBattleEnded, (unsigned)g_world.bMapMissionVictory, (int)nTick);
            if (bRequireTurnWitness) {
                printf("pocketufo: campaign enemyturn witness: terminal screen 18 steps %d "
                       "callbacks %d aiTickChanged %d turnProgress 1\n",
                       (int)nTick, (int)nGameTickCallbacks, bEnemyAiTickChanged ? 1 : 0);
                fflush(stdout);
            }
            fflush(stdout);
            if (bRequireDetonation) {
                printf("pocketufo: campaign detonation: FAIL cannot observe item result after tactical teardown\n");
                fflush(stdout);
                return false;
            }
            return true;
        }
        if (g_world.nUiScreen == 13u && g_world.bMapBattleEnded != 0) {
            pMap = g_world.pTactMap;
            pQueue = g_world.pTactState;
            if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
                || (uintptr_t)(void *)pMap != nMapKey || (uintptr_t)(void *)pQueue != nQueueKey) {
                printf("pocketufo: campaign enemyturn: FAIL (screen 13 ended with missing or replaced tactical owners)\n");
                fflush(stdout);
                return false;
            }
            bTerminalScreen13 = true;
            break;
        }

        pMap = g_world.pTactMap;
        pQueue = g_world.pTactState;
        if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
            || (uintptr_t)(void *)pMap != nMapKey || (uintptr_t)(void *)pQueue != nQueueKey) {
            printf("pocketufo: campaign enemyturn: FAIL (tactical owners disappeared or changed outside screen 13/18)\n");
            fflush(stdout);
            return false;
        }
        if (pMap->tact.bBusy == 0 && g_world.bMapEnemyTurnActive == 0
            && pQueue->nQueueLen == 0 && pQueue->bQueueActive == 0) {
            if (bRequireTurnWitness && !bTurnProgressObserved) {
                printf("pocketufo: campaign enemyturn witness: FAIL stage=idle-before-game-tick "
                       "steps %d callbacks %d startEdge %d aiTickChanged %d enemy %u input %u\n",
                       (int)nTick, (int)nGameTickCallbacks, bTurnStartObserved ? 1 : 0,
                       bEnemyAiTickChanged ? 1 : 0,
                       (unsigned)g_world.bMapEnemyTurnActive,
                       (unsigned)g_world.bMapPlayerInput);
                fflush(stdout);
                return false;
            }
            break;
        }
        if (nTick >= 1600) {
            break;
        }

        pMap = NULL;
        pQueue = NULL;
        if (bRequireTurnWitness) {
            Timer_TickBySpeed((i32)g_world.gameClock.nSpeed,
                              App_ScriptCampaignCountEnemyTurnGameTick,
                              &nGameTickCallbacks);
        } else {
            Timer_TickBySpeed((i32)g_world.gameClock.nSpeed, App_GameTickOnce, NULL);
        }
        ++nTick;
        if (bRequireTurnWitness && bTurnStartObserved && nGameTickCallbacks != 0) {
            bTurnProgressObserved = true;
        }

        if (g_world.nUiScreen == 18u
            && g_world.bTactLive == 0 && g_world.pTactMap == NULL && g_world.pTactState == NULL) {
            continue;
        }
        if (g_world.nUiScreen == 13u && g_world.bMapBattleEnded != 0) {
            continue;
        }
        pMap = g_world.pTactMap;
        pQueue = g_world.pTactState;
        if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
            || (uintptr_t)(void *)pMap != nMapKey || (uintptr_t)(void *)pQueue != nQueueKey) {
            printf("pocketufo: campaign enemyturn: FAIL (tactical owners disappeared or changed during GameTick_OnTimer)\n");
            fflush(stdout);
            return false;
        }
        if (bRequireTurnWitness) {
            bEnemyAiTickChanged = bEnemyAiTickChanged
                || App_ScriptCampaignEnemyAiTickChanged(pMap, aEnemyKey,
                                                         aEnemyAiTick, nEnemyBefore);
            if (bEnemyAiTickChanged) { bTurnProgressObserved = true; }
        }
        if (bRequireTurnWitness && bTurnProgressObserved && !bTickWitnessLogged) {
            printf("pocketufo: campaign enemyturn witness: progress steps %d callbacks %d "
                   "aiTickChanged %d enemy %u busy %u phaseCounter %d\n",
                   (int)nTick, (int)nGameTickCallbacks, bEnemyAiTickChanged ? 1 : 0,
                   (unsigned)g_world.bMapEnemyTurnActive,
                   (unsigned)pMap->tact.bBusy, (int)pQueue->nPhaseCounter);
            fflush(stdout);
            bTickWitnessLogged = true;
        }
        pMap = NULL;
        pQueue = NULL;
        App_DrawFrame();

        if (g_world.nUiScreen == 18u
            && g_world.bTactLive == 0 && g_world.pTactMap == NULL && g_world.pTactState == NULL) {
            continue;
        }
        if (g_world.nUiScreen == 13u && g_world.bMapBattleEnded != 0) {
            continue;
        }
        pMap = g_world.pTactMap;
        pQueue = g_world.pTactState;
        if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
            || (uintptr_t)(void *)pMap != nMapKey || (uintptr_t)(void *)pQueue != nQueueKey) {
            printf("pocketufo: campaign enemyturn: FAIL (tactical owners disappeared or changed during draw)\n");
            fflush(stdout);
            return false;
        }
        pMap = NULL;
        pQueue = NULL;
    }

    if (bTerminalScreen13) {
        if (bRequireTurnWitness && !bTurnProgressObserved) {
            printf("pocketufo: campaign enemyturn witness: FAIL stage=screen13-before-game-tick "
                   "steps %d callbacks %d startEdge %d aiTickChanged %d\n",
                   (int)nTick, (int)nGameTickCallbacks, bTurnStartObserved ? 1 : 0,
                   bEnemyAiTickChanged ? 1 : 0);
            fflush(stdout);
            return false;
        }
        printf("pocketufo: campaign enemyturn: terminal screen 13 battle %u victory %u ticks %d\n",
               (unsigned)g_world.bMapBattleEnded, (unsigned)g_world.bMapMissionVictory, (int)nTick);
        if (bRequireTurnWitness) {
            printf("pocketufo: campaign enemyturn witness: terminal screen 13 steps %d "
                   "callbacks %d aiTickChanged %d turnProgress 1\n",
                   (int)nTick, (int)nGameTickCallbacks, bEnemyAiTickChanged ? 1 : 0);
            fflush(stdout);
        }
        fflush(stdout);
    } else {
        pMap = g_world.pTactMap;
        pQueue = g_world.pTactState;
        if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
            || (uintptr_t)(void *)pMap != nMapKey || (uintptr_t)(void *)pQueue != nQueueKey) {
            printf("pocketufo: campaign enemyturn: FAIL (tactical owners disappeared before result readback)\n");
            fflush(stdout);
            return false;
        }
    }
    for (i32 i = 0; i < (i32)pMap->nEnemyCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
        const Soldier *pE = pMap->apEnemyUnits[i];
        if (pE == NULL) { continue; }
        for (i32 j = 0; j < nEnemyBefore; ++j) {
            if (aEnemyKey[j] != (uintptr_t)(const void *)pE) { continue; }
            const i32 nX = (pE->pCell != NULL) ? pE->pCell->nX : -999;
            const i32 nY = (pE->pCell != NULL) ? pE->pCell->nY : -999;
            if (nX != aX[j] || nY != aY[j] || pE->uHp != aHp[j]) {
                ++nEnemyMoves;
            }
            if (pE->uHp != aHp[j]) {
                ++nEnemyHpChanged;
            }
            break;
        }
    }
    for (i32 i = 0; i < (i32)pMap->nXcomCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
        const Soldier *pX = pMap->apXcomUnits[i];
        if (pX == NULL) { continue; }
        for (i32 j = 0; j < nXcomBefore; ++j) {
            if (aXcomKey[j] == (uintptr_t)(const void *)pX) {
                if (pX->uHp != aXcomHp[j]) { ++nXcomHpChanged; }
                break;
            }
        }
    }
    if (bRequireDetonation) {
        const MapCell *pArmedAfter = App_ScriptCampaignFindItemCellKey(nArmedItemKey, pGrenadeObj);
        const bool bPointerGone = pArmedAfter == NULL;
        nPrimedAfter = App_ScriptCampaignFindPrimedGrenades(pGrenadeObj, NULL, NULL);
        const bool bOneShotDeviceConsumed = bPointerGone && nPrimedAfter == 0;

        printf("pocketufo: campaign detonation: result %s pointerGone %d armedRemaining %d "
               "enemyHpChanged %d xcomHpChanged %d after enemy tick %d\n",
               bOneShotDeviceConsumed ? "PASS" : "FAIL", bPointerGone ? 1 : 0,
               (int)nPrimedAfter, (int)nEnemyHpChanged, (int)nXcomHpChanged, (int)nTick);
        fflush(stdout);
        if (!bOneShotDeviceConsumed) {
            return false;
        }
    }
    if (bTerminalScreen13) {
        return true;
    }
    if (!App_ScriptCampaignSettleEnemyTurnFade(nMapKey, nQueueKey)
        || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
        return false;
    }
    if (bRequireTurnWitness) {
        if (!bTurnProgressObserved) {
            printf("pocketufo: campaign enemyturn witness: FAIL stage=completion steps %d "
                   "callbacks %d startEdge %d aiTickChanged %d\n",
                   (int)nTick, (int)nGameTickCallbacks, bTurnStartObserved ? 1 : 0,
                   bEnemyAiTickChanged ? 1 : 0);
            fflush(stdout);
            return false;
        }
        printf("pocketufo: campaign enemyturn witness: complete steps %d callbacks %d "
               "aiTickChanged %d turnProgress 1 phaseCounter %d->%d enemyMoves %d\n",
               (int)nTick, (int)nGameTickCallbacks, bEnemyAiTickChanged ? 1 : 0,
               (int)nPhaseCounterStart,
               (int)pQueue->nPhaseCounter, (int)nEnemyMoves);
        fflush(stdout);
    }
    printf("pocketufo: campaign enemyturn: ticks %d enemy %d input %d busy %d queue %d/%d changed %d\n",
           (int)nTick, (int)g_world.bMapEnemyTurnActive, (int)g_world.bMapPlayerInput,
           (int)pMap->tact.bBusy, (int)pQueue->nQueueCursor, (int)pQueue->nQueueLen, (int)nEnemyMoves);
    fflush(stdout);
    return g_world.bMapEnemyTurnActive == 0 && g_world.bMapPlayerInput != 0
        && pMap->tact.bBusy == 0 && pQueue->nQueueLen == 0 && pQueue->bQueueActive == 0;
}

typedef struct AppCampaignDefeatSnapshot {
    uintptr_t aXcomKey[PUFO_MAP_ROSTER_MAX];
    u16 aXcomHp[PUFO_MAP_ROSTER_MAX];
    uintptr_t aEnemyKey[PUFO_MAP_ROSTER_MAX];
    u16 aEnemyHp[PUFO_MAP_ROSTER_MAX];
    i32 nMissionType;
    i32 nXcomCount;
    i32 nEnemyCount;
    i32 nEnemyReserve;
    i32 nXcomSeen;
    i32 nEnemySeen;
} AppCampaignDefeatSnapshot;

static void App_ScriptCampaignDefeatSnapshot(const GameMap *pMap, i32 nTurn,
                                              const char *pszPhase,
                                              AppCampaignDefeatSnapshot *pSnapshot)
{
    pSnapshot->nMissionType = (i32)pMap->nMissionType;
    pSnapshot->nXcomCount = (i32)pMap->nXcomCount;
    pSnapshot->nEnemyCount = (i32)pMap->nEnemyCount;
    pSnapshot->nEnemyReserve = (i32)g_world.nMapEnemyReserveCount;
    pSnapshot->nXcomSeen = 0;
    pSnapshot->nEnemySeen = 0;

    printf("pocketufo: campaign natural-defeat snapshot turn %d phase %s mission %d "
           "xcom %d enemy %d reserve %d\n",
           (int)nTurn, pszPhase, (int)pSnapshot->nMissionType,
           (int)pSnapshot->nXcomCount, (int)pSnapshot->nEnemyCount,
           (int)pSnapshot->nEnemyReserve);
    for (i32 i = 0; i < pSnapshot->nXcomCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
        const Soldier *pUnit = pMap->apXcomUnits[i];
        if (pUnit == NULL) { continue; }
        pSnapshot->aXcomKey[pSnapshot->nXcomSeen] = (uintptr_t)(const void *)pUnit;
        pSnapshot->aXcomHp[pSnapshot->nXcomSeen] = pUnit->uHp;
        printf("pocketufo: campaign natural-defeat unit turn %d phase %s side xcom key 0x%llx hp %u\n",
               (int)nTurn, pszPhase,
               (unsigned long long)pSnapshot->aXcomKey[pSnapshot->nXcomSeen],
               (unsigned)pSnapshot->aXcomHp[pSnapshot->nXcomSeen]);
        ++pSnapshot->nXcomSeen;
    }
    for (i32 i = 0; i < pSnapshot->nEnemyCount && i < PUFO_MAP_ROSTER_MAX; ++i) {
        const Soldier *pUnit = pMap->apEnemyUnits[i];
        if (pUnit == NULL) { continue; }
        pSnapshot->aEnemyKey[pSnapshot->nEnemySeen] = (uintptr_t)(const void *)pUnit;
        pSnapshot->aEnemyHp[pSnapshot->nEnemySeen] = pUnit->uHp;
        printf("pocketufo: campaign natural-defeat unit turn %d phase %s side enemy key 0x%llx hp %u\n",
               (int)nTurn, pszPhase,
               (unsigned long long)pSnapshot->aEnemyKey[pSnapshot->nEnemySeen],
               (unsigned)pSnapshot->aEnemyHp[pSnapshot->nEnemySeen]);
        ++pSnapshot->nEnemySeen;
    }
    fflush(stdout);
}

static i32 App_ScriptCampaignDefeatCountMissing(const uintptr_t *pBeforeKey,
                                                const u16 *pBeforeHp, i32 nBefore,
                                                const uintptr_t *pAfterKey,
                                                const u16 *pAfterHp, i32 nAfter,
                                                i32 *pnHpDrops)
{
    i32 nMissing = 0;

    *pnHpDrops = 0;
    for (i32 i = 0; i < nBefore; ++i) {
        bool bFound = false;
        for (i32 j = 0; j < nAfter; ++j) {
            if (pBeforeKey[i] != pAfterKey[j]) { continue; }
            bFound = true;
            if (pAfterHp[j] < pBeforeHp[i]) { ++*pnHpDrops; }
            break;
        }
        if (!bFound) { ++nMissing; }
    }
    return nMissing;
}

static bool App_ScriptCampaignNaturalDefeatTerminal(
    const AppCampaignDefeatSnapshot *pLastBefore, i32 nAttemptedTurns,
    i32 nXcomHpDrops, i32 nXcomRemoved)
{
    const i32 nMapOwnerLive = g_world.pTactMap != NULL ? 1 : 0;
    const i32 nQueueOwnerLive = g_world.pTactState != NULL ? 1 : 0;
    const i32 nEndingBackdrop = g_pEndScreenBackdrop != NULL ? 1 : 0;
    const bool bExpected = g_world.nUiScreen == 18u && g_nUiScreen == 18u
        && g_world.bTactLive == 0 && g_world.bTactFlag == 0
        && nMapOwnerLive == 0 && nQueueOwnerLive == 0
        && g_world.bMapBattleEnded != 0 && g_world.bMapMissionVictory == 0
        && pLastBefore->nMissionType == 3 && g_world.nBaseCount == 1
        && g_nTitlesMaxRows > 0 && nEndingBackdrop != 0;

    printf("pocketufo: campaign natural-defeat terminal %s attemptedTurns %d mission %d baseCount %d "
           "screen %u tact %u/%u owners %d/%d battle %u victory %u endingRows %d backdrop %d "
           "preXcom %d preEnemy %d preReserve %d observedXcomHpDrops %d observedXcomRemoved %d\n",
           bExpected ? "PASS" : "FAIL", (int)nAttemptedTurns,
           (int)pLastBefore->nMissionType, (int)g_world.nBaseCount,
           (unsigned)g_world.nUiScreen, (unsigned)g_world.bTactLive,
           (unsigned)g_world.bTactFlag, nMapOwnerLive, nQueueOwnerLive,
           (unsigned)g_world.bMapBattleEnded, (unsigned)g_world.bMapMissionVictory,
           (int)g_nTitlesMaxRows, nEndingBackdrop,
           (int)pLastBefore->nXcomCount, (int)pLastBefore->nEnemyCount,
           (int)pLastBefore->nEnemyReserve, (int)nXcomHpDrops, (int)nXcomRemoved);
    fflush(stdout);
    return bExpected;
}

static bool App_ScriptCampaignNaturalDefeatExpose(i32 nTurn, i32 *pnMoves)
{
    GameMap *pMap = NULL;
    uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    uintptr_t aSeen[PUFO_MAP_ROSTER_MAX];
    i32 nSeen = 0, nMoves = 0, nAttempts = 0, nBound;

    if (pnMoves != NULL) { *pnMoves = 0; }
    if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)
        || g_world.bMapPlayerInput == 0) {
        printf("pocketufo: campaign natural-defeat movement: FAIL (need live player input)\n");
        fflush(stdout);
        return false;
    }
    nBound = (i32)pMap->nXcomCount;
    if (nBound > PUFO_MAP_ROSTER_MAX) { nBound = PUFO_MAP_ROSTER_MAX; }
    if (nBound <= 0) {
        printf("pocketufo: campaign natural-defeat movement: FAIL (empty XCOM roster)\n");
        fflush(stdout);
        return false;
    }
    if (pMap->pSelUnit == NULL) {
        if (!App_ScriptCampaignClickBank(0)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
            printf("pocketufo: campaign natural-defeat movement: FAIL (widget0 could not select a defender)\n");
            fflush(stdout);
            return false;
        }
    }

    for (i32 nCycle = 0; nCycle < nBound; ++nCycle) {
        Soldier *pSelected;
        uintptr_t nSelectedKey;
        bool bRepeated = false;

        if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
            break;
        }
        pSelected = pMap->pSelUnit;
        nSelectedKey = (uintptr_t)(void *)pSelected;
        if (nSelectedKey != 0) {
            for (i32 i = 0; i < nSeen; ++i) {
                if (aSeen[i] == nSelectedKey) { bRepeated = true; break; }
            }
            if (bRepeated) {
                printf("pocketufo: campaign natural-defeat movement turn %d: widget0 repeated actor 0x%llx; cycle stopped\n",
                       (int)nTurn, (unsigned long long)nSelectedKey);
                fflush(stdout);
                break;
            }
            aSeen[nSeen++] = nSelectedKey;
        }

        Soldier *pUnit = App_ScriptCampaignFindActiveXcom(pMap, nSelectedKey);
        if (pUnit == NULL || pMap->pSelUnit != pUnit) {
            printf("pocketufo: campaign natural-defeat movement turn %d: skip actor 0x%llx (inactive, dead, or unplaced)\n",
                   (int)nTurn, (unsigned long long)nSelectedKey);
        } else {
            bool bMoved = false;
            for (i32 nEnemy = 0; nEnemy < (i32)pMap->nEnemyCount; ++nEnemy) {
                Soldier *pEnemy;
                uintptr_t nEnemyKey;

                if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
                    break;
                }
                pUnit = App_ScriptCampaignFindActiveXcom(pMap, nSelectedKey);
                if (pUnit == NULL || pMap->pSelUnit != pUnit) { break; }
                pEnemy = pMap->apEnemyUnits[nEnemy];
                nEnemyKey = (uintptr_t)(void *)pEnemy;
                pEnemy = App_ScriptCampaignFindActiveEnemy(pMap, nEnemyKey);
                if (pEnemy == NULL || pEnemy->pCell->nZ != pUnit->pCell->nZ) { continue; }

                ++nAttempts;
                printf("pocketufo: campaign natural-defeat movement turn %d: attempt actor 0x%llx enemy %d floor %u\n",
                       (int)nTurn, (unsigned long long)nSelectedKey, (int)nEnemy,
                       (unsigned)pUnit->pCell->nZ);
                fflush(stdout);
                bMoved = App_ScriptCampaignApproach(nEnemy, true, NULL, NULL, NULL);
                if (g_world.nUiScreen == 18u || g_world.nUiScreen == 13u) { break; }
                if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
                    break;
                }
                if (bMoved) {
                    ++nMoves;
                    printf("pocketufo: campaign natural-defeat movement turn %d: moved actor 0x%llx toward enemy %d\n",
                           (int)nTurn, (unsigned long long)nSelectedKey, (int)nEnemy);
                    fflush(stdout);
                    break;
                }
            }
            if (g_world.nUiScreen == 18u || g_world.nUiScreen == 13u
                || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
                break;
            }
            if (!bMoved) {
                printf("pocketufo: campaign natural-defeat movement turn %d: actor 0x%llx had no successful legal closer move\n",
                       (int)nTurn, (unsigned long long)nSelectedKey);
                fflush(stdout);
            }
        }

        if (nCycle + 1 >= nBound) { break; }
        if (!App_ScriptCampaignClickBank(0)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, NULL)) {
            break;
        }
    }

    if (pnMoves != NULL) { *pnMoves = nMoves; }
    printf("pocketufo: campaign natural-defeat movement turn %d: attempts %d moved %d rosterBound %d\n",
           (int)nTurn, (int)nAttempts, (int)nMoves, (int)nBound);
    fflush(stdout);
    return nMoves > 0;
}

static bool App_ScriptCampaignNaturalDefeat(i32 nMaxTurns, bool bExposeDefenders)
{
    AppCampaignDefeatSnapshot sBefore, sAfter;
    AppCampaignDefeatSnapshot sAfterMove;
    GameMap *pMap = g_world.pTactMap;
    TactState *pQueue = g_world.pTactState;
    uintptr_t nMapKey, nQueueKey;
    i32 nObservedXcomHpDrops = 0;
    i32 nObservedXcomRemoved = 0;
    bool bObservedXcomLoss = false;

    if (!g_bWorldReady || !World_bTactPointerLive(&g_world)
        || pMap == NULL || pQueue == NULL || g_world.nUiScreen != 0u
        || pMap->nMissionType != 3u || g_world.nBaseCount != 1
        || g_world.bTactLive == 0 || g_world.bTactFlag == 0
        || g_world.bMapBattleEnded != 0 || g_world.bMapMissionVictory != 0
        || g_world.bMapPlayerInput == 0 || g_world.bMapEnemyTurnActive != 0
        || pMap->nXcomCount == 0
        || (pMap->nEnemyCount == 0 && g_world.nMapEnemyReserveCount == 0)
        || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0
        || pQueue->bQueueActive != 0) {
        printf("pocketufo: campaign natural-defeat: FAIL (need an idle live type-3 mission, one base, "
               "player input, XCOM, and an enemy or reserve)\n");
        fflush(stdout);
        return false;
    }

    nMapKey = (uintptr_t)(void *)pMap;
    nQueueKey = (uintptr_t)(void *)pQueue;
    printf("pocketufo: campaign natural-defeat%s: start maxTurns %d type %u baseCount %d xcom %u enemy %u reserve %d\n",
           bExposeDefenders ? "-exposed" : "",
           (int)nMaxTurns, (unsigned)pMap->nMissionType, (int)g_world.nBaseCount,
           (unsigned)pMap->nXcomCount, (unsigned)pMap->nEnemyCount,
           (int)g_world.nMapEnemyReserveCount);
    fflush(stdout);

    for (i32 nTurn = 1; nTurn <= nMaxTurns; ++nTurn) {
        i32 nXcomHpDrops, nEnemyHpDrops;
        i32 nXcomRemoved, nEnemyRemoved;
        i32 nMovementMoves = 0;
        bool bTurn;

        pMap = g_world.pTactMap;
        pQueue = g_world.pTactState;
        if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
            || (uintptr_t)(void *)pMap != nMapKey || (uintptr_t)(void *)pQueue != nQueueKey
            || g_world.nUiScreen != 0u || pMap->nMissionType != 3u
            || g_world.nBaseCount != 1 || g_world.bTactLive == 0
            || g_world.bTactFlag == 0 || g_world.bMapBattleEnded != 0
            || g_world.bMapMissionVictory != 0 || g_world.bMapPlayerInput == 0
            || g_world.bMapEnemyTurnActive != 0 || pMap->nXcomCount == 0
            || (pMap->nEnemyCount == 0 && g_world.nMapEnemyReserveCount == 0)
            || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0
            || pQueue->bQueueActive != 0) {
            printf("pocketufo: campaign natural-defeat: FAIL (type-3 owners or idle player input changed before turn %d)\n",
                   (int)nTurn);
            fflush(stdout);
            return false;
        }

        App_ScriptCampaignDefeatSnapshot(pMap, nTurn, "before", &sBefore);
        pMap = NULL;
        pQueue = NULL;

        if (bExposeDefenders) {
            i32 nMoveHpDrops, nMoveRemoved;
            if (!App_ScriptCampaignNaturalDefeatExpose(nTurn, &nMovementMoves)) {
                if (g_world.nUiScreen == 18u) {
                    nObservedXcomRemoved += sBefore.nXcomSeen;
                    bObservedXcomLoss = sBefore.nXcomSeen > 0;
                    printf("pocketufo: campaign natural-defeat-exposed: terminal during movement turn %d; pre-movement XCOM removal evidence %d\n",
                           (int)nTurn, (int)sBefore.nXcomSeen);
                    fflush(stdout);
                    return App_ScriptCampaignNaturalDefeatTerminal(
                        &sBefore, nTurn, nObservedXcomHpDrops, nObservedXcomRemoved);
                }
                if (g_world.nUiScreen == 13u) {
                    printf("pocketufo: campaign natural-defeat-exposed: FAIL (unexpected screen 13 during movement turn %d)\n",
                           (int)nTurn);
                    fflush(stdout);
                    return false;
                }
                if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, NULL, NULL)) {
                    printf("pocketufo: campaign natural-defeat-exposed: FAIL (tactical owners changed during movement turn %d)\n",
                           (int)nTurn);
                    fflush(stdout);
                    return false;
                }
                printf("pocketufo: campaign natural-defeat-exposed: FAIL (no legal closer movement on turn %d; no passive turn added)\n",
                       (int)nTurn);
                fflush(stdout);
                return false;
            }
            if (g_world.nUiScreen == 18u) {
                nObservedXcomRemoved += sBefore.nXcomSeen;
                bObservedXcomLoss = sBefore.nXcomSeen > 0;
                printf("pocketufo: campaign natural-defeat-exposed: terminal during movement turn %d; pre-movement XCOM removal evidence %d\n",
                       (int)nTurn, (int)sBefore.nXcomSeen);
                fflush(stdout);
                return App_ScriptCampaignNaturalDefeatTerminal(
                    &sBefore, nTurn, nObservedXcomHpDrops, nObservedXcomRemoved);
            }
            if (g_world.nUiScreen == 13u) {
                printf("pocketufo: campaign natural-defeat-exposed: FAIL (unexpected screen 13 during movement turn %d)\n",
                       (int)nTurn);
                fflush(stdout);
                return false;
            }
            if (!App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
                || g_world.bMapPlayerInput == 0 || pMap->nMissionType != 3u
                || g_world.nBaseCount != 1 || pMap->tact.bBusy != 0
                || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
                printf("pocketufo: campaign natural-defeat-exposed: FAIL (live owners or idle player input changed after movement turn %d)\n",
                       (int)nTurn);
                fflush(stdout);
                return false;
            }
            App_ScriptCampaignDefeatSnapshot(pMap, nTurn, "after-movement", &sAfterMove);
            nMoveRemoved = App_ScriptCampaignDefeatCountMissing(
                sBefore.aXcomKey, sBefore.aXcomHp, sBefore.nXcomSeen,
                sAfterMove.aXcomKey, sAfterMove.aXcomHp, sAfterMove.nXcomSeen, &nMoveHpDrops);
            printf("pocketufo: campaign natural-defeat movement turn %d: moved %d xcom %d->%d hpDrops %d removed %d\n",
                   (int)nTurn, (int)nMovementMoves, (int)sBefore.nXcomCount,
                   (int)sAfterMove.nXcomCount, (int)nMoveHpDrops, (int)nMoveRemoved);
            fflush(stdout);
        }

        bTurn = App_ScriptCampaignEnemyTurn(false, false);

        if (g_world.nUiScreen == 18u) {
            if (!bTurn) {
                printf("pocketufo: campaign natural-defeat: FAIL (End Turn helper rejected screen 18)\n");
                fflush(stdout);
                return false;
            }
            nObservedXcomRemoved += sBefore.nXcomSeen;
            bObservedXcomLoss = sBefore.nXcomSeen > 0;
            printf("pocketufo: campaign natural-defeat%s: terminal after enemy turn %d; pre-turn XCOM removal evidence %d\n",
                   bExposeDefenders ? "-exposed" : "", (int)nTurn, (int)sBefore.nXcomSeen);
            fflush(stdout);
            if (!bObservedXcomLoss && nTurn > 1) {
                printf("pocketufo: campaign natural-defeat: FAIL (screen 18 followed %d turns without observed XCOM HP loss or removal)\n",
                       (int)(nTurn - 1));
                fflush(stdout);
                return false;
            }
            return App_ScriptCampaignNaturalDefeatTerminal(
                &sBefore, nTurn, nObservedXcomHpDrops, nObservedXcomRemoved);
        }
        if (g_world.nUiScreen == 13u) {
            printf("pocketufo: campaign natural-defeat: FAIL (unexpected screen 13 battle %u victory %u after turn %d)\n",
                   (unsigned)g_world.bMapBattleEnded, (unsigned)g_world.bMapMissionVictory,
                   (int)nTurn);
            fflush(stdout);
            return false;
        }
        if (!bTurn) {
            printf("pocketufo: campaign natural-defeat: FAIL (owner-safe End Turn did not complete turn %d)\n",
                   (int)nTurn);
            fflush(stdout);
            return false;
        }
        if (!World_bTactPointerLive(&g_world) || g_world.pTactMap == NULL
            || g_world.pTactState == NULL
            || (uintptr_t)(void *)g_world.pTactMap != nMapKey
            || (uintptr_t)(void *)g_world.pTactState != nQueueKey) {
            printf("pocketufo: campaign natural-defeat: FAIL (tactical owners disappeared or changed after turn %d)\n",
                   (int)nTurn);
            fflush(stdout);
            return false;
        }
        pMap = g_world.pTactMap;
        pQueue = g_world.pTactState;
        if (g_world.nUiScreen != 0u || pMap->nMissionType != 3u
            || g_world.nBaseCount != 1 || g_world.bTactLive == 0
            || g_world.bTactFlag == 0 || g_world.bMapBattleEnded != 0
            || g_world.bMapMissionVictory != 0 || g_world.bMapPlayerInput == 0
            || g_world.bMapEnemyTurnActive != 0 || pMap->tact.bBusy != 0
            || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
            printf("pocketufo: campaign natural-defeat: FAIL (unexpected live state after turn %d screen %u type %u base %d input %u enemyturn %u)\n",
                   (int)nTurn, (unsigned)g_world.nUiScreen,
                   (unsigned)pMap->nMissionType, (int)g_world.nBaseCount,
                   (unsigned)g_world.bMapPlayerInput, (unsigned)g_world.bMapEnemyTurnActive);
            fflush(stdout);
            return false;
        }

        App_ScriptCampaignDefeatSnapshot(pMap, nTurn, "after", &sAfter);
        nXcomRemoved = App_ScriptCampaignDefeatCountMissing(
            sBefore.aXcomKey, sBefore.aXcomHp, sBefore.nXcomSeen,
            sAfter.aXcomKey, sAfter.aXcomHp, sAfter.nXcomSeen, &nXcomHpDrops);
        nEnemyRemoved = App_ScriptCampaignDefeatCountMissing(
            sBefore.aEnemyKey, sBefore.aEnemyHp, sBefore.nEnemySeen,
            sAfter.aEnemyKey, sAfter.aEnemyHp, sAfter.nEnemySeen, &nEnemyHpDrops);
        nObservedXcomHpDrops += nXcomHpDrops;
        nObservedXcomRemoved += nXcomRemoved;
        if (nXcomHpDrops > 0 || nXcomRemoved > 0
            || sAfter.nXcomCount < sBefore.nXcomCount) {
            bObservedXcomLoss = true;
        }
        printf("pocketufo: campaign natural-defeat turn %d: xcom %d->%d hpDrops %d removed %d "
               "enemy %d->%d hpDrops %d removed %d reserve %d->%d\n",
               (int)nTurn, (int)sBefore.nXcomCount, (int)sAfter.nXcomCount,
               (int)nXcomHpDrops, (int)nXcomRemoved,
               (int)sBefore.nEnemyCount, (int)sAfter.nEnemyCount,
               (int)nEnemyHpDrops, (int)nEnemyRemoved,
               (int)sBefore.nEnemyReserve, (int)sAfter.nEnemyReserve);
        fflush(stdout);
        if (sAfter.nXcomCount == 0) {
            printf("pocketufo: campaign natural-defeat: FAIL (no defenders remain but screen 18 did not open)\n");
            fflush(stdout);
            return false;
        }
        if (sAfter.nEnemyCount == 0 && sAfter.nEnemyReserve == 0) {
            printf("pocketufo: campaign natural-defeat: FAIL (no attackers remain but no result screen opened)\n");
            fflush(stdout);
            return false;
        }
    }

    App_ScriptCampaignState("natural_defeat_limit");
    App_ScriptTact();
    pMap = g_world.pTactMap;
    if (pMap != NULL) {
        for (i32 nSide = 0; nSide < 2; ++nSide) {
            const i32 nUnits = nSide == 0 ? (i32)pMap->nXcomCount : (i32)pMap->nEnemyCount;
            for (i32 i = 0; i < nUnits && i < PUFO_MAP_ROSTER_MAX; ++i) {
                const Soldier *pUnit = nSide == 0 ? pMap->apXcomUnits[i] : pMap->apEnemyUnits[i];
                if (pUnit == NULL) { continue; }
                const TactItem *pLeft = pUnit->pHandL;
                const TactItem *pRight = pUnit->pHandR;
                printf("pocketufo: natural-defeat loadout side %d unit %d type %u hp %u tu %u "
                       "left %d ammo %d right %d ammo %d\n", nSide, i,
                       (unsigned)pUnit->nUnitType, (unsigned)pUnit->uHp, (unsigned)pUnit->uTu,
                       App_ScriptCampaignObjectSlot(pLeft != NULL ? pLeft->pObj : NULL),
                       pLeft != NULL ? (i32)pLeft->nAmmo : -1,
                       App_ScriptCampaignObjectSlot(pRight != NULL ? pRight->pObj : NULL),
                       pRight != NULL ? (i32)pRight->nAmmo : -1);
            }
        }
    }
    printf("pocketufo: campaign natural-defeat: FAIL (turn limit %d reached without natural screen 18; "
           "last snapshot mission %d xcom %d enemy %d reserve %d observedXcomHpDrops %d observedXcomRemoved %d)\n",
           (int)nMaxTurns, (int)sAfter.nMissionType, (int)sAfter.nXcomCount,
           (int)sAfter.nEnemyCount, (int)sAfter.nEnemyReserve,
           (int)nObservedXcomHpDrops, (int)nObservedXcomRemoved);
    fflush(stdout);
    return false;
}

/* Return 1 only for a real normal victory, -1 for any ended non-win, and 0
 * while the live mission is still open. The caller never starts another action
 * after this reports an outcome. */
static i32 App_ScriptCampaignNaturalOutcome(const char *pszAfter, i32 nTurns, i32 nActions)
{
    const GameMap *pMap = g_world.pTactMap;

    if (g_world.bMapBattleEnded == 0 && g_world.nUiScreen != 13u && g_world.nUiScreen != 18u) {
        return 0;
    }
    if (g_world.nUiScreen == 13u && g_world.bMapBattleEnded != 0
        && g_world.bMapMissionVictory != 0 && pMap != NULL && pMap->nXcomCount != 0
        && g_world.nMapEnemyReserveCount == 0) {
        printf("pocketufo: campaign natural-fight outcome: victory after %s, screen %u, xcom %u, "
               "enemy %u, reserve %u, missionVictory %u\n",
               (pszAfter != NULL) ? pszAfter : "action", (unsigned)g_world.nUiScreen,
               (unsigned)pMap->nXcomCount, (unsigned)pMap->nEnemyCount,
               (unsigned)g_world.nMapEnemyReserveCount, (unsigned)g_world.bMapMissionVictory);
        App_ScriptCampaignState("natural_victory_debrief");
        printf("pocketufo: campaign natural-fight: PASS turns %d actions %d\n",
               (int)nTurns, (int)nActions);
        fflush(stdout);
        return 1;
    }
    printf("pocketufo: campaign natural-fight outcome: FAIL ended after %s, screen %u, battle %u, "
           "missionVictory %u, xcom %u, enemy %u, reserve %u\n",
           (pszAfter != NULL) ? pszAfter : "action", (unsigned)g_world.nUiScreen,
           (unsigned)g_world.bMapBattleEnded, (unsigned)g_world.bMapMissionVictory,
           pMap != NULL ? (unsigned)pMap->nXcomCount : 0u,
           pMap != NULL ? (unsigned)pMap->nEnemyCount : 0u,
           (unsigned)g_world.nMapEnemyReserveCount);
    fflush(stdout);
    return -1;
}

/* Bounded test driver. Every move, shot, and end turn goes through the same
 * pen/key helpers as the campaign fixture; battle end is read after each one. */
static bool App_ScriptCampaignNaturalFight(i32 nMaxTurns, bool bOutsidePlane)
{
    GameMap *pMap = g_world.pTactMap;
    TactState *pQueue = g_world.pTactState;
    const uintptr_t nMapKey = (uintptr_t)(void *)pMap;
    const uintptr_t nQueueKey = (uintptr_t)(void *)pQueue;
    AppCampaignPlaneRect sPlane = { 0 };
    AppCampaignFireWeapon sFirstFire = { 0 };
    const AppCampaignPlaneRect *pPlane = bOutsidePlane ? &sPlane : NULL;
    i32 nTurns = 0, nActions = 0;
    i32 nFirstPathCost = 0, nFirstSnapCost = 0;
    uintptr_t nPreferredPrepActorKey = 0;
    AppCampaignOutsidePrepMove sLastPrepMove = { 0 };
    bool bFirstOutsideFirePending = false;
    bool bPrepMoveNeedsEndTurn = false;

    if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
        || g_world.nUiScreen != 0 || g_world.bMapPlayerInput == 0
        || pMap->nMissionType != 1u || nMaxTurns <= 0 || nMaxTurns > 32
        || (bOutsidePlane && nMaxTurns > 12)) {
        printf("pocketufo: campaign natural-fight: FAIL (need live type-1 input and 1..32 turns)\n");
        fflush(stdout);
        return false;
    }
    if (pPlane != NULL) {
        if (!App_ScriptCampaignCheckOutsidePlaneLayout(pMap, &sPlane)
            || !App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)
            || g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
            || g_world.bMapPlayerInput == 0 || pMap->nEnemyCount == 0
            || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0
            || pQueue->bQueueActive != 0) {
            printf("pocketufo: campaign natural-fight-outside-plane: FAIL pre-input layout or idle gate\n");
            fflush(stdout);
            return false;
        }
    }
    printf("pocketufo: campaign natural-fight: start maxTurns %d xcom %u enemy %u reserve %u\n",
           (int)nMaxTurns, (unsigned)pMap->nXcomCount, (unsigned)pMap->nEnemyCount,
           (unsigned)g_world.nMapEnemyReserveCount);
    fflush(stdout);
    App_ScriptCampaignFireLoadoutCensus(nMapKey, nQueueKey, "fight_start");

    if (pPlane != NULL) {
        if (App_ScriptCampaignMoveOutsideRifle(pPlane, nPreferredPrepActorKey,
                                                &sFirstFire, &nFirstPathCost,
                                                &nFirstSnapCost)) {
            ++nActions;
            printf("pocketufo: campaign natural-fight-outside-plane: preparation move counted actions %d/160 "
                   "pathCost %d snapCost %d turnsRemain %d actionsRemain %d\n",
                   (int)nActions, (int)nFirstPathCost, (int)nFirstSnapCost,
                   (int)(nMaxTurns - nTurns), (int)(160 - nActions));
            fflush(stdout);
            i32 nOutcome = App_ScriptCampaignNaturalOutcome("outside-plane move", nTurns, nActions);
            if (nOutcome != 0) { return nOutcome > 0; }
            bFirstOutsideFirePending = true;
        } else {
            const AppCampaignOutsidePrepResult ePrep = App_ScriptCampaignMoveOutsidePrefix(
                pPlane, nPreferredPrepActorKey, nTurns, nActions, &sLastPrepMove);
            if (ePrep == APP_CAMPAIGN_OUTSIDE_PREP_FAIL) { return false; }
            if (ePrep != APP_CAMPAIGN_OUTSIDE_PREP_MOVED) {
                printf("pocketufo: campaign natural-fight-outside-plane: FAIL no affordable "
                       "outside move or inside prefix\n");
                fflush(stdout);
                return false;
            }
            ++nActions;
            nPreferredPrepActorKey = sLastPrepMove.nActorKey;
            bPrepMoveNeedsEndTurn = true;
            printf("pocketufo: campaign outside-plane prep action: seq %d actions %d/160 "
                   "turn %d/%d turnsRemain %d actionsRemain %d\n",
                   (int)sLastPrepMove.nSequence, (int)nActions,
                   (int)nTurns, (int)nMaxTurns, (int)(nMaxTurns - nTurns),
                   (int)(160 - nActions));
            fflush(stdout);
            i32 nOutcome = App_ScriptCampaignNaturalOutcome("outside-plane prep prefix",
                                                             nTurns, nActions);
            if (nOutcome != 0) { return nOutcome > 0; }
        }
    }

    while (nTurns < nMaxTurns && (pPlane == NULL || nActions < 160)) {
        i32 nOutcome = App_ScriptCampaignNaturalOutcome("turn start", nTurns, nActions);
        if (nOutcome != 0) { return nOutcome > 0; }
        pMap = g_world.pTactMap;
        pQueue = g_world.pTactState;
        if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
            || g_world.nUiScreen != 0 || g_world.bMapPlayerInput == 0
            || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
            printf("pocketufo: campaign natural-fight: FAIL (live player input is not idle)\n");
            fflush(stdout);
            return false;
        }
        if (bPrepMoveNeedsEndTurn) { goto campaign_natural_fight_end_turn; }

        /* Consume a primed grenade through the normal enemy-turn control, then
         * recheck the result before selecting another action. */
        {
            const ObjectRecFull *pGrenadeObj = (g_assets.apObjects != NULL
                                                 && g_assets.nObjectCount > 17)
                ? g_assets.apObjects[17] : NULL;
            const i32 nPrimed = bPrepMoveNeedsEndTurn ? 0
                : App_ScriptCampaignFindPrimedGrenades(pGrenadeObj, NULL, NULL);
            if (nPrimed != 0) {
                bool bTurn;
                char azStateLabel[64];
                if (nPrimed != 1) {
                    printf("pocketufo: campaign natural-fight: FAIL (expected one armed grenade, found %d)\n",
                           (int)nPrimed);
                    fflush(stdout);
                    return false;
                }
                (void)snprintf(azStateLabel, sizeof(azStateLabel),
                               "natural_fight_enemy_turn_%d_before", (int)nTurns + 1);
                App_ScriptCampaignState(azStateLabel);
                bTurn = App_ScriptCampaignEnemyTurn(true, pPlane != NULL);
                (void)snprintf(azStateLabel, sizeof(azStateLabel),
                               "natural_fight_enemy_turn_%d_after", (int)nTurns + 1);
                App_ScriptCampaignState(azStateLabel);
                if (bTurn && nTurns < 12) {
                    App_ScriptCampaignFireLoadoutCensus(nMapKey, nQueueKey, azStateLabel);
                }
                if (bTurn) { ++nTurns; }
                nOutcome = App_ScriptCampaignNaturalOutcome("grenade enemy turn", nTurns, nActions);
                if (nOutcome != 0) { return nOutcome > 0; }
                if (!bTurn) {
                    printf("pocketufo: campaign natural-fight: FAIL (real grenade End Turn did not settle)\n");
                    fflush(stdout);
                    return false;
                }
                continue;
            }
        }

        for (i32 nAction = 0; nAction < 16 && (pPlane == NULL || nActions < 160)
             && !bPrepMoveNeedsEndTurn; ++nAction) {
            i32 nEnemyIndex = -1;
            bool bFired = false;
            const char *pszOutsideApproachReason = NULL;

            nOutcome = App_ScriptCampaignNaturalOutcome("before action", nTurns, nActions);
            if (nOutcome != 0) { return nOutcome > 0; }
            pMap = g_world.pTactMap;
            pQueue = g_world.pTactState;
            if (!World_bTactPointerLive(&g_world) || pMap == NULL || pQueue == NULL
                || g_world.nUiScreen != 0 || g_world.bMapPlayerInput == 0
                || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
                printf("pocketufo: campaign natural-fight: FAIL (player input changed during the turn)\n");
                fflush(stdout);
                return false;
            }
            for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
                const Soldier *pEnemy = pMap->apEnemyUnits[i];
                if (pEnemy != NULL && pEnemy->uHp != 0
                    && pEnemy->bStatus < UNIT_STATUS_INACTIVE && pEnemy->pCell != NULL) {
                    nEnemyIndex = i;
                    break;
                }
            }
            if (nEnemyIndex < 0 && g_world.nMapEnemyReserveCount == 0) {
                printf("pocketufo: campaign natural-fight: FAIL (no live enemy or reserve, but no natural result)\n");
                fflush(stdout);
                return false;
            }

            bFired = App_ScriptCampaignFire(
                pPlane,
                bFirstOutsideFirePending ? &sFirstFire : NULL,
                bFirstOutsideFirePending ? nFirstSnapCost : 0,
                &pszOutsideApproachReason);
            bFirstOutsideFirePending = false;
            if (bFired) { ++nActions; }
            nOutcome = App_ScriptCampaignNaturalOutcome("fire", nTurns, nActions);
            if (nOutcome != 0) { return nOutcome > 0; }
            if (bFired) { continue; }

            /* A no-shot attempt may leave a real fire menu or latch open. Clear
             * that input state before trying movement. */
            pMap = g_world.pTactMap;
            if (pMap != NULL && pMap->bActionLatch != PUFO_ACTION_LATCH_NONE) {
                if (!App_ScriptCampaignKey(PUFO_KEY_CONFIRM)) {
                    printf("pocketufo: campaign natural-fight: FAIL (could not clear action latch)\n");
                    fflush(stdout);
                    return false;
                }
                nOutcome = App_ScriptCampaignNaturalOutcome("action cancel", nTurns, nActions);
                if (nOutcome != 0) { return nOutcome > 0; }
            }
            if (g_world.nUiScreen != 0) {
                if (!App_ScriptCampaignKey(PUFO_KEY_ESCAPE)) {
                    printf("pocketufo: campaign natural-fight: FAIL (could not close action panel)\n");
                    fflush(stdout);
                    return false;
                }
                (void)App_DrawFrame();
                nOutcome = App_ScriptCampaignNaturalOutcome("panel close", nTurns, nActions);
                if (nOutcome != 0) { return nOutcome > 0; }
            }
            if (pPlane != NULL) {
                AppCampaignFireWeapon sNextFire = { 0 };
                i32 nPathCost = 0;
                i32 nSnapCost = 0;

                if (pszOutsideApproachReason != NULL
                    && App_ScriptCampaignTactOwnersMatch(nMapKey, nQueueKey, &pMap, &pQueue)) {
                    Soldier *pApproachUnit = pMap->pSelUnit;
                    const i32 nApproachEnemy = App_ScriptCampaignClosestLiveEnemyIndex(
                        pMap, pApproachUnit);

                    if (nApproachEnemy >= 0 && pApproachUnit != NULL
                        && pApproachUnit->pCell != NULL) {
                        Soldier *pApproachTarget = pMap->apEnemyUnits[nApproachEnemy];
                        const uintptr_t nApproachTargetKey =
                            (uintptr_t)(void *)pApproachTarget;
                        const i32 nApproachDistance =
                            App_ScriptCampaignCellDistance(pApproachUnit->pCell,
                                                           pApproachTarget->pCell);

                        for (i32 i = 0; i < (i32)pMap->nEnemyCount
                             && i < PUFO_MAP_ROSTER_MAX; ++i) {
                            Soldier *pCandidate = pMap->apEnemyUnits[i];

                            if (pCandidate == NULL || pCandidate->pCell == NULL
                                || pCandidate->uHp == 0
                                || pCandidate->bStatus >= UNIT_STATUS_INACTIVE) {
                                continue;
                            }
                            printf("pocketufo: campaign outside-plane approach candidate: actor 0x%llX "
                                   "target 0x%llX roster %d cell %d/%d/%d distance %d live 1 selected %d\n",
                                   (unsigned long long)(uintptr_t)(void *)pApproachUnit,
                                   (unsigned long long)(uintptr_t)(void *)pCandidate, (int)i,
                                   (int)pCandidate->pCell->nX, (int)pCandidate->pCell->nY,
                                   (int)pCandidate->pCell->nZ,
                                   (int)App_ScriptCampaignCellDistance(pApproachUnit->pCell,
                                                                      pCandidate->pCell),
                                   pCandidate == pApproachTarget ? 1 : 0);
                        }

                        printf("pocketufo: campaign outside-plane approach goal: actor 0x%llX "
                               "cell %d/%d/%d target 0x%llX roster %d cell %d/%d/%d "
                               "distance %d metric chebyshev reason %s\n",
                               (unsigned long long)(uintptr_t)(void *)pApproachUnit,
                               (int)pApproachUnit->pCell->nX, (int)pApproachUnit->pCell->nY,
                               (int)pApproachUnit->pCell->nZ,
                               (unsigned long long)(uintptr_t)(void *)pApproachTarget,
                               (int)nApproachEnemy,
                               pApproachTarget != NULL && pApproachTarget->pCell != NULL
                                   ? (int)pApproachTarget->pCell->nX : -1,
                               pApproachTarget != NULL && pApproachTarget->pCell != NULL
                                   ? (int)pApproachTarget->pCell->nY : -1,
                               pApproachTarget != NULL && pApproachTarget->pCell != NULL
                                   ? (int)pApproachTarget->pCell->nZ : -1,
                               (int)nApproachDistance, pszOutsideApproachReason);
                        fflush(stdout);
                        if (pApproachTarget != NULL && pApproachTarget->pCell != NULL) {
                            bool bApproachActionSpent = false;
                            AppCampaignApproachResult eApproach = APP_CAMPAIGN_APPROACH_FAILED;

                            (void)App_ScriptCampaignApproach(nApproachEnemy, false, pPlane,
                                                            &bApproachActionSpent,
                                                            &eApproach);

                            if (eApproach != APP_CAMPAIGN_APPROACH_FAILED) {
                                const char *pszMoveStatus =
                                    eApproach == APP_CAMPAIGN_APPROACH_MOVED ? "moved" :
                                    (eApproach == APP_CAMPAIGN_APPROACH_PARTIAL ? "partial"
                                                                                : "interrupted");

                                if (!bApproachActionSpent) {
                                    printf("pocketufo: campaign natural-fight-outside-plane: STOP "
                                           "after approach interruption without proven TU or cell change\n");
                                    fflush(stdout);
                                    return false;
                                }
                                ++nActions;
                                printf("pocketufo: campaign natural-fight-outside-plane: approach "
                                       "counted status %s actions %d/160 turn %d/%d turnsRemain %d "
                                       "actionsRemain %d target 0x%llX roster %d\n",
                                       pszMoveStatus, (int)nActions, (int)nTurns, (int)nMaxTurns,
                                       (int)(nMaxTurns - nTurns), (int)(160 - nActions),
                                       (unsigned long long)nApproachTargetKey, (int)nApproachEnemy);
                                fflush(stdout);
                                nOutcome = App_ScriptCampaignNaturalOutcome(
                                    "outside-plane approach", nTurns, nActions);
                                if (nOutcome != 0) { return nOutcome > 0; }
                                if (eApproach == APP_CAMPAIGN_APPROACH_INTERRUPTED) {
                                    printf("pocketufo: campaign natural-fight-outside-plane: STOP "
                                           "after interrupted approach action\n");
                                    fflush(stdout);
                                    return false;
                                }
                                continue;
                            }
                        }
                    }
                }
                if (nActions >= 159) { break; }
                if (App_ScriptCampaignMoveOutsideRifle(pPlane, nPreferredPrepActorKey,
                                                        &sNextFire, &nPathCost,
                                                        &nSnapCost)) {
                    ++nActions;
                    printf("pocketufo: campaign natural-fight-outside-plane: preparation move counted "
                           "actions %d/160 pathCost %d snapCost %d turnsRemain %d actionsRemain %d\n",
                           (int)nActions, (int)nPathCost, (int)nSnapCost,
                           (int)(nMaxTurns - nTurns), (int)(160 - nActions));
                    nOutcome = App_ScriptCampaignNaturalOutcome("outside-plane move", nTurns, nActions);
                    if (nOutcome != 0) { return nOutcome > 0; }
                    if (sNextFire.nActorKey == nPreferredPrepActorKey) {
                        nPreferredPrepActorKey = 0;
                    }
                    continue;
                }
                {
                    const AppCampaignOutsidePrepResult ePrep = App_ScriptCampaignMoveOutsidePrefix(
                        pPlane, nPreferredPrepActorKey, nTurns, nActions, &sLastPrepMove);
                    if (ePrep == APP_CAMPAIGN_OUTSIDE_PREP_FAIL) { return false; }
                    if (ePrep == APP_CAMPAIGN_OUTSIDE_PREP_MOVED) {
                        ++nActions;
                        nPreferredPrepActorKey = sLastPrepMove.nActorKey;
                        bPrepMoveNeedsEndTurn = true;
                        printf("pocketufo: campaign outside-plane prep action: seq %d actions %d/160 "
                               "turn %d/%d turnsRemain %d actionsRemain %d\n",
                               (int)sLastPrepMove.nSequence, (int)nActions,
                               (int)nTurns, (int)nMaxTurns, (int)(nMaxTurns - nTurns),
                               (int)(160 - nActions));
                        fflush(stdout);
                        nOutcome = App_ScriptCampaignNaturalOutcome("outside-plane prep prefix",
                                                                     nTurns, nActions);
                        if (nOutcome != 0) { return nOutcome > 0; }
                        break;
                    }
                    if (nPreferredPrepActorKey != 0 && nActions < 158) {
                        GameMap *pUnreadyMap = g_world.pTactMap;
                        Soldier *pUnready = (pUnreadyMap != NULL)
                            ? App_ScriptCampaignFindActiveXcom(pUnreadyMap,
                                                               nPreferredPrepActorKey) : NULL;
                        printf("pocketufo: campaign outside-plane prep unready: priorSeq %d actor 0x%llX "
                               "active %d cell %d/%d/%d tu %u reason no_staged_prefix_candidate "
                               "turn %d actions %d/160\n",
                               (int)sLastPrepMove.nSequence,
                               (unsigned long long)nPreferredPrepActorKey,
                               pUnready != NULL && pUnready->pCell != NULL ? 1 : 0,
                               pUnready != NULL && pUnready->pCell != NULL ? (int)pUnready->pCell->nX : -1,
                               pUnready != NULL && pUnready->pCell != NULL ? (int)pUnready->pCell->nY : -1,
                               pUnready != NULL && pUnready->pCell != NULL ? (int)pUnready->pCell->nZ : -1,
                               pUnready != NULL ? (unsigned)pUnready->uTu : 0u,
                               (int)nTurns, (int)nActions);
                        fflush(stdout);
                        /* No affordable prep path is a normal end of this player turn.
                         * Keep the existing action/turn limits and use the real End Turn below. */
                        break;
                    }
                }
            } else if (nEnemyIndex >= 0
                       && App_ScriptCampaignApproach(nEnemyIndex, false, NULL, NULL, NULL)) {
                ++nActions;
                nOutcome = App_ScriptCampaignNaturalOutcome("approach", nTurns, nActions);
                if (nOutcome != 0) { return nOutcome > 0; }
                continue;
            }
            break;
        }

        if (pPlane != NULL && nActions >= 160) { break; }

campaign_natural_fight_end_turn:
        pMap = g_world.pTactMap;
        pQueue = g_world.pTactState;
        if (!World_bTactPointerLive(&g_world) || g_world.nUiScreen != 0
            || g_world.bMapPlayerInput == 0 || pMap == NULL || pQueue == NULL
            || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0 || pQueue->bQueueActive != 0) {
            printf("pocketufo: campaign natural-fight: FAIL (real End Turn is not ready)\n");
            fflush(stdout);
            return false;
        }
        {
            char azStateLabel[64];
            (void)snprintf(azStateLabel, sizeof(azStateLabel),
                           "natural_fight_enemy_turn_%d_before", (int)nTurns + 1);
            App_ScriptCampaignState(azStateLabel);
            const bool bTurn = App_ScriptCampaignEnemyTurn(false, pPlane != NULL);
            (void)snprintf(azStateLabel, sizeof(azStateLabel),
                           "natural_fight_enemy_turn_%d_after", (int)nTurns + 1);
            App_ScriptCampaignState(azStateLabel);
            if (!bTurn) {
                printf("pocketufo: campaign natural-fight: FAIL (real End Turn did not settle)\n");
                fflush(stdout);
                return false;
            }
            ++nTurns;
            if (bPrepMoveNeedsEndTurn) {
                GameMap *pResumeMap = g_world.pTactMap;
                Soldier *pResume = (pResumeMap != NULL)
                    ? App_ScriptCampaignFindActiveXcom(pResumeMap, sLastPrepMove.nActorKey)
                    : NULL;

                if (!App_ScriptCampaignLogOutsidePrepTurn(&sLastPrepMove, nTurns,
                                                           nActions, nMaxTurns)) {
                    printf("pocketufo: campaign outside-plane prep: FAIL turn witness did not restore player input\n");
                    fflush(stdout);
                    return false;
                }
                nPreferredPrepActorKey = (pResume != NULL && pResume->pCell != NULL)
                    ? sLastPrepMove.nActorKey : 0;
                bPrepMoveNeedsEndTurn = false;
            }
            if (nTurns <= 12) {
                App_ScriptCampaignFireLoadoutCensus(nMapKey, nQueueKey, azStateLabel);
            }
            nOutcome = App_ScriptCampaignNaturalOutcome("enemy turn", nTurns, nActions);
            if (nOutcome != 0) { return nOutcome > 0; }
        }
    }

    printf("pocketufo: campaign natural-fight: FAIL bounded turns %d actions %d, screen %u, xcom %u, "
           "enemy %u, reserve %u, missionVictory %u\n",
           (int)nTurns, (int)nActions, (unsigned)g_world.nUiScreen,
           g_world.pTactMap != NULL ? (unsigned)g_world.pTactMap->nXcomCount : 0u,
           g_world.pTactMap != NULL ? (unsigned)g_world.pTactMap->nEnemyCount : 0u,
           (unsigned)g_world.nMapEnemyReserveCount, (unsigned)g_world.bMapMissionVictory);
    fflush(stdout);
    return false;
}

static void App_ScriptCampaignTraceReturnCard(void)
{
    static const GeoscapeUiCtlSlot aSlots[] = {
        GEOUI_CTL_186B00, GEOUI_CTL_186BB4, GEOUI_CTL_186BB0,
        GEOUI_CTL_18704C, GEOUI_CTL_187024
    };
    static const char *const aNames[] = { "close", "controlA", "controlB", "statusOK", "statusOpen" };
    const UiPanel *pPanel = g_world.ui.pStatusPanel;

    printf("pocketufo: campaign returnack: card lines %u firstFlag %u slots %u/%u/%u/%u pause %u geo %u screen %d modal %d panel %d row %d aux %d panelClick %d\n",
           g_world.msgLines.nLineCount,
           g_world.msgLines.nLineCount != 0 ? (unsigned)g_world.msgLines.abLineFlags[0] : 0u,
           g_world.msgLines.nLineCount != 0 ? (unsigned)g_world.msgLines.anColor[0][0] : 0u,
           g_world.msgLines.nLineCount != 0 ? (unsigned)g_world.msgLines.anColor[0][1] : 0u,
           g_world.msgLines.nLineCount != 0 ? (unsigned)g_world.msgLines.anColor[0][2] : 0u,
           g_world.msgLines.nLineCount != 0 ? (unsigned)g_world.msgLines.anColor[0][3] : 0u,
           (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bGeoscapeUi,
           (int)g_world.screen.nGeoscapeScreen, g_world.sModal.bOpen ? 1 : 0,
           pPanel != NULL, pPanel != NULL ? (int)pPanel->nUnk3032 : -1,
           pPanel != NULL && pPanel->nUnk3032 >= 0 && pPanel->nUnk3032 < pPanel->nLineCount
               ? (int)pPanel->anLineAux[pPanel->nUnk3032] : -1,
           pPanel != NULL && pPanel->pfnClick != NULL);
    fflush(stdout);
    App_ScriptLines();

    for (i32 i = 0; i < (i32)(sizeof(aSlots) / sizeof(aSlots[0])); ++i) {
        UiControl *pCtl = (g_world.ptrEnv.apCtl != NULL) ? g_world.ptrEnv.apCtl[aSlots[i]] : NULL;
        printf("pocketufo: campaign returnack: control %s present %d enabled %d rect %d/%d/%u/%u mode %u state %u callback %d\n",
               aNames[i], pCtl != NULL, pCtl != NULL ? (int)pCtl->bEnabled : 0,
               pCtl != NULL ? (int)pCtl->x : -1, pCtl != NULL ? (int)pCtl->y : -1,
               pCtl != NULL ? (unsigned)pCtl->uWidth : 0u,
               pCtl != NULL ? (unsigned)pCtl->uHeight : 0u,
               pCtl != NULL ? (unsigned)pCtl->nMode : 0u,
               pCtl != NULL ? (unsigned)pCtl->nState : 0u,
               pCtl != NULL && pCtl->pfnClick != NULL);
    }
    fflush(stdout);
}

enum { APP_CAMPAIGN_RETURN_CREW_MAX = 64, APP_CAMPAIGN_RETURN_ITEM_COUNT = 3 };

typedef struct AppCampaignReturnProof {
    i32 nCraftIndex;
    uintptr_t nCarrierKey;
    u8 bCraftId;
    u8 bCraftType;
    u8 bHomeBase;
    u8 bAtBase;
    u8 bIntercepting;
    i32 nTargetIndex;
    i32 nCraftSlot;
    u16 uRangeRemain;
    i16 nFuel;
    i32 nCrewCount;
    i32 nCrewRowCount;
    i32 anCrewRosterIndex[APP_CAMPAIGN_RETURN_CREW_MAX];
    uintptr_t auCrewKey[APP_CAMPAIGN_RETURN_CREW_MAX];
    u8 abCrewAssignment[APP_CAMPAIGN_RETURN_CREW_MAX];
    u8 abCrewType[APP_CAMPAIGN_RETURN_CREW_MAX];
    bool abCrewRemovedOnShutdown[APP_CAMPAIGN_RETURN_CREW_MAX];
    u8 abCrewLandingCredit[APP_CAMPAIGN_RETURN_CREW_MAX];
    i32 anCrewMissionValue[APP_CAMPAIGN_RETURN_CREW_MAX];
    u16 auCrewItemIndex[APP_CAMPAIGN_RETURN_CREW_MAX][APP_PERSISTENT_LOADOUT_SLOTS];
    u8 abCrewAmmoType[APP_CAMPAIGN_RETURN_CREW_MAX][APP_PERSISTENT_LOADOUT_SLOTS];
    u16 auObjectByCraftIndex[APP_CAMPAIGN_RETURN_ITEM_COUNT];
    u16 auObjectByShipSlot[APP_CAMPAIGN_RETURN_ITEM_COUNT];
    i32 anCargo[PUFO_SHIP_CARGO_COLUMNS];
} AppCampaignReturnProof;

static bool App_ScriptCampaignRemovedOnShutdown(const Soldier *pSoldier)
{
    const GameMap *pMap = g_world.pTactMap;
    if (!g_world.bTactLive || pMap == NULL) {
        return false;
    }
    /* Raw 0x3DD0C..0x3DE10 selects side-list records; the global roster can compact.
     * Record the decision while the units are live, before clicking debrief OK. */
    for (i32 i = 0; i < pMap->nEnemyCount; ++i) {
        if (pMap->apEnemyUnits[i] == pSoldier) { return true; }
    }
    for (i32 i = 0; i < pMap->nCivilianCount; ++i) {
        if (pMap->apCivilians[i] == pSoldier) { return pSoldier->bInZone == 0; }
    }
    for (i32 i = 0; i < pMap->nXcomCount; ++i) {
        if (pMap->apXcomUnits[i] == pSoldier) {
            return (pSoldier->nUnitType > 3 && pSoldier->nUnitType <= 8)
                   || pSoldier->bInZone == 0;
        }
    }
    return false;
}

static bool App_ScriptCampaignCaptureReturnProof(const char *pszStage,
                                                 AppCampaignReturnProof *pOut,
                                                 const AppCampaignReturnProof *pBefore)
{
    static const i32 anObjectSlot[APP_CAMPAIGN_RETURN_ITEM_COUNT] = { 4, 17, 38 };
    const i32 nCraftIndex = (i32)g_world.nMapCraftIndex;
    const Ship *pCraft;

    if (pOut == NULL || !g_bWorldReady || g_assets.apObjects == NULL
        || nCraftIndex < 0 || nCraftIndex >= (i32)g_world.bCraftCount
        || nCraftIndex >= PUFO_ITEM_QTY_COUNT || g_assets.nObjectCount <= 38) {
        printf("pocketufo: campaign returnproof: FAIL stage=%s invalid persistent owners\n",
               pszStage != NULL ? pszStage : "unknown");
        fflush(stdout);
        return false;
    }
    pCraft = g_world.apCraft[nCraftIndex];
    if (pCraft == NULL || pCraft->nSlotIdx < 0 || pCraft->nSlotIdx >= PUFO_ITEM_QTY_COUNT) {
        printf("pocketufo: campaign returnproof: FAIL stage=%s craft is missing or has invalid slot\n",
               pszStage != NULL ? pszStage : "unknown");
        fflush(stdout);
        return false;
    }

    memset(pOut, 0, sizeof(*pOut));
    pOut->nCraftIndex = nCraftIndex;
    pOut->nCarrierKey = (uintptr_t)(const void *)pCraft;
    pOut->bCraftId = pCraft->bId;
    pOut->bCraftType = pCraft->bCraftType;
    pOut->bHomeBase = pCraft->bHomeBase;
    pOut->bAtBase = pCraft->bAtBase;
    pOut->bIntercepting = pCraft->bIntercepting;
    pOut->nTargetIndex = pCraft->nStrategIdx;
    pOut->nCraftSlot = pCraft->nSlotIdx;
    pOut->uRangeRemain = pCraft->uRangeRemain;
    pOut->nFuel = pCraft->nFuel;
    pOut->nCrewCount = Craft_CountSoldiersAboard(g_world.apSoldiers,
                                                 (i32)g_world.nSoldierCount,
                                                 nCraftIndex);
    if (pOut->nCrewCount > APP_CAMPAIGN_RETURN_CREW_MAX) {
        printf("pocketufo: campaign returnproof: FAIL stage=%s assigned crew count %d exceeds observer bound\n",
               pszStage != NULL ? pszStage : "unknown", (int)pOut->nCrewCount);
        fflush(stdout);
        return false;
    }
    for (i32 i = 0; i < (i32)g_world.nSoldierCount; ++i) {
        const Soldier *pSoldier = g_world.apSoldiers[i];
        if (pSoldier == NULL) {
            continue;
        }
        bool bTracked = pSoldier->bUnk183 == (u8)nCraftIndex;
        if (pBefore != NULL) {
            for (i32 j = 0; j < pBefore->nCrewRowCount; ++j) {
                if (pBefore->auCrewKey[j] == (uintptr_t)pSoldier) { bTracked = true; break; }
            }
        }
        if (!bTracked) {
            continue;
        }
        if (pOut->nCrewRowCount >= APP_CAMPAIGN_RETURN_CREW_MAX) {
            return false;
        }
        for (i32 j = 0; j < pOut->nCrewRowCount; ++j) {
            if (pOut->auCrewKey[j] == (uintptr_t)(const void *)pSoldier) {
                printf("pocketufo: campaign returnproof: FAIL stage=%s duplicate assigned crew pointer\n",
                       pszStage != NULL ? pszStage : "unknown");
                fflush(stdout);
                return false;
            }
        }
        const i32 nCrewRow = pOut->nCrewRowCount++;
        i32 nLoadoutItems = 0;
        for (i32 nSlot = 0; nSlot < APP_PERSISTENT_LOADOUT_SLOTS; ++nSlot) {
            if (pSoldier->auUnk962[nSlot] != 0) { ++nLoadoutItems; }
        }
        if (nLoadoutItems == 0) {
            printf("pocketufo: campaign returnproof: FAIL stage=%s crew roster %d has no persistent loadout entries\n",
                   pszStage != NULL ? pszStage : "unknown", (int)i);
            fflush(stdout);
            return false;
        }
        pOut->anCrewRosterIndex[nCrewRow] = i;
        pOut->auCrewKey[nCrewRow] = (uintptr_t)(const void *)pSoldier;
        pOut->abCrewAssignment[nCrewRow] = pSoldier->bUnk183;
        pOut->abCrewType[nCrewRow] = pSoldier->nUnitType;
        pOut->abCrewRemovedOnShutdown[nCrewRow] = App_ScriptCampaignRemovedOnShutdown(pSoldier);
        pOut->abCrewLandingCredit[nCrewRow] = pSoldier->bUnk247;
        pOut->anCrewMissionValue[nCrewRow] = pSoldier->nUnk436;
        printf("pocketufo: campaign returnrule stage=%s soldierKey=0x%llX inZone=%u remove=%u credit=%u missionValue=%d\n",
               pszStage != NULL ? pszStage : "unknown",
               (unsigned long long)pOut->auCrewKey[nCrewRow], (unsigned)pSoldier->bInZone,
               pOut->abCrewRemovedOnShutdown[nCrewRow] ? 1u : 0u,
               (unsigned)pSoldier->bUnk247, (int)pSoldier->nUnk436);
        printf("pocketufo: campaign returncrew stage=%s rosterIndex=%d soldierKey=0x%llX "
               "assignedCraft=%u unitType=%u loadoutItems=%d itemIndexAmmo=",
               pszStage != NULL ? pszStage : "unknown", (int)i,
               (unsigned long long)pOut->auCrewKey[nCrewRow],
               (unsigned)pOut->abCrewAssignment[nCrewRow],
               (unsigned)pOut->abCrewType[nCrewRow], nLoadoutItems);
        for (i32 nSlot = 0; nSlot < APP_PERSISTENT_LOADOUT_SLOTS; ++nSlot) {
            pOut->auCrewItemIndex[nCrewRow][nSlot] = pSoldier->auUnk962[nSlot];
            pOut->abCrewAmmoType[nCrewRow][nSlot] = pSoldier->abUnk1012[nSlot];
            printf("%s%d:%u/%u", nSlot == 0 ? "" : ",", (int)nSlot,
                   (unsigned)pSoldier->auUnk962[nSlot],
                   (unsigned)pSoldier->abUnk1012[nSlot]);
        }
        printf("\n");
    }
    if (pBefore == NULL && pOut->nCrewRowCount != pOut->nCrewCount) {
        printf("pocketufo: campaign returnproof: FAIL stage=%s assigned count %d differs from crew rows %d\n",
               pszStage != NULL ? pszStage : "unknown",
               (int)pOut->nCrewCount, (int)pOut->nCrewRowCount);
        fflush(stdout);
        return false;
    }
    for (i32 i = 0; i < APP_CAMPAIGN_RETURN_ITEM_COUNT; ++i) {
        const i32 nObject = anObjectSlot[i];
        const ObjectRecFull *pObject = g_assets.apObjects[nObject];
        if (pObject == NULL) {
            printf("pocketufo: campaign returnproof: FAIL stage=%s object %d is missing\n",
                   pszStage != NULL ? pszStage : "unknown", (int)nObject);
            fflush(stdout);
            return false;
        }
        pOut->auObjectByCraftIndex[i] = pObject->aItemQty[nCraftIndex];
        pOut->auObjectByShipSlot[i] = pObject->aItemQty[pCraft->nSlotIdx];
    }
    for (i32 i = 0; i < PUFO_SHIP_CARGO_COLUMNS; ++i) {
        pOut->anCargo[i] = pCraft->anCargoQty[i];
    }

    printf("pocketufo: campaign returnproof stage=%s scene=%u/%u tact=%u lines=%u modal=%u "
           "craftIndex=%d carrierKey=0x%llX carrierId=%u craftType=%u homeBase=%u atBase=%u "
           "intercept=%u targetIndex=%d craftSlot=%d range=%u fuel=%d crew=%d crewRows=%d loadoutSlots=%d",
           pszStage != NULL ? pszStage : "unknown",
           (unsigned)g_world.bGeoscapeUi, (unsigned)g_world.screen.nGeoscapeScreen,
           (unsigned)g_world.bTactLive, (unsigned)g_world.msgLines.nLineCount,
           g_world.sModal.bOpen ? 1u : 0u, (int)pOut->nCraftIndex,
           (unsigned long long)pOut->nCarrierKey,
           (unsigned)pOut->bCraftId, (unsigned)pOut->bCraftType,
           (unsigned)pOut->bHomeBase, (unsigned)pOut->bAtBase,
           (unsigned)pOut->bIntercepting, (int)pOut->nTargetIndex,
           (int)pOut->nCraftSlot, (unsigned)pOut->uRangeRemain, (int)pOut->nFuel,
           (int)pOut->nCrewCount, (int)pOut->nCrewRowCount,
           APP_PERSISTENT_LOADOUT_SLOTS);
    printf(" item4CraftIndex=%u item4ShipSlot=%u item17CraftIndex=%u item17ShipSlot=%u "
           "item38CraftIndex=%u item38ShipSlot=%u cargo=%d,%d,%d,%d\n",
           (unsigned)pOut->auObjectByCraftIndex[0], (unsigned)pOut->auObjectByShipSlot[0],
           (unsigned)pOut->auObjectByCraftIndex[1], (unsigned)pOut->auObjectByShipSlot[1],
           (unsigned)pOut->auObjectByCraftIndex[2], (unsigned)pOut->auObjectByShipSlot[2],
           (int)pOut->anCargo[0], (int)pOut->anCargo[1],
           (int)pOut->anCargo[2], (int)pOut->anCargo[3]);
    fflush(stdout);
    return true;
}

static bool App_ScriptCampaignReturnProofMatches(const AppCampaignReturnProof *pBefore,
                                                  const AppCampaignReturnProof *pAfter)
{
    /* Raw Tact_Shutdown removes units only through Soldier_RemoveById and the abort-alt
     * branch removes a strategic target; neither writes these surviving Soldiers' 25
     * persistent index/ammo slots. ObjectRec craft-index/ship-slot stock and Ship cargo
     * are logged at both stages, but are allowed to reflect outcome writeback. */
    if (pBefore == NULL || pAfter == NULL
        || pBefore->nCraftIndex != pAfter->nCraftIndex
        || pBefore->nCarrierKey != pAfter->nCarrierKey
        || pBefore->bCraftId != pAfter->bCraftId
        || pBefore->bCraftType != pAfter->bCraftType
        || pBefore->bHomeBase != pAfter->bHomeBase
        || pBefore->nCraftSlot != pAfter->nCraftSlot) {
        return false;
    }
    i32 nSurvivors = 0;
    i32 nAssigned = 0;
    for (i32 i = 0; i < pBefore->nCrewRowCount; ++i) {
        i32 nAfter = -1;
        for (i32 j = 0; j < pAfter->nCrewRowCount; ++j) {
            if (pBefore->auCrewKey[i] == pAfter->auCrewKey[j]) { nAfter = j; break; }
        }
        if (pBefore->abCrewRemovedOnShutdown[i]) {
            if (nAfter >= 0) { return false; }
            continue;
        }
        ++nSurvivors;
        if (nAfter < 0 || pBefore->abCrewType[i] != pAfter->abCrewType[nAfter]) {
            return false;
        }
        const bool bSameAssignment = pBefore->abCrewAssignment[i] == pAfter->abCrewAssignment[nAfter]
            && pBefore->abCrewLandingCredit[i] == pAfter->abCrewLandingCredit[nAfter];
        /* Base_TransferTacticalLoot @0x685CC..0x6861C changes the assignment to254
         * on landing when +0xF7 is zero and +0x1B4 is nonzero. The Soldier survives. */
        const bool bLandingAssignment = pAfter->bAtBase != 0
            && pBefore->abCrewLandingCredit[i] == 0 && pBefore->anCrewMissionValue[i] != 0
            && pAfter->abCrewAssignment[nAfter] == 254
            && pAfter->abCrewLandingCredit[nAfter] == (u8)(40 * pBefore->anCrewMissionValue[i] / 100);
        if (!bSameAssignment && !bLandingAssignment) { return false; }
        if (pAfter->abCrewAssignment[nAfter] == (u8)pAfter->nCraftIndex) { ++nAssigned; }
        for (i32 nSlot = 0; nSlot < APP_PERSISTENT_LOADOUT_SLOTS; ++nSlot) {
            if (pBefore->auCrewItemIndex[i][nSlot] != pAfter->auCrewItemIndex[nAfter][nSlot]
                || pBefore->abCrewAmmoType[i][nSlot] != pAfter->abCrewAmmoType[nAfter][nSlot]) {
                return false;
            }
        }
    }
    return pAfter->nCrewRowCount == nSurvivors && pAfter->nCrewCount == nAssigned;
}

/* Read-only continuation census for the natural carrier return. Unlike the
 * debrief proof above, this can be called at any live geoscape checkpoint. */
static void App_ScriptCampaignHomeObserve(const char *pszStage)
{
    static const i32 anObservedObject[] = { 4, 17, 31, 32, 34, 35, 38, 52, 53, 54, 55, 56, 57, 58, 105 };
    const i32 nCraftIndex = (i32)g_world.nMapCraftIndex;
    const GeoscapeWorld *pGeo = &g_world.geo.world;
    const i32 nShipCount = GeoscapeWorld_GetShipCount(pGeo);
    Ship *pCraft = nCraftIndex >= 0 && nCraftIndex < nShipCount && pGeo->apCraft != NULL
        ? pGeo->apCraft[nCraftIndex] : NULL;
    const StrategList listView = {
        (StrategItem **)g_world.strategList.apItems, g_world.strategList.nCount,
        g_world.nNextDisplayId, g_world.nNextSiteId
    };
    const StrategItem *pMarker = NULL;
    i32 nMarkerIndex = -1;
    i32 nBaseIndex = pCraft != NULL ? (i32)pCraft->bHomeBase : -1;
    const bool bBaseValid = nBaseIndex >= 0 && nBaseIndex < (i32)g_world.nBaseCount
        && nBaseIndex < PUFO_FAC_BASE_COUNT && g_world.apBases[nBaseIndex] != NULL;
    const BaseRec *pBase = bBaseValid ? g_world.apBases[nBaseIndex] : NULL;
    const uintptr_t nHomeBaseKey = (uintptr_t)(const void *)pBase;
    const PadImage *pStrip = g_world.ui.apPad[GEOUI_PAD_186BBC];
    AppSmapItemPoint markerPoint;
    const UiControl *pReturnCtl = g_world.ui.apCtl[GEOUI_CTL_186BA8];
    const i32 nLineFlag = g_world.msgLines.nLineCount > 0
        ? (i32)g_world.msgLines.abLineFlags[0] : -1;
    const i32 nLineCraft = g_world.msgLines.nLineCount > 0
        ? (i32)g_world.msgLines.anColor[0][0] : -1;
    const bool bCraftCard = nLineFlag == PUFO_STATUS_FLAG_CRAFT_DETAIL
        && nLineCraft == nCraftIndex;
    const bool bReturnDismiss = pReturnCtl != NULL && pReturnCtl->pfnClick != NULL
        && pReturnCtl->pfnClick == g_world.frame.panel.pfnDismissSelected;
    const bool bReturnResolve = pReturnCtl != NULL && pReturnCtl->pfnClick != NULL
        && pReturnCtl->pfnClick == g_world.frame.panel.pfnResolveInterceptUI;
    const char *pszReturnCallback = pReturnCtl == NULL || pReturnCtl->pfnClick == NULL
        ? "none" : (bReturnDismiss ? "dismiss-selected"
                    : (bReturnResolve ? "resolve-intercept" : "other"));

    memset(&markerPoint, 0, sizeof(markerPoint));
    markerPoint.nBaseX = -1;
    markerPoint.nY = -1;
    markerPoint.anTryX[0] = -1;
    markerPoint.anTryX[1] = -1;
    markerPoint.anTryX[2] = -1;
    markerPoint.nX = -1;
    if (pCraft != NULL && g_world.strategList.apItems != NULL
        && g_world.strategList.nCount > 0) {
        nMarkerIndex = StrategList_FindByShipId(&listView, pGeo, nCraftIndex);
        if (nMarkerIndex >= 0 && nMarkerIndex < listView.nCount) {
            pMarker = listView.apItems[nMarkerIndex];
        }
    }
    const bool bPointValid = pMarker != NULL
        && App_ScriptSmapItemPoint(&g_world.view, pStrip, pMarker, &markerPoint);

    printf("pocketufo: campaign home-observe stage=%s scene=%u/%u tact=%u modal=%u lines=%u firstFlag=%d lineCraft=%d pause=%u clock=%d lastMinute=%d lastHour=%d tickSec=%d shortfall=%u mapSelect=%u viewMode=%d scroll=%d/%d map=%d/%d stripHeight=%u\n",
           pszStage != NULL ? pszStage : "requested",
           (unsigned)g_world.bGeoscapeUi, (unsigned)g_world.screen.nGeoscapeScreen,
           (unsigned)g_world.bTactLive, g_world.sModal.bOpen ? 1u : 0u,
           (unsigned)g_world.msgLines.nLineCount, nLineFlag, nLineCraft,
           (unsigned)g_world.geo.time.bPaused, (int)g_world.geo.time.nTotalSec,
           (int)g_world.geo.time.nLastTickMinSec, (int)g_world.geo.time.nLastTickHourSec,
           (int)g_world.geo.world.nTickSeconds, (unsigned)g_world.nShortfall1870C8,
           (unsigned)g_world.bMapSelectionFlag,
           (int)g_world.view.nMode, (int)g_world.view.nScrollX, (int)g_world.view.nScrollY,
           (int)g_world.view.nMapW, (int)g_world.view.nMapH,
           pStrip != NULL ? (unsigned)pStrip->uHeight : 0u);
    printf("pocketufo: campaign home-carrier stage=%s present=%d craftIndex=%d key=0x%llX id=%u type=%u homeBase=%u baseKey=0x%llX atBase=%u intercept=%u targetIndex=%d weaponsHot=%u contactMsg=%u refueling=%u needsSupply=%u range=%u/%u rate=%u fuel=%d/%u mount=%d/%d ammo25=%u ammo26=%u ammo27=%u ammo29=%u ammo30=%u crewAboard=%d hangar=%u/%u slot=%d cargo=%d,%d,%d,%d basePresent=%d baseXY=%d/%d\n",
           pszStage != NULL ? pszStage : "requested", pCraft != NULL, nCraftIndex,
           (unsigned long long)(uintptr_t)pCraft,
           pCraft != NULL ? (unsigned)pCraft->bId : 0u,
           pCraft != NULL ? (unsigned)pCraft->bCraftType : 0u,
           pCraft != NULL ? (unsigned)pCraft->bHomeBase : 0u,
           (unsigned long long)nHomeBaseKey,
           pCraft != NULL ? (unsigned)pCraft->bAtBase : 0u,
           pCraft != NULL ? (unsigned)pCraft->bIntercepting : 0u,
           pCraft != NULL ? (int)pCraft->nStrategIdx : -1,
           pCraft != NULL ? (unsigned)pCraft->bWeaponsHot : 0u,
           pCraft != NULL ? (unsigned)pCraft->bContactMsg : 0u,
           pCraft != NULL ? (unsigned)pCraft->bRefueling : 0u,
           pCraft != NULL ? (unsigned)pCraft->bNeedsSupply : 0u,
           pCraft != NULL ? (unsigned)pCraft->uRangeRemain : 0u,
           pCraft != NULL ? (unsigned)pCraft->uRangeMax : 0u,
           pCraft != NULL ? (unsigned)pCraft->uRangeRate : 0u,
           pCraft != NULL ? (int)pCraft->nFuel : 0,
           pCraft != NULL ? (unsigned)pCraft->uFuelMax : 0u,
           pCraft != NULL ? (int)pCraft->bWeaponL : 0,
           pCraft != NULL ? (int)pCraft->bWeaponR : 0,
           pCraft != NULL ? (unsigned)pCraft->bAmmo25 : 0u,
           pCraft != NULL ? (unsigned)pCraft->bAmmo26 : 0u,
           pCraft != NULL ? (unsigned)pCraft->bAmmo27 : 0u,
           pCraft != NULL ? (unsigned)pCraft->bAmmo29 : 0u,
           pCraft != NULL ? (unsigned)pCraft->bAmmo30 : 0u,
           pCraft != NULL ? Craft_CountSoldiersAboard(g_world.apSoldiers,
                                                       (i32)g_world.nSoldierCount,
                                                       nCraftIndex) : 0,
           pCraft != NULL ? (unsigned)pCraft->bHangarX : 0u,
           pCraft != NULL ? (unsigned)pCraft->bHangarY : 0u,
           pCraft != NULL ? (int)pCraft->nSlotIdx : -1,
           pCraft != NULL ? (int)pCraft->anCargoQty[0] : 0,
           pCraft != NULL ? (int)pCraft->anCargoQty[1] : 0,
           pCraft != NULL ? (int)pCraft->anCargoQty[2] : 0,
           pCraft != NULL ? (int)pCraft->anCargoQty[3] : 0,
           pBase != NULL, pBase != NULL ? (int)pBase->nUnk8 : 0,
           pBase != NULL ? (int)pBase->nUnk12 : 0);
    printf("pocketufo: campaign home-marker stage=%s present=%d index=%d seed=%d type=%u active=%u display=%d visible=%u at=%d/%d timer=%d start=%d/%d dest=%d/%d target=%d/%d targetIndex=%d phase=%d sub=%u travelMin=%u travelEpoch=%d travelStart=%d dir=%d hitPoint=%d point=%d/%d baseX=%d candidates=%d/%d/%d stripHeight=%u\n",
           pszStage != NULL ? pszStage : "requested", pMarker != NULL, nMarkerIndex,
           pMarker != NULL ? (int)pMarker->nSeed : -1,
           pMarker != NULL ? (unsigned)pMarker->nType : 0u,
           pMarker != NULL ? (unsigned)pMarker->bActive : 0u,
           pMarker != NULL ? (int)pMarker->nDisplayId : -1,
           pMarker != NULL ? (unsigned)pMarker->bVisible : 0u,
           pMarker != NULL ? (int)pMarker->nX : -1,
           pMarker != NULL ? (int)pMarker->nY : -1,
           pMarker != NULL ? (int)pMarker->nTimer : -1,
           pMarker != NULL ? (int)pMarker->nStartX : -1,
           pMarker != NULL ? (int)pMarker->nStartY : -1,
           pMarker != NULL ? (int)pMarker->nDestX : -1,
           pMarker != NULL ? (int)pMarker->nDestY : -1,
           pMarker != NULL ? (int)pMarker->nTargetX : -1,
           pMarker != NULL ? (int)pMarker->nTargetY : -1,
           pMarker != NULL ? (int)pMarker->nTargetIdx : -1,
           pMarker != NULL ? (int)pMarker->nPhase : -1,
           pMarker != NULL ? (unsigned)pMarker->nMissionSub : 0u,
           pMarker != NULL ? (unsigned)pMarker->uTravelMin : 0u,
           pMarker != NULL ? pMarker->nTravelEpochSec : -1,
           pMarker != NULL ? pMarker->nTravelStartSec : -1,
           pMarker != NULL ? (int)pMarker->bTravelDir : 0,
           bPointValid ? 1 : 0, bPointValid ? markerPoint.nX : -1,
           bPointValid ? markerPoint.nY : -1, markerPoint.nBaseX,
           markerPoint.anTryX[0], markerPoint.anTryX[1], markerPoint.anTryX[2],
           markerPoint.nStripHeight);
    {
        const i32 nExpectedLineCraft = pCraft != NULL ? nCraftIndex : -1;
        const bool bReturnCallbackExpected = pCraft != NULL && pCraft->bContactMsg == 0
            ? bReturnDismiss : (pCraft != NULL && bReturnResolve);
        const bool bReturnCallbackMatch = bCraftCard && bReturnCallbackExpected;
        const bool bReturnRectValid = pReturnCtl != NULL && pReturnCtl->uWidth != 0
            && pReturnCtl->uHeight != 0 && pReturnCtl->x >= 0 && pReturnCtl->y >= 0
            && (i32)pReturnCtl->x + (i32)pReturnCtl->uWidth <= 240
            && (i32)pReturnCtl->y + (i32)pReturnCtl->uHeight <= 320;
        const bool bReturnVisibleCandidate = bCraftCard && bReturnRectValid
            && pReturnCtl->pfnClick != NULL;
        const i32 nReturnX = pReturnCtl != NULL
            ? (i32)pReturnCtl->x + (i32)pReturnCtl->uWidth / 2 : -1;
        const i32 nReturnY = pReturnCtl != NULL
            ? (i32)pReturnCtl->y + (i32)pReturnCtl->uHeight / 2 : -1;
        printf("pocketufo: campaign home-return-control stage=%s card=%d cardCraft=%d expectedCraft=%d contactMsg=%u visibleCandidate=%d rectValid=%d present=%d enabled=%u rect=%d/%d/%u/%u center=%d/%d mode=%u state=%u callback=%s expectedCallback=%s callbackMatch=%d\n",
               pszStage != NULL ? pszStage : "requested", bCraftCard ? 1 : 0,
               nLineCraft, nExpectedLineCraft,
               pCraft != NULL ? (unsigned)pCraft->bContactMsg : 0u,
               bReturnVisibleCandidate ? 1 : 0, bReturnRectValid ? 1 : 0,
               pReturnCtl != NULL, pReturnCtl != NULL ? (unsigned)pReturnCtl->bEnabled : 0u,
               pReturnCtl != NULL ? (int)pReturnCtl->x : -1,
               pReturnCtl != NULL ? (int)pReturnCtl->y : -1,
               pReturnCtl != NULL ? (unsigned)pReturnCtl->uWidth : 0u,
               pReturnCtl != NULL ? (unsigned)pReturnCtl->uHeight : 0u,
               nReturnX, nReturnY,
               pReturnCtl != NULL ? (unsigned)pReturnCtl->nMode : 0u,
               pReturnCtl != NULL ? (unsigned)pReturnCtl->nState : 0u,
               pszReturnCallback,
               pCraft == NULL ? "none" : (pCraft->bContactMsg == 0
                   ? "dismiss-selected" : "resolve-intercept"),
               bReturnCallbackMatch ? 1 : 0);
    }
    if (g_assets.apObjects != NULL && pCraft != NULL) {
        const i32 nShipSlot = (pCraft->nSlotIdx >= 0 && pCraft->nSlotIdx < PUFO_ITEM_QTY_COUNT)
            ? pCraft->nSlotIdx : -1;
        for (i32 i = 0; i < (i32)(sizeof(anObservedObject) / sizeof(anObservedObject[0])); ++i) {
            const i32 nObject = anObservedObject[i];
            const ObjectRecFull *pObject = nObject >= 0 && nObject < (i32)g_assets.nObjectCount
                ? g_assets.apObjects[nObject] : NULL;
            const i32 nBaseQty = pObject != NULL && bBaseValid
                ? (i32)pObject->auPerBaseVal[nBaseIndex] : -1;
            const i32 nCraftQty = pObject != NULL && nCraftIndex < PUFO_ITEM_QTY_COUNT
                ? (i32)pObject->aItemQty[nCraftIndex] : -1;
            const i32 nShipQty = pObject != NULL && nShipSlot >= 0
                ? (i32)pObject->aItemQty[nShipSlot] : -1;
            printf("pocketufo: campaign home-stock stage=%s object=%d present=%d base=%d craftIndex=%d shipSlot=%d\n",
                   pszStage != NULL ? pszStage : "requested", nObject, pObject != NULL,
                   nBaseQty, nCraftQty, nShipQty);
        }
    } else {
        printf("pocketufo: campaign home-stock stage=%s ownerMissing objects=%d craft=%d\n",
               pszStage != NULL ? pszStage : "requested", g_assets.apObjects != NULL,
               pCraft != NULL);
    }
    if (g_world.nSoldierCount < 0) {
        printf("pocketufo: campaign home-crew stage=%s ownerMissing soldiers=%d count=%d\n",
               pszStage != NULL ? pszStage : "requested", 1,
               (int)g_world.nSoldierCount);
    } else {
        const i32 nRosterShown = g_world.nSoldierCount < APP_CAMPAIGN_RETURN_CREW_MAX
            ? (i32)g_world.nSoldierCount : APP_CAMPAIGN_RETURN_CREW_MAX;
        printf("pocketufo: campaign home-roster stage=%s total=%d shown=%d truncated=%d\n",
               pszStage != NULL ? pszStage : "requested", (int)g_world.nSoldierCount,
               (int)nRosterShown, g_world.nSoldierCount > APP_CAMPAIGN_RETURN_CREW_MAX);
        for (i32 i = 0; i < nRosterShown; ++i) {
            const Soldier *pSoldier = g_world.apSoldiers[i];
            if (pSoldier == NULL) {
                printf("pocketufo: campaign home-crew stage=%s rosterIndex=%d present=0\n",
                       pszStage != NULL ? pszStage : "requested", (int)i);
                continue;
            }
            i32 nLoadoutItems = 0;
            for (i32 nSlot = 0; nSlot < APP_PERSISTENT_LOADOUT_SLOTS; ++nSlot) {
                if (pSoldier->auUnk962[nSlot] != 0) {
                    ++nLoadoutItems;
                }
            }
            printf("pocketufo: campaign home-crew stage=%s rosterIndex=%d present=1 soldierKey=0x%llX id=%d base=%u assignment=%u unitType=%u credit=%u missionValue=%d hp=%u hpSeed=%u field8=%u loadoutItems=%d items=",
                   pszStage != NULL ? pszStage : "requested", (int)i,
                   (unsigned long long)(uintptr_t)pSoldier,
                   (int)pSoldier->nSaveId, (unsigned)pSoldier->nKind,
                   (unsigned)pSoldier->bUnk183, (unsigned)pSoldier->nUnitType,
                   (unsigned)pSoldier->bUnk247, (int)pSoldier->nUnk436,
                   (unsigned)pSoldier->uHp, (unsigned)pSoldier->auStat[1],
                   (unsigned)pSoldier->uStunOrArmor, (int)nLoadoutItems);
            for (i32 nSlot = 0; nSlot < APP_PERSISTENT_LOADOUT_SLOTS; ++nSlot) {
                printf("%s%d:%u/%u", nSlot == 0 ? "" : ",", (int)nSlot,
                       (unsigned)pSoldier->auUnk962[nSlot],
                       (unsigned)pSoldier->abUnk1012[nSlot]);
            }
            printf("\n");
        }
    }
    fflush(stdout);
}

/* A radar contact pauses only the geoscape clock. Consume exactly that visible
 * status card through its own enabled confirm control; do not use the keyboard
 * confirm path, which closes the geoscape UI. */
static bool App_ScriptCampaignAckConfirm(void)
{
    UiControl *pConfirm;
    i32 nFlag = -1;

    if (!g_bWorldReady || g_world.bGeoscapeUi == 0
        || g_world.screen.nGeoscapeScreen != 0 || g_world.bTactLive != 0
        || World_bTactPointerLive(&g_world) || g_world.sModal.bOpen != 0) {
        printf("pocketufo: campaign ack-confirm: FAIL stage=scene geo=%u screen=%d tact=%u tactOwner=%d modal=%u lines=%u pause=%u\n",
               (unsigned)g_world.bGeoscapeUi, (int)g_world.screen.nGeoscapeScreen,
               (unsigned)g_world.bTactLive, World_bTactPointerLive(&g_world) ? 1 : 0,
               g_world.sModal.bOpen ? 1u : 0u, (unsigned)g_world.msgLines.nLineCount,
               (unsigned)g_world.geo.time.bPaused);
        App_ScriptLines();
        fflush(stdout);
        return false;
    }
    if (g_world.msgLines.nLineCount > 0) {
        nFlag = (i32)g_world.msgLines.abLineFlags[0];
    }
    if (g_world.msgLines.nLineCount == 0u && g_world.geo.time.bPaused == 0
        && g_world.bMapSelectionFlag == 0) {
        printf("pocketufo: campaign ack-confirm: SKIP no_flag10 screen=%d pause=%u selection=%u\n",
               (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.geo.time.bPaused,
               (unsigned)g_world.bMapSelectionFlag);
        fflush(stdout);
        return true;
    }
    if (g_world.msgLines.nLineCount != 1u || nFlag != PUFO_STATUS_SELFLAG
        || g_world.geo.time.bPaused == 0 || g_world.bMapSelectionFlag != 0) {
        printf("pocketufo: campaign ack-confirm: FAIL stage=line-contract lines=%u flag=%d pause=%u selection=%u expectedFlag=%d\n",
               (unsigned)g_world.msgLines.nLineCount, nFlag,
               (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag,
               PUFO_STATUS_SELFLAG);
        App_ScriptLines();
        fflush(stdout);
        return false;
    }

    pConfirm = g_world.ui.apCtl[GEOUI_CTL_186BA4];
    const bool bOwnerMatch = pConfirm != NULL
        && pConfirm == g_world.frame.panel.pCtlConfirm;
    const bool bCallbackMatch = pConfirm != NULL && pConfirm->pfnClick != NULL
        && pConfirm->pfnClick == g_world.frame.panel.pfnSelectTarget;
    const bool bRectValid = pConfirm != NULL && pConfirm->bEnabled != 0
        && pConfirm->uWidth != 0 && pConfirm->uHeight != 0
        && pConfirm->x >= 0 && pConfirm->y >= 0
        && (i32)pConfirm->x + (i32)pConfirm->uWidth <= 240
        && (i32)pConfirm->y + (i32)pConfirm->uHeight <= 320;
    const i32 nCenterX = pConfirm != NULL
        ? (i32)pConfirm->x + (i32)pConfirm->uWidth / 2 : -1;
    const i32 nCenterY = pConfirm != NULL
        ? (i32)pConfirm->y + (i32)pConfirm->uHeight / 2 : -1;
    printf("pocketufo: campaign ack-confirm: before flag=%d lineCount=%u pause=%u selection=%u screen=%d geo=%u tact=%u owner=%d modal=%u confirmOwner=%d enabled=%u rect=%d/%d/%u/%u center=%d/%d callback=%d callbackMatch=%d tickSec=%d\n",
           nFlag, (unsigned)g_world.msgLines.nLineCount,
           (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag,
           (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bGeoscapeUi,
           (unsigned)g_world.bTactLive, World_bTactPointerLive(&g_world) ? 1 : 0,
           g_world.sModal.bOpen ? 1u : 0u, bOwnerMatch ? 1 : 0,
           pConfirm != NULL ? (unsigned)pConfirm->bEnabled : 0u,
           pConfirm != NULL ? (int)pConfirm->x : -1,
           pConfirm != NULL ? (int)pConfirm->y : -1,
           pConfirm != NULL ? (unsigned)pConfirm->uWidth : 0u,
           pConfirm != NULL ? (unsigned)pConfirm->uHeight : 0u,
           nCenterX, nCenterY, pConfirm != NULL && pConfirm->pfnClick != NULL,
           bCallbackMatch ? 1 : 0, (int)g_world.geo.world.nTickSeconds);
    fflush(stdout);
    if (!bOwnerMatch || !bCallbackMatch || !bRectValid) {
        printf("pocketufo: campaign ack-confirm: FAIL stage=confirm-control contract\n");
        App_ScriptLines();
        fflush(stdout);
        return false;
    }

    /* This goes through the same pointer input path as the window; no pfnClick is called here. */
    App_ScriptPointerLogical(nCenterX, nCenterY, true);
    App_OnPointerUp();
    App_DrawFrame();

    const i32 nAfterFlag = g_world.msgLines.nLineCount > 0
        ? (i32)g_world.msgLines.abLineFlags[0] : -1;
    const bool bAcked = g_world.bGeoscapeUi != 0
        && g_world.screen.nGeoscapeScreen == 0 && g_world.bTactLive == 0
        && !World_bTactPointerLive(&g_world) && g_world.sModal.bOpen == 0
        && g_world.msgLines.nLineCount == 0u && g_world.geo.time.bPaused == 0
        && g_world.bMapSelectionFlag == 0 && g_world.geo.world.nTickSeconds == 5;
    printf("pocketufo: campaign ack-confirm: after lineCount=%u firstFlag=%d pause=%u selection=%u screen=%d geo=%u tact=%u modal=%u tickSec=%d result=%s\n",
           (unsigned)g_world.msgLines.nLineCount, nAfterFlag,
           (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag,
           (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bGeoscapeUi,
           (unsigned)g_world.bTactLive, g_world.sModal.bOpen ? 1u : 0u,
           (int)g_world.geo.world.nTickSeconds, bAcked ? "PASS" : "FAIL");
    App_ScriptLines();
    fflush(stdout);
    return bAcked;
}

/* Clear only one approved live status card through its installed open control. */
static bool App_ScriptCampaignAckStatus(i32 nExpectedFlag)
{
    UiControl *pOpen;
    i32 nFlag = -1;

    if (nExpectedFlag != 34 && nExpectedFlag != 37
        && nExpectedFlag != 38 && nExpectedFlag != 41) {
        printf("pocketufo: campaign ack-status: FAIL stage=argument expected=%d\n",
               nExpectedFlag);
        fflush(stdout);
        return false;
    }
    if (g_world.msgLines.nLineCount > 0) {
        nFlag = (i32)g_world.msgLines.abLineFlags[0];
    }
    if (!g_bWorldReady || g_world.bGeoscapeUi == 0
        || g_world.screen.nGeoscapeScreen != 0 || g_world.bTactLive != 0
        || World_bTactPointerLive(&g_world) || g_world.sModal.bOpen != 0
        || g_world.geo.time.bPaused == 0 || g_world.bMapSelectionFlag != 0
        || g_world.msgLines.nLineCount != 1u || nFlag != nExpectedFlag) {
        printf("pocketufo: campaign ack-status: FAIL stage=line-contract expected=%d flag=%d lineCount=%u pause=%u selection=%u screen=%d geo=%u tact=%u tactOwner=%d modal=%u\n",
               nExpectedFlag, nFlag, (unsigned)g_world.msgLines.nLineCount,
               (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag,
               (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bGeoscapeUi,
               (unsigned)g_world.bTactLive, World_bTactPointerLive(&g_world) ? 1 : 0,
               g_world.sModal.bOpen ? 1u : 0u);
        App_ScriptLines();
        fflush(stdout);
        return false;
    }

    pOpen = g_world.ui.apCtl[GEOUI_CTL_187024];
    const bool bOwnerMatch = pOpen != NULL && pOpen == g_world.frame.panel.pCtlOpen;
    const bool bCallbackMatch = pOpen != NULL && pOpen->pfnClick != NULL
        && pOpen->pfnClick == g_world.frame.panel.pfnCtlOpen;
    const bool bRectValid = pOpen != NULL && pOpen->bEnabled != 0
        && pOpen->uWidth != 0 && pOpen->uHeight != 0
        && pOpen->x >= 0 && pOpen->y >= 0
        && (i32)pOpen->x + (i32)pOpen->uWidth <= 240
        && (i32)pOpen->y + (i32)pOpen->uHeight <= 320;
    const i32 nCenterX = pOpen != NULL
        ? (i32)pOpen->x + (i32)pOpen->uWidth / 2 : -1;
    const i32 nCenterY = pOpen != NULL
        ? (i32)pOpen->y + (i32)pOpen->uHeight / 2 : -1;
    printf("pocketufo: campaign ack-status: before expected=%d flag=%d lineCount=%u pause=%u selection=%u screen=%d geo=%u tact=%u tactOwner=%d modal=%u control=GEOUI_CTL_187024 owner=%d enabled=%u rect=%d/%d/%u/%u center=%d/%d callbackMatch=%d tickSec=%d\n",
           nExpectedFlag, nFlag, (unsigned)g_world.msgLines.nLineCount,
           (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag,
           (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bGeoscapeUi,
           (unsigned)g_world.bTactLive, World_bTactPointerLive(&g_world) ? 1 : 0,
           g_world.sModal.bOpen ? 1u : 0u, bOwnerMatch ? 1 : 0,
           pOpen != NULL ? (unsigned)pOpen->bEnabled : 0u,
           pOpen != NULL ? (int)pOpen->x : -1,
           pOpen != NULL ? (int)pOpen->y : -1,
           pOpen != NULL ? (unsigned)pOpen->uWidth : 0u,
           pOpen != NULL ? (unsigned)pOpen->uHeight : 0u,
           nCenterX, nCenterY, bCallbackMatch ? 1 : 0,
           (int)g_world.geo.world.nTickSeconds);
    fflush(stdout);
    if (!bOwnerMatch || !bCallbackMatch || !bRectValid) {
        printf("pocketufo: campaign ack-status: FAIL stage=control-contract expected=%d\n",
               nExpectedFlag);
        App_ScriptLines();
        fflush(stdout);
        return false;
    }

    /* Use the ordinary pointer route; the installed callback owns line removal and unpause. */
    App_ScriptPointerLogical(nCenterX, nCenterY, true);
    App_OnPointerUp();
    App_DrawFrame();

    const i32 nAfterFlag = g_world.msgLines.nLineCount > 0
        ? (i32)g_world.msgLines.abLineFlags[0] : -1;
    const bool bAcked = g_world.msgLines.nLineCount == 0u
        && nAfterFlag == -1 && g_world.geo.time.bPaused == 0
        && g_world.bMapSelectionFlag == 0 && g_world.screen.nGeoscapeScreen == 0
        && g_world.bGeoscapeUi != 0 && g_world.bTactLive == 0
        && !World_bTactPointerLive(&g_world) && g_world.sModal.bOpen == 0
        && g_world.geo.world.nTickSeconds == 5;
    printf("pocketufo: campaign ack-status: after expected=%d lineCount=%u firstFlag=%d pause=%u selection=%u screen=%d geo=%u tact=%u modal=%u tickSec=%d result=%s\n",
           nExpectedFlag, (unsigned)g_world.msgLines.nLineCount, nAfterFlag,
           (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag,
           (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bGeoscapeUi,
           (unsigned)g_world.bTactLive, g_world.sModal.bOpen ? 1u : 0u,
           (int)g_world.geo.world.nTickSeconds, bAcked ? "PASS" : "FAIL");
    App_ScriptLines();
    fflush(stdout);
    return bAcked;
}

/* Send the current original-kit carrier home through the selected craft's live
 * status card. This helper sends real pointer events only; the original input
 * handlers own any resulting Ship, marker, clock, or line-list changes. */
static bool App_ScriptCampaignHomeOrder(void)
{
    const i32 nCraftIndex = (i32)g_world.nMapCraftIndex;
    const GeoscapeWorld *pGeo = &g_world.geo.world;
    const i32 nShipCount = GeoscapeWorld_GetShipCount(pGeo);
    const PadImage *pStrip = g_world.ui.apPad[GEOUI_PAD_186BBC];
    const i32 nLineCountBefore = (i32)g_world.msgLines.nLineCount;
    Ship *pCraft = nCraftIndex >= 0 && nCraftIndex < nShipCount && pGeo->apCraft != NULL
        ? pGeo->apCraft[nCraftIndex] : NULL;
    const i32 nBaseIndex = pCraft != NULL ? (i32)pCraft->bHomeBase : -1;
    const BaseRec *pBase = nBaseIndex >= 0 && nBaseIndex < (i32)g_world.nBaseCount
        && nBaseIndex < PUFO_FAC_BASE_COUNT && g_world.apBases[nBaseIndex] != NULL
        ? g_world.apBases[nBaseIndex] : NULL;
    StrategList listView = {
        (StrategItem **)g_world.strategList.apItems, g_world.strategList.nCount,
        g_world.nNextDisplayId, g_world.nNextSiteId
    };
    i32 nMarkerIndex = -1;
    StrategItem *pMarker = NULL;
    AppSmapItemPoint markerPoint;
    bool bPointValid;
    uintptr_t nCarrierKey, nHomeBaseKey;
    u8 bCarrierId, bMarkerSeed, bMarkerSub;
    UiControl *pReturnCtl;
    i32 nReturnX, nReturnY;
    const i32 nLineFlagBefore = nLineCountBefore > 0
        ? (i32)g_world.msgLines.abLineFlags[0] : -1;

    if (!g_bWorldReady || g_world.bGeoscapeUi == 0
        || g_world.screen.nGeoscapeScreen != 0 || g_world.bTactLive != 0
        || World_bTactPointerLive(&g_world) || g_world.sModal.bOpen != 0
        || g_world.geo.time.bPaused != 0 || g_world.bMapSelectionFlag != 0
        || nLineCountBefore != 0 || pCraft == NULL || pBase == NULL
        || pCraft->bAtBase != 0 || pCraft->bIntercepting != 0
        || pCraft->bContactMsg != 0
        || g_world.strategList.apItems == NULL || g_world.strategList.nCount <= 0
        || pStrip == NULL || g_world.view.nMode != 0) {
        printf("pocketufo: campaign home-order: FAIL stage=precondition scene=%u/%u tact=%u tactOwner=%d modal=%u lines=%d firstFlag=%d pause=%u selection=%u craft=%d atBase=%u intercept=%u contactMsg=%u base=%d list=%d viewMode=%d strip=%d\n",
               (unsigned)g_world.bGeoscapeUi, (unsigned)g_world.screen.nGeoscapeScreen,
               (unsigned)g_world.bTactLive, World_bTactPointerLive(&g_world) ? 1 : 0,
               g_world.sModal.bOpen ? 1u : 0u, nLineCountBefore, nLineFlagBefore,
               (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag,
               pCraft != NULL, pCraft != NULL ? (unsigned)pCraft->bAtBase : 0u,
               pCraft != NULL ? (unsigned)pCraft->bIntercepting : 0u,
               pCraft != NULL ? (unsigned)pCraft->bContactMsg : 0u, pBase != NULL,
               g_world.strategList.nCount,
               (int)g_world.view.nMode, pStrip != NULL);
        App_ScriptCampaignHomeObserve("home_order_precondition_fail");
        fflush(stdout);
        return false;
    }

    nMarkerIndex = StrategList_FindByShipId(&listView, pGeo, nCraftIndex);
    if (nMarkerIndex < 0 || nMarkerIndex >= listView.nCount
        || listView.apItems[nMarkerIndex] == NULL) {
        printf("pocketufo: campaign home-order: FAIL stage=marker-lookup index=%d count=%d\n",
               nMarkerIndex, listView.nCount);
        App_ScriptCampaignHomeObserve("home_order_marker_lookup_fail");
        fflush(stdout);
        return false;
    }
    pMarker = listView.apItems[nMarkerIndex];
    if (pMarker->nType != 1 || pMarker->bActive != 0 || pMarker->bVisible == 0
        || pMarker->nDisplayId != (i32)pCraft->bId) {
        printf("pocketufo: campaign home-order: FAIL stage=marker-identity index=%d seed=%d type=%u active=%u visible=%u display=%d carrierId=%u\n",
               nMarkerIndex, (int)pMarker->nSeed, (unsigned)pMarker->nType,
               (unsigned)pMarker->bActive, (unsigned)pMarker->bVisible,
               (int)pMarker->nDisplayId, (unsigned)pCraft->bId);
        App_ScriptCampaignHomeObserve("home_order_marker_identity_fail");
        fflush(stdout);
        return false;
    }
    bPointValid = App_ScriptSmapItemPoint(&g_world.view, pStrip, pMarker, &markerPoint);
    if (!bPointValid) {
        printf("pocketufo: campaign home-order: FAIL stage=marker-point index=%d point=%d/%d mode=%d scroll=%d/%d map=%d/%d strip=%u item=%d/%d candidates=%d/%d/%d\n",
               nMarkerIndex, markerPoint.nX, markerPoint.nY, (int)g_world.view.nMode,
               (int)g_world.view.nScrollX, (int)g_world.view.nScrollY,
               (int)g_world.view.nMapW, (int)g_world.view.nMapH,
               (unsigned)markerPoint.nStripHeight, (int)pMarker->nX, (int)pMarker->nY,
               markerPoint.anTryX[0], markerPoint.anTryX[1], markerPoint.anTryX[2]);
        App_ScriptCampaignHomeObserve("home_order_marker_point_fail");
        fflush(stdout);
        return false;
    }

    nCarrierKey = (uintptr_t)(const void *)pCraft;
    nHomeBaseKey = (uintptr_t)(const void *)pBase;
    bCarrierId = pCraft->bId;
    bMarkerSeed = pMarker->nSeed;
    bMarkerSub = pMarker->nMissionSub;
    printf("pocketufo: campaign home-order input=marker stage=before craftIndex=%d carrierKey=0x%llX carrierId=%u baseKey=0x%llX markerIndex=%d markerSeed=%u markerSub=%u point=%d/%d marker=%d/%d\n",
           nCraftIndex, (unsigned long long)nCarrierKey, (unsigned)bCarrierId,
           (unsigned long long)nHomeBaseKey, nMarkerIndex, (unsigned)bMarkerSeed,
           (unsigned)bMarkerSub,
           markerPoint.nX, markerPoint.nY,
           (int)pMarker->nX, (int)pMarker->nY);
    App_ScriptPointerLogical(markerPoint.nX, markerPoint.nY, true);
    App_OnPointerUp();
    App_DrawFrame();

    const i32 nShipCountAfterMarker = GeoscapeWorld_GetShipCount(pGeo);
    pCraft = nCraftIndex >= 0 && nCraftIndex < nShipCountAfterMarker
        && pGeo->apCraft != NULL ? pGeo->apCraft[nCraftIndex] : NULL;
    pBase = pCraft != NULL && nBaseIndex >= 0 && nBaseIndex < (i32)g_world.nBaseCount
        && nBaseIndex < PUFO_FAC_BASE_COUNT && g_world.apBases[nBaseIndex] != NULL
        ? g_world.apBases[nBaseIndex] : NULL;
    listView.apItems = (pCraft != NULL && g_world.strategList.apItems != NULL)
        ? (StrategItem **)g_world.strategList.apItems : NULL;
    listView.nCount = g_world.strategList.nCount;
    nMarkerIndex = (pCraft != NULL && listView.apItems != NULL)
        ? StrategList_FindByShipId(&listView, pGeo, nCraftIndex) : -1;
    pMarker = nMarkerIndex >= 0 && nMarkerIndex < listView.nCount
        ? listView.apItems[nMarkerIndex] : NULL;
    if (pCraft == NULL || pBase == NULL
        || (uintptr_t)(const void *)pBase != nHomeBaseKey
        || pCraft->bHomeBase != (u8)nBaseIndex
        || (uintptr_t)(const void *)pCraft != nCarrierKey
        || pCraft->bId != bCarrierId || pCraft->bAtBase != 0
        || pCraft->bIntercepting != 0 || pCraft->bContactMsg != 0
        || pMarker == NULL || pMarker->nSeed != bMarkerSeed
        || pMarker->nType != 1 || pMarker->bActive != 0 || pMarker->bVisible == 0
        || pMarker->nMissionSub != bMarkerSub
        || pMarker->nDisplayId != (i32)bCarrierId
        || g_world.msgLines.nLineCount != 1u
        || g_world.msgLines.abLineFlags[0] != PUFO_STATUS_FLAG_CRAFT_DETAIL
        || g_world.msgLines.anColor[0][0] != (u32)nCraftIndex
        || g_world.geo.time.bPaused == 0 || g_world.bMapSelectionFlag != 0
        || g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bTactLive != 0 || World_bTactPointerLive(&g_world)
        || g_world.sModal.bOpen != 0) {
        printf("pocketufo: campaign home-order: FAIL stage=marker-input-result carrierPresent=%d carrierKey=0x%llX carrierId=%u markerIndex=%d markerSeed=%u lineCount=%u lineFlag=%u lineCraft=%u pause=%u\n",
               pCraft != NULL, (unsigned long long)(uintptr_t)pCraft,
               pCraft != NULL ? (unsigned)pCraft->bId : 0u,
               nMarkerIndex, pMarker != NULL ? (unsigned)pMarker->nSeed : 0u,
               (unsigned)g_world.msgLines.nLineCount,
               g_world.msgLines.nLineCount != 0
                   ? (unsigned)g_world.msgLines.abLineFlags[0] : 0u,
               g_world.msgLines.nLineCount != 0
                   ? (unsigned)g_world.msgLines.anColor[0][0] : 0u,
               (unsigned)g_world.geo.time.bPaused);
        App_ScriptCampaignHomeObserve("home_order_marker_input_fail");
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign home-order input=marker stage=after lineFlag=%u lineCraft=%u pause=%u carrierId=%u markerSeed=%u markerSub=%u\n",
           (unsigned)g_world.msgLines.abLineFlags[0],
           (unsigned)g_world.msgLines.anColor[0][0],
           (unsigned)g_world.geo.time.bPaused, (unsigned)pCraft->bId,
           (unsigned)pMarker->nSeed, (unsigned)pMarker->nMissionSub);
    fflush(stdout);

    /* Re-read the current control after the paint installed the craft-card branch. */
    pReturnCtl = g_world.ui.apCtl[GEOUI_CTL_186BA8];
    if (pBase == NULL || (uintptr_t)(const void *)pBase != nHomeBaseKey
        || pCraft->bContactMsg != 0 || pReturnCtl == NULL
        || pReturnCtl != g_world.frame.panel.pCtlReturnBase
        || pReturnCtl->bEnabled == 0 || pReturnCtl->pfnClick == NULL
        || pReturnCtl->pfnClick != g_world.frame.panel.pfnDismissSelected
        || pReturnCtl->uWidth == 0 || pReturnCtl->uHeight == 0
        || pReturnCtl->x < 0 || pReturnCtl->y < 0
        || (i32)pReturnCtl->x + (i32)pReturnCtl->uWidth > 240
        || (i32)pReturnCtl->y + (i32)pReturnCtl->uHeight > 320) {
        printf("pocketufo: campaign home-order: FAIL stage=return-control contactMsg=%u control=%d enabled=%u rect=%d/%d/%u/%u dismissCallback=%d currentCallback=%d\n",
               (unsigned)pCraft->bContactMsg, pReturnCtl != NULL,
               pReturnCtl != NULL ? (unsigned)pReturnCtl->bEnabled : 0u,
               pReturnCtl != NULL ? (int)pReturnCtl->x : -1,
               pReturnCtl != NULL ? (int)pReturnCtl->y : -1,
               pReturnCtl != NULL ? (unsigned)pReturnCtl->uWidth : 0u,
               pReturnCtl != NULL ? (unsigned)pReturnCtl->uHeight : 0u,
               pReturnCtl != NULL && pReturnCtl->pfnClick
                   == g_world.frame.panel.pfnDismissSelected,
               pReturnCtl != NULL && pReturnCtl->pfnClick != NULL);
        App_ScriptCampaignHomeObserve("home_order_return_control_fail");
        fflush(stdout);
        return false;
    }

    nReturnX = (i32)pReturnCtl->x + (i32)pReturnCtl->uWidth / 2;
    nReturnY = (i32)pReturnCtl->y + (i32)pReturnCtl->uHeight / 2;
    printf("pocketufo: campaign home-order input=return stage=before craftIndex=%d carrierKey=0x%llX carrierId=%u baseKey=0x%llX contactMsg=%u control=GEOUI_CTL_186BA8 rect=%d/%d/%u/%u center=%d/%d callback=dismiss-selected\n",
           nCraftIndex, (unsigned long long)nCarrierKey, (unsigned)bCarrierId,
           (unsigned long long)nHomeBaseKey, (unsigned)pCraft->bContactMsg,
           (int)pReturnCtl->x, (int)pReturnCtl->y,
           (unsigned)pReturnCtl->uWidth, (unsigned)pReturnCtl->uHeight,
           nReturnX, nReturnY);
    App_ScriptPointerLogical(nReturnX, nReturnY, true);
    App_OnPointerUp();
    App_DrawFrame();

    const i32 nShipCountAfterReturn = GeoscapeWorld_GetShipCount(pGeo);
    pCraft = nCraftIndex >= 0 && nCraftIndex < nShipCountAfterReturn
        && pGeo->apCraft != NULL ? pGeo->apCraft[nCraftIndex] : NULL;
    pBase = pCraft != NULL && nBaseIndex >= 0 && nBaseIndex < (i32)g_world.nBaseCount
        && nBaseIndex < PUFO_FAC_BASE_COUNT && g_world.apBases[nBaseIndex] != NULL
        ? g_world.apBases[nBaseIndex] : NULL;
    listView.apItems = g_world.strategList.apItems != NULL
        ? (StrategItem **)g_world.strategList.apItems : NULL;
    listView.nCount = g_world.strategList.nCount;
    nMarkerIndex = pCraft != NULL && listView.apItems != NULL
        ? StrategList_FindByShipId(&listView, pGeo, nCraftIndex) : -1;
    pMarker = nMarkerIndex >= 0 && nMarkerIndex < listView.nCount
        ? listView.apItems[nMarkerIndex] : NULL;
    const bool bHomeOrderArmed = pCraft != NULL
        && (uintptr_t)(const void *)pCraft == nCarrierKey
        && pCraft->bId == bCarrierId && pCraft->bHomeBase == (u8)nBaseIndex
        && pBase != NULL && (uintptr_t)(const void *)pBase == nHomeBaseKey
        && pCraft->bAtBase == 0 && pCraft->bIntercepting == 0
        && pCraft->bContactMsg == 0
        && g_world.bGeoscapeUi != 0 && g_world.screen.nGeoscapeScreen == 0
        && g_world.bTactLive == 0 && !World_bTactPointerLive(&g_world)
        && g_world.sModal.bOpen == 0
        && g_world.msgLines.nLineCount == 0u
        && g_world.geo.time.bPaused == 0 && g_world.bMapSelectionFlag == 0
        && pMarker != NULL && pMarker->nType == 1 && pMarker->bActive == 0
        && pMarker->bVisible != 0 && pMarker->nDisplayId == (i32)bCarrierId
        && pMarker->nSeed == bMarkerSeed && pMarker->nMissionSub == 1
        && pMarker->nTimer == 0
        && pBase != NULL && pMarker->nDestX == pBase->nUnk8
        && pMarker->nDestY == pBase->nUnk12;
    printf("pocketufo: campaign home-order input=return stage=after craftIndex=%d carrierKey=0x%llX carrierId=%u baseKey=0x%llX atBase=%u intercept=%u contactMsg=%u targetIndex=%d markerIndex=%d markerSeed=%u sub=%u visible=%u timer=%d travelMin=%u dest=%d/%d home=%d/%d lineCount=%u pause=%u selection=%u\n",
           nCraftIndex, (unsigned long long)(uintptr_t)pCraft,
           pCraft != NULL ? (unsigned)pCraft->bId : 0u,
           (unsigned long long)(uintptr_t)pBase,
           pCraft != NULL ? (unsigned)pCraft->bAtBase : 0u,
           pCraft != NULL ? (unsigned)pCraft->bIntercepting : 0u,
           pCraft != NULL ? (unsigned)pCraft->bContactMsg : 0u,
           pCraft != NULL ? (int)pCraft->nStrategIdx : -1, nMarkerIndex,
           pMarker != NULL ? (unsigned)pMarker->nSeed : 0u,
           pMarker != NULL ? (unsigned)pMarker->nMissionSub : 0u,
           pMarker != NULL ? (unsigned)pMarker->bVisible : 0u,
           pMarker != NULL ? (int)pMarker->nTimer : -1,
           pMarker != NULL ? (unsigned)pMarker->uTravelMin : 0u,
           pMarker != NULL ? (int)pMarker->nDestX : -1,
           pMarker != NULL ? (int)pMarker->nDestY : -1,
           pBase != NULL ? (int)pBase->nUnk8 : -1,
           pBase != NULL ? (int)pBase->nUnk12 : -1,
           (unsigned)g_world.msgLines.nLineCount,
           (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag);
    fflush(stdout);
    if (!bHomeOrderArmed) {
        printf("pocketufo: campaign home-order: FAIL stage=return-input-result\n");
        App_ScriptCampaignHomeObserve("home_order_return_input_fail");
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign home-order: PASS carrierId=%u craftIndex=%d carrierKey=0x%llX baseKey=0x%llX markerIndex=%d markerSeed=%u travelMin=%u home=%d/%d\n",
           (unsigned)bCarrierId, nCraftIndex, (unsigned long long)nCarrierKey,
           (unsigned long long)nHomeBaseKey, nMarkerIndex,
           (unsigned)bMarkerSeed, (unsigned)pMarker->uTravelMin,
           (int)pBase->nUnk8, (int)pBase->nUnk12);
    App_ScriptCampaignHomeObserve("home_order_armed");
    fflush(stdout);
    return true;
}

/* Select the original one-minute speed through its live root control. */
static bool App_ScriptCampaignHomeMinuteSpeed(const char *pszStage)
{
    UiControl *pMinute = g_world.ui.apRootCtl[3];

    if (g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bTactLive != 0 || World_bTactPointerLive(&g_world)
        || g_world.sModal.bOpen != 0 || g_world.msgLines.nLineCount != 0
        || g_world.geo.time.bPaused != 0 || g_world.bMapSelectionFlag != 0
        || pMinute == NULL || pMinute->bEnabled == 0 || pMinute->pfnClick == NULL
        || pMinute->uWidth == 0 || pMinute->uHeight == 0
        || pMinute->x < 0 || pMinute->y < 0
        || (i32)pMinute->x + (i32)pMinute->uWidth > 240
        || (i32)pMinute->y + (i32)pMinute->uHeight > 320) {
        printf("pocketufo: campaign home-wait speed: FAIL stage=%s screen=%d geo=%u tact=%u modal=%u lines=%u pause=%u selection=%u present=%d enabled=%u rect=%d/%d/%u/%u callback=%d tickSec=%d\n",
               pszStage != NULL ? pszStage : "requested",
               (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bGeoscapeUi,
               (unsigned)g_world.bTactLive, g_world.sModal.bOpen ? 1u : 0u,
               (unsigned)g_world.msgLines.nLineCount, (unsigned)g_world.geo.time.bPaused,
               (unsigned)g_world.bMapSelectionFlag, pMinute != NULL,
               pMinute != NULL ? (unsigned)pMinute->bEnabled : 0u,
               pMinute != NULL ? (int)pMinute->x : -1,
               pMinute != NULL ? (int)pMinute->y : -1,
               pMinute != NULL ? (unsigned)pMinute->uWidth : 0u,
               pMinute != NULL ? (unsigned)pMinute->uHeight : 0u,
               pMinute != NULL && pMinute->pfnClick != NULL,
               (int)g_world.geo.world.nTickSeconds);
        fflush(stdout);
        return false;
    }

    if (g_world.geo.world.nTickSeconds == 60 && pMinute->nState == 1u) {
        printf("pocketufo: campaign home-wait speed: OK stage=%s root=3 tickSec=60 selectedState=%u input=already-minute\n",
               pszStage != NULL ? pszStage : "requested", (unsigned)pMinute->nState);
        fflush(stdout);
        return true;
    }

    const uintptr_t nControlKey = (uintptr_t)(const void *)pMinute;
    const i32 nClickX = (i32)pMinute->x + (i32)pMinute->uWidth / 2;
    const i32 nClickY = (i32)pMinute->y + (i32)pMinute->uHeight / 2;
    printf("pocketufo: campaign home-wait speed: input stage=%s root=3 tickSecBefore=%d controlKey=0x%llX rect=%d/%d/%u/%u\n",
           pszStage != NULL ? pszStage : "requested", (int)g_world.geo.world.nTickSeconds,
           (unsigned long long)nControlKey, (int)pMinute->x, (int)pMinute->y,
           (unsigned)pMinute->uWidth, (unsigned)pMinute->uHeight);
    App_ScriptPointerLogical(nClickX, nClickY, true);
    App_OnPointerUp();
    App_DrawFrame();

    pMinute = g_world.ui.apRootCtl[3];
    const bool bMinute = g_world.bGeoscapeUi != 0
        && g_world.screen.nGeoscapeScreen == 0 && g_world.bTactLive == 0
        && !World_bTactPointerLive(&g_world) && g_world.sModal.bOpen == 0
        && g_world.msgLines.nLineCount == 0u && g_world.geo.time.bPaused == 0
        && g_world.bMapSelectionFlag == 0 && pMinute != NULL
        && pMinute->pfnClick != NULL && pMinute->bEnabled != 0
        && (uintptr_t)(const void *)pMinute == nControlKey
        && pMinute->nState == 1u && g_world.geo.world.nTickSeconds == 60;
    printf("pocketufo: campaign home-wait speed: %s stage=%s root=3 controlKey=0x%llX tickSec=%d selectedState=%u\n",
           bMinute ? "OK" : "FAIL", pszStage != NULL ? pszStage : "requested",
           (unsigned long long)(uintptr_t)pMinute,
           (int)g_world.geo.world.nTickSeconds,
           pMinute != NULL ? (unsigned)pMinute->nState : 0u);
    fflush(stdout);
    return bMinute;
}

/* Advance only the actual one-minute timer until the pinned home leg arrives. */
static bool App_ScriptCampaignHomeWait(void)
{
    static const i32 anLandingStockObject[8] = { 52, 53, 54, 55, 56, 57, 58, 105 };
    static const i32 anLandingCargoObject[PUFO_SHIP_CARGO_COLUMNS] = { 31, 32, 34, 35 };
    const i32 nCraftIndex = (i32)g_world.nMapCraftIndex;
    const GeoscapeWorld *pGeo = &g_world.geo.world;
    const PadImage *pStrip = g_world.ui.apPad[GEOUI_PAD_186BBC];
    i32 nShipCount = GeoscapeWorld_GetShipCount(pGeo);
    Ship *pCraft = nCraftIndex >= 0 && nCraftIndex < nShipCount && pGeo->apCraft != NULL
        ? pGeo->apCraft[nCraftIndex] : NULL;
    i32 nBaseIndex = pCraft != NULL ? (i32)pCraft->bHomeBase : -1;
    BaseRec *pBase = nBaseIndex >= 0 && nBaseIndex < (i32)g_world.nBaseCount
        && nBaseIndex < PUFO_FAC_BASE_COUNT && g_world.apBases[nBaseIndex] != NULL
        ? g_world.apBases[nBaseIndex] : NULL;
    StrategList listView = {
        (StrategItem **)g_world.strategList.apItems, g_world.strategList.nCount,
        g_world.nNextDisplayId, g_world.nNextSiteId
    };
    i32 nMarkerIndex = -1;
    StrategItem *pMarker = NULL;
    uintptr_t nCarrierKey, nBaseKey;
    u8 bCarrierId, bMarkerSeed;
    i32 nTravelEpochSec;
    u16 uTravelMin;
    i32 nElapsedMinutes, nRemainingMinutes, nPulseBound;
    i32 nPulses = 0;
    bool bHotSeenAway;

    if (!g_bWorldReady || g_world.bGeoscapeUi == 0
        || g_world.screen.nGeoscapeScreen != 0 || g_world.bTactLive != 0
        || World_bTactPointerLive(&g_world) || g_world.sModal.bOpen != 0
        || g_world.geo.time.bPaused != 0 || g_world.bMapSelectionFlag != 0
        || g_world.msgLines.nLineCount != 0 || pCraft == NULL || pBase == NULL
        || pCraft->bAtBase != 0 || pCraft->bIntercepting != 0 || pCraft->bContactMsg != 0
        || g_world.strategList.apItems == NULL || g_world.strategList.nCount <= 0
        || pStrip == NULL || g_world.view.nMode != 0) {
        printf("pocketufo: campaign home-wait: FAIL stage=start scene=%u/%d tact=%u tactOwner=%d modal=%u lines=%u pause=%u selection=%u craft=%d base=%d atBase=%u intercept=%u contactMsg=%u viewMode=%d strip=%d\n",
               (unsigned)g_world.bGeoscapeUi, (int)g_world.screen.nGeoscapeScreen,
               (unsigned)g_world.bTactLive, World_bTactPointerLive(&g_world) ? 1 : 0,
               g_world.sModal.bOpen ? 1u : 0u, (unsigned)g_world.msgLines.nLineCount,
               (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag,
               pCraft != NULL, pBase != NULL,
               pCraft != NULL ? (unsigned)pCraft->bAtBase : 0u,
               pCraft != NULL ? (unsigned)pCraft->bIntercepting : 0u,
               pCraft != NULL ? (unsigned)pCraft->bContactMsg : 0u,
               (int)g_world.view.nMode, pStrip != NULL);
        App_ScriptCampaignHomeObserve("home_wait_start_fail");
        fflush(stdout);
        return false;
    }
    nMarkerIndex = StrategList_FindByShipId(&listView, pGeo, nCraftIndex);
    if (nMarkerIndex < 0 || nMarkerIndex >= listView.nCount
        || listView.apItems[nMarkerIndex] == NULL) {
        printf("pocketufo: campaign home-wait: FAIL stage=start markerIndex=%d listCount=%d\n",
               nMarkerIndex, listView.nCount);
        App_ScriptCampaignHomeObserve("home_wait_marker_missing");
        fflush(stdout);
        return false;
    }
    pMarker = listView.apItems[nMarkerIndex];
    if (pMarker->nType != 1 || pMarker->bActive != 0 || pMarker->bVisible == 0
        || pMarker->nDisplayId != (i32)pCraft->bId || pMarker->nMissionSub != 1
        || pMarker->nTimer != 0 || pMarker->nDestX != pBase->nUnk8
        || pMarker->nDestY != pBase->nUnk12) {
        printf("pocketufo: campaign home-wait: FAIL stage=start leg index=%d seed=%d type=%u active=%u display=%d carrierId=%u visible=%u sub=%u timer=%d dest=%d/%d home=%d/%d\n",
               nMarkerIndex, (int)pMarker->nSeed, (unsigned)pMarker->nType,
               (unsigned)pMarker->bActive, (int)pMarker->nDisplayId,
               (unsigned)pCraft->bId, (unsigned)pMarker->bVisible,
               (unsigned)pMarker->nMissionSub, (int)pMarker->nTimer,
               (int)pMarker->nDestX, (int)pMarker->nDestY,
               (int)pBase->nUnk8, (int)pBase->nUnk12);
        App_ScriptCampaignHomeObserve("home_wait_bad_leg");
        fflush(stdout);
        return false;
    }

    nCarrierKey = (uintptr_t)(const void *)pCraft;
    nBaseKey = (uintptr_t)(const void *)pBase;
    bCarrierId = pCraft->bId;
    bMarkerSeed = pMarker->nSeed;
    nTravelEpochSec = pMarker->nTravelEpochSec;
    uTravelMin = pMarker->uTravelMin;
    bHotSeenAway = pCraft->bWeaponsHot != 0;
    if (!App_ScriptCampaignHomeMinuteSpeed("home_wait_start")) {
        App_ScriptCampaignHomeObserve("home_wait_speed_fail");
        return false;
    }

    /* The speed control click is a live callback; re-resolve all world owners after it. */
    nShipCount = GeoscapeWorld_GetShipCount(pGeo);
    pCraft = nCraftIndex >= 0 && nCraftIndex < nShipCount && pGeo->apCraft != NULL
        ? pGeo->apCraft[nCraftIndex] : NULL;
    pBase = nBaseIndex >= 0 && nBaseIndex < (i32)g_world.nBaseCount
        && nBaseIndex < PUFO_FAC_BASE_COUNT && g_world.apBases[nBaseIndex] != NULL
        ? g_world.apBases[nBaseIndex] : NULL;
    listView.apItems = g_world.strategList.apItems != NULL
        ? (StrategItem **)g_world.strategList.apItems : NULL;
    listView.nCount = g_world.strategList.nCount;
    nMarkerIndex = pCraft != NULL && listView.apItems != NULL
        ? StrategList_FindByShipId(&listView, pGeo, nCraftIndex) : -1;
    pMarker = nMarkerIndex >= 0 && nMarkerIndex < listView.nCount
        ? listView.apItems[nMarkerIndex] : NULL;
    if (pCraft == NULL || pBase == NULL || pMarker == NULL
        || (uintptr_t)(const void *)pCraft != nCarrierKey
        || (uintptr_t)(const void *)pBase != nBaseKey || pCraft->bId != bCarrierId
        || pCraft->bHomeBase != (u8)nBaseIndex || pCraft->bAtBase != 0
        || pCraft->bIntercepting != 0 || pCraft->bContactMsg != 0
        || pMarker->nSeed != bMarkerSeed || pMarker->nType != 1
        || pMarker->bActive != 0 || pMarker->bVisible == 0
        || pMarker->nDisplayId != (i32)bCarrierId || pMarker->nMissionSub != 1
        || pMarker->nTimer != 0
        || pMarker->nTravelEpochSec != nTravelEpochSec
        || pMarker->uTravelMin != uTravelMin
        || pMarker->nDestX != pBase->nUnk8 || pMarker->nDestY != pBase->nUnk12
        || g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bTactLive != 0 || World_bTactPointerLive(&g_world)
        || g_world.sModal.bOpen != 0
        || g_world.geo.world.nTickSeconds != 60 || g_world.geo.time.bPaused != 0
        || g_world.msgLines.nLineCount != 0u || g_world.bMapSelectionFlag != 0) {
        printf("pocketufo: campaign home-wait: FAIL stage=after-speed carrier=%d base=%d marker=%d atBase=%u intercept=%u contactMsg=%u markerVisible=%u markerSub=%u markerTimer=%d geo=%u screen=%d tact=%u tactOwner=%d modal=%u tickSec=%d lines=%u pause=%u selection=%u\n",
               pCraft != NULL, pBase != NULL, pMarker != NULL,
               pCraft != NULL ? (unsigned)pCraft->bAtBase : 0u,
               pCraft != NULL ? (unsigned)pCraft->bIntercepting : 0u,
               pCraft != NULL ? (unsigned)pCraft->bContactMsg : 0u,
               pMarker != NULL ? (unsigned)pMarker->bVisible : 0u,
               pMarker != NULL ? (unsigned)pMarker->nMissionSub : 0u,
               pMarker != NULL ? (int)pMarker->nTimer : -1,
               (unsigned)g_world.bGeoscapeUi, (int)g_world.screen.nGeoscapeScreen,
               (unsigned)g_world.bTactLive,
               World_bTactPointerLive(&g_world) ? 1 : 0,
               g_world.sModal.bOpen ? 1u : 0u,
               (int)g_world.geo.world.nTickSeconds, (unsigned)g_world.msgLines.nLineCount,
               (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag);
        App_ScriptCampaignHomeObserve("home_wait_after_speed_fail");
        fflush(stdout);
        return false;
    }

    i32 nElapsedSec = g_world.geo.time.nTotalSec - nTravelEpochSec;
    if (nElapsedSec < 0) {
        printf("pocketufo: campaign home-wait: FAIL stage=clock epoch=%d total=%d\n",
               nTravelEpochSec, (int)g_world.geo.time.nTotalSec);
        return false;
    }
    nElapsedMinutes = nElapsedSec / 60;
    nRemainingMinutes = (i32)uTravelMin - nElapsedMinutes;
    if (nRemainingMinutes < 0) { nRemainingMinutes = 0; }
    nPulseBound = nRemainingMinutes + 2;
    printf("pocketufo: campaign home-wait start carrierId=%u craftIndex=%d carrierKey=0x%llX baseIndex=%d baseKey=0x%llX markerIndex=%d markerSeed=%u dest=%d/%d travelEpoch=%d travelMin=%u totalSec=%d elapsedMin=%d remainingMin=%d pulseBound=%d tickSec=%d weaponsHot=%u shortfall=%u\n",
           (unsigned)bCarrierId, nCraftIndex, (unsigned long long)nCarrierKey,
           nBaseIndex, (unsigned long long)nBaseKey, nMarkerIndex,
           (unsigned)bMarkerSeed, (int)pMarker->nDestX, (int)pMarker->nDestY,
           nTravelEpochSec, (unsigned)uTravelMin, (int)g_world.geo.time.nTotalSec,
           nElapsedMinutes, nRemainingMinutes, nPulseBound,
           (int)g_world.geo.world.nTickSeconds, (unsigned)pCraft->bWeaponsHot,
           (unsigned)g_world.nShortfall1870C8);
    fflush(stdout);
    App_ScriptCampaignHomeObserve("home_wait_start");

    for (nPulses = 1; nPulses <= nPulseBound; ++nPulses) {
        const i32 nTotalBefore = g_world.geo.time.nTotalSec;
        const i32 nLastMinuteBefore = g_world.geo.time.nLastTickMinSec;
        const i32 nLastHourBefore = g_world.geo.time.nLastTickHourSec;
        const u32 nShortfallBefore = g_world.nShortfall1870C8;
        const u8 bHotBefore = pCraft->bWeaponsHot;
        const i32 nShipSlotBefore = pCraft->nSlotIdx >= 0
            && pCraft->nSlotIdx < PUFO_ITEM_QTY_COUNT ? pCraft->nSlotIdx : -1;
        const u8 bWeaponLBefore = (u8)pCraft->bWeaponL;
        const u8 bWeaponRBefore = (u8)pCraft->bWeaponR;
        const u8 bAmmo25Before = pCraft->bAmmo25;
        const u8 bAmmo26Before = pCraft->bAmmo26;
        const u8 bAmmo27Before = pCraft->bAmmo27;
        const u8 bAmmo29Before = pCraft->bAmmo29;
        const u8 bAmmo30Before = pCraft->bAmmo30;
        const u16 uRangeBefore = pCraft->uRangeRemain;
        const i16 nFuelBefore = pCraft->nFuel;
        i32 anBaseStockBefore[8];
        i32 anCargoIndexQtyBefore[4];
        i32 anCargoShipSlotQtyBefore[4];
        i32 anCargoColumnBefore[PUFO_SHIP_CARGO_COLUMNS];
        for (i32 i = 0; i < (i32)(sizeof(anLandingStockObject) / sizeof(anLandingStockObject[0])); ++i) {
            const i32 nObject = anLandingStockObject[i];
            const ObjectRecFull *pObject = g_assets.apObjects != NULL
                && nObject < (i32)g_assets.nObjectCount ? g_assets.apObjects[nObject] : NULL;
            anBaseStockBefore[i] = pObject != NULL
                ? (i32)pObject->auPerBaseVal[nBaseIndex] : -1;
        }
        for (i32 i = 0; i < PUFO_SHIP_CARGO_COLUMNS; ++i) {
            const i32 nObject = anLandingCargoObject[i];
            const ObjectRecFull *pObject = g_assets.apObjects != NULL
                && nObject < (i32)g_assets.nObjectCount ? g_assets.apObjects[nObject] : NULL;
            anCargoIndexQtyBefore[i] = pObject != NULL
                ? (i32)pObject->aItemQty[nCraftIndex] : -1;
            anCargoShipSlotQtyBefore[i] = pObject != NULL && nShipSlotBefore >= 0
                ? (i32)pObject->aItemQty[nShipSlotBefore] : -1;
            anCargoColumnBefore[i] = pCraft->anCargoQty[i];
        }
        if (g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
            || g_world.bTactLive != 0 || World_bTactPointerLive(&g_world)
            || g_world.sModal.bOpen != 0 || g_world.msgLines.nLineCount != 0u
            || g_world.geo.time.bPaused != 0 || g_world.bMapSelectionFlag != 0
            || g_world.geo.world.nTickSeconds != 60) {
            printf("pocketufo: campaign home-wait: FAIL stage=pulse-pre pulse=%d screen=%d geo=%u tact=%u modal=%u lines=%u pause=%u selection=%u tickSec=%d\n",
                   nPulses, (int)g_world.screen.nGeoscapeScreen,
                   (unsigned)g_world.bGeoscapeUi, (unsigned)g_world.bTactLive,
                   g_world.sModal.bOpen ? 1u : 0u, (unsigned)g_world.msgLines.nLineCount,
                   (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.bMapSelectionFlag,
                   (int)g_world.geo.world.nTickSeconds);
            App_ScriptCampaignHomeObserve("home_wait_pulse_pre_fail");
            fflush(stdout);
            return false;
        }

        App_ScriptTicks(1);
        const i32 nTotalAfter = g_world.geo.time.nTotalSec;
        const i32 nPulseDelta = nTotalAfter - nTotalBefore;
        nShipCount = GeoscapeWorld_GetShipCount(pGeo);
        pCraft = nCraftIndex >= 0 && nCraftIndex < nShipCount && pGeo->apCraft != NULL
            ? pGeo->apCraft[nCraftIndex] : NULL;
        pBase = nBaseIndex >= 0 && nBaseIndex < (i32)g_world.nBaseCount
            && nBaseIndex < PUFO_FAC_BASE_COUNT && g_world.apBases[nBaseIndex] != NULL
            ? g_world.apBases[nBaseIndex] : NULL;
        listView.apItems = g_world.strategList.apItems != NULL
            ? (StrategItem **)g_world.strategList.apItems : NULL;
        listView.nCount = g_world.strategList.nCount;
        nMarkerIndex = pCraft != NULL && listView.apItems != NULL
            ? StrategList_FindByShipId(&listView, pGeo, nCraftIndex) : -1;
        pMarker = nMarkerIndex >= 0 && nMarkerIndex < listView.nCount
            ? listView.apItems[nMarkerIndex] : NULL;
        if (pCraft == NULL || pBase == NULL || pMarker == NULL
            || (uintptr_t)(const void *)pCraft != nCarrierKey
            || (uintptr_t)(const void *)pBase != nBaseKey || pCraft->bId != bCarrierId
            || pCraft->bHomeBase != (u8)nBaseIndex
            || pMarker->nSeed != bMarkerSeed || pMarker->nType != 1
            || pMarker->bActive != 0 || pMarker->nDisplayId != (i32)bCarrierId
            || pMarker->nDestX != pBase->nUnk8 || pMarker->nDestY != pBase->nUnk12) {
            printf("pocketufo: campaign home-wait: FAIL stage=pulse-owner pulse=%d carrier=%d base=%d marker=%d carrierKey=0x%llX carrierId=%u baseIndex=%d markerIndex=%d markerSeed=%u\n",
                   nPulses, pCraft != NULL, pBase != NULL, pMarker != NULL,
                   (unsigned long long)(uintptr_t)pCraft,
                   pCraft != NULL ? (unsigned)pCraft->bId : 0u,
                   pCraft != NULL ? (int)pCraft->bHomeBase : -1,
                   nMarkerIndex, pMarker != NULL ? (unsigned)pMarker->nSeed : 0u);
            App_ScriptCampaignHomeObserve("home_wait_pulse_owner_fail");
            fflush(stdout);
            return false;
        }
        if (nPulseDelta != 61) {
            printf("pocketufo: campaign home-wait: FAIL stage=pulse-clock pulse=%d totalBefore=%d totalAfter=%d delta=%d expectedDelta=61 tickSec=%d\n",
                   nPulses, nTotalBefore, nTotalAfter, nPulseDelta,
                   (int)g_world.geo.world.nTickSeconds);
            App_ScriptCampaignHomeObserve("home_wait_pulse_clock_fail");
            fflush(stdout);
            return false;
        }

        bHotSeenAway = bHotSeenAway || (pCraft->bAtBase == 0 && pCraft->bWeaponsHot != 0);
        nElapsedSec = nTotalAfter - nTravelEpochSec;
        if (nElapsedSec < 0) {
            printf("pocketufo: campaign home-wait: FAIL stage=pulse-epoch pulse=%d epoch=%d total=%d\n",
                   nPulses, nTravelEpochSec, nTotalAfter);
            return false;
        }
        nElapsedMinutes = nElapsedSec / 60;
        nRemainingMinutes = (i32)uTravelMin - nElapsedMinutes;
        if (nRemainingMinutes < 0) { nRemainingMinutes = 0; }
        const i32 nLastMinuteAfter = g_world.geo.time.nLastTickMinSec;
        const i32 nLastHourAfter = g_world.geo.time.nLastTickHourSec;

        printf("pocketufo: campaign home-wait pulse=%d carrierKey=0x%llX carrierId=%u baseKey=0x%llX baseIndex=%d totalSec=%d->%d delta=%d elapsedMin=%d remainingMin=%d atBase=%u intercept=%u weaponsHotBefore=%u weaponsHot=%u hotSeenAway=%d markerIndex=%d markerSeed=%u markerType=%u active=%u visible=%u sub=%u timer=%d travelMin=%u dest=%d/%d lineCount=%u lineFlag=%d pause=%u selection=%u geo=%u screen=%d tact=%u tactOwner=%d modal=%u shortfall=%u\n",
               nPulses, (unsigned long long)nCarrierKey, (unsigned)bCarrierId,
               (unsigned long long)nBaseKey, nBaseIndex,
               nTotalBefore, nTotalAfter, nPulseDelta,
               nElapsedMinutes, nRemainingMinutes,
               (unsigned)pCraft->bAtBase, (unsigned)pCraft->bIntercepting,
               (unsigned)bHotBefore, (unsigned)pCraft->bWeaponsHot,
               bHotSeenAway ? 1 : 0, nMarkerIndex, (unsigned)pMarker->nSeed,
               (unsigned)pMarker->nType, (unsigned)pMarker->bActive,
               (unsigned)pMarker->bVisible, (unsigned)pMarker->nMissionSub,
               (int)pMarker->nTimer, (unsigned)pMarker->uTravelMin,
               (int)pMarker->nDestX, (int)pMarker->nDestY,
               (unsigned)g_world.msgLines.nLineCount,
               g_world.msgLines.nLineCount != 0u
                   ? (int)g_world.msgLines.abLineFlags[0] : -1,
               (unsigned)g_world.geo.time.bPaused,
               (unsigned)g_world.bMapSelectionFlag,
               (unsigned)g_world.bGeoscapeUi,
               (int)g_world.screen.nGeoscapeScreen,
               (unsigned)g_world.bTactLive,
               World_bTactPointerLive(&g_world) ? 1 : 0,
               g_world.sModal.bOpen ? 1u : 0u,
               (unsigned)g_world.nShortfall1870C8);
        fflush(stdout);

        if (pCraft->bAtBase != 0) {
            const bool bArrivalState = pCraft->bAtBase != 0
                && pCraft->bHomeBase == (u8)nBaseIndex && pCraft->bIntercepting == 0
                && pCraft->bContactMsg == 0 && pMarker->nMissionSub == 0
                && pMarker->bVisible == 0 && pMarker->nDestX == pBase->nUnk8
                && pMarker->nDestY == pBase->nUnk12;
            const bool bLandingGateObserved = bHotBefore != 0
                && pCraft->bWeaponsHot == 0;
            const i32 nShipSlotAfter = pCraft->nSlotIdx >= 0
                && pCraft->nSlotIdx < PUFO_ITEM_QTY_COUNT ? pCraft->nSlotIdx : -1;
            printf("pocketufo: campaign home-wait arrival-detail pulse=%d clockLastMin=%d->%d clockLastHour=%d->%d hourAdvancedSamePulse=%d fuel=%d->%d range=%u->%u shortfall=%u->%u mount=%d/%d->%d/%d ammo=%u/%u/%u/%u/%u->%u/%u/%u/%u/%u cargo=%d,%d,%d,%d->%d,%d,%d,%d shipSlot=%d->%d\n",
                   nPulses, nLastMinuteBefore, nLastMinuteAfter,
                   nLastHourBefore, nLastHourAfter,
                   nLastHourBefore != nLastHourAfter ? 1 : 0,
                   (int)nFuelBefore, (int)pCraft->nFuel,
                   (unsigned)uRangeBefore, (unsigned)pCraft->uRangeRemain,
                   (unsigned)nShortfallBefore, (unsigned)g_world.nShortfall1870C8,
                   (int)bWeaponLBefore, (int)bWeaponRBefore,
                   (int)pCraft->bWeaponL, (int)pCraft->bWeaponR,
                   (unsigned)bAmmo25Before, (unsigned)bAmmo26Before,
                   (unsigned)bAmmo27Before, (unsigned)bAmmo29Before,
                   (unsigned)bAmmo30Before, (unsigned)pCraft->bAmmo25,
                   (unsigned)pCraft->bAmmo26, (unsigned)pCraft->bAmmo27,
                   (unsigned)pCraft->bAmmo29, (unsigned)pCraft->bAmmo30,
                   (int)anCargoColumnBefore[0], (int)anCargoColumnBefore[1],
                   (int)anCargoColumnBefore[2], (int)anCargoColumnBefore[3],
                   (int)pCraft->anCargoQty[0], (int)pCraft->anCargoQty[1],
                   (int)pCraft->anCargoQty[2], (int)pCraft->anCargoQty[3],
                   nShipSlotBefore, nShipSlotAfter);
            for (i32 i = 0; i < (i32)(sizeof(anLandingStockObject) / sizeof(anLandingStockObject[0])); ++i) {
                const i32 nObject = anLandingStockObject[i];
                const ObjectRecFull *pObject = g_assets.apObjects != NULL
                    && nObject < (i32)g_assets.nObjectCount ? g_assets.apObjects[nObject] : NULL;
                const i32 nBaseStockAfter = pObject != NULL
                    ? (i32)pObject->auPerBaseVal[nBaseIndex] : -1;
                printf("pocketufo: campaign home-wait arrival-stock object=%d baseBefore=%d baseAfter=%d\n",
                       nObject, anBaseStockBefore[i], nBaseStockAfter);
            }
            for (i32 i = 0; i < PUFO_SHIP_CARGO_COLUMNS; ++i) {
                const i32 nObject = anLandingCargoObject[i];
                const ObjectRecFull *pObject = g_assets.apObjects != NULL
                    && nObject < (i32)g_assets.nObjectCount ? g_assets.apObjects[nObject] : NULL;
                const i32 nCraftIndexQtyAfter = pObject != NULL
                    ? (i32)pObject->aItemQty[nCraftIndex] : -1;
                const i32 nShipSlotQtyAfter = pObject != NULL && nShipSlotAfter >= 0
                    ? (i32)pObject->aItemQty[nShipSlotAfter] : -1;
                printf("pocketufo: campaign home-wait arrival-cargo-object object=%d craftIndexBefore=%d craftIndexAfter=%d shipSlotBefore=%d shipSlotAfter=%d\n",
                       nObject, anCargoIndexQtyBefore[i], nCraftIndexQtyAfter,
                       anCargoShipSlotQtyBefore[i], nShipSlotQtyAfter);
            }
            printf("pocketufo: campaign home-wait arrival pulses=%d carrierKey=0x%llX carrierId=%u craftIndex=%d baseKey=0x%llX homeBase=%u atBase=%u intercept=%u markerIndex=%d markerSeed=%u markerSub=%u markerVisible=%u transferHotBefore=%u transferHotAfter=%u transferGateObserved=%d hotSeenAway=%d shortfall=%u arrivalState=%d lineCount=%u lineFlag=%d pause=%u selection=%u geo=%u screen=%d tact=%u tactOwner=%d modal=%u\n",
                   nPulses, (unsigned long long)nCarrierKey, (unsigned)pCraft->bId,
                   nCraftIndex, (unsigned long long)nBaseKey,
                   (unsigned)pCraft->bHomeBase, (unsigned)pCraft->bAtBase,
                   (unsigned)pCraft->bIntercepting, nMarkerIndex,
                   (unsigned)pMarker->nSeed, (unsigned)pMarker->nMissionSub,
                   (unsigned)pMarker->bVisible, (unsigned)bHotBefore,
                   (unsigned)pCraft->bWeaponsHot, bLandingGateObserved ? 1 : 0,
                   bHotSeenAway ? 1 : 0, (unsigned)g_world.nShortfall1870C8,
                   bArrivalState ? 1 : 0, (unsigned)g_world.msgLines.nLineCount,
                   g_world.msgLines.nLineCount != 0u
                       ? (int)g_world.msgLines.abLineFlags[0] : -1,
                   (unsigned)g_world.geo.time.bPaused,
                   (unsigned)g_world.bMapSelectionFlag,
                   (unsigned)g_world.bGeoscapeUi,
                   (int)g_world.screen.nGeoscapeScreen,
                   (unsigned)g_world.bTactLive,
                   World_bTactPointerLive(&g_world) ? 1 : 0,
                   g_world.sModal.bOpen ? 1u : 0u);
            App_ScriptCampaignHomeObserve("home_wait_arrival");
            if (!bArrivalState || g_world.bGeoscapeUi == 0
                || g_world.screen.nGeoscapeScreen != 0 || g_world.bTactLive != 0
                || World_bTactPointerLive(&g_world) || g_world.sModal.bOpen != 0) {
                printf("pocketufo: campaign home-wait: FAIL stage=arrival-contract\n");
                fflush(stdout);
                return false;
            }
            if (g_world.msgLines.nLineCount == 0u && g_world.geo.time.bPaused == 0
                && g_world.bMapSelectionFlag == 0) {
                printf("pocketufo: campaign home-wait: PASS stage=natural_home_arrival transferGateObserved=%d shortfall=%u\n",
                       bLandingGateObserved ? 1 : 0,
                       (unsigned)g_world.nShortfall1870C8);
                fflush(stdout);
                return true;
            }
            if (g_world.msgLines.nLineCount == 1u
                && g_world.msgLines.abLineFlags[0] == PUFO_STATUS_SELFLAG
                && g_world.geo.time.bPaused != 0 && g_world.bMapSelectionFlag == 0) {
                printf("pocketufo: campaign home-wait: PASS stage=natural_home_arrival pendingFlag10=1 transferGateObserved=%d shortfall=%u\n",
                       bLandingGateObserved ? 1 : 0,
                       (unsigned)g_world.nShortfall1870C8);
                fflush(stdout);
                return true;
            }
            printf("pocketufo: campaign home-wait: FAIL stage=arrival-message-contract\n");
            App_ScriptLines();
            fflush(stdout);
            return false;
        }

        if (pCraft->bIntercepting != 0 || pCraft->bContactMsg != 0
            || pMarker->bVisible == 0 || pMarker->nMissionSub != 1
            || pMarker->nTimer != 0
            || pMarker->nTravelEpochSec != nTravelEpochSec
            || pMarker->uTravelMin != uTravelMin) {
            printf("pocketufo: campaign home-wait: FAIL stage=leg-changed pulse=%d atBase=%u intercept=%u contactMsg=%u markerVisible=%u sub=%u timer=%d epoch=%d/%d travelMin=%u/%u\n",
                   nPulses, (unsigned)pCraft->bAtBase,
                   (unsigned)pCraft->bIntercepting, (unsigned)pCraft->bContactMsg,
                   (unsigned)pMarker->bVisible, (unsigned)pMarker->nMissionSub,
                   (int)pMarker->nTimer, (int)pMarker->nTravelEpochSec,
                   nTravelEpochSec, (unsigned)pMarker->uTravelMin,
                   (unsigned)uTravelMin);
            App_ScriptCampaignHomeObserve("home_wait_leg_changed");
            fflush(stdout);
            return false;
        }

        if (g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
            || g_world.bTactLive != 0 || World_bTactPointerLive(&g_world)
            || g_world.sModal.bOpen != 0) {
            printf("pocketufo: campaign home-wait: FAIL stage=pulse-scene pulse=%d geo=%u screen=%d tact=%u modal=%u\n",
                   nPulses, (unsigned)g_world.bGeoscapeUi,
                   (int)g_world.screen.nGeoscapeScreen, (unsigned)g_world.bTactLive,
                   g_world.sModal.bOpen ? 1u : 0u);
            App_ScriptCampaignHomeObserve("home_wait_pulse_scene_fail");
            fflush(stdout);
            return false;
        }

        if (g_world.msgLines.nLineCount != 0u) {
            if (g_world.msgLines.nLineCount != 1u
                || g_world.msgLines.abLineFlags[0] != PUFO_STATUS_SELFLAG
                || g_world.geo.time.bPaused == 0 || g_world.bMapSelectionFlag != 0) {
                printf("pocketufo: campaign home-wait: FAIL stage=unexpected-line pulse=%d lines=%u firstFlag=%d pause=%u selection=%u\n",
                       nPulses, (unsigned)g_world.msgLines.nLineCount,
                       (int)g_world.msgLines.abLineFlags[0],
                       (unsigned)g_world.geo.time.bPaused,
                       (unsigned)g_world.bMapSelectionFlag);
                App_ScriptLines();
                App_ScriptCampaignHomeObserve("home_wait_unexpected_line");
                fflush(stdout);
                return false;
            }
            if (!App_ScriptCampaignAckConfirm()) {
                printf("pocketufo: campaign home-wait: FAIL stage=flag10-ack pulse=%d\n",
                       nPulses);
                App_ScriptCampaignHomeObserve("home_wait_ack_fail");
                fflush(stdout);
                return false;
            }

            /* Ack runs the original status-card handler, which resets the speed to 5 seconds. */
            nShipCount = GeoscapeWorld_GetShipCount(pGeo);
            pCraft = nCraftIndex >= 0 && nCraftIndex < nShipCount && pGeo->apCraft != NULL
                ? pGeo->apCraft[nCraftIndex] : NULL;
            pBase = nBaseIndex >= 0 && nBaseIndex < (i32)g_world.nBaseCount
                && nBaseIndex < PUFO_FAC_BASE_COUNT && g_world.apBases[nBaseIndex] != NULL
                ? g_world.apBases[nBaseIndex] : NULL;
            listView.apItems = g_world.strategList.apItems != NULL
                ? (StrategItem **)g_world.strategList.apItems : NULL;
            listView.nCount = g_world.strategList.nCount;
            nMarkerIndex = pCraft != NULL && listView.apItems != NULL
                ? StrategList_FindByShipId(&listView, pGeo, nCraftIndex) : -1;
            pMarker = nMarkerIndex >= 0 && nMarkerIndex < listView.nCount
                ? listView.apItems[nMarkerIndex] : NULL;
            if (pCraft == NULL || pBase == NULL || pMarker == NULL
                || (uintptr_t)(const void *)pCraft != nCarrierKey
                || (uintptr_t)(const void *)pBase != nBaseKey
                || pCraft->bId != bCarrierId || pCraft->bHomeBase != (u8)nBaseIndex
                || pCraft->bAtBase != 0 || pCraft->bIntercepting != 0
                || pMarker->nSeed != bMarkerSeed || pMarker->nType != 1
                || pMarker->bActive != 0 || pMarker->bVisible == 0
                || pMarker->nMissionSub != 1
                || pMarker->nTravelEpochSec != nTravelEpochSec
                || pMarker->uTravelMin != uTravelMin
                || pMarker->nDestX != pBase->nUnk8 || pMarker->nDestY != pBase->nUnk12
                || g_world.geo.time.bPaused != 0 || g_world.msgLines.nLineCount != 0u
                || g_world.bMapSelectionFlag != 0) {
                printf("pocketufo: campaign home-wait: FAIL stage=after-ack ownerOrLegChanged pulse=%d\n",
                       nPulses);
                App_ScriptCampaignHomeObserve("home_wait_after_ack_fail");
                fflush(stdout);
                return false;
            }
            if (!App_ScriptCampaignHomeMinuteSpeed("home_wait_after_ack")) {
                App_ScriptCampaignHomeObserve("home_wait_speed_restore_fail");
                return false;
            }
            nShipCount = GeoscapeWorld_GetShipCount(pGeo);
            pCraft = nCraftIndex >= 0 && nCraftIndex < nShipCount && pGeo->apCraft != NULL
                ? pGeo->apCraft[nCraftIndex] : NULL;
            pBase = nBaseIndex >= 0 && nBaseIndex < (i32)g_world.nBaseCount
                && nBaseIndex < PUFO_FAC_BASE_COUNT && g_world.apBases[nBaseIndex] != NULL
                ? g_world.apBases[nBaseIndex] : NULL;
            listView.apItems = g_world.strategList.apItems != NULL
                ? (StrategItem **)g_world.strategList.apItems : NULL;
            listView.nCount = g_world.strategList.nCount;
            nMarkerIndex = pCraft != NULL && listView.apItems != NULL
                ? StrategList_FindByShipId(&listView, pGeo, nCraftIndex) : -1;
            pMarker = nMarkerIndex >= 0 && nMarkerIndex < listView.nCount
                ? listView.apItems[nMarkerIndex] : NULL;
            if (pCraft == NULL || pBase == NULL || pMarker == NULL
                || (uintptr_t)(const void *)pCraft != nCarrierKey
                || (uintptr_t)(const void *)pBase != nBaseKey
                || pCraft->bId != bCarrierId || pCraft->bHomeBase != (u8)nBaseIndex
                || pCraft->bAtBase != 0 || pCraft->bIntercepting != 0
                || pCraft->bContactMsg != 0 || pMarker->nSeed != bMarkerSeed
                || pMarker->nType != 1 || pMarker->bActive != 0
                || pMarker->bVisible == 0 || pMarker->nMissionSub != 1
                || pMarker->nTimer != 0
                || pMarker->nTravelEpochSec != nTravelEpochSec
                || pMarker->uTravelMin != uTravelMin
                || pMarker->nDestX != pBase->nUnk8
                || pMarker->nDestY != pBase->nUnk12
                || g_world.geo.time.bPaused != 0 || g_world.msgLines.nLineCount != 0u
                || g_world.bMapSelectionFlag != 0) {
                printf("pocketufo: campaign home-wait: FAIL stage=minute-speed-owner-changed pulse=%d\n",
                       nPulses);
                App_ScriptCampaignHomeObserve("home_wait_minute_speed_owner_changed");
                fflush(stdout);
                return false;
            }
        } else if (g_world.geo.time.bPaused != 0 || g_world.bMapSelectionFlag != 0) {
            printf("pocketufo: campaign home-wait: FAIL stage=unexpected-pause pulse=%d pause=%u selection=%u\n",
                   nPulses, (unsigned)g_world.geo.time.bPaused,
                   (unsigned)g_world.bMapSelectionFlag);
            App_ScriptCampaignHomeObserve("home_wait_unexpected_pause");
            fflush(stdout);
            return false;
        }
    }

    printf("pocketufo: campaign home-wait: FAIL stage=travel-pulse-bound pulses=%d bound=%d travelMin=%u epoch=%d total=%d\n",
           nPulses, nPulseBound, (unsigned)uTravelMin,
           nTravelEpochSec, (int)g_world.geo.time.nTotalSec);
    App_ScriptCampaignHomeObserve("home_wait_travel_bound");
    fflush(stdout);
    return false;
}

/* Let the normal returned-geoscape timer publish any pending card, then clear it through the
 * control selected by its raw pointer branch. Screen 13 can rebuild the geoscape with no pending
 * line, so zero acknowledgements is valid only after the line/modal/scene gates stay clear through
 * both timer windows. The 31..49 cards use the left OK control; flag 13 uses control B. */
static bool App_ScriptCampaignAcknowledgeReturn(void)
{
    const i32 nAckLimit = 8;
    i32 nAcked = 0;

    if (!g_bWorldReady || g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bTactLive != 0) {
        printf("pocketufo: campaign returnack: FAIL (need the returned geoscape)\n");
        fflush(stdout);
        return false;
    }

    printf("pocketufo: campaign returnack: before timer lines %u firstFlag %u pause %u\n",
           g_world.msgLines.nLineCount,
           g_world.msgLines.nLineCount != 0 ? (unsigned)g_world.msgLines.abLineFlags[0] : 0u,
           (unsigned)g_world.geo.time.bPaused);
    fflush(stdout);
    App_ScriptTicks(50); /* run the real return timer before checking for a newly posted card */
    (void)App_DrawFrame();
    App_ScriptCampaignTraceReturnCard();
    App_ScriptDump("campaign_return_card.ppm");
    for (i32 i = 0; i < nAckLimit && g_world.msgLines.nLineCount > 0; ++i) {
        const i32 nCountBefore = (i32)g_world.msgLines.nLineCount;
        const i32 nFlag = (i32)g_world.msgLines.abLineFlags[0];
        UiControl *pAck = NULL;
        const char *pszAck = NULL;

        if (nFlag == PUFO_STATUS_SELFLAG) {
            if (!App_ScriptCampaignAckConfirm()) {
                return false;
            }
            ++nAcked;
            continue;
        }

        if (nFlag > 30 && nFlag < 50) {
            pAck = (g_world.ptrEnv.apCtl != NULL)
                       ? g_world.ptrEnv.apCtl[GEOUI_CTL_18704C] : NULL;
            pszAck = "return-ok";
        } else if (nFlag == 13) {
            pAck = (g_world.ptrEnv.apCtl != NULL)
                       ? g_world.ptrEnv.apCtl[GEOUI_CTL_186BB0] : NULL;
            pszAck = "return-close-only";
        }

        if (pAck == NULL || pAck->bEnabled == 0 || pAck->uWidth == 0 || pAck->uHeight == 0) {
            printf("pocketufo: campaign returnack: FAIL (line %d flag %d has no live native ack control)\n",
                   (int)nCountBefore, (int)nFlag);
            fflush(stdout);
            return false;
        }

        printf("pocketufo: campaign returnack: click %s flag %d line %d modal %d pause %u\n",
               pszAck,
               (int)nFlag, (int)nCountBefore,
               g_world.sModal.bOpen ? 1 : 0, (unsigned)g_world.geo.time.bPaused);
        fflush(stdout);
        if (!App_ScriptCampaignClickGeoControl(pAck, pszAck)) { return false; }
        if (g_world.msgLines.nLineCount >= nCountBefore) {
            printf("pocketufo: campaign returnack: FAIL (native control did not consume line; count %d -> %d modal %d)\n",
                   (int)nCountBefore, (int)g_world.msgLines.nLineCount,
                   g_world.sModal.bOpen ? 1 : 0);
            fflush(stdout);
            return false;
        }
        ++nAcked;
        (void)App_DrawFrame();
        App_ScriptCampaignTraceReturnCard();
    }

    /* The real timer and paint path lets the returned screen finish its fade. */
    App_ScriptTicks(50);
    if (nAcked == 0 && g_world.msgLines.nLineCount == 0 && !g_world.sModal.bOpen
        && g_world.bGeoscapeUi != 0 && g_world.screen.nGeoscapeScreen == 0
        && g_world.bTactLive == 0 && g_world.geo.time.bPaused == 0) {
        printf("pocketufo: campaign returnack: NONE pending lines 0 modal 0 geo 1 screen 0 tact 0 pause 0 timerWindows 2\n");
        fflush(stdout);
        return true;
    }
    if (nAcked == 0 || g_world.msgLines.nLineCount != 0 || g_world.sModal.bOpen
        || g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bTactLive != 0 || g_world.geo.time.bPaused != 0) {
        printf("pocketufo: campaign returnack: FAIL acked %d lines %d modal %d geo %d screen %d tact %d pause %u\n",
               (int)nAcked, (int)g_world.msgLines.nLineCount,
               g_world.sModal.bOpen ? 1 : 0, (int)g_world.bGeoscapeUi,
               (int)g_world.screen.nGeoscapeScreen, (int)g_world.bTactLive,
               (unsigned)g_world.geo.time.bPaused);
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign returnack: PASS acked %d lines %d modal %d geo %d screen %d tact %d pause %u\n",
           (int)nAcked, (int)g_world.msgLines.nLineCount, g_world.sModal.bOpen ? 1 : 0,
           (int)g_world.bGeoscapeUi, (int)g_world.screen.nGeoscapeScreen,
           (int)g_world.bTactLive, (unsigned)g_world.geo.time.bPaused);
    fflush(stdout);
    return true;
}

static bool App_ScriptCampaignAbort(void)
{
    i32 nModalX, nModalY;
    const bool bAlreadyEnded = g_world.nUiScreen == 13u && g_world.bMapBattleEnded != 0;
    AppCampaignReturnProof debriefProof;
    AppCampaignReturnProof shutdownProof;

    if (!bAlreadyEnded) {
        if (!World_bTactPointerLive(&g_world) || g_world.nUiScreen != 0
            || g_world.bMapBattleEnded != 0) {
            printf("pocketufo: campaign abort: FAIL (need a live unended mission)\n");
            fflush(stdout);
            return false;
        }
        if (!App_ScriptCampaignClickBank(14) || g_world.nUiScreen != 7u
            || !App_ScriptCampaignClickBank(56) || !g_world.sModal.bOpen) {
            printf("pocketufo: campaign abort: FAIL (screen-7 abort dialog did not open)\n");
            fflush(stdout);
            return false;
        }
        (void)App_DrawFrame();
        nModalX = (i32)g_world.sTactInput.nSlot20X + 25;
        nModalY = (i32)g_world.sTactInput.nSlot22Y + 8;
        App_ScriptPointerLogical(nModalX, nModalY, true);
        App_OnPointerUp();
        (void)App_DrawFrame();
        printf("pocketufo: campaign abort: yes at %d/%d, ended %d screen %d\n",
               (int)nModalX, (int)nModalY, (int)g_world.bMapBattleEnded, (int)g_world.nUiScreen);
        if (g_world.nUiScreen != 13u || g_world.bMapBattleEnded == 0) {
            printf("pocketufo: campaign abort: FAIL (real abort did not build the debrief)\n");
            fflush(stdout);
            return false;
        }
    } else {
        printf("pocketufo: campaign abort: battle already ended on screen 13\n");
        fflush(stdout);
    }
    App_ScriptCampaignState("debrief");
    App_ScriptTicks(50); /* the debrief's own WM_TIMER fade before its OK pad is hit */
    App_ScriptDump("campaign_debrief.ppm");
    if (!App_ScriptCampaignCaptureReturnProof("debrief", &debriefProof, NULL)) {
        return false;
    }
    if (!App_ScriptCampaignClickBank(128)) {
        return false;
    }
    if (g_world.nUiScreen == 19u) {
        (void)App_ScriptCampaignClickBank(129);
    }
    (void)App_DrawFrame();
    if (!App_ScriptCampaignCaptureReturnProof("geoscape_after_shutdown", &shutdownProof, &debriefProof)) {
        return false;
    }
    const bool bPersistentAtShutdown = App_ScriptCampaignReturnProofMatches(&debriefProof,
                                                                             &shutdownProof);
    if (!App_ScriptCampaignAcknowledgeReturn()) {
        return false;
    }
    if (!bPersistentAtShutdown) {
        printf("pocketufo: campaign returnproof: FAIL carrier or assigned crew identity changed at debrief shutdown\n");
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign returnproof: PASS stage=debrief_exit_persisted carrierId=%u "
           "craftIndex=%d crew=%d loadoutIndexAmmo=stable itemStock=observed homeArrivalObserved=%u "
           "atBase=%u intercept=%u targetIndex=%d\n",
           (unsigned)shutdownProof.bCraftId, (int)shutdownProof.nCraftIndex,
           (int)shutdownProof.nCrewCount,
           shutdownProof.bAtBase != 0 ? 1u : 0u,
           (unsigned)shutdownProof.bAtBase, (unsigned)shutdownProof.bIntercepting,
           (int)shutdownProof.nTargetIndex);
    fflush(stdout);
    printf("pocketufo: campaign abort: returned geoscape %d screen %d tact %d\n",
           (int)g_world.bGeoscapeUi, (int)g_world.screen.nGeoscapeScreen,
           (int)g_world.bTactLive);
    fflush(stdout);
    return g_world.bGeoscapeUi != 0 && g_world.screen.nGeoscapeScreen == 0
        && g_world.bTactLive == 0;
}

/* This is only the native OK/return input for an already won debrief. It has
 * no abort path and cannot create an outcome. */
static bool App_ScriptCampaignCloseDebrief(void)
{
    const GameMap *pMap = g_world.pTactMap;

    if (!World_bTactPointerLive(&g_world) || g_world.nUiScreen != 13u
        || g_world.bMapBattleEnded == 0 || g_world.bMapMissionVictory == 0
        || pMap == NULL || pMap->nMissionType != 1u || pMap->nXcomCount == 0
        || g_world.nMapEnemyReserveCount != 0) {
        printf("pocketufo: campaign close-debrief: FAIL (need the live normal-victory screen 13)\n");
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign close-debrief: victory 1 screen 13 xcom %u enemy %u reserve %u\n",
           (unsigned)pMap->nXcomCount, (unsigned)pMap->nEnemyCount,
           (unsigned)g_world.nMapEnemyReserveCount);
    fflush(stdout);
    App_ScriptCampaignState("natural_victory_debrief");
    App_ScriptTicks(50);
    App_ScriptDump("campaign_victory_debrief.ppm");
    if (!App_ScriptCampaignClickBank(128)) {
        printf("pocketufo: campaign close-debrief: FAIL (native result OK did not accept input)\n");
        fflush(stdout);
        return false;
    }
    if (g_world.nUiScreen == 19u && !App_ScriptCampaignClickBank(129)) {
        printf("pocketufo: campaign close-debrief: FAIL (native result follow-up did not accept input)\n");
        fflush(stdout);
        return false;
    }
    (void)App_DrawFrame();
    if (!App_ScriptCampaignAcknowledgeReturn()) {
        return false;
    }
    if (g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.bTactLive != 0) {
        printf("pocketufo: campaign close-debrief: FAIL (return did not restore the geoscape)\n");
        fflush(stdout);
        return false;
    }
    App_ScriptCampaignState("victory_returned");
    if (!App_ScriptCampaignCargo("victory_returned")) {
        return false;
    }
    App_ScriptStrategyState("victory_returned");
    printf("pocketufo: campaign close-debrief: PASS normal victory returned to geoscape\n");
    fflush(stdout);
    return true;
}

/* Read-only owner census for the cold tactical save/load parity flow. This
 * command does not call SaveGame or write game state. */
enum {
    APP_RESUME_SIDE_NONE = -1,
    APP_RESUME_SIDE_XCOM = 0,
    APP_RESUME_SIDE_ENEMY = 1,
    APP_RESUME_SIDE_CIVILIAN = 2,
    APP_RESUME_SIDE_RESERVE = 3,
    APP_RESUME_OWNER_NONE = 0,
    APP_RESUME_OWNER_UNIT_SLOT = 1,
    APP_RESUME_OWNER_GROUND_CELL = 2,
    APP_RESUME_OWNER_UNSERIALIZED_SLOT = 3
};

static i32 App_ScriptCampaignResumeWireOrdinal(const Soldier *pNeedle,
                                                i32 *pnMatches)
{
    i32 nFound = -1;
    i32 nMatches = 0;

    if (pnMatches != NULL) {
        *pnMatches = 0;
    }
    if (pNeedle == NULL) {
        return -1;
    }
    /* World_SaveSlot passes this exact apSoldiers array/count to SaveGame;
     * Save_AssignRosterIndices uses its array index as the wire ordinal. */
    for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
        if (g_world.apSoldiers[i] == pNeedle) {
            nFound = i;
            ++nMatches;
        }
    }
    if (pnMatches != NULL) {
        *pnMatches = nMatches;
    }
    return (nMatches == 1) ? nFound : -1;
}

static i32 App_ScriptCampaignResumeItemOrdinal(const TactItem *pNeedle,
                                               i32 *pnMatches)
{
    i32 nFound = -1;
    i32 nMatches = 0;

    if (pnMatches != NULL) {
        *pnMatches = 0;
    }
    if (pNeedle == NULL) {
        return -1;
    }
    /* Save_AssignRosterIndices numbers this active pool prefix from zero. */
    for (i32 i = 0; i < g_world.nTactItemCount; ++i) {
        if (g_world.apTactItems[i] == pNeedle) {
            nFound = i;
            ++nMatches;
        }
    }
    if (pnMatches != NULL) {
        *pnMatches = nMatches;
    }
    return (nMatches == 1) ? nFound : -1;
}

static i32 App_ScriptCampaignResumeCellSlot(const MapCell *pNeedle,
                                           i32 *pnMatches)
{
    i32 nFound = -1;
    i32 nMatches = 0;

    if (pnMatches != NULL) {
        *pnMatches = 0;
    }
    if (pNeedle == NULL) {
        return -1;
    }
    for (i32 i = 0; i < PUFO_TERRAIN_CELL_SLOTS; ++i) {
        if (g_world.apTactCells[i] == pNeedle) {
            nFound = i;
            ++nMatches;
        }
    }
    if (pnMatches != NULL) {
        *pnMatches = nMatches;
    }
    return (nMatches == 1) ? nFound : -1;
}

static i32 App_ScriptCampaignResumeObjectOrdinal(const ObjectRecFull *pNeedle,
                                                 i32 *pnMatches)
{
    i32 nFound = -1;
    i32 nMatches = 0;

    if (pnMatches != NULL) {
        *pnMatches = 0;
    }
    if (pNeedle == NULL || g_world.pAssets == NULL
        || g_world.pAssets->apObjects == NULL) {
        return -1;
    }
    for (i32 i = 0; i < (i32)g_world.pAssets->nObjectCount; ++i) {
        if (g_world.pAssets->apObjects[i] == pNeedle) {
            nFound = i;
            ++nMatches;
        }
    }
    if (pnMatches != NULL) {
        *pnMatches = nMatches;
    }
    return (nMatches == 1) ? nFound : -1;
}

static const char *App_ScriptCampaignResumeSideName(i32 nSide)
{
    switch (nSide) {
    case APP_RESUME_SIDE_XCOM:     return "xcom";
    case APP_RESUME_SIDE_ENEMY:    return "enemy";
    case APP_RESUME_SIDE_CIVILIAN: return "civilian";
    case APP_RESUME_SIDE_RESERVE:  return "reserve";
    default:                       return "none";
    }
}

static const char *App_ScriptCampaignResumeSlotName(i32 nSlot,
                                                     i32 *pnLocalSlot)
{
    if (nSlot == 0) {
        *pnLocalSlot = 0;
        return "handL";
    }
    if (nSlot == 1) {
        *pnLocalSlot = 0;
        return "handR";
    }
    if (nSlot < 10) {
        *pnLocalSlot = nSlot - 2;
        return "pocket";
    }
    if (nSlot < 18) {
        *pnLocalSlot = nSlot - 10;
        return "backpack";
    }
    *pnLocalSlot = nSlot - 18;
    return "belt";
}

static TactItem *App_ScriptCampaignResumeInventorySlot(Soldier *pUnit,
                                                        i32 nSlot)
{
    if (nSlot == 0) { return pUnit->pHandL; }
    if (nSlot == 1) { return pUnit->pHandR; }
    if (nSlot < 10) {
        i32 nPocket = (nSlot - 2) / 2;
        i32 nItem = (nSlot - 2) % 2;
        return pUnit->aPockets[nPocket].apItem[nItem];
    }
    if (nSlot < 18) {
        return pUnit->apBackpack[nSlot - 10];
    }
    return pUnit->apBelt[nSlot - 18];
}

static void App_ScriptCampaignResumeError(const char *pszStage,
                                          const char *pszCode,
                                          i32 nA, i32 nB,
                                          i32 *pnErrors)
{
    ++*pnErrors;
    printf("pocketufo: campaign tact-resume-owners: stage=%s record=error "
           "code=%s a=%d b=%d\n",
           pszStage, pszCode, (int)nA, (int)nB);
}

static void App_ScriptCampaignResumeAddItemOwner(
    const char *pszStage, i32 nItemOrdinal, i32 nOwnerKind,
    i32 nOwnerOrdinal, i32 nOwnerSlot, i32 nOwnerCellSlot,
    i32 *anOwnerCount, i32 *anOwnerKind, i32 *anOwnerOrdinal,
    i32 *anOwnerSlot, i32 *anOwnerCellSlot, i32 *pnErrors)
{
    if (nItemOrdinal < 0 || nItemOrdinal >= g_world.nTactItemCount) {
        App_ScriptCampaignResumeError(pszStage, "owner_item_outside_active_pool",
                                      nItemOrdinal, g_world.nTactItemCount,
                                      pnErrors);
        return;
    }
    ++anOwnerCount[nItemOrdinal];
    if (anOwnerCount[nItemOrdinal] == 1) {
        anOwnerKind[nItemOrdinal] = nOwnerKind;
        anOwnerOrdinal[nItemOrdinal] = nOwnerOrdinal;
        anOwnerSlot[nItemOrdinal] = nOwnerSlot;
        anOwnerCellSlot[nItemOrdinal] = nOwnerCellSlot;
    } else {
        App_ScriptCampaignResumeError(pszStage, "duplicate_item_owner",
                                      nItemOrdinal, anOwnerCount[nItemOrdinal],
                                      pnErrors);
    }
}

static void App_ScriptCampaignResumeRosterSlot(
    const char *pszStage, const char *pszSide, i32 nSide,
    i32 nSideOrdinal, Soldier *pUnit, i32 *anSide,
    i32 *anSideOrdinal, i32 *pnRosterMembers, i32 *pnErrors)
{
    i32 nMatches = 0;
    i32 nWireOrdinal;

    if (pUnit == NULL) {
        printf("pocketufo: campaign tact-resume-owners: stage=%s record=roster "
               "side=%s sideOrdinal=%d present=0 wire=-1\n",
               pszStage, pszSide, (int)nSideOrdinal);
        return;
    }
    nWireOrdinal = App_ScriptCampaignResumeWireOrdinal(pUnit, &nMatches);
    if (nMatches != 1 || nWireOrdinal < 0) {
        App_ScriptCampaignResumeError(pszStage, "roster_unit_not_unique_in_wire_pool",
                                      nSide, nSideOrdinal, pnErrors);
        printf("pocketufo: campaign tact-resume-owners: stage=%s record=roster "
               "side=%s sideOrdinal=%d present=1 wire=-1\n",
               pszStage, pszSide, (int)nSideOrdinal);
        return;
    }
    if (anSide[nWireOrdinal] != APP_RESUME_SIDE_NONE) {
        App_ScriptCampaignResumeError(pszStage, "unit_in_multiple_save_rosters",
                                      nWireOrdinal, nSideOrdinal, pnErrors);
        printf("pocketufo: campaign tact-resume-owners: stage=%s record=roster "
               "side=%s sideOrdinal=%d present=1 wire=%d duplicate=1\n",
               pszStage, pszSide, (int)nSideOrdinal, (int)nWireOrdinal);
        return;
    }
    anSide[nWireOrdinal] = nSide;
    anSideOrdinal[nWireOrdinal] = nSideOrdinal;
    ++*pnRosterMembers;
    printf("pocketufo: campaign tact-resume-owners: stage=%s record=roster "
           "side=%s sideOrdinal=%d present=1 wire=%d\n",
           pszStage, pszSide, (int)nSideOrdinal, (int)nWireOrdinal);
}

static bool App_ScriptCampaignTactResumeOwners(const char *pszStage)
{
    static const i32 anFootprintOffsets[4] = { 0, 1, 60, 61 };
    static const i32 anFootprintDx[4] = { 0, 1, 0, 1 };
    static const i32 anFootprintDy[4] = { 0, 0, 1, 1 };
    const char *pszUseStage =
        (pszStage != NULL && pszStage[0] != '\0') ? pszStage : "requested";
    const i32 nSoldierCapacity =
        (i32)(sizeof(g_world.apSoldiers) / sizeof(g_world.apSoldiers[0]));
    const i32 nItemCapacity =
        (i32)(sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0]));
    uintptr_t nMapKey = (uintptr_t)(void *)g_world.pTactMap;
    uintptr_t nQueueKey = (uintptr_t)(void *)g_world.pTactState;
    GameMap *pMap = NULL;
    TactState *pQueue = NULL;
    i32 anSide[PUFO_SOLDIER_CAPACITY];
    i32 anSideOrdinal[PUFO_SOLDIER_CAPACITY];
    i32 anPrimaryCell[PUFO_SOLDIER_CAPACITY];
    i32 anCellOwnerCount[PUFO_SOLDIER_CAPACITY];
    i32 abLarge[PUFO_SOLDIER_CAPACITY];
    i32 anItemOwnerCount[
        sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0])];
    i32 anItemOwnerKind[
        sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0])];
    i32 anItemOwnerOrdinal[
        sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0])];
    i32 anItemOwnerSlot[
        sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0])];
    i32 anItemOwnerCellSlot[
        sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0])];
    i32 anItemPrevCount[
        sizeof(g_world.apTactItems) / sizeof(g_world.apTactItems[0])];
    i32 nErrors = 0;
    i32 nRosterMembers = 0;
    i32 nCellRows = 0;
    i32 nCellUnitOwners = 0;
    i32 nCellItemHeads = 0;
    i32 nSlotRows = 0;
    i32 nItemRows = 0;
    i32 nAmmoPayloadRows = 0;
    i32 nFootprintRows = 0;
    i32 nTailNonNull = 0;
    i32 nTailActiveAlias = 0;
    i32 nTailOpaque = 0;

    if (g_world.nSoldierCount < 0 || g_world.nSoldierCount > nSoldierCapacity
        || g_world.nTactItemCount < 0 || g_world.nTactItemCount > nItemCapacity
        || g_world.nMapEnemyReserveCount < 0 || g_world.nMapEnemyReserveCount > 30
        || g_world.pAssets == NULL || g_world.pAssets->apObjects == NULL
        || g_world.pAssets->nObjectCount == 0
        || g_world.pAssets->apObjects[0] == NULL) {
        printf("pocketufo: campaign tact-resume-owners: stage=%s record=summary "
               "schema=1 status=fail reason=pool_or_asset_bounds\n",
               pszUseStage);
        fflush(stdout);
        return false;
    }
    if (g_world.bTactLive == 0 || g_world.bTactFlag == 0
        || !App_ScriptCampaignTactOwnerKeysMatch(nMapKey, nQueueKey,
                                                  &pMap, &pQueue)
        || pMap == NULL || pQueue == NULL
        || g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
        || g_world.bMapPlayerInput == 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
        || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0
        || pQueue->bQueueActive != 0
        || pMap->nXcomCount > PUFO_MAP_ROSTER_MAX
        || pMap->nEnemyCount > PUFO_MAP_ROSTER_MAX
        || pMap->nCivilianCount < 0
        || pMap->nCivilianCount > PUFO_MAP_CIV_MAX) {
        printf("pocketufo: campaign tact-resume-owners: stage=%s record=summary "
               "schema=1 status=fail reason=active_tactical_owner_missing\n",
               pszUseStage);
        fflush(stdout);
        return false;
    }
    if (g_world.nUiScreen != 0u || g_world.sModal.bOpen != 0
        || g_world.bMapPlayerInput == 0 || pMap->bActionLatch != PUFO_ACTION_LATCH_NONE
        || pMap->tact.bBusy != 0 || pQueue->nQueueLen != 0
        || pQueue->bQueueActive != 0 || g_world.bMapFire2 != 0
        || pMap->bFireStatus1 != 0 || pMap->bTrajHitStop != 0
        || g_world.bMapEnemyTurnActive != 0) {
        printf("pocketufo: campaign tact-resume-owners: stage=%s record=idle "
               "status=fail screen=%u modal=%u player=%u latch=%u busy=%u "
               "queueLen=%d queueActive=%u fall=%u fire=%u throw=%u enemyTurn=%u\n",
               pszUseStage, (unsigned)g_world.nUiScreen,
               (unsigned)g_world.sModal.bOpen, (unsigned)g_world.bMapPlayerInput,
               (unsigned)pMap->bActionLatch, (unsigned)pMap->tact.bBusy,
               (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive,
               (unsigned)g_world.bMapFire2, (unsigned)pMap->bFireStatus1,
               (unsigned)pMap->bTrajHitStop, (unsigned)g_world.bMapEnemyTurnActive);
        fflush(stdout);
        return false;
    }
    printf("pocketufo: campaign tact-resume-owners: stage=%s record=idle "
           "status=pass screen=%u modal=%u player=%u latch=%u busy=%u "
           "queueLen=%d queueActive=%u fall=%u fire=%u throw=%u enemyTurn=%u\n",
           pszUseStage, (unsigned)g_world.nUiScreen,
           (unsigned)g_world.sModal.bOpen, (unsigned)g_world.bMapPlayerInput,
           (unsigned)pMap->bActionLatch, (unsigned)pMap->tact.bBusy,
           (int)pQueue->nQueueLen, (unsigned)pQueue->bQueueActive,
           (unsigned)g_world.bMapFire2, (unsigned)pMap->bFireStatus1,
           (unsigned)pMap->bTrajHitStop, (unsigned)g_world.bMapEnemyTurnActive);
    fflush(stdout);
    if (g_itemSlotEmpty.pObj != g_world.pAssets->apObjects[0]
        || (u32)g_itemSlotEmpty.nRefId != PUFO_SAVE_EMPTY_ITEM_ID) {
        printf("pocketufo: campaign tact-resume-owners: stage=%s record=summary "
               "schema=1 status=fail reason=sentinel_init_mismatch\n",
               pszUseStage);
        fflush(stdout);
        return false;
    }

    for (i32 i = 0; i < PUFO_SOLDIER_CAPACITY; ++i) {
        anSide[i] = APP_RESUME_SIDE_NONE;
        anSideOrdinal[i] = -1;
        anPrimaryCell[i] = -1;
        anCellOwnerCount[i] = 0;
        abLarge[i] = 0;
    }
    for (i32 i = 0; i < nItemCapacity; ++i) {
        anItemOwnerCount[i] = 0;
        anItemOwnerKind[i] = APP_RESUME_OWNER_NONE;
        anItemOwnerOrdinal[i] = -1;
        anItemOwnerSlot[i] = -1;
        anItemOwnerCellSlot[i] = -1;
        anItemPrevCount[i] = 0;
    }

    for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
        Soldier *pUnit = g_world.apSoldiers[i];
        i32 nMatches = 0;
        if (pUnit == NULL) {
            App_ScriptCampaignResumeError(pszUseStage, "null_inside_active_soldier_pool",
                                          i, g_world.nSoldierCount, &nErrors);
            continue;
        }
        (void)App_ScriptCampaignResumeWireOrdinal(pUnit, &nMatches);
        if (nMatches != 1) {
            App_ScriptCampaignResumeError(pszUseStage, "duplicate_soldier_pool_pointer",
                                          i, nMatches, &nErrors);
        }
    }
    for (i32 i = 0; i < g_world.nTactItemCount; ++i) {
        TactItem *pItem = g_world.apTactItems[i];
        i32 nMatches = 0;
        if (pItem == NULL) {
            App_ScriptCampaignResumeError(pszUseStage, "null_inside_active_item_pool",
                                          i, g_world.nTactItemCount, &nErrors);
            continue;
        }
        (void)App_ScriptCampaignResumeItemOrdinal(pItem, &nMatches);
        if (nMatches != 1) {
            App_ScriptCampaignResumeError(pszUseStage, "duplicate_item_pool_pointer",
                                          i, nMatches, &nErrors);
        }
    }

    /* Save_bSoldierInTact is exactly membership in these four prefixes; it
     * does not use HP, bStatus, or pCell as its flag predicate. */
    for (i32 i = 0; i < (i32)pMap->nXcomCount; ++i) {
        App_ScriptCampaignResumeRosterSlot(pszUseStage, "xcom",
            APP_RESUME_SIDE_XCOM, i, pMap->apXcomUnits[i], anSide,
            anSideOrdinal, &nRosterMembers, &nErrors);
    }
    for (i32 i = 0; i < (i32)pMap->nEnemyCount; ++i) {
        App_ScriptCampaignResumeRosterSlot(pszUseStage, "enemy",
            APP_RESUME_SIDE_ENEMY, i, pMap->apEnemyUnits[i], anSide,
            anSideOrdinal, &nRosterMembers, &nErrors);
    }
    for (i32 i = 0; i < pMap->nCivilianCount; ++i) {
        App_ScriptCampaignResumeRosterSlot(pszUseStage, "civilian",
            APP_RESUME_SIDE_CIVILIAN, i, pMap->apCivilians[i], anSide,
            anSideOrdinal, &nRosterMembers, &nErrors);
    }
    for (i32 i = 0; i < g_world.nMapEnemyReserveCount; ++i) {
        App_ScriptCampaignResumeRosterSlot(pszUseStage, "reserve",
            APP_RESUME_SIDE_RESERVE, i, g_world.apTactHidden[i], anSide,
            anSideOrdinal, &nRosterMembers, &nErrors);
    }

    for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
        Soldier *pUnit = g_world.apSoldiers[i];
        i32 nCellSlot = -1;
        i32 nCellMatches = 0;
        i32 nX = -1, nY = -1, nZ = -1;
        i32 bPrimaryReciprocal = 0;
        if (pUnit == NULL) {
            printf("pocketufo: campaign tact-resume-owners: stage=%s record=unit "
                   "wire=%d side=none sideOrdinal=-1 sideMember=0 state=null\n",
                   pszUseStage, (int)i);
            continue;
        }
        if (anSide[i] != APP_RESUME_SIDE_NONE) {
            abLarge[i] = Unit_bIsLarge(pUnit) ? 1 : 0;
        }
        /* Record every wire Soldier's pCell reference. Only side-prefix
         * membership makes it a saved tactical placement; inactive/reserve
         * references remain visible here and are not promoted to cell owners. */
        if (pUnit->pCell != NULL) {
            nCellSlot = App_ScriptCampaignResumeCellSlot(pUnit->pCell,
                                                         &nCellMatches);
            if (nCellMatches != 1 || nCellSlot < 0) {
                App_ScriptCampaignResumeError(pszUseStage,
                    "soldier_primary_cell_not_unique_in_map_pool",
                    i, nCellMatches, &nErrors);
                nCellSlot = -1;
            } else {
                MapCell *pCell = g_world.apTactCells[nCellSlot];
                nX = (i32)pCell->nX;
                nY = (i32)pCell->nY;
                nZ = (i32)pCell->nZ;
                bPrimaryReciprocal = (pCell->pUnit == pUnit) ? 1 : 0;
                if (bPrimaryReciprocal) {
                    anPrimaryCell[i] = nCellSlot;
                    if (anSide[i] == APP_RESUME_SIDE_NONE
                        || anSide[i] == APP_RESUME_SIDE_RESERVE) {
                        App_ScriptCampaignResumeError(pszUseStage,
                            "nondeployed_unit_has_live_cell_owner",
                            i, nCellSlot, &nErrors);
                    }
                } else if (anSide[i] != APP_RESUME_SIDE_NONE
                           && anSide[i] != APP_RESUME_SIDE_RESERVE
                           && pUnit->bStatus < UNIT_STATUS_INACTIVE) {
                    App_ScriptCampaignResumeError(pszUseStage,
                        "active_roster_primary_not_reciprocal",
                        i, nCellSlot, &nErrors);
                }
            }
        } else if (anSide[i] != APP_RESUME_SIDE_NONE
                   && anSide[i] != APP_RESUME_SIDE_RESERVE
                   && pUnit->bStatus < UNIT_STATUS_INACTIVE) {
            App_ScriptCampaignResumeError(pszUseStage,
                "active_roster_unit_has_no_primary_cell",
                i, anSide[i], &nErrors);
        }
        printf("pocketufo: campaign tact-resume-owners: stage=%s record=unit "
               "wire=%d side=%s sideOrdinal=%d sideMember=%d hp=%u status=%u "
               "large=%d pCellSlot=%d pCell=%d/%d/%d primaryReciprocal=%d\n",
               pszUseStage, (int)i,
               App_ScriptCampaignResumeSideName(anSide[i]),
               (int)anSideOrdinal[i], anSide[i] != APP_RESUME_SIDE_NONE ? 1 : 0,
               (unsigned)pUnit->uHp, (unsigned)pUnit->bStatus,
               (int)abLarge[i], (int)nCellSlot, (int)nX, (int)nY, (int)nZ,
               (int)bPrimaryReciprocal);
        if (anSide[i] != APP_RESUME_SIDE_NONE && bPrimaryReciprocal) {
            const i32 nFootCount = abLarge[i] ? 4 : 1;
            for (i32 f = 0; f < nFootCount; ++f) {
                i32 nFootSlot = nCellSlot + anFootprintOffsets[f];
                i32 nFootX = -1, nFootY = -1, nFootZ = -1;
                i32 nFootOwner = -1;
                i32 nFootMatches = 0;
                MapCell *pFoot = NULL;
                if (!MapCell_LargeFootprintCoords(
                        nX, nY, nZ, anFootprintDx[f], anFootprintDy[f],
                        &nFootX, &nFootY, &nFootZ)) {
                    App_ScriptCampaignResumeError(pszUseStage,
                        "large_footprint_coordinate_unresolved",
                        i, nFootSlot, &nErrors);
                } else if (nFootSlot < 0 || nFootSlot >= PUFO_TERRAIN_CELL_SLOTS) {
                    App_ScriptCampaignResumeError(pszUseStage,
                        "large_footprint_outside_flat_cell_pool",
                        i, nFootSlot, &nErrors);
                } else {
                    pFoot = g_world.apTactCells[nFootSlot];
                    if (pFoot == NULL) {
                        App_ScriptCampaignResumeError(pszUseStage,
                            "large_footprint_null_cell",
                            i, nFootSlot, &nErrors);
                    } else {
                        if (pFoot->nX != nFootX || pFoot->nY != nFootY
                            || pFoot->nZ != nFootZ) {
                            App_ScriptCampaignResumeError(pszUseStage,
                                "large_footprint_cell_not_canonical",
                                nFootSlot, nFootX, &nErrors);
                        }
                        if (pFoot->pUnit != NULL) {
                            nFootOwner = App_ScriptCampaignResumeWireOrdinal(
                                pFoot->pUnit, &nFootMatches);
                        }
                        if (pFoot->pUnit != pUnit) {
                            App_ScriptCampaignResumeError(pszUseStage,
                                "footprint_cell_not_reciprocal",
                                i, nFootSlot, &nErrors);
                        }
                    }
                }
                printf("pocketufo: campaign tact-resume-owners: stage=%s "
                       "record=footprint wire=%d offset=%d cellSlot=%d "
                       "x=%d y=%d z=%d ownerWire=%d\n",
                       pszUseStage, (int)i, (int)anFootprintOffsets[f],
                       (int)nFootSlot,
                       pFoot != NULL ? (int)pFoot->nX : -1,
                       pFoot != NULL ? (int)pFoot->nY : -1,
                       pFoot != NULL ? (int)pFoot->nZ : -1,
                       pFoot != NULL && pFoot->pUnit == pUnit
                           && nFootMatches == 1 ? (int)nFootOwner : -1);
                ++nFootprintRows;
            }
        }
    }

    for (i32 c = 0; c < PUFO_TERRAIN_CELL_SLOTS; ++c) {
        MapCell *pCell = g_world.apTactCells[c];
        i32 nUnitWire = -1;
        i32 nHeadPool = -1;
        i32 nItemNodes = 0;
        if (pCell == NULL) {
            App_ScriptCampaignResumeError(pszUseStage,
                "null_cell_in_physical_map_pool", c, PUFO_TERRAIN_CELL_SLOTS,
                &nErrors);
            continue;
        }
        {
            const i32 nExpectedX = c % PUFO_MAP_CELL_DIM;
            const i32 nExpectedY = (c / PUFO_MAP_CELL_DIM) % PUFO_MAP_CELL_DIM;
            const i32 nExpectedZ = c / (PUFO_MAP_CELL_DIM * PUFO_MAP_CELL_DIM);
            const i32 nActualFlat = (i32)pCell->nZ * PUFO_MAP_CELL_DIM * PUFO_MAP_CELL_DIM
                + (i32)pCell->nY * PUFO_MAP_CELL_DIM + (i32)pCell->nX;

            if (pCell->nX != nExpectedX || pCell->nY != nExpectedY
                || pCell->nZ != nExpectedZ || nActualFlat != c) {
                App_ScriptCampaignResumeError(pszUseStage,
                    "cell_coordinates_do_not_match_flat_pool_slot",
                    c, nActualFlat, &nErrors);
            }
        }
        if (pCell->pUnit != NULL) {
            i32 nMatches = 0;
            nUnitWire = App_ScriptCampaignResumeWireOrdinal(pCell->pUnit,
                                                            &nMatches);
            if (nMatches != 1 || nUnitWire < 0) {
                App_ScriptCampaignResumeError(pszUseStage,
                    "cell_unit_owner_not_unique_in_wire_pool",
                    c, nMatches, &nErrors);
                nUnitWire = -2;
            } else {
                Soldier *pOwner = g_world.apSoldiers[nUnitWire];
                i32 nExpectedOffset = -1;
                if (anSide[nUnitWire] == APP_RESUME_SIDE_NONE
                    || anSide[nUnitWire] == APP_RESUME_SIDE_RESERVE) {
                    App_ScriptCampaignResumeError(pszUseStage,
                        "cell_unit_owner_not_in_deployed_save_roster",
                        c, nUnitWire, &nErrors);
                }
                if (pOwner->bStatus >= UNIT_STATUS_INACTIVE) {
                    App_ScriptCampaignResumeError(pszUseStage,
                        "inactive_unit_still_owns_map_cell",
                        c, nUnitWire, &nErrors);
                }
                if (anPrimaryCell[nUnitWire] >= 0) {
                    if (abLarge[nUnitWire] != 0) {
                        for (i32 f = 0; f < 4; ++f) {
                            if (anPrimaryCell[nUnitWire]
                                + anFootprintOffsets[f] == c) {
                                nExpectedOffset = anFootprintOffsets[f];
                            }
                        }
                    } else if (anPrimaryCell[nUnitWire] == c) {
                        nExpectedOffset = 0;
                    }
                }
                if (nExpectedOffset < 0) {
                    App_ScriptCampaignResumeError(pszUseStage,
                        "cell_owner_outside_unit_footprint",
                        c, nUnitWire, &nErrors);
                }
                ++anCellOwnerCount[nUnitWire];
                ++nCellUnitOwners;
            }
        }
        if (pCell->pItemList != NULL) {
            TactItem *pWalk = pCell->pItemList;
            i32 nHeadMatches = 0;
            nHeadPool = App_ScriptCampaignResumeItemOrdinal(pWalk,
                                                            &nHeadMatches);
            if (nHeadMatches != 1 || nHeadPool < 0) {
                App_ScriptCampaignResumeError(pszUseStage,
                    "cell_item_head_not_unique_in_active_pool",
                    c, nHeadMatches, &nErrors);
                nHeadPool = -2;
            } else {
                ++nCellItemHeads;
                while (pWalk != NULL) {
                    i32 nMatches = 0;
                    i32 nPool = App_ScriptCampaignResumeItemOrdinal(pWalk,
                                                                    &nMatches);
                    TactItem *pItem;
                    if (nItemNodes >= g_world.nTactItemCount) {
                        App_ScriptCampaignResumeError(pszUseStage,
                            "ground_list_cycle_or_budget",
                            c, nItemNodes, &nErrors);
                        break;
                    }
                    if (nMatches != 1 || nPool < 0) {
                        App_ScriptCampaignResumeError(pszUseStage,
                            "ground_item_not_unique_in_active_pool",
                            c, nMatches, &nErrors);
                        break;
                    }
                    pItem = g_world.apTactItems[nPool];
                    App_ScriptCampaignResumeAddItemOwner(pszUseStage, nPool,
                        APP_RESUME_OWNER_GROUND_CELL, c, -1, c,
                        anItemOwnerCount, anItemOwnerKind,
                        anItemOwnerOrdinal, anItemOwnerSlot,
                        anItemOwnerCellSlot, &nErrors);
                    pWalk = pItem->pNext;
                    ++nItemNodes;
                }
            }
        }
        if (pCell->pUnit != NULL || pCell->pItemList != NULL) {
            printf("pocketufo: campaign tact-resume-owners: stage=%s "
                   "record=cell slot=%d x=%u y=%u z=%u unitWire=%d "
                   "itemHead=%d itemNodes=%d\n",
                   pszUseStage, (int)c, (unsigned)pCell->nX,
                   (unsigned)pCell->nY, (unsigned)pCell->nZ,
                   (int)nUnitWire, (int)nHeadPool, (int)nItemNodes);
            ++nCellRows;
        }
    }

    for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
        Soldier *pUnit = g_world.apSoldiers[i];
        if (pUnit == NULL) {
            continue;
        }
        if (anSide[i] != APP_RESUME_SIDE_NONE) {
            i32 nExpected = 0;
            if (anPrimaryCell[i] >= 0) {
                MapCell *pCell = g_world.apTactCells[anPrimaryCell[i]];
                if (pCell != NULL && pCell->pUnit == pUnit) {
                    nExpected = abLarge[i] ? 4 : 1;
                }
            }
            if (anCellOwnerCount[i] != nExpected) {
                App_ScriptCampaignResumeError(pszUseStage,
                    "unit_cell_owner_count_mismatch",
                    i, anCellOwnerCount[i], &nErrors);
            }
        } else if (anCellOwnerCount[i] != 0) {
            App_ScriptCampaignResumeError(pszUseStage,
                "inactive_unit_has_cell_owner",
                i, anCellOwnerCount[i], &nErrors);
        }
    }

    for (i32 i = 0; i < g_world.nSoldierCount; ++i) {
        Soldier *pUnit = g_world.apSoldiers[i];
        if (pUnit == NULL) {
            continue;
        }
        for (i32 slot = 0; slot < 27; ++slot) {
            i32 nLocalSlot = -1;
            const char *pszSlotKind =
                App_ScriptCampaignResumeSlotName(slot, &nLocalSlot);
            TactItem *pItem = App_ScriptCampaignResumeInventorySlot(pUnit,
                                                                    slot);
            const bool bReservedBackpack = strcmp(pszSlotKind, "backpack") == 0
                && (nLocalSlot == 5 || nLocalSlot == 6);
            const bool bTacticalUnit = anSide[i] != APP_RESUME_SIDE_NONE;
            i32 nPool = -1;
            i32 nMatches = 0;
            u32 nSaveRef = PUFO_SAVE_ID_NONE;
            const char *pszState = "null";
            if (pItem == &g_itemSlotEmpty) {
                pszState = "empty_sentinel";
                if (bReservedBackpack) {
                    nSaveRef = PUFO_SAVE_ID_NONE;
                } else {
                    nSaveRef = PUFO_SAVE_EMPTY_ITEM_ID;
                }
            } else if (pItem != NULL) {
                nPool = App_ScriptCampaignResumeItemOrdinal(pItem, &nMatches);
                if (nMatches != 1 || nPool < 0) {
                    pszState = "unresolved";
                    App_ScriptCampaignResumeError(pszUseStage,
                        bTacticalUnit ? "inventory_item_not_unique_in_active_pool"
                                      : "inactive_inventory_item_not_in_active_pool",
                        i, slot, &nErrors);
                } else {
                    pszState = "item";
                    nSaveRef = (u32)nPool;
                    if (bReservedBackpack) {
                        nSaveRef = PUFO_SAVE_ID_NONE;
                        App_ScriptCampaignResumeError(pszUseStage,
                            "reserved_backpack_slot_has_serialized_item",
                            i, slot, &nErrors);
                        App_ScriptCampaignResumeAddItemOwner(pszUseStage,
                            nPool, APP_RESUME_OWNER_UNSERIALIZED_SLOT, i,
                            slot, -1, anItemOwnerCount, anItemOwnerKind,
                            anItemOwnerOrdinal, anItemOwnerSlot,
                            anItemOwnerCellSlot, &nErrors);
                    } else if (bTacticalUnit) {
                        App_ScriptCampaignResumeAddItemOwner(pszUseStage,
                            nPool, APP_RESUME_OWNER_UNIT_SLOT, i, slot, -1,
                            anItemOwnerCount, anItemOwnerKind,
                            anItemOwnerOrdinal, anItemOwnerSlot,
                            anItemOwnerCellSlot, &nErrors);
                    } else {
                        App_ScriptCampaignResumeError(pszUseStage,
                            "inactive_unit_owns_active_item",
                            i, nPool, &nErrors);
                        App_ScriptCampaignResumeAddItemOwner(pszUseStage,
                            nPool, APP_RESUME_OWNER_UNSERIALIZED_SLOT, i,
                            slot, -1, anItemOwnerCount, anItemOwnerKind,
                            anItemOwnerOrdinal, anItemOwnerSlot,
                            anItemOwnerCellSlot, &nErrors);
                    }
                }
            }
            if (bTacticalUnit) {
                printf("pocketufo: campaign tact-resume-owners: stage=%s "
                       "record=slot wire=%d kind=%s slot=%d state=%s "
                       "itemPool=%d saveRef=%u\n",
                       pszUseStage, (int)i, pszSlotKind, (int)nLocalSlot,
                       pszState, (int)nPool, (unsigned)nSaveRef);
                ++nSlotRows;
            } else if (pItem != NULL && pItem != &g_itemSlotEmpty) {
                printf("pocketufo: campaign tact-resume-owners: stage=%s "
                       "record=inactiveSlot wire=%d kind=%s slot=%d "
                       "state=%s itemPool=%d\n",
                       pszUseStage, (int)i, pszSlotKind, (int)nLocalSlot,
                       pszState, (int)nPool);
            }
        }
    }

    for (i32 i = 0; i < g_world.nTactItemCount; ++i) {
        TactItem *pItem = g_world.apTactItems[i];
        i32 nObjectMatches = 0;
        i32 nObject = -1;
        i32 nNextMatches = 0;
        i32 nNext = -1;
        i32 nLinkedMatches = 0;
        i32 nLinked = -1;
        i32 nOwnerKind = anItemOwnerKind[i];
        const char *pszOwnerKind = "none";
        i32 nOwnerOrdinal = anItemOwnerOrdinal[i];
        i32 nOwnerSlot = anItemOwnerSlot[i];
        i32 nOwnerCellSlot = anItemOwnerCellSlot[i];
        if (pItem == NULL) {
            printf("pocketufo: campaign tact-resume-owners: stage=%s "
                   "record=item pool=%d state=null ownerCount=%d\n",
                   pszUseStage, (int)i, (int)anItemOwnerCount[i]);
            ++nItemRows;
            continue;
        }
        nObject = App_ScriptCampaignResumeObjectOrdinal(
            (const ObjectRecFull *)pItem->pObj, &nObjectMatches);
        if (nObjectMatches != 1 || nObject < 0) {
            App_ScriptCampaignResumeError(pszUseStage,
                "item_object_not_unique_in_asset_table",
                i, nObjectMatches, &nErrors);
        }
        if (pItem->pNext != NULL) {
            nNext = App_ScriptCampaignResumeItemOrdinal(pItem->pNext,
                                                        &nNextMatches);
            if (nNextMatches != 1 || nNext < 0) {
                App_ScriptCampaignResumeError(pszUseStage,
                    "item_next_not_unique_in_active_pool",
                    i, nNextMatches, &nErrors);
                nNext = -2;
            } else if (nOwnerKind == APP_RESUME_OWNER_GROUND_CELL) {
                /* Only links reachable from a real cell root participate in
                 * ground-list ownership. Unit-held items retain serialized
                 * pNext metadata after removal from a cell list. */
                ++anItemPrevCount[nNext];
                if (anItemPrevCount[nNext] > 1) {
                    App_ScriptCampaignResumeError(pszUseStage,
                        "ground_item_has_multiple_ground_predecessors",
                        nNext, anItemPrevCount[nNext], &nErrors);
                }
                if (anItemOwnerKind[nNext] != APP_RESUME_OWNER_GROUND_CELL
                    || nOwnerCellSlot != anItemOwnerCellSlot[nNext]) {
                    App_ScriptCampaignResumeError(pszUseStage,
                        "ground_next_link_outside_same_cell_chain",
                        i, nNext, &nErrors);
                }
            }
        }
        if (pItem->pLinkedSoldier != NULL) {
            nLinked = App_ScriptCampaignResumeWireOrdinal(
                pItem->pLinkedSoldier, &nLinkedMatches);
            if (nLinkedMatches != 1 || nLinked < 0) {
                App_ScriptCampaignResumeError(pszUseStage,
                    "linked_soldier_not_unique_in_wire_pool",
                    i, nLinkedMatches, &nErrors);
                nLinked = -2;
            }
        }
        if (anItemOwnerCount[i] != 1) {
            App_ScriptCampaignResumeError(pszUseStage, "item_owner_count_not_one",
                                          i, anItemOwnerCount[i], &nErrors);
        }
        if (nOwnerKind == APP_RESUME_OWNER_UNIT_SLOT) {
            pszOwnerKind = "unitSlot";
        } else if (nOwnerKind == APP_RESUME_OWNER_GROUND_CELL) {
            pszOwnerKind = "groundCell";
        } else if (nOwnerKind == APP_RESUME_OWNER_UNSERIALIZED_SLOT) {
            pszOwnerKind = "unserializedSlot";
        }
        printf("pocketufo: campaign tact-resume-owners: stage=%s "
               "record=item pool=%d object=%d index=%u ammo=%u unk7=%u unk8=%u "
               "next=%d linkedWire=%d ownerCount=%d ownerKind=%s "
               "ownerOrdinal=%d ownerSlot=%d ownerCellSlot=%d\n",
               pszUseStage, (int)i, (int)nObject,
               (unsigned)pItem->nIndex, (unsigned)pItem->nAmmo,
               (unsigned)pItem->bUnk7, (unsigned)pItem->bUnk8,
               (int)nNext, (int)nLinked, (int)anItemOwnerCount[i],
               pszOwnerKind, (int)nOwnerOrdinal, (int)nOwnerSlot,
               (int)nOwnerCellSlot);
        printf("pocketufo: campaign tact-resume-owners: stage=%s "
               "record=ammoPayload parentPool=%d index=%u count=%u model=inline\n",
               pszUseStage, (int)i, (unsigned)pItem->nIndex,
               (unsigned)pItem->nAmmo);
        ++nItemRows;
        ++nAmmoPayloadRows;
    }

    for (i32 i = g_world.nTactItemCount; i < nItemCapacity; ++i) {
        TactItem *pTail = g_world.apTactItems[i];
        bool bAlias = false;
        if (pTail == NULL) {
            continue;
        }
        ++nTailNonNull;
        for (i32 j = 0; j < g_world.nTactItemCount; ++j) {
            if (g_world.apTactItems[j] == pTail) {
                bAlias = true;
                break;
            }
        }
        if (bAlias) {
            ++nTailActiveAlias;
        } else {
            ++nTailOpaque;
        }
    }
    printf("pocketufo: campaign tact-resume-owners: stage=%s "
           "record=poolTail active=%d capacity=%d tailSlots=%d "
           "tailNonNull=%d activeAlias=%d opaque=%d dereferenced=0\n",
           pszUseStage, (int)g_world.nTactItemCount, (int)nItemCapacity,
           (int)(nItemCapacity - g_world.nTactItemCount),
           (int)nTailNonNull, (int)nTailActiveAlias, (int)nTailOpaque);
    printf("pocketufo: campaign tact-resume-owners: stage=%s record=summary "
           "schema=1 status=%s wireUnits=%d sideMembers=%d rosterSlots=%d "
           "itemPool=%d cellScan=%d "
           "cellRows=%d cellUnitOwners=%d cellItemHeads=%d slots=%d "
           "items=%d ammoPayloads=%d footprints=%d errors=%d\n",
           pszUseStage, nErrors == 0 ? "complete" : "fail",
           (int)g_world.nSoldierCount, (int)nRosterMembers,
           (int)pMap->nXcomCount + (int)pMap->nEnemyCount
               + pMap->nCivilianCount + g_world.nMapEnemyReserveCount,
           (int)g_world.nTactItemCount,
           (int)PUFO_TERRAIN_CELL_SLOTS, (int)nCellRows,
           (int)nCellUnitOwners, (int)nCellItemHeads, (int)nSlotRows,
           (int)nItemRows, (int)nAmmoPayloadRows, (int)nFootprintRows,
           (int)nErrors);
    fflush(stdout);
    return nErrors == 0;
}
/* Keep this one checkpoint continuation bounded. It uses the normal load hook, speed-control
 * input, clock pulse bodies, funding close control, and save hook. It never sets game fields. */
static bool App_ScriptMonthStamps(const char *pszLabel)
{
    const GameTime *const pTime = &g_world.geo.time;
    const bool bTactOwner = World_bTactPointerLive(&g_world);
    const bool bReady = g_bWorldReady && g_nUiScreen == 22
        && g_world.bGeoscapeUi != 0 && g_world.screen.nGeoscapeScreen == 0
        && g_world.bTactLive == 0 && g_world.bTactFlag == 0 && !bTactOwner
        && g_world.sModal.bOpen == 0 && g_world.msgLines.nLineCount == 0u
        && pTime->bPaused == 0;

    printf("pocketufo: month-stamps label=%s world=%d start_screen=%d geo_screen=%d geo=%u "
           "tact=%u tactflag=%u tact_owner=%u modal=%u lines=%u pause=%u tickSec=%d "
           "totalSec=%d lastMinSec=%d lastHourSec=%d lastDaySec=%d monthEpochSec=%d "
           "date=%d/%d/%d weekday=%d time=%02d:%02d:%02d ready=%d\n",
           pszLabel != NULL ? pszLabel : "requested", g_bWorldReady ? 1 : 0,
           (int)g_nUiScreen, (int)g_world.screen.nGeoscapeScreen,
           (unsigned)g_world.bGeoscapeUi, (unsigned)g_world.bTactLive,
           (unsigned)g_world.bTactFlag, bTactOwner ? 1u : 0u,
           g_world.sModal.bOpen ? 1u : 0u, (unsigned)g_world.msgLines.nLineCount,
           (unsigned)pTime->bPaused, (int)g_world.geo.world.nTickSeconds,
           (int)pTime->nTotalSec, (int)pTime->nLastTickMinSec,
           (int)pTime->nLastTickHourSec, (int)pTime->nLastTickDaySec,
           (int)pTime->nMonthEpochSec, (int)pTime->nYear, (int)pTime->nMonth,
           (int)pTime->nDay, (int)pTime->nWeekday, (int)pTime->nHour,
           (int)pTime->nMinute, (int)pTime->nSecond, bReady ? 1 : 0);
    fflush(stdout);
    return bReady;
}

static bool App_ScriptMonthBoundaryFail(const char *pszStage, const char *pszWhy)
{
    printf("pocketufo: month-boundary: FAIL stage=%s why=%s screen=%d paused=%u lines=%u modal=%u funds=%d\n",
           pszStage, pszWhy, (int)g_world.screen.nGeoscapeScreen,
           (unsigned)g_world.geo.time.bPaused, (unsigned)g_world.msgLines.nLineCount,
           g_world.sModal.bOpen ? 1u : 0u, (int)g_world.nFunds);
    if (g_bWorldReady) {
        (void)App_ScriptMonthStamps(pszStage);
    }
    App_ScriptState(pszStage);
    App_ScriptLines();
    App_ScriptStrategyState(pszStage);
    (void)App_ScriptCampaignCargo(pszStage);
    App_ScriptStrateg();
    fflush(stdout);
    return false;
}

static void App_ScriptMonthBoundaryBudgetInputs(i32 nFundsBaseline,
                                                i32 nFundsBefore,
                                                i32 nBoundaryTicks)
{
    const PufoWorldState *const pW = &g_world;
    printf("pocketufo: month-budget report funds_baseline=%d funds_before=%d month_counter=%d "
           "bars=%d,%d,%d,%d,%d "
           "base_count=%d craft_count=%u soldier_count=%d site_count=%d pulses=%d\n",
           nFundsBaseline, nFundsBefore, (int)pW->nMonthCounter,
           pW->economy.anFundBar[0], pW->economy.anFundBar[1], pW->economy.anFundBar[2],
           pW->economy.anFundBar[3], pW->economy.anFundBar[4], (int)pW->nBaseCount,
           (unsigned)pW->bCraftCount, (int)pW->nSoldierCount, (int)pW->nSiteCount,
           nBoundaryTicks);

    for (i32 nBase = 0; nBase < (i32)pW->nBaseCount; ++nBase) {
        const BaseRec *const pBase = pW->apBases[nBase];
        const i32 nObject112 = g_assets.apObjects != NULL && g_assets.nObjectCount > 112
            ? Base_GetItemQty(g_assets.apObjects[112], nBase) : 0;
        const i32 nObject113 = g_assets.apObjects != NULL && g_assets.nObjectCount > 113
            ? Base_GetItemQty(g_assets.apObjects[113], nBase) : 0;
        const u8 *const pFacility = pW->abFacilityGrid + 480 + 144 * nBase;

        printf("pocketufo: month-budget base index=%d region=%d object112=%d object113=%d facilities=",
               (int)nBase, pBase != NULL ? (int)pBase->bRegionId : -1,
               nObject112, nObject113);
        for (i32 i = 0; i < 36; ++i) {
            printf("%s%u", i == 0 ? "" : ",", (unsigned)pFacility[i]);
        }
        printf("\n");
    }
    for (i32 i = 0; i < (i32)pW->bCraftCount; ++i) {
        const Ship *const pCraft = pW->apCraft[i];
        printf("pocketufo: month-budget craft index=%d type=%d home=%d\n", (int)i,
               pCraft != NULL ? (int)pCraft->bCraftType : -1,
               pCraft != NULL ? (int)pCraft->bHomeBase : -1);
    }
    for (i32 i = 0; i < (i32)pW->nSoldierCount; ++i) {
        const Soldier *const pSoldier = pW->apSoldiers[i];
        printf("pocketufo: month-budget soldier index=%d base=%d\n", (int)i,
               pSoldier != NULL ? (int)pSoldier->nKind : -1);
    }
    for (i32 i = 0; i < (i32)pW->nSiteCount; ++i) {
        const SmapSite *const pSite = &pW->apSites[i];
        printf("pocketufo: month-budget site index=%d base=%d recovered_assign=%d item224=%u item226=%u\n",
               (int)i, (int)(signed char)pSite->bFlag1,
               (int)(signed char)pSite->bRecoveredSoldierAssign,
               (unsigned)pSite->auItemQty[224], (unsigned)pSite->auItemQty[226]);
    }
    fflush(stdout);
}

static bool App_ScriptMonthBoundary(void)
{
    /* The pinned slot starts 1999/1/17 20:07. At speed 3600, 400 real pulse bodies
     * cross 1999/2/1 with room to spare; screen 19 stops further clock advance. */
    static const i32 nMaxPulses = 400;
    const ObjectRecFull *pEngineer, *pScientist;
    UiControl *pSpeed;
    UiControl *pClose;
    i32 nEngineerStock, nEngineerFree, nScientistStock, nScientistFree;
    i32 nFundsAtStart, nFundsBefore, nMonthBefore;
    i32 nPsiBaseExpected = -1, nExpectedReturnScreen;
    i32 nBoundaryTicks = 0;
    bool bCargoBefore;

    if (App_HookLoadGame(3, NULL) == 0) {
        return App_ScriptMonthBoundaryFail("load-slot-3", "checkpoint_load_failed");
    }
    if (!g_bWorldReady || g_world.bGeoscapeUi == 0
        || g_world.screen.nGeoscapeScreen != 0 || g_world.bTactLive != 0
        || World_bTactPointerLive(&g_world) || g_world.sModal.bOpen != 0
        || g_world.msgLines.nLineCount != 0u || g_world.geo.time.bPaused != 0
        || g_world.nBaseCount != 1 || g_world.nFunds != 4115500
        || g_world.geo.time.nYear != 1999 || g_world.geo.time.nMonth != 1
        || g_world.geo.time.nDay != 17 || g_world.geo.time.nHour != 20
        || g_world.geo.time.nMinute != 7 || g_world.geo.world.nTickSeconds != 5
        || g_world.nSoldierCount != 8 || g_world.bCraftCount != 3
        || g_world.nSiteCount != 0 || g_world.apBases[0] == NULL
        || g_assets.apObjects == NULL || g_assets.nObjectCount <= 113
        || g_world.nResearchEntries <= 14) {
        return App_ScriptMonthBoundaryFail("load-slot-3", "checkpoint_state_mismatch");
    }
    pEngineer = g_assets.apObjects[112];
    pScientist = g_assets.apObjects[113];
    nEngineerStock = Base_GetItemQty(g_assets.apObjects[20], 0);
    nEngineerFree = pEngineer != NULL
        ? Base_GetEngineerCount(pEngineer, g_world.auManufCol, 0,
                                (i32)g_world.nResearchEntries) : 0;
    nScientistStock = Base_GetItemQty(pScientist, 0);
    nScientistFree = pScientist != NULL
        ? Base_GetScientistCount(pScientist, g_world.auResearchCol, 0,
                                 (i32)g_world.nResearchEntries) : 0;
    if (nEngineerStock != 1 || nEngineerFree != 10
        || Base_GetItemQty(pEngineer, 0) != 10
        || nScientistStock != 10 || nScientistFree != 10
        || g_world.aTopics[14].bActive != 0 || g_world.aTopics[14].bUnlocked == 0
        || g_world.aTopics[14].wProgress < g_world.aTopics[14].wTotal) {
        return App_ScriptMonthBoundaryFail("load-slot-3", "checkpoint_stock_staff_or_research_mismatch");
    }
    nFundsAtStart = g_world.nFunds;
    printf("pocketufo: month-boundary cold-load PASS funds=%d item20=%d engineers=%d/10 scientists=%d/10 research14=%u/%u month=%d day=%d time=%02d:%02d bases=%d soldiers=%d craft=%u\n",
           (int)g_world.nFunds, nEngineerStock, nEngineerFree, nScientistFree,
           (unsigned)g_world.aTopics[14].wProgress,
           (unsigned)g_world.aTopics[14].wTotal,
           (int)g_world.geo.time.nMonth, (int)g_world.geo.time.nDay,
           (int)g_world.geo.time.nHour, (int)g_world.geo.time.nMinute,
           (int)g_world.nBaseCount, (int)g_world.nSoldierCount,
           (unsigned)g_world.bCraftCount);
    if (!App_ScriptMonthStamps("month-before")) {
        return App_ScriptMonthBoundaryFail("month-before", "checkpoint_scene_not_ready");
    }
    App_ScriptState("month-before");
    App_ScriptStrategyState("month-before");
    bCargoBefore = App_ScriptCampaignCargo("month-before");
    App_ScriptLines();
    App_ScriptStrateg();
    if (!bCargoBefore) {
        return App_ScriptMonthBoundaryFail("month-before", "cargo_probe_failed");
    }

    pSpeed = g_world.ui.apRootCtl[7];
    if (pSpeed == NULL || pSpeed->bEnabled == 0 || pSpeed->pfnClick == NULL
        || !App_ScriptNormalClickControl(pSpeed)
        || g_world.geo.world.nTickSeconds != 3600 || pSpeed->nState != 1u
        || g_world.screen.nGeoscapeScreen != 0 || g_world.msgLines.nLineCount != 0u
        || g_world.sModal.bOpen != 0 || g_world.geo.time.bPaused != 0) {
        return App_ScriptMonthBoundaryFail("speed-control-7", "normal_speed_click_failed_or_modal_opened");
    }
    printf("pocketufo: month-boundary speed PASS control=7 tickSec=%d\n",
           (int)g_world.geo.world.nTickSeconds);
    App_ScriptStrategyControl("speed", 7, pSpeed);

    nMonthBefore = (i32)g_world.nMonthCounter;
    for (i32 i = 0; i < nMaxPulses; ++i) {
        App_ScriptTicks(1);
        if (g_world.screen.nGeoscapeScreen == 19) {
            nBoundaryTicks = i + 1;
            break;
        }
        if (g_world.screen.nGeoscapeScreen == 0
            && g_world.msgLines.nLineCount == 1u
            && g_world.msgLines.abLineFlags[0] == PUFO_STATUS_SELFLAG
            && g_world.geo.time.bPaused != 0) {
            printf("pocketufo: month-boundary contact pulse=%d flag=10\n", (int)i + 1);
            App_ScriptLines();
            if (!App_ScriptCampaignAckConfirm()) {
                return App_ScriptMonthBoundaryFail("clock-contact", "normal_flag10_ack_failed");
            }
            pSpeed = g_world.ui.apRootCtl[7];
            if (pSpeed == NULL || pSpeed->bEnabled == 0 || pSpeed->pfnClick == NULL
                || !App_ScriptNormalClickControl(pSpeed)
                || g_world.geo.world.nTickSeconds != 3600 || pSpeed->nState != 1u) {
                return App_ScriptMonthBoundaryFail("clock-contact", "normal_hourly_speed_restore_failed");
            }
        }
        if (g_world.screen.nGeoscapeScreen != 0 || g_world.bGeoscapeUi == 0
            || g_world.bTactLive != 0 || World_bTactPointerLive(&g_world)
            || g_world.sModal.bOpen != 0 || g_world.msgLines.nLineCount != 0u
            || g_world.geo.time.bPaused != 0) {
            return App_ScriptMonthBoundaryFail("clock-pulse", "unexpected_screen_message_or_pause");
        }
    }
    if (g_world.screen.nGeoscapeScreen != 19 || nBoundaryTicks == 0
        || g_world.nMonthCounter != nMonthBefore + 1
        || g_world.geo.time.nMonth != 2 || g_world.msgLines.nLineCount != 0u
        || g_world.sModal.bOpen != 0 || g_world.bTactLive != 0
        || World_bTactPointerLive(&g_world)) {
        return App_ScriptMonthBoundaryFail("month-report-open", "screen_19_not_open_or_state_invalid");
    }

    App_ScriptDump("month_report.ppm");
    (void)App_ScriptMonthStamps("month-report-open");
    App_ScriptState("month-report-open");
    App_ScriptStrategyState("month-report-open");
    App_ScriptLines();
    App_ScriptStrateg();
    nFundsBefore = g_world.nFunds;
    if (nFundsBefore != nFundsAtStart) {
        printf("pocketufo: month-budget preclose-drift funds_baseline=%d funds_report_before=%d delta=%d\n",
               (int)nFundsAtStart, (int)nFundsBefore,
               (int)(nFundsBefore - nFundsAtStart));
        return App_ScriptMonthBoundaryFail("month-report-open", "funds_changed_before_report_close");
    }
    App_ScriptMonthBoundaryBudgetInputs(nFundsAtStart, nFundsBefore, nBoundaryTicks);

    for (i32 nBase = 0; nBase < (i32)g_world.nBaseCount; ++nBase) {
        const i32 nPsiCapacity = Base_CalcPsiLabCap(g_world.abFacilityGrid, nBase);
        printf("pocketufo: month-psi predicate base_index=%d capacity=%d\n",
               (int)nBase, (int)nPsiCapacity);
        if (nPsiCapacity > 0 && nPsiBaseExpected < 0) {
            nPsiBaseExpected = nBase;
        }
    }
    nExpectedReturnScreen = nPsiBaseExpected >= 0 ? 17 : 0;
    printf("pocketufo: month-psi expected_base=%d expected_screen=%d\n",
           (int)nPsiBaseExpected, (int)nExpectedReturnScreen);

    pClose = g_world.ui.apCtl[GEOUI_CTL_186B00];
    if (pClose == NULL || pClose != g_world.fundingInit.pCloseCtl
        || pClose->pfnClick == NULL
        || pClose->pfnClick != g_world.fundingInit.pfnCloseClick
        || pClose->bEnabled == 0 || pClose->uWidth == 0 || pClose->uHeight == 0
        || pClose->x != 224 || pClose->y != 1
        || pClose->uWidth != 15 || pClose->uHeight != 15
        || (i32)pClose->x + (i32)pClose->uWidth > 240
        || (i32)pClose->y + (i32)pClose->uHeight > 320) {
        return App_ScriptMonthBoundaryFail("month-report-close", "live_funding_close_control_mismatch");
    }
    if (!App_ScriptNormalClickControl(pClose)) {
        return App_ScriptMonthBoundaryFail("month-report-close", "normal_close_input_failed");
    }
    if (g_world.screen.nGeoscapeScreen != nExpectedReturnScreen
        || (nExpectedReturnScreen == 17
            && (i32)g_world.nShownBaseIdx != nPsiBaseExpected)
        || g_world.msgLines.nLineCount != 0u || g_world.sModal.bOpen != 0
        || g_world.bTactLive != 0 || World_bTactPointerLive(&g_world)) {
        return App_ScriptMonthBoundaryFail("month-report-closed", "close_screen_did_not_match_psi_predicate");
    }
    printf("pocketufo: month-budget actual funds_after=%d delta=%d screen=%d expected_screen=%d psi_base=%d shown_base=%d\n",
           (int)g_world.nFunds, (int)(g_world.nFunds - nFundsBefore),
           (int)g_world.screen.nGeoscapeScreen, (int)nExpectedReturnScreen,
           (int)nPsiBaseExpected, (int)g_world.nShownBaseIdx);
    (void)App_ScriptMonthStamps("month-after-close");
    App_ScriptState("month-after-close");
    App_ScriptStrategyState("month-after-close");
    App_ScriptLines();
    if (!App_ScriptCampaignCargo("month-after-close")) {
        return App_ScriptMonthBoundaryFail("month-after-close", "cargo_probe_failed");
    }
    if (g_world.nFunds < PUFO_FUNDS_BANKRUPT_LIMIT
        || g_world.nBaseCount != 1 || g_world.nSoldierCount != 8
        || g_world.bCraftCount != 3 || g_world.nSiteCount != 0
        || Base_GetItemQty(g_assets.apObjects[20], 0) != nEngineerStock
        || Base_GetEngineerCount(pEngineer, g_world.auManufCol, 0,
                                 (i32)g_world.nResearchEntries) != nEngineerFree
        || Base_GetItemQty(pScientist, 0) != nScientistStock
        || Base_GetScientistCount(pScientist, g_world.auResearchCol, 0,
                                  (i32)g_world.nResearchEntries) != nScientistFree
        || g_world.aTopics[14].bActive != 0 || g_world.aTopics[14].bUnlocked == 0
        || g_world.aTopics[14].wProgress < g_world.aTopics[14].wTotal) {
        return App_ScriptMonthBoundaryFail("month-after-close", "stock_staff_research_or_funds_invalid");
    }
    if (App_HookSaveGame(4, NULL) == 0) {
        return App_ScriptMonthBoundaryFail("save-slot-4", "save_failed");
    }
    printf("pocketufo: month-boundary PASS slot3_to_report19_close_save4 ticks=%d\n",
           nBoundaryTicks);
    fflush(stdout);
    return true;
}

static bool App_ScriptCampaign(const char *pszAction, const char *pszLabel)
{
    bool bOk = false;

    if (strcmp(pszAction, "gear") == 0) {
        bOk = App_ScriptCampaignStageGear();
    } else if (strcmp(pszAction, "contact") == 0) {
        bOk = App_ScriptCampaignCreateContact();
    } else if (strcmp(pszAction, "base-defense-entry") == 0) {
        bOk = App_ScriptCampaignStageBaseDefense();
    } else if (strcmp(pszAction, "trace") == 0) {
        App_ScriptCampaignTrace(pszLabel);
        return true;
    } else if (strcmp(pszAction, "home-observe") == 0) {
        App_ScriptCampaignHomeObserve(pszLabel);
        return true;
    } else if (strcmp(pszAction, "tact-resume-owners") == 0) {
        return App_ScriptCampaignTactResumeOwners(pszLabel);
    } else if (strcmp(pszAction, "home-order") == 0) {
        bOk = App_ScriptCampaignHomeOrder();
    } else if (strcmp(pszAction, "home-wait") == 0) {
        bOk = App_ScriptCampaignHomeWait();
    } else if (strcmp(pszAction, "ack-confirm") == 0) {
        bOk = App_ScriptCampaignAckConfirm();
    } else if (strcmp(pszAction, "ack-status") == 0) {
        if (strcmp(pszLabel, "34") == 0) {
            bOk = App_ScriptCampaignAckStatus(34);
        } else if (strcmp(pszLabel, "37") == 0) {
            bOk = App_ScriptCampaignAckStatus(37);
        } else if (strcmp(pszLabel, "38") == 0) {
            bOk = App_ScriptCampaignAckStatus(38);
        } else if (strcmp(pszLabel, "41") == 0) {
            bOk = App_ScriptCampaignAckStatus(41);
        } else {
            printf("pocketufo: campaign ack-status: FAIL stage=argument value=%s expected=34|37|38|41\n",
                   pszLabel != NULL ? pszLabel : "requested");
            fflush(stdout);
        }
    } else if (strcmp(pszAction, "month-boundary") == 0) {
        bOk = App_ScriptMonthBoundary();
    } else if (strcmp(pszAction, "month-stamps") == 0) {
        bOk = App_ScriptMonthStamps(pszLabel);
        if (!bOk) {
            App_ScriptLines();
        }
        return bOk;
    } else if (strcmp(pszAction, "navcycle") == 0) {
        bOk = App_ScriptCampaignNavCycle();
    } else if (strcmp(pszAction, "launch") == 0) {
        bOk = App_ScriptCampaignLaunchContact();
    } else if (strcmp(pszAction, "deploy") == 0) {
        bOk = App_ScriptCampaignDeploy();
    } else if (strcmp(pszAction, "grenade") == 0) {
        bOk = App_ScriptCampaignPickupGrenade();
    } else if (strcmp(pszAction, "move") == 0) {
        bOk = App_ScriptCampaignMove(false);
    } else if (strcmp(pszAction, "moveprobe") == 0) {
        bOk = App_ScriptCampaignMove(true);
    } else if (strcmp(pszAction, "approach") == 0) {
        char *pszEnd = NULL;
        const long nEnemy = (pszLabel != NULL) ? strtol(pszLabel, &pszEnd, 10) : -1;
        if (pszLabel == NULL || pszEnd == pszLabel || *pszEnd != '\0' || nEnemy < 0
            || nEnemy > INT32_MAX) {
            printf("pocketufo: campaign approach: FAIL (need an enemy roster index)\n");
            fflush(stdout);
            return false;
        }
        bOk = App_ScriptCampaignApproach((i32)nEnemy, false, NULL, NULL, NULL);
    } else if (strcmp(pszAction, "fire") == 0) {
        bOk = App_ScriptCampaignFire(NULL, NULL, 0, NULL);
    } else if (strcmp(pszAction, "throw") == 0) {
        bOk = App_ScriptCampaignThrow();
    } else if (strcmp(pszAction, "enemyturn") == 0) {
        bOk = App_ScriptCampaignEnemyTurn(pszLabel != NULL
                                          && strcmp(pszLabel, "grenade") == 0, false);
    } else if (strcmp(pszAction, "natural-defeat") == 0
               || strcmp(pszAction, "natural-defeat-exposed") == 0) {
        const bool bExposeDefenders = strcmp(pszAction, "natural-defeat-exposed") == 0;
        char *pszEnd = NULL;
        const long nTurns = (pszLabel != NULL) ? strtol(pszLabel, &pszEnd, 10) : 0;
        if (pszLabel == NULL || pszEnd == pszLabel || *pszEnd != '\0'
            || nTurns <= 0 || nTurns > 32) {
            printf("pocketufo: campaign natural-defeat%s: FAIL (need a bounded turn count from 1 to 32)\n",
                   bExposeDefenders ? "-exposed" : "");
            fflush(stdout);
            return false;
        }
        bOk = App_ScriptCampaignNaturalDefeat((i32)nTurns, bExposeDefenders);
    } else if (strcmp(pszAction, "natural-fight") == 0
               || strcmp(pszAction, "natural-fight-outside-plane") == 0) {
        const bool bOutsidePlane = strcmp(pszAction, "natural-fight-outside-plane") == 0;
        char *pszEnd = NULL;
        const long nTurns = (pszLabel != NULL) ? strtol(pszLabel, &pszEnd, 10) : 0;
        if (pszLabel == NULL || pszEnd == pszLabel || *pszEnd != '\0'
            || nTurns <= 0 || nTurns > (bOutsidePlane ? 12 : 32)) {
            printf("pocketufo: campaign %s: FAIL (need a bounded turn count from 1 to %d)\n",
                   bOutsidePlane ? "natural-fight-outside-plane" : "natural-fight",
                   bOutsidePlane ? 12 : 32);
            fflush(stdout);
            return false;
        }
        bOk = App_ScriptCampaignNaturalFight((i32)nTurns, bOutsidePlane);
    } else if (strcmp(pszAction, "abort") == 0) {
        bOk = App_ScriptCampaignAbort();
    } else if (strcmp(pszAction, "close-debrief") == 0) {
        bOk = App_ScriptCampaignCloseDebrief();
    } else if (strcmp(pszAction, "state") == 0) {
        App_ScriptCampaignState((pszLabel != NULL) ? pszLabel : "requested");
        return true;
    } else if (strcmp(pszAction, "cargo") == 0) {
        bOk = App_ScriptCampaignCargo((pszLabel != NULL) ? pszLabel : "requested");
    } else {
        printf("pocketufo: campaign: UNKNOWN action '%s'\n", pszAction);
        fflush(stdout);
        return false;
    }
    if (strcmp(pszAction, "contact") == 0) {
        printf("pocketufo: campaign contact creation: %s\n", bOk ? "PASS" : "FAIL");
    } else if (strcmp(pszAction, "abort") == 0 && bOk) {
        printf("pocketufo: campaign abort: PASS stage=debrief_exit_persisted\n");
    } else if (strcmp(pszAction, "enemyturn") == 0
               && ((g_world.nUiScreen == 18u && g_world.bTactLive == 0
                    && g_world.pTactMap == NULL && g_world.pTactState == NULL)
                   || (g_world.nUiScreen == 13u && g_world.bMapBattleEnded != 0))) {
        /* The helper already printed the explicit terminal screen and outcome state. */
        return bOk;
    } else {
        printf("pocketufo: campaign %s: %s\n", pszAction, bOk ? "PASS" : "FAIL");
    }
    fflush(stdout);
    return bOk;
}

/* SHELL FIXTURE: run the real world check on a live tactical frame with the legal failed-base branch
 * inputs. This follows tests/test_geoscapeframe.c's existing model: the newly placed base is the one
 * live base, then the scenario is marked as a base defence with no victory. The check itself is not
 * mocked: raw ARM at 0x3515c reads the signed base count, 0x35168 compares mission type 3, 0x35174
 * checks victory is zero, and 0x35180/0x35188 call Tact_Shutdown/EndScreen_Init. */
static void App_ScriptEndCheck(void)
{
    bool bEnded;
    i32 nScrollBefore;

    if (!g_bWorldReady || !World_bTactFrameLive(&g_world) || g_world.nBaseCount != 1
        || g_world.apBases[0] == NULL || g_world.bMapBattleEnded != 0) {
        printf("pocketufo: ending fixture: FAIL (need a live tact frame and one live base)\n");
        fflush(stdout);
        return;
    }

    g_world.nMapMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    g_world.nBaseCount = (i8)PUFO_FACILITY_END_FLAG_SET;
    g_world.bMapMissionVictory = 0;
    bEnded = World_MissionEndCheck(&g_world, 1);

    printf("pocketufo: ending fixture end: ended %d, screen %d/%d, rows %d, backdrop %d, tact live %d\n",
           bEnded ? 1 : 0, (int)g_world.nUiScreen, (int)g_nUiScreen, (int)g_nTitlesMaxRows,
           g_pEndScreenBackdrop != NULL ? 1 : 0, g_world.bTactLive ? 1 : 0);
    if (!bEnded || g_world.nUiScreen != 18u || g_nUiScreen != 18 || g_nTitlesMaxRows != 16
        || g_pEndScreenBackdrop == NULL || g_world.bTactLive) {
        printf("pocketufo: ending fixture: FAIL (end check did not initialize screen 18)\n");
        fflush(stdout);
        return;
    }

    App_DrawFrame();
    nScrollBefore = g_world.nMapTitlesScroll;
    App_TickAnim();
    App_DrawFrame();
    printf("pocketufo: ending fixture tick: scroll %d -> %d\n",
           (int)nScrollBefore, (int)g_world.nMapTitlesScroll);
    fflush(stdout);
}

/* SHELL FIXTURE: set the scan's two raw gate bytes and use the live map cells. The raw address
 * `Map+0x193F0` starts at apCells layer 1, row 9, column 2; spawn-tile coordinates add ten cells
 * per tile. The raw scan reads each cell's +0x0C pointer, which MapCell_SetTileSprites stores as
 * the ceiling sprite, and succeeds if any of the four is NULL. */
static void App_ScriptGoodEndCheck(void)
{
    const i32 nSpawnX = g_world.nMapSpawnTileX;
    const i32 nSpawnY = g_world.nMapSpawnTileY;
    const i32 nCellX0 = 2 + PUFO_CELLS_PER_TILE * nSpawnX;
    const i32 nCellY0 = 9 + PUFO_CELLS_PER_TILE * nSpawnY;
    i32 nOpenCeiling = 0;
    bool bEnded;

    if (!g_bWorldReady || !World_bTactFrameLive(&g_world) || g_world.bMapBattleEnded != 0) {
        printf("pocketufo: good-ending fixture: FAIL (need a live, unended tactical frame)\n");
        fflush(stdout);
        return;
    }

    for (i32 dy = 0; dy < 2; ++dy) {
        for (i32 dx = 0; dx < 2; ++dx) {
            const i32 nX = nCellX0 + dx;
            const i32 nY = nCellY0 + dy;
            MapCell *pCell = App_ScriptCampaignCellAt(nX, nY, 1);

            if (pCell == NULL) {
                printf("pocketufo: good-ending fixture: FAIL (missing layer-1 cell %d/%d)\n",
                       nX, nY);
                fflush(stdout);
                return;
            }
            if (pCell->pCeilSpr == NULL) {
                ++nOpenCeiling;
            }
            printf("pocketufo: good-ending probe: %d/%d cell %d ceiling-null %d\n",
                   dx, dy, nX, pCell->pCeilSpr == NULL ? 1 : 0);
        }
    }
    if (nOpenCeiling == 0) {
        printf("pocketufo: good-ending fixture: FAIL (the live 2x2 probe has four ceilings)\n");
        fflush(stdout);
        return;
    }

    g_world.nMapMissionType = PUFO_MISSION_TYPE_ALIEN_BASE;
    g_world.bMapSpawnAlt = 1;
    bEnded = World_MissionEndCheck(&g_world, 0);
    printf("pocketufo: good-ending fixture: ended %d, screen %d/%d, rows %d, backdrop %d, tact live %d, "
           "open ceilings %d, spawn %d/%d\n",
           bEnded ? 1 : 0, (int)g_world.nUiScreen, (int)g_nUiScreen, (int)g_nTitlesMaxRows,
           g_pEndScreenBackdrop != NULL ? 1 : 0, g_world.bTactLive ? 1 : 0,
           nOpenCeiling, nSpawnX, nSpawnY);
    if (!bEnded || g_world.nUiScreen != 18u || g_nUiScreen != 18 || g_nTitlesMaxRows != 10
        || g_pEndScreenBackdrop == NULL || g_world.bTactLive) {
        printf("pocketufo: good-ending fixture: FAIL (live scan did not initialize the good ending)\n");
        fflush(stdout);
        return;
    }

    App_DrawFrame();
    App_TickAnim();
    App_DrawFrame();
}

/* SHELL FIXTURE: stage the calendar six seconds before February, then use one real app pulse. The
 * clock stamps match the staged time so no day/hour/minute callbacks run. Funds go below the limit
 * only after the report opens, so close 19 still runs its own bankruptcy branch. */
static void App_ScriptBankruptcyReport(void)
{
    GameTime *pTime = &g_world.geo.time;
    const i32 nBeforeSec = 31 * GAME_SECONDS_DAY - 6;
    i32 nMonthBefore;
    i32 nTickSecondsSave;

    if (!g_bWorldReady || g_world.bGeoscapeUi == 0 || g_world.screen.nGeoscapeScreen != 0
        || g_world.nShortfall1870C8 != 0) {
        printf("pocketufo: bankruptcy fixture: FAIL (ready %d, geoscape %d, screen %d, action-fail %d)\n",
               g_bWorldReady ? 1 : 0, (int)g_world.bGeoscapeUi,
               (int)g_world.screen.nGeoscapeScreen, (int)g_world.nShortfall1870C8);
        fflush(stdout);
        return;
    }

    nTickSecondsSave = g_world.geo.world.nTickSeconds;
    g_world.geo.world.nTickSeconds = 5;
    pTime->bPaused = 0;
    pTime->nTotalSec = nBeforeSec;
    pTime->nLastTickMinSec = nBeforeSec;
    pTime->nLastTickHourSec = nBeforeSec;
    pTime->nLastTickDaySec = nBeforeSec;
    pTime->nMonthEpochSec = nBeforeSec;
    (void)GameTime_Decode(pTime);
    nMonthBefore = pTime->nMonth;
    App_TickPulse();
    g_world.geo.world.nTickSeconds = nTickSecondsSave;
    if (g_world.screen.nGeoscapeScreen == 19) {
        g_world.nFunds = PUFO_FUNDS_BANKRUPT_LIMIT - 10000000;
    }

    printf("pocketufo: bankruptcy fixture report: open %d, funds %d, screen %d, pulse 1, seconds %d->%d, tick 5, month %d->%d, paused %d, action-fail %d\n",
           g_world.screen.nGeoscapeScreen == 19 ? 1 : 0, (int)g_world.nFunds,
           (int)g_world.screen.nGeoscapeScreen, nBeforeSec, (int)pTime->nTotalSec,
           nMonthBefore, (int)pTime->nMonth,
           (int)pTime->bPaused, (int)g_world.nShortfall1870C8);
    fflush(stdout);
}

static void App_ScriptEndingState(void)
{
    printf("pocketufo: ending state: screen %d/%d, rows %d, backdrop %d, scroll %d, geo %d, hold %d, ended %d, tact %d\n",
           (int)g_world.nUiScreen, (int)g_nUiScreen, (int)g_nTitlesMaxRows,
           g_pEndScreenBackdrop != NULL ? 1 : 0, (int)g_world.nMapTitlesScroll,
           (int)g_world.bGeoscapeUi, (int)g_world.bMapTickHold, (int)g_world.bMapBattleEnded,
           g_world.bTactLive ? 1 : 0);
    fflush(stdout);
}

static void App_RunDebugScript(LPCWSTR pwszPath)
{
    static char azBuf[8192];
    HANDLE      hFile;
    DWORD       cbRead = 0;
    char       *pszLine;
    char       *pszNext;
    char        azArg[3][64];
    i32         nArgs;

    hFile = CreateFileW(pwszPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        printf("pocketufo: --script: cannot open the script\n");
        fflush(stdout);
        return;
    }
    if (!ReadFile(hFile, azBuf, (DWORD)sizeof(azBuf) - 1, &cbRead, NULL)) {
        cbRead = 0;
    }
    (void)CloseHandle(hFile);
    azBuf[cbRead] = '\0';

    for (pszLine = azBuf; pszLine != NULL && *pszLine != '\0'; pszLine = pszNext) {
        char *pszEnd = strchr(pszLine, '\n');

        pszNext = (pszEnd != NULL) ? pszEnd + 1 : NULL; /* the next line, read before the newline
                                                         * below is replaced */

        if (pszEnd != NULL) {
            *pszEnd = '\0';
        }
        if (strchr(pszLine, '\r') != NULL) {
            *strchr(pszLine, '\r') = '\0';
        }
        if (*pszLine == '#' || *pszLine == ' ') {
            continue;
        }

        nArgs = 0;
        for (char *pszTok = pszLine; pszTok != NULL && nArgs < 3; ++nArgs) {
            i32 i = 0;
            while (*pszTok == ' ') {
                ++pszTok;
            }
            while (*pszTok != '\0' && *pszTok != ' ' && i < 63) {
                azArg[nArgs][i++] = *pszTok++;
            }
            azArg[nArgs][i] = '\0';
            if (i == 0) {
                break;
            }
        }
        if (nArgs == 0) {
            continue;
        }

        if (strcmp(azArg[0], "dump") == 0 && nArgs > 1) {
            App_ScriptDump(azArg[1]);
        } else if (strcmp(azArg[0], "click") == 0 && nArgs > 2) {
            App_ScriptPointerLogical((i32)atoi(azArg[1]), (i32)atoi(azArg[2]), true);
            App_OnPointerUp();
            printf("pocketufo: step click %s %s\n", azArg[1], azArg[2]);
        } else if (strcmp(azArg[0], "press") == 0 && nArgs > 2) {
            App_ScriptPointerLogical((i32)atoi(azArg[1]), (i32)atoi(azArg[2]), true);
            printf("pocketufo: step press %s %s\n", azArg[1], azArg[2]);
        } else if (strcmp(azArg[0], "release") == 0) {
            App_OnPointerUp();
            printf("pocketufo: step release\n");
        } else if (strcmp(azArg[0], "confirm") == 0) {
            (void)App_ScriptCampaignKey(PUFO_KEY_CONFIRM);
            printf("pocketufo: step confirm\n");
        } else if (strcmp(azArg[0], "tick") == 0 && nArgs > 1) {
            App_ScriptTicks((i32)atoi(azArg[1]));
        } else if (strcmp(azArg[0], "fasttick") == 0 && nArgs > 1) {
            App_ScriptFastTicks((i32)atoi(azArg[1]));
        } else if (strcmp(azArg[0], "gametick") == 0 && nArgs > 1) {
            App_ScriptGameTick((i32)atoi(azArg[1]));
            printf("pocketufo: step gametick %s\n", azArg[1]);
        } else if (strcmp(azArg[0], "tact") == 0) {
            App_ScriptTact();
            printf("pocketufo: step tact\n");
        } else if (strcmp(azArg[0], "nativeinfo") == 0) {
            App_ScriptNativeTactInfo();
        } else if (strcmp(azArg[0], "newgame") == 0) {
            App_StartNewGameFromShortcut();
            printf("pocketufo: step newgame\n");
        } else if (strcmp(azArg[0], "pick") == 0) {
            App_NewGamePickThroughPen();
            printf("pocketufo: step pick\n");
        } else if (strcmp(azArg[0], "close") == 0) {
            World_CloseGeoscape(&g_world);
            printf("pocketufo: step close\n");
        } else if (strcmp(azArg[0], "equiparmor") == 0) {
            App_ScriptEquipArmour();
        } else if (strcmp(azArg[0], "launch") == 0) {
            /* SHELL SCAFFOLDING (Part 150): run the launch tail the descent panel's control reaches --
             * 0x83ABC GeoscapeUi_FreeAll(0), which clears Map_bGeoscapeUi at 0x86D34 and so opens
             * GameTick_OnTimer's tactical branch (0x27508), then 0x83AC4 Tact_InitMission mode 0. Scripted
             * runs stage no descent panel, so the two seam hooks are dispatched directly. */
            printf("pocketufo: step launch (0x83ABC then 0x83AC4): %d\n",
                   World_ScriptLaunchMissionTail(&g_world));
            App_ScriptTact();
        } else if (strcmp(azArg[0], "mindendcheck") == 0) {
            App_ScriptEndCheck();
        } else if (strcmp(azArg[0], "mindgoodcheck") == 0) {
            App_ScriptGoodEndCheck();
        } else if (strcmp(azArg[0], "mindbankreport") == 0) {
            App_ScriptBankruptcyReport();
        } else if (strcmp(azArg[0], "mindendstate") == 0) {
            App_ScriptEndingState();
        } else if (strcmp(azArg[0], "save") == 0 && nArgs > 1) {
            (void)App_HookSaveGame((i32)atoi(azArg[1]), NULL);
            printf("pocketufo: step save slot %s\n", azArg[1]);
        } else if (strcmp(azArg[0], "load") == 0 && nArgs > 1) {
            (void)App_HookLoadGame((i32)atoi(azArg[1]), NULL);
            printf("pocketufo: step load slot %s\n", azArg[1]);
        } else if (strcmp(azArg[0], "state") == 0) {
            App_ScriptState((nArgs > 1) ? azArg[1] : "");
        } else if (strcmp(azArg[0], "grid") == 0) {
            App_ScriptGrid((nArgs > 1) ? atoi(azArg[1]) : 0);
        } else if (strcmp(azArg[0], "geoframe") == 0) {
            App_ScriptGeoFrame();
        } else if (strcmp(azArg[0], "strateg") == 0) {
            App_ScriptStrateg();
        } else if (strcmp(azArg[0], "strategystate") == 0) {
            App_ScriptStrategyState((nArgs > 1) ? azArg[1] : "requested");
        } else if (strcmp(azArg[0], "lines") == 0) {
            App_ScriptLines();
        } else if (strcmp(azArg[0], "audio_wait_music") == 0 && nArgs > 1) {
            i32 nTimeoutMs = atoi(azArg[1]);
            bool bDevice = PufoAudioWinmm_HasDevice();
            u32 nRate = PufoAudioWinmm_GetOutputRate();
            printf("pocketufo: audio-proof music device=%d rate=%u musicOff=%u timeout=%d\n",
                   bDevice ? 1 : 0, nRate, (unsigned)g_options.bMusicOff, (int)nTimeoutMs);
            if (nTimeoutMs <= 0 || nTimeoutMs > 15000 || g_options.bMusicOff != 0u
                || !bDevice || nRate != PUFO_AUDIO_MIXRATE) {
                printf("pocketufo: audio-proof music precondition failed\n");
            } else if (PufoAudioWinmm_WaitMusicDone((u32)nTimeoutMs)) {
                printf("pocketufo: audio-proof music-completed-nonzero-buffer\n");
            } else {
                printf("pocketufo: audio-proof music-timeout\n");
            }
        } else if (strcmp(azArg[0], "audio_drain_sfx") == 0 && nArgs > 1) {
            i32 nMaxSignals = atoi(azArg[1]);
            if (nMaxSignals <= 0 || nMaxSignals > 32) {
                printf("pocketufo: audio-proof sfx-drain-invalid-limit %d\n", (int)nMaxSignals);
            } else {
                PufoAudioWinmm_DrainDone(nMaxSignals);
                printf("pocketufo: audio-proof sfx-baseline-drained max=%d (no playback)\n",
                       (int)nMaxSignals);
            }
        } else if (strcmp(azArg[0], "audio_wait_sfx") == 0 && nArgs > 2) {
            i32 nSample = atoi(azArg[1]);
            i32 nTimeoutMs = atoi(azArg[2]);
            bool bDevice = PufoAudioWinmm_HasDevice();
            u32 nRate = PufoAudioWinmm_GetOutputRate();
            printf("pocketufo: audio-proof sfx device=%d rate=%u soundOff=%u expectedSample=%d count=%d timeout=%d\n",
                   bDevice ? 1 : 0, nRate, (unsigned)g_options.bSoundOff, (int)nSample,
                   (int)Sound_GetSampleCount(), (int)nTimeoutMs);
            if (nSample < 0 || nSample >= Sound_GetSampleCount() || nTimeoutMs <= 0
                || nTimeoutMs > 10000 || g_options.bSoundOff != 0u || !bDevice
                || nRate != PUFO_AUDIO_MIXRATE) {
                printf("pocketufo: audio-proof sfx precondition failed\n");
            } else if (PufoAudioWinmm_WaitDone((u32)nTimeoutMs)) {
                printf("pocketufo: audio-proof sfx-completed-after-ui-request expectedSample=%d identity-unavailable\n",
                       (int)nSample);
            } else {
                printf("pocketufo: audio-proof sfx-completion-timeout expectedSample=%d\n", (int)nSample);
            }
        } else if (strcmp(azArg[0], "normalair") == 0) {
            if (!App_ScriptNormalAirIntercept()) {
                break;
            }
        } else if (strcmp(azArg[0], "normalaircombat") == 0) {
            if (!App_ScriptNormalAirCombat()) {
                break;
            }
            printf("pocketufo: step normalaircombat\n");
        } else if (strcmp(azArg[0], "normalairground") == 0) {
            if (!App_ScriptNormalGroundEntry()) {
                break;
            }
            printf("pocketufo: step normalairground\n");
        } else if (strcmp(azArg[0], "normalorder") == 0) {
            if (!App_ScriptNormalInterceptOrder()) {
                break;
            }
        } else if (strcmp(azArg[0], "campaign") == 0 && nArgs > 1) {
            if (!App_ScriptCampaign(azArg[1], (nArgs > 2) ? azArg[2] : "requested")) {
                break;
            }
        } else if (strcmp(azArg[0], "quityes") == 0) {
            /* SHELL SCAFFOLDING (Part 103): run the same seam the quit dialog's YES button reaches --
             * slot 16 -> World_QuitYesThunk -> TactInputHooks.pfnShutdownQuit -> App_HookShutdownQuit
             * -> App_QuitFromTact @ 0x18e18. The scripted runs cannot stage a tactical mission, so
             * this command dispatches the seam itself. */
            App_HookShutdownQuit(NULL);
            printf("pocketufo: step quityes\n");
        } else {
            printf("pocketufo: step UNKNOWN '%s'\n", azArg[0]);
        }
        fflush(stdout);
    }
}

/* Debug paths that exit without the message pump use this teardown. The --native-loop script path
 * stays in App_PumpMessages and follows the normal window-close path. */
static void App_DebugTeardown(void)
{
    App_ShutdownWorld();
    Boot_Shutdown(&g_assets);
    g_bAssetsLoaded = false;
    Gx_Close(&g_gx);
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

/* InitInstance's two live startup picks, held here until the world exists: App_InitInstance runs
 * before World_Init, and World_Init memsets its whole block, so the values from 0xA01C0/0xA0228/
 * 0xA024C are kept here and installed in wWinMain right after World_Init. Nothing between the two
 * reads them, which is also true in the original: outside this window the only stores in the image are
 * the save read (Save_ReadTGameData 0x4430C / 0x44318) and the mission-start path --
 * Smap_BeginCraftSelect writes both six times, and Tact_CheckBattleEnd stores the mission type at
 * 0x365B8. */
static u8 g_nStartupMissionType;   /* Map_nMissionType @ 0x128AD2 */
static u8 g_bStartupMapUnk1511E;   /* unk_128AD6 @ Map+0x1511E */

static bool App_InitInstance(HINSTANCE hInstance, int nCmdShow, bool bHidden,
                             bool bScripted)
{
    WNDCLASSW wc;
    HWND      hWnd;

    g_bHiddenRun = bHidden;

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
        /* QoL --landscape: the window client becomes the 320x240 screen; the
         * logical surface stays portrait 240x320, shown upright inside it
         * (see pufo_gx.h). */
        RECT rc = { 0, 0,
                    (LONG)(g_bLandscapeFlag ? PUFO_SURF_H : PUFO_SURF_W),
                    (LONG)(g_bLandscapeFlag ? PUFO_SURF_W : PUFO_SURF_H) };
        DWORD dwStyle = WS_OVERLAPPEDWINDOW;
        DWORD dwExStyle = 0;

        /* DEVIATION (shell scaffolding): the two debug paths (--dump-frame, --script) never show the
         * window. Nothing they do needs a picture on the desktop -- the frame they check is the logical
         * surface App_DumpFramePpm writes -- and a window that pops up and grabs the foreground on every
         * test run gets in the way of the person using the machine. The interactive path keeps the
         * original's visible window. */
        if (bHidden) {
            dwExStyle |= WS_EX_NOACTIVATE;
        } else {
            dwStyle |= WS_VISIBLE;
        }
        (void)AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
        g_nScreenW = rc.right - rc.left; /* 0x9FE44, from GetSystemMetrics(0) there */
        g_nScreenH = rc.bottom - rc.top; /* 0x9FE58, from GetSystemMetrics(1) there */
        hWnd = CreateWindowExW(dwExStyle, PUFO_CLASS_NAME, PUFO_WINDOW_NAME,
                               dwStyle,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               g_nScreenW, g_nScreenH,
                               NULL, NULL, hInstance, NULL);
    }
    if (hWnd == NULL) {
        return false;
    }
    g_hWndMain = hWnd;

    /* Part 108: the sound output. The original links fmodce.dll; the port answers the
     * same PufoAudioBackend table with winmm (PufoAudioWinmm_HasDevice decides whether
     * the device opens, and a false init is the state the original reaches after 11
     * failed FSOUND_Init calls -- Sound_InitAndLoadSamples sets its ready flag either
     * way, 0x9E2D4). The mute pair is handed over as live cells: Sound_Play's soundoff
     * read is pCtxBase + 99 and Sound_LoadAndPlayMusic's load-time pause reads
     * Map_bMusicOff (0x9E228/0x9E230). Both installs precede the first
     * Sound_InitAndLoadSamples, which GameUI_InitStartup reaches at 0x38D48. */
    Sound_SetBackend(&g_pufoAudioWinmm);
    Sound_SetMuteBytes(&g_options.bSoundOff, &g_options.bMusicOff);
    Sound_SetLoopByte(&g_options.bMusicLoop); /* port-side options cell (2026-11-04) */

    if (!Gx_Open(&g_gx, hWnd, PUFO_SURF_W, PUFO_SURF_H, g_bLandscapeFlag ? 1 : 0)) {
        printf("pocketufo: display surface failed to open\n");
        fflush(stdout);
        DestroyWindow(hWnd);
        g_hWndMain = NULL;
        return false;
    }

    /* The seeding block, 0xA00D8..0xA024C. Game_SeedRng is srand(100 * wSecond + wMilliseconds) at
     * 0xA00F8 followed by the warm-up loop at 0xA0108..0xA011C, so a real new game draws a different
     * map every run (the region the pick lands on is a Game_RandU16 draw -- measured: six scripted runs
     * of the same flow gave two different 01_newgame.ppm pictures, and every one of the 240x320 pixels
     * differed). The two debug paths compare their pictures against committed references, so they draw
     * from a fixed clock instead. SHELL SCAFFOLDING: the interactive path keeps the original's clock.
     * Game_SeedStartupPicks is the nine draws after the seed; the record spends them before the asset
     * paths (0xA0250) are built, and so do we. */
    /* The fixed clock keys on SCRIPTED: a `--landscape --script` run renders
     * wide frames and must stay on the same clock the calibrated flows were
     * recorded with. */
    if (bScripted) {
        Game_SeedRngFixed();
    } else {
        Game_SeedRng();
    }
    Game_SeedStartupPicks(&g_nStartupMissionType, &g_bStartupMapUnk1511E);

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

    if (!bHidden) {
        ShowWindow(hWnd, nCmdShow);   /* 0x9FE60 */
    }
    UpdateWindow(hWnd);           /* 0x9FE68 */

    /* 0x9FE74..0x9FE94: the stored pair straight back through SetWindowPos -- HWND_TOP (hwndInsertAfter 0),
     * x/y 0, fuFlags 2 = SWP_NOMOVE, so it only re-applies the size. */
    (void)SetWindowPos(hWnd, HWND_TOP, 0, 0, g_nScreenW, g_nScreenH, SWP_NOMOVE);
    return true;
}

/* App_ResizeMainWindow @ 0x9ecb8, the other user of the same pair: HWND_BOTTOM (hwndInsertAfter 1), the origin, the
 * stored size, and fuFlags 0x80 = SWP_HIDEWINDOW -- the window restored to the size it was created with, pushed to the
 * bottom of the z-order and hidden. Its only caller in the image is MenuBtn_ResizeWindow @ 0x19930 (sample 16 first),
 * which Tact_InitUiControls installs on tactical widget 58 (0x3B6AC, and again in the handler table at 0x3BC74); the
 * port has that half as Menu_ResizeWindow(pHooks, pfnResizeWindow) in game/pufo_menu.c, so this is the callback that
 * wiring passes. Part 61 wired the last piece: widget 58's world thunk reaches it through
 * WorldHooks.pfnResizeMainWindow via App_ResizeMainWindowThunk. */
void App_ResizeMainWindow(void)
{
    (void)SetWindowPos(g_hWndMain, HWND_BOTTOM, 0, 0, g_nScreenW, g_nScreenH, SWP_HIDEWINDOW);
}

/* The window half of the two focus arms. All four sites pass x/y 0 and the stored pair and differ only in z-order and
 * the show/hide flag: WM_KILLFOCUS uses HWND_NOTOPMOST (-2) with fuFlags 0 at 0xA04AC and 0xA0514, WM_SETFOCUS uses
 * HWND_TOP with 0x40 (SWP_SHOWWINDOW) at 0xA05B8 and HWND_TOP with 0 at 0xA0610. */
static void App_FocusReposition(HWND hwndInsertAfter, u32 fuFlags)
{
    /* Win64 adaptation: flags 0 on focus loss reactivates this window inside
     * SetWindowPos. The nested gain starts timers that the outer loss then kills.
     * Keep the ARM geometry/z-order change without taking focus back. */
    if (hwndInsertAfter == HWND_NOTOPMOST) {
        fuFlags |= SWP_NOACTIVATE;
    }
    (void)SetWindowPos(g_hWndMain, hwndInsertAfter, 0, 0, g_nScreenW, g_nScreenH, fuFlags);
}

/* WM_KILLFOCUS, PocketUFO_WndProc 0xA046C..0xA0560. Two arms, on the mission flag and on the geoscape-UI flag, and
 * then the audio pause on every path (0xA052C: R1 = 1, R0 = FSOUND_ALL) plus the music pause while byte_187F28 is
 * set (0xA0538). Note the shape when neither flag is set: geometry, timers and display are left alone but the audio
 * is paused anyway -- that is the original's, not an oversight. */
static void App_FocusLost(void)
{
    const bool bTact     = (g_bWorldReady && g_world.bTactFlag != 0);   /* Map.bTactFlag @ 0x1139B8, tested 0xA0470 */
    const bool bGeoscape = (g_bWorldReady && g_world.bGeoscapeUi != 0); /* Map_bGeoscapeUi @ Map+0x44, tested 0xA04D8 */

    if (bTact) {
        App_FocusReposition(HWND_NOTOPMOST, 0); /* 0xA04AC */
        Timer_KillBySpeed();                    /* 0xA04B0 */
        Gx_Suspend(&g_gx);                      /* 0xA04B4..0xA04C0, the tail call into the gx import */
    } else if (bGeoscape) {
        App_FocusReposition(HWND_NOTOPMOST, 0); /* 0xA0514 */
        Timer_KillD();                          /* 0xA0518 */
        Gx_Suspend(&g_gx);                      /* 0xA051C..0xA0528 */
    }
    Sound_SetPaused(true);                      /* 0xA052C..0xA0560 */
}

/* WM_SETFOCUS, PocketUFO_WndProc 0xA0564..0xA0674, the mirror: take the display back, re-apply the geometry, restart
 * the timers, resume the audio (0xA0618: R1 = 0), then clear the four held-key repeat flags. R9 is zeroed at 0xA0568
 * before either arm runs and the four STRBs at 0xA064C..0xA0670 write it to UiBank_bHeldUp/Down/Left/Right, so a key
 * held across a focus change does not keep repeating on the way back in. */
static void App_FocusGained(void)
{
    const bool bTact     = (g_bWorldReady && g_world.bTactFlag != 0);   /* 0xA056C */
    const bool bGeoscape = (g_bWorldReady && g_world.bGeoscapeUi != 0); /* 0xA05C8 */

    if (bTact) {
        Gx_Resume(&g_gx);                              /* 0xA0578 */
        App_FocusReposition(HWND_TOP, SWP_SHOWWINDOW); /* 0xA05B8, fuFlags 0x40 */
        Timer_StartBySpeed(g_world.gameClock.nSpeed);  /* 0xA05BC; StartTimers_BySpeed reads 0x128AB4 itself */
    } else if (bGeoscape) {
        Gx_Resume(&g_gx);                              /* 0xA05D4 */
        App_FocusReposition(HWND_TOP, 0);              /* 0xA0610, fuFlags = R9 = 0 */
        Timer_StartD();                                /* 0xA0614 */
    }
    Sound_SetPaused(false);     /* 0xA0618..0xA063C */
    g_bank.abHeldDir[0] = 0;    /* UiBank_bHeldUp   @ 0xAD71C, written 0xA064C */
    g_bank.abHeldDir[1] = 0;    /* UiBank_bHeldDown @ 0xAD71D, written 0xA0658 */
    g_bank.abHeldDir[2] = 0;    /* UiBank_bHeldLeft @ 0xAD71E, written 0xA0664 */
    g_bank.abHeldDir[3] = 0;    /* UiBank_bHeldRight@ 0xAD71F, written 0xA0670 */
}

/* App_QuitDisplay @ 0xa0388 (100 bytes), the shell's teardown, in the original's order:
 * `if (Map_bTactFlag) Tact_Shutdown(0)` at 0xA0398/0xA039C, GameAssets_Shutdown at 0xA03A0,
 * GXCloseInput at 0xA03B0, GXCloseDisplay at 0xA03C0, PostQuitMessage(0) at 0xA03C8, and then the
 * store nobody expects on a quit path: `g_bListRowSticky[0] = 1` at 0xA03D4, on
 * unk_188D4C -- the same byte the screen-17 list handler reads at 0x14A94..0x14ADC to decide whether
 * the hit row is cleared. Here it is g_pointerWorld.bListRowSticky. Reproduced because the original
 * stores it unconditionally; nothing can observe it, because it is the last store of a process that
 * has already queued its quit message (see the Part 126 mutation results).
 * The two GX calls are one Gx_Close here: the port's close releases the input and the surface
 * together. The first half maps as `Tact_Shutdown(0)` -> World_Free, which begins with
 * World_FreeTactical and then frees the geoscape UI and roster; the gate differs on purpose -- the
 * original tests Map_bTactFlag (0xA0398, "a tactical mission is live") and the port tests
 * g_bWorldReady ("the world was built"), because the port's teardown owns both halves at once. */
static void App_QuitDisplay(void)
{
    if (g_nUiScreen == 18 || g_world.nUiScreen == 18u) {
        Timer_KillA();
    }
    if (g_pEndScreenBackdrop != NULL) {
        free(g_pEndScreenBackdrop);
        g_pEndScreenBackdrop = NULL;
    }
    App_ShutdownWorld();
    if (g_bAssetsLoaded) {
        Boot_Shutdown(&g_assets);
        g_bAssetsLoaded = false;
    }
    Gx_Close(&g_gx);
    PostQuitMessage(0);
    g_pointerWorld.bListRowSticky = true; /* 0xA03D4 */
}

/* SHELL SCAFFOLDING, not the original: the shell's own Map-block copy, so a test can see WHICH arm a
 * WM_TIMER firing moved (tests/test_appshell.c). The original has one Map block at 0x1139B8 that every
 * function reaches directly; the port's copy is this static, and nothing outside this file could name it
 * without an accessor. */
PufoWorldState *App_WorldState(void)
{
    return &g_world;
}

bool App_InitializeWorld(void)
{
    g_bWorldReady = World_Init(&g_world);
    return g_bWorldReady;
}

void App_ShutdownWorld(void)
{
    if (g_bWorldReady) {
        World_Free(&g_world);
        g_bWorldReady = false;
    }
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
    case WM_CREATE:
        /* case 1 @ 0xA06A4: the AYGSHELL activation block is created here -- memset of the
         * 12 bytes at 0x188380 and cbSize = 12. */
        Sh_ActivateInfoInit();
        return 0;
    case WM_ACTIVATE:
        /* case 6 @ 0xA0690: SHHandleWMActivate(hwnd, msg, wParam, lParam, psai). The
         * stand-in is documented in platform/pufo_aygshell.h. */
        Sh_HandleWmActivate(hWnd, (u32)wParam, (const void *)lParam);
        return 0;
    case WM_SETTINGCHANGE:
        /* case 0x1A @ 0xA06D0: SHHandleWMSettingChange(hwnd, wParam, psai). */
        Sh_HandleWmSettingChange(hWnd, (u32)wParam, (const void *)lParam);
        return 0;
    case WM_ERASEBKGND:
        return 1; /* the surface covers the client area */
    case WM_SIZE:
        if (g_gx.pBits != NULL) {
            (void)Gx_Present(&g_gx);
        }
        return 0;
    case WM_KILLFOCUS: /* case 8 @ 0xA046C */
        App_FocusLost();
        return 0;
    case WM_SETFOCUS:  /* case 7 @ 0xA0564 */
        App_FocusGained();
        return 0;
    case WM_TIMER:
        /* PocketUFO_WndProc @ 0xA0944's timer dispatch, reached through the `wParam != 50000` gate at
         * 0xA08B0 that keeps 0xC350 on the game-tick path. 0xC351 is the render tick (Render_Frame at
         * 0xA094C); 0xC352 advances the anim phase (0xA0978..0xA098C) and the per-screen scroll
         * (0xA0AC8); 0xC353 is UiPulse_OnTimer (0xA0C64); and the switch's default arm is 0xC354's
         * anim-progress byte (0xA0C88..0xA0C90). */
        if (wParam == PUFO_TIMER_GAMETICK) {
            /* The original's gate at 0xA08B0 keeps 0xC350 out of that switch: the speed decides the tick
             * count (Timer_TickBySpeed, 0xA08C0..0xA0934) and App_GameTickOnce is this shell's callee. */
            Timer_TickBySpeed((i32)g_world.gameClock.nSpeed, App_GameTickOnce, NULL);
        } else if (wParam == PUFO_TIMER_ANIM) {
            App_TickAnim();
        } else if (wParam == PUFO_TIMER_RENDER) {
            App_DrawFrame();
        } else if (wParam == PUFO_TIMER_UIPULSE) {
            App_TickPulse();
        } else if (wParam == PUFO_TIMER_EXTRA) {
            App_TickAnimProgress();
        }
        return 0;
    case WM_COMMAND:
        /* 0xA0C9C..0xA0CDC. The arm tests the LOW half of wParam: `MOV R3,R0,LSL#16` /
         * `MOV R3,R3,LSR#16` / `CMP R3,#1` at 0xA0C9C..0xA0CA4, R0 being the wParam the prologue moved in
         * (0xA03F4 `MOV R0,R2`). Command 1 re-enters this window procedure twice -- SendMessageW
         * (hWnd, WM_ACTIVATE, 0, hWnd) at 0xA0CB0, where the lParam slot really gets the hwnd (R4, saved
         * from R0 at 0xA03F0), and SendMessageW (hWnd, WM_CLOSE, 0, 0) at 0xA0CC4 -- and returns 0. WM_CLOSE
         * has no arm of its own here, so DefWindowProcW destroys the window and WM_DESTROY runs
         * App_QuitDisplay: command 1 is the window's own quit. Any other command value jumps to 0xA0740,
         * which is the `MOV R3,R2 / MOV R2,R0 / MOV R0,R4 / BL DefWindowProcW` tail with R1 left at 0x111 by
         * 0xA0CA8's `MOVNE`. */
        if ((u16)wParam == 1u) {
            (void)SendMessageW(hWnd, WM_ACTIVATE, 0, (LPARAM)(INT_PTR)hWnd);
            (void)SendMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    case WM_MOUSEMOVE:
        App_OnPointer(lParam, false); /* 0x13c6c, hover marks */
        return 0;
    case WM_LBUTTONDOWN:
        App_OnPointer(lParam, true);
        return 0;
    case WM_LBUTTONUP:
        App_OnPointerUp(); /* 0x148d0, fires the hovered row's handler */
        return 0;
    case WM_KEYDOWN: {
        /* PocketUFO_WndProc's WM_KEYDOWN arm (0xA0DC8..0xA0F74): one of the eight key-map slots, or
         * nothing. Both key messages return 0 in the original (0xA0E1C / 0xA0D00) -- DefWindowProc is
         * never reached, so a game key generates no character. Esc no longer quits the shell: the
         * original's escape key is the MenuEscape slot, whose press arm is nullsub_1 (0xA0F70) and
         * whose release arm is Input_OnMenuEscape (0xA0DC0). Quitting runs through the quit dialog
         * (screen 8's App_QuitFromTact) or WM_DESTROY, i.e. the window's own close. */
        i32 nSlot = App_KeySlotOf(wParam);

        if (nSlot >= 0) {
            App_OnKeyDown((PufoKeySlot)nSlot);
        }
        return 0;
    }
    case WM_KEYUP: {
        i32 nSlot = App_KeySlotOf(wParam);

        if (nSlot >= 0) {
            App_OnKeyUp((PufoKeySlot)nSlot); /* 0xA0CE0..0xA0DC4 */
        }
        return 0;
    }
    case WM_DESTROY:
        App_QuitDisplay();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

/* WinMain @ 0x9fcc4's pump, 0x9FD04..0x9FD44: GetMessageW(&Msg, NULL, 0, 0) tested for NON-ZERO
 * (0x9FD38 -- the record has no signed comparison, so a -1 error answer would keep the loop spinning,
 * and the port used to read this as `> 0`), then TranslateAcceleratorW(msg.hwnd, hAccel, &Msg) FIRST at
 * 0x9FD0C with the TranslateMessage / DispatchMessageW pair at 0x9FD14/0x9FD1C only when it answers 0,
 * and the exit code is the WM_QUIT message's own wParam at 0x9FD44.
 *
 * hAccel is passed through exactly as 0x9FCF0 leaves it: the record checks nothing, and a table that
 * failed to load is handed straight to TranslateAcceleratorW. Measured here (build/crops/accel_null.c):
 * TranslateAcceleratorW(hwnd, NULL, &msg) answers 0 and leaves the message untouched, so the port's old
 * `hAccel == NULL ||` short-circuit was not load-bearing -- it is gone. */
int App_PumpMessages(HACCEL hAccel)
{
    MSG msg;

    while (GetMessageW(&msg, NULL, 0, 0) != 0) {
        if (TranslateAcceleratorW(msg.hwnd, hAccel, &msg) == 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}

/* WinMain @ 0x9fcc4, the whole 132-byte body: the InitInstance gate at 0x9FCDC (FALSE -> return 0),
 * LoadAcceleratorsW from table 3 (`MOV R1,#3` 0x9FCE4, `BL LoadAcceleratorsW` 0x9FCEC), the pump, and its
 * return value. src/pufo/app/pufo_main.c carries the `wWinMain` the CRT looks for and calls this, so that
 * tests/test_appshell.c links the pump and the window procedure it tests instead of a copy of them. */
int App_Run(HINSTANCE hInstance, HINSTANCE hPrevInstance,
            LPWSTR lpCmdLine, int nCmdShow)
{
    HACCEL         hAccel;
    unsigned short colour = 0x0000u; /* black in RGB565 */

    LPCWSTR pszDumpPath;
    LPCWSTR pszScript;
    bool    bNativeLoop;

    (void)hPrevInstance;
    pszDumpPath = App_FindDumpFlag(lpCmdLine);
    pszScript = App_FindScriptFlag(lpCmdLine);
    bNativeLoop = App_HasNativeLoopFlag(lpCmdLine);
    if (bNativeLoop && (pszScript == NULL || pszDumpPath != NULL)) {
        printf("pocketufo: --native-loop requires --script and cannot use --dump-frame\n");
        fflush(stdout);
        return 1;
    }
    App_FindClickFlag(lpCmdLine);
    App_FindNewGameFlag(lpCmdLine);
    App_FindLoadSlotFlag(lpCmdLine);
    App_FindLandscapeFlag(lpCmdLine);

    /* QoL: with `--landscape` a scripted run keeps its window hidden (the
     * deterministic scripted pump) while the geometry switch renders WIDE
     * frames, so the wide layouts are checkable by script dump. Every scripted
     * run without the flag stays the hidden 240x320 path the e2e gate and
     * every frame dump rely on. */
    if (!App_InitInstance(hInstance, nCmdShow,
                          pszDumpPath != NULL || pszScript != NULL,
                          pszDumpPath != NULL || pszScript != NULL)) {
        return 0;
    }

    /* Match WinMain @ 0x9fcc4: accelerators are loaded unconditionally, from table 3 --
     * `MOV R1, #3` at 0x9FCE4, `BL LoadAcceleratorsW` at 0x9FCEC. A missing table is not fatal
     * there either: TranslateAcceleratorW then returns 0 and every message goes through the
     * normal translate/dispatch pair. */
    hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(3));

    /* GameUI_InitStartup @ 0x38a28 runs after the assets are in (the original reaches it from
     * the title/menu path once Boot_LoadStartup's archives are open). It loads the fonts, the
     * startg/menu palettes, the backdrop glyph and the "tact" samples, sets the ui screen to 17,
     * and arms the 0xC351/0xC352 timers through StartTimers_B. */
    if (g_bAssetsLoaded) {
        App_InitStartupUi();
        (void)App_InitializeWorld();
        /* The window was bound in App_InitStartupUi. Install the real timer
         * restart bridge now that World_Init has initialized the live clock. */
        Timer_InstallAsGameTimers(&g_world.gameClock);
        /* InitInstance's startup picks, into the Map block the world owns: the mission type from
         * 0xA0228 and the Map+0x1511E byte from 0xA01C8 / 0xA024C. A save read overwrites both
         * (Save_ReadTGameData 0x4430C / 0x44318), which is the original's order too. */
        g_world.nMapMissionType = g_nStartupMissionType;
        g_world.bMapUnk1511E    = g_bStartupMapUnk1511E;
        /* Map_nUiScreen is ONE global in the original and the world's copy is the one LoadGame @ 0x44CB0
         * tests (`== 17`) to decide that a load starts from the start menu -- that arm runs
         * StartUi_Shutdown, then Geoscape_InitUI(1), and zeroes the soldier/base/craft/strateg counters
         * (0x44CCC..0x44CEC). The shell reads its OWN copy on the draw path (a second home for the same
         * byte, recorded as a design problem in PLAN.md), so the two are seeded equal while the menu is
         * up, which is the state the original's single cell holds there (GameUI_InitStartup 0x38D40). */
        g_world.nUiScreen = (u16)g_nUiScreen;
        if (g_bNewGameFlag) {
            App_StartNewGameFromShortcut(); /* the startup click shuts down its audio before Geoscape_InitUI */
        }
        if (g_nLoadSlot >= 0) {
            (void)App_HookLoadGame(g_nLoadSlot, NULL); /* the load-slot rows run the same path */
        }
        App_DrawFrame();
        if (pszDumpPath != NULL) {
            /* Debug path, only with --new-game: the game's own way out of the new-base pick, so the frame dump lands
             * on the screen the clock runs on. Game_StartNew leaves the id 2 (0x95450) with the pick pending
             * (0x945E0); App_NewGamePickThroughPen commits it through the pointer -- try-confirm's tail call reaches
             * Geoscape_OnMapClick @ 0x8b4a8, whose pick arm writes 1 (0x8B600) and clears 0x1870AC (0x8B608) -- and
             * the placement screen's close control carries Geoscape_CloseUI_ThunkA @ 0x838d0, whose case 1 writes 0
             * (0x8BE94). It used to call World_MapClick straight, which is the 0x857e8 shim's shape and skipped
             * NewBaseScreen_TryConfirm's sample (0x61538) and its funds gate (0x6156C) entirely. */
            if (g_bNewGameFlag) {
                App_NewGamePickThroughPen();
                printf("pocketufo: after the pick: region %02X, screen %d, pick pending %d, place active %d, "
                       "bases %d, base 0 region %02X at %d/%d\n",
                       (int)g_world.bPendingRegionId, (int)g_world.screen.nGeoscapeScreen,
                       (int)g_world.bPlacementPending, (int)g_world.bPlaceActive, (int)g_world.nBaseCount,
                       (int)g_world.apBases[0]->bRegionId, g_world.apBases[0]->nUnk8, g_world.apBases[0]->nUnk12);
                World_CloseGeoscape(&g_world);
                printf("pocketufo: after the close: screen %d\n", (int)g_world.screen.nGeoscapeScreen);
                fflush(stdout);
            }
            if (g_bClickAt) {
                /* Debug path: one press+release at logical coordinates. Input_OnPointer marks the
                 * row under the pointer, Input_OnPointerUp fires that row's handler (a MenuCtl
                 * thunk), which is how the real window delivers a menu click. */
                App_PointerLogical(g_nClickX, g_nClickY, true);
                App_OnPointerUp();
            }
            /* Debug path: run the timer set for about a second -- 50 firings of all three bodies
             * (0xC352 anim, 0xC353 pulse at 20 ms, 0xC354 anim progress), each followed by a paint --
             * so the row keys reach their steady value and the geoscape clock shows its own advance.
             * The paint has to be inside the loop here as much as in App_ScriptTicks: the batched
             * shape this used to have (40 anim, then 50 pulse+progress, one paint at the end) is the
             * Part 74 defect -- any panel that opened after the last paint had its progress steps
             * swallowed by Ui_AnimExpandBlit's first-call reset (0x95C14) and the picture came out on
             * the frame where nothing has opened. */
            App_ScriptTicks(50);
            printf("pocketufo: clock after 50 pulses: geoscape screen %d, tick %d s, "
                   "total %d s, %02d:%02d day %d month %d year %d, anim progress %d, funds %d\n",
                   (int)g_world.screen.nGeoscapeScreen, (int)g_world.geo.world.nTickSeconds,
                   (int)g_world.geo.time.nTotalSec, (int)g_world.geo.time.nHour,
                   (int)g_world.geo.time.nMinute, (int)g_world.geo.time.nDay,
                   (int)g_world.geo.time.nMonth, (int)g_world.geo.time.nYear,
                   (int)g_world.cUiAnimProgress, (int)g_world.nFunds);
            fflush(stdout);
            App_DrawFrame();
            App_DumpFramePpm(pszDumpPath);
            App_DebugTeardown();
            return 0;
        }
        if (pszScript != NULL) {
            printf("pocketufo: script flag parsed: '%ls'\n", pszScript);
            fflush(stdout);
            /* Run each requested debug step before either teardown or the native message pump. */
            App_RunDebugScript(pszScript);
            if (!bNativeLoop) {
                App_DebugTeardown();
                return 0;
            }
            printf("pocketufo: native loop ready; entering the standard message pump\n");
            fflush(stdout);
        }
    } else {
        App_ClearSurface(colour);
        (void)Gx_Present(&g_gx);
    }

    return App_PumpMessages(hAccel);
}
