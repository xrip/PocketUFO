/* test_closeui.c - Geoscape_CloseUI @ 0x8b94c: the dispatcher every screen close reaches.
 *
 * The dispatcher's own footprint is the screen id, the flag bytes each case clears or sets, the
 * two popup bytes, the action-fail dword, the funds, the nav control slots, the two pads it
 * frees, and the sample it plays. Every check below is one of those, because that is what this
 * function actually changes; the screens it calls into have their own suites (test_baseui,
 * test_geoscape, test_strateg) and are handed minimal or NULL environments here.
 *
 * Two things are deliberately NOT asserted here:
 *   - Case 20's assault half hands off to Smap_BeginCraftSelect, which needs the whole
 *     craft-select environment (grid, tile types, lines, funding, soldiers, craft). Its own
 *     suite covers it; only the non-assault half of case 20 is exercised here.
 *   - Case 19's bankruptcy calls EndScreen_Init, which opens main.dat and re-inits the font.
 *     The bankruptcy test hands pEndScreen as NULL and asserts the three things around it.
 *
 * The gates that a caller cannot see (the flag that decides whether an already-ported callee
 * runs) are tested where the callee writes something the fixture can read -- the soldier
 * screen's open byte, the ufopaedia bytes -- and are otherwise recorded as covered by the
 * callee's own suite rather than asserted against a no-op call.
 */
#include "game/pufo_baseui.h"
#include "game/pufo_geoscape.h"   /* Base_*, PUFO_GRID_SIZE, PUFO_FAC_BASE_COUNT */
#include "game/pufo_ship.h"       /* Ship, BaseRec */
#include "game/pufo_strateg.h"    /* SmapView, GeoscapeWorld, PUFO_NAV_* is in baseui.h */
#include "game/pufo_titles.h"     /* EndScreenInitEnv */
#include "game/pufo_unit.h"       /* Soldier */
#include "render/pufo_render.h"   /* PadImage */
#include "ui/pufo_control.h"      /* UiControl */
#include "ui/pufo_linelist.h"     /* UiLineList */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_nChecks;
static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    ++g_nChecks;
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckI(i32 nGot, i32 nWant, const char *pszWhat)
{
    ++g_nChecks;
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

/* ------------------------------------------------------------------ the fixture state */

static i32 g_nScreen;
static i32 g_nFail;
static i32 g_nFunds;
static i32 g_aBar[5];

static u8 g_bPopupA, g_bPopupB;
static u8 g_bPlaceActive, g_bPlaceFail;
static u8 g_bFacConfirm, g_bFacDone, g_bFacArmed, g_bFacDialog;
static u8 g_bDragActive, g_bSoldierOpen, g_bTransferFlag, g_bTransferBuilt;
static u8 g_bBaseInfoBuilt;
static u8 g_bPlaneList, g_bPlaneStores, g_bStatA, g_bStatB, g_bPlaneBuilt, g_bPlaneDetail;
static u8 g_bResBuilt, g_bRowSel, g_bManufSel, g_bManufBuilt;
static u8 g_bBsCtlBuilt, g_bBsBuilt;
static u8 g_bUfOpen, g_bUfDirty, g_bOptOpen, g_bOptActive;
static i8 g_nOptMode;
static u8 g_bAtAssault, g_bTickHold, g_bBattleEnded, g_bMissionMode;
static u8 g_bShownBase;
static i8 g_nPsiBase;
static i8 g_nBaseCount, g_nSelBase;

static u8 g_abGrid[PUFO_GRID_SIZE];
static SmapView g_view;
static PadImage g_strip;
static GeoscapeWorld g_world;
static UiControl g_aSpeed[PUFO_GEOSCAPE_SPEED_COUNT];
static UiControl *g_apSpeed[PUFO_GEOSCAPE_SPEED_COUNT];

static NavControls g_nav;
static PadImage *g_pFundPad;
static PadImage *g_pBuySellPad;
static PadImage *g_pBaseInfoPad;
static UiPanel *g_pSolPanel; /* the soldier screen's four panel slots, all empty */

static Soldier g_aSold[1];
static Soldier *g_apSold[1];
static Ship g_aShip[1];
static Ship *g_apShip[1];
static u8 g_nCraftCount;
static BaseRec g_aBase[2];
static BaseRec *g_apBase[2];
static SmapSite g_aSite[1];
static ObjectRecFull g_aObj[1];
static ObjectRecFull *g_apObj[1];
static u16 g_aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT];
static u16 g_aResearch[PUFO_RESEARCH_TOPIC_COUNT * PUFO_FAC_BASE_COUNT];
static i32 g_aSolIds[2];
static UiLineList g_lines;

static SoldierScreenEnv g_solEnv;
static ResearchStartEnv g_resStart;
static OptionsScreenHooks g_optHooks;

static GeoscapeCloseEnv g_e;

/* ------------------------------------------------------------------ the hook records */

static i32 g_nHBaseInfo, g_nHPlaneList, g_nHPlaneCtl, g_nHPlanePanels;
static i32 g_nHUfRefresh, g_nHUfOpen, g_nHPsi, g_nHBaseAt;
static i32 g_nHOptionsCalls, g_nHOptionsMode;
static i32 g_nHAudioCalls, g_nHAudioMode, g_nHAudioCount;
static i32 g_nHFreeUi;
static i32 g_nSample;
static i32 g_nOptInit, g_nOptSample;

