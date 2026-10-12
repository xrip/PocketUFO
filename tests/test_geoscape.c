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
#include "game/pufo_save.h" /* SmapSite, for the monthly upkeep fixture */
#include "game/pufo_strateg.h"
#include "game/pufo_clock.h"
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

static int g_nProcess, g_nProcessSpawn, g_nReach, g_nMsg;
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

static bool Hook_Reach(i32 nShipIdx, void *pUser)
{
    (void)nShipIdx; (void)pUser;
    ++g_nReach;
    return true;
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
    pHooks->pfnCanInterceptReach = Hook_Reach;
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

    g_nProcess = g_nProcessSpawn = g_nReach = g_nMsg = 0;
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

    /* Ship 0: away from base, reachable, no contact yet -> the contact path.
     * Ship 1: at base -> the range-recovery path. Ship_TickRangeRecover only
     * recovers while bAtBase, so one ship cannot show both. */
    st.world.nShipCount = 2;
    g_aShips[0].bAtBase = 0;
    g_aShips[0].uRangeRemain = 10;
    g_aShips[0].uRangeMax = 1000;
    g_aShips[0].uRangeRate = 5;
    g_aShips[1].bAtBase = 1;
    g_aShips[1].uRangeRemain = 10;
    g_aShips[1].uRangeMax = 1000;
    g_aShips[1].uRangeRate = 5;

    Geoscape_TickShips(&st, &hooks);
    CheckEqI32(g_aShips[1].uRangeRemain, 15, "range recovery ran first");
    CheckEqI32(g_aShips[0].uRangeRemain, 10, "no recovery away from base");
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

int main(void)
{
    TestTickShips();
    TestTickHourly();
    TestTickBases();
    TestAdvance();
    TestPanelFlagsAndQty();
    TestBaseGridLookup();
    TestFacilityClassify();
    TestWorkshopCap();
    TestCapacityFamily();
    TestSoldiersAssigned();
    TestShipsAtBase();
    TestClearSelection();
    TestPsiCapAndUpkeep();
    TestFindFacilityCell();
    TestActiveManuf();
    TestWorkshopNeeded();
    TestLabChain();
    TestRemainingNeededPasses();
    TestEmptyFacilityCells();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
