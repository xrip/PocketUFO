/* test_geopointer.c - Geoscape_OnPointer @ 0x872d0: which controls each screen answers to.
 *
 * The dispatcher has no rules of its own; what it decides is which control set the
 * pointer belongs to, and that is what these checks assert. Every control in the fixture
 * is a mode-0 button covering the whole 240x320 surface, so a hit test flips its state
 * 0 -> 1 and a control that was never reached stays at 0. That makes "was this control
 * hit-tested, in which arm" directly observable, and an early return observable as the
 * controls after it staying down.
 *
 * The panels are set up so UiPanel_TrackHoldRow and UiPanel_ClickRow leave different
 * marks: tracking sets bUnk3024 and leaves nUnk3032 alone, clicking clears bUnk3024 and
 * writes nUnk3032 = nUnk3028 + row. So "which panel was tracked, which was clicked" is
 * observable too.
 */
#include "game/pufo_geopointer.h"
#include "game/pufo_ship.h"

#include <stdio.h>
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

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    ++g_nChecks;
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

/* ------------------------------------------------- the fixture */
#define GEO_TEST_BASES 3 /* nBaseCount: the strip draws 28 * 3 rows */

typedef struct Fix {
    UiControl aCtl[PUFO_GEOUI_ROOT_CTLS];      /* the 13 singletons, slot order */
    UiControl aRoot[PUFO_GEOUI_ROOT_CTLS];     /* the 13 slots in the 0x1864F8 block */
    UiControl aStrip[PUFO_GEO_STRIP_CTLS];
    UiControl aBaseAt[2];
    UiControl aSolPanel[2];
    UiControl aSolName[3];
    UiControl aPlane[PUFO_GEO_PLANE_CTLS];
    UiControl aRadar[PUFO_RADAR_CONTROLS];
    UiControl aPsi[2];
    UiControl aFac[2];
    UiControl aResearch[4];
    UiControl aManuf[6];
    UiControl aSiteInfo[4];
    UiControl aBuySell[2];
    UiControl a16[6];
    UiControl aOptList[PUFO_GEO_OPT_LIST_CTLS];
    UiControl aOptMusic[PUFO_GEO_OPT_SOUND_CTLS];
    UiControl aOptSfx[PUFO_GEO_OPT_SOUND_CTLS];
    UiControl aOptPrev;
    UiControl aOptNext;

    /* The env takes arrays of control POINTERS, which is what the original's globals
     * hold; these are the slot arrays that point at the objects above. */
    UiControl *apStrip[PUFO_GEO_STRIP_CTLS];
    UiControl *apBaseAt[2];
    UiControl *apSolPanel[2];
    UiControl *apSolName[3];
    UiControl *apPlane[PUFO_GEO_PLANE_CTLS];
    UiControl *apRadar[PUFO_RADAR_CONTROLS];
    UiControl *apPsiCtl[2];
    UiControl *apFac[2];
    UiControl *apResearch[4];
    UiControl *apManuf[6];
    UiControl *apSiteInfo[4];
    UiControl *apBuySell[2];
    UiControl *ap16[6];
    UiControl *apOptList[PUFO_GEO_OPT_LIST_CTLS];
    UiControl *apOptMusic[PUFO_GEO_OPT_SOUND_CTLS];
    UiControl *apOptSfx[PUFO_GEO_OPT_SOUND_CTLS];
    Ship *apCraft[2];

    UiPanel pStatus, pSolNames, pSolEquip, pFac, pResearch, pManufProd, pManufD;
    UiPanel pBuySell, pBuySellB, pBuySellC, p16, p16Set, pOptShips, pPsi[2];
    UiPanel pPlaneCraft, pPlaneStatus, pPlaneSel, pPlaneCommit, pPlaneSlot, pPlaneCrew;

    UiControl *apCtl[PUFO_GEOUI_ROOT_CTLS];
    UiControl *apRoot[PUFO_GEOUI_ROOT_CTLS];
    UiPanel *apPsiPanel[2];
    UiPanel *ap16Set[1];

    Ship aCraft[2];
    PadImage oOverlay;
    SmapView oView;
    UiLineList oLines;
    u8 aTiles[64];

    i32 nScreenId;
    i32 nCursorX, nCursorY, nTapX, nTapY;
    i32 nTapCooldown, nRadarPulseTimer;
    i32 nSelectedCraftIdx, nPanelIndex;
    i8 nSelectedBase, nBaseCount, nTransferBase, nStripHitBase;
    u8 bPopupA, bPopupB, bDragActive, bNewBaseMode, bPlaceFail, bPlaceActive;
    u8 bTargetSelect, bInterceptDone, bMultiSelect, bPlacementPending, bPendingRegion;
    u8 bMapSelect, bRadarActive, bGateTransfer, bGateBaseInfo;
    u8 bPsiOpen, bPsiRow, bTransferRow, bSolOpen, bNameEdit, bAnimStarted, bTransferFlag;
    u8 bFacArmed, bRowSelected, bProductSelected;
    u8 b16ModeA, b16ModeB, b16Full;
    u8 bOptConfirm, bOptPageActive, bBaseAtDone;
    u8 bPlaneBuilt, bPlaneDetail, bPlaneStores, bPlaneListBuilt, bStatAOpen, bStatBOpen;
    i8 nOptPage;
    i32 anStatGate[PUFO_GEO_PLANE_STAT_COUNT];
    u32 nShortfall;
    i32 nPendingX, nPendingY;

    u32 nUiSlot0;
    UiControlHitCtx oHit;
    GeoscapePointerEnv oEnv;

    /* What the injected hooks recorded. */
    i32 nSamplePlayed;
    i32 nGlyphX, nGlyphY;
    bool bGlyphCalled;
    u8 bStrategHit;
    i32 nBackspaceClicks;
} Fix;

static Fix g;

static u8 HookStrategHit(const SmapView *pView, i32 nX, i32 nY, void *pUser)
{
    (void)pView;
    (void)nX;
    (void)nY;
    (void)pUser;
    return g.bStrategHit;
}

static void HookPlaySample(i32 nSample, void *pUser)
{
    (void)pUser;
    g.nSamplePlayed = nSample;
}

static void HookAppendGlyph(i32 nX, i32 nY, void *pUser)
{
    (void)pUser;
    g.bGlyphCalled = true;
    g.nGlyphX = nX;
    g.nGlyphY = nY;
}

static void HookBackspace(void) { ++g.nBackspaceClicks; }

/* Harness only. UiPanel_ClickRow tail-calls the panel's own pfnClick, and in the original that
 * is the only way byte_1870F8 can be non-zero by the time the screen-17 tail re-tests it at
 * 0x8AC9C. This handler stands in for the psi panel's unported click handler so the comparison
 * there is observable at all. */
static void HookPsiClickReArm(void) { g.bTransferRow = 1; }
static void HookCopyName(void) {}

static void Fix_Ctl(UiControl *pCtl)
{
    memset(pCtl, 0, sizeof(*pCtl));
    pCtl->x = 0;
    pCtl->y = 0;
    pCtl->uWidth = PUFO_SURF_W;
    pCtl->uHeight = PUFO_SURF_H;
    pCtl->nMode = 0;
    pCtl->bEnabled = 1;
}

static void Fix_CtlArray(UiControl *apCtl, UiControl *apSlots[], i32 nCount)
{
    for (i32 i = 0; i < nCount; ++i) {
        Fix_Ctl(&apCtl[i]);
        apSlots[i] = &apCtl[i];
    }
}

static void Fix_Panel(UiPanel *p)
{
    memset(p, 0, sizeof(*p));
    p->nStyle = 200;
    p->nLineCount = 200;
    p->nPosY = 0;
    p->nFont = 1;
    p->nUnk7052 = 0;
    p->nUnk7056 = 0;
    p->nViewW = PUFO_SURF_W;
    p->nViewH = PUFO_SURF_H;
    p->bFlag7073 = 1;
}

static i32 PanelRow(const UiPanel *p) { return *(const i32 *)(const void *)(p->gap_0 + 12); }
static bool CtlHit(const UiControl *pCtl) { return pCtl->nState != 0; }

