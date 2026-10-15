/* test_geoscape.c - Geoscape_TickShips @ 0x5d4e4, Geoscape_TickHourly @
 * 0x5d1bc, Geoscape_TickBases @ 0x5d3bc and the AdvanceTime wiring.
 *
 * The tick order is the contract: minute -> range recovery, even-minute
 * detect, motion, spawn/process, counter, every-5 intercept sweep, contact
 * messages; hour -> mission prune, refuel+consume, recovery/manuf/research,
 * fallback spawn, empty-cell refresh; day -> construction, grid rows into the
 * site-state table, soldier timers.
 */
#include "game/pufo_geoscape.h"
#include "game/pufo_boot.h"  /* PUFO_TEXT_REC_STRIDE, for the cost label fixture */
#include "game/pufo_save.h"  /* SmapSite, for the monthly upkeep fixture */
#include "game/pufo_ship.h"
#include "game/pufo_unit.h"
#include "util/pufo_ptrarray.h"
#include <stdlib.h>
#include "game/pufo_strateg.h"
#include "game/pufo_clock.h"
#include "render/pufo_render.h" /* g_aPalette, for the radar palette cycle */
#include "game/pufo_font.h"     /* g_fontSys, Font_DrawGlyph: the mount icons are real sprites */
#include "asset/pufo_asset.h"   /* Asset_OpenArchives, the radar session runs on real pads */
#include "ui/pufo_linelist.h"
#include "util/pufo_rand.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_nFailures;

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static void CheckEqU32(u32 uGot, u32 uWant, const char *pszWhat)
{
    if (uGot == uWant) {
        printf("  ok   %s (%08X)\n", pszWhat, uGot);
    } else {
        printf("  FAIL %s: got %08X, want %08X\n", pszWhat, uGot, uWant);
        g_nFailures += 1;
    }
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

/* --- fixtures --- */

static StrategItem g_aItems[4];
static StrategItem *g_apItems[4];
static Ship g_aShips[2];
static Ship *g_apShips[2];
static u8 g_aSiteState[2];
static u8 g_aRadarPct[2];
static BaseRec g_aBases[2];
static BaseRec *g_apBases[2] = { &g_aBases[0], &g_aBases[1] };
static ObjectRecFull g_aObj105;
static Soldier g_aSoldiers[2];
static Soldier *g_apSoldiers[2] = { &g_aSoldiers[0], &g_aSoldiers[1] };
static i32 g_aEmptyCells[2];
static StrategList g_List;

static int g_nProcess, g_nProcessSpawn, g_nMsg;
static int g_nMsgId, g_nMsgArg3;
static int g_nSpawnSite, g_nGrant, g_nManuf, g_nResearch, g_nConstruct;
static int g_nClearRow, g_nEmptyCells;
static int g_nMonthly;
static char g_szOrder[64];

static void Order(char c)
{
    size_t n = strlen(g_szOrder);
    if (n < sizeof(g_szOrder) - 1) {
        g_szOrder[n] = c;
        g_szOrder[n + 1] = 0;
    }
}

static void Hook_ProcessEvents(u8 bSpawn, void *pUser)
{
    (void)pUser;
    ++g_nProcess;
    if (bSpawn) ++g_nProcessSpawn;
    Order('P');
}

static void Hook_Msg(i32 nMsg, i32 nArg3, i32 nArg4, void *pUser)
{
    (void)nArg4; (void)pUser;
    ++g_nMsg;
    g_nMsgId = nMsg;
    g_nMsgArg3 = nArg3;
}

static void Hook_SpawnSite(StrategItem *pUfo, i32 nSub, void *pUser)
{
    (void)pUfo; (void)nSub; (void)pUser;
    ++g_nSpawnSite;
}

static void Hook_Grant(void *pUser)  { (void)pUser; ++g_nGrant; Order('G'); }
static void Hook_Manuf(void *pUser)  { (void)pUser; ++g_nManuf; Order('M'); }
static void Hook_Research(void *pUser){ (void)pUser; ++g_nResearch; Order('R'); }
static void Hook_Construct(void *pUser){ (void)pUser; ++g_nConstruct; Order('C'); }

static i32 Hook_ClearRow(i32 nBaseIdx, void *pUser)
{
    (void)pUser;
    ++g_nClearRow;
    return 1 + nBaseIdx; /* site state 1 for base 0, 2 for base 1 */
}

static i32 Hook_EmptyCells(i32 nBaseIdx, void *pUser)
{
    (void)pUser;
    ++g_nEmptyCells;
    return 7 + nBaseIdx;
}

static void Hook_Monthly(void *pUser) { (void)pUser; ++g_nMonthly; }

static i32 Hook_FindBase(i32 nSiteId, void *pUser)
{
    (void)nSiteId; (void)pUser;
    return 0;
}

static void Setup(GeoscapeState *pState, GeoscapeHooks *pHooks)
{
    for (i32 i = 0; i < 4; ++i) {
        memset(&g_aItems[i], 0, sizeof(g_aItems[i]));
        g_aItems[i].nSiteId = STRATEG_ID_UNASSIGNED;
        g_aItems[i].nDisplayId = STRATEG_ID_UNASSIGNED;
        g_apItems[i] = &g_aItems[i];
    }
    for (i32 i = 0; i < 2; ++i) {
        memset(&g_aShips[i], 0, sizeof(g_aShips[i]));
        g_apShips[i] = &g_aShips[i];
        memset(&g_aBases[i], 0, sizeof(g_aBases[i]));
        memset(&g_aSoldiers[i], 0, sizeof(g_aSoldiers[i]));
    }
    memset(&g_aObj105, 0, sizeof(g_aObj105));
    memset(g_aSiteState, 0, sizeof(g_aSiteState));
    memset(g_aRadarPct, 0, sizeof(g_aRadarPct));

    GameTime_Reset(&pState->time);
    pState->world.apCraft = g_apShips;
    pState->world.nShipCount = 1;
    pState->world.apSiteState = g_aSiteState;
    pState->world.apRadarPct = g_aRadarPct;
    pState->world.nTickSeconds = 60;
    pState->world.nRadarBoostOff = 1;
    pState->world.nMinuteTicks = 0;
    g_List.apItems = g_apItems;
    g_List.nCount = 0; /* each test sets the entries it wants live */
    g_List.nNextDisplayId = 0;
    g_List.nNextSiteId = 0;
    pState->pList = &g_List;
    pState->world.pTime = &pState->time;
    pState->nDaysSinceStart = 0;
    pState->nSelectedShipIdx = -1;
    pState->nSelectedShipAux = -1;
    pState->bLowSupplyFlag = 0;
    pState->apBases = g_apBases;
    pState->pSupplyItem = &g_aObj105;
    pState->nBaseCount = 2;
    pState->apEmptyFacilityCells = g_aEmptyCells;
    pState->apSoldiers = g_apSoldiers;
    pState->nSoldierCount = 2;

    pHooks->pfnProcessEvents = Hook_ProcessEvents;
    pHooks->pfnContactMsg = Hook_Msg;
    pHooks->pfnSpawnMissionSite = Hook_SpawnSite;
    pHooks->pfnGrantSiteRecovery = Hook_Grant;
    pHooks->pfnRefreshBaseTabs = Hook_Manuf;
    pHooks->pfnAccumulateResearch = Hook_Research;
    pHooks->pfnTickFacilityConstruction = Hook_Construct;
    pHooks->pfnClearFacilityGridRow = Hook_ClearRow;
    pHooks->pfnCountEmptyFacilityCells = Hook_EmptyCells;
    pHooks->pfnOpenMonthly = Hook_Monthly;
    pHooks->pfnFindBaseBySiteId = Hook_FindBase;
    pHooks->pUser = 0;

    g_nProcess = g_nProcessSpawn = g_nMsg = 0;
    g_nSpawnSite = g_nGrant = g_nManuf = g_nResearch = g_nConstruct = 0;
    g_nClearRow = g_nEmptyCells = g_nMonthly = 0;
    g_szOrder[0] = 0;
}

static void TestTickShips(void)
{
    printf("Geoscape_TickShips\n");
    GeoscapeState st;
    GeoscapeHooks hooks;
    Setup(&st, &hooks);

    /* Ship 0: away from base and out of range -> the contact path. Ship 1: at base -> the
     * range-recovery path. Ship_TickRangeRecover only recovers while bAtBase, so one ship cannot
     * show both.
     *
     * The range gate needs the craft's own strateg item (nType 1, bActive 0, nDisplayId == bId)
     * and its home base. Craft class 3 divides uRangeMax by 12000, so the factor is 1/12: the
     * burn is (3000 / 60) / 12 = 4.1667, the 10 remaining drops to 6, and 300 map units from base
     * 0 scales to 300 * 50 / 12 = 1250, which is far above 6. */
    st.world.nShipCount = 2;
    g_aShips[0].bAtBase = 0;
    g_aShips[0].bId = 7;
    g_aShips[0].bCraftType = 3;
    g_aShips[0].bHomeBase = 0;
    g_aShips[0].uFuelCap = 3000;
    g_aShips[0].uRangeRemain = 10;
    g_aShips[0].uRangeMax = 1000;
    g_aShips[0].uRangeRate = 5;
    g_aShips[1].bAtBase = 1;
    g_aShips[1].uRangeRemain = 10;
    g_aShips[1].uRangeMax = 1000;
    g_aShips[1].uRangeRate = 5;
    g_aBases[0].nUnk8 = 0;
    g_aBases[0].nUnk12 = 0;
    g_List.nCount = 1;
    g_aItems[0].nType = 1;
    g_aItems[0].bActive = 0;
    g_aItems[0].nDisplayId = 7;
    g_aItems[0].nX = 300;
    g_aItems[0].nY = 0;

    Geoscape_TickShips(&st, &hooks);
    CheckEqI32(g_aShips[1].uRangeRemain, 15, "range recovery ran first");
    CheckEqI32(g_aShips[0].uRangeRemain, 6, "the gate burned 4 of the 10 remaining");
    CheckEqI32(g_nProcess, 1, "process events ran");
    CheckEqI32(st.world.nMinuteTicks, 1, "minute counter advanced");
    CheckEqI32(g_nMsgId, 15, "contact message 15 posted");
    CheckEqI32(g_nMsgArg3, 0, "contact message carries the ship index");
    CheckEqI32(g_aShips[0].bContactMsg, 1, "contact flag set");
    CheckEqI32(st.nSelectedShipIdx, 0, "selected ship index set");
    CheckEqI32(st.nSelectedShipAux, 0, "aux slot mirrors it");
    CheckEqI32(st.time.bPaused, 1, "contact 15 pauses the clock");
    st.time.bPaused = 0;

    /* Every fifth counter value sweeps expired intercepts. */
    g_List.nCount = 1;
    g_aItems[0].nType = 1; g_aItems[0].bActive = 1;
    g_aItems[0].bVisible = 0; g_aItems[0].nDisplayId = 99;
    g_aShips[0].bAtBase = 1; /* no more contact messages */
    g_aShips[0].bIntercepting = 1;
    g_aShips[0].nStrategIdx = 0;
    g_nMsg = 0;
    for (int i = 1; i < 5; ++i)
        Geoscape_TickShips(&st, &hooks);
    CheckEqI32(st.world.nMinuteTicks, 5, "counter reached 5");
    CheckEqI32(g_aShips[0].bIntercepting, 0, "expired intercept swept at 5");
    CheckEqI32(g_nMsgId, 13, "sweep posted message 13");
    CheckEqI32(st.time.bPaused, 1, "the sweep pauses the clock");
    st.time.bPaused = 0;

    /* Paused: no process events, no counter, no contact block. */
    st.time.bPaused = 1;
    g_nProcess = 0;
    Geoscape_TickShips(&st, &hooks);
    CheckEqI32(g_nProcess, 0, "paused skips the spawn/contact block");
    CheckEqI32(st.world.nMinuteTicks, 5, "counter untouched while paused");
    st.time.bPaused = 0;
}

static void TestTickHourly(void)
{
    printf("Geoscape_TickHourly\n");
    GeoscapeState st;
    GeoscapeHooks hooks;
    Setup(&st, &hooks);

    /* Two live contacts keeps StrategList_CountActive at 2, so the fallback
     * roll is skipped and the first two ticks stay deterministic (no rand). */
    g_List.nCount = 2;
    g_aItems[0].nType = 1; g_aItems[0].bActive = 1;
    g_aItems[1].nType = 1; g_aItems[1].bActive = 1;

    /* A mission far outside the map is teleported and re-seeded. */
    g_aItems[0].nX = 1500; g_aItems[0].nY = 0;
    Geoscape_TickHourly(&st, &hooks);
    CheckEqI32(g_aItems[0].nX, 150, "far mission reset to x=150");
    CheckEqI32(g_aItems[0].nY, 250, "far mission reset to y=250");
    CheckEqI32(g_nSpawnSite, 1, "re-seeded through SpawnMissionSite");

    /* Refuel ran for the ship at base. */
    g_aShips[0].bAtBase = 1;
    g_aShips[0].uFuelMax = 100;
    g_aShips[0].nFuel = 10;
    g_aShips[0].bCraftType = 1;
    Geoscape_TickHourly(&st, &hooks);
    CheckEqI32(g_aShips[0].nFuel, 12, "refuel +2 per hour tick");

    /* Hook order: grant, manuf, research. */
    Check(strcmp(g_szOrder, "GMRGMR") == 0,
          "recovery, manufacturing and research run in order");
    CheckEqI32(g_nGrant, 2, "grant ran once per hourly tick");
    CheckEqI32(g_nManuf, 2, "manuf tabs refreshed once per hourly tick");
    CheckEqI32(g_nResearch, 2, "research accumulated once per hourly tick");

    /* Empty-cell refresh fills one entry per base. */
    CheckEqI32(g_aEmptyCells[0], 7, "empty cells for base 0");
    CheckEqI32(g_aEmptyCells[1], 8, "empty cells for base 1");

    /* Fallback spawn: with fewer than 2 active contacts and a rand roll of
     * exactly 1, ProcessEvents is called with spawn=1. Find a seed whose
     * first Game_RandU16(4) is 1, then replay it. */
    g_aItems[0].bActive = 0; g_aItems[1].bActive = 0; /* CountActive == 0 */
    int nSeed = -1;
    for (int seed = 0; seed < 40000 && nSeed < 0; ++seed) {
        srand(seed);
        if (Game_RandU16(4u) == 1u)
            nSeed = seed;
    }
    Check(nSeed >= 0, "found a seed that rolls 1");
    srand(nSeed);
    g_nProcess = g_nProcessSpawn = 0;
    Geoscape_TickHourly(&st, &hooks);
    CheckEqI32(g_nProcessSpawn, 1, "fallback spawn fired when under 2 active");

    /* With 2 active contacts no rand is consumed and no spawn fires. */
    g_aItems[0].bActive = 1;
    g_aItems[1].bActive = 1;
    g_nProcess = g_nProcessSpawn = 0;
    Geoscape_TickHourly(&st, &hooks);
    CheckEqI32(g_nProcess, 0, "two active contacts skip the fallback roll");
}

static void TestTickBases(void)
{
    printf("Geoscape_TickBases\n");
    GeoscapeState st;
    GeoscapeHooks hooks;
    Setup(&st, &hooks);

    Geoscape_TickBases(&st, &hooks);
    CheckEqI32(st.nDaysSinceStart, 1, "day counter advances");
    CheckEqI32(g_nConstruct, 1, "facility construction ticked");
    CheckEqI32(g_nClearRow, 2, "one grid row cleared per base");
    CheckEqI32(g_aSiteState[0], 1, "site state from row result (base 0)");
    CheckEqI32(g_aSiteState[1], 2, "site state from row result (base 1)");

    /* Soldier timers. */
    g_aSoldiers[0].bUnk423 = 5;
    g_aSoldiers[0].auStat[6] = 10;
    g_aSoldiers[0].bUnk247 = 1;
    g_aSoldiers[0].bUnk183 = 254;
    Geoscape_TickBases(&st, &hooks);
    CheckEqI32(g_aSoldiers[0].auStat[6], 11, "bUnk423 > 0 raises auStat[6]");
    CheckEqI32(g_aSoldiers[0].bUnk423, 4, "bUnk423 counts down");
    CheckEqI32(g_aSoldiers[0].bUnk247, 0, "bUnk247 counts down");
    CheckEqI32(g_aSoldiers[0].bUnk183, 255, "bUnk183 254 -> -1 when timer hits 0");

    /* Day counter caps at 200. */
    st.nDaysSinceStart = 200;
    Geoscape_TickBases(&st, &hooks);
    CheckEqI32(st.nDaysSinceStart, 200, "day counter caps at 200");
}

static void TestAdvance(void)
{
    printf("Geoscape_Advance wiring\n");
    GeoscapeState st;
    GeoscapeHooks hooks;
    Setup(&st, &hooks);
    g_aShips[0].bAtBase = 1; /* keep contact messages out of the count */
    g_List.nCount = 2;       /* 2 active contacts -> no fallback roll, no rand */
    g_aItems[0].nType = 1; g_aItems[0].bActive = 1;
    g_aItems[1].nType = 1; g_aItems[1].bActive = 1;

    Geoscape_Advance(&st, &hooks, 3600);
    CheckEqI32(g_nProcess, 60, "one hour advances 60 minute ticks");
    CheckEqI32(g_nGrant, 1, "one hourly tick");
    CheckEqI32(g_nConstruct, 0, "no day tick within an hour");

    Geoscape_Advance(&st, &hooks, 86400 - 3600);
    CheckEqI32(g_nGrant, 24, "24 hourly ticks in a day");
    CheckEqI32(g_nConstruct, 1, "one day tick");

    /* Month boundary fires the monthly hook. */
    Geoscape_Advance(&st, &hooks, 31 * 86400 - 86400);
    CheckEqI32(g_nMonthly, 1, "monthly funding fired at the month change");
}

static void TestPanelFlagsAndQty(void)
{
    GeoscapePanelFlags flags;
    ObjectRecFull obj;
    GameTime time;

    printf("panel flags, per-base quantity, unpause\n");

    memset(&flags, 0, sizeof(flags));
    Check(Geoscape_SetPanelFlagA(0, &flags), "screen 0 arms panel flag A");
    Check(Geoscape_SetPanelFlagB(2, &flags), "screen 2 arms panel flag B");
    Check(!Geoscape_SetPanelFlagA(5, &flags), "any other screen arms nothing");
    CheckEqI32(flags.bPanelA, 1, "A lands at 0x1864F5");
    CheckEqI32(flags.bPanelB, 1, "B lands at 0x1864F7");
    CheckEqI32(flags.bUnk0, 0, "the neighbouring byte is untouched");
    CheckEqI32(flags.bUnk2, 0, "so is the other one");

    memset(&obj, 0, sizeof(obj));
    obj.auPerBaseVal[3] = 42;
    CheckEqI32(Base_GetItemQty(&obj, 3), 42, "per-base quantity lookup");
    CheckEqI32(Base_GetItemQty(&obj, 8), 0, "an index past the eight entries is refused");
    CheckEqI32(Base_GetItemQty(NULL, 0), 0, "NULL object returns 0");

    memset(&time, 0, sizeof(time));
    time.bPaused = 1;
    GameTime_Unpause(&time);
    CheckEqI32(time.bPaused, 0, "GameTime_Unpause clears the pause byte");
    GameTime_Unpause(&time);
    CheckEqI32(time.bPaused, 0, "unpausing while running is a no-op");
}

static void TestBaseGridLookup(void)
{
    static BaseRec aBases[3];
    static BaseRec *apBases[3];
    static u8 aGrid[8];
    static ObjectRecFull obj;

    printf("Base_FindIndexByTypeByte / BaseRec_AddItemQty\n");

    for (i32 i = 0; i < 3; ++i) {
        memset(&aBases[i], 0, sizeof(aBases[i]));
        apBases[i] = &aBases[i];
    }
    aBases[0].bUnk4 = 7;
    aBases[1].bUnk4 = 9;
    aBases[2].bUnk4 = 11;
    memset(aGrid, 0, sizeof(aGrid));

    CheckEqI32(Base_FindIndexByTypeByte(apBases, 3, 7), 0, "the first base matches at index 0");
    CheckEqI32(Base_FindIndexByTypeByte(apBases, 3, 9), 1, "the type byte is BaseRec.bUnk4");
    CheckEqI32(Base_FindIndexByTypeByte(apBases, 3, 5), -1,
               "no match stops at the array end instead of running off it");
    CheckEqI32(Base_FindIndexByTypeByte(apBases, 0, 7), -1, "an empty base list answers -1");

    CheckEqI32(BaseGrid_GetTypeByte(apBases, 3, aGrid, 9), 0, "the grid starts clear");
    CheckEqI32(BaseGrid_SetTypeByte(apBases, 3, aGrid, 9, 42), 1, "the setter returns the index");
    CheckEqI32(aGrid[1], 42, "the write lands at the found index");
    CheckEqI32(BaseGrid_GetTypeByte(apBases, 3, aGrid, 9), 42, "the getter reads it back");
    CheckEqI32(BaseGrid_SetTypeByte(apBases, 3, aGrid, 5, 1), -1, "no match writes nothing");
    CheckEqI32(aGrid[0], 0, "the grid is untouched after a refused write");

    /* BaseGrid_SubByte0x654 @ 0x6acec. */
    CheckEqI32(BaseGrid_SubTypeByte(apBases, 3, aGrid, 9, 12), 1, "the subtract returns the index");
    CheckEqI32(aGrid[1], 30, "42 minus 12");
    CheckEqI32(BaseGrid_SubTypeByte(apBases, 3, aGrid, 9, 40), 1, "and it keeps going");
    CheckEqI32(aGrid[1], -10 & 0xFF,
               "there is no floor, so a positive byte wraps below zero as the original does");

    aGrid[2] = 0;
    CheckEqI32(BaseGrid_SubTypeByte(apBases, 3, aGrid, 11, 5), 2, "base 2 is found by its type");
    CheckEqI32(aGrid[2], 0, "a byte that is already zero is left alone - that is the guard");

    CheckEqI32(BaseGrid_SubTypeByte(apBases, 3, aGrid, 5, 5), -1,
               "no match is refused, where the original reads and writes abGrid[-1]");

    memset(&obj, 0, sizeof(obj));
    aBases[1].bSlot = 2;
    aBases[1].nStoresTotal = 10;
    obj.auPerBaseVal[2] = 5;
    BaseRec_AddItemQty(&aBases[1], &obj, 3);
    CheckEqI32(obj.auPerBaseVal[2], 8, "the per-base slot grows by the delta");
    CheckEqI32(aBases[1].nStoresTotal, 13, "the base total grows by the same delta");
    BaseRec_AddItemQty(&aBases[1], &obj, -20);
    CheckEqI32((i32)(i16)obj.auPerBaseVal[2], -12, "neither add clamps at zero");
    CheckEqI32(aBases[1].nStoresTotal, -7, "the total goes negative too");

    aBases[1].bSlot = 99;
    BaseRec_AddItemQty(&aBases[1], &obj, 5);
    CheckEqI32(aBases[1].nStoresTotal, -7, "an out-of-range slot skips both adds");
}

static void TestFacilityClassify(void)
{
    printf("Base_ClearFacilityGridRow\n");
    static u8 grid[700];
    static u8 site[8];

    /* Occupancy for base 0 is grid[0..35], cell type is grid[480..515]. */
#define CELL_T(c, r) grid[480 + 6 * (c) + (r)]
#define CELL_O(c, r) grid[6 * (c) + (r)]

    memset(grid, 0, sizeof(grid));
    memset(site, 0, sizeof(site));
    CheckEqI32(Base_ClearFacilityGridRow(grid, 0, site), 0,
               "an all-zero layout is state 0");
    CheckEqI32(site[0], 0, "and the site-state byte is written");

    memset(grid, 0, sizeof(grid));
    CELL_T(0, 0) = 4;
    CheckEqI32(Base_ClearFacilityGridRow(grid, 0, site), 1, "a free type-4 slot gives state 1");

    memset(grid, 0, sizeof(grid));
    CELL_T(0, 0) = 4;
    CELL_O(0, 0) = 1;
    CheckEqI32(Base_ClearFacilityGridRow(grid, 0, site), 0,
               "a type-4 slot that is occupied does not count");

    memset(grid, 0, sizeof(grid));
    CELL_T(0, 0) = 4;
    CELL_T(1, 1) = 5;
    CheckEqI32(Base_ClearFacilityGridRow(grid, 0, site), 2,
               "type 5 overwrites the 1, so the two are never both present");

    memset(grid, 0, sizeof(grid));
    CELL_T(2, 3) = 9;
    CheckEqI32(Base_ClearFacilityGridRow(grid, 0, site), 10,
               "a free type-9 slot alone gives 10");

    memset(grid, 0, sizeof(grid));
    CELL_T(0, 0) = 4;
    CELL_T(2, 3) = 9;
    CheckEqI32(Base_ClearFacilityGridRow(grid, 0, site), 11, "type 4 plus a free 9 is 11");

    memset(grid, 0, sizeof(grid));
    CELL_T(0, 0) = 5;
    CELL_T(2, 3) = 9;
    CELL_T(3, 3) = 9;
    CheckEqI32(Base_ClearFacilityGridRow(grid, 0, site), 12,
               "two free 9 slots still add the 10 only once");

    /* A second base reads its own block, not base 0's. */
    memset(grid, 0, sizeof(grid));
    memset(site, 0, sizeof(site));
    grid[480 + 144 + 6 * 0 + 0] = 4;   /* base 1 type grid */
    grid[36 + 6 * 0 + 0] = 1;          /* base 1 occupancy, slot 0 occupied */
    grid[480 + 144 + 6 * 1 + 1] = 4;   /* base 1 type grid, another slot */
    CheckEqI32(Base_ClearFacilityGridRow(grid, 1, site), 1,
               "base 1 is classified from its own 144-byte type block");
    CheckEqI32(site[1], 1, "and lands in its own site-state byte");
    CheckEqI32(site[0], 0, "base 0 is left alone");

#undef CELL_T
#undef CELL_O
}

static void TestFindFacilityCell(void)
{
    static u8 grid[700];

    printf("Base_FindFacilityCell\n");

#define F_T(c, r) grid[480 + 144 * 0 + 6 * (c) + (r)]
#define F_O(c, r) grid[6 * (c) + (r)]

    memset(grid, 0, sizeof(grid));
    F_T(2, 3) = 3;
    CheckEqI32(Base_FindFacilityCell(grid, 0, 3), 1,
               "one free cell of the asked-for kind is a hit");

    memset(grid, 0, sizeof(grid));
    F_T(2, 3) = 3;
    CheckEqI32(Base_FindFacilityCell(grid, 0, 2), 0,
               "a lab request finds nothing when only a workshop is free");

    memset(grid, 0, sizeof(grid));
    F_T(2, 3) = 3;
    F_O(2, 3) = 4;
    CheckEqI32(Base_FindFacilityCell(grid, 0, 3), 0,
               "a cell under construction is not a free cell");

    memset(grid, 0, sizeof(grid));
    F_T(0, 0) = 9;
    CheckEqI32(Base_FindFacilityCell(grid, 0, 9), 1,
               "the last cell in the walk still counts");

    /* The kind is compared as an unsigned byte, so a signed char argument still matches 200. */
    memset(grid, 0, sizeof(grid));
    F_T(5, 5) = 200;
    CheckEqI32(Base_FindFacilityCell(grid, 0, 200), 1,
               "a kind above 127 is compared unsigned, as the original does");

    /* Base 1 reads its own 144-byte type block but shares the buildDays stride rule. */
    memset(grid, 0, sizeof(grid));
    grid[480 + 144 * 1 + 6 * 1 + 1] = 3;
    CheckEqI32(Base_FindFacilityCell(grid, 1, 3), 1,
               "base 1 is looked up in its own type block");
    CheckEqI32(Base_FindFacilityCell(grid, 0, 3), 0,
               "and base 0 does not see it");

    grid[6 * 1 + 1] = 2; /* base 0's buildDays for the same cell is irrelevant here */
    CheckEqI32(Base_FindFacilityCell(grid, 1, 3), 1,
               "base 1's own buildDays byte is still zero, so it still hits");

#undef F_T
#undef F_O
}

static void TestWorkshopCap(void)
{
    printf("Base_CalcWorkshopCap\n");
    static u8 grid[700];

#define W_T(c, r) grid[480 + 6 * (c) + (r)]
#define W_O(c, r) grid[6 * (c) + (r)]

    memset(grid, 0, sizeof(grid));
    CheckEqI32(Base_CalcWorkshopCap(grid, 0), 0, "no workshop cells means no capacity");

    memset(grid, 0, sizeof(grid));
    W_T(0, 0) = 3;
    CheckEqI32(Base_CalcWorkshopCap(grid, 0), 50,
               "one free workshop cell is worth exactly 50");

    memset(grid, 0, sizeof(grid));
    W_T(0, 0) = 3;
    W_O(0, 0) = 1;
    CheckEqI32(Base_CalcWorkshopCap(grid, 0), 0,
               "an occupied workshop cell counts for nothing");

    memset(grid, 0, sizeof(grid));
    W_T(0, 0) = 3;
    W_T(1, 1) = 3;
    W_T(2, 2) = 3;
    W_T(3, 3) = 4; /* not a workshop */
    CheckEqI32(Base_CalcWorkshopCap(grid, 0), 150,
               "three free cells give 150, and type 4 is not a workshop");

    memset(grid, 0, sizeof(grid));
    for (int i = 0; i < 36; ++i) {
        grid[480 + i] = 3;
    }
    CheckEqI32(Base_CalcWorkshopCap(grid, 0), 36 * 50,
               "a full grid of free workshops is 36 * 50");

    /* Base 1 reads its own 144-byte type block and its own occupancy column. */
    memset(grid, 0, sizeof(grid));
    grid[480 + 144 + 0] = 3;   /* base 1, cell 0 free */
    grid[480 + 144 + 6] = 3;   /* base 1, another cell */
    grid[36 + 6] = 7;          /* but its occupancy byte is set */
    CheckEqI32(Base_CalcWorkshopCap(grid, 1), 50,
               "base 1 counts only its own free cells");

#undef W_T
#undef W_O
}

static void TestActiveManuf(void)
{
    printf("Base_CountActiveManuf\n");
    static u16 col[200];

    memset(col, 0, sizeof(col));
    CheckEqI32(Base_CountActiveManuf(col, 0), 0, "a zero entry count never touches the array");

    memset(col, 0, sizeof(col));
    col[0] = 7;
    col[1] = 13;
    col[2] = 20;
    CheckEqI32(Base_CountActiveManuf(col, 2), 20, "only the first nEntries entries count");
    CheckEqI32(Base_CountActiveManuf(col, 3), 40, "all three entries sum to 40");

    memset(col, 0, sizeof(col));
    col[0] = 0xFFFF;
    CheckEqI32(Base_CountActiveManuf(col, 1), 65535,
               "a u16 entry is widened to int before it is added");

    /* The count is read as an unsigned byte, so 256 entries is not reachable. */
    memset(col, 0, sizeof(col));
    for (int i = 0; i < 200; ++i) {
        col[i] = 1;
    }
    CheckEqI32(Base_CountActiveManuf(col, 255), 200,
               "the count is clamped to a byte, so only the 200 real entries add up");

    /* Base 1's column is 200 u16 past base 0's, which is the 400-byte stride. */
    memset(col, 0, sizeof(col));
    col[0] = 5;      /* base 0 */
    col[200 - 1] = 0;
    CheckEqI32(Base_CountActiveManuf(&col[0], 1), 5, "base 0 reads the start of the area");
}

static void TestWorkshopNeeded(void)
{
    printf("Base_UpdateWorkshopNeeded\n");
    static u8 grid[700];

#define N_T(c, r) grid[480 + 6 * (c) + (r)]
#define N_N(c, r) grid[444 + 6 * (c) + (r)]

    /* No active manufacturing means no budget, so every workshop is marked unneeded. */
    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 3;
    N_T(1, 1) = 3;
    CheckEqI32(Base_UpdateWorkshopNeeded(grid, 0, 0), 6, "the return value is always 6");
    CheckEqI32(N_N(0, 0), 0, "a zero total marks the first workshop unneeded");
    CheckEqI32(N_N(1, 1), 0, "and the second one too");

    /* Budget is total / 50 + 1, so any non-zero total buys at least one. */
    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 3;
    N_T(1, 1) = 3;
    Base_UpdateWorkshopNeeded(grid, 0, 1);
    CheckEqI32(N_N(0, 0), 1, "a total of 1 still buys one needed workshop");
    CheckEqI32(N_N(1, 1), 0, "and no more than that");

    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 3;
    N_T(1, 0) = 3;
    N_T(2, 0) = 3;
    Base_UpdateWorkshopNeeded(grid, 0, 50);
    CheckEqI32(N_N(0, 0) + N_N(1, 0) + N_N(2, 0), 2, "a total of 50 buys exactly two");

    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 3;
    N_T(1, 0) = 3;
    N_T(2, 0) = 3;
    Base_UpdateWorkshopNeeded(grid, 0, 100);
    CheckEqI32(N_N(0, 0) + N_N(1, 0) + N_N(2, 0), 3, "a total of 100 buys exactly three");

    /* Visit order is row 0 across all columns first, then row 1, and so on. */
    memset(grid, 0, sizeof(grid));
    N_T(1, 0) = 3; /* visited second overall */
    N_T(0, 1) = 3; /* visited seventh */
    Base_UpdateWorkshopNeeded(grid, 0, 1);
    CheckEqI32(N_N(1, 0), 1, "the earlier row claims the budget first");
    CheckEqI32(N_N(0, 1), 0, "the later row gets nothing");

    /* Cells that are not workshops are never written. */
    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 4;
    N_N(0, 0) = 9;
    Base_UpdateWorkshopNeeded(grid, 0, 500);
    CheckEqI32(N_N(0, 0), 9, "a type-4 cell keeps whatever its needed byte held");

#undef N_T
#undef N_N
}

static void TestLabChain(void)
{
    printf("Base_CalcLabCap / Base_CountActiveResearch / Base_UpdateLabNeeded\n");
    static u8 grid[700];
    static u16 col[200];

#define L_T(c, r) grid[480 + 6 * (c) + (r)]
#define L_N(c, r) grid[444 + 6 * (c) + (r)]

    /* Kind separation: the two capacity functions look at different cell kinds. */
    memset(grid, 0, sizeof(grid));
    L_T(0, 0) = 3; /* workshop */
    L_T(1, 1) = 2; /* lab */
    CheckEqI32(Base_CalcWorkshopCap(grid, 0), 50, "the workshop count sees only kind 3");
    CheckEqI32(Base_CalcLabCap(grid, 0), 50, "the lab count sees only kind 2");

    memset(grid, 0, sizeof(grid));
    L_T(0, 0) = 2;
    L_T(1, 0) = 2;
    L_T(2, 0) = 2;
    grid[6 * 1 + 0] = 4; /* base 0 occupancy for the middle lab */
    CheckEqI32(Base_CalcLabCap(grid, 0), 100,
               "two free labs give 100, an occupied one gives nothing");

    memset(grid, 0, sizeof(grid));
    L_T(0, 0) = 3;
    CheckEqI32(Base_CalcLabCap(grid, 0), 0, "a workshop is not a lab");

    /* The lab needed pass marks kind 2 and leaves kind 3 alone. */
    memset(grid, 0, sizeof(grid));
    L_T(0, 0) = 3;
    L_T(1, 0) = 2;
    CheckEqI32(Base_UpdateLabNeeded(grid, 0, 1), 6, "the lab pass also returns 6");
    CheckEqI32(L_N(1, 0), 1, "the lab cell is marked needed");
    CheckEqI32(L_N(0, 0), 0, "the workshop cell in the same row is not");

    memset(grid, 0, sizeof(grid));
    L_T(0, 0) = 3;
    L_T(1, 0) = 2;
    Base_UpdateWorkshopNeeded(grid, 0, 1);
    CheckEqI32(L_N(0, 0), 1, "the workshop pass marks the workshop");
    CheckEqI32(L_N(1, 0), 0, "and leaves the lab unmarked");

    /* Research quantity is the same sum over a different column. */
    memset(col, 0, sizeof(col));
    col[0] = 100;
    col[1] = 200;
    CheckEqI32(Base_CountActiveResearch(col, 2), 300, "research totals the column");
    CheckEqI32(Base_CountActiveResearch(col, 0), 0, "and a zero count reads nothing");
    CheckEqI32(Base_CountActiveResearch(&col[1], 1), 200,
               "a different column offset is a different base");

#undef L_T
#undef L_N
}

static void TestCrewAvailable(void)
{
    printf("Base_GetScientistCount / Base_GetEngineerCount\n");
    static ObjectRecFull sci, eng;
    static u16 col[400];

    memset(&sci, 0, sizeof(sci));
    memset(&eng, 0, sizeof(eng));
    memset(col, 0, sizeof(col));

    sci.auPerBaseVal[0] = 10;
    sci.auPerBaseVal[1] = 20;
    eng.auPerBaseVal[0] = 7;
    eng.auPerBaseVal[1] = 300;

    col[0] = 3;
    col[1] = 4;
    col[2] = 100;
    col[144] = 2;  /* base 1's research window starts 144 u16 in */
    col[145] = 3;
    col[200] = 5;  /* base 1's manufacturing window starts 200 u16 in */
    col[201] = 6;

    CheckEqI32(Base_GetScientistCount(&sci, col, 0, 2), 3, "10 scientists minus 7 allocated");
    CheckEqI32(Base_GetScientistCount(&sci, col, 0, 0), 10, "a zero entry count subtracts nothing");
    CheckEqI32(Base_GetScientistCount(&sci, col, 0, 1), 7, "only the first entry counts");
    CheckEqI32(Base_GetScientistCount(&sci, col + 144, 1, 2), 15,
               "base 1 reads its own total and its own 144-entry window");
    CheckEqI32(Base_GetScientistCount(&sci, NULL, 0, 2), 10, "a NULL column subtracts nothing");
    CheckEqI32(Base_GetScientistCount(&sci, col, 0, 3), -97, "the subtraction has no floor");
    CheckEqI32(Base_GetScientistCount(NULL, col, 0, 2), -7, "a missing item contributes 0");
    CheckEqI32(Base_GetScientistCount(&sci, col, 8, 2), -7,
               "an index past the eight per-base slots gives a total of 0");

    CheckEqI32(Base_GetEngineerCount(&eng, col, 0, 2), 0, "7 engineers minus 7 allocated");
    CheckEqI32(Base_GetEngineerCount(&eng, col, 0, 1), 4, "7 minus only the first entry");
    CheckEqI32(Base_GetEngineerCount(&eng, col + 200, 1, 2), 289, "300 minus base 1's window");
    CheckEqI32(Base_GetEngineerCount(&eng, col, 0, 300), 7 - 123,
               "the entry count is capped at 255, the width of the original's byte read");

    /* The two getters are the same expression over a different item and column, so a column
     * that is not the base's own window reads the wrong entries: this is the caller's job. */
    CheckEqI32(Base_GetEngineerCount(&eng, col + 200, 1, 1), 295, "base 1's first entry only");
}

static void TestRemainingNeededPasses(void)
{
    printf("LivingQuarters / Stores / PsiLab needed passes\n");
    static u8 grid[700];

#define K_T(c, r) grid[480 + 6 * (c) + (r)]
#define K_N(c, r) grid[444 + 6 * (c) + (r)]

    /* Living quarters is kind 1 with the usual divisor of 50. */
    memset(grid, 0, sizeof(grid));
    K_T(0, 0) = 1;
    K_T(1, 0) = 3;
    CheckEqI32(Base_UpdateLivingQuartersNeeded(grid, 0, 1), 6, "living quarters returns 6");
    CheckEqI32(K_N(0, 0), 1, "the kind-1 cell is marked");
    CheckEqI32(K_N(1, 0), 0, "the workshop in the same row is not");

    memset(grid, 0, sizeof(grid));
    K_T(0, 0) = 1;
    K_T(1, 0) = 1;
    Base_UpdateLivingQuartersNeeded(grid, 0, 50);
    CheckEqI32(K_N(0, 0) + K_N(1, 0), 2, "a total of 50 buys two living-quarter cells");

    /* Stores is kind 6 and the only pass with a divisor of 300. */
    memset(grid, 0, sizeof(grid));
    K_T(0, 0) = 6;
    K_T(1, 0) = 6;
    Base_UpdateStoresNeeded(grid, 0, 50);
    CheckEqI32(K_N(0, 0) + K_N(1, 0), 1,
               "a total of 50 buys only one storage cell, the divisor is 300");

    memset(grid, 0, sizeof(grid));
    K_T(0, 0) = 6;
    K_T(1, 0) = 6;
    Base_UpdateStoresNeeded(grid, 0, 300);
    CheckEqI32(K_N(0, 0) + K_N(1, 0), 2, "a total of 300 buys two");

    memset(grid, 0, sizeof(grid));
    K_T(0, 0) = 6;
    CheckEqI32(Base_UpdateStoresNeeded(grid, 0, 0), 6, "a zero total marks nothing");
    CheckEqI32(K_N(0, 0), 0, "the storage cell stays unmarked");

    /* Psi lab is kind 8 with a divisor of 10, the most sensitive of the five. */
    memset(grid, 0, sizeof(grid));
    K_T(0, 0) = 8;
    K_T(1, 0) = 8;
    Base_UpdatePsiLabNeeded(grid, 0, 10);
    CheckEqI32(K_N(0, 0) + K_N(1, 0), 2, "ten psi soldiers buy two lab cells");

    memset(grid, 0, sizeof(grid));
    K_T(0, 0) = 8;
    K_T(1, 0) = 8;
    Base_UpdatePsiLabNeeded(grid, 0, 1);
    CheckEqI32(K_N(0, 0) + K_N(1, 0), 1, "one psi soldier still buys one");

    /* The five passes do not interfere: each only writes its own kind. */
    memset(grid, 0, sizeof(grid));
    K_T(0, 0) = 1;
    K_T(1, 0) = 2;
    K_T(2, 0) = 3;
    K_T(3, 0) = 6;
    K_T(4, 0) = 8;
    Base_UpdateLivingQuartersNeeded(grid, 0, 500);
    Base_UpdateLabNeeded(grid, 0, 500);
    Base_UpdateWorkshopNeeded(grid, 0, 500);
    Base_UpdateStoresNeeded(grid, 0, 500);
    Base_UpdatePsiLabNeeded(grid, 0, 500);
    CheckEqI32(K_N(0, 0) + K_N(1, 0) + K_N(2, 0) + K_N(3, 0) + K_N(4, 0), 5,
               "all five kinds claimed once each, no cross-marking");

#undef K_T
#undef K_N
}

static void TestEmptyFacilityCells(void)
{
    printf("Base_CountEmptyFacilityCells\n");
    static u8 grid[700];

#define E_T(c, r) grid[480 + 6 * (c) + (r)]
#define E_B(c, r) grid[6 * (c) + (r)]

    memset(grid, 0, sizeof(grid));
    CheckEqI32(Base_CountEmptyFacilityCells(grid, 0), 0, "an all-zero grid scores 0");

    memset(grid, 0, sizeof(grid));
    E_T(0, 0) = 4;
    CheckEqI32(Base_CountEmptyFacilityCells(grid, 0), 2, "a free kind-4 cell is worth 2");

    memset(grid, 0, sizeof(grid));
    E_T(0, 0) = 5;
    CheckEqI32(Base_CountEmptyFacilityCells(grid, 0), 3, "a free kind-5 cell is worth 3");

    memset(grid, 0, sizeof(grid));
    E_T(0, 0) = 4;
    E_B(0, 0) = 1;
    CheckEqI32(Base_CountEmptyFacilityCells(grid, 0), 0,
               "a cell under construction contributes nothing");

    memset(grid, 0, sizeof(grid));
    E_T(0, 0) = 3;
    E_T(1, 1) = 8;
    CheckEqI32(Base_CountEmptyFacilityCells(grid, 0), 0,
               "kinds 3 and 8 are not counted at all");

    memset(grid, 0, sizeof(grid));
    E_T(0, 0) = 4;
    E_T(1, 0) = 5;
    E_T(2, 0) = 5;
    E_T(3, 0) = 4;
    E_B(3, 0) = 9;
    CheckEqI32(Base_CountEmptyFacilityCells(grid, 0), 2 + 3 + 3,
               "two 4s and two 5s, one of them occupied, score 8");

    memset(grid, 0, sizeof(grid));
    for (int i = 0; i < 36; ++i) {
        grid[480 + i] = 5;
    }
    CheckEqI32(Base_CountEmptyFacilityCells(grid, 0), 36 * 3,
               "a full grid of free kind-5 cells is 108");

#undef E_T
#undef E_B
}


static void TestCapacityFamily(void)
{
    static u8 grid[700];

    printf("Base capacity family\n");

#define C_T(c, r) grid[480 + 6 * (c) + (r)]
#define C_O(c, r) grid[6 * (c) + (r)]

    /* Living quarters: 50 per free cell. */
    memset(grid, 0, sizeof(grid));
    C_T(0, 0) = 1;
    C_T(1, 1) = 1;
    C_O(1, 1) = 4;
    CheckEqI32(Base_CalcLivingQuartersCap(grid, 0), 50,
               "one free living-quarters cell houses 50, the one under construction counts for nothing");

    /* Stores: 300 per free cell. */
    memset(grid, 0, sizeof(grid));
    C_T(0, 0) = 6;
    C_T(2, 3) = 6;
    CheckEqI32(Base_CalcStoresCapacity(grid, 0), 600,
               "one stores cell is worth 300 units");

    /* Alien containment: 10 per free cell. */
    memset(grid, 0, sizeof(grid));
    C_T(5, 5) = 7;
    CheckEqI32(Base_CalcAlienContainmentCap(grid, 0), 10,
               "one containment cell holds 10");

    /* Hangar: one slot per free cell, no multiplier. */
    memset(grid, 0, sizeof(grid));
    C_T(0, 0) = 11;
    C_T(0, 1) = 11;
    C_T(0, 2) = 11;
    C_O(0, 2) = 9;
    CheckEqI32(Base_CalcHangarCap(grid, 0), 2,
               "a hangar cell is one craft slot, and an unfinished one is not a slot");

    /* Base_HasResearchSlotFree and Base_HasWorkshopSlotFree compute the whole capacity and
     * compare, so the boundary is the capacity itself. */
    memset(grid, 0, sizeof(grid));
    C_T(0, 0) = 2;
    C_T(1, 1) = 2;
    CheckEqI32(Base_HasResearchSlotFree(grid, 0, 100), 1,
               "two free lab cells cover a need of exactly 100");
    CheckEqI32(Base_HasResearchSlotFree(grid, 0, 150), 0,
               "and do not cover 150");
    CheckEqI32(Base_HasResearchSlotFree(grid, 0, 0), 1,
               "a need of zero is always covered");

    memset(grid, 0, sizeof(grid));
    C_T(0, 0) = 3;
    CheckEqI32(Base_HasWorkshopSlotFree(grid, 0, 50), 1, "one workshop cell covers 50");
    CheckEqI32(Base_HasWorkshopSlotFree(grid, 0, 51), 0, "and not 51");

    /* Base_CountBuiltFacilities is the only member that ignores build days. */
    memset(grid, 0, sizeof(grid));
    C_T(0, 0) = 13;
    C_T(0, 1) = 14;
    C_T(0, 2) = 15;
    C_T(0, 3) = 16;
    CheckEqI32(Base_CountBuiltFacilities(grid, 0), 500 + 600 + 900 + 1500,
               "kinds 13, 14, 15 and 16 are weighted 500, 600, 900 and 1500");

    C_O(0, 0) = 7; /* under construction */
    CheckEqI32(Base_CountBuiltFacilities(grid, 0), 500 + 600 + 900 + 1500,
               "and unlike every other member it does not test build days");

    memset(grid, 0, sizeof(grid));
    C_T(0, 0) = 12;
    C_T(0, 1) = 17;
    CheckEqI32(Base_CountBuiltFacilities(grid, 0), 0, "kinds 12 and 17 are not counted");

    /* A second base reads its own 144-byte type block. */
    memset(grid, 0, sizeof(grid));
    grid[480 + 144 * 1 + 6 * 0 + 0] = 6;
    CheckEqI32(Base_CalcStoresCapacity(grid, 1), 300, "base 1 has its own stores cells");
    CheckEqI32(Base_CalcStoresCapacity(grid, 0), 0, "and base 0 does not see them");

#undef C_T
#undef C_O
}


static void TestSoldiersAssigned(void)
{
    static Soldier g_aSol[5];
    static Soldier *g_apSol[5];
    static Ship g_aShip[2];
    static Ship *g_apShip[2];

    printf("Base_CountSoldiersAssigned\n");

    memset(g_aSol, 0, sizeof(g_aSol));
    memset(g_aShip, 0, sizeof(g_aShip));
    for (int i = 0; i < 5; ++i) {
        g_apSol[i] = &g_aSol[i];
        g_aSol[i].nKind = 0;
        g_aSol[i].bUnk183 = 255;
    }
    g_aShip[0].bAtBase = 1;
    g_aShip[1].bAtBase = 0;
    for (int i = 0; i < 2; ++i) {
        g_apShip[i] = &g_aShip[i];
    }

    g_aSol[1].bUnk183 = 0;   /* aboard craft 0, which is home */
    g_aSol[2].bUnk183 = 1;   /* aboard craft 1, which is out */
    g_aSol[3].bUnk183 = 254; /* away, sentinel */
    g_aSol[4].nKind = 1;     /* a different base */

    CheckEqI32(Base_CountSoldiersAssigned(g_apSol, 5, g_apShip, 2, 0), 2,
               "unassigned soldiers and those in a craft that is home both count");
    CheckEqI32(Base_CountSoldiersAssigned(g_apSol, 5, g_apShip, 2, 1), 1,
               "the soldier whose base slot is 1 belongs to base 1");
    CheckEqI32(Base_CountSoldiersAssigned(g_apSol, 5, g_apShip, 2, 2), 0,
               "a base nobody is assigned to has none");

    /* Flip the craft's at-base flag and the count follows. */
    g_aShip[1].bAtBase = 1;
    CheckEqI32(Base_CountSoldiersAssigned(g_apSol, 5, g_apShip, 2, 0), 3,
               "a soldier in a craft that came home counts");
    g_aShip[0].bAtBase = 0;
    g_aShip[1].bAtBase = 0;
    CheckEqI32(Base_CountSoldiersAssigned(g_apSol, 5, g_apShip, 2, 0), 1,
               "and neither counts once both craft are out");

    /* An out-of-range craft index is refused rather than read past the array. */
    g_aSol[1].bUnk183 = 7;
    CheckEqI32(Base_CountSoldiersAssigned(g_apSol, 5, g_apShip, 2, 0), 1,
               "a craft index past the array is not counted");
}


static void TestShipsAtBase(void)
{
    static Ship g_aShip[3];
    static Ship *g_apShip[3];
    static BaseRec g_aBase[2];
    static BaseRec *g_apBase[2];
    static u8 g_abGrid[8];

    printf("Base_CountShipsAtBase\n");

    memset(g_aShip, 0, sizeof(g_aShip));
    memset(g_aBase, 0, sizeof(g_aBase));
    memset(g_abGrid, 0, sizeof(g_abGrid));
    for (int i = 0; i < 3; ++i) {
        g_apShip[i] = &g_aShip[i];
        g_aShip[i].bHomeBase = (u8)(i < 2 ? 0 : 1);
    }
    for (int i = 0; i < 2; ++i) {
        g_apBase[i] = &g_aBase[i];
    }
    g_aBase[0].bUnk4 = 7;
    g_aBase[1].bUnk4 = 9;
    g_abGrid[0] = 5;
    g_abGrid[1] = 3;

    CheckEqI32(Base_CountShipsAtBase(g_apShip, 3, g_apBase, 2, g_abGrid, 0), 7,
               "two craft homed here, plus the grid byte for this base's type byte");
    CheckEqI32(Base_CountShipsAtBase(g_apShip, 3, g_apBase, 2, g_abGrid, 1), 4,
               "one craft homed at base 1, plus its own grid byte");

    /* The grid half follows the type byte, not the base index: a second base sharing the
     * type byte resolves to the first one. */
    g_aBase[1].bUnk4 = 7;
    CheckEqI32(Base_CountShipsAtBase(g_apShip, 3, g_apBase, 2, g_abGrid, 1), 1 + 5,
               "a shared type byte resolves to the first base that has it");

    /* The type byte always answers at least its own base, so the grid half is never
     * skipped by a failed search. What can skip it is an empty base slot. */
    g_aBase[0].bUnk4 = 20;
    g_aBase[1].bUnk4 = 21;
    CheckEqI32(Base_CountShipsAtBase(g_apShip, 3, g_apBase, 2, g_abGrid, 1), 1 + 3,
               "a base with its own type byte reads its own grid slot");

    g_apBase[0] = NULL;
    CheckEqI32(Base_CountShipsAtBase(g_apShip, 3, g_apBase, 2, g_abGrid, 0), 2,
               "an empty base slot still counts the craft homed there, and adds no grid byte, where the original would dereference it");
    g_apBase[0] = &g_aBase[0];

    /* Deviation: the original indexes apBases with an unbounded base index. */
    CheckEqI32(Base_CountShipsAtBase(g_apShip, 3, g_apBase, 2, g_abGrid, 5), 0,
               "a base index past the array is refused rather than read");
}

static void TestClearSelection(void)
{
    static UiLineList lines;
    static GameTime time;
    static u8 bSelected;

    printf("Smap_ClearSelection\n");

    memset(&lines, 0, sizeof(lines));
    memset(&time, 0, sizeof(time));
    lines.nLineCount = 2;
    time.bPaused = 1;
    bSelected = 1;

    /* Two messages on screen: clearing the selection removes one and leaves the game paused,
     * because a message is still up. */
    Smap_ClearSelection(&lines, &time, &bSelected);
    CheckEqI32(lines.nLineCount, 1, "the first line is dropped");
    CheckEqI32(time.bPaused, 1, "and the game stays paused while a message is still on screen");
    CheckEqI32(bSelected, 0, "the selection flag is cleared either way");

    /* Now the list is empty, so the same call is allowed to resume the game. */
    Smap_ClearSelection(&lines, &time, &bSelected);
    CheckEqI32(lines.nLineCount, 0, "the list is empty");
    CheckEqI32(time.bPaused, 0,
               "and only now does the same call unpause - that conditional is the function");
}

static void TestDismissSelected(void)
{
    static UiLineList lines;
    static GameTime time;
    static u8 bSelected;
    static PtrArray list;
    static GeoscapeWorld world;
    static StrategSpawnCtx ctx;
    static Ship g_aShip[1];
    static Ship *g_apShip[1];
    static BaseRec g_Base;
    static BaseRec *g_apBase[1];
    static Soldier *g_apSol[1];
    static ObjectRecFull *g_apObj[1];
    static u8 g_abHangar[64];
    static i32 g_anAssigned[1];
    static i32 nSeed, nTotalSec, nAssignedCount;
    static u8 nShipCount, once;
    static u16 nSoldierCount;
    ShipRemovalEnv env;
    StrategItem *pEntry;
    void **apSlots;

    printf("Smap_DismissSelected\n");

    memset(&lines, 0, sizeof(lines));
    memset(&time, 0, sizeof(time));
    lines.nLineCount = 2;
    lines.anColor[0][0] = 0; /* the selected craft index lives in colour slot 0 of line 0 */
    lines.anColor[1][0] = 0;
    time.bPaused = 1;
    bSelected = 1;

    memset(&g_aShip[0], 0, sizeof(g_aShip[0]));
    g_aShip[0].bId = 10;
    g_aShip[0].bHomeBase = 0;
    g_aShip[0].bIntercepting = 1;
    g_aShip[0].nStrategIdx = 0;
    g_apShip[0] = &g_aShip[0];

    pEntry = (StrategItem *)calloc(1, sizeof(StrategItem));
    pEntry->nType = 1;
    pEntry->bActive = 0;
    pEntry->nDisplayId = 10; /* the craft's own entry, found by bId */
    pEntry->nTimer = 777;
    pEntry->nX = 40;
    pEntry->nY = 60;
    apSlots = (void **)malloc(sizeof(void *) * 2);
    apSlots[0] = pEntry;
    list.nCount = 1;
    list.apItems = apSlots;

    memset(&g_Base, 0, sizeof(g_Base));
    g_Base.nUnk8 = 100;
    g_Base.nUnk12 = 200;
    g_apBase[0] = &g_Base;

    nShipCount = 1;
    nSoldierCount = 0;
    nAssignedCount = 0;
    once = 0;
    nTotalSec = 0;
    nSeed = 0;
    world.apCraft = g_apShip;
    world.nShipCount = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnSeed = &nSeed;

    env.apCraft = g_apShip;
    env.pnShipCount = &nShipCount;
    env.apSoldiers = g_apSol;
    env.pnSoldierCount = &nSoldierCount;
    env.apObjects = g_apObj;
    env.nObjectCount = 0;
    env.pStrategList = &list;
    env.pWorld = &world;
    env.pnSpawnOnceFlag = &once;
    env.pSpawnCtx = &ctx;
    env.abHangarFlag = g_abHangar;
    env.nHangarFlagCap = 64;
    env.anAssignedShip = g_anAssigned;
    env.pnAssignedShipCount = &nAssignedCount;
    env.apBases = g_apBase;
    env.pnGameTotalSec = &nTotalSec;

    /* Two messages on screen: the selected craft is sent home, line 0 is dropped, and a message
     * is still up, so the game stays paused. */
    CheckEqI32((i32)Smap_DismissSelected(&env, &lines, &time, &bSelected), 0,
               "returns the cleared flag value");
    CheckEqI32(lines.nLineCount, 1, "the first line is dropped");
    CheckEqI32(time.bPaused, 1, "the game stays paused while a message is still on screen");
    CheckEqI32(bSelected, 0, "the selection flag is cleared either way");
    CheckEqI32(pEntry->nTimer, 0, "the selected craft's own entry timer is restarted at zero");
    CheckEqI32(pEntry->nDestX, 100, "and its travel heads for the home base x");
    CheckEqI32(pEntry->nDestY, 200, "and its y");
    CheckEqI32(g_aShip[0].bIntercepting, 0, "and the intercept flag is cleared");

    /* Now the list is empty, so the same call is allowed to resume the game. */
    (void)Smap_DismissSelected(&env, &lines, &time, &bSelected);
    CheckEqI32(lines.nLineCount, 0, "the list is empty");
    CheckEqI32(time.bPaused, 0,
               "and only now does the call unpause - the same conditional as Smap_ClearSelection");

    free(apSlots);
    free(pEntry);
}

static void TestInitCraftSelectState(void)
{
    static u8 g_abGrid[1524];
    static u8 g_abTiles[44];

    printf("Smap_InitCraftSelectState\n");

/* buildDays lane and facType layer, the two row bases the ARM forms */
#define C_D(b, r, c) g_abGrid[36 * (b) + 6 * (r) + (c)]
#define C_F(b, r, c) g_abGrid[480 + 144 * (b) + 6 * (r) + (c)]

    /* An empty base: buildDays all zero, facType all the 0xFF sentinel -> the grid is all 20,
     * and the guard bytes past the grid are untouched. */
    memset(g_abGrid, 0, sizeof(g_abGrid));
    memset(g_abGrid + 480, 0xFF, sizeof(g_abGrid) - 480);
    memset(g_abTiles, 0xCC, sizeof(g_abTiles));
    Smap_InitCraftSelectState(g_abGrid, g_abTiles, 0);
    {
        int bAll20 = 1;
        for (int i = 0; i < 36; ++i) {
            if (g_abTiles[i] != 20) { bAll20 = 0; }
        }
        Check(bAll20, "an empty base fills the grid with the 20 sentinel");
        Check(g_abTiles[36] == 0xCC && g_abTiles[43] == 0xCC,
              "and never writes past the 36-byte grid");
    }

    /* Base 1: a plain facility maps through the table; a cell under construction contributes
     * nothing even when its facType is set. */
    C_D(1, 0, 0) = 0;  C_F(1, 0, 0) = 6;  /* code 7 */
    C_D(1, 1, 1) = 5;  C_F(1, 1, 1) = 3;  /* under construction */
    Smap_InitCraftSelectState(g_abGrid, g_abTiles, 1);
    CheckEqI32(g_abTiles[0], 7, "facType 6 maps to placement code 7");
    CheckEqI32(g_abTiles[7], 20, "a cell under construction stays empty");

    /* The hangar stamp wins over the later cell's own code: the anchor at (2,3) stamps
     * 16/17/18/19 at +0/+1/+6/+7, and the loop reaches cell (2,4) afterwards - its facType 0
     * would give code 0, but the `== 20` gate skips the already-stamped cell. */
    C_F(1, 2, 3) = 11;
    C_F(1, 2, 4) = 0;
    Smap_InitCraftSelectState(g_abGrid, g_abTiles, 1);
    CheckEqI32(g_abTiles[15], 16, "the hangar anchor is stamped 16");
    CheckEqI32(g_abTiles[16], 17, "the +1 cell 17");
    CheckEqI32(g_abTiles[21], 18, "the +6 cell 18");
    CheckEqI32(g_abTiles[22], 19, "and the diagonal +7 cell 19");
    Check(g_abTiles[0] == 7 && g_abTiles[7] == 20,
          "and the earlier cells are rebuilt the same way - the grid is re-derived, not merged");

    /* The guard: a hangar anchored at (5,5) would write six bytes past the grid in the original
     * (into 0x159D80, the base count byte); the port refuses those stores. */
    C_F(2, 5, 5) = 11;
    Smap_InitCraftSelectState(g_abGrid, g_abTiles, 2);
    CheckEqI32(g_abTiles[35], 16, "the (5,5) hangar still stamps its anchor");
    Check(g_abTiles[36] == 0xCC && g_abTiles[42] == 0xCC,
          "but the past-the-grid stamp stores are refused (deviation, latent original bug)");

#undef C_D
#undef C_F
}

/* --- Smap_BeginCraftSelect fixtures -------------------------------------------------------- */

static i32 g_nFreeAll0, g_nFreeAll1, g_nInitMissionCalls, g_nEndScreenCalls;

static void SelHookFreeAll(i32 bFreeWorld, void *pUser)
{
    (void)pUser;
    if (bFreeWorld != 0) { ++g_nFreeAll1; } else { ++g_nFreeAll0; }
}
static i32 SelHookInitMission(i32 nMode, void *pUser)
{
    (void)nMode; (void)pUser;
    ++g_nInitMissionCalls;
    return 7;
}
static void SelHookEndScreen(u8 bGoodEnd, void *pUser)
{
    (void)bGoodEnd; (void)pUser;
    ++g_nEndScreenCalls;
}

static void TestBeginCraftSelect(void)
{
    static Ship *g_apShip[1];
    static BaseRec *g_apBase[8];
    static Soldier *g_apSol[1];
    static ObjectRecFull *g_apObj[1];
    static BaseSiteRec g_aSite[8];
    static u8 g_abGrid[1524];
    static u8 g_abTiles[36];
    static u8 g_abOverlay[256];
    static u8 g_abAlt[256];
    static PadImage aOverlay[3];
    static PadImage g_AltMap;
    static SmapView g_View;
    static UiLineList g_Lines;
    static GameTime g_Time;
    static PtrArray g_List;
    static GeoscapeWorld g_World;
    static StrategSpawnCtx g_Ctx;
    static ShipRemovalEnv g_Rem;
    static BaseDemolishEnv g_Dem;
    static SmapSite g_aSites[4];
    static u16 g_aManuf[200], g_aProdC[200], g_aResearch[144];
    static i32 g_aProdB[200];
    static u16 g_nResearchEntries, g_nObjectCount, g_nSoldierCount;
    static i32 g_nSeed, g_nTotalSec, g_nAssignedCount;
    static u8 g_abHangar[64];
    static i32 g_anAssigned[1];
    static i32 nSelectedShip, nStratIdx2, nSelStrat, nRadar, nFunding;
    static u8 bAssault, bSelFlag, bFlag187084, bGeoFlag, bSelBaseIdx;
    static u8 nCraftCount, bSpawnOnce, bTickHold, bBattleEnded, nCraftIndex;
    static u8 bMissionMode, bUnk1511E, nMissionType, abSprite[4], bSpawnAlt, bUnk15115;
    static u8 nTerrainVariant, nMapLayers;
    static i32 nTierRaw;
    static u8 bBaseCount;
    static i8 bHomeBase, bBaseRace;
    static u8 nShipCountByte;
    SmapCraftSelectEnv env;
    Ship *pShip;
    StrategItem *pItem;
    StrategItem *pTarget;
    i32 nRet;

    printf("\nSmap_BeginCraftSelect @ 0x838dc\n");
    (void)fflush(stdout);

    /* Shared static furniture, built once. */
    /* The base record is heap-allocated because Base_DemolishFacility frees it (0x6715C,
     * operator delete); case 5 is the only case that reaches that free. */
    BaseRec *pBaseRec = (BaseRec *)calloc(1, sizeof(BaseRec));
    pBaseRec->bUnk4  = 7;
    pBaseRec->nUnk8  = 100;
    pBaseRec->nUnk12 = 200;
    g_apBase[0] = pBaseRec;
    memset(g_aSite, 0, sizeof(g_aSite));
    g_aSite[0].nFacType = 7;
    memset(g_abGrid, 0, sizeof(g_abGrid));
    memset(g_abGrid + 480, 0xFF, 36); /* base 0's facType lane: an empty base, every cell 0xFF */
    memset(g_abOverlay, 0x10, sizeof(g_abOverlay)); /* every overlay tile is under 0x32 */
    memset(g_abAlt, 0, sizeof(g_abAlt));
    g_abAlt[5 * 16 + 10] = 242;                    /* polar, at the item's (10, 5) */
    for (i32 k = 0; k < 3; ++k) {
        PadImage_ctor(&aOverlay[k]);
        aOverlay[k].uWidth = 16;
        aOverlay[k].uHeight = 16;
        aOverlay[k].pData = g_abOverlay;
    }
    PadImage_ctor(&g_AltMap);
    g_AltMap.uWidth = 16;
    g_AltMap.uHeight = 16;
    g_AltMap.pData = g_abAlt;
    memset(&g_View, 0, sizeof(g_View));

    g_World.apCraft = g_apShip;
    g_World.nShipCount = 1;
    memset(&g_Ctx, 0, sizeof(g_Ctx));
    g_nSeed = 0;
    g_Ctx.pnSeed = &g_nSeed;

    /* Each case rebuilds the mutable decision state and a fresh craft + strateg entry. */
#define RESET(flag, mode)                                                                          \
    do {                                                                                           \
        pShip = (Ship *)calloc(1, sizeof(Ship));                                                   \
        pShip->bId = 10;                                                                           \
        pShip->bHomeBase = 0;                                                                      \
        pShip->bAtBase = 1;                                                                        \
        pShip->bWeaponsHot = 1;                                                                    \
        pShip->bCraftType = 2;                                                                     \
        pShip->nStrategIdx = 0;                                                                    \
        g_apShip[0] = pShip;                                                                       \
        nShipCountByte = 1;                                                                        \
                                                                                                   \
        PtrArray_ctor(&g_List);                                                                    \
        (void)StrategItem_AllocAdd(&g_List, 0);                                                    \
        (void)StrategItem_AllocAdd(&g_List, 1);                                                    \
        pItem = (StrategItem *)g_List.apItems[0];  /* the craft's own entry */                     \
        pItem->nType = 1;                                                                          \
        pItem->bActive = 0;                                                                        \
        pItem->nDisplayId = 10;                                                                    \
        pItem->nSiteId = STRATEG_ID_UNASSIGNED;                                                    \
        pItem->bRace = 1;                                                                          \
        pItem->nTimer = 777;                                                                       \
        pItem->bVisible = 1;                                                                       \
        pTarget = (StrategItem *)g_List.apItems[1]; /* the selected target item */                 \
        pTarget->nType = 1;                                                                        \
        pTarget->bActive = 0;                                                                      \
        pTarget->nDisplayId = 99999;                                                               \
        pTarget->nSiteId = STRATEG_ID_UNASSIGNED;                                                  \
        pTarget->bCraftClass = 2;                                                                  \
        pTarget->nHp = 200;                                                                        \
        pTarget->nX = 10;                                                                          \
        pTarget->nY = 5;                                                                           \
        pTarget->bVisible = 1;                                                                     \
                                                                                                   \
        memset(&g_Lines, 0, sizeof(g_Lines));                                                      \
        g_Lines.nLineCount = 1;                                                                    \
        g_Lines.abLineFlags[0] = (u8)(flag);                                                       \
        g_Lines.anColor[0][0] = 0;                                                                 \
        g_Lines.anColor[0][1] = 1;                                                                 \
        memset(&g_Time, 0, sizeof(g_Time));                                                        \
        g_Time.bPaused = 1;                                                                        \
                                                                                                   \
        bMissionMode = (u8)(mode);                                                                 \
        bUnk1511E = 0;                                                                             \
        nMissionType = 0;                                                                          \
        memset(abSprite, 0, sizeof(abSprite));                                                     \
        bSpawnAlt = 0;                                                                             \
        bUnk15115 = 0;                                                                             \
        nTerrainVariant = 0;                                                                       \
        nMapLayers = 0;                                                                            \
        nTierRaw = -1;                                                                             \
        nCraftIndex = 0xFF;                                                                        \
        bTickHold = 0;                                                                             \
        bBattleEnded = 0;                                                                          \
        nSelectedShip = -1;                                                                        \
        nStratIdx2 = -1;                                                                           \
        nSelStrat = -1;                                                                            \
        bAssault = 0;                                                                              \
        bSelFlag = 1;                                                                              \
        bFlag187084 = 1;                                                                           \
        bGeoFlag = 0;                                                                              \
        nCraftCount = 1;                                                                           \
        bSpawnOnce = 0;                                                                            \
        nRadar = 0;                                                                                \
        nFunding = 10000;                                                                          \
        bBaseCount = 1;                                                                            \
        bHomeBase = 0;                                                                             \
        bBaseRace = 1;                                                                             \
        bSelBaseIdx = 0xFF;                                                                        \
        memset(g_abTiles, 0xCC, sizeof(g_abTiles));                                                \
        g_nFreeAll0 = g_nFreeAll1 = g_nInitMissionCalls = g_nEndScreenCalls = 0;                   \
                                                                                                   \
        g_Rem.apCraft = g_apShip;                                                                  \
        g_Rem.pnShipCount = &nShipCountByte;                                                       \
        g_Rem.apSoldiers = g_apSol;                                                                \
        g_nSoldierCount = 0;                                                                       \
        g_Rem.pnSoldierCount = &g_nSoldierCount;                                                   \
        g_Rem.apObjects = g_apObj;                                                                 \
        g_nObjectCount = 0;                                                                        \
        g_Rem.nObjectCount = 0;                                                                    \
        g_Rem.pStrategList = &g_List;                                                              \
        g_Rem.pWorld = &g_World;                                                                   \
        g_Rem.pnSpawnOnceFlag = &bSpawnOnce;                                                       \
        g_Rem.pSpawnCtx = &g_Ctx;                                                                  \
        g_Rem.abHangarFlag = g_abHangar;                                                           \
        g_Rem.nHangarFlagCap = 64;                                                                 \
        g_Rem.anAssignedShip = g_anAssigned;                                                       \
        g_nAssignedCount = 0;                                                                      \
        g_Rem.pnAssignedShipCount = &g_nAssignedCount;                                             \
        g_Rem.apBases = g_apBase;                                                                  \
        g_Rem.pnGameTotalSec = &g_nTotalSec;                                                       \
        g_nTotalSec = 0;                                                                           \
                                                                                                   \
        memset(&g_Dem, 0, sizeof(g_Dem));                                                          \
        g_Dem.apCraft = g_apShip;                                                                  \
        g_Dem.pnShipCount = &nShipCountByte;                                                       \
        g_Dem.apSoldiers = g_apSol;                                                                \
        g_Dem.pnSoldierCount = &g_nSoldierCount;                                                   \
        g_Dem.apBases = g_apBase;                                                                  \
        g_Dem.apObjects = g_apObj;                                                                 \
        g_Dem.pnObjectCount = &g_nObjectCount;                                                     \
        g_Dem.abFacilityGrid = g_abGrid;                                                           \
        g_Dem.apSiteRecs = g_aSite;                                                                \
        g_Dem.apManufCols = g_aManuf;                                                              \
        g_Dem.apProductC = g_aProdC;                                                               \
        g_Dem.apProductB = g_aProdB;                                                               \
        g_Dem.apResearchCols = g_aResearch;                                                        \
        g_Dem.pnResearchEntries = &g_nResearchEntries;                                             \
        g_Dem.apSites = g_aSites;                                                                  \
        g_Dem.nSiteCount = 0;                                                                      \
        g_Dem.pbBaseCount = &bBaseCount;                                                           \
        g_Dem.pShipEnv = &g_Rem;                                                                   \
                                                                                                   \
        env.apCraft = g_apShip;                                                                    \
        env.pnCraftCount = &nCraftCount;                                                           \
        env.pbMissionMode = &bMissionMode;                                                         \
        env.pbUnk1511E = &bUnk1511E;                                                               \
        env.pnMissionType = &nMissionType;                                                         \
        env.abSpriteSet = abSprite;                                                                \
        env.pbSpawnAlt = &bSpawnAlt;                                                               \
        env.pbUnk15115 = &bUnk15115;                                                               \
        env.pnTerrainVariant = &nTerrainVariant;                                                   \
        env.pnMapLayers = &nMapLayers;                                                             \
        env.pnTierRaw = &nTierRaw;                                                                 \
        env.pnCraftIndex = &nCraftIndex;                                                           \
        env.pbTickHold = &bTickHold;                                                               \
        env.pbBattleEnded = &bBattleEnded;                                                         \
        env.apBases = g_apBase;                                                                    \
        env.pbHomeBase = &bHomeBase;                                                               \
        env.pbBaseRace = &bBaseRace;                                                               \
        env.pStrategList = &g_List;                                                                \
        env.pLines = &g_Lines;                                                                     \
        env.pTime = &g_Time;                                                                       \
        env.pnSelectedShipIdx = &nSelectedShip;                                                    \
        env.pnStrategIndex = &nStratIdx2;                                                          \
        env.pnSelectedStrategIdx = &nSelStrat;                                                     \
        env.pbAssaultFlag = &bAssault;                                                             \
        env.pbSelectionFlag = &bSelFlag;                                                           \
        env.pbFlag187084 = &bFlag187084;                                                           \
        env.pbGeoscapeFlag = &bGeoFlag;                                                            \
        env.pnRadarPulseTimer = &nRadar;                                                           \
        env.pnFunding = &nFunding;                                                                 \
        env.pbBaseCount = &bBaseCount;                                                             \
        env.pbSelectedBaseIdx = &bSelBaseIdx;                                                      \
        env.abFacilityGrid = g_abGrid;                                                             \
        env.abTileTypes = g_abTiles;                                                               \
        env.apSoldiers = g_apSol;                                                                  \
        env.nSoldierCount = 0;                                                                     \
        env.apObjects = g_apObj;                                                                   \
        env.nObjectCount = 0;                                                                      \
        env.pView = &g_View;                                                                       \
        env.apOverlay[0] = &aOverlay[0];                                                           \
        env.apOverlay[1] = &aOverlay[1];                                                           \
        env.apOverlay[2] = &aOverlay[2];                                                           \
        env.nOverlayOriginX = 0;                                                                   \
        env.pTilemapAlt = &g_AltMap;                                                               \
        env.pShipEnv = &g_Rem;                                                                     \
        env.pbSpawnOnceFlag = &bSpawnOnce;                                                         \
        env.pSpawnCtx = &g_Ctx;                                                                    \
        env.pDemolishEnv = &g_Dem;                                                                 \
        env.pfnFreeGeoscapeUi = SelHookFreeAll;                                                    \
        env.pfnInitMission = SelHookInitMission;                                                   \
        env.pfnInitEndScreen = SelHookEndScreen;                                                   \
        env.pHookUser = NULL;                                                                      \
    } while (0)

    /* Case 1: flag 21 - urban mission. */
    RESET(21, 0);
    nRet = Smap_BeginCraftSelect(&env);
    CheckEqI32(pShip->bWeaponsHot, 0, "the visibility pass parks the craft with cold weapons");
    /* The visibility pass hid the entry, but the tail's DismissSelected retargets the craft and
     * Ship_RetargetTravel -> StrategEntry_InitTravel re-raises bVisible (its bB argument is 1).
     * The original re-raises it the same way. */
    CheckEqI32(pItem->bVisible, 1, "and the retarget re-raises the entry's visibility");
    CheckEqI32(bUnk1511E, 0, "the overlay tile under the item (0x10 < 0x32) clears unk_128AD6");
    CheckEqI32(abSprite[0], 10, "race 1 picks sprite set 10/15/19/23");
    CheckEqI32(abSprite[3], 23, "down to the no-race marker");
    CheckEqI32(nMissionType, 5, "flag 21 makes it an urban mission (type 5)");
    CheckEqI32(bMissionMode, 3, "mode 3");
    CheckEqI32(nMapLayers, 3, "map layers from the craft's bCraftType + 1");
    CheckEqI32(g_List.nCount, 1, "the target item is removed from the strateg list");
    CheckEqI32(pItem->nTimer, 0, "DismissSelected retargets the selected craft's own entry");
    CheckEqI32(g_Lines.nLineCount, 0, "the tail's DismissSelected drops the line");
    CheckEqI32(g_Time.bPaused, 0, "and unpauses because the list is now empty");
    CheckEqI32(bSelFlag, 0, "the selection flag is cleared");
    CheckEqI32(g_nFreeAll0, 1, "GeoscapeUi_FreeAll(0) runs once");
    CheckEqI32(g_nInitMissionCalls, 1, "and the mission launches");
    CheckEqI32(nRet, 7, "the return is Tact_InitMission's value");
    free(pShip);
    PtrArray_DeleteAll(&g_List);

    /* Case 2: flag 22 - alien base. */
    RESET(22, 0);
    nRet = Smap_BeginCraftSelect(&env);
    CheckEqI32(nMissionType, 4, "flag 22 makes it an alien base mission (type 4)");
    CheckEqI32(bMissionMode, 2, "mode 2");
    CheckEqI32(nMapLayers, 0, "with no map layers");
    CheckEqI32(nRet, 7, "and the same launch tail");
    free(pShip);
    PtrArray_DeleteAll(&g_List);

    /* Case 3: flag 23 - crash landing on a polar tile. */
    RESET(23, 0);
    nRet = Smap_BeginCraftSelect(&env);
    CheckEqI32(nMissionType, 8, "alt tile 242 picks polar (type 8)");
    CheckEqI32(nTerrainVariant, 2, "craft class 2 sets the terrain variant");
    CheckEqI32(nTierRaw, 1, "and the tier raw");
    CheckEqI32(bUnk15115, 6, "the band is 100*(250-200)/250 = 20, then /3 = 6");
    CheckEqI32(bMissionMode, 0, "mode 0");
    CheckEqI32(nRet, 7, "launch tail");
    free(pShip);
    PtrArray_DeleteAll(&g_List);

    /* Case 4: mode 1, flag 17 - Xcom base assault WITH soldiers: straight to launch, and
     * DismissSelected is skipped because mode stays 1. */
    RESET(17, 1);
    {
        static Soldier sol;
        memset(&sol, 0, sizeof(sol));
        sol.nKind = 0;    /* this base */
        sol.bUnk183 = 255; /* not aboard anything */
        g_apSol[0] = &sol;
        env.nSoldierCount = 1;
    }
    nRet = Smap_BeginCraftSelect(&env);
    CheckEqI32(nMissionType, 3, "the assault is an Xcom base mission (type 3)");
    CheckEqI32(abSprite[1], 15, "with the base race's sprite set");
    CheckEqI32(g_abTiles[0], 20, "Smap_InitCraftSelectState built the tile grid (all empty)");
    CheckEqI32(bSelBaseIdx, 0, "nSelectedBaseIdx = nCraftHomeBase");
    CheckEqI32(bMissionMode, 1, "mode stays 1");
    CheckEqI32(g_Lines.nLineCount, 1, "so DismissSelected is skipped and the line survives");
    CheckEqI32(g_Time.bPaused, 1, "and the game stays paused");
    CheckEqI32(bSelFlag, 0, "but the selection flag is still cleared");
    CheckEqI32(nRet, 7, "and the mission launches");
    g_apSol[0] = NULL;
    free(pShip);
    PtrArray_DeleteAll(&g_List);

    /* Case 5: the same assault WITHOUT soldiers: funding is deducted, the base is demolished,
     * the base count hits zero, and the bad ending runs. */
    RESET(17, 1);
    nRet = Smap_BeginCraftSelect(&env);
    /* 10000 - 800 - 400 (the one craft homed at base 0) - 30*0 (no soldiers, no objects). */
    CheckEqI32(nFunding, 8800, "funding loses 800 + 400 per craft at the base + 30*occupied");
    CheckEqI32(bGeoFlag, 1, "the geoscape flag at 0x15961C is set");
    CheckEqI32(bFlag187084, 0, "and the write-only byte is cleared");
    CheckEqI32(bBaseCount, 0, "Base_DemolishFacility removed the base (site nFacType 7)");
    CheckEqI32(nShipCountByte, 0, "with the craft homed there");
    CheckEqI32(g_nFreeAll1, 1, "GeoscapeUi_FreeAll(1) releases the world");
    CheckEqI32(g_nEndScreenCalls, 1, "EndScreen_Init(0) runs the bad ending");
    CheckEqI32(bTickHold, 1, "the tick hold is set");
    CheckEqI32(bBattleEnded, 1, "and the battle-end flag");
    CheckEqI32(nRet, 1, "the return is 1 on the bad-end path");
    CheckEqI32(g_nInitMissionCalls, 0, "and no mission launches");
    /* pShip was freed by Ship_RemoveWithCargo inside the demolish - do not free it again. */
    PtrArray_DeleteAll(&g_List);

#undef RESET
}

static void TestPsiCapAndUpkeep(void)
{
    static u8 g_abGrid[700];
    static Soldier g_aSol[3];
    static Soldier *g_apSol[3];
    static Ship g_aShip[2];
    static Ship *g_apShip[2];
    static ObjectRecFull g_aObj[114];
    static ObjectRecFull *g_apObj[114];
    static SmapSite g_aSite[2];

    printf("Base_CalcPsiLabCap and Base_CalcMonthlyUpkeep\n");

#define P_T(c, r) g_abGrid[480 + 6 * (c) + (r)]
#define P_O(c, r) g_abGrid[6 * (c) + (r)]

    memset(g_abGrid, 0, sizeof(g_abGrid));
    P_T(0, 0) = 8;
    CheckEqI32(Base_CalcPsiLabCap(g_abGrid, 0), 10, "one free psi lab cell holds 10");
    P_O(0, 0) = 5;
    CheckEqI32(Base_CalcPsiLabCap(g_abGrid, 0), 0, "one under construction holds nothing");

    for (int i = 0; i < 114; ++i) {
        memset(&g_aObj[i], 0, sizeof(g_aObj[i]));
        g_apObj[i] = &g_aObj[i];
    }
    for (int i = 0; i < 3; ++i) {
        memset(&g_aSol[i], 0, sizeof(g_aSol[i]));
        g_apSol[i] = &g_aSol[i];
        g_aSol[i].nKind = 0;
    }
    g_aSol[2].nKind = 1;
    for (int i = 0; i < 2; ++i) {
        memset(&g_aShip[i], 0, sizeof(g_aShip[i]));
        g_apShip[i] = &g_aShip[i];
    }
    g_aShip[0].bHomeBase = 0;
    g_aShip[0].bCraftType = 0;
    g_aShip[1].bHomeBase = 1;
    g_aShip[1].bCraftType = 2;
    g_aObj[112].auPerBaseVal[0] = 3;
    g_aObj[113].auPerBaseVal[0] = 2;
    memset(g_aSite, 0, sizeof(g_aSite));
    g_aSite[0].bFlag1 = 0;
    g_aSite[0].auItemQty[224] = 1;
    g_aSite[0].bRecoveredSoldierAssign = 2;
    g_aSite[0].auItemQty[226] = 3;
    g_aSite[1].bFlag1 = 1;
    g_aSite[1].auItemQty[224] = 4;
    g_aSite[1].bRecoveredSoldierAssign = 1;
    g_aSite[1].auItemQty[226] = 2;

    CheckEqI32(Base_CountSoldiers(g_apSol, 3, 0), 2, "two soldiers are assigned to base 0");
    CheckEqI32(Base_CountSoldiers(g_apSol, 3, 1), 1, "and one to base 1");

    /* An EMPTY facility cell is 255, not zero. That matters here because the upkeep term charges
     * every cell whose type byte is NOT 255, so a zeroed grid looks like 36 built facilities.
     * Fill both bases' type blocks with 255 first. */
    for (int i = 480; i < (int)sizeof(g_abGrid); ++i) {
        g_abGrid[i] = 255;
    }

    /* Base 0: one type-0 craft (500000) + 2 soldiers (40000) + 3 of object 112 at 20000 (60000)
     * + 2 of object 113 at 30000 (60000) + one site (20000*3 + 30000*3 = 150000)
     * + two facility cells at 50000 (100000). */
    P_T(0, 0) = 8;
    P_T(1, 1) = 3;
    CheckEqI32(Base_CalcMonthlyUpkeep(g_apShip, 2, g_apSol, 3, g_aSite, 2, g_apObj, g_abGrid, 0),
               500000 + 40000 + 60000 + 60000 + 150000 + 100000,
               "every term of the monthly cost, added");

    /* A cell under construction is charged the same as a finished one: this walk reads the type
     * grid and never looks at build days. */
    P_O(0, 0) = 9;
    CheckEqI32(Base_CalcMonthlyUpkeep(g_apShip, 2, g_apSol, 3, g_aSite, 2, g_apObj, g_abGrid, 0),
               500000 + 40000 + 60000 + 60000 + 150000 + 100000,
               "an unfinished facility is charged the full 50000");

    /* An empty cell (type 255) is free. */
    P_T(0, 0) = 255;
    CheckEqI32(Base_CalcMonthlyUpkeep(g_apShip, 2, g_apSol, 3, g_aSite, 2, g_apObj, g_abGrid, 0),
               500000 + 40000 + 60000 + 60000 + 150000 + 50000,
               "a cell whose type byte is 255 contributes nothing");

    /* Base 1 has a type-2 craft (700000), one soldier, no stock at this base, its own site, and
     * its own 36 facility cells, which are all zero here. */
    CheckEqI32(Base_CalcMonthlyUpkeep(g_apShip, 2, g_apSol, 3, g_aSite, 2, g_apObj, g_abGrid, 1),
               700000 + 20000 + (20000 * 5 + 30000 * 2),
               "base 1 is charged its own craft, soldier and site, and no facility cells");

    /* A zeroed type block is 36 built facilities to this function. */
    for (int i = 480; i < 480 + 144; ++i) {
        g_abGrid[i] = 0;
    }
    CheckEqI32(Base_CalcMonthlyUpkeep(g_apShip, 2, g_apSol, 3, g_aSite, 2, g_apObj, g_abGrid, 0),
               500000 + 40000 + 60000 + 60000 + 150000 + 36 * 50000,
               "a block of zeros is 36 facilities, because empty is 255 and not 0");

#undef P_T
#undef P_O
}

/* The columns are 200 entries per base, and the port shifts them flat, so the fixture needs
 * room for a column past the last base. */
static u16 g_apManufCol[200 * 4];
static u16 g_apProductC[200 * 4];
static i32 g_apProductB[200 * 4];
static u16 g_apResearch[144 * 4];
static u8 g_abDemoGrid[1524];
static BaseSiteRec g_aSiteRec[3];
static ObjectRecFull g_aDemoObj[1];
static ObjectRecFull *g_apDemoObj[1];
static SmapSite g_aDemoSite[2];
static u8 g_abDemoHangar[256];

static void TestDemolishBase(void)
{
    static Ship *apCraft[2];
    static Soldier *apSoldier[3];
    static BaseRec *apBase[3];
    static void *apStrateg[2];
    Ship *aShip[2];
    Soldier *aSoldier[3];
    BaseRec *aBase[3];
    StrategItem *aItem[2];
    static PtrArray list;
    static GeoscapeWorld world;
    static StrategSpawnCtx ctx;
    static i32 nSeed;
    ShipRemovalEnv shipEnv;
    BaseDemolishEnv env;
    u8 nShipCount = 2;
    u8 nBaseCount = 3;
    u16 nSoldierCount = 3;
    u16 nObjectCount = 1;
    u16 nResearchEntries = 1;
    u8 once = 0;

    printf("Base_DemolishFacility\n");

    for (int i = 0; i < 2; ++i) {
        aShip[i] = (Ship *)calloc(1, sizeof(Ship));
        aShip[i]->bId = (u8)(7 + i);
        aShip[i]->bHomeBase = (u8)(1 + i); /* both homed above the base being removed */
        apCraft[i] = aShip[i];
    }
    aShip[1]->nSlotIdx = 1;

    for (int i = 0; i < 3; ++i) {
        aSoldier[i] = (Soldier *)calloc(1, sizeof(Soldier));
        aSoldier[i]->nUnk440 = 100 + i;
        aSoldier[i]->bUnk183 = 255; /* not aboard anything */
        apSoldier[i] = aSoldier[i];
    }
    aSoldier[0]->nKind = 1; /* stationed at the doomed base, deleted */
    aSoldier[1]->nKind = 2; /* above it, renumbered down */
    aSoldier[2]->nKind = 1;
    aSoldier[2]->bUnk183 = 1; /* aboard craft 1, so not deleted by the base pass */

    /* Everything the teardown deletes has to be a heap object: the original operator-deletes
     * apBases[newCount-1] and the port frees it. */
    for (int i = 0; i < 3; ++i) {
        aBase[i] = (BaseRec *)calloc(1, sizeof(BaseRec));
        aBase[i]->bSlot = (u8)i;
        aBase[i]->nStoresTotal = 1000 + i;
        apBase[i] = aBase[i];
    }

    g_aSiteRec[0].nFacType = 10;
    g_aSiteRec[1].nFacType = 11; /* the one asked for */
    g_aSiteRec[2].nFacType = 12;

    aItem[0] = (StrategItem *)calloc(1, sizeof(StrategItem));
    aItem[1] = (StrategItem *)calloc(1, sizeof(StrategItem));
    aItem[0]->nType = 0;
    aItem[0]->bActive = 0;
    aItem[0]->nSiteId = 11; /* found by SITE ID, not by facility type */
    aItem[1]->nType = 1;
    aItem[1]->nDisplayId = 7;
    apStrateg[0] = aItem[0];
    apStrateg[1] = aItem[1];
    list.nCount = 2;
    list.apItems = (void **)calloc(2, sizeof(void *));
    list.apItems[0] = aItem[0];
    list.apItems[1] = aItem[1];

    g_apDemoObj[0] = &g_aDemoObj[0];
    g_aDemoObj[0].auPerBaseVal[1] = 7;
    g_aDemoObj[0].auPerBaseVal[2] = 9;

    g_aDemoSite[0].bFlag1 = 11;
    g_aDemoSite[1].bFlag1 = 12;

    memset(g_abDemoGrid, 255, sizeof(g_abDemoGrid));
    memset(g_abDemoGrid, 0, 36 * 8);
    g_abDemoGrid[480 + 144 * 1] = 0xA1; /* facType cell (0,0) of base 1 */
    g_abDemoGrid[480 + 144 * 2] = 0xA2;
    g_abDemoGrid[36 * 1] = 5; /* buildDays cell (0,0) of base 1 */
    g_abDemoGrid[36 * 2] = 9;

    g_apManufCol[200 * 1] = 111;
    g_apManufCol[200 * 2] = 222;
    g_apManufCol[200 * 3] = 333;
    g_apProductC[200 * 1] = 1111;
    g_apProductC[200 * 2] = 2222;
    g_apProductB[200 * 1] = 11111;
    g_apProductB[200 * 2] = 22222;
    g_apResearch[144 * 1] = 55;
    g_apResearch[144 * 2] = 66;

    memset(&world, 0, sizeof(world));
    world.apCraft = apCraft;
    world.nShipCount = 2;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnSeed = &nSeed;

    memset(&shipEnv, 0, sizeof(shipEnv));
    shipEnv.apCraft = apCraft;
    shipEnv.pnShipCount = &nShipCount;
    shipEnv.apSoldiers = apSoldier;
    shipEnv.pnSoldierCount = &nSoldierCount;
    shipEnv.apObjects = g_apDemoObj;
    shipEnv.nObjectCount = nObjectCount;
    shipEnv.pStrategList = &list;
    shipEnv.pWorld = &world;
    shipEnv.pnSpawnOnceFlag = &once;
    shipEnv.pSpawnCtx = &ctx;
    shipEnv.abHangarFlag = g_abDemoHangar;
    shipEnv.nHangarFlagCap = (i32)sizeof(g_abDemoHangar);

    memset(&env, 0, sizeof(env));
    env.apCraft = apCraft;
    env.pnShipCount = &nShipCount;
    env.apSoldiers = apSoldier;
    env.pnSoldierCount = &nSoldierCount;
    env.apBases = apBase;
    env.apObjects = g_apDemoObj;
    env.pnObjectCount = &nObjectCount;
    env.abFacilityGrid = g_abDemoGrid;
    env.apSiteRecs = g_aSiteRec;
    env.apManufCols = g_apManufCol;
    env.apProductC = g_apProductC;
    env.apProductB = g_apProductB;
    env.apResearchCols = g_apResearch;
    env.pnResearchEntries = &nResearchEntries;
    env.apSites = g_aDemoSite;
    env.nSiteCount = 2;
    env.pbBaseCount = &nBaseCount;
    env.pShipEnv = &shipEnv;

    CheckEqI32(Base_DemolishFacility(&env, 11), 2, "it returns the new base count");

    CheckEqI32(nShipCount, 1, "the craft homed here is gone");
    CheckEqI32(apCraft[0]->bId, 8, "and the array compacted over it");
    CheckEqI32(apCraft[0]->bHomeBase, 1, "its home base renumbered down from 2");

    CheckEqI32(nSoldierCount, 2, "the soldier stationed here is deleted");
    CheckEqI32(apSoldier[0]->nUnk440, 101, "the roster compacted over the deletion");
    CheckEqI32(apSoldier[0]->nKind, 1, "and the soldier from base 2 is now at base 1");
    CheckEqI32(apSoldier[1]->bUnk183, 0,
               "the craft index the other soldier was aboard renumbered with the craft array");

    CheckEqI32(list.nCount, 0,
               "both strateg entries go: the base pass drops the site entry and the craft pass "
               "drops the craft's");

    CheckEqI32(g_aDemoSite[0].bFlag0, 255, "the site owned by this base is marked unassigned");
    CheckEqI32(g_aDemoSite[1].bFlag0, 0, "and the other site is left alone");

    CheckEqI32(g_abDemoGrid[480 + 144 * 1], 0xA2, "the facility block moves down one base");
    CheckEqI32(g_abDemoGrid[36 * 1], 9, "and the build days move with it");

    CheckEqI32(g_aDemoObj[0].auPerBaseVal[1], 9, "every object's per-base column shifts left");
    CheckEqI32(g_apProductC[200 * 1], 2222, "product C shifts left");
    CheckEqI32(g_apProductB[200 * 1], 22222, "product B shifts left");
    CheckEqI32(g_apResearch[144 * 1], 66, "the research column shifts left");

    CheckEqI32(g_apManufCol[200 * 1], 111,
               "but the manufacturing column does NOT: it starts one base later");
    CheckEqI32(g_apManufCol[200 * 2], 333, "so base 2 takes base 3's value and base 1 keeps its own");

    CheckEqI32(g_aSiteRec[1].nFacType, 12, "the facility records compact too");
    CheckEqI32(apBase[1]->nStoresTotal, 1002, "the BaseRec records are moved through the pointers");
    CheckEqI32(apBase[1]->bSlot, 1, "and the slots are renumbered");
    CheckEqI32(apBase[2] == NULL ? 1 : 0, 1, "the last apBases slot is freed and cleared");

    /* A type no base has: the search lands on the count, the compaction guard fails, and the
     * function still drops the count and deletes the last base. */
    CheckEqI32(Base_DemolishFacility(&env, 99), 1,
               "a type no base has still removes the LAST base - the original has no found test");
    CheckEqI32(g_aSiteRec[0].nFacType, 10, "and nothing was compacted, so base 0 is untouched");

    /* The removals free what they remove, and a test that frees it again corrupts the heap under
     * ctest. Everything here is static or intentionally left allocated. */
}

/* --- the sixth needed pass and the two object-column readers -------------------------
 *
 * Base_CountAmmoItems @ 0x656ac, Base_UpdateAlienContainmentNeeded @ 0x65f04,
 * Base_CalcOccupiedSpace @ 0x655f8. */

static ObjectRecFull s_aSpaceObj[120];
static ObjectRecFull *s_apSpaceObj[120];
static Soldier       s_aSpaceSoldier[4];
static Soldier      *s_apSpaceSoldier[4];

static void SpaceFixture(void)
{
    for (i32 i = 0; i < 120; ++i) {
        memset(&s_aSpaceObj[i], 0, sizeof(s_aSpaceObj[i]));
        s_apSpaceObj[i] = &s_aSpaceObj[i];
    }
    for (i32 i = 0; i < 4; ++i) {
        memset(&s_aSpaceSoldier[i], 0, sizeof(s_aSpaceSoldier[i]));
        s_apSpaceSoldier[i] = &s_aSpaceSoldier[i];
    }
}

static void TestContainmentNeededAndSpace(void)
{
    static u8 grid[700];

    printf("Base_UpdateAlienContainmentNeeded @ 0x65f04, Base_CountAmmoItems @ 0x656ac, "
           "Base_CalcOccupiedSpace @ 0x655f8\n");

#define N_T(c, r) grid[480 + 6 * (c) + (r)]
#define N_N(c, r) grid[444 + 6 * (c) + (r)]

    /* Three containment cells, 25 ammo: budget is 25 / 10 + 1 = 3, so all three are claimed. */
    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 7;
    N_T(1, 1) = 7;
    N_T(2, 2) = 7;
    CheckEqI32(Base_UpdateAlienContainmentNeeded(grid, 0, 25), 6,
               "the return is loop-counter residue, always 6");
    CheckEqI32(N_N(0, 0), 1, "first containment cell claimed");
    CheckEqI32(N_N(1, 1), 1, "second containment cell claimed");
    CheckEqI32(N_N(2, 2), 1, "third containment cell claimed");

    /* 5 ammo still gives a budget of 1, because the formula is n / 10 + 1 and not n / 10. */
    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 7;
    N_T(1, 1) = 7;
    Base_UpdateAlienContainmentNeeded(grid, 0, 5);
    CheckEqI32(N_N(0, 0), 1, "five ammo still claims one cell");
    CheckEqI32(N_N(1, 1), 0, "and no more");

    /* No ammo at all is the special case: the budget is 0, not 1. */
    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 7;
    Base_UpdateAlienContainmentNeeded(grid, 0, 0);
    CheckEqI32(N_N(0, 0), 0, "zero ammo claims nothing");

    /* Cells that are not containment are left exactly as they were, including a stale value. */
    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 7;
    N_T(3, 3) = 6;
    N_N(3, 3) = 99;
    Base_UpdateAlienContainmentNeeded(grid, 0, 100);
    CheckEqI32(N_N(0, 0), 1, "the containment cell is claimed");
    CheckEqI32(N_N(3, 3), 99, "a stores cell keeps whatever the stores pass left there");

    /* Base index 1 reads its own block, not base 0's. */
    memset(grid, 0, sizeof(grid));
    N_T(0, 0) = 7;
    grid[480 + 144 + 6 * 0 + 0] = 7;
    Base_UpdateAlienContainmentNeeded(grid, 1, 100);
    CheckEqI32(N_N(0, 0), 0, "base 0 is untouched when base 1 is updated");
    CheckEqI32(grid[444 + 144 + 6 * 0 + 0], 1, "base 1 claims its own cell");

    /* Base_CountAmmoItems: 77..104 inclusive, plus object 114. The bounds are the interesting part. */
    SpaceFixture();
    s_aSpaceObj[76].auPerBaseVal[0] = 1000;
    s_aSpaceObj[77].auPerBaseVal[0] = 10;
    s_aSpaceObj[104].auPerBaseVal[0] = 20;
    s_aSpaceObj[105].auPerBaseVal[0] = 1000;
    s_aSpaceObj[113].auPerBaseVal[0] = 1000;
    s_aSpaceObj[114].auPerBaseVal[0] = 5;
    CheckEqI32(Base_CountAmmoItems(s_apSpaceObj, 120, 0), 35,
               "77 and 104 and 114 count, 76 and 105 and 113 do not");

    /* Only object 77 has a base 3 column set, so the answer is 40 and not 45: object 114's base 3
     * entry was never written. */
    s_aSpaceObj[77].auPerBaseVal[3] = 40;
    CheckEqI32(Base_CountAmmoItems(s_apSpaceObj, 120, 3), 40, "base 3 reads its own column");

    /* Base_CalcOccupiedSpace: soldiers assigned to the base, plus objects 112 and 113. */
    SpaceFixture();
    s_aSpaceSoldier[0].nKind = 2;
    s_aSpaceSoldier[1].nKind = 2;
    s_aSpaceSoldier[2].nKind = 1;
    s_aSpaceSoldier[3].nKind = 2;
    s_aSpaceObj[112].auPerBaseVal[2] = 7;
    s_aSpaceObj[113].auPerBaseVal[2] = 9;
    s_aSpaceObj[111].auPerBaseVal[2] = 500;
    s_aSpaceObj[114].auPerBaseVal[2] = 500;
    CheckEqI32(Base_CalcOccupiedSpace(s_apSpaceSoldier, 4, s_apSpaceObj, 120, 2), 19,
               "three soldiers plus 7 and 9 from objects 112 and 113");

    /* The base index is a char in the original, so a negative one matches no nKind, which is u8. */
    CheckEqI32(Base_CalcOccupiedSpace(s_apSpaceSoldier, 4, s_apSpaceObj, 120, -1), 0,
               "a negative base index matches no soldier");

#undef N_T
#undef N_N
}

static void TestFacilityPlaceAndGrant(void)
{
    static u8 grid[1524];
    static BaseRec base;
    static ObjectRecFull objs[123];
    static ObjectRecFull *apObjs[123];

    printf("BaseFac_SetCellBuildState / Base_GrantStarterItems\n");

    /* fac[] starts at grid + 0x174. Within base b the four layers are tileA at +0, tileB at +36,
     * layer2 at +72 and facType at +108, each indexed 6*row + col where row is the ARM's nY.
     * buildDays is its own array at grid + 36*b. */
#define FAC(l, b, y, x) grid[0x174 + 144 * (b) + (l) + 6 * (y) + (x)]
#define DAYS(b, y, x)   grid[36 * (b) + 6 * (y) + (x)]

    memset(grid, 0, sizeof(grid));
    /* Spec record 1 is tileA 0x01, tileB 0x12, 16 build days. */
    CheckEqI32(BaseFac_SetCellBuildState(grid, 0, 1, 2, 3, 0), 6 * (24 * 0 + 3 + 18),
               "the return is the facType address delta, not a status");
    CheckEqI32(FAC(0, 0, 3, 2), 0x01, "tileA gets the spec's first byte");
    CheckEqI32(FAC(36, 0, 3, 2), 0x12, "tileB gets the spec's second byte");
    CheckEqI32(DAYS(0, 3, 2), 16, "buildDays gets the spec's third byte");
    CheckEqI32(FAC(108, 0, 3, 2), 1, "the shared tail writes facType in the anchor cell");
    CheckEqI32(FAC(0, 0, 3, 3), 0, "the neighbouring tileA cell is untouched");
    CheckEqI32(FAC(72, 0, 3, 2), 0, "layer2 is never written");
    CheckEqI32(FAC(108, 0, 3, 3), 0, "and neither is the neighbouring facType cell");

    memset(grid, 0, sizeof(grid));
    (void)BaseFac_SetCellBuildState(grid, 0, 1, 2, 3, 1);
    CheckEqI32(DAYS(0, 3, 2), 0, "bClear writes zero build days instead of the spec value");
    CheckEqI32(FAC(0, 0, 3, 2), 0x01, "but the tile bytes still come from the spec");

    memset(grid, 0, sizeof(grid));
    (void)BaseFac_SetCellBuildState(grid, 2, 4, 1, 5, 0);
    CheckEqI32(FAC(0, 2, 5, 1), 0x02, "base 2 lands 144 bytes further on");
    CheckEqI32(DAYS(2, 5, 1), 12, "and its buildDays row is 36 bytes further on");
    CheckEqI32(DAYS(1, 5, 1), 0, "the previous base's row is left alone");

    /* Type 11 is the hangar: a 2 by 2 footprint, spec days 25. */
    memset(grid, 0, sizeof(grid));
    (void)BaseFac_SetCellBuildState(grid, 1, 11, 1, 2, 0);
    CheckEqI32(FAC(0, 1, 2, 1), 9,  "hangar quadrant at (nY,nX)");
    CheckEqI32(FAC(0, 1, 2, 2), 10, "hangar quadrant at (nY,nX+1)");
    CheckEqI32(FAC(0, 1, 3, 1), 11, "hangar quadrant at (nY+1,nX)");
    CheckEqI32(FAC(0, 1, 3, 2), 12, "hangar quadrant at the diagonal");
    CheckEqI32(DAYS(1, 2, 1), 25, "buildDays at the anchor");
    CheckEqI32(DAYS(1, 2, 2), 25, "and one column right");
    CheckEqI32(DAYS(1, 3, 2), 25, "and at the diagonal");
    CheckEqI32(DAYS(1, 3, 1), 0,
               "but NOT at (nY+1,nX): the ARM reaches the three as +0, +1 and +7");
    CheckEqI32(FAC(36, 1, 2, 1), 0, "a hangar writes no tileB at all");
    CheckEqI32(FAC(108, 1, 2, 1), 11, "facType is set in the anchor cell only");
    CheckEqI32(FAC(108, 1, 2, 2), 0, "so the other three quadrants have no facType byte");
    CheckEqI32(FAC(108, 1, 3, 1), 0, "which is why a hangar counts once, not four times");

    memset(grid, 0, sizeof(grid));
    CheckEqI32(BaseFac_SetCellBuildState(grid, 0, 18, 0, 0, 0), 0,
               "a facility type past the 18 spec records is refused");
    CheckEqI32(grid[0x174], 0, "and nothing is written");
    (void)BaseFac_SetCellBuildState(grid, 0, 1, 6, 0, 0);
    CheckEqI32(grid[0x174], 0, "a column of 6 is refused too");
    (void)BaseFac_SetCellBuildState(grid, 8, 1, 0, 0, 0);
    CheckEqI32(grid[0x174 + 144 * 8 - 1], 0, "and so is base index 8");

    memset(objs, 0, sizeof(objs));
    for (i32 i = 0; i < 123; ++i) {
        apObjs[i] = &objs[i];
    }
    memset(&base, 0, sizeof(base));
    base.bSlot = 3;

    Base_GrantStarterItems(&base, apObjs, 0);
    CheckEqI32(objs[105].auPerBaseVal[3], 0, "with the grant flag clear nothing is added");
    CheckEqI32(base.nStoresTotal, 0, "and the base total does not move");

    Base_GrantStarterItems(&base, apObjs, 1);
    CheckEqI32(objs[105].auPerBaseVal[3], 10, "object 105 gets 10");
    CheckEqI32(objs[34].auPerBaseVal[3], 2, "object 34 gets 2");
    CheckEqI32(objs[31].auPerBaseVal[3], 1, "object 31 gets 1");
    CheckEqI32(objs[56].auPerBaseVal[3], 33, "object 56 gets 33");
    CheckEqI32(objs[35].auPerBaseVal[3], 0, "no other object is touched");
    CheckEqI32(base.nStoresTotal, 46, "the base total is the sum of the four deltas");

#undef FAC
#undef DAYS
}

static void TestSpeedButtons(void)
{
    GeoscapeWorld world;
    UiControl ctl[6];
    UiControl *apCtl[6];
    i32 i;
    i32 nLit;

    memset(&world, 0, sizeof(world));
    memset(ctl, 0, sizeof(ctl));
    for (i = 0; i < 6; ++i) {
        apCtl[i] = &ctl[i];
    }

    printf("\nGameTime_Set5Sec..SetSpeedIdx5 @ 0x84f00..0x85158\n");

    GameTime_Set5Sec(&world, apCtl);
    CheckEqI32(world.nTickSeconds, 5, "Set5Sec stores 5 seconds");
    CheckEqI32(ctl[0].nState, 1, "and lights control 0");
    CheckEqI32(ctl[5].nState, 0, "control 5 stays dark");

    GameTime_Set1Min(&world, apCtl);
    CheckEqI32(world.nTickSeconds, 60, "Set1Min stores 60");
    CheckEqI32(ctl[1].nState, 1, "lights control 1");
    CheckEqI32(ctl[0].nState, 0, "and darkens control 0");

    GameTime_Set5Min(&world, apCtl);
    CheckEqI32(world.nTickSeconds, 300, "Set5Min stores 300");
    GameTime_SetSpeedIdx3(&world, apCtl);
    CheckEqI32(world.nTickSeconds, 1380, "SetSpeedIdx3 stores 1380");
    GameTime_SetSpeedIdx4(&world, apCtl);
    CheckEqI32(world.nTickSeconds, 2460, "SetSpeedIdx4 stores 2460");

    GameTime_SetSpeedIdx5(&world, apCtl);
    CheckEqI32(world.nTickSeconds, 3600, "SetSpeedIdx5 stores 3600");
    CheckEqI32(ctl[5].nState, 1, "and lights control 5, the only one it ever lights");
    CheckEqI32(ctl[4].nState, 0, "control 4 goes dark");

    nLit = 0;
    for (i = 0; i < 6; ++i) {
        nLit += ctl[i].nState != 0;
    }
    CheckEqI32(nLit, 1, "exactly one control is lit after a set");
}

static void TestCreateNamedEntry(void)
{
    static BaseRec *apBases[8];
    static ObjectRecFull objs[3];
    static ObjectRecFull *apObjs[3];
    static BaseSiteRec sites[8];
    static u32 prodU32[1600 + 3];
    static u16 prodU16[2176 + 3];
    static u16 research[3];
    static const u8 kindTable[8] = { 3, 9, 4, 0, 0, 0, 0, 0 };
    static PtrArray list;
    BaseCreateEnv env;
    StrategItem *pItem;
    i8 bBaseCount = 0;
    i32 seed = 5;
    i32 siteId = 500;
    i32 i;
    i32 nRet;

    memset(apBases, 0, sizeof(apBases));
    memset(objs, 0, sizeof(objs));
    memset(sites, 0, sizeof(sites));
    for (i = 0; i < 3; ++i) {
        apObjs[i] = &objs[i];
        objs[i].auPerBaseVal[0] = 7;
        objs[i].auPerBaseVal[1] = 7;
    }
    for (i = 0; i < 1600 + 3; ++i) {
        prodU32[i] = 0xAAAAAAAAu;
    }
    for (i = 0; i < 2176 + 3; ++i) {
        prodU16[i] = 0xAAAA;
    }
    for (i = 0; i < 3; ++i) {
        research[i] = 0xAAAA;
    }
    PtrArray_ctor(&list);

    env.apBases = apBases;
    env.apObjects = apObjs;
    env.nObjectCount = 3;
    env.pbBaseCount = &bBaseCount;
    env.pStrategList = &list;
    env.pnSeed = &seed;
    env.nZoomMode = 0;
    env.pnNextSiteId = &siteId;
    env.apSiteRecs = sites;
    env.abKindTable = kindTable;
    env.pProductU32Col = prodU32;
    env.pProductU16Col = prodU16;
    env.pResearchCol = research;

    printf("\nBase_CreateNamedEntry @ 0x66930\n");

    nRet = Base_CreateNamedEntry(&env, -6000, -6000, 1, "alpha");

    CheckEqI32(nRet, 1560, "the return is the 0x618 residue");
    CheckEqI32(bBaseCount, 1, "the base count is raised");
    CheckEqI32(siteId, 501, "and the next site id");

    Check(apBases[0] != NULL, "BaseRec_Create stored a record at slot 0");
    CheckEqI32(apBases[0]->bUnk17, 1, "bUnk17 = the kind");
    CheckEqI32(apBases[0]->nUnk8, -6000, "nUnk8 = x");
    CheckEqI32(apBases[0]->nUnk12, -6000, "nUnk12 = y");
    CheckEqI32(apBases[0]->bUnk16, 8, "bUnk16 = kindTable[1] - 1");
    CheckEqI32(objs[0].auPerBaseVal[0], 0, "the ctor zeroed slot 0 in every object");
    CheckEqI32(objs[0].auPerBaseVal[1], 7, "and left the other slots alone");

    CheckEqI32(list.nCount, 1, "one strateg entry was added");
    pItem = (StrategItem *)PtrArray_Get(&list, 0);
    CheckEqI32(pItem->nType, 0, "type 0");
    CheckEqI32(pItem->nX, -6000, "at x");
    CheckEqI32(pItem->nSiteId, 500, "with the site id (type 0 writes nSiteId)");
    CheckEqI32(pItem->nDisplayId, -1, "and nDisplayId -1");
    CheckEqI32(pItem->bRandA, 0x81, "bRandA = 1 - 0x80 as a byte");
    CheckEqI32(pItem->bVisible, 1, "visible");
    CheckEqI32(pItem->nSeed, 5, "seeded from the counter");
    CheckEqI32(seed, 6, "and the counter advanced");

    CheckEqI32(sites[0].bType, 0x81, "site bType = kind - 0x80");
    Check(strcmp(sites[0].abName, "alpha") == 0, "the name landed in abName");
    CheckEqI32(sites[0].nFacType, 500, "nFacType = the site id");
    CheckEqI32(sites[0].nUnk48, 0, "the three scratch dwords are cleared");

    CheckEqI32((i32)research[0], 0, "research column cleared");
    CheckEqI32((i32)prodU16[2], 0, "product u16 column cleared");
    CheckEqI32((i32)prodU16[2178], 0, "and its +2176 twin");
    CheckEqI32((i32)prodU32[1602], 0, "product u32 column and its +1600 twin");
    Check(prodU32[3] == 0xAAAAAAAAu, "the entry past the object count is untouched");

    /* A 30-character name: the original would run past the 20-byte field; the port truncates
     * at 19 + NUL and the record's own fields stay clean. */
    (void)Base_CreateNamedEntry(&env, 100, 200, 0, "012345678901234567890123456789");
    CheckEqI32(bBaseCount, 2, "second base created");
    Check(strcmp(sites[1].abName, "0123456789012345678") == 0, "the long name is truncated to 19");
    CheckEqI32(sites[1].nUnk48, 0, "and nothing spilled into the fields below");
    CheckEqI32(sites[1].bType, 0x80, "kind 0 gives bType 0x80");
    CheckEqI32(apBases[1]->bUnk16, 2, "kindTable[0] - 1 for the second base");
}

static void TestBudgetHelpers(void)
{
    static SmapSite sites[4]; /* the fourth is the working slot at the site count */
    static BaseRec base0;
    static BaseRec *apBases[1];
    static ObjectRecFull objs[3];
    static ObjectRecFull *apObjs[3];
    static Soldier sols[3];
    static Soldier *apSols[3];
    i32 i;

    printf("\nBudget helpers: Base_CountOutboundTransferSpace @ 0x68044, "
           "Base_CountItemQtyAcrossBases @ 0x67d68, Base_CountPsiSoldiers @ 0x65a08\n");

    memset(sites, 0, sizeof(sites));
    memset(objs, 0, sizeof(objs));
    memset(sols, 0, sizeof(sols));
    sites[0].bFlag1 = 4;
    sites[0].uRecoveredSoldiers = 3;
    sites[0].bRecoveredSoldierAssign = (u8)(i8)-2; /* signed in the sum */
    sites[0].auItemQty[112] = 7;
    sites[0].auItemQty[113] = 11;
    sites[0].auItemQty[1] = 5;
    sites[1].bFlag1 = 4;
    sites[1].uRecoveredSoldiers = 5;
    sites[1].bRecoveredSoldierAssign = 1;
    sites[1].auItemQty[112] = 1;
    sites[1].auItemQty[113] = 2;
    sites[1].auItemQty[1] = 4;
    sites[2].bFlag1 = 9; /* a different base type: never scanned */
    sites[2].auItemQty[1] = 99;
    sites[2].uRecoveredSoldiers = 50;

    base0.bUnk4 = 4;
    apBases[0] = &base0;
    for (i = 0; i < 3; ++i) {
        apObjs[i] = &objs[i];
    }
    objs[1].bCargoSize = 2;
    objs[2].bCargoSize = 3;

    /* Own site: 3 + (-2) + 11 + 7 = 19; scanned site 1: 5 + 1 + 2 + 1 = 9; site 2 has the
     * wrong bFlag1 and site 0 is the own site, skipped by the i != a2 test. */
    CheckEqI32(Base_CountOutboundTransferSpace(sites, 3, 0, apBases, 0, true), 28,
               "outbound transfer: own load plus matching-type sites");
    /* With nSiteIdx -1 the own-site block is skipped but the scan's `i != a2` test matches
     * every index, so site 0 is scanned too: 19 + 9. */
    CheckEqI32(Base_CountOutboundTransferSpace(sites, 3, -1, apBases, 0, true), 28,
               "with nSiteIdx -1 the scan covers every matching site including 0");
    CheckEqI32(Base_CountOutboundTransferSpace(sites, 3, 0, apBases, 0, false), 19,
               "and without the scan only the own site counts");

    /* Item qty across bases: block 1 = 2*5 = 10 (column 2 is zero at the own site);
     * block 2 = 2*4 = 8 (gate column 1 non-zero at site 0, contribution from site 1). */
    CheckEqI32(Base_CountItemQtyAcrossBases(sites, 3, 0, apBases, 0, apObjs, 3, true), 18,
               "item qty across bases: own cargo plus matching-site cargo");
    CheckEqI32(Base_CountItemQtyAcrossBases(sites, 3, 0, apBases, 0, apObjs, 3, false), 10,
               "without the scan only the own site");
    CheckEqI32(Base_CountItemQtyAcrossBases(sites, 3, -1, apBases, 0, apObjs, 3, true), 0,
               "the needed-pass call (nSiteIdx -1) gates on zero and adds nothing");

    /* The other call shape: five of the ten Base_CountItemQtyAcrossBases sites and both extra
     * Base_CountOutboundTransferSpace sites pass the site COUNT as the site index, because that is
     * the record the transfer flow is working on (0x750B0, 0x75CC0, 0x9641C, 0x9692C, 0x97D30, and
     * 0x75E34 / 0x9666C). Indexing it against the count instead of the array drops the whole term. */
    sites[3].uRecoveredSoldiers = 6;
    sites[3].auItemQty[112] = 1;
    sites[3].auItemQty[113] = 2;
    sites[3].auItemQty[1] = 8;
    CheckEqI32(Base_CountOutboundTransferSpace(sites, 3, 3, apBases, 0, false), 9,
               "nSiteIdx == nSiteCount is the working record, and it counts (0x75E34)");
    CheckEqI32(Base_CountItemQtyAcrossBases(sites, 3, 3, apBases, 0, apObjs, 3, false), 16,
               "the same shape on the cargo side: 2 * 8 (0x75CC0)");
    CheckEqI32(Base_CountItemQtyAcrossBases(sites, 3, 3, apBases, 0, apObjs, 3, true), 34,
               "with the scan the working record gates sites 0 and 1: 16 + 10 + 8");
    CheckEqI32(Base_CountItemQtyAcrossBases(sites, 3, PUFO_SMAP_SITE_COUNT, apBases, 0, apObjs, 3,
                                            true),
               0, "Deviation: an index at the array end is refused, gate and scan both skipped");

    sols[0].nKind = 0; sols[0].bUnk149 = 1;
    sols[1].nKind = 0; sols[1].bUnk149 = 0; /* assigned but not psi */
    sols[2].nKind = 1; sols[2].bUnk149 = 1; /* psi at another base */
    for (i = 0; i < 3; ++i) {
        apSols[i] = &sols[i];
    }
    CheckEqI32(Base_CountPsiSoldiers(apSols, 3, 0), 1, "psi soldiers at base 0");
    CheckEqI32(Base_CountPsiSoldiers(apSols, 3, 1), 1, "and at base 1");
    CheckEqI32(Base_CountPsiSoldiers(apSols, 3, 2), 0, "none at base 2");
}

/* Base_CanPlaceHangarAt @ 0x629b0: the scratch map, the flood, and the two tails. */
static void TestHangarPlacement(void)
{
    static u8 grid[1524];
    static u8 abGridBytes[8];
    static Ship aShips[2];
    static Ship *apShips[2];
    static BaseRec aBase;
    static BaseRec *apBases[1];
    HangarPlaceEnv env;

    printf("Base_CanPlaceHangarAt\n");

#define FAC(l, b, y, x) grid[0x174 + 144 * (b) + (l) + 6 * (y) + (x)]
#define DAYS(b, y, x)   grid[36 * (b) + 6 * (y) + (x)]
#define RESET()                                                      \
    do {                                                            \
        memset(grid, 0, sizeof(grid));                              \
        memset(abGridBytes, 0, sizeof(abGridBytes));                \
        memset(aShips, 0, sizeof(aShips));                          \
        memset(&aBase, 0, sizeof(aBase));                           \
        aBase.bUnk4 = 3;                                            \
        aShips[0].bHomeBase = 255;                                  \
        aShips[1].bHomeBase = 255; /* homed nowhere: the count starts at zero */ \
        apShips[0] = &aShips[0];                                    \
        apShips[1] = &aShips[1];                                    \
        apBases[0] = &aBase;                                        \
        env.abFacilityGrid = grid;                                  \
        env.abGridBytes = abGridBytes;                              \
        env.nSelectedBaseIdx = 0;                                   \
        env.apCraft = apShips;                                      \
        env.nCraftCount = 2;                                        \
        env.apBases = apBases;                                      \
        env.nBaseCount = 1;                                         \
    } while (0)

    /* 1. an anchor that is still under construction is accepted outright (0x62A30 / 0x62A50 both
     * jump to the MOV R0,#1 tail at 0x62D44), and none of the connectivity work runs. */
    RESET();
    DAYS(0, 2, 2) = 7;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 1,
               "a cell with build days left is accepted without any other test");
    RESET();
    DAYS(0, 2, 2) = 7;
    FAC(108, 0, 2, 2) = 11;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 1,
               "the same through the facType 11 early test");
    RESET();
    DAYS(0, 2, 2) = 7;
    FAC(0, 0, 0, 0) = 1;  FAC(108, 0, 0, 0) = 1;   /* a facility on its own at the far corner */
    FAC(0, 0, 5, 5) = 1;  FAC(108, 0, 5, 5) = 1;   /* and another one, disconnected */
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 1,
               "even with two disconnected facilities: the flood never runs for an unfinished anchor");

    /* 2. the tail at 0x62D50..0x62D6C: layer2 first, then facType == 0. */
    RESET();
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 0, "empty ground cannot host a hangar");
    RESET();
    FAC(0, 0, 2, 2) = 1; /* a stamp, but no facType byte */
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 0,
               "a stamped cell whose facType is still 0 is refused too - facType decides, not the stamp");
    RESET();
    FAC(0, 0, 2, 2) = 1;
    FAC(108, 0, 2, 2) = 0xFF; /* the empty sentinel the grid reset writes */
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 1,
               "0xFF is not 0, so the sentinel cell passes the facType test");
    RESET();
    FAC(0, 0, 2, 2) = 1;  FAC(108, 0, 2, 2) = 1;
    FAC(72, 0, 2, 2) = 1; /* the needed-flag layer has claimed this cell */
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 0, "a cell layer2 has claimed is refused");
    RESET();
    FAC(0, 0, 2, 2) = 1;  FAC(108, 0, 2, 2) = 1;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 1, "a plain facility cell is accepted");

    /* 3. the flood: every finished stamped cell has to be reachable from the first one. */
    RESET();
    FAC(0, 0, 0, 0) = 1;  FAC(108, 0, 0, 0) = 1;
    FAC(0, 0, 5, 5) = 1;  FAC(108, 0, 5, 5) = 1;
    FAC(0, 0, 2, 2) = 1;  FAC(108, 0, 2, 2) = 1;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 0,
               "a facility the flood never reaches means the placement is refused");
    RESET();
    FAC(0, 0, 0, 0) = 1;  FAC(108, 0, 0, 0) = 1;
    FAC(0, 0, 0, 1) = 1;  FAC(108, 0, 0, 1) = 1;
    FAC(0, 0, 2, 2) = 1;  FAC(108, 0, 2, 2) = 1;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 1, "cells connected to the seed are fine");
    RESET();
    for (i32 x = 0; x < 4; ++x) { FAC(0, 0, 0, x) = 1; FAC(108, 0, 0, x) = 1; }
    CheckEqI32(Base_CanPlaceHangarAt(&env, 1, 0), 0,
               "tapping the middle of a row of four cuts the far half off");
    RESET();
    for (i32 x = 0; x < 4; ++x) { FAC(0, 0, 0, x) = 1; FAC(108, 0, 0, x) = 1; }
    CheckEqI32(Base_CanPlaceHangarAt(&env, 0, 0), 1, "tapping the end of the row does not");
    RESET();
    for (i32 x = 0; x < 4; ++x) { FAC(0, 0, 0, x) = 1; FAC(108, 0, 0, x) = 1; }
    DAYS(0, 0, 2) = 5; /* under construction: scratch 0, so it blocks the flood */
    CheckEqI32(Base_CanPlaceHangarAt(&env, 0, 0), 0,
               "a cell that is still being built blocks the flood, so the far cells are cut off");

    /* 4. The hangar footprint marking (0x62AB8..0x62B10) is dead code, and this is the case that
     * shows it. The marking writes 1 into the three cells beyond the anchor, but the very same
     * loop visits those cells afterwards, in row-major order, and writes each one's own value
     * over the mark. So a hangar whose quadrants are not stamped marks nothing that survives. */
    RESET();
    FAC(0, 0, 0, 0) = 9;   /* only the anchor quadrant is stamped */
    FAC(108, 0, 0, 0) = 11;
    FAC(0, 0, 2, 0) = 1;   FAC(108, 0, 2, 0) = 1;
    FAC(0, 0, 4, 4) = 1;   FAC(108, 0, 4, 4) = 1;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 4, 4), 0,
               "the 2x2 marking does NOT link the block: the loop overwrites it at (nY+1,nX)");
    RESET();
    FAC(0, 0, 0, 0) = 9;   FAC(0, 0, 0, 1) = 10;
    FAC(0, 0, 1, 0) = 11;  FAC(0, 0, 1, 1) = 12;
    FAC(108, 0, 0, 0) = 11;
    FAC(0, 0, 2, 0) = 1;   FAC(108, 0, 2, 0) = 1;
    FAC(0, 0, 4, 4) = 1;   FAC(108, 0, 4, 4) = 1;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 4, 4), 1,
               "a fully stamped hangar links it instead - through its own four tileA bytes");

    /* 5. the hangar tail (0x62D14..0x62D40): capacity minus craft has to be positive. */
    RESET();
    FAC(0, 0, 2, 2) = 9;  FAC(0, 0, 2, 3) = 10; FAC(0, 0, 3, 2) = 11; FAC(0, 0, 3, 3) = 12;
    FAC(108, 0, 2, 2) = 11;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 1,
               "one hangar slot, no craft homed: the capacity test passes");
    aShips[0].bHomeBase = 0;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 0,
               "one hangar slot taken by a craft homed here: it fails");
    RESET();
    FAC(0, 0, 2, 2) = 9;  FAC(0, 0, 2, 3) = 10; FAC(0, 0, 3, 2) = 11; FAC(0, 0, 3, 3) = 12;
    FAC(108, 0, 2, 2) = 11;
    FAC(0, 0, 0, 0) = 9;  FAC(0, 0, 0, 1) = 10; FAC(0, 0, 1, 0) = 11; FAC(0, 0, 1, 1) = 12;
    FAC(108, 0, 0, 0) = 11;
    aShips[0].bHomeBase = 0;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 1,
               "two hangar slots and one craft still leaves one free");
    RESET();
    FAC(0, 0, 2, 2) = 9;  FAC(0, 0, 2, 3) = 10; FAC(0, 0, 3, 2) = 11; FAC(0, 0, 3, 3) = 12;
    FAC(108, 0, 2, 2) = 11;
    abGridBytes[0] = 1; /* the byte BaseGrid_GetByte0x654 adds for this base's type byte */
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 0,
               "the 0x159DBC grid byte is added to the craft total, and one slot is not enough");

    /* 6. the selected base, not the argument, picks the block. */
    RESET();
    env.nSelectedBaseIdx = 1;
    FAC(0, 1, 2, 2) = 1;  FAC(108, 1, 2, 2) = 1;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 1, "base 1's own cell is the one tested");
    RESET();
    FAC(0, 0, 2, 2) = 1;  FAC(108, 0, 2, 2) = 1;
    env.nSelectedBaseIdx = 1;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 0,
               "and a facility in base 0 does not make base 1's empty cell valid");

    /* 7. deviations: the original bounds nothing. */
    RESET();
    env.nSelectedBaseIdx = 8;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 0, "base index 8 is refused");
    RESET();
    env.nSelectedBaseIdx = -1;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 2), 0, "a negative base index is refused");
    RESET();
    CheckEqI32(Base_CanPlaceHangarAt(&env, 6, 2), 0, "a column of 6 is refused");
    RESET();
    CheckEqI32(Base_CanPlaceHangarAt(&env, 2, 6), 0, "and a row of 6");

    /* 8. the clamp: a hangar anchor at row 5 would write scratch[36..42] past the original's
     * 36 dword buffer (into its saved registers). The port keeps the stores inside the 36 cells,
     * and the grid itself is not touched outside the base's own block. */
    RESET();
    FAC(0, 0, 5, 5) = 9;
    FAC(108, 0, 5, 5) = 11;
    CheckEqI32(Base_CanPlaceHangarAt(&env, 5, 5), 1,
               "a row 5 hangar anchor still answers, through the capacity tail");
    CheckEqI32(grid[0x174 + 144], 0,
               "and the clamped footprint stores wrote nothing: this function only reads the grid");

