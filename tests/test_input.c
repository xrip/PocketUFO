/* test_input.c - tactical/geoscape directional input handlers
 * (Input_OnLeft @ 0x116c0, Input_OnRight @ 0x117b0, Input_OnUp @ 0x118b0,
 * Input_OnDown @ 0x119b0, Input_OnAction @ 0x1557c, Input_OnMenuEscape @ 0x162ec). */
#include <stdio.h>
#include <string.h>

#include "game/pufo_input.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_tactstate.h"  /* TactState, TactQueueEntry, TACTCMD_* */
#include "game/pufo_tactui.h"     /* PUFO_ACTION_LATCH_*, PUFO_TXT_OUT_OF_TU */
#include "game/pufo_unit.h"   /* the full Soldier type, PUFO_RANK_NAME_STRIDE */
#include "util/pufo_str.h"    /* pufo_strcpy: strcpy is deprecated under /WX */
#include "pufo_test.h"

/* This suite keeps the (condition, what) argument order its call sites use. */
static inline void Check(bool bCond, const char *pszWhat)
{
    TestCheck(pszWhat, bCond);
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
    int nGeoscapeClose;
    int nInvCommit;
    int nSetCursor;
    int nCursorX;
    int nCursorY;
    int nFocusCount;
    int nFocusLayer;
    int nFocusY;
    int nFocusX;
    int nSaveOptions;
    /* StatusMsg_Push records, in order, so a refusal can be pinned by index. */
    int anText[8];
    int nTextCount;
    /* The Unit_PathToCell seam. */
    int nPathCalls;
    int nPathX;
    int nPathY;
    int nPathLayer;
    int nPathFlag;
} Trace;

static void Tr_PushText(i32 nRecord, void *p)
{
    Trace *t = (Trace *)p;
    if (t->nTextCount < 8) {
        t->anText[t->nTextCount] = nRecord;
    }
    t->nTextCount++;
}

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
static void Tr_GeoscapeClose(void *p) { ((Trace *)p)->nGeoscapeClose++; }
static void Tr_InvCommit(void *p) { ((Trace *)p)->nInvCommit++; }
static void Tr_SetCursor(i32 nX, i32 nY, void *p)
{
    Trace *t = (Trace *)p;
    t->nSetCursor++;
    t->nCursorX = nX;
    t->nCursorY = nY;
}
static void Tr_FocusView(i32 nLayer, i32 nViewY, i32 nViewX, void *p)
{
    Trace *t = (Trace *)p;
    t->nFocusCount++;
    t->nFocusLayer = nLayer;
    t->nFocusY = nViewY;
    t->nFocusX = nViewX;
}
static void Tr_SaveOptions(void *p) { ((Trace *)p)->nSaveOptions++; }

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
    h.pfnGeoscapeCloseUi = Tr_GeoscapeClose;
    h.pfnInvCloseAndCommit = Tr_InvCommit;
    h.pfnSetCursorPos = Tr_SetCursor;
    h.pfnFocusOnViewCell = Tr_FocusView;
    h.pfnSaveOptions = Tr_SaveOptions;
    h.pfnPushTextRecord = Tr_PushText;
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

/* Input_OnConfirm @ 0x15f3c. */
static MapCell g_aCell[2];
static TactItem g_aItem[2];
static ObjectRecFull g_aObj[2];
static Soldier g_aUnit[2];
static u8 g_aScratch9;

static void ConfirmWeapon(TactInputState *pS, int nItem, u16 nSortKey)
{
    g_aObj[nItem].uSortKey = nSortKey;
    g_aItem[nItem].pObj = &g_aObj[nItem];
    pS->pSelWeapon = &g_aItem[nItem];
}

static TactInputState ConfirmState(void)
{
    TactInputState s = MakeState();
    s.bTactFlag = true;
    return s;
}

static void TestOnConfirm(void)
{
    Trace t;
    TactInputHooks h = MakeHooks(&t);
    TactInputState s;
    static const u16 aModal[5] = { 3u, 5u, 10u, 11u, 15u };
    static const u16 aNone[5] = { 8u, 9u, 13u, 14u, 16u };

    /* The geoscape branch closes the geoscape UI and stops. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.bGeoscapeUi = true;
    s.nUiScreen = 0;
    Input_OnConfirm(&s, &h);
    Check(t.nGeoscapeClose == 1, "confirm closes the geoscape UI when bGeoscapeUi");
    Check(t.nModalEnd == 0 && t.nSampleCount == 0, "confirm stops after the geoscape branch");

    /* The tactical gate. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.bTactFlag = false;
    Input_OnConfirm(&s, &h);
    Check(t.nGeoscapeClose == 0 && t.nSampleCount == 0, "confirm needs bTactFlag");

    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.bFireStatus1 = 1;
    Input_OnConfirm(&s, &h);
    Check(t.nSampleCount == 0, "confirm is blocked while a shot is in flight");

    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.bQueueActive = true;
    Input_OnConfirm(&s, &h);
    Check(t.nSampleCount == 0, "confirm is blocked while the action queue runs");

    /* Screen 0, multi-target not armed: sample 7 and the deploy panel toggles. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 0;
    s.bDeployPanelOpen = 0;
    Input_OnConfirm(&s, &h);
    Check(t.nSampleCount == 1 && t.anSamples[0] == 7, "screen 0 unarmed plays sample 7");
    Check(s.bDeployPanelOpen == 1, "screen 0 unarmed opens the deploy panel");
    Input_OnConfirm(&s, &h);
    Check(s.bDeployPanelOpen == 0, "screen 0 unarmed closes it again");

    /* Screen 0, armed: scratch flag, cursor, target marks, count. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 0;
    s.nActionLatch = 1;
    s.pbScratchFlag9 = &g_aScratch9;
    g_aScratch9 = 3;
    s.nCursorX = 7;
    s.nCursorY = 9;
    ConfirmWeapon(&s, 0, 13);
    g_aCell[0].bFlags29 = 0x60;
    g_aCell[1].bFlags29 = 0x60;
    s.apMultiTargets[0] = &g_aCell[0];
    s.apMultiTargets[1] = &g_aCell[1];
    s.nMultiTargetCount = 2;
    Input_OnConfirm(&s, &h);
    Check(g_aScratch9 == 0, "armed confirm clears the *unk_1571E4+9 scratch");
    Check(s.nActionLatch == 0, "armed confirm clears the action latch");
    Check(t.nSetCursor == 1 && t.nCursorX == 7 && t.nCursorY == 9,
          "armed confirm re-places the cursor at nCursorX/nCursorY");
    Check((g_aCell[0].bFlags29 & 0x40) == 0 && (g_aCell[1].bFlags29 & 0x40) == 0,
          "uSortKey 13 clears the target mark on both cells");
    Check((g_aCell[0].bFlags29 & 0x20) != 0, "only bit 6 is cleared, not bit 5");
    Check(s.nMultiTargetCount == 0, "armed confirm clears the target count");

    /* A weapon that is not one of the two multi-target kinds keeps its marks. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 0;
    s.nActionLatch = 1;
    ConfirmWeapon(&s, 1, 20);
    g_aCell[0].bFlags29 = 0x40;
    s.apMultiTargets[0] = &g_aCell[0];
    s.nMultiTargetCount = 1;
    Input_OnConfirm(&s, &h);
    Check((g_aCell[0].bFlags29 & 0x40) != 0, "uSortKey 20 keeps the target mark");
    Check(s.nMultiTargetCount == 0, "the count clears whatever the weapon");

    /* uSortKey 35 behaves like 13. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 0;
    s.nActionLatch = 1;
    ConfirmWeapon(&s, 1, 35);
    g_aCell[0].bFlags29 = 0x40;
    s.apMultiTargets[0] = &g_aCell[0];
    s.nMultiTargetCount = 1;
    Input_OnConfirm(&s, &h);
    Check((g_aCell[0].bFlags29 & 0x40) == 0, "uSortKey 35 clears the target mark");

    /* Screen 1: inventory. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 1;
    Input_OnConfirm(&s, &h);
    Check(t.nSampleCount == 1 && t.anSamples[0] == 16, "screen 1 plays sample 16");
    Check(t.nInvCommit == 1, "screen 1 closes and commits the inventory");

    /* Screen 2: look mode focuses on the unit stored for the camera cell. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 2;
    s.nLayerScratch = 2;
    s.anCamWorldX = 11;
    s.anCamWorldY = 13;
    Input_OnConfirm(&s, &h);
    Check(t.nFocusCount == 1 && t.nFocusLayer == 2 && t.nFocusY == 13 && t.nFocusX == 11,
          "screen 2 focuses on layer/anCamWorldOff[1]/[0]");
    Check(s.nUiScreen == 0, "screen 2 clears the ui screen");
    Check(t.nSampleCount == 1 && t.anSamples[0] == 16, "screen 2 plays sample 16");

    /* Screens 3, 5, 10, 11, 15 end the modal and clear the screen. */
    for (int i = 0; i < 5; ++i) {
        memset(&t, 0, sizeof(t));
        s = ConfirmState();
        s.nUiScreen = aModal[i];
        Input_OnConfirm(&s, &h);
        Check(t.nModalEnd == 1 && s.nUiScreen == 0,
              "modal screens 3/5/10/11/15 end the modal and clear the screen");
    }

    /* Screen 4: the command pad. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 4;
    s.bLookCommitFlag = 1;
    s.pLookCommitUnit = &g_aUnit[1];
    s.pSelUnit = &g_aUnit[0];
    s.bUiSlot8Byte0 = 1;
    Input_OnConfirm(&s, &h);
    Check(s.pSelUnit == &g_aUnit[1], "screen 4 adopts the saved unit pointer");
    Check(s.nUiScreen == 1, "screen 4 keeps the pad open from apUiSlotsA[8]");
    Check(s.bLookCommitFlag == 0 && s.bUiSlot8Byte0 == 0, "screen 4 clears both scratch fields");
    Check(t.nSampleCount == 1 && t.anSamples[0] == 16, "screen 4 plays sample 16");

    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 4;
    s.bLookCommitFlag = 0;
    s.pSelUnit = &g_aUnit[0];
    s.bUiSlot8Byte0 = 0;
    Input_OnConfirm(&s, &h);
    Check(s.pSelUnit == &g_aUnit[0], "screen 4 without the flag keeps pSelUnit");
    Check(s.nUiScreen == 0, "screen 4 with a clear slot byte closes the pad");

    /* Screens 6, 7, 12. */
    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 6;
    Input_OnConfirm(&s, &h);
    Check(t.nSampleCount == 1 && t.anSamples[0] == 16 && s.nUiScreen == 0,
          "screen 6 plays 16 and clears the screen");

    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 7;
    Input_OnConfirm(&s, &h);
    Check(t.nSampleCount == 1 && t.anSamples[0] == 14 && s.nUiScreen == 0,
          "screen 7 plays 14 and clears the screen");

    memset(&t, 0, sizeof(t));
    s = ConfirmState();
    s.nUiScreen = 12;
    Input_OnConfirm(&s, &h);
    Check(t.nSaveOptions == 1 && s.nUiScreen == 0, "screen 12 saves the options to the registry");

    /* Screens 8, 9, 13, 14 and 16 do nothing at all. */
    for (int i = 0; i < 5; ++i) {
        memset(&t, 0, sizeof(t));
        s = ConfirmState();
        s.nUiScreen = aNone[i];
        Input_OnConfirm(&s, &h);
        Check(t.nSampleCount == 0 && t.nModalEnd == 0 && s.nUiScreen == aNone[i],
              "screens 8/9/13/14/16 fall through the switch");
    }
}