static void Fix_Reset(void)
{
    memset(&g, 0, sizeof(g));

    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        Fix_Ctl(&g.aCtl[i]);
        Fix_Ctl(&g.aRoot[i]);
        g.apCtl[i] = &g.aCtl[i];
        g.apRoot[i] = &g.aRoot[i];
    }
    Fix_CtlArray(g.aStrip, g.apStrip, PUFO_GEO_STRIP_CTLS);
    Fix_CtlArray(g.aBaseAt, g.apBaseAt, 2);
    Fix_CtlArray(g.aSolPanel, g.apSolPanel, 2);
    Fix_CtlArray(g.aSolName, g.apSolName, 3);
    Fix_CtlArray(g.aPlane, g.apPlane, PUFO_GEO_PLANE_CTLS);
    Fix_CtlArray(g.aRadar, g.apRadar, PUFO_RADAR_CONTROLS);
    Fix_CtlArray(g.aPsi, g.apPsiCtl, 2);
    Fix_CtlArray(g.aFac, g.apFac, 2);
    Fix_CtlArray(g.aResearch, g.apResearch, 4);
    Fix_CtlArray(g.aManuf, g.apManuf, 6);
    Fix_CtlArray(g.aSiteInfo, g.apSiteInfo, 4);
    Fix_CtlArray(g.aBuySell, g.apBuySell, 2);
    Fix_CtlArray(g.a16, g.ap16, 6);
    Fix_CtlArray(g.aOptList, g.apOptList, PUFO_GEO_OPT_LIST_CTLS);
    Fix_CtlArray(g.aOptMusic, g.apOptMusic, PUFO_GEO_OPT_SOUND_CTLS);
    Fix_CtlArray(g.aOptSfx, g.apOptSfx, PUFO_GEO_OPT_SOUND_CTLS);
    Fix_Ctl(&g.aOptPrev);
    Fix_Ctl(&g.aOptNext);
    g.apCraft[0] = &g.aCraft[0];
    g.apCraft[1] = &g.aCraft[1];

    Fix_Panel(&g.pStatus);
    Fix_Panel(&g.pSolNames);
    Fix_Panel(&g.pSolEquip);
    Fix_Panel(&g.pFac);
    Fix_Panel(&g.pResearch);
    Fix_Panel(&g.pManufProd);
    Fix_Panel(&g.pManufD);
    Fix_Panel(&g.pBuySell);
    Fix_Panel(&g.pBuySellB);
    Fix_Panel(&g.pBuySellC);
    Fix_Panel(&g.p16);
    Fix_Panel(&g.p16Set);
    Fix_Panel(&g.pOptShips);
    Fix_Panel(&g.pPsi[0]);
    Fix_Panel(&g.pPsi[1]);
    Fix_Panel(&g.pPlaneCraft);
    Fix_Panel(&g.pPlaneStatus);
    Fix_Panel(&g.pPlaneSel);
    Fix_Panel(&g.pPlaneCommit);
    Fix_Panel(&g.pPlaneSlot);
    Fix_Panel(&g.pPlaneCrew);

    g.apPsiPanel[0] = &g.pPsi[0];
    g.apPsiPanel[1] = &g.pPsi[1];
    g.ap16Set[0] = &g.p16Set;

    g.nBaseCount = GEO_TEST_BASES;
    g.nSelectedBase = 1;
    g.nTransferBase = 1;
    g.oOverlay.uWidth = 240;
    g.oOverlay.uHeight = 160; /* so the drag band is cursorY < 160 */
    g.oView.nMapW = 8;
    g.oView.nMapH = 8;
    g.oView.nScrollX = 120; /* puts tile (104, 104) under screen point (100, 100) */
    g.oView.nScrollY = 160;
    g.aCraft[0].bCrewSlots = 3;
    g.aCraft[0].bCanDogfight = 1;
    g.nSelectedCraftIdx = 0;
    g.bPendingRegion = 0xFF; /* no region pending */
    g.nUiSlot0 = 0xDEADBEEFu;

    g.oHit.pnUiSlot0 = &g.nUiSlot0;
    g.oHit.pfnBlit = NULL;
    g.oHit.pfnBlitEx = NULL;
    g.oHit.pUser = NULL;

    GeoscapePointerEnv *p = &g.oEnv;
    p->apRootCtl = g.apRoot;
    p->pbPopupA = &g.bPopupA;
    p->pbPopupB = &g.bPopupB;
    p->apCtl = g.apCtl;
    p->pnScreenId = &g.nScreenId;
    p->pLines = &g.oLines;
    p->pnCursorX = &g.nCursorX;
    p->pnCursorY = &g.nCursorY;
    p->pnTapX = &g.nTapX;
    p->pnTapY = &g.nTapY;
    p->pbDragActive = &g.bDragActive;
    p->pbNewBaseMode = &g.bNewBaseMode;
    p->pbPlaceFail = &g.bPlaceFail;
    p->pbPlaceActive = &g.bPlaceActive;
    p->pbTargetSelectActive = &g.bTargetSelect;
    p->pbInterceptDone = &g.bInterceptDone;
    p->pbMultiSelectPanel = &g.bMultiSelect;
    p->pbPlacementPending = &g.bPlacementPending;
    p->pbPendingRegionId = &g.bPendingRegion;
    p->pbMapSelectionFlag = &g.bMapSelect;
    p->pView = &g.oView;
    p->pZoomOverlay = &g.oOverlay;
    p->pTiles = g.aTiles;
    p->pnTapCooldown = &g.nTapCooldown;
    p->pnRadarPulseTimer = &g.nRadarPulseTimer;
    p->pbRadarScreenActive = &g.bRadarActive;
    p->apRadarCtl = g.apRadar;
    p->pStatusPanel = &g.pStatus;
    p->stripHit.pbBaseCount = (const u8 *)&g.nBaseCount;
    p->stripHit.pbSelectedBase = (const u8 *)&g.nSelectedBase;
    p->stripHit.pbGateTransfer = &g.bGateTransfer;
    p->stripHit.pbGateBaseInfo = &g.bGateBaseInfo;
    p->stripHit.pbGatePsi = &g.bPsiOpen;
    p->stripHit.pbTapFlag = &g.bPlaceActive;
    p->apStripCtl = g.apStrip;
    p->pnSelectedBaseIdx = &g.nSelectedBase;
    p->pnBaseCount = &g.nBaseCount;
    p->pbTransferRowSel = &g.bTransferRow;
    p->pnTransferBaseIdx = &g.nTransferBase;
    p->pbStripHitBase = &g.nStripHitBase;
    p->pbPsiPanelOpen = &g.bPsiOpen;
    p->pbPsiRowSel = &g.bPsiRow;
    p->apPsiPanel = g.apPsiPanel;
    p->apPsiCtl = g.apPsiCtl;
    p->apBaseAtCtl = g.apBaseAt;
    p->pbSolScreenOpen = &g.bSolOpen;
    p->pSolPanelNames = &g.pSolNames;
    p->pSolPanelEquip = &g.pSolEquip;
    p->apSolPanelCtl = g.apSolPanel;
    p->apSolNameCtl = g.apSolName;
    p->pbNameEditActive = &g.bNameEdit;
    p->pbUiAnimStarted = &g.bAnimStarted;
    p->pbTransferFlag = &g.bTransferFlag;
    p->pFacPanel = &g.pFac;
    p->pbFacPlaceArmed = &g.bFacArmed;
    p->apFacCtl = g.apFac;
    p->pbRowSelected = &g.bRowSelected;
    p->pResearchPanel = &g.pResearch;
    p->apResearchCtl = g.apResearch;
    p->pbProductSelected = &g.bProductSelected;
    p->pManufProductPanel = &g.pManufProd;
    p->pManufPanelD = &g.pManufD;
    p->apManufCtl = g.apManuf;
    p->apSiteInfoCtl = g.apSiteInfo;
    p->apBuySellCtl = g.apBuySell;
    p->pBuySellPanelA = &g.pBuySell;
    p->pb16ModeA = &g.b16ModeA;
    p->pb16ModeB = &g.b16ModeB;
    p->pb16ListFull = &g.b16Full;
    p->ap16Ctl = g.ap16;
    p->ap16PanelSet = g.ap16Set;
    p->pn16PanelIndex = &g.nPanelIndex;
    p->p16Panel = &g.p16;
    p->pbOptionsConfirm = &g.bOptConfirm;
    p->pbOptionsPageActive = &g.bOptPageActive;
    p->pnOptionsPage = &g.nOptPage;
    p->apOptListCtl = g.apOptList;
    p->apOptMusicCtl = g.apOptMusic;
    p->apOptSfxCtl = g.apOptSfx;
    p->pOptCtlPrev = &g.aOptPrev;
    p->pOptCtlNext = &g.aOptNext;
    p->pOptPanelShips = &g.pOptShips;
    p->pbBaseAtDone = &g.bBaseAtDone;
    p->apCraft = g.apCraft;
    p->pnSelectedCraftIdx = &g.nSelectedCraftIdx;
    p->apPlaneCtl = g.apPlane;
    p->pbPlanePanelsBuilt = &g.bPlaneBuilt;
    p->pbPlaneShowCraftDetail = &g.bPlaneDetail;
    p->pbPlaneStoresCommitted = &g.bPlaneStores;
    p->pbPlaneListBuilt = &g.bPlaneListBuilt;
    p->pbStatPanelAOpen = &g.bStatAOpen;
    p->pbStatPanelBOpen = &g.bStatBOpen;
    p->pPlaneCraftPanel = &g.pPlaneCraft;
    p->pPlaneCraftStatusPanel = &g.pPlaneStatus;
    p->pPlaneSelRowPanel = &g.pPlaneSel;
    p->pPlaneCommitRowPanel = &g.pPlaneCommit;
    p->pPlaneSlotPanel = &g.pPlaneSlot;
    p->pPlaneCrewPanel = &g.pPlaneCrew;
    for (i32 i = 0; i < PUFO_GEO_PLANE_STAT_COUNT; ++i) {
        p->apanStatGate[i] = &g.anStatGate[i];
    }
    p->pfnBackspaceName = HookBackspace;
    p->pfnCopyNameToEdit = HookCopyName;
    p->pfnAppendNameGlyph = HookAppendGlyph;
    p->pfnPlaySample = HookPlaySample;
    p->pfnHitTestStrategSelect = HookStrategHit;
    p->pdwShortfall = &g.nShortfall;
    p->pBuySellPanelB = &g.pBuySellB;
    p->pBuySellPanelC = &g.pBuySellC;
    p->pnPendingPlaceX = &g.nPendingX;
    p->pnPendingPlaceY = &g.nPendingY;
    p->pHit = &g.oHit;
}

static i32 Call(i32 nX, i32 nY, bool bDown)
{
    return Geoscape_OnPointer(&g.oEnv, nX, nY, bDown);
}

static bool Hit(GeoscapeUiCtlSlot nSlot) { return CtlHit(&g.aCtl[nSlot]); }

/* ------------------------------------------------- the release fixture
 *
 * A release ignores the hit box, so the only way to see one is to start from a control that is
 * already down: UiControl_HitTest with bDown == 0 takes a mode-0 control from state 1 to state 0
 * and returns 1, and leaves a control at state 0 alone and returns 0. So every release test arms
 * every control first (nState = 1, "the press hit it") and then asserts which ones the release
 * put back down. A control still armed is one the arm never reached - which is what an early
 * return looks like. */
static void Fix_Arm(UiControl *apCtl, i32 nCount)
{
    for (i32 i = 0; i < nCount; ++i) {
        apCtl[i].nState = 1;
    }
}

static void Fix_ArmAll(void)
{
    Fix_Arm(g.aCtl, PUFO_GEOUI_ROOT_CTLS);
    Fix_Arm(g.aRoot, PUFO_GEOUI_ROOT_CTLS);
    Fix_Arm(g.aStrip, PUFO_GEO_STRIP_CTLS);
    Fix_Arm(g.aBaseAt, 2);
    Fix_Arm(g.aSolPanel, 2);
    Fix_Arm(g.aSolName, 3);
    Fix_Arm(g.aPlane, PUFO_GEO_PLANE_CTLS);
    Fix_Arm(g.aRadar, PUFO_RADAR_CONTROLS);
    Fix_Arm(g.aPsi, 2);
    Fix_Arm(g.aFac, 2);
    Fix_Arm(g.aResearch, 4);
    Fix_Arm(g.aManuf, 6);
    Fix_Arm(g.aSiteInfo, 4);
    Fix_Arm(g.aBuySell, 2);
    Fix_Arm(g.a16, 6);
    Fix_Arm(g.aOptList, PUFO_GEO_OPT_LIST_CTLS);
    Fix_Arm(g.aOptMusic, PUFO_GEO_OPT_SOUND_CTLS);
    Fix_Arm(g.aOptSfx, PUFO_GEO_OPT_SOUND_CTLS);
    Fix_Arm(&g.aOptPrev, 1);
    Fix_Arm(&g.aOptNext, 1);
}

static i32 CallUp(void) { return Geoscape_OnPointerUp(&g.oEnv); }

/* True when the release put this singleton back down. */
static bool Up(GeoscapeUiCtlSlot nSlot) { return g.aCtl[nSlot].nState == 0; }
static bool StillArmed(GeoscapeUiCtlSlot nSlot) { return g.aCtl[nSlot].nState != 0; }
static bool UpCtl(const UiControl *pCtl) { return pCtl->nState == 0; }
static bool ArmedCtl(const UiControl *pCtl) { return pCtl->nState != 0; }

/* The release tests park the cursor here: inside every panel's click rect, so UiPanel_ClickRow
 * lands on row 100 and writes nUnk3032 = nUnk3028 + 100, which a panel that was not clicked
 * leaves at 0. */
static void Fix_UpCursor(void)
{
    g.nCursorX = 100;
    g.nCursorY = 100;
}

static bool Clicked(const UiPanel *p) { return p->nUnk3032 == 100; }
static bool NotClicked(const UiPanel *p) { return p->nUnk3032 == 0; }

/* ------------------------------------------------- screen 0: the main-menu band */
static void TestMenuBand(void)
{
    printf("screen 0, the main-menu message band\n");
    Fix_Reset();
    g.nScreenId = 0;
    g.oLines.abLineFlags[0] = 40;
    CheckEqI32(Call(100, 100, true), 1, "flag 40 returns the 187024 hit");
    Check(Hit(GEOUI_CTL_18704C), "flag 40 hit-tests 18704C");
    Check(Hit(GEOUI_CTL_187024), "flag 40 hit-tests 187024");
    Check(!Hit(GEOUI_CTL_186BA4), "flag 40 does not touch the confirm control");
    Check(!CtlHit(&g.aRoot[0]), "flag 40 does not reach the world arm");

    Fix_Reset();
    g.nScreenId = 0;
    g.oLines.abLineFlags[0] = 37;
    CheckEqI32(Call(100, 100, true), 1, "flag 37 returns the 186BAC hit");
    Check(Hit(GEOUI_CTL_186BA4), "flag 37 adds the confirm control");
    Check(Hit(GEOUI_CTL_186BAC), "flag 37 adds the craft-select control");

    Fix_Reset();
    g.nScreenId = 0;
    g.oLines.abLineFlags[0] = 30; /* the band is exclusive at both ends */
    Call(100, 100, false);
    Check(!Hit(GEOUI_CTL_18704C), "flag 30 is outside the band");
    Check(CtlHit(&g.aRoot[0]), "flag 30 falls through to the world arm");

    Fix_Reset();
    g.nScreenId = 0;
    g.oLines.abLineFlags[0] = 50;
    Call(100, 100, false);
    Check(!Hit(GEOUI_CTL_18704C), "flag 50 is outside the band");
}