#undef FAC
#undef DAYS
#undef RESET
}

/* BaseFac_FormatCostLabel @ 0x6a404: the TEXTS name, the dash, the counter, and the increment. */
static void TestFacCostLabel(void)
{
    static u8 aTexts[PUFO_TEXT_REC_STRIDE * (PUFO_FAC_NAME_REC_BASE + 17)];
    static i8 abKind[PUFO_BASE_KIND_TABLE_LEN];
    static char szLabel[64];
    const char *psz;

    printf("BaseFac_FormatCostLabel\n");

    memset(aTexts, 0, sizeof(aTexts));
    for (i32 k = 0; k < 17; ++k) {
        char szName[16];
        (void)snprintf(szName, sizeof(szName), "Fac%d", PUFO_FAC_NAME_REC_BASE + k);
        memcpy(aTexts + (size_t)PUFO_TEXT_REC_STRIDE * (PUFO_FAC_NAME_REC_BASE + k),
               szName, strlen(szName));
    }

    memset(abKind, 0, sizeof(abKind));
    psz = BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, 0, abKind);
    Check(psz == (const char *)szLabel, "the return is the label buffer it was given");
    Check(strcmp(szLabel, "Fac905-0") == 0, "TEXTS[905 + kind], a dash, then the counter");
    CheckEqI32(abKind[0], 1, "and the counter is incremented for next time");

    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, 0, abKind);
    Check(strcmp(szLabel, "Fac905-1") == 0, "the second call shows the incremented counter");
    CheckEqI32(abKind[0], 2, "which is one more again");

    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, 16, abKind);
    Check(strcmp(szLabel, "Fac921-0") == 0, "kind 16 reads TEXTS[921] and its own counter slot");

    /* The counter is loaded SIGNED and printed as a signed decimal, so a negative entry gives a
     * label with two dashes. */
    abKind[3] = -3;
    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, 3, abKind);
    Check(strcmp(szLabel, "Fac908--3") == 0, "a negative counter prints as a negative number");
    CheckEqI32(abKind[3], -2, "and +1 moves it towards zero");

    /* The increment is sign-extended before the byte store, so 127 wraps to -128. */
    abKind[4] = 127;
    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, 4, abKind);
    Check(strcmp(szLabel, "Fac909-127") == 0, "127 prints as 127");
    CheckEqI32(abKind[4], -128, "and the stored +1 wraps to -128");

    /* Deviation: the original copies the name with no bound at all. */
    szLabel[0] = 'X';
    szLabel[1] = 'X';
    (void)BaseFac_FormatCostLabel(szLabel, 1, aTexts, 0, abKind);
    CheckEqI32(szLabel[0], 0, "a one byte buffer holds only the NUL");
    CheckEqI32(szLabel[1], 'X', "and the byte past the capacity is untouched");
}

