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

static UiPanel g_aScrollPanels[5];

static void ResetScrollPanels(void)
{
    for (i32 i = 0; i < 5; ++i) {
        memset(&g_aScrollPanels[i], 0, sizeof(g_aScrollPanels[i]));
        g_aScrollPanels[i].nUnk3028 = 2;
        g_aScrollPanels[i].nLineCount = 10;
        g_aScrollPanels[i].nStyle = 4; /* so next stops at 10 - 4 == 6 */
    }
}

static void TestPanelScrollFamily(void)
{
    printf("the panel scroll family\n");

    UiPanel *apFour[4] = { &g_aScrollPanels[0], &g_aScrollPanels[1],
                           &g_aScrollPanels[2], &g_aScrollPanels[3] };
    UiPanel *apFive[5] = { &g_aScrollPanels[0], &g_aScrollPanels[1], &g_aScrollPanels[2],
                           &g_aScrollPanels[3], &g_aScrollPanels[4] };

    ResetScrollPanels();
    ManufScreen_PanelNext(apFour);
    CheckEqI32(g_aScrollPanels[0].nUnk3028, 3, "ManufScreen_PanelNext pages its four");
    CheckEqI32(g_aScrollPanels[3].nUnk3028, 3, "the tail-called one last, same effect");
    CheckEqI32(g_aScrollPanels[4].nUnk3028, 2, "and the fifth panel is not in that set");
    ManufScreen_PanelPrev(apFour);
    CheckEqI32(g_aScrollPanels[0].nUnk3028, 2, "ManufScreen_PanelPrev moves them back");

    PsiScreen_PanelNext(apFour);
    CheckEqI32(g_aScrollPanels[2].nUnk3028, 3, "PsiScreen_PanelNext pages its own four");
    PsiScreen_PanelPrev(apFour);
    CheckEqI32(g_aScrollPanels[2].nUnk3028, 2, "and PsiScreen_PanelPrev returns them");

    BuySellScreen_PanelNext(apFive);
    CheckEqI32(g_aScrollPanels[4].nUnk3028, 3, "the buy/sell set carries five panels");

    /* Next stops at nLineCount - nStyle and prev stops at zero: no wrap either way. */
    g_aScrollPanels[0].nUnk3028 = 6;
    BuySellScreen_PanelNext(apFive);
    CheckEqI32(g_aScrollPanels[0].nUnk3028, 6, "next stops at nLineCount - nStyle");
    for (i32 i = 0; i < 5; ++i) {
        g_aScrollPanels[i].nUnk3028 = 0;
    }
    BuySellScreen_PanelPrev(apFive);
    CheckEqI32(g_aScrollPanels[4].nUnk3028, 0, "prev stops at zero");

    BuySellScreen_DetailPanelNext(apFive);
    CheckEqI32(g_aScrollPanels[1].nUnk3028, 1, "the detail set pages the same way");
    BuySellScreen_DetailPanelPrev(apFive);
    CheckEqI32(g_aScrollPanels[1].nUnk3028, 0, "and back again");

    apFive[2] = NULL;
    BuySellScreen_PanelNext(apFive);
    CheckEqI32(g_aScrollPanels[3].nUnk3028, 1,
               "a NULL slot is stepped over and the panels after it still move");
    apFive[2] = &g_aScrollPanels[2];
}

/* One panel per global the dispatcher touches, in the order the disassembly loads them:
 * 0..1 soldier, 2..6 the hangar stores, 7..8 the hangar crew pair, 9 basefac, 10..12 research,
 * 13..16 manufacturing, 17..21 buy/sell, 22..25 psi, 26 the screen-16 panel. */
static UiPanel g_aFocusPanels[27];
static u8 g_bFocusFlagA;
static u8 g_bFocusFlagB;

static void ResetFocusPanels(void)
{
    for (i32 i = 0; i < 27; ++i) {
        memset(&g_aFocusPanels[i], 0, sizeof(g_aFocusPanels[i]));
        g_aFocusPanels[i].nUnk3028 = 2;
        g_aFocusPanels[i].nLineCount = 10;
        g_aFocusPanels[i].nStyle = 4; /* so next stops at 10 - 4 == 6 */
    }
    g_bFocusFlagA = 0;
    g_bFocusFlagB = 0;
}

static void FillFocusEnv(GeoscapeFocusEnv *pEnv, i32 nScreenId)
{
    memset(pEnv, 0, sizeof(*pEnv));
    pEnv->nScreenId = nScreenId;
    pEnv->pSolPanelNames = &g_aFocusPanels[0];
    pEnv->pSolPanelCraft = &g_aFocusPanels[1];
    pEnv->bPlaneStoresCommitted = 1;
    pEnv->bPlaneListPanelsBuilt = 1;
    for (i32 i = 0; i < 5; ++i) {
        pEnv->apPlanePanels[i] = &g_aFocusPanels[2 + i];
    }
    pEnv->pPlaneCrewPanel = &g_aFocusPanels[7];
    pEnv->pPlaneCrewStatusPanel = &g_aFocusPanels[8];
    pEnv->pBaseFacPanel = &g_aFocusPanels[9];
    for (i32 i = 0; i < 3; ++i) {
        pEnv->apResearchPanels[i] = &g_aFocusPanels[10 + i];
    }
    for (i32 i = 0; i < 4; ++i) {
        pEnv->apManufPanels[i] = &g_aFocusPanels[13 + i];
    }
    for (i32 i = 0; i < 5; ++i) {
        pEnv->apBuySellPanels[i] = &g_aFocusPanels[17 + i];
    }
    for (i32 i = 0; i < 4; ++i) {
        pEnv->apPsiPanels[i] = &g_aFocusPanels[22 + i];
    }
    pEnv->pScreen16Panel = &g_aFocusPanels[26];
    pEnv->pbFocusFlagA = &g_bFocusFlagA;
    pEnv->pbFocusFlagB = &g_bFocusFlagB;
}

static i32 CountMovedPanels(void)
{
    i32 nMoved = 0;
    for (i32 i = 0; i < 27; ++i) {
        if (g_aFocusPanels[i].nUnk3028 != 2) {
            ++nMoved;
        }
    }
    return nMoved;
}

static void TestFocusDispatcher(void)
{
    GeoscapeFocusEnv env;

    printf("the screen-id scroll dispatcher\n");

    ResetFocusPanels();
    FillFocusEnv(&env, 0);
    Geoscape_UiFocusPrev(&env);
    CheckEqI32(g_bFocusFlagA, 1, "screen 0 prev sets the unk_1864F4 flag byte");
    CheckEqI32(g_bFocusFlagB, 0, "and leaves unk_1864F6 alone");
    CheckEqI32(CountMovedPanels(), 0, "no panel moves on screen 0");
    Geoscape_UiFocusNext(&env);
    CheckEqI32(g_bFocusFlagB, 1, "screen 0 next sets unk_1864F6 instead");
    CheckEqI32(CountMovedPanels(), 0, "and still scrolls nothing");

    ResetFocusPanels();
    FillFocusEnv(&env, 2);
    Geoscape_UiFocusPrev(&env);
    Geoscape_UiFocusNext(&env);
    CheckEqI32(g_bFocusFlagA + g_bFocusFlagB, 2, "screen 2 shares the 0 case");

    ResetFocusPanels();
    FillFocusEnv(&env, 4);
    Geoscape_UiFocusNext(&env);
    CheckEqI32(g_aFocusPanels[0].nUnk3028, 3, "the soldier names panel pages");
    CheckEqI32(g_aFocusPanels[1].nUnk3028, 3, "and the craft panel");
    CheckEqI32(CountMovedPanels(), 2, "nothing else on the soldier screen moves");
    Geoscape_UiFocusPrev(&env);
    CheckEqI32(g_aFocusPanels[0].nUnk3028, 2, "prev returns them");

    ResetFocusPanels();
    FillFocusEnv(&env, 6);
    Geoscape_UiFocusNext(&env);
    CheckEqI32(CountMovedPanels(), 7, "the hangar pages five store panels plus the crew pair");
    CheckEqI32(g_aFocusPanels[6].nUnk3028, 3, "pPlaneAvailPanel is the last store panel");
    CheckEqI32(g_aFocusPanels[8].nUnk3028, 3, "pPlaneCrewStatusPanel is the tail call");

    ResetFocusPanels();
    FillFocusEnv(&env, 6);
    env.bPlaneStoresCommitted = 0;
    Geoscape_UiFocusNext(&env);
    CheckEqI32(CountMovedPanels(), 2,
               "with bPlaneStoresCommitted clear only the crew pair moves");

    ResetFocusPanels();
    FillFocusEnv(&env, 6);
    env.bPlaneListPanelsBuilt = 0;
    Geoscape_UiFocusNext(&env);
    CheckEqI32(CountMovedPanels(), 5,
               "with bPlaneListPanelsBuilt clear the handler stops after the five");
    CheckEqI32(g_aFocusPanels[7].nUnk3028, 2, "the crew panels stay put");

    ResetFocusPanels();
    FillFocusEnv(&env, 7);
    Geoscape_UiFocusPrev(&env);
    CheckEqI32(g_aFocusPanels[9].nUnk3028, 1, "the base facility panel pages on its own");
    CheckEqI32(CountMovedPanels(), 1, "and it is the only one");

    ResetFocusPanels();
    FillFocusEnv(&env, 8);
    Geoscape_UiFocusNext(&env);
    CheckEqI32(CountMovedPanels(), 3, "research pages its three panels");

    ResetFocusPanels();
    FillFocusEnv(&env, 9);
    Geoscape_UiFocusNext(&env);
    CheckEqI32(CountMovedPanels(), 4, "manufacturing pages its four");

    for (i32 nScreenId = 10; nScreenId <= 12; ++nScreenId) {
        ResetFocusPanels();
        FillFocusEnv(&env, nScreenId);
        Geoscape_UiFocusNext(&env);
        CheckEqI32(CountMovedPanels(), 5, "screens 10, 11 and 12 share the five buy/sell panels");
    }

    ResetFocusPanels();
    FillFocusEnv(&env, 16);
    Geoscape_UiFocusNext(&env);
    CheckEqI32(g_aFocusPanels[26].nUnk3028, 3, "screen 16 pages its single panel");
    CheckEqI32(CountMovedPanels(), 1, "and nothing else");

    ResetFocusPanels();
    FillFocusEnv(&env, 17);
    Geoscape_UiFocusPrev(&env);
    CheckEqI32(CountMovedPanels(), 4, "psi pages the four arrays of the selected base");

    static const i32 aDeadIds[6] = { 1, 3, 5, 13, 14, 15 };
    for (i32 i = 0; i < 6; ++i) {
        ResetFocusPanels();
        FillFocusEnv(&env, aDeadIds[i]);
        Geoscape_UiFocusPrev(&env);
        Geoscape_UiFocusNext(&env);
        CheckEqI32(CountMovedPanels(), 0, "an id outside the table scrolls nothing");
        CheckEqI32(g_bFocusFlagA + g_bFocusFlagB, 0, "and sets no flag byte");
    }

    /* The movers keep their own limits: next stops at nLineCount - nStyle, prev at zero. */
    ResetFocusPanels();
    FillFocusEnv(&env, 9);
    for (i32 i = 13; i < 17; ++i) {
        g_aFocusPanels[i].nUnk3028 = 6;
    }
    Geoscape_UiFocusNext(&env);
    CheckEqI32(g_aFocusPanels[16].nUnk3028, 6, "next stops at nLineCount - nStyle");
    for (i32 i = 13; i < 17; ++i) {
        g_aFocusPanels[i].nUnk3028 = 0;
    }
    Geoscape_UiFocusPrev(&env);
    CheckEqI32(g_aFocusPanels[13].nUnk3028, 0, "prev stops at zero");

    ResetFocusPanels();
    FillFocusEnv(&env, 4);
    env.pSolPanelCraft = NULL;
    Geoscape_UiFocusNext(&env);
    CheckEqI32(g_aFocusPanels[0].nUnk3028, 3, "a missing panel is stepped over");
    CheckEqI32(g_bFocusFlagA, 0, "and the flag pointers are only used by the 0/2 case");

    Geoscape_UiFocusPrev(NULL);
    Geoscape_UiFocusNext(NULL);
    Check(1, "a missing environment does nothing at all");
}

static i32 g_nBuySellPrev;
static i32 g_nBuySellNext;
static void BuySellTestPanelPrev(void) { ++g_nBuySellPrev; }
static void BuySellTestPanelNext(void) { ++g_nBuySellNext; }

