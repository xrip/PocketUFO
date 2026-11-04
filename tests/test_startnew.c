/* test_startnew.c - Game_StartNew @ 0x94554 against the shipped archives.
 *
 * The new-game state is built over the real object records and text records, so the
 * facility names ("Hangar", "Access Lift") and the object slots ("Elerium-115") are
 * ground truth out of main.dat, not port-internal expectations.
 *
 * The starting state the original leaves behind: one base ("new" at -6000,-6000), ten
 * placed facilities, six base-stock grants, a type-0 craft with twenty
 * cargo adds, two type-3 craft with object slots 25/26 and weapons 25/26, eight soldiers,
 * funds 4147000, and the spawn pass run through the strateg hooks.
 */
#include "game/pufo_boot.h"
#include "game/pufo_clock.h"
#include "game/pufo_geoscape.h"
#include "game/pufo_objectrec.h"
#include "game/pufo_save.h"
#include "game/pufo_ship.h"
#include "game/pufo_strateg.h"
#include "game/pufo_unit.h"
#include "game/pufo_baseui.h"
#include "ui/pufo_control.h"
#include "ui/pufo_geoiinit.h"
#include "util/pufo_ptrarray.h"

#include <stdio.h>
#include <stdlib.h>
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

/* Independent expected data from the raw Game_StartNew BL arguments. These are Map.apObjects
 * indices and quantities, not the production startup tables or tactical OBJ_* names. */
typedef struct RawStartGrant {
    i32 nObjectSlot;
    i32 nQuantity;
} RawStartGrant;

static const RawStartGrant g_aRawExpectedBase0[] = {
    {  27,  1 }, {  52, 20 }, {  53, 15 }, {  54, 10 }, { 112, 10 }, { 113, 10 },
};

static const RawStartGrant g_aRawExpectedCraft0[] = {
    {  1,  3 }, {  4,  6 }, {  7,  2 }, {  8,  2 }, { 10,  1 },
    { 14,  4 }, { 15,  4 }, { 16,  4 }, { 17,  4 }, { 36,  4 },
    { 38, 10 }, { 40,  2 }, { 41,  2 }, { 42,  2 }, { 43,  2 },
    { 44,  2 }, { 45,  2 }, { 46,  2 }, { 47,  1 }, { 48,  1 },
};

static const RawStartGrant g_aRawExpectedFighter[] = {
    { 25, 1 }, { 26, 1 },
};

static i32 RawExpectedQty(const RawStartGrant *aGrants, i32 nGrantCount, i32 nObjectSlot)
{
    for (i32 i = 0; i < nGrantCount; ++i) {
        if (aGrants[i].nObjectSlot == nObjectSlot) {
            return aGrants[i].nQuantity;
        }
    }
    return 0;
}

/* The state Game_StartNew writes through its environment. */
static Soldier  *apSoldiers[16];
static i32       nSoldierCount;
static i32       nNextSoldierId;
static Ship     *apCraft[8];
static u8        bCraftCount;
static i32       nNextCraftId;      /* 0x186550 */
static BaseRec  *apBases[8];
static i8        nBaseCount;        /* 0x159D80 */
static i8        nSelectedBase;     /* 0x159D81 */
static i32       nFunds;            /* 0x1598D8 */
static i32       nMonthCounter;     /* unk_159648 */
static u16       nAlienPoolAddend;  /* 0x1472c8 */
static GameTime  g_time;

static u8   grid[PUFO_GRID_SIZE];
static u16  prodU16[1600];          /* auManufCommitted @ 0x15D09C, 200 per base */
static i32  prodReq[1600];          /* aManufProductB @ 0x15B79C, 200 per base */
static u32  prodU32[1600];          /* the accumulator @ 0x159E9C, 200 per base */
static u16  manuf[1600];            /* auManufCol @ 0x15E19C, 200 per base: engineers assigned */
static u16  researchCol[PUFO_RESEARCH_TOPIC_COUNT * PUFO_FAC_BASE_COUNT];
static u8   resZero[PUFO_RESEARCH_TOPIC_COUNT];
static u8   resActive[PUFO_RESEARCH_TOPIC_COUNT];
static ResearchTopicLive aTopics[PUFO_RESEARCH_TOPIC_COUNT];

static SmapSite *apSites;           /* 0x162030, 200 records */
static i32       nSiteCount;        /* 0x186410 */
static i32       radarPct[8];       /* unk_162008, eight dwords */
static PtrArray  strategList;
static i32       nStrategSeed;      /* 0x18640C, shared by the three callees */
static i32       nZoomMode;         /* 0x1863E8 */

static u8   bPlacementPending;      /* 0x1870AC */
static i32  nStrategCount;          /* 0x186530, the geoscape block's +0x38 strateg count dword */
static u8   abTypeCounters[5];      /* 0x18653C..0x186540 */
static i32  nNextDisplayId;         /* 0x186544 */
static i32  nNextSiteId;            /* 0x186548 */
static i32  nMissionId;             /* 0x18654C */
static i32  nCreateNextSiteId;      /* dword_186558, Base_CreateNamedEntry's own counter */
static u8   aRegionBuildCount[10];        /* 0x18655D */
static GeoscapeEconomy economy;
static i32  anFundTable[8];         /* the 0x14B4C8 zero buffer */

