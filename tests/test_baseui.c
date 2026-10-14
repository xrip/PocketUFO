/* test_baseui.c - base-screen scratch-cell helpers. */
#include <stdio.h>
#include <stdlib.h> /* free, the nav controls the test releases */
#include <string.h>

#include "game/pufo_baseui.h"
#include "game/pufo_strateg.h" /* StrategList_ctor, StrategItem_AllocAdd */
#include "game/pufo_boot.h"    /* PUFO_TEXT_REC_STRIDE, for the cost label */
#include "audio/pufo_audio.h"  /* PufoAudioBackend / Sound_SetBackend, the volume seam */

static int g_nFailures;

static void PutStr(char *pszDst, const char *pszSrc)
{
    while (*pszSrc != 0) {
        *pszDst++ = *pszSrc++;
    }
    *pszDst = 0;
}

static void PutTextRec(u8 *pText, i32 nRec, const char *psz)
{
    PutStr((char *)(pText + PUFO_TEXT_REC_STRIDE * nRec), psz);
}

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

static int g_nCloseCalls;
static void CloseUiThunk(void *pUser) { (void)pUser; ++g_nCloseCalls; }

static int g_nRebuildCalls;
static i32 RebuildThunk(void *pUser) { (void)pUser; ++g_nRebuildCalls; return 4242; }

static void TestResearchAllocate(void)
{
    static UiPanel g_Panel;
    static u16 g_aCols[PUFO_RESEARCH_TOPIC_COUNT * PUFO_FAC_BASE_COUNT];
    static u8 g_aGrid[PUFO_GRID_SIZE];
    static ObjectRecFull g_SciRec;
    static ResearchTopicLive g_aTopics[PUFO_RESEARCH_TOPIC_COUNT];
    static i32 g_nRow;
    static i8 g_nBase;
    static u16 g_nEntries;
    ResearchQtyEnv env;
    ResearchCancelEnv cancel;
    ResearchAccumulateEnv accum;

    printf("\nResearchScreen_UpdateProgress @ 0x72d14 / CancelSelectedAndClose @ 0x73380 / "
           "AccumulateAllocated @ 0x74914\n");

    memset(&g_Panel, 0, sizeof(g_Panel));
    memset(g_aCols, 0, sizeof(g_aCols));
    memset(g_aGrid, 0, sizeof(g_aGrid));
    memset(&g_SciRec, 0, sizeof(g_SciRec));
    memset(g_aTopics, 0, sizeof(g_aTopics));

    g_Panel.anLineAux[0] = 3; /* topic 3 */
    g_aGrid[480 + 144 * 0 + 6 * 0 + 0] = 2; /* one free lab cell for base 0: capacity 50 */
    g_SciRec.auPerBaseVal[0] = 6;

    memset(&env, 0, sizeof(env));
    env.pTopicPanel = &g_Panel;
    env.pnTopicRow = &g_nRow;
    env.apResearchCols = g_aCols;
    env.puResearchEntries = &g_nEntries;
    env.pScientistRec = &g_SciRec;
    env.abFacilityGrid = g_aGrid;
    env.pnSelectedBaseIdx = &g_nBase;
    g_nRow = 0;
    g_nBase = 0;
    g_nEntries = 0;

    /* Six iterations, all six gates pass, so the whole spare goes to topic 3. */
    CheckEqI32(ResearchScreen_UpdateProgress(&env), 6, "the return is the scientist total");
    CheckEqI32(g_aCols[3], 6, "topic 3 took all six spare scientists");
    CheckEqI32(g_aCols[2], 0, "no other topic moves");

    /* With the entry count at zero both gates sum an empty prefix, so neither one ever notices
     * what this loop is writing: 55 iterations, 55 stores, over a capacity of 50 and against a
     * total of 6 that is already spent. That is the original's behaviour, not a port artefact. */
    g_SciRec.auPerBaseVal[0] = 55;
    CheckEqI32(ResearchScreen_UpdateProgress(&env), 55, "a zero entry count makes both gates blind");
    CheckEqI32(g_aCols[3], 61, "and the entry takes all 55 more units on top of the 6");
    CheckEqI32(ResearchScreen_UpdateProgress(&env), 55, "again, because the total never changes");
    CheckEqI32(g_aCols[3], 116, "and again 55 more land");
    g_SciRec.auPerBaseVal[0] = 6;
    memset(g_aCols, 0, sizeof(g_aCols));

    /* The lab capacity is the binding limit here: one free lab cell is a capacity of 50, and the
     * gate asks for Base_CountActiveResearch + 1, so the 51st allocation is refused even though
     * five scientists are still spare. The loop still runs its full 55 iterations.
     *
     * The entry count has to cover the topic being written for that to show at all: both gates
     * sum only the first nResearchEntryCount entries, so with a count of 0 the allocation the
     * loop is making is invisible to them and nothing ever binds. */
    g_SciRec.auPerBaseVal[0] = 55;
    g_nEntries = 6;
    memset(g_aCols, 0, sizeof(g_aCols));
    CheckEqI32(ResearchScreen_UpdateProgress(&env), 55, "the loop count is the total, not what landed");
    CheckEqI32(g_aCols[3], 50, "the lab capacity stops it at 50");
    g_nEntries = 0;

    /* A total of zero never reaches the loop. */
    g_SciRec.auPerBaseVal[0] = 0;
    g_nEntries = 2;
    g_aCols[0] = 5;
    CheckEqI32(ResearchScreen_UpdateProgress(&env), 0, "a total of zero returns 0");
    CheckEqI32(g_aCols[3], 50, "and stores nothing");
    g_nEntries = 0;

    /* A positive total with a negative availability runs the loop and stores nothing: gate 1
     * fails on every iteration. */
    g_SciRec.auPerBaseVal[0] = 3;
    g_nEntries = 1;
    g_aCols[0] = 10;
    CheckEqI32(ResearchScreen_UpdateProgress(&env), 3, "the total still decides the iteration count");
    CheckEqI32(g_aCols[3], 50, "but the negative availability stores nothing");
    g_aCols[0] = 0;
    g_nEntries = 0;
    g_SciRec.auPerBaseVal[0] = 6;

    /* Cancel: no gate, and the close runs whatever the store did. */
    memset(&cancel, 0, sizeof(cancel));
    cancel.qty = env;
    cancel.close.pfnCloseUi = CloseUiThunk;
    g_aCols[3] = 7;
    g_nCloseCalls = 0;
    ResearchScreen_CancelSelectedAndClose(&cancel);
    CheckEqI32(g_aCols[3], 0, "cancel zeroes the same entry the buttons use");
    CheckEqI32(g_nCloseCalls, 1, "and tail-calls Geoscape_CloseUI");

    g_aCols[3] = 0;
    g_nCloseCalls = 0;
    ResearchScreen_CancelSelectedAndClose(&cancel);
    CheckEqI32(g_aCols[3], 0, "a zero entry stays zero");
    CheckEqI32(g_nCloseCalls, 1, "and the close still runs");

    /* The port's guard skips the store but must not skip the tail call. */
    g_aCols[3] = 9;
    g_nCloseCalls = 0;
    cancel.qty.pTopicPanel = NULL;
    ResearchScreen_CancelSelectedAndClose(&cancel);
    CheckEqI32(g_aCols[3], 9, "a missing panel skips the store");
    CheckEqI32(g_nCloseCalls, 1, "but not the close");
    cancel.qty.pTopicPanel = &g_Panel;
    g_nCloseCalls = 0;
    ResearchScreen_CancelSelectedAndClose(NULL);
    CheckEqI32(g_nCloseCalls, 0, "a missing environment does nothing at all");

    /* Accumulate: eight bases, the topic index is the column index, so every base feeds the
     * same 144 records. */
    memset(g_aCols, 0, sizeof(g_aCols));
    memset(g_aTopics, 0, sizeof(g_aTopics));
    g_aCols[0] = 1;          /* base 0, topic 0 */
    g_aCols[144] = 2;        /* base 1, topic 0 */
    g_aCols[145] = 0;        /* base 1, topic 1: zero is skipped */
    g_aCols[2 * 144 + 1] = 4; /* base 2, topic 1 */
    g_aCols[7 * 144 + 2] = 5; /* base 7, topic 2 */
    g_aTopics[0].wProgress = 10;
    g_aTopics[1].wProgress = 20;
    g_aTopics[2].wProgress = 0;
    g_aTopics[3].wProgress = 30;
    g_nEntries = 3;

    memset(&accum, 0, sizeof(accum));
    accum.aTopics = g_aTopics;
    accum.apResearchCols = g_aCols;
    accum.puResearchEntries = &g_nEntries;
    accum.pfnRebuild = RebuildThunk;
    g_nRebuildCalls = 0;

    CheckEqI32(ResearchScreen_AccumulateAllocated(&accum), 4242,
               "the function returns the tail-called rebuild");
    CheckEqI32(g_nRebuildCalls, 1, "and rebuilds exactly once");
    CheckEqI32(g_aTopics[0].wProgress, 13, "base 0's 1 and base 1's 2 both land on topic 0");
    CheckEqI32(g_aTopics[1].wProgress, 24, "only base 2's 4 lands on topic 1, the zero is skipped");
    CheckEqI32(g_aTopics[2].wProgress, 5, "base 7 is walked too, so topic 2 gets its 5");
    CheckEqI32(g_aTopics[3].wProgress, 30, "a topic past the entry count is untouched");

    /* The add is a u16 LDRH/ADD/STRH triple, so it wraps. */
    g_aTopics[0].wProgress = 65535;
    CheckEqI32(ResearchScreen_AccumulateAllocated(&accum), 4242, "the second pass runs");
    CheckEqI32(g_aTopics[0].wProgress, 2, "65535 + 1 + 2 wraps to 2 in 16 bits");

    /* A zero entry count walks nothing but still rebuilds. */
    g_nEntries = 0;
    g_nRebuildCalls = 0;
    const u16 wTopic2Before = g_aTopics[2].wProgress;
    const u16 wTopic1Before = g_aTopics[1].wProgress;
    CheckEqI32(ResearchScreen_AccumulateAllocated(&accum), 4242, "a zero count still rebuilds");
    CheckEqI32(g_nRebuildCalls, 1, "once");
    CheckEqI32(g_aTopics[2].wProgress, wTopic2Before, "and changes no topic");
    CheckEqI32(g_aTopics[1].wProgress, wTopic1Before, "not even the one a base had for it");
    g_nEntries = 3;

    /* No rebuild hook means no tail call and no return value. */
    accum.pfnRebuild = NULL;
    CheckEqI32(ResearchScreen_AccumulateAllocated(&accum), 0, "without the hook it returns 0");
    CheckEqI32(ResearchScreen_AccumulateAllocated(NULL), 0, "and a missing environment returns 0");
}

static u32 g_aSampleLog[8];
static int g_nSampleCount;
static void SampleThunk(u32 nSample, void *pUser)
{
    (void)pUser;
    if (g_nSampleCount < (int)(sizeof(g_aSampleLog) / sizeof(g_aSampleLog[0]))) {
        g_aSampleLog[g_nSampleCount] = nSample;
    }
    ++g_nSampleCount;
}

static int g_nStartRebuilds;
static i32 StartRebuildThunk(void *pUser) { (void)pUser; ++g_nStartRebuilds; return 4242; }

static void TestScreenSelect(void)
{
    static UiPanel g_ManufPanel;
    static UiPanel g_TopicPanel, g_QtyPanel, g_ProgPanel;
    static u8 (*g_aLines)[PUFO_PANEL_LINE_BYTES];
    static ResearchTopicLive g_aTopics[PUFO_RESEARCH_TOPIC_COUNT];
    static u16 g_aResCols[PUFO_RESEARCH_TOPIC_COUNT * PUFO_FAC_BASE_COUNT];
    static u16 g_aManufCols[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT];
    static u8 g_aGrid[PUFO_GRID_SIZE];
    static ObjectRecFull g_SciRec;
    static i32 g_nRow, g_nManufRow;
    static i8 g_nBase;
    static u16 g_nEntries;
    static u8 g_bSelected, g_bGate, g_bAnim, g_bTopicSel;
    ManufSelectEnv sel;
    ResearchStartEnv start;

    printf("\nManufScreen_SelectListed @ 0x6e914 / ResearchScreen_StartSelected @ 0x73240\n");

    memset(&g_ManufPanel, 0, sizeof(g_ManufPanel));
    memset(&g_TopicPanel, 0, sizeof(g_TopicPanel));
    memset(&g_QtyPanel, 0, sizeof(g_QtyPanel));
    memset(&g_ProgPanel, 0, sizeof(g_ProgPanel));
    g_aLines = calloc(6, PUFO_PANEL_LINE_BYTES);
    Check(g_aLines != NULL, "line buffers allocated");
    for (i32 i = 0; i < 3; ++i) {
        g_QtyPanel.apLines[i] = g_aLines[i];
        g_ProgPanel.apLines[i] = g_aLines[3 + i];
    }
    memset(g_aTopics, 0, sizeof(g_aTopics));
    memset(g_aResCols, 0, sizeof(g_aResCols));
    memset(g_aManufCols, 0, sizeof(g_aManufCols));
    memset(g_aGrid, 0, sizeof(g_aGrid));
    memset(&g_SciRec, 0, sizeof(g_SciRec));

    /* The row click: cache the cursor, set the flags, clear the gate -- and clear the panel's own
     * cursor with a BYTE store, so the upper 24 bits of the dword survive. */
    memset(&sel, 0, sizeof(sel));
    sel.pProductPanel = &g_ManufPanel;
    sel.pnProductRow = &g_nManufRow;
    sel.pbProductSelected = &g_bSelected;
    sel.pbQtyGate = &g_bGate;
    sel.pbAnimStarted = &g_bAnim;
    sel.pfnPlaySample = SampleThunk;
    g_ManufPanel.nUnk3032 = 5;
    g_bSelected = 0;
    g_bGate = 1;
    g_bAnim = 1;
    g_nSampleCount = 0;

    CheckEqI32(ManufScreen_SelectListed(&sel), 5, "the click returns the cached row");
    CheckEqI32(g_nManufRow, 5, "the panel's nUnk3032 is cached into nManufProductRowCache");
    CheckEqI32(g_ManufPanel.nUnk3032, 0, "and the panel's own cursor is cleared");
    CheckEqI32(g_bSelected, 1, "bManufProductSelected is set");
    CheckEqI32(g_bAnim, 0, "bUiAnimStarted is cleared");
    CheckEqI32(g_bGate, 0, "and so is the quantity gate -- the only write it ever gets");
    CheckEqI32(g_nSampleCount, 1, "one sample played");
    CheckEqI32((i32)g_aSampleLog[0], 7, "sample 7");

    g_ManufPanel.nUnk3032 = 0x123456;
    (void)ManufScreen_SelectListed(&sel);
    CheckEqI32(g_nManufRow, 0x123456, "the cached row is the whole dword");
    CheckEqI32(g_ManufPanel.nUnk3032, 0x123400,
               "but the clear is a byte store: only the low byte of the cursor goes to zero");
    CheckEqI32(ManufScreen_SelectListed(NULL), 0, "a missing environment does nothing");

    /* The research start button. Topic 2 is allocated 3 units for base 0 and is idle. */
    g_TopicPanel.anLineAux[0] = 2;
    g_nRow = 0;
    g_nBase = 0;
    g_nEntries = 3;
    g_aResCols[2] = 3;
    g_aTopics[1].bActive = 1;
    g_aTopics[2].bActive = 1;
    g_aTopics[2].wProgress = 0;
    g_aManufCols[0] = 60; /* a workshop budget of 60/50+1 = 2 */
    g_aGrid[480 + 144 * 0 + 6 * 0 + 0] = 3; /* two workshop cells */
    g_aGrid[480 + 144 * 0 + 6 * 1 + 0] = 3;
    g_aGrid[480 + 144 * 0 + 6 * 3 + 0] = 2; /* one lab cell, past the three workshop cells */
    g_SciRec.auPerBaseVal[0] = 4;
    g_bTopicSel = 1;
    g_nSampleCount = 0;

    memset(&start, 0, sizeof(start));
    start.qty.pTopicPanel = &g_TopicPanel;
    start.qty.pnTopicRow = &g_nRow;
    start.qty.apResearchCols = g_aResCols;
    start.qty.puResearchEntries = &g_nEntries;
    start.qty.pScientistRec = &g_SciRec;
    start.qty.abFacilityGrid = g_aGrid;
    start.qty.pnSelectedBaseIdx = &g_nBase;
    start.aTopics = g_aTopics;
    start.pPanelQty = &g_QtyPanel;
    start.pPanelProgress = &g_ProgPanel;
    start.apManufCols = g_aManufCols;
    start.pbTopicSelected = &g_bTopicSel;
    start.pfnPlaySample = SampleThunk;
    start.pfnRebuild = StartRebuildThunk;
    g_nStartRebuilds = 0;

    CheckEqI32(ResearchScreen_StartSelected(&start), 4242, "the tail-called rebuild is the return");
    CheckEqI32(g_bTopicSel, 0, "the detail flag is cleared first");
    CheckEqI32(g_nSampleCount, 1, "sample 17 played once");
    CheckEqI32((i32)g_aSampleLog[0], 17, "sample 17");
    CheckEqI32(g_aTopics[2].wProgress, 1, "an idle topic with an allocation starts at 1");
    CheckEqI32(g_QtyPanel.nLineCount, 2, "the panels were refreshed: two active topics");
    CheckEqI32(g_ProgPanel.nLineCount, 2, "all three of them");
    CheckEqI32(g_aGrid[444 + 144 * 0 + 6 * 0 + 0], 1, "the workshop pass marked the first cell");
    CheckEqI32(g_aGrid[444 + 144 * 0 + 6 * 1 + 0], 1, "and the second, a budget of 60/50+1 = 2");
    CheckEqI32(g_aGrid[444 + 144 * 0 + 6 * 2 + 0], 0, "the third workshop cell is past the budget");
    CheckEqI32(g_nStartRebuilds, 1, "rebuild ran once");

    /* A topic already running is not restarted, and an unallocated one is not started. */
    g_aTopics[2].wProgress = 5;
    (void)ResearchScreen_StartSelected(&start);
    CheckEqI32(g_aTopics[2].wProgress, 5, "a running topic keeps its progress");
    g_aTopics[2].wProgress = 0;
    g_aResCols[2] = 0;
    (void)ResearchScreen_StartSelected(&start);
    CheckEqI32(g_aTopics[2].wProgress, 0, "an idle topic with no allocation stays idle");
    g_aResCols[2] = 3;

    /* Without the rebuild hook there is no tail call and no return value. */
    start.pfnRebuild = NULL;
    CheckEqI32(ResearchScreen_StartSelected(&start), 0, "no hook, no return");
    CheckEqI32(ResearchScreen_StartSelected(NULL), 0, "and a missing environment does nothing");
    free(g_aLines);
}