/* Input_OnSecondary @ 0x156e0. Needs a live grid, a queue and a unit. */
#define SEC_DIM 8
static MapCell   g_aGrid[4][SEC_DIM][SEC_DIM];
static TactState g_SecQueue;
static Soldier   g_SecUnit;
static Mc1Rec    g_SecWall;

static MapCell *SecLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nLayer < 0 || nLayer >= 4 || nX < 0 || nX >= SEC_DIM || nY < 0 || nY >= SEC_DIM) {
        return NULL;
    }
    return &g_aGrid[nLayer][nY][nX];
}

/* One tactical map: unit at (1,1,0) facing east, cursor at (3,2) on layer 1
 * (so its cell is layer 0), and a door wall on the cursor cell. */
static void SecWorldReset(MapCell **ppDoor)
{
    i32 nLayer;
    i32 nY;
    i32 nX;

    for (nLayer = 0; nLayer < 4; ++nLayer) {
        for (nY = 0; nY < SEC_DIM; ++nY) {
            for (nX = 0; nX < SEC_DIM; ++nX) {
                MapCell_Init(&g_aGrid[nLayer][nY][nX], nX, nY, nLayer);
                /* MapCell_Init keeps the door bit, so clear it here. */
                g_aGrid[nLayer][nY][nX].bFlags57[0] = 0;
            }
        }
    }
    memset(&g_SecQueue, 0, sizeof(g_SecQueue));
    memset(&g_SecUnit, 0, sizeof(g_SecUnit));
    memset(&g_SecWall, 0, sizeof(g_SecWall));
    g_SecWall.gap_2f[0] = 1;                 /* a real wall the door can open */
    g_SecUnit.pCell    = &g_aGrid[0][1][1];  /* (1,1,0) */
    g_SecUnit.bFacing  = 2;                  /* east */
    g_SecUnit.nUnitType = 0;                 /* a small unit */
    g_aGrid[0][2][3].pWallSprA = &g_SecWall; /* the cursor cell */
    *ppDoor = &g_aGrid[0][1][2];             /* the cell east of the unit */
}

static TactInputState SecState(void)
{
    TactInputState s = ConfirmState();
    s.nMapDimX     = 1;   /* cursor range 0..9 */
    s.nMapDimY     = 1;
    s.nLayerScratch = 1;  /* the cursor cell is on layer 0 */
    s.nCursorX     = 3;
    s.nCursorY     = 2;
    s.pSelUnit     = &g_SecUnit;
    return s;
}

static void TestOnSecondary(void)
{
    Trace           t;
    TactInputHooks  h = MakeHooks(&t);
    TactInputState  s;
    TactInputWorld  w = { &g_SecQueue, SecLookup, NULL };
    MapCell        *pDoor;
    bool            bEscape;
    static const u16 aClear[5] = { 6u, 10u, 11u, 12u, 15u };
    static const u16 aNone[8]  = { 3u, 5u, 7u, 8u, 9u, 13u, 14u, 16u };

    /* The geoscape branch is a no-op, and it does not touch the escape flag. */
    SecWorldReset(&pDoor);
    memset(&t, 0, sizeof(t));
    bEscape = false;
    s = SecState();
    s.bGeoscapeUi = true;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(t.nSampleCount == 0 && !bEscape && g_SecQueue.nQueueLen == 0,
          "secondary does nothing in the geoscape");

    /* The tactical gate. */
    memset(&t, 0, sizeof(t));
    s = SecState();
    s.bTactFlag = false;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(g_SecQueue.nQueueLen == 0 && !bEscape, "secondary needs bTactFlag");

    memset(&t, 0, sizeof(t));
    s = SecState();
    s.bQueueActive = true;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(g_SecQueue.nQueueLen == 0 && !bEscape, "secondary is blocked while the queue runs");

    /* Screens 1, 2 and 4 delegate to Input_OnConfirm. */
    memset(&t, 0, sizeof(t));
    s = SecState();
    s.nUiScreen = 1;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(t.nInvCommit == 1 && t.anSamples[0] == 16, "screen 1 runs the confirm inventory path");

    memset(&t, 0, sizeof(t));
    s = SecState();
    s.nUiScreen = 2;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(t.nFocusCount == 1 && s.nUiScreen == 0, "screen 2 runs the confirm look path");

    memset(&t, 0, sizeof(t));
    s = SecState();
    s.nUiScreen = 4;
    s.bLookCommitFlag = 1;
    s.pLookCommitUnit = &g_SecUnit;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(s.pSelUnit == &g_SecUnit && s.nUiScreen == 0 && t.nSampleCount == 1,
          "screen 4 runs the confirm command pad");

    /* Screens that only clear themselves. */
    for (int i = 0; i < 5; ++i) {
        memset(&t, 0, sizeof(t));
        s = SecState();
        s.nUiScreen = aClear[i];
        Input_OnSecondary(&s, &h, &w, &bEscape);
        Check(s.nUiScreen == 0 && g_SecQueue.nQueueLen == 0,
              "secondary clears the screen id");
    }

    /* Screens that do nothing at all. */
    for (int i = 0; i < 8; ++i) {
        memset(&t, 0, sizeof(t));
        s = SecState();
        s.nUiScreen = aNone[i];
        Input_OnSecondary(&s, &h, &w, &bEscape);
        Check(s.nUiScreen == aNone[i] && g_SecQueue.nQueueLen == 0 && !bEscape,
              "secondary leaves these screens alone");
    }

    /* Screen 0, mode 3: pop the last selected target. */
    SecWorldReset(&pDoor);
    memset(&t, 0, sizeof(t));
    g_aScratch9 = 1;
    s = SecState();
    s.nActionLatch = 3;
    s.pbScratchFlag9    = &g_aScratch9;
    s.nMultiTargetCount = 2;
    s.apMultiTargets[0] = &g_aGrid[0][1][1];
    s.apMultiTargets[1] = &g_aGrid[0][2][2];
    s.apMultiTargets[0]->bFlags29 = 0x40;
    s.apMultiTargets[1]->bFlags29 = 0x60;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(s.apMultiTargets[1]->bFlags29 == 0x20, "secondary pops the last target mark");
    Check(s.apMultiTargets[0]->bFlags29 == 0x40, "secondary leaves the earlier target marked");
    Check(s.nMultiTargetCount == 1 && g_aScratch9 == 1, "the scratch flag waits for the last pop");

    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(s.nMultiTargetCount == 0 && g_aScratch9 == 0,
          "the last pop clears the multi-target scratch flag");

    s.apMultiTargets[0]->bFlags29 = 0x40;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(s.apMultiTargets[0]->bFlags29 == 0x40 && s.nMultiTargetCount == 0,
          "an empty target list pops nothing");

    /* Screen 0, cursor outside the map: the escape flag is set, nothing else. */
    SecWorldReset(&pDoor);
    memset(&t, 0, sizeof(t));
    bEscape = false;
    s = SecState();
    s.nCursorX = 10;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(bEscape, "secondary sets the escape flag before the cursor test");
    Check(g_SecQueue.nQueueLen == 0, "an out-of-map cursor queues no action");

    /* Screen 0, east-facing small unit onto a door cell: Map_QueueDoorAction
     * appends its open+door pair, then the unit turns toward the cursor cell and
     * the queue starts. */
    SecWorldReset(&pDoor);
    memset(&t, 0, sizeof(t));
    bEscape = false;
    pDoor->bFlags57[0] = 8;
    s = SecState();
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(bEscape, "secondary sets the escape flag in tactical mode");
    Check(g_SecQueue.nQueueLen == 3, "secondary queues the door action and the turn");
    Check(g_SecQueue.aQueue[0].nCmd == TACTCMD_OPEN
          && g_SecQueue.aQueue[1].nCmd == TACTCMD_DOOR
          && g_SecQueue.aQueue[1].nFacing == 4
          && g_SecQueue.aQueue[1].nX == 3 && g_SecQueue.aQueue[1].nY == 2,
          "the door pair carries the cursor cell and the door facing");
    Check(g_SecQueue.aQueue[2].nCmd == TACTCMD_TURN, "secondary turns the unit to the cursor");
    Check(g_SecQueue.bQueueActive, "secondary kicks the queue");

    /* A diagonal facing never reaches the door path. */
    SecWorldReset(&pDoor);
    memset(&t, 0, sizeof(t));
    pDoor->bFlags57[0] = 8;
    s = SecState();
    g_SecUnit.bFacing = 1;   /* north-east */
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(g_SecQueue.nQueueLen == 1 && g_SecQueue.aQueue[0].nCmd == TACTCMD_TURN,
          "odd facings never open a door");

    /* A large unit never opens a door either. */
    SecWorldReset(&pDoor);
    memset(&t, 0, sizeof(t));
    pDoor->bFlags57[0] = 8;
    s = SecState();
    g_SecUnit.nUnitType = 4;  /* a large type */
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(g_SecQueue.nQueueLen == 1 && g_SecQueue.aQueue[0].nCmd == TACTCMD_TURN,
          "large units never open a door");

    /* No door mark on the cell in front: only the turn. */
    SecWorldReset(&pDoor);
    memset(&t, 0, sizeof(t));
    s = SecState();
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(g_SecQueue.nQueueLen == 1 && g_SecQueue.aQueue[0].nCmd == TACTCMD_TURN,
          "no door mark means no door action");

    /* The cursor cell is the unit's own cell: no turn, no kick. */
    SecWorldReset(&pDoor);
    memset(&t, 0, sizeof(t));
    s = SecState();
    s.nCursorX = 1;
    s.nCursorY = 1;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(g_SecQueue.nQueueLen == 0 && !g_SecQueue.bQueueActive,
          "secondary does not turn a unit toward its own cell");

    /* Layer 0 has no layer below it, so the cursor cell does not exist. */
    SecWorldReset(&pDoor);
    memset(&t, 0, sizeof(t));
    s = SecState();
    s.nLayerScratch = 0;
    Input_OnSecondary(&s, &h, &w, &bEscape);
    Check(g_SecQueue.nQueueLen == 0 && !g_SecQueue.bQueueActive,
          "layer 0 has no cursor cell to face");
}

