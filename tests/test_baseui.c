/* test_baseui.c - base-screen scratch-cell helpers. */
#include <stdio.h>
#include <stdlib.h> /* free, the nav controls the test releases */
#include <string.h>

#include "game/pufo_baseui.h"
#include "game/pufo_strateg.h" /* StrategList_ctor, StrategItem_AllocAdd */

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

static i32 g_nSamples[4];
static i32 g_nSampleCount;
static i32 g_nRebuild;
static i32 g_nOptionsOpen;
static i32 g_nOptionsOpenArg;
static i32 g_nFundingOpen;

static void RecSample(u32 nSample, void *pUser)
{
    (void)pUser;
    if (g_nSampleCount < 4) {
        g_nSamples[g_nSampleCount] = (i32)nSample;
    }
    g_nSampleCount++;
}

static void RecRebuild(void *pUser) { (void)pUser; g_nRebuild++; }
static void RecOptionsOpen(i32 nArg, void *pUser)
{
    (void)pUser;
    g_nOptionsOpen++;
    g_nOptionsOpenArg = nArg;
}
static void RecFundingOpen(void *pUser) { (void)pUser; g_nFundingOpen++; }

static void TestScratch(void)
{
    ScreenScratch s;

    printf("BaseUi scratch setters\n");
    memset(&s, 0, sizeof(s));
    BaseUi_SetScratch4(&s);
    CheckEqI32(s.nLineAux145, 4, "  scratch byte 4");

    BaseUi_SetScratch3AndUiY(&s);
    CheckEqI32(s.nLineAux145, 3, "  scratch byte 3");
    CheckEqI32(s.nControl0Y, 120, "  control 0 y = 120");
    CheckEqI32(s.nControl1Y, 142, "  control 1 y = 142");

    CheckEqI32(BaseUi_Cache186BE8(&s, 0x1234), 0x1234, "  cache returns the value");
    CheckEqI32(s.nCache186BE8, 0x1234, "  cache stored");

    g_nSampleCount = 0;
    CheckEqI32(BaseUi_PlaySample7Cache160C74(&s, 0x5678, RecSample, NULL), 0x5678,
               "  sample7 cache returns the value");
    CheckEqI32(g_nSampleCount, 1, "  one sample played");
    CheckEqI32(g_nSamples[0], 7, "  sample 7");
    Check(s.bFlag160C80, "  unk_160C80 set");
    CheckEqI32(s.bFlag187080, 0, "  byte_187080 cleared");
    CheckEqI32(s.nCache160C74, 0x5678, "  cache stored");

    /* NULL tolerance. */
    BaseUi_SetScratch4(NULL);
    BaseUi_SetScratch3AndUiY(NULL);
    CheckEqI32(BaseUi_Cache186BE8(NULL, 9), 9, "  NULL cache returns the value");
    CheckEqI32(BaseUi_PlaySample7Cache160C74(NULL, 9, NULL, NULL), 9,
               "  NULL scratch returns the value");
}

static void TestGeoscapeScreens(void)
{
    GeoscapeScreenState st;
    GeoscapeScreenHooks h;

    printf("Geoscape screen openers\n");
    memset(&st, 0, sizeof(st));
    memset(&h, 0, sizeof(h));
    h.pfnPlaySample = RecSample;
    h.pfnOptionsRebuildWithSound = RecRebuild;
    h.pfnOptionsOpenWithSound = RecOptionsOpen;
    h.pfnFundingOpen = RecFundingOpen;

    g_nSampleCount = 0;
    g_nRebuild = 0;
    Ui_OpenScreen18(&st, &h);
    CheckEqI32(g_nSamples[0], 17, "  screen 18 plays sample 17");
    CheckEqI32(st.nGeoscapeScreen, 18, "  screen 18 id");
    CheckEqI32(g_nRebuild, 1, "  screen 18 rebuilds options");

    g_nSampleCount = 0;
    g_nOptionsOpen = 0;
    st.nLineAux119 = 77;
    Ui_CloseScreen18(&st, &h);
    CheckEqI32(g_nSamples[0], 17, "  close 18 plays sample 17");
    CheckEqI32(st.nGeoscapeScreen, 18, "  close 18 also sets id 18");
    CheckEqI32(g_nOptionsOpen, 1, "  close 18 opens options");
    CheckEqI32(g_nOptionsOpenArg, 77, "  options arg is anLineAux[119]");

    g_nSampleCount = 0;
    g_nFundingOpen = 0;
    Ui_OpenFunding(&st, &h);
    CheckEqI32(g_nSamples[0], 17, "  funding plays sample 17");
    CheckEqI32(st.nGeoscapeScreen, 15, "  funding screen id 15");
    CheckEqI32(g_nFundingOpen, 1, "  funding screen opened");
}

static void TestUfopaediaCache(void)
{
    u8 bFlag = 0;
    i32 nCached = -1;

    printf("Ufopaedia_CacheSelRow\n");

    CheckEqI32(Ufopaedia_CacheSelRow(&bFlag, &nCached, 4) ? 1 : 0, 1,
               "the first call caches and raises the flag");
    CheckEqI32(nCached, 4, "the row that was current is the row that gets stored");
    CheckEqI32(bFlag, 1, "and the flag is raised");

    /* Once the flag is up the function does nothing at all - the cache is never refreshed. */
    CheckEqI32(Ufopaedia_CacheSelRow(&bFlag, &nCached, 9) ? 1 : 0, 0,
               "a second call caches nothing");
    CheckEqI32(nCached, 4, "so the first row is what the rest of the ufopaedia code still sees");

    bFlag = 0;
    CheckEqI32(Ufopaedia_CacheSelRow(&bFlag, &nCached, 9) ? 1 : 0, 1,
               "clearing the flag somewhere else re-arms it");
    CheckEqI32(nCached, 9, "and the new row is cached");
}

static void TestOptionsPanelPaging(void)
{
    static UiPanel aPanels[4];
    UiPanel *apPanels[4];

    printf("OptionsScreen_PanelPrev / PanelNext\n");

    for (int i = 0; i < 4; ++i) {
        memset(&aPanels[i], 0, sizeof(aPanels[i]));
        aPanels[i].nUnk3028 = 3;
        aPanels[i].nLineCount = 20;
        aPanels[i].nStyle = 5;
        apPanels[i] = &aPanels[i];
    }

    /* One press moves all four cursors. They are not per-panel controls. */
    OptionsScreen_PanelPrev(apPanels);
    for (int i = 0; i < 4; ++i) {
        CheckEqI32(aPanels[i].nUnk3028, 2, "prev steps every one of the four cursors");
    }

    OptionsScreen_PanelNext(apPanels);
    OptionsScreen_PanelNext(apPanels);
    for (int i = 0; i < 4; ++i) {
        CheckEqI32(aPanels[i].nUnk3028, 4, "next steps them all back");
    }

    /* The next step is bounded by lineCount - style = 15, the prev step by zero. */
    for (int i = 0; i < 40; ++i) {
        OptionsScreen_PanelNext(apPanels);
    }
    CheckEqI32(aPanels[0].nUnk3028, 15, "next stops at lineCount - style");

    for (int i = 0; i < 40; ++i) {
        OptionsScreen_PanelPrev(apPanels);
    }
    CheckEqI32(aPanels[0].nUnk3028, 0, "prev stops at zero");
}