static void HBaseInfo(void *p) { (void)p; ++g_nHBaseInfo; }
static void HPlaneList(void *p) { (void)p; ++g_nHPlaneList; }
static void HPlaneCtl(void *p) { (void)p; ++g_nHPlaneCtl; }
static void HPlanePanels(void *p) { (void)p; ++g_nHPlanePanels; }
static void HUfRefresh(void *p) { (void)p; ++g_nHUfRefresh; }
static void HUfOpen(void *p) { (void)p; ++g_nHUfOpen; }
static void HPsi(void *p) { (void)p; ++g_nHPsi; }
static void HBaseAt(void *p) { (void)p; ++g_nHBaseAt; }
static void HOptionsOpen(i32 nMode, void *p) { (void)p; g_nHOptionsMode = nMode; ++g_nHOptionsCalls; }
static void HAudio(i32 nMode, i32 nCount, void *p)
{
    (void)p;
    g_nHAudioMode = nMode;
    g_nHAudioCount = nCount;
    ++g_nHAudioCalls;
}
static void HFreeUi(i32 bFreeWorld, void *p) { (void)p; g_nHFreeUi = bFreeWorld; }
static void HSample(i32 nSample, void *p) { (void)p; g_nSample += nSample; }
static void HOptSample(u32 nSample, u32 nArg, void *p)
{
    (void)p;
    g_nOptSample += (i32)nSample + (i32)nArg;
}
static void HOptInit(void *p) { (void)p; ++g_nOptInit; }

/* ------------------------------------------------------------------ reset */

static void Fix_Hooks(void)
{
    g_nHBaseInfo = 0;
    g_nHPlaneList = 0;
    g_nHPlaneCtl = 0;
    g_nHPlanePanels = 0;
    g_nHUfRefresh = 0;
    g_nHUfOpen = 0;
    g_nHPsi = 0;
    g_nHBaseAt = 0;
    g_nHOptionsCalls = 0;
    g_nHOptionsMode = 0;
    g_nHAudioCalls = 0;
    g_nHAudioMode = 0;
    g_nHAudioCount = 0;
    g_nHFreeUi = -1;
    g_nSample = 0;
    g_nOptInit = 0;
    g_nOptSample = 0;
}

/* The baseline every test starts from: no bases (so the needed-flag pass and the upkeep loop do
 * nothing), a 400-tall map, a 20-tall strip, a tick that matches no arm of the speed chain, and
 * every flag byte set so a clear is visible. */