static void TestManufQty(void)
{
    static UiPanel g_Panel;
    static u16 g_aCols[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT];
    static i32 g_aProdB[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT];
    static u8 g_aGrid[PUFO_GRID_SIZE];
    static ObjectRecFull g_EngRec;
    static i32 g_nRow;
    static i8 g_nBase;
    static u16 g_nEntries;
    static u8 g_nGate;
    ManufQtyEnv env;

    printf("\nManufScreen_IncSelectedQty @ 0x6dddc / DecSelectedQty @ 0x6e09c / "
           "ReduceSelectedQtyToOne @ 0x6dec0 / DecEngineers @ 0x6e330 / UpdateProgress @ 0x6df6c\n");

    memset(&g_Panel, 0, sizeof(g_Panel));
    memset(g_aCols, 0, sizeof(g_aCols));
    memset(g_aProdB, 0, sizeof(g_aProdB));
    memset(g_aGrid, 0, sizeof(g_aGrid));
    memset(&g_EngRec, 0, sizeof(g_EngRec));

    g_Panel.anLineAux[0] = 7; /* product 7 */
    g_Panel.anLineAux[1] = 199;
    g_aGrid[480 + 144 * 0 + 6 * 1 + 2] = 3; /* one free workshop cell for base 0: capacity 50 */
    g_EngRec.auPerBaseVal[0] = 6;
    g_EngRec.auPerBaseVal[1] = 3;

    memset(&env, 0, sizeof(env));
    env.pProductPanel = &g_Panel;
    env.pnProductRow = &g_nRow;
    env.apManufCols = g_aCols;
    env.apProductB = g_aProdB;
    env.puManufEntries = &g_nEntries;
    env.pEngineerRec = &g_EngRec;
    env.abFacilityGrid = g_aGrid;
    env.pnSelectedBaseIdx = &g_nBase;
    env.pbQtyGate = &g_nGate;
    g_nRow = 0;
    g_nBase = 0;
    g_nEntries = 8;
    g_nGate = 0;

    g_aCols[7] = 3;
    CheckEqI32(ManufScreen_IncSelectedQty(&env), 17156,
               "the increment returns 17156, the column's offset from the product-block anchor");
    CheckEqI32(g_aCols[7], 4, "product 7's quantity went 3 to 4");
    CheckEqI32(g_aCols[6], 0, "the neighbour is untouched");
    CheckEqI32(g_aCols[200 + 7], 0, "and base 1's copy of the same product is untouched");

    /* The gate byte. It is dead in the shipped build -- nothing writes it non-zero -- but the
     * original tests it, so the port does too. */
    g_nGate = 1;
    CheckEqI32(ManufScreen_IncSelectedQty(&env), 1, "a set gate returns the byte itself");
    CheckEqI32(g_aCols[7], 4, "and nothing is stored");
    CheckEqI32(ManufScreen_DecSelectedQty(&env), 1, "the decrement is gated too");
    CheckEqI32(ManufScreen_ReduceSelectedQtyToOne(&env), 1, "so is the clamp");
    CheckEqI32(ManufScreen_DecEngineers(&env), 1, "and the engineer step");
    g_nGate = 0;

    /* The engineer gate: 0 total against 4 allocated is -4, returned verbatim. */
    g_EngRec.auPerBaseVal[0] = 0;
    CheckEqI32(ManufScreen_IncSelectedQty(&env), -4, "no engineers to spare: the negative is returned");
    CheckEqI32(g_aCols[7], 4, "and the quantity is not touched");
    g_EngRec.auPerBaseVal[0] = 6;

    /* The workshop gate: 60 already active needs 61 over a capacity of 50. The engineer total has
     * to clear the first gate for this one to be the one that answers. */
    g_EngRec.auPerBaseVal[0] = 100;
    for (i32 i = 0; i < 8; ++i) {
        g_aCols[i] = 8; /* 64 active */
    }
    CheckEqI32(ManufScreen_IncSelectedQty(&env), 0, "64 active against a capacity of 50 is refused");
    for (i32 i = 0; i < 8; ++i) {
        g_aCols[i] = 0;
    }
    g_EngRec.auPerBaseVal[0] = 6;

    /* DecSelectedQty: no gate beyond non-zero, and 17156 either way. */
    g_aCols[7] = 9;
    CheckEqI32(ManufScreen_DecSelectedQty(&env), 17156, "the decrement returns 17156");
    CheckEqI32(g_aCols[7], 8, "9 to 8");
    g_aCols[7] = 0;
    CheckEqI32(ManufScreen_DecSelectedQty(&env), 17156, "a zero entry still returns 17156");
    CheckEqI32(g_aCols[7], 0, "and stays zero");

    /* ReduceSelectedQtyToOne: the ARM steps it down while above 1, which is a clamp to 1. Zero is
     * not raised. */
    g_aCols[7] = 5;
    CheckEqI32(ManufScreen_ReduceSelectedQtyToOne(&env), 17156, "the clamp returns 17156");
    CheckEqI32(g_aCols[7], 1, "5 lands on 1, not 0");
    g_aCols[7] = 0;
    (void)ManufScreen_ReduceSelectedQtyToOne(&env);
    CheckEqI32(g_aCols[7], 0, "a zero entry is left alone");
    g_aCols[7] = 1;
    (void)ManufScreen_ReduceSelectedQtyToOne(&env);
    CheckEqI32(g_aCols[7], 1, "and an entry already at 1 stays there");

    /* DecEngineers: the assigned engineers may not fall to or below quantity + 1. */
    g_aCols[7] = 3;
    g_aProdB[7] = 10;
    CheckEqI32(ManufScreen_DecEngineers(&env), PUFO_MANUF_ANCHOR,
               "the engineer step returns the product-block anchor, register residue");
    CheckEqI32(g_aProdB[7], 9, "10 assigned over a quantity of 3 drops to 9");
    g_aProdB[7] = 4;
    CheckEqI32(ManufScreen_DecEngineers(&env), PUFO_MANUF_ANCHOR, "again");
    CheckEqI32(g_aProdB[7], 4, "4 assigned is exactly quantity + 1, so it stops there");
    g_aProdB[7] = 0;
    (void)ManufScreen_DecEngineers(&env);
    CheckEqI32(g_aProdB[7], 0, "and a zero assignment never goes negative");

    /* UpdateProgress: six engineers spare, all six land. */
    g_aCols[7] = 0;
    CheckEqI32(ManufScreen_UpdateProgress(&env), 6, "the return is the engineer total");
    CheckEqI32(g_aCols[7], 6, "and the whole spare landed on product 7");

    /* The workshop capacity binding: 55 spare over a capacity of 50 stores 50 and still returns 55. */
    g_EngRec.auPerBaseVal[0] = 55;
    memset(g_aCols, 0, sizeof(g_aCols));
    CheckEqI32(ManufScreen_UpdateProgress(&env), 55, "the loop count is the total");
    CheckEqI32(g_aCols[7], 50, "the workshop capacity stops it at 50");

    /* With the gate set the loop still runs its full count but stores nothing. */
    g_nGate = 1;
    memset(g_aCols, 0, sizeof(g_aCols));
    CheckEqI32(ManufScreen_UpdateProgress(&env), 55, "a gated loop still counts its 55");
    CheckEqI32(g_aCols[7], 0, "and stores nothing");
    g_nGate = 0;
    g_EngRec.auPerBaseVal[0] = 6;
    memset(g_aCols, 0, sizeof(g_aCols));

    /* Row and base selection. */
    g_nRow = 1; /* product 199, the last of base 0's column */
    g_aCols[199] = 1;
    (void)ManufScreen_IncSelectedQty(&env);
    CheckEqI32(g_aCols[199], 2, "row 1 selects product 199");
    CheckEqI32(g_aCols[198], 0, "the entry before it is untouched");

    g_nRow = 0;
    g_nBase = 1;
    g_aGrid[480 + 144 * 1 + 6 * 0 + 0] = 3; /* give base 1 a workshop cell */
    g_aCols[7] = 20;
    g_aCols[200 + 7] = 1;
    (void)ManufScreen_IncSelectedQty(&env);
    CheckEqI32(g_aCols[200 + 7], 2, "base 1's copy of product 7 is the one that moved");
    CheckEqI32(g_aCols[7], 20, "base 0's copy is untouched");
    g_nBase = 0;

    /* The guards the original does not have. */
    CheckEqI32(ManufScreen_IncSelectedQty(NULL), 0, "a missing environment is refused");
    CheckEqI32(ManufScreen_UpdateProgress(NULL), 0, "so is the progress loop");
    {
        ManufQtyEnv bad = env;
        bad.pProductPanel = NULL;
        CheckEqI32(ManufScreen_DecSelectedQty(&bad), 0, "a missing product panel is refused");
        bad = env;
        bad.apManufCols = NULL;
        CheckEqI32(ManufScreen_ReduceSelectedQtyToOne(&bad), 0, "a missing column is refused");
        bad = env;
        bad.apProductB = NULL;
        CheckEqI32(ManufScreen_DecEngineers(&bad), PUFO_MANUF_ANCHOR,
                   "a missing product-B array skips the store but keeps the return");
    }
    g_nRow = PUFO_PANEL_LINES;
    CheckEqI32(ManufScreen_IncSelectedQty(&env), 0, "a row past the panel's rows is refused");
    g_nRow = 0;
    g_Panel.anLineAux[0] = PUFO_PRODUCT_ENTRIES_PER_BASE;
    CheckEqI32(ManufScreen_DecEngineers(&env), 0, "a product number past the 200 is refused");
    g_Panel.anLineAux[0] = -1;
    CheckEqI32(ManufScreen_ReduceSelectedQtyToOne(&env), 0, "and a negative one");
    g_Panel.anLineAux[0] = 7;
    g_nBase = PUFO_FAC_BASE_COUNT;
    CheckEqI32(ManufScreen_UpdateProgress(&env), 0, "a base index past the eight is refused");
    g_nBase = 0;
}

