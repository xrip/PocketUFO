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
#include "game/pufo_tactend.h"  /* Tact_Shutdown, the shutdown half of App_QuitFromTact */
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

static void App_PresentLoadedSurface(void);
static i32  App_PresentHook(void *pUser); /* GX_Present @ 0x27788; WorldHooks.pfnPresentFrame's copy */
static void App_TitlesToStartup(void *pUser);
static i32 App_HookLoadGame(i32 nSlot, void *pUser);
static i32 App_HookSaveGame(i32 nSlot, void *pUser);
static void App_HookFormatSlots(void *pUser);

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
    PadImage_BlitEx(pPad, 255u);
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
    hooks.pnListMode          = &g_nListMode;                /* *0x157188 */
    hooks.pTactOptions        = &g_options;                  /* writable alias: the menu handlers write
                                                              * speed/gamma/volumes/toggles here */

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
static void App_HookTactOnConfirm(u16 nUiScreen, void *pUser);   /* the 0x15660 / 0x163C8 arms' callee */
static void App_HookTactSetCursorPos(i32 nX, i32 nY, void *pUser); /* MapCursor_SetPos @ 0x27014 */
static void App_HookTactPushTextRecord(i32 nRecIndex, void *pUser); /* StatusMsg_Push @ 0x2dda4 */
static void App_HookSaveOptions(void *pUser);            /* Options_SaveToRegistry @ 0x114d8 */
static void App_HookTactEscapeGeoscape(void *pUser);     /* Tact_Shutdown(0) + Geoscape_InitUI(1) */
static void App_HookGeoscapePanelA(void *pUser);         /* Geoscape_SetPanelFlagA @ 0x8b448 */
static void App_HookGeoscapePanelB(void *pUser);         /* Geoscape_SetPanelFlagB @ 0x8b478 */
static void App_HookGeoscapeFocusPrev(void *pUser);      /* Geoscape_UiFocusPrev @ 0x8afa0 */
static void App_HookGeoscapeFocusNext(void *pUser);      /* Geoscape_UiFocusNext @ 0x8b1f4 */
static void App_HookGeoscapeMapClick(void *pUser);       /* Geoscape_OnMapClick @ 0x8b4a8 */
static void App_ShutdownStartUi(void *pUser); /* defined below with the start-UI teardown */
static void App_QuitDisplay(void);      /* defined below with the window teardown */

/* App_QuitFromTact @ 0x18e18, wired to the screen-8 escape seam (TactInputHooks.pfnShutdownQuit).
 * Deviation: the shell never runs a tactical mission, so its shutdown half reaches Tact_Shutdown with
 * no mission env -- its NULL guard makes the call a no-op. The original's Tact_Shutdown(1) works on the
 * global mission context, which only exists during a mission. */