/* Smap_SumSiteScores @ 0x96894: the 37 halfwords of the transfer flow's working site slot. */
static void TestSiteScore(void)
{
    static SmapSite aSites[4];

    printf("Smap_SumSiteScores\n");

    memset(aSites, 0, sizeof(aSites));

    /* Every scored slot of the working record -- index 3, the one AT the count -- carries 10. */
    for (i32 i = PUFO_SITE_SCORE_A_FIRST; i <= PUFO_SITE_SCORE_A_LAST; ++i) {
        aSites[3].auItemQty[i] = 10;
    }
    aSites[3].auItemQty[PUFO_SITE_SCORE_SINGLE] = 10;
    for (i32 i = PUFO_SITE_SCORE_B_FIRST; i <= PUFO_SITE_SCORE_B_LAST; ++i) {
        aSites[3].auItemQty[i] = 10;
    }

    CheckEqI32(Smap_SumSiteScores(aSites, 3), 37 * 10,
               "28 + 1 + 8 halfwords, each counted once (0x968C0..0x968F8)");

    /* The record before the count is not the one read. */
    for (i32 i = 0; i < 256; ++i) {
        aSites[2].auItemQty[i] = 999;
    }
    CheckEqI32(Smap_SumSiteScores(aSites, 3), 37 * 10,
               "the slot at the count is read, not the last used one (0x96898)");

    /* Slots outside the three ranges are ignored, even inside the same record. */
    aSites[3].auItemQty[PUFO_SITE_SCORE_A_FIRST - 1] = 5000; /* 76  */
    aSites[3].auItemQty[PUFO_SITE_SCORE_A_LAST + 1]  = 5000; /* 105 */
    aSites[3].auItemQty[PUFO_SITE_SCORE_SINGLE - 1]  = 5000; /* 113 */
    aSites[3].auItemQty[PUFO_SITE_SCORE_SINGLE + 1]  = 5000; /* 115 */
    aSites[3].auItemQty[PUFO_SITE_SCORE_B_FIRST - 1] = 5000; /* 140 */
    aSites[3].auItemQty[PUFO_SITE_SCORE_B_LAST + 1]  = 5000; /* 149 */
    aSites[3].auItemQty[0] = 5000;
    aSites[3].auItemQty[255] = 5000;
    CheckEqI32(Smap_SumSiteScores(aSites, 3), 37 * 10,
               "the neighbour of each range is outside the sum");

    /* The two gaps between the three LDRH chains are really skipped: 105..113 and 115..140. */
    for (i32 i = PUFO_SITE_SCORE_A_LAST + 1; i < PUFO_SITE_SCORE_SINGLE; ++i) {
        aSites[3].auItemQty[i] = 1;
    }
    for (i32 i = PUFO_SITE_SCORE_SINGLE + 1; i < PUFO_SITE_SCORE_B_FIRST; ++i) {
        aSites[3].auItemQty[i] = 1;
    }
    CheckEqI32(Smap_SumSiteScores(aSites, 3), 37 * 10,
               "the 9 + 26 halfwords between the ranges add nothing");

    /* Halfwords are zero-extended into a dword accumulator, and 37 * 0xFFFF fits in it. */
    memset(aSites, 0, sizeof(aSites));
    for (i32 i = PUFO_SITE_SCORE_A_FIRST; i <= PUFO_SITE_SCORE_A_LAST; ++i) {
        aSites[0].auItemQty[i] = 0xFFFF;
    }
    aSites[0].auItemQty[PUFO_SITE_SCORE_SINGLE] = 0xFFFF;
    for (i32 i = PUFO_SITE_SCORE_B_FIRST; i <= PUFO_SITE_SCORE_B_LAST; ++i) {
        aSites[0].auItemQty[i] = 0xFFFF;
    }
    CheckEqI32(Smap_SumSiteScores(aSites, 0), 37 * 0xFFFF,
               "the maximum score 2424795 stays positive in the dword accumulator");
    Check(Smap_SumSiteScores(aSites, 0) != 0, "a count of 0 reads slot 0: the count is an index");

    /* Deviation: the original bounds nothing -- it just adds 742 to the array base. */
    CheckEqI32(Smap_SumSiteScores(NULL, 3), 0, "a null array returns 0 instead of faulting");
    CheckEqI32(Smap_SumSiteScores(aSites, -1), 0, "a negative count returns 0");
    CheckEqI32(Smap_SumSiteScores(aSites, PUFO_SMAP_SITE_COUNT), 0,
               "count == 200 is the original's read past aSites[200]");
    CheckEqI32(Smap_SumSiteScores(aSites, PUFO_SMAP_SITE_COUNT + 1), 0, "and further out too");
}