static void TestFundingHelpers(void)
{
    printf("Funding_ScoreToBarLevel and Funding_DeltaToArrow\n");

    CheckEqI32(Funding_ScoreToBarLevel(0, 1), 75000, "score 0 is level 3, one step is 25000");
    CheckEqI32(Funding_ScoreToBarLevel(1, 2), 250000, "score 1 is level 5, two steps is 50000 each");
    CheckEqI32(Funding_ScoreToBarLevel(3, -1), -25000, "score 3 is level 1, and down is negative");
    CheckEqI32(Funding_ScoreToBarLevel(4, 1), 75000,
               "score 4 shares level 3 with score 0, so it costs the same");
    CheckEqI32(Funding_ScoreToBarLevel(5, 1), 0, "a score outside 0..4 has level 0");
    CheckEqI32(Funding_ScoreToBarLevel(1, 0), 0,
               "direction 0 returns 0 and throws the level away");
    CheckEqI32(Funding_ScoreToBarLevel(1, 3), 0, "and so does a direction outside -2..2");

    /* A positive total makes three of the five arrows unreachable: the first test
     * (delta <= total/8 + total/2) already swallows everything up to 500, and the -1 band would
     * need a delta above 500 that is also at or below 100. */
    CheckEqI32(Funding_DeltaToArrow(-600, 800), -2, "a big negative delta is -2");
    CheckEqI32(Funding_DeltaToArrow(0, 800), -2, "even zero is -2 when the total is positive");
    CheckEqI32(Funding_DeltaToArrow(500, 800), -2, "and everything up to 500 is -2");
    CheckEqI32(Funding_DeltaToArrow(501, 800), 2, "the next value jumps straight to 2");

    /* A negative total moves the bands, and all five arrows become reachable. */
    CheckEqI32(Funding_DeltaToArrow(-600, -800), -2, "down a lot");
    CheckEqI32(Funding_DeltaToArrow(-300, -800), -1, "down some");
    CheckEqI32(Funding_DeltaToArrow(50, -800), 0, "flat");
    CheckEqI32(Funding_DeltaToArrow(200, -800), 1, "up some");
    CheckEqI32(Funding_DeltaToArrow(600, -800), 2, "up a lot");
}

static void TestPanelPaging(void)
{
    UiPanel a, b;

    printf("screen list paging\n");
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.nUnk3028 = 3;
    a.nLineCount = 10;
    a.nStyle = 5;
    b.nUnk3028 = 0;
    b.nLineCount = 4;
    b.nStyle = 4;

    BaseFacScreen_PanelPrev(&a);
    CheckEqI32(a.nUnk3028, 2, "prev decrements the scroll cursor");
    BaseFacScreen_PanelNext(&a);
    CheckEqI32(a.nUnk3028, 3, "next increments while the limit is not reached");
    a.nUnk3028 = 5;
    BaseFacScreen_PanelNext(&a);
    CheckEqI32(a.nUnk3028, 5, "next stops at nLineCount - nStyle");

    SoldierScreen_PanelPrev(&a, &b);
    CheckEqI32(a.nUnk3028, 4, "both panels move, in slot order");
    CheckEqI32(b.nUnk3028, 0, "a cursor of zero cannot go below it");

    a.nUnk3028 = 0;
    SoldierScreen_PanelNext(&a, &b);
    CheckEqI32(a.nUnk3028, 1, "the first panel moves");
    CheckEqI32(b.nUnk3028, 0, "nLineCount == nStyle means next never moves");

    BaseFacScreen_PanelPrev(NULL);
    BaseFacScreen_PanelNext(NULL);
    SoldierScreen_PanelPrev(NULL, NULL);
    SoldierScreen_PanelNext(NULL, NULL);
    Check(1, "NULL panels are tolerated");
}

static void TestSelectionCaches(void)
{
    ScreenScratch s;
    i32 nAux147;

    printf("screen selection caches\n");

    memset(&s, 0, sizeof(s));
    nAux147 = -1;

    CheckEqI32(BuySellScreen_SyncSelection(&s, 7), 7,
               "BuySellScreen_SyncSelection returns the +0xBD8 value");
    CheckEqI32(s.nCache186BE8, 7, "and stores it in dword_186BE8");

    /* 0x96ab0 is byte for byte 0x76138, so it lands in the same global. */
    CheckEqI32(Transfer_CacheSelIndex(&s, 3), 3, "Transfer_CacheSelIndex returns it");
    CheckEqI32(s.nCache186BE8, 3, "the transfer screen overwrites the same cache");

    CheckEqI32(ManufScreen_SyncSelection(&nAux147, 11), 11,
               "ManufScreen_SyncSelection returns the +0xBD8 value");
    CheckEqI32(nAux147, 11, "and writes gUiRootPanel.anLineAux[147]");
    CheckEqI32(s.nCache186BE8, 3, "it leaves the other cache alone");

    CheckEqI32(ManufScreen_SyncSelection(NULL, 5), 5,
               "a missing destination still returns");
}

static void TestBaseInfoOk(void)
{
    BaseInfoOkState st;

    printf("BaseInfoScreen_OnOk\n");

    st.nPendingBase = 2;
    st.bPsiPending = 0;
    st.nCurrentBase = 9;

    CheckEqI32(BaseInfoScreen_OnOk(&st), 2, "OK returns the pending base index");
    CheckEqI32(st.bPsiPending, 1, "OK arms the psi screen");
    CheckEqI32(st.nCurrentBase, 2, "and makes it the base the screens act on");

    /* 0x557DC uses LDRSB, so a negative index is not widened to a positive one. */
    st.nPendingBase = -1;
    st.bPsiPending = 0;
    st.nCurrentBase = 4;
    CheckEqI32(BaseInfoScreen_OnOk(&st), -1, "a negative index stays negative");
    CheckEqI32(st.nCurrentBase, -1, "and is copied as a signed byte");

    CheckEqI32(BaseInfoScreen_OnOk(NULL), 0, "NULL is tolerated");

    /* 0x716a8 is the same eight instructions as 0x557c8. */
    st.nPendingBase = 5;
    st.bPsiPending = 0;
    st.nCurrentBase = 1;
    CheckEqI32(PsiScreen_OnOk(&st), 5, "PsiScreen_OnOk behaves identically");
    CheckEqI32(st.bPsiPending, 1, "it arms the psi screen");
    CheckEqI32(st.nCurrentBase, 5, "and moves the same base index");
}

static UiPanel g_aBuySellPanels[2];

static void TestBuySellPaging(void)
{
    printf("buy/sell list paging\n");

    memset(g_aBuySellPanels, 0, sizeof(g_aBuySellPanels));
    g_aBuySellPanels[0].nUnk3028 = 4;   /* unk_161EDC */
    g_aBuySellPanels[0].nLineCount = 9;
    g_aBuySellPanels[0].nStyle = 3;
    g_aBuySellPanels[1].nUnk3028 = 1;   /* unk_161A3C */
    g_aBuySellPanels[1].nLineCount = 6;
    g_aBuySellPanels[1].nStyle = 2;

    BuySellScreen_ListPanelNext(&g_aBuySellPanels[0], &g_aBuySellPanels[1]);
    CheckEqI32(g_aBuySellPanels[0].nUnk3028, 5, "both panels page");
    CheckEqI32(g_aBuySellPanels[1].nUnk3028, 2, "the second one too");

    BuySellScreen_ListPanelPrev(&g_aBuySellPanels[0], &g_aBuySellPanels[1]);
    CheckEqI32(g_aBuySellPanels[0].nUnk3028, 4, "prev moves both back");
    CheckEqI32(g_aBuySellPanels[1].nUnk3028, 1, "in the same slot order");

    g_aBuySellPanels[0].nUnk3028 = 6;
    BuySellScreen_ListPanelNext(&g_aBuySellPanels[0], &g_aBuySellPanels[1]);
    CheckEqI32(g_aBuySellPanels[0].nUnk3028, 6, "next stops at nLineCount - nStyle");

    BuySellScreen_ListPanelPrev(NULL, NULL);
    BuySellScreen_ListPanelNext(NULL, NULL);
    Check(1, "NULL panels are tolerated");
}