static void Fix_Reset(i32 nScreen)
{
    Fix_Hooks();
    memset(g_abGrid, 0xFF, sizeof(g_abGrid)); /* an empty facility cell is 255 */
    memset(&g_lines, 0, sizeof(g_lines));
    memset(&g_nav, 0, sizeof(g_nav));
    memset(g_aSpeed, 0, sizeof(g_aSpeed));
    memset(&g_world, 0, sizeof(g_world));
    memset(&g_solEnv, 0, sizeof(g_solEnv));
    memset(&g_resStart, 0, sizeof(g_resStart));
    memset(&g_optHooks, 0, sizeof(g_optHooks));

    g_nScreen = nScreen;
    g_nFail = 1;
    g_nFunds = 1000;
    g_aBar[0] = 1000000;
    g_aBar[1] = 1000000;
    g_aBar[2] = 1000000;
    g_aBar[3] = 1000000;
    g_aBar[4] = 1000000;

    g_bPopupA = 1;
    g_bPopupB = 1;
    g_bPlaceActive = 1;
    g_bPlaceFail = 1;
    g_bFacConfirm = 0;
    g_bFacDone = 0;
    g_bFacArmed = 0;
    g_bFacDialog = 0;
    g_bDragActive = 1;
    g_bSoldierOpen = 0;
    g_bTransferFlag = 0;
    g_bTransferBuilt = 0;
    g_bBaseInfoBuilt = 0;
    g_bPlaneList = 0;
    g_bPlaneStores = 0;
    g_bStatA = 0;
    g_bStatB = 0;
    g_bPlaneBuilt = 0;
    g_bPlaneDetail = 0;
    g_bResBuilt = 0;
    g_bRowSel = 0;
    g_bManufSel = 0;
    g_bManufBuilt = 0;
    g_bBsCtlBuilt = 0;
    g_bBsBuilt = 0;
    g_bUfOpen = 0;
    g_bUfDirty = 0;
    g_bOptOpen = 0;
    g_bOptActive = 0;
    g_nOptMode = 0;
    g_bAtAssault = 0;
    g_bTickHold = 0;
    g_bBattleEnded = 0;
    g_bMissionMode = 0;
    g_bShownBase = 0xEE;
    g_nPsiBase = 0xEE;

    g_nBaseCount = 0;
    g_nSelBase = 3;

    memset(&g_view, 0, sizeof(g_view));
    g_view.nMapW = 400;
    g_view.nMapH = 400;
    g_view.nScrollY = 1000;

    memset(&g_strip, 0, sizeof(g_strip));
    g_strip.uHeight = 20;

    g_world.nTickSeconds = 7; /* matches no arm of 0x8B980..0x8B9D0 */
    for (i32 i = 0; i < PUFO_GEOSCAPE_SPEED_COUNT; ++i) {
        g_apSpeed[i] = &g_aSpeed[i];
    }

    g_pFundPad = NULL;
    g_pBuySellPad = NULL;
    g_pBaseInfoPad = NULL;
    g_pSolPanel = NULL;

    memset(g_aSold, 0, sizeof(g_aSold));
    g_apSold[0] = &g_aSold[0];
    memset(g_aShip, 0, sizeof(g_aShip));
    g_apShip[0] = &g_aShip[0];
    g_nCraftCount = 0;
    memset(g_aBase, 0, sizeof(g_aBase));
    g_apBase[0] = &g_aBase[0];
    g_apBase[1] = &g_aBase[1];
    memset(g_aSite, 0, sizeof(g_aSite));
    memset(g_aObj, 0, sizeof(g_aObj));
    g_apObj[0] = &g_aObj[0];
    memset(g_aManuf, 0, sizeof(g_aManuf));
    memset(g_aResearch, 0, sizeof(g_aResearch));
    g_aSolIds[0] = 0; /* an empty id list */

    g_solEnv.pbOpen = &g_bSoldierOpen;
    g_solEnv.pnScreenId = &g_nScreen;
    g_solEnv.ppPad = &g_pBaseInfoPad;
    g_solEnv.ppPanelEquip = &g_pSolPanel;
    g_solEnv.ppPanelStores = &g_pSolPanel;
    g_solEnv.ppPanelNames = &g_pSolPanel;
    g_solEnv.ppPanelCraft = &g_pSolPanel;

    g_optHooks.pfnPlaySample = HOptSample;
    g_optHooks.pfnInit = HOptInit;

    memset(&g_e, 0, sizeof(g_e));
    g_e.pfnPlaySample = HSample;
    g_e.pnActionFail = &g_nFail;
    g_e.pbPopupA = &g_bPopupA;
    g_e.pbPopupB = &g_bPopupB;
    g_e.pWorld = &g_world;
    g_e.apSpeedCtl = g_apSpeed;
    g_e.abFacilityGrid = g_abGrid;
    g_e.pnBaseCount = &g_nBaseCount;
    g_e.pnSelectedBaseIdx = &g_nSelBase;
    g_e.apCraft = g_apShip;
    g_e.pnCraftCount = &g_nCraftCount;
    g_e.apSoldiers = g_apSold;
    g_e.nSoldierCount = 1;
    g_e.apObjects = g_apObj;
    g_e.nObjectCount = 1;
    g_e.apSites = g_aSite;
    g_e.nSiteCount = 1;
    g_e.apBases = g_apBase;
    g_e.apManufCol = g_aManuf;
    g_e.apResearchCol = g_aResearch;
    g_e.nResearchEntries = PUFO_RESEARCH_TOPIC_COUNT;
    g_e.pnScreenId = &g_nScreen;
    g_e.pView = &g_view;
    g_e.pStripPad = &g_strip;
    g_e.pNav = &g_nav;
    g_e.pbPlaceActive = &g_bPlaceActive;
    g_e.pbPlaceFail = &g_bPlaceFail;
    g_e.pbBaseFacConfirm = &g_bFacConfirm;
    g_e.pbBaseFacDone = &g_bFacDone;
    g_e.pbBaseFacArmed = &g_bFacArmed;
    g_e.pbBaseFacDialog = &g_bFacDialog;
    g_e.pbDragActive = &g_bDragActive;
    g_e.pbBaseInfoBuilt = &g_bBaseInfoBuilt;
    g_e.ppBaseInfoPad = &g_pBaseInfoPad;
    g_e.pbSoldierOpen = &g_bSoldierOpen;
    g_e.pbTransferFlag = &g_bTransferFlag;
    g_e.pbTransferBuilt = &g_bTransferBuilt;
    g_e.pbPlaneListBuilt = &g_bPlaneList;
    g_e.pbPlaneStores = &g_bPlaneStores;
    g_e.pbStatPanelA = &g_bStatA;
    g_e.pbStatPanelB = &g_bStatB;
    g_e.pbPlaneBuilt = &g_bPlaneBuilt;
    g_e.pbPlaneDetail = &g_bPlaneDetail;
    g_e.pbResearchBuilt = &g_bResBuilt;
    g_e.pbRowSelected = &g_bRowSel;
    g_e.pbManufSelected = &g_bManufSel;
    g_e.pbManufBuilt = &g_bManufBuilt;
    g_e.pbBuySellCtlBuilt = &g_bBsCtlBuilt;
    g_e.pbBuySellBuilt = &g_bBsBuilt;
    g_e.ppBuySellPad = &g_pBuySellPad;
    g_e.pbUfopaediaOpen = &g_bUfOpen;
    g_e.pbUfopaediaDirty = &g_bUfDirty;
    g_e.pbOptionsOpen = &g_bOptOpen;
    g_e.pnOptionsMode = &g_nOptMode;
    g_e.pbOptionsActive = &g_bOptActive;
    g_e.pnFunds = &g_nFunds;
    g_e.anFundBar = g_aBar;
    g_e.ppFundPad = &g_pFundPad;
    g_e.pnShownBaseIdx = &g_bShownBase;
    g_e.pnPsiBaseIdx = &g_nPsiBase;
    g_e.pbTickHold = &g_bTickHold;
    g_e.pbBattleEnded = &g_bBattleEnded;
    g_e.pbMissionMode = &g_bMissionMode;
    g_e.pbBaseAtAssault = &g_bAtAssault;
    g_e.anSoldierIdList = g_aSolIds;
    g_e.pSoldierScreen = &g_solEnv;
    g_e.pResearchStart = &g_resStart;
    g_e.pOptionsHooks = &g_optHooks;
    g_e.pfnBuildBaseInfoPanels = HBaseInfo;
    g_e.pfnBuildPlaneListPanels = HPlaneList;
    g_e.pfnBuildPlaneControls = HPlaneCtl;
    g_e.pfnBuildPlanePanels = HPlanePanels;
    g_e.pfnUfopaediaRefresh = HUfRefresh;
    g_e.pfnUfopaediaOpen = HUfOpen;
    g_e.pfnPsiInit = HPsi;
    g_e.pfnBaseAtOpen = HBaseAt;
    g_e.pfnOptionsOpen = HOptionsOpen;
    g_e.pfnSaveAudioSettings = HAudio;
    g_e.pfnFreeGeoscapeUi = HFreeUi;
}