/* RadarScreen_SetCursorNW / NE / SW / SE @ 0x5e768 / 0x5e808 / 0x5e93c / 0x5ea70 */
static void TestRadarCursors(void)
{
    static char szMsg[108];
    RadarCursorEnv env;
    i32 nX, nY, nRange, nTimer;
    i32 nInterceptA, nInterceptB, nLevel, nBoundA, nBoundB;
    u8 bGate, bFlip;

    printf("RadarScreen_SetCursorNW/NE/SW/SE\n");

    /* The scratch string has to hold something before each setter runs, or "it was cleared" proves
     * nothing. Assigned by hand rather than with strcpy, which this toolchain deprecates. */
#define RC_RESET()                                                           \
    szMsg[0] = 's'; szMsg[1] = 't'; szMsg[2] = 'a'; szMsg[3] = 'l';          \
    szMsg[4] = 'e'; szMsg[5] = 0;                                            \
    nX = -1; nY = -1; nRange = -1; nTimer = 55;                              \
    nInterceptA = 0; nInterceptB = 0;                                        \
    nLevel = 0; nBoundA = 100; nBoundB = 200;                                \
    bGate = 0; bFlip = 9;                                                    \
    memset(&env, 0, sizeof(env));                                            \
    env.pbGate = &bGate; env.pnCursorX = &nX; env.pnCursorY = &nY;           \
    env.pnRangeTarget = &nRange; env.pbFlip = &bFlip;                        \
    env.pnMsgTimer = &nTimer; env.pszMsg = szMsg; env.cchMsg = sizeof(szMsg);\
    env.pnInterceptA = &nInterceptA; env.pnInterceptB = &nInterceptB;        \
    env.pnLevel = &nLevel; env.pnBoundA = &nBoundA; env.pnBoundB = &nBoundB;

    /* 1. the four corners. */
    RC_RESET();
    RadarScreen_SetCursorNW(&env);
    CheckEqI32(nX, 117, "NW x (0x5E77C)");
    CheckEqI32(nY, 86, "  y (0x5E788)");
    CheckEqI32(nRange, 560, "  the range target is the fixed 560 (0x5E794)");
    CheckEqI32(bFlip, 0, "  and the flip byte is cleared");
    CheckEqI32(nTimer, 0, "  the message timer is reset (0x5E7DC)");
    CheckEqI32(szMsg[0], 0, "  and the scratch string is cleared, not written (0x5E7C0)");

    RC_RESET();
    RadarScreen_SetCursorNE(&env);
    CheckEqI32(nX, 161, "NE x (0x5E848)");
    CheckEqI32(nY, 86, "  y, the same north row as NW");
    CheckEqI32(bFlip, 0, "  flip cleared");

    RC_RESET();
    RadarScreen_SetCursorSW(&env);
    CheckEqI32(nX, 117, "SW x, the same west column as NW");
    CheckEqI32(nY, 107, "  y (0x5E98C)");
    CheckEqI32(bFlip, 0, "  flip cleared");

    RC_RESET();
    RadarScreen_SetCursorSE(&env);
    CheckEqI32(nX, 161, "SE x");
    CheckEqI32(nY, 107, "  y");
    CheckEqI32(nRange, 560, "  the fixed 560 again (0x5EA9C)");
    CheckEqI32(bFlip, 1, "  and SE is the only corner that SETS the flip byte (0x5EAA8)");

    /* 2. NE takes the MAX of the two bounds, SW the MIN, both over 560*(26-level)/114.
     * level 0: 560*26 = 14560, 14560/114 = 127. */
    RC_RESET();
    RadarScreen_SetCursorNE(&env);
    CheckEqI32(nRange, 200 + 127, "NE adds the scaled range to the larger bound (0x5E8D0)");
    RC_RESET();
    RadarScreen_SetCursorSW(&env);
    CheckEqI32(nRange, 100 + 127, "SW adds it to the smaller bound (0x5EA04)");

    /* level 26 leaves nothing to add, so the target is the bound itself. */
    RC_RESET();
    nLevel = 26;
    RadarScreen_SetCursorNE(&env);
    CheckEqI32(nRange, 200, "level 26 adds nothing");
    RC_RESET();
    nLevel = 26;
    RadarScreen_SetCursorSW(&env);
    CheckEqI32(nRange, 100, "  on both of them");

    /* level 13: 560*13 = 7280, 7280/114 = 63. */
    RC_RESET();
    nLevel = 13;
    RadarScreen_SetCursorNE(&env);
    CheckEqI32(nRange, 200 + 63, "the scale is linear in the level");

    /* equal bounds make max and min the same. */
    RC_RESET();
    nBoundA = 150; nBoundB = 150;
    RadarScreen_SetCursorNE(&env);
    nRange = -1;
    RadarScreen_SetCursorSW(&env);
    CheckEqI32(nRange, 150 + 127, "with equal bounds the two corners agree");

    /* 3. NE and SW bail when both intercept slots are the -2 sentinel; NW and SE do not guard. */
    RC_RESET();
    nInterceptA = -2; nInterceptB = -2;
    RadarScreen_SetCursorNE(&env);
    CheckEqI32(nX, -1, "NE does nothing with no intercept target (0x5E818)");
    CheckEqI32(nTimer, 55, "  not even the timer reset");
    RC_RESET();
    nInterceptA = -2; nInterceptB = -2;
    RadarScreen_SetCursorSW(&env);
    CheckEqI32(nX, -1, "  nor does SW (0x5E94C)");
    RC_RESET();
    nInterceptA = -2; nInterceptB = 3;
    RadarScreen_SetCursorNE(&env);
    CheckEqI32(nX, 161, "one real target is enough for NE to act");

    RC_RESET();
    nInterceptA = -2; nInterceptB = -2;
    RadarScreen_SetCursorNW(&env);
    CheckEqI32(nX, 117, "NW has no intercept guard at all");
    RC_RESET();
    nInterceptA = -2; nInterceptB = -2;
    RadarScreen_SetCursorSE(&env);
    CheckEqI32(nX, 161, "  nor does SE");

    /* 4. the gate. */
    RC_RESET();
    bGate = 1;
    RadarScreen_SetCursorNW(&env);
    CheckEqI32(nX, -1, "a non-zero gate stops the button (0x5E774)");
    CheckEqI32(szMsg[0], 's', "  the scratch string survives");
    RC_RESET();
    bGate = 200;
    RadarScreen_SetCursorNE(&env);
    CheckEqI32(nX, -1, "  on the guarded pair too");

    /* 5. nulls. */
    RadarScreen_SetCursorNW(NULL);
    RadarScreen_SetCursorNE(NULL);
    RadarScreen_SetCursorSW(NULL);
    RadarScreen_SetCursorSE(NULL);
    Check(true, "a null env does nothing in any of the four");

    RC_RESET();
    env.pnCursorX = NULL; env.pnCursorY = NULL; env.pnRangeTarget = NULL;
    env.pbFlip = NULL; env.pnMsgTimer = NULL; env.pszMsg = NULL;
    RadarScreen_SetCursorNW(&env);
    Check(true, "every field is optional");
#undef RC_RESET
}