static i32 g_nIdAtInit;
static i32 g_nOpenSample;
static i32 g_nOpenSampleArg;
static void *g_pInitSlot;
static i32 g_nCloseCalls;
static i32 g_nCloseSamples;
static i32 g_nFreeMode;
static i32 g_nStartupCalls;

static void OpenSample(u32 nSample, u32 nArg, void *pUser)
{
    (void)pUser;
    g_nOpenSample = (i32)nSample;
    g_nOpenSampleArg = (i32)nArg;
}

static void OpenInit(void *pSlot, void *pUser)
{
    const GeoscapeScreenState *pState = (const GeoscapeScreenState *)pUser;
    g_pInitSlot = pSlot;
    g_nIdAtInit = (pState != NULL) ? pState->nGeoscapeScreen : -1;
}

static void CloseSample(u32 nSample, void *pUser)
{
    (void)pUser;
    g_nCloseSamples += (i32)nSample;
}

static void CloseUi(void *pUser)
{
    (void)pUser;
    ++g_nCloseCalls;
}

static void FreeUi(i32 nMode, void *pUser)
{
    (void)pUser;
    g_nFreeMode = nMode;
}

static i32 StartupUi(void *pUser)
{
    (void)pUser;
    ++g_nStartupCalls;
    return 77;
}

static void TestScreenOpeners(void)
{
    GeoscapeScreenState st;
    ScreenInitHooks hk;
    static i32 nSlot;

    printf("geoscape screen openers\n");

    hk.pfnPlaySample = OpenSample;
    hk.pfnInit = OpenInit;
    hk.pSlot = &nSlot;
    hk.pUser = &st;

    /* Manuf, Transfer and BaseInfo write the id AFTER the Init. */
    for (i32 i = 0; i < 3; ++i) {
        static const i32 aWant[3] = { 9, 10, 3 };
        memset(&st, 0, sizeof(st));
        g_nIdAtInit = -1;
        if (i == 0) {
            ManufScreen_Open(&st, &hk);
        } else if (i == 1) {
            TransferScreen_Open(&st, &hk);
        } else {
            BaseInfoScreen_Open(&st, &hk);
        }
        CheckEqI32(g_nOpenSample, 17, "every opener plays sample 17");
        CheckEqI32(g_nOpenSampleArg, 4, "Sound_Play takes a second argument, 4");
        Check(g_pInitSlot == (void *)&nSlot, "the Init gets the screen slot");
        CheckEqI32(g_nIdAtInit, 0, "the id is still unset while the Init runs");
        CheckEqI32(st.nGeoscapeScreen, aWant[i], "the id is written after the Init");
    }

    /* BuySell and BaseFac write it BEFORE. */
    memset(&st, 0, sizeof(st));
    g_nIdAtInit = -1;
    BuySellScreen_OpenBuy(&st, &hk);
    CheckEqI32(g_nIdAtInit, 11, "the buy opener sets the id before the Init");
    CheckEqI32(st.nGeoscapeScreen, 11, "and the screen id is 11");

    memset(&st, 0, sizeof(st));
    g_nIdAtInit = -1;
    BuySellScreen_OpenSell(&st, &hk);
    CheckEqI32(g_nIdAtInit, 12, "the sell opener sets 12 first");

    memset(&st, 0, sizeof(st));
    g_nIdAtInit = -1;
    BaseFacScreen_Open(&st, &hk);
    CheckEqI32(g_nIdAtInit, 7, "the facility opener sets 7 first");

    memset(&st, 0, sizeof(st));
    ManufScreen_Open(&st, NULL);
    CheckEqI32(st.nGeoscapeScreen, 9, "a missing hook set still records the id");
}

static void TestCloseThunks(void)
{
    GeoscapeCloseHooks hk;

    printf("screen close thunks\n");

    hk.pfnCloseUi = CloseUi;
    hk.pfnPlaySample = CloseSample;
    hk.pUser = NULL;

    g_nCloseCalls = 0;
    g_nCloseSamples = 0;
    BaseInfoScreen_CloseThunk(&hk);
    BaseFacScreen_CloseThunkA(&hk);
    BaseFacScreen_CloseThunk(&hk);
    SoldierScreen_CloseThunk(&hk);
    ManufScreen_CloseThunk(&hk);
    ResearchScreen_CloseThunk(&hk);
    BuySellScreen_CloseThunk(&hk);
    CheckEqI32(g_nCloseCalls, 7, "seven thunks, one tail call each");
    CheckEqI32(g_nCloseSamples, 0, "none of the seven plays a sample");

    g_nCloseCalls = 0;
    g_nCloseSamples = 0;
    NewBaseScreen_CloseThunk(&hk);
    CheckEqI32(g_nCloseCalls, 1, "the new-base thunk closes too");
    CheckEqI32(g_nCloseSamples, 17, "and it is the one that plays sample 17 first");

    BaseInfoScreen_CloseThunk(NULL);
    NewBaseScreen_CloseThunk(NULL);
    CheckEqI32(g_nCloseCalls, 1, "a missing hook set is tolerated");
}

static void TestTitleReturn(void)
{
    ScreenScratch s;
    TitleReturnHooks hk;

    printf("OptionsScreen_ReturnToTitle\n");

    memset(&s, 0, sizeof(s));
    s.nLineAux145 = 4;
    hk.pfnCloseUi = CloseUi;
    hk.pfnFreeGeoscapeUi = FreeUi;
    hk.pfnStartupUi = StartupUi;
    hk.pUser = NULL;

    g_nCloseCalls = 0;
    g_nFreeMode = -1;
    g_nStartupCalls = 0;

    CheckEqI32(OptionsScreen_ReturnToTitle(&s, &hk), 77, "the startup result is returned");
    CheckEqI32(s.nLineAux145, 0, "the panel scratch byte is cleared first");
    CheckEqI32(g_nCloseCalls, 1, "the geoscape screen is closed");
    CheckEqI32(g_nFreeMode, 1, "every geoscape widget is freed with mode 1");
    CheckEqI32(g_nStartupCalls, 1, "the startup UI is re-inited");

    CheckEqI32(OptionsScreen_ReturnToTitle(NULL, NULL), 0, "NULL hooks return zero");
}

static void TestMoreCloseThunks(void)
{
    GeoscapeCloseHooks hk;
    MapClickHooks mk;

    printf("five more close thunks, the map click shim\n");

    hk.pfnCloseUi = CloseUi;
    hk.pfnPlaySample = CloseSample;
    hk.pUser = NULL;

    g_nCloseCalls = 0;
    FundingScreen_CloseThunk(&hk);
    FundingScreen_ExitThunk(&hk);
    Geoscape_CloseUI_ThunkA(&hk);
    Geoscape_CloseUI_ThunkB(&hk);
    Geoscape_CloseUI_ThunkC(&hk);
    CheckEqI32(g_nCloseCalls, 5, "all five reach the same dispatcher");

    mk.pfnMapClick = StartupUi; /* any i32(void*) will do */
    mk.pUser = NULL;
    g_nStartupCalls = 0;
    CheckEqI32(Geoscape_OnMapClick_Thunk(&mk), 77, "the shim forwards the result");
    CheckEqI32(g_nStartupCalls, 1, "and calls the map click once");
    CheckEqI32(Geoscape_OnMapClick_Thunk(NULL), 0, "a missing hook returns zero");
}