static void App_ShutdownTactNoMission(u8 bKeepSome, void *pUser)
{
    (void)pUser;
    Tact_Shutdown(NULL, bKeepSome);
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
    hooks.pnListMode          = &g_nListMode;
    hooks.pTactOptions        = &g_options;

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
    MapInitEnv initEnv;

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
    memset(&g_tactHooks, 0, sizeof(g_tactHooks));
    /* Screen 16 (the credits) answers a pointer release with Titles_Shutdown @ 0x3826c and then
     * GameUI_InitStartup @ 0x38a28 -- the credits go back to the start menu. Screen 18 (an ending)
     * has no shell env yet, so its arm stays a seam. */
    g_tactHooks.pfnTitlesToStartup = App_TitlesToStartup;
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
    g_tactHooks.pfnOnConfirm         = App_HookTactOnConfirm;
    g_tactHooks.pfnSetCursorPos      = App_HookTactSetCursorPos;
    g_tactHooks.pfnPushTextRecord    = App_HookTactPushTextRecord;
    g_tactHooks.pfnSaveOptions       = App_HookSaveOptions;         /* Input_OnConfirm's screen-12 arm */
    g_tactHooks.pfnShutdownReinit    = App_HookTactEscapeGeoscape;  /* UiScreen_OnCancel 13/19 */
    g_tactHooks.pfnGeoscapePanelA    = App_HookGeoscapePanelA;
    g_tactHooks.pfnGeoscapePanelB    = App_HookGeoscapePanelB;
    g_tactHooks.pfnGeoscapeFocusPrev = App_HookGeoscapeFocusPrev;
    g_tactHooks.pfnGeoscapeFocusNext = App_HookGeoscapeFocusNext;
    g_tactHooks.pfnGeoscapeMapClick  = App_HookGeoscapeMapClick;

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

/* The screen-16 arm of Input_OnPointerUp @ 0x14924: release the credits screen, then bring the
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
 * (0, 8, 9) draws through World_TactFrame (Part 63); the rest is the start menu (17) and the
 * credits (0x10), because the remaining tactical branches need subsystem envs the shell does not
 * own yet (the roster/status/labeled/modal-panel/top-bar/action pads, the minimap, the command
 * pads, the markers, the debrief banner, the confirm dialog, Cydonia). */
static void App_DrawFrame(void)
{
    MainMenuEnv    mm;
    RenderFrameEnv rf;

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

/* The WM_TIMER 0xC352 body (PocketUFO_WndProc @ 0xA0944): advance the anim phase counter
 * (0xA0978..0xA098C, >=7 wraps to 0), then the debrief fade arm (0xA0990..0xA09AC: banner counter up
 * AND screen 13 -> Render_Frame_DecRef), then per screen: 17 runs the row-key flicker (0xA0AC8),
 * 0x10 advances the credits scroll by one and wraps it past 20 * (nTitlesMaxRows + 48) (0xA0C58). */
static void App_TickAnim(void)
{
    if (g_nAnimPhase >= 7) {
        g_nAnimPhase = 0; /* 0xA0988 */
    } else {
        ++g_nAnimPhase;   /* 0xA098C */
    }
    if (g_bWorldReady) {
        World_TickBannerFade(&g_world); /* 0xA0990 gate + 0xA09AC BLEQ Render_Frame_DecRef */
    }
    if (g_nUiScreen == 17) {
        MainMenu_TickRowKeys(g_aRowKeys, g_aRowKeySrc, g_nAnimPhase, &g_nPlaneStatRowCount);
    } else if (g_nUiScreen == 0x10) {
        if (++g_titlesAnim.nScroll > 20 * (g_nTitlesMaxRows + 48)) {
            g_titlesAnim.nScroll = 0;
        }
    }
}

/* The WM_TIMER 0xC353 body (0xA0C64): UiPulse_OnTimer @ 0x36630 over the Map block. Its geoscape
 * branch (0x36644) returns after one hook call, and that hook is Geoscape_PulseUpdate @ 0x954dc --
 * the geoscape clock tick. The tact branch is the held-key repeat, and the shell has no tactical
 * mission state yet, so bTactFlag keeps it at the `return` at 0x36660. */
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
 * arm (TTL decay, GameTick_Step, the 0x275E8 frame-scratch clear). The TickActiveUnit walker
 * stays unhooked until the tactical runtime unit wires its TactTickCtx; on an empty queue the
 * original's walker early-returns, so no launch-frame behavior is skipped. */
static void App_GameTickOnce(void *pUser)
{
    (void)pUser;
    (void)World_GameTickOnce(&g_world);
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

    if (World_bTactPointerLive(&g_world)) {
        World_TactPointerUp(&g_world, &g_pointerWorld); /* Input_OnPointerUp @ 0x148d0 (Part 60) */
        return;
    }
    if (!g_bStartupUi && !bGeoscape) {
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
        App_PointerLogical(120, nMidY, true);                          /* the press arm moves nothing */
        App_PointerLogical(120 - nDx, nMidY - nDy, false);            /* this one scrolls */
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
    App_PointerLogical(pConfirm->x + 2, pConfirm->y + 2, true);
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

static WCHAR g_awcScriptPath[MAX_PATH];

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

/* `--script <path>`: the end-to-end driver (the shell's own scaffolding, not the original).
 *
 * One step per line; `#` starts a comment. Every step calls the function the real UI reaches at
 * that point, and `click` goes through the pen path -- Input_OnPointer for the press,
 * Input_OnPointerUp for the release -- exactly as WM_LBUTTONDOWN/WM_LBUTTONUP do:
 *
 *   dump <path>   render one frame, write it as a PPM, print the step
 *   click x y     press+release at logical 240x320 coordinates
 *   tick n        n anim ticks (0xC352) plus n pulses (0xC353) and n anim-progress ticks (0xC354)
 *   newgame       App_HookNewGame -- what the menu's new-game row runs
 *   pick          App_NewGamePickThroughPen -- the placement screen's confirm, through the pen
 *   close         World_CloseGeoscape -- the placement screen's close control
 *   save n        App_HookSaveGame -- World_SaveSlot, what a save-slot row runs
 *   load n        App_HookLoadGame -- LoadGame @ 0x44bbc, what a load-slot row runs
 *   state         print the shell's own state line
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
    printf("pocketufo: state %s: start screen %d, list mode %d, geoscape screen %d, geo ui byte %d, tact flag %d, "
           "bases %d, base 0 at %d/%d, soldiers %d, funds %d\n",
           pszLabel, g_nUiScreen, g_nListMode, (int)g_world.screen.nGeoscapeScreen, (int)g_world.bGeoscapeUi,
           (int)g_world.bTactFlag, (int)g_world.nBaseCount,
           (g_world.nBaseCount > 0) ? g_world.apBases[0]->nUnk8 : 0,
           (g_world.nBaseCount > 0) ? g_world.apBases[0]->nUnk12 : 0,
           (int)g_world.nSoldierCount, (int)g_world.nFunds);
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

/* n anim ticks, n pulses and n anim-progress ticks -- the same three timer bodies WndProc
 * dispatch, run the way the --dump-frame path already runs them. */
static void App_ScriptTicks(i32 nCount)
{
    for (i32 i = 0; i < nCount; ++i) {
        App_TickPulse();
        App_TickAnimProgress();
    }
    for (i32 i = 0; i < nCount; ++i) {
        App_TickAnim();
    }
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
            App_PointerLogical((i32)atoi(azArg[1]), (i32)atoi(azArg[2]), true);
            App_OnPointerUp();
            printf("pocketufo: step click %s %s\n", azArg[1], azArg[2]);
        } else if (strcmp(azArg[0], "tick") == 0 && nArgs > 1) {
            App_ScriptTicks((i32)atoi(azArg[1]));
        } else if (strcmp(azArg[0], "newgame") == 0) {
            App_HookNewGame(NULL);
            printf("pocketufo: step newgame\n");
        } else if (strcmp(azArg[0], "pick") == 0) {
            App_NewGamePickThroughPen();
            printf("pocketufo: step pick\n");
        } else if (strcmp(azArg[0], "close") == 0) {
            World_CloseGeoscape(&g_world);
            printf("pocketufo: step close\n");
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
        } else {
            printf("pocketufo: step UNKNOWN '%s'\n", azArg[0]);
        }
        fflush(stdout);
    }
}

/* The two debug paths (--dump-frame, --script) leave through the same teardown the window
 * path runs at App_QuitDisplay, minus the message loop. */
static void App_DebugTeardown(void)
{
    if (g_bWorldReady) {
        World_Free(&g_world);
        g_bWorldReady = false;
    }
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

static bool App_InitInstance(HINSTANCE hInstance, int nCmdShow, bool bHidden)
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

    if (!Gx_Open(&g_gx, hWnd, PUFO_SURF_W, PUFO_SURF_H)) {
        printf("pocketufo: display surface failed to open\n");
        fflush(stdout);
        DestroyWindow(hWnd);
        g_hWndMain = NULL;
        return false;
    }

    /* Game_SeedRng is the port's srand @ 0xA00F8, seeded from 100 * wSecond + wMilliseconds, so a real
     * new game draws a different map every run (the region the pick lands on is a Game_RandU16 draw --
     * measured: six scripted runs of the same flow gave two different 01_newgame.ppm pictures, and every
     * one of the 240x320 pixels differed). The two debug paths compare their pictures against committed
     * references, so they draw from a fixed seed instead. SHELL SCAFFOLDING: the interactive path keeps
     * the original's clock seed. */
    if (bHidden) {
        Game_SeedRngFixed();
    } else {
        Game_SeedRng();
    }

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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    MSG            msg;
    HACCEL         hAccel;
    unsigned short colour = 0x0000u; /* black in RGB565 */

    LPCWSTR pszDumpPath;
    LPCWSTR pszScript;

    (void)hPrevInstance;
    pszDumpPath = App_FindDumpFlag(lpCmdLine);
    pszScript = App_FindScriptFlag(lpCmdLine);
    App_FindClickFlag(lpCmdLine);
    App_FindNewGameFlag(lpCmdLine);
    App_FindLoadSlotFlag(lpCmdLine);

    if (!App_InitInstance(hInstance, nCmdShow, pszDumpPath != NULL || pszScript != NULL)) {
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
        /* Map_nUiScreen is ONE global in the original and the world's copy is the one LoadGame @ 0x44CB0
         * tests (`== 17`) to decide that a load starts from the start menu -- that arm runs
         * StartUi_Shutdown, then Geoscape_InitUI(1), and zeroes the soldier/base/craft/strateg counters
         * (0x44CCC..0x44CEC). The shell reads its OWN copy on the draw path (a second home for the same
         * byte, recorded as a design problem in PLAN.md), so the two are seeded equal while the menu is
         * up, which is the state the original's single cell holds there (GameUI_InitStartup 0x38D40). */
        g_world.nUiScreen = (u16)g_nUiScreen;
        if (g_bNewGameFlag) {
            App_HookNewGame(NULL); /* the menu's new-game row runs the same path on a click */
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
            /* Debug path: run the timer set for about a second -- 40 anim ticks (0xC352 at 25 ms) and
             * 50 pulses (0xC353 at 20 ms, StartTimers_D's interval) plus the same number of 0xC354
             * anim-progress ticks -- so the row keys reach their steady value and the geoscape clock
             * shows its own advance, then render one frame, write it out, and leave. */
            for (int i = 0; i < 40; ++i) {
                App_TickAnim();
            }
            for (int i = 0; i < 50; ++i) {
                App_TickPulse();
                App_TickAnimProgress();
            }
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
            /* Debug path: the step list drives menu -> new game -> placement -> geoscape ->
             * save -> load and writes one picture per step. */
            App_RunDebugScript(pszScript);
            App_DebugTeardown();
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
