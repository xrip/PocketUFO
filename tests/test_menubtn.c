/* test_menubtn.c - the widget click adapters.
 * (MenuBtn_Confirm0..5 @ 0x19650..0x1a014, ConfirmSnd15 @ 0x198f0,
 *  ConfirmClear @ 0x194c8, Secondary @ 0x19498, SecondaryDismiss @ 0x195e0,
 *  ActionBack0/1 @ 0x195a8/0x19618, Cancel0/1 @ 0x1a24c/0x1a26c,
 *  Menu_OpenLoadSlots @ 0x18864, Menu_OpenSaveSlots @ 0x188a4,
 *  Menu_ConfirmQuitDialog @ 0x18e2c, Menu_CancelQuitDialog @ 0x18e04,
 *  App_QuitFromTact @ 0x18e18.) */
#include <stdlib.h>
#include "game/pufo_input.h"
#include "game/pufo_menu.h"
#include "pufo_test.h"
#include "ui/pufo_menubtn.h"

#include <stdio.h>
#include <string.h>

#define SAMPLE_LOG_MAX 8

static i32  g_anSamples[SAMPLE_LOG_MAX];
static i32  g_nSamples;
static i32  g_nSaveOptions;
static i32  g_nModalEnd;
static i32  g_nShutdownQuit;
static bool g_bActionFlag;
static bool g_bEscapeFlag;
static char g_szOrder[32];
static i32  g_nOrder;

static void Order_Add(char c)
{
    if (g_nOrder < (i32)sizeof(g_szOrder) - 1) {
        g_szOrder[g_nOrder++] = c;
        g_szOrder[g_nOrder] = '\0';
    }
}

static void Log_Sample(i32 nSample)
{
    if (g_nSamples < SAMPLE_LOG_MAX) {
        g_anSamples[g_nSamples] = nSample;
    }
    ++g_nSamples;
    Order_Add('s');
}

static void CbSample(u32 nSample, void *pUser)
{
    (void)pUser;
    Log_Sample((i32)nSample);
}

static void CbSampleMenu(i32 nSample, void *pUser)
{
    (void)pUser;
    Log_Sample(nSample);
}

static void CbSaveOptions(void *pUser)
{
    (void)pUser;
    ++g_nSaveOptions;
    Order_Add('O');
}

static void CbModalEnd(void *pUser)
{
    (void)pUser;
    ++g_nModalEnd;
    Order_Add('M');
}

static void CbShutdownQuit(void *pUser)
{
    (void)pUser;
    ++g_nShutdownQuit;
    Order_Add('Q');
}

static void CbFormat(void *pUser)
{
    (void)pUser;
    Order_Add('F');
}

static TactInputState  g_State;
static TactInputHooks  g_Hooks;
static MenuHooks       g_MenuHooks;
static UiButtonCtx     g_Ctx;
static UiModalState    g_Modal;

/* Distinct bodies: two empty stubs fold to one address under /OPT:ICF, and the YES/NO identity
 * claims would then pass whatever the modal stored. Each records its own id. */
static i32 g_nMenuSeamFired;
static void YesCb(void) { g_nMenuSeamFired = 1; }
static void NoCb(void)  { g_nMenuSeamFired = 2; }

static void Reset(void)
{
    memset(&g_State, 0, sizeof(g_State));
    memset(&g_Hooks, 0, sizeof(g_Hooks));
    memset(&g_MenuHooks, 0, sizeof(g_MenuHooks));
    memset(&g_Modal, 0xAA, sizeof(g_Modal));
    memset(g_anSamples, 0, sizeof(g_anSamples));
    g_nSamples = 0;
    g_nOrder = 0;
    g_szOrder[0] = '\0';
    g_nSaveOptions = 0;
    g_nModalEnd = 0;
    g_nShutdownQuit = 0;
    g_bActionFlag = false;
    g_bEscapeFlag = false;

    g_Hooks.pfnPlaySample   = CbSample;
    g_Hooks.pfnSaveOptions  = CbSaveOptions;
    g_Hooks.pfnModalEnd     = CbModalEnd;
    g_Hooks.pfnShutdownQuit = CbShutdownQuit;
    g_MenuHooks.pfnPlaySample = CbSampleMenu;

    g_State.bTactFlag = true;

    g_Ctx.pState = &g_State;
    g_Ctx.pHooks = &g_Hooks;
    g_Ctx.pWorld = NULL;
    g_Ctx.pbActionFlag = &g_bActionFlag;
    g_Ctx.pbEscapeFlag = &g_bEscapeFlag;
}