static void TestResearchPaging(void)
{
    UiPanel a, b, c;

    printf("research screen paging\n");

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    a.nUnk3028 = 2; a.nLineCount = 8; a.nStyle = 4;
    b.nUnk3028 = 1; b.nLineCount = 5; b.nStyle = 2;
    c.nUnk3028 = 0; c.nLineCount = 6; c.nStyle = 3;

    ResearchScreen_PanelNext(&a, &b, &c);
    CheckEqI32(a.nUnk3028, 3, "all three panels page");
    CheckEqI32(b.nUnk3028, 2, "the middle one too");
    CheckEqI32(c.nUnk3028, 1, "and the last one");

    ResearchScreen_PanelPrev(&a, &b, &c);
    CheckEqI32(a.nUnk3028, 2, "prev moves all three back");
    CheckEqI32(c.nUnk3028, 0, "the last cursor stops at zero");

    ResearchScreen_PanelNext(NULL, NULL, NULL);
    Check(1, "NULL panels are tolerated");
}

static void TestOptionsRadioClear(void)
{
    UiControl aPool[6];
    UiControl *apCtl[6];
    UiControl *pRet;

    printf("options radio group clear\n");

    for (i32 i = 0; i < 6; ++i) {
        memset(&aPool[i], 0, sizeof(aPool[i]));
        aPool[i].nState = (u8)(i + 1);
        apCtl[i] = &aPool[i];
    }

    pRet = OptionsScreen_ClearMusicButtons(apCtl);
    Check(pRet == &aPool[5], "the return value is the sixth widget pointer");
    for (i32 i = 0; i < 6; ++i) {
        CheckEqI32(aPool[i].nState, 0, "every nState byte is cleared");
    }

    for (i32 i = 0; i < 6; ++i) {
        aPool[i].nState = 3;
    }
    pRet = OptionsScreen_ClearSfxButtons(apCtl);
    Check(pRet == &aPool[5], "the sfx group returns its sixth pointer too");
    CheckEqI32(aPool[0].nState, 0, "the sfx group is cleared as well");

    for (i32 i = 0; i < 6; ++i) {
        apCtl[i] = NULL;
    }
    Check(OptionsScreen_ClearMusicButtons(apCtl) == NULL,
          "null widgets are skipped without crashing");
}

static void TestAtexitRegistration(void)
{
    /* The atexit thunk runs AFTER main returns, so the list it clears must outlive the
     * stack frame. A local here is a use-after-scope crash (0xc0000374 under ctest). */
    static PtrArray list;

    printf("StrategList_RegisterAtexit\n");

    StrategList_ctor(&list);
    Check(StrategItem_AllocAdd(&list, 3), "the list holds an item");
    CheckEqI32(StrategList_RegisterAtexit(&list), 0,
               "atexit accepts the clear thunk (the clear itself runs at exit)");
}

static i32 g_nOptSample;
static i32 g_nOptSampleArg;
static i32 g_nOptMode;
static i32 g_nOptInitCalls;

static void OptSample(u32 nSample, u32 nArg, void *pUser)
{
    (void)pUser;
    g_nOptSample = (i32)nSample;
    g_nOptSampleArg = (i32)nArg;
}

static void OptInit(void *pUser) { (void)pUser; ++g_nOptInitCalls; }
static void OptOpen(i32 nMode, void *pUser)
{
    (void)pUser;
    g_nOptMode = nMode;
    ++g_nOptInitCalls;
}

static void TestOptionsSoundWrappers(void)
{
    OptionsScreenHooks hk;

    printf("options screen sound wrappers\n");

    hk.pfnPlaySample = OptSample;
    hk.pfnInit = OptInit;
    hk.pfnOpen = OptOpen;
    hk.pUser = NULL;

    g_nOptSample = 0;
    g_nOptSampleArg = 0;
    g_nOptInitCalls = 0;
    OptionsScreen_RebuildWithSound(&hk);
    CheckEqI32(g_nOptSample, 7, "rebuild plays sample 7");
    CheckEqI32(g_nOptSampleArg, 4, "Sound_Play takes a second argument, 4");
    CheckEqI32(g_nOptInitCalls, 1, "and rebuilds the screen once");

    g_nOptSample = 0;
    g_nOptMode = -1;
    g_nOptInitCalls = 0;
    OptionsScreen_OpenWithSound(999, -1, &hk);
    CheckEqI32(g_nOptSample, 7, "open plays sample 7");
    CheckEqI32(g_nOptSampleArg, 4, "with the same second argument");
    CheckEqI32(g_nOptMode, -1, "the mode reaches OptionsScreen_Open");
    CheckEqI32(g_nOptInitCalls, 1, "the open runs once");

    /* 0x6DD08 keeps R1 and never reads R0, so the first argument cannot matter. */
    g_nOptMode = -1;
    OptionsScreen_OpenWithSound(0, 5, &hk);
    CheckEqI32(g_nOptMode, 5, "the first argument is dropped");

    OptionsScreen_RebuildWithSound(NULL);
    OptionsScreen_OpenWithSound(0, 0, NULL);
    Check(1, "NULL hooks are tolerated");
}

static i32 g_nAtX;
static i32 g_nAtY;
static i32 g_nAtClickCalls;
static i32 g_nAtMode;
static UiControl *g_pAtCtl;

static void AtClick(void)
{
    ++g_nAtClickCalls;
}

static void AtUpdate(i32 nMode)
{
    g_nAtMode = nMode;
}

static void AtInit(void *pSlot, void *pUser)
{
    (void)pSlot;
    (void)pUser;
    ++g_nOptInitCalls;
}

static void TestAtAndHangarOpeners(void)
{
    UiControl ctl;
    GeoscapeScreenState st;
    ScreenInitHooks ih;
    BaseAtScreenOpenHooks ah;

    printf("BaseAtScreen_Open / BasePlaneScreen_Open\n");

    memset(&ctl, 0, sizeof(ctl));
    ctl.uWidth = 40;
    g_pAtCtl = &ctl;
    g_nAtClickCalls = 0;
    g_nAtMode = -1;

    ah.pfnClick = AtClick;
    ah.pfnUpdate = AtUpdate;
    BaseAtScreen_Open(&ctl, 3, &ah);

    g_nAtX = ctl.x;
    g_nAtY = ctl.y;
    CheckEqI32(g_nAtX, 100, "x centres the control on 120: 120 - (40 >> 1)");
    CheckEqI32(g_nAtY, 290, "y is 290, built as 0x120|2");
    CheckEqI32(g_nAtMode, 3, "the mode reaches the update");
    Check(ctl.pfnClick == AtClick, "the click handler is installed at +0x0C");

    ctl.pfnClick();
    CheckEqI32(g_nAtClickCalls, 1, "and it is the control's own handler");

    ctl.uWidth = 41;
    BaseAtScreen_Open(&ctl, 0, &ah);
    CheckEqI32(ctl.x, 100, "an odd width still drops to the same centre");

    BaseAtScreen_Open(NULL, 1, &ah);
    CheckEqI32(g_nAtMode, 1, "a NULL control still runs the update");
    BaseAtScreen_Open(&ctl, 2, NULL);
    Check(ctl.pfnClick == AtClick, "NULL hooks change nothing and call nothing");

    /* BasePlaneScreen_Open @ 0x617F4: id 6 written BEFORE the Init. */
    memset(&st, 0, sizeof(st));
    memset(&ih, 0, sizeof(ih));
    g_nOptSample = 0;
    g_nOptSampleArg = 0;
    g_nOptInitCalls = 0;
    ih.pfnPlaySample = OptSample;
    ih.pfnInit = AtInit;
    BasePlaneScreen_Open(&st, &ih);
    CheckEqI32(g_nOptSample, 17, "the hangar opener plays sample 17");
    CheckEqI32(g_nOptSampleArg, 4, "with the second argument 4");
    CheckEqI32(st.nGeoscapeScreen, 6, "screen id 6 is the hangar");
    CheckEqI32(g_nOptInitCalls, 1, "and the Init runs once");
}