/* --- Input_OnPointer @ 0x13c6c ------------------------------------------------
 *
 * The pointer handler is mostly geometry, so the checks pin the boxes: the screen-17
 * row bands, the status-line columns, the eleven HUD edge tests, the multi-target
 * list rows, and which widgets a screen answers to. */
typedef struct PtrTrace {
    i32 nFocusCell;
    i32 nInvClear;
    i32 nInvHandle;
    i32 nHitTest;
    i32 nGeoscape;
    i32 nSample;
    i32 nGeosUp;
    i32 nTitles;
    i32 nEndScreen;
    i32 nModalEnd;
    i32 nPushRec;
    i32 nLastRec;
    i32 nSetCursor;
    i32 nCtlClick;
    i32 nSlot16;
    i32 nSlot18;
    MapCell *pLastCell;
} PtrTrace;

static PtrTrace g_ptr;
static UiWidgetBank g_bank;
static PadImage g_hudPad;
static CamOffset g_camOff;
static MapCell g_ptrCell;
static Soldier g_aPtrUnits[2];

static void Tr_GeosPointer(i32 x, i32 y, bool bDown, void *pUser)
{
    (void)x; (void)y; (void)bDown;
    ((PtrTrace *)pUser)->nGeoscape++;
}
static bool Tr_HitTestItem(i32 x, i32 y, void *pUser)
{
    (void)x; (void)y;
    ((PtrTrace *)pUser)->nHitTest++;
    return true;
}
static void Tr_InvClear(void *pUser) { ((PtrTrace *)pUser)->nInvClear++; }
static void Tr_InvHandle(UiWidgetBank *pBank, void *pUser)
{
    (void)pBank;
    ((PtrTrace *)pUser)->nInvHandle++;
}
static void Tr_FocusCell(MapCell *pCell, void *pUser)
{
    ((PtrTrace *)pUser)->pLastCell = pCell;
    ((PtrTrace *)pUser)->nFocusCell++;
}
static void Tr_PtrSample(u32 nSample, void *pUser)
{
    ((PtrTrace *)pUser)->nSample = (i32)nSample;
}
static void Tr_GeosUp(void *pUser)        { ((PtrTrace *)pUser)->nGeosUp++; }
static void Tr_Titles(void *pUser)        { ((PtrTrace *)pUser)->nTitles++; }
static void Tr_EndScreen(void *pUser)     { ((PtrTrace *)pUser)->nEndScreen++; }
static void Tr_UpModalEnd(void *pUser)    { ((PtrTrace *)pUser)->nModalEnd++; }
static void Tr_UpSetCursor(i32 x, i32 y, void *pUser)
{
    (void)x; (void)y;
    ((PtrTrace *)pUser)->nSetCursor++;
}
static void Tr_PushRec(i32 nRecord, void *pUser)
{
    ((PtrTrace *)pUser)->nPushRec++;
    ((PtrTrace *)pUser)->nLastRec = nRecord;
}
static void Tr_CtlClick(void) { g_ptr.nCtlClick++; }
static void Tr_Slot16(void)   { g_ptr.nSlot16++; }
static void Tr_Slot18(void)   { g_ptr.nSlot18++; }

static MapCell *PtrLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)nX; (void)nY; (void)nLayer; (void)pUser;
    return &g_ptrCell;
}

static TactInputHooks PtrHooks(void)
{
    TactInputHooks h;
    memset(&h, 0, sizeof(h));
    h.pfnGeoscapePointer  = Tr_GeosPointer;
    h.pfnHitTestTactItem  = Tr_HitTestItem;
    h.pfnInvClearHitSlot  = Tr_InvClear;
    h.pfnInvHandlePointer = Tr_InvHandle;
    h.pfnFocusOnCell      = Tr_FocusCell;
    h.pfnPlaySample       = Tr_PtrSample;
    h.pfnGeoscapePointerUp = Tr_GeosUp;
    h.pfnTitlesToStartup  = Tr_Titles;
    h.pfnEndScreenShutdown = Tr_EndScreen;
    h.pfnModalEnd         = Tr_UpModalEnd;
    h.pfnSetCursorPos     = Tr_UpSetCursor;
    h.pfnPushTextRecord   = Tr_PushRec;
    h.pUser               = &g_ptr;
    return h;
}

/* A bank of disabled widgets plus one live widget per index we care about. */
static void PtrBankReset(void)
{
    i32 i;
    memset(&g_bank, 0, sizeof(g_bank));
    for (i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        g_bank.aCtl[i].bEnabled = 0;
    }
}

static void PtrLive(i32 nIndex, i32 x, i32 y, u16 w, u16 h)
{
    UiControl *p = &g_bank.aCtl[nIndex];
    p->bEnabled = 1;
    p->nMode    = 0;
    p->nState   = 0;
    p->x        = (i16)x;
    p->y        = (i16)y;
    p->uWidth   = w;
    p->uHeight  = h;
}

/* For the release pass the hit box is never consulted, so every widget is made a
 * live mode-0 control in state 1 with a counting click handler. After the call a
 * widget still in state 1 was not released, which is how the ranges are pinned. */
static void PtrBankLiveAll(void)
{
    i32 i;
    PtrBankReset();
    for (i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        g_bank.aCtl[i].bEnabled = 1;
        g_bank.aCtl[i].nMode    = 0;
        g_bank.aCtl[i].nState   = 1;
        g_bank.aCtl[i].pfnClick = Tr_CtlClick;
    }
}

static TactPointerWorld PtrWorld(void)
{
    TactPointerWorld w;
    memset(&w, 0, sizeof(w));
    w.pBank        = &g_bank;
    w.pHudPad      = &g_hudPad;
    w.pCamOffset   = &g_camOff;
    w.nListMode    = 0;
    w.tact.pfnLookup    = PtrLookup;
    w.tact.pLookupUser  = NULL;
    w.hit.pnUiSlot0     = NULL;
    w.hit.pfnBlit       = NULL;
    w.hit.pfnBlitEx     = NULL;
    w.hit.pUser         = NULL;
    return w;
}

static TactInputState PtrState(u16 nScreen)
{
    TactInputState s = MakeState();
    s.bTactFlag    = true;
    s.nUiScreen    = nScreen;
    s.bEdgeTestOn  = 1;
    return s;
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    TestEqI32(pszWhat, nGot, nWant);
}