static void TestBuySellBuildPanels(void)
{
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static Soldier aSol[3];
    static Soldier *apSol[3];
    static Ship aShip[3];
    static Ship *apShip[3];
    static u8 aText[(415 + 5) * PUFO_TEXT_REC_STRIDE];
    static const i32 aWantAux[8] = { 117, 1000, 1000, 112, 1, 122, 105, 2077 };
    static const char *const apWantName[8] = {
        "Skyranger-1", "AVeryLongSoldierN", "Chloe", "Engineer",
        "Pistol", "Personal armour", "Elerium-115", "Sectoid soldier"
    };
    static const i32 aWantQty[8] = { 1, 1, 1, 5, 3, 1, 2, 4 };
    UiPanel *pA = NULL, *pB = NULL, *pC = NULL, *pD = NULL, *pE = NULL;
    UiControl aCtl[4];
    BuySellBuildEnv env;
    u8 bBuilt = 0;
    i32 nAssigned = 7;
    i32 nIdCount = 9;
    i32 nFunds = 5000;
    i32 nOk = 0;
    i32 i;

    printf("the buy/sell list panels\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    PutStr(aObj[1].szName, "Pistol");
    PutStr(aObj[70].szName, "Operative corpse");
    PutStr(aObj[77].szName, "Sectoid soldier");
    PutStr(aObj[105].szName, "Elerium-115");
    PutStr(aObj[112].szName, "Engineer");
    PutStr(aObj[113].szName, "Scientist");
    PutStr(aObj[122].szName, "Personal armour");
    aObj[1].auPerBaseVal[0] = 3;
    aObj[70].auPerBaseVal[0] = 0; /* a stocked slot the base holds none of */
    aObj[77].auPerBaseVal[0] = 4;
    aObj[105].auPerBaseVal[0] = 2;
    aObj[112].auPerBaseVal[0] = 5;
    aObj[113].auPerBaseVal[0] = 0; /* no scientists, so slot 113 gets no row */
    aObj[122].auPerBaseVal[0] = 1;

    aSol[0].nKind = 0; aSol[0].bUnk183 = 0xFF; aSol[0].nUnk440 = 101;
    PutStr(aSol[0].szName, "Chloe");
    aSol[1].nKind = 0; aSol[1].bUnk183 = 0xFF; aSol[1].nUnk440 = 102;
    PutStr(aSol[1].szName, "AVeryLongSoldierNameHere"); /* 24 chars, cut to 17 */
    aSol[2].nKind = 1; aSol[2].bUnk183 = 0xFF; aSol[2].nUnk440 = 103;
    PutStr(aSol[2].szName, "OtherBase");
    for (i = 0; i < 3; ++i) {
        apSol[i] = &aSol[i];
    }

    aShip[0].bType = 1; aShip[0].bId = 7; aShip[0].bCraftType = 0;
    aShip[0].bHomeBase = 0; aShip[0].bAtBase = 1;
    aShip[1].bType = 2; aShip[1].bId = 8; aShip[1].bCraftType = 1;
    aShip[1].bHomeBase = 0; aShip[1].bAtBase = 0; /* not present, so no row */
    aShip[2].bType = 1; aShip[2].bId = 9; aShip[2].bCraftType = 2;
    aShip[2].bHomeBase = 1; aShip[2].bAtBase = 1; /* based elsewhere, so no row */
    for (i = 0; i < 3; ++i) {
        apShip[i] = &aShip[i];
    }

    PutTextRec(aText, 415, "Skyranger");
    PutTextRec(aText, 416, "Lightning");
    PutTextRec(aText, 417, "Avenger");
    PutTextRec(aText, 418, "Interceptor");
    PutTextRec(aText, 419, "Fire Storm");

    memset(&env, 0, sizeof(env));
    memset(aCtl, 0, sizeof(aCtl));
    env.pbPanelsBuilt = &bBuilt;
    for (i = 0; i < 4; ++i) {
        env.apCtl[i] = &aCtl[i];
    }
    env.ppPanelA = &pA;
    env.ppPanelB = &pB;
    env.ppPanelC = &pC;
    env.ppPanelD = &pD;
    env.ppPanelE = &pE;
    env.apSoldiers = apSol;
    env.nSoldierCount = 3;
    env.apCrafts = apShip;
    env.nCraftCount = 3;
    env.apObjects = apObj;
    env.nObjectCount = 149;
    env.nSelectedBaseIdx = 0;
    env.pnAssignedShipCount = &nAssigned;
    env.pnSoldierIdCount = &nIdCount;
    env.pnFunds = &nFunds;
    env.nFundsSnapshot = 4147000;
    env.pTextRecs = (const char *)aText;
    env.pfnPanelPrev = BuySellTestPanelPrev;
    env.pfnPanelNext = BuySellTestPanelNext;

    BuySellScreen_BuildPanels(&env);

    CheckEqI32(bBuilt, 1, "the build branch sets the built flag");
    CheckEqI32(nAssigned, 0, "the assigned-ship count is cleared");
    CheckEqI32(nIdCount, 0, "and so is the scratch soldier-id list count");
    Check(aCtl[0].pfnClick == BuySellTestPanelPrev, "the first control gets the prev handler");
    Check(aCtl[1].pfnClick == BuySellTestPanelNext, "the second gets the next handler");
    CheckEqI32(aCtl[0].x, 0xCD, "the four controls land on their descriptor offsets");
    CheckEqI32(aCtl[1].x, 0xDC, "the second one too");
    CheckEqI32(aCtl[3].y, 0x129, "and the fourth");
    Check(pA != NULL && pB != NULL && pC != NULL && pD != NULL && pE != NULL,
          "all five panels are built");
    if (pA == NULL || pB == NULL || pC == NULL || pD == NULL || pE == NULL) {
        return;
    }

    /* 2 soldiers + 1 craft + slots 1, 77, 105, 112, 122. Slot 70 holds nothing here and slot 113
     * has no scientists, so neither gets a row. */
    CheckEqI32(pA->nLineCount, 8, "the row total is eight");
    CheckEqI32(pE->nLineCount, 8, "every panel gets the same total");
    CheckEqI32(pA->nStyle, 13, "all five show thirteen rows");
    CheckEqI32(pA->nViewW, 125, "the name column is 125 wide");
    CheckEqI32(pE->nViewW, 45, "the quantity column 45");
    CheckEqI32(pB->nPosX, 164, "the decrement column sits at 164");
    CheckEqI32(pC->nPosX, 180, "the increment column at 180");
    Check(pA->pfnClick == NULL, "the name column's click handler comes from the env, unset here");

    for (i = 0; i < 8; ++i) {
        if (pA->anLineAux[i] == aWantAux[i]) { ++nOk; }
    }
    CheckEqI32(nOk, 8, "the sort pass puts the rows in the g_aBuySellRowOrder order");
    nOk = 0;
    for (i = 0; i < 8; ++i) {
        const char *psz = UiPanel_GetLineText(pA, i);
        if (psz != NULL && strcmp(psz, apWantName[i]) == 0) { ++nOk; }
    }
    CheckEqI32(nOk, 8, "and the names match, the 24-character one cut to 17");
    CheckEqI32(pA->anLineAux[7], 2077,
               "the alien row keeps the fill order at the end: 77 + 2000 matches no table entry");
    CheckEqI32(pA->anLineAux[0], 117, "the craft row is first, marked by its item slot");

    nOk = 0;
    for (i = 0; i < 8; ++i) {
        if (pD->anLineAux[i] == aWantQty[i]) { ++nOk; }
    }
    CheckEqI32(nOk, 8, "the count column carries the per-base quantity, or the crew count");
    nOk = 0;
    for (i = 0; i < 8; ++i) {
        const char *pszB = UiPanel_GetLineText(pB, i);
        const char *pszC = UiPanel_GetLineText(pC, i);
        if (pszB != NULL && strcmp(pszB, "<") == 0 && pszC != NULL && strcmp(pszC, ">") == 0
            && pE->anLineAux[i] == 0) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 8, "every row gets its < and > columns and a zero pending quantity");

    CheckEqI32(pB->anLineAux[1], 1, "a soldier row's decrement column holds the apSoldiers index");
    CheckEqI32(pB->anLineAux[2], 0, "the other soldier was index zero");
    CheckEqI32(pC->anLineAux[1], 102, "and its increment column holds the roster id");
    CheckEqI32(pC->anLineAux[0], 7, "a craft row holds the craft id instead");
    CheckEqI32(pC->anLineAux[3], 0, "an object row leaves it zero");

    CheckEqI32(nFunds, 5000, "the build branch does not touch the funds");

    nFunds = 100; /* what an uncommitted transaction would have left behind */
    BuySellScreen_BuildPanels(&env);
    CheckEqI32(bBuilt, 0, "the second call takes the teardown branch");
    CheckEqI32(nFunds, 4147000, "and rolls the funds back to the snapshot");
    Check(pA == NULL && pB == NULL && pC == NULL && pD == NULL && pE == NULL,
          "the five panel slots are cleared");

    BuySellScreen_BuildPanels(NULL);
    Check(1, "a missing environment does nothing at all");
}

static i32 g_nCommitClose;
static void CommitTestClose(void) { ++g_nCommitClose; }

static void TestBuySellCommitTransaction(void)
{
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static Soldier aSol[2];
    static Soldier *apSol[2];
    static Ship aShip[1];
    static Ship *apShip[1];
    static i32 aDelta[149];
    static i32 aAssigned[8];
    static i32 aSolIds[8];
    static UiPanel pA, pC, pE;
    BaseRec base;
    BuySellCommitEnv env;
    i32 nAssignedCount = -1;
    i32 nSolCount = -1;
    i32 nSnapshot = -1;
    i32 i;

    printf("the buy/sell commit\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    aObj[10].auPerBaseVal[0] = 5; /* the base holds five */
    aObj[10].aItemQty[0] = 3;     /* and the craft carries three */
    aObj[11].auPerBaseVal[0] = 0;
    aObj[11].aItemQty[0] = 4;
    aObj[59].auPerBaseVal[0] = 2; /* Sectoid corpse */
    aObj[77].auPerBaseVal[0] = 4; /* Sectoid soldier */
    aObj[122].auPerBaseVal[0] = 1; /* Personal armour */
    aObj[123].auPerBaseVal[0] = 1; /* Power suit */

    aDelta[10] = 3;  /* the screen queued three more of slot 10 */
    aDelta[77] = -2; /* and two fewer Sectoid soldiers */

    memset(&base, 0, sizeof(base));
    base.bSlot = 0;

    aShip[0].bId = 7; aShip[0].bType = 1; aShip[0].bCraftType = 0;
    aShip[0].bHomeBase = 0; aShip[0].bAtBase = 1; aShip[0].nSlotIdx = 0;
    apShip[0] = &aShip[0];

    aSol[0].nUnk440 = 101; aSol[0].nUnitType = 2; /* selected, wears the power suit */
    aSol[1].nUnk440 = 102; aSol[1].nUnitType = 1; /* not selected, wears personal armour */
    apSol[0] = &aSol[0];
    apSol[1] = &aSol[1];

    (void)UiPanel_ctor(&pA, 4);
    (void)UiPanel_ctor(&pC, 4);
    (void)UiPanel_ctor(&pE, 4);
    /* Row 0 a selected craft, row 1 a selected soldier, row 2 an unselected soldier, row 3 a
     * plain object row. */
    pA.anLineAux[0] = 117;  pC.anLineAux[0] = 7;   pE.anLineAux[0] = 1;
    pA.anLineAux[1] = 1000; pC.anLineAux[1] = 101; pE.anLineAux[1] = 1;
    pA.anLineAux[2] = 1000; pC.anLineAux[2] = 102; pE.anLineAux[2] = 0;
    pA.anLineAux[3] = 1;    pC.anLineAux[3] = 0;   pE.anLineAux[3] = 0;

    memset(&env, 0, sizeof(env));
    env.anSlotDelta = aDelta;
    env.pPanelA = &pA;
    env.pPanelC = &pC;
    env.pPanelE = &pE;
    env.pBase = &base;
    env.apObjects = apObj;
    env.nObjectCount = 149;
    env.apCrafts = apShip;
    env.nCraftCount = 1;
    env.apSoldiers = apSol;
    env.nSoldierCount = 2;
    env.pnAssignedCount = &nAssignedCount;
    env.anAssignedList = aAssigned;
    env.nAssignedCapacity = 8;
    env.pnSoldierIdCount = &nSolCount;
    env.anSoldierIdList = aSolIds;
    env.nSoldierIdCapacity = 8;
    env.pnFundsSnapshot = &nSnapshot;
    env.nFunds = 4147000;
    env.pfnCloseUi = CommitTestClose;

    g_nCommitClose = 0;
    BuySellScreen_CommitTransaction(&env);

    CheckEqI32(aObj[10].auPerBaseVal[0], 11,
               "the pending delta lands, then the craft's three more arrive");
    CheckEqI32(aObj[11].auPerBaseVal[0], 4, "and so does everything else the craft carried");
    CheckEqI32(aObj[10].aItemQty[0], 0, "the craft's own cargo column empties");
    CheckEqI32(aObj[11].aItemQty[0], 0, "for every slot");
    CheckEqI32((i32)(i16)aShip[0].uUnk4, -7, "and the craft's carried total drops by the same");
    CheckEqI32(aObj[77].auPerBaseVal[0], 2, "a negative delta takes Sectoid soldiers away");
    CheckEqI32(aObj[59].auPerBaseVal[0], 4, "while the Sectoid corpse moves the other way");
    CheckEqI32(nAssignedCount, 1, "the selected craft row lands in the assigned-ship list");
    CheckEqI32(aAssigned[0], 0, "as the craft index Ship_FindIndexById found");
    CheckEqI32(nSolCount, 1, "only the selected soldier row reaches the id list");
    CheckEqI32(aSolIds[0], 101, "by the roster id panel C carries");
    CheckEqI32(aObj[123].auPerBaseVal[0], 2, "the sold soldier's armour goes back to the base");
    CheckEqI32(aObj[122].auPerBaseVal[0], 1,
               "the unselected soldier keeps the armour he was wearing");
    CheckEqI32(base.nStoresTotal, 11, "the store total is delta 3, cargo 7 and the armour 1");
    CheckEqI32(nSnapshot, 4147000, "the funds snapshot is taken last");
    CheckEqI32(g_nCommitClose, 1, "and the screen closes");

    BuySellScreen_CommitTransaction(NULL);
    CheckEqI32(g_nCommitClose, 1, "a missing environment does nothing at all");

    UiPanel_FreeLines(&pA);
    UiPanel_FreeLines(&pC);
    UiPanel_FreeLines(&pE);
}

static void TestBuySellRowEditors(void)
{
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static i32 aDelta[149];
    static UiPanel pA, pB, pC, pD, pE;
    BuySellRowEditEnv env;
    i32 nTimer = 25;
    i32 nTransTotal = 0;
    i32 nFunds = 5000;
    u8 r, g, b;
    i32 i;

    printf("the buy/sell row editors\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    aObj[10].auPerBaseVal[0] = 5; aObj[10].uPrice = 100;  /* a plain object row */
    aObj[11].auPerBaseVal[0] = 1; aObj[11].uPrice = 250;  /* one the base can spare once */
    aObj[77].auPerBaseVal[0] = 3; aObj[77].uPrice = 900;  /* Sectoid soldier, an alien row */
    aObj[112].auPerBaseVal[0] = 5;                        /* the engineer crew slot */

    (void)UiPanel_ctor(&pA, 6);
    (void)UiPanel_ctor(&pB, 6);
    (void)UiPanel_ctor(&pC, 6);
    (void)UiPanel_ctor(&pD, 6);
    (void)UiPanel_ctor(&pE, 6);
    /* Row 0 a soldier, row 1 a craft, row 2 an alien, row 3 a plain object, row 4 the engineer
     * crew slot, row 5 an object the base holds only one of. */
    pA.anLineAux[0] = 1000;
    pA.anLineAux[1] = 117;
    pA.anLineAux[2] = 2077;
    pA.anLineAux[3] = 10;
    pA.anLineAux[4] = 112;
    pA.anLineAux[5] = 11;
    pD.anLineAux[0] = 2;
    pD.anLineAux[1] = 1;
    pD.anLineAux[2] = 3;
    pD.anLineAux[3] = 5;
    pD.anLineAux[4] = 5;
    pD.anLineAux[5] = 4;

    memset(&env, 0, sizeof(env));
    env.pnRadarPulseTimer = &nTimer;
    env.pPanelA = &pA;
    env.pPanelB = &pB;
    env.pPanelC = &pC;
    env.pPanelD = &pD;
    env.pPanelE = &pE;
    env.anSlotDelta = aDelta;
    env.nSlotDeltaCount = 149;
    env.apObjects = apObj;
    env.nObjectCount = 149;
    env.nSelectedBaseIdx = 0;
    env.pnTransTotal = &nTransTotal;
    env.pnFunds = &nFunds;

    /* The ">" column. A soldier row and a craft row share one body. */
    pC.nUnk3032 = 0;
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(pE.anLineAux[0], 1, "one click marks one soldier");
    CheckEqI32(pD.anLineAux[0], 1, "and takes him off the available count");
    Check(UiPanel_GetLineMeta(&pE, 0, 1, &r, &g, &b) && r == PUFO_BUYSELL_META_SELL_A
              && g == PUFO_BUYSELL_META_SELL_G && b == PUFO_BUYSELL_META_SELL_A,
          "the marked row gets the green triple");
    Check(UiPanel_GetLineMeta(&pE, 0, 2, &r, &g, &b) && r == PUFO_BUYSELL_META_SELL_B
              && g == PUFO_BUYSELL_META_SELL_BLUE && b == PUFO_BUYSELL_META_SELL_B,
          "and the blue one second");
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(pE.anLineAux[0], 1,
               "a second click leaves the flag at 1: 0x755F4 writes #1, it does not add");
    CheckEqI32(pD.anLineAux[0], 0, "while the count column still drops");
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(pE.anLineAux[0], 1, "the count column guards the row: nothing more can be marked");
    CheckEqI32(pD.anLineAux[0], 0, "and it does not go negative");

    pC.nUnk3032 = 1;
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(pE.anLineAux[1], 1, "a craft row behaves exactly like a soldier row");
    CheckEqI32(pD.anLineAux[1], 0, "with the same guard on its count");

    /* An alien row moves the slot delta instead of a selection flag. */
    pC.nUnk3032 = 2;
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(aDelta[77], -1, "the alien row writes 2077 - 2000 = slot 77 of the delta array");
    CheckEqI32(pE.anLineAux[2], 1, "and counts the marked units in panel E");
    CheckEqI32(pD.anLineAux[2], 2, "while the count column drops");
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(aDelta[77], -2, "one more click, one more unit queued for the commit");
    CheckEqI32(pE.anLineAux[2], 2, "and the marked count follows");

    /* A plain object row runs the repeat loop: 25 / 10 + 1 = three units per click. */
    pC.nUnk3032 = 3;
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(aDelta[10], -3, "a radar timer of 25 lets one click queue three units");
    CheckEqI32(pD.anLineAux[3], 2, "three off the count column");
    CheckEqI32(nTransTotal, 300, "the transaction total gains the price each time");
    CheckEqI32(nFunds, 5300, "and so do the funds: the original adds the sale price straight back");
    CheckEqI32(pE.anLineAux[3], 300, "panel E holds the money this row will bring");

    /* The crew slots write panel E from the delta instead of adding to it, and move no money. */
    pC.nUnk3032 = 4;
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(aDelta[112], -3, "the engineer row repeats the same three times");
    CheckEqI32(pE.anLineAux[4], 3, "but its quantity is the delta negated, not a running sum");
    CheckEqI32(pD.anLineAux[4], 2, "and the count column still drops by one each pass");
    CheckEqI32(nTransTotal, 300, "a crew row moves no money at all");

    /* The sell guard breaks out of the loop as soon as the base cannot cover the delta. */
    pC.nUnk3032 = 5;
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(aDelta[11], -1, "one unit is all the base holds, so the loop breaks after one");
    CheckEqI32(nTransTotal, 550, "one unit at 250, not three");
    CheckEqI32(pE.anLineAux[5], 250, "and the row shows that one unit's price");

    /* The "<" column. */
    pB.nUnk3032 = 0;
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(pE.anLineAux[0], 0, "the decrement column clears the soldier mark");
    CheckEqI32(pD.anLineAux[0], 1, "and hands the soldier back to the count column");
    Check(UiPanel_GetLineMeta(&pE, 0, 1, &r, &g, &b) && r == PUFO_BUYSELL_META_BUY_A
              && g == PUFO_BUYSELL_META_BUY_A && b == PUFO_BUYSELL_META_BUY_A,
          "the row goes back to the neutral triple");
    Check(UiPanel_GetLineMeta(&pE, 0, 2, &r, &g, &b) && r == PUFO_BUYSELL_META_BUY_B
              && g == PUFO_BUYSELL_META_BUY_B && b == PUFO_BUYSELL_META_BUY_B,
          "in both triples");
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(pD.anLineAux[0], 1, "with nothing marked the buy side does nothing");

    pB.nUnk3032 = 2;
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(aDelta[77], -1, "an alien row gives one queued unit back");
    CheckEqI32(pE.anLineAux[2], 1, "and its marked count drops");
    CheckEqI32(pD.anLineAux[2], 2, "with the count column restored");

    pB.nUnk3032 = 3;
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(aDelta[10], 0, "the plain object row unwinds the same three units");
    CheckEqI32(nTransTotal, 250, "the total unwinds by the price each time");
    CheckEqI32(nFunds, 5250, "and the funds land 250 up: the one unit sold from the last row");
    CheckEqI32(pE.anLineAux[3], 0, "the row's money column empties");
    CheckEqI32(pD.anLineAux[3], 5, "and the count column is whole again");

    pB.nUnk3032 = 4;
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(aDelta[112], 0, "the crew row unwinds to nothing queued");
    CheckEqI32(pE.anLineAux[4], 0, "one unit off its quantity per pass");
    CheckEqI32(pD.anLineAux[4], 5, "with the count column restored");
    CheckEqI32(nTransTotal, 250, "and again no money moves");

    /* The two loops are not symmetric: the buy guard skips the body but keeps the loop turning,
     * and the subtraction has no floor. */
    pB.nUnk3032 = 3;
    pE.anLineAux[3] = 50; /* less than one unit's price */
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(pE.anLineAux[3], -50, "50 pending against a 100 price goes negative, no floor");
    CheckEqI32(aDelta[10], 1, "only one unit moves: the guard skips the body, it does not break");
    CheckEqI32(nTransTotal, 150, "and the total goes down by the full price once");

    /* The repeat bound is nRadarPulseTimer / 10, re-read every iteration. */
    nTimer = 0;
    aDelta[10] = 0;
    pD.anLineAux[3] = 0;
    pE.anLineAux[3] = 1000;
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(aDelta[10], 1, "a timer of 0 still runs the loop once");
    nTimer = 255;
    aDelta[10] = 0;
    pD.anLineAux[3] = 0;
    pE.anLineAux[3] = 100000;
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(aDelta[10], 26, "the radar screen's 255 makes one click move 26 units");
    CheckEqI32(pD.anLineAux[3], 26, "with no floor on the count column either");
    env.pnRadarPulseTimer = NULL;
    aDelta[10] = 0;
    pD.anLineAux[3] = 0;
    pE.anLineAux[3] = 1000;
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(aDelta[10], 1, "a missing timer counts as zero, so one pass");
    env.pnRadarPulseTimer = &nTimer;

    /* Guards the original does not have. */
    pC.nUnk3032 = 99;
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(pE.anLineAux[0], 0, "a row past the end changes nothing");
    pC.nUnk3032 = -1;
    BuySellScreen_FillSellPanel(&env);
    CheckEqI32(pD.anLineAux[0], 1, "and neither does a negative one");

    BuySellScreen_FillSellPanel(NULL);
    BuySellScreen_FillBuyPanel(NULL);
    env.pPanelD = NULL;
    BuySellScreen_FillSellPanel(&env);
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(aDelta[10], 1, "a missing panel makes both handlers do nothing");
    env.pPanelD = &pD;

    UiPanel_FreeLines(&pA);
    UiPanel_FreeLines(&pB);
    UiPanel_FreeLines(&pC);
    UiPanel_FreeLines(&pD);
    UiPanel_FreeLines(&pE);
}

static void TestBuySellCapacityGate(void)
{
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static Ship aShip[1];
    static Ship *apShip[1];
    static SmapSite aSites[2];
    static BaseRec aBase;
    static BaseRec *apBases[8];
    static u8 aGrid[PUFO_GRID_SIZE];
    static u8 aGridBytes[64];
    static UiPanel pA, pD;
    BuySellCapacityEnv env;
    i32 nRowCursor = 0;
    i32 nTimer = 25;
    i32 nFunds = 100000;
    i32 nTotal = 0;
    i32 nHire = 0;
    u32 nShortfall = 0;
    i32 nResidue = -999;
    i32 i;

    printf("the buy screen's capacity gate\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    aObj[10].uPrice = 100;  aObj[10].bCargoSize = 2;  /* a plain object row */
    aObj[117].uPrice = 5000;                           /* the Skyranger product */

    aShip[0].bHomeBase = 1; /* based at another base, so it never fills this hangar */
    apShip[0] = &aShip[0];

    aBase.bUnk4 = 7;
    for (i = 0; i < 8; ++i) {
        apBases[i] = &aBase;
    }
    aSites[0].bFlag1 = 99; /* matches no base, so the site scan contributes nothing */

    /* Base 0 gets one living-quarters cell (50), one stores cell (300) and one hangar slot (1). */
    aGrid[480 + 144 * 0 + 6 * 0 + 0] = 1;
    aGrid[480 + 144 * 0 + 6 * 0 + 1] = 6;
    aGrid[480 + 144 * 0 + 6 * 0 + 2] = 11;

    (void)UiPanel_ctor(&pA, 6);
    (void)UiPanel_ctor(&pD, 6);
    pA.anLineAux[0] = PUFO_ROW_SOLDIER;
    pA.anLineAux[1] = PUFO_ROW_ENGINEER;
    pA.anLineAux[2] = PUFO_ROW_SCIENTIST;
    pA.anLineAux[3] = 10;
    pA.anLineAux[4] = PUFO_ROW_CRAFT_FIRST;
    pA.anLineAux[5] = 2077; /* an alien row: the gate treats it as a plain product */

    memset(&env, 0, sizeof(env));
    env.pPanelA = &pA;
    env.pPanelD = &pD;
    env.pnRowCursor = &nRowCursor;
    env.pnRadarPulseTimer = &nTimer;
    env.abFacilityGrid = aGrid;
    env.abGridBytes = aGridBytes;
    env.aSites = aSites;
    env.nSiteCount = 1; /* the working site is the record AT the count, aSites[1] */
    env.apBases = apBases;
    env.nBaseCount = 1;
    env.apCraft = apShip;
    env.nCraftCount = 1;
    env.apObjects = apObj;
    env.nObjectCount = 149;
    env.nSelectedBaseIdx = 0;
    env.pnFunds = &nFunds;
    env.pnTransTotal = &nTotal;
    env.pnSoldierHireCount = &nHire;
    env.pdwShortfall = &nShortfall;
    env.pnResidue = &nResidue;

    /* A soldier row runs the whole loop and then hits the funds guard. */
    nRowCursor = 0;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_FUNDS,
               "100000 buys two soldiers and the third pass reports the funds shortfall");
    CheckEqI32(nFunds, 20000, "two soldiers at 40000 each");
    CheckEqI32(nHire, 2, "unk_159D90 counts the hires");
    CheckEqI32(nTotal, 80000, "and the transaction total follows");
    CheckEqI32(aSites[1].uRecoveredSoldiers, 2, "the working site carries them");
    CheckEqI32(pD.anLineAux[0], 2, "with two on the count column");
    CheckEqI32((i32)nShortfall, 1, "the shortfall flag is set");
    CheckEqI32(nResidue, 1, "and the reason is the funds code");

    nRowCursor = 1;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_FUNDS,
               "20000 cannot cover a 50000 engineer");
    CheckEqI32(aSites[1].auItemQty[112], 0, "and nothing is queued");

    nFunds = 60000;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_NONE,
               "with the money the engineer goes through");
    CheckEqI32(aSites[1].auItemQty[112], 1, "queued on the working site");
    CheckEqI32(pD.anLineAux[1], 1, "one on the count column");
    CheckEqI32(nFunds, 10000, "and 50000 is gone");
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_FUNDS,
               "a crew row returns out of the function, so a second pass needs a second click");

    nRowCursor = 2;
    nFunds = 70000;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_NONE,
               "the scientist row costs 60000");
    CheckEqI32(aSites[1].auItemQty[113], 1, "and lands in the scientist column");
    CheckEqI32(nFunds, 10000, "leaving 10000");

    /* A plain object row: the stores budget, then the price. */
    nRowCursor = 3;
    nFunds = 10000;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_NONE,
               "three units of a 100-price object fit 300 units of stores");
    CheckEqI32(aSites[1].auItemQty[10], 3, "three queued on the working site");
    CheckEqI32(pD.anLineAux[3], 3, "three on the count column");
    CheckEqI32(nFunds, 9700, "and 300 spent");

    nFunds = 50;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_FUNDS,
               "50 cannot cover a 100 price: an unsigned compare at 0x75D28");
    CheckEqI32(aSites[1].auItemQty[10], 3, "the queue stays where it was");
    CheckEqI32(nResidue, 1, "and this path writes the flag's own value as the reason");

    /* The craft row: one hangar slot, and the working site's own pending craft counts against it. */
    nRowCursor = 4;
    nFunds = 10000;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_HANGAR,
               "the first Skyranger fits, the second pass sees it and reports the hangar full");
    CheckEqI32(aSites[1].bUnk518, 1, "the pending craft byte carries the one that got through");
    CheckEqI32(aSites[1].auItemQty[117], 1, "and its column holds it");
    CheckEqI32(nResidue, 2, "with the hangar reason code");
    CheckEqI32(nFunds, 5000, "one craft at 5000, no more");

    nShortfall = 0;
    nResidue = -999;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_HANGAR,
               "the hangar path sets the flag only when it is still clear");
    CheckEqI32((i32)nShortfall, 1, "so a clear flag becomes 1");
    CheckEqI32(nResidue, 2, "with the reason beside it");

    /* Living quarters full: the flag is written unconditionally here, 0x75F98. */
    aGrid[480 + 144 * 0 + 6 * 0 + 0] = 0;
    nRowCursor = 0;
    nShortfall = 5;
    nResidue = -999;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_LIVING,
               "with no living-quarters cell even one soldier does not fit");
    CheckEqI32((i32)nShortfall, 1, "and this path overwrites the flag whatever it held");
    CheckEqI32(nResidue, 0, "with the living-quarters reason 0");
    CheckEqI32(nFunds, 5000, "no money moves on a capacity failure");
    aGrid[480 + 144 * 0 + 6 * 0 + 0] = 1;

    /* An alien row is a product row here: no object record, so no price and no cargo. */
    nRowCursor = 5;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_NONE,
               "slot 2077 passes the gate with a zero price");
    CheckEqI32(pD.anLineAux[5], 3, "three passes, three on the count column");
    CheckEqI32(aSites[1].auItemQty[0], 0, "and the port does not write auItemQty out of range");

    /* The "<" control takes it all back, with no capacity test at all. */
    nRowCursor = 3;
    nFunds = 50;
    nTotal = 195300;
    CheckEqI32(BuySellScreen_UpdatePrices(&env), PUFO_BUYSELL_SHORTFALL_NONE,
               "the count column is the only guard on the sell side");
    CheckEqI32(pD.anLineAux[3], 0, "three units back off a count of three");
    CheckEqI32(aSites[1].auItemQty[10], 0, "the working site empties");
    CheckEqI32(nFunds, 350, "and the money comes back");
    CheckEqI32(nTotal, 195000, "with the total unwound");

    BuySellScreen_UpdatePrices(&env);
    CheckEqI32(nFunds, 350, "an empty count column makes the sell side do nothing");

    nRowCursor = 0;
    pD.anLineAux[0] = 2;
    nHire = 2;
    aSites[1].uRecoveredSoldiers = 2;
    BuySellScreen_UpdatePrices(&env);
    CheckEqI32(nHire, 0, "the soldier row gives the hires back");
    CheckEqI32(aSites[1].uRecoveredSoldiers, 0, "and the working site's soldier count with them");
    CheckEqI32(pD.anLineAux[0], 0, "the third pass skips on the empty count");

    nRowCursor = 1;
    pD.anLineAux[1] = 3;
    aSites[1].auItemQty[112] = 1;
    nFunds = 80350;
    nTotal = 115000;
    BuySellScreen_UpdatePrices(&env);
    CheckEqI32(pD.anLineAux[1], 2, "a crew row returns after one unit, whatever the count held");
    CheckEqI32(aSites[1].auItemQty[112], 0, "one engineer back to the base");
    CheckEqI32(nFunds, 130350, "with 50000 refunded");

    nRowCursor = 4;
    pD.anLineAux[4] = 1;
    aSites[1].bUnk518 = 1;
    aSites[1].auItemQty[117] = 1;
    nFunds = 130350;
    nTotal = 65000;
    BuySellScreen_UpdatePrices(&env);
    CheckEqI32(aSites[1].bUnk518, 0, "the craft row clears the pending-craft byte too");
    CheckEqI32(aSites[1].auItemQty[117], 0, "and the craft column");
    CheckEqI32(nTotal, 60000, "with the 5000 back out of the total");

    /* The repeat bound is the same radar timer the row editors use. */
    nTimer = 0;
    nRowCursor = 3;
    pD.anLineAux[3] = 30;
    aSites[1].auItemQty[10] = 0;
    nFunds = 100000;
    nTotal = 0;
    BuySellScreen_ValidateCapacity(&env);
    CheckEqI32(aSites[1].auItemQty[10], 1, "a timer of 0 runs the loop once");
    nTimer = 255;
    aSites[1].auItemQty[10] = 0;
    nTotal = 0;
    BuySellScreen_ValidateCapacity(&env);
    CheckEqI32(aSites[1].auItemQty[10], 26, "the radar screen's 255 makes one click buy 26 units");
    CheckEqI32(nTotal, 2600, "at 100 each");
    nTimer = 25;

    /* Guards the original does not have. */
    nRowCursor = 99;
    CheckEqI32(BuySellScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_NONE,
               "a row past the end reports no shortfall");
    CheckEqI32(aSites[1].auItemQty[10], 26, "and changes nothing");
    BuySellScreen_ValidateCapacity(NULL);
    BuySellScreen_UpdatePrices(NULL);
    env.pnRowCursor = NULL;
    BuySellScreen_ValidateCapacity(&env);
    BuySellScreen_UpdatePrices(&env);
    env.pnRowCursor = &nRowCursor;
    CheckEqI32(aSites[1].auItemQty[10], 26, "a missing cursor makes both handlers do nothing");

    UiPanel_FreeLines(&pA);
    UiPanel_FreeLines(&pD);
}