static void TestRefreshPanels(void)
{
    static ResearchTopicLive g_aTopics[8];
    static u16 g_aQty[144];
    static u8 g_aBuf[3][3][50];
    UiPanel pCount, pQty, pProg;
    i32 nRet;

    printf("\nResearchScreen_RefreshPanels @ 0x73008\n");
    memset(g_aTopics, 0, sizeof(g_aTopics));
    memset(g_aQty, 0, sizeof(g_aQty));
    memset(g_aBuf, 0, sizeof(g_aBuf));
    memset(&pCount, 0, sizeof(pCount));
    memset(&pQty, 0, sizeof(pQty));
    memset(&pProg, 0, sizeof(pProg));

    /* Five topics. Topic 0 is active but never walked. Topics 1, 3, 4 are active; 2 is not. */
    g_aTopics[0].bActive = 1;
    g_aTopics[1].bActive = 1; g_aTopics[1].wProgress = 50; g_aTopics[1].wTotal = 200;
    g_aTopics[2].bActive = 0;
    g_aTopics[3].bActive = 1; g_aTopics[3].wProgress = 0;  g_aTopics[3].wTotal = 100;
    g_aTopics[4].bActive = 1; g_aTopics[4].wProgress = 1;  g_aTopics[4].wTotal = 3;
    g_aQty[1] = 12; g_aQty[3] = 0; g_aQty[4] = 7;

    for (i32 i = 0; i < 3; ++i) {
        pCount.apLines[i] = g_aBuf[0][i];
        pQty.apLines[i]   = g_aBuf[1][i];
        pProg.apLines[i]  = g_aBuf[2][i];
    }

    nRet = ResearchScreen_RefreshPanels(g_aTopics, 5, g_aQty, &pCount, &pQty, &pProg);
    CheckEqI32(nRet, 5, "the return is the topic count");
    CheckEqI32(pCount.nLineCount, 3, "three active topics set all three line counts");
    CheckEqI32(pQty.nLineCount, 3, "including the quantity panel");
    CheckEqI32(pProg.nLineCount, 3, "and the progress panel");
    Check(strcmp((char *)pQty.apLines[0], "12") == 0, "topic 1's quantity is itoa'd");
    Check(strcmp((char *)pQty.apLines[1], "") == 0, "a zero quantity gets the empty string");
    Check(strcmp((char *)pQty.apLines[2], "7") == 0, "and the line index skips the inactive topic");
    Check(strcmp((char *)pProg.apLines[0], "25%") == 0, "50/200 renders as 25%");
    Check(strcmp((char *)pProg.apLines[1], "") == 0, "zero progress gets the empty string");
    Check(strcmp((char *)pProg.apLines[2], "33%") == 0, "1/3 truncates to 33%");

    /* A count of 1 walks nothing: topic 0 alone never reaches the loop. */
    memset(&pCount, 0, sizeof(pCount));
    nRet = ResearchScreen_RefreshPanels(g_aTopics, 1, g_aQty, &pCount, NULL, NULL);
    CheckEqI32(nRet, 1, "count 1 returns 1");
    CheckEqI32(pCount.nLineCount, 0, "and no topic is walked");
}

static void TestTopicNeedsItem(void)
{
    static ResearchTopicLive g_aTopics[144];
    static ObjectRecFull g_aObj[1];
    static ObjectRecFull *g_apObj[256];

    printf("\nResearch_TopicNeedsItem @ 0x72c8c\n");
    memset(g_aTopics, 0, sizeof(g_aTopics));
    memset(g_aObj, 0, sizeof(g_aObj));
    memset(g_apObj, 0, sizeof(g_apObj));
    /* Every topic in a gate band asks for object 7; the band test is about the topic index,
     * not the item. Object 7 holds 5 at base 7 and nothing at base 3. */
    for (i32 i = 0; i < 144; ++i) {
        g_aTopics[i].nItemId = 7;
    }
    g_apObj[7] = &g_aObj[0];
    g_aObj[0].auPerBaseVal[7] = 5;

    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 54, 7), 0, "topic 54 is outside the bands");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 55, 7), 1, "topic 55 opens the first band");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 65, 7), 1, "topic 65 closes it");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 66, 7), 0, "topic 66 is out again");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 117, 7), 0, "topic 117 is out");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 118, 7), 1, "topic 118 opens the second band");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 135, 7), 0, "topic 135 is excluded by name");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 140, 7), 0, "and topic 140");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 142, 7), 1, "topic 142 still answers");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 143, 7), 0, "topic 143 is out");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 60, 3), 0,
               "the count is read at the selected base (base 3 holds nothing)");
    CheckEqI32(Research_TopicNeedsItem(g_aTopics, g_apObj, 60, 7), 1, "and base 7 holds 5");
}

/* ------------------------------------------------- the soldier cursor movers, the transfer
 * flag, the new-base screen and the nav controls. */

static void TestFindAtBase(void)
{
    static Soldier g_aSold[5];
    static Soldier *g_apSold[5];
    i32 nCursor;

    printf("\nSoldierScreen_FindNextAtBase @ 0x6192c / FindPrevAtBase @ 0x618c4\n");
    for (i32 i = 0; i < 5; ++i) {
        g_apSold[i] = &g_aSold[i];
    }
    g_aSold[0].nKind = 1; /* the base byte at +0xB6 */
    g_aSold[1].nKind = 2;
    g_aSold[2].nKind = 1;
    g_aSold[3].nKind = 2;
    g_aSold[4].nKind = 1;

    nCursor = 0;
    CheckEqI32(SoldierScreen_FindNextAtBase(&nCursor, g_apSold, 5, 2), 1, "next from 0 finds 1");
    CheckEqI32(nCursor, 1, "  cursor moved to 1");
    CheckEqI32(SoldierScreen_FindNextAtBase(&nCursor, g_apSold, 5, 2), 3, "next from 1 skips base 1");
    CheckEqI32(nCursor, 3, "  cursor moved to 3");
    CheckEqI32(SoldierScreen_FindNextAtBase(&nCursor, g_apSold, 5, 2), 5, "no match past 3 returns the count");
    CheckEqI32(nCursor, 3, "  cursor untouched on failure");
    nCursor = 4;
    CheckEqI32(SoldierScreen_FindNextAtBase(&nCursor, g_apSold, 5, 2), 5, "cursor + 1 == count returns early");
    CheckEqI32(nCursor, 4, "  cursor untouched");

    nCursor = 3;
    CheckEqI32(SoldierScreen_FindPrevAtBase(&nCursor, g_apSold, 2), 1, "prev from 3 finds 1");
    CheckEqI32(nCursor, 1, "  cursor moved to 1");
    CheckEqI32(SoldierScreen_FindPrevAtBase(&nCursor, g_apSold, 2), -1, "prev from 1 runs off the bottom");
    CheckEqI32(nCursor, 1, "  cursor untouched");

    /* LDRB (0..255) against LDRSB (-128..127): a negative selected base matches nothing. */
    nCursor = 0;
    CheckEqI32(SoldierScreen_FindNextAtBase(&nCursor, g_apSold, 5, -1), 5, "negative base matches nothing");
    CheckEqI32(SoldierScreen_FindPrevAtBase(&nCursor, g_apSold, -1), -1, "  nor backwards");

    nCursor = 0;
    CheckEqI32(SoldierScreen_FindNextAtBase(&nCursor, g_apSold, 0, 1), 1, "empty roster: 1 >= 0 returns 1");
}