static void CheckSample(i32 nIdx, i32 nWant, const char *pszWhat)
{
    TestCheck(pszWhat, g_nSamples > nIdx);
    TestEqI32(pszWhat, g_nSamples > nIdx ? g_anSamples[nIdx] : -1, nWant);
}

static void CheckOrder(const char *pszWant, const char *pszWhat)
{
    TestEqI32(pszWhat, strcmp(g_szOrder, pszWant), 0);
}

/* MenuBtn_Confirm0..5 and ConfirmSnd15: sample, then Input_OnConfirm, and the
 * screen is NOT cleared first. Screen 12 is the options screen, whose confirm
 * saves the options and closes the screen. */
static void TestConfirm(void)
{
    printf("MenuBtn_Confirm (0x19650, 0x196e8, 0x19884, 0x198a4, 0x19910, 0x1a014, 0x198f0)\n");

    Reset();
    g_State.nUiScreen = 12u;
    MenuBtn_Confirm(&g_Ctx, PUFO_BTN_SAMPLE_CONFIRM);
    CheckSample(0, 16, "  the adapter plays sample 16");
    TestEqI32("  Input_OnConfirm ran the screen-12 path", g_nSaveOptions, 1);
    TestEqI32("  the screen is cleared by the handler", (i32)g_State.nUiScreen, 0);

    Reset();
    g_State.nUiScreen = 12u;
    MenuBtn_Confirm(&g_Ctx, PUFO_BTN_SAMPLE_CONFIRM_ALT);
    CheckSample(0, 15, "  ConfirmSnd15 plays sample 15 instead");
    TestEqI32("  and behaves identically", g_nSaveOptions, 1);

    /* The adapter's sample and the handler's own sample are two calls. */
    Reset();
    g_State.nUiScreen = 6u;
    MenuBtn_Confirm(&g_Ctx, PUFO_BTN_SAMPLE_CONFIRM);
    TestEqI32("  two samples are played", g_nSamples, 2);
    CheckSample(1, 16, "  the handler adds its own 16");

    /* Gate: with no tactical flag the handler does nothing at all. */
    Reset();
    g_State.bTactFlag = false;
    g_State.nUiScreen = 12u;
    MenuBtn_Confirm(&g_Ctx, PUFO_BTN_SAMPLE_CONFIRM);
    TestEqI32("  the gate blocks the handler", g_nSaveOptions, 0);
    TestEqI32("  and the screen survives", (i32)g_State.nUiScreen, 12);
}

/* MenuBtn_ConfirmClear @ 0x194c8 clears nUiScreen BEFORE Input_OnConfirm, so the
 * screen the widget was on never reaches the handler. */
static void TestConfirmClear(void)
{
    printf("MenuBtn_ConfirmClear (0x194c8)\n");

    Reset();
    g_State.nUiScreen = 12u;
    MenuBtn_ConfirmClear(&g_Ctx);
    TestEqI32("  the options path is dead: no save", g_nSaveOptions, 0);
    TestEqI32("  the screen is already 0", (i32)g_State.nUiScreen, 0);
    /* With the screen cleared the handler runs its screen-0 case instead, which
     * with a clear action latch is the deploy-panel toggle and sample 7. */
    TestEqI32("  two samples play", g_nSamples, 2);
    CheckSample(0, 16, "  the adapter's 16");
    CheckSample(1, 7, "  then the handler's screen-0 7");
    TestEqI32("  and the deploy panel toggles", g_State.bDeployPanelOpen, 1);
    CheckOrder("ss", "  adapter sample then handler sample");

    /* The screen the widget was on never reaches the handler, so 6 and 12 end the
     * same way. */
    Reset();
    g_State.nUiScreen = 6u;
    MenuBtn_ConfirmClear(&g_Ctx);
    TestEqI32("  screen 6 ends identically", g_nSamples, 2);
    CheckSample(1, 7, "  still the screen-0 path");
}