/* ------------------------------------------------- screens 0 and 2: the world arm */
static void TestWorldFlags(void)
{
    printf("screens 0 and 2, the message-flag chain\n");
    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 16;
    CheckEqI32(Call(100, 100, true), 1, "flag 16 returns the 186BB4 hit");
    Check(Hit(GEOUI_CTL_186BA4), "flag 16 hit-tests the confirm control first");
    Check(Hit(GEOUI_CTL_186BB4), "flag 16 hit-tests 186BB4");
    Check(!Hit(GEOUI_CTL_186BB0), "flag 16 stops there");

    Fix_Reset();
    g.nScreenId = 2;
    g.bTargetSelect = 1;
    CheckEqI32(Call(100, 100, true), 1, "the target-select gate returns the craft-select hit");
    Check(Hit(GEOUI_CTL_186B8C), "the gate hit-tests 186B8C");
    CheckEqI32(g.bNewBaseMode, 1, "screen 2 raises the new-base mode on the way in");
    Check(Hit(GEOUI_CTL_186B00), "with nothing pending the close control is hit");

    Fix_Reset();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.bPendingRegion = 7;
    Call(100, 100, true);
    Check(!Hit(GEOUI_CTL_186B00), "with the placement byte set the close control is skipped");
    Check(Hit(GEOUI_CTL_186BA4), "a real pending region id releases the confirm control");

    Fix_Reset();
    g.nScreenId = 2;
    Call(100, 100, true);
    Check(!Hit(GEOUI_CTL_186BA4), "with no region pending (0xFF) the confirm control is skipped");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 17;
    CheckEqI32(Call(100, 100, true), 1, "flag 17 returns the 186BB4 hit");
    Check(!Hit(GEOUI_CTL_186BB0), "flag 17 does not reach 186BB0");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 22;
    CheckEqI32(Call(100, 100, true), 1, "flag 22 returns the 186BB0 hit");
    Check(Hit(GEOUI_CTL_186BB4), "flag 22 hit-tests 186BB4 first");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 18;
    CheckEqI32(Call(100, 100, true), 1, "flag 18 without multi-select returns 186BB4");
    Check(!Hit(GEOUI_CTL_186BB0), "flag 18 without multi-select stops at 186BB4");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 20;
    g.bMapSelect = 1;
    CheckEqI32(Call(100, 100, true), 1, "flag 20 with multi-select returns 186BB0");
    Check(Hit(GEOUI_CTL_186BB4), "flag 20 with multi-select still hit-tests 186BB4");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 14;
    Call(100, 100, true);
    Check(Hit(GEOUI_CTL_186BB4) && Hit(GEOUI_CTL_186BA4), "flag 14 hit-tests 186BB4 then 186BA4");
    Check(!Hit(GEOUI_CTL_186BB0), "flag 14 does not reach 186BB0");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 11;
    Call(100, 100, true);
    Check(Hit(GEOUI_CTL_186BA8) && Hit(GEOUI_CTL_186B9C) && Hit(GEOUI_CTL_186B98),
          "flag 11 hit-tests the three intercept controls");
    Check(Hit(GEOUI_CTL_186BB0), "flag 11 reaches 186BB0 when the intercept is not done");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 11;
    g.bInterceptDone = 3;
    CheckEqI32(Call(100, 100, true), 3, "flag 11 returns the intercept flag when it is set");
    Check(!Hit(GEOUI_CTL_186BB0), "and it returns before 186BB0");

    Fix_Reset();
    g.nScreenId = 2;
    g.bRadarActive = 1;
    CheckEqI32(Call(100, 100, true), PUFO_RADAR_CONTROLS, "the radar screen hit-tests its five controls");
    bool bAllRadar = true;
    for (i32 i = 0; i < PUFO_RADAR_CONTROLS; ++i) {
        bAllRadar = bAllRadar && CtlHit(&g.aRadar[i]);
    }
    Check(bAllRadar, "all five radar controls are hit");
    Check(!Hit(GEOUI_CTL_186BB4), "the radar screen returns before the message chain continues");

    Fix_Reset();
    g.nScreenId = 2;
    g.bMultiSelect = 1;
    CheckEqI32(Call(100, 100, true), 100, "the multi-select panel tracks the pointer row");
    CheckEqI32(g.pStatus.bUnk3024, 1, "the status panel is the one tracked");
    CheckEqI32(PanelRow(&g.pStatus), 100, "the tracked row is the cursor y");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 60;
    Call(100, 100, true);
    CheckEqI32(g.nSamplePlayed, 17, "flag 60 plays sample 17");
    Check(Hit(GEOUI_CTL_186BA4) && Hit(GEOUI_CTL_186BB4), "flag 60 still hit-tests the two controls");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 13;
    CheckEqI32(Call(100, 100, true), 1, "flag 13 returns the 186BB0 hit");
    Check(!Hit(GEOUI_CTL_186BA4), "flag 13 does not hit-test the confirm control");

    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 16;
    Call(100, 100, false);
    Check(!Hit(GEOUI_CTL_186BA4), "the flag chain needs the pen down");
}

/* ------------------------------------------------- screens 0 and 2: the drag block */
static void TestGlobeDrag(void)
{
    printf("screens 0 and 2, the globe drag\n");

    /* A press on a selectable object, with the cooldown spent, resolves the tile. */
    Fix_Reset();
    g.nScreenId = 2;
    g.nTapCooldown = 0;
    g.bStrategHit = 1;
    CheckEqI32(Call(100, 100, true), 255, "a select hit inside the band returns 255");
    CheckEqI32(g.nTapCooldown, 255, "and charges the tap cooldown");
    CheckEqI32(g.oView.nPickX, 104, "Smap_GetTileAtScreen resolved the tile x");
    CheckEqI32(g.oView.nPickY, 104, "and the tile y");
    CheckEqI32(g.bDragActive, 0, "the select hit clears the drag flag");

    /* The same press with the cooldown not spent falls through into the drag. */
    Fix_Reset();
    g.nScreenId = 2;
    g.nTapCooldown = 9;
    g.bStrategHit = 1;
    Call(100, 100, true);
    CheckEqI32(g.bDragActive, 1, "an unexpired cooldown still starts the drag");
    CheckEqI32(g.nTapCooldown, 0, "and the drag zeroes it");
    CheckEqI32(g.oView.nLastDx, 100, "the drag records the cursor into nLastDx");
    CheckEqI32(g.oView.nLastDy, 100, "and into nLastDy");
    CheckEqI32(g.oView.nScrollX, 120, "the first drag frame leaves the scroll where it was");

    /* Below the strip no drag starts, and screen 2 keeps the root controls down. */
    Fix_Reset();
    g.nScreenId = 2;
    Call(100, 200, true);
    CheckEqI32(g.bDragActive, 0, "below the strip no drag starts");
    Check(!CtlHit(&g.aRoot[0]), "screen 2 does not hand the pointer to the root controls");

    Fix_Reset();
    g.nScreenId = 0;
    const i32 nRet = Call(100, 200, false);
    bool bAllRoot = true;
    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        bAllRoot = bAllRoot && CtlHit(&g.aRoot[i]);
    }
    Check(bAllRoot, "screen 0 with no drag hit-tests all thirteen root controls");
    CheckEqI32(nRet, 0, "and returns the drag flag, not a hit");

    /* A held pointer keeps scrolling even though the press test is skipped. */
    Fix_Reset();
    g.nScreenId = 2;
    g.bDragActive = 1;
    g.oView.nScrollX = 50;
    g.oView.nLastDx = 20;
    Call(100, 100, false);
    CheckEqI32(g.oView.nScrollX, 50 - 100 + 20, "a released held drag still scrolls the view");
    CheckEqI32(g.oView.nLastDx, 100, "and stores the new delta last");

    /* A message on the status line stops the scroll. */
    Fix_Reset();
    g.nScreenId = 2;
    g.bDragActive = 1;
    g.oLines.nLineCount = 1;
    g.oView.nScrollX = 50;
    Call(100, 100, false);
    CheckEqI32(g.oView.nScrollX, 50, "a status message stops the globe scrolling");

    /* Flag 27 hands the pointer to 186BB0 after the drag block. */
    Fix_Reset();
    g.nScreenId = 2;
    g.oLines.abLineFlags[0] = 27;
    CheckEqI32(Call(100, 100, true), 1, "flag 27 returns the 186BB0 hit");
    CheckEqI32(g.bDragActive, 1, "flag 27 still starts a drag when nothing selectable is under the pointer");
}

/* ------------------------------------------------- screen 1: the base strip */
static void TestBaseStrip(void)
{
    printf("screen 1, the base tab strip and the tap cell\n");

    Fix_Reset();
    g.nScreenId = 1;
    CheckEqI32(Call(220, 60, true), 2, "a hit at y 60 selects base 2 (60 / 30)");
    CheckEqI32(g.nSelectedBase, 2, "and stores it into the selected base");
    Check(Hit(GEOUI_CTL_186B00), "screen 1 hit-tests the close control");
    bool bAllStrip = true;
    for (i32 i = 0; i < PUFO_GEO_STRIP_CTLS; ++i) {
        bAllStrip = bAllStrip && CtlHit(&g.aStrip[i]);
    }
    Check(bAllStrip, "screen 1 hit-tests all ten grid controls");
    CheckEqI32(g.nTapX, 0, "x 220 is in the strip column, so no tap cell is computed");
    CheckEqI32(g.bPlaceActive, 0, "and the tap flag is not raised");
    CheckEqI32(g.bPlaceFail, 0, "the placement-fail byte is cleared on entry");

    Fix_Reset();
    g.nScreenId = 1;
    Call(100, 100, true);
    CheckEqI32(g.nTapX, 100 / 32, "the tap cell x is cursor x / 32");
    CheckEqI32(g.nTapY, (100 - 40) / 32, "the tap cell y is (cursor y - 40) / 32");
    CheckEqI32(g.bPlaceActive, 1, "and inside the map area the tap flag is raised");

    /* Outside the map area the tap coordinates are left alone. */
    Fix_Reset();
    g.nScreenId = 1;
    g.nTapX = 11;
    g.nTapY = 12;
    g.bPlaceActive = 1;
    Call(220, 250, true);
    CheckEqI32(g.nTapX, 11, "y 250 is outside the map area, so the tap x is kept");
    CheckEqI32(g.nTapY, 12, "and so is the tap y");
    Check(Hit(GEOUI_CTL_186BB4), "with the tap flag set 186BB4 is hit-tested");

    Fix_Reset();
    g.nScreenId = 1;
    g.bPlaceActive = 0;
    Call(220, 250, true);
    Check(!Hit(GEOUI_CTL_186BB4), "with the tap flag clear it is not");

    Fix_Reset();
    g.nScreenId = 1;
    Call(150, 20, true);
    CheckEqI32(g.nTapX, 0, "y 20 is outside the area on the low side too");
    CheckEqI32(g.nTapY, 0, "so the tap pair keeps its initial value");

    /* The original's division: (v + 31) >> 5 for a negative value, not floor division. */
    Fix_Reset();
    g.nScreenId = 1;
    Call(-1, 100, true);
    CheckEqI32(g.nTapX, 0, "tap x for cursor -1 is (-1 + 31) >> 5 = 0");
    Call(-32, 100, true);
    CheckEqI32(g.nTapX, -1, "tap x for cursor -32 is (-32 + 31) >> 5 = -1");

    /* A miss on the strip leaves the selection where it was. */
    Fix_Reset();
    g.nScreenId = 1;
    CheckEqI32(Call(50, 60, true), 1, "a strip miss returns the selected base back");
    CheckEqI32(g.nSelectedBase, 1, "so the selection is unchanged");

    /* A disabled grid control does not stop the ones around it. */
    Fix_Reset();
    g.nScreenId = 1;
    g.aStrip[4].bEnabled = 0;
    Call(50, 60, true);
    Check(CtlHit(&g.aStrip[3]) && CtlHit(&g.aStrip[5]),
          "a disabled grid control does not stop the ones around it");
}

