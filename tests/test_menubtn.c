/* test_menubtn.c - the widget click adapters.
 * (MenuBtn_Confirm0..5 @ 0x19650..0x1a014, ConfirmSnd15 @ 0x198f0,
 *  ConfirmClear @ 0x194c8, Secondary @ 0x19498, SecondaryDismiss @ 0x195e0,
 *  ActionBack0/1 @ 0x195a8/0x19618, Cancel0/1 @ 0x1a24c/0x1a26c,
 *  Menu_OpenLoadSlots @ 0x18864, Menu_OpenSaveSlots @ 0x188a4,
 *  Menu_ConfirmQuitDialog @ 0x18e2c, Menu_CancelQuitDialog @ 0x18e04.) */
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

static void YesCb(void) {}
static void NoCb(void) {}

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

    return TestSummary("test_menubtn");
}
