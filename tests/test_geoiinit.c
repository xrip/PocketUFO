/* test_geoiinit.c - Geoscape_InitUI @ 0x857f4 against the shipped archives.
 *
 * The new-game path is run for real: the pads, sprites, palettes and research.dat come
 * out of main.dat / lang.dat, so the loaded geometry (earth100.pad is 800 x 400) and the
 * research bytes are independent ground truth, not port-internal expectations.
 */
#include "game/pufo_boot.h"
#include "game/pufo_font.h"
#include "game/pufo_options.h"
#include "game/pufo_save.h" /* PUFO_RESEARCH_TOPIC_COUNT */
#include "ui/pufo_geoiinit.h"
#include "ui/pufo_geouimanage.h"

#include <stdio.h>
#include <string.h>

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

static BootAssets g_assets;

static GeoscapeUiState g_ui;
static SmapView        g_view;
static SmapZoomSet     g_sets[3];
static i32             g_nScreenMode;
static u8              g_bGeoscapeUi;
static u8              g_bRoot34;
static u8              g_bRoot35;
static GameTime        g_time;
static ResearchTopicLive g_aTopics[PUFO_RESEARCH_TOPIC_COUNT];
static u16             g_nResearchEntries;
static BaseFacFlag     g_aFac[PUFO_BASEFAC_COUNT];
static u8              g_aFacilityGrid[480 + 144]; /* one base: type lane + occupancy lane */
static u8              g_aSiteState[8];
static i8              g_nBaseCount;
static MissionSiteRec  g_aSites[PUFO_MISSION_SITE_COUNT];
static u8              g_aRegionBuildCount[9];
static i8              g_nSaveVersion;
static PufoOptions     g_options;
static GeoscapeWorld   g_world;
static AssetReader     g_rdr;
static i32             g_nGlyphPalBase;
static i32             g_nUnk159DC4;
static i32             g_nStartNewCalls;

static void NoopClick(void) { }
static void StartNewProbe(void *pUser) { (void)pUser; ++g_nStartNewCalls; }

static bool LoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                     const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2);
    return true;
}

static bool AllSiteNamesEmpty(void)
{
    i32 i;
    for (i = 0; i < PUFO_MISSION_SITE_COUNT; ++i) {
        if (g_aSites[i].szName[0] != '\0') {
            return false;
        }
    }
    return true;
}