/* MenuBtn_Secondary @ 0x19498: sample 16, screen = 0, Input_OnSecondary. */
static void TestSecondary(void)
{
    printf("MenuBtn_Secondary (0x19498)\n");

    Reset();
    g_State.bTactFlag = false;
    g_State.nUiScreen = 12u;
    g_bEscapeFlag = true;
    MenuBtn_Secondary(&g_Ctx);
    CheckSample(0, 16, "  sample 16");
    TestEqI32("  the screen is cleared before the handler", (i32)g_State.nUiScreen, 0);
    TestCheck("  the escape flag is untouched by the gate", g_bEscapeFlag);
}

/* MenuBtn_SecondaryDismiss @ 0x195e0: the same, then UiScreen_DismissHigh. The
 * clear makes the modal and cancel branches unreachable. */
static void TestSecondaryDismiss(void)
{
    printf("MenuBtn_SecondaryDismiss (0x195e0)\n");

    Reset();
    g_State.nUiScreen = 8u;
    g_bEscapeFlag = true;
    {
        i32 nResult = MenuBtn_SecondaryDismiss(&g_Ctx);
        TestEqI32("  the modal branch is dead: returns NONE", nResult, PUFO_UI_DISMISS_NONE);
    }
    TestEqI32("  no modal end ran", g_nModalEnd, 0);
    TestCheck("  DismissHigh still clears the escape flag", !g_bEscapeFlag);
    TestEqI32("  the screen stays cleared", (i32)g_State.nUiScreen, 0);
}

/* MenuBtn_ActionBack0/1 @ 0x195a8 / 0x19618: identical handlers. Input_OnAction
 * sets byte_AD711 and UiScreen_OnBack clears the same byte. */
static void TestActionBack(void)
{
    printf("MenuBtn_ActionBack (0x195a8, 0x19618)\n");

    Reset();
    g_bActionFlag = false;
    MenuBtn_ActionBack(&g_Ctx);
    CheckSample(0, 16, "  sample 16");
    TestEqI32("  the screen is cleared", (i32)g_State.nUiScreen, 0);
    TestCheck("  AD711 ends cleared by the pair", !g_bActionFlag);

    Reset();
    g_State.nUiScreen = 8u;
    MenuBtn_ActionBack(&g_Ctx);
    TestEqI32("  the screen-8 quit branch is dead", g_nShutdownQuit, 0);
    TestEqI32("  no modal end either", g_nModalEnd, 0);
}

/* MenuBtn_Cancel0/1 @ 0x1a24c / 0x1a26c: sample 16, then UiScreen_OnCancel. */
static void TestCancel(void)
{
    printf("MenuBtn_Cancel (0x1a24c, 0x1a26c)\n");

    Reset();
    g_State.nUiScreen = 8u;
    {
        i32 nResult = MenuBtn_Cancel(&g_Ctx);
        TestEqI32("  the modal screen closes to 0", nResult, 0);
    }
    CheckSample(0, 16, "  sample 16");
    TestEqI32("  UiModal_End ran", g_nModalEnd, 1);
    TestEqI32("  the screen is 0", (i32)g_State.nUiScreen, 0);

    Reset();
    g_State.nUiScreen = 3u;
    {
        i32 nResult = MenuBtn_Cancel(&g_Ctx);
        TestEqI32("  a low screen returns its id unchanged", nResult, 3);
    }
    TestEqI32("  and nothing else ran", g_nModalEnd, 0);
}

