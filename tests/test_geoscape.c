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
#include "game/pufo_ship.h"
#include "game/pufo_unit.h"
#include "util/pufo_ptrarray.h"
#include <stdlib.h>
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
    static SmapSite sites[3];
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
    TestContainmentNeededAndSpace();
    TestSoldiersAssigned();
    TestShipsAtBase();
    TestClearSelection();
    TestDismissSelected();
    TestInitCraftSelectState();
    TestBeginCraftSelect();
    TestDemolishBase();
    TestPsiCapAndUpkeep();
    TestFindFacilityCell();
    TestActiveManuf();
    TestWorkshopNeeded();
    TestLabChain();
    TestRemainingNeededPasses();
    TestEmptyFacilityCells();
    TestFacilityPlaceAndGrant();
    TestSpeedButtons();
    TestCreateNamedEntry();
    TestBudgetHelpers();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