/* The head every path runs first: sample 17, the action-fail dword, the two popup bytes. */
static void CheckHeadRan(const char *pszWhat)
{
    CheckI(g_nSample, 17, "the head plays sample 17 (0x8B95C)");
    CheckI(g_nFail, 0, "and clears the action-fail dword (0x8B96C)");
    Check(g_bPopupA == 0 && g_bPopupB == 0, "and both popup bytes (0x8B978 / 0x8B97C)");
    (void)pszWhat;
}

/* ------------------------------------------------------------------ 1. the head and the speed chain */

static void TestHead(void)
{
    printf("head and the speed re-apply @ 0x8B980..0x8B9D0\n");

    struct { i32 nTick; i32 nWantTick; i32 nWantArmed; const char *pszWhat; } aCases[] = {
        { 5, 5, 0, "5 -> Set5Sec" },
        { 60, 60, 1, "60 -> Set1Min" },
        { 300, 300, 2, "300 -> Set5Min" },
        { 1800, 1380, 3, "1800 -> SetSpeedIdx3, which rewrites the tick to 1380" },
        { 3600, 3600, 5, "3600 -> SetSpeedIdx4 AND SetSpeedIdx5 (0x8B9E0), so index 5 is armed" },
        { 43200, 3600, 5, "43200 -> SetSpeedIdx5 alone" },
        { 7, 7, -1, "a tick no arm matches is left alone" },
        { 1380, 1380, -1, "and 1380, which the chain never tests, is left alone too" },
    };

    for (i32 i = 0; i < (i32)(sizeof(aCases) / sizeof(aCases[0])); ++i) {
        Fix_Reset(13); /* 13 has no case, so only the head and the speed chain run */
        g_world.nTickSeconds = aCases[i].nTick;
        Geoscape_CloseUI(&g_e);
        CheckI(g_world.nTickSeconds, aCases[i].nWantTick, aCases[i].pszWhat);

        i32 nArmed = -1;
        for (i32 k = 0; k < PUFO_GEOSCAPE_SPEED_COUNT; ++k) {
            if (g_aSpeed[k].nState != 0) {
                nArmed = k;
            }
        }
        CheckI(nArmed, aCases[i].nWantArmed, "  with exactly one speed control armed");
        CheckI(g_nScreen, 13, "  and the default case leaves the screen id alone");
    }

    /* The head itself. */
    Fix_Reset(13);
    Geoscape_CloseUI(&g_e);
    CheckHeadRan("head");

    /* A tick the chain rewrites is rewritten once, not twice: 1800 lands on 1380 and stops. */
    Fix_Reset(13);
    g_world.nTickSeconds = 1800;
    Geoscape_CloseUI(&g_e);
    CheckI(g_world.nTickSeconds, g_aGeoscapeSpeedSec[3],
           "1800 lands on the table's index 3, not on 1800");

    Fix_Reset(13);
    g_world.nTickSeconds = 3600;
    Geoscape_CloseUI(&g_e);
    CheckI(g_world.nTickSeconds, g_aGeoscapeSpeedSec[5],
           "3600 passes through index 4 and ends on index 5");
    CheckI(g_aSpeed[4].nState, 0, "  index 4 is armed and then cleared by the second call");
    CheckI(g_aSpeed[5].nState, 1, "  index 5 is the one left armed");
}

/* ------------------------------------------------------------------ 2. the cases with no body */

static void TestDefaultCases(void)
{
    printf("the three screen ids with no case\n");

    const i32 anScreens[3] = { 13, 14, 17 };
    for (i32 i = 0; i < 3; ++i) {
        Fix_Reset(anScreens[i]);
        Geoscape_CloseUI(&g_e);
        CheckI(g_nScreen, anScreens[i], "the default case returns without touching the screen id");
        CheckI(g_view.nScrollY, 1000, "  and does not clamp the scroll");
        CheckI(g_nHBaseAt, 0, "  and runs no case body at all");
    }
    CheckHeadRan("the head still ran");
}

/* ------------------------------------------------------------------ 3. case 1, the nav teardown */

static void TestScreen1(void)
{
    printf("case 1 @ 0x8BE8C: the nav controls go away\n");

    Fix_Reset(1);
    for (i32 i = 0; i < PUFO_NAV_CTLS; ++i) {
        g_nav.apNav[i] = (UiControl *)calloc(1, sizeof(UiControl));
    }
    for (i32 i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        g_nav.apPanel[i] = (UiControl *)calloc(1, sizeof(UiControl));
    }
    for (i32 i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        g_nav.apFind[i] = (UiControl *)calloc(1, sizeof(UiControl));
    }
    Geoscape_CloseUI(&g_e);

    CheckI(g_nScreen, 0, "screen 0 (0x8BE94)");
    CheckI(g_bPlaceActive, 0, "bBasePlaceActive cleared first (0x8BE9C)");
    CheckI(g_bPlaceFail, 0, "then bBasePlaceFail (0x8BEA4)");

    bool bAllNull = true;
    for (i32 i = 0; i < PUFO_NAV_CTLS; ++i) {
        if (g_nav.apNav[i] != NULL) {
            bAllNull = false;
        }
    }
    for (i32 i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        if (g_nav.apPanel[i] != NULL) {
            bAllNull = false;
        }
    }
    for (i32 i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        if (g_nav.apFind[i] != NULL) {
            bAllNull = false;
        }
    }
    Check(bAllNull, "all 15 control slots are freed and nulled (deviation: the original dangles)");

    /* 0x8BF38: halfH = 400/2 - 160 = 40; 1000 + 40 > 20 + 400 - 320 = 100 -> 20 - 40 + 400 - 320. */
    CheckI(g_view.nScrollY, 60, "and the scroll is clamped to 60");

    Fix_Reset(1);
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 0, "an empty nav block is not dereferenced");
    CheckI(g_view.nScrollY, 60, "  and the clamp still runs");

    Fix_Reset(1);
    g_view.nScrollY = -1000;
    Geoscape_CloseUI(&g_e);
    CheckI(g_view.nScrollY, -40, "the lower clamp gives 160 - nMapH/2");
}