/* RadarScreen_RollWeaponHit @ 0x5db24 and RadarScreen_BlitOverlay @ 0x5d6d4. The roll is random, so
 * the checks are invariants over many draws: every HP change has to sit inside the event's band, a
 * gate that fails changes nothing, and an event outside 25..30 never fires. */
static void TestRadarWeaponHit(void)
{
    static PtrArray list;
    static i32 nStrategIdx;
    static i32 nHp;
    static RadarHitRollEnv env;
    StrategItem *pItem;
    i32 nIter;
    i32 nFired;
    i32 nBad;

    printf("RadarScreen_RollWeaponHit @ 0x5db24\n");

    PtrArray_ctor(&list);
    (void)StrategItem_AllocAdd(&list, 0);
    pItem = (StrategItem *)PtrArray_Get(&list, 0);
    if (pItem == NULL) {
        printf("  FAIL the strateg entry could not be created\n");
        return;
    }

    nStrategIdx = 0;
    memset(&env, 0, sizeof(env));
    env.pStrategList = &list;
    env.pnStrategIdx = &nStrategIdx;
    env.pnTargetHp = &nHp;

    /* event 25 is the fixed -50; the other five add a RandU16 band. */
    struct { i32 nEvent; i32 nLo; i32 nHi; } aCases[] = {
        { 25, -50, -50 }, { 26, -100, -81 }, { 27, -120, -101 },
        { 28, -90, -61 }, { 29, -140, -91 }, { 30, -230, -201 },
    };
    for (i32 c = 0; c < 6; ++c) {
        nFired = 0;
        nBad = 0;
        for (nIter = 0; nIter < 400; ++nIter) {
            i32 nBefore = 1000;
            bool bHit;
            nHp = nBefore;
            bHit = RadarScreen_RollWeaponHit(&env, aCases[c].nEvent);
            if (bHit) {
                i32 nDelta = nHp - nBefore;
                nFired += 1;
                if (nDelta < aCases[c].nLo || nDelta > aCases[c].nHi) {
                    nBad += 1;
                }
                if (pItem->nHp != nHp) {
                    nBad += 1;      /* 0x5DBB0 mirrors the global into StrategItem.nHp */
                }
            } else if (nHp != nBefore) {
                nBad += 1;          /* a failed gate must leave the target alone */
            }
        }
        Check(nBad == 0, aCases[c].nEvent == 25
              ? "event 25 is exactly -50 and mirrors StrategItem.nHp (0x5DC74)"
              : "every band stays inside its RandU16 range (0x5DB90, 0x5DBB0)");
        Check(nFired > 0 && nFired < 400, "the gate both passes and fails across draws");
    }

    /* An event outside 25..30 never fires. */
    nHp = 777;
    pItem->nHp = 777;
    Check(!RadarScreen_RollWeaponHit(&env, 24), "event 24 is outside the switch (0x5DC80)");
    Check(!RadarScreen_RollWeaponHit(&env, 31), "and so is event 31");
    CheckEqI32(nHp, 777, "neither touches the target HP");
    CheckEqI32(pItem->nHp, 777, "nor the strateg entry");

    /* Nothing clamps the HP, so a big band can drive it negative. */
    nHp = 10;
    for (nIter = 0; nIter < 200; ++nIter) {
        (void)RadarScreen_RollWeaponHit(&env, 30);
    }
    Check(nHp < 0, "event 30 drives the target HP negative, nothing clamps it (0x5DB8C)");

    /* Deviation: the original dereferences the strateg lookup unchecked. */
    nStrategIdx = 9999;
    nHp = 400;
    Check(RadarScreen_RollWeaponHit(&env, 25), "an out-of-range strateg index still rolls");
    CheckEqI32(nHp, 350, "the global HP is still written (0x5DB90)");
    nStrategIdx = 0;

    Check(!RadarScreen_RollWeaponHit(NULL, 25), "a null env rolls nothing");

    RadarScreen_BlitOverlay(NULL);
    Check(true, "RadarScreen_BlitOverlay @ 0x5d6d4 tolerates the null pad");
}

static void TestCountInbound(void)
{
    static SmapSite aSites[4];
    static BaseRec aBases[2];
    static BaseRec *apBases[2];
    i32 i;

    printf("Base_CountInboundTransfers @ 0x67F78\n");

#define INB_RESET()                                          \
    memset(aSites, 0, sizeof(aSites));                       \
    memset(aBases, 0, sizeof(aBases));                       \
    for (i = 0; i < 2; ++i) { apBases[i] = &aBases[i]; }

    INB_RESET();
    CheckEqI32(Base_CountInboundTransfers(aSites, 2, -1, apBases, 0, false), 0,
               "no site and no scan gives nothing (0x67FA4)");

    /* The buy/sell and transfer flows pass the site COUNT, i.e. the working record. */
    INB_RESET();
    aSites[2].bRecoveredCraftCount = 3;
    aSites[2].bUnk518 = 4;
    CheckEqI32(Base_CountInboundTransfers(aSites, 2, 2, apBases, 0, false), 7,
               "the 0x205 + 0x206 pair of the working record (0x67FB8)");

    /* Both bytes are loaded with LDRSB, so 0xFF is minus one and not 255. */
    INB_RESET();
    aSites[2].bRecoveredCraftCount = 0xFF;
    aSites[2].bUnk518 = 2;
    CheckEqI32(Base_CountInboundTransfers(aSites, 2, 2, apBases, 0, false), 1,
               "the pair is signed: 255 counts as -1 (0x67FB8 LDRSB)");

    /* The scan adds every OTHER site whose bFlag1 equals this base's type byte. */
    INB_RESET();
    aBases[0].bUnk4 = 5;
    aSites[0].bFlag1 = 5; aSites[0].bRecoveredCraftCount = 1; aSites[0].bUnk518 = 1; /* 2  */
    aSites[1].bFlag1 = 6; aSites[1].bRecoveredCraftCount = 9; aSites[1].bUnk518 = 9; /* 18 */
    aSites[2].bFlag1 = 5; aSites[2].bRecoveredCraftCount = 4; aSites[2].bUnk518 = 4; /* 8  */
    CheckEqI32(Base_CountInboundTransfers(aSites, 3, 2, apBases, 0, true), 10,
               "site 0 matches the type, site 1 does not, site 2 is the one already counted "
               "(0x67FEC / 0x68014)");
    CheckEqI32(Base_CountInboundTransfers(aSites, 3, 2, apBases, 1, true), 8,
               "a base of another type matches nothing in the scan (0x68014 CMP)");
    CheckEqI32(Base_CountInboundTransfers(aSites, 3, 2, apBases, 0, false), 8,
               "with the scan off the type match is never consulted (0x67FD0 ANDS / BEQ)");

    /* The loop bound is the signed site count; the direct term is not gated by it. */
    INB_RESET();
    aBases[0].bUnk4 = 5;
    aSites[0].bFlag1 = 5; aSites[0].bRecoveredCraftCount = 7;
    aSites[2].bRecoveredCraftCount = 5; aSites[2].bUnk518 = 5;
    CheckEqI32(Base_CountInboundTransfers(aSites, 0, 2, apBases, 0, true), 10,
               "a count of zero skips the scan (0x67FDC BLE) but keeps the direct term");
    CheckEqI32(Base_CountInboundTransfers(aSites, -1, 2, apBases, 0, true), 10,
               "  and so does a negative one");

    /* Deviations: the original bounds nothing at all. */
    CheckEqI32(Base_CountInboundTransfers(aSites, 3, 2, apBases, 99, true), 10,
               "an out-of-range base index skips the scan instead of reading past apBases");
    CheckEqI32(Base_CountInboundTransfers(aSites, 3, PUFO_SMAP_SITE_COUNT, apBases, 0, true), 7,
               "a site index at the array end is refused, not read past it");
    CheckEqI32(Base_CountInboundTransfers(NULL, 3, 2, apBases, 0, true), 0, "a null site array gives 0");
    CheckEqI32(Base_CountInboundTransfers(aSites, 3, 2, NULL, 0, true), 0, "a null base array too");
#undef INB_RESET
}

static void TestBaseStripHit(void)
{
    BaseStripHitEnv env;
    u8 bCount, bSelected, bTransfer, bBaseInfo, bPsi, bTap;

    printf("Base_HitTestFacilityStrip @ 0x671B0\n");

#define STRIP_RESET()                                                     \
    bCount = 8;      /* 0x159D80, the base count */                       \
    bSelected = 3;   /* 0x159D81, the selected base */                    \
    bTransfer = 0; bBaseInfo = 0; bPsi = 0; bTap = 1;                    \
    memset(&env, 0, sizeof(env));                                         \
    env.pbBaseCount = &bCount; env.pbSelectedBase = &bSelected;           \
    env.pbGateTransfer = &bTransfer; env.pbGateBaseInfo = &bBaseInfo;    \
    env.pbGatePsi = &bPsi; env.pbTapFlag = &bTap;

    STRIP_RESET();
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 15), 0,
               "a hit in the first cell gives base 0 (0x67264)");
    CheckEqI32(bTap, 0, "a hit clears the geoscape tap flag (0x67250 STRB)");
    CheckEqI32(Base_HitTestFacilityStrip(&env, 211, 3), 0, "the band's corner (211, 3) is inside");
    CheckEqI32(Base_HitTestFacilityStrip(&env, 237, 224), 7,
               "and so is (237, 28 * 8) (0x67214 BLE)");

    STRIP_RESET();
    CheckEqI32(Base_HitTestFacilityStrip(&env, 210, 15), 3,
               "x below 211 misses and returns nSelectedBaseIdx (0x67224)");
    CheckEqI32(Base_HitTestFacilityStrip(&env, 238, 15), 3, "x above 237 misses");
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 2), 3, "y below 3 misses");
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 225), 3, "y above 28 * base count misses");
    CheckEqI32(bTap, 1, "a miss leaves the tap flag alone");

    /* Each of the three screens forces the x argument to 10, which is below 211. */
    STRIP_RESET(); bPsi = 1;
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 15), 3,
               "the psi screen open disables the strip (0x6723C / 0x671E8)");
    CheckEqI32(bTap, 1, "  and a disabled strip does not clear the tap flag");
    STRIP_RESET(); bTransfer = 1;
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 15), 3, "so does the transfer screen (unk_1870EC)");
    STRIP_RESET(); bBaseInfo = 1;
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 15), 3, "so does the base-info screen (unk_15883C)");
    STRIP_RESET(); bPsi = 0x80;
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 15), 3, "the gate is non-zero, not equal-to-one");

    /* The drawer steps 28 pixels per base; the hit test divides by 30. From the third cell down the
     * first row of a cell reports the previous base. */
    STRIP_RESET();
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 59), 1,
               "the third drawn cell starts at 28*2 + 3 = 59 and reports base 1 (ORIGINAL BUG)");
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 60), 2, "one row lower it reports 2");
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 199), 6,
               "the eighth drawn cell starts at 28*7 + 3 = 199 and reports 6");

    /* The base count is the signed byte at 0x159D80. */
    STRIP_RESET(); bCount = 0;
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 3), 3, "with no bases the bound is 0, so every row misses");
    STRIP_RESET(); bCount = 0xFF; /* -1 */
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 3), 3, "a negative count misses too (0x67204 LDRSB)");
    STRIP_RESET(); bCount = 1;
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 28), 0, "one base reaches y = 28 and still gives 0");
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 29), 3, "  and 29 is outside its own bound");

    CheckEqI32(Base_HitTestFacilityStrip(NULL, 220, 15), 0, "a null env returns 0");
    STRIP_RESET(); env.pbTapFlag = NULL;
    CheckEqI32(Base_HitTestFacilityStrip(&env, 220, 15), 0, "a null tap flag still hits");
#undef STRIP_RESET
}

static void TestRadarPaletteCycle(void)
{
    i32 nCur, nTgt;
    u8 bFlip;
    RadarPaletteEnv env;
    u32 aBefore[PUFO_RADAR_PAL_CYCLE];
    i32 i;

    printf("\nRadarScreen_CyclePalette @ 0x5ed0c\n");

    /* 1. The palette half: slot 22 entries 0..24 shift up one place, entry 24 wraps to entry 0. */
    for (i = 0; i < PUFO_RADAR_PAL_CYCLE; ++i) {
        g_aPalette[PUFO_RADAR_PAL_SLOT][i] = (u32)(100 + i);
        aBefore[i] = (u32)(100 + i);
    }
    g_aPalette[PUFO_RADAR_PAL_SLOT][PUFO_RADAR_PAL_CYCLE] = 0xAAu;
    g_aPalette[PUFO_RADAR_PAL_SLOT + 1][0] = 0xBBu;

    nCur = 0; nTgt = 0; bFlip = 0;
    env.pnRangeCur = &nCur; env.pnRangeTarget = &nTgt; env.pbFlip = &bFlip;
    RadarScreen_CyclePalette(&env);

    CheckEqI32((i32)g_aPalette[PUFO_RADAR_PAL_SLOT][0], (i32)aBefore[PUFO_RADAR_PAL_CYCLE - 1],
               "entry 0 takes the old entry 24 (0x5ED50)");
    CheckEqI32((i32)g_aPalette[PUFO_RADAR_PAL_SLOT][1], (i32)aBefore[0],
               "every other entry takes the one below it");
    CheckEqI32((i32)g_aPalette[PUFO_RADAR_PAL_SLOT][24], (i32)aBefore[23], "  up to entry 24");
    CheckEqI32((i32)g_aPalette[PUFO_RADAR_PAL_SLOT][PUFO_RADAR_PAL_CYCLE], 0xAA,
               "entry 25 of the slot is outside the cycle and untouched");
    CheckEqI32((i32)g_aPalette[PUFO_RADAR_PAL_SLOT + 1][0], 0xBB, "  and the next slot is untouched");

    /* 2. The range half without the flip byte: two steps toward the target, whichever way. */
    nCur = 0; nTgt = 5; bFlip = 0;
    RadarScreen_CyclePalette(&env);
    CheckEqI32(nCur, 2, "below the target it moves up two (0x5ED64 then 0x5ED84)");

    nCur = 5; nTgt = 0;
    RadarScreen_CyclePalette(&env);
    CheckEqI32(nCur, 3, "above the target it moves down two (0x5ED74 then 0x5ED94)");

    nCur = 7; nTgt = 7;
    RadarScreen_CyclePalette(&env);
    CheckEqI32(nCur, 7, "already at the target it does not move at all");

    nCur = 0; nTgt = 1;
    RadarScreen_CyclePalette(&env);
    CheckEqI32(nCur, 1, "one step short moves by exactly one, not two");

    /* 3. With the flip byte set, a value still short of the target gets up to four more steps. */
    nCur = 0; nTgt = 10; bFlip = 1;
    RadarScreen_CyclePalette(&env);
    CheckEqI32(nCur, 6, "flip set: two normal steps plus four more (0x5EDB0..0x5EDDC)");

    nCur = 0; nTgt = 3; bFlip = 1;
    RadarScreen_CyclePalette(&env);
    CheckEqI32(nCur, 3, "  and the extra steps stop at the target, not past it");

    nCur = 0; nTgt = 1; bFlip = 1;
    RadarScreen_CyclePalette(&env);
    CheckEqI32(nCur, 1, "  one step short still moves by one with the flip set");

    /* The flip branch is upward only: a value above the target gets the same two steps either way. */
    nCur = 10; nTgt = 0; bFlip = 1;
    RadarScreen_CyclePalette(&env);
    CheckEqI32(nCur, 8, "the flip byte adds no downward steps (0x5ED9C gates on cur < tgt)");

    RadarScreen_CyclePalette(NULL);
    CheckEqI32(nCur, 8, "a null env leaves the range alone");
}

/* Base_TickFacilityConstruction @ 0x62824. Base 0 unless a case says otherwise:
 * fac[] is grid + 0x174 (tileA +0, tileB +36, layer2 +72, facType +108) and buildDays is grid +
 * 36*nBase. */
static u8 g_aTickGrid[1524];
static BaseRec g_aTickBases[2];
static BaseRec *g_apTickBases[2];
static ObjectRecFull g_aTickObj[4];
static ObjectRecFull *g_apTickObj[4];
static Soldier g_aTickSoldier;
static Soldier *g_apTickSoldiers[1];
static SmapSite g_aTickSites[2];
static u16 g_aTickResearch[144 * 8];
static u16 g_aTickManuf[200 * 8];
static UiLineList g_tickLines;
static u32 g_dwTickShortfall;
static u8 g_nTickBaseCount;
static i32 g_nTickSoldierCount;
static i32 g_nTickSamples;

static void TickSample(i32 nSample, void *pUser)
{
    (void)nSample;
    (void)pUser;
    ++g_nTickSamples;
}

#define TICKFAC(nLayer, nRow, nCol) g_aTickGrid[0x174 + (nLayer) + 6 * (nRow) + (nCol)]
#define TICKDAYS(nRow, nCol)        g_aTickGrid[6 * (nRow) + (nCol)]

#define TICK_RESET()                                                        \
    do {                                                                    \
        memset(g_aTickGrid, 0, sizeof(g_aTickGrid));                        \
        memset(g_aTickObj, 0, sizeof(g_aTickObj));                          \
        memset(g_aTickResearch, 0, sizeof(g_aTickResearch));                \
        memset(g_aTickManuf, 0, sizeof(g_aTickManuf));                      \
        memset(&g_aTickSoldier, 0, sizeof(g_aTickSoldier));                 \
        memset(g_aTickSites, 0, sizeof(g_aTickSites));                      \
        memset(&g_tickLines, 0, sizeof(g_tickLines));                       \
        for (i = 0; i < 2; ++i) {                                           \
            memset(&g_aTickBases[i], 0, sizeof(g_aTickBases[i]));           \
            g_apTickBases[i] = &g_aTickBases[i];                            \
            g_aTickBases[i].bSlot = (u8)i;                                  \
        }                                                                   \
        for (i = 0; i < 4; ++i) {                                           \
            g_apTickObj[i] = &g_aTickObj[i];                                \
        }                                                                   \
        memset(g_apTickSoldiers, 0, sizeof(g_apTickSoldiers));              \
        g_apTickSoldiers[0] = &g_aTickSoldier;                              \
        g_aTickSoldier.nKind = 0;                                           \
        g_dwTickShortfall = 0;                                              \
        g_nTickBaseCount = 1;                                               \
        g_nTickSoldierCount = 1;                                            \
        g_nTickSamples = 0;                                                 \
    } while (0)

#define TICK_ENV(env)                                                     \
    memset(&(env), 0, sizeof(env));                                       \
    (env).abFacilityGrid = g_aTickGrid;                                   \
    (env).pbBaseCount = &g_nTickBaseCount;                                \
    (env).apSoldiers = g_apTickSoldiers;                                  \
    (env).pnSoldierCount = &g_nTickSoldierCount;                          \
    (env).apBases = g_apTickBases;                                        \
    (env).apObjects = g_apTickObj;                                        \
    (env).nObjectCount = 4;                                               \
    (env).aSites = g_aTickSites;                                          \
    (env).nSiteCount = 0;                                                 \
    (env).apResearchCols = g_aTickResearch;                               \
    (env).nResearchEntries = 1;                                           \
    (env).apManufCols = g_aTickManuf;                                     \
    (env).nManufEntries = 1;                                              \
    (env).pdwShortfall = &g_dwTickShortfall;                              \
    (env).pLines = &g_tickLines;                                          \
    (env).pfnPlaySample = TickSample;                                     \
    (env).pSoundUser = NULL

static void TestTickFacilityConstruction(void)
{
    BaseTickConstructionEnv env;
    i32 i;

    printf("Base_TickFacilityConstruction @ 0x62824\n");

    /* A non-zero day byte that does not reach zero is just decremented: no report, no passes. */
    TICK_RESET();
    TICKDAYS(0, 0) = 3;
    TICKFAC(PUFO_GRID_FAC_TYPE_OFF, 0, 0) = FAC_GENERAL_STORES;
    g_aTickManuf[0] = 100;
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKDAYS(0, 0), 2, "a build timer counts down by one");
    CheckEqI32((i32)g_tickLines.nLineCount, 0, "and nothing is reported until it reaches zero");
    CheckEqI32(TICKFAC(PUFO_GRID_LAYER2_OFF, 0, 0), 0, "and no needed pass runs");

    /* Reaching zero reports message 38 and raises the shortfall dword. The callee takes six
     * arguments -- a3 in R2, a4 in R3, a5 at [SP+0] and a6 at [SP+4] -- and the ARM puts the base
     * in R2 (0x62938), the ROW in R3 (AND R3,R7,#0xFF at 0x6292C), the COLUMN at [SP+0]
     * (STRB R5 at 0x62944) and R9 -- the zero the completion stores with -- at [SP+4]
     * (STRB R9 at 0x62930). So the payload is (38, base, row, col, 0). */
    TICK_RESET();
    TICKDAYS(1, 2) = 1;
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKDAYS(1, 2), 0, "a timer that reaches zero stays zero");
    CheckEqI32((i32)g_tickLines.nLineCount, 1, "one report line");
    CheckEqI32(g_tickLines.abLineFlags[0], 38, "message 38");
    CheckEqI32((i32)g_tickLines.anColor[0][0], 0, "colour 0 is the base");
    CheckEqI32((i32)g_tickLines.anColor[0][1], 1, "colour 1 is the row");
    CheckEqI32((i32)g_tickLines.anColor[0][2], 2, "colour 2 is the column");
    CheckEqI32((i32)g_tickLines.anColor[0][3], 0, "colour 3 is zero");
    CheckEqI32((i32)g_dwTickShortfall, 1, "and the shortfall dword is raised");

    /* A completed HANGAR also clears three more days bytes: below, below-right and right. */
    TICK_RESET();
    TICKFAC(PUFO_GRID_FAC_TYPE_OFF, 1, 2) = FAC_HANGAR;
    TICKDAYS(1, 2) = 1;
    TICKDAYS(2, 2) = 5;
    TICKDAYS(2, 3) = 5;
    TICKDAYS(1, 3) = 5;
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKDAYS(2, 2), 0, "the cell below the finished hangar is cleared");
    CheckEqI32(TICKDAYS(2, 3), 0, "the cell below-right is cleared");
    CheckEqI32(TICKDAYS(1, 3), 0, "and the cell to the right");

    /* A finished cell that is not a hangar clears nothing. */
    TICK_RESET();
    TICKFAC(PUFO_GRID_FAC_TYPE_OFF, 1, 2) = FAC_LIVING_QUARTERS;
    TICKDAYS(1, 2) = 1;
    TICKDAYS(2, 2) = 5;
    TICKDAYS(2, 3) = 5;
    TICKDAYS(1, 3) = 5;
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKDAYS(2, 2), 4, "a non-hangar completion clears nothing below, it only counts down");
    CheckEqI32(TICKDAYS(2, 3), 4, "clears nothing below-right");
    CheckEqI32(TICKDAYS(1, 3), 4, "and clears nothing to the right");

    /* The three clears are the days block's own arithmetic, so at the last row they reach into the
     * NEXT base's days block. The grid is big enough and the original does it; the port does too. */
    TICK_RESET();
    g_nTickBaseCount = 1; /* only base 0 is walked, but base 1's bytes are still in the grid */
    TICKFAC(PUFO_GRID_FAC_TYPE_OFF, 5, 5) = FAC_HANGAR;
    TICKDAYS(5, 5) = 1;
    g_aTickGrid[6 * 6 + 5 + 6] = 7;  /* grid[41], base 1's row 0 column 5 */
    g_aTickGrid[6 * 6 + 5 + 7] = 7;  /* grid[42] */
    g_aTickGrid[6 * 6 + 5 + 1] = 7;  /* grid[36], base 1's row 0 column 0 */
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(g_aTickGrid[41], 0, "the below-clear lands in the next base's days block");
    CheckEqI32(g_aTickGrid[42], 0, "and the below-right clear does too");
    CheckEqI32(g_aTickGrid[36], 0, "while the right clear lands on that base's first cell");

    /* Completion runs the six needed passes: a workshop cell plus 100 active manufacturing buys a
     * needed flag (100 / 50 + 1 = 3 cells of budget). */
    TICK_RESET();
    TICKFAC(PUFO_GRID_FAC_TYPE_OFF, 0, 0) = FAC_WORKSHOP;
    TICKDAYS(3, 3) = 1;
    g_aTickManuf[0] = 100;
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKFAC(PUFO_GRID_LAYER2_OFF, 0, 0), 1, "the workshop needed pass ran on completion");

    /* The lab pass is the same shape from the research column. */
    TICK_RESET();
    TICKFAC(PUFO_GRID_FAC_TYPE_OFF, 0, 0) = FAC_LABORATORY;
    TICKDAYS(3, 3) = 1;
    g_aTickResearch[0] = 60;
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKFAC(PUFO_GRID_LAYER2_OFF, 0, 0), 1, "the lab needed pass ran on completion");

    /* Base 1 is walked through its own days block, and its base index reaches the report. Row 1
     * column 3 makes the four payload slots four different values: 1, 1, 3 and 0. */
    TICK_RESET();
    g_nTickBaseCount = 2;
    g_aTickGrid[36 + 6 * 1 + 3] = 1; /* base 1, row 1, column 3 */
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(g_aTickGrid[36 + 9], 0, "base 1's days block is walked too");
    CheckEqI32((i32)g_tickLines.nLineCount, 1, "and it reports once");
    CheckEqI32((i32)g_tickLines.anColor[0][0], 1, "colour 0 names base 1");
    CheckEqI32((i32)g_tickLines.anColor[0][1], 1, "colour 1 is the row");
    CheckEqI32((i32)g_tickLines.anColor[0][2], 3, "colour 2 is the column");
    CheckEqI32((i32)g_tickLines.anColor[0][3], 0, "colour 3 is zero, not 6 * the base");

    /* No bases, no work. */
    TICK_RESET();
    g_nTickBaseCount = 0;
    TICKDAYS(0, 0) = 4;
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKDAYS(0, 0), 4, "a zero base count leaves the timers alone");
    CheckEqI32((i32)g_tickLines.nLineCount, 0, "and posts nothing");

    /* A null environment or a null grid is refused. */
    TICK_RESET();
    TICKDAYS(0, 0) = 4;
    TICK_ENV(env);
    Base_TickFacilityConstruction(NULL);
    env.abFacilityGrid = NULL;
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKDAYS(0, 0), 4, "a null grid changes nothing");
}

/* ---- RadarScreen_BlitMapBackdrop @ 0x5eb10 ---------------------------------------
 *
 * Run on a real 240x320 16 bpp surface, with a real glyph record for the world bitmap
 * (8x3, three different indices in three rows), a real alpha plane for the layer pad and a
 * real glyph for the map pad. Fixture numbers are chosen so the map pad and all three world
 * offsets land on screen: nMapW = nMapH = 100 gives half-extents -70 and -110, scroll (20, 10)
 * puts the map pad at (50, 100), and an overlay origin of 60 puts the world blits at x 110, 10
 * and 210. */
#define BD_SURF_W   240
#define BD_SURF_H   320
#define BD_W          8 /* the world bitmap's width  */
#define BD_H          3 /* and its height            */
#define BD_PAD_W      6 /* the map pad's glyph       */
#define BD_PAD_H      5
#define BD_MAP_SLOT   7 /* the map pad's palette slot */
#define BD_SCROLL_X  20
#define BD_SCROLL_Y  10
#define BD_ORIGIN_X  60 /* nSmapOverlayOriginX */