static BaseFacFlag aFac[PUFO_BASEFAC_COUNT];
static GeoscapeScreenState screen;
static UiControl ctlResearchPanel;  /* unk_186BB4 */
static UiControl ctlConfirm;        /* unk_186BA4 */
static SmapView view;               /* 0x1863E8 */
static SmapZoomSet sets[3];
static SmapWorldSize worldSize;
static u8 bFlag187080;
static NavControls nav;
static NavCtlEnv navEnv;
static BaseSiteRec aSiteRecs[8];    /* 0x186C40 */
static u8 paRegionBuildCount[8];           /* g_anRegionBuildCount @ 0x18655D */

static i32 g_nSpawnCalls;
static i32 g_nEventCalls;
static i32 g_nNavClickedSlot = -1;

static volatile i32 g_nConfirmSeamFired = -1; /* a distinct body: an empty one folds with any other */
static void NoopClick(void) { g_nConfirmSeamFired = 1; }

#define DEFINE_NAVCLICK(i) static void NavClick##i(void) { g_nNavClickedSlot = (i); }
DEFINE_NAVCLICK(0)  DEFINE_NAVCLICK(1)  DEFINE_NAVCLICK(2)  DEFINE_NAVCLICK(3)
DEFINE_NAVCLICK(4)  DEFINE_NAVCLICK(5)  DEFINE_NAVCLICK(6)  DEFINE_NAVCLICK(7)
DEFINE_NAVCLICK(8)  DEFINE_NAVCLICK(9)  DEFINE_NAVCLICK(10) DEFINE_NAVCLICK(11)
DEFINE_NAVCLICK(12) DEFINE_NAVCLICK(13) DEFINE_NAVCLICK(14)

static bool NavLoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2);
    return true;
}

static int SpawnProbe(StrategItem *pUfo, i32 nMissionSub, void *pUser)
{
    (void)pUfo;
    (void)nMissionSub;
    (void)pUser;
    ++g_nSpawnCalls;
    return 0;
}

static int EventProbe(void *pUser)
{
    (void)pUser;
    ++g_nEventCalls;
    return 0;
}

/* The IDB-verified roll tables (unk_AAC98, byte_AACD8, byte_AAD08), as tests/test_strateg.c
 * uses them. */
static const StrategRollTables g_Roll = {
    {
        { 100, 0, 0, 0, 0, 0, 0, 0 },
        { 100, 0, 0, 0, 0, 0, 0, 0 },
        { 100, 0, 0, 0, 0, 0, 0, 0 },
        {  50, 50, 0, 0, 0, 0, 0, 0 },
        {  50, 0, 50, 0, 0, 0, 0, 0 },
        {  20, 0, 0, 40, 0, 40, 0, 0 },
        {   0, 0, 0, 30, 0, 0, 70, 25 },
        {   0, 0, 60, 0, 0, 15, 0, 0 },
    },
    {
        { 22, 33, 35,  5,  5,  0,  0,  0 },
        { 10, 30, 35, 10, 10,  5,  0,  0 },
        {  8, 20, 20, 15, 15,  7, 10,  5 },
        {  0, 20, 20, 15, 15,  8, 17,  5 },
        {  0, 15, 15, 15, 15,  8, 20, 12 },
        {  0, 10, 15, 15, 15,  8, 22, 15 },
    },
    {
        { 100, 0, 0, 0, 0 },
        {  70, 0, 30, 0, 0 },
        {  33, 0, 33, 34, 0 },
        {  25, 25, 25, 25, 0 },
        {  20, 20, 20, 20, 20 },
        {  20, 20, 20, 20, 20 },
    },
};

/* Base 0 only: the fac block is grid + 0x174, the layers are tileA +0, tileB +36,
 * layer2 +72, facType +108; buildDays is grid + 6*nY + nX. */
#define FACCELL(layer, y, x) grid[PUFO_GRID_FAC_OFF + (layer) + 6 * (y) + (x)]
#define DAYS(y, x)           grid[6 * (y) + (x)]