static void TestNewGame(void)
{
    GeoscapeInitEnv env;
    ObjectRecFull *pObj0, *pObj1, *pObj147, *pObj148;
    i32 i;

    printf("Geoscape_InitUI(0) against the shipped archives\n");

    /* Sentinels, so "untouched" is observable. */
    g_nScreenMode = 9;
    g_bGeoscapeUi = 9;
    g_bRoot34 = 9;
    g_bRoot35 = 9;
    g_nGlyphPalBase = 9;
    g_nUnk159DC4 = 9;
    g_nSaveVersion = 0;
    g_nResearchEntries = 0;
    g_nBaseCount = 1;
    g_time.nTotalSec = GAME_SECONDS_YEAR; /* exactly one year: 2000-01-01 */
    g_time.bPaused = 1;
    memset(g_aSiteState, 0xFF, sizeof(g_aSiteState));
    memset(g_aSites, 0, sizeof(g_aSites));
    for (i = 0; i < PUFO_MISSION_SITE_COUNT; ++i) {
        g_aSites[i].uField0 = 0xAAAAAAAAu;
    }
    pObj0   = g_assets.apObjects[0];
    pObj1   = g_assets.apObjects[1];
    pObj147 = g_assets.apObjects[147];
    pObj148 = g_assets.apObjects[148];
    pObj0->uPrice = 0xAAAAAAAAu;
    pObj0->szName[0] = 'X';
    pObj148->szName[0] = 'X';
    pObj1->uFirePower = 0x5A5A;
    g_view.nScrollX = 555;

    memset(&env, 0, sizeof(env));
    env.pUi             = &g_ui;
    env.pView           = &g_view;
    env.pSetsOut        = g_sets;
    env.pnScreenMode    = &g_nScreenMode;
    env.pbGeoscapeUi    = &g_bGeoscapeUi;
    env.pbRoot34        = &g_bRoot34;
    env.pbRoot35        = &g_bRoot35;
    env.pFont           = &g_fontSys;
    env.pRdr            = &g_rdr;
    env.pTextRecs       = g_assets.pTextRecs;
    env.pTime           = &g_time;
    env.apTopics        = g_aTopics;
    env.puResearchEntries = &g_nResearchEntries;
    env.paFacFlags      = g_aFac;
    env.apObjects       = g_assets.apObjects;
    env.nObjectCount    = (i32)g_assets.nObjectCount;
    env.pnGlyphPalBase  = &g_nGlyphPalBase;
    env.abFacilityGrid  = g_aFacilityGrid;
    env.pWorld          = &g_world;
    env.pbBaseCount     = &g_nBaseCount;
    env.pnUnk159DC4     = &g_nUnk159DC4;
    env.paMissionSites  = g_aSites;
    env.aRegionBuildCount     = g_aRegionBuildCount;
    env.pbSaveVersion   = &g_nSaveVersion;
    env.pOptions        = &g_options;
    for (i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        env.apfnRootClick[i] = NoopClick;
    }
    env.pfnCraftSelectClick = NoopClick;
    env.pfnRadarClick       = NoopClick;
    env.pfnStartNew         = StartNewProbe;
    env.pfnLoadPads         = LoadPads;

    g_ui.pFont        = &g_fontSys;
    g_ui.pbGeoscapeUi = &g_bGeoscapeUi;
    memset(&g_world, 0, sizeof(g_world));
    g_world.apSiteState = g_aSiteState;
    g_world.pTime       = &g_time;

    g_nStartNewCalls = 0;
    Geoscape_InitUI(&env, 0);

    printf("flags and allocations\n");
    CheckEqI32(g_nStartNewCalls, 1, "Game_StartNew hook ran once");
    {
        bool bAll1 = true;
        for (i = 0; i < 9; ++i) {
            if (g_aRegionBuildCount[i] != 1) { bAll1 = false; }
        }
        Check(bAll1, "the nine 0x18655D bytes are 1");
    }
    CheckEqI32(g_nScreenMode, 0, "screen id cleared");
    CheckEqI32(g_bGeoscapeUi, 1, "geoscape flag byte set");
    CheckEqI32(g_bRoot34, 0, "root +0x34 cleared");
    CheckEqI32(g_bRoot35, 0, "root +0x35 cleared");
    {
        bool bAll = true;
        for (i = 0; i < PUFO_GEOUI_PAD_SLOTS; ++i)   { if (g_ui.apPad[i] == NULL) { bAll = false; } }
        for (i = 0; i < PUFO_GEOUI_PAD_SLOTS; ++i)   { if (g_ui.apCtl[i] == NULL) { bAll = false; } }
        for (i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i)   { if (g_ui.apRootCtl[i] == NULL) { bAll = false; } }
        for (i = 0; i < PUFO_GEOUI_LAYER_PADS; ++i)  { if (g_ui.apLayerPad[i] == NULL) { bAll = false; } }
        Check(bAll, "13 pads + 13 controls + 13 root controls + 10 clock pads allocated");
    }
    Check(g_ui.pWorldSize != NULL && g_ui.pWorldSize->uW == 800 && g_ui.pWorldSize->uH == 400,
          "world size pair is 800 x 400");

    printf("pads loaded from main.dat\n");
    Check(g_ui.apPad[GEOUI_PAD_186B78]->pData != NULL, "earth100 pad loaded");
    CheckEqI32((i32)g_ui.apPad[GEOUI_PAD_186B78]->uWidth, 800, "earth100 width (320004-byte .pad)");
    CheckEqI32((i32)g_ui.apPad[GEOUI_PAD_186B78]->uHeight, 400, "earth100 height");
    Check(g_ui.apPad[GEOUI_PAD_186B74]->pData != NULL, "m100 pad loaded");
    Check(g_ui.apPad[GEOUI_PAD_186BA0]->pData != NULL, "research pad loaded");
    Check(g_ui.apPad[GEOUI_PAD_186BBC]->pData != NULL && g_ui.apPad[GEOUI_PAD_186BBC]->nOriginY == 236,
          "gamemenu pad loaded at origin y 236");
    Check(g_ui.apPad[GEOUI_PAD_187048]->pData != NULL && g_ui.apPad[GEOUI_PAD_187048]->nOriginY == 228,
          "bpanel pad loaded at origin y 228");
    Check(g_ui.apLayerPad[0]->pData != NULL && g_ui.apLayerPad[9]->pData != NULL,
          "clock digit pads b0..b9 loaded");

    printf("facility table\n");
    {
        bool bIdx = true;
        for (i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
            if (g_aFac[i].bFlag21 != (u8)i) { bIdx = false; }
        }
        Check(bIdx, "bFlag21 is the record index");
    }
    CheckEqI32((i32)g_aFac[0].uCost, 300000, "facility 0 cost");
    CheckEqI32((i32)g_aFac[10].uCost, 1300000, "facility 10 cost (the 0x13D620 literal as a number)");
    CheckEqI32((i32)g_aFac[16].uCost, 1800000, "facility 16 cost");
    Check(g_aFac[0].szName[0] != '\0', "facility 0 name from text record 106");
    Check(g_aFac[13].szName[0] != '\0', "facility 13 name from text record 463");

    printf("sprite group bases\n");
    /* nGlyphBase is the frame count before the first load, which is zero here because the
     * test does not run GameUI_InitStartup's own "font" load first. The invariant the
     * original enforces is the strict ordering of the captured bases. */
    Check(g_fontSys.nBaseBitsBase > g_fontSys.nGlyphBase
              && g_fontSys.nMapBitsBase > g_fontSys.nBaseBitsBase
              && g_fontSys.nBigObsBase > g_fontSys.nMapBitsBase
              && g_fontSys.nTotalSprFrames > g_fontSys.nBigObsBase,
          "font <= basebits < mapbits < bigobs < total, each captured before its load");
    CheckEqI32(g_nGlyphPalBase, 3, "Map_nGlyphPalBase = 3");

    printf("controls\n");
    {
        bool bClicks = true;
        for (i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
            if (g_ui.apRootCtl[i]->pfnClick != NoopClick) { bClicks = false; }
        }
        Check(bClicks && g_ui.apCtl[GEOUI_CTL_186B8C]->pfnClick == NoopClick,
              "13 root handlers plus the 186B8C craft-select handler stored");
    }
    CheckEqI32(g_ui.apRootCtl[0]->x, 0, "root 0 x");
    CheckEqI32(g_ui.apRootCtl[0]->y, 236, "root 0 y (cplus)");
    CheckEqI32(g_ui.apRootCtl[8]->x, 82, "root 8 x (coption)");
    CheckEqI32(g_ui.apRootCtl[12]->y, 298, "root 12 y (cfund)");
    CheckEqI32(g_ui.apRootCtl[2]->nMode, 1, "speed buttons are mode 1");
    CheckEqI32(g_ui.apRootCtl[0]->nMode, 0, "zoom button is mode 0");
    CheckEqI32(g_ui.apCtl[GEOUI_CTL_186B00]->x, 224, "bclose singleton at x 224");
    CheckEqI32(g_ui.apCtl[GEOUI_CTL_18704C]->x, 1, "18704C got descriptor 5 (memptyb at 1,1)");

    printf("zoom sets and viewport\n");
    Check(g_sets[0].pTiles == g_ui.apPad[GEOUI_PAD_186B74]
              && g_sets[0].pGrid == g_ui.apPad[GEOUI_PAD_186B78]
              && g_sets[0].pOverlay == g_ui.apPad[GEOUI_PAD_186C28],
          "level 0 layers: m100 / earth100 / e100n");
    CheckEqI32((i32)g_sets[1].uWorldW, 600, "level 1 world width");
    CheckEqI32((i32)g_sets[2].uWorldH, 236, "level 2 world height");
    CheckEqI32(g_view.nMode, 0, "viewport starts at level 0");
    CheckEqI32(g_view.nMapW, 800, "nMapW from the level-0 grid");
    CheckEqI32(g_view.nMapH, 400, "nMapH from the level-0 grid");
    CheckEqI32(g_view.nScrollX, 0, "scroll cleared in the new-game branch");

    printf("research\n");
    CheckEqI32((i32)g_nResearchEntries, PUFO_RESEARCH_TOPIC_COUNT, "entry count byte set to 144");
    {
        bool bZero = true;
        for (i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
            if (g_aTopics[i].wUnk6 != 0) { bZero = false; }
        }
        Check(bZero, "the u16 at +6 zeroed in every topic");
    }
    CheckEqI32(g_aTopics[143].abNeedItems[1], 0x1D,
               "research.dat really loaded (record 143 byte +0x0F is 0x1D in main.dat)");

    printf("clock, bases, objects\n");
    CheckEqI32(g_time.nYear, 2000, "GameTime_Decode ran on the one-year clock");
    Check(g_aSiteState[0] != 0xFF, "Base_ClearFacilityGridRow ran for the one base");
    CheckEqI32((i32)pObj1->uPrice, 800, "object 1 price 800");
    CheckEqI32((i32)g_assets.apObjects[119]->uPrice, 5000000, "object 119 price 5000000");
    CheckEqI32((i32)pObj0->uPrice, (i32)0xAAAAAAAAu, "object 0 price untouched (loop starts at 1)");
    CheckEqI32(pObj0->szName[0], 'X',
               "object 0 name untouched: 0x86A64 loads apObjects[counter+1], so slot 0 is never written");
    Check(pObj1->szName[0] != '\0' && pObj1->szName[0] != 'X', "object 1 name from text record 1447");
    Check(pObj147->szName[0] != '\0', "object 147 name from text record 1593");
    Check(pObj148->szName[0] != '\0' && pObj148->szName[0] != 'X',
          "object 148 name from text record 1594, the last of the 148 copies");
    /* The name is the record's first 36 bytes, and the copy lands on the name OBJECTS.DAT
     * already carries there, so reading a slot back gives the shipped name for that slot. */
    Check((i32)((char *)g_assets.apObjects[59]->szName - (char *)g_assets.apObjects[59]) == 0,
          "szName is at record offset 0");
    /* The whole mapping, checked against the loaded text table: slot j carries record
     * 1446+j. The longest of the 148 names is 27 bytes, so none is cut by the 36-byte field.
     * The names are the shipped Russian set (lang.dat TEXTS.DAT); record 1505 is "Sectoid
     * corpse" and 1523 is "Sectoid soldier". */
    i32 nNameOk = 0;
    for (i = 1; i <= 148; ++i) {
        const char *pWant = (const char *)g_assets.pTextRecs + PUFO_TEXT_REC_STRIDE * (1446 + i);
        if (strcmp(g_assets.apObjects[i]->szName, pWant) == 0) {
            ++nNameOk;
        }
    }
    CheckEqI32(nNameOk, 148, "every slot 1..148 carries text record 1446+slot");
    CheckEqI32(pObj1->uFirePower, 110, "apObjects[1].uFirePower = 110");
    CheckEqI32(g_nUnk159DC4, 10, "0x159DC4 = 10");

    printf("speed and panel\n");
    CheckEqI32(g_world.nTickSeconds, 5, "GameTime_Set5Sec ran (tick seconds 5)");
    CheckEqI32(g_ui.apRootCtl[2]->nState, 1, "the 5sec button is active");
    CheckEqI32(g_ui.apRootCtl[3]->nState, 0, "the 1min button is not");
    Check(g_ui.pStatusPanel != NULL && g_ui.pStatusPanel->nLineCount == 20
              && g_ui.pStatusPanel->nStyle == 13 && g_ui.pStatusPanel->nFont == 16
              && g_ui.pStatusPanel->nUnk7076 == 1 && g_ui.pStatusPanel->bFlag7073 == 1
              && g_ui.pStatusPanel->bUnk7088 == 100 && g_ui.pStatusPanel->pfnClick == NoopClick,
          "status panel built (20 lines, style 13, font 16, radar click wired)");

    printf("mission sites\n");
    Check(g_aSites[0].uField0 != 0xAAAAAAAAu, "0.cts read replaced the sentinel");
    Check(AllSiteNamesEmpty(),
          "all 56 names empty: lang.dat loads 1650 text records, so 1655..1710 are zero");

    printf("tail state\n");
    CheckEqI32(g_time.bPaused, 0, "clock unpaused");
    CheckEqI32(g_nSaveVersion, 3, "save format version 3");
}