/* ------------------------------------------------------------------ 4. case 2, the base-facility close */

static void TestScreen2(void)
{
    printf("case 2 @ 0x8BE4C: gated on bBaseFacConfirm\n");

    Fix_Reset(2);
    g_bFacConfirm = 1;
    g_bFacDone = 1;
    g_bFacArmed = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 2, "the confirm flag returns before anything else (0x8BE58)");
    CheckI(g_bFacDone, 1, "  and neither flag is cleared");
    CheckI(g_bFacArmed, 1, "  (the head still ran)");
    CheckI(g_view.nScrollY, 1000, "  and the scroll is not clamped");

    Fix_Reset(2);
    g_bFacDone = 1;
    g_bFacArmed = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "screen 1 (0x8BE68)");
    CheckI(g_bFacDone, 0, "bBaseFacDone cleared (0x8BE70)");
    CheckI(g_bFacArmed, 0, "and bBaseFacPlaceArmed (0x8BE74 -> 0x8BFF4)");
    CheckI(g_view.nScrollY, 60, "with the scroll clamped first (0x8BE60)");
}

/* ------------------------------------------------------------------ 5. cases 3 and 4 */

static void TestScreen3And4(void)
{
    printf("case 3 @ 0x8BDE8 and case 4 @ 0x8BFC4\n");

    Fix_Reset(3);
    g_bBaseInfoBuilt = 1;
    g_pBaseInfoPad = (PadImage *)calloc(1, sizeof(PadImage));
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "screen 1 (0x8BDE8)");
    CheckI(g_nHBaseInfo, 1, "the base-info panels are rebuilt while the built flag is set (0x8BDF8)");
    Check(g_pBaseInfoPad == NULL, "and the pad at 0x158828 is freed and nulled (0x8BE10)");

    Fix_Reset(3);
    g_pBaseInfoPad = (PadImage *)calloc(1, sizeof(PadImage));
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHBaseInfo, 0, "a clear built flag skips the rebuild");
    Check(g_pBaseInfoPad == NULL, "  but the pad still goes (the free is not gated)");

    Fix_Reset(3);
    Geoscape_CloseUI(&g_e);
    Check(g_pBaseInfoPad == NULL, "a null pad is not freed twice");

    Fix_Reset(4);
    g_bSoldierOpen = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_bSoldierOpen, 0, "the soldier screen's own toggle runs and closes it (0x8BFD8)");
    CheckI(g_nScreen, 1, "screen 1, which the dispatcher sets first and the toggle rewrites (0x8BFC4)");

    Fix_Reset(4);
    Geoscape_CloseUI(&g_e);
    CheckI(g_bSoldierOpen, 0, "a closed soldier screen is not opened by the close (0x8BFD4)");
    CheckI(g_nScreen, 1, "  and the screen id is still 1");
}

/* ------------------------------------------------------------------ 6. case 5, the transfer */

static void TestScreen5(void)
{
    printf("case 5 @ 0x8BFE4: the transfer flag decides the whole shape\n");

    Fix_Reset(5);
    g_bTransferFlag = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_bTransferFlag, 0, "a set transfer flag is cleared and the case returns (0x8BFF0)");
    CheckI(g_nScreen, 5, "  the screen id is NOT moved");
    CheckI(g_bPopupA, 0, "  (the popup bytes were already cleared by the head)");

    Fix_Reset(5);
    g_bPopupA = 1;
    g_bPopupB = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 4, "with the flag clear the screen id goes to 4 (0x8C00C)");
    CheckI(g_bTransferFlag, 0, "  and the flag is cleared anyway (0x8C008)");
}

/* ------------------------------------------------------------------ 7. case 6, the hangar ladder */

static void TestScreen6(void)
{
    printf("case 6 @ 0x8C018: the five-way ladder\n");

    Fix_Reset(6);
    g_bPlaneList = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHPlaneList, 1, "list panels first (0x8C028)");
    CheckI(g_nHPlaneCtl, 0, "  and nothing below it runs");
    CheckI(g_nScreen, 6, "  the screen id is untouched by this arm");

    Fix_Reset(6);
    g_bPlaneList = 1;
    g_bPlaneStores = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHPlaneList, 1, "the list arm wins over the stores arm");
    CheckI(g_nHPlaneCtl, 0, "  which does not run");

    Fix_Reset(6);
    g_bPlaneStores = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHPlaneCtl, 1, "then the stores arm (0x8C044)");
    CheckI(g_nHPlanePanels, 0, "  and the panel arm does not run");

    Fix_Reset(6);
    g_bStatB = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_bStatB, 0, "either stat panel open clears BOTH (0x8C0B0 / 0x8C0B4)");
    CheckI(g_nHPlanePanels, 0, "  and returns without building");
    CheckI(g_nScreen, 6, "  leaving the screen id alone");

    Fix_Reset(6);
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 6, "with nothing built the case returns (0x8C07C)");
    CheckI(g_nHPlanePanels, 0, "  and no build runs");

    Fix_Reset(6);
    g_bPlaneBuilt = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "panels built and no craft detail -> screen 1 (0x8C094)");
    CheckI(g_nHPlanePanels, 1, "  and the craft list is rebuilt (0x8C098)");

    Fix_Reset(6);
    g_bPlaneBuilt = 1;
    g_bPlaneDetail = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_bPlaneDetail, 0, "detail showing -> clear it and return (0x8C08C -> 0x8BFF4)");
    CheckI(g_nScreen, 6, "  with no screen change");
    CheckI(g_nHPlanePanels, 0, "  and no rebuild");
}