/* Menu_OpenLoadSlots @ 0x18864 / Menu_OpenSaveSlots @ 0x188a4. */
static void TestSlotList(void)
{
    u16 nScreen = 0u;
    i32 nListMode = 2;

    printf("Menu_OpenLoadSlots / Menu_OpenSaveSlots (0x18864, 0x188a4)\n");

    Reset();
    Menu_OpenSlotList(&nScreen, &nListMode, true, CbFormat, &g_MenuHooks);
    TestEqI32("  the save list opens screen 10", (i32)nScreen, 10);
    TestEqI32("  the list mode is cleared", nListMode, 0);
    CheckSample(0, 16, "  sample 16");
    CheckOrder("Fs", "  the list is formatted before the sample");

    Reset();
    nScreen = 0u;
    nListMode = 1;
    Menu_OpenSlotList(&nScreen, &nListMode, false, CbFormat, &g_MenuHooks);
    TestEqI32("  the load list opens screen 11", (i32)nScreen, 11);
    TestEqI32("  its list mode is cleared too", nListMode, 0);

    Reset();
    nScreen = 7u;
    Menu_OpenSlotList(&nScreen, NULL, true, NULL, &g_MenuHooks);
    TestEqI32("  a missing format hook is skipped", (i32)nScreen, 10);
    CheckOrder("s", "  and the sample still plays");
}

/* Menu_ConfirmQuitDialog @ 0x18e2c and Menu_CancelQuitDialog @ 0x18e04. */
static void TestQuitDialog(void)
{
    u16 nScreen = 0u;

    printf("Menu_ConfirmQuitDialog / Menu_CancelQuitDialog (0x18e2c, 0x18e04)\n");

    Reset();
    {
        i32 nResult = Menu_OpenQuitDialog(&g_Modal, &nScreen, YesCb, NoCb, &g_MenuHooks);
        TestEqI32("  returns the new screen id", nResult, 8);
    }
    CheckSample(0, 16, "  sample 16");
    TestEqI32("  nUiScreen becomes 8", (i32)nScreen, 8);
    TestCheck("  the modal is open", g_Modal.bOpen);
    TestEqI32("  slot 12 = 140", g_Modal.anRect12_14[0], 140);
    TestEqI32("  slot 13 = 24", g_Modal.anRect12_14[1], 24);
    TestEqI32("  slot 14 = 4", g_Modal.anRect12_14[2], 4);
    TestCheck("  slot 15 is the YES label",
              g_Modal.pLabel15 != NULL && strcmp((const char *)g_Modal.pLabel15, "YES") == 0);
    TestCheck("  slot 17 is the NO label",
              g_Modal.p17 != NULL && strcmp((const char *)g_Modal.p17, "NO") == 0);
    TestCheck("  slot 16 is the yes callback", g_Modal.pfn16 == YesCb);
    TestCheck("  slot 18 is the no callback", g_Modal.pfn18 == NoCb);

    Menu_CancelQuitDialog(&nScreen);
    TestEqI32("  the NO callback only clears the screen", (i32)nScreen, 0);
}

/* App_QuitFromTact @ 0x18e18: Tact_Shutdown(1) at 0x18E20, then App_QuitDisplay at 0x18E28. */
static i32 g_nQtKeep;

static void QtShutdown(u8 bKeepSome, void *pUser)
{
    (void)pUser;
    g_nQtKeep = bKeepSome;
    Order_Add('S');
}

static void QtDisplay(void *pUser)
{
    (void)pUser;
    Order_Add('D');
}

static void TestQuitFromTact(void)
{
    printf("App_QuitFromTact (0x18e18)\n");

    Reset();
    App_QuitFromTact(QtShutdown, QtDisplay, NULL);
    CheckOrder("SD", "  shutdown first, then the display teardown");
    TestEqI32("  the shutdown half gets bKeepSome = 1 (0x18E20)", g_nQtKeep, 1);

    Reset();
    App_QuitFromTact(NULL, QtDisplay, NULL);
    CheckOrder("D", "  a missing shutdown half is skipped");

    Reset();
    App_QuitFromTact(QtShutdown, NULL, NULL);
    CheckOrder("S", "  a missing display half is skipped");
}

static i32 g_nSmSample;
static i32 g_nSmLoadSlot;
static i32 g_nSmLoadResult;
static char g_szSmLog[24];