static void TestResearchQty(void)
{
    static UiPanel g_Panel;
    static u16 g_aCols[PUFO_RESEARCH_TOPIC_COUNT * PUFO_FAC_BASE_COUNT];
    static u8 g_aGrid[PUFO_GRID_SIZE];
    static ObjectRecFull g_SciRec;
    static i32 g_nRow;
    static i8 g_nBase;
    static u16 g_nEntries;
    ResearchQtyEnv env;

    printf("\nResearchScreen_IncSelectedQty @ 0x72ea0 / ResearchScreen_DecSelectedQty @ 0x72f70\n");

    memset(&g_Panel, 0, sizeof(g_Panel));
    memset(g_aCols, 0, sizeof(g_aCols));
    memset(g_aGrid, 0, sizeof(g_aGrid));
    memset(&g_SciRec, 0, sizeof(g_SciRec));

    /* The topic panel carries a topic number per row in anLineAux, which is what
     * ResearchScreen_RebuildTopicList fills; the row cache picks one of those rows. */
    g_Panel.anLineAux[0] = 5;
    g_Panel.anLineAux[1] = 7;
    g_Panel.anLineAux[2] = 143;

    /* One free lab cell (facType 2, zero build days) gives base 0 a capacity of 50. */
    g_aGrid[480 + 144 * 0 + 6 * 2 + 3] = 2;
    g_SciRec.auPerBaseVal[0] = 4;
    g_SciRec.auPerBaseVal[1] = 3;

    memset(&env, 0, sizeof(env));
    env.pTopicPanel = &g_Panel;
    env.pnTopicRow = &g_nRow;
    env.apResearchCols = g_aCols;
    env.puResearchEntries = &g_nEntries;
    env.pScientistRec = &g_SciRec;
    env.abFacilityGrid = g_aGrid;
    env.pnSelectedBaseIdx = &g_nBase;

    g_nRow = 1; /* topic 7 */
    g_nBase = 0;
    g_nEntries = 0;

    g_aCols[7] = 3;
    CheckEqI32(ResearchScreen_IncSelectedQty(&env), 4900,
               "the return is 4900, the research column's offset, not a status code");
    CheckEqI32(g_aCols[7], 4, "topic 7's entry went 3 to 4");
    CheckEqI32(g_aCols[6], 0, "the entry below it is untouched");
    CheckEqI32(g_aCols[8], 0, "and the entry above it");
    CheckEqI32(g_aCols[144 + 7], 0, "base 1's copy of the same topic is untouched");

    /* 65535 + 1 becomes 0: the ARM truncates with LSL #16 / LSR #16 at 0x72F50. */
    g_aCols[7] = 65535;
    (void)ResearchScreen_IncSelectedQty(&env);
    CheckEqI32(g_aCols[7], 0, "the store is truncated to 16 bits");

    /* Dec has no gate at all beyond the entry being non-zero. */
    g_aCols[7] = 9;
    CheckEqI32(ResearchScreen_DecSelectedQty(&env), 4900, "the decrement returns 4900 too");
    CheckEqI32(g_aCols[7], 8, "topic 7's entry went 9 to 8");
    g_aCols[7] = 0;
    CheckEqI32(ResearchScreen_DecSelectedQty(&env), 4900,
               "a zero entry still returns 4900");
    CheckEqI32(g_aCols[7], 0, "and stays zero: the ARM has no floor to go below");

    /* Gate 1: the scientist count. With 1 and 2 allocated against a total of 0 the getter
     * returns -3, and Inc returns that count rather than a status. */
    g_SciRec.auPerBaseVal[0] = 0;
    g_nEntries = 2;
    g_aCols[0] = 1;
    g_aCols[1] = 2;
    g_aCols[7] = 5;
    CheckEqI32(ResearchScreen_IncSelectedQty(&env), -3,
               "no scientists left: Inc returns the negative availability it tested");
    CheckEqI32(g_aCols[7], 5, "and the topic entry is not touched");
    g_SciRec.auPerBaseVal[0] = 4;
    g_nEntries = 0;
    g_aCols[0] = 0;
    g_aCols[1] = 0;

    /* Gate 2: the lab capacity. Base 1 has no lab cell, so even with scientists going spare
     * the increment is refused. */
    g_nBase = 1;
    g_nRow = 0; /* topic 5 */
    g_aCols[144 + 5] = 11;
    CheckEqI32(ResearchScreen_IncSelectedQty(&env), 0,
               "no free lab slot: Inc returns the 0 that Base_HasResearchSlotFree left in R0");
    CheckEqI32(g_aCols[144 + 5], 11, "and nothing is stored");

    /* The same gate from the other side. The scientist total has to be high enough to clear
     * gate 1 first, otherwise it is the scientist gate that answers: 100 - 60 allocated leaves
     * 40 spare, and it is the lab capacity that then refuses 61 needed over 50. */
    g_nBase = 0;
    g_nEntries = 6;
    g_SciRec.auPerBaseVal[0] = 100;
    for (i32 i = 0; i < 6; ++i) {
        g_aCols[i] = 10;
    }
    CheckEqI32(ResearchScreen_IncSelectedQty(&env), 0,
               "60 allocated against a capacity of 50 is refused too");
    for (i32 i = 0; i < 6; ++i) {
        g_aCols[i] = 0;
    }
    g_nEntries = 0;
    g_SciRec.auPerBaseVal[0] = 4;

    /* Row and base selection: row 2 is topic 143, the last entry of base 0's column. */
    g_nRow = 2;
    g_aCols[143] = 1;
    (void)ResearchScreen_IncSelectedQty(&env);
    CheckEqI32(g_aCols[143], 2, "row 2 selects topic 143");
    CheckEqI32(g_aCols[142], 0, "the entry before it is untouched");

    /* Base 1's column starts 144 entries in, so the same row must land 144 further on. */
    g_aGrid[480 + 144 * 1] = 2; /* give base 1 a lab cell too */
    g_nRow = 0; /* topic 5 */
    g_nBase = 1;
    g_aCols[5] = 20;
    g_aCols[144 + 5] = 1;
    (void)ResearchScreen_IncSelectedQty(&env);
    CheckEqI32(g_aCols[144 + 5], 2, "base 1's copy of topic 5 is the one that moved");
    CheckEqI32(g_aCols[5], 20, "base 0's copy is untouched");

    /* The guards the original does not have. */
    CheckEqI32(ResearchScreen_IncSelectedQty(NULL), 0, "a missing environment is refused");
    CheckEqI32(ResearchScreen_DecSelectedQty(NULL), 0, "for the decrement too");
    {
        ResearchQtyEnv bad = env;
        bad.pTopicPanel = NULL;
        CheckEqI32(ResearchScreen_IncSelectedQty(&bad), 0, "a missing topic panel is refused");
        CheckEqI32(ResearchScreen_DecSelectedQty(&bad), 0, "and the same for the decrement");
        bad = env;
        bad.apResearchCols = NULL;
        CheckEqI32(ResearchScreen_IncSelectedQty(&bad), 0, "a missing quantity column is refused");
    }
    g_nRow = PUFO_PANEL_LINES;
    CheckEqI32(ResearchScreen_IncSelectedQty(&env), 0, "a row past the panel's rows is refused");
    g_nRow = -1;
    CheckEqI32(ResearchScreen_DecSelectedQty(&env), 0, "and a negative row");
    g_nRow = 0;
    g_Panel.anLineAux[0] = PUFO_RESEARCH_TOPIC_COUNT;
    CheckEqI32(ResearchScreen_IncSelectedQty(&env), 0, "a topic number past the 144 is refused");
    g_Panel.anLineAux[0] = -1;
    CheckEqI32(ResearchScreen_DecSelectedQty(&env), 0, "and a negative one");
    g_Panel.anLineAux[0] = 5;
    g_nBase = PUFO_FAC_BASE_COUNT;
    CheckEqI32(ResearchScreen_IncSelectedQty(&env), 0, "a base index past the eight is refused");
    g_nBase = 1;
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

/* --- BaseFacScreen_Init @ 0x5a198 --- */

static int g_nToggleRuns;
static int g_nPadLoads;
static char g_szLastPad0[16];

static void RecordToggle(void *pUser)
{
    (void)pUser;
    g_nToggleRuns += 1;
}

static bool RecordPads(UiControl *pCtl, const char *psz0, const char *psz1,
                       const char *psz2, void *pUser)
{
    (void)pCtl;
    (void)psz1;
    (void)psz2;
    (void)pUser;
    g_nPadLoads += 1;
    snprintf(g_szLastPad0, sizeof(g_szLastPad0), "%s", psz0);
    return true;
}

static void NopClick(void) { }

static void TestBaseFacScreenInit(void)
{
    static UiControl closeCtl, upCtl, downCtl;
    static i32 n7C, n80;
    static u8 n88;
    BaseFacInitEnv env;
    i32 nRet;

    printf("BaseFacScreen_Init @ 0x5a198\n");

    memset(&closeCtl, 0x5A, sizeof(closeCtl));
    memset(&upCtl, 0x5A, sizeof(upCtl));
    memset(&downCtl, 0x5A, sizeof(downCtl));
    n7C = 7; n80 = 8; n88 = 9;
    g_nToggleRuns = 0;
    g_nPadLoads = 0;
    g_szLastPad0[0] = '\0';

    memset(&env, 0, sizeof(env));
    env.pCloseCtl = &closeCtl;
    env.pUpCtl = &upCtl;
    env.pDownCtl = &downCtl;
    env.pnUnk15887C = &n7C;
    env.pnUnk158880 = &n80;
    env.pbArmed = &n88;
    env.pfnPanelPrev = NopClick;
    env.pfnPanelNext = NopClick;
    env.pfnCloseThunk = NopClick;
    env.pfnLoadPads = RecordPads;
    env.pfnToggle = RecordToggle;

    nRet = BaseFacScreen_Init(&env);
    CheckEqI32(nRet, 0, "the return is 0 where the original returns Toggle's register");
    CheckEqI32(n7C, 0, "0x15887C is cleared before the controls are built");
    CheckEqI32(n80, 0, "and so is 0x158880");
    CheckEqI32((i32)n88, 0, "0x158888 is cleared just before Toggle");
    CheckEqI32(g_nToggleRuns, 1, "BaseFacScreen_Toggle runs once, last");
    CheckEqI32(upCtl.uWidth, 10, "UiControl_ctor runs on the first control (10x10 enabled)");
    CheckEqI32(downCtl.uWidth, 10, "and on the second");
    CheckEqI32(g_nPadLoads, 2, "both get UiControl_Setup, each with its own descriptor");
    Check(strcmp(g_szLastPad0, "down") == 0, "the second descriptor is the down/sdown pair");
    CheckEqI32(upCtl.x, 11, "the up control lands at the descriptor's x offset");
    CheckEqI32(upCtl.y, 252, "and y 252");
    CheckEqI32(downCtl.x, 11, "the down control at the same x");
    CheckEqI32(downCtl.y, 277, "and y 277");
    CheckEqI32(upCtl.nMode, 0, "the descriptor's mode lands on the control");
    Check(upCtl.pfnClick == NopClick, "the +12 handler is stored BEFORE Setup and survives it");
    Check(downCtl.pfnClick == NopClick, "for both controls");
    CheckEqI32(closeCtl.x, 224, "the close control is moved to x 224");
    CheckEqI32(closeCtl.y, 229, "and y 229");
    Check(closeCtl.pfnClick == NopClick, "its click handler is installed at both ends of the init");

    CheckEqI32(BaseFacScreen_Init(NULL), 0, "a null env is refused");
}

/* --- Geoscape_OnMapClick @ 0x8b4a8 --- */

static u8 g_aMapGrid[1524];
#define MFAC(l, y, x) g_aMapGrid[0x174 + 144 * 0 + (l) + 6 * (y) + (x)]
#define MDAYS(y, x)   g_aMapGrid[36 * 0 + 6 * (y) + (x)]

static void TestMapClick(void)
{
    static u8 abGridBytes[8];
    static Ship aCraft[1];
    static Ship *apCraft[1];
    static BaseRec aBase;
    static BaseRec *apBases[1];
    static BaseSiteRec aSiteRecs[1];
    static PtrArray list;
    static StrategItem *pItem;
    static u8 abKind[17];
    static u8 aTexts[PUFO_TEXT_REC_STRIDE * (PUFO_FAC_NAME_REC_BASE + 17)];
    static char szLabel[64];
    static UiControl closeCtl, upCtl, downCtl;
    static i8 nBaseCount, nSelBase;
    HangarPlaceEnv hEnv;
    BaseFacInitEnv facEnv;
    MapClickEnv env;
    i32 nScreen;
    u8 bActive, bFail, bConfirm, bDone;

    printf("Geoscape_OnMapClick @ 0x8b4a8\n");

    memset(aTexts, 0, sizeof(aTexts));
    memcpy(aTexts + (size_t)PUFO_TEXT_REC_STRIDE * PUFO_FAC_NAME_REC_BASE,
           "Fac905", 6);

    memset(&facEnv, 0, sizeof(facEnv));
    facEnv.pCloseCtl = &closeCtl;
    facEnv.pUpCtl = &upCtl;
    facEnv.pDownCtl = &downCtl;
    facEnv.pfnCloseThunk = NopClick;
    facEnv.pfnLoadPads = RecordPads;
    facEnv.pfnToggle = RecordToggle;

#define MAP_BEGIN(ID)                                                          \
    memset(g_aMapGrid, 0, sizeof(g_aMapGrid));                                 \
    memset(abGridBytes, 0, sizeof(abGridBytes));                               \
    memset(aCraft, 0, sizeof(aCraft));                                         \
    aCraft[0].bHomeBase = 255; /* homed nowhere: the craft total starts at 0 */\
    apCraft[0] = &aCraft[0];                                                   \
    memset(&aBase, 0, sizeof(aBase));                                          \
    aBase.bUnk4 = 7;                                                           \
    apBases[0] = &aBase;                                                       \
    memset(aSiteRecs, 0, sizeof(aSiteRecs));                                   \
    memset(abKind, 0, sizeof(abKind));                                         \
    szLabel[0] = '\0';                                                         \
    PtrArray_ctor(&list);                                                      \
    pItem = (StrategItem *)calloc(1, sizeof(StrategItem));                    \
    pItem->nSiteId = 7;                                                        \
    pItem->nX = -1;                                                            \
    pItem->nY = -1;                                                            \
    PtrArray_Add(&list, pItem);                                                \
    nScreen = (ID); bActive = 1; bFail = 0; bConfirm = 1; bDone = 1;          \
    nBaseCount = 3; nSelBase = 0;                                              \
    g_nToggleRuns = 0; g_nPadLoads = 0;                                        \
    memset(&hEnv, 0, sizeof(hEnv));                                            \
    hEnv.abFacilityGrid = g_aMapGrid;                                          \
    hEnv.abGridBytes = abGridBytes;                                            \
    hEnv.nSelectedBaseIdx = 0;                                                 \
    hEnv.apCraft = apCraft;                                                    \
    hEnv.nCraftCount = 1;                                                      \
    hEnv.apBases = apBases;                                                    \
    hEnv.nBaseCount = 1;                                                       \
    memset(&env, 0, sizeof(env));                                              \
    env.abFacilityGrid = g_aMapGrid;                                           \
    env.pnScreenId = &nScreen;                                                 \
    env.pbPlaceActive = &bActive;                                              \
    env.pbPlaceFail = &bFail;                                                  \
    env.nTapX = 2;                                                             \
    env.nTapY = 2;                                                             \
    env.pHangar = &hEnv;                                                       \
    env.bPendingKind = 0;                                                      \
    env.pbConfirm = &bConfirm;                                                 \
    env.pbFacDone = &bDone;                                                    \
    env.nPlaceX = 1234;                                                        \
    env.nPlaceY = 5678;                                                        \
    env.apBases = apBases;                                                     \
    env.apSiteRecs = aSiteRecs;                                                \
    env.pStrategList = &list;                                                  \
    env.pTextRecs = aTexts;                                                    \
    env.abKindTable = (i8 *)abKind;                                            \
    env.pszLabelScratch = szLabel;                                             \
    env.cchLabelScratch = (i32)sizeof(szLabel);                                \
    env.pbBaseCount = &nBaseCount;                                             \
    env.pnSelectedBaseIdx = &nSelBase;                                         \
    env.pFacInit = &facEnv

    /* 1. any other screen id does nothing at all. */
    MAP_BEGIN(0);
    CheckEqI32(Geoscape_OnMapClick(&env), 0, "screen 0: the function does nothing");
    CheckEqI32(MFAC(108, 2, 2), 0, "and writes no grid byte");

    /* 2. screen 1 is gated on bBasePlaceActive. */
    MAP_BEGIN(1);
    bActive = 0;
    CheckEqI32(Geoscape_OnMapClick(&env), 0, "screen 1 with the placement byte clear: nothing runs");
    CheckEqI32(bFail, 0, "and bBasePlaceFail is not touched");

    /* 3. a refused placement sets the fail byte and writes nothing. */
    MAP_BEGIN(1);
    CheckEqI32(Geoscape_OnMapClick(&env), 1, "empty ground refuses the hangar");
    CheckEqI32(bFail, 1, "bBasePlaceFail (0x1870B8) is set");
    CheckEqI32(MFAC(108, 2, 2), 0, "and no cell is cleared");

    /* 4. an accepted single cell: tileA, tileB and facType go, the footprint does not. */
    MAP_BEGIN(1);
    MFAC(0, 2, 2) = 1;   MFAC(36, 2, 2) = 5;   MFAC(108, 2, 2) = 1;
    MFAC(0, 2, 3) = 1;   MFAC(36, 2, 3) = 5;   MFAC(108, 2, 3) = 1;
    CheckEqI32(Geoscape_OnMapClick(&env), 0, "a plain facility cell is accepted");
    CheckEqI32(bFail, 0, "with no fail byte");
    CheckEqI32(MFAC(0, 2, 2), 0, "tileA of the tapped cell is cleared");
    CheckEqI32(MFAC(36, 2, 2), 0, "and its tileB");
    CheckEqI32(MFAC(108, 2, 2), 0xFF, "with the 0xFF facType sentinel written in its place");
    CheckEqI32(MFAC(0, 2, 3), 1, "the neighbouring cell keeps its tileA - no footprint here");
    CheckEqI32(MFAC(108, 2, 3), 1, "and keeps its facType");

    /* 5. a hangar quadrant: the whole 2 by 2 footprint goes, across all three layers. The tap is
     * at (nY,nX) = (2,3), the quadrant stamped 10. */
    MAP_BEGIN(1);
    env.nTapX = 3;
    MFAC(0, 2, 2) = 9;   MFAC(0, 2, 3) = 10;  MFAC(0, 3, 2) = 11;  MFAC(0, 3, 3) = 12;
    MFAC(108, 2, 2) = 11;                     MFAC(108, 2, 3) = 11;
    MFAC(36, 2, 3) = 4;  MFAC(36, 2, 4) = 4;  MFAC(36, 3, 3) = 4;  MFAC(36, 3, 4) = 4;
    MFAC(72, 2, 3) = 4;  MFAC(72, 2, 4) = 4;  MFAC(72, 3, 3) = 4;  MFAC(72, 3, 4) = 4;
    MDAYS(4, 4) = 9;
    CheckEqI32(Geoscape_OnMapClick(&env), 0, "a hangar quadrant is accepted (two free slots, no craft)");
    CheckEqI32(MFAC(0, 2, 3), 0, "tileA cleared at the tapped cell");
    CheckEqI32(MFAC(0, 2, 4), 0, "tileA cleared at (nY,nX+1)");
    CheckEqI32(MFAC(0, 3, 3), 0, "tileA cleared at (nY+1,nX)");
    CheckEqI32(MFAC(0, 3, 4), 0, "and at the diagonal");
    CheckEqI32(MFAC(36, 2, 3), 0, "the same four cells of tileB");
    CheckEqI32(MFAC(36, 3, 4), 0, "including the diagonal");
    CheckEqI32(MFAC(72, 2, 3), 0, "and of layer2");
    CheckEqI32(MFAC(72, 3, 4), 0, "again at the diagonal");
    CheckEqI32(MFAC(108, 2, 3), 0xFF, "facType is the sentinel at the tapped cell only");
    CheckEqI32(MFAC(108, 2, 4), 0, "the facType of (nY,nX+1) is left alone");
    CheckEqI32(MDAYS(2, 3), 0, "buildDays cleared at the tapped cell");
    CheckEqI32(MDAYS(3, 3), 0, "at (nY+1,nX)");
    CheckEqI32(MDAYS(2, 4), 0, "at (nY,nX+1)");
    CheckEqI32(MDAYS(3, 4), 0, "and at the diagonal");
    CheckEqI32(MFAC(0, 2, 2), 9, "the anchor quadrant is NOT cleared: the footprint starts at the tap");
    CheckEqI32(MDAYS(4, 4), 9, "a cell outside the footprint keeps its build days");

    /* 6. screen 2 with no pending kind does nothing. */
    MAP_BEGIN(2);
    env.bPendingKind = 0xFF;
    CheckEqI32(Geoscape_OnMapClick(&env), 0, "screen 2 with the pending kind 0xFF: nothing runs");
    CheckEqI32(nScreen, 2, "and the screen id stays 2");

    /* 7. the confirm branch commits the pending facility. */
    MAP_BEGIN(2);
    CheckEqI32(Geoscape_OnMapClick(&env), 2, "the confirm branch commits the pending facility");
    CheckEqI32(aBase.nUnk8, 1234, "the base takes the pending x");
    CheckEqI32(aBase.nUnk12, 5678, "and the pending y");
    CheckEqI32(aBase.bUnk17, 0, "bUnk17 becomes the pending kind");
    CheckEqI32(aBase.bUnk16, 1, "and bUnk16 is set to 1");
    CheckEqI32(aSiteRecs[0].bType, 0x80, "the facility record's type is kind + 0x80");
    Check(strcmp(aSiteRecs[0].abName, "Fac905-0") == 0,
          "its name field gets the cost label the counter builds");
    CheckEqI32(abKind[0], 1, "and the counter behind the label is incremented");
    CheckEqI32(pItem->nX, 1234, "the strateg entry found by site id gets the pending x");
    CheckEqI32(pItem->nY, 5678, "and the pending y");
    CheckEqI32(nScreen, 1, "the screen id becomes 1");
    CheckEqI32(bConfirm, 0, "bBaseFacConfirm is cleared");
    CheckEqI32(bDone, 0, "and bBaseFacDone (0x187078)");

    /* 8. a miss in the strateg search: the original would write past the last entry. */
    MAP_BEGIN(2);
    pItem->nSiteId = 99;
    CheckEqI32(Geoscape_OnMapClick(&env), 2, "the commit still runs when no entry matches");
    CheckEqI32(pItem->nX, -1, "and the port does not write nX past the end of the list");
    CheckEqI32(pItem->nY, -1, "nor nY");

    /* 9. without the confirm flag the base-facility screen is opened instead. */
    MAP_BEGIN(2);
    bConfirm = 0;
    CheckEqI32(Geoscape_OnMapClick(&env), 3, "no confirm: the base-facility screen opens");
    CheckEqI32(nSelBase, 3, "nSelectedBaseIdx takes the base COUNT (0x159D80)");
    CheckEqI32(nScreen, 7, "and the screen id becomes 7");
    CheckEqI32(g_nToggleRuns, 1, "BaseFacScreen_Init ran, Toggle included");
    CheckEqI32(g_nPadLoads, 2, "with both panel buttons positioned");

#undef MAP_BEGIN
#undef MFAC
#undef MDAYS
}

/* --- BaseFacScreen_Toggle @ 0x589ec --- */

static BaseFacFlag g_aToggleFac[PUFO_BASEFAC_COUNT];

static void ToggleFacTable(void)
{
    i32 i;
    for (i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
        memset(&g_aToggleFac[i], 0, sizeof(g_aToggleFac[i]));
        snprintf(g_aToggleFac[i].szName, sizeof(g_aToggleFac[i].szName), "Fac%02d", i);
        /* The shipped InitUI table (g_abFacFlag20Init): 0..7, 11 and 13 are placeable. */
        g_aToggleFac[i].bFlag20 = (u8)((i <= 7 || i == 11 || i == 13) ? 1 : 0);
        g_aToggleFac[i].bFlag21 = (u8)i;
        g_aToggleFac[i].uCost = (u32)(1000 * (i + 1));
    }
}

static void TestBaseFacToggle(void)
{
    static u8 abGrid[1524];
    static UiControl upCtl, downCtl;
    static UiPanel *pPanel;
    static u8 bOpen, bArmed, bNewBase;
    static i32 nSel;
    static i8 nSelBase;
    static const char szLast[4] = { 0, 0, 0, 0 };
    BaseFacToggleEnv env;
    UiPanel *p;
    i32 i;

    printf("BaseFacScreen_Toggle @ 0x589ec\n");

    ToggleFacTable();
    memset(abGrid, 0, sizeof(abGrid));
    memset(&upCtl, 0, sizeof(upCtl));
    memset(&downCtl, 0, sizeof(downCtl));
    pPanel = NULL;
    bOpen = 0; bArmed = 0; bNewBase = 0; nSel = -1; nSelBase = 0;

    memset(&env, 0, sizeof(env));
    env.pbOpen = &bOpen;
    env.apPanelCtl[0] = &upCtl;
    env.apPanelCtl[1] = &downCtl;
    env.ppPanel = &pPanel;
    env.pnPanelSel = &nSel;
    env.pbArmed = &bArmed;
    env.pbNewBaseMode = &bNewBase;
    env.abFacilityGrid = abGrid;
    env.pnSelectedBaseIdx = &nSelBase;
    env.paFac = g_aToggleFac;
    env.pszLastRowName = szLast;
    env.pfnBuyThunk = NopClick;

    BaseFacScreen_Toggle(&env);
    CheckEqI32(bOpen, 1, "the first call takes the build branch");
    Check(pPanel != NULL, "the panel is allocated");
    p = pPanel;
    CheckEqI32(p->nLineCount, 9, "nLineCount is the ctor count 10 minus the 0x58C68 decrement");
    CheckEqI32(p->nStyle, 5, "nStyle 5");
    CheckEqI32(p->nFont, 16, "nFont 16");
    CheckEqI32(p->nPosX, 120, "nPosX 120");
    CheckEqI32(p->nPosY, 234, "nPosY 234");
    CheckEqI32(p->nUnk7052, 5, "nUnk7052 5");
    CheckEqI32(p->nUnk7056, 234, "nUnk7056 234");
    CheckEqI32(p->nViewW, 230, "nViewW 230");
    CheckEqI32(p->nUnk7076, 1, "nUnk7076 1");
    CheckEqI32(p->bUnk7088, 0x96, "bUnk7088 is the byte -106");
    CheckEqI32(p->bFlag7072, 0, "bFlag7072 cleared");
    CheckEqI32(p->bFlag7073, 1, "bFlag7073 set");
    CheckEqI32(p->gap_0[0], 0xB4, "gap_0[0] B4");
    CheckEqI32(p->gap_0[3], 0x46, "gap_0[3] 46");
    CheckEqI32(p->gap_0[6], 0x44, "gap_0[6] 44");
    CheckEqI32(p->gap_0[11], 0x4B, "gap_0[11] 4B");
    Check(p->pfnClick == NopClick, "the +3020 handler is the buy thunk");

    /* Rows: the flagged records 1..16, each carrying its record number as the aux. */
    Check(strcmp(UiPanel_GetLineText(p, 0), "Fac01") == 0, "row 0 is facility 1");
    CheckEqI32(UiPanel_GetLineAux(p, 0), 1, "with aux 1, the facility kind");
    Check(strcmp(UiPanel_GetLineText(p, 7), "Fac11") == 0,
          "row 7 is facility 11: records 8, 9 and 10 are unflagged");
    CheckEqI32(UiPanel_GetLineAux(p, 7), 11, "with aux 11");
    Check(strcmp(UiPanel_GetLineText(p, 8), "Fac13") == 0,
          "row 8 is facility 13: records 9, 10, 12, 14..16 are unflagged");
    CheckEqI32(UiPanel_GetLineAux(p, 8), 13, "with aux 13");
    CheckEqI32(UiPanel_GetLineAux(p, 9), 0, "the 17th row's aux is 0");
    CheckEqI32((p->apLines[9] != NULL) ? (i32)p->apLines[9][0] : -1, 0,
               "the 17th row's name comes from the 0x187024 slot, empty here");

    /* The close branch. */
    upCtl.x = 77;
    BaseFacScreen_Toggle(&env);
    CheckEqI32(bOpen, 0, "the second call takes the close branch");
    Check(pPanel == NULL, "the panel is freed and its slot cleared");
    CheckEqI32(upCtl.x, 77, "the caller-owned controls keep their fields; only their pads go");

    /* The new-base wipe, on a freshly built dialog. */
    for (i = 0; i < (i32)sizeof(abGrid); ++i) {
        abGrid[i] = 0x33;
    }
    bNewBase = 1;
    nSelBase = 2;
    nSel = -1;
    bArmed = 0;
    BaseFacScreen_Toggle(&env);
    CheckEqI32(nSel, 9, "unk_158868 takes the row count");
    CheckEqI32(bArmed, 1, "unk_158888 is armed");
    CheckEqI32(abGrid[36 * 2 + 10], 0, "the selected base's buildDays are cleared");
    CheckEqI32(abGrid[0x174 + 144 * 2 + 10], 0, "its tileA");
    CheckEqI32(abGrid[0x174 + 144 * 2 + 36 + 10], 0, "its tileB");
    CheckEqI32(abGrid[0x174 + 144 * 2 + 72 + 10], 0, "its needed-flag layer");
    CheckEqI32(abGrid[0x174 + 144 * 2 + 108 + 10], 0xFF, "and its facType takes the 0xFF sentinel");
    CheckEqI32(abGrid[36 * 1 + 10], 0x33, "the previous base is untouched");
    CheckEqI32(abGrid[0x174 + 144 * 3 + 108 + 10], 0x33, "and so is the next one");
    CheckEqI32(abGrid[36 * 2 + 35], 0, "the whole 6 by 6 base goes, last cell included");

    BaseFacScreen_Toggle(&env); /* release the dialog the new-base call built */
    Check(pPanel == NULL, "the close branch releases that panel too");
    BaseFacScreen_Toggle(NULL);
    CheckEqI32(bOpen, 0, "a null env changes nothing");
}

/* --- BaseFacScreen_TryBuySelected @ 0x58910 --- */

static void TestBaseFacBuy(void)
{
    static UiPanel panel;
    static BaseFacBuyEnv env;
    static u8 bArmed, bNewBase;
    static i32 nSel, nResidue;
    static u32 nShort;
    static i32 nFunds;

    printf("BaseFacScreen_TryBuySelected @ 0x58910\n");

    ToggleFacTable();
    memset(&panel, 0, sizeof(panel));
    if (!UiPanel_ctor(&panel, 3)) {
        printf("  FAIL the panel could not be constructed\n");
        return;
    }
    panel.nUnk3032 = 1;                    /* the selected row */
    UiPanel_SetLineAux(&panel, 1, 11);     /* the hangar, cost 12000 in this table */

    nFunds = 20000; bArmed = 0; bNewBase = 1; nSel = -1; nResidue = 7; nShort = 7;
    memset(&env, 0, sizeof(env));
    env.pPanel = &panel;
    env.paFac = g_aToggleFac;
    env.pnFunds = &nFunds;
    env.pbArmed = &bArmed;
    env.pbNewBaseMode = &bNewBase;
    env.pnPanelSel = &nSel;
    env.pdwShortfall = &nShort;
    env.pnResidue = &nResidue;

    CheckEqI32(BaseFacScreen_TryBuySelected(&env), 1, "an affordable facility is accepted");
    CheckEqI32(bArmed, 1, "unk_158888 is armed (0x5897C)");
    CheckEqI32(nSel, -1, "with the new-base byte set, unk_158868 is not touched");
    CheckEqI32((i32)nShort, 7, "the shortfall global is not written");
    CheckEqI32(nResidue, 7, "nor the residue");

    bNewBase = 0;
    CheckEqI32(BaseFacScreen_TryBuySelected(&env), 1, "the same call without the new-base byte");
    CheckEqI32(nSel, 1, "unk_158868 takes panel->nUnk3032 (0x58994)");

    nFunds = 1000;
    bArmed = 0;
    CheckEqI32(BaseFacScreen_TryBuySelected(&env), 0, "an unaffordable facility is refused");
    CheckEqI32((i32)nShort, 1, "byte_1870C8 is set, as a dword");
    CheckEqI32(nResidue, 0, "dword_1870B4[0] is cleared");
    CheckEqI32(bArmed, 0, "and the placement is not armed");

    UiPanel_SetLineAux(&panel, 1, 99);
    nShort = 7;
    CheckEqI32(BaseFacScreen_TryBuySelected(&env), 0, "an out-of-range kind is refused, not read");
    CheckEqI32((i32)nShort, 7, "and the shortfall global stays untouched");

    UiPanel_FreeLines(&panel);
}

/* --- SoldierScreen_Toggle @ 0x64af0 --- */

static int g_nFormatCalls;

static const char *FormatNameStub(void *pUser, Soldier *pSol)
{
    (void)pUser;
    g_nFormatCalls += 1;
    return (pSol->bUnk183 == 254) ? "Craft 7 dead" : "Craft 7";
}

static void TestSoldierScreenToggle(void)
{
    static Soldier aSol[4];
    static Soldier *apSol[4];
    static UiPanel *pA, *pB, *pC, *pD;
    static PadImage *pPad;
    static UiControl closeCtl;
    static u8 bOpen;
    static u16 nSolCount;
    static i32 nScreen;
    static i8 nSelBase;
    SoldierScreenEnv env;
    i32 i;

    printf("SoldierScreen_Toggle @ 0x64af0\n");

    for (i = 0; i < 4; ++i) {
        memset(&aSol[i], 0, sizeof(aSol[i]));
        snprintf(aSol[i].szName, sizeof(aSol[i].szName), "Sol%d", i);
        aSol[i].nKind = (u8)((i == 1) ? 2 : 1); /* the base byte at +0xB6 */
        aSol[i].bUnk183 = (u8)((i == 2) ? 254 : 255);
        apSol[i] = &aSol[i];
    }
    nSolCount = 4; nSelBase = 1; nScreen = 0; bOpen = 0;
    pA = NULL; pB = NULL; pC = NULL; pD = NULL; pPad = NULL;
    memset(&closeCtl, 0, sizeof(closeCtl));
    g_nFormatCalls = 0;

    memset(&env, 0, sizeof(env));
    env.pbOpen = &bOpen;
    env.pnScreenId = &nScreen;
    env.apSoldiers = apSol;
    env.pnSoldierCount = &nSolCount;
    env.pnSelectedBaseIdx = &nSelBase;
    env.ppPanelEquip = &pA;
    env.ppPanelStores = &pB;
    env.ppPanelNames = &pC;
    env.ppPanelCraft = &pD;
    env.ppPad = &pPad;
    env.pszPadName = NULL; /* the test has no pad bank */
    env.pCloseCtl = &closeCtl;
    env.pfnCloseSoldierThunk = NopClick;
    env.pfnEquipThunk = NopClick;
    env.pfnSelectThunk = NopClick;
    env.pfnFormatName = FormatNameStub;

    SoldierScreen_Toggle(&env);
    CheckEqI32(nScreen, 4, "the screen id goes to 4 on either direction");
    CheckEqI32(bOpen, 1, "the first call takes the open branch");
    Check(pPad != NULL, "the pad object is allocated");
    CheckEqI32(pPad->nOriginX, 0, "with origin 0 on x");
    CheckEqI32(pPad->nOriginY, 0, "and on y");
    Check(pA != NULL && pB != NULL && pC != NULL && pD != NULL, "all four panels are built");
    CheckEqI32(pA->nLineCount, 4, "panel A keeps its fixed 4 lines");
    CheckEqI32(pA->nStyle, 4, "nStyle 4");
    CheckEqI32(pA->nFont, 18, "nFont 18");
    CheckEqI32(pA->nPosX, 55, "panel A at x 55");
    CheckEqI32(pA->nViewW, 167, "167 wide");
    Check(pA->pfnClick == NopClick, "panel A's click is the equip thunk");
    CheckEqI32(pA->bUnk7088, 0xFF, "panel A's bUnk7088 is the byte -1");
    CheckEqI32(pA->gap_0[3], 0x32, "panel A's gap_0[3] 32");
    CheckEqI32(pA->gap_0[5], 0xBD, "and gap_0[5] BD");
    CheckEqI32(pA->gap_0[6], 0xC8, "gap_0[6] C8");
    CheckEqI32(pB->nPosX, 190, "panel B at x 190");
    Check(pB->pfnClick == NULL, "panel B has no click handler");
    CheckEqI32(pB->gap_0[5], 0xFA, "panel B's gap_0[5] FA");
    CheckEqI32(pC->nLineCount, 3, "panels C and D hold one line per soldier on the base");
    CheckEqI32(pC->nStyle, 13, "nStyle 13");
    CheckEqI32(pC->nViewW, 234, "panel C is 234 wide");
    Check(pC->pfnClick == NopClick, "panel C's click is the select thunk");
    CheckEqI32(pC->bUnk7088, 0x96, "panel C's bUnk7088 is the byte -106");
    CheckEqI32(pD->nViewW, 40, "panel D is 40 wide");
    CheckEqI32(pD->nUnk7076, 2, "panel D's nUnk7076 is 2");
    CheckEqI32(pD->bFlag7073, 0, "panel D's bFlag7073 is clear");
    Check(pD->pfnClick == NULL, "panel D has no click handler");

    Check(strcmp(UiPanel_GetLineText(pC, 0), "Sol0") == 0, "row 0 is soldier 0");
    CheckEqI32(UiPanel_GetLineAux(pC, 0), 0, "its aux is the ROSTER index");
    Check(strcmp(UiPanel_GetLineText(pC, 1), "Sol2") == 0,
          "soldier 1 is on another base, so row 1 is soldier 2");
    CheckEqI32(UiPanel_GetLineAux(pC, 1), 2, "with aux 2");
    Check(strcmp(UiPanel_GetLineText(pC, 2), "Sol3") == 0, "row 2 is soldier 3");
    CheckEqI32(UiPanel_GetLineAux(pC, 2), 3, "with aux 3");
    Check(strcmp(UiPanel_GetLineText(pD, 0), "Craft 7") == 0, "panel D takes the formatted name");
    CheckEqI32(g_nFormatCalls, 3, "the formatter runs once per row");
    CheckEqI32(pC->aLineMeta[6 * 0 + 0], 150, "a 255 craft byte gives the 150,150,250 triple");
    CheckEqI32(pC->aLineMeta[6 * 0 + 2], 250, "with 250 last");
    CheckEqI32(pC->aLineMeta[6 * 1 + 0], 250, "a 254 craft byte gives 250,150,150");
    CheckEqI32(pC->aLineMeta[6 * 1 + 2], 150, "with 150 last");
    CheckEqI32(pD->aLineMeta[6 * 1 + 0], 250, "panel D gets the same colours");
    CheckEqI32(pD->aLineMeta[6 * 1 + 3], 20, "and the second triple is 20,20,20 on both");
    CheckEqI32(pC->aLineMeta[6 * 2 + 0], 150, "row 2 is a 255 soldier again");
    Check(closeCtl.pfnClick == NopClick, "the close control gets the soldier close thunk");

    SoldierScreen_Toggle(&env);
    CheckEqI32(nScreen, 1, "the close branch puts the screen id back to 1");
    CheckEqI32(bOpen, 0, "and clears the flag");
    Check(pA == NULL && pB == NULL && pC == NULL && pD == NULL, "all four panels are released");
    Check(pPad == NULL, "and so is the pad");

    SoldierScreen_Toggle(NULL);
    CheckEqI32(nScreen, 1, "a null env changes nothing");
}

/* --- SoldierScreen_SelectListed @ 0x64a9c --- */

static void TestSoldierSelectListed(void)
{
    static UiPanel panel;
    static SoldierSelectEnv env;
    static i32 nCursor;
    static i32 nScreen;

    printf("SoldierScreen_SelectListed @ 0x64a9c\n");

    memset(&panel, 0, sizeof(panel));
    if (!UiPanel_ctor(&panel, 3)) {
        printf("  FAIL the panel could not be constructed\n");
        return;
    }
    panel.nUnk3032 = 1;                    /* the visible selection */
    UiPanel_SetLineAux(&panel, 1, 7);      /* the roster index the row pass stored */

    nCursor = -1; nScreen = 4;
    memset(&env, 0, sizeof(env));
    env.pPanel = &panel;
    env.pnCursor = &nCursor;
    env.pnScreenId = &nScreen;

    CheckEqI32(SoldierScreen_SelectListed(&env), 7, "the cursor takes the row's roster index");
    CheckEqI32(nCursor, 7, "written into nSoldierCursor @ 0x159E08");
    CheckEqI32(nScreen, 5, "and the screen id becomes 5");

    panel.nUnk3032 = 999;
    CheckEqI32(SoldierScreen_SelectListed(&env), 0, "an out-of-range selection reads 0");

    UiPanel_FreeLines(&panel);
}

/* --- SoldierScreen_EquipSelected @ 0x619cc --- */

static void TestSoldierEquipSelected(void)
{
    static Soldier aSol[2];
    static Soldier *apSol[2];
    static Ship aCraft[PUFO_FAC_BASE_COUNT];
    static Ship *apCraft[PUFO_FAC_BASE_COUNT];
    static BaseRec aBase;
    static BaseRec *apBases[PUFO_FAC_BASE_COUNT];
    static ObjectRecFull aObj[125];
    static ObjectRecFull *apObj[125];
    static UiPanel panel;
    static SoldierEquipEnv env;
    static u16 nCount;
    static i32 nCursor;
    static i8 nSelBase;
    static u8 bTransfer;
    i32 i;

    printf("SoldierScreen_EquipSelected @ 0x619cc\n");

    memset(aObj, 0, sizeof(aObj));
    memset(aCraft, 0, sizeof(aCraft));
    memset(&aBase, 0, sizeof(aBase));
    for (i = 0; i < 125; ++i) {
        apObj[i] = &aObj[i];
    }
    for (i = 0; i < PUFO_FAC_BASE_COUNT; ++i) {
        apCraft[i] = &aCraft[i];
    }
    apBases[0] = &aBase;
    aBase.bSlot = 3;
    aBase.nStoresTotal = 13;
    aObj[122].auPerBaseVal[3] = 4;
    aObj[123].auPerBaseVal[3] = 2;
    aObj[124].auPerBaseVal[3] = 7;

    memset(aSol, 0, sizeof(aSol));
    aSol[0].bUnk183 = 255; /* no craft */
    aSol[0].nUnitType = 0;
    aSol[1].bUnk183 = 0;   /* craft 0 */
    aSol[1].nUnitType = 2;
    apSol[0] = &aSol[0];
    apSol[1] = &aSol[1];

    nCount = 2;
    nSelBase = 0;
    bTransfer = 1;

    memset(&panel, 0, sizeof(panel));
    if (!UiPanel_ctor(&panel, 4)) {
        printf("  FAIL the equip panel could not be constructed\n");
        return;
    }
    for (i = 0; i < 4; ++i) {
        UiPanel_SetLineAux(&panel, i, i); /* the row numbers the panel fill would store */
    }

    memset(&env, 0, sizeof(env));
    env.apSoldiers = apSol;
    env.pnSoldierCount = &nCount;
    env.apCraft = apCraft;
    env.apBases = apBases;
    env.apObjects = apObj;
    env.pnCursor = &nCursor;
    env.pnSelectedBaseIdx = &nSelBase;
    env.pPanelEquip = &panel;
    env.pbTransferFlag = &bTransfer;

    nCursor = 0;
    panel.nUnk3032 = 1;
    bTransfer = 1;
    CheckEqI32(SoldierScreen_EquipSelected(&env), 1, "row 1 runs the body");
    CheckEqI32(aSol[0].nUnitType, 1, "the soldier takes equipment 1");
    CheckEqI32(aObj[122].auPerBaseVal[3], 3, "one unit leaves the base stock (0x61B04)");
    CheckEqI32(aBase.nStoresTotal, 12, "and the base total drops with it");
    CheckEqI32(bTransfer, 0, "bTransferFlag is cleared (0x61B78)");

    panel.nUnk3032 = 0;
    CheckEqI32(SoldierScreen_EquipSelected(&env), 1, "row 0 is the unequip row");
    CheckEqI32(aSol[0].nUnitType, 0, "the equipment goes back to 0");
    CheckEqI32(aObj[122].auPerBaseVal[3], 4, "and the item returns to the base (0x61B60)");
    CheckEqI32(aBase.nStoresTotal, 13, "with the total");

    /* Craft 0 is off-base, so soldier 1 cannot be re-equipped. */
    nCursor = 1;
    aCraft[0].bAtBase = 0;
    panel.nUnk3032 = 3;
    bTransfer = 1;
    CheckEqI32(SoldierScreen_EquipSelected(&env), 0, "a soldier on a craft that is off-base is refused");
    CheckEqI32(aSol[1].nUnitType, 2, "its equipment is untouched");
    CheckEqI32(aObj[124].auPerBaseVal[3], 7, "the stock is untouched");
    CheckEqI32(bTransfer, 1, "and the tail never runs, so the flag stays set");

    aCraft[0].bAtBase = 1;
    panel.nUnk3032 = 3;
    bTransfer = 1;
    CheckEqI32(SoldierScreen_EquipSelected(&env), 1, "with the craft at base the swap runs");
    CheckEqI32(aSol[1].nUnitType, 3, "the soldier moves from equipment 2 to 3");
    CheckEqI32(aObj[124].auPerBaseVal[3], 6, "the new item leaves the base (0x61A80)");
    CheckEqI32(aObj[123].auPerBaseVal[3], 3, "the old one comes back (0x61AC4)");
    CheckEqI32(aBase.nStoresTotal, 13, "so the base total is unchanged");
    CheckEqI32(bTransfer, 0, "and the tail clears the flag");

    UiPanel_SetLineAux(&panel, 2, 7); /* an aux value outside the four rows */
    panel.nUnk3032 = 2;
    bTransfer = 1;
    CheckEqI32(SoldierScreen_EquipSelected(&env), 1, "an out-of-range row only runs the tail");
    CheckEqI32(aSol[1].nUnitType, 3, "with no equipment change");
    CheckEqI32(aObj[124].auPerBaseVal[3], 6, "and no stock change");

    /* The bounds deviation: an old type outside the 121..124 group still clears, but no item moves. */
    aSol[1].nUnitType = 9;
    panel.nUnk3032 = 0;
    CheckEqI32(SoldierScreen_EquipSelected(&env), 1, "row 0 with an out-of-range old type");
    CheckEqI32(aSol[1].nUnitType, 0, "the type still goes to 0");
    CheckEqI32(aBase.nStoresTotal, 13, "but nothing is added to the base");

    nCursor = 99;
    CheckEqI32(SoldierScreen_EquipSelected(&env), 0, "a cursor past the roster is refused");

    nCursor = 1;
    nSelBase = 40;
    CheckEqI32(SoldierScreen_EquipSelected(&env), 0, "and so is a base index outside the eight");

    SoldierScreen_EquipSelected(NULL);
    CheckEqI32(aBase.nStoresTotal, 13, "a null env changes nothing");

    UiPanel_FreeLines(&panel);
}

/* --- BaseFacScreen_DrawPlacementOverlay @ 0x58e10 --- */

#define GridTileA(g, b, r, c) ((g)[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * (b) + 6 * (r) + (c)])
#define GridDays(g, b, r, c) ((g)[PUFO_GRID_DAYS_STRIDE * (b) + 6 * (r) + (c)])
#define GridFacType(g, b, r, c)                                                    \
    ((g)[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * (b) + PUFO_GRID_FAC_TYPE_OFF + \
         6 * (r) + (c)])

static void TestFacOverlay(void)
{
    static u8 abGrid[PUFO_GRID_SIZE];
    static UiPanel panel;
    static BaseFacFlag aFac[PUFO_BASEFAC_COUNT];
    static FacOverlayEnv env;
    static BaseFacToggleEnv toggleEnv;
    static UiPanel *pDialogPanel = NULL;
    static i32 nFunds;
    static i32 nScreen;
    static i32 nTapX;
    static i32 nTapY;
    static u8 bPreviewActive;
    static u8 bPreviewOk;
    static u8 bOverlayShown;
    static u8 bNewBaseMode;
    static u8 bDialogOpen;
    static u32 nShortfall;
    static i32 nResidue;
    static char szLabel[108];
    static SoftSurf gOverlaySurf;
    i32 i;

    printf("BaseFacScreen_DrawPlacementOverlay @ 0x58e10\n");

    /* The preview draws through SoftSurf_BlitRectProxy, so the blit target has to exist. */
    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&gOverlaySurf);
    SoftSurf_AllocPixels(&gOverlaySurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &gOverlaySurf;

    memset(abGrid, 0, sizeof(abGrid));
    memset(aFac, 0, sizeof(aFac));
    for (i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
        aFac[i].bFlag21 = (u8)i;              /* InitUI stores the record index here */
        aFac[i].uCost = (u32)(1000 * (i + 1));
    }

    memset(&panel, 0, sizeof(panel));
    if (!UiPanel_ctor(&panel, 2)) {
        printf("  FAIL the facility panel could not be constructed\n");
        return;
    }
    UiPanel_SetLineAux(&panel, 0, 3);         /* an ordinary facility */
    UiPanel_SetLineAux(&panel, 1, 11);        /* the hangar */

    nFunds = 5000;
    nScreen = 2;
    nTapX = 9;
    nTapY = 9;
    bPreviewActive = 1;
    bPreviewOk = 1;
    bOverlayShown = 0;
    bNewBaseMode = 0;
    bDialogOpen = 1;
    nShortfall = 0;
    nResidue = 7;

    memset(&toggleEnv, 0, sizeof(toggleEnv));
    toggleEnv.pbOpen = &bDialogOpen;
    toggleEnv.ppPanel = &pDialogPanel;
    toggleEnv.paFac = aFac;
    toggleEnv.abFacilityGrid = abGrid;

    memset(&env, 0, sizeof(env));
    env.abFacilityGrid = abGrid;
    env.pPanel = &panel;
    env.nPanelSel = 0;                        /* row 0 -> facility 3 */
    env.paFac = aFac;
    env.pnFunds = &nFunds;
    env.nSelectedBaseIdx = 0;
    env.pnTapX = &nTapX;
    env.pnTapY = &nTapY;
    env.nPendingX = 3;
    env.nPendingY = 2;
    env.nOriginX = 0;
    env.nOriginY = 0;
    env.nCursorX = 100;                       /* tap = (100/32, (100-36)/32) = (3, 2) */
    env.nCursorY = 100;
    env.pbPreviewActive = &bPreviewActive;
    env.pbPreviewOk = &bPreviewOk;
    env.pbOverlayShown = &bOverlayShown;
    env.pbNewBaseMode = &bNewBaseMode;
    env.nPendingKind = 0x80 + 2;
    env.paNewBaseCost = g_anNewBaseCostByKind;
    env.nPhaseByte = 0;
    env.pdwShortfall = &nShortfall;
    env.pnResidue = &nResidue;
    env.pnScreenId = &nScreen;
    env.pszLabel = szLabel;
    env.cchLabel = sizeof(szLabel);
    env.pToggleEnv = &toggleEnv;
    env.nPlaceX = -6000;
    env.nPlaceY = -6000;

    /* An empty grid with nothing next to the tap is not a legal placement. */
    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(&env), 0, "an isolated tap does not commit");
    CheckEqI32(bOverlayShown, 1, "the overlay flag is raised (0x59234)");
    CheckEqI32(nTapX, 3, "the tap column comes from the cursor (0x58EC0)");
    CheckEqI32(nTapY, 2, "and the tap row too (0x58EC8)");
    CheckEqI32(bPreviewOk, 0, "a zero score clears bFacPreviewOk (0x599AC)");
    CheckEqI32(nFunds, 5000, "and nothing is spent");

    /* A finished neighbour makes it legal. */
    GridTileA(abGrid, 0, 2, 2) = 9;
    bPreviewOk = 1;
    bOverlayShown = 0;
    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(&env), 1, "a neighbour to the left makes it legal");
    CheckEqI32(nFunds, 1000, "the facility cost comes off nFunds (0x59AB4)");
    CheckEqI32(GridFacType(abGrid, 0, 2, 3), 3, "the cell kind is aBaseFacFlags[3].bFlag21");
    CheckEqI32(bOverlayShown, 1, "the preview block ran");

    /* Not enough funds. */
    memset(abGrid, 0, sizeof(abGrid));
    GridTileA(abGrid, 0, 2, 2) = 9;
    nFunds = 100;
    nShortfall = 0;
    nResidue = 7;
    bPreviewOk = 1;
    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(&env), 0, "an unaffordable facility stops (0x59A24)");
    CheckEqI32(nShortfall, 1, "the shortfall byte is written as a dword");
    CheckEqI32(nResidue, 0, "and the residue slot is cleared");
    CheckEqI32(nFunds, 100, "with nFunds untouched");
    CheckEqI32(GridFacType(abGrid, 0, 2, 3), 0, "and no cell written");

    /* The three commit guards. */
    nFunds = 5000;
    bPreviewOk = 0;
    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(&env), 0, "a cleared bFacPreviewOk stops the commit");
    bPreviewOk = 1;
    env.nPendingX = 4;
    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(&env), 0, "a tap that is not the pending one stops it");
    env.nPendingX = 3;
    bPreviewActive = 0;
    nTapX = 9;
    nTapY = 9;
    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(&env), 0, "and so does a cleared bFacPreviewActive");
    CheckEqI32(nFunds, 5000, "with nothing spent");
    CheckEqI32(nTapX, 3, "the tap then comes from the pending pair, not the cursor");
    bPreviewActive = 1;
    env.nCursorX = 100;

    /* The hangar: a 2x2 footprint, and the side limit drops from 5 to 4. */
    memset(abGrid, 0, sizeof(abGrid));
    GridTileA(abGrid, 0, 1, 3) = 7;           /* the up neighbour of the 2x2 anchor */
    nFunds = 20000;
    bPreviewOk = 1;
    env.nPanelSel = 1;                        /* row 1 -> facility 11, the hangar */
    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(&env), 1, "the hangar commits through the same path");
    CheckEqI32(nFunds, 20000 - 12000, "at its own cost");
    CheckEqI32(GridFacType(abGrid, 0, 2, 3), 11, "one facType byte at the anchor");
    CheckEqI32(GridTileA(abGrid, 0, 2, 3), 9, "with the 2x2 stamp 9 at the anchor");
    CheckEqI32(GridTileA(abGrid, 0, 2, 4), 10, "10 to the right");
    CheckEqI32(GridTileA(abGrid, 0, 3, 3), 11, "11 below");
    CheckEqI32(GridTileA(abGrid, 0, 3, 4), 12, "and 12 on the diagonal");

    /* New-base mode forces the score, spends the base cost too and closes the dialog. */
    env.nPanelSel = 0;
    memset(abGrid, 0, sizeof(abGrid));
    nFunds = 5000;
    bPreviewOk = 1;
    bNewBaseMode = 1;
    bDialogOpen = 1;
    nScreen = 2;
    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(&env), 1, "new-base mode commits with an empty grid");
    CheckEqI32(nFunds, 5000 - 4000 - g_anNewBaseCostByKind[2],
               "the facility cost and aNewBaseCostByRace[kind - 0x80] both come off");
    CheckEqI32(bPreviewActive, 0, "bFacPreviewActive is cleared (0x59B20)");
    CheckEqI32(nScreen, 1, "nGeoscapeScreen goes back to 1 (0x59B28)");
    CheckEqI32(bNewBaseMode, 0, "new-base mode is cleared (0x59B2C)");
    CheckEqI32(bPreviewOk, 0, "and so is bFacPreviewOk (0x59B34)");
    CheckEqI32(bDialogOpen, 0, "the dialog closed through BaseFacScreen_Toggle");

    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(NULL), 0, "a null env does nothing");

    UiPanel_FreeLines(&panel);
}

