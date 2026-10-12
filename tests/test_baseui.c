/* test_baseui.c - base-screen scratch-cell helpers. */
#include <stdio.h>
#include <string.h>

#include "game/pufo_baseui.h"

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
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestScratch();
    TestGeoscapeScreens();
    TestPanelPaging();
    TestSelectionCaches();
    TestBaseInfoOk();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