static void SmLog(char c)
{
    size_t n = strlen(g_szSmLog);
    if (n + 1 < sizeof(g_szSmLog)) {
        g_szSmLog[n] = c;
        g_szSmLog[n + 1] = 0;
    }
}

static void SmSample(u32 nSample, void *pUser)
{
    (void)pUser;
    g_nSmSample = (i32)nSample;
    SmLog('s');
}

static void SmShutdown(void *pUser) { (void)pUser; SmLog('x'); }
static void SmNewGame(void *pUser) { (void)pUser; SmLog('g'); }
static void SmFormatSlots(void *pUser) { (void)pUser; SmLog('f'); }
static void SmTitles(void *pUser) { (void)pUser; SmLog('t'); }
static void SmQuit(void *pUser) { (void)pUser; SmLog('q'); }

static i32 SmLoadGame(i32 nSlot, void *pUser)
{
    (void)pUser;
    g_nSmLoadSlot = nSlot;
    SmLog('l');
    return g_nSmLoadResult;
}

static i32 g_nHookSlot;

static i32 SaveHook(i32 nSlot, void *pUser)
{
    (void)pUser;
    g_nHookSlot = nSlot;
    return 5;
}

static i32 LoadHookOk(i32 nSlot, void *pUser)
{
    (void)pUser;
    g_nHookSlot = nSlot;
    return 7;
}

/* 0x100 has a zero LOW byte, so the original treats it as a failure. */
static i32 LoadHookFail(i32 nSlot, void *pUser)
{
    (void)nSlot;
    (void)pUser;
    return 0x100;
}

static i32 LoadHookLow(i32 nSlot, void *pUser)
{
    (void)nSlot;
    (void)pUser;
    return 0x101;
}

static void TestSaveLoadSlot(void)
{
    SaveLoadSlotHooks hooks;
    i32 nMode;

    printf("SaveLoad_Slot0..6\n");

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnSaveGame = SaveHook;
    hooks.pfnLoadGame = LoadHookOk;

    /* The save screen: nUiScreen == 10 runs SaveGame and the mode stays 1. */
    g_nHookSlot = -1;
    nMode = 99;
    TestEqI32("the save screen returns SaveGame's value unchanged",
              SaveLoad_Slot(6, PUFO_MENU_SCREEN_SAVE_LIST, &nMode, &hooks), 5);
    TestEqI32("and the button's own slot number", g_nHookSlot, 6);
    TestEqI32("the mode becomes 1 and stays 1", nMode, PUFO_MENU_MODE_SAVE_LOAD_LIST);

    /* Any other screen runs LoadGame. */
    g_nHookSlot = -1;
    nMode = 99;
    TestEqI32("another screen runs LoadGame", SaveLoad_Slot(2, 3, &nMode, &hooks), 7);
    TestEqI32("with the same slot number", g_nHookSlot, 2);
    TestEqI32("and a good load keeps the mode at 1", nMode, PUFO_MENU_MODE_SAVE_LOAD_LIST);

    /* A failed load restores 0, not 2, and returns 0. */
    hooks.pfnLoadGame = LoadHookFail;
    nMode = 99;
    TestEqI32("a failed load returns 0", SaveLoad_Slot(0, 3, &nMode, &hooks), 0);
    TestEqI32("and the mode goes back to 0, not 2", nMode, 0);

    /* The test is on the low byte: 0x101 has a non-zero low byte. */
    hooks.pfnLoadGame = LoadHookLow;
    nMode = 99;
    TestEqI32("0x101 passes because only the low byte is tested",
              SaveLoad_Slot(1, 3, &nMode, &hooks), 0x101);
    TestEqI32("so the mode is left alone", nMode, PUFO_MENU_MODE_SAVE_LOAD_LIST);

    hooks.pfnSaveGame = NULL;
    TestEqI32("no SaveGame hook gives 0",
              SaveLoad_Slot(0, PUFO_MENU_SCREEN_SAVE_LIST, &nMode, &hooks), 0);
    TestCheck("NULL hooks refused", SaveLoad_Slot(0, 3, &nMode, NULL) == 0);
}