static void TestRoleToItemId(void)
{
    printf("BuySell_RoleToItemId @ 0x76d88\n");

    CheckEqI32(BuySell_RoleToItemId(0), 117, "role 0");
    CheckEqI32(BuySell_RoleToItemId(1), 120, "role 1");
    CheckEqI32(BuySell_RoleToItemId(2), 121, "role 2");
    CheckEqI32(BuySell_RoleToItemId(3), 118, "role 3");
    CheckEqI32(BuySell_RoleToItemId(4), 119, "role 4, the fall-through");
    CheckEqI32(BuySell_RoleToItemId(5), 0xFF, "anything else is 255");
    CheckEqI32(BuySell_RoleToItemId(-1), 0xFF,
               "and so is a negative role: 0x76D88 widens the byte as a signed char");
    CheckEqI32(BuySell_RoleToItemId(256), 117, "only the low byte counts");
}

static void TestAlienRaceId(void)
{
    printf("BuySell_ItemIdToAlienRaceId @ 0x6ad30\n");

    /* Both ends of every group, so a range that is one short shows up. */
    CheckEqI32(BuySell_ItemIdToAlienRaceId(77), PUFO_ALIEN_RACE_SECTOID, "77 Sectoid soldier");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(81), PUFO_ALIEN_RACE_SECTOID, "81 Sectoid commander");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(82), PUFO_ALIEN_RACE_SNAKEMAN, "82 Snakeman soldier");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(86), PUFO_ALIEN_RACE_SNAKEMAN, "86 Snakeman commander");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(87), PUFO_ALIEN_RACE_FLOATER, "87 Floater soldier");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(91), PUFO_ALIEN_RACE_FLOATER, "91 Floater commander");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(92), PUFO_ALIEN_RACE_MUTON, "92 Muton soldier");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(94), PUFO_ALIEN_RACE_MUTON, "94 Muton navigator");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(95), PUFO_ALIEN_RACE_ETHEREAL, "95 Ethereal soldier");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(98), PUFO_ALIEN_RACE_ETHEREAL, "98 Ethereal commander");

    /* The six singletons after the jump table. */
    CheckEqI32(BuySell_ItemIdToAlienRaceId(99), PUFO_ALIEN_RACE_CYBERDISK, "99 Cyberdisk");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(100), PUFO_ALIEN_RACE_CHRYSSALID, "100 Chryssalid");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(101), PUFO_ALIEN_RACE_REAPER, "101 Reaper");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(102), PUFO_ALIEN_RACE_SILACOID, "102 Silacoid");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(103), PUFO_ALIEN_RACE_CELATID, "103 Celatid");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(104), PUFO_ALIEN_RACE_SECTOPOD, "104 Sectopod");

    /* The extra Ethereal rank and the five medics, each landing on its own race's id. */
    CheckEqI32(BuySell_ItemIdToAlienRaceId(114), PUFO_ALIEN_RACE_ETHEREAL, "114 Ethereal navigator");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(144), PUFO_ALIEN_RACE_SNAKEMAN, "144 Snakeman medic");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(145), PUFO_ALIEN_RACE_MUTON, "145 Muton medic");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(146), PUFO_ALIEN_RACE_FLOATER, "146 Floater medic");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(147), PUFO_ALIEN_RACE_ETHEREAL, "147 Ethereal medic");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(148), PUFO_ALIEN_RACE_SECTOID, "148 Sectoid medic");

    /* The gaps the original's compares leave open. */
    CheckEqI32(BuySell_ItemIdToAlienRaceId(76), PUFO_ALIEN_RACE_NONE, "76, one before the table");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(105), PUFO_ALIEN_RACE_NONE, "105, one past it");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(113), PUFO_ALIEN_RACE_NONE, "113");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(115), PUFO_ALIEN_RACE_NONE, "115");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(143), PUFO_ALIEN_RACE_NONE, "143");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(149), PUFO_ALIEN_RACE_NONE, "149, past the shipped count");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(0), PUFO_ALIEN_RACE_NONE, "0");
    CheckEqI32(BuySell_ItemIdToAlienRaceId(-1), PUFO_ALIEN_RACE_NONE,
               "-1: every compare in 0x6AD34..0x6AE34 is signed");

    /* Exactly 34 slots carry a race, which is the group widths summed. */
    i32 nMapped = 0;
    for (i32 i = 0; i < 149; ++i) {
        if (BuySell_ItemIdToAlienRaceId(i) != PUFO_ALIEN_RACE_NONE) {
            ++nMapped;
        }
    }
    CheckEqI32(nMapped, 34, "34 of the 149 shipped slots carry an alien race");
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