static SoftSurf g_bdSurf;
static SmapView g_bdView;
static PadImage g_bdMapPad;
static PadImage g_bdLayerPad;
static u8       g_abWorld[4 + BD_W * BD_H];
static u8       g_abMask[4 + BD_W * BD_H];
static u8       g_abMapGlyph[4 + BD_PAD_W * BD_PAD_H];
static u8       g_bdBlink;
static RadarBackdropEnv g_bdEnv;

static i32 BdPix(i32 x, i32 y)
{
    return (i32)((const u16 *)g_bdSurf.pPixels)[y * g_bdSurf.nWidth + x];
}

static void BdSet(i32 x, i32 y, u16 uInk)
{
    ((u16 *)g_bdSurf.pPixels)[y * g_bdSurf.nWidth + x] = uInk;
}

static void BdFixture(void)
{
    memset(g_bdSurf.pPixels, 0, (size_t)g_bdSurf.nWidth * (size_t)g_bdSurf.nHeight * 2u);
    memset(g_aPalette, 0, sizeof(g_aPalette));

    /* The world bitmap is a glyph record: u16 w, u16 h, then w*h indices. Row 0 is index 1,
     * row 1 index 2, row 2 index 3, so the three rows read back as three different colours. */
    g_abWorld[0] = (u8)BD_W; g_abWorld[1] = 0;
    g_abWorld[2] = (u8)BD_H; g_abWorld[3] = 0;
    for (i32 y = 0; y < BD_H; ++y) {
        for (i32 x = 0; x < BD_W; ++x) {
            g_abWorld[4 + y * BD_W + x] = (u8)(y + 1);
        }
    }
    /* Slot 29 is the one the original passes (aPalette + 0x7400). Slots 28 and 30 carry
     * different colours so a wrong slot cannot pass unnoticed. */
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][1] = 0xFFFFu;
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][2] = 0x001Fu;
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][3] = 0xF800u;
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT - 1][1] = 0xAAAAu;
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT + 1][1] = 0xBBBBu;

    /* The layer pad's data plane is the alpha mask, and it carries the same 4-byte header the
     * glyph does, because the original offsets it by (pIdx - pGlyph). */
    g_bdLayerPad.pData    = g_abMask;
    g_bdLayerPad.nMode    = (u8)PUFO_RADAR_MAP_PAL_SLOT;
    g_bdLayerPad.uWidth   = (u16)BD_W;
    g_bdLayerPad.uHeight  = (u16)BD_H;
    g_abMask[0] = (u8)BD_W; g_abMask[1] = 0;
    g_abMask[2] = (u8)BD_H; g_abMask[3] = 0;
    memset(g_abMask + 4, 0xFF, (size_t)BD_W * (size_t)BD_H);

    /* The map pad's glyph: 6x5 of index 1, through palette slot 7. */
    g_abMapGlyph[0] = (u8)BD_PAD_W; g_abMapGlyph[1] = 0;
    g_abMapGlyph[2] = (u8)BD_PAD_H; g_abMapGlyph[3] = 0;
    memset(g_abMapGlyph + 4, 1, (size_t)BD_PAD_W * (size_t)BD_PAD_H);
    g_aPalette[BD_MAP_SLOT][1] = 0x0123u;
    g_bdMapPad.pData   = g_abMapGlyph;
    g_bdMapPad.nMode   = (u8)BD_MAP_SLOT;
    g_bdMapPad.uWidth  = (u16)BD_PAD_W;
    g_bdMapPad.uHeight = (u16)BD_PAD_H;

    memset(&g_bdView, 0, sizeof(g_bdView));
    g_bdView.nScrollX = BD_SCROLL_X;
    g_bdView.nScrollY = BD_SCROLL_Y;
    g_bdView.nMapW    = 100;
    g_bdView.nMapH    = 100;

    g_bdEnv.pView           = &g_bdView;
    g_bdEnv.pMapPad         = &g_bdMapPad;
    g_bdEnv.pLayerPad       = &g_bdLayerPad;
    g_bdEnv.pWorldBits      = g_abWorld;
    g_bdEnv.nOverlayOriginX = BD_ORIGIN_X;
    g_bdEnv.nScreenId       = 1;
    g_bdEnv.pbBlinkPhase    = &g_bdBlink;

    /* A sentinel origin proves that the phase-setting call touches nothing. */
    g_bdMapPad.nOriginX   = -1;
    g_bdMapPad.nOriginY   = -1;
    g_bdLayerPad.nOriginX = -1;
    g_bdLayerPad.nOriginY = -1;
}

static void TestRadarBackdrop(void)
{
    printf("\nRadarScreen_BlitMapBackdrop @ 0x5eb10\n");

    SoftSurf_ctor(&g_bdSurf);
    SoftSurf_AllocPixels(&g_bdSurf, BD_SURF_W, BD_SURF_H, PIXMODE_16BPP);
    g_pMainSurface = &g_bdSurf;
    Render_SetTarget(BD_SURF_W, BD_SURF_H, 2, 2, BD_SURF_W * 2, g_bdSurf.pPixels);

    /* --- the phase toggle: the first call draws nothing at all --- */
    BdFixture();
    g_bdBlink = 0;
    Check(!RadarScreen_BlitMapBackdrop(&g_bdEnv), "the first call reports it drew nothing");
    CheckEqI32(g_bdBlink, 1, "and leaves bRadarBlinkPhase set");
    CheckEqI32(g_bdMapPad.nOriginX, -1, "no pad origin is touched on that call");
    CheckEqI32(g_bdLayerPad.nOriginX, -1, "neither the map pad's nor the layer pad's");
    CheckEqI32(BdPix(50, 100), 0, "and nothing is painted");

    /* --- the second call draws, and clears the phase --- */
    Check(RadarScreen_BlitMapBackdrop(&g_bdEnv), "the next call draws");
    CheckEqI32(g_bdBlink, 0, "and clears the phase");
    CheckEqI32(g_bdMapPad.nOriginX, -50,
               "the map pad ends one map width on from -(scroll + nMapW/2 - 120)");
    CheckEqI32(g_bdMapPad.nOriginY, 100, "its y is -(scroll + nMapH/2 - 160)");
    CheckEqI32(g_bdLayerPad.nOriginX, 210,
               "the layer pad ends at overlay origin - scroll - halfW + nMapW");
    CheckEqI32(g_bdLayerPad.nOriginY, 100, "and its y is copied from the map pad");

    CheckEqI32(BdPix(50, 100), 0x0123, "the map pad paints its glyph at (50, 100)");
    CheckEqI32(BdPix(55, 104), 0x0123, "filling the whole 6x5");
    CheckEqI32(BdPix(56, 100), 0, "one column past the glyph stays empty");
    CheckEqI32(BdPix(50, 105), 0, "one row past it too");

    /* The world bitmap at the first offset: full mask, so each channel is (src * 255) >> 8. */
    CheckEqI32(BdPix(110, 100), 0xF7DE, "world row 0, index 1, at full mask");
    CheckEqI32(BdPix(110, 101), 0x001E, "row 1, index 2");
    CheckEqI32(BdPix(110, 102), 0xF000, "row 2, index 3");
    CheckEqI32(BdPix(117, 102), 0xF000, "eight columns wide");
    CheckEqI32(BdPix(118, 100), 0, "and no further");
    CheckEqI32(BdPix(110, 103), 0, "three rows tall");

    /* The other two offsets are one map width either side of the first. */
    CheckEqI32(BdPix(10, 100), 0xF7DE, "the -nMapW copy paints at x 10");
    CheckEqI32(BdPix(210, 100), 0xF7DE, "the +nMapW copy at x 210");
    CheckEqI32(BdPix(217, 102), 0xF000, "both eight columns wide");
    CheckEqI32(BdPix(100, 100), 0, "the gap between them stays empty");

    /* --- a zero mask leaves the destination exactly as it was --- */
    BdFixture();
    g_bdBlink = 1;
    memset(g_abMask + 4, 0, (size_t)BD_W * (size_t)BD_H);
    BdSet(110, 100, 0x1234u);
    BdSet(10, 101, 0x1234u);
    (void)RadarScreen_BlitMapBackdrop(&g_bdEnv);
    CheckEqI32(BdPix(110, 100), 0x1234, "a zero alpha plane leaves the destination alone");
    CheckEqI32(BdPix(10, 101), 0x1234, "at every offset");
    CheckEqI32(BdPix(50, 100), 0x0123, "while the map pad still paints");

    /* --- screen id 2 skips the three world blits but not the pad --- */
    BdFixture();
    g_bdBlink = 1;
    g_bdEnv.nScreenId = PUFO_RADAR_SCREEN_NO_WRAP;
    (void)RadarScreen_BlitMapBackdrop(&g_bdEnv);
    CheckEqI32(BdPix(110, 100), 0, "screen 2 paints no world bitmap at the first offset");
    CheckEqI32(BdPix(10, 100), 0, "nor at the -nMapW offset");
    CheckEqI32(BdPix(210, 100), 0, "nor at the +nMapW offset");
    CheckEqI32(BdPix(50, 100), 0x0123, "but the map pad still blits twice");
    CheckEqI32(g_bdLayerPad.nOriginX, 210, "and the layer origins are still rewritten");

    /* --- a scroll that puts only the middle world offset on screen --- */
    BdFixture();
    g_bdBlink = 1;
    g_bdView.nScrollX = -120; /* world offsets become 250, 150 and 350 */
    (void)RadarScreen_BlitMapBackdrop(&g_bdEnv);
    CheckEqI32(g_bdMapPad.nOriginX, 90, "the map pad ends on its wrap offset");
    CheckEqI32(BdPix(190, 100), 0x0123, "the first blit painted at 190");
    CheckEqI32(BdPix(90, 100), 0x0123, "and its wrap copy at 90");
    CheckEqI32(BdPix(150, 100), 0xF7DE, "only the middle world offset is on screen");
    CheckEqI32(BdPix(157, 102), 0xF000, "for the full 8x3");
    CheckEqI32(BdPix(149, 100), 0, "the two off-screen offsets paint nothing");

    /* --- the half-extents use C division, not an arithmetic shift --- */
    BdFixture();
    g_bdBlink = 1;
    g_bdView.nMapW = -5;
    g_bdView.nMapH = -5;
    (void)RadarScreen_BlitMapBackdrop(&g_bdEnv);
    CheckEqI32(g_bdMapPad.nOriginX, 107, "-5 / 2 is -2, so halfW is -122 and x = -(20 - 5 - 122)");
    CheckEqI32(g_bdMapPad.nOriginY, 152, "halfH is -162, and y carries no nMapH term");

    /* --- the palette is slot 29, not its neighbours --- */
    BdFixture();
    g_bdBlink = 1;
    (void)RadarScreen_BlitMapBackdrop(&g_bdEnv);
    CheckEqI32(BdPix(110, 100), 0xF7DE, "slot 29's entry 1 is the one used");
    Check(g_aPalette[PUFO_RADAR_MAP_PAL_SLOT - 1][1] == 0xAAAAu
              && g_aPalette[PUFO_RADAR_MAP_PAL_SLOT + 1][1] == 0xBBBBu,
          "while slots 28 and 30 hold different colours and stay untouched");

    /* --- the guards --- */
    BdFixture();
    g_bdBlink = 1;
    Check(!RadarScreen_BlitMapBackdrop(NULL), "a null environment is refused");
    CheckEqI32(g_bdBlink, 1, "and the phase is left alone by it");
    g_bdEnv.pWorldBits = NULL;
    Check(!RadarScreen_BlitMapBackdrop(&g_bdEnv), "a null world bitmap draws nothing");
    CheckEqI32(g_bdBlink, 0, "though the phase is still consumed");
    CheckEqI32(BdPix(50, 100), 0, "the map pad half does not run either");
    CheckEqI32(g_bdMapPad.nOriginX, -1, "and no pad origin is touched");
}

/* ---- RadarScreen_DrawFuelGauge @ 0x5d918 and RadarScreen_DrawRangeBar @ 0x5d6e4 ---------
 *
 * The gauge is run on the same real 240x320 surface. Each of the eleven digit pads is a real 2x1
 * glyph whose first column carries the pad's own palette index and whose second column carries
 * index 1 -- the colour key PadImage_BlitEx is called with -- so a pad is identified by the colour
 * it leaves and the key column proves the keyed blit really skips it. */
#define FUEL_PAD_SLOT 25 /* 0x19, the slot RadarScreen_Init loads i0..i9 through (0x5FE9C) */

static PadImage g_aFuelPad[PUFO_RADAR_DIGIT_PADS];
static u8       g_aFuelGlyph[PUFO_RADAR_DIGIT_PADS][6];
static Ship     g_fuelCraft;
static u8       g_bOutOfFuel;
static RadarFuelEnv g_fuelEnv;

static void FuelFixture(void)
{
    memset(g_bdSurf.pPixels, 0, (size_t)g_bdSurf.nWidth * (size_t)g_bdSurf.nHeight * 2u);
    memset(g_aPalette, 0, sizeof(g_aPalette));
    for (i32 i = 0; i < PUFO_RADAR_DIGIT_PADS; ++i) {
        g_aFuelGlyph[i][0] = 2; g_aFuelGlyph[i][1] = 0;
        g_aFuelGlyph[i][2] = 1; g_aFuelGlyph[i][3] = 0;
        g_aFuelGlyph[i][4] = (u8)(3 + i);
        g_aFuelGlyph[i][5] = 1; /* the colour key */
        g_aPalette[FUEL_PAD_SLOT][3 + i] = (u32)(0x100 + i);
        memset(&g_aFuelPad[i], 0, sizeof(g_aFuelPad[i]));
        g_aFuelPad[i].pData   = g_aFuelGlyph[i];
        g_aFuelPad[i].nMode   = (u8)FUEL_PAD_SLOT;
        g_aFuelPad[i].uWidth  = 2;
        g_aFuelPad[i].uHeight = 1;
        g_aFuelPad[i].nOriginX = -1;
        g_aFuelPad[i].nOriginY = -1;
        g_fuelEnv.apDigitPads[i] = &g_aFuelPad[i];
    }
    memset(&g_fuelCraft, 0, sizeof(g_fuelCraft));
    g_fuelCraft.uFuelMax = 200;
    g_fuelCraft.nFuel    = 200;
    g_bOutOfFuel = 0;
    g_fuelEnv.pCraft      = &g_fuelCraft;
    g_fuelEnv.pbOutOfFuel = &g_bOutOfFuel;
}

static void TestRadarFuelGauge(void)
{
    printf("\nRadarScreen_DrawFuelGauge @ 0x5d918\n");

    /* --- a full tank: 100 - 100*1 = 0 consumed, so only the units digit and the percent --- */
    FuelFixture();
    CheckEqI32(RadarScreen_DrawFuelGauge(&g_fuelEnv), 0, "a full tank reads 0 consumed");
    CheckEqI32(g_bOutOfFuel, 0, "and the out-of-fuel flag stays clear");
    CheckEqI32(BdPix(162, 60), 0, "the hundreds digit is a leading zero and is not drawn");
    CheckEqI32(BdPix(172, 60), 0, "nor is the tens zero");
    CheckEqI32(BdPix(182, 60), 0x100, "the units digit is drawn at 182");
    CheckEqI32(BdPix(183, 60), 0, "its second column carries the key index and stays empty");
    CheckEqI32(BdPix(192, 60), 0x10A, "and the eleventh pad is the percent at 192");
    CheckEqI32(g_aFuelPad[0].nOriginX, 182, "pad 0 is moved three times and ends at 182");
    CheckEqI32(g_aFuelPad[10].nOriginX, 192, "the percent pad is moved to 192");
    CheckEqI32(g_aFuelPad[0].nOriginY, 60, "every pad lands on y 60");
    CheckEqI32(g_aFuelPad[10].nOriginY, 60, "including it");

    /* --- an empty tank: 100, clamped, flag set, all three digits drawn --- */
    FuelFixture();
    g_fuelCraft.nFuel = 0;
    CheckEqI32(RadarScreen_DrawFuelGauge(&g_fuelEnv), 100, "an empty tank reads 100 consumed");
    CheckEqI32(g_bOutOfFuel, 1, "and sets the out-of-fuel flag");
    CheckEqI32(BdPix(162, 60), 0x101, "the hundreds digit 1 is drawn at 162");
    CheckEqI32(BdPix(172, 60), 0x100, "the tens zero is drawn because a hundreds digit justifies it");
    CheckEqI32(BdPix(182, 60), 0x100, "and the units zero at 182");
    CheckEqI32(BdPix(192, 60), 0x10A, "with the percent pad");

    /* --- half a tank: 50, so the tens digit is real and the hundreds is dropped --- */
    FuelFixture();
    g_fuelCraft.nFuel = 100; /* uFuelMax 200 -> half gone */
    CheckEqI32(RadarScreen_DrawFuelGauge(&g_fuelEnv), 50, "half a tank reads 50 consumed");
    CheckEqI32(BdPix(162, 60), 0, "no hundreds digit");
    CheckEqI32(BdPix(172, 60), 0x105, "the tens digit 5 at 172");
    CheckEqI32(BdPix(182, 60), 0x100, "the units zero at 182");
    CheckEqI32(g_bOutOfFuel, 0, "and the flag stays clear");

    /* --- a single digit: 1 consumed --- */
    FuelFixture();
    g_fuelCraft.uFuelMax = 100;
    g_fuelCraft.nFuel    = 99;
    CheckEqI32(RadarScreen_DrawFuelGauge(&g_fuelEnv), 1, "one percent consumed");
    CheckEqI32(BdPix(172, 60), 0, "the tens zero is dropped when there is no hundreds digit");
    CheckEqI32(BdPix(182, 60), 0x101, "only the units digit is drawn");

    /* --- the clamp at 100, reached from a negative fuel --- */
    FuelFixture();
    g_fuelCraft.uFuelMax = 100;
    g_fuelCraft.nFuel    = -10;
    CheckEqI32(RadarScreen_DrawFuelGauge(&g_fuelEnv), 100, "110 clamps to 100");
    CheckEqI32(g_bOutOfFuel, 1, "and the clamp is what sets the flag");

    /* --- uFuelMax is read signed, like the original's LDRSH at 0x5D960 --- */
    FuelFixture();
    g_fuelCraft.uFuelMax = 0x8000; /* -32768 read signed, 32768 read unsigned */
    g_fuelCraft.nFuel    = 100;
    CheckEqI32(RadarScreen_DrawFuelGauge(&g_fuelEnv), 100,
               "the signed read gives 100.3 and clamps; an unsigned one would give 99");
    CheckEqI32(g_bOutOfFuel, 1, "so the clamp runs and the flag is set");

    /* --- a zero uFuelMax: the original divides by zero, the port lands on 100 --- */
    FuelFixture();
    g_fuelCraft.uFuelMax = 0;
    g_fuelCraft.nFuel    = 0;
    CheckEqI32(RadarScreen_DrawFuelGauge(&g_fuelEnv), 100, "a zero uFuelMax lands on 100");
    CheckEqI32(g_bOutOfFuel, 1, "and sets the flag");

    /* --- the guards --- */
    FuelFixture();
    CheckEqI32(RadarScreen_DrawFuelGauge(NULL), 0, "a null environment is refused");
    CheckEqI32(g_bOutOfFuel, 0, "and changes nothing");
    g_fuelEnv.pCraft = NULL;
    CheckEqI32(RadarScreen_DrawFuelGauge(&g_fuelEnv), 0, "a null craft is refused");
    CheckEqI32(BdPix(182, 60), 0, "with no pad drawn");
}

static StrategItem g_barItem;
static Ship        g_barCraft;
static i32         g_nRangeCur;
static i32         g_nPulseTimer;
static RadarRangeBarEnv g_barEnv;

static void BarFixture(void)
{
    memset(g_bdSurf.pPixels, 0, (size_t)g_bdSurf.nWidth * (size_t)g_bdSurf.nHeight * 2u);
    memset(&g_barItem, 0, sizeof(g_barItem));
    g_barItem.bCraftClass = 1;
    memset(&g_barCraft, 0, sizeof(g_barCraft));
    g_barCraft.nFuel = 500;
    g_nRangeCur   = 280; /* 114*280/560 = 57, so y 126 and height 42 */
    g_nPulseTimer = PUFO_RADAR_PULSE_ARMED;
    g_barEnv.pInterceptItem = &g_barItem;
    g_barEnv.pCraft         = &g_barCraft;
    g_barEnv.pnRangeCur     = &g_nRangeCur;
    g_barEnv.nRangeLimit    = 300;
    g_barEnv.nCraftLevel    = 13; /* the level term is zero at this level */
    g_barEnv.pnPulseTimer   = &g_nPulseTimer;
}

static void TestRadarRangeBar(void)
{
    printf("\nRadarScreen_DrawRangeBar @ 0x5d6e4\n");

    /* --- all three gates pass: two rects sharing a y and a height --- */
    BarFixture();
    Check(RadarScreen_DrawRangeBar(&g_barEnv), "all three gates pass");
    CheckEqI32(g_nPulseTimer, PUFO_RADAR_PULSE_NEXT, "and the pulse timer is rearmed to 230");
    CheckEqI32(BdPix(67, 126), 0x2B20, "the 3-wide bar starts at y = 183 - 57");
    CheckEqI32(BdPix(67, 167), 0x2B20, "and runs 57 - 15 = 42 rows");
    CheckEqI32(BdPix(67, 125), 0, "one row above it is empty");
    CheckEqI32(BdPix(67, 168), 0, "one row below it is empty");
    CheckEqI32(BdPix(69, 126), 0x2B20, "the bar is three columns wide");
    CheckEqI32(BdPix(70, 126), 0, "and no further");
    CheckEqI32(BdPix(68, 126), 0x56E0, "the 1-wide bar at x 68 is painted over it");
    CheckEqI32(BdPix(68, 167), 0x56E0, "for the same height");

    /* --- gate 1: a zero bCraftClass returns before anything --- */
    BarFixture();
    g_barItem.bCraftClass = 0;
    Check(!RadarScreen_DrawRangeBar(&g_barEnv), "a zero bCraftClass fails the first gate");
    CheckEqI32(g_nPulseTimer, PUFO_RADAR_PULSE_ARMED, "and leaves the pulse timer armed");
    CheckEqI32(BdPix(67, 126), 0, "drawing nothing");

    /* --- gate 3: the pulse timer must be exactly 255 --- */
    BarFixture();
    g_nPulseTimer = 254;
    Check(!RadarScreen_DrawRangeBar(&g_barEnv), "a pulse timer of 254 fails the third gate");
    CheckEqI32(g_nPulseTimer, 254, "and is left at 254");
    CheckEqI32(BdPix(67, 126), 0, "drawing nothing");

    /* --- gate 2: the level term loosens the limit, and it is always negative --- */
    BarFixture();
    g_nRangeCur = 320; /* 320 > 300 at level 13 */
    Check(!RadarScreen_DrawRangeBar(&g_barEnv), "a range above the limit fails the second gate");
    BarFixture();
    g_nRangeCur        = 320;
    g_barEnv.nCraftLevel = 8; /* 560*(8-13)/114 = -24, so 320 - 24 = 296 <= 300 */
    Check(RadarScreen_DrawRangeBar(&g_barEnv),
          "level 8 shifts the gate by -24 and the same range passes");
    CheckEqI32(BdPix(67, 126), 0x2B20, "drawing the bar");

    /* --- a zero range gives a negative height, which clips to nothing --- */
    BarFixture();
    g_nRangeCur = 0;
    Check(RadarScreen_DrawRangeBar(&g_barEnv), "a zero range still passes all three gates");
    CheckEqI32(BdPix(67, 183), 0, "but the height is -15 and nothing is drawn");
    CheckEqI32(BdPix(67, 182), 0, "not one row above the requested y either");

    /* --- the 70% roll and the per-class penalty, pinned seed by seed ---
     *
     * Seeds 1..4 draw a Game_RandU16(100) below 70 and so fire; 7013..7016 draw exactly 70 and so
     * do not. Both branches are therefore covered by fixed seeds rather than by a probability. */
    {
        static const i32 aSeeds[] = { 1, 2, 3, 4, 7013, 7014, 7015, 7016 };
        i32 nMismatch = 0;
        i32 nFired = 0;
        for (i32 i = 0; i < (i32)(sizeof(aSeeds) / sizeof(aSeeds[0])); ++i) {
            const i32 nSeed = aSeeds[i];
            srand((unsigned)nSeed);
            const u16 uRoll = Game_RandU16(100u);
            const u16 uPen  = Game_RandU16(8u);
            BarFixture();
            const i32 nBefore = g_barCraft.nFuel;
            srand((unsigned)nSeed); /* the port draws the same two rolls from the same seed */
            (void)RadarScreen_DrawRangeBar(&g_barEnv);
            const bool bFire = (uRoll < PUFO_RADAR_FIRE_CHANCE);
            const i32 nWant = bFire ? (i32)uPen - 20 : 0;
            if (bFire) {
                nFired += 1;
            }
            if ((i32)g_barCraft.nFuel != (i32)(i16)(nBefore + nWant)) {
                nMismatch += 1;
            }
        }
        CheckEqI32(nFired, 4, "four seeds draw a roll below 70 and four draw 70 or more");
        CheckEqI32(nMismatch, 0, "class 1's penalty is exactly rand(8) - 20 under the roll");
    }

    /* --- a class the switch does not know passes gate 1 but pays nothing --- */
    BarFixture();
    g_barItem.bCraftClass = 255; /* LDRSB makes this -1, which is non-zero */
    {
        const i32 nBefore = g_barCraft.nFuel;
        Check(RadarScreen_DrawRangeBar(&g_barEnv), "a 255 bCraftClass passes the first gate");
        CheckEqI32(g_barCraft.nFuel, nBefore, "but the switch default fires no penalty");
        CheckEqI32(g_nPulseTimer, PUFO_RADAR_PULSE_NEXT, "and the timer is still rearmed");
        CheckEqI32(BdPix(67, 126), 0x2B20, "with the bar drawn");
    }

    /* --- the guards --- */
    BarFixture();
    Check(!RadarScreen_DrawRangeBar(NULL), "a null environment is refused");
    CheckEqI32(g_nPulseTimer, PUFO_RADAR_PULSE_ARMED, "leaving the pulse timer armed");
    g_barEnv.pCraft = NULL;
    Check(!RadarScreen_DrawRangeBar(&g_barEnv), "a null craft is refused");
}

/* --- RadarScreen_Init @ 0x5f490 / RadarScreen_SetActive @ 0x6144c / RollDetection @ 0x602a0 ---
 *
 * These run against the shipped archives: every pad the original loads is really loaded, and the
 * radar message comes from the real lang.dat TEXTS blob. */

static RadarUiState g_radarUi;
static u8  g_bRadarActive, g_bRadarOutFuel, g_bRadarBlink, g_bRadarFlip, g_bRadarTrace;
static u8  g_bRadarFlag50, g_bRadarAnim;
static i32 g_nRadarMsgTimer, g_nRadarTargetHp, g_nRadarTargetHpMax, g_nRadarLevel;
static i32 g_nRadarRangeLimit, g_nRadarRangeTarget, g_nRadarRangeCur;
static i32 g_nRadarAmmoL, g_nRadarAmmoR, g_nRadarCurX, g_nRadarCurY;
static i32 g_nRadarShipIdx, g_nRadarStrategIdx;
static GameTime g_radarTime;
static i8 g_nRadarFrameStyle;
static StrategItem g_aRadarItems[8];
static PtrArray g_radarStrateg;
static bool g_bRadarListBuilt;
static Ship g_aRadarCraft[2];
static Ship *g_apRadarCraft[2] = { &g_aRadarCraft[0], &g_aRadarCraft[1] };
static RadarSessionEnv g_radarEnv;
static char g_szRadarMsg[108];
static u8 *g_pRadarTexts;

static void RadarClickNW(void) { }
static void RadarClickNE(void) { }
static void RadarClickSW(void) { }
static void RadarClickSE(void) { }
static void RadarClickBackdrop(void) { }

static bool RadarLoadCtlPads(UiControl *pCtl, const char *psz0, const char *psz1,
                             const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2);
    return true;
}

static void RadarSession_Reset(i32 nStrategIdx)
{
    i32 i;

    if (g_bRadarListBuilt) {
        PtrArray_Clear(&g_radarStrateg);
    }
    PtrArray_ctor(&g_radarStrateg);
    for (i = 0; i < 8; ++i) {
        PtrArray_Add(&g_radarStrateg, &g_aRadarItems[i]);
    }
    g_bRadarListBuilt = true;

    memset(&g_radarUi, 0, sizeof(g_radarUi));
    for (i = 0; i < 8; ++i) {
        memset(&g_aRadarItems[i], 0, sizeof(g_aRadarItems[i]));
        g_aRadarItems[i].bCraftClass = (u8)i;
        g_aRadarItems[i].nHp = PUFO_STRATEG_HP_FRESH;
        g_aRadarItems[i].dSpeed = 1.5;
        g_aRadarItems[i].dTrackA = 1.5;
    }

    /* Every scalar starts at a value the ported code is not supposed to leave behind. */
    g_bRadarActive = 0; g_bRadarOutFuel = 1; g_bRadarBlink = 1; g_bRadarFlip = 1;
    g_bRadarTrace = 0; g_bRadarFlag50 = 1; g_bRadarAnim = 1;
    g_nRadarMsgTimer = -1; g_nRadarTargetHp = -1; g_nRadarTargetHpMax = -1;
    g_nRadarLevel = -1; g_nRadarRangeLimit = -1; g_nRadarRangeTarget = -1;
    g_nRadarRangeCur = -1; g_nRadarAmmoL = -999; g_nRadarAmmoR = -999;
    g_nRadarCurX = -1; g_nRadarCurY = -1;
    g_nRadarShipIdx = 0; g_nRadarStrategIdx = nStrategIdx;
    memset(&g_radarTime, 0, sizeof(g_radarTime));
    g_nRadarFrameStyle = -1;

    memset(g_aRadarCraft, 0, sizeof(g_aRadarCraft));
    g_aRadarCraft[0].bWeaponL = 25; g_aRadarCraft[0].bWeaponR = 26;
    g_aRadarCraft[0].bAmmo25 = 7;  g_aRadarCraft[0].bAmmo26 = 3;
    g_aRadarCraft[0].uFuelCap = 600;

    memset(&g_radarEnv, 0, sizeof(g_radarEnv));
    g_radarEnv.pUi = &g_radarUi;
    g_radarEnv.pbActive = &g_bRadarActive;
    g_radarEnv.pbOutOfFuel = &g_bRadarOutFuel;
    g_radarEnv.pbBlinkPhase = &g_bRadarBlink;
    g_radarEnv.pbFlip = &g_bRadarFlip;
    g_radarEnv.pbTraceLost = &g_bRadarTrace;
    g_radarEnv.pbFlag159650 = &g_bRadarFlag50;
    g_radarEnv.pbAnimStarted = &g_bRadarAnim;
    g_radarEnv.pnMsgTimer = &g_nRadarMsgTimer;
    g_radarEnv.pnTargetHp = &g_nRadarTargetHp;
    g_radarEnv.pnTargetHpMax = &g_nRadarTargetHpMax;
    g_radarEnv.pnCraftLevel = &g_nRadarLevel;
    g_radarEnv.pnRangeLimit = &g_nRadarRangeLimit;
    g_radarEnv.pnRangeTarget = &g_nRadarRangeTarget;
    g_radarEnv.pnRangeCur = &g_nRadarRangeCur;
    g_radarEnv.pnAmmoL = &g_nRadarAmmoL;
    g_radarEnv.pnAmmoR = &g_nRadarAmmoR;
    g_radarEnv.pnCursorX = &g_nRadarCurX;
    g_radarEnv.pnCursorY = &g_nRadarCurY;
    g_radarEnv.pnShipIdx = &g_nRadarShipIdx;
    g_radarEnv.pnStrategIdx = &g_nRadarStrategIdx;
    g_radarEnv.apCraft = g_apRadarCraft;
    g_radarEnv.nCraftCount = 2;
    g_radarEnv.pStrategList = &g_radarStrateg;
    g_radarEnv.pTime = &g_radarTime;
    g_radarEnv.pnFrameStyle = &g_nRadarFrameStyle;
    g_radarEnv.apfnClick[0] = RadarClickNW;
    g_radarEnv.apfnClick[1] = RadarClickNE;
    g_radarEnv.apfnClick[2] = RadarClickSW;
    g_radarEnv.apfnClick[3] = RadarClickSE;
    g_radarEnv.apfnClick[4] = RadarClickBackdrop;
    g_radarEnv.pfnLoadPads = RadarLoadCtlPads;

    memset(g_szRadarMsg, 0, sizeof(g_szRadarMsg));
}

/* True when pPad holds exactly the bytes of "<pszName>.pad". PadImage_Load keeps the whole member,
 * which is the 4-byte dimension header plus uWidth * uHeight index bytes. */
static bool RadarPadIs(const PadImage *pPad, const char *pszName)
{
    PadImage ref;
    bool bSame;
    size_t cb;

    PadImage_ctor(&ref);
    if (!PadImage_Load(&ref, pszName, pPad->nMode)) {
        PadImage_FreeData(&ref);
        return false;
    }
    cb = 4u + (size_t)ref.uWidth * (size_t)ref.uHeight;
    bSame = pPad->pData != NULL
         && pPad->uWidth == ref.uWidth && pPad->uHeight == ref.uHeight
         && memcmp(pPad->pData, ref.pData, cb) == 0;
    PadImage_FreeData(&ref);
    return bSame;
}

static bool RadarSlotIs(i32 nSlot, const char *pszPal)
{
    i32 i;
    Palette_Load(pszPal, (u8)(nSlot + 30)); /* scratch slot, 34..39 for the 1..9 range used here */
    for (i = 0; i < PUFO_PAL_SIZE; ++i) {
        if (g_aPalette[nSlot][i] != g_aPalette[nSlot + 30][i]) {
            return false;
        }
    }
    return true;
}