static void TestStartMenu(void)
{
    StartMenuHooks hk;
    i32 nMode;

    printf("start menu buttons\n");

    hk.pfnPlaySample = SmSample;
    hk.pfnShutdownStartUi = SmShutdown;
    hk.pfnNewGame = SmNewGame;
    hk.pfnFormatSlots = SmFormatSlots;
    hk.pfnTitles = SmTitles;
    hk.pfnQuit = SmQuit;
    hk.pUser = NULL;

    g_nSmSample = 0;
    g_szSmLog[0] = 0;
    MenuCtl0_OnClick(&hk);
    TestEqI32("new game plays sample 16", g_nSmSample, 16);
    TestEqI32("new game shuts the start UI down first", strcmp(g_szSmLog, "sxg"), 0);

    g_szSmLog[0] = 0;
    nMode = 0;
    MenuCtl1_OnClick(&nMode, &hk);
    TestEqI32("load formats the list before it sets the mode", strcmp(g_szSmLog, "sf"), 0);
    TestEqI32("and the mode becomes 2", nMode, PUFO_MENU_MODE_LOAD_LIST);

    g_szSmLog[0] = 0;
    MenuCtl2_OnClick(&hk);
    TestEqI32("credits run after the shutdown", strcmp(g_szSmLog, "sxt"), 0);

    g_szSmLog[0] = 0;
    MenuCtl3_OnClick(&hk);
    TestEqI32("quit runs after the shutdown", strcmp(g_szSmLog, "sxq"), 0);

    /* MenuCtl8..14: the seven load slots. */
    for (i32 nSlot = 0; nSlot < 7; ++nSlot) {
        g_szSmLog[0] = 0;
        nMode = 0;
        g_nSmLoadResult = 1;
        TestEqI32("a successful load returns the callee value",
                  Menu_LoadSlot(&nMode, nSlot, SmLoadGame, NULL), 1);
        TestEqI32("the mode is 3 while the load runs", nMode, PUFO_MENU_MODE_LOADING);
        TestEqI32("the slot number travels", g_nSmLoadSlot, nSlot);
    }

    /* A failed load: mode back to 2, and the returned value is 2, not the callee's. */
    nMode = 0;
    g_nSmLoadResult = 0;
    TestEqI32("a failed load returns 2", Menu_LoadSlot(&nMode, 3, SmLoadGame, NULL), 2);
    TestEqI32("and the mode goes back to 2", nMode, PUFO_MENU_MODE_LOAD_LIST);

    /* 0x1A0DC tests the LOW BYTE, so a result whose low byte is zero still fails. */
    nMode = 0;
    g_nSmLoadResult = 0x100;
    TestEqI32("a result of 0x100 fails on its low byte",
              Menu_LoadSlot(&nMode, 0, SmLoadGame, NULL), 2);

    nMode = 0;
    g_nSmLoadResult = 0x101;
    TestEqI32("a result of 0x101 passes and is returned whole",
              Menu_LoadSlot(&nMode, 0, SmLoadGame, NULL), 0x101);

    nMode = 0;
    TestEqI32("a missing load hook counts as a failure",
              Menu_LoadSlot(&nMode, 0, NULL, NULL), 2);

    MenuCtl0_OnClick(NULL);
    TestEqI32("NULL hooks are tolerated", 1, 1);
}

/* ------------------------------------------------------------------ start UI teardown */

static i32 g_nSoundCalls, g_nKillCalls, g_nFontCalls, g_nFreeCalls;
static void *g_pFreedFont;

static void SdSound(void *pUser)   { (void)pUser; ++g_nSoundCalls; }
static void SdKill(void *pUser)    { (void)pUser; ++g_nKillCalls; }
static void SdFontFree(void *pFont, void *pUser) { (void)pUser; ++g_nFontCalls; g_pFreedFont = pFont; }
static void *SdFree(void *pBlock, void *pUser)
{
    (void)pUser;
    ++g_nFreeCalls;
    free(pBlock);
    return NULL;
}