/* --- ManufScreen_ClearProductSlot @ 0x6f8cc, ConfirmAndClose @ 0x6e98c, IncEngineers @ 0x6e13c --- */

static u32 g_aProdA[1600];
static i32 g_aProdB[1600];
static u16 g_aProdC[1600];
static u16 g_aManufCol[1600];
static u8 g_aGridBytes[8];
static BaseRec g_aBases[3];
static BaseRec *g_apBases[3];
static UiPanel g_ClearPanel;
static i32 g_nClearRow;
static i8 g_nClearBase;
static i32 g_nCloseCalls2;

static void Close2Thunk(void *pUser)
{
    (void)pUser;
    ++g_nCloseCalls2;
}

static void TestClearProductSlot(void)
{
    ManufClearEnv env;
    GeoscapeCloseHooks hooks;
    i32 i;

    printf("ManufScreen_ClearProductSlot @ 0x6f8cc\n");

#define CLEAR_RESET()                                                        \
    memset(g_aProdA, 0, sizeof(g_aProdA));                                   \
    memset(g_aProdB, 0, sizeof(g_aProdB));                                   \
    memset(g_aProdC, 0, sizeof(g_aProdC));                                   \
    memset(g_aManufCol, 0, sizeof(g_aManufCol));                             \
    memset(g_aGridBytes, 0, sizeof(g_aGridBytes));                           \
    memset(g_aBases, 0, sizeof(g_aBases));                                   \
    memset(&g_ClearPanel, 0, sizeof(g_ClearPanel));                          \
    for (i = 0; i < 3; ++i) { g_apBases[i] = &g_aBases[i]; }                \
    g_nClearRow = 7;                                                         \
    g_nClearBase = 1;                                                        \
    g_nCloseCalls2 = 0;                                                      \
    memset(&env, 0, sizeof(env));                                            \
    env.pProductPanel = &g_ClearPanel; env.pnProductRow = &g_nClearRow;      \
    env.apProductA = g_aProdA; env.apProductB = g_aProdB;                    \
    env.apProductC = g_aProdC; env.apManufCols = g_aManufCol;                \
    env.abGridBytes = g_aGridBytes;                                          \
    env.apBases = g_apBases; env.nBaseCount = 3;                             \
    env.pnSelectedBaseIdx = &g_nClearBase;                                   \
    memset(&hooks, 0, sizeof(hooks));                                        \
    hooks.pfnCloseUi = Close2Thunk;

    /* 1. one index of all four columns goes to zero, its neighbours do not move. */
    CLEAR_RESET();
    for (i = 238; i <= 242; ++i) {
        g_aProdA[i] = 11u; g_aProdB[i] = 22; g_aProdC[i] = 33; g_aManufCol[i] = 44;
    }
    ManufScreen_ClearProductSlot(&env, 1, 40); /* idx = 200*1 + 40 = 240 */
    CheckEqI32((i32)g_aProdA[240], 0, "the u32 column at 0x159E9C is cleared (0x6F918)");
    CheckEqI32(g_aProdB[240], 0, "  the u32 column at 0x15B79C is cleared (0x6F8FC)");
    CheckEqI32((i32)g_aProdC[240], 0, "  the u16 column at 0x15D09C is cleared (0x6F92C)");
    CheckEqI32((i32)g_aManufCol[240], 0, "  and auManufCol at 0x15E19C (0x6F910)");
    CheckEqI32(g_aProdB[239], 22, "the entry before it is untouched");
    CheckEqI32(g_aProdB[241], 22, "  and the entry after");

    /* 2. the product argument is masked to a byte (AND R3,R2,#0xFF at 0x6F8D0). */
    CLEAR_RESET();
    g_aProdB[240] = 5;
    ManufScreen_ClearProductSlot(&env, 1, 296); /* 296 & 0xFF == 40 */
    CheckEqI32(g_aProdB[240], 0, "product 296 lands on the same index as 40");

    /* 3. deviation: an index outside the 1600-entry columns is refused. The original would write
     * 200*(-1)+40 = -160 and 200*8+40 = 1640 straight through the arrays. */
    CLEAR_RESET();
    g_aProdB[39] = 5; /* what idx -160+200 would land on, if the port allowed it */
    ManufScreen_ClearProductSlot(&env, -1, 40);
    CheckEqI32(g_aProdB[39], 5, "a negative base index clears nothing");
    CLEAR_RESET();
    g_aProdB[1599] = 5;
    ManufScreen_ClearProductSlot(&env, 8, 40);
    CheckEqI32(g_aProdB[1599], 5, "  and base index 8 clears nothing either");

    /* 4. products 119..121 also clear the selected base's byte in the 0x159DBC table, and they do
     * it by reading the byte and subtracting that same amount back. */
    CLEAR_RESET();
    g_aBases[1].bUnk4 = 5; /* the selected base's type */
    g_aGridBytes[1] = 7;
    ManufScreen_ClearProductSlot(&env, 1, 119);
    CheckEqI32(g_aGridBytes[1], 0, "product 119 clears the base's grid byte (0x6F96C..0x6F990)");

    CLEAR_RESET();
    g_aBases[1].bUnk4 = 5;
    g_aGridBytes[1] = 7;
    ManufScreen_ClearProductSlot(&env, 1, 121);
    CheckEqI32(g_aGridBytes[1], 0, "product 121 does too");

    CLEAR_RESET();
    g_aBases[1].bUnk4 = 5;
    g_aGridBytes[1] = 7;
    ManufScreen_ClearProductSlot(&env, 1, 118);
    CheckEqI32(g_aGridBytes[1], 7, "product 118 is outside the range and leaves it (0x6F934)");
    CLEAR_RESET();
    g_aBases[1].bUnk4 = 5;
    g_aGridBytes[1] = 7;
    ManufScreen_ClearProductSlot(&env, 1, 122);
    CheckEqI32(g_aGridBytes[1], 7, "  so is product 122 (0x6F93C)");

    /* 5. the byte is found by TYPE, not by the selected base's index: the search answers the first
     * base that matches, so with two bases of the same type it is the earlier one that clears. */
    CLEAR_RESET();
    g_aBases[0].bUnk4 = 5;
    g_aBases[1].bUnk4 = 5;
    g_aGridBytes[0] = 9;
    g_aGridBytes[1] = 9;
    ManufScreen_ClearProductSlot(&env, 1, 120);
    CheckEqI32(g_aGridBytes[0], 0, "the first base with the type is the one cleared (0x6AC30)");
    CheckEqI32(g_aGridBytes[1], 9, "  the selected base's own byte is not the target");

    /* 6. no base has the type: Base_FindIndexByTypeByte answers -1 and the port refuses, where the
     * original reads and writes abGrid[-1]. */
    CLEAR_RESET();
    g_aBases[1].bUnk4 = 5;
    g_aBases[0].bUnk4 = 6;
    g_aBases[2].bUnk4 = 7;
    g_aGridBytes[0] = 4;
    ManufScreen_ClearProductSlot(&env, 1, 119);
    CheckEqI32(g_aGridBytes[0], 4, "no matching type writes nothing (deviation at 0x6AD1C)");

    /* 7. the four column stores happen before the product range test. */
    CLEAR_RESET();
    g_aProdB[259] = 5;
    ManufScreen_ClearProductSlot(&env, 1, 59);
    CheckEqI32(g_aProdB[259], 0, "a non-hangar product still clears its slot");

    /* 8. null columns are skipped rather than dereferenced. */
    CLEAR_RESET();
    env.apProductA = NULL; env.apProductB = NULL;
    env.apProductC = NULL; env.apManufCols = NULL;
    ManufScreen_ClearProductSlot(&env, 1, 40);
    Check(true, "null columns are skipped");
    ManufScreen_ClearProductSlot(NULL, 1, 40);
    Check(true, "a null env does nothing");

    /* --- ManufScreen_ConfirmAndClose @ 0x6e98c --- */
    printf("ManufScreen_ConfirmAndClose @ 0x6e98c\n");

    CLEAR_RESET();
    g_ClearPanel.anLineAux[7] = 40;
    g_aProdB[240] = 5;
    ManufScreen_ConfirmAndClose(&env, &hooks);
    CheckEqI32(g_aProdB[240], 0, "the product comes from the panel row's aux cell (0x6E9A0)");
    CheckEqI32(g_nCloseCalls2, 1, "and Geoscape_CloseUI runs afterwards (0x6E9C8)");

    CLEAR_RESET();
    g_ClearPanel.anLineAux[7] = 119;
    g_aBases[1].bUnk4 = 5;
    g_aGridBytes[1] = 6;
    g_aProdB[319] = 5;
    ManufScreen_ConfirmAndClose(&env, &hooks);
    CheckEqI32(g_aProdB[319], 0, "a hangar product clears its slot here too");
    CheckEqI32(g_aGridBytes[1], 0, "  and the base grid byte");

    CLEAR_RESET();
    g_ClearPanel.anLineAux[7] = 40;
    g_aProdB[240] = 5;
    ManufScreen_ConfirmAndClose(&env, NULL);
    CheckEqI32(g_aProdB[240], 0, "no close hook: the clear still runs");
    CheckEqI32(g_nCloseCalls2, 0, "  and nothing is called");

    ManufScreen_ConfirmAndClose(NULL, &hooks);
    CheckEqI32(g_nCloseCalls2, 0, "a null env closes nothing");
#undef CLEAR_RESET
}