static void TestRadarSession(void)
{
    static const i32 anLevel[8] = { 8, 6, 6, 4, 4, 2, 2, 0 };
    static const i32 anHp[8] = { 50, 200, 250, 500, 500, 1200, 2200, 3000 };
    static const i32 anLimit[8] = { 240, 360, 360, 400, 400, 480, 480, 480 };
    static const char *const apszArt[8] = {
        "ufo_4", "ufo_3", "ufo_3", "ufo_2", "ufo_2", "ufo_1", "ufo_1", "ufo_0"
    };
    static const char *const apszPal[8] = {
        "85.pal", "86.pal", "87.pal", "88.pal", "89.pal", "90.pal", "92.pal", "91.pal"
    };
    static const char *const apszOverlay[8] = { "85", "86", "87", "88", "89", "90", "92", "91" };
    /* The five origins RadarScreen_Init writes at 0x5FDB8..0x5FE3C, restated from the IDB. */
    static const i32 anButtonX[PUFO_RADAR_BUTTON_PADS] = { 117, 161, 117, 161, 117 };
    static const i32 anButtonY[PUFO_RADAR_BUTTON_PADS] = {  86,  86, 107, 107, 128 };
    static const char *const apszArchive[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    i32 nClass;
    i32 i;

    printf("RadarScreen_Init @ 0x5f490 (open path)\n");
    if (!Asset_OpenArchives(apszArchive)) {
        Check(false, "the shipped archives opened");
        return;
    }

    /* The palette state the copy at 0x5FC34 works on, set up the way Geoscape_InitUI leaves it. */
    Palette_Load("all_btn.pal", PUFO_RADAR_PAL_SRC_SLOT);
    Palette_Load("ufoflash.pal", 27);
    Palette_Load("funding.pal", 28);
    Palette_Load("menu.pal", PUFO_RADAR_PAL_DST_SLOT);
    Palette_Load("cic.pal", 2);

    RadarSession_Reset(3); /* bCraftClass 3 -> ufo_2, level 4, hp 500, 88.pal, limit 400 */
    RadarScreen_Init(&g_radarEnv);

    CheckEqI32(g_bRadarActive, 1, "the open path sets bRadarScreenActive");
    CheckEqI32(g_bRadarOutFuel, 0, "bRadarCraftOutOfFuel cleared (0x5F4B0)");
    CheckEqI32(g_bRadarBlink, 0, "bRadarBlinkPhase cleared (0x5F4BC)");
    CheckEqI32(g_bRadarFlip, 0, "bRadarFlip cleared (0x5F4C8)");
    CheckEqI32(g_bRadarTrace, 0, "bRadarTraceLost cleared (0x5F4D4)");
    CheckEqI32(g_bRadarFlag50, 0, "unk_159650 cleared (0x5F4E0)");
    CheckEqI32(g_bRadarAnim, 0, "bUiAnimStarted cleared (0x5FEFC)");
    CheckEqI32(g_nRadarMsgTimer, 255, "nRadarMsgTimer starts at 255 (0x5F60C)");
    CheckEqI32(g_nRadarRangeTarget, 560, "nRadarRangeTarget starts full (0x5FC10)");
    CheckEqI32(g_nRadarRangeCur, 560, "nRadarRangeCur starts full (0x5FC18)");
    CheckEqI32(g_nRadarCurX, 117, "the radar cursor starts at the NW corner x");
    CheckEqI32(g_nRadarCurY, 86, "and the NW corner y");

    CheckEqI32(g_nRadarLevel, 4, "class 3 gives nRadarCraftLevel 4");
    CheckEqI32(g_nRadarTargetHp, 500, "class 3 gives 500 target HP");
    CheckEqI32(g_nRadarTargetHpMax, 500, "and the same in nInterceptTargetHpMax");
    CheckEqI32(g_nRadarRangeLimit, 400, "class 3 gives a 400 range limit");
    CheckEqI32(g_aRadarItems[3].nHp, 500, "a fresh contact (-1) adopts the table HP (0x5FAB4)");

    Check(g_radarUi.pFramePad != NULL && RadarPadIs(g_radarUi.pFramePad, "radar"),
          "the frame pad really is radar.pad");
    Check(g_radarUi.pFramePad != NULL && g_radarUi.pFramePad->nMode == PUFO_RADAR_FRAME_MODE
          && g_radarUi.pFramePad->uWidth == 85 && g_radarUi.pFramePad->uHeight == 114,
          "radar.pad is 85x114 in palette slot 22");
    Check(g_radarUi.pFramePad != NULL && g_radarUi.pFramePad->nOriginX == 26
          && g_radarUi.pFramePad->nOriginY == 56, "the frame pad sits at (26, 56)");
    Check(g_radarUi.pUfoPad != NULL && RadarPadIs(g_radarUi.pUfoPad, "ufo_2"),
          "class 3 loads ufo_2 into pRadarUfoPad");
    Check(g_radarUi.pUfoPad != NULL && g_radarUi.pUfoPad->nMode == PUFO_RADAR_UFO_MODE
          && g_radarUi.pUfoPad->nOriginX == 55, "the ufo pad is mode 23 at x 55");
    Check(g_radarUi.pUfoFlashPad != NULL && RadarPadIs(g_radarUi.pUfoFlashPad, "ufo_2f")
          && g_radarUi.pUfoFlashPad->nMode == PUFO_RADAR_FLASH_MODE,
          "ufo_2f into pRadarUfoFlashPad, mode 27");
    Check(g_radarUi.pUfoMissilePad != NULL && RadarPadIs(g_radarUi.pUfoMissilePad, "ufo_2m")
          && g_radarUi.pUfoMissilePad->nMode == PUFO_RADAR_FLASH_MODE,
          "ufo_2m into pRadarUfoMissilePad, mode 27");
    Check(g_radarUi.pOverlayPad != NULL && RadarPadIs(g_radarUi.pOverlayPad, "88"),
          "class 3 loads the 88 overlay pad");
    Check(g_radarUi.pOverlayPad != NULL && g_radarUi.pOverlayPad->uWidth == 154
          && g_radarUi.pOverlayPad->nOriginX == 115 - (154 >> 1)
          && g_radarUi.pOverlayPad->nOriginY == 180,
          "the overlay is centred as 115 - (uWidth >> 1), y 180 (0x5FA48)");
    Check(RadarSlotIs(17, "88.pal"), "class 3 loads 88.pal into palette slot 17");

    CheckEqI32(g_nRadarAmmoL, 7, "mount 25 draws 7 from Ship.bAmmo25");
    CheckEqI32(g_nRadarAmmoR, 3, "mount 26 draws 3 from Ship.bAmmo26");

    for (i = 0; i < PUFO_RADAR_CONTROLS; ++i) {
        Check(g_radarUi.apCtl[i] != NULL && g_radarUi.apCtl[i]->pfnClick == g_radarEnv.apfnClick[i],
              "each radar control gets its click handler at +0xC");
    }
    Check(g_radarUi.apCtl[0] != NULL && g_radarUi.apCtl[0]->apPads[0] != NULL
          && RadarPadIs(g_radarUi.apCtl[0]->apPads[0], "i_but0")
          && g_radarUi.apCtl[0]->apPads[1] != NULL
          && RadarPadIs(g_radarUi.apCtl[0]->apPads[1], "i_but1"),
          "UiControl_Setup loads the descriptor's pad0 and pad1 into slots 0 and 1");
    Check(g_radarUi.apCtl[4] != NULL && g_radarUi.apCtl[4]->apPads[0] != NULL
          && RadarPadIs(g_radarUi.apCtl[4]->apPads[0], "i_but7"),
          "the fifth control's descriptor names i_but7");

    for (i = 0; i < PUFO_RADAR_BUTTON_PADS; ++i) {
        Check(g_radarUi.apButtonPad[i] != NULL
              && g_radarUi.apButtonPad[i]->nMode == PUFO_RADAR_BUTTON_MODE
              && g_radarUi.apButtonPad[i]->nOriginX == anButtonX[i]
              && g_radarUi.apButtonPad[i]->nOriginY == anButtonY[i],
              "button pad sits on its descriptor origin, in mode 26");
    }
    Check(g_radarUi.apButtonPad[0] != NULL && RadarPadIs(g_radarUi.apButtonPad[0], "i_but3"),
          "button pad 0 is i_but3");
    Check(g_radarUi.apButtonPad[1] != NULL && RadarPadIs(g_radarUi.apButtonPad[1], "i_but4"),
          "button pad 1 is i_but4");
    Check(g_radarUi.apButtonPad[2] != NULL && RadarPadIs(g_radarUi.apButtonPad[2], "i_but2"),
          "button pad 2 is i_but2");
    Check(g_radarUi.apButtonPad[3] != NULL && RadarPadIs(g_radarUi.apButtonPad[3], "i_but5"),
          "button pad 3 is i_but5");
    Check(g_radarUi.apButtonPad[4] != NULL && RadarPadIs(g_radarUi.apButtonPad[4], "i_but6"),
          "button pad 4 is i_but6");

    for (i = 0; i < PUFO_RADAR_DIGIT_PADS; ++i) {
        Check(g_radarUi.apDigitPad[i] != NULL
              && g_radarUi.apDigitPad[i]->nMode == PUFO_RADAR_DIGIT_MODE,
              "digit pad exists in mode 25");
    }
    Check(g_radarUi.apDigitPad[0] != NULL && RadarPadIs(g_radarUi.apDigitPad[0], "i0")
          && g_radarUi.apDigitPad[0]->uWidth == 8 && g_radarUi.apDigitPad[0]->uHeight == 13,
          "digit pad 0 is i0, 8x13");
    Check(g_radarUi.apDigitPad[10] != NULL && RadarPadIs(g_radarUi.apDigitPad[10], "perc")
          && g_radarUi.apDigitPad[10]->uWidth == 13 && g_radarUi.apDigitPad[10]->uHeight == 14,
          "the eleventh digit pad is perc, 13x14");

    /* The 766-dword copy at 0x5FC34, and the slots it runs over. */
    Check(RadarSlotIs(1, "all_btn.pal"), "slot 1 (menu.pal) is overwritten with all_btn.pal");
    Check(RadarSlotIs(2, "ufoflash.pal"),
          "ORIGINAL BUG: the overrun writes ufoflash.pal over all of slot 2");
    Check(RadarSlotIs(3, "base.pal"), "slot 3 is repaired by Palette_Load(\"base.pal\", 3)");

    /* ------------------------------------------------- the close path */
    RadarScreen_Init(&g_radarEnv); /* the flag is 1, so this is the teardown */

    CheckEqI32(g_bRadarActive, 0, "the close path clears bRadarScreenActive (0x60148)");
    Check(g_radarUi.pFramePad == NULL && g_radarUi.pUfoPad == NULL
          && g_radarUi.pUfoFlashPad == NULL && g_radarUi.pUfoMissilePad == NULL
          && g_radarUi.pOverlayPad == NULL, "all five single pads are freed and NULLed");
    for (i = 0; i < PUFO_RADAR_BUTTON_PADS; ++i) {
        Check(g_radarUi.apButtonPad[i] == NULL, "button pad freed");
    }
    for (i = 0; i < PUFO_RADAR_DIGIT_PADS; ++i) {
        Check(g_radarUi.apDigitPad[i] == NULL, "digit pad freed");
    }
    for (i = 0; i < PUFO_RADAR_CONTROLS; ++i) {
        Check(g_radarUi.apCtl[i] == NULL, "control freed");
    }
    CheckEqI32(g_aRadarCraft[0].bAmmo25, 7, "mount 25 gets its own 7 back (0x60098..0x600D0)");
    CheckEqI32(g_aRadarCraft[0].bAmmo26, 3, "mount 26 gets its own 3 back");
    Check(RadarSlotIs(1, "menu.pal"), "slot 1 is restored to menu.pal (0x60290)");
    Check(RadarSlotIs(2, "ufoflash.pal"),
          "ORIGINAL BUG: nothing ever restores slot 2, it stays ufoflash.pal after the close");

    /* ------------------------------------------------- the class table, all eight rows */
    for (nClass = 0; nClass < 8; ++nClass) {
        RadarSession_Reset(nClass);
        RadarScreen_Init(&g_radarEnv);
        CheckEqI32(g_nRadarLevel, anLevel[nClass], "class level");
        CheckEqI32(g_nRadarTargetHp, anHp[nClass], "class target HP");
        CheckEqI32(g_nRadarRangeLimit, anLimit[nClass], "class range limit");
        Check(g_radarUi.pUfoPad != NULL && RadarPadIs(g_radarUi.pUfoPad, apszArt[nClass]),
              "class ufo art");
        Check(g_radarUi.pOverlayPad != NULL && RadarPadIs(g_radarUi.pOverlayPad, apszOverlay[nClass]),
              "class overlay pad");
        Check(RadarSlotIs(17, apszPal[nClass]), "class palette in slot 17");
        RadarScreen_Init(&g_radarEnv);
    }

    /* ------------------------------------------------- the HP re-sync with a contact that already fought */
    RadarSession_Reset(1);
    g_aRadarItems[1].nHp = 123;
    RadarScreen_Init(&g_radarEnv);
    CheckEqI32(g_nRadarTargetHp, 123, "a contact with HP 123 replaces the table's 200 (0x5FA9C)");
    CheckEqI32(g_nRadarTargetHpMax, 123, "nInterceptTargetHpMax follows it (0x5FAA8)");
    CheckEqI32(g_aRadarItems[1].nHp, 123, "the item keeps its own HP");
    RadarScreen_Init(&g_radarEnv);

    /* ------------------------------------------------- the twin-mount split and its write-back */
    RadarSession_Reset(0);
    g_aRadarCraft[0].bWeaponR = 25;
    g_aRadarCraft[0].bAmmo25 = 7;
    RadarScreen_Init(&g_radarEnv);
    CheckEqI32(g_nRadarAmmoR, 3, "a twin mount splits 7 as 7/2 on the right (0x5FBE8)");
    CheckEqI32(g_nRadarAmmoL, 4, "and 7/2 + 7%2 on the left (0x5FBFC..0x5FC04)");
    RadarScreen_Init(&g_radarEnv);
    CheckEqI32(g_aRadarCraft[0].bAmmo25, 7, "the split sums back to 7 on close (0x60144)");
    CheckEqI32(g_aRadarCraft[0].bAmmo26, 3, "and the unused mount 26 byte is untouched");

    RadarSession_Reset(0);
    g_aRadarCraft[0].bWeaponL = 25; g_aRadarCraft[0].bWeaponR = 25;
    g_aRadarCraft[0].bAmmo25 = 6;
    RadarScreen_Init(&g_radarEnv);
    CheckEqI32(g_nRadarAmmoL, 3, "an even pool splits evenly");
    CheckEqI32(g_nRadarAmmoR, 3, "on both sides");
    RadarScreen_Init(&g_radarEnv);

    RadarSession_Reset(0);
    g_aRadarCraft[0].bWeaponL = 28; g_aRadarCraft[0].bWeaponR = 28;
    g_aRadarCraft[0].bAmmo25 = 9;
    RadarScreen_Init(&g_radarEnv);
    CheckEqI32(g_nRadarAmmoL, PUFO_RADAR_AMMO_UNLIMITED, "mount kind 28 is the -1 unlimited mount");
    CheckEqI32(g_nRadarAmmoR, PUFO_RADAR_AMMO_UNLIMITED, "on both mounts");
    RadarScreen_Init(&g_radarEnv);
    CheckEqI32(g_aRadarCraft[0].bAmmo25, 9, "kind 28 writes nothing back (0x60148 path)");

    RadarSession_Reset(0);
    g_aRadarCraft[0].bWeaponL = 99; g_aRadarCraft[0].bWeaponR = 25;
    RadarScreen_Init(&g_radarEnv);
    CheckEqI32(g_nRadarAmmoL, PUFO_RADAR_AMMO_NONE, "an unknown mount kind keeps the -2 sentinel");
    CheckEqI32(g_nRadarAmmoR, 7, "while the other mount still draws its own ammo byte");
    RadarScreen_Init(&g_radarEnv);

    /* An out-of-range class leaves the whole table half alone. */
    RadarSession_Reset(0);
    g_aRadarItems[0].bCraftClass = 200; /* (i8)200 == -56 */
    RadarScreen_Init(&g_radarEnv);
    CheckEqI32(g_nRadarLevel, -1, "an out-of-range class writes no level");
    CheckEqI32(g_nRadarTargetHp, 0, "and no target HP, it stays the 0 the open path stored");
    Check(g_radarUi.pUfoPad != NULL && g_radarUi.pUfoPad->pData == NULL,
          "and no ufo art is loaded");
    RadarScreen_Init(&g_radarEnv);

    /* ------------------------------------------------- RadarScreen_SetActive @ 0x6144c */
    RadarSession_Reset(5);
    RadarScreen_SetActive(&g_radarEnv, 1);
    CheckEqI32(g_bRadarActive, 1, "SetActive(1) opens through Init");
    CheckEqI32(g_radarTime.bPaused, 1, "and pauses the game clock (0x61464)");
    CheckEqI32(g_nRadarFrameStyle, 1, "and picks the bright panel frame (0x6148C)");
    CheckEqI32(g_nRadarTargetHp, 1200, "class 5 is the one SetActive was pointed at");
    RadarScreen_SetActive(&g_radarEnv, 0);
    CheckEqI32(g_bRadarActive, 0, "SetActive(0) closes through the same Init call");
    CheckEqI32(g_radarTime.bPaused, 0, "and resumes the clock (0x61480)");
    CheckEqI32(g_nRadarFrameStyle, 0, "and clears the frame style");
    Check(g_radarUi.pOverlayPad == NULL, "the teardown really ran");

    RadarSession_Reset(0);
    RadarScreen_Init(NULL);
    CheckEqI32(g_bRadarActive, 0, "a null environment is refused");

    Asset_CloseArchives();
}

/* lang.dat TEXTS[1599], the string RadarScreen_RollDetection copies into szRadarMsg. */
static const char g_szTraceLost[] = "\xD1\xEB\xE5\xE4 \xCD\xCB\xCE \xEF\xEE\xF2\xE5\xF0\xFF\xED";

static bool RadarLoadTextBlob(void)
{
    static const char *const apszArchive[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    AssetReader rdr;
    u8 aHdr[PUFO_TEXT_HDR_SIZE];
    u32 cb, n, i;

    if (!Asset_OpenArchives(apszArchive)) {
        return false;
    }
    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, "texts.dat")) {
        Asset_CloseArchives();
        return false;
    }
    cb = AssetReader_Size(&rdr);
    n = (cb >= PUFO_TEXT_HDR_SIZE)
      ? (cb - PUFO_TEXT_HDR_SIZE) / PUFO_TEXT_REC_SIZE_DISK : 0u;
    if (n <= (u32)PUFO_RADAR_TEXT_TRACE_LOST) {
        Asset_CloseArchives();
        return false;
    }
    if (!AssetReader_Read(&rdr, aHdr, PUFO_TEXT_HDR_SIZE)) {
        Asset_CloseArchives();
        return false;
    }
    g_pRadarTexts = malloc((size_t)n * PUFO_TEXT_REC_STRIDE);
    if (g_pRadarTexts == NULL) {
        Asset_CloseArchives();
        return false;
    }
    memset(g_pRadarTexts, 0, (size_t)n * PUFO_TEXT_REC_STRIDE);
    for (i = 0; i < n; ++i) {
        if (!AssetReader_Read(&rdr, g_pRadarTexts + (size_t)i * PUFO_TEXT_REC_STRIDE,
                              PUFO_TEXT_REC_SIZE_DISK)) {
            break;
        }
    }
    return true;
}

static RadarDetectEnv g_detectEnv;

static void RadarDetect_Reset(i32 nClass, i32 nFuelCap)
{
    RadarSession_Reset(nClass);
    g_aRadarCraft[0].uFuelCap = (u16)nFuelCap;
    g_bRadarTrace = 0;
    g_bRadarFlip = 0;
    g_nRadarRangeTarget = 100;
    g_nRadarMsgTimer = 7;

    memset(&g_detectEnv, 0, sizeof(g_detectEnv));
    g_detectEnv.pStrategList = &g_radarStrateg;
    g_detectEnv.pnStrategIdx = &g_nRadarStrategIdx;
    g_detectEnv.apCraft = g_apRadarCraft;
    g_detectEnv.nCraftCount = 2;
    g_detectEnv.pnShipIdx = &g_nRadarShipIdx;
    g_detectEnv.pbTraceLost = &g_bRadarTrace;
    g_detectEnv.pbFlip = &g_bRadarFlip;
    g_detectEnv.pnRangeTarget = &g_nRadarRangeTarget;
    g_detectEnv.pnMsgTimer = &g_nRadarMsgTimer;
    g_detectEnv.pTextRecs = g_pRadarTexts;
    g_detectEnv.pszMsg = g_szRadarMsg;
    g_detectEnv.cchMsg = sizeof(g_szRadarMsg);
}

static void TestRadarDetect(void)
{
    const char *pszRec;
    u8 bGot;

    printf("RadarScreen_RollDetection @ 0x602a0\n");
    if (!RadarLoadTextBlob()) {
        Check(false, "the lang.dat TEXTS blob loaded");
        return;
    }

    pszRec = (const char *)(g_pRadarTexts + (size_t)PUFO_RADAR_TEXT_TRACE_LOST * PUFO_TEXT_REC_STRIDE);
    Check(strcmp(pszRec, g_szTraceLost) == 0, "lang.dat TEXTS[1599] is the trace-lost message");
    Check(strcmp((const char *)(g_pRadarTexts + 1598 * PUFO_TEXT_REC_STRIDE), g_szTraceLost) != 0,
          "TEXTS[1598] is a different string, so the index is not off by one");
    Check(strcmp((const char *)(g_pRadarTexts + 1600 * PUFO_TEXT_REC_STRIDE), g_szTraceLost) != 0,
          "and so is TEXTS[1600]");

    /* Game_RandU16(1) is always 0, so nChance 1 always takes the roll's success branch. */
    RadarDetect_Reset(2, 600); /* class 2 -> 45.0; the craft runs at 600/60 = 10 */
    bGot = RadarScreen_RollDetection(&g_detectEnv, 1);
    CheckEqI32(bGot, 1, "an outrunning contact sets the result byte");
    Check(g_aRadarItems[2].dSpeed == 45.0, "class 2 writes 45.0 into StrategItem.dSpeed");
    Check(g_aRadarItems[2].dTrackA == 45.0 * PUFO_RADAR_SPEED_TRACK_A,
          "and dTrackA = dSpeed * 0.02 (0x60448)");
    CheckEqI32(g_bRadarFlip, 1, "bRadarFlip is set (0x604D4)");
    CheckEqI32(g_bRadarTrace, 1, "bRadarTraceLost is set (0x604E0)");
    CheckEqI32(g_nRadarRangeTarget, 560, "nRadarRangeTarget jumps to full scale (0x604E4)");
    CheckEqI32(g_nRadarMsgTimer, 0, "the message timer restarts (0x60530)");
    Check(strcmp(g_szRadarMsg, g_szTraceLost) == 0, "szRadarMsg holds the trace-lost message");

    /* The craft is faster this time: 6000/60 = 100, which is not below 45.0. */
    RadarDetect_Reset(2, 6000);
    bGot = RadarScreen_RollDetection(&g_detectEnv, 1);
    CheckEqI32(bGot, 0, "a craft that keeps up leaves the flag clear");
    Check(g_aRadarItems[2].dSpeed == 45.0, "but the speed rewrite already happened (0x602E0)");
    CheckEqI32(g_bRadarFlip, 0, "bRadarFlip stays clear");
    CheckEqI32(g_nRadarRangeTarget, 100, "nRadarRangeTarget is untouched");
    CheckEqI32(g_nRadarMsgTimer, 7, "and so is the message timer");
    Check(g_szRadarMsg[0] == 0, "and no message is copied");

    /* The boundary: class 0 is 36.6666, and 2200/60 is 36.666666..., which is NOT below it. */
    RadarDetect_Reset(0, 2200);
    CheckEqI32(RadarScreen_RollDetection(&g_detectEnv, 1), 0,
               "2200 fuel is just over the class 0 speed, so no trace loss");
    RadarDetect_Reset(0, 2199);
    CheckEqI32(RadarScreen_RollDetection(&g_detectEnv, 1), 1,
               "2199 fuel is just under it, and the trace is lost");

    /* A trace that was already lost still re-posts the message, without touching the flags. */
    RadarDetect_Reset(2, 6000);
    g_bRadarTrace = 1;
    g_nRadarRangeTarget = 100;
    bGot = RadarScreen_RollDetection(&g_detectEnv, 1);
    CheckEqI32(bGot, 1, "an already-lost trace reports itself lost (0x6051C)");
    CheckEqI32(g_nRadarRangeTarget, 100, "but the outrun branch did not run");
    CheckEqI32(g_nRadarMsgTimer, 0, "the message is posted anyway (0x60530)");
    Check(strcmp(g_szRadarMsg, g_szTraceLost) == 0, "with the same text");

    /* A failing roll skips the whole speed half. Game_RandU16(100) is 23 for seed 12345. */
    RadarDetect_Reset(2, 600);
    srand(12345);
    bGot = RadarScreen_RollDetection(&g_detectEnv, 100);
    CheckEqI32(bGot, 0, "a non-zero roll skips to the flag check (0x602C0)");
    Check(g_aRadarItems[2].dSpeed == 1.5, "dSpeed is not rewritten on a failed roll");
    Check(g_aRadarItems[2].dTrackA == 1.5, "nor is dTrackA");
    CheckEqI32(g_nRadarMsgTimer, 7, "and no message is posted");

    /* An out-of-range class keeps the item's own speed and still scales dTrackA. */
    RadarDetect_Reset(0, 600);
    g_aRadarItems[0].bCraftClass = 200;
    g_aRadarItems[0].dSpeed = 10.0;
    bGot = RadarScreen_RollDetection(&g_detectEnv, 1);
    Check(g_aRadarItems[0].dSpeed == 10.0, "an out-of-range class writes no speed (0x602E0 default)");
    Check(g_aRadarItems[0].dTrackA == 10.0 * PUFO_RADAR_SPEED_TRACK_A,
          "but dTrackA is still derived from it");
    CheckEqI32(bGot, 0, "and 600/60 == 10.0 is not below 10.0, so the trace is not lost");

    Check(RadarScreen_RollDetection(NULL, 1) == 0, "a null environment rolls nothing");

    free(g_pRadarTexts);
    g_pRadarTexts = NULL;
    Asset_CloseArchives();
}

/* ---- RadarScreen_DrawCraftArmament @ 0x5edf4 -------------------------------------------
 *
 * This one runs on the real font: the shipped font.dat and basebits.dat are loaded, so the mount
 * icons and the ammo labels are the original's own sprites, and every assertion below reads pixels
 * out of a real 240x320 surface. */

static Ship              g_armCraft;
static Ship             *g_apArmCraft[1];
static i32               g_nArmShipIdx, g_nArmRangeCur, g_nArmLevel;
static i32               g_nArmBoundL, g_nArmBoundR, g_nArmCoolL, g_nArmCoolR;
static i32               g_nArmShotL, g_nArmShotR, g_nArmAmmoL, g_nArmAmmoR;
static u8                g_bArmEngageL, g_bArmEngageR;
static RadarArmamentEnv  g_armEnv;
static bool              g_bArmReady;

#define ARM_BAR_COLOR 0x7E45 /* 0x78 / 0xC8 / 0x28 through SoftSurf_BlitRectProxy's pack */
#define ARM_ICON_W    15     /* measured: every basebits mount icon is 15 x 17 */
#define ARM_ICON_H    17

/* The icon box of one mount, on the surface as it is right now. */
static u32 ArmIconSignature(i32 nX0)
{
    u32 uHash = 2166136261u;
    for (i32 y = 0; y < ARM_ICON_H; ++y) {
        for (i32 x = 0; x < ARM_ICON_W; ++x) {
            uHash = (uHash ^ (u32)BdPix(nX0 + x, PUFO_RADAR_MOUNT_GLYPH_Y + y)) * 16777619u;
        }
    }
    return uHash;
}

/* The same box after drawing one basebits frame straight onto a cleared surface. This CLEARS the
 * surface, so every reference value has to be taken before the call under test. The glyph base is
 * pinned to 3 first because that is the state the original is in when it draws the icon. */
static u32 ArmGlyphSignature(i32 nX0, u16 nGlyphOff)
{
    memset(g_bdSurf.pPixels, 0, (size_t)g_bdSurf.nWidth * (size_t)g_bdSurf.nHeight * 2u);
    g_nGlyphPalSlotBase = PUFO_RADAR_MOUNT_PAL_BASE;
    Font_DrawGlyph(nX0, PUFO_RADAR_MOUNT_GLYPH_Y,
                   (u16)(g_fontSys.nBaseBitsBase + nGlyphOff), 0);
    return ArmIconSignature(nX0);
}

static i32 ArmCount(i32 x0, i32 y0, i32 x1, i32 y1, i32 nColor)
{
    i32 n = 0;
    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            if (BdPix(x, y) == nColor) {
                ++n;
            }
        }
    }
    return n;
}

static i32 ArmAny(i32 x0, i32 y0, i32 x1, i32 y1)
{
    i32 n = 0;
    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            if (BdPix(x, y) != 0) {
                ++n;
            }
        }
    }
    return n;
}

/* The leftmost painted column of the ammo-label band, which sits below both icons. */
static i32 ArmLabelLeftmost(i32 x0, i32 x1)
{
    for (i32 x = x0; x < x1; ++x) {
        for (i32 y = 154; y < 170; ++y) {
            if (BdPix(x, y) != 0) {
                return x;
            }
        }
    }
    return -1;
}

static void ArmFixture(i8 nMountL, i8 nMountR, i32 nAmmoL, i32 nAmmoR,
                       i32 nRangeCur, i32 nLevel)
{
    memset(g_bdSurf.pPixels, 0, (size_t)g_bdSurf.nWidth * (size_t)g_bdSurf.nHeight * 2u);
    memset(g_aPalette, 0, sizeof(g_aPalette));

    /* Slot 3 is the glyph palette the icons use (Map_nGlyphPalBase = 3, nPal = 0) and slot 2 is the
     * one Text_DrawString forces for its own duration. Every entry but 0 gets its own colour, so a
     * painted sprite is visible and a wrong sprite index shows up as different pixels. */
    for (i32 i = 1; i < PUFO_PAL_SIZE; ++i) {
        g_aPalette[3][i] = (u32)(0x0100 + i);
        g_aPalette[2][i] = (u32)(0x0200 + i);
    }

    memset(&g_armCraft, 0, sizeof(g_armCraft));
    g_armCraft.bWeaponL = nMountL;
    g_armCraft.bWeaponR = nMountR;
    g_armCraft.bAmmo25 = 7;
    g_armCraft.bAmmo26 = 3;
    g_apArmCraft[0] = &g_armCraft;

    g_nArmShipIdx  = 0;
    g_nArmRangeCur = nRangeCur;
    g_nArmLevel    = nLevel;
    g_nArmBoundL   = -999;
    g_nArmBoundR   = -999;
    g_nArmCoolL    = 100; /* sentinels: a value the function is expected to change */
    g_nArmCoolR    = 100;
    g_nArmShotL    = 40;
    g_nArmShotR    = 40;
    g_nArmAmmoL    = nAmmoL;
    g_nArmAmmoR    = nAmmoR;
    g_bArmEngageL  = 9;
    g_bArmEngageR  = 9;
    g_nGlyphPalSlotBase = 0; /* the original starts at 0; the function must set it to 3 */

    memset(&g_armEnv, 0, sizeof(g_armEnv));
    g_armEnv.pnGlyphPalBase = &g_nGlyphPalSlotBase; /* the original's one global */
    g_armEnv.pnShipIdx      = &g_nArmShipIdx;
    g_armEnv.apCraft        = g_apArmCraft;
    g_armEnv.nCraftCount    = 1;
    g_armEnv.pnRangeCur     = &g_nArmRangeCur;
    g_armEnv.pnCraftLevel   = &g_nArmLevel;
    g_armEnv.pnBoundL       = &g_nArmBoundL;
    g_armEnv.pnBoundR       = &g_nArmBoundR;
    g_armEnv.pbEngageL      = &g_bArmEngageL;
    g_armEnv.pbEngageR      = &g_bArmEngageR;
    g_armEnv.pnCooldownL    = &g_nArmCoolL;
    g_armEnv.pnCooldownR    = &g_nArmCoolR;
    g_armEnv.pnShotYL       = &g_nArmShotL;
    g_armEnv.pnShotYR       = &g_nArmShotR;
    g_armEnv.pnAmmoL        = &g_nArmAmmoL;
    g_armEnv.pnAmmoR        = &g_nArmAmmoR;
}