static void TestStartNew(void)
{
    UnitCreateEnv unitEnv;
    ShipCtorEnv ctorEnv;
    ShipSpawnEnv shipEnv;
    BaseCreateEnv createEnv;
    StrategSpawnCtx spawnCtx;
    StartNewEnv env;
    i32 i, c;
    i32 nRet;

    printf("Game_StartNew @ 0x94554 against the shipped archives\n");

    /* Sentinels, so "untouched" is observable. */
    nFunds = -1;
    nMonthCounter = 999;
    nAlienPoolAddend = 0x5A5A;
    nBaseCount = 0;             /* the radar pass runs over the count as it stands */
    nSelectedBase = 9;
    bCraftCount = 0;
    nSoldierCount = 0;
    nNextSoldierId = 1;
    nNextCraftId = 0;
    nSiteCount = 0xAA; /* StartNew stores only the low byte (0x947EC), so the upper half
                        * stays as it was; the original's state has it zero here. */
    nStrategSeed = 0;
    nZoomMode = 0;
    bPlacementPending = 9;
    nStrategCount = 9;
    nNextDisplayId = 9;
    nNextSiteId = 9;
    nMissionId = 9;
    nCreateNextSiteId = 1;
    memset(abTypeCounters, 9, sizeof(abTypeCounters));
    memset(aRegionBuildCount, 0, sizeof(aRegionBuildCount));
    memset(radarPct, 0xAA, sizeof(radarPct));
    memset(grid, 0x5A, sizeof(grid));
    memset(prodU16, 0xAA, sizeof(prodU16));
    memset(prodReq, 0xAA, sizeof(prodReq));
    memset(prodU32, 0xAA, sizeof(prodU32));
    memset(manuf, 0xBB, sizeof(manuf));
    memset(researchCol, 0xAA, sizeof(researchCol));
    memset(resZero, 0xAA, sizeof(resZero));
    memset(resActive, 0xAA, sizeof(resActive));
    memset(aTopics, 0, sizeof(aTopics));
    for (i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        aTopics[i].bActive = (u8)((i % 3) == 0);
    }
    for (i = 0; i < PUFO_SMAP_SITE_COUNT; ++i) {
        apSites[i].bFlag0 = 0xAA;
        apSites[i].bFlag1 = 0xAA;
        apSites[i].bUnk518 = 0xAA;
        apSites[i].bRecoveredSoldierAssign = 0xAA;
        apSites[i].uRecoveredSoldiers = 0xAAAA;
        apSites[i].bRecoveredCraftCount = 0xAA;
        for (c = 0; c < 256; ++c) {
            apSites[i].auItemQty[c] = 0xAAAA;
        }
    }
    for (i = 0; i < (i32)g_assets.nObjectCount; ++i) {
        ObjectRecFull *pObj = g_assets.apObjects[i];
        for (c = 0; c < PUFO_FAC_BASE_COUNT; ++c) {
            pObj->auPerBaseVal[c] = 0xAAAA;
        }
        for (c = 0; c < PUFO_ITEM_QTY_COUNT; ++c) {
            pObj->aItemQty[c] = 0x5A5A;
        }
    }
    for (i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
        memset(aFac[i].szName, 'X', sizeof(aFac[i].szName));
        aFac[i].bFlag20 = 9;
        aFac[i].bFlag21 = 9;
        aFac[i].uCost = 0xAAAAAAAAu;
    }
    memset(&economy, 0xFF, sizeof(economy));
    memset(anFundTable, 0, sizeof(anFundTable));
    screen.nGeoscapeScreen = 9;
    ctlResearchPanel.x = -1;
    ctlResearchPanel.y = -1;
    ctlConfirm.pfnClick = NULL;
    view.nMode = 2;             /* StartNew zooms out twice: 2 -> 1 -> 0 */
    view.nScrollX = 100;
    view.nScrollY = 100;
    sets[0].uWorldW = 111;
    sets[0].uWorldH = 222;
    sets[1].uWorldW = 333;
    sets[1].uWorldH = 444;
    sets[2].uWorldW = 555;
    sets[2].uWorldH = 666;
    worldSize.uW = 0;
    worldSize.uH = 0;
    bFlag187080 = 9;
    g_time.nTotalSec = 123456;
    g_time.bPaused = 1;
    memset(aSiteRecs, 0xFF, sizeof(aSiteRecs));
    memset(paRegionBuildCount, 0, sizeof(paRegionBuildCount));
    memset(&nav, 0xFF, sizeof(nav));
    PtrArray_ctor(&strategList);
    for (i = 0; i < 16; ++i) {
        apSoldiers[i] = NULL;
    }
    for (i = 0; i < 8; ++i) {
        apCraft[i] = NULL;
        apBases[i] = NULL;
    }

    memset(&unitEnv, 0, sizeof(unitEnv));
    unitEnv.apSoldiers = apSoldiers;
    unitEnv.pnSoldierCount = &nSoldierCount;
    unitEnv.pnNextSoldierId = &nNextSoldierId;

    memset(&ctorEnv, 0, sizeof(ctorEnv));
    ctorEnv.apObjects = g_assets.apObjects;
    ctorEnv.nObjectCount = (i32)g_assets.nObjectCount;

    memset(&shipEnv, 0, sizeof(shipEnv));
    shipEnv.apCraft = apCraft;
    shipEnv.pnCraftCount = &bCraftCount;
    shipEnv.pnNextCraftId = &nNextCraftId;
    shipEnv.apBases = apBases;
    shipEnv.abFacilityGrid = grid;
    shipEnv.pCtorEnv = &ctorEnv;
    shipEnv.pStrategList = &strategList;
    shipEnv.pnStrategSeed = &nStrategSeed;
    shipEnv.pnZoomMode = &nZoomMode;

    memset(&createEnv, 0, sizeof(createEnv));
    createEnv.apBases = apBases;
    createEnv.apObjects = g_assets.apObjects;
    createEnv.nObjectCount = g_assets.nObjectCount;
    createEnv.pbBaseCount = &nBaseCount;
    createEnv.pStrategList = &strategList;
    createEnv.pnSeed = &nStrategSeed;
    createEnv.nZoomMode = nZoomMode;
    createEnv.pnNextSiteId = &nCreateNextSiteId;
    createEnv.apSiteRecs = aSiteRecs;
    createEnv.paRegionBuildCount = paRegionBuildCount;
    createEnv.pProductU32Col = prodU32;
    createEnv.pProductReqCol = prodReq;
    createEnv.pProductU16Col = prodU16;
    createEnv.pManufCol = manuf;
    createEnv.pResearchCol = researchCol;

    memset(&spawnCtx, 0, sizeof(spawnCtx));
    spawnCtx.pTables = &g_Roll;
    spawnCtx.pnDifficulty = &nMonthCounter;    /* the same two cells env.pnMonthCounter/pnAlienPoolAddend
                                                * point at, so the roll reads what the clock writes */
    spawnCtx.pnEarlyGate = &nAlienPoolAddend;
    spawnCtx.nZoomMode = nZoomMode;
    spawnCtx.pnSeed = &nStrategSeed;
    spawnCtx.pfnSpawnMission = SpawnProbe;
    spawnCtx.pfnProcessEvents = EventProbe;

    navEnv.apfnNavClick[0] = NavClick0;
    navEnv.apfnNavClick[1] = NavClick1;
    navEnv.apfnNavClick[2] = NavClick2;
    navEnv.apfnNavClick[3] = NavClick3;
    navEnv.apfnNavClick[4] = NavClick4;
    navEnv.apfnNavClick[5] = NavClick5;
    navEnv.apfnNavClick[6] = NavClick6;
    navEnv.apfnNavClick[7] = NavClick7;
    navEnv.apfnNavClick[8] = NavClick8;
    navEnv.apfnNavClick[9] = NavClick9;
    navEnv.apfnPanelClick[0] = NavClick10;
    navEnv.apfnPanelClick[1] = NavClick11;
    navEnv.apfnFindClick[0] = NavClick12;
    navEnv.apfnFindClick[1] = NavClick13;
    navEnv.apfnFindClick[2] = NavClick14;
    navEnv.pfnLoadPads = NavLoadPads;

    memset(&env, 0, sizeof(env));
    env.apObjects = g_assets.apObjects;
    env.nObjectCount = g_assets.nObjectCount;
    env.apSoldiers = apSoldiers;
    env.pnSoldierCount = &nSoldierCount;
    env.apCraft = apCraft;
    env.pbCraftCount = &bCraftCount;
    env.apBases = apBases;
    env.pbBaseCount = &nBaseCount;
    env.pnSelectedBaseIdx = &nSelectedBase;
    env.pnFunds = &nFunds;
    env.pnMonthCounter = &nMonthCounter;
    env.pnAlienPoolAddend = &nAlienPoolAddend;
    env.pTime = &g_time;
    env.abFacilityGrid = grid;
    env.pProductU16Col = prodU16;
    env.pProductReqCol = prodReq;
    env.pProductU32Col = prodU32;
    env.pManufCol = manuf;
    env.pResearchCol = researchCol;
    env.abResearchZero = resZero;
    env.abResearchActive = resActive;
    env.bResearchCount = PUFO_RESEARCH_TOPIC_COUNT;
    env.paTopics = aTopics;
    env.apSites = apSites;
    env.pnSiteCount = &nSiteCount;
    env.apRadarPct = radarPct;
    env.pStrategList = &strategList;
    env.pSpawnCtx = &spawnCtx;
    env.pbPlacementPending = &bPlacementPending;
    env.pnStrategCount = &nStrategCount;
    env.abTypeCounters = abTypeCounters;
    env.pnNextDisplayId = &nNextDisplayId;
    env.pnNextSiteId = &nNextSiteId;
    env.pnMissionId = &nMissionId;
    env.paRegionBuildCount = aRegionBuildCount;
    env.pEconomy = &economy;
    env.paFacFlags = aFac;
    env.pTextRecs = g_assets.pTextRecs;
    env.pScreen = &screen;
    env.pCtlResearchPanel = &ctlResearchPanel;
    env.pCtlConfirm = &ctlConfirm;
    env.pView = &view;
    env.pZoomSets = sets;
    env.pWorldOut = &worldSize;
    env.pbFlag187080 = &bFlag187080;
    env.pfnTryConfirmClick = NoopClick;
    env.pNav = &nav;
    env.pNavEnv = &navEnv;
    env.pUnitEnv = &unitEnv;
    env.pShipEnv = &shipEnv;
    env.pCreateEnv = &createEnv;

    nRet = Game_StartNew(&env);
    printf("  (MaybeSpawnCraft_Process returned %d, spawn hook ran %d, event hook %d)\n",
           (int)nRet, (int)g_nSpawnCalls, (int)g_nEventCalls);

    /* The counters and flags. */
    CheckEqI32(nFunds, 4147000, "funds");
    CheckEqI32(nMonthCounter, 0, "month counter");
    CheckEqI32(nBaseCount, 1, "base count after Base_CreateNamedEntry");
    CheckEqI32(nSelectedBase, 0, "selected base");
    CheckEqI32(nSoldierCount, PUFO_STARTNEW_SOLDIER_COUNT, "soldier count");
    CheckEqI32(bCraftCount, 3, "craft count");
    Check(bPlacementPending == 1, "placement pending (0x1870AC)");
    CheckEqI32(nStrategCount, 0, "the strateg count dword at block +0x38 (0x94680; R8 is 0 from 0x94560)");
    Check(nNextDisplayId == 1 && nNextSiteId == 1 && nMissionId == 1, "the three id counters = 1");
    CheckEqI32(nCreateNextSiteId, 2, "Base_CreateNamedEntry consumed its own site id");
    CheckEqI32(nNextCraftId, 3, "craft id counter after three spawns");
    Check(nAlienPoolAddend == 0, "alien pool addend");
    {
        bool bAll = true;
        for (i = 0; i < 10; ++i) {
            if (aRegionBuildCount[i] != 1) {
                bAll = false;
            }
        }
        for (i = 0; i < PUFO_CRAFT_TYPE_COUNT; ++i) {
            if (abTypeCounters[i] != 0) {
                bAll = false;
            }
        }
        Check(bAll, "ten save flags 1 and the five StartNew type counters 0");
    }
    /* ShipCtorEnv keeps its own copy of the type counters (the original shares one global
     * block); the shell is expected to move them back. Recorded deviation. */
    CheckEqI32(ctorEnv.abTypeCounters[CRAFT_TYPE_SKYRANGER], 1, "ctor counter for the Skyranger");
    CheckEqI32(ctorEnv.abTypeCounters[CRAFT_TYPE_INTERCEPTOR], 2, "ctor counter for the two interceptors");

    /* The economy block. */
    Check(economy.anFundBar[0] == 1625000 && economy.anFundBar[1] == 383000 &&
          economy.anFundBar[3] == 1668000 && economy.anFundBar[4] == 968000,
          "the four round funding bars");
    CheckEqI32(economy.anFundBar[2], PUFO_FUND_BAR2_INIT,
               "the third bar holds 0x14B4C8 = 1357832, the value 0x946A4 stores (0x9469C)");
    {
        bool bAll = true;
        for (i = 0; i < 11; ++i) {
            if (economy.anFundingBlock[i] != 0) {
                bAll = false;
            }
        }
        for (i = 0; i < 6; ++i) {
            if (economy.aZero44_49[i] != 0) {
                bAll = false;
            }
        }
        Check(bAll, "the zeroed economy dwords");
    }

    /* The clock. */
    Check(g_time.nTotalSec == 0 && g_time.bPaused == 0, "GameTime_Reset ran");

    /* The base. */
    Check(apBases[0] != NULL, "the starting base record exists");
    if (apBases[0] != NULL) {
        CheckEqI32(apBases[0]->nUnk8, -6000, "base x");
        CheckEqI32(apBases[0]->nUnk12, -6000, "base y");
        CheckEqI32(apBases[0]->nStoresTotal, 66, "base stock total from six raw grants");
    }
    Check(strcmp(aSiteRecs[0].abName, "new") == 0, "the base site record is named \"new\"");

    /* The raw BL argument table: 0x94EFC, 0x94F14, 0x94F2C, 0x94F44,
     * 0x95188, and 0x951A0. Check every object slot and every base owner column. */
    {
        bool bAll = true;
        for (i = 0; i < (i32)g_assets.nObjectCount; ++i) {
            ObjectRecFull *pObj = g_assets.apObjects[i];
            if (pObj->auPerBaseVal[0] !=
                (u16)RawExpectedQty(g_aRawExpectedBase0,
                                    (i32)(sizeof(g_aRawExpectedBase0) /
                                          sizeof(g_aRawExpectedBase0[0])), i)) {
                bAll = false;
            }
            for (c = 1; c < PUFO_FAC_BASE_COUNT; ++c) {
                if (pObj->auPerBaseVal[c] != 0) {
                    bAll = false;
                }
            }
        }
        Check(bAll, "Base0 has exactly the six raw object-slot grants; other stock slots and base columns are zero");
    }
    CheckEqI32(g_assets.apObjects[4]->auPerBaseVal[0], 0, "no starting rifle stock");
    CheckEqI32(g_assets.apObjects[17]->auPerBaseVal[0], 0, "no starting grenade stock");
    CheckEqI32(g_assets.apObjects[38]->auPerBaseVal[0], 0, "no starting rifle-clip stock");

    /* The 17 facility records, named from the shipped text records. */
    /* The names are copied from the text records the table order selects. Which language
     * they are in depends on the archive the boot loaded (this run has lang.dat's Russian
     * TEXTS), so the check is against the loaded record itself, not a hardcoded string. */
    {
        const u8 *pText = g_assets.pTextRecs;
        bool bAll = true;
        bAll = bAll && strcmp(aFac[FAC_ACCESS_LIFT].szName,
                              (const char *)(pText + PUFO_TEXT_REC_STRIDE * 106)) == 0;
        bAll = bAll && strcmp(aFac[FAC_HANGAR].szName,
                              (const char *)(pText + PUFO_TEXT_REC_STRIDE * 117)) == 0;
        bAll = bAll && strcmp(aFac[FAC_MISSILE_DEFENCES].szName,
                              (const char *)(pText + PUFO_TEXT_REC_STRIDE * 463)) == 0;
        bAll = bAll && strcmp(aFac[FAC_FUSION_BALL_DEFENCE].szName,
                              (const char *)(pText + PUFO_TEXT_REC_STRIDE * 122)) == 0;
        bAll = bAll && aFac[FAC_HANGAR].szName[0] != '\0';
        Check(bAll, "facility names copied from text records 106 / 117 / 463 / 122");
    }
    {
        static const u8 abWantFlag20[PUFO_BASEFAC_COUNT] = {
            1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0
        };
        static const u32 auWantCost[PUFO_BASEFAC_COUNT] = {
            300000, 400000, 750000, 800000, 500000, 800000, 150000, 500000, 750000, 2000000,
            1300000, 200000, 2300000, 300000, 900000, 1200000, 1800000
        };
        bool bAll = true;
        for (i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
            if (aFac[i].bFlag20 != abWantFlag20[i] || aFac[i].bFlag21 != (u8)i ||
                aFac[i].uCost != auWantCost[i]) {
                bAll = false;
            }
        }
        Check(bAll, "bFlag20 / bFlag21 / uCost for all 17 records");
    }

    /* The grid: the ten placed anchors, the hangar's 2x2 stamp, the empty sentinel. */
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 0, 2), FAC_HANGAR, "hangar anchor (2,0)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 2, 2), FAC_ACCESS_LIFT, "access lift (2,2)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 2, 3), FAC_LIVING_QUARTERS, "living quarters (3,2)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 3, 1), FAC_SHORT_RANGE_RADAR, "short range radar (1,3)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 3, 2), FAC_GENERAL_STORES, "general stores (2,3)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 3, 3), FAC_WORKSHOP, "workshop (3,3)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 3, 4), FAC_LABORATORY, "laboratory (4,3)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 3, 5), FAC_GENERAL_STORES, "general stores (5,3)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 4, 0), FAC_HANGAR, "hangar anchor (0,4)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 4, 4), FAC_HANGAR, "hangar anchor (4,4)");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 0, 0), PUFO_FAC_TYPE_NONE, "an unplaced cell keeps 0xFF");
    CheckEqI32(FACCELL(PUFO_GRID_TILEA_OFF, 0, 2), 9, "hangar stamp tileA (2,0)");
    CheckEqI32(FACCELL(PUFO_GRID_TILEA_OFF, 0, 3), 10, "hangar stamp tileA (3,0)");
    CheckEqI32(FACCELL(PUFO_GRID_TILEA_OFF, 1, 2), 11, "hangar stamp tileA (2,1)");
    CheckEqI32(FACCELL(PUFO_GRID_TILEA_OFF, 1, 3), 12, "hangar stamp tileA (3,1)");
    /* StartNew places with bClear 1, and BaseFac_SetCellBuildState writes the spec's build
     * days as 0 in that mode: the starter facilities arrive already built. */
    CheckEqI32(DAYS(0, 2), 0, "hangar anchor has zero build days (bClear 1)");
    CheckEqI32(DAYS(0, 3), 0, "hangar stamp (3,0) zero days");
    CheckEqI32(DAYS(1, 3), 0, "hangar stamp (3,1) zero days");
    CheckEqI32(DAYS(1, 2), 0, "the hangar's fourth cell keeps zero days");
    CheckEqI32(FACCELL(PUFO_GRID_TILEA_OFF, 2, 2), 0x02, "access lift tileA");
    CheckEqI32(FACCELL(PUFO_GRID_TILEB_OFF, 2, 2), 0x11, "access lift tileB");
    CheckEqI32(DAYS(2, 2), 0, "access lift build days");
    CheckEqI32(FACCELL(PUFO_GRID_FAC_TYPE_OFF, 5, 5), PUFO_FAC_TYPE_NONE, "base 0 layer 3 is otherwise 0xFF");
    {
        bool bAll = true;
        for (i = 1; i < PUFO_FAC_BASE_COUNT; ++i) {
            for (c = 0; c < PUFO_GRID_CELL_COUNT; ++c) {
                if (grid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * i +
                        PUFO_GRID_FAC_TYPE_OFF + c] != PUFO_FAC_TYPE_NONE ||
                    grid[PUFO_GRID_DAYS_STRIDE * i + c] != 0) {
                    bAll = false;
                }
            }
        }
        Check(bAll, "bases 1..7 reset to facType 0xFF and zero build days");
    }

    /* The rosters. */
    {
        bool bAll = true;
        for (i = 0; i < PUFO_STARTNEW_SOLDIER_COUNT; ++i) {
            Soldier *pSol = apSoldiers[i];
            if (pSol == NULL || pSol->nUnitType != 0 || pSol->bUnk248 != 0 ||
                pSol->nKind != 0 || pSol->bUnk183 != 0 || pSol->uMissions != 0 ||
                pSol->uKills != 0 || pSol->bPsiActive != 0 || pSol->bRankType != 0 ||
                pSol->bUnk247 != 0) {
                bAll = false;
            }
        }
        Check(bAll, "eight soldiers pinned to type 0 with a zero history");
    }

    /* The three craft. */
    Check(apCraft[0] != NULL && apCraft[1] != NULL && apCraft[2] != NULL, "three craft records exist");
    if (apCraft[0] != NULL) {
        CheckEqI32(apCraft[0]->bCraftType, CRAFT_TYPE_SKYRANGER, "craft 0 type");
        CheckEqI32(apCraft[0]->uRangeMax, 10000, "craft 0 range max");
        CheckEqI32(apCraft[0]->uRangeRemain, apCraft[0]->uRangeMax, "craft 0 range filled");
        CheckEqI32(apCraft[0]->bCrewSlots, 0, "craft 0 crew slots");
    }
    if (apCraft[1] != NULL && apCraft[2] != NULL) {
        CheckEqI32(apCraft[1]->bCraftType, CRAFT_TYPE_INTERCEPTOR, "craft 1 type");
        CheckEqI32(apCraft[1]->uRangeMax, 1000, "craft 1 range max");
        CheckEqI32(apCraft[1]->uRangeRemain, 1000, "craft 1 range filled");
        CheckEqI32(apCraft[1]->nFuel, (i16)apCraft[1]->uFuelMax, "craft 1 fuel full");
        CheckEqI32(apCraft[1]->bWeaponL, 25, "craft 1 weapon L");
        CheckEqI32(apCraft[1]->bWeaponR, 26, "craft 1 weapon R");
        CheckEqI32(apCraft[1]->bAmmo25, 10, "craft 1 ammo 25");
        CheckEqI32(apCraft[1]->bAmmo26, 3, "craft 1 ammo 26");
        CheckEqI32(apCraft[2]->bCraftType, CRAFT_TYPE_INTERCEPTOR, "craft 2 type");
        CheckEqI32(apCraft[2]->nFuel, (i16)apCraft[2]->uFuelMax, "craft 2 fuel full");
        CheckEqI32(apCraft[2]->bAmmo25, 10, "craft 2 ammo 25");
        CheckEqI32(apCraft[2]->bAmmo26, 3, "craft 2 ammo 26");
    }

    /* Independent raw cargo expectations: twenty slots for craft0, then 25/26 once
     * for each interceptor. Compare all three actual owner columns over every object
     * slot, so stale alien cargo or missing standard kit cannot hide in an unlisted row. */
    {
        bool bAll = true;
        for (i = 0; i < (i32)g_assets.nObjectCount; ++i) {
            ObjectRecFull *pObj = g_assets.apObjects[i];
            const i32 nWantCraft0 = RawExpectedQty(
                g_aRawExpectedCraft0,
                (i32)(sizeof(g_aRawExpectedCraft0) / sizeof(g_aRawExpectedCraft0[0])), i);
            const i32 nWantFighter = RawExpectedQty(
                g_aRawExpectedFighter,
                (i32)(sizeof(g_aRawExpectedFighter) / sizeof(g_aRawExpectedFighter[0])), i);

            if (pObj->aItemQty[0] != nWantCraft0 ||
                pObj->aItemQty[1] != nWantFighter ||
                pObj->aItemQty[2] != nWantFighter) {
                bAll = false;
            }
            for (c = 3; c < 50; ++c) {
                if (pObj->aItemQty[c] != 0) {
                    bAll = false;
                }
            }
            if (pObj->aItemQty[50] != 0x5A5A || pObj->aItemQty[51] != 0x5A5A) {
                bAll = false; /* StartNew clears [0..49] only */
            }
        }
        Check(bAll, "all craft owner columns match raw grants; no stray cargo; [3..49] zero and [50]/[51] kept");
    }
    CheckEqI32(g_assets.apObjects[4]->aItemQty[0], 6, "Skyranger raw cargo: Rifle slot 4 x6");
    CheckEqI32(g_assets.apObjects[17]->aItemQty[0], 4, "Skyranger raw cargo: Grenade slot 17 x4");
    CheckEqI32(g_assets.apObjects[38]->aItemQty[0], 10, "Skyranger raw cargo: Rifle clip slot 38 x10");
    CheckEqI32(g_assets.apObjects[25]->aItemQty[1], 1, "interceptor 1 raw cargo: slot 25 x1");
    CheckEqI32(g_assets.apObjects[26]->aItemQty[1], 1, "interceptor 1 raw cargo: slot 26 x1");
    CheckEqI32(g_assets.apObjects[25]->aItemQty[2], 1, "interceptor 2 raw cargo: slot 25 x1");
    CheckEqI32(g_assets.apObjects[26]->aItemQty[2], 1, "interceptor 2 raw cargo: slot 26 x1");
    CheckEqI32(g_assets.apObjects[67]->aItemQty[0], 0, "no stray Celatid corpse cargo");
    CheckEqI32(g_assets.apObjects[99]->aItemQty[0], 0, "no stray Cyberdisk cargo");
    CheckEqI32(g_assets.apObjects[108]->aItemQty[0], 0, "no stray Alien surgery cargo");
    CheckEqI32(g_assets.apObjects[4]->bCargoSize, 3, "OBJECTS.DAT Rifle slot 4 cargo size");
    CheckEqI32(g_assets.apObjects[17]->bCargoSize, 1, "OBJECTS.DAT Grenade slot 17 cargo size");
    CheckEqI32(g_assets.apObjects[38]->bCargoSize, 1, "OBJECTS.DAT Rifle clip slot 38 cargo size");
    Check(strcmp(g_assets.apObjects[4]->szName, "Rifle ") == 0,
          "Map.apObjects slot 4 keeps the raw main.dat name including its trailing blank");
    Check(strcmp(g_assets.apObjects[17]->szName, "Grenade") == 0,
          "Map.apObjects slot 17 is the main.dat Grenade record");
    Check(strcmp(g_assets.apObjects[38]->szName, "Rifle clip") == 0,
          "Map.apObjects slot 38 is the main.dat Rifle clip record");

    /* The columns. */
    {
        bool bAll = true;
        for (i = 0; i < (i32)sizeof(researchCol) / (i32)sizeof(researchCol[0]); ++i) {
            if (researchCol[i] != 0) {
                bAll = false;
            }
        }
        Check(bAll, "the eight research columns cleared");
    }
    {
        /* Game_StartNew clears entry [i] of all four product-block columns for every base:
         * 0x947F8 committed, 0x947F0 engineers (the +0x1100 term), 0x94808 accumulator,
         * 0x94800 ordered units (the +0x1900 term). The port's [i + 2176] / [i + 1600] stores
         * faked the two neighbour columns inside one array and left the real columns alone. */
        const i32 nObjects = (i32)g_assets.nObjectCount;
        bool bAll = true;
        for (i = 0; i < PUFO_FAC_BASE_COUNT; ++i) {
            const i32 nFirst = PUFO_PRODUCT_ENTRIES_PER_BASE * i;
            if (prodU16[nFirst] != 0 || prodU16[nFirst + nObjects - 1] != 0 ||
                prodU32[nFirst] != 0 || prodU32[nFirst + nObjects - 1] != 0 ||
                prodReq[nFirst] != 0 || prodReq[nFirst + nObjects - 1] != 0 ||
                manuf[nFirst] != 0 || manuf[nFirst + nObjects - 1] != 0) {
                bAll = false;
            }
        }
        Check(bAll, "all four columns cleared for every base, first and last cleared entry");
        Check(prodU16[nObjects] == 0xAAAA && prodU32[nObjects] == 0xAAAAAAAAu &&
              (u32)prodReq[nObjects] == 0xAAAAAAAAu && manuf[nObjects] == 0xBBBB,
              "each column stops at the object count");
        Check(prodU16[1599] == 0xAAAA && prodU32[1599] == 0xAAAAAAAAu &&
              (u32)prodReq[1599] == 0xAAAAAAAAu && manuf[1599] == 0xBBBB,
              "and the tail of base 7 keeps its value");
    }
    {
        bool bAll = true;
        for (i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
            if (resActive[i] != aTopics[i].bActive || resZero[i] != 0) {
                bAll = false;
            }
        }
        Check(bAll, "the research mirror (active from the topics, zero array cleared)");
    }

    /* The sites. */
    Check((nSiteCount & 0xFF) == 0, "the site count's low byte cleared");
    {
        bool bAll = true;
        for (i = 0; i < PUFO_SMAP_SITE_COUNT; ++i) {
            if (apSites[i].bFlag0 != 0 || apSites[i].bFlag1 != 0 || apSites[i].bUnk518 != 0 ||
                apSites[i].bRecoveredSoldierAssign != 0 || apSites[i].uRecoveredSoldiers != 0 ||
                apSites[i].bRecoveredCraftCount != 0) {
                bAll = false;
            }
            for (c = 0; c < (i32)g_assets.nObjectCount; ++c) {
                if (apSites[i].auItemQty[c] != 0) {
                    bAll = false;
                }
            }
            if (apSites[i].auItemQty[255] != 0xAAAA) {
                bAll = false; /* only [0..nObjectCount-1] is cleared */
            }
        }
        Check(bAll, "all 200 site records cleared over the object range");
    }

    /* The radar byte pass runs over the base count as it stands (0 here), so it writes nothing. */
    CheckEqI32(radarPct[0], (i32)0xAAAAAAAA, "the radar pass skipped (base count was 0 at 0x94600)");

    /* The UI tail. */
    CheckEqI32(screen.nGeoscapeScreen, 2, "screen id ends at 2");
    CheckEqI32(ctlResearchPanel.x, 1000, "research panel parked at x=1000");
    CheckEqI32(ctlResearchPanel.y, 1000, "research panel parked at y=1000");
    Check(ctlConfirm.pfnClick == NoopClick, "the confirm control got the TryConfirm thunk");
    g_nConfirmSeamFired = -1;
    if (ctlConfirm.pfnClick != NULL) {
        ctlConfirm.pfnClick();
    }
    CheckEqI32(g_nConfirmSeamFired, 1, "and calling the stored pointer runs that thunk");
    Check(bFlag187080 == 0, "byte_187080 cleared");
    CheckEqI32(view.nMode, 0, "zoom 2 -> 0 through two Smap_ZoomOut steps");
    Check(worldSize.uW == 111 && worldSize.uH == 222, "the world size published by the last zoom step");
    {
        bool bAll = true;
        for (i = 0; i < PUFO_NAV_CTLS; ++i) {
            if (nav.apNav[i] == NULL || nav.apNav[i]->pfnClick != navEnv.apfnNavClick[i]) {
                bAll = false;
            }
        }
        for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
            if (nav.apPanel[i] == NULL || nav.apPanel[i]->pfnClick != navEnv.apfnPanelClick[i]) {
                bAll = false;
            }
        }
        for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
            if (nav.apFind[i] == NULL || nav.apFind[i]->pfnClick != navEnv.apfnFindClick[i]) {
                bAll = false;
            }
        }
        Check(bAll, "fifteen nav controls built and click-wired");
    }
    if (nav.apNav[0] != NULL) {
        CheckEqI32(nav.apNav[0]->x, 1, "nav 0 x");
        CheckEqI32(nav.apNav[0]->y, 229, "nav 0 y");
    }
    if (nav.apNav[1] != NULL) {
        CheckEqI32(nav.apNav[1]->x, 1, "nav 1 x (the left column runs 0..4)");
    }
    if (nav.apNav[5] != NULL) {
        CheckEqI32(nav.apNav[5]->x, 120, "nav 5 x (the right column starts at 5)");
        CheckEqI32(nav.apNav[5]->y, 229, "nav 5 y");
    }

    /* The strateg list carries the base and the three craft; the spawn pass ran its hooks. */
    Check(strategList.nCount >= 4, "the strateg list holds the base and the three craft");
    Check(g_nEventCalls >= 1, "the event hook ran inside MaybeSpawnCraft_Process");
    Check(nRet >= 0, "MaybeSpawnCraft_Process returned a non-negative result");
}

int main(void)
{
    const char *const apsz[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    i32 i;

    apSites = (SmapSite *)malloc(PUFO_SMAP_SITE_COUNT * sizeof(SmapSite));
    if (apSites == NULL) {
        printf("FAIL: site allocation\n");
        return 1;
    }
    memset(apSites, 0, PUFO_SMAP_SITE_COUNT * sizeof(SmapSite));

    if (!Boot_LoadStartup(&g_assets, apsz)) {
        printf("FAIL: Boot_LoadStartup\n");
        return 1;
    }

    TestStartNew();

    for (i = 0; i < PUFO_STARTNEW_SOLDIER_COUNT; ++i) {
        free(apSoldiers[i]);
    }
    for (i = 0; i < 3; ++i) {
        free(apCraft[i]);
    }
    for (i = 0; i < 8; ++i) {
        free(apBases[i]);
    }
    PtrArray_DeleteAll(&strategList);
    free(apSites);
    Boot_Shutdown(&g_assets);

    printf("%s\n", g_nFailures == 0 ? "all startnew checks passed" : "FAILURES");
    return g_nFailures == 0 ? 0 : 1;
}