static void TestIncEngineers(void)
{
    static Ship aCraft[3];
    static Ship *apCraft[3];
    static ObjectRecFull aObjs[2];
    static ObjectRecFull *apObjects[2];
    static SmapSite aSites[2];
    static BaseRec aBases[2];
    static BaseRec *apBases[2];
    static i32 aProdB[1600];
    static u8 aGrid[1524];
    static u8 aGridBytes[8];
    static UiPanel panel;
    ManufEngineerEnv env;
    i32 nRow;
    i8 nBase;
    u8 nGate;
    u32 dwShortfall;
    i32 nResidue;
    i32 i;

    printf("ManufScreen_IncEngineers @ 0x6e13c\n");

#define ENG_RESET(nProduct)                                                  \
    memset(aProdB, 0, sizeof(aProdB));                                       \
    memset(aGrid, 0, sizeof(aGrid));                                         \
    memset(aGridBytes, 0, sizeof(aGridBytes));                               \
    memset(aCraft, 0, sizeof(aCraft));                                       \
    memset(aObjs, 0, sizeof(aObjs));                                         \
    memset(aSites, 0, sizeof(aSites));                                       \
    memset(aBases, 0, sizeof(aBases));                                       \
    memset(&panel, 0, sizeof(panel));                                        \
    for (i = 0; i < 2; ++i) { apBases[i] = &aBases[i]; apCraft[i] = &aCraft[i]; } \
    apCraft[2] = &aCraft[2];                                                 \
    apObjects[0] = &aObjs[0]; apObjects[1] = &aObjs[1];                      \
    panel.anLineAux[3] = (nProduct);                                         \
    nRow = 3;                                                                \
    nBase = 0;                                                               \
    nGate = 0;                                                               \
    dwShortfall = 0;                                                         \
    nResidue = 7;                                                            \
    memset(&env, 0, sizeof(env));                                            \
    env.pProductPanel = &panel; env.pnProductRow = &nRow;                    \
    env.apProductB = aProdB;                                                 \
    env.abFacilityGrid = aGrid; env.abGridBytes = aGridBytes;                \
    env.pnSelectedBaseIdx = &nBase; env.pbQtyGate = &nGate;                  \
    env.apCraft = apCraft; env.nCraftCount = 0; /* a zeroed Ship is homed at base 0 */  \
    env.apBases = apBases; env.nBaseCount = 2;                               \
    env.apObjects = apObjects; env.nObjectCount = 1;                         \
    env.aSites = aSites; env.nSiteCount = 0;                                 \
    env.pdwShortfall = &dwShortfall; env.pnResidue = &nResidue;

    /* The hangar products, 119..121. One free hangar cell is one slot. */
    ENG_RESET(119);
    aGrid[480 + 0] = 11; /* facType[base 0, row 0, col 0], buildDays 0 so it is free */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[119], 1, "one free hangar cell lets the allocation reach 1 (0x6E228)");
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[119], 1, "a second press stops at the free-slot count (0x6E21C)");
    CheckEqI32(dwShortfall, 0, "  and a hangar with room sets no shortfall");

    ENG_RESET(120);
    aGrid[480 + 0] = 11;
    aGrid[480 + 6] = 11; /* row 0, col 1 */
    aProdB[120] = 1;
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[120], 2, "two hangar cells allow two units");
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[120], 2, "  and stops there");

    /* A hangar cell under construction is not capacity: the capacity pass wants buildDays zero. */
    ENG_RESET(119);
    aGrid[480 + 0] = 11;
    aGrid[0] = 3; /* buildDays[base 0, row 0, col 0] */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[119], 0, "a hangar still being built gives no free slot (0x6611C)");
    CheckEqI32(dwShortfall, 1, "  cap 0 == at 0, so the shortfall dword is set (0x6E1BC)");
    CheckEqI32(nResidue, 1, "  and dword_1870B4[0] becomes 1 here (0x6E1C8)");

    /* Craft homed at the base count against the free slots. */
    ENG_RESET(119);
    env.nCraftCount = 3;
    aGrid[480 + 0] = 11;
    aCraft[0].bHomeBase = 0;
    aCraft[1].bHomeBase = 1; /* a different base */
    aCraft[2].bHomeBase = 1;
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[119], 0, "the craft at this base fills the only slot");
    CheckEqI32(dwShortfall, 1, "  shortfall dword set");
    CheckEqI32(nResidue, 1, "  residue 1 on this path");

    /* Base_CountShipsAtBase adds the 0x159DBC byte for the base's type, and that counts too. */
    ENG_RESET(119);
    aGrid[480 + 0] = 11;
    aGrid[480 + 6] = 11;
    aGrid[480 + 12] = 11; /* three slots */
    aBases[0].bUnk4 = 4;
    aGridBytes[0] = 3;
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[119], 0, "the grid byte fills all three slots (0x66110)");
    CheckEqI32(dwShortfall, 1, "  so this is the full-hangar path");

    /* The dead gate still short-circuits, and it is read a second time after the two flag stores. */
    ENG_RESET(119);
    nGate = 1;
    aGrid[480 + 0] = 11;
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[119], 0, "a non-zero gate stops the button before anything else (0x6E14C)");
    CheckEqI32(dwShortfall, 0, "  no capacity call, so no shortfall store either");

    /* --- the stores path: every other product --- */
    ENG_RESET(0);
    aGrid[480 + 0] = 6; /* one free stores cell -> capacity 300 */
    aObjs[0].bCargoSize = 7;
    aProdB[0] = 42; /* 42 * 7 = 294 < 300 */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[0], 43, "below the stores capacity the quantity rises by one (0x6E30C)");
    aProdB[0] = 43; /* 43 * 7 = 301 >= 300 */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[0], 43, "at the capacity it stops");
    CheckEqI32(dwShortfall, 1, "  shortfall dword set (0x6E314)");
    CheckEqI32(nResidue, 0, "  and residue 0 here, where the full-hangar path wrote 1 (0x6E320)");

    ENG_RESET(0);
    aGrid[480 + 0] = 6;
    aGrid[480 + 6] = 6; /* two cells -> 600 */
    aObjs[0].bCargoSize = 7;
    aProdB[0] = 85; /* 595 < 600 */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[0], 86, "a second stores cell doubles the capacity");

    ENG_RESET(0);
    aGrid[480 + 6] = 6; /* facType at 480 + 6*col + row, here row 0 col 1 */
    aGrid[6] = 5;       /* buildDays at 6*col + row, the same cell */
    aObjs[0].bCargoSize = 7;
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[0], 0, "a stores cell under construction gives no capacity (0x65720)");
    CheckEqI32(dwShortfall, 1, "  capacity 0 == pressure 0, so the shortfall dword is set");

    /* Base_CalcStorageUsed adds bCargoSize * auPerBaseVal[base] over the filtered object indices,
     * and object 0 is never in that set -- so the product's own cargo size is not double counted. */
    ENG_RESET(0);
    env.nObjectCount = 2; /* the storage sum walks indices 1..nObjectCount-1 */
    aGrid[480 + 0] = 6;
    aObjs[0].bCargoSize = 7;
    aObjs[1].bCargoSize = 10;
    aObjs[1].auPerBaseVal[0] = 4; /* index 1 is inside the 1..69 filter */
    aProdB[0] = 37; /* 259 + 40 = 299 < 300 */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[0], 38, "the base's own stock counts against the capacity (0x6E274)");
    aProdB[0] = 38; /* 266 + 40 = 306 >= 300 */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[0], 38, "  and pushes it over");

    /* Base_CountItemQtyAcrossBases adds the site cargo, gated on the site index being the base. */
    ENG_RESET(0);
    env.nObjectCount = 2; /* the site walk also needs more than one object */
    aGrid[480 + 0] = 6;
    aObjs[0].bCargoSize = 7;
    aObjs[1].bCargoSize = 10;
    env.nSiteCount = 1;
    aSites[0].auItemQty[1] = 5; /* 10 * 5 = 50 */
    aProdB[0] = 35; /* 245 + 50 = 295 < 300 */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[0], 36, "the site cargo counts too (0x6E294)");
    aProdB[0] = 36; /* 252 + 50 = 302 >= 300 */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[0], 36, "  and pushes it over");

    /* The object whose cargo size is read is apObjects[PRODUCT], not apObjects[base]. */
    ENG_RESET(1);
    env.nObjectCount = 2; /* product 1 has to be inside the object table to be read at all */
    aGrid[480 + 0] = 6;
    aObjs[0].bCargoSize = 100; /* if the base index were used here, 42*100 would blow the cap */
    aObjs[1].bCargoSize = 7;
    aProdB[1] = 42; /* base 0, product 1 -> idx 1, and 42*7 = 294 < 300 */
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[1], 43, "the cargo size comes from the product's object record (0x6E214)");

    ENG_RESET(0);
    nBase = 1;
    aGrid[480 + 144] = 6; /* base 1's stores cell */
    aObjs[0].bCargoSize = 7;
    aProdB[200] = 42;
    ManufScreen_IncEngineers(&env);
    CheckEqI32(aProdB[200], 43, "the column index is 200*nSelectedBaseIdx + product");

    ENG_RESET(0);
    nGate = 200;
    ManufScreen_IncEngineers(&env);
    CheckEqI32(dwShortfall, 0, "the gate short-circuits the stores path too");

    ENG_RESET(0);
    env.apProductB = NULL;
    ManufScreen_IncEngineers(&env);
    CheckEqI32(dwShortfall, 0, "no product column: nothing is stored");

    ENG_RESET(0);
    env.pnProductRow = NULL;
    ManufScreen_IncEngineers(&env);
    Check(true, "a missing row cache is not dereferenced");

    ManufScreen_IncEngineers(NULL);
    Check(true, "a null env does nothing");
#undef ENG_RESET
}

/* --- BaseInfoScreen_TogglePanelMode @ 0x55754 --- */
static void TestTogglePanelMode(void)
{
    static UiPanel panelA, panelB, panelC;
    BaseInfoToggleEnv env;
    u8 bFlag;

    printf("BaseInfoScreen_TogglePanelMode @ 0x55754\n");

#define TOG_RESET()                                                          \
    memset(&panelA, 0, sizeof(panelA));                                      \
    memset(&panelB, 0, sizeof(panelB));                                      \
    memset(&panelC, 0, sizeof(panelC));                                      \
    panelA.nUnk3028 = 999; panelB.nUnk3028 = 999; panelC.nUnk3028 = 999;     \
    bFlag = 0;                                                               \
    memset(&env, 0, sizeof(env));                                            \
    env.pbModeFlag = &bFlag;                                                 \
    env.pPanelA = &panelA; env.pPanelB = &panelB; env.pPanelC = &panelC;

    /* 1. from zero: the flag becomes 1, the cursor written is 0. */
    TOG_RESET();
    BaseInfoScreen_TogglePanelMode(&env);
    CheckEqI32(bFlag, 1, "a zero flag becomes 1 (0x55778)");
    CheckEqI32(panelB.nUnk3028, 0, "the cursor written is 0, not the flag's new value (0x55784)");
    CheckEqI32(panelA.nUnk3028, 0, "  all three panels get it");
    CheckEqI32(panelC.nUnk3028, 0, "  including the last one");

    /* 2. from non-zero: the flag becomes 0, the cursor written is 16. */
    BaseInfoScreen_TogglePanelMode(&env);
    CheckEqI32(bFlag, 0, "the second press clears the flag (0x5576C)");
    CheckEqI32(panelB.nUnk3028, PUFO_BASEINFO_CURSOR_ON, "and writes 16 into the cursor (0x55770)");

    /* 3. the test is non-zero, not equal-to-one, so any other value takes the 16 path. */
    TOG_RESET();
    bFlag = 200;
    BaseInfoScreen_TogglePanelMode(&env);
    CheckEqI32(bFlag, 0, "a flag of 200 is cleared");
    CheckEqI32(panelB.nUnk3028, 16, "  and the cursor is 16");

    TOG_RESET();
    bFlag = 255;
    BaseInfoScreen_TogglePanelMode(&env);
    CheckEqI32(bFlag, 0, "so is 255");
    CheckEqI32(panelB.nUnk3028, 16, "  with the same cursor");

    /* 4. the cursor is a dword field, and it is replaced outright rather than masked. */
    TOG_RESET();
    panelB.nUnk3028 = 0x123456;
    BaseInfoScreen_TogglePanelMode(&env);
    CheckEqI32(panelB.nUnk3028, 0, "the old cursor is replaced, not partly cleared");

    /* 5. a missing panel is skipped, and a missing flag stops the whole thing. */
    TOG_RESET();
    env.pPanelA = NULL; env.pPanelC = NULL;
    BaseInfoScreen_TogglePanelMode(&env);
    CheckEqI32(bFlag, 1, "two null panels still toggle the flag");
    CheckEqI32(panelB.nUnk3028, 0, "  and still write the one that exists");

    TOG_RESET();
    env.pbModeFlag = NULL;
    BaseInfoScreen_TogglePanelMode(&env);
    CheckEqI32(panelB.nUnk3028, 999, "a null flag writes nothing");

    BaseInfoScreen_TogglePanelMode(NULL);
    Check(true, "a null env does nothing");
#undef TOG_RESET
}