/* ------------------------------------------------- screens 3 and 17: the transfer row */
static void TestTransferRow(void)
{
    printf("screens 3 and 17, the transfer row selection\n");

    Fix_Reset();
    g.nScreenId = 3;
    g.bTransferRow = 1;
    g.nTransferBase = 2;
    CheckEqI32(Call(220, 60, true), 2, "screen 3 runs the strip hit test while a row is selected");
    CheckEqI32(g.nStripHitBase, 2, "and stores the hit base");
    CheckEqI32(g.bTransferRow, 0, "a hit on the selected base inside the rows drops the selection");
    CheckEqI32(g.bPsiRow, 0, "screen 3 does not touch the psi row byte");
    Check(!CtlHit(&g.aBaseAt[0]), "that branch does not hit-test the dialog controls");

    Fix_Reset();
    g.nScreenId = 3;
    g.bTransferRow = 1;
    g.nTransferBase = 2;
    Call(220, 200, true); /* 200 >= 28 * 3, so outside the drawn rows */
    CheckEqI32(g.bTransferRow, 1, "a hit below the drawn rows keeps the selection");

    Fix_Reset();
    g.nScreenId = 3;
    Call(100, 100, true);
    Check(CtlHit(&g.aBaseAt[0]) && CtlHit(&g.aBaseAt[1]),
          "with no row selected the two dialog controls are hit");
    Check(Hit(GEOUI_CTL_186B00), "and then the close control");

    Fix_Reset();
    g.nScreenId = 17;
    g.bTransferRow = 1;
    g.bPsiRow = 1;
    g.nTransferBase = 2;
    (void)Call(220, 60, true);
    CheckEqI32(g.bTransferRow, 0, "screen 17 drops the row selection the same way");
    CheckEqI32(g.bPsiRow, 0, "and screen 17 also clears the psi row byte (0x890C4)");

    Fix_Reset();
    g.nScreenId = 17;
    CheckEqI32(Call(100, 100, true), 0, "screen 17 with no row selected returns 0");
    Check(Hit(GEOUI_CTL_186BA4) && Hit(GEOUI_CTL_186BAC),
          "it hit-tests confirm and craft-select first");

    Fix_Reset();
    g.nScreenId = 17;
    g.bPsiOpen = 1;
    CheckEqI32(Call(100, 100, true), 0, "an unselected psi row still returns 0");
    Check(!CtlHit(&g.aPsi[0]), "and does not hit-test the psi controls");

    Fix_Reset();
    g.nScreenId = 17;
    g.bPsiOpen = 1;
    g.bPsiRow = 1;
    g.nTransferBase = 1;
    CheckEqI32(Call(100, 100, true), 2, "a selected psi row hit-tests its two controls");
    Check(CtlHit(&g.aPsi[0]) && CtlHit(&g.aPsi[1]), "both psi controls are hit");
    CheckEqI32(PanelRow(&g.pPsi[1]), 100, "and the base's own psi panel is the one tracked");
    CheckEqI32(PanelRow(&g.pPsi[0]), 0, "the other base's panel is not");
}

/* ------------------------------------------------- screens 4 and 5: the soldier screen */
static void TestSoldierScreen(void)
{
    printf("screens 4 and 5, the soldier screen\n");

    Fix_Reset();
    g.nScreenId = 4;
    g.bSolOpen = 1;
    Call(100, 100, true);
    Check(CtlHit(&g.aSolPanel[0]) && CtlHit(&g.aSolPanel[1]),
          "screen 4 hit-tests its two panel controls");
    CheckEqI32(g.pSolNames.bUnk3024, 1, "the open soldier screen tracks the names panel");
    CheckEqI32(g.pSolEquip.bUnk3024, 0, "not the equipment panel");
    Check(Hit(GEOUI_CTL_186B00), "and screen 4 ends on the close control");

    Fix_Reset();
    g.nScreenId = 4;
    Call(100, 100, true);
    CheckEqI32(g.pSolNames.bUnk3024, 0, "with the screen closed no panel is tracked");

    Fix_Reset();
    g.nScreenId = 5;
    g.bNameEdit = 1;
    Call(100, 100, true);
    CheckEqI32(g.bPopupB, 1, "an open name edit raises popup B");
    Check(g.bGlyphCalled && g.nGlyphX == 100 && g.nGlyphY == 100,
          "and appends the name glyph at the pointer");
    Check(Hit(GEOUI_CTL_186BA8) && Hit(GEOUI_CTL_186B9C), "then hit-tests the two name controls");
    Check(!Hit(GEOUI_CTL_186B00), "and returns without the close control");

    Fix_Reset();
    g.nScreenId = 5;
    g.bNameEdit = 1;
    Call(100, 300, true);
    Check(!g.bGlyphCalled, "y 300 is outside the name band, so no glyph");
    CheckEqI32(g.bPopupB, 0, "and popup B is not raised either");

    Fix_Reset();
    g.nScreenId = 5;
    CheckEqI32(Call(50, 10, true), 3, "a press in the name band ends on the three name controls");
    CheckEqI32(g.bNameEdit, 1, "and opens the edit");
    CheckEqI32(g.aCtl[GEOUI_CTL_186BA8].x, 1, "the backspace control moves to x 1");
    CheckEqI32(g.aCtl[GEOUI_CTL_186BA8].y, 300, "and y 300");
    CheckEqI32(g.aCtl[GEOUI_CTL_186B9C].x, 120, "the confirm control moves to x 120");
    CheckEqI32(g.aCtl[GEOUI_CTL_186B9C].y, 300, "and y 300");
    Check(g.aCtl[GEOUI_CTL_186BA8].pfnClick == HookBackspace, "the backspace handler is installed");
    CheckEqI32(g.bAnimStarted, 0, "and the animation flag is cleared");
    Check(CtlHit(&g.aSolName[0]) && CtlHit(&g.aSolName[2]), "all three name controls are hit");

    Fix_Reset();
    g.nScreenId = 5;
    Call(300, 10, true);
    CheckEqI32(g.bNameEdit, 0, "a press outside the band does not open the edit");

    Fix_Reset();
    g.nScreenId = 5;
    g.bTransferFlag = 1;
    Call(100, 100, true);
    CheckEqI32(g.pSolEquip.bUnk3024, 1, "with the transfer flag the equipment panel is tracked");
    Check(!CtlHit(&g.aSolName[0]), "and the name controls are skipped");
}

/* ------------------------------------------------- screen 6: the craft loadout */
static void TestCraftScreen(void)
{
    printf("screen 6, the craft loadout\n");

    Fix_Reset();
    g.nScreenId = 6;
    g.bPlaneBuilt = 1;
    Call(100, 100, true);
    CheckEqI32(g.bPopupB, 1, "screen 6 raises popup B");
    CheckEqI32(g.bPopupA, 1, "and popup A while the craft list is showing");
    CheckEqI32(g.pPlaneCraft.bUnk3024, 1, "the craft panel is tracked");
    CheckEqI32(PanelRow(&g.pPlaneStatus), 100, "the status panel is tracked at the raw y");
    CheckEqI32(g.pPlaneStatus.nUnk3032, 100, "and its row is clicked");
    Check(Hit(GEOUI_CTL_186B00), "screen 6 ends on the close control");

    Fix_Reset();
    g.nScreenId = 6;
    g.bPlaneBuilt = 1;
    g.bPlaneDetail = 1;
    Call(100, 100, true);
    CheckEqI32(g.bPopupA, 0, "with the craft detail open popup A stays down");
    CheckEqI32(g.pPlaneCraft.bUnk3024, 0, "and the craft panel is not tracked");

    Fix_Reset();
    g.nScreenId = 6;
    g.bPlaneStores = 1;
    Call(100, 100, true);
    Check(g.pPlaneSel.bUnk3024 == 1 && g.pPlaneCommit.bUnk3024 == 1,
          "committed stores track the two row panels");
    CheckEqI32(g.pPlaneSlot.nUnk3032, 100, "the slot panel is tracked and clicked");
    Check(CtlHit(&g.aPlane[PUFO_GEO_PLANE_STORE_CTL]) &&
          CtlHit(&g.aPlane[PUFO_GEO_PLANE_STORE_CTL + 1]), "the two store controls are hit");
    Check(Hit(GEOUI_CTL_186BA4), "and the confirm control");

    Fix_Reset();
    g.nScreenId = 6;
    g.bPlaneListBuilt = 1;
    Call(100, 100, true);
    CheckEqI32(g.pPlaneCrew.bUnk3024, 1, "the crew panel is tracked with the raw pointer");
    CheckEqI32(PanelRow(&g.pPlaneCrew), 100, "at the raw y");
    Check(CtlHit(&g.aPlane[PUFO_GEO_PLANE_CREW_CTL]) &&
          CtlHit(&g.aPlane[PUFO_GEO_PLANE_CREW_CTL + 1]), "the two crew controls are hit");

    Fix_Reset();
    g.nScreenId = 6;
    g.bPlaneDetail = 1;
    Call(100, 100, true);
    bool bWeapon = true;
    for (i32 i = 0; i < g.aCraft[0].bCrewSlots; ++i) {
        bWeapon = bWeapon && CtlHit(&g.aPlane[PUFO_GEO_PLANE_WEAPON_CTL + i]);
    }
    Check(bWeapon, "the craft detail hit-tests one control per Ship.bCrewSlots");
    Check(CtlHit(&g.aPlane[PUFO_GEO_PLANE_DOGFIGHT_CTL]) &&
          CtlHit(&g.aPlane[PUFO_GEO_PLANE_DOGFIGHT_CTL + 1]),
          "a craft that can dogfight gets its two controls");

    /* The weapon run and the dogfight pair share one array, so the boundary only shows
     * on a craft with one slot and no dogfight. */
    Fix_Reset();
    g.nScreenId = 6;
    g.bPlaneDetail = 1;
    g.aCraft[0].bCrewSlots = 1;
    g.aCraft[0].bCanDogfight = 0;
    Call(100, 100, true);
    Check(CtlHit(&g.aPlane[PUFO_GEO_PLANE_WEAPON_CTL]), "one crew slot gets one weapon control");
    Check(!CtlHit(&g.aPlane[PUFO_GEO_PLANE_WEAPON_CTL + 1]), "and nothing above it");
    Check(!CtlHit(&g.aPlane[PUFO_GEO_PLANE_DOGFIGHT_CTL]) &&
          !CtlHit(&g.aPlane[PUFO_GEO_PLANE_DOGFIGHT_CTL + 1]),
          "a craft that cannot dogfight gets neither of the pair");

    Fix_Reset();
    g.nScreenId = 6;
    g.bPlaneDetail = 1;
    g.bPlaneListBuilt = 1; /* the quiet gate closes */
    Call(100, 100, true);
    Check(!CtlHit(&g.aPlane[PUFO_GEO_PLANE_WEAPON_CTL]),
          "with the list panels built the weapon slots are not hit");

    Fix_Reset();
    g.nScreenId = 6;
    g.bStatAOpen = 1;
    g.anStatGate[0] = 1;
    g.anStatGate[1] = 0;
    g.anStatGate[2] = 3;
    Call(100, 100, true);
    Check(CtlHit(&g.aPlane[PUFO_GEO_PLANE_STAT_CTL_A]),
          "an open stat panel hit-tests its control");
    Check(CtlHit(&g.aPlane[PUFO_GEO_PLANE_STAT_CTL]), "a gate above zero releases its control");
    Check(!CtlHit(&g.aPlane[PUFO_GEO_PLANE_STAT_CTL + 1]), "a zero gate holds its control back");
    Check(CtlHit(&g.aPlane[PUFO_GEO_PLANE_STAT_CTL + 2]), "and a positive gate releases it");
    Check(!CtlHit(&g.aPlane[PUFO_GEO_PLANE_STAT_CTL + 3]), "the unset gates release nothing");
}

