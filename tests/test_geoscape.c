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

int main(void)
{
    TestTickShips();
    TestTickHourly();
    TestTickBases();
    TestAdvance();
    TestPanelFlagsAndQty();
    TestBaseGridLookup();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