/* StartUi_Shutdown @ 0x38d7c: sound shutdown at 0x38D80, kill timers at 0x38D84, then 15 groups
 * (R8 = 15 at 0x38D8C) of 3 pads each (R5 = 3 at 0x38D94) walked with ADD R4,#4 inside and
 * ADD R7,#0x20 outside -- so the three dwords are one widget's pPad0..pPad2, and the constant
 * 0xE34 loaded once outside both loops is 32*113 + 0x14, i.e. widget 113's pPad0: the range is bank
 * widgets 113..127. Each pad goes PadImage_FreeData (0x38DB8) then delete (0x38DC0) and is stored
 * NULL. Then the font at 0x38DE8, the standalone control at 0x38DF8..0x38E08, the two blocks at
 * 0x38E14/0x38E20, and Map.nUiScreen = 22 at 0x38E2C. The sentinels just outside the range are what
 * make the 113..127 claim a check rather than a reading. */
static void TestStartUiShutdown(void)
{
    static UiControl g_aBank[130];
    static PadImage g_Font;
    static i32 nUiScreen;
    PadImage *pSentinelA;
    PadImage *pSentinelB;
    bool bKeepA;
    bool bKeepB;
    StartUiShutdownEnv env;
    UiControl *pStartCtl;

    printf("StartUi_Shutdown\n");

    memset(g_aBank, 0, sizeof(g_aBank));
    memset(&env, 0, sizeof(env));
    env.pBank = g_aBank;
    env.pFont = &g_Font;
    env.pnUiScreen = &nUiScreen;
    env.hooks.pfnSoundShutdown = SdSound;
    env.hooks.pfnKillTimers = SdKill;
    env.hooks.pfnFontFree = SdFontFree;
    env.hooks.pfnFree = SdFree;

    /* Sentinels just outside the range the loop covers, and heap-owned on purpose. The earlier
     * version used two statics, which made an out-of-range tear-down undefined behaviour: shifting
     * the loop to 112..126 aborted the process instead of failing a check, so the range start had no
     * teeth at all. A heap sentinel is freed legally by the wrong range and the observable is the
     * port's own NULL store. */
    pSentinelA = (PadImage *)calloc(1, sizeof(PadImage));
    pSentinelB = (PadImage *)calloc(1, sizeof(PadImage));
    g_aBank[112].apPads[0] = pSentinelA;
    g_aBank[128].apPads[0] = pSentinelB;

    /* Real heap pads at both ends of the range, so a miss shows up as a pointer left set. */
    for (int k = 0; k < 3; ++k) {
        g_aBank[113].apPads[k] = (PadImage *)calloc(1, sizeof(PadImage));
    }
    g_aBank[127].apPads[0] = (PadImage *)calloc(1, sizeof(PadImage));

    nUiScreen = 7;
    g_nSoundCalls = g_nKillCalls = g_nFontCalls = g_nFreeCalls = 0;
    pStartCtl = (UiControl *)calloc(1, sizeof(UiControl));
    env.pStartCtl = pStartCtl;
    env.pBlockA = calloc(1, 16);
    env.pBlockB = calloc(1, 16);

    StartUi_Shutdown(&env);

    TestEqI32("the sound layer is shut down", g_nSoundCalls, 1);
    TestEqI32("the timers are killed", g_nKillCalls, 1);
    TestEqI32("the start UI's font is freed", g_nFontCalls, 1);
    TestCheck("and it is the font the environment carried", g_pFreedFont == &g_Font);

    TestCheck("widget 113's first pad is gone", g_aBank[113].apPads[0] == NULL);
    TestCheck("widget 113's third pad is gone", g_aBank[113].apPads[2] == NULL);
    TestCheck("widget 127 is still inside the range", g_aBank[127].apPads[0] == NULL);
    bKeepA = (g_aBank[112].apPads[0] == pSentinelA);
    bKeepB = (g_aBank[128].apPads[0] == pSentinelB);
    TestCheck("widget 112 is OUTSIDE the range and keeps its pad (0x38D8C's 15 groups start at 113)",
              bKeepA);
    TestCheck("widget 128 is outside the range too", bKeepB);

    /* Release the sentinels only while the port still holds them: a wrong range means the port has
     * already freed them, and a second free would be a new bug rather than a second check. */
    if (bKeepA) {
        free(pSentinelA);
    }
    if (bKeepB) {
        free(pSentinelB);
    }

    TestEqI32("the standalone control and the two blocks are freed", g_nFreeCalls, 3);
    TestEqI32("and the screen id becomes 22", nUiScreen, 22);

    /* With no standalone control, only the two blocks are freed. */
    memset(g_aBank, 0, sizeof(g_aBank));
    nUiScreen = 0;
    g_nFreeCalls = 0;
    env.pStartCtl = NULL;
    env.pBlockA = calloc(1, 16);
    env.pBlockB = calloc(1, 16);
    StartUi_Shutdown(&env);
    TestEqI32("a missing standalone control is skipped, not an error", g_nFreeCalls, 2);
    TestEqI32("and the screen id is still set", nUiScreen, 22);
}