/* ------------------------------------------------- screen 7: the build dialog */
static void TestBuildDialog(void)
{
    printf("screen 7, the build dialog\n");

    Fix_Reset();
    g.nScreenId = 7;
    g.pFac.bFlag7073 = 0;
    Call(100, 100, true);
    CheckEqI32(g.bPopupA, 1, "screen 7 raises popup A");
    CheckEqI32(g.pFac.bFlag7073, 1, "with placement not armed the overlay flag is raised");
    Check(CtlHit(&g.aFac[0]) && CtlHit(&g.aFac[1]), "and the two dialog controls are hit");
    CheckEqI32(g.pFac.bUnk3024, 1, "the dialog panel is tracked");

    Fix_Reset();
    g.nScreenId = 7;
    g.pFac.bFlag7073 = 1;
    Call(230, 240, true); /* the stored cursor in the 220..250 corner */
    CheckEqI32(g.pFac.bFlag7073, 0, "the right corner hides the placement overlay");

    Fix_Reset();
    g.nScreenId = 7;
    g.pFac.bFlag7073 = 1;
    Call(20, 260, true); /* the raw pointer in the 0..40 / 251..300 corner */
    CheckEqI32(g.pFac.bFlag7073, 0, "the bottom-left corner hides it too");

    Fix_Reset();
    g.nScreenId = 7;
    g.pFac.bFlag7073 = 1;
    Call(230, 300, true);
    CheckEqI32(g.pFac.bFlag7073, 1, "y 300 is outside the corner band");

    Fix_Reset();
    g.nScreenId = 7;
    g.pFac.bFlag7073 = 0;
    g.bFacArmed = 1;
    Call(100, 100, true);
    CheckEqI32(g.pFac.bFlag7073, 0, "with placement armed the flag is not raised");
    Check(!CtlHit(&g.aFac[0]), "and the dialog controls are not hit");
    CheckEqI32(g.pFac.bUnk3024, 0, "and the panel is not tracked");
}

/* ------------------------------------------------- screens 8 and 9 */
static void TestResearchAndManuf(void)
{
    printf("screens 8 and 9, research and manufacturing\n");

    Fix_Reset();
    g.nScreenId = 8;
    Call(100, 100, true);
    CheckEqI32(g.pResearch.bUnk3024, 1, "with no row selected the topic panel is tracked");
    Check(CtlHit(&g.aResearch[0]) && CtlHit(&g.aResearch[1]), "the two row controls are hit");
    Check(!CtlHit(&g.aResearch[2]), "the confirm-row controls are not");
    Check(Hit(GEOUI_CTL_186B00), "and screen 8 ends on the close control");

    Fix_Reset();
    g.nScreenId = 8;
    g.bRowSelected = 1;
    Call(100, 100, true);
    CheckEqI32(g.pResearch.bUnk3024, 0, "with a row selected the topic panel is not tracked");
    Check(CtlHit(&g.aResearch[2]) && CtlHit(&g.aResearch[3]), "the confirm-row controls are hit");
    Check(!CtlHit(&g.aResearch[0]), "and the row controls are not");
    Check(Hit(GEOUI_CTL_186BB4) && Hit(GEOUI_CTL_186BB0) && Hit(GEOUI_CTL_186B94),
          "then the three shared controls");

    Fix_Reset();
    g.nScreenId = 9;
    Call(100, 100, true);
    CheckEqI32(g.bPopupB, 1, "screen 9 raises popup B");
    CheckEqI32(g.pManufProd.bUnk3024, 1, "the product panel is tracked at the stored cursor");
    CheckEqI32(PanelRow(&g.pManufProd), 100, "at the stored y");
    Check(CtlHit(&g.aManuf[0]) && CtlHit(&g.aManuf[1]), "the two row controls are hit");
    CheckEqI32(g.pManufD.nUnk3032, 100, "the detail panel is tracked and clicked");
    CheckEqI32(g.pManufD.bUnk3024, 0, "the click clears the hover flag");

    Fix_Reset();
    g.nScreenId = 9;
    g.bProductSelected = 1;
    Call(100, 100, true);
    Check(CtlHit(&g.aManuf[2]) && CtlHit(&g.aManuf[5]),
          "a selected product hits the four action controls");
    Check(!CtlHit(&g.aManuf[0]), "and not the two row controls");
    Check(Hit(GEOUI_CTL_186B90), "including the fourth shared control");
}

/* ------------------------------------------------- screens 10, 11 and 12 */
static void TestSiteAndBuySell(void)
{
    printf("screens 10, 11 and 12, the site list and buy/sell\n");

    Fix_Reset();
    g.nScreenId = 10;
    g.nRadarPulseTimer = 9;
    Call(100, 100, true);
    CheckEqI32(g.nRadarPulseTimer, 0, "a press resets the radar pulse timer");
    CheckEqI32(g.bPopupB, 1, "and raises popup B");
    Check(CtlHit(&g.aSiteInfo[0]) && CtlHit(&g.aSiteInfo[3]), "the four site controls are hit");
    CheckEqI32(g.pBuySell.nUnk3032, 100, "screen 10 ends on the buy/sell panel row click");

    Fix_Reset();
    g.nScreenId = 10;
    g.bTransferRow = 4;
    g.nRadarPulseTimer = 9;
    CheckEqI32(Call(100, 100, false), 4, "with a row selected screen 10 returns that flag");
    Check(Hit(GEOUI_CTL_186BA4), "and hit-tests the confirm control");
    Check(!CtlHit(&g.aSiteInfo[0]), "skipping the site controls");
    CheckEqI32(g.nRadarPulseTimer, 9, "a released pen does not reset the pulse timer");

    Fix_Reset();
    g.nScreenId = 11;
    Call(100, 100, true);
    Check(CtlHit(&g.aBuySell[0]) && CtlHit(&g.aBuySell[1]), "screen 11 hit-tests its two controls");
    Check(Hit(GEOUI_CTL_186B00) && Hit(GEOUI_CTL_186BA4), "then close and confirm");
    CheckEqI32(g.pBuySell.nUnk3032, 100, "and clicks the buy/sell row");

    Fix_Reset();
    g.nScreenId = 12;
    Call(100, 100, true);
    Check(CtlHit(&g.aBuySell[0]), "screen 12 is the same arm");
}

/* ------------------------------------------------- screen 16 */
static void TestScreen16(void)
{
    printf("screen 16, the startup list\n");

    Fix_Reset();
    g.nScreenId = 16;
    Call(100, 100, true);
    Check(!CtlHit(&g.a16[0]), "with both mode bytes clear no list control is hit");
    CheckEqI32(g.p16.bUnk3024, 1, "the default panel is the one tracked");
    CheckEqI32(g.p16Set.bUnk3024, 0, "not the selected set");

    Fix_Reset();
    g.nScreenId = 16;
    g.b16ModeA = 1;
    Call(100, 100, true);
    Check(CtlHit(&g.a16[4]) && CtlHit(&g.a16[5]), "mode A hit-tests controls 4 and 5");
    Check(!CtlHit(&g.a16[0]), "and not the lower ones");
    CheckEqI32(g.p16Set.bUnk3024, 1, "mode A tracks the panel from the selected set");
    CheckEqI32(PanelRow(&g.p16Set), 100, "at the stored cursor y");

    Fix_Reset();
    g.nScreenId = 16;
    g.b16ModeB = 1;
    g.b16Full = 1;
    Call(100, 100, true);
    Check(CtlHit(&g.a16[0]) && CtlHit(&g.a16[3]), "mode B with a full list hit-tests 0..3");
    Check(!CtlHit(&g.a16[4]), "and stops at 3");
    Check(g.p16.bUnk3024 == 0 && g.p16Set.bUnk3024 == 0, "mode B tracks no panel at all");

    Fix_Reset();
    g.nScreenId = 16;
    g.b16ModeB = 1;
    Call(100, 100, true);
    Check(!CtlHit(&g.a16[0]) && !CtlHit(&g.a16[1]), "mode B with a short list skips 0 and 1");
    Check(CtlHit(&g.a16[2]) && CtlHit(&g.a16[3]), "and hit-tests 2 and 3");
}

/* ------------------------------------------------- screen 18: options */
static void TestOptions(void)
{
    printf("screen 18, the options screen\n");

    Fix_Reset();
    g.nScreenId = 18;
    CheckEqI32(Call(100, 100, true), 100, "with the page byte clear the arm returns the cursor x");
    Check(!CtlHit(&g.aOptList[0]), "and nothing is hit-tested");

    Fix_Reset();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    CheckEqI32(Call(100, 100, true), 1, "page 0 returns the shared control's hit");
    Check(CtlHit(&g.aOptList[0]) && CtlHit(&g.aOptList[3]), "page 0 hit-tests list controls 0..3");
    Check(!CtlHit(&g.aOptList[4]), "and stops at 3");
    Check(Hit(GEOUI_CTL_186BB4), "then the shared control");

    Fix_Reset();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 2;
    Call(100, 100, true);
    Check(CtlHit(&g.aOptList[0]) && CtlHit(&g.aOptList[6]), "page 2 hit-tests all seven list controls");

    Fix_Reset();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 3;
    Call(100, 100, true);
    Check(Hit(GEOUI_CTL_186BB4) && Hit(GEOUI_CTL_186BB0), "page 3 hit-tests the two shared controls");
    Check(!CtlHit(&g.aOptList[0]), "and none of the list controls");

    Fix_Reset();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 4;
    Call(100, 100, true);
    bool bSound = true;
    for (i32 i = 0; i < PUFO_GEO_OPT_SOUND_CTLS; ++i) {
        bSound = bSound && CtlHit(&g.aOptMusic[i]) && CtlHit(&g.aOptSfx[i]);
    }
    Check(bSound, "page 4 hit-tests the six music and six sound controls");
    Check(!CtlHit(&g.aOptList[0]), "and none of the list controls");

    Fix_Reset();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 9;
    Call(100, 100, true);
    Check(!CtlHit(&g.aOptList[0]), "an unknown page hit-tests nothing");

    Fix_Reset();
    g.nScreenId = 18;
    g.nOptPage = -1; /* the byte is read SIGNED at 0x89128 */
    Call(100, 100, true);
    Check(!CtlHit(&g.aOptList[0]), "a negative page is not page 0");

    Fix_Reset();
    g.nScreenId = 18;
    g.bOptConfirm = 1;
    Call(100, 100, false);
    Check(!Hit(GEOUI_CTL_186BB4), "the confirm tail needs the pen down");

    Fix_Reset();
    g.nScreenId = 18;
    g.bOptConfirm = 1;
    CheckEqI32(Call(100, 100, true), 100, "the confirm tail returns the tracked row");
    Check(Hit(GEOUI_CTL_186BB4), "the tail hit-tests the shared control even with no page");
    Check(CtlHit(&g.aOptPrev) && CtlHit(&g.aOptNext), "and the prev/next controls");
    CheckEqI32(g.pOptShips.bUnk3024, 1, "tracking the ships panel");

    Fix_Reset();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.bOptConfirm = 1;
    g.nOptPage = 3;
    Call(100, 100, true);
    CheckEqI32(g.pOptShips.bUnk3024, 1, "page 3 still runs the confirm tail");
}

/* ------------------------------------------------- screen 20 and the default arm */
static void TestScreen20AndDefault(void)
{
    printf("screen 20 and the default arm\n");

    Fix_Reset();
    g.nScreenId = 20;
    CheckEqI32(Call(100, 100, true), 0, "with the base-at dialog done screen 20 returns 0");
    Check(!Hit(GEOUI_CTL_186BA4), "and hit-tests nothing");

    Fix_Reset();
    g.nScreenId = 20;
    g.bBaseAtDone = 1;
    CheckEqI32(Call(100, 100, true), 1, "while it is open screen 20 returns the confirm hit");
    Check(Hit(GEOUI_CTL_186BA4), "hit-testing the confirm control");

    Fix_Reset();
    g.nScreenId = 15;
    CheckEqI32(Call(100, 100, true), 1, "screen 15 is just the close control");
    Check(Hit(GEOUI_CTL_186B00), "and nothing else");
    Check(!Hit(GEOUI_CTL_186BA4), "not even the confirm control");

    Fix_Reset();
    g.nScreenId = 19;
    CheckEqI32(Call(100, 100, true), 1, "screen 19 is the same arm");

    Fix_Reset();
    g.nScreenId = 13;
    CheckEqI32(Call(100, 100, true), 100, "screen 13 falls to the default and returns the cursor x");
    Check(!Hit(GEOUI_CTL_186B00), "the default arm hit-tests nothing");

    Fix_Reset();
    g.nScreenId = 99;
    CheckEqI32(Call(-7, 100, true), -7, "an unknown screen id returns the cursor x unchanged");
}