static void TestBuySellConfirm(void)
{
    static SmapSite aSites[4];
    static ObjectRecFull aObjs[2];
    static ObjectRecFull *apObjects[2];
    static BaseRec aBases[2];
    static BaseRec *apBases[2];
    BuySellConfirmEnv env;
    GeoscapeCloseHooks hooks;
    i32 nSiteCount, nObjectCount;
    i8 nBase;
    i32 nSnapshot;
    i32 i;

    printf("BuySellScreen_ConfirmSiteAndClose @ 0x75090\n");

#define BS_RESET()                                                                  \
    memset(aSites, 0, sizeof(aSites));                                              \
    memset(aObjs, 0, sizeof(aObjs));                                                \
    memset(aBases, 0, sizeof(aBases));                                              \
    for (i = 0; i < 2; ++i) { apObjects[i] = &aObjs[i]; apBases[i] = &aBases[i]; }  \
    aObjs[1].bCargoSize = 3;      /* object 0 has no cargo size on purpose */       \
    nSiteCount = 2; nObjectCount = 2; nBase = 0;                                    \
    nSnapshot = -1;                                                                 \
    g_nCloseCalls2 = 0;                                                             \
    memset(&env, 0, sizeof(env));                                                   \
    env.aSites = aSites; env.pnSiteCount = &nSiteCount;                             \
    env.pnSelectedBaseIdx = &nBase; env.apBases = apBases;                          \
    env.apObjects = apObjects; env.nObjectCount = nObjectCount;                     \
    env.nFunds = 123456; env.pnFundsSnapshot = &nSnapshot;                          \
    memset(&hooks, 0, sizeof(hooks));                                               \
    hooks.pfnCloseUi = Close2Thunk;

    /* 1. An empty working record is not committed, but the tail still runs. */
    BS_RESET();
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(aSites[2].bFlag0, 0, "an empty working record keeps its flag");
    CheckEqI32(nSiteCount, 2, "  and the site count does not grow");
    CheckEqI32(nSnapshot, 123456, "the funds snapshot is written either way (0x75130)");
    CheckEqI32(g_nCloseCalls2, 1, "  and Geoscape_CloseUI runs either way (0x7513C)");

    /* 2. Cargo volume in a filtered column commits it. */
    BS_RESET();
    aSites[2].auItemQty[1] = 2; /* object 1, cargo size 3 -> 6 */
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(aSites[2].bFlag0, PUFO_SITE_FLAG_CONFIRM, "cargo in the working record commits it (0x75120)");
    CheckEqI32(nSiteCount, 3, "  and the count grows by one (0x75124)");
    CheckEqI32(aSites[0].bFlag0, 0, "  only the working record is touched");
    CheckEqI32(aSites[1].bFlag0, 0, "  not the record before it");

    /* 3. Object 0 is never counted, so its column is not a reason to commit. */
    BS_RESET();
    aObjs[0].bCargoSize = 9;
    aSites[2].auItemQty[0] = 5;
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(aSites[2].bFlag0, 0, "object 0 is outside the cargo filter (0x67E00)");

    /* 4. The soldier count is the second test. */
    BS_RESET();
    aSites[2].uRecoveredSoldiers = 1;
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(aSites[2].bFlag0, PUFO_SITE_FLAG_CONFIRM, "uRecoveredSoldiers commits it (0x750DC)");

    /* 5. Each of the four item columns commits it on its own, even though none of them is in the
     * cargo filter. */
    /* BS_RESET uses i, so the loop counters here are n / k, never i. */
    static const i32 anItems[4] = {112, 113, 117, 118};
    for (i32 n = 0; n < 4; ++n) {
        BS_RESET();
        aSites[2].auItemQty[anItems[n]] = 1;
        BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
        CheckEqI32(aSites[2].bFlag0, PUFO_SITE_FLAG_CONFIRM, "auItemQty[112/113/117/118 commits it (0x750E8)");
    }
    BS_RESET();
    aSites[2].auItemQty[114] = 1; /* between 113 and 117, and outside the cargo filter */
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(aSites[2].bFlag0, 0, "the column between the two pairs is not one of the four");

    /* 6. The scan-sites flag is OFF, so another site of the same base type is not a reason. */
    BS_RESET();
    aBases[0].bUnk4 = 5;
    aSites[0].bFlag1 = 5;
    aSites[0].auItemQty[1] = 40;
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(aSites[2].bFlag0, 0, "a matching site elsewhere is not counted (0x750B0 a4 = 0)");

    /* 7. Deviation: the original reads aSmapSites[200], one record past the array. */
    BS_RESET();
    nSiteCount = PUFO_SMAP_SITE_COUNT;
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(nSiteCount, PUFO_SMAP_SITE_COUNT, "a full site array is not committed past its end");
    CheckEqI32(nSnapshot, 123456, "  the tail still runs");
    CheckEqI32(g_nCloseCalls2, 1, "  including the close");

    /* 8. Nulls. */
    BS_RESET();
    env.pnFundsSnapshot = NULL;
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(g_nCloseCalls2, 1, "a null snapshot still closes");
    BS_RESET();
    env.pnSiteCount = NULL;
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    Check(true, "a null site count is treated as zero");
    BuySellScreen_ConfirmSiteAndClose(NULL, &hooks);
    BuySellScreen_ConfirmSiteAndClose(&env, NULL);
    Check(true, "a null env or a null hook does nothing");
#undef BS_RESET
}

static void TestFundingBonus(void)
{
    i32 aSlots[PUFO_FUNDING_SLOT_COUNT];
    i32 i;

    printf("Funding_AddRaceBonus @ 0x7debc\n");

#define FUND_RESET()                                                     \
    for (i = 0; i < PUFO_FUNDING_SLOT_COUNT; ++i) { aSlots[i] = 0; }

    /* The compare chain's own mapping: 0x84 feeds slot 1 while 0x82 and 0x83 feed slots 2 and 3. */
    /* FUND_RESET uses i, so the loop counters here are n / k, never i. */
    static const i32 anRaces[8] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x88, 0xFF};
    static const i32 anRaceSlot[8] = {0, 0, 2, 3, 1, 4, 0, 5};
    for (i32 n = 0; n < 8; ++n) {
        FUND_RESET();
        Funding_AddRaceBonus(aSlots, anRaces[n]);
        CheckEqI32(aSlots[anRaceSlot[n]], PUFO_FUNDING_BONUS, "the race id raises its own slot by 2 (0x7DEBC)");
        i32 nOthers = 0;
        for (i32 k = 0; k < PUFO_FUNDING_SLOT_COUNT; ++k) {
            if (k != anRaceSlot[n]) {
                nOthers += aSlots[k];
            }
        }
        CheckEqI32(nOthers, 0, "  and no other slot");
    }

    /* Three of the ten race ids give no bonus at all. */
    static const i32 anNoBonus[6] = {0x86, 0x87, 0x89, 0x00, 0x7F, 0x8A};
    for (i32 n = 0; n < 6; ++n) {
        FUND_RESET();
        Funding_AddRaceBonus(aSlots, anNoBonus[n]);
        i32 nTotal = 0;
        for (i32 k = 0; k < PUFO_FUNDING_SLOT_COUNT; ++k) {
            nTotal += aSlots[k];
        }
        CheckEqI32(nTotal, 0, "an unmapped race id changes nothing (locret_7DF7C)");
    }

    /* The argument is masked to a byte before the compares (0x7DEBC AND R0,R1,#0xFF). */
    FUND_RESET();
    Funding_AddRaceBonus(aSlots, 0x180);
    CheckEqI32(aSlots[0], 2, "0x180 masks down to 0x80");
    FUND_RESET();
    Funding_AddRaceBonus(aSlots, 0x1FF);
    CheckEqI32(aSlots[5], 2, "0x1FF masks down to 0xFF, the no-race tile");

    /* It accumulates: every radar contact of that race adds two more. */
    FUND_RESET();
    Funding_AddRaceBonus(aSlots, 0x85);
    Funding_AddRaceBonus(aSlots, 0x85);
    Funding_AddRaceBonus(aSlots, 0x85);
    CheckEqI32(aSlots[4], 6, "three contacts of race 0x85 give six");

    Funding_AddRaceBonus(NULL, 0x80);
    Check(true, "a null slot array does nothing");
#undef FUND_RESET
}

static i32 g_nSroSampleCalls;
static u32 g_nSroSampleId;
static u32 g_nSroSampleArg;
static i32 g_nSroInitCalls;
static void *g_pSroInitSlot;

static void SroSample(u32 nSample, u32 nArg, void *pUser)
{
    (void)pUser;
    ++g_nSroSampleCalls;
    g_nSroSampleId = nSample;
    g_nSroSampleArg = nArg;
}

static void SroInit(void *pSlot, void *pUser)
{
    (void)pUser;
    ++g_nSroInitCalls;
    g_pSroInitSlot = pSlot;
}

static void TestScreenResetOpen(void)
{
    static GeoscapeScreenState state;
    static GameTime clock;
    static GeoscapeWorld world;
    static UiControl ctl[PUFO_GEOSCAPE_SPEED_COUNT];
    static UiControl *apCtl[PUFO_GEOSCAPE_SPEED_COUNT];
    static UiLineList lines;
    static u8 abActive[PUFO_RESEARCH_TOPIC_COUNT];
    static u8 bCount;
    static u32 dwShortfall;
    static char szMsg[32];
    static char szScratch[32];
    static char szSlot[4];
    ScreenResetOpenEnv env;
    i32 i;

    printf("\nResearchScreen_Open @ 0x616ac, Ui_OpenBaseInfo @ 0x84AF8, "
           "Geoscape_PrepScreenOpen @ 0x85194\n");

    for (i = 0; i < PUFO_GEOSCAPE_SPEED_COUNT; ++i) {
        apCtl[i] = &ctl[i];
    }

#define SRO_RESET()                                                                        \
    do {                                                                                   \
        memset(&state, 0, sizeof(state));                                                  \
        memset(&clock, 0, sizeof(clock));                                                  \
        memset(&world, 0, sizeof(world));                                                  \
        memset(ctl, 0, sizeof(ctl));                                                       \
        memset(&lines, 0, sizeof(lines));                                                  \
        memset(abActive, 0, sizeof(abActive));                                             \
        memset(szMsg, 'x', sizeof(szMsg)); szMsg[sizeof(szMsg) - 1] = 0;                   \
        memset(szScratch, 'y', sizeof(szScratch)); szScratch[sizeof(szScratch) - 1] = 0;   \
        bCount = 0;                                                                        \
        dwShortfall = 0x7777u;                                                             \
        g_nSroSampleCalls = 0; g_nSroSampleId = 0; g_nSroSampleArg = 0;                    \
        g_nSroInitCalls = 0; g_pSroInitSlot = NULL;                                        \
        state.nGeoscapeScreen = 3;                                                         \
        world.nTickSeconds = 600;                                                          \
        env.pState = &state;                                                               \
        env.pszMsg = szMsg; env.cchMsg = sizeof(szMsg);                                    \
        env.pszScratch = szScratch; env.cchScratch = sizeof(szScratch);                    \
        env.abResearchActive = abActive; env.pbResearchCount = &bCount;                    \
        env.pdwShortfall = &dwShortfall;                                                   \
        env.pTime = &clock;                                                                \
        env.pWorld = &world;                                                               \
        env.apSpeedCtl = apCtl;                                                            \
        env.pLines = &lines;                                                               \
        env.init.pfnPlaySample = SroSample;                                                \
        env.init.pfnInit = SroInit;                                                        \
        env.init.pSlot = szSlot;                                                           \
        env.init.pUser = NULL;                                                             \
    } while (0)

    /* ---- the shared prologue, checked through ResearchScreen_Open ---- */
    SRO_RESET();
    bCount = 3;
    abActive[0] = 1; abActive[1] = 0; abActive[2] = 1;
    ResearchScreen_Open(&env);
    CheckEqI32(g_nSroSampleCalls, 1, "ResearchScreen_Open plays a sample first (0x616B4)");
    CheckEqI32((i32)g_nSroSampleId, 17, "  sample 17");
    CheckEqI32((i32)g_nSroSampleArg, 4, "  with the second argument 4");
    CheckEqI32(szMsg[0], 0, "szRadarMsg is cleared (strcpy from the all-zero 0x10DA38)");
    CheckEqI32(szScratch[0], 0, "unk_186420 is cleared the same way");
    CheckEqI32((i32)abActive[0], 2, "an active entry marked 1 becomes 2");
    CheckEqI32((i32)abActive[1], 0, "an entry marked 0 is left alone");
    CheckEqI32((i32)abActive[2], 2, "and the third entry too");
    Check(dwShortfall == 0, "the shortfall dword is cleared with a 4-byte store (0x61754)");
    CheckEqI32(world.nTickSeconds, 5, "GameTime_Set5Sec puts the geoscape on a 5 s tick");
    CheckEqI32(ctl[0].nState, 1, "  and lights speed control 0");
    CheckEqI32(ctl[1].nState, 0, "  and darkens the other five");
    CheckEqI32(ctl[5].nState, 0, "  including control 5");
    CheckEqI32(state.nGeoscapeScreen, 8, "the screen id 8 is written BEFORE the Init (0x6176C)");
    CheckEqI32(g_nSroInitCalls, 1, "the Init is a tail call");
    Check(g_pSroInitSlot == (void *)szSlot, "  and gets the screen slot the hooks carry");

    /* ---- Ui_OpenBaseInfo differs only in the id ---- */
    SRO_RESET();
    Ui_OpenBaseInfo(&env);
    CheckEqI32(g_nSroSampleCalls, 1, "Ui_OpenBaseInfo also plays sample 17 (0x84AFC)");
    CheckEqI32(state.nGeoscapeScreen, 16, "with the screen id 16 (0x84BD8)");
    CheckEqI32(world.nTickSeconds, 5, "and the same 5 s tick");
    CheckEqI32(g_nSroInitCalls, 1, "then tail-calls Ui_InitBaseInfo");

    /* ---- Geoscape_PrepScreenOpen: no sample, no id, and the clock dance ---- */
    SRO_RESET();
    clock.bPaused = 1;
    lines.nLineCount = 2;
    lines.abLineFlags[0] = 99;
    lines.abLineFlags[1] = 40;
    Geoscape_PrepScreenOpen(&env);
    CheckEqI32(g_nSroSampleCalls, 0, "Geoscape_PrepScreenOpen plays no sample of its own");
    CheckEqI32(state.nGeoscapeScreen, 3, "and writes no screen id");
    CheckEqI32(lines.nLineCount, 1, "line 0 of the root message list is removed (0x8525C)");
    CheckEqI32(lines.abLineFlags[0], 40, "  so the old line 1 is now line 0");
    CheckEqI32(clock.bPaused, 1, "a flag of 40 re-pauses the clock (0x8528C GameTime_Pause)");
    CheckEqI32(world.nTickSeconds, 5, "the prologue still runs, tick 5");

    /* The two thresholds, each on its own GetLineFlag call. The list needs two lines: Remove(0)
     * shifts line 1 down into line 0, so the tested flag has to start at index 1. */
    static const u8 abFlags[6] = {0, 30, 31, 49, 50, 255};
    static const u8 abPaused[6] = {0, 0, 1, 1, 0, 0};
    for (i32 n = 0; n < 6; ++n) {
        SRO_RESET();
        clock.bPaused = 1;
        lines.nLineCount = 2;
        lines.abLineFlags[0] = 99;
        lines.abLineFlags[1] = abFlags[n];
        Geoscape_PrepScreenOpen(&env);
        CheckEqI32(clock.bPaused, abPaused[n],
                   "the pause window is flag > 30 and flag < 50, two separate reads");
    }

    /* An empty list: Remove leaves the count at zero and the flag reads 0, so no pause. */
    SRO_RESET();
    clock.bPaused = 1;
    Geoscape_PrepScreenOpen(&env);
    CheckEqI32(lines.nLineCount, 0, "removing from an empty list keeps the count at 0");
    CheckEqI32(clock.bPaused, 0, "  and the clock stays un-paused");

    /* ---- the research walk on its own ---- */
    SRO_RESET();
    bCount = 0;
    abActive[0] = 1;
    ResearchScreen_Open(&env);
    CheckEqI32((i32)abActive[0], 1, "a zero count skips the walk entirely, entry 0 included");

    SRO_RESET();
    bCount = 1;
    abActive[0] = 1;
    ResearchScreen_Open(&env);
    CheckEqI32((i32)abActive[0], 2, "a non-zero count is a do/while: entry 0 is always examined");

    SRO_RESET();
    bCount = 4;
    abActive[0] = 2; abActive[1] = 3; abActive[2] = 255; abActive[3] = 1;
    ResearchScreen_Open(&env);
    CheckEqI32((i32)abActive[0], 2, "only the entries marked exactly 1 are promoted");
    CheckEqI32((i32)abActive[1], 3, "  2 stays 2");
    CheckEqI32((i32)abActive[2], 255, "  255 stays 255");
    CheckEqI32((i32)abActive[3], 2, "  1 becomes 2");

    /* Deviation: the count is a byte and could ask for 255 of the 144 entries. */
    SRO_RESET();
    bCount = 255;
    for (i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        abActive[i] = 1;
    }
    abActive[PUFO_RESEARCH_TOPIC_COUNT - 1] = 1;
    ResearchScreen_Open(&env);
    CheckEqI32((i32)abActive[PUFO_RESEARCH_TOPIC_COUNT - 1], 2,
               "the walk stops at PUFO_RESEARCH_TOPIC_COUNT, not at the byte count");

    /* Nulls. The clock pair stays valid: the originals call GameTime_Set5Sec unconditionally and
     * the port's version writes through both, exactly as the original writes its two globals. */
    SRO_RESET();
    env.pState = NULL; env.pszMsg = NULL; env.pszScratch = NULL;
    env.abResearchActive = NULL; env.pbResearchCount = NULL;
    env.pdwShortfall = NULL; env.pLines = NULL;
    env.init.pfnPlaySample = NULL; env.init.pfnInit = NULL;
    ResearchScreen_Open(&env);
    Ui_OpenBaseInfo(&env);
    Geoscape_PrepScreenOpen(&env);
    ResearchScreen_Open(NULL);
    Ui_OpenBaseInfo(NULL);
    Geoscape_PrepScreenOpen(NULL);
    Check(true, "a fully nulled env runs without touching anything");
#undef SRO_RESET
}