/* BaseFacScreen_DrawBuildDialog @ 0x59b60. The dialog is a draw function, so the checks go through
 * the state it drives: the overlay flag, the placement overlay it calls on the dialog's own panel
 * selection, the funds that commit spends, and the shortfall panel the shared tail animates. The
 * pixel probes sit where only the shortfall panel can reach them: the dialog's own frame is at
 * y = 228 and the overlay outline at columns 96 and 127. */
static void TestBuildDialog(void)
{
    static u8 abGrid[PUFO_GRID_SIZE];
    static UiPanel panel;
    static BaseFacFlag aFac[PUFO_BASEFAC_COUNT];
    static FacOverlayEnv overlayEnv;
    static BuildDialogEnv env;
    static UiAnimBlitEnv anim;
    static GfxColorEnv color;
    static UiControl ctl;
    static SoftSurf gDialogSurf;
    static u8 bAnimStarted;
    static i8 nAnimProgress;
    static u8 bPreviewActive;
    static u8 bPreviewOk;
    static u8 bOverlayShown;
    static u8 bNewBaseMode;
    static u8 bDialogOpen;
    static u8 bPlaceArmed;
    static i32 nFunds;
    static i32 nTapX;
    static i32 nTapY;
    static i32 nResidue;
    static u32 nShortfall;
    static char szLabel[108];
    const u16 *pPix;
    i32 i;

    printf("BaseFacScreen_DrawBuildDialog @ 0x59b60\n");

    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&gDialogSurf);
    SoftSurf_AllocPixels(&gDialogSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &gDialogSurf;
    pPix = (const u16 *)gDialogSurf.pPixels;

    memset(abGrid, 0, sizeof(abGrid));
    memset(aFac, 0, sizeof(aFac));
    for (i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
        aFac[i].bFlag21 = (u8)i;              /* InitUI stores the record index here */
        aFac[i].uCost = (u32)(1000 * (i + 1));
    }

    memset(&panel, 0, sizeof(panel));
    if (!UiPanel_ctor(&panel, 2)) {
        printf("  FAIL the facility panel could not be constructed\n");
        return;
    }
    UiPanel_SetLineAux(&panel, 0, 3);         /* an ordinary facility */
    UiPanel_SetLineAux(&panel, 1, 11);        /* the hangar */

    memset(&ctl, 0, sizeof(ctl));             /* disabled: the dialog's pad blit draws nothing */

    memset(&anim, 0, sizeof(anim));
    anim.pbStarted = &bAnimStarted;
    anim.pnProgress = &nAnimProgress;
    anim.nFrameStyle = 0;

    memset(&overlayEnv, 0, sizeof(overlayEnv));
    overlayEnv.abFacilityGrid = abGrid;
    overlayEnv.pPanel = &panel;
    overlayEnv.nPanelSel = 0;
    overlayEnv.paFac = aFac;
    overlayEnv.pnFunds = &nFunds;
    overlayEnv.nSelectedBaseIdx = 0;
    overlayEnv.pnTapX = &nTapX;
    overlayEnv.pnTapY = &nTapY;
    overlayEnv.nPendingX = 3;
    overlayEnv.nPendingY = 2;
    overlayEnv.nCursorX = 100;                /* tap = (100/32, (100-36)/32) = (3, 2) */
    overlayEnv.nCursorY = 100;
    overlayEnv.pbPreviewActive = &bPreviewActive;
    overlayEnv.pbPreviewOk = &bPreviewOk;
    overlayEnv.pbOverlayShown = &bOverlayShown;
    overlayEnv.pbNewBaseMode = &bNewBaseMode;
    overlayEnv.nPhaseByte = 0;
    overlayEnv.pdwShortfall = &nShortfall;
    overlayEnv.pnResidue = &nResidue;
    overlayEnv.pszLabel = szLabel;
    overlayEnv.cchLabel = sizeof(szLabel);
    overlayEnv.nPlaceX = -6000;
    overlayEnv.nPlaceY = -6000;

    memset(&env, 0, sizeof(env));
    env.pColor = &color;
    env.pAnim = &anim;
    env.pPanel = &panel;
    env.nPanelSel = 0;
    env.paFac = aFac;
    env.pnFunds = &nFunds;
    env.pCtl = &ctl;
    env.pbDialogOpen = &bDialogOpen;
    env.pbPlaceArmed = &bPlaceArmed;
    env.pbOverlayShown = &bOverlayShown;
    env.pdwShortfall = &nShortfall;
    env.pnResidue = &nResidue;
    env.pszFundsPrefix = "funds ";
    env.pszRowCost = "cost";
    env.pszRowBuild = "build time";
    env.pszRowNewBase = "new base";
    env.pszDaysSuffix = "days";
    env.pszTitleArmed = "build";
    env.pszTitleList = "facilities";
    env.pszShortfall = "not enough funds";
    env.pOverlayEnv = &overlayEnv;

    bDialogOpen = 0;
    bPlaceArmed = 1;
    bOverlayShown = 1;
    nTapX = 9;
    nFunds = 5000;
    CheckEqI32(BaseFacScreen_DrawBuildDialog(&env), 0, "a closed dialog draws nothing (0x59B6C)");
    CheckEqI32(bOverlayShown, 1, "and it touches no state");
    CheckEqI32(nTapX, 9, "the placement overlay never ran");

    /* The unarmed branch: the overlay flag goes down and the overlay is not called at all. */
    bDialogOpen = 1;
    bPlaceArmed = 0;
    bOverlayShown = 1;
    memset(gDialogSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    CheckEqI32(BaseFacScreen_DrawBuildDialog(&env), 1, "an open dialog draws (0x59B84)");
    CheckEqI32(bOverlayShown, 0, "the unarmed branch clears bBaseFacOverlayShown (0x59FAC)");
    CheckEqI32(nTapX, 9, "and never reaches the placement overlay");
    Check(pPix[130 * 240 + 20] == 0, "the shortfall panel is not drawn (0x5A138)");

    /* Armed: the overlay runs, on the dialog's own panel selection. */
    bPlaceArmed = 1;
    bOverlayShown = 0;
    bPreviewActive = 1;
    bPreviewOk = 1;
    bNewBaseMode = 0;
    nFunds = 5000;
    nShortfall = 0;
    nResidue = 7;
    GridTileA(abGrid, 0, 2, 2) = 9;           /* a finished neighbour makes the tap legal */
    memset(gDialogSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    CheckEqI32(BaseFacScreen_DrawBuildDialog(&env), 1, "the armed branch runs the overlay (0x59EF8)");
    CheckEqI32(nTapX, 3, "the tap column comes from the cursor (0x58EC0)");
    CheckEqI32(nTapY, 2, "and the tap row too (0x58EC8)");
    CheckEqI32(nFunds, 5000 - 4000, "the commit spends aBaseFacFlags[3].uCost (0x59AB4)");
    CheckEqI32(GridFacType(abGrid, 0, 2, 3), 3, "the cell kind is the panel selection (0x59CB4)");
    CheckEqI32(bOverlayShown, 1, "the preview block raised the overlay flag (0x59234)");
    Check(pPix[130 * 240 + 20] == 0, "an affordable purchase skips the shortfall panel");

    /* Unaffordable: the overlay leaves the shortfall state behind and the shared tail draws. */
    memset(abGrid, 0, sizeof(abGrid));
    GridTileA(abGrid, 0, 2, 2) = 9;
    nFunds = 100;
    nShortfall = 0;
    nResidue = 7;
    bPreviewOk = 1;
    memset(gDialogSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    CheckEqI32(BaseFacScreen_DrawBuildDialog(&env), 1, "the dialog still draws when the purchase fails");
    CheckEqI32(nShortfall, 1, "the overlay records the shortfall (0x59A24)");
    CheckEqI32(nResidue, 0, "and clears the residue slot");
    CheckEqI32(nFunds, 100, "with nFunds untouched");
    Check(pPix[130 * 240 + 20] != 0, "the shortfall panel animates at (20, 130) (0x5A138)");

    /* The hangar row: the same selection drives the 2x2 stamp. */
    memset(abGrid, 0, sizeof(abGrid));
    GridTileA(abGrid, 0, 1, 3) = 7;           /* the up neighbour of the 2x2 anchor */
    nFunds = 20000;
    nShortfall = 0;
    nResidue = 7;
    bPreviewOk = 1;
    env.nPanelSel = 1;
    overlayEnv.nPanelSel = 1;
    CheckEqI32(BaseFacScreen_DrawBuildDialog(&env), 1, "the hangar row commits through the same path");
    CheckEqI32(GridFacType(abGrid, 0, 2, 3), 11, "one facType byte at the anchor");
    CheckEqI32(GridTileA(abGrid, 0, 3, 4), 12, "with the 2x2 stamp reaching the diagonal");

    CheckEqI32(BaseFacScreen_DrawBuildDialog(NULL), 0, "a null env does nothing");

    UiPanel_FreeLines(&panel);
}

/* Soldier_FormatDisplayName @ 0x6654c. The soldier's craft byte (Soldier.bUnk183 @ +0xB7) picks the
 * label: 0xFF is record 93, 0xFE is record 140, and anything below 0xFE looks the craft up and names
 * it with records 137/138/139 for craft types 0..2 plus Ship.bType. Craft types 3 and 4 name nothing
 * at all -- the original hands back the shared scratch string as the previous call left it. */
static void TestSoldierDisplayName(void)
{
    static u8 aText[PUFO_TEXT_REC_STRIDE * 420];
    static Ship aShips[2];
    static Ship *apCraft[2];
    static Soldier sol;
    static char szOut[108];

    printf("Soldier_FormatDisplayName @ 0x6654c\n");

    PutTextRec(aText, 93, "no craft");
    PutTextRec(aText, 137, "INTERCEPTOR");
    PutTextRec(aText, 138, "TRANSPORT");
    PutTextRec(aText, 139, "BOMBER");
    PutTextRec(aText, 140, "on foot");

    memset(aShips, 0, sizeof(aShips));
    aShips[0].bCraftType = 1;
    aShips[0].bType = 7;
    aShips[1].bCraftType = 4;
    aShips[1].bType = 2;
    apCraft[0] = &aShips[0];
    apCraft[1] = &aShips[1];

    memset(&sol, 0, sizeof(sol));
    sol.bUnk183 = 0xFF;
    Check(strcmp(Soldier_FormatDisplayName(&sol, apCraft, aText, szOut, sizeof(szOut)), "no craft") == 0,
          "0xFF is record 93 and returns early (0x66560)");

    sol.bUnk183 = 0xFE;
    Check(strcmp(Soldier_FormatDisplayName(&sol, apCraft, aText, szOut, sizeof(szOut)), "on foot") == 0,
          "0xFE skips the craft branch and gets record 140 (0x66694)");

    sol.bUnk183 = 0;
    Check(strcmp(Soldier_FormatDisplayName(&sol, apCraft, aText, szOut, sizeof(szOut)), "TRANSPORT7") == 0,
          "a type-1 craft is record 138 plus Ship.bType (0x66610, 0x66664)");

    aShips[0].bCraftType = 0;
    aShips[0].bType = 12;
    Check(strcmp(Soldier_FormatDisplayName(&sol, apCraft, aText, szOut, sizeof(szOut)), "INTERCEPTOR12") == 0,
          "type 0 is record 137 (0x6663C)");

    aShips[0].bCraftType = 2;
    aShips[0].bType = 1;
    Check(strcmp(Soldier_FormatDisplayName(&sol, apCraft, aText, szOut, sizeof(szOut)), "BOMBER1") == 0,
          "type 2 is record 139 (0x665EC)");

    /* Types 3 and 4 branch past both the label and the number. */
    sol.bUnk183 = 1;
    PutStr(szOut, "stale");
    Check(strcmp(Soldier_FormatDisplayName(&sol, apCraft, aText, szOut, sizeof(szOut)), "") == 0,
          "a type-4 craft names nothing (0x665CC default)");

    Check(strcmp(Soldier_FormatDisplayName(NULL, apCraft, aText, szOut, sizeof(szOut)), "") == 0,
          "a null soldier yields the empty string");
    Check(strcmp(Soldier_FormatDisplayName(&sol, NULL, aText, szOut, sizeof(szOut)), "") == 0,
          "and so does a null roster");
}

/* OptionsScreen_Init @ 0x6be74, the twelve volume handlers @ 0x6ba14..0x6be10,
 * OptionsScreen_RefreshVolumeHighlight @ 0x6b8fc and the three tab binders
 * @ 0x6b6c4 / 0x6b49c / 0x6b588. */

static UiControl g_aOptList[7];
static UiControl g_aOptMusic[6];
static UiControl g_aOptSfx[6];
static UiControl g_ctlSave;  /* unk_186BB4 */
static UiControl g_ctlBack;  /* unk_186BB0 */
static OptionsScreenCtl g_optCtl;
static ScreenScratch g_optScratch;
static i32 g_nMusicVol;
static u8 g_bMusicOff;
static i32 g_nSfxVol;
static u8 g_bSfxOff;
static u8 g_bOptBuilt;
static OptionsVolumeEnv g_optVol;
static OptionsTabEnv g_optTab;
static OptionsInitEnv g_optInit;

static i32 g_nSetupCalls;
static char g_szRowPad0[16];
static char g_szRowPad1[16];
static char g_szVolPad0[16];
static char g_szVolPad1[16];

static bool Opt_LoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                         const char *psz2, void *pUser)
{
    (void)pCtl;
    (void)psz2;
    (void)pUser;
    g_nSetupCalls += 1;
    if (g_nSetupCalls == 1) {
        PutStr(g_szRowPad0, psz0);
        PutStr(g_szRowPad1, psz1);
    }
    if (g_nSetupCalls == 8) { /* the first of the twelve radio buttons */
        PutStr(g_szVolPad0, psz0);
        PutStr(g_szVolPad1, psz1);
    }
    return true;
}

/* The twelve handlers the app would install, and the tab/slot stubs. */
static void OptMusic0(void) { (void)OptionsScreen_SetMusicVol(&g_optVol, 0); }
static void OptMusic1(void) { (void)OptionsScreen_SetMusicVol(&g_optVol, 1); }
static void OptMusic2(void) { (void)OptionsScreen_SetMusicVol(&g_optVol, 2); }
static void OptMusic3(void) { (void)OptionsScreen_SetMusicVol(&g_optVol, 3); }
static void OptMusic4(void) { (void)OptionsScreen_SetMusicVol(&g_optVol, 4); }
static void OptMusic5(void) { (void)OptionsScreen_SetMusicVol(&g_optVol, 5); }
static void OptSfx0(void) { (void)OptionsScreen_SetSfxVol(&g_optVol, 0); }
static void OptSfx1(void) { (void)OptionsScreen_SetSfxVol(&g_optVol, 1); }
static void OptSfx2(void) { (void)OptionsScreen_SetSfxVol(&g_optVol, 2); }
static void OptSfx3(void) { (void)OptionsScreen_SetSfxVol(&g_optVol, 3); }
static void OptSfx4(void) { (void)OptionsScreen_SetSfxVol(&g_optVol, 4); }
static void OptSfx5(void) { (void)OptionsScreen_SetSfxVol(&g_optVol, 5); }

static i32 g_nTabFired;
static void OptTabSave(void) { g_nTabFired = 1; }
static void OptTabLoad(void) { g_nTabFired = 2; }
static void OptTab3(void) { g_nTabFired = 3; }
static void OptTab4(void) { g_nTabFired = 4; }
static void OptSaveAudio(void) { g_nTabFired = 5; }
static void OptReturnTitle(void) { g_nTabFired = 6; }

static i32 g_nSlotFired;
static void OptSave0(void) { g_nSlotFired = 100; }
static void OptSave1(void) { g_nSlotFired = 101; }
static void OptSave2(void) { g_nSlotFired = 102; }
static void OptSave3(void) { g_nSlotFired = 103; }
static void OptSave4(void) { g_nSlotFired = 104; }
static void OptSave5(void) { g_nSlotFired = 105; }
static void OptSave6(void) { g_nSlotFired = 106; }
static void OptLoad0(void) { g_nSlotFired = 200; }
static void OptLoad1(void) { g_nSlotFired = 201; }
static void OptLoad2(void) { g_nSlotFired = 202; }
static void OptLoad3(void) { g_nSlotFired = 203; }
static void OptLoad4(void) { g_nSlotFired = 204; }
static void OptLoad5(void) { g_nSlotFired = 205; }
static void OptLoad6(void) { g_nSlotFired = 206; }
static void OptFormatSlots(void *pUser) { (void)pUser; g_nSlotFired = 7; }

/* The level the sfx applier hands the backend is observable through this seam. */
static i32 g_nSfxLevel;
static i32 g_nSfxLevelCalls;

static bool Fake_Init(u32 nMix, i32 nCh, u32 nFlags) { (void)nMix; (void)nCh; (void)nFlags; return false; }
static void Fake_SetWindow(void *h) { (void)h; }
static void *Fake_SampleLoad(const void *p, u32 cb, u32 m) { (void)p; (void)cb; (void)m; return NULL; }
static void Fake_SampleSetMode(void *p, u32 m) { (void)p; (void)m; }
static void Fake_SampleFree(void *p) { (void)p; }
static i32 Fake_PlaySound(void *p) { (void)p; return 0; }
static void Fake_SetSfxVolume(i32 n) { g_nSfxLevel = n; g_nSfxLevelCalls += 1; }
static void Fake_SetSfxPaused(i32 n) { (void)n; }
static void *Fake_MusicLoad(const void *p, u32 cb, u32 m) { (void)p; (void)cb; (void)m; return NULL; }
static void Fake_MusicLoop(void *p, i32 b) { (void)p; (void)b; }
static void Fake_MusicOpt(void *p, i32 a, i32 b) { (void)p; (void)a; (void)b; }
static void Fake_MusicPlay(void *p) { (void)p; }
static void Fake_MusicVol(void *p, i32 n) { (void)p; (void)n; }
static void Fake_MusicPause(void *p, i32 b) { (void)p; (void)b; }
static void Fake_MusicFree(void *p) { (void)p; }
static void Fake_Close(void) { }

static const PufoAudioBackend g_fakeOptBackend = {
    Fake_Init, Fake_SetWindow, Fake_SampleLoad, Fake_SampleSetMode, Fake_SampleFree,
    Fake_PlaySound, Fake_SetSfxVolume, Fake_SetSfxPaused, Fake_MusicLoad, Fake_MusicLoop,
    Fake_MusicOpt, Fake_MusicPlay, Fake_MusicVol, Fake_MusicPause, Fake_MusicFree, Fake_Close
};

/* ------------------------------------------------- the soldier screen's frame
 *
 * Evidence: SoldierScreen_Draw @ 0x65290. */

static SoftSurf g_solSurf;
static PadImage g_solPad;
static u8 g_aSolPad[8];
static UiPanel g_solPanelNames, g_solPanelCraft;
static SoldierDrawEnv g_solEnv;
static i32 g_nSolScreenId;
static u8 g_bSolOpen;
static i32 g_nSolCursor;
static i32 g_nSolTextCalls;
static i32 g_nSolTextCursor;

static i32 SolTextInputThunk(i32 nCursor, void *pUser)
{
    (void)pUser;
    ++g_nSolTextCalls;
    g_nSolTextCursor = nCursor;
    return 0x5A5;
}

/* Blit_PackedColor with all three colour arguments equal, which is the only shape this draw uses. */
static u16 SolPix(u32 c)
{
    return (u16)(((c >> 3) << 11) + 32u * (c >> 2) + (c >> 3));
}

static void SolReset(void)
{
    g_nSolScreenId = 4;
    g_bSolOpen = 1;
    g_nSolCursor = 17;
    g_nSolTextCalls = 0;
    g_nSolTextCursor = -1;

    /* PadImage_Blit has no NULL guard, so the pad needs a real blob: u16 width, u16 height, indices. */
    memset(&g_solPad, 0, sizeof(g_solPad));
    memset(g_aSolPad, 0, sizeof(g_aSolPad));
    g_aSolPad[0] = 1; g_aSolPad[1] = 0;   /* uWidth 1 */
    g_aSolPad[2] = 1; g_aSolPad[3] = 0;   /* uHeight 1 */
    g_aSolPad[4] = 1;                     /* one indexed pixel */
    PadImage_ctor(&g_solPad);
    g_solPad.pData = g_aSolPad;
    g_solPad.uWidth = 1;
    g_solPad.uHeight = 1;
    g_solPad.nMode = 8;                   /* the mode SoldierScreen_Toggle loads the "soldier" pad at */

    memset(&g_solEnv, 0, sizeof(g_solEnv));
    g_solEnv.pnScreenId = &g_nSolScreenId;
    g_solEnv.pbOpen = &g_bSolOpen;
    g_solEnv.pnCursor = &g_nSolCursor;
    g_solEnv.pPad = &g_solPad;
    g_solEnv.pPanelNames = &g_solPanelNames;
    g_solEnv.pPanelCraft = &g_solPanelCraft;
    g_solEnv.pfnTextInput = SolTextInputThunk;
}

static void TestSoldierScreenDraw(void)
{
    const u16 *pPix;

    printf("SoldierScreen_Draw @ 0x65290\n");

    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&g_solSurf);
    SoftSurf_AllocPixels(&g_solSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_solSurf;
    pPix = (const u16 *)g_solSurf.pPixels;
    memset(g_solSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    memset(g_aPalette[8], 0, sizeof(g_aPalette[8]));
    g_aPalette[8][1] = 0x1234u;

    memset(&g_solPanelNames, 0, sizeof(g_solPanelNames));
    memset(&g_solPanelCraft, 0, sizeof(g_solPanelCraft));
    SolReset();

    /* CLOSED (0x652CC): the open byte gates the whole draw. */
    g_bSolOpen = 0;
    CheckEqI32(SoldierScreen_Draw(&g_solEnv), 0, "a closed soldier screen draws nothing (0x652CC)");
    Check(pPix[23 * 240 + 69] == 0, "  not the header bevel");
    Check(pPix[6 * 240 + 2] == 0, "  nor the side rails");
    Check(pPix[0] == 0, "  nor the pad");
    g_bSolOpen = 1;

    /* SCREEN 5 (0x652A4): the name-edit dialog that SoldierScreen_SelectListed arms. */
    g_nSolScreenId = 5;
    CheckEqI32(SoldierScreen_Draw(&g_solEnv), 0x5A5, "screen 5 hands back the dialog's value");
    CheckEqI32(g_nSolTextCalls, 1, "the text-input hook ran once (0x652B0)");
    CheckEqI32(g_nSolTextCursor, 17, "  with nSoldierCursor as its argument");
    Check(pPix[23 * 240 + 69] == 0, "and no frame was drawn");
    g_nSolScreenId = 4;

    g_nSolScreenId = 5;
    g_solEnv.pfnTextInput = NULL;
    CheckEqI32(SoldierScreen_Draw(&g_solEnv), 0, "with no dialog hook installed nothing happens");
    CheckEqI32(g_nSolTextCalls, 1, "  and the frame is still not drawn");
    g_nSolScreenId = 4;
    g_solEnv.pfnTextInput = SolTextInputThunk;

    /* OPEN (0x652D4..0x65538): the pad, then the frame. */
    CheckEqI32(SoldierScreen_Draw(&g_solEnv), 0, "the open path draws and returns nothing useful");
    CheckEqI32(g_nSolTextCalls, 1, "the dialog hook is not reached on the draw path");
    CheckEqI32((i32)pPix[0], 0x1234, "the soldier pad is blitted first (0x652D4)");

    /* The header bevel: light top and right, dark bottom and left (0x65318..0x65390). */
    CheckEqI32((i32)pPix[23 * 240 + 69], SolPix(0xB4), "the bevel's top edge starts at 69,23 in 0xB4");
    CheckEqI32((i32)pPix[23 * 240 + 165], SolPix(0xB4), "  and runs 97 wide to 165");
    CheckEqI32((i32)pPix[44 * 240 + 69], SolPix(0x64), "the bottom edge at 69,44 is 0x64");
    CheckEqI32((i32)pPix[44 * 240 + 165], SolPix(0x64), "  over the same 97 columns");
    CheckEqI32((i32)pPix[24 * 240 + 69], SolPix(0x64), "the left edge at 69,24 is 0x64");
    CheckEqI32((i32)pPix[44 * 240 + 166], SolPix(0xB4), "the right edge at 166,23 is 0xB4 down to y 44");
    Check(pPix[22 * 240 + 69] == 0, "nothing is drawn above the top edge");
    Check(pPix[45 * 240 + 69] == 0, "  or below the bottom one");

    /* The side rails at x 2 and x 235: a climbing 1x8 on a 7 step (so they overlap by one row), and a
     * falling 1x6 on a 6 step from y 174. */
    CheckEqI32((i32)pPix[6 * 240 + 2], SolPix(100), "the left rail starts at 5 with 5*(0+20) = 100");
    CheckEqI32((i32)pPix[166 * 240 + 2], SolPix(215), "  and the 24th step at 5 + 7*23 = 166 is 215");
    CheckEqI32((i32)pPix[174 * 240 + 2], SolPix(230), "the falling half starts at 174 with 0xE6 - 0 = 230");
    CheckEqI32((i32)pPix[317 * 240 + 2], SolPix(115), "  and the last 1x6 at 174 + 6*23 ends at 317 as 115");
    CheckEqI32((i32)pPix[11 * 240 + 235], SolPix(100), "the right rail climbs with the same values");
    CheckEqI32((i32)pPix[317 * 240 + 235], SolPix(115), "  and falls with them");
    /* The climbing blocks are 8 tall but step 7, so each one overwrites the previous block's last row. */
    CheckEqI32((i32)pPix[165 * 240 + 2], SolPix(210), "the 23rd block (5 + 7*22 = 159) still owns y 165");
    CheckEqI32((i32)pPix[166 * 240 + 2], SolPix(215), "  and the 24th takes 166, its own first row");
    CheckEqI32((i32)pPix[173 * 240 + 2], SolPix(215), "the last climbing block reaches 166 + 7 = 173");
    CheckEqI32((i32)pPix[179 * 240 + 2], SolPix(230), "the falling blocks are 6 tall on a 6 step, so "
                                                      "the first one owns 174..179 with no overlap");
    CheckEqI32((i32)pPix[180 * 240 + 2], SolPix(225), "  and the next starts at 180 with 0xE6 - 5 = 225");

    /* The top and bottom rails: 24 blocks of 5, climbing on the left half and falling on the right. */
    CheckEqI32((i32)pPix[5 * 240 + 1], SolPix(100), "the top rail starts at x 1 with 100");
    CheckEqI32((i32)pPix[5 * 240 + 116], SolPix(215), "  and the climbing half ends at 1 + 5*23 = 116");
    CheckEqI32((i32)pPix[5 * 240 + 121], SolPix(250), "the falling half starts at 119 + 0 = 119 with 0xFA = 250");
    CheckEqI32((i32)pPix[5 * 240 + 234], SolPix(135), "  and the last block at 119 + 115 = 234 is 135");
    CheckEqI32((i32)pPix[315 * 240 + 1], SolPix(100), "the bottom rail mirrors the top one");
    CheckEqI32((i32)pPix[315 * 240 + 234], SolPix(135), "  down to the same last block");
    CheckEqI32((i32)pPix[5 * 240 + 119], SolPix(215), "the two halves overlap at 119..120 and the later "
                                                      "climbing block wins, as in the original's order");
    Check(pPix[4 * 240 + 1] == 0, "the rails are one row thick");
    Check(pPix[6 * 240 + 1] == 0, "  at y 5 and y 315 only");

    /* The guards the original does not have. */
    memset(g_solSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    g_solEnv.pPad = NULL;
    g_solEnv.pPanelNames = NULL;
    g_solEnv.pPanelCraft = NULL;
    CheckEqI32(SoldierScreen_Draw(&g_solEnv), 0, "a missing pad or panel is skipped, not dereferenced");
    Check(pPix[0] == 0, "the pad blit is skipped");
    CheckEqI32((i32)pPix[23 * 240 + 69], SolPix(0xB4), "  while the frame still draws");

    CheckEqI32(SoldierScreen_Draw(NULL), 0, "a null env does nothing");
}

static void TestOptionsAudioTab(void)
{
    i32 nBad;

    printf("OptionsScreen_Init @ 0x6be74 and the volume radio group\n");

    memset(&g_optCtl, 0, sizeof(g_optCtl));
    memset(&g_optScratch, 0, sizeof(g_optScratch));
    for (i32 i = 0; i < 7; ++i) {
        g_optCtl.apList[i] = &g_aOptList[i];
    }
    for (i32 i = 0; i < 6; ++i) {
        g_optCtl.apMusic[i] = &g_aOptMusic[i];
        g_optCtl.apSfx[i] = &g_aOptSfx[i];
    }
    g_ctlSave.uWidth = 30; /* the hotspots are pre-existing geoscape controls */
    g_ctlBack.uWidth = 50;

    g_nMusicVol = 3;
    g_bMusicOff = 1;
    g_nSfxVol = 2;
    g_bSfxOff = 0;
    g_bOptBuilt = 0;
    g_nSetupCalls = 0;

    g_optVol.pCtl = &g_optCtl;
    g_optVol.pnMusicVol = &g_nMusicVol;
    g_optVol.pbMusicOff = &g_bMusicOff;
    g_optVol.pnSfxVol = &g_nSfxVol;
    g_optVol.pbSfxOff = &g_bSfxOff;

    g_optTab.pCtl = &g_optCtl;
    g_optTab.pScratch = &g_optScratch;
    g_optTab.pSaveCtl = &g_ctlSave;
    g_optTab.pBackCtl = &g_ctlBack;
    g_optTab.pfnBindSaveSlots = OptTabSave;
    g_optTab.pfnBindLoadSlots = OptTabLoad;
    g_optTab.pfnSetTab3 = OptTab3;
    g_optTab.pfnSetTab4 = OptTab4;
    g_optTab.apfnSaveSlot[0] = OptSave0;
    g_optTab.apfnSaveSlot[1] = OptSave1;
    g_optTab.apfnSaveSlot[2] = OptSave2;
    g_optTab.apfnSaveSlot[3] = OptSave3;
    g_optTab.apfnSaveSlot[4] = OptSave4;
    g_optTab.apfnSaveSlot[5] = OptSave5;
    g_optTab.apfnSaveSlot[6] = OptSave6;
    g_optTab.apfnLoadSlot[0] = OptLoad0;
    g_optTab.apfnLoadSlot[1] = OptLoad1;
    g_optTab.apfnLoadSlot[2] = OptLoad2;
    g_optTab.apfnLoadSlot[3] = OptLoad3;
    g_optTab.apfnLoadSlot[4] = OptLoad4;
    g_optTab.apfnLoadSlot[5] = OptLoad5;
    g_optTab.apfnLoadSlot[6] = OptLoad6;
    g_optTab.pfnFormatSlots = OptFormatSlots;
    g_optTab.pUser = NULL;

    g_optInit.pCtl = &g_optCtl;
    g_optInit.pVolEnv = &g_optVol;
    g_optInit.pTabEnv = &g_optTab;
    g_optInit.pbControlsExist = &g_bOptBuilt;
    g_optInit.pSaveCtl = &g_ctlSave;
    g_optInit.pBackCtl = &g_ctlBack;
    g_optInit.pfnSaveAudio = OptSaveAudio;
    g_optInit.pfnReturnToTitle = OptReturnTitle;
    g_optInit.apfnMusicVol[0] = OptMusic0;
    g_optInit.apfnMusicVol[1] = OptMusic1;
    g_optInit.apfnMusicVol[2] = OptMusic2;
    g_optInit.apfnMusicVol[3] = OptMusic3;
    g_optInit.apfnMusicVol[4] = OptMusic4;
    g_optInit.apfnMusicVol[5] = OptMusic5;
    g_optInit.apfnSfxVol[0] = OptSfx0;
    g_optInit.apfnSfxVol[1] = OptSfx1;
    g_optInit.apfnSfxVol[2] = OptSfx2;
    g_optInit.apfnSfxVol[3] = OptSfx3;
    g_optInit.apfnSfxVol[4] = OptSfx4;
    g_optInit.apfnSfxVol[5] = OptSfx5;
    g_optInit.pfnLoadPads = Opt_LoadPads;
    g_optInit.pPadUser = NULL;

    OptionsScreen_Init(&g_optInit);

    CheckEqI32(g_bOptBuilt, 1, "the bring-up branch builds the controls (0x6BE90)");
    CheckEqI32(g_nSetupCalls, 19, "seven rows plus twelve radio buttons get positioned (0x6BEF0, 0x6BF90)");
    Check(strcmp(g_szRowPad0, "firebtn") == 0 && strcmp(g_szRowPad1, "cfirebtn") == 0,
          "the rows use the 0xAB750 record firebtn/cfirebtn");
    Check(strcmp(g_szVolPad0, "initg") == 0 && strcmp(g_szVolPad1, "cinitg") == 0,
          "the buttons use the 0xAB830 record initg/cinitg");

    nBad = 0;
    for (i32 i = 0; i < 6; ++i) {
        if (g_aOptMusic[i].y != 107 || g_aOptMusic[i].x != 46 + 25 * i) {
            nBad += 1;
        }
        if (g_aOptSfx[i].y != 147 || g_aOptSfx[i].x != 46 + 25 * i) {
            nBad += 1;
        }
    }
    CheckEqI32(nBad, 0, "music at y 107 and sfx at y 147, both x = 46 + 25*i (0x6BFBC..0x6C018)");

    Check(g_aOptMusic[0].pfnClick == OptMusic0 && g_aOptMusic[5].pfnClick == OptMusic5
          && g_aOptSfx[0].pfnClick == OptSfx0 && g_aOptSfx[5].pfnClick == OptSfx5,
          "the twelve click slots hold the twelve radio handlers (0x6C02C..0x6C0B0)");
    Check(g_ctlSave.pfnClick == OptSaveAudio, "unk_186BB4 gets the save handler (0x6BF1C)");
    Check(g_ctlBack.pfnClick == OptReturnTitle, "unk_186BB0 gets the return handler (0x6BF2C)");

    /* RefreshVolumeHighlight ran last: music was muted, sfx was not. */
    CheckEqI32(g_nMusicVol, 0, "a muted group resets its step to 0 (0x6B91C)");
    CheckEqI32(g_aOptMusic[0].nState, 2, "  and lights button 0 (0x6B944)");
    CheckEqI32(g_aOptMusic[1].nState, 0, "  the rest of the group stays clear");
    CheckEqI32(g_nSfxVol, 2, "an unmuted group keeps its stored step (0x6B93C)");
    CheckEqI32(g_aOptSfx[2].nState, 2, "  and lights that step's button (0x6B984)");

    CheckEqI32(g_optScratch.nLineAux145, 0, "LayoutButtons selects the audio tab (0x6B6D0)");
    Check(g_aOptList[0].pfnClick == OptTabSave && g_aOptList[1].pfnClick == OptTabLoad
          && g_aOptList[2].pfnClick == OptTab3 && g_aOptList[3].pfnClick == OptTab4,
          "the first four rows get the four tab handlers (0x6B6DC..0x6B700)");
    nBad = 0;
    for (i32 i = 0; i < 4; ++i) {
        if (g_aOptList[i].x != 120 - (g_aOptList[i].uWidth >> 1) || g_aOptList[i].y != 90 + 25 * i) {
            nBad += 1;
        }
    }
    CheckEqI32(nBad, 0, "those four centre at x = 120 - (uWidth >> 1), y = 90 + 25*i (0x6B718)");
    CheckEqI32(g_aOptList[4].y, 229, "the fifth row keeps the descriptor's y — only four move");
    CheckEqI32(g_ctlSave.x, 105, "the save hotspot centres on its OWN 30-wide pad (0x6B75C)");
    CheckEqI32(g_ctlSave.y, 198, "  and sits at y 198 (0x6B784)");
    CheckEqI32(g_ctlBack.x, 105, "the return hotspot uses the SAVE width, not its own 50 (0x6B778)");

    (void)OptionsScreen_SetMusicVol3(&g_optVol);
    CheckEqI32(g_nMusicVol, 3, "SetMusicVol3 writes step 3 (0x6BAD0)");
    CheckEqI32(g_bMusicOff, 0, "  and unmutes (0x6BB04)");
    CheckEqI32(g_aOptMusic[3].nState, 2, "  and lights button 3 (0x6BAF8)");
    CheckEqI32(g_aOptMusic[0].nState, 0, "  the group clear took button 0's light away (0x6BAD4)");

    (void)OptionsScreen_SetMusicVol0(&g_optVol);
    CheckEqI32(g_bMusicOff, 1, "music step 0 is the mute step (0x6BA5C)");
    CheckEqI32(g_aOptMusic[0].nState, 2, "  and lights button 0 (0x6BA50)");

    (void)OptionsScreen_SetSfxVol0(&g_optVol);
    CheckEqI32(g_nSfxVol, 0, "SetSfxVol0 writes step 0 (0x6BC38)");
    CheckEqI32(g_bSfxOff, 1, "  and mutes sfx (0x6BC54)");
    CheckEqI32(g_aOptSfx[0].nState, 2, "  and lights button 0 (0x6BC48)");
    CheckEqI32(g_aOptSfx[2].nState, 0, "  the group was cleared first (0x6BC20)");

    /* The level the applier hands the backend: 4 * (muted ? 0 : 12 * step + 4) - 1. */
    Sound_SetBackend(&g_fakeOptBackend);
    g_nSfxLevel = 999;
    g_nSfxLevelCalls = 0;
    (void)OptionsScreen_SetSfxVol0(&g_optVol);
    CheckEqI32(g_nSfxLevel, -1, "muted sfx applies -1, the original's arithmetic (0x9E470)");
    (void)OptionsScreen_SetSfxVol1(&g_optVol);
    CheckEqI32(g_nSfxLevel, 63, "step 1 applies 4 * (12 * 1 + 4) - 1 = 63 (0x9E488)");
    (void)OptionsScreen_SetSfxVol5(&g_optVol);
    CheckEqI32(g_nSfxLevel, 255, "step 5 applies 255");
    CheckEqI32(g_nSfxLevelCalls, 3, "one applier call per handler");
    Sound_SetBackend(NULL);

    g_nSlotFired = -1;
    OptionsScreen_BindSaveSlots(&g_optTab);
    CheckEqI32(g_optScratch.nLineAux145, 1, "the save tab is scratch byte 1 (0x6B4A8)");
    Check(g_aOptList[0].pfnClick == OptSave0 && g_aOptList[6].pfnClick == OptSave6,
          "all seven rows get their save-slot handler (0x6B4B4..0x6B4FC)");
    nBad = 0;
    for (i32 i = 0; i < 7; ++i) {
        if (g_aOptList[i].y != 40 + 22 * i) {
            nBad += 1;
        }
    }
    CheckEqI32(nBad, 0, "seven rows at y = 40 + 22*i (0x6B538)");
    CheckEqI32(g_ctlSave.y, 215, "the save hotspot drops to y 215 (0x6B550)");
    CheckEqI32(g_nSlotFired, 7, "  and the slot list is formatted (0x6B558)");

    OptionsScreen_BindLoadSlots(&g_optTab);
    CheckEqI32(g_optScratch.nLineAux145, 2, "the load tab is scratch byte 2 (0x6B594)");
    Check(g_aOptList[0].pfnClick == OptLoad0 && g_aOptList[6].pfnClick == OptLoad6,
          "  and the rows get the load-slot handlers (0x6B5A0..0x6B5E8)");

    /* The installed handlers drive the real setters. */
    if (g_aOptMusic[4].pfnClick != NULL) {
        g_aOptMusic[4].pfnClick();
    }
    CheckEqI32(g_nMusicVol, 4, "clicking the installed handler drives the setter");

    g_nSetupCalls = 0;
    OptionsScreen_Init(&g_optInit); /* the flag is set now, so this is the teardown branch */
    CheckEqI32(g_bOptBuilt, 0, "the second call tears the controls down (0x6C0FC)");
    CheckEqI32(g_nSetupCalls, 0, "  and positions nothing");
    CheckEqI32(g_aOptMusic[0].y, 107, "  the positions survive it (only the pads are dropped)");

    CheckEqI32(OptionsScreen_SetMusicVol(NULL, 0), 0, "a null volume env changes nothing");
    Check(OptionsScreen_LayoutButtons(NULL) == NULL, "a null tab env lays out nothing");
    OptionsScreen_Init(NULL);
    Check(true, "a null init env is tolerated");
}
/* SaveGame_Slot0..6_CloseUI @ 0x6af80..0x6b148 and LoadGame_Slot0..6_InitOptions @ 0x6b194..0x6b404.
 * Both drivers are ported, so the handlers run for real. The slot path names a directory that does
 * not exist, which is the one input that makes each driver fail before it touches any save state. */

static i32 g_nSlotListMode;
static i32 g_nSlotListModeAtRender;
static i32 g_nSlotListModeAtClose;
static i32 g_nSlotRenderCalls;
static i32 g_nSlotCloseCalls;

static void Slot_RenderFrame(void *pUser)
{
    (void)pUser;
    g_nSlotRenderCalls += 1;
    g_nSlotListModeAtRender = g_nSlotListMode;
}

static void Slot_CloseUi(void *pUser)
{
    (void)pUser;
    g_nSlotCloseCalls += 1;
    g_nSlotListModeAtClose = g_nSlotListMode;
}

static void TestSaveLoadSlots(void)
{
    static DatFile dat;
    static SaveGameInput in;
    static LoadGameEnv loadEnv;
    static LoadGameHooks loadHooks;
    static SaveSlotEnv env;
    static i32 nGeoscapeScreen;
    static u8 bControlsExist;
    static ScreenScratch scratch;

    printf("SaveGame_SlotN_CloseUI @ 0x6af80 and LoadGame_SlotN_InitOptions @ 0x6b194\n");

    memset(&in, 0, sizeof(in));
    in.pszAppDir = "pufo_slot_test_dir_zz\\";
    in.pfnRenderFrame = Slot_RenderFrame;

    memset(&loadHooks, 0, sizeof(loadHooks));
    memset(&loadEnv, 0, sizeof(loadEnv));
    loadEnv.pszAppDir = "pufo_slot_test_dir_zz\\";
    loadEnv.pHooks = &loadHooks;

    memset(&env, 0, sizeof(env));
    env.pDat = &dat;
    env.pIn = &in;
    env.pLoadEnv = &loadEnv;
    env.pnListMode = &g_nSlotListMode;
    env.pScratch = &scratch;
    env.pbControlsExist = &bControlsExist;
    env.pnGeoscapeScreen = &nGeoscapeScreen;
    env.pOptionsInit = &g_optInit; /* the options fixture the previous test built */
    env.pfnCloseUi = Slot_CloseUi;

    /* The save handler. The render hook is the only place the transient mode 4 is visible. */
    g_nSlotListMode = 0;
    bControlsExist = 0;
    nGeoscapeScreen = 7;
    scratch.nLineAux145 = 9;
    g_nSlotRenderCalls = 0;
    g_nSlotCloseCalls = 0;

    DatFile_ctor(&dat);
    SaveGame_Slot3_CloseUI(&env);
    DatFile_Close(&dat);

    CheckEqI32(in.nSlot, 3, "the handler hands slot 3 to SaveGame (0x6B07C)");
    CheckEqI32(g_nSlotRenderCalls, 1, "SaveGame renders a frame before it opens the file (0x4499C)");
    CheckEqI32(g_nSlotListModeAtRender, 4, "  and the list mode is 4 while it writes (0x6AF90)");
    CheckEqI32(scratch.nLineAux145, 0, "the tab byte goes back to 0 (0x6AFA4)");
    CheckEqI32(g_nSlotCloseCalls, 1, "Geoscape_CloseUI runs last (0x6AFB8)");
    CheckEqI32(g_nSlotListModeAtClose, 1, "  with the list mode already back to 1 (0x6AFAC)");

    /* The load handler: the flag it sets makes OptionsScreen_Init take its teardown branch. */
    g_nSlotListMode = 0;
    bControlsExist = 0;
    nGeoscapeScreen = 7;
    scratch.nLineAux145 = 9;
    g_bOptBuilt = 1;
    g_nSetupCalls = 0;

    DatFile_ctor(&dat);
    CheckEqI32(LoadGame_Slot5_InitOptions(&env), 1, "a missing slot is the failure path, which returns 1 (0x6B1E0)");
    DatFile_Close(&dat);

    CheckEqI32(loadEnv.nSlot, 5, "the handler hands slot 5 to LoadGame (0x6B1D4)");
    CheckEqI32(scratch.nLineAux145, 0, "the tab byte is cleared first (0x6B1A4)");
    CheckEqI32(nGeoscapeScreen, 0, "the geoscape screen id is cleared (0x6B1B0)");
    CheckEqI32(g_nSlotListMode, 1, "the failed load puts the list mode back to 1 (0x6B1E0)");
    CheckEqI32(g_bOptBuilt, 0, "OptionsScreen_Init ran and took its teardown branch (0x6B1C4, 0x6B1C8)");
    CheckEqI32(g_nSetupCalls, 0, "  which positions nothing");

    CheckEqI32(LoadGame_Slot_InitOptions(NULL, 0), 0, "a null slot env loads nothing");
    SaveGame_Slot0_CloseUI(NULL);
    Check(true, "a null slot env saves nothing");
}

/* --- Base_EnterBuildMode @ 0x84e20 and Smap_SetWaypoint @ 0x85440 --- */

static i32 g_aBuildSamples[4];
static i32 g_nBuildSamples;

static void BuildPlaySample(u32 nSample, void *pUser)
{
    (void)pUser;
    if (g_nBuildSamples < 4) {
        g_aBuildSamples[g_nBuildSamples] = (i32)nSample;
    }
    g_nBuildSamples++;
}

/* Cell offset inside one base's 36-cell block, in the order the two passes visit it:
 * row 0 across all six columns, then row 1. The pass writes 6*col + row. */
#define GRID_CELL(nBase, nRow, nCol) (480 + 144 * (nBase) + 6 * (nCol) + (nRow))
#define GRID_NEED(nBase, nRow, nCol) (444 + 144 * (nBase) + 6 * (nCol) + (nRow))

static void TestEnterBuildMode(void)
{
    static u8 aGrid[1524];
    static u16 aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * 8];
    static u16 aResearch[PUFO_RESEARCH_TOPIC_COUNT * 8];
    static NavControls ctl;
    static NavCtlEnv navEnv;
    static UiControl parkCtl;
    i8 nBase;
    u16 nEntries;
    i32 nScreen;
    BaseBuildModeEnv env;
    i32 i;

    printf("Base_EnterBuildMode @ 0x84e20\n");

    memset(&navEnv, 0, sizeof(navEnv));
    navEnv.pfnLoadPads = NavLoadPads;

#define BUILD_BEGIN()                                                        \
    memset(aGrid, 0, sizeof(aGrid));                                         \
    memset(aManuf, 0, sizeof(aManuf));                                       \
    memset(aResearch, 0, sizeof(aResearch));                                 \
    memset(&ctl, 0, sizeof(ctl));                                            \
    memset(&parkCtl, 0, sizeof(parkCtl));                                    \
    parkCtl.x = 3; parkCtl.y = 4;                                            \
    nBase = 2;                                                               \
    nEntries = 4;                                                            \
    nScreen = 9;                                                             \
    g_nBuildSamples = 0;                                                     \
    g_nPadLoads = 0;                                                         \
    memset(&env, 0, sizeof(env));                                            \
    env.pNavOut = &ctl; env.pNavEnv = &navEnv;                               \
    env.abFacilityGrid = aGrid;                                              \
    env.apManufCols = aManuf; env.apResearchCols = aResearch;                \
    env.puResearchEntries = &nEntries;                                       \
    env.pnSelectedBaseIdx = &nBase;                                          \
    env.pnScreenId = &nScreen;                                               \
    env.pParkCtl = &parkCtl;                                                 \
    env.pfnPlaySample = BuildPlaySample;

    /* 1. the whole handler runs, in order, on a base with no facilities at all. */
    BUILD_BEGIN();
    Base_EnterBuildMode(&env);
    CheckEqI32(g_nBuildSamples, 1, "one sample played");
    CheckEqI32(g_aBuildSamples[0], 17, "  sample 17 (0x84E2C)");
    CheckEqI32(nScreen, 1, "the geoscape screen id becomes 1 (0x84E64)");
    CheckEqI32(g_nPadLoads, PUFO_NAV_CTLS + PUFO_NAV_PANEL_CTLS + PUFO_NAV_FIND_CTLS,
               "the fifteen nav controls were rebuilt (0x84E6C)");
    CheckEqI32(parkCtl.x, 1000, "unk_186BB4 parked at x 1000 (0x84E80)");
    CheckEqI32(parkCtl.y, 1000, "  and y 1000 (0x84E88)");
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        free(ctl.apNav[i]);
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        free(ctl.apPanel[i]);
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        free(ctl.apFind[i]);
    }

    /* 2. the workshop pass: three workshop cells, a quantity total of 60, so the budget is
     * 60/50 + 1 = 2. The first two cells in visit order get 1, the third gets 0. */
    BUILD_BEGIN();
    aGrid[GRID_CELL(2, 0, 0)] = 3;
    aGrid[GRID_CELL(2, 0, 1)] = 3;
    aGrid[GRID_CELL(2, 0, 2)] = 3;
    aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * 2 + 0] = 30;
    aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * 2 + 1] = 30;
    aGrid[GRID_NEED(2, 0, 2)] = 1; /* stale, must be overwritten with 0 */
    Base_EnterBuildMode(&env);
    CheckEqI32(aGrid[GRID_NEED(2, 0, 0)], 1, "the first workshop cell is marked needed");
    CheckEqI32(aGrid[GRID_NEED(2, 0, 1)], 1, "  the second spends the rest of the budget");
    CheckEqI32(aGrid[GRID_NEED(2, 0, 2)], 0, "  the third is cleared, not left stale");
    CheckEqI32(aGrid[GRID_NEED(2, 0, 3)], 0, "  and a cell that is not a workshop is untouched");

    /* 3. the lab pass runs on the same grid with kind 2 and its own quantity column. */
    BUILD_BEGIN();
    aGrid[GRID_CELL(2, 1, 0)] = 2;
    aGrid[GRID_CELL(2, 1, 1)] = 2;
    aResearch[PUFO_RESEARCH_TOPIC_COUNT * 2 + 0] = 10; /* budget 10/50 + 1 = 1 */
    Base_EnterBuildMode(&env);
    CheckEqI32(aGrid[GRID_NEED(2, 1, 0)], 1, "the lab pass marks its own kind");
    CheckEqI32(aGrid[GRID_NEED(2, 1, 1)], 0, "  and stops when its budget runs out");

    /* 4. the two passes read different columns: manufacturing quantity does not feed the lab. */
    BUILD_BEGIN();
    aGrid[GRID_CELL(2, 0, 0)] = 3;
    aGrid[GRID_CELL(2, 1, 0)] = 2;
    aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * 2] = 500; /* workshop budget 11 */
    Base_EnterBuildMode(&env);
    CheckEqI32(aGrid[GRID_NEED(2, 0, 0)], 1, "the workshop cell is marked");
    CheckEqI32(aGrid[GRID_NEED(2, 1, 0)], 0, "  the lab cell sees a zero research total");

    /* 5. a zero total gives a zero budget, which still writes 0 over every matching cell. */
    BUILD_BEGIN();
    aGrid[GRID_CELL(2, 0, 0)] = 3;
    aGrid[GRID_NEED(2, 0, 0)] = 1;
    aGrid[GRID_CELL(2, 1, 0)] = 2;
    aGrid[GRID_NEED(2, 1, 0)] = 1;
    Base_EnterBuildMode(&env);
    CheckEqI32(aGrid[GRID_NEED(2, 0, 0)], 0, "no active manufacturing clears the workshop mark");
    CheckEqI32(aGrid[GRID_NEED(2, 1, 0)], 0, "  and no active research clears the lab mark");

    /* 6. only the selected base's 144-byte block is walked. */
    BUILD_BEGIN();
    aGrid[GRID_CELL(3, 0, 0)] = 3;
    aGrid[GRID_NEED(3, 0, 0)] = 1;
    aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * 3] = 500;
    Base_EnterBuildMode(&env);
    CheckEqI32(aGrid[GRID_NEED(3, 0, 0)], 1,
               "base 3 keeps its mark: the passes use nSelectedBaseIdx, not a scan");

    /* 7. the count is an unsigned byte, so entries past 255 are outside the sum. */
    BUILD_BEGIN();
    nEntries = 300;
    aGrid[GRID_CELL(2, 0, 0)] = 3;
    aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * 2 + 255] = 9000;
    Base_EnterBuildMode(&env);
    CheckEqI32(aGrid[GRID_NEED(2, 0, 0)], 0,
               "a count of 300 is capped at 255, so the 9000 at index 255 is not summed");

    /* 8. deviation: an index outside 0..7 is refused. The original sign-extends the byte and
     * walks whatever block that lands on -- base 8 would write needed at grid[1632..1667], past
     * the 1524-byte grid, and read the quantity column 400 bytes past its eight blocks. */
    BUILD_BEGIN();
    nBase = -1;
    aGrid[GRID_CELL(0, 0, 0)] = 3;
    aGrid[GRID_NEED(0, 0, 0)] = 1;
    aManuf[0] = 500;
    Base_EnterBuildMode(&env);
    CheckEqI32(aGrid[GRID_NEED(0, 0, 0)], 1, "a negative base index marks nothing");
    CheckEqI32(nScreen, 1, "  but the rest of the handler still runs");

    BUILD_BEGIN();
    nBase = 8;
    aGrid[GRID_CELL(7, 0, 0)] = 3;
    aGrid[GRID_NEED(7, 0, 0)] = 1;
    Base_EnterBuildMode(&env);
    CheckEqI32(aGrid[GRID_NEED(7, 0, 0)], 1, "base index 8 is refused too, so nothing is written");

    /* 9. without a grid the two passes are skipped and the widget work still happens. */
    BUILD_BEGIN();
    env.abFacilityGrid = NULL;
    env.apManufCols = NULL;
    env.apResearchCols = NULL;
    Base_EnterBuildMode(&env);
    CheckEqI32(nScreen, 1, "no grid: the screen id is still set");
    CheckEqI32(g_nPadLoads, 15, "  the nav controls are still built");
    CheckEqI32(parkCtl.x, 1000, "  and the control is still parked");
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        free(ctl.apNav[i]);
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        free(ctl.apPanel[i]);
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        free(ctl.apFind[i]);
    }

    Base_EnterBuildMode(NULL);
    Check(true, "a null env does nothing");