/* =========================================== the release half, Geoscape_OnPointerUp @ 0x89450 */

/* Screen 0's menu band: the sample plays first, and the flag-37 pair is released BEFORE the two
 * menu controls - the opposite order from the press (0x894C0 vs 0x87384). */
static void TestUpMenuBand(void)
{
    printf("release, screen 0: the main-menu message band\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 0;
    g.oLines.abLineFlags[0] = 40;
    g.nSamplePlayed = 0;
    CheckEqI32(CallUp(), 1, "flag 40 returns the 187024 hit");
    Check(g.nSamplePlayed == 17, "flag 40 plays sample 17 (0x8949C)");
    Check(Up(GEOUI_CTL_18704C), "flag 40 releases 18704C");
    Check(Up(GEOUI_CTL_187024), "flag 40 releases 187024");
    Check(StillArmed(GEOUI_CTL_186BA4), "flag 40 does not release the dialog confirm");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 0;
    g.oLines.abLineFlags[0] = 37;
    CheckEqI32(CallUp(), 1, "flag 37 returns the 187024 hit");
    Check(Up(GEOUI_CTL_186BA4), "flag 37 releases 186BA4");
    Check(Up(GEOUI_CTL_186BAC), "flag 37 releases 186BAC");
    Check(Up(GEOUI_CTL_18704C), "flag 37 still releases 18704C after the pair");
    Check(Up(GEOUI_CTL_187024), "flag 37 still releases 187024");
    Check(g.aRoot[0].nState == 1, "the menu band never touches the root controls");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 0;
    g.oLines.abLineFlags[0] = 30; /* the band is exclusive at both ends (0x89478 / 0x8948C) */
    CheckEqI32(CallUp(), 0, "flag 30 is outside the band and falls through to the world arm");
    Check(g.nSamplePlayed == 0, "flag 30 plays no sample");
}

/* The world arm's flag chain, and the sample that almost every arm adds over the press. */
static void TestUpWorldFlags(void)
{
    printf("release, screens 0 and 2: the message-flag chain\n");

    struct { u8 nFlag; i32 nWantSample; i32 nWantRet; } aCases[] = {
        { 16, 0, 1 }, { 17, 17, 1 }, { 15, 17, 1 }, { 21, 17, 1 }, { 22, 17, 1 },
        { 23, 17, 1 }, { 12, 17, 1 }, { 14, 17, 1 }, { 10, 17, 1 }, { 60, 17, 1 },
        { 13, 17, 1 }, { 27, 17, 1 }, { 18, 17, 1 }, { 19, 17, 1 }, { 20, 17, 1 },
    };
    for (i32 i = 0; i < (i32)(sizeof(aCases) / sizeof(aCases[0])); ++i) {
        Fix_Reset();
        Fix_ArmAll();
        Fix_UpCursor();
        g.nScreenId = 2;
        g.bPlacementPending = 1;  /* keep the case-2 head quiet */
        g.oLines.abLineFlags[0] = aCases[i].nFlag;
        g.nSamplePlayed = 0;
        CheckEqI32(CallUp(), aCases[i].nWantRet, "the flag arm returns its last hit test");
        CheckEqI32(g.nSamplePlayed, aCases[i].nWantSample, "the flag arm plays the sample it owns");
    }

    /* Which control each arm ends on. */
    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.oLines.abLineFlags[0] = 16;
    (void)CallUp();
    Check(Up(GEOUI_CTL_186BA4), "flag 16 releases 186BA4");
    Check(Up(GEOUI_CTL_186BB4), "flag 16 releases 186BB4");
    Check(StillArmed(GEOUI_CTL_186BB0), "flag 16 does not release 186BB0");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.oLines.abLineFlags[0] = 21;
    (void)CallUp();
    Check(Up(GEOUI_CTL_186BB4), "flag 21 releases 186BB4 first");
    Check(Up(GEOUI_CTL_186BB0), "flag 21 releases 186BB0 last");
    Check(StillArmed(GEOUI_CTL_186BA4), "flag 21 does not release 186BA4");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.oLines.abLineFlags[0] = 14;
    (void)CallUp();
    Check(Up(GEOUI_CTL_186BB4), "flag 14 releases 186BB4 first");
    Check(Up(GEOUI_CTL_186BA4), "flag 14 releases 186BA4 last");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.oLines.abLineFlags[0] = 18;
    (void)CallUp();
    Check(Up(GEOUI_CTL_186BB4), "flag 18 without a map selection releases only 186BB4");
    Check(StillArmed(GEOUI_CTL_186BB0), "flag 18 without a map selection keeps 186BB0 armed");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.oLines.abLineFlags[0] = 19;
    g.bMapSelect = 1;
    CheckEqI32(CallUp(), 1, "flag 19 with a map selection returns the 186BB0 hit");
    Check(Up(GEOUI_CTL_186BB4), "flag 19 with a map selection releases 186BB4 first");
    Check(Up(GEOUI_CTL_186BB0), "flag 19 with a map selection releases 186BB0 last");

    /* Intercept: three controls, then the intercept-done byte decides. */
    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.oLines.abLineFlags[0] = 11;
    (void)CallUp();
    Check(Up(GEOUI_CTL_186BA8), "flag 11 releases 186BA8");
    Check(Up(GEOUI_CTL_186B9C), "flag 11 releases 186B9C");
    Check(Up(GEOUI_CTL_186B98), "flag 11 releases 186B98");
    Check(Up(GEOUI_CTL_186BB0), "flag 11 with no intercept done releases 186BB0");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.oLines.abLineFlags[0] = 11;
    g.bInterceptDone = 7;
    CheckEqI32(CallUp(), 7, "flag 11 with an intercept done hands back that byte (0x89A54)");
    Check(StillArmed(GEOUI_CTL_186BB0), "and it never reaches 186BB0");

    /* Target select: a hit on 186B8C returns before the flag chain. */
    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.bTargetSelect = 1;
    g.oLines.abLineFlags[0] = 17;
    g.nSamplePlayed = 0;
    CheckEqI32(CallUp(), 1, "a target-select hit returns 1 early");
    Check(Up(GEOUI_CTL_186B8C), "the target-select control is released");
    Check(StillArmed(GEOUI_CTL_186BB4), "the flag arm after it never runs");
    Check(g.nSamplePlayed == 0, "and its sample is not played");

    /* Radar panel. */
    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.bRadarActive = 1;
    CheckEqI32(CallUp(), PUFO_RADAR_CONTROLS, "the radar gate returns 5");
    Check(UpCtl(&g.aRadar[0]) && UpCtl(&g.aRadar[4]), "the radar gate releases all five controls");
    Check(StillArmed(GEOUI_CTL_186BB0), "and nothing after it");

    /* The multi-select panel goes through the shared click-and-flag7073 tail. */
    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.bMultiSelect = 1;
    g.pStatus.bFlag7073 = 0;
    CheckEqI32(CallUp(), 0, "the multi-select gate returns 0 (deviation 5: the original returns the panel)");
    Check(PanelRow(&g.pStatus) == 100, "the multi-select gate clicks the status panel row");
    Check(g.pStatus.bFlag7073 == 1, "and raises the panel's bFlag7073 (0x89B8C)");

    Check(g.bDragActive == 0, "the release clears the drag flag (0x896BC)");
}

/* The fall-through arm: no flag matched. */
static void TestUpWorldFallthrough(void)
{
    printf("release, screens 0 and 2: the no-flag fall-through\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.bTargetSelect = 1;
    g.aCtl[GEOUI_CTL_186B8C].nState = 0; /* down-free, so the gate falls through to the tail */
    CheckEqI32(CallUp(), 2, "screen 2 with no flag returns 2 (0x89CE0)");
    Check(g.bTargetSelect == 0, "and clears the target-select flag (0x89CD8)");
    Check(g.aRoot[0].nState == 1 && g.aRoot[12].nState == 1, "screen 2 does not release the root controls");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 0;
    CheckEqI32(CallUp(), 0, "screen 0 with no flag returns 0");
    bool bAll = true;
    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        bAll = bAll && g.aRoot[i].nState == 0;
    }
    Check(bAll, "screen 0 releases all thirteen root controls (0x89CF4..0x89D1C)");

    /* Screen 2's head: the close control unless a placement is pending, the confirm control
     * unless a region is pending. */
    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 0;
    g.bPendingRegion = 0xFF;
    (void)CallUp();
    Check(Up(GEOUI_CTL_186B00), "with no placement pending the close control is released");
    Check(StillArmed(GEOUI_CTL_186BA4), "with no region pending the confirm control is not");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 2;
    g.bPlacementPending = 1;
    g.bPendingRegion = 3;
    (void)CallUp();
    Check(StillArmed(GEOUI_CTL_186B00), "with a placement pending the close control is skipped");
    Check(Up(GEOUI_CTL_186BA4), "with a region pending the confirm control is released");
}

/* The shortfall gate on the dword at 0x1870C8 (0x89578..0x89600). */
static void TestUpShortfallGate(void)
{
    printf("release: the shortfall gate on 0x1870C8\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 5;
    g.nShortfall = 1;
    CheckEqI32(CallUp(), 0, "a shortfall of 1 on an unlisted screen returns before the arm");
    Check(g.nShortfall == 0, "and the report is cleared (0x895C4)");
    Check(StillArmed(GEOUI_CTL_186B00), "the screen-5 arm never runs");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 6;
    g.nShortfall = 1;
    (void)CallUp();
    Check(g.nShortfall == 0, "screen 6 clears the report and carries on (0x89600)");
    Check(Up(GEOUI_CTL_186B00), "and its arm runs normally");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 10;
    g.nShortfall = 1;
    (void)CallUp();
    Check(g.nShortfall == 0, "screen 10 clears the report and carries on");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 5;
    g.nShortfall = 2; /* only a value of exactly 1 takes the gate (0x89588) */
    (void)CallUp();
    Check(g.nShortfall == 2, "a shortfall of 2 is not the gate's value and survives");
    Check(Up(GEOUI_CTL_186B00), "and the arm runs");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 0;
    g.nShortfall = 1;
    g.oLines.abLineFlags[0] = 0;
    (void)CallUp();
    Check(g.nShortfall == 1, "screen 0 never reaches the gate (0x89594)");

    /* Screen 9 has its own branch: clear, then the product decides. */
    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 9;
    g.nShortfall = 1;
    g.bProductSelected = 0;
    g.bPopupA = 1;
    g.bPopupB = 1;
    CheckEqI32(CallUp(), 0, "screen 9 with no product selected returns 0");
    Check(g.bPopupA == 0 && g.bPopupB == 0, "and drops both popup bytes (0x895E0 / 0x895E4)");
    Check(StillArmed(GEOUI_CTL_186B00), "the manuf arm never runs");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 9;
    g.nShortfall = 1;
    g.bProductSelected = 1;
    g.nSamplePlayed = 0;
    (void)CallUp();
    Check(g.nSamplePlayed == 17, "screen 9 with a product selected plays the sample (0x895F4)");
    Check(Up(GEOUI_CTL_186B00), "and then runs the manuf arm");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 9;
    g.nShortfall = 0;
    g.bProductSelected = 1;
    g.nSamplePlayed = 0;
    (void)CallUp();
    Check(g.nSamplePlayed == 0, "without the gate screen 9 plays no sample of its own");
}

static void TestUpBaseStrip(void)
{
    printf("release, screen 1: the base strip\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 1;
    g.bPlaceActive = 0;
    CheckEqI32(CallUp(), 0, "with no tap pending the arm returns 0 (0x89DE0)");
    Check(Up(GEOUI_CTL_186B00), "the close control is released");
    bool bAll = true;
    for (i32 i = 0; i < PUFO_GEO_STRIP_CTLS; ++i) {
        bAll = bAll && g.aStrip[i].nState == 0;
    }
    Check(bAll, "all ten nav slots are released");
    Check(StillArmed(GEOUI_CTL_186BB4), "and the confirm control is not");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 1;
    g.bPlaceActive = 1;
    CheckEqI32(CallUp(), 1, "with a tap pending the arm returns the confirm hit");
    Check(Up(GEOUI_CTL_186BB4), "and releases the confirm control");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 1;
    g.apStrip[4] = NULL; /* the original null-checks only this run (0x89DA4) */
    CheckEqI32(CallUp(), 0, "a null nav slot is skipped, not fatal");
    bool bOthers = true;
    for (i32 i = 0; i < PUFO_GEO_STRIP_CTLS; ++i) {
        if (i == 4) {
            continue;
        }
        bOthers = bOthers && g.aStrip[i].nState == 0;
    }
    Check(bOthers, "and the run continues past it");
}

static void TestUpTransferAndPsi(void)
{
    printf("release, screens 3 and 17: the transfer rows and the psi panel\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 3;
    g.bTransferRow = 4;
    CheckEqI32(CallUp(), 4, "screen 3 with a row selected hands the flag back and stops");
    Check(StillArmed(GEOUI_CTL_186B00), "its controls are not released");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 3;
    CheckEqI32(CallUp(), 1, "screen 3 without a row returns the close hit");
    Check(UpCtl(&g.aBaseAt[0]) && UpCtl(&g.aBaseAt[1]), "both base-at controls are released");
    Check(Up(GEOUI_CTL_186B00), "and so is the close control");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 17;
    g.bTransferRow = 2;
    CheckEqI32(CallUp(), 2, "screen 17 with a row selected hands the flag back and stops");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 17;
    g.bPsiOpen = 1;
    g.bPsiRow = 1;
    g.nStripHitBase = 1;
    g.nTransferBase = 1;
    g.nCursorY = 50; /* inside 28 * nBaseCount = 84 */
    g.pPsi[1].pfnClick = HookPsiClickReArm;
    CheckEqI32(CallUp(), 1, "screen 17 always returns 1 (0x8ACA8)");
    Check(Up(GEOUI_CTL_186BA4), "186BA4 is released");
    Check(Up(GEOUI_CTL_186BAC), "186BAC is released");
    Check(PanelRow(&g.pPsi[1]) == 50, "the selected base's psi panel is clicked on row 50");
    Check(UpCtl(&g.aPsi[0]) && UpCtl(&g.aPsi[1]), "both psi controls are released");
    Check(g.bTransferRow == 0, "a strip hit on the selected base inside the rows drops the row the click handler raised (0x8AC9C)");
    Check(g.bPsiRow == 1, "and the psi row-selection byte is set to 1 (0x8ACA4)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 17;
    g.bPsiOpen = 1;
    g.bPsiRow = 0;
    g.nStripHitBase = 1;
    g.nTransferBase = 1;
    (void)CallUp();
    Check(NotClicked(&g.pPsi[1]), "with no psi row selected the panel is not clicked (0x8AC08)");
    Check(ArmedCtl(&g.aPsi[0]), "and the psi controls are gated out with it");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 17;
    g.bPsiOpen = 1;
    g.bPsiRow = 1;
    g.nStripHitBase = 2;
    g.nTransferBase = 1;
    g.pPsi[1].pfnClick = HookPsiClickReArm;
    (void)CallUp();
    Check(g.bTransferRow == 1, "a strip hit on a different base keeps the row the handler raised");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 17;
    g.nCursorY = 28 * GEO_TEST_BASES; /* the bound is strict: cursorY < 28 * nBaseCount */
    g.nStripHitBase = 1;
    g.nTransferBase = 1;
    g.bPsiOpen = 1;
    g.bPsiRow = 1;
    g.pPsi[1].pfnClick = HookPsiClickReArm;
    (void)CallUp();
    Check(g.bTransferRow == 1, "a row at or past 28 * nBaseCount keeps the selection");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 17;
    g.nStripHitBase = 1;
    g.nTransferBase = 1;
    g.bPsiOpen = 1;
    g.bPsiRow = 1;
    g.nCursorY = 50;
    g.nBaseCount = -1; /* read LDRSB at 0x8AC8C: the bound becomes -28, not 7140 */
    g.pPsi[1].pfnClick = HookPsiClickReArm;
    (void)CallUp();
    Check(g.bTransferRow == 1, "the base count is read SIGNED, so a negative one fails the bound");
}

static void TestUpSoldier(void)
{
    printf("release, screens 4 and 5: the soldier screen\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 4;
    g.bSolOpen = 1;
    CheckEqI32(CallUp(), 1, "screen 4 returns the close hit");
    Check(UpCtl(&g.aSolPanel[0]) && UpCtl(&g.aSolPanel[1]), "both page controls are released");
    Check(Clicked(&g.pSolNames), "the name list is clicked while the screen is open (0x89FBC)");
    Check(Up(GEOUI_CTL_186B00), "and the close control is released");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 4;
    g.bSolOpen = 0;
    (void)CallUp();
    Check(NotClicked(&g.pSolNames), "with the screen closed the name list is not clicked");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 5;
    g.bNameEdit = 1;
    g.bPopupB = 1;
    CheckEqI32(CallUp(), 1, "an active name edit returns the confirm hit");
    Check(g.bPopupB == 0, "and drops popup B (0x8A008)");
    Check(Up(GEOUI_CTL_186BA8), "the backspace control is released");
    Check(Up(GEOUI_CTL_186B9C), "the confirm control is released");
    Check(StillArmed(GEOUI_CTL_186B00), "and the arm stops there");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 5;
    g.bTransferFlag = 1;
    CheckEqI32(CallUp(), 100, "with the transfer flag the arm returns the equip row clicked");
    Check(Up(GEOUI_CTL_186B00), "the close control is released first");
    Check(Clicked(&g.pSolEquip), "the equip list is clicked (0x8A074)");
    Check(ArmedCtl(&g.aSolName[0]), "and the three find controls are not reached");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 5;
    CheckEqI32(CallUp(), 0, "without the transfer flag the arm returns 0");
    Check(UpCtl(&g.aSolName[0]) && UpCtl(&g.aSolName[2]), "all three find controls are released");
    Check(NotClicked(&g.pSolEquip), "and the equip list is not clicked");
}

static void TestUpCraft(void)
{
    printf("release, screen 6: the craft loadout\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 6;
    g.bPlaneBuilt = 1;
    g.bPlaneDetail = 0;
    g.bPopupA = 1;
    CheckEqI32(CallUp(), 1, "the arm returns the close hit");
    Check(g.bPopupA == 0, "popup A is cleared first (0x8A0CC)");
    Check(Clicked(&g.pPlaneCraft), "the craft list is clicked when its panels are built (0x8A100)");
    Check(NotClicked(&g.pPlaneStatus), "the craft status panel has no release counterpart");
    Check(NotClicked(&g.pPlaneSlot), "nor has the slot panel");
    Check(Up(GEOUI_CTL_186B00), "and the close control is released");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 6;
    g.bPlaneStores = 1;
    g.bPlaneDetail = 1;
    (void)CallUp();
    Check(Clicked(&g.pPlaneSel) && Clicked(&g.pPlaneCommit), "both store row lists are clicked (0x8A144)");
    Check(Up(GEOUI_CTL_186BA4), "and the confirm control is released (0x8A174)");
    Check(UpCtl(&g.aPlane[PUFO_GEO_PLANE_STORE_CTL]) &&
          UpCtl(&g.aPlane[PUFO_GEO_PLANE_STORE_CTL + 1]), "the two store controls are released (0x8A1CC)");
    Check(ArmedCtl(&g.aPlane[PUFO_GEO_PLANE_CREW_CTL]), "the crew pair is not reached without the list");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 6;
    g.bPlaneListBuilt = 1;
    g.bPlaneDetail = 1;
    (void)CallUp();
    Check(Clicked(&g.pPlaneCrew), "the crew list is clicked when its panels are built (0x8A120)");
    Check(UpCtl(&g.aPlane[PUFO_GEO_PLANE_CREW_CTL]) &&
          UpCtl(&g.aPlane[PUFO_GEO_PLANE_CREW_CTL + 1]), "the crew pair is released (0x8A214)");

    /* The weapon slots follow Ship.bCrewSlots, and only while no other plane panel is open. */
    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 6;
    g.bPlaneDetail = 1;
    g.aCraft[0].bCrewSlots = 3;
    (void)CallUp();
    Check(UpCtl(&g.aPlane[0]) && UpCtl(&g.aPlane[1]) && UpCtl(&g.aPlane[2]),
          "three crew slots release three weapon controls (0x8A2AC)");
    Check(ArmedCtl(&g.aPlane[4]), "the stat control at slot 4 is outside the crew range");
    Check(UpCtl(&g.aPlane[PUFO_GEO_PLANE_DOGFIGHT_CTL]) &&
          UpCtl(&g.aPlane[PUFO_GEO_PLANE_DOGFIGHT_CTL + 1]), "the dogfight pair is released (0x8A344)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 6;
    g.bPlaneDetail = 1;
    g.bStatAOpen = 1; /* the four-panel quiet gate closes */
    (void)CallUp();
    Check(ArmedCtl(&g.aPlane[0]), "with a stat panel open the weapon slots are not released");
    Check(ArmedCtl(&g.aPlane[PUFO_GEO_PLANE_DOGFIGHT_CTL]), "nor is the dogfight pair");
    Check(UpCtl(&g.aPlane[PUFO_GEO_PLANE_STAT_CTL_A]), "the stat control itself is (0x8A388)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 6;
    g.bStatBOpen = 1;
    g.anStatGate[0] = 1;
    g.anStatGate[3] = 2;
    (void)CallUp();
    Check(UpCtl(&g.aPlane[PUFO_GEO_PLANE_STAT_CTL]), "a stat gate above zero releases its control");
    Check(UpCtl(&g.aPlane[PUFO_GEO_PLANE_STAT_CTL + 3]), "in the order the original tests them");
    Check(ArmedCtl(&g.aPlane[PUFO_GEO_PLANE_STAT_CTL + 1]), "a zero gate keeps its control armed");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 6;
    g.aCraft[0].bCrewSlots = 0;
    g.aCraft[0].bCanDogfight = 0;
    g.bPlaneDetail = 1;
    (void)CallUp();
    Check(ArmedCtl(&g.aPlane[0]), "a craft with no crew slots releases no weapon control (0x8A290)");
    Check(ArmedCtl(&g.aPlane[PUFO_GEO_PLANE_DOGFIGHT_CTL]), "and no dogfight control (0x8A320)");
}

static void TestUpBuildDialog(void)
{
    printf("release, screen 7: the build dialog\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 7;
    g.bFacArmed = 0;
    g.nTapX = 77;
    g.nTapY = 88;
    g.bPopupA = 1;
    g.bPopupB = 0;
    CheckEqI32(CallUp(), 1, "the arm returns the close hit");
    Check(g.bPopupA == 0 && g.bPopupB == 1, "popup A is cleared and popup B raised (0x89E14 / 0x89E84)");
    Check(g.nPendingX == 77 && g.nPendingY == 88, "the tap is parked in nPendingPlaceX/Y (0x89E8C)");
    Check(UpCtl(&g.aFac[0]) && UpCtl(&g.aFac[1]), "both dialog controls are released (0x89E68)");
    Check(Clicked(&g.pFac), "the facility list is clicked while unarmed (0x89F20)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 7;
    g.bFacArmed = 1;
    g.pFac.bFlag7073 = 1;
    (void)CallUp();
    Check(ArmedCtl(&g.aFac[0]), "while armed the dialog controls are not released (0x89E40)");
    Check(NotClicked(&g.pFac), "and the list is not clicked (0x89F10)");
    Check(g.pFac.bFlag7073 == 1, "and the overlay flag is left alone");

    /* The two hot corners hide the placement overlay. */
    Fix_Reset();
    Fix_ArmAll();
    g.nScreenId = 7;
    g.bFacArmed = 1;
    g.nCursorX = 230;
    g.nCursorY = 240; /* 229 < y < 250 and x > 220 */
    (void)CallUp();
    Check(g.pFac.bFlag7073 == 0, "the bottom-right hot corner hides the overlay (0x89ECC)");

    Fix_Reset();
    Fix_ArmAll();
    g.nScreenId = 7;
    g.bFacArmed = 1;
    g.nCursorX = 20;
    g.nCursorY = 260; /* 251 < y < 300 and 0 < x < 40 */
    (void)CallUp();
    Check(g.pFac.bFlag7073 == 0, "the bottom-left hot corner hides it too (0x89F00)");

    Fix_Reset();
    Fix_ArmAll();
    g.nScreenId = 7;
    g.bFacArmed = 1;
    g.nCursorX = 20;
    g.nCursorY = 300; /* the band is exclusive at 300 */
    (void)CallUp();
    Check(g.pFac.bFlag7073 == 1, "a cursor at y == 300 is outside the band");
}

static void TestUpResearchAndManuf(void)
{
    printf("release, screens 8 and 9: research and manufacturing\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 8;
    g.bRowSelected = 0;
    CheckEqI32(CallUp(), 1, "screen 8 returns the close hit");
    Check(Clicked(&g.pResearch), "with no row selected the topic list is clicked (0x8A7B0)");
    Check(UpCtl(&g.aResearch[0]) && UpCtl(&g.aResearch[1]), "the two list controls are released");
    Check(ArmedCtl(&g.aResearch[2]), "the row controls are not");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 8;
    g.bRowSelected = 1;
    (void)CallUp();
    Check(NotClicked(&g.pResearch), "with a row selected the list is not clicked");
    Check(UpCtl(&g.aResearch[2]) && UpCtl(&g.aResearch[3]), "the row controls are released (0x8A840)");
    Check(ArmedCtl(&g.aResearch[0]), "the list controls are not");
    Check(Up(GEOUI_CTL_186BB4) && Up(GEOUI_CTL_186BB0) && Up(GEOUI_CTL_186B94),
          "and the confirm, cancel and info controls are released");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 9;
    g.bProductSelected = 0;
    CheckEqI32(CallUp(), 0, "screen 9 without a product returns 0");
    Check(Clicked(&g.pManufProd), "the product list is clicked (0x8A8F0)");
    Check(Up(GEOUI_CTL_186B00), "the close control is released");
    Check(UpCtl(&g.aManuf[0]) && UpCtl(&g.aManuf[1]), "the two list controls are released");
    Check(ArmedCtl(&g.aManuf[2]), "the product controls are not");
    Check(NotClicked(&g.pManufD), "the release has no panel-D click (the press does)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 9;
    g.bProductSelected = 1;
    CheckEqI32(CallUp(), 1, "screen 9 with a product returns the 186B90 hit");
    Check(UpCtl(&g.aManuf[2]) && UpCtl(&g.aManuf[5]), "four product controls are released (0x8A998)");
    Check(ArmedCtl(&g.aManuf[0]), "the list controls are not");
    Check(Up(GEOUI_CTL_186BB4) && Up(GEOUI_CTL_186BB0) && Up(GEOUI_CTL_186B94) &&
          Up(GEOUI_CTL_186B90), "and the four dialog controls are released in order");
}

static void TestUpSiteAndBuySell(void)
{
    printf("release, screens 10, 11 and 12: site info and buy/sell\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 10;
    g.bPopupB = 1;
    CheckEqI32(CallUp(), 1, "screen 10 returns the close hit");
    Check(g.bPopupB == 0, "popup B is cleared (0x8A64C)");
    Check(Clicked(&g.pBuySellB) && Clicked(&g.pBuySellC),
          "both buy/sell lists are clicked (0x8A65C / 0x8A670)");
    Check(NotClicked(&g.pBuySell), "the panel the press arm tracks is not clicked here");
    Check(UpCtl(&g.aSiteInfo[0]) && UpCtl(&g.aSiteInfo[3]), "all four site-info controls are released");
    Check(Up(GEOUI_CTL_186B00), "and the close control is released");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 10;
    g.bTransferRow = 1;
    CheckEqI32(CallUp(), 1, "with a transfer row selected screen 10 returns the confirm hit");
    Check(Up(GEOUI_CTL_186BA4), "and releases only the confirm control (0x8A680)");
    Check(ArmedCtl(&g.aSiteInfo[0]), "the site-info controls are not reached");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 11;
    CheckEqI32(CallUp(), 1, "screen 11 returns the confirm hit");
    Check(Clicked(&g.pBuySellB) && Clicked(&g.pBuySellC), "both lists are clicked");
    Check(UpCtl(&g.aBuySell[0]) && UpCtl(&g.aBuySell[1]), "both buy/sell controls are released");
    Check(Up(GEOUI_CTL_186B00) && Up(GEOUI_CTL_186BA4), "then close and confirm (LABEL_153)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 12;
    (void)CallUp();
    Check(Up(GEOUI_CTL_186B00) && Up(GEOUI_CTL_186BA4), "screen 12 shares the screen-11 tail");
    Check(ArmedCtl(&g.aSiteInfo[0]), "and it does not touch the site-info controls");
}

static void TestUpScreen16(void)
{
    printf("release, screen 16: the startup / load list\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 16;
    g.b16ModeA = 1;
    g.b16ModeB = 0;
    CheckEqI32(CallUp(), 1, "the arm returns the close hit");
    Check(UpCtl(&g.a16[4]) && UpCtl(&g.a16[5]), "mode A releases the two side controls (0x8AA70)");
    Check(Clicked(&g.p16Set), "and clicks the panel from the panel set (0x8AB64)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 16;
    g.b16ModeA = 0;
    g.b16ModeB = 1;
    g.b16Full = 1;
    (void)CallUp();
    Check(UpCtl(&g.a16[0]) && UpCtl(&g.a16[3]), "mode B with a full list releases slots 0..3");
    Check(ArmedCtl(&g.a16[4]), "and not the side controls");
    Check(NotClicked(&g.p16), "with mode B set no panel is clicked (0x8AB30)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 16;
    g.b16ModeB = 1;
    g.b16Full = 0;
    (void)CallUp();
    Check(ArmedCtl(&g.a16[0]) && UpCtl(&g.a16[2]) && UpCtl(&g.a16[3]),
          "mode B with a short list releases only slots 2 and 3");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 16;
    g.b16ModeA = 0;
    g.b16ModeB = 0;
    (void)CallUp();
    Check(Clicked(&g.p16), "with neither mode the single panel is clicked (0x8AB50)");
}

static void TestUpOptions(void)
{
    printf("release, screen 18: the options pages\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptPageActive = 0;
    g.bOptConfirm = 0;
    CheckEqI32(CallUp(), 0, "with the page inactive nothing is released and the arm returns 0");
    Check(ArmedCtl(&g.aOptList[0]), "no page control is touched");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 0;
    (void)CallUp();
    Check(UpCtl(&g.aOptList[0]) && UpCtl(&g.aOptList[3]), "page 0 releases four list controls (0x8AE80)");
    Check(ArmedCtl(&g.aOptList[4]), "and no more");
    Check(Up(GEOUI_CTL_186BB4), "then the shared tail releases 186BB4 (LABEL_228)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 1;
    (void)CallUp();
    Check(UpCtl(&g.aOptList[0]) && UpCtl(&g.aOptList[6]), "page 1 releases all seven list controls");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 2;
    (void)CallUp();
    Check(UpCtl(&g.aOptList[0]) && UpCtl(&g.aOptList[6]), "page 2 is the same arm as page 1");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 3;
    CheckEqI32(CallUp(), 1, "page 3 returns the close hit and stops (0x8AD98)");
    Check(StillArmed(GEOUI_CTL_186BB0), "the cancel control is not reached");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 3;
    g.aCtl[GEOUI_CTL_186BB4].nState = 0; /* the close control was not down: no hit */
    CheckEqI32(CallUp(), 1, "with the close control down-free page 3 falls to the cancel hit");
    Check(Up(GEOUI_CTL_186BB0), "and the cancel control is released (0x8ADB4)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 4;
    (void)CallUp();
    bool bSound = true;
    for (i32 i = 0; i < PUFO_GEO_OPT_SOUND_CTLS; ++i) {
        bSound = bSound && g.aOptMusic[i].nState == 0 && g.aOptSfx[i].nState == 0;
    }
    Check(bSound, "page 4 releases the twelve sound controls, slots 9..20 (0x8AD3C)");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptPageActive = 1;
    g.nOptPage = 5;
    (void)CallUp();
    Check(ArmedCtl(&g.aOptList[0]), "an unknown page releases no list control (0x8AD18)");

    /* The confirm tail, reached from every page arm. */
    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptPageActive = 0;
    g.bOptConfirm = 1;
    g.pOptShips.bFlag7073 = 0;
    CheckEqI32(CallUp(), 0, "the confirm tail returns 0 (deviation 5: the original returns the panel)");
    Check(Up(GEOUI_CTL_186BB4), "it releases 186BB4 (0x8AED4)");
    Check(UpCtl(&g.aOptPrev) && UpCtl(&g.aOptNext), "and the prev / next pair, slots 7 and 8 (0x8AEEC)");
    Check(PanelRow(&g.pOptShips) == 100,
          "then clicks the ships panel through the shared tail (0x8AF1C -> 0x89B64)");
    Check(g.pOptShips.bFlag7073 == 1, "and raises its bFlag7073");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 18;
    g.bOptConfirm = 0;
    (void)CallUp();
    Check(ArmedCtl(&g.aOptPrev), "with no confirm the tail stops before the pair");
    Check(NotClicked(&g.pOptShips), "and the ships panel is not clicked");
}

static void TestUpScreen20AndDefault(void)
{
    printf("release, screen 20 and the default arms\n");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 20;
    g.bBaseAtDone = 0;
    CheckEqI32(CallUp(), 0, "screen 20 with the base not placed returns 0");
    Check(StillArmed(GEOUI_CTL_186BA4), "and releases nothing");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 20;
    g.bBaseAtDone = 1;
    CheckEqI32(CallUp(), 1, "screen 20 with the base placed returns the confirm hit");
    Check(Up(GEOUI_CTL_186BA4), "and releases the confirm control");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 15;
    CheckEqI32(CallUp(), 1, "screen 15 releases only the close control");
    Check(StillArmed(GEOUI_CTL_186BA4), "and nothing else");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 19;
    (void)CallUp();
    Check(Up(GEOUI_CTL_186B00), "screen 19 shares the screen-15 arm");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 13;
    CheckEqI32(CallUp(), 0, "screen 13 takes the default arm (deviation 5: the original returns an address)");
    Check(ArmedCtl(&g.aSolName[0]), "and no control is released");

    Fix_Reset();
    Fix_ArmAll();
    Fix_UpCursor();
    g.nScreenId = 14;
    (void)CallUp();
    Check(StillArmed(GEOUI_CTL_186B00), "screen 14 likewise");
}

int main(void)
{
    TestMenuBand();
    TestWorldFlags();
    TestGlobeDrag();
    TestBaseStrip();
    TestTransferRow();
    TestSoldierScreen();
    TestCraftScreen();
    TestBuildDialog();
    TestResearchAndManuf();
    TestSiteAndBuySell();
    TestScreen16();
    TestOptions();
    TestScreen20AndDefault();

    TestUpMenuBand();
    TestUpWorldFlags();
    TestUpWorldFallthrough();
    TestUpShortfallGate();
    TestUpBaseStrip();
    TestUpTransferAndPsi();
    TestUpSoldier();
    TestUpCraft();
    TestUpBuildDialog();
    TestUpResearchAndManuf();
    TestUpSiteAndBuySell();
    TestUpScreen16();
    TestUpOptions();
    TestUpScreen20AndDefault();

    printf("\ngeopointer: %d checks, %d failures\n", g_nChecks, g_nFailures);
    return g_nFailures != 0 ? 1 : 0;
}
