/* test_input.c - tactical/geoscape directional input handlers
 * (Input_OnLeft @ 0x116c0, Input_OnRight @ 0x117b0, Input_OnUp @ 0x118b0,
 * Input_OnDown @ 0x119b0, Input_OnAction @ 0x1557c, Input_OnMenuEscape @ 0x162ec). */
#include <stdio.h>
#include <string.h>

#include "game/pufo_input.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_tactstate.h"  /* TactState, TactQueueEntry, TACTCMD_* */
#include "game/pufo_unit.h"   /* the full Soldier type */
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
    s.bMultiTargetArmed = 1;
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
    Check(s.bMultiTargetArmed == 0, "armed confirm clears byte 0x22");
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
    s.bMultiTargetArmed = 1;
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
    s.bMultiTargetArmed = 1;
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
    s.bMultiTargetArmed = 3;
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

    return TestSummary("test_input");
}