#undef BUILD_BEGIN
#undef GRID_CELL
#undef GRID_NEED
}

static void TestSetWaypoint(void)
{
    static UiLineList lines;
    static GameTime clock;
    static Ship *apCraft[2];
    static Ship aCraft[2];
    static GeoscapeWorld world;
    static PtrArray list;
    static ShipRemovalEnv shipEnv;
    static StrategItem item;
    u8 nShipCount;
    i32 nStratIdx, nShipSel;
    SmapWaypointEnv env;

    printf("Smap_SetWaypoint @ 0x85440\n");

#define WP_BEGIN(nColor0)                                                    \
    memset(&lines, 0, sizeof(lines));                                        \
    lines.nLineCount = 2;                                                    \
    lines.abLineFlags[0] = 7;                                                \
    lines.abLineFlags[1] = 8;                                                \
    lines.anColor[0][0] = (u32)(nColor0);                                    \
    lines.anColor[1][0] = 999u;                                              \
    memset(&clock, 0, sizeof(clock));                                        \
    clock.bPaused = 0;                                                       \
    memset(aCraft, 0, sizeof(aCraft));                                       \
    aCraft[0].bId = 41;                                                      \
    aCraft[1].bId = 42;                                                      \
    aCraft[1].bIntercepting = 1;                                             \
    aCraft[1].nStrategIdx = 0;                                               \
    apCraft[0] = &aCraft[0]; apCraft[1] = &aCraft[1];                        \
    memset(&world, 0, sizeof(world));                                        \
    world.apCraft = apCraft; world.nShipCount = 2;                           \
    nShipCount = 2;                                                          \
    PtrArray_ctor(&list);                                                    \
    memset(&item, 0, sizeof(item));                                          \
    item.nType = 1; item.bActive = 0; item.nDisplayId = 42;                  \
    item.nTimer = 1234;                                                      \
    PtrArray_Add(&list, &item);                                              \
    memset(&shipEnv, 0, sizeof(shipEnv));                                    \
    shipEnv.apCraft = apCraft; shipEnv.pnShipCount = &nShipCount;            \
    shipEnv.pStrategList = &list; shipEnv.pWorld = &world;                   \
    nStratIdx = -1; nShipSel = -1;                                           \
    memset(&env, 0, sizeof(env));                                            \
    env.pLines = &lines; env.pnStrategIndex = &nStratIdx;                    \
    env.pnSelectedShipIdx = &nShipSel; env.pShipEnv = &shipEnv;              \
    env.pTime = &clock;

    /* 1. the ship index lives in colour slot 0 of message line 0. */
    WP_BEGIN(1);
    Smap_SetWaypoint(&env);
    CheckEqI32(nShipSel, 1, "colour slot 0 of line 0 is the ship index (0x85460)");
    CheckEqI32(nStratIdx, 1, "  0x159E74 mirrors it (0x85464)");
    CheckEqI32(aCraft[1].bIntercepting, 0, "the craft's intercept flag is cleared (0x5A8B8)");
    CheckEqI32(item.nTimer, 0xFFFF, "its strateg timer is parked, not restarted (0x5A8B0)");
    CheckEqI32(lines.nLineCount, 2, "drop line 0 then insert at 0 keeps the count");
    CheckEqI32(lines.abLineFlags[0], 11, "the re-inserted line carries flag 11 (0x85494)");
    CheckEqI32((i32)lines.anColor[0][0], 1, "  colour slot 0 is the ship index");
    CheckEqI32((i32)lines.anColor[0][1], 0, "  slots 1..3 are the three zero stack bytes");
    CheckEqI32(lines.abLineFlags[1], 8, "the old line 1 slid back to 1");
    CheckEqI32((i32)lines.anColor[1][0], 999, "  and its record moved with it");
    CheckEqI32(clock.bPaused, 1, "the clock is paused (0x854B0)");

    /* 2. the ship index reaches the two globals as a full dword, but the colour slot is the
     * byte the original reloaded with LDRB at 0x85490 and InsertLine masks at 0x5A2F4. */
    WP_BEGIN(300);
    Smap_SetWaypoint(&env);
    CheckEqI32(nShipSel, 300, "the global keeps the whole dword");
    CheckEqI32(nStratIdx, 300, "  both of them do");
    CheckEqI32((i32)lines.anColor[0][0], 44, "the colour slot holds only the low byte (300 & 0xFF)");
    CheckEqI32(aCraft[1].bIntercepting, 1, "  and the out-of-range index clears nothing");
    CheckEqI32(item.nTimer, 1234, "  nor parks any timer");

    /* 3. a craft with no type-1 inactive entry still has its intercept flag cleared. */
    WP_BEGIN(0);
    Smap_SetWaypoint(&env);
    CheckEqI32(aCraft[0].bIntercepting, 0, "the flag is cleared even with no matching entry");
    CheckEqI32(item.nTimer, 1234, "  the entry for craft 42 is not the one for craft 41");
    CheckEqI32((i32)lines.anColor[0][0], 0, "  ship index 0 still re-inserts line 0");

    /* 4. the optional seams. */
    WP_BEGIN(1);
    env.pTime = NULL;
    Smap_SetWaypoint(&env);
    CheckEqI32(lines.abLineFlags[0], 11, "no clock: the message line is still rewritten");
    CheckEqI32(nShipSel, 1, "  and the ship index is still stored");

    WP_BEGIN(1);
    env.pShipEnv = NULL;
    Smap_SetWaypoint(&env);
    CheckEqI32(aCraft[1].bIntercepting, 1, "no ship env: nothing is cleared on the craft");
    CheckEqI32(lines.abLineFlags[0], 11, "  the line work still runs");

    WP_BEGIN(1);
    env.pnStrategIndex = NULL;
    env.pnSelectedShipIdx = NULL;
    Smap_SetWaypoint(&env);
    CheckEqI32(lines.abLineFlags[0], 11, "two null index outputs still leave the rest intact");

    Smap_SetWaypoint(NULL);
    Check(true, "a null env does nothing");
    WP_BEGIN(1);
    env.pLines = NULL;
    Smap_SetWaypoint(&env);
    CheckEqI32(clock.bPaused, 0, "a null line list stops the whole handler, pause included");
#undef WP_BEGIN
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
    TestResearchQty();
    TestResearchAllocate();
    TestManufQty();
    TestScreenSelect();
    TestFindAtBase();
    TestTransferFlag();
    TestNewBaseScreen();
    TestBuildNavControls();
    TestRadarListClick();
    TestBaseFacScreenInit();
    TestMapClick();
    TestBaseFacToggle();
    TestBaseFacBuy();
    TestSoldierScreenToggle();
    TestSoldierSelectListed();
    TestSoldierEquipSelected();
    TestFacOverlay();
    TestBuildDialog();
    TestSoldierDisplayName();
    TestSoldierScreenDraw();
    TestOptionsAudioTab();
    TestSaveLoadSlots();
    TestEnterBuildMode();
    TestSetWaypoint();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