/* MenuBtn_SetGamma0 @ 0x19070, SetGamma1 @ 0x190d0, SetGamma2 @ 0x19130, SetGamma3 @ 0x19190,
 * SetGamma4 @ 0x191f0.
 *
 * The interesting claim is which five widgets light. The originals write 0xACEFA, 0xACF1A, 0xACF3A,
 * 0xACF5A and 0xACF7A; against pBank @ 0xAC6D0 those are 32*w + 10 for w = 65..69, and 10 is
 * UiControl.nState. The sentinels on the neighbours prove the range is exactly those five. */
static void TestGamma(void)
{
    UiControl aBank[PUFO_BANK_GAMMA_FIRST + PUFO_BANK_GAMMA_COUNT + 1];
    const i32 nBankCount = (i32)(sizeof(aBank) / sizeof(aBank[0]));
    i32 nGamma;
    i32 k;

    Reset();

    for (nGamma = 0; nGamma <= PUFO_PAL_GAMMA_MAX; ++nGamma) {
        i32 nStore = -1;
        g_nSamples = 0;
        g_nOrder = 0;
        g_szOrder[0] = '\0';

        for (k = 0; k < nBankCount; ++k) {
            aBank[k].nState = 5;    /* sentinel: 1 or 0 is expected only on 65..69 */
        }

        MenuBtn_SetGamma(aBank, nGamma, &nStore, &g_MenuHooks);

        for (k = 0; k < PUFO_BANK_GAMMA_COUNT; ++k) {
            TestEqI32("  the gamma row lights exactly one widget",
                      (i32)aBank[PUFO_BANK_GAMMA_FIRST + k].nState,
                      (nGamma == k) ? 1 : 0);
        }
        TestEqI32("  the widget below the row is untouched",
                  (i32)aBank[PUFO_BANK_GAMMA_FIRST - 1].nState, 5);
        TestEqI32("  the widget above the row is untouched",
                  (i32)aBank[PUFO_BANK_GAMMA_FIRST + PUFO_BANK_GAMMA_COUNT].nState, 5);
        TestEqI32("  the gamma value is stored", nStore, nGamma);
        TestEqI32("  one sample is played", g_nSamples, 1);
        TestEqI32("  and it is sample 17", g_anSamples[0], PUFO_BTN_SAMPLE_GAMMA);
    }

    /* A gamma outside the row must clear all five rather than light something out of range. */
    for (k = 0; k < nBankCount; ++k) {
        aBank[k].nState = 5;
    }
    MenuBtn_SetGamma(aBank, 9, NULL, &g_MenuHooks);
    for (k = 0; k < PUFO_BANK_GAMMA_COUNT; ++k) {
        TestEqI32("  an out of range gamma clears the row",
                  (i32)aBank[PUFO_BANK_GAMMA_FIRST + k].nState, 0);
    }
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestConfirm();
    TestConfirmClear();
    TestSecondary();
    TestSecondaryDismiss();
    TestActionBack();
    TestCancel();
    TestSlotList();
    TestQuitDialog();
    TestQuitFromTact();
    TestStartMenu();
    TestStartUiShutdown();
    TestSaveLoadSlot();
    TestGamma();

    return TestSummary("test_menubtn");
}