static void TestRadarArmament(void)
{
    static const char *const apszArchive[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    printf("\nRadarScreen_DrawCraftArmament @ 0x5edf4\n");

    if (g_bdSurf.pPixels == NULL) {
        SoftSurf_ctor(&g_bdSurf);
        SoftSurf_AllocPixels(&g_bdSurf, BD_SURF_W, BD_SURF_H, PIXMODE_16BPP);
        g_pMainSurface = &g_bdSurf;
        Render_SetTarget(BD_SURF_W, BD_SURF_H, 2, 2, BD_SURF_W * 2, g_bdSurf.pPixels);
    }
    if (!g_bArmReady) {
        g_bArmReady = Asset_OpenArchives(apszArchive) != 0;
        if (g_bArmReady) {
            FontMetrics_Load();
            PufoFont_Init(&g_fontSys);
            g_fontSys.nGlyphBase    = g_fontSys.nTotalSprFrames;
            (void)Font_LoadSpr(&g_fontSys, "font");
            g_fontSys.nBaseBitsBase = g_fontSys.nTotalSprFrames;
            (void)Font_LoadSpr(&g_fontSys, "basebits");
            Font_BuildIndex(&g_fontSys);
            g_bArmReady = g_fontMetrics.bLoaded && g_fontSys.apSprIndex != NULL
                       && g_fontSys.nBaseBitsBase > 53;
        }
    }
    if (!g_bArmReady) {
        Check(false, "the shipped font.dat and basebits.dat loaded, so the mount icons are real");
        return;
    }

    /* --- the cannon / stingray fixture: reach 250 + 560*(8-26)/114 = 162 --- */
    ArmFixture(WM_CANNON, WM_STINGRAY, 7, 3, 250, 8);
    /* Reference sprites first: taking one clears the surface. */
    const u32 uGlyph50 = ArmGlyphSignature(PUFO_RADAR_MOUNT_GLYPH_X_L, 50);
    const u32 uGlyph48 = ArmGlyphSignature(PUFO_RADAR_MOUNT_GLYPH_X_L, 48);
    const u32 uGlyphR48 = ArmGlyphSignature(PUFO_RADAR_MOUNT_GLYPH_X_R, 48);
    RadarScreen_DrawCraftArmament(&g_armEnv);

    CheckEqI32(g_nGlyphPalSlotBase, PUFO_RADAR_MOUNT_PAL_BASE,
               "the call sets Map_nGlyphPalBase to 3 and Text_DrawString restores it");
    CheckEqI32(g_nArmBoundL, 140, "the cannon's mount range is 140");
    CheckEqI32(g_nArmBoundR, 260, "the stingray's is 260");
    CheckEqI32(g_bArmEngageL, 0, "a reach of 162 is above 140, so the cannon is out of gunshot");
    CheckEqI32(g_bArmEngageR, 1, "while 162 is inside 260, so the stingray stays engaged");
    CheckEqI32(g_nArmCoolL, PUFO_RADAR_COOLDOWN_READY,
               "the out-of-gunshot mount's cooldown is forced to 255");
    CheckEqI32(g_nArmShotL, 0, "and its in-flight projectile is dropped");
    CheckEqI32(g_nArmCoolR, 100, "the engaged mount's cooldown is left alone");
    CheckEqI32(g_nArmShotR, 40, "and its projectile is left alone");

    CheckEqU32(ArmIconSignature(PUFO_RADAR_MOUNT_GLYPH_X_L), uGlyph50,
               "the left icon is exactly basebits frame +50 at (30, 136)");
    Check(ArmIconSignature(PUFO_RADAR_MOUNT_GLYPH_X_L) != uGlyph48,
          "and not the stingray's +48 -- the glyph offset is per mount id");
    CheckEqU32(ArmIconSignature(PUFO_RADAR_MOUNT_GLYPH_X_R), uGlyphR48,
               "the right icon is basebits frame +48 at (91, 136)");

    /* The cannon's bar: h = 114*140/560 = 28, and 28 - 34 is negative, so nothing is painted. */
    CheckEqI32(ArmCount(35, 100, 50, 136, ARM_BAR_COLOR), 0,
               "a 140 range scales to 28 pixels and the bar height 28-34 draws nothing");
    CheckEqI32(ArmCount(35, 136, 50, 170, ARM_BAR_COLOR), 0,
               "not even below the icon, where a 170-range cap would have gone");
    /* ... but the mount's own ticks are drawn anyway, at y = 170 - 28. */
    CheckEqI32(ArmCount(54, 142, 68, 143, ARM_BAR_COLOR), 10,
               "the five 2x1 ticks at x 54,57,60,63,66 are drawn at y 142 even with no bar");
    CheckEqI32(ArmCount(51, 141, 54, 144, ARM_BAR_COLOR), 9, "with the 3x3 at (51, 141)");
    CheckEqI32(ArmCount(54, 143, 68, 144, ARM_BAR_COLOR), 0, "and the ticks are one row tall");

    /* The stingray's bar: h = 114*260/560 = 52, so (98, 118) 1 wide, 52-34 = 18 tall. */
    CheckEqI32(ArmCount(98, 118, 99, 136, ARM_BAR_COLOR), 18,
               "the stingray bar is one column at x 98 from y 118 to 135");
    CheckEqI32(ArmCount(98, 100, 99, 117, ARM_BAR_COLOR), 0,
               "and stops at 118 -- the only bar colour above it is the cap's own 3x3");
    CheckEqI32(ArmCount(100, 118, 102, 136, ARM_BAR_COLOR), 0, "one column wide only");
    CheckEqI32(ArmCount(97, 117, 100, 120, ARM_BAR_COLOR), 9,
               "a 260 range is above 170, so the 3x3 cap lands at (97, 117)");
    CheckEqI32(ArmCount(69, 118, 83, 119, ARM_BAR_COLOR), 10,
               "the right ticks are at x 69,72,75,78,81 on the bar's top edge");
    CheckEqI32(ArmCount(83, 117, 86, 120, ARM_BAR_COLOR), 9, "with the 3x3 at (83, 117)");

    /* The ammo labels, centred on the bar's own column. */
    const i32 nWidth7 = Text_MeasureWidth("7");
    CheckEqI32(nWidth7, 7, "the real font metrics make \"7\" 7 pixels wide");
    CheckEqI32(ArmLabelLeftmost(20, 34), -1, "nothing is painted left of 37 - 7/2 = 34");
    Check(ArmLabelLeftmost(20, 60) == 34, "the left label \"7\" starts at x 34");
    Check(ArmLabelLeftmost(85, 95) == -1, "nothing is painted left of 98 - 7/2 = 95");
    Check(ArmLabelLeftmost(85, 115) == 95, "the right label \"3\" starts at x 95");
    const i32 nSevenPainted = ArmAny(20, 154, 60, 170);
    Check(nSevenPainted > 0, "the left label is a real sprite, not a stray pixel");

    /* --- a wider number shifts the centring, which is what proves it is centring --- */
    ArmFixture(WM_CANNON, WM_STINGRAY, 100, 3, 250, 8);
    RadarScreen_DrawCraftArmament(&g_armEnv);
    const i32 nWidth100 = Text_MeasureWidth("100");
    Check(nWidth100 > nWidth7, "\"100\" is wider than \"7\" under the real metrics");
    Check(ArmLabelLeftmost(20, 60) < 34, "and the centred label therefore starts further left");

    /* --- -1 is the unlimited sentinel and draws the single dash --- */
    ArmFixture(WM_CANNON, WM_STINGRAY, PUFO_RADAR_AMMO_UNLIMITED, 3, 250, 8);
    RadarScreen_DrawCraftArmament(&g_armEnv);
    const i32 nDashPainted = ArmAny(20, 154, 60, 170);
    Check(nDashPainted > 0 && nDashPainted != nSevenPainted,
          "-1 draws the dash sprite instead of the digits \"7\"");
    Check(ArmLabelLeftmost(20, 60) == 37 - Text_MeasureWidth("-") / 2,
          "and it is centred on 37 like every other label");

    /* --- -2 draws no label at all --- */
    ArmFixture(WM_CANNON, WM_STINGRAY, PUFO_RADAR_AMMO_NONE, 3, 250, 8);
    RadarScreen_DrawCraftArmament(&g_armEnv);
    CheckEqI32(ArmAny(20, 154, 60, 170), 0, "-2 draws no left label");
    Check(ArmAny(85, 154, 115, 170) > 0, "while the right mount still draws its own");

    /* --- the fusion / laser fixture: a long range, a short one, and an empty mount ---
     * reach = 100 + 560*(26-26)/114 = 100, so both mounts are engaged. */
    ArmFixture(WM_FUSION, WM_LASER, 4, PUFO_RADAR_AMMO_NONE, 100, 26);
    RadarScreen_DrawCraftArmament(&g_armEnv);
    CheckEqI32(g_nArmBoundL, 420, "the fusion launcher's mount range is 420");
    /* The laser's 180 is proven by its bar geometry below; the tail at 0x5F45C then overwrites
     * nRadarBoundR, because this mount holds the -2 sentinel. */
    CheckEqI32(g_bArmEngageL, 1, "a reach of 100 is inside both");
    CheckEqI32(g_bArmEngageR, 1, "so neither cooldown is touched");
    CheckEqI32(g_nArmCoolL, 100, "the left cooldown stays at its sentinel");
    CheckEqI32(g_nArmCoolR, 100, "and the right one too");

    /* h = 114*420/560 = 85, so the left bar is (37, 85) 1 wide and 85-34 = 51 tall. */
    CheckEqI32(ArmCount(37, 85, 38, 136, ARM_BAR_COLOR), 51,
               "the 420 range scales to 85 pixels and draws 51 of them");
    CheckEqI32(ArmCount(37, 79, 38, 84, ARM_BAR_COLOR), 0, "starting at y 85, one row below the cap");
    CheckEqI32(ArmCount(36, 84, 39, 87, ARM_BAR_COLOR), 9, "with the 3x3 cap at (36, 84)");
    CheckEqI32(ArmCount(54, 85, 68, 86, ARM_BAR_COLOR), 10, "and the ticks moved up to y 85");
    CheckEqI32(ArmCount(51, 84, 54, 87, ARM_BAR_COLOR), 9, "with their 3x3 at (51, 84)");

    /* h = 114*180/560 = 36, so the laser's bar is only 36-34 = 2 rows, but it still gets a cap. */
    CheckEqI32(ArmCount(98, 134, 99, 136, ARM_BAR_COLOR), 2,
               "the 180 range scales to 36 pixels and draws just 2");
    CheckEqI32(ArmCount(97, 133, 100, 136, ARM_BAR_COLOR), 9,
               "180 is still above 170, so the cap is drawn at (97, 133)");
    CheckEqI32(ArmCount(69, 134, 83, 135, ARM_BAR_COLOR), 10, "and the ticks sit at y 134");

    /* The tail: the right mount holds -2, so it inherits the left mount's bound. */
    CheckEqI32(g_nArmBoundL, 420, "the mount that has ammo keeps its own bound");
    CheckEqI32(g_nArmBoundR, 420, "and the empty one inherits it");

    /* --- the other tail direction --- */
    ArmFixture(WM_CANNON, WM_STINGRAY, 7, PUFO_RADAR_AMMO_NONE, 250, 8);
    RadarScreen_DrawCraftArmament(&g_armEnv);
    CheckEqI32(g_nArmBoundL, 140, "the mount that has ammo keeps its own 140");
    CheckEqI32(g_nArmBoundR, 140, "and an empty right mount inherits it, the other way round");

    ArmFixture(WM_CANNON, WM_STINGRAY, PUFO_RADAR_AMMO_NONE, PUFO_RADAR_AMMO_NONE, 250, 8);
    RadarScreen_DrawCraftArmament(&g_armEnv);
    CheckEqI32(g_nArmBoundL, 260, "with both empty the left mount takes the right's 260");
    CheckEqI32(g_nArmBoundR, 260, "and the right one keeps it -- the second copy moves the first");

    /* --- the six mount ids, each with its own range and its own icon --- */
    {
        static const i8 anMount[PUFO_RADAR_MOUNT_COUNT] = {
            WM_CANNON, WM_STINGRAY, WM_AVALANCHE, WM_LASER, WM_PLASMA, WM_FUSION
        };
        static const i32 anRange[PUFO_RADAR_MOUNT_COUNT] = { 140, 260, 360, 180, 300, 420 };
        static const u16 anGlyph[PUFO_RADAR_MOUNT_COUNT] = { 50, 48, 49, 52, 53, 51 };
        i32 nRangeBad = 0, nGlyphBad = 0;
        for (i32 i = 0; i < PUFO_RADAR_MOUNT_COUNT; ++i) {
            const u32 uWant = ArmGlyphSignature(PUFO_RADAR_MOUNT_GLYPH_X_L, anGlyph[i]);
            ArmFixture(anMount[i], 0, 1, PUFO_RADAR_AMMO_NONE, 100, 26);
            RadarScreen_DrawCraftArmament(&g_armEnv);
            if (g_nArmBoundL != anRange[i]) {
                ++nRangeBad;
            }
            if (ArmIconSignature(PUFO_RADAR_MOUNT_GLYPH_X_L) != uWant) {
                ++nGlyphBad;
            }
        }
        CheckEqI32(nRangeBad, 0, "ids 25..30 give bounds 140, 260, 360, 180, 300, 420");
        CheckEqI32(nGlyphBad, 0, "and icons +50, +48, +49, +52, +53, +51");
    }

    /* --- an id no switch handles: the psi amplifier --- */
    ArmFixture(WM_PSI_AMP, WM_PSI_AMP, PUFO_RADAR_AMMO_NONE, PUFO_RADAR_AMMO_NONE, 250, 8);
    RadarScreen_DrawCraftArmament(&g_armEnv);
    CheckEqI32(g_nArmBoundL, 0, "id 24 falls through the switch and stores a zero bound");
    CheckEqI32(ArmAny(30, 136, 45, 153), 0, "drawing no left icon");
    CheckEqI32(ArmAny(91, 136, 106, 153), 0, "and no right icon");
    /* The tick row is gated on bWeaponL != 0, not on the switch having a row, so a psi-armed mount
     * still draws it -- at y = 170 - 0, since a zero bound scales to a zero bar. */
    CheckEqI32(ArmCount(54, 170, 68, 171, ARM_BAR_COLOR), 10,
               "id 24 still draws the tick row, at y 170 because the bound is zero");
    CheckEqI32(ArmCount(51, 169, 54, 172, ARM_BAR_COLOR), 9, "with its 3x3 at (51, 169)");
    CheckEqI32(ArmCount(37, 136, 38, 170, ARM_BAR_COLOR), 0, "while the bar itself draws nothing");
    CheckEqI32(g_bArmEngageL, 0, "a zero bound puts any positive reach out of gunshot");
    CheckEqI32(g_nArmCoolL, PUFO_RADAR_COOLDOWN_READY, "which rearms the cooldown");

    /* --- the guards --- */
    ArmFixture(WM_CANNON, WM_STINGRAY, 7, 3, 250, 8);
    RadarScreen_DrawCraftArmament(NULL);
    CheckEqI32(g_nArmBoundL, -999, "a null environment is refused");
    CheckEqI32(g_nGlyphPalSlotBase, 0, "leaving Map_nGlyphPalBase untouched");
    CheckEqI32(ArmAny(20, 100, 120, 175), 0, "and painting nothing");

    g_nArmShipIdx = 4;
    RadarScreen_DrawCraftArmament(&g_armEnv);
    CheckEqI32(g_nGlyphPalSlotBase, PUFO_RADAR_MOUNT_PAL_BASE,
               "an out-of-range ship index still sets the glyph base, as the original does");
    CheckEqI32(g_nArmBoundL, -999, "but it reaches no craft and stores no bound");
}

/* ---- RadarScreen_DrawOverlay @ 0x5dc84 ------------------------------------------------
 *
 * The shot animation, drawn on the same real surface. The fixture uses nRadarRangeCur 560 and
 * nRadarCraftLevel 8, so the flight gate is 8 + (114*560/560) - 33 = 89 pixels. */

static Ship              g_ovCraft;
static Ship             *g_apOvCraft[1];
static PtrArray          g_ovStrateg;
static bool              g_bOvListReady;
static i32               g_nOvStrategIdx, g_nOvTargetHp;
static RadarHitRollEnv   g_ovHitEnv;
static i32               g_nOvShipIdx, g_nOvRangeCur, g_nOvLevel;
static i32               g_nOvCoolL, g_nOvCoolR, g_nOvShotL, g_nOvShotR;
static i32               g_nOvAmmoL, g_nOvAmmoR;
static u8                g_bOvEngL, g_bOvEngR, g_bOvFlip, g_bOvLastHit;
static RadarOverlayEnv   g_ovEnv;

#define OV_COLOR_SHOT   0x5640 /* 0x50 / 0xC8 / 0 */
#define OV_COLOR_WIDE   0x5500 /* 0x50 / 0xA0 / 0 */
#define OV_COLOR_BAR    0x7E45 /* 0x78 / 0xC8 / 0x28 */
#define OV_COLOR_PLASMA 0x2B20 /* 0x28 / 0x64 / 0 */
#define OV_COLOR_CORE   0x56E0 /* 0x50 / 0xDC / 0 */
#define OV_GATE           89   /* 8 + 114 - 33, the fixture's flight gate */

static StrategItem *OvStrategItem(void)
{
    return (StrategItem *)PtrArray_Get(&g_ovStrateg, g_nOvStrategIdx);
}

static void OvFixture(i8 nMountL, i8 nMountR, i32 nAmmoL, i32 nAmmoR,
                      i32 nShotL, i32 nShotR, i32 nRangeCur, i32 nLevel)
{
    memset(g_bdSurf.pPixels, 0, (size_t)g_bdSurf.nWidth * (size_t)g_bdSurf.nHeight * 2u);
    memset(g_aPalette, 0, sizeof(g_aPalette));

    memset(&g_ovCraft, 0, sizeof(g_ovCraft));
    g_ovCraft.bWeaponL = nMountL;
    g_ovCraft.bWeaponR = nMountR;
    g_apOvCraft[0] = &g_ovCraft;

    g_nOvShipIdx  = 0;
    g_nOvRangeCur = nRangeCur;
    g_nOvLevel    = nLevel;
    g_nOvCoolL    = PUFO_RADAR_COOLDOWN_READY;
    g_nOvCoolR    = PUFO_RADAR_COOLDOWN_READY;
    g_nOvShotL    = nShotL;
    g_nOvShotR    = nShotR;
    g_nOvAmmoL    = nAmmoL;
    g_nOvAmmoR    = nAmmoR;
    g_bOvEngL     = 1;
    g_bOvEngR     = 1;
    g_bOvFlip     = 0;
    g_bOvLastHit  = 9; /* a sentinel the call must clear */
    g_nOvTargetHp = 1000;
    {
        StrategItem *pItem = OvStrategItem();
        if (pItem != NULL) {
            pItem->nHp = 1000;
        }
    }

    memset(&g_ovEnv, 0, sizeof(g_ovEnv));
    g_ovEnv.pnShipIdx     = &g_nOvShipIdx;
    g_ovEnv.apCraft       = g_apOvCraft;
    g_ovEnv.nCraftCount   = 1;
    g_ovEnv.pnRangeCur    = &g_nOvRangeCur;
    g_ovEnv.pnCraftLevel  = &g_nOvLevel;
    g_ovEnv.pbFlip        = &g_bOvFlip;
    g_ovEnv.pbEngageL     = &g_bOvEngL;
    g_ovEnv.pbEngageR     = &g_bOvEngR;
    g_ovEnv.pnCooldownL   = &g_nOvCoolL;
    g_ovEnv.pnCooldownR   = &g_nOvCoolR;
    g_ovEnv.pnShotYL      = &g_nOvShotL;
    g_ovEnv.pnShotYR      = &g_nOvShotR;
    g_ovEnv.pnAmmoL       = &g_nOvAmmoL;
    g_ovEnv.pnAmmoR       = &g_nOvAmmoR;
    g_ovEnv.pbLastShotHit = &g_bOvLastHit;
    g_ovEnv.pHitEnv       = &g_ovHitEnv;
}

static void TestRadarOverlay(void)
{
    printf("\nRadarScreen_DrawOverlay @ 0x5dc84\n");

    if (g_bdSurf.pPixels == NULL) {
        SoftSurf_ctor(&g_bdSurf);
        SoftSurf_AllocPixels(&g_bdSurf, BD_SURF_W, BD_SURF_H, PIXMODE_16BPP);
        g_pMainSurface = &g_bdSurf;
        Render_SetTarget(BD_SURF_W, BD_SURF_H, 2, 2, BD_SURF_W * 2, g_bdSurf.pPixels);
    }
    if (!g_bOvListReady) {
        PtrArray_ctor(&g_ovStrateg);
        g_bOvListReady = StrategItem_AllocAdd(&g_ovStrateg, 0) != 0;
        g_nOvStrategIdx = 0;
        memset(&g_ovHitEnv, 0, sizeof(g_ovHitEnv));
        g_ovHitEnv.pStrategList = &g_ovStrateg;
        g_ovHitEnv.pnStrategIdx = &g_nOvStrategIdx;
        g_ovHitEnv.pnTargetHp   = &g_nOvTargetHp;
    }
    if (!g_bOvListReady || OvStrategItem() == NULL) {
        Check(false, "the intercept strateg entry the shot roll writes to could not be created");
        return;
    }

    /* --- the idle test, and the unconditional clear of bRadarLastShotHit --- */
    OvFixture(WM_CANNON, WM_CANNON, 5, 0, 0, 0, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_bOvEngR, 0, "ammo 0 with the projectile at 0 is idle, which clears bRadarEngageR");
    CheckEqI32(g_bOvLastHit, 0, "bRadarLastShotHit is cleared on every call (0x5DCC0)");
    CheckEqI32(g_nOvAmmoR, 0, "and the idle mount takes no round");
    CheckEqI32(g_nOvAmmoL, 4, "while the other mount, still engaged, does take one");
    CheckEqI32(ArmCount(67, 159, 70, 161, OV_COLOR_SHOT), 4,
               "only the left cannon's two 1x2 rects are on the surface, at x 67 and 69 -- the same "
               "columns the right half uses");

    /* --- the -2 sentinel is NOT idle, and it is not exempt from the round either --- */
    OvFixture(WM_CANNON, WM_CANNON, 5, PUFO_RADAR_AMMO_NONE, 0, 0, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_bOvEngR, 1, "ammo -2 is not 0, so the mount is not idle");
    CheckEqI32(g_nOvAmmoR, PUFO_RADAR_AMMO_NONE - 1,
               "and the round is taken anyway: -2 becomes -3 (ORIGINAL BUG 0x5DD1C)");
    OvFixture(WM_PSI_AMP, WM_CANNON, PUFO_RADAR_AMMO_NONE, 5, 0, 5, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    RadarScreen_DrawOverlay(&g_ovEnv);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvAmmoL, PUFO_RADAR_AMMO_NONE - 3,
               "a mount the switch has no arm for never advances its projectile, so the round is "
               "taken on every frame and the counter runs off: -2, -3, -4, -5");
    CheckEqI32(g_nOvShotL, 0, "the projectile offset really never moves");
    CheckEqI32(g_nOvAmmoR, 5, "while a mount with a real id and a projectile already in flight is "
                              "left alone");

    /* --- the two gates, and that the halves are independent --- */
    OvFixture(WM_CANNON, WM_CANNON, 5, 5, 0, 0, 560, 8);
    g_nOvCoolR = 254;
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvAmmoR, 5, "a cooldown of 254 is not 255, so the right half does not run");
    CheckEqI32(g_nOvAmmoL, 4, "and the left half runs anyway");
    CheckEqI32(g_nOvCoolL, PUFO_RADAR_COOLDOWN_READY, "leaving the left cooldown armed");

    OvFixture(WM_CANNON, WM_CANNON, 5, 5, 0, 0, 560, 8);
    g_bOvEngR = 0;
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvAmmoR, 5, "a cleared engage byte stops that half");
    CheckEqI32(g_nOvAmmoL, 4, "and not the other one");

    /* --- the cannon: +6, or +8 when bRadarFlip is set --- */
    OvFixture(WM_CANNON, WM_CANNON, 7, 7, 0, 0, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvShotR, 6, "the projectile advances 6 pixels (0x5E198)");
    CheckEqI32(g_nOvAmmoR, 6, "and one round was consumed");
    CheckEqI32(BdPix(67, 159), OV_COLOR_SHOT, "the first 1x2 is at (67, 165-6)");
    CheckEqI32(BdPix(67, 160), OV_COLOR_SHOT, "two rows tall");
    CheckEqI32(BdPix(67, 161), 0, "and no further");
    CheckEqI32(BdPix(68, 159), 0, "one column wide");
    CheckEqI32(BdPix(69, 159), OV_COLOR_SHOT, "the second 1x2 is at x 69");

    OvFixture(WM_CANNON, WM_CANNON, 7, 7, 0, 0, 560, 8);
    g_bOvFlip = 1;
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvShotR, 8, "bRadarFlip adds 2 more, for 8 (0x5E1BC ADDNE)");
    CheckEqI32(BdPix(67, 157), OV_COLOR_SHOT, "so the projectile is two rows higher");

    OvFixture(WM_CANNON, WM_CANNON, 7, 7, 10, 10, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvShotR, 16, "a projectile already in flight advances but is not reloaded");
    CheckEqI32(g_nOvAmmoR, 7, "and no second round is taken -- only offset 0 starts a shot");

    /* --- landing: the gate is `offset < gate`, so 89 lands --- */
    OvFixture(WM_CANNON, WM_CANNON, 7, 7, OV_GATE, OV_GATE, 560, 8);
    g_nOvCoolL = 254; /* only the right half lands, so one roll runs and its result survives */
    srand(12345); /* Game_RandU16(100) == 23, below event 25's 50 */
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvShotR, 0, "a projectile at the gate lands and its offset is zeroed");
    CheckEqI32(g_nOvCoolR, PUFO_RADAR_COOLDOWN_CANNON, "the cannon rearms its cooldown to 255");
    CheckEqI32(g_nOvAmmoR, 7, "a landing takes no round -- only a start does");
    CheckEqI32(g_bOvLastHit, 1, "and the roll's result is stored in bRadarLastShotHit");
    CheckEqI32(g_nOvTargetHp, 950, "event 25 is the fixed -50");
    CheckEqI32(ArmCount(60, 150, 80, 170, OV_COLOR_SHOT), 0, "landing draws no projectile");

    OvFixture(WM_CANNON, WM_CANNON, 7, 7, OV_GATE, OV_GATE, 560, 8);
    srand(7013); /* Game_RandU16(100) == 70, above event 25's 50 */
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_bOvLastHit, 0, "a roll that fails its gate stores 0");
    CheckEqI32(g_nOvTargetHp, 1000, "and changes no HP");

    /* --- the stingray: 2x4 at x 70 right, x 64 left --- */
    OvFixture(WM_STINGRAY, WM_STINGRAY, 3, 3, 10, 10, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvShotR, 12, "the wide rockets step 2");
    CheckEqI32(BdPix(70, 153), OV_COLOR_WIDE, "the right rocket is 2 wide at x 70, y 165-12");
    CheckEqI32(BdPix(71, 156), OV_COLOR_WIDE, "and 4 tall");
    CheckEqI32(BdPix(72, 153), 0, "no further across");
    CheckEqI32(BdPix(70, 157), 0, "no further down");
    CheckEqI32(BdPix(64, 153), OV_COLOR_WIDE, "the left rocket is at x 64");
    CheckEqI32(BdPix(65, 153), OV_COLOR_WIDE, "two columns wide there too");
    OvFixture(WM_STINGRAY, WM_STINGRAY, 3, 3, 10, 10, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(BdPix(66, 153), 0, "and the left rocket stops at 65");

    /* --- the avalanche: a 3-wide body plus a 1x5 inner column drawn over it --- */
    OvFixture(WM_AVALANCHE, WM_AVALANCHE, 2, 2, 10, 10, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(BdPix(70, 153), OV_COLOR_WIDE, "the right body is 3 wide at x 70..72");
    CheckEqI32(BdPix(72, 153), OV_COLOR_WIDE, "its far column");
    CheckEqI32(BdPix(71, 152), OV_COLOR_BAR, "the 1x5 inner column starts one row higher at y 164-12");
    CheckEqI32(BdPix(71, 153), OV_COLOR_BAR, "and is drawn AFTER the body, overwriting its middle");
    CheckEqI32(BdPix(71, 156), OV_COLOR_BAR, "five rows");
    CheckEqI32(BdPix(71, 157), 0, "no further");
    CheckEqI32(BdPix(64, 153), OV_COLOR_WIDE, "the left body is at x 64..66");
    CheckEqI32(BdPix(65, 152), OV_COLOR_BAR, "with its inner column at 65");

    /* --- the laser: a beam, and the two halves use different columns --- */
    OvFixture(WM_LASER, WM_LASER, PUFO_RADAR_AMMO_UNLIMITED, PUFO_RADAR_AMMO_UNLIMITED, 0, 0, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvShotR, 1, "the beam counts 1 (0x5DF84)");
    CheckEqI32(g_nOvAmmoR, PUFO_RADAR_AMMO_UNLIMITED, "-1 is exempt from the round");
    CheckEqI32(BdPix(72, 72), OV_COLOR_SHOT, "the right beam is at x 72, y 194-114-8");
    CheckEqI32(BdPix(72, 163), OV_COLOR_SHOT, "114+8-30 = 92 rows tall");
    CheckEqI32(BdPix(72, 164), 0, "and no further");
    CheckEqI32(BdPix(72, 71), 0, "starting at 72");
    CheckEqI32(BdPix(73, 72), 0, "one column wide");
    CheckEqI32(BdPix(64, 72), OV_COLOR_SHOT, "the LEFT beam is at x 64, not 72");
    CheckEqI32(BdPix(64, 163), OV_COLOR_SHOT, "same height");

    OvFixture(WM_LASER, WM_LASER, 4, 4, 1, 1, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvShotR, 2, "the beam counts to 2");
    CheckEqI32(g_nOvAmmoR, 4, "offset 1 is not a start, so no round");
    OvFixture(WM_LASER, WM_LASER, 4, 4, 2, 2, 560, 8);
    srand(12345);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvCoolR, PUFO_RADAR_COOLDOWN_LASER, "at exactly 2 the laser lands, cooldown 251");
    CheckEqI32(g_nOvShotR, 0, "and its offset is zeroed");
    OvFixture(WM_LASER, WM_LASER, 4, 4, 3, 3, 560, 8);
    g_nOvCoolR = 100;
    g_nOvCoolL = 100;
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvShotR, 3, "a beam past 2 is neither in flight nor landing");
    CheckEqI32(g_nOvCoolR, 100, "so it never sets a cooldown (0x5DFEC BNE)");
    CheckEqI32(g_nOvCoolL, 100, "on either side");
    CheckEqI32(ArmCount(60, 60, 80, 170, OV_COLOR_SHOT), 0, "and draws nothing");

    /* --- the plasma: a 3-wide outer column and a 1-wide core --- */
    OvFixture(WM_PLASMA, WM_PLASMA, 9, 9, 0, 0, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(BdPix(70, 72), OV_COLOR_PLASMA, "the right plasma's outer column is 3 wide at x 70");
    CheckEqI32(BdPix(72, 72), OV_COLOR_PLASMA, "its far column");
    CheckEqI32(BdPix(71, 72), OV_COLOR_CORE, "drawn over by the 1-wide core at x 71");
    CheckEqI32(BdPix(73, 72), 0, "no further across");
    CheckEqI32(BdPix(64, 72), OV_COLOR_PLASMA, "the left plasma's outer column is at x 64");
    CheckEqI32(BdPix(65, 72), OV_COLOR_CORE, "with its core at 65");
    OvFixture(WM_PLASMA, WM_PLASMA, 9, 9, 2, 2, 560, 8);
    srand(12345);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvCoolR, PUFO_RADAR_COOLDOWN_PLASMA, "the plasma lands at 2 with cooldown 249");

    /* --- the fusion ball: body, inner body and a cross-bar --- */
    OvFixture(WM_FUSION, WM_FUSION, 2, 2, 0, 0, 560, 8);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvShotR, 2, "the fusion ball steps 2");
    CheckEqI32(BdPix(70, 167), OV_COLOR_WIDE, "the 2x6 body at x 70..71, y 165-2");
    CheckEqI32(BdPix(70, 163), OV_COLOR_BAR, "overlaid by the 2x4 inner body at the same y");
    CheckEqI32(BdPix(69, 164), OV_COLOR_BAR, "and the 4x2 cross-bar at x 69..72, y 166-2");
    CheckEqI32(BdPix(72, 165), OV_COLOR_BAR, "the cross-bar reaches x 72");
    CheckEqI32(BdPix(69, 166), 0, "two rows tall only");
    CheckEqI32(BdPix(64, 167), OV_COLOR_WIDE, "the left body is at x 64..65");
    CheckEqI32(BdPix(63, 164), OV_COLOR_BAR, "with its cross-bar at x 63..66");
    OvFixture(WM_FUSION, WM_FUSION, 2, 2, OV_GATE, OV_GATE, 560, 8);
    srand(12345);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvCoolR, PUFO_RADAR_COOLDOWN_FUSION, "the fusion ball lands with cooldown 230");

    /* --- the six cooldowns, one per mount id --- */
    {
        static const i8 anMount[6] = { WM_CANNON, WM_STINGRAY, WM_AVALANCHE,
                                       WM_LASER, WM_PLASMA, WM_FUSION };
        static const i32 anCool[6] = { PUFO_RADAR_COOLDOWN_CANNON, PUFO_RADAR_COOLDOWN_STINGRAY,
                                       PUFO_RADAR_COOLDOWN_AVALANCHE, PUFO_RADAR_COOLDOWN_LASER,
                                       PUFO_RADAR_COOLDOWN_PLASMA, PUFO_RADAR_COOLDOWN_FUSION };
        static const i32 anShot[6] = { OV_GATE, OV_GATE, OV_GATE, 2, 2, OV_GATE };
        i32 nBad = 0;
        for (i32 i = 0; i < 6; ++i) {
            OvFixture(anMount[i], anMount[i], 5, 5, anShot[i], anShot[i], 560, 8);
            g_nOvCoolL = 254; /* only the right half runs */
            srand(12345);
            RadarScreen_DrawOverlay(&g_ovEnv);
            if (g_nOvCoolR != anCool[i] || g_nOvShotR != 0) {
                ++nBad;
            }
        }
        CheckEqI32(nBad, 0,
                    "ids 25..30 land with cooldowns 255, 240, 235, 251, 249, 230 and zero the offset");
    }

    /* --- the roll writes the strateg item too --- */
    OvFixture(WM_CANNON, WM_CANNON, 5, 5, OV_GATE, OV_GATE, 560, 8);
    g_nOvCoolL = 254;
    srand(12345);
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(OvStrategItem()->nHp, 950, "RadarScreen_RollWeaponHit mirrors the HP into the item");

    /* --- the guards --- */
    OvFixture(WM_CANNON, WM_CANNON, 5, 5, 0, 0, 560, 8);
    RadarScreen_DrawOverlay(NULL);
    CheckEqI32(g_nOvAmmoR, 5, "a null environment is refused");
    CheckEqI32(ArmCount(20, 60, 120, 200, OV_COLOR_SHOT), 0, "and draws nothing");
    g_nOvShipIdx = 7;
    RadarScreen_DrawOverlay(&g_ovEnv);
    CheckEqI32(g_nOvAmmoR, 5, "an out-of-range ship index reaches no craft");
    CheckEqI32(g_bOvLastHit, 9, "and does not even reach the clear of bRadarLastShotHit");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0); /* so a crash points at the check that caused it */

    TestTickShips();
    TestTickHourly();
    TestTickBases();
    TestAdvance();
    TestPanelFlagsAndQty();
    TestBaseGridLookup();
    TestFacilityClassify();
    TestWorkshopCap();
    TestCapacityFamily();
    TestContainmentNeededAndSpace();
    TestSoldiersAssigned();
    TestShipsAtBase();
    TestClearSelection();
    TestRadarWeaponHit();
    TestDismissSelected();
    TestInitCraftSelectState();
    TestBeginCraftSelect();
    TestDemolishBase();
    TestPsiCapAndUpkeep();
    TestFindFacilityCell();
    TestActiveManuf();
    TestWorkshopNeeded();
    TestLabChain();
    TestCrewAvailable();
    TestRemainingNeededPasses();
    TestEmptyFacilityCells();
    TestFacilityPlaceAndGrant();
    TestSpeedButtons();
    TestCreateNamedEntry();
    TestBudgetHelpers();
    TestHangarPlacement();
    TestFacCostLabel();
    TestSiteScore();
    TestRadarCursors();
    TestRadarPaletteCycle();
    TestCountInbound();
    TestBaseStripHit();
    TestTickFacilityConstruction();
    TestRadarBackdrop();
    TestRadarFuelGauge();
    TestRadarRangeBar();
    TestRadarSession();
    TestRadarDetect();
    TestRadarArmament();
    TestRadarOverlay();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