static void TestTransferFlag(void)
{
    u8 bFlag = 0;

    printf("\nUi_SetTransferFlag @ 0x619a8\n");
    g_nOpenSample = 0;
    g_nOpenSampleArg = 0;
    Ui_SetTransferFlag(&bFlag, OpenSample, NULL);
    Check(bFlag == 1, "byte_159E10 raised");
    CheckEqI32(g_nOpenSample, 17, "sample 17");
    CheckEqI32(g_nOpenSampleArg, 4, "  second argument 4");
}

static i32 g_nMapClickCalls;
static i32 g_nMapClickResult;

static i32 RecMapClick(void *pUser)
{
    (void)pUser;
    g_nMapClickCalls++;
    return g_nMapClickResult;
}

static void ThunkTryConfirm(void) {}
static void ThunkClose(void) {}

static void TestNewBaseScreen(void)
{
    static SmapView view;
    static SmapZoomSet aSets[3];
    static SmapWorldSize world;
    UiControl frame, confirm;
    GeoscapeScreenState state;
    i8 bBaseCount;
    u8 bFlag187080;
    i32 nFunds;
    u8 bFlag1870C8;
    i32 nFlag1870B4;
    NewBaseOpenEnv openEnv;
    NewBaseConfirmEnv confEnv;

    printf("\nNewBaseScreen_Open @ 0x615d0 / NewBaseScreen_TryConfirm @ 0x6152c\n");

    memset(&openEnv, 0, sizeof(openEnv));
    openEnv.pfnPlaySample = OpenSample;
    openEnv.pCtlFrame = &frame;
    openEnv.pCtlConfirm = &confirm;
    openEnv.pView = &view;
    openEnv.pZoomSets = aSets;
    openEnv.pWorldOut = &world;
    openEnv.pbBaseCount = &bBaseCount;
    openEnv.pbFlag187080 = &bFlag187080;
    openEnv.pfnTryConfirm = ThunkTryConfirm;
    openEnv.pfnCloseThunk = ThunkClose;

    UiControl_ctor(&frame);
    UiControl_ctor(&confirm);
    memset(&view, 0, sizeof(view));
    view.nMode = 2;
    aSets[0].uWorldW = 800; aSets[0].uWorldH = 400;
    aSets[1].uWorldW = 600; aSets[1].uWorldH = 300;
    aSets[2].uWorldW = 472; aSets[2].uWorldH = 236;
    bBaseCount = 3;
    bFlag187080 = 1;
    state.nGeoscapeScreen = 9;
    g_nOpenSample = 0;

    NewBaseScreen_Open(&state, &openEnv);
    CheckEqI32(g_nOpenSample, 17, "open plays sample 17");
    CheckEqI32(frame.x, 224, "frame x = 224 (STRH)");
    CheckEqI32(frame.y, 1, "frame y = 1 (STRH)");
    CheckEqI32(state.nGeoscapeScreen, 2, "screen id 2 at 0x186BC4");
    Check(confirm.pfnClick == ThunkTryConfirm, "confirm control wired to TryConfirm");
    Check(frame.pfnClick == ThunkClose, "frame control wired to the close thunk");
    CheckEqI32(view.nMode, 0, "two zoom steps take 2 -> 1 -> 0");
    CheckEqI32((i32)world.uW, 800, "level 0 world width published");
    CheckEqI32((i32)world.uH, 400, "level 0 world height published");
    CheckEqI32(bFlag187080, 0, "byte_187080 cleared");

    /* Eight bases: the sample plays, then nothing else happens (0x615F4 BEQ). */
    UiControl_ctor(&frame);
    UiControl_ctor(&confirm);
    view.nMode = 2;
    bBaseCount = 8;
    bFlag187080 = 1;
    state.nGeoscapeScreen = 9;
    g_nOpenSample = 0;
    NewBaseScreen_Open(&state, &openEnv);
    CheckEqI32(g_nOpenSample, 17, "the sample plays before the count test");
    CheckEqI32(frame.x, 0, "frame untouched");
    CheckEqI32(state.nGeoscapeScreen, 9, "screen id untouched");
    CheckEqI32(view.nMode, 2, "no zoom step");
    CheckEqI32(bFlag187080, 1, "byte_187080 untouched");

    memset(&confEnv, 0, sizeof(confEnv));
    confEnv.pfnPlaySample = OpenSample;
    confEnv.pfnMapClick = RecMapClick;
    confEnv.pnFunds = &nFunds;
    confEnv.pbFlag1870C8 = &bFlag1870C8;
    confEnv.pnFlag1870B4 = &nFlag1870B4;

    /* Pending placement (0x1870AC != 0) skips the funds test entirely. */
    nFunds = 0;
    confEnv.bPendingKind = 128 + 2; /* cost 1,000,000 */
    bFlag1870C8 = 0;
    nFlag1870B4 = 5;
    g_nMapClickCalls = 0;
    g_nMapClickResult = 77;
    CheckEqI32(NewBaseScreen_TryConfirm(1, &confEnv), 77, "pending placement goes straight to OnMapClick");
    CheckEqI32(g_nMapClickCalls, 1, "  OnMapClick reached despite zero funds");
    Check(bFlag1870C8 == 0 && nFlag1870B4 == 5, "  failure flags untouched");

    /* funds - cost == 0 passes (SUBS is not MI). */
    nFunds = 1000000;
    confEnv.bPendingKind = 128 + 2;
    g_nMapClickCalls = 0;
    CheckEqI32(NewBaseScreen_TryConfirm(0, &confEnv), 77, "exact funds pass");
    CheckEqI32(g_nMapClickCalls, 1, "  OnMapClick reached");

    /* One short fails: 0x1870C8 = 1, dword_1870B4 = 0, return 0. */
    nFunds = 999999;
    confEnv.bPendingKind = 128 + 2;
    bFlag1870C8 = 0;
    nFlag1870B4 = 5;
    g_nMapClickCalls = 0;
    CheckEqI32(NewBaseScreen_TryConfirm(0, &confEnv), 0, "shortfall returns dword_1870B4");
    CheckEqI32(g_nMapClickCalls, 0, "  OnMapClick not reached");
    Check(bFlag1870C8 == 1, "  0x1870C8 raised");
    CheckEqI32(nFlag1870B4, 0, "  dword_1870B4 cleared");

    /* The table boundary: kind 0 costs 800000, kind 9 costs 0, 0xFF costs nothing (deviation). */
    nFunds = 799999;
    confEnv.bPendingKind = 128;
    g_nMapClickCalls = 0;
    CheckEqI32(NewBaseScreen_TryConfirm(0, &confEnv), 0, "kind 0 costs 800000");
    nFunds = 0;
    confEnv.bPendingKind = 128 + 9;
    g_nMapClickCalls = 0;
    CheckEqI32(NewBaseScreen_TryConfirm(0, &confEnv), 77, "kind 9 costs 0");
    confEnv.bPendingKind = 0xFF;
    g_nMapClickCalls = 0;
    CheckEqI32(NewBaseScreen_TryConfirm(0, &confEnv), 77, "the 0xFF sentinel costs nothing");
}