static void TestPointer(void)
{
    TactInputHooks h = PtrHooks();
    TactPointerWorld w = PtrWorld();
    TactInputState s;

    printf("Input_OnPointer -- screens, boxes and the camera drag\n");

    /* The geoscape branch owns everything. */
    PtrBankReset();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(0);
    s.bGeoscapeUi = true;
    Input_OnPointer(&s, &h, &w, 10, 10, true);
    Check(g_ptr.nGeoscape == 1, "  geoscape branch runs");

    /* No tactical flag: nothing runs, and the edge flags stay cleared. */
    PtrBankReset();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(0);
    s.bTactFlag = false;
    s.nUiSlot5 = 0xFF;
    Input_OnPointer(&s, &h, &w, 10, 10, true);
    CheckEqI32((i32)s.nUiSlot5, 0, "  edge flags clear even when the gate refuses");
    Check(g_ptr.nGeoscape == 0, "  nothing else runs without bTactFlag");

    /* The position is stored in the bank on entry. */
    CheckEqI32(g_bank.nPointerX, 10, "  pointer x stored");
    CheckEqI32(g_bank.nPointerY, 10, "  pointer y stored");

    /* Screen 17, mode 0: four rows of 22 starting at 115. */
    PtrBankReset();
    s = PtrState(17);
    w.nListMode = 0;
    memset(w.abListHit, 0xFF, sizeof(w.abListHit));
    Input_OnPointer(&s, &h, &w, 100, 120, true);
    CheckEqI32(w.abListHit[0], 1, "  screen 17 mode 0 marks row 0");
    CheckEqI32(w.abListHit[1], 0, "  and only that row");
    Input_OnPointer(&s, &h, &w, 100, 140, true);
    CheckEqI32(w.abListHit[1], 1, "  row 1 spans 137..155");
    Input_OnPointer(&s, &h, &w, 10, 120, true);
    CheckEqI32(w.abListHit[0], 0, "  outside the column clears the row");

    /* Screen 17, mode 2: seven rows starting at 65, plus the control. */
    PtrBankReset();
    PtrLive(0, 0, 0, 100, 100);
    w.pListCtl = &g_bank.aCtl[0];
    w.nListMode = 2;
    s = PtrState(17);
    memset(w.abListHit, 0xFF, sizeof(w.abListHit));
    Input_OnPointer(&s, &h, &w, 50, 70, true);
    CheckEqI32(w.abListHit[0], 1, "  mode 2 marks its own row band");
    CheckEqI32(w.abListHit[6], 0, "  mode 2 has seven rows only");
    CheckEqI32((i32)w.pListCtl->nState, 1, "  mode 2 also hit-tests the control");

    /* Screen 13 clears the fire flags and the queue before the switch. */
    PtrBankReset();
    s = PtrState(13);
    s.bFireStatus1 = 1;
    s.bFireStatus2 = 1;
    s.bTrajPad27_2 = 1;
    s.bQueueActive = true;
    Input_OnPointer(&s, &h, &w, 10, 10, true);
    Check(s.bFireStatus1 == 0 && s.bFireStatus2 == 0 && s.bTrajPad27_2 == 0 &&
          !s.bQueueActive, "  screen 13 clears the fire flags and the queue");

    /* The status-line hit box. */
    PtrBankReset();
    s = PtrState(0);
    s.bSlot19Active = 1;
    s.nSlot23H = 18;
    s.nSlot22Y = 200;
    s.nSlot20X = 50;
    s.nSlot21X = 120;
    s.bEdgeTestOn = 0;
    s.bNoHalfCursor = 1;
    Input_OnPointer(&s, &h, &w, 60, 205, true);
    CheckEqI32(s.nSlot24Hit, 1, "  left status column");
    Input_OnPointer(&s, &h, &w, 130, 205, true);
    CheckEqI32(s.nSlot24Hit, 2, "  right status column");
    Input_OnPointer(&s, &h, &w, 60, 250, true);
    CheckEqI32(s.nSlot24Hit, 0, "  outside the status row");
    s.bSlot19Active = 0;
    s.nSlot24Hit = 7;
    Input_OnPointer(&s, &h, &w, 60, 205, true);
    CheckEqI32(s.nSlot24Hit, 7, "  an inactive status box is left alone");

    /* The HUD edge flags. Deploy panel open: band 303, thirds 101 and 202. */
    #define EDGE(x_, y_) (PtrBankReset(), s.nUiSlot5 = 0, s.bEdgeTestOn = 1, \
                          s.bDeployPanelOpen = 1, Input_OnPointer(&s, &h, &w, (x_), (y_), true), \
                          (i32)s.nUiSlot5)
    s.bSlot19Active = 0;
    CheckEqI32(EDGE(5, 150), 0x00000001, "  left edge flag");
    CheckEqI32(EDGE(230, 150), 0x00000100, "  right edge flag");
    CheckEqI32(EDGE(100, 10), 0x00010000, "  top edge flag");
    CheckEqI32(EDGE(100, 295), 0x1000100, "  right and bottom together");
    CheckEqI32(EDGE(5, 50), 0x00010001, "  top-left corner sets top and left");
    CheckEqI32(EDGE(230, 250), 0x1000100, "  lower right band");
    CheckEqI32(EDGE(230, 50), 0x00010100, "  top-right corner sets top and right");
    CheckEqI32(EDGE(5, 250), 0x1000001, "  lower left band");
    CheckEqI32(EDGE(100, 150), 0, "  the map itself sets no edge");
    #undef EDGE

    /* The camera drag: the cursor is re-placed from the iso offsets plus the
     * camera scratch, and the halves are taken unless the flag says otherwise. */
    PtrBankReset();
    s = PtrState(0);
    s.bEdgeTestOn = 0;
    s.bNoHalfCursor = 0;
    s.nCursorX = 5;
    s.nCursorY = 5;
    s.nCamScrollX = 30;
    s.nCamScrollY = 25;
    s.bClickCommits = 1;
    s.nUiSlot3 = 0;
    g_camOff.bScratch40 = 0;
    g_camOff.bScratch41 = 0;
    Input_OnPointer(&s, &h, &w, 100, 150, true);
    /* SetCamOffset writes the two scratch bytes on its way out, so the cursor lands
     * on the iso offset plus whatever it computed. */
    CheckEqI32(s.nCursorX, 5 + (i8)g_camOff.bScratch40,
               "  cursor x is the iso offset plus the camera scratch");
    CheckEqI32(s.nCursorY, 19 + (i8)g_camOff.bScratch41,
               "  cursor y is the iso offset plus the camera scratch");
    CheckEqI32(s.nUiSlot3, 8, "  a press with no drag in progress starts one");

    /* A release that moved the cursor marks a scroll. */
    s.bDragScrollA = 1;
    s.nUiSlot4 = 0;
    s.nCursorX = 5;
    s.nCursorY = 5;
    Input_OnPointer(&s, &h, &w, 100, 150, false);
    CheckEqI32(s.nUiSlot4, 200, "  a scroll marks slot 4 with 200");

    /* The multi-target list rows: 207..237 by 17 pixels, 18 apart. */
    PtrBankReset();
    memset(g_aPtrUnits, 0, sizeof(g_aPtrUnits));
    MapCell_Init(&g_ptrCell, 3, 4, 1);
    g_aPtrUnits[0].nMultiTarget = 1;
    g_aPtrUnits[0].multiOrAnim.apMultiTargets[0] = &g_aPtrUnits[1];
    g_aPtrUnits[1].pCell = &g_ptrCell;
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(0);
    s.bEdgeTestOn = 0;
    s.pSelUnit = &g_aPtrUnits[0];
    s.bDeployPanelOpen = 1;
    Input_OnPointer(&s, &h, &w, 210, 290, true);
    CheckEqI32(g_ptr.nFocusCell, 1, "  a list row focuses its target cell");
    Check(g_ptr.pLastCell == &g_ptrCell, "  with the target's own cell");
    CheckEqI32(g_ptr.nSample, 17, "  and plays sample 17");
    memset(&g_ptr, 0, sizeof(g_ptr));
    Input_OnPointer(&s, &h, &w, 210, 200, true);
    CheckEqI32(g_ptr.nFocusCell, 0, "  a row below the list misses");

    /* Widgets: all nineteen, or only the four live ones while multi-targeting. */
    PtrBankReset();
    PtrLive(0, 0, 0, 300, 300);
    PtrLive(2, 0, 0, 300, 300);
    s = PtrState(0);
    s.bEdgeTestOn = 1;
    s.bDeployPanelOpen = 1;
    s.nActionLatch = 0;
    Input_OnPointer(&s, &h, &w, 5, 150, true);
    CheckEqI32((i32)g_bank.aCtl[0].nState, 1, "  the plain screen hits widget 0");

    PtrBankReset();
    PtrLive(0, 0, 0, 300, 300);
    PtrLive(2, 0, 0, 300, 300);
    s.nActionLatch = 1;
    Input_OnPointer(&s, &h, &w, 5, 150, true);
    CheckEqI32((i32)g_bank.aCtl[0].nState, 0, "  multi-target mode skips widget 0");
    CheckEqI32((i32)g_bank.aCtl[2].nState, 1, "  and hits widget 2 instead");

    /* Screen 1: the inventory drag. */
    PtrBankReset();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(1);
    s.bInvPanelOpen = 1;
    s.bInvDragActive = 1;
    Input_OnPointer(&s, &h, &w, 10, 10, true);
    Check(g_ptr.nInvClear == 1 && g_ptr.nInvHandle == 1,
          "  an open panel with a drag drops the item");
    CheckEqI32((i32)s.bInvDragActive, 0, "  and the drag ends");

    memset(&g_ptr, 0, sizeof(g_ptr));
    s.bInvPanelOpen = 0;
    s.bInvDragActive = 0;
    Input_OnPointer(&s, &h, &w, 10, 10, true);
    CheckEqI32((i32)s.bInvDragActive, 1, "  a hit on a closed panel starts a drag");

    memset(&g_ptr, 0, sizeof(g_ptr));
    s.bInvDragActive = 0;
    Input_OnPointer(&s, &h, &w, 10, 10, false);
    Check(g_ptr.nHitTest == 0 && s.bInvDragActive == 0, "  a release with no drag does nothing");

    /* Screen 2: the look-mode drag moves the view. */
    PtrBankReset();
    s = PtrState(2);
    s.anCamWorldX = 100;
    s.anCamWorldY = 80;
    Input_OnPointer(&s, &h, &w, 40, 20, true);
    CheckEqI32(s.anCamDragX, 110, "  a press records the drag start x");
    CheckEqI32(s.anCamDragY, 85, "  and the drag start y");
    Input_OnPointer(&s, &h, &w, 40, 20, false);
    CheckEqI32(s.anCamWorldX, 100, "  a release moves the view back");
    CheckEqI32(s.anCamWorldY, 80, "  in both axes");

    /* Screen 12 owns twenty-nine widgets. */
    PtrBankReset();
    PtrLive(60, 0, 0, 300, 300);
    PtrLive(88, 0, 0, 300, 300);
    s = PtrState(12);
    Input_OnPointer(&s, &h, &w, 10, 10, true);
    CheckEqI32((i32)g_bank.aCtl[60].nState, 1, "  screen 12 hits its first widget");
    CheckEqI32((i32)g_bank.aCtl[88].nState, 1, "  and its last");

    /* Screens 8, 9, 14 and 16..18 answer to nothing. */
    PtrBankReset();
    PtrLive(0, 0, 0, 300, 300);
    s = PtrState(8);
    Input_OnPointer(&s, &h, &w, 10, 10, true);
    CheckEqI32((i32)g_bank.aCtl[0].nState, 0, "  screen 8 answers to nothing");
}

/* Input_OnPointerUp @ 0x148d0. No coordinates reach the original, so the checks pin
 * which controls the release pass reaches and what the screen-3 action pad decides.
 * A widget left in state 1 was not released. */
static bool UpLive(i32 nIndex) { return g_bank.aCtl[nIndex].nState == 1; }

