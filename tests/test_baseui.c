/* test_baseui.c - base-screen scratch-cell helpers. */
#include <stdio.h>
#include <stdlib.h> /* free, the nav controls the test releases */
#include <string.h>

#include "game/pufo_baseui.h"
#include "game/pufo_plane.h"  /* PlaneDrawEnv, BasePlaneScreen_Draw @ 0x7d374 */
#include "game/pufo_font.h"  /* PufoFont_Init, g_fontSys: the dump renders real labels */
#include "asset/pufo_asset.h" /* Asset_OpenArchives */
#include "game/pufo_strateg.h" /* StrategList_ctor, StrategItem_AllocAdd */
#include "game/pufo_boot.h"    /* PUFO_TEXT_REC_STRIDE, for the cost label */
#include "audio/pufo_audio.h"  /* PufoAudioBackend / Sound_SetBackend, the volume seam */
#include "util/pufo_rand.h"    /* Game_RandU16, seeded to pin the unlock roll exactly */

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
    UiControl ctlB; /* *pGeoscapeCtlB @ 0x186BB0 */
    UiControl ctlA; /* *pGeoscapeCtlA @ 0x186BB4 */
    i8 nTab;        /* gOptionsScratch[0] @ 0x159E88 */

    printf("BaseUi scratch setters\n");
    memset(&s, 0, sizeof(s));
    memset(&ctlB, 0, sizeof(ctlB));
    memset(&ctlA, 0, sizeof(ctlA));

    nTab = -1;
    BaseUi_SetScratch4(&nTab);
    CheckEqI32(nTab, 4, "  the tab byte is 4 (STRB at 0x6B6B8)");

    nTab = -1;
    ctlB.y = 1;
    ctlA.y = 1;
    BaseUi_SetScratch3AndUiY(&nTab, &ctlB, &ctlA);
    CheckEqI32(nTab, 3, "  the tab byte is 3 (STRB at 0x6B680)");
    CheckEqI32(ctlB.y, 120, "  *pGeoscapeCtlB's own y halfword is 120 (STRH at 0x6B68C)");
    CheckEqI32(ctlA.y, 142, "  *pGeoscapeCtlA's own y halfword is 142 (STRH at 0x6B69C)");

    /* Cache_ObjPlus0xBD8_To_dword_186BE8 @ 0x7afec: read the panel's dword at +0xBD8, store it into
     * dword_186BE8, and return it -- the store is the only effect (0x7B000 then 0x7B004). The port's
     * global lives in the scratch block, so the check reads it there. */
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
    BaseUi_SetScratch3AndUiY(NULL, NULL, NULL);
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
    Ui_CloseScreen18(&st, &h);
    CheckEqI32(g_nSamples[0], 17, "  close 18 plays sample 17 @ 0x84ecc");
    CheckEqI32(st.nGeoscapeScreen, 18, "  close 18 also sets id 18 @ 0x84ecc");
    CheckEqI32(g_nOptionsOpen, 1, "  close 18 opens options @ 0x84ecc");
    CheckEqI32(g_nOptionsOpenArg, -1, "  the mode is the literal -1 (0x84EC8 MOV R1,#0xFFFFFFFF; "
                                      "0x6DD08 saves it and never reads the first argument)");

    g_nSampleCount = 0;
    g_nFundingOpen = 0;
    Ui_OpenFunding(&st, &h);
    CheckEqI32(g_nSamples[0], 17, "  funding plays sample 17 @ 0x85164");
    CheckEqI32(st.nGeoscapeScreen, 15, "  funding screen id 15 @ 0x85164");
    CheckEqI32(g_nFundingOpen, 1, "  funding screen opened @ 0x85164");
}

static void TestUfopaediaCache(void)
{
    u8 bFlag = 0;
    i32 nCached = -1;

    printf("Ufopaedia_CacheSelRow\n");

    CheckEqI32(Ufopaedia_CacheSelRow(&bFlag, &nCached, 4) ? 1 : 0, 1,
               "the first call caches and raises the flag");
    CheckEqI32(nCached, 4, "the row that was current is the row that gets stored @ 0x99fd0");
    CheckEqI32(bFlag, 1, "and the flag is raised @ 0x99fd0");

    /* Once the flag is up the function does nothing at all - the cache is never refreshed. */
    CheckEqI32(Ufopaedia_CacheSelRow(&bFlag, &nCached, 9) ? 1 : 0, 0,
               "a second call caches nothing @ 0x99fd0");
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

    /* One press moves all four cursors. They are not per-panel controls: each original is 14
     * instructions that load the four panel pointers (anLineAux[117], [141], [142], [118]) and run one
     * scroll step on each, the fourth through a tail call after the POP (0x6D050 / 0x6D098). */
    OptionsScreen_PanelPrev(apPanels);
    for (int i = 0; i < 4; ++i) {
        CheckEqI32(aPanels[i].nUnk3028, 2, "prev steps every one of the four cursors @ 0x6d01c");
    }

    OptionsScreen_PanelNext(apPanels);
    OptionsScreen_PanelNext(apPanels);
    for (int i = 0; i < 4; ++i) {
        CheckEqI32(aPanels[i].nUnk3028, 4, "next steps them all back @ 0x6d064");
    }

    /* The next step is bounded by lineCount - style = 15, the prev step by zero. */
    for (int i = 0; i < 40; ++i) {
        OptionsScreen_PanelNext(apPanels);
    }
    CheckEqI32(aPanels[0].nUnk3028, 15, "next stops at lineCount - style @ 0x6d064");

    for (int i = 0; i < 40; ++i) {
        OptionsScreen_PanelPrev(apPanels);
    }
    CheckEqI32(aPanels[0].nUnk3028, 0, "prev stops at zero @ 0x6d01c");
}

static void TestFundingHelpers(void)
{
    printf("Funding_ScoreToBarLevel @ 0x5aa60 and Funding_DeltaToArrow\n");

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

/* One cite builder for every test in this file: the message plus `@ 0x...`, which is what the
 * `test_cites_addr` column of docs/parity/teeth.tsv looks for. */
static char g_szOptCited[256];
static const char *OptCite(const char *pszText, const char *pszAddr)
{
    snprintf(g_szOptCited, sizeof(g_szOptCited), "%s %s", pszText, pszAddr);
    return g_szOptCited;
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

    /* BaseFacScreen_PanelPrev @ 0x589cc / PanelNext @ 0x589dc: 16 bytes each, one panel global (0x15886C),
     * `LDR R0,=pBaseFacPanel` / `LDR R0,[R0]` / tail call into the step. Three instructions plus one pool word
     * -- the only panel pair in the game that moves a single panel. */
    BaseFacScreen_PanelPrev(&a);
    CheckEqI32(a.nUnk3028, 2, OptCite("prev decrements the one cursor", "@ 0x589cc"));
    BaseFacScreen_PanelNext(&a);
    CheckEqI32(a.nUnk3028, 3, OptCite("next increments while the limit is not reached", "@ 0x589dc"));
    a.nUnk3028 = 5;
    BaseFacScreen_PanelNext(&a);
    CheckEqI32(a.nUnk3028, 5, OptCite("next stops at nLineCount - nStyle", "@ 0x589dc"));
    a.nUnk3028 = 0;
    BaseFacScreen_PanelPrev(&a);
    CheckEqI32(a.nUnk3028, 0, OptCite("prev floors at zero, with no wrap", "@ 0x589cc"));
    a.nUnk3028 = 5; /* back to the value the SoldierScreen pair below starts from */

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

    /* The three selection caches are 40 bytes = 7 instructions (one of them the 2-word `MOV R3,#0xBD8`) + 2 pool
     * words, no stack frame and no call: read the panel pointer out of the first pool global, load the dword at
     * panel+0xBD8, store it into the second pool global, RET. Compared word by word: 0x76138 and 0x96ab0 share all
     * 10 words; 0x6dd24 differs in exactly 2 (its panel is pManufPanelD @ 0x15EE38 and its destination is
     * unk_159E90, i.e. gUiRootPanel.anLineAux[147], while the other two read pBuySellPanelA @ 0x161A1C and write
     * nPanelRowCursor @ 0x186BE8). The port hands the panel+0xBD8 value in as an argument, which is the one place
     * the shape differs. */
    CheckEqI32(BuySellScreen_SyncSelection(&s, 7), 7,
               OptCite("BuySellScreen_SyncSelection returns the +0xBD8 value", "@ 0x76138"));
    CheckEqI32(s.nCache186BE8, 7, OptCite("and stores it in dword_186BE8 @ 0x186BE8", "@ 0x76138"));

    /* 0x96ab0 is byte for byte 0x76138 (0 of 10 words differ), so it lands in the same global. */
    CheckEqI32(Transfer_CacheSelIndex(&s, 3), 3, OptCite("Transfer_CacheSelIndex returns it", "@ 0x96ab0"));
    CheckEqI32(s.nCache186BE8, 3, OptCite("the transfer screen overwrites the same cache", "@ 0x96ab0"));

    CheckEqI32(ManufScreen_SyncSelection(&nAux147, 11), 11,
               OptCite("ManufScreen_SyncSelection returns the +0xBD8 value", "@ 0x6dd24"));
    CheckEqI32(nAux147, 11, OptCite("and writes gUiRootPanel.anLineAux[147] @ 0x159E90", "@ 0x6dd24"));
    CheckEqI32(s.nCache186BE8, 3, OptCite("it leaves the other cache alone", "@ 0x6dd24"));

    CheckEqI32(ManufScreen_SyncSelection(NULL, 5), 5,
               OptCite("a missing destination still returns", "@ 0x6dd24"));
}

static void TestBaseInfoOk(void)
{
    BaseInfoOkState st;

    printf("BaseInfoScreen_OnOk\n");

    st.nPendingBase = 2;
    st.bPsiPending = 0;
    st.nCurrentBase = 9;

    /* BaseInfoScreen_OnOk @ 0x557c8: 44 bytes = 8 instructions + 3 pool words, no stack frame and no call --
     * `STRB #1` into byte_1870F8, then `LDRSB R0,[byte_1870F4]` and `STRB R0,[byte_158834]`, then RET with R0
     * holding the SIGNED byte it just read. PsiScreen_OnOk @ 0x716a8 is byte-identical to it (compared word by
     * word: 0 of 11 words differ), so the port keeps one body (BaseInfoScreen_OnOk) for both. */
    CheckEqI32(BaseInfoScreen_OnOk(&st), 2, OptCite("OK returns the pending base index", "@ 0x557c8"));
    CheckEqI32(st.bPsiPending, 1, OptCite("OK arms the psi screen (0x557D0 STRB into byte_1870F8)", "@ 0x557c8"));
    CheckEqI32(st.nCurrentBase, 2, OptCite("and makes it the base the screens act on (0x557E0 STRB)",
                                           "@ 0x557c8"));

    /* 0x557DC uses LDRSB, so a negative index is not widened to a positive one. */
    st.nPendingBase = -1;
    st.bPsiPending = 0;
    st.nCurrentBase = 4;
    CheckEqI32(BaseInfoScreen_OnOk(&st), -1, OptCite("a negative index stays negative @ 0x557DC", "@ 0x557c8"));
    CheckEqI32(st.nCurrentBase, -1, OptCite("and is copied as a signed byte", "@ 0x557c8"));

    CheckEqI32(BaseInfoScreen_OnOk(NULL), 0, OptCite("NULL is tolerated (the original has no guard)",
                                                     "@ 0x557c8"));

    st.nPendingBase = 5;
    st.bPsiPending = 0;
    st.nCurrentBase = 1;
    CheckEqI32(PsiScreen_OnOk(&st), 5, OptCite("PsiScreen_OnOk behaves identically", "@ 0x716a8"));
    CheckEqI32(st.bPsiPending, 1, OptCite("it arms the psi screen", "@ 0x716a8"));
    CheckEqI32(st.nCurrentBase, 5, OptCite("and moves the same base index", "@ 0x716a8"));
}

static UiPanel g_aBuySellPanels[2];

static void TestBuySellPaging(void)
{
    printf("buy/sell list paging\n");

    memset(g_aBuySellPanels, 0, sizeof(g_aBuySellPanels));
    g_aBuySellPanels[0].nUnk3028 = 4;   /* pPlaneCrewPanel @ 0x161EDC */
    g_aBuySellPanels[0].nLineCount = 9;
    g_aBuySellPanels[0].nStyle = 3;
    g_aBuySellPanels[1].nUnk3028 = 1;   /* pPlaneCrewStatusPanel @ 0x161A3C */
    g_aBuySellPanels[1].nLineCount = 6;
    g_aBuySellPanels[1].nStyle = 2;

    /* BuySellScreen_ListPanelPrev @ 0x79a20 / ListPanelNext @ 0x79a48: 40 bytes = 8 instructions + 2 pool
     * words over pPlaneCrewPanel (0x161EDC) and pPlaneCrewStatusPanel (0x161A3C) -- the second step is the tail
     * call, so both cursors move on one press and nothing else does. */
    BuySellScreen_ListPanelNext(&g_aBuySellPanels[0], &g_aBuySellPanels[1]);
    CheckEqI32(g_aBuySellPanels[0].nUnk3028, 5, OptCite("both panels page", "@ 0x79a48"));
    CheckEqI32(g_aBuySellPanels[1].nUnk3028, 2, OptCite("the second one, through the tail call", "@ 0x79a48"));

    BuySellScreen_ListPanelPrev(&g_aBuySellPanels[0], &g_aBuySellPanels[1]);
    CheckEqI32(g_aBuySellPanels[0].nUnk3028, 4, OptCite("prev moves both back", "@ 0x79a20"));
    CheckEqI32(g_aBuySellPanels[1].nUnk3028, 1, OptCite("in the same slot order", "@ 0x79a20"));

    g_aBuySellPanels[0].nUnk3028 = 6;
    BuySellScreen_ListPanelNext(&g_aBuySellPanels[0], &g_aBuySellPanels[1]);
    CheckEqI32(g_aBuySellPanels[0].nUnk3028, 6, OptCite("next stops at nLineCount - nStyle", "@ 0x79a48"));

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

    /* ManufScreen_PanelPrev @ 0x6dd4c / PanelNext @ 0x6dd94 (72 B = 14 insns + 4 pool words over
     * pManufProductPanel, pManufPanelB/C/D) and PsiScreen_PanelPrev @ 0x715e8 / PanelNext @ 0x71648 (96 B =
     * 19 insns + 5 pool words). The Psi pair differs in HOW the four columns are found: it re-reads
     * `LDRSB R3,[R4]` with R4 = byte_1870F4 before EVERY step (0x715F4, 0x71600, 0x71610, 0x71620) and indexes
     * four arrays -- g_apPsiNamePanel, g_apPsiStatPanel, g_apPsiDeltaPanel, g_apPsiCheckPanel -- so the SIGNED
     * byte selects which per-base set scrolls. The port resolves that byte at the caller and hands the four
     * resolved pointers here (see PsiScreen_ScrollEnv.apPsiPanels and the dispatcher case 17 at 0x8B198). */
    ResetScrollPanels();
    ManufScreen_PanelNext(apFour);
    CheckEqI32(g_aScrollPanels[0].nUnk3028, 3, OptCite("ManufScreen_PanelNext pages its four", "@ 0x6dd94"));
    CheckEqI32(g_aScrollPanels[3].nUnk3028, 3, OptCite("the tail-called one last, same effect", "@ 0x6dd94"));
    CheckEqI32(g_aScrollPanels[4].nUnk3028, 2, OptCite("and the fifth panel is not in that set", "@ 0x6dd94"));
    ManufScreen_PanelPrev(apFour);
    CheckEqI32(g_aScrollPanels[0].nUnk3028, 2, OptCite("ManufScreen_PanelPrev moves them back", "@ 0x6dd4c"));
    CheckEqI32(g_aScrollPanels[3].nUnk3028, 2, OptCite("all four, the tail-called one included", "@ 0x6dd4c"));

    PsiScreen_PanelNext(apFour);
    CheckEqI32(g_aScrollPanels[2].nUnk3028, 3, OptCite("PsiScreen_PanelNext pages its own four", "@ 0x71648"));
    CheckEqI32(g_aScrollPanels[3].nUnk3028, 3, OptCite("through the tail call too", "@ 0x71648"));
    CheckEqI32(g_aScrollPanels[4].nUnk3028, 2, OptCite("and stops at four columns", "@ 0x71648"));
    PsiScreen_PanelPrev(apFour);
    CheckEqI32(g_aScrollPanels[2].nUnk3028, 2, OptCite("and PsiScreen_PanelPrev returns them", "@ 0x715e8"));
    CheckEqI32(g_aScrollPanels[3].nUnk3028, 2, OptCite("all four back", "@ 0x715e8"));

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

    /* BuySellScreen_DetailPanelPrev @ 0x79a70 / DetailPanelNext @ 0x79ac8: 88 B = 17 insns + 5 pool words over
     * pPlaneSlotPanel, pPlaneSelRowPanel, pPlaneCommitRowPanel, pPlaneTakenPanel, pPlaneAvailPanel -- the
     * hangar's five stores columns, the same width as the transfer pair at 0x967e4/0x9683c. */
    BuySellScreen_DetailPanelNext(apFive);
    CheckEqI32(g_aScrollPanels[1].nUnk3028, 1, OptCite("the detail set pages the same way", "@ 0x79ac8"));
    CheckEqI32(g_aScrollPanels[4].nUnk3028, 1, OptCite("five columns, the tail-called last", "@ 0x79ac8"));
    BuySellScreen_DetailPanelPrev(apFive);
    CheckEqI32(g_aScrollPanels[1].nUnk3028, 0, OptCite("and back again", "@ 0x79a70"));
    CheckEqI32(g_aScrollPanels[4].nUnk3028, 0, OptCite("all five, and prev floors at zero", "@ 0x79a70"));

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
    const i32 nSnapOpen = 4147000;
    env.pnFundsSnapshot = &nSnapOpen;
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
    CheckEqI32(pA->nViewW, 125, "the name column is 125 wide @ 0x76de0");
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
    /* BuySellScreen_BuildPanels @ 0x76de0: 4236 bytes = 1000 instructions (34 of them 2-word MOV immediates) +
     * 25 pool words, 27 calls -- the five columns, built or released on the built byte. */
    CheckEqI32(nOk, 8, OptCite("every row gets its < and > columns and a zero pending quantity", "@ 0x76de0"));

    CheckEqI32(pB->anLineAux[1], 1,
               OptCite("a soldier row's decrement column holds the apSoldiers index", "@ 0x76de0"));
    CheckEqI32(pB->anLineAux[2], 0, OptCite("the other soldier was index zero", "@ 0x76de0"));
    CheckEqI32(pC->anLineAux[1], 102, OptCite("and its increment column holds the roster id", "@ 0x76de0"));
    CheckEqI32(pC->anLineAux[0], 7, OptCite("a craft row holds the craft id instead", "@ 0x76de0"));
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

/* Geoscape_CloseUI is ported, so "the close ran" is now an observable fact instead of a hook
 * counter: screen 13 has no case in its switch (def_8BA94), so the dispatcher reaches only its
 * head, and the head clears the action-fail dword at 0x8B96C. A still-set dword means the
 * dispatcher never ran. */
static i32 g_nCloseScreen;
static i32 g_nCloseFail;

static void CloseProbe(GeoscapeCloseEnv *pEnv)
{
    memset(pEnv, 0, sizeof(*pEnv));
    g_nCloseScreen = 13;
    g_nCloseFail = 1;
    pEnv->pnScreenId = &g_nCloseScreen;
    pEnv->pnActionFail = &g_nCloseFail;
}

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
    GeoscapeCloseEnv closeEnv;
    CloseProbe(&closeEnv);
    env.pClose = &closeEnv;

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
    CheckEqI32(g_nCloseFail, 0, "and the screen closes (0x75078 -> 0x8B96C)");
    CheckEqI32(g_nCloseScreen, 13, "  on a screen id the switch has no case for");

    CloseProbe(&closeEnv);
    BuySellScreen_CommitTransaction(NULL);
    CheckEqI32(g_nCloseFail, 1, "a missing environment does nothing at all");

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
    /* BuySellScreen_FillBuyPanel @ 0x75160: 772 bytes = 180 instructions (3 two-word MOVs) + 10 pool words, 5
     * calls. It walks the "<" column's cursor and rewrites the row pair it points at. */
    pB.nUnk3032 = 0;
    BuySellScreen_FillBuyPanel(&env);
    CheckEqI32(pE.anLineAux[0], 0, OptCite("the decrement column clears the soldier mark", "@ 0x75160"));
    CheckEqI32(pD.anLineAux[0], 1, OptCite("and hands the soldier back to the count column", "@ 0x75160"));
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
               OptCite("the count column is the only guard on the sell side", "@ 0x75878"));
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
    CheckEqI32(nFunds, 130350, "with 50000 refunded @ 0x75878");

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

static i32 g_nInitBuy, g_nInitSell, g_nInitControls, g_nInitPanels, g_nInitClose;
static void InitStubClose(void) { ++g_nInitClose; } /* every fixture in this group records: an empty
                                                     * body folds under /OPT:ICF into some other
                                                     * empty body's address */
static void InitStubBuy(void) { ++g_nInitBuy; }
static void InitStubSell(void) { ++g_nInitSell; }
static void InitStubControls(void) { ++g_nInitControls; }
static void InitStubPanels(void) { ++g_nInitPanels; }

static void TestBuySellInit(void)
{
    static PadImage *pScreen;
    static UiControl closeCtl, confirmCtl;
    static i32 aDelta[149];
    BuySellInitEnv env;
    i32 nFunds = 4147000;
    i32 nSnapshot = -1;
    i32 nTotal = 999;
    i32 nHire = 7;
    i32 nOk = 0;
    i32 i;

    printf("the buy/sell screen bring-up\n");

    for (i = 0; i < 149; ++i) {
        aDelta[i] = 1;
    }
    pScreen = NULL;
    memset(&closeCtl, 0, sizeof(closeCtl));
    memset(&confirmCtl, 0, sizeof(confirmCtl));
    confirmCtl.uWidth = 40;

    memset(&env, 0, sizeof(env));
    env.ppScreen = &pScreen;
    env.pCloseCtl = &closeCtl;
    env.pConfirmCtl = &confirmCtl;
    env.pfnCloseClick = InitStubClose;
    env.pfnConfirmBuy = InitStubBuy;
    env.pfnConfirmSell = InitStubSell;
    env.pfnBuildControls = InitStubControls;
    env.pfnBuildPanels = InitStubPanels;
    env.nScreenId = PUFO_BUYSSELL_SCREEN_BUY;
    env.pnFunds = &nFunds;
    env.pnFundsSnapshot = &nSnapshot;
    env.pnTransTotal = &nTotal;
    env.pnSoldierHireCount = &nHire;
    env.anSlotDelta = aDelta;
    env.nSlotDeltaCount = 149;
    g_nInitBuy = 0;
    g_nInitSell = 0;
    g_nInitControls = 0;
    g_nInitPanels = 0;

    BuySellScreen_Init(&env);
    CheckEqI32(nSnapshot, 4147000, "the funds snapshot is taken at the open, not only at the close");
    Check(pScreen != NULL, "the screen's pad object is allocated");
    CheckEqI32(pScreen != NULL ? pScreen->nOriginX : -1, 0, "with origin 0 on x");
    CheckEqI32(pScreen != NULL ? pScreen->nOriginY : -1, 0, "and on y");
    CheckEqI32(nTotal, 0, "the transaction total is cleared");
    CheckEqI32(nHire, 0, "and so is the soldier hire count");
    CheckEqI32(closeCtl.x, 224, "the close control lands at 224, 1");
    CheckEqI32(closeCtl.y, 1, "both as 16-bit stores");
    Check(closeCtl.pfnClick == InitStubClose, "and gets the close thunk");
    CheckEqI32(confirmCtl.x, 100, "the confirm control centres on its 40-pixel width");
    CheckEqI32(confirmCtl.y, 297, "at y 297");
    Check(confirmCtl.pfnClick == InitStubBuy, "screen 11 installs the site confirm");
    CheckEqI32(g_nInitControls, 1, "and builds the buy screen's controls");
    CheckEqI32(g_nInitPanels, 0, "not the sell panels");
    for (i = 0; i < 149; ++i) {
        if (aDelta[i] == 0) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 149, "the whole slot-delta array is cleared");

    free(pScreen);
    pScreen = NULL;
    nTotal = 999;
    nHire = 7;
    env.nScreenId = PUFO_BUYSSELL_SCREEN_SELL;
    confirmCtl.pfnClick = NULL;
    BuySellScreen_Init(&env);
    Check(confirmCtl.pfnClick == InitStubSell, "screen 12 installs the commit instead");
    CheckEqI32(g_nInitPanels, 1, "and builds the panels");
    CheckEqI32(g_nInitControls, 1, "leaving the buy controls alone");
    CheckEqI32(nTotal, 0, "the scratch is cleared on both modes");
    CheckEqI32(nHire, 0, "including the hire count");

    free(pScreen);
    pScreen = NULL;
    env.nScreenId = 3;
    confirmCtl.pfnClick = NULL;
    BuySellScreen_Init(&env);
    Check(confirmCtl.pfnClick == NULL, "an id that is neither leaves the confirm control alone");
    CheckEqI32(g_nInitPanels, 1, "and calls neither builder");
    CheckEqI32(g_nInitControls, 1, "on either side");

    free(pScreen);
    pScreen = NULL;
    BuySellScreen_Init(NULL);
    env.ppScreen = NULL;
    BuySellScreen_Init(&env);
    env.ppScreen = &pScreen;
    CheckEqI32(g_nInitPanels, 1, "a missing screen slot changes nothing");

    env.pCloseCtl = NULL;
    env.pConfirmCtl = NULL;
    BuySellScreen_Init(&env);
    CheckEqI32(g_nInitControls, 1, "missing controls are skipped, not dereferenced");
    env.pCloseCtl = &closeCtl;
    env.pConfirmCtl = &confirmCtl;

    free(pScreen);
    pScreen = NULL;
}

/* Three columns, three handlers: the selection re-read, the decrement and the increment. Distinct
 * bodies, or the three slot claims are one comparison repeated three times. */
static i32 g_nCtlStubId;
static void CtlStubSync(void)     { g_nCtlStubId = 1; }
static void CtlStubPrices(void)   { g_nCtlStubId = 2; }
static void CtlStubCapacity(void) { g_nCtlStubId = 3; }

static void TestBuySellBuildControls(void)
{
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static BaseRec aBase[1];
    static BaseRec *apBase[1];
    static SmapSite aSites[2];
    static u8 aText[160 * PUFO_TEXT_REC_STRIDE];
    static const i32 aWantAux[7] = { 117, 118, 1000, 112, 113, 1, 4 };
    static const char *const apWantName[7] = {
        "SKYRANGER", "INTERCEPTOR", "Soldier", "Engineer", "Scientist", "Pistol", "Auto cannon"
    };
    static const i32 aWantPrice[7] = { 500000, 600000, 40000, 50000, 60000, 800, 1200 };
    UiPanel *pA = NULL, *pB = NULL, *pC = NULL, *pD = NULL, *pE = NULL;
    UiControl aCtl[4];
    BuySellControlsEnv env;
    u8 bBuilt = 0;
    i32 nSiteCount = 1; /* the working site is the record AT the count, aSites[1] */
    i32 nFunds = 1000;
    i32 nOk = 0;
    i32 i;

    printf("the buy screen's object block\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    /* Six slots pass the include tests; the rest fail one of them, each on a different test. */
    PutStr(aObj[1].szName, "Pistol");
    PutStr(aObj[4].szName, "Auto cannon");
    PutStr(aObj[20].szName, "Motion scaner");   /* in range, no price */
    PutStr(aObj[21].szName, "Medi-kit");        /* in range, priced, but +0xBA is non-zero */
    PutStr(aObj[58].szName, "HWP blaster bomb");/* last in-range slot, no price */
    PutStr(aObj[59].szName, "Sectoid corpse");  /* priced, but 59 is outside the range */
    PutStr(aObj[112].szName, "Engineer");
    PutStr(aObj[113].szName, "Scientist");
    PutStr(aObj[114].szName, "Ethereal navigator"); /* priced, outside the range */
    PutStr(aObj[117].szName, "SKYRANGER");
    PutStr(aObj[118].szName, "INTERCEPTOR");
    PutStr(aObj[119].szName, "FIRE STORM");     /* priced, 119 is outside the range */
    PutStr(aObj[122].szName, "Personal armour");/* priced, outside the range */
    aObj[1].uPrice = 800;
    aObj[4].uPrice = 1200;
    aObj[20].uPrice = 0;
    aObj[20].uManufWorkPerUnit = 432;   /* the shipped Motion scaner value: work per unit, not a price */
    aObj[21].uPrice = 300;
    aObj[21].uManufWorkPerUnit = 816;   /* the shipped Medi-kit value */
    aObj[58].uPrice = 0;
    aObj[59].uPrice = 700;
    aObj[112].uPrice = 50000;
    aObj[113].uPrice = 60000;
    aObj[114].uPrice = 900;
    aObj[117].uPrice = 500000;
    aObj[118].uPrice = 600000;
    aObj[119].uPrice = 700000;
    aObj[122].uPrice = 1584;

    PutTextRec(aText, 159, "Soldier");

    aBase[0].bUnk4 = 3;
    apBase[0] = &aBase[0];

    /* A dirty working site, so the build branch's clearing is visible. */
    aSites[1].bFlag0 = 99;
    aSites[1].bFlag1 = 99;
    aSites[1].auItemQty[10] = 5;
    aSites[1].uRecoveredSoldiers = 3;
    aSites[1].bRecoveredSoldierAssign = 2;
    aSites[1].bRecoveredCraftCount = 1;
    aSites[1].bUnk518 = 77;
    aSites[1].anSoldierIds[0] = 4;
    aSites[1].anCraftIds[0] = 5;

    memset(&env, 0, sizeof(env));
    memset(aCtl, 0, sizeof(aCtl));
    env.pbControlsBuilt = &bBuilt;
    for (i = 0; i < 4; ++i) {
        env.apCtl[i] = &aCtl[i];
    }
    env.ppPanelA = &pA;
    env.ppPanelB = &pB;
    env.ppPanelC = &pC;
    env.ppPanelD = &pD;
    env.ppPanelE = &pE;
    env.aSites = aSites;
    env.pnSiteCount = &nSiteCount; /* a pointer, not a copy: 0x7619C and 0x76C14 re-read the cell live */
    env.apBases = apBase;
    env.nSelectedBaseIdx = 0;
    env.apObjects = apObj;
    env.nObjectCount = 149;
    env.pnFunds = &nFunds;
    const i32 nSnapOpen = 4147000;
    env.pnFundsSnapshot = &nSnapOpen;
    env.pTextRecs = (const char *)aText;
    env.pfnPanelPrev = BuySellTestPanelPrev;
    env.pfnPanelNext = BuySellTestPanelNext;
    env.pfnSyncSelection = CtlStubSync;
    env.pfnUpdatePrices = CtlStubPrices;
    env.pfnValidateCapacity = CtlStubCapacity;

    BuySellScreen_BuildControls(&env);

    CheckEqI32(bBuilt, 1, "the build branch sets the controls-built flag");
    CheckEqI32(aSites[1].bFlag0, PUFO_BUYSSELL_SITE_FLAG_NEW,
               "the working site aSmapSites[nSiteCount] is opened with flag 8");
    CheckEqI32(aSites[1].bFlag1, 3, "and its type byte copied from the selected base");
    CheckEqI32(aSites[1].bUnk518, 0, "the pending-craft byte is cleared here");
    CheckEqI32(aSites[1].uRecoveredSoldiers, 0, "the recovery count too");
    CheckEqI32(aSites[1].auItemQty[10], 0, "the whole item-quantity array is cleared");
    CheckEqI32(aSites[1].bRecoveredCraftCount, 1,
               "the build branch leaves the recovery craft count alone, 0x7619C..0x7620C");
    CheckEqI32(aSites[1].anSoldierIds[0], 4, "and the two id lists");
    CheckEqI32(aSites[1].anCraftIds[0], 5, "which only the teardown branch clears");
    Check(aCtl[0].pfnClick == BuySellTestPanelPrev, "the first control gets the prev handler");
    Check(aCtl[1].pfnClick == BuySellTestPanelNext, "the second gets the next handler");
    Check(aCtl[2].pfnClick == NULL, "the third and fourth get none");
    CheckEqI32(aCtl[0].x, 0xCD, "the four controls land on their descriptor offsets");
    CheckEqI32(aCtl[3].y, 0x129, "the same descriptors the sell mode uses");

    Check(pA != NULL && pB != NULL && pC != NULL && pD != NULL && pE != NULL,
          "all five panels are built");
    if (pA == NULL || pB == NULL || pC == NULL || pD == NULL || pE == NULL) {
        return;
    }

    /* Slots 1, 4, 112, 113, 117, 118 plus the soldier row. */
    CheckEqI32(pA->nLineCount, 7, "six catalogue rows plus the soldier row");
    CheckEqI32(pE->nLineCount, 7, "every panel gets the same total");
    CheckEqI32(pA->nStyle, 14, "the buy screen draws fourteen rows, not the sell screen's thirteen");
    CheckEqI32(pA->nViewW, 125, "the name column is 125 wide here too");
    CheckEqI32(pE->nPosX, 174, "the price column moves to 174");
    CheckEqI32(pE->nViewW, 50, "and is 50 wide");
    CheckEqI32(pB->nPosX, 180, "the decrement column to 180");
    CheckEqI32(pC->nPosX, 196, "the increment column to 196");
    CheckEqI32(pD->nPosX, 232, "the quantity column to 232");
    Check(pA->pfnClick == CtlStubSync, "the name column re-reads the selection");
    Check(pB->pfnClick == CtlStubPrices, "the decrement column is the sell handler");
    Check(pC->pfnClick == CtlStubCapacity, "the increment column is the buy handler");
    Check(pD->pfnClick == NULL, "the quantity column has no handler");

    nOk = 0;
    for (i = 0; i < 7; ++i) {
        if (pA->anLineAux[i] == aWantAux[i]) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 7, "the sort pass puts the rows in the g_aBuySellRowOrder order");
    nOk = 0;
    for (i = 0; i < 7; ++i) {
        const char *psz = UiPanel_GetLineText(pA, i);
        if (psz != NULL && strcmp(psz, apWantName[i]) == 0) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 7, "and the names match, the soldier row labelled from TEXTS[159]");
    nOk = 0;
    for (i = 0; i < 7; ++i) {
        const char *pszB = UiPanel_GetLineText(pB, i);
        const char *pszC = UiPanel_GetLineText(pC, i);
        if (pszB != NULL && strcmp(pszB, "<") == 0 && pszC != NULL && strcmp(pszC, ">") == 0
            && pE->anLineAux[i] == aWantPrice[i] && pD->anLineAux[i] == 0) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 7, "every row gets its < and > columns and the price it costs, 40000 for a soldier");

    nOk = 0;
    for (i = 0; i < 7; ++i) {
        if (pA->anLineAux[i] == 21 || pA->anLineAux[i] == 59 || pA->anLineAux[i] == 114
            || pA->anLineAux[i] == 119 || pA->anLineAux[i] == 122) {
            ++nOk;
        }
    }
    /* BuySellScreen_BuildControls @ 0x76160: 3112 bytes = 737 instructions (18 two-word MOVs) + 23 pool words,
     * 23 calls -- the head of it releases what a built screen held, then the row scan builds the columns. */
    CheckEqI32(nOk, 0, OptCite("no row for a slot outside the range, an unpriced slot, or a slot with a non-zero "
                               "+0xBA", "@ 0x76160"));

    CheckEqI32(nFunds, 1000, OptCite("the build branch does not touch the funds", "@ 0x76160"));

    /* Teardown. The last element of each cleared span is prefilled too: the record's three clear loops
     * are 256 u16 from site+2 (`ADD R3,R0,#2` at 0x76C18 sets the start, 256 x `STRH [R3],#2` at 0x76C20
     * with the SUBS/BPL counter at 0x76C24/0x76C28), 100 u16 from site+0x208 (0x76C50..0x76C64) and 10 u16
     * from site+0x2D0 (0x76C68..0x76C7C), and only the last slot says how far a loop actually went. */
    nFunds = 100;
    aSites[1].auItemQty[10] = 5;
    aSites[1].auItemQty[255] = 7;
    aSites[1].anSoldierIds[99] = 11;
    aSites[1].anCraftIds[9] = 13;
    aSites[1].uRecoveredSoldiers = 3;
    aSites[1].bUnk518 = 77;
    BuySellScreen_BuildControls(&env);
    CheckEqI32(bBuilt, 0, "the second call takes the teardown branch");
    Check(pA == NULL && pB == NULL && pC == NULL && pD == NULL && pE == NULL,
          "the five panel slots are cleared");
    CheckEqI32(aSites[1].bFlag0, 0, "the working site is reset whole @ 0x76160");
    CheckEqI32(aSites[1].bFlag1, 0, "including the type byte the build branch wrote");
    CheckEqI32(aSites[1].auItemQty[10], 0, "the item quantities");
    CheckEqI32(aSites[1].uRecoveredSoldiers, 0, "and the recovery count");
    CheckEqI32(aSites[1].bRecoveredCraftCount, 0, "the recovery craft count");
    CheckEqI32(aSites[1].bRecoveredSoldierAssign, 0, "the recovery assignment byte");
    CheckEqI32(aSites[1].anSoldierIds[0], 0, "the soldier id list");
    CheckEqI32(aSites[1].anCraftIds[0], 0, "and the craft id list");
    CheckEqI32(aSites[1].auItemQty[255], 0, OptCite("the quantity clear spans all 256 slots, site+2..0x201",
                                                    "@ 0x76160"));
    CheckEqI32(aSites[1].anSoldierIds[99], 0, OptCite("the soldier id clear spans all 100 slots, site+0x208..0x2CE",
                                                      "@ 0x76160"));
    CheckEqI32(aSites[1].anCraftIds[9], 0, OptCite("the craft id clear spans all 10 slots, site+0x2D0..0x2DE",
                                                   "@ 0x76160"));
    CheckEqI32(aSites[1].bUnk518, 77, OptCite("but not the byte at site+0x206: no store in the teardown reaches it",
                                              "@ 0x76160"));
    CheckEqI32(nFunds, 4147000, "and the funds roll back to the snapshot");

    /* Slot 113 with no price: the +1 row is still counted, but the soldier row is written inside the
     * slot-113 branch, so it never appears. */
    aObj[113].uPrice = 0;
    BuySellScreen_BuildControls(&env);
    Check(pA != NULL, "the third call builds again");
    if (pA != NULL) {
        CheckEqI32(pA->nLineCount, 6, "the row total still counts the soldier row");
        nOk = 0;
        for (i = 0; i < 6; ++i) {
            const char *psz = UiPanel_GetLineText(pA, i);
            if (psz != NULL && strcmp(psz, "Soldier") == 0) {
                ++nOk;
            }
        }
        CheckEqI32(nOk, 0, "but no soldier row is written when slot 113 gets no row of its own");
        CheckEqI32(pA->anLineAux[5], 0, "and the counted row stays as the constructor left it");
    }
    BuySellScreen_BuildControls(&env);
    CheckEqI32(bBuilt, 0, "and the teardown closes it again");

    BuySellScreen_BuildControls(NULL);
    env.pbControlsBuilt = NULL;
    BuySellScreen_BuildControls(&env);
    CheckEqI32(bBuilt, 0, "a missing flag pointer makes it do nothing");
}

static i32 g_nTransferBuild, g_nTransferReset, g_nTransferCommit, g_nTransferClose;
static void TransferStubBuild(void) { ++g_nTransferBuild; }
static void TransferStubReset(void) { ++g_nTransferReset; }
static void TransferStubCommit(void) { ++g_nTransferCommit; }
static void TransferStubClose(void) { ++g_nTransferClose; }

static void TestTransferInitAndReset(void)
{
    static void *aRoot[5];
    static UiControl aCtl[4], closeCtl, confirmCtl;
    static SmapSite aSites[2];
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static BaseRec aBase[1];
    static BaseRec *apBase[1];
    static UiPanel pA, pD, pE;
    TransferInitEnv ienv;
    TransferResetEnv renv;
    i8 nBaseCount = 3;
    u8 bTransferOk = 0;
    i8 nScratchD0 = 0;
    i8 nScratchF0 = 0;
    u32 nShortfall = 0;
    i32 nResidue = -999;
    const SmapSite *pSite;
    i32 i;

    printf("the transfer screen's bring-up and reset\n");

    memset(aCtl, 0, sizeof(aCtl));
    confirmCtl.uWidth = 40;

    memset(&ienv, 0, sizeof(ienv));
    ienv.ppRoot = aRoot;
    for (i = 0; i < 4; ++i) {
        ienv.apCtl[i] = &aCtl[i];
    }
    ienv.pCloseCtl = &closeCtl;
    ienv.pConfirmCtl = &confirmCtl;
    ienv.pfnCloseClick = TransferStubClose;
    ienv.pfnPanelPrev = BuySellTestPanelPrev;
    ienv.pfnPanelNext = BuySellTestPanelNext;
    ienv.pfnResetSite = TransferStubReset;
    ienv.pfnCommit = TransferStubCommit;
    ienv.pfnBuildPanels = TransferStubBuild;
    ienv.pbBaseCount = &nBaseCount;
    ienv.pbTransferOk = &bTransferOk;
    g_nTransferBuild = 0;
    g_nTransferReset = 0;
    g_nTransferCommit = 0;

    Ui_InitTransfer(&ienv);
    Check(aRoot[0] != NULL, "the screen's pad object is allocated @ 0x9955c");
    Check(((PadImage *)aRoot[0])->nOriginX == 0, "with origin 0 on x");
    Check(((PadImage *)aRoot[0])->nOriginY == 0, "and on y");
    CheckEqI32(g_nTransferBuild, 1, "the panels are built before the controls exist @ 0x9955c");
    CheckEqI32(bTransferOk, 1, "and the transfer is marked possible");
    for (i = 0; i < 4; ++i) {
        Check(aRoot[i + 1] == (void *)&aCtl[i], "the four controls land in the root array's slots");
    }
    Check(aCtl[0].pfnClick == BuySellTestPanelPrev, "the first control scrolls the panels back");
    Check(aCtl[1].pfnClick == BuySellTestPanelNext, "the second forward");
    Check(aCtl[2].pfnClick == TransferStubReset, "the third is the site reset");
    Check(aCtl[3].pfnClick == TransferStubCommit, "the fourth is the commit");
    CheckEqI32(aCtl[0].x, 0xCD, "the scroll pair sits at the buy/sell pair's x");
    CheckEqI32(aCtl[1].y, 0x26, "the down descriptor at y 0x26");
    CheckEqI32(aCtl[2].x, 1, "the reset button at 1, 0x12C");
    CheckEqI32(aCtl[2].y, 0x12C, "both drawn from the emptyb pad pair");
    CheckEqI32(aCtl[3].x, 0x78, "the confirm button at 0x78, 0x12C");
    CheckEqI32(closeCtl.x, 224, "the close control lands at 224, 1");
    CheckEqI32(closeCtl.y, 1, "as 16-bit stores");
    Check(closeCtl.pfnClick == TransferStubClose, "with the close thunk");
    Check(confirmCtl.pfnClick == TransferStubClose,
          "and the confirm control gets the same close thunk, not a commit handler");
    g_nTransferClose = 0;
    if (closeCtl.pfnClick != NULL) {
        closeCtl.pfnClick();
    }
    if (confirmCtl.pfnClick != NULL) {
        confirmCtl.pfnClick();
    }
    CheckEqI32(g_nTransferClose, 2,
               "pressing both runs the close thunk twice, so neither carries some other handler");
    CheckEqI32(confirmCtl.x, 100, "the confirm control centres on its 40-pixel width");
    CheckEqI32(confirmCtl.y, 94, "and sits at 28 * 3 bases + 10");

    free(aRoot[0]);
    aRoot[0] = NULL;
    Ui_InitTransfer(NULL);
    ienv.ppRoot = NULL;
    Ui_InitTransfer(&ienv);
    ienv.ppRoot = aRoot;
    CheckEqI32(g_nTransferBuild, 1, "a missing root array makes it do nothing");

    /* The reset. */
    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    aObj[1].bCargoSize = 5;
    aBase[0].bUnk4 = 7;
    apBase[0] = &aBase[0];

    (void)UiPanel_ctor(&pA, 3);
    (void)UiPanel_ctor(&pD, 3);
    (void)UiPanel_ctor(&pE, 3);
    pE.anLineAux[0] = 2;
    pE.anLineAux[1] = 0;
    pE.anLineAux[2] = 5;
    pD.anLineAux[0] = 10;
    pD.anLineAux[1] = 7;
    pD.anLineAux[2] = 0;

    aSites[1].bFlag0 = 8;
    aSites[1].bFlag1 = 3;
    aSites[1].bUnk518 = 7;
    aSites[1].uRecoveredSoldiers = 3;
    aSites[1].bRecoveredCraftCount = 2;
    aSites[1].bRecoveredSoldierAssign = 1;
    aSites[1].bHasLoot = 1;
    aSites[1].auItemQty[0] = 9;
    aSites[1].auItemQty[149] = 8;
    aSites[1].auItemQty[255] = 8;

    memset(&renv, 0, sizeof(renv));
    renv.aSites = aSites;
    renv.nSiteCount = 1;
    renv.apBases = apBase;
    renv.apObjects = apObj;
    renv.nObjectCount = 149;
    renv.nPendingBase = 0;
    renv.pbTransferOk = &bTransferOk;
    renv.pbScratch1870D0 = &nScratchD0;
    renv.pbScratch1870F0 = &nScratchF0;
    renv.pdwShortfall = &nShortfall;
    renv.pnResidue = &nResidue;
    renv.pPanelA = &pA;
    renv.pPanelD = &pD;
    renv.pPanelE = &pE;

    bTransferOk = 0;
    /* Transfer_ResetSite @ 0x9690c: 420 bytes = 86 instructions (6 two-word MOVs) + 13 pool words, 3 calls. */
    pSite = Transfer_ResetSite(&renv);
    Check(pSite == &aSites[1], OptCite("the reset works on the working site aSmapSites[nSiteCount]", "@ 0x9690c"));
    CheckEqI32(bTransferOk, 1,
               OptCite("the possible-transfer flag is set first and stays set when the gate passes", "@ 0x9690c"));
    CheckEqI32(nShortfall, 0, "a passing gate leaves the shortfall flag alone");
    CheckEqI32(nResidue, -999, "and the reason code alone");
    CheckEqI32(pD.anLineAux[0], 12, "the pending quantity folds into the count column");
    CheckEqI32(pD.anLineAux[1], 7, "a zero pending leaves the count as it was");
    CheckEqI32(pD.anLineAux[2], 5, "and a zero count still takes the pending");
    CheckEqI32(pE.anLineAux[0], 0, "the pending column is cleared");
    CheckEqI32(pE.anLineAux[2], 0, "every row of it");
    CheckEqI32(aSites[1].bUnk518, 0, "the site's pending-craft byte");
    CheckEqI32(aSites[1].uRecoveredSoldiers, 0, "the recovery count");
    CheckEqI32(aSites[1].bRecoveredCraftCount, 0, "the recovery craft count");
    CheckEqI32(aSites[1].bRecoveredSoldierAssign, 0, "the recovery assignment byte");
    CheckEqI32(aSites[1].bHasLoot, 0,
               "and the loot flag, which BuySellScreen_BuildControls' teardown never touches");
    CheckEqI32(aSites[1].auItemQty[0], 0, "the item quantities are cleared from index 0");
    CheckEqI32(aSites[1].auItemQty[148], 0, "up to Map_nObjectCount entries");
    CheckEqI32(aSites[1].auItemQty[149], 8, "and no further: index 149 keeps its value");
    CheckEqI32(aSites[1].bFlag0, 8, "the two site flags are left alone here");
    CheckEqI32(aSites[1].bFlag1, 3, "unlike the buy screen's teardown, which clears both");

    /* The gate: cargo already on the working site. */
    aSites[1].auItemQty[1] = 1; /* bCargoSize 5, so the cargo measure is 5 */
    bTransferOk = 1;
    nShortfall = 0;
    nResidue = -999;
    pD.anLineAux[0] = 10;
    pE.anLineAux[0] = 2;
    Transfer_ResetSite(&renv);
    CheckEqI32(bTransferOk, 0, "cargo already on the working site blocks the transfer");
    CheckEqI32(nShortfall, 1, "the shortfall flag is written as a dword");
    CheckEqI32(nResidue, PUFO_BUYSELL_SHORTFALL_TRANSFER, "with the fifth reason code");
    CheckEqI32(pD.anLineAux[0], 12, "the fold runs whether or not the gate blocked");
    CheckEqI32(aSites[1].auItemQty[1], 0, "and the site clear runs too");

    nScratchD0 = -1;
    bTransferOk = 1;
    Transfer_ResetSite(&renv);
    CheckEqI32(bTransferOk, 0, "a negative scratch byte blocks it too: the test is LDRSB");
    nScratchD0 = 0;
    nScratchF0 = 1;
    bTransferOk = 1;
    Transfer_ResetSite(&renv);
    CheckEqI32(bTransferOk, 0, "and so does the second scratch byte");
    nScratchF0 = 0;

    aSites[1].auItemQty[114] = 1; /* the single score column Smap_SumSiteScores reads */
    bTransferOk = 1;
    Transfer_ResetSite(&renv);
    CheckEqI32(bTransferOk, 0, "a site that still owes recovery blocks it as well");
    CheckEqI32(aSites[1].auItemQty[114], 0, "and the clear still runs afterwards");

    Transfer_ResetSite(NULL);
    renv.aSites = NULL;
    Check(Transfer_ResetSite(&renv) == NULL, "a missing site array stops the reset");
    renv.aSites = aSites;
    renv.nSiteCount = PUFO_SMAP_SITE_COUNT;
    Check(Transfer_ResetSite(&renv) == NULL, "and so does a count at the array end");
    renv.nSiteCount = 1;

    UiPanel_FreeLines(&pA);
    UiPanel_FreeLines(&pD);
    UiPanel_FreeLines(&pE);
}

static i32 g_nTransferCache, g_nTransferMove, g_nTransferValidate;
static void TransferStubCache(void) { ++g_nTransferCache; }
static void TransferStubMove(void) { ++g_nTransferMove; }
static void TransferStubValidate(void) { ++g_nTransferValidate; }

static void TestTransferBuildAndMove(void)
{
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static Soldier aSol[3];
    static Soldier *apSol[3];
    static Ship aShip[3];
    static Ship *apShip[3];
    static SmapSite aSites[2];
    static BaseRec aBase[1];
    static BaseRec *apBase[1];
    static PadImage *pPad;
    static UiControl aCtl[4];
    static u8 aText[(415 + 5) * PUFO_TEXT_REC_STRIDE];
    static const i32 aWantAux[8] = { 117, 1000, 1000, 112, 1, 105, 77, 0 };
    static const char *const apWantName[8] = {
    /* The swap that moves the craft row to the front carries the first soldier along with it, so
     * the two soldier rows come out in the opposite order to the fill pass. */
        "Skyranger-1", "Alan", "Chloe", "Engineer", "Pistol", "Elerium-115", "Sectoid soldier", ""
    };
    static const i32 aWantQty[8] = { 1, 1, 1, 5, 3, 2, 4, 0 };
    UiPanel *pA = NULL, *pB = NULL, *pC = NULL, *pD = NULL, *pE = NULL;
    TransferBuildEnv benv;
    TransferMoveEnv menv;
    u8 bBuilt = 0;
    u8 bTransferOk = 9;
    i32 nRowCursor = 0;
    i32 nTimer = 25;
    i32 nOk = 0;
    i32 i;

    printf("the transfer screen's row list\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    PutStr(aObj[1].szName, "Pistol");
    PutStr(aObj[77].szName, "Sectoid soldier");
    PutStr(aObj[105].szName, "Elerium-115");
    PutStr(aObj[112].szName, "Engineer");
    PutStr(aObj[113].szName, "Scientist");
    PutStr(aObj[142].szName, "Alien grenades");
    aObj[1].auPerBaseVal[0] = 3;
    aObj[77].auPerBaseVal[0] = 4;
    aObj[105].auPerBaseVal[0] = 2;
    aObj[112].auPerBaseVal[0] = 5; /* the engineer row, via Base_GetEngineerCount */
    aObj[113].auPerBaseVal[0] = 0; /* no scientists, so slot 113 gets no row */
    aObj[142].auPerBaseVal[0] = 6; /* the sell mode counts 142; this screen does not */

    aSol[0].nKind = 0; aSol[0].bUnk183 = 0xFF; aSol[0].nUnk440 = 101;
    PutStr(aSol[0].szName, "Chloe");
    aSol[1].nKind = 0; aSol[1].bUnk183 = 0xFF; aSol[1].nUnk440 = 102;
    PutStr(aSol[1].szName, "Alan");
    aSol[2].nKind = 1; aSol[2].bUnk183 = 0xFF; aSol[2].nUnk440 = 103;
    PutStr(aSol[2].szName, "OtherBase");
    for (i = 0; i < 3; ++i) {
        apSol[i] = &aSol[i];
    }

    aShip[0].bType = 1; aShip[0].bId = 7; aShip[0].bCraftType = 0;
    aShip[0].bHomeBase = 0; aShip[0].bAtBase = 1; /* counted and written */
    aShip[1].bType = 2; aShip[1].bId = 8; aShip[1].bCraftType = 1;
    aShip[1].bHomeBase = 0; aShip[1].bAtBase = 0; /* counted, never written */
    aShip[2].bType = 1; aShip[2].bId = 9; aShip[2].bCraftType = 2;
    aShip[2].bHomeBase = 1; aShip[2].bAtBase = 1; /* homed elsewhere */
    for (i = 0; i < 3; ++i) {
        apShip[i] = &aShip[i];
    }

    PutTextRec(aText, 415, "Skyranger");
    PutTextRec(aText, 416, "Lightning");
    PutTextRec(aText, 417, "Avenger");
    PutTextRec(aText, 418, "Interceptor");
    PutTextRec(aText, 419, "Fire Storm");

    aBase[0].bUnk4 = 7;
    apBase[0] = &aBase[0];
    aSites[1].bFlag0 = 8;
    aSites[1].bFlag1 = 3;
    aSites[1].bUnk518 = 7;
    aSites[1].uRecoveredSoldiers = 3;
    aSites[1].bRecoveredCraftCount = 2;
    aSites[1].bRecoveredSoldierAssign = 1;
    aSites[1].bHasLoot = 1;
    aSites[1].auItemQty[1] = 10;
    aSites[1].auItemQty[149] = 8;

    memset(&benv, 0, sizeof(benv));
    memset(aCtl, 0, sizeof(aCtl));
    pPad = NULL;
    benv.pbPanelsBuilt = &bBuilt;
    benv.ppPad = &pPad;
    benv.pbTransferOk = &bTransferOk;
    for (i = 0; i < 4; ++i) {
        benv.apCtl[i] = &aCtl[i];
    }
    benv.aSites = aSites;
    benv.nSiteCount = 1;
    benv.apSoldiers = apSol;
    benv.nSoldierCount = 3;
    benv.apCrafts = apShip;
    benv.nCraftCount = 3;
    benv.apObjects = apObj;
    benv.nObjectCount = 149;
    benv.nSelectedBaseIdx = 0;
    benv.pTextRecs = (const char *)aText;
    benv.ppPanelA = &pA;
    benv.ppPanelB = &pB;
    benv.ppPanelC = &pC;
    benv.ppPanelD = &pD;
    benv.ppPanelE = &pE;
    benv.pfnCacheSelIndex = TransferStubCache;
    benv.pfnMoveOneItem = TransferStubMove;
    benv.pfnValidateCapacity = TransferStubValidate;

    TransferScreen_BuildPanels(&benv);

    /* TransferScreen_BuildPanels @ 0x96b30: 3956 bytes = 931 instructions (37 two-word MOVs) + 21 pool words,
     * 25 calls -- the five columns, and the site bytes the build branch clears. */
    CheckEqI32(bBuilt, 1, OptCite("the build branch sets the transfer built flag", "@ 0x96b30"));
    CheckEqI32(aSites[1].bUnk518, 0, OptCite("the working site is cleared on the build branch too", "@ 0x96b30"));
    CheckEqI32(aSites[1].bHasLoot, 0, OptCite("including the loot flag", "@ 0x96b30"));
    CheckEqI32(aSites[1].bFlag0, 8, OptCite("but not the two site flags", "@ 0x96b30"));
    Check(pA != NULL && pB != NULL && pC != NULL && pD != NULL && pE != NULL,
          OptCite("all five panels are built", "@ 0x96b30"));
    if (pA == NULL || pB == NULL || pC == NULL || pD == NULL || pE == NULL) {
        return;
    }

    /* 2 soldiers + 2 craft counted + slots 1, 77, 105, 112. Slot 113 has no scientists and slot 142
     * is outside this screen's filter. */
    CheckEqI32(pA->nLineCount, 8, "the row total counts the away craft the fill pass never writes");
    CheckEqI32(pE->nLineCount, 8, "every panel gets the same total");
    CheckEqI32(pA->nStyle, 13, "the transfer screen draws thirteen rows, like the sell mode");
    CheckEqI32(pB->nPosX, 172, "the decrement column sits at 172, eight right of the sell mode");
    CheckEqI32(pC->nPosX, 188, "the increment column at 188");
    CheckEqI32(pD->nPosX, 166, "the count column at 166");
    CheckEqI32(pA->nViewW, 125, "the name column is the same 125 wide");
    CheckEqI32(pE->nViewW, 45, "and the quantity column the same 45");
    Check(pA->pfnClick == TransferStubCache, "the name column caches the selected row");
    Check(pB->pfnClick == TransferStubMove, "the decrement column is Transfer_MoveOneItem");
    Check(pC->pfnClick == TransferStubValidate, "the increment column is the capacity gate");
    Check(pD->pfnClick == NULL, "the count column has no handler");

    nOk = 0;
    for (i = 0; i < 8; ++i) {
        if (pA->anLineAux[i] == aWantAux[i]) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 8, "the sort pass puts the rows in the g_aBuySellRowOrder order");
    nOk = 0;
    for (i = 0; i < 8; ++i) {
        const char *psz = UiPanel_GetLineText(pA, i);
        if (psz != NULL && strcmp(psz, apWantName[i]) == 0) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 8, "and the names match, the last row the empty one the count left behind");
    nOk = 0;
    for (i = 0; i < 8; ++i) {
        if (pA->anLineAux[i] == 2077) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 0, "no row carries the sell mode's +2000 alien offset: 77 sorts as 77 here");
    nOk = 0;
    for (i = 0; i < 8; ++i) {
        if (pA->anLineAux[i] == 142 || pA->anLineAux[i] == 113) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 0, "slot 142 is outside this filter and slot 113 has no scientists");
    nOk = 0;
    for (i = 0; i < 8; ++i) {
        if (pD->anLineAux[i] == aWantQty[i] && pE->anLineAux[i] == 0) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 8, "the count column holds the per-base quantity or the crew count");
    nOk = 0;
    for (i = 0; i < 8; ++i) {
        if (pE->anLineAux[i] != 0) {
            ++nOk;
        }
    }
    CheckEqI32(nOk, 0, "and the pending column is zero everywhere: no price column here");
    CheckEqI32(pC->anLineAux[0], 7, "a craft row's increment column holds the craft id");
    CheckEqI32(pC->anLineAux[1], 102, "a soldier row's holds the roster id");
    CheckEqI32(pB->anLineAux[1], 1, "and the decrement column holds the apSoldiers index");

    /* Transfer_MoveOneItem, the decrement column. */
    memset(&menv, 0, sizeof(menv));
    menv.pPanelA = pA;
    menv.pPanelB = pB;
    menv.pPanelD = pD;
    menv.pPanelE = pE;
    menv.aSites = aSites;
    menv.nSiteCount = 1;
    menv.pnRowCursor = &nRowCursor;
    menv.pnRadarPulseTimer = &nTimer;

    pB->nUnk3032 = 4; nRowCursor = 4; /* the Pistol row, aux 1 */
    pE->anLineAux[4] = 3;
    pD->anLineAux[4] = 3;
    aSites[1].auItemQty[1] = 10;
    nTimer = 25; /* 25 / 10 = 2, so three passes */
    Transfer_MoveOneItem(&menv);
    CheckEqI32(pE->anLineAux[4], 0, "one click moves timer/10 + 1 pending units");
    CheckEqI32(pD->anLineAux[4], 6, "raising the count column by the same three");
    CheckEqI32(aSites[1].auItemQty[1], 7, "and dropping the site's quantity by three");

    pE->anLineAux[4] = 3;
    pD->anLineAux[4] = 3;
    aSites[1].auItemQty[1] = 10;
    nTimer = 0;
    Transfer_MoveOneItem(&menv);
    CheckEqI32(pE->anLineAux[4], 2, "a timer of 0 still moves one unit");
    CheckEqI32(aSites[1].auItemQty[1], 9, "and one unit only");

    pE->anLineAux[4] = 0;
    pD->anLineAux[4] = 3;
    aSites[1].auItemQty[1] = 9;
    nTimer = 250;
    Transfer_MoveOneItem(&menv);
    CheckEqI32(pD->anLineAux[4], 3, "with nothing pending the loop turns and changes nothing");
    CheckEqI32(aSites[1].auItemQty[1], 9, "including the site quantity");

    pB->nUnk3032 = 0; nRowCursor = 0; /* the Skyranger row, aux 117 */
    pE->anLineAux[0] = 4;
    pD->anLineAux[0] = 1;
    aSites[1].bRecoveredCraftCount = 3;
    nTimer = 250;
    Transfer_MoveOneItem(&menv);
    CheckEqI32(pE->anLineAux[0], 0, "a craft row moves at most one unit and clears the pending");
    CheckEqI32(pD->anLineAux[0], 2, "raising the count by one");
    CheckEqI32(aSites[1].bRecoveredCraftCount, 2, "and dropping the site's pending craft by one");

    pB->nUnk3032 = 1; nRowCursor = 1; /* the soldier row, aux 1000 */
    pE->anLineAux[1] = 5;
    pD->anLineAux[1] = 1;
    aSites[1].bRecoveredSoldierAssign = 2;
    nTimer = 250;
    /* Transfer_MoveOneItem @ 0x9602c: 604 bytes = 140 instructions (2 two-word MOVs) + 9 pool words. The timer
     * quotient it computes gates the repeat loop, and the soldier row is outside that loop. */
    Transfer_MoveOneItem(&menv);
    CheckEqI32(pE->anLineAux[1], 0,
               OptCite("the soldier row is handled once, outside the repeat loop", "@ 0x9602c"));
    CheckEqI32(pD->anLineAux[1], 2, OptCite("one soldier added to the count", "@ 0x9602c"));
    CheckEqI32(aSites[1].bRecoveredSoldierAssign, 1,
               OptCite("and the site's pending soldier byte down by one", "@ 0x9602c"));

    nTimer = -15; /* -15 / 10 = -1, and 0x9608C branches out on a negative quotient */
    pB->nUnk3032 = 4; nRowCursor = 4;
    pE->anLineAux[4] = 3;
    pD->anLineAux[4] = 3;
    Transfer_MoveOneItem(&menv);
    CheckEqI32(pE->anLineAux[4], 3, "a negative timer quotient makes the handler do nothing");
    CheckEqI32(pD->anLineAux[4], 3, "on either column");

    pB->nUnk3032 = 99; nRowCursor = 99999;
    Transfer_MoveOneItem(&menv);
    Transfer_MoveOneItem(NULL);
    menv.pnRowCursor = NULL;
    Transfer_MoveOneItem(&menv);
    CheckEqI32(pE->anLineAux[4], 3, "a row index out of range is skipped, not dereferenced");

    /* Teardown. */
    bTransferOk = 9;
    TransferScreen_BuildPanels(&benv);
    CheckEqI32(bBuilt, 0, "the second call takes the teardown branch");
    CheckEqI32(bTransferOk, 0, "and clears the transfer-possible flag");
    Check(pA == NULL && pB == NULL && pC == NULL && pD == NULL && pE == NULL,
          "the five panel slots are cleared");
    Check(pPad == NULL, "the pad the bring-up allocated is freed here");

    TransferScreen_BuildPanels(NULL);
    benv.pbPanelsBuilt = NULL;
    TransferScreen_BuildPanels(&benv);
    CheckEqI32(bBuilt, 0, "a missing flag pointer makes it do nothing");
}

static void TestTransferCapacityGate(void)
{
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static Soldier aSol[2];
    static Soldier *apSol[2];
    static Ship aShip[1];
    static Ship *apShip[1];
    static SmapSite aSites[2];
    static BaseRec aBase;
    static BaseRec *apBases[8];
    static u8 aGrid[PUFO_GRID_SIZE];
    static UiPanel pA, pC, pD, pE;
    TransferCapacityEnv env;
    i32 nRowCursor = 0;
    i32 nTimer = 25;
    u32 nShortfall = 0;
    i32 nResidue = -999;
    i32 i;

    printf("the transfer screen's capacity gate\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    aObj[10].auPerBaseVal[0] = 5; /* a plain item row */
    aObj[10].bCargoSize = 1;
    aObj[77].auPerBaseVal[0] = 4; /* an alien containment row */
    aObj[104].auPerBaseVal[0] = 10; /* Base_CountAmmoItems sums 77..104, so 14 aliens here */
    aObj[112].auPerBaseVal[0] = 10; /* the engineer row */

    aSol[0].nKind = 0; /* assigned to the base the gate measures */
    aSol[1].nKind = 1;
    apSol[0] = &aSol[0];
    apSol[1] = &aSol[1];

    aShip[0].bHomeBase = 0; /* homed at the base the gate measures */
    apShip[0] = &aShip[0];

    aBase.bUnk4 = 7;
    for (i = 0; i < 8; ++i) {
        apBases[i] = &aBase;
    }
    aSites[0].bFlag1 = 99; /* matches no base, so the site scan contributes nothing */

    /* Base 0 gets one living-quarters cell (50), one stores cell (300), one hangar slot (1) and one
     * containment cell (10). Base 1 gets nothing at all. */
    aGrid[480 + 144 * 0 + 6 * 0 + 0] = 1;
    aGrid[480 + 144 * 0 + 6 * 0 + 1] = 6;
    aGrid[480 + 144 * 0 + 6 * 0 + 2] = 11;
    aGrid[480 + 144 * 0 + 6 * 0 + 3] = 7;

    (void)UiPanel_ctor(&pA, 6);
    (void)UiPanel_ctor(&pC, 6);
    (void)UiPanel_ctor(&pD, 6);
    (void)UiPanel_ctor(&pE, 6);
    pA.anLineAux[0] = PUFO_ROW_SOLDIER;
    pA.anLineAux[1] = PUFO_ROW_ENGINEER;
    pA.anLineAux[2] = 10;
    pA.anLineAux[3] = PUFO_ROW_CRAFT_FIRST;
    pA.anLineAux[4] = 77;
    pA.anLineAux[5] = PUFO_ROW_SCIENTIST;

    memset(&env, 0, sizeof(env));
    env.pPanelA = &pA;
    env.pPanelC = &pC;
    env.pPanelD = &pD;
    env.pPanelE = &pE;
    env.pnRowCursor = &nRowCursor;
    env.pnRadarPulseTimer = &nTimer;
    env.abFacilityGrid = aGrid;
    env.apSoldiers = apSol;
    env.nSoldierCount = 2;
    env.apCraft = apShip;
    env.nCraftCount = 1;
    env.apObjects = apObj;
    env.nObjectCount = 149;
    env.aSites = aSites;
    env.nSiteCount = 1; /* the working site is the record AT the count, aSites[1] */
    env.apBases = apBases;
    env.nBaseCount = 1;
    env.nSelectedBaseIdx = 0;
    env.nTransferBaseIdx = 0;
    env.pdwShortfall = &nShortfall;
    env.pnResidue = &nResidue;

    /* The soldier row: the living-quarters gate, then the move. */
    pC.nUnk3032 = 0;
    nRowCursor = 0;
    pD.anLineAux[0] = 3;
    pE.anLineAux[0] = 0;
    aObj[112].auPerBaseVal[0] = 50; /* 51 occupied against a cap of 50 */
    nShortfall = 0;
    nResidue = -999;
    /* TransferScreen_ValidateCapacity @ 0x96288: 1372 bytes = 325 instructions (4 two-word MOVs) + 14 pool words,
     * 15 calls -- the same three-reason gate the buy/sell side runs, over the increment column's pending rows. */
    CheckEqI32(TransferScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_LIVING,
               OptCite("a full living-quarters gate reports reason 0", "@ 0x96288"));
    CheckEqI32(nShortfall, 1, OptCite("and writes the shortfall flag unconditionally", "@ 0x96288"));
    CheckEqI32(nResidue, 0, OptCite("with the living-quarters reason code", "@ 0x96288"));
    CheckEqI32(pD.anLineAux[0], 3, "nothing moves when the gate fails");
    CheckEqI32(aSites[1].bRecoveredSoldierAssign, 0, "including the site's pending soldier byte");

    aObj[112].auPerBaseVal[0] = 10; /* 11 occupied, the cap has room */
    nTimer = 250; /* 25 passes, but 0x96358 never lets these three rows into the loop */
    pD.anLineAux[0] = 3;
    pE.anLineAux[0] = 0;
    nShortfall = 0;
    CheckEqI32(TransferScreen_ValidateCapacity(&env), 25,
               "a soldier row runs once and returns the timer quotient @ 0x96288");
    CheckEqI32(pE.anLineAux[0], 1, "panel E is assigned 1, not incremented");
    CheckEqI32(pD.anLineAux[0], 2, "one soldier moved to the count column");
    CheckEqI32(aSites[1].bRecoveredSoldierAssign, 1, "and the site's pending soldier byte up by one");

    /* The engineer row (slot 112). */
    pC.nUnk3032 = 1;
    nRowCursor = 1;
    pD.anLineAux[1] = 3;
    pE.anLineAux[1] = 0;
    aSites[1].auItemQty[112] = 2; /* 10 available at the selected base */
    CheckEqI32(TransferScreen_ValidateCapacity(&env), 25, "a crew row also runs once, not 26 times @ 0x96288");
    CheckEqI32(aSites[1].auItemQty[112], 3, "one engineer queued onto the site");
    CheckEqI32(pD.anLineAux[1], 2, "the count column down by one");
    CheckEqI32(pE.anLineAux[1], 1, "the pending column up by one");

    aSites[1].auItemQty[112] = 10; /* the base has no spare engineer left */
    pD.anLineAux[1] = 3;
    pE.anLineAux[1] = 0;
    TransferScreen_ValidateCapacity(&env);
    CheckEqI32(aSites[1].auItemQty[112], 10, "a crew row with nothing left at the base moves nothing");
    CheckEqI32(pD.anLineAux[1], 3, "and neither column changes");

    /* A craft row against a full hangar: one ship homed here, cap 1. */
    pC.nUnk3032 = 3;
    nRowCursor = 3;
    pD.anLineAux[3] = 3;
    pE.anLineAux[3] = 0;
    aSites[1].bRecoveredCraftCount = 0;
    aSites[1].bUnk518 = 0;
    nShortfall = 0;
    nResidue = -999;
    CheckEqI32(TransferScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_HANGAR,
               "a full hangar reports reason 2");
    CheckEqI32(nShortfall, 1, "the flag written because it was clear");
    CheckEqI32(nResidue, 2, "with the hangar reason code");
    CheckEqI32(pE.anLineAux[3], 0, "and the craft stays put");
    CheckEqI32(aSites[1].bRecoveredCraftCount, 0, "including the site's pending craft byte");

    aShip[0].bHomeBase = 1; /* homed elsewhere, so the hangar has room */
    nShortfall = 0;
    CheckEqI32(TransferScreen_ValidateCapacity(&env), 25,
               "a craft row moves one unit however long the timer is @ 0x96288");
    CheckEqI32(pE.anLineAux[3], 1, "panel E assigned 1");
    CheckEqI32(pD.anLineAux[3], 2, "the count column up by one");
    CheckEqI32(aSites[1].bRecoveredCraftCount, 1, "and the site's pending craft byte up by one");
    CheckEqI32(nShortfall, 0, "with no shortfall recorded");

    /* An alien row (slot 77) against a containment cap of 10 holding 14 aliens. */
    pC.nUnk3032 = 4;
    nRowCursor = 4;
    pD.anLineAux[4] = 3;
    pE.anLineAux[4] = 0;
    aSites[1].auItemQty[77] = 0;
    nShortfall = 0;
    nResidue = -999;
    CheckEqI32(TransferScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_CONTAINMENT,
               "an alien row with no containment left reports reason 4");
    CheckEqI32(nShortfall, 1, "the flag written unconditionally");
    CheckEqI32(nResidue, 4, "the containment reason code");
    CheckEqI32(aSites[1].auItemQty[77], 0, "and nothing is queued");

    /* A plain item row (slot 10) against a stores cap of 300 holding 500. */
    pC.nUnk3032 = 2;
    nRowCursor = 2;
    pD.anLineAux[2] = 3;
    pE.anLineAux[2] = 0;
    aSites[1].auItemQty[10] = 0;
    aObj[10].bCargoSize = 100; /* Base_CalcStorageUsed: 100 * 5 = 500 */
    nShortfall = 0;
    nResidue = -999;
    CheckEqI32(TransferScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_STORES,
               "an overfull stores gate reports reason 3");
    CheckEqI32(nShortfall, 1, "the flag written because it was clear");
    CheckEqI32(nResidue, 3, "the stores reason code");
    aObj[10].bCargoSize = 1;

    /* The same row with room: the repeat loop runs timer/10 + 1 times. */
    nTimer = 25; /* 3 passes */
    pD.anLineAux[2] = 5;
    pE.anLineAux[2] = 0;
    aSites[1].auItemQty[10] = 0;
    nShortfall = 0;
    CheckEqI32(TransferScreen_ValidateCapacity(&env), 2, "an item row runs timer/10 + 1 passes @ 0x96288");
    CheckEqI32(aSites[1].auItemQty[10], 3, "three units queued onto the site @ 0x96288");
    CheckEqI32(pD.anLineAux[2], 2, "the count column down by three @ 0x96288");
    CheckEqI32(pE.anLineAux[2], 3, "the pending column up by three @ 0x96288");
    CheckEqI32(nShortfall, 0, "and no shortfall recorded");

    aObj[10].auPerBaseVal[0] = 0; /* the base does not actually have the item */
    pD.anLineAux[2] = 5;
    pE.anLineAux[2] = 0;
    aSites[1].auItemQty[10] = 3;
    TransferScreen_ValidateCapacity(&env);
    CheckEqI32(aSites[1].auItemQty[10], 3, "an item the base does not have moves nothing");
    CheckEqI32(pD.anLineAux[2], 5, "the loop turns and changes nothing");
    aObj[10].auPerBaseVal[0] = 5;

    /* The two base indices are different: capacity measured at base 1, availability read at base 0. */
    env.nTransferBaseIdx = 1; /* base 1 has no facilities at all */
    pC.nUnk3032 = 0;
    nRowCursor = 0;
    pD.anLineAux[0] = 3;
    pE.anLineAux[0] = 0;
    nShortfall = 0;
    nResidue = -999;
    CheckEqI32(TransferScreen_ValidateCapacity(&env), PUFO_BUYSELL_SHORTFALL_LIVING,
               "the capacity gates measure the psi screen's base, not the selected one");
    env.nTransferBaseIdx = 0;

    nTimer = -15; /* -15 / 10 = -1, and 0x962F0 branches out on a negative quotient */
    pC.nUnk3032 = 2;
    nRowCursor = 2;
    pD.anLineAux[2] = 5;
    pE.anLineAux[2] = 0;
    aSites[1].auItemQty[10] = 3;
    CheckEqI32(TransferScreen_ValidateCapacity(&env), -1,
               "a negative timer quotient makes the gate do nothing @ 0x96288");
    CheckEqI32(aSites[1].auItemQty[10], 3, "including the site quantity");
    nTimer = 25;

    pC.nUnk3032 = 99999;
    TransferScreen_ValidateCapacity(&env);
    TransferScreen_ValidateCapacity(NULL);
    env.pPanelC = NULL;
    TransferScreen_ValidateCapacity(&env);
    env.pPanelC = &pC;
    CheckEqI32(aSites[1].auItemQty[10], 3, "a cached row index out of range is skipped, not dereferenced");

    UiPanel_FreeLines(&pA);
    UiPanel_FreeLines(&pC);
    UiPanel_FreeLines(&pD);
    UiPanel_FreeLines(&pE);
}

/* Transfer_WriteErrorDump @ 0x996e4 writes "<dir>t_error<rand>.txt" into the working directory, with
 * the number from Game_RandU16(0xE100). The tests point it at a fixed prefix so the files are
 * findable and removable without guessing the number. */
#define PUFO_TEST_DUMP_DIR "test_dump_"

static void DumpPattern(char *pszPat, size_t cchPat)
{
    snprintf(pszPat, cchPat, "%st_error*.txt", PUFO_TEST_DUMP_DIR);
}

/* Removes every dump file the tests left behind, under any name: the sweep that mutates the dump's own
 * name label has to be cleaned up too. Returns how many went. */
static i32 DumpClean(void)
{
    char szPat[256];
    WIN32_FIND_DATAA fd;
    i32 nRemoved = 0;

    snprintf(szPat, sizeof(szPat), "%s*", PUFO_TEST_DUMP_DIR);
    for (;;) {
        HANDLE hFind = FindFirstFileA(szPat, &fd);

        if (hFind == INVALID_HANDLE_VALUE) {
            break;
        }
        (void)FindClose(hFind);
        if (remove(fd.cFileName) != 0) {
            break; /* a file that will not go would loop forever */
        }
        ++nRemoved;
    }
    return nRemoved;
}

/* Copies the bytes of the one dump file that is there into pszOut, NUL-terminates it and copies the
 * name into pszName. Returns the byte count, or -1 when no dump file exists. */
static i32 DumpReadOne(char *pszOut, i32 cchOut, char *pszName, size_t cchName)
{
    char szPat[256];
    WIN32_FIND_DATAA fd;
    HANDLE hFind, hFile;
    DWORD cbRead = 0;
    i32 cb = -1;

    DumpPattern(szPat, sizeof(szPat));
    hFind = FindFirstFileA(szPat, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return -1;
    }
    (void)FindClose(hFind);
    snprintf(pszName, cchName, "%s", fd.cFileName);
    hFile = CreateFileA(fd.cFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }
    if (ReadFile(hFile, pszOut, (DWORD)(cchOut - 1), &cbRead, NULL)) {
        pszOut[cbRead] = '\0';
        cb = (i32)cbRead;
    }
    (void)CloseHandle(hFile);
    return cb;
}

static void TestTransferCommit(void)
{
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static Soldier aSol[2];
    static Soldier *apSol[2];
    static Ship aShip[2];
    static Ship *apShip[2];
    static SmapSite aSites[3];
    static BaseRec aBase;
    static BaseRec *apBases[8];
    static u8 aGrid[PUFO_GRID_SIZE];
    static UiPanel pA, pC, pE;
    TransferCommitEnv env;
    SmapSite *pNew;
    i32 nSiteCount = 1;
    i32 i;
    u8 bBaseCount = 1;

    printf("the transfer screen's commit\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    aObj[10].auPerBaseVal[0] = 5;
    aObj[10].bCargoSize = 2;
    aObj[20].auPerBaseVal[0] = 60001; /* the overflow report threshold is 60000 */

    aSol[0].nKind = 0;
    aSol[0].nUnk440 = 101; /* the roster id panel C carries on the soldier row */
    aSol[1].nKind = 0;
    aSol[1].nUnk440 = 102;
    apSol[0] = &aSol[0];
    apSol[1] = &aSol[1];

    aShip[0].bId = 7;
    aShip[0].bHomeBase = 0;
    aShip[0].bHangarX = 2;
    aShip[0].bHangarY = 1;
    aShip[1].bId = 8;
    aShip[1].bHomeBase = 1;
    aShip[1].bHangarX = 0;
    aShip[1].bHangarY = 0;
    apShip[0] = &aShip[0];
    apShip[1] = &aShip[1];

    aBase.bSlot = 0;
    aBase.bUnk4 = 7;
    aBase.nStoresTotal = 100;
    for (i = 0; i < 8; ++i) {
        apBases[i] = &aBase;
    }

    /* The layer2 cells the two craft occupy: base 0 at (row 1, col 2), base 1 at (row 0, col 0). */
    aGrid[444 + 144 * 0 + 6 * 1 + 2] = 11;
    aGrid[444 + 144 * 1 + 6 * 0 + 0] = 11;

    aSites[1].auItemQty[10] = 3; /* three pistols queued onto the working site */
    aSites[2].bUnk518 = 9;
    aSites[2].uRecoveredSoldiers = 4;
    aSites[2].bRecoveredCraftCount = 3;
    aSites[2].bRecoveredSoldierAssign = 2;
    aSites[2].bHasLoot = 1;
    aSites[2].auItemQty[5] = 7;

    (void)UiPanel_ctor(&pA, 3);
    (void)UiPanel_ctor(&pC, 3);
    (void)UiPanel_ctor(&pE, 3);
    pA.anLineAux[0] = PUFO_ROW_SOLDIER;
    pC.anLineAux[0] = 101;
    pE.anLineAux[0] = 1;
    pA.anLineAux[1] = PUFO_ROW_CRAFT_FIRST;
    pC.anLineAux[1] = 7;
    pE.anLineAux[1] = 1;
    pA.anLineAux[2] = 10;
    pC.anLineAux[2] = 0;
    pE.anLineAux[2] = 1;

    memset(&env, 0, sizeof(env));
    env.pPanelA = &pA;
    env.pPanelC = &pC;
    env.pPanelE = &pE;
    env.aSites = aSites;
    env.pnSiteCount = &nSiteCount;
    env.apBases = apBases;
    env.apSoldiers = apSol;
    env.nSoldierCount = 2;
    env.apCraft = apShip;
    env.nCraftCount = 2;
    env.apObjects = apObj;
    env.nObjectCount = 149;
    env.abFacilityGrid = aGrid;
    env.nSelectedBaseIdx = 0;
    env.nTransferBaseIdx = 0;
    env.pszAppDir = PUFO_TEST_DUMP_DIR;
    env.pbBaseCount = &bBaseCount; /* one base, its type byte is 7 */

    (void)DumpClean();
    pNew = TransferScreen_CommitTransfer(&env);

    Check(pNew == &aSites[2], "the commit returns the fresh working record at the new count");
    CheckEqI32(nSiteCount, 2, "the site count is raised by the commit");
    CheckEqI32(aSites[1].anSoldierIds[0], 101, "the queued soldier's roster id lands in the site list");
    CheckEqI32(aSites[1].anCraftIds[0], 7, "and the queued craft's id in the craft list");
    CheckEqI32(aSol[0].nKind, 255, "the queued soldier leaves the base roster (nKind = 255)");
    CheckEqI32(aSol[1].nKind, 0, "the soldier nobody queued stays put");
    CheckEqI32(aShip[0].bHomeBase, 255, "the queued craft is unhomed");
    CheckEqI32(aShip[1].bHomeBase, 1, "and the craft nobody queued keeps its home");
    CheckEqI32(aGrid[444 + 144 * 0 + 6 * 1 + 2], 0, "the queued craft's hangar cell is cleared");
    CheckEqI32(aGrid[444 + 144 * 1 + 6 * 0 + 0], 11, "the other base's cell is left alone");
    CheckEqI32(aSites[1].bFlag0, 24, "the completed record carries the commit flag 24");
    CheckEqI32(aSites[1].bFlag1, 7, "and the destination base's type byte");
    CheckEqI32(pE.anLineAux[0] + pE.anLineAux[1] + pE.anLineAux[2], 0,
               "the pending column is zeroed over every row");
    CheckEqI32(aObj[10].auPerBaseVal[0], 2, "the three queued pistols leave the selected base");
    CheckEqI32(aBase.nStoresTotal, 97, "and the base's stores total drops by the same three");
    {
        static char aDump[8192];
        char szName[256];
        i32 cb = DumpReadOne(aDump, sizeof(aDump), szName, sizeof(szName));
        i32 nIncoming = 0;
        const char *p;

        Check(cb > 0, "the overfull stock writes a t_error<rand>.txt dump (0x996e4)");
        Check(cb > 0 && strncmp(szName, PUFO_TEST_DUMP_DIR "t_error", 17) == 0,
              "  named <szAppDir>t_error<rand>.txt");
        Check(cb > 0 && strstr(aDump, "problem with obj=20") != NULL,
              "  reporting the object index that tripped the 60000 gate");
        Check(cb > 0 && strstr(aDump, "(basenumber)current base=0") != NULL,
              "  against the selected base");
        Check(cb > 0 && strstr(aDump, "count=2") != NULL,
              "  with the site count the commit just raised");
        for (p = aDump; (p = strstr(p, "(incoming transfer)")) != NULL; p += 19) {
            ++nIncoming;
        }
        CheckEqI32(nIncoming, 1,
                   "  and only the site whose destination byte resolves to this base is marked incoming");
        CheckEqI32(DumpClean(), 1, "  the dump is exactly one file");
    }
    CheckEqI32(aSites[2].bUnk518, 0, "the fresh record is cleared: bUnk518");
    CheckEqI32(aSites[2].uRecoveredSoldiers, 0, "uRecoveredSoldiers");
    CheckEqI32(aSites[2].bRecoveredCraftCount, 0, "bRecoveredCraftCount");
    CheckEqI32(aSites[2].bRecoveredSoldierAssign, 0, "bRecoveredSoldierAssign");
    CheckEqI32(aSites[2].bHasLoot, 0, "bHasLoot");
    CheckEqI32(aSites[2].auItemQty[5], 0, "and the quantity columns up to the object count");

    /* Nothing pending: the gate skips the whole commit. */
    nSiteCount = 1;
    memset(aSites[1].auItemQty, 0, sizeof(aSites[1].auItemQty));
    aSites[1].bFlag0 = 8;
    pE.anLineAux[0] = 0;
    pE.anLineAux[1] = 0;
    pE.anLineAux[2] = 0;
    Check(TransferScreen_CommitTransfer(&env) == NULL,
          "with nothing pending the commit does nothing at all");
    CheckEqI32(nSiteCount, 1, "the site count is unchanged");
    CheckEqI32(aSites[1].bFlag0, 8, "and the working record keeps its flag");

    /* A row whose pending count is not exactly 1 is not queued. */
    aSites[1].auItemQty[10] = 3; /* the gate still fires through the item quantity */
    pE.anLineAux[0] = 2;
    pE.anLineAux[1] = 2;
    TransferScreen_CommitTransfer(&env);
    CheckEqI32(aSites[1].anSoldierIds[1], 0, "a soldier row with pending 2 is not queued");
    CheckEqI32(aSites[1].anCraftIds[1], 0, "and neither is a craft row with pending 2");

    TransferScreen_CommitTransfer(NULL);
    env.pnSiteCount = NULL;
    Check(TransferScreen_CommitTransfer(&env) == NULL, "a missing site-count pointer makes it do nothing");
    env.pnSiteCount = &nSiteCount;
    env.aSites = NULL;
    Check(TransferScreen_CommitTransfer(&env) == NULL, "and so does a missing site array");

    (void)DumpClean(); /* the commits above that reached the gate each wrote their own dump */

    UiPanel_FreeLines(&pA);
    UiPanel_FreeLines(&pC);
    UiPanel_FreeLines(&pE);
}

/* Transfer_WriteErrorDump @ 0x996e4. The dump is a text file, so the check is the whole file: the
 * expected bytes are written out here from the ARM's own labels and the fixture's own values, and the
 * file the port produced is compared against them byte for byte. */
static SmapSite     g_aTdSites[3];
static BaseRec      g_aTdBase[3];
static BaseRec     *g_apTdBases[3];
static u8           g_bTdBaseCount;
static i32          g_nTdSiteCount;
static TransferDumpEnv g_tdEnv;
static char         g_aTdWant[8192];
static char         g_aTdFile[8192];
static char         g_szTdName[256];
static char        *g_pTdWant;

static size_t TdLeft(void)
{
    return (size_t)(g_aTdWant + sizeof(g_aTdWant) - g_pTdWant);
}

static void TdWant(const char *psz)
{
    size_t n = strlen(psz);

    memcpy(g_pTdWant, psz, n);
    g_pTdWant += n;
    *g_pTdWant = '\0'; /* the buffer is reused case by case, so it has to end where the case ends */
}

static void TdWantEol(void)
{
    *g_pTdWant++ = '\r'; /* the 2-byte value 2573 at 0x99784 is 0x0D 0x0A on the wire */
    *g_pTdWant++ = '\n';
}

static void TdWantNum(const char *pszLabel, i32 nValue)
{
    g_pTdWant += snprintf(g_pTdWant, TdLeft(), "%s%d\r\n", pszLabel, nValue);
}

static bool DumpExists(const char *pszPat)
{
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pszPat, &fd);

    if (hFind == INVALID_HANDLE_VALUE) {
        return false;
    }
    (void)FindClose(hFind);
    return true;
}

static void TdFixture(void)
{
    i32 i;

    (void)DumpClean();
    memset(g_aTdSites, 0, sizeof(g_aTdSites));
    memset(g_aTdBase, 0, sizeof(g_aTdBase));
    for (i = 0; i < 3; ++i) {
        g_apTdBases[i] = &g_aTdBase[i];
    }
    g_aTdBase[0].bUnk4 = 7;
    g_aTdBase[1].bUnk4 = 9;
    g_aTdBase[2].bUnk4 = 11;
    g_bTdBaseCount = 3;
    g_nTdSiteCount = 2;

    /* Values chosen so every signed load shows up: six of the nine site numbers are read LDRSB and
     * print negative for 0x80 and above, while uRecoveredSoldiers (LDRH) and bHasLoot (LDRB) do not. */
    g_aTdSites[0].bFlag0 = 0xFF;                  /* -1 */
    g_aTdSites[0].bFlag1 = 7;                     /* base 0, not the dump's own index 1 */
    g_aTdSites[0].uRecoveredSoldiers = 65535;     /* 65535 */
    g_aTdSites[0].bRecoveredSoldierAssign = 0x80; /* -128 */
    g_aTdSites[0].bRecoveredCraftCount = 0x82;    /* -126 */
    g_aTdSites[0].bUnk518 = 0x81;                 /* -127 */
    g_aTdSites[0].bHasLoot = 0x80;                /* 128, LDRB */
    g_aTdSites[0].auItemQty[0] = 1;
    g_aTdSites[0].auItemQty[1] = 65535;

    g_aTdSites[1].bFlag0 = 1;
    g_aTdSites[1].bFlag1 = 9;                     /* base 1, the dump's own index */
    g_aTdSites[1].auItemQty[2] = 4;
}

static void TdRun(i32 nObjCount, i8 bBaseIdx, i32 nObjSlot, const char *pszDir)
{
    g_tdEnv.pszAppDir = pszDir;
    g_tdEnv.pnSiteCount = &g_nTdSiteCount;
    g_tdEnv.aSites = g_aTdSites;
    g_tdEnv.nObjectCount = nObjCount;
    g_tdEnv.apBases = g_apTdBases;
    g_tdEnv.nBaseCount = (i32)(i8)g_bTdBaseCount;
    g_tdEnv.bBaseIdx = bBaseIdx;
    g_tdEnv.nObjSlot = nObjSlot;
    (void)Transfer_WriteErrorDump(&g_tdEnv);
}

/* The header every report starts with. */
static void TdWantHeader(i32 nSiteCount, i32 nBaseIdx, i32 nObjSlot)
{
    g_pTdWant = g_aTdWant;
    TdWant("TRANSFERS\r\n");
    TdWantNum("count=", nSiteCount);
    TdWantNum("(basenumber)current base=", nBaseIdx);
    TdWantNum("problem with obj=", nObjSlot);
}

static void TestTransferErrorDump(void)
{
    i32 nRet, cb;

    printf("the transfer error dump\n");
    TdFixture();

    TdWantHeader(2, 1, 7);
    TdWantEol();
    TdWantNum("transfer N ", 0);
    TdWant("(baseidx)destination base=7\r\n");
    TdWantNum("timeremain=", -1);
    TdWantNum("add_new_ship= ", -127);
    TdWantNum("add_new_soldiers=", 65535);
    TdWantNum("ship_transfer=", -126);
    TdWantNum("soldier_transfer=", -128);
    TdWantNum("(bool)frommiss=", 128);
    TdWant("obj0=1\r\nobj1=65535\r\nobj2=0\r\n");
    TdWantEol();
    TdWantNum("transfer N ", 1);
    TdWant("(baseidx)destination base=9(incoming transfer)\r\n");
    TdWantNum("timeremain=", 1);
    TdWantNum("add_new_ship= ", 0);
    TdWantNum("add_new_soldiers=", 0);
    TdWantNum("ship_transfer=", 0);
    TdWantNum("soldier_transfer=", 0);
    TdWantNum("(bool)frommiss=", 0);
    TdWant("obj0=0\r\nobj1=0\r\nobj2=4\r\n");

    srand(20261016u);
    TdRun(3, 1, 7, PUFO_TEST_DUMP_DIR);
    cb = DumpReadOne(g_aTdFile, sizeof(g_aTdFile), g_szTdName, sizeof(g_szTdName));
    {
        const char *pNum = g_szTdName + strlen(PUFO_TEST_DUMP_DIR "t_error");
        i32 nGot = atoi(pNum);

        srand(20261016u);
        CheckEqI32(nGot, (i32)Game_RandU16(0xE100u),
                   "the number in the name is the dump's own Game_RandU16(0xE100) draw (0x9972C)");
    }
    CheckEqI32(cb, (i32)strlen(g_aTdWant), "the report is exactly the expected byte count (0x996e4)");
    Check(cb > 0 && memcmp(g_aTdFile, g_aTdWant, (size_t)cb) == 0,
          "and byte for byte the report the ARM writes: labels, order, the blank line between blocks");
    Check(cb > 0 && strncmp(g_szTdName, PUFO_TEST_DUMP_DIR "t_error", 17) == 0,
          "named <szAppDir>t_error<rand>, the number from Game_RandU16(0xE100) (0x9972C)");
    Check(cb > 0 && strlen(g_szTdName) > 4 && strcmp(g_szTdName + strlen(g_szTdName) - 4, ".txt") == 0,
          "  and closed with .txt (0x99760)");
    CheckEqI32(DumpClean(), 1, "one file, in the directory the env names");

    /* The base lookup is bounded by nBaseCount, so a site whose destination sits past the visible
     * bases is not incoming even though a later base would match. */
    g_bTdBaseCount = 1;
    TdWantHeader(2, 1, 7);
    TdWantEol();
    TdWantNum("transfer N ", 0);
    TdWant("(baseidx)destination base=7\r\n");
    TdWantNum("timeremain=", -1);
    TdWantNum("add_new_ship= ", -127);
    TdWantNum("add_new_soldiers=", 65535);
    TdWantNum("ship_transfer=", -126);
    TdWantNum("soldier_transfer=", -128);
    TdWantNum("(bool)frommiss=", 128);
    TdWant("obj0=1\r\nobj1=65535\r\nobj2=0\r\n");
    TdWantEol();
    TdWantNum("transfer N ", 1);
    TdWant("(baseidx)destination base=9\r\n");
    TdWantNum("timeremain=", 1);
    TdWantNum("add_new_ship= ", 0);
    TdWantNum("add_new_soldiers=", 0);
    TdWantNum("ship_transfer=", 0);
    TdWantNum("soldier_transfer=", 0);
    TdWantNum("(bool)frommiss=", 0);
    TdWant("obj0=0\r\nobj1=0\r\nobj2=4\r\n");
    TdRun(3, 1, 7, PUFO_TEST_DUMP_DIR);
    cb = DumpReadOne(g_aTdFile, sizeof(g_aTdFile), g_szTdName, sizeof(g_szTdName));
    Check(cb > 0 && cb == (i32)strlen(g_aTdWant) && memcmp(g_aTdFile, g_aTdWant, (size_t)cb) == 0,
          "one visible base drops the (incoming transfer) tail from the second site (0x99AD8)");
    (void)DumpClean();
    g_bTdBaseCount = 3;

    /* nSmapSiteCount not positive: the header block only, no site blocks at all. */
    g_nTdSiteCount = 0;
    TdWantHeader(0, 1, 7);
    TdRun(3, 1, 7, PUFO_TEST_DUMP_DIR);
    cb = DumpReadOne(g_aTdFile, sizeof(g_aTdFile), g_szTdName, sizeof(g_szTdName));
    Check(cb > 0 && cb == (i32)strlen(g_aTdWant) && memcmp(g_aTdFile, g_aTdWant, (size_t)cb) == 0,
          "a site count of 0 writes the header block and nothing else (0x9996C)");
    (void)DumpClean();

    g_nTdSiteCount = -1;
    TdWantHeader(-1, 1, 7);
    TdRun(3, 1, 7, PUFO_TEST_DUMP_DIR);
    cb = DumpReadOne(g_aTdFile, sizeof(g_aTdFile), g_szTdName, sizeof(g_szTdName));
    Check(cb > 0 && cb == (i32)strlen(g_aTdWant) && memcmp(g_aTdFile, g_aTdWant, (size_t)cb) == 0,
          "  and a negative one prints as-is in count= while still writing no site block");
    (void)DumpClean();
    g_nTdSiteCount = 2;

    /* Map_nObjectCount 0: the obj lines are gone, everything else stays. */
    TdWantHeader(2, 1, 7);
    TdWantEol();
    TdWantNum("transfer N ", 0);
    TdWant("(baseidx)destination base=7\r\n");
    TdWantNum("timeremain=", -1);
    TdWantNum("add_new_ship= ", -127);
    TdWantNum("add_new_soldiers=", 65535);
    TdWantNum("ship_transfer=", -126);
    TdWantNum("soldier_transfer=", -128);
    TdWantNum("(bool)frommiss=", 128);
    TdWantEol();
    TdWantNum("transfer N ", 1);
    TdWant("(baseidx)destination base=9(incoming transfer)\r\n");
    TdWantNum("timeremain=", 1);
    TdWantNum("add_new_ship= ", 0);
    TdWantNum("add_new_soldiers=", 0);
    TdWantNum("ship_transfer=", 0);
    TdWantNum("soldier_transfer=", 0);
    TdWantNum("(bool)frommiss=", 0);
    TdRun(0, 1, 7, PUFO_TEST_DUMP_DIR);
    cb = DumpReadOne(g_aTdFile, sizeof(g_aTdFile), g_szTdName, sizeof(g_szTdName));
    Check(cb > 0 && cb == (i32)strlen(g_aTdWant) && memcmp(g_aTdFile, g_aTdWant, (size_t)cb) == 0,
          "an object count of 0 drops every objN line and nothing else (0x99E44)");
    (void)DumpClean();

    /* The base index is a signed char: 0x80 prints -128 in the header line (0x99890). */
    TdWantHeader(2, -128, 7);
    TdWantEol();
    TdWantNum("transfer N ", 0);
    TdWant("(baseidx)destination base=7\r\n");
    TdWantNum("timeremain=", -1);
    TdWantNum("add_new_ship= ", -127);
    TdWantNum("add_new_soldiers=", 65535);
    TdWantNum("ship_transfer=", -126);
    TdWantNum("soldier_transfer=", -128);
    TdWantNum("(bool)frommiss=", 128);
    TdWant("obj0=1\r\nobj1=65535\r\nobj2=0\r\n");
    TdWantEol();
    TdWantNum("transfer N ", 1);
    TdWant("(baseidx)destination base=9\r\n");
    TdWantNum("timeremain=", 1);
    TdWantNum("add_new_ship= ", 0);
    TdWantNum("add_new_soldiers=", 0);
    TdWantNum("ship_transfer=", 0);
    TdWantNum("soldier_transfer=", 0);
    TdWantNum("(bool)frommiss=", 0);
    TdWant("obj0=0\r\nobj1=0\r\nobj2=4\r\n");
    TdRun(3, (i8)0x80, 7, PUFO_TEST_DUMP_DIR);
    cb = DumpReadOne(g_aTdFile, sizeof(g_aTdFile), g_szTdName, sizeof(g_szTdName));
    Check(cb > 0 && cb == (i32)strlen(g_aTdWant) && memcmp(g_aTdFile, g_aTdWant, (size_t)cb) == 0,
          "a base index of 0x80 prints -128, and no site matches it so no incoming tail either");
    (void)DumpClean();

    /* bFlag1 is read LDRSB as well (0x99A9C): a destination byte of 0x81 prints -127. */
    g_aTdSites[0].bFlag1 = 0x81;
    g_nTdSiteCount = 1;
    TdWantHeader(1, 1, 7);
    TdWantEol();
    TdWantNum("transfer N ", 0);
    TdWant("(baseidx)destination base=-127\r\n");
    TdWantNum("timeremain=", -1);
    TdWantNum("add_new_ship= ", -127);
    TdWantNum("add_new_soldiers=", 65535);
    TdWantNum("ship_transfer=", -126);
    TdWantNum("soldier_transfer=", -128);
    TdWantNum("(bool)frommiss=", 128);
    TdWant("obj0=1\r\nobj1=65535\r\nobj2=0\r\n");
    TdRun(3, 1, 7, PUFO_TEST_DUMP_DIR);
    cb = DumpReadOne(g_aTdFile, sizeof(g_aTdFile), g_szTdName, sizeof(g_szTdName));
    Check(cb > 0 && cb == (i32)strlen(g_aTdWant) && memcmp(g_aTdFile, g_aTdWant, (size_t)cb) == 0,
          "the destination byte is read LDRSB too: 0x81 prints -127 (0x99A9C)");
    (void)DumpClean();
    g_aTdSites[0].bFlag1 = 7;
    g_nTdSiteCount = 2;

    /* A failed open: the original hands the NULL FILE * to every fwrite and to fclose. The port
     * refuses. The path is made unopenable by putting a plain FILE where a directory should be. */
    {
        static const char szBlocker[] = "test_dump_blocker";
        char szDir[64];
        HANDLE hFile;

        (void)remove(szBlocker);
        hFile = CreateFileA(szBlocker, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
        Check(hFile != INVALID_HANDLE_VALUE, "the test's blocker file was created");
        (void)CloseHandle(hFile);

        snprintf(szDir, sizeof(szDir), "%s/", szBlocker);
        g_tdEnv.pszAppDir = szDir;
        g_tdEnv.pnSiteCount = &g_nTdSiteCount;
        g_tdEnv.aSites = g_aTdSites;
        g_tdEnv.nObjectCount = 3;
        g_tdEnv.apBases = g_apTdBases;
        g_tdEnv.nBaseCount = 3;
        g_tdEnv.bBaseIdx = 1;
        g_tdEnv.nObjSlot = 7;
        nRet = Transfer_WriteErrorDump(&g_tdEnv);
        CheckEqI32(nRet, -1, "an unopenable path returns -1 instead of writing through a NULL FILE *");
        Check(!DumpExists("test_dump_blocker/*"), "  and nothing was created under it");
        (void)remove(szBlocker);
    }

    CheckEqI32(Transfer_WriteErrorDump(NULL), -1, "a missing env writes nothing");
    CheckEqI32(DumpClean(), 0, "  and no dump file appeared anywhere");
}

/* TransferScreen_Draw @ 0x97fa4. The draw writes into the loaded surface, so the checks read pixels
 * back out of a 240x320 16bpp surface, exactly as the original's Blit_PackedColor builds them. */

static SoftSurf g_xfSurf;
static UiPanel g_xfA, g_xfB, g_xfC, g_xfD, g_xfE;
static TransferDrawEnv g_xfEnv;
static GfxColorEnv g_xfColor;
/* The gamma cell the colour envs of this file that state no gamma point at. The original reads one
 * global here (0x2d860); a fixture states its own. */
static i32 g_nGammaZero;
static UiAnimBlitEnv g_xfAnim;
static ObjectRecFull g_xfObj[149];
static ObjectRecFull *g_apXfObj[149];
static SmapSite g_aXfSites[2];
static BaseRec g_aXfBase[8];
static BaseRec *g_apXfBases[8];
static BaseSiteRec g_aXfSiteRecs[8];
static BaseStripHitEnv g_xfStrip;
static UiControl g_aXfCtl[4];
static u8 g_aXfGrid[PUFO_GRID_SIZE];
static u8 g_aXfText[1721 * PUFO_TEXT_REC_STRIDE];
static u8 g_bXfBuilt, g_bXfOverlay, g_bXfPreview, g_bXfStarted, g_bXfBaseCount, g_bXfSelected, g_bXfTap;
static i8 g_bXfTransferBase, g_nXfProgress;
static i32 g_nXfSiteCount, g_nXfCursorX, g_nXfCursorY, g_nXfResidue, g_nXfRowCursor;
static u8 g_nXfMapPhase; /* TransferDrawEnv.pnMapPhase is a byte view: 0x98A0C reads nMapPhase LDRB */
static u32 g_dwXfShortfall;
static i32 g_nXfStripCalls, g_nXfStripArg;

static void XfIconStripThunk(i32 nBaseIdx, void *pUser)
{
    (void)pUser;
    ++g_nXfStripCalls;
    g_nXfStripArg = nBaseIdx;
}

static u16 XfPix(u32 nIdx, u32 nMid, u32 nLow)
{
    return (u16)(((nIdx >> 3) << 11) + 32u * (nMid >> 2) + (nLow >> 3));
}

static void XfReset(void)
{
    g_bXfBuilt = 1;
    g_bXfOverlay = 0;
    g_bXfPreview = 0;
    g_bXfStarted = 0;
    g_nXfProgress = 0;
    g_bXfBaseCount = 2;
    g_bXfSelected = 2; /* the strip hit-test misses with the transfer gate open and returns this */
    g_bXfTap = 0;
    g_bXfTransferBase = 7;
    g_nXfSiteCount = 1;
    g_nXfCursorX = 220;
    g_nXfCursorY = 100;
    g_nXfMapPhase = 1;
    g_nXfResidue = -999;
    g_dwXfShortfall = 0;
    g_nXfRowCursor = 0;
    g_nXfStripCalls = 0;
    g_nXfStripArg = -1;

    static const char *const apszNames[5] = { "Soldier", "Pistol", "Sectoid soldier",
                                              "Elerium-115", "Skyranger" };
    static const char *const apszCounts[5] = { "1", "3", "4", "2", "1" };
    static const i32 anAux[5] = { 1000, 1, 77, 105, 117 };

    for (i32 r = 0; r < 5; ++r) {
        (void)UiPanel_SetLineText(&g_xfA, r, apszNames[r]);
        (void)UiPanel_SetLineText(&g_xfB, r, "0");
        (void)UiPanel_SetLineText(&g_xfC, r, "0");
        (void)UiPanel_SetLineText(&g_xfD, r, apszCounts[r]);
        (void)UiPanel_SetLineText(&g_xfE, r, "0");
        UiPanel_SetLineAux(&g_xfA, r, anAux[r]);
        UiPanel_SetLineAux(&g_xfB, r, 0);
        UiPanel_SetLineAux(&g_xfC, r, 0);
        UiPanel_SetLineAux(&g_xfD, r, anAux[r]);
        UiPanel_SetLineAux(&g_xfE, r, 0);
    }
    g_xfA.nPosX = 10;  g_xfA.nViewW = 125;
    g_xfB.nPosX = 172; g_xfB.nViewW = 15;
    g_xfC.nPosX = 188; g_xfC.nViewW = 15;
    g_xfD.nPosX = 166; g_xfD.nViewW = 50;
    g_xfE.nPosX = 232; g_xfE.nViewW = 45;
    UiPanel *const apPanels[5] = { &g_xfA, &g_xfB, &g_xfC, &g_xfD, &g_xfE };
    for (i32 r = 0; r < 5; ++r) {
        apPanels[r]->nPosY = 71;
        apPanels[r]->nFont = 0;
    }
    g_xfB.nStyle = 13; /* the band is 16 * 5 = 80, capped by nStyle 13 */

    memset(g_aXfSites, 0, sizeof(g_aXfSites));
    for (i32 i = 0; i < 8; ++i) {
        g_aXfBase[i].bUnk4 = (u8)(10 + i);
        g_apXfBases[i] = &g_aXfBase[i];
        g_aXfSiteRecs[i].abName[0] = (char)('A' + i);
        g_aXfSiteRecs[i].abName[1] = 0;
    }
    memset(g_aXfCtl, 0, sizeof(g_aXfCtl));
    memset(g_aXfGrid, 0, sizeof(g_aXfGrid));

    memset(&g_xfStrip, 0, sizeof(g_xfStrip));
    g_xfStrip.pbBaseCount = &g_bXfBaseCount;
    g_xfStrip.pbSelectedBase = &g_bXfSelected;
    g_xfStrip.pbGateTransfer = &g_bXfBuilt;
    g_xfStrip.pbTapFlag = &g_bXfTap;

    memset(&g_xfEnv, 0, sizeof(g_xfEnv));
    g_xfColor.pnGamma = &g_nGammaZero;
    g_xfColor.nFadeCount = 0;
    g_xfAnim.pbStarted = &g_bXfStarted;
    g_xfAnim.pnProgress = &g_nXfProgress;
    g_xfAnim.nFrameStyle = 0;
    g_xfEnv.pColor = &g_xfColor;
    g_xfEnv.pAnim = &g_xfAnim;
    g_xfEnv.pPanelA = &g_xfA;
    g_xfEnv.pPanelB = &g_xfB;
    g_xfEnv.pPanelC = &g_xfC;
    g_xfEnv.pPanelD = &g_xfD;
    g_xfEnv.pPanelE = &g_xfE;
    g_xfEnv.pbPanelsBuilt = &g_bXfBuilt;
    g_xfEnv.pnRowCursor = &g_nXfRowCursor;
    g_xfEnv.apObjects = g_apXfObj;
    g_xfEnv.nObjectCount = 149;
    g_xfEnv.pTextRecs = (const char *)g_aXfText;
    g_xfEnv.apSiteRecs = g_aXfSiteRecs;
    g_xfEnv.pCtlPrev = &g_aXfCtl[0];
    g_xfEnv.pCtlNext = &g_aXfCtl[1];
    g_xfEnv.pCtlReset = &g_aXfCtl[2];
    g_xfEnv.pCtlConfirm = &g_aXfCtl[3];
    g_xfEnv.pbOverlayOpen = &g_bXfOverlay;
    g_xfEnv.pbPreviewOk = &g_bXfPreview;
    g_xfEnv.pnCursorX = &g_nXfCursorX;
    g_xfEnv.pnCursorY = &g_nXfCursorY;
    g_xfEnv.pdwShortfall = &g_dwXfShortfall;
    g_xfEnv.pnResidue = &g_nXfResidue;
    g_xfEnv.pbTransferBase = &g_bXfTransferBase;
    g_xfEnv.aSites = g_aXfSites;
    g_xfEnv.pnSiteCount = &g_nXfSiteCount;
    g_xfEnv.apBases = g_apXfBases;
    g_xfEnv.pbBaseCount = &g_bXfBaseCount;
    g_xfEnv.abFacilityGrid = g_aXfGrid;
    g_xfEnv.pnMapPhase = &g_nXfMapPhase;
    g_xfEnv.pStripHit = &g_xfStrip;
    g_xfEnv.pfnDrawIconStrip = XfIconStripThunk;
    /* In the original these are one byte: Geoscape_Draw passes nSelectedBaseIdx as the draw's
     * second argument (0x93EA8) and Base_HitTestFacilityStrip reads the same byte back out of
     * abFacilityGrid + 0x619. The harness keeps them equal for the same reason. */
    g_xfEnv.nSelectedBaseIdx = 2;
    g_xfEnv.nDestBaseIdx = 1;
}

static void TestTransferDraw(void)
{
    const u16 *pPix;

    printf("the transfer screen's frame\n");

    for (i32 i = 0; i < 149; ++i) {
        g_apXfObj[i] = &g_xfObj[i];
    }
    PutTextRec(g_aXfText, PUFO_BUYSSELL_SOLDIER_TEXT_REC, "Soldier");
    PutTextRec(g_aXfText, PUFO_TRANSFER_LABEL_DEST_CAP, "Destination Base");
    PutTextRec(g_aXfText, PUFO_TRANSFER_LABEL_XFER_CAP, "Transfer");
    PutTextRec(g_aXfText, PUFO_TRANSFER_LABEL_FROM, "From: ");
    PutTextRec(g_aXfText, PUFO_TRANSFER_LABEL_STOP, "Stop Transfer");
    PutTextRec(g_aXfText, PUFO_TRANSFER_LABEL_SELECT, "Select destination base");
    PutTextRec(g_aXfText, PUFO_TRANSFER_LABEL_TO, "TO: ");
    PutTextRec(g_aXfText, PUFO_TRANSFER_SHORTFALL_REC, "Not enouth living quarters");
    PutTextRec(g_aXfText, PUFO_TRANSFER_SHORTFALL_REC + 1, "Not enouth money");
    PutTextRec(g_aXfText, PUFO_TRANSFER_SHORTFALL_REC + 2, "Not enough free hangar space");
    PutTextRec(g_aXfText, PUFO_TRANSFER_SHORTFALL_REC + 3, "Not enough storage space");
    PutTextRec(g_aXfText, PUFO_TRANSFER_SHORTFALL_ALIEN, "Not enough containment space");
    PutTextRec(g_aXfText, PUFO_TRANSFER_SHORTFALL_BLOCK, "Transfer is not complete");

    (void)UiPanel_ctor(&g_xfA, 5);
    (void)UiPanel_ctor(&g_xfB, 5);
    (void)UiPanel_ctor(&g_xfC, 5);
    (void)UiPanel_ctor(&g_xfD, 5);
    (void)UiPanel_ctor(&g_xfE, 5);

    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&g_xfSurf);
    SoftSurf_AllocPixels(&g_xfSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_xfSurf;
    pPix = (const u16 *)g_xfSurf.pPixels;

    XfReset();
    memset(g_xfSurf.pPixels, 0, (size_t)240 * 320u * 2u);

    /* The rails and the separators run before the built flag, so an unbuilt screen still frames. */
    g_bXfBuilt = 0;
    CheckEqI32(TransferScreen_Draw(&g_xfEnv), 0, "the unbuilt screen returns nothing useful");
    CheckEqI32((i32)pPix[2 * 240 + 5], XfPix(100, 100, 100), "the left rail starts at y 2 with 100");
    CheckEqI32((i32)pPix[163 * 240 + 5], XfPix(215, 215, 215), "  and the 24th climbing block owns 163");
    /* The climbing blocks run 1..168 with no gaps (7 tall on a 7 step) and are drawn after the
     * falling ones, so the falling half only becomes visible where the climbing half has stopped. */
    CheckEqI32((i32)pPix[169 * 240 + 5], XfPix(220, 220, 220), "the falling half shows from 169 with 0xE6 - 10");
    CheckEqI32((i32)pPix[314 * 240 + 5], XfPix(115, 115, 115), "  and the last block owns 314 as 115");
    CheckEqI32((i32)pPix[2 * 240 + 234], XfPix(100, 100, 100), "the right rail mirrors the left");
    CheckEqI32((i32)pPix[5 * 240 + 1], XfPix(100, 100, 100), "the top rail starts at x 1 with 100");
    CheckEqI32((i32)pPix[5 * 240 + 116], XfPix(215, 215, 215), "  and climbs to 1 + 5*23 = 116");
    CheckEqI32((i32)pPix[5 * 240 + 119], XfPix(215, 215, 215), "the climbing half overdraws the falling "
                                                               "one at 119..120, as in the original's order");
    CheckEqI32((i32)pPix[5 * 240 + 234], XfPix(135, 135, 135), "the falling half ends at 234 with 135");
    CheckEqI32((i32)pPix[315 * 240 + 1], XfPix(100, 100, 100), "the bottom rail mirrors the top");
    CheckEqI32((i32)pPix[69 * 240 + 130], XfPix(0xB4, 0xB4, 0xB4), "the first separator at 130,69");
    CheckEqI32((i32)pPix[150 * 240 + 198], XfPix(0xB4, 0xB4, 0xB4), "  and the last runs to 69 + 82 - 1");
    Check(pPix[151 * 240 + 130] == 0, "the band is 16 * 5 + 2 tall and no taller");
    Check(pPix[69 * 240 + 131] == 0, "and one pixel wide");
    Check(pPix[0] == 0, "no panel row is drawn while the built byte is clear");
    CheckEqI32(g_xfB.bRepeatArmed, 0, "and the panels are not flagged either");

    /* Built: the panel flags and the closed-state label path. */
    g_bXfBuilt = 1;
    memset(g_xfSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    CheckEqI32(TransferScreen_Draw(&g_xfEnv), 0, "the built screen with the overlay closed returns 0");
    CheckEqI32(g_xfB.bRepeatArmed, 1, "panel B gets bRepeatArmed (0x98248)");
    CheckEqI32(g_xfC.bRepeatArmed, 1, "and so does panel C (0x98258)");
    Check(pPix[0 * 240 + 208] == 0, "the destination overlay is not drawn (0x986CC)");
    CheckEqI32(g_nXfStripCalls, 0, "the icon-strip hook does not run");
    CheckEqI32(g_bXfTransferBase, 7, "and the destination byte is left alone");

    /* The shortfall panel short-circuits the rest of the draw. */
    memset(g_xfSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    g_dwXfShortfall = 1;
    g_nXfResidue = PUFO_BUYSELL_SHORTFALL_CONTAINMENT;
    CheckEqI32(TransferScreen_Draw(&g_xfEnv), 0, "a shortfall returns after its own panel (0x98B44)");
    CheckEqI32(g_bXfStarted, 1, "the panel animation started (0x985DC)");
    Check(pPix[0 * 240 + 208] == 0, "and the overlay path never ran");

    /* The destination overlay. */
    g_dwXfShortfall = 0;
    g_nXfResidue = -999;
    g_bXfStarted = 0;
    g_bXfOverlay = 1;
    g_bXfTransferBase = 7;
    g_nXfCursorX = 220;
    g_nXfCursorY = 100;
    memset(g_xfSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    CheckEqI32(TransferScreen_Draw(&g_xfEnv), 1, "the overlay path returns the site count (0x98A80)");
    CheckEqI32(g_nXfStripCalls, 1, "the facility strip is drawn (0x98744)");
    CheckEqI32(g_nXfStripArg, 2, "  marked at the selected base, the draw's second argument");
    CheckEqI32((i32)pPix[3 * 240 + 3], XfPix(0x46, 0xA0, 0xB3), "the first base row's outer bevel (0x987D8)");
    CheckEqI32((i32)pPix[4 * 240 + 3], XfPix(0x00, 0x6E, 0x77), "  its middle bevel (0x9880C)");
    CheckEqI32((i32)pPix[5 * 240 + 5], XfPix(0x04, 0x46, 0x4B), "  and its inner bevel (0x9883C)");
    CheckEqI32((i32)pPix[31 * 240 + 3], XfPix(0x46, 0xA0, 0xB3), "the second base row starts at 28 + 3");
    CheckEqI32(g_bXfTransferBase, 2, "the strip hit-test's answer lands in byte_1870F4 (0x98994)");
    CheckEqI32(g_aXfSites[1].bFlag1, 12, "the working site's bFlag1 takes that base's type byte (0x989C4)");
    /* ORIGINAL BUG: the gate that makes the hit test miss is bTransferPanelsBuilt @ 0x1870EC, the
     * same byte this screen sets to draw its panels, so the call at 0x98980 always returns
     * nSelectedBaseIdx -- the very value 0x989D0 compares against. The close branch never fires
     * while the transfer screen is up, and byte_1870F4 is pinned to the source base. */
    CheckEqI32(g_bXfOverlay, 1, "the close branch cannot fire while the panels are built (0x989E0)");
    CheckEqI32(g_bXfTransferBase, 2, "  and the destination byte is the source base (0x98994)");
    CheckEqI32(g_nXfCursorX, 0, "both cursor coordinates are zeroed (0x989E8)");
    CheckEqI32(g_nXfCursorY, 0, "  including the y");
    CheckEqI32(g_nXfStripCalls, 1, "the strip is drawn once per call");

    /* The coupling that makes the branch dead: the draw returns at 0x98224 unless the same byte is
     * set, so the overlay path only ever runs with the strip gate open. */
    g_bXfBuilt = 0;
    g_bXfOverlay = 1;
    g_bXfTransferBase = 7;
    g_nXfCursorX = 220;
    g_nXfCursorY = 30;
    g_nXfStripCalls = 0;
    memset(g_xfSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    CheckEqI32(TransferScreen_Draw(&g_xfEnv), 0, "with the panels unbuilt the draw stops at 0x98224");
    CheckEqI32(g_bXfTransferBase, 7, "  so the overlay never runs and byte_1870F4 is untouched");
    CheckEqI32(g_nXfStripCalls, 0, "  and the strip is not drawn either");

    /* Guards. */
    CheckEqI32(TransferScreen_Draw(NULL), 0, "a null env does nothing");
    g_bXfOverlay = 0;
    g_xfEnv.pPanelB = NULL;
    memset(g_xfSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    CheckEqI32(TransferScreen_Draw(&g_xfEnv), 0, "a missing panel B is skipped, not dereferenced");
    CheckEqI32((i32)pPix[2 * 240 + 5], XfPix(100, 100, 100), "  while the rails still draw");
    CheckEqI32((i32)pPix[69 * 240 + 130], XfPix(0xB4, 0xB4, 0xB4),
               "  and the separators shrink to the 0 + 2 stub the original gets too");
    Check(pPix[71 * 240 + 130] == 0, "  and no further");
    g_xfEnv.pPanelB = &g_xfB;
}

static SoftSurf g_stripSurf;
static u8 g_aStripGrid[PUFO_GRID_SIZE];

static void TestBaseStripDrawer(void)
{
    const u16 *pPix;
    BaseStripDrawEnv env;
    u8 bCount, bSelected;

    printf("Base_DrawFacilityIconStrip @ 0x6250c\n");

    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&g_stripSurf);
    SoftSurf_AllocPixels(&g_stripSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_stripSurf;
    pPix = (const u16 *)g_stripSurf.pPixels;

    /* Sets the three scalars, clears the grid and the surface, and leaves the caller to write the
     * grid bytes it wants before calling the drawer. */
#define STRIP_PREPARE(nCount, nSel, nHilite)                                    \
    bCount = (u8)(nCount); bSelected = (u8)(nSel);                              \
    memset(g_aStripGrid, 0, sizeof(g_aStripGrid));                              \
    memset(g_stripSurf.pPixels, 0, (size_t)240 * 320u * 2u);                    \
    memset(&env, 0, sizeof(env));                                               \
    env.abFacilityGrid = g_aStripGrid;                                          \
    env.pbBaseCount = &bCount; env.pbSelectedBase = &bSelected;                 \
    env.nHighlightRow = (nHilite)

    /* A cell with no tile is grey: the grey pair is drawn after the status pair and wins. */
    STRIP_PREPARE(1, 1, -1);
    Base_DrawFacilityIconStrip(&env);
    Check(pPix[4 * 240 + 211] == 0, "nSelectedBaseIdx == nBaseCount draws no erase rect (0x62544)");
    CheckEqI32((i32)pPix[4 * 240 + 212], XfPix(0x14, 0x14, 0x14), "an empty cell is the 4x4 grey (0x626B8)");
    CheckEqI32((i32)pPix[7 * 240 + 215], XfPix(0x14, 0x14, 0x14), "  filling 4x4 out to (215, 7)");
    CheckEqI32((i32)pPix[5 * 240 + 213], XfPix(0x28, 0x28, 0x28), "with the 2x2 grey inside it (0x62710)");
    CheckEqI32((i32)pPix[6 * 240 + 214], XfPix(0x28, 0x28, 0x28), "  the inner block reaches (214, 6)");
    CheckEqI32((i32)pPix[7 * 240 + 213], XfPix(0x14, 0x14, 0x14), "  and no further: (213, 7) is the 4x4 again");
    Check(pPix[4 * 240 + 236] == 0, "the last column block ends at x 235 (212 + 4 * 5 + 3)");
    Check(pPix[28 * 240 + 4] == 0, "one base draws nothing below y 28");
    Check(pPix[4 * 240 + 210] == 0, "and nothing left of x 212");

    /* A tile with a construction timer is the red marker; a tile without one is the green marker. */
    STRIP_PREPARE(1, 1, -1);
    g_aStripGrid[0] = 5;                                   /* buildDays[0][0] */
    g_aStripGrid[PUFO_GRID_FAC_OFF + 0] = 1;               /* fac[0].tileA[0] */
    g_aStripGrid[PUFO_GRID_DAYS_STRIDE + 1] = 0;           /* buildDays[0][1] */
    g_aStripGrid[PUFO_GRID_FAC_OFF + 1] = 1;               /* fac[0].tileA[1] */
    Base_DrawFacilityIconStrip(&env);
    CheckEqI32((i32)pPix[4 * 240 + 212], XfPix(0x96, 0, 0), "a cell under construction puts 0x96 in red (0x6260C)");
    CheckEqI32((i32)pPix[5 * 240 + 213], XfPix(0xFF, 0, 0), "  and 0xFF in its 2x2");
    CheckEqI32((i32)pPix[4 * 240 + 216], XfPix(0, 0x96, 0), "a finished cell puts 0x96 in green (0x62660)");
    CheckEqI32((i32)pPix[5 * 240 + 217], XfPix(0, 0xFF, 0), "  and 0xFF in its 2x2");

    /* No tile beats a non-zero timer, because the grey pair is drawn last. */
    STRIP_PREPARE(1, 1, -1);
    g_aStripGrid[2] = 7;                                   /* buildDays[0][2] non-zero */
    Base_DrawFacilityIconStrip(&env);
    CheckEqI32((i32)pPix[4 * 240 + 220], XfPix(0x14, 0x14, 0x14),
               "a cell with a timer but no tile is still grey (0x626B8 after 0x6260C)");

    /* The rows step 4 pixels and the bases step 28. */
    STRIP_PREPARE(2, 2, -1);
    Base_DrawFacilityIconStrip(&env);
    CheckEqI32((i32)pPix[24 * 240 + 212], XfPix(0x14, 0x14, 0x14), "the sixth row of a base is at 28 * base + 24");
    CheckEqI32((i32)pPix[32 * 240 + 212], XfPix(0x14, 0x14, 0x14), "the second base's first row is at 28 + 4");
    CheckEqI32((i32)pPix[55 * 240 + 235], XfPix(0x14, 0x14, 0x14), "  and its last cell ends at (235, 55)");

    /* The erase rect over the selected cell, drawn before the cells. */
    STRIP_PREPARE(1, 0, -1);
    Base_DrawFacilityIconStrip(&env);
    CheckEqI32((i32)pPix[3 * 240 + 211], XfPix(0xFF, 0xFF, 0x64), "the erase rect starts at (211, 28 * sel + 3) (0x6257C)");
    CheckEqI32((i32)pPix[28 * 240 + 236], XfPix(0xFF, 0xFF, 0x64), "  and is 26x26, so (236, 28) is its corner");
    CheckEqI32((i32)pPix[4 * 240 + 212], XfPix(0x14, 0x14, 0x14), "  the cells are drawn over it");

    /* The selected byte is read signed: -1 puts the rect at y -25, off screen. */
    STRIP_PREPARE(1, 0xFF, -1);
    Base_DrawFacilityIconStrip(&env);
    Check(pPix[0] == 0, "a selected index of -1 puts the erase rect at y -25 (0x62520 LDRSB)");
    CheckEqI32((i32)pPix[4 * 240 + 212], XfPix(0x14, 0x14, 0x14), "  while the cells still draw");

    /* The chevron: 23 steps down-right from (212, 28 * base + 4) and 23 up-right from
     * (212, 28 * base + 26), meeting at x 234. */
    STRIP_PREPARE(1, 1, 0);
    Base_DrawFacilityIconStrip(&env);
    CheckEqI32((i32)pPix[4 * 240 + 212], XfPix(0xFF, 0, 0), "the marked base gets the chevron over its cells (0x62720)");
    CheckEqI32((i32)pPix[26 * 240 + 234], XfPix(0xFF, 0, 0), "  the two diagonals meet at (234, 26)");
    CheckEqI32((i32)pPix[4 * 240 + 234], XfPix(0xFF, 0, 0), "  and at (234, 4)");
    CheckEqI32((i32)pPix[15 * 240 + 223], XfPix(0xFF, 0, 0), "  the down-right arm passes (223, 15)");
    STRIP_PREPARE(1, 1, -1);
    Base_DrawFacilityIconStrip(&env);
    CheckEqI32((i32)pPix[26 * 240 + 234], XfPix(0x28, 0x28, 0x28),
               "an unmarked base leaves (234, 26) as the cell's own 2x2 grey");
    STRIP_PREPARE(2, 2, 1);
    Base_DrawFacilityIconStrip(&env);
    CheckEqI32((i32)pPix[32 * 240 + 212], XfPix(0xFF, 0, 0), "the mark follows the row index, here base 1 (0x62720)");
    CheckEqI32((i32)pPix[4 * 240 + 212], XfPix(0x14, 0x14, 0x14), "  and only that base");

    /* No bases: the erase rect still runs, the cell loop does not. */
    STRIP_PREPARE(0, 3, 3);
    Base_DrawFacilityIconStrip(&env);
    CheckEqI32((i32)pPix[87 * 240 + 211], XfPix(0xFF, 0xFF, 0x64), "the erase rect is drawn before the count test (0x6257C)");
    Check(pPix[4 * 240 + 212] == 0, "  and with no bases no cell draws (0x62590 BLE)");

    Base_DrawFacilityIconStrip(NULL);
    STRIP_PREPARE(1, 1, -1);
    env.abFacilityGrid = NULL;
    Base_DrawFacilityIconStrip(&env);
    Check(pPix[4 * 240 + 212] == 0, "a null grid does nothing instead of faulting");
#undef STRIP_PREPARE
}

static void TestRoleToItemId(void)
{
    /* BuySell_RoleToItemId @ 0x76d88 and Transfer_RoleToIconId @ 0x96ad8 are 88 bytes = 22 instructions with no
     * pool word at all, and compared word by word they share all 22: `MOV R3,R0,LSL#24` + `MOVS R0,R3,ASR#24`
     * sign-extend the low byte first, so the range test is on a signed value, then five compares with the values
     * reached in reverse order (0 -> 0x75, 1 -> 0x78, 2 -> 0x79, 3 -> 0x76, 4 -> 0x77) and `MOVNE R0,#0xFF` for
     * anything else. The port keeps one body for both. */
    printf("BuySell_RoleToItemId @ 0x76d88, Transfer_RoleToIconId @ 0x96ad8\n");
    CheckEqI32(BuySell_RoleToItemId(0), 117, OptCite("role 0", "@ 0x96ad8"));
    CheckEqI32(BuySell_RoleToItemId(1), 120, OptCite("role 1", "@ 0x96ad8"));
    CheckEqI32(BuySell_RoleToItemId(2), 121, OptCite("role 2", "@ 0x96ad8"));
    CheckEqI32(BuySell_RoleToItemId(3), 118, OptCite("role 3", "@ 0x96ad8"));
    CheckEqI32(BuySell_RoleToItemId(4), 119, OptCite("role 4, the fall-through", "@ 0x96ad8"));
    CheckEqI32(BuySell_RoleToItemId(5), 0xFF, OptCite("anything else is 255 (0x96B00 MOVNE)", "@ 0x96ad8"));
    CheckEqI32(BuySell_RoleToItemId(-1), 0xFF,
               OptCite("and so is a negative role: 0x96AD8 widens the byte as a signed char", "@ 0x96ad8"));
    CheckEqI32(BuySell_RoleToItemId(256), 117, OptCite("only the low byte counts", "@ 0x96ad8"));
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

/* The dispatcher's own sample hook: GeoscapeCloseEnv types it with an i32. */
static void CloseSampleI(i32 nSample, void *pUser)
{
    (void)pUser;
    g_nCloseSamples += nSample;
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
    GeoscapeCloseEnv hk;
    u8 bPopupA, bPopupB;
    GeoscapeWorld world;

    printf("screen close thunks\n");

    /* Screen 13 has no case, so each thunk shows the dispatcher head and nothing else. */
    CloseProbe(&hk);
    memset(&world, 0, sizeof(world));
    world.nTickSeconds = 7; /* matches no arm of the 0x8B980..0x8B9D0 chain */
    bPopupA = 1;
    bPopupB = 1;
    hk.pWorld = &world;
    hk.pbPopupA = &bPopupA;
    hk.pbPopupB = &bPopupB;
    hk.pfnPlaySample = CloseSampleI;

    /* Seven of them are the same 12-byte shape: `LDR R0,=sGeoscapeScreenBlock`, `B Geoscape_CloseUI`, and the block
     * as the only pool word. Measured 2026-10-23 by comparing those records word by word: the twelve members of the
     * shape (0x557bc 0x589c0 0x59b54 0x5abd8 0x5c078 0x64ae4 0x6e3d4 0x72ffc 0x75154 0x838d0 0x96020 0x9a8c0) share
     * the LDR word E59F0000 and the DCD 0x1864F8 and differ ONLY in the branch-displacement word -- shape-identical,
     * not byte-identical -- and 25 function bodies end with this tail call in all (census in the IDB at 0x557bc). The
     * row that tells them apart is the screen whose close control carries it, so each one runs on its own here and
     * gets its own cite. */
    static const char *const apszThunk[7] = { "@ 0x557bc", "@ 0x589c0", "@ 0x59b54", "@ 0x64ae4",
                                              "@ 0x6e3d4", "@ 0x72ffc", "@ 0x75154" };
    static const char *const apszThunkName[7] = { "BaseInfo", "BaseFac A", "BaseFac B", "Soldier",
                                                  "Manuf", "Research", "BuySell" };
    static void (*const apfnThunk[7])(const GeoscapeCloseEnv *) = {
        BaseInfoScreen_CloseThunk, BaseFacScreen_CloseThunkA, BaseFacScreen_CloseThunk,
        SoldierScreen_CloseThunk, ManufScreen_CloseThunk, ResearchScreen_CloseThunk,
        BuySellScreen_CloseThunk };

    for (i32 i = 0; i < 7; ++i) {
        CloseProbe(&hk);
        hk.pWorld = &world;
        hk.pbPopupA = &bPopupA;
        hk.pbPopupB = &bPopupB;
        hk.pfnPlaySample = CloseSampleI;
        bPopupA = 1;
        bPopupB = 1;
        g_nCloseSamples = 0;
        apfnThunk[i](&hk);
        CheckEqI32(g_nCloseSamples, 17, OptCite(apszThunkName[i], apszThunk[i]));
        CheckEqI32(g_nCloseFail, 0, OptCite("  its tail call reaches the dispatcher head (0x8B95C, 0x8B96C)",
                                            apszThunk[i]));
        Check(bPopupA == 0 && bPopupB == 0, OptCite("  and both popup bytes are cleared (0x8B978, 0x8B97C)",
                                                    apszThunk[i]));
        CheckEqI32(g_nCloseScreen, 13, OptCite("  with the default case leaving the screen id alone",
                                               apszThunk[i]));
    }
    CheckEqI32(world.nTickSeconds, 7, "and a tick no arm matches is not rewritten");

    CloseProbe(&hk);
    hk.pWorld = &world;
    hk.pbPopupA = &bPopupA;
    hk.pbPopupB = &bPopupB;
    hk.pfnPlaySample = CloseSampleI;
    bPopupA = 1;
    bPopupB = 1;
    g_nCloseSamples = 0;
    NewBaseScreen_CloseThunk(&hk);
    CheckEqI32(g_nCloseSamples, 2 * 17, OptCite(
        "the odd thunk: 32 bytes playing 17 itself (0x615BC) and the dispatcher plays it again",
        "@ 0x615b0"));

    CloseProbe(&hk);
    g_nCloseSamples = 0;
    BaseInfoScreen_CloseThunk(NULL);
    NewBaseScreen_CloseThunk(NULL);
    CheckEqI32(g_nCloseSamples, 0, "a null environment plays nothing");
    CheckEqI32(g_nCloseFail, 1, "and clears nothing");
}

static void TestTitleReturn(void)
{
    i8 nTab;
    TitleReturnHooks hk;

    printf("OptionsScreen_ReturnToTitle\n");

    nTab = 4;
    hk.pfnCloseUi = CloseUi;
    hk.pfnFreeGeoscapeUi = FreeUi;
    hk.pfnStartupUi = StartupUi;
    hk.pUser = NULL;

    g_nCloseCalls = 0;
    g_nFreeMode = -1;
    g_nStartupCalls = 0;

    CheckEqI32(OptionsScreen_ReturnToTitle(&nTab, &hk), 77, "the startup result is returned");
    CheckEqI32(nTab, 0, "the tab byte is cleared first (STRB at 0x6B478)");
    CheckEqI32(g_nCloseCalls, 1, "the geoscape screen is closed");
    CheckEqI32(g_nFreeMode, 1, "every geoscape widget is freed with mode 1");
    CheckEqI32(g_nStartupCalls, 1, "the startup UI is re-inited");

    CheckEqI32(OptionsScreen_ReturnToTitle(NULL, NULL), 0, "NULL hooks return zero");
}

static void TestMoreCloseThunks(void)
{
    GeoscapeCloseEnv hk;
    MapClickHooks mk;

    printf("FundingScreen_CloseThunk @ 0x5abd8, FundingScreen_ExitThunk @ 0x5c078,\n"
           "Geoscape_CloseUI_ThunkA @ 0x838d0, Geoscape_CloseUI_ThunkB @ 0x96020,\n"
           "Geoscape_CloseUI_ThunkC @ 0x9a8c0, Geoscape_OnMapClick_Thunk @ 0x857e8\n");

    /* Each thunk gets its own probe: CloseProbe arms g_nCloseFail with 1 and Geoscape_CloseUI is what
     * clears it, so a probe per call is the only way to tell which thunk reached the dispatcher -- one
     * probe after five calls is cleared by the first one and says nothing about the other four. */
    CloseProbe(&hk);
    FundingScreen_CloseThunk(&hk);
    CheckEqI32(g_nCloseFail, 0, "FundingScreen_CloseThunk reaches the dispatcher");
    CloseProbe(&hk);
    FundingScreen_ExitThunk(&hk);
    CheckEqI32(g_nCloseFail, 0, "FundingScreen_ExitThunk reaches the dispatcher");
    CloseProbe(&hk);
    Geoscape_CloseUI_ThunkA(&hk);
    CheckEqI32(g_nCloseFail, 0, "Geoscape_CloseUI_ThunkA reaches the dispatcher");
    CloseProbe(&hk);
    Geoscape_CloseUI_ThunkB(&hk);
    CheckEqI32(g_nCloseFail, 0, "Geoscape_CloseUI_ThunkB reaches the dispatcher");
    CloseProbe(&hk);
    Geoscape_CloseUI_ThunkC(&hk);
    CheckEqI32(g_nCloseFail, 0, "Geoscape_CloseUI_ThunkC reaches the dispatcher");
    CloseProbe(&hk);
    FundingScreen_CloseThunk(&hk);
    FundingScreen_ExitThunk(&hk);
    Geoscape_CloseUI_ThunkA(&hk);
    Geoscape_CloseUI_ThunkB(&hk);
    Geoscape_CloseUI_ThunkC(&hk);
    CheckEqI32(g_nCloseFail, 0, "all five reach the same dispatcher (0x8B96C)");

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

    /* ResearchScreen_PanelPrev @ 0x72e30 / PanelNext @ 0x72e68: 56 B = 11 instructions + 3 pool words over
     * pResearchTopicPanel, pResearchPanelB, pResearchPanelC -- the third step is the tail call. */
    ResearchScreen_PanelNext(&a, &b, &c);
    CheckEqI32(a.nUnk3028, 3, OptCite("all three panels page", "@ 0x72e68"));
    CheckEqI32(b.nUnk3028, 2, OptCite("the middle one too", "@ 0x72e68"));
    CheckEqI32(c.nUnk3028, 1, OptCite("and the last one, through the tail call", "@ 0x72e68"));

    a.nUnk3028 = 4; /* 8 - 4: the ceiling the step allows */
    ResearchScreen_PanelNext(&a, &b, &c);
    CheckEqI32(a.nUnk3028, 4, OptCite("next stops at nLineCount - nStyle", "@ 0x72e68"));

    c.nUnk3028 = 0; /* the ceiling step moved it too; put it back on the floor */
    ResearchScreen_PanelPrev(&a, &b, &c);
    CheckEqI32(a.nUnk3028, 3, OptCite("prev moves all three back", "@ 0x72e30"));
    CheckEqI32(c.nUnk3028, 0, OptCite("the last cursor stays at zero, no wrap", "@ 0x72e30"));

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
    Check(pRet == &aPool[5], "the return value is the sixth widget pointer @ 0x6b994");
    for (i32 i = 0; i < 6; ++i) {
        CheckEqI32(aPool[i].nState, 0, "every nState byte is cleared @ 0x6b994");
    }

    for (i32 i = 0; i < 6; ++i) {
        aPool[i].nState = 3;
    }
    pRet = OptionsScreen_ClearSfxButtons(apCtl);
    Check(pRet == &aPool[5], "the sfx group returns its sixth pointer too @ 0x6b9d4");
    CheckEqI32(aPool[0].nState, 0, "the sfx group is cleared as well @ 0x6b9d4");

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

    printf("StrategList_RegisterAtexit @ 0x838a0\n");

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
    CheckEqI32(g_nOptSample, 7, "open plays sample 7 @ 0x6dd04");
    CheckEqI32(g_nOptSampleArg, 4, "with the same second argument @ 0x6dd04");
    CheckEqI32(g_nOptMode, -1, "the mode reaches OptionsScreen_Open @ 0x6dd04");
    CheckEqI32(g_nOptInitCalls, 1, "the open runs once @ 0x6dd04");

    /* 0x6DD08 keeps R1 and never reads R0, so the first argument cannot matter. */
    g_nOptMode = -1;
    OptionsScreen_OpenWithSound(0, 5, &hk);
    CheckEqI32(g_nOptMode, 5, "the first argument is dropped @ 0x6dd04");

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

    /* BaseAtScreen_Open @ 0x588cc: 68 bytes = 15 instructions + 2 pool words. It reads the control out of
     * pGeoscapeCtlConfirm @ 0x186BA4, centres it (`LDRH uWidth` + `LSR #1` + `RSB R1,R1,#0x78` -> x = 120 -
     * (uWidth >> 1), stored as u16), stores y as `0x120 | 2` = 290, installs the click handler at +0x0C (the
     * original installs BaseAtScreen_CloseThunk @ 0x577c0 there; the port installs the hook's click, the shell's
     * stand-in for it), drops its FIRST argument (0x588D0 MOV R0,R1) and tail-calls BaseAtScreen_Update @ 0x57808
     * with the mode. No sample, no screen id. */
    ah.pfnClick = AtClick;
    ah.pfnUpdate = AtUpdate;
    BaseAtScreen_Open(&ctl, 3, &ah);

    g_nAtX = ctl.x;
    g_nAtY = ctl.y;
    CheckEqI32(g_nAtX, 100, OptCite("x centres the control on 120: 120 - (40 >> 1)", "@ 0x588cc"));
    CheckEqI32(g_nAtY, 290, OptCite("y is 290, built as 0x120|2 (0x588E8 MOV / 0x588F0 ORR)", "@ 0x588cc"));
    CheckEqI32(g_nAtMode, 3, OptCite("the mode reaches the update (0x58904 tail call)", "@ 0x588cc"));
    Check(ctl.pfnClick == AtClick, OptCite("the click handler is installed at +0x0C (0x58900)", "@ 0x588cc"));

    ctl.pfnClick();
    CheckEqI32(g_nAtClickCalls, 1, OptCite("and it is the control's own handler", "@ 0x588cc"));

    ctl.uWidth = 41;
    BaseAtScreen_Open(&ctl, 0, &ah);
    CheckEqI32(ctl.x, 100, OptCite("an odd width still drops to the same centre (LSR #1, not /2 rounding)",
                                   "@ 0x588cc"));

    BaseAtScreen_Open(NULL, 1, &ah);
    CheckEqI32(g_nAtMode, 1, OptCite("a NULL control still runs the update", "@ 0x588cc"));
    BaseAtScreen_Open(&ctl, 2, NULL);
    Check(ctl.pfnClick == AtClick, OptCite("NULL hooks change nothing and call nothing", "@ 0x588cc"));

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
static GeoscapeCloseEnv g_cancelClose;

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
    static u8 g_nEntries;
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
    CloseProbe(&g_cancelClose);
    cancel.pClose = &g_cancelClose;
    g_aCols[3] = 7;
    ResearchScreen_CancelSelectedAndClose(&cancel);
    CheckEqI32(g_aCols[3], 0, "cancel zeroes the same entry the buttons use");
    CheckEqI32(g_nCloseFail, 0, "and tail-calls Geoscape_CloseUI (0x733DC -> 0x8B96C)");

    CloseProbe(&g_cancelClose);
    g_aCols[3] = 0;
    ResearchScreen_CancelSelectedAndClose(&cancel);
    CheckEqI32(g_aCols[3], 0, "a zero entry stays zero");
    CheckEqI32(g_nCloseFail, 0, "and the close still runs");

    /* The port's guard skips the store but must not skip the tail call. */
    g_aCols[3] = 9;
    CloseProbe(&g_cancelClose);
    cancel.qty.pTopicPanel = NULL;
    ResearchScreen_CancelSelectedAndClose(&cancel);
    CheckEqI32(g_aCols[3], 9, "a missing panel skips the store");
    CheckEqI32(g_nCloseFail, 0, "but not the close");
    cancel.qty.pTopicPanel = &g_Panel;
    CloseProbe(&g_cancelClose);
    ResearchScreen_CancelSelectedAndClose(NULL);
    CheckEqI32(g_nCloseFail, 1, "a missing environment does nothing at all");

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
    static u8 g_nEntries;
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
    static u16 g_aDone[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT]; /* auManufCommitted */
    static u8 g_aGrid[PUFO_GRID_SIZE];
    static ObjectRecFull g_EngRec;
    static i32 g_nRow;
    static i8 g_nBase;
    static u8 g_nEntries;
    static u8 g_nGate;
    ManufQtyEnv env;

    printf("\nManufScreen_IncEngineers @ 0x6dddc / DecEngineers @ 0x6e09c / "
           "ReduceSelectedQtyToOne @ 0x6dec0 / DecUnits @ 0x6e330 / UpdateProgress @ 0x6df6c\n");

    memset(&g_Panel, 0, sizeof(g_Panel));
    memset(g_aCols, 0, sizeof(g_aCols));
    memset(g_aProdB, 0, sizeof(g_aProdB));
    memset(g_aDone, 0, sizeof(g_aDone));
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
    env.apProductC = g_aDone;
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
    CheckEqI32(ManufScreen_IncEngineers(&env), 17156,
               "the increment returns 17156, the column's offset from the product-block anchor");
    CheckEqI32(g_aCols[7], 4, "product 7's engineers went 3 to 4");
    CheckEqI32(g_aCols[6], 0, "the neighbour is untouched");
    CheckEqI32(g_aCols[200 + 7], 0, "and base 1's copy of the same product is untouched");

    /* The gate byte. It is dead in the shipped build -- nothing writes it non-zero -- but the
     * original tests it, so the port does too. */
    g_nGate = 1;
    CheckEqI32(ManufScreen_IncEngineers(&env), 1, "a set gate returns the byte itself");
    CheckEqI32(g_aCols[7], 4, "and nothing is stored");
    CheckEqI32(ManufScreen_DecEngineers(&env), 1, "the decrement is gated too");
    CheckEqI32(ManufScreen_ReduceSelectedQtyToOne(&env), 1, "so is the clamp");
    CheckEqI32(ManufScreen_DecUnits(&env), 1, "and the units step");
    g_nGate = 0;

    /* The engineer gate: 0 total against 4 allocated is -4, returned verbatim. */
    g_EngRec.auPerBaseVal[0] = 0;
    CheckEqI32(ManufScreen_IncEngineers(&env), -4, "no engineers to spare: the negative is returned");
    CheckEqI32(g_aCols[7], 4, "and the engineers column is not touched");
    g_EngRec.auPerBaseVal[0] = 6;

    /* The workshop gate: 60 already active needs 61 over a capacity of 50. The engineer total has
     * to clear the first gate for this one to be the one that answers. */
    g_EngRec.auPerBaseVal[0] = 100;
    for (i32 i = 0; i < 8; ++i) {
        g_aCols[i] = 8; /* 64 active */
    }
    CheckEqI32(ManufScreen_IncEngineers(&env), 0, "64 active against a capacity of 50 is refused");
    for (i32 i = 0; i < 8; ++i) {
        g_aCols[i] = 0;
    }
    g_EngRec.auPerBaseVal[0] = 6;

    /* DecEngineers @ 0x6e09c: no gate beyond non-zero, and 17156 either way. */
    g_aCols[7] = 9;
    CheckEqI32(ManufScreen_DecEngineers(&env), 17156, "the decrement returns 17156");
    CheckEqI32(g_aCols[7], 8, "9 to 8");
    g_aCols[7] = 0;
    CheckEqI32(ManufScreen_DecEngineers(&env), 17156, "a zero entry still returns 17156");
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

    /* DecUnits @ 0x6e330: the floor is the units already committed -- auManufCommitted @ 0x15D09C,
     * the column CommitProduction raises at 0x6F6C0, read here with LDRH at 0x6E3A0 -- plus one. */
    g_aCols[7] = 3;
    g_aDone[7] = 3;
    g_aProdB[7] = 10;
    CheckEqI32(ManufScreen_DecUnits(&env), PUFO_MANUF_ANCHOR,
               "the units step returns the product-block anchor, register residue");
    CheckEqI32(g_aProdB[7], 9, "10 units with 3 committed drops to 9");
    g_aProdB[7] = 4;
    CheckEqI32(ManufScreen_DecUnits(&env), PUFO_MANUF_ANCHOR, "again");
    CheckEqI32(g_aProdB[7], 4, "4 units is exactly committed + 1, so it stops there");
    g_aProdB[7] = 0;
    (void)ManufScreen_DecUnits(&env);
    CheckEqI32(g_aProdB[7], 0, "and a zero order never goes negative");

    /* The engineers column is NOT the floor: 20 engineers and nothing committed still lets the
     * order fall, and neither column this function does not own moves. */
    g_aCols[7] = 20;
    g_aDone[7] = 0;
    g_aProdB[7] = 2;
    (void)ManufScreen_DecUnits(&env);
    CheckEqI32(g_aProdB[7], 1, "20 engineers, 0 committed: 2 orders drop to 1");
    CheckEqI32((i32)g_aCols[7], 20, "the engineers column is not touched");
    CheckEqI32((i32)g_aDone[7], 0, "the committed column is read here, not written");

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
    (void)ManufScreen_IncEngineers(&env);
    CheckEqI32(g_aCols[199], 2, "row 1 selects product 199");
    CheckEqI32(g_aCols[198], 0, "the entry before it is untouched");

    g_nRow = 0;
    g_nBase = 1;
    g_aGrid[480 + 144 * 1 + 6 * 0 + 0] = 3; /* give base 1 a workshop cell */
    g_aCols[7] = 20;
    g_aCols[200 + 7] = 1;
    (void)ManufScreen_IncEngineers(&env);
    CheckEqI32(g_aCols[200 + 7], 2, "base 1's copy of product 7 is the one that moved");
    CheckEqI32(g_aCols[7], 20, "base 0's copy is untouched");
    g_nBase = 0;

    /* The guards the original does not have. */
    CheckEqI32(ManufScreen_IncEngineers(NULL), 0, "a missing environment is refused");
    CheckEqI32(ManufScreen_UpdateProgress(NULL), 0, "so is the progress loop");
    {
        ManufQtyEnv bad = env;
        bad.pProductPanel = NULL;
        CheckEqI32(ManufScreen_DecEngineers(&bad), 0, "a missing product panel is refused");
        bad = env;
        bad.apManufCols = NULL;
        CheckEqI32(ManufScreen_ReduceSelectedQtyToOne(&bad), 0, "a missing column is refused");
        bad = env;
        bad.apProductB = NULL;
        CheckEqI32(ManufScreen_DecUnits(&bad), PUFO_MANUF_ANCHOR,
                   "a missing product-B array skips the store but keeps the return");
        bad = env;
        bad.apProductC = NULL;
        g_aProdB[7] = 5;
        CheckEqI32(ManufScreen_DecUnits(&bad), PUFO_MANUF_ANCHOR,
                   "a missing committed column skips the store but keeps the return");
        CheckEqI32(g_aProdB[7], 5, "and the order is left alone");
    }
    g_nRow = PUFO_PANEL_LINES;
    CheckEqI32(ManufScreen_IncEngineers(&env), 0, "a row past the panel's rows is refused");
    g_nRow = 0;
    g_Panel.anLineAux[0] = PUFO_PRODUCT_ENTRIES_PER_BASE;
    CheckEqI32(ManufScreen_DecUnits(&env), 0, "a product number past the 200 is refused");
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
    static u8 g_nEntries;
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
        g_aTopics[i].abNeedItems[0] = 7;
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

static i32 g_nNewBaseSeamId; /* distinct: the confirm control and the frame control get different thunks */
static void ThunkTryConfirm(void) { g_nNewBaseSeamId = 1; }
static void ThunkClose(void)      { g_nNewBaseSeamId = 2; }

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
    confEnv.bPendingRegionId = 128 + 2; /* cost 1,000,000 */
    bFlag1870C8 = 0;
    nFlag1870B4 = 5;
    g_nMapClickCalls = 0;
    g_nMapClickResult = 77;
    CheckEqI32(NewBaseScreen_TryConfirm(1, &confEnv), 77, "pending placement goes straight to OnMapClick");
    CheckEqI32(g_nMapClickCalls, 1, "  OnMapClick reached despite zero funds");
    Check(bFlag1870C8 == 0 && nFlag1870B4 == 5, "  failure flags untouched");

    /* funds - cost == 0 passes (SUBS is not MI). */
    nFunds = 1000000;
    confEnv.bPendingRegionId = 128 + 2;
    g_nMapClickCalls = 0;
    CheckEqI32(NewBaseScreen_TryConfirm(0, &confEnv), 77, "exact funds pass");
    CheckEqI32(g_nMapClickCalls, 1, "  OnMapClick reached");

    /* One short fails: 0x1870C8 = 1, dword_1870B4 = 0, return 0. */
    nFunds = 999999;
    confEnv.bPendingRegionId = 128 + 2;
    bFlag1870C8 = 0;
    nFlag1870B4 = 5;
    g_nMapClickCalls = 0;
    CheckEqI32(NewBaseScreen_TryConfirm(0, &confEnv), 0, "shortfall returns dword_1870B4");
    CheckEqI32(g_nMapClickCalls, 0, "  OnMapClick not reached");
    Check(bFlag1870C8 == 1, "  0x1870C8 raised");
    CheckEqI32(nFlag1870B4, 0, "  dword_1870B4 cleared");

    /* The table boundary: kind 0 costs 800000, kind 9 costs 0, 0xFF costs nothing (deviation). */
    nFunds = 799999;
    confEnv.bPendingRegionId = 128;
    g_nMapClickCalls = 0;
    CheckEqI32(NewBaseScreen_TryConfirm(0, &confEnv), 0, "kind 0 costs 800000");
    nFunds = 0;
    confEnv.bPendingRegionId = 128 + 9;
    g_nMapClickCalls = 0;
    CheckEqI32(NewBaseScreen_TryConfirm(0, &confEnv), 77, "kind 9 costs 0");
    confEnv.bPendingRegionId = 0xFF;
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
    env.pnMsgShipIdx = &nStratIdx;                                                          \
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
    CheckEqI32(nShipSel, 1, "  Ship_FindIndexByStrategType @ 0x5a92c found craft 1 (bId 42)");
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

/* One fixture per env handler, not one fixture for all of them. Two reasons, both real: the claims
 * below name DIFFERENT original handlers (panel prev vs panel next vs the close thunk), which one
 * shared function cannot express; and empty bodies fold to a single address under MSVC's /OPT:ICF,
 * so even the shared-pointer claims were comparisons of a pointer with itself. Each records its id. */
static i32 g_nNopClickId;
static void NopPanelPrev(void)     { g_nNopClickId = 1; }
static void NopPanelNext(void)     { g_nNopClickId = 2; }
static void NopFacClose(void)      { g_nNopClickId = 3; }
static void NopBuyThunk(void)      { g_nNopClickId = 4; }
static void NopSoldierClose(void)  { g_nNopClickId = 5; }
static void NopEquipThunk(void)    { g_nNopClickId = 6; }
static void NopSelectThunk(void)   { g_nNopClickId = 7; }
static void NopPlaneClose(void)    { g_nNopClickId = 8; }

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
    env.pfnPanelPrev = NopPanelPrev;
    env.pfnPanelNext = NopPanelNext;
    env.pfnCloseThunk = NopFacClose;
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
    Check(upCtl.pfnClick == NopPanelPrev, "the +12 handler is the panel-prev thunk, stored BEFORE Setup and surviving it");
    Check(downCtl.pfnClick == NopPanelNext, "and the +22 handler is the panel-next thunk, not the same one");
    CheckEqI32(closeCtl.x, 224, "the close control is moved to x 224");
    CheckEqI32(closeCtl.y, 229, "and y 229");
    Check(closeCtl.pfnClick == NopFacClose, "its click handler is the close thunk, installed at both ends of the init");

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
    static u8 abKind[PUFO_REGION_BUILD_COUNT];
    static u8 aTexts[PUFO_TEXT_REC_STRIDE * (PUFO_REGION_NAME_REC_BASE + 0x8A)];
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
    memcpy(aTexts + (size_t)PUFO_TEXT_REC_STRIDE * (PUFO_REGION_NAME_REC_BASE + 0x80),
           "Reg1033", 7);

    memset(&facEnv, 0, sizeof(facEnv));
    facEnv.pCloseCtl = &closeCtl;
    facEnv.pUpCtl = &upCtl;
    facEnv.pDownCtl = &downCtl;
    facEnv.pfnCloseThunk = NopPlaneClose;
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
    env.bPendingRegionId = PUFO_REGION_NORTH_AMERICA;                                                      \
    env.pbConfirm = &bConfirm;                                                 \
    env.pbFacDone = &bDone;                                                    \
    env.nPlaceX = 1234;                                                        \
    env.nPlaceY = 5678;                                                        \
    env.apBases = apBases;                                                     \
    env.apSiteRecs = aSiteRecs;                                                \
    env.pStrategList = &list;                                                  \
    env.pTextRecs = aTexts;                                                    \
    env.paRegionBuildCount = (i8 *)abKind;                                            \
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
    env.bPendingRegionId = 0xFF;
    CheckEqI32(Geoscape_OnMapClick(&env), 0, "screen 2 with the pending kind 0xFF: nothing runs");
    CheckEqI32(nScreen, 2, "and the screen id stays 2");

    /* 7. the confirm branch commits the pending facility. */
    MAP_BEGIN(2);
    CheckEqI32(Geoscape_OnMapClick(&env), 2, "the confirm branch commits the pending facility");
    CheckEqI32(aBase.nUnk8, 1234, "the base takes the pending x");
    CheckEqI32(aBase.nUnk12, 5678, "and the pending y");
    CheckEqI32(aBase.bRegionId, 0x80, "bRegionId becomes the pending region");
    CheckEqI32(aBase.bUnk16, 1, "and bUnk16 is set to 1");
    CheckEqI32(aSiteRecs[0].bType, 0, "the record type is region + 0x80, which wraps to the index");
    Check(strcmp(aSiteRecs[0].abName, "Reg1033-0") == 0,
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
    env.pfnBuyThunk = NopBuyThunk;

    BaseFacScreen_Toggle(&env);
    CheckEqI32(bOpen, 1, "the first call takes the build branch");
    Check(pPanel != NULL, "the panel is allocated");
    p = pPanel;
    CheckEqI32(p->nLineCount, 9, "nLineCount is the ctor count 10 minus the 0x58C68 decrement");
    CheckEqI32(p->nStyle, 5, "nStyle 5");
    CheckEqI32(p->nFont, 16, "nFont 16");
    CheckEqI32(p->nPosX, 120, "nPosX 120");
    CheckEqI32(p->nPosY, 234, "nPosY 234");
    CheckEqI32(p->nRectX, 5, "nRectX 5");
    CheckEqI32(p->nRectY, 234, "nRectY 234");
    CheckEqI32(p->nViewW, 230, "nViewW 230");
    CheckEqI32(p->nUnk7076, 1, "nUnk7076 1");
    CheckEqI32(p->bColorKey, 0x96, "bColorKey is the byte -106");
    CheckEqI32(p->bRowRules, 0, "bRowRules cleared");
    CheckEqI32(p->bSelLineHighlight, 1, "bSelLineHighlight set");
    CheckEqI32(p->aStyleRgb[0], 0xB4, "aStyleRgb[0] B4");
    CheckEqI32(p->aStyleRgb[3], 0x46, "aStyleRgb[3] 46");
    CheckEqI32(p->aStyleRgb[6], 0x44, "aStyleRgb[6] 44");
    CheckEqI32(p->aStyleRgb[11], 0x4B, "aStyleRgb[11] 4B");
    Check(p->pfnClick == NopBuyThunk, "the +3020 handler is the buy thunk");

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
        Check(false, "the panel could not be constructed"); /* counted: a bare printf would exit 0 */
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
    CheckEqI32(nSel, 1, "unk_158868 takes panel->nUnk3032 (the store at 0x58998)");

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
    static i32 nSolCount;   /* SoldierScreenEnv.pnSoldierCount takes the shell's i32 cell */
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
    env.pfnCloseSoldierThunk = NopSoldierClose;
    env.pfnEquipThunk = NopEquipThunk;
    env.pfnSelectThunk = NopSelectThunk;
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
    Check(pA->pfnClick == NopEquipThunk, "panel A's click is the equip thunk, not the select one");
    CheckEqI32(pA->bColorKey, 0xFF, "panel A's bColorKey is the byte -1");
    CheckEqI32(pA->aStyleRgb[3], 0x32, "panel A's aStyleRgb[3] 32");
    CheckEqI32(pA->aStyleRgb[5], 0xBD, "and aStyleRgb[5] BD");
    CheckEqI32(pA->aStyleRgb[6], 0xC8, "aStyleRgb[6] C8");
    CheckEqI32(pB->nPosX, 190, "panel B at x 190");
    Check(pB->pfnClick == NULL, "panel B has no click handler");
    CheckEqI32(pB->aStyleRgb[5], 0xFA, "panel B's aStyleRgb[5] FA");
    CheckEqI32(pC->nLineCount, 3, "panels C and D hold one line per soldier on the base");
    CheckEqI32(pC->nStyle, 13, "nStyle 13");
    CheckEqI32(pC->nViewW, 234, "panel C is 234 wide");
    Check(pC->pfnClick == NopSelectThunk, "panel C's click is the select thunk");
    CheckEqI32(pC->bColorKey, 0x96, "panel C's bColorKey is the byte -106");
    CheckEqI32(pD->nViewW, 40, "panel D is 40 wide");
    CheckEqI32(pD->nUnk7076, 2, "panel D's nUnk7076 is 2");
    CheckEqI32(pD->bSelLineHighlight, 0, "panel D's bSelLineHighlight is clear");
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
    Check(closeCtl.pfnClick == NopSoldierClose, "the close control gets the soldier close thunk");

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
        Check(false, "the panel could not be constructed"); /* counted: a bare printf would exit 0 */
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
        Check(false, "the facility panel could not be constructed"); /* counted: a bare printf would exit 0 */
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
    env.nPendingRegionId = 0x80 + 2;
    env.paNewBaseCost = g_anNewBaseCostByRegion;
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
    CheckEqI32(nFunds, 1000, "the facility cost comes off nFunds (the store at 0x59AC8)");
    CheckEqI32(GridFacType(abGrid, 0, 2, 3), 3, "the cell kind is aBaseFacFlags[3].bFlag21");
    CheckEqI32(bOverlayShown, 1, "the preview block ran");

    /* Not enough funds. */
    memset(abGrid, 0, sizeof(abGrid));
    GridTileA(abGrid, 0, 2, 2) = 9;
    nFunds = 100;
    nShortfall = 0;
    nResidue = 7;
    bPreviewOk = 1;
    CheckEqI32(BaseFacScreen_DrawPlacementOverlay(&env), 0, "an unaffordable facility stops (the SUBS at 0x59A2C)");
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
    CheckEqI32(nFunds, 5000 - 4000 - g_anNewBaseCostByRegion[2],
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
        Check(false, "the facility panel could not be constructed"); /* counted: a bare printf would exit 0 */
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
static i8 g_nOptTab; /* gOptionsScratch[0] @ 0x159E88, the tab byte */
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

/* The level the sfx applier hands the backend is observable through this seam, and so is the sample
 * count: Fake_Init succeeds so Sound_InitAndLoadSamples opens the seam without the retry sleeps, and
 * Fake_PlaySound counts what Sound_Play hands the backend. */
static i32 g_nSfxLevel;
static i32 g_nSfxLevelCalls;
static i32 g_nOptPlayCalls;

static bool Fake_Init(u32 nMix, i32 nCh, u32 nFlags) { (void)nMix; (void)nCh; (void)nFlags; return true; }
static void Fake_SetWindow(void *h) { (void)h; }
static char g_fakeSampleHandle;
static void *Fake_SampleLoad(const void *p, u32 cb, u32 m) { (void)p; (void)cb; (void)m; return &g_fakeSampleHandle; }
static void Fake_SampleSetMode(void *p, u32 m) { (void)p; (void)m; }
static void Fake_SampleFree(void *p) { (void)p; }
static i32 Fake_PlaySound(void *p) { (void)p; g_nOptPlayCalls += 1; return 0; }
static void Fake_SetSfxVolume(i32 n) { g_nSfxLevel = n; g_nSfxLevelCalls += 1; }
static void Fake_SetSfxPaused(i32 nChannel, i32 bPaused) { (void)nChannel; (void)bPaused; }
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

/* ------------------------------------------------- the soldier detail read-out and the transfer panels
 *
 * Evidence: SoldierScreen_DrawDetail @ 0x632cc, SoldierScreen_DrawEquipPanels @ 0x62d78. */

static SoftSurf g_sdSurf;
static Soldier g_aSdSol[2];
static Soldier *g_apSdSol[2];
static u16 g_nSdSolCount;
static Ship g_aSdShip[3];
static Ship *g_apSdShip[3];
static ObjectRecFull g_aSdObj[125];
static ObjectRecFull *g_apSdObj[125];
static char g_aSdText[200 * PUFO_TEXT_REC_STRIDE];
static u8 g_aSdRank[PUFO_TXT_E_COUNT * PUFO_TXT_E_STRIDE];
static PadImage g_aSdPad[3];
static PadImage *g_apSdPad[3];
static u8 g_aSdPadData[3][8];
static PadImage g_aSdCtlPad[2];
static UiControl g_sdCtl[2];
static UiPanel g_sdPanelEquip, g_sdPanelStores;
static u8 g_bSdAnimStarted;
static i8 g_nSdAnimProgress;
static UiAnimBlitEnv g_sdAnim;
static GfxColorEnv g_sdColor;
static i32 g_nSdCursorCopy;
static u8 g_bSdTransfer, g_bSdNameEdit, g_bSdSelChar, g_bSdPreviewOk;
static i8 g_nSdBaseIdx;
static SoldierDetailEnv g_sdEnv;
static i32 g_aSdCtlLog[8];
static i32 g_nSdCtlBlits;

/* The fill colour, written out here instead of shared with the render module, so a changed blit
 * argument in the port shows up as a different expected value. */
static u16 SdPix(u32 nIdx, u32 nMid, u32 nLow)
{
    return (u16)(((nIdx >> 3) << 11) + 32u * (nMid >> 2) + (nLow >> 3));
}

static u16 SdGet(i32 x, i32 y)
{
    return ((const u16 *)g_sdSurf.pPixels)[(size_t)y * 240u + (size_t)x];
}

static void SdClear(void)
{
    memset(g_sdSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    g_nSdCtlBlits = 0;
}

static i32 SdInk(i32 x0, i32 y0, i32 x1, i32 y1)
{
    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            if (SdGet(x, y) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* Gfx_PackRgb565Pair's colour, written out independently (0x544fc): the pair's first colour lands in
 * palette entry 33 and the second in entry 27, both after the same 0.7 scale (the gamma this fixture
 * states is 0). */
static u16 SdPair565(i32 nR, i32 nG, i32 nB)
{
    i32 nR1 = (i32)(((double)(nR >> 3)) / 0.7);
    i32 nG1 = (i32)(((double)(nG >> 2)) / 0.7);
    i32 nB1 = (i32)(((double)(nB >> 3)) / 0.7);
    if (nR1 > 31) { nR1 = 31; }
    if (nG1 > 63) { nG1 = 63; }
    if (nB1 > 31) { nB1 = 31; }
    return (u16)(nB1 + 32 * (nG1 + (nR1 << 6)));
}

static i32 SdHasColour(i32 x0, i32 y0, i32 x1, i32 y1, u16 nWant)
{
    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            if (SdGet(x, y) == nWant) {
                return 1;
            }
        }
    }
    return 0;
}

static void SdCtlBlit(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    if (g_nSdCtlBlits < 8) {
        g_aSdCtlLog[g_nSdCtlBlits] = (i32)(pPad - g_aSdCtlPad);
    }
    ++g_nSdCtlBlits;
}

static void SdPutText(i32 nRec, const char *psz)
{
    snprintf(g_aSdText + (size_t)PUFO_TEXT_REC_STRIDE * (size_t)nRec, 32, "%s", psz);
}

static void SdFixture(void)
{
    memset(g_aSdSol, 0, sizeof(g_aSdSol));
    memset(g_aSdShip, 0, sizeof(g_aSdShip));
    memset(g_aSdObj, 0, sizeof(g_aSdObj));
    memset(g_aSdText, 0, sizeof(g_aSdText));
    memset(g_aSdRank, 0, sizeof(g_aSdRank));
    memset(g_aSdPad, 0, sizeof(g_aSdPad));
    memset(g_aSdPadData, 0, sizeof(g_aSdPadData));
    memset(g_aSdCtlPad, 0, sizeof(g_aSdCtlPad));
    memset(g_sdCtl, 0, sizeof(g_sdCtl));

    /* Soldier 0 is the full case: a veteran (5 missions, so the class insignia draws), armour type 1,
     * aboard craft 1, psi-trained, class 3 (cs0 then cs1). */
    snprintf(g_aSdSol[0].szName, sizeof(g_aSdSol[0].szName), "%s", "HEMAN");
    g_aSdSol[0].bRankType = 2;
    g_aSdSol[0].nUnitType = 1;
    g_aSdSol[0].uMissions = 5;
    g_aSdSol[0].uKills = 7;
    g_aSdSol[0].bUnk183 = 1;
    g_aSdSol[0].bPsiActive = 1;
    g_aSdSol[0].nClass = 3;
    g_aSdSol[0].auStat[0] = 60;  /* Time     -> bar 32 wide at y 126 */
    g_aSdSol[0].auStat[1] = 40;  /* never drawn */
    g_aSdSol[0].auStat[2] = 30;
    g_aSdSol[0].auStat[3] = 50;
    g_aSdSol[0].auStat[4] = 70;
    g_aSdSol[0].auStat[5] = 20;
    g_aSdSol[0].auStat[6] = 15;  /* Psi Skill     -> bar at y 302 */
    g_aSdSol[0].auStat[7] = 80;  /* Psi Strength  -> bar at y 280 */
    g_aSdSol[0].auStat[8] = 90;  /* Strength      -> bar 47 wide at y 258 */
    g_aSdSol[0].auStat[9] = 45;  /* Stamina       -> bar 24 wide at y 148 */

    /* Soldier 1 is the gate case: a rookie (3 missions, no insignia), an alien unit type (no armour
     * value), credited off a returned craft (254, 12 recovery days), no psi, class 0. */
    snprintf(g_aSdSol[1].szName, sizeof(g_aSdSol[1].szName), "%s", "ZLOKAR");
    g_aSdSol[1].bRankType = 0;
    g_aSdSol[1].nUnitType = 5;
    g_aSdSol[1].uMissions = 3;
    g_aSdSol[1].uKills = 1;
    g_aSdSol[1].bUnk183 = 254;
    g_aSdSol[1].bUnk247 = 12;
    g_aSdSol[1].nClass = 0;
    for (i32 i = 0; i < PUFO_UNIT_STAT_COUNT; ++i) {
        g_aSdSol[1].auStat[i] = 20;
    }

    g_apSdSol[0] = &g_aSdSol[0];
    g_apSdSol[1] = &g_aSdSol[1];
    g_nSdSolCount = 2;

    for (i32 i = 0; i < 3; ++i) {
        g_aSdShip[i].bType = (u8)(i + 1);
        g_apSdShip[i] = &g_aSdShip[i];
    }
    g_aSdShip[0].bCraftType = 0; /* Skyranger-1  */
    g_aSdShip[1].bCraftType = 1; /* Lightning-2  */
    g_aSdShip[2].bCraftType = 3; /* Interceptor-3: the arm the original leaves unset */

    for (i32 i = 0; i < 125; ++i) {
        g_apSdObj[i] = &g_aSdObj[i];
    }
    g_aSdObj[122].auPerBaseVal[0] = 5;
    g_aSdObj[123].auPerBaseVal[1] = 7;
    g_aSdObj[124].auPerBaseVal[0] = 3;

    /* Labels, in ASCII so the ink checks do not depend on the code page. */
    SdPutText(90, "ARM");   SdPutText(93, "NONE");  SdPutText(94, "PERS");
    SdPutText(95, "POWER"); SdPutText(96, "FLY");   SdPutText(123, "RK:");
    SdPutText(124, "CR:");  SdPutText(125, "MS:");  SdPutText(126, "KL:");
    SdPutText(127, "WR");   SdPutText(128, "T");    SdPutText(129, "ST");
    SdPutText(130, "BR");   SdPutText(131, "RE");   SdPutText(132, "FA");
    SdPutText(133, "TA");   SdPutText(134, "SN");   SdPutText(135, "PS");
    SdPutText(136, "PK");   SdPutText(137, "SKY-"); SdPutText(138, "LGT-");
    SdPutText(139, "AVG-");

    for (i32 i = 0; i < PUFO_TXT_E_COUNT; ++i) {
        g_aSdRank[(size_t)i * PUFO_TXT_E_STRIDE] = (u8)('A' + i);
    }

    /* Each class pad is one indexed pixel in its own palette slot, so the two insignia slots show which
     * pad was blitted where. */
    for (i32 i = 0; i < 3; ++i) {
        PadImage_ctor(&g_aSdPad[i]);
        g_aSdPad[i].pData = g_aSdPadData[i];
        g_aSdPad[i].uWidth = 1;
        g_aSdPad[i].uHeight = 1;
        g_aSdPad[i].nMode = (u8)(40 + i);
        g_aSdPadData[i][0] = 1;   /* the glyph header Blit_Indexed reads: u16 width, u16 height, indices */
        g_aSdPadData[i][2] = 1;
        g_aSdPadData[i][4] = 1;
        g_apSdPad[i] = &g_aSdPad[i];
        memset(g_aPalette[40 + i], 0, sizeof(g_aPalette[40 + i]));
        g_aPalette[40 + i][1] = (u32)(0x1111 * (i + 1));
    }

    for (i32 i = 0; i < 2; ++i) {
        memset(&g_aSdCtlPad[i], 0, sizeof(g_aSdCtlPad[i]));
        g_sdCtl[i].apPads[0] = &g_aSdCtlPad[i];
        g_sdCtl[i].bEnabled = 1;
    }

    UiPanel_FreeLines(&g_sdPanelEquip);
    UiPanel_FreeLines(&g_sdPanelStores);
    (void)UiPanel_ctor(&g_sdPanelEquip, 8);
    (void)UiPanel_ctor(&g_sdPanelStores, 8);

    g_bSdAnimStarted = 1;   /* already started... */
    g_nSdAnimProgress = 5;  /* ...and 50*5 >= 236, so the wipe reports "finished" */
    memset(&g_sdAnim, 0, sizeof(g_sdAnim));
    g_sdAnim.pbStarted = &g_bSdAnimStarted;
    g_sdAnim.pnProgress = &g_nSdAnimProgress;
    g_sdAnim.nFrameStyle = 0;

    memset(&g_sdColor, 0, sizeof(g_sdColor));

    g_nSdBaseIdx = 0;
    g_bSdTransfer = 0;
    g_bSdNameEdit = 0;
    g_bSdSelChar = 32;
    g_bSdPreviewOk = 1;
    g_nSdCursorCopy = -1;

    memset(&g_sdEnv, 0, sizeof(g_sdEnv));
    g_sdEnv.apSoldiers = g_apSdSol;
    g_sdEnv.pnSoldierCount = &g_nSdSolCount;
    g_sdEnv.apCraft = g_apSdShip;
    g_sdEnv.nCraftCount = 3;
    g_sdEnv.pTextRecs = g_aSdText;
    g_sdEnv.aszRankNames = g_aSdRank;
    g_sdEnv.apClassPads = g_apSdPad;
    g_sdEnv.pColor = &g_sdColor;
    g_sdEnv.pAnim = &g_sdAnim;
    g_sdEnv.pCtlBackspace = &g_sdCtl[0];
    g_sdEnv.pCtlAccept = &g_sdCtl[1];
    g_sdEnv.pfnCtlBlit = SdCtlBlit;
    g_sdEnv.pfnCtlBlitEx = SdCtlBlit;
    g_sdEnv.pbPreviewOk = &g_bSdPreviewOk;
    g_sdEnv.pbNameEditActive = &g_bSdNameEdit;
    g_sdEnv.pnNameEditChar = &g_bSdSelChar;
    g_sdEnv.pnCursorCopy = &g_nSdCursorCopy;
    g_sdEnv.pbTransferFlag = &g_bSdTransfer;
    g_sdEnv.EquipPanels.apObjects = g_apSdObj;
    g_sdEnv.EquipPanels.pnSelectedBaseIdx = &g_nSdBaseIdx;
    g_sdEnv.EquipPanels.pPanelEquip = &g_sdPanelEquip;
    g_sdEnv.EquipPanels.pPanelStores = &g_sdPanelStores;
    g_sdEnv.EquipPanels.pTextRecs = g_aSdText;
}

static void SdCheckLine(UiPanel *pPanel, i32 nRow, const char *pszWant, const char *pszWhat)
{
    const char *pszGot = UiPanel_GetLineText(pPanel, nRow);
    Check(pszGot != NULL && strcmp(pszGot, pszWant) == 0, pszWhat);
    if (pszGot != NULL && strcmp(pszGot, pszWant) != 0) {
        printf("      got \"%s\", wanted \"%s\"\n", pszGot, pszWant);
    }
}

static void TestSoldierTransferPanels(void)
{
    printf("SoldierScreen_DrawEquipPanels @ 0x62d78\n");

    PufoFont_Free(&g_fontSys); /* the panels' own text would otherwise land on the surface too */
    memset(&g_fontMetrics, 0, sizeof(g_fontMetrics));

    SdFixture();
    SoftSurf_ctor(&g_sdSurf);
    SoftSurf_AllocPixels(&g_sdSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_sdSurf;
    SdClear();

    /* Base 0 stocks objects 122 and 124: three rows, and the frame 18*3+29 tall. */
    CheckEqI32(SoldierScreen_DrawEquipPanels(&g_sdEnv.EquipPanels), 3,
               "base 0 stocks two of the three armour objects, so three rows (0x62DC0..0x62E28)");
    SdCheckLine(&g_sdPanelEquip, 0, "NONE", "row 0 is the equip panel's \"None\" (0x62E2C)");
    SdCheckLine(&g_sdPanelStores, 0, "", "  and the stores panel's row 0 label is empty");
    CheckEqI32(UiPanel_GetLineAux(&g_sdPanelEquip, 0), 0, "row 0 carries the 0 marker (0x62E84)");
    SdCheckLine(&g_sdPanelEquip, 1, "PERS", "row 1 is apObjects[122]'s name (0x62EAC)");
    SdCheckLine(&g_sdPanelStores, 1, "5", "  with its stock on the stores side (0x62ED8)");
    CheckEqI32(UiPanel_GetLineAux(&g_sdPanelEquip, 1), 1, "  and the 1 marker EquipSelected switches on");
    SdCheckLine(&g_sdPanelEquip, 2, "FLY", "row 2 is apObjects[124], not [123] (0x62FC0)");
    SdCheckLine(&g_sdPanelStores, 2, "3", "  with 123's zero row skipped (0x62F1C)");
    CheckEqI32(UiPanel_GetLineAux(&g_sdPanelEquip, 2), 3, "  and the marker is 3, not 2");
    CheckEqI32(UiPanel_GetLineAux(&g_sdPanelEquip, 3), 0, "no fourth row is written");
    CheckEqI32(g_sdPanelEquip.nLineCount, 3, "the equip panel gets nLineCount 3 (0x63040)");
    CheckEqI32(g_sdPanelStores.nLineCount, 3, "and so does the stores panel (0x63058)");

    CheckEqI32((i32)SdGet(45, 75), (i32)SdPix(0, 0x6E, 0x77), "the frame's outer rect starts at 45,75 (0x6308C)");
    CheckEqI32((i32)SdGet(214, 75), (i32)SdPix(0, 0x6E, 0x77), "  and runs 170 wide to 214");
    Check(SdGet(215, 75) == 0, "  no further");
    CheckEqI32((i32)SdGet(45, 157), (i32)SdPix(0, 0x6E, 0x77), "the outer rect is 18*3+29 = 83 tall, to y 157");
    Check(SdGet(45, 158) == 0, "  and one row shorter than that");
    CheckEqI32((i32)SdGet(46, 76), (i32)SdPix(0x30, 0xE0, 0xE8), "the middle rect is inset by 1 in its own colours (0x630CC)");
    CheckEqI32((i32)SdGet(46, 156), (i32)SdPix(0x30, 0xE0, 0xE8), "  and is 18*3+27 = 81 tall");
    Check(SdGet(46, 157) == (u16)SdPix(0, 0x6E, 0x77), "  so its last row leaves the outer one showing");

    /* Base 1 stocks only object 123. */
    SdClear();
    g_nSdBaseIdx = 1;
    CheckEqI32(SoldierScreen_DrawEquipPanels(&g_sdEnv.EquipPanels), 2,
               "the selected base picks the column, not the object (0x62DC0)");
    SdCheckLine(&g_sdPanelEquip, 1, "POWER", "so row 1 here is apObjects[123] (0x62F30)");
    SdCheckLine(&g_sdPanelStores, 1, "7", "  with base 1's quantity");
    CheckEqI32(UiPanel_GetLineAux(&g_sdPanelEquip, 1), 2, "  with the 2 marker");
    CheckEqI32(g_sdPanelEquip.nLineCount, 2, "and only two rows");
    g_nSdBaseIdx = 0;

    /* Nothing stocked at base 2: one row, and the frame 18*1+29 tall. */
    SdClear();
    g_nSdBaseIdx = 2;
    CheckEqI32(SoldierScreen_DrawEquipPanels(&g_sdEnv.EquipPanels), 1,
               "a base that stocks none of the three still draws row 0 (0x62DB8)");
    SdCheckLine(&g_sdPanelEquip, 0, "NONE", "the \"None\" row is unconditional");
    CheckEqI32((i32)SdGet(45, 121), (i32)SdPix(0, 0x6E, 0x77), "the frame is 18*1+29 = 47 tall, to y 121");
    Check(SdGet(45, 122) == 0, "  and stops there");
    g_nSdBaseIdx = 0;

    /* A longer call after a shorter one: the row count is stored before the rows are written, so the
     * rows a previous one-row call would have gated are reachable again. */
    SdClear();
    CheckEqI32(SoldierScreen_DrawEquipPanels(&g_sdEnv.EquipPanels), 3,
               "the panel still holds the shorter call's count of 1, and three rows are written anyway");
    SdCheckLine(&g_sdPanelEquip, 1, "PERS", "row 1 is written after a one-row call (0x62EAC)");
    SdCheckLine(&g_sdPanelStores, 2, "3", "  and so is row 2 on the stores side (0x62FF0)");

    /* The guards the original does not have. */
    SdClear();
    g_sdEnv.EquipPanels.pPanelEquip = NULL;
    CheckEqI32(SoldierScreen_DrawEquipPanels(&g_sdEnv.EquipPanels), 0, "a missing panel returns instead of dereferencing");
    Check(SdInk(45, 75, 214, 157) == 0, "  and draws no frame");
    CheckEqI32(SoldierScreen_DrawEquipPanels(NULL), 0, "a null env does nothing");
    g_sdEnv.EquipPanels.pPanelEquip = &g_sdPanelEquip;
}

static void TestSoldierDetail(void)
{
    i32 nRet;

    printf("SoldierScreen_DrawDetail @ 0x632cc\n");

    /* Phase A asserts only what the rect blits can produce, so the font is put away first: an earlier
     * suite may have left it loaded, and a glyph landing in the same band would hide a changed rect. */
    PufoFont_Free(&g_fontSys);
    memset(&g_fontMetrics, 0, sizeof(g_fontMetrics));

    SdFixture();
    SoftSurf_ctor(&g_sdSurf);
    SoftSurf_AllocPixels(&g_sdSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_sdSurf;

    /* --- the gates, with no font loaded: only the rect blits can land --- */
    SdClear();
    nRet = SoldierScreen_DrawDetail(NULL, 0);
    CheckEqI32(nRet, 0, "a null env does nothing");

    SdClear();
    nRet = SoldierScreen_DrawDetail(&g_sdEnv, 5);
    CheckEqI32(nRet, 0, "a cursor past the soldier count draws nothing (the original would not stop)");
    CheckEqI32(g_nSdCursorCopy, 5, "  but the cursor copy at 0x632F4 already ran");
    Check(SdInk(0, 0, 239, 319) == 0, "  and nothing was drawn");

    SdClear();
    nRet = SoldierScreen_DrawDetail(&g_sdEnv, 0);
    CheckEqI32(nRet, 0, "with the name editor not armed the read-out returns 0 (0x6469C)");
    CheckEqI32(g_nSdCursorCopy, 0, "and unk_159760 gets the cursor (0x632F4)");

    /* The header frame: 24 steps of 5 on y 5 and y 31, then the two 1x27 edges. */
    CheckEqI32((i32)SdGet(5, 5), (i32)SdPix(100, 100, 100), "the header ramp starts at x 3 in 100 (0x6332C)");
    CheckEqI32((i32)SdGet(120, 5), (i32)SdPix(215, 215, 215), "the last left step (x 118..122) overpaints the right half's first steps");
    CheckEqI32((i32)SdGet(123, 5), (i32)SdPix(245, 245, 245), "the right half survives only from x 123, in 0xFA-5*1 (0x63380)");
    CheckEqI32((i32)SdGet(232, 5), (i32)SdPix(135, 135, 135), "  and its last step is 0xFA-5*23 = 135");
    CheckEqI32((i32)SdGet(5, 31), (i32)SdPix(100, 100, 100), "the same ramp repeats at y 31 (0x63350)");
    Check(SdGet(2, 5) == 0, "nothing is drawn left of x 3");
    CheckEqI32((i32)SdGet(3, 20), (i32)SdPix(0x96, 0x96, 0x96), "the left edge is 0x96 at x 3 (0x633E0)");
    CheckEqI32((i32)SdGet(235, 20), (i32)SdPix(0x96, 0x96, 0x96), "and the right edge at x 235 (0x63404)");
    Check(SdGet(3, 32) == 0, "  both are 27 tall, so y 32 is clear");

    /* The class insignia: nClass 3 is cs0 in the left slot and cs1 in the right one. */
    CheckEqI32((i32)SdGet(33, 2), (i32)0x1111, "nClass 3 puts cs0 at 33,2 (0x63540)");
    CheckEqI32((i32)SdGet(67, 2), (i32)0x2222, "  and cs1 at 67,2 (0x63560)");
    Check(SdGet(34, 2) == 0, "  each pad is one pixel wide");
    CheckEqI32(g_aSdPad[0].nOriginX, 33, "cs0 is left with nOriginX 33");
    CheckEqI32(g_aSdPad[1].nOriginX, 67, "cs1 with 67 (the ARM's += 34)");
    CheckEqI32(g_aSdPad[2].nOriginX, 0, "and cs2 is untouched");

    SdClear();
    g_aSdSol[0].nClass = 5;
    (void)SoldierScreen_DrawDetail(&g_sdEnv, 0);
    CheckEqI32((i32)SdGet(33, 2), (i32)0x1111, "nClass 5 is cs0 then cs2 (0x634D4)");
    CheckEqI32((i32)SdGet(67, 2), (i32)0x3333, "  so the right slot shows cs2");

    SdClear();
    g_aSdSol[0].nClass = 6;
    (void)SoldierScreen_DrawDetail(&g_sdEnv, 0);
    Check(SdGet(33, 2) == 0 && SdGet(67, 2) == 0, "a class above 5 draws no insignia (0x634C0)");

    SdClear();
    g_aSdSol[0].nClass = 3;
    g_aSdSol[0].uMissions = 3;
    (void)SoldierScreen_DrawDetail(&g_sdEnv, 0);
    Check(SdGet(33, 2) == 0 && SdGet(67, 2) == 0, "and 3 missions is not above the gate, so no insignia (0x6348C)");
    g_aSdSol[0].uMissions = 5;

    /* The stat bars: (value >> 1) + 2 wide for the frame, one less for the fill. */
    CheckEqI32((i32)SdGet(9, 126), (i32)SdPix(0, 0x96, 0), "the Time bar frame starts at 9,126 (0x63C4C)");
    CheckEqI32((i32)SdGet(40, 126), (i32)SdPix(0, 0x96, 0), "  and is (60>>1)+2 = 32 wide");
    Check(SdGet(41, 126) == 0, "  and stops there");
    CheckEqI32((i32)SdGet(10, 127), (i32)SdPix(0x14, 0xFA, 0x14), "the fill is inset by 1 and 30 wide (0x63C80)");
    CheckEqI32((i32)SdGet(40, 127), (i32)SdPix(0, 0x96, 0), "  so the frame's last column still shows at 40,127");
    Check(SdGet(41, 127) == 0, "  and nothing is drawn past the frame");
    CheckEqI32((i32)SdGet(9, 148), (i32)SdPix(0x96, 0x96, 0), "Stamina reads auStat[9], not [1] (0x63D34)");
    CheckEqI32((i32)SdGet(32, 148), (i32)SdPix(0x96, 0x96, 0), "  45 gives a 24-wide bar");
    Check(SdGet(33, 148) == 0, "  and no further");
    CheckEqI32((i32)SdGet(9, 258), (i32)SdPix(0x96, 0, 0), "Strength reads auStat[8] in its own colours (0x641CC)");
    CheckEqI32((i32)SdGet(55, 258), (i32)SdPix(0x96, 0, 0), "  90 gives a 47-wide bar");
    Check(SdGet(56, 258) == 0, "  and no further");
    CheckEqI32((i32)SdGet(9, 280), (i32)SdPix(0x6E, 0x6E, 0x96), "Psi Strength reads auStat[7] under bPsiActive (0x642D4)");
    CheckEqI32((i32)SdGet(9, 302), (i32)SdPix(0x6E, 0x6E, 0x96), "and Psi Skill reads auStat[6] (0x643C8)");

    SdClear();
    (void)SoldierScreen_DrawDetail(&g_sdEnv, 1);
    Check(SdGet(9, 280) == 0 && SdGet(9, 302) == 0, "without bPsiActive neither psi bar is drawn (0x64218)");
    CheckEqI32((i32)SdGet(9, 258), (i32)SdPix(0x96, 0, 0), "  while the seven ordinary rows still draw");
    Check(SdGet(33, 2) == 0, "  and this rookie's 3 missions draws no insignia");

    /* The panel frame: nine rows at y 134 + 22k with a top edge only, and the two 1x6 side rails. */
    CheckEqI32((i32)SdGet(3, 134), (i32)SdPix(100, 100, 100), "the first panel row is at y 134 (0x64548)");
    CheckEqI32((i32)SdGet(3, 156), (i32)SdPix(100, 100, 100), "the next one 22 lower (0x64508)");
    Check(SdGet(3, 135) == 0, "  each row is one line, not a band");
    CheckEqI32((i32)SdGet(123, 134), (i32)SdPix(245, 245, 245), "  with the falling ramp on the right half");
    CheckEqI32((i32)SdGet(5, 58), (i32)SdPix(100, 100, 100), "the left side rail starts at 5,58 (0x645DC)");
    CheckEqI32((i32)SdGet(5, 63), (i32)SdPix(100, 100, 100), "  each step is 6 tall");
    CheckEqI32((i32)SdGet(5, 64), (i32)SdPix(105, 105, 105), "  and the next one starts immediately, in 105");
    CheckEqI32((i32)SdGet(5, 204), (i32)SdPix(205, 205, 205), "the falling half runs from 174 in 0xE6-5*k (0x64608)");
    CheckEqI32((i32)SdGet(5, 174), (i32)SdPix(195, 195, 195), "  and the climbing half reaches 201, overpainting its top");
    CheckEqI32((i32)SdGet(232, 58), (i32)SdPix(100, 100, 100), "and the right rail mirrors it at x 232 (0x6462C)");

    /* The transfer panels, behind bTransferFlag. */
    Check(SdGet(45, 75) == 0, "with bTransferFlag clear the transfer panels are not drawn (0x64678)");
    SdClear();
    g_bSdTransfer = 1;
    (void)SoldierScreen_DrawDetail(&g_sdEnv, 0);
    CheckEqI32((i32)SdGet(45, 75), (i32)SdPix(0, 0x6E, 0x77), "armed, the equip frame appears at 45,75 (0x6468C)");
    SdCheckLine(&g_sdPanelEquip, 1, "PERS", "  with base 0's armour rows");
    g_bSdTransfer = 0;
    UiPanel_FreeLines(&g_sdPanelEquip);
    UiPanel_FreeLines(&g_sdPanelStores);
    (void)UiPanel_ctor(&g_sdPanelEquip, 8);
    (void)UiPanel_ctor(&g_sdPanelStores, 8);

    /* The keyboard: armed and finished, armed and still wiping, and not armed at all. */
    SdClear();
    g_bSdNameEdit = 1;
    g_nSdAnimProgress = 0;
    nRet = SoldierScreen_DrawDetail(&g_sdEnv, 0);
    CheckEqI32(nRet, 0, "while the wipe is still expanding the keyboard is skipped (0x646E0)");
    CheckEqI32(g_nSdCtlBlits, 0, "  and neither key pad is blitted");
    Check(SdGet(12, 80) == 0, "  and no key box is drawn");

    SdClear();
    g_nSdAnimProgress = 5;
    nRet = SoldierScreen_DrawDetail(&g_sdEnv, 0);
    CheckEqI32(nRet, 1, "once the wipe finishes the keyboard draws (0x646E8)");
    CheckEqI32(g_nSdCtlBlits, 2, "the two key pads blit (0x646F4, 0x64700)");
    CheckEqI32(g_aSdCtlLog[0], 0, "  the BackSpace pad first");
    CheckEqI32(g_aSdCtlLog[1], 1, "  then the Accept pad");
    CheckEqI32((i32)SdGet(12, 80), (i32)SdPix(0xFA, 0, 0), "the selected space key gets the dark box (0x64824)");
    CheckEqI32((i32)SdGet(12, 93), (i32)SdPix(0xFA, 0, 0), "  whose right edge is 14 tall, to y 93");
    /* The wipe itself blends the surface it covers, so "clear" is not what a pixel outside the box
     * looks like here; what says "the box stopped" is that the box colour is absent. */
    Check(SdGet(12, 94) != (u16)SdPix(0xFA, 0, 0), "  and stops there");

    SdClear();
    g_bSdPreviewOk = 0;
    (void)SoldierScreen_DrawDetail(&g_sdEnv, 0);
    CheckEqI32((i32)SdGet(12, 80), (i32)SdPix(0xFA, 0x14, 0x14), "without the preview flag the box is the ordinary one (0x64914)");
    g_bSdPreviewOk = 1;

    SdClear();
    g_bSdSelChar = 65;
    (void)SoldierScreen_DrawDetail(&g_sdEnv, 0);
    Check(SdGet(12, 80) != (u16)SdPix(0xFA, 0, 0),
          "a selected character that is not a space leaves the space key unframed (0x647F0)");
    g_bSdSelChar = 32;

    SdClear();
    g_bSdNameEdit = 0;
    (void)SoldierScreen_DrawDetail(&g_sdEnv, 0);
    CheckEqI32(g_nSdCtlBlits, 0, "with the name editor off no key pad blits (0x6469C)");

    /* --- with the real font: the parts only text can produce --- */
    {
        const char *const apszArch[2] = { "lang.dat", "main.dat" };
        bool bReady = false;

        if (Asset_OpenArchives(apszArch)) {
            PufoFont_Init(&g_fontSys);
            g_fontSys.nGlyphBase = g_fontSys.nTotalSprFrames;
            (void)Font_LoadSpr(&g_fontSys, "font");
            g_fontSys.nBaseBitsBase = g_fontSys.nTotalSprFrames; /* Geoscape_InitUI @ 0x85F78 */
            (void)Font_LoadSpr(&g_fontSys, "basebits");
            Font_BuildIndex(&g_fontSys);
            FontMetrics_Load();
            bReady = (g_fontSys.apSprIndex != NULL && g_fontMetrics.bLoaded);
        }
        if (!bReady) {
            printf("  SKIP font assets unavailable, the text-only checks are not run\n");
            return;
        }

        SdClear();
        (void)SoldierScreen_DrawDetail(&g_sdEnv, 0);
        Check(SdInk(0, 7, 60, 20) != 0, "the rank icon and the name draw at 5,7 and around x 160 (0x63468)");
        Check(SdInk(170, 40, 235, 52) != 0, "unit type 1 draws an armour value at 188,38 (0x636F8)");

        SdClear();
        (void)SoldierScreen_DrawDetail(&g_sdEnv, 1);
        Check(SdInk(170, 40, 235, 52) == 0, "unit type 5 has no switch arm, so no armour value is drawn (0x636C0)");
        Check(SdInk(100, 91, 140, 100) != 0, "craft byte 254 draws the Wound Recovery line at 120,89 (0x63930)");

        SdClear();
        g_aSdSol[1].bUnk183 = 255;
        (void)SoldierScreen_DrawDetail(&g_sdEnv, 1);
        Check(SdInk(100, 91, 140, 100) == 0, "craft byte 255 does not (0x6391C)");
        g_aSdSol[1].bUnk183 = 254;

        SdClear();
        g_bSdNameEdit = 1;
        g_nSdAnimProgress = 5;
        (void)SoldierScreen_DrawDetail(&g_sdEnv, 0);
        /* x 100..226 keeps the bars out of the band and y 275..279 keeps the Psi Strength bar (which
         * starts at 280) out of it, so only the grid's last row can put ink there. */
        /* The wipe blends everything inside (0,70)-(235,319), so "some ink" proves nothing here. What
         * does prove the grid drew is its own colour: the glyph palette entry the pair writes. */
        Check(SdHasColour(100, 275, 200, 279, SdPair565(20, 20, 20)),
              "the keyboard's last row draws its cells in the (150,150,120)/(20,20,20) pair (0x64A24)");
        Check(SdHasColour(60, 80, 200, 92, SdPair565(250, 250, 20)),
              "  and its first row draws its cells in the (250,250,20)/(20,20,20) pair");
        g_bSdNameEdit = 0;

        PufoFont_Free(&g_fontSys);
        memset(&g_fontMetrics, 0, sizeof(g_fontMetrics));
    }
}

static void TestOptionsAudioTab(void)
{
    i32 nBad;

    printf("OptionsScreen_Init @ 0x6be74 and the volume radio group\n");

    memset(&g_optCtl, 0, sizeof(g_optCtl));
    g_nOptTab = 0;
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
    g_optTab.pbTab = &g_nOptTab;
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

    CheckEqI32(g_nOptTab, 0, "LayoutButtons selects the audio tab (0x6B6D0)");
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

    /* Twelve radio handlers, twelve original bodies, each hard-coding its own step and its own widget,
     * so every iteration cites the handler it drives. The sample is the point: each sfx member plays
     * Sound_Play(17,4) TWICE -- a BL at 0x6BC64 and a tail `B Sound_Play` at 0x6BC74 with the
     * immediates reloaded at 0x6BC68/0x6BC6C -- while step 0 plays nothing because Sound_Play @ 0x9E10C
     * tests Map_bSoundOff first. The music group plays nothing at all (no Sound_Play in 18 instructions). */
    static const char *const apszMusic[6] = { "@ 0x6ba14", "@ 0x6ba70", "@ 0x6babc", "@ 0x6bb18",
                                              "@ 0x6bb74", "@ 0x6bbd0" };
    static const char *const apszSfx[6] = { "@ 0x6bc1c", "@ 0x6bc80", "@ 0x6bce4", "@ 0x6bd48",
                                            "@ 0x6bdac", "@ 0x6be10" };
    static i32 (*const apfnMusic[6])(OptionsVolumeEnv *) = { OptionsScreen_SetMusicVol0,
                                                             OptionsScreen_SetMusicVol1,
                                                             OptionsScreen_SetMusicVol2,
                                                             OptionsScreen_SetMusicVol3,
                                                             OptionsScreen_SetMusicVol4,
                                                             OptionsScreen_SetMusicVol5 };
    static i32 (*const apfnSfx[6])(OptionsVolumeEnv *) = { OptionsScreen_SetSfxVol0,
                                                           OptionsScreen_SetSfxVol1,
                                                           OptionsScreen_SetSfxVol2,
                                                           OptionsScreen_SetSfxVol3,
                                                           OptionsScreen_SetSfxVol4,
                                                           OptionsScreen_SetSfxVol5 };

    Check(Sound_InitAndLoadSamples("tact"), "the audio seam opens");
    Check(Sound_GetSampleCount() > 17, "sample 17 is loaded, so Sound_Play reaches the backend");
    Sound_SetMuteBytes(&g_bSfxOff, &g_bMusicOff); /* Sound_Play @ 0x9E10C reads the live soundoff cell */
    for (i32 nStep = 0; nStep < 6; ++nStep) {
        g_nOptPlayCalls = 0;
        (void)apfnSfx[nStep](&g_optVol);
        CheckEqI32(g_nSfxVol, nStep, OptCite("the sfx handler stores its own step", apszSfx[nStep]));
        CheckEqI32(g_bSfxOff, (nStep == 0) ? 1 : 0, OptCite("  step 0 is the mute step", apszSfx[nStep]));
        CheckEqI32(g_aOptSfx[nStep].nState, 2, OptCite("  and lights its own button", apszSfx[nStep]));
        nBad = 0;
        for (i32 i = 0; i < 6; ++i) {
            if (i != nStep && g_aOptSfx[i].nState != 0) {
                nBad += 1;
            }
        }
        CheckEqI32(nBad, 0, OptCite("  the group clear left only that one lit", apszSfx[nStep]));
        CheckEqI32(g_nOptPlayCalls, (nStep == 0) ? 0 : 2,
                   OptCite("  muted plays nothing, a step plays 17 twice (0x6BC64, 0x6BC74)",
                           apszSfx[nStep]));
    }
    for (i32 nStep = 0; nStep < 6; ++nStep) {
        g_nOptPlayCalls = 0;
        (void)apfnMusic[nStep](&g_optVol);
        CheckEqI32(g_nMusicVol, nStep, OptCite("the music handler stores its own step", apszMusic[nStep]));
        CheckEqI32(g_bMusicOff, (nStep == 0) ? 1 : 0, OptCite("  step 0 mutes music", apszMusic[nStep]));
        CheckEqI32(g_aOptMusic[nStep].nState, 2, OptCite("  and lights its own button", apszMusic[nStep]));
        nBad = 0;
        for (i32 i = 0; i < 6; ++i) {
            if (i != nStep && g_aOptMusic[i].nState != 0) {
                nBad += 1;
            }
        }
        CheckEqI32(nBad, 0, OptCite("  the group clear left only that one lit", apszMusic[nStep]));
        CheckEqI32(g_nOptPlayCalls, 0, OptCite("  and the music group plays no sample at all",
                                               apszMusic[nStep]));
    }
    Sound_SetMuteBytes(NULL, NULL);
    Sound_Shutdown();
    Sound_SetBackend(NULL);

    g_nSlotFired = -1;
    OptionsScreen_BindSaveSlots(&g_optTab);
    CheckEqI32(g_nOptTab, 1, "the save tab is scratch byte 1 (0x6B4A8)");
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
    CheckEqI32(g_nOptTab, 2, "the load tab is scratch byte 2 (0x6B594)");
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

/* The save handler's tail call is now the ported dispatcher, so the probe runs INSIDE it:
 * screen 16 with both ufopaedia bytes clear takes the branch that opens the ufopaedia (0x8BD58),
 * and that call is the point the original's `B Geoscape_CloseUI` reached. Case 16 then sets the
 * screen id to 0 (0x8BD5C), which is the second thing the probe proves. */
static GeoscapeCloseEnv g_slotClose;
static u8 g_bSlotUfDirty;
static u8 g_bSlotUfOpen;

static void SlotCloseProbe(SaveSlotEnv *pEnv, i32 *pnScreen)
{
    CloseProbe(&g_slotClose);
    g_bSlotUfDirty = 0;
    g_bSlotUfOpen = 0;
    g_slotClose.pnScreenId = pnScreen;
    g_slotClose.pbUfopaediaDirty = &g_bSlotUfDirty;
    g_slotClose.pbUfopaediaOpen = &g_bSlotUfOpen;
    g_slotClose.pfnUfopaediaOpen = Slot_CloseUi;
    pEnv->pClose = &g_slotClose;
}

static void TestSaveLoadSlots(void)
{
    static DatFile dat;
    static SaveGameInput in;
    static LoadGameEnv loadEnv;
    static LoadGameHooks loadHooks;
    static SaveSlotEnv env;
    static i32 nGeoscapeScreen;
    static i8 nTab;

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
    env.pbTab = &nTab;
    /* In the original the slot handler's store at 0x6B1C4 and the byte OptionsScreen_Init branches on at
     * 0x6BE84 are the SAME byte: LDR R0,=(gOptionsScratch+4) / STRB at 0x6B1C0..0x6B1C4 against the flag
     * test that opens the function. A second cell here would make the teardown check read something the
     * store did not cause -- which is how a first-draft mutation of 0x6b2cc came out inert. */
    env.pbControlsExist = &g_bOptBuilt; /* gOptionsScratch[4], the cell g_optInit.pbControlsExist holds */
    env.pnGeoscapeScreen = &nGeoscapeScreen;
    env.pOptionsInit = &g_optInit; /* the options fixture the previous test built */
    SlotCloseProbe(&env, &nGeoscapeScreen);

    /* The save handler. The render hook is the only place the transient mode 4 is visible. */
    g_nSlotListMode = 0;
    nGeoscapeScreen = 16;
    nTab = 9;
    g_nSlotRenderCalls = 0;
    g_nSlotCloseCalls = 0;

    DatFile_ctor(&dat);
    SaveGame_Slot3_CloseUI(&env);
    DatFile_Close(&dat);

    CheckEqI32(in.nSlot, 3, "the handler hands slot 3 to SaveGame (0x6B07C)");
    CheckEqI32(g_nSlotRenderCalls, 1, "SaveGame renders a frame before it opens the file (0x4499C)");
    CheckEqI32(g_nSlotListModeAtRender, 4, "  and the list mode is 4 while it writes (0x6AF90)");
    CheckEqI32(nTab, 0, "the tab byte goes back to 0 (0x6AFA4)");
    CheckEqI32(g_nSlotCloseCalls, 1, "Geoscape_CloseUI runs last (0x6AFB8)");
    CheckEqI32(g_nSlotListModeAtClose, 1, "  with the list mode already back to 1 (0x6AFAC)");
    CheckEqI32(nGeoscapeScreen, 0, "  and case 16 of the dispatcher sets the screen id to 0 (0x8BD5C)");
    CheckEqI32(g_nCloseFail, 0, "  and its head cleared the action-fail dword (0x8B96C)");

    /* The load handler: the byte it raises at 0x6B1C4 is what makes OptionsScreen_Init take its teardown
     * branch -- the store runs first, so the flag test at 0x6BE84 always sees 1 here. */
    g_nSlotListMode = 0;
    nGeoscapeScreen = 7;
    nTab = 9;
    g_bOptBuilt = 1;
    g_nSetupCalls = 0;

    DatFile_ctor(&dat);
    CheckEqI32(LoadGame_Slot5_InitOptions(&env), 1, "a missing slot is the failure path, which returns 1 (0x6B1E0)");
    DatFile_Close(&dat);

    CheckEqI32(loadEnv.nSlot, 5, "the handler hands slot 5 to LoadGame (0x6B1D4)");
    CheckEqI32(nTab, 0, "the tab byte is cleared first (0x6B1A4)");
    CheckEqI32(nGeoscapeScreen, 0, "the geoscape screen id is cleared (0x6B1B0)");
    CheckEqI32(g_nSlotListMode, 1, "the failed load puts the list mode back to 1 (0x6B1E0)");
    CheckEqI32(g_bOptBuilt, 0, "OptionsScreen_Init ran and took its teardown branch (0x6B1C4, 0x6B1C8)");
    CheckEqI32(g_nSetupCalls, 0, "  which positions nothing");

    /* The seven save handlers and the seven load handlers are copies that differ only in the slot
     * immediate -- `MOV R1,#1..#6` at 0x6B024, 0x6B070, 0x6B0BC, 0x6B108, 0x6B154 for the saves
     * (SaveGame_Slot1..6_CloseUI @ 0x6afcc..0x6b148) and at 0x6B29C, 0x6B304, 0x6B36C, 0x6B3D4,
     * 0x6B43C for the loads (LoadGame_Slot1..6_InitOptions @ 0x6b1fc..0x6b404). Each slot number is
     * therefore checked through the handler that hard-codes it. */
    static const char *const apszSave[7] = { "@ 0x6af80", "@ 0x6afcc", "@ 0x6b018", "@ 0x6b064",
                                             "@ 0x6b0b0", "@ 0x6b0fc", "@ 0x6b148" };
    static const char *const apszLoad[7] = { "@ 0x6b194", "@ 0x6b1fc", "@ 0x6b264", "@ 0x6b2cc",
                                             "@ 0x6b334", "@ 0x6b39c", "@ 0x6b404" };
    static void (*const apfnSave[7])(SaveSlotEnv *) = { SaveGame_Slot0_CloseUI, SaveGame_Slot1_CloseUI,
                                                        SaveGame_Slot2_CloseUI, SaveGame_Slot3_CloseUI,
                                                        SaveGame_Slot4_CloseUI, SaveGame_Slot5_CloseUI,
                                                        SaveGame_Slot6_CloseUI };
    static i32 (*const apfnLoad[7])(SaveSlotEnv *) = { LoadGame_Slot0_InitOptions,
                                                       LoadGame_Slot1_InitOptions,
                                                       LoadGame_Slot2_InitOptions,
                                                       LoadGame_Slot3_InitOptions,
                                                       LoadGame_Slot4_InitOptions,
                                                       LoadGame_Slot5_InitOptions,
                                                       LoadGame_Slot6_InitOptions };

    for (i32 nSlot = 0; nSlot < 7; ++nSlot) {
        g_nSlotListMode = 0;
        g_nSlotRenderCalls = 0;
        g_nSlotCloseCalls = 0;
        g_nSlotListModeAtRender = -1;
        g_nSlotListModeAtClose = -1;
        in.nSlot = -1;
        nTab = 9;
        nGeoscapeScreen = 16; /* the dispatcher case the probe hook answers to (0x8BD58) */
        DatFile_ctor(&dat);
        apfnSave[nSlot](&env);
        DatFile_Close(&dat);
        CheckEqI32(in.nSlot, nSlot, OptCite("the save handler hands SaveGame its own slot", apszSave[nSlot]));
        CheckEqI32(g_nSlotListModeAtRender, 4, OptCite("  mode 4 while the file is written", apszSave[nSlot]));
        CheckEqI32(nTab, 0, OptCite("  the tab byte cleared after it", apszSave[nSlot]));
        CheckEqI32(g_nSlotListModeAtClose, 1, OptCite("  mode 1 before the close tail call", apszSave[nSlot]));
        CheckEqI32(g_nSlotCloseCalls, 1, OptCite("  and exactly one Geoscape_CloseUI", apszSave[nSlot]));
    }

    for (i32 nSlot = 0; nSlot < 7; ++nSlot) {
        g_nSlotListMode = 0;
        loadEnv.nSlot = -1;
        nTab = 9;
        g_bOptBuilt = 1; /* the shared controls byte, raised here and cleared by the teardown */
        g_nSetupCalls = 0;
        nGeoscapeScreen = 7;
        DatFile_ctor(&dat);
        CheckEqI32(apfnLoad[nSlot](&env), 1,
                   OptCite("a missing slot is the failure path, which returns 1", apszLoad[nSlot]));
        DatFile_Close(&dat);
        CheckEqI32(loadEnv.nSlot, nSlot, OptCite("the load handler hands LoadGame its own slot",
                                                  apszLoad[nSlot]));
        CheckEqI32(g_nSlotListMode, 1, OptCite("  and mode 3 goes back to 1 on that failure",
                                                apszLoad[nSlot]));
        CheckEqI32(nGeoscapeScreen, 0, OptCite("  the screen id is cleared first", apszLoad[nSlot]));
        CheckEqI32(g_bOptBuilt, 0, OptCite("  the byte it raises drives the teardown", apszLoad[nSlot]));
        CheckEqI32(g_nSetupCalls, 0, OptCite("  and that branch positions nothing", apszLoad[nSlot]));
    }

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
    u8 nEntries;
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

    /* 7. the count is an unsigned byte (one byte at 0x160C4C, STRB @ 0x859E4), so its largest
     * possible value 255 still leaves the entry at index 255 outside the sum. The old spelling
     * modelled the count as u16 and tested a count of 300, a value the byte cannot hold. */
    BUILD_BEGIN();
    nEntries = 255;
    aGrid[GRID_CELL(2, 0, 0)] = 3;
    aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * 2 + 255] = 9000;
    Base_EnterBuildMode(&env);
    CheckEqI32(aGrid[GRID_NEED(2, 0, 0)], 0,
               "the byte count tops out at 255, so the 9000 at index 255 is not summed");

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
    env.pLines = &lines; env.pnMsgShipIdx = &nStratIdx;                    \
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
    env.pnMsgShipIdx = NULL;
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
static void TestClearProductSlot(void)
{
    ManufClearEnv env;
    GeoscapeCloseEnv hooks;
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
    memset(&env, 0, sizeof(env));                                            \
    env.pProductPanel = &g_ClearPanel; env.pnProductRow = &g_nClearRow;      \
    env.apProductA = g_aProdA; env.apProductB = g_aProdB;                    \
    env.apProductC = g_aProdC; env.apManufCols = g_aManufCol;                \
    env.abGridBytes = g_aGridBytes;                                          \
    env.apBases = g_apBases; env.nBaseCount = 3;                             \
    env.pnSelectedBaseIdx = &g_nClearBase;                                   \
    CloseProbe(&hooks);

    /* 1. one index of all four columns goes to zero, its neighbours do not move. */
    CLEAR_RESET();
    for (i = 238; i <= 242; ++i) {
        g_aProdA[i] = 11u; g_aProdB[i] = 22; g_aProdC[i] = 33; g_aManufCol[i] = 44;
    }
    /* The cleared slot needs all four bytes to reach zero: a 0x100 in its high half survives a
     * 2-byte store, and 0x6F8FC is a dword STR (0x6F8FC `STR R2,[LR,R0,LSL#2]`, stride x4). */
    g_aProdB[240] = 0x10016;
    ManufScreen_ClearProductSlot(&env, 1, 40); /* idx = 200*1 + 40 = 240 */
    CheckEqI32((i32)g_aProdA[240], 0, "the u32 column at 0x159E9C is cleared (0x6F918)");
    CheckEqI32(g_aProdB[240], 0, "  the 32-bit column at 0x15B79C is cleared (0x6F8FC)");
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
    CheckEqI32(g_nCloseFail, 0, "and Geoscape_CloseUI runs afterwards (0x6E9C8 -> 0x8B96C)");

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
    CheckEqI32(g_aProdB[240], 0, "no close env: the clear still runs");
    CheckEqI32(g_nCloseFail, 1, "  and the dispatcher does not run");

    ManufScreen_ConfirmAndClose(NULL, &hooks);
    CheckEqI32(g_nCloseFail, 1, "a null env closes nothing");
#undef CLEAR_RESET
}

static void TestIncUnits(void)
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
    ManufUnitsEnv env;
    i32 nRow;
    i8 nBase;
    u8 nGate;
    u32 dwShortfall;
    i32 nResidue;
    i32 i;

    printf("ManufScreen_IncUnits @ 0x6e13c\n");

#define UNITS_RESET(nProduct)                                                  \
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
    UNITS_RESET(119);
    aGrid[480 + 0] = 11; /* facType[base 0, row 0, col 0], buildDays 0 so it is free */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[119], 1, "one free hangar cell lets the allocation reach 1 (0x6E228)");
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[119], 1, "a second press stops at the free-slot count (0x6E21C)");
    CheckEqI32(dwShortfall, 0, "  and a hangar with room sets no shortfall");

    UNITS_RESET(120);
    aGrid[480 + 0] = 11;
    aGrid[480 + 6] = 11; /* row 0, col 1 */
    aProdB[120] = 1;
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[120], 2, "two hangar cells allow two units");
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[120], 2, "  and stops there");

    /* A hangar cell under construction is not capacity: the capacity pass wants buildDays zero. */
    UNITS_RESET(119);
    aGrid[480 + 0] = 11;
    aGrid[0] = 3; /* buildDays[base 0, row 0, col 0] */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[119], 0, "a hangar still being built gives no free slot (0x6611C)");
    CheckEqI32(dwShortfall, 1, "  cap 0 == at 0, so the shortfall dword is set (0x6E1BC)");
    CheckEqI32(nResidue, 1, "  and dword_1870B4[0] becomes 1 here (0x6E1C8)");

    /* Craft homed at the base count against the free slots. */
    UNITS_RESET(119);
    env.nCraftCount = 3;
    aGrid[480 + 0] = 11;
    aCraft[0].bHomeBase = 0;
    aCraft[1].bHomeBase = 1; /* a different base */
    aCraft[2].bHomeBase = 1;
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[119], 0, "the craft at this base fills the only slot");
    CheckEqI32(dwShortfall, 1, "  shortfall dword set");
    CheckEqI32(nResidue, 1, "  residue 1 on this path");

    /* Base_CountShipsAtBase adds the 0x159DBC byte for the base's type, and that counts too. */
    UNITS_RESET(119);
    aGrid[480 + 0] = 11;
    aGrid[480 + 6] = 11;
    aGrid[480 + 12] = 11; /* three slots */
    aBases[0].bUnk4 = 4;
    aGridBytes[0] = 3;
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[119], 0, "the grid byte fills all three slots (0x66110)");
    CheckEqI32(dwShortfall, 1, "  so this is the full-hangar path");

    /* The dead gate still short-circuits, and it is read a second time after the two flag stores. */
    UNITS_RESET(119);
    nGate = 1;
    aGrid[480 + 0] = 11;
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[119], 0, "a non-zero gate stops the button before anything else (0x6E14C)");
    CheckEqI32(dwShortfall, 0, "  no capacity call, so no shortfall store either");

    /* --- the stores path: every other product --- */
    UNITS_RESET(0);
    aGrid[480 + 0] = 6; /* one free stores cell -> capacity 300 */
    aObjs[0].bCargoSize = 7;
    aProdB[0] = 42; /* 42 * 7 = 294 < 300 */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[0], 43, "below the stores capacity the units count rises by one (0x6E30C)");
    aProdB[0] = 43; /* 43 * 7 = 301 >= 300 */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[0], 43, "at the capacity it stops");
    CheckEqI32(dwShortfall, 1, "  shortfall dword set (0x6E314)");
    CheckEqI32(nResidue, 0, "  and residue 0 here, where the full-hangar path wrote 1 (0x6E320)");

    UNITS_RESET(0);
    aGrid[480 + 0] = 6;
    aGrid[480 + 6] = 6; /* two cells -> 600 */
    aObjs[0].bCargoSize = 7;
    aProdB[0] = 85; /* 595 < 600 */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[0], 86, "a second stores cell doubles the capacity");

    UNITS_RESET(0);
    aGrid[480 + 6] = 6; /* facType at 480 + 6*col + row, here row 0 col 1 */
    aGrid[6] = 5;       /* buildDays at 6*col + row, the same cell */
    aObjs[0].bCargoSize = 7;
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[0], 0, "a stores cell under construction gives no capacity (0x65720)");
    CheckEqI32(dwShortfall, 1, "  capacity 0 == pressure 0, so the shortfall dword is set");

    /* Base_CalcStorageUsed @ 0x6555c adds bCargoSize * auPerBaseVal[base] over the filtered object indices,
     * and object 0 is never in that set -- so the product's own cargo size is not double counted. */
    UNITS_RESET(0);
    env.nObjectCount = 2; /* the storage sum walks indices 1..nObjectCount-1 */
    aGrid[480 + 0] = 6;
    aObjs[0].bCargoSize = 7;
    aObjs[1].bCargoSize = 10;
    aObjs[1].auPerBaseVal[0] = 4; /* index 1 is inside the 1..69 filter */
    aProdB[0] = 37; /* 259 + 40 = 299 < 300 */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[0], 38, "the base's own stock counts against the capacity (0x6E274)");
    aProdB[0] = 38; /* 266 + 40 = 306 >= 300 */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[0], 38, "  and pushes it over");

    /* Base_CountItemQtyAcrossBases adds the site cargo, gated on the site index being the base. */
    UNITS_RESET(0);
    env.nObjectCount = 2; /* the site walk also needs more than one object */
    aGrid[480 + 0] = 6;
    aObjs[0].bCargoSize = 7;
    aObjs[1].bCargoSize = 10;
    env.nSiteCount = 1;
    aSites[0].auItemQty[1] = 5; /* 10 * 5 = 50 */
    aProdB[0] = 35; /* 245 + 50 = 295 < 300 */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[0], 36, "the site cargo counts too (0x6E294)");
    aProdB[0] = 36; /* 252 + 50 = 302 >= 300 */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[0], 36, "  and pushes it over");

    /* The object whose cargo size is read is apObjects[PRODUCT], not apObjects[base]. */
    UNITS_RESET(1);
    env.nObjectCount = 2; /* product 1 has to be inside the object table to be read at all */
    aGrid[480 + 0] = 6;
    aObjs[0].bCargoSize = 100; /* if the base index were used here, 42*100 would blow the cap */
    aObjs[1].bCargoSize = 7;
    aProdB[1] = 42; /* base 0, product 1 -> idx 1, and 42*7 = 294 < 300 */
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[1], 43, "the cargo size comes from the product's object record (0x6E214)");

    UNITS_RESET(0);
    nBase = 1;
    aGrid[480 + 144] = 6; /* base 1's stores cell */
    aObjs[0].bCargoSize = 7;
    aProdB[200] = 42;
    ManufScreen_IncUnits(&env);
    CheckEqI32(aProdB[200], 43, "the column index is 200*nSelectedBaseIdx + product");

    UNITS_RESET(0);
    nGate = 200;
    ManufScreen_IncUnits(&env);
    CheckEqI32(dwShortfall, 0, "the gate short-circuits the stores path too");

    UNITS_RESET(0);
    env.apProductB = NULL;
    ManufScreen_IncUnits(&env);
    CheckEqI32(dwShortfall, 0, "no product column: nothing is stored");

    UNITS_RESET(0);
    env.pnProductRow = NULL;
    ManufScreen_IncUnits(&env);
    Check(true, "a missing row cache is not dereferenced");

    ManufScreen_IncUnits(NULL);
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
    GeoscapeCloseEnv hooks;
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
    memset(&env, 0, sizeof(env));                                                   \
    env.aSites = aSites; env.pnSiteCount = &nSiteCount;                             \
    env.pnSelectedBaseIdx = &nBase; env.apBases = apBases;                          \
    env.apObjects = apObjects; env.nObjectCount = nObjectCount;                     \
    env.nFunds = 123456; env.pnFundsSnapshot = &nSnapshot;                          \
    CloseProbe(&hooks);

    /* 1. An empty working record is not committed, but the tail still runs. */
    BS_RESET();
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(aSites[2].bFlag0, 0, "an empty working record keeps its flag");
    CheckEqI32(nSiteCount, 2, "  and the site count does not grow");
    CheckEqI32(nSnapshot, 123456, "the funds snapshot is written either way (0x75130)");
    CheckEqI32(g_nCloseFail, 0, "  and Geoscape_CloseUI runs either way (0x7513C -> 0x8B96C)");

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
    CheckEqI32(g_nCloseFail, 0, "  including the close");

    /* 8. Nulls. */
    BS_RESET();
    env.pnFundsSnapshot = NULL;
    BuySellScreen_ConfirmSiteAndClose(&env, &hooks);
    CheckEqI32(g_nCloseFail, 0, "a null snapshot still closes");
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

    printf("Funding_AddRegionBonus @ 0x7debc\n");

#define FUND_RESET()                                                     \
    for (i = 0; i < PUFO_FUNDING_SLOT_COUNT; ++i) { aSlots[i] = 0; }

    /* The compare chain's own mapping: 0x84 feeds slot 1 while 0x82 and 0x83 feed slots 2 and 3. */
    /* FUND_RESET uses i, so the loop counters here are n / k, never i. */
    static const i32 anRaces[8] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x88, 0xFF};
    static const i32 anRaceSlot[8] = {0, 0, 2, 3, 1, 4, 0, 5};
    for (i32 n = 0; n < 8; ++n) {
        FUND_RESET();
        Funding_AddRegionBonus(aSlots, anRaces[n]);
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
        Funding_AddRegionBonus(aSlots, anNoBonus[n]);
        i32 nTotal = 0;
        for (i32 k = 0; k < PUFO_FUNDING_SLOT_COUNT; ++k) {
            nTotal += aSlots[k];
        }
        CheckEqI32(nTotal, 0, "an unmapped race id changes nothing (locret_7DF7C)");
    }

    /* The argument is masked to a byte before the compares (0x7DEBC AND R0,R1,#0xFF). */
    FUND_RESET();
    Funding_AddRegionBonus(aSlots, 0x180);
    CheckEqI32(aSlots[0], 2, "0x180 masks down to 0x80");
    FUND_RESET();
    Funding_AddRegionBonus(aSlots, 0x1FF);
    CheckEqI32(aSlots[5], 2, "0x1FF masks down to 0xFF, the no-race tile");

    /* It accumulates: every radar contact of that race adds two more. */
    FUND_RESET();
    Funding_AddRegionBonus(aSlots, 0x85);
    Funding_AddRegionBonus(aSlots, 0x85);
    Funding_AddRegionBonus(aSlots, 0x85);
    CheckEqI32(aSlots[4], 6, "three contacts of race 0x85 give six");

    Funding_AddRegionBonus(NULL, 0x80);
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
static i32 g_nMonthCancel;  /* unk_159648: the roll's weight-row index, LDRSB at 0x81AB4 */
static u16 g_nAddendCancel; /* 0x1472C8: the roll's early gate, LDRH at 0x81B6C */
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
    g_nMonthCancel = 0;
    g_nAddendCancel = 0;
    g_ctxCancel.pnDifficulty = &g_nMonthCancel;
    g_ctxCancel.pnEarlyGate = &g_nAddendCancel;
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
    static i32 nMonthCounter;    /* unk_159648: the roll's weight-row index, LDRSB at 0x81AB4 */
    static u16 nAlienPoolAddend; /* 0x1472C8: the roll's early gate, LDRH at 0x81B6C */
    nMonthCounter = 0;
    nAlienPoolAddend = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnDifficulty = &nMonthCounter;
    ctx.pnEarlyGate = &nAlienPoolAddend;
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
/* Smap_GrantSiteRecovery @ 0x98b50. The fixture is the whole delivery world: the site array at its
 * shipped capacity (the compaction reads one record past the live tail), two bases whose bUnk4 is
 * their id, a craft table, one roster soldier, 150 object records with bCargoSize 1 so the cargo
 * volume equals the quantity, and the facility grid. */
static SmapSite g_aGrantSites[PUFO_SMAP_SITE_COUNT];
static BaseRec g_aGrantBases[2];
static BaseRec *g_apGrantBases[2];
static Ship g_aGrantCraft[4];
static Ship *g_apGrantCraft[4];
static Soldier g_aGrantSoldier;
static Soldier *g_apGrantSoldiers[4];
static ObjectRecFull g_aGrantObj[150];
static ObjectRecFull *g_apGrantObj[150];
static u8 g_aGrantGrid[PUFO_GRID_SIZE];
static UiLineList g_grantLines;
static u32 g_dwGrantShortfall;
static u8 g_nGrantBaseCount;
static u8 g_nGrantCraftCount;
static i32 g_nGrantSiteCount;
static i32 g_nGrantSoldierCount;
static i32 g_nGrantNextSoldierId;
static i32 g_nGrantNextCraftId;
static i32 g_nGrantSeed;
static i32 g_nGrantZoom;
static i32 g_nGrantSamples;
static ShipCtorEnv g_grantCtorEnv;
static PtrArray g_grantStratList;
static UnitCreateEnv g_grantUnitEnv;
static ShipSpawnEnv g_grantShipEnv;

static void GrantSample(i32 nSample, void *pUser)
{
    (void)nSample;
    (void)pUser;
    ++g_nGrantSamples;
}

/* fac[nBase] is grid + 0x174 + 144*nBase with tileA at +0, tileB at +36, layer2 at +72 and the
 * cell kind at +108; buildDays is grid + 36*nBase. */
#define GFAC(nBase, nLayer, nRow, nCol) \
    g_aGrantGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * (nBase) + (nLayer) + 6 * (nRow) + (nCol)]
#define GDAYS(nBase, nRow, nCol) \
    g_aGrantGrid[PUFO_GRID_DAYS_STRIDE * (nBase) + 6 * (nRow) + (nCol)]

#define GRANT_RESET()                                                                     \
    do {                                                                                  \
        memset(g_aGrantSites, 0, sizeof(g_aGrantSites));                                  \
        memset(g_aGrantGrid, 0, sizeof(g_aGrantGrid));                                    \
        memset(g_aGrantObj, 0, sizeof(g_aGrantObj));                                      \
        memset(g_aGrantCraft, 0, sizeof(g_aGrantCraft));                                  \
        memset(&g_aGrantSoldier, 0, sizeof(g_aGrantSoldier));                             \
        memset(&g_grantLines, 0, sizeof(g_grantLines));                                   \
        for (i = 0; i < 2; ++i) {                                                         \
            memset(&g_aGrantBases[i], 0, sizeof(g_aGrantBases[i]));                       \
            g_apGrantBases[i] = &g_aGrantBases[i];                                        \
            g_aGrantBases[i].bUnk4 = (u8)i;  /* the base id site->bFlag1 is matched on */ \
            g_aGrantBases[i].bSlot = (u8)i;  /* its column in auPerBaseVal */             \
        }                                                                                 \
        for (i = 0; i < 4; ++i) {                                                         \
            g_apGrantCraft[i] = &g_aGrantCraft[i];                                        \
        }                                                                                 \
        for (i = 0; i < 150; ++i) {                                                       \
            g_apGrantObj[i] = &g_aGrantObj[i];                                            \
            g_aGrantObj[i].bCargoSize = 1;  /* cargo volume == quantity */                \
        }                                                                                 \
        memset(g_apGrantSoldiers, 0, sizeof(g_apGrantSoldiers));                          \
        g_apGrantSoldiers[0] = &g_aGrantSoldier;                                          \
        g_aGrantSoldier.nUnk440 = 11;                                                     \
        g_aGrantSoldier.nKind = 9;                                                        \
        g_dwGrantShortfall = 0;                                                           \
        g_nGrantBaseCount = 2;                                                            \
        g_nGrantCraftCount = 0;                                                           \
        g_nGrantSiteCount = 0;                                                            \
        g_nGrantSoldierCount = 1;                                                         \
        g_nGrantNextSoldierId = 100;                                                      \
        g_nGrantNextCraftId = 7;                                                          \
        g_nGrantSamples = 0;                                                              \
    } while (0)

#define GRANT_ENV(env)                                        \
    memset(&(env), 0, sizeof(env));                           \
    (env).aSites = g_aGrantSites;                             \
    (env).pnSiteCount = &g_nGrantSiteCount;                   \
    (env).abFacilityGrid = g_aGrantGrid;                      \
    (env).pbBaseCount = &g_nGrantBaseCount;                   \
    (env).apBases = g_apGrantBases;                           \
    (env).apCraft = g_apGrantCraft;                           \
    (env).pnCraftCount = &g_nGrantCraftCount;                 \
    (env).apSoldiers = g_apGrantSoldiers;                     \
    (env).pnSoldierCount = &g_nGrantSoldierCount;             \
    (env).apObjects = g_apGrantObj;                           \
    (env).nObjectCount = 150;                                 \
    (env).pLines = &g_grantLines;                             \
    (env).pdwShortfall = &g_dwGrantShortfall;                 \
    (env).pUnitEnv = &g_grantUnitEnv;                         \
    (env).pShipEnv = &g_grantShipEnv;                         \
    (env).pfnPlaySample = GrantSample;                        \
    (env).pSoundUser = NULL

static void TestGrantSiteRecovery(void)
{
    SmapGrantRecoveryEnv env;
    i32 i;

    printf("Smap_GrantSiteRecovery @ 0x98b50\n");

    PtrArray_ctor(&g_grantStratList);
    g_grantCtorEnv.apObjects = g_apGrantObj;
    g_grantCtorEnv.nObjectCount = 150;
    g_grantUnitEnv.apSoldiers = g_apGrantSoldiers;
    g_grantUnitEnv.pnSoldierCount = &g_nGrantSoldierCount;
    g_grantUnitEnv.pnNextSoldierId = &g_nGrantNextSoldierId;
    g_grantShipEnv.apCraft = g_apGrantCraft;
    g_grantShipEnv.pnCraftCount = &g_nGrantCraftCount;
    g_grantShipEnv.pnNextCraftId = &g_nGrantNextCraftId;
    g_grantShipEnv.apBases = g_apGrantBases;
    g_grantShipEnv.abFacilityGrid = g_aGrantGrid;
    g_grantShipEnv.pCtorEnv = &g_grantCtorEnv;
    g_grantShipEnv.pStrategList = &g_grantStratList;
    g_grantShipEnv.pnStrategSeed = &g_nGrantSeed;
    g_grantShipEnv.pnZoomMode = &g_nGrantZoom;

    /* A null environment or a null site array stops the whole thing, aging included. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag0 = 3;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(NULL);
    env.aSites = NULL;
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_aGrantSites[0].bFlag0, 3, "a null environment or a null site array ages nothing");
    CheckEqI32(g_nGrantSiteCount, 1, "and drops nothing");

    /* Phase 1 ages every countdown, phase 2 delivers the sites that reach zero, and phase 3 drops
     * them. The site that is still counting survives, shifted down to index 0. */
    GRANT_RESET();
    g_nGrantSiteCount = 3;
    g_aGrantSites[0].bFlag0 = 1;
    g_aGrantSites[0].bFlag1 = 0;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[1].bFlag0 = 2;
    g_aGrantSites[1].bFlag1 = 1;
    g_aGrantSites[2].bFlag0 = 0;
    g_aGrantSites[2].bFlag1 = 0;
    g_aGrantSites[2].bHasLoot = 1;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_nGrantSiteCount, 1, "the two delivered sites are dropped from the count");
    CheckEqI32(g_aGrantSites[0].bFlag1, 1, "the survivor is the site still counting down");
    CheckEqI32(g_aGrantSites[0].bFlag0, 1, "and it was aged by one");
    CheckEqI32((i32)g_grantLines.nLineCount, 0,
               "a site with no cargo and no stores cells posts nothing (0 > 0 is not over budget)");
    CheckEqI32((i32)g_dwGrantShortfall, 0, "and the shortfall dword stays clear");

    /* The stores gate: cargo over a base with no stores cells raises the shortfall and message 42,
     * and the item loop still adds the cargo afterwards. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 1;   /* base 1, by bUnk4 */
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].auItemQty[1] = 5;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32((i32)g_dwGrantShortfall, 1, "cargo over a base with no stores cells raises the shortfall");
    CheckEqI32((i32)g_grantLines.nLineCount, 1, "one message line");
    CheckEqI32(g_grantLines.abLineFlags[0], 42, "message 42");
    CheckEqI32((i32)g_grantLines.anColor[0][0], 1, "named with the base the lookup found");
    CheckEqI32(g_aGrantObj[1].auPerBaseVal[1], 5, "and the cargo is delivered into that base's column");

    /* No stores cells but a 300-cell budget: the same cargo now fits and nothing is posted. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 1;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].auItemQty[1] = 5;
    GFAC(1, PUFO_GRID_FAC_TYPE_OFF, 0, 0) = FAC_GENERAL_STORES;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32((i32)g_grantLines.nLineCount, 0, "5 cargo inside a 300-cell budget posts nothing");
    CheckEqI32((i32)g_dwGrantShortfall, 0, "and leaves the shortfall clear");

    /* bHasLoot clear is the unconditional report 41, shortfall included. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 0;
    g_aGrantSites[0].bHasLoot = 0;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32((i32)g_grantLines.nLineCount, 1, "a site without loot posts one line");
    CheckEqI32(g_grantLines.abLineFlags[0], 41, "message 41");
    CheckEqI32((i32)g_dwGrantShortfall, 1, "and raises the shortfall unconditionally");

    /* A base lookup miss (bFlag1 matches no bUnk4) is the port's bounds deviation: the site is
     * still marked delivered and dropped, but nothing is delivered through it. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 77;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].auItemQty[1] = 9;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_nGrantSiteCount, 0, "a site that matches no base is still dropped");
    CheckEqI32(g_aGrantObj[1].auPerBaseVal[0], 0, "and nothing is delivered through it");

    /* Containment: with no containment cells the cap is 0, so an alien item is remapped to its
     * corpse slot with the source slot zeroed, and message 36 is posted. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 0;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].auItemQty[OBJ_SECTOID_SOLDIER] = 3;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_grantLines.abLineFlags[0], 36, "an alien over the containment cap posts message 36");
    CheckEqI32(g_aGrantSites[0].auItemQty[OBJ_SECTOID_SOLDIER], 0, "and the source slot is zeroed");
    CheckEqI32(g_aGrantObj[OBJ_SECTOID_CORPSE].auPerBaseVal[0], 3,
               "the quantity lands on the Sectoid corpse slot");
    CheckEqI32(g_aGrantObj[OBJ_SECTOID_SOLDIER].auPerBaseVal[0], 0, "not on the soldier slot");

    /* The containment counter is function-wide and counts non-zero alien SLOTS, not quantity: one
     * containment cell is worth 10, the first site uses all ten of them, and the second site's
     * single alien slot is the eleventh and goes over. */
    GRANT_RESET();
    g_nGrantSiteCount = 2;
    GFAC(0, PUFO_GRID_FAC_TYPE_OFF, 0, 0) = FAC_ALIEN_CONTAINMENT;
    g_aGrantSites[0].bFlag1 = 0;
    g_aGrantSites[0].bHasLoot = 1;
    for (i = OBJ_SECTOID_SOLDIER; i <= OBJ_SNAKEMAN_COMMANDER; ++i) {
        g_aGrantSites[0].auItemQty[i] = 1;
    }
    g_aGrantSites[1].bFlag1 = 0;
    g_aGrantSites[1].bHasLoot = 1;
    g_aGrantSites[1].auItemQty[OBJ_FLOATER_SOLDIER] = 5;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_aGrantObj[OBJ_SECTOID_SOLDIER].auPerBaseVal[0], 1, "the first site's first alien fits");
    CheckEqI32(g_aGrantObj[OBJ_SNAKEMAN_COMMANDER].auPerBaseVal[0], 1,
               "and so does its tenth, filling the cell's ten slots");
    CheckEqI32(g_aGrantObj[OBJ_FLOATER_SOLDIER].auPerBaseVal[0], 0,
               "the second site's alien is the eleventh and is not delivered as itself");
    CheckEqI32(g_aGrantObj[OBJ_FLOATER_CORPSE].auPerBaseVal[0], 5,
               "it arrives whole as a Floater corpse instead");
    CheckEqI32((i32)g_grantLines.nLineCount, 1, "one message for the one overflow");
    CheckEqI32(g_grantLines.abLineFlags[0], 36, "message 36");

    /* The corpse map, through the remap: the medics land on their own race's corpse, 114 is the
     * Ethereal navigator, and 102 is the Silacoid. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 0;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].auItemQty[OBJ_SECTOID_MEDIC] = 2;
    g_aGrantSites[0].auItemQty[OBJ_SNAKEMAN_MEDIC] = 1;
    g_aGrantSites[0].auItemQty[OBJ_FLOATER_MEDIC] = 4;
    g_aGrantSites[0].auItemQty[OBJ_ETHEREAL_NAVIGATOR] = 6;
    g_aGrantSites[0].auItemQty[OBJ_SILACOID] = 7;
    g_aGrantSites[0].auItemQty[OBJ_SECTOPOD] = 8;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_aGrantObj[OBJ_SECTOID_CORPSE].auPerBaseVal[0], 2, "Sectoid medic (148) -> Sectoid corpse");
    CheckEqI32(g_aGrantObj[OBJ_SNAKEMAN_CORPSE].auPerBaseVal[0], 1, "Snakeman medic (144) -> Snakeman corpse");
    CheckEqI32(g_aGrantObj[OBJ_FLOATER_CORPSE].auPerBaseVal[0], 4, "Floater medic (146) -> Floater corpse");
    CheckEqI32(g_aGrantObj[OBJ_ETHEREAL_CORPSE].auPerBaseVal[0], 6, "Ethereal navigator (114) -> Ethereal corpse");
    CheckEqI32(g_aGrantObj[OBJ_SILACOID_CORPSE].auPerBaseVal[0], 7, "Silacoid (102) -> Silacoid corpse");
    CheckEqI32(g_aGrantObj[OBJ_SECTOPOD_REMAINS].auPerBaseVal[0], 8, "Sectopod (104) -> Sectopod remains");

    /* Slots 117..121 are never added to the stores; they spawn craft instead. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 0;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].auItemQty[OBJ_CRAFT_COUNT_SKYRANGER] = 2;
    g_aGrantSites[0].auItemQty[OBJ_CRAFT_COUNT_INTERCEPTOR] = 1;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_aGrantObj[OBJ_CRAFT_COUNT_SKYRANGER].auPerBaseVal[0], 0,
               "the craft-count slots are not added to the stores");
    CheckEqI32(g_nGrantCraftCount, 3, "and they spawn craft instead (2 + 1)");
    CheckEqI32(g_apGrantCraft[0] != NULL ? g_apGrantCraft[0]->bCraftType : -1, CRAFT_TYPE_SKYRANGER,
               "slot 117 spawns Skyrangers");
    CheckEqI32(g_apGrantCraft[0] != NULL ? g_apGrantCraft[0]->bHomeBase : -1, 0,
               "homed at the site's base");
    CheckEqI32(g_apGrantCraft[2] != NULL ? g_apGrantCraft[2]->bCraftType : -1, CRAFT_TYPE_INTERCEPTOR,
               "slot 118 spawns Interceptors");
    for (i = 0; i < 3; ++i) {
        free(g_apGrantCraft[i]);
        g_apGrantCraft[i] = NULL;
    }

    /* Craft recovery: the craft whose bId matches anCraftIds is homed unconditionally, and the
     * hangar cell stamps its type glyph into layer2 at row+1, col+1. */
    GRANT_RESET();
    g_nGrantCraftCount = 1;
    g_aGrantCraft[0].bId = 5;
    g_aGrantCraft[0].bCraftType = CRAFT_TYPE_INTERCEPTOR;
    g_aGrantCraft[0].bHomeBase = 9;
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 1;   /* base 1 */
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].bRecoveredCraftCount = 1;
    g_aGrantSites[0].anCraftIds[0] = 5;
    GFAC(1, PUFO_GRID_FAC_TYPE_OFF, 2, 3) = FAC_HANGAR;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_aGrantCraft[0].bHomeBase, 1, "the matched craft is homed at the site's base");
    CheckEqI32(g_aGrantCraft[0].bHangarX, 4, "bHangarX is the matched column + 1");
    CheckEqI32(g_aGrantCraft[0].bHangarY, 3, "bHangarY is the matched row + 1");
    CheckEqI32(GFAC(1, PUFO_GRID_LAYER2_OFF, 3, 4), 36, "the Interceptor glyph 36 is stamped into layer2");

    /* The layer2 test reads the cell the stamp writes, so it follows the row: 0x99164..0x9917C is
     * `LDRB R3,[R3,#0x1C3]` (451 = 372 + 72 + 6 + 1) over the row advance `ADD R4,R4,#6` at 0x99210, and
     * 0x99314 is `STRB R2,[R3,#0x4F]` on that same cell. A hangar that already holds a craft is
     * therefore skipped: (2,0) is blocked by its OWN byte layer2[3][1], and the match becomes (2,3). */
    GRANT_RESET();
    g_nGrantCraftCount = 1;
    g_aGrantCraft[0].bId = 5;
    g_aGrantCraft[0].bCraftType = CRAFT_TYPE_INTERCEPTOR;
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 1;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].bRecoveredCraftCount = 1;
    g_aGrantSites[0].anCraftIds[0] = 5;
    GFAC(1, PUFO_GRID_FAC_TYPE_OFF, 2, 0) = FAC_HANGAR;
    GFAC(1, PUFO_GRID_FAC_TYPE_OFF, 2, 3) = FAC_HANGAR;
    GFAC(1, PUFO_GRID_LAYER2_OFF, 3, 1) = 1; /* layer2[2+1][0+1]: (2,0)'s own cell, already held */
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_aGrantCraft[0].bHangarX, 4, "the occupied hangar cell is skipped and the scan goes on");
    CheckEqI32(g_aGrantCraft[0].bHangarY, 3, "the match is the next hangar of the row");
    CheckEqI32(GFAC(1, PUFO_GRID_LAYER2_OFF, 3, 1), 1, "the occupant byte is left alone");
    CheckEqI32(GFAC(1, PUFO_GRID_LAYER2_OFF, 3, 4), 36, "the glyph lands on the matched cell's own byte");
    CheckEqI32(g_aGrantCraft[0].bHomeBase, 1, "while the homing still happened");

    /* A craft type outside 0..4 CONTINUES the scan here (Ship_SpawnAtSlot returns unstamped):
     * both hangar cells match, neither stamps, and the coordinates are the last match's. */
    GRANT_RESET();
    g_nGrantCraftCount = 1;
    g_aGrantCraft[0].bId = 5;
    g_aGrantCraft[0].bCraftType = 5;
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 1;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].bRecoveredCraftCount = 1;
    g_aGrantSites[0].anCraftIds[0] = 5;
    GFAC(1, PUFO_GRID_FAC_TYPE_OFF, 0, 0) = FAC_HANGAR;
    GFAC(1, PUFO_GRID_FAC_TYPE_OFF, 0, 1) = FAC_HANGAR;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_aGrantCraft[0].bHangarX, 2, "an unknown type keeps scanning to the next hangar cell");
    CheckEqI32(GFAC(1, PUFO_GRID_LAYER2_OFF, 1, 1), 0, "and stamps nothing");

    /* A buildDays cell that is still under construction blocks the hangar cell. */
    GRANT_RESET();
    g_nGrantCraftCount = 1;
    g_aGrantCraft[0].bId = 5;
    g_aGrantCraft[0].bCraftType = CRAFT_TYPE_SKYRANGER;
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 1;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].bRecoveredCraftCount = 1;
    g_aGrantSites[0].anCraftIds[0] = 5;
    GFAC(1, PUFO_GRID_FAC_TYPE_OFF, 0, 0) = FAC_HANGAR;
    GDAYS(1, 0, 0) = 3;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_aGrantCraft[0].bHangarX, 0, "a hangar cell still being built is skipped");

    /* Soldier recovery: uRecoveredSoldiers new soldiers, and bRecoveredSoldierAssign re-homes the
     * roster soldiers the site names by their roster id. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 0;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].uRecoveredSoldiers = 1;
    g_aGrantSites[0].bRecoveredSoldierAssign = 1;
    g_aGrantSites[0].anSoldierIds[0] = 11;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_nGrantSoldierCount, 2, "uRecoveredSoldiers adds a roster soldier");
    CheckEqI32(g_apGrantSoldiers[1] != NULL ? g_apGrantSoldiers[1]->nKind : -1, 0,
               "the new soldier is classified for the site's base");
    CheckEqI32(g_aGrantSoldier.nKind, 0, "and the named roster soldier is re-homed to that base");
    free(g_apGrantSoldiers[1]);
    g_apGrantSoldiers[1] = NULL;

    /* A roster id the array does not hold is the port's bounds deviation: nothing is written. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag1 = 0;
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].bRecoveredSoldierAssign = 1;
    g_aGrantSites[0].anSoldierIds[0] = 4242;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_aGrantSoldier.nKind, 9, "an unknown roster id leaves the roster alone");

    /* A run of delivered sites collapses in one pass: the extra record the shift copies is the
     * unused slot after the live tail, and it carries bFlag0 == 0. */
    GRANT_RESET();
    g_nGrantSiteCount = 4;
    for (i = 0; i < 3; ++i) {
        g_aGrantSites[i].bFlag1 = 0;
        g_aGrantSites[i].bHasLoot = 1;
    }
    g_aGrantSites[3].bFlag0 = 5;
    g_aGrantSites[3].bFlag1 = 1;
    GRANT_ENV(env);
    Smap_GrantSiteRecovery(&env);
    CheckEqI32(g_nGrantSiteCount, 1, "three delivered sites collapse in one call");
    CheckEqI32(g_aGrantSites[0].bFlag1, 1, "the survivor shifted down to index 0");
    CheckEqI32(g_aGrantSites[0].bFlag0, 4, "aged from 5 to 4");

    /* The delivery path's overflow report (0x99038): the base's column for the slot passes 60000
     * after the site's quantity is added, and the dump goes out with the base the site resolved to
     * and the slot number. */
    GRANT_RESET();
    g_nGrantSiteCount = 1;
    g_aGrantSites[0].bFlag0 = 0;                    /* delivered on this pass */
    g_aGrantSites[0].bFlag1 = 0;                    /* resolves to base 0 */
    g_aGrantSites[0].bHasLoot = 1;
    g_aGrantSites[0].auItemQty[10] = 1;
    g_aGrantObj[10].auPerBaseVal[0] = 60000;        /* the add takes it to 60001 */
    GRANT_ENV(env);
    env.pszAppDir = PUFO_TEST_DUMP_DIR;
    (void)DumpClean();
    Smap_GrantSiteRecovery(&env);
    {
        static char aDump[8192];
        char szName[256];
        i32 cb = DumpReadOne(aDump, sizeof(aDump), szName, sizeof(szName));

        CheckEqI32(g_aGrantObj[10].auPerBaseVal[0], 60001, "the delivered quantity lands on the base");
        Check(cb > 0, "  and the 60001 column writes the dump too (0x99038)");
        Check(cb > 0 && strstr(aDump, "problem with obj=10") != NULL, "  naming the slot");
        Check(cb > 0 && strstr(aDump, "(basenumber)current base=0") != NULL,
              "  against the base the site resolved to");
        Check(cb > 0 && strstr(aDump, "obj10=1") != NULL, "  with the site's own quantity line");
        CheckEqI32(DumpClean(), 1, "  one file");
    }
}

/* ManufScreen_RefreshBaseTabs @ 0x70ebc. Fixture: the engineers table auManufCol (200 u16 per
 * base), the accumulator auProductU32 (200 u32 per base), the three other product columns the
 * clear writes, 150 object records with bCargoSize 1, and the facility grid. */
static u16 g_aMfQty[200 * 8];
static u32 g_aMfA[200 * 8];
static i32 g_aMfB[200 * 8];
static u16 g_aMfC[200 * 8];
static u8 g_aMfGridBytes[8];
static u8 g_aMfGrid[1524];
static BaseRec g_aMfBases[2];
static BaseRec *g_apMfBases[2];
static ObjectRecFull g_aMfObj[150];
static ObjectRecFull *g_apMfObj[150];
static SmapSite g_aMfSites[2];
static UiLineList g_mfLines;
static u32 g_dwMfShortfall;
static u8 g_nMfBaseCount;
static i8 g_nMfSelBase;
static i32 g_nMfSamples;
static ManufClearEnv g_mfClear;
static ManufRefreshEnv g_mfEnv;

static void MfSample(i32 nSample, void *pUser)
{
    (void)nSample;
    (void)pUser;
    ++g_nMfSamples;
}

#define MFIDX(nBase, nProduct) (200 * (nBase) + (nProduct))
#define MFCAP(nBase) g_aMfGrid[0x174 + 144 * (nBase) + 108] /* fac[nBase].facType cell (0,0) */

#define MF_RESET()                                                                          \
    do {                                                                                    \
        memset(g_aMfQty, 0, sizeof(g_aMfQty));                                              \
        memset(g_aMfA, 0, sizeof(g_aMfA));                                                  \
        memset(g_aMfB, 0, sizeof(g_aMfB));                                                  \
        memset(g_aMfC, 0, sizeof(g_aMfC));                                                  \
        memset(g_aMfGridBytes, 0, sizeof(g_aMfGridBytes));                                  \
        memset(g_aMfGrid, 0, sizeof(g_aMfGrid));                                            \
        memset(g_aMfSites, 0, sizeof(g_aMfSites));                                          \
        memset(&g_mfLines, 0, sizeof(g_mfLines));                                           \
        for (i = 0; i < 2; ++i) {                                                           \
            memset(&g_aMfBases[i], 0, sizeof(g_aMfBases[i]));                               \
            g_apMfBases[i] = &g_aMfBases[i];                                                \
            g_aMfBases[i].bUnk4 = (u8)i;                                                    \
            g_aMfBases[i].bSlot = (u8)i;                                                    \
        }                                                                                   \
        for (i = 0; i < 150; ++i) {                                                         \
            g_apMfObj[i] = &g_aMfObj[i];                                                    \
            g_aMfObj[i].bCargoSize = 1;                                                     \
        }                                                                                   \
        g_dwMfShortfall = 0;                                                                \
        g_nMfBaseCount = 1;                                                                 \
        g_nMfSelBase = 0;                                                                   \
        g_nMfSamples = 0;                                                                   \
        memset(&g_mfClear, 0, sizeof(g_mfClear));                                           \
        g_mfClear.apProductA = g_aMfA;                                                      \
        g_mfClear.apProductB = g_aMfB;                                                      \
        g_mfClear.apProductC = g_aMfC;                                                      \
        g_mfClear.apManufCols = g_aMfQty;                                                   \
        g_mfClear.abGridBytes = g_aMfGridBytes;                                             \
        g_mfClear.apBases = g_apMfBases;                                                    \
        g_mfClear.nBaseCount = 1;                                                           \
        g_mfClear.pnSelectedBaseIdx = &g_nMfSelBase;                                        \
        memset(&g_mfEnv, 0, sizeof(g_mfEnv));                                               \
        g_mfEnv.apManufCols = g_aMfQty;                                                     \
        g_mfEnv.apProductA = g_aMfA;                                                        \
        g_mfEnv.apObjects = g_apMfObj;                                                      \
        g_mfEnv.nObjectCount = 150;                                                         \
        g_mfEnv.abFacilityGrid = g_aMfGrid;                                                 \
        g_mfEnv.pbBaseCount = &g_nMfBaseCount;                                              \
        g_mfEnv.apBases = g_apMfBases;                                                      \
        g_mfEnv.aSites = g_aMfSites;                                                        \
        g_mfEnv.nSiteCount = 0;                                                             \
        g_mfEnv.pdwShortfall = &g_dwMfShortfall;                                            \
        g_mfEnv.pLines = &g_mfLines;                                                        \
        g_mfEnv.pClearEnv = &g_mfClear;                                                     \
        g_mfEnv.pfnPlaySample = MfSample;                                                   \
    } while (0)

static void TestManufRefreshBaseTabs(void)
{
    i32 i;

    printf("ManufScreen_RefreshBaseTabs @ 0x70ebc\n");

    /* A product whose base has room takes its engineers into the accumulator column. */
    MF_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;   /* one stores cell: capacity 300 */
    g_aMfQty[MFIDX(0, 1)] = 5;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32((i32)g_aMfA[MFIDX(0, 1)], 5, "a product under capacity is added to its column");
    CheckEqI32((i32)g_mfLines.nLineCount, 0, "and nothing is reported");
    CheckEqI32((i32)g_dwMfShortfall, 0, "with the shortfall dword clear");

    /* A zero engineer count is skipped outright. */
    MF_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    g_aMfQty[MFIDX(0, 1)] = 0;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32((i32)g_aMfA[MFIDX(0, 1)], 0, "a zero quantity adds nothing");

    /* With no stores cells the capacity is 0, so an ordinary product is over budget: shortfall,
     * message 32 with the ARM's payload, and the slot cleared. The callee takes six arguments (a3
     * in R2, a4 in R3, a5 at [SP+0], a6 at [SP+4]) and 0x7107C/0x71084 put the BASE in both
     * registers while 0x7108C stores the product at [SP+0] and 0x71078 stores 0 at [SP+4] -- so the
     * payload is (32, base, base, product, 0). Base 1 and product 7 are used so a wrong order
     * cannot pass: the four slots hold 1, 1, 7 and 0. */
    MF_RESET();
    g_nMfBaseCount = 2;
    g_mfClear.nBaseCount = 2;
    g_aMfQty[MFIDX(1, 7)] = 5;
    g_aMfB[MFIDX(1, 7)] = 9;
    g_aMfC[MFIDX(1, 7)] = 9;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32((i32)g_dwMfShortfall, 1, "a product at zero capacity raises the shortfall");
    CheckEqI32(g_mfLines.abLineFlags[0], 32, "message 32");
    CheckEqI32((i32)g_mfLines.anColor[0][0], 1, "colour 0 is the base");
    CheckEqI32((i32)g_mfLines.anColor[0][1], 1, "colour 1 is the base again, exactly as the ARM stores it");
    CheckEqI32((i32)g_mfLines.anColor[0][2], 7, "colour 2 is the product");
    CheckEqI32((i32)g_mfLines.anColor[0][3], 0, "colour 3 is zero");
    CheckEqI32((i32)g_aMfQty[MFIDX(1, 7)], 0, "the product slot is cleared");
    CheckEqI32(g_aMfB[MFIDX(1, 7)], 0, "in every one of the four columns");
    CheckEqI32(g_aMfC[MFIDX(1, 7)], 0, "including the u16 column");

    /* The over-budget product ENDS the refresh: a later product with quantity is never reached. */
    MF_RESET();
    g_aMfQty[MFIDX(0, 1)] = 5;
    g_aMfQty[MFIDX(0, 2)] = 3;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32((i32)g_mfLines.nLineCount, 1, "one report for the first over-budget product");
    CheckEqI32((i32)g_aMfA[MFIDX(0, 2)], 0, "and the later product is never added");

    /* Products 119..121 skip the capacity test entirely, even at zero capacity. */
    MF_RESET();
    g_aMfQty[MFIDX(0, 120)] = 7;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32((i32)g_aMfA[MFIDX(0, 120)], 7, "product 120 is added with no capacity test");
    CheckEqI32((i32)g_mfLines.nLineCount, 0, "and reports nothing");
    CheckEqI32((i32)g_dwMfShortfall, 0, "with no shortfall");

    /* A product above 121 takes the tested path again. */
    MF_RESET();
    g_aMfQty[MFIDX(0, 130)] = 4;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32((i32)g_aMfA[MFIDX(0, 130)], 0, "product 130 is tested like an ordinary one");
    CheckEqI32(g_mfLines.abLineFlags[0], 32, "and is over budget at zero capacity");
    CheckEqI32((i32)g_mfLines.anColor[0][1], 0, "the base again in colour 1");
    CheckEqI32((i32)g_mfLines.anColor[0][2], 130, "and product 130 in colour 2");

    /* Base 1 reads and writes its own 200-entry slice. */
    MF_RESET();
    g_nMfBaseCount = 2;
    g_mfClear.nBaseCount = 2;
    MFCAP(1) = FAC_GENERAL_STORES;   /* base 1 has room, base 0 has none */
    g_aMfQty[MFIDX(1, 1)] = 6;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32((i32)g_aMfA[MFIDX(1, 1)], 6, "base 1's slice is added into");

    /* Base 0 first: with no capacity its product aborts the whole refresh before base 1 is seen. */
    MF_RESET();
    g_nMfBaseCount = 2;
    g_aMfQty[MFIDX(0, 1)] = 5;
    g_aMfQty[MFIDX(1, 1)] = 6;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32((i32)g_aMfA[MFIDX(1, 1)], 0, "an over-budget product in base 0 stops base 1 too");

    /* No bases: nothing runs. */
    MF_RESET();
    g_nMfBaseCount = 0;
    g_aMfQty[MFIDX(0, 1)] = 5;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32((i32)g_aMfA[MFIDX(0, 1)], 0, "a zero base count adds nothing");

    /* A missing table is refused. */
    MF_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    g_aMfQty[MFIDX(0, 1)] = 5;
    g_mfEnv.apProductA = NULL;
    ManufScreen_RefreshBaseTabs(&g_mfEnv);
    CheckEqI32(g_aMfQty[MFIDX(0, 1)], 5, "a missing accumulator table changes nothing");

}

/* --- ManufScreen_CommitProduction ------------------------------------------------------------
 *
 * The same four columns as the refresh test, plus the price table, the funds, and a ShipSpawnEnv
 * borrowed from the recovery fixture for the craft branch.
 */
static u32 g_aMfPrice[150];
static i32 g_nMfFunds;
static ManufCommitEnv g_mfCommit;

/* MF_RESET only re-arms bCargoSize, so the work cost and the component bytes have to be cleared
 * here or they leak from one case into the next and a component shortage appears out of nowhere. */
#define MC_RESET()                                                                          \
    do {                                                                                    \
        MF_RESET();                                                                         \
        memset(g_aMfObj, 0, sizeof(g_aMfObj));                                              \
        for (i = 0; i < 150; ++i) {                                                         \
            g_apMfObj[i] = &g_aMfObj[i];                                                   \
            g_aMfObj[i].bCargoSize = 1;                                                     \
        }                                                                                   \
        memset(g_aMfPrice, 0, sizeof(g_aMfPrice));                                          \
        for (i = 0; i < 150; ++i) {                                                         \
            g_aMfPrice[i] = 100;                                                            \
        }                                                                                   \
        g_nMfFunds = 1000000;                                                               \
        g_nMfSamples = 0;                                                                   \
        g_nMfSelBase = 0;                                                                   \
        g_nGrantCraftCount = 0;                                                             \
        g_nGrantNextCraftId = 7;                                                            \
        memset(g_apGrantCraft, 0, sizeof(g_apGrantCraft));                                  \
        memset(&g_mfCommit, 0, sizeof(g_mfCommit));                                         \
        g_mfCommit.apManufCols = g_aMfQty;                                                  \
        g_mfCommit.apProductA = g_aMfA;                                                     \
        g_mfCommit.apProductB = g_aMfB;                                                     \
        g_mfCommit.apProductC = g_aMfC;                                                     \
        g_mfCommit.pdwPrices = g_aMfPrice;                                                  \
        g_mfCommit.pnFunds = &g_nMfFunds;                                                   \
        g_mfCommit.apObjects = g_apMfObj;                                                   \
        g_mfCommit.nObjectCount = 150;                                                      \
        g_mfCommit.abFacilityGrid = g_aMfGrid;                                              \
        g_mfCommit.pbBaseCount = &g_nMfBaseCount;                                           \
        g_mfCommit.apBases = g_apMfBases;                                                   \
        g_mfCommit.aSites = g_aMfSites;                                                     \
        g_mfCommit.nSiteCount = 0;                                                          \
        g_mfCommit.pdwShortfall = &g_dwMfShortfall;                                         \
        g_mfCommit.pLines = &g_mfLines;                                                     \
        g_mfCommit.pClearEnv = &g_mfClear;                                                  \
        g_mfCommit.pShipEnv = &g_grantShipEnv;                                              \
        g_mfCommit.pfnPlaySample = MfSample;                                                \
    } while (0)

/* Product 20 and its 432-unit work cost are the shipped Motion scaner; the shipped Medi-kit is 816. */
#define MC_WORK(nProduct, nWork) (g_aMfObj[(nProduct)].uManufWorkPerUnit = (u16)(nWork))

static void TestManufCommitProduction(void)
{
    i32 i;

    printf("ManufScreen_CommitProduction @ 0x6f34c\n");

    PtrArray_ctor(&g_grantStratList);
    g_grantCtorEnv.apObjects = g_apGrantObj;
    g_grantCtorEnv.nObjectCount = 150;
    g_grantShipEnv.apCraft = g_apGrantCraft;
    g_grantShipEnv.pnCraftCount = &g_nGrantCraftCount;
    g_grantShipEnv.pnNextCraftId = &g_nGrantNextCraftId;
    g_grantShipEnv.apBases = g_apGrantBases;
    g_grantShipEnv.abFacilityGrid = g_aGrantGrid;
    g_grantShipEnv.pCtorEnv = &g_grantCtorEnv;
    g_grantShipEnv.pStrategList = &g_grantStratList;
    g_grantShipEnv.pnStrategSeed = &g_nGrantSeed;
    g_grantShipEnv.pnZoomMode = &g_nGrantZoom;

    /* Work that has paid for one unit and none committed yet: one item, one committed unit, and the
     * price charged at 1.4x (300 * 1.4 = 420). */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfPrice[20] = 300;
    g_nMfFunds = 10000;
    g_aMfQty[MFIDX(0, 20)] = 432;
    g_aMfA[MFIDX(0, 20)] = 432;
    g_aMfB[MFIDX(0, 20)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 1, "one unit lands in the base's column");
    CheckEqI32((i32)g_aMfC[MFIDX(0, 20)], 1, "one committed unit is recorded");
    CheckEqI32(g_nMfFunds, 9580, "the price is charged at 1.4 times");
    CheckEqI32((i32)g_mfLines.nLineCount, 0, "nothing is reported");
    CheckEqI32((i32)g_dwMfShortfall, 0, "with no shortfall");
    CheckEqI32((i32)g_aMfQty[MFIDX(0, 20)], 432, "the allocation stays until the target is met");

    /* 0x6F5E8: production needs paid-for units STRICTLY above the committed ones. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfQty[MFIDX(0, 20)] = 431;
    g_aMfA[MFIDX(0, 20)] = 431;
    g_aMfB[MFIDX(0, 20)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 0, "431 of 432 units of work produces nothing");
    CheckEqI32(g_nMfFunds, 1000000, "and nothing is spent");

    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfQty[MFIDX(0, 20)] = 864;
    g_aMfA[MFIDX(0, 20)] = 864;
    g_aMfC[MFIDX(0, 20)] = 2;
    g_aMfB[MFIDX(0, 20)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 0,
               "two paid units with two already committed produce nothing more");
    CheckEqI32((i32)g_aMfC[MFIDX(0, 20)], 2, "and the committed count stands");

    /* A zero allocation is skipped before any gate runs. */
    MC_RESET();
    MC_WORK(20, 432);
    g_aMfA[MFIDX(0, 20)] = 1000;
    g_aMfB[MFIDX(0, 20)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 0, "a zero allocation produces nothing");
    CheckEqI32((i32)g_mfLines.nLineCount, 0, "and reports nothing, gates included");

    /* Completion, 0x6F798: paid-for units EQUAL the target clears the slot and posts 34. Only ONE
     * unit is produced even though two are paid for -- one unit per call. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfQty[MFIDX(0, 20)] = 864;
    g_aMfA[MFIDX(0, 20)] = 864;
    g_aMfB[MFIDX(0, 20)] = 2;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 1, "one unit per call, not two");
    CheckEqI32((i32)g_aMfQty[MFIDX(0, 20)], 0, "the completion clears the allocation");
    CheckEqI32((i32)g_aMfA[MFIDX(0, 20)], 0, "the accumulated work");
    CheckEqI32((i32)g_aMfB[MFIDX(0, 20)], 0, "the target");
    CheckEqI32((i32)g_aMfC[MFIDX(0, 20)], 0, "and the committed count");
    CheckEqI32(g_mfLines.abLineFlags[0], 34, "message 34");
    CheckEqI32((i32)g_mfLines.anColor[0][0], 0, "colour 0 is the base");
    CheckEqI32((i32)g_mfLines.anColor[0][1], 0, "colour 1 is the base again");
    CheckEqI32((i32)g_mfLines.anColor[0][2], 20, "colour 2 is the product");
    CheckEqI32((i32)g_mfLines.anColor[0][3], 0, "colour 3 is zero");
    CheckEqI32((i32)g_dwMfShortfall, 1, "and the shortfall dword is raised");
    CheckEqI32(g_nMfSamples, 1, "with one sample played");

    /* Completion does NOT return: a later product in the same base still runs. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    MC_WORK(21, 816);
    g_aMfQty[MFIDX(0, 20)] = 432;
    g_aMfA[MFIDX(0, 20)] = 432;
    g_aMfB[MFIDX(0, 20)] = 1;
    g_aMfQty[MFIDX(0, 21)] = 816;
    g_aMfA[MFIDX(0, 21)] = 816;
    g_aMfB[MFIDX(0, 21)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[21].auPerBaseVal[0], 1,
               "a completed product does not stop the next one");
    CheckEqI32((i32)g_mfLines.nLineCount, 1, "and only the completed one is reported");

    /* Funds gate, 0x6F408: blocked, message 35, slot cleared, and the whole pass RETURNS. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfPrice[20] = 300;
    g_nMfFunds = 100;
    g_aMfQty[MFIDX(0, 20)] = 432;
    g_aMfA[MFIDX(0, 20)] = 432;
    g_aMfB[MFIDX(0, 20)] = 5;
    g_aMfQty[MFIDX(0, 21)] = 816;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32(g_mfLines.abLineFlags[0], 35, "message 35 for the funds gate");
    CheckEqI32((i32)g_mfLines.nLineCount, 1, "and message 33 is NOT added after it");
    CheckEqI32((i32)g_aMfQty[MFIDX(0, 20)], 0, "the blocked product's slot is cleared");
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 0, "with nothing produced");
    CheckEqI32(g_nMfFunds, 100, "and no money spent");
    CheckEqI32((i32)g_aMfQty[MFIDX(0, 21)], 816,
               "the blocked product ends the pass, so the next slot is untouched");

    /* Stores gate, 0x6F4D0: with no stores cells an ordinary product is strictly over capacity. */
    MC_RESET();
    MC_WORK(20, 432);
    g_aMfQty[MFIDX(0, 20)] = 432;
    g_aMfA[MFIDX(0, 20)] = 432;
    g_aMfB[MFIDX(0, 20)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32(g_mfLines.abLineFlags[0], 32, "message 32 for the stores gate");
    CheckEqI32((i32)g_aMfQty[MFIDX(0, 20)], 0, "and the slot is cleared");

    /* Component gate, 0x6F574: money and stores are fine but the base has no Elerium, so this is the
     * only gate that leaves the shortfall dword zero going in -- the one case that posts 33. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfObj[20].abManufComponents[0] = 2;   /* two Elerium-115, and the base has none */
    g_aMfQty[MFIDX(0, 20)] = 432;
    g_aMfA[MFIDX(0, 20)] = 432;
    g_aMfB[MFIDX(0, 20)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32(g_mfLines.abLineFlags[0], 33, "message 33, the blocked-with-no-other-shortfall case");
    CheckEqI32((i32)g_mfLines.anColor[0][2], 20, "naming the product in colour 2");
    CheckEqI32((i32)g_aMfQty[MFIDX(0, 20)], 0, "and the slot cleared");
    CheckEqI32((i32)g_dwMfShortfall, 1, "with the shortfall raised by the 33 itself");

    /* Components are consumed from the four stocks when a unit is produced. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfObj[20].abManufComponents[0] = 2;
    g_aMfObj[20].abManufComponents[1] = 1;
    g_aMfObj[105].auPerBaseVal[0] = 5;
    g_aMfObj[106].auPerBaseVal[0] = 3;
    g_aMfObj[107].auPerBaseVal[0] = 1;
    g_aMfObj[109].auPerBaseVal[0] = 1;
    g_aMfQty[MFIDX(0, 20)] = 432;
    g_aMfA[MFIDX(0, 20)] = 432;
    g_aMfB[MFIDX(0, 20)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 1, "the product is produced");
    CheckEqI32((i32)g_aMfObj[105].auPerBaseVal[0], 3, "two Elerium-115 consumed");
    CheckEqI32((i32)g_aMfObj[106].auPerBaseVal[0], 2, "one Alien alloys consumed");
    CheckEqI32((i32)g_aMfObj[107].auPerBaseVal[0], 1, "the untouched power source stands");
    CheckEqI32((i32)g_aMfObj[109].auPerBaseVal[0], 1, "and so does the navigation");

    /* A stock exactly equal to the requirement is enough: 0x6F588 blocks only when stock < req. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfObj[20].abManufComponents[0] = 4;
    g_aMfObj[105].auPerBaseVal[0] = 4;
    g_aMfQty[MFIDX(0, 20)] = 432;
    g_aMfA[MFIDX(0, 20)] = 432;
    g_aMfB[MFIDX(0, 20)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_mfLines.nLineCount, 0, "a stock equal to the requirement is not a shortage");
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 1, "and the unit is produced");

    /* Craft branch, 0x6F5F4..0x6F658: product 119 is FIRE STORM, it skips the stores gate even at
     * zero capacity, and it spawns a CraftType-4 craft homed at the base being processed. */
    MC_RESET();
    MC_WORK(119, 27984);
    g_aMfObj[119].abManufComponents[1] = 65;  /* 65 Alien alloys, the shipped value */
    g_aMfObj[106].auPerBaseVal[0] = 70;
    g_aMfQty[MFIDX(0, 119)] = 27984;
    g_aMfA[MFIDX(0, 119)] = 27984;
    g_aMfB[MFIDX(0, 119)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_nGrantCraftCount, 1, "a craft is spawned with no stores cells at all");
    Check(g_apGrantCraft[0] != NULL && g_apGrantCraft[0]->bCraftType == CRAFT_TYPE_FIRESTORM,
          "product 119 spawns a Firestorm");
    Check(g_apGrantCraft[0] != NULL && g_apGrantCraft[0]->bHomeBase == 0,
          "homed at the base being processed");
    CheckEqI32((i32)g_aMfObj[106].auPerBaseVal[0], 5, "and its 65 alloys are consumed");
    CheckEqI32((i32)g_mfLines.nLineCount, 0, "with nothing reported");

    /* The craft branch decrements the SELECTED base's grid byte, not the processed base's. */
    MC_RESET();
    MC_WORK(120, 35712);
    g_aMfObj[120].abManufComponents[1] = 1;
    g_aMfObj[106].auPerBaseVal[0] = 5;
    g_aMfGridBytes[0] = 3;                    /* base 0's grid byte, base 0 has type byte 0 */
    g_nMfSelBase = 0;
    g_aMfQty[MFIDX(0, 120)] = 35712;
    g_aMfA[MFIDX(0, 120)] = 35712;
    g_aMfB[MFIDX(0, 120)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfGridBytes[0], 2, "the selected base's grid byte is decremented by one");
    Check(g_apGrantCraft[0] != NULL && g_apGrantCraft[0]->bCraftType == CRAFT_TYPE_LIGHTNING,
          "product 120 spawns a Lightning");

    /* Products 117 and 118 are outside the craft range (0x6F5F4 / 0x6F5FC), so they take the item
     * branch even though Ship_ProductSlotToType knows them. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(117, 100);
    g_aMfQty[MFIDX(0, 117)] = 100;
    g_aMfA[MFIDX(0, 117)] = 100;
    g_aMfB[MFIDX(0, 117)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_nGrantCraftCount, 0, "product 117 spawns no craft");
    CheckEqI32((i32)g_aMfObj[117].auPerBaseVal[0], 1, "it is added as an ordinary item instead");

    /* Deviation: a zero work cost. The original divides through the external COREDLL helper with a
     * zero divisor; the port answers zero units, so nothing is produced. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    g_aMfQty[MFIDX(0, 20)] = 100;
    g_aMfA[MFIDX(0, 20)] = 100;
    g_aMfB[MFIDX(0, 20)] = 5;   /* a zero target would equal the zero quotient and read as finished,
                                 * so the target has to be a real one to test the divisor alone */
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 0, "a product with no work cost produces nothing");
    CheckEqI32((i32)g_mfLines.nLineCount, 0, "and is not reported as finished");

    /* Base 1 reads and writes its own slice, and its own per-base component stocks. */
    MC_RESET();
    g_nMfBaseCount = 2;
    g_mfClear.nBaseCount = 2;
    MFCAP(1) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfObj[20].abManufComponents[0] = 1;
    g_aMfObj[105].auPerBaseVal[1] = 2;
    g_aMfQty[MFIDX(1, 20)] = 432;
    g_aMfA[MFIDX(1, 20)] = 432;
    g_aMfB[MFIDX(1, 20)] = 5;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[1], 1, "base 1 produces into its own column");
    CheckEqI32((i32)g_aMfObj[105].auPerBaseVal[1], 1, "and consumes from its own stock");
    CheckEqI32((i32)g_aMfObj[105].auPerBaseVal[0], 0, "base 0's stock is untouched");

    /* A zero base count does nothing. */
    MC_RESET();
    g_nMfBaseCount = 0;
    g_aMfQty[MFIDX(0, 20)] = 432;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_mfLines.nLineCount, 0, "a zero base count produces nothing");

    /* A missing table is refused. */
    MC_RESET();
    MFCAP(0) = FAC_GENERAL_STORES;
    MC_WORK(20, 432);
    g_aMfQty[MFIDX(0, 20)] = 432;
    g_aMfA[MFIDX(0, 20)] = 432;
    g_mfCommit.apProductC = NULL;
    ManufScreen_CommitProduction(&g_mfCommit);
    CheckEqI32((i32)g_aMfObj[20].auPerBaseVal[0], 0, "a missing committed-count table changes nothing");
}

/* Stubs for the bring-up. Each handler gets its own counter so a wrong slot is visible, and the
 * panel builder records the first control's x at the moment it runs: the original tail-calls
 * ManufScreen_BuildPanels last (0x71268), so the controls must already be positioned there. That
 * is the opposite of Ui_InitTransfer, which builds the panels before the controls exist. */
static UiControl g_aManufInitCtl[6], g_manufClose, g_manufDraw, g_manufConfirm;
static i32 g_nManufStub[10];
static i32 g_nBuildCtlX;

static void ManufStubClose(void)       { ++g_nManufStub[0]; }
static void ManufStubDraw(void)        { ++g_nManufStub[1]; }
static void ManufStubConfirm(void)     { ++g_nManufStub[2]; }
static void ManufStubPanelPrev(void)   { ++g_nManufStub[3]; }
static void ManufStubPanelNext(void)   { ++g_nManufStub[4]; }
static void ManufStubIncQty(void)      { ++g_nManufStub[5]; }
static void ManufStubDecQty(void)      { ++g_nManufStub[6]; }
static void ManufStubIncUnits(void)      { ++g_nManufStub[7]; }
static void ManufStubDecUnits(void)      { ++g_nManufStub[8]; }
static void ManufStubBuildPanels(void) { ++g_nManufStub[9]; g_nBuildCtlX = g_aManufInitCtl[0].x; }

/* The pad seam runs for real here: the archives are open, so the descriptor names either resolve
 * to members of main.dat or the pad slot stays empty. That is what makes the six descriptor pad
 * names a checked claim instead of a transcription. */
static bool ManufLoadPadsReal(UiControl *pCtl, const char *psz0, const char *psz1,
                              const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2);
    return true;
}

static void ManufFreeCtlPads(void)
{
    for (i32 c = 0; c < 6; ++c) {
        for (i32 p = 0; p < PUFO_CONTROL_PADS; ++p) {
            if (g_aManufInitCtl[c].apPads[p] != NULL) {
                PadImage_FreeData(g_aManufInitCtl[c].apPads[p]);
                free(g_aManufInitCtl[c].apPads[p]);
                g_aManufInitCtl[c].apPads[p] = NULL;
            }
        }
    }
}

static void TestManufInit(void)
{
    static void *aRoot[7];
    ManufInitEnv env;
    PadImage *pPad;
    const char *const apszArch[2] = { "lang.dat", "main.dat" };
    i32 i, nLoaded, nEmpty;

    printf("the manufacturing screen's bring-up\n");

    if (!Asset_OpenArchives(apszArch)) {
        printf("  FAIL main.dat / lang.dat did not open\n");
        ++g_nFailures;
        return;
    }

    memset(aRoot, 0, sizeof(aRoot));
    memset(g_aManufInitCtl, 0, sizeof(g_aManufInitCtl));
    memset(&g_manufClose, 0, sizeof(g_manufClose));
    memset(&g_manufDraw, 0, sizeof(g_manufDraw));
    memset(&g_manufConfirm, 0, sizeof(g_manufConfirm));
    memset(g_nManufStub, 0, sizeof(g_nManufStub));
    g_nBuildCtlX = -1;

    memset(&env, 0, sizeof(env));
    env.ppRoot = aRoot;
    for (i = 0; i < 6; ++i) {
        env.apCtl[i] = &g_aManufInitCtl[i];
    }
    env.pCloseCtl = &g_manufClose;
    env.pDrawCtl = &g_manufDraw;
    env.pConfirmCtl = &g_manufConfirm;
    env.pfnCloseClick = ManufStubClose;
    env.pfnDrawClick = ManufStubDraw;
    env.pfnConfirmClick = ManufStubConfirm;
    env.pfnPanelPrev = ManufStubPanelPrev;
    env.pfnPanelNext = ManufStubPanelNext;
    env.pfnIncQty = ManufStubIncQty;
    env.pfnDecQty = ManufStubDecQty;
    env.pfnIncUnits = ManufStubIncUnits;
    env.pfnDecUnits = ManufStubDecUnits;
    env.pfnBuildPanels = ManufStubBuildPanels;
    env.pfnLoadPads = ManufLoadPadsReal;

    ManufScreen_Init(&env);

    /* Step 1: the screen pad. The two origin stores at 0x710F8 / 0x7110C are not observable here
     * because the port's allocation already zeroes the block, so they are recorded in the header
     * rather than asserted. */
    Check(aRoot[0] != NULL, "the screen's pad object is allocated");
    pPad = (PadImage *)aRoot[0];
    Check(pPad != NULL && pPad->pData != NULL, "manuf.pad really loads out of main.dat");
    Check(pPad != NULL && pPad->uWidth == 240 && pPad->uHeight == 320,
          "manuf.pad is the 240 x 320 member (76804 bytes with its 4-byte header)");
    Check(pPad != NULL && pPad->nMode == 7,
          OptCite("loaded in mode 7, the MOV R2,#7 at 0x710DC", "@ 0x710b0"));

    /* Step 3: the six slots. */
    for (i = 0; i < 6; ++i) {
        Check(aRoot[i + 1] == (void *)&g_aManufInitCtl[i],
              OptCite("the six controls land in the root array's slots 1..6", "@ 0x710b0"));
    }

    /* Step 4: the handlers, in slot order. ManufScreen_Init @ 0x710b0 is 488 bytes = 102 instructions (6 of them
     * 2-word MOV immediates) + 14 pool words, and the order below is the order 0x71184..0x711EC stores them. */
    Check(g_aManufInitCtl[0].pfnClick == ManufStubPanelPrev,
          OptCite("slot 1 scrolls the panels back", "@ 0x710b0"));
    Check(g_aManufInitCtl[1].pfnClick == ManufStubPanelNext,
          OptCite("slot 2 scrolls them forward", "@ 0x710b0"));
    Check(g_aManufInitCtl[2].pfnClick == ManufStubIncQty, OptCite("slot 3 is the quantity +", "@ 0x710b0"));
    Check(g_aManufInitCtl[3].pfnClick == ManufStubDecQty, OptCite("slot 4 is the quantity -", "@ 0x710b0"));
    Check(g_aManufInitCtl[4].pfnClick == ManufStubIncUnits, OptCite("slot 5 is the units +", "@ 0x710b0"));
    Check(g_aManufInitCtl[5].pfnClick == ManufStubDecUnits, OptCite("slot 6 is the units -", "@ 0x710b0"));

    /* Step 6: the positions come straight out of g_aManufCtlDesc, both bases being zero. */
    CheckEqI32(g_aManufInitCtl[0].x, 0xCD, "the tab scroll pair sits at x 0xCD");
    CheckEqI32(g_aManufInitCtl[0].y, 0x12, "the up arrow at y 0x12");
    CheckEqI32(g_aManufInitCtl[1].y, 0x26, "the down arrow 20 pixels below it");
    CheckEqI32(g_aManufInitCtl[1].x, 0xCD, "at the same x");
    CheckEqI32(g_aManufInitCtl[2].x, 0x0A, "the quantity pair at x 0x0A");
    CheckEqI32(g_aManufInitCtl[2].y, 0xFB, "the quantity + at y 0xFB");
    CheckEqI32(g_aManufInitCtl[3].x, 0x0A, "the quantity - under it");
    CheckEqI32(g_aManufInitCtl[3].y, 0x10F, "at y 0x10F");
    CheckEqI32(g_aManufInitCtl[4].x, 0x8C, "the engineers pair at x 0x8C");
    CheckEqI32(g_aManufInitCtl[4].y, 0xFB, "the engineers + at y 0xFB");
    CheckEqI32(g_aManufInitCtl[5].x, 0x8C, "the engineers - under it");
    CheckEqI32(g_aManufInitCtl[5].y, 0x10F, "at y 0x10F");
    for (i = 0; i < 6; ++i) {
        CheckEqI32(g_aManufInitCtl[i].nMode, 0, "every one of the six is a mode-0 button");
    }

    /* The pads the descriptors name are real members of main.dat: up / sup / down / sdown are each
     * 23 x 17, and the empty third name gets no pad at all. */
    nLoaded = 0;
    nEmpty = 0;
    for (i = 0; i < 6; ++i) {
        if (g_aManufInitCtl[i].apPads[0] != NULL && g_aManufInitCtl[i].apPads[1] != NULL &&
            g_aManufInitCtl[i].apPads[0]->pData != NULL &&
            g_aManufInitCtl[i].apPads[0]->uWidth == 23 &&
            g_aManufInitCtl[i].apPads[0]->uHeight == 17 &&
            g_aManufInitCtl[i].apPads[1]->pData != NULL &&
            g_aManufInitCtl[i].apPads[0]->nMode == 1 &&
            g_aManufInitCtl[i].apPads[1]->nMode == 1) {
            ++nLoaded;
        }
        if (g_aManufInitCtl[i].apPads[2] == NULL) {
            ++nEmpty;
        }
    }
    CheckEqI32(nLoaded, 6, "up/sup and down/sdown load for all six, in UiControl_LoadPads' mode 1");
    CheckEqI32(nEmpty, 6, "the empty third pad name gets no pad");

    /* Step 2 and step 5: the three controls that already exist as globals. */
    CheckEqI32(g_manufClose.x, 224, OptCite("the close control lands at 224, 1 (0x71104/0x71110)", "@ 0x710b0"));
    CheckEqI32(g_manufClose.y, 1, OptCite("as 16-bit stores", "@ 0x710b0"));
    Check(g_manufClose.pfnClick == ManufStubClose, OptCite("with the close thunk", "@ 0x710b0"));
    CheckEqI32(g_manufDraw.x, 40, OptCite("the draw control at 40, 0x126 (0x711F0)", "@ 0x710b0"));
    CheckEqI32(g_manufDraw.y, 0x126, OptCite("bottom left of the screen", "@ 0x710b0"));
    Check(g_manufDraw.pfnClick == ManufStubDraw, OptCite("with the screen drawer as its handler", "@ 0x710b0"));
    CheckEqI32(g_manufConfirm.x, 130, "the confirm control at 130, 0x126");
    CheckEqI32(g_manufConfirm.y, 0x126, "to its right");
    Check(g_manufConfirm.pfnClick == ManufStubConfirm, "with confirm-and-close");

    /* Step 7: the panel build is the last thing the function does. */
    CheckEqI32(g_nManufStub[9], 1, "the panels are built once");
    CheckEqI32(g_nBuildCtlX, 0xCD,
               "and only after the six controls are positioned, unlike the transfer bring-up");

    ManufFreeCtlPads();
    free(aRoot[0]);
    aRoot[0] = NULL;

    ManufScreen_Init(NULL);
    env.ppRoot = NULL;
    ManufScreen_Init(&env);
    env.ppRoot = aRoot;
    Check(aRoot[0] == NULL, OptCite("a missing root array makes it do nothing at all", "@ 0x710b0"));
    CheckEqI32(g_nManufStub[9], 1, OptCite("including the panel build (0x71268 tail call)", "@ 0x710b0"));
    Check(g_aManufInitCtl[0].pfnClick == ManufStubPanelPrev,
          OptCite("and no handler is rewired", "@ 0x710b0"));

    Asset_CloseArchives();
}

static void TestManufBuildPanels(void)
{
    static ResearchTopicLive aTopic[4];
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static u16 aManufCol[1600];
    static u32 aProdA[1600];
    static i32 aProdB[1600];
    static UiControl aCtl[6];
    static void *aRoot[7];
    UiPanel *pA = NULL, *pB = NULL, *pC = NULL, *pD = NULL;
    ManufBuildEnv env;
    u8 bBuilt = 0;
    u8 bTopicCount = 4;
    i32 i;

    printf("the manufacturing panel build and teardown\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    /* Names chosen to show the 17-character cut at 0x6EF18: 11 stays, 17 stays, 18 and 25 are cut. */
    PutStr(aObj[2].szName,   "Laser pistol");
    PutStr(aObj[3].szName,   "PlasmaPistol12345");           /* 17, stays whole */
    PutStr(aObj[107].szName, "UFOPowerSource1234");          /* 18 -> "UFOPowerSource123" */
    PutStr(aObj[120].szName, "LIGHTNINGCraftNameTwenty");    /* 25 -> "LIGHTNINGCraftNam" */
    PutStr(aObj[108].szName, "Alien surgery");
    aObj[2].uManufWorkPerUnit   = 576;    /* the shipped values */
    aObj[3].uManufWorkPerUnit   = 1152;
    aObj[107].uManufWorkPerUnit = 2544;
    aObj[120].uManufWorkPerUnit = 0;      /* shipped 35712; zeroed here to reach the zero-divisor path,
                                           * which the shipped data never hits because every slot
                                           * abManufRowFlag flags has a work cost */
    aObj[108].uManufWorkPerUnit = 0;      /* and abManufRowFlag leaves 108 clear, so it gets no row */

    /* Topic 0 is never visited: the walk starts one record in, at 0x15EF2A. */
    aTopic[0].bUnlocked = 1;
    aTopic[0].abItemIds[0] = 2;
    aTopic[1].bUnlocked = 1;
    aTopic[1].abItemIds[0] = 2;    /* flagged -> row */
    aTopic[1].abItemIds[1] = 108;  /* Alien surgery: abManufRowFlag leaves it clear, so no row */
    aTopic[1].abItemIds[2] = 3;    /* flagged -> row */
    aTopic[2].bUnlocked = 0;       /* 5, 6 and 9 are all flagged, so this proves the +0xD gate */
    aTopic[2].abItemIds[0] = 5;
    aTopic[2].abItemIds[1] = 6;
    aTopic[2].abItemIds[2] = 9;
    aTopic[3].bUnlocked = 1;
    aTopic[3].abItemIds[0] = 107;
    aTopic[3].abItemIds[1] = 120;
    aTopic[3].abItemIds[2] = 107;  /* the same slot twice: nothing deduplicates rows */

    /* Base 1, so every product index is 200 + slot. */
    aManufCol[200 + 2]   = 100;
    aManufCol[200 + 3]   = 0;
    aManufCol[200 + 107] = 7;
    aManufCol[200 + 120] = 3;
    aProdA[200 + 2]   = 576u * 3u;   /* 1728 -> 3 units, 100*1728/(576*4) = 75 */
    aProdB[200 + 2]   = 4;
    aProdA[200 + 3]   = 1152;
    aProdB[200 + 3]   = 0;           /* nothing asked for: both remaining columns get the empty string */
    aProdA[200 + 107] = 0;
    aProdB[200 + 107] = 1;
    aProdA[200 + 120] = 10;
    aProdB[200 + 120] = 5;           /* with work 0 this is the zero-divisor row: 0% and 5/0 */

    memset(aCtl, 0, sizeof(aCtl));
    memset(aRoot, 0, sizeof(aRoot));

    memset(&env, 0, sizeof(env));
    env.pbPanelsBuilt = &bBuilt;
    env.ppRoot = aRoot;
    for (i = 0; i < 6; ++i) {
        env.apCtl[i] = &aCtl[i];
    }
    env.ppPanelA = &pA;
    env.ppPanelB = &pB;
    env.ppPanelC = &pC;
    env.ppPanelD = &pD;
    env.aTopics = aTopic;
    env.pbTopicCount = &bTopicCount;
    env.apObjects = apObj;
    env.nObjectCount = 149;
    env.apProductA = aProdA;
    env.apProductB = aProdB;
    env.apManufCols = aManufCol;
    env.nSelectedBaseIdx = 1;
    env.pfnSelectListed = ManufStubPanelPrev;
    env.pfnSyncSelection = ManufStubPanelNext;

    ManufScreen_BuildPanels(&env);

    CheckEqI32(bBuilt, 1, "the build flag is raised");
    Check(pA != NULL && pB != NULL && pC != NULL && pD != NULL, "four panels are allocated");
    if (pA == NULL || pB == NULL || pC == NULL || pD == NULL) {
        return;
    }

    /* The count pass: topic 0 is skipped, topic 2 is gated out, 106 is filtered out, and 107 twice
     * counts twice. Five rows. */
    CheckEqI32(pA->nLineCount, 5, "five rows counted, topic 0 skipped and the locked topic ignored");
    CheckEqI32(pB->nLineCount, 5, "the same count in panel B");
    CheckEqI32(pC->nLineCount, 5, "and in C");
    CheckEqI32(pD->nLineCount, 5, "and in D");

    CheckEqI32(pA->nStyle, 13, "all four are nStyle 13");
    CheckEqI32(pA->nFont, 16, "and nFont 16");
    CheckEqI32(pB->nStyle, 13, "B too");
    CheckEqI32(pC->nFont, 16, "C too");
    CheckEqI32(pA->nPosX, 10, "A sits at x 10");
    CheckEqI32(pB->nPosX, 150, "B at 150");
    CheckEqI32(pC->nPosX, 187, "C at 187");
    CheckEqI32(pD->nPosX, 233, "D at 233");
    CheckEqI32(pA->nViewW, 230, "A and D are 230 wide");
    CheckEqI32(pD->nViewW, 230, "D too");
    CheckEqI32(pB->nViewW, 30, "B and C are 30 wide");
    CheckEqI32(pC->nViewW, 30, "C too");
    CheckEqI32(pA->nPosY, 70, "all four at y 70");
    CheckEqI32(pC->nRectY, 70, "with +7056 70");
    CheckEqI32(pA->nRectX, 5, "A's +7052 is 5");
    CheckEqI32(pB->nRectX, 155, "B's is 155");
    CheckEqI32(pC->nRectX, 185, "C's is 185");
    CheckEqI32(pD->nRectX, 5, "D's is 5");
    CheckEqI32(pA->nUnk7076, 0, "only B, C and D get nUnk7076 2");
    CheckEqI32(pB->nUnk7076, 2, "B has it");
    CheckEqI32(pD->nUnk7076, 2, "and D");
    CheckEqI32(pA->bSelLineHighlight, 1, "flag 7073 is 1 everywhere -- the constructor sets it and only A and "
                                 "D write it again");
    CheckEqI32(pB->bSelLineHighlight, 1, "B keeps the constructor's 1");
    CheckEqI32(pA->bColorKey, 0x96, "A and D set +7088 to 0x96");
    CheckEqI32(pD->bColorKey, 0x96, "D too");
    CheckEqI32(pB->bColorKey, 0xFF, "B and C keep the constructor's 0xFF");
    CheckEqI32(pA->aStyleRgb[3], 0x32, "only A gets style bytes 3..5");
    CheckEqI32(pA->aStyleRgb[5], 0xFA, "0x32, 0x32, 0xFA");
    CheckEqI32(pB->aStyleRgb[5], 0, "B leaves them at zero");
    Check(pA->pfnClick == ManufStubPanelPrev, "A gets the row click");
    Check(pD->pfnClick == ManufStubPanelNext, "D gets the selection sync");
    Check(pB->pfnClick == NULL, "B and C get no click here");

    /* The rows, in topic order: 2, 3, 107, 120, 107. */
    {
        static const i32 anWantSlot[5] = { 2, 3, 107, 120, 107 };
        static const i32 anWantTopic[5] = { 1, 1, 3, 3, 3 };
        static const char *const apzName[5] = {
            "Laser pistol", "PlasmaPistol12345", "UFOPowerSource123", "LIGHTNINGCraftNam",
            "UFOPowerSource123"
        };
        static const char *const apzB[5] = { "100", "", "7", "3", "7" };
        static const char *const apzC[5] = { "75%", "", "0%", "0%", "0%" };
        static const char *const apzD[5] = { "4/3", "", "1/0", "5/0", "1/0" };
        i32 nOk = 0;
        for (i = 0; i < 5; ++i) {
            const char *pszA = UiPanel_GetLineText(pA, i);
            const char *pszB = UiPanel_GetLineText(pB, i);
            const char *pszC = UiPanel_GetLineText(pC, i);
            const char *pszD = UiPanel_GetLineText(pD, i);
            if (pszA != NULL && strcmp(pszA, apzName[i]) == 0 &&
                pszB != NULL && strcmp(pszB, apzB[i]) == 0 &&
                pszC != NULL && strcmp(pszC, apzC[i]) == 0 &&
                pszD != NULL && strcmp(pszD, apzD[i]) == 0 &&
                pA->anLineAux[i] == anWantSlot[i] && pC->anLineAux[i] == anWantTopic[i]) {
                ++nOk;
            }
        }
        CheckEqI32(nOk, 5, OptCite("every row: name cut to 17, allocated work, percent, asked-for/units, "
                                   "A aux the slot id and C aux the topic index", "@ 0x6e9e8"));
        CheckEqI32(pB->anLineAux[0], 0, OptCite("B and D get no aux from this function", "@ 0x6e9e8"));
        CheckEqI32(pD->anLineAux[2], 0, OptCite("not even on a row that has values", "@ 0x6e9e8"));
    }

    /* The teardown. ManufScreen_BuildPanels @ 0x6e9e8 is 2404 bytes = 571 instructions (14 of them 2-word MOV
     * immediates) + 16 pool words, and the branch selector is the built byte the Init's tail call sets. Its
     * release order is A, C, B, D -- not slot order -- and nothing is stored back, so the original leaves all
     * four globals dangling. */
    aRoot[0] = calloc(1, sizeof(PadImage));
    ManufScreen_BuildPanels(&env);
    CheckEqI32(bBuilt, 0, OptCite("the second call takes the teardown branch and clears the flag", "@ 0x6e9e8"));
    Check(pA == NULL && pB == NULL && pC == NULL && pD == NULL,
          OptCite("the four panel slots are cleared", "@ 0x6e9e8"));
    Check(aRoot[0] == NULL, OptCite("and the screen pad is freed", "@ 0x6e9e8"));

    /* A third call builds again, with nothing queued: zero rows. */
    bTopicCount = 1;
    ManufScreen_BuildPanels(&env);
    CheckEqI32(bBuilt, 1, "the third call builds again");
    Check(pA != NULL, "and allocates");
    if (pA != NULL) {
        CheckEqI32(pA->nLineCount, 0, "one topic means the walk visits nothing, so zero rows");
    }
    ManufScreen_BuildPanels(&env);
    CheckEqI32(bBuilt, 0, "and tears down again");

    bTopicCount = 4;
    bBuilt = 1; /* pretend the panels are up: with no flag pointer the call cannot even branch */
    ManufScreen_BuildPanels(NULL);
    env.pbPanelsBuilt = NULL;
    ManufScreen_BuildPanels(&env);
    CheckEqI32(bBuilt, 1, "a missing flag pointer makes it do nothing at all");
}

/* ManufScreen_Draw @ 0x6e3e0. The four panels come from the real builder, so the row count, the aux
 * column and panel A's style bytes are exactly what the manufacturing screen itself leaves behind. */
static i32 g_nDrawSample;

static void DrawSampleStub(u32 nSample, void *pUser)
{
    (void)pUser;
    g_nDrawSample = (i32)nSample;
}

static bool ManufLineIs(const UiPanel *pPanel, i32 nRow, const char *pszWant)
{
    const char *psz = (pPanel != NULL) ? UiPanel_GetLineText(pPanel, nRow) : NULL;
    return psz != NULL && strcmp(psz, pszWant) == 0;
}

static void TestManufDraw(void)
{
    static ResearchTopicLive aTopic[4];
    static ObjectRecFull aObj[149];
    static ObjectRecFull *apObj[149];
    static u16 aManufCol[1600];
    static u32 aProdA[1600];
    static i32 aProdB[1600];   /* the column at 0x15B79C is u32 in the IDB; the clear and draw envs
                                * spell it i32, so the two spellings meet in an explicit cast here. */
    static u16 aProdC[1600];
    static BaseRec aBases[8];
    static BaseRec *apBases[8];
    static u8 aGridBytes[8];
    static u8 aFacGrid[1524];
    static u16 aResearchCol[144 * 8];
    static UiControl aCtl[6];
    static void *aRoot[7];
    UiPanel *pA = NULL, *pB = NULL, *pC = NULL, *pD = NULL;
    ManufBuildEnv benv;
    ManufClearEnv cenv;
    ManufDrawEnv denv;
    u8 bBuilt = 0, bTopicCount = 4, bEntryCount = 144, bSelected = 1;
    i8 nSelBase = 1;
    i32 nRow = 0;
    i32 i;

    printf("the manufacturing draw click\n");

    for (i = 0; i < 149; ++i) {
        apObj[i] = &aObj[i];
    }
    aObj[2].uManufWorkPerUnit   = 576;
    aObj[3].uManufWorkPerUnit   = 1152;
    aObj[107].uManufWorkPerUnit = 2544;
    aObj[120].uManufWorkPerUnit = 0;   /* shipped 35712; zeroed to reach the zero-divisor path */
    aTopic[1].bUnlocked = 1;
    aTopic[1].abItemIds[0] = 2;
    aTopic[1].abItemIds[1] = 3;
    aTopic[2].bUnlocked = 0;           /* 5, 6 and 9 are flagged, so this proves the gate */
    aTopic[2].abItemIds[0] = 5;
    aTopic[2].abItemIds[1] = 6;
    aTopic[3].bUnlocked = 1;
    aTopic[3].abItemIds[0] = 107;
    aTopic[3].abItemIds[1] = 120;

    /* Base 1, so every product index is 200 + slot. The quantity total is 110, which makes the
     * workshop budget 110 / 50 + 1 = 3 in the two tail passes. */
    aManufCol[200 + 2]   = 100;
    aManufCol[200 + 107] = 7;
    aManufCol[200 + 120] = 3;
    aProdA[200 + 2]      = 576u * 3u;  /* 1728 -> 3 units, 100*1728/(576*4) = 75 */
    aProdB[200 + 2]      = 4;
    aProdA[200 + 3]      = 1152;
    aProdC[200 + 3]      = 2;
    aProdB[200 + 3]      = 0;          /* nothing asked for: this row takes the clear path */
    aProdA[200 + 107]    = 0;          /* the seed row: quantity and engineers, no work yet */
    aProdB[200 + 107]    = 1;
    aProdA[200 + 120]    = 10;
    aProdB[200 + 120]    = 5;

    for (i = 0; i < 8; ++i) {
        apBases[i] = &aBases[i];
        aBases[i].bUnk4 = (u8)(7 + i); /* base 1 has type 9, and 9 is found at index 1 */
    }
    aGridBytes[1] = 5;

    memset(aCtl, 0, sizeof(aCtl));
    memset(aRoot, 0, sizeof(aRoot));

    memset(&benv, 0, sizeof(benv));
    benv.pbPanelsBuilt = &bBuilt;
    benv.ppRoot = aRoot;
    for (i = 0; i < 6; ++i) {
        benv.apCtl[i] = &aCtl[i];
    }
    benv.ppPanelA = &pA;
    benv.ppPanelB = &pB;
    benv.ppPanelC = &pC;
    benv.ppPanelD = &pD;
    benv.aTopics = aTopic;
    benv.pbTopicCount = &bTopicCount;
    benv.apObjects = apObj;
    benv.nObjectCount = 149;
    benv.apProductA = aProdA;
    benv.apProductB = aProdB;
    benv.apManufCols = aManufCol;
    benv.nSelectedBaseIdx = 1;

    memset(&cenv, 0, sizeof(cenv));
    cenv.apProductA = aProdA;
    cenv.apProductB = aProdB;
    cenv.apProductC = aProdC;
    cenv.apManufCols = aManufCol;
    cenv.abGridBytes = aGridBytes;
    cenv.apBases = apBases;
    cenv.nBaseCount = 8;
    cenv.pnSelectedBaseIdx = &nSelBase;

    memset(&denv, 0, sizeof(denv));
    denv.pProductPanel = pA;
    denv.pPanelB = pB;
    denv.pPanelC = pC;
    denv.pPanelD = pD;
    denv.pnProductRow = &nRow;
    denv.pbProductSelected = &bSelected;
    denv.apProductA = aProdA;
    denv.apProductB = aProdB;
    denv.apManufCols = aManufCol;
    denv.apResearchCols = aResearchCol;
    denv.pbEntryCount = &bEntryCount;
    denv.apObjects = apObj;
    denv.nObjectCount = 149;
    denv.apBases = apBases;
    denv.nBaseCount = 8;
    denv.abGridBytes = aGridBytes;
    denv.abFacilityGrid = aFacGrid;
    denv.pnSelectedBaseIdx = &nSelBase;
    denv.pClearEnv = &cenv;
    denv.pfnPlaySample = DrawSampleStub;

    ManufScreen_BuildPanels(&benv);
    CheckEqI32(pA != NULL ? pA->nLineCount : -1, 4,
               "the builder gives four rows: 2, 3, 107, 120");
    if (pA == NULL || pB == NULL || pC == NULL || pD == NULL) {
        return;
    }
    denv.pProductPanel = pA;
    denv.pPanelB = pB;
    denv.pPanelC = pC;
    denv.pPanelD = pD;

    CheckEqI32((i32)*(const u32 *)(const void *)pA->aStyleRgb, (i32)0x32B4B4B4u,
               "panel A's offset-0 dword is its style bytes B4 B4 B4 32, not a product id");

    /* Row 0, product 2: the full percent and unit arithmetic. */
    nRow = 0;
    bSelected = 1;
    g_nDrawSample = -1;
    pA->bSelLineHighlight = 0;
    ManufScreen_Draw(&denv);
    CheckEqI32(g_nDrawSample, 17, "sample 17 is played (0x6E430)");
    CheckEqI32(bSelected, 0, "bManufProductSelected is cleared (0x6E410)");
    CheckEqI32(pA->bSelLineHighlight, 1, "panel A's flag 7073 is set (0x6E424)");
    Check(ManufLineIs(pB, 0, "100"), "panel B shows the allocated work");
    Check(ManufLineIs(pC, 0, "75%"), "panel C shows 100 * 1728 / (576 * 4)");
    Check(ManufLineIs(pD, 0, "4/3"), "panel D shows 4 asked for and 3 units paid for");
    CheckEqI32((i32)aProdA[202], 1728, "a row that already has work is not seeded");

    /* Row 1, product 3: the quantity is zero, so the whole slot is cleared first. */
    nRow = 1;
    ManufScreen_Draw(&denv);
    CheckEqI32((i32)aProdB[203], 0, "the quantity column stays zero");
    CheckEqI32((i32)aProdA[203], 0, "the accumulated work 1152 is cleared with it (0x6F918)");
    CheckEqI32((i32)aProdC[203], 0, "the committed-unit column is cleared too (0x6F92C)");
    CheckEqI32((i32)aManufCol[203], 0, "and the allocated work (0x6F910)");
    CheckEqI32((i32)aProdA[202], 1728, "the neighbouring row is untouched by the clear");
    Check(ManufLineIs(pB, 1, ""), "panel B gets the empty string");
    Check(ManufLineIs(pC, 1, ""), "panel C gets it");
    Check(ManufLineIs(pD, 1, ""), "and panel D");

    /* Row 2, product 107: quantity 1, engineers 7, no accumulated work -- the seed. */
    nRow = 2;
    ManufScreen_Draw(&denv);
    CheckEqI32((i32)aProdA[307], 1, "the seed stores 1 (0x6E51C)");
    Check(ManufLineIs(pB, 2, "7"), "panel B shows the allocated 7");
    Check(ManufLineIs(pC, 2, "0%"), "100 * 1 / (2544 * 1) is 0");
    Check(ManufLineIs(pD, 2, "1/0"), "1 asked for, 1 / 2544 units paid for");

    /* Row 3, product 120: work-per-unit zero, so both divisions would be by zero. */
    nRow = 3;
    ManufScreen_Draw(&denv);
    Check(ManufLineIs(pB, 3, "3"), "panel B shows the allocated 3");
    Check(ManufLineIs(pC, 3, "0%"), "the zero divisor answers 0 instead of dividing");
    Check(ManufLineIs(pD, 3, "5/0"), "and the unit count answers 0");
    CheckEqI32((i32)aProdA[320], 10, "a row with work is not seeded");

    /* The hangar branch, and that it is dead. With the style dword the builder really leaves, the
     * base byte at 0x159DBC does not move; force the dword into 119..121 and it does. */
    CheckEqI32(aGridBytes[1], 5, "the base byte starts at 5");
    nRow = 3;
    ManufScreen_Draw(&denv);
    CheckEqI32(aGridBytes[1], 5, "the real style dword 0x32B4B4B4 is outside 119..121, so nothing is written");
    *(i32 *)(void *)pA->aStyleRgb = 118;
    ManufScreen_Draw(&denv);
    CheckEqI32(aGridBytes[1], 5, "118 is below the range");
    *(i32 *)(void *)pA->aStyleRgb = 122;
    ManufScreen_Draw(&denv);
    CheckEqI32(aGridBytes[1], 5, "122 is above it");
    *(i32 *)(void *)pA->aStyleRgb = 120;
    ManufScreen_Draw(&denv);
    CheckEqI32(aGridBytes[1], 10, "120 takes the branch: 5 plus the low byte of the quantity 5");
    *(i32 *)(void *)pA->aStyleRgb = 121;
    aGridBytes[1] = 250;
    ManufScreen_Draw(&denv);
    CheckEqI32(aGridBytes[1], 255, "the sum is stored as a byte, so 250 + 5 wraps to 255");
    *(i32 *)(void *)pA->aStyleRgb = (i32)0x32B4B4B4u;

    /* The two tail passes, with the real budget: 110 allocated work buys 3 needed workshops. */
#define D_T(c, r) aFacGrid[480 + 144 + 6 * (c) + (r)]
#define D_N(c, r) aFacGrid[444 + 144 + 6 * (c) + (r)]
    memset(aFacGrid, 0, sizeof(aFacGrid));
    D_T(0, 0) = 3; D_T(0, 1) = 3; D_T(0, 2) = 3; D_T(0, 3) = 3; D_T(0, 4) = 3;
    D_T(1, 1) = 2;   /* a lab */
    D_T(2, 2) = 4;   /* neither */
    D_N(0, 0) = 1; D_N(0, 1) = 1; D_N(0, 2) = 1; D_N(0, 3) = 1; D_N(0, 4) = 1;
    D_N(1, 1) = 1;
    D_N(2, 2) = 9;
    nRow = 0;
    ManufScreen_Draw(&denv);
    CheckEqI32(D_N(0, 0) + D_N(0, 1) + D_N(0, 2), 3, "three workshops claim the budget");
    CheckEqI32(D_N(0, 3), 0, "the fourth is marked unneeded");
    CheckEqI32(D_N(0, 4), 0, "and the fifth");
    CheckEqI32(D_N(1, 1), 0, "the lab pass runs too, with no active research so no budget");
    CheckEqI32(D_N(2, 2), 9, "a cell that is neither workshop nor lab keeps its byte");
#undef D_T
#undef D_N

    /* A row outside the panel: no table work, but the tail passes still run. */
    memset(aFacGrid, 0, sizeof(aFacGrid));
    aFacGrid[480 + 144 + 0] = 3;   /* one workshop, base 1 */
    aFacGrid[444 + 144 + 0] = 1;
    nRow = PUFO_PANEL_LINES;
    ManufScreen_Draw(&denv);
    CheckEqI32((i32)aProdA[202], 1728, "an out-of-panel row touches no product column");
    Check(ManufLineIs(pB, 0, "100"), "and rewrites no line");
    CheckEqI32(aFacGrid[444 + 144 + 0], 1, "the two tail passes still run for that row");

    /* A missing environment or panel does nothing at all. */
    bSelected = 1;
    ManufScreen_Draw(NULL);
    CheckEqI32(bSelected, 1, "a null environment cannot even clear the selected flag");
    denv.pProductPanel = NULL;
    ManufScreen_Draw(&denv);
    CheckEqI32(bSelected, 1, "and a null panel stops it the same way");
    denv.pProductPanel = pA;

    ManufScreen_BuildPanels(&benv);
    CheckEqI32(bBuilt, 0, "the panels tear down");
}

/* ---- ManufScreen_DrawFrame @ 0x6f9a4 -------------------------------------------------------
 *
 * The frame is a drawing function, so the test draws it for real: a 240 x 320 16 bpp surface, the
 * real FONT.SPR out of lang.dat, and the real TEXTS.DAT layout. Every claim below is a pixel that
 * is there or is not there, a control that moved, or a gate byte that changed. Nothing asks whether
 * a function "ran" -- each check names the coordinate the original names. */

#define FRAME_W 240
#define FRAME_H 320
#define FRAME_PIX (FRAME_W * FRAME_H)

static SoftSurf g_frameSurf;
static SoftSurf g_frameRef;
static PadImage g_aFramePads[12];
static i32 g_aFrameBlitLog[16];
static i32 g_nFrameBlitCount;
static u8 g_bFrameAnimStarted;
static i8 g_nFrameAnimProgress;
static UiAnimBlitEnv g_frameAnim;
static GfxColorEnv g_frameColor;
static i32 g_nFrameGamma = 3;   /* factor 1.0: the neutral gamma */
static bool g_bFrameFontReady;

static ObjectRecFull g_aFrameObj[149];
static ObjectRecFull *g_apFrameObj[149];
static u16 g_aFrameCol[1600];
static i32 g_aFrameProdB[1600];
static u32 g_aFramePrice[149];
static BaseSiteRec g_aFrameSite[8];
static u8 g_aFrameGrid[1524];
static char g_aFrameText[1712 * PUFO_TEXT_REC_STRIDE];
static UiPanel g_aFramePanel[4];
static UiControl g_aFrameCtl[11];

static void FrameBlitRec(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    if (g_nFrameBlitCount < 16) {
        g_aFrameBlitLog[g_nFrameBlitCount] = (i32)(pPad - g_aFramePads);
    }
    ++g_nFrameBlitCount;
}

/* The real font, loaded the way MainMenu_Init @ 0x38B78 loads it: the glyph base is the running
 * frame count BEFORE the load, which is zero for a fresh font object. */
static void FrameLoadFont(void)
{
    const char *const apszArch[2] = { "lang.dat", "main.dat" };

    g_bFrameFontReady = false;
    if (!Asset_OpenArchives(apszArch)) {
        return;
    }
    PufoFont_Init(&g_fontSys);
    g_fontSys.nGlyphBase = g_fontSys.nTotalSprFrames;
    (void)Font_LoadSpr(&g_fontSys, "font");
    Font_BuildIndex(&g_fontSys);
    FontMetrics_Load();
    g_bFrameFontReady = (g_fontSys.apSprIndex != NULL && g_fontMetrics.bLoaded);
}

static void FrameRestoreFont(void)
{
    PufoFont_Free(&g_fontSys);
    memset(&g_fontMetrics, 0, sizeof(g_fontMetrics));
}

static void FrameZero(void)
{
    memset(g_frameSurf.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_nFrameBlitCount = 0;
}

static void FrameSnapshot(void)
{
    memcpy(g_frameRef.pPixels, g_frameSurf.pPixels, (size_t)FRAME_PIX * 2u);
}

static u16 FramePix(i32 x, i32 y)
{
    return ((u16 *)g_frameSurf.pPixels)[y * FRAME_W + x];
}

static bool FrameBandEmpty(i32 x0, i32 x1, i32 y0, i32 y1)
{
    const u16 *p = (const u16 *)g_frameSurf.pPixels;

    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            if (p[y * FRAME_W + x] != 0) {
                return false;
            }
        }
    }
    return true;
}

static i32 FrameBandFirstX(i32 x0, i32 x1, i32 y0, i32 y1)
{
    const u16 *p = (const u16 *)g_frameSurf.pPixels;

    for (i32 x = x0; x < x1; ++x) {
        for (i32 y = y0; y < y1; ++y) {
            if (p[y * FRAME_W + x] != 0) {
                return x;
            }
        }
    }
    return -1;
}

static bool FrameBandSame(i32 x0, i32 x1, i32 y0, i32 y1)
{
    const u16 *pa = (const u16 *)g_frameSurf.pPixels;
    const u16 *pb = (const u16 *)g_frameRef.pPixels;

    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            if (pa[y * FRAME_W + x] != pb[y * FRAME_W + x]) {
                return false;
            }
        }
    }
    return true;
}

/* Rebuild just the price band: the detail-panel frame, then the label at (10, 97). Nothing else the
 * frame draws reaches x 10..129 at y 99..112 -- the rails are at x 5 and 234, the separators at
 * x 130, 152 and 187, the component headers start at y 114 -- so this reproduces that band exactly
 * and the comparison pins the label string, not just its presence. */
static void FramePriceRef(const char *pszLabel)
{
    memset(g_frameRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_frameRef;
    (void)Ui_AnimExpandBlit(&g_frameAnim, 0, 30, 236, 284, 4, 0x28, 0x2D, 0xC8, 3);
    (void)Gfx_PackRgb565Pair(&g_frameColor, 250, 250, 250, 0, 0, 0);
    Text_DrawString(10, 97, pszLabel);
    g_pMainSurface = &g_frameSurf;
}

static void TestManufDrawFrame(void)
{
    static u8 bBuilt, bSelected, bPreviewOk, bEntryCount, bMapPhase, bQtyGate;
    static u32 nShortfall;
    static i32 nCursorY, nShortfallReason, nRow;
    static i8 nSelBase;
    ManufFrameEnv env;
    i32 i;

    printf("the manufacturing screen's frame\n");

    FrameLoadFont();
    if (!g_bFrameFontReady) {
        printf("  FAIL the real font did not load, so the frame cannot be checked\n");
        ++g_nFailures;
        return;
    }

    SoftSurf_ctor(&g_frameSurf);
    (void)SoftSurf_AllocPixels(&g_frameSurf, FRAME_W, FRAME_H, PIXMODE_16BPP);
    SoftSurf_ctor(&g_frameRef);
    (void)SoftSurf_AllocPixels(&g_frameRef, FRAME_W, FRAME_H, PIXMODE_16BPP);
    g_pMainSurface = &g_frameSurf;
    g_blitState.nPixelMode = 2;

    /* Labels. They are ASCII stand-ins for the lang.dat records -- the record NUMBERS are the
     * claim, and those come from the address arithmetic in the header. */
    memset(g_aFrameText, 0, sizeof(g_aFrameText));
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_HEADER, "MANUF");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_ONE_UNIT, "unit");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_REQUIRED, "REQ");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_AVAILABLE, "AVL");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_ENG_FREE, "ENG: ");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_WS_FREE, "WS: ");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_ENG_CAP, "Eng");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_ENG_USED, "used: ");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_UNIT_CAP, "Units");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_UNIT_ASK, "ask: ");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_NO_STORES, "NO STORES");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_NO_HANGAR, "NO HANGARS");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_OK, "OK");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_CANCEL, "CANCEL");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_ADD, "ADD");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_REMOVE, "DEL");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_ALL, "ALL");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_WORK_HOURS, " hrs");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_COMP_ELERIUM, "ELERIUM");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_COMP_ALLOY, "ALLOY");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_COMP_POWER, "POWER");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_MANUF_TEXT_COMP_NAV, "NAV");
    /* 1175 (the price caption) and 1711 (the second quantity button) stay empty: 1175 so the price
     * band holds only the number and the dollar sign, 1711 because that record is past the 1650
     * lang.dat loads. */

    for (i = 0; i < 149; ++i) {
        g_apFrameObj[i] = &g_aFrameObj[i];
    }
    PutStr(g_aFrameObj[2].szName, "PANEL-X");
    PutStr(g_aFrameObj[107].szName, "POWER CORE");
    g_aFrameObj[2].uManufWorkPerUnit = 576;
    g_aFrameObj[2].abManufComponents[0] = 4;   /* stock 10: enough */
    g_aFrameObj[2].abManufComponents[1] = 2;   /* stock 1: short, this is the gate */
    g_aFrameObj[2].abManufComponents[2] = 0;   /* absent: no row, no row advance */
    g_aFrameObj[2].abManufComponents[3] = 1;   /* stock 5: enough */
    g_aFrameObj[OBJ_ELERIUM_115].auPerBaseVal[1] = 10;
    g_aFrameObj[OBJ_ALIEN_ALLOYS].auPerBaseVal[1] = 1;
    g_aFrameObj[OBJ_UFO_NAVIGATION].auPerBaseVal[1] = 5;
    g_aFrameObj[112].auPerBaseVal[1] = 130;    /* 130 - 110 allocated = 20 engineers free */
    g_aFramePrice[2] = 1000u;                  /* * 0.7 -> 700 */

    /* Base 1 again: every product index is 200 + slot, and the allocated column sums to 110. */
    g_aFrameCol[200 + 2] = 100;
    g_aFrameCol[200 + 107] = 7;
    g_aFrameCol[200 + 120] = 3;
    g_aFrameProdB[200 + 2] = 4;

    for (i = 0; i < 8; ++i) {
        PutStr(g_aFrameSite[i].abName, "NORTH");
    }
    PutStr(g_aFrameSite[1].abName, "ALPHA");

    /* Three free workshop cells at base 1: capacity 3 * 50 = 150, minus 110 allocated = 40. */
    memset(g_aFrameGrid, 0, sizeof(g_aFrameGrid));
    g_aFrameGrid[480 + 144 + 6 * 0 + 0] = 3;
    g_aFrameGrid[480 + 144 + 6 * 0 + 1] = 3;
    g_aFrameGrid[480 + 144 + 6 * 1 + 0] = 3;

    /* Four hand-built panels. nLineCount 4 with nStyle 3 makes the band 16 * 3 = 48, which is the
     * min() the original applies. Their rows sit at (200, 210) so nothing they draw lands on a
     * separator column or a label row. */
    for (i = 0; i < 4; ++i) {
        if (!UiPanel_ctor(&g_aFramePanel[i], 4)) {
            printf("  FAIL a frame panel could not be constructed\n");
            ++g_nFailures;
            return;
        }
        g_aFramePanel[i].nLineCount = 4;
        g_aFramePanel[i].nStyle = 3;
        g_aFramePanel[i].nUnk3028 = 0;
        g_aFramePanel[i].nPosX = 200;
        g_aFramePanel[i].nPosY = 210;
        g_aFramePanel[i].nUnk7076 = 0;
        (void)UiPanel_SetLineText(&g_aFramePanel[i], 0, "ROW0");
        (void)UiPanel_SetLineText(&g_aFramePanel[i], 1, "ROW1");
        (void)UiPanel_SetLineText(&g_aFramePanel[i], 2, "ROW2");
        UiPanel_SetLineAux(&g_aFramePanel[i], 0, 2);
        UiPanel_SetLineAux(&g_aFramePanel[i], 1, 3);
        UiPanel_SetLineAux(&g_aFramePanel[i], 2, 107);
        UiPanel_SetLineAux(&g_aFramePanel[i], 3, 120);
    }
    g_aFramePanel[3].nUnk3032 = 2; /* panel D's row cursor -> aux 107 */

    memset(g_aFramePads, 0, sizeof(g_aFramePads));
    memset(g_aFrameCtl, 0, sizeof(g_aFrameCtl));
    for (i = 0; i < 11; ++i) {
        g_aFrameCtl[i].bEnabled = 1;
        g_aFrameCtl[i].nMode = 0;
        g_aFrameCtl[i].nState = 0;
        g_aFrameCtl[i].apPads[0] = &g_aFramePads[i];
    }

    g_bFrameAnimStarted = 1;
    g_nFrameAnimProgress = 100;
    memset(&g_frameAnim, 0, sizeof(g_frameAnim));
    g_frameAnim.pbStarted = &g_bFrameAnimStarted;
    g_frameAnim.pnProgress = &g_nFrameAnimProgress;
    g_frameAnim.nFrameStyle = 0;

    g_frameColor.pnGamma = &g_nFrameGamma; /* factor 1.0, so the colour pairs pass through */
    g_frameColor.nFadeCount = 0;

    bEntryCount = 144;
    nSelBase = 1;
    nRow = 0;
    nCursorY = 90;
    bMapPhase = 0;

    memset(&env, 0, sizeof(env));
    env.pColor = &g_frameColor;
    env.pAnim = &g_frameAnim;
    env.pPanelA = &g_aFramePanel[0];
    env.pPanelB = &g_aFramePanel[1];
    env.pPanelC = &g_aFramePanel[2];
    env.pPanelD = &g_aFramePanel[3];
    env.pbPanelsBuilt = &bBuilt;
    env.pbProductSelected = &bSelected;
    env.pbQtyGate = &bQtyGate;
    env.pnProductRow = &nRow;
    env.pbPreviewOk = &bPreviewOk;
    env.pnCursorY = &nCursorY;
    env.pnSelectedBaseIdx = &nSelBase;
    env.pbMapPhase = &bMapPhase;
    env.apObjects = g_apFrameObj;
    env.nObjectCount = 149;
    env.pTextRecs = g_aFrameText;
    env.apSiteRecs = g_aFrameSite;
    env.apProductB = g_aFrameProdB;
    env.apManufCols = g_aFrameCol;
    env.pbEntryCount = &bEntryCount;
    env.pdwPrices = g_aFramePrice;
    env.pEngineerRec = &g_aFrameObj[112];
    env.abFacilityGrid = g_aFrameGrid;
    for (i = 0; i < 6; ++i) {
        env.apCtl[i] = &g_aFrameCtl[i];
    }
    env.pCtlReset = &g_aFrameCtl[6];
    env.pCtlAllocAll = &g_aFrameCtl[7];
    env.pCtlClampOne = &g_aFrameCtl[8];
    env.pCtlDraw = &g_aFrameCtl[9];
    env.pCtlConfirm = &g_aFrameCtl[10];
    env.pfnCtlBlit = FrameBlitRec;
    env.pfnCtlBlitEx = FrameBlitRec;
    env.pdwShortfall = &nShortfall;
    env.pnShortfallReason = &nShortfallReason;

    /* ---- 1. the rails, with nothing else on the screen ---- */
    bBuilt = 0;
    bSelected = 0;
    nShortfall = 0;
    g_aFrameCtl[6].x = 7;
    g_aFrameCtl[6].y = 9;
    FrameZero();
    ManufScreen_DrawFrame(&env);

    CheckEqI32(FramePix(5, 1), 0x632C, "the first left-rail step is index 5 * (0 + 20) = 100 at (5, 1)");
    CheckEqI32(FramePix(5, 7), 0x632C, "and that step is seven rows tall, so (5, 7) is the same");
    CheckEqI32(FramePix(5, 0), 0, "nothing is drawn above the first step");
    CheckEqI32(FramePix(5, 8), 0x6B4D, "the next step climbs to index 105 at (5, 8)");
    CheckEqI32(FramePix(5, 172), 0xD6BA, "the descending half at step 3 is 0xE6 - 15 = 215 at (5, 172)");
    CheckEqI32(FramePix(5, 151), 0xCE79, "y 151 is where the two halves overlap, and the later climbing step wins");
    CheckEqI32(FramePix(5, 312), 0x738E, "the last descending step is 0xE6 - 115 = 115 at (5, 312)");
    CheckEqI32(FramePix(234, 1), 0x632C, "the right rail mirrors the left one at x 234");
    CheckEqI32(FramePix(234, 312), 0x738E, "and its descending half too");
    CheckEqI32(FramePix(1, 5), 0x632C, "the top rail starts at (1, 5) with the same climbing index");
    CheckEqI32(FramePix(234, 5), 0x8430, "the top rail's last step is 0xFA - 115 = 135 at (234, 5)");
    CheckEqI32(FramePix(1, 315), 0x632C, "the bottom rail repeats the top one at y 315");
    CheckEqI32(FramePix(234, 315), 0x8430, "with the same falling index at the far end");
    CheckEqI32(FramePix(5, 315), 0x632C, "the bottom rail is drawn after the side rails, so it wins at (5, 315)");
    CheckEqI32(FramePix(5, 316), 0x738E, "one row lower the side rail's own colour is still there");
    Check(FrameBandEmpty(10, 129, 99, 112), "the interior stays empty when no panel and no product is drawn");
    CheckEqI32(g_nFrameBlitCount, 1, "the reset control is the only control blitted");
    Check(g_nFrameBlitCount == 1 && g_aFrameBlitLog[0] == 6, "and it is the reset control's pad");
    CheckEqI32(g_aFramePads[6].nOriginX, 7, "the pad follows the control's x");
    CheckEqI32(g_aFramePads[6].nOriginY, 9, "and its y");

    /* ---- 2. panels built, no product selected ---- */
    bBuilt = 1;
    bSelected = 0;
    bPreviewOk = 1;
    nCursorY = 90;
    FrameZero();
    ManufScreen_DrawFrame(&env);

    Check(!FrameBandEmpty(200, 232, 210, 226), "the four panels are drawn through UiPanel_DrawText");
    CheckEqI32(FramePix(130, 69), 0xB5B6, "the first column separator is index 0xB4 at (130, 69)");
    CheckEqI32(FramePix(130, 118), 0xB5B6, "it is 16 * min(4, 3) + 2 = 50 tall, so (130, 118) is the last row");
    CheckEqI32(FramePix(130, 119), 0, "and (130, 119) is outside -- the band is capped by nStyle, not nLineCount");
    CheckEqI32(FramePix(152, 69), 0xB5B6, "the second separator sits at x 152");
    CheckEqI32(FramePix(187, 69), 0xB5B6, "the third at x 187");
    Check(!FrameBandEmpty(10, 129, 300, 315), "the map-cursor row names its object at (10, 300)");
    CheckEqI32(FrameBandFirstX(10, 129, 300, 315), 11, "that name starts at x 11, the left margin");
    Check(FrameBandEmpty(20, 220, 130, 200), "no shortfall panel with the shortfall value clear");

    /* The cursor-row gate, one variable at a time. */
    nCursorY = 70;
    FrameZero();
    ManufScreen_DrawFrame(&env);
    Check(FrameBandEmpty(10, 129, 300, 315), "cursor y 70 is not above 70, so no name");
    nCursorY = 118; /* 70 + 48, the band's own height: the compare is strict */
    FrameZero();
    ManufScreen_DrawFrame(&env);
    Check(FrameBandEmpty(10, 129, 300, 315), "cursor y 70 + band is outside the band");
    nCursorY = 90;
    bPreviewOk = 0;
    FrameZero();
    ManufScreen_DrawFrame(&env);
    Check(FrameBandEmpty(10, 129, 300, 315), "and a base preview that is not ok stops it too");
    bPreviewOk = 1;

    /* ---- 3. product selected, the animation not ready ---- */
    bSelected = 1;
    g_nFrameAnimProgress = 0;
    FrameZero();
    ManufScreen_DrawFrame(&env);
    CheckEqI32(g_nFrameAnimProgress, 0, "mode 3 at progress 0 is 0 wide, so the panel is not finished");
    Check(FrameBandEmpty(60, 129, 99, 112), "and the detail panel's interior is not drawn");
    CheckEqI32(g_nFrameBlitCount, 0, "no button control is blitted on that early return");

    /* ---- 4. product selected, animation ready ---- */
    g_nFrameAnimProgress = 100;
    bQtyGate = 0;
    g_aFrameCtl[7].x = 1;
    g_aFrameCtl[7].y = 2;
    g_aFrameCtl[8].x = 3;
    g_aFrameCtl[8].y = 4;
    g_aFrameCtl[7].pfnClick = NULL;
    g_aFrameCtl[8].pfnClick = NULL;
    FrameZero();
    ManufScreen_DrawFrame(&env);

    CheckEqI32(g_nFrameAnimProgress, 100, "progress 100 clamps to the full 236 x 284 rect");
    Check(FramePix(120, 170) != 0, "the detail panel now covers the interior");
    CheckEqI32(bQtyGate, 1, "component 1 asks for 2 out of a stock of 1, so the quantity gate is set");
    CheckEqI32(g_nFrameBlitCount, 8, "four arrow controls, the two quantity buttons and the two bottom ones");
    Check(g_nFrameBlitCount == 8 && g_aFrameBlitLog[0] == 2 && g_aFrameBlitLog[1] == 3
              && g_aFrameBlitLog[2] == 4 && g_aFrameBlitLog[3] == 5,
          "the arrow controls blit in slot order, starting at the third");
    Check(g_nFrameBlitCount == 8 && g_aFrameBlitLog[4] == 7 && g_aFrameBlitLog[5] == 8
              && g_aFrameBlitLog[6] == 9 && g_aFrameBlitLog[7] == 10,
          "then the allocate-all, clamp-to-one, draw and confirm controls");
    CheckEqI32(g_aFrameCtl[7].x, 94, "the allocate-all control is moved to x 94");
    CheckEqI32(g_aFrameCtl[7].y, 251, "and y 251");
    CheckEqI32(g_aFrameCtl[8].x, 94, "the clamp-to-one control is moved to the same x");
    CheckEqI32(g_aFrameCtl[8].y, 271, "and y 271");
    Check(g_aFrameCtl[7].pfnClick == NULL && g_aFrameCtl[8].pfnClick == NULL,
          "the frame does not invent handlers, it installs the ones the environment gives");

    /* The component rows: a zero component draws nothing and does not advance the row counter. */
    FrameSnapshot();
    g_aFrameObj[2].abManufComponents[2] = 3; /* now every component is asked for */
    FrameZero();
    ManufScreen_DrawFrame(&env);
    Check(!FrameBandSame(10, 129, 172, 186), "the fourth component row only appears when component 2 is asked for");
    Check(FrameBandSame(10, 129, 130, 144), "the first component row is the same in both");
    Check(FrameBandSame(10, 129, 144, 158), "and so is the short one -- the rows do not shift");
    g_aFrameObj[2].abManufComponents[2] = 0;

    /* The short-component colour follows nMapPhase: 20 * (nMapPhase + 5). */
    FrameSnapshot();
    bMapPhase = 5;
    FrameZero();
    ManufScreen_DrawFrame(&env);
    Check(!FrameBandSame(10, 129, 144, 158), "phase 5 draws the short row in 20 * (5 + 5), not 20 * 5");
    bMapPhase = 0;

    /* Every component in stock: the gate stays down. */
    g_aFrameObj[OBJ_ALIEN_ALLOYS].auPerBaseVal[1] = 9;
    bQtyGate = 0;
    FrameZero();
    ManufScreen_DrawFrame(&env);
    CheckEqI32(bQtyGate, 0, "with 2 asked out of a stock of 9 the gate is not set");
    g_aFrameObj[OBJ_ALIEN_ALLOYS].auPerBaseVal[1] = 1;

    /* The price: TEXTS[1175] is empty here, so the band holds only itoa(price * 0.7) and "$". */
    FrameZero();
    ManufScreen_DrawFrame(&env);
    FramePriceRef("700$");
    Check(FrameBandSame(10, 130, 99, 112), "1000 * 0.7 is 700, and the band is exactly that string plus $");
    FramePriceRef("701$");
    Check(!FrameBandSame(10, 130, 99, 112), "it is not 701, so the product truncates");
    FramePriceRef("699$");
    Check(!FrameBandSame(10, 130, 99, 112), "nor 699");
    FramePriceRef("700");
    Check(!FrameBandSame(10, 130, 99, 112), "and the dollar sign is part of the label");
    g_aFramePrice[2] = 1428u; /* 1428 * 0.7 = 999.6 -> 999 */
    FrameZero();
    ManufScreen_DrawFrame(&env);
    FramePriceRef("999$");
    Check(FrameBandSame(10, 130, 99, 112), "1428 * 0.7 is 999");
    g_aFramePrice[2] = 1429u; /* 1429 * 0.7 = 1000.3 -> 1000 */
    FrameZero();
    ManufScreen_DrawFrame(&env);
    FramePriceRef("1000$");
    Check(FrameBandSame(10, 130, 99, 112), "1429 * 0.7 is 1000, so the factor is 0.7 to within 1/1428");
    g_aFramePrice[2] = 1000u;

    /* The crew and capacity read-out follows its own inputs. */
    FrameSnapshot();
    g_aFrameGrid[480 + 144 + 6 * 1 + 1] = 3; /* a fourth free workshop: 200 - 110 = 90 */
    FrameZero();
    ManufScreen_DrawFrame(&env);
    Check(!FrameBandSame(10, 129, 199, 213), "the free-workshop line follows the grid");
    g_aFrameGrid[480 + 144 + 6 * 1 + 1] = 0;
    FrameSnapshot();
    g_aFrameObj[112].auPerBaseVal[1] = 131; /* 131 - 110 = 21 engineers */
    FrameZero();
    ManufScreen_DrawFrame(&env);
    Check(!FrameBandSame(10, 129, 185, 199), "and the engineer line follows the crew stock");
    g_aFrameObj[112].auPerBaseVal[1] = 130;

    /* ---- 5. the shortfall panel ---- */
    bSelected = 0;
    nShortfall = 1;
    nShortfallReason = 0;
    FrameZero();
    ManufScreen_DrawFrame(&env);
    Check(FramePix(20, 130) != 0, "shortfall 1 with reason 0 draws the 200 x 70 panel at (20, 130)");
    Check(!FrameBandEmpty(20, 220, 158, 174), "with its message centred at y 158");
    FrameSnapshot();
    nShortfallReason = 1;
    FrameZero();
    ManufScreen_DrawFrame(&env);
    Check(FramePix(20, 130) != 0, "reason 1 draws the same panel");
    Check(!FrameBandSame(20, 220, 158, 174), "with the hangar message instead of the stores one");
    nShortfall = 2;
    FrameZero();
    ManufScreen_DrawFrame(&env);
    CheckEqI32(FramePix(20, 130), 0, "any other shortfall value draws nothing");
    nShortfall = 0;
    nShortfallReason = 0;

    /* A missing environment or a missing panel does nothing at all. */
    bSelected = 1;
    FrameZero();
    ManufScreen_DrawFrame(NULL);
    CheckEqI32(g_nFrameBlitCount, 0, "a null environment blits no control");
    Check(FrameBandEmpty(0, FRAME_W, 0, FRAME_H), "and draws no pixel");
    bSelected = 0;
    env.pPanelA = NULL;
    FrameZero();
    ManufScreen_DrawFrame(&env);
    CheckEqI32(FramePix(5, 1), 0x632C, "the rails do not depend on the product panel");
    Check(FrameBandEmpty(10, 129, 300, 315), "but the cursor-row name needs panel A's aux column");
    env.pPanelA = &g_aFramePanel[0];

    for (i = 0; i < 4; ++i) {
        UiPanel_FreeLines(&g_aFramePanel[i]);
    }
    FrameRestoreFont();
}

/* ---- ResearchScreen_Draw @ 0x73a34 ---------------------------------------------------------
 *
 * Checked on the real surface with the real font and the real research.pad, so every claim below is
 * about pixels: which parts of the screen the function draws, in which order, and what the nine
 * labels and the three numbers actually say. The label strings are ASCII stand-ins for the lang.dat
 * records -- the record NUMBERS are the claim, and those come from the address arithmetic recorded in
 * the header. */

static PadImage g_rPad;
static SoftSurf g_rPadRef;

static void RPadRef(void)
{
    memset(g_rPadRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_rPadRef;
    PadImage_Blit(&g_rPad);
    g_pMainSurface = &g_frameSurf;
}

static u16 RPadPix(i32 x, i32 y)
{
    return ((u16 *)g_rPadRef.pPixels)[y * FRAME_W + x];
}

/* The screen against the bare pad: true where the frame drew nothing at all. */
static bool RPadBandSame(i32 x0, i32 x1, i32 y0, i32 y1)
{
    const u16 *pa = (const u16 *)g_frameSurf.pPixels;
    const u16 *pb = (const u16 *)g_rPadRef.pPixels;

    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            if (pa[y * FRAME_W + x] != pb[y * FRAME_W + x]) {
                return false;
            }
        }
    }
    return true;
}

static bool RRefBandSame(i32 x0, i32 x1, i32 y0, i32 y1)
{
    const u16 *pa = (const u16 *)g_frameSurf.pPixels;
    const u16 *pb = (const u16 *)g_frameRef.pPixels;

    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            if (pa[y * FRAME_W + x] != pb[y * FRAME_W + x]) {
                return false;
            }
        }
    }
    return true;
}

/* One label of the reference frame, with the exact colour pair the original had packed when it drew
 * it. Both triples matter: the pen Text_DrawString paints with is the SECOND one, so a reference that
 * passes 68,244,252 / 0,0,0 for a teal caption paints nothing at all. */
typedef struct RRefLabel {
    i32 x, y;
    const char *psz;
    i32 nR1, nG1, nB1, nR2, nG2, nB2;
} RRefLabel;

#define RREF_WHITE 250, 250, 250, 0, 0, 0     /* 0x73C34 and 0x73F24 */
#define RREF_TEAL  68, 244, 252, 4, 70, 75    /* 0x73E8C and 0x740BC */

/* The reference frame: the pad, then the detail panel, then the labels the claim is about. The rails
 * are left out on purpose -- every band compared against this reference sits clear of x 5, x 234,
 * y 5 and y 315. */
static void RRefBuild(const RRefLabel *aLabels, i32 nLabels, bool bAnim)
{
    memset(g_frameRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_frameRef;
    PadImage_Blit(&g_rPad);
    if (bAnim) {
        (void)Ui_AnimExpandBlit(&g_frameAnim, 0, 110, 236, 100, 0x32, 0x96, 0x32, 0x96, 4);
    }
    for (i32 i = 0; i < nLabels; ++i) {
        (void)Gfx_PackRgb565Pair(&g_frameColor, aLabels[i].nR1, aLabels[i].nG1, aLabels[i].nB1,
                                 aLabels[i].nR2, aLabels[i].nG2, aLabels[i].nB2);
        Text_DrawString(aLabels[i].x, aLabels[i].y, aLabels[i].psz);
    }
    g_pMainSurface = &g_frameSurf;
}

static i32 RCenterX(i32 nCenter, const char *psz)
{
    return nCenter - Text_MeasureWidth(psz) / 2;
}

static i32 g_nResearchAllocClicks;
static void ResearchStubAlloc(void) { ++g_nResearchAllocClicks; }

static void TestResearchDraw(void)
{
    static u8 bBuilt, bSelected, bEntryCount;
    static i32 nRow;
    static i8 nSelBase;
    ResearchDrawEnv env;
    const char *const pszSci = "SCI:";
    const char *const pszLab = "LAB:";
    const char *const pszUse = "USED:";
    i32 i;

    printf("the research screen's frame\n");

    FrameLoadFont();
    if (!g_bFrameFontReady) {
        printf("  FAIL the real font did not load, so the frame cannot be checked\n");
        ++g_nFailures;
        return;
    }

    memset(&g_rPad, 0, sizeof(g_rPad));
    (void)PadImage_Load(&g_rPad, PUFO_RESEARCH_PAD_NAME, PUFO_RESEARCH_PAD_MODE);
    Check(g_rPad.pData != NULL, "research.pad really loads out of main.dat");
    if (g_rPad.pData == NULL) {
        printf("  FAIL no research.pad, so the frame cannot be checked\n");
        ++g_nFailures;
        FrameRestoreFont();
        return;
    }
    CheckEqI32(g_rPad.uWidth, 240, "research.pad is 240 wide");
    CheckEqI32(g_rPad.uHeight, 320, "and 320 tall, the whole screen");
    CheckEqI32(g_rPad.nMode, 6, "loaded in mode 6, the MOV R2,#6 at 0x749DC");

    SoftSurf_ctor(&g_frameSurf);
    (void)SoftSurf_AllocPixels(&g_frameSurf, FRAME_W, FRAME_H, PIXMODE_16BPP);
    SoftSurf_ctor(&g_frameRef);
    (void)SoftSurf_AllocPixels(&g_frameRef, FRAME_W, FRAME_H, PIXMODE_16BPP);
    SoftSurf_ctor(&g_rPadRef);
    (void)SoftSurf_AllocPixels(&g_rPadRef, FRAME_W, FRAME_H, PIXMODE_16BPP);
    g_pMainSurface = &g_frameSurf;
    g_blitState.nPixelMode = 2;

    memset(g_aFrameText, 0, sizeof(g_aFrameText));
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_RESEARCH_TEXT_HEADER, "RESEARCH");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_RESEARCH_TEXT_SCI_FREE, pszSci);
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_RESEARCH_TEXT_LAB_FREE, pszLab);
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_RESEARCH_TEXT_SCI_USED, pszUse);
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_RESEARCH_TEXT_ADD, "ADD");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_RESEARCH_TEXT_REMOVE, "DEL");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_RESEARCH_TEXT_CANCEL, "CANCEL");
    PutStr(g_aFrameText + PUFO_RESEARCH_TEXT_OK * PUFO_TEXT_REC_STRIDE, "OK");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_RESEARCH_TEXT_ALL, "ALL");

    memset(g_aFrameObj, 0, sizeof(g_aFrameObj));
    memset(g_apFrameObj, 0, sizeof(g_apFrameObj));
    memset(g_aFrameCol, 0, sizeof(g_aFrameCol));
    memset(g_aFrameGrid, 0, sizeof(g_aFrameGrid));
    memset(g_aFrameSite, 0, sizeof(g_aFrameSite));
    memset(g_aFramePads, 0, sizeof(g_aFramePads));
    memset(g_aFrameCtl, 0, sizeof(g_aFrameCtl));

    for (i = 0; i < 8; ++i) {
        PutStr(g_aFrameSite[i].abName, "NORTH");
    }
    PutStr(g_aFrameSite[1].abName, "ALPHA");

    /* Base 1: three free lab cells (150), 50 allocated across the column, 130 scientists on hand. */
    g_aFrameGrid[480 + 144 * 1 + 6 * 0 + 0] = 2;
    g_aFrameGrid[480 + 144 * 1 + 6 * 0 + 1] = 2;
    g_aFrameGrid[480 + 144 * 1 + 6 * 1 + 0] = 2;
    g_aFrameCol[144 * 1 + 5] = 42;
    g_aFrameCol[144 * 1 + 6] = 8;
    g_aFrameObj[113].auPerBaseVal[1] = 130;

    /* Three panels, parked at (10, 240) so no row of theirs lands on a label band or a bar column. */
    for (i = 0; i < 3; ++i) {
        if (!UiPanel_ctor(&g_aFramePanel[i], 4)) {
            printf("  FAIL a research panel could not be constructed\n");
            ++g_nFailures;
            return;
        }
        g_aFramePanel[i].nLineCount = 4;
        g_aFramePanel[i].nStyle = 3;
        g_aFramePanel[i].nPosX = 10;
        g_aFramePanel[i].nPosY = 240;
        (void)UiPanel_SetLineText(&g_aFramePanel[i], 0, "ROW0");
        (void)UiPanel_SetLineText(&g_aFramePanel[i], 1, "ROW1");
        (void)UiPanel_SetLineText(&g_aFramePanel[i], 2, "ROW2");
    }
    UiPanel_SetLineAux(&g_aFramePanel[0], 0, 5);
    UiPanel_SetLineAux(&g_aFramePanel[0], 1, 6);
    UiPanel_SetLineAux(&g_aFramePanel[0], 2, 143);

    for (i = 0; i < 6; ++i) {
        g_aFrameCtl[i].bEnabled = 1;
        g_aFrameCtl[i].nMode = 0;
        g_aFrameCtl[i].nState = 0;
        g_aFrameCtl[i].x = 6;
        g_aFrameCtl[i].y = 260;
        g_aFrameCtl[i].apPads[0] = &g_aFramePads[i];
    }

    g_bFrameAnimStarted = 1;
    g_nFrameAnimProgress = 100;
    memset(&g_frameAnim, 0, sizeof(g_frameAnim));
    g_frameAnim.pbStarted = &g_bFrameAnimStarted;
    g_frameAnim.pnProgress = &g_nFrameAnimProgress;
    g_frameAnim.nFrameStyle = 0;

    g_frameColor.pnGamma = &g_nFrameGamma; /* factor 1.0, so the colour pairs pass through */
    g_frameColor.nFadeCount = 0;

    bEntryCount = 144;
    nSelBase = 1;
    nRow = 0;

    memset(&env, 0, sizeof(env));
    env.pPad = &g_rPad;
    env.pColor = &g_frameColor;
    env.pAnim = &g_frameAnim;
    env.pPanelTopic = &g_aFramePanel[0];
    env.pPanelQty = &g_aFramePanel[1];
    env.pPanelProgress = &g_aFramePanel[2];
    env.pbPanelsBuilt = &bBuilt;
    env.pbTopicSelected = &bSelected;
    env.pnTopicRow = &nRow;
    env.pnSelectedBaseIdx = &nSelBase;
    env.pTextRecs = g_aFrameText;
    env.apSiteRecs = g_aFrameSite;
    env.apResearchCols = g_aFrameCol;
    env.pbEntryCount = &bEntryCount;
    env.pScientistRec = &g_aFrameObj[113];
    env.abFacilityGrid = g_aFrameGrid;
    env.apCtlQty[0] = &g_aFrameCtl[0];
    env.apCtlQty[1] = &g_aFrameCtl[1];
    env.pCtlCancel = &g_aFrameCtl[2];
    env.pCtlStart = &g_aFrameCtl[3];
    env.pCtlAllocAll = &g_aFrameCtl[4];
    env.pCtlClose = &g_aFrameCtl[5];
    env.pfnAllocAllClick = ResearchStubAlloc;
    env.pfnCtlBlit = FrameBlitRec;
    env.pfnCtlBlitEx = FrameBlitRec;

    RPadRef();

    /* ---- 1. the pad and the rails, with byte_160C7C clear ---- */
    bBuilt = 0;
    bSelected = 0;
    FrameZero();
    ResearchScreen_Draw(&env);

    CheckEqI32(FramePix(5, 1), 0x632C, "the pad is blitted first and the left rail paints over it");
    Check(RPadPix(5, 1) != 0x632C, "the pad's own pixel at (5, 1) is a different colour, so the rail really painted over it");
    CheckEqI32(FramePix(5, 7), 0x632C, "and that rail step is seven rows tall");
    CheckEqI32(FramePix(5, 8), 0x6B4D, "the next step climbs to index 105");
    CheckEqI32(FramePix(234, 1), 0x632C, "the right rail mirrors the left one");
    CheckEqI32(FramePix(1, 5), 0x632C, "the top rail starts at (1, 5)");
    CheckEqI32(FramePix(234, 5), 0x8430, "and its falling half ends at 0xFA - 115 = 135");
    CheckEqI32(FramePix(1, 315), 0x632C, "the bottom rail repeats it at y 315");
    CheckEqI32(g_nFrameBlitCount, 0, "no control at all is blitted while byte_160C7C is clear");
    Check(RPadBandSame(170, 171, 69, 119), "the column bars are past the byte_160C7C gate, so they are absent");
    Check(RPadBandSame(10, 129, 50, 64), "and so is the header label");

    /* ---- 2. the two column bars, and the min() that caps them ---- */
    bBuilt = 1;
    bSelected = 0;
    FrameZero();
    ResearchScreen_Draw(&env);

    CheckEqI32(FramePix(170, 69), 0xB5B6, "the first bar is index 0xB4 at (170, 69)");
    CheckEqI32(FramePix(170, 118), 0xB5B6, "16 * min(4, 3) + 2 = 50 rows, so (170, 118) is the last");
    Check(RPadPix(170, 119) != 0xB5B6 && FramePix(170, 119) == RPadPix(170, 119),
          "(170, 119) falls back to the pad -- the bar is capped by nStyle, not nLineCount");
    Check(FramePix(170, 68) == RPadPix(170, 68), "and nothing is drawn above y 69");
    CheckEqI32(FramePix(200, 69), 0xB5B6, "the second bar sits at x 200");
    CheckEqI32(FramePix(200, 118), 0xB5B6, "with the same height");
    Check(FramePix(200, 119) == RPadPix(200, 119), "and the same end");
    Check(FramePix(171, 90) == RPadPix(171, 90), "the bar is one pixel wide, so x 171 is untouched");
    CheckEqI32(g_nFrameBlitCount, 1, "with no row selected the close control is the only control blitted");
    Check(g_nFrameBlitCount == 1 && g_aFrameBlitLog[0] == 5, "and it is the close control's pad");
    Check(RPadBandSame(10, 129, 116, 130), "no read-out label on that path");
    Check(RPadBandSame(195, 233, 120, 132), "and no read-out number either -- the bar column ends at y 118");

    g_aFramePanel[0].nLineCount = 1;
    g_aFramePanel[0].nStyle = 1; /* the bars become 16 * 1 + 2 = 18 tall from here on */
    FrameZero();
    ResearchScreen_Draw(&env);
    CheckEqI32(FramePix(170, 86), 0xB5B6, "with nLineCount 1 and nStyle 1 the bar ends at (170, 86)");
    Check(FramePix(170, 87) == RPadPix(170, 87), "and (170, 87) is the pad again");

    /* The panel cursor row does not change the bar -- only nLineCount and nStyle do. */
    nRow = 2;
    FrameZero();
    ResearchScreen_Draw(&env);
    CheckEqI32(FramePix(170, 86), 0xB5B6, "the bar height does not follow the cached row cursor");
    nRow = 0;

    /* ---- 3. a row selected, the detail animation not finished ---- */
    bSelected = 1;
    g_nFrameAnimProgress = 0;
    FrameZero();
    ResearchScreen_Draw(&env);

    CheckEqI32(g_nFrameAnimProgress, 0, "mode 4 at progress 0 is a 0 x 0 square, so the panel is not done");
    CheckEqI32(g_nFrameBlitCount, 0, "and nothing past it runs -- not even the close control");
    Check(RPadBandSame(10, 129, 116, 130), "the read-out label band stays as the bare pad");
    Check(RPadBandSame(195, 233, 162, 176), "so does the button-caption band");

    /* ---- 4. the animation finished: the panel, then the labels ---- */
    g_nFrameAnimProgress = 100;
    FrameZero();
    ResearchScreen_Draw(&env);

    CheckEqI32(g_nFrameAnimProgress, 100, "progress 100 clamps to the full 236 x 100 rect");
    Check(!RPadBandSame(10, 129, 116, 130), "the detail panel now covers the read-out band");
    CheckEqI32(g_nFrameBlitCount, 5, "two quantity controls, cancel, start and allocate-all");
    Check(g_nFrameBlitCount == 5 && g_aFrameBlitLog[0] == 0 && g_aFrameBlitLog[1] == 1
              && g_aFrameBlitLog[2] == 2 && g_aFrameBlitLog[3] == 3 && g_aFrameBlitLog[4] == 4,
          "in slot order: IncSelectedQty, DecSelectedQty, cancel, start, allocate-all");
    Check(g_nFrameBlitCount == 5 && g_aFrameBlitLog[0] != 5, "the close control is not blitted on this path");
    CheckEqI32(g_aFrameCtl[4].x, PUFO_RESEARCH_ALLOC_X, "the allocate-all control is moved to x 100");
    CheckEqI32(g_aFrameCtl[4].y, PUFO_RESEARCH_ALLOC_Y, "and y 164");
    Check(g_aFrameCtl[4].pfnClick == ResearchStubAlloc, "and its handler is the one the environment gives");

    {
        const RRefLabel aLabels[] = {
            { 10, 52, "RESEARCH", RREF_WHITE },
            { RCenterX(120, "ALPHA"), 13, "ALPHA", RREF_WHITE },
            { 10, 118, pszSci, RREF_WHITE },
        };
        RRefBuild(aLabels, 3, true);
        Check(RRefBandSame(10, 129, 52, 66), "the header is TEXTS[1138] at (10, 52)");
        Check(RRefBandSame(60, 180, 13, 27), "the site name is centred on x 120 at y 13");
        Check(RRefBandSame(10, 129, 118, 132), "the first read-out label is TEXTS[1139] at (10, 118)");
    }

    {
        const RRefLabel aWrong[] = { { 10, 118, "SC1:", RREF_WHITE } };
        RRefBuild(aWrong, 1, true);
        Check(!RRefBandSame(10, 129, 118, 132), "the band is not some other string, so the record index is pinned");
    }

    {
        const RRefLabel aLabels[] = { { 10, 132, pszLab, RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(10, 129, 132, 146), "TEXTS[1140] at (10, 132)");
    }
    {
        const RRefLabel aLabels[] = { { 10, 146, pszUse, RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(10, 129, 146, 160), "TEXTS[1141] at (10, 146)");
    }
    {
        const RRefLabel aLabels[] = { { 40, 164, "ADD", RREF_TEAL } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(40, 107, 164, 178), "TEXTS[1142] \"ADD\" at (40, 164), in the teal pair");
    }
    {
        const RRefLabel aLabels[] = { { 40, 184, "DEL", RREF_TEAL } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(40, 107, 184, 198), "TEXTS[1143] \"DEL\" at (40, 184)");
    }
    {
        const RRefLabel aLabels[] = { { 40, 164, "ADD", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(!RRefBandSame(40, 107, 164, 178), "the button captions are not the white pair -- 0x73E8C sets teal first");
    }

    /* ---- 5. the three numbers ---- */
    {
        const RRefLabel aLabels[] = { { 195, 118, "80", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(195, 233, 118, 132),
              "130 scientists on hand minus 50 allocated is 80, drawn at (195, 118)");
    }
    {
        const RRefLabel aLabels[] = { { 195, 132, "100", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(195, 233, 132, 146),
              "three free lab cells is 150, minus 50 allocated is 100, at (195, 132)");
    }
    {
        const RRefLabel aLabels[] = { { 195, 146, "42", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(195, 233, 146, 160),
              "the cached row 0 maps to topic 5, whose column entry is 42, at (195, 146)");
    }

    /* Each number follows its own input. */
    g_aFrameObj[113].auPerBaseVal[1] = 131;
    FrameZero();
    ResearchScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { 195, 118, "81", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(195, 233, 118, 132), "one more scientist on hand reads 81");
    }
    g_aFrameObj[113].auPerBaseVal[1] = 130;

    g_aFrameGrid[480 + 144 * 1 + 6 * 1 + 1] = 2; /* a fourth free lab: 200 - 50 = 150 */
    FrameZero();
    ResearchScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { 195, 132, "150", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(195, 233, 132, 146), "a fourth free lab cell reads 150");
    }
    g_aFrameGrid[480 + 144 * 1 + 6 * 1 + 1] = 0;

    UiPanel_SetLineAux(&g_aFramePanel[0], 0, 6); /* the cached row now names topic 6 */
    FrameZero();
    ResearchScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { 195, 146, "8", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(195, 233, 146, 160), "the number follows the cached row's aux topic");
    }
    UiPanel_SetLineAux(&g_aFramePanel[0], 0, 5);

    /* The column is the selected base's, not a fixed one. */
    nSelBase = 2;
    g_aFrameCol[144 * 2 + 5] = 7;
    FrameZero();
    ResearchScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { 195, 146, "7", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(195, 233, 146, 160), "base 2 reads its own 144-entry column");
    }
    nSelBase = 1;
    g_aFrameCol[144 * 2 + 5] = 0;
    FrameZero();
    ResearchScreen_Draw(&env);

    /* ---- 6. the three captions, and the centring ---- */
    {
        const RRefLabel aLabels[] = { { RCenterX(198, "CANCEL"), 189, "CANCEL", RREF_TEAL } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(160, 233, 189, 203), "TEXTS[1144] \"CANCEL\" is centred on x 198 at y 189");
    }
    {
        const RRefLabel aLabels[] = { { RCenterX(198, "CANCEL") + 8, 189, "CANCEL", RREF_TEAL } };
        RRefBuild(aLabels, 1, true);
        Check(!RRefBandSame(160, 233, 189, 203), "eight pixels off is not the same, so the centring is pinned");
    }
    {
        const RRefLabel aLabels[] = { { RCenterX(198, "OK"), 167, "OK", RREF_TEAL } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(160, 233, 167, 181), "TEXTS[1127] \"OK\" is centred on x 198 at y 167");
    }
    {
        const RRefLabel aLabels[] = { { RCenterX(119, "ALL"), 165, "ALL", RREF_TEAL } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(90, 160, 165, 179), "TEXTS[1604] \"ALL\" is centred on x 119 at y 165");
    }
    {
        const RRefLabel aLabels[] = { { RCenterX(119, "ALL"), 165, "ALL", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(!RRefBandSame(90, 160, 165, 179), "and it is still the teal pair, the last one set");
    }

    /* ---- 7. the guards the port adds ---- */
    nRow = 999; /* outside the panel's aux column */
    FrameZero();
    ResearchScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { 195, 146, "0", RREF_WHITE } };
        RRefBuild(aLabels, 1, true);
        Check(RRefBandSame(195, 233, 146, 160), "a row cursor outside the panel reads 0, not whatever is there");
    }
    nRow = 0;

    env.pPad = NULL;
    bBuilt = 0;
    bSelected = 0;
    FrameZero();
    ResearchScreen_Draw(&env);
    CheckEqI32(FramePix(5, 1), 0x632C, "with no pad the rails still draw");
    Check(FramePix(120, 170) == 0, "and the interior stays empty");
    env.pPad = &g_rPad;

    env.pPanelTopic = NULL;
    bBuilt = 1;
    bSelected = 0;
    FrameZero();
    ResearchScreen_Draw(&env);
    CheckEqI32(FramePix(170, 69), 0xB5B6, "with no topic panel the band is 0, so the bar is 0 + 2 tall");
    CheckEqI32(FramePix(170, 70), 0xB5B6, "two rows of bar");
    Check(FramePix(170, 71) == RPadPix(170, 71), "and (170, 71) is the pad again");
    env.pPanelTopic = &g_aFramePanel[0];

    for (i = 0; i < 3; ++i) {
        UiPanel_FreeLines(&g_aFramePanel[i]);
    }
    PadImage_FreeData(&g_rPad);
    FrameRestoreFont();
}

/* ---- BuySellScreen_Draw @ 0x77e6c ----------------------------------------------------------
 *
 * The biggest of the four screen frames, and the only one whose two halves are different drawings.
 * Checked on the real surface with the real font, pixel by pixel. The label strings are ASCII
 * stand-ins for the lang.dat records; the record NUMBERS are the claim, and they are pinned by the
 * address arithmetic in the header and cross-checked against lang.dat: 156 "Деньги: ", 157 "Итого",
 * 158 "Цена", 92 "НОК-во", 159 "Солдат", 405..408 the four "Нет ..." shortfall reasons, 411
 * "Выручка ", 412 "Уволить", 413 "Цена 1 ед. ", 414 "уволить", 1605 "Усыплен", 1127 "ОК". */

static UiPanel g_aBsPanel[5];
static Soldier g_aBsSoldier[2];
static Soldier *g_apBsSoldier[2];

/* A reference frame with just the named labels on an empty surface: every band compared against it
 * sits clear of the rails (x 5, x 234, y 5, y 315) and of the separator columns (x >= 130). */
static void BsRef(const RRefLabel *aLabels, i32 nLabels, bool bPanel)
{
    memset(g_frameRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_frameRef;
    if (bPanel) {
        (void)Ui_AnimExpandBlit_IfEnabled(&g_frameAnim, 20, 130, 200, 70, 1);
    }
    for (i32 i = 0; i < nLabels; ++i) {
        (void)Gfx_PackRgb565Pair(&g_frameColor, aLabels[i].nR1, aLabels[i].nG1, aLabels[i].nB1,
                                 aLabels[i].nR2, aLabels[i].nG2, aLabels[i].nB2);
        Text_DrawString(aLabels[i].x, aLabels[i].y, aLabels[i].psz);
    }
    g_pMainSurface = &g_frameSurf;
}

static void TestBuySellDraw(void)
{
    static u8 bBuilt, bCtlBuilt, bScratch, bOnRow, bPreviewOk;
    static u32 nShortfall;
    static i32 nReason, nRow, nCursorY, nFunds, nTotal;
    static i8 nSelBase;
    BuySellDrawEnv env;
    const i32 nBand = 48; /* 16 * min(4, 3), the panel B band every gate below uses */
    i32 i;

    printf("the buy/sell screen's frame\n");

    FrameLoadFont();
    if (!g_bFrameFontReady) {
        printf("  FAIL the real font did not load, so the frame cannot be checked\n");
        ++g_nFailures;
        return;
    }

    SoftSurf_ctor(&g_frameSurf);
    (void)SoftSurf_AllocPixels(&g_frameSurf, FRAME_W, FRAME_H, PIXMODE_16BPP);
    SoftSurf_ctor(&g_frameRef);
    (void)SoftSurf_AllocPixels(&g_frameRef, FRAME_W, FRAME_H, PIXMODE_16BPP);
    g_pMainSurface = &g_frameSurf;
    g_blitState.nPixelMode = 2;

    memset(g_aFrameText, 0, sizeof(g_aFrameText));
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_FUNDS, "FUNDS: ");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_TOTAL_DOWN, "TOTAL");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_TOTAL_UP, "REVENUE ");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_COL_LEFT, "QTY");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_COL_RIGHT, "PRICE");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_SOLDIER, "SOLDIER");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_SOL_NAME, "DISMISS");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_PRICE, "PRICE 1 ");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_CRAFT, "dismiss");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_ALIEN, "SEDATED");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_BUYSELL_TEXT_OK, "OK");
    for (i = 0; i < 4; ++i) {
        PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * (PUFO_BUYSELL_TEXT_SHORTFALL + i),
               (i == 0) ? "NO SPACE" : (i == 1) ? "NO MONEY" : (i == 2) ? "NO HANGAR" : "NO STORES");
    }

    memset(g_aFrameObj, 0, sizeof(g_aFrameObj));
    memset(g_apFrameObj, 0, sizeof(g_apFrameObj));
    for (i = 0; i < 149; ++i) {
        g_apFrameObj[i] = &g_aFrameObj[i];
    }
    PutStr(g_aFrameObj[5].szName, "ELERIUM");
    g_aFrameObj[5].uPrice = 1234u;
    PutStr(g_aFrameObj[77].szName, "ALIEN-X");
    g_aFrameObj[77].uPrice = 7u;
    PutStr(g_aFrameObj[117].szName, "SKYRANGER");
    g_aFrameObj[117].uPrice = 99u;

    memset(g_aBsSoldier, 0, sizeof(g_aBsSoldier));
    PutStr(g_aBsSoldier[0].szName, "SMITH");
    PutStr(g_aBsSoldier[1].szName, "JONES");
    g_apBsSoldier[0] = &g_aBsSoldier[0];
    g_apBsSoldier[1] = &g_aBsSoldier[1];

    memset(g_aFrameSite, 0, sizeof(g_aFrameSite));
    for (i = 0; i < 8; ++i) {
        PutStr(g_aFrameSite[i].abName, "NORTH");
    }
    PutStr(g_aFrameSite[1].abName, "ALPHA");

    /* Five panels, parked at (10, 200): no row of theirs lands on a label band or a separator column. */
    for (i = 0; i < 5; ++i) {
        if (!UiPanel_ctor(&g_aBsPanel[i], 4)) {
            printf("  FAIL a buy/sell panel could not be constructed\n");
            ++g_nFailures;
            return;
        }
        g_aBsPanel[i].nLineCount = 4;
        g_aBsPanel[i].nStyle = 3;
        g_aBsPanel[i].nPosX = 10;
        g_aBsPanel[i].nPosY = 200;
        g_aBsPanel[i].bSelLineHighlight = 5;  /* a value the frame could not produce, to see who writes it */
        g_aBsPanel[i].bRepeatArmed = 6;   /* same, for the scratch byte */
    }
    /* Panel A's row markers, and panel B's soldier index for the two soldier rows. Row 4 is outside
     * the drawn rows but the cursor reads anLineAux by index, not by nLineCount. */
    UiPanel_SetLineAux(&g_aBsPanel[0], 0, PUFO_ROW_SOLDIER);
    UiPanel_SetLineAux(&g_aBsPanel[0], 1, PUFO_ROW_SOLDIER);
    UiPanel_SetLineAux(&g_aBsPanel[0], 2, 77 + PUFO_ROW_ALIEN_OFFSET);
    UiPanel_SetLineAux(&g_aBsPanel[0], 3, 5);
    UiPanel_SetLineAux(&g_aBsPanel[0], 4, PUFO_ROW_CRAFT_FIRST);
    UiPanel_SetLineAux(&g_aBsPanel[1], 0, 0); /* apSoldiers[0] */
    UiPanel_SetLineAux(&g_aBsPanel[1], 1, 1); /* apSoldiers[1] */

    memset(g_aFramePads, 0, sizeof(g_aFramePads));
    memset(g_aFrameCtl, 0, sizeof(g_aFrameCtl));
    for (i = 0; i < 2; ++i) {
        g_aFrameCtl[i].bEnabled = 1;
        g_aFrameCtl[i].nMode = 0;
        g_aFrameCtl[i].nState = 0;
        g_aFrameCtl[i].x = 6;
        g_aFrameCtl[i].y = 260;
        g_aFrameCtl[i].apPads[0] = &g_aFramePads[i];
    }

    g_bFrameAnimStarted = 1;
    g_nFrameAnimProgress = 100;
    memset(&g_frameAnim, 0, sizeof(g_frameAnim));
    g_frameAnim.pbStarted = &g_bFrameAnimStarted;
    g_frameAnim.pnProgress = &g_nFrameAnimProgress;
    g_frameAnim.nFrameStyle = 0;
    g_frameColor.pnGamma = &g_nFrameGamma;
    g_frameColor.nFadeCount = 0;

    bBuilt = 0;
    bCtlBuilt = 0;
    bScratch = 3;
    bOnRow = 9; /* not 0 and not 1, so a write is visible */
    bPreviewOk = 1;
    nShortfall = 0;
    nReason = 0;
    nRow = 0;
    nCursorY = 90; /* inside 75 < y < 75 + 48 */
    nFunds = 1234;
    nTotal = 500;
    nSelBase = 1;

    memset(&env, 0, sizeof(env));
    env.pColor = &g_frameColor;
    env.pPanelA = &g_aBsPanel[0];
    env.pPanelB = &g_aBsPanel[1];
    env.pPanelC = &g_aBsPanel[2];
    env.pPanelD = &g_aBsPanel[3];
    env.pPanelE = &g_aBsPanel[4];
    env.pbPanelsBuilt = &bBuilt;
    env.pbControlsBuilt = &bCtlBuilt;
    env.pbCursorOnRow = &bOnRow;
    env.pbPanelFlagScratch = &bScratch;
    env.pnRowCursor = &nRow;
    env.pnSelectedBaseIdx = &nSelBase;
    env.pTextRecs = g_aFrameText;
    env.apSiteRecs = g_aFrameSite;
    env.apObjects = g_apFrameObj;
    env.nObjectCount = 149;
    env.apSoldiers = g_apBsSoldier;
    env.nSoldierCount = 2;
    env.pnFunds = &nFunds;
    env.pnTransTotal = &nTotal;
    env.pbPreviewOk = &bPreviewOk;
    env.pnCursorY = &nCursorY;
    env.pdwShortfall = &nShortfall;
    env.pnShortfallReason = &nReason;
    env.pAnim = &g_frameAnim;
    env.pCtlClose = &g_aFrameCtl[0];
    env.pCtlOk = &g_aFrameCtl[1];
    env.pfnCtlBlit = FrameBlitRec;
    env.pfnCtlBlitEx = FrameBlitRec;

    /* ---- 1. nothing built: rails, the close control, the funds line, the site name ---- */
    FrameZero();
    BuySellScreen_Draw(&env);

    CheckEqI32(FramePix(5, 1), 0x632C, "the shared rails draw first, index 100 at (5, 1)");
    CheckEqI32(FramePix(234, 312), 0x738E, "and the side rail's last falling step at (234, 312)");
    CheckEqI32(g_nFrameBlitCount, 1, "the close control is blitted unconditionally, unlike the research frame");
    Check(g_nFrameBlitCount == 1 && g_aFrameBlitLog[0] == 0, "and it is the close control's pad");

    {
        const RRefLabel aLabels[] = { { 10, 28, "FUNDS: 1234 $", 120, 255, 120, 10, 10, 10 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(10, 129, 26, 40), "TEXTS[156] + itoa(nFunds) + \" $\" at (10, 28)");
    }
    {
        const RRefLabel aLabels[] = { { 10, 28, "FUNDS: 1235 $", 120, 255, 120, 10, 10, 10 } };
        BsRef(aLabels, 1, false);
        Check(!RRefBandSame(10, 129, 26, 40), "one dollar more is a different band, so the funds value is pinned");
    }
    {
        const RRefLabel aLabels[] = { { RCenterX(120, "ALPHA"), 10, "ALPHA", 180, 180, 180, 10, 10, 10 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(60, 180, 10, 24), "the site name is centred on x 120 at y 10");
    }
    Check(FrameBandEmpty(10, 129, 42, 56), "no total line while the controls are not built");
    Check(FrameBandEmpty(130, 233, 52, 68), "no column captions either");
    CheckEqI32(FramePix(130, 69), 0, "and no column separators");

    /* ---- 2. controls built, panels not built. The cursor is parked outside the gate here so the
     * OK tail is visible; section 3 moves it inside. ---- */
    bCtlBuilt = 1;
    bScratch = 3;
    bOnRow = 9;
    nCursorY = 60; /* not above 75 */
    g_aBsPanel[1].bSelLineHighlight = 5;
    g_aBsPanel[2].bSelLineHighlight = 5;
    g_aBsPanel[1].bRepeatArmed = 6;
    g_aBsPanel[2].bRepeatArmed = 6;
    FrameZero();
    BuySellScreen_Draw(&env);

    {
        const RRefLabel aLabels[] = { { 10, 40, "TOTAL:500 $", 255, 120, 120, 10, 10, 10 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(10, 129, 42, 56), "TEXTS[157] + \":\" + itoa(total) + \" $\" at (10, 40), in the red pair");
    }
    {
        const RRefLabel aLabels[] = { { 10, 40, "REVENUE :500 $", 255, 120, 120, 10, 10, 10 } };
        BsRef(aLabels, 1, false);
        Check(!RRefBandSame(10, 129, 42, 56), "the other half's total record is not used here");
    }
    {
        const RRefLabel aLabels[] = {
            { RCenterX(210, "QTY"), 54, "QTY", 180, 180, 250, 10, 10, 10 },
            { RCenterX(155, "PRICE"), 54, "PRICE", 180, 180, 250, 10, 10, 10 },
        };
        BsRef(aLabels, 2, false);
        Check(RRefBandSame(130, 233, 52, 68), "TEXTS[92] centred on 210 and TEXTS[158] on 155 in this half");
    }
    {
        const RRefLabel aLabels[] = {
            { RCenterX(145, "QTY"), 54, "QTY", 180, 180, 250, 10, 10, 10 },
            { RCenterX(212, "PRICE"), 54, "PRICE", 180, 180, 250, 10, 10, 10 },
        };
        BsRef(aLabels, 2, false);
        Check(!RRefBandSame(130, 233, 52, 68), "the built half's caption positions are the other pair");
    }

    CheckEqI32(g_aBsPanel[1].bSelLineHighlight, 1, "panels B and C get bSelLineHighlight = (shortfall dword != 1) here");
    CheckEqI32(g_aBsPanel[2].bSelLineHighlight, 1, "both of them");
    CheckEqI32(g_aBsPanel[1].bRepeatArmed, 3, "and bRepeatArmed takes the scratch byte's value");
    CheckEqI32(g_aBsPanel[2].bRepeatArmed, 3, "on both panels");
    CheckEqI32(bScratch, 0, "the scratch byte is zeroed after the copy");
    CheckEqI32(bOnRow, 0, "and the cursor flag is cleared outside the cursor gate");

    CheckEqI32(FramePix(130, 69), 0xB5B6, "the first separator is index 0xB4 at (130, 69)");
    CheckEqI32(FramePix(176, 69), 0xB5B6, "the second at x 176 in this half");
    CheckEqI32(FramePix(191, 69), 0xB5B6, "the third at x 191");
    CheckEqI32(FramePix(206, 69), 0xB5B6, "the fourth at x 206");
    CheckEqI32(FramePix(160, 69), 0, "x 160 has no separator on this path");
    CheckEqI32(FramePix(175, 69), 0, "nor x 175");
    CheckEqI32(FramePix(190, 69), 0, "nor x 190");
    CheckEqI32(FramePix(130, 69 + nBand + 1), 0xB5B6, "the separators are 16 * min(4, 3) + 2 = 50 tall, so (130, 118) is the last row");
    CheckEqI32(FramePix(130, 69 + nBand + 2), 0, "and (130, 119) is outside them");
    CheckEqI32(FramePix(130, 68), 0, "they start at y 69");

    CheckEqI32(g_nFrameBlitCount, 2, "the close control and the OK control");
    Check(g_nFrameBlitCount == 2 && g_aFrameBlitLog[1] == 1, "the OK control is the second blit");
    {
        const RRefLabel aLabels[] = { { RCenterX(120, "OK"), 297, "OK", 68, 244, 252, 4, 70, 75 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(10, 129, 297, 313), "TEXTS[1127] centred on x 120 at y 297");
    }

    /* ---- 3. the cursor gate, panels not built ---- */
    bOnRow = 9;
    nCursorY = 90;
    FrameZero();
    BuySellScreen_Draw(&env);
    CheckEqI32(bOnRow, 1, "cursor y 90 is inside 75 < y < 75 + 48, so the flag is set");
    {
        const RRefLabel aLabels[] = { { 10, 297, "SOLDIER", 150, 150, 255, 20, 20, 55 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(10, 129, 297, 313), "a soldier row gets TEXTS[159] at (10, 297) in this half");
    }
    Check(FrameBandEmpty(10, 129, 279, 295), "and no name line at (10, 281) here");
    CheckEqI32(g_nFrameBlitCount, 1, "the OK control is not blitted when the cursor is on a row");

    nRow = 3; /* panel A aux 5, a plain object row */
    FrameZero();
    BuySellScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { 10, 297, "ELERIUM", 150, 150, 255, 20, 20, 55 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(10, 129, 297, 313), "any other marker names its object record instead");
    }
    /* 0x78B80 is the marker used raw: this half never subtracts the 2000 alien offset, so an alien
     * row would deref apObjects[2077]. The port bounds it and draws nothing, which is the recorded
     * deviation -- the built half does subtract it and reads slot 77. */
    nRow = 2;
    FrameZero();
    BuySellScreen_Draw(&env);
    Check(FrameBandEmpty(10, 129, 297, 313), "and an alien row in this half names nothing (deviation)");
    nRow = 0;

    nCursorY = 75;
    FrameZero();
    BuySellScreen_Draw(&env);
    CheckEqI32(bOnRow, 0, "y 75 is not above 75, so the gate fails and the flag clears");
    CheckEqI32(g_nFrameBlitCount, 2, "and the OK control comes back");
    nCursorY = 75 + nBand;
    FrameZero();
    BuySellScreen_Draw(&env);
    CheckEqI32(bOnRow, 0, "y 75 + band is outside too -- the compare is strict");
    nCursorY = 90;
    bPreviewOk = 0;
    FrameZero();
    BuySellScreen_Draw(&env);
    CheckEqI32(bOnRow, 0, "and a base preview that is not ok stops the gate as well");
    bPreviewOk = 1;

    /* ---- 4. the shortfall panel, this half only ---- */
    for (i = 0; i < 4; ++i) {
        nShortfall = 1;
        nReason = i;
        bOnRow = 9;
        FrameZero();
        BuySellScreen_Draw(&env);
        {
            char szRef[16];
            const char *const apsz[] = { "NO SPACE", "NO MONEY", "NO HANGAR", "NO STORES" };
            (void)snprintf(szRef, sizeof(szRef), "%s", apsz[i]);
            const RRefLabel aLabels[] = { { RCenterX(120, szRef), 158, szRef, 220, 80, 80, 50, 30, 30 } };
            BsRef(aLabels, 1, true);
            Check(RRefBandSame(60, 180, 156, 172), "reason 0..3 opens the 200x70 panel and names it");
        }
        CheckEqI32(bOnRow, 9, "and the shortfall return never reaches the cursor block");
        CheckEqI32(g_nFrameBlitCount, 1, "nor blits the OK control");
    }
    nShortfall = 1;
    nReason = 4;
    bOnRow = 9;
    FrameZero();
    BuySellScreen_Draw(&env);
    CheckEqI32(bOnRow, 1, "reason 4 falls through the switch into the cursor block");
    Check(FrameBandEmpty(60, 180, 156, 172), "and the 0x789D0 default breaks, so no panel opens for it");
    nShortfall = 0;
    nReason = 0;

    /* ---- 5. panels built: the other half ---- */
    bBuilt = 1;
    bScratch = 4;
    bOnRow = 9;
    g_aBsPanel[1].bSelLineHighlight = 5;
    g_aBsPanel[2].bSelLineHighlight = 5;
    g_aBsPanel[1].bRepeatArmed = 6;
    g_aBsPanel[2].bRepeatArmed = 6;
    FrameZero();
    BuySellScreen_Draw(&env);

    {
        const RRefLabel aLabels[] = { { 10, 40, "REVENUE :500 $", 120, 120, 255, 10, 10, 10 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(10, 129, 42, 56), "TEXTS[411] + \":\" + itoa(total) + \" $\", in the blue pair");
    }
    {
        const RRefLabel aLabels[] = {
            { RCenterX(145, "QTY"), 54, "QTY", 180, 180, 250, 10, 10, 10 },
            { RCenterX(212, "PRICE"), 54, "PRICE", 180, 180, 250, 10, 10, 10 },
        };
        BsRef(aLabels, 2, false);
        Check(RRefBandSame(130, 233, 52, 68), "TEXTS[92] centred on 145 and TEXTS[158] on 212 here");
    }
    CheckEqI32(g_aBsPanel[1].bSelLineHighlight, 5, "this half never writes bSelLineHighlight");
    CheckEqI32(g_aBsPanel[2].bSelLineHighlight, 5, "on either panel");
    CheckEqI32(g_aBsPanel[1].bRepeatArmed, 4, "but the scratch byte still lands on both panels");
    CheckEqI32(bScratch, 0, "and is still zeroed");
    CheckEqI32(bOnRow, 9, "and the cursor flag is untouched by this half, gate or no gate");

    CheckEqI32(FramePix(160, 69), 0xB5B6, "the second separator moves to x 160");
    CheckEqI32(FramePix(175, 69), 0xB5B6, "the third to x 175");
    CheckEqI32(FramePix(190, 69), 0xB5B6, "the fourth to x 190");
    CheckEqI32(FramePix(176, 69), 0, "x 176 is empty on this path");
    CheckEqI32(FramePix(206, 69), 0, "and so is x 206");

    nShortfall = 1;
    nReason = 0;
    FrameSnapshot();
    FrameZero();
    BuySellScreen_Draw(&env);
    Check(FrameBandSame(60, 180, 156, 172), "the shortfall panel does not exist on this half");
    nShortfall = 0;

    /* ---- 6. the cursor block, panels built ---- */
    nRow = 0; /* soldier row: panel A marker 1000, panel B aux 0 */
    FrameZero();
    BuySellScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = {
            { 10, 281, "SMITH", 150, 150, 255, 20, 20, 55 },
            { 10, 297, "DISMISS", 150, 150, 255, 20, 20, 55 },
        };
        BsRef(aLabels, 2, false);
        Check(RRefBandSame(10, 129, 279, 295), "the soldier name comes from apSoldiers[panel B's aux]");
        Check(RRefBandSame(10, 129, 297, 313), "with TEXTS[412] under it");
    }
    nRow = 1; /* panel B aux 1 -> the second soldier */
    FrameZero();
    BuySellScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { 10, 281, "JONES", 150, 150, 255, 20, 20, 55 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(10, 129, 279, 295), "the soldier index really is panel B's, not panel A's");
    }
    nRow = 2; /* panel A marker 2077 = slot 77 + 2000 */
    FrameZero();
    BuySellScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = {
            { 10, 281, "ALIEN-X", 150, 150, 255, 20, 20, 55 },
            { 10, 297, "SEDATED", 150, 150, 255, 20, 20, 55 },
        };
        BsRef(aLabels, 2, false);
        Check(RRefBandSame(10, 129, 279, 295), "an alien row names apObjects[marker - 2000]");
        Check(RRefBandSame(10, 129, 297, 313), "with TEXTS[1605] under it");
    }
    nRow = 3; /* plain object row, slot 5 */
    FrameZero();
    BuySellScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = {
            { 10, 281, "ELERIUM", 150, 150, 255, 20, 20, 55 },
            { 10, 297, "PRICE 1 1234", 150, 150, 255, 20, 20, 55 },
        };
        BsRef(aLabels, 2, false);
        Check(RRefBandSame(10, 129, 279, 295), "a plain row names its object");
        Check(RRefBandSame(10, 129, 297, 313), "and quotes TEXTS[413] plus ObjectRecFull.uPrice");
    }
    g_aFrameObj[5].uPrice = 1235u;
    FrameZero();
    BuySellScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { 10, 297, "PRICE 1 1234", 150, 150, 255, 20, 20, 55 } };
        BsRef(aLabels, 1, false);
        Check(!RRefBandSame(10, 129, 297, 313), "the price really comes from the record");
    }
    g_aFrameObj[5].uPrice = 1234u;

    nRow = 4; /* craft row 117 */
    FrameZero();
    BuySellScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = {
            { 10, 281, "SKYRANGER", 150, 150, 255, 20, 20, 55 },
            { 10, 297, "dismiss", 150, 150, 255, 20, 20, 55 },
        };
        BsRef(aLabels, 2, false);
        Check(RRefBandSame(10, 129, 279, 295), "the craft row names its object too");
        Check(RRefBandSame(10, 129, 297, 313), "but 117..121 get TEXTS[414] instead of the price");
    }
    nRow = 3;
    {
        const RRefLabel aLabels[] = { { 10, 297, "dismiss", 150, 150, 255, 20, 20, 55 } };
        BsRef(aLabels, 1, false);
        FrameZero();
        BuySellScreen_Draw(&env);
        Check(!RRefBandSame(10, 129, 297, 313), "which a plain row does not get");
    }

    nRow = 999; /* the cursor outside the panel */
    FrameZero();
    BuySellScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { 10, 281, "", 150, 150, 255, 20, 20, 55 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(10, 129, 279, 295), "a cursor outside the panel draws no name");
    }
    nRow = 0;

    nCursorY = 75 + nBand;
    FrameZero();
    BuySellScreen_Draw(&env);
    {
        const RRefLabel aLabels[] = { { RCenterX(120, "OK"), 297, "OK", 68, 244, 252, 4, 70, 75 } };
        BsRef(aLabels, 1, false);
        Check(RRefBandSame(10, 129, 297, 313), "outside the gate this half draws the OK caption too");
    }
    CheckEqI32(g_nFrameBlitCount, 2, "close and OK, the same tail as the other half");
    CheckEqI32(bOnRow, 9, "and still no write to the cursor flag");
    nCursorY = 90;

    for (i = 0; i < 5; ++i) {
        UiPanel_FreeLines(&g_aBsPanel[i]);
    }
    FrameRestoreFont();
}

/* ---- ResearchScreen_Init @ 0x749a8 ---------------------------------------------------------
 *
 * The bring-up is mostly wiring, so the test checks the wiring and the ORDER of the wiring: two of
 * the hooks record what the screen looked like when they ran, which is what proves the rebuild runs
 * before the topic walk and the panel refresh runs after the panel build. */

static UiControl g_aResInitCtl[4];
static UiControl g_resClose, g_resCancel, g_resStart;
static u16 g_aResCol[144 * 8];
static UiPanel g_aResPanel[3];
static i32 g_nResHookRebuild, g_nResHookBuild;
static i32 g_nResCancelXAtRebuild, g_nResCancelYAtRebuild, nResPanelLinesAtBuild;

/* Seven env handlers, seven ids. The init claims one handler per slot (panel prev, panel next,
 * quantity +, quantity -) plus the close/cancel/start controls; with seven EMPTY bodies MSVC's
 * /OPT:ICF folds them into a single address and every one of those claims compares a pointer with
 * itself. */
static i32 g_nResStubId;
static void ResStubClose(void)     { g_nResStubId = 1; }
static void ResStubCancel(void)    { g_nResStubId = 2; }
static void ResStubStart(void)     { g_nResStubId = 3; }
static void ResStubPanelPrev(void) { g_nResStubId = 4; }
static void ResStubPanelNext(void) { g_nResStubId = 5; }
static void ResStubIncQty(void)    { g_nResStubId = 6; }
static void ResStubDecQty(void)    { g_nResStubId = 7; }

static void ResHookRebuild(void)
{
    ++g_nResHookRebuild;
    g_nResCancelXAtRebuild = g_resCancel.x;
    g_nResCancelYAtRebuild = g_resCancel.y;
    g_aResCol[144 + 0] = 9; /* topic 0 is inactive: the walk that follows must clear this */
}

static void ResHookBuildPanels(void)
{
    ++g_nResHookBuild;
    /* The refresh has not run yet, so the panels still carry the value the test preset. */
    nResPanelLinesAtBuild = g_aResPanel[0].nLineCount;
}

static bool ResLoadPadsReal(UiControl *pCtl, const char *psz0, const char *psz1,
                            const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2);
    return true;
}

static void TestResearchInit(void)
{
    static void *aRoot[5];
    static ResearchTopicLive aTopic[144];
    static u8 bEntryCount;
    static i8 nSelBase;
    ResearchInitEnv env;
    PadImage *pPad;
    i32 i;

    printf("the research screen's bring-up\n");

    const char *const apszArch[2] = { "lang.dat", "main.dat" };
    if (!Asset_OpenArchives(apszArch)) {
        printf("  FAIL main.dat / lang.dat did not open\n");
        ++g_nFailures;
        return;
    }

    bEntryCount = 8;
    nSelBase = 1;
    g_nResHookRebuild = 0;
    g_nResHookBuild = 0;
    g_nResCancelXAtRebuild = -1;
    g_nResCancelYAtRebuild = -1;
    nResPanelLinesAtBuild = -1;

    memset(aRoot, 0, sizeof(aRoot));
    memset(g_aResInitCtl, 0, sizeof(g_aResInitCtl));
    memset(&g_resClose, 0, sizeof(g_resClose));
    memset(&g_resCancel, 0, sizeof(g_resCancel));
    memset(&g_resStart, 0, sizeof(g_resStart));
    memset(aTopic, 0, sizeof(aTopic));
    memset(g_aResCol, 0, sizeof(g_aResCol));

    /* Eight topics in the walk: 1, 3, 4 and 6 are on the screen list, 0, 2, 5 and 7 are not. */
    aTopic[1].bActive = 1;
    aTopic[3].bActive = 1;
    aTopic[4].bActive = 1;
    aTopic[6].bActive = 1;
    aTopic[1].wTotal = 100; aTopic[1].wProgress = 50; /* 50% */
    aTopic[4].wTotal = 4;   aTopic[4].wProgress = 1;  /* 25% */

    /* Every entry of base 1's column, plus one past the walk's bound, plus base 0's first entry. */
    for (i = 0; i < 9; ++i) {
        g_aResCol[144 + i] = 7;
    }
    g_aResCol[0] = 7;

    for (i = 0; i < 3; ++i) {
        if (!UiPanel_ctor(&g_aResPanel[i], 8)) {
            printf("  FAIL a research panel could not be constructed\n");
            ++g_nFailures;
            return;
        }
        g_aResPanel[i].nLineCount = 99; /* so the refresh's own value is distinguishable */
    }

    /* The cancel control starts parked somewhere obvious, so the late move is visible. */
    g_resCancel.x = 11;
    g_resCancel.y = 12;

    memset(&env, 0, sizeof(env));
    env.ppRoot = aRoot;
    for (i = 0; i < 4; ++i) {
        env.apCtl[i] = &g_aResInitCtl[i];
    }
    env.pCloseCtl = &g_resClose;
    env.pCancelCtl = &g_resCancel;
    env.pStartCtl = &g_resStart;
    env.pfnCloseClick = ResStubClose;
    env.pfnCancelClick = ResStubCancel;
    env.pfnStartClick = ResStubStart;
    env.pfnPanelPrev = ResStubPanelPrev;
    env.pfnPanelNext = ResStubPanelNext;
    env.pfnIncQty = ResStubIncQty;
    env.pfnDecQty = ResStubDecQty;
    env.pfnRebuildTopicList = ResHookRebuild;
    env.pfnBuildPanels = ResHookBuildPanels;
    env.aTopics = aTopic;
    env.pbEntryCount = &bEntryCount;
    env.apResearchCols = g_aResCol;
    env.pnSelectedBaseIdx = &nSelBase;
    env.pPanelTopic = &g_aResPanel[0];
    env.pPanelQty = &g_aResPanel[1];
    env.pPanelProgress = &g_aResPanel[2];
    env.pfnLoadPads = ResLoadPadsReal;

    ResearchScreen_Init(&env);

    /* Step 1: the screen pad. */
    Check(aRoot[0] != NULL, "the research screen's pad object is allocated");
    pPad = (PadImage *)aRoot[0];
    Check(pPad != NULL && pPad->pData != NULL, "Research.pad really loads out of main.dat");
    Check(pPad != NULL && pPad->uWidth == 240 && pPad->uHeight == 320,
          "Research.pad is the 240 x 320 member");
    Check(pPad != NULL && pPad->nMode == 6, "loaded in mode 6, the MOV R2,#6 at 0x749DC");
    Check(pPad != NULL && pPad->nOriginX == 0 && pPad->nOriginY == 0, "and both origins are zeroed");

    /* Steps 2 and 3: the four controls and their handlers. */
    for (i = 0; i < 4; ++i) {
        Check(aRoot[i + 1] == (void *)&g_aResInitCtl[i], "the four controls land in slots 1..4");
    }
    Check(g_aResInitCtl[0].pfnClick == ResStubPanelPrev, "slot 1 scrolls the topic list back");
    Check(g_aResInitCtl[1].pfnClick == ResStubPanelNext, "slot 2 scrolls it forward");
    Check(g_aResInitCtl[2].pfnClick == ResStubIncQty, "slot 3 is the quantity +");
    Check(g_aResInitCtl[3].pfnClick == ResStubDecQty, "slot 4 is the quantity -");

    /* Step 4: the close control, same place the manufacturing bring-up puts it. */
    CheckEqI32(g_resClose.x, 224, "the close control sits at x 224");
    CheckEqI32(g_resClose.y, 1, "and y 1");
    Check(g_resClose.pfnClick == ResStubClose, "with the research close thunk");

    /* Step 6: the four descriptors, both bases zero. */
    CheckEqI32(g_aResInitCtl[0].x, 0xCD, "the topic scroll pair sits at x 0xCD");
    CheckEqI32(g_aResInitCtl[0].y, 0x12, "the up arrow at y 0x12");
    CheckEqI32(g_aResInitCtl[1].x, 0xCD, "the down arrow shares the column");
    CheckEqI32(g_aResInitCtl[1].y, 0x26, "at y 0x26");
    CheckEqI32(g_aResInitCtl[2].x, 0x0A, "the quantity pair sits at x 10");
    CheckEqI32(g_aResInitCtl[2].y, 0xA3, "the up arrow at y 163");
    CheckEqI32(g_aResInitCtl[3].x, 0x0A, "the down arrow shares the column");
    CheckEqI32(g_aResInitCtl[3].y, 0xB7, "at y 183");

    /* Step 7 before step 8: the rebuild ran, and the bottom controls were not positioned yet. */
    CheckEqI32(g_nResHookRebuild, 1, "the topic rebuild runs once, before the panel build");
    CheckEqI32(g_nResCancelXAtRebuild, 11, "and the cancel control is still parked when it runs");
    CheckEqI32(g_nResCancelYAtRebuild, 12, "on both coordinates");

    /* Step 8: the walk clears the inactive topics' allocations, for this base only. */
    CheckEqI32(g_aResCol[144 + 0], 0, "topic 0 is inactive, so its allocation is cleared (0x74B68)");
    CheckEqI32(g_aResCol[144 + 1], 7, "topic 1 is active and keeps its 7");
    CheckEqI32(g_aResCol[144 + 2], 0, "topic 2 is inactive");
    CheckEqI32(g_aResCol[144 + 3], 7, "topic 3 keeps its 7");
    CheckEqI32(g_aResCol[144 + 4], 7, "topic 4 keeps its 7");
    CheckEqI32(g_aResCol[144 + 5], 0, "topic 5 is inactive");
    CheckEqI32(g_aResCol[144 + 6], 7, "topic 6 keeps its 7");
    CheckEqI32(g_aResCol[144 + 7], 0, "topic 7 is inactive");
    CheckEqI32(g_aResCol[144 + 8], 7, "the walk stops at the count byte, so entry 8 survives");
    CheckEqI32(g_aResCol[0], 7, "and only the selected base's column is touched");

    /* Step 9: the two bottom controls, handlers first and positions last. */
    Check(g_resCancel.pfnClick == ResStubCancel, "the cancel control keeps its handler");
    Check(g_resStart.pfnClick == ResStubStart, "and the start control its own");
    CheckEqI32(g_resCancel.x, 165, "the cancel control is moved to x 165 at 0x74B98");
    CheckEqI32(g_resCancel.y, 188, "and y 188");
    CheckEqI32(g_resStart.x, 165, "the start control shares the column");
    CheckEqI32(g_resStart.y, 166, "one row above, at y 166");

    /* Step 10: the panel build, then the refresh. */
    CheckEqI32(g_nResHookBuild, 1, "the panel build runs once");
    CheckEqI32(nResPanelLinesAtBuild, 99, "the panel refresh runs after the build, not before it");
    CheckEqI32(g_aResPanel[0].nLineCount, 4, "the refresh counts four active topics into panel 1");
    CheckEqI32(g_aResPanel[1].nLineCount, 4, "and writes the same count into panel 2");
    CheckEqI32(g_aResPanel[2].nLineCount, 4, "and into panel 3");
    Check( strcmp(UiPanel_GetLineText(&g_aResPanel[1], 0), "7") == 0,
           "the quantity column shows the allocation of the first active topic");
    Check( strcmp(UiPanel_GetLineText(&g_aResPanel[2], 0), "50%") == 0,
           "its progress is 100 * 50 / 100");
    Check( strcmp(UiPanel_GetLineText(&g_aResPanel[2], 1), "") == 0,
           "a topic with no progress gets the empty string");
    Check( strcmp(UiPanel_GetLineText(&g_aResPanel[2], 2), "25%") == 0,
           "and 100 * 1 / 4 is 25");

    /* A missing environment does nothing. */
    ResearchScreen_Init(NULL);
    CheckEqI32(g_nResHookRebuild, 1, "a null environment calls no hook");
    env.ppRoot = NULL;
    ResearchScreen_Init(&env);
    CheckEqI32(g_nResHookBuild, 1, "and a null root stops it before the pad is allocated");

    for (i = 0; i < 3; ++i) {
        UiPanel_FreeLines(&g_aResPanel[i]);
    }
}

/* ---- ResearchScreen_BuildPanels @ 0x733f4 --------------------------------------------------
 *
 * The build/teardown pair, and the percentage that this function computes the other way round from
 * ResearchScreen_RefreshPanels. The fixture makes that visible: one topic at one quarter done. */

static u8 g_bResBuilt;
static void *g_aResBuildRoot[5];
static UiControl g_aResBuildCtl[4];
static PadImage g_aResBuildPad[4];
static UiPanel *g_pResPanelA, *g_pResPanelB, *g_pResPanelC;
static u16 g_aResColB[144 * 8];
static char g_aResText[8 * PUFO_TEXT_REC_STRIDE];
static ResearchTopicLive g_aResTopicB[144];
static u8 g_bResCount;
static i8 g_nResBase;

static void ResStubPanelClick(void) { g_nResStubId = 8; }

static bool ResLineIs(const UiPanel *pPanel, i32 nRow, const char *pszWant)
{
    const char *psz = (pPanel != NULL) ? UiPanel_GetLineText(pPanel, nRow) : NULL;
    return psz != NULL && strcmp(psz, pszWant) == 0;
}

static void TestResearchBuildPanels(void)
{
    ResearchBuildEnv env;
    i32 i;

    printf("the research screen's panels\n");

    g_bResBuilt = 0;
    g_bResCount = 8;
    g_nResBase = 1;
    memset(g_aResBuildRoot, 0, sizeof(g_aResBuildRoot));
    memset(g_aResBuildCtl, 0, sizeof(g_aResBuildCtl));
    memset(g_aResBuildPad, 0, sizeof(g_aResBuildPad));
    memset(g_aResColB, 0, sizeof(g_aResColB));
    memset(g_aResText, 0, sizeof(g_aResText));
    memset(g_aResTopicB, 0, sizeof(g_aResTopicB));
    g_pResPanelA = g_pResPanelB = g_pResPanelC = NULL;

    for (i = 0; i < 4; ++i) {
        PutStr(g_aResText + PUFO_TEXT_REC_STRIDE * (i + 1), (i == 0) ? "TOPIC1"
                       : (i == 1) ? "TOPIC2" : (i == 2) ? "TOPIC3" : "TOPIC4");
        g_aResBuildCtl[i].apPads[0] = &g_aResBuildPad[i];
    }

    /* Topics 1, 3, 4 and 6 are on the list. Topic 1 is one quarter done, topic 4 is double done,
     * topic 6 is exactly done, topic 3 has no progress. */
    g_aResTopicB[1].bActive = 1; g_aResTopicB[1].wGate4 = 1;
    g_aResTopicB[1].wTotal = 4;  g_aResTopicB[1].wProgress = 1;
    g_aResTopicB[3].bActive = 1; g_aResTopicB[3].wGate4 = 2;
    g_aResTopicB[4].bActive = 1; g_aResTopicB[4].wGate4 = 3;
    g_aResTopicB[4].wTotal = 1;  g_aResTopicB[4].wProgress = 2;
    g_aResTopicB[6].bActive = 1; g_aResTopicB[6].wGate4 = 4;
    g_aResTopicB[6].wTotal = 3;  g_aResTopicB[6].wProgress = 3;

    g_aResColB[144 + 1] = 12;
    g_aResColB[144 + 4] = 7;
    g_aResColB[144 + 6] = 1;

    memset(&env, 0, sizeof(env));
    env.pbPanelsBuilt = &g_bResBuilt;
    env.ppRoot = g_aResBuildRoot;
    for (i = 0; i < 4; ++i) {
        env.apCtl[i] = &g_aResBuildCtl[i];
    }
    env.ppPanelTopic = &g_pResPanelA;
    env.ppPanelB = &g_pResPanelB;
    env.ppPanelC = &g_pResPanelC;
    env.aTopics = g_aResTopicB;
    env.pbEntryCount = &g_bResCount;
    env.apResearchCols = g_aResColB;
    env.pnSelectedBaseIdx = &g_nResBase;
    env.pTextRecs = g_aResText;
    env.pfnPanelClick = ResStubPanelClick;

    ResearchScreen_BuildPanels(&env);

    CheckEqI32(g_bResBuilt, 1, "the build raises the panels-built byte (0x73410)");
    CheckEqI32(g_aResTopicB[0].bActive, 1, "and forces topic 0 onto the list (0x7341C)");
    Check(g_pResPanelA != NULL && g_pResPanelB != NULL && g_pResPanelC != NULL,
          "the three panels are allocated");
    if (g_pResPanelA == NULL || g_pResPanelB == NULL || g_pResPanelC == NULL) {
        return;
    }

    CheckEqI32(g_pResPanelA->nStyle, 15, "panel A's style is 15 rows, not the manufacturing 13");
    CheckEqI32(g_pResPanelA->nFont, 16, "and its font is 16");
    CheckEqI32(g_pResPanelA->nPosX, 10, "panel A starts at x 10");
    CheckEqI32(g_pResPanelA->nViewW, 230, "230 wide, the whole topic column");
    CheckEqI32(g_pResPanelA->nRectX, 5, "with its inner origin at 5");
    CheckEqI32(g_pResPanelA->nPosY, 70, "both at y 70");
    CheckEqI32(g_pResPanelA->bSelLineHighlight, 1, "panel A carries flag 7073");
    CheckEqI32(g_pResPanelA->bColorKey, 0x94, "and byte 7088 is 0x94, not the manufacturing 0x96");
    Check(g_pResPanelA->pfnClick == ResStubPanelClick,
          "panel A's click handler is the one that caches the row cursor");
    Check(g_pResPanelA->aStyleRgb[0] == 0xB4 && g_pResPanelA->aStyleRgb[1] == 0xB4
              && g_pResPanelA->aStyleRgb[2] == 0xB4,
          "style bytes 0..2 are B4 B4 B4");
    Check(g_pResPanelA->aStyleRgb[3] == 0x32 && g_pResPanelA->aStyleRgb[4] == 0x32
              && g_pResPanelA->aStyleRgb[5] == 0xFA,
          "bytes 3..5 are 32 32 FA, only on panel A");
    Check(g_pResPanelA->aStyleRgb[6] == 0xF0 && g_pResPanelA->aStyleRgb[9] == 0x14,
          "bytes 6..11 are F0 F0 F0 14 14 14 on all three");
    Check(g_pResPanelB->aStyleRgb[3] == 0 && g_pResPanelC->aStyleRgb[5] == 0,
          "panels B and C get no bytes 3..5");

    CheckEqI32(g_pResPanelB->nPosX, 197, "panel B, the quantity column, sits at x 197");
    CheckEqI32(g_pResPanelB->nViewW, 30, "30 wide");
    CheckEqI32(g_pResPanelB->nRectX, 175, "inner origin 175");
    CheckEqI32(g_pResPanelB->nUnk7076, 2, "right-aligned");
    CheckEqI32(g_pResPanelC->nPosX, 233, "panel C, the progress column, at x 233");
    CheckEqI32(g_pResPanelC->nRectX, 205, "inner origin 205");
    CheckEqI32(g_pResPanelC->nUnk7076, 2, "also right-aligned");

    CheckEqI32(g_pResPanelA->nLineCount, 4, "four active topics make four rows");
    CheckEqI32(g_pResPanelB->nLineCount, 4, "and all three panels end with that count");
    CheckEqI32(g_pResPanelC->nLineCount, 4, "including panel C");
    Check(ResLineIs(g_pResPanelA, 0, "TOPIC1"), "the name comes from TEXTS[topic + 4] (0x7373C)");
    Check(ResLineIs(g_pResPanelA, 3, "TOPIC4"), "the fourth row is the fourth active topic");
    CheckEqI32(g_pResPanelA->anLineAux[0], 1, "the aux column carries the topic number, not the row");
    CheckEqI32(g_pResPanelA->anLineAux[3], 6, "so row 3 is topic 6");
    Check(ResLineIs(g_pResPanelB, 0, "12"), "panel B shows the allocation of topic 1");
    Check(ResLineIs(g_pResPanelB, 1, ""), "a zero allocation gets the empty string (0x7380C)");
    Check(ResLineIs(g_pResPanelB, 2, "7"), "topic 4 shows its 7");
    Check(ResLineIs(g_pResPanelB, 3, "1"), "and topic 6 its 1");

    /* The percentage, and the way it differs from the refresh pass. */
    Check(ResLineIs(g_pResPanelC, 0, "0%"),
          "one quarter done reads 0% here: 100 * (1 / 4), the divide comes first (0x73870)");
    Check(ResLineIs(g_pResPanelC, 1, ""), "no progress gets the empty string");
    Check(ResLineIs(g_pResPanelC, 2, "200%"), "double done reads 200%, not clamped");
    Check(ResLineIs(g_pResPanelC, 3, "100%"), "exactly done reads 100%");

    (void)ResearchScreen_RefreshPanels(g_aResTopicB, g_bResCount, g_aResColB + 144,
                                       g_pResPanelA, g_pResPanelB, g_pResPanelC);
    Check(ResLineIs(g_pResPanelC, 0, "25%"),
          "the same topic on the same data reads 25% from RefreshPanels: (100 * 1) / 4 (0x73168)");
    Check(ResLineIs(g_pResPanelC, 2, "200%"), "the two agree only where the division is exact");

    /* The teardown. */
    g_aResBuildRoot[0] = calloc(1, sizeof(PadImage));
    ResearchScreen_BuildPanels(&env);
    CheckEqI32(g_bResBuilt, 0, "the second call tears the panels down (0x7394C)");
    Check(g_pResPanelA == NULL && g_pResPanelB == NULL && g_pResPanelC == NULL,
          "the three panel slots are cleared");
    Check(g_aResBuildRoot[0] == NULL, "the screen pad is released through the root slot");
    Check(g_aResBuildCtl[0].apPads[0] == NULL && g_aResBuildCtl[3].apPads[0] == NULL,
          "and the four controls' pads are released (0x73A10)");

    /* And the flag really drives both directions. */
    ResearchScreen_BuildPanels(&env);
    CheckEqI32(g_bResBuilt, 1, "a third call builds again");
    Check(g_pResPanelA != NULL && g_pResPanelA->nLineCount == 4, "with the same four rows");
    ResearchScreen_BuildPanels(&env);
    CheckEqI32(g_bResBuilt, 0, "and tears down once more");

    ResearchScreen_BuildPanels(NULL);
    CheckEqI32(g_bResBuilt, 0, "a null environment changes nothing");
}

/* ---- ResearchScreen_RebuildTopicList @ 0x74270 --------------------------------------- */

static ResearchTopicLive g_aRbTopic[PUFO_RESEARCH_TOPIC_COUNT];
static u8         g_bRbCount;
static u16        g_aRbCol[PUFO_RESEARCH_TOPIC_COUNT * PUFO_FAC_BASE_COUNT];
static u8         g_abRbZero[PUFO_RESEARCH_TOPIC_COUNT];
static BaseFacFlag g_aRbFac[PUFO_BASEFAC_COUNT];
static u32        g_nRbShortfall;
static UiLineList g_rbLines;
static ObjectRecFull g_aRbObj[9];
static ObjectRecFull *g_apRbObj[256];
static const u16 g_nRbObjCount = 7; /* Map_nObjectCount: ids 0..7 are in range for the 0x74814 test */
static const i8 g_nRbBaseIdx = 0;   /* nSelectedBaseIdx */
static BaseRec    g_rbBase;
static BaseRec   *g_apRbBase[PUFO_FAC_BASE_COUNT];
static u8         g_bRbFinished;
static i32        g_nRbShown;
static i32        g_nRbSampleId;
static i32        g_nRbSampleCount;

static void RbPlaySample(i32 nSample, void *pUser)
{
    (void)pUser;
    g_nRbSampleId = nSample;
    ++g_nRbSampleCount;
}

static void RbReset(ResearchRebuildEnv *pEnv)
{
    memset(g_aRbTopic, 0, sizeof(g_aRbTopic));
    memset(g_aRbCol, 0, sizeof(g_aRbCol));
    memset(g_abRbZero, 0, sizeof(g_abRbZero));
    memset(g_aRbFac, 0, sizeof(g_aRbFac));
    memset(&g_rbLines, 0, sizeof(g_rbLines));
    memset(g_aRbObj, 0, sizeof(g_aRbObj));
    memset(g_apRbObj, 0, sizeof(g_apRbObj));
    memset(&g_rbBase, 0, sizeof(g_rbBase));
    g_bRbCount = 0;
    g_nRbShortfall = 0;
    g_bRbFinished = 0;
    g_nRbShown = 0;
    g_nRbSampleId = -1;
    g_nRbSampleCount = 0;
    g_rbBase.bSlot = 0; /* BaseRec_SubItemQty spends from auPerBaseVal[bSlot], not from the index */
    for (i32 i = 0; i < 9; ++i) {
        g_apRbObj[i] = &g_aRbObj[i];
    }
    for (i32 i = 0; i < PUFO_FAC_BASE_COUNT; ++i) {
        g_apRbBase[i] = &g_rbBase;
    }
    memset(pEnv, 0, sizeof(*pEnv));
    pEnv->aTopics = g_aRbTopic;
    pEnv->pbEntryCount = &g_bRbCount;
    pEnv->apResearchCols = g_aRbCol;
    pEnv->abResearchZero = g_abRbZero;
    pEnv->paFac = g_aRbFac;
    pEnv->pdwShortfall = &g_nRbShortfall;
    pEnv->pLines = &g_rbLines;
    pEnv->apObjects = g_apRbObj;
    pEnv->pnObjectCount = &g_nRbObjCount;
    pEnv->apBases = g_apRbBase;
    pEnv->pnSelectedBaseIdx = &g_nRbBaseIdx;
    pEnv->pbFinishedTopic = &g_bRbFinished;
    pEnv->pnShownTopic = &g_nRbShown;
    pEnv->pfnPlaySample = RbPlaySample;
}

static i32 RbFacFlagsSet(void)
{
    i32 n = 0;
    for (i32 i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
        if (g_aRbFac[i].bFlag20 != 0) {
            ++n;
        }
    }
    return n;
}

static void TestResearchRebuild(void)
{
    ResearchRebuildEnv env;

    printf("\nResearchScreen_RebuildTopicList @ 0x74270\n");

    /* A. Which topics the pass can touch at all. Everything zero, count 144 (what main.dat really
     * loads -- test_geoiinit asserts the same byte), base 0, object 7 stocked with 3 at base 0.
     * A topic outside 55..65 / 118..142 answers "needs no item" (0x72C9C) and the gate at 0x74364
     * skips the whole iteration; 135 and 140 are pulled out of the second band by name. Each topic
     * that does finish spends one unit of object 7 (0x743E8), so only three get through. */
    RbReset(&env);
    g_bRbCount = 144;
    for (i32 i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        g_aRbTopic[i].abNeedItems[0] = 7;
    }
    g_aRbObj[7].auPerBaseVal[0] = 3; /* apObjects[7] */
    g_aRbTopic[55].wProgress = 7;
    g_aRbTopic[56].wProgress = 4;
    CheckEqI32(ResearchScreen_RebuildTopicList(&env), 144,
               "the port returns the entry count where the original returns its own argument");
    CheckEqI32(g_aRbTopic[54].bUnlocked, 0, "topic 54 is outside the bands, so the gate skips it (0x74388)");
    CheckEqI32(g_aRbTopic[55].bUnlocked, 1, "topic 55 reaches the finished branch and unlocks (0x7440C)");
    CheckEqI32(g_aRbTopic[57].bUnlocked, 1, "topic 57 too");
    CheckEqI32(g_aRbTopic[58].bUnlocked, 0, "topic 58 is skipped: finishing spent the last unit (0x743E8)");
    CheckEqI32(g_aRbObj[0].auPerBaseVal[0], 0, "three finished topics spent all three units");
    CheckEqI32(g_rbBase.nStoresTotal, -3, "BaseRec_SubItemQty also moves nStoresTotal");
    CheckEqI32(g_aRbTopic[55].wProgress, 0, "wProgress is zeroed when the item was spent (0x743F8)");
    CheckEqI32(g_aRbTopic[56].wProgress, 0, "and for the next one too");
    CheckEqI32(g_aRbTopic[66].bUnlocked, 0, "topic 66 is outside the first band");
    CheckEqI32(g_aRbTopic[117].bUnlocked, 0, "topic 117 is outside the second band");
    CheckEqI32(g_aRbTopic[118].bUnlocked, 0, "the second band never runs: the stock is already zero");
    CheckEqI32(g_abRbZero[55], 1, "abResearchZero marks the topic that ran");
    CheckEqI32(g_abRbZero[58], 0, "and not the one the gate skipped");
    CheckEqI32(g_nRbShortfall, 1, "the finished branch writes 1 into the shortfall dword (0x74458)");
    CheckEqI32((i32)g_bRbFinished, 57, "nResearchFinishedTopic holds the last topic that finished");
    CheckEqI32(g_nRbShown, 57, "nResearchTopicShown too (0x74424)");
    CheckEqI32((i32)g_rbLines.nLineCount, 3, "one message line per finished topic (0x74554)");
    CheckEqI32(g_rbLines.abLineFlags[0], PUFO_RESEARCH_LINE_FLAG, "the line flag is 37");
    CheckEqI32((i32)g_rbLines.anColor[0][0], 55, "colour slot 0 carries the topic number");
    CheckEqI32((i32)g_rbLines.anColor[0][1], 0xFF, "slots 1..3 are 0xFF on the appended line (0x74540)");
    CheckEqI32((i32)g_rbLines.anColor[2][0], 57, "the third line carries topic 57");
    CheckEqI32(g_nRbSampleCount, 3, "each append plays a sample");
    CheckEqI32(g_nRbSampleId, 7, "and the sample is 7 (UiPanel_AppendLineWithSound)");
    CheckEqI32(g_aRbTopic[143].bUnlocked, 0,
               "record 143 stays locked: its five prerequisites are all record 0, which is locked");

    /* B. Record 143's own gate (0x74294): the product of the five prerequisites' bUnlocked has to be
     * exactly 1, and the product is masked to a byte after every multiply. */
    RbReset(&env);
    g_bRbCount = 2; /* walk only topic 1, so record 143 is not walked and only block 1 touches it */
    for (i32 i = 0; i < 5; ++i) {
        g_aRbTopic[143].abPreTopics[i] = (u8)(1 + i);
    }
    for (i32 i = 1; i <= 5; ++i) {
        g_aRbTopic[i].bUnlocked = 1;
    }
    (void)ResearchScreen_RebuildTopicList(&env);
    CheckEqI32(g_aRbTopic[143].bUnlocked, 1,
               "five unlocked prerequisites give 1, so record 143 unlocks (0x742E4)");

    RbReset(&env);
    g_bRbCount = 2;
    for (i32 i = 0; i < 5; ++i) {
        g_aRbTopic[143].abPreTopics[i] = (u8)(1 + i);
    }
    for (i32 i = 1; i <= 5; ++i) {
        g_aRbTopic[i].bUnlocked = 1;
    }
    g_aRbTopic[3].bUnlocked = 2;
    (void)ResearchScreen_RebuildTopicList(&env);
    CheckEqI32(g_aRbTopic[143].bUnlocked, 0,
               "a prerequisite holding 2 makes the product 2, and the test is == 1 (0x742DC)");

    /* C. The in-progress branch (0x747A0): gate from the prerequisites and the five needed items,
     * then the availability scan over the second range pair. */
    RbReset(&env);
    g_bRbCount = 62;
    for (i32 i = 1; i <= 5; ++i) {
        g_aRbTopic[i].bUnlocked = 1;
    }
    for (i32 i = 0; i < 5; ++i) {
        g_aRbTopic[60].abPreTopics[i] = (u8)(1 + i);
        g_aRbTopic[60].abNeedItems[i] = 7;
    }
    g_aRbObj[7].auPerBaseVal[0] = 5;
    g_aRbTopic[60].bUnlocked = 1;
    g_aRbTopic[60].wTotal = 100;
    g_aRbTopic[60].wProgress = 20;
    g_aRbTopic[61].abUnk0[2] = 1; /* lo2 */
    g_aRbTopic[61].abUnk0[3] = 3; /* hi2 */
    (void)ResearchScreen_RebuildTopicList(&env);
    CheckEqI32(g_aRbTopic[60].bActive, 1,
               "gate intact and lo2 + hi2 + the sum is 54, so the running topic goes on the list (0x748E8)");
    CheckEqI32(g_aRbTopic[60].wProgress, 20, "the in-progress branch never writes wProgress");
    CheckEqI32((i32)g_rbLines.nLineCount, 0, "and appends no message line");
    CheckEqI32((i32)g_nRbShortfall, 0, "nor touches the shortfall dword");
    CheckEqI32(g_abRbZero[60], 1, "bUnlocked == 1 marks abResearchZero before the split (0x74348)");
    CheckEqI32(g_abRbZero[7], 0, "a locked topic is not marked");

    RbReset(&env);
    g_bRbCount = 62;
    for (i32 i = 1; i <= 5; ++i) {
        g_aRbTopic[i].bUnlocked = 1;
    }
    g_aRbTopic[60].bUnlocked = 1;
    g_aRbTopic[60].wTotal = 100;
    g_aRbTopic[60].wProgress = 20;
    for (i32 i = 0; i < 5; ++i) {
        g_aRbTopic[60].abPreTopics[i] = (u8)(1 + i);
    }
    g_aRbTopic[60].abNeedItems[0] = 8; /* stocked, so the 0x74364 gate passes */
    g_aRbTopic[60].abNeedItems[1] = 7; /* empty, so the 0x747E4 loop clears the gate */
    g_aRbObj[8].auPerBaseVal[0] = 5; /* apObjects[8]: stocked, and above Map_nObjectCount so the
                                      * 0x74814 range test skips it rather than derefing it */
    g_aRbObj[7].auPerBaseVal[0] = 0; /* apObjects[7]: inside the range and empty */
    g_aRbTopic[61].abUnk0[2] = 1;
    g_aRbTopic[61].abUnk0[3] = 3;
    (void)ResearchScreen_RebuildTopicList(&env);
    CheckEqI32(g_aRbTopic[60].bActive, 0,
               "a needed item with no stock at this base clears the gate AND drops bActive (0x74854)");

    RbReset(&env);
    g_bRbCount = 62;
    for (i32 i = 40; i <= 44; ++i) {
        g_aRbTopic[i].bUnlocked = 1;
    }
    for (i32 i = 0; i < 5; ++i) {
        g_aRbTopic[60].abPreTopics[i] = (u8)(40 + i);
        g_aRbTopic[60].abNeedItems[i] = 7;
    }
    g_aRbObj[7].auPerBaseVal[0] = 5;
    g_aRbTopic[60].bUnlocked = 1;
    g_aRbTopic[60].wTotal = 100;
    g_aRbTopic[60].wProgress = 20;
    g_aRbTopic[61].abUnk0[2] = 1; /* [1..3] are all locked and unmarked */
    g_aRbTopic[61].abUnk0[3] = 3;
    (void)ResearchScreen_RebuildTopicList(&env);
    CheckEqI32(g_aRbTopic[60].bActive, 0,
               "no topic in [lo2, hi2] is unlocked or marked, so the scan clears the gate (0x748C8)");

    RbReset(&env);
    g_bRbCount = 62;
    g_aRbObj[0].auPerBaseVal[0] = 5; /* abNeedItems[0] == 0 still passes the gate: id 0 is stocked */
    g_aRbTopic[60].bUnlocked = 1;
    g_aRbTopic[60].wTotal = 100;
    g_aRbTopic[60].wProgress = 20;
    (void)ResearchScreen_RebuildTopicList(&env);
    CheckEqI32(g_aRbTopic[60].bActive, 0,
               "gate 1 but lo2 + hi2 + the sum is 0, so bActive is not set (0x748E0)");

    /* D. The finished branch's side effects: the facility-kind permutation, the activate group, the
     * unlock roll, the line rewrite and the column clear. Topics 37, 43, 47 and 51 finish: locked
     * with progress already booked, so 0x74364 skips the gate and 0x74394 sends them down the
     * finished path. None of them is in a needs-item band, so no item is spent. */
    RbReset(&env);
    g_bRbCount = 54;
    const i32 anFinish[4] = { 37, 43, 47, 51 };
    for (i32 i = 0; i < 4; ++i) {
        g_aRbTopic[anFinish[i]].bUnlocked = 0;
        g_aRbTopic[anFinish[i]].wTotal = 10;
        g_aRbTopic[anFinish[i]].wProgress = 10;
    }
    g_aRbTopic[37].abActivateTopics[0] = 20;
    g_aRbTopic[37].abActivateTopics[2] = 21;
    g_aRbTopic[38].abUnk0[0] = 10; /* topic 37's roll range: lo */
    g_aRbTopic[38].abUnk0[1] = 13; /* hi */
    for (i32 b = 0; b < PUFO_FAC_BASE_COUNT; ++b) {
        g_aRbCol[37 + b * PUFO_RESEARCH_TOPIC_COUNT] = 999;
        g_aRbCol[12 + b * PUFO_RESEARCH_TOPIC_COUNT] = 888;
    }
    i32 nSeed = -1;
    i32 nRollWant = -1;
    for (i32 nTry = 1; nTry < 1000 && nSeed < 0; ++nTry) {
        srand((unsigned)nTry);
        const i32 nRoll = (i32)Game_RandU16(4u);
        if (nRoll >= 0 && nRoll <= 3) {
            nSeed = nTry;
            nRollWant = nRoll;
        }
    }
    srand((unsigned)nSeed);
    (void)ResearchScreen_RebuildTopicList(&env);
    const i32 nPick = 10 + nRollWant;
    Check(nSeed > 0, "a seed was found to pin the unlock roll");
    CheckEqI32(g_aRbTopic[37].wProgress, 10,
               "no item was needed, so wProgress keeps its value (0x743B0 BEQ 0x743FC)");
    CheckEqI32(g_aRbTopic[37].bUnlocked, 1, "the finished topic unlocks");
    CheckEqI32(g_aRbFac[FAC_ACCESS_LIFT].bFlag20, 1, "topic 37 -> FAC_ACCESS_LIFT (0x186E5C)");
    CheckEqI32(g_aRbFac[FAC_MISSILE_DEFENCES].bFlag20, 1, "topic 43 -> FAC_MISSILE_DEFENCES (0x186FC8)");
    CheckEqI32(g_aRbFac[FAC_MIND_SHIELD].bFlag20, 1, "topic 51 -> FAC_MIND_SHIELD (0x186F74)");
    CheckEqI32(g_aRbFac[FAC_HANGAR].bFlag20, 0, "FAC_HANGAR is the kind no topic writes");
    CheckEqI32(RbFacFlagsSet(), 3, "exactly three flags: topic 47's case has no slot at all");
    CheckEqI32(g_aRbTopic[20].bActive, 1, "the +0x1D group switches topic 20 on (0x745A0)");
    CheckEqI32(g_aRbTopic[21].bActive, 1, "and topic 21");
    CheckEqI32(g_aRbTopic[19].bActive, 0, "a zero entry switches nothing on");
    CheckEqI32(g_aRbTopic[nPick].bUnlocked, 1, "the roll unlocks one topic inside [10, 13] (0x74630)");
    CheckEqI32(g_abRbZero[nPick], 1, "and marks it in abResearchZero (0x74798)");
    CheckEqI32((i32)g_rbLines.nLineCount, 4, "four finished topics, four lines");
    CheckEqI32((i32)g_rbLines.anColor[0][1], nRollWant,
               "the appended line is rewritten in place with the roll in slot 1 (0x74780)");
    CheckEqI32((i32)g_rbLines.anColor[0][2], 0xE8, "slot 2 becomes 0xE8");
    CheckEqI32((i32)g_rbLines.anColor[0][3], 0, "slot 3 becomes 0");
    CheckEqI32((i32)g_rbLines.anColor[1][0], 43, "the later lines keep the appended colours");
    CheckEqI32((i32)g_rbLines.anColor[1][1], 0xFF, "and are not rewritten");
    for (i32 b = 0; b < PUFO_FAC_BASE_COUNT; ++b) {
        CheckEqI32(g_aRbCol[37 + b * PUFO_RESEARCH_TOPIC_COUNT], 0,
                   "topic 37's allocation is cleared in every base");
    }
    for (i32 b = 0; b < PUFO_FAC_BASE_COUNT; ++b) {
        CheckEqI32(g_aRbCol[12 + b * PUFO_RESEARCH_TOPIC_COUNT], 888,
                   "the newly unlocked topic's allocation is NOT cleared (0x74648 MOV R0,R7)");
    }
    CheckEqI32((i32)g_bRbFinished, 51, "the last finished topic wins the finished global");
    CheckEqI32(g_nRbShown, 51, "and the shown global");

    /* E. The 50-try cap (0x7460C). Every candidate in [10, 13] is already unlocked, so the roll
     * retries until the counter reaches 50 and then accepts whatever it landed on. */
    RbReset(&env);
    g_bRbCount = 38;
    g_aRbTopic[37].wTotal = 10;
    g_aRbTopic[37].wProgress = 10;
    g_aRbTopic[38].abUnk0[0] = 10;
    g_aRbTopic[38].abUnk0[1] = 13;
    for (i32 i = 10; i <= 13; ++i) {
        g_aRbTopic[i].bUnlocked = 1;
    }
    i32 nCapSeed = -1;
    i32 nFirst = -1;
    i32 nLast = -1;
    for (i32 nTry = 1; nTry < 1000 && nCapSeed < 0; ++nTry) {
        srand((unsigned)nTry);
        const i32 nA = (i32)Game_RandU16(4u);
        for (i32 k = 0; k < 48; ++k) {
            (void)Game_RandU16(4u);
        }
        const i32 nB = (i32)Game_RandU16(4u);
        if (nA != nB) {
            nCapSeed = nTry;
            nFirst = nA;
            nLast = nB;
        }
    }
    srand((unsigned)nCapSeed);
    (void)ResearchScreen_RebuildTopicList(&env);
    Check(nCapSeed > 0, "a seed was found whose first and fiftieth rolls differ");
    CheckEqI32(g_nRbShown, 10 + nLast,
               "after 50 tries the original accepts the 50th roll, not the first (0x7460C)");
    Check(g_nRbShown != 10 + nFirst, "so the first roll really was thrown away");
    CheckEqI32((i32)g_rbLines.anColor[0][1], 0xFF,
               "the line is not rewritten: abResearchZero[pick] was already set (0x7474C)");

    /* F. Topic 143's range pair lives at 0x160228, the gap between the 144-record table and
     * auResearchCol. Nothing in the IDB writes it, so the port reads zeros and the roll is skipped. */
    RbReset(&env);
    g_bRbCount = 144;
    g_aRbTopic[143].wTotal = 5;
    g_aRbTopic[143].wProgress = 5;
    (void)ResearchScreen_RebuildTopicList(&env);
    CheckEqI32(g_aRbTopic[143].bUnlocked, 1, "topic 143 finishes");
    CheckEqI32(g_aRbTopic[143].wProgress, 5, "and keeps wProgress: it needs no item");
    CheckEqI32((i32)g_bRbFinished, 143, "the finished global holds 143");
    CheckEqI32(g_nRbShown, 143, "and no roll moved the shown global");
    CheckEqI32(RbFacFlagsSet(), 0, "topic 143 is past the switch's 37..53 range, so no kind is set");
    {
        i32 nUnlocked = 0;
        for (i32 i = 0; i < 143; ++i) {
            if (g_aRbTopic[i].bUnlocked != 0) {
                ++nUnlocked;
            }
        }
        CheckEqI32(nUnlocked, 0, "the zero gap reads lo == hi, so no topic is unlocked by a roll");
    }

    /* G. The count gate. */
    RbReset(&env);
    g_bRbCount = 1;
    CheckEqI32(ResearchScreen_RebuildTopicList(&env), 1, "count 1 skips the walk entirely (0x74300)");
    CheckEqI32((i32)g_rbLines.nLineCount, 0, "and appends nothing");
    RbReset(&env);
    g_bRbCount = 0;
    CheckEqI32(ResearchScreen_RebuildTopicList(&env), 0, "count 0 likewise");
}

/* ------------------------------------------------- the monthly funding recompute */
static BaseRec      g_aFundBases[8];
static const BaseRec *g_apFundBases[8];
static PadImage    *g_pFundPad;
static UiControl    g_fundClose;
static i32          g_nFundCloseClicks;
static i32          g_nFundPending;
static i32          g_nFundScreenId;
static u8           g_bFundAnim;
/* The env's array members are pointers into the caller's globals (0x186568..0x1865B8 and
 * 0x1595F0..0x159600), so the fixture owns them here. */
static i32          g_aFundScore[6], g_aFundShare[5], g_aFundBaseline[5];
static i32          g_aFundBar[5], g_aFundArrow[5], g_nFundMonth;

static void FundCloseClick(void) { ++g_nFundCloseClicks; }

/* Five bases with kinds 0x80, 0x82, 0x84, 0x86 and 0x88: slots 0, 2, 1, none and 0. Slot 3 is the
 * only one no base feeds, so its share stays at the plain score product. */
static void FundFixture(FundingInitEnv *pEnv, i32 nMonth, i8 nBaseCount)
{
    static const u8 aKind[8] = {0x80, 0x82, 0x84, 0x86, 0x88, 0xFF, 0x83, 0x85};
    i32 i;

    memset(g_aFundBases, 0, sizeof(g_aFundBases));
    for (i = 0; i < 8; ++i) {
        g_aFundBases[i].bRegionId = aKind[i];
        g_apFundBases[i] = &g_aFundBases[i];
    }
    memset(&g_fundClose, 0, sizeof(g_fundClose));
    g_pFundPad = NULL;
    g_nFundCloseClicks = 0;
    g_nFundPending = 1510;
    g_nFundScreenId = -1;
    g_bFundAnim = 1;

    memset(g_aFundScore, 0, sizeof(g_aFundScore));
    memset(g_aFundShare, 0, sizeof(g_aFundShare));
    memset(g_aFundBaseline, 0, sizeof(g_aFundBaseline));
    memset(g_aFundBar, 0, sizeof(g_aFundBar));
    memset(g_aFundArrow, 0, sizeof(g_aFundArrow));

    memset(pEnv, 0, sizeof(*pEnv));
    pEnv->ppPadSlot = &g_pFundPad;
    pEnv->pCloseCtl = &g_fundClose;
    pEnv->pfnCloseClick = FundCloseClick;
    pEnv->anCountryScore = g_aFundScore;
    pEnv->anShare = g_aFundShare;
    pEnv->anBaseline = g_aFundBaseline;
    pEnv->anBarTotal = g_aFundBar;
    pEnv->anArrow = g_aFundArrow;
    pEnv->pnMonthCounter = &g_nFundMonth;
    g_aFundScore[0] = 10;
    g_aFundScore[1] = 20;
    g_aFundScore[2] = 30;
    g_aFundScore[3] = 40;
    g_aFundScore[4] = 50;
    g_aFundScore[PUFO_FUNDING_MULT_SLOT] = 3;
    pEnv->pnPendingTotal = &g_nFundPending;
    g_nFundMonth = nMonth;
    pEnv->apBases = g_apFundBases;
    pEnv->nBaseCount = nBaseCount;
}

static void TestFundingRecompute(void)
{
    FundingInitEnv env;

    printf("the monthly funding recompute\n");

    const char *const apszArch[2] = { "lang.dat", "main.dat" };
    if (!Asset_OpenArchives(apszArch)) {
        printf("  FAIL main.dat / lang.dat did not open\n");
        ++g_nFailures;
        return;
    }

    /* A. The pad and the close control. */
    FundFixture(&env, 4, 5);
    FundingScreen_Init(&env);
    Check(g_pFundPad != NULL, "FundingScreen_Init @ 0x5c084 allocates the funding pad (0x5C090)");
    Check(g_pFundPad != NULL && g_pFundPad->uWidth != 0 && g_pFundPad->uHeight != 0,
          "and PadImage_Load(\"funding\", 28) filled it from the shipped archives (0x5C0B8)");
    CheckEqI32(g_pFundPad != NULL ? g_pFundPad->nOriginX : -1, 0, "the pad origin is x 0 (0x5C0C8)");
    CheckEqI32(g_pFundPad != NULL ? g_pFundPad->nOriginY : -1, 0, "and y 0 (0x5C0D4)");
    CheckEqI32(g_fundClose.x, 224, "the close control goes to x 224 (0x5C0E0)");
    CheckEqI32(g_fundClose.y, 1, "and y 1 (0x5C0EC)");
    Check(g_fundClose.pfnClick == FundCloseClick, "with FundingScreen_ExitThunk as its handler (0x5C0F8)");

    /* B. Month 4: the money, the base bonus, the upkeep and the bars.
     * scores 10/20/30/40/50 sum to 150, so the divisor is 151. Pending 1510 gives 1510/151 = 10,
     * the multiplier 3 turns that into a 30 bonus, the total 1540 divided by 151 is still 10, and
     * the five shares are 100/200/300/400/500. The five bases add 3 * 10 = 30 to slots 0, 2, 1,
     * nothing and 0, so the shares become 160/230/330/400/500. Month 4's upkeep is -500 for the
     * USA / Euro / Asia rows, -200 for Australia and -350 for Africa. */
    FundFixture(&env, 4, 5);
    FundingScreen_Init(&env);
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_USA], -500, "month 4 puts -500 in the USA baseline (0x5C2F4)");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_EURO], -500, "the same -500 in Euro's");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_ASIA], -500, "and in Asia's");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_AUS], -200, "Australia gets -200");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_AFR], -350, "Africa gets -350");
    CheckEqI32(env.anShare[0], -340, "slot 0: 100 + 30 + 30 - 500 (0x5C20C, 0x5C34C)");
    CheckEqI32(env.anShare[1], 30, "slot 1: 200 + 30 - 200 (0x5C2B8, 0x5C374)");
    CheckEqI32(env.anShare[2], -170, "slot 2: 300 + 30 - 500 (0x5C2E8, 0x5C350)");
    CheckEqI32(env.anShare[3], -100, "slot 3: 400 with no base behind it, minus 500 (0x5C368)");
    CheckEqI32(env.anShare[4], 150, "slot 4: 500 - 350 (0x5C38C)");
    CheckEqI32(g_nFundPending, 0, "the pending total is zeroed at the end (0x5C544)");

    /* The bars. Row 0: DeltaToArrow(-340, -500) is -2 (-340 <= -500/8 + -500/2 = -312), so the
     * level is 3 * -50000. Row 1: -170 is above -312 but at or below -62, so -1 and 5 * -25000.
     * Row 2: -100 likewise -1, 4 * -25000. Row 3: 30 against -200 is +1, 1 * +25000. Row 4: 150
     * against -350 is +1, 3 * +25000. Month 4 is not below 3, so nothing is clamped early. */
    CheckEqI32(env.anBarTotal[0], 0, "row USA: the -150000 bar went negative, so it is wiped (0x5C4D4)");
    CheckEqI32(env.anArrow[0], 0, "and its arrow is wiped with it (0x5C4E0)");
    CheckEqI32(env.anBarTotal[2], 0, "row Euro: -125000 wiped (0x5C4EC)");
    CheckEqI32(env.anArrow[2], 0, "arrow too (0x5C4F8)");
    CheckEqI32(env.anBarTotal[3], 0, "row Asia: -100000 wiped (0x5C504)");
    CheckEqI32(env.anArrow[3], 0, "arrow too (0x5C510)");
    CheckEqI32(env.anBarTotal[1], 25000, "row Australia: 1 * +25000 survives (0x5C48C)");
    CheckEqI32(env.anArrow[1], 25000, "with its arrow (0x5C468)");
    CheckEqI32(env.anBarTotal[4], 75000, "row Africa: 3 * +25000 (0x5C4C8)");
    CheckEqI32(env.anArrow[4], 75000, "with its arrow (0x5C4A8)");

    /* C. Month 1: the same money, the lighter upkeep, and the below-month-3 clamp.
     * Upkeep -300/-300/-300/-100/-200 gives shares -140 / 130 / 30 / 100 / 300. Row 0's arrow is
     * -1 (3 * -25000 = -75000) and month 1 is below 3, so the level is zeroed before it is added.
     * Row 1's arrow is 0, and Funding_ScoreToBarLevel returns 0 for direction 0. Rows 2, 3 and 4
     * are positive, so the clamp does not touch them. */
    FundFixture(&env, 1, 5);
    FundingScreen_Init(&env);
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_USA], -300, "month 1 puts -300 in the USA baseline (0x5C308)");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_AUS], -100, "Australia -100");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_AFR], -200, "Africa -200");
    CheckEqI32(env.anShare[0], -140, "slot 0: 160 - 300");
    CheckEqI32(env.anShare[1], 130, "slot 1: 230 - 100");
    CheckEqI32(env.anShare[2], 30, "slot 2: 330 - 300");
    CheckEqI32(env.anShare[3], 100, "slot 3: 400 - 300");
    CheckEqI32(env.anShare[4], 300, "slot 4: 500 - 200");
    CheckEqI32(env.anArrow[0], 0, "the negative level is zeroed before the add when month < 3 (0x5C3B8)");
    CheckEqI32(env.anBarTotal[0], 0, "so the bar never goes negative this month");
    CheckEqI32(env.anArrow[2], 0, "a 0 arrow gives a 0 level outright (0x5AAD0)");
    CheckEqI32(env.anBarTotal[2], 0, "and the bar stays 0");
    CheckEqI32(env.anArrow[3], 100000, "row Asia: 4 * +25000 (0x5C428)");
    CheckEqI32(env.anBarTotal[3], 100000, "added to the bar (0x5C44C)");
    CheckEqI32(env.anArrow[1], 50000, "row Australia: 1 * +50000, the +2 arrow (0x5C468)");
    CheckEqI32(env.anBarTotal[1], 50000, "added to the bar (0x5C48C)");
    CheckEqI32(env.anArrow[4], 150000, "row Africa: 3 * +50000 (0x5C4A8)");
    CheckEqI32(env.anBarTotal[4], 150000, "added to the bar (0x5C4C8)");

    /* D. Month 3 is still in the light band, and months 0 and 6 share the heaviest one. */
    FundFixture(&env, 3, 5);
    FundingScreen_Init(&env);
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_USA], -300, "month 3 is still the light band (0x5C23C)");
    CheckEqI32(env.anArrow[0], 0, "and month 3 is still below 3 for the clamp (0x5C3AC)");
    FundFixture(&env, 0, 5);
    FundingScreen_Init(&env);
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_USA], -1100, "month 0 falls into the heaviest block (0x5C230)");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_AUS], -300, "Australia -300 there");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_AFR], -800, "Africa -800 there");
    FundFixture(&env, 6, 5);
    FundingScreen_Init(&env);
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_USA], -1100, "month 6 reaches the same block (0x5C250)");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_AUS], -300, "identically for Australia");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_AFR], -800, "and for Africa");
    CheckEqI32(env.anArrow[0], 0, "month 6 is not below 3, so a negative bar is only wiped at the end");
    CheckEqI32(env.anBarTotal[0], 0, "which still leaves the bar at 0");

    /* E. The base walk is bounded by the signed count. */
    FundFixture(&env, 4, 0);
    FundingScreen_Init(&env);
    CheckEqI32(env.anShare[0], -400, "count 0: slot 0 is 100 - 500 with no base bonus (0x5C1B0)");
    CheckEqI32(env.anShare[1], 0, "slot 1: 200 - 200");
    CheckEqI32(env.anShare[2], -200, "slot 2: 300 - 500");
    FundFixture(&env, 4, -1);
    FundingScreen_Init(&env);
    CheckEqI32(env.anShare[0], -400, "a negative count runs nothing either (LDRSB at 0x5C188)");
    CheckEqI32(env.anShare[2], -200, "on every slot");
    FundFixture(&env, 4, 1);
    g_aFundBases[0].bRegionId = 0x86; /* a kind with no arm in the compare chain (0x5C200) */
    FundingScreen_Init(&env);
    CheckEqI32(env.anShare[0], -400, "kind 0x86 has no arm, so it feeds nothing (0x5C200)");

    /* F. The opener: the month goes up first, then the recompute runs. */
    FundFixture(&env, 3, 5);
    FundingScreen_OpenMonthly(&env, &g_nFundScreenId, &g_bFundAnim);
    CheckEqI32(*env.pnMonthCounter, 4, "the month counter is incremented before the recompute (0x5D170)");
    CheckEqI32(env.anBaseline[PUFO_FUND_ROW_USA], -500, "so this report runs at month 4's upkeep");
    CheckEqI32(g_nFundScreenId, 19, "the geoscape screen id becomes 19 (0x5D18C)");
    CheckEqI32((i32)g_bFundAnim, 0, "and the shared animation flag is cleared (0x5D1A0)");
    CheckEqI32(g_nFundPending, 0, "the recompute ran and zeroed the pending total");

    /* G. The guards. */
    FundingScreen_Init(NULL);
    FundFixture(&env, 4, 5);
    env.ppPadSlot = NULL;
    FundingScreen_Init(&env);
    CheckEqI32(env.anShare[0], 0, "a missing pad slot refuses the whole recompute");
    FundFixture(&env, 4, 5);
    env.apBases = NULL;
    FundingScreen_Init(&env);
    CheckEqI32(env.anShare[0], 0, "so does a missing base array");
    FundingScreen_OpenMonthly(NULL, &g_nFundScreenId, &g_bFundAnim);
    CheckEqI32(g_nFundScreenId, -1, "and OpenMonthly refuses to touch the screen id");

    Asset_CloseArchives();
}

/* ---------------------------------------------------------------------------
 * The funding screen's own UI: FundingScreen_Toggle @ 0x5abe4, FundingScreen_Open @ 0x5b560,
 * FundingScreen_Draw @ 0x5b1fc and FundingScreen_DrawExtended @ 0x5b59c.
 * ------------------------------------------------------------------------- */

static u8        g_bFundOpen;
static i32       g_nFundTotal;
static PadImage *g_pFundUiPad;
static UiPanel  *g_pFundPanelNames;
static UiPanel  *g_pFundPanelBars;
static UiPanel  *g_pFundPanelArrows;
static i32       g_anFundBarUi[PUFO_FUND_ROW_COUNT];   /* slot-indexed, g_nFundBar0..4 @ 0x186594 */
static i32       g_anFundLevelUi[PUFO_FUND_ROW_COUNT]; /* slot-indexed, g_nFundArrow0..4 @ 0x1865A8 */
static i32       g_anFundShareUi[PUFO_FUND_ROW_COUNT]; /* slot-indexed, g_nFundShare0..4 @ 0x186580 */
static i32       g_anFundBaseUi[PUFO_FUND_ROW_COUNT];  /* row-indexed, g_nFundBase0..4 @ 0x1595F0 */
static UiControl g_fundUiClose;
static PadImage  g_fundUiClosePad;

static void FundUiFixture(FundingToggleEnv *pEnv)
{
    memset(pEnv, 0, sizeof(*pEnv));
    memset(g_anFundBarUi, 0, sizeof(g_anFundBarUi));
    memset(g_anFundLevelUi, 0, sizeof(g_anFundLevelUi));
    memset(g_anFundShareUi, 0, sizeof(g_anFundShareUi));
    memset(g_anFundBaseUi, 0, sizeof(g_anFundBaseUi));
    memset(&g_fundUiClose, 0, sizeof(g_fundUiClose));
    memset(&g_fundUiClosePad, 0, sizeof(g_fundUiClosePad));
    g_fundUiClose.bEnabled = 1;   /* a disabled control blits nothing (0x1A360) */
    g_fundUiClose.apPads[0] = &g_fundUiClosePad;
    g_bFundOpen = 0;
    g_nFundTotal = 0;
    g_pFundUiPad = NULL;
    g_pFundPanelNames = NULL;
    g_pFundPanelBars = NULL;
    g_pFundPanelArrows = NULL;

    pEnv->pbOpen = &g_bFundOpen;
    pEnv->pnTotal = &g_nFundTotal;
    pEnv->ppPad = &g_pFundUiPad;
    pEnv->ppPanelNames = &g_pFundPanelNames;
    pEnv->ppPanelBars = &g_pFundPanelBars;
    pEnv->ppPanelArrows = &g_pFundPanelArrows;
    pEnv->pTextRecs = (const u8 *)g_aFrameText;
    pEnv->anBar = g_anFundBarUi;
    pEnv->anLevel = g_anFundLevelUi;
}

/* Every TEXTS record the funding UI reads, as a distinct ASCII stand-in. The record NUMBER is the
 * claim: a wrong record lands different pixels on the frame. */
static void FundUiText(void)
{
    i32 nRec;

    memset(g_aFrameText, 0, sizeof(g_aFrameText));
    for (nRec = PUFO_FUND_TEXT_TITLE; nRec <= PUFO_FUND_TEXT_TOTAL; ++nRec) {
        char szName[8];
        (void)snprintf(szName, sizeof(szName), "T%d", (int)nRec);
        PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * nRec, szName);
    }
    for (nRec = PUFO_FUND_TEXT_MONTHLY; nRec <= 1333; ++nRec) {
        char szName[8];
        (void)snprintf(szName, sizeof(szName), "T%d", (int)nRec);
        PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * nRec, szName);
    }
}

static void TestFundingPanels(void)
{
    FundingToggleEnv env;
    FundingOpenEnv oenv;
    UiPanel *pNames;
    UiPanel *pBars;
    UiPanel *pArrows;
    i32 i;

    printf("the funding screen's pad and three panels\n");

    FrameLoadFont(); /* opens lang.dat / main.dat, which PadImage_Load needs */
    if (!g_bFrameFontReady) {
        printf("  FAIL lang.dat / main.dat did not open\n");
        ++g_nFailures;
        return;
    }

    FundUiFixture(&env);
    FundUiText();
    g_anFundBarUi[0] = 500;
    g_anFundBarUi[1] = -300;
    g_anFundBarUi[2] = 0;
    g_anFundBarUi[3] = 700;
    g_anFundBarUi[4] = -1;
    g_anFundLevelUi[0] = 2;
    g_anFundLevelUi[1] = -2;
    g_anFundLevelUi[2] = 1;
    g_anFundLevelUi[3] = 1;
    g_anFundLevelUi[4] = -1;

    /* ---- 1. open ---- */
    FundingScreen_Toggle(&env);

    CheckEqI32(g_bFundOpen, 1, "the open flag at 0x1595EC is set (0x5AC00)");
    Check(g_pFundUiPad != NULL, "the screen gets its own pad (0x5AC14..0x5AC40)");
    Check(g_pFundUiPad != NULL && g_pFundUiPad->uWidth != 0 && g_pFundUiPad->uHeight != 0,
          "and PadImage_Load(\"funding\", 28) filled it from the shipped archives (0x5AC44)");
    CheckEqI32(g_pFundUiPad != NULL ? g_pFundUiPad->nOriginX : -1, 0, "with the origin at x 0 (0x5AC4C)");
    CheckEqI32(g_pFundUiPad != NULL ? g_pFundUiPad->nOriginY : -1, 0, "and y 0 (0x5AC54)");

    pNames = g_pFundPanelNames;
    pBars = g_pFundPanelBars;
    pArrows = g_pFundPanelArrows;
    Check(pNames != NULL && pBars != NULL && pArrows != NULL, "and the three 5-line panels (0x5AC90, 0x5ADA8, 0x5AE88)");
    if (pNames == NULL || pBars == NULL || pArrows == NULL) {
        printf("  FAIL the panels are gone, so the rest of the test cannot run\n");
        ++g_nFailures;
        return;
    }

    CheckEqI32(pNames->nLineCount, 5, "UiPanel_ctor(5) for every panel (0x5AC70)");
    CheckEqI32(pNames->nStyle, 5, "nStyle 5, so all five rows are visible (0x5AC98)");
    CheckEqI32(pNames->nFont, 16, "nFont 16 (0x5ACAC)");
    CheckEqI32(pNames->nPosX, 10, "the names column at x 10 (0x5ACC0)");
    CheckEqI32(pNames->nPosY, 70, "the panel row base at y 70 (0x5ACD4)");
    CheckEqI32(pNames->nRectX, 5, "the names panel alone sets the click rect left to 5 (0x5ACDC)");
    CheckEqI32(pNames->nRectY, 70, "and its top to 70 (0x5ACF0)");
    CheckEqI32(pNames->nViewW, 230, "and narrows the view to 230 (0x5AD04)");
    CheckEqI32(pNames->nViewH, 320, "keeping the ctor's height 320");
    CheckEqI32(pNames->aStyleRgb[0], 0x64, "its key triple is (0x64, 0x96, 0x64) (0x5AD14)");
    CheckEqI32(pNames->aStyleRgb[1], 0x96, "...");
    CheckEqI32(pNames->aStyleRgb[2], 0x64, "...");
    CheckEqI32(pNames->aStyleRgb[3], 0x32, "the source triple is (0x32, 0x32, 0xFA) for all three (0x5AD60)");
    CheckEqI32(pNames->aStyleRgb[4], 0x32, "...");
    CheckEqI32(pNames->aStyleRgb[5], 0xFA, "...");
    CheckEqI32(pNames->aStyleRgb[6], 0xC8, "the colour triples are (0xC8, 0xB4, 0xF0) and (0x14, 0x14, 0x14) (0x5AD4C)");
    CheckEqI32(pNames->aStyleRgb[7], 0xB4, "...");
    CheckEqI32(pNames->aStyleRgb[8], 0xF0, "...");
    CheckEqI32(pNames->aStyleRgb[9], 0x14, "...");
    CheckEqI32(pNames->aStyleRgb[10], 0x14, "...");
    CheckEqI32(pNames->aStyleRgb[11], 0x14, "...");
    CheckEqI32((i32)pNames->bColorKey, 0x96, "bColorKey is written as -106 (0x5AD28)");
    CheckEqI32((i32)pNames->bRowRules, 1, "the names panel keeps the ctor's bRowRules = 1");
    CheckEqI32((i32)pNames->bSelLineHighlight, 0, "and clears bSelLineHighlight (0x5AD70)");
    CheckEqI32((i32)pNames->bAuxNumbers, 0, "and leaves bAuxNumbers at 0");
    CheckEqI32((i32)pNames->bAuxPlusSign, 0, "and bAuxPlusSign at 0");

    /* The five names, in SLOT order -- this is what pins slot 0 USA, 1 Australia, 2 Euro, 3 Asia,
     * 4 Africa (0x5AF5C..0x5B038). */
    static const char *const apzSlotName[PUFO_FUND_ROW_COUNT] = { "T1302", "T1305", "T1303", "T1304", "T1306" };
    for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        const char *psz = UiPanel_GetLineText(pNames, i);
        Check(psz != NULL && strcmp(psz, apzSlotName[i]) == 0,
              "names line is the country of that slot, in slot order");
    }

    CheckEqI32(pBars->nPosX, 100, "the bars column at x 100 (0x5ADA8 path)");
    CheckEqI32(pBars->nStyle, 5, "with the same 5 rows");
    CheckEqI32(pBars->nFont, 16, "and the same font");
    CheckEqI32(pBars->nPosY, 70, "and the same row base");
    CheckEqI32(pBars->aStyleRgb[0], 0xB4, "the bars and arrows panels use the flat key (0xB4, 0xB4, 0xB4)");
    CheckEqI32(pBars->aStyleRgb[1], 0xB4, "...");
    CheckEqI32(pBars->aStyleRgb[2], 0xB4, "...");
    CheckEqI32((i32)pBars->bRowRules, 0, "they clear bRowRules");
    CheckEqI32((i32)pBars->bAuxNumbers, 1, "and set bAuxNumbers");
    CheckEqI32((i32)pBars->bAuxPlusSign, 0, "while only the arrows panel sets bAuxPlusSign");
    CheckEqI32(pBars->nRectX, 0, "the bars panel keeps the click rect at 0");
    CheckEqI32(pBars->nRectY, 0, "...");
    CheckEqI32(pBars->nViewW, 240, "and the ctor's 240 x 320 view");
    CheckEqI32(pBars->nViewH, 320, "...");
    CheckEqI32(pBars->aStyleRgb[6], 0xC8, "with the same colour triples");
    CheckEqI32(pBars->aStyleRgb[8], 0xF0, "...");
    CheckEqI32((i32)pBars->bColorKey, 0x96, "and the same bColorKey");

    CheckEqI32(pArrows->nPosX, 170, "the arrows column at x 170 (0x5AE88 path)");
    CheckEqI32((i32)pArrows->bAuxPlusSign, 1, "and it is the panel that sets bAuxPlusSign = 1");
    CheckEqI32((i32)pArrows->bAuxNumbers, 1, "with bAuxNumbers like the bars panel");

    /* aux: the bars panel takes g_nFundBar0..4, the arrows panel g_nFundArrow0..4, and a bar that
     * is not positive wipes its own bar AND its arrow (0x5B0EC..0x5B11C). */
    static const i32 anWantBar[PUFO_FUND_ROW_COUNT]   = { 500, 0, 0, 700, 0 };
    static const i32 anWantArrow[PUFO_FUND_ROW_COUNT] = { 2, 0, 0, 1, 0 };
    for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        CheckEqI32(pBars->anLineAux[i], anWantBar[i], "the bars panel shows the bar, zeroed when it was not positive");
        CheckEqI32(pArrows->anLineAux[i], anWantArrow[i], "and the arrow survives only where the bar did");
    }
    CheckEqI32(g_nFundTotal, 1200, "the $ total is the sum of the surviving bars, 500 + 700 (0x5B124..0x5B144)");

    /* ---- 2. close ---- */
    FundingScreen_Toggle(&env);
    CheckEqI32(g_bFundOpen, 0, "the second call closes the screen (0x5B170)");
    Check(g_pFundUiPad == NULL, "the pad is freed and its slot cleared (0x5B1E8..0x5B1F0)");
    Check(g_pFundPanelNames == NULL, "and so is the names panel (0x5B17C..0x5B190)");
    Check(g_pFundPanelBars == NULL, "the bars panel (0x5B19C..0x5B1B0)");
    Check(g_pFundPanelArrows == NULL, "and the arrows panel (0x5B1BC..0x5B1D0)");

    /* ---- 3. the opener wires the close control ---- */
    FundUiFixture(&env);
    oenv.pCloseCtl = &g_fundUiClose;
    oenv.pfnCloseClick = FundCloseClick;
    Check(FundingScreen_Open(&env, &oenv) == &g_fundUiClose, "FundingScreen_Open returns the close control (0x5B590)");
    CheckEqI32(g_bFundOpen, 1, "after opening the screen through FundingScreen_Toggle (0x5B564)");
    CheckEqI32(g_fundUiClose.x, 224, "the control goes to x 224 (0x5B574)");
    CheckEqI32(g_fundUiClose.y, 1, "and y 1 (0x5B580)");
    Check(g_fundUiClose.pfnClick == FundCloseClick, "with the close thunk as its handler (0x5B58C)");
    FundingScreen_Toggle(&env);
    CheckEqI32(g_bFundOpen, 0, "and the toggle closes it again");

    /* ---- 4. the guards ---- */
    FundingScreen_Toggle(NULL);
    FundingScreen_Toggle(&env); /* a fixture with no bars panel slot */
    FundUiFixture(&env);
    env.ppPanelBars = NULL;
    FundingScreen_Toggle(&env);
    Check(g_pFundPanelBars == NULL, "a missing bars-panel slot builds no bars panel");
    Check(g_pFundPanelNames != NULL, "while the names panel is still built");
    CheckEqI32(g_nFundTotal, 0, "and the total stays 0 because the bar walk never runs");
    FundingScreen_Toggle(&env); /* free what the guarded open allocated */
    Check(g_pFundPanelNames == NULL, "the close branch frees it too");

    FrameRestoreFont();
    Asset_CloseArchives();
}

/* The reference frame for FundingScreen_Draw: the three TEXTS strings at the positions and colours
 * the original uses, plus the names panel's five strings at the geometry its own fields carry
 * (x 10, y 70 + 16 * row -- UiPanel_DrawText @ 0x3f1ac), plus the bars and arrows panels' numbers:
 * both panels carry bAuxNumbers = 1 (0x5AE40 / 0x5AF2C), so UiPanelMsg_Draw @ 0x3EBA8 takes the
 * numeric branch (0x3EC08..0x3EC90) -- bars draw plain itoa(anLineAux) at x 100 (nUnk7076 left),
 * arrows draw the "+" variant at x 170, both at y 70 + 16 * row. The toggle zeroed a bar's aux
 * when the bar was not positive and cleared the same row's arrow with it (0x5B0EC..0x5B11C), so
 * the reference mirrors that. Rails are not reproduced: the compared band excludes the two
 * rail columns (x 5, 234) and the two rail rows (y 5, 315). */
static void FundRefSimple(i32 nTotal)
{
    const char *pszTitle = g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_TITLE;
    char szTotal[64];
    char szNum[16];
    i32 i;

    memset(g_frameRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_frameRef;

    (void)Gfx_PackRgb565Pair(&g_frameColor, 210, 190, 250, 20, 20, 20); /* 0x5B414 */
    Text_DrawString(120 - Text_MeasureWidth(pszTitle) / 2, 20, pszTitle); /* 0x5B440 */
    Text_DrawString(10, 50, g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_COLUMNS); /* 0x5B480 */
    for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        Text_DrawString(10, 70 + 16 * i,
                        g_aFrameText + PUFO_TEXT_REC_STRIDE * g_anFundingSlotText[i]);
    }
    for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        const i32 nBar = (g_anFundBarUi[i] <= 0) ? 0 : g_anFundBarUi[i];
        const i32 nArrow = (nBar <= 0) ? 0 : g_anFundLevelUi[i];

        (void)snprintf(szNum, sizeof(szNum), "%d", nBar);
        Text_DrawString(100, 70 + 16 * i, szNum); /* bars panel aux, x 100 */
        if (nArrow > 0) {
            (void)snprintf(szNum, sizeof(szNum), "+%d", nArrow);
        } else {
            (void)snprintf(szNum, sizeof(szNum), "%d", nArrow);
        }
        Text_DrawString(170, 70 + 16 * i, szNum); /* arrows panel, 0x3EC34 vs 0x3EC8C */
    }
    (void)snprintf(szTotal, sizeof(szTotal), "T1151%d$", (int)nTotal); /* 0x5B4E0..0x5B4F8 */
    (void)Gfx_PackRgb565Pair(&g_frameColor, 200, 180, 240, 20, 20, 20); /* 0x5B518 */
    Text_DrawString(10, 160, szTotal); /* 0x5B52C */
    g_pMainSurface = &g_frameSurf;
}

/* The reference frame for FundingScreen_DrawExtended: the centred header, then the five rows --
 * title at (5, 60*row + 20) in (40,240,40) and three commentary lines at +35, +45, +55 in
 * (180,180,240). aWant[row][k] is the TEXTS record the row is expected to show. */
static void FundRefReport(const i32 aWant[PUFO_FUND_ROW_COUNT][3])
{
    const char *pszHead = g_aFrameText + PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_MONTHLY;
    i32 nRow;

    memset(g_frameRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_frameRef;

    (void)Gfx_PackRgb565Pair(&g_frameColor, 240, 240, 240, 20, 20, 20); /* 0x5B5C8, before the pad */
    Text_DrawString(120 - Text_MeasureWidth(pszHead) / 2, 5, pszHead); /* 0x5B7D4 */

    for (nRow = 0; nRow < PUFO_FUND_ROW_COUNT; ++nRow) {
        (void)Gfx_PackRgb565Pair(&g_frameColor, 40, 240, 40, 20, 20, 20);   /* 0x5BFC8 */
        Text_DrawString(5, 60 * nRow + 20,
                        g_aFrameText + PUFO_TEXT_REC_STRIDE * (PUFO_FUND_TEXT_ROW0 + nRow));
        (void)Gfx_PackRgb565Pair(&g_frameColor, 180, 180, 240, 20, 20, 20); /* 0x5C018 */
        Text_DrawString(5, 60 * nRow + 35, g_aFrameText + PUFO_TEXT_REC_STRIDE * aWant[nRow][0]);
        Text_DrawString(5, 60 * nRow + 45, g_aFrameText + PUFO_TEXT_REC_STRIDE * aWant[nRow][1]);
        Text_DrawString(5, 60 * nRow + 55, g_aFrameText + PUFO_TEXT_REC_STRIDE * aWant[nRow][2]);
    }
    g_pMainSurface = &g_frameSurf;
}

static void TestFundingReport(void)
{
    /* Shares against a -80000 baseline give one of each arrow: -60000 -> -2, -20000 -> -1,
     * 0 -> 0, 20000 -> +1, 60000 -> +2 (Funding_DeltaToArrow @ 0x5ab34, thresholds total/8 and
     * total/2). The report reads them in ROW order through the permutation {0, 2, 3, 1, 4}. */
    static const i32 anShare[PUFO_FUND_ROW_COUNT] = { -60000, 20000, -20000, 0, 60000 };
    /* row 0 USA: arrow -2, group 0, bar negative -> the red tail 1311
     * row 1 Euro: arrow -1, group 1, bar positive -> the plain tail 1318
     * row 2 Asia: arrow 0, group 1
     * row 3 Australia: arrow +1, group 0
     * row 4 Africa: arrow +2, group 0 */
    static const i32 aPlain[PUFO_FUND_ROW_COUNT][3] = {
        { 1309, 1310, 1311 }, { 1313, 1314, 1318 }, { 1319, 1320, 1323 },
        { 1326, 1327, 1328 }, { 1331, 1332, 1333 }
    };
    /* The same month with Euro's bar negative: only the tail record changes (0x5BE34), which is what
     * proves the BAR and not the share decides the red variant. */
    static const i32 aRedEuro[PUFO_FUND_ROW_COUNT][3] = {
        { 1309, 1310, 1311 }, { 1313, 1314, 1317 }, { 1319, 1320, 1323 },
        { 1326, 1327, 1328 }, { 1331, 1332, 1333 }
    };
    /* Month 1: every negative arrow is drawn as 0 (0x5B8CC..0x5B8F0), so rows 0 and 1 fall to the
     * arrow-0 wording -- row 0 the group-0 pair 1321/1322, row 1 the group-1 pair 1319/1320. */
    static const i32 aClamped[PUFO_FUND_ROW_COUNT][3] = {
        { 1321, 1322, 1323 }, { 1319, 1320, 1323 }, { 1319, 1320, 1323 },
        { 1326, 1327, 1328 }, { 1331, 1332, 1333 }
    };
    FundingToggleEnv tenv;
    FundingDrawEnv denv;
    i32 i;

    printf("the funding screen and the monthly report, on the real frame\n");

    FrameLoadFont();
    if (!g_bFrameFontReady) {
        printf("  FAIL the real font did not load, so the frame cannot be checked\n");
        ++g_nFailures;
        return;
    }

    SoftSurf_ctor(&g_frameSurf);
    (void)SoftSurf_AllocPixels(&g_frameSurf, FRAME_W, FRAME_H, PIXMODE_16BPP);
    SoftSurf_ctor(&g_frameRef);
    (void)SoftSurf_AllocPixels(&g_frameRef, FRAME_W, FRAME_H, PIXMODE_16BPP);
    g_pMainSurface = &g_frameSurf;
    g_blitState.nPixelMode = 2;
    g_frameColor.pnGamma = &g_nFrameGamma; /* factor 1.0, so the colour pairs pass through */
    g_frameColor.nFadeCount = 0;

    FundUiFixture(&tenv);
    FundUiText();

    memset(&denv, 0, sizeof(denv));
    denv.pbOpen = &g_bFundOpen;
    denv.pTextRecs = (const u8 *)g_aFrameText;
    denv.pnTotal = &g_nFundTotal;
    denv.pPad = NULL; /* the port guards a null pad; the pad itself is pinned in TestFundingPanels */
    denv.pCloseCtl = &g_fundUiClose;
    denv.pfnCtlBlit = FrameBlitRec;
    denv.pfnCtlBlitEx = FrameBlitRec;
    denv.pColor = &g_frameColor;
    denv.anShare = g_anFundShareUi;
    denv.anBaseline = g_anFundBaseUi;
    denv.anBar = g_anFundBarUi;

    /* ---- 1. FundingScreen_Draw while the screen is closed ---- */
    FrameZero();
    FundingScreen_Draw(&denv);
    Check(FrameBandEmpty(7, 233, 20, 314), "a closed funding screen draws no text (0x5B210)");
    CheckEqI32(FramePix(5, 1), 0, "not even the frame rails");
    CheckEqI32(g_nFrameBlitCount, 0, "and no control pad");

    /* ---- 2. open it through the real opener and draw ---- */
    for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        g_anFundBarUi[i] = 100 + 100 * i;
        g_anFundLevelUi[i] = 1;
    }
    {
        FundingOpenEnv oenv;
        oenv.pCloseCtl = &g_fundUiClose;
        oenv.pfnCloseClick = FundCloseClick;
        (void)FundingScreen_Open(&tenv, &oenv);
    }
    /* the draw reads the three panels the toggle built, exactly as the original reads the three
     * globals at 0x1595E4 / 0x1595E0 / 0x1595E8 */
    denv.pPanelNames = g_pFundPanelNames;
    denv.pPanelBars = g_pFundPanelBars;
    denv.pPanelArrows = g_pFundPanelArrows;
    Check(denv.pPanelNames != NULL && denv.pPanelBars != NULL && denv.pPanelArrows != NULL,
          "the opener built the three panels the draw then reads");
    FrameZero();
    FundingScreen_Draw(&denv);

    CheckEqI32(FramePix(5, 1), 0x632C, "the shared frame rails are the same 96 blits the other screens use, index 100 at (5, 1)");
    CheckEqI32(FramePix(234, 312), 0x738E, "with the same falling side index at (234, 312)");
    CheckEqI32(FramePix(1, 5), 0x632C, "and the same climbing top index at (1, 5)");
    CheckEqI32(FramePix(1, 315), 0x632C, "repeated at the bottom rail");
    CheckEqI32(g_nFrameBlitCount, 1, "the close control is the only control blitted (0x5B3C0)");
    CheckEqI32(g_fundUiClosePad.nOriginX, 224, "its pad follows the x the opener set (0x5B574)");
    CheckEqI32(g_fundUiClosePad.nOriginY, 1, "and the y (0x5B580)");

    FundRefSimple(g_nFundTotal);
    Check(FrameBandSame(7, 233, 20, 314),
          "TEXTS[1149] centred at y 20, TEXTS[1150] at (10, 50), the five slot-order names at (10, 70 + 16 * row) and TEXTS[1151] with the total and a $ at (10, 160), in the original's colours");

    /* ---- 3. the monthly report, month 4 ---- */
    for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        g_anFundShareUi[i] = anShare[i];
        g_anFundBaseUi[i] = -80000;
    }
    g_anFundBarUi[0] = -500;
    g_anFundBarUi[1] = 300;
    g_anFundBarUi[2] = 50;
    g_anFundBarUi[3] = 100;
    g_anFundBarUi[4] = 700;
    denv.nMonthCounter = 4;

    FrameZero();
    FundingScreen_DrawExtended(&denv);
    FundRefReport(aPlain);
    Check(FrameBandSame(7, 233, 6, 314),
          "month 4: the centred TEXTS[1301] header and the five rows with arrows -2, -1, 0, +1, +2 draw TEXTS[1309/1310/1311], [1313/1314/1318], [1319/1320/1323], [1326/1327/1328], [1331/1332/1333] under the TEXTS[1302..1306] titles");
    Check(!FrameBandEmpty(7, 233, 20, 34), "the first row's title band really has pixels");

    /* ---- 4. the same month with Euro's bar negative ---- */
    g_anFundBarUi[2] = -50;
    FrameZero();
    FundingScreen_DrawExtended(&denv);
    FundRefReport(aRedEuro);
    Check(FrameBandSame(7, 233, 6, 314),
          "a negative BAR, not a negative share, is what swaps row 1's third line to the red variant TEXTS[1317] (0x5B810..0x5B820)");

    /* ---- 5. month 1 clamps every negative arrow to 0 ---- */
    g_anFundBarUi[2] = 50;
    denv.nMonthCounter = 1;
    FrameZero();
    FundingScreen_DrawExtended(&denv);
    FundRefReport(aClamped);
    Check(FrameBandSame(7, 233, 6, 314),
          "below month 3 the two falling rows are drawn with the arrow-0 commentary instead (0x5B8CC..0x5B8F0)");

    /* ---- 6. the report has no open gate: it draws even after the screen is closed ---- */
    denv.nMonthCounter = 4;
    FundingScreen_Toggle(&tenv);
    CheckEqI32(g_bFundOpen, 0, "the screen is closed");
    FrameZero();
    FundingScreen_DrawExtended(&denv);
    FundRefReport(aPlain);
    Check(!FrameBandEmpty(7, 233, 20, 314),
          "yet FundingScreen_DrawExtended still draws -- it has no open-flag gate (0x5B59C)");
    Check(FrameBandSame(7, 233, 6, 314), "and draws the same report as before");

    /* ---- 7. the guards ---- */
    FrameZero();
    FundingScreen_Draw(NULL);
    FundingScreen_DrawExtended(NULL);
    denv.anShare = NULL;
    FundingScreen_DrawExtended(&denv);
    Check(FrameBandEmpty(7, 233, 6, 314), "a missing share array refuses the whole report");

    FrameRestoreFont();
    Asset_CloseArchives();
}

/* ------------------------------------------------- BaseFac_BuildStatusMsg @ 0x6a4bc
 *
 * The geoscape status line, thirteen arms on a character case. The six label prefixes are shipped
 * lang.dat TEXTS records 1334, 1339, 1340, 1341, 1342 and 1343, so the fixture loads the real table:
 * a wrong record index still produces a string, it just produces the wrong one. */

static u8                g_fsGrid[1524];
static BaseSiteRec       g_fsSites[PUFO_FAC_BASE_COUNT];
static ObjectRecFull     g_fsObj[149];
static ObjectRecFull    *g_fsApObj[149];
static BaseFacFlag       g_fsFac[PUFO_BASEFAC_COUNT];
static ResearchTopicLive g_fsTopics[PUFO_RESEARCH_TOPIC_COUNT];
static Ship              g_fsCraft[4];
static Ship             *g_fsApCraft[4];
static GameTime          g_fsTime;
static char              g_fsMsg[108];
static char              g_fsExtra[104];
static char              g_fsScratch[112];
static u8               *g_fsTexts;
static BaseFacStatusEnv  g_fsEnv;

static const char *FsRec(i32 nRec)
{
    return (const char *)(g_fsTexts + PUFO_TEXT_REC_STRIDE * (size_t)nRec);
}

static void CheckStr(const char *pszGot, const char *pszWant, const char *pszWhat)
{
    if (strcmp(pszGot, pszWant) == 0) {
        printf("  ok   %s (%s)\n", pszWhat, pszWant);
    } else {
        printf("  FAIL %s: got [%s], want [%s]\n", pszWhat, pszGot, pszWant);
        ++g_nFailures;
    }
}

static bool FsLoadTexts(void)
{
    AssetReader rdr;
    u8 aHdr[PUFO_TEXT_HDR_SIZE];
    u32 cb, n, i;

    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, "texts.dat")) {
        return false;
    }
    cb = AssetReader_Size(&rdr);
    n = (cb >= PUFO_TEXT_HDR_SIZE)
      ? (cb - PUFO_TEXT_HDR_SIZE) / PUFO_TEXT_REC_SIZE_DISK : 0u;
    if (n <= (u32)PUFO_FAC_STATUS_TEXT_CRAFT) {
        return false;
    }
    if (!AssetReader_Read(&rdr, aHdr, PUFO_TEXT_HDR_SIZE)) {
        return false;
    }
    /* The capacity, not just the loaded count: Ufopaedia_Open reads records 1646..1654 and 1650..1654
     * are past what TEXTS.DAT fills -- in the app those slots exist and stay zero (the boot blob is
     * PUFO_TEXT_REC_CAPACITY records), and the test has to model the same block. */
    g_fsTexts = malloc((size_t)PUFO_TEXT_REC_CAPACITY * PUFO_TEXT_REC_STRIDE);
    if (g_fsTexts == NULL) {
        return false;
    }
    memset(g_fsTexts, 0, (size_t)PUFO_TEXT_REC_CAPACITY * PUFO_TEXT_REC_STRIDE);
    for (i = 0; i < n; ++i) {
        if (!AssetReader_Read(&rdr, g_fsTexts + (size_t)i * PUFO_TEXT_REC_STRIDE,
                              PUFO_TEXT_REC_SIZE_DISK)) {
            break;
        }
    }
    return true;
}

/* The facility-kind cell the '&' arm reads: abFacilityGrid + 480 + 144 * base + 6 * row + col. */
static u8 *FsCell(i32 nBase, i32 nRow, i32 nCol)
{
    return g_fsGrid + PUFO_FAC_GRID_FACILITY_BASE
                   + PUFO_FAC_GRID_BASE_STRIDE * (size_t)nBase
                   + PUFO_FAC_GRID_ROW_STRIDE * (size_t)nRow
                   + (size_t)nCol;
}

static void FsFixture(void)
{
    i32 i;

    memset(g_fsGrid, 0, sizeof(g_fsGrid));
    memset(g_fsObj, 0, sizeof(g_fsObj));
    memset(g_fsFac, 0, sizeof(g_fsFac));
    memset(g_fsTopics, 0, sizeof(g_fsTopics));
    memset(g_fsCraft, 0, sizeof(g_fsCraft));

    for (i = 0; i < 149; ++i) {
        g_fsApObj[i] = &g_fsObj[i];
    }
    PutStr(g_fsObj[1].szName, "Pistol");
    PutStr(g_fsObj[25].szName, "Craft cannon");
    PutStr(g_fsObj[105].szName, "Elerium-115");

    for (i = 0; i < PUFO_FAC_BASE_COUNT; ++i) {
        snprintf(g_fsSites[i].abName, sizeof(g_fsSites[i].abName), "Base%c", (int)('A' + i));
    }

    PutStr(g_fsFac[FAC_LIVING_QUARTERS].szName, "Living Quarters");
    PutStr(g_fsFac[FAC_LABORATORY].szName, "Laboratory");
    PutStr(g_fsFac[FAC_WORKSHOP].szName, "Workshop");
    PutStr(g_fsFac[FAC_HANGAR].szName, "Hangar");

    /* The '&' cell under test is base 2, row 1, col 3. Its three neighbours hold different kinds, so
     * an off-by-one in the 144 / 6 / 1 arithmetic shows up as a different facility name. */
    *FsCell(2, 1, 3) = FAC_HANGAR;
    *FsCell(2, 0, 3) = FAC_LABORATORY;
    *FsCell(2, 1, 4) = FAC_LIVING_QUARTERS;
    *FsCell(3, 1, 3) = FAC_WORKSHOP;

    /* '%' names each line from a topic's wGate4 record. Record 3's abUnk0[0] is the index the second
     * line adds to; record 2's own abUnk0[0] is a decoy that must NOT be the one used. */
    g_fsTopics[2].wGate4    = 1446;
    g_fsTopics[2].abUnk0[0] = 7;    /* decoy: 5 + 7 = 12 -> record 1449 */
    g_fsTopics[3].abUnk0[0] = 99;   /* real: 5 + 99 = 104 -> record 1448 */
    g_fsTopics[12].wGate4   = 1449;
    g_fsTopics[104].wGate4  = 1448;
    PutTextRec(g_fsTexts, 1446, "TOPIC-1446");
    PutTextRec(g_fsTexts, 1448, "TOPIC-1448");
    PutTextRec(g_fsTexts, 1449, "TOPIC-1449");

    g_fsCraft[0].bType = 3;
    g_fsCraft[0].bCraftType = 0;   /* TEXTS[137] plus itoa(bType) */
    for (i = 0; i < 4; ++i) {
        g_fsApCraft[i] = &g_fsCraft[i];
    }

    memset(&g_fsEnv, 0, sizeof(g_fsEnv));
    g_fsEnv.abFacilityGrid = g_fsGrid;
    g_fsEnv.apSiteRecs     = g_fsSites;
    g_fsEnv.apObjects      = g_fsApObj;
    g_fsEnv.nObjectCount   = 149;
    g_fsEnv.paFac          = g_fsFac;
    g_fsEnv.aTopics        = g_fsTopics;
    g_fsEnv.apCraft        = g_fsApCraft;
    g_fsEnv.pTextRecs      = g_fsTexts;
    g_fsEnv.pTime          = &g_fsTime;
    g_fsEnv.pszMsg         = g_fsMsg;
    g_fsEnv.cchMsg         = sizeof(g_fsMsg);
    g_fsEnv.pszExtra       = g_fsExtra;
    g_fsEnv.cchExtra       = sizeof(g_fsExtra);
    g_fsEnv.pszScratch     = g_fsScratch;
    g_fsEnv.cchScratch     = sizeof(g_fsScratch);
}

/* Sentinels on purpose: an arm that does not write a line has to leave the sentinel in place. */
static char *FsRun(i32 nCase, i32 nBaseIdx, i32 nFacRow, i32 nFacCol)
{
    PutStr(g_fsMsg, "<msg>");
    PutStr(g_fsExtra, "<extra>");
    g_fsTime.bPaused = 0;
    return BaseFac_BuildStatusMsg(&g_fsEnv, nCase, nBaseIdx, nFacRow, nFacCol);
}

static void TestFacStatusLine(void)
{
    static const i32 aAll[13] = { ' ', '!', '"', '#', '$', '%', '&',
                                  '\'', '(', ')', '*', '+', ',' };
    static const i32 aOneLine[3] = { '$', ')', '*' };
    const char *const apszArch[2] = { "lang.dat", "main.dat" };
    char szWant[256];
    char *pRet;
    i32 i;

    printf("the base-facility status line\n");

    if (!Asset_OpenArchives(apszArch)) {
        printf("  FAIL lang.dat / main.dat did not open\n");
        ++g_nFailures;
        return;
    }
    if (!FsLoadTexts()) {
        printf("  FAIL lang.dat texts.dat did not load\n");
        ++g_nFailures;
        return;
    }
    FsFixture();

    /* The six prefixes, checked against the shipped file. Every arm below compares against these
     * records, so a wrong index would have to be caught here. */
    CheckStr(FsRec(PUFO_FAC_STATUS_TEXT_BASE),
             "\xC1\xE0\xE7\xE0\x3A\x20", "record 1334 is the shipped \"Base: \"");
    CheckStr(FsRec(PUFO_FAC_STATUS_TEXT_BASE_SPACED),
             "\xC1\xE0\xE7\xE0\x20\x3A\x20", "record 1339 is the same word with the space before the colon");
    CheckStr(FsRec(PUFO_FAC_STATUS_TEXT_PROD),
             "\xCF\xF0\xEE\xE8\xE7\xE2\xEE\xE4\xF1\xF2\xE2\xEE\x3A\x20", "record 1340 is Production:");
    CheckStr(FsRec(PUFO_FAC_STATUS_TEXT_RESEARCH),
             "\xC8\xF1\xF1\xEB\xE5\xE4\xEE\xE2\xE0\xED\xE8\xE5\x3A\x20", "record 1341 is Research:");
    CheckStr(FsRec(PUFO_FAC_STATUS_TEXT_MODULE),
             "\xCC\xEE\xE4\xF3\xEB\xFC\x3A\x20", "record 1342 is Module:");
    CheckStr(FsRec(PUFO_FAC_STATUS_TEXT_CRAFT),
             "\xCA\xEE\xF0\xE0\xE1\xEB\xFC\x3A\x20", "record 1343 is Craft:");
    Check(strcmp(FsRec(1334), FsRec(1339)) != 0
          && strcmp(FsRec(1340), FsRec(1341)) != 0
          && strcmp(FsRec(1342), FsRec(1343)) != 0,
          "the prefixes are distinct, so a wrong index cannot pass by accident");

    /* The clock is paused before the switch, so it happens on every arm including the default. */
    pRet = FsRun(0x2D, 2, 1, 25);
    Check(pRet == NULL, "case 0x2D is past the jumptable's thirteen arms");
    CheckStr(g_fsMsg, "<msg>", "and it leaves the first line alone");
    CheckStr(g_fsExtra, "<extra>", "and the second line alone");
    CheckEqI32(g_fsTime.bPaused, 1, "GameTime_Pause still runs, it sits ahead of the switch at 0x6A4D4");
    pRet = FsRun(0x1F, 2, 1, 25);
    CheckStr(g_fsMsg, "<msg>", "a case below 0x20 is untouched too");
    for (i = 0; i < 13; ++i) {
        (void)FsRun(aAll[i], 2, 1, 25);
        if (g_fsTime.bPaused != 1) {
            break;
        }
    }
    CheckEqI32(i, 13, "all thirteen arms pause the clock");

    /* ' ' */
    pRet = FsRun(' ', 2, 1, 25);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1334), g_fsSites[2].abName);
    CheckStr(g_fsMsg, szWant, "' ' puts the damage label and the base name on the first line");
    CheckStr(g_fsExtra, "Product: Craft cannon",
             "' ' names the object on the second, from a real literal at 0xAB73C");
    Check(pRet == g_fsExtra, "' ' returns the line it last wrote");

    /* '!' and '"' are the same arm twice. */
    pRet = FsRun('!', 2, 1, 25);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1339), g_fsSites[2].abName);
    CheckStr(g_fsMsg, szWant, "'!' uses the production prefix on the first line");
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1340), "Craft cannon");
    CheckStr(g_fsExtra, szWant, "and the research prefix on the second");
    pRet = FsRun('"', 2, 1, 25);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1339), g_fsSites[2].abName);
    CheckStr(g_fsMsg, szWant, "'\"' is 0x6A5A0 and 0x6A610 written out twice");
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1340), "Craft cannon");
    CheckStr(g_fsExtra, szWant, "with the same result");

    /* '#' shares the first line with ' ' and the second with '!'. */
    pRet = FsRun('#', 2, 1, 25);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1334), g_fsSites[2].abName);
    CheckStr(g_fsMsg, szWant, "'#' keeps the damage label on the first line");
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1340), "Craft cannon");
    CheckStr(g_fsExtra, szWant, "where ' ' had Product: on the second");

    /* '$', ')' and '*' all land on the same LABEL_60 tail. */
    for (i = 0; i < 3; ++i) {
        pRet = FsRun(aOneLine[i], 2, 1, 25);
        snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1334), g_fsSites[2].abName);
        CheckStr(g_fsMsg, szWant, "the one-line arms all reach the same tail");
        CheckStr(g_fsExtra, "<extra>", "and none of them touches the second line");
        Check(pRet == g_fsMsg, "and they return the first line");
    }

    /* '%' */
    Check(strcmp(FsRec(1448), FsRec(1449)) != 0,
          "the two candidate topic names differ, so the off-by-one check below is not vacuous");
    pRet = FsRun('%', 2, 5, 999);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1341), "TOPIC-1446");
    CheckStr(g_fsMsg, szWant, "'%' names the module from the topic's own wGate4 record");
    CheckStr(g_fsExtra, "<extra>", "and with nFacCol not 1000 it stops at the first line");
    Check(pRet == g_fsMsg, "returning that line");
    pRet = FsRun('%', 2, 5, PUFO_FAC_STATUS_RESEARCH_TAG);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1341), "TOPIC-1448");
    CheckStr(g_fsExtra, szWant,
             "the 1000 tag opens the second line, named from topic 5 + record 3's abUnk0[0]");
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1341), "TOPIC-1446");
    CheckStr(g_fsMsg, szWant, "and the first line still names topic 2");

    /* '&' */
    pRet = FsRun('&', 2, 1, 3);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1342), "Hangar");
    CheckStr(g_fsExtra, szWant, "'&' names the facility the grid cell holds");
    pRet = FsRun('&', 2, 0, 3);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1342), "Laboratory");
    CheckStr(g_fsExtra, szWant, "one row earlier in the same base is a different facility");
    pRet = FsRun('&', 2, 1, 4);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1342), "Living Quarters");
    CheckStr(g_fsExtra, szWant, "one column later too");
    pRet = FsRun('&', 3, 1, 3);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1342), "Workshop");
    CheckStr(g_fsExtra, szWant, "and the 144-byte per-base stride is right");
    pRet = FsRun('&', 2, 6, 3);
    snprintf(szWant, sizeof(szWant), "%s", FsRec(1342));
    CheckStr(g_fsExtra, szWant, "a row past the grid's last six appends nothing");

    /* '\'' */
    pRet = FsRun('\'', 2, 0, 25);
    snprintf(szWant, sizeof(szWant), "%s%s%s", FsRec(1343), FsRec(137), "3");
    CheckStr(g_fsExtra, szWant, "'\\'' appends the craft label plus Ship.bType");
    CheckStr(g_fsScratch, "Craft cannon", "and leaves the object name in the scratch for the next call");
    pRet = FsRun('\'', 2, 0, PUFO_FAC_STATUS_NO_OBJECT);
    CheckStr(g_fsScratch, "", "the 255 sentinel clears the scratch instead");

    /* '(' uses nFacRow for the object, not nFacCol. */
    pRet = FsRun('(', 2, 105, 25);
    CheckStr(g_fsScratch, "Elerium-115", "'(' indexes the object with nFacRow, not nFacCol");
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1334), g_fsSites[2].abName);
    CheckStr(g_fsMsg, szWant, "and still writes the first line");

    /* '+' writes the second line first, then overwrites the first. */
    pRet = FsRun('+', 2, 0, 25);
    snprintf(szWant, sizeof(szWant), "%s%s%s", FsRec(1343), FsRec(137), "3");
    CheckStr(g_fsExtra, szWant, "'+' puts the craft name on the second line");
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1334), g_fsSites[2].abName);
    CheckStr(g_fsMsg, szWant,
             "and the craft name that passed through the first line is overwritten");

    /* ',' reads back what a previous call left in the scratch. */
    PutStr(g_fsScratch, "carried name");
    pRet = FsRun(',', 2, 0, 25);
    snprintf(szWant, sizeof(szWant), "%s%s", FsRec(1343), "carried name");
    CheckStr(g_fsExtra, szWant, "',' appends whatever the scratch holds");

    /* The port's bounds on the original's untested indices. */
    pRet = FsRun(' ', -1, 1, 25);
    snprintf(szWant, sizeof(szWant), "%s", FsRec(1334));
    CheckStr(g_fsMsg, szWant, "a negative base index appends no base name");
    CheckStr(g_fsExtra, "Product: Craft cannon", "and the object line is unaffected");
    pRet = FsRun(' ', 2, 1, 9999);
    CheckStr(g_fsExtra, "Product: ", "an object index past the array appends no name");
    pRet = FsRun('&', 99, 1, 3);
    snprintf(szWant, sizeof(szWant), "%s", FsRec(1342));
    CheckStr(g_fsExtra, szWant, "a base index past the eight grid blocks reads no cell");

    free(g_fsTexts);
    g_fsTexts = NULL;
}

/* ------------------------------------------------- BasePlane_ResetDetailState @ 0x79cb8
 *
 * The six craft-weapon rows of the plane screen's stat panel. The fixture uses the shipped
 * OBJECTS.DAT layout: 149 records of 208 bytes, slots 25..30 being exactly the six craft weapons
 * (Craft cannon, Stingray launcher, Avalanche launcher, Laser cannon, Plasma cannon, Fusion ball
 * launcher -- their names sit 208 bytes apart in main.dat at 0x2A082E, 0x2A08FE, ... 0x2A0C3E),
 * with 24 Psi-arm and 31..35 being the soldier and vehicle weapons that must never become a row. */

static ObjectRecFull g_psObj[149];
static ObjectRecFull *g_psApObj[149];
static i32 g_psStat[6];
static i32 g_psRows;
static BasePlaneDetailEnv g_psEnv;

static const char *const g_apPsWeaponName[6] = {
    "Craft cannon", "Stingray launcher", "Avalanche launcher",
    "Laser cannon", "Plasma cannon", "Fusion ball launcher"
};

static void PsFixture(void)
{
    i32 i;

    for (i = 0; i < 149; ++i) {
        g_psApObj[i] = &g_psObj[i];
    }
    for (i = 0; i < 6; ++i) {
        PutStr(g_psObj[PUFO_PLANE_WEAPON_FIRST + i].szName, g_apPsWeaponName[i]);
    }
    PutStr(g_psObj[24].szName, "Psi-arm");
    PutStr(g_psObj[31].szName, "Tank - cannon");
    PutStr(g_psObj[32].szName, "Tank - rocket launcher");
    PutStr(g_psObj[34].szName, "Hovertank - plasma");
    PutStr(g_psObj[117].szName, "Skyranger");

    g_psEnv.apObjects    = g_psApObj;
    g_psEnv.nObjectCount = 149;
    g_psEnv.nBaseIdx     = 0;
    g_psEnv.pnCraftCannon  = &g_psStat[0];
    g_psEnv.pnStingray     = &g_psStat[1];
    g_psEnv.pnAvalanche    = &g_psStat[2];
    g_psEnv.pnLaserCannon  = &g_psStat[3];
    g_psEnv.pnPlasmaCannon = &g_psStat[4];
    g_psEnv.pnFusionBall   = &g_psStat[5];
    g_psEnv.pnRowCount     = &g_psRows;
}

static void PsClearCounts(void)
{
    i32 i, c;

    for (i = 0; i < 149; ++i) {
        for (c = 0; c < PUFO_FAC_BASE_COUNT; ++c) {
            g_psObj[i].auPerBaseVal[c] = 0;
        }
    }
}

/* Stale on purpose: 0x79CC0..0x79D24 clears the seven globals before it looks at any data, so a
 * previous screen visit's values must not survive into the result. */
static void PsSeedStale(void)
{
    i32 i;

    for (i = 0; i < 6; ++i) {
        g_psStat[i] = 777;
    }
    g_psRows = 99;
}

static void TestPlaneStatRows(void)
{
    static const i32 aHold[6] = { 5, 0, 7, 0, 9, 0 };
    static const i32 aIdx[3] = { 0, 3, 7 };
    static const i32 aWantCraft[3] = { 5, 9, 13 };
    static const i32 aWantSting[3] = { 0, 21, 0 };
    i32 i;
    i32 nRet;

    printf("the plane screen's stat rows\n");

    PsFixture();

    /* 1. Nothing to look at, and the reset still happens. */
    PsClearCounts();
    g_psEnv.nObjectCount = 0;
    PsSeedStale();
    nRet = BasePlane_ResetDetailState(&g_psEnv);
    CheckEqI32(g_psRows, 1, "an empty object list still leaves the panel its header row");
    CheckEqI32(nRet, 1, "and the return value is that row count");
    for (i = 0; i < 6; ++i) {
        CheckEqI32(g_psStat[i], 0, "the six counts are cleared before the loop, not after it");
    }
    g_psEnv.nObjectCount = 149;

    /* 2. Each slot has its own global, and the values are distinct so a swapped switch arm shows. */
    PsClearCounts();
    for (i = 0; i < 6; ++i) {
        g_psObj[PUFO_PLANE_WEAPON_FIRST + i].auPerBaseVal[0] = (u16)(11 + i);
    }
    PsSeedStale();
    nRet = BasePlane_ResetDetailState(&g_psEnv);
    for (i = 0; i < 6; ++i) {
        CheckEqI32(g_psStat[i], 11 + i, "each weapon slot stores its count into its own global");
    }
    CheckEqI32(g_psRows, 7, "six held weapons give six rows plus the header");
    CheckEqI32(nRet, g_psRows, "the return value and the global agree");

    /* 3. Slots outside 25..30 are read and thrown away. */
    PsClearCounts();
    g_psObj[0].auPerBaseVal[0] = 8;
    g_psObj[24].auPerBaseVal[0] = 3;   /* Psi-arm */
    g_psObj[31].auPerBaseVal[0] = 4;   /* Tank - cannon */
    g_psObj[32].auPerBaseVal[0] = 5;   /* Tank - rocket launcher */
    g_psObj[34].auPerBaseVal[0] = 6;   /* Hovertank - plasma */
    g_psObj[117].auPerBaseVal[0] = 7;  /* Skyranger itself */
    g_psObj[148].auPerBaseVal[0] = 9;
    PsSeedStale();
    BasePlane_ResetDetailState(&g_psEnv);
    for (i = 0; i < 6; ++i) {
        CheckEqI32(g_psStat[i], 0, "a non-craft slot's count is read and then discarded");
    }
    CheckEqI32(g_psRows, 1, "and it adds no row -- the switch has no arm outside 25..30");

    /* 4. The boundary itself, at both ends. */
    PsClearCounts();
    g_psObj[24].auPerBaseVal[0] = 12;
    g_psObj[25].auPerBaseVal[0] = 13;
    g_psObj[30].auPerBaseVal[0] = 14;
    g_psObj[31].auPerBaseVal[0] = 15;
    PsSeedStale();
    BasePlane_ResetDetailState(&g_psEnv);
    CheckEqI32(g_psStat[0], 13, "slot 25, Craft cannon, is the first row");
    CheckEqI32(g_psStat[5], 14, "slot 30, Fusion ball launcher, is the last");
    CheckEqI32(g_psRows, 3, "24 Psi-arm and 31 Tank - cannon sit just outside and add nothing");

    /* 5. A weapon the base holds none of gets no row and keeps a zero count. */
    PsClearCounts();
    for (i = 0; i < 6; ++i) {
        g_psObj[PUFO_PLANE_WEAPON_FIRST + i].auPerBaseVal[0] = (u16)aHold[i];
    }
    PsSeedStale();
    BasePlane_ResetDetailState(&g_psEnv);
    for (i = 0; i < 6; ++i) {
        CheckEqI32(g_psStat[i], aHold[i], "an unheld weapon stays at zero rather than keeping its old value");
    }
    CheckEqI32(g_psRows, 4, "only the three held weapons get rows");

    /* 6. The loop bound is Map_nObjectCount, so the slot has to be inside it. */
    PsClearCounts();
    g_psObj[25].auPerBaseVal[0] = 4;
    g_psEnv.nObjectCount = 25; /* slots 0..24 */
    PsSeedStale();
    BasePlane_ResetDetailState(&g_psEnv);
    CheckEqI32(g_psStat[0], 0, "a count of 25 objects stops one short of slot 25");
    CheckEqI32(g_psRows, 1, "so Craft cannon gets no row");
    g_psEnv.nObjectCount = 26;
    PsSeedStale();
    BasePlane_ResetDetailState(&g_psEnv);
    CheckEqI32(g_psStat[0], 4, "one more object reaches slot 25");
    CheckEqI32(g_psRows, 2, "and the row appears");
    g_psEnv.nObjectCount = 149;

    /* 7. The column is the selected base's, not a fixed one. */
    PsClearCounts();
    g_psObj[25].auPerBaseVal[0] = 5;
    g_psObj[25].auPerBaseVal[3] = 9;
    g_psObj[25].auPerBaseVal[7] = 13;
    g_psObj[26].auPerBaseVal[3] = 21;
    for (i = 0; i < 3; ++i) {
        g_psEnv.nBaseIdx = aIdx[i];
        PsSeedStale();
        BasePlane_ResetDetailState(&g_psEnv);
        CheckEqI32(g_psStat[0], aWantCraft[i], "the count comes from the selected base's column");
        CheckEqI32(g_psStat[1], aWantSting[i], "a weapon only base 3 holds shows up only for base 3");
    }
    g_psEnv.nBaseIdx = 0;

    /* 8. The out-of-range guard. auPerBaseVal[-1] is the u16 at record offset 0x28 (bTwoHanded /
     * bUnk41) and auPerBaseVal[8] is aItemQty[0] at 0x3a; the original reads both, the port refuses
     * and lands on the same observable result -- six zeroes and one row. */
    PsClearCounts();
    g_psObj[25].auPerBaseVal[0] = 6;
    g_psObj[25].bTwoHanded = 0x34;
    g_psObj[25].bUnk41 = 0x12;   /* auPerBaseVal[-1] would read 0x1234 */
    g_psObj[25].aItemQty[0] = 0x5678; /* auPerBaseVal[8] would read 0x5678 */
    g_psEnv.nBaseIdx = -1;
    PsSeedStale();
    BasePlane_ResetDetailState(&g_psEnv);
    CheckEqI32(g_psStat[0], 0, "a negative base index does not read the word below the column array");
    CheckEqI32(g_psRows, 1, "and the panel still gets just its header row");
    g_psEnv.nBaseIdx = PUFO_FAC_BASE_COUNT;
    PsSeedStale();
    BasePlane_ResetDetailState(&g_psEnv);
    CheckEqI32(g_psStat[0], 0, "a base index past the eight columns does not read aItemQty[0]");
    CheckEqI32(g_psRows, 1, "and the same one-row result");
    g_psObj[25].bTwoHanded = 0;
    g_psObj[25].bUnk41 = 0;
    g_psObj[25].aItemQty[0] = 0;
    g_psEnv.nBaseIdx = 0;

    /* 9. The last case's data is real, so the guard above was not testing an empty fixture. */
    PsSeedStale();
    BasePlane_ResetDetailState(&g_psEnv);
    CheckEqI32(g_psStat[0], 6, "the same object with a valid base index does give Craft cannon its row");
    CheckEqI32(g_psRows, 2, "so cases 8's zeroes came from the guard, not from missing data");
}

/* ---- ufopaedia: the encyclopedia's paging and entry layout ----------------------- */

static UiPanel g_ufCat[PUFO_UFOPAEDIA_CATEGORIES];
static UiPanel g_ufText;
static u8 g_ufCatLines[PUFO_UFOPAEDIA_CATEGORIES][PUFO_UFOPAEDIA_CAT_WIDTH][PUFO_PANEL_LINE_BYTES];
/* All 500 line slots are backed, not just the 50 the panel was built with: the original's wrap
 * has no bound against its own panel, so a fixture that needs more than 50 lines must not turn
 * into a crash in the middle of the suite. */
static u8 g_ufTextLines[PUFO_PANEL_LINES][PUFO_PANEL_LINE_BYTES];
static ResearchTopicLive g_ufTopics[PUFO_RESEARCH_TOPIC_COUNT];
static u8 g_ufZero[PUFO_RESEARCH_TOPIC_COUNT];
static i32 g_ufEntryIdx, g_ufCategory, g_ufTopic, g_ufEntryNum;
static u8 g_ufWide, g_ufPadDirty, g_ufEntryDirty, g_ufReselect;
static PadImage *g_ufPad;
static UfopaediaEnv g_ufEnv;

/* The nine rows, transcribed from aUfopaediaCatTopics @ 0xAA830. They are written out here as
 * literals rather than read back out of the port's table, so the table itself is under test. */
static const u8 aCatWant[PUFO_UFOPAEDIA_CATEGORIES][PUFO_UFOPAEDIA_CAT_WIDTH] = {
    { 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {  97,  98,  99, 100, 101,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   1,   2,   3,   4,   5,   6,   7,   8,  14,  15,  16,  17,  18,  22,  23,  24,  25,   0,   0,   0,   0,   0 },
    {   9, 113,  10, 114,  11, 115,  12, 116,  13, 117,  21,  19,  20,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  48,  49,  50,  51,  52,  53,  54,   0,   0,   0,   0,   0 },
    {  55,  66,  56,  67,  57,  68,  58,  69,  59,  70,  60,  71,  61,  72,  62,  73,  63,  74,  64,  75,  65,  76 },
    {  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {  77,  78,  79,  80,  81,  82,  83,  84,  85,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {  86,  87,  88,  89,  90,  91,  92,  93,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 }
};
static const i32 aCatLen[PUFO_UFOPAEDIA_CATEGORIES] = { 11, 5, 17, 13, 17, 22, 11, 9, 8 };

/* The order Ufopaedia_NextEntry walks, starting at (category 0, entry 0): every slot of every
 * row in table order, 113 stops, and the 113th lands back on the start. */
static const i32 aWalkTopic[113] = {
    103, 104, 105, 106, 107, 108, 109, 110, 111, 112,
     97,  98,  99, 100, 101,
      1,   2,   3,   4,   5,   6,   7,   8,  14,  15,  16,  17,  18,  22,  23,  24,  25,
      9, 113,  10, 114,  11, 115,  12, 116,  13, 117,  21,  19,  20,
     37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  48,  49,  50,  51,  52,  53,  54,
     55,  66,  56,  67,  57,  68,  58,  69,  59,  70,  60,  71,  61,  72,  62,  73,  63,  74,  64,  75,  65,  76,
     26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,
     77,  78,  79,  80,  81,  82,  83,  84,  85,
     86,  87,  88,  89,  90,  91,  92,  93,
    102
};
static const i32 aWalkCat[113] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8,
    0
};

static void UfPutRec(i32 nRec, const char *psz)
{
    PutStr((char *)(g_fsTexts) + PUFO_TEXT_REC_STRIDE * (size_t)nRec, psz);
}

static void UfFixture(void)
{
    i32 c, i;

    if (g_ufPad != NULL) {
        PadImage_FreeData(g_ufPad);
        free(g_ufPad);
        g_ufPad = NULL;
    }
    memset(g_ufCat, 0, sizeof(g_ufCat));
    memset(&g_ufText, 0, sizeof(g_ufText));
    memset(g_ufTopics, 0, sizeof(g_ufTopics));
    memset(g_ufZero, 0, sizeof(g_ufZero));

    /* The panels are built the way the screen's opener builds them: one row of the table per
     * panel, nLineCount the row's length, anLineAux the topic number. */
    for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
        g_ufCat[c].nLineCount = aCatLen[c];
        for (i = 0; i < PUFO_UFOPAEDIA_CAT_WIDTH; ++i) {
            g_ufCat[c].apLines[i] = g_ufCatLines[c][i];
            g_ufCatLines[c][i][0] = 0;
            g_ufCat[c].anLineAux[i] = g_aUfopaediaCatTopics[c][i];
        }
    }
    for (i = 0; i < PUFO_PANEL_LINES; ++i) {
        g_ufText.apLines[i] = g_ufTextLines[i];
        g_ufTextLines[i][0] = 0;
    }
    for (i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        g_ufTopics[i].bUnlocked = 1;   /* paging is tested first with everything unlocked */
    }

    g_ufEntryIdx = 0;
    g_ufCategory = 0;
    g_ufTopic = 0;
    g_ufEntryNum = 0;
    g_ufWide = 0;
    g_ufPadDirty = 0;
    g_ufEntryDirty = 0;
    g_ufReselect = 0;

    for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
        g_ufEnv.apCatPanels[c] = &g_ufCat[c];
    }
    g_ufEnv.pTextPanel  = &g_ufText;
    g_ufEnv.pnEntryIdx  = &g_ufEntryIdx;
    g_ufEnv.pnCategory  = &g_ufCategory;
    g_ufEnv.pnTopic     = &g_ufTopic;
    g_ufEnv.pnEntryNum  = &g_ufEntryNum;
    g_ufEnv.pbWidePanel = &g_ufWide;
    g_ufEnv.pbPadDirty  = &g_ufPadDirty;
    g_ufEnv.pbEntryDirty= &g_ufEntryDirty;
    g_ufEnv.pbReselect  = &g_ufReselect;
    g_ufEnv.ppPad       = &g_ufPad;
    g_ufEnv.aTopics     = g_ufTopics;
    g_ufEnv.abResearchZero = g_ufZero;
    g_ufEnv.pTextRecs   = g_fsTexts;
}

/* Drives RefreshEntry straight from an entry number, with the reselect branch and the pad
 * branch both held off so the number under test is the one the layout sees. */
static void UfLayoutFor(i32 nEntry)
{
    g_ufPadDirty = 1;      /* skip 0x9A054's reselect, so pnEntryNum survives */
    g_ufEntryDirty = 1;    /* take the free arm, so no asset work happens */
    g_ufEntryNum = nEntry;
    g_ufTopic = 0;
    Ufopaedia_RefreshEntry(&g_ufEnv);
}

static void TestUfopaedia(void)
{
    const char *const apszArch[2] = { "lang.dat", "main.dat" };
    i32 c, i, nBad, nFirst;
    i32 nWideLines, nTightLines;

    printf("the ufopaedia's paging and entry layout\n");

    if (!Asset_OpenArchives(apszArch)) {
        printf("  FAIL lang.dat / main.dat did not open\n");
        ++g_nFailures;
        return;
    }
    if (g_fsTexts == NULL && !FsLoadTexts()) {
        printf("  FAIL lang.dat texts.dat did not load\n");
        ++g_nFailures;
        return;
    }
    FontMetrics_Load();
    Check(g_fontMetrics.aAdvance['A'] > 0 && g_fontMetrics.aAdvance[' '] > 0,
          "real glyph advances are loaded, so the wrap widths below are measured, not assumed");

    /* The shipped description text is replaced with the test's own wording: the wrap tests measure
     * the band widths, not the language file's phrasing, and no fixture can overflow the 50-line
     * panel with a real paragraph. 4 * 143 + 537 = 1109 covers the last topic's last record. */
    for (i = PUFO_UFOPAEDIA_DESC_REC_FIRST; i <= 4 * (PUFO_RESEARCH_TOPIC_COUNT - 1) + PUFO_UFOPAEDIA_DESC_REC_FIRST + 3; ++i) {
        UfPutRec(i, "");
    }

    /* 1. The category table, against the literals transcribed from 0xAA830. */
    for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
        Check(memcmp(g_aUfopaediaCatTopics[c], aCatWant[c], PUFO_UFOPAEDIA_CAT_WIDTH) == 0,
              "category row matches the shipped topic order");
    }
    Check(g_aUfopaediaCatTopics[5][PUFO_UFOPAEDIA_CAT_WIDTH - 1] != 0 &&
          g_aUfopaediaCatTopics[0][PUFO_UFOPAEDIA_CAT_WIDTH - 1] == 0,
          "row 5 is the only row that fills all 22 slots, so the ++idx == 22 test is not dead code");
    for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
        Check(aCatLen[c] != PUFO_UFOPAEDIA_CAT_WIDTH || c == 5,
              "every other row is shorter than the row width");
    }

    /* 2. The topic -> entry map, at the values that make the stride and the aliases visible. */
    CheckEqI32(g_anUfopaediaEntryNum[0], 0, "topic 0 has no ufopaedia entry");
    CheckEqI32(g_anUfopaediaEntryNum[1], 1001, "topic 1 is entry 1001");
    CheckEqI32(g_anUfopaediaEntryNum[55], 54, "topic 55 is entry 54");
    CheckEqI32(g_anUfopaediaEntryNum[66], 65, "topic 66 is entry 65, not 66 -- the row interleaves");
    CheckEqI32(g_anUfopaediaEntryNum[93], 92, "topic 93 is entry 92, the last of the sub-1000 run");
    CheckEqI32(g_anUfopaediaEntryNum[102], 93, "topic 102 restarts the numbering at 93");
    CheckEqI32(g_anUfopaediaEntryNum[112], 103, "topic 112 is entry 103");
    CheckEqI32(g_anUfopaediaEntryNum[26], 2026, "topic 26 is entry 2026");
    CheckEqI32(g_anUfopaediaEntryNum[37], 3000, "topic 37 is entry 3000");
    CheckEqI32(g_anUfopaediaEntryNum[47], 0, "topic 47 is the gap in the 3000-run");
    CheckEqI32(g_anUfopaediaEntryNum[118], 54, "the Sectoid topic reuses race entry 54");
    CheckEqI32(g_anUfopaediaEntryNum[142], 56, "the last race topic reuses entry 56");
    CheckEqI32(g_anUfopaediaEntryNum[143], 0, "topic 143, the last record, has no entry");

    /* 3. A full cycle of NextEntry, everything unlocked. */
    UfFixture();
    nBad = 0;
    nFirst = -1;
    for (i = 0; i < 113; ++i) {
        Ufopaedia_NextEntry(&g_ufEnv);
        if (g_ufTopic != aWalkTopic[i] || g_ufCategory != aWalkCat[i]) {
            ++nBad;
            if (nFirst < 0) {
                nFirst = i;
            }
        }
    }
    CheckEqI32(nBad, 0, "113 NextEntry calls walk every slot of every row in the table's order");
    if (nFirst >= 0) {
        printf("       first mismatch at step %d: got cat %d topic %d, want cat %d topic %d\n",
               nFirst, g_ufCategory, g_ufTopic, aWalkCat[nFirst], aWalkTopic[nFirst]);
    }
    CheckEqI32(g_ufCategory, 0, "the cycle closes back onto category 0");
    CheckEqI32(g_ufEntryIdx, 0, "and onto entry slot 0");
    CheckEqI32(g_ufTopic, 102, "the 113th stop is the row-0 head topic again");

    /* 4. Paging's own choice survives the refresh it calls. DropPad sets the pad-dirty byte,
     * which is exactly what holds off 0x9A054's re-read of the panel's selected row. */
    UfFixture();
    g_ufCat[0].nUnk3032 = 0;
    Ufopaedia_NextEntry(&g_ufEnv);
    CheckEqI32(g_ufEntryIdx, 1, "paging's entry index is not overwritten by the panel's stale selection");
    CheckEqI32(g_ufCat[0].nUnk3032, 0, "the panel's own selection byte is left alone");
    Check(g_ufPadDirty == 0 && g_ufEntryDirty == 1,
          "the refresh that paging triggers takes the create arm, not the free arm");
    Check(g_ufPad != NULL, "and it builds the entry's pad");

    /* 5. PrevEntry has no table test at all: from the head of row 0 it walks the index to -1 and
     * wraps to the LAST category's LAST slot, then filters by the panel's line count. */
    UfFixture();
    Ufopaedia_PrevEntry(&g_ufEnv);
    CheckEqI32(g_ufCategory, 8, "PrevEntry from (0,0) wraps to category 8");
    CheckEqI32(g_ufEntryIdx, 7, "and lands on slot 7, the last real entry of row 8");
    CheckEqI32(g_ufTopic, 93, "which is topic 93");
    UfFixture();
    g_ufCategory = 1;
    g_ufEntryIdx = 0;
    Ufopaedia_PrevEntry(&g_ufEnv);
    CheckEqI32(g_ufCategory, 0, "PrevEntry from (1,0) wraps back to category 0");
    CheckEqI32(g_ufEntryIdx, 10, "slots 21..11 of row 0 are past its 11 entries and get skipped");
    CheckEqI32(g_ufTopic, 112, "so the first accepted slot is row 0's last topic");

    /* 6. The asymmetry, made explicit: a slot the table calls empty is left by NextEntry and
     * entered by PrevEntry, because only NextEntry consults the table. */
    UfFixture();
    g_ufCat[0].nLineCount = PUFO_UFOPAEDIA_CAT_WIDTH;   /* pretend row 0 is full */
    for (i = 0; i < PUFO_UFOPAEDIA_CAT_WIDTH; ++i) {
        g_ufCat[0].anLineAux[i] = (i < PUFO_RESEARCH_TOPIC_COUNT) ? (i + 1) : 0;
    }
    g_ufCategory = 0;
    g_ufEntryIdx = 12;                                  /* a slot the table says is empty */
    Ufopaedia_PrevEntry(&g_ufEnv);
    CheckEqI32(g_ufCategory, 0, "PrevEntry stays in category 0 at a table-empty slot");
    CheckEqI32(g_ufEntryIdx, 11, "it steps to 11, which the table also calls empty");
    CheckEqI32(g_ufTopic, 12, "and accepts it, because PrevEntry filters on the panel, not the table");
    UfFixture();
    g_ufCat[0].nLineCount = PUFO_UFOPAEDIA_CAT_WIDTH;
    for (i = 0; i < PUFO_UFOPAEDIA_CAT_WIDTH; ++i) {
        g_ufCat[0].anLineAux[i] = (i < PUFO_RESEARCH_TOPIC_COUNT) ? (i + 1) : 0;
    }
    g_ufCategory = 0;
    g_ufEntryIdx = 12;
    Ufopaedia_NextEntry(&g_ufEnv);
    CheckEqI32(g_ufCategory, 1, "NextEntry leaves category 0 from the same slot");
    CheckEqI32(g_ufEntryIdx, 0, "starting the next row at its head");
    CheckEqI32(g_ufTopic, 97, "the table's zero terminator at row 0 slot 13 is what moved it");

    /* 7. The unlock filter, and which byte it really reads. */
    UfFixture();
    g_ufTopics[103].bUnlocked = 0;
    g_ufTopics[104].bUnlocked = 0;
    Ufopaedia_NextEntry(&g_ufEnv);
    CheckEqI32(g_ufTopic, 105, "locked topics are stepped over, not shown blank");
    CheckEqI32(g_ufEntryIdx, 3, "one call still lands on the next ACCEPTED slot, two past the start");
    Ufopaedia_NextEntry(&g_ufEnv);
    CheckEqI32(g_ufTopic, 106, "and the next call moves on from there, not from the locked pair");

    UfFixture();
    g_ufTopics[103].bUnlocked = 0;
    g_ufZero[103] = 1;      /* abResearchZero[103] set, abResearchZero[104] left clear */
    Ufopaedia_NextEntry(&g_ufEnv);
    CheckEqI32(g_ufTopic, 103,
               "the mirror byte at aResearchTopics + 0x1C24 is abResearchZero[topic], not [topic + 1]");
    UfFixture();
    g_ufTopics[103].bUnlocked = 0;
    g_ufZero[104] = 1;      /* the decompiler's reading would accept on this byte instead */
    Ufopaedia_NextEntry(&g_ufEnv);
    CheckEqI32(g_ufTopic, 104,
               "and the mirror is read at the topic itself: setting the NEXT topic's byte does not unlock it");

    /* 8. The layout bands. */
    UfFixture();
    UfLayoutFor(0);
    CheckEqI32(g_ufText.nStyle, 7, "entry 0 keeps the default 7-row panel");
    CheckEqI32(g_ufText.nPosY, 230, "at y 230");
    CheckEqI32(g_ufWide, 0, "and no wide flag");
    UfLayoutFor(1000);
    CheckEqI32(g_ufText.nStyle, 7, "entry 1000 is still the default band");
    CheckEqI32(g_ufWide, 0, "the cascade's `<= 1000` arm is inclusive");
    UfLayoutFor(1001);
    CheckEqI32(g_ufText.nStyle, 7, "entry 1001 keeps 7 rows and y 230");
    CheckEqI32(g_ufText.nPosY, 230, "only the flag changes");
    CheckEqI32(g_ufWide, 1, "1000 < entry < 2000 sets the wide flag");
    UfLayoutFor(2000);
    CheckEqI32(g_ufWide, 0, "entry 2000 falls back to the default -- the cascade's exact boundary");
    CheckEqI32(g_ufText.nStyle, 7, "with the default row count");
    UfLayoutFor(2001);
    CheckEqI32(g_ufText.nStyle, 13, "2000 < entry < 3000 gives 13 rows");
    CheckEqI32(g_ufText.nPosY, 50, "at y 50");
    CheckEqI32(g_ufWide, 1, "and the wide flag");
    UfLayoutFor(2999);
    CheckEqI32(g_ufText.nStyle, 13, "2999 is still the 13-row band");
    UfLayoutFor(3000);
    CheckEqI32(g_ufText.nStyle, 8, "entry 3000 is the > 2999 band, not the 13-row one");
    CheckEqI32(g_ufText.nPosY, 110, "at y 110");
    CheckEqI32(g_ufWide, 1, "and the wide flag");
    UfLayoutFor(9999);
    CheckEqI32(g_ufText.nStyle, 8, "the > 2999 band has no upper end");

    /* The flag-only bands, at each of their four boundaries. */
    UfLayoutFor(53);
    CheckEqI32(g_ufWide, 0, "entry 53 sits just below the first flag band");
    UfLayoutFor(54);
    CheckEqI32(g_ufWide, 1, "entry 54, Sectoid, opens it");
    UfLayoutFor(75);
    CheckEqI32(g_ufWide, 1, "entry 75 is the band's last");
    UfLayoutFor(76);
    CheckEqI32(g_ufWide, 1, "and 76 continues it through the second band");
    UfLayoutFor(84);
    CheckEqI32(g_ufWide, 1, "84 ends the second band");
    UfLayoutFor(85);
    CheckEqI32(g_ufWide, 1, "85 starts the third");
    UfLayoutFor(92);
    CheckEqI32(g_ufWide, 1, "92 ends the third");
    UfLayoutFor(98);
    CheckEqI32(g_ufWide, 0, "98 sits past all four bands");

    /* 93..97 also indents the text by seven lines. */
    UfLayoutFor(93);
    CheckEqI32(g_ufWide, 1, "93 sets the flag");
    CheckEqI32(g_ufText.nLineCount, PUFO_UFOPAEDIA_INDENT_LINES,
               "and reserves seven lines: nLineCount is the indent with no text yet");
    UfLayoutFor(97);
    CheckEqI32(g_ufText.nLineCount, PUFO_UFOPAEDIA_INDENT_LINES, "97 is the indent band's last entry");
    UfLayoutFor(98);
    CheckEqI32(g_ufText.nLineCount, 0, "98 gets no indent");

    /* 9. The description is TEXTS[4*topic + 534..537], concatenated. */
    UfFixture();
    UfPutRec(533, "PRE");
    UfPutRec(534, "A1");
    UfPutRec(535, "B2");
    UfPutRec(536, "C3");
    UfPutRec(537, "D4");
    UfPutRec(538, "E5");
    UfPutRec(539, "F6");
    UfPutRec(540, "G7");
    UfPutRec(541, "H8");
    g_ufPadDirty = 1;
    g_ufEntryDirty = 1;
    g_ufEntryNum = 0;
    g_ufTopic = 0;
    Ufopaedia_RefreshEntry(&g_ufEnv);
    CheckStr((const char *)g_ufTextLines[0], "A1B2C3D4", "topic 0's text is records 534,535,536,537 in order");
    Check(strstr((const char *)g_ufTextLines[0], "PRE") == NULL, "record 533 is not part of it");
    Check(strstr((const char *)g_ufTextLines[0], "E5") == NULL, "and record 538 is not either");
    CheckEqI32(g_ufText.nLineCount, 1, "one short paragraph is one line");

    g_ufTopic = 1;   /* the stride is 4 records per topic */
    Ufopaedia_RefreshEntry(&g_ufEnv);
    CheckStr((const char *)g_ufTextLines[0], "E5F6G7H8", "topic 1 shifts the window by exactly four records");

    /* 10. The panel is cleared before the wrap, and the scroll cursor is reset. */
    for (i = 0; i < PUFO_UFOPAEDIA_TEXT_LINES; ++i) {
        PutStr((char *)g_ufTextLines[i], "stale");
    }
    g_ufText.nUnk3028 = 9;
    Ufopaedia_RefreshEntry(&g_ufEnv);
    nBad = 0;
    for (i = 1; i < PUFO_UFOPAEDIA_TEXT_LINES; ++i) {
        if (g_ufTextLines[i][0] != 0) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "all 50 lines are cleared before the new text is wrapped in");
    CheckEqI32(g_ufText.nUnk3028, 0, "and the scroll cursor is reset");

    /* 11. The wrap width really is what the band selects: the same text needs more lines at 180
     * than at 220. */
    UfPutRec(534, "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi omega");
    UfPutRec(535, "");
    UfPutRec(536, "");
    UfPutRec(537, "");
    UfLayoutFor(1001);   /* 220 px */
    nWideLines = g_ufText.nLineCount;
    UfLayoutFor(2001);   /* 180 px */
    nTightLines = g_ufText.nLineCount;
    Check(nWideLines > 1, "the long description wraps onto several lines at 220 px");
    Check(nTightLines > nWideLines,
          "the 2000..3000 band's 180 px limit wraps the same text onto strictly more lines");

    /* 12. The pad toggle: paging builds it, a refresh that did not come from paging frees it. */
    UfFixture();
    g_ufEntryNum = 54;   /* < 1000, so the entry has palette and pad art */
    g_ufPadDirty = 1;
    g_ufEntryDirty = 0;
    Ufopaedia_RefreshEntry(&g_ufEnv);
    Check(g_ufPad != NULL, "the create arm allocates the pad");
    CheckEqI32(g_ufEntryDirty, 1, "and marks the entry dirty for the next call");
    CheckEqI32(g_ufPadDirty, 0, "clearing its own dirty byte");
    Ufopaedia_RefreshEntry(&g_ufEnv);
    Check(g_ufPad == NULL, "the second call finds the flag set and frees the pad instead");
    CheckEqI32(g_ufEntryDirty, 0, "leaving the pair back at its starting state");

    /* 13. The reselect branch reads the panel's own selection when neither flag is set. */
    UfFixture();
    g_ufCategory = 2;
    g_ufCat[2].nUnk3032 = 8;      /* row 2 slot 8 is topic 14 */
    g_ufPadDirty = 0;
    g_ufReselect = 0;
    Ufopaedia_RefreshEntry(&g_ufEnv);
    CheckEqI32(g_ufEntryIdx, 8, "the reselect reads nUnk3032 out of the current category panel");
    CheckEqI32(g_ufTopic, 14, "and takes the topic from that slot's aux column");
    CheckEqI32(g_ufEntryNum, 1020, "and the entry number from the topic -- 0xAA5F0[14], note the 1105 outlier nearby");
    g_ufPadDirty = 1;
    g_ufEntryIdx = 3;
    Ufopaedia_RefreshEntry(&g_ufEnv);
    CheckEqI32(g_ufEntryIdx, 3, "with the pad-dirty byte set the reselect is skipped and the index stands");
    CheckEqI32(g_ufTopic, 14, "and the entry stays the one paging chose");

    if (g_ufPad != NULL) {
        PadImage_FreeData(g_ufPad);
        free(g_ufPad);
        g_ufPad = NULL;
    }
}

/* ---- ufopaedia's builder and teardown ------------------------------------------- */

static ResearchTopicLive g_ufoTopics[PUFO_RESEARCH_TOPIC_COUNT];
static u8 g_ufoZero[PUFO_RESEARCH_TOPIC_COUNT];
static u8 g_ufoOpen;
static PadImage *g_ufoScreenPad;
static PadImage *g_ufoEntryPad;
static UiControl *g_ufoCtl[PUFO_UFOPAEDIA_CTL_COUNT];
static UfopaediaEnv g_ufoEnv;

/* The original installs Ufopaedia_CacheSelRow and Ufopaedia_RefreshEntry straight into the
 * argument-less pfnClick slot. The port's versions take an env, so the caller supplies thunks. */
static volatile i32 g_nUfoSeamFired = -1; /* distinct bodies: two empty ones fold to one address */
static void UfoSeamSel(void) { g_nUfoSeamFired = 1; }
static void UfoSeamCat(void) { g_nUfoSeamFired = 2; }

static void UfoFixture(void)
{
    i32 i;

    if (g_ufoOpen != 0) {           /* release whatever the previous case left built */
        Ufopaedia_Open(&g_ufoEnv);
    }
    memset(g_ufoTopics, 0, sizeof(g_ufoTopics));
    memset(g_ufoZero, 0, sizeof(g_ufoZero));
    memset(&g_ufoEnv, 0, sizeof(g_ufoEnv));
    g_ufoOpen = 0;
    g_ufoScreenPad = NULL;
    g_ufoEntryPad = NULL;
    for (i = 0; i < PUFO_UFOPAEDIA_CTL_COUNT; ++i) {
        g_ufoCtl[i] = NULL;
    }
    for (i = 0; i < PUFO_UFOPAEDIA_CATEGORIES; ++i) {
        g_ufoEnv.apCatPanels[i] = NULL;
    }
    g_ufoEnv.pTextPanel = NULL;
    g_ufoEnv.pSelPanel  = NULL;
    g_ufoEnv.aTopics        = g_ufoTopics;
    g_ufoEnv.abResearchZero = g_ufoZero;
    g_ufoEnv.pTextRecs      = g_fsTexts;
    g_ufoEnv.pbOpen         = &g_ufoOpen;
    g_ufoEnv.ppScreenPad    = &g_ufoScreenPad;
    g_ufoEnv.ppPad          = &g_ufoEntryPad;
    g_ufoEnv.apCtl          = g_ufoCtl;
    g_ufoEnv.pfnSelClick    = UfoSeamSel;
    g_ufoEnv.pfnCatClick    = UfoSeamCat;
    g_ufoEnv.pnEntryIdx   = &g_ufEntryIdx;
    g_ufoEnv.pnCategory   = &g_ufCategory;
    g_ufoEnv.pnTopic      = &g_ufTopic;
    g_ufoEnv.pnEntryNum   = &g_ufEntryNum;
    g_ufoEnv.pbWidePanel  = &g_ufWide;
    g_ufoEnv.pbPadDirty   = &g_ufPadDirty;
    g_ufoEnv.pbEntryDirty = &g_ufEntryDirty;
    g_ufoEnv.pbReselect   = &g_ufReselect;
}

static void UfoUnlock(i32 nTopic)
{
    g_ufoTopics[nTopic].bUnlocked = 1;
}

static i32 UfoCount(i32 nCat)
{
    return g_ufoEnv.apCatPanels[nCat] != NULL ? g_ufoEnv.apCatPanels[nCat]->nLineCount : -1;
}

static const char *UfoRow(i32 nCat, i32 nRow)
{
    UiPanel *p = g_ufoEnv.apCatPanels[nCat];
    if (p == NULL || nRow < 0 || nRow >= PUFO_PANEL_LINES || p->apLines[nRow] == NULL) {
        return "<none>";
    }
    return (const char *)p->apLines[nRow];
}

/* How many of a panel's rows carry text. The two passes must agree: the counting pass sizes the
 * panel and the filling pass writes exactly that many rows. */
static i32 UfoFilledRows(i32 nCat)
{
    UiPanel *p = g_ufoEnv.apCatPanels[nCat];
    i32 i, n = 0;
    if (p == NULL) {
        return -1;
    }
    for (i = 0; i < p->nLineCount; ++i) {
        if (p->apLines[i] != NULL && p->apLines[i][0] != 0) {
            ++n;
        }
    }
    return n;
}

static void TestUfopaediaOpen(void)
{
    const char *const apszArch[2] = { "lang.dat", "main.dat" };
    static const i32 aAllOpen[PUFO_UFOPAEDIA_CATEGORIES] = { 11, 5, 17, 13, 17, 22, 11, 9, 8 };
    i32 c, i, nBad;

    printf("the ufopaedia's builder and teardown\n");

    if (!Asset_OpenArchives(apszArch)) {
        printf("  FAIL lang.dat / main.dat did not open\n");
        ++g_nFailures;
        return;
    }
    if (g_fsTexts == NULL && !FsLoadTexts()) {
        printf("  FAIL lang.dat texts.dat did not load\n");
        ++g_nFailures;
        return;
    }

    /* 1. One function, two jobs, on a toggle. */
    UfoFixture();
    Ufopaedia_Open(&g_ufoEnv);
    CheckEqI32(g_ufoOpen, 1, "the build arm sets the toggle and leaves it set");
    Check(g_ufoEnv.pSelPanel != NULL && g_ufoEnv.pTextPanel != NULL,
          "the selector and the text panel are allocated");
    nBad = 0;
    for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
        if (g_ufoEnv.apCatPanels[c] == NULL) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "and all nine category panels");
    Ufopaedia_Open(&g_ufoEnv);
    CheckEqI32(g_ufoOpen, 0, "the second call takes the release arm and clears the toggle");
    Check(g_ufoEnv.pSelPanel == NULL && g_ufoEnv.pTextPanel == NULL,
          "the selector and the text panel are released");
    nBad = 0;
    for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
        if (g_ufoEnv.apCatPanels[c] != NULL) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "and every category panel with them");

    /* 2. Panel geometry. */
    UfoFixture();
    Ufopaedia_Open(&g_ufoEnv);
    CheckEqI32(g_ufoEnv.pSelPanel->nLineCount, PUFO_UFOPAEDIA_SEL_LINES, "the selector is built with 9 rows");
    CheckEqI32(g_ufoEnv.pSelPanel->nStyle, 15, "and shows 15 of them");
    CheckEqI32(g_ufoEnv.pSelPanel->nFont, 24, "at font 24");
    CheckEqI32(g_ufoEnv.pSelPanel->nPosX, 120, "at x 120");
    CheckEqI32(g_ufoEnv.pSelPanel->nPosY, 50, "y 50");
    CheckEqI32(g_ufoEnv.pSelPanel->nViewW, 230, "230 wide");
    CheckEqI32(g_ufoEnv.pSelPanel->nUnk7076, 1, "centred rows");
    CheckEqI32(g_ufoEnv.pSelPanel->bColorKey, 0x96, "the -106 byte");
    Check(g_ufoEnv.pSelPanel->aStyleRgb[0] == 0xB4 && g_ufoEnv.pSelPanel->aStyleRgb[1] == 0xB4 &&
          g_ufoEnv.pSelPanel->aStyleRgb[2] == 0xB4, "the row-blit key triple is 0xB4");
    Check(g_ufoEnv.pSelPanel->aStyleRgb[6] == 0xF0 && g_ufoEnv.pSelPanel->aStyleRgb[7] == 0xF0 &&
          g_ufoEnv.pSelPanel->aStyleRgb[8] == 0xF0, "the panel colour is white");
    Check(g_ufoEnv.pSelPanel->aStyleRgb[9] == 0x14 && g_ufoEnv.pSelPanel->aStyleRgb[11] == 0x14,
          "the second triple is 0x14");
    Check(g_ufoEnv.pSelPanel->aStyleRgb[3] == 0x32 && g_ufoEnv.pSelPanel->aStyleRgb[5] == 0xFA,
          "the source rect is (50, 50, 250)");
    g_nUfoSeamFired = -1;
    if (g_ufoEnv.pSelPanel->pfnClick != NULL) {
        g_ufoEnv.pSelPanel->pfnClick();
    }
    CheckEqI32(g_nUfoSeamFired, 1, "the selector gets the selector click seam");
    CheckEqI32(g_ufoEnv.pTextPanel->nLineCount, PUFO_UFOPAEDIA_TEXT_LINES_BUILT,
               "the text panel is built with 50 rows");
    CheckEqI32(g_ufoEnv.pTextPanel->nStyle, 7, "shows 7");
    CheckEqI32(g_ufoEnv.pTextPanel->nFont, 12, "at font 12");
    CheckEqI32(g_ufoEnv.pTextPanel->nPosX, 5, "at x 5");
    CheckEqI32(g_ufoEnv.pTextPanel->nPosY, 230, "y 230");
    CheckEqI32(g_ufoEnv.pTextPanel->bRowRules, 0, "the text panel is the one panel that clears bRowRules");
    CheckEqI32(g_ufoEnv.pSelPanel->bRowRules, 1, "the selector keeps the ctor's 1");
    Check(g_ufoEnv.pTextPanel->aStyleRgb[6] == 0xBE && g_ufoEnv.pTextPanel->aStyleRgb[7] == 0x78 &&
          g_ufoEnv.pTextPanel->aStyleRgb[8] == 0xF0, "and its colour is the pale blue 0xBE,0x78,0xF0");
    g_nUfoSeamFired = -1;
    if (g_ufoEnv.apCatPanels[0]->pfnClick != NULL) {
        g_ufoEnv.apCatPanels[0]->pfnClick();
    }
    CheckEqI32(g_nUfoSeamFired, 2, "the category panels get the refresh seam, not the selector's");

    /* 3. Nothing researched: every panel is built, with no rows. */
    UfoFixture();
    Ufopaedia_Open(&g_ufoEnv);
    for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
        CheckEqI32(UfoCount(c), 0, "an unresearched category still gets its panel, with zero rows");
        CheckEqI32(g_ufoEnv.apCatPanels[c]->nStyle, 0, "and a zero visible row count");
        CheckEqI32(g_ufoEnv.apCatPanels[c]->nPosY, PUFO_UFOPAEDIA_CAT_CENTRE_Y,
                   "centred at 160 when there is nothing to centre");
    }

    /* 4. Everything researched: the counts are the table's own row lengths. */
    UfoFixture();
    for (i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        UfoUnlock(i);
    }
    Ufopaedia_Open(&g_ufoEnv);
    for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
        CheckEqI32(UfoCount(c), aAllOpen[c], "fully researched, the category shows exactly its table row");
        CheckEqI32(UfoFilledRows(c), UfoCount(c), "and the filling pass writes exactly that many rows");
        CheckEqI32(g_ufoEnv.apCatPanels[c]->nStyle,
                   aAllOpen[c] > PUFO_UFOPAEDIA_CAT_VISIBLE_MAX ? PUFO_UFOPAEDIA_CAT_VISIBLE_MAX : aAllOpen[c],
                   "nStyle is the count clamped to 13");
        CheckEqI32(g_ufoEnv.apCatPanels[c]->nPosY,
                   PUFO_UFOPAEDIA_CAT_CENTRE_Y - 8 * (aAllOpen[c] > PUFO_UFOPAEDIA_CAT_VISIBLE_MAX
                                                    ? PUFO_UFOPAEDIA_CAT_VISIBLE_MAX : aAllOpen[c]),
                   "and the panel is centred on 160 minus 8 per visible row");
    }
    CheckEqI32(g_ufoEnv.apCatPanels[PUFO_UFOPAEDIA_RACE_CATEGORY]->nLineCount, 22,
               "category 5 keeps all 22 line buffers even though only 13 are visible");
    CheckEqI32(g_ufoEnv.apCatPanels[PUFO_UFOPAEDIA_RACE_CATEGORY]->nStyle, 13, "the clamp lands on nStyle, not on the panel");
    CheckEqI32(g_ufoEnv.apCatPanels[PUFO_UFOPAEDIA_RACE_CATEGORY]->nPosY, 56, "so its y is 160 - 8*13");

    /* 5. The race override: a race row shows on an encounter topic, not on its own. */
    UfoFixture();
    UfoUnlock(123);                       /* a Sectoid-family topic only */
    Ufopaedia_Open(&g_ufoEnv);
    CheckEqI32(UfoCount(PUFO_UFOPAEDIA_RACE_CATEGORY), 1, "one Sectoid encounter topic puts Sectoid on the list");
    CheckEqI32(g_ufoEnv.apCatPanels[PUFO_UFOPAEDIA_RACE_CATEGORY]->anLineAux[0], 55, "on the Sectoid row");
    CheckEqI32(g_ufoTopics[55].bUnlocked, 0, "although Sectoid's own topic is not researched");
    CheckEqI32(g_ufoZero[55], 1, "and opening the screen marks abResearchZero[55], the mirror the pager reads");
    CheckStr(UfoRow(5, 0), "Sectoid", "the row is named Sectoid");
    CheckEqI32(UfoCount(0), 0, "and nothing else appears");

    UfoFixture();
    Ufopaedia_Open(&g_ufoEnv);            /* nothing researched */
    CheckEqI32(UfoCount(PUFO_UFOPAEDIA_RACE_CATEGORY), 0, "with no encounter topic researched the race row is absent");
    CheckEqI32(g_ufoZero[55], 0, "and the mirror byte is not written");

    /* 6. Each race's own list, at the topics the original actually names. */
    UfoFixture(); UfoUnlock(138); Ufopaedia_Open(&g_ufoEnv);
    CheckStr(UfoRow(5, 0), "Sectoid", "138 is Sectoid's fallback");
    UfoFixture(); UfoUnlock(141); Ufopaedia_Open(&g_ufoEnv);
    CheckStr(UfoRow(5, 0), "Snakeman", "141 is Snakeman's fallback");
    UfoFixture(); UfoUnlock(130); Ufopaedia_Open(&g_ufoEnv);
    CheckStr(UfoRow(5, 0), "Muton", "130 is Muton's fallback");
    UfoFixture(); UfoUnlock(119); Ufopaedia_Open(&g_ufoEnv);
    CheckStr(UfoRow(5, 0), "Floater", "119 is Floater's fallback");
    UfoFixture(); UfoUnlock(57); Ufopaedia_Open(&g_ufoEnv);
    CheckStr(UfoRow(5, 0), "Ethereal", "Ethereal's fallback is its own topic 57");
    UfoFixture(); UfoUnlock(58); Ufopaedia_Open(&g_ufoEnv);
    CheckStr(UfoRow(5, 0), "Muton", "Muton's own topic 58 sits inside its chain");

    /* The two chains the original left incomplete. The 25 encounter topics 118..142 form five
     * families by residue mod 5, so 142 belongs to Ethereal and 135 / 140 to Muton -- but neither
     * is ever mentioned, so researching them shows nothing. */
    UfoFixture(); UfoUnlock(142); Ufopaedia_Open(&g_ufoEnv);
    CheckEqI32(UfoCount(PUFO_UFOPAEDIA_RACE_CATEGORY), 0, "topic 142 is Ethereal's family slot and the original never lists it");
    UfoFixture(); UfoUnlock(135); Ufopaedia_Open(&g_ufoEnv);
    CheckEqI32(UfoCount(PUFO_UFOPAEDIA_RACE_CATEGORY), 0, "topic 135 is a Muton family slot and is never listed either");
    UfoFixture(); UfoUnlock(140); Ufopaedia_Open(&g_ufoEnv);
    CheckEqI32(UfoCount(PUFO_UFOPAEDIA_RACE_CATEGORY), 0, "nor is 140");
    UfoFixture(); UfoUnlock(137); Ufopaedia_Open(&g_ufoEnv);
    CheckStr(UfoRow(5, 0), "Ethereal", "while 137, the last Ethereal topic the chain does list, works");

    /* 7. The row name is TEXTS[aResearchTopics[topic].wGate4], and the race name overwrites it. */
    UfoFixture();
    UfPutRec(1300, "HyperWave");
    g_ufoTopics[1].wGate4 = 1300;
    UfoUnlock(1);
    Ufopaedia_Open(&g_ufoEnv);
    CheckStr(UfoRow(2, 0), "HyperWave", "an ordinary row takes its name from the topic's wGate4 record");
    CheckEqI32(g_ufoEnv.apCatPanels[2]->anLineAux[0], 1, "and the aux column carries the topic number");

    UfoFixture();
    UfPutRec(1301, "SHOULD_NOT_SURVIVE");
    g_ufoTopics[55].wGate4 = 1301;
    UfoUnlock(123);
    Ufopaedia_Open(&g_ufoEnv);
    CheckStr(UfoRow(5, 0), "Sectoid", "the race row is overwritten with the literal race name");

    /* 8. Accepted rows are packed from row 0, whatever slots they came from. */
    UfoFixture();
    UfoUnlock(2); UfoUnlock(4);           /* category 2 slots 1 and 3 */
    Ufopaedia_Open(&g_ufoEnv);
    CheckEqI32(UfoCount(2), 2, "two researched topics in slots 1 and 3 give two rows");
    CheckEqI32(g_ufoEnv.apCatPanels[2]->anLineAux[0], 2, "packed into row 0");
    CheckEqI32(g_ufoEnv.apCatPanels[2]->anLineAux[1], 4, "and row 1");
    Check(g_ufoEnv.apCatPanels[2]->apLines[2] == NULL,
          "the panel is sized to the shown count, so a third row does not exist at all");

    /* 9. The selector's nine rows are labelled, from TEXTS 1646..1654 (the ARM's 0x105EE6..0x10654E).
     * The shipped lang.dat has "не исп." ("not used") in 1646..1649 and nothing at all from 1650 on, so
     * the first four rows carry a real caption and the last five copy a slot TEXTS.DAT never wrote.
     * The fixture reads the archive itself, so the expected value is the record's own text. */
    UfoFixture();
    for (i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        UfoUnlock(i);
    }
    Ufopaedia_Open(&g_ufoEnv);
    nBad = 0;
    for (i = 0; i < PUFO_UFOPAEDIA_SEL_LINES; ++i) {
        const char *pszRow = (g_ufoEnv.pSelPanel != NULL && g_ufoEnv.pSelPanel->apLines[i] != NULL)
                                 ? (const char *)g_ufoEnv.pSelPanel->apLines[i] : "<none>";
        if (strcmp(pszRow, FsRec(PUFO_UFOPAEDIA_SEL_REC_FIRST + i)) != 0) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "each selector row holds the TEXTS record the ARM names");
    Check(g_ufoEnv.pSelPanel->apLines[0][0] != 0, "the first four are real labels, not clears");
    Check(g_ufoEnv.pSelPanel->apLines[4][0] == 0, "and row 4 copies record 1650, which is unwritten");
    Check(g_ufoEnv.apCatPanels[0]->apLines[0] != NULL && g_ufoEnv.apCatPanels[0]->apLines[0][0] != 0,
          "while the category panels do carry names, so the clear is not a general 'no names' path");

    /* 10. The teardown releases the live entry pad, which the original's loop misses. */
    UfoFixture();
    Ufopaedia_Open(&g_ufoEnv);
    g_ufoEntryPad = (PadImage *)calloc(1, sizeof(PadImage));
    Check(g_ufoEntryPad != NULL, "a live entry pad is planted, the state every refresh leaves behind");
    Ufopaedia_Open(&g_ufoEnv);
    Check(g_ufoEntryPad == NULL,
          "the release frees it -- the original's loop starts at 0x1872E0 and reads slot +2, so it never reaches 0x1872E4 and leaks the pad");

    /* 11. The screen pad and the six controls. */
    UfoFixture();
    Ufopaedia_Open(&g_ufoEnv);
    g_ufoScreenPad = (PadImage *)calloc(1, sizeof(PadImage));
    for (i = 0; i < PUFO_UFOPAEDIA_CTL_COUNT; ++i) {
        g_ufoCtl[i] = (UiControl *)calloc(1, sizeof(UiControl));
    }
    Ufopaedia_Open(&g_ufoEnv);
    Check(g_ufoScreenPad == NULL, "the screen's own pad is released");
    nBad = 0;
    for (i = 0; i < PUFO_UFOPAEDIA_CTL_COUNT; ++i) {
        if (g_ufoCtl[i] != NULL) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "and all six controls at 0x1872E8..0x1872FC");

    if (g_ufoOpen != 0) {
        Ufopaedia_Open(&g_ufoEnv);
    }
}

/* ---- Ui_InitBaseInfo @ 0x9ddc8 ------------------------------------------------------------
 *
 * The bring-up is wiring plus the ufopaedia reselect, so the fixture wires the ufopaedia env to the
 * same eight-slot block the original shares: slot 0 is the screen pad this function allocates and
 * Ufopaedia_Open's teardown releases, slot 1 is the entry pad Ufopaedia_RefreshEntry allocates, and
 * slots 2..7 are the six controls. Slot 1 starts NULL rather than as a sentinel because the
 * teardown frees whatever it finds there.
 *
 * The reselect cases are pinned with literals measured off aUfopaediaCatTopics @ 0xAA830 and
 * anUfopaediaEntryNum @ 0xAA5F0, not recomputed from the port's tables. */

static UiControl g_aUbiCtl[PUFO_UFOPAEDIA_CTL_COUNT];
static UiControl g_ubiClose;
static void *g_aUbiRoot[8];
static ResearchTopicLive g_ubiTopics[PUFO_RESEARCH_TOPIC_COUNT];
static u8 g_ubiZero[PUFO_RESEARCH_TOPIC_COUNT];
static u8 g_ubiOpen, g_ubiReselect, g_ubiPadDirty, g_ubiEntryDirty, g_ubiWide, g_ubiPageOpen;
static i32 g_ubiEntryIdx, g_ubiCategory, g_ubiTopic, g_ubiEntryNum, g_ubiShown;
static UfopaediaEnv g_ubiUfo;
static UiBaseInfoInitEnv g_ubiEnv;
static const char *g_pszUbiPad0[PUFO_UFOPAEDIA_CTL_COUNT];
static const char *g_pszUbiPad1[PUFO_UFOPAEDIA_CTL_COUNT];
static const char *g_pszUbiPad2[PUFO_UFOPAEDIA_CTL_COUNT];
static i32 g_nUbiPadCalls;

/* Each seam records its own id. Empty stubs would not do: the linker folds identical functions
 * into one address, so a pointer comparison between two of them can never fail. The test calls the
 * installed handler and reads the id back, which is the wiring check that actually bites. */
static i32 g_nUbiSeamLast;

static void UbiSeamClose(void)      { g_nUbiSeamLast = 1; }
static void UbiSeamPanelPrev(void)  { g_nUbiSeamLast = 2; }
static void UbiSeamPanelNext(void)  { g_nUbiSeamLast = 3; }
static void UbiSeamEntryPrev(void)  { g_nUbiSeamLast = 4; }
static void UbiSeamEntryNext(void)  { g_nUbiSeamLast = 5; }
static void UbiSeamSelPrev(void)    { g_nUbiSeamLast = 6; }
static void UbiSeamSelNext(void)    { g_nUbiSeamLast = 7; }
static void UbiSeamSelClick(void)   { g_nUbiSeamLast = 8; }
static void UbiSeamCatClick(void)   { g_nUbiSeamLast = 9; }

/* The names UiControl_Setup hands down are captured, and the load itself is real: the eight pad
 * names in the descriptor table are then checked against the archive, not against the table. */
static bool UbiLoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser)
{
    (void)pUser;
    if (g_nUbiPadCalls < PUFO_UFOPAEDIA_CTL_COUNT) {
        g_pszUbiPad0[g_nUbiPadCalls] = psz0;
        g_pszUbiPad1[g_nUbiPadCalls] = psz1;
        g_pszUbiPad2[g_nUbiPadCalls] = psz2;
    }
    ++g_nUbiPadCalls;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2);
    return true;
}

static void UbiRelease(void)
{
    if (g_ubiOpen != 0) {
        Ufopaedia_Open(&g_ubiUfo);
    }
}

static void UbiFixture(i32 nShown, u8 bReselect)
{
    i32 i;

    UbiRelease();
    memset(g_aUbiCtl, 0, sizeof(g_aUbiCtl));
    memset(&g_ubiClose, 0, sizeof(g_ubiClose));
    memset(g_ubiTopics, 0, sizeof(g_ubiTopics));
    memset(g_ubiZero, 0, sizeof(g_ubiZero));
    memset(&g_ubiUfo, 0, sizeof(g_ubiUfo));
    memset(&g_ubiEnv, 0, sizeof(g_ubiEnv));
    memset(g_pszUbiPad0, 0, sizeof(g_pszUbiPad0));
    memset(g_pszUbiPad1, 0, sizeof(g_pszUbiPad1));
    memset(g_pszUbiPad2, 0, sizeof(g_pszUbiPad2));
    g_nUbiPadCalls = 0;

    for (i = 0; i < 8; ++i) {
        g_aUbiRoot[i] = (void *)(size_t)0x5A5A5A5Au;
    }
    g_aUbiRoot[1] = NULL;   /* the entry pad slot: the teardown frees what it finds here */

    g_ubiOpen       = 0;
    g_ubiReselect   = bReselect;
    g_ubiPadDirty   = 0x5A;
    g_ubiEntryDirty = 0x5A;
    g_ubiWide       = 0x5A;
    g_ubiPageOpen   = 0x5A;
    g_ubiEntryIdx   = 0x5A5A5A5A;
    g_ubiCategory   = 0x5A5A5A5A;
    g_ubiTopic      = 0x5A5A5A5A;
    g_ubiEntryNum   = 0x5A5A5A5A;
    g_ubiShown      = nShown;

    for (i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        g_ubiTopics[i].bUnlocked = 1;   /* every entry is shown, so every panel is sized */
    }

    g_ubiUfo.aTopics        = g_ubiTopics;
    g_ubiUfo.abResearchZero = g_ubiZero;
    g_ubiUfo.pTextRecs      = g_fsTexts;
    g_ubiUfo.pbOpen         = &g_ubiOpen;
    g_ubiUfo.ppScreenPad    = (PadImage **)g_aUbiRoot;      /* slot 0 */
    g_ubiUfo.ppPad          = (PadImage **)&g_aUbiRoot[1];  /* slot 1 */
    g_ubiUfo.apCtl          = NULL;   /* the six controls are the test's statics, not heap */
    g_ubiUfo.pnEntryIdx     = &g_ubiEntryIdx;
    g_ubiUfo.pnCategory     = &g_ubiCategory;
    g_ubiUfo.pnTopic        = &g_ubiTopic;
    g_ubiUfo.pnEntryNum     = &g_ubiEntryNum;
    g_ubiUfo.pbWidePanel    = &g_ubiWide;
    g_ubiUfo.pbPadDirty     = &g_ubiPadDirty;
    g_ubiUfo.pbEntryDirty   = &g_ubiEntryDirty;
    g_ubiUfo.pbReselect     = &g_ubiReselect;
    g_ubiUfo.pfnSelClick    = UbiSeamSelClick;
    g_ubiUfo.pfnCatClick    = UbiSeamCatClick;

    for (i = 0; i < PUFO_UFOPAEDIA_CTL_COUNT; ++i) {
        g_ubiEnv.apCtl[i] = &g_aUbiCtl[i];
    }
    g_ubiEnv.ppRoot         = g_aUbiRoot;
    g_ubiEnv.pCloseCtl      = &g_ubiClose;
    g_ubiEnv.pUfopaedia     = &g_ubiUfo;
    g_ubiEnv.pnShownTopic   = &g_ubiShown;
    g_ubiEnv.pbUfopaediaOpen = &g_ubiPageOpen;
    g_ubiEnv.pfnPanelPrev   = UbiSeamPanelPrev;
    g_ubiEnv.pfnPanelNext   = UbiSeamPanelNext;
    g_ubiEnv.pfnEntryPrev   = UbiSeamEntryPrev;
    g_ubiEnv.pfnEntryNext   = UbiSeamEntryNext;
    g_ubiEnv.pfnSelPrev     = UbiSeamSelPrev;
    g_ubiEnv.pfnSelNext     = UbiSeamSelNext;
    g_ubiEnv.pfnCloseClick  = UbiSeamClose;
    g_ubiEnv.pfnLoadPads    = UbiLoadPads;
}

static void TestUiInitBaseInfo(void)
{
    const char *const apszArch[2] = { "lang.dat", "main.dat" };
    PadImage *pPad;
    i32 i, nBad;

    printf("\nUi_InitBaseInfo @ 0x9ddc8: the base-info bring-up\n");

    if (!Asset_OpenArchives(apszArch)) {
        printf("  FAIL lang.dat / main.dat did not open\n");
        ++g_nFailures;
        return;
    }
    if (g_fsTexts == NULL && !FsLoadTexts()) {
        printf("  FAIL lang.dat texts.dat did not load\n");
        ++g_nFailures;
        return;
    }

    /* ---- 1. the wiring, with the reselect flag clear ---- */
    UbiFixture(55, 0);
    Ui_InitBaseInfo(&g_ubiEnv);

    pPad = (PadImage *)g_aUbiRoot[0];
    Check(pPad != NULL, "the screen pad is allocated into slot 0 (0x9DDF8)");
    Check(pPad != NULL && pPad->pData != NULL, "baseinfo.pad really loads out of main.dat");
    Check(pPad != NULL && pPad->nMode == 14, "loaded in mode 14, the MOV R2,#0xE at 0x9DDFC");
    /* The two origin stores are not observable in the port: BuySellPadNew zeroes the whole block,
     * which is a recorded deviation from the original's uninitialised operator new. */
    Check(pPad != NULL && pPad->nOriginX == 0 && pPad->nOriginY == 0,
          "and both origins end up zero (0x9DE0C / 0x9DE14)");
    Check(g_aUbiRoot[1] == NULL, "slot 1, the entry pad, is not touched here");

    for (i = 0; i < PUFO_UFOPAEDIA_CTL_COUNT; ++i) {
        Check(g_aUbiRoot[i + 2] == (void *)&g_aUbiCtl[i],
              "the six controls land in slots 2..7, not 1..6");
    }
    /* Each handler is CALLED and its id read back. Pointer equality alone would not catch a handler
     * installed in the wrong slot. */
    static const i32 aSeamId[6] = { 2, 3, 4, 5, 6, 7 };
    static const char *const apszSeam[6] = {
        "slot 2 runs the text-panel scroller back @ 0x99f88",
        "slot 3 runs it forward @ 0x99f78",
        "slot 4 runs the ufopaedia pager back (0x9a60c)",
        "slot 5 runs it forward (0x9a754)",
        "slot 6 runs the selector scroller back @ 0x99fb4",
        "slot 7 runs it forward @ 0x99f98"
    };
    for (i = 0; i < 6; ++i) {
        g_nUbiSeamLast = 0;
        if (g_aUbiCtl[i].pfnClick != NULL) {
            g_aUbiCtl[i].pfnClick();
        }
        CheckEqI32(g_nUbiSeamLast, aSeamId[i], apszSeam[i]);
    }

    /* The six descriptors at 0xAC548, read back as positions and as the names handed to the loader. */
    static const i32 aWantX[6] = { 142, 172, 10, 40, 200, 200 };
    static const i32 aWantY[6] = { 210, 210, 210, 210, 18, 38 };
    static const char *const apszPad0[6] = { "up", "down", "leftp", "rightp", "up", "down" };
    static const char *const apszPad1[6] = { "sup", "sdown", "cleftp", "crightp", "sup", "sdown" };
    nBad = 0;
    for (i = 0; i < 6; ++i) {
        if (g_aUbiCtl[i].x != aWantX[i] || g_aUbiCtl[i].y != aWantY[i] || g_aUbiCtl[i].nMode != 0) {
            ++nBad;
        }
        if (strcmp(g_pszUbiPad0[i], apszPad0[i]) != 0 || strcmp(g_pszUbiPad1[i], apszPad1[i]) != 0
            || g_pszUbiPad2[i][0] != 0) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "each control gets its own descriptor: position, mode and pad names");
    CheckEqI32(g_nUbiPadCalls, 6, "UiControl_Setup runs once per control (0x9DE9C..0x9DEC0)");
    Check(g_aUbiCtl[2].apPads[0] != NULL && g_aUbiCtl[2].apPads[1] != NULL,
          "leftp / cleftp are real pad names in main.dat, not guesses");

    CheckEqI32(g_ubiClose.x, 224, "the close control goes to x 224 (0x9DED0)");
    CheckEqI32(g_ubiClose.y, 1, "and y 1 (0x9DEDC)");
    g_nUbiSeamLast = 0;
    if (g_ubiClose.pfnClick != NULL) {
        g_ubiClose.pfnClick();
    }
    CheckEqI32(g_nUbiSeamLast, 1, "and its handler is the base-info close thunk (0x9DEE8)");

    CheckEqI32(g_ubiOpen, 1, "Ufopaedia_Open ran and left its toggle set (0x9DEEC)");
    Check(g_ubiUfo.pSelPanel != NULL && g_ubiUfo.pTextPanel != NULL,
          "  building the selector and the text panel");
    static const i32 aWantLines[PUFO_UFOPAEDIA_CATEGORIES] = { 11, 5, 17, 13, 17, 22, 11, 9, 8 };
    nBad = 0;
    for (i = 0; i < PUFO_UFOPAEDIA_CATEGORIES; ++i) {
        if (g_ubiUfo.apCatPanels[i] == NULL
            || g_ubiUfo.apCatPanels[i]->nLineCount != aWantLines[i]) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "and the nine category panels with every entry unlocked");

    /* The reselect half did not run: every value it owns still holds the fixture's sentinel. */
    CheckEqI32(g_ubiCategory, 0x5A5A5A5A, "with the flag clear the category is left alone");
    CheckEqI32(g_ubiEntryIdx, 0x5A5A5A5A, "and the entry index");
    CheckEqI32(g_ubiTopic, 0x5A5A5A5A, "and the topic");
    CheckEqI32(g_ubiEntryNum, 0x5A5A5A5A, "and the entry number");
    CheckEqI32(g_ubiPageOpen, 0x5A, "and the page-open byte at 0x18730C");
    CheckEqI32(g_ubiShown, 55, "and the shown topic is not rewritten");

    /* The teardown releases the pad this function allocated -- the ownership pair. */
    UbiRelease();
    Check(g_aUbiRoot[0] == NULL, "Ufopaedia_Open's teardown releases the pad from slot 0");

    /* ---- 2. the reselect ---- */
    static const i32 aShown[]  = { 121, 118, 142, 120, 119, 130, 138, 141, 122, 55, 117, 102, 0,
                                   135, 140, 47, 94, 95, 96, 143 };
    static const i32 aFold[]   = {  56,  55,  57,  58,  59,  58,  55,  56,  57,  55, 117, 102, 0,
                                    135, 140, 47,  94,  95,  96, 143 };
    static const i32 aCat[]    = {   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   3,   0,  0,
                                     9,   9,   9,   9,   9,   9,   9,   9 };
    static const i32 aIdx[]    = {   2,   0,   4,   6,   8,   6,   0,   2,   4,   0,   9,   0, 11,
                                    22,  22,  22,  22,  22,  22,  22,  22 };
    static const i32 aTopic[]  = {  56,  55,  57,  58,  59,  58,  55,  56,  57,  55, 117, 102,  0,
                                    10,  10,  10,  10,  10,  10,  10,  10 };
    static const i32 aEntry[]  = {  55,  54,  56,  57,  58,  57,  54,  55,  56,  54, 1050,  93,  0,
                                  1006, 1006, 1006, 1006, 1006, 1006, 1006, 1006 };

    for (i = 0; i < (i32)(sizeof(aShown) / sizeof(aShown[0])); ++i) {
        UbiFixture(aShown[i], 1);
        Ui_InitBaseInfo(&g_ubiEnv);
        CheckEqI32(g_ubiShown, aFold[i],
                   "the shown topic is folded onto its race topic before the walk");
        CheckEqI32(g_ubiCategory, aCat[i], "the walk lands on this category");
        CheckEqI32(g_ubiEntryIdx, aIdx[i], "and this row of it");
        CheckEqI32(g_ubiTopic, aTopic[i], "the row's topic");
        CheckEqI32(g_ubiEntryNum, aEntry[i], "and that topic's ufopaedia entry number");
        CheckEqI32(g_ubiPageOpen, 1, "0x18730C is set (0x9E07C)");
        /* 0x9E038 sets pad dirty and RefreshEntry clears it again at 0x9A5C4, so the byte's end
         * state is zero either way: what the set is for is RefreshEntry's own re-pick at 0x9A054,
         * and that is already held off by the pending flag still being set. Dropping the store is
         * therefore an equivalent change, and the values above are what prove the re-pick did not
         * run. */
        CheckEqI32(g_ubiPadDirty, 0, "pad dirty is cleared again by RefreshEntry (0x9A5C4)");
        CheckEqI32(g_ubiReselect, 0, "and the pending flag is cleared (0x9E084)");
        Check(g_aUbiRoot[1] != NULL, "RefreshEntry built the entry pad into slot 1");
        UbiRelease();
    }

    /* The two arms the jump table sends to the default: 135 and 140 keep their topic, and neither is
     * in any category row, so the walk runs off the table. */
    UbiFixture(135, 1);
    Ui_InitBaseInfo(&g_ubiEnv);
    CheckEqI32(g_ubiShown, 135, "topic 135 is NOT folded, the switch's default arm");
    UbiRelease();
    UbiFixture(140, 1);
    Ui_InitBaseInfo(&g_ubiEnv);
    CheckEqI32(g_ubiShown, 140, "nor is 140");
    UbiRelease();
    UbiFixture(117, 1);
    Ui_InitBaseInfo(&g_ubiEnv);
    CheckEqI32(g_ubiShown, 117, "and 117 falls out of the switch on the low side (ADDLS is unsigned)");
    UbiRelease();
    UbiFixture(143, 1);
    Ui_InitBaseInfo(&g_ubiEnv);
    CheckEqI32(g_ubiShown, 143, "and 143 on the high side");
    UbiRelease();

    /* A fold that lands on a race topic is the same row whichever family member came in: 118, 123,
     * 128, 133 and 138 all reach category 5 row 0. */
    {
        static const i32 aSectoid[5] = { 118, 123, 128, 133, 138 };
        nBad = 0;
        for (i = 0; i < 5; ++i) {
            UbiFixture(aSectoid[i], 1);
            Ui_InitBaseInfo(&g_ubiEnv);
            if (g_ubiCategory != 5 || g_ubiEntryIdx != 0 || g_ubiEntryNum != 54) {
                ++nBad;
            }
            UbiRelease();
        }
        CheckEqI32(nBad, 0, "the Sectoid family 118/123/128/133/138 all reach entry 54");
    }

    /* Nulls. */
    UbiFixture(55, 1);
    g_ubiEnv.pUfopaedia = NULL;
    Ui_InitBaseInfo(&g_ubiEnv);
    CheckEqI32(g_ubiShown, 55, "a null ufopaedia env skips the reselect half");
    if (g_aUbiRoot[0] != NULL) {   /* no teardown runs without the env, so release slot 0 by hand */
        PadImage_FreeData((PadImage *)g_aUbiRoot[0]);
        free(g_aUbiRoot[0]);
        g_aUbiRoot[0] = NULL;
    }
    UbiFixture(55, 1);
    g_ubiEnv.pnShownTopic = NULL;
    Ui_InitBaseInfo(&g_ubiEnv);
    CheckEqI32(g_ubiOpen, 1, "the build half still runs");
    CheckEqI32(g_ubiCategory, 0x5A5A5A5A, "but the reselect half has no topic to re-point");
    UbiRelease();
    Ui_InitBaseInfo(NULL);
    UbiFixture(55, 1);
    g_ubiEnv.ppRoot = NULL;
    Ui_InitBaseInfo(&g_ubiEnv);
    CheckEqI32(g_ubiOpen, 0, "a null root stops it before anything is built");
}

/* ------------------------------------------------- the psi screen (geoscape screen 17) */

#define PSI_NAME  0
#define PSI_STAT  1
#define PSI_DELTA 2
#define PSI_CHECK 3

static Soldier   g_aPsiSol[16];
static Soldier  *g_apPsiSol[16];
static u8        g_abPsiGrid[PUFO_GRID_SIZE];
static UiControl g_psiOkCtl, g_psiApplyCtl;
static u8        g_bPsiBuilt, g_bPsiRowSel;
static i32       g_nPsiScratch, g_nPsiActionFail, g_nPsiResidue, g_nPsiScreen;
static i8        g_nPsiBaseIdx, g_nPsiBaseCount;
static PsiScreenEnv g_psiEnv;

/* Five seams, five ids (Ok 0x72BB4, ApplyAssignments 0x72BDC, the column select, PanelPrev 0x72C34,
 * PanelNext 0x72C40). Each records, because five empty bodies are one address under /OPT:ICF and the
 * per-control claims would then prove nothing about which seam landed where. */
static i32 g_nPsiSeamId;
static void PsiSeamOk(void)    { g_nPsiSeamId = 1; }
static void PsiSeamApply(void) { g_nPsiSeamId = 2; }
static void PsiSeamSel(void)   { g_nPsiSeamId = 3; }
static void PsiSeamPrev(void)  { g_nPsiSeamId = 4; }
static void PsiSeamNext(void)  { g_nPsiSeamId = 5; }

/* The pad names UiControl_Setup hands down, captured so the descriptor table is read back from the
 * image instead of assumed. */
static const char *g_pszPsiPad0[2], *g_pszPsiPad1[2], *g_pszPsiPad2[2];
static i32 g_nPsiPadCalls;

static bool PsiSeamLoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                            const char *psz2, void *pUser)
{
    (void)pCtl;
    (void)pUser;
    if (g_nPsiPadCalls < 2) {
        g_pszPsiPad0[g_nPsiPadCalls] = psz0;
        g_pszPsiPad1[g_nPsiPadCalls] = psz1;
        g_pszPsiPad2[g_nPsiPadCalls] = psz2;
    }
    ++g_nPsiPadCalls;
    return true;
}

static void PsiFixture(i32 nBaseCount, i32 nSoldierCount)
{
    i32 i;

    if (g_bPsiBuilt != 0) {              /* release whatever the previous case left built */
        PsiScreen_BuildPanels(&g_psiEnv);
    }
    memset(g_abPsiGrid, 0, sizeof(g_abPsiGrid));
    memset(g_aPsiSol, 0, sizeof(g_aPsiSol));
    memset(&g_psiEnv, 0, sizeof(g_psiEnv));
    memset(&g_psiOkCtl, 0, sizeof(g_psiOkCtl));
    memset(&g_psiApplyCtl, 0, sizeof(g_psiApplyCtl));
    g_bPsiBuilt = 0;
    g_bPsiRowSel = 0;
    g_nPsiScratch = -1;
    g_nPsiActionFail = 0;
    g_nPsiResidue = -1;
    g_nPsiScreen = 17;
    g_nPsiBaseIdx = 0;
    g_nPsiBaseCount = (i8)nBaseCount;
    g_nPsiPadCalls = 0;
    for (i = 0; i < 2; ++i) {
        g_pszPsiPad0[i] = NULL;
        g_pszPsiPad1[i] = NULL;
        g_pszPsiPad2[i] = NULL;
    }
    for (i = 0; i < 16; ++i) {
        g_apPsiSol[i] = NULL;
    }
    g_psiEnv.pbBuilt        = &g_bPsiBuilt;
    g_psiEnv.pbRowSel       = &g_bPsiRowSel;
    g_psiEnv.pnScratchRow   = &g_nPsiScratch;
    g_psiEnv.pnBaseIdx      = &g_nPsiBaseIdx;
    g_psiEnv.pnBaseCount    = &g_nPsiBaseCount;
    g_psiEnv.pnActionFail   = &g_nPsiActionFail;
    g_psiEnv.pnResidue      = &g_nPsiResidue;
    g_psiEnv.pnGeoscapeScreen = &g_nPsiScreen;
    g_psiEnv.abFacilityGrid = g_abPsiGrid;
    g_psiEnv.apSoldiers     = g_apPsiSol;
    g_psiEnv.nSoldierCount  = nSoldierCount;
    g_psiEnv.pCtlOk         = &g_psiOkCtl;
    g_psiEnv.pCtlApply      = &g_psiApplyCtl;
    g_psiEnv.pfnOkClick     = PsiSeamOk;
    g_psiEnv.pfnApplyClick  = PsiSeamApply;
    g_psiEnv.pfnSelClick    = PsiSeamSel;
    g_psiEnv.pfnScrollPrev  = PsiSeamPrev;
    g_psiEnv.pfnScrollNext  = PsiSeamNext;
    g_psiEnv.pfnLoadPads    = PsiSeamLoadPads;
    /* The check column carries TEXTS records, not blanks: 1186 "Обучение" on an assigned row
     * (0x71C8C) and 1187 "Нет" on a free one (0x71E14), so the fixture has to hold both. */
    g_psiEnv.pTextRecs      = g_aXfText;
    PutTextRec(g_aXfText, PUFO_PSI_TEXT_ASSIGNED, "ASSIGNED");
    PutTextRec(g_aXfText, PUFO_PSI_TEXT_UNASSIGNED, "NO");
}

/* The roster: soldier i is named "S<i>" and stationed at base anKind[i]. */
static void PsiRoster(const i32 *anKind, i32 nCount)
{
    i32 i;

    for (i = 0; i < nCount; ++i) {
        g_aPsiSol[i].nKind = (u8)anKind[i];
        snprintf(g_aPsiSol[i].szName, sizeof(g_aPsiSol[i].szName), "S%d", i);
        g_apPsiSol[i] = &g_aPsiSol[i];
    }
    g_psiEnv.nSoldierCount = nCount;
}

static void PsiSetPsi(i32 i, u8 bAssigned, u8 bActive, u16 uPsi, u16 uEnergy)
{
    g_aPsiSol[i].bPsiAssigned = bAssigned;
    g_aPsiSol[i].bPsiActive   = bActive;
    g_aPsiSol[i].auStat[6]    = uPsi;
    g_aPsiSol[i].auStat[7]    = uEnergy;
}

static void PsiAddLab(i32 nBase, i32 nCol, i32 nRow)
{
    g_abPsiGrid[480 + 144 * nBase + 6 * nCol + nRow] = (u8)FAC_PSI_LABORATORY;
}

static i32 PsiNeed(i32 nBase, i32 nCol, i32 nRow)
{
    return g_abPsiGrid[444 + 144 * nBase + 6 * nCol + nRow];
}

static UiPanel *PsiPanel(i32 nCol, i32 nBase)
{
    switch (nCol) {
    case PSI_NAME:  return g_psiEnv.apNamePanel[nBase];
    case PSI_STAT:  return g_psiEnv.apStatPanel[nBase];
    case PSI_DELTA: return g_psiEnv.apDeltaPanel[nBase];
    default:        return g_psiEnv.apCheckPanel[nBase];
    }
}

static const char *PsiRow(i32 nCol, i32 nBase, i32 nRow)
{
    UiPanel *p = PsiPanel(nCol, nBase);

    if (p == NULL || nRow < 0 || nRow >= p->nLineCount || p->apLines[nRow] == NULL) {
        return "<none>";
    }
    return (const char *)p->apLines[nRow];
}

static i32 PsiAux(i32 nCol, i32 nBase, i32 nRow)
{
    UiPanel *p = PsiPanel(nCol, nBase);

    if (p == NULL || nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return -999;
    }
    return p->anLineAux[nRow];
}

static void PsiSetAux(i32 nCol, i32 nBase, i32 nRow, i32 nValue)
{
    UiPanel *p = PsiPanel(nCol, nBase);

    if (p != NULL) {
        p->anLineAux[nRow] = nValue;
    }
}

static i32 PsiMeta(i32 nCol, i32 nBase, i32 nRow, i32 nSlot)
{
    UiPanel *p = PsiPanel(nCol, nBase);

    if (p == NULL || nRow < 0 || nRow >= PUFO_PANEL_LINES || nSlot < 0 || nSlot > 5) {
        return -1;
    }
    return p->aLineMeta[6 * nRow + nSlot];
}

/* True when a row's meta is exactly the given two triples. */
static bool PsiMetaIs(i32 nCol, i32 nBase, i32 nRow, i32 a, i32 b, i32 c, i32 d, i32 e, i32 f)
{
    return PsiMeta(nCol, nBase, nRow, 0) == a && PsiMeta(nCol, nBase, nRow, 1) == b &&
           PsiMeta(nCol, nBase, nRow, 2) == c && PsiMeta(nCol, nBase, nRow, 3) == d &&
           PsiMeta(nCol, nBase, nRow, 4) == e && PsiMeta(nCol, nBase, nRow, 5) == f;
}

static void TestPsiScreen(void)
{
    static const i32 aKind6[6]  = { 0, 1, 0, 2, 0, 3 };
    static const i32 aKind12[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    i32 i, nBad;

    printf("the psi screen's panels, assignment and apply\n");

    /* 1. Init: the screen pad, the two buttons, the two scroll controls, then the build arm. */
    PsiFixture(3, 6);
    PsiRoster(aKind6, 6);
    PsiScreen_Init(&g_psiEnv);
    Check(g_psiEnv.pScreenPad != NULL, "Init allocates the screen's own pad (0x72B4C)");
    Check(g_psiEnv.pScreenPad != NULL && g_psiEnv.pScreenPad->nOriginX == 0 &&
          g_psiEnv.pScreenPad->nOriginY == 0, "and zeroes its origin (0x72B88, 0x72B90)");
    CheckEqI32(g_psiOkCtl.x, 1, "the OK control goes to x 1 (0x72B9C)");
    CheckEqI32(g_psiOkCtl.y, 300, "and y 300 (0x72BAC) @ 0x72b40");
    Check(g_psiOkCtl.pfnClick == PsiSeamOk, "with PsiScreen_OnOk installed (0x72BB4)");
    CheckEqI32(g_psiApplyCtl.x, 120, "the Apply control goes to x 120 (0x72BC4)");
    CheckEqI32(g_psiApplyCtl.y, 300, "and y 300 (0x72BD0)");
    Check(g_psiApplyCtl.pfnClick == PsiSeamApply, "with ApplyAssignments installed (0x72BDC)");
    CheckEqI32(g_nPsiPadCalls, 2, "UiControl_Setup runs once per scroll control (0x72C44)");
    Check(g_pszPsiPad0[0] != NULL && strcmp(g_pszPsiPad0[0], "up") == 0 &&
          g_pszPsiPad1[0] != NULL && strcmp(g_pszPsiPad1[0], "sup") == 0 &&
          g_pszPsiPad2[0] != NULL && g_pszPsiPad2[0][0] == 0,
          "descriptor 0 at 0xABAC8 is up/sup with no third pad");
    Check(g_pszPsiPad0[1] != NULL && strcmp(g_pszPsiPad0[1], "down") == 0 &&
          g_pszPsiPad1[1] != NULL && strcmp(g_pszPsiPad1[1], "sdown") == 0,
          "descriptor 1 at 0xABAE8 is down/sdown");
    CheckEqI32(g_psiEnv.apScrollCtl[0]->x, 10, "prev lands at 10,10");
    CheckEqI32(g_psiEnv.apScrollCtl[0]->y, 10, "the descriptor's y offset is 10, not 34");
    CheckEqI32(g_psiEnv.apScrollCtl[1]->x, 10, "next lands at 10,34");
    CheckEqI32(g_psiEnv.apScrollCtl[1]->y, 34, "");
    Check(g_psiEnv.apScrollCtl[0]->pfnClick == PsiSeamPrev, "prev carries PsiScreen_PanelPrev (0x72C34)");
    Check(g_psiEnv.apScrollCtl[1]->pfnClick == PsiSeamNext, "next carries PsiScreen_PanelNext (0x72C40)");
    CheckEqI32(g_bPsiRowSel, 1, "byte_15EE84[0] is set at 0x72C70");
    CheckEqI32(g_bPsiBuilt, 1, "and the tail call at 0x72C78 builds the panels");

    /* 2. The roster split: each base's four panels hold exactly its own soldiers. */
    CheckEqI32(PsiPanel(PSI_NAME, 0)->nLineCount, 3, "base 0 lists soldiers 0, 2 and 4");
    CheckEqI32(PsiPanel(PSI_NAME, 1)->nLineCount, 1, "base 1 lists soldier 1");
    CheckEqI32(PsiPanel(PSI_NAME, 2)->nLineCount, 1, "base 2 lists soldier 3");
    Check(PsiPanel(PSI_NAME, 3) == NULL, "base 3 is outside the base count, so it is never built");
    CheckEqI32(PsiPanel(PSI_STAT, 0)->nLineCount, 3, "the stat column is sized the same");
    CheckEqI32(PsiPanel(PSI_DELTA, 0)->nLineCount, 3, "the delta column too");
    CheckEqI32(PsiPanel(PSI_CHECK, 0)->nLineCount, 3, "and the checkbox column");

    /* 3. The shared style, and the three things only the name column gets. */
    nBad = 0;
    for (i = 0; i < 4; ++i) {
        UiPanel *p = PsiPanel(i, 0);
        if (p->nStyle != PUFO_PSI_PANEL_STYLE || p->nFont != PUFO_PSI_PANEL_FONT ||
            p->nPosY != PUFO_PSI_ROW_Y || p->nRectY != PUFO_PSI_ROW_Y ||
            p->aStyleRgb[0] != 0xB4 || p->aStyleRgb[1] != 0xB4 || p->aStyleRgb[2] != 0xB4 ||
            p->aStyleRgb[6] != 0xF0 || p->aStyleRgb[7] != 0xF0 || p->aStyleRgb[8] != 0xF0 ||
            p->aStyleRgb[9] != 0x14 || p->aStyleRgb[10] != 0x14 || p->aStyleRgb[11] != 0x14) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "all four columns share nStyle 14, nFont 16, y 70 and the two byte triples");
    CheckEqI32(PsiPanel(PSI_NAME, 0)->nPosX, 10, "the name column sits at x 10");
    CheckEqI32(PsiPanel(PSI_NAME, 0)->nRectX, 5, "with its draw origin at 5");
    CheckEqI32(PsiPanel(PSI_NAME, 0)->nViewW, 230, "and a 230-wide view");
    CheckEqI32(PsiPanel(PSI_NAME, 0)->aStyleRgb[3], 0x32, "only the name column gets SetBytes3to5");
    CheckEqI32(PsiPanel(PSI_NAME, 0)->aStyleRgb[5], 0xFA, "0x32, 0x32, 0xFA (0x718D4)");
    CheckEqI32(PsiPanel(PSI_NAME, 0)->bColorKey, 0x96, "and bColorKey 0x96 instead of the ctor's 0xFF");
    Check(PsiPanel(PSI_NAME, 0)->pfnClick == PsiSeamSel, "and it is the only column with a click handler");
    CheckEqI32(PsiPanel(PSI_STAT, 0)->aStyleRgb[3], 0, "the stat column leaves aStyleRgb[3] at the ctor's 0");
    CheckEqI32(PsiPanel(PSI_STAT, 0)->bColorKey, 0xFF, "and keeps bColorKey 0xFF");
    CheckEqI32(PsiPanel(PSI_STAT, 0)->nPosX, 154, "the stat column is at x 154, origin 175, 10 wide");
    CheckEqI32(PsiPanel(PSI_STAT, 0)->nRectX, 175, "");
    CheckEqI32(PsiPanel(PSI_STAT, 0)->nViewW, 10, "");
    CheckEqI32(PsiPanel(PSI_DELTA, 0)->nPosX, 184, "the delta column at 184, origin 209");
    CheckEqI32(PsiPanel(PSI_DELTA, 0)->nRectX, 209, "");
    CheckEqI32(PsiPanel(PSI_CHECK, 0)->nPosX, 215, "the checkbox column at 215, origin 210");
    CheckEqI32(PsiPanel(PSI_CHECK, 0)->nRectX, 210, "");
    nBad = 0;
    for (i = 0; i < 4; ++i) {
        if (PsiPanel(i, 0)->nUnk7076 != (i == PSI_NAME ? 0 : 1)) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "nUnk7076 = 1 on the three narrow columns only");

    /* 4. Rows are packed from 0 in Map.apSoldiers order and carry the soldier INDEX. */
    Check(strcmp(PsiRow(PSI_NAME, 0, 0), "S0") == 0, "row 0 is soldier 0");
    Check(strcmp(PsiRow(PSI_NAME, 0, 1), "S2") == 0, "row 1 is soldier 2, not soldier 1");
    Check(strcmp(PsiRow(PSI_NAME, 0, 2), "S4") == 0, "row 2 is soldier 4");
    CheckEqI32(PsiAux(PSI_NAME, 0, 0), 0, "the name column's aux is the apSoldiers index");
    CheckEqI32(PsiAux(PSI_NAME, 0, 1), 2, "");
    CheckEqI32(PsiAux(PSI_NAME, 0, 2), 4, "");
    Check(strcmp(PsiRow(PSI_NAME, 1, 0), "S1") == 0, "and base 1's single row is soldier 1");

    /* 5. An assigned soldier: the box row is labelled with TEXTS[1186], the aux carries the state,
     * the row is highlighted on all four columns, and bPsiActive is set by the build itself. */
    PsiFixture(3, 6);
    PsiRoster(aKind6, 6);
    PsiSetPsi(2, 1, 0, 40, 80);            /* assigned, but bPsiActive starts clear */
    PsiScreen_BuildPanels(&g_psiEnv);
    CheckEqI32(PsiAux(PSI_CHECK, 0, 1), 1, "an assigned soldier gets checkbox aux 1 (0x71CEC)");
    Check(strcmp(PsiRow(PSI_CHECK, 0, 1), "ASSIGNED") == 0,
          "while the box row itself reads TEXTS[1186] \"Обучение\", not a blank (0x71C8C)");
    CheckEqI32(PsiAux(PSI_CHECK, 0, 0), 0, "an unassigned soldier gets 0 (0x71DC4)");
    Check(strcmp(PsiRow(PSI_CHECK, 0, 0), "NO") == 0,
          "and its row reads TEXTS[1187] \"Нет\" (0x71E14)");
    CheckEqI32(g_aPsiSol[2].bPsiActive, 1, "the build sets Soldier.bPsiActive for an assigned soldier (0x71D00)");
    Check(PsiMetaIs(PSI_NAME, 0, 1, 240, 120, 240, 10, 10, 10), "the assigned row is (240,120,240)");
    Check(PsiMetaIs(PSI_STAT, 0, 1, 240, 120, 240, 10, 10, 10), "on the stat column");
    Check(PsiMetaIs(PSI_DELTA, 0, 1, 240, 120, 240, 10, 10, 10), "on the delta column");
    Check(PsiMetaIs(PSI_CHECK, 0, 1, 240, 120, 240, 10, 10, 10), "and on the checkbox column");
    Check(PsiMetaIs(PSI_NAME, 0, 0, 0, 0, 0, 0, 0, 0), "an unassigned row gets no meta at all");
    Check(PsiMetaIs(PSI_STAT, 0, 0, 0, 0, 0, 0, 0, 0), "on any of the four columns");

    /* 6. The two stat columns, and the delta aux. */
    Check(strcmp(PsiRow(PSI_STAT, 0, 1), "40\\80") == 0, "the stat column is auStat[6] backslash auStat[7]");
    Check(strcmp(PsiRow(PSI_DELTA, 0, 1), "+24") == 0, "30% of 80 is 24, so the delta reads +24 (0x71F4C)");
    CheckEqI32(PsiAux(PSI_DELTA, 0, 1), 24, "and the aux carries the same number (0x71F7C)");
    PsiFixture(3, 6);
    PsiRoster(aKind6, 6);
    PsiSetPsi(2, 1, 1, 90, 80);            /* already at or above psi energy */
    PsiScreen_BuildPanels(&g_psiEnv);
    Check(strcmp(PsiRow(PSI_DELTA, 0, 1), "+1") == 0,
          OptCite("at or above auStat[7] the delta is the literal +1", "@ 0x716d4"));
    CheckEqI32(PsiAux(PSI_DELTA, 0, 1), 1, OptCite("with aux 1 (0x71FCC)", "@ 0x716d4"));
    Check(strcmp(PsiRow(PSI_STAT, 0, 1), "90\\80") == 0, "the stat column still shows both values");
    /* The two stat columns are gated on bPsiActive alone, not on the assignment: a psi-capable
     * soldier shows their numbers even while their box is clear. Only a soldier with no psi powers
     * gets the cleared pair at 0x71FDC / 0x72008. */
    PsiFixture(3, 6);
    PsiRoster(aKind6, 6);
    PsiSetPsi(2, 0, 1, 40, 80);            /* psi-capable but not assigned */
    PsiScreen_BuildPanels(&g_psiEnv);
    Check(strcmp(PsiRow(PSI_STAT, 0, 1), "40\\80") == 0, "an unassigned psi-capable soldier still shows both stats");
    Check(strcmp(PsiRow(PSI_DELTA, 0, 1), "+24") == 0, "and still shows the training delta");
    CheckEqI32(PsiAux(PSI_DELTA, 0, 1), 24, "with the aux filled in");
    CheckEqI32(PsiAux(PSI_CHECK, 0, 1), 0, "while the checkbox stays clear");
    Check(PsiRow(PSI_STAT, 0, 0)[0] == 0, "a soldier with no psi powers gets a cleared stat row (0x72000)");
    Check(PsiRow(PSI_DELTA, 0, 0)[0] == 0, "and a cleared delta row (0x7202C)");
    CheckEqI32(PsiAux(PSI_DELTA, 0, 0), 0, "with the aux left at 0");

    /* 7. The training countdown, in all four shapes. */
    PsiFixture(3, 6);
    PsiRoster(aKind6, 6);
    PsiSetPsi(2, 1, 1, 40, 80);
    PsiSetPsi(0, 1, 1, 90, 80);
    PsiSetPsi(4, 0, 1, 40, 80);
    PsiSetPsi(1, 1, 0, 40, 80);            /* assigned, but bPsiActive starts clear */
    PsiScreen_BuildPanels(&g_psiEnv);
    /* PsiScreen_BuildPanels @ 0x716d4: 2940 bytes = 700 instructions (17 of them 2-word MOV immediates) + 18 pool
     * words, 21 calls -- the four columns (name, stat, delta, checkbox) per selected base, and the countdown it
     * writes into the soldier record is one of its own outputs. */
    CheckEqI32(g_aPsiSol[2].nPsiTrainDays, 24,
               OptCite("assigned, psi-capable, below energy: 30% of 80 days (0x72090)", "@ 0x716d4"));
    CheckEqI32(g_aPsiSol[0].nPsiTrainDays, 1,
               OptCite("assigned and already at or above energy: still one day (0x7209C)", "@ 0x716d4"));
    CheckEqI32(g_aPsiSol[4].nPsiTrainDays, 0,
               OptCite("psi-capable but unassigned: no training (0x720A4)", "@ 0x716d4"));
    CheckEqI32(g_aPsiSol[1].nPsiTrainDays, 24,
               OptCite("assigning a soldier marks them psi-capable at 0x71D00 first, so they train too",
                       "@ 0x716d4"));

    /* 8. Teardown is the same function on the same flag. Re-init first: the previous case's panels
     * are live but the pad and the two controls were already released by an earlier teardown. */
    PsiFixture(3, 6);
    PsiRoster(aKind6, 6);
    /* PsiScreen_Init @ 0x72b40: 332 bytes = 74 instructions (no two-word MOV) + 9 pool words, 8 calls. Its order is
     * the pad, the two scroll controls, then the tail call that builds the panels -- the checks below run in that
     * order on purpose. */
    PsiScreen_Init(&g_psiEnv);
    Check(g_psiEnv.pScreenPad != NULL, OptCite("Init allocates a screen pad", "@ 0x72b40"));
    Check(g_psiEnv.apScrollCtl[0] != NULL && g_psiEnv.apScrollCtl[1] != NULL,
          OptCite("and the two scroll controls, before the tail call builds", "@ 0x72b40"));
    CheckEqI32(g_bPsiBuilt, 1, OptCite("the build arm leaves the flag set", "@ 0x72b40"));
    PsiScreen_BuildPanels(&g_psiEnv);
    CheckEqI32(g_bPsiBuilt, 0, "the second call clears byte_15EEF0 (0x72134) @ 0x716d4");
    CheckEqI32(g_nPsiScreen, 0, "and puts the geoscape screen id back to 0 (0x7212C)");
    Check(g_psiEnv.pScreenPad == NULL, "the screen pad is released (0x72148)");
    nBad = 0;
    for (i = 0; i < 3; ++i) {
        if (PsiPanel(PSI_NAME, i) != NULL || PsiPanel(PSI_STAT, i) != NULL ||
            PsiPanel(PSI_DELTA, i) != NULL || PsiPanel(PSI_CHECK, i) != NULL) {
            ++nBad;
        }
    }
    CheckEqI32(nBad, 0, "and all four columns of every base");
    Check(g_psiEnv.apScrollCtl[0] == NULL && g_psiEnv.apScrollCtl[1] == NULL,
          "plus the two scroll controls at 0x15EE7C and 0x15EE80 (0x72214)");

    /* 9. SelectListed: the row comes from the name column's cursor, and the scratch mirrors it. */
    PsiFixture(3, 6);
    PsiRoster(aKind6, 6);
    PsiAddLab(0, 0, 0);
    PsiScreen_BuildPanels(&g_psiEnv);
    PsiPanel(PSI_NAME, 0)->nUnk3032 = 1;
    Check(PsiScreen_SelectListed(&g_psiEnv), "assigning the selected row succeeds when the lab has room");
    CheckEqI32(g_nPsiScratch, 1, "byte_15EE84[68] takes the row the four meta calls use (0x712E0) @ 0x71298");
    CheckEqI32(PsiAux(PSI_CHECK, 0, 1), 1, "the checkbox aux flips to 1 (0x71420)");
    Check(strcmp(PsiRow(PSI_CHECK, 0, 1), "ASSIGNED") == 0,
          "and the box row is labelled with TEXTS[1186] at the same time (0x71480)");
    Check(PsiMetaIs(PSI_NAME, 0, 1, 240, 120, 240, 10, 10, 10), "the row turns to the assigned colour");
    Check(PsiMetaIs(PSI_DELTA, 0, 1, 240, 120, 240, 10, 10, 10), "on all four columns");
    CheckEqI32(g_nPsiActionFail, 0, "no failure flag on the success path");
    Check(PsiScreen_SelectListed(&g_psiEnv), "clicking again frees the row");
    CheckEqI32(PsiAux(PSI_CHECK, 0, 1), 0, "the aux flips back to 0 (0x71578)");
    Check(strcmp(PsiRow(PSI_CHECK, 0, 1), "NO") == 0,
          "and the box row goes back to TEXTS[1187] (0x715D8)");
    Check(PsiMetaIs(PSI_NAME, 0, 1, 240, 240, 240, 10, 10, 10), "and the row takes the free colour (0x714D4)");
    Check(PsiMetaIs(PSI_CHECK, 0, 1, 240, 240, 240, 10, 10, 10), "on all four columns");

    /* 10. The capacity gate, and the exact 1 the counter compares against. */
    PsiFixture(3, 12);
    PsiRoster(aKind12, 12);
    PsiAddLab(0, 0, 0);                    /* one free cell -> capacity 10 */
    PsiScreen_BuildPanels(&g_psiEnv);
    for (i = 0; i < 10; ++i) {
        PsiSetAux(PSI_CHECK, 0, i, 1);
    }
    PsiPanel(PSI_NAME, 0)->nUnk3032 = 10;
    Check(!PsiScreen_SelectListed(&g_psiEnv), "ten assigned rows against a capacity of 10 blocks the eleventh");
    CheckEqI32(g_nPsiActionFail, 1, "dwActionFail is set (0x7133C)");
    CheckEqI32(g_nPsiResidue, 0, "and dword_1870B4[0] is cleared (0x71348)");
    CheckEqI32(PsiAux(PSI_CHECK, 0, 10), 0, "the row is left unassigned");
    Check(PsiMetaIs(PSI_NAME, 0, 10, 0, 0, 0, 0, 0, 0), "and no colour is applied to it");
    PsiFixture(3, 12);
    PsiRoster(aKind12, 12);
    PsiAddLab(0, 0, 0);
    PsiScreen_BuildPanels(&g_psiEnv);
    for (i = 0; i < 10; ++i) {
        PsiSetAux(PSI_CHECK, 0, i, 2);     /* assigned-looking, but not 1 */
    }
    PsiPanel(PSI_NAME, 0)->nUnk3032 = 10;
    /* PsiScreen_SelectListed @ 0x71298: 848 bytes = 198 instructions (2 two-word MOVs) + 12 pool words. It walks
     * the listed rows of the selected base and flips the checkbox column's aux between 0 and 1. */
    Check(PsiScreen_SelectListed(&g_psiEnv),
          OptCite("the counter compares against 1 exactly, so aux values of 2 are not counted (0x71314)",
                  "@ 0x71298"));
    CheckEqI32(PsiAux(PSI_CHECK, 0, 10), 1, OptCite("and the row is assigned", "@ 0x71298"));

    /* 11. The selected base is byte_1870F4, read SIGNED. */
    PsiFixture(3, 6);
    PsiRoster(aKind6, 6);
    PsiAddLab(1, 0, 0);
    PsiScreen_BuildPanels(&g_psiEnv);
    g_nPsiBaseIdx = 1;
    PsiPanel(PSI_NAME, 1)->nUnk3032 = 0;
    Check(PsiScreen_SelectListed(&g_psiEnv),
          OptCite("the handler works on the base byte_1870F4 selects", "@ 0x71298"));
    CheckEqI32(PsiAux(PSI_CHECK, 1, 0), 1, OptCite("base 1's row flips", "@ 0x71298"));
    CheckEqI32(PsiAux(PSI_CHECK, 0, 0), 0, OptCite("and base 0's does not", "@ 0x71298"));
    g_nPsiBaseIdx = -1;
    Check(!PsiScreen_SelectListed(&g_psiEnv), OptCite("a negative base index is refused", "@ 0x71298"));

    /* 12. ApplyAssignments writes the checkbox column back into the soldier. */
    PsiFixture(2, 3);
    PsiRoster(aKind6, 3);                  /* soldier 0 and 2 at base 0, soldier 1 at base 1 */
    PsiSetPsi(0, 0, 1, 10, 50);
    PsiSetPsi(2, 1, 1, 20, 60);
    PsiScreen_BuildPanels(&g_psiEnv);
    PsiSetAux(PSI_CHECK, 0, 0, 1);         /* row 0 -> soldier 0, now assigned */
    PsiSetAux(PSI_CHECK, 0, 1, 0);         /* row 1 -> soldier 2, now freed */
    PsiSetAux(PSI_DELTA, 0, 0, 15);        /* the delta aux is what feeds the countdown */
    PsiSetAux(PSI_DELTA, 0, 1, 7);
    PsiScreen_ApplyAssignments(&g_psiEnv);
    /* PsiScreen_ApplyAssignments @ 0x72250: 328 bytes = 75 instructions (2 two-word MOVs) + 5 pool words. It reads
     * the checkbox and delta columns back into the soldier records and ends by calling BuildPanels @ 0x72394. */
    CheckEqI32(g_aPsiSol[0].bPsiAssigned, 1,
               OptCite("an aux of 1 sets Soldier.bPsiAssigned (0x722E8)", "@ 0x72250"));
    CheckEqI32(g_aPsiSol[0].nPsiTrainDays, 15,
               OptCite("and copies the delta column's aux into nPsiTrainDays (0x7230C)", "@ 0x72250"));
    CheckEqI32(g_aPsiSol[2].bPsiAssigned, 0, OptCite("any other value clears it (0x7234C)", "@ 0x72250"));
    CheckEqI32(g_aPsiSol[2].nPsiTrainDays, 0, OptCite("and clears the countdown too (0x72330)", "@ 0x72250"));
    CheckEqI32(g_aPsiSol[1].bPsiAssigned, 0,
               OptCite("base 1's soldier is untouched by base 0's rows", "@ 0x72250"));

    /* The delta aux is read with LDRSB, so a negative value lands as 255. */
    PsiFixture(1, 1);
    PsiRoster(aKind6, 1);
    PsiScreen_BuildPanels(&g_psiEnv);
    PsiSetAux(PSI_CHECK, 0, 0, 1);
    PsiSetAux(PSI_DELTA, 0, 0, -1);
    PsiScreen_ApplyAssignments(&g_psiEnv);
    CheckEqI32(g_aPsiSol[0].nPsiTrainDays, 255, "the delta aux is sign-extended before the byte store");

    /* 13. The psi-lab needed pass takes the ROW COUNT as its budget, not the assigned count: R2 is
     * never loaded at 0x72370, so it carries the inner loop's exit value. */
    PsiFixture(1, 12);
    PsiRoster(aKind12, 12);
    PsiAddLab(0, 0, 0);
    PsiAddLab(0, 1, 0);
    PsiAddLab(0, 2, 0);
    PsiScreen_BuildPanels(&g_psiEnv);
    PsiSetAux(PSI_CHECK, 0, 0, 1);         /* one soldier assigned, twelve rows */
    PsiScreen_ApplyAssignments(&g_psiEnv);
    CheckEqI32(PsiNeed(0, 0, 0), 1,
               OptCite("twelve rows give a budget of 12/10+1 = 2, so the first cell is needed", "@ 0x72250"));
    CheckEqI32(PsiNeed(0, 1, 0), 1,
               OptCite("and the second one is too -- the assigned count of 1 would not reach it", "@ 0x72250"));
    CheckEqI32(PsiNeed(0, 2, 0), 0,
               OptCite("while the third is cleared, which pins the budget at exactly 2", "@ 0x72250"));

    /* 14. ApplyAssignments ends in BuildPanels, and the flag is still set, so that call releases. */
    CheckEqI32(g_bPsiBuilt, 0, OptCite("ApplyAssignments closes the screen through the toggle (0x72394)",
                                       "@ 0x72250"));
    Check(g_psiEnv.apNamePanel[0] == NULL, OptCite("the panels are gone afterwards", "@ 0x72250"));
    CheckEqI32(g_nPsiScreen, 0, OptCite("and the geoscape screen id is cleared with them", "@ 0x72250"));
}

/* -------------------------------------------------------------------------- *
 * PsiScreen_Draw @ 0x72398.                                                   *
 * -------------------------------------------------------------------------- */

static SoftSurf g_pdSurf;
static PsiScreenDrawEnv g_pdEnv;
static GfxColorEnv g_pdColor;
static UiAnimBlitEnv g_pdAnim;
static UiPanel g_aPdPanel[4];
static UiControl g_aPdCtl[2];
static PadImage g_aPdCtlPad[2];
static PadImage g_pdScreenPad;
static BaseSiteRec g_aPdSite[8];
static u8 g_bPdOpen, g_bPdOverlay, g_bPdAnimStarted;
static i8 g_nPdPsiBase, g_nPdWorkBase, g_nPdDrawBase, g_nPdProgress, g_nPdBaseCount;
static i32 g_nPdFail, g_nPdResidue;
static i32 g_nPdStripCalls, g_nPdStripArg, g_nPdCtlCalls;
static const PadImage *g_apPdCtlPad[2];

static void PdIconStripThunk(i32 nBaseIdx, void *pUser)
{
    (void)pUser;
    ++g_nPdStripCalls;
    g_nPdStripArg = nBaseIdx;
}

static void PdCtlBlitThunk(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    if (g_nPdCtlCalls < 2) {
        g_apPdCtlPad[g_nPdCtlCalls] = pPad;
    }
    ++g_nPdCtlCalls;
}

static void PdClear(void)
{
    memset(g_pdSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    g_nPdStripCalls = 0;
    g_nPdStripArg = 999;
    g_nPdCtlCalls = 0;
    g_bPdAnimStarted = 0;
}

static i32 PdInkAll(const u16 *pPix)
{
    i32 nInk = 0;

    for (i32 i = 0; i < 240 * 320; ++i) {
        if (pPix[i] != 0) {
            ++nInk;
        }
    }
    return nInk;
}

/* Ink of one exact colour. Gfx_PackRgb565Pair @ 0x2d82c stores the (r2,g2,b2) half of its pair at
 * unk_147B3C -- palette slot 2 entry 27 -- and the font glyphs use entry 27 for their main colour,
 * so counting that colour counts the text the draw put there. */
static i32 PdInkColour(const u16 *pPix, i32 x0, i32 y0, i32 x1, i32 y1, u16 uWant)
{
    i32 nInk = 0;

    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            if (pPix[y * 240 + x] == uWant) {
                ++nInk;
            }
        }
    }
    return nInk;
}

/* The leftmost / topmost pixel of one colour in a band: the centring probes use these, because a
 * band that only asks "is there ink here" cannot see a label slide sideways. */
static i32 PdInkLeft(const u16 *pPix, i32 x0, i32 x1, i32 y0, i32 y1, u16 uWant)
{
    for (i32 x = x0; x <= x1; ++x) {
        for (i32 y = y0; y <= y1; ++y) {
            if (pPix[y * 240 + x] == uWant) {
                return x;
            }
        }
    }
    return -1;
}

static i32 PdInkTop(const u16 *pPix, i32 x0, i32 x1, i32 y0, i32 y1, u16 uWant)
{
    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            if (pPix[y * 240 + x] == uWant) {
                return y;
            }
        }
    }
    return -1;
}

/* The four caption bands and the panel band. Every one is clear of the rails (x 5 / 234, y 5 / 315),
 * of the shortfall panel at (20, 130) and of each other, so ink inside one can only come from the
 * text that band's own source feeds. */
#define PD_BAND_COLUMNS_X0 7
#define PD_BAND_COLUMNS_Y0 54
#define PD_BAND_SEL_X0     20
#define PD_BAND_SEL_Y0     300
#define PD_BAND_OK_X0      160
#define PD_BAND_OK_Y0      300
#define PD_BAND_NAME_X0    60
#define PD_BAND_NAME_Y0    13
#define PD_BAND_ROOM_X0    60
#define PD_BAND_ROOM_Y0    158
#define PD_BAND_ROWS_X0    10
#define PD_BAND_ROWS_Y0    70
#define PD_BAND_X1         205
#define PD_BAND_YSPAN      15

/* Clears the grid, the site records and the TEXTS block, and re-arms every scalar. The panels and
 * the controls are built once by the caller and only re-pointed here. */
static void PdFixture(i32 nBaseCount)
{
    memset(&g_pdEnv, 0, sizeof(g_pdEnv));
    memset(g_aPdCtl, 0, sizeof(g_aPdCtl));
    memset(g_aPdSite, 0, sizeof(g_aPdSite));
    memset(g_abPsiGrid, 0, sizeof(g_abPsiGrid));
    memset(g_aXfText, 0, sizeof(g_aXfText));

    for (i32 i = 0; i < 2; ++i) {
        g_aPdCtl[i].bEnabled = 1;
        g_aPdCtl[i].nMode = 0;
        g_aPdCtl[i].nState = 0;
        g_aPdCtl[i].apPads[0] = &g_aPdCtlPad[i];
    }

    g_bPdOpen = 1;
    g_bPdOverlay = 0;
    g_bPdAnimStarted = 0;
    g_nPdProgress = 0;
    g_nPdBaseCount = (i8)nBaseCount;
    g_nPdPsiBase = 0;
    g_nPdWorkBase = 0;
    g_nPdDrawBase = 0;
    g_nPdFail = 0;
    g_nPdResidue = 0;

    g_pdEnv.pColor = &g_pdColor;
    g_pdEnv.pAnim = &g_pdAnim;
    g_pdEnv.pbPanelOpen = &g_bPdOpen;
    g_pdEnv.pScreenPad = NULL;
    for (i32 i = 0; i < 4; ++i) {
        g_pdEnv.apPanels[i] = &g_aPdPanel[i];
    }
    g_pdEnv.nBaseIdx = g_nPdDrawBase;
    g_pdEnv.nWorkBaseIdx = g_nPdWorkBase;
    g_pdEnv.pnPsiBaseIdx = &g_nPdPsiBase;
    g_pdEnv.pbOverlayOpen = &g_bPdOverlay;
    g_pdEnv.pCtlConfirm = &g_aPdCtl[0];
    g_pdEnv.pCtlApply = &g_aPdCtl[1];
    g_pdEnv.pfnCtlBlit = PdCtlBlitThunk;
    g_pdEnv.pfnCtlBlitEx = PdCtlBlitThunk;
    g_pdEnv.pTextRecs = (const char *)g_aXfText;
    g_pdEnv.apSiteRecs = g_aPdSite;
    g_pdEnv.pnBaseCount = &g_nPdBaseCount;
    g_pdEnv.abFacilityGrid = g_abPsiGrid;
    g_pdEnv.pnActionFail = &g_nPdFail;
    g_pdEnv.pnResidue = &g_nPdResidue;
    g_pdEnv.pfnDrawIconStrip = PdIconStripThunk;
}

/* The original keeps nWorkBaseIdx in a global and passes the drawn base as an argument; the harness
 * keeps both in step with the env the same way the geoscape caller does. */
static void PdDraw(void)
{
    g_pdEnv.nBaseIdx = g_nPdDrawBase;
    g_pdEnv.nWorkBaseIdx = g_nPdWorkBase;
    PsiScreen_Draw(&g_pdEnv);
}

static void TestPsiScreenDraw(void)
{
    const u16 *pPix;
    u16 uCapGlyph, uTailGlyph;
    u32 dwCapFirst, dwTailFirst;

    printf("PsiScreen_Draw @ 0x72398\n");

    FrameLoadFont(); /* the real font, so the captions really reach the surface */
    if (!g_bFrameFontReady) {
        printf("  FAIL lang.dat / main.dat did not open\n");
        ++g_nFailures;
        return;
    }

    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&g_pdSurf);
    SoftSurf_AllocPixels(&g_pdSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_pdSurf;
    pPix = (const u16 *)g_pdSurf.pPixels;

    g_pdColor.pnGamma = &g_nGammaZero;
    g_pdColor.nFadeCount = 0;
    g_pdAnim.pbStarted = &g_bPdAnimStarted;
    g_pdAnim.pnProgress = &g_nPdProgress;
    g_pdAnim.nFrameStyle = 0;

    /* The two colour pairs the draw packs, in the order it packs them: 0x7265C before the captions
     * and 0x72A94 in the tail. The (r2,g2,b2) half is what the glyphs are drawn with; the
     * (r1,g1,b1) half lands at unk_147B54 and says which of the two calls ran last. */
    uCapGlyph = (u16)Gfx_PackRgb565Pair(&g_pdColor, 68, 244, 252, 4, 70, 75);
    dwCapFirst = g_aPalette[2][33];
    uTailGlyph = (u16)Gfx_PackRgb565Pair(&g_pdColor, 220, 80, 80, 50, 30, 30);
    dwTailFirst = g_aPalette[2][33];
    Check(uCapGlyph != 0 && uTailGlyph != 0 && uCapGlyph != uTailGlyph,
          "both colour pairs pack to distinct non-zero glyph colours (0x2d82c)");

    for (i32 i = 0; i < 4; ++i) {
        (void)UiPanel_ctor(&g_aPdPanel[i], 3);
        g_aPdPanel[i].nPosX = 10 + 55 * i;
        g_aPdPanel[i].nPosY = 70;
        g_aPdPanel[i].nStyle = 3;
        g_aPdPanel[i].nFont = 16;
        for (i32 r = 0; r < 3; ++r) {
            (void)UiPanel_SetLineText(&g_aPdPanel[i], r, "Row");
        }
    }

    /* 1. The gate. */
    PdFixture(2);
    g_bPdOpen = 0;
    g_bPdOverlay = 1;
    g_nPdFail = 1;
    g_nPdResidue = 0;
    PdClear();
    PdDraw();
    CheckEqI32(PdInkAll(pPix), 0, "nothing draws while g_bPsiPanelOpen @ 0x15EEF0 is clear (0x723B8)");
    CheckEqI32(g_nPdStripCalls, 0, "  not even the overlay's icon strip");
    CheckEqI32(g_nPdCtlCalls, 0, "  nor the two geoscape controls");
    CheckEqI32(g_bPdAnimStarted, 0, "  nor the shortfall panel");
    CheckEqI32(g_nPdPsiBase, 0, "  and the tail never adopts a base");

    /* 2. Open, overlay closed, no shortfall: the frame, the controls, the panels, the tail colour. */
    PdFixture(2);
    PdClear();
    PdDraw();
    CheckEqI32((i32)pPix[2 * 240 + 5], XfPix(100, 100, 100), "the shared rails frame the screen (0x723C8)");
    CheckEqI32((i32)pPix[314 * 240 + 5], XfPix(115, 115, 115), "  down to the last left-rail block");
    CheckEqI32((i32)pPix[5 * 240 + 234], XfPix(135, 135, 135), "  and the top rail across to x 234");
    CheckEqI32(g_nPdCtlCalls, 2, "the confirm and apply controls both blit (0x72630 / 0x7263C)");
    Check(g_apPdCtlPad[0] == &g_aPdCtlPad[0], "the confirm control's pad first");
    Check(g_apPdCtlPad[1] == &g_aPdCtlPad[1], "then the apply control's");
    Check(PdInkAll(pPix) > 0, "the four columns draw their rows (0x72600..0x72624)");
    CheckEqI32((i32)pPix[3 * 240 + 3], 0, "the overlay bevels stay off with byte_1870F8 clear (0x72760)");
    CheckEqI32((i32)pPix[300 * 240 + 239], 0, "and the full-screen blend never runs either (0x72790)");
    CheckEqI32(g_nPdStripCalls, 0, "and the icon strip is inside the overlay (0x727D8)");
    CheckEqI32((i32)g_aPalette[2][33], (i32)dwTailFirst,
               "the tail pair (DC,50,50)/(1E,32,50) is packed at 0x72A94 with the overlay closed");
    CheckEqI32(g_bPdAnimStarted, 0, "no shortfall panel while dwActionFail is clear (0x72AA4)");

    /* 3. The four captions, one record at a time, so each band pins its own record index. */
    PdFixture(2);
    for (i32 i = 0; i < 4; ++i) {
        for (i32 r = 0; r < 3; ++r) {
            (void)UiPanel_SetLineText(&g_aPdPanel[i], r, "");
        }
    }
    PdClear();
    PdDraw();
    CheckEqI32(PdInkColour(pPix, PD_BAND_COLUMNS_X0, PD_BAND_COLUMNS_Y0, PD_BAND_X1,
                           PD_BAND_COLUMNS_Y0 + PD_BAND_YSPAN, uCapGlyph), 0,
               "an empty TEXTS[1188] leaves the column captions off (0x726A0)");
    CheckEqI32(PdInkColour(pPix, PD_BAND_SEL_X0, PD_BAND_SEL_Y0, PD_BAND_OK_X0 - 1,
                           PD_BAND_SEL_Y0 + PD_BAND_YSPAN, uCapGlyph), 0,
               "  the same for TEXTS[1189] (0x726F8)");
    CheckEqI32(PdInkColour(pPix, PD_BAND_OK_X0, PD_BAND_OK_Y0, PD_BAND_X1,
                           PD_BAND_OK_Y0 + PD_BAND_YSPAN, uCapGlyph), 0,
               "  for TEXTS[1127] (0x7274C)");
    CheckEqI32(PdInkColour(pPix, PD_BAND_NAME_X0, PD_BAND_NAME_Y0, PD_BAND_X1 - 25,
                           PD_BAND_NAME_Y0 + PD_BAND_YSPAN, uCapGlyph), 0,
               "  and for an empty base name (0x725F0)");

    PutTextRec(g_aXfText, PUFO_PSI_TEXT_COLUMNS, "Name");
    PdClear();
    PdDraw();
    Check(PdInkColour(pPix, PD_BAND_COLUMNS_X0, PD_BAND_COLUMNS_Y0, PD_BAND_X1,
                      PD_BAND_COLUMNS_Y0 + PD_BAND_YSPAN, uCapGlyph) > 0,
          "TEXTS[1188] draws at (7, 54) (0x726A0)");
    CheckEqI32(PdInkColour(pPix, PD_BAND_SEL_X0, PD_BAND_SEL_Y0, PD_BAND_X1,
                           PD_BAND_SEL_Y0 + PD_BAND_YSPAN, uCapGlyph), 0,
               "  and nothing lands in the (60, 300) band");

    memset(g_aXfText, 0, sizeof(g_aXfText));
    PutTextRec(g_aXfText, PUFO_PSI_TEXT_SEL_BASE, "Choose base");
    PdClear();
    PdDraw();
    Check(PdInkColour(pPix, PD_BAND_SEL_X0, PD_BAND_SEL_Y0, PD_BAND_OK_X0 - 1,
                      PD_BAND_SEL_Y0 + PD_BAND_YSPAN, uCapGlyph) > 0,
          "TEXTS[1189] draws centred at (60, 300) (0x726F8)");
    CheckEqI32(PdInkColour(pPix, PD_BAND_COLUMNS_X0, PD_BAND_COLUMNS_Y0, PD_BAND_X1,
                           PD_BAND_COLUMNS_Y0 + PD_BAND_YSPAN, uCapGlyph), 0,
          "  and the caption band above the rows stays empty");

    memset(g_aXfText, 0, sizeof(g_aXfText));
    PutTextRec(g_aXfText, PUFO_PSI_TEXT_OK, "OK");
    PdClear();
    PdDraw();
    Check(PdInkColour(pPix, PD_BAND_OK_X0, PD_BAND_OK_Y0, PD_BAND_X1,
                      PD_BAND_OK_Y0 + PD_BAND_YSPAN, uCapGlyph) > 0,
          "TEXTS[1127], the shared OK record, draws centred at (180, 300) (0x7274C)");
    CheckEqI32(PdInkColour(pPix, PD_BAND_SEL_X0, PD_BAND_SEL_Y0, PD_BAND_OK_X0 - 1,
                           PD_BAND_SEL_Y0 + PD_BAND_YSPAN, uCapGlyph), 0,
          "  and its own band is the only one with ink");

    memset(g_aXfText, 0, sizeof(g_aXfText));
    PutStr(g_aPdSite[0].abName, "Alpha");
    PdClear();
    PdDraw();
    Check(PdInkColour(pPix, PD_BAND_NAME_X0, PD_BAND_NAME_Y0, PD_BAND_X1 - 25,
                      PD_BAND_NAME_Y0 + PD_BAND_YSPAN, uCapGlyph) > 0,
          "the working base's name is centred at (120, 13) (0x725F0)");
    {
        const i32 nWant = 120 - Text_MeasureWidth("Alpha") / 2; /* 0x72910: ASR #1, no rounding add */
        const i32 nLeft = PdInkLeft(pPix, PD_BAND_NAME_X0, PD_BAND_X1 - 25, PD_BAND_NAME_Y0,
                                    PD_BAND_NAME_Y0 + PD_BAND_YSPAN, uCapGlyph);
        Check(nLeft >= nWant && nLeft <= nWant + 3,
              "  centred means the first glyph lands on 120 - width/2 (0x72910)");
        /* The glyph raster's own leading byte (Font_BlitGlyph @ 0xa41d4, pGlyph[0]) puts the first
         * inked row a few pixels below the draw row, so the probe is a band, not a line. */
        const i32 nTop = PdInkTop(pPix, PD_BAND_NAME_X0, PD_BAND_X1 - 25, PD_BAND_NAME_Y0,
                                  PD_BAND_NAME_Y0 + PD_BAND_YSPAN, uCapGlyph);
        Check(nTop >= 13 && nTop <= 19, "  and the title's ink starts at the draw row 13 (0x725F0)");
    }
    g_nPdWorkBase = 1; /* base 1 has no name in this fixture */
    PdClear();
    PdDraw();
    CheckEqI32(PdInkColour(pPix, PD_BAND_NAME_X0, PD_BAND_NAME_Y0, PD_BAND_X1 - 25,
                           PD_BAND_NAME_Y0 + PD_BAND_YSPAN, uCapGlyph), 0,
               "byte_158834 picks which site record the title comes from (0x725F0)");
    g_nPdWorkBase = 0;

    /* 4. The overlay. */
    PdFixture(2);
    for (i32 i = 0; i < 4; ++i) {
        for (i32 r = 0; r < 3; ++r) {
            (void)UiPanel_SetLineText(&g_aPdPanel[i], r, "Row");
        }
    }
    PutStr(g_aPdSite[0].abName, "Alpha");
    PsiAddLab(0, 0, 0);          /* base 0 has a psi lab, base 1 has none */
    g_bPdOverlay = 1;
    g_nPdWorkBase = 1;
    g_nPdPsiBase = 1;            /* equal, so the tail takes the panel branch */
    PdClear();
    PdDraw();
    CheckEqI32(g_nPdStripCalls, 1, "the facility strip runs once inside the overlay (0x727D8)");
    CheckEqI32(g_nPdStripArg, -1, "with the mark argument -1, so the drawer paints no chevron itself");
    CheckEqI32((i32)pPix[3 * 240 + 3], XfPix(0x46, 0xA0, 0xB3), "the first row's outer bevel (0x72870)");
    CheckEqI32((i32)pPix[4 * 240 + 3], XfPix(0x00, 0x6E, 0x77), "  its middle bevel (0x728A4)");
    CheckEqI32((i32)pPix[5 * 240 + 5], XfPix(0x04, 0x46, 0x4B), "  and its inner bevel (0x728D0)");
    CheckEqI32((i32)pPix[31 * 240 + 3], XfPix(0x46, 0xA0, 0xB3), "the second row starts one 28-step lower (0x729B8)");
    CheckEqI32((i32)pPix[59 * 240 + 3], 0, "and the row loop stops at nBaseCount (0x72824)");
    Check(pPix[300 * 240 + 239] != 0, "the blend covers the whole 240x320 screen (0x72790)");
    CheckEqI32((i32)pPix[59 * 240 + 210], 0, "the 32-wide erase strip reaches 28*nBaseCount + 3 (0x727CC)");
    Check(pPix[60 * 240 + 210] != 0, "  and the blend it erases is still there one row lower");
    Check(PdInkColour(pPix, PD_BAND_NAME_X0, 7, PD_BAND_X1 - 25, 21, uCapGlyph) > 0,
          "each row carries its base name at (110, 28*i + 7) (0x7291C)");
    {
        /* Differential probe. The overlay's full-screen blend leaves its own colours in these bands,
         * so the absolute ink count says nothing; the difference between the named and the unnamed
         * draw is what pins the row's y. */
        const i32 nName = PdInkColour(pPix, 90, 7, 130, 21, uCapGlyph);
        const i32 nBelow = PdInkColour(pPix, 90, 22, 130, 34, uCapGlyph);
        const i32 nWant = 110 - Text_MeasureWidth("Alpha") / 2; /* 0x72918: RSB from 0x6E */
        const i32 nLeft = PdInkLeft(pPix, 60, 160, 7, 21, uCapGlyph);
        Check(nLeft >= nWant && nLeft <= nWant + 3,
              "  and it is centred on x 110, not on the row's left edge (0x72918)");

        PutStr(g_aPdSite[0].abName, "");
        PdClear();
        PdDraw();
        Check(PdInkColour(pPix, 90, 7, 130, 21, uCapGlyph) < nName,
              "  the name ink at (110, 7) is the base name, not the blend (0x7291C)");
        CheckEqI32(PdInkColour(pPix, 90, 22, 130, 34, uCapGlyph), nBelow,
          "  and none of it spills into the gap before the next row (0x72914)");
        PutStr(g_aPdSite[0].abName, "Alpha");
    }
    CheckEqI32((i32)pPix[4 * 240 + 234], 0, "a base WITH a psi lab gets no bracket (0x72930)");
    CheckEqI32((i32)pPix[26 * 240 + 234], 0, "  neither arm of it");
    CheckEqI32((i32)pPix[32 * 240 + 212], XfPix(0xFF, 0, 0),
               "a base WITHOUT one gets the 23-step bracket, from (212, 28*base + 4) (0x72968)");
    Check(pPix[31 * 240 + 212] == 0, "  and its down arm starts there, not one pixel lower");
    CheckEqI32((i32)pPix[32 * 240 + 234], XfPix(0xFF, 0, 0), "the up arm reaches the same x 234 (0x72994)");
    CheckEqI32((i32)pPix[54 * 240 + 234], XfPix(0xFF, 0, 0), "  both arms meet at x 234");
    CheckEqI32((i32)pPix[43 * 240 + 223], XfPix(0xFF, 0, 0), "  and the down arm runs one step per iteration");
    CheckEqI32((i32)g_aPalette[2][33], (i32)dwTailFirst,
               "the equal-base compare still takes the tail panel branch (0x72A0C)");

    /* 5. The tail: adopt, refuse, or show the shortfall panel. */
    PdFixture(2);
    g_bPdOverlay = 1;
    g_nPdWorkBase = 1;
    g_nPdPsiBase = 0;
    PsiAddLab(1, 0, 0);         /* the working base has room, so it is adopted */
    PdClear();
    PdDraw();
    CheckEqI32(g_nPdPsiBase, 1, "a different base with capacity is adopted into byte_1870F4 (0x72A28)");
    CheckEqI32(g_bPdOverlay, 0, "  and byte_1870F8 clears with it (0x72A2C)");
    CheckEqI32((i32)g_aPalette[2][33], (i32)dwCapFirst,
               "  the tail colour pair is never packed on this path (0x72A1C)");
    CheckEqI32(g_bPdAnimStarted, 0, "  and no shortfall panel runs");

    PdFixture(2);
    g_bPdOverlay = 1;
    g_nPdWorkBase = 1;
    g_nPdPsiBase = 0;           /* base 1 has no psi lab in a fresh grid */
    PdClear();
    PdDraw();
    CheckEqI32(g_nPdPsiBase, 0, "a different base with no capacity is not adopted (0x72A1C)");
    CheckEqI32(g_bPdOverlay, 1, "  and the overlay stays open");
    CheckEqI32((i32)g_aPalette[2][33], (i32)dwCapFirst, "  with neither tail branch taken");

    PdFixture(2);
    PutTextRec(g_aXfText, PUFO_PSI_TEXT_NO_ROOM, "No room");
    g_nPdFail = 1;
    g_nPdResidue = 0;
    PdClear();
    PdDraw();
    CheckEqI32((i32)pPix[130 * 240 + 20], XfPix(10, 80, 90),
               "dwActionFail 1 with a zero residue opens the shortfall panel (0x72AD4)");
    CheckEqI32(g_bPdAnimStarted, 1, "  the panel animation is flagged started");
    Check(PdInkColour(pPix, PD_BAND_ROOM_X0, PD_BAND_ROOM_Y0, PD_BAND_ROOM_X0 + 120,
                      PD_BAND_ROOM_Y0 + PD_BAND_YSPAN, uTailGlyph) > 0,
          "  and TEXTS[1190] is centred at (120, 158) in the tail colour (0x72B2C)");

    PdFixture(2);
    PutTextRec(g_aXfText, PUFO_PSI_TEXT_NO_ROOM, "No room");
    g_nPdFail = 1;
    g_nPdResidue = 3;
    PdClear();
    PdDraw();
    Check(pPix[130 * 240 + 20] == 0, "a non-zero residue keeps the panel off (0x72AB4)");
    CheckEqI32(g_bPdAnimStarted, 0, "  and the animation is not started");
    CheckEqI32(PdInkColour(pPix, PD_BAND_ROOM_X0, PD_BAND_ROOM_Y0, PD_BAND_ROOM_X0 + 120,
                           PD_BAND_ROOM_Y0 + PD_BAND_YSPAN, uTailGlyph), 0,
          "  nor is its caption drawn");

    PdFixture(2);
    g_nPdFail = 2;
    g_nPdResidue = 0;
    PdClear();
    PdDraw();
    CheckEqI32(g_bPdAnimStarted, 0, "dwActionFail is compared against 1 exactly (0x72AA4)");

    /* 6. The base-count byte is read signed, and it bounds the row loop. */
    PdFixture(0);
    g_bPdOverlay = 1;
    PdClear();
    PdDraw();
    CheckEqI32(PdInkColour(pPix, 3, 3, 207, 56, XfPix(0x46, 0xA0, 0xB3)), 0,
               "nBaseCount 0 draws no base row (0x72824)");
    CheckEqI32(g_nPdStripCalls, 1, "  while the strip drawer still runs");

    PdFixture(-1);
    g_bPdOverlay = 1;
    PsiAddLab(0, 0, 0);
    PdClear();
    PdDraw();
    CheckEqI32(PdInkColour(pPix, 3, 3, 207, 56, XfPix(0x46, 0xA0, 0xB3)), 0,
               "a negative nBaseCount is read signed and draws no row either");
    CheckEqI32(PdInkColour(pPix, 212, 4, 235, 55, XfPix(0xFF, 0, 0)), 0,
               "  and its bracket is skipped with it");

    /* 7. The screen pad. Slot 19 is painted with one colour so any byte the pad carries shows up. */
    PdFixture(2);
    for (i32 i = 0; i < 4; ++i) {
        for (i32 r = 0; r < 3; ++r) {
            (void)UiPanel_SetLineText(&g_aPdPanel[i], r, "");
        }
    }
    for (i32 i = 0; i < 256; ++i) {
        g_aPalette[19][i] = 0x1234u;
    }
    PadImage_ctor(&g_pdScreenPad);
    Check(PadImage_Load(&g_pdScreenPad, PUFO_PSI_PAD_NAME, PUFO_PSI_PAD_MODE),
          "the shipped Psi pad loads (0x72C44)");
    PdClear();
    PdDraw();
    CheckEqI32(PdInkColour(pPix, 100, 200, 140, 220, 0x1234u), 0,
               "with no pad pointer the middle of the screen stays blank (0x723C0)");
    g_pdEnv.pScreenPad = &g_pdScreenPad;
    PdClear();
    PdDraw();
    Check(PdInkColour(pPix, 100, 200, 140, 220, 0x1234u) > 0,
          "the screen pad is blitted before anything else (0x723C0)");
    g_pdEnv.pScreenPad = NULL;
    PadImage_FreeData(&g_pdScreenPad);

    /* 8. Guards. The overlay's full-screen blend at 0x72790 overdraws the rails, so the rail probe
     * runs with the overlay closed and the overlay probes use the bracket colour instead. */
    PdFixture(2);
    g_bPdOverlay = 1;
    for (i32 i = 0; i < 4; ++i) {
        g_pdEnv.apPanels[i] = NULL;
    }
    g_pdEnv.pfnDrawIconStrip = NULL;
    PdClear();
    PdDraw();
    CheckEqI32(g_nPdStripCalls, 0, "a null strip hook is skipped, not called (0x727D8)");
    CheckEqI32(g_nPdCtlCalls, 2, "while the two controls still blit");
    Check(PdInkColour(pPix, 212, 4, 235, 55, XfPix(0xFF, 0, 0)) > 0,
          "  and the overlay rows are still drawn without the panel list");

    g_pdEnv.pnBaseCount = NULL;
    PdClear();
    PdDraw();
    CheckEqI32(PdInkColour(pPix, 212, 4, 235, 55, XfPix(0xFF, 0, 0)), 0,
               "a null base-count pointer is read as zero, not dereferenced");

    g_bPdOverlay = 0;
    PdClear();
    PdDraw();
    CheckEqI32((i32)pPix[2 * 240 + 5], XfPix(100, 100, 100), "a missing panel list still frames (0x72600)");
    Check(PdInkAll(pPix) > 0, "  the rails are then the only drawing left");

    PdClear();
    PsiScreen_Draw(NULL);
    CheckEqI32(PdInkAll(pPix), 0, "a null env draws nothing");
}

/* ---- BaseInfoScreen_BuildPanels @ 0x557f4 and BaseInfoScreen_Init @ 0x57688 ---- */

static UiPanel *g_apBiPanel[3];
static UiControl *g_apBiCtl[2];
static u8 g_bBiBuilt;
static u8 g_bBiMode;
static i8 g_nBiWorkBase;
static Ship g_aBiShip[4];
static Ship *g_apBiShip[4];
static Soldier g_aBiSol[3];
static Soldier *g_apBiSol[3];
static ObjectRecFull g_aBiObj[149];
static ObjectRecFull *g_apBiObj[149];
static BaseRec g_aBiBase;
static BaseRec *g_apBiBases[8];
static SmapSite g_aBiSites[2];
static u8 g_abBiGrid[PUFO_GRID_SIZE];
static u8 g_abBiGridBytes[64];
static u16 g_auBiManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT];
static u16 g_auBiResearch[PUFO_RESEARCH_TOPIC_COUNT * PUFO_FAC_BASE_COUNT];
static u8 g_bBiEntryCount;
static u8 g_bBiCraftCount;
static BaseInfoBuildEnv g_biEnv;
static PadImage *g_pBiPad;
static UiControl g_biCloseCtl;
static i32 g_nBiOk, g_nBiToggle, g_nBiClose, g_nBiBuild;
static char g_szBiPad0[32], g_szBiPad1[32], g_szBiPad2[32];

static const char *BiRow(UiPanel *pPanel, i32 nRow)
{
    const char *psz = (pPanel != NULL) ? UiPanel_GetLineText(pPanel, nRow) : NULL;

    return (psz != NULL) ? psz : "<none>";
}

static void BiRowMeta(UiPanel *pPanel, i32 nRow, i32 nWhich, i32 *pR, i32 *pG, i32 *pB)
{
    u8 r = 0, g = 0, b = 0;

    (void)UiPanel_GetLineMeta(pPanel, nRow, nWhich, &r, &g, &b);
    *pR = r; *pG = g; *pB = b;
}

static void BiFixture(void)
{
    i32 i;

    for (i = 0; i < 3; ++i) {
        g_apBiPanel[i] = NULL;
    }
    for (i = 0; i < 2; ++i) {
        g_apBiCtl[i] = (UiControl *)calloc(1, sizeof(UiControl));
        if (g_apBiCtl[i] != NULL) {
            (void)UiControl_ctor(g_apBiCtl[i]);
        }
    }
    g_bBiBuilt = 0;
    g_bBiMode = 0;
    g_nBiWorkBase = 0;
    g_bBiEntryCount = 4;
    g_bBiCraftCount = 4;

    /* id 7 / type 1 at base 0, id 9 / type 2 at base 0, id 5 / type 4 at base 1 (skipped),
     * id 3 / type 0 at base 0: the aux-0 craft the cost loop still prices at 500000. */
    memset(g_aBiShip, 0, sizeof(g_aBiShip));
    g_aBiShip[0].bType = 1; g_aBiShip[0].bId = 7; g_aBiShip[0].bCraftType = 1; g_aBiShip[0].bHomeBase = 0;
    g_aBiShip[1].bType = 2; g_aBiShip[1].bId = 9; g_aBiShip[1].bCraftType = 2; g_aBiShip[1].bHomeBase = 0;
    g_aBiShip[2].bType = 3; g_aBiShip[2].bId = 5; g_aBiShip[2].bCraftType = 4; g_aBiShip[2].bHomeBase = 1;
    g_aBiShip[3].bType = 4; g_aBiShip[3].bId = 3; g_aBiShip[3].bCraftType = 0; g_aBiShip[3].bHomeBase = 0;
    for (i = 0; i < 4; ++i) {
        g_apBiShip[i] = &g_aBiShip[i];
    }

    /* Two soldiers at base 0 and one at base 1 that is still aboard craft 9: the aboard count
     * keys on Soldier.bUnk183 alone, exactly as Craft_CountSoldiersAboard does. */
    memset(g_aBiSol, 0, sizeof(g_aBiSol));
    g_aBiSol[0].nKind = 0; g_aBiSol[0].bUnk183 = 7;
    g_aBiSol[1].nKind = 0; g_aBiSol[1].bUnk183 = 9;
    g_aBiSol[2].nKind = 1; g_aBiSol[2].bUnk183 = 9;
    for (i = 0; i < 3; ++i) {
        g_apBiSol[i] = &g_aBiSol[i];
    }

    memset(g_aBiObj, 0, sizeof(g_aBiObj));
    for (i = 0; i < 149; ++i) {
        g_apBiObj[i] = &g_aBiObj[i];
    }
    g_aBiObj[112].auPerBaseVal[0] = 5; /* engineers held at base 0 */
    g_aBiObj[113].auPerBaseVal[0] = 3; /* scientists held at base 0 */

    memset(g_auBiManuf, 0, sizeof(g_auBiManuf));
    g_auBiManuf[0] = 1; g_auBiManuf[1] = 2;      /* 3 of the 5 engineers are allocated */
    memset(g_auBiResearch, 0, sizeof(g_auBiResearch));
    g_auBiResearch[0] = 1; g_auBiResearch[1] = 1; /* 2 of the 3 scientists are allocated */

    memset(&g_aBiBase, 0, sizeof(g_aBiBase));
    g_aBiBase.bUnk4 = 7;
    for (i = 0; i < 8; ++i) {
        g_apBiBases[i] = &g_aBiBase;
    }
    memset(g_aBiSites, 0, sizeof(g_aBiSites));
    g_aBiSites[0].bFlag1 = 99; /* matches no base, so the inbound scan finds nothing */

    /* Build days live at grid + 36*nBase, the facility type bytes at grid + 480 + 144*nBase. Only
     * base 0's 36 type cells get the 0xFF "nothing here" marker, so every capacity starts at zero. */
    memset(g_abBiGrid, 0, sizeof(g_abBiGrid));
    memset(g_abBiGrid + 480, 0xFF, 36);
    memset(g_abBiGridBytes, 0, sizeof(g_abBiGridBytes));

    PutTextRec(g_aXfText, 137, "Inter");
    PutTextRec(g_aXfText, 138, "Intercep");
    PutTextRec(g_aXfText, 139, "Transport");
    PutTextRec(g_aXfText, 418, "Blimp");
    PutTextRec(g_aXfText, 419, "Ufo");

    /* BaseInfoScreen_BuildPanels names its rows from TEXTS records 1623..1645 (0x55AE0..0x56178 and
     * 0x56DD4..0x56ED4). Naming each one here makes a row/record mix-up show as the wrong string. */
    for (i = 1623; i <= 1645; ++i) {
        char szRec[8];

        snprintf(szRec, sizeof(szRec), "T%d", i);
        PutTextRec(g_aXfText, i, szRec);
    }

    memset(&g_biEnv, 0, sizeof(g_biEnv));
    g_biEnv.pbBuilt = &g_bBiBuilt;
    g_biEnv.pbModeFlag = &g_bBiMode;
    g_biEnv.pppPanel = g_apBiPanel;
    g_biEnv.papCtl = g_apBiCtl;
    g_biEnv.apCraft = g_apBiShip;
    g_biEnv.pnCraftCount = &g_bBiCraftCount;
    g_biEnv.pnWorkBaseIdx = &g_nBiWorkBase;
    g_biEnv.pTextRecs = g_aXfText;
    g_biEnv.apSoldiers = g_apBiSol;
    g_biEnv.nSoldierCount = 3;
    g_biEnv.apObjects = g_apBiObj;
    g_biEnv.nObjectCount = 149;
    g_biEnv.apManufCol = g_auBiManuf;
    g_biEnv.apResearchCol = g_auBiResearch;
    g_biEnv.pbEntryCount = &g_bBiEntryCount;
    g_biEnv.apBases = g_apBiBases;
    g_biEnv.nBaseCount = 1;
    g_biEnv.aSites = g_aBiSites;
    g_biEnv.nSiteCount = 1;
    g_biEnv.abFacilityGrid = g_abBiGrid;
    g_biEnv.abGridBytes = g_abBiGridBytes;
}

/* Runs the release arm, which is also how the fixture gives its panels and controls back. */
static void BiRelease(void)
{
    g_bBiBuilt = 1;
    BaseInfoScreen_BuildPanels(&g_biEnv);
}

static void TestBaseInfoBuildPanels(void)
{
    UiPanel *pNames, *pFree, *pTotals;
    i32 nR, nG, nB;
    i32 i;

    printf("the base info screen's panels\n");

    /* ---- the build arm, with an empty base layout ---- */
    BiFixture();
    BaseInfoScreen_BuildPanels(&g_biEnv);
    pNames = g_apBiPanel[0];
    pFree = g_apBiPanel[1];
    pTotals = g_apBiPanel[2];

    Check(g_bBiBuilt == 1, "the build arm sets the built flag");
    CheckEqI32(g_bBiMode, 1, "the build arm sets the panel-mode byte at 0xAB190");
    Check(pNames != NULL && pFree != NULL && pTotals != NULL, "all three columns are allocated");
    if (pNames == NULL || pFree == NULL || pTotals == NULL) {
        return;
    }

    CheckEqI32(pNames->nLineCount, 42, "the name column is built with 42 lines");
    CheckEqI32(pNames->nPosX, 10, "the name column sits at x 10");
    CheckEqI32(pFree->nPosX, 130, "the free column at x 130");
    CheckEqI32(pTotals->nPosX, 190, "the total column at x 190");
    CheckEqI32(pNames->nViewW, 200, "the name column is 200 wide");
    CheckEqI32(pFree->nViewW, 10, "the free column is 10 wide");
    CheckEqI32(pTotals->nViewW, 10, "the total column is 10 wide");
    CheckEqI32(pNames->nRectX, 5, "the name column's click rect starts at 5");
    CheckEqI32(pFree->nRectX, 210, "the narrow columns' starts at 210");
    CheckEqI32(pTotals->nRectX, 210, "in both of them");
    CheckEqI32(pNames->nStyle, 16, "each column shows 16 rows");
    CheckEqI32(pNames->nFont, 16, "each column draws in font 16");
    CheckEqI32(pNames->nPosY, 40, "each column starts at y 40");
    CheckEqI32(pNames->nRectY, 70, "and its click rect at y 70");
    CheckEqI32(pNames->bRowRules, 0, "the ctor's two style flags are cleared");
    CheckEqI32(pNames->bSelLineHighlight, 0, "in both");
    CheckEqI32(pNames->nUnk7076, 0, "the name column leaves nUnk7076 alone");
    CheckEqI32(pFree->nUnk7076, 1, "the free column sets it to 1");
    CheckEqI32(pTotals->nUnk7076, 1, "and so does the total column");
    Check(pNames->pfnClick == NULL, "no column gets a click handler");
    CheckEqI32(pNames->aStyleRgb[0], 0xB4, "the shared panel colours");
    CheckEqI32(pNames->aStyleRgb[2], 0xB4, "for the key triple");
    CheckEqI32(pNames->aStyleRgb[6], 0xF0, "and the two RGB triples");
    CheckEqI32(pNames->aStyleRgb[9], 0x14, "down to the last byte");

    /* The four meta rows, and a row that must not have any. */
    BiRowMeta(pNames, 0, 1, &nR, &nG, &nB);
    Check(nR == 200 && nG == 150 && nB == 150, "name row 0 gets the 200,150,-106 triple");
    BiRowMeta(pNames, 0, 2, &nR, &nG, &nB);
    Check(nR == 50 && nG == 10 && nB == 10, "and the 50,10,10 highlight triple");
    BiRowMeta(pNames, 5, 1, &nR, &nG, &nB);
    Check(nR == 200 && nG == 150 && nB == 150, "row 5 too");
    BiRowMeta(pNames, 12, 1, &nR, &nG, &nB);
    Check(nR == 200 && nG == 150 && nB == 150, "row 12 too");
    BiRowMeta(pNames, 16, 1, &nR, &nG, &nB);
    Check(nR == 200 && nG == 150 && nB == 150, "row 16 too");
    BiRowMeta(pNames, 1, 1, &nR, &nG, &nB);
    Check(nR == 0 && nG == 0 && nB == 0, "while row 1 keeps the zeroed meta");

    /* The 15 caption rows take their TEXTS record (0x55AE0..0x55E04). Rows 4 and 11 are the two copies
     * the original makes from szEmptyString, so they are the only rows in 0..16 with no label. */
    Check(0 == strcmp(BiRow(pNames, 0), "T1623"), "name row 0 is TEXTS[1623]");
    Check(0 == strcmp(BiRow(pNames, 1), "T1624"), "row 1 is TEXTS[1624]");
    Check(0 == strcmp(BiRow(pNames, 2), "T1625"), "row 2 is TEXTS[1625]");
    Check(0 == strcmp(BiRow(pNames, 3), "T1626"), "row 3 is TEXTS[1626]");
    Check(0 == strcmp(BiRow(pNames, 4), ""), "row 4 is the empty copy between the groups");
    Check(0 == strcmp(BiRow(pNames, 5), "T1627"), "row 5 is TEXTS[1627]");
    Check(0 == strcmp(BiRow(pNames, 6), "T1628"), "row 6 is TEXTS[1628]");
    Check(0 == strcmp(BiRow(pNames, 7), "T1629"), "row 7 is TEXTS[1629]");
    Check(0 == strcmp(BiRow(pNames, 8), "T1630"), "row 8 is TEXTS[1630]");
    Check(0 == strcmp(BiRow(pNames, 9), "T1631"), "row 9 is TEXTS[1631]");
    Check(0 == strcmp(BiRow(pNames, 10), "T1632"), "row 10 is TEXTS[1632], reached by the ORR literal");
    Check(0 == strcmp(BiRow(pNames, 11), ""), "row 11 is the other empty copy");
    Check(0 == strcmp(BiRow(pNames, 12), "T1633"), "row 12 is TEXTS[1633]");
    Check(0 == strcmp(BiRow(pNames, 13), "T1634"), "row 13 is TEXTS[1634]");
    Check(0 == strcmp(BiRow(pNames, 14), "T1635"), "row 14 is TEXTS[1635]");
    Check(0 == strcmp(BiRow(pNames, 15), "T1636"), "row 15 is TEXTS[1636]");
    Check(0 == strcmp(BiRow(pNames, 16), "T1637"), "row 16 is TEXTS[1637]");

    /* The craft list: three of the four craft are homed here, in apCraft order. */
    Check(0 == strcmp(BiRow(pNames, 17), "Intercep1"), "name row 17 is the first craft's display name");
    Check(0 == strcmp(BiRow(pNames, 18), "Transport2"), "row 18 the second");
    Check(0 == strcmp(BiRow(pNames, 19), "Inter4"), "row 19 the type-0 one");
    Check(0 == strcmp(BiRow(pNames, 20), ""), "row 20, past the last craft, is cleared");
    CheckEqI32(pNames->anLineAux[17], 1, "the name column's aux is Ship.bCraftType");
    CheckEqI32(pNames->anLineAux[18], 2, "for every craft row");
    CheckEqI32(pNames->anLineAux[19], 0, "including the type-0 craft");
    CheckEqI32(pFree->anLineAux[17], 7, "the free column's aux is Ship.bId");
    CheckEqI32(pFree->anLineAux[18], 9, "for every craft row");
    CheckEqI32(pFree->anLineAux[19], 3, "including the type-0 craft");
    CheckEqI32(pTotals->anLineAux[17], 1, "the total column's aux is the run marker");
    CheckEqI32(pTotals->anLineAux[19], 1, "on every craft row");
    CheckEqI32(pTotals->anLineAux[20], 0, "and stops after the last one");

    /* The tail rows move with the craft count: nShips is 3 here. */
    BiRowMeta(pNames, 21, 1, &nR, &nG, &nB);
    Check(nR == 200 && nG == 150 && nB == 150, "the tail meta lands on nShips+18");
    BiRowMeta(pNames, 26, 1, &nR, &nG, &nB);
    Check(nR == 200 && nG == 150 && nB == 150, "on nShips+23");
    BiRowMeta(pNames, 28, 1, &nR, &nG, &nB);
    Check(nR == 200 && nG == 150 && nB == 150, "and on nShips+25");
    Check(0 == strcmp(BiRow(pNames, 21), "T1638"), "nShips+18 is TEXTS[1638]");
    Check(0 == strcmp(BiRow(pNames, 22), "T1639"), "nShips+19 is TEXTS[1639]");
    Check(0 == strcmp(BiRow(pNames, 23), "T1640"), "nShips+20 is TEXTS[1640]");
    Check(0 == strcmp(BiRow(pNames, 24), "T1641"), "nShips+21 is TEXTS[1641]");
    Check(0 == strcmp(BiRow(pNames, 25), ""), "nShips+22 is the empty copy");
    Check(0 == strcmp(BiRow(pNames, 26), "T1642"), "nShips+23 is TEXTS[1642]");
    Check(0 == strcmp(BiRow(pNames, 27), ""), "nShips+24 is the other empty copy");
    Check(0 == strcmp(BiRow(pNames, 28), "T1643"), "nShips+25 is TEXTS[1643]");

    /* Crew and capacity rows of the free column. */
    Check(0 == strcmp(BiRow(pFree, 1), "3"), "soldiers aboard counts by craft id, across every base");
    Check(0 == strcmp(BiRow(pFree, 2), "3"), "free engineers is the held total minus the allocated");
    Check(0 == strcmp(BiRow(pFree, 3), "2"), "and the same for scientists");
    Check(0 == strcmp(BiRow(pFree, 6), "-10"), "living quarters: no cells, ten occupants");
    Check(0 == strcmp(BiRow(pFree, 7), "0"), "stores: nothing built, nothing stored");
    Check(0 == strcmp(BiRow(pFree, 8), "-2"), "labs: none built, two research lines active");
    Check(0 == strcmp(BiRow(pFree, 9), "-3"), "workshops: none built, three manufacturing lines");
    Check(0 == strcmp(BiRow(pFree, 10), "-3"), "hangar: no slots, three craft homed here");

    /* The craft cost rows. Aux 0 gets the 500000 label and the run continues (0x56810). */
    Check(0 == strcmp(BiRow(pFree, 17), "500000$"), "craft type 1 costs 500000");
    Check(0 == strcmp(BiRow(pFree, 18), "700000$"), "type 2 costs 700000");
    Check(0 == strcmp(BiRow(pFree, 19), "500000$"), "and aux 0 is priced at 500000 too");
    Check(0 == strcmp(BiRow(pFree, 20), "") && 0 == strcmp(BiRow(pFree, 21), ""),
          "the two rows between the craft and the prices stay empty");
    Check(0 == strcmp(BiRow(pFree, 22), "20000$"), "the free column's soldier price row");
    Check(0 == strcmp(BiRow(pFree, 23), "20000$"), "its engineer price row");
    Check(0 == strcmp(BiRow(pFree, 24), "30000$"), "and its scientist price row");

    /* The total column's money breakdown. */
    Check(0 == strcmp(BiRow(pTotals, 22), "40000$"), "two soldiers at 20000");
    Check(0 == strcmp(BiRow(pTotals, 23), "100000$"), "five engineers at 20000");
    Check(0 == strcmp(BiRow(pTotals, 24), "90000$"), "three scientists at 30000");

    /* Facility total and the grand total: 140000 + 90000 + 1700000 craft. */
    Check(0 == strcmp(BiRow(pFree, 26), "0$"), "no facility cell is built, so the facility total is zero");
    Check(0 == strcmp(BiRow(pFree, 28), "1930000$"), "the grand total is crew plus craft");

    /* The total column's count rows. */
    Check(0 == strcmp(BiRow(pTotals, 1), "2"), "soldiers assigned to this base");
    Check(0 == strcmp(BiRow(pTotals, 2), "5"), "engineers held here");
    Check(0 == strcmp(BiRow(pTotals, 3), "3"), "scientists held here");
    Check(0 == strcmp(BiRow(pTotals, 6), "0"), "living quarters capacity");
    Check(0 == strcmp(BiRow(pTotals, 7), "0"), "stores capacity");
    Check(0 == strcmp(BiRow(pTotals, 8), "0"), "lab capacity");
    Check(0 == strcmp(BiRow(pTotals, 9), "0"), "workshop capacity");
    Check(0 == strcmp(BiRow(pTotals, 10), "0"), "hangar capacity");
    Check(0 == strcmp(BiRow(pTotals, 13), "0"), "the built-facility score");
    Check(0 == strcmp(BiRow(pTotals, 14), "T1644") && 0 == strcmp(BiRow(pTotals, 15), "T1644"),
          "the two radar rows start as TEXTS[1644] when the base has no radar");

    BiRelease();
    Check(g_apBiPanel[0] == NULL && g_apBiPanel[1] == NULL && g_apBiPanel[2] == NULL,
          "the release arm clears all three panel slots");
    Check(g_apBiCtl[0] == NULL && g_apBiCtl[1] == NULL, "and both control slots");
    Check(g_bBiBuilt == 0, "and clears the built flag");

    /* ---- a base with three living-quarters cells ---- */
    BiFixture();
    /* Four cells, one of them in the far corner of the 6x6 block, so the walk's extent matters. */
    g_abBiGrid[480 + 144 * 0 + 6 * 0 + 0] = 1;
    g_abBiGrid[480 + 144 * 0 + 6 * 0 + 1] = 1;
    g_abBiGrid[480 + 144 * 0 + 6 * 1 + 2] = 1;
    g_abBiGrid[480 + 144 * 0 + 6 * 5 + 5] = 1;
    BaseInfoScreen_BuildPanels(&g_biEnv);
    pNames = g_apBiPanel[0];
    pFree = g_apBiPanel[1];
    pTotals = g_apBiPanel[2];
    Check(0 == strcmp(BiRow(pFree, 26), "200000$"),
          "four built cells cost 50000 each, whatever kind they are");
    Check(0 == strcmp(BiRow(pFree, 28), "2130000$"), "and the grand total follows them");
    Check(0 == strcmp(BiRow(pTotals, 6), "200"), "the same four cells give 200 living places");
    Check(0 == strcmp(BiRow(pFree, 6), "190"), "ten occupants leave 190 free");
    Check(0 == strcmp(BiRow(pTotals, 13), "0"), "the built-facility score only counts kinds 13..16");
    Check(0 == strcmp(BiRow(pTotals, 14), "T1644") && 0 == strcmp(BiRow(pTotals, 15), "T1644"),
          "kind 1 cells do not flip either radar row");
    BiRelease();

    /* ---- a base with both radars ---- */
    BiFixture();
    g_abBiGrid[480 + 144 * 0 + 6 * 2 + 3] = 4; /* FAC_SHORT_RANGE_RADAR */
    g_abBiGrid[480 + 144 * 0 + 6 * 4 + 1] = 5; /* FAC_LONG_RANGE_RADAR */
    BaseInfoScreen_BuildPanels(&g_biEnv);
    pTotals = g_apBiPanel[2];
    Check(0 == strcmp(BiRow(pTotals, 14), "T1645"),
          "a kind-4 cell answers the short-range row with TEXTS[1645]");
    Check(0 == strcmp(BiRow(pTotals, 15), "T1645"), "and a kind-5 cell the long-range row");
    BiRelease();

    /* ---- only the long-range radar ---- */
    BiFixture();
    g_abBiGrid[480 + 144 * 0 + 6 * 0 + 0] = 5;
    BaseInfoScreen_BuildPanels(&g_biEnv);
    pTotals = g_apBiPanel[2];
    Check(0 == strcmp(BiRow(pTotals, 14), "T1644"), "the short-range row stays TEXTS[1644]");
    Check(0 == strcmp(BiRow(pTotals, 15), "T1645"), "the long-range row takes TEXTS[1645]");
    BiRelease();

    /* ---- a site of this base's type is inbound with craft ---- */
    BiFixture();
    g_aBiSites[0].bFlag1 = 7;   /* the same type as base 0's bUnk4 */
    g_aBiSites[0].bRecoveredCraftCount = 2;
    g_aBiSites[0].bUnk518 = 1;
    BaseInfoScreen_BuildPanels(&g_biEnv);
    pFree = g_apBiPanel[1];
    Check(0 == strcmp(BiRow(pFree, 10), "-6"), "the hangar row also subtracts the inbound craft");
    BiRelease();

    /* ---- no craft homed here: every tail row shifts down by the craft count ---- */
    BiFixture();
    for (i = 0; i < 4; ++i) {
        g_aBiShip[i].bHomeBase = 1;
    }
    BaseInfoScreen_BuildPanels(&g_biEnv);
    pNames = g_apBiPanel[0];
    pFree = g_apBiPanel[1];
    pTotals = g_apBiPanel[2];
    Check(0 == strcmp(BiRow(pNames, 17), ""), "no craft, so row 17 stays empty");
    CheckEqI32(pTotals->anLineAux[17], 0, "and the run marker is never written");
    Check(0 == strcmp(BiRow(pFree, 1), "0"), "the aboard count is zero");
    Check(0 == strcmp(BiRow(pFree, 10), "0"), "no craft homed here leaves the hangar row at zero");
    Check(0 == strcmp(BiRow(pFree, 17), "") && 0 == strcmp(BiRow(pFree, 18), ""),
          "no craft cost rows");
    Check(0 == strcmp(BiRow(pFree, 19), "20000$"), "the price rows move to nShips+19");
    Check(0 == strcmp(BiRow(pTotals, 19), "40000$"), "and so do the money rows");
    Check(0 == strcmp(BiRow(pFree, 23), "0$"), "the facility row to nShips+23");
    Check(0 == strcmp(BiRow(pFree, 25), "230000$"), "the grand total to nShips+25");
    BiRowMeta(pNames, 18, 1, &nR, &nG, &nB);
    Check(nR == 200 && nG == 150 && nB == 150, "the tail meta moves to nShips+18");
    BiRelease();

    /* ---- a craft type outside the switch: no label, no cost, but the run continues ---- */
    BiFixture();
    g_aBiShip[3].bCraftType = 7;
    BaseInfoScreen_BuildPanels(&g_biEnv);
    pNames = g_apBiPanel[0];
    pFree = g_apBiPanel[1];
    pTotals = g_apBiPanel[2];
    Check(0 == strcmp(BiRow(pNames, 19), ""), "an unknown craft type has no display name");
    CheckEqI32(pNames->anLineAux[19], 7, "its aux still records the type");
    CheckEqI32(pTotals->anLineAux[19], 1, "and it still marks the run");
    Check(0 == strcmp(BiRow(pFree, 19), ""), "but the cost row stays empty");
    Check(0 == strcmp(BiRow(pFree, 22), "20000$"), "the run still reaches nShips+19");
    Check(0 == strcmp(BiRow(pFree, 28), "1430000$"), "and prices only the two known craft");
    BiRelease();

    /* ---- the name comes from the id lookup, the aux from the roster slot ---- */
    BiFixture();
    g_aBiShip[3].bId = 7; /* the same id as craft 0, so Ship_FindIndexById answers 0 */
    BaseInfoScreen_BuildPanels(&g_biEnv);
    pNames = g_apBiPanel[0];
    Check(0 == strcmp(BiRow(pNames, 19), "Intercep1"),
          "the row shows the craft the id resolves to, not the roster slot");
    CheckEqI32(pNames->anLineAux[19], 0, "while the aux still comes from the roster slot");
    Check(0 == strcmp(BiRow(g_apBiPanel[1], 19), "500000$"), "and it is still priced by that aux");
    BiRelease();

    /* ---- no craft at all ---- */
    BiFixture();
    g_bBiCraftCount = 0;
    BaseInfoScreen_BuildPanels(&g_biEnv);
    Check(0 == strcmp(BiRow(g_apBiPanel[0], 17), ""), "a zero craft count builds no rows");
    CheckEqI32(g_apBiPanel[0]->anLineAux[17], 0, "and leaves every aux at zero");
    BiRelease();

    /* ---- guards ---- */
    BiFixture();
    BaseInfoScreen_BuildPanels(NULL);
    Check(g_bBiBuilt == 0, "a null environment is refused");
    g_biEnv.pppPanel = NULL;
    BaseInfoScreen_BuildPanels(&g_biEnv);
    Check(g_bBiBuilt == 0, "an environment without a panel array is refused");
    BiFixture();
    g_biEnv.apCraft = NULL;
    BaseInfoScreen_BuildPanels(&g_biEnv);
    Check(g_apBiPanel[0] != NULL && 0 == strcmp(BiRow(g_apBiPanel[0], 17), ""),
          "a missing craft array still builds the screen");
    BiRelease();
}

static void BiThunkOk(void) { g_nBiOk++; }
static void BiThunkToggle(void) { g_nBiToggle++; }
static void BiThunkClose(void) { g_nBiClose++; }
static void BiThunkBuild(void) { g_nBiBuild++; }

static bool BiLoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                       const char *psz2, void *pUser)
{
    (void)pCtl;
    (void)pUser;
    PutStr(g_szBiPad0, psz0);
    PutStr(g_szBiPad1, psz1);
    PutStr(g_szBiPad2, psz2);
    return true;
}

static void TestBaseInfoInit(void)
{
    BaseInfoInitEnv env;
    i8 nSelected = 2;
    u8 bOverlay = 9;
    UiControl *pOk, *pToggle;

    printf("the base info screen bring-up\n");

    g_pBiPad = NULL;
    memset(&g_biCloseCtl, 0, sizeof(g_biCloseCtl));
    g_apBiCtl[0] = NULL;
    g_apBiCtl[1] = NULL;
    g_nBiWorkBase = 77;
    g_nBiOk = g_nBiToggle = g_nBiClose = g_nBiBuild = 0;
    g_szBiPad0[0] = g_szBiPad1[0] = g_szBiPad2[0] = 0;

    memset(&env, 0, sizeof(env));
    env.ppPad = &g_pBiPad;
    env.papCtl = g_apBiCtl;
    env.pCtlClose = &g_biCloseCtl;
    env.pfnOkClick = BiThunkOk;
    env.pfnToggleClick = BiThunkToggle;
    env.pfnCloseClick = BiThunkClose;
    env.pfnLoadPads = BiLoadPads;
    env.pPadUser = (void *)0x1234;
    env.pnWorkBaseIdx = &g_nBiWorkBase;
    env.pnSelectedBaseIdx = &nSelected;
    env.pbOverlayOpen = &bOverlay;
    env.pfnBuildPanels = BiThunkBuild;

    BaseInfoScreen_Init(&env);

    Check(g_pBiPad != NULL, "the screen pad is allocated");
    Check(g_pBiPad->pData != NULL, "the baseinfo pad loads out of the archives");
    CheckEqI32(g_pBiPad->nMode, PUFO_BASEINFO_PAD_MODE, "in palette slot 14");
    CheckEqI32(g_pBiPad->nOriginX, 0, "with its origin at 0");
    CheckEqI32(g_pBiPad->nOriginY, 0, "in both axes");
    Check(g_apBiCtl[0] != NULL && g_apBiCtl[1] != NULL, "both controls are allocated");
    if (g_apBiCtl[0] == NULL || g_apBiCtl[1] == NULL) {
        return;
    }
    pOk = g_apBiCtl[0];
    pToggle = g_apBiCtl[1];
    CheckEqI32(pOk->x, 1, "the OK control is placed at (1, 300)");
    CheckEqI32(pOk->y, 300, "in both axes");
    CheckEqI32(pToggle->x, 120, "the panel-mode toggle at (120, 300)");
    CheckEqI32(pToggle->y, 300, "in both axes");
    CheckEqI32(pOk->nMode, 0, "both in mode 0");
    Check(0 == strcmp(g_szBiPad0, "emptyb") && 0 == strcmp(g_szBiPad1, "cemptyb")
          && 0 == strcmp(g_szBiPad2, ""), "both use the empty-button pad pair");
    Check(pOk->pfnClick == BiThunkOk, "the first control gets the OK handler");
    Check(pToggle->pfnClick == BiThunkToggle, "the second gets the panel-mode toggle");
    CheckEqI32(g_biCloseCtl.x, 224, "the shared close control moves to (224, 1)");
    CheckEqI32(g_biCloseCtl.y, 1, "in both axes");
    Check(g_biCloseCtl.pfnClick == BiThunkClose, "and keeps the close thunk");

    CheckEqI32(g_nBiWorkBase, 2, "the working base takes the psi screen's selection");
    CheckEqI32(bOverlay, 0, "the overlay flag is cleared");
    CheckEqI32(g_nBiBuild, 1, "the panel build runs once, as the tail call");

    if (pOk->pfnClick != NULL) {
        pOk->pfnClick();
    }
    if (pToggle->pfnClick != NULL) {
        pToggle->pfnClick();
    }
    if (g_biCloseCtl.pfnClick != NULL) {
        g_biCloseCtl.pfnClick();
    }
    Check(g_nBiOk == 1 && g_nBiToggle == 1 && g_nBiClose == 1, "all three handlers are the wired ones");

    UiControl_FreePads(pOk);
    free(pOk);
    UiControl_FreePads(pToggle);
    free(pToggle);
    g_apBiCtl[0] = NULL;
    g_apBiCtl[1] = NULL;
    if (g_pBiPad != NULL) {
        PadImage_FreeData(g_pBiPad);
        free(g_pBiPad);
        g_pBiPad = NULL;
    }

    BaseInfoScreen_Init(NULL);
    Check(g_pBiPad == NULL && g_nBiBuild == 1, "a null environment is refused");
}

/* ---- BaseInfoScreen_Draw @ 0x56fb8 ---- */

static SoftSurf g_biSurf;
static GfxColorEnv g_biColor;
static BaseSiteRec g_aBiSiteRecs[8];
static BaseInfoDrawEnv g_biDrawEnv;
static i8 g_nBiBaseCount;
static i8 g_nBiAltBase;
static u8 g_bBiOverlay;
static i32 g_nBiStripCalls;
static i32 g_nBiStripArg;
static i32 g_nBiRebuildCalls;

static void BiThunkStrip(i32 nMark, void *pUser)
{
    (void)pUser;
    g_nBiStripArg = nMark;
    g_nBiStripCalls++;
}
static void BiThunkRebuild(void) { g_nBiRebuildCalls++; }

static i32 BiInkColour(const u16 *pPix, i32 x0, i32 y0, i32 x1, i32 y1, u16 uWant)
{
    i32 n = 0;

    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            if (pPix[y * 240 + x] == uWant) {
                ++n;
            }
        }
    }
    return n;
}

static i32 BiInkAny(const u16 *pPix, i32 x0, i32 y0, i32 x1, i32 y1)
{
    i32 n = 0;

    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            if (pPix[y * 240 + x] != 0) {
                ++n;
            }
        }
    }
    return n;
}

static void BiClear(const u16 *pPix)
{
    memset((void *)pPix, 0, 240 * 320 * 2);
    g_nBiStripCalls = 0;
    g_nBiStripArg = 999;
    g_nBiRebuildCalls = 0;
}

/* Brings up the real panels through BuildPanels, then wires the draw env over them. Two craft are
 * homed here, so the moving bands land at 18*2+59 = 95 and 18*2+169 = 205 -- clear of the fixed
 * 113 and 225 the panel-mode arm uses. */
static void BiDrawFixture(void)
{
    BiFixture();
    g_bBiCraftCount = 2;
    BaseInfoScreen_BuildPanels(&g_biEnv);

    memset(g_aBiSiteRecs, 0, sizeof(g_aBiSiteRecs));
    PutStr(g_aBiSiteRecs[0].abName, "Alpha");
    PutStr(g_aBiSiteRecs[1].abName, "Beta");

    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_SELECT, "Select");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_MONTHLY, "Monthly");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_BASE, "Base");

    g_nBiWorkBase = 0;
    g_nBiAltBase = 0;
    g_bBiOverlay = 0;
    g_nBiBaseCount = 2;
    g_biColor.pnGamma = &g_nGammaZero;
    g_biColor.nFadeCount = 0;

    memset(&g_biDrawEnv, 0, sizeof(g_biDrawEnv));
    g_biDrawEnv.pColor = &g_biColor;
    g_biDrawEnv.pbBuilt = &g_bBiBuilt;
    g_biDrawEnv.pbModeFlag = &g_bBiMode;
    g_biDrawEnv.pppPanel = g_apBiPanel;
    g_biDrawEnv.pnWorkBaseIdx = &g_nBiWorkBase;
    g_biDrawEnv.pnAltBaseIdx = &g_nBiAltBase;
    g_biDrawEnv.pbOverlayOpen = &g_bBiOverlay;
    g_biDrawEnv.apSiteRecs = g_aBiSiteRecs;
    g_biDrawEnv.pTextRecs = (const char *)g_aXfText;
    g_biDrawEnv.pnBaseCount = &g_nBiBaseCount;
    g_biDrawEnv.apCraft = g_apBiShip;
    g_biDrawEnv.nCraftCount = 2;
    g_biDrawEnv.apBases = g_apBiBases;
    g_biDrawEnv.nBaseCount = 1;
    g_biDrawEnv.abGridBytes = g_abBiGridBytes;
    g_biDrawEnv.pfnDrawIconStrip = BiThunkStrip;
    g_biDrawEnv.pfnBuildPanels = BiThunkRebuild;
}

static void TestBaseInfoDraw(void)
{
    const u16 *pPix;
    u16 uCapGlyph;

    printf("the base info screen's frame\n");

    FrameLoadFont();
    if (!g_bFrameFontReady) {
        printf("  FAIL lang.dat / main.dat did not open\n");
        ++g_nFailures;
        return;
    }

    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&g_biSurf);
    SoftSurf_AllocPixels(&g_biSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_biSurf;
    pPix = (const u16 *)g_biSurf.pPixels;

    /* The draw packs its colour pair at 0x5729C, AFTER the base name is drawn at 0x57284, so the
     * name is drawn in whatever the palette already held -- here the same pair, packed on entry. */
    uCapGlyph = (u16)Gfx_PackRgb565Pair(&g_biColor, 68, 244, 252, 4, 70, 75);
    Check(uCapGlyph != 0, "the caption colour pair packs to a non-zero glyph colour (0x2d82c)");

    /* ---- the gate ---- */
    BiDrawFixture();
    g_bBiBuilt = 0;
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32(BiInkAny(pPix, 0, 0, 239, 319), 0, "a screen with no panels built draws nothing at all");

    /* ---- panel mode on: the three fixed bands ---- */
    BiDrawFixture();
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32(g_bBiMode, 1, "the build arm left the mode flag set");
    CheckEqI32((i32)pPix[35 * 240 + 1], XfPix(100, 100, 100), "the ramp's first left block at (1, 35)");
    CheckEqI32((i32)pPix[35 * 240 + 5], XfPix(100, 100, 100), "  and it is 5 wide");
    CheckEqI32((i32)pPix[35 * 240 + 6], XfPix(105, 105, 105), "  the next step starts right at 6");
    CheckEqI32((i32)pPix[36 * 240 + 1], 0, "each block is one pixel tall");
    /* The left ramp runs to x 120 and the right one starts at x 119, so the left half -- drawn
     * later, in the same step order -- overdraws x 119 and 120 of the right half's first block. */
    CheckEqI32((i32)pPix[35 * 240 + 119], XfPix(215, 215, 215), "x 119 is the left ramp's last block, overdrawn");
    CheckEqI32((i32)pPix[35 * 240 + 121], XfPix(250, 250, 250), "the right ramp shows from x 121 with 250");
    CheckEqI32((i32)pPix[35 * 240 + 116], XfPix(215, 215, 215), "the left half climbs to 5*(23+20) = 215");
    CheckEqI32((i32)pPix[35 * 240 + 234], XfPix(135, 135, 135), "the right half falls to -6-5*23 = 135");
    CheckEqI32((i32)pPix[113 * 240 + 1], XfPix(100, 100, 100), "the second band is fixed at 113 in panel mode");
    CheckEqI32((i32)pPix[225 * 240 + 1], XfPix(100, 100, 100), "and the third at 225");
    CheckEqI32(BiInkColour(pPix, 0, 34, 239, 36, XfPix(100, 100, 100)), 5,
               "only the first left block is 100 on band A");
    CheckEqI32(BiInkColour(pPix, 0, 0, 239, 319, XfPix(100, 100, 100)), 15,
               "and 100 appears on all three bands, 5 pixels each");
    CheckEqI32(BiInkColour(pPix, 0, 0, 239, 319, XfPix(250, 250, 250)), 9,
               "250 survives only 3 pixels per band, x 119 and 120 being overdrawn");
    /* -6-5*7 = -41 = 215 as a byte, so the right ramp repeats the left ramp's top colour. */
    CheckEqI32(BiInkColour(pPix, 0, 0, 239, 319, XfPix(215, 215, 215)), 30,
               "215 comes from the left ramp's last step AND the right ramp's step 7");
    /* 5*(7+20) = 135 on the left, -6-5*23 = 135 on the right: the same doubling. */
    CheckEqI32(BiInkColour(pPix, 0, 0, 239, 319, XfPix(135, 135, 135)), 30,
               "and 135 from the right ramp's last step AND the left ramp's step 7");

    /* ---- panel mode off: the bands move with the craft count ---- */
    BiDrawFixture();
    g_bBiMode = 0;
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32((i32)pPix[95 * 240 + 1], XfPix(100, 100, 100), "with two craft the second band is 18*2+59 = 95");
    CheckEqI32((i32)pPix[205 * 240 + 1], XfPix(100, 100, 100), "and the third 18*2+169 = 205");
    CheckEqI32((i32)pPix[113 * 240 + 1], 0, "the fixed 113 is left empty");
    CheckEqI32((i32)pPix[225 * 240 + 1], 0, "and the fixed 225 too");
    g_aBiShip[2].bHomeBase = 0; /* the third craft is homed here too */
    g_bBiCraftCount = 3;
    g_biDrawEnv.nCraftCount = 3;
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32((i32)pPix[113 * 240 + 1], XfPix(100, 100, 100), "a third craft pushes the band to 18*3+59 = 113");
    CheckEqI32((i32)pPix[223 * 240 + 1], XfPix(100, 100, 100), "and the last to 18*3+169 = 223");

    /* ---- the two captions, one record at a time ---- */
    BiDrawFixture();
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_SELECT, "");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_MONTHLY, "");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_BASE, "");
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32(BiInkColour(pPix, 20, 300, 100, 315, uCapGlyph), 0, "with all three records empty no caption is drawn");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_SELECT, "Select");
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    Check(BiInkColour(pPix, 20, 300, 100, 315, uCapGlyph) > 0, "TEXTS[1112] reaches the (60, 300) band");
    CheckEqI32(BiInkColour(pPix, 140, 300, 220, 315, uCapGlyph), 0, "  and nothing lands at (180, 300) yet");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_SELECT, "");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_MONTHLY, "Monthly");
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    Check(BiInkColour(pPix, 140, 300, 220, 315, uCapGlyph) > 0,
          "in panel mode the toggle label is TEXTS[1113] at (180, 300)");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_MONTHLY, "");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_BASE, "Base");
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32(BiInkColour(pPix, 140, 300, 220, 315, uCapGlyph), 0,
               "TEXTS[1114] is NOT used while the mode flag is set");
    g_bBiMode = 0;
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    Check(BiInkColour(pPix, 140, 300, 220, 315, uCapGlyph) > 0,
          "with the flag clear it is TEXTS[1114] at (180, 300)");
    PutTextRec(g_aXfText, PUFO_BASEINFO_TEXT_BASE, "");

    /* ---- the base name, centred at (120, 13) in the colour that was current on entry ---- */
    BiDrawFixture();
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    {
        const i32 nWithName = BiInkColour(pPix, 60, 12, 180, 22, uCapGlyph);
        PutStr(g_aBiSiteRecs[0].abName, "");
        BiClear(pPix);
        BaseInfoScreen_Draw(&g_biDrawEnv);
        Check(nWithName > 0 && BiInkColour(pPix, 60, 12, 180, 22, uCapGlyph) == 0,
              "the site record's name is the caption centred at (120, 13)");
    }

    /* ---- the three panels really reach the surface. Only the name column's rows 0, 5, 12 and 16
     * carry a meta colour; every other row keeps the zeroed meta and draws in black, which a
     * cleared surface cannot show, so the probe uses a meta row. ---- */
    BiDrawFixture();
    /* The build writes the caption records into the name column, so this probe has to empty the column
     * first: rows 0 and 1 both reach the y 40..60 band. */
    for (i32 nClear = 0; nClear < PUFO_BASEINFO_PANEL_LINES; ++nClear) {
        (void)UiPanel_SetLineText(g_apBiPanel[0], nClear, "");
    }
    (void)UiPanel_SetLineText(g_apBiPanel[0], 0, "Zeta");
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    {
        const i32 nWithPanels = BiInkAny(pPix, 10, 40, 120, 60);
        (void)UiPanel_SetLineText(g_apBiPanel[0], 0, "");
        BiClear(pPix);
        BaseInfoScreen_Draw(&g_biDrawEnv);
        Check(nWithPanels > 0 && BiInkAny(pPix, 10, 40, 120, 60) == 0,
              "the columns are drawn through UiPanelMsg_Draw, at y 40");
    }

    /* ---- the overlay ---- */
    BiDrawFixture();
    g_bBiOverlay = 1;
    BiClear(pPix);
    SoftSurf_BlitRectProxy(0, 210, 59, 20, 20, 0xFF, 0xFF, 0xFF);
    SoftSurf_BlitRectProxy(0, 239, 300, 1, 1, 0xFF, 0xFF, 0xFF);
    SoftSurf_BlitRectProxy(0, 235, 59, 1, 1, 0xFF, 0xFF, 0xFF); /* only the 32-wide strip reaches x 235 */
    SoftSurf_BlitRectProxy(0, 207, 59, 1, 1, 0xFF, 0xFF, 0xFF); /* only the 210-wide rect reaches x 207 */
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32(g_nBiStripCalls, 1, "the icon strip runs once under the overlay");
    CheckEqI32(g_nBiStripArg, -1, "with the mark -1, so it paints no chevron of its own");
    CheckEqI32((i32)pPix[59 * 240 + 210], 0, "the 32-wide erase strip covers y 0..59 (28*2+4)");
    Check((i32)pPix[60 * 240 + 210] != 0, "  and stops exactly there");
    CheckEqI32((i32)pPix[59 * 240 + 235], 0, "  and it reaches x 208..239");
    CheckEqI32((i32)pPix[59 * 240 + 207], 0, "the 210-wide rect covers x 0..209, where the strip does not");
    Check((i32)pPix[300 * 240 + 239] != 0, "and neither rect reaches y 60 or x 210..239 below it");
    CheckEqI32((i32)pPix[3 * 240 + 3], XfPix(0x46, 0xA0, 0xB3), "the first base row's outer bevel");
    CheckEqI32((i32)pPix[4 * 240 + 3], XfPix(0x00, 0x6E, 0x77), "  its middle bevel");
    CheckEqI32((i32)pPix[5 * 240 + 5], XfPix(0x04, 0x46, 0x4B), "  and its inner bevel");
    CheckEqI32((i32)pPix[31 * 240 + 3], XfPix(0x46, 0xA0, 0xB3), "the second base row starts at 28+3");
    CheckEqI32(BiInkColour(pPix, 0, 55, 205, 55, XfPix(0x46, 0xA0, 0xB3)), 0,
               "a third row would start at 56, and there is none");
    {
        const i32 nRow0 = BiInkColour(pPix, 60, 7, 160, 17, uCapGlyph);
        const i32 nRow1 = BiInkColour(pPix, 60, 35, 160, 45, uCapGlyph);
        PutStr(g_aBiSiteRecs[0].abName, "");
        PutStr(g_aBiSiteRecs[1].abName, "");
        BiClear(pPix);
        SoftSurf_BlitRectProxy(0, 210, 59, 20, 20, 0xFF, 0xFF, 0xFF);
        SoftSurf_BlitRectProxy(0, 239, 300, 1, 1, 0xFF, 0xFF, 0xFF);
        BaseInfoScreen_Draw(&g_biDrawEnv);
        Check(nRow0 > 0 && nRow1 > 0
                  && BiInkColour(pPix, 60, 7, 160, 17, uCapGlyph) == 0
                  && BiInkColour(pPix, 60, 35, 160, 45, uCapGlyph) == 0,
              "each base row carries its own name centred at (110, 28*i+7)");
    }

    /* ---- the adopt arm, inside the overlay ---- */
    BiDrawFixture();
    g_bBiOverlay = 1;
    g_nBiAltBase = 1;
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32(g_nBiWorkBase, 1, "a different byte_158834 is adopted into byte_1870F4");
    CheckEqI32(g_bBiOverlay, 0, "the overlay flag is cleared");
    CheckEqI32(g_bBiBuilt, 0, "the built flag is cleared");
    Check(g_apBiPanel[0] == NULL && g_apBiPanel[1] == NULL && g_apBiPanel[2] == NULL,
          "the three panels are freed here, not on a click");
    CheckEqI32(g_nBiRebuildCalls, 1, "and the panel build runs again in the same call");
    BiRelease();

    BiDrawFixture();
    g_bBiOverlay = 1;
    g_nBiAltBase = 0;
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32(g_nBiWorkBase, 0, "the same base is not adopted");
    CheckEqI32(g_bBiBuilt, 1, "the panels stay built");
    CheckEqI32(g_nBiRebuildCalls, 0, "and nothing is rebuilt");
    BiRelease();

    /* no overlay, different base: the adopt arm is inside the overlay and does not run */
    BiDrawFixture();
    g_bBiOverlay = 0;
    g_nBiAltBase = 1;
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32(g_nBiWorkBase, 0, "with the overlay closed a different base is left alone");
    CheckEqI32(g_bBiBuilt, 1, "and the panels are not rebuilt");
    BiRelease();

    /* ---- guards ---- */
    BiDrawFixture();
    BiClear(pPix);
    BaseInfoScreen_Draw(NULL);
    CheckEqI32(BiInkAny(pPix, 0, 0, 239, 319), 0, "a null environment draws nothing");
    g_biDrawEnv.pnBaseCount = NULL;
    g_bBiOverlay = 1;
    BiClear(pPix);
    BaseInfoScreen_Draw(&g_biDrawEnv);
    CheckEqI32((i32)pPix[3 * 240 + 3], 0, "a missing base count gives no base rows");
    CheckEqI32(g_nBiStripCalls, 1, "but the erase rects and the strip still run");
    BiRelease();
}

/* ================= unit 94: the base-attack screen ================= */

/* Deliberately larger than PUFO_GRID_SIZE: one test asks about a base index past the eight grid
 * blocks, and it has to read a defined zero rather than whatever follows the array. */
static u8 g_abBaGrid[2048];
static UiPanel *g_pBaPanel;
static PadImage *g_pBaPad;
static PadImage *g_pBaNoPad; /* a slot that stays NULL, so the draw tests see only panel text */
static u8 g_bBaActive;
static u8 g_bBaConfirm;
static u8 g_bBaResult;
static u8 g_bBaClass;
static u8 g_bBaSpawn;
static i32 g_nBaLimit;
static i32 g_nBaCap;
static i32 g_nBaCooldown;
static BaseAtUpdateEnv g_baUpd;
static BaseAtDrawEnv g_baDraw;
static SoftSurf g_baSurf;
static GfxColorEnv g_baColor;
static UiControl g_baConfirmCtl;
static PadImage g_baCtlPad;
static i32 g_nBaBlitCalls;

static void BaThunkBlit(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
    ++g_nBaBlitCalls;
}

static void BaLabels(void)
{
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_FIRE_MISSILE, "MV");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_HIT, "HIT");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_MISS, "MISS");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_FIRE_LASER, "LR");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_FIRE_PLASMA, "PL");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_FIRE_FUSION, "FU");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_GRAVITY, "GRAV");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_RECHARGED, "RECH");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_DESTROYED, "DOWN");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_THREAT, "SAFE");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_LANDED, "LAND");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_PREPARING, "READY");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_OK, "OK");
}

static void BaFree(void)
{
    if (g_pBaPanel != NULL) {
        UiPanel_FreeLines(g_pBaPanel);
        free(g_pBaPanel);
        g_pBaPanel = NULL;
    }
    if (g_pBaPad != NULL) {
        PadImage_FreeData(g_pBaPad);
        free(g_pBaPad);
        g_pBaPad = NULL;
    }
}

static void BaEnv(void)
{
    BaFree();
    g_bBaActive = 0;
    g_bBaConfirm = 9;
    g_bBaResult = 9;
    g_bBaClass = 0;
    g_bBaSpawn = 0;
    g_nBaLimit = -1;
    g_nBaCap = -1;
    g_nBaCooldown = 0;
    g_nBaBlitCalls = 0;

    memset(&g_baUpd, 0, sizeof(g_baUpd));
    g_baUpd.pbActive = &g_bBaActive;
    g_baUpd.pbConfirmShown = &g_bBaConfirm;
    g_baUpd.pbMissionFollows = &g_bBaResult;
    g_baUpd.ppPad = &g_pBaPad;
    g_baUpd.ppPanel = &g_pBaPanel;
    g_baUpd.pnLineLimit = &g_nBaLimit;
    g_baUpd.pnLineCap = &g_nBaCap;
    g_baUpd.pbCraftClass = &g_bBaClass;
    g_baUpd.abFacilityGrid = g_abBaGrid;
    g_baUpd.pTextRecs = (const char *)g_aXfText;
    g_baUpd.pbSpawnReplacement = &g_bBaSpawn;
    g_baUpd.pnRadarCooldownR = &g_nBaCooldown;
}

/* Base 0's 36 cells, laid out in order from the type layer. The days layer stays all zero, so every
 * cell here is free -- the scan counts all of them. */
static void BaGrid(i32 nMissile, i32 nLaser, i32 nPlasma, i32 nFusion, i32 nGravity)
{
    u8 *const pType = g_abBaGrid + PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_TYPE_OFF;
    i32 nCell = 0;

    memset(g_abBaGrid, 0, sizeof(g_abBaGrid));
    for (i32 i = 0; i < nMissile; ++i) pType[nCell++] = (u8)FAC_MISSILE_DEFENCES;
    for (i32 i = 0; i < nLaser; ++i) pType[nCell++] = (u8)FAC_LASER_DEFENCE;
    for (i32 i = 0; i < nPlasma; ++i) pType[nCell++] = (u8)FAC_PLASMA_DEFENCE;
    for (i32 i = 0; i < nFusion; ++i) pType[nCell++] = (u8)FAC_FUSION_BALL_DEFENCE;
    for (i32 i = 0; i < nGravity; ++i) pType[nCell++] = (u8)FAC_GRAVITY_SHIELD;
}

/* A seed whose first Game_RandU16(100) lands in [nLo, nHi). The rolls are deterministic from the seed,
 * so this pins a roll into the band that separates one kind's threshold from the next. */
static i32 BaSeedRoll(i32 nLo, i32 nHi)
{
    for (i32 nSeed = 1; nSeed < 20000; ++nSeed) {
        srand((unsigned)nSeed);
        const i32 nRoll = (i32)Game_RandU16(100u);

        if (nRoll >= nLo && nRoll < nHi) {
            return nSeed;
        }
    }
    return -1;
}

/* And the same for the first nCount rolls all landing in one band. */
static i32 BaSeedRolls(i32 nCount, i32 nLo, i32 nHi)
{
    for (i32 nSeed = 1; nSeed < 500000; ++nSeed) {
        i32 nOk = 1;

        srand((unsigned)nSeed);
        for (i32 i = 0; i < nCount; ++i) {
            const i32 nRoll = (i32)Game_RandU16(100u);

            if (nRoll < nLo || nRoll >= nHi) {
                nOk = 0;
                break;
            }
        }
        if (nOk != 0) {
            return nSeed;
        }
    }
    return -1;
}

static void BaRun(i32 nClass, i32 nMissile, i32 nLaser, i32 nPlasma, i32 nFusion, i32 nGravity,
                  i32 nSeed)
{
    BaEnv();
    BaGrid(nMissile, nLaser, nPlasma, nFusion, nGravity);
    g_bBaClass = (u8)nClass;
    srand((unsigned)nSeed);
    BaseAtScreen_Update(0, &g_baUpd);
}

static void CheckLine(i32 nRow, const char *pszWant, const char *pszWhat)
{
    /* The update leaves nLineCount at 0 -- it is the reveal counter, not the row count -- so the
     * labels are read straight out of the line buffers. */
    const char *pszGot = (g_pBaPanel != NULL && nRow >= 0 && nRow < PUFO_PANEL_LINES
                          && g_pBaPanel->apLines[nRow] != NULL)
                             ? (const char *)g_pBaPanel->apLines[nRow] : NULL;

    CheckStr((pszGot != NULL) ? pszGot : "<none>", pszWant, pszWhat);
}

static void CheckAux(i32 nRow, i32 nWant, const char *pszWhat)
{
    CheckEqI32((g_pBaPanel != NULL) ? UiPanel_GetLineAux(g_pBaPanel, nRow) : -1, nWant, pszWhat);
}

static void CheckMeta(i32 nRow, i32 r0, i32 r1, i32 r2, i32 r3, i32 r4, i32 r5, const char *pszWhat)
{
    const u8 *p = (g_pBaPanel != NULL) ? (g_pBaPanel->aLineMeta + 6 * nRow) : NULL;

    if (p != NULL && p[0] == r0 && p[1] == r1 && p[2] == r2 && p[3] == r3 && p[4] == r4 && p[5] == r5) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s: got %d,%d,%d / %d,%d,%d want %d,%d,%d / %d,%d,%d\n", pszWhat,
               (p != NULL) ? p[0] : -1, (p != NULL) ? p[1] : -1, (p != NULL) ? p[2] : -1,
               (p != NULL) ? p[3] : -1, (p != NULL) ? p[4] : -1, (p != NULL) ? p[5] : -1,
               r0, r1, r2, r3, r4, r5);
        ++g_nFailures;
    }
}

static void TestBaseAtUpdate(void)
{
    i32 nSeedMiss;
    i32 nSeedHit;
    i32 nSeedBand;
    i32 nSeedHits;

    printf("the base-attack screen's battle log\n");

    BaLabels();

    /* ---- the build arm: the pad, the panel geometry, the flags ---- */
    nSeedMiss = BaSeedRoll(50, 100);
    Check(nSeedMiss > 0, "a seed with a first roll of 50 or more was found");
    BaRun(0, 1, 0, 0, 0, 0, nSeedMiss);
    CheckEqI32(g_bBaActive, 1, "the build arm arms the screen");
    CheckEqI32(g_bBaConfirm, 0, "and clears the confirm byte the pointer handlers gate on");
    Check(g_pBaPad != NULL, "the baseat pad is allocated");
    Check(g_pBaPad != NULL && g_pBaPad->nMode == 30, "and loaded in mode 30 (0x578E4)");
    Check(g_pBaPad != NULL && g_pBaPad->pData != NULL, "  the pad data is really in main.dat");

    Check(g_pBaPad != NULL && g_pBaPad->nOriginX == 0 && g_pBaPad->nOriginY == 0,
          "with both origins cleared");
    Check(g_pBaPanel != NULL, "the panel is allocated");
    /* The geometry is asserted against the literals the ARM stores, not against the port's own macros:
     * a check that reads the macro cannot tell a wrong macro from a right one. */
    CheckEqI32(g_pBaPanel->nStyle, 15, "nStyle 15 is the visible row count (0x57AE4)");
    CheckEqI32(g_pBaPanel->nFont, 16, "nFont 16 (0x57AF4)");
    CheckEqI32(g_pBaPanel->nPosX, 10, "the panel sits at x 10 (0x57B08)");
    CheckEqI32(g_pBaPanel->nPosY, 10, "and y 10 (0x57B18)");
    CheckEqI32(g_pBaPanel->nRectX, 5, "nRectX 5 (0x57B2C)");
    CheckEqI32(g_pBaPanel->nRectY, 70, "nRectY 70 (0x57B40)");
    CheckEqI32(g_pBaPanel->nViewW, 230, "nViewW 230 (0x57B54)");
    CheckEqI32(g_pBaPanel->aStyleRgb[0], 0x64, "the style key is 0x64,0x96,0x64");
    CheckEqI32(g_pBaPanel->aStyleRgb[1], 0x96, "  its middle byte");
    CheckEqI32(g_pBaPanel->aStyleRgb[2], 0x64, "  and its last");
    CheckEqI32(g_pBaPanel->aStyleRgb[3], 0x32, "the source triple is 0x32,0x32,0xFA");
    CheckEqI32(g_pBaPanel->aStyleRgb[4], 0x32, "  written by the third setter call");
    CheckEqI32(g_pBaPanel->aStyleRgb[5], 0xFA, "  the three setters cover 0..11 without overlap");
    CheckEqI32(g_pBaPanel->aStyleRgb[6], 0xDC, "the panel colours are 0xDC,0xC8,0xF0");
    CheckEqI32(g_pBaPanel->aStyleRgb[8], 0xF0, "  and 0x14,0x14,0x14 for the second");
    CheckEqI32(g_pBaPanel->aStyleRgb[11], 0x14, "  reaching byte 11");
    CheckEqI32(g_pBaPanel->bColorKey, 0x96, "bColorKey is -106 as a byte");
    CheckEqI32(g_pBaPanel->bRowRules, 0, "both ctor flags are cleared");
    CheckEqI32(g_pBaPanel->bSelLineHighlight, 0, "  the second too");

    /* ---- one missile cell, class 0: a miss leaves the budget alone ---- */
    CheckEqI32(g_nBaLimit, 5, "one defence pair plus the three tail rows");
    CheckEqI32(g_nBaCap, 5, "and the capacity formula gives the same 2*(1*1+0)+3");
    CheckLine(0, "MV", "the missile cell opens with its FIRE line");
    CheckLine(1, "MISS", "a roll of 50 or more is a miss");
    CheckMeta(1, 255, 50, 50, 20, 0, 0, "  the miss row gets the red meta");
    CheckLine(2, "", "the tail's first row is the empty label, not a text record");
    CheckLine(3, "LAND", "the budget survived, so the craft got out");
    CheckLine(4, "READY", "and the base prepares for the assault");
    CheckEqI32(g_bBaResult, 1, "the follow-up flag is set, which is what the close thunk tests");
    CheckAux(0, 20, "a defence row carries aux 20, the reveal delay");
    CheckAux(1, 20, "  and so does its result row");
    CheckAux(2, 0, "the tail rows carry aux 0");
    CheckAux(3, 0, "  the middle one too, so its reveal delay is not 0 by accident");
    CheckAux(4, 0, "  and the last");
    CheckEqI32(g_nBaCooldown, PUFO_RADAR_COOLDOWN_READY, "the cooldown is parked at 255 for the reveal");
    CheckEqI32(g_pBaPanel->nLineCount, 0, "and every row starts hidden");

    /* ---- the same cell with a hit: 50 - 500 burns the whole budget ---- */
    nSeedHit = BaSeedRoll(0, 50);
    Check(nSeedHit > 0, "a seed with a first roll below 50 was found");
    BaRun(0, 1, 0, 0, 0, 0, nSeedHit);
    CheckLine(1, "HIT", "a roll below 50 is a hit");
    CheckMeta(1, 50, 255, 50, 0, 20, 0, "  the hit row gets the green meta");
    CheckLine(3, "DOWN", "the budget is gone, so the craft is destroyed");
    CheckLine(4, "SAFE", "and the capture threat is lifted");
    CheckEqI32(g_bBaResult, 0, "with no ground mission to follow");
    CheckEqI32(g_nBaLimit, 5, "the pair still ran before the budget test stopped it");

    /* ---- the four thresholds, each pinned by a roll in the band below the next ---- */
    nSeedBand = BaSeedRoll(50, 60);
    Check(nSeedBand > 0, "a seed with a first roll in [50,60) was found");
    BaRun(0, 0, 1, 0, 0, 0, nSeedBand);
    CheckLine(1, "HIT", "laser defence hits on a roll of 50, its threshold is 60");
    BaRun(0, 1, 0, 0, 0, 0, nSeedBand);
    CheckLine(1, "MISS", "  while missile defence misses on the same roll");

    nSeedBand = BaSeedRoll(60, 70);
    BaRun(0, 0, 0, 1, 0, 0, nSeedBand);
    CheckLine(1, "HIT", "plasma defence hits on a roll of 60, its threshold is 70");
    BaRun(0, 0, 1, 0, 0, 0, nSeedBand);
    CheckLine(1, "MISS", "  laser defence misses on it");

    nSeedBand = BaSeedRoll(70, 80);
    BaRun(0, 0, 0, 0, 1, 0, nSeedBand);
    CheckLine(1, "HIT", "fusion-ball defence hits on a roll of 70, its threshold is 80");
    BaRun(0, 0, 0, 1, 0, 0, nSeedBand);
    CheckLine(1, "MISS", "  plasma defence misses on it");

    nSeedBand = BaSeedRoll(80, 100);
    BaRun(0, 0, 0, 0, 1, 0, nSeedBand);
    CheckLine(1, "MISS", "fusion-ball defence misses at 80 and above");
    CheckLine(0, "FU", "  from its own FIRE line");

    /* ---- the four damages, each pinned by how far the budget reaches ---- */
    nSeedHits = BaSeedRolls(3, 0, 50);
    Check(nSeedHits > 0, "a seed with three rolls below 50 was found");
    BaRun(5, 3, 0, 0, 0, 0, nSeedHits); /* 1200 - 500 - 500 - 500 */
    CheckEqI32(g_nBaLimit, 9, "class 5 burns 1200 over three missile hits of 500");
    CheckEqI32(g_bBaResult, 0, "  and the third one finishes it");
    BaRun(5, 2, 0, 0, 0, 0, nSeedHits); /* 1200 - 500 - 500 leaves 200 */
    CheckEqI32(g_nBaLimit, 7, "two of those hits leave 200, so only two pairs run");
    CheckEqI32(g_bBaResult, 1, "  and the craft lands");

    BaRun(5, 0, 2, 0, 0, 0, nSeedHits); /* 1200 - 600 - 600 */
    CheckEqI32(g_nBaLimit, 7, "class 5 burns 1200 over two laser hits of 600");
    CheckEqI32(g_bBaResult, 0, "  the second reaches exactly 0, which is below 1");

    nSeedHits = BaSeedRolls(3, 0, 60);
    BaRun(6, 0, 0, 3, 0, 0, nSeedHits); /* 2200 - 900 - 900 - 900 */
    CheckEqI32(g_nBaLimit, 9, "class 6 burns 2200 over three plasma hits of 900");
    CheckEqI32(g_bBaResult, 0, "  and the third one finishes it");
    BaRun(6, 0, 0, 2, 0, 0, nSeedHits); /* 2200 - 900 - 900 leaves 400 */
    CheckEqI32(g_bBaResult, 1, "two plasma hits leave 400, so the craft lands");

    nSeedHits = BaSeedRolls(2, 0, 70);
    BaRun(7, 0, 0, 0, 2, 0, nSeedHits); /* 3000 - 1500 - 1500 */
    CheckEqI32(g_nBaLimit, 7, "class 7 burns 3000 over two fusion hits of 1500");
    CheckEqI32(g_bBaResult, 0, "  reaching exactly 0");
    BaRun(7, 0, 0, 0, 1, 0, nSeedHits); /* 3000 - 1500 leaves 1500 */
    CheckEqI32(g_bBaResult, 1, "one of them leaves 1500, so the craft lands");

    /* ---- the budget table, read through how many hits each class can take ---- */
    nSeedHits = BaSeedRolls(6, 0, 50);
    Check(nSeedHits > 0, "a seed with six rolls below 50 was found");
    BaRun(0, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 5, "class 0's budget of 50 dies on the first 500-point hit");
    BaRun(1, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 5, "class 1's 200 does too");
    BaRun(2, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 5, "class 2's 250 does too");
    BaRun(3, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 5, "class 3's 500 reaches exactly 0");
    BaRun(4, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 5, "and class 4 shares that budget");
    BaRun(5, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 9, "class 5's 1200 takes three hits");
    BaRun(6, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 13, "class 6's 2200 takes five");
    BaRun(7, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 15, "class 7's 3000 takes all six cells");
    CheckEqI32(g_nBaCap, 15, "  and its capacity is 2*6+3");
    BaRun(8, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 5, "a class outside 0..7 has no budget at all");
    CheckEqI32(g_bBaResult, 0, "  so the first pair already ends the fight");
    BaRun(255, 6, 0, 0, 0, 0, nSeedHits);
    CheckEqI32(g_nBaLimit, 5, "and 255 reads as -1, which the switch has no case for");

    /* ---- the gravity rounds: the whole barrage runs (gravity + 1) times ---- */
    nSeedMiss = BaSeedRolls(3, 50, 100);
    Check(nSeedMiss > 0, "a seed with three rolls of 50 or more was found");
    BaRun(0, 1, 0, 0, 0, 2, nSeedMiss);
    CheckEqI32(g_nBaLimit, 13, "one defence cell with two gravity cells runs three rounds");
    CheckEqI32(g_nBaCap, 13, "and the capacity 2*(1*3+2)+3 matches the rows produced");
    CheckLine(0, "MV", "round 0's defence pair");
    CheckLine(2, "GRAV", "  then the gravity pair");
    CheckLine(3, "RECH", "  with the recharge line under it");
    CheckLine(4, "MV", "round 1 runs the defence pair again");
    CheckLine(6, "GRAV", "  and the gravity pair again");
    CheckLine(8, "MV", "round 2 runs the defence pair a third time");
    CheckLine(10, "", "the tail follows, with no gravity pair in the last round");
    CheckLine(11, "LAND", "  and the budget survived every miss");
    CheckAux(2, 0, "the gravity pair's first row has aux 0");
    CheckAux(3, 10, "  and its second row aux 10");
    CheckAux(6, 0, "the second gravity pair repeats it");
    CheckAux(7, 10, "  in the same order");
    CheckMeta(2, 150, 150, 255, 0, 20, 20, "the gravity rows share the 150,150,255 meta");
    CheckMeta(3, 150, 150, 255, 0, 20, 20, "  on both of them");
    CheckLine(9, "MISS", "  and the third defence pair still rolled");

    BaRun(0, 1, 0, 0, 0, 0, nSeedMiss);
    CheckEqI32(g_nBaLimit, 5, "with no gravity cell the barrage runs exactly once");

    /* ---- only FREE cells count ---- */
    BaEnv();
    BaGrid(2, 0, 0, 0, 0);
    g_abBaGrid[0] = 7; /* the days byte of the first missile cell */
    g_bBaClass = 0;
    srand(nSeedHits);
    /* BaseAtScreen_Update @ 0x57808: 3872 bytes = 925 instructions (25 of them 2-word MOV immediates) + 18 pool
     * words, 32 calls. The checks below pin the parts a reader can actually re-derive from the record: the
     * facility-type scan over the eight grid blocks, the days layer read from the same base block, the release arm
     * that frees the panel and the pad, and the bound on the base index. */
    BaseAtScreen_Update(0, &g_baUpd);
    CheckEqI32(g_nBaLimit, 5, OptCite("a cell still under construction is not a defence", "@ 0x57808"));
    CheckLine(0, "MV", OptCite("  only the finished one fires", "@ 0x57808"));

    BaEnv();
    BaGrid(1, 0, 0, 0, 0);
    g_abBaGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_TYPE_OFF] = (u8)FAC_HANGAR;
    g_bBaClass = 0;
    srand(nSeedHits);
    BaseAtScreen_Update(0, &g_baUpd);
    CheckEqI32(g_nBaLimit, 3, "a hangar is not a defence either");
    CheckLine(1, "LAND", "  so the log is only the tail");
    CheckEqI32(g_nBaCap, 3, "and the capacity drops to the three tail rows");

    /* ---- the base index selects the grid block ---- */
    BaEnv();
    BaGrid(0, 0, 0, 0, 0);
    g_abBaGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE + PUFO_GRID_FAC_TYPE_OFF] =
        (u8)FAC_MISSILE_DEFENCES; /* base 1's first cell */
    g_bBaClass = 0;
    srand(nSeedHits);
    BaseAtScreen_Update(1, &g_baUpd);
    CheckEqI32(g_nBaLimit, 5, OptCite("the scan follows the base index into the second grid block", "@ 0x57808"));
    BaEnv();
    BaGrid(1, 0, 0, 0, 0);
    g_bBaClass = 0;
    srand(nSeedHits);
    BaseAtScreen_Update(1, &g_baUpd);
    CheckEqI32(g_nBaLimit, 3, "  and base 1 sees none of base 0's cells");

    /* The days layer has to follow the base index too. Hex-Rays renders both layers through the
     * buildDays array type, so a port that reuses one base offset for both looks correct. */
    BaEnv();
    BaGrid(0, 0, 0, 0, 0);
    g_abBaGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE + PUFO_GRID_FAC_TYPE_OFF] =
        (u8)FAC_MISSILE_DEFENCES;   /* base 1's first cell is a missile defence */
    g_abBaGrid[PUFO_GRID_DAYS_STRIDE] = 9; /* and it is still under construction */
    g_bBaClass = 0;
    srand(nSeedHits);
    BaseAtScreen_Update(1, &g_baUpd);
    CheckEqI32(g_nBaLimit, 3, OptCite("the days layer is read from the same base block as the type layer",
                                      "@ 0x57808"));

    /* And the eight-block bound is real: a ninth block would sit at 480 + 144*8. */
    BaEnv();
    BaGrid(0, 0, 0, 0, 0);
    g_abBaGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * 8 + PUFO_GRID_FAC_TYPE_OFF] =
        (u8)FAC_MISSILE_DEFENCES;
    g_bBaClass = 0;
    srand(nSeedHits);
    BaseAtScreen_Update(8, &g_baUpd);
    CheckEqI32(g_nBaLimit, 3, OptCite("a base index past the eight grid blocks is refused", "@ 0x57808"));
    Check(g_pBaPanel != NULL, OptCite("  the panel is still built, only the scan is skipped", "@ 0x57808"));

    /* ---- the release arm ---- */
    BaRun(0, 1, 0, 0, 0, 0, nSeedMiss);
    Check(g_pBaPanel != NULL && g_pBaPad != NULL, "the build arm left both objects live");
    g_bBaSpawn = 0;
    BaseAtScreen_Update(0, &g_baUpd);
    CheckEqI32(g_bBaActive, 0, OptCite("the second call takes the release arm instead", "@ 0x57808"));
    Check(g_pBaPanel == NULL, OptCite("  the panel is freed and the slot nulled", "@ 0x57808"));
    Check(g_pBaPad == NULL, OptCite("  and so is the pad", "@ 0x57808"));
    CheckEqI32(g_bBaSpawn, 1, OptCite("and the one-shot replacement flag is armed for the strateg pass",
                                      "@ 0x57808"));
    CheckEqI32(g_nBaLimit, 5, OptCite("the row limit is left as it was", "@ 0x57808"));

    /* ---- guards ---- */
    BaEnv();
    BaGrid(1, 0, 0, 0, 0);
    BaseAtScreen_Update(0, NULL);
    CheckEqI32(g_bBaActive, 0, "a null environment does nothing");
    BaEnv();
    g_baUpd.abFacilityGrid = NULL;
    srand(nSeedHits);
    BaseAtScreen_Update(0, &g_baUpd);
    CheckEqI32(g_nBaLimit, 3, "with no grid the log is only the tail");
    Check(g_pBaPanel != NULL, "  the panel is still built");
    BaEnv();
    BaGrid(1, 0, 0, 0, 0);
    srand(nSeedHits);
    BaseAtScreen_Update(-1, &g_baUpd);
    CheckEqI32(g_nBaLimit, 3, OptCite("a negative base index is bounded instead of reading before the grid",
                                      "@ 0x57808"));
    BaseAtScreen_Update(8, &g_baUpd);
    CheckEqI32(g_bBaActive, 0, OptCite("and the release arm runs on the same call", "@ 0x57808"));

    /* ---- the burn each defence pair takes out of the attack budget ------------------------------------
     * The burns are fixed immediates in the record: 500 (`SUB R3,R3,#0x1F4` at 0x57CE0, stored back at
     * 0x57CE4), 600 (`#0x258` at 0x57E8C), 900 (`#0x384` at 0x5803C) and 1500 (`MOV R3,#0x5D0` / `ORR
     * R3,R3,#0xC` at 0x581E0, `SUB R3,R2,R3` at 0x581EC, stored back at 0x581F0). The budget itself is a
     * stack cell ([SP,#var_C0]) and nothing reads it out: it only decides whether the barrage runs the
     * next pair, so a burn is observable as the pair count it allows. Two fixtures per constant therefore
     * pin that constant's interval, and the interval's ends are what the checks assert. Gravity pairs burn
     * nothing (0x58390 is `SUB R3,R3,R1` between the two ends of the "ALL RECHARGED" label copy, not a
     * burn), the barrage runs nGravity + 1 rounds, and every round re-fires every defence cell. */
    {
        const i32 nSeedM = BaSeedRolls(5, 0, 50); /* five rolls under the missile threshold: all hit */
        const i32 nSeedL = BaSeedRolls(5, 0, 60);
        const i32 nSeedP = BaSeedRolls(5, 0, 70);
        const i32 nSeedF = BaSeedRolls(5, 0, 80);

        Check(nSeedM > 0 && nSeedL > 0 && nSeedP > 0 && nSeedF > 0, "seeds with five hits per kind were found");

        /* missile >= 500: a 500 budget with one missile cell and a gravity cell, so 2 rounds are available.
         * The first pair leaves 0, which is < 1, and the barrage stops before the gravity pair. */
        BaRun(3, 1, 0, 0, 0, 1, nSeedM);
        CheckEqI32(g_nBaLimit, 5, OptCite("one missile pair burns the whole 500 budget and stops the barrage",
                                          "@ 0x57808"));
        CheckEqI32(g_nBaCap, 9, OptCite("  the capacity still counts the round that never ran", "@ 0x57808"));

        /* missile <= 599: a 1200 budget, one missile cell, 3 rounds. 1200 - 2*600 is 0, so a burn of 600
         * would stop the barrage after the second pair; the burn that lets all three pairs run is under 600. */
        BaRun(5, 1, 0, 0, 0, 2, nSeedM);
        CheckEqI32(g_nBaLimit, 13, OptCite("all three missile pairs run, so the missile burn is under 600",
                                           "@ 0x57808"));
        CheckEqI32(g_nBaCap, 13, OptCite("  and the log fills the capacity exactly", "@ 0x57808"));

        /* laser >= 600: 1200 budget, one laser cell, 3 rounds. Two laser burns of 600 empty the budget, so
         * the third pair never runs; a burn of 599 leaves 2 and the run completes. */
        BaRun(5, 0, 1, 0, 0, 2, nSeedL);
        CheckEqI32(g_nBaLimit, 9, OptCite("the laser burn is at least 600: two pairs empty a 1200 budget",
                                          "@ 0x57808"));

        /* laser <= 749: 3000 budget, one laser cell, 5 rounds. Four burns of 750 empty it (3000 - 4*750 = 0),
         * so the fifth round would not fire; 600 leaves 600 and the run completes all five rounds. */
        BaRun(7, 0, 1, 0, 0, 4, nSeedL);
        CheckEqI32(g_nBaLimit, 21, OptCite("the laser burn is under 750: five rounds of it fit in 3000",
                                           "@ 0x57808"));
        CheckEqI32(g_nBaCap, 21, OptCite("  so the log reaches the capacity", "@ 0x57808"));

        /* plasma >= 750: 3000 budget, one plasma cell, 5 rounds. Four burns of 749 leave 5, so the fifth pair
         * runs; the real burn of 900 leaves -600 and stops after the fourth. */
        BaRun(7, 0, 0, 1, 0, 4, nSeedP);
        CheckEqI32(g_nBaLimit, 17, OptCite("the plasma burn is at least 750: four rounds of it overshoot 3000",
                                           "@ 0x57808"));

        /* plasma <= 999: 3000 budget, one plasma cell, 4 rounds. A burn of 1000 empties the budget on the
         * third round's pair and that round's gravity pair never appears; 900 leaves 300 and all four
         * rounds' pairs run, which is exactly the capacity. */
        BaRun(7, 0, 0, 1, 0, 3, nSeedP);
        CheckEqI32(g_nBaLimit, 17, OptCite("the plasma burn is under 1000: three rounds of it leave 300 to spare",
                                           "@ 0x57808"));
        CheckEqI32(g_nBaCap, 17, OptCite("  and the log fills the capacity", "@ 0x57808"));

        /* fusion >= 1500: 3000 budget, one fusion cell, 3 rounds. Two burns of 1500 empty it, so the barrage
         * stops after the second pair; 1499 leaves 2 and the run reaches the third pair. */
        BaRun(7, 0, 0, 0, 1, 2, nSeedF);
        CheckEqI32(g_nBaLimit, 9, OptCite("the fusion burn is at least 1500: two pairs empty a 3000 budget",
                                          "@ 0x57808"));

        /* fusion <= 2199: 2200 budget, one fusion cell, 2 rounds. A burn of 2200 empties the budget on the
         * first pair and the gravity pair never appears; 1500 leaves 700 and the run completes. */
        BaRun(6, 0, 0, 0, 1, 1, nSeedF);
        CheckEqI32(g_nBaLimit, 9, OptCite("the fusion burn is under 2200: one pair leaves 700 of a 2200 budget",
                                          "@ 0x57808"));
        CheckEqI32(g_nBaCap, 9, OptCite("  so both rounds run and the log fills the capacity", "@ 0x57808"));
    }
}

static void BaDrawEnv(void)
{
    memset(&g_baDraw, 0, sizeof(g_baDraw));
    g_baDraw.pColor = &g_baColor;
    g_baDraw.pbActive = &g_bBaActive;
    g_baDraw.ppPad = &g_pBaNoPad; /* the pad paint is checked on its own, away from the text */
    g_baDraw.ppPanel = &g_pBaPanel;
    g_baDraw.pnLineLimit = &g_nBaLimit;
    g_baDraw.pnRadarCooldownR = &g_nBaCooldown;
    g_baDraw.pbConfirmShown = &g_bBaConfirm;
    g_baDraw.pCtlConfirm = &g_baConfirmCtl;
    g_baDraw.pfnCtlBlit = BaThunkBlit;
    g_baDraw.pfnCtlBlitEx = BaThunkBlit;
    g_baDraw.pTextRecs = (const char *)g_aXfText;
}

static void TestBaseAtDraw(void)
{
    const u16 *pPix;
    i32 nSeed;
    i32 nSeedWide;
    u16 uBaGlyph;

    printf("the base-attack screen's frame\n");

    FrameLoadFont();
    if (!g_bFrameFontReady) {
        printf("  FAIL lang.dat / main.dat did not open\n");
        ++g_nFailures;
        return;
    }

    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&g_baSurf);
    SoftSurf_AllocPixels(&g_baSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_baSurf;
    pPix = (const u16 *)g_baSurf.pPixels;

    g_baColor.pnGamma = &g_nGammaZero;
    g_baColor.nFadeCount = 0;
    /* The draw packs this same pair at 0x5884C right before the caption, so pre-packing it gives the
     * glyph colour the caption must be drawn in. */
    uBaGlyph = (u16)Gfx_PackRgb565Pair(&g_baColor, 68, 244, 252, 4, 70, 75);
    Check(uBaGlyph != 0, "the caption colour pair packs to a non-zero glyph colour (0x2d82c)");
    g_baConfirmCtl.uWidth = 40;
    g_baConfirmCtl.nMode = 0;      /* mode 0 uses nState directly */
    g_baConfirmCtl.bEnabled = 1;
    g_baConfirmCtl.nState = 0;
    g_baConfirmCtl.bUnk17 = 0;     /* so the plain blit hook is the one called */
    g_baConfirmCtl.apPads[0] = &g_baCtlPad;

    /* 13 rows: one missile cell and two gravity cells, every roll a miss. Row r draws at
     * y = 16*r + 10, x 10, because nUnk7076 stays 0. */
    nSeed = BaSeedRolls(3, 50, 100);
    BaRun(0, 1, 0, 0, 0, 2, nSeed);
    BaDrawEnv();

    /* ---- the gate ---- */
    BiClear(pPix);
    g_bBaActive = 0;
    /* BaseAtScreen_Draw @ 0x58728: 420 bytes = 94 instructions (one 2-word MOV) + 10 pool words, 7 calls. What the
     * checks below pin is the gate on the update's active byte, the per-row reveal pacing through the row's aux
     * value, the scroll gate at nStyle, and the confirm control that only appears once every row is shown. */
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(BiInkAny(pPix, 0, 0, 239, 319), 0,
               OptCite("a screen the update released draws nothing", "@ 0x58728"));
    g_bBaActive = 1;

    /* ---- the reveal pacing ---- */
    BiClear(pPix);
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_nBaCooldown, 235, OptCite("the first row's aux 20 parks the cooldown at 255-20", "@ 0x58728"));
    CheckEqI32(g_pBaPanel->nLineCount, 1, OptCite("and one row becomes visible", "@ 0x58728"));
    Check(BiInkAny(pPix, 10, 12, 60, 24) > 0, "row 0's FIRE line reaches the surface");
    CheckEqI32(BiInkAny(pPix, 10, 28, 60, 40), 0, "  row 1 is still hidden");

    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_nBaCooldown, 235, "a cooldown below 255 reveals nothing new");
    CheckEqI32(g_pBaPanel->nLineCount, 1, "  and the row count stays put");

    g_nBaCooldown = PUFO_RADAR_COOLDOWN_READY;
    BiClear(pPix);
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_nBaCooldown, 235, "the result row also carries aux 20");
    CheckEqI32(g_pBaPanel->nLineCount, 2, "so two rows are visible now");
    Check(BiInkAny(pPix, 10, 28, 60, 40) > 0, "row 1's MISS line draws");
    CheckEqI32(BiInkAny(pPix, 10, 44, 60, 56), 0, "  and row 2 does not");

    g_nBaCooldown = PUFO_RADAR_COOLDOWN_READY;
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_nBaCooldown, 255, "the gravity pair's first row has aux 0, so the cooldown stays ready");
    CheckEqI32(g_pBaPanel->nLineCount, 3, "  and the row appears at once");

    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_nBaCooldown, 245, "its second row has aux 10, so the cooldown parks at 245");
    CheckEqI32(g_pBaPanel->nLineCount, 4, "  after that row is shown");

    /* ---- the empty tail row draws nothing ---- */
    for (i32 nStep = 0; nStep < 9; ++nStep) {
        g_nBaCooldown = PUFO_RADAR_COOLDOWN_READY;
        BaseAtScreen_Draw(&g_baDraw);
    }
    CheckEqI32(g_pBaPanel->nLineCount, 13, "every row is revealed once the cooldown keeps resetting");
    CheckEqI32(g_pBaPanel->nLineCount, g_nBaLimit, "  which is exactly the row limit");
    BiClear(pPix);
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(BiInkAny(pPix, 10, 16 * 10 + 10, 60, 16 * 10 + 24), 0,
               "the tail's empty label draws no ink at all");
    Check(BiInkAny(pPix, 10, 16 * 11 + 10, 60, 16 * 11 + 24) > 0, "  while the row under it does");

    /* ---- the scroll gate at nStyle ---- */
    /* Eight defence cells and no gravity: 16 pairs plus the tail is 19 rows, enough to pass 15. */
    nSeedWide = BaSeedRolls(8, 50, 100);
    Check(nSeedWide > 0, "a seed with eight rolls of 50 or more was found");
    BaRun(0, 8, 0, 0, 0, 0, nSeedWide);
    CheckEqI32(g_nBaLimit, 19, "eight cells with no gravity give 2*8+3 rows");
    BaDrawEnv();
    g_pBaPanel->nLineCount = 14;
    g_nBaCooldown = PUFO_RADAR_COOLDOWN_READY;
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_pBaPanel->nLineCount, 15, OptCite("the 15th row is shown", "@ 0x58728"));
    CheckEqI32(g_pBaPanel->nUnk3028, 0, OptCite("  and 15 is not past nStyle, so nothing scrolls", "@ 0x58728"));
    g_nBaCooldown = PUFO_RADAR_COOLDOWN_READY;
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_pBaPanel->nLineCount, 16, OptCite("the 16th row passes nStyle", "@ 0x58728"));
    CheckEqI32(g_pBaPanel->nUnk3028, 1, OptCite("  and the panel scrolls one row", "@ 0x58728"));

    /* ---- the confirm control ---- */
    BaRun(0, 1, 0, 0, 0, 2, nSeed);
    BaDrawEnv();
    g_baConfirmCtl.x = 1;
    g_baConfirmCtl.y = 1;
    g_bBaConfirm = 0;
    BiClear(pPix);
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_bBaConfirm, 0, "with rows still hidden the confirm byte stays clear");
    CheckEqI32(g_baConfirmCtl.x, 1, "  the control is not moved");
    CheckEqI32(g_baConfirmCtl.y, 1, "  and not placed");
    CheckEqI32(g_nBaBlitCalls, 0, "  and its pad is not blitted");

    g_pBaPanel->nLineCount = g_nBaLimit;
    BiClear(pPix);
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_bBaConfirm, 1, OptCite("once every row is shown the confirm byte is set", "@ 0x58728"));
    CheckEqI32(g_baConfirmCtl.x, 100, OptCite("the control is centred on 120 - (40 >> 1)", "@ 0x58728"));
    CheckEqI32(g_baConfirmCtl.y, 290, OptCite("  at y 290", "@ 0x58728"));
    CheckEqI32(g_nBaBlitCalls, 1, OptCite("its pad is blitted once", "@ 0x58728"));
    Check(BiInkColour(pPix, 100, 292, 140, 306, uBaGlyph) > 0,
          "and the OK caption is drawn in the packed pair's glyph colour");
    Check(BiInkAny(pPix, 100, 292, 140, 306) > 0, "  which is real ink, not a palette accident");

    /* The caption uses the pair this function packs, so a differential probe pins both the pack and
     * the caption: the same draw with no text records for 1127 leaves the band empty. */
    BaRun(0, 1, 0, 0, 0, 2, nSeed);
    BaDrawEnv();
    g_pBaPanel->nLineCount = g_nBaLimit;
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_OK, "");
    BiClear(pPix);
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(BiInkAny(pPix, 100, 292, 140, 306), 0, "an empty caption leaves the band clear");
    CheckEqI32(g_bBaConfirm, 1, "  while the confirm itself still runs");
    PutTextRec(g_aXfText, PUFO_BASEAT_TEXT_OK, "OK");

    /* ---- the pad blit ---- */
    /* With no palette loaded for the pad's index slot the blit paints nothing, so the pad is pinned by
     * what it LOADED as: the archive record's own dimensions and header bytes. */
    BaRun(0, 1, 0, 0, 0, 2, nSeed);
    BaDrawEnv();
    BiClear(pPix);
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(BiInkAny(pPix, 0, 0, 239, 319), 143, "the frame without the pad still shows row 0");
    BaRun(0, 1, 0, 0, 0, 2, nSeed);
    BaDrawEnv();
    g_baDraw.ppPad = &g_pBaPad;
    BiClear(pPix);
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(BiInkAny(pPix, 0, 0, 239, 319), 143,
               "  and the pad blit adds nothing to it: the same 143 pixels");
    Check(g_pBaPad->pData != NULL, "the baseat record loads from main.dat");
    CheckEqI32(g_pBaPad->uWidth, 240, "  as a 240 wide record");
    CheckEqI32(g_pBaPad->uHeight, 320, "  and 320 tall");
    Check(g_pBaPad->pData != NULL && ((const u8 *)g_pBaPad->pData)[4] == 35
          && ((const u8 *)g_pBaPad->pData)[5] == 35,
          "  with the first data bytes 35,35: baseinfo has 43,83, soldier 12,12 and emptyb 81,82");

    /* ---- guards ---- */
    BaseAtScreen_Draw(NULL);
    Check(g_pBaPanel != NULL, "a null environment is refused");
    g_baDraw.ppPanel = NULL;
    g_baDraw.pnLineLimit = NULL;
    g_bBaConfirm = 1;
    BiClear(pPix);
    BaseAtScreen_Draw(&g_baDraw);
    CheckEqI32(g_bBaConfirm, 1, "with no panel the confirm gate cannot run");
    CheckEqI32(g_nBaBlitCalls, 0, "  and no pad is blitted");
}

static i32 g_nBaCloseScreen;
static i32 g_nBaCloseFail;

static void TestBaseAtCloseThunk(void)
{
    static PtrArray list;
    static GameTime clock;
    static GeoscapeCloseEnv closeEnv;
    BaseAtCloseEnv env;
    StrategItem *pItem0;
    StrategItem *pItem1;

    printf("the base-attack screen's close\n");

    /* Screen 13 has no case in the dispatcher's switch, so the head is all that runs: the action-fail
     * dword it clears is the proof that the thunk reached Geoscape_CloseUI. */
    memset(&env, 0, sizeof(env));
    memset(&closeEnv, 0, sizeof(closeEnv));
    g_nBaCloseScreen = 13;
    g_nBaCloseFail = 1;
    closeEnv.pnScreenId = &g_nBaCloseScreen;
    closeEnv.pnActionFail = &g_nBaCloseFail;

    /* The list OWNS its items: PtrArray_DeleteAt @ 0x82bac frees the element it removes. So the items
     * have to come from StrategItem_AllocAdd (operator new(0x88) at 0x415d8), not from statics. */
    PtrArray_ctor(&list);
    Check(StrategItem_AllocAdd(&list, 100), "the list holds the first item");
    Check(StrategItem_AllocAdd(&list, 200), "and the second");
    pItem0 = (StrategItem *)PtrArray_Get(&list, 0);
    pItem1 = (StrategItem *)PtrArray_Get(&list, 1);
    Check(pItem0 != NULL && pItem1 != NULL && pItem0 != pItem1, "  as two distinct records");

    clock.bPaused = 1;
    env.pbMissionFollows = &g_bBaResult;
    env.pStrategList = &list;
    env.pnStrategIdx = &g_nBaLimit;
    env.pTime = &clock;
    env.pClose = &closeEnv;

    /* ---- the aliens landed: the contact stays on the map and the game stays paused ---- */
    g_bBaResult = 1;
    g_nBaLimit = 0;
    g_nBaCloseFail = 1;
    clock.bPaused = 1;
    /* BaseAtScreen_CloseThunk @ 0x577c0: 72 B = 14 instructions + 4 pool words (re-walked 2026-10-23 item by item:
     * PUSH, LDR, LDRB, ANDS, BNE, LDR, LDR, BL, LDR, MOV, STRB, LDR, POP, B; pool 0x1864F8 0x159618 0x1863E8
     * 0x158840 -- an earlier note here said 17 instructions, which is 12 bytes too many). The only test is
     * `LDRB bBaseAtMissionFollows` + `ANDS` + `BNE 0x577EC` -- with the follow-up byte up the handler skips
     * BOTH the strateg-list removal (0x577D4) and the clock resume (0x577E0..0x577E8), and both arms converge
     * on the tail `B Geoscape_CloseUI` at 0x577F4. */
    BaseAtScreen_CloseThunk(&env);
    CheckEqI32(PtrArray_GetCount(&list), 2, OptCite("with the follow-up flag set the contact is not removed",
                                                    "@ 0x577c0"));
    CheckEqI32(clock.bPaused, 1, OptCite("  and the game stays paused for the ground mission", "@ 0x577c0"));
    CheckEqI32(g_nBaCloseFail, 0, OptCite("the dispatcher still ran, so the close happened either way",
                                          "@ 0x577c0"));

    /* ---- the craft was destroyed: the item goes and the game resumes ---- */
    g_bBaResult = 0;
    g_nBaLimit = 0;
    g_nBaCloseFail = 1;
    clock.bPaused = 1;
    BaseAtScreen_CloseThunk(&env);
    CheckEqI32(PtrArray_GetCount(&list), 1, OptCite("with the flag clear the contact is removed (0x577D4)",
                                                    "@ 0x577c0"));
    Check(PtrArray_Get(&list, 0) == pItem1, OptCite("  nBaseAtStrategIdx says which one", "@ 0x577c0"));
    CheckEqI32(clock.bPaused, 0, OptCite("and the pause byte is cleared (0x577E8)", "@ 0x577c0"));
    CheckEqI32(g_nBaCloseFail, 0, OptCite("  on the same tail call as the other arm (0x577F4)", "@ 0x577c0"));

    /* ---- the index is the one the opener stored ---- */
    PtrArray_DeleteAll(&list);
    (void)StrategItem_AllocAdd(&list, 300);
    (void)StrategItem_AllocAdd(&list, 400);
    (void)StrategItem_AllocAdd(&list, 500);
    pItem0 = (StrategItem *)PtrArray_Get(&list, 0);
    pItem1 = (StrategItem *)PtrArray_Get(&list, 2);
    g_bBaResult = 0;
    g_nBaLimit = 1;
    clock.bPaused = 1;
    BaseAtScreen_CloseThunk(&env);
    CheckEqI32(PtrArray_GetCount(&list), 2, "removing the stored index takes exactly that item");
    Check(PtrArray_Get(&list, 0) == pItem0, "  leaving the first");
    Check(PtrArray_Get(&list, 1) == pItem1, "  and the third in its place");

    /* ---- the null guard ---- */
    clock.bPaused = 1;
    g_bBaResult = 0;
    g_nBaLimit = 0;
    BaseAtScreen_CloseThunk(NULL);
    CheckEqI32(PtrArray_GetCount(&list), 2, "a null close environment removes nothing");
    CheckEqI32(clock.bPaused, 1, "  and clears no pause byte");

    PtrArray_DeleteAll(&list);
}

/* -------------------------------------------------------------------------- *
 * BasePlaneScreen_Draw @ 0x7d374: the hangar frame, the craft readout, the   *
 * rails. The readout is checked against a hand-painted frame, so the exact   *
 * value strings, their positions and the inner frame geometry are all pinned.*
 * -------------------------------------------------------------------------- */

static SoftSurf g_plSurf;
static SoftSurf g_plRef;
static PlaneDrawEnv g_plEnv;
static GfxColorEnv g_plColor;
static UiPanel g_plCraftPanel;
static UiPanel g_plStatusPanel;
static ObjectRecFull g_aPlObj[64];
static ObjectRecFull *g_apPlObj[64];
static Soldier g_aPlSol[6];
static Soldier *g_apPlSol[6];
static Ship g_aPlCraft[4];
static Ship *g_apPlCraft[4];
static u8 g_bPlDetail, g_bPlBuilt, g_bPlPreview;
static i32 g_nPlCursorY, g_nPlSelIdx, g_nPlSoldiers, g_nPlCrafts;

/* The readout window: the three labels, the three values and the inner frame, and nothing else. The
 * panels sit at y 110 and 210, the outer rails at x 5 / 234 and y 5 / 315, the inner rails' rows at
 * y 15 and 85 and their columns at x 15 and 224 -- so the window x 14..229 / y 14..99 holds exactly
 * the readout plus the inner frame's edges. */
#define PL_BAND_X0 14
#define PL_BAND_Y0 14
#define PL_BAND_X1 229
#define PL_BAND_Y1 99
/* The label-only window: clear of the inner frame (x 15 / 224, y 15 / 85) and of the values at
 * x 190 / 195, so ink here can only be one of the three readout captions. */
#define PL_LBL_X0 20
#define PL_LBL_Y0 20
#define PL_LBL_X1 120
#define PL_LBL_Y1 80
/* The caption window, below the inner frame's bottom row at y 85. */
#define PL_CAP_X0 20
#define PL_CAP_Y0 88
#define PL_CAP_X1 229
#define PL_CAP_Y1 102
/* The two panel windows. */
#define PL_PANEL_X0 8
#define PL_PANEL_X1 60
#define PL_PANEL_Y0 108
#define PL_PANEL_Y1 260

static i32 PlInk(const u16 *pPix, i32 x0, i32 y0, i32 x1, i32 y1, u16 uWant)
{
    i32 nInk = 0;

    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            if (pPix[y * FRAME_W + x] == uWant) {
                ++nInk;
            }
        }
    }
    return nInk;
}

static i32 PlInkAll(const u16 *pPix)
{
    i32 nInk = 0;

    for (i32 i = 0; i < FRAME_PIX; ++i) {
        if (pPix[i] != 0) {
            ++nInk;
        }
    }
    return nInk;
}

static void PlClear(void)
{
    memset(g_plSurf.pPixels, 0, (size_t)FRAME_PIX * 2u);
}

/* The inner frame, painted from the ARM rather than from the port's macros: rail C runs a climbing x
 * from 15 against a descending x from 220 in steps of 5 while the descending one stays above 115
 * (0x7D840..0x7D8FC), 5x1 at y 15 and 85, colour 5*(n+20); rail D runs m from 0 down to above -36 in
 * steps of 2 (0x7D8FC..0x7D9B4), 1x2 at x 15 and 224, y 2*v+15 and m+83, colour 5*(v+20). The
 * literals are the expectation, so a change on either side shows up as a difference. */
static void PlPaintInnerRails(void)
{
    for (i32 n = 0, x1 = 15, x2 = 220; x2 > 115; x1 += 5, x2 -= 5, ++n) {
        const u8 c = (u8)(5 * (n + 20));

        SoftSurf_BlitRectProxy(0, x1, 15, 5, 1, c, c, c);
        SoftSurf_BlitRectProxy(0, x1, 85, 5, 1, c, c, c);
        SoftSurf_BlitRectProxy(0, x2, 15, 5, 1, c, c, c);
        SoftSurf_BlitRectProxy(0, x2, 85, 5, 1, c, c, c);
    }
    for (i32 m = 0, v = 0; m > -36; m -= 2, ++v) {
        const u8 c = (u8)(5 * (v + 20));

        SoftSurf_BlitRectProxy(0, 15, 2 * v + 15, 1, 2, c, c, c);
        SoftSurf_BlitRectProxy(0, 15, m + 83, 1, 2, c, c, c);
        SoftSurf_BlitRectProxy(0, 224, 2 * v + 15, 1, 2, c, c, c);
        SoftSurf_BlitRectProxy(0, 224, m + 83, 1, 2, c, c, c);
    }
}

/* The frame the draw should produce, painted in the original's order: the readout (when armed), the
 * two captions, then the shared outer rails, then the inner frame. */
static void PlPaintRef(bool bReadout, const char *pszArm, const char *pszCrew, const char *pszQty)
{
    memset(g_plRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_plRef;

    if (bReadout) {
        (void)Gfx_PackRgb565Pair(&g_plColor, 250, 250, 250, 20, 25, 20);
        Text_DrawString(25, 23, "ARM");
        Text_DrawString(25, 43, "CREW");
        Text_DrawString(25, 63, "QTY");
        Text_DrawString(190, 23, pszArm);
        Text_DrawString(195, 43, pszCrew);
        Text_DrawString(195, 63, pszQty);
    }
    (void)Gfx_PackRgb565Pair(&g_plColor, 250, 250, 250, 10, 10, 25);
    Text_DrawString(25, 90, "SHIP:");
    Text_DrawString(165, 90, "STATUS");
    BaseUi_DrawFrameRails();
    PlPaintInnerRails();
    g_pMainSurface = &g_plSurf;
}

static bool PlBandSame(const u16 *pa, const u16 *pb)
{
    for (i32 y = PL_BAND_Y0; y <= PL_BAND_Y1; ++y) {
        for (i32 x = PL_BAND_X0; x <= PL_BAND_X1; ++x) {
            if (pa[y * FRAME_W + x] != pb[y * FRAME_W + x]) {
                return false;
            }
        }
    }
    return true;
}

static void PlFixture(void)
{
    memset(&g_plEnv, 0, sizeof(g_plEnv));
    memset(g_aPlObj, 0, sizeof(g_aPlObj));
    memset(g_aPlSol, 0, sizeof(g_aPlSol));
    memset(g_aPlCraft, 0, sizeof(g_aPlCraft));
    memset(g_aFrameText, 0, sizeof(g_aFrameText));

    for (i32 i = 0; i < 64; ++i) {
        g_apPlObj[i] = &g_aPlObj[i];
    }
    /* Craft 2 carries these. The two bands are 25..30 and 31..35; 24, 36 and 40 are decoys that must
     * stay out of both sums. */
    g_aPlObj[24].aItemQty[2] = 5;
    g_aPlObj[25].aItemQty[2] = 3;
    g_aPlObj[26].aItemQty[2] = 4;
    g_aPlObj[28].aItemQty[2] = 2;
    g_aPlObj[29].aItemQty[2] = 1;
    g_aPlObj[30].aItemQty[2] = 6;  /* armament total 16 */
    g_aPlObj[31].aItemQty[2] = 7;
    g_aPlObj[32].aItemQty[2] = 5;
    g_aPlObj[35].aItemQty[2] = 1;  /* crew total 13 */
    g_aPlObj[36].aItemQty[2] = 9;
    g_aPlObj[40].aItemQty[2] = 11;

    for (i32 i = 0; i < 6; ++i) {
        g_apPlSol[i] = &g_aPlSol[i];
    }
    g_aPlSol[0].bUnk183 = 2;
    g_aPlSol[1].bUnk183 = 255;
    g_aPlSol[2].bUnk183 = 1;
    g_aPlSol[3].bUnk183 = 2;
    g_aPlSol[4].bUnk183 = 2;  /* three aboard within the count of 5 */
    g_aPlSol[5].bUnk183 = 2;  /* a fourth, past the count */

    for (i32 i = 0; i < 4; ++i) {
        g_apPlCraft[i] = &g_aPlCraft[i];
    }
    g_aPlCraft[2].bCrewSlots = 9;

    /* The five record numbers are literals here on purpose: a test that places its text with the same
     * macro the port reads cannot see the macro move. */
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * 146, "ARM");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * 147, "CREW");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * 148, "QTY");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * 124, "SHIP:");
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * 153, "STATUS");

    g_bPlDetail = 0;
    g_bPlBuilt = 1;
    g_bPlPreview = 1;
    g_nPlCursorY = 120;   /* inside the status panel's band 110..190 */
    g_nPlSelIdx = 2;
    g_nPlSoldiers = 5;
    g_nPlCrafts = 4;

    g_plEnv.pColor = &g_plColor;
    g_plEnv.pbShowCraftDetail = &g_bPlDetail;
    g_plEnv.pbPanelsBuilt = &g_bPlBuilt;
    g_plEnv.pbPreviewActive = &g_bPlPreview;
    g_plEnv.pnCursorY = &g_nPlCursorY;
    g_plEnv.pCraftPanel = &g_plCraftPanel;
    g_plEnv.pStatusPanel = &g_plStatusPanel;
    g_plEnv.apCraft = g_apPlCraft;
    g_plEnv.nCraftCount = g_nPlCrafts;
    g_plEnv.pnSelCraftIdx = &g_nPlSelIdx;
    g_plEnv.apObjects = g_apPlObj;
    g_plEnv.nObjectCount = 64;
    g_plEnv.apSoldiers = g_apPlSol;
    g_plEnv.nSoldierCount = g_nPlSoldiers;
    g_plEnv.pTextRecs = (const char *)g_aFrameText;
    g_plEnv.pDetail = NULL; /* the delegation itself is checked with a live detail env below */
}

static void PlBuildPanels(void)
{
    (void)UiPanel_ctor(&g_plCraftPanel, 3);
    g_plCraftPanel.nPosX = 10;
    g_plCraftPanel.nPosY = 210;
    g_plCraftPanel.nStyle = 3;
    g_plCraftPanel.nFont = 16;
    g_plCraftPanel.nUnk7076 = 0;
    for (i32 r = 0; r < 3; ++r) {
        (void)UiPanel_SetLineText(&g_plCraftPanel, r, "Row");
    }

    (void)UiPanel_ctor(&g_plStatusPanel, 5);
    g_plStatusPanel.nPosX = 10;
    g_plStatusPanel.nPosY = 110;   /* the band the cursor gate reads: 110 .. 110 + 16*5 */
    g_plStatusPanel.nStyle = 5;
    g_plStatusPanel.nFont = 16;
    g_plStatusPanel.nUnk7076 = 0;
    for (i32 r = 0; r < 5; ++r) {
        (void)UiPanel_SetLineText(&g_plStatusPanel, r, "Row");
    }
}

static void TestPlaneScreenDraw(void)
{
    const u16 *pPix;
    u16 uArmGlyph, uCapGlyph, uPanelGlyph;
    i32 nRet;

    printf("BasePlaneScreen_Draw @ 0x7d374\n");

    FrameLoadFont();
    if (!g_bFrameFontReady) {
        printf("  FAIL lang.dat / main.dat did not open, so the frame cannot be checked\n");
        ++g_nFailures;
        return;
    }

    SoftSurf_ctor(&g_plSurf);
    (void)SoftSurf_AllocPixels(&g_plSurf, FRAME_W, FRAME_H, PIXMODE_16BPP);
    SoftSurf_ctor(&g_plRef);
    (void)SoftSurf_AllocPixels(&g_plRef, FRAME_W, FRAME_H, PIXMODE_16BPP);
    g_pMainSurface = &g_plSurf;
    g_blitState.nPixelMode = 2;
    pPix = (const u16 *)g_plSurf.pPixels;

    g_plColor.pnGamma = &g_nGammaZero;
    g_plColor.nFadeCount = 0;
    PlBuildPanels();

    /* The three glyph colours the test counts. Gfx_PackRgb565Pair @ 0x2d82c writes the (r2,g2,b2)
     * half into palette slot 2 entry 27 and Text_DrawString @ 0x2d7bc forces the glyph base to 2, so
     * each packed pair is exactly the colour its following strings draw in. */
    uPanelGlyph = (u16)Gfx_PackRgb565Pair(&g_plColor, 0, 0, 0, 200, 30, 200); /* the panels' colour */
    uArmGlyph = (u16)Gfx_PackRgb565Pair(&g_plColor, 250, 250, 250, 20, 25, 20);
    uCapGlyph = (u16)Gfx_PackRgb565Pair(&g_plColor, 250, 250, 250, 10, 10, 25);
    Check(uPanelGlyph != uArmGlyph && uArmGlyph != uCapGlyph && uPanelGlyph != uCapGlyph,
          "the three colour pairs pack to three distinct glyph colours");

    /* 1. The detail arm: the whole call goes to the detail view and nothing of this function runs.
     * 0x7D37C is a straight call, so with no detail env there is nothing to call and the return is 0.
     * The delegation with a live detail env is checked in TestPlaneCraftDetail. */
    PlFixture();
    g_bPlDetail = 1;
    PlClear();
    nRet = BasePlaneScreen_Draw(&g_plEnv);
    CheckEqI32(nRet, 0, "bPlaneShowCraftDetail @ 0x161F20 set hands the whole call to the detail view (0x7D37C)");
    CheckEqI32(PlInkAll(pPix), 0, "  and nothing of this function's own drawing runs -- no captions, no rails");

    /* 2. The built flag: the panels go, the captions and the rails stay. */
    PlFixture();
    g_bPlBuilt = 0;
    PlClear();
    (void)Gfx_PackRgb565Pair(&g_plColor, 0, 0, 0, 200, 30, 200); /* what the panels would draw in */
    (void)BasePlaneScreen_Draw(&g_plEnv);
    CheckEqI32(PlInk(pPix, PL_PANEL_X0, PL_PANEL_Y0, PL_PANEL_X1, PL_PANEL_Y1, uPanelGlyph), 0,
               "neither list panel draws while bPlanePanelsBuilt @ 0x161F08 is clear (0x7D398)");
    CheckEqI32(PlInk(pPix, PL_LBL_X0, PL_LBL_Y0, PL_LBL_X1, PL_LBL_Y1, uArmGlyph), 0,
               "  and the craft readout stays off with it");
    Check(PlInk(pPix, PL_CAP_X0, PL_CAP_Y0, PL_CAP_X1, PL_CAP_Y1, uCapGlyph) > 0,
          "  yet the two captions still draw (both skips land on 0x7D640)");
    CheckEqI32((i32)pPix[2 * FRAME_W + 5], XfPix(100, 100, 100), "  and so do the shared outer rails");
    CheckEqI32((i32)pPix[15 * FRAME_W + 15], XfPix(100, 100, 100), "  and the inner frame still closes");

    /* 3. Built: both panels reach the surface. */
    PlFixture();
    PlClear();
    (void)Gfx_PackRgb565Pair(&g_plColor, 0, 0, 0, 200, 30, 200);
    (void)BasePlaneScreen_Draw(&g_plEnv);
    Check(PlInk(pPix, PL_PANEL_X0, PL_PANEL_Y0, PL_PANEL_X1, PL_PANEL_Y1, uPanelGlyph) > 0,
          "with the flag set both list panels draw (0x7D3B4 / 0x7D3C4)");
    Check(PlInk(pPix, PL_LBL_X0, PL_LBL_Y0, PL_LBL_X1, PL_LBL_Y1, uArmGlyph) > 0,
          "and the readout runs with the cursor inside the status panel's band (0x7D3E0..0x7D414)");

    /* 4. The band gate, edge by edge. The status panel is nPosY 110 with 5 rows, so the band is
     * 110 .. 190 and both ends are exclusive. */
    for (i32 i = 0; i < 5; ++i) {
        static const i32 anCursor[5] = { 109, 110, 111, 189, 190 };
        static const bool abWant[5] = { false, false, true, true, false };

        PlFixture();
        g_nPlCursorY = anCursor[i];
        PlClear();
        (void)BasePlaneScreen_Draw(&g_plEnv);
        CheckEqI32(PlInk(pPix, PL_LBL_X0, PL_LBL_Y0, PL_LBL_X1, PL_LBL_Y1, uArmGlyph) > 0 ? 1 : 0,
                   abWant[i] ? 1 : 0,
                   "the readout band is strictly between nPosY and nPosY + 16 * nLineCount");
        Check(PlInk(pPix, PL_CAP_X0, PL_CAP_Y0, PL_CAP_X1, PL_CAP_Y1, uCapGlyph) > 0,
              "  whatever the gate says, the two captions draw");
    }

    PlFixture();
    g_nPlCursorY = -5; /* nSmapCursorY is read LDRSH, so a negative row is a real value */
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    CheckEqI32(PlInk(pPix, PL_LBL_X0, PL_LBL_Y0, PL_LBL_X1, PL_LBL_Y1, uArmGlyph), 0,
               "a negative cursor row (LDRSH at 0x7D3EC) keeps the readout off");

    PlFixture();
    g_bPlPreview = 0;
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    CheckEqI32(PlInk(pPix, PL_LBL_X0, PL_LBL_Y0, PL_LBL_X1, PL_LBL_Y1, uArmGlyph), 0,
               "bBaseFacPreviewActive @ 0x18652C clear keeps the readout off (0x7D3D0)");
    Check(PlInk(pPix, PL_CAP_X0, PL_CAP_Y0, PL_CAP_X1, PL_CAP_Y1, uCapGlyph) > 0,
          "  and the captions are not gated by it");

    /* 5. The whole readout window against a hand-painted frame. This pins the three labels, the three
     * value strings and their positions, and the inner frame's geometry, all at once. */
    PlFixture();
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    PlPaintRef(true, "16\\9", "3", "13");
    Check(PlBandSame(pPix, (const u16 *)g_plRef.pPixels),
          "the readout window matches a hand-painted frame: 16\\9 at (190,23), 3 at (195,43), 13 at (195,63)");

    /* 6. The two slot bands. The decoys at 24, 36 and 40 must stay out; a quantity at 27 must count. */
    PlFixture();
    g_aPlObj[24].aItemQty[2] = 40;
    g_aPlObj[36].aItemQty[2] = 40;
    g_aPlObj[40].aItemQty[2] = 40;
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    PlPaintRef(true, "16\\9", "3", "13");
    Check(PlBandSame(pPix, (const u16 *)g_plRef.pPixels),
          "slots 24, 36 and 40 stay out of both sums (0x7D45C / 0x7D464, 0x7D4A4 / 0x7D4AC)");

    PlFixture();
    g_aPlObj[27].aItemQty[2] = 4; /* inside 25..30, and it was the only empty one */
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    PlPaintRef(true, "20\\9", "3", "13");
    Check(PlBandSame(pPix, (const u16 *)g_plRef.pPixels),
          "slot 27 counts towards the armament total, 16 becomes 20");

    PlFixture();
    g_aPlObj[33].aItemQty[2] = 6; /* inside 31..35 */
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    PlPaintRef(true, "16\\9", "3", "19");
    Check(PlBandSame(pPix, (const u16 *)g_plRef.pPixels),
          "slot 33 counts towards the crew total, 13 becomes 19");

    /* 7. The value string is itoa(sum) + the backslash at asc_AB9F0 + itoa(Ship.bCrewSlots @ +0x14). */
    PlFixture();
    g_aPlCraft[2].bCrewSlots = 120;
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    PlPaintRef(true, "16\\120", "3", "13");
    Check(PlBandSame(pPix, (const u16 *)g_plRef.pPixels),
          "the armament value is the sum, a backslash, then Ship.bCrewSlots (0x7D5A8 / 0x7D5D0)");

    /* 8. The soldier count is bounded by nSoldierCount and keyed on Soldier.bUnk183 @ +0xB7. */
    PlFixture();
    g_plEnv.nSoldierCount = 6; /* the sixth soldier is assigned too, and now he counts */
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    PlPaintRef(true, "16\\9", "4", "13");
    Check(PlBandSame(pPix, (const u16 *)g_plRef.pPixels),
          "the crew value is the soldiers with bUnk183 == the craft index, bounded by nSoldierCount (0x7D4C4)");

    PlFixture();
    g_aPlSol[1].bUnk183 = 2; /* 255 is the unassigned sentinel; making it a match must show up */
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    PlPaintRef(true, "16\\9", "4", "13");
    Check(PlBandSame(pPix, (const u16 *)g_plRef.pPixels),
          "  and the sentinel 255 is only unassigned because it is not this craft's index");

    /* 9. A craft index past the craft array and past aItemQty's 52 slots: the original reads whatever
     * is there, the port resolves it to nothing. */
    PlFixture();
    g_nPlSelIdx = 60;
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    PlPaintRef(true, "0\\0", "0", "0");
    Check(PlBandSame(pPix, (const u16 *)g_plRef.pPixels),
          "a craft index past the array and past aItemQty's 52 slots reads as nothing (port bound)");

    /* 10. The inner frame's own geometry, probed at the corners the two rails meet. */
    PlFixture();
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    CheckEqI32((i32)pPix[15 * FRAME_W + 15], XfPix(100, 100, 100), "rail C starts at (15,15) with index 100 (0x7D840)");
    CheckEqI32((i32)pPix[15 * FRAME_W + 19], XfPix(100, 100, 100), "  its first block is 5 wide");
    CheckEqI32((i32)pPix[15 * FRAME_W + 20], XfPix(105, 105, 105), "  and the next block is one step up");
    CheckEqI32((i32)pPix[15 * FRAME_W + 124], XfPix(200, 200, 200), "  the descending x ends at 120..124 with 200");
    CheckEqI32((i32)pPix[15 * FRAME_W + 125], XfPix(195, 195, 195),
               "  its two x runs are continuous: 125..129 is the previous step, 195");
    CheckEqI32((i32)pPix[15 * FRAME_W + 224], XfPix(100, 100, 100), "  the descending x starts at 220..224 with 100");
    CheckEqI32((i32)pPix[15 * FRAME_W + 225], 0, "  and nothing is drawn past x 224");
    CheckEqI32((i32)pPix[85 * FRAME_W + 15], XfPix(100, 100, 100), "rail C mirrors the same ramp at y 85");
    CheckEqI32((i32)pPix[86 * FRAME_W + 15], 0, "  and its rows are one pixel tall");
    CheckEqI32((i32)pPix[47 * FRAME_W + 15], XfPix(180, 180, 180), "rail D's climbing row reaches y 47 with 180 (0x7D8FC)");
    CheckEqI32((i32)pPix[48 * FRAME_W + 15], XfPix(180, 180, 180), "  its blocks are 2 tall");
    CheckEqI32((i32)pPix[49 * FRAME_W + 15], XfPix(185, 185, 185),
               "  the bound is tested after the step, so m = -34 / v = 17 draws too: y 49..50 with 185");
    CheckEqI32((i32)pPix[50 * FRAME_W + 15], XfPix(185, 185, 185), "  and the two halves meet there, not at 47");
    CheckEqI32((i32)pPix[51 * FRAME_W + 15], XfPix(180, 180, 180),
               "  the descending half runs 51..52 with its own 180, so the column is continuous 15..84");
    CheckEqI32((i32)pPix[84 * FRAME_W + 224], XfPix(100, 100, 100),
               "rail D's descending row starts at m + 83, so y 83..84 carry its first index 100");
    CheckEqI32((i32)pPix[14 * FRAME_W + 15], 0, "the inner frame starts at y 15 and x 15, not earlier");
    CheckEqI32((i32)pPix[15 * FRAME_W + 14], 0, "  and not earlier in x either");

    /* 11. The two colour pairs are packed in the header's order, so each caption draws in its own. */
    PlFixture();
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    Check(PlInk(pPix, PL_LBL_X0, PL_LBL_Y0, PL_LBL_X1, PL_LBL_Y1, uArmGlyph) > 0,
          "the three readout captions draw in the (20,25,20) pair (0x7D504)");
    CheckEqI32(PlInk(pPix, PL_LBL_X0, PL_LBL_Y0, PL_LBL_X1, PL_LBL_Y1, uCapGlyph), 0,
          "  and not in the caption pair's (10,10,25)");
    Check(PlInk(pPix, PL_CAP_X0, PL_CAP_Y0, PL_CAP_X1, PL_CAP_Y1, uCapGlyph) > 0,
          "the two captions draw in the (10,10,25) pair (0x7D640)");
    CheckEqI32(PlInk(pPix, PL_CAP_X0, PL_CAP_Y0, PL_CAP_X1, PL_CAP_Y1, uArmGlyph), 0,
          "  and not in the readout pair's");

    /* 12. The null guards. */
    CheckEqI32(BasePlaneScreen_Draw(NULL), 0, "a null draw environment draws nothing");

    PlFixture();
    g_plEnv.pCraftPanel = NULL;
    g_plEnv.pStatusPanel = NULL;
    g_plEnv.apObjects = NULL;
    g_plEnv.apSoldiers = NULL;
    g_plEnv.apCraft = NULL;
    g_plEnv.pTextRecs = NULL;
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    CheckEqI32((i32)pPix[15 * FRAME_W + 15], XfPix(100, 100, 100),
               "a hangar with no panels, no objects, no soldiers and no craft still frames itself");
    CheckEqI32(PlInk(pPix, PL_LBL_X0, PL_LBL_Y0, PL_LBL_X1, PL_LBL_Y1, uArmGlyph), 0,
               "  the readout runs but has nothing to sum, so no caption reaches the surface");

    PlFixture();
    g_plEnv.pbPanelsBuilt = NULL;
    g_plEnv.pbShowCraftDetail = NULL;
    g_plEnv.pbPreviewActive = NULL;
    g_plEnv.pnCursorY = NULL;
    PlClear();
    (void)BasePlaneScreen_Draw(&g_plEnv);
    CheckEqI32((i32)pPix[15 * FRAME_W + 15], XfPix(100, 100, 100),
               "a missing flag byte never takes the detail arm -- the inner rails still run");
    CheckEqI32(PlInk(pPix, PL_LBL_X0, PL_LBL_Y0, PL_LBL_X1, PL_LBL_Y1, uArmGlyph), 0,
               "  and a missing built flag draws no panels");
    Check(PlInk(pPix, PL_CAP_X0, PL_CAP_Y0, PL_CAP_X1, PL_CAP_Y1, uCapGlyph) > 0,
          "  while the captions and the rails keep running");
}

/* ------------------------------------------------- unit 97: the hangar's craft-detail screen
 *
 * BasePlaneScreen_DrawCraftDetail @ 0x7baf8, both arms. The fixture drives the stores arm and the
 * detail arm over one 240x320 surface, and each arm is compared against a reference frame painted
 * from the ARM with literals. */

static SoftSurf g_cdSurf;
static SoftSurf g_cdRef;
static PlaneDetailEnv g_cdEnv;
static GfxColorEnv g_cdColor;
static BasePlaneScreen g_cdScreen;
static PadImage g_aCdCraftPad[5];
static PadImage g_aCdWeaponPad[6];
static PadImage g_cdCtlPad;
static UiControl g_cdOkCtl;
static UiPanel g_cdSlotPanel, g_cdAvailPanel, g_cdSelRowPanel, g_cdCommitPanel, g_cdTakenPanel;
static UiPanel g_cdCrewPanel, g_cdCrewStatusPanel;
static Ship g_aCdCraft[4];
static Ship *g_apCdCraft[4];
static ObjectRecFull g_aCdObj[64];
static ObjectRecFull *g_apCdObj[64];
static Soldier g_aCdSol[6];
static Soldier *g_apCdSol[6];
static UiAnimBlitEnv g_cdAnim;
static u8 g_bCdAnimStarted;
static i8 g_nCdAnimProgress;
static PlaneWeaponPanels g_cdStatPanels;
static BasePlaneDetailEnv g_cdStatEnv;
static i32 g_nCdStatRows;
static i32 g_nCdStatW[6];
static u8 g_bCdListBuilt, g_bCdStores, g_bCdPreviewOk, g_bCdPanelFlag, g_bCdShowDetail;
static u32 g_dwCdFail;
static i32 g_nCdReason, g_nCdCursorY, g_nCdRowCursor, g_nCdCraftIdx;
static i8 g_nCdBaseIdx;
static i32 g_nCdSoldiers, g_nCdAboard, g_nCdAtBase, g_nCdUnassigned, g_nCdCarried;
static i32 g_nCdCtlBlits;

/* A 2x1 glyph: index 27 (the palette entry Gfx_PackRgb565Pair writes) and index 0xFE, the key every
 * PadImage_BlitEx here passes. So each test pad blits exactly one visible pixel, in the colour of the
 * pair packed before it, and the key itself is pinned by the second pixel staying blank. */
static u8 g_aCdGlyph[6] = { 2, 0, 1, 0, 27, 0xFE };

/* The craft pads keep the nOriginX the fixture gives them; the weapon pads get theirs overwritten with
 * 55 or 156, so their nOriginY is what says which pad ran. */
#define CD_CRAFT_PAD_X(k) (100 + 3 * (k))
#define CD_CRAFT_PAD_Y 310
#define CD_WEAPON_PAD_Y(k) (270 + 3 * (k))
#define CD_SEP_COLOR XfPix(0xB4, 0xB4, 0xB4)

/* A pad or glyph pixel is Gfx_PackRgb565Pair's packed value, which divides each channel by
 * (0 * 0.1 + 0.7) before packing -- so the expected colour is the pack's return, not XfPix of the
 * triple. The rect blits take their three byte arguments straight through, so those keep XfPix. */
static u16 CdPadColor(void)
{
    return (u16)Gfx_PackRgb565Pair(&g_cdColor, 100, 150, 250, 10, 25, 10);
}

static u16 CdRefuseColor(void)
{
    return (u16)Gfx_PackRgb565Pair(&g_cdColor, 220, 80, 80, 50, 30, 30);
}

static void CdCtlBlit(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
    ++g_nCdCtlBlits;
}

static void CdText(i32 nRec, const char *psz)
{
    PutStr(g_aFrameText + PUFO_TEXT_REC_STRIDE * nRec, psz);
}

static void CdDrawCentered(i32 nCentre, i32 y, const char *psz)
{
    Text_DrawString(nCentre - Text_MeasureWidth(psz) / 2, y, psz);
}

static u16 CdPix(const u16 *pPix, i32 x, i32 y)
{
    return pPix[y * FRAME_W + x];
}

static i32 CdInk(const u16 *pPix, i32 x0, i32 y0, i32 x1, i32 y1, u16 uWant)
{
    i32 nInk = 0;

    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            if (pPix[y * FRAME_W + x] == uWant) {
                ++nInk;
            }
        }
    }
    return nInk;
}

/* Ink in a window that nothing else of this screen reaches -- the rails sit at x 5 / 234 and y 5 / 315,
 * so a window inside them counts only what the test is asking about. */
static i32 CdInkAny(const u16 *pPix, i32 x0, i32 y0, i32 x1, i32 y1)
{
    i32 nInk = 0;

    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            if (pPix[y * FRAME_W + x] != 0) {
                ++nInk;
            }
        }
    }
    return nInk;
}

static void CdClear(void)
{
    memset(g_cdSurf.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_cdSurf;
    g_nCdCtlBlits = 0;
    g_bCdAnimStarted = 0;
    g_nCdAnimProgress = 0;
}

static bool CdSame(void)
{
    const u16 *pa = (const u16 *)g_cdSurf.pPixels;
    const u16 *pb = (const u16 *)g_cdRef.pPixels;

    for (i32 i = 0; i < FRAME_PIX; ++i) {
        if (pa[i] != pb[i]) {
            return false;
        }
    }
    return true;
}

static void CdResetStorePanels(void);

static void CdFixture(void)
{
    memset(&g_cdEnv, 0, sizeof(g_cdEnv));
    memset(&g_cdScreen, 0, sizeof(g_cdScreen));
    memset(g_aCdCraftPad, 0, sizeof(g_aCdCraftPad));
    memset(g_aCdWeaponPad, 0, sizeof(g_aCdWeaponPad));
    memset(&g_cdCtlPad, 0, sizeof(g_cdCtlPad));
    memset(&g_cdOkCtl, 0, sizeof(g_cdOkCtl));
    memset(g_aCdCraft, 0, sizeof(g_aCdCraft));
    memset(g_aCdObj, 0, sizeof(g_aCdObj));
    memset(g_aCdSol, 0, sizeof(g_aCdSol));
    memset(&g_cdStatPanels, 0, sizeof(g_cdStatPanels));
    memset(&g_cdStatEnv, 0, sizeof(g_cdStatEnv));

    for (i32 i = 0; i < 5; ++i) {
        g_aCdCraftPad[i].pData = g_aCdGlyph;
        g_aCdCraftPad[i].nMode = 2;
        g_aCdCraftPad[i].nOriginX = CD_CRAFT_PAD_X(i);
        g_aCdCraftPad[i].nOriginY = CD_CRAFT_PAD_Y;
        g_cdScreen.apCraftPads[i] = &g_aCdCraftPad[i];
    }
    for (i32 i = 0; i < 6; ++i) {
        g_aCdWeaponPad[i].pData = g_aCdGlyph;
        g_aCdWeaponPad[i].nMode = 2;
        g_aCdWeaponPad[i].nOriginX = 0;
        g_aCdWeaponPad[i].nOriginY = CD_WEAPON_PAD_Y(i);
        g_cdScreen.apWeaponPads[i] = &g_aCdWeaponPad[i];
    }

    g_cdCtlPad.pData = g_aCdGlyph;
    g_cdCtlPad.nMode = 2;
    g_cdOkCtl.bEnabled = 1;
    g_cdOkCtl.nMode = 0;
    g_cdOkCtl.nState = 0;
    g_cdOkCtl.bUnk17 = 1;
    g_cdOkCtl.uWidth = 40;
    g_cdOkCtl.apPads[0] = &g_cdCtlPad;

    /* Craft 2 is the selected one: a type-1 hull (cargo cap 120) that can dogfight, 3000 of 30000
     * range left, 200 of 800 fuel, type ordinal 3, left mount 26 and right mount 25. */
    for (i32 i = 0; i < 4; ++i) {
        g_apCdCraft[i] = &g_aCdCraft[i];
        g_aCdCraft[i].bType = (u8)i;
        g_aCdCraft[i].bCraftType = 1;
        g_aCdCraft[i].bCanDogfight = 1;
        g_aCdCraft[i].uRangeRemain = 3000;
        g_aCdCraft[i].uRangeMax = 30000;
        g_aCdCraft[i].nFuel = 200;
        g_aCdCraft[i].uFuelMax = 800;
    }
    g_aCdCraft[2].bType = 3;
    g_aCdCraft[2].bWeaponL = 26;
    g_aCdCraft[2].bWeaponR = 25;

    for (i32 i = 0; i < 64; ++i) {
        g_apCdObj[i] = &g_aCdObj[i];
        (void)snprintf(g_aCdObj[i].szName, sizeof(g_aCdObj[i].szName), "Obj%02d", i);
    }
    g_aCdObj[31].aItemQty[2] = 7;  /* the crew slots this craft carries: 7 + 5 + 0 + 4 + 0 = 16 */
    g_aCdObj[32].aItemQty[2] = 5;
    g_aCdObj[34].aItemQty[2] = 4;
    g_aCdObj[30].aItemQty[2] = 9;  /* just below the crew band */
    g_aCdObj[36].aItemQty[2] = 11; /* just above it, and inside the hold's ammo band */
    g_aCdObj[36].bCargoSize = 1;
    g_aCdObj[52].aItemQty[2] = 13; /* just past the hold's ammo band */
    g_aCdObj[52].bCargoSize = 1;
    g_aCdObj[25].aItemQty[2] = 17; /* the weapon-slot band the hold leaves out */
    g_aCdObj[25].bCargoSize = 1;
    g_aCdObj[5].bCargoSize = 3;   /* 3 * 4 = 12 of the hold */
    g_aCdObj[5].aItemQty[2] = 4;
    g_aCdObj[40].bCargoSize = 2;  /* 2 * 5 = 10 more, so 22 used of 120 */
    g_aCdObj[40].aItemQty[2] = 5;
    g_aCdObj[60].bCargoSize = 7;  /* index 60 is outside both hold bands */
    g_aCdObj[60].aItemQty[2] = 9;

    /* Five soldiers inside the count: three in the shown base (index 0), one of them unassigned. */
    for (i32 i = 0; i < 6; ++i) {
        g_apCdSol[i] = &g_aCdSol[i];
    }
    g_aCdSol[0].nKind = 0; g_aCdSol[0].bUnk183 = 2;
    g_aCdSol[1].nKind = 1; g_aCdSol[1].bUnk183 = 2;
    g_aCdSol[2].nKind = 0; g_aCdSol[2].bUnk183 = 1;
    g_aCdSol[3].nKind = 0; g_aCdSol[3].bUnk183 = PUFO_SOLDIER_CRAFT_NONE;
    g_aCdSol[4].nKind = 0; g_aCdSol[4].bUnk183 = 2;
    g_aCdSol[5].nKind = 0; g_aCdSol[5].bUnk183 = PUFO_SOLDIER_CRAFT_ABOARD_OUT;

    /* The record numbers are literals here for the same reason unit 96's are: a fixture that places its
     * text with the macro the port reads cannot see the macro move. */
    CdText(89, "SHIP");
    CdText(155, "BASE");
    CdText(144, "FUEL ");
    CdText(145, "DMG ");
    CdText(403, "FREE ");
    CdText(404, "TOTAL ");
    CdText(408, "NOSTORE");
    CdText(409, "NOAMMO");
    CdText(410, "NOROOM");
    CdText(1127, "OK");
    CdText(137, "SKY");
    CdText(138, "LGT");
    CdText(139, "AVG");
    CdText(418, "INT");  /* types 3 and 4 put a dash between the label and the ordinal (asc_AB340) */
    CdText(419, "FIRE");
    CdText(141, "AAA");
    CdText(142, "BBBBBBBBBB");
    CdText(143, "CCCCCCCC");

    g_bCdListBuilt = 0;
    g_bCdStores = 0;
    g_bCdPreviewOk = 0;
    g_bCdPanelFlag = 0;
    g_dwCdFail = 0;
    g_nCdReason = 0;
    g_nCdCursorY = 100;   /* inside the stores band 75 .. 75 + 64 */
    g_nCdRowCursor = 1;   /* the slot panel's row 1 holds slot 32 */
    g_nCdCraftIdx = 2;
    g_nCdBaseIdx = 0;
    g_nCdSoldiers = 5;
    g_nCdAboard = 0;
    g_nCdAtBase = 0;
    g_nCdUnassigned = 0;
    g_nCdCarried = 0;
    g_nCdStatRows = 3;

    g_cdAnim.pbStarted = &g_bCdAnimStarted;
    g_cdAnim.pnProgress = &g_nCdAnimProgress;
    g_cdAnim.nFrameStyle = 0;

    g_cdColor.pnGamma = &g_nGammaZero;
    g_cdColor.nFadeCount = 0;

    g_cdEnv.pColor = &g_cdColor;
    g_cdEnv.pScreen = &g_cdScreen;
    g_cdEnv.apCraft = g_apCdCraft;
    g_cdEnv.nCraftCount = 4;
    g_cdEnv.pnCraftIdx = &g_nCdCraftIdx;
    g_cdEnv.pnBaseIdx = &g_nCdBaseIdx;
    g_cdEnv.pbListPanelsBuilt = &g_bCdListBuilt;
    g_cdEnv.pbStoresCommitted = &g_bCdStores;
    g_cdEnv.pdwActionFail = &g_dwCdFail;
    g_cdEnv.pnRefuseReason = &g_nCdReason;
    g_cdEnv.pbPanelFlagScratch = &g_bCdPanelFlag;
    g_cdEnv.pCrewPanel = &g_cdCrewPanel;
    g_cdEnv.pCrewStatusPanel = &g_cdCrewStatusPanel;
    g_cdEnv.pnRowCursor = &g_nCdRowCursor;
    g_cdEnv.pbPreviewOk = &g_bCdPreviewOk;
    g_cdEnv.pnCursorY = &g_nCdCursorY;
    g_cdEnv.pCtlConfirm = &g_cdOkCtl;
    g_cdEnv.pfnCtlBlit = CdCtlBlit;
    g_cdEnv.pfnCtlBlitEx = CdCtlBlit;
    g_cdEnv.pAnim = &g_cdAnim;
    g_cdEnv.apObjects = g_apCdObj;
    g_cdEnv.nObjectCount = 64;
    g_cdEnv.apSoldiers = g_apCdSol;
    g_cdEnv.nSoldierCount = g_nCdSoldiers;
    g_cdEnv.pnSoldiersAboard = &g_nCdAboard;
    g_cdEnv.pnSoldiersAtBase = &g_nCdAtBase;
    g_cdEnv.pnSoldiersUnassigned = &g_nCdUnassigned;
    g_cdEnv.pnCrewCarried = &g_nCdCarried;
    g_cdEnv.pStatPanels = &g_cdStatPanels;
    g_cdEnv.pnStatRowCount = &g_nCdStatRows;
    g_cdEnv.pTextRecs = (char *)g_aFrameText;
    g_cdEnv.pStatEnv = &g_cdStatEnv;

    g_cdStatEnv.apObjects = g_apCdObj;
    g_cdStatEnv.nObjectCount = 64;
    g_cdStatEnv.nBaseIdx = 0;
    g_cdStatEnv.pnCraftCannon = &g_nCdStatW[0];
    g_cdStatEnv.pnStingray = &g_nCdStatW[1];
    g_cdStatEnv.pnAvalanche = &g_nCdStatW[2];
    g_cdStatEnv.pnLaserCannon = &g_nCdStatW[3];
    g_cdStatEnv.pnPlasmaCannon = &g_nCdStatW[4];
    g_cdStatEnv.pnFusionBall = &g_nCdStatW[5];
    g_cdStatEnv.pnRowCount = &g_nCdStatRows;

    g_fontSys.nBaseBitsBase = 0; /* Geoscape_InitUI @ 0x85F78 fills it with the basebits group base; 0 here keeps the glyph ids literal */

    /* The two count lines call Font_DrawGlyph with nPal 0 while the glyph base is 0, so they read
     * palette slot 0. Left zeroed, every icon pixel would blit as 0 and the sprite id would be
     * unobservable. Entry 0 stays 0 -- Font_BlitGlyph treats it as transparency (the ARM at 0xa4288
     * tests *v5 != 0), the rest get distinct colours so a wrong sprite id shows up. */
    for (i32 i = 1; i < PUFO_PAL_SIZE; ++i) {
        g_aPalette[0][i] = (u32)(0x100 + i);
    }

    /* The pads' glyph is { 27, 0xFE }: 27 is the packed colour, 0xFE is the blit key. A wrong key
     * would blit 0xFE through slot 2 entry 254, which stays zero unless the fixture colours it --
     * and a zero pixel on a zeroed surface is invisible, so the key would be untestable. Literal
     * 0xFE here, not PUFO_PLANE_PAD_KEY: the test must not read the macro the code under test reads. */
    g_aPalette[2][0xFEu] = 0x2FEu;

    CdResetStorePanels();
}

/* The five stores panels, laid out the way BasePlaneScreen_BuildControls @ 0x7b014 lays them out. */
static void CdBuildStorePanels(void)
{
    UiPanel *const ap[5] = { &g_cdSlotPanel, &g_cdAvailPanel, &g_cdSelRowPanel,
                             &g_cdCommitPanel, &g_cdTakenPanel };

    for (i32 i = 0; i < 5; ++i) {
        (void)UiPanel_ctor(ap[i], 4);
    }
    for (i32 r = 0; r < 4; ++r) {
        (void)UiPanel_SetLineText(&g_cdSlotPanel, r, "Item");
        (void)UiPanel_SetLineText(&g_cdAvailPanel, r, "Av");
        (void)UiPanel_SetLineText(&g_cdSelRowPanel, r, "<");
        (void)UiPanel_SetLineText(&g_cdCommitPanel, r, ">");
        (void)UiPanel_SetLineText(&g_cdTakenPanel, r, "Tk");
    }
}

/* The fixture re-runs this after every CdFixture, because CdFixture zeroes the env and the panels carry
 * the flags the draw writes. */
static void CdResetStorePanels(void)
{
    static const i32 anX[5] = { 10, 164, 170, 186, 232 };
    UiPanel *const ap[5] = { &g_cdSlotPanel, &g_cdAvailPanel, &g_cdSelRowPanel,
                             &g_cdCommitPanel, &g_cdTakenPanel };

    for (i32 i = 0; i < 5; ++i) {
        ap[i]->nPosX = anX[i];
        ap[i]->nPosY = 70;
        ap[i]->nStyle = 14;
        ap[i]->nFont = 16;
        ap[i]->nUnk7076 = 0;
        ap[i]->bSelLineHighlight = 1;
        ap[i]->bRepeatArmed = 0;
        for (i32 r = 0; r < 4; ++r) {
            ap[i]->anLineAux[r] = 0;
        }
    }
    for (i32 r = 0; r < 4; ++r) {
        g_cdSlotPanel.anLineAux[r] = 31 + r;  /* the slot id, which is what the preview arm reads */
        g_cdAvailPanel.anLineAux[r] = 10 - r;
        g_cdTakenPanel.anLineAux[r] = 3;
    }

    g_cdEnv.pSlotPanel = &g_cdSlotPanel;
    g_cdEnv.pAvailPanel = &g_cdAvailPanel;
    g_cdEnv.pSelRowPanel = &g_cdSelRowPanel;
    g_cdEnv.pCommitPanel = &g_cdCommitPanel;
    g_cdEnv.pTakenPanel = &g_cdTakenPanel;
}

/* The stores arm's frame, painted in the original's order: rails, the two column captions, the slot
 * panel, then separator / panel interleaved, then the bottom line, then the name, then the red pair
 * and (when the transfer was refused) the reason line. */
static void CdPaintStoresRef(i32 nBand, bool bPreview, const char *pszBottom, i32 nBottomX,
                             const char *pszName, const char *pszReason)
{
    u16 *pRef = (u16 *)g_cdRef.pPixels;

    memset(g_cdRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_cdRef;

    BaseUi_DrawFrameRails();
    (void)Gfx_PackRgb565Pair(&g_cdColor, 150, 150, 255, 20, 20, 20);
    CdDrawCentered(210, 54, "SHIP");
    CdDrawCentered(147, 54, "BASE");

    UiPanel_DrawText(&g_cdSlotPanel);
    for (i32 i = 0; i < 4; ++i) {
        static const i32 anX[4] = { 130, 166, 181, 196 };
        UiPanel *const ap[4] = { &g_cdAvailPanel, &g_cdSelRowPanel, &g_cdCommitPanel, &g_cdTakenPanel };
        SoftSurf_BlitRectProxy(0, anX[i], 69, 1, nBand + 2, 0xB4, 0xB4, 0xB4);
        UiPanel_DrawText(ap[i]);
    }

    if (bPreview) {
        (void)Gfx_PackRgb565Pair(&g_cdColor, 150, 150, 255, 20, 20, 55);
        Text_DrawString(10, 297, pszBottom);
    } else {
        (void)Gfx_PackRgb565Pair(&g_cdColor, 68, 244, 252, 4, 70, 75);
        Text_DrawString(nBottomX, 297, pszBottom);
    }

    (void)Gfx_PackRgb565Pair(&g_cdColor, 200, 200, 200, 10, 25, 10);
    CdDrawCentered(120, 30, pszName);
    (void)Gfx_PackRgb565Pair(&g_cdColor, 220, 80, 80, 50, 30, 30);
    if (pszReason != NULL) {
        (void)Ui_AnimExpandBlit_IfEnabled(&g_cdAnim, 20, 130, 200, 70, 1);
        CdDrawCentered(120, 158, pszReason);
    }

    (void)pRef;
    g_pMainSurface = &g_cdSurf;
}

/* The detail arm's frame: rails, the name at y 10, the two percentage lines, the three pad blits, the
 * (250,250,250 / 10,10,25) pair, the two glyph-and-count lines and the two hold lines. */
static void CdPaintDetailRef(const char *pszName, const char *pszFuel, const char *pszDamage,
                             i32 nCraftPadX, i32 nPadLX, i32 nPadRX,
                             const char *pszAboard, const char *pszCarried,
                             const char *pszFree, const char *pszTotal)
{
    u16 *pRef = (u16 *)g_cdRef.pPixels;

    memset(g_cdRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_cdRef;

    BaseUi_DrawFrameRails();
    (void)Gfx_PackRgb565Pair(&g_cdColor, 200, 200, 200, 10, 25, 10);
    CdDrawCentered(120, 10, pszName);
    const u16 uPad = (u16)Gfx_PackRgb565Pair(&g_cdColor, 100, 150, 250, 10, 25, 10);
    Text_DrawString(150, 30, pszFuel);
    Text_DrawString(10, 30, pszDamage);

    if (nCraftPadX >= 0) {
        pRef[CD_CRAFT_PAD_Y * FRAME_W + nCraftPadX] = uPad;
    }
    if (nPadLX >= 0) {
        pRef[CD_WEAPON_PAD_Y(0) * FRAME_W + nPadLX] = uPad;
    }
    if (nPadRX >= 0) {
        pRef[CD_WEAPON_PAD_Y(2) * FRAME_W + nPadRX] = uPad;
    }

    (void)Gfx_PackRgb565Pair(&g_cdColor, 250, 250, 250, 10, 10, 25);
    if (pszAboard != NULL) {
        Font_DrawGlyph(130, 200, (u16)(g_fontSys.nBaseBitsBase + 38), 0);
        Text_DrawString(142, 200, "x");
        Text_DrawString(150, 200, pszAboard);
    }
    if (pszCarried != NULL) {
        Font_DrawGlyph(180, 200, (u16)(g_fontSys.nBaseBitsBase + 40), 0);
        Text_DrawString(192, 200, "x");
        Text_DrawString(200, 200, pszCarried);
    }
    if (pszFree != NULL) {
        Text_DrawString(95, 236, pszFree);
        Text_DrawString(95, 248, pszTotal);
    }

    g_pMainSurface = &g_cdSurf;
}

/* BaseFacScreen_DrawNameGrid @ 0x61c8c. The whole draw is glyphs on the logical surface, so the check is
 * the whole frame: a reference frame is painted here from the ARM's own coordinates and sprite offsets
 * and compared against what the port drew. Every number below is a literal off the disassembly -- the
 * test must not read the macros the port reads. */

static SoftSurf g_ngSurf;
static SoftSurf g_ngRef;
static u8 g_aNgGrid[1524];
static Ship g_aNgShip[4];
static Ship *g_apNgShip[4];
static u8 g_bNgCraftCount;
static i32 g_nNgOriginX, g_nNgOriginY;
static GfxColorEnv g_ngColor;
static FacNameGridEnv g_ngEnv;

/* abFacilityGrid @ 0x159768: buildDays u8[8][6][6] at +0, then eight 144-byte blocks at 0x174 whose
 * layers sit at 0 (tileA), 36 (tileB) and 72 (layer2). */
#define NG_FAC(nBase) (372 + 144 * (nBase))
#define NG_CELL(nBase, nLayer, nRow, nCol) \
    g_aNgGrid[NG_FAC(nBase) + (nLayer) + 6 * (nRow) + (nCol)]
#define NG_DAYS(nBase, nRow, nCol) (36 * (nBase) + 6 * (nRow) + (nCol))

static void NgClear(void)
{
    memset(g_ngSurf.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_ngSurf;
}

static bool NgSame(void)
{
    return memcmp(g_ngSurf.pPixels, g_ngRef.pPixels, (size_t)FRAME_PIX * 2u) == 0;
}

/* The fixture: base 0 carries a finished cell, three cells under construction (stamps 1, 9 and 12), an
 * empty cell, a finished cell whose tileA stamp is outside the switch, and three hangar quadrants
 * holding craft whose layer2 bytes are 34, 33 and 37. Base 1 carries one cell so the base index shows. */
static void NgFixture(void)
{
    i32 i;

    memset(g_aNgGrid, 0, sizeof(g_aNgGrid));
    memset(g_aNgShip, 0, sizeof(g_aNgShip));
    for (i = 0; i < 4; ++i) {
        g_apNgShip[i] = &g_aNgShip[i];
    }
    g_aNgShip[0].bId = 1;
    g_aNgShip[0].bHangarX = 3;
    g_aNgShip[0].bHangarY = 3;
    g_aNgShip[0].bAtBase = 1;
    g_aNgShip[0].bHomeBase = 0;
    g_aNgShip[1].bId = 2;
    g_aNgShip[1].bHangarX = 4;
    g_aNgShip[1].bHangarY = 4;
    g_aNgShip[1].bAtBase = 1;
    g_aNgShip[1].bHomeBase = 0;
    g_aNgShip[2].bId = 3;
    g_aNgShip[2].bHangarX = 5;  /* deliberately off the diagonal: a port that compares bHangarX against
                                 * the row, or bHangarY against the column, would still match every craft
                                 * if all three stood at (n,n) */
    g_aNgShip[2].bHangarY = 3;
    g_aNgShip[2].bAtBase = 1;
    g_aNgShip[2].bHomeBase = 0;
    g_aNgShip[3].bId = 4;
    g_aNgShip[3].bHangarX = 0;
    g_aNgShip[3].bHangarY = 0;
    g_aNgShip[3].bAtBase = 1;
    g_aNgShip[3].bHomeBase = 1; /* homed elsewhere, so the grid must not draw it */
    g_bNgCraftCount = 4;

    NG_CELL(0, 36, 0, 0) = 18;  /* finished: buildDays 0, so the name sprite is used as-is */
    NG_CELL(0, 36, 0, 1) = 18;
    NG_CELL(0, 0, 0, 1) = 1;    /* stamp 1: build icon +4, days at (-w/2 + 16, +7) */
    g_aNgGrid[NG_DAYS(0, 0, 1)] = 5;
    NG_CELL(0, 36, 1, 0) = 18;
    NG_CELL(0, 0, 1, 0) = 9;    /* stamp 9: build icon +13, no days line at all */
    g_aNgGrid[NG_DAYS(0, 1, 0)] = 3;
    NG_CELL(0, 36, 1, 1) = 18;
    NG_CELL(0, 0, 1, 1) = 12;   /* stamp 12: build icon +16, days at (-w/2, -8) */
    g_aNgGrid[NG_DAYS(0, 1, 1)] = 12;
    NG_CELL(0, 36, 0, 2) = 18;
    NG_CELL(0, 0, 0, 2) = 11;   /* stamp 11: build icon +15 and, unlike 1..3, no days line */
    g_aNgGrid[NG_DAYS(0, 0, 2)] = 1;
    NG_CELL(0, 36, 2, 2) = 0;   /* a zero tileB skips the cell even though it is under construction */
    NG_CELL(0, 0, 2, 2) = 1;
    g_aNgGrid[NG_DAYS(0, 2, 2)] = 4;
    NG_CELL(0, 36, 2, 3) = 18;
    NG_CELL(0, 0, 2, 3) = 7;    /* a stamp outside the switch: the icon and the days line are both skipped */
    g_aNgGrid[NG_DAYS(0, 2, 3)] = 2;
    NG_CELL(0, 36, 3, 3) = 18;
    NG_CELL(0, 0, 3, 3) = 12;   /* the hangar's bottom-right quadrant */
    NG_CELL(0, 72, 3, 3) = 34;  /* craft sprite nudged one pixel LOW */
    NG_CELL(0, 36, 4, 4) = 18;
    NG_CELL(0, 0, 4, 4) = 12;
    NG_CELL(0, 72, 4, 4) = 33;  /* nudged one pixel HIGH */
    /* Days in a quadrant whose layer2 is non-zero: pass 1 has to take the NAME sprite there even though
     * the cell is still under construction, which is the only thing that pins the `nPass != 0` test. */
    g_aNgGrid[NG_DAYS(0, 4, 4)] = 6;
    NG_CELL(0, 36, 3, 5) = 18;
    NG_CELL(0, 0, 3, 5) = 12;   /* the hangar's other bottom-right quadrant, at column 5 row 3 */
    NG_CELL(0, 72, 3, 5) = 37;  /* nudged two pixels HIGH */
    NG_CELL(1, 36, 0, 0) = 20;  /* base 1's only cell, under construction: the buildDays stride for a
                                 * non-zero base is only visible here, not in base 0 at offset 0 */
    NG_CELL(1, 0, 0, 0) = 2;
    g_aNgGrid[NG_DAYS(1, 0, 0)] = 9;

    memset(&g_ngColor, 0, sizeof(g_ngColor));
    g_nNgOriginX = 0;
    g_nNgOriginY = 0;

    memset(&g_ngEnv, 0, sizeof(g_ngEnv));
    g_ngEnv.abFacilityGrid = g_aNgGrid;
    g_ngEnv.apCraft = g_apNgShip;
    g_ngEnv.pnCraftCount = &g_bNgCraftCount;
    g_ngEnv.pnOriginX = &g_nNgOriginX;
    g_ngEnv.pnOriginY = &g_nNgOriginY;
    g_ngEnv.pColor = &g_ngColor;
    g_ngEnv.nBaseIdx = 0;

    /* Font_DrawGlyph with nPal 0 reads palette slot 0. Left zeroed, every glyph would blit as colour 0
     * and the frame compare would prove nothing, so the slot gets distinct non-zero colours. */
    for (i = 1; i < PUFO_PAL_SIZE; ++i) {
        g_aPalette[0][i] = (u32)(0x100 + i);
    }
}

/* The reference frame, painted from the ARM: the 36 blank sprites, the two name passes, the build icons
 * with their days labels, then the craft. */
static void NgPaintRef(i32 nBase, bool bCraft)
{
    const u8 *pG = g_aNgGrid;
    i32 nRow, nCol;

    memset(g_ngRef.pPixels, 0, (size_t)FRAME_PIX * 2u);
    g_pMainSurface = &g_ngRef;

    for (nRow = 0; nRow < 6; ++nRow) {
        for (nCol = 0; nCol < 6; ++nCol) {
            Font_DrawGlyph(32 * nCol + g_nNgOriginX, 36 + 32 * nRow + g_nNgOriginY,
                           g_fontSys.nBaseBitsBase, 0);
        }
    }

    /* `while (v27 < 2)` with the layer byte at 372 + 144*base + 36*v27 + 6*row + col: pass 0 is tileA,
     * pass 1 is tileB, and layer2 is left to the craft pass (0x61D14..0x61D50). */
    for (i32 nPass = 0; nPass < 2; ++nPass) {
        const i32 nLayer = (nPass == 0) ? 0 : 36;

        for (nRow = 0; nRow < 6; ++nRow) {
            for (nCol = 0; nCol < 6; ++nCol) {
                const u8 uLayer = pG[NG_FAC(nBase) + nLayer + 6 * nRow + nCol];
                const u8 uDays = pG[36 * nBase + 6 * nRow + nCol];
                const i32 x = 32 * nCol + g_nNgOriginX;
                const i32 y = 36 + 32 * nRow + g_nNgOriginY;
                i32 nStamp;

                if (uLayer == 0) {
                    continue;
                }
                Font_DrawGlyph(x, y,
                               (u16)((uDays == 0 || nPass != 0) ? g_fontSys.nBaseBitsBase + uLayer
                                                                : g_fontSys.nBaseBitsBase), 0);
                if (uDays == 0) {
                    continue;
                }
                nStamp = pG[NG_FAC(nBase) + 6 * nRow + nCol];
                if (nStamp == 1 || nStamp == 2 || nStamp == 3) {
                    char szNum[8];
                    i32 nW;

                    Font_DrawGlyph(x, y, (u16)(g_fontSys.nBaseBitsBase + nStamp + 3), 0);
                    (void)Gfx_PackRgb565Pair(&g_ngColor, 150, 150, 255, 0, 0, 255);
                    snprintf(szNum, sizeof(szNum), "%u", (u32)uDays);
                    nW = Text_MeasureWidth(szNum);
                    Text_DrawString(x - nW / 2 + 16, y + 7, szNum);
                } else if (nStamp >= 9 && nStamp <= 11) {
                    Font_DrawGlyph(x, y, (u16)(g_fontSys.nBaseBitsBase + nStamp + 4), 0);
                } else if (nStamp == 12) {
                    char szNum[8];
                    i32 nW;

                    Font_DrawGlyph(x, y, (u16)(g_fontSys.nBaseBitsBase + 16), 0);
                    (void)Gfx_PackRgb565Pair(&g_ngColor, 150, 150, 255, 0, 0, 255);
                    snprintf(szNum, sizeof(szNum), "%u", (u32)uDays);
                    nW = Text_MeasureWidth(szNum);
                    Text_DrawString(x - nW / 2, y - 8, szNum);
                }
            }
        }
    }

    if (bCraft && g_bNgCraftCount > 0) {
        for (nRow = 0; nRow < 6; ++nRow) {
            for (nCol = 0; nCol < 6; ++nCol) {
                i32 i;

                if (pG[NG_FAC(nBase) + 6 * nRow + nCol] != 12) {
                    continue;
                }
                for (i = 0; i < (i32)g_bNgCraftCount; ++i) {
                    const Ship *pShip = &g_aNgShip[i];
                    i32 nLayer2 = pG[NG_FAC(nBase) + 72 + 6 * nRow + nCol];
                    i32 nNudge;

                    if ((i32)pShip->bHangarX != nCol || (i32)pShip->bHangarY != nRow
                        || pShip->bAtBase == 0 || (i32)pShip->bHomeBase != nBase) {
                        continue;
                    }
                    if (nLayer2 == 33 || nLayer2 == 35) {
                        nNudge = -1;
                    } else if (nLayer2 == 34) {
                        nNudge = 1;
                    } else if (nLayer2 == 36) {
                        nNudge = 0;
                    } else if (nLayer2 == 37) {
                        nNudge = -2;
                    } else {
                        continue;
                    }
                    Font_DrawGlyph(32 * nCol - 16 + g_nNgOriginX,
                                   19 + 32 * nRow + g_nNgOriginY + nNudge,
                                   (u16)(g_fontSys.nBaseBitsBase + nLayer2), 0);
                }
            }
        }
    }

    g_pMainSurface = &g_ngSurf;
}

static void TestBaseFacNameGrid(void)
{
    const u16 *pPix;

    printf("BaseFacScreen_DrawNameGrid @ 0x61c8c\n");

    FrameLoadFont();
    if (!g_bFrameFontReady) {
        printf("  FAIL lang.dat / main.dat did not open, so the name grid cannot be checked\n");
        ++g_nFailures;
        return;
    }

    SoftSurf_ctor(&g_ngSurf);
    (void)SoftSurf_AllocPixels(&g_ngSurf, FRAME_W, FRAME_H, PIXMODE_16BPP);
    SoftSurf_ctor(&g_ngRef);
    (void)SoftSurf_AllocPixels(&g_ngRef, FRAME_W, FRAME_H, PIXMODE_16BPP);
    pPix = (const u16 *)g_ngSurf.pPixels;

    /* Geoscape_InitUI @ 0x85F64..0x85F80 loads "basebits" straight after "font" and captures the frame
     * count BEFORE the load into fontSys_nBaseBitsSprBase @ 0x158294. Every sprite this grid draws --
     * the backdrop, the facility names, the build icons and the craft -- is a basebits sprite, so the
     * group has to be loaded for the frame to mean anything. */
    g_fontSys.nBaseBitsBase = g_fontSys.nTotalSprFrames;
    if (!Font_LoadSpr(&g_fontSys, "basebits")) {
        printf("  FAIL basebits.spr did not load, so the name grid has no sprites to draw\n");
        ++g_nFailures;
        return;
    }
    Font_BuildIndex(&g_fontSys);

    NgFixture();

    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    NgPaintRef(0, true);
    Check(NgSame(), "the whole name grid matches the ARM frame: backdrop, both name passes, the build icons, the days labels and the craft");

    /* The 36 backdrop sprites are drawn whatever the cells hold. */
    NgClear();
    memset(g_aNgGrid, 0, sizeof(g_aNgGrid));
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    Check(CdInkAny(pPix, 0, 36, 239, 319) == 36 * 1024,
          "an empty grid still draws the 6x6 blank backdrop: 36 full 32x32 basebits sprites (0x61CA4)");
    Check(CdInkAny(pPix, 0, 0, 239, 35) == 0 && CdInkAny(pPix, 192, 0, 239, 319) == 0
          && CdInkAny(pPix, 0, 228, 239, 319) == 0,
          "  and the grid occupies exactly x 0..191 and y 36..227 -- six columns and six rows at a 32 px pitch");
    NgFixture();

    /* A base index outside the eight blocks draws nothing at all; the original walks off the array. */
    NgClear();
    g_ngEnv.nBaseIdx = 8;
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    Check(CdInkAny(pPix, 0, 36, 239, 227) == 0,
          "a base index of 8 draws nothing -- the port bounds the eight fac blocks (deviation)");
    g_ngEnv.nBaseIdx = -1;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    Check(CdInkAny(pPix, 0, 36, 239, 227) == 0,
          "  and so does a negative one");
    g_ngEnv.nBaseIdx = 7;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    Check(CdInkAny(pPix, 0, 36, 239, 319) == 36 * 1024,
          "base 7 is the last of the eight blocks and is still drawn -- only its cells are empty");
    g_ngEnv.nBaseIdx = 1;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    NgPaintRef(1, true);
    Check(NgSame(), "base 1 draws its own grid, with base 0's cells left alone");
    g_ngEnv.nBaseIdx = 0;

    /* No craft: the name passes are untouched and only the craft sprites disappear. */
    g_bNgCraftCount = 0;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    NgPaintRef(0, false);
    Check(NgSame(), "Map_nCraftCount 0 skips the whole craft pass and nothing else (0x62154)");
    g_bNgCraftCount = 4;

    /* The craft must belong to the base being drawn: ship 0 stands in the (3,3) quadrant but is homed to
     * base 1, so the quadrant keeps its tile stamp and the craft disappears. */
    g_aNgShip[0].bHomeBase = 1;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    NgPaintRef(0, true);
    Check(NgSame(), "a craft homed to another base is not drawn (0x6219C)");
    g_aNgShip[0].bHomeBase = 0;

    /* bAtBase gates the same walk. */
    g_aNgShip[1].bAtBase = 0;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    NgPaintRef(0, true);
    Check(NgSame(), "a craft that is not at the base is not drawn either (0x6217C)");
    g_aNgShip[1].bAtBase = 1;

    /* layer2 both names the craft sprite and picks its nudge: 36 is the one value that shifts nothing,
     * 35 gets the same nudge as 33, and a byte outside 33..37 draws no craft at all. */
    NG_CELL(0, 72, 3, 3) = 36;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    NgPaintRef(0, true);
    Check(NgSame(), "layer2 36 draws the craft sprite unshifted (0x62230)");
    NG_CELL(0, 72, 3, 3) = 35;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    NgPaintRef(0, true);
    Check(NgSame(), "layer2 35 draws the craft one pixel HIGH, the same nudge 33 gets (0x62278)");
    NG_CELL(0, 72, 3, 3) = 40;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    NgPaintRef(0, true);
    Check(NgSame(), "a layer2 byte outside 33..37 draws no craft at all (0x621E0)");
    NG_CELL(0, 72, 3, 3) = 34;

    /* The scroll origin moves every glyph, the craft's -16 x offset included. */
    g_nNgOriginX = 40;
    g_nNgOriginY = 20;
    NgClear();
    BaseFacScreen_DrawNameGrid(&g_ngEnv);
    NgPaintRef(0, true);
    Check(NgSame(), "nSmapOriginX / nSmapOriginY shift the whole grid");
    g_nNgOriginX = 0;
    g_nNgOriginY = 0;

    NgClear();
    BaseFacScreen_DrawNameGrid(NULL);
    Check(CdInkAny(pPix, 0, 36, 239, 227) == 0,
          "a null env draws nothing at all");
}

static void TestPlaneCraftDetail(void)
{
    const u16 *pPix;
    PlaneDrawEnv stDrawEnv;
    i32 nRet;

    printf("BasePlaneScreen_DrawCraftDetail @ 0x7baf8\n");

    FrameLoadFont();
    if (!g_bFrameFontReady) {
        printf("  FAIL lang.dat / main.dat did not open, so the detail frame cannot be checked\n");
        ++g_nFailures;
        return;
    }

    SoftSurf_ctor(&g_cdSurf);
    (void)SoftSurf_AllocPixels(&g_cdSurf, FRAME_W, FRAME_H, PIXMODE_16BPP);
    SoftSurf_ctor(&g_cdRef);
    (void)SoftSurf_AllocPixels(&g_cdRef, FRAME_W, FRAME_H, PIXMODE_16BPP);
    CdBuildStorePanels();
    CdFixture();
    pPix = (const u16 *)g_cdSurf.pPixels;

    /* ---- 1. the stores arm ---- */
    g_bCdStores = 1;
    g_bCdPreviewOk = 1;
    g_dwCdFail = 0;
    g_bCdPanelFlag = 0xA5;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_cdSelRowPanel.bSelLineHighlight, 1, "a transfer that did not fail leaves both row panels enabled (0x7BDB8)");
    CheckEqI32(g_cdCommitPanel.bSelLineHighlight, 1, "  on the sel-row and the commit-row panel");
    CheckEqI32(g_cdSelRowPanel.bRepeatArmed, 0xA5, "bPanelFlagScratch @ 0x161F0C is copied into both row panels (0x7BE58)");
    CheckEqI32(g_cdCommitPanel.bRepeatArmed, 0xA5, "  and into the commit-row panel too (0x7BE68)");
    CheckEqI32(g_bCdPanelFlag, 0, "  and the scratch byte is cleared -- the draw consumes it (0x7BE70)");

    g_dwCdFail = 1;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_cdSelRowPanel.bSelLineHighlight, 0, "a failed transfer clears both row panels' flag (0x7BDA4)");
    CheckEqI32(g_cdCommitPanel.bSelLineHighlight, 0, "  on both of them");
    g_dwCdFail = 2;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_cdSelRowPanel.bSelLineHighlight, 1, "  and only the value 1 does it -- 2 sets the flag back");

    /* The whole stores frame, with the preview armed: the hovered row's object name is drawn straight
     * out of the record at (10, 297). */
    CdFixture();
    g_bCdStores = 1;
    g_bCdPreviewOk = 1;
    g_nCdCursorY = 100;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CdPaintStoresRef(64, true, "Obj32", 0, "LGT3", NULL);
    Check(CdSame(), "the stores frame matches: rails, the two captions, the five panels and four separators");
    Check(CdInkAny(pPix, 8, 295, 120, 312) > 0,
          "  the hovered row's object name is drawn at (10, 297) (0x7C0B4)");
    CheckEqI32(g_nCdCtlBlits, 0, "  and the confirm control is not blitted while the preview is armed");

    /* Not armed: the control is recentred onto the bottom row, blitted, and "ОК" is centred there. */
    CdFixture();
    g_bCdStores = 1;
    g_nCdCursorY = 100;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32((i32)g_cdOkCtl.x, 120 - (40 >> 1), "the confirm control is recentred to 120 - (uWidth >> 1) (0x7C01C)");
    CheckEqI32((i32)g_cdOkCtl.y, 297, "  and onto the bottom row, y 297 (0x7C038)");
    CheckEqI32(g_nCdCtlBlits, 1, "  and blitted once through UiControl_BlitActivePad (0x7C040)");
    CdPaintStoresRef(64, false, "OK", 120 - Text_MeasureWidth("OK") / 2, "LGT3", NULL);
    Check(CdSame(), "  and the rest of the frame is the same, with the centred ОК in the control's place");

    /* The band is 16 * min(nLineCount, nStyle) of the SELECT-ROW panel, and every separator is band+2
     * tall at y 69. */
    CdFixture();
    g_bCdStores = 1;
    g_cdSelRowPanel.nStyle = 2; /* min(4, 2) = 2 rows -> band 32 -> separators 34 tall */
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32((i32)CdPix(pPix, 130, 69), XfPix(0xB4, 0xB4, 0xB4), "the separators start at y 69 in palette index 0xB4 (0x7BEC8)");
    CheckEqI32((i32)CdPix(pPix, 130, 69 + 33), XfPix(0xB4, 0xB4, 0xB4), "  and run band + 2 = 34 tall, from nStyle 2 (0x7BEA4)");
    Check(CdPix(pPix, 130, 69 + 34) != XfPix(0xB4, 0xB4, 0xB4), "  and stop there");
    CheckEqI32((i32)CdPix(pPix, 196, 69), XfPix(0xB4, 0xB4, 0xB4), "  the fourth separator is at x 196 (0x7BF50)");
    Check(CdPix(pPix, 145, 69) != XfPix(0xB4, 0xB4, 0xB4), "  and there is no separator at x 145");
    CdFixture();
    g_bCdStores = 1;
    g_bCdPreviewOk = 1;
    g_nCdCursorY = 139; /* band 64 -> the gate is 75 < y < 139, and 139 is outside it */
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_nCdCtlBlits, 1, "the preview gate is 75 < cursor < band + 75, so 139 falls outside (0x7BF90)");

    CdFixture();
    g_bCdStores = 1;
    g_bCdPreviewOk = 1;
    g_nCdCursorY = 75; /* the lower bound is exclusive too */
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_nCdCtlBlits, 1, "  and so does 75, the band's own lower bound (0x7BF80)");

    /* The refusal panel: only dwActionFail == 1 reaches it, and the reason is a switch. */
    CdFixture();
    g_bCdStores = 1;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_bCdAnimStarted, 0, "no refusal panel while dwActionFail @ 0x1870C8 is not 1 (0x7C14C)");
    CheckEqI32(CdInk(pPix, 20, 150, 219, 172, CdRefuseColor()), 0, "  and no reason line either");

    CdFixture();
    g_bCdStores = 1;
    g_dwCdFail = 1;
    g_nCdReason = 0;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_bCdAnimStarted, 1, "dwActionFail 1 opens the 200x70 refusal panel (0x7C21C)");
    CdPaintStoresRef(64, false, "OK", 120 - Text_MeasureWidth("OK") / 2, "LGT3", "NOROOM");
    Check(CdSame(), "  with TEXTS[410] centred at (120 - w/2, 158) for reason 0");

    CdFixture();
    g_bCdStores = 1;
    g_dwCdFail = 1;
    g_nCdReason = 1;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CdPaintStoresRef(64, false, "OK", 120 - Text_MeasureWidth("OK") / 2, "LGT3", "NOSTORE");
    Check(CdSame(), "  reason 1 draws TEXTS[408] instead (0x7C1F4)");

    CdFixture();
    g_bCdStores = 1;
    g_dwCdFail = 1;
    g_nCdReason = 2;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CdPaintStoresRef(64, false, "OK", 120 - Text_MeasureWidth("OK") / 2, "LGT3", "NOAMMO");
    Check(CdSame(), "  reason 2 draws TEXTS[409] (0x7C190)");

    CdFixture();
    g_bCdStores = 1;
    g_dwCdFail = 1;
    g_nCdReason = 3;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_bCdAnimStarted, 0, "  and any other reason returns with the red pair packed and nothing drawn");
    CdPaintStoresRef(64, false, "OK", 120 - Text_MeasureWidth("OK") / 2, "LGT3", NULL);
    Check(CdSame(), "  -- the frame is byte-identical to the no-refusal one, so the switch really leaves");
    CheckEqI32(CdInkAny(pPix, 20, 140, 219, 196), 0,
               "  -- the hangar's reason map is a switch, not the buy/sell screen's 405 + reason");

    /* The list-panel gate short-circuits the whole stores arm. */
    CdFixture();
    g_bCdListBuilt = 1;
    g_bCdStores = 1;
    (void)UiPanel_ctor(&g_cdCrewPanel, 2);
    (void)UiPanel_ctor(&g_cdCrewStatusPanel, 2);
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    Check(CdPix(pPix, 130, 69) != XfPix(0xB4, 0xB4, 0xB4),
          "bPlaneListPanelsBuilt @ 0x161F24 set skips the stores arm entirely (0x7BCF4)");
    Check(CdInkAny(pPix, 100, 28, 140, 42) > 0, "  while the centred name at y 30 still draws (0x7BD3C)");

    /* ---- 2. the detail arm ---- */
    CdFixture();
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CdPaintDetailRef("LGT3", "FUEL 10%", "DMG 75%", CD_CRAFT_PAD_X(1), 55, 156, "3", "16",
                     "FREE 87", "TOTAL 120");
    Check(CdSame(), "the detail frame matches: name, the two percentages, three pads, two count lines, two hold lines");
    Check(CdInkAny(pPix, 128, 198, 170, 214) > 0,
          "  the aboard line draws its sprite glyph and count at (130, 200) (0x7C7A0)");
    Check(CdInkAny(pPix, 178, 198, 224, 214) > 0,
          "  and the crew line draws its own at (180, 200) (0x7C820)");
    Check(CdInkAny(pPix, 90, 234, 229, 258) > 0, "  and the two hold lines draw at (95, 236) and (95, 248)");

    /* The label and the field do not match: "Топливо" reads the RANGE pair, "Повреждения" the FUEL pair. */
    CdFixture();
    g_aCdCraft[2].uRangeMax = 60000; /* 3000 / 60000 -> 5%, and the damage line must not move */
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CdPaintDetailRef("LGT3", "FUEL 5%", "DMG 75%", CD_CRAFT_PAD_X(1), 55, 156, "3", "16",
                     "FREE 87", "TOTAL 120");
    Check(CdSame(), "the fuel line is 100 * uRangeRemain / uRangeMax, from the range pair (0x7C2E4)");

    CdFixture();
    g_aCdCraft[2].nFuel = 800; /* 100 - 100 * 800 / 800 -> 0%, and the fuel line must not move */
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CdPaintDetailRef("LGT3", "FUEL 10%", "DMG 0%", CD_CRAFT_PAD_X(1), 55, 156, "3", "16",
                     "FREE 87", "TOTAL 120");
    Check(CdSame(), "  and the damage line is 100 - 100 * nFuel / uFuelMax, from the fuel pair (0x7C37C)");

    CdFixture();
    g_aCdCraft[2].uRangeMax = 0;
    g_aCdCraft[2].uFuelMax = 0;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CdPaintDetailRef("LGT3", "FUEL 0%", "DMG 100%", CD_CRAFT_PAD_X(1), 55, 156, "3", "16",
                     "FREE 87", "TOTAL 120");
    Check(CdSame(), "a zero divisor draws 0 percent / 100 percent instead of faulting (the original has no test)");

    /* The pads. */
    CdFixture();
    g_aCdCraft[2].bCraftType = 4;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32((i32)CdPix(pPix, CD_CRAFT_PAD_X(4), CD_CRAFT_PAD_Y), CdPadColor(),
               "bCraftType picks the craft pad out of the object block's five (0x7C410)");
    CdFixture();
    g_aCdCraft[2].bCraftType = 5;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(CdInkAny(pPix, 100, CD_CRAFT_PAD_Y, 112, CD_CRAFT_PAD_Y), 0,
               "  and a type outside 0..4 blits no craft pad (0x7C40C)");

    CdFixture();
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_aCdWeaponPad[0].nOriginX, 55, "the left mount writes nOriginX 55 into its pad (0x7C4DC)");
    CheckEqI32(g_aCdWeaponPad[2].nOriginX, 156, "  and the right mount writes 156 into its own (0x7C5AC)");
    CheckEqI32((i32)CdPix(pPix, 55, CD_WEAPON_PAD_Y(0)), CdPadColor(),
               "  weapon 26 takes the FIRST pad, not the second (0x7C4D8)");
    CheckEqI32((i32)CdPix(pPix, 156, CD_WEAPON_PAD_Y(2)), CdPadColor(),
               "  and weapon 25 takes the THIRD (0x7C5A8)");

    CdFixture();
    g_aCdCraft[2].bWeaponL = 24;
    g_aCdCraft[2].bWeaponR = 31;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(CdInkAny(pPix, 55, CD_WEAPON_PAD_Y(0), 156, CD_WEAPON_PAD_Y(5)), 0,
               "a weapon byte outside 25..30 blits no mount pad (0x7C484 / 0x7C540)");

    /* The four counters. */
    CdFixture();
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_nCdAboard, 3, "the aboard counter is the soldiers whose bUnk183 is this craft (0x7C650)");
    CheckEqI32(g_nCdAtBase, 4, "the at-base counter is the soldiers whose nKind is this base (0x7C664)");
    CheckEqI32(g_nCdUnassigned, 1, "  and the unassigned one is at this base with bUnk183 == 255 (0x7C67C)");
    CheckEqI32(g_nCdCarried, 16, "the carried counter is the five crew slots' aItemQty[craft] (0x7C6A0)");

    CdFixture();
    g_nCdSoldiers = 0;
    g_cdEnv.nSoldierCount = 0;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_nCdAboard, 0, "a non-positive soldier count skips the walk (0x7C618)");
    CheckEqI32(g_nCdCarried, 16, "  but the crew sum still runs");

    CdFixture();
    g_aCdCraft[2].bCanDogfight = 0;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_nCdAboard, 0, "Ship.bCanDogfight @ +0x21 clear leaves all four counters at zero (0x7C604)");
    CheckEqI32(g_nCdCarried, 0, "  including the crew sum");
    CheckEqI32(CdInkAny(pPix, 90, 234, 229, 258), 0, "  and neither hold line draws");
    CheckEqI32(CdInkAny(pPix, 128, 198, 224, 214), 0, "  and neither count line draws");

    CdFixture();
    g_aCdSol[0].bUnk183 = 1;
    g_aCdSol[1].bUnk183 = 1;
    g_aCdSol[4].bUnk183 = 1;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(g_nCdAboard, 0, "  and the aboard count follows the craft index, not the soldier count");
    CheckEqI32(CdInkAny(pPix, 128, 198, 170, 214), 0,
               "  with nothing aboard, the aboard line is skipped (0x7C780)");
    Check(CdInkAny(pPix, 178, 198, 224, 214) > 0,
               "  while the crew line still draws, because 16 are carried");

    /* The hold lines read the cap and the used space. */
    CdFixture();
    g_aCdObj[5].aItemQty[2] = 0; /* 10 used instead of 22 */
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CdPaintDetailRef("LGT3", "FUEL 10%", "DMG 75%", CD_CRAFT_PAD_X(1), 55, 156, "3", "16",
                     "FREE 99", "TOTAL 120");
    Check(CdSame(), "the free-space line is Craft_GetTypeCargoCap - Base_CalcWeaponStorageUsed (0x7C8D0)");

    CdFixture();
    g_aCdCraft[2].bCraftType = 3; /* the cap is 0 for this type */
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CdPaintDetailRef("INT-3", "FUEL 10%", "DMG 75%", CD_CRAFT_PAD_X(3), 55, 156, "3", "16",
                     "FREE -33", "TOTAL 0");
    Check(CdSame(), "  and a type with no hold gives a cap of 0, so the free line goes negative");

    /* ---- 3. the stat tail ---- */
    CdFixture();
    g_cdStatPanels.bPanelAOpen = 1;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32((i32)CdPix(pPix, 100, 100), XfPix(0x14, 0x50, 0x63),
               "bStatPanelAOpen @ 0x161F18 set draws the first stat panel's frame (0x7C998)");
    CheckEqI32(g_cdStatPanels.bPanelBOpen, 0, "  and leaves the second panel closed");

    CdFixture();
    g_cdStatPanels.bPanelBOpen = 1;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32((i32)CdPix(pPix, 100, 100), XfPix(0x14, 0x50, 0x63),
               "with only bStatPanelBOpen @ 0x161F1C set, the second panel is drawn instead (0x7C9D0)");

    CdFixture();
    g_aCdObj[25].auPerBaseVal[0] = 4;
    g_aCdObj[27].auPerBaseVal[0] = 2;
    g_aCdObj[29].auPerBaseVal[0] = 1;
    CdClear();
    (void)BasePlaneScreen_DrawCraftDetail(&g_cdEnv);
    CheckEqI32(CdInkAny(pPix, 8, 75, 231, 155), 0,
               "with neither gate set the tail only rebuilds the stat rows, drawing nothing (0x7C9DC)");
    CheckEqI32(g_nCdStatRows, 4, "  and the row count is 1 plus the three weapons this base holds");
    CheckEqI32(g_nCdStatW[0], 4, "  into the six per-weapon globals");
    CheckEqI32(g_nCdStatW[4], 1, "  including the plasma cannon");

    /* ---- 4. the delegation from BasePlaneScreen_Draw ---- */
    CdFixture();
    memset(&stDrawEnv, 0, sizeof(stDrawEnv));
    stDrawEnv.pColor = &g_cdColor;
    stDrawEnv.pbShowCraftDetail = &g_bCdShowDetail;
    stDrawEnv.pDetail = &g_cdEnv;
    CdClear();
    g_bCdShowDetail = 1;
    nRet = BasePlaneScreen_Draw(&stDrawEnv);
    CdPaintDetailRef("LGT3", "FUEL 10%", "DMG 75%", CD_CRAFT_PAD_X(1), 55, 156, "3", "16",
                     "FREE 87", "TOTAL 120");
    Check(CdSame(), "BasePlaneScreen_Draw hands the whole call to the detail view (0x7D37C)");
    CheckEqI32(nRet, 0, "  and returns its value");
    CheckEqI32(CdInkAny(pPix, 20, 88, 229, 102), 0,
               "  while none of the hangar's own captions or inner rails run");

    FrameRestoreFont();
}

/* ------------------------------------------------- the ufopaedia screen draw
 *
 * Evidence: Ui_DrawBaseInfo @ 0x9b5f4. The draw's observable layers are the screen pad, the
 * dashed frame (entry not dirty), the close control, the three gates, the screen fill and the
 * solid frame (entry dirty), the normalisation of the entry number, and the per-category rect
 * signatures. The fill colour is predictable: Blit_PackedColor(0x64) with mid 0x64 and low 0xC8
 * blended into a zeroed surface with key 0x50, field by field. */

static SoftSurf g_udSurf;
static PadImage g_udScreenPad, g_udEntryPad, g_udClosePad;
static u8 g_aUdScreenPad[8];
static u8 g_aUdEntryPad[4 + 10 * 8];
static u8 g_aUdClosePad[8];
static UiControl g_udCloseCtl;
static UiPanel g_udSelPanel, g_udTextPanel, g_udCatPanel;
static UiPanel *g_apUdCatPanels[PUFO_UFOPAEDIA_CATEGORIES];
static ResearchTopicLive g_aUdTopics[64];
static u8 g_aUdText[PUFO_TEXT_REC_STRIDE * 1721];
static BaseFacFlag g_aUdFac[PUFO_BASEFAC_COUNT];
static FacBuildSpec g_aUdBuild[PUFO_FACBUILDSPEC_COUNT];
static ObjectRecFull g_aUdObj[8];
static ObjectRecFull *g_apUdObj[8];
static PufoFont g_udFont;
static GfxColorEnv g_udColor;
static UiBaseInfoDrawEnv g_udEnv;
static i32 g_nUdEntryNum, g_nUdSub, g_nUdTopic, g_nUdCategory, g_nUdPalBase;
static u8 g_bUdDirty, g_bUdBuilt, g_bUdOpen;
static i32 g_nUdCtlBlits;

static void UdCtlBlit(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
    ++g_nUdCtlBlits;
}

/* Blit_PackedColor: the index supplies the 5-bit top field, the operands the middle and low. */
static u16 UdPix(u32 nIdx, u32 nMid, u32 nLow)
{
    return (u16)(((nIdx >> 3) << 11) + 32u * (nMid >> 2) + (nLow >> 3));
}

#define UD_FRAME UdPix(0, 0x64, 0x50) /* the solid frame colour every block shares (810) */
#define UD_FILL  6375u                /* the screen fill over a zeroed surface: top 3, mid 7, low 7 */

static void UdPutRec(i32 nRec, const char *psz)
{
    PutTextRec(g_aUdText, nRec, psz);
}

static void UdReset(i32 nEntryNum)
{
    g_nUdEntryNum = nEntryNum;
    g_nUdSub = 0;
    g_nUdTopic = 20;
    g_nUdCategory = 0;
    g_nUdPalBase = 0;
    g_bUdDirty = 1;
    g_bUdBuilt = 1;
    g_bUdOpen = 1;
    g_nUdCtlBlits = 0;

    memset(&g_udScreenPad, 0, sizeof(g_udScreenPad));
    memset(g_aUdScreenPad, 0, sizeof(g_aUdScreenPad));
    g_aUdScreenPad[0] = 1; /* uWidth 1 */
    g_aUdScreenPad[2] = 1; /* uHeight 1 */
    g_aUdScreenPad[4] = 1; /* one indexed pixel */
    PadImage_ctor(&g_udScreenPad);
    g_udScreenPad.pData = g_aUdScreenPad;
    g_udScreenPad.uWidth = 1;
    g_udScreenPad.uHeight = 1;
    g_udScreenPad.nMode = 8;

    memset(&g_udEntryPad, 0, sizeof(g_udEntryPad));
    memset(g_aUdEntryPad, 1, sizeof(g_aUdEntryPad));
    g_aUdEntryPad[0] = 10; g_aUdEntryPad[1] = 0; /* uWidth 10 */
    g_aUdEntryPad[2] = 8;  g_aUdEntryPad[3] = 0; /* uHeight 8 */
    PadImage_ctor(&g_udEntryPad);
    g_udEntryPad.pData = g_aUdEntryPad + 4;
    g_udEntryPad.uWidth = 10;
    g_udEntryPad.uHeight = 8;
    g_udEntryPad.nMode = 8;
    g_udEntryPad.nOriginX = 33;   /* the blocks that do not centre the pad must leave these */
    g_udEntryPad.nOriginY = 77;

    memset(&g_udClosePad, 0, sizeof(g_udClosePad));
    memset(g_aUdClosePad, 0, sizeof(g_aUdClosePad));
    g_aUdClosePad[4] = 1;
    PadImage_ctor(&g_udClosePad);
    g_udClosePad.pData = g_aUdClosePad;
    g_udClosePad.uWidth = 1;
    g_udClosePad.uHeight = 1;
    g_udClosePad.nMode = 8;

    memset(&g_udCloseCtl, 0, sizeof(g_udCloseCtl));
    g_udCloseCtl.bEnabled = 1;
    g_udCloseCtl.nMode = 0;
    g_udCloseCtl.nState = 0;
    g_udCloseCtl.apPads[0] = &g_udClosePad;

    memset(g_aUdTopics, 0, sizeof(g_aUdTopics));
    for (i32 n = 0; n < 64; ++n) {
        g_aUdTopics[n].wGate4 = (u16)(1500 + n); /* the name record, 0x9BA94..0x9BAA4 */
    }
    UdPutRec(1500 + 20, "Topic");

    memset(g_aUdFac, 0, sizeof(g_aUdFac));
    for (i32 n = 0; n < PUFO_BASEFAC_COUNT; ++n) {
        g_aUdFac[n].uCost = (u32)(100 * (n + 1));
    }
    memset(g_aUdBuild, 0, sizeof(g_aUdBuild));
    for (i32 n = 0; n < PUFO_FACBUILDSPEC_COUNT; ++n) {
        g_aUdBuild[n].bTileA = (u8)n;
        g_aUdBuild[n].bTileB = (u8)(n + 1);
        g_aUdBuild[n].nBuildDays = (i8)(n + 2);
    }

    memset(g_aUdObj, 0, sizeof(g_aUdObj));
    for (i32 n = 0; n < 8; ++n) {
        g_apUdObj[n] = &g_aUdObj[n];
        g_aUdObj[n].uIconId = (u16)(n + 1);
        g_aUdObj[n].nFootW = 2;
        g_aUdObj[n].nFootH = 3;
    }

    memset(&g_udFont, 0, sizeof(g_udFont));
    g_udFont.nBigObsBase = 100;
    g_udFont.nBaseBitsBase = 200; /* no sprite index is loaded, so the glyphs no-op */

    memset(&g_udColor, 0, sizeof(g_udColor));

}

static void UdResetEnv(void)
{
    static PadImage *apPads[2];
    apPads[0] = &g_udScreenPad;
    apPads[1] = &g_udEntryPad;
    g_udEnv.ppScreenPad = apPads;
    g_udEnv.pCloseCtl = &g_udCloseCtl;
    g_udEnv.pfnCtlBlit = UdCtlBlit;
    g_udEnv.pfnCtlBlitEx = UdCtlBlit;
    g_udEnv.pCtlUser = NULL;
    g_udEnv.pbEntryDirty = &g_bUdDirty;
    g_udEnv.pbScreenBuilt = &g_bUdBuilt;
    g_udEnv.pbUfopaediaOpen = &g_bUdOpen;
    g_udEnv.pnEntryNum = &g_nUdEntryNum;
    g_udEnv.pnSub = &g_nUdSub;
    g_udEnv.pnTopic = &g_nUdTopic;
    g_udEnv.pnCategory = &g_nUdCategory;
    g_apUdCatPanels[0] = &g_udCatPanel; /* category 0 only; the guard covers the rest */
    g_udEnv.apCatPanels = g_apUdCatPanels;
    g_udEnv.pSelPanel = &g_udSelPanel;
    g_udEnv.pTextPanel = &g_udTextPanel;
    g_udEnv.aTopics = g_aUdTopics;
    g_udEnv.pTextRecs = (const char *)g_aUdText;
    g_udEnv.paFac = g_aUdFac;
    g_udEnv.apObjects = (const ObjectRecFull *const *)g_apUdObj;
    g_udEnv.nObjectCount = 8;
    g_udEnv.paBuildSpec = g_aUdBuild;
    g_udEnv.pFont = &g_udFont;
    g_udEnv.pnGlyphPalBase = &g_nUdPalBase;
    g_udEnv.pColor = &g_udColor;
}

static const u16 *g_pUdPix;

static void UdSurface(void)
{
    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&g_udSurf);
    (void)SoftSurf_AllocPixels(&g_udSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_udSurf;
    g_pUdPix = (const u16 *)g_udSurf.pPixels;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    memset(g_aPalette[8], 0, sizeof(g_aPalette[8]));
    g_aPalette[8][1] = 0xABCDu;
}

static void TestUiDrawBaseInfo(void)
{
    printf("Ui_DrawBaseInfo @ 0x9b5f4\n");

    UdSurface();
    UdReset(2500);
    UdResetEnv();
    (void)UiPanel_ctor(&g_udSelPanel, 9);
    (void)UiPanel_ctor(&g_udTextPanel, 50);
    memset(&g_udCatPanel, 0, sizeof(g_udCatPanel));
    g_udCatPanel.nLineCount = 0;

    /* The screen pad runs first, whatever the gates (0x9B5FC). */
    g_bUdBuilt = 0;
    g_bUdDirty = 0;
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32((i32)g_pUdPix[0], 0xABCD, "the screen pad is blitted before anything else (0x9B600)");
    CheckEqI32((i32)g_pUdPix[1 * 240 + 5], UdPix(100, 100, 100),
               "the dashed frame's first vertical dash at 5,1 is (0+20)*5 = 100 (0x9B638)");
    CheckEqI32((i32)g_pUdPix[166 * 240 + 5], UdPix(215, 215, 215),
               "  and the 24th climbing dash owns 166 as (23+20)*5 = 215");
    /* The climbing dashes own rows 1..168 with no gaps (7 tall on a 7 step), so the falling
     * pair's first rows (151..168) are overdrawn by later climbing iterations; the first row the
     * falling pair keeps is 172, iteration 3, colour 0xE6 - 3*5 = 215. */
    CheckEqI32((i32)g_pUdPix[152 * 240 + 5], UdPix(205, 205, 205),
               "152 is the falling pair's first row, but climbing iteration 21 (148..154) owns it (0x9B66C)");
    CheckEqI32((i32)g_pUdPix[172 * 240 + 5], UdPix(215, 215, 215),
               "the falling pair's first surviving row 172 is iteration 3 with 0xE6 - 15 = 215 (0x9B670)");
    CheckEqI32((i32)g_pUdPix[317 * 240 + 5], UdPix(115, 115, 115),
               "  and the last one ends at 317 as 230 - 115 = 115");
    CheckEqI32((i32)g_pUdPix[5 * 240 + 1], UdPix(100, 100, 100), "the top rail starts at x 1 with 100");
    CheckEqI32((i32)g_pUdPix[5 * 240 + 119], UdPix(215, 215, 215),
               "the climbing half (116..120) overdraws the falling half (119..123) at 119, as drawn");
    CheckEqI32((i32)g_pUdPix[5 * 240 + 121], UdPix(250, 250, 250),
               "  while 121 keeps the falling half's 0xFA - 0*5 = 250");
    CheckEqI32((i32)g_pUdPix[5 * 240 + 234], UdPix(135, 135, 135),
               "the falling half ends at 119 + 115 = 234 with 135");
    CheckEqI32(g_nUdCtlBlits, 1, "the close control is blitted once before the gates (0x9B7C0)");
    CheckEqI32((i32)g_pUdPix[160 * 240 + 120], 0, "and an unbuilt screen never reaches the fill (0x9B7D8)");

    /* The dirty byte decides the frame style, not the gates (0x9B618). */
    g_bUdDirty = 1;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32((i32)g_pUdPix[1 * 240 + 5], 0, "a dirty entry draws no dashed frame (0x9B618)");
    CheckEqI32((i32)g_pUdPix[160 * 240 + 120], 0, "  and the unbuilt gate still stops the fill");
    g_bUdBuilt = 1;

    /* Not open: the selector panel is drawn with no line-count gate (loc_9DDB0). */
    g_bUdOpen = 0;
    g_nUdCtlBlits = 0;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32((i32)g_pUdPix[160 * 240 + 120], 0, "the closed ufopaedia never fills the screen (0x9B7E8)");
    CheckEqI32(g_nUdCtlBlits, 1, "  and only the pre-gate close blit runs");

    /* Open but not dirty: the category-panel gate (loc_9DD80). A signed category index out of
     * the nine panels is the port's guard, not the original's. */
    g_bUdOpen = 1;
    g_bUdDirty = 0;
    g_nUdCategory = 0;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32((i32)g_pUdPix[160 * 240 + 120], 0, "a clean entry stops at the category gate (0x9B7F8)");
    g_nUdCategory = 9;
    Ui_DrawBaseInfo(&g_udEnv);
    Check(true, "a category index outside the nine panels is guarded, not dereferenced (0x9DD84 LDRSB)");
    g_nUdCategory = 0;

    /* Entry dirty: the fill and the solid frame. The fill colour pins the BlitSrcRect operands
     * (idx 0x64, mid 0x64, low 0xC8, key 0x50) against a zeroed surface. */
    g_bUdDirty = 1;
    g_nUdCtlBlits = 0;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32((i32)g_pUdPix[160 * 240 + 120], (i32)UD_FILL,
               "the screen fill blends 0x64/0x64/0xC8 with key 0x50 (0x9B828)");
    CheckEqI32((i32)g_pUdPix[0], (i32)UD_FRAME, "the frame's top-left rect overwrites the pad pixel (0x9B82C)");
    CheckEqI32((i32)g_pUdPix[4 * 240 + 239], (i32)UD_FRAME, "  240 wide, 5 tall");
    CheckEqI32((i32)g_pUdPix[5 * 240 + 5], (i32)UD_FILL, "  and exactly 5 rows (5,5 is past both rails)");
    CheckEqI32((i32)g_pUdPix[319 * 240 + 120], (i32)UD_FRAME, "the bottom rail spans the middle at 120,319 (0x9B854)");
    CheckEqI32((i32)g_pUdPix[314 * 240 + 120], (i32)UD_FILL, "  and starts at 315, not 314");
    CheckEqI32((i32)g_pUdPix[160 * 240 + 239], (i32)UD_FRAME, "the right rail covers 239 at mid height (0x9B8A4)");
    CheckEqI32((i32)g_pUdPix[160 * 240 + 234], (i32)UD_FILL, "  and starts at 235, not 234");
    CheckEqI32(g_nUdCtlBlits, 2, "the entry path blits the close control again at its tail (0x9DD50)");

    /* The normalisation table, read back from the two globals it writes. */
    /* 0xBB7 = 2999, so the craft rule fires AT 3000 (sub 0), and 2999 is the last weapon number
     * (the >= 2000 branch tests n < 0xBB8). 2000 itself is not a category and stays 2000. */
    static const i32 anIn[]  = { 53, 54, 75, 76, 84, 85, 1000, 1001, 1999, 2000, 2001, 2999, 3000, 3001, 50 };
    static const i32 anCat[] = { 53, 3,  3,  4,  4,  85, 1000, 1,    1,    2000, 26,   26,   2,    2,    50 };
    static const i32 anSub[] = { 0,  0,  0,  0,  0,  0,  0,    1,    999,  0,    0,    0,    0,    1,    0 };
    for (i32 n = 0; n < 15; ++n) {
        g_nUdEntryNum = anIn[n];
        g_nUdSub = 0;
        memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
        Ui_DrawBaseInfo(&g_udEnv);
        CheckEqI32(g_nUdEntryNum, anCat[n], "normalised entry number");
        CheckEqI32(g_nUdSub, anSub[n], "  and its sub");
    }

    /* Category 26 (weapon): the 200 x 225 panel at (0,20), the pad untouched. */
    g_nUdEntryNum = 2500;
    g_udEntryPad.nOriginX = 33; /* the normalisation loop's entry 85 centred the pad; reset it */
    g_udEntryPad.nOriginY = 77;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32((i32)g_pUdPix[20 * 240 + 0], (i32)UD_FRAME, "the weapon panel's top rail at 0,20 (0x9B9E4)");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 199], (i32)UD_FRAME, "  200 wide");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 200], (i32)UD_FRAME, "the weapon panel's side rail at 200,20 (0x9BA30)");
    CheckEqI32((i32)g_pUdPix[244 * 240 + 200], (i32)UD_FRAME, "  225 tall to 244");
    CheckEqI32((i32)g_pUdPix[245 * 240 + 200], (i32)UD_FILL, "  and no further");
    CheckEqI32(g_udEntryPad.nOriginX, 33, "the weapon block never centres the pad (0x9B9E4 has no pad code)");
    CheckEqI32(g_udEntryPad.nOriginY, 77, "  or moves it");

    /* Category 2 (craft): glyph pal base 3, the 220-wide rails, the hangar divider at 80. */
    g_nUdEntryNum = 3005; /* sub 5 */
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32(g_nUdPalBase, 3, "the craft block forces Map_nGlyphPalBase to 3 (0x9BF60)");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 219], (i32)UD_FRAME, "the craft rail runs 220 wide to 219 (0x9C030)");
    CheckEqI32((i32)g_pUdPix[100 * 240 + 0], (i32)UD_FRAME, "the middle rail at 0,100 (0x9C07C)");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 80], (i32)UD_FRAME, "the inner divider at 80,20 (0x9C0A0)");
    CheckEqI32((i32)g_pUdPix[103 * 240 + 80], (i32)UD_FRAME, "  84 tall to 103");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 220], (i32)UD_FRAME, "the outer divider at 220,20 (0x9C0C8)");

    /* Category 1 (item): glyph pal base 34, the 52-wide strip at y 83, the dividers at 47 and 132. */
    g_nUdEntryNum = 1003; /* sub 3 */
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32(g_nUdPalBase, 0x22, "the item block forces Map_nGlyphPalBase to 34 (0x9C54C)");
    CheckEqI32((i32)g_pUdPix[83 * 240 + 0], (i32)UD_FRAME, "the item strip at 0,83 (0x9C608)");
    /* The strip's own right edge (51) sits inside the left divider's column (47..51), so the
     * strip is pinned at 46 -- the last column the divider does not cover. */
    CheckEqI32((i32)g_pUdPix[83 * 240 + 46], (i32)UD_FRAME, "  the strip reaches 46 (51 is the divider's column)");
    CheckEqI32((i32)g_pUdPix[83 * 240 + 52], (i32)UD_FILL, "  and nothing draws at 52");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 47], (i32)UD_FRAME, "the left divider at 47,20 (0x9C62C)");
    CheckEqI32((i32)g_pUdPix[87 * 240 + 47], (i32)UD_FRAME, "  68 tall to 87");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 132], (i32)UD_FRAME, "the right divider at 132,20 (0x9C654)");
    CheckEqI32((i32)g_pUdPix[204 * 240 + 132], (i32)UD_FRAME, "  185 tall to 204");

    /* Category 4 (alien): the pad centred on x AND y = 100 - h/2. */
    g_nUdEntryNum = 80;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32(g_udEntryPad.nOriginX, 115, "the alien pad is centred on x: 120 - 10/2 (0x9BB08)");
    CheckEqI32(g_udEntryPad.nOriginY, 96, "  and on y: 100 - 8/2 (0x9BB1C)");
    CheckEqI32((i32)g_pUdPix[94 * 240 + 113], (i32)UD_FRAME, "the border rect starts 2px out (0x9BB24)");
    CheckEqI32((i32)g_pUdPix[96 * 240 + 115], 0xABCD, "the entry pad is blitted over it (0x9BB58)");
    CheckEqI32((i32)g_pUdPix[200 * 240 + 0], (i32)UD_FRAME, "the alien frame rail at 0,200 (0x9BB5C)");

    /* Category 3 (facility): the pad centred on x only -- its own originY survives. */
    g_nUdEntryNum = 60;
    g_udEntryPad.nOriginX = 33;
    g_udEntryPad.nOriginY = 77;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32(g_udEntryPad.nOriginX, 115, "the facility pad is centred on x (0x9BCB4)");
    CheckEqI32(g_udEntryPad.nOriginY, 77, "  but its originY is left alone (0x9BC98 writes no +4)");
    CheckEqI32((i32)g_pUdPix[75 * 240 + 113], (i32)UD_FRAME, "the border rect follows the kept originY");

    /* Entries 85..92: the pad pinned to originY 25 and the 320-wide rail clipped to the surface. */
    g_nUdEntryNum = 86;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32(g_udEntryPad.nOriginY, 25, "the stat drawer pins originY to 25 (0x9D34C)");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 0], (i32)UD_FRAME, "its fourth rail is 320 wide (0x9D304)");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 239], (i32)UD_FRAME, "  clipped to the 240 columns");
    CheckEqI32((i32)g_pUdPix[200 * 240 + 0], (i32)UD_FRAME, "and the usual bottom rail at 0,200 (0x9D290)");

    /* Entries 93..97: the six text-panel rows, record plus number, in the ARM's row order. */
    UdPutRec(1715, "P0"); UdPutRec(1716, "P1"); UdPutRec(1717, "P2");
    UdPutRec(1718, "P3"); UdPutRec(1719, "P4"); UdPutRec(1720, "P5");
    g_nUdEntryNum = 93;
    g_udEntryPad.nOriginY = 77;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32(g_udEntryPad.nOriginY, 77, "the panel drawer leaves the pad's originY alone (0x9D79C)");
    CheckEqI32((i32)g_pUdPix[200 * 240 + 0], (i32)UD_FRAME, "its bottom rail at 0,200 (0x9D568)");
    Check(strcmp((char *)g_udTextPanel.apLines[0], "P0760") == 0, "row 0 is record 1715 + var_11C (0x9D5D4)");
    Check(strcmp((char *)g_udTextPanel.apLines[1], "P12") == 0,   "row 1 is record 1716 + var_118 (0x9D624)");
    Check(strcmp((char *)g_udTextPanel.apLines[2], "P21500") == 0,"row 2 is record 1717 + var_108 (0x9D674)");
    Check(strcmp((char *)g_udTextPanel.apLines[3], "P30") == 0,   "row 3 is record 1718 + var_10C (0x9D6BC)");
    Check(strcmp((char *)g_udTextPanel.apLines[4], "P414") == 0,  "row 4 is record 1719 + var_110 (0x9D710)");
    Check(strcmp((char *)g_udTextPanel.apLines[5], "P53") == 0,   "row 5 is record 1720 + var_114 (0x9D764)");

    /* Entries 98..103: the pad pinned to 50 and the 200-wide panel, no text-panel draw. */
    g_nUdEntryNum = 98;
    g_nUdCtlBlits = 0;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32(g_udEntryPad.nOriginY, 50, "the number drawer pins originY to 50 (0x9DC98)");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 199], (i32)UD_FRAME, "its panel rail is 200 wide (0x9DCE4)");
    CheckEqI32((i32)g_pUdPix[20 * 240 + 200], (i32)UD_FRAME, "with the side rail at 200,20 (0x9DD28)");
    CheckEqI32(g_nUdCtlBlits, 2, "and it ends on the close control like every other entry path");

    /* A number the dispatch does not know: fill and frame only, then the default tail. */
    g_nUdEntryNum = 50;
    g_nUdCtlBlits = 0;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32((i32)g_pUdPix[20 * 240 + 20], (i32)UD_FILL, "an unknown entry draws no category panel (def_9D460)");
    CheckEqI32((i32)g_pUdPix[0], (i32)UD_FRAME, "  while the entry frame still draws");
    CheckEqI32(g_nUdCtlBlits, 2, "  and the default case blits the close control (0x9DD50)");

    /* Guards the original does not have. */
    g_nUdEntryNum = 1003; /* item, sub 3 -- out of the object table */
    g_udEnv.nObjectCount = 2;
    memset(g_udSurf.pPixels, 0, (size_t)240 * 320u * 2u);
    Ui_DrawBaseInfo(&g_udEnv);
    CheckEqI32((i32)g_pUdPix[160 * 240 + 120], (i32)UD_FILL,
               "an item sub past the object table is skipped, not dereferenced (Map+0x108 guard)");
    CheckEqI32((i32)g_pUdPix[83 * 240 + 52], (i32)UD_FILL, "  and none of its frame rects draw (52 is past the strip)");
    g_udEnv.nObjectCount = 8;

    Ui_DrawBaseInfo(NULL);
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
    TestBuySellInit();
    TestBuySellBuildControls();
    TestTransferInitAndReset();
    TestTransferBuildAndMove();
    TestTransferCapacityGate();
    TestTransferCommit();
    TestTransferErrorDump();
    TestTransferDraw();
    TestBaseStripDrawer();
    TestGrantSiteRecovery();
    TestManufRefreshBaseTabs();
    TestManufCommitProduction();
    TestManufInit();
    TestManufBuildPanels();
    TestManufDraw();
    TestManufDrawFrame();
    TestResearchInit();
    TestResearchBuildPanels();
    TestResearchRebuild();
    TestResearchDraw();
    TestBuySellDraw();
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
    TestSoldierTransferPanels();
    TestSoldierDetail();
    TestOptionsAudioTab();
    TestSaveLoadSlots();
    TestEnterBuildMode();
    TestSetWaypoint();
    TestClearProductSlot();
    TestIncUnits();
    TestTogglePanelMode();
    TestBuySellConfirm();
    TestFundingBonus();
    TestScreenResetOpen();
    TestOpenSfxAndAltVariants();
    TestCancelTarget();
    TestSelectedAnswers();
    TestConfirmCraftTarget();
    TestFundingRecompute();
    TestFundingPanels();
    TestFundingReport();
    TestPlaneStatRows();
    TestFacStatusLine();
    TestUfopaedia();
    TestUfopaediaOpen();
    TestUiInitBaseInfo();
    TestPsiScreen();
    TestPsiScreenDraw();
    TestBaseInfoBuildPanels();
    TestBaseInfoInit();
    TestBaseInfoDraw();
    TestBaseAtUpdate();
    TestBaseAtDraw();
    TestBaseAtCloseThunk();
    TestPlaneScreenDraw();
    TestBaseFacNameGrid();
    TestPlaneCraftDetail();
    TestUiDrawBaseInfo();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
