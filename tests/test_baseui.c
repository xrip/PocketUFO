/* test_baseui.c - base-screen scratch-cell helpers. */
#include <stdio.h>
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
    GeoscapeHooks h;

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

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