static void TestPointerUp(void)
{
    TactInputHooks h = PtrHooks();
    TactPointerWorld w = PtrWorld();
    TactInputState s;

    printf("Input_OnPointerUp -- the release pass\n");

    /* The prologue clears the edge bytes and sets the scroll flag to 200. */
    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(8);
    s.nUiSlot5 = 0x01010101;
    s.nUiSlot4 = 0;
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32((i32)s.nUiSlot5, 0, "  edge flags clear");
    CheckEqI32(s.nUiSlot4, 200, "  slot 4 becomes 200");

    /* The geoscape branch owns everything. */
    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(0);
    s.bGeoscapeUi = true;
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32(g_ptr.nGeosUp, 1, "  geoscape branch runs");
    Check(UpLive(0), "  no widget is released");

    /* Screens 16 and 18 are answered before the tactical gate. */
    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(16);
    s.bTactFlag = false;
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32(g_ptr.nTitles, 1, "  screen 16 goes titles -> startup");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(18);
    s.bTactFlag = false;
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32(g_ptr.nEndScreen, 1, "  screen 18 shuts the end screen down");

    /* The tactical gate: bTactFlag, and either the scroll gate or the tick hold. */
    PtrBankLiveAll();
    s = PtrState(0);
    s.bTactFlag = false;
    s.bPlayerInputOn = 1;
    Input_OnPointerUp(&s, &h, &w);
    Check(UpLive(0), "  no release without bTactFlag");

    PtrBankLiveAll();
    s = PtrState(0);
    s.bPlayerInputOn = 1;
    s.bQueueActive = true;
    s.bGameTickHold = false;
    Input_OnPointerUp(&s, &h, &w);
    Check(UpLive(0), "  a busy queue blocks it while the tick is not held");

    PtrBankLiveAll();
    s = PtrState(0);
    s.bPlayerInputOn = 1;
    s.bQueueActive = true;
    s.bGameTickHold = true;
    g_bank.aCtl[128].nMode = 3;   /* an inert mode: the release answers 0 */
    w.pLookCtl = &g_bank.aCtl[128];
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(0), "  the tick hold lets it through");

    /* Screen 0: the look control is released first and a firing one stops the pass. */
    PtrBankLiveAll();
    s = PtrState(0);
    s.bPlayerInputOn = 1;
    w.pLookCtl = &g_bank.aCtl[128];
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(128), "  the look control is released");
    Check(UpLive(0) && UpLive(18), "  a firing look control stops the pass");

    PtrBankLiveAll();
    s = PtrState(0);
    s.bPlayerInputOn = 1;
    g_bank.aCtl[128].nMode = 3;
    w.pLookCtl = &g_bank.aCtl[128];
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(0) && !UpLive(18) && UpLive(19), "  no latch releases 0..18");

    PtrBankLiveAll();
    s = PtrState(0);
    s.bPlayerInputOn = 1;
    s.nActionLatch = 3;
    g_bank.aCtl[128].nMode = 3;
    w.pLookCtl = &g_bank.aCtl[128];
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(2) && !UpLive(7) && !UpLive(9) && !UpLive(15),
          "  an armed latch releases only 2, 7, 9, 15");
    Check(UpLive(0) && UpLive(3) && UpLive(18), "  and none of the others");

    PtrBankLiveAll();
    s = PtrState(0);
    s.bPlayerInputOn = 0;
    w.pLookCtl = NULL;
    Input_OnPointerUp(&s, &h, &w);
    Check(UpLive(0), "  the HUD panel owns the pointer when 0x38 is clear");

    /* Screen 1: thirteen widgets, then the drop when a drag is live. */
    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(1);
    s.bInvPanelOpen = 1;
    s.bInvDragActive = 1;
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(19) && !UpLive(31) && UpLive(32), "  screen 1 releases 19..31");
    CheckEqI32(g_ptr.nInvHandle, 0, "  an open panel does not drop");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(1);
    s.bInvPanelOpen = 0;
    s.bInvDragActive = 1;
    Input_OnPointerUp(&s, &h, &w);
    Check(UpLive(19), "  a closed panel skips the release");
    CheckEqI32(g_ptr.nInvHandle, 1, "  the live drag drops the item");
    CheckEqI32((i32)s.bInvDragActive, 0, "  and the drag flag clears");

    /* Screens 2, 4, 5, 7, 12, 15 and the standalone controls. */
    PtrBankLiveAll();
    s = PtrState(2);
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(98) && !UpLive(100) && UpLive(101), "  screen 2 releases 98..100");

    PtrBankLiveAll();
    s = PtrState(5);
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(44) && !UpLive(52) && UpLive(53), "  screen 5 releases 44..52");
    CheckEqI32((i32)s.nUiScreen, 0, "  and screen 5 closes the screen");

    PtrBankLiveAll();
    s = PtrState(4);
    s.bLookCommitFlag = 1;
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(101) && UpLive(102), "  screen 4 with the flag set releases one");

    PtrBankLiveAll();
    s = PtrState(4);
    s.bLookCommitFlag = 0;
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(102) && !UpLive(103) && UpLive(104),
          "  and clears the flag to release 102..103");

    PtrBankLiveAll();
    s = PtrState(7);
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(53) && !UpLive(59) && UpLive(60), "  screen 7 releases 53..59");

    PtrBankLiveAll();
    s = PtrState(12);
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(60) && !UpLive(88) && UpLive(89), "  screen 12 releases 60..88");

    PtrBankLiveAll();
    s = PtrState(15);
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(89) && !UpLive(97) && UpLive(98), "  screen 15 releases 89..97");

    PtrBankLiveAll();
    s = PtrState(13);
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(128) && UpLive(129), "  screen 13 releases the standalone control");

    PtrBankLiveAll();
    s = PtrState(19);
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(129) && UpLive(128), "  screen 19 releases its own");

    PtrBankLiveAll();
    s = PtrState(10);
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(105) && UpLive(106), "  screens 10/11 stop at the first release");

    /* Screens 8, 9 and 14 answer to nothing; 20/21 adopt apUiSlotsA[11]. */
    PtrBankLiveAll();
    s = PtrState(14);
    Input_OnPointerUp(&s, &h, &w);
    Check(UpLive(0) && UpLive(129), "  screen 14 answers to nothing");

    PtrBankLiveAll();
    s = PtrState(20);
    s.nUiSlot11 = 3;
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32((i32)s.nUiScreen, 3, "  screen 20 adopts slot 11");

    /* The status line takes over while Map+0xac is live. */
    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(0);
    s.bPlayerInputOn = 1;
    s.bSlot19Active = 1;
    s.nSlot24Hit = 0;
    Input_OnPointerUp(&s, &h, &w);
    Check(UpLive(0), "  hover column 0 releases nothing");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(0);
    s.bSlot19Active = 1;
    s.nSlot24Hit = 1;
    w.pfnSlot16 = Tr_Slot16;
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32(g_ptr.nModalEnd, 1, "  column 1 ends the modal");
    CheckEqI32(g_ptr.nSlot16, 1, "  then runs the stored action at Map+0xa0");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(0);
    s.bSlot19Active = 1;
    s.nSlot24Hit = 2;
    w.pfnSlot18 = Tr_Slot18;
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32(g_ptr.nSlot18, 1, "  column 2 runs the one at Map+0xa8");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(0);
    s.bSlot19Active = 1;
    s.nSlot24Hit = 7;
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32(g_ptr.nModalEnd, 0, "  an unknown column does nothing");
    w.pfnSlot16 = NULL;
    w.pfnSlot18 = NULL;

    /* Screen 3: the action pad, then the latch is re-derived from the mode. */
    memset(g_aUnit, 0, sizeof(g_aUnit));
    memset(g_aObj, 0, sizeof(g_aObj));
    memset(g_aItem, 0, sizeof(g_aItem));
    g_aUnit[0].uTu = 20;

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(3);
    s.nActionMode = 1;
    s.anActionCost[1] = 10;
    s.pSelUnit = &g_aUnit[0];
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(32) && UpLive(44), "  screen 3 releases 32..43");
    CheckEqI32((i32)s.nActionLatch, 2, "  mode 0..2 latches 2");
    CheckEqI32((i32)s.nUiScreen, 0, "  and the screen closes");
    CheckEqI32(g_ptr.nSetCursor, 1, "  the cursor is re-placed");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(3);
    s.nActionMode = 11;
    s.anActionCost[11] = 10;
    s.pSelUnit = &g_aUnit[0];
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32((i32)s.nActionLatch, 1, "  mode 11 latches 1");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(3);
    s.nActionMode = 12;
    s.nActionLatch = 4;
    s.pSelUnit = &g_aUnit[0];
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32((i32)s.nActionLatch, 0, "  mode 12 clears the latch");
    CheckEqI32(g_ptr.nPushRec, 0, "  and is never cost-checked");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(3);
    s.nActionMode = 3;
    s.anActionCost[3] = 10;
    s.pSelUnit = &g_aUnit[0];
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32((i32)s.nUiScreen, 3, "  mode 3 (throw) keeps the screen open");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(3);
    s.nActionMode = 8;
    s.nActionLatch = 4;
    s.anActionCost[8] = 10;
    s.pSelUnit = &g_aUnit[0];
    ConfirmWeapon(&s, 1, 23);
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32((i32)s.nActionLatch, 4, "  mode 8 with a uSortKey-23 weapon keeps the latch");
    CheckEqI32((i32)s.nUiScreen, 3, "  and mode 8 keeps the screen open");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(3);
    s.nActionMode = 8;
    s.nActionLatch = 4;
    s.anActionCost[8] = 10;
    s.pSelUnit = &g_aUnit[0];
    ConfirmWeapon(&s, 1, 20);
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32((i32)s.nActionLatch, 0, "  mode 8 with any other weapon clears the latch");

    /* An unaffordable mode pushes "not enough time" and switches to mode 12. */
    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(3);
    s.nActionMode = 5;
    s.nActionLatch = 4;
    s.anActionCost[5] = 30;
    s.pSelUnit = &g_aUnit[0];
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32(g_ptr.nPushRec, 1, "  an unaffordable mode pushes a record");
    CheckEqI32(g_ptr.nLastRec, 1, "  the record is 1, not enough time");
    CheckEqI32(s.nActionMode, 12, "  and the mode becomes 12");
    CheckEqI32((i32)s.nActionLatch, 0, "  and the latch clears");
    CheckEqI32((i32)s.nUiScreen, 0, "  and the screen closes");

    /* Screen 17: three row blocks, and mode 0 has its own clearing rule. */
    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(17);
    w.nListMode = 0;
    memset(w.abListHit, 0, sizeof(w.abListHit));
    w.abListHit[2] = 1;
    w.bListRowSticky = false;
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(113) && !UpLive(116) && UpLive(117), "  mode 0 releases 113..116");
    CheckEqI32(g_ptr.nCtlClick, 5, "  four releases plus the row click");
    CheckEqI32((i32)w.abListHit[0], 0, "  rows before the hit one clear");
    CheckEqI32((i32)w.abListHit[1], 0, "  as the scan goes");
    CheckEqI32((i32)w.abListHit[2], 0, "  and the hit row clears when not sticky");
    CheckEqI32((i32)w.abListHit[3], 0, "  rows after it are untouched");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(17);
    w.nListMode = 0;
    memset(w.abListHit, 0, sizeof(w.abListHit));
    w.abListHit[1] = 1;
    w.bListRowSticky = true;
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32((i32)w.abListHit[1], 1, "  a sticky row keeps its mark");
    Check(!UpLive(114), "  and its control is still released");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(17);
    w.nListMode = 0;
    memset(w.abListHit, 0, sizeof(w.abListHit));
    Input_OnPointerUp(&s, &h, &w);
    CheckEqI32(g_ptr.nCtlClick, 4, "  no marked row means no row click");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(17);
    w.nListMode = 1;
    memset(w.abListHit, 0, sizeof(w.abListHit));
    w.abListHit[3] = 1;
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(117) && !UpLive(120) && UpLive(121), "  mode 1 releases 117..120");
    CheckEqI32(g_ptr.nCtlClick, 5, "  and clicks the marked row");
    CheckEqI32((i32)w.abListHit[3], 0, "  mode 1 always clears the marks");

    PtrBankLiveAll();
    memset(&g_ptr, 0, sizeof(g_ptr));
    s = PtrState(17);
    w.nListMode = 2;
    memset(w.abListHit, 0, sizeof(w.abListHit));
    w.abListHit[6] = 1;
    Input_OnPointerUp(&s, &h, &w);
    Check(!UpLive(121) && !UpLive(127), "  mode 2 releases 121..127");
    CheckEqI32(g_ptr.nCtlClick, 8, "  seven releases plus the row click");

    w.nListMode = 0;
}