static void FreeNavControls(NavControls *pNav)
{
    i32 i;
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        free(pNav->apNav[i]);
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        free(pNav->apPanel[i]);
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        free(pNav->apFind[i]);
    }
    memset(pNav, 0, sizeof(*pNav));
}

static void TestOpenSfxAndAltVariants(void)
{
    static GeoscapeScreenState state;
    static GameTime clock;
    static GeoscapeWorld world;
    static UiControl ctl[PUFO_GEOSCAPE_SPEED_COUNT];
    static UiControl *apCtl[PUFO_GEOSCAPE_SPEED_COUNT];
    static UiLineList lines;
    static u8 abActive[PUFO_RESEARCH_TOPIC_COUNT];
    static u8 bCount;
    static u32 dwShortfall;
    static char szMsg[32];
    static char szScratch[32];
    static char szSlot[4];
    static NavControls nav;
    static NavCtlEnv navEnv;
    static UiControl parkCtl;
    static u8 bReselect;
    ScreenResetOpenEnv env;
    ScreenResetOpen2Env env2;
    i32 i;

    printf("\nGeoscape_PrepScreenOpenSfx @ 0x852b8, Ui_OpenBaseInfoAlt @ 0x84d18, "
           "Ui_OpenResearch @ 0x84bf8\n");

    for (i = 0; i < PUFO_GEOSCAPE_SPEED_COUNT; ++i) {
        apCtl[i] = &ctl[i];
    }
    memset(&navEnv, 0, sizeof(navEnv));
    navEnv.pfnLoadPads = NavLoadPads;

#define SRO2_RESET()                                                                       \
    do {                                                                                   \
        memset(&state, 0, sizeof(state));                                                  \
        memset(&clock, 0, sizeof(clock));                                                  \
        memset(&world, 0, sizeof(world));                                                  \
        memset(ctl, 0, sizeof(ctl));                                                       \
        memset(&lines, 0, sizeof(lines));                                                  \
        memset(abActive, 0, sizeof(abActive));                                             \
        memset(szMsg, 'x', sizeof(szMsg)); szMsg[sizeof(szMsg) - 1] = 0;                   \
        memset(szScratch, 'y', sizeof(szScratch)); szScratch[sizeof(szScratch) - 1] = 0;   \
        memset(&parkCtl, 0, sizeof(parkCtl));                                              \
        parkCtl.x = 3; parkCtl.y = 4;                                                      \
        bCount = 0;                                                                        \
        bReselect = 0;                                                                     \
        dwShortfall = 0x7777u;                                                             \
        g_nSroSampleCalls = 0;                                                             \
        g_nSroInitCalls = 0; g_pSroInitSlot = NULL;                                        \
        g_nPadLoads = 0;                                                                   \
        state.nGeoscapeScreen = 3;                                                         \
        world.nTickSeconds = 600;                                                          \
        env.pState = &state;                                                               \
        env.pszMsg = szMsg; env.cchMsg = sizeof(szMsg);                                    \
        env.pszScratch = szScratch; env.cchScratch = sizeof(szScratch);                    \
        env.abResearchActive = abActive; env.pbResearchCount = &bCount;                    \
        env.pdwShortfall = &dwShortfall;                                                   \
        env.pTime = &clock;                                                                \
        env.pWorld = &world;                                                               \
        env.apSpeedCtl = apCtl;                                                            \
        env.pLines = &lines;                                                               \
        env.init.pfnPlaySample = SroSample;                                                \
        env.init.pfnInit = SroInit;                                                        \
        env.init.pSlot = szSlot;                                                           \
        env.init.pUser = NULL;                                                             \
        memcpy(&env2.base, &env, sizeof(env));                                             \
        env2.pNavOut = &nav; env2.pNavEnv = &navEnv;                                       \
        env2.pParkCtl = &parkCtl;                                                          \
        env2.pbUfopaediaReselect = &bReselect;                                             \
    } while (0)

    /* ---- Geoscape_PrepScreenOpenSfx: the sample is in, the tick is out ---- */
    SRO2_RESET();
    clock.bPaused = 1;
    bCount = 2;
    abActive[0] = 1;
    lines.nLineCount = 2;
    lines.abLineFlags[0] = 99;
    lines.abLineFlags[1] = 40;
    Geoscape_PrepScreenOpenSfx(&env);
    CheckEqI32(g_nSroSampleCalls, 1, "the Sfx variant plays sample 17 itself (0x852D0)");
    CheckEqI32((i32)g_nSroSampleId, 17, "  sample 17");
    CheckEqI32((i32)g_nSroSampleArg, 4, "  with the second argument 4");
    CheckEqI32(world.nTickSeconds, 600, "and it is the only one of the six that never calls "
                                        "GameTime_Set5Sec (0x8537C falls through)");
    CheckEqI32(szMsg[0], 0, "the message scratch is still cleared");
    CheckEqI32((i32)abActive[0], 2, "the research walk still runs");
    Check(dwShortfall == 0, "the shortfall dword is still cleared (0x85374)");
    CheckEqI32(lines.nLineCount, 1, "line 0 is still removed (0x85384)");
    CheckEqI32(clock.bPaused, 1, "  and the flag-40 line still re-pauses the clock");

    SRO2_RESET();
    clock.bPaused = 1;
    Geoscape_PrepScreenOpenSfx(&env);
    CheckEqI32(clock.bPaused, 0, "the pause byte is cleared first and a flag of 0 does not re-set it");

    /* ---- Ui_OpenBaseInfoAlt: the prep runs, then the prologue runs again ---- */
    SRO2_RESET();
    bCount = 1;
    abActive[0] = 1;
    lines.nLineCount = 3;
    lines.abLineFlags[0] = 11;
    lines.abLineFlags[1] = 40;
    lines.abLineFlags[2] = 7;
    Ui_OpenBaseInfoAlt(&env2);
    CheckEqI32(bReselect, 1, "byte_18706C is set on the way in (0x84D1C)");
    CheckEqI32(lines.nLineCount, 2, "the message line is dropped ONCE: the second inline prologue "
                                    "at 0x84D30 has no RemoveLine, only Geoscape_PrepScreenOpen's tail does");
    CheckEqI32(lines.abLineFlags[0], 40, "  so the old line 1 is the one in slot 0");
    CheckEqI32((i32)abActive[0], 2, "the research walk does run twice, harmlessly");
    CheckEqI32(world.nTickSeconds, 5, "the 5 s tick is written twice");
    CheckEqI32(state.nGeoscapeScreen, 16, "the screen id is 16 (0x84DE4)");
    CheckEqI32(g_nSroInitCalls, 1, "one tail call into Ui_InitBaseInfo");
    CheckEqI32(g_nSroSampleCalls, 0, "no sample hook call of its own");

    /* The first pass pauses the clock and the second pass does not clear it: only
     * Geoscape_PrepScreenOpen clears the pause byte, and it runs once. */
    SRO2_RESET();
    lines.nLineCount = 2;
    lines.abLineFlags[0] = 11;
    lines.abLineFlags[1] = 40;
    Ui_OpenBaseInfoAlt(&env2);
    CheckEqI32(clock.bPaused, 1, "the first pass pauses the clock and the second leaves it paused");

    /* ---- Ui_OpenResearch: prologue without the tick, then the full prep, then the tick ---- */
    SRO2_RESET();
    bCount = 1;
    abActive[0] = 1;
    lines.nLineCount = 3;
    lines.abLineFlags[0] = 11;
    lines.abLineFlags[1] = 40;
    lines.abLineFlags[2] = 7;
    Ui_OpenResearch(&env2);
    CheckEqI32(g_nSroSampleCalls, 0, "Ui_OpenResearch plays no sample of its own");
    CheckEqI32(g_nPadLoads, PUFO_NAV_CTLS + PUFO_NAV_PANEL_CTLS + PUFO_NAV_FIND_CTLS,
               "the fifteen nav controls are rebuilt (0x84CC8)");
    CheckEqI32(parkCtl.x, 1000, "unk_186BB4 parked at x 1000 (0x84CD8)");
    CheckEqI32(parkCtl.y, 1000, "  and y 1000 (0x84CE4)");
    CheckEqI32(state.nGeoscapeScreen, 8, "the screen id is 8 (0x84CEC)");
    CheckEqI32(world.nTickSeconds, 5, "the tick is set once, after the prep (0x84CC0)");
    CheckEqI32(lines.nLineCount, 2, "the message line is dropped once, by the prep call only");
    CheckEqI32(lines.abLineFlags[0], 40, "  and that is the line the pause test reads");
    CheckEqI32(clock.bPaused, 1, "the prep's flag-40 line pauses the clock (0x84CBC)");
    CheckEqI32((i32)abActive[0], 2, "the research walk runs twice here too");
    CheckEqI32(g_nSroInitCalls, 1, "one tail call into ResearchScreen_Init");
    FreeNavControls(&nav);

    /* Nulls. The nav pair stays valid: Geoscape_BuildNavControls writes through both, exactly as
     * the original writes through abFacilityGrid and its control array. */
    SRO2_RESET();
    env.pState = NULL; env.pszMsg = NULL; env.pszScratch = NULL;
    env.abResearchActive = NULL; env.pbResearchCount = NULL;
    env.pdwShortfall = NULL; env.pLines = NULL;
    env.init.pfnPlaySample = NULL; env.init.pfnInit = NULL;
    env2.pParkCtl = NULL; env2.pbUfopaediaReselect = NULL;
    memcpy(&env2.base, &env, sizeof(env));
    Geoscape_PrepScreenOpenSfx(&env);
    Ui_OpenBaseInfoAlt(&env2);
    Ui_OpenResearch(&env2);
    Geoscape_PrepScreenOpenSfx(NULL);
    Ui_OpenBaseInfoAlt(NULL);
    Ui_OpenResearch(NULL);
    FreeNavControls(&nav);
    Check(true, "the three variants survive a nulled env");
#undef SRO2_RESET
}

static Ship g_shipCancel;
static Ship *g_apShipCancel[1];
static BaseRec g_baseCancel;
static BaseRec *g_apBaseCancel[1];
static StrategItem g_ownCancel;
static StrategItem g_targetCancel;
static void *g_apSlotCancel[2];
static PtrArray g_listCancel;
static GeoscapeWorld g_worldCancel;
static StrategSpawnCtx g_ctxCancel;
static ShipRemovalEnv g_shipEnvCancel;
static Soldier *g_apSoldierCancel[1];
static ObjectRecFull *g_apObjCancel[1];
static u8 g_abHangarCancel[64];
static i32 g_nAssignedCancel[1];
static u8 g_nShipCountCancel;
static u16 g_nSoldierCountCancel;
static u8 g_onceCancel;
static i32 g_nTotalSecCancel;
static i32 g_nSeedCancel;

static void CancelEnvReset(void)
{
    memset(&g_shipCancel, 0, sizeof(g_shipCancel));
    g_shipCancel.bId = 21;
    g_shipCancel.bHomeBase = 0;
    g_apShipCancel[0] = &g_shipCancel;

    memset(&g_ownCancel, 0, sizeof(g_ownCancel));
    g_ownCancel.nType = 1;
    g_ownCancel.bActive = 0;
    g_ownCancel.nDisplayId = 21;
    g_ownCancel.nTimer = 777;
    g_ownCancel.nX = 40;
    g_ownCancel.nY = 60;

    memset(&g_targetCancel, 0, sizeof(g_targetCancel));
    g_targetCancel.nType = 5;
    g_targetCancel.nX = 300;
    g_targetCancel.nY = 400;

    g_apSlotCancel[0] = &g_ownCancel;
    g_apSlotCancel[1] = &g_targetCancel;
    memset(&g_listCancel, 0, sizeof(g_listCancel));
    g_listCancel.nCount = 2;
    g_listCancel.apItems = g_apSlotCancel;

    memset(&g_baseCancel, 0, sizeof(g_baseCancel));
    g_baseCancel.nUnk8 = 100;
    g_baseCancel.nUnk12 = 200;
    g_apBaseCancel[0] = &g_baseCancel;

    g_nShipCountCancel = 1;
    g_nSoldierCountCancel = 0;
    g_onceCancel = 0;
    g_nTotalSecCancel = 0;
    g_nSeedCancel = 0;
    memset(&g_ctxCancel, 0, sizeof(g_ctxCancel));
    g_ctxCancel.pnSeed = &g_nSeedCancel;
    memset(&g_worldCancel, 0, sizeof(g_worldCancel));
    g_worldCancel.apCraft = g_apShipCancel;
    g_worldCancel.nShipCount = 1;

    memset(&g_shipEnvCancel, 0, sizeof(g_shipEnvCancel));
    g_shipEnvCancel.apCraft = g_apShipCancel;
    g_shipEnvCancel.pnShipCount = &g_nShipCountCancel;
    g_shipEnvCancel.apSoldiers = g_apSoldierCancel;
    g_shipEnvCancel.pnSoldierCount = &g_nSoldierCountCancel;
    g_shipEnvCancel.apObjects = g_apObjCancel;
    g_shipEnvCancel.nObjectCount = 0;
    g_shipEnvCancel.pStrategList = &g_listCancel;
    g_shipEnvCancel.pWorld = &g_worldCancel;
    g_shipEnvCancel.pnSpawnOnceFlag = &g_onceCancel;
    g_shipEnvCancel.pSpawnCtx = &g_ctxCancel;
    g_shipEnvCancel.abHangarFlag = g_abHangarCancel;
    g_shipEnvCancel.nHangarFlagCap = 64;
    g_shipEnvCancel.anAssignedShip = g_nAssignedCancel;
    g_shipEnvCancel.pnAssignedShipCount = &g_nAssignedCancel[0];
    g_shipEnvCancel.apBases = g_apBaseCancel;
    g_shipEnvCancel.pnGameTotalSec = &g_nTotalSecCancel;
}