static void TestModeOne(void)
{
    GeoscapeInitEnv env;

    printf("Geoscape_InitUI(1): the loaded-world path\n");

    memset(&env, 0, sizeof(env));
    env.pUi             = &g_ui;
    env.pView           = &g_view;
    env.pSetsOut        = g_sets;
    env.pnScreenMode    = &g_nScreenMode;
    env.pbGeoscapeUi    = &g_bGeoscapeUi;
    env.pbRoot34        = &g_bRoot34;
    env.pbRoot35        = &g_bRoot35;
    env.pFont           = &g_fontSys;
    env.pRdr            = &g_rdr;
    env.pTextRecs       = g_assets.pTextRecs;
    env.pTime           = &g_time;
    env.apTopics        = g_aTopics;
    env.puResearchEntries = &g_nResearchEntries;
    env.paFacFlags      = g_aFac;
    env.apObjects       = g_assets.apObjects;
    env.nObjectCount    = (i32)g_assets.nObjectCount;
    env.pnGlyphPalBase  = &g_nGlyphPalBase;
    env.abFacilityGrid  = g_aFacilityGrid;
    env.pWorld          = &g_world;
    env.pbBaseCount     = &g_nBaseCount;
    env.pnUnk159DC4     = &g_nUnk159DC4;
    env.paMissionSites  = g_aSites;
    env.aRegionBuildCount     = g_aRegionBuildCount;
    env.pbSaveVersion   = &g_nSaveVersion;
    env.pOptions        = &g_options;
    env.pfnStartNew     = StartNewProbe;
    env.pfnLoadPads     = LoadPads;

    g_view.nScrollX = 123; /* the loaded save's scroll must survive */
    g_nSaveVersion = 0;
    g_nStartNewCalls = 0;
    Geoscape_InitUI(&env, 1);

    CheckEqI32(g_nStartNewCalls, 0, "Game_StartNew hook not called in mode 1");
    CheckEqI32(g_view.nScrollX, 123, "scroll preserved");
    CheckEqI32(g_nSaveVersion, 3, "save version re-stamped");

    /* Cleanup: mode 1 allocated the whole set again, exactly as the original does after
     * LoadGame's teardown. */
    {
        GeoscapeRoster roster;
        memset(&roster, 0, sizeof(roster));
        GeoscapeUi_FreeAll(&g_ui, &roster, 0);
    }
}

static void TestFreeAllPairing(void)
{
    GeoscapeRoster roster;

    printf("Geoscape_InitUI pairs with GeoscapeUi_FreeAll\n");

    memset(&roster, 0, sizeof(roster));
    GeoscapeUi_FreeAll(&g_ui, &roster, 0);

    Check(g_ui.apPad[0] == NULL && g_ui.apCtl[0] == NULL && g_ui.apRootCtl[0] == NULL
              && g_ui.apLayerPad[0] == NULL && g_ui.pWorldSize == NULL
              && g_ui.pStatusPanel == NULL,
          "everything InitUI allocated is released");
}

int main(void)
{
    const char *const apsz[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    if (!Boot_LoadStartup(&g_assets, apsz)) {
        printf("FAIL: Boot_LoadStartup\n");
        return 1;
    }
    AssetReader_Clear(&g_rdr);

    TestNewGame();
    TestFreeAllPairing(); /* mode 1 runs on the freed state, as LoadGame's path does */
    TestModeOne();

    Boot_Shutdown(&g_assets);
    printf("%s\n", g_nFailures == 0 ? "all geoiinit checks passed" : "FAILURES");
    return g_nFailures == 0 ? 0 : 1;
}