static i32 g_nPulseUpdate;
static i32 g_nPulseSample;
static i32 g_nClickA;
static i32 g_nClickB;

static void Pulse_Update(void *p) { (void)p; g_nPulseUpdate += 1; }
static void Pulse_Sample(i32 nSample, void *p) { (void)p; g_nPulseSample = nSample; }
static void Pulse_ClickA(void) { g_nClickA += 1; }
static void Pulse_ClickB(void) { g_nClickB += 1; }
static void Pulse_Blit(const PadImage *pPad, void *pUser) { (void)pPad; (void)pUser; }

static void TestPulseTimer(void)
{
    Trace t;
    TactInputState s = MakeState();
    TactInputHooks h = MakeHooks(&t);
    UiControl ctlA, ctlB;
    UiControlHitCtx hit;
    u32 nHitSlot;
    u8 abBank[4];
    bool bEscape = false;
    UiPulseHooks timers;
    UiPulseCtx ctx;
    TactInputWorld world;

    printf("UiPulse_OnTimer\n");
    memset(&t, 0, sizeof(t));
    g_nPulseUpdate = 0;
    g_nPulseSample = 0;
    g_nClickA = 0;
    g_nClickB = 0;

    UiControl_ctor(&ctlA);
    UiControl_ctor(&ctlB);
    ctlA.nMode = 0; ctlA.nState = 1; ctlA.pfnClick = Pulse_ClickA;
    ctlB.nMode = 0; ctlB.nState = 1; ctlB.pfnClick = Pulse_ClickB;
    nHitSlot = 0xDEAD;
    memset(&hit, 0, sizeof(hit));
    hit.pnUiSlot0 = &nHitSlot;
    hit.pfnBlit = Pulse_Blit;
    hit.pfnBlitEx = Pulse_Blit;

    memset(&timers, 0, sizeof(timers));
    timers.pfnPulseUpdate = Pulse_Update;
    timers.pfnPlaySample = Pulse_Sample;

    memset(&ctx, 0, sizeof(ctx));
    ctx.pState = &s;
    ctx.pHooks = &h;
    ctx.pCtlPulseA = &ctlA;
    ctx.pCtlPulseB = &ctlB;
    ctx.pHit = &hit;
    ctx.pbBankHeld = abBank;
    ctx.pTimers = &timers;
    ctx.pbEscapeFlag = &bEscape;
    memset(&world, 0, sizeof(world));
    ctx.pWorld = &world;

    s.bGeoscapeUi = true;
    s.nUiSlot0 = 0;
    UiPulse_OnTimer(&ctx);
    CheckEqI32(g_nPulseUpdate, 1, "  geoscape mode calls Geoscape_PulseUpdate");
    CheckEqI32((i32)s.nUiSlot0, 0, "  and touches nothing else");

    s.bGeoscapeUi = false;
    s.bTactFlag = false;
    UiPulse_OnTimer(&ctx);
    CheckEqI32((i32)s.nUiSlot0, 0, "  no tactical flag, nothing runs");

    /* apUiSlotsA[0] counts up and wraps past 3 on the fourth call. */
    s.bTactFlag = true;
    s.nUiSlot0 = 0;
    s.nUiSlot5 = 0;
    s.nUiSlot3 = 0;
    s.nUiSlot4 = 0;
    abBank[0] = abBank[1] = abBank[2] = abBank[3] = 0;
    UiPulse_OnTimer(&ctx);
    CheckEqI32((i32)s.nUiSlot0, 1, "  slot 0 counts 0 -> 1");
    CheckEqI32(g_nClickA, 0, "  no forced click yet");
    UiPulse_OnTimer(&ctx);
    UiPulse_OnTimer(&ctx);
    CheckEqI32((i32)s.nUiSlot0, 3, "  slot 0 reaches 3");
    UiPulse_OnTimer(&ctx);
    CheckEqI32(g_nClickA, 1, "  the wrap force-releases control A");
    CheckEqI32(g_nClickB, 1, "  and control B");
    CheckEqI32((i32)s.nUiSlot0, 0, "  slot 0 clears after the wrap");

    /* The four bytes of nUiSlot5 repeat as held direction input. */
    s.nCamScrollX = 0;
    s.nCamScrollY = 0;
    s.nUiSlot5 = 0x000000FFu;   /* left */
    UiPulse_OnTimer(&ctx);
    CheckEqI32(s.nCamScrollX, -1, "  held left scrolls the camera");
    s.nUiSlot5 = 0xFF000000u;   /* bottom */
    UiPulse_OnTimer(&ctx);
    CheckEqI32(s.nCamScrollY, 1, "  held bottom scrolls the camera down");
    s.nUiSlot5 = 0;

    /* The bank's four held bytes only repeat on screen 0. */
    s.nUiScreen = 0;
    abBank[0] = 1; abBank[1] = 1; abBank[2] = 1; abBank[3] = 1;
    s.nCamScrollX = 0;
    s.nCamScrollY = 0;
    UiPulse_OnTimer(&ctx);
    CheckEqI32(s.nCamScrollX, 0, "  bank left and right cancel on one tick");
    CheckEqI32(s.nCamScrollY, 0, "  bank up and down cancel on one tick");
    abBank[0] = 1; abBank[1] = 0; abBank[2] = 0; abBank[3] = 0;
    s.nCamScrollY = 0;
    UiPulse_OnTimer(&ctx);
    CheckEqI32(s.nCamScrollY, -1, "  bank held byte 0 is up");
    s.nUiScreen = 1;
    s.nCamScrollY = 0;
    UiPulse_OnTimer(&ctx);
    CheckEqI32(s.nCamScrollY, 0, "  bank held bytes are ignored off screen 0");
    abBank[0] = abBank[1] = abBank[2] = abBank[3] = 0;

    /* slot 3 counts down only while positive, slot 4 stops at 200. */
    s.nUiSlot3 = 5;
    s.nUiSlot4 = 200;
    UiPulse_OnTimer(&ctx);
    CheckEqI32(s.nUiSlot3, 4, "  slot 3 decrements");
    CheckEqI32(s.nUiSlot4, 200, "  slot 4 never exceeds 200");
    s.nUiSlot3 = 0;
    UiPulse_OnTimer(&ctx);
    CheckEqI32(s.nUiSlot3, 0, "  slot 3 does not go negative");

    /* The tick where slot 4 reaches exactly 15 is the long-press action. */
    s.nUiSlot4 = 14;
    s.nUiSlot3 = 0;
    s.bDragScrollB = 1;
    s.nUiScreen = 0;
    g_nPulseSample = 0;
    UiPulse_OnTimer(&ctx);
    CheckEqI32(s.nUiSlot4, 15, "  slot 4 reaches 15");
    CheckEqI32((i32)s.nUiScreen, 15, "  the pen-menu byte opens screen 15");
    CheckEqI32(g_nPulseSample, 15, "  and plays sample 15");

    /* Without that byte it is the secondary key instead: screen 6 clears. */
    s.bDragScrollB = 0;
    s.nUiSlot4 = 14;
    s.nUiScreen = 6;
    UiPulse_OnTimer(&ctx);
    CheckEqI32((i32)s.nUiScreen, 0, "  otherwise the secondary key runs");

    /* Slot 4 keeps counting past 15, so the action fires once per ramp. */
    s.nUiSlot4 = 15;
    s.nUiScreen = 6;
    UiPulse_OnTimer(&ctx);
    CheckEqI32(s.nUiSlot4, 16, "  slot 4 moves past 15");
    CheckEqI32((i32)s.nUiScreen, 6, "  and does not fire again");

    /* NULL context is tolerated. */
    UiPulse_OnTimer(NULL);
    Check(true, "  NULL context is safe");
}