static i32 g_nNavClick;
static void NavClick0(void) { g_nNavClick = 0; }
static void NavClick1(void) { g_nNavClick = 1; }
static void NavClick2(void) { g_nNavClick = 2; }
static void NavClick3(void) { g_nNavClick = 3; }
static void NavClick4(void) { g_nNavClick = 4; }
static void NavClick5(void) { g_nNavClick = 5; }
static void NavClick6(void) { g_nNavClick = 6; }
static void NavClick7(void) { g_nNavClick = 7; }
static void NavClick8(void) { g_nNavClick = 8; }
static void NavClick9(void) { g_nNavClick = 9; }
static void NavClick10(void) { g_nNavClick = 10; }
static void NavClick11(void) { g_nNavClick = 11; }
static void NavClick12(void) { g_nNavClick = 12; }
static void NavClick13(void) { g_nNavClick = 13; }
static void NavClick14(void) { g_nNavClick = 14; }

static i32 g_nPadLoads;

static bool NavLoadPads(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2,
                        void *pUser)
{
    (void)pCtl;
    (void)psz0;
    (void)psz1;
    (void)psz2;
    (void)pUser;
    g_nPadLoads++;
    return true;
}

static void TestBuildNavControls(void)
{
    static const i16 aNavPos[PUFO_NAV_CTLS][2] = {
        { 1, 229 }, { 1, 247 }, { 1, 265 }, { 1, 283 }, { 1, 301 },
        { 120, 229 }, { 120, 247 }, { 120, 265 }, { 120, 283 }, { 120, 301 },
    };
    static const i16 aPanelPos[PUFO_NAV_PANEL_CTLS][2] = { { 210, 20 }, { 210, 40 } };
    static const i16 aFindPos[PUFO_NAV_FIND_CTLS][2] = { { 35, 37 }, { 3, 37 }, { 70, 37 } };
    NavControls ctl;
    NavCtlEnv env;
    i32 i;

    printf("\nGeoscape_BuildNavControls @ 0x67b1c\n");

    memset(&ctl, 0, sizeof(ctl));
    memset(&env, 0, sizeof(env));
    env.apfnNavClick[0] = NavClick0;   env.apfnNavClick[1] = NavClick1;
    env.apfnNavClick[2] = NavClick2;   env.apfnNavClick[3] = NavClick3;
    env.apfnNavClick[4] = NavClick4;   env.apfnNavClick[5] = NavClick5;
    env.apfnNavClick[6] = NavClick6;   env.apfnNavClick[7] = NavClick7;
    env.apfnNavClick[8] = NavClick8;   env.apfnNavClick[9] = NavClick9;
    env.apfnPanelClick[0] = NavClick10; env.apfnPanelClick[1] = NavClick11;
    env.apfnFindClick[0] = NavClick12;  env.apfnFindClick[1] = NavClick13;
    env.apfnFindClick[2] = NavClick14;
    env.pfnLoadPads = NavLoadPads;
    g_nPadLoads = 0;

    Check(Geoscape_BuildNavControls(&ctl, &env), "all fifteen controls allocated");
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        Check(ctl.apNav[i] != NULL, "nav control allocated");
    }
    Check(ctl.apPanel[0] != NULL && ctl.apPanel[1] != NULL, "panel controls allocated");
    Check(ctl.apFind[0] != NULL && ctl.apFind[1] != NULL && ctl.apFind[2] != NULL,
          "find controls allocated");
    CheckEqI32(g_nPadLoads, PUFO_NAV_CTLS + PUFO_NAV_PANEL_CTLS + PUFO_NAV_FIND_CTLS,
               "UiControl_Setup ran once per control");

    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        Check(ctl.apNav[i]->x == aNavPos[i][0] && ctl.apNav[i]->y == aNavPos[i][1],
              "nav control at its stru_AB4B0 offset");
        Check(ctl.apNav[i]->nMode == 0, "  descriptor mode 0");
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        Check(ctl.apPanel[i]->x == aPanelPos[i][0] && ctl.apPanel[i]->y == aPanelPos[i][1],
              "panel control at its stru_AB5F0 offset");
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        Check(ctl.apFind[i]->x == aFindPos[i][0] && ctl.apFind[i]->y == aFindPos[i][1],
              "find control at its stru_AB630 offset");
    }

    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        g_nNavClick = -1;
        ctl.apNav[i]->pfnClick();
        CheckEqI32(g_nNavClick, i, "nav click handler wired to its slot");
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        g_nNavClick = -1;
        ctl.apPanel[i]->pfnClick();
        CheckEqI32(g_nNavClick, 10 + i, "panel click handler wired to its slot");
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        g_nNavClick = -1;
        ctl.apFind[i]->pfnClick();
        CheckEqI32(g_nNavClick, 12 + i, "find click handler wired to its slot");
    }

    /* The original never frees these; the port hands them over, so the test releases them. */
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        free(ctl.apNav[i]);
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        free(ctl.apPanel[i]);
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        free(ctl.apFind[i]);
    }
    Check(1, "controls released by the caller");
}

/* --- Geoscape_OnRadarListClick @ 0x854c4 --- */

static int g_nRadarSamples;
static int g_nRadarOptionsOpens;
static int g_nRadarOptionMode;

static void RadarPlaySample(i32 nSample, void *pUser)
{
    (void)nSample;
    (void)pUser;
    ++g_nRadarSamples;
}

static void RadarOptPlay(u32 nSample, u32 nArg, void *pUser)
{
    (void)nSample;
    (void)nArg;
    (void)pUser;
}

static void RadarOptInit(void *pUser)
{
    (void)pUser;
}

static void RadarOptOpen(i32 nMode, void *pUser)
{
    (void)pUser;
    ++g_nRadarOptionsOpens;
    g_nRadarOptionMode = nMode;
}

/* One item in the list, the panel row pointing at it, two seeded message lines. */
static void RadarSetup(UiLineList *pLines, UiPanel *pPanel, PtrArray *pList, GameTime *pClock,
                       GeoscapeWorld *pWorld, Ship **apCraft, Ship *aCraft,
                       i32 nType, i32 bActive, i32 nDisplayId, i32 nSiteId)
{
    StrategItem *pItem;

    memset(pLines, 0, sizeof(*pLines));
    pLines->nLineCount = 2;
    pLines->abLineFlags[0] = 7;
    pLines->abLineFlags[1] = 8;
    pLines->anColor[0][0] = 111u;
    pLines->anColor[1][0] = 222u;

    memset(pPanel, 0, sizeof(*pPanel));
    pPanel->nUnk3032 = 3;              /* the clicked row */
    pPanel->anLineAux[3] = 0;          /* its aux column is the item index */

    PtrArray_ctor(pList);
    pItem = (StrategItem *)calloc(1, sizeof(StrategItem));
    pItem->nType = (u8)nType;
    pItem->bActive = (u8)bActive;
    pItem->nDisplayId = nDisplayId;
    pItem->nSiteId = nSiteId;
    PtrArray_Add(pList, pItem);

    memset(pClock, 0, sizeof(*pClock));
    pClock->bPaused = 1;               /* the entry clears it before the dispatch */

    memset(aCraft, 0, sizeof(Ship) * 2);
    aCraft[0].bId = 41;
    aCraft[1].bId = 42;
    apCraft[0] = &aCraft[0];
    apCraft[1] = &aCraft[1];
    memset(pWorld, 0, sizeof(*pWorld));
    pWorld->apCraft = apCraft;
    pWorld->nShipCount = 2;
}

static void RadarFree(PtrArray *pList)
{
    PtrArray_DeleteAll(pList);
}