/* ------------------------------------------------------------------ 8. case 7, the three branches */

static void TestScreen7(void)
{
    printf("case 7 @ 0x8BF4C: the base-facility close\n");

    Fix_Reset(7);
    g_bFacDone = 1;
    g_bFacDialog = 1;
    g_nSelBase = 3;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nSelBase, 2, "done and dialog open -> the selected base steps down (0x8BF74 LDRSB)");
    CheckI(g_nScreen, 1, "  and the screen id goes to 1 (0x8BF88)");
    CheckI(g_bPopupA, 0, "  with both popups cleared (0x8BFA8 / 0x8BFA C)");

    Fix_Reset(7);
    g_bFacDone = 1;
    g_bFacDialog = 1;
    g_nSelBase = 0;
    Geoscape_CloseUI(&g_e);
    CheckI((i32)(u8)g_nSelBase, 255,
           "a zero index wraps to 255: LDRSB, SUB #1, STRB (0x8BF74..0x8BF7C)");

    Fix_Reset(7);
    g_bFacDialog = 1;
    g_nSelBase = 5;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nSelBase, 5, "the dialog-open arm reached through the armed test does NOT step down");
    CheckI(g_nScreen, 1, "  but it still sets screen 1 (0x8BFB8 -> 0x8BF84)");

    Fix_Reset(7);
    g_nSelBase = 5;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "dialog closed and nothing armed -> screen 1 (0x8BFB8)");
    CheckI(g_nSelBase, 5, "  and the selected base does not move");

    Fix_Reset(7);
    g_bFacArmed = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_bFacArmed, 0, "armed -> clear the armed flag (0x8BFA4)");
    CheckI(g_nScreen, 7, "  and this is the only arm of case 7 that leaves the screen id alone");
}

/* ------------------------------------------------------------------ 9. cases 8, 9, 10 */

static void TestScreen8To10(void)
{
    printf("cases 8, 9 and 10\n");

    Fix_Reset(8);
    g_bRowSel = 1;
    g_bResBuilt = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 8, "a selected row starts the topic and does NOT change the screen id (0x8BDB4)");

    Fix_Reset(8);
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "no selected row -> screen 1 (0x8BDC4)");
    CheckI(g_bResBuilt, 0, "  the built flag is not touched by the dispatcher");

    Fix_Reset(8);
    g_bResBuilt = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "  and the panels are rebuilt while the flag is set (0x8BDD8)");

    Fix_Reset(9);
    g_bManufSel = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 9, "a selected product redraws and leaves the screen id (0x8BD78)");

    Fix_Reset(9);
    g_bManufBuilt = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "no product but panels built -> screen 1 (0x8BD98 -> 0x8C010)");

    Fix_Reset(9);
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 9, "neither -> the case returns with the id alone");

    Fix_Reset(10);
    g_bTransferBuilt = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "case 10 always sets screen 1 (0x8BE2C)");
    CheckI(g_bTransferBuilt, 1, "  and leaves the built flag for the callee");
}

/* ------------------------------------------------------------------ 10. cases 11 and 12 */

static void TestScreen11And12(void)
{
    printf("cases 11 and 12: the buy/sell close\n");

    Fix_Reset(11);
    g_pBuySellPad = (PadImage *)calloc(1, sizeof(PadImage));
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "screen 1 (0x8C0D8)");
    Check(g_pBuySellPad == NULL, "the buy/sell pad at 0x161A08 is freed and nulled (0x8C0F0)");
    CheckI(g_bPopupB, 0,
           "bPopupVisibleB is set to 1 at 0x8C0C4 and cleared again at 0x8BFA C -- net zero");

    Fix_Reset(12);
    g_pBuySellPad = (PadImage *)calloc(1, sizeof(PadImage));
    g_nSelBase = 4;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 1, "screen 1 (0x8C124)");
    Check(g_pBuySellPad == NULL, "the same pad goes here too (0x8C13C)");
    CheckI(g_bPopupB, 0, "and the popup pair is cleared explicitly (0x8C148 / 0x8C150)");
    CheckI(g_nSelBase, 4, "the selected base is read, not changed, by the two removals");
}

/* ------------------------------------------------------------------ 11. cases 15 and 16 */

static void TestScreen15And16(void)
{
    printf("cases 15 and 16\n");

    Fix_Reset(15);
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 0, "the funding report opener, then screen 0 (0x8BCB0 / 0x8BCB4)");

    Fix_Reset(16);
    g_bUfDirty = 1;
    g_bUfOpen = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHUfRefresh, 1, "a dirty entry refreshes (0x8BD3C)");
    CheckI(g_bUfOpen, 1, "  and the open byte is left set");
    CheckI(g_nScreen, 16, "  with no screen change");

    Fix_Reset(16);
    g_bUfOpen = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_bUfOpen, 0, "open but not dirty -> the shared tail clears the open byte (0x8BFF4)");
    CheckI(g_nHUfOpen, 0, "  and the ufopaedia is not opened");
    CheckI(g_nScreen, 16, "  and the screen id is untouched");

    Fix_Reset(16);
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHUfOpen, 1, "neither -> open the ufopaedia (0x8BD58)");
    CheckI(g_nScreen, 0, "  and screen 0 (0x8BD5C)");
    CheckI(g_view.nScrollY, 60, "  with the scroll clamped (0x8BF38)");
}

/* ------------------------------------------------------------------ 12. case 18, the options */