/* Tact_HandleMapClick @ 0x1512c. Reuses the secondary-key grid: with nLayerScratch 1 the
 * cursor cell is g_aGrid[0][2][3] and the selected unit stands at (1,1,0). */
static TactCursor   g_ClickCursor;
static MapCursorPos g_ClickPos;
static StatusStack  g_ClickStack;
static Soldier     *g_pSelected;
static char         g_aClickRank[9][PUFO_RANK_NAME_STRIDE];
static Mc1Rec       g_ClickCeil;
static Mc1Rec       g_ClickFloor;
static Soldier      g_aClickUnit[2];

static bool Click_NotBusy(void *pUser)   { (void)pUser; return false; }
static bool Click_NoRepaint(void *pUser) { (void)pUser; return false; }
static void Click_NoRender(void *pUser)  { (void)pUser; }

static bool Click_Path(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser)
{
    Trace *t = (Trace *)pUser;

    t->nPathCalls += 1;
    t->nPathX = nX;
    t->nPathY = nY;
    t->nPathLayer = nLayer;
    t->nPathFlag = bFlag ? 1 : 0;
    return true;
}

static void ClickWorld(MapCell **ppCursorCell, i32 nLayerScratch)
{
    MapCell *pDoor;
    i32      i;

    SecWorldReset(&pDoor);
    memset(&g_ClickCursor, 0, sizeof(g_ClickCursor));
    memset(&g_ClickStack, 0, sizeof(g_ClickStack));
    memset(g_aClickUnit, 0, sizeof(g_aClickUnit));
    memset(&g_ClickCeil, 0, sizeof(g_ClickCeil));
    memset(&g_ClickFloor, 0, sizeof(g_ClickFloor));
    g_pSelected = NULL;
    for (i = 0; i < 9; ++i) {
        pufo_strcpy(g_aClickRank[i], "Squad");
    }
    g_ClickCursor.nX = 3;
    g_ClickCursor.nY = 2;
    g_ClickCursor.nFlags = (u8)nLayerScratch;
    g_ClickPos.pCursor        = &g_ClickCursor;
    g_ClickPos.geom.nDimX     = 1;
    g_ClickPos.geom.nDimY     = 1;
    g_ClickPos.bBusy          = false;
    g_ClickPos.bSelUnitLarge  = false;
    g_ClickPos.bSuppressLarge = false;
    g_aClickUnit[0].bStatus   = 0;   /* operative, and an occupant */
    g_aClickUnit[0].nUnitType = 0;   /* small */
    g_aClickUnit[0].pCell     = &g_aGrid[nLayerScratch - 1][2][3];
    *ppCursorCell = &g_aGrid[nLayerScratch - 1][2][3];
}

static TactSelectCtx ClickSelect(void)
{
    static const StatusHooks hooks = { Click_NotBusy, Click_NoRepaint, Click_NoRender, NULL };
    TactSelectCtx c;

    memset(&c, 0, sizeof(c));
    c.ppSelUnit     = &g_pSelected;
    c.pCursorPos    = &g_ClickPos;
    c.pfnLookup     = SecLookup;
    c.pStatus       = &g_ClickStack;
    c.pStatusHooks  = &hooks;
    c.panRankLow    = &g_aClickRank[0][0];
    c.panRankOther  = &g_aClickRank[0][0];
    return c;
}

static i32 g_nMeleeX;
static i32 g_nMeleeY;
static i32 g_nMeleeLayer;