static void TestCancelTarget(void)
{
    static UiLineList lines;
    static GameTime clock;
    i32 nShipIdx;
    u8 bSel, b98, b8C, b90;
    SmapCancelTargetEnv env;

    printf("\nSmap_CancelTarget @ 0x84254\n");

    /* The target index lives in colour SLOT 1; slot 0 carries the ship index for the other
     * handlers, so a decoy there proves which slot this one reads. */
    CancelEnvReset();
    memset(&lines, 0, sizeof(lines));
    lines.nLineCount = 2;
    UiLineList_SetLine(&lines, 0, 11, 999, 1, 0, 0);
    nShipIdx = 0;
    bSel = 1; b98 = 1; b8C = 1; b90 = 1;
    clock.bPaused = 1;
    memset(&env, 0, sizeof(env));
    env.pLines = &lines;
    env.pnSelectedShipIdx = &nShipIdx;
    env.pbSelectionFlag = &bSel;
    env.pbFlag187098 = &b98;
    env.pbFlag18708C = &b8C;
    env.pbFlag187090 = &b90;
    env.pTime = &clock;
    env.pShipEnv = &g_shipEnvCancel;

    Smap_CancelTarget(&env);
    CheckEqI32(g_ownCancel.nStartX, 40, "the intercept runs, so the target came from slot 1 (0x84268)");
    CheckEqI32(g_ownCancel.nDestX, 300, "  and it is the entry slot 1 names");
    CheckEqI32(g_shipCancel.bIntercepting, 1, "  which the ship is now chasing");
    CheckEqI32(bSel, 0, "bMapSelectionFlag is cleared (0x84284)");
    CheckEqI32(b98, 0, "0x187098 is cleared (0x84290)");
    CheckEqI32(lines.nLineCount, 1, "message line 0 is dropped (0x8429C)");
    CheckEqI32(clock.bPaused, 1, "a line is still standing, so the clock stays paused (0x842A4)");
    CheckEqI32(b8C, 0, "0x18708C is cleared (0x842BC)");
    CheckEqI32(b90, 0, "0x187090 is cleared (0x842C8)");

    /* Slot 0 is NOT the one read: with the valid index there and 999 in slot 1 nothing launches. */
    CancelEnvReset();
    memset(&lines, 0, sizeof(lines));
    lines.nLineCount = 1;
    UiLineList_SetLine(&lines, 0, 11, 1, 999, 0, 0);
    Smap_CancelTarget(&env);
    CheckEqI32(g_ownCancel.nTimer, 777, "a valid index in slot 0 does not launch anything");
    CheckEqI32(g_shipCancel.bIntercepting, 0, "  and the ship keeps its intercept state");

    /* The last line down un-pauses the clock. */
    CancelEnvReset();
    memset(&lines, 0, sizeof(lines));
    lines.nLineCount = 1;
    UiLineList_SetLine(&lines, 0, 11, 999, 1, 0, 0);
    clock.bPaused = 1;
    Smap_CancelTarget(&env);
    CheckEqI32(lines.nLineCount, 0, "the removal empties the list");
    CheckEqI32(clock.bPaused, 0, "and the root line-list count byte is read after the removal (0x842A4)");

    /* Nulls. */
    env.pLines = NULL;
    Smap_CancelTarget(&env);
    Smap_CancelTarget(NULL);
    Check(true, "a null env and a null line list both do nothing");
}

static void TestSelectedAnswers(void)
{
    static UiLineList lines;
    static GameTime clock;
    u8 bSel;
    SmapSelectedEnv env;

    printf("\nSmap_ConfirmSelected @ 0x845d8, Smap_ConfirmSelectedAlt @ 0x853e4\n");

    /* ConfirmSelected keeps the craft on its course and parks the timer. Slot 1 carries a decoy, so
     * a wrong slot would show up as a bad ship index. */
    CancelEnvReset();
    g_shipCancel.bIntercepting = 0; /* no waypoint to drop here; test_ship covers that path */
    g_ownCancel.nDestX = 555;
    g_ownCancel.nDestY = 666;
    memset(&lines, 0, sizeof(lines));
    lines.nLineCount = 2;
    UiLineList_SetLine(&lines, 0, 11, 0, 999, 0, 0);
    bSel = 1;
    clock.bPaused = 1;
    memset(&env, 0, sizeof(env));
    env.pLines = &lines;
    env.pShipEnv = &g_shipEnvCancel;
    env.pTime = &clock;
    env.pbSelectionFlag = &bSel;

    Smap_ConfirmSelected(&env);
    CheckEqI32(g_ownCancel.nTimer, 0xFFFF, "the timer is parked, not restarted (0x845FC)");
    CheckEqI32(g_ownCancel.nDestX, 555, "and no travel runs: the destination is untouched");
    CheckEqI32(g_ownCancel.nDestY, 666, "  in both axes");
    CheckEqI32(lines.nLineCount, 1, "then Smap_ClearSelection drops line 0 (0x84604)");
    CheckEqI32(clock.bPaused, 1, "  and a line is still standing, so the clock stays paused");
    CheckEqI32(bSel, 0, "  and the selection flag is cleared last (0x84674)");

    /* With the list emptied by the removal the clock is un-paused. */
    CancelEnvReset();
    g_shipCancel.bIntercepting = 0;
    g_ownCancel.nDestX = 555;
    g_ownCancel.nDestY = 666;
    memset(&lines, 0, sizeof(lines));
    lines.nLineCount = 1;
    UiLineList_SetLine(&lines, 0, 11, 0, 999, 0, 0);
    bSel = 1;
    clock.bPaused = 1;

    Smap_ConfirmSelected(&env);
    CheckEqI32(g_ownCancel.nTimer, 0xFFFF, "ConfirmSelected parks the timer instead of restarting it");
    CheckEqI32(g_ownCancel.nDestX, 555, "and runs no travel at all: the destination is untouched");
    CheckEqI32(g_ownCancel.nDestY, 666, "  in both axes");
    CheckEqI32(lines.nLineCount, 0, "the line is dropped");
    CheckEqI32(clock.bPaused, 0, "and with the list empty the clock is un-paused (0x84668)");

    /* The Alt variant: same ship call, tail order swapped. */
    CancelEnvReset();
    g_shipCancel.bIntercepting = 0;
    g_ownCancel.nTimer = 777;
    g_ownCancel.nDestX = 555;
    memset(&lines, 0, sizeof(lines));
    lines.nLineCount = 1;
    UiLineList_SetLine(&lines, 0, 11, 0, 999, 0, 0);
    bSel = 1;
    clock.bPaused = 1;

    Smap_ConfirmSelectedAlt(&env);
    CheckEqI32(g_ownCancel.nTimer, 0xFFFF, "the Alt variant answers the same way (0x8540C)");
    CheckEqI32(g_ownCancel.nDestX, 555, "  and still runs no travel");
    CheckEqI32(lines.nLineCount, 0, "the line is dropped (0x85414)");
    CheckEqI32(bSel, 0, "the selection flag is cleared BEFORE the count test here (0x85420)");
    CheckEqI32(clock.bPaused, 0, "  and the clock is still un-paused (0x8542C)");

    env.pLines = NULL;
    Smap_ConfirmSelected(&env);
    Smap_ConfirmSelectedAlt(&env);
    Smap_ConfirmSelected(NULL);
    Smap_ConfirmSelectedAlt(NULL);
    Check(true, "both survive a null env and a null line list");
}

static void TestConfirmCraftTarget(void)
{
    static UiLineList lines;
    static GameTime clock;
    static SmapView view;
    static PtrArray list;
    static Ship ship;
    static Ship *apShip[1];
    static BaseRec base;
    static BaseRec *apBase[1];
    static Soldier *apSoldier[1];
    static ObjectRecFull *apObj[1];
    static u8 abHangar[64];
    static i32 anAssigned[1];
    static StrategSpawnCtx ctx;
    static ShipRemovalEnv rem;
    static GeoscapeWorld world;
    static i32 nSeed, nPickX, nPickY, nShipIdx, nTotalSec;
    static u8 bSel, bOnce, nShipCount;
    static u16 nSoldierCount;
    SmapConfirmTargetEnv env;
    StrategItem *pOwn, *pWaypoint, *pNew;

    printf("\nSmap_ConfirmCraftTarget @ 0x842ec\n");

    /* Heap items: StrategList_RemoveAt frees what it removes, so static storage would be freed. */
    PtrArray_ctor(&list);
    pOwn = (StrategItem *)calloc(1, sizeof(StrategItem));
    pWaypoint = (StrategItem *)calloc(1, sizeof(StrategItem));
    pOwn->nType = 1;
    pOwn->bActive = 0;
    pOwn->nDisplayId = 22;
    pOwn->nTimer = 777;
    pOwn->nX = 40;
    pOwn->nY = 60;
    pWaypoint->nType = 5;
    pWaypoint->nX = 300;
    pWaypoint->nY = 400;
    PtrArray_Add(&list, pOwn);
    PtrArray_Add(&list, pWaypoint);

    memset(&ship, 0, sizeof(ship));
    ship.bId = 22;
    ship.bHomeBase = 0;
    apShip[0] = &ship;

    memset(&base, 0, sizeof(base));
    base.nUnk8 = 100;
    base.nUnk12 = 200;
    apBase[0] = &base;

    nShipCount = 1;
    nSoldierCount = 0;
    bOnce = 0;
    nTotalSec = 0;
    nSeed = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnSeed = &nSeed;
    memset(&rem, 0, sizeof(rem));
    rem.apCraft = apShip;
    rem.pnShipCount = &nShipCount;
    rem.apSoldiers = apSoldier;
    rem.pnSoldierCount = &nSoldierCount;
    rem.apObjects = apObj;
    rem.nObjectCount = 0;
    rem.pStrategList = &list;
    memset(&world, 0, sizeof(world));
    world.apCraft = apShip;
    world.nShipCount = 1;
    rem.pWorld = &world;
    rem.pnSpawnOnceFlag = &bOnce;
    rem.pSpawnCtx = &ctx;
    rem.abHangarFlag = abHangar;
    rem.nHangarFlagCap = 64;
    rem.anAssignedShip = anAssigned;
    rem.pnAssignedShipCount = &anAssigned[0];
    rem.apBases = apBase;
    rem.pnGameTotalSec = &nTotalSec;

    memset(&view, 0, sizeof(view));
    view.nMode = 0; /* zoom 0 does not scale a type-5 position */
    nPickX = 700;
    nPickY = 350;
    nShipIdx = 99;
    bSel = 1;
    clock.bPaused = 1;

    memset(&lines, 0, sizeof(lines));
    lines.nLineCount = 1;
    UiLineList_SetLine(&lines, 0, 11, 0, 0, 0, 0);

    memset(&env, 0, sizeof(env));
    env.pLines = &lines;
    env.pnSelectedShipIdx = &nShipIdx;
    env.pStrategList = &list;
    env.pnSeed = &nSeed;
    env.pView = &view;
    env.pnPickX = &nPickX;
    env.pnPickY = &nPickY;
    env.pShipEnv = &rem;
    env.pTime = &clock;
    env.pbSelectionFlag = &bSel;

    /* 1. Chasing a waypoint: it is removed, a new one is added, and the craft is sent to the new one. */
    ship.bIntercepting = 1;
    ship.nStrategIdx = 1; /* pWaypoint, nType 5 */
    Smap_ConfirmCraftTarget(&env);
    CheckEqI32(nShipIdx, 0, "the ship index from colour slot 0 is stored into 0x187054 (0x84310)");
    CheckEqI32(list.nCount, 2, "the old waypoint is removed and a new one is appended");
    Check(PtrArray_Get(&list, 0) == pOwn, "the craft's own entry is the one that survives");
    pNew = (StrategItem *)PtrArray_Get(&list, 1);
    /* The allocator hands back the block the removed waypoint just freed, so identity proves
     * nothing; the coordinates do. */
    Check(pNew != NULL, "index 1 holds a live item");
    CheckEqI32(pNew->nType, 5, "  nType 5 (0x843A8)");
    CheckEqI32(pNew->bActive, 1, "  bActive 1");
    CheckEqI32(pNew->bCraftClass, 0, "  bCraftClass 0");
    CheckEqI32(pNew->bVisible, 1, "  bVisible 1");
    CheckEqI32(pNew->nX, 700, "  at the pending click x, unscaled at zoom 0");
    CheckEqI32(pNew->nY, 350, "  and click y");
    CheckEqI32(ship.nStrategIdx, 1, "the craft is launched at GetCount()-1 (0x843D0)");
    CheckEqI32(ship.bIntercepting, 1, "and Ship_LaunchIntercept re-arms the intercept flag");
    CheckEqI32(pOwn->nStartX, 40, "the leg starts at the craft's own entry position");
    CheckEqI32(pOwn->nDestX, 700, "and heads for the new waypoint's click x, not the old one's 300");
    CheckEqI32(lines.nLineCount, 0, "message line 0 is dropped (0x843E4)");
    CheckEqI32(clock.bPaused, 0, "and with the list empty the clock is un-paused (0x843EC)");
    CheckEqI32(bSel, 0, "and the selection flag is cleared (0x843F8)");

    /* 2. Chasing a real contact (nType 1): nothing is removed, so the list only grows. */
    ship.bIntercepting = 1;
    ship.nStrategIdx = 0; /* pOwn, nType 1 */
    lines.nLineCount = 2;
    clock.bPaused = 1;
    bSel = 1;
    Smap_ConfirmCraftTarget(&env);
    CheckEqI32(list.nCount, 3, "a non-waypoint target is left in place, so only the add happens");
    Check(PtrArray_Get(&list, 0) == pOwn && PtrArray_Get(&list, 1) == pNew,
          "  both earlier items are still there");
    CheckEqI32(ship.nStrategIdx, 2, "  and the craft is launched at the new last index");
    CheckEqI32(lines.nLineCount, 1, "one line dropped out of two");
    CheckEqI32(clock.bPaused, 1, "  so the clock stays paused");

    /* 3. Zoom 1 scales a type-5 position by 4/3, truncating toward zero. */
    view.nMode = 1;
    nPickX = 300;
    nPickY = 150;
    ship.bIntercepting = 0;
    lines.nLineCount = 1;
    Smap_ConfirmCraftTarget(&env);
    pNew = (StrategItem *)PtrArray_Get(&list, list.nCount - 1);
    CheckEqI32(pNew->nX, 399, "zoom 1 scales the click by 1.333 (300 becomes 399)");
    CheckEqI32(pNew->nY, 199, "  in both axes");
    CheckEqI32(ship.bIntercepting, 1, "a craft that was not intercepting is launched anyway");

    PtrArray_DeleteAll(&list);
    Smap_ConfirmCraftTarget(NULL);
    Check(true, "a null env does nothing");
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
    TestPanelScrollFamily();
    TestRoleToItemId();
    TestAlienRaceId();
    TestFocusDispatcher();
    TestBuySellBuildPanels();
    TestBuySellCommitTransaction();
    TestBuySellRowEditors();
    TestBuySellCapacityGate();
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
    TestClearProductSlot();
    TestIncEngineers();
    TestTogglePanelMode();
    TestBuySellConfirm();
    TestFundingBonus();
    TestScreenResetOpen();
    TestOpenSfxAndAltVariants();
    TestCancelTarget();
    TestSelectedAnswers();
    TestConfirmCraftTarget();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