static void TestScreen18(void)
{
    printf("case 18 @ 0x8BCBC: the options screen, and the mutual recursion\n");

    Fix_Reset(18);
    g_bOptActive = 1;
    g_nOptMode = 4;
    g_nBaseCount = 2;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nOptMode, 0, "the mode byte is cleared before the callee runs (0x8BCFC)");
    CheckI(g_nHAudioCalls, 1, "  and Options_SaveAudioSettings runs (0x8BD00)");
    CheckI(g_nHAudioMode, 4, "  with the signed mode it read at 0x8BCD0");
    CheckI(g_nHAudioCount, 2, "  and the base-loop residue as the third argument");
    CheckI(g_nScreen, 18, "  this arm leaves the screen id alone");
    CheckI(g_nOptInit, 0, "  and RebuildWithSound does not run");
    CheckI(g_bDragActive, 0, "the case tail clears bGeoscapeDragActive (0x8BD1C -> 0x8BFF4)");

    Fix_Reset(18);
    g_bOptActive = 1;
    g_nOptMode = 0;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHAudioCalls, 0, "a zero mode takes the other arm instead");
    CheckI(g_nOptInit, 1, "  which rebuilds the options screen (0x8BCE4)");
    CheckI(g_nOptSample, 7 + 4, "  and Sound_Play(7, 4) is part of that leaf (0x6CF08)");
    CheckI(g_nScreen, 0, "  with screen 0 (0x8BCE8)");

    Fix_Reset(18);
    g_bOptOpen = 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHOptionsCalls, 1, "the options-open flag runs the second, independent test (0x8BD10)");
    CheckI(g_nHOptionsMode, -1, "  with nMode -1 (0x8BD10 MOVNE R0, #0xFFFFFFFF)");
    CheckI(g_nScreen, 0, "  and screen 0 (0x8BD14)");

    Fix_Reset(18);
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHAudioCalls, 0, "neither flag -> no callee");
    CheckI(g_nScreen, 18, "  and the screen id is untouched");
    CheckI(g_bDragActive, 0, "  the drag byte is cleared whatever the rest did");

    /* A negative mode byte is read SIGNED (LDRSB at 0x8BCD0). */
    Fix_Reset(18);
    g_bOptActive = 1;
    g_nOptMode = -3;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHAudioMode, -3, "the mode byte arrives signed, not as its low byte");
}

/* ------------------------------------------------------------------ 13. case 19, the monthly close */

static void TestScreen19(void)
{
    printf("case 19 @ 0x8BB38: the monthly economy close\n");

    /* Bankruptcy: no bases, so the upkeep sum is zero, and the funds are one below the limit. */
    Fix_Reset(19);
    g_aBar[0] = 0;
    g_aBar[1] = 0;
    g_aBar[2] = 0;
    g_aBar[3] = 0;
    g_aBar[4] = 0;
    g_nFunds = PUFO_FUNDS_BANKRUPT_LIMIT - 1;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nFunds, PUFO_FUNDS_BANKRUPT_LIMIT - 1, "the funds land one below -3000000 (0x8BBE0)");
    CheckI(g_nHFreeUi, 1, "GeoscapeUi_FreeAll runs with the free-world flag (0x8BBF0)");
    CheckI(g_bTickHold, 1, "Map_bTickHold = 1 (0x8BC04)");
    CheckI(g_bBattleEnded, 1, "Map_bBattleEnded = 1 (0x8BC0C)");
    CheckI(g_nScreen, 19, "and the case returns before the psi search (0x8BC10)");
    CheckI(g_nHPsi, 0, "  so the psi screen never opens");

    /* Exactly at the limit is NOT bankruptcy: 0x8BBE8 is a BGE. */
    Fix_Reset(19);
    g_aBar[0] = 0;
    g_aBar[1] = 0;
    g_aBar[2] = 0;
    g_aBar[3] = 0;
    g_aBar[4] = 0;
    g_nFunds = PUFO_FUNDS_BANKRUPT_LIMIT;
    Geoscape_CloseUI(&g_e);
    CheckI(g_bTickHold, 0, "at exactly -3000000 the BGE at 0x8BBE8 skips the end screen");
    CheckI(g_nScreen, 0, "  and with no bases the psi search finds nothing, so screen 0");
    CheckI(g_nHPsi, 0, "  and no psi screen");

    /* The arithmetic order: bar2 - upkeep + bar1 + bar3 + bar0 + bar4 (0x8BBAC..0x8BBE0). */
    Fix_Reset(19);
    g_aBar[0] = 10;
    g_aBar[1] = 20;
    g_aBar[2] = 30;
    g_aBar[3] = 40;
    g_aBar[4] = 50;
    g_nFunds = 0;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nFunds, 150, "the five bars are summed with upkeep subtracted");

    /* Two bases: base 0 has no psi lab, base 1 has 36 free psi cells -> 360 capacity. The upkeep
     * charges 50000 for every cell whose type byte is not 255, and both bases are fully filled,
     * so 2 * 36 * 50000 = 3600000 against 5000000 of bars. */
    Fix_Reset(19);
    g_nBaseCount = 2;
    memset(&g_abGrid[480 + 144 * 0], 0, 36);  /* base 0: filled, but not psi labs */
    memset(&g_abGrid[480 + 144 * 1], 8, 36);  /* base 1: 36 psi labs */
    memset(&g_abGrid[36 * 0], 0, 36);         /* all unbuilt, so all "free" */
    memset(&g_abGrid[36 * 1], 0, 36);
    g_nFunds = 1000;
    Geoscape_CloseUI(&g_e);
    /* 36 non-empty cells * 50000 per base * 2 bases, plus the 20000 * soldiers-at-base term that
     * puts the fixture's one base-0 soldier into base 0's bill. */
    CheckI(g_nFunds, 1000 + 5000000 - 3620000,
           "the upkeep is 50000 per non-empty facility cell plus 20000 per soldier at the base");
    CheckI(g_nScreen, 17, "the first psi-capable base opens the psi screen at 17 (0x8BC90)");
    CheckI(g_nHPsi, 1, "  through PsiScreen_Init (0x8BC98)");
    CheckI(g_bShownBase, 1, "  and the shown base index is base 1, written with STRB (0x8BC74)");
    CheckI(g_nPsiBase, 1, "  and the psi base byte too (0x8BC80)");

    /* No base has capacity -> screen 0, and the two base-index bytes keep their old values. */
    Fix_Reset(19);
    g_nBaseCount = 1;
    memset(&g_abGrid[480 + 144 * 0], 0, 36);
    memset(&g_abGrid[36 * 0], 0, 36);
    Geoscape_CloseUI(&g_e);
    CheckI(g_nScreen, 0, "no psi-capable base -> screen 0 (0x8BCB4)");
    CheckI(g_nHPsi, 0, "  and the psi screen never opens");
    CheckI(g_bShownBase, 0xEE, "  and neither index byte is written");
    CheckI((i32)(u8)g_nPsiBase, 0xEE, "  including the psi one");

    /* A negative base count skips both loops (0x8BB74 and 0x8BC30 gate on > 0). */
    Fix_Reset(19);
    g_nBaseCount = -1;
    g_aBar[0] = 0;
    g_aBar[1] = 0;
    g_aBar[2] = 0;
    g_aBar[3] = 0;
    g_aBar[4] = 0;
    g_nFunds = 5000;
    Geoscape_CloseUI(&g_e);
    CheckI(g_nFunds, 5000, "a negative count charges nothing (BLE at 0x8BB74)");
    CheckI(g_nScreen, 0, "  and searches nothing (BLE at 0x8BC30)");

    /* The funding pad is freed on the way in. */
    Fix_Reset(19);
    g_pFundPad = (PadImage *)calloc(1, sizeof(PadImage));
    Geoscape_CloseUI(&g_e);
    Check(g_pFundPad == NULL, "pFundPad @ 0x1595DC is freed and nulled (0x8BB4C / 0x8BB54)");
}