static void TestHandleMapClick(void)
{
    Trace          t;
    TactInputHooks h = MakeHooks(&t);
    TactInputState s;
    TactInputWorld w = { &g_SecQueue, SecLookup, NULL };
    MapClickCtx    c;
    UiControl      look;
    MapCell       *pCell;

    printf("Tact_HandleMapClick @ 0x1512c\n");

    #define CLICK_SETUP(nLayer)                                                  \
        do {                                                                     \
            TactSelectCtx sel = ClickSelect();                                   \
            ClickWorld(&pCell, (nLayer));                                        \
            s = SecState();                                                      \
            s.nLayerScratch = (u8)(nLayer);                                      \
            memset(&look, 0, sizeof(look));                                      \
            look.uWidth = 60;                                                    \
            memset(&c, 0, sizeof(c));                                            \
            c.pState = &s;                                                       \
            c.pWorld = &w;                                                       \
            c.pLookCtl = &look;                                                  \
            c.pSelect = &sel;                                                    \
            c.pHooks = &h;                                                       \
            c.pnMeleeCellX = &g_nMeleeX;                                         \
            c.pnMeleeCellY = &g_nMeleeY;                                         \
            c.pnMeleeCellLayer = &g_nMeleeLayer;                                 \
            c.pfnPathToCell = Click_Path;                                        \
            c.pUser = &t;                                                        \
            g_SecUnit.uTu = 40;                                                  \
            for (i32 i = 0; i < 12; ++i) {                                       \
                s.anActionCost[i] = 4;                                           \
                s.anActionEnabled[i] = 7;                                        \
            }                                                                    \
            memset(&t, 0, sizeof(t));                                            \
        } while (0)

    /* 1. The cursor gate and the own-cell gate. */
    CLICK_SETUP(1);
    s.nCursorX = 40;                       /* outside 0 .. 10*dimX-1 */
    Tact_HandleMapClick(&c);
    Check(t.nPathCalls == 0 && g_SecQueue.nQueueLen == 0 && t.nTextCount == 0,
          "an out-of-map cursor does nothing at all");

    CLICK_SETUP(1);
    s.nCursorX = 1;
    s.nCursorY = 1;                        /* the cell the selected unit stands on */
    Tact_HandleMapClick(&c);
    Check(t.nPathCalls == 0 && g_SecQueue.nQueueLen == 0 && t.nTextCount == 0,
          "clicking the selected unit's own cell does nothing");

    /* 2. Latch 1: the throw. FaceToward runs first, so the turn is entry 0. */
    CLICK_SETUP(1);
    s.nActionLatch = PUFO_ACTION_LATCH_THROW;
    s.nActionMode = 11;
    s.anActionCost[11] = 5;
    s.anActionEnabled[11] = 3;
    Tact_HandleMapClick(&c);
    Check(g_SecQueue.nQueueLen == 2, "throw queues a turn plus the throw");
    CheckEqI32(g_SecQueue.aQueue[0].nCmd, TACTCMD_TURN, "  entry 0 is the facing turn");
    CheckEqI32(g_SecQueue.aQueue[1].nCmd, TACTCMD_THROW, "  entry 1 is command 17");
    CheckEqI32(g_SecQueue.aQueue[1].nX, 3, "  aimed at the cursor cell x");
    CheckEqI32(g_SecQueue.aQueue[1].nY, 2, "  cursor cell y");
    CheckEqI32(g_SecQueue.aQueue[1].nZ, 0, "  cursor cell layer");
    CheckEqI32(g_SecQueue.aQueue[1].nTuCost, 5, "  charged anActionCost[11]");
    CheckEqI32(g_SecQueue.aQueue[1].nArg, 3, "  carried anActionEnabled[11]");
    CheckEqI32(g_SecQueue.bQueueActive, 1, "  the queue was kicked");
    CheckEqI32(t.nTextCount, 0, "  no message on the accepted throw");

    /* The callee's own out-of-TU message, which the port moves to the caller. The TU
     * check runs BEFORE the facing turn is queued (0x4B310 gates 0x4B344), so an
     * unaffordable throw queues nothing at all. */
    CLICK_SETUP(1);
    s.nActionLatch = PUFO_ACTION_LATCH_THROW;
    s.nActionMode = 11;
    s.anActionCost[11] = 99;
    g_SecUnit.uTu = 10;
    Tact_HandleMapClick(&c);
    CheckEqI32(g_SecQueue.nQueueLen, 0, "  an unaffordable throw queues not even the turn");
    CheckEqI32(t.nTextCount, 1, "  the out-of-TU message was pushed");
    CheckEqI32(t.anText[0], PUFO_TXT_OUT_OF_TU, "  record 1");

    /* 3. Latch 2: the fire burst. */
    CLICK_SETUP(1);
    g_aObj[1].bAmmoCap = 10;
    g_aItem[1].pObj = &g_aObj[1];
    g_aItem[1].nAmmo = 3;
    s.pSelWeapon = &g_aItem[1];
    s.nActionLatch = PUFO_ACTION_LATCH_BURST;
    s.nActionMode = 1;                     /* snap: one shot */
    s.anActionCost[1] = 6;
    s.anActionEnabled[1] = 9;
    Tact_HandleMapClick(&c);
    Check(g_SecQueue.nQueueLen == 4, "burst queues turn + shot + mark + clear-aim");
    CheckEqI32(g_SecQueue.aQueue[1].nCmd, TACTCMD_FIRE_SHOT, "  the shot entry");
    CheckEqI32(g_SecQueue.aQueue[1].nX, 3, "  aimed at the cursor cell");
    CheckEqI32(g_SecQueue.aQueue[1].nTuCost, 6, "  charged anActionCost[1]");
    CheckEqI32(g_SecQueue.aQueue[1].nArgDword, 9, "  carried anActionEnabled[1]");
    CheckEqI32(g_SecQueue.aQueue[2].nCmd, TACTCMD_FIRE_MARK, "  the paired mark entry");
    CheckEqI32(g_SecQueue.aQueue[3].nCmd, TACTCMD_CLEAR_AIM, "  the aim is cleared after");

    /* 4. Latch 3: multi-target pick. */
    CLICK_SETUP(1);
    s.nActionLatch = PUFO_ACTION_LATCH_TARGETING;
    look.bEnabled = 0;
    look.x = 100;
    Tact_HandleMapClick(&c);
    CheckEqI32(look.bEnabled, 1, "  the first pick enables the look control");
    CheckEqI32(look.x, -60, "  slid left by its own width");
    CheckEqI32(t.nSampleCount, 1, "  sample 14 played");
    CheckEqI32(t.anSamples[0], 14, "  sample 14");
    CheckEqI32(s.nMultiTargetCount, 1, "  one target recorded");
    Check(s.apMultiTargets[0] == pCell, "  the cursor cell was appended");
    CheckEqI32(pCell->bFlags29 & 0x40, 0x40, "  the target mark is set on the cell");
    CheckEqI32(g_SecQueue.nQueueLen, 0, "  this arm does not kick the queue");

    CLICK_SETUP(1);
    s.nActionLatch = PUFO_ACTION_LATCH_TARGETING;
    look.bEnabled = 1;
    s.nMultiTargetCount = PUFO_MULTI_TARGET_MAX;
    Tact_HandleMapClick(&c);
    CheckEqI32(t.nSampleCount, 0, "  an already-enabled control plays nothing");
    CheckEqI32(s.nMultiTargetCount, PUFO_MULTI_TARGET_MAX, "  the ninth target is refused");

    /* 5. Latch 4: the melee confirm. The gate is occupied AND NOT operative AND not
     * large (0x151D8..0x15200: an OPERATIVE occupant is a failure, not a target) -- the
     * paralyzer is used on a unit that is already down, not on a healthy one. */
    CLICK_SETUP(1);
    pCell->pUnit = &g_aClickUnit[0];
    g_aClickUnit[0].bStatus = 1;         /* occupying, but not operative */
    s.nActionLatch = PUFO_ACTION_LATCH_FIRE;
    s.nActionMode = 6;
    Tact_HandleMapClick(&c);
    CheckEqI32(g_nMeleeX, 3, "  melee x is the CURSOR cell");
    CheckEqI32(g_nMeleeY, 2, "  melee y is the cursor cell");
    CheckEqI32(g_nMeleeLayer, 0, "  melee layer is the cursor cell");
    CheckEqI32(g_SecQueue.nQueueLen, 1, "  one command-18 entry");
    CheckEqI32(g_SecQueue.aQueue[0].nCmd, TACTCMD_ACTION18, "  command 18");
    Check(g_SecQueue.aQueue[0].pTarget == &g_aClickUnit[0], "  target is the occupant");
    CheckEqI32(g_SecQueue.aQueue[0].nMode, 6, "  mode 6");
    CheckEqI32(s.nActionLatch, PUFO_ACTION_LATCH_NONE, "  the latch is cleared");
    CheckEqI32(t.nTextCount, 0, "  no refusal message");

    CLICK_SETUP(1);                       /* empty cell -> record 7 */
    s.nActionLatch = PUFO_ACTION_LATCH_FIRE;
    s.nActionMode = 6;
    Tact_HandleMapClick(&c);
    CheckEqI32(t.nTextCount, 1, "  an empty cell refuses");
    CheckEqI32(t.anText[0], PUFO_TXT_NO_ALIEN_SPECIFIED, "  record 7, no alien specified");
    CheckEqI32(g_SecQueue.nQueueLen, 0, "  nothing queued");

    CLICK_SETUP(1);                       /* large occupant -> record 47 */
    pCell->pUnit = &g_aClickUnit[0];
    g_aClickUnit[0].bStatus = 1;          /* not operative, so the large test is reached */
    g_aClickUnit[0].nUnitType = 4;        /* a large type */
    s.nActionLatch = PUFO_ACTION_LATCH_FIRE;
    s.nActionMode = 6;
    Tact_HandleMapClick(&c);
    CheckEqI32(t.nTextCount, 1, "  a large occupant refuses");
    CheckEqI32(t.anText[0], PUFO_TXT_CANNOT_ATTACK_SOLDIER, "  record 47, cannot attack");

    CLICK_SETUP(1);                       /* an OPERATIVE occupant -> record 7 */
    pCell->pUnit = &g_aClickUnit[0];
    g_aClickUnit[0].bStatus = 0;          /* operative: not a valid paralyzer target */
    s.nActionLatch = PUFO_ACTION_LATCH_FIRE;
    s.nActionMode = 6;
    Tact_HandleMapClick(&c);
    CheckEqI32(t.anText[0], PUFO_TXT_NO_ALIEN_SPECIFIED, "  an operative occupant gives record 7");
    CheckEqI32(g_SecQueue.nQueueLen, 0, "  and nothing is queued");

    /* 6. Latch 0: select the live unit under the cursor. */
    CLICK_SETUP(1);
    pCell->pUnit = &g_aClickUnit[0];
    pufo_strcpy(g_aClickUnit[0].szName, "Sheeran");
    s.nActionLatch = PUFO_ACTION_LATCH_NONE;
    s.nActionMode = 6;
    Tact_HandleMapClick(&c);
    Check(g_pSelected == &g_aClickUnit[0], "  the occupant was selected");
    CheckEqI32(s.nActionLatch, PUFO_ACTION_LATCH_NONE, "  the latch was cleared");
    CheckEqI32(t.nPathCalls, 0, "  and no move was attempted");
    CheckEqI32(g_ClickStack.nCount, 1, "  the selection pushed the unit name");

    /* 7. Latch 0: the breakable ceiling one layer down. */
    CLICK_SETUP(2);                       /* cursor cell is g_aGrid[1][2][3] */
    pCell = &g_aGrid[1][2][3];
    g_ClickCeil.iCeilParam = -30;
    g_aGrid[0][2][3].pCeilSpr = &g_ClickCeil;
    g_aGrid[0][2][3].pUnit = &g_aClickUnit[0];
    g_aClickUnit[0].pCell = &g_aGrid[0][2][3];
    pufo_strcpy(g_aClickUnit[0].szName, "Viper");
    s.nActionLatch = PUFO_ACTION_LATCH_NONE;
    Tact_HandleMapClick(&c);
    Check(g_pSelected == &g_aClickUnit[0], "  a unit under a breakable ceiling is selected");
    CheckEqI32(t.nPathCalls, 0, "  no move on that path");

    CLICK_SETUP(2);                       /* -23 exactly is NOT breakable */
    pCell = &g_aGrid[1][2][3];
    g_ClickCeil.iCeilParam = -23;
    g_aGrid[0][2][3].pCeilSpr = &g_ClickCeil;
    g_aGrid[0][2][3].pUnit = &g_aClickUnit[0];
    g_aClickUnit[0].pCell = &g_aGrid[0][2][3];
    g_SecUnit.nUnitType = 11;             /* flag0 set, so no floor descent muddies the layer */
    s.nActionLatch = PUFO_ACTION_LATCH_NONE;
    Tact_HandleMapClick(&c);
    Check(g_pSelected == NULL, "  -23 exactly does not count as breakable");
    CheckEqI32(t.nPathCalls, 1, "  and the click falls through to the move");
    CheckEqI32(t.nPathLayer, 1, "  to the cursor cell's own layer");

    CLICK_SETUP(2);                       /* breakable, but the unit under it is down */
    pCell = &g_aGrid[1][2][3];
    g_ClickCeil.iCeilParam = -40;
    g_aGrid[0][2][3].pCeilSpr = &g_ClickCeil;
    g_aGrid[0][2][3].pUnit = &g_aClickUnit[0];
    g_aClickUnit[0].bStatus = 1;
    g_aClickUnit[0].pCell = &g_aGrid[0][2][3];
    s.nActionLatch = PUFO_ACTION_LATCH_NONE;
    Tact_HandleMapClick(&c);
    Check(g_pSelected == NULL, "  a non-operative unit under the ceiling is not selected");
    CheckEqI32(t.nPathCalls, 0, "  and the click stops there instead of moving");

    /* 8. Latch 0: an inoperative occupant with bUnk148 set stops the click. */
    CLICK_SETUP(1);
    pCell->pUnit = &g_aClickUnit[0];
    g_aClickUnit[0].bStatus = 1;
    g_aClickUnit[0].bUnk148 = 1;
    s.nActionLatch = PUFO_ACTION_LATCH_NONE;
    Tact_HandleMapClick(&c);
    CheckEqI32(t.nPathCalls, 0, "  bUnk148 set stops the click");

    CLICK_SETUP(1);
    pCell->pUnit = &g_aClickUnit[0];
    g_aClickUnit[0].bStatus = 1;
    g_aClickUnit[0].bUnk148 = 0;
    s.nActionLatch = PUFO_ACTION_LATCH_NONE;
    Tact_HandleMapClick(&c);
    CheckEqI32(t.nPathCalls, 1, "  bUnk148 clear continues into the move");
    CheckEqI32(t.nPathFlag, 1, "  always the bFlag = 1 form");

    /* 9. Latch 0: the floor descent for the non-levitating types. */
    CLICK_SETUP(3);                       /* cursor cell is g_aGrid[2][2][3] */
    pCell = &g_aGrid[2][2][3];
    g_SecUnit.nUnitType = 0;              /* flag0 clear -> must land on a floor */
    s.nActionLatch = PUFO_ACTION_LATCH_NONE;
    Tact_HandleMapClick(&c);
    CheckEqI32(t.nPathCalls, 1, "  the walk ran");
    CheckEqI32(t.nPathLayer, 0, "  down to layer 0, the last slot the walk loads");

    CLICK_SETUP(3);
    pCell = &g_aGrid[2][2][3];
    g_ClickFloor.bStrength = 30;
    g_aGrid[1][2][3].pFloorSpr = &g_ClickFloor;   /* the first floor is one layer down */
    g_SecUnit.nUnitType = 0;
    s.nActionLatch = PUFO_ACTION_LATCH_NONE;
    Tact_HandleMapClick(&c);
    CheckEqI32(t.nPathLayer, 1, "  the walk stops at the first cell with a floor sprite");

    CLICK_SETUP(3);
    pCell = &g_aGrid[2][2][3];
    g_SecUnit.nUnitType = 11;             /* flag0 set (floater) -> no walk at all */
    s.nActionLatch = PUFO_ACTION_LATCH_NONE;
    Tact_HandleMapClick(&c);
    CheckEqI32(t.nPathLayer, 2, "  a levitating type stays on the cursor layer");

    #undef CLICK_SETUP
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
    TestOnConfirm();
    TestOnSecondary();
    TestPointer();
    TestPointerUp();
    TestPulseTimer();
    TestHandleMapClick();

    return TestSummary("test_input");
}