static void TestRadarListClick(void)
{
    static UiLineList lines;
    static UiPanel panel;
    static PtrArray list;
    static GameTime clock;
    static GeoscapeWorld world;
    static Ship *apCraft[2];
    static Ship aCraft[2];
    RadarClickEnv env;
    OptionsScreenHooks optHooks;
    i32 nScreen, nSelIdx, nShipSel, nStratIdx, nTimer;
    u8 f1870A0, f18708C, f187090, f187098;
    i32 nRet;

    printf("Geoscape_OnRadarListClick @ 0x854c4\n");

    memset(&optHooks, 0, sizeof(optHooks));
    optHooks.pfnPlaySample = RadarOptPlay;
    optHooks.pfnInit = RadarOptInit;
    optHooks.pfnOpen = RadarOptOpen;

#define RADAR_BEGIN(nType, bActive, nDisp, nSite)                                             \
    RadarSetup(&lines, &panel, &list, &clock, &world, apCraft, aCraft, (nType), (bActive),   \
               (nDisp), (nSite));                                                             \
    nScreen = 1;                                                                              \
    nSelIdx = -1;                                                                             \
    nShipSel = -1;                                                                            \
    nStratIdx = -1;                                                                           \
    nTimer = 0;                                                                               \
    f1870A0 = 1;                                                                              \
    f18708C = 0;                                                                              \
    f187090 = 0;                                                                              \
    f187098 = 0;                                                                              \
    g_nRadarSamples = 0;                                                                      \
    g_nRadarOptionsOpens = 0;                                                                 \
    memset(&env, 0, sizeof(env));                                                             \
    env.pLines = &lines;                                                                      \
    env.pStatusPanel = &panel;                                                                \
    env.pStrategList = &list;                                                                 \
    env.pnSelectedStrategIdx = &nSelIdx;                                                      \
    env.pTime = &clock;                                                                       \
    env.pWorld = &world;                                                                      \
    env.pnSelectedShipIdx = &nShipSel;                                                        \
    env.pnStrategIndex = &nStratIdx;                                                          \
    env.pnScreenId = &nScreen;                                                                \
    env.pbFlag1870A0 = &f1870A0;                                                              \
    env.pbFlag18708C = &f18708C;                                                              \
    env.pbFlag187090 = &f187090;                                                              \
    env.pbFlag187098 = &f187098;                                                              \
    env.pnRadarPulseTimer = &nTimer;                                                          \
    env.pOptionsHooks = &optHooks;                                                            \
    env.pfnPlaySample = RadarPlaySample;

    RADAR_BEGIN(1, 1, 42, 50000);
    nRet = Geoscape_OnRadarListClick(&env);
    CheckEqI32(nSelIdx, 0, "  the item index lands at 0x186BE4");
    CheckEqI32(f1870A0, 0, "  byte_18709C[4] cleared");
    CheckEqI32(clock.bPaused, 1, "  type 1 active pauses the clock");
    CheckEqI32(lines.nLineCount, 2, "  drop line 0 then insert at 0 keeps the count");
    CheckEqI32(lines.abLineFlags[0], 10, "  the inserted line carries flag 10");
    CheckEqI32(lines.abLineFlags[1], 8, "  the old line 1 slid back to 1");
    CheckEqI32((i32)lines.anColor[0][0], 0, "  colour slot 0 is the item index");
    CheckEqI32(nRet, 0, "  no radar timer for this case");
    RadarFree(&list);

    RADAR_BEGIN(0, 0, 42, 777);
    (void)Geoscape_OnRadarListClick(&env);
    CheckEqI32(nScreen, 18, "  type 0 inactive sets screen 18");
    CheckEqI32(g_nRadarOptionsOpens, 1, "  OptionsScreen_OpenWithSound ran");
    CheckEqI32(g_nRadarOptionMode, 777, "  with the item's site id as the mode");
    CheckEqI32(lines.nLineCount, 2, "  the message list is untouched");
    CheckEqI32(lines.abLineFlags[0], 7, "  line 0 keeps its flag");
    RadarFree(&list);

    RADAR_BEGIN(1, 0, 42, 50000);
    (void)Geoscape_OnRadarListClick(&env);
    CheckEqI32(nShipSel, 1, "  Ship_FindIndexByStrategType found craft 1 (bId 42)");
    CheckEqI32(nStratIdx, 1, "  0x159E74 mirrors the ship index");
    CheckEqI32(lines.nLineCount, 3, "  the append grew the list");
    CheckEqI32(lines.abLineFlags[2], 11, "  the appended line carries flag 11");
    CheckEqI32((i32)lines.anColor[2][0], 1, "  colour slot 0 is the ship index");
    CheckEqI32(nTimer, 250, "  the radar pulse timer is armed");
    CheckEqI32(g_nRadarSamples, 1, "  the append played sample 7");
    RadarFree(&list);

    RADAR_BEGIN(4, 1, 42, 50000);
    (void)Geoscape_OnRadarListClick(&env);
    CheckEqI32(f18708C, 1, "  flag 0x18708C set");
    CheckEqI32(lines.nLineCount, 2, "  drop then append keeps the count");
    CheckEqI32(lines.abLineFlags[1], 20, "  the appended line carries flag 20");
    CheckEqI32((i32)lines.anColor[1][0], 3, "  colour slot 0 is the panel row index");
    CheckEqI32((i32)lines.anColor[1][1], 0, "  colour slot 1 is the item index");
    CheckEqI32(nTimer, 0, "  this case does not arm the radar timer");
    RadarFree(&list);

    RADAR_BEGIN(3, 1, 42, 50000);
    (void)Geoscape_OnRadarListClick(&env);
    CheckEqI32(f187090, 1, "  flag 0x187090 set");
    CheckEqI32(lines.abLineFlags[1], 19, "  the appended line carries flag 19");
    CheckEqI32(nTimer, 250, "  the radar pulse timer is armed");
    RadarFree(&list);

    RADAR_BEGIN(0, 1, 42, 50000);
    nRet = Geoscape_OnRadarListClick(&env);
    CheckEqI32(nRet, 250, "  the tail case returns 250");
    CheckEqI32(f187098, 1, "  flag 0x187098 set");
    CheckEqI32(lines.abLineFlags[1], 18, "  the appended line carries flag 18");
    CheckEqI32(nTimer, 250, "  the radar pulse timer is armed");
    RadarFree(&list);

    /* The panel's aux value points past the list: the port returns 0 where the original
     * would dereference whatever PtrArray_Get handed back. */
    RADAR_BEGIN(1, 1, 42, 50000);
    panel.anLineAux[3] = 9;
    nRet = Geoscape_OnRadarListClick(&env);
    CheckEqI32(nRet, 0, "  an out-of-range item index returns 0");
    CheckEqI32(clock.bPaused, 0, "  but the entry unpause still happened");
    CheckEqI32(lines.nLineCount, 2, "  and no branch ran");
    RadarFree(&list);

#undef RADAR_BEGIN
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestScratch();
    TestGeoscapeScreens();
    TestUfopaediaCache();
    TestOptionsPanelPaging();
    TestFundingHelpers();
    TestPanelPaging();
    TestSelectionCaches();
    TestBaseInfoOk();
    TestBuySellPaging();
    TestScreenOpeners();
    TestCloseThunks();
    TestTitleReturn();
    TestMoreCloseThunks();
    TestResearchPaging();
    TestOptionsRadioClear();
    TestAtexitRegistration();
    TestOptionsSoundWrappers();
    TestAtAndHangarOpeners();
    TestTopicNeedsItem();
    TestRefreshPanels();
    TestFindAtBase();
    TestTransferFlag();
    TestNewBaseScreen();
    TestBuildNavControls();
    TestRadarListClick();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