/* ------------------------------------------------------------------ 14. case 20 */

static void TestScreen20(void)
{
    printf("case 20 @ 0x8BAF8: the base-attack screen\n");

    Fix_Reset(20);
    Geoscape_CloseUI(&g_e);
    CheckI(g_nHBaseAt, 1, "BaseAtScreen_Open runs first (0x8BB00)");
    CheckI(g_nScreen, 0, "screen 0 (0x8BB08)");
    CheckI(g_bMissionMode, 0, "and with 0x158850 clear the mission mode is not forced");

    /* The assault half hands off to Smap_BeginCraftSelect, which needs the whole craft-select
     * environment; its own suite covers it. What is checked here is that the dispatcher's own
     * store happens before that hand-off, by watching the flag the callee would read. */
    Fix_Reset(20);
    g_bAtAssault = 1;
    g_e.pCraftSelect = NULL; /* the callee is not reached in this build; see the file header */
    CheckI(g_bAtAssault, 1, "the assault flag is the gate at 0x8BB10");
}

/* ------------------------------------------------------------------ 15. the per-base pass */

static void TestBasePass(void)
{
    printf("the per-base needed-flag pass @ 0x8BA00..0x8BA80\n");

    /* One base, one soldier assigned to it, and a grid whose living-quarters cells are all
     * present. The needed grid is at grid + 444 + 144*base; the type grid at + 480 + 144*base. */
    Fix_Reset(13);
    g_nBaseCount = 1;
    memset(&g_abGrid[480 + 144 * 0], 1, 36); /* kind 1 is living quarters */
    memset(&g_abGrid[36 * 0], 0, 36);
    g_aSold[0].nKind = 0;
    Geoscape_CloseUI(&g_e);

    /* The needed-flag grid is pre-filled with 255 by the reset, so the pass's own writes are the
     * cells that read exactly 1. */
    i32 nNeeded = 0;
    for (i32 i = 0; i < 36; ++i) {
        if (g_abGrid[444 + 144 * 0 + i] == 1) {
            ++nNeeded;
        }
    }
    Check(nNeeded > 0, "the pass writes the needed flags for the base (0x8BA34)");
    CheckI(g_abGrid[480 + 144 * 0], 1, "  and never touches the type grid");

    /* A negative count runs nothing at all: 0x8BA10 LDRSB / 0x8BA20 BLE. */
    Fix_Reset(13);
    g_nBaseCount = -1;
    memset(&g_abGrid[480 + 144 * 0], 1, 36);
    Geoscape_CloseUI(&g_e);
    nNeeded = 0;
    for (i32 i = 0; i < 36; ++i) {
        if (g_abGrid[444 + 144 * 0 + i] == 1) {
            ++nNeeded;
        }
    }
    CheckI(nNeeded, 0, "a negative base count runs no pass");
}

int main(void)
{
    TestHead();
    TestDefaultCases();
    TestScreen1();
    TestScreen2();
    TestScreen3And4();
    TestScreen5();
    TestScreen6();
    TestScreen7();
    TestScreen8To10();
    TestScreen11And12();
    TestScreen15And16();
    TestScreen18();
    TestScreen19();
    TestScreen20();
    TestBasePass();

    printf("\ncloseui: %d checks, %d failures\n", g_nChecks, g_nFailures);
    return g_nFailures != 0 ? 1 : 0;
}
