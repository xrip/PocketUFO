/* test_input.c - tactical/geoscape directional input handlers
 * (Input_OnLeft @ 0x116c0, Input_OnRight @ 0x117b0, Input_OnUp @ 0x118b0,
 * Input_OnDown @ 0x119b0, Input_OnAction @ 0x1557c, Input_OnMenuEscape @ 0x162ec). */
#include <stdio.h>
#include <string.h>

#include "game/pufo_input.h"

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (!bCond) {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

typedef struct Trace {
    int nPanelA;
    int nPanelB;
    int nFocusPrev;
    int nFocusNext;
    int nMapClick;
    int nScrollUp;
    int nScrollDown;
    int nSelNext;
    int nSelPrev;
    int nCursorChanged;
    int anSamples[8];
    int nSampleCount;
    int nMapClicks;
    int anConfirm[8];
    int nConfirmCount;
    int nModalEnd;
    int nShutdownReinit;
    int nShutdownQuit;
    int nModalEndCheckBattle;
    int nOnCancel;
} Trace;

static void Tr_PanelA(void *p) { ((Trace *)p)->nPanelA++; }
static void Tr_PanelB(void *p) { ((Trace *)p)->nPanelB++; }
static void Tr_FocusPrev(void *p) { ((Trace *)p)->nFocusPrev++; }
static void Tr_FocusNext(void *p) { ((Trace *)p)->nFocusNext++; }
static void Tr_MapClick(void *p) { ((Trace *)p)->nMapClick++; }
static void Tr_ScrollUp(void *p) { ((Trace *)p)->nScrollUp++; }
static void Tr_ScrollDown(void *p) { ((Trace *)p)->nScrollDown++; }
static void Tr_SelNext(void *p) { ((Trace *)p)->nSelNext++; }
static void Tr_SelPrev(void *p) { ((Trace *)p)->nSelPrev++; }
static void Tr_CursorChanged(void *p) { ((Trace *)p)->nCursorChanged++; }
static void Tr_Sample(u32 nSample, void *p)
{
    Trace *t = (Trace *)p;
    if (t->nSampleCount < 8) {
        t->anSamples[t->nSampleCount] = (int)nSample;
    }
    t->nSampleCount++;
}
static void Tr_HandleMapClick(void *p) { ((Trace *)p)->nMapClicks++; }
static void Tr_OnConfirm(u16 nScreen, void *p)
{
    Trace *t = (Trace *)p;
    if (t->nConfirmCount < 8) {
        t->anConfirm[t->nConfirmCount] = (int)nScreen;
    }
    t->nConfirmCount++;
}
static void Tr_ModalEnd(void *p) { ((Trace *)p)->nModalEnd++; }
static void Tr_ShutdownReinit(void *p) { ((Trace *)p)->nShutdownReinit++; }
static void Tr_ShutdownQuit(void *p) { ((Trace *)p)->nShutdownQuit++; }
static void Tr_ModalEndCheckBattle(void *p) { ((Trace *)p)->nModalEndCheckBattle++; }
static void Tr_OnCancel(void *p) { ((Trace *)p)->nOnCancel++; }

static TactInputHooks MakeHooks(Trace *t)
{
    TactInputHooks h;
    memset(&h, 0, sizeof(h));
    h.pfnGeoscapePanelA = Tr_PanelA;
    h.pfnGeoscapePanelB = Tr_PanelB;
    h.pfnGeoscapeFocusPrev = Tr_FocusPrev;
    h.pfnGeoscapeFocusNext = Tr_FocusNext;
    h.pfnGeoscapeMapClick = Tr_MapClick;
    h.pfnInvScrollUp = Tr_ScrollUp;
    h.pfnInvScrollDown = Tr_ScrollDown;
    h.pfnSelectNextUnit = Tr_SelNext;
    h.pfnSelectPrevUnit = Tr_SelPrev;
    h.pfnCursorChanged = Tr_CursorChanged;
    h.pfnPlaySample = Tr_Sample;
    h.pfnHandleMapClick = Tr_HandleMapClick;
    h.pfnOnConfirm = Tr_OnConfirm;
    h.pfnModalEnd = Tr_ModalEnd;
    h.pfnShutdownReinit = Tr_ShutdownReinit;
    h.pfnShutdownQuit = Tr_ShutdownQuit;
    h.pfnModalEndCheckBattle = Tr_ModalEndCheckBattle;
    h.pfnOnCancel = Tr_OnCancel;
    h.pUser = t;
    return h;
}

static TactInputState MakeState(void)
{
    TactInputState s;
    memset(&s, 0, sizeof(s));
    s.nTileShiftX = 8;
    s.nTileShiftY = 8;
    s.nMapDimX = 10;
    s.nMapDimY = 5;
    s.nLayerScratch = 2;
    return s;
}

static void TestGeoscape(void)
{
    Trace t;
    TactInputState s = MakeState();
    TactInputHooks h = MakeHooks(&t);

    memset(&t, 0, sizeof(t));
    s.bGeoscapeUi = true;
    Input_OnLeft(&s, &h);
    Input_OnRight(&s, &h);
    Input_OnUp(&s, &h);
    Input_OnDown(&s, &h);
    Input_OnAction(&s, &h, NULL);
    Check(t.nPanelA == 1 && t.nPanelB == 1, "geoscape left/right panels");
    Check(t.nFocusPrev == 1 && t.nFocusNext == 1, "geoscape up/down focus");
    Check(t.nMapClick == 1, "geoscape action map click");
}

static void TestGate(void)
{
    Trace t;
    TactInputState s = MakeState();
    TactInputHooks h = MakeHooks(&t);

    memset(&t, 0, sizeof(t));
    s.bFireStatus2 = 1;
    Input_OnLeft(&s, &h);
    s.bFireStatus2 = 0;
    s.bFireStatus1 = 1;
    Input_OnLeft(&s, &h);
    s.bFireStatus1 = 0;
    s.bTrajPad27_2 = 1;
    Input_OnLeft(&s, &h);
    s.bTrajPad27_2 = 0;
    s.bQueueActive = true;
    Input_OnLeft(&s, &h);
    Check(t.nCursorChanged == 0, "blocked input never changes the cursor");
}

static void TestCursorScroll(void)
{
    Trace t;
    TactInputState s = MakeState();
    TactInputHooks h = MakeHooks(&t);

    memset(&t, 0, sizeof(t));
    s.nCamScrollX = 0;
    s.nCamScrollY = 0;
    Input_OnLeft(&s, &h);
    Check(s.nCamScrollX == -1, "left decrements camera X");
    Check(t.nCursorChanged == 1, "left reports the cursor change");

    Input_OnRight(&s, &h);
    Check(s.nCamScrollX == 0, "right increments camera X");

    Input_OnUp(&s, &h);
    Check(s.nCamScrollY == -1, "up decrements camera Y");
    Input_OnDown(&s, &h);
    Check(s.nCamScrollY == 0, "down increments camera Y");

    /* Lower bound is -((u8)shift >> 1). */
    s.nCamScrollX = -(i32)(8 >> 1);
    Input_OnLeft(&s, &h);
    Check(s.nCamScrollX == -4, "left at lower bound does not move");

    /* Upper bound is 10 * nMapDimX - (shift >> 1). */
    s.nCamScrollX = 10 * 10 - 4;
    Input_OnRight(&s, &h);
    Check(s.nCamScrollX == 96, "right at upper bound does not move");
}

static void TestScreenRouting(void)
{
    Trace t;
    TactInputState s = MakeState();
    TactInputHooks h = MakeHooks(&t);

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 1;
    Input_OnLeft(&s, &h);
    Input_OnRight(&s, &h);
    Input_OnUp(&s, &h);
    Input_OnDown(&s, &h);
    Check(t.nScrollUp == 1 && t.nScrollDown == 1, "inventory screen scroll");
    Check(t.nSelNext == 1 && t.nSelPrev == 1, "inventory screen selection");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 2;
    s.anCamWorldX = 0;
    Input_OnLeft(&s, &h);
    Check(s.anCamWorldX == -1, "look screen left moves camera");
    Input_OnRight(&s, &h);
    Check(s.anCamWorldX == 0, "look screen right moves camera");
    s.nLayerScratch = 2;
    Input_OnUp(&s, &h);
    Check(s.nLayerScratch == 3, "look screen up raises a layer");
    Input_OnDown(&s, &h);
    Check(s.nLayerScratch == 2, "look screen down lowers a layer");
    Check(t.nSampleCount == 2 && t.anSamples[0] == 17, "look screen plays sample 17");
    s.nLayerScratch = 4;
    Input_OnUp(&s, &h);
    Check(s.nLayerScratch == 4, "layer clamps at 4");
    s.nLayerScratch = 1;
    Input_OnDown(&s, &h);
    Check(s.nLayerScratch == 1, "layer clamps at 1");
}

static void TestAction(void)
{
    Trace t;
    TactInputState s = MakeState();
    TactInputHooks h = MakeHooks(&t);
    bool bFlag = false;

    memset(&t, 0, sizeof(t));
    s.bTactFlag = true;
    s.nUiScreen = 0;
    Input_OnAction(&s, &h, &bFlag);
    Check(bFlag, "action sets the action flag");
    Check(t.nMapClicks == 1, "action with no screen handles a map click");

    memset(&t, 0, sizeof(t));
    bFlag = false;
    s.nUiScreen = 4;
    Input_OnAction(&s, &h, &bFlag);
    Check(t.nConfirmCount == 1 && t.anConfirm[0] == 4, "action confirms command screen");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 12;
    Input_OnAction(&s, &h, &bFlag);
    Check(s.nUiScreen == 0, "action clears screen 12");
    Check(t.nMapClicks == 0, "action on screen 12 does not click the map");
}

static void TestMenuEscape(void)
{
    Trace t;
    TactInputState s = MakeState();
    TactInputHooks h = MakeHooks(&t);

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 0;
    Input_OnMenuEscape(&s, &h);
    Check(s.nUiScreen == 7, "escape from no screen opens screen 7");
    Check(t.nSampleCount == 1 && t.anSamples[0] == 14, "escape plays sample 14");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 1;
    Input_OnMenuEscape(&s, &h);
    Check(t.nConfirmCount == 1 && t.anConfirm[0] == 1, "escape confirms screen 1");
    Check(s.nUiScreen == 1, "confirm screens keep their id");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 9;
    Input_OnMenuEscape(&s, &h);
    Check(s.nUiScreen == 0 && t.nConfirmCount == 1, "escape closes target screen");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 13;
    Input_OnMenuEscape(&s, &h);
    Check(t.nConfirmCount == 1 && s.nUiScreen == 13, "escape screen 13 keeps the id");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 15;
    Input_OnMenuEscape(&s, &h);
    Check(s.nUiScreen == 0, "escape clears screen 15");
}

static void TestDismissHigh(void)
{
    Trace t;
    TactInputState s = MakeState();
    TactInputHooks h = MakeHooks(&t);
    bool bFlag = true;

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 8;
    Check(UiScreen_DismissHigh(&s, &h, &bFlag) == PUFO_UI_DISMISS_MODAL,
          "dismiss screen 8 ends the modal");
    Check(s.nUiScreen == 0 && !bFlag, "dismiss screen 8 clears screen and flag");

    memset(&t, 0, sizeof(t));
    bFlag = true;
    s.nUiScreen = 9;
    Check(UiScreen_DismissHigh(&s, &h, &bFlag) == PUFO_UI_DISMISS_MODAL,
          "dismiss screen 9 ends the modal");
    Check(s.nUiScreen == 0, "dismiss screen 9 clears the screen");

    memset(&t, 0, sizeof(t));
    bFlag = true;
    s.nUiScreen = 13;
    Check(UiScreen_DismissHigh(&s, &h, &bFlag) == PUFO_UI_DISMISS_CANCEL,
          "dismiss screen 13 runs cancel");
    Check(s.nUiScreen == 13, "dismiss screen 13 keeps the screen id");
    Check(!bFlag, "dismiss clears the escape flag");

    memset(&t, 0, sizeof(t));
    bFlag = true;
    s.nUiScreen = 1;
    Check(UiScreen_DismissHigh(&s, &h, &bFlag) == PUFO_UI_DISMISS_NONE,
          "dismiss low screen is a no-op");
    Check(s.nUiScreen == 1, "no-op leaves the screen alone");
}

static void TestOnBackCancel(void)
{
    Trace t;
    TactInputState s = MakeState();
    TactInputHooks h = MakeHooks(&t);
    bool bFlag = true;

    /* UiScreen_OnBack. */
    memset(&t, 0, sizeof(t));
    s.nUiScreen = 8;
    UiScreen_OnBack(&s, &h, &bFlag);
    Check(t.nShutdownQuit == 1 && !bFlag, "back on screen 8 quits");

    memset(&t, 0, sizeof(t));
    bFlag = true;
    s.nUiScreen = 9;
    UiScreen_OnBack(&s, &h, &bFlag);
    Check(t.nModalEndCheckBattle == 1 && !bFlag, "back on screen 9 aborts battle");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 13;
    UiScreen_OnBack(&s, &h, &bFlag);
    Check(t.nOnCancel == 1, "back on screen 13 cancels");

    /* UiScreen_OnCancel. A clear tactical flag returns immediately. */
    memset(&t, 0, sizeof(t));
    s.bTactFlag = false;
    Check(UiScreen_OnCancel(&s, &h) == 0, "cancel with no tactical flag returns 0");
    Check(t.nModalEnd == 0, "cancel does nothing without a tactical flag");

    memset(&t, 0, sizeof(t));
    s.bTactFlag = true;
    s.nUiScreen = 8;
    Check(UiScreen_OnCancel(&s, &h) == 0, "cancel screen 8 returns 0");
    Check(s.nUiScreen == 0 && t.nModalEnd == 1, "cancel screen 8 ends the modal");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 13;
    s.bCancelTo19 = true;
    Check(UiScreen_OnCancel(&s, &h) == 19, "cancel screen 13 escapes to 19");
    Check(s.nUiScreen == 19, "cancel screen 13 stores 19");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 13;
    s.bCancelTo19 = false;
    UiScreen_OnCancel(&s, &h);
    Check(t.nShutdownReinit == 1, "cancel screen 13 tears down to geoscape");

    memset(&t, 0, sizeof(t));
    s.nUiScreen = 21;
    s.nUiSlot11 = 5;
    UiScreen_OnCancel(&s, &h);
    Check(s.nUiScreen == 5, "cancel screen 21 adopts apUiSlotsA[11]");

    /* The fire gate needs the game-tick hold to proceed. */
    memset(&t, 0, sizeof(t));
    s.nUiScreen = 8;
    s.bFireStatus2 = 1;
    s.bGameTickHold = false;
    UiScreen_OnCancel(&s, &h);
    Check(t.nModalEnd == 0, "cancel is gated by the tick hold while firing");
}

int main(void)
{
    TestGeoscape();
    TestGate();
    TestCursorScroll();
    TestScreenRouting();
    TestAction();
    TestMenuEscape();
    TestDismissHigh();
    TestOnBackCancel();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
