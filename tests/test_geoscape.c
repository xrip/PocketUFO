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
#include "game/pufo_baseui.h"  /* PufoRegionId, the strategy-map regions */
#include "game/pufo_boot.h"  /* PUFO_TEXT_REC_STRIDE, for the cost label fixture */
#include "game/pufo_save.h"  /* SmapSite, for the monthly upkeep fixture */
#include "game/pufo_plane.h" /* PlaneDrawEnv, the hangar arm's callee */
#include "game/pufo_ship.h"
#include "game/pufo_unit.h"
#include "util/pufo_ptrarray.h"
#include <stdlib.h>
#include "game/pufo_strateg.h"
#include "game/pufo_clock.h"
#include "render/pufo_render.h" /* g_aPalette, for the radar palette cycle */
#include "game/pufo_font.h"     /* g_fontSys, Font_DrawGlyph: the mount icons are real sprites */
#include "asset/pufo_asset.h"   /* Asset_OpenArchives, the radar session runs on real pads */
#include "ui/pufo_geouimanage.h" /* PUFO_GEOUI_CLOCK_DIGIT_PADS, the ten clock pads at 0x186BF0 */
#include "ui/pufo_linelist.h"
#include "ui/pufo_modal.h"   /* UiModalAnim / UiModalLayout, the frame end's dialog */
#include "util/pufo_rand.h"
#include "util/pufo_str.h"    /* pufo_strncpy, the fixture's bounded copies */

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
static i32 g_aRadarPct[2];   /* the dword block @ 0x162008: the radar percent AND
                           * the hourly empty-cell counts -- the original has ONE array here */
static BaseRec g_aBases[2];
static BaseRec *g_apBases[2] = { &g_aBases[0], &g_aBases[1] };
static ObjectRecFull g_aObj105;
static Soldier g_aSoldiers[2];
static Soldier *g_apSoldiers[2] = { &g_aSoldiers[0], &g_aSoldiers[1] };
static PtrArray g_List;              /* the block GeoscapeState.pList points at (0x186534) */
static i32      g_nNextDisplayId;    /* and its two id counters, 0x186544 / 0x186548 */
static i32      g_nNextSiteId;
static u16      g_nDays;             /* the four shared cells GeoscapeState now points at */
static i32      g_nSelShipIdx, g_nLowSupply, g_nSoldierCount;

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
    g_List.apItems = (void **)g_apItems;
    g_List.nCount = 0; /* each test sets the entries it wants live */
    g_nNextDisplayId = 0;
    g_nNextSiteId = 0;
    pState->pList = &g_List;
    pState->pnNextDisplayId = &g_nNextDisplayId;
    pState->pnNextSiteId = &g_nNextSiteId;
    pState->world.pTime = &pState->time;
    g_nDays = 0;
    g_nSelShipIdx = -1;
    g_nLowSupply = 0;
    g_nSoldierCount = 2;
    pState->pnDaysSinceStart = &g_nDays;         /* word_1472C8 */
    pState->pnSelectedShipIdx = &g_nSelShipIdx;  /* 0x187054 */
    pState->pnLowSupplyFlag = &g_nLowSupply;     /* dwActionFail @ 0x1870C8 */
    pState->nSelectedShipAux = -1;
    pState->apBases = g_apBases;
    pState->pSupplyItem = &g_aObj105;
    pState->nBaseCount = 2;
    pState->apEmptyFacilityCells = g_aRadarPct; /* 0x162008, the same array */
    pState->apSoldiers = g_apSoldiers;
    pState->pnSoldierCount = &g_nSoldierCount;   /* 0x115350 */

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
    CheckEqI32(g_nSelShipIdx, 0, "selected ship index set (0x187054, the cell the state points at)");
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
    CheckEqI32(g_aRadarPct[0], 7, "empty cells for base 0");
    CheckEqI32(g_aRadarPct[1], 8, "empty cells for base 1");

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
    CheckEqI32(g_nDays, 1, "day counter advances (word_1472C8)");
    CheckEqI32(g_nConstruct, 1, "facility construction ticked");
    CheckEqI32(g_nClearRow, 2, "one grid row cleared per base");
    CheckEqI32(g_aSiteState[0], 1, "site state from row result (base 0)");
    CheckEqI32(g_aSiteState[1], 2, "site state from row result (base 1)");

    /* Soldier timers. */
    g_aSoldiers[0].nPsiTrainDays = 5;
    g_aSoldiers[0].auStat[6] = 10;
    g_aSoldiers[0].bUnk247 = 1;
    g_aSoldiers[0].bUnk183 = 254;
    Geoscape_TickBases(&st, &hooks);
    CheckEqI32(g_aSoldiers[0].auStat[6], 11, "nPsiTrainDays > 0 raises auStat[6]");
    CheckEqI32(g_aSoldiers[0].nPsiTrainDays, 4, "nPsiTrainDays counts down");
    CheckEqI32(g_aSoldiers[0].bUnk247, 0, "bUnk247 counts down");
    CheckEqI32(g_aSoldiers[0].bUnk183, 255, "bUnk183 254 -> -1 when timer hits 0");

    /* Day counter caps at 200. */
    g_nDays = 200;
    Geoscape_TickBases(&st, &hooks);
    CheckEqI32(g_nDays, 200, "day counter caps at 200");
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

    /* Both originals load the field SIGNED (LDRSB at 0x6AC68 and 0x5A900) while 0x6AC58 masks the
     * argument to a byte, so a field of 0x80 or more matches nothing in 0..255. */
    aBases[0].bUnk4 = 0x80;
    CheckEqI32(Base_FindIndexByTypeByte(apBases, 3, 0x80), -1,
               "a field of 0x80 is read as -128 and never matches the byte 0x80");
    aBases[0].bUnk4 = 0x7F;
    CheckEqI32(Base_FindIndexByTypeByte(apBases, 3, 0x7F), 0,
               "0x7F is the top of the signed range and does match");
    aBases[0].bUnk4 = 7;

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
    printf("Base_ClearFacilityGridRow @ 0x6a0e0\n");
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

    printf("Base_FindFacilityCell @ 0x6ae54\n");

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
    printf("Base_CalcWorkshopCap @ 0x65894\n");
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
    printf("Base_CountActiveManuf @ 0x68264\n");
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
    printf("Base_UpdateWorkshopNeeded @ 0x65bb4\n");
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
    printf("Base_CalcLabCap @ 0x65818 / Base_CountActiveResearch @ 0x68198 / Base_UpdateLabNeeded @ 0x65c7c\n");
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
    printf("Base_GetScientistCount @ 0x6812c / Base_GetEngineerCount @ 0x681f4\n");
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
    printf("Base_UpdateLivingQuartersNeeded @ 0x65e24 / Base_UpdateStoresNeeded @ 0x65d44 / "
           "Base_UpdatePsiLabNeeded @ 0x65fc8\n");
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
    printf("Base_CountEmptyFacilityCells @ 0x6aee0\n");
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

    printf("Base_CalcStoresCapacity @ 0x65720, Base_CalcLivingQuartersCap @ 0x6579c, "
           "Base_CalcAlienContainmentCap @ 0x65910, Base_HasResearchSlotFree @ 0x662bc, "
           "Base_HasWorkshopSlotFree @ 0x66340, Base_CountBuiltFacilities @ 0x682c8\n");

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

    printf("Base_CountShipsAtBase @ 0x66090\n");

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
    static i32 nMonthCounter;    /* unk_159648: the roll's weight-row index, LDRSB at 0x81AB4 */
    static u16 nAlienPoolAddend; /* 0x1472C8: the roll's early gate, LDRH at 0x81B6C */
    nMonthCounter = 0;
    nAlienPoolAddend = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnDifficulty = &nMonthCounter;
    ctx.pnEarlyGate = &nAlienPoolAddend;
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

/* Smap_ResolveInterceptUI @ 0x84964: the flag standing on message line 0 decides what the pending
 * map order becomes. Two craft with their own strateg entries, one base at (100, 200), and a message
 * list the function edits. */
static void TestResolveInterceptUI(void)
{
    static UiLineList lines;
    static GameTime time;
    static PtrArray list;
    static GeoscapeWorld world;
    static StrategSpawnCtx ctx;
    static Ship g_aShip[2];
    static Ship *g_apShip[2];
    static BaseRec g_Base;
    static BaseRec *g_apBase[1];
    static Soldier *g_apSol[1];
    static ObjectRecFull *g_apObj[1];
    static u8 g_abHangar[64];
    static i32 g_anAssigned[1];
    static UiControl ctl[6];
    static UiControl *apCtl[6];
    static StrategItem *pEnt[2];
    static i32 nSeed, nTotalSec, nAssignedCount;
    static u8 nShipCount, once;
    static u16 nSoldierCount;
    static u8 bSelected, bDone, b8C, b90, b98, bArmed;
    ShipRemovalEnv env;
    SmapResolveInterceptEnv renv;
    void **apSlots;

    printf("Smap_ResolveInterceptUI\n");

    for (int i = 0; i < 2; ++i) {
        memset(&g_aShip[i], 0, sizeof(g_aShip[i]));
        g_aShip[i].bId = (u8)(10 + i);
        g_aShip[i].bHomeBase = 0;
        g_aShip[i].bIntercepting = 1;
        g_aShip[i].nStrategIdx = 0; /* item 0 is type 1, so no one-shot waypoint is removed */
        g_apShip[i] = &g_aShip[i];
        pEnt[i] = (StrategItem *)calloc(1, sizeof(StrategItem));
        pEnt[i]->nType = 1;
        pEnt[i]->bActive = 0;
        pEnt[i]->nDisplayId = (u8)(10 + i); /* found by Ship.bId */
        pEnt[i]->nTimer = (i == 0) ? 111 : 222;
        pEnt[i]->nX = 40;
        pEnt[i]->nY = 60;
    }
    apSlots = (void **)malloc(sizeof(void *) * 2);
    apSlots[0] = pEnt[0];
    apSlots[1] = pEnt[1];
    list.nCount = 2;
    list.apItems = apSlots;

    memset(&g_Base, 0, sizeof(g_Base));
    g_Base.nUnk8 = 100;
    g_Base.nUnk12 = 200;
    g_apBase[0] = &g_Base;

    nShipCount = 2;
    nSoldierCount = 0;
    nAssignedCount = 0;
    once = 0;
    nTotalSec = 0;
    nSeed = 0;
    world.apCraft = g_apShip;
    world.nShipCount = 2;
    static i32 nMonthCounter;    /* unk_159648: the roll's weight-row index, LDRSB at 0x81AB4 */
    static u16 nAlienPoolAddend; /* 0x1472C8: the roll's early gate, LDRH at 0x81B6C */
    nMonthCounter = 0;
    nAlienPoolAddend = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnDifficulty = &nMonthCounter;
    ctx.pnEarlyGate = &nAlienPoolAddend;
    ctx.pnSeed = &nSeed;
    for (int i = 0; i < 6; ++i) {
        apCtl[i] = &ctl[i];
    }

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

    memset(&renv, 0, sizeof(renv));
    renv.pLines = &lines;
    renv.pbInterceptDone = &bDone;
    renv.pbFlag187090 = &b90;
    renv.pbFlag18708C = &b8C;
    renv.pbFlag187098 = &b98;
    renv.pbSelectionFlag = &bSelected;
    renv.pbFinalMissionCtl = &bArmed;
    renv.pWorld = &world;
    renv.apSpeedCtl = apCtl;
    renv.pShipEnv = &env;
    renv.pTime = &time;

    /* Flag 27 -- the pick-a-cell line -- sends the craft home and the tail still runs. */
    memset(&lines, 0, sizeof(lines));
    memset(&time, 0, sizeof(time));
    memset(ctl, 0, sizeof(ctl));
    lines.nLineCount = 2;
    lines.abLineFlags[0] = 27;
    lines.abLineFlags[1] = 40;
    lines.anColor[0][0] = 0;
    time.bPaused = 1;
    world.nTickSeconds = 60;
    ctl[3].nState = 1;
    bSelected = 1; b8C = 1; b90 = 1; b98 = 1; bDone = 1; bArmed = 1;
    Smap_ResolveInterceptUI(&renv);
    CheckEqI32(world.nTickSeconds, 5, "the head runs GameTime_Set5Sec (0x84968)");
    CheckEqI32(ctl[0].nState, 1, "  which lights nav control 0");
    CheckEqI32(ctl[3].nState, 0, "  and darkens the one that was lit");
    CheckEqI32(bDone, 0, "byte_18709C is cleared on entry (0x84984)");
    CheckEqI32(pEnt[0]->nTimer, 0, "flag 27 retargets the craft in colour slot 0: its timer restarts (0x849AC)");
    CheckEqI32(pEnt[0]->nDestX, 100, "  towards the home base x");
    CheckEqI32(pEnt[0]->nDestY, 200, "  and y");
    CheckEqI32(g_aShip[0].bIntercepting, 0, "  and the intercept flag is cleared");
    CheckEqI32(pEnt[1]->nTimer, 222, "the other craft is left alone");
    CheckEqI32(bSelected, 0, "the tail clears the selection byte (0x84A34)");
    CheckEqI32(b8C, 0, "  and 0x18708C (0x84A2C)");
    CheckEqI32(b90, 0, "  and 0x187090 (0x84A24)");
    CheckEqI32(b98, 0, "  and 0x187098 (0x84A3C)");
    CheckEqI32(lines.nLineCount, 1, "line 0 is dropped and the flag-40 line survives");
    CheckEqI32(lines.abLineFlags[0], 40, "  shifted down into slot 0");
    CheckEqI32(time.bPaused, 1, "a message is still up, so the clock stays paused (0x84A84)");
    CheckEqI32(bArmed, 0, "and the final-mission control is cleared on this path (0x84A94)");

    /* Flag 13 is a different animal: it rewrites the line, pauses, and skips the whole tail. */
    memset(&lines, 0, sizeof(lines));
    memset(&time, 0, sizeof(time));
    lines.nLineCount = 2;
    lines.abLineFlags[0] = 13;
    lines.abLineFlags[1] = 40;
    lines.anColor[0][1] = 0x1234;
    time.bPaused = 0;
    bSelected = 1; b8C = 1; b90 = 1; b98 = 1; bDone = 0; bArmed = 1;
    pEnt[0]->nTimer = 111;
    Smap_ResolveInterceptUI(&renv);
    CheckEqI32(lines.nLineCount, 2, "flag 13 inserts a line at 1 and drops line 0 (0x849F0 / 0x849FC)");
    CheckEqI32(lines.abLineFlags[0], 11, "the flag-11 line lands at index 0");
    CheckEqI32((i32)lines.anColor[0][0], 0x34, "carrying line 0's colour SLOT 1 masked to a byte (0x849DC)");
    CheckEqI32(lines.abLineFlags[1], 40, "and the line that was at index 1 lands back at index 1");
    CheckEqI32(bDone, 1, "byte_18709C is set on this path (0x84A04)");
    CheckEqI32(time.bPaused, 1, "and the clock is paused (0x84A0C)");
    CheckEqI32(bArmed, 1, "0x84A10 branches to 0x84A9C, past the clear, so the armed byte survives");
    CheckEqI32(bSelected, 1, "and none of the four selection bytes are touched");
    CheckEqI32(b90, 1, "  including 0x187090");
    CheckEqI32(pEnt[0]->nTimer, 111, "flag 13 does not retarget the craft -- only flag 27 and the dismiss arms do");

    /* ORIGINAL BUG (0x84A80): the tail removal is unconditional. A dismiss already dropped line 0,
     * so the tail eats the NEXT message and the clock unpauses a step early. */
    memset(&lines, 0, sizeof(lines));
    memset(&time, 0, sizeof(time));
    lines.nLineCount = 2;
    lines.abLineFlags[0] = 23;
    lines.abLineFlags[1] = 18;
    lines.anColor[0][0] = 0;
    lines.anColor[1][0] = 1;
    time.bPaused = 1;
    bSelected = 1; b8C = 1; b90 = 1; b98 = 1; bDone = 0; bArmed = 1;
    pEnt[0]->nTimer = 111;
    pEnt[1]->nTimer = 222;
    Smap_ResolveInterceptUI(&renv);
    CheckEqI32(pEnt[0]->nTimer, 0, "flag 23 runs Smap_DismissSelected, which retargets craft 0 (0x84A4C)");
    CheckEqI32(pEnt[1]->nTimer, 222, "the flag-18 line under it is not a dismiss, so craft 1 is untouched");
    CheckEqI32(lines.nLineCount, 0, "and the tail removal eats that flag-18 message too (0x84A80)");
    CheckEqI32(time.bPaused, 0, "  which is why the clock unpauses here (0x84A90)");
    CheckEqI32(bArmed, 0, "and the armed byte is cleared on this path");

    /* The three dismiss tests re-read line 0, and every dismiss removes line 0 -- so a second
     * dismissable line underneath is dismissed by the NEXT test, not by the one that saw it. */
    memset(&lines, 0, sizeof(lines));
    memset(&time, 0, sizeof(time));
    lines.nLineCount = 2;
    lines.abLineFlags[0] = 23;
    lines.abLineFlags[1] = 21;
    lines.anColor[0][0] = 0;
    lines.anColor[1][0] = 1;
    time.bPaused = 1;
    pEnt[0]->nTimer = 111;
    pEnt[1]->nTimer = 222;
    Smap_ResolveInterceptUI(&renv);
    CheckEqI32(pEnt[0]->nTimer, 0, "the flag-23 test dismisses craft 0");
    CheckEqI32(pEnt[1]->nTimer, 0, "and the shift hands the flag-21 line to the next test, which dismisses craft 1 (0x84A58)");
    CheckEqI32(lines.nLineCount, 0, "both lines are gone and the guarded tail removal adds nothing (0x84A80)");
    CheckEqI32(time.bPaused, 0, "so the clock resumes");

    /* A null env touches nothing. */
    memset(&lines, 0, sizeof(lines));
    memset(&time, 0, sizeof(time));
    bArmed = 1;
    world.nTickSeconds = 60;
    Smap_ResolveInterceptUI(NULL);
    CheckEqI32(world.nTickSeconds, 60, "a null env does not even run GameTime_Set5Sec");
    CheckEqI32(bArmed, 1, "  and clears nothing");

    for (int i = 0; i < 2; ++i) {
        free(pEnt[i]);
    }
    free(apSlots);
}

/* Smap_OnPointerUp @ 0x84688: either launch the intercept the message line offers, or recentre the
 * view on the strateg item it names and leave the crosshair Geoscape_Draw blits. */
static void TestSmapPointerUp(void)
{
    static UiLineList lines;
    static GameTime time;
    static PtrArray list;
    static GeoscapeWorld world;
    static StrategSpawnCtx ctx;
    static SmapView view;
    static PadImage strip;
    static Ship g_aShip[2];
    static Ship *g_apShip[2];
    static BaseRec g_Base;
    static BaseRec *g_apBase[1];
    static Soldier *g_apSol[1];
    static ObjectRecFull *g_apObj[1];
    static u8 g_abHangar[64];
    static i32 g_anAssigned[1];
    static UiControl ctl[6];
    static UiControl *apCtl[6];
    static StrategItem *pEnt[2];
    static i32 nSeed, nTotalSec, nAssignedCount;
    static i32 nCrossX, nCrossY, nCooldown, nSelShip;
    static u8 nShipCount, once, bPending, bSelected;
    static u16 nSoldierCount;
    ShipRemovalEnv env;
    SmapPointerUpEnv upEnv;
    void **apSlots;

    printf("Smap_OnPointerUp\n");

    for (int i = 0; i < 2; ++i) {
        memset(&g_aShip[i], 0, sizeof(g_aShip[i]));
        g_aShip[i].bId = (u8)(10 + i);
        g_aShip[i].bHomeBase = 0;
        g_aShip[i].bAtBase = 1;
        g_apShip[i] = &g_aShip[i];
    }
    pEnt[0] = (StrategItem *)calloc(1, sizeof(StrategItem));   /* craft 0's own entry */
    pEnt[0]->nType = 1;
    pEnt[0]->bActive = 0;
    pEnt[0]->nDisplayId = 10;
    pEnt[0]->nTimer = 111;
    pEnt[0]->nX = 40;
    pEnt[0]->nY = 60;
    pEnt[1] = (StrategItem *)calloc(1, sizeof(StrategItem));   /* the target the line names */
    pEnt[1]->nType = 5;
    pEnt[1]->bActive = 1;
    pEnt[1]->nX = 300;
    pEnt[1]->nY = 250;
    apSlots = (void **)malloc(sizeof(void *) * 2);
    apSlots[0] = pEnt[0];
    apSlots[1] = pEnt[1];
    list.nCount = 2;
    list.apItems = apSlots;

    memset(&g_Base, 0, sizeof(g_Base));
    g_Base.nUnk8 = 100;
    g_Base.nUnk12 = 200;
    g_apBase[0] = &g_Base;

    nShipCount = 2;
    nSoldierCount = 0;
    nAssignedCount = 0;
    once = 0;
    nTotalSec = 0;
    nSeed = 0;
    world.apCraft = g_apShip;
    world.nShipCount = 2;
    static i32 nMonthCounter;    /* unk_159648: the roll's weight-row index, LDRSB at 0x81AB4 */
    static u16 nAlienPoolAddend; /* 0x1472C8: the roll's early gate, LDRH at 0x81B6C */
    nMonthCounter = 0;
    nAlienPoolAddend = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnDifficulty = &nMonthCounter;
    ctx.pnEarlyGate = &nAlienPoolAddend;
    ctx.pnSeed = &nSeed;
    for (int i = 0; i < 6; ++i) {
        apCtl[i] = &ctl[i];
    }
    strip.uHeight = 40;

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

    memset(&upEnv, 0, sizeof(upEnv));
    upEnv.pbSelectionFlag = &bSelected;
    upEnv.pLines = &lines;
    upEnv.pnSelectedShipIdx = &nSelShip;
    upEnv.pView = &view;
    upEnv.pStrategList = &list;
    upEnv.pStripPad = &strip;
    upEnv.pnCrosshairX = &nCrossX;
    upEnv.pnCrosshairY = &nCrossY;
    upEnv.pbCrosshairPending = &bPending;
    upEnv.pnCooldownR = &nCooldown;
    upEnv.pWorld = &world;
    upEnv.apSpeedCtl = apCtl;
    upEnv.pShipEnv = &env;
    upEnv.pTime = &time;

    /* The selection arm: the line's colour slot 0 is the target, 0x187054 the craft. */
    memset(&lines, 0, sizeof(lines));
    memset(&time, 0, sizeof(time));
    lines.nLineCount = 2;
    lines.abLineFlags[0] = 18;
    lines.abLineFlags[1] = 40;
    lines.anColor[0][0] = 1;                 /* pEnt[1], the target */
    nSelShip = 0;                            /* the selected craft */
    bSelected = 1;
    time.bPaused = 1;
    world.nTickSeconds = 60;
    Smap_OnPointerUp(&upEnv);
    CheckEqI32(g_aShip[0].bIntercepting, 1, "the selection arm launches the intercept (0x84930)");
    CheckEqI32(g_aShip[0].nStrategIdx, 1, "  at the item the line names");
    CheckEqI32(g_aShip[0].bAtBase, 0, "  and the craft leaves base");
    CheckEqI32(g_aShip[0].bWeaponsHot, 1, "  with its weapons hot");
    CheckEqI32(pEnt[0]->nDestX, 300, "  the travel heads for the target x");
    CheckEqI32(pEnt[0]->nDestY, 250, "  and y");
    CheckEqI32(bSelected, 0, "and the selection flag is cleared (0x84938)");
    CheckEqI32(world.nTickSeconds, 60, "the selection arm never runs GameTime_Set5Sec");
    CheckEqI32(lines.nLineCount, 1, "the shared tail drops line 0 (0x84944)");
    CheckEqI32(bPending, 0, "  and leaves no crosshair pending");

    /* The recentre arm, zoom mode 0: 400x400 map, half-extents 80 and 40, target at (300, 250). */
    memset(&lines, 0, sizeof(lines));
    memset(&time, 0, sizeof(time));
    memset(&view, 0, sizeof(view));
    view.nMapW = 400;
    view.nMapH = 400;
    view.nMode = 0;
    lines.nLineCount = 1;
    lines.abLineFlags[0] = 18;
    lines.anColor[0][0] = 1;
    bSelected = 0;
    time.bPaused = 1;
    world.nTickSeconds = 60;
    ctl[4].nState = 1;
    nCooldown = 0xFF;
    Smap_OnPointerUp(&upEnv);
    CheckEqI32(world.nTickSeconds, 5, "the recentre arm runs GameTime_Set5Sec (0x8469C)");
    CheckEqI32(ctl[4].nState, 0, "  and darkens the nav control that was lit");
    CheckEqI32(view.nScrollX, 100, "nScrollX = nX - (nMapW/2 - 120) - 120 (0x84850)");
    CheckEqI32(view.nScrollY, 50, "nScrollY = nY - (nMapH/2 - 160) - 160 (0x8483C)");
    CheckEqI32(nCrossX, 120, "the crosshair x is 120 while nMapW is at or above it (0x84880)");
    CheckEqI32(nCrossY, 160, "nCrossY = nY - nScrollY - v4, which is the fixed 160 whenever no arm replaced the scroll");
    CheckEqI32(bPending, 1, "the crosshair is armed for Geoscape_Draw (0x848B8)");
    CheckEqI32(nCooldown, 0, "  and the radar cooldown is restarted (0x848C4)");
    CheckEqI32(lines.nLineCount, 0, "the tail drops the line");
    CheckEqI32(time.bPaused, 0, "  and the empty list lets the clock resume (0x84954)");

    /* Zoom mode 1 unscales both coordinates by 1/1.333 before the recentre. */
    memset(&view, 0, sizeof(view));
    view.nMapW = 400;
    view.nMapH = 400;
    view.nMode = 1;
    lines.nLineCount = 1;
    lines.abLineFlags[0] = 18;
    lines.anColor[0][0] = 1;
    Smap_OnPointerUp(&upEnv);
    CheckEqI32(view.nScrollX, 25, "mode 1 scales 300 to 225 before the recentre (0x84730)");
    CheckEqI32(view.nScrollY, -13, "  and 250 to 187");
    CheckEqI32(nCrossY, 160, "the crosshair is still 160 -- the unscale moves the view, not the mark");

    /* Zoom mode 2 uses 1/1.694. */
    memset(&view, 0, sizeof(view));
    view.nMapW = 400;
    view.nMapH = 400;
    view.nMode = 2;
    lines.nLineCount = 1;
    lines.abLineFlags[0] = 18;
    lines.anColor[0][0] = 1;
    Smap_OnPointerUp(&upEnv);
    CheckEqI32(view.nScrollX, -23, "mode 2 scales 300 to 177 (0x847B8)");
    CheckEqI32(view.nScrollY, -40, "  and 250 to 147, which is under 160 so the replace arm takes over (0x8484C)");
    CheckEqI32(nCrossY, 147, "  and the crosshair y becomes the SCALED nY");

    /* nY below 160 takes the RSBMI arm: nScrollY becomes -nHalfH instead. */
    memset(&view, 0, sizeof(view));
    view.nMapW = 400;
    view.nMapH = 400;
    view.nMode = 0;
    lines.nLineCount = 1;
    lines.abLineFlags[0] = 18;
    lines.anColor[0][0] = 1;
    pEnt[1]->nY = 100;
    Smap_OnPointerUp(&upEnv);
    CheckEqI32(view.nScrollY, -40, "nY - 160 negative replaces nScrollY with -(nMapH/2 - 160) (0x8484C)");
    CheckEqI32(nCrossY, 100, "  and the crosshair y becomes nY itself");
    pEnt[1]->nY = 250;

    /* The strip-height test can replace nScrollY a second time. Threshold here: 40 + 400 - 320 = 120. */
    memset(&view, 0, sizeof(view));
    view.nMapW = 400;
    view.nMapH = 400;
    view.nMode = 0;
    lines.nLineCount = 1;
    lines.abLineFlags[0] = 18;
    lines.anColor[0][0] = 1;
    pEnt[1]->nY = 350;
    Smap_OnPointerUp(&upEnv);
    CheckEqI32(view.nScrollY, 80, "nScrollY + nHalfH over the strip threshold replaces it with uHeight - nHalfH + nMapH - 320 (0x8487C)");
    CheckEqI32(nCrossY, 230, "and the crosshair y is measured against the REPLACED scroll");
    pEnt[1]->nY = 250;

    /* Odd extents. 0x846B4..0x846D8 is MOVS / ADDMI #1 / ASR #1, and ADDMI (bytes 01 30 83 42) fires
     * only when the tested value is NEGATIVE: a non-negative extent is halved by plain `n >> 1`, and
     * the +1 exists only to make a negative extent round toward zero like C division. So 401 halves to
     * 200, NOT 201, and an odd map centres exactly where an even one of the same integer half does. */
    memset(&view, 0, sizeof(view));
    view.nMapW = 401;
    view.nMapH = 401;
    view.nMode = 0;
    lines.nLineCount = 1;
    lines.abLineFlags[0] = 18;
    lines.anColor[0][0] = 1;
    Smap_OnPointerUp(&upEnv);
    CheckEqI32(view.nScrollX, 100, "401/2 - 120 is 80, not ceil's 81, so nScrollX is 100 (0x846C4)");
    CheckEqI32(view.nScrollY, 50, "  and 401/2 - 160 is 40, not ceil's 41, so nScrollY is 50 (0x846D8)");
    CheckEqI32(nCrossY, 160, "the crosshair y is still the fixed 160 (nY - nScrollY - nHalfH)");

    /* A small map: the crosshair y wraps once by nMapH and the x is 120 - nMapW. */
    memset(&view, 0, sizeof(view));
    view.nMapW = 100;
    view.nMapH = 100;
    view.nMode = 0;
    lines.nLineCount = 1;
    lines.abLineFlags[0] = 18;
    lines.anColor[0][0] = 1;
    pEnt[1]->nX = 120;
    pEnt[1]->nY = 120;
    Smap_OnPointerUp(&upEnv);
    CheckEqI32(nCrossX, 20, "nMapW below 120 gives 120 - nMapW (0x848A0)");
    CheckEqI32(nCrossY, 200, "and the y wraps ONCE by nMapH, not until it fits (0x84894)");
    pEnt[1]->nX = 300;

    free(apSlots);
    for (int i = 0; i < 2; ++i) {
        free(pEnt[i]);
    }
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
    static u8  g_nResearchEntries;
    static u16 g_nObjectCount, g_nSoldierCount;
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
    static i32 g_nMonth;    /* unk_159648: the roll's weight-row index, LDRSB at 0x81AB4 */
    static u16 g_nAddend;   /* 0x1472C8: the roll's early gate, LDRH at 0x81B6C */
    g_nMonth = 0;
    g_nAddend = 0;
    memset(&g_Ctx, 0, sizeof(g_Ctx));
    g_nSeed = 0;
    g_Ctx.pnDifficulty = &g_nMonth;
    g_Ctx.pnEarlyGate = &g_nAddend;
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
        env.pnMsgShipIdx = &nStratIdx2;                                                          \
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

    /* Case 6: the assault-confirm nav button, Smap_BeginCraftSelectBtn @ 0x84634. The original is two
     * instructions -- `STRB R1` storing 1 into bCraftSelectAssaultFlag @ 0x1870A8 (0x8463C), then the
     * tail call into Smap_BeginCraftSelect. The flag is not inert: 0x839E8 reads it and the arm at
     * 0x83A04 forces Map_nMissionType = MissionType_MarsOrSpecial with sprite sets 13/21/23/23. So the
     * pin is a pair of runs from the same RESET state -- once with the flag set by hand and the plain
     * call, once with the flag clear and the button call -- and both must land in the same place. */
    RESET(21, 0);
    bAssault = 1;
    nRet = Smap_BeginCraftSelect(&env);
    CheckEqI32(nMissionType, 9, "the assault flag forces the Mars/special mission type (0x83A1C)");
    CheckEqI32(abSprite[0], 13, "with that arm's sprite set 13/21/23/23");
    CheckEqI32(abSprite[1], 21, "  second entry 21");
    free(pShip);
    PtrArray_DeleteAll(&g_List);

    RESET(21, 0);
    bAssault = 0;
    nRet = Smap_BeginCraftSelectBtn(&env);
    CheckEqI32(bAssault, 1, "the button stores 1 into the assault flag before it runs (0x8463C)");
    CheckEqI32(nMissionType, 9, "and the run it launches is the flag-armed one");
    CheckEqI32(abSprite[0], 13, "  same sprite set as the hand-set flag");
    CheckEqI32(nRet, 7, "  same launch tail as the plain call");
    free(pShip);
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

    printf("Base_CalcPsiLabCap @ 0x6598c, Base_CountSoldiers @ 0x661d8, Base_GetItemQtyA @ 0x66198 (slot 113), "
           "Base_GetItemQtyB @ 0x661b8 (slot 112), Base_CalcMonthlyUpkeep @ 0x6a230\n");

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
static u32 g_apProductA[200 * 4];
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
    u8 nResearchEntries = 1;
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
    g_apProductA[200 * 1] = 11111111u;
    g_apProductA[200 * 2] = 22222222u;
    g_apResearch[144 * 1] = 55;
    g_apResearch[144 * 2] = 66;

    memset(&world, 0, sizeof(world));
    world.apCraft = apCraft;
    world.nShipCount = 2;
    static i32 nMonthCounter;    /* unk_159648: the roll's weight-row index, LDRSB at 0x81AB4 */
    static u16 nAlienPoolAddend; /* 0x1472C8: the roll's early gate, LDRH at 0x81B6C */
    nMonthCounter = 0;
    nAlienPoolAddend = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnDifficulty = &nMonthCounter;
    ctx.pnEarlyGate = &nAlienPoolAddend;
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
    env.apProductA = g_apProductA;
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
    CheckEqI32(g_apProductC[200 * 1], 2222, "auManufCommitted shifts left (0x67088 -> 0x67090)");
    CheckEqI32(g_apProductB[200 * 1], 22222, "aManufProductB shifts left (0x670A0 -> 0x670A8)");
    CheckEqI32((i32)g_apProductA[200 * 1], 22222222,
               "the accumulator shifts left too (0x670B0 LDR [R1,#4] -> 0x670BC STR [R1,#-0x31C])");
    Check(g_apProductA[200 * 2] == 22222222u, "and its source column is not zeroed by the shift");
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
    /* Four columns, two bases each (200 entries), so a clear that reached the wrong column or the
     * wrong base shows up as a sentinel that moved. */
    static u32 prodU32[400];          /* the accumulator @ 0x159E9C */
    static i32 prodReq[400];          /* aManufProductB @ 0x15B79C */
    static u16 prodU16[400];          /* auManufCommitted @ 0x15D09C */
    static u16 manufCol[400];         /* auManufCol @ 0x15E19C */
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
    for (i = 0; i < 400; ++i) {
        prodU32[i] = 0xAAAAAAAAu;
        prodReq[i] = 0xAAAAAAAA;
        prodU16[i] = 0xAAAA;
        manufCol[i] = 0xAAAA;
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
    env.paRegionBuildCount = kindTable;
    env.pProductU32Col = prodU32;
    env.pProductReqCol = prodReq;
    env.pProductU16Col = prodU16;
    env.pManufCol = manufCol;
    env.pResearchCol = research;

    printf("\nBase_CreateNamedEntry @ 0x66930\n");

    nRet = Base_CreateNamedEntry(&env, -6000, -6000, PUFO_REGION_EUROPE, "alpha");

    CheckEqI32(nRet, 1560, "the return is the 0x618 residue");
    CheckEqI32(bBaseCount, 1, "the base count is raised");
    CheckEqI32(siteId, 501, "and the next site id");

    Check(apBases[0] != NULL, "BaseRec_Create stored a record at slot 0");
    CheckEqI32(apBases[0]->bRegionId, 0x82, "bRegionId = the region");
    CheckEqI32(apBases[0]->nUnk8, -6000, "nUnk8 = x");
    CheckEqI32(apBases[0]->nUnk12, -6000, "nUnk12 = y");
    CheckEqI32(apBases[0]->bUnk16, 3, "bUnk16 = the Europe counter (kindTable[2] = 4) minus 1");
    CheckEqI32(objs[0].auPerBaseVal[0], 0, "the ctor zeroed slot 0 in every object");
    CheckEqI32(objs[0].auPerBaseVal[1], 7, "and left the other slots alone");

    CheckEqI32(list.nCount, 1, "one strateg entry was added");
    pItem = (StrategItem *)PtrArray_Get(&list, 0);
    CheckEqI32(pItem->nType, 0, "type 0");
    CheckEqI32(pItem->nX, -6000, "at x");
    CheckEqI32(pItem->nSiteId, 500, "with the site id (type 0 writes nSiteId)");
    CheckEqI32(pItem->nDisplayId, -1, "and nDisplayId -1");
    CheckEqI32(pItem->bRandA, 2, "bRandA = the region index (0x82 - 0x80)");
    CheckEqI32(pItem->bVisible, 1, "visible");
    CheckEqI32(pItem->nSeed, 5, "seeded from the counter");
    CheckEqI32(seed, 6, "and the counter advanced");

    CheckEqI32(sites[0].bType, 2, "site bType = region - 0x80");
    Check(strcmp(sites[0].abName, "alpha") == 0, "the name landed in abName");
    CheckEqI32(sites[0].nFacType, 500, "nFacType = the site id");
    CheckEqI32(sites[0].nUnk48, 0, "the three scratch dwords are cleared");

    CheckEqI32((i32)research[0], 0, "research column cleared");
    CheckEqI32((i32)prodU16[2], 0, "auManufCommitted [0..2] cleared (0x66B24 STRH [R3])");
    CheckEqI32((i32)manufCol[2], 0, "auManufCol [0..2] cleared (0x66B1C STRH [R3,#0x1100])");
    CheckEqI32(prodReq[2], 0, "aManufProductB [0..2] cleared (0x66B2C STR [LR,#0x1900])");
    CheckEqI32((i32)prodU32[2], 0, "the accumulator [0..2] cleared (0x66B34 STR [LR])");
    Check(prodU32[3] == 0xAAAAAAAAu, "the entry past the object count is untouched");
    Check(prodU16[3] == 0xAAAA && manufCol[3] == 0xAAAA && (u32)prodReq[3] == 0xAAAAAAAAu,
          "and every other column stops at the object count too");
    Check(prodU16[200] == 0xAAAA && manufCol[200] == 0xAAAA && (u32)prodReq[200] == 0xAAAAAAAAu
          && prodU32[200] == 0xAAAAAAAAu, "the next base keeps its entry in all four columns");

    /* A 30-character name: the original would run past the 20-byte field; the port truncates
     * at 19 + NUL and the record's own fields stay clean. The column pointers move with the base,
     * as the original's +800 / +400 per-base advance does. */
    env.pProductU32Col = prodU32 + 200;
    env.pProductReqCol = prodReq + 200;
    env.pProductU16Col = prodU16 + 200;
    env.pManufCol = manufCol + 200;
    (void)Base_CreateNamedEntry(&env, 100, 200, 0, "012345678901234567890123456789");
    CheckEqI32(bBaseCount, 2, "second base created");
    Check(strcmp(sites[1].abName, "0123456789012345678") == 0, "the long name is truncated to 19");
    CheckEqI32(sites[1].nUnk48, 0, "and nothing spilled into the fields below");
    CheckEqI32(sites[1].bType, 0x80, "region 0 (Game_StartNew) gives bType 0 - 0x80 as a byte");
    CheckEqI32(apBases[1]->bUnk16, 0xFF, "and region 0 has no counter, so bUnk16 is 0 - 1");
    Check(prodU16[202] == 0 && manufCol[202] == 0 && prodReq[202] == 0 && prodU32[202] == 0u,
          "the re-offset pointers clear the second base's own four columns");
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

    sols[0].nKind = 0; sols[0].bPsiAssigned = 1;
    sols[1].nKind = 0; sols[1].bPsiAssigned = 0; /* assigned but not psi */
    sols[2].nKind = 1; sols[2].bPsiAssigned = 1; /* psi at another base */
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

/* BaseFac_FormatCostLabel @ 0x6a404: the region's own name, the dash, the region counter, and
 * the increment. The names the shipped game prints are TEXTS[1033..1042] -- "Северная Америка"
 * for 0x80 through "Здесь нельзя строить" for 0x89 -- so the synthetic records here sit at the
 * same indices and are named Reg1033..Reg1042. */
static void TestFacCostLabel(void)
{
    static u8 aTexts[PUFO_TEXT_REC_STRIDE * (PUFO_REGION_NAME_REC_BASE + 0x8A)];
    static i8 abRegion[PUFO_REGION_BUILD_COUNT];
    static char szLabel[64];
    const char *psz;

    printf("BaseFac_FormatCostLabel\n");

    memset(aTexts, 0, sizeof(aTexts));
    for (i32 k = 0; k < 10; ++k) {
        char szName[16];
        (void)snprintf(szName, sizeof(szName), "Reg%d", PUFO_REGION_NAME_REC_BASE + 0x80 + k);
        memcpy(aTexts + (size_t)PUFO_TEXT_REC_STRIDE * (PUFO_REGION_NAME_REC_BASE + 0x80 + k),
               szName, strlen(szName));
    }
    memcpy(aTexts + (size_t)PUFO_TEXT_REC_STRIDE * PUFO_REGION_NAME_REC_BASE, "Reg905", 6);

    memset(abRegion, 0, sizeof(abRegion));
    psz = BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, PUFO_REGION_NORTH_AMERICA, abRegion);
    Check(psz == (const char *)szLabel, "the return is the label buffer it was given");
    Check(strcmp(szLabel, "Reg1033-0") == 0, "TEXTS[905 + region], a dash, then the region counter");
    CheckEqI32(abRegion[0], 1, "and the counter is incremented for next time");

    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, PUFO_REGION_NORTH_AMERICA, abRegion);
    Check(strcmp(szLabel, "Reg1033-1") == 0, "the second call shows the incremented counter");
    CheckEqI32(abRegion[0], 2, "which is one more again");

    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, PUFO_REGION_CANADA, abRegion);
    Check(strcmp(szLabel, "Reg1041-0") == 0,
          "region 0x88 reads TEXTS[1041] and its own counter slot (index 8)");

    /* Region 0x89 ("Здесь нельзя строить") has no counter of its own: the original's store at
     * 0x18655D + 9 lands on 0x186566, the global IDA names bUiFrameStyle. The port's tenth byte
     * is the same slot of the same sweep StartNew writes. */
    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, PUFO_REGION_NO_BUILD, abRegion);
    Check(strcmp(szLabel, "Reg1042-0") == 0, "region 0x89 reads TEXTS[1042]");
    CheckEqI32(abRegion[9], 1, "and its slot is the tenth byte (0x186566 in the original)");

    /* The counter is loaded SIGNED and printed as a signed decimal, so a negative entry gives a
     * label with two dashes. */
    abRegion[3] = -3; /* region 0x83, Asia */
    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, PUFO_REGION_ASIA, abRegion);
    Check(strcmp(szLabel, "Reg1036--3") == 0, "a negative counter prints as a negative number");
    CheckEqI32(abRegion[3], -2, "and +1 moves it towards zero");

    /* The increment is sign-extended before the byte store, so 127 wraps to -128. */
    abRegion[4] = 127; /* region 0x84, Australia */
    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, PUFO_REGION_AUSTRALIA, abRegion);
    Check(strcmp(szLabel, "Reg1037-127") == 0, "127 prints as 127");
    CheckEqI32(abRegion[4], -128, "and the stored +1 wraps to -128");

    /* A value below the region range (Game_StartNew passes 0 for the starting base) has no
     * counter: the port reads 0 and stores nothing. */
    (void)BaseFac_FormatCostLabel(szLabel, sizeof(szLabel), aTexts, 0, abRegion);
    Check(strcmp(szLabel, "Reg905-0") == 0, "a value below 0x80 reads TEXTS[905] and no counter");
    CheckEqI32(abRegion[0], 2, "and the North America counter is left alone");

    /* Deviation: the original copies the name with no bound at all. */
    szLabel[0] = 'X';
    szLabel[1] = 'X';
    (void)BaseFac_FormatCostLabel(szLabel, 1, aTexts, PUFO_REGION_NORTH_AMERICA, abRegion);
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

/* A TEXTS block big enough that the cursor records 1595..1598 can be addressed by number. The fixture
 * puts its own labels in those four slots (the shipped strings are pinned in tests/test_texts_lang.c),
 * so a setter wired to the wrong record shows up as the wrong string. */
static u8 g_abRadarCursorTexts[1599 * PUFO_TEXT_REC_STRIDE];

static void RadarPutCursorRec(i32 nRec, const char *psz)
{
    u8 *p = g_abRadarCursorTexts + (size_t)nRec * PUFO_TEXT_REC_STRIDE;
    size_t i = 0;

    do {
        p[i] = (u8)psz[i];
    } while (psz[i++] != 0);
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
    env.pnLevel = &nLevel; env.pnBoundA = &nBoundA; env.pnBoundB = &nBoundB; \
    env.pTextRecs = g_abRadarCursorTexts;

    RadarPutCursorRec(1595, "PursueNW");
    RadarPutCursorRec(1596, "MaxRangeNE");
    RadarPutCursorRec(1597, "MinRangeSW");
    RadarPutCursorRec(1598, "StopSE");

    /* 1. the four corners. Each one also writes its own mode text into szRadarMsg -- TEXTS 1595..1598,
     * the ARM's 0x10360F / 0x1036DC / 0x1037A9 / 0x103876 (0x5E7C0 and friends). */
    RC_RESET();
    RadarScreen_SetCursorNW(&env);
    CheckEqI32(nX, 117, "NW x (0x5E77C)");
    CheckEqI32(nY, 86, "  y (0x5E788)");
    CheckEqI32(nRange, 560, "  the range target is the fixed 560 (0x5E794)");
    CheckEqI32(bFlip, 0, "  and the flip byte is cleared");
    CheckEqI32(nTimer, 0, "  the message timer is reset (0x5E7DC)");
    Check(strcmp(szMsg, "PursueNW") == 0, "  and the message string takes TEXTS[1595] (0x5E7C0)");

    RC_RESET();
    RadarScreen_SetCursorNE(&env);
    CheckEqI32(nX, 161, "NE x (0x5E848)");
    CheckEqI32(nY, 86, "  y, the same north row as NW");
    CheckEqI32(bFlip, 0, "  flip cleared");
    Check(strcmp(szMsg, "MaxRangeNE") == 0, "  and NE writes TEXTS[1596]");

    RC_RESET();
    RadarScreen_SetCursorSW(&env);
    CheckEqI32(nX, 117, "SW x, the same west column as NW");
    CheckEqI32(nY, 107, "  y (0x5E98C)");
    CheckEqI32(bFlip, 0, "  flip cleared");
    Check(strcmp(szMsg, "MinRangeSW") == 0, "  and SW writes TEXTS[1597]");

    RC_RESET();
    RadarScreen_SetCursorSE(&env);
    CheckEqI32(nX, 161, "SE x");
    CheckEqI32(nY, 107, "  y");
    CheckEqI32(nRange, 560, "  the fixed 560 again (0x5EA9C)");
    CheckEqI32(bFlip, 1, "  and SE is the only corner that SETS the flip byte (0x5EAA8)");
    Check(strcmp(szMsg, "StopSE") == 0, "  and SE writes TEXTS[1598]");

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
        Check(false, "the strateg entry could not be created"); /* counted: a bare printf would exit 0 */
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
    (env).nManufEntries = 144;  /* 0x160C4C, the count Base_CountActiveManuf reads */ \
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

/* Base_CountActiveManuf @ 0x68264 takes no entry count in the record: 0x68278 reads the shared byte at
 * 0x160C4C (144, written once at 0x859E4 in Geoscape_InitUI; LoadGame 0x45374 and Base_DemolishFacility
 * 0x66FF8 only read it) and sums exactly that many u16 of the base's 200-entry column. The port hoists the
 * call to its callers, so each one has to pass the shared count, not the column size: reading 200 would let
 * a quantity at rows 144..199 reach a budget the original never sees. */
static void TestManufCountIsTheSharedByte(void)
{
    BaseTickConstructionEnv env;
    i32 i;

    printf("Base_CountActiveManuf @ 0x68264: the count is the byte at 0x160C4C\n");

    memset(g_aTickManuf, 0, sizeof(g_aTickManuf));
    g_aTickManuf[143] = 100;
    CheckEqI32(Base_CountActiveManuf(g_aTickManuf, 144), 100,
               "row 143 is the last row inside the 144 the record reads");
    memset(g_aTickManuf, 0, sizeof(g_aTickManuf));
    g_aTickManuf[144] = 100;
    CheckEqI32(Base_CountActiveManuf(g_aTickManuf, 144), 0,
               "row 144 is outside it, even though the column holds 200 entries");

    /* The budget the hoisted call buys, through Base_TickFacilityConstruction's pass at 0x62920. */
    TICK_RESET();
    TICKFAC(PUFO_GRID_FAC_TYPE_OFF, 0, 0) = FAC_WORKSHOP;
    TICKDAYS(3, 3) = 1;
    g_aTickManuf[144] = 100;
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKFAC(PUFO_GRID_LAYER2_OFF, 0, 0), 0,
               "a workshop is not wanted for manufacturing the record cannot count");

    TICK_RESET();
    TICKFAC(PUFO_GRID_FAC_TYPE_OFF, 0, 0) = FAC_WORKSHOP;
    TICKDAYS(3, 3) = 1;
    g_aTickManuf[143] = 100;
    TICK_ENV(env);
    Base_TickFacilityConstruction(&env);
    CheckEqI32(TICKFAC(PUFO_GRID_LAYER2_OFF, 0, 0), 1,
               "one row earlier it is 100 / 50 + 1 = 3 cells of budget, so the flag is raised");

    memset(g_aTickManuf, 0, sizeof(g_aTickManuf));
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

/* ---- Geoscape_Draw @ 0x8c180 map-view chrome --------------------------------------- */
#define MV_PAD_W        240 /* the map pad glyph the shipped game carries */
#define MV_PAD_H          1
#define MV_MAP_SLOT       7
#define MV_W              8 /* the world bitmap, as the backdrop test's */
#define MV_H              3
#define MV_SCROLL_X     -30 /* puts scroll + halfW exactly on the wrap boundary */
#define MV_SCROLL_Y      10
#define MV_MAP_SIZE     100
#define MV_INK       0x0123 /* palette slot 7 entry 1, what the map pad paints */
#define MV_XHAIR_INK  13894 /* Blit_PackedColor(50) with mid 200: (6<<11) + 32*50 + 6 */

static SoftSurf g_mvSurf;
static SmapView g_mvView;
static PadImage g_mvMapPad;
static PadImage g_mvLayerPad;
static PadImage g_mvChromePad;
static u8 g_abMvWorld[4 + MV_W * MV_H];
static u8 g_abMvMask[4 + MV_W * MV_H];
static u8 g_abMvGlyph[4 + MV_PAD_W * MV_PAD_H];
static GameTime g_mvTime;
static i32 g_nMvOverlay;
static u8 g_bMvPending;
static i32 g_nMvCooldown;
static GeoscapeMapViewEnv g_mvEnv;
static GeoscapeCrosshairEnv g_mvXh;

static i32 MvPix(i32 x, i32 y)
{
    return (i32)((const u16 *)g_mvSurf.pPixels)[y * g_mvSurf.nWidth + x];
}

static void MvFixture(void)
{
    memset(g_mvSurf.pPixels, 0, (size_t)g_mvSurf.nWidth * (size_t)g_mvSurf.nHeight * 2u);
    memset(g_aPalette, 0, sizeof(g_aPalette));

    /* The world bitmap: row 0 index 1, row 1 index 2, row 2 index 3. */
    g_abMvWorld[0] = (u8)MV_W; g_abMvWorld[1] = 0;
    g_abMvWorld[2] = (u8)MV_H; g_abMvWorld[3] = 0;
    for (i32 y = 0; y < MV_H; ++y) {
        for (i32 x = 0; x < MV_W; ++x) {
            g_abMvWorld[4 + y * MV_W + x] = (u8)(y + 1);
        }
    }
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][1] = 0xFFFFu;
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][2] = 0x001Fu;
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][3] = 0xF800u;

    /* A fully opaque alpha plane, so each flip lands as the palette colour exactly. */
    g_mvLayerPad.pData   = g_abMvMask;
    g_mvLayerPad.nMode   = (u8)PUFO_RADAR_MAP_PAL_SLOT;
    g_mvLayerPad.uWidth  = (u16)MV_W;
    g_mvLayerPad.uHeight = (u16)MV_H;
    g_abMvMask[0] = (u8)MV_W; g_abMvMask[1] = 0;
    g_abMvMask[2] = (u8)MV_H; g_abMvMask[3] = 0;
    memset(g_abMvMask + 4, 0xFF, (size_t)MV_W * (size_t)MV_H);

    /* The map pad is the shipped 240 x 1 strip, which is also what the drift divides by. */
    g_abMvGlyph[0] = (u8)MV_PAD_W; g_abMvGlyph[1] = 0;
    g_abMvGlyph[2] = (u8)MV_PAD_H; g_abMvGlyph[3] = 0;
    memset(g_abMvGlyph + 4, 1, (size_t)MV_PAD_W * (size_t)MV_PAD_H);
    g_aPalette[MV_MAP_SLOT][1] = MV_INK;
    g_mvMapPad.pData   = g_abMvGlyph;
    g_mvMapPad.nMode   = (u8)MV_MAP_SLOT;
    g_mvMapPad.uWidth  = (u16)MV_PAD_W;
    g_mvMapPad.uHeight = (u16)MV_PAD_H;

    memset(&g_mvView, 0, sizeof(g_mvView));
    g_mvView.nScrollX = MV_SCROLL_X;
    g_mvView.nScrollY = MV_SCROLL_Y;
    g_mvView.nMapW    = MV_MAP_SIZE;
    g_mvView.nMapH    = MV_MAP_SIZE;

    memset(&g_mvTime, 0, sizeof(g_mvTime));
    g_mvTime.nHour   = 3;
    g_mvTime.nMinute = 30;

    g_mvEnv.pView           = &g_mvView;
    g_mvEnv.pMapPad         = &g_mvMapPad;
    g_mvEnv.pLayerPad       = &g_mvLayerPad;
    g_mvEnv.pWorldBits      = g_abMvWorld;
    g_mvEnv.pMapPal         = g_aPalette[PUFO_RADAR_MAP_PAL_SLOT];
    g_mvEnv.pTime           = &g_mvTime;
    g_mvEnv.nScreenId       = 1;
    g_mvEnv.pnOverlayOriginX = &g_nMvOverlay;

    g_mvChromePad.uHeight = 20; /* the crosshair line is 320 - 20 tall */
    g_bMvPending   = 1;
    g_nMvCooldown  = 0;
    g_mvXh.pChromePad  = &g_mvChromePad;
    g_mvXh.nCrosshairX = 100;
    g_mvXh.nCrosshairY = 50;
    g_mvXh.pbPending   = &g_bMvPending;
    g_mvXh.pnCooldownR = &g_nMvCooldown;

    /* Sentinels: a guard that draws nothing has to leave them in place. */
    g_mvMapPad.nOriginX   = -1;
    g_mvMapPad.nOriginY   = -1;
    g_mvLayerPad.nOriginX = -1;
    g_mvLayerPad.nOriginY = -1;
    g_nMvOverlay          = 0x7F7F;
}

static void TestGeoscapeMapView(void)
{
    printf("\nGeoscape_Draw @ 0x8c180 map-view chrome (0x8C2B0..0x8C690)\n");

    SoftSurf_ctor(&g_mvSurf);
    SoftSurf_AllocPixels(&g_mvSurf, BD_SURF_W, BD_SURF_H, PIXMODE_16BPP);
    g_pMainSurface = &g_mvSurf;
    Render_SetTarget(BD_SURF_W, BD_SURF_H, 2, 2, BD_SURF_W * 2, g_mvSurf.pPixels);

    /* --- the overlay drift: (nHour * (uWidth / -24)) - uWidth * nMinute / 1440 --- */
    MvFixture();
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_nMvOverlay, -35, "240 / -24 is -10, so hour 3 gives -30 and 240*30/1440 gives 5");

    MvFixture();
    g_mvTime.nHour = 0; g_mvTime.nMinute = 1;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_nMvOverlay, 0, "0 - 0.1667 truncates toward zero, not down to -1");

    MvFixture();
    g_mvTime.nHour = 1; g_mvTime.nMinute = 1;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_nMvOverlay, -10, "-10 - 0.1667 truncates to -10");

    MvFixture();
    g_mvTime.nHour = 0; g_mvTime.nMinute = 30;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_nMvOverlay, -5, "the hour term alone is zero, so only the minute term counts");

    /* 240 * 6 / 1440 is exactly 1, so the minute term crosses a whole pixel at six minutes:
     * this is what pins 1440 rather than any nearby divisor. */
    MvFixture();
    g_mvTime.nHour = 0; g_mvTime.nMinute = 5;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_nMvOverlay, 0, "five minutes is 0.833 of a pixel, truncated to 0");

    MvFixture();
    g_mvTime.nHour = 0; g_mvTime.nMinute = 6;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_nMvOverlay, -1, "six minutes is exactly one pixel");

    /* --- the blits, screen id 1: halfW = 100/2 - 120 = -70, halfH = -110, overlay 0,
     * so the three world offsets land at 100, 0 and 200 --- */
    MvFixture();
    g_mvTime.nHour = 0;
    g_mvTime.nMinute = 0;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_nMvOverlay, 0, "hour 0 and minute 0 put the overlay origin at 0");
    CheckEqI32(g_mvMapPad.nOriginX, 0, "the last map blit is at -(scroll + nMapW + halfW)");
    CheckEqI32(g_mvMapPad.nOriginY, 100, "and at -(scroll + halfH)");
    CheckEqI32(g_mvLayerPad.nOriginX, 200, "the last world flip is overlay - scroll - halfW + nMapW");
    CheckEqI32(g_mvLayerPad.nOriginY, 100, "at the y copied out of the map pad");

    CheckEqI32(MvPix(100, 100), 0xF7DE, "the first world flip paints at overlay - scroll - halfW");
    CheckEqI32(MvPix(100, 101), 0x001E, "world row 1");
    CheckEqI32(MvPix(100, 102), 0xF000, "world row 2");
    CheckEqI32(MvPix(107, 102), 0xF000, "eight columns wide");
    CheckEqI32(MvPix(108, 100), (i32)MV_INK, "and no further, so the map pad shows through");
    CheckEqI32(MvPix(0, 100), 0xF7DE, "the -nMapW flip paints at 0");
    CheckEqI32(MvPix(7, 102), 0xF000, "eight columns wide");
    CheckEqI32(MvPix(8, 100), (i32)MV_INK, "with the map pad past it");
    CheckEqI32(MvPix(200, 100), 0xF7DE, "the +nMapW flip paints at 200");
    CheckEqI32(MvPix(207, 102), 0xF000, "again eight wide");
    CheckEqI32(MvPix(199, 100), (i32)MV_INK, "with the map pad before it");
    CheckEqI32(MvPix(50, 100), (i32)MV_INK, "the map pad covers the gap between the flips");
    CheckEqI32(MvPix(100, 103), 0, "the map pad is one row tall and the flips are three");
    CheckEqI32(MvPix(60, 0), 0, "screen 1 gets no vertical wrap copy");
    CheckEqI32(g_mvView.nScrollX, MV_SCROLL_X, "scroll + halfW exactly on -nMapW is NOT wrapped");
    CheckEqI32(g_mvView.nScrollY, MV_SCROLL_Y, "and screen 1 leaves y alone");

    /* --- screen id 2: the two vertical wrap copies, and no world flips --- */
    MvFixture();
    g_mvTime.nHour = 0;
    g_mvTime.nMinute = 0;
    g_mvEnv.nScreenId = PUFO_RADAR_SCREEN_NO_WRAP;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(MvPix(60, 0), (i32)MV_INK, "screen 2 adds the y + nMapH copy at row 0");
    CheckEqI32(MvPix(230, 0), (i32)MV_INK, "and its x + nMapW partner covers the row too");
    CheckEqI32(MvPix(100, 100), (i32)MV_INK, "screen 2 paints no world bitmap at the first offset");
    CheckEqI32(MvPix(0, 100), (i32)MV_INK, "nor at the -nMapW offset");
    CheckEqI32(MvPix(200, 100), (i32)MV_INK, "nor at the +nMapW offset");
    CheckEqI32(g_mvLayerPad.nOriginX, 200, "but the layer origins are still rewritten");
    CheckEqI32(g_mvLayerPad.nOriginY, 100, "to the same values");

    /* --- the wrap: X for every id, Y only for id 2 --- */
    MvFixture();
    g_mvView.nScrollX = 200;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_mvView.nScrollX, 100, "200 + halfW is 130 > 0, so one map width comes off");
    CheckEqI32(g_mvView.nScrollY, MV_SCROLL_Y, "and screen 1 never touches y");

    MvFixture();
    g_mvView.nScrollY = 200;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_mvView.nScrollY, 200, "screen 1 leaves an out-of-band y exactly where it was");

    MvFixture();
    g_mvView.nScrollY = 200;
    g_mvEnv.nScreenId = PUFO_RADAR_SCREEN_NO_WRAP;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_mvView.nScrollY, 100, "screen 2 wraps it by one map width");

    MvFixture();
    g_mvView.nScrollX = -40;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_mvView.nScrollX, 60, "-40 + halfW is -110 < -nMapW, so one map width is added");

    /* --- a broken env draws nothing and touches nothing --- */
    MvFixture();
    g_mvEnv.pMapPad = NULL;
    Geoscape_BlitMapView(&g_mvEnv);
    CheckEqI32(g_mvLayerPad.nOriginX, -1, "a missing map pad leaves the sentinels alone");
    CheckEqI32(g_nMvOverlay, 0x7F7F, "and leaves the overlay alone");
}

static void TestGeoscapeCrosshair(void)
{
    printf("\nGeoscape_Draw @ 0x8c180 crosshair tail (0x8C69C..0x8C760)\n");

    /* The surface is the map-view test's; it is already allocated and MvFixture clears it. */
    Render_SetTarget(BD_SURF_W, BD_SURF_H, 2, 2, BD_SURF_W * 2, g_mvSurf.pPixels);

    MvFixture();
    Geoscape_BlitCrosshair(&g_mvXh);
    CheckEqI32(MvPix(103, 0), MV_XHAIR_INK, "the vertical line starts at nSmapCrosshairX + 3");
    CheckEqI32(MvPix(103, 299), MV_XHAIR_INK, "and runs 320 - 20 rows tall");
    CheckEqI32(MvPix(103, 300), 0, "one row past the chrome pad's height is empty");
    CheckEqI32(MvPix(104, 100), 0, "one pixel wide");
    CheckEqI32(MvPix(0, 53), MV_XHAIR_INK, "the horizontal line at nSmapCrosshairY + 3");
    CheckEqI32(MvPix(239, 53), MV_XHAIR_INK, "the full 240 wide");
    CheckEqI32(MvPix(0, 54), 0, "one pixel tall");
    CheckEqI32(g_bMvPending, 1, "the pending flag stays set while it draws");
    CheckEqI32(g_nMvCooldown, 0, "and the cooldown is untouched");

    MvFixture();
    g_nMvCooldown = 4;
    Geoscape_BlitCrosshair(&g_mvXh);
    CheckEqI32(MvPix(103, 100), MV_XHAIR_INK, "cooldown 4 still draws");

    MvFixture();
    g_nMvCooldown = 5;
    Geoscape_BlitCrosshair(&g_mvXh);
    CheckEqI32(MvPix(103, 100), 0, "cooldown 5 draws nothing");
    CheckEqI32(MvPix(0, 53), 0, "on either axis");
    CheckEqI32(g_bMvPending, 0, "and clears the pending flag");
    CheckEqI32(g_nMvCooldown, PUFO_RADAR_COOLDOWN_READY, "and parks the cooldown at 0xFF");

    MvFixture();
    g_bMvPending = 0;
    Geoscape_BlitCrosshair(&g_mvXh);
    CheckEqI32(MvPix(103, 100), 0, "a clear flag draws nothing even with the cooldown ready");
    CheckEqI32(g_nMvCooldown, PUFO_RADAR_COOLDOWN_READY, "and still parks the cooldown");

    MvFixture();
    g_mvXh.pChromePad = NULL;
    Geoscape_BlitCrosshair(&g_mvXh);
    CheckEqI32(MvPix(103, 100), 0, "a missing chrome pad draws nothing");
    CheckEqI32(g_bMvPending, 1, "and leaves the pending flag as it was");
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
static u8  g_bRadarDone, g_bRadarAnim;
static i32 g_nRadarMsgTimer, g_nRadarTargetHp, g_nRadarTargetHpMax, g_nRadarLevel;
static i32 g_nRadarRangeLimit, g_nRadarRangeTarget, g_nRadarRangeCur;
static i32 g_nRadarAmmoL, g_nRadarAmmoR, g_nRadarCurX, g_nRadarCurY, g_nRadarScan;
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

static volatile i32 g_nRadarClickFired = -1; /* distinct bodies: five empty ones fold to one address,
                                              * which makes the "each control gets its handler in order"
                                              * loop below unable to see a wrong slot */
static void RadarClickNW(void) { g_nRadarClickFired = 0; }
static void RadarClickNE(void) { g_nRadarClickFired = 1; }
static void RadarClickSW(void) { g_nRadarClickFired = 2; }
static void RadarClickSE(void) { g_nRadarClickFired = 3; }
static void RadarClickBackdrop(void) { g_nRadarClickFired = 4; }

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
    g_bRadarTrace = 0; g_bRadarDone = 1; g_bRadarAnim = 1;
    g_nRadarMsgTimer = -1; g_nRadarTargetHp = -1; g_nRadarTargetHpMax = -1;
    g_nRadarLevel = -1; g_nRadarRangeLimit = -1; g_nRadarRangeTarget = -1;
    g_nRadarRangeCur = -1; g_nRadarAmmoL = -999; g_nRadarAmmoR = -999;
    g_nRadarCurX = -1; g_nRadarCurY = -1; g_nRadarScan = -1;
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
    g_radarEnv.pbInterceptDone = &g_bRadarDone;
    g_radarEnv.pnScanOffset = &g_nRadarScan;
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
    CheckEqI32(g_bRadarDone, 0, "bRadarInterceptDone cleared (0x5F4E0)");
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
        g_nRadarClickFired = -1;
        if (g_radarUi.apCtl[i] != NULL && g_radarUi.apCtl[i]->pfnClick != NULL) {
            g_radarUi.apCtl[i]->pfnClick();
        }
        CheckEqI32(g_nRadarClickFired, i,
                   "and calling control i runs the handler the env carries in slot i");
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

/* --- RadarScreen_Update @ 0x60570 ---
 *
 * The frame function, so the checks are about the STATE it drives: the damage ladder, the three
 * tail gates, the intercept-done flag, the signal-bar scroll. Two pixel checks pin the blink
 * phase's +50 panel height and the wash it draws over the readout.
 *
 * The RNG is pinned by srand(). With the range bar's pulse gate closed and both mounts disarmed
 * the frame makes exactly ONE Game_RandU16 call (RadarScreen_RollDetection's) before the escape
 * roll, so seed 3 lands Game_RandU16(4) on 0 (the contact escapes) and seed 1 lands it on 2
 * (it does not). Both seeds give Game_RandU16(2) == 0, which makes RollDetection do the speed
 * comparison; the craft runs at 6000/60 = 100, above every class speed, so it never loses the
 * trace on its own. */

#define UPD_SEED_ESCAPE       1
#define UPD_SEED_STAY         2
#define UPD_WASH_COLOR     0x1A4E /* pack(idx 0x1F, mid 0x4A, low 0x75) */
#define UPD_TILE_W             16
#define UPD_TILE_H             16
#define UPD_REGION_LAND        0x85 /* Funding_AddRegionBonus maps 0x85 to funding slot 4 */
#define UPD_RACE_SEA         0xFF /* and 0xFF to the monthly multiplier, slot 5 */
#define UPD_FUND_SLOT_LAND      4
#define UPD_FUND_SLOT_SEA       5
#define UPD_CONTACT_X           3
#define UPD_CONTACT_Y           4

static RadarUpdateEnv   g_updEnv;
static UiAnimBlitEnv    g_updAnim;
static GfxColorEnv      g_updColor;
static const i32 g_nUpdGamma = 3;   /* neutral gamma: divisor 1.0 */
static const i32 g_nGammaOne = 1;   /* divisor 1.1, the gamma-1 value */
static RadarPaletteEnv  g_updPal;
static RadarRangeBarEnv g_updBar;
static RadarFuelEnv     g_updFuel;
static RadarArmamentEnv g_updArm;
static RadarOverlayEnv  g_updOv;
static RadarHitRollEnv  g_updHit;
static RadarDetectEnv   g_updDetect;
static ShipRemovalEnv   g_updShip;
static StrategSpawnCtx  g_updSpawn;
static i32              g_updMonth;  /* unk_159648: the roll's weight-row index, LDRSB at 0x81AB4 */
static u16              g_updAddend; /* 0x1472C8: the roll's early gate, LDRH at 0x81B6C */
static GeoscapeWorld    g_updWorld;
static PadImage         g_updTiles;
static u8               g_abUpdTiles[UPD_TILE_W * UPD_TILE_H];
static i32              g_anUpdFunding[11];
static u32              g_nUpdShortfall;
static char             g_szUpdName[108];
static UiLineList       g_updMsgList;
static u8               g_bUpdSpawnOnce;
static u8               g_bUpdShipCount;
static u16              g_nUpdSoldierCount;
static Soldier         *g_apUpdSoldiers[8];
static Ship            *g_apUpdCraft[2];
static StrategItem     *g_apUpdItems[8];
static BaseRec          g_aUpdBases[2];
static BaseRec         *g_apUpdBases[2] = { &g_aUpdBases[0], &g_aUpdBases[1] };
static u8               g_abUpdHangar[6 * 24 * 8];
static i32              g_nUpdGameSec;
static i32              g_anUpdAssigned[8];
static i32              g_nUpdAssignedCount;
static i32              g_nUpdSeed;
static u8               g_bUpdEngL, g_bUpdEngR, g_bUpdLastHit;
static i32              g_nUpdCoolL, g_nUpdCoolR, g_nUpdShotL, g_nUpdShotR;
static i32              g_nUpdBoundL, g_nUpdBoundR;
static i32              g_nUpdPulseTimer;
static i32              g_nUpdSamples;
static i32              g_nUpdSampleId;
static i32              g_nUpdCtlBlits;
static i8               g_nUpdProgress;

static void UpdSample(i32 nSample, void *pUser)
{
    (void)pUser;
    ++g_nUpdSamples;
    g_nUpdSampleId = nSample;
}

static void UpdCtlBlit(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
    ++g_nUpdCtlBlits;
}

/* One full open session, with the craft on the heap because Ship_RemoveWithCargo frees the one it
 * drops. Item 0 is the intercept target; items 1..3 are the three active alien craft the
 * replacement-spawn gate counts; items 4 and 5 are the two craft's own entries. */
/* A scenario that removed a contact (RadarScreen_Update's destroyed branch, or Ship_RemoveWithCargo
 * dropping a craft's own entry) freed that item through PtrArray_DeleteAt without telling us. Before
 * the fixture reclaims the slots it has to learn which pointers the list still owns, otherwise the
 * second free hands the allocator the same block twice and two slots end up aliasing. */
static void UpdReapItems(void)
{
    for (i32 i = 0; i < 8; ++i) {
        if (g_apUpdItems[i] == NULL) {
            continue;
        }
        bool bLive = false;
        for (i32 k = 0; k < g_radarStrateg.nCount; ++k) {
            if (PtrArray_Get(&g_radarStrateg, k) == (void *)g_apUpdItems[i]) {
                bLive = true;
                break;
            }
        }
        if (!bLive) {
            g_apUpdItems[i] = NULL;
        }
    }
}

static void UpdFixture(i32 nStrategIdx)
{
    i32 i;

    if (g_bRadarActive != 0) {
        RadarScreen_Init(&g_radarEnv); /* the flag is 1, so this is the close path */
    }
    UpdReapItems(); /* must run before Reset drops the list */
    RadarSession_Reset(nStrategIdx);

    for (i = 0; i < 2; ++i) {
        free(g_apUpdCraft[i]); /* a previous scenario may have left one NULL: Ship_RemoveWithCargo
                                * frees the craft it drops, and the scenario NULLs the slot */
        g_apUpdCraft[i] = (Ship *)malloc(sizeof(Ship));
        memset(g_apUpdCraft[i], 0, sizeof(Ship));
    }
    g_apUpdCraft[0]->bId = 7;
    g_apUpdCraft[0]->bType = 7;
    g_apUpdCraft[0]->bCraftType = 0;
    g_apUpdCraft[0]->bHomeBase = 0;
    g_apUpdCraft[0]->bIntercepting = 1;
    g_apUpdCraft[0]->nStrategIdx = nStrategIdx;
    g_apUpdCraft[0]->uFuelCap = 6000; /* 100 on the speed scale, above every class speed */
    g_apUpdCraft[0]->uFuelMax = 1000;
    g_apUpdCraft[0]->nFuel = 900;
    g_apUpdCraft[0]->bWeaponL = WM_LASER;  /* bound 180, unlimited ammo */
    g_apUpdCraft[0]->bWeaponR = WM_CANNON; /* bound 140, the mount that fires */
    g_apUpdCraft[0]->bAmmo25 = 7;
    g_apUpdCraft[1]->bId = 8;
    g_apUpdCraft[1]->bType = 8;
    g_apUpdCraft[1]->bCraftType = 0;
    g_apUpdCraft[1]->bHomeBase = 0;
    g_apUpdCraft[1]->bIntercepting = 1;
    g_apUpdCraft[1]->nStrategIdx = nStrategIdx;
    g_apUpdCraft[1]->uFuelCap = 6000;
    g_apUpdCraft[1]->uFuelMax = 1000;
    g_apUpdCraft[1]->nFuel = 1000;
    g_apUpdCraft[1]->bWeaponL = WM_LASER;
    g_apUpdCraft[1]->bWeaponR = WM_LASER;
    g_apUpdCraft[1]->nSlotIdx = 1;
    g_radarEnv.apCraft = g_apUpdCraft;

    /* StrategList_RemoveAt reaches PtrArray_DeleteAt, which frees the item, so the contacts have
     * to be heap objects here even though the other radar tests keep them static. */
    PtrArray_Clear(&g_radarStrateg);
    for (i = 0; i < 8; ++i) {
        free(g_apUpdItems[i]); /* a scenario that removed this item NULLed the slot */
        g_apUpdItems[i] = (StrategItem *)malloc(sizeof(StrategItem));
        memset(g_apUpdItems[i], 0, sizeof(StrategItem));
        g_apUpdItems[i]->bCraftClass = (u8)i;
        g_apUpdItems[i]->nType = (u8)((i >= 1 && i <= 5) ? 1 : 0);
        g_apUpdItems[i]->bActive = (u8)((i >= 1 && i <= 3) ? 1 : 0);
        g_apUpdItems[i]->nX = UPD_CONTACT_X;
        g_apUpdItems[i]->nY = UPD_CONTACT_Y;
        g_apUpdItems[i]->nHp = 500;
        g_apUpdItems[i]->nTimer = 12345;
        g_apUpdItems[i]->dSpeed = 1.5;
        PtrArray_Add(&g_radarStrateg, g_apUpdItems[i]);
    }
    /* Items 1..3 are the three active alien craft the replacement-spawn gate counts; items 4 and 5
     * are the two craft's own entries, and StrategList_FindByShipId wants nType 1 AND bActive 0. */
    g_apUpdItems[4]->nDisplayId = 7;
    g_apUpdItems[5]->nDisplayId = 8;

    memset(g_abUpdTiles, UPD_REGION_LAND, sizeof(g_abUpdTiles));
    PadImage_ctor(&g_updTiles);
    g_updTiles.uWidth = UPD_TILE_W;
    g_updTiles.uHeight = UPD_TILE_H;
    g_updTiles.pData = g_abUpdTiles;

    memset(g_aUpdBases, 0, sizeof(g_aUpdBases));
    g_aUpdBases[0].nUnk8 = 40;
    g_aUpdBases[0].nUnk12 = 60;
    g_aUpdBases[1].nUnk8 = 70;
    g_aUpdBases[1].nUnk12 = 90;

    memset(&g_updWorld, 0, sizeof(g_updWorld));
    g_updWorld.apCraft = g_apUpdCraft;
    g_updWorld.nShipCount = 2;
    g_updWorld.pTime = &g_radarTime;

    for (i = 0; i < 8; ++i) {
        free(g_apUpdSoldiers[i]);
        g_apUpdSoldiers[i] = NULL;
    }
    g_apUpdSoldiers[0] = (Soldier *)malloc(sizeof(Soldier));
    memset(g_apUpdSoldiers[0], 0, sizeof(Soldier));
    g_apUpdSoldiers[0]->bUnk183 = 1; /* the crew sits on craft index 1, not on craft 0 */
    g_nUpdSoldierCount = 1;

    memset(&g_updShip, 0, sizeof(g_updShip));
    g_updShip.apCraft = g_apUpdCraft;
    g_updShip.pnShipCount = &g_bUpdShipCount;
    g_updShip.apSoldiers = (Soldier **)g_apUpdSoldiers;
    g_updShip.pnSoldierCount = &g_nUpdSoldierCount;
    g_updShip.pStrategList = &g_radarStrateg;
    g_updShip.pWorld = &g_updWorld;
    g_updShip.pnSpawnOnceFlag = &g_bUpdSpawnOnce;
    g_updShip.pSpawnCtx = &g_updSpawn;
    g_updShip.abHangarFlag = g_abUpdHangar;
    g_updShip.nHangarFlagCap = (i32)sizeof(g_abUpdHangar);
    g_updShip.apBases = g_apUpdBases;
    g_updShip.pnGameTotalSec = &g_nUpdGameSec;
    g_updShip.anAssignedShip = g_anUpdAssigned;
    g_updShip.pnAssignedShipCount = &g_nUpdAssignedCount;
    g_bUpdShipCount = 2;
    g_nUpdAssignedCount = 0;
    g_bUpdSpawnOnce = 0;
    g_nUpdGameSec = 0;
    g_nUpdSeed = 99;

    memset(&g_updSpawn, 0, sizeof(g_updSpawn));
    g_updMonth = 0;
    g_updAddend = 0;
    g_updSpawn.pnDifficulty = &g_updMonth;
    g_updSpawn.pnEarlyGate = &g_updAddend;
    g_updSpawn.pnSeed = &g_nUpdSeed;

    /* Open the screen for real: every pad RadarScreen_Init loads comes from the shipped archives. */
    RadarScreen_Init(&g_radarEnv);

    memset(&g_updEnv, 0, sizeof(g_updEnv));
    g_updEnv.pSession = &g_radarEnv;
    g_updEnv.pAnim = &g_updAnim;
    g_updEnv.pColor = &g_updColor;
    g_updEnv.pPalette = &g_updPal;
    g_updEnv.pRangeBar = &g_updBar;
    g_updEnv.pFuel = &g_updFuel;
    g_updEnv.pArmament = &g_updArm;
    g_updEnv.pOverlay = &g_updOv;
    g_updEnv.pDetect = &g_updDetect;
    g_updEnv.pShipEnv = &g_updShip;
    g_updEnv.pTilemap = &g_updTiles;
    g_updEnv.anFundingBlock = g_anUpdFunding;
    g_updEnv.pdwShortfall = &g_nUpdShortfall;
    g_updEnv.pszNameBuf = g_szUpdName;
    g_updEnv.cchNameBuf = sizeof(g_szUpdName);
    g_updEnv.pMsgList = &g_updMsgList;
    g_updEnv.pfnPlaySample = UpdSample;
    g_updEnv.pfnCtlBlit = UpdCtlBlit;
    g_updEnv.pfnCtlBlitEx = UpdCtlBlit;

    memset(g_anUpdFunding, 0, sizeof(g_anUpdFunding));
    memset(&g_updMsgList, 0, sizeof(g_updMsgList));
    g_nUpdShortfall = 777;
    memset(g_szUpdName, 0, sizeof(g_szUpdName));
    g_nUpdSamples = 0;
    g_nUpdSampleId = -1;
    g_nUpdCtlBlits = 0;

    g_updAnim.pbStarted = &g_bRadarAnim;
    g_updAnim.pnProgress = &g_nUpdProgress;
    g_updAnim.nFrameStyle = 1;

    g_updColor.pnGamma = &g_nUpdGamma; /* neutral gamma: the LUT divisor is 1.0 */
    g_updColor.nFadeCount = 0;

    g_updPal.pnRangeCur = &g_nRadarRangeCur;
    g_updPal.pnRangeTarget = &g_nRadarRangeTarget;
    g_updPal.pbFlip = &g_bRadarFlip;

    g_updBar.pInterceptItem = g_apUpdItems[nStrategIdx];
    g_updBar.pCraft = g_apUpdCraft[0];
    g_updBar.pnRangeCur = &g_nRadarRangeCur;
    g_updBar.nRangeLimit = g_nRadarRangeLimit;
    g_updBar.nCraftLevel = g_nRadarLevel;
    g_updBar.pnPulseTimer = &g_nUpdPulseTimer;

    for (i = 0; i < PUFO_RADAR_DIGIT_PADS; ++i) {
        g_updFuel.apDigitPads[i] = g_radarUi.apDigitPad[i];
    }
    g_updFuel.pCraft = g_apUpdCraft[0];
    g_updFuel.pbOutOfFuel = &g_bRadarOutFuel;

    g_updArm.pnGlyphPalBase = &g_nGlyphPalSlotBase;
    g_updArm.pnShipIdx = &g_nRadarShipIdx;
    g_updArm.apCraft = g_apUpdCraft;
    g_updArm.nCraftCount = 2;
    g_updArm.pnRangeCur = &g_nRadarRangeCur;
    g_updArm.pnCraftLevel = &g_nRadarLevel;
    g_updArm.pnBoundL = &g_nUpdBoundL;
    g_updArm.pnBoundR = &g_nUpdBoundR;
    g_updArm.pbEngageL = &g_bUpdEngL;
    g_updArm.pbEngageR = &g_bUpdEngR;
    g_updArm.pnCooldownL = &g_nUpdCoolL;
    g_updArm.pnCooldownR = &g_nUpdCoolR;
    g_updArm.pnShotYL = &g_nUpdShotL;
    g_updArm.pnShotYR = &g_nUpdShotR;
    g_updArm.pnAmmoL = &g_nRadarAmmoL;
    g_updArm.pnAmmoR = &g_nRadarAmmoR;

    g_updHit.pStrategList = &g_radarStrateg;
    g_updHit.pnStrategIdx = &g_nRadarStrategIdx;
    g_updHit.pnTargetHp = &g_nRadarTargetHp;

    g_updOv.pnShipIdx = &g_nRadarShipIdx;
    g_updOv.apCraft = g_apUpdCraft;
    g_updOv.nCraftCount = 2;
    g_updOv.pnRangeCur = &g_nRadarRangeCur;
    g_updOv.pnCraftLevel = &g_nRadarLevel;
    g_updOv.pbFlip = &g_bRadarFlip;
    g_updOv.pbEngageL = &g_bUpdEngL;
    g_updOv.pbEngageR = &g_bUpdEngR;
    g_updOv.pnCooldownL = &g_nUpdCoolL;
    g_updOv.pnCooldownR = &g_nUpdCoolR;
    g_updOv.pnShotYL = &g_nUpdShotL;
    g_updOv.pnShotYR = &g_nUpdShotR;
    g_updOv.pnAmmoL = &g_nRadarAmmoL;
    g_updOv.pnAmmoR = &g_nRadarAmmoR;
    g_updOv.pbLastShotHit = &g_bUpdLastHit;
    g_updOv.pHitEnv = &g_updHit;

    g_updDetect.pStrategList = &g_radarStrateg;
    g_updDetect.pnStrategIdx = &g_nRadarStrategIdx;
    g_updDetect.apCraft = g_apUpdCraft;
    g_updDetect.nCraftCount = 2;
    g_updDetect.pnShipIdx = &g_nRadarShipIdx;
    g_updDetect.pbTraceLost = &g_bRadarTrace;
    g_updDetect.pbFlip = &g_bRadarFlip;
    g_updDetect.pnRangeTarget = &g_nRadarRangeTarget;
    g_updDetect.pnMsgTimer = &g_nRadarMsgTimer;
    g_updDetect.pTextRecs = g_pRadarTexts;
    g_updDetect.pszMsg = g_szRadarMsg;
    g_updDetect.cchMsg = sizeof(g_szRadarMsg);

    /* A playable baseline. The range sits at 150, which puts the radar's reach at
    * 150 + 560*(8-13)/114 = 126 -- inside both mounts' bounds, so RadarScreen_DrawCraftArmament
    * leaves the engage bytes set and the cooldowns and projectile offsets alone. The shot gate is
    * then 8 + 114*150/560 - 33 = 5, and the right mount's offset starts at 5, so its cannon shot
    * LANDS on the first frame: that is the only way bRadarLastShotHit can be set, because
    * RadarScreen_DrawOverlay clears it at 0x5DCC0 before either mount runs. The left mount's
    * cooldown is parked at 254 so exactly one shot runs and the frame burns exactly one roll.
    * The range bar's third gate is closed so it burns none of its own. */
    g_bRadarAnim = 1;
    g_nUpdProgress = 100;
    g_bRadarOutFuel = 0;
    g_bRadarBlink = 0;
    g_bRadarFlip = 0;
    g_bRadarTrace = 0;
    g_bRadarDone = 0;
    g_nRadarMsgTimer = 255;
    g_nRadarTargetHp = 130;
    g_nRadarTargetHpMax = 200;
    g_nRadarRangeCur = 150;
    g_nRadarRangeTarget = 150;
    g_nRadarCurX = 117;
    g_nRadarCurY = 86;
    g_nRadarScan = 0;
    g_nUpdPulseTimer = 0;
    g_bUpdEngL = 1;
    g_bUpdEngR = 1;
    g_bUpdLastHit = 0;
    g_nUpdCoolL = 254;
    g_nUpdCoolR = 255;
    g_nUpdShotL = 0;
    g_nUpdShotR = 5;
    g_nUpdBoundL = 180;
    g_nUpdBoundR = 140;
    g_nRadarAmmoL = PUFO_RADAR_AMMO_UNLIMITED; /* the laser mount needs no ammo */
    g_nRadarAmmoR = 5;
    g_nRadarShipIdx = 0;
}

/* The original reaches the craft through nInterceptShipIdx everywhere, so the envs that hold a
 * craft pointer have to follow it. */
static void UpdSetShip(i32 nShipIdx)
{
    g_nRadarShipIdx = nShipIdx;
    g_updFuel.pCraft = g_apUpdCraft[nShipIdx];
    g_updBar.pCraft = g_apUpdCraft[nShipIdx];
}

static const char *UpdText(i32 nRec)
{
    return (const char *)(g_pRadarTexts + (size_t)nRec * PUFO_TEXT_REC_STRIDE);
}

/* Park both cooldowns below 255 so no mount fires: the frame then burns no RNG at all, which is
 * what the tests that are not about the damage ladder need. */
static void UpdNoFire(void)
{
    g_nUpdCoolL = 254;
    g_nUpdCoolR = 254;
    g_nUpdShotL = 0;
    g_nUpdShotR = 0;
    g_bUpdLastHit = 0;
}

static void TestRadarUpdate(void)
{
    char szRef[108];

    printf("\nRadarScreen_Update @ 0x60570\n");

    if (!g_bArmReady || g_bdSurf.pPixels == NULL) {
        Check(false, "the archives and the render surface are live from the armament test");
        return;
    }
    if (!RadarLoadTextBlob()) {
        Check(false, "the lang.dat TEXTS blob loaded");
        return;
    }

    /* --- the closed screen reaches nothing at all --- */
    UpdFixture(0);
    g_bRadarActive = 0;
    g_nRadarScan = 20;
    g_nRadarMsgTimer = 255;
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_nRadarScan, 20, "a closed radar screen does not even advance the scroll");
    CheckEqI32(g_nRadarMsgTimer, 255, "and none of the three tail gates runs");
    CheckEqI32(g_bRadarActive, 0, "the screen stays closed");

    /* --- the animation gate skips the draw but not the tail --- */
    UpdFixture(0);
    g_nUpdProgress = 1; /* mode 4 at 50*1 is nowhere near 188x140 */
    g_nRadarScan = 20;
    g_bRadarDone = 1;
    g_nRadarMsgTimer = 31;
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_nRadarScan, 20, "an unfinished panel animation skips the whole draw block");
    CheckEqI32(g_bRadarActive, 0, "but 0x605F0 still lands on the tail gates, which close the screen");
    CheckEqI32(g_radarTime.bPaused, 0, "and closing unpauses the clock");
    CheckEqI32(g_nRadarFrameStyle, 0, "and picks the ordinary panel frame");

    /* --- the scroll: +10, and zero when the NEW value is above 39 --- */
    UpdFixture(0);
    UpdNoFire();
    g_nRadarScan = 0;
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_nRadarScan, 10, "the scroll advances by 10 (0x61084)");
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_nRadarScan, 20, "again");
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_nRadarScan, 30, "again");
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_nRadarScan, 0, "and 40 is above 39, so it wraps to 0 (0x61088..0x610A0)");
    CheckEqI32(g_bRadarActive, 1, "and four frames of it still leave the screen open");

    /* --- the blink phase: +50 panel height, and the wash over the readout --- */
    UpdFixture(0);
    UpdNoFire();
    g_bRadarBlink = 0;
    memset(g_bdSurf.pPixels, 0, (size_t)g_bdSurf.nWidth * (size_t)g_bdSurf.nHeight * 2u);
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(BdPix(26, 230), 0,
               "with the phase clear the panel is 140 tall, so (26, 230) stays bare");
    UpdFixture(0);
    UpdNoFire();
    g_bRadarBlink = 1;
    memset(g_bdSurf.pPixels, 0, (size_t)g_bdSurf.nWidth * (size_t)g_bdSurf.nHeight * 2u);
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(BdPix(26, 230), UPD_WASH_COLOR,
               "with the phase set the panel is 190 tall and the 0x1F/0x4A/0x75 wash covers it");
    CheckEqI32(g_nUpdCtlBlits, 5, "the five corner controls are blitted (0x6084C..0x60864)");

    /* Every ladder case starts from a shot that LANDS: the baseline parks the right mount's
     * projectile exactly at the gate, so RadarScreen_RollWeaponHit runs, writes the HP and sets
     * bRadarLastShotHit -- which RadarScreen_DrawOverlay had just cleared. Seed 1 gives the cannon
     * a roll of 0 (hit, -50 hp) and the escape chain a roll of 0; seed 2 gives the same hit but an
     * escape roll of 2. */

    /* --- the target is gone: message, spawn gate, removal, done flag --- */
    UpdFixture(0);
    g_nRadarTargetHp = 50; /* the cannon's -50 takes it to 0 */
    g_apUpdCraft[1]->nStrategIdx = 2; /* above the hole being made */
    srand(UPD_SEED_ESCAPE);
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_nRadarTargetHp, 0, "the landing cannon shot takes the target to 0");
    Check(strcmp(g_szRadarMsg, UpdText(PUFO_RADAR_TEXT_TARGET_DEAD)) == 0,
          "and hp <= 0 posts TEXTS[1600] (0x6094C)");
    CheckEqI32(g_nRadarMsgTimer, 0, "and restarts the message timer (0x60940)");
    CheckEqI32(g_bRadarDone, 1, "and marks the intercept done (0x609A0)");
    CheckEqI32(g_radarStrateg.nCount, 7, "the contact is removed from the strateg list (0x60988)");
    g_apUpdItems[0] = NULL; /* PtrArray_DeleteAt freed it */
    CheckEqI32(g_bUpdSpawnOnce, 0,
               "0x162028 is set at 0x6097C and then consumed by the removal it just triggered");
    CheckEqI32(g_apUpdCraft[1]->nStrategIdx, 1,
               "and the craft chasing a LATER item is re-pointed down by one (0x82AFC)");
    CheckEqI32(g_bRadarActive, 1, "the screen stays open: the timer is 0, not above 30");

    /* --- 40..69 percent, land tile, escape roll 0: the contact slips away --- */
    UpdFixture(0);
    g_nRadarTargetHp = 130; /* the shot leaves 80 of 200, i.e. exactly 40 percent */
    srand(UPD_SEED_ESCAPE);
    RadarScreen_Update(&g_updEnv);
    Check(strcmp(g_szRadarMsg, UpdText(PUFO_RADAR_TEXT_TARGET_LANDED)) == 0,
          "the escape posts TEXTS[1601] (0x60ABC)");
    CheckEqI32(g_bRadarDone, 1, "and ends the intercept (0x60B4C)");
    CheckEqI32(g_apUpdItems[0]->nType, 4, "the contact becomes a landed object (0x60B30)");
    CheckEqI32(g_apUpdItems[0]->nTimer, PUFO_RADAR_TIMER_LANDED,
               "with a 7200 proximity timer (0x60B44)");
    CheckEqI32(g_anUpdFunding[UPD_FUND_SLOT_LAND], 2,
               "and its tile's race gets the +2 funding bonus (0x60B1C)");
    CheckEqI32(g_radarStrateg.nCount, 8, "but the item is NOT removed -- it came down on land");
    CheckEqI32(g_bRadarActive, 1, "and 40 percent is not below 40, so the crash branch is skipped");

    /* --- the same band, but the escape roll is not 0 --- */
    UpdFixture(0);
    g_nRadarTargetHp = 130;
    srand(UPD_SEED_STAY);
    RadarScreen_Update(&g_updEnv);
    Check(g_szRadarMsg[0] == '\0', "a non-zero Game_RandU16(4) leaves the contact where it is");
    CheckEqI32(g_bRadarDone, 0, "so the intercept is still live");
    CheckEqI32(g_apUpdItems[0]->nType, 0, "the item keeps its type");
    CheckEqI32(g_anUpdFunding[UPD_FUND_SLOT_LAND], 0, "and no funding bonus is paid");

    /* --- the ocean tile blocks the escape before the roll is even made --- */
    UpdFixture(0);
    g_nRadarTargetHp = 130;
    g_abUpdTiles[UPD_TILE_W * UPD_CONTACT_Y + UPD_CONTACT_X] = UPD_RACE_SEA;
    srand(UPD_SEED_ESCAPE);
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_bRadarDone, 0, "a 255 tile short-circuits the escape chain at 0x60A94");
    CheckEqI32(g_anUpdFunding[UPD_FUND_SLOT_SEA], 0,
               "so the no-race multiplier is not touched either");

    /* --- a lost trace blocks it too --- */
    UpdFixture(0);
    g_nRadarTargetHp = 130;
    g_bRadarTrace = 1;
    srand(UPD_SEED_ESCAPE);
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_bRadarDone, 0, "bRadarTraceLost is the first test in the chain (0x60A4C)");

    /* --- below 40 percent on land --- */
    UpdFixture(0);
    g_nRadarTargetHp = 100; /* the shot leaves 50 of 200, i.e. 25 percent */
    srand(UPD_SEED_ESCAPE);
    RadarScreen_Update(&g_updEnv);
    Check(strcmp(g_szRadarMsg, UpdText(PUFO_RADAR_TEXT_TARGET_CRASHED)) == 0,
          "under 40 percent posts TEXTS[1602] (0x60BAC)");
    CheckEqI32(g_bRadarDone, 1, "and ends the intercept BEFORE the tile test (0x60C10)");
    CheckEqI32(g_apUpdItems[0]->nType, 4, "with the object conversion (0x60C64)");
    CheckEqI32(g_apUpdItems[0]->nTimer, PUFO_RADAR_TIMER_LANDED, "and the same 7200 timer (0x60C74)");
    CheckEqI32(g_anUpdFunding[UPD_FUND_SLOT_LAND], 2, "and the race bonus (0x60C50)");
    CheckEqI32(g_radarStrateg.nCount, 8, "the item stays in the list");

    /* --- below 40 percent in the sea --- */
    UpdFixture(0);
    g_nRadarTargetHp = 100;
    g_abUpdTiles[UPD_TILE_W * UPD_CONTACT_Y + UPD_CONTACT_X] = UPD_RACE_SEA;
    srand(UPD_SEED_ESCAPE);
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_radarStrateg.nCount, 7, "a 255 tile removes the contact instead (0x60CB8)");
    CheckEqI32(g_anUpdFunding[UPD_FUND_SLOT_SEA], 2,
               "and 0xFF feeds the monthly multiplier, not a race score");
    /* PtrArray_DeleteAt frees the item, so the removed contact cannot be read any more; the shift
     * is checked through the list instead. */
    Check(PtrArray_Get(&g_radarStrateg, 0) == (void *)g_apUpdItems[1],
          "the removal shifts the later contacts down (0x60CB8)");
    g_apUpdItems[0] = NULL; /* PtrArray_DeleteAt already freed it */

    /* --- 70 percent or more leaves everything alone --- */
    UpdFixture(0);
    g_nRadarTargetHp = 200; /* the shot leaves 150 of 200, i.e. 75 percent */
    srand(UPD_SEED_ESCAPE);
    RadarScreen_Update(&g_updEnv);
    Check(g_szRadarMsg[0] == '\0', "75 percent is above the ladder's top gate (0x60A2C)");
    CheckEqI32(g_bRadarDone, 0, "the intercept stays live");
    CheckEqI32(g_radarStrateg.nCount, 8, "and nothing is removed");

    /* --- no shot means no ladder at all --- */
    UpdFixture(0);
    UpdNoFire();
    g_nRadarTargetHp = 0;
    RadarScreen_Update(&g_updEnv);
    Check(g_szRadarMsg[0] == '\0', "no landing shot leaves bRadarLastShotHit 0 and skips the block (0x608C8)");
    CheckEqI32(g_bRadarDone, 0, "so a dead target is not even noticed this frame");
    CheckEqI32(g_radarStrateg.nCount, 8, "and the contact stays in the list");

    /* --- once it is done, DrawOverlay is skipped and the flash gate is closed --- */
    UpdFixture(0);
    UpdNoFire();
    g_bRadarDone = 1;
    g_bUpdLastHit = 1;
    g_nRadarTargetHp = 0;
    RadarScreen_Update(&g_updEnv);
    Check(g_szRadarMsg[0] == '\0',
          "a done intercept posts nothing: the flash gate also requires !bRadarInterceptDone (0x608B8)");
    CheckEqI32(g_bRadarDone, 1, "and stays done");
    CheckEqI32(g_radarStrateg.nCount, 8, "with the contact still in the list");

    /* --- tail 1: done, and the message has been up long enough --- */
    UpdFixture(0);
    UpdNoFire();
    g_bRadarDone = 1;
    g_nRadarMsgTimer = 31;
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_bRadarActive, 0, "the screen closes (0x6125C)");
    CheckEqI32(g_apUpdCraft[0]->bIntercepting, 0,
               "craft 0 has no crew, so it is sent home (0x612CC)");
    CheckEqI32(g_apUpdItems[4]->nTimer, 0, "and its own entry's proximity timer restarts at 0");
    CheckEqI32(g_apUpdItems[4]->nDestX, 40, "with the travel leg aimed at its home base");
    CheckEqI32(g_apUpdItems[4]->nDestY, 60, "on both axes");
    CheckEqI32(g_apUpdCraft[1]->bIntercepting, 1,
               "while craft 1 has a soldier aboard and is left intercepting");
    Check(g_radarUi.pFramePad == NULL, "the close really ran: the frame pad is freed");

    /* --- tail 2: out of fuel deletes the craft --- */
    UpdFixture(0);
    UpdNoFire();
    UpdSetShip(1);
    g_apUpdCraft[1]->nFuel = 0; /* 100 percent consumed, so DrawFuelGauge sets the flag itself */
    g_nRadarMsgTimer = 31;
    g_bRadarDone = 0;
    /* The reference name is built BEFORE the update: 0x61360 formats it out of the live roster and
     * 0x613A0's Ship_RemoveWithCargo then compacts that roster, so index 1 is not the stranded craft
     * any more once RadarScreen_Update returns. */
    (void)Ship_FormatDisplayName(g_apUpdCraft, 1, g_pRadarTexts, szRef, sizeof(szRef));
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_bRadarOutFuel, 1, "the fuel gauge set bRadarCraftOutOfFuel (0x5D9D8)");
    CheckEqI32(g_bRadarActive, 0, "and the tail closed the screen (0x6132C)");
    CheckEqI32((i32)g_nUpdShortfall, 1, "the low-supply dword is set as a dword (0x61340)");
    CheckEqI32(g_updMsgList.nLineCount, 1, "message 44 is posted (0x61354)");
    CheckEqI32(g_updMsgList.abLineFlags[0], PUFO_RADAR_MSG_SHIP_LOST, "with the right message id");
    CheckEqI32((i32)g_updMsgList.anColor[0][0], 1, "and the ship index in colour slot 0");
    CheckEqI32(g_nUpdSampleId, 7, "UiLineList_AppendWithSound plays sample 7");
    Check(strcmp(g_szUpdName, szRef) == 0,
          "the craft's display name lands in the 0x186488 caption scratch");
    CheckEqI32((i32)g_bUpdShipCount, 1, "Ship_RemoveWithCargo drops the craft (0x613A0)");
    CheckEqI32(g_apUpdCraft[0]->bId, 7, "the survivor keeps its id");
    CheckEqI32(g_apUpdCraft[0]->nSlotIdx, 0, "and its hangar slot is renumbered (0x36B80)");
    CheckEqI32((i32)g_nUpdSoldierCount, 0, "the soldier assigned to the removed craft is deleted");
    g_apUpdCraft[1] = NULL; /* Ship_RemoveWithCargo already freed the craft (0x36B40) and the
                             * soldier assigned to it (0x369BC). */
    g_apUpdSoldiers[0] = NULL;
    g_apUpdItems[5] = NULL; /* Ship_RemoveWithCargo also dropped this craft's strateg entry (0x36934) */

    /* --- tail 3: full zoom sends the intercept craft home --- */
    UpdFixture(0);
    UpdNoFire();
    g_bRadarFlip = 1;
    g_nRadarRangeCur = PUFO_RADAR_FULL_RANGE;
    g_nRadarRangeTarget = PUFO_RADAR_FULL_RANGE;
    g_nRadarMsgTimer = 0;
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_bRadarActive, 0, "full zoom with the flip set closes the screen (0x613E0)");
    CheckEqI32(g_apUpdCraft[0]->bIntercepting, 0, "and retargets the intercept craft (0x613EC)");
    CheckEqI32(g_apUpdCraft[1]->bIntercepting, 1,
               "ORIGINAL BUG 0x61434: the Ship_ClearInterceptByStrategIdx there is unreachable, so "
               "the second craft still chases a contact the screen has given up on");

    /* --- the same, with the trace lost: the craft's own entry gets a 10-second timer --- */
    UpdFixture(0);
    UpdNoFire();
    g_bRadarFlip = 1;
    g_bRadarTrace = 1;
    g_nRadarRangeCur = PUFO_RADAR_FULL_RANGE;
    g_nRadarRangeTarget = PUFO_RADAR_FULL_RANGE;
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_apUpdItems[4]->nTimer, PUFO_RADAR_TIMER_TRACE_LOST,
               "bRadarTraceLost parks the craft's entry timer at 10 (0x61420)");

    /* --- and the two gates that keep it from running --- */
    UpdFixture(0);
    UpdNoFire();
    g_bRadarFlip = 0;
    g_nRadarRangeCur = PUFO_RADAR_FULL_RANGE;
    g_nRadarRangeTarget = PUFO_RADAR_FULL_RANGE;
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_bRadarActive, 1, "without the flip byte the full-zoom exit does not run (0x613BC)");
    UpdFixture(0);
    UpdNoFire();
    g_bRadarFlip = 1;
    g_nRadarRangeCur = 559;
    g_nRadarRangeTarget = 559; /* parked here, so CyclePalette never pushes it to 560 */
    RadarScreen_Update(&g_updEnv);
    CheckEqI32(g_bRadarActive, 1, "and neither does it below full range (0x613D0)");

    /* --- the guards --- */
    UpdFixture(0);
    UpdNoFire();
    g_nRadarScan = 20;
    RadarScreen_Update(NULL);
    CheckEqI32(g_nRadarScan, 20, "a null environment is refused");
    CheckEqI32(g_bRadarActive, 1, "and the screen is left alone");
    if (g_bRadarActive != 0) {
        RadarScreen_Init(&g_radarEnv);
    }
    for (i32 i = 0; i < 2; ++i) {
        free(g_apUpdCraft[i]);
        g_apUpdCraft[i] = NULL;
    }
    for (i32 i = 0; i < 8; ++i) {
        free(g_apUpdSoldiers[i]);
        g_apUpdSoldiers[i] = NULL;
    }
}

/* ---- Smap_DrawStrategIcons @ 0x822c4 and Geoscape_DrawClockPads @ 0x5c5c8 ----------
 *
 * Both run on the same real 240x320 surface. The strateg icons are real mapbits sprites (mapbits.pal
 * is palette slot 32, which is the value the original forces into Map_nGlyphPalBase), so a glyph is
 * identified by hashing the 40x40 box it paints. The clock digit pads are synthetic 2 x 1 glyphs whose
 * first column carries the pad's own palette index and whose second column carries index 0 -- the
 * colour key PadImage_BlitEx is called with -- so a pad is identified by the colour it leaves and the
 * key column proves the keyed blit really skips it.
 */
#define GD_BOX 40        /* the box hashed around a glyph's origin */
#define CK_PAD_SLOT 25   /* an arbitrary slot; only these ten indices are set */

static bool g_bGdReady;

static bool GdEnsureFont(void)
{
    static const char *const apszArchive[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    if (g_bdSurf.pPixels == NULL) {
        SoftSurf_ctor(&g_bdSurf);
        SoftSurf_AllocPixels(&g_bdSurf, BD_SURF_W, BD_SURF_H, PIXMODE_16BPP);
        g_pMainSurface = &g_bdSurf;
        Render_SetTarget(BD_SURF_W, BD_SURF_H, 2, 2, BD_SURF_W * 2, g_bdSurf.pPixels);
    }
    if (!g_bGdReady) {
        if (Asset_OpenArchives(apszArchive) == 0) {
            return false;
        }
        FontMetrics_Load();
        PufoFont_Init(&g_fontSys);
        g_fontSys.nGlyphBase = g_fontSys.nTotalSprFrames;
        (void)Font_LoadSpr(&g_fontSys, "font");
        g_fontSys.nMapBitsBase = g_fontSys.nTotalSprFrames;
        (void)Font_LoadSpr(&g_fontSys, "mapbits");
        Font_BuildIndex(&g_fontSys);
        g_bGdReady = g_fontMetrics.bLoaded && g_fontSys.apSprIndex != NULL
                  && g_fontSys.nMapBitsBase > 16;
    }
    return g_bGdReady;
}

static void GdClear(void)
{
    memset(g_bdSurf.pPixels, 0, (size_t)g_bdSurf.nWidth * (size_t)g_bdSurf.nHeight * 2u);
}

static u32 GdHash(i32 x0, i32 y0)
{
    u32 uHash = 2166136261u;

    for (i32 y = 0; y < GD_BOX; ++y) {
        for (i32 x = 0; x < GD_BOX; ++x) {
            uHash = (uHash ^ (u32)BdPix(x0 + x, y0 + y)) * 16777619u;
        }
    }
    return uHash;
}

static i32 GdCount(i32 x0, i32 y0, i32 nW, i32 nH)
{
    i32 n = 0;

    for (i32 y = y0; y < y0 + nH; ++y) {
        for (i32 x = x0; x < x0 + nW; ++x) {
            if (BdPix(x, y) != 0) {
                ++n;
            }
        }
    }
    return n;
}

/* ---- the strateg icons ------------------------------------------------------------------ */

static SmapView        g_siView;
static StrategItem     g_siItem;
static PtrArray        g_siList;
static SmapIconDrawEnv g_siEnv;

static void SiFixture(i32 nMapW, i32 nMapH)
{
    GdClear();
    /* mapbits.pal is slot 32, but the palette table is only filled by Geoscape_InitUI's .pal
     * table, which this test does not run. Filled here with one distinct colour per index so a
     * glyph paints and two different glyph indices cannot hash alike. */
    memset(g_aPalette, 0, sizeof(g_aPalette));
    for (i32 k = 1; k < 256; ++k) {
        g_aPalette[PUFO_GLYPH_PAL_STRATEG_ICONS][k] = (u32)(0x100 + k);
    }
    memset(&g_siView, 0, sizeof(g_siView));
    g_siView.nMapW = nMapW;
    g_siView.nMapH = nMapH;
    memset(&g_siItem, 0, sizeof(g_siItem));
    memset(&g_siList, 0, sizeof(g_siList));
    PtrArray_ctor(&g_siList);
    (void)PtrArray_Add(&g_siList, &g_siItem);
    memset(&g_siEnv, 0, sizeof(g_siEnv));
    g_siEnv.pStrategList = &g_siList;
    g_siEnv.nMapBitsBase = g_fontSys.nMapBitsBase;
}

/* The reference: one mapbits glyph drawn straight onto a cleared surface, in the palette the
 * original forces. This CLEARS the surface, so every reference is taken before the call under test. */
static u32 SiRef(i32 nX, i32 nY, u16 nIdx)
{
    GdClear();
    g_nGlyphPalSlotBase = PUFO_GLYPH_PAL_STRATEG_ICONS;
    Font_DrawGlyph(nX, nY, nIdx, 0);
    return GdHash(nX, nY);
}

static void SiRun(void)
{
    GdClear();
    Smap_DrawStrategIcons(&g_siView, &g_siEnv);
}


static void TestDrawStrategIcons(void)
{
    u32 uRef;

    printf("\nSmap_DrawStrategIcons @ 0x822c4\n");
    if (!GdEnsureFont()) {
        Check(false, "font.dat and mapbits.dat loaded, so the icons are real sprites");
        return;
    }

    /* Every glyph index the function can pick must paint something, or a hash match against a
     * reference that drew nothing would pass without testing anything. */
    static const i32 anK[8] = { 0, 2, 4, 7, 8, 10, 12, 14 };
    char szMsg[96];

    memset(g_aPalette, 0, sizeof(g_aPalette));
    for (i32 k = 1; k < 256; ++k) {
        g_aPalette[PUFO_GLYPH_PAL_STRATEG_ICONS][k] = (u32)(0x100 + k);
    }
    /* Measured against the shipped main.dat: the mapbits frame the +6 glyph lands on is empty,
     * which is why the active-base case below runs with animation phase 1 (frame +7) instead. */
    GdClear();
    g_nGlyphPalSlotBase = PUFO_GLYPH_PAL_STRATEG_ICONS;
    Font_DrawGlyph(20, 20, (u16)(g_fontSys.nMapBitsBase + 6), 0);
    CheckEqI32(GdCount(20, 20, GD_BOX, GD_BOX), 0,
               "mapbits frame +6 is blank in the shipped main.dat (measured, not assumed)");
    for (i32 k = 0; k < 8; ++k) {
        GdClear();
        g_nGlyphPalSlotBase = PUFO_GLYPH_PAL_STRATEG_ICONS;
        Font_DrawGlyph(20, 20, (u16)(g_fontSys.nMapBitsBase + anK[k]), 0);
        snprintf(szMsg, sizeof(szMsg), "mapbits +%d paints pixels, so its hash means something",
                 anK[k]);
        Check(GdCount(20, 20, GD_BOX, GD_BOX) > 0, szMsg);
    }

    /* 400x400 map, no scroll: half extents 400/2 - 120 = 80 and - 160 = 40, so the map
     * coordinate (180, 140) lands at screen (100, 100). */
    g_siEnv.nAnimPhase = 0;

    SiFixture(400, 400);
    g_siItem.nType = 3; g_siItem.nX = 180; g_siItem.nY = 140;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 8));
    SiRun();
    CheckEqI32(g_nGlyphPalSlotBase, PUFO_GLYPH_PAL_STRATEG_ICONS,
               "the pass forces palette slot 32, which is mapbits.pal");
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef, "a type-3 entry draws mapbits + anim + 8");
    Check(GdCount(100, 100, GD_BOX, GD_BOX) > 0, "and the glyph really paints pixels");

    /* Odd extents pin the halving itself. 0x822EC..0x8231C is MOVS / ADDMI #1 / ASR #1 (the middle
     * word is 0x42833001), and ADDMI fires only when the tested value is NEGATIVE, so 401 halves to
     * 200 -- exactly the half a 400 map gives. A ceiling reading would land the glyph at (101, 101)
     * and this hash would not match. */
    SiFixture(401, 401);
    g_siItem.nType = 3; g_siItem.nX = 180; g_siItem.nY = 140;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 8));
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef,
               "401 halves to 200, not ceil's 201, so an odd map centres where 400 does");

    SiFixture(400, 400);
    g_siItem.nType = 3; g_siItem.nX = 180; g_siItem.nY = 140;
    g_siEnv.nAnimPhase = 2;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 2 + 8));
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef,
               "the Map + 0x60 animation phase is added for a terror site");

    SiFixture(400, 400);
    g_siItem.nType = 4; g_siItem.nX = 180; g_siItem.nY = 140;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 10));
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef, "a type-4 UFO draws mapbits + anim + 10");

    SiFixture(400, 400);
    g_siItem.nType = 5; g_siItem.nX = 180; g_siItem.nY = 140;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 14));
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef, "a type-5 entry draws mapbits + anim + 14");

    /* Type 0: inactive always draws, active needs bVisible. */
    SiFixture(400, 400);
    g_siItem.nType = 0; g_siItem.nX = 180; g_siItem.nY = 140; g_siItem.bActive = 0; g_siItem.bVisible = 0;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 0));
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef,
               "an inactive type-0 entry draws mapbits + anim even with bVisible 0");

    SiFixture(400, 400);
    g_siEnv.nAnimPhase = 1; /* frame +7, because +6 is the blank shipped frame */
    g_siItem.nType = 0; g_siItem.nX = 180; g_siItem.nY = 140; g_siItem.bActive = 1; g_siItem.bVisible = 1;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 1 + 6));
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef, "an active visible base draws mapbits + anim + 6");
    g_siEnv.nAnimPhase = 0;

    SiFixture(400, 400);
    g_siItem.nType = 0; g_siItem.nX = 180; g_siItem.nY = 140; g_siItem.bActive = 1; g_siItem.bVisible = 0;
    SiRun();
    CheckEqI32(GdCount(100, 100, GD_BOX, GD_BOX), 0, "an active base with bVisible 0 is not drawn");

    /* Type 1: the two branches that do NOT take the animation phase. */
    SiFixture(400, 400);
    g_siItem.nType = 1; g_siItem.nX = 180; g_siItem.nY = 140; g_siItem.bActive = 0; g_siItem.bVisible = 1;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 2));
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef,
               "an inactive visible craft draws mapbits + 2 (0x825DC adds no anim phase)");

    g_siEnv.nAnimPhase = 3;
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef,
               "and the same glyph with anim 3, so the anim phase really is not added here");
    g_siEnv.nAnimPhase = 0;

    SiFixture(400, 400);
    g_siItem.nType = 1; g_siItem.nX = 180; g_siItem.nY = 140; g_siItem.bActive = 1; g_siItem.bVisible = 1;
    g_siItem.nTimer = 0;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 4));
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef,
               "an active craft with nTimer 0 draws mapbits + 4 (0x82620, again no anim phase)");

    SiFixture(400, 400);
    g_siItem.nType = 1; g_siItem.nX = 180; g_siItem.nY = 140; g_siItem.bActive = 1; g_siItem.bVisible = 1;
    g_siItem.nTimer = 7;
    uRef = SiRef(100, 100, (u16)(g_fontSys.nMapBitsBase + 12));
    SiRun();
    CheckEqI32((i32)GdHash(100, 100), (i32)uRef, "an active craft with a timer draws mapbits + anim + 12");

    SiFixture(400, 400);
    g_siItem.nType = 1; g_siItem.nX = 180; g_siItem.nY = 140; g_siItem.bActive = 1; g_siItem.bVisible = 0;
    SiRun();
    CheckEqI32(GdCount(100, 100, GD_BOX, GD_BOX), 0, "an active invisible craft is not drawn");

    SiFixture(400, 400);
    g_siItem.nType = 1; g_siItem.nX = 180; g_siItem.nY = 140; g_siItem.bActive = 0; g_siItem.bVisible = 0;
    SiRun();
    CheckEqI32(GdCount(100, 100, GD_BOX, GD_BOX), 0, "nor is an inactive invisible craft");

    /* Zoom scales the stored map coordinates before the scroll is subtracted. */
    /* Zoom scales BOTH coordinates (0x8239C..0x82408 scales nX and nY in turn), so the y the test
     * expects is the scaled one too. */
    SiFixture(400, 400);
    g_siView.nMode = 1;
    g_siItem.nType = 3; g_siItem.nX = 200; g_siItem.nY = 140;
    uRef = SiRef(70, 65, (u16)(g_fontSys.nMapBitsBase + 8)); /* 150 - 80, and 105 - 40 */
    SiRun();
    CheckEqI32((i32)GdHash(70, 65), (i32)uRef,
               "zoom mode 1 scales nX by 1/1.333 (200 -> 150 -> x 70) and nY too (140 -> 105 -> y 65)");
    Check(GdCount(70, 95, GD_BOX, GD_BOX) == 0, "so nothing is painted at the unscaled y 100");

    SiFixture(400, 400);
    g_siView.nMode = 2;
    g_siItem.nType = 3; g_siItem.nX = 200; g_siItem.nY = 140;
    uRef = SiRef(38, 42, (u16)(g_fontSys.nMapBitsBase + 8)); /* 118 - 80, and 82 - 40 */
    SiRun();
    CheckEqI32((i32)GdHash(38, 42), (i32)uRef, "zoom mode 2 scales by 1/1.694 instead (200 -> 118)");

    /* The wrap: 100x100 map gives half extents -70 and -110, so an entry at (100, 100) computes
     * (170, 210) and wraps to (70, 110). An entry at (200, 200) computes (270, 310) and must land on
     * (170, 210) -- ONE subtraction, not a full modulo. */
    SiFixture(100, 100);
    g_siItem.nType = 3; g_siItem.nX = 100; g_siItem.nY = 100;
    uRef = SiRef(70, 110, (u16)(g_fontSys.nMapBitsBase + 8));
    SiRun();
    CheckEqI32((i32)GdHash(70, 110), (i32)uRef, "an out-of-range entry wraps by the map size once");

    SiFixture(100, 100);
    g_siItem.nType = 3; g_siItem.nX = 200; g_siItem.nY = 200;
    uRef = SiRef(170, 210, (u16)(g_fontSys.nMapBitsBase + 8));
    SiRun();
    CheckEqI32((i32)GdHash(170, 210), (i32)uRef,
               "two map sizes out of range wraps to 170, not 70: the wrap is one SUBGT (0x824C0)");

    Smap_DrawStrategIcons(NULL, &g_siEnv);
    Check(true, "a null view is refused rather than dereferenced");
}

/* ---- the clock strip -------------------------------------------------------------------- */

static PadImage             g_ckPad[PUFO_GEOUI_CLOCK_DIGIT_PADS];
static u8                   g_ckGlyph[PUFO_GEOUI_CLOCK_DIGIT_PADS][6];
static PadImage            *g_ckPads[PUFO_GEOUI_CLOCK_DIGIT_PADS];
static GameTime             g_ckTime;
static GfxColorEnv          g_ckColor;
static char                 g_ckTexts[PUFO_TEXT_REC_STRIDE * 1300];
static GeoscapeClockDrawEnv g_ckEnv;

static void CkFixture(void)
{
    GdClear();
    memset(g_aPalette, 0, sizeof(g_aPalette));
    for (i32 i = 0; i < PUFO_GEOUI_CLOCK_DIGIT_PADS; ++i) {
        g_ckGlyph[i][0] = 2;  g_ckGlyph[i][1] = 0; /* u16 width 2 */
        g_ckGlyph[i][2] = 1;  g_ckGlyph[i][3] = 0; /* u16 height 1 */
        g_ckGlyph[i][4] = (u8)(1 + i);             /* the pad's own colour */
        g_ckGlyph[i][5] = 0;                       /* index 0, the key PadImage_BlitEx skips */
        g_aPalette[CK_PAD_SLOT][1 + i] = (u32)(0x1000 + i);
        memset(&g_ckPad[i], 0, sizeof(g_ckPad[i]));
        g_ckPad[i].pData    = g_ckGlyph[i];
        g_ckPad[i].nMode    = (u8)CK_PAD_SLOT;
        g_ckPad[i].uWidth   = 2;
        g_ckPad[i].uHeight  = 1;
        g_ckPad[i].nOriginX = -1;
        g_ckPad[i].nOriginY = -1;
        g_ckPads[i] = &g_ckPad[i];
    }
    memset(g_ckTexts, 0, sizeof(g_ckTexts));
    for (i32 i = 0; i < 7; ++i) {
        snprintf(g_ckTexts + PUFO_TEXT_REC_STRIDE * (PUFO_TEXT_WEEKDAY_FIRST + i),
                 PUFO_TEXT_REC_STRIDE, "WED%d", i + 1);
    }
    snprintf(g_ckTexts + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_CLOCK_SEP, PUFO_TEXT_REC_STRIDE, "/");
    for (i32 i = 0; i < 12; ++i) {
        snprintf(g_ckTexts + PUFO_TEXT_REC_STRIDE * (PUFO_TEXT_MONTH_FIRST + i),
                 PUFO_TEXT_REC_STRIDE, "MONTH%d", i + 1);
    }
    /* 1999 has no zero digit, so pad 0 is touched only by the hour / minute / second tens (all
     * before the day) and by the day's own tens write -- which lets a test read pad 0's final
     * position as proof of the day-tens write. */
    memset(&g_ckTime, 0, sizeof(g_ckTime));
    g_ckTime.nYear = 1999;
    g_ckTime.nMonth = 1;
    g_ckTime.nDay = 3;
    g_ckTime.nWeekday = 1;
    g_ckTime.nHour = 7;
    g_ckTime.nMinute = 5;
    g_ckTime.nSecond = 9;
    memset(&g_ckEnv, 0, sizeof(g_ckEnv));
    g_ckEnv.apDigitPads = g_ckPads;
    g_ckEnv.pTextRecs = g_ckTexts;
    g_ckEnv.pTime = &g_ckTime;
    g_ckEnv.pColor = &g_ckColor;
}

static void TestDrawClockPads(void)
{
    printf("\nGeoscape_DrawClockPads @ 0x5c5c8\n");
    if (!GdEnsureFont()) {
        Check(false, "font.dat loaded, so the clock's text is real");
        return;
    }

    CkFixture();
    Geoscape_DrawClockPads(&g_ckEnv);

    CheckEqI32(BdPix(90, 260), 0x1000, "hour tens 0 is drawn at 90 (no leading-zero rule for hours)");
    CheckEqI32(BdPix(91, 260), 0, "the pad's second column is index 0, the key, and stays empty");
    CheckEqI32(BdPix(100, 260), 0x1007, "hour ones 7 at 100");
    CheckEqI32(BdPix(115, 260), 0x1000, "minute tens 0 at 115");
    CheckEqI32(BdPix(125, 260), 0x1005, "minute ones 5 at 125");
    CheckEqI32(BdPix(140, 260), 0x1000, "second tens 0 at 140");
    CheckEqI32(BdPix(150, 260), 0x1009, "second ones 9 at 150");

    CheckEqI32(BdPix(105, 304), 0x1001, "the year 1999 puts its thousands digit at 105");
    CheckEqI32(BdPix(115, 304), 0x1009, "the hundreds at 115");
    CheckEqI32(BdPix(125, 304), 0x1009, "the tens at 125");
    CheckEqI32(BdPix(135, 304), 0x1009, "and the units at 135");

    CheckEqI32(BdPix(90, 287), 0, "the day 3 tens zero is NOT blitted (0x5CA14 LDRNE / 0x5CA18 BLNE)");
    CheckEqI32(g_ckPad[0].nOriginX, 90, "but its pad is still moved to 90 first (0x5C9FC STR)");
    CheckEqI32(g_ckPad[0].nOriginY, 287, "and to y 287 (0x5CA10 STR)");
    CheckEqI32(BdPix(100, 287), 0x1003, "the day's ones digit is always blitted, at 100");

    /* The weekday name drawn at y 272 paints rows 275..284; the two ':' glyphs drawn at y 258
     * paint rows 263..270 and the day/month line at y 287 starts at 287, so 273..286 isolates it. */
    Check(GdCount(60, 273, 130, 14) > 0, "the weekday name from TEXTS[1244] is drawn at y 272");

    CkFixture();
    g_ckTime.nDay = 23;
    Geoscape_DrawClockPads(&g_ckEnv);
    CheckEqI32(BdPix(90, 287), 0x1002, "a two-digit day blits its tens at 90");
    CheckEqI32(BdPix(100, 287), 0x1003, "and its ones at 100");

    /* Month out of range: psz still holds the TEXTS[1251] separator copied at 0x5CA48, and 0x5CCFC
     * draws that. In range it draws the month name instead, which paints strictly more pixels. */
    CkFixture();
    g_ckTime.nMonth = 0;
    Geoscape_DrawClockPads(&g_ckEnv);
    const i32 nSepPixels = GdCount(128, 285, 60, 14);

    CkFixture();
    g_ckTime.nMonth = 1;
    Geoscape_DrawClockPads(&g_ckEnv);
    const i32 nMonthPixels = GdCount(128, 285, 60, 14);
    Check(nMonthPixels > nSepPixels,
          "month 1 draws TEXTS[1252] while month 0 draws the stale TEXTS[1251] separator");

    /* Weekday out of range: the original would draw its uninitialised buffer; the port draws an
     * empty string, so nothing lands at y 272. */
    CkFixture();
    g_ckTime.nWeekday = 0;
    Geoscape_DrawClockPads(&g_ckEnv);
    CheckEqI32(GdCount(60, 273, 130, 14), 0,
               "weekday 0 draws nothing at y 272 (the port's deviation: the original read uninitialised stack)");

    Geoscape_DrawClockPads(NULL);
    Check(true, "a null environment is refused");
}

/* ---- Geoscape_DrawScreen @ 0x6c16c -------------------------------------------------------
 *
 * Runs on the real 240x320 surface, the real font and the real lang.dat TEXTS records, so every
 * text assertion below is about the shipped strings at their shipped positions. */

#define DS_MAP_SLOT   7
#define DS_MAP_W      6
#define DS_MAP_H      5
#define DS_STRIP_W    8
#define DS_STRIP_H    3
#define DS_MASK_W     40
#define DS_MASK_H     20
#define DS_CTLS       24
#define DS_OPT_CTLS   21

static u8        g_dsMapGlyph[4 + DS_MAP_W * DS_MAP_H];
static u8        g_dsStripGlyph[4 + DS_STRIP_W * DS_STRIP_H];
static u8        g_dsMask[4 + DS_MASK_W * DS_MASK_H];
static u8        g_dsWorld[4 + 16];
static PadImage  g_dsMapPad, g_dsLayerPad, g_dsStripPad;
static SmapView  g_dsView;
static GameTime  g_dsTime;
static u8       *g_dsTexts;
static UiControl g_dsCtl[DS_CTLS];
static PadImage  g_dsCtlPad[DS_CTLS];
static u8        g_dsCtlPadData[DS_CTLS][4 + 4 * 4];
static UiControl *g_dsOptCtl[DS_OPT_CTLS];
static UiPanel   g_dsPanel[4];
static u8        g_dsAnimStarted;
static i8        g_dsAnimProgress;
static UiAnimBlitEnv g_dsAnim;
static i8        g_dsScratch[8];
static u8        g_dsSelFlag, g_dsOptOpen;
static GfxColorEnv g_dsColor;
static i32       g_dsCloseCalls;
static i32       g_dsPadHits[DS_CTLS];
static PadImage  g_dsClockPad[10];
static PadImage *g_dsClockPads[10];
static u8        g_dsClockData[10][4 + 4 * 6];
static PtrArray  g_dsList;
static GeoscapeScreenDrawEnv g_dsEnv;
static char      g_dsSlotLabels[7 * PUFO_GEOOPT_LABEL_STRIDE]; /* MainMenu_pSlotCtx, a flat 280-byte block */
static i16       g_dsCtlBY = 160; /* ctlB's y, moved by one test to show the label follows it */

static void DsCtlBlit(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    for (i32 i = 0; i < DS_CTLS; ++i) {
        if (&g_dsCtlPad[i] == pPad) {
            ++g_dsPadHits[i];
            return;
        }
    }
}

static void DsCtlBlitEx(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
}

static void DsCloseUi(void *pUser)
{
    (void)pUser;
    ++g_dsCloseCalls;
}

static bool DsLoadTexts(void)
{
    AssetReader rdr;
    u8  aHdr[PUFO_TEXT_HDR_SIZE];
    u32 cb, n, i;

    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, "texts.dat")) {
        return false;
    }
    cb = AssetReader_Size(&rdr);
    n  = (cb >= PUFO_TEXT_HDR_SIZE)
       ? (cb - PUFO_TEXT_HDR_SIZE) / PUFO_TEXT_REC_SIZE_DISK : 0u;
    if (n <= (u32)PUFO_TEXT_REC_PICK_LOAD) {
        return false;
    }
    if (!AssetReader_Read(&rdr, aHdr, PUFO_TEXT_HDR_SIZE)) {
        return false;
    }
    g_dsTexts = malloc((size_t)n * PUFO_TEXT_REC_STRIDE);
    if (g_dsTexts == NULL) {
        return false;
    }
    memset(g_dsTexts, 0, (size_t)n * PUFO_TEXT_REC_STRIDE);
    for (i = 0; i < n; ++i) {
        if (!AssetReader_Read(&rdr, g_dsTexts + (size_t)i * PUFO_TEXT_REC_STRIDE,
                              PUFO_TEXT_REC_SIZE_DISK)) {
            break;
        }
    }
    return true;
}

static const char *DsRec(i32 nRec)
{
    return (const char *)g_dsTexts + PUFO_TEXT_REC_STRIDE * nRec;
}

static u16 g_dsSnap[16 * BD_SURF_W];
static void DsFixture(void); /* DsTextScan re-runs it, and it is defined below */

/* Pixels in a box that differ from a sampled background colour. Used where the drawing is not a
 * TEXTS record (the volume screen's snprintf'd digits) and so cannot be isolated by blanking. */
/* A hash of an exact box. GdHash's fixed 40x40 window is too wide here: on the volume screen it
 * swallows the neighbouring caption row, and on the confirm panel it crosses the panel's lower
 * edge, so two runs that differ only in a label's row hash differently for the wrong reason. */
static u32 DsHashBox(i32 x0, i32 y0, i32 nW, i32 nH)
{
    u32 uHash = 2166136261u;

    for (i32 y = 0; y < nH; ++y) {
        for (i32 x = 0; x < nW; ++x) {
            uHash = (uHash ^ (u32)BdPix(x0 + x, y0 + y)) * 16777619u;
        }
    }
    return uHash;
}

static i32 DsDiffBg(i32 x0, i32 y0, i32 nW, i32 nH, u16 uBg)
{
    i32 n = 0;

    for (i32 y = y0; y < y0 + nH; ++y) {
        for (i32 x = x0; x < x0 + nW; ++x) {
            if ((u16)BdPix(x, y) != uBg) {
                ++n;
            }
        }
    }
    return n;
}

/* Runs one menu case twice: once with the TEXTS record intact, once with its first byte zeroed so
 * the draw emits an empty string, and reports the pixels that differ. Every menu paints a solid
 * panel behind its labels (292 in this fixture), so a count of lit pixels would be satisfied by the
 * panel alone -- only the difference between the two runs is the glyphs themselves.
 *
 * Returns the number of differing pixels and, through pnMin / pnMax, the leftmost and rightmost
 * column they fall in. */
static i32 DsTextScan(i32 nCase, i32 nRec, u8 bOverlay, i32 x0, i32 y0, i32 nW, i32 nH,
                      i32 *pnMin, i32 *pnMax)
{
    char chSaved = g_dsTexts[PUFO_TEXT_REC_STRIDE * nRec];
    i32 nDiff = 0;

    *pnMin = BD_SURF_W;
    *pnMax = -1;

    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = (i8)nCase;
    g_dsOptOpen = bOverlay;
    Geoscape_DrawScreen(&g_dsEnv);
    for (i32 y = 0; y < nH; ++y) {
        for (i32 x = 0; x < nW; ++x) {
            g_dsSnap[y * BD_SURF_W + x] = (u16)BdPix(x0 + x, y0 + y);
        }
    }

    g_dsTexts[PUFO_TEXT_REC_STRIDE * nRec] = 0;
    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = (i8)nCase;
    g_dsOptOpen = bOverlay;
    Geoscape_DrawScreen(&g_dsEnv);
    g_dsTexts[PUFO_TEXT_REC_STRIDE * nRec] = chSaved;

    for (i32 x = 0; x < nW; ++x) {
        for (i32 y = 0; y < nH; ++y) {
            if ((u16)BdPix(x0 + x, y0 + y) != g_dsSnap[y * BD_SURF_W + x]) {
                ++nDiff;
                if (x < *pnMin) {
                    *pnMin = x;
                }
                if (x > *pnMax) {
                    *pnMax = x;
                }
            }
        }
    }
    *pnMin += x0;
    *pnMax += x0;
    return nDiff;
}

static void DsFixture(void)
{
    GdClear();
    memset(g_dsPadHits, 0, sizeof(g_dsPadHits));
    g_dsCloseCalls = 0;

    memset(g_aPalette, 0, sizeof(g_aPalette));
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][1] = 0xFFFFu;
    g_aPalette[DS_MAP_SLOT][1]             = 0x0123u;
    g_aPalette[CK_PAD_SLOT][1]             = 0x0FFFu;

    g_dsMapGlyph[0] = DS_MAP_W;   g_dsMapGlyph[1] = 0;
    g_dsMapGlyph[2] = DS_MAP_H;   g_dsMapGlyph[3] = 0;
    memset(g_dsMapGlyph + 4, 1, sizeof(g_dsMapGlyph) - 4);
    g_dsMapPad.pData = g_dsMapGlyph;
    g_dsMapPad.nMode = DS_MAP_SLOT;
    g_dsMapPad.uWidth = DS_MAP_W; g_dsMapPad.uHeight = DS_MAP_H;
    g_dsMapPad.nOriginX = -1; g_dsMapPad.nOriginY = -1;

    g_dsStripGlyph[0] = DS_STRIP_W; g_dsStripGlyph[1] = 0;
    g_dsStripGlyph[2] = DS_STRIP_H; g_dsStripGlyph[3] = 0;
    memset(g_dsStripGlyph + 4, 1, sizeof(g_dsStripGlyph) - 4);
    g_dsStripPad.pData = g_dsStripGlyph;
    g_dsStripPad.nMode = DS_MAP_SLOT;
    g_dsStripPad.uWidth = DS_STRIP_W; g_dsStripPad.uHeight = DS_STRIP_H;
    g_dsStripPad.nOriginX = 0; g_dsStripPad.nOriginY = 0;

    g_dsMask[0] = DS_MASK_W; g_dsMask[1] = 0;
    g_dsMask[2] = DS_MASK_H; g_dsMask[3] = 0;
    memset(g_dsMask + 4, 0xFF, sizeof(g_dsMask) - 4);
    g_dsLayerPad.pData = g_dsMask;
    g_dsLayerPad.nMode = PUFO_RADAR_MAP_PAL_SLOT;
    g_dsLayerPad.uWidth = DS_MASK_W; g_dsLayerPad.uHeight = DS_MASK_H;
    g_dsLayerPad.nOriginX = -1; g_dsLayerPad.nOriginY = -1;

    g_dsWorld[0] = 8; g_dsWorld[1] = 0; g_dsWorld[2] = 2; g_dsWorld[3] = 0;
    memset(g_dsWorld + 4, 1, sizeof(g_dsWorld) - 4);

    memset(&g_dsView, 0, sizeof(g_dsView));
    /* Chosen so both wrap copies of the map pad land on screen: halfW = 100/2 - 120 = -70 and
     * halfH = 100/2 - 160 = -110, which puts the copies at (130, 120) and (30, 120). */
    g_dsView.nScrollX = -60;
    g_dsView.nScrollY = -10;
    g_dsView.nMapW    = 100;
    g_dsView.nMapH    = 100;

    for (i32 i = 0; i < 10; ++i) {
        g_dsClockData[i][0] = 4; g_dsClockData[i][1] = 0;
        g_dsClockData[i][2] = 6; g_dsClockData[i][3] = 0;
        memset(g_dsClockData[i] + 4, 1, sizeof(g_dsClockData[i]) - 4);
        g_dsClockPad[i].pData = g_dsClockData[i];
        g_dsClockPad[i].nMode = CK_PAD_SLOT;
        g_dsClockPad[i].uWidth = 4; g_dsClockPad[i].uHeight = 6;
        g_dsClockPads[i] = &g_dsClockPad[i];
    }

    for (i32 i = 0; i < DS_CTLS; ++i) {
        g_dsCtlPadData[i][0] = 4; g_dsCtlPadData[i][1] = 0;
        g_dsCtlPadData[i][2] = 4; g_dsCtlPadData[i][3] = 0;
        memset(g_dsCtlPadData[i] + 4, 0xC3, sizeof(g_dsCtlPadData[i]) - 4);
        g_dsCtlPad[i].pData = g_dsCtlPadData[i];
        g_dsCtlPad[i].nMode = DS_MAP_SLOT;
        g_dsCtlPad[i].uWidth = 4; g_dsCtlPad[i].uHeight = 4;
        UiControl_ctor(&g_dsCtl[i]);
        g_dsCtl[i].bEnabled = 1;
        g_dsCtl[i].nMode    = 0;
        g_dsCtl[i].nState   = 0;
        g_dsCtl[i].bUnk17   = 0;
        g_dsCtl[i].apPads[0] = &g_dsCtlPad[i];
    }
    for (i32 i = 0; i < DS_OPT_CTLS; ++i) {
        g_dsOptCtl[i] = &g_dsCtl[i];
    }
    for (i32 i = 0; i < DS_OPT_CTLS; ++i) {
        g_dsCtl[i].x = (i16)(10 + 10 * i);
        g_dsCtl[i].y = (i16)(100 + 10 * i);
    }
    g_dsCtl[21].x = 120; g_dsCtl[21].y = 150;   /* pGeoscapeCtlA */
    g_dsCtl[22].x = 60;  g_dsCtl[22].y = g_dsCtlBY;   /* pGeoscapeCtlB */

    memset(g_dsPanel, 0, sizeof(g_dsPanel));
    for (i32 i = 0; i < 4; ++i) {
        g_dsPanel[i].nFont = 17;
    }

    memset(&g_dsTime, 0, sizeof(g_dsTime));
    g_dsTime.nTotalSec = 0;
    (void)GameTime_Reset(&g_dsTime);

    memset(&g_dsList, 0, sizeof(g_dsList));
    PtrArray_ctor(&g_dsList);

    g_dsAnimStarted  = 1;
    g_dsAnimProgress = 100;
    g_dsAnim.pbStarted   = &g_dsAnimStarted;
    g_dsAnim.pnProgress  = &g_dsAnimProgress;
    g_dsAnim.nFrameStyle = 0;

    memset(g_dsScratch, 0, sizeof(g_dsScratch));
    g_dsSelFlag  = 0;
    g_dsOptOpen  = 0;
    g_dsColor.pnGamma = &g_nGammaOne;
    g_dsColor.nFadeCount   = 0;

    memset(&g_dsEnv, 0, sizeof(g_dsEnv));
    g_dsEnv.pView           = &g_dsView;
    g_dsEnv.pMapPad         = &g_dsMapPad;
    g_dsEnv.pLayerPad       = &g_dsLayerPad;
    g_dsEnv.pStripPad       = &g_dsStripPad;
    g_dsEnv.pWorldBits      = g_dsWorld;
    g_dsEnv.nOverlayOriginX = 0;
    g_dsEnv.nScreenId       = 1;
    g_dsEnv.pStrategList    = &g_dsList;
    g_dsEnv.nMapBitsBase    = g_fontSys.nMapBitsBase;
    g_dsEnv.nAnimPhase      = 0;
    g_dsEnv.pTime           = &g_dsTime;
    g_dsEnv.apClockPads     = g_dsClockPads;
    g_dsEnv.pTextRecs       = (const char *)g_dsTexts;
    g_dsEnv.pbMapSelection  = &g_dsSelFlag;
    g_dsEnv.pbOptionsOpen   = &g_dsOptOpen;
    g_dsEnv.abOptionsScratch = g_dsScratch;
    g_dsEnv.pAnim           = &g_dsAnim;
    g_dsEnv.pPanelShips     = &g_dsPanel[0];
    g_dsEnv.pPanelMidA      = &g_dsPanel[1];
    g_dsEnv.pPanelMidB      = &g_dsPanel[2];
    g_dsEnv.pPanelRight     = &g_dsPanel[3];
    g_dsEnv.pCtlA           = &g_dsCtl[21];
    g_dsEnv.pCtlB           = &g_dsCtl[22];
    g_dsEnv.apOptCtl        = g_dsOptCtl;
    g_dsEnv.pSlotLabels     = g_dsSlotLabels;
    g_dsEnv.pColor          = &g_dsColor;
    g_dsEnv.pfnCloseUi      = DsCloseUi;
    g_dsEnv.pCloseUser      = NULL;
    g_dsEnv.pfnCtlBlit      = DsCtlBlit;
    g_dsEnv.pfnCtlBlitEx    = DsCtlBlitEx;
    g_dsEnv.pCtlBlitUser    = NULL;
}

static void TestDrawScreen(void)
{
    i32 nMin, nMax, nDiff, nYes161;
    char szDsMsg[96];

    printf("\nGeoscape_DrawScreen @ 0x6c16c\n");
    if (!GdEnsureFont()) {
        Check(false, "the real font is loaded, so the labels are real glyphs");
        return;
    }
    if (!DsLoadTexts()) {
        Check(false, "lang.dat texts.dat loaded, so the labels are the shipped strings");
        return;
    }
    for (i32 i = 0; i < 7; ++i) {
        snprintf(g_dsSlotLabels + i * PUFO_GEOOPT_LABEL_STRIDE, PUFO_GEOOPT_LABEL_STRIDE,
                 "slot%d", (int)i);
    }

    /* Every label below is reached through a PUFO_TEXT_REC_* index, so the indices themselves
     * have to be checked against the shipped lang.dat: a wrong index still paints a string, it
     * just paints the wrong one. */
    Check(DsRec(PUFO_TEXT_REC_OPTIONS)[0] != 0 && DsRec(PUFO_TEXT_REC_INTERCEPT)[0] != 0
          && DsRec(PUFO_TEXT_REC_OPT_HEAD)[0] != 0 && DsRec(PUFO_TEXT_REC_OPT_SAVE)[0] != 0
          && DsRec(PUFO_TEXT_REC_OPT_LOAD)[0] != 0 && DsRec(PUFO_TEXT_REC_OPT_QUIT)[0] != 0
          && DsRec(PUFO_TEXT_REC_PICK_SAVE)[0] != 0 && DsRec(PUFO_TEXT_REC_PICK_LOAD)[0] != 0
          && DsRec(PUFO_TEXT_REC_QUIT_ASK)[0] != 0 && DsRec(PUFO_TEXT_REC_YES)[0] != 0
          && DsRec(PUFO_TEXT_REC_NO)[0] != 0 && DsRec(PUFO_TEXT_REC_OK)[0] != 0
          && DsRec(PUFO_TEXT_REC_CANCEL)[0] != 0 && DsRec(PUFO_TEXT_REC_VOL_MUSIC)[0] != 0
          && DsRec(PUFO_TEXT_REC_VOL_SOUND)[0] != 0 && DsRec(PUFO_TEXT_REC_OPT_COLUMNS)[0] != 0,
          "all sixteen record indices name a non-empty shipped string");
    Check(strcmp(DsRec(PUFO_TEXT_REC_YES), DsRec(PUFO_TEXT_REC_NO)) != 0,
          "Да and Нет are different records (1163 / 1164)");
    Check(strcmp(DsRec(PUFO_TEXT_REC_OK), DsRec(PUFO_TEXT_REC_CANCEL)) != 0,
          "OK and Отмена are different records (1104 / 1144)");
    Check(strcmp(DsRec(PUFO_TEXT_REC_PICK_SAVE), DsRec(PUFO_TEXT_REC_PICK_LOAD)) != 0,
          "Выбрать запись and Выбрать загрузку are different records (1160 / 1161)");
    Check(strcmp(DsRec(PUFO_TEXT_REC_VOL_MUSIC), DsRec(PUFO_TEXT_REC_VOL_SOUND)) != 0,
          "the two volume captions are different records (1031 / 1032)");
    Check(strcmp(DsRec(PUFO_TEXT_REC_OPT_SAVE), DsRec(PUFO_TEXT_REC_OPT_LOAD)) != 0
          && strcmp(DsRec(PUFO_TEXT_REC_OPT_LOAD), DsRec(PUFO_TEXT_REC_OPT_QUIT)) != 0,
          "the three menu rows are three different records (1157 / 1158 / 1159)");

    /* --- a pending map selection closes the UI and draws nothing at all --- */
    DsFixture();
    g_dsSelFlag = 1;
    Geoscape_DrawScreen(&g_dsEnv);
    CheckEqI32(g_dsCloseCalls, 1, "bMapSelectionFlag set calls Geoscape_CloseUI once (0x6C188)");
    CheckEqI32(GdCount(0, 0, 240, 320), 0, "and the whole frame draws nothing (0x6C194)");
    CheckEqI32(g_dsMapPad.nOriginX, -1, "not even the backdrop pad is moved");
    CheckEqI32(g_dsClockPads[0]->nOriginX, 0, "nor any clock pad");

    /* --- no overlay, no menu: the map alone, with the layer wrap disabled so the two map copies
     *    can be read on their own --- */
    DsFixture();
    g_dsEnv.nScreenId = PUFO_RADAR_SCREEN_NO_WRAP;
    Geoscape_DrawScreen(&g_dsEnv);
    CheckEqI32(g_dsCloseCalls, 0, "with the flag clear the close is not reached");
    CheckEqI32(BdPix(130, 120), 0x0123,
               "map copy 1 sits at -(scroll + nMapW/2 - 120) = -(-60 - 70) = 130 (0x6C1A4)");
    CheckEqI32(BdPix(135, 124), 0x0123, "and the pad is 6x5, so its last pixel is (135, 124)");
    CheckEqI32(BdPix(30, 120), 0x0123,
               "map copy 2 is one map width on: -(scroll + nMapW + halfW) = 30 (0x6C1F0)");
    CheckEqI32(BdPix(80, 122), 0, "and the two copies are separate blits, not a fill");
    CheckEqI32(g_dsMapPad.nOriginX, 30, "the pad is left holding the second (wrap) origin");
    CheckEqI32(g_dsMapPad.nOriginY, 120, "-(scroll + nMapH/2 - 160) = -(-10 - 110) = 120");
    CheckEqI32(BdPix(5, 5), 0,
               "but the menu wash is not painted: gOptionsScratch[4] is zero (0x6C64C)");

    /* --- the layer pad, which takes the overlay origin on x and the map pad's y --- */
    DsFixture();
    Geoscape_DrawScreen(&g_dsEnv);
    CheckEqI32(g_dsLayerPad.nOriginX, 230,
               "the last layer copy is overlay - scroll - halfW + nMapW = 0 + 60 + 70 + 100 (0x6C2B0)");
    CheckEqI32(g_dsLayerPad.nOriginY, 120, "and its y is copied from the map pad, not recomputed");
    Check(BdPix(230, 120) != 0 && BdPix(230, 120) == BdPix(130, 120),
          "the third wrap copy paints the same colour as the first, at its own origin");
    CheckEqI32(BdPix(225, 120), 0, "and it is a discrete 8-wide copy");

    /* --- the wash and the shared tail, on a menu id the switch has no case for --- */
    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = 7;
    Geoscape_DrawScreen(&g_dsEnv);
    Check(BdPix(5, 5) != 0, "the 240x320 wash is painted once the gate opens (0x6C680)");
    CheckEqI32(g_dsPadHits[21], 1, "the unknown id still runs the tail's cancel-control blit (0x6CEB8)");
    Check(DsTextScan(7, PUFO_TEXT_REC_CANCEL, 0, 0, 154, BD_SURF_W, 10, &nMin, &nMax) == 0,
          "an unknown id reaches the tail but fills no string, so the tail draws nothing (0x6C6B8)");

    /* --- case 0: the options menu --- */
    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = PUFO_GEOOPT_MENU;
    Geoscape_DrawScreen(&g_dsEnv);
    CheckEqI32(g_dsPadHits[0] + g_dsPadHits[1] + g_dsPadHits[2] + g_dsPadHits[3], 4,
               "the menu blits exactly its four controls (0x6CC6C)");
    CheckEqI32(g_dsPadHits[21], 2,
               "case 0 blits the cancel control TWICE, at 0x6CE84 and again in the tail");
    /* The font draws its ink a few rows below the y it is given (measured: y 111 puts the glyph at
     * rows 114..123), so every band below starts three rows under the call site. */
    nDiff = DsTextScan(PUFO_GEOOPT_MENU, PUFO_TEXT_REC_OPT_HEAD, 0, 0, 63, BD_SURF_W, 10, &nMin, &nMax);
    Check(nDiff > 0, "the menu title Опции is painted on the y 60 line (0x6CD04)");
    Check(nMin + nMax >= 236 && nMin + nMax <= 242,
          "the title is centred on 120, i.e. 120 - Text_MeasureWidth / 2");
    Check(DsTextScan(PUFO_GEOOPT_MENU, PUFO_TEXT_REC_OPT_SAVE, 0, 0, 104, BD_SURF_W, 10, &nMin, &nMax) > 0,
          "row 0 draws Сохранить игру under control 0 at its y + 1 (0x6CD5C)");
    Check(DsTextScan(PUFO_GEOOPT_MENU, PUFO_TEXT_REC_OPT_QUIT, 0, 0, 134, BD_SURF_W, 10, &nMin, &nMax) > 0,
          "row 3 draws Покинуть игру under control 3 at y 131");
    Check(DsTextScan(PUFO_GEOOPT_MENU, PUFO_TEXT_REC_CANCEL, 0, 0, 154, BD_SURF_W, 10, &nMin, &nMax) > 0,
          "and the tail draws Отмена under the cancel control at y 151 (0x6CEEC)");

    /* --- cases 1 and 2 differ only in the title record --- */
    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = PUFO_GEOOPT_SAVE_LIST;
    Geoscape_DrawScreen(&g_dsEnv);
    CheckEqI32(g_dsPadHits[0] + g_dsPadHits[1] + g_dsPadHits[2] + g_dsPadHits[3]
                   + g_dsPadHits[4] + g_dsPadHits[5] + g_dsPadHits[6], 7,
               "the slot list blits seven controls (0x6CBBC)");
    CheckEqI32(g_dsPadHits[7], 0, "and no eighth");
    const u32 uSaveTitle  = GdHash(100, 18);
    const u32 uSaveLabels = GdHash(100, 101);
    Check(DsTextScan(PUFO_GEOOPT_SAVE_LIST, PUFO_TEXT_REC_PICK_SAVE, 0, 0, 21, BD_SURF_W, 10, &nMin, &nMax) > 0,
          "the save list draws its Выбрать запись title on the y 18 line (0x6CBAC)");
    Check(nMin + nMax >= 236 && nMin + nMax <= 242, "and centres that title on 120 as well");
    Check(DsTextScan(PUFO_GEOOPT_SAVE_LIST, PUFO_TEXT_REC_CANCEL, 0, 0, 154, BD_SURF_W, 10, &nMin, &nMax) > 0,
          "the tail label is Отмена here too (0x6CEEC)");

    /* The seven rows are not TEXTS records: they come from the 280-byte slot table. Changing one
     * entry has to move exactly one row. */
    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = PUFO_GEOOPT_SAVE_LIST;
    Geoscape_DrawScreen(&g_dsEnv);
    snprintf(g_dsSlotLabels, PUFO_GEOOPT_LABEL_STRIDE, "ZZZZZ");
    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = PUFO_GEOOPT_SAVE_LIST;
    Geoscape_DrawScreen(&g_dsEnv);
    Check(GdHash(100, 101) != uSaveLabels, "the seven rows are drawn from MainMenu_pSlotCtx, not from TEXTS");
    snprintf(g_dsSlotLabels, PUFO_GEOOPT_LABEL_STRIDE, "slot0");

    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = PUFO_GEOOPT_LOAD_LIST;
    Geoscape_DrawScreen(&g_dsEnv);
    Check(GdHash(100, 18) != uSaveTitle,
          "the load list's title is a different record (1160 vs 1161)");
    Check(GdHash(100, 101) == uSaveLabels,
          "the seven slot labels are the same table in both cases -- only the title record changes");

    /* --- case 3: Да under ctlB, Нет under ctlA --- */
    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = PUFO_GEOOPT_QUIT_ASK;
    Geoscape_DrawScreen(&g_dsEnv);
    CheckEqI32(g_dsPadHits[22], 1, "the confirm blits pGeoscapeCtlB (0x6C984)");
    CheckEqI32(g_dsPadHits[21], 1, "and the cancel control once, in the tail");
    nYes161 = DsTextScan(PUFO_GEOOPT_QUIT_ASK, PUFO_TEXT_REC_YES, 0, 0, 164, BD_SURF_W, 10, &nMin, &nMax);
    Check(nYes161 > 0, "Да is drawn on the ctlB->y + 1 line, i.e. 161 (0x6C9E0)");
    Check(DsTextScan(PUFO_GEOOPT_QUIT_ASK, PUFO_TEXT_REC_YES, 0, 0, 154, BD_SURF_W, 10, &nMin, &nMax) == 0,
          "and blanking it changes nothing on the tail line -- the two labels are separate draws");
    nDiff = DsTextScan(PUFO_GEOOPT_QUIT_ASK, PUFO_TEXT_REC_NO, 0, 0, 154, BD_SURF_W, 10, &nMin, &nMax);
    Check(nDiff > 0, "Нет is drawn by the tail on the ctlA->y + 1 line, i.e. 151 (0x6CEEC)");
    Check(DsTextScan(PUFO_GEOOPT_QUIT_ASK, PUFO_TEXT_REC_NO, 0, 0, 164, BD_SURF_W, 10, &nMin, &nMax) == 0,
          "and blanking it leaves the Да line untouched");
    nDiff = DsTextScan(PUFO_GEOOPT_QUIT_ASK, PUFO_TEXT_REC_YES, 0, 0, 164, BD_SURF_W, 10, &nMin, &nMax);
    Check(nMin + nMax >= 236 && nMin + nMax <= 242, "Да is centred on 120 like every other label");

    /* The anchor is the control's own y, not a constant: moving ctlB moves the label with it. The
     * differential scan is used again here rather than a background sample, because the menu wash
     * is not one flat colour and would not separate the glyphs from the panel behind them. */
    g_dsCtlBY = 170;
    Check(DsTextScan(PUFO_GEOOPT_QUIT_ASK, PUFO_TEXT_REC_YES, 0, 0, 164, BD_SURF_W, 10, &nMin, &nMax) == 0,
          "with ctlB moved to y 170 the old line 161 draws nothing");
    nDiff = DsTextScan(PUFO_GEOOPT_QUIT_ASK, PUFO_TEXT_REC_YES, 0, 0, 174, BD_SURF_W, 10, &nMin, &nMax);
    Check(nDiff > 0 && nDiff == nYes161,
          "and the identical label now sits on line 171 (LDRSH [R1,#2] then + 1)");
    g_dsCtlBY = 160;

    /* --- case 4: the volume screen --- */
    DsFixture();
    g_dsScratch[4] = 1;
    g_dsScratch[0] = PUFO_GEOOPT_VOLUME;
    Geoscape_DrawScreen(&g_dsEnv);
    i32 nVolHits = 0;
    for (i32 i = 9; i < 21; ++i) {
        nVolHits += g_dsPadHits[i];
    }
    CheckEqI32(nVolHits, 12, "the volume screen blits controls 9..20, the six music then six sound (0x6C78C)");
    CheckEqI32(g_dsPadHits[0] + g_dsPadHits[8], 0, "and none of the others");
    nDiff = DsTextScan(PUFO_GEOOPT_VOLUME, PUFO_TEXT_REC_VOL_MUSIC, 0, 0, 93, BD_SURF_W, 10, &nMin, &nMax);
    Check(nDiff > 0, "the Громкость музыки caption is on the y 90 line (0x6C7F8)");
    Check(nMin + nMax >= 236 && nMin + nMax <= 242, "and it is centred on 120");
    Check(DsTextScan(PUFO_GEOOPT_VOLUME, PUFO_TEXT_REC_VOL_SOUND, 0, 0, 133, BD_SURF_W, 10, &nMin, &nMax) > 0,
          "the Громкость звука caption is on the y 130 line (0x6C850)");
    Check(DsTextScan(PUFO_GEOOPT_VOLUME, PUFO_TEXT_REC_OK, 0, 0, 154, BD_SURF_W, 10, &nMin, &nMax) > 0,
          "and the tail label here is OK, not Отмена (0x6CEEC with the case-4 string)");

    /* The digits are snprintf'd, not TEXTS, so they are isolated against the panel colour itself.
     * Measured: Text_DrawString(x, 111) inks rows 114..123, seven columns wide. */
    const u16 uBg = (u16)BdPix(30, 105);
    for (i32 i = 0; i < PUFO_GEOOPT_VOL_STEPS; ++i) {
        snprintf(szDsMsg, sizeof(szDsMsg),
                 "level %d is inked at x %d on the 111 line and the 151 line", i, 54 + 25 * i);
        Check(DsDiffBg(54 + 25 * i, 114, 7, 10, uBg) > 0
              && DsDiffBg(54 + 25 * i, 154, 7, 10, uBg) > 0, szDsMsg);
    }
    Check(DsDiffBg(61, 114, 18, 10, uBg) == 0, "the columns are 25 apart with a clear gap between them");
    Check(DsDiffBg(54 + 25 * PUFO_GEOOPT_VOL_STEPS, 114, 7, 10, uBg) == 0,
          "and there is no seventh column: the loop stops at 150 (0x6C8C4)");
    Check(DsHashBox(54, 114, 7, 10) == DsHashBox(54, 154, 7, 10),
          "the same digit 0 is drawn on the 111 line and the 151 line");
    Check(DsHashBox(54, 114, 7, 10) != DsHashBox(79, 114, 7, 10),
          "and 0 and 1 are different glyphs, so the loop really counts");
    Check(BdPix(100, 82) != uBg && BdPix(100, 228) != uBg
          && BdPix(22, 150) != uBg && BdPix(217, 150) != uBg,
          "the four border rects at (22,82)-(217,228) are painted (0x6C700)");
    Check(BdPix(100, 105) == uBg, "the inside of that border is plain wash");

    /* --- the options overlay, and the fall-through when its animation is not done --- */
    DsFixture();
    g_dsAnimProgress = 0;
    g_dsOptOpen = 1;
    g_dsScratch[4] = 1;
    g_dsScratch[0] = 7;
    Geoscape_DrawScreen(&g_dsEnv);
    Check(BdPix(5, 5) != 0,
          "an unfinished overlay animation falls through to the menu branch (0x6C3B4 AND)");

    DsFixture();
    g_dsAnimProgress = 100;
    g_dsOptOpen = 1;
    g_dsScratch[4] = 1;
    g_dsScratch[0] = 7;
    Geoscape_DrawScreen(&g_dsEnv);
    Check(BdPix(5, 5) == 0,
          "a finished overlay takes the overlay branch instead, so no menu wash is painted");
    CheckEqI32(g_dsPadHits[7] + g_dsPadHits[8], 2, "the overlay blits option controls 7 and 8 (0x6C5DC)");
    CheckEqI32(g_dsPadHits[0], 0, "and none of the menu controls");
    nDiff = DsTextScan(PUFO_GEOOPT_MENU, PUFO_TEXT_REC_INTERCEPT, 1, 0, 36, BD_SURF_W, 10, &nMin, &nMax);
    Check(nDiff > 0, "the Перехват цели title is on the y 33 line (0x6C488)");
    Check(nMin + nMax >= 236 && nMin + nMax <= 242, "and it is centred on 120 too");
    nDiff = DsTextScan(PUFO_GEOOPT_MENU, PUFO_TEXT_REC_OPT_COLUMNS, 1, 0, 53, BD_SURF_W, 10, &nMin, &nMax);
    Check(nDiff > 0, "the column header row is drawn from x 8 on the y 50 line (0x6C430)");
    Check(nMin >= 8 && nMin <= 12, "and it is left-anchored at 8, not centred");
    Check(BdPix(80, 75) != 0 && BdPix(150, 75) != 0 && BdPix(200, 75) != 0,
          "the three column rules at x 80 / 150 / 200 are painted (0x6C4D0..0x6C530)");
    Check(BdPix(120, 75) == 0, "and nothing between them at that row");

    free(g_dsTexts);
    g_dsTexts = NULL;
    Asset_CloseArchives();
}

/* ---- Smap_SelectMapTarget @ 0x84484 ---------------------------------------------------------
 *
 * The arm that pauses the geoscape. Two craft, three strateg entries, a two-line message list. */

static UiLineList        g_stLines;
static GameTime          g_stTime;
static PtrArray          g_stList;
/* The list owns both its item array and its items: PtrArray_DeleteAt frees the item it drops and
 * PtrArray_Remove frees the slot array and installs a freshly malloc'd one. So the fixture hands in
 * malloc'd storage and never keeps a second handle on it -- after a removal the array the fixture
 * started with is already gone. */
static Ship              g_stShip[2];
static Ship             *g_stShips[2];
static u8                g_stShipCount, g_stOnce;
static GeoscapeWorld     g_stWorld;
static StrategSpawnCtx   g_stSpawn;
static i32               g_stMonth;  /* unk_159648: the roll's weight-row index, LDRSB at 0x81AB4 */
static u16               g_stAddend; /* 0x1472C8: the roll's early gate, LDRH at 0x81B6C */
static ShipRemovalEnv    g_stRem;
static ResearchTopicLive g_stTopics[PUFO_RESEARCH_TOPIC_COUNT];
static SmapSelectTargetEnv g_stEnv;
static i32               g_stSelIdx, g_stMsgIdx, g_stSeed;
static u8                g_stFlag, g_stArmed;

static void StFixture(i32 nLineColour, i32 nType0)
{
    memset(&g_stLines, 0, sizeof(g_stLines));
    g_stLines.nLineCount = 2;
    g_stLines.abLineFlags[0] = 40;
    g_stLines.abLineFlags[1] = 41;
    g_stLines.anColor[0][0] = (u32)nLineColour;
    g_stLines.anColor[1][0] = 7u;

    PtrArray_DeleteAll(&g_stList);
    PtrArray_Clear(&g_stList);
    g_stList.nCount = 0;
    g_stList.apItems = NULL;

    void **apSlots = (void **)malloc(sizeof(void *) * 3);
    for (i32 i = 0; i < 3; ++i) {
        StrategItem *pItem = (StrategItem *)calloc(1, sizeof(StrategItem));
        pItem->nType = (u8)((i == 0) ? nType0 : 1);
        pItem->bActive = 1;
        apSlots[i] = pItem;
    }
    g_stList.apItems = apSlots;
    g_stList.nCount = 3;

    memset(g_stShip, 0, sizeof(g_stShip));
    g_stShip[0].bId = 10; g_stShip[0].bCraftType = 0;
    g_stShip[0].bIntercepting = 1; g_stShip[0].nStrategIdx = 0;
    g_stShip[1].bId = 11; g_stShip[1].bCraftType = 2;   /* the Avenger */
    g_stShip[1].bIntercepting = 1; g_stShip[1].nStrategIdx = 2;
    g_stShips[0] = &g_stShip[0];
    g_stShips[1] = &g_stShip[1];
    g_stShipCount = 2;

    memset(&g_stWorld, 0, sizeof(g_stWorld));
    g_stWorld.apCraft = g_stShips;
    g_stWorld.nShipCount = 2;
    memset(&g_stSpawn, 0, sizeof(g_stSpawn));
    g_stMonth = 0;
    g_stAddend = 0;
    g_stSpawn.pnDifficulty = &g_stMonth;
    g_stSpawn.pnEarlyGate = &g_stAddend;
    g_stSpawn.pnSeed = &g_stSeed;
    g_stOnce = 0;

    memset(&g_stRem, 0, sizeof(g_stRem));
    g_stRem.apCraft = g_stShips;
    g_stRem.pnShipCount = &g_stShipCount;
    g_stRem.pStrategList = &g_stList;
    g_stRem.pWorld = &g_stWorld;
    g_stRem.pnSpawnOnceFlag = &g_stOnce;
    g_stRem.pSpawnCtx = &g_stSpawn;

    memset(g_stTopics, 0, sizeof(g_stTopics));
    memset(&g_stTime, 0, sizeof(g_stTime));

    g_stSelIdx = -1;
    g_stMsgIdx = -1;
    g_stFlag = 0;
    g_stArmed = 0;

    memset(&g_stEnv, 0, sizeof(g_stEnv));
    g_stEnv.pLines = &g_stLines;
    g_stEnv.pnSelectedShipIdx = &g_stSelIdx;
    g_stEnv.pnSlot159E74 = &g_stMsgIdx;
    g_stEnv.pStrategList = &g_stList;
    g_stEnv.pShipEnv = &g_stRem;
    g_stEnv.aTopics = g_stTopics;
    g_stEnv.pTime = &g_stTime;
    g_stEnv.pbSelectionFlag = &g_stFlag;
    g_stEnv.pbFinalMissionCtl = &g_stArmed;
}

static void TestSelectMapTarget(void)
{
    printf("\nSmap_SelectMapTarget @ 0x84484\n");

    /* Line colour 1 selects the Avenger, and its target is a type-1 contact, not a waypoint. */
    StFixture(1, 1);
    g_stTopics[PUFO_RESEARCH_TOPIC_CYDONIA].bUnlocked = 1;
    Smap_SelectMapTarget(&g_stEnv);
    CheckEqI32(g_stFlag, 1, "bMapSelectionFlag is SET here (0x84498) -- the flag that blanks the next frame");
    CheckEqI32(g_stSelIdx, 1, "the ship index is colour slot 0 of message line 0 (0x844A4)");
    CheckEqI32(g_stList.nCount, 3, "a type-1 target is left in the list (0x844F0)");
    CheckEqI32(g_stShip[1].bIntercepting, 1,
               "and bIntercepting survives -- the clear sits inside the type-5 branch (0x84514)");
    CheckEqI32(g_stArmed, 1, "Cydonia researched plus bCraftType 2 arms the final-mission control (0x84544)");
    CheckEqI32(g_stTime.bPaused, 1, "the geoscape is paused, unconditionally (0x84588)");
    CheckEqI32(g_stLines.nLineCount, 2, "insert at 1 then remove 0 leaves the count unchanged");
    CheckEqI32(g_stLines.abLineFlags[0], 27, "line 0 is now the flag-27 line the insert put there");
    CheckEqI32((i32)g_stLines.abLineFlags[1], 41, "and the old line 1 is the one that moved up");
    CheckEqI32(g_stSelIdx, 1, "the fourth read leaves 0x187054 on the value the insert carried (0x845A0)");
    CheckEqI32(g_stMsgIdx, 1, "and writes the same value to 0x159E74 (0x845A4)");

    /* A type-5 waypoint IS removed, and the other craft's reference shifts down with it. */
    StFixture(0, 5);
    Smap_SelectMapTarget(&g_stEnv);
    CheckEqI32(g_stList.nCount, 2, "the waypoint the craft was chasing is removed (0x84504)");
    CheckEqI32(g_stShip[0].bIntercepting, 0, "and its intercept flag is cleared");
    CheckEqI32(g_stShip[1].nStrategIdx, 1,
               "StrategList_RemoveAt shifts the other craft's index down, so it still points at its own item");
    CheckEqI32(g_stArmed, 0,
               "line colour 0 selects the non-Avenger, so nothing is armed even with Cydonia clear");

    /* The arm needs BOTH halves of its test. */
    StFixture(1, 1);
    Smap_SelectMapTarget(&g_stEnv);
    CheckEqI32(g_stArmed, 0, "with the Cydonia topic unresearched the Avenger alone does not arm it");

    StFixture(1, 1);
    g_stTopics[PUFO_RESEARCH_TOPIC_CYDONIA].bUnlocked = 1;
    g_stShip[1].bCraftType = 1;
    Smap_SelectMapTarget(&g_stEnv);
    CheckEqI32(g_stArmed, 0, "and with Cydonia researched a non-Avenger does not arm it either");

    /* Out of range: the original indexes apCraft blind; the port skips the two ship steps and the
     * line work and the pause still happen. */
    StFixture(9, 1);
    g_stTopics[PUFO_RESEARCH_TOPIC_CYDONIA].bUnlocked = 1;
    Smap_SelectMapTarget(&g_stEnv);
    CheckEqI32(g_stArmed, 0, "an out-of-range craft index arms nothing");
    CheckEqI32(g_stTime.bPaused, 1, "but the pause still runs");
    CheckEqI32(g_stLines.abLineFlags[0], 27, "and the message list is still rewritten");
    CheckEqI32(g_stMsgIdx, 9, "the fourth read still records the index the insert carried");

    Smap_SelectMapTarget(NULL);
    Check(true, "a NULL environment is a no-op rather than a crash");

    PtrArray_DeleteAll(&g_stList);
    PtrArray_Clear(&g_stList);
    g_stList.nCount = 0;
    g_stList.apItems = NULL;
}

/* ---- Geoscape_PulseUpdate @ 0x954dc ---------------------------------------------------------
 *
 * Three independent halves -- the map-screen craft pass, the phase-gated screen switch, and the four
 * saturating counters -- so each is driven on its own. */

static i32 g_nPulseClicks;
static i32 g_nTickShips, g_nTickHourly, g_nTickBases, g_nOpenMonthly;

static void PulseCountClick(void) { ++g_nPulseClicks; }
static void PulseTickShips(void *p)   { (void)p; ++g_nTickShips; }
static void PulseTickHourly(void *p)  { (void)p; ++g_nTickHourly; }
static void PulseTickBases(void *p)   { (void)p; ++g_nTickBases; }
static void PulseOpenMonthly(void *p) { (void)p; ++g_nOpenMonthly; }

static SmapView              g_pvView;
static PtrArray              g_pvList;
static void                 *g_pvSlots[3];
static StrategItem           g_pvItem[3];
static Ship                  g_pvShip[2];
static Ship                 *g_pvShips[2];
static u8                    g_pvShipCount;
static u8                    g_pvDrag[4];
static i32                   g_pvPhase, g_pvFail;
static i32                   g_pvCR, g_pvCL, g_pvCM, g_pvCP;
static UiControl             g_pvCtl[6];
static UiControl            *g_pvCtls[6];
static UiControlHitCtx       g_pvHit;
static u32                   g_pvUiSlot;
static GameTime              g_pvTime;
static GeoscapeClockHooks    g_pvHooks;
static GeoscapePulseCtlGroup g_pvGroup[3];
static GeoscapePulseScreen   g_pvScreen[2];
static GeoscapePulseEnv      g_pvEnv;
static u8                    g_pvGate;

static void PvFixture(i32 nScreenId)
{
    g_nPulseClicks = 0;
    g_nTickShips = g_nTickHourly = g_nTickBases = g_nOpenMonthly = 0;

    memset(&g_pvView, 0, sizeof(g_pvView));
    g_pvView.nMapW = 400;
    g_pvView.nMapH = 400;

    /* item 0 is the player craft's own entry: type 1, INACTIVE, id 10. Item 1 has the same id but is
     * active, item 2 is inactive but type 0 -- neither is a match. */
    memset(g_pvItem, 0, sizeof(g_pvItem));
    for (i32 i = 0; i < 3; ++i) {
        g_pvSlots[i] = &g_pvItem[i];
        g_pvItem[i].nDisplayId = 10;
    }
    g_pvItem[0].nType = 1; g_pvItem[0].bActive = 0;
    g_pvItem[1].nType = 1; g_pvItem[1].bActive = 1;
    g_pvItem[2].nType = 0; g_pvItem[2].bActive = 0;
    g_pvList.nCount = 3;
    g_pvList.apItems = g_pvSlots;

    memset(g_pvShip, 0, sizeof(g_pvShip));
    g_pvShip[0].bId = 10; g_pvShip[0].bAtBase = 0;
    g_pvShip[1].bId = 11; g_pvShip[1].bAtBase = 1;
    g_pvShips[0] = &g_pvShip[0];
    g_pvShips[1] = &g_pvShip[1];
    g_pvShipCount = 2;

    memset(g_pvDrag, 0, sizeof(g_pvDrag));
    g_pvPhase = 0;
    g_pvFail = 0;
    g_pvCR = 0; g_pvCL = 1; g_pvCM = 2; g_pvCP = 3;

    memset(&g_pvTime, 0, sizeof(g_pvTime));
    (void)GameTime_Reset(&g_pvTime);

    memset(g_pvCtl, 0, sizeof(g_pvCtl));
    for (i32 i = 0; i < 6; ++i) {
        g_pvCtls[i] = &g_pvCtl[i];
        g_pvCtl[i].bEnabled = 1;
        g_pvCtl[i].nMode = 0;
        g_pvCtl[i].nState = 1;                 /* lit, so a forced click fires */
        g_pvCtl[i].pfnClick = PulseCountClick;
    }
    g_pvUiSlot = 7;
    memset(&g_pvHit, 0, sizeof(g_pvHit));
    g_pvHit.pnUiSlot0 = &g_pvUiSlot;

    g_pvGate = 1;

    /* Screen 18 gets three groups: 2 ungated, 3 gated on a set byte, 1 gated on a CLEAR byte. With
     * the gate set that is 5 clicks; with it clear, 3. */
    g_pvGroup[0].apCtl = &g_pvCtls[0]; g_pvGroup[0].nCount = 2;
    g_pvGroup[0].pbGate = NULL; g_pvGroup[0].pbNotGate = NULL;
    g_pvGroup[1].apCtl = &g_pvCtls[2]; g_pvGroup[1].nCount = 3;
    g_pvGroup[1].pbGate = &g_pvGate; g_pvGroup[1].pbNotGate = NULL;
    g_pvGroup[2].apCtl = &g_pvCtls[5]; g_pvGroup[2].nCount = 1;
    g_pvGroup[2].pbGate = NULL; g_pvGroup[2].pbNotGate = &g_pvGate;
    g_pvScreen[0].nScreenId = 18;
    g_pvScreen[0].pGroups = g_pvGroup;
    g_pvScreen[0].nGroupCount = 3;
    g_pvScreen[1].nScreenId = 4;
    g_pvScreen[1].pGroups = NULL;
    g_pvScreen[1].nGroupCount = 0;

    memset(&g_pvHooks, 0, sizeof(g_pvHooks));
    g_pvHooks.pfnTickShips = PulseTickShips;
    g_pvHooks.pfnTickHourly = PulseTickHourly;
    g_pvHooks.pfnTickBases = PulseTickBases;
    g_pvHooks.pfnOpenMonthly = PulseOpenMonthly;

    memset(&g_pvEnv, 0, sizeof(g_pvEnv));
    g_pvEnv.pnActionFail = &g_pvFail;
    g_pvEnv.nScreenId = nScreenId;
    g_pvEnv.pView = &g_pvView;
    g_pvEnv.nStripHeight = 0;
    g_pvEnv.pabDragFlags = g_pvDrag;
    g_pvEnv.apCraft = g_pvShips;
    g_pvEnv.pnCraftCount = &g_pvShipCount;
    g_pvEnv.pStrategList = &g_pvList;
    g_pvEnv.pTime = &g_pvTime;
    g_pvEnv.pClockHooks = &g_pvHooks;
    g_pvEnv.nTickSeconds = 59;
    g_pvEnv.pnAnimPhase = &g_pvPhase;
    g_pvEnv.pnCooldownR = &g_pvCR;
    g_pvEnv.pnCooldownL = &g_pvCL;
    g_pvEnv.pnMsgTimer = &g_pvCM;
    g_pvEnv.pnPulseTimer = &g_pvCP;
    g_pvEnv.pScreens = g_pvScreen;
    g_pvEnv.nScreenCount = 2;
    g_pvEnv.pHitCtx = &g_pvHit;
}

static void TestPulseUpdate(void)
{
    printf("\nGeoscape_PulseUpdate @ 0x954dc\n");

    /* --- a failed action stops everything, cooldowns included --- */
    PvFixture(0);
    g_pvFail = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvTime.nTotalSec, 0, "dwActionFail == 1 returns before the clock (0x954F0)");
    CheckEqI32(g_pvCR, 0, "and before the four counters");
    CheckEqI32(g_pvItem[0].bVisible, 0, "and before the craft pass");

    /* --- the craft pass is map-screen only --- */
    PvFixture(18);
    g_pvPhase = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvItem[0].bVisible, 0, "off the map screen the craft pass does not run");
    CheckEqI32(g_pvTime.nTotalSec, 0, "and neither does the clock");
    CheckEqI32(g_pvCR, 1, "but the counters still step (0x95B8C)");

    PvFixture(0);
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvItem[0].bVisible, 1,
               "an INACTIVE type-1 entry with nDisplayId == Ship.bId becomes visible (0x955B0)");
    CheckEqI32(g_pvShip[0].bWeaponsHot, 1, "and the craft gets bWeaponsHot (0x955C8)");
    CheckEqI32(g_pvItem[1].bVisible, 0, "the ACTIVE entry with the same id is not touched (bActive == 0)");
    CheckEqI32(g_pvItem[2].bVisible, 0, "and the inactive TYPE-0 entry is not touched either (nType == 1)");
    CheckEqI32(g_pvTime.nTotalSec, 60, "the clock advances nGeoscapeTickSeconds + 1 (0x9562C)");
    Check(g_nTickShips > 0, "and the minute tick actually ran");

    PvFixture(0);
    g_pvShip[0].bAtBase = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvItem[0].bVisible, 0, "a craft that is AT base is skipped outright (0x9554C)");

    PvFixture(0);
    g_pvShipCount = 0;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvTime.nTotalSec, 60,
               "an empty craft list jumps INTO the clock block rather than skipping it (LABEL_13, 0x95618)");

    PvFixture(0);
    g_pvTime.bPaused = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvTime.nTotalSec, 0, "the pause byte at 0x15961C gates the clock (0x95618)");

    /* --- the phase gate: the switch fires only when the phase arrives as exactly 1 --- */
    PvFixture(18);
    g_pvPhase = 0;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_nPulseClicks, 0, "phase 0 increments to 1, which is not above 1, so no switch (0x95654)");
    CheckEqI32(g_pvPhase, 0, "and the phase is reset either way (LABEL_88, 0x95B78)");

    PvFixture(18);
    g_pvPhase = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_nPulseClicks, 5, "phase 1 increments to 2 and the switch runs: 2 + 3 gated-on + 0 gated-off");
    CheckEqI32(g_pvPhase, 0, "and the phase resets");

    PvFixture(18);
    g_pvPhase = 2;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_nPulseClicks, 0, "a phase already above 1 SKIPS the switch entirely (0x95644)");
    CheckEqI32(g_pvPhase, 0, "and is reset");

    PvFixture(18);
    g_pvGate = 0;
    g_pvPhase = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_nPulseClicks, 3, "flipping the gate moves the group that runs: 2 + 0 + 1");

    PvFixture(18);
    g_pvCtl[0].nState = 0;
    g_pvPhase = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_nPulseClicks, 4,
               "a forced click only fires on a control already in state 1, so the unlit one is silent");

    PvFixture(4);
    g_pvPhase = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_nPulseClicks, 0, "a screen with no group list runs the switch and clicks nothing");

    /* --- the held-direction scroll, map screen: step 25, absolute, flags cleared --- */
    PvFixture(0);
    g_pvPhase = 1;
    g_pvDrag[PUFO_PULSE_FLAG_UP] = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvView.nScrollY, -25, "the up flag moves the view by 25 (0x95718, 0x95864)");
    CheckEqI32(g_pvView.nScrollX, 0, "and nothing on x");
    CheckEqI32(g_pvView.nLastDx, 120, "nLastDx ends on the 120 that was passed (0x95714 preset, stored last)");
    CheckEqI32(g_pvView.nLastDy, 185,
               "nLastDy ends on 160 + 25, which is what Smap_ScrollBy was handed (0x9570C, 0x95864)");
    CheckEqI32(g_pvDrag[PUFO_PULSE_FLAG_UP], 0, "the flag is cleared afterwards (0x95874)");

    PvFixture(0);
    g_pvPhase = 1;
    g_pvDrag[PUFO_PULSE_FLAG_DOWN] = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvView.nScrollY, 25, "the down flag goes the other way (0x9572C)");

    PvFixture(0);
    g_pvPhase = 1;
    g_pvDrag[PUFO_PULSE_FLAG_RIGHT] = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvView.nScrollX, -25, "the right flag moves x by 25 (0x95724)");

    PvFixture(0);
    g_pvPhase = 1;
    g_pvDrag[PUFO_PULSE_FLAG_LEFT] = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvView.nScrollX, 25, "and the left flag the other way (0x95734)");

    PvFixture(0);
    g_pvPhase = 1;
    g_pvDrag[PUFO_PULSE_FLAG_UP] = 1;
    g_pvDrag[PUFO_PULSE_FLAG_DOWN] = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvView.nScrollY, 0, "opposite flags cancel, because each is an independent +/- (0x9572C)");

    PvFixture(0);
    g_pvPhase = 1;
    g_pvView.nLastDx = 999;
    g_pvView.nLastDy = 999;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvView.nScrollX, 0, "with no flag set the view does not move");
    CheckEqI32(g_pvView.nLastDx, 999, "and the last-delta preset is not written either");

    PvFixture(2);
    g_pvPhase = 1;
    g_pvDrag[PUFO_PULSE_FLAG_UP] = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvView.nScrollY, -5, "the zoom screen uses step 5, not 25 (0x95844)");

    /* Smap_ScrollBy skips its vertical clamp when the screen id is 2, so the same starting scroll
     * clamps on the map screen and does not on the zoom screen. */
    PvFixture(0);
    g_pvPhase = 1;
    g_pvView.nScrollY = -100;
    g_pvDrag[PUFO_PULSE_FLAG_UP] = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvView.nScrollY, -40,
               "on the map screen the clamp pulls it back to -(nMapH/2 - 160) (Smap_ScrollBy)");

    PvFixture(2);
    g_pvPhase = 1;
    g_pvView.nScrollY = -100;
    g_pvDrag[PUFO_PULSE_FLAG_UP] = 1;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvView.nScrollY, -105, "on the zoom screen the clamp is skipped, so it stays where it went");

    /* --- the four counters saturate at 255 --- */
    PvFixture(18);
    g_pvCR = 254; g_pvCL = 255; g_pvCM = 0; g_pvCP = 255;
    Geoscape_PulseUpdate(&g_pvEnv);
    CheckEqI32(g_pvCR, 255, "254 steps to 255 (0x95B8C)");
    CheckEqI32(g_pvCL, 255, "255 stays at 255 (0x95BA0)");
    CheckEqI32(g_pvCM, 1, "and an unrelated counter is unaffected");
    CheckEqI32(g_pvCP, 255, "the pulse timer saturates too (0x95BC8)");

    Geoscape_PulseUpdate(NULL);
    Check(true, "a NULL environment is a no-op rather than a crash");
}

/* ------------------------------------------------------- Geoscape_DrawGlobeHover @ 0x6727c
 *
 * Fixture: a real 240x320 surface, the shipped lang.dat TEXTS records, and a 100x100 tile grid with
 * nScrollX = nScrollY = 0. With the strip pad at uHeight 100 the sample point is (120, 110), so
 * tileX = 0 + 50 + 120 - 120 = 50 and tileY = 0 + 50 + 110 - 160 = 0 -- tile (50, 0). At uHeight 102
 * the point is (120, 109), tileY becomes -1, and the one wrap puts it at tile (50, 99).
 *
 * Rect fills here take their colour straight from Blit_PackedColor -- the index plus the two colour
 * bytes, no palette -- so the expected pixel values are arithmetic and are pinned exactly:
 *   rule  0x32 / 0xC8 / 0x32 -> 13894   sweep 0x0A / 0xFF / 0x0A -> 4065
 *   scan  8 / 80 / 85        -> 2698    wash  0xFF / 255 / 255   -> 65535 */
#define GH_DIM        100
#define GH_STRIP_H    100
#define GH_RULE_COL   13894u
#define GH_SWEEP_COL   4065u
#define GH_SCAN_COL    2698u
#define GH_WASH_COL   65535u
#define GH_SAMPLE_IDX (0 * GH_DIM + 50)  /* tile (50, 0): what (120, 110) resolves to */
#define GH_ALT_IDX    (99 * GH_DIM + 50) /* tile (50, 99): what (120, 109) wraps to */

static u8        g_ghTiles[GH_DIM * GH_DIM];
static u8        g_ghStripGlyph[4 + 8 * 3]; /* the strip pad's bytes are never read here, only uHeight is */
static PadImage  g_ghStripPad;
static SmapView  g_ghView;
static i32       g_ghScanX, g_ghSweepV, g_ghSweepH, g_ghPendX, g_ghPendY;
static u8        g_ghPending;
static UiControl g_ghCtl;
static PadImage  g_ghCtlPad;
static u8        g_ghCtlPadData[4 + 6 * 4];
static u8        g_ghAnimStarted;
static i8        g_ghAnimProgress;
static UiAnimBlitEnv g_ghAnim;
static GfxColorEnv g_ghColor;
static GeoscapeGlobeHoverEnv g_ghEnv;
static i32       g_ghCtlBlits;
static i32       g_ghStripH = GH_STRIP_H;
static i32       g_ghSeed;
static i32       g_ghAnimDone = 1;
static i32       g_ghSetScanX, g_ghSetSweepV, g_ghSetSweepH; /* the counters each frame starts from */
static u16       g_ghSnap[10 * BD_SURF_W];

/* aNewBaseCostByRegion @ 0xab488, read straight out of the image. */
static const u32 g_ghRegionCost[10] = { 800000, 600000, 1000000, 800000, 750000,
                                        650000, 900000, 950000, 800000, 0 };

/* The sixteen tick columns in row nMidY - 1: (i + 3)^2 and 240 - (i + 3)^2 for i = 0..7. */
static const i32 g_aGhTickCols[16] = { 9, 16, 25, 36, 49, 64, 81, 100,
                                       140, 159, 176, 191, 204, 215, 224, 231 };

static void GhCtlBlit(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
    ++g_ghCtlBlits;
}

static void GhCtlBlitEx(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
}

/* Seeds whose FIRST Game_RandU16(nMax) is nWant. The wash key is the first roll the frame makes, so
 * this pins it without touching the six readout rolls that follow. */
static i32 GhSeedFor(i32 nMax, i32 nWant)
{
    for (i32 nSeed = 1; nSeed < 200000; ++nSeed) {
        srand((unsigned)nSeed);
        if ((i32)Game_RandU16((u16)nMax) == nWant) {
            return nSeed;
        }
    }
    return -1;
}

/* A seed whose first roll is nWant1 and whose second (the first readout number) is nWant2. Two
 * seeds with the same nWant1 paint the same wash and the same crosshair but different readouts,
 * which is what separates the seed-dependent drawing from the seed-independent one. */
static i32 GhSeedForRolls(i32 nWant1, i32 nWant2)
{
    for (i32 nSeed = 1; nSeed < 200000; ++nSeed) {
        srand((unsigned)nSeed);
        if ((i32)Game_RandU16(10) == nWant1 && (i32)Game_RandU16(PUFO_GLOBE_READOUT_SPAN) == nWant2) {
            return nSeed;
        }
    }
    return -1;
}

static u16 g_ghRowSnap[BD_SURF_W];

static void GhRowSave(i32 nY)
{
    for (i32 x = 0; x < BD_SURF_W; ++x) {
        g_ghRowSnap[x] = (u16)BdPix(x, nY);
    }
}

/* Columns of one row that changed since GhRowSave. The scan strips blend (key 100), so their exact
 * colour is not arithmetic the way an opaque rect fill is; the difference between two runs is. */
static i32 GhRowDiff(i32 nY, i32 *pnMin, i32 *pnMax)
{
    i32 n = 0;

    *pnMin = BD_SURF_W;
    *pnMax = -1;
    for (i32 x = 0; x < BD_SURF_W; ++x) {
        if ((u16)BdPix(x, nY) != g_ghRowSnap[x]) {
            ++n;
            if (x < *pnMin) {
                *pnMin = x;
            }
            if (x > *pnMax) {
                *pnMax = x;
            }
        }
    }
    return n;
}

static void GhFixture(i32 nTile)
{
    memset(g_ghTiles, 0, sizeof(g_ghTiles));
    g_ghTiles[GH_SAMPLE_IDX] = (u8)nTile;
    g_ghTiles[GH_ALT_IDX]    = PUFO_REGION_AFRICA; /* the tile the sample moves to when the strip grows */

    memset(&g_ghStripPad, 0, sizeof(g_ghStripPad));
    g_ghStripPad.pData   = g_ghStripGlyph;
    g_ghStripPad.uWidth  = 8;
    g_ghStripPad.uHeight = (u16)g_ghStripH;

    memset(&g_ghView, 0, sizeof(g_ghView));
    g_ghView.nScrollX = 0;
    g_ghView.nScrollY = 0;
    g_ghView.nMapW    = GH_DIM;
    g_ghView.nMapH    = GH_DIM;

    UiControl_ctor(&g_ghCtl);
    memset(g_ghCtlPadData, 0xC3, sizeof(g_ghCtlPadData));
    g_ghCtlPad.pData   = g_ghCtlPadData;
    g_ghCtlPad.uWidth  = 60;
    g_ghCtlPad.uHeight = 4;
    g_ghCtl.apPads[0]  = &g_ghCtlPad;
    g_ghCtl.bEnabled   = 1;
    g_ghCtl.uWidth     = 60; /* the control's own width is what 0x679F0 halves, not the pad's */

    g_ghAnim.pbStarted   = &g_ghAnimStarted;
    g_ghAnim.pnProgress  = &g_ghAnimProgress;
    g_ghAnim.nFrameStyle = 0;

    g_ghColor.pnGamma = &g_nGammaOne;
    g_ghColor.nFadeCount   = 0;

    memset(&g_ghEnv, 0, sizeof(g_ghEnv));
    g_ghEnv.pStripPad          = &g_ghStripPad;
    g_ghEnv.pTiles             = g_ghTiles;
    g_ghEnv.pView              = &g_ghView;
    g_ghEnv.pAnim              = &g_ghAnim;
    g_ghEnv.pnPendingPickX     = &g_ghPendX;
    g_ghEnv.pnPendingPickY     = &g_ghPendY;
    g_ghEnv.pnScanX            = &g_ghScanX;
    g_ghEnv.pnSweepV           = &g_ghSweepV;
    g_ghEnv.pnSweepH           = &g_ghSweepH;
    g_ghEnv.pbPlacementPending = &g_ghPending;
    g_ghEnv.pCtlConfirm        = &g_ghCtl;
    g_ghEnv.pTextRecs          = (const char *)g_dsTexts;
    g_ghEnv.paRegionCost       = g_ghRegionCost;
    g_ghEnv.pColor             = &g_ghColor;
    g_ghEnv.pfnCtlBlit         = GhCtlBlit;
    g_ghEnv.pfnCtlBlitEx       = GhCtlBlitEx;

    g_ghSetScanX  = 0;
    g_ghSetSweepV = 37; /* both sweep gates closed by default, so a test opens exactly one at a time */
    g_ghSetSweepH = 15;
    g_ghPendX  = -1;
    g_ghPendY  = -1;
    g_ghPending = 0;
    g_ghSeed   = GhSeedFor(10, 0);
    g_ghAnimDone = 1;
}

/* One frame, from a known start state. GhTextScan replays it, so every input is re-applied here. */
static i32 GhRun(void)
{
    GdClear();
    g_ghStripPad.uHeight = (u16)g_ghStripH;
    g_ghScanX  = g_ghSetScanX;  /* every frame starts from the same counters, so GhTextScan's two
                                 * runs differ only in the TEXTS record it blanked */
    g_ghSweepV = g_ghSetSweepV;
    g_ghSweepH = g_ghSetSweepH;
    g_ghCtl.x = 7;   /* sentinels: a moved control is then visible */
    g_ghCtl.y = 9;
    g_ghCtlBlits = 0;
    g_ghAnimStarted  = g_ghAnimDone ? 1 : 0;
    g_ghAnimProgress = g_ghAnimDone ? 100 : 50;
    g_ghView.nPickX  = 111; /* the PREVIOUS pick, which the pending copy must see */
    g_ghView.nPickY  = 222;
    srand((unsigned)g_ghSeed);
    return Geoscape_DrawGlobeHover(&g_ghEnv);
}

/* Columns of one row whose colour is exactly uWant. */
static i32 GhRowColour(i32 nY, i32 x0, i32 x1, u16 uWant, i32 *pnFirst, i32 *pnLast)
{
    i32 n = 0;

    *pnFirst = -1;
    *pnLast  = -1;
    for (i32 x = x0; x <= x1; ++x) {
        if ((u16)BdPix(x, nY) == uWant) {
            ++n;
            if (*pnFirst < 0) {
                *pnFirst = x;
            }
            *pnLast = x;
        }
    }
    return n;
}

/* Runs one frame twice, once with a TEXTS record intact and once with its first byte zeroed, and
 * reports the pixels that differ plus the dominant colour among them. The frame paints a solid panel
 * and two dark rects behind every label, so a count of lit pixels would be satisfied by those alone;
 * only the difference between the two runs is the glyphs. The dominant colour is what pins the two
 * Gfx_PackRgb565Pair calls and their order. */
static i32 GhTextScan(i32 nRec, i32 x0, i32 y0, i32 nW, i32 nH,
                      i32 *pnMin, i32 *pnMax, u16 *puInk)
{
    char  chSaved = (char)g_dsTexts[PUFO_TEXT_REC_STRIDE * nRec];
    u16   aSeen[32];
    i32   aCount[32];
    i32   nColours = 0;
    i32   nDiff = 0;
    i32   nBest = 0;

    *pnMin = BD_SURF_W;
    *pnMax = -1;
    *puInk = 0;

    (void)GhRun();
    for (i32 y = 0; y < nH; ++y) {
        for (i32 x = 0; x < nW; ++x) {
            g_ghSnap[y * BD_SURF_W + x] = (u16)BdPix(x0 + x, y0 + y);
        }
    }

    g_dsTexts[PUFO_TEXT_REC_STRIDE * nRec] = 0;
    (void)GhRun();
    g_dsTexts[PUFO_TEXT_REC_STRIDE * nRec] = (u8)chSaved;

    for (i32 y = 0; y < nH; ++y) {
        for (i32 x = 0; x < nW; ++x) {
            const u16 uNow = (u16)BdPix(x0 + x, y0 + y);

            if (uNow != g_ghSnap[y * BD_SURF_W + x]) {
                /* The colour tallied is the one from the run that DREW the text: the blanked run
                 * leaves the panel behind there, which is the same for every label and would make
                 * every ink look identical. */
                const u16 uInk = g_ghSnap[y * BD_SURF_W + x];

                ++nDiff;
                if (x0 + x < *pnMin) {
                    *pnMin = x0 + x;
                }
                if (x0 + x > *pnMax) {
                    *pnMax = x0 + x;
                }
                i32 k;
                for (k = 0; k < nColours; ++k) {
                    if (aSeen[k] == uInk) {
                        ++aCount[k];
                        break;
                    }
                }
                if (k == nColours && nColours < 32) {
                    aSeen[nColours] = uInk;
                    aCount[nColours] = 1;
                    ++nColours;
                }
            }
        }
    }

    for (i32 k = 0; k < nColours; ++k) {
        if (aCount[k] > nBest) {
            nBest = aCount[k];
            *puInk = aSeen[k];
        }
    }
    return nDiff;
}

static void TestGlobeHover(void)
{
    i32 nMin, nMax, nFirst, nLast, nDiff, nRet, nLit, i;
    i32 nSeed0, nSeed8, nSeed9, nSeedA, nSeedB, nMinLong, nMinShort;
    u16 uInkName, uInkCost, uInkBuild, uInkOther;
    u32 uHashReadout, uHashCrosshair;
    char szMsg[120];

    printf("\nGeoscape_DrawGlobeHover @ 0x6727c\n");
    if (!GdEnsureFont()) {
        Check(false, "the real font is loaded, so the labels are real glyphs");
        return;
    }
    if (g_dsTexts == NULL) {
        /* TestDrawScreen frees the blob it loaded, and a later group may have closed the archives,
         * so this group reopens them and loads its own copy. */
        static const char *const apszArchive[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
        (void)Asset_OpenArchives(apszArchive);
        if (!DsLoadTexts()) {
            Check(false, "lang.dat texts.dat loaded, so the labels are the shipped strings");
            return;
        }
    }

    nSeed0 = GhSeedFor(10, 0);
    nSeed8 = GhSeedFor(10, 8);
    nSeed9 = GhSeedFor(10, 9);
    nSeedA = GhSeedForRolls(0, 0);
    nSeedB = GhSeedForRolls(0, PUFO_GLOBE_READOUT_SPAN - 1);
    Check(nSeed0 > 0 && nSeed8 > 0 && nSeed9 > 0, "seeds exist for a first roll of 0, 8 and 9");
    Check(nSeedA > 0 && nSeedB > 0 && nSeedA != nSeedB,
          "and for a first roll of 0 paired with a first readout number of 100 or 999");

    /* --- 1. the wash key: rolls 0..7 collapse to 0, and key 0 leaves the pixel alone --- */
    GhFixture(PUFO_REGION_EUROPE);
    g_ghSeed = nSeed0;
    (void)GhRun();
    CheckEqI32(BdPix(200, 30), 0,
               "a roll below 8 becomes key 0 (0x672B4 MOVLT), and key 0 leaves the surface alone");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSeed = nSeed8;
    (void)GhRun();
    CheckEqI32(BdPix(200, 30), 32,
               "roll 8 blends 8/255 of the index-0xFF wash: only the 6-bit field survives truncation");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSeed = nSeed9;
    (void)GhRun();
    CheckEqI32(BdPix(200, 30), 2113,
               "roll 9 blends more, so the two surviving rolls really are different keys");

    /* --- 2. the crosshair: the rule, its span, and the sixteen (i + 3)^2 ticks --- */
    GhFixture(PUFO_REGION_EUROPE);
    nRet = GhRun();
    CheckEqI32(nRet, PUFO_REGION_EUROPE,
               "the hovered tile is the return value, which the caller stores as bPendingRegionId");

    nLit = GhRowColour(110, 0, 239, GH_RULE_COL, &nFirst, &nLast);
    CheckEqI32(nLit, 240, "the horizontal rule spans the whole width at (320 - uHeight) / 2 = 110");
    CheckEqI32(nFirst, 0, "from x=0");
    CheckEqI32(nLast, 239, "to x=239");

    nLit = GhRowColour(109, 0, 239, GH_RULE_COL, &nFirst, &nLast);
    CheckEqI32(nLit, 17, "row 109 carries the sixteen tick marks plus the vertical rule crossing it");
    Check((u16)BdPix(120, 109) == GH_RULE_COL, "the 17th column is the vertical rule, not a tick");
    for (i = 0; i < 16; ++i) {
        snprintf(szMsg, sizeof(szMsg), "tick %d of 16 sits at x=%d ((i + 3)^2 and its mirror)",
                 i + 1, g_aGhTickCols[i]);
        Check((u16)BdPix(g_aGhTickCols[i], 109) == GH_RULE_COL, szMsg);
    }
    Check((u16)BdPix(10, 109) != GH_RULE_COL, "the gap after the first tick (x=9) is empty");
    Check((u16)BdPix(101, 109) != GH_RULE_COL, "and the gap after the last one (x=100) is empty");

    Check((u16)BdPix(120, 0) == GH_RULE_COL, "the vertical rule starts at y=0");
    Check((u16)BdPix(120, 219) == GH_RULE_COL, "and runs to 320 - uHeight - 1 = 219");
    Check((u16)BdPix(120, 220) != GH_RULE_COL, "one pixel further is the panel, not the rule");
    Check((u16)BdPix(119, 0) != GH_RULE_COL, "the rule is one column wide");

    Check((u16)BdPix(119, 9) == GH_RULE_COL, "the upper vertical ticks are 3 wide at x=119..121");
    Check((u16)BdPix(121, 9) == GH_RULE_COL, "including the far column");
    Check((u16)BdPix(118, 9) != GH_RULE_COL, "and no further");
    Check((u16)BdPix(119, 211) == GH_RULE_COL, "the lower mirror is at 320 - uHeight - (i + 3)^2 = 211");
    Check((u16)BdPix(119, 120) == GH_RULE_COL, "down to the last one at 120");

    /* --- 3. the panel gate: while it expands, nothing below the frame runs --- */
    GhFixture(PUFO_REGION_EUROPE);
    g_ghAnimDone = 0;
    g_ghSetScanX = 7; g_ghSetSweepV = 5; g_ghSetSweepH = 3;
    nRet = GhRun();
    CheckEqI32(nRet, 0, "an unfinished Ui_AnimExpandBlit returns the frame out at 0x67490");
    CheckEqI32(g_ghScanX, 7, "so the scan counter never advances");
    CheckEqI32(g_ghSweepV, 5, "nor the vertical sweep counter");
    CheckEqI32(g_ghSweepH, 3, "nor the horizontal one");
    CheckEqI32(g_ghPendX, -1, "and the pending pick is not written");
    CheckEqI32(g_ghCtlBlits, 0, "and the Build base button is never blitted");
    CheckEqI32((i32)g_ghCtl.x, 7, "nor moved");

    /* --- 4. the counters, and the order of the pending pick --- */
    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetScanX = 7; g_ghSetSweepV = 5; g_ghSetSweepH = 3;
    (void)GhRun();
    CheckEqI32(g_ghScanX, 8, "all three counters step by one, after the drawing (0x67664)");
    CheckEqI32(g_ghSweepV, 6, "including the vertical sweep");
    CheckEqI32(g_ghSweepH, 4, "and the horizontal one");
    CheckEqI32(g_ghPendX, 111,
               "the pending copy sees the pick from BEFORE this frame's lookup (0x6786C runs first)");
    CheckEqI32(g_ghPendY, 222, "for both axes");
    CheckEqI32(g_ghView.nPickX, 50, "and the lookup writes the view's pick: 0 + 50 + 120 - 120");
    CheckEqI32(g_ghView.nPickY, 0, "0 + 50 + 110 - 160 = 0, so (120, 110) resolves to tile (50, 0)");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetScanX = 319;
    (void)GhRun();
    CheckEqI32(g_ghScanX, 320, "319 steps to 320 and stays, because the wrap is past 320 (0x67684)");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetScanX = 320;
    (void)GhRun();
    CheckEqI32(g_ghScanX, 0, "320 steps to 321, which is past the wrap, so it zeroes");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetSweepV = 39; g_ghSetSweepH = 9;
    (void)GhRun();
    CheckEqI32(g_ghSweepV, 40, "39 steps to 40, which is not past the sweep wrap");
    CheckEqI32(g_ghSweepH, 10, "so the horizontal counter keeps its own value");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetSweepV = 40; g_ghSetSweepH = 9;
    (void)GhRun();
    CheckEqI32(g_ghSweepV, 0, "past 40 the vertical sweep zeroes (0x67694 CMP #0x28)");
    CheckEqI32(g_ghSweepH, 0, "and it takes the horizontal one with it -- one condition, two stores");

    /* --- 5. the sweep gates, measured on the surface --- */
    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetSweepV = 36; g_ghSetSweepH = 15;
    (void)GhRun();
    nLit = GhRowColour(228, 0, 239, GH_SWEEP_COL, &nFirst, &nLast);
    CheckEqI32(nLit, 2, "sweep 36 draws the vertical pair: x = 2 * (36 + 80) and 235 - 2 * 36");
    CheckEqI32(nFirst, 163, "the inner line at 235 - 72 = 163");
    CheckEqI32(nLast, 232, "the outer line at 2 * 116 = 232");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetSweepV = 37; g_ghSetSweepH = 15;
    (void)GhRun();
    CheckEqI32(GhRowColour(228, 0, 239, GH_SWEEP_COL, &nFirst, &nLast), 0,
               "sweep 37 is past the gate (0x67594), so the pair is gone");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetSweepV = 37; g_ghSetSweepH = 14;
    (void)GhRun();
    nLit = GhRowColour(228, 0, 239, GH_SWEEP_COL, &nFirst, &nLast);
    CheckEqI32(nLit, 75, "sweep 14 draws the horizontal pair, 75 wide from x=160");
    CheckEqI32(nFirst, 160, "starting at 160");
    CheckEqI32(nLast, 234, "ending at 234");
    CheckEqI32(GhRowColour(202, 0, 239, GH_SWEEP_COL, &nFirst, &nLast), 75,
               "and its mirror at 2 * (115 - 14) = 202");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetSweepV = 37; g_ghSetSweepH = 15;
    (void)GhRun();
    CheckEqI32(GhRowColour(228, 0, 239, GH_SWEEP_COL, &nFirst, &nLast), 0,
               "sweep 15 is past the horizontal gate (0x67600)");
    CheckEqI32(GhRowColour(202, 0, 239, GH_SWEEP_COL, &nFirst, &nLast), 0, "on both rows");
    CheckEqI32(GhRowColour(230, 0, 239, GH_SWEEP_COL, &nFirst, &nLast), 0,
               "and nothing lands at 2 * (15 + 100) = 230, the row a gate of 16 would have used");

    /* --- 6. the scan strips and their two bounds --- */
    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetScanX = 220; /* both bounds fail here, so this row is the baseline */
    (void)GhRun();
    GhRowSave(242);

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetScanX = 0;
    (void)GhRun();
    nLit = GhRowDiff(242, &nMin, &nMax);
    CheckEqI32(nLit, 20, "scan 0 draws both 10-wide strips: x=10..19 and x=230..239");
    CheckEqI32(nMin, 10, "the left copy starts at scanX + 10");
    CheckEqI32(nMax, 239, "the right copy ends at 230 - scanX + 9");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetScanX = 219;
    (void)GhRun();
    nLit = GhRowDiff(242, &nMin, &nMax);
    CheckEqI32(nLit, 20, "scan 219 still draws both: 229 < 230 and 11 > 10");
    CheckEqI32(nMin, 11, "the right copy is now at 230 - 219 = 11");
    CheckEqI32(nMax, 238, "and the left copy at 219 + 10 = 229, ten wide");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSetScanX = 220;
    (void)GhRun();
    CheckEqI32(GhRowDiff(242, &nMin, &nMax), 0,
               "scan 220 fails both bounds (0x6752C / 0x67560), so neither strip is drawn");

    /* --- 7. the dark rects the readouts sit on --- */
    GhFixture(PUFO_REGION_EUROPE);
    (void)GhRun();
    CheckEqI32(BdPix(79, 200), 25356,
               "the 75x30 rect at (5, 200) blends index 0xFF at key 100 over the cleared surface (0x674E0)");
    CheckEqI32(BdPix(234, 200), 25356, "and its twin at (160, 200) (0x674BC)");

    /* --- 8. the readout lines: seeded, live, and really inked --- */
    /* Two seeds with the SAME wash key but different first readout numbers: everything that does
     * not depend on the roll has to match, everything that does has to differ. */
    GhFixture(PUFO_REGION_EUROPE);
    g_ghSeed = nSeedA;
    (void)GhRun();
    uHashReadout   = DsHashBox(5, 203, 75, 10);
    uHashCrosshair = DsHashBox(0, 109, 240, 1);

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSeed = nSeedA;
    (void)GhRun();
    Check(DsHashBox(5, 203, 75, 10) == uHashReadout,
          "the same seed paints the same readout, so the frame is reproducible");

    GhFixture(PUFO_REGION_EUROPE);
    g_ghSeed = nSeedB;
    (void)GhRun();
    Check(DsHashBox(5, 203, 75, 10) != uHashReadout,
          "the same key with different rolls paints a different readout -- the six rolls are live");
    Check(DsHashBox(0, 109, 240, 1) == uHashCrosshair,
          "while the tick row is identical, because nothing there depends on the roll");
    Check(DsDiffBg(5, 203, 75, 10, 25356u) > 0,
          "digits are inked over the dark rect, not just reserved space");

    /* --- 9. the region labels, isolated by blanking each TEXTS record --- */
    GhFixture(PUFO_REGION_EUROPE);
    (void)GhRun();
    nDiff = GhTextScan(PUFO_GLOBE_TEXT_TERRAIN, 0, 253, 240, 10, &nMin, &nMax, &uInkName);
    Check(nDiff > 0, "TEXTS[1136] \"Terrain: \" really is drawn at y=250");
    nDiff = GhTextScan(PUFO_GLOBE_TEXT_REGION0 + PUFO_REGION_EUROPE, 0, 253, 240, 10,
                       &nMin, &nMax, &uInkOther);
    Check(nDiff > 0, "and so is the region's own name, TEXTS[905 + tile]");
    Check(uInkName == uInkOther, "both come from the colour pair set at 0x678B8");

    nDiff = GhTextScan(PUFO_GLOBE_TEXT_COST, 0, 273, 240, 10, &nMin, &nMax, &uInkCost);
    Check(nDiff > 0, "TEXTS[1137] \"Base cost: \" is drawn at y=270");
    Check(uInkCost == uInkName, "the cost line shares the name's colour -- no colour call between them");

    nDiff = GhTextScan(PUFO_GLOBE_TEXT_BUILD, 0, 293, 240, 10, &nMin, &nMax, &uInkBuild);
    Check(nDiff > 0, "TEXTS[1603] \"Build base\" is drawn at y=290");
    Check(uInkBuild != uInkName, "and it is a different colour, because 0x67A34 repacks between them");
    Check(nMin + nMax >= 236 && nMin + nMax <= 242,
          "the caption is centred on 120 (120 - Text_MeasureWidth / 2)");

    CheckEqI32((i32)g_ghCtl.x, 90, "the confirm control centres on 120 - uWidth / 2, uWidth 60 (0x679F0)");
    CheckEqI32((i32)g_ghCtl.y, 290, "and pins to y=290 (0x67A00)");
    CheckEqI32(g_ghCtlBlits, 1, "its pad is blitted exactly once (0x67A08)");

    /* centring follows the real measured width, not a fixed offset */
    GhFixture(PUFO_REGION_NORTH_AMERICA);
    GhTextScan(PUFO_GLOBE_TEXT_REGION0 + PUFO_REGION_NORTH_AMERICA, 0, 253, 240, 10,
               &nMinLong, &nMax, &uInkOther);
    GhFixture(PUFO_REGION_ASIA);
    GhTextScan(PUFO_GLOBE_TEXT_REGION0 + PUFO_REGION_ASIA, 0, 253, 240, 10,
               &nMinShort, &nMax, &uInkOther);
    Check(nMinLong < nMinShort,
          "the longer region name starts further left, so the centring uses its measured width");

    /* --- 10. the cost line's gate --- */
    GhFixture(PUFO_REGION_EUROPE);
    g_ghPending = 1;
    (void)GhRun();
    Check(GhTextScan(PUFO_GLOBE_TEXT_COST, 0, 273, 240, 10, &nMin, &nMax, &uInkOther) == 0,
          "bPlacementPending @ 0x1870AC suppresses the cost line (0x679A8)");
    Check(GhTextScan(PUFO_GLOBE_TEXT_TERRAIN, 0, 253, 240, 10, &nMin, &nMax, &uInkOther) > 0,
          "but not the name line");
    Check(GhTextScan(PUFO_GLOBE_TEXT_BUILD, 0, 293, 240, 10, &nMin, &nMax, &uInkOther) > 0,
          "nor the caption");
    CheckEqI32(g_ghCtlBlits, 1, "and the button still blits -- the gate covers only the cost text");

    /* --- 11. ocean: the no-build label and nothing else --- */
    GhFixture(PUFO_REGION_ID_NONE);
    nRet = GhRun();
    CheckEqI32(nRet, PUFO_REGION_ID_NONE, "tile 0xFF is the ocean and is returned unchanged");
    Check(GhTextScan(PUFO_GLOBE_TEXT_OCEAN, 0, 253, 240, 10, &nMin, &nMax, &uInkOther) > 0,
          "ocean gets TEXTS[1042] centred at y=250");
    Check(GhTextScan(PUFO_GLOBE_TEXT_COST, 0, 273, 240, 10, &nMin, &nMax, &uInkOther) == 0,
          "no cost line on ocean");
    Check(GhTextScan(PUFO_GLOBE_TEXT_BUILD, 0, 293, 240, 10, &nMin, &nMax, &uInkOther) == 0,
          "no Build base caption either");
    CheckEqI32(g_ghCtlBlits, 0, "and the button is not blitted");
    CheckEqI32((i32)g_ghCtl.x, 7, "nor moved -- the whole region arm is skipped");

    /* --- 12. the sample point follows the strip height --- */
    g_ghStripH = 102;
    GhFixture(PUFO_REGION_EUROPE);
    nRet = GhRun();
    CheckEqI32(nRet, PUFO_REGION_AFRICA,
               "a 2-pixel taller strip moves the sample to (120, 109), a whole tile row up (0x67884)");
    Check((u16)BdPix(0, 109) == GH_RULE_COL, "and the horizontal rule moves with it, at (320 - 102) / 2");
    Check((u16)BdPix(120, 217) == GH_RULE_COL, "the vertical rule shortens to 320 - uHeight too");
    Check((u16)BdPix(120, 218) != GH_RULE_COL, "one pixel past it is the panel");
    g_ghStripH = GH_STRIP_H;

    free(g_dsTexts);
    g_dsTexts = NULL;
}

/* ---- Geoscape_DrawStatusPanel @ 0x8c760..0x8e658 ------------------------------------
 *
 * The panel is tested as a frame. The fixture runs it once with every text source emptied and
 * once with them filled; the pixels that differ are exactly the glyphs, so their rows, their
 * centring anchors and their colour are read straight off the surface. The expected rows and
 * anchors are the ARM's, listed in pufo_geoscape.h.
 */
#define PN_TEXT_MAX 1715 /* the highest record any suite here reaches: 1712..1714, the funding arm's captions
                          * (the panel and BuildStatusMsg only reach 1446, the alien-base label) */
#define PN_BAND     12   /* a row's search window below its y; rows are at least 14 apart */
#define PN_GLYPH_DY  2   /* Text_DrawString puts the ink PN_GLYPH_DY + 1 below the y it is given */
/* Every arm of the chain ends on the shared tail at 0x91CEC, which blits the strip pad at its own origin
 * (30,250), draws the clock down to y 305 and blits the root block's thirteen widgets. So an arm's OWN ink
 * is measured above PN_TAIL_INK_Y, and its own control blits above the tail's thirteen. The 0x1B arm is the
 * exception: every exit of it jumps to 0x944B4, so it never reaches the tail. */
#define PN_TAIL_INK_Y     250

/* The panel geometry constants, pinned to the immediates read off the ARM. The frame tests below
 * compare against the same names the implementation uses, so without these pins a change to a
 * constant would move the expectation and the code together and no test would notice. */
_Static_assert(PUFO_STATUS_TEXT_X_CENTRE == 120, "0x8C8F0 RSB R1, R1, #0x78");
_Static_assert(PUFO_STATUS_TEXT_X_LEFT == 28, "0x8E3C4-side footer label");
_Static_assert(PUFO_STATUS_TEXT_X_RIGHT == 0xD1, "0x8E3D8 RSB R1, R1, #0xD1");
_Static_assert(PUFO_STATUS_ANIM_Y == 0x6E, "0x8C894 MOV R2, #0x6E");
_Static_assert(PUFO_STATUS_ANIM_W == 0xEC, "0x8C898 MOV R3, #0xEC");
_Static_assert(PUFO_STATUS_ANIM_H_TALL == 0x68, "0x8C888 MOV R3, #0x68");
_Static_assert(PUFO_STATUS_ANIM_H_STD == 0x5A, "0x8E3E8 MOV R3, #0x5A");
_Static_assert(PUFO_STATUS_ANIM_Y_RESEARCH == 0x14, "the '%' arm's anim y");
_Static_assert(PUFO_STATUS_ANIM_H_RESEARCH == 0xBE, "the '%' arm's anim height");
_Static_assert(PUFO_STATUS_Y_MSG == 0x7C, "0x8E458 MOV R2, #0x7C");
_Static_assert(PUFO_STATUS_Y_EXTRA == 0x8A, "0x8C9A8-side extra line");
_Static_assert(PUFO_STATUS_Y_TEXT == 0x98, "0x8E484 MOV R2, #0x98");
_Static_assert(PUFO_STATUS_Y_TEXT2 == 0xA6, "the second record, ' ' and '$' only");
_Static_assert(PUFO_STATUS_Y_FOOT_STD == 0xB2, "0x8E3C4 MOV R2, #0xB2");
_Static_assert(PUFO_STATUS_Y_FOOT_TALL == 0xC0, "the ' ' and '$' footer");
_Static_assert(PUFO_STATUS_Y_FOOT_RESEARCH == 0xBC, "the '%' footer");
_Static_assert(PUFO_STATUS_Y_RESEARCH_TITLE == 0x1A, "the '%' title line");
_Static_assert(PUFO_STATUS_Y_RESEARCH_MSG == 0x2C, "the '%' pszMsg line");
_Static_assert(PUFO_STATUS_Y_RESEARCH_REPORT == 0x4B, "0x8D478-side report line, R8 = 0x4B");
_Static_assert(PUFO_STATUS_Y_RESEARCH_HEAD == 0x6F, "0x8D51C TEXTS[1202]");
_Static_assert(PUFO_STATUS_Y_RESEARCH_ROW == 0x7F, "0x8D5D0 ADD R2, R2, #0x7F");
_Static_assert(PUFO_STATUS_Y_RESEARCH_APPLY == 0x8E, "0x8D670 / 0x8D6E0 ADD R3, R2, #0x8E");
_Static_assert(PUFO_STATUS_RESEARCH_ROW_STEP == 14, "0x8D6C4 MOV R2, #0xE");
_Static_assert(PUFO_STATUS_CTL_X_SFX == 9, "0x8D6F4 MOV R3, #9");
_Static_assert(PUFO_STATUS_CTL_X_OPEN == 0xBE, "0x8D71C MOV R3, #0xBE");
_Static_assert(PUFO_STATUS_CTL_Y_TALL == 0xBD, "the ' ' and '$' control row");
_Static_assert(PUFO_STATUS_CTL_Y_STD == 0xAF, "0x8E4A8 MOV R0, #0xAF");
_Static_assert(PUFO_STATUS_CTL_Y_RESEARCH == 0xB9, "0x8D6F8 MOV R2, #0xB9");
_Static_assert(PUFO_STATUS_CTL_PARKED == 1000, "0x8D3D0 MOV R11, #0x3E8");

/* The selected-contact block: the record offsets read off the ARM as (addr - 0xB38D0) / 205. */
_Static_assert(PUFO_STATUS_TEXT_NO_MONEY == 406, "0x8E6B0 record 406");
_Static_assert(PUFO_STATUS_TEXT_CANCEL == 1144, "0x8F1F8 MOV R1, #0x39418; 0x39418 / 205 == 1144");
_Static_assert(PUFO_STATUS_TEXT_UFO == 1210, "0x8E7C8-side title line");
_Static_assert(PUFO_STATUS_TEXT_SIZE_LABEL == 1211, "0x8E854 (unk_F028F - 0xB38D0) / 205 == 1211");
_Static_assert(PUFO_STATUS_TEXT_ATTACK == 1212, "0x8F178 (unk_F035C - 0xB38D0) / 205 == 1212");
_Static_assert(PUFO_STATUS_TEXT_CENTRE == 1213, "0x8F1A8 (unk_F0429 - 0xB38D0) / 205 == 1213");
_Static_assert(PUFO_STATUS_TEXT_HYPERWAVE == 1345, "0x8EE00 (unk_F6DDD - 0xB38D0) / 205 == 1345");
_Static_assert(PUFO_STATUS_TEXT_TYPE_LABEL == 1346, "0x8EE54 (unk_F6EAA - 0xB38D0) / 205 == 1346");
_Static_assert(PUFO_STATUS_TEXT_RACE_LABEL == 1347, "0x8EF4C (unk_F6F77 - 0xB38D0) / 205 == 1347");
_Static_assert(PUFO_STATUS_TEXT_MISSION_LABEL == 1348, "0x8F018 (unk_F7044 - 0xB38D0) / 205 == 1348");
_Static_assert(PUFO_STATUS_TEXT_SIZE_FIRST == 1359, "0x8E964 MOV R3, #0x44043; 0x44043 / 205 == 1359");
_Static_assert(PUFO_STATUS_TEXT_SPEED_LABEL == 1364, "0x8EC18 (unk_F7D14 - 0xB38D0) / 205 == 1364");
_Static_assert(PUFO_STATUS_TEXT_KNOTS == 1365, "0x8EC48 (unk_F7DE1 - 0xB38D0) / 205 == 1365");
_Static_assert(PUFO_STATUS_TEXT_TYPE_FIRST == 1366, "0x8EED4 LDR R3, =0x445DE; 0x445DE / 205 == 1366");
_Static_assert(PUFO_STATUS_TEXT_RACE_FIRST == 1374, "0x8EFF4 LDR R3, =0x44C46; 0x44C46 / 205 == 1374");
_Static_assert(PUFO_STATUS_TEXT_MISSION_FIRST == 1379, "0x8F094 MOV R3, #0x45047; 0x45047 / 205 == 1379");
_Static_assert(PUFO_STATUS_SIZE_BASE_STEP == 61, "0x8E920 MOV R2, #0x3D");
_Static_assert(PUFO_STATUS_TEXT_X_LEFT_PANEL == 0x28, "0x8EF40 MOV R1, #0x28");
_Static_assert(PUFO_STATUS_Y_FAIL_TEXT == 0x9E, "0x8E6D0 MOV R2, #0x9E");
_Static_assert(PUFO_STATUS_Y_SEL_SIZE == 0x60, "0x8EBBC MOV R2, #0x60");
_Static_assert(PUFO_STATUS_Y_SEL_SPEED == 0x70, "0x8EC5C MOV R2, #0x70");
_Static_assert(PUFO_STATUS_Y_SEL_HYPER == 0x3C, "0x8EE44 MOV R2, #0x3C");
_Static_assert(PUFO_STATUS_Y_SEL_TYPE == 0x80, "0x8EF3C MOV R2, #0x80");
_Static_assert(PUFO_STATUS_Y_SEL_RACE == 0x90, "0x8F008 MOV R2, #0x90");
_Static_assert(PUFO_STATUS_Y_SEL_MISSION == 0xA0, "0x8F0FC MOV R2, #0xA0");
_Static_assert(PUFO_STATUS_SELCTL_Y_STD_CONF == 0x88, "0x8ECCC MOV R3, #0x88");
_Static_assert(PUFO_STATUS_SELCTL_Y_STD_CLEAR == 0xA0, "0x8ECF4 MOV R3, #0xA0");
_Static_assert(PUFO_STATUS_SELCTL_Y_HYP_CONF == 0xB4, "0x8F124 MOV R3, #0xB4");
_Static_assert(PUFO_STATUS_SELCTL_Y_HYP_CLEAR == 0xCC, "0x8F150 MOV R3, #0xCC");
_Static_assert(PUFO_STATUS_Y_SEL_LABEL_HYP == 0xB4, "0x8F1E8 MOV R2, #0xB4");
_Static_assert(PUFO_STATUS_Y_SEL_CANCEL_STD == 0xA1, "0x8EDF4 MOV R2, #0xA1");
_Static_assert(PUFO_STATUS_Y_SEL_CANCEL_HYP == 0xCD, "0x8F230 MOV R2, #0xCD");
_Static_assert(PUFO_STATUS_SELFLAG == 0x0A, "the flag gate before 0x8E708");

/* The order questions, 0x8F250..0x8F9E0. */
_Static_assert(PUFO_STATUS_FLAG_BASE_LOST == 0x11, "0x8F270 CMP R0, #0x11");
_Static_assert(PUFO_STATUS_FLAG_FUEL_LOW == 0x0F, "0x8F41C CMP R0, #0xF");
_Static_assert(PUFO_STATUS_FLAG_FLY_TO_POINT == 0x0C, "0x8F5B4 CMP R0, #0xC");
_Static_assert(PUFO_STATUS_FLAG_POINT_REACHED == 0x0E, "0x8F768 CMP R0, #0xE");
_Static_assert(PUFO_STATUS_TEXT_ORDER_OK == 1127, "aTextOk @ 0xEBF4B; (0xEBF4B - 0xB38D0) / 205 == 1127");
_Static_assert(PUFO_STATUS_TEXT_BASE_LABEL == 1214, "0x8F268 ORR R9,R9,#0x26 -> 0x3CC26 / 205 == 1214");
_Static_assert(PUFO_STATUS_TEXT_BASE_LOST == 1215, "0x8F328 (unk_F05C3 - 0xB38D0) / 205 == 1215");
_Static_assert(PUFO_STATUS_TEXT_SHIP_LABEL == 1216, "0x8F418 ORR R9,R9,#0x1C0 -> 0x3CDC0 / 205 == 1216");
_Static_assert(PUFO_STATUS_TEXT_FUEL_LOW == 1217, "0x8F4CC (unk_F075D - 0xB38D0) / 205 == 1217");
_Static_assert(PUFO_STATUS_TEXT_FLY_TO_POINT == 1218, "0x8F5F4 (unk_F082A - 0xB38D0) / 205 == 1218");
_Static_assert(PUFO_STATUS_TEXT_POINT_REACHED == 1219, "0x8F824 (unk_F08F7 - 0xB38D0) / 205 == 1219");
_Static_assert(PUFO_STATUS_TEXT_NEW_PATROL == 1220, "0x8F87C (unk_F09C4 - 0xB38D0) / 205 == 1220");
_Static_assert(PUFO_STATUS_TEXT_RETARGET == 1221, "0x8F990 (unk_F0A91 - 0xB38D0) / 205 == 1221");
_Static_assert(PUFO_STATUS_ORDER_NH_SHORT == 0x5A, "0x8F264 MOV R8, #0x5A");
_Static_assert(PUFO_STATUS_ORDER_NH_TALL == 0x78, "0x8F78C MOV R3, #0x78");
_Static_assert(PUFO_STATUS_Y_ORDER_LINE1 == 0x46, "0x8F318 MOV R2, #0x46");
_Static_assert(PUFO_STATUS_Y_ORDER_LINE2 == 0x5A, "0x8F370 MOV R2, #0x5A");
_Static_assert(PUFO_STATUS_Y_ORDER_0E_LINE2 == 0x56, "0x8F86C MOV R2, #0x56");
_Static_assert(PUFO_STATUS_Y_ORDER_0E_LINE3 == 0x66, "0x8F8C4 MOV R2, #0x66");
_Static_assert(PUFO_STATUS_ORDERCTL_Y_ONE == 0x73, "0x8F26C MOV R6, #0x73");
_Static_assert(PUFO_STATUS_Y_ORDER_OK_ONE == 0x74, "0x8F3F4 MOV R2, #0x74");
_Static_assert(PUFO_STATUS_ORDERCTL_Y_TOP == 0x64, "0x8F678 MOV R3, #0x64");
_Static_assert(PUFO_STATUS_Y_ORDER_OK_TOP == 0x65, "0x8F6F4 MOV R2, #0x65");
_Static_assert(PUFO_STATUS_ORDERCTL_Y_BOTTOM == 0x7C, "0x8F5B0 MOV R6, #0x7C");
_Static_assert(PUFO_STATUS_Y_ORDER_CANCEL == 0x7D, "0x8F74C / 0x8F980 MOV R2, #0x7D");
_Static_assert(PUFO_STATUS_ORDERCTL_Y_0E_CONF == 0x96, "0x8F904 MOV R3, #0x96");
_Static_assert(PUFO_STATUS_RADAR_MSG_BYTES == 108, "szRadarMsg 0x186B10 up to nSmapTapX 0x186B7C");

/* The craft-detail card @ 0x8f9e4, pinned the same way: the record constants are the byte offsets
 * the ARM literals carry (0xF0B5E, 0xF0C2B, 0xF0CF8, 0xF0DC5, 0xF71DE, ... minus 0xB38D0), and the
 * geometry constants are the MOV immediates next to each Text_DrawString. */
_Static_assert(PUFO_STATUS_FLAG_CRAFT_DETAIL == 0x0B, "0x8F9F0 CMP R0, #0xB");
_Static_assert(PUFO_STATUS_DETAIL_NH == 0xDC, "0x8FA50 MOV R3, #0xDC");
_Static_assert(PUFO_STATUS_Y_DETAIL_SHIP == 0x0F, "0x8FAEC MOV R2, #0xF");
_Static_assert(PUFO_STATUS_Y_DETAIL_BASE == 0x1E, "0x8FB74 MOV R2, #0x1E");
_Static_assert(PUFO_STATUS_Y_DETAIL_SPEED == 0x2D, "0x8FBF4 MOV R2, #0x2D");
_Static_assert(PUFO_STATUS_Y_DETAIL_FUEL == 0x3C, "0x8FCA4 MOV R2, #0x3C");
_Static_assert(PUFO_STATUS_DETAIL_Y_WEAPON0 == 0x4B, "0x8FE74 ADD R2, R1, #0x4B");
_Static_assert(PUFO_STATUS_DETAIL_Y_AMMO0 == 0x5A, "0x8FF70, the ammo row 9 bytes under the weapon row");
_Static_assert(PUFO_STATUS_DETAIL_ROW_STEP == 0x1E, "0x8FE64 MOV R8, #0x1E");
_Static_assert(PUFO_STATUS_DETAILCTL_Y_RETURN == 0x8C, "0x9005C MOV R3, #0x8C");
_Static_assert(PUFO_STATUS_DETAILCTL_Y_TARGET == 0xA0, "0x90068 MOV R3, #0xA0");
_Static_assert(PUFO_STATUS_DETAILCTL_Y_PATROL == 0xB4, "0x90074 MOV R3, #0xB4");
_Static_assert(PUFO_STATUS_DETAILCTL_Y_CANCEL == 0xC8, "0x90080 MOV R3, #0xC8");
_Static_assert(PUFO_STATUS_Y_DETAIL_CANCEL_LBL == 0xC9, "0x90210 MOV R2, #0xC9");
_Static_assert(PUFO_STATUS_WEAPON_ID_FIRST == 0x19, "0x8FD80 CMP R8, #0x19");
_Static_assert(PUFO_STATUS_WEAPON_ID_LAST == 0x1E, "0x8FDAC CMP R8, #0x1E");
_Static_assert(PUFO_STATUS_WEAPON_ID_NO_AMMO == 0x1C, "0x8FDF8 MOV R11, #-1 for id 28");
_Static_assert(PUFO_STATUS_TEXT_WEAPON_NAME_FIRST == 1350, "0x8FE3C, unk_F71DE - 0xB38D0 = 1350 * 205");
_Static_assert(PUFO_STATUS_TEXT_SPEED_VALUE == 1222, "0x8FB84, unk_F0B5E - 0xB38D0 = 1222 * 205");
_Static_assert(PUFO_STATUS_TEXT_FUEL_VALUE == 1223, "0x8FC04, unk_F0C2B - 0xB38D0 = 1223 * 205");
_Static_assert(PUFO_STATUS_TEXT_WEAPON1 == 1224, "0x8FCD0, unk_F0CF8 - 0xB38D0 = 1224 * 205");
_Static_assert(PUFO_STATUS_TEXT_WEAPON2 == 1225, "0x8FD50, unk_F0DC5 - 0xB38D0 = 1225 * 205");
_Static_assert(PUFO_STATUS_TEXT_AMMO == 1226, "0x8FE90, unk_F0E92 - 0xB38D0 = 1226 * 205");
_Static_assert(PUFO_STATUS_TEXT_RETURN_BASE == 1227, "0x900B8 record");
_Static_assert(PUFO_STATUS_TEXT_NEW_TARGET == 1228, "0x9010C record");
_Static_assert(PUFO_STATUS_TEXT_PATROL == 1229, "0x90164 record");
_Static_assert(PUFO_STATUS_TEXT_CANCEL_ORDER == 1230, "0x901BC record");

/* The 0x0D trace-lost panel @ 0x90220. */
_Static_assert(PUFO_STATUS_FLAG_INTERCEPT_PAUSE == 0x0D, "0x9022C CMP R0, #0xD");
_Static_assert(PUFO_STATUS_COLOUR_NO_TARGET == 0xFF, "0x902C8 CMP R0, #0xFF");
_Static_assert(PUFO_STATUS_TEXT_TARGET_LOST == 1231, "0x902D0, unk_F1293 - 0xB38D0 = 1231 * 205");
_Static_assert(PUFO_STATUS_TEXT_TRACE_LOST == 1356, "0x90380, unk_F76AC - 0xB38D0 = 1356 * 205");
_Static_assert(PUFO_STATUS_STRATEG_DISPLAY_ID == 0x10, "0x90368 LDR R0, [R0,#0x10]");
_Static_assert(PUFO_STATUS_PAUSE_ANIM_Y == 0x46, "0x9029C MOV R2, #0x46");
_Static_assert(PUFO_STATUS_PAUSE_NH == 0x78, "0x90278 MOV R7, #0x78");
_Static_assert(PUFO_STATUS_PAUSE_Y_LINE1 == 0x5A, "0x903AC MOV R2, #0x5A");
_Static_assert(PUFO_STATUS_PAUSE_Y_LINE2 == 0x76, "0x9042C MOV R2, #0x76");
_Static_assert(PUFO_STATUS_PAUSECTL_Y == 0x9B, "0x90454 MOV R3, #0x9B");
_Static_assert(PUFO_STATUS_PAUSE_Y_LABEL == 0x9C, "0x904BC MOV R2, #0x9C");
/* The 0x10 attacked-city panel @ 0x904d8. */
_Static_assert(PUFO_STATUS_FLAG_CITY_ATTACKED == 0x10, "0x90504 CMP R0, #0x10");
_Static_assert(PUFO_STATUS_TEXT_CITY == 1232, "0x904FC, unk_F1360 - 0xB38D0 = 1232 * 205");
_Static_assert(PUFO_STATUS_TEXT_CITY_ATTACKED == 1233, "0x905BC, unk_F142D - 0xB38D0 = 1233 * 205");
_Static_assert(PUFO_STATUS_TEXT_GO_TO_ZONE == 1234, "0x90680, unk_F14FA - 0xB38D0 = 1234 * 205");
_Static_assert(PUFO_STATUS_CITY_ANIM_Y == 0x46, "0x90518 MOV R2, #0x46");
_Static_assert(PUFO_STATUS_CITY_NH == 0x78, "0x90534 STR R7, the R7 built at 0x904DC MOV R7, #0x78");
_Static_assert(PUFO_STATUS_CITY_Y_LINE1 == 0x56, "0x905AC MOV R2, #0x56");
_Static_assert(PUFO_STATUS_CITY_Y_LINE2 == 0x68, "0x90600 MOV R2, #0x68");
_Static_assert(PUFO_STATUS_CITYCTL_Y_CONFIRM == 0x88, "0x9062C MOV R3, #0x88");
_Static_assert(PUFO_STATUS_CITY_Y_CONFIRM_LBL == 0x88, "0x906C8 MOV R2, #0x88: the label shares its control's row");
_Static_assert(PUFO_STATUS_CITYCTL_Y_ACTION == 0xA0, "0x90664 MOV R3, #0xA0");
_Static_assert(PUFO_STATUS_CITY_Y_ACTION_LBL == 0xA1, "0x90718 MOV R2, #0xA1");
_Static_assert(PUFO_MISSION_SITE_COUNT == 56, "0x86CD8 CMP R0, #0x38, the InitUI mission-site loop");

/* The 0x1B intercept list @ 0x90728. */
_Static_assert(PUFO_STATUS_FLAG_INTERCEPT_LIST == 0x1B, "0x90738 CMP R0, #0x1B");
_Static_assert(PUFO_STATUS_TEXT_CHOOSE_TARGET == 1235, "0x9082C, unk_F15C7 - 0xB38D0 = 1235 * 205");
_Static_assert(PUFO_STATUS_TEXT_FINAL_MISSION == 1236, "0x90914, aTextRec1236 @ 0xF1694 - 0xB38D0 = 1236 * 205");
_Static_assert(PUFO_STATUS_LIST_ANIM_Y == 0x104, "0x9080C MOV R2, #0x104");
_Static_assert(PUFO_STATUS_LIST_NH == 0x28, "0x90824 STR R10, the R10 built at 0x904F4 MOV R10, #0x28");
_Static_assert(PUFO_STATUS_LIST_X_LABEL == 0x28, "0x90864 MOV R1, #0x28: a fixed x with no measure");
_Static_assert(PUFO_STATUS_LIST_Y_LINE == 0x113, "0x90858 / 0x90860 MOV R2,#0x110 then ORR R2,R2,#3");
_Static_assert(PUFO_STATUS_LISTCTL_X == 0x8C, "0x90874 MOV R3, #0x8C");
_Static_assert(PUFO_STATUS_LISTCTL_Y == 0x112, "0x90880 / 0x90888 MOV R3,#0x110 then ORR R3,R3,#2");
_Static_assert(PUFO_STATUS_LIST_CANCEL_ANCHOR == 0xAD, "0x908EC RSB R1,R1,#0xAD");
_Static_assert(PUFO_STATUS_LIST_Y_LABEL == 0x113, "0x908E8 ORR R2,R2,#3");
_Static_assert(PUFO_STATUS_FINAL_MISSION_Y == 0x01, "0x90960 MOV R2, #1");
_Static_assert(PUFO_GEOUI_ROOT_CTLS == 13, "0x907F0 CMP R0, #0xD, the root-control blit loop");

/* The 0x14 map-target panel @ 0x909A8. */
_Static_assert(PUFO_STATUS_FLAG_TARGET_PANEL == 0x14, "0x909B8 CMP R0, #0x14");
_Static_assert(PUFO_STATUS_TEXT_CRASH == 1237, "0x90A0C, unk_F1761 - 0xB38D0 = 1237 * 205");
_Static_assert(PUFO_STATUS_TEXT_MISSION_PREFIX == 1238, "0x909B0, unk_F182E - 0xB38D0 = 1238 * 205");
_Static_assert(PUFO_STATUS_TARGET_ANIM_Y == 0x3C, "0x909C8 MOV R2, #0x3C");
_Static_assert(PUFO_STATUS_TARGET_NH == 0x5A, "0x909EC MOV R3, #0x5A, stored at 0x909F0");
_Static_assert(PUFO_STATUS_TARGET_Y_LINE == 0x46, "0x90B24 / 0x90C3C MOV R2, #0x46");
_Static_assert(PUFO_STATUS_TARGETCTL_Y_OK == 0x73, "0x90B4C MOV R3, #0x73");
_Static_assert(PUFO_STATUS_TARGET_Y_OK_LBL == 0x74, "0x90BA0 MOV R2, #0x74");
_Static_assert(PUFO_STATUS_TARGETCTL_Y_CANCEL == 0x66, "0x90C64 MOV R3, #0x66");
_Static_assert(PUFO_STATUS_TARGET_Y_CANCEL_LBL == 0x67, "0x90CC8 MOV R2, #0x67");
_Static_assert(PUFO_STATUS_TARGETCTL_Y_OK2 == 0x7D, "0x90CF0 MOV R3, #0x7D");
_Static_assert(PUFO_STATUS_TARGET_Y_CANCELLED_LBL == 0x7E, "0x90D40 MOV R2, #0x7E");
_Static_assert(PUFO_PANEL_LINES == 500, "the apLines array the arm indexes at panel + 0xBE4 (0x909AC/0x909B4)");

/* The 0x13 intercept-mission panel @ 0x90D78 and the 0x12 travel panel @ 0x91190. */
_Static_assert(PUFO_STATUS_FLAG_MISSION_SITE == 0x13, "0x90D90 CMP R0, #0x13");
_Static_assert(PUFO_STATUS_PANEL_SEL_ROW == 0xBD8, "0x90D88 MOV R11, #0xBD8: the UiPanel nUnk3032 offset");
_Static_assert(PUFO_STATUS_SITE_Y_LINE2 == 0x56, "0x90ECC / 0x91070 MOV R2, #0x56");
_Static_assert(PUFO_STATUS_SITECTL_Y_OK == 0x73, "0x90EF4 MOV R3, #0x73");
_Static_assert(PUFO_STATUS_SITE_Y_OK_LBL == 0x74, "0x90F44 MOV R2, #0x74");
_Static_assert(PUFO_STATUS_SITECTL_Y_CANCEL == 0x69, "0x91098 MOV R3, #0x69");
_Static_assert(PUFO_STATUS_SITE_Y_CANCEL_LBL == 0x6A, "0x910F8 MOV R2, #0x6A");
_Static_assert(PUFO_STATUS_SITECTL_Y_OK2 == 0x80, "0x91120 MOV R3, #0x80");
_Static_assert(PUFO_STATUS_SITE_Y_CANCELLED_LBL == 0x81, "0x91170 MOV R2, #0x81");
_Static_assert(PUFO_STATUS_MISSION_SITE_STRIDE == 0x18, "0x90E90 / 0x91034 MOV R2, #0x18");
_Static_assert(sizeof(MissionSiteRec) == PUFO_STATUS_MISSION_SITE_STRIDE,
               "the ARM's 0x18 scale is the mission-site record size");
_Static_assert(PUFO_STATUS_FLAG_TRAVEL_PANEL == 0x12, "0x9119C CMP R0, #0x12");
_Static_assert(PUFO_STATUS_FLAG_DESCENT_PANEL == 0x17, "0x91498 CMP R0, #0x17");
_Static_assert(PUFO_STATUS_TEXT_DESCENT == 1239, "unk_F18FB @ 0xF18FB = 0xB38D0 + 1239 * 205");
_Static_assert(PUFO_STATUS_FLAG_DESCENT_TARGET == 0x15, "0x91698 CMP R0, #0x15");
_Static_assert(0xB38D0 + 1239 * PUFO_TEXT_REC_STRIDE == 0xF18FB, "R6 = (unk_F18FB - 0xB38D0) at 0x91490");
_Static_assert(PUFO_STATUS_FLAG_DESCENT_ALT == 0x16, "0x91930 CMP R0, #0x16");
_Static_assert(PUFO_STATUS_FLAG_ALIEN_BASE_PANEL == 0x3C, "0x91B2C CMP R0, #0x3C");
_Static_assert(0xB38D0 + 1445 * PUFO_TEXT_REC_STRIDE == 0xFBDF1, "0x91B74 LDR R0,=(unk_FBDF1 - 0xB38D0)");
_Static_assert(0xB38D0 + 1446 * PUFO_TEXT_REC_STRIDE == 0xFBEBE, "0x91BF4 LDR R0,=(unk_FBEBE - 0xB38D0)");
_Static_assert(1144 * PUFO_TEXT_REC_STRIDE == 0x39418, "0x91C74 MOV R1,#0x39418");
_Static_assert(PUFO_STATUS_BASECTL_Y_CONFIRM == 100, "0x91BE0 MOV R3, #0x64");
_Static_assert(PUFO_STATUS_BASE_Y_CONFIRM_LBL == 100, "0x91C38 MOV R2, #0x64");
/* The chain tail's own numbers: the branch test, and the loop bound of the root-widget blits it shares with
 * the 0x1B arm (0x907F0 there, 0x91D34 here). */
_Static_assert(PUFO_STATUS_SCREEN_ID_GLOBE_HOVER == 2, "0x91CF4 CMP R0, #2");
_Static_assert(PUFO_GEOUI_ROOT_CTLS == 13, "0x91D34 CMP R0, #0xD, the tail's own root loop");
/* The frame end's own numbers (0x944B4..0x94534). */
_Static_assert(PUFO_GEOFRAME_LIST_MODE_SAVING == 4, "0x944C4 CMP R0, #4");
_Static_assert(PUFO_GEOFRAME_LIST_MODE_LOADING == 3, "0x94500 CMP R0, #3");
_Static_assert(PUFO_GEOFRAME_MODAL_WIDTH == 150, "0x944F0 / 0x9452C MOV R1, #0x96");
_Static_assert(PUFO_GEOFRAME_PHASE_SAVE == 0x28, "0x944CC / 0x94508 MOV R0, #0x28");
_Static_assert(PUFO_GEOFRAME_TEXT_SAVING == 1103, "0x944D4 MOV R2,#0x440 then 0x944EC ORR R2,R2,#0xF");
_Static_assert(PUFO_GEOFRAME_TEXT_LOADING == 1102, "0x94510 MOV R2,#0x440 then 0x94528 ORR R2,R2,#0xE");

_Static_assert(PUFO_STATUS_FLAG_MIN == 0x1F, "0x8C798 CMP #0x1E, BLE away");
_Static_assert(PUFO_STATUS_FLAG_MAX == 0x31, "0x8C7AC CMP #0x32, BGE away");

static char   g_abPnText[PN_TEXT_MAX * PUFO_TEXT_REC_STRIDE];
static u16    g_aPnBase[BD_SURF_W * BD_SURF_H];
static u16    g_aPnFrame[BD_SURF_W * BD_SURF_H];   /* a whole saved frame, for arm-vs-arm comparison */
static char   g_szPnMsg[108], g_szPnExtra[108], g_szPnScratch[108];
static UiLineList g_pnLines;
static UiControl g_pnCtlSfx, g_pnCtlOpen, g_pnCtlConfirm, g_pnCtlApply, g_pnCtlA, g_pnCtlB;
static UiPanel  g_pnPanel;
static char     g_szPnPanelLine[64];
static SmapView g_pnView;
static StrategItem g_pnStrat[2];
static void    *g_apPnStrat[2];
static PtrArray g_pnArr;
static i32      g_nPnActionFail, g_nPnActionReason;
static u8       g_bPnMapSel;
static char     g_szPnRadar[PUFO_TEXT_REC_STRIDE], g_szPnHyper[PUFO_TEXT_REC_STRIDE];
static i32      g_nPnSelectClicks, g_nPnClearClicks;
static u8       g_bPnMulti;
static u8       g_bPnAnimStarted;
static i8       g_nPnAnimProgress;
static UiAnimBlitEnv g_pnAnim;
static GfxColorEnv g_pnColor;
static ResearchTopicLive g_pnTopics[PUFO_RESEARCH_TOPIC_COUNT];
static u8       g_abPnActive[PUFO_RESEARCH_TOPIC_COUNT];
static u8       g_nPnFinished;
static i32      g_nPnShown;
static BaseFacStatusEnv g_pnFac;
static GeoscapeStatusPanelEnv g_pnEnv;
static i32      g_nPnBlits;
static PadImage g_aPnPads[9];
static UiControl g_pnCtlRet, g_pnCtlTarg, g_pnCtlPatrol; /* the card's three own controls */
static PadImage  g_pnStripPad;                /* the pad at 0x186BBC: origin and size feed the rect blit */
static u8        g_bPnInterceptDone;          /* bInterceptDone @ 0x18709C byte 0 */
static i32      g_nPnSfxClicks, g_nPnOpenClicks, g_nPnReportClicks, g_nPnApplyClicks;
static i32      g_nPnResolveClicks, g_nPnDismissClicks, g_nPnTargetClicks;
static i32      g_nPnConfirmAltClicks, g_nPnWaypointClicks;
static i32      g_nPnMapTargetClicks, g_nPnConfirmSelClicks;
static i32      g_nPnCancelClicks;
/* The 0x17 arm's roster: two soldiers whose bUnk183 (the craft they are assigned to) the count test
 * reads. 254 is the sentinel Ship_RemoveWithCargo leaves alone, so the default fixture has nobody
 * aboard any craft. */
static Soldier  g_aPnSoldiers[2];
static Soldier *g_apPnSoldiers[2] = { &g_aPnSoldiers[0], &g_aPnSoldiers[1] };
static u16      g_nPnSoldierCount;
static i32      g_nPnBeginCraftClicks;
/* The status panel's own row buffers. Geoscape_InitUI builds this panel with 20 rows
 * (0x86AC0..0x86C30, UiPanel_ctor(pPanel, 20)) and the ctor allocates exactly nLineCount of them,
 * so apLines[20..499] stay NULL -- which is what the 0x14 arm's unbounded index walks into. */
#define PN_PANEL_ROWS 20
static char     g_aPnPanelRow[PN_PANEL_ROWS][PUFO_PANEL_LINE_BYTES];
static bool     g_bPnReady;
static BaseSiteRec g_pnSites[8];
static ObjectRecFull g_pnObj;
static ObjectRecFull *g_apPnObj[1];
static BaseFacFlag g_pnFlags[PUFO_BASEFAC_COUNT];
static Ship     g_pnCraft, g_pnCraft1;
static Ship    *g_apPnCraft[2];
static i32      g_nPnShipIdx;
static u8       g_abPnGrid[PUFO_GRID_SIZE];
static GameTime g_pnTime;
static MissionSiteRec g_pnMission[PUFO_MISSION_SITE_COUNT]; /* aMissionSites @ 0x1865C0 */
/* The 0x1B intercept list reaches further than the other arms: the root block's thirteen widgets,
 * the clock strip's ten digit pads, the strip pad's OWN glyph (PadImage_Blit at 0x907C0) and the
 * final-mission control. The pads carry a 2-byte-wide, 1-byte-high glyph each so a real blit is
 * visible; the palette slot is one no other part of this fixture touches. */
#define PN_TEST_PAL_SLOT 20
static UiControl  g_pnRootCtl[PUFO_GEOUI_ROOT_CTLS];
static UiControl *apPnRootCtl[PUFO_GEOUI_ROOT_CTLS];
static PadImage   g_aPnRootPad[PUFO_GEOUI_ROOT_CTLS];
static PadImage   g_pnClockPad[PUFO_GEOUI_CLOCK_DIGIT_PADS];
static u8         g_abPnClockGlyph[PUFO_GEOUI_CLOCK_DIGIT_PADS][6];
static PadImage  *apPnClockPad[PUFO_GEOUI_CLOCK_DIGIT_PADS];
static u8         g_abPnStripGlyph[4 + 8];
static UiControl  g_pnCtlFinal;              /* pFinalMissionCtl @ 0x186B8C */
static UiControl  g_pnCtlClose;              /* g_pUiCloseCtl @ 0x186B00, the chain tail's control */
static PadImage   g_pnClosePad;
static i32        g_nPnScreenId;             /* nGeoscapeScreen @ 0x186BC4, the tail's branch */
static u8         g_bPnPlacePending;         /* bPlacementPending @ 0x1870AC, the tail's gate */
static u8         g_bPnPendRegion;           /* bPendingRegionId @ 0x159DF4 */
static PadImage   g_pnFinalPad;
static u8         g_bPnUfoCrash, g_bPnSite, g_bPnAlienBase; /* 0x18708C, 0x187090, 0x187098 */
static u8         g_bPnFinalArmed;           /* bFinalMissionCtlArmed @ 0x187068 */
static u16      g_uPnInkHead, g_uPnInkStatus;

static const struct { i32 nRec; const char *psz; } g_aPnRecs[] = {
    { 1191, "A" }, { 1192, "B" }, { 1193, "C" }, { 1194, "D" }, { 1195, "E" },
    { 1196, "F" }, { 1197, "G" }, { 1198, "H" }, { 1199, "I" }, { 1200, "J" },
    { 1201, "K" }, { 1202, "L" }, { 1203, "M" }, { 1204, "N" }, { 1205, "O" },
    { 1206, "P" }, { 1207, "Q" }, { 1208, "R" }, { 1209, "S" },
    { 1334, "a" }, { 1339, "b" }, { 1340, "c" }, { 1341, "d" }, { 1342, "e" }, { 1343, "f" },
    { 1300, "T" }, { 1301, "U" },
    /* The default arm's continuation: every record it copies. Two-character labels so the
    * strstr-based composition checks cannot match by accident. */
    { 406, "A2" }, { 1144, "B2" }, { 1210, "C2" }, { 1211, "D2" }, { 1212, "E2" }, { 1213, "F2" },
    { 1345, "G2" }, { 1346, "H2" }, { 1347, "I2" }, { 1348, "J2" }, { 1359, "K2" }, { 1360, "L2" },
    { 1361, "M2" }, { 1362, "N2" }, { 1363, "O2" }, { 1364, "P2" }, { 1365, "Q2" }, { 1366, "R2" },
    { 1367, "S2" }, { 1368, "T2" }, { 1369, "U2" }, { 1370, "V2" }, { 1371, "W2" }, { 1372, "X2" },
    { 1373, "Y2" }, { 1374, "Z2" }, { 1375, "a3" }, { 1376, "b3" }, { 1377, "c3" }, { 1378, "d3" },
    { 1379, "e3" }, { 1380, "f3" }, { 1381, "g3" }, { 1382, "h3" }, { 1383, "i3" }, { 1384, "j3" },
    { 1385, "k3" }, { 1386, "l3" },
    /* The order questions and the two craft-name records their ship lines build from. */
    { 1127, "m4" }, { 1214, "n4" }, { 1215, "o4" }, { 1216, "p4" }, { 1217, "q4" },
    { 1218, "r4" }, { 1219, "s4" }, { 1220, "t4" }, { 1221, "u4" },
    { 137, "v4" }, { 418, "w4x" },
    /* The craft-detail card. The six weapon names get DIFFERENT lengths so a wrong name index shows
     * up as a row width, and 1230 differs from 1144 although the original ships both as "Отмена". */
    { 1222, "x4" }, { 1223, "y4" }, { 1224, "z4" }, { 1225, "a5" }, { 1226, "b5" },
    { 1227, "c5" }, { 1228, "d5" }, { 1229, "e5" }, { 1230, "fff5" },
    { 1350, "g5" }, { 1351, "hh5" }, { 1352, "iii5" }, { 1353, "j5" }, { 1354, "kkk5" },
    { 1355, "ll5" },
    /* The 0x0D trace-lost panel. Record 1356 keeps the leading space the shipped record has. */
    { 1231, "n5" }, { 1356, " o5" },
    /* The 0x10 attacked-city panel. Three different lengths so a record mix-up shows as a row. */
    { 1232, "q6" }, { 1233, "rr6" }, { 1234, "sss6" },
    /* The 0x1B intercept list. */
    { 1235, "t6" }, { 1236, "uu6" },
    /* The 0x14 map-target panel: the two records bMapSelectionFlag chooses between. */
    { 1237, "w6" }, { 1238, "aaa6" }, { 1239, "d6" },
    /* The 0x3C alien-base panel. 1445 is the message, 1446 the label under the confirm control; the lengths
     * differ so a mix-up shows up as a row width, and neither is a substring of another label here. */
    { 1445, "gggg6" }, { 1446, "h6" },
    /* The frame end's modal dialog: mode 4 draws 1103, mode 3 draws 1102. Different lengths, so a
     * record mix-up is a different frame. */
    { 1102, "t2t2" }, { 1103, "u5u5u5" },
};

static i32 PnAbs(i32 n) { return n < 0 ? -n : n; }

/* The blit callback also records WHICH pad was offered, so an arm that blits the right number of
 * controls but the wrong ones is still caught (the 0x1B arm's thirteen root widgets). */
static const PadImage *g_apnPnBlitPad[32];
/* UiControl_BlitActivePad offers the PAD, not the control, so the pad identifies the control (each of
 * the nine fixture controls has its own). Reading the control's pfnClick at the moment of the blit
 * makes the handler/blit ORDER observable: the 0x14 arm installs its handler first (0x90B60 then
 * 0x90B68) while the 0x0B, 0x0D, 0x10 and 0x1B arms do it the other way round. */
static void (*g_apnPnBlitHandler[32])(void);
/* The ten fixture controls, so a recorded pad can be named. g_pnCtlClose is here because the chain tail's
 * second branch blits *g_pUiCloseCtl (0x91DA8 -> the blit at 0x93058). */
static void (*PnHandlerAtBlit(const PadImage *pPad))(void)
{
    const UiControl *const apMap[10] = { &g_pnCtlSfx, &g_pnCtlOpen, &g_pnCtlConfirm, &g_pnCtlApply,
                                         &g_pnCtlA, &g_pnCtlB, &g_pnCtlRet, &g_pnCtlTarg,
                                         &g_pnCtlPatrol, &g_pnCtlClose };
    for (i32 i = 0; i < 10; ++i) {
        if (apMap[i]->apPads[0] == pPad) {
            return apMap[i]->pfnClick;
        }
    }
    return NULL;
}
static void PnBlit(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    if (g_nPnBlits < (i32)(sizeof(g_apnPnBlitPad) / sizeof(g_apnPnBlitPad[0]))) {
        g_apnPnBlitPad[g_nPnBlits] = pPad;
        g_apnPnBlitHandler[g_nPnBlits] = PnHandlerAtBlit(pPad);
    }
    ++g_nPnBlits;
}
static void PnBlitEx(const PadImage *pPad, void *pUser) { (void)pPad; (void)pUser; }
static void PnClickSfx(void)    { ++g_nPnSfxClicks; }
static void PnClickOpen(void)   { ++g_nPnOpenClicks; }
static void PnClickReport(void) { ++g_nPnReportClicks; }
static void PnClickApply(void)  { ++g_nPnApplyClicks; }
static void PnSelectTarget(void){ ++g_nPnSelectClicks; }
static void PnClearSel(void)    { ++g_nPnClearClicks; }
static void PnResolve(void)     { ++g_nPnResolveClicks; }
static void PnDismiss(void)     { ++g_nPnDismissClicks; }
static void PnConfirmTarget(void){ ++g_nPnTargetClicks; }
static void PnConfirmAlt(void)  { ++g_nPnConfirmAltClicks; }
static void PnWaypoint(void)    { ++g_nPnWaypointClicks; }
static void PnMapTarget(void)   { ++g_nPnMapTargetClicks; }
static void PnConfirmSel(void)  { ++g_nPnConfirmSelClicks; }
static void PnCancelTarget(void){ ++g_nPnCancelClicks; }
static void PnBeginCraftSelect(void){ ++g_nPnBeginCraftClicks; }

static void PnSetText(i32 nRec, const char *psz)
{
    pufo_strncpy(&g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * (size_t)nRec], psz,
                 PUFO_TEXT_REC_STRIDE);
}

/* Every string the panel can draw, emptied. */
static void PnEmptySources(void)
{
    memset(g_abPnText, 0, sizeof(g_abPnText));
    g_pnSites[0].abName[0] = 0;
    g_pnObj.szName[0] = 0;
    for (i32 i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
        g_pnFlags[i].szName[0] = 0;
    }
    g_szPnMsg[0] = g_szPnExtra[0] = g_szPnScratch[0] = 0;
}

static void PnFillSources(void)
{
    for (size_t i = 0; i < sizeof(g_aPnRecs) / sizeof(g_aPnRecs[0]); ++i) {
        PnSetText(g_aPnRecs[i].nRec, g_aPnRecs[i].psz);
    }
    pufo_strncpy(g_pnSites[0].abName, "1", sizeof(g_pnSites[0].abName));
    pufo_strncpy(g_pnObj.szName, "2", sizeof(g_pnObj.szName));
    for (i32 i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
        pufo_strncpy(g_pnFlags[i].szName, "3", sizeof(g_pnFlags[i].szName));
    }
    pufo_strncpy(g_szPnScratch, "4", sizeof(g_szPnScratch));
}

static void PnRun(void)
{
    g_bPnAnimStarted = 0;   /* both runs of a pair must paint the same panel frame */
    g_nPnAnimProgress = 0;
    g_nPnBlits = 0;
    g_szPnRadar[0] = 0;
    g_szPnHyper[0] = 0;
    g_nPnSelectClicks = 0;
    g_nPnClearClicks = 0;
    g_nPnResolveClicks = 0;
    g_nPnDismissClicks = 0;
    g_nPnTargetClicks = 0;
    g_nPnConfirmAltClicks = 0;
    g_nPnWaypointClicks = 0;
    g_nPnMapTargetClicks = 0;
    g_nPnConfirmSelClicks = 0;
    g_nPnCancelClicks = 0;
    memset(g_bdSurf.pPixels, 0, (size_t)BD_SURF_W * (size_t)BD_SURF_H * 2u);
    Geoscape_DrawStatusPanel(&g_pnEnv);
}

/* The baseline (every source empty) then the real frame. */
static void PnCapture(void)
{
    PnEmptySources();
    PnRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    PnFillSources();
    PnRun();
}

/* The same baseline, but the second run has ONE text record missing: the rows that vanish are
 * exactly the rows that record feeds. */
static void PnCaptureMissing(i32 nRec)
{
    PnEmptySources();
    PnRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    PnFillSources();
    g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * (size_t)nRec] = 0;
    PnRun();
}

/* The same pair with the base NAME taken away instead of a text record: aSiteRecs[0].abName @
 * 0x186C40 + 0x1A is the only non-TEXTS string the order arms append. */
static void PnCaptureMissingBaseName(void)
{
    PnEmptySources();
    PnRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    PnFillSources();
    g_pnSites[0].abName[0] = 0;
    PnRun();
}

typedef struct PnDiff {
    i32 nCount;
    i32 nMinX, nMaxX;
    i32 nMinY, nMaxY;
    u16 uInk;
} PnDiff;

static PnDiff PnDiffRect(i32 x0, i32 y0, i32 x1, i32 y1)
{
    PnDiff d;
    u16 aSeen[8];
    i32 aCnt[8];
    i32 nCols = 0;

    d.nCount = 0;
    d.nMinX = BD_SURF_W; d.nMaxX = -1;
    d.nMinY = BD_SURF_H; d.nMaxY = -1;
    d.uInk = 0;
    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            const u16 uNow  = (u16)BdPix(x, y);
            const u16 uBase = g_aPnBase[y * BD_SURF_W + x];
            if (uNow == uBase) {
                continue;
            }
            ++d.nCount;
            if (x < d.nMinX) d.nMinX = x;
            if (x > d.nMaxX) d.nMaxX = x;
            if (y < d.nMinY) d.nMinY = y;
            if (y > d.nMaxY) d.nMaxY = y;
            i32 k;
            for (k = 0; k < nCols; ++k) {
                if (aSeen[k] == uNow) { ++aCnt[k]; break; }
            }
            if (k == nCols && nCols < 8) { aSeen[k] = uNow; aCnt[k] = 1; ++nCols; }
        }
    }
    i32 nBest = 0;
    for (i32 k = 0; k < nCols; ++k) {
        if (aCnt[k] > nBest) { nBest = aCnt[k]; d.uInk = aSeen[k]; }
    }
    return d;
}

/* One expected row. nCentreX is the arm's anchor; bWant says whether the row must be there. */
static void PnCheckRow(i32 nY, i32 x0, i32 x1, i32 nCentreX, u16 uWantInk, bool bWant,
                       const char *pszWhat)
{
    const PnDiff d = PnDiffRect(x0, nY + PN_GLYPH_DY, x1, nY + PN_BAND);

    if (!bWant) {
        CheckEqI32(d.nCount, 0, pszWhat);
        return;
    }
    Check(d.nCount > 0, pszWhat);
    Check(d.nMinY >= nY + PN_GLYPH_DY && d.nMinY <= nY + PN_GLYPH_DY + 4,
          "the row starts at its own y");
    Check(d.nMinY >= 0 && d.nMaxY < BD_SURF_H, "and stays on the surface");
    if (x1 - x0 + 1 > 60) { /* a whole-width band: the anchor is the cluster's own centre */
        Check(PnAbs((d.nMinX + d.nMaxX) / 2 - nCentreX) <= 3, "the row is centred on its anchor");
    }
    CheckEqI32(d.uInk, (i32)uWantInk, "the row is drawn in the colour the arm packed for it");
}

/* The ink a glyph gets after one colour pack, so the two candidate colours are measurable. */
static u16 PnReferenceInk(i32 r1, i32 g1, i32 b1, i32 r2, i32 g2, i32 b2)
{
    memset(g_bdSurf.pPixels, 0, (size_t)BD_SURF_W * (size_t)BD_SURF_H * 2u);
    (void)Gfx_PackRgb565Pair(&g_pnColor, r1, g1, b1, r2, g2, b2);
    Text_DrawString(20, 20, "A");
    u16 aSeen[8];
    i32 aCnt[8];
    i32 nCols = 0;
    for (i32 y = 20; y < 40; ++y) {
        for (i32 x = 20; x < 60; ++x) {
            const u16 u = (u16)BdPix(x, y);
            if (u == 0) {
                continue;
            }
            i32 k;
            for (k = 0; k < nCols; ++k) {
                if (aSeen[k] == u) { ++aCnt[k]; break; }
            }
            if (k == nCols && nCols < 8) { aSeen[k] = u; aCnt[k] = 1; ++nCols; }
        }
    }
    u16 uBest = 0;
    i32 nBest = 0;
    for (i32 k = 0; k < nCols; ++k) {
        if (aCnt[k] > nBest) { nBest = aCnt[k]; uBest = aSeen[k]; }
    }
    return uBest;
}

static void PnFixture(u8 nFlag, u32 nSlot0, u32 nSlot1, u32 nSlot2)
{
    static const char *const apszArchive[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    if (g_bdSurf.pPixels == NULL) {
        SoftSurf_ctor(&g_bdSurf);
        SoftSurf_AllocPixels(&g_bdSurf, BD_SURF_W, BD_SURF_H, PIXMODE_16BPP);
        g_pMainSurface = &g_bdSurf;
        Render_SetTarget(BD_SURF_W, BD_SURF_H, 2, 2, BD_SURF_W * 2, g_bdSurf.pPixels);
    }
    if (!g_bPnReady) {
        g_bPnReady = Asset_OpenArchives(apszArchive) != 0;
        if (g_bPnReady) {
            FontMetrics_Load();
            PufoFont_Init(&g_fontSys);
            g_fontSys.nGlyphBase    = g_fontSys.nTotalSprFrames;
            (void)Font_LoadSpr(&g_fontSys, "font");
            g_fontSys.nBaseBitsBase = g_fontSys.nTotalSprFrames;
            (void)Font_LoadSpr(&g_fontSys, "basebits");
            Font_BuildIndex(&g_fontSys);
            g_bPnReady = g_fontMetrics.bLoaded && g_fontSys.apSprIndex != NULL;
        }
    }

    memset(g_abPnText, 0, sizeof(g_abPnText));
    PnFillSources();
    memset(&g_pnLines, 0, sizeof(g_pnLines));
    UiLineList_SetLine(&g_pnLines, 0, nFlag, nSlot0, nSlot1, nSlot2, 0x77);

    memset(&g_pnTime, 0, sizeof(g_pnTime));
    memset(g_abPnGrid, 0, sizeof(g_abPnGrid));
    memset(g_pnSites, 0, sizeof(g_pnSites));
    memset(g_pnMission, 0, sizeof(g_pnMission));
    memset(&g_pnObj, 0, sizeof(g_pnObj));
    memset(g_pnFlags, 0, sizeof(g_pnFlags));
    memset(&g_pnCraft, 0, sizeof(g_pnCraft));
    memset(&g_pnCraft1, 0, sizeof(g_pnCraft1));
    g_pnCraft.bCraftType = (u8)PUFO_CRAFT_TYPE_COUNT; /* out of range: the name comes back empty */
    g_apPnObj[0]   = &g_pnObj;
    g_apPnCraft[0] = &g_pnCraft;
    g_apPnCraft[1] = &g_pnCraft1;
    g_nPnShipIdx   = 0;
    g_abPnGrid[480] = 0; /* the '&' arm's facility kind for row 18, column 0 */
    memset(g_aPnSoldiers, 0, sizeof(g_aPnSoldiers));
    for (i32 i = 0; i < 2; ++i) {
        g_aPnSoldiers[i].bUnk183 = 254; /* nobody aboard anything until a test says so */
    }
    g_nPnSoldierCount = 2;
    g_nPnBeginCraftClicks = 0;

    memset(g_pnTopics, 0, sizeof(g_pnTopics));
    for (i32 i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        g_pnTopics[i].wGate4 = (u16)(1300 + (i % 2)); /* TEXTS 1300 "T" or 1301 "U" */
    }
    memset(g_abPnActive, 0, sizeof(g_abPnActive));

    memset(&g_pnFac, 0, sizeof(g_pnFac));
    g_pnFac.pTime          = &g_pnTime;
    g_pnFac.abFacilityGrid = g_abPnGrid;
    g_pnFac.apSiteRecs     = g_pnSites;
    g_pnFac.apObjects      = g_apPnObj;
    g_pnFac.nObjectCount   = 1;
    g_pnFac.paFac          = g_pnFlags;
    g_pnFac.aTopics        = g_pnTopics;
    g_pnFac.apCraft        = g_apPnCraft;
    g_pnFac.pTextRecs      = (const u8 *)g_abPnText;
    g_pnFac.pszMsg         = g_szPnMsg;   g_pnFac.cchMsg    = sizeof(g_szPnMsg);
    g_pnFac.pszExtra       = g_szPnExtra; g_pnFac.cchExtra  = sizeof(g_szPnExtra);
    g_pnFac.pszScratch     = g_szPnScratch; g_pnFac.cchScratch = sizeof(g_szPnScratch);

    memset(&g_pnCtlSfx, 0, sizeof(g_pnCtlSfx));
    memset(&g_pnCtlOpen, 0, sizeof(g_pnCtlOpen));
    memset(&g_pnCtlConfirm, 0, sizeof(g_pnCtlConfirm));
    memset(&g_pnCtlApply, 0, sizeof(g_pnCtlApply));
    memset(&g_pnCtlA, 0, sizeof(g_pnCtlA));
    memset(&g_pnCtlB, 0, sizeof(g_pnCtlB));
    memset(&g_pnCtlRet, 0, sizeof(g_pnCtlRet));
    memset(&g_pnCtlTarg, 0, sizeof(g_pnCtlTarg));
    memset(&g_pnCtlPatrol, 0, sizeof(g_pnCtlPatrol));
    g_pnCtlSfx.uWidth     = 20;
    g_pnCtlOpen.uWidth    = 20;
    g_pnCtlConfirm.uWidth = 40;
    g_pnCtlApply.uWidth   = 40;
    g_pnCtlA.uWidth       = 40;
    g_pnCtlB.uWidth       = 40;
    g_pnCtlRet.uWidth     = 40;
    g_pnCtlTarg.uWidth    = 40;
    g_pnCtlPatrol.uWidth  = 40;
    /* UiControl_BlitActivePad paints nothing for a disabled control or one with no pad for its
     * state, so all nine get both. The blit callbacks only count calls; they paint nothing. */
    UiControl *const apPnCtrls[9] = { &g_pnCtlSfx, &g_pnCtlOpen, &g_pnCtlConfirm, &g_pnCtlApply,
                                      &g_pnCtlA, &g_pnCtlB, &g_pnCtlRet, &g_pnCtlTarg,
                                      &g_pnCtlPatrol };
    for (i32 i = 0; i < 9; ++i) {
        UiControl *const pCtl = apPnCtrls[i];
        pCtl->bEnabled  = 1;
        pCtl->nMode     = 0;
        pCtl->nState    = 0;
        pCtl->apPads[0] = &g_aPnPads[i];
    }

    /* The card's rect blit takes x/y/width/height from THIS pad (0x8FA20..0x8FA2C reads +0x0A,
     * +8, +4, +0). The two sizes and the two coordinates are all different so a swapped read shows
     * up as a different painted rectangle. */
    g_pnStripPad.nOriginX = 30;
    g_pnStripPad.nOriginY = 250;
    g_pnStripPad.uWidth   = 100;
    g_pnStripPad.uHeight  = 24;
    g_bPnInterceptDone    = 0;

    /* The 0x1B arm blits the strip pad ITSELF (0x907C0), so the pad needs a real glyph: 4x2 with
     * eight distinct indices 11..18, index 11 being the one at the pad's origin. BdPix(30, 250) then
     * reads the pad's own colour and not the rectangle's 8745 blend -- proof of the blit AND of its
     * order. The indices stay clear of the digit pads' 1..10 in the same palette slot. */
    memset(g_aPalette[PN_TEST_PAL_SLOT], 0, sizeof(g_aPalette[PN_TEST_PAL_SLOT]));
    g_abPnStripGlyph[0] = 4;  g_abPnStripGlyph[1] = 0;   /* u16 width 4 */
    g_abPnStripGlyph[2] = 2;  g_abPnStripGlyph[3] = 0;   /* u16 height 2 */
    for (i32 i = 0; i < 8; ++i) {
        g_abPnStripGlyph[4 + i] = (u8)(11 + i);  /* 11..18, clear of the digit pads' 1..10 */
    }
    g_pnStripPad.pData = g_abPnStripGlyph;
    g_pnStripPad.nMode = PN_TEST_PAL_SLOT;
    for (i32 i = 0; i < 8; ++i) {
        g_aPalette[PN_TEST_PAL_SLOT][11 + i] = (u32)(0x2F00 + i);
    }

    /* The clock strip's ten digit pads, built like the clock suite's own: 2x1, index 1 the colour,
     * index 0 the key. The arm's anim later paints over them, so the clock is pinned through the
     * origins the pads end with, not through pixels. */
    for (i32 i = 0; i < PUFO_GEOUI_CLOCK_DIGIT_PADS; ++i) {
        g_abPnClockGlyph[i][0] = 2;  g_abPnClockGlyph[i][1] = 0;
        g_abPnClockGlyph[i][2] = 1;  g_abPnClockGlyph[i][3] = 0;
        g_abPnClockGlyph[i][4] = (u8)(1 + i);
        g_abPnClockGlyph[i][5] = 0;
        g_aPalette[PN_TEST_PAL_SLOT][1 + i] = (u32)(0x2000 + i);
        memset(&g_pnClockPad[i], 0, sizeof(g_pnClockPad[i]));
        g_pnClockPad[i].pData    = g_abPnClockGlyph[i];
        g_pnClockPad[i].nMode    = PN_TEST_PAL_SLOT;
        g_pnClockPad[i].uWidth   = 2;
        g_pnClockPad[i].uHeight  = 1;
        g_pnClockPad[i].nOriginX = -1;
        g_pnClockPad[i].nOriginY = -1;
        apPnClockPad[i] = &g_pnClockPad[i];
    }

    /* The root block's thirteen widgets, all enabled with a pad for state 0. */
    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        memset(&g_pnRootCtl[i], 0, sizeof(g_pnRootCtl[i]));
        g_pnRootCtl[i].uWidth   = 40;
        g_pnRootCtl[i].bEnabled = 1;
        g_pnRootCtl[i].apPads[0] = &g_aPnRootPad[i];
        apPnRootCtl[i] = &g_pnRootCtl[i];
    }

    memset(&g_pnCtlFinal, 0, sizeof(g_pnCtlFinal));
    g_pnCtlFinal.uWidth   = 40;
    g_pnCtlFinal.bEnabled = 1;
    g_pnCtlFinal.apPads[0] = &g_pnFinalPad;

    /* The chain tail's own state: the screen id picks its branch, bPlacementPending gates the close blit,
     * and bPendingRegionId is where the globe hover's tile lands. The hover env is the globe suite's own
     * (built by GhFixture) and stays NULL until a test asks for the == 2 branch. */
    memset(&g_pnCtlClose, 0, sizeof(g_pnCtlClose));
    g_pnCtlClose.uWidth    = 20;
    g_pnCtlClose.bEnabled  = 1;
    g_pnCtlClose.apPads[0] = &g_pnClosePad;
    g_nPnScreenId       = 0;
    g_bPnPlacePending   = 0;
    g_bPnPendRegion     = 0;
    g_bPnUfoCrash   = 0;
    g_bPnSite       = 0;
    g_bPnAlienBase  = 0;
    g_bPnFinalArmed = 0;

    memset(&g_pnPanel, 0, sizeof(g_pnPanel));
    g_bPnMulti = 0;
    g_nPnFinished = 0;
    g_nPnShown = 0;
    for (i32 i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        if (g_anUfopaediaEntryNum[i] != 0) { g_nPnShown = i; break; }
    }

    /* The default arm's continuation: the map half-extent, the strateg list and its two entries,
     * the two action-fail dwords and the two line buffers they are built in. nMapW 240 is the
     * shipped grid pad's width, which makes nMapW / 2 - 120 exactly zero. */
    memset(&g_pnView, 0, sizeof(g_pnView));
    g_pnView.nMapW = 240;
    g_pnView.nMapH = 240;
    memset(g_pnStrat, 0, sizeof(g_pnStrat));
    g_pnStrat[0].nDisplayId   = 7;
    g_pnStrat[0].bRandA       = 10;   /* 61 * 10 = 610 */
    g_pnStrat[0].bCraftClass  = 6;    /* +0x02: the hyperwave type line reads the SLOT 0 entry */
    g_pnStrat[0].bRace        = 0;    /* +0x70 -> TEXTS[1374] */
    g_pnStrat[0].nMissionSub  = 5;    /* +0x39 -> TEXTS[1386], the non-monotonic arm */
    g_pnStrat[1].bCraftClass  = 2;    /* the size line reads the SLOT 1 entry: base 2700, 1361 */
    g_apPnStrat[0] = &g_pnStrat[0];
    g_apPnStrat[1] = &g_pnStrat[1];
    g_pnArr.nCount  = 2;
    g_pnArr.apItems = g_apPnStrat;
    g_nPnActionFail   = 0;
    g_nPnActionReason = 0;
    g_bPnMapSel       = 0;
    g_szPnRadar[0]    = 0;
    g_szPnHyper[0]    = 0;

    g_pnAnim.pbStarted   = &g_bPnAnimStarted;
    g_pnAnim.pnProgress  = &g_nPnAnimProgress;
    g_pnAnim.nFrameStyle = 0;
    g_pnColor.pnGamma = &g_nGammaOne;
    g_pnColor.nFadeCount   = 0;

    memset(&g_pnEnv, 0, sizeof(g_pnEnv));
    g_pnEnv.pFac             = &g_pnFac;
    g_pnEnv.pLines           = &g_pnLines;
    g_pnEnv.pTextRecs        = g_abPnText;
    g_pnEnv.pAnim            = &g_pnAnim;
    g_pnEnv.pColor           = &g_pnColor;
    g_pnEnv.pCtlSfx          = &g_pnCtlSfx;
    g_pnEnv.pCtlOpen         = &g_pnCtlOpen;
    g_pnEnv.pCtlConfirm      = &g_pnCtlConfirm;
    g_pnEnv.pCtlApply        = &g_pnCtlApply;
    g_pnEnv.pStatusPanel     = &g_pnPanel;
    g_pnEnv.pbMultiSelectPanel = &g_bPnMulti;
    g_pnEnv.pfnCtlSfx        = PnClickSfx;
    g_pnEnv.pfnCtlOpen       = PnClickOpen;
    g_pnEnv.pfnConfirmReport = PnClickReport;
    g_pnEnv.pfnApplyResearch = PnClickApply;
    g_pnEnv.pfnCtlBlit       = PnBlit;
    g_pnEnv.pfnCtlBlitEx     = PnBlitEx;
    g_pnEnv.pCtlBlitUser     = NULL;
    g_pnEnv.aTopics          = g_pnTopics;
    g_pnEnv.abResearchActive = g_abPnActive;
    g_pnEnv.pbFinishedTopic  = &g_nPnFinished;
    g_pnEnv.pnShownTopic     = &g_nPnShown;
    g_pnEnv.pView              = &g_pnView;
    g_pnEnv.pStrategList       = &g_pnArr;
    g_pnEnv.pnActionFail       = &g_nPnActionFail;
    g_pnEnv.pnActionFailReason = &g_nPnActionReason;
    g_pnEnv.pbMapSelectionFlag = &g_bPnMapSel;
    g_pnEnv.pszRadarMsg        = g_szPnRadar;
    g_pnEnv.pszHyper           = g_szPnHyper;
    g_pnEnv.pCtlA              = &g_pnCtlA;
    g_pnEnv.pCtlB              = &g_pnCtlB;
    g_pnEnv.pfnSelectTarget    = PnSelectTarget;
    g_pnEnv.pfnClearSelection  = PnClearSel;
    g_pnEnv.apCraft            = g_apPnCraft;
    g_pnEnv.apSiteRecs         = g_pnSites;
    g_pnEnv.pnSelectedShipIdx  = &g_nPnShipIdx;
    g_pnEnv.pfnResolveInterceptUI  = PnResolve;
    g_pnEnv.pfnDismissSelected     = PnDismiss;
    g_pnEnv.pfnConfirmCraftTarget  = PnConfirmTarget;
    g_pnEnv.pfnConfirmSelectedAlt  = PnConfirmAlt;
    g_pnEnv.pfnSetWaypoint         = PnWaypoint;
    g_pnEnv.pStripPad              = &g_pnStripPad;
    g_pnEnv.pbInterceptDone        = &g_bPnInterceptDone;
    g_pnEnv.pCtlReturnBase         = &g_pnCtlRet;
    g_pnEnv.pCtlNewTarget          = &g_pnCtlTarg;
    g_pnEnv.pCtlPatrol             = &g_pnCtlPatrol;
    g_pnEnv.pfnSelectMapTarget     = PnMapTarget;
    g_pnEnv.pfnConfirmSelected     = PnConfirmSel;
    g_pnEnv.pTime                  = &g_pnTime;
    g_pnEnv.paMissionSites         = g_pnMission;
    g_pnEnv.apRootCtl              = apPnRootCtl;
    g_pnEnv.apClockDigitPads       = apPnClockPad;
    g_pnEnv.pbUfoCrashPanel        = &g_bPnUfoCrash;
    g_pnEnv.pbSitePanel            = &g_bPnSite;
    g_pnEnv.pbAlienBasePanel       = &g_bPnAlienBase;
    g_pnEnv.pFinalMissionCtl       = &g_pnCtlFinal;
    g_pnEnv.pbFinalMissionCtl      = &g_bPnFinalArmed;
    g_pnEnv.pfnCancelTarget        = PnCancelTarget;
    g_pnEnv.apSoldiers             = g_apPnSoldiers;
    g_pnEnv.pnSoldierCount         = &g_nPnSoldierCount;
    g_pnEnv.pfnBeginCraftSelect    = PnBeginCraftSelect;
    g_pnEnv.pnScreenId             = &g_nPnScreenId;
    g_pnEnv.pbPendingRegionId      = &g_bPnPendRegion;
    g_pnEnv.pbPlacementPending     = &g_bPnPlacePending;
    g_pnEnv.pCloseCtl              = &g_pnCtlClose;
    g_pnEnv.pGlobeEnv              = NULL;
}

/* A record as the panel sees it, and the string the panel should have built from two of them. */
static const char *PnRec(i32 nRec)
{
    return &g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * (size_t)nRec];
}

static void PnWantTwo(char *pszDst, i32 nFirst, i32 nSecond)
{
    pufo_strncpy(pszDst, PnRec(nFirst), PUFO_TEXT_REC_STRIDE);
    (void)pufo_strcat(pszDst, PnRec(nSecond), PUFO_TEXT_REC_STRIDE);
}

static void PnCheckStr(const char *pszGot, const char *pszWant, const char *pszWhat)
{
    if (strcmp(pszGot, pszWant) == 0) {
        printf("  ok   %s\n", pszWhat);
        return;
    }
    printf("  FAIL %s: got [%s], want [%s]\n", pszWhat, pszGot, pszWant);
    g_nFailures += 1;
}

static void TestStatusPanelArms(void)
{
    printf("\nGeoscape_DrawStatusPanel @ 0x8c760..0x8e658, the thirteen arms\n");
    PnFixture(' ', 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the shipped font.dat loaded, so the panel labels are real glyphs");
        return;
    }
    g_uPnInkHead   = PnReferenceInk(0xDC, 0x50, 0x50, PUFO_SMAP_CROSSHAIR_INDEX, 0x1E, 0x1E);
    g_uPnInkStatus = PnReferenceInk(0x44, 0xF4, 0xFC, 4, 0x46, 0x4B);
    Check(g_uPnInkHead != g_uPnInkStatus,
          "the head pair and the status pair are different inks, so the pack order is observable");

    /* flag, slot0 (base), slot1 (row), slot2 (column), own record, own y, own ink, foot y */
    static const struct {
        u8 nFlag; i32 nRec; i32 nOwnY; bool bOwnEarly; i32 nFootY; bool bExtra; bool bScratch;
    } aArms[] = {
        { ' ',  1191, PUFO_STATUS_Y_TEXT,  false, PUFO_STATUS_Y_FOOT_TALL, true,  false },
        { '!',  1195, PUFO_STATUS_Y_TEXT,  false, PUFO_STATUS_Y_FOOT_STD,  true,  false },
        { '"',  1196, PUFO_STATUS_Y_TEXT,  true,  PUFO_STATUS_Y_FOOT_STD,  true,  false },
        { '#',  1197, PUFO_STATUS_Y_TEXT,  false, PUFO_STATUS_Y_FOOT_STD,  true,  false },
        { '$',  1198, PUFO_STATUS_Y_TEXT,  false, PUFO_STATUS_Y_FOOT_TALL, false, false },
        { '&',  1196, PUFO_STATUS_Y_TEXT,  true,  PUFO_STATUS_Y_FOOT_STD,  true,  false },
        { '\'', 1204, PUFO_STATUS_Y_TEXT,  false, PUFO_STATUS_Y_FOOT_STD,  true,  true  },
        { '(',  1205, PUFO_STATUS_Y_TEXT,  false, PUFO_STATUS_Y_FOOT_STD,  false, true  },
        { ')',  1206, PUFO_STATUS_Y_TEXT,  true,  PUFO_STATUS_Y_FOOT_STD,  false, false },
        { '*',  1207, PUFO_STATUS_Y_TEXT,  false, PUFO_STATUS_Y_FOOT_STD,  false, false },
        { '+',  1208, PUFO_STATUS_Y_TEXT,  false, PUFO_STATUS_Y_FOOT_STD,  true,  false },
        { ',',  1209, PUFO_STATUS_Y_TEXT,  false, PUFO_STATUS_Y_FOOT_STD,  false, false },
    };

    for (size_t i = 0; i < sizeof(aArms) / sizeof(aArms[0]); ++i) {
        const i32 nRec    = aArms[i].nRec;
        const i32 nFootY  = aArms[i].nFootY;
        const bool bExtra = aArms[i].bExtra;
        const bool bScratch = aArms[i].bScratch;
        const u16 uOwnInk = aArms[i].bOwnEarly ? g_uPnInkStatus : g_uPnInkHead;
        char szWhat[96];

        PnFixture(aArms[i].nFlag, 0, 0, 0);
        PnCapture();

        snprintf(szWhat, sizeof(szWhat), "'%c' draws its own TEXTS record at y=%d",
                 aArms[i].nFlag, PUFO_STATUS_Y_TEXT);
        PnCheckRow(PUFO_STATUS_Y_TEXT, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE, uOwnInk,
                   true, szWhat);

        snprintf(szWhat, sizeof(szWhat), "'%c' draws the second record at y=166 only for ' ' and '$'",
                 aArms[i].nFlag);
        PnCheckRow(PUFO_STATUS_Y_TEXT2, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE, uOwnInk,
                   aArms[i].nFlag == ' ' || aArms[i].nFlag == '$', szWhat);

        /* The 124 row: pszMsg, except ',' which puts pszExtra there and '\'' too. */
        snprintf(szWhat, sizeof(szWhat), "'%c' draws a line at y=124", aArms[i].nFlag);
        PnCheckRow(PUFO_STATUS_Y_MSG, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE, uOwnInk,
                   true, szWhat);

        snprintf(szWhat, sizeof(szWhat),
                 "'%c' draws the 138 line only where the ARM does", aArms[i].nFlag);
        PnCheckRow(PUFO_STATUS_Y_EXTRA, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE, uOwnInk,
                   bExtra || bScratch, szWhat);

        snprintf(szWhat, sizeof(szWhat), "'%c' draws the left footer \"OK\" at y=%d on x=28",
                 aArms[i].nFlag, nFootY);
        PnCheckRow(nFootY, 0, 119, PUFO_STATUS_TEXT_X_LEFT, g_uPnInkStatus, true, szWhat);
        snprintf(szWhat, sizeof(szWhat), "'%c' draws the right footer at y=%d on x=209",
                 aArms[i].nFlag, nFootY);
        PnCheckRow(nFootY, 120, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_RIGHT, g_uPnInkStatus,
                   true, szWhat);

        snprintf(szWhat, sizeof(szWhat), "'%c' draws nothing below its footer", aArms[i].nFlag);
        PnCheckRow(nFootY + PN_BAND + 4, 0, BD_SURF_W - 1, 0, 0, false, szWhat);

        /* The anim frame is painted identically in both runs of a pair, so the diff cannot see it.
         * Measure it against the zeroed surface: nH is 0x68 for ' ' and '$', 0x5A for the rest. */
        const i32 nAnimH = aArms[i].nFlag == ' ' || aArms[i].nFlag == '$'
                         ? PUFO_STATUS_ANIM_H_TALL : PUFO_STATUS_ANIM_H_STD;
        memset(g_aPnBase, 0, sizeof(g_aPnBase));
        PnRun();
        const PnDiff dFrame = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
        snprintf(szWhat, sizeof(szWhat), "'%c' frames the panel from x=0 (Ui_AnimExpandBlit_IfEnabled)",
                 aArms[i].nFlag);
        CheckEqI32(dFrame.nMinX, 0, szWhat);
        CheckEqI32(dFrame.nMinY, PUFO_STATUS_ANIM_Y, "and the frame's top edge is 0x6E");
        snprintf(szWhat, sizeof(szWhat), "its bottom edge is 0x6E + 0x%X + 5", nAnimH);
        CheckEqI32(dFrame.nMaxY, PUFO_STATUS_ANIM_Y + nAnimH + 5, szWhat);

        /* Which record feeds which row: drop one record and see only its row disappear. */
        PnCaptureMissing(nRec);
        snprintf(szWhat, sizeof(szWhat), "'%c' loses its y=152 line when TEXTS[%d] is missing",
                 aArms[i].nFlag, nRec);
        PnCheckRow(PUFO_STATUS_Y_TEXT, 0, BD_SURF_W - 1, 0, 0, false, szWhat);
        snprintf(szWhat, sizeof(szWhat), "while TEXTS[%d] is not the footer: y=%d still draws",
                 nRec, nFootY);
        PnCheckRow(nFootY, 0, 119, PUFO_STATUS_TEXT_X_LEFT, g_uPnInkStatus, true, szWhat);

        if (aArms[i].nFlag == ' ' || aArms[i].nFlag == '$') {
            const i32 nRec2 = aArms[i].nFlag == ' ' ? PUFO_STATUS_TEXT_PRODUCTION
                                                    : PUFO_STATUS_TEXT_DIED;
            PnCaptureMissing(nRec2);
            snprintf(szWhat, sizeof(szWhat),
                     "'%c' loses its y=166 line when TEXTS[%d] is missing", aArms[i].nFlag, nRec2);
            PnCheckRow(PUFO_STATUS_Y_TEXT2, 0, BD_SURF_W - 1, 0, 0, false, szWhat);
            snprintf(szWhat, sizeof(szWhat), "and TEXTS[%d] is not its y=152 line", nRec2);
            PnCheckRow(PUFO_STATUS_Y_TEXT, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
                       uOwnInk, true, szWhat);
        }

        CheckEqI32(g_pnCtlSfx.x, PUFO_STATUS_CTL_X_SFX, "the left control's x is 9");
        CheckEqI32(g_pnCtlOpen.x, PUFO_STATUS_CTL_X_OPEN, "the right control's x is 0xBE");
        CheckEqI32(g_pnCtlSfx.y, aArms[i].nFlag == ' ' || aArms[i].nFlag == '$'
                     ? PUFO_STATUS_CTL_Y_TALL : PUFO_STATUS_CTL_Y_STD,
                     "both controls sit at the arm's y");
        CheckEqI32(g_pnCtlOpen.y, g_pnCtlSfx.y, "the same y on both");
        Check(g_pnCtlSfx.pfnClick == PnClickSfx, "the left control gets the sfx opener");
        Check(g_pnCtlOpen.pfnClick == PnClickOpen, "the right control gets the plain opener");
        /* 0x8E3E4 is the ONE jumptable case that jumps to the chain's tail at 0x91CEC instead of the function's
         * default label: flag 0x2B '+' therefore blits the thirteen root widgets too. The other twelve end at
         * def_8C1B0 and never see them. */
        const i32 nWantBlits = aArms[i].nFlag == '+' ? PUFO_GEOUI_ROOT_CTLS + 2 : 2;
        snprintf(szWhat, sizeof(szWhat), "'%c' blits %d pads: %s", aArms[i].nFlag, nWantBlits,
                 aArms[i].nFlag == '+' ? "its two controls plus the tail's thirteen root widgets"
                                       : "its two controls, and no tail");
        CheckEqI32(g_nPnBlits, nWantBlits, szWhat);
    }
}

static void TestStatusPanelResearch(void)
{
    printf("\nGeoscape_DrawStatusPanel, the '%%' arm @ 0x8d2f4\n");

    PnFixture('%', 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    /* Topic 0 finished and hands over topics 3 and 7; 7 is already past the gate (>= 2) so only
     * 3 is listed, and the counter still advances to 2 -- which is what sizes the apply row. */
    g_pnTopics[0].abActivateTopics[0] = 3;
    g_pnTopics[0].abActivateTopics[1] = 7;
    g_abPnActive[3] = 0;
    g_abPnActive[7] = 2;
    PnCapture();

    PnCheckRow(PUFO_STATUS_Y_RESEARCH_TITLE, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "'%' draws TEXTS[1200] at y=26 in the status colour");
    PnCheckRow(PUFO_STATUS_Y_RESEARCH_MSG, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "'%' draws pszMsg at y=44");
    PnCheckRow(PUFO_STATUS_Y_RESEARCH_REPORT, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "'%' draws the report label TEXTS[1201] at y=75");
    PnCheckRow(PUFO_STATUS_Y_RESEARCH_HEAD, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "'%' draws TEXTS[1202] at y=111");
    PnCheckRow(PUFO_STATUS_Y_RESEARCH_ROW, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "'%' draws the new topic's own name at y=127 + 14*0");
    PnCheckRow(PUFO_STATUS_Y_RESEARCH_ROW + 14, 0, BD_SURF_W - 1, 0, 0, false,
               "the topic already past the gate gets no row at 141");
    PnCheckRow(PUFO_STATUS_Y_RESEARCH_APPLY + 28, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "'%' draws TEXTS[1203] at y=142 + 14*2, the walked count");
    PnCheckRow(PUFO_STATUS_Y_EXTRA, 0, BD_SURF_W - 1, 0, 0, false,
               "'%' draws no 138 line: pszExtra is only written when nFacCol is 1000");
    PnCheckRow(PUFO_STATUS_Y_FOOT_RESEARCH, 0, 119, PUFO_STATUS_TEXT_X_LEFT, g_uPnInkStatus,
               true, "'%' footers at y=188");

    CheckEqI32(g_abPnActive[3], 1, "the listed topic is marked done in byte_160BBC");
    CheckEqI32(g_abPnActive[7], 2, "and the skipped one is left at 2");
    CheckEqI32(g_pnCtlConfirm.x, 120 - (i32)(g_pnCtlConfirm.uWidth >> 1),
               "the confirm control is centred on 120 because the topic has a ufopaedia entry");
    CheckEqI32(g_pnCtlConfirm.y, PUFO_STATUS_Y_RESEARCH_REPORT, "and sits at the report row, 75");
    Check(g_pnCtlConfirm.pfnClick == PnClickReport, "with Ui_OpenBaseInfoAlt as its handler");
    CheckEqI32(g_pnCtlApply.x, PUFO_STATUS_CTL_PARKED,
               "the apply control's x stays parked at 1000 -- only its y is set");
    CheckEqI32(g_pnCtlApply.y, PUFO_STATUS_Y_RESEARCH_APPLY + 28, "and its y is the walked row");
    Check(g_pnCtlApply.pfnClick == PnClickApply, "with Ui_OpenResearch as its handler");
    CheckEqI32(g_pnCtlSfx.y, PUFO_STATUS_CTL_Y_RESEARCH, "the bottom controls sit at 0xB9");
    CheckEqI32(g_nPnBlits, 4, "confirm, apply and the two bottom controls are blitted");

    /* No topic listed: the walk draws nothing and the apply control is never armed. */
    PnFixture('%', 0, 0, 0);
    PnCapture();
    PnCheckRow(PUFO_STATUS_Y_RESEARCH_HEAD, 0, BD_SURF_W - 1, 0, 0, false,
               "with no finished topic there is no 111 row");
    PnCheckRow(PUFO_STATUS_Y_RESEARCH_APPLY, 0, BD_SURF_W - 1, 0, 0, false,
               "and no apply label either");
    CheckEqI32(g_pnCtlApply.y, PUFO_STATUS_CTL_PARKED, "the apply control stays parked");
    Check(g_pnCtlApply.pfnClick == NULL, "and gets no handler");
    /* 0x8D400's BEQ gates CONFIRM on anUfopaediaEntryNum[nResearchTopicShown], not on the walk,
     * so the report control and its label are still there with nothing to research. */
    PnCheckRow(PUFO_STATUS_Y_RESEARCH_REPORT, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "the report label at y=75 is gated by the ufopaedia entry");
    CheckEqI32(g_pnCtlConfirm.y, PUFO_STATUS_Y_RESEARCH_REPORT,
               "and confirm is still armed at 75");
    CheckEqI32(g_nPnBlits, 3, "confirm and the two bottom controls are blitted, apply is not");
}

static void TestStatusPanelDefault(void)
{
    printf("\nGeoscape_DrawStatusPanel, the default arm @ 0x8e5a4\n");

    PnFixture((u8)0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    /* Absolute measurement here: the baseline is the zeroed surface, so every painted pixel shows. The arm
     * is the whole default arm with both calls skipped, so the ONLY ink left is the chain tail's own. */
    memset(g_aPnBase, 0, sizeof(g_aPnBase));
    PnRun();
    const PnDiff dOff = PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1).nCount, 0,
               "flag 0x1E with byte_18709C[4] clear draws NOTHING above row 250: 0x8E5E0's BEQ skips both calls");
    const i32 nTailInk = dOff.nCount;
    Check(nTailInk > 0,
          "and the frame is not empty: the arm still falls through into the shared tail at 0x91CEC, whose "
          "strip-pad blit and clock pads are the only ink left");

    g_bPnMulti = 1;
    g_pnPanel.nFont      = 17;
    g_pnPanel.nLineCount = 3;
    g_pnPanel.nStyle     = 1;
    g_pnPanel.nPosX      = 120;
    g_pnPanel.nUnk7076   = 1;
    g_pnPanel.nPosY      = 100;
    g_pnPanel.apLines[0] = (u8 *)g_szPnPanelLine;
    g_szPnPanelLine[0]   = 0;
    memset(g_aPnBase, 0, sizeof(g_aPnBase));
    PnRun();
    const PnDiff dFrame = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    Check(dFrame.nCount > 0, "with the flag set Ui_AnimExpandBlit paints the frame");
    CheckEqI32(dFrame.nMinX, 0xE, "the frame's left edge is x = 0xE (0x8E620)");
    CheckEqI32(dFrame.nMinY, 100 - 6, "its top edge is nPosY - 6 (0x8E648)");
    CheckEqI32(dFrame.nMaxX, 0xE + 0xD0 + 3, "its right edge is x + nW + 3 with nW = 0xD0 (0x8E640)");
    CheckEqI32(dFrame.nMaxY, 100 - 6 + (17 * 3 + 6) + 5,
               "its bottom edge is y + nH + 5 with nH = nFont * nLineCount + 6 (0x8E62C)");

    /* 0x8E654's UiPanelMsg_Draw: the frame is the same in both runs, so the only differing
     * pixels are the panel's own first row, at UiPanel_DrawText's y formula. */
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    pufo_strncpy(g_szPnPanelLine, "X", sizeof(g_szPnPanelLine));
    PnRun();
    PnCheckRow(100 + (17 - 16) / 2, 0, BD_SURF_W - 1, 120, g_uPnInkStatus, true,
               "0x8E654's UiPanelMsg_Draw draws the panel's first row at nPosY");

    /* 0x1F and 0x31 pass the two gates but fall off the jumptable's far side. */
    g_bPnMulti = 0;
    PnFixture((u8)0x1F, 0, 0, 0);
    PnCapture();
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "flag 0x1F takes the default arm too (SUB #0x20 wraps it below the table)");
    PnFixture((u8)0x31, 0, 0, 0);
    PnCapture();
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and so does 0x31");

    /* The status pair is packed before the gate, so the empty default arm still leaves it in the
     * palette: a glyph drawn right after it takes the status ink, not the head ink. */
    PnFixture((u8)0x1E, 0, 0, 0);
    PnRun();
    memset(g_bdSurf.pPixels, 0, (size_t)BD_SURF_W * (size_t)BD_SURF_H * 2u);
    Text_DrawString(20, 20, "A");
    u16 uAfter = 0;
    for (i32 y = 20; y < 40 && uAfter == 0; ++y) {
        for (i32 x = 20; x < 60; ++x) {
            if ((u16)BdPix(x, y) != 0) { uAfter = (u16)BdPix(x, y); break; }
        }
    }
    CheckEqI32(uAfter, (i32)g_uPnInkStatus,
               "the default arm leaves the status pair packed, not the head pair");
}

/* A left-aligned panel row: presence, its own y, x = 40 and the colour the arm packed. */
static void PnCheckLeftRow(i32 nY, const char *pszWhat)
{
    const PnDiff d = PnDiffRect(0, nY + PN_GLYPH_DY, BD_SURF_W - 1, nY + PN_BAND);

    Check(d.nCount > 0, pszWhat);
    CheckEqI32(d.nMinX, PUFO_STATUS_TEXT_X_LEFT_PANEL, "and it is left-aligned at x = 40");
    CheckEqI32(d.uInk, (i32)g_uPnInkStatus, "and drawn in the status pair the default arm packed");
}

static i32 PnRowWidth(i32 nY)
{
    const PnDiff d = PnDiffRect(0, nY + PN_GLYPH_DY, BD_SURF_W - 1, nY + PN_BAND);

    return d.nMaxX - d.nMinX;
}

static void TestStatusPanelActionFail(void)
{
    printf("\nGeoscape_DrawStatusPanel, the action-fail line @ 0x8e658\n");
    PnFixture(0x1D, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_nPnActionFail   = 1;
    g_nPnActionReason = 0;
    PnCapture();
    PnCheckRow(PUFO_STATUS_Y_FAIL_TEXT, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "flag 0x1D with dwActionFail 1 and reason 0 draws TEXTS[406] at y=158");

    g_nPnActionReason = 1;
    PnCapture();
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "a non-zero dwActionFailReason draws nothing");

    g_nPnActionReason = 0;
    g_nPnActionFail   = 0;
    PnCapture();
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "dwActionFail 0 draws nothing");

    g_nPnActionFail = 2;
    PnCapture();
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and so does dwActionFail 2 -- 0x8E664 tests CMP #1 / BNE, not non-zero");

    /* The gate at 0x8E678 is CMP #0x1E / BGE, so 0x1E itself is NOT below the bound. */
    PnFixture(0x1E, 0, 0, 0);
    g_nPnActionFail   = 1;
    g_nPnActionReason = 0;
    PnCapture();
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1).nCount, 0,
               "flag 0x1E is not below 0x1E, so the line is skipped (only the tail's own ink is left)");

    /* The anim's height is R8, the map half-extent the map chrome left at 0x8C2D0. The shipped
     * 240-wide grid makes it zero, so the frame degenerates to its own top border at y = 0x82. */
    PnFixture(0x1D, 0, 0, 0);
    g_nPnActionFail   = 1;
    g_nPnActionReason = 0;
    PnEmptySources();                  /* the frame alone: the text row would otherwise join it */
    memset(g_aPnBase, 0, sizeof(g_aPnBase));
    PnRun();
    const PnDiff dFrame = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(dFrame.nMinY, 0x82, "its frame's top edge is y = 0x82 (0x8E694)");
    CheckEqI32(dFrame.nMaxY, 0x82 + 5, "and with nMapW / 2 - 120 == 0 the frame is only 6 tall");
    CheckEqI32(dFrame.nMinX, 0x14, "and it starts at x = 0x14 (0x8E698)");
}

static void TestStatusPanelContact(void)
{
    char szWant[PUFO_TEXT_REC_STRIDE];
    char szWhat[96];

    printf("\nGeoscape_DrawStatusPanel, the selected contact @ 0x8e708\n");
    PnFixture(0x0A, 0, 1, 0); /* colour slot 0 -> entry 0, colour slot 1 -> entry 1 */
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_pnStrat[0].bHyperDetect = 0;
    g_bPnMapSel               = 1;
    PnCapture();

    PnCheckRow(PUFO_STATUS_Y_SEL_TITLE, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1210] + itoa(nDisplayId) centred at y=80");
    PnCheckLeftRow(PUFO_STATUS_Y_SEL_SIZE, "the size line draws at y=96");
    PnCheckLeftRow(PUFO_STATUS_Y_SEL_SPEED, "the speed line draws at y=112");
    PnCheckRow(PUFO_STATUS_Y_SEL_LABEL_STD, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "the confirm label draws centred at y=136");
    PnCheckRow(PUFO_STATUS_Y_SEL_CANCEL_STD, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1144] Отмена draws centred at y=161");

    CheckEqI32(g_pnCtlConfirm.y, PUFO_STATUS_SELCTL_Y_STD_CONF, "confirm sits at y=136");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_SELCTL_Y_STD_CLEAR, "pGeoscapeCtlA sits at y=160");
    Check(g_pnCtlConfirm.pfnClick == PnSelectTarget, "confirm gets Smap_OnPointerUp");
    Check(g_pnCtlA.pfnClick == PnClearSel, "and pGeoscapeCtlA gets Smap_ClearSelection");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "and the blits are the tail's thirteen root widgets plus the two controls");

    /* szRadarMsg after the panel is the SPEED line: the size number is left in psz and the speed
     * line appends it whenever nTimer is not positive (0x8EBF4's BLE). */
    pufo_strncpy(szWant, PnRec(PUFO_STATUS_TEXT_SPEED_LABEL), sizeof(szWant));
    (void)pufo_strcat(szWant, "2090", sizeof(szWant)); /* |2700 - 61 * 10| */
    (void)pufo_strcat(szWant, PnRec(PUFO_STATUS_TEXT_KNOTS), sizeof(szWant));
    PnCheckStr(g_szPnRadar, szWant, "the speed buffer is TEXTS[1364] + the size number + TEXTS[1365]");

    g_pnStrat[0].nTimer = 5;
    PnCapture();
    pufo_strncpy(szWant, PnRec(PUFO_STATUS_TEXT_SPEED_LABEL), sizeof(szWant));
    (void)pufo_strcat(szWant, "0", sizeof(szWant));
    (void)pufo_strcat(szWant, PnRec(PUFO_STATUS_TEXT_KNOTS), sizeof(szWant));
    PnCheckStr(g_szPnRadar, szWant, "with nTimer > 0 pszValue replaces it with 0");
    g_pnStrat[0].nTimer = 0;

    /* The size label comes from the SLOT 1 entry's bCraftClass; outside 0..7 the switch jumps past
     * the strcat at 0x8EBAC, so nothing is appended and psz still holds TEXTS[1211], which the
     * speed line then appends. */
    const i32 nWideSize = PnRowWidth(PUFO_STATUS_Y_SEL_SIZE);
    PnCaptureMissing(PUFO_STATUS_TEXT_SIZE_FIRST + 1); /* 1360, what class 2 appends */
    const i32 nSizeLabelOnly = PnRowWidth(PUFO_STATUS_Y_SEL_SIZE);
    Check(nSizeLabelOnly < nWideSize, "TEXTS[1360] is the size label class 2 appends");
    g_pnStrat[1].bCraftClass = 8;
    PnCapture();
    CheckEqI32(PnRowWidth(PUFO_STATUS_Y_SEL_SIZE), nSizeLabelOnly,
               "an out-of-range bCraftClass jumps past the strcat: only TEXTS[1211] is on the row");
    pufo_strncpy(szWant, PnRec(PUFO_STATUS_TEXT_SPEED_LABEL), sizeof(szWant));
    (void)pufo_strcat(szWant, PnRec(PUFO_STATUS_TEXT_SIZE_LABEL), sizeof(szWant));
    (void)pufo_strcat(szWant, PnRec(PUFO_STATUS_TEXT_KNOTS), sizeof(szWant));
    PnCheckStr(g_szPnRadar, szWant, "and the speed line then carries the unoverwritten TEXTS[1211]");
    g_pnStrat[1].bCraftClass = 2;

    /* Which record the confirm label uses follows bMapSelectionFlag. */
    PnCaptureMissing(PUFO_STATUS_TEXT_ATTACK);
    PnCheckRow(PUFO_STATUS_Y_SEL_LABEL_STD, 0, BD_SURF_W - 1, 0, 0, false,
               "with the selection flag set the 136 row IS TEXTS[1212]");
    PnCaptureMissing(PUFO_STATUS_TEXT_CENTRE);
    PnCheckRow(PUFO_STATUS_Y_SEL_LABEL_STD, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "and TEXTS[1213] is not it");
    g_bPnMapSel = 0;
    PnCaptureMissing(PUFO_STATUS_TEXT_CENTRE);
    snprintf(szWhat, sizeof(szWhat), "with the flag clear it IS TEXTS[1213]");
    PnCheckRow(PUFO_STATUS_Y_SEL_LABEL_STD, 0, BD_SURF_W - 1, 0, 0, false, szWhat);
    PnCaptureMissing(PUFO_STATUS_TEXT_ATTACK);
    PnCheckRow(PUFO_STATUS_Y_SEL_LABEL_STD, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "and TEXTS[1212] is not it");
    g_bPnMapSel = 1;

    PnCaptureMissing(PUFO_STATUS_TEXT_CANCEL);
    PnCheckRow(PUFO_STATUS_Y_SEL_CANCEL_STD, 0, BD_SURF_W - 1, 0, 0, false,
               "the 161 row is TEXTS[1144]");
    PnCheckRow(PUFO_STATUS_Y_SEL_LABEL_STD, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "and TEXTS[1144] is not the confirm label");

    /* ---- the hyperwave contact ---- */
    g_pnStrat[0].bHyperDetect = 1;
    PnCapture();
    PnCheckRow(PUFO_STATUS_Y_SEL_HYPER, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1345] alone centred at y=60");
    PnCheckLeftRow(PUFO_STATUS_Y_SEL_TYPE, "the type line draws at y=128");
    PnCheckLeftRow(PUFO_STATUS_Y_SEL_RACE, "the race line draws at y=144");
    PnCheckLeftRow(PUFO_STATUS_Y_SEL_MISSION, "the mission line draws at y=160");
    PnCheckRow(PUFO_STATUS_Y_SEL_LABEL_HYP, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "the confirm label moves to y=180");
    PnCheckRow(PUFO_STATUS_Y_SEL_CANCEL_HYP, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "and Отмена to y=205");
    CheckEqI32(g_pnCtlConfirm.y, PUFO_STATUS_SELCTL_Y_HYP_CONF, "confirm sits at y=180");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_SELCTL_Y_HYP_CLEAR, "and pGeoscapeCtlA at y=204");

    /* szHyper after the panel is the mission line: nMissionSub 5 takes record 1386, not 1386-5. */
    PnWantTwo(szWant, PUFO_STATUS_TEXT_MISSION_LABEL, PUFO_STATUS_TEXT_MISSION_FIRST + 7);
    PnCheckStr(g_szPnHyper, szWant, "nMissionSub 5 takes the LAST mission record (1386)");
    g_pnStrat[0].nMissionSub = 6;
    PnCapture();
    PnWantTwo(szWant, PUFO_STATUS_TEXT_MISSION_LABEL, PUFO_STATUS_TEXT_MISSION_FIRST + 5);
    PnCheckStr(g_szPnHyper, szWant, "while nMissionSub 6 takes 1384 -- the jumptable is not ordered");
    g_pnStrat[0].nMissionSub = 8;
    PnCapture();
    PnCheckStr(g_szPnHyper, PnRec(PUFO_STATUS_TEXT_MISSION_LABEL),
               "and a mission sub outside 0..7 appends no label");
    g_pnStrat[0].nMissionSub = 5;

    /* szHyper is overwritten by every line, so after the call it holds ONLY the mission line. The
     * type and race lines are identified through the record that is missing from the second run:
     * the row shrinks exactly when the missing record is the one that arm appends. */
    const i32 nWideRace = PnRowWidth(PUFO_STATUS_Y_SEL_RACE);
    PnCaptureMissing(PUFO_STATUS_TEXT_RACE_FIRST);
    const i32 nRaceLabelOnly = PnRowWidth(PUFO_STATUS_Y_SEL_RACE);
    Check(nRaceLabelOnly < nWideRace,
          "bRace 0 appends TEXTS[1374] -- the BEQ at 0x8EF98 has its own record");
    PnCaptureMissing(PUFO_STATUS_TEXT_RACE_FIRST + 1);
    CheckEqI32(PnRowWidth(PUFO_STATUS_Y_SEL_RACE), nWideRace,
               "and TEXTS[1375] is not in that row");
    g_pnStrat[0].bRace = 5;
    PnCapture();
    CheckEqI32(PnRowWidth(PUFO_STATUS_Y_SEL_RACE), nRaceLabelOnly,
               "a race above 4 appends no race label -- 0x8EFB8's BNE skips the strcat");
    PnCheckLeftRow(PUFO_STATUS_Y_SEL_RACE, "but the race label itself is still drawn");
    g_pnStrat[0].bRace = 0;

    const i32 nWideType = PnRowWidth(PUFO_STATUS_Y_SEL_TYPE);
    PnCaptureMissing(PUFO_STATUS_TEXT_TYPE_FIRST + 6);
    const i32 nTypeLabelOnly = PnRowWidth(PUFO_STATUS_Y_SEL_TYPE);
    Check(nTypeLabelOnly < nWideType,
          "the type line carries TEXTS[1372] for bCraftClass 6");
    PnCaptureMissing(PUFO_STATUS_TEXT_TYPE_FIRST + 5);
    CheckEqI32(PnRowWidth(PUFO_STATUS_Y_SEL_TYPE), nWideType,
               "and not TEXTS[1371]");
    g_pnStrat[0].bCraftClass = 8;
    PnCapture();
    CheckEqI32(PnRowWidth(PUFO_STATUS_Y_SEL_TYPE), nTypeLabelOnly,
               "the type line reads the SLOT 0 entry's bCraftClass, and 8 appends nothing");
    PnCheckLeftRow(PUFO_STATUS_Y_SEL_TYPE, "with the type label still drawn on its own");
    g_pnStrat[0].bCraftClass = 6;

    /* The hyperwave line is the one record on its row, so removing it empties the row. The type row
     * has TWO records, so removing either one only shortens it -- which is also how the two rows are
     * told apart. */
    const i32 nWideHyper = PnRowWidth(PUFO_STATUS_Y_SEL_HYPER);
    PnCaptureMissing(PUFO_STATUS_TEXT_HYPERWAVE);
    PnCheckRow(PUFO_STATUS_Y_SEL_HYPER, 0, BD_SURF_W - 1, 0, 0, false,
               "the y=60 row is TEXTS[1345] and nothing else");
    CheckEqI32(PnRowWidth(PUFO_STATUS_Y_SEL_TYPE), nWideType,
               "and TEXTS[1345] is not part of the type row");
    PnCaptureMissing(PUFO_STATUS_TEXT_TYPE_LABEL);
    Check(PnRowWidth(PUFO_STATUS_Y_SEL_TYPE) < nWideType,
          "TEXTS[1346] is the type line's label: the row keeps only its class name");
    CheckEqI32(PnRowWidth(PUFO_STATUS_Y_SEL_HYPER), nWideHyper,
               "and it is not part of the hyperwave row");
}

/* The arm drawn in full as the baseline, then the SAME arm with one record emptied: a zero diff
 * proves the arm never reads that record at all. */
static void PnCaptureDropped(i32 nRec)
{
    PnEmptySources();
    PnFillSources();
    PnRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * (size_t)nRec] = 0;
    PnRun();
}

/* The animated frame on its own: the sources are emptied so only the frame is drawn, and the
 * baseline is the zeroed surface rather than a first run. */
static void PnCaptureFrameOnly(void)
{
    PnEmptySources();
    memset(g_aPnBase, 0, sizeof(g_aPnBase));
    PnRun();
}

/* Whole-frame compare, for "these two flag values draw the same panel". The fixture, the run order and the
 * anim reset are the same on both sides, so a difference can only come from the arm that drew. */
static void PnSaveFrame(void)
{
    memcpy(g_aPnFrame, g_bdSurf.pPixels, sizeof(g_aPnFrame));
}
static i32 PnFrameDiff(void)
{
    i32 n = 0;
    for (i32 y = 0; y < BD_SURF_H; ++y) {
        for (i32 x = 0; x < BD_SURF_W; ++x) {
            if ((u16)BdPix(x, y) != g_aPnFrame[y * BD_SURF_W + x]) {
                ++n;
            }
        }
    }
    return n;
}

/* Two crafts that build different names, so a wrong index shows up as a different row. PnFixture
 * zeroes both, so this runs after every fixture call in the test below. */
static void PnSetCrafts(void)
{
    g_pnCraft.bCraftType  = 0;   /* TEXTS[137] + itoa(bType), no separator */
    g_pnCraft.bType       = 3;
    g_pnCraft1.bCraftType = 3;   /* TEXTS[418] + '-' + itoa(bType) */
    g_pnCraft1.bType      = 5;
}

static void TestStatusPanelOrder(void)
{
    printf("\nGeoscape_DrawStatusPanel, the order questions @ 0x8f250\n");
    PnFixture(0x11, 0, 1, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetCrafts();
    g_nPnShipIdx = 0;

    /* ---- 0x11 @ 0x8F270: the base the message names is gone ---- */
    PnCapture();
    PnCheckRow(PUFO_STATUS_Y_ORDER_LINE1, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1214] plus the base name centred at y=70");
    PnCheckRow(PUFO_STATUS_Y_ORDER_LINE2, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1215] alone centred at y=90");
    PnCheckRow(PUFO_STATUS_Y_ORDER_OK_ONE, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "the control label centred at y=116");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_ORDERCTL_Y_ONE, "pGeoscapeCtlA sits at y=115");
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 20, "centred from its own 40-wide pad");
    Check(g_pnCtlA.pfnClick == PnResolve, "with Smap_ResolveInterceptUI installed");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 1,
               "and the blits are the tail's thirteen root widgets plus this arm's one control");
    CheckEqI32(g_szPnRadar[0], 0, "the base arm never calls Ship_FormatDisplayName");

    const i32 nBaseWide = PnRowWidth(PUFO_STATUS_Y_ORDER_LINE1);
    PnCaptureMissingBaseName();
    Check(PnRowWidth(PUFO_STATUS_Y_ORDER_LINE1) < nBaseWide,
          "the rest of the 70 row is aSiteRecs[0].abName @ 0x186C40 + 0x1A");
    PnCaptureMissing(PUFO_STATUS_TEXT_BASE_LABEL);
    Check(PnRowWidth(PUFO_STATUS_Y_ORDER_LINE1) < nBaseWide,
          "and TEXTS[1214] is the front half of it -- the colour value IS the record index");
    const i32 nOkWide = PnRowWidth(PUFO_STATUS_Y_ORDER_OK_ONE);
    PnCaptureMissing(PUFO_STATUS_TEXT_ORDER_OK);
    PnCheckRow(PUFO_STATUS_Y_ORDER_OK_ONE, 0, BD_SURF_W - 1, 0, 0, false,
               "the 116 row is TEXTS[1127] aTextOk and nothing else");
    PnCaptureMissing(PUFO_STATUS_TEXT_OK);
    CheckEqI32(PnRowWidth(PUFO_STATUS_Y_ORDER_OK_ONE), nOkWide,
               "and it is NOT the Latin TEXTS[1193] the 13-arm footer uses");
    PnCaptureDropped(PUFO_STATUS_TEXT_SHIP_LABEL);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "the 0x11 arm never reads the 0x0F/0x0E ship label TEXTS[1216]");
    PnCaptureMissing(PUFO_STATUS_TEXT_BASE_LOST);
    PnCheckRow(PUFO_STATUS_Y_ORDER_LINE2, 0, BD_SURF_W - 1, 0, 0, false,
               "the 90 row is TEXTS[1215] alone");

    PnCaptureFrameOnly();
    PnDiff dFrame = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(dFrame.nMinX, 0xE, "the frame starts at x=14 (0x8F2A4)");
    CheckEqI32(dFrame.nMinY, 0x3C, "and y=60 (0x8F29C)");
    CheckEqI32(dFrame.nMaxX, 0xE + 0xD0 + 3, "with nW 0xD0 it ends at x=225");
    CheckEqI32(dFrame.nMaxY, 0x3C + PUFO_STATUS_ORDER_NH_SHORT + 5,
               "and nH 0x5A (0x8F264) makes it end at y=155");

    /* ---- 0x0F @ 0x8F41C: a craft is short of range and heading home ---- */
    PnFixture(0x0F, 0, 1, 0);       /* slot 0 points at the FIRST craft; the arm must use 0x187054 */
    PnSetCrafts();
    g_nPnShipIdx = 1;               /* the second craft: TEXTS[418] + '-' + itoa(5) */
    PnCapture();
    PnCheckRow(PUFO_STATUS_Y_ORDER_LINE1, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1216] plus the craft name centred at y=70");
    PnCheckRow(PUFO_STATUS_Y_ORDER_LINE2, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1217] centered at y=90");
    Check(g_pnCtlA.pfnClick == PnDismiss, "pGeoscapeCtlA gets Smap_DismissSelected");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_ORDERCTL_Y_ONE, "at the same y=115 as the 0x11 arm");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 1,
               "and the tail's thirteen root widgets plus CtlA are all this arm blits");
    PnCheckStr(g_szPnRadar, "w4x-5",
               "szRadarMsg is left holding TEXTS[418] + the dash + itoa(5), the 0x187054 craft");
    PnCaptureFrameOnly();
    dFrame = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(dFrame.nMaxY, 0x3C + PUFO_STATUS_ORDER_NH_SHORT + 5,
               "the 0x0F frame is the same 0x5A tall as 0x11's");

    const i32 nShipWide = PnRowWidth(PUFO_STATUS_Y_ORDER_LINE1);
    PnCaptureMissing(418);
    Check(PnRowWidth(PUFO_STATUS_Y_ORDER_LINE1) < nShipWide,
          "the name on the 70 row is the second craft's TEXTS[418]");
    PnCaptureDropped(137);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and the first craft's TEXTS[137] is nowhere on it -- the index is 0x187054, not slot 0");
    PnCaptureDropped(PUFO_STATUS_TEXT_BASE_LOST);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and the 0x11 arm's TEXTS[1215] is nowhere here either");

    /* ---- 0x0C @ 0x8F5B4: fly to the chosen point? the only arm with two controls ---- */
    PnFixture(0x0C, 0, 0, 0);
    PnSetCrafts();
    PnCapture();
    PnCheckRow(PUFO_STATUS_Y_ORDER_LINE1, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1218] alone centred at y=70");
    PnCheckRow(PUFO_STATUS_Y_ORDER_OK_TOP, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "the ОК label centred at y=101");
    PnCheckRow(PUFO_STATUS_Y_ORDER_CANCEL, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "and TEXTS[1144] Отмена centred at y=125");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_ORDERCTL_Y_TOP, "pGeoscapeCtlA at y=100 (0x8F678)");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_ORDERCTL_Y_BOTTOM, "pGeoscapeCtlB at y=124 (0x8F68C, R6)");
    CheckEqI32(g_pnCtlB.x, PUFO_STATUS_TEXT_X_CENTRE - 20, "both centred from a 40-wide pad");
    Check(g_pnCtlA.pfnClick == PnConfirmTarget, "CtlA gets Smap_ConfirmCraftTarget");
    Check(g_pnCtlB.pfnClick == PnResolve, "and CtlB gets Smap_ResolveInterceptUI");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "and the blits are the tail's thirteen root widgets plus CtlA and CtlB");
    CheckEqI32(g_szPnRadar[0], 0, "no ship name on this arm either");

    PnCaptureMissing(PUFO_STATUS_TEXT_CANCEL);
    PnCheckRow(PUFO_STATUS_Y_ORDER_CANCEL, 0, BD_SURF_W - 1, 0, 0, false,
               "the 125 row is TEXTS[1144]");
    PnCaptureMissing(PUFO_STATUS_TEXT_ORDER_OK);
    PnCheckRow(PUFO_STATUS_Y_ORDER_OK_TOP, 0, BD_SURF_W - 1, 0, 0, false,
               "and the 101 row is TEXTS[1127], under its own control");
    PnCaptureMissing(PUFO_STATUS_TEXT_FLY_TO_POINT);
    PnCheckRow(PUFO_STATUS_Y_ORDER_LINE1, 0, BD_SURF_W - 1, 0, 0, false,
               "and the 70 row is TEXTS[1218] alone");

    /* ---- 0x0E @ 0x8F768: the point was reached, a new patrol is offered ---- */
    PnFixture(0x0E, 0, 0, 0);
    PnSetCrafts();
    g_nPnShipIdx = 1;               /* 0x187054 points at the second craft... */
    PnCapture();                    /* ... but this arm names the craft by colour slot 0 */
    PnCheckRow(PUFO_STATUS_Y_ORDER_LINE1, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1216] plus the craft name centred at y=70");
    PnCheckRow(PUFO_STATUS_Y_ORDER_0E_LINE2, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1219] centred at y=86");
    PnCheckRow(PUFO_STATUS_Y_ORDER_0E_LINE3, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1220] centred at y=102");
    PnCheckRow(PUFO_STATUS_Y_ORDER_CANCEL, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "the ОК label centred at y=125");
    PnCheckRow(PUFO_STATUS_Y_ORDER_RETARGET, 0, BD_SURF_W - 1, PUFO_STATUS_TEXT_X_CENTRE,
               g_uPnInkStatus, true, "TEXTS[1221] centred at y=150");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_ORDERCTL_Y_BOTTOM, "CtlA sits at y=124 (0x8F90C, R6)");
    CheckEqI32(g_pnCtlConfirm.y, PUFO_STATUS_ORDERCTL_Y_0E_CONF, "the confirm control at y=150");
    Check(g_pnCtlA.pfnClick == PnConfirmAlt, "CtlA gets Smap_ConfirmSelectedAlt");
    Check(g_pnCtlConfirm.pfnClick == PnWaypoint, "and confirm gets Smap_SetWaypoint");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "both blitted, before the handlers were installed, then the tail's thirteen root widgets");
    PnCheckStr(g_szPnRadar, "v43",
               "the name is TEXTS[137] + itoa(3): colour slot 0 picks the craft here, not 0x187054");

    const i32 n0EWide = PnRowWidth(PUFO_STATUS_Y_ORDER_LINE1);
    PnCaptureMissing(137);
    Check(PnRowWidth(PUFO_STATUS_Y_ORDER_LINE1) < n0EWide, "the 70 row carries TEXTS[137]");
    PnCaptureMissing(418);
    CheckEqI32(PnRowWidth(PUFO_STATUS_Y_ORDER_LINE1), n0EWide, "and not TEXTS[418]");
    PnCaptureMissing(PUFO_STATUS_TEXT_RETARGET);
    PnCheckRow(PUFO_STATUS_Y_ORDER_RETARGET, 0, BD_SURF_W - 1, 0, 0, false,
               "the 150 row is TEXTS[1221], under the confirm control");

    PnCaptureFrameOnly();
    dFrame = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(dFrame.nMinY, 0x3C, "same frame origin as the other three arms");
    CheckEqI32(dFrame.nMaxY, 0x3C + PUFO_STATUS_ORDER_NH_TALL + 5,
               "but nH 0x78 (0x8F78C) makes this one end at y=185, not 155");
}

/* The blend SoftSurf_BlitSrcRect @ 0xa4450 writes for the card's strip rect over a cleared surface:
 * the colour is (0x3C >> 3) << 11 | (0x78 >> 2) << 5 | (0x82 >> 3) = 0x3BD0, blended at key 0x96 with
 * 255 - 0x96 against zero, field by field: 9 + 32 * 17 + (4 << 11). */
#define PN_STRIP_INK 8745

/* The card's craft: the numbers and mounts its lines print. PnFixture zeroes both crafts, so this
 * runs after every fixture call below. The card indexes CRAFT 1 because slot 0 is 1 in every case. */
static void PnSetCardCrafts(void)
{
    PnSetCrafts();                          /* craft 1 -> TEXTS[418] + '-' + "5" */
    g_pnCraft1.bHomeBase    = 3;            /* site 3, so the record index is the field and not a 0 */
    pufo_strncpy(g_pnSites[3].abName, "3", sizeof(g_pnSites[3].abName));
    g_pnCraft1.uFuelCap     = 90;
    g_pnCraft1.uRangeRemain = 30;
    g_pnCraft1.uRangeMax    = 40;           /* 100 * 30 / 40 = 75 */
    g_pnCraft1.bWeaponL     = PUFO_STATUS_WEAPON_ID_FIRST;
    g_pnCraft1.bWeaponR     = PUFO_STATUS_WEAPON_ID_FIRST; /* equal mounts: one shared pool */
    g_pnCraft1.bAmmo25      = 12;
    g_pnCraft1.bAmmo26      = 12;
    g_pnCraft1.bContactMsg  = 0;
}

/* Mission sites 0, 4, 5 and 6 with four different names (a fixture choice -- a real InitUI leaves
 * every name empty, because records 1655..1710 do not exist in lang.dat): the ARM indexes the table as
 * index * 0x18 (0x90574) and takes the name at +4 (0x90584), so a wrong stride or a wrong field
 * offset reads one of the neighbours instead of the wanted name. The names are constant across a
 * PnCapture pair -- they are not text records -- which is why the city row is checked with PnRowIs
 * and not with a dropped-source diff. */
static void PnSetMissionSites(void)
{
    g_pnMission[0].x = 0;      /* the four x values are the fixture's way of telling the slots apart */
    g_pnMission[4].x = 0x1111;
    g_pnMission[5].x = 0x2222;
    g_pnMission[6].x = 0x3333;
    pufo_strncpy(g_pnMission[0].szName, "z6",  sizeof(g_pnMission[0].szName));
    pufo_strncpy(g_pnMission[4].szName, "p6",  sizeof(g_pnMission[4].szName));
    pufo_strncpy(g_pnMission[5].szName, "m6",  sizeof(g_pnMission[5].szName));
    pufo_strncpy(g_pnMission[6].szName, "nn6", sizeof(g_pnMission[6].szName));
}

/* The negative half of the mission-site bound needs a NON-ZERO record in front of the table: with the
 * plain global array an unguarded paMissionSites[-1] reads zeros and looks exactly like the guard, so the
 * env's table is pointed one slot into a longer buffer whose slot 0 carries a known name. */
static MissionSiteRec g_aPnSiteBuf[PUFO_MISSION_SITE_COUNT + 1];

static void PnShiftSiteTableRight(void)
{
    memcpy(g_aPnSiteBuf + 1, g_pnMission, sizeof(g_pnMission));
    memset(&g_aPnSiteBuf[0], 0, sizeof(g_aPnSiteBuf[0]));
    pufo_strncpy(g_aPnSiteBuf[0].szName, "bb6", sizeof(g_aPnSiteBuf[0].szName));
    g_pnEnv.paMissionSites = &g_aPnSiteBuf[1];
}

/* PN_ROW_PIX pixels: one row band, x from 20 to 219. The window keeps the frame's two border columns
 * at 14 and 225 out -- Ui_AnimExpandBlit fills the whole panel body, not just its outline (the BLT at
 * pufo_panel.c:567), so the body colour has to come from a run that draws the body with no text. */
#define PN_ROW_W   200
#define PN_ROW_PIX ((PN_BAND - PN_GLYPH_DY + 1) * PN_ROW_W)

/* The card's row at y, compared as INK against a reference draw of the string the card should have
 * composed there. Ui_AnimExpandBlit fills the whole panel body (pufo_panel.c:567), so ink is decided
 * against that body colour -- read at x=20, far left of any row centred on 120 -- and not against a
 * second run of the card: the card's numbers come from the craft fields, not from TEXTS, so a run with
 * every record emptied still draws them. The centring is the original's own (0x90204..0x90218: the
 * measured width halved and subtracted from 120), so a wrong digit also moves the whole row. The
 * expected string comes from the test, never from the port. */
static void PnRowIs(i32 nY, const char *pszWant, const char *pszWhat)
{
    static u16 aCard[PN_ROW_PIX];
    const i32 nY0 = nY + PN_GLYPH_DY, nY1 = nY + PN_BAND;
    i32 nDiff = 0;

    PnCapture();
    const u16 uBody = (u16)BdPix(20, nY + 8);
    for (i32 y = nY0; y <= nY1; ++y) {
        for (i32 x = 20; x < 20 + PN_ROW_W; ++x) {
            aCard[(y - nY0) * PN_ROW_W + (x - 20)] = (u16)BdPix(x, y);
        }
    }
    memset(g_bdSurf.pPixels, 0, (size_t)BD_SURF_W * (size_t)BD_SURF_H * 2u);
    Text_DrawString(PUFO_STATUS_TEXT_X_CENTRE - Text_MeasureWidth(pszWant) / 2, nY, pszWant);

    for (i32 i = 0; i < PN_ROW_PIX; ++i) {
        const u16 uRef = (u16)BdPix(20 + i % PN_ROW_W, nY0 + i / PN_ROW_W);
        if ((aCard[i] != uBody) != (uRef != 0)) {
            ++nDiff;
        }
    }
    if (nDiff == 0) {
        printf("  ok   %s\n", pszWhat);
        return;
    }
    printf("  FAIL %s: %d ink pixels differ from a reference draw of [%s]\n", pszWhat, nDiff, pszWant);
    g_nFailures += 1;
}

static void TestStatusPanelDetail(void)
{
    printf("\nGeoscape_DrawStatusPanel, the craft-detail card @ 0x8f9e4\n");
    PnFixture(0x0B, 1, 0, 0); /* slot 0 is 1 while 0x187054 still holds 0 */
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetCardCrafts();

    /* ---- 1. the strip pad re-blit and the card's own frame ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, 240);
    CheckEqI32(d.nMinX, 0xE, "the frame starts at x=14 (0x8FA6C)");
    CheckEqI32(d.nMinY, 5, "and y=5 (0x8FA64)");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x8FA5C) puts the right border at 225");
    CheckEqI32(d.nMaxY, 5 + PUFO_STATUS_DETAIL_NH + 5, "nH 0xDC (0x8FA50) puts the bottom at 230");
    /* The measured rect is the pad's own 100x24 exactly: the chain tail that follows this arm paints the
     * clock rows down to y 305 and those sit outside this window, while its pad blit lands INSIDE the rect
     * (the same 0x186BBC pad, at its own origin) and so cannot change a pixel count that is already full. */
    d = PnDiffRect(30, 250, 129, 273);
    CheckEqI32(d.nCount, 100 * 24, "the pad at 0x186BBC is re-blitted whole, 100x24 (0x8FA20..0x8FA30)");
    CheckEqI32(d.nMinX, 30, "at its nOriginX");
    CheckEqI32(d.nMinY, 250, "its nOriginY");
    CheckEqI32(d.nMaxX, 129, "uWidth from LDRH [R0,#8]");
    CheckEqI32(d.nMaxY, 273, "uHeight from LDRH [R0,#0xA], the two reads are not swapped");
    CheckEqI32(d.uInk, PN_STRIP_INK, "blended from nIdx 0x3C, mid 0x78, low 0x82 at key 0x96 (0x8F9F8..0x8FA1C)");
    CheckEqI32(BdPix(30, 250), 0x2F00,
               "and the tail's own PadImage_Blit (0x91CFC / 0x91D04) leaves the pad's glyph over the rect's corner");

    /* ---- 2. the four value lines ---- */
    PnCapture();
    CheckEqI32(g_nPnShipIdx, 1, "0x8FA90 stores line 0's colour slot 0 into 0x187054");
    PnCheckStr(g_szPnRadar, "w4x-5", "and the craft it then names is the SECOND one");
    PnRowIs(PUFO_STATUS_Y_DETAIL_SHIP, "p4w4x-5", "y=15 is TEXTS[1216] + Ship_FormatDisplayName");
    PnRowIs(PUFO_STATUS_Y_DETAIL_BASE, "n43", "y=30 is TEXTS[1214] + aSiteRecs[Ship.bHomeBase] + 0x1A, the home base is 3");
    PnRowIs(PUFO_STATUS_Y_DETAIL_SPEED, "x490", "y=45 is TEXTS[1222] + itoa(LDRH [Ship,#0x10])");
    PnRowIs(PUFO_STATUS_Y_DETAIL_FUEL, "y475%", "y=60 is TEXTS[1223] + 100*uRangeRemain/uRangeMax + a percent sign");

    /* The multiply comes first in the original (0x8FC50 MUL R1, R3, R2 with R2 = 0x64). With
     * uRangeRemain 1 of 40 the original would divide by zero; the port's guard draws 0 and this is
     * the recorded deviation. */
    g_pnCraft1.uRangeRemain = 1;
    PnCapture();
    PnRowIs(PUFO_STATUS_Y_DETAIL_FUEL, "y42%", "the 100 is applied before the divide: 1 of 40 is 2%");
    g_pnCraft1.uRangeRemain = 0;
    PnCapture();
    PnRowIs(PUFO_STATUS_Y_DETAIL_FUEL, "y40%", "and an empty tank reads 0%, where the original divides by zero");
    g_pnCraft1.uRangeRemain = 30;

    /* ---- 3. the two weapon slots and the shared pool ---- */
    PnCapture();
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0, "z4g5", "y=75 is TEXTS[1224] + TEXTS[1350], the LDRSB [Ship,#0x22] id");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0, "b56", "y=90 rounds the shared 12 UP for slot 0 (0x8FEE4, 0x8FF04, 0x8FF0C)");
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0 + PUFO_STATUS_DETAIL_ROW_STEP, "a5g5",
            "y=105 is TEXTS[1225] + record 1350 again: the LDRSB [Ship,#0x23] id is the same 25");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0 + PUFO_STATUS_DETAIL_ROW_STEP, "b56",
            "y=120 rounds it DOWN for slot 1 (0x8FEE8 BNE loc_8FF28)");

    /* The very same craft with UNEQUAL mounts: 0x8FEDC BNE loc_8FF34 skips the halving altogether,
     * so both rows keep the full 12. */
    g_pnCraft1.bWeaponR = PUFO_STATUS_WEAPON_ID_FIRST + 1;
    g_pnCraft1.bAmmo26  = 14;               /* id 26 reads +0x3E, id 25 reads +0x3D */
    PnCapture();
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0, "b512", "unequal mounts keep the left count whole (0x8FEDC BNE)");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0 + PUFO_STATUS_DETAIL_ROW_STEP, "b514",
            "and the right one whole, read from its own ammo byte 0x3E");
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0 + PUFO_STATUS_DETAIL_ROW_STEP, "a5hh5",
            "id 26 reads record 1351, not 1350");

    /* 3 of 12 is not the interesting case; 2 and 3 are the ones that take the round-up arm. */
    g_pnCraft1.bWeaponR = PUFO_STATUS_WEAPON_ID_FIRST;
    g_pnCraft1.bAmmo25  = 3;
    PnCapture();
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0, "b52", "a pool of 3 gives slot 0 the round-up half, 2");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0 + PUFO_STATUS_DETAIL_ROW_STEP, "b51", "and slot 1 the round-down half, 1");

    /* id 28 is the no-ammo id: -1 exits at 0x8FEBE BEFORE the shared-pool test is even reached. */
    g_pnCraft1.bWeaponL = PUFO_STATUS_WEAPON_ID_NO_AMMO;
    g_pnCraft1.bWeaponR = PUFO_STATUS_WEAPON_ID_NO_AMMO;
    PnCapture();
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0, "z4j5", "id 28 still names itself from record 1353");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0, "b5N.A.", "and prints the aNA literal, not a halved -1");
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0 + PUFO_STATUS_DETAIL_ROW_STEP, "a5j5", "on both slots");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0 + PUFO_STATUS_DETAIL_ROW_STEP, "b5N.A.", "on both slots");

    /* An id outside 25..30 that is not zero: no name is appended and the count stays the 0 the arm
     * starts with at 0x8FCBC. */
    g_pnCraft1.bWeaponL = 7;
    g_pnCraft1.bWeaponR = 8;
    PnCapture();
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0, "z4", "an id outside 25..30 gets no name (0x8FD80..0x8FDAC)");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0, "b50", "and its count is the 0 set at 0x8FCBC");
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0 + PUFO_STATUS_DETAIL_ROW_STEP, "a5", "same on the right slot");

    /* Ids 29 and 30 name themselves from records 1354 and 1355 and read the ammo bytes 0x40 and 0x41
     * (0x8FDDC and 0x8FDC0). */
    g_pnCraft1.bWeaponL = PUFO_STATUS_WEAPON_ID_FIRST + 4;  /* 29 */
    g_pnCraft1.bWeaponR = PUFO_STATUS_WEAPON_ID_LAST;       /* 30 */
    g_pnCraft1.bAmmo29  = 8;
    g_pnCraft1.bAmmo30  = 5;
    PnCapture();
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0, "z4kkk5", "id 29 takes the name from record 1354");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0, "b58", "and the count from the ammo byte 0x40");
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0 + PUFO_STATUS_DETAIL_ROW_STEP, "a5ll5", "id 30 from record 1355");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0 + PUFO_STATUS_DETAIL_ROW_STEP, "b55", "and the count from 0x41");

    /* A zero id skips the whole slot. */
    g_pnCraft1.bWeaponL = PUFO_STATUS_WEAPON_ID_FIRST;
    g_pnCraft1.bWeaponR = 0;
    g_pnCraft1.bAmmo25  = 12;
    PnCapture();
    PnRowIs(PUFO_STATUS_DETAIL_Y_WEAPON0, "z4g5", "the left mount still draws its weapon row");
    PnRowIs(PUFO_STATUS_DETAIL_Y_AMMO0, "b512", "and its ammo row, unhalved because the mounts differ");
    PnCapture();                          /* PnRowIs leaves a reference draw on the surface */
    PnCheckRow(PUFO_STATUS_DETAIL_Y_WEAPON0 + PUFO_STATUS_DETAIL_ROW_STEP, 20, 219, 0, 0, false,
               "a zero id draws no weapon row at all (0x8FE50 BEQ loc_8FF9C)");
    PnCheckRow(PUFO_STATUS_DETAIL_Y_AMMO0 + PUFO_STATUS_DETAIL_ROW_STEP, 20, 219, 0, 0, false,
               "and no ammo row for that slot either");

    /* ---- 4. the four controls, their labels and their handlers ---- */
    PnCapture();
    Check(g_pnCtlRet.x == PUFO_STATUS_TEXT_X_CENTRE - 20 && g_pnCtlTarg.x == g_pnCtlRet.x
          && g_pnCtlPatrol.x == g_pnCtlRet.x && g_pnCtlB.x == g_pnCtlRet.x,
          "all four controls centre on 120 from their own 40-wide pads (0x9000C..0x90058)");
    CheckEqI32(g_pnCtlRet.y, PUFO_STATUS_DETAILCTL_Y_RETURN, "the return control sits at y=140");
    CheckEqI32(g_pnCtlTarg.y, PUFO_STATUS_DETAILCTL_Y_TARGET, "the new-target control at 160");
    CheckEqI32(g_pnCtlPatrol.y, PUFO_STATUS_DETAILCTL_Y_PATROL, "the patrol control at 180");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_DETAILCTL_Y_CANCEL, "and the cancel control at 200");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 5,
               "eighteen pad blits: the tail's thirteen root widgets, then this arm's four controls plus the "
               "cancel control's extra one at 0x900B4");
    PnRowIs(PUFO_STATUS_DETAILCTL_Y_RETURN, "c5", "TEXTS[1227] centred under the return control at y=140");
    PnRowIs(PUFO_STATUS_DETAILCTL_Y_TARGET, "d5", "TEXTS[1228] at 160");
    PnRowIs(PUFO_STATUS_DETAILCTL_Y_PATROL, "e5", "TEXTS[1229] at 180");
    PnRowIs(PUFO_STATUS_Y_DETAIL_CANCEL_LBL, "fff5", "TEXTS[1230] at 201, only while no intercept is done");

    Check(g_pnCtlRet.pfnClick == PnDismiss, "no contact message: Smap_DismissSelected on the return control");
    Check(g_pnCtlTarg.pfnClick == PnMapTarget,
          "the new-target control ends on Smap_SelectMapTarget, which overwrites the Dismiss store (0x8FFE8..0x8FFEC)");
    Check(g_pnCtlPatrol.pfnClick == PnConfirmSel, "the patrol control gets Smap_ConfirmSelected");
    Check(g_pnCtlB.pfnClick == PnResolve, "the cancel control gets Smap_ResolveInterceptUI either way (0x8FFFC)");

    g_pnCraft1.bContactMsg = 1;
    PnRun();
    Check(g_pnCtlRet.pfnClick == PnResolve && g_pnCtlTarg.pfnClick == PnResolve
          && g_pnCtlPatrol.pfnClick == PnResolve && g_pnCtlB.pfnClick == PnResolve,
          "with Ship.bContactMsg @ +0x3C set all four take Smap_ResolveInterceptUI (0x8FFB8)");
    g_pnCraft1.bContactMsg = 0;

    /* ---- 5. bInterceptDone @ 0x18709C byte 0 ---- */
    g_bPnInterceptDone = 1;
    PnCapture();
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 4,
               "with the byte set the cancel control is blitted once (0x900AC..0x900B4), thirteen root blits after");
    PnCheckRow(PUFO_STATUS_Y_DETAIL_CANCEL_LBL, 20, 219, 0, 0, false,
               "and the 'Отмена' label under it is not drawn either (0x901E8 BNE loc_90220)");
    g_bPnInterceptDone = 0;

    /* ---- 6. records the card never reads ---- */
    PnCaptureDropped(PUFO_STATUS_TEXT_ORDER_OK);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, 240).nCount, 0,
               "the card never reads TEXTS[1127] 'ОК' that the order questions use");
    PnCaptureDropped(PUFO_STATUS_TEXT_CANCEL);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, 240).nCount, 0,
               "nor TEXTS[1144] 'Отмена' -- its own cancel label is the separate record 1230");
    PnCaptureDropped(PUFO_STATUS_TEXT_BASE_LOST);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, 240).nCount, 0, "nor any of the order-question lines");
}

static void TestStatusPanelPause(void)
{
    printf("\nGeoscape_DrawStatusPanel, the trace-lost panel @ 0x90220\n");
    PnFixture(0x0D, 0, 1, 0); /* colour slot 0 -> strateg entry 0, slot 1 -> craft 1 */
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetCrafts();
    g_pnStrat[0].nDisplayId = 41;     /* the +0x10 dword the arm prints (0x90368) */
    g_nPnShipIdx            = 0;      /* the global points at the FIRST craft; the arm must not use it */

    /* ---- 1. the pause, and that this arm is the only one in the chain that does it ---- */
    CheckEqI32(g_pnTime.bPaused, 0, "the fixture starts with the clock running");
    PnRun();
    CheckEqI32(g_pnTime.bPaused, 1, "0x90234..0x90238 GameTime_Pause stops the clock");
    PnFixture(0x0B, 1, 0, 0);
    PnRun();
    CheckEqI32(g_pnTime.bPaused, 0, "the 0x0B card leaves the clock alone, so the pause is 0x0D's own");
    PnFixture(0x0D, 0, 1, 0);
    PnSetCrafts();
    g_pnStrat[0].nDisplayId = 41;
    g_nPnShipIdx            = 0;

    /* ---- 2. the strip rectangle and the panel body ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, 240);
    CheckEqI32(d.nMinX, 0xE, "the body starts at x=14 (0x902A4)");
    CheckEqI32(d.nMinY, PUFO_STATUS_PAUSE_ANIM_Y, "and y=70 (0x9029C)");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x902AC) puts the right border at 225");
    CheckEqI32(d.nMaxY, PUFO_STATUS_PAUSE_ANIM_Y + PUFO_STATUS_PAUSE_NH + 5,
               "nH 0x78 (0x90278) puts the bottom at 195, not 230 like the card");
    d = PnDiffRect(30, 250, 129, 273);
    CheckEqI32(d.nCount, 100 * 24, "the same whole strip-pad rectangle as the 0x0B card (0x90260..0x90274)");
    CheckEqI32(d.uInk, PN_STRIP_INK, "blended with the same nIdx 0x3C, mid 0x78, low 0x82, key 0x96");

    /* ---- 3. both lines with a target selected ---- */
    PnCapture();
    PnRowIs(PUFO_STATUS_PAUSE_Y_LINE1, "C241 o5",
            "y=90 is TEXTS[1210] + itoa(nDisplayId) + TEXTS[1356] with its own leading space");
    PnRowIs(PUFO_STATUS_PAUSE_Y_LINE2, "p4w4x-5", "y=118 is TEXTS[1216] + the craft named by colour SLOT 1");
    PnCheckStr(g_szPnRadar, "w4x-5", "and the name it built is the second craft's");
    CheckEqI32(g_nPnShipIdx, 0, "0x187054 is not read here -- 0x903F4 takes slot 1 instead");

    /* ---- 4. colour slot 0 as the "no target" sentinel ---- */
    PnFixture(0x0D, 0xFF, 1, 0);
    PnSetCrafts();
    PnCapture();
    PnRowIs(PUFO_STATUS_PAUSE_Y_LINE1, "n5", "slot 0 == 0xFF draws TEXTS[1231] alone (0x902C8 / 0x902D0)");
    PnRowIs(PUFO_STATUS_PAUSE_Y_LINE2, "p4w4x-5", "and leaves the ship line as it was");

    /* ---- 5. the one control and its label ---- */
    PnCapture();
    CheckEqI32(g_pnCtlB.x, PUFO_STATUS_TEXT_X_CENTRE - 20,
               "pGeoscapeCtlB centres on 120 from its own 40-wide pad (0x90444 LDRH [R0,#4])");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_PAUSECTL_Y, "and sits at y=155 (0x90454)");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 1,
               "CtlB is the only control this arm blits; the other thirteen are the tail's root widgets");
    Check(g_pnCtlB.pfnClick == PnResolve, "with Smap_ResolveInterceptUI installed at 0x9047C");
    PnRowIs(PUFO_STATUS_PAUSE_Y_LABEL, "B2", "TEXTS[1144] centred one pixel under it at y=156");

    /* ---- 6. records this arm never reads ---- */
    PnCaptureDropped(PUFO_STATUS_TEXT_CANCEL_ORDER);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, 240).nCount, 0,
               "its cancel label is TEXTS[1144] and not the card's TEXTS[1230]");
    PnCaptureDropped(PUFO_STATUS_TEXT_WEAPON1);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, 240).nCount, 0, "and it reads none of the card's weapon records");
}

static void TestStatusPanelCity(void)
{
    printf("\nGeoscape_DrawStatusPanel, the attacked-city panel @ 0x904d8\n");
    PnFixture(0x10, 0, 5, 0); /* colour slot 1 -> mission-site record 5 */
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetMissionSites();

    /* ---- 1. the body, and the strip re-blit this arm does NOT do ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, 240);
    CheckEqI32(d.nMinX, 0xE, "the body starts at x=14 (0x90524)");
    CheckEqI32(d.nMinY, PUFO_STATUS_CITY_ANIM_Y, "and y=70 (0x90518)");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x9051C) puts the right border at 225");
    CheckEqI32(d.nMaxY, PUFO_STATUS_CITY_ANIM_Y + PUFO_STATUS_CITY_NH + 5,
               "nH 0x78 puts the bottom at 195 (0x90534 STR R7)");
    /* The 0x10 arm blits no strip rectangle -- 0x9050C goes straight to the anim. What IS in this band is the
     * chain tail's own ink: the pad glyph at its origin (30,250) and the clock rows. The card's rectangle
     * colour (PN_STRIP_INK) appears nowhere here, which is the difference between the three arms. */
    d = PnDiffRect(0, 241, BD_SURF_W - 1, BD_SURF_H - 1);
    Check(d.uInk != PN_STRIP_INK, "no strip-pad rectangle: the 0x0B card and the 0x0D panel blit one, 0x10 goes straight to the anim");
    CheckEqI32(BdPix(60, 255), 0, "the 100x24 the other two arms paint over is empty here (only the tail's glyph at 30,250 is not)");
    CheckEqI32(BdPix(30, 250), 0x2F00, "and the tail's own strip-pad blit is what is left in the band");
    CheckEqI32(g_pnTime.bPaused, 0, "the clock keeps running -- only the 0x0D panel pauses it");

    /* ---- 2. the city line, TEXTS[1232] plus the mission-site name at +4 ---- */
    PnRowIs(PUFO_STATUS_CITY_Y_LINE1, "q6m6", "y=86 is TEXTS[1232] + aMissionSites[5].szName (0x90584 ADD R1,R3,#4)");
    PnFixture(0x10, 0, 6, 0); PnSetMissionSites();
    PnRowIs(PUFO_STATUS_CITY_Y_LINE1, "q6nn6", "colour slot 1 picks the record (0x9056C), index * 0x18 (0x90574)");
    PnFixture(0x10, 0, 4, 0); PnSetMissionSites();
    PnRowIs(PUFO_STATUS_CITY_Y_LINE1, "q6p6", "a neighbour index reads the neighbour name, so the stride is 24 and not 20");
    PnFixture(0x10, 0, PUFO_MISSION_SITE_COUNT, 0); PnSetMissionSites();
    PnRowIs(PUFO_STATUS_CITY_Y_LINE1, "q6", "an index past the 56 records is guarded here and unchecked in the original (bugs.tsv)");
    PnFixture(0x10, 0, 0, 0); PnSetMissionSites();
    PnRowIs(PUFO_STATUS_CITY_Y_LINE1, "q6z6", "index 0 is a real record and reads the table's first name");
    PnFixture(0x10, 0, 5, 0); PnSetMissionSites();

    /* ---- 3. the second line ---- */
    PnRowIs(PUFO_STATUS_CITY_Y_LINE2, "rr6", "y=104 is TEXTS[1233] alone (0x9060C)");

    /* ---- 4. the two controls, both blitted before their handler ---- */
    g_nPnShipIdx = 1;         /* the 0x187054 global: this arm neither reads nor writes it */
    g_pnCtlA.uWidth  = 30;    /* deliberately different from the confirm control's 40, so a width read
                               * from the wrong control shows as a different x */
    PnCapture();
    CheckEqI32(g_pnCtlConfirm.x, PUFO_STATUS_TEXT_X_CENTRE - 20,
               "pGeoscapeCtlConfirm centres on 120 from its own 40-wide pad (0x90618 LDRH [R0,#4])");
    CheckEqI32(g_pnCtlConfirm.y, PUFO_STATUS_CITYCTL_Y_CONFIRM, "and sits at y=136 (0x9062C)");
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 15, "pGeoscapeCtlA centres from ITS OWN 30-wide pad (0x90650)");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_CITYCTL_Y_ACTION, "at y=160 (0x90664)");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "two controls, one blit each, then the tail's thirteen root widgets");
    Check(g_pnCtlConfirm.pfnClick == PnSelectTarget, "the confirm control gets Smap_OnPointerUp (0x90648)");
    Check(g_pnCtlA.pfnClick == PnResolve, "the other gets Smap_ResolveInterceptUI (0x9067C)");
    CheckEqI32(g_nPnShipIdx, 1, "0x187054 keeps the value the fixture put in it: this arm neither reads nor writes it");

    /* ---- 5. the two labels, drawn after both controls ---- */
    PnRowIs(PUFO_STATUS_CITY_Y_CONFIRM_LBL, "sss6",
            "TEXTS[1234] centred on the confirm control's OWN row, y=136 (0x906C8)");
    PnRowIs(PUFO_STATUS_CITY_Y_ACTION_LBL, "B2",
            "and TEXTS[1144] (R9 = 0x39400 | 0x18) one row under CtlA at y=161 (0x90718)");

    /* ---- 6. what this arm does not read ---- */
    PnCaptureDropped(PUFO_STATUS_TEXT_CANCEL_ORDER);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, 240).nCount, 0,
               "its cancel label is TEXTS[1144] and not the card's TEXTS[1230]");
    PnCaptureDropped(PUFO_STATUS_TEXT_SHIP_LABEL);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, 240).nCount, 0, "and this arm has no ship line at all");
    PnCaptureDropped(PUFO_STATUS_TEXT_CITY_ATTACKED);
    d = PnDiffRect(20, PUFO_STATUS_CITY_Y_LINE2 + PN_GLYPH_DY, 219, PUFO_STATUS_CITY_Y_LINE2 + PN_BAND);
    Check(d.nCount > 0, "TEXTS[1233] is the only source of the second row");
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, PUFO_STATUS_CITY_Y_LINE2 + 1).nCount, 0,
               "dropping it moves nothing above that row");
    CheckEqI32(PnDiffRect(0, PUFO_STATUS_CITY_Y_LINE2 + PN_BAND + 1, BD_SURF_W - 1, 240).nCount, 0,
               "and nothing below it");
}

/* The clock fields the intercept-list suite runs with: 07:05:09 on 23 January 1999. Every digit the
 * clock draws is non-zero except the three tens of HH:MM:SS, so pad 0 ends at the seconds tens while
 * pads 7, 5, 9, 1, 2 and 3 each end at a different position -- a wrong digit-to-pad mapping or a
 * wrong row shows up as a wrong origin. */
static void PnSetClockTime(void)
{
    g_pnTime.nYear    = 1999;
    g_pnTime.nMonth   = 1;
    g_pnTime.nDay     = 23;
    g_pnTime.nWeekday = 1;
    g_pnTime.nHour    = 7;
    g_pnTime.nMinute  = 5;
    g_pnTime.nSecond  = 9;
}

static void TestStatusPanelInterceptList(void)
{
    printf("\nGeoscape_DrawStatusPanel, the intercept list @ 0x90728\n");
    PnFixture(0x1B, 7, 0, 0); /* colour slot 0 is the value this arm stores into 0x187054 */
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetClockTime();

    /* ---- 1. the frame, with bFinalMissionCtlArmed clear ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1);
    CheckEqI32(d.nMinX, 0xE, "the anim body starts at x=14 (0x90818)");
    CheckEqI32(d.nMinY, 250, "and the strip rect's y=250 is the first row this arm paints");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x907F8) puts the right border at 225");
    CheckEqI32(d.nMaxY, PUFO_STATUS_LIST_ANIM_Y + PUFO_STATUS_LIST_NH + 5,
               "nH 0x28 (0x90824, R10 from 0x904F4) puts the bottom at 305 -- over the clock it drew");
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, 249).nCount, 0,
               "nothing above the strip: 0x1B has no panel body of its own");
    CheckEqI32(g_nPnBlits, 14, "thirteen root controls plus CtlB, one blit each");

    /* ---- 2. the strip rectangle, then the pad's own glyph painted over it ---- */
    CheckEqI32(BdPix(60, 255), PN_STRIP_INK, "the rect uses nIdx 0x3C, mid 0x78, low 0x82, key 0x96 (0x9074C..0x90788)");
    CheckEqI32(BdPix(129, 255), PN_STRIP_INK, "and it covers the pad's full 100x24");
    CheckEqI32(BdPix(130, 255), 0, "no further (the +8/+0xA LDRH reads)");
    CheckEqI32(BdPix(30, 250), 0x2F00, "0x907C0 then blits the strip PAD at its own origin, over the rect");

    /* ---- 3. the clock call, read through the origins its pads end with ---- */
    CheckEqI32(g_pnClockPad[0].nOriginX, 140, "the seconds tens left pad 0 at x=140 (the 0x5C720 pair)");
    CheckEqI32(g_pnClockPad[0].nOriginY, 260, "on the time row 260");
    CheckEqI32(g_pnClockPad[7].nOriginX, 100, "the hour ones 7 took pad 7 to x=100");
    CheckEqI32(g_pnClockPad[5].nOriginX, 125, "the minute ones 5 took pad 5 to x=125");
    CheckEqI32(g_pnClockPad[9].nOriginX, 135, "the year 1999 runs LAST and left its last 9 at x=135");
    CheckEqI32(g_pnClockPad[9].nOriginY, 304, "on the year row 304");
    CheckEqI32(g_pnClockPad[2].nOriginX, 90, "the day 23 tens moved pad 2 to x=90, row 287");
    CheckEqI32(g_pnClockPad[3].nOriginX, 100, "and its ones moved pad 3 to x=100");

    /* ---- 4. which controls got those blits, in order ---- */
    bool bRootOrder = true;
    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        if (g_apnPnBlitPad[i] != &g_aPnRootPad[i]) {
            bRootOrder = false;
        }
    }
    Check(bRootOrder, "the first thirteen blits are the root block's own widgets in slot order (0x1864F8 + 4*i)");
    Check(g_apnPnBlitPad[13] == g_pnCtlB.apPads[0], "and the fourteenth is CtlB's pad (0x90894)");

    /* ---- 5. the write into 0x187054 ---- */
    CheckEqI32(g_nPnShipIdx, 7, "0x90760 stores colour slot 0 into 0x187054, as the 0x0B card does");
    /* 255 is the largest value a colour slot can hold: UiLineList_SetLine masks slot 0 with
     * AND #0xFF, so a value above 0xFF cannot reach the store at all (pufo_linelist.h). 255 is also
     * the "no target" sentinel the 0x0D panel tests, and Smap_HitTestStrategSelect's -1 arrives as it. */
    PnFixture(0x1B, PUFO_LINE_COLOR_MAX, 0, 0);
    PnSetClockTime();
    PnRun();
    CheckEqI32(g_nPnShipIdx, 255, "the whole masked byte travels into 0x187054, sentinel value included");
    PnFixture(0x1B, 7, 0, 0);
    PnSetClockTime();

    /* ---- 6. the one control and its two labels ---- */
    PnCapture();
    CheckEqI32(g_pnCtlB.x, PUFO_STATUS_LISTCTL_X,
               "CtlB is FIXED at x=140 (0x90874) -- the only control in this chain that is not centred");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_LISTCTL_Y, "at y=274 (0x90880 MOV R3,#0x110 then ORR R3,R3,#2)");
    Check(g_pnCtlB.pfnClick == PnResolve, "with Smap_ResolveInterceptUI installed at 0x908A4");
    d = PnDiffRect(0, PUFO_STATUS_LIST_Y_LINE + PN_GLYPH_DY, 100, PUFO_STATUS_LIST_Y_LINE + PN_BAND);
    Check(d.nCount > 0 && d.nMinX == PUFO_STATUS_LIST_X_LABEL,
          "TEXTS[1235] starts at x=40: no Text_MeasureWidth call at all on this line (0x90858..0x9086C)");
    d = PnDiffRect(140, PUFO_STATUS_LIST_Y_LINE + PN_GLYPH_DY, 215, PUFO_STATUS_LIST_Y_LINE + PN_BAND);
    Check(d.nCount > 0, "TEXTS[1144] shares that row");
    Check(PnAbs((d.nMinX + d.nMaxX) / 2 - PUFO_STATUS_LIST_CANCEL_ANCHOR) <= 2,
          "and it is centred on 173, not on 120 (0x908EC RSB R1,R1,#0xAD)");

    /* ---- 7. the three gates: each AFTER the strip rect, each BEFORE everything else ---- */
    static u8 *const apnPnGate[3] = { &g_bPnUfoCrash, &g_bPnSite, &g_bPnAlienBase };
    static const char *const apszPnGate[3] = {
        "bSmapUfoCrashPanel @ 0x18708C (0x9078C)", "bSmapSitePanel @ 0x187090 (0x907A0)",
        "bSmapAlienBasePanel @ 0x187098 (0x907B4)"
    };
    for (i32 i = 0; i < 3; ++i) {
        char szWhat[160];
        PnFixture(0x1B, 7, 0, 0);
        PnSetClockTime();
        *apnPnGate[i] = 1;
        PnCaptureFrameOnly();
        d = PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1);
        snprintf(szWhat, sizeof(szWhat),
                 "%s ends the frame with only the strip rect (30,250)..(129,273) painted", apszPnGate[i]);
        Check(d.nMinX == 30 && d.nMaxX == 129 && d.nMinY == 250 && d.nMaxY == 273, szWhat);
        CheckEqI32(g_nPnBlits, 0, "  no control blit, no anim, no labels past it");
        CheckEqI32(BdPix(30, 250), PN_STRIP_INK, "  and the pad's own blit at 0x907C0 is past it too");
        *apnPnGate[i] = 0;
    }

    /* ---- 8. the final-mission block ---- */
    g_bPnFinalArmed = 1;
    PnCapture();
    CheckEqI32(g_nPnBlits, 15, "0x187068 set adds pFinalMissionCtl's blit (0x90910)");
    d = PnDiffRect(0, 0, BD_SURF_W - 1, 30);
    Check(d.nCount > 0, "TEXTS[1236] is drawn at y=1 -- the top edge of the screen (0x90960 MOV R2,#1)");
    const i32 nW = Text_MeasureWidth(PnRec(PUFO_STATUS_TEXT_FINAL_MISSION));
    const i32 nX = (i32)(g_pnCtlConfirm.uWidth >> 1) - (nW >> 1) + 1;
    Check(PnAbs((d.nMinX + d.nMaxX) / 2 - (nX + nW / 2)) <= 2,
          "centred on the CONFIRM CONTROL'S WIDTH and not on 120: x = uWidth/2 - w/2 + 1 (0x90964..0x90970)");

    /* ---- 9. the records this arm reads, one at a time ---- */
    PnCaptureDropped(PUFO_STATUS_TEXT_CHOOSE_TARGET);
    Check(PnDiffRect(0, PUFO_STATUS_LIST_Y_LINE + PN_GLYPH_DY, 100,
                     PUFO_STATUS_LIST_Y_LINE + PN_BAND).nCount > 0, "TEXTS[1235] feeds the left row");
    CheckEqI32(PnDiffRect(140, PUFO_STATUS_LIST_Y_LINE + PN_GLYPH_DY, 215,
                          PUFO_STATUS_LIST_Y_LINE + PN_BAND).nCount, 0, "and nothing on the right one");
    PnCaptureDropped(PUFO_STATUS_TEXT_CANCEL);
    Check(PnDiffRect(140, PUFO_STATUS_LIST_Y_LINE + PN_GLYPH_DY, 215,
                     PUFO_STATUS_LIST_Y_LINE + PN_BAND).nCount > 0, "TEXTS[1144] feeds the right row");
    PnCaptureDropped(PUFO_STATUS_TEXT_FINAL_MISSION);
    Check(PnDiffRect(0, 0, BD_SURF_W - 1, 30).nCount > 0, "TEXTS[1236] is the top row's only source");
    PnCaptureDropped(PUFO_STATUS_TEXT_CITY);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and this arm reads none of the 0x10 panel's records");
}

/* The status panel's rows as Geoscape_InitUI's UiPanel_ctor(pPanel, 20) would have them: 20 buffers,
 * two of them carrying text, the rest empty. apLines[20..499] stay NULL exactly as the ctor leaves
 * them, which is what the 0x14 arm's unbounded colour-slot index walks into. */
static void PnSetPanelRows(void)
{
    memset(g_aPnPanelRow, 0, sizeof(g_aPnPanelRow));
    pufo_strncpy(g_aPnPanelRow[3], "k6", PUFO_PANEL_LINE_BYTES);
    pufo_strncpy(g_aPnPanelRow[4], "kk6", PUFO_PANEL_LINE_BYTES);
    g_pnPanel.nLineCount = PN_PANEL_ROWS;
    for (i32 i = 0; i < PN_PANEL_ROWS; ++i) {
        g_pnPanel.apLines[i] = (u8 *)g_aPnPanelRow[i];
    }
}

static void TestStatusPanelTargetPanel(void)
{
    printf("\nGeoscape_DrawStatusPanel, the map-target panel @ 0x909a8\n");
    PnFixture(0x14, 3, 1, 0); /* apLines row 3; strateg entry 1 -> bCraftClass 2 -> TEXTS[1360] */
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetPanelRows();

    /* ---- 1. the body ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(d.nMinX, 0xE, "the anim body starts at x=14 (0x909C4 MOV R1, #0xE)");
    CheckEqI32(d.nMinY, PUFO_STATUS_TARGET_ANIM_Y, "and y=60 (0x909C8 MOV R2, #0x3C)");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x909F4) puts the right border at 225");
    CheckEqI32(d.nMaxY, PUFO_STATUS_TARGET_ANIM_Y + PUFO_STATUS_TARGET_NH + 5,
               "nH 0x5A (0x909EC then 0x909F0) puts the bottom at 155");
    CheckEqI32(g_pnTime.bPaused, 0, "and the clock keeps running: only the 0x0D panel pauses it");

    /* ---- 2. the anim is the 0x0F panel's anim, value for value ----
     * 0x8F428..0x8F45C and 0x909C4..0x90A04 hand Ui_AnimExpandBlit the same nine arguments, so the
     * body ink has to match pixel for pixel. That is what pins nIdx 0, src 0x28/0x32, key 0xB4 and
     * mode 0 here -- the geometry alone cannot see them. */
    PnFixture(0x0F, 3, 1, 0);
    PnEmptySources();
    PnRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase)); /* the baseline is now the 0x0F frame */
    PnFixture(0x14, 3, 1, 0);
    PnSetPanelRows();
    PnEmptySources();
    PnRun();
    CheckEqI32(PnDiffRect(20, 120, 219, 145).nCount, 0,
               "the body ink equals the 0x0F panel's: same nIdx 0, src 0x28/0x32, key 0xB4, mode 0");
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, PUFO_STATUS_TARGET_ANIM_Y - 1).nCount, 0,
               "and the same box: nothing painted above y=60 in either arm");

    /* ---- 3. the clear arm's line: TEXTS[1237] + apLines[slot 0] + " " + the size word ---- */
    PnFixture(0x14, 3, 1, 0); PnSetPanelRows();
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6k6 L2",
            "y=70 is TEXTS[1237] + apLines[3] + \" \" + TEXTS[1360] (0x90A54, 0x90A60, 0x90B04)");
    PnFixture(0x14, 4, 1, 0); PnSetPanelRows();
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6kk6 L2",
            "colour SLOT 0 IS the apLines index (0x90A34 MOV R1,#0, 0x90A48 LDR [R3,R10])");
    PnFixture(0x14, 3, 0, 0); PnSetPanelRows();
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6k6 N2",
            "colour SLOT 1 picks the strateg entry and its +0x02 byte the word (0x90A68, 0x90A84)");
    PnFixture(0x14, 3, 1, 0); PnSetPanelRows(); g_pnStrat[1].bCraftClass = 0;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6k6 K2", "class 0 -> TEXTS[1359] (0x90AC8)");
    PnFixture(0x14, 3, 1, 0); PnSetPanelRows(); g_pnStrat[1].bCraftClass = 7;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6k6 O2", "class 7 -> TEXTS[1363] (0x90AF8)");
    PnFixture(0x14, 3, 1, 0); PnSetPanelRows(); g_pnStrat[1].bCraftClass = 8;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6k6 ", "class 8 is outside the jumptable: ADDLS does not fire and the default appends nothing (0x90A8C/0x90A90)");
    PnFixture(0x14, 3, 1, 0); PnSetPanelRows(); g_pnStrat[1].bCraftClass = 0xFF;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6k6 ", "and LDRSB makes 0xFF a NEGATIVE class, which also takes the default");
    PnFixture(0x14, 3, 5, 0); PnSetPanelRows();
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6k6 ", "an entry index past the strateg list yields no entry, so no word (0x90A80)");
    PnFixture(0x14, 20, 1, 0); PnSetPanelRows();
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6 L2",
            "row 20 is past the panel's 20 rows: the port skips the NULL, the original hands it to strcat (bugs.tsv)");
    PnFixture(0x14, 255, 1, 0); PnSetPanelRows();
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "w6 L2", "the same at the top of the byte: 255 is inside apLines[500] but never allocated");

    /* ---- 4. the clear arm's control and label ---- */
    PnFixture(0x14, 3, 1, 0); PnSetPanelRows();
    g_pnCtlA.uWidth = 30; /* different from the other controls' 40, so a wrong control shows as a wrong x */
    PnCapture();
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 15,
               "CtlA centres on 120 from ITS OWN 30-wide pad (0x90B3C LDRH [R0,#4])");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_TARGETCTL_Y_OK, "at y=115 (0x90B4C)");
    CheckEqI32(g_pnCtlB.y, 0, "this arm does not touch CtlB at all");
    CheckEqI32(g_pnCtlConfirm.y, 0, "nor the confirm control");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 1, "one control blit (0x90B68) plus the tail's thirteen");
    Check(g_pnCtlA.pfnClick == PnResolve, "CtlA gets Smap_ResolveInterceptUI (0x90B5C / 0x90B60)");
    Check(g_pnCtlB.pfnClick == NULL, "and CtlB keeps no handler from this arm");

    /* One fresh run, with the handler slot emptied first: PnCapture runs the arm twice, so without
     * this the handler the FIRST run installed would still be there when the second one blits. */
    g_pnCtlA.pfnClick = NULL;
    PnRun();
    Check(g_apnPnBlitHandler[0] == PnResolve,
          "the handler is installed BEFORE the blit -- the reverse of the 0x0B, 0x0D, 0x10 and 0x1B arms");
    PnRowIs(PUFO_STATUS_TARGET_Y_OK_LBL, "m4", "TEXTS[1127] centred on 120 at y=116 (0x90BA0 then 0x90D5C)");

    /* ---- 5. the set arm: TEXTS[1238] + apLines[slot 0], no space and no size word ---- */
    PnFixture(0x14, 3, 1, 0); PnSetPanelRows(); g_bPnMapSel = 1;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "aaa6k6",
            "y=70 is TEXTS[1238] + apLines[3] with NO space and NO size word (0x90BD4, 0x90C1C)");
    PnFixture(0x14, 4, 1, 0); PnSetPanelRows(); g_bPnMapSel = 1;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "aaa6kk6", "colour SLOT 0 indexes the row here too (0x90C00 then 0x90C14)");
    PnFixture(0x14, 3, 5, 0); PnSetPanelRows(); g_bPnMapSel = 1;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "aaa6k6",
            "colour SLOT 1 is not read in this arm: an unusable entry changes nothing");

    /* ---- 6. the set arm's two controls ---- */
    PnFixture(0x14, 3, 1, 0); PnSetPanelRows(); g_bPnMapSel = 1;
    g_pnCtlB.uWidth = 30;
    PnCapture();
    CheckEqI32(g_pnCtlB.x, PUFO_STATUS_TEXT_X_CENTRE - 15,
               "CtlB centres on 120 from ITS OWN 30-wide pad (0x90C54)");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_TARGETCTL_Y_CANCEL, "at y=102 (0x90C64)");
    Check(g_pnCtlB.pfnClick == PnCancelTarget, "CtlB gets Smap_CancelTarget (0x90C74 / 0x90C78)");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_TARGETCTL_Y_OK2, "CtlA is this arm's SECOND control, at y=125 (0x90CF0)");
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 20, "centred from its own 40-wide pad (0x90CE0)");
    Check(g_pnCtlA.pfnClick == PnResolve, "with Smap_ResolveInterceptUI (0x90D00 / 0x90D04)");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "two controls, one blit each, then the tail's thirteen root widgets");
    Check(g_apnPnBlitPad[0] == g_pnCtlB.apPads[0] && g_apnPnBlitPad[1] == g_pnCtlA.apPads[0],
          "CtlB first (0x90C80), CtlA second (0x90D0C)");
    g_pnCtlB.pfnClick = NULL;
    g_pnCtlA.pfnClick = NULL;
    PnRun();                                                        /* one fresh run, handlers clear */
    Check(g_apnPnBlitHandler[0] == PnCancelTarget, "CtlB's handler is installed before its 0x90C80 blit");
    Check(g_apnPnBlitHandler[1] == PnResolve, "and CtlA's before its 0x90D0C blit");
    PnRowIs(PUFO_STATUS_TARGET_Y_CANCEL_LBL, "m4", "TEXTS[1127] centred at y=103 (0x90CC8)");
    PnRowIs(PUFO_STATUS_TARGET_Y_CANCELLED_LBL, "B2",
            "TEXTS[1144] centred at y=126 (0x90D14, R9 built at 0x904D8/0x904E0)");

    /* ---- 7. the records, one at a time ---- */
    PnFixture(0x14, 3, 1, 0); PnSetPanelRows(); g_bPnMapSel = 0;
    PnCaptureDropped(PUFO_STATUS_TEXT_CRASH);
    d = PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                   PUFO_STATUS_TARGET_Y_LINE + PN_BAND);
    Check(d.nCount > 0, "TEXTS[1237] is the clear arm's line");
    CheckEqI32(PnDiffRect(0, PUFO_STATUS_TARGET_Y_OK_LBL + PN_GLYPH_DY, BD_SURF_W - 1,
                          PUFO_STATUS_TARGET_Y_OK_LBL + PN_BAND).nCount, 0,
               "and dropping it moves nothing else");
    PnCaptureDropped(PUFO_STATUS_TEXT_MISSION_PREFIX);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "the clear arm never reads TEXTS[1238]");
    PnCaptureDropped(1360);
    d = PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                   PUFO_STATUS_TARGET_Y_LINE + PN_BAND);
    Check(d.nCount > 0, "TEXTS[1360] feeds the size word of the clear arm's line");
    PnCaptureDropped(1361);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and not TEXTS[1361], which is the selected-contact panel's word");
    PnCaptureDropped(PUFO_STATUS_TEXT_CANCEL);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "the clear arm has no TEXTS[1144] label -- that one is the set arm's");

    PnFixture(0x14, 3, 1, 0); PnSetPanelRows(); g_bPnMapSel = 1;
    PnCaptureDropped(PUFO_STATUS_TEXT_MISSION_PREFIX);
    d = PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                   PUFO_STATUS_TARGET_Y_LINE + PN_BAND);
    Check(d.nCount > 0, "TEXTS[1238] is the set arm's line");
    PnCaptureDropped(PUFO_STATUS_TEXT_CRASH);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and the set arm never reads TEXTS[1237]");
    PnCaptureDropped(1360);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "nor any size record: the switch is only in the clear arm");
    PnCaptureDropped(PUFO_STATUS_TEXT_CANCEL);
    d = PnDiffRect(0, PUFO_STATUS_TARGET_Y_CANCELLED_LBL + PN_GLYPH_DY, BD_SURF_W - 1,
                   PUFO_STATUS_TARGET_Y_CANCELLED_LBL + PN_BAND);
    Check(d.nCount > 0, "TEXTS[1144] is the set arm's second label");
    PnCaptureDropped(PUFO_STATUS_TEXT_CITY);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and none of the 0x10 panel's records feed this arm");
    g_bPnMapSel = 0;
}

/* The 0x13 / 0x12 arms @ 0x90d78. Flag 0x13 reads the status panel's own selected row (nUnk3032) and a
 * second "Город: " line built from the SLOT 1 strateg entry's +0x02 byte used as a MISSION-SITE index;
 * flag 0x12 is the 0x14 arm with the space and the size word taken away. The two fixtures therefore set
 * the panel's selected row, colour slot 0 and nUnk3032 to DIFFERENT rows, so an arm that reads the
 * wrong one shows up as a different line. */
static void PnSetSiteFixture(void)
{
    PnSetPanelRows();
    PnSetMissionSites();
    g_pnPanel.nUnk3032 = 3;        /* "k6"; colour slot 0 in these runs is 4 -> "kk6" */
    g_pnStrat[0].bCraftClass = 4;  /* -> aMissionSites[4] = "p6" */
    g_pnStrat[1].bCraftClass = 6;  /* -> aMissionSites[6] = "nn6" */
}

/* The 0x17 arm counts soldiers aboard the craft colour SLOT 0 names (0x915D4..0x915EC), and the count
 * decides CtlB's handler. One soldier on nCraft, the other on the 254 sentinel, makes it exactly 1. */
static void PnSetSoldierAboard(i32 nCraft)
{
    g_aPnSoldiers[0].bUnk183 = (u8)nCraft;
    g_aPnSoldiers[1].bUnk183 = 254;
}

static void TestStatusPanelDescentPanel(void)
{
    printf("\nGeoscape_DrawStatusPanel, the descent panel @ 0x9148c\n");
    PnFixture(0x17, 4, 1, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetPanelRows();
    PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;   /* row 3 "k6"; colour slot 0 is 4 -> row 4 "kk6" */

    /* ---- 1. the body ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(d.nMinX, 0xE, "the anim body starts at x=14 (0x914D4 MOV R1,#0xE)");
    CheckEqI32(d.nMinY, PUFO_STATUS_TARGET_ANIM_Y, "and y=60 (0x914C8 MOV R2,#0x3C)");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x914CC) puts the right border at 225");
    CheckEqI32(d.nMaxY, PUFO_STATUS_TARGET_ANIM_Y + PUFO_STATUS_TARGET_NH + 5,
               "nH 0x5A (0x914C4 STR R3,[SP+nKey]) puts the bottom at 155");

    PnFixture(0x0F, 4, 1, 0);
    PnEmptySources();
    PnRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    PnFixture(0x17, 4, 1, 0);
    PnEmptySources();
    PnRun();
    CheckEqI32(PnDiffRect(20, 120, 219, 145).nCount, 0,
               "body ink equals the 0x0F panel's: nKey 0xB4 (0x914A4/0x914AC), src 0x28/0x32, nIdx and nMode 0 (R7, 0x9149C)");

    /* ---- 2. the line: TEXTS[1239] plus the panel's SELECTED row ---- */
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "d6k6",
           "y=70 is TEXTS[1239] \"Снижение: \" (0x914E0..0x91500, R6 from 0x91490) + apLines[nUnk3032] (0x9151C)");
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 4;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "d6kk6",
           "and the row is the SELECTED one, not the colour slot: the slot says 3 here, the panel says 4");
    PnFixture(0x17, 3, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 25;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "d6",
           "row 25 is past the panel's 20 rows: the port appends nothing, 0x9151C would hand the NULL to strcat");
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnCaptureDropped(1239);
    Check(PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                     PUFO_STATUS_TARGET_Y_LINE + PN_BAND).nCount > 0,
          "and dropping TEXTS[1239] changes that row, so 1239 is the record and not a neighbour");
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "",
            "no \"Город: \" line here (0x90E44's city line belongs to 0x13 only) -- y=86 stays empty");

    /* ---- 3. the two controls ---- */
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    g_pnCtlB.uWidth = 30;
    PnCapture();
    CheckEqI32(g_pnCtlB.x, PUFO_STATUS_TEXT_X_CENTRE - 15, "CtlB centres on 120 from ITS OWN 30-wide pad (0x91554 LDRH [R0,#4])");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_TARGETCTL_Y_CANCEL, "at y=102 (0x9156C MOV R3,#0x66) -- the 0x14 arm's set-arm row");
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 20, "CtlA centres from its own 40-wide pad (0x9160C)");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_TARGETCTL_Y_OK2, "at y=125 (0x91624 MOV R3,#0x7D)");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "two control blits (0x91580 and 0x91638), then the tail's thirteen root widgets");
    Check(g_apnPnBlitPad[0] == g_pnCtlB.apPads[0] && g_apnPnBlitPad[1] == g_pnCtlA.apPads[0],
          "CtlB first, then CtlA");
    /* One fresh run with both handlers cleared: PnCapture runs the arm twice, and the second run would
     * otherwise see the first run's handler already installed. */
    g_pnCtlA.pfnClick = NULL;
    g_pnCtlB.pfnClick = NULL;
    PnRun();
    Check(g_apnPnBlitHandler[0] == PnBeginCraftSelect,
          "CtlB is blitted with Smap_BeginCraftSelect @ 0x838dc already installed (0x91578 before 0x91580)");
    Check(g_apnPnBlitHandler[1] == PnResolve, "CtlA gets Smap_ResolveInterceptUI before its 0x91638 blit (0x91630)");
    CheckEqI32(g_nPnBeginCraftClicks, 0, "and the arm installs handlers, it never calls one");
    CheckEqI32(g_nPnCancelClicks, 0, "including no Smap_CancelTarget call -- this arm has no cancel control");
    PnRowIs(PUFO_STATUS_TARGET_Y_CANCEL_LBL, "m4", "TEXTS[1127] centred at y=103 (0x91584..0x915A0, R8 = aTextOk, then 0x915D0)");
    PnRowIs(PUFO_STATUS_TARGET_Y_CANCELLED_LBL, "B2", "TEXTS[1144] centred at y=126 (0x9163C..0x9165C, R9, then 0x91688)");

    /* ---- 4. the soldiers-aboard gate ---- */
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnBeginCraftSelect,
          "one soldier aboard craft 4 (colour SLOT 0, 0x915D4..0x915E4 AND #0xFF) keeps CtlB on Smap_BeginCraftSelect");
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(5); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnResolve,
          "nobody aboard craft 4 -- 0x915F4 MOVS then 0x915F8..0x91604 REPLACE the handler, after its blit");
    Check(g_apnPnBlitHandler[0] == PnBeginCraftSelect,
          "and the replacement is AFTER the 0x91580 blit: the pad still went out with BeginCraftSelect");
    PnFixture(0x17, 5, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(5); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnBeginCraftSelect,
          "the craft is the one colour SLOT 0 names: slot 0 = 5 with a soldier aboard 5 counts");
    PnFixture(0x17, 5, 4, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnResolve,
          "and NOT colour SLOT 1: slot 1 = 4 with the soldier aboard 4 still counts zero here (0x915D8 MOV R2,#0)");
    /* The craft index is a whole byte, not the 0..7 class the 0x14 arm reads the same field as. The
     * AND at 0x915E4 is a formality: every writer of a colour slot stores the LOW BYTE only
     * (0x5A314 LDRB then STR into the dword), so the value is 0..255 before the mask. */
    PnFixture(0x17, 255, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(255); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnBeginCraftSelect, "craft 255 counts: the index is a full byte, not the 0..7 size class");
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(255); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnResolve, "and a soldier aboard 255 is nobody aboard craft 4");
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    g_nPnSoldierCount = 0;
    PnCapture();
    g_nPnSoldierCount = 2;
    Check(g_pnCtlB.pfnClick == PnResolve, "an empty roster counts zero, so the resolve handler wins (0x664B4 CMP R3,#0)");

    /* ---- 5. bMapSelectionFlag is not read here ---- */
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    g_bPnMapSel = 0;
    PnFillSources();
    PnRun();                                        /* the baseline IS the flag-clear filled run */
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    g_bPnMapSel = 1;
    PnRun();
    CheckEqI32(PnDiffRect(0, PUFO_STATUS_TARGET_ANIM_Y, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "the arm has ONE path: bMapSelectionFlag @ 0x159E84 is never read in 0x9148C..0x91688");
    g_bPnMapSel = 0;
}

/* StrategItem.nTargetIdx @ +0x84 -- the index the 0x15 arm reads at 0x91798, where the 0x13 arm reads the
 * byte at +0x02. Sites 0 / 4 / 5 / 6 are "z6" / "p6" / "m6" / "nn6". */
static void PnSetTargetIdx(i32 nEntry1, i32 nEntry0)
{
    g_pnStrat[1].nTargetIdx = nEntry1;
    g_pnStrat[0].nTargetIdx = nEntry0;
}

static void TestStatusPanelDescentTarget(void)
{
    printf("\nGeoscape_DrawStatusPanel, the descent-target panel @ 0x916a0\n");
    PnFixture(0x15, 4, 1, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetPanelRows();
    PnSetMissionSites();
    PnSetTargetIdx(6, 5);
    PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;

    /* ---- 1. the body ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(d.nMinX, 0xE, "the anim body starts at x=14 (0x916D0 MOV R1,#0xE)");
    CheckEqI32(d.nMinY, PUFO_STATUS_TARGET_ANIM_Y, "and y=60 (0x916C4 MOV R2,#0x3C)");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x916C8) puts the right border at 225");
    CheckEqI32(d.nMaxY, PUFO_STATUS_TARGET_ANIM_Y + PUFO_STATUS_TARGET_NH + 5, "nH 0x5A puts the bottom at 155");

    PnFixture(0x0F, 4, 1, 0);
    PnEmptySources();
    PnRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    PnFixture(0x15, 4, 1, 0);
    PnEmptySources();
    PnRun();
    CheckEqI32(PnDiffRect(20, 120, 219, 145).nCount, 0,
               "body ink equals the 0x0F panel's -- the same nine anim values, zeros from R7 (0x916CC / 0x916D4)");

    /* ---- 2. line 1: TEXTS[1239] (R6, sticky from the 0x17 prologue at 0x91490) + the selected row ---- */
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(6, 5); PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "d6k6", "y=70 is TEXTS[1239] + apLines[nUnk3032] (0x91700..0x91710, then BL strcat at 0x91718)");
    g_pnPanel.nUnk3032 = 4;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "d6kk6", "the SELECTED row and not the colour-slot row (R11 = 0xBD8, sticky from 0x90D88)");
    g_pnPanel.nUnk3032 = 25;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "d6", "row 25 is past the panel's 20 rows: guarded here, 0x91718 would strcat a NULL");

    /* ---- 3. line 2: TEXTS[1232] + aMissionSites[ entry->nTargetIdx ], entry from colour SLOT 1 ---- */
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(6, 5); PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6nn6", "y=86 is TEXTS[1232] (0x91748) + aMissionSites[nTargetIdx] (0x91798 LDR R3,[R0,#0x84], 0x917A0 MUL #0x18, 0x917AC ADD #4)");
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(4, 6); PnSetSoldierAboard(4);
    g_pnStrat[1].bCraftClass = 6;   /* the +0x02 byte the 0x13 arm would use, deliberately different */
    g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6p6", "and the index IS +0x84: nTargetIdx 4 while bCraftClass is 6 (0x13 would say nn6)");
    PnFixture(0x15, 0, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(6, 4); PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6nn6", "the ENTRY comes from colour SLOT 1 (0x91778 MOV R2,#1): entry 0's index 4 is not used");
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(56, 5); PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6", "index 56 is past the 56 records: 0x917A0 MULs and 0x917A8 ADDs with no bound (bugs.tsv)");
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(-1, 5); PnSetSoldierAboard(4);
    PnShiftSiteTableRight();   /* paMissionSites[-1] is "bb6" in this run */
    g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6",
           "and a negative nTargetIdx is guarded -- the table here has \"bb6\" in the slot before it, which is what 0x917A0 / 0x917A8 would read");

    /* ---- 4. controls and labels (the 0x13 arm's rows) ---- */
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(6, 5); PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;
    g_pnCtlB.uWidth = 30;
    PnCapture();
    CheckEqI32(g_pnCtlB.x, PUFO_STATUS_TEXT_X_CENTRE - 15, "CtlB centres on 120 from ITS OWN 30-wide pad (0x917E8 LDRH [R0,#4])");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_SITECTL_Y_CANCEL, "at y=105 (0x917F8 MOV R3,#0x69)");
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 20, "CtlA centres from its own 40-wide pad (0x918A4)");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_SITECTL_Y_OK2, "at y=128 (0x918B0 MOV R3,#0x80)");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "two control blits (0x91814 and 0x918CC), then the tail's thirteen root widgets");
    Check(g_apnPnBlitPad[0] == g_pnCtlB.apPads[0] && g_apnPnBlitPad[1] == g_pnCtlA.apPads[0], "CtlB first, then CtlA");
    g_pnCtlA.pfnClick = NULL;
    g_pnCtlB.pfnClick = NULL;
    PnRun();
    Check(g_apnPnBlitHandler[0] == PnBeginCraftSelect, "CtlB is blitted with Smap_BeginCraftSelect installed (0x9180C before 0x91814)");
    Check(g_apnPnBlitHandler[1] == PnResolve, "CtlA gets Smap_ResolveInterceptUI before its 0x918CC blit (0x918C4)");
    PnRowIs(PUFO_STATUS_SITE_Y_CANCEL_LBL, "m4", "TEXTS[1127] centred at y=106 (0x91818..0x91838 then 0x91864)");
    PnRowIs(PUFO_STATUS_SITE_Y_CANCELLED_LBL, "B2", "TEXTS[1144] centred at y=129 (0x918D0..0x918F8, R9, then 0x9191C)");

    /* ---- 5. the same soldiers-aboard gate as 0x17, at 0x91868..0x91898 ---- */
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(6, 5); PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnBeginCraftSelect, "a soldier aboard craft 4 keeps CtlB on Smap_BeginCraftSelect");
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(6, 5); PnSetSoldierAboard(3);
    g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnResolve, "and an empty craft overwrites it with Smap_ResolveInterceptUI (0x9188C..0x91898)");
    Check(g_apnPnBlitHandler[0] == PnBeginCraftSelect, "the overwrite is after the 0x91814 blit, exactly as in 0x17");

    /* ---- 6. one path, like 0x17 ---- */
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(6, 5); PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;
    g_bPnMapSel = 0;
    PnFillSources();
    PnRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    g_bPnMapSel = 1;
    PnRun();
    CheckEqI32(PnDiffRect(0, PUFO_STATUS_TARGET_ANIM_Y, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "no bMapSelectionFlag read in 0x916A0..0x9191C either");
    g_bPnMapSel = 0;
}

/* Flag 0x16 (0x91930 CMP, body 0x91938..0x91B1C) is a second full copy of the flag-0x17 body. The checks below
 * run it on its own -- anim, records, controls, gate -- and the last pair compares WHOLE frames: 0x16 against
 * 0x17 must match, and 0x15 against the same saved frame must not, so the match carries information. */
static void TestStatusPanelDescentPanelAlt(void)
{
    printf("\nGeoscape_DrawStatusPanel, the descent panel's second copy @ 0x91938 (flag 0x16)\n");
    PnFixture(0x16, 4, 1, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetPanelRows();
    PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;

    /* ---- 1. the anim frame, from this arm's own call ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(d.nMinX, 0xE, "the anim body starts at x=14 (0x91968 MOV R1,#0xE)");
    CheckEqI32(d.nMinY, PUFO_STATUS_TARGET_ANIM_Y, "and y=60 (0x9195C MOV R2,#0x3C)");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x91960) puts the right border at 225");
    CheckEqI32(d.nMaxY, PUFO_STATUS_TARGET_ANIM_Y + PUFO_STATUS_TARGET_NH + 5, "nH 0x5A (0x91954 then 0x91958) puts the bottom at 155");

    /* ---- 2. line 1: TEXTS[1239] + the SELECTED row; no city line ---- */
    PnFixture(0x16, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "d6k6",
            "y=70 is TEXTS[1239] (inline copy 0x91974..0x91994, R6 sticky from 0x91490) + apLines[nUnk3032] (BL strcat at 0x919B0)");
    g_pnPanel.nUnk3032 = 4;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "d6kk6", "and the SELECTED row, not the colour-slot row (0x91998..0x919A8, R11 = 0xBD8 from 0x90D88)");
    g_pnPanel.nUnk3032 = 25;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "d6", "row 25 is past the panel's 20 rows: guarded, 0x919B0 would strcat a NULL");
    PnFixture(0x16, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "", "y=86 stays empty: 0x91938..0x91B1C never reads TEXTS[1232]");

    /* ---- 3. controls, labels, handler order ---- */
    PnFixture(0x16, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    g_pnCtlB.uWidth = 30;
    PnCapture();
    CheckEqI32(g_pnCtlB.x, PUFO_STATUS_TEXT_X_CENTRE - 15, "CtlB centres on 120 from ITS OWN 30-wide pad (0x919E8 LDRH [R0,#4])");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_TARGETCTL_Y_CANCEL, "at y=102 (0x919F8 MOV R3,#0x66) -- the 0x17 rows, not the 0x15 arm's 105");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_TARGETCTL_Y_OK2, "CtlA at y=125, the 0x17 row");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "two control blits (0x91A14 and 0x91ACC), then the tail's thirteen root widgets");
    Check(g_apnPnBlitPad[0] == g_pnCtlB.apPads[0] && g_apnPnBlitPad[1] == g_pnCtlA.apPads[0], "CtlB first, then CtlA");
    g_pnCtlA.pfnClick = NULL;
    g_pnCtlB.pfnClick = NULL;
    PnRun();
    Check(g_apnPnBlitHandler[0] == PnBeginCraftSelect, "CtlB is blitted with Smap_BeginCraftSelect @ 0x838dc installed (0x91A0C before 0x91A14)");
    Check(g_apnPnBlitHandler[1] == PnResolve, "CtlA gets Smap_ResolveInterceptUI before its 0x91ACC blit (0x91AC4)");
    PnRowIs(PUFO_STATUS_TARGET_Y_CANCEL_LBL, "m4", "TEXTS[1127] centred at y=103 (copy 0x91A18..0x91A38 with R8, then 0x91A64)");
    PnRowIs(PUFO_STATUS_TARGET_Y_CANCELLED_LBL, "B2", "TEXTS[1144] centred at y=126 (copy 0x91AD0..0x91AF0 with R9, then 0x91B1C)");

    /* ---- 4. the soldiers-aboard gate ---- */
    PnFixture(0x16, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnBeginCraftSelect, "a soldier aboard craft 4 (colour SLOT 0, 0x91A68..0x91A80) keeps Smap_BeginCraftSelect");
    PnFixture(0x16, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(3); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(g_pnCtlB.pfnClick == PnResolve, "an empty craft replaces it with Smap_ResolveInterceptUI (0x91A8C..0x91A98)");
    Check(g_apnPnBlitHandler[0] == PnBeginCraftSelect, "and the replacement is after the blit, as in 0x17");

    /* ---- 5. whole-frame equality with the other descent arms ---- */
    PnFixture(0x17, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    PnSaveFrame();
    PnFixture(0x16, 4, 1, 0); PnSetPanelRows(); PnSetSoldierAboard(4); g_pnPanel.nUnk3032 = 3;
    PnCapture();
    CheckEqI32(PnFrameDiff(), 0, "0x16 draws EXACTLY what 0x17 draws: 117 of the clone's 121 instructions are identical and the other 4 are equivalent");
    PnFixture(0x15, 4, 1, 0); PnSetPanelRows(); PnSetMissionSites(); PnSetTargetIdx(6, 5); PnSetSoldierAboard(4);
    g_pnPanel.nUnk3032 = 3;
    PnCapture();
    Check(PnFrameDiff() > 0, "and the compare is not blind: the 0x15 arm changes the frame (city line at 86, labels at 106 and 129)");
}

/* Flag 0x3C (test 0x91B20..0x91B30, body 0x91B34..0x91CE8): the alien-base panel. Three records, two controls,
 * no colour slot, no strateg entry, no panel row and no gate -- and both handlers stored AFTER both blits. */
static void TestStatusPanelAlienBase(void)
{
    printf("\nGeoscape_DrawStatusPanel, the alien-base panel @ 0x91b34 (flag 0x3C)\n");
    PnFixture(0x3C, 4, 1, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }

    /* ---- 1. the widest anim in the chain: x 2, nW 0xE8 ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(d.nMinX, 2, "the anim body starts at x=2 (0x91B64 MOV R1,#2), not the usual 14");
    CheckEqI32(d.nMinY, PUFO_STATUS_TARGET_ANIM_Y, "and y=60 (0x91B5C MOV R2,#0x3C)");
    CheckEqI32(d.nMaxX, 2 + 0xE8 + 3, "nW 0xE8 (0x91B6C MOV R3,#0xE8) puts the right border at 237");
    CheckEqI32(d.nMaxY, PUFO_STATUS_TARGET_ANIM_Y + PUFO_STATUS_TARGET_NH + 5, "nH 0x5A (0x91B50/0x91B60) puts the bottom at 155");

    /* ---- 2. the three records ---- */
    PnFixture(0x3C, 4, 1, 0);
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "gggg6", "y=70 is TEXTS[1445] alone (copy 0x91B88..0x91B98, 0x91B74 = unk_FBDF1); no row is appended");
    PnRowIs(PUFO_STATUS_BASE_Y_CONFIRM_LBL, "h6", "y=100 is TEXTS[1446] \"Центр на базу\" (copy 0x91C08..0x91C18, 0x91BF4 = unk_FBEBE)");
    PnRowIs(PUFO_STATUS_TARGET_Y_CANCELLED_LBL, "B2", "y=126 is TEXTS[1144] \"Отмена\" (0x91C74 MOV R1,#0x39418 = 1144*205, copy 0x91C8C..0x91C9C)");
    /* 1127 and 1239 are the descent arms' records; here they must not be read at all. Dropping one and
     * comparing the whole frame is the check -- a row-band emptiness test would be confounded by the ink of
     * the y=100 row, which reaches down into it. */
    PnFixture(0x3C, 4, 1, 0);
    PnCaptureDropped(1127);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0, "TEXTS[1127] is not drawn by this arm at all");
    PnFixture(0x3C, 4, 1, 0);
    PnCaptureDropped(1239);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0, "nor is TEXTS[1239]");
    PnFixture(0x3C, 4, 1, 0);
    PnCaptureDropped(1445);
    Check(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount > 0, "while dropping TEXTS[1445] does change the frame, so the two checks above are not blind");
    PnFixture(0x3C, 4, 1, 0);
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "", "and y=86 stays empty: no city line here");

    /* ---- 3. the two controls ---- */
    PnFixture(0x3C, 4, 1, 0);
    PnCapture();
    CheckEqI32(g_pnCtlConfirm.x, PUFO_STATUS_TEXT_X_CENTRE - 20, "CtlConfirm centres on 120 from its own 40-wide pad (0x91BD0 LDRH [R0,#4])");
    CheckEqI32(g_pnCtlConfirm.y, PUFO_STATUS_BASECTL_Y_CONFIRM, "at y=100 (0x91BE0 MOV R3,#0x64), the same row its label is drawn on");
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 20, "CtlA centres from its own 40-wide pad (0x91C50)");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_TARGETCTL_Y_OK2, "at y=125 (0x91C60 MOV R3,#0x7D)");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "two control blits (0x91BF0 and 0x91C70), then the tail's thirteen root widgets");
    Check(g_apnPnBlitPad[0] == g_pnCtlConfirm.apPads[0] && g_apnPnBlitPad[1] == g_pnCtlA.apPads[0], "CtlConfirm first, then CtlA");

    /* ---- 4. handlers are stored AFTER both blits ---- */
    /* ONE run with both handlers cleared: PnCapture would run the arm twice and the second blit would see the
     * first run's handler already installed. */
    PnFixture(0x3C, 4, 1, 0);
    g_pnCtlConfirm.pfnClick = NULL;
    g_pnCtlA.pfnClick = NULL;
    PnRun();
    Check(g_apnPnBlitHandler[0] == NULL && g_apnPnBlitHandler[1] == NULL,
          "neither control has a handler at the moment it is blitted (0x91BF0 and 0x91C70 come before 0x91CD4 / 0x91CE8)");
    Check(g_pnCtlConfirm.pfnClick == PnSelectTarget, "CtlConfirm then gets Smap_OnPointerUp @ 0x84688 (0x91CD0/0x91CD8)");
    Check(g_pnCtlA.pfnClick == PnClearSel, "and CtlA gets Smap_ClearSelection @ 0x84648 (0x91CE0/0x91CE8)");
    CheckEqI32(g_nPnSelectClicks + g_nPnClearClicks, 0, "the arm installs handlers, it never calls one");

    /* ---- 5. it reads none of the things the other arms read ---- */
    PnFixture(0x3C, 4, 1, 0);
    g_pnPanel.nUnk3032 = 3;
    g_bPnMapSel = 0;
    PnCapture();
    PnSaveFrame();
    PnFixture(0x3C, 255, 7, 9);
    g_pnPanel.nUnk3032 = 12;
    g_pnStrat[0].bCraftClass = 3;
    g_pnStrat[1].nTargetIdx = 4;
    g_bPnMapSel = 1;
    PnCapture();
    CheckEqI32(PnFrameDiff(), 0,
               "colour slots, the selected row, the strateg entry and bMapSelectionFlag change nothing here");
}

static void TestStatusPanelMissionSite(void)
{
    printf("\nGeoscape_DrawStatusPanel, the intercept-mission panel @ 0x90d78\n");
    PnFixture(0x13, 4, 1, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetSiteFixture();

    /* ---- 1. the body ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(d.nMinX, 0xE, "the anim body starts at x=14 (0x914D4-side MOV R1,#0xE at 0x90D9C)");
    CheckEqI32(d.nMinY, PUFO_STATUS_TARGET_ANIM_Y, "and y=60 (0x90DA0 MOV R2,#0x3C)");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x90DDC) puts the right border at 225");
    CheckEqI32(d.nMaxY, PUFO_STATUS_TARGET_ANIM_Y + PUFO_STATUS_TARGET_NH + 5,
               "nH 0x5A (0x90DCC then 0x90DD8) puts the bottom at 155");

    /* ---- 2. the anim arguments, against the 0x0F arm's identical call ---- */
    PnFixture(0x0F, 4, 1, 0);
    PnEmptySources();
    PnRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
    PnFixture(0x13, 4, 1, 0);
    PnSetSiteFixture();
    PnEmptySources();
    PnRun();
    CheckEqI32(PnDiffRect(20, 120, 219, 145).nCount, 0,
               "body ink equals the 0x0F panel's: nKey 0xB4 (0x90DAC), src 0x28/0x32, nMode and nIdx 0 (0x90DB0)");

    /* ---- 3. line 1 is the panel's SELECTED row, not the colour slot ---- */
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture();
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "k6", "y=70 is apLines[panel->nUnk3032] ALONE (0x90DF4 LDR [R0,R11], 0x90DFC LDR [R3,R10])");
    PnFixture(0x13, 3, 1, 0); PnSetSiteFixture(); g_pnPanel.nUnk3032 = 4;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "kk6", "and the colour slot is never read here: slot 0 says 3, the panel says 4");
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture(); g_pnPanel.nUnk3032 = 25;
    PnCapture();
    d = PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                   PUFO_STATUS_TARGET_Y_LINE + PN_BAND);
    CheckEqI32(d.nCount, 0, "row 25 is past the panel's 20 rows: the port draws nothing, the original hands the NULL to strcpy (bugs.tsv)");
    Check(PnDiffRect(0, PUFO_STATUS_SITE_Y_LINE2 + PN_GLYPH_DY, BD_SURF_W - 1,
                     PUFO_STATUS_SITE_Y_LINE2 + PN_BAND).nCount > 0,
          "while the second line still draws, so the missing first row is the guard and not a dead arm");
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture(); g_pnPanel.nUnk3032 = -1;
    PnCapture();
    CheckEqI32(PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                          PUFO_STATUS_TARGET_Y_LINE + PN_BAND).nCount, 0,
               "and a negative selected row is guarded too -- UiPanel_TrackHoldRow's cursor + row is a dword");
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture();

    /* ---- 4. line 2: TEXTS[1232] + the mission-site name at the SLOT 1 entry's +0x02 ---- */
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6nn6",
            "y=86 is TEXTS[1232] + aMissionSites[6] (0x90E94 LDRSB [R0,#2], 0x90E9C MUL #0x18, 0x90EA0 ADD #0xC8, 0x90EA8 ADD #4)");
    PnFixture(0x13, 4, 0, 0); PnSetSiteFixture();
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6p6", "colour SLOT 0 picks the entry, so the neighbour's class reads the neighbour's site (0x90E74 MOV R2,#1 -> 0 here)");
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture(); g_pnStrat[1].bCraftClass = 5;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6m6", "the byte at +0x02 IS the site index for an intercept item (BasePlaneScreen_AddInterceptMission @ 0x7de14 indexes 0x1865C0 with the same value it stores at +0x02)");
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture(); g_pnStrat[1].bCraftClass = PUFO_MISSION_SITE_COUNT;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6", "index 56 is past the 56 records: guarded here, untested in the original (0x90E9C MUL, 0x90EA4 ADD)");
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture(); g_pnStrat[1].bCraftClass = 0xFF;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6", "and LDRSB makes 0xFF negative -- the original would read BEFORE 0x1865C0");
    PnFixture(0x13, 4, 5, 0); PnSetSiteFixture();
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6", "an entry index past the strateg list yields no entry, so no name (0x90E8C)");

    /* ---- 5. the clear arm's control and labels ---- */
    g_pnCtlA.uWidth = 30;
    PnCapture();
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 15, "CtlA centres on 120 from ITS OWN 30-wide pad (0x90EE4 LDRH [R0,#4])");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_SITECTL_Y_OK, "at y=115 (0x90EF4)");
    CheckEqI32(g_pnCtlB.y, 0, "CtlB is not touched when the flag is clear");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 1, "one control blit (0x90F10) plus the tail's thirteen");
    Check(g_pnCtlA.pfnClick == PnResolve, "CtlA gets Smap_ResolveInterceptUI (0x90F04 / 0x90F08)");
    g_pnCtlA.pfnClick = NULL;
    PnRun();
    Check(g_apnPnBlitHandler[0] == PnResolve, "installed before the blit (0x90F08 then 0x90F10)");
    PnRowIs(PUFO_STATUS_SITE_Y_OK_LBL, "m4", "TEXTS[1127] centred at y=116 (0x90F44 then 0x9118C)");

    /* ---- 6. the set arm ---- */
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture(); g_bPnMapSel = 1;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "aaa6k6", "the set arm starts from TEXTS[1238] and APPENDS the row (0x90F7C, 0x90FB8)");
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture(); g_bPnMapSel = 1;
    PnRowIs(PUFO_STATUS_SITE_Y_LINE2, "q6nn6", "and it builds the same \"Город: \" line as the clear arm (0x90FE8, 0x91050)");
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture(); g_bPnMapSel = 1;
    g_pnCtlB.uWidth = 30;
    PnCapture();
    CheckEqI32(g_pnCtlB.x, PUFO_STATUS_TEXT_X_CENTRE - 15, "CtlB centres from ITS OWN 30-wide pad (0x91088)");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_SITECTL_Y_CANCEL, "at y=105 (0x91098) -- not the 102 the 0x14 arm uses");
    Check(g_pnCtlB.pfnClick == PnCancelTarget, "CtlB gets Smap_CancelTarget (0x910A8 / 0x910AC)");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_SITECTL_Y_OK2, "CtlA sits at y=128 (0x91120) -- not the 0x14 arm's 125");
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 20, "centred from its own 40-wide pad (0x91110)");
    Check(g_pnCtlA.pfnClick == PnResolve, "with Smap_ResolveInterceptUI (0x91130 / 0x91134)");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "two controls, one blit each, then the tail's thirteen root widgets");
    Check(g_apnPnBlitPad[0] == g_pnCtlB.apPads[0] && g_apnPnBlitPad[1] == g_pnCtlA.apPads[0],
          "CtlB first (0x910B4), CtlA second (0x9113C)");
    g_pnCtlB.pfnClick = NULL;
    g_pnCtlA.pfnClick = NULL;
    PnRun();
    Check(g_apnPnBlitHandler[0] == PnCancelTarget, "CtlB's handler precedes its blit");
    Check(g_apnPnBlitHandler[1] == PnResolve, "and CtlA's precedes its 0x9113C blit");
    PnRowIs(PUFO_STATUS_SITE_Y_CANCEL_LBL, "m4", "TEXTS[1127] centred at y=106 (0x910F8)");
    PnRowIs(PUFO_STATUS_SITE_Y_CANCELLED_LBL, "B2", "TEXTS[1144] centred at y=129 (0x91170 then 0x9118C)");

    /* ---- 7. the records, one at a time ---- */
    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture();
    PnCaptureDropped(PUFO_STATUS_TEXT_CITY);
    d = PnDiffRect(0, PUFO_STATUS_SITE_Y_LINE2 + PN_GLYPH_DY, BD_SURF_W - 1,
                   PUFO_STATUS_SITE_Y_LINE2 + PN_BAND);
    Check(d.nCount > 0, "TEXTS[1232] is the second line's only record source");
    CheckEqI32(PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                          PUFO_STATUS_TARGET_Y_LINE + PN_BAND).nCount, 0,
               "and it does not touch the first line, which is the panel row");
    PnCaptureDropped(PUFO_STATUS_TEXT_CRASH);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "this arm never reads TEXTS[1237]: its first line comes from the panel, not a record");
    PnCaptureDropped(1360);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and no size record: the +0x02 byte names a mission site here");

    PnFixture(0x13, 4, 1, 0); PnSetSiteFixture(); g_bPnMapSel = 1;
    PnCaptureDropped(PUFO_STATUS_TEXT_MISSION_PREFIX);
    d = PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                   PUFO_STATUS_TARGET_Y_LINE + PN_BAND);
    Check(d.nCount > 0, "TEXTS[1238] feeds the set arm's first line");
    g_bPnMapSel = 0;
}

static void TestStatusPanelTravel(void)
{
    printf("\nGeoscape_DrawStatusPanel, the travel panel @ 0x91190\n");
    PnFixture(0x12, 4, 1, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetSiteFixture();

    /* ---- 1. the body, and the one thing this arm has less than 0x14 ---- */
    PnCaptureFrameOnly();
    PnDiff d = PnDiffRect(0, 0, BD_SURF_W - 1, PN_TAIL_INK_Y - 1);
    CheckEqI32(d.nMinX, 0xE, "the anim body starts at x=14 (0x911A8 MOV R1,#0xE)");
    CheckEqI32(d.nMinY, PUFO_STATUS_TARGET_ANIM_Y, "and y=60 (0x911AC)");
    CheckEqI32(d.nMaxX, 0xE + 0xD0 + 3, "nW 0xD0 (0x911E8) puts the right border at 225");
    CheckEqI32(d.nMaxY, PUFO_STATUS_TARGET_ANIM_Y + PUFO_STATUS_TARGET_NH + 5,
               "nH 0x5A (0x911D8 then 0x911E4) puts the bottom at 155");
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "kk6",
            "y=70 is apLines[colour slot 0] ALONE -- no TEXTS record, no space, no size word (0x91218..0x9122C, 0x91258)");
    PnCaptureDropped(1360);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "no size word here, which is the ONLY line this arm differs from 0x14 on");
    PnFixture(0x12, 3, 1, 0); PnSetSiteFixture();
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "k6", "colour SLOT 0 is the row index, exactly as in the 0x14 arm (0x911F8 MOV R2,#0)");
    PnFixture(0x12, 3, 1, 0); PnSetSiteFixture(); g_pnPanel.nUnk3032 = 4;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "k6", "and NOT the panel's selected row -- that is the 0x13 arm's read");
    PnFixture(0x12, 25, 1, 0); PnSetSiteFixture();
    PnCapture();
    CheckEqI32(PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                          PUFO_STATUS_TARGET_Y_LINE + PN_BAND).nCount, 0,
               "row 25 is past the panel's 20 rows: guarded, unguarded in the original (0x91210)");

    /* ---- 2. the clear arm's control and label ---- */
    PnFixture(0x12, 4, 1, 0); PnSetSiteFixture();
    g_pnCtlA.uWidth = 30;
    PnCapture();
    CheckEqI32(g_pnCtlA.x, PUFO_STATUS_TEXT_X_CENTRE - 15, "CtlA centres from ITS OWN 30-wide pad (0x91264)");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_TARGETCTL_Y_OK, "at y=115 (0x91274), the same row 0x14 uses");
    CheckEqI32(g_pnCtlB.y, 0, "CtlB untouched while the flag is clear");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 1, "one control blit (0x91290) plus the tail's thirteen");
    Check(g_pnCtlA.pfnClick == PnResolve, "CtlA gets Smap_ResolveInterceptUI (0x91284 / 0x91288)");
    g_pnCtlA.pfnClick = NULL;
    PnRun();
    Check(g_apnPnBlitHandler[0] == PnResolve, "installed before the blit, as in 0x14");
    PnRowIs(PUFO_STATUS_TARGET_Y_OK_LBL, "m4", "TEXTS[1127] centred at y=116 (0x912C4 then 0x91480)");

    /* ---- 3. the set arm ---- */
    PnFixture(0x12, 4, 1, 0); PnSetSiteFixture(); g_bPnMapSel = 1;
    PnRowIs(PUFO_STATUS_TARGET_Y_LINE, "aaa6kk6",
            "TEXTS[1238] + apLines[slot 0] (0x912FC, 0x91344), no space and no size word");
    PnFixture(0x12, 4, 1, 0); PnSetSiteFixture(); g_bPnMapSel = 1;
    g_pnCtlB.uWidth = 30;
    PnCapture();
    CheckEqI32(g_pnCtlB.x, PUFO_STATUS_TEXT_X_CENTRE - 15, "CtlB centres from ITS OWN 30-wide pad (0x9137C)");
    CheckEqI32(g_pnCtlB.y, PUFO_STATUS_TARGETCTL_Y_CANCEL, "at y=102 (0x9138C) -- the 0x14 arm's row, not the 0x13 arm's 105");
    Check(g_pnCtlB.pfnClick == PnCancelTarget, "CtlB gets Smap_CancelTarget (0x9139C / 0x913A0)");
    CheckEqI32(g_pnCtlA.y, PUFO_STATUS_TARGETCTL_Y_OK2, "CtlA at y=125 (0x91414)");
    Check(g_pnCtlA.pfnClick == PnResolve, "with Smap_ResolveInterceptUI (0x91424 / 0x91428)");
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS + 2,
               "two controls, one blit each, then the tail's thirteen root widgets");
    PnRowIs(PUFO_STATUS_TARGET_Y_CANCEL_LBL, "m4", "TEXTS[1127] centred at y=103 (0x913EC)");
    PnRowIs(PUFO_STATUS_TARGET_Y_CANCELLED_LBL, "B2", "TEXTS[1144] centred at y=126 (0x91464)");
    PnFixture(0x12, 4, 1, 0); PnSetSiteFixture(); g_bPnMapSel = 1;
    PnCaptureDropped(PUFO_STATUS_TEXT_MISSION_PREFIX);
    d = PnDiffRect(0, PUFO_STATUS_TARGET_Y_LINE + PN_GLYPH_DY, BD_SURF_W - 1,
                   PUFO_STATUS_TARGET_Y_LINE + PN_BAND);
    Check(d.nCount > 0, "TEXTS[1238] feeds the set arm's line");
    PnCaptureDropped(PUFO_STATUS_TEXT_CRASH);
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1).nCount, 0,
               "and TEXTS[1237] belongs to 0x14 only");
    g_bPnMapSel = 0;
}

/* 0x91CEC..0x91DB0: the tail every arm of the chain lands on. Flag 0x1E with the multi-select panel flag clear
 * reaches it with NO arm body at all -- 0x8E5E0's BEQ skips the panel anim and the panel text, dwActionFail 0
 * skips the action-fail line at 0x8E658, and no flag test in 0x8E708..0x91B30 matches 0x1E -- so everything on
 * the surface is the tail's own work. */
static void TestStatusPanelChainTail(void)
{
    printf("\nGeoscape_Draw's chain tail @ 0x91ceC\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    PnSetClockTime();

    /* ---- 1. the not-2 branch: the strip pad, the clock, then the thirteen root widgets ---- */
    PnCaptureFrameOnly();
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS,
               "the tail blits exactly the root block's thirteen widgets and nothing else (0x91D1C..0x91D38)");
    bool bRootOrder = true;
    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        if (g_apnPnBlitPad[i] != &g_aPnRootPad[i]) {
            bRootOrder = false;
        }
    }
    Check(bRootOrder, "in slot order over 0x1864F8 + 4*i, the loop whose counter the original keeps in dword_186B08");
    Check(g_apnPnBlitPad[0] != g_pnCtlClose.apPads[0], "the close control is NOT blitted on this branch (0x91D3C ends the frame)");
    CheckEqI32(BdPix(30, 250), 0x2F00,
               "*0x186BBC is blitted as a PAD at its own origin (0x91CFC / 0x91D04), glyph index 11");
    /* The pad's 8 glyph indices are 11..18 in row-major order for a 4x2 pad, and palette slot 20 maps index
     * 11 + i to 0x2F00 + i, so the far corner of the glyph is 0x2F07. */
    CheckEqI32(BdPix(33, 251), 0x2F07, "and it is the pad's own 4x2 glyph row-major (11..14, 15..18), not a blended rectangle");
    CheckEqI32(g_pnClockPad[0].nOriginX, 140, "Geoscape_DrawClockPads ran at 0x91D0C: the seconds-tens pad went to x=140");
    CheckEqI32(g_pnClockPad[0].nOriginY, 260, "on the time row 260");
    CheckEqI32(g_pnClockPad[9].nOriginX, 135, "and the last 9 of 1999 ends the run at x=135");
    CheckEqI32(g_pnClockPad[9].nOriginY, 304, "on the year row 304, the lowest ink in the frame");
    Check(g_pnCtlA.y == 0 && g_pnCtlB.y == 0 && g_pnCtlConfirm.y == 0,
          "the tail moves no panel control -- it has none of its own");
    CheckEqI32(g_bPnPendRegion, 0, "and bPendingRegionId @ 0x159DF4 is untouched on this branch");

    PnFixture(0x1E, 0, 0, 0); PnSetClockTime();
    g_nPnScreenId = 3;                 /* what LoadGame leaves there (0x6B1AC..0x6B1B0 stores 3) */
    PnCaptureFrameOnly();
    CheckEqI32(g_nPnBlits, PUFO_GEOUI_ROOT_CTLS,
               "screen id 3 takes the same branch: 0x91CF4 is CMP #2 / BEQ, not a range test");
    CheckEqI32(BdPix(30, 250), 0x2F00, "with the strip pad blitted here too");

    /* ---- 2. the == 2 branch: the new-base globe hover, then the close control ---- */
    PnFixture(0x1E, 0, 0, 0);
    GhFixture(PUFO_REGION_EUROPE);
    /* The byte at 0x1870AC is one global in the original: 0x679A8 reads it for the hover's cost line and
     * 0x91D98 for the close blit, so the test points both env fields at the same variable. */
    g_ghEnv.pbPlacementPending = &g_bPnPlacePending;
    g_pnEnv.pGlobeEnv   = &g_ghEnv;
    g_nPnScreenId       = PUFO_STATUS_SCREEN_ID_GLOBE_HOVER;
    g_ghScanX  = g_ghSetScanX;
    g_ghSweepV = g_ghSetSweepV;
    g_ghSweepH = g_ghSetSweepH;
    g_ghAnimStarted  = 1;
    g_ghAnimProgress = 100;            /* the panel frame is complete, so 0x67490 does not return early */
    g_ghView.nPickX  = 111;            /* the PREVIOUS pick, which the pending copy must see */
    g_ghView.nPickY  = 222;
    g_ghCtlBlits     = 0;
    for (i32 i = 0; i < PUFO_GEOUI_CLOCK_DIGIT_PADS; ++i) {
        g_pnClockPad[i].nOriginX = -1; /* sentinels: a clock run is then visible */
        g_pnClockPad[i].nOriginY = -1;
    }
    PnRun();
    CheckEqI32(g_nPnBlits, 1, "the only control blit is *g_pUiCloseCtl's (0x91DA8 / 0x91DAC, the blit at 0x93058)");
    Check(g_apnPnBlitPad[0] == g_pnCtlClose.apPads[0], "and it is the close control's own pad");
    CheckEqI32(g_bPnPendRegion, PUFO_REGION_EUROPE,
               "the hovered tile is stored with STRB at 0x91D94 into bPendingRegionId @ 0x159DF4");
    CheckEqI32(g_ghPendX, 111, "the hover's pending copy ran through the tail: it stores the pick from BEFORE its own lookup (0x6786C)");
    CheckEqI32(g_ghPendY, 222, "for both axes");
    CheckEqI32(g_ghView.nPickX, 50, "and the lookup itself landed on tile (50, 0), what (120, 110) resolves to");
    Check(g_pnClockPad[0].nOriginX == -1 && g_pnClockPad[9].nOriginY == -1,
          "no clock on this branch -- 0x91CF8's BEQ jumps over the strip blit and the clock call");
    /* The hover paints its own wash over 0x0,0 + 0xF0x0x140 first (0x672C8..0x672D8), so this pixel is not
     * zero here; what must be absent is the strip pad's glyph colour, which only PadImage_Blit writes. */
    Check((u16)BdPix(30, 250) != 0x2F00, "and the strip pad is not blitted on this branch (the pixel holds the hover's wash instead)");
    Check(g_ghCtlBlits > 0, "the hover itself blitted its Build base control");

    g_bPnPlacePending  = 1;            /* 0x1870AC set: the same byte the hover's cost line reads */
    g_ghScanX  = g_ghSetScanX;
    g_ghSweepV = g_ghSetSweepV;
    g_ghSweepH = g_ghSetSweepH;
    g_ghAnimStarted  = 1;
    g_ghAnimProgress = 100;
    g_ghView.nPickX  = 111;            /* re-applied, exactly as GhRun re-applies them per frame */
    g_ghView.nPickY  = 222;
    PnRun();
    CheckEqI32(g_nPnBlits, 0, "with the byte set, 0x91DA4's BNE skips the close blit and the frame ends");
    CheckEqI32(g_bPnPendRegion, PUFO_REGION_EUROPE, "but the tile is stored first: the gate sits AFTER 0x91D94");
    CheckEqI32(g_ghPendX, 111, "and the hover ran again, gate or no gate");

    g_bPnPlacePending = 0;
    g_pnEnv.pGlobeEnv = NULL;          /* no other suite supplies a hover env */
}

/* 0x944B4..0x94550: the frame end every arm of the screen switch at 0x8C1B0 lands on (and the block case 20
 * falls through into at 0x944B0). Driven directly -- the dispatcher itself is not ported yet. */
static i32        g_nFePresents;
static i32        g_nFeListMode, g_nFePhaseSave, g_nFeLimit, g_nFeCooldown;
static u8         g_bFeActive, g_bFeShown;
static UiPanel    g_fePanel;
static UiPanel   *p_fePanel = &g_fePanel;
static PadImage  *p_fePad = NULL;
static GfxColorEnv g_feColor;
static UiModalAnim   g_feModal;
static UiModalLayout g_feLayout;
static BaseAtDrawEnv   g_feBaseAt;
static GeoscapeFrameEndEnv g_feEnv;

static i32 FePresent(void *pUser)
{
    (void)pUser;
    ++g_nFePresents;
    return 7;
}

/* One frame end, from a known start state: the modal's own state is re-applied per run, exactly as the
 * original takes it fresh out of the Map block each frame. */
static i32 FeRun(i32 nMode)
{
    g_nFeListMode = nMode;
    /* A completed typewriter pass: at 40 the callee wraps the cursor to 1 and clears the progressive flag
     * (the block the port notes at 0x26744), which is the state where WHOLE lines are drawn. A cursor of 0
     * would set bProgressive and draw a zero-length prefix instead -- no text at all. */
    g_feModal.nCursor      = 40;
    g_feModal.bProgressive = 0;
    g_feModal.nAnimPhase  = 0;
    g_feModal.nBand       = 0;
    g_feModal.nSelButton  = 0;
    memset(g_bdSurf.pPixels, 0, (size_t)BD_SURF_W * (size_t)BD_SURF_H * 2u);
    g_nFePresents = 0;
    return Geoscape_DrawFrameEnd(&g_feEnv);
}

static void TestGeoscapeFrameEnd(void)
{
    printf("\nGeoscape_Draw's frame end @ 0x944b4\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }

    memset(&g_fePanel, 0, sizeof(g_fePanel));
    g_fePanel.nLineCount = 0;                 /* equal to nLimit: BaseAt's confirm-shown path runs (0x587D8) */
    g_feColor.pnGamma = &g_nGammaOne;
    memset(&g_feModal, 0, sizeof(g_feModal));
    g_feModal.pColor = &g_feColor;
    memset(&g_feLayout, 0, sizeof(g_feLayout));
    memset(&g_feBaseAt, 0, sizeof(g_feBaseAt));
    g_feBaseAt.pColor           = &g_feColor;
    g_feBaseAt.pbActive         = &g_bFeActive;
    g_feBaseAt.ppPad            = &p_fePad;   /* NULL pad: the pad blit is skipped, the rest still runs */
    g_feBaseAt.ppPanel          = &p_fePanel;
    g_feBaseAt.pnLineLimit      = &g_nFeLimit;
    g_feBaseAt.pnRadarCooldownR = &g_nFeCooldown;
    g_feBaseAt.pbConfirmShown   = &g_bFeShown;
    g_feBaseAt.pTextRecs        = g_abPnText;
    memset(&g_feEnv, 0, sizeof(g_feEnv));
    g_feEnv.pBaseAtEnv           = &g_feBaseAt;
    g_feEnv.pnListMode           = &g_nFeListMode;
    g_feEnv.pnActionPhaseSave112 = &g_nFePhaseSave;
    g_feEnv.pModalAnim           = &g_feModal;
    g_feEnv.pModalLayout         = &g_feLayout;
    g_feEnv.pTextRecs            = (const u8 *)g_abPnText;
    g_feEnv.pfnPresent           = FePresent;
    g_bFeActive = 1;
    g_nFeLimit = 0;
    g_nFeCooldown = 0;

    /* ---- 1. mode 0: BaseAt draws, no dialog, present once ---- */
    g_nFePhaseSave = -1;
    g_bFeShown = 0;
    const i32 nRet = FeRun(0);
    CheckEqI32(nRet, 7, "the frame end hands back the present thunk's value (0x94538 MOV R0,R4 / 0x9453C)");
    CheckEqI32(g_nFePresents, 1, "GX_Present runs exactly once -- which is why Render_Frame's geoscape branch skips its own");
    CheckEqI32(g_bFeShown, 1, "BaseAtScreen_Draw runs first (0x944B4): its confirm-shown byte is set");
    CheckEqI32(g_nFePhaseSave, -1, "mode 0 takes neither dialog arm, so Map.anActionPhaseSave0[112] is untouched");
    CheckEqI32(PnDiffRect(0, 0, BD_SURF_W - 1, 285).nCount, 0, "nothing above BaseAt's confirm row");
    Check(PnDiffRect(0, 286, BD_SURF_W - 1, 305).nCount > 0,
          "BaseAt's own label at PUFO_BASEAT_CONFIRM_Y 290 is the only ink here");
    PnSaveFrame();                            /* the no-dialog frame: every later compare is against it */

    /* ---- 2. mode 4: 40 into the phase save, then the TEXTS[1103] line ---- */
    FeRun(4);
    CheckEqI32(g_nFePhaseSave, PUFO_GEOFRAME_PHASE_SAVE,
               "mode 4 writes 40 (0x944CC MOV R0,#0x28 stored at 0x944E0, Map+0x112)");
    Check(g_feModal.nCursor != 0, "the modal renderer ran: its typewriter cursor advanced");
    Check(PnFrameDiff() > 0, "mode 4 adds ink over the mode-0 frame");
    /* The dialog's own box, pinned from the formulas the renderer uses: nWidth 150 gives nX = (240-150)/2 = 45,
     * and nLines 1 gives nHeight = 13 + 10 = 23 with nY = (320-23)/2 = 148. */
    CheckEqI32(PnDiffRect(0, 120, BD_SURF_W - 1, 147).nCount, 0, "nothing above the dialog: its top border is y=148");
    CheckEqI32(PnDiffRect(0, 171, BD_SURF_W - 1, 240).nCount, 0, "and nothing below it: 148 + 23 - 1 = 170 is the last row");
    CheckEqI32(PnDiffRect(0, 148, 44, 170).nCount, 0, "nothing left of x=45 either ((240 - 150) / 2)");
    Check((u16)BdPix(45, 148) != 0, "the body's top-left corner is drawn (0x26530, x = (240 - 150) / 2)");
    Check((u16)BdPix(195, 148) != 0, "the right border is at nX + nWidth = 195, one column outside the body -- the 0x265D4 quirk");
    Check((u16)BdPix(197, 148) == 0, "and it is two columns wide, no further");
    PnSaveFrame();                            /* the mode-4 frame */
    FeRun(4);
    CheckEqI32(PnFrameDiff(), 0, "the same mode drawn twice is the same frame");
    PnSetText(1103, "");
    FeRun(4);
    Check(PnFrameDiff() > 0, "TEXTS[1103] is the record mode 4 draws (0x440 ORR #0xF at 0x944D4 / 0x944EC)");
    PnSetText(1103, "u5u5u5");
    PnSetText(1102, "");
    FeRun(4);
    CheckEqI32(PnFrameDiff(), 0, "and mode 4 does NOT draw TEXTS[1102]: blanking it changes nothing");
    PnSetText(1102, "t2t2");

    /* ---- 3. mode 3: the same shape, the other record ---- */
    FeRun(3);
    CheckEqI32(g_nFePhaseSave, PUFO_GEOFRAME_PHASE_SAVE, "mode 3 writes the same 40 (0x94508 / 0x9451C)");
    PnSaveFrame();                            /* the mode-3 frame */
    FeRun(3);
    CheckEqI32(PnFrameDiff(), 0, "mode 3 twice is the same frame");
    PnSetText(1102, "");
    FeRun(3);
    Check(PnFrameDiff() > 0, "TEXTS[1102] is the record mode 3 draws (0x440 ORR #0xE at 0x94510 / 0x94528)");
    PnSetText(1102, "t2t2");
    PnSetText(1103, "");
    FeRun(3);
    CheckEqI32(PnFrameDiff(), 0, "and mode 3 does NOT draw TEXTS[1103]");
    PnSetText(1103, "u5u5u5");
    FeRun(4);
    Check(PnFrameDiff() > 0, "mode 4 and mode 3 are different frames -- the two arms are not the same dialog");

    /* ---- 4. every other mode takes no dialog ---- */
    FeRun(1);
    PnSaveFrame();
    g_nFePhaseSave = -1;
    FeRun(0xC);                               /* 12: a screen id the switch has an arm for, but no dialog */
    CheckEqI32(g_nFePhaseSave, -1, "mode 12 writes no phase save");
    CheckEqI32(PnFrameDiff(), 0, "and its frame is the mode-1 frame: neither dialog arm ran");
    g_nFePhaseSave = -1;
    FeRun(13);                                /* the default case's own id */
    CheckEqI32(g_nFePhaseSave, -1, "screen 13, which arrives straight from the jumptable, takes no dialog either");
    CheckEqI32(g_nFePresents, 1, "and it still presents");

    /* ---- 5. the two guards the original has no need of ---- */
    g_bFeActive = 0;                          /* BaseAt's own gate at 0x5873C */
    g_bFeShown = 0;
    FeRun(0);
    CheckEqI32(g_bFeShown, 0, "with the BaseAt gate clear nothing is drawn under it");
    CheckEqI32(g_nFePresents, 1, "while the frame end keeps running to the present");
    g_bFeActive = 1;
    g_feEnv.pBaseAtEnv = NULL;
    g_nFePhaseSave = -1;
    FeRun(3);
    CheckEqI32(g_nFePresents, 1, "a fixture with no BaseAt env still presents");
    CheckEqI32(g_nFePhaseSave, PUFO_GEOFRAME_PHASE_SAVE, "and the dialog arm still runs");
    g_feEnv.pBaseAtEnv = &g_feBaseAt;
    g_feEnv.pfnPresent = NULL;
    CheckEqI32(FeRun(0), 0, "and with no present hook installed the frame end returns 0");
    g_feEnv.pfnPresent = FePresent;
}

/* ---- Geoscape_Draw's short screen arms, 0x93DD4..0x93F04 and 0x94114..0x941A4 --------------------
 *
 * The four thin arms: a pad blit, a run of the screen's own control pads, the screen frame, and for
 * screens 10, 11 and 12 the buy/sell list tail. Every callee env here is zeroed except the fields its
 * first, ungated block needs, so what is measured is the ARM's own work. UiControl_BlitActivePad offers
 * the control's PAD to PnBlit, which records it without painting -- so the control run is checked by pad
 * identity and by ORDER, while the pad and frame ink is checked on the surface.
 *
 * The arm pad is a 2x2 glyph of one private colour placed exactly on the transfer frame's first
 * separator column: that is what makes the pad-before-frame order of screens 9, 10, 11 and 12 observable
 * (the separator overwrites the pad's left column). For screens 8 and 10 the arm/callee order has no
 * such marker -- the research frame blits no control on this path and the transfer frame blits none
 * either -- so those two orders rest on the citations, which is stated rather than tested. */

static UiControl               g_arCtl[4];              /* the arm's control run */
static PadImage                g_arCtlPad[4];
static UiControl              *g_apArCtl[4];
static PadImage                g_arArmPad;              /* the arm's own pad */
static u8                      g_abArArmGlyph[8];       /* 2x2, every pixel index 20 */
static GeoscapeScreenArmEnv    g_arArm;
static GeoscapeBuySellTrackEnv g_arTrack;
static ResearchDrawEnv         g_arResearch;
static ManufFrameEnv           g_arManuf;
static TransferDrawEnv         g_arTransfer;
static BuySellDrawEnv          g_arBuySell;
static GfxColorEnv             g_arColor;
static u8                      g_bArPreviewOk;
static i16                     g_nArCurX, g_nArCurY;
static UiPanel                 g_arPanelB, g_arPanelC;  /* the two lists the tail feeds */
static UiPanel                *g_pArPanelB;             /* the globals the tail re-reads (0x93ED4, 0x93EE8) */
static UiPanel                *g_pArPanelC;
static UiControl               g_arCtlFrame;            /* a control the CALLEE blits */
static PadImage                g_arFramePad;

#define AR_ARM_X      130   /* the transfer frame's first separator column (0x981A8) */
#define AR_ARM_Y      69
#define AR_ARM_INK  0x3A00  /* g_aPalette[PN_TEST_PAL_SLOT][20], clear of every other fixture glyph */

/* The row UiPanel_TrackHoldRow stores at panel+0xC (its own write is nHoverRow). */
static i32 *ArPanelRowSlot(UiPanel *pPanel)
{
    return &pPanel->nHoverRow;
}

static void ArReset(void)
{
    g_nPnBlits = 0;
    memset(g_apnPnBlitPad, 0, sizeof(g_apnPnBlitPad));
    memset(g_bdSurf.pPixels, 0, (size_t)BD_SURF_W * (size_t)BD_SURF_H * 2u);
}

static void TestGeoscapeScreenArms(void)
{
    printf("\nGeoscape_Draw's short screen arms @ 0x93dd4 / 0x93e20 / 0x93e5c / 0x94114 / 0x9415c\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }

    for (i32 i = 0; i < 4; ++i) {
        memset(&g_arCtl[i], 0, sizeof(g_arCtl[i]));
        memset(&g_arCtlPad[i], 0, sizeof(g_arCtlPad[i]));
        g_arCtlPad[i].uWidth  = 2;
        g_arCtlPad[i].uHeight = 2;
        g_arCtl[i].apPads[0]  = &g_arCtlPad[i];        /* a pad must exist or nothing is offered */
        g_arCtl[i].bEnabled   = 1;
        g_arCtl[i].nMode      = 0;
        g_arCtl[i].nState     = 0;
        g_arCtl[i].x          = (i16)(200 + 10 * i);   /* clear of the rails and of the arm pad */
        g_arCtl[i].y          = 200;
        g_apArCtl[i]          = &g_arCtl[i];
    }
    memset(&g_arArmPad, 0, sizeof(g_arArmPad));
    g_abArArmGlyph[0] = 2;  g_abArArmGlyph[1] = 0;     /* u16 width 2 */
    g_abArArmGlyph[2] = 2;  g_abArArmGlyph[3] = 0;     /* u16 height 2 */
    for (i32 i = 0; i < 4; ++i) {
        g_abArArmGlyph[4 + i] = 20;
    }
    g_arArmPad.pData    = g_abArArmGlyph;
    g_arArmPad.uWidth   = 2;
    g_arArmPad.uHeight  = 2;
    g_arArmPad.nOriginX = AR_ARM_X;
    g_arArmPad.nOriginY = AR_ARM_Y;
    g_arArmPad.nMode    = PN_TEST_PAL_SLOT;
    g_aPalette[PN_TEST_PAL_SLOT][20] = (u32)AR_ARM_INK;

    memset(&g_arCtlFrame, 0, sizeof(g_arCtlFrame));
    memset(&g_arFramePad, 0, sizeof(g_arFramePad));
    g_arFramePad.uWidth  = 2;
    g_arFramePad.uHeight = 2;
    g_arCtlFrame.apPads[0] = &g_arFramePad;
    g_arCtlFrame.bEnabled  = 1;
    g_arCtlFrame.nMode     = 0;
    g_arCtlFrame.nState    = 0;
    g_arCtlFrame.x         = 210;
    g_arCtlFrame.y         = 200;

    memset(&g_arArm, 0, sizeof(g_arArm));
    g_arArm.apCtl        = g_apArCtl;
    g_arArm.pfnCtlBlit   = PnBlit;
    g_arArm.pfnCtlBlitEx = PnBlitEx;

    /* The tail's two lists: nFont 10 so the row is (nY - nPosY) / 10, and bSelLineHighlight clear so the
     * callee stores the row and returns -- no hover repeat, no click. */
    memset(&g_arPanelB, 0, sizeof(g_arPanelB));
    memset(&g_arPanelC, 0, sizeof(g_arPanelC));
    g_arPanelB.nPosY = 100;
    g_arPanelB.nFont = 10;
    g_arPanelC.nPosY = 200;
    g_arPanelC.nFont = 10;
    g_nArCurX     = 50;
    g_nArCurY     = 150;                              /* row 5 in panel B, -5 in panel C */
    g_bArPreviewOk = 1;
    memset(&g_arTrack, 0, sizeof(g_arTrack));
    g_pArPanelB = &g_arPanelB;
    g_pArPanelC = &g_arPanelC;
    g_arTrack.pbPreviewOk = &g_bArPreviewOk;
    g_arTrack.ppPanelB    = &g_pArPanelB;             /* the callee re-reads the pointer (0x93ED4) */
    g_arTrack.ppPanelC    = &g_pArPanelC;
    g_arTrack.pnCursorX   = &g_nArCurX;
    g_arTrack.pnCursorY   = &g_nArCurY;

    /* Every callee env is zeroed but for the fields the first ungated block touches. */
    memset(&g_arResearch, 0, sizeof(g_arResearch));
    g_arResearch.pColor    = &g_arColor;
    g_arResearch.pTextRecs = (const char *)g_abPnText;
    g_arResearch.apSiteRecs = g_pnSites;
    memset(&g_arManuf, 0, sizeof(g_arManuf));
    g_arManuf.pColor       = &g_arColor;
    g_arManuf.pTextRecs    = (const char *)g_abPnText;
    g_arManuf.apSiteRecs   = g_pnSites;
    g_arManuf.pCtlReset    = &g_arCtlFrame;           /* 0x70D84: blitted when no product is selected */
    g_arManuf.pfnCtlBlit   = PnBlit;
    g_arManuf.pfnCtlBlitEx = PnBlitEx;
    memset(&g_arTransfer, 0, sizeof(g_arTransfer));
    g_arTransfer.pColor    = &g_arColor;
    g_arTransfer.pTextRecs = (const char *)g_abPnText;
    g_arTransfer.apSiteRecs = g_pnSites;
    memset(&g_arBuySell, 0, sizeof(g_arBuySell));
    g_arBuySell.pColor     = &g_arColor;
    g_arBuySell.pTextRecs  = (const char *)g_abPnText;
    g_arBuySell.apSiteRecs = g_pnSites;
    g_arBuySell.pCtlClose  = &g_arCtlFrame;           /* 0x78028: unconditional, unlike the research frame */
    g_arBuySell.pfnCtlBlit = PnBlit;
    g_arBuySell.pfnCtlBlitEx = PnBlitEx;

    /* ---- calibration: the two markers the checks below rely on ---- */
    ArReset();
    (void)TransferScreen_Draw(&g_arTransfer);
    Check((u16)BdPix(AR_ARM_X, AR_ARM_Y) != 0,
          "the transfer frame's first separator paints at (130,69), so the order check has something "
          "to overwrite (0x981A8, height nBand + 2 = 2 with no panel)");
    Check((u16)BdPix(5, 3) != 0,
          "BaseUi_DrawFrameRails paints the left rail at x 5 -- the marker that a screen frame ran");

    /* ---- screen 8, 0x94114..0x94154: draw first, then two controls, and NO pad of its own ---- */
    ArReset();
    Geoscape_DrawResearchArm(&g_arResearch, &g_arArm);
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_2,
               "the research arm blits two controls (0x94120..0x94150, CMP R0,#2 at 0x9414C)");
    Check(g_apnPnBlitPad[0] == &g_arCtlPad[0] && g_apnPnBlitPad[1] == &g_arCtlPad[1],
          "and they are aResearchTopics + 0x1D48 then + 0x1D4C (0x160C50, 0x160C54), in that order");
    Check((u16)BdPix(AR_ARM_X, AR_ARM_Y) == 0 && (u16)BdPix(AR_ARM_X + 1, AR_ARM_Y) == 0,
          "the research arm blits no pad of its own -- the frame blits *ppScreen at 0x73A40 instead");
    Check((u16)BdPix(5, 3) != 0, "and ResearchScreen_Draw ran (0x9411C): its rails are on the surface");

    /* ---- screen 9, 0x9415C..0x941A4: pad, two controls, then the frame ---- */
    ArReset();
    Geoscape_DrawManufArm(&g_arArmPad, &g_arManuf, &g_arArm);
    Check((u16)BdPix(AR_ARM_X + 1, AR_ARM_Y) == AR_ARM_INK,
          "the arm blits *unk_159E98 (0x94160 / 0x94164): its glyph is on the surface");
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_2 + 1,
               "two arm controls plus the frame's own reset control -- the frame ran (0x941A0)");
    Check(g_apnPnBlitPad[0] == &g_arCtlPad[0] && g_apnPnBlitPad[1] == &g_arCtlPad[1],
          "the arm's run is 0x15EE1C then 0x15EE20 (R6 + 0x4F84, 0x4F88 built at 0x94174 / 0x9417C)");
    Check(g_apnPnBlitPad[2] == &g_arFramePad,
          "and it comes BEFORE ManufScreen_DrawFrame: the frame's reset control is the LAST blit");
    Check((u16)BdPix(5, 3) != 0, "and the manuf rails are on the surface too");

    /* ---- screen 10, 0x93E5C..0x93F04: pad, four controls, frame, then the buy/sell tail ---- */
    ArReset();
    *ArPanelRowSlot(&g_arPanelB) = -999;
    *ArPanelRowSlot(&g_arPanelC) = -999;
    Geoscape_DrawTransferArm(&g_arArmPad, &g_arTransfer, &g_arArm, &g_arTrack);
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_4,
               "the transfer arm blits FOUR controls (0x93E68..0x93E90, CMP R0,#4 at 0x93E8C)");
    Check(g_apnPnBlitPad[0] == &g_arCtlPad[0] && g_apnPnBlitPad[1] == &g_arCtlPad[1]
          && g_apnPnBlitPad[2] == &g_arCtlPad[2] && g_apnPnBlitPad[3] == &g_arCtlPad[3],
          "in slot order -- the transfer root array's own controls 0x1870DC..0x1870E8");
    Check((u16)BdPix(AR_ARM_X + 1, AR_ARM_Y) == AR_ARM_INK,
          "the arm pad (the array's [0], blitted at 0x93E64) painted its right column");
    Check((u16)BdPix(AR_ARM_X, AR_ARM_Y) != AR_ARM_INK,
          "and the frame's separator overwrote its left column: the pad blit precedes TransferScreen_Draw");
    CheckEqI32(*ArPanelRowSlot(&g_arPanelB), 5,
               "the tail feeds nSmapCursorY 150 into panel B: (150 - 100) / 10 = 5 (0x93ED8)");
    CheckEqI32(*ArPanelRowSlot(&g_arPanelC), -5,
               "and into panel C with the SAME cursor: (150 - 200) / 10 = -5 (0x93EEC)");
    g_bArPreviewOk = 0;
    ArReset();
    *ArPanelRowSlot(&g_arPanelB) = -999;
    *ArPanelRowSlot(&g_arPanelC) = -999;
    Geoscape_DrawTransferArm(&g_arArmPad, &g_arTransfer, &g_arArm, &g_arTrack);
    CheckEqI32(*ArPanelRowSlot(&g_arPanelB), -999,
               "with bBaseFacPreviewOk clear (0x18652D, the root block's +0x35 byte read at 0x93EB4) "
               "neither list is touched");
    CheckEqI32(*ArPanelRowSlot(&g_arPanelC), -999, "and neither is the second one");
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_4, "while the four control blits still happen");
    g_bArPreviewOk = 1;

    /* ---- screens 11 and 12, 0x93DD4..0x93E14 and 0x93E20..0x93E58: one function for both ---- */
    ArReset();
    *ArPanelRowSlot(&g_arPanelB) = -999;
    Geoscape_DrawBuySellArm(&g_arArmPad, &g_arBuySell, &g_arArm, &g_arTrack);
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_2 + 1,
               "the buy/sell arm blits two controls plus the frame's unconditional close control");
    Check(g_apnPnBlitPad[0] == &g_arCtlPad[0] && g_apnPnBlitPad[1] == &g_arCtlPad[1],
          "g_aBuySellCtl[0] then [1] (0x161A0C, 0x161A10) -- 0x93DF0 / 0x93DFC reading [R3,#4]");
    Check(g_apnPnBlitPad[2] == &g_arFramePad,
          "and BuySellScreen_Draw comes after them (0x93E10, the call site screens 11 and 12 share)");
    Check((u16)BdPix(AR_ARM_X, AR_ARM_Y) == AR_ARM_INK
          && (u16)BdPix(AR_ARM_X + 1, AR_ARM_Y) == AR_ARM_INK,
          "the arm pad survives whole here: this frame draws no separator on the ungated path");
    CheckEqI32(*ArPanelRowSlot(&g_arPanelB), 5, "and the same tail runs for the buy/sell arm (0x93EB4)");

    /* ---- the arm guard: no controls, but the frame still draws ---- */
    ArReset();
    GeoscapeScreenArmEnv noCtl = g_arArm;
    noCtl.apCtl = NULL;
    Geoscape_DrawBuySellArm(&g_arArmPad, &g_arBuySell, &noCtl, &g_arTrack);
    CheckEqI32(g_nPnBlits, 1, "a fixture with no control run blits only the frame's own control");
    CheckEqI32(*ArPanelRowSlot(&g_arPanelB), 5, "and its tail still runs");
}

/* ---- Geoscape_Draw's ufopaedia and psi arms, 0x941AC..0x94288 and 0x94288..0x942E8 --------------------
 *
 * Screen 16 calls Ui_DrawBaseInfo with the BLOCK BASE (0x941AC LDR R6,=g_pUfopaediaScreenPad / 0x941B0
 * MOV R0,R6), so the callee blits the screen pad (0x9B600) and the close control (0x9B7C0) and the arm blits
 * no pad at all: what the arm chooses is one of three ranges of the block's six control slots. Screen 17 calls
 * PsiScreen_Draw and then blits the block's two scroll controls, gated on two bytes the callee reads too.
 * UiControl_BlitActivePad offers the control's pad to PnBlit, which records it without painting, so every claim
 * here is about WHICH pads and in WHICH order. Ui_DrawBaseInfo runs with bUfopaediaScreenBuilt clear -- the one
 * path of it that needs no panels: screen pad, close control, return (0x9B7CC..0x9B7D8). PsiScreen_Draw runs
 * with the four panels NULL and dwActionFail clear, so the rails, the two geoscape buttons and the captions run
 * while the shortfall panel does not (0x72A94..0x72AD4). */

static UiControl              g_ufoCtl[6];              /* the ufopaedia block's six slots, 0x1872E8.. */
static PadImage               g_ufoCtlPad[6];
static UiControl             *g_apUfoCtl[6];
static GeoscapeScreenArmEnv   g_ufoArm;
static GeoscapeUfopaediaArmEnv g_ufoGate;
static UiBaseInfoDrawEnv      g_ufoBaseInfo;
static PadImage              *g_apUfoScreenPad[1];      /* the block's [0], as Ui_DrawBaseInfo receives it */
static PadImage               g_ufoScreenPad;
static u8                     g_abUfoScreenGlyph[8];    /* 2x2, every pixel index 21 */
static UiControl              g_ufoCloseCtl;            /* the close control the callee blits */
static PadImage               g_ufoClosePad;
static u8                     g_bUfoOpen, g_bUfoDirty, g_bUfoWide, g_bUfoBuilt;

static UiControl              g_psCtl[2];               /* g_pPsiScrollPrevCtl / NextCtl, 0x15EE7C / 80 */
static PadImage               g_psCtlPad[2];
static UiControl             *g_apPsCtl[2];
static GeoscapeScreenArmEnv   g_psArm;
static PsiScreenDrawEnv       g_psEnv;
static UiControl              g_psCtlConfirm, g_psCtlApply;
static PadImage               g_psConfirmPad, g_psApplyPad;
static PadImage               g_psScreenPad;
static u8                     g_abPsScreenGlyph[8];
static u8                     g_bPsOpen, g_bPsOverlay;
static i8                     g_nPsPsiBase;
static i8                     g_nPsBaseCount;           /* nBaseCount @ 0x159D80, read SIGNED */
static i32                    g_nPsActionFail, g_nPsResidue;
static GfxColorEnv            g_psColor;

#define AR_CAL_INK   0x4C00  /* g_aPalette[PN_TEST_PAL_SLOT][21], clear of AR_ARM_INK and the fixture glyphs */
#define AR_CAL_X     60
#define AR_CAL_Y     250     /* clear of the rails, the separators and every other glyph */

/* Where a pad appears in the recorded blit run, or -1. */
static i32 ArFindPad(const PadImage *pPad)
{
    const i32 nMax = (i32)(sizeof(g_apnPnBlitPad) / sizeof(g_apnPnBlitPad[0]));
    for (i32 i = 0; i < g_nPnBlits && i < nMax; ++i) {
        if (g_apnPnBlitPad[i] == pPad) {
            return i;
        }
    }
    return -1;
}

static void ArMakeGlyph(PadImage *pPad, u8 *abGlyph, i32 nX, i32 nY)
{
    abGlyph[0] = 2;  abGlyph[1] = 0;                   /* u16 width 2, little endian */
    abGlyph[2] = 2;  abGlyph[3] = 0;                   /* u16 height 2 */
    for (i32 i = 0; i < 4; ++i) {
        abGlyph[4 + i] = 21;
    }
    memset(pPad, 0, sizeof(*pPad));
    pPad->pData    = abGlyph;
    pPad->uWidth   = 2;
    pPad->uHeight  = 2;
    pPad->nOriginX = nX;
    pPad->nOriginY = nY;
    pPad->nMode    = PN_TEST_PAL_SLOT;
}

static void TestGeoscapeUfoPsiArms(void)
{
    printf("\nGeoscape_Draw's ufopaedia and psi arms @ 0x941ac / 0x94288\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_aPalette[PN_TEST_PAL_SLOT][21] = (u32)AR_CAL_INK;

    for (i32 i = 0; i < 6; ++i) {
        memset(&g_ufoCtl[i], 0, sizeof(g_ufoCtl[i]));
        memset(&g_ufoCtlPad[i], 0, sizeof(g_ufoCtlPad[i]));
        g_ufoCtlPad[i].uWidth  = 2;
        g_ufoCtlPad[i].uHeight = 2;
        g_ufoCtl[i].apPads[0]  = &g_ufoCtlPad[i];
        g_ufoCtl[i].bEnabled   = 1;
        g_ufoCtl[i].nMode      = 0;
        g_ufoCtl[i].nState     = 0;
        g_ufoCtl[i].x          = (i16)(10 + 12 * i);   /* the row is recorded, never painted */
        g_ufoCtl[i].y          = 205;
        g_apUfoCtl[i]          = &g_ufoCtl[i];
    }
    for (i32 i = 0; i < 2; ++i) {
        memset(&g_psCtl[i], 0, sizeof(g_psCtl[i]));
        memset(&g_psCtlPad[i], 0, sizeof(g_psCtlPad[i]));
        g_psCtlPad[i].uWidth  = 2;
        g_psCtlPad[i].uHeight = 2;
        g_psCtl[i].apPads[0]  = &g_psCtlPad[i];
        g_psCtl[i].bEnabled   = 1;
        g_psCtl[i].nMode      = 0;
        g_psCtl[i].nState     = 0;
        g_psCtl[i].x          = (i16)(212 + 12 * i);
        g_psCtl[i].y          = 205;
        g_apPsCtl[i]          = &g_psCtl[i];
    }
    ArMakeGlyph(&g_ufoScreenPad, g_abUfoScreenGlyph, AR_CAL_X, AR_CAL_Y);
    ArMakeGlyph(&g_psScreenPad, g_abPsScreenGlyph, AR_CAL_X, AR_CAL_Y + 4);
    g_apUfoScreenPad[0] = &g_ufoScreenPad;

    memset(&g_ufoCloseCtl, 0, sizeof(g_ufoCloseCtl));
    memset(&g_ufoClosePad, 0, sizeof(g_ufoClosePad));
    g_ufoClosePad.uWidth  = 2;
    g_ufoClosePad.uHeight = 2;
    g_ufoCloseCtl.apPads[0] = &g_ufoClosePad;
    g_ufoCloseCtl.bEnabled  = 1;
    g_ufoCloseCtl.nMode     = 0;
    g_ufoCloseCtl.nState    = 0;
    g_ufoCloseCtl.x         = 224;
    g_ufoCloseCtl.y         = 205;

    for (i32 i = 0; i < 2; ++i) {
        UiControl *pCtl = (i == 0) ? &g_psCtlConfirm : &g_psCtlApply;
        PadImage  *pPad = (i == 0) ? &g_psConfirmPad : &g_psApplyPad;
        memset(pCtl, 0, sizeof(*pCtl));
        memset(pPad, 0, sizeof(*pPad));
        pPad->uWidth  = 2;
        pPad->uHeight = 2;
        pCtl->apPads[0] = pPad;
        pCtl->bEnabled  = 1;
        pCtl->nMode     = 0;
        pCtl->nState    = 0;
        pCtl->x         = (i16)(20 + 12 * i);
        pCtl->y         = 218;                          /* the two buttons are recorded, never painted */
    }

    memset(&g_ufoArm, 0, sizeof(g_ufoArm));
    g_ufoArm.apCtl        = g_apUfoCtl;
    g_ufoArm.pfnCtlBlit   = PnBlit;
    g_ufoArm.pfnCtlBlitEx = PnBlitEx;
    memset(&g_psArm, 0, sizeof(g_psArm));
    g_psArm.apCtl         = g_apPsCtl;
    g_psArm.pfnCtlBlit    = PnBlit;
    g_psArm.pfnCtlBlitEx  = PnBlitEx;

    /* The callee's own gate bytes are the arm's gate bytes: one instance each, as in the original. */
    memset(&g_ufoBaseInfo, 0, sizeof(g_ufoBaseInfo));
    g_ufoBaseInfo.ppScreenPad     = g_apUfoScreenPad;   /* [0] is what 0x9B5FC reads */
    g_ufoBaseInfo.pCloseCtl       = &g_ufoCloseCtl;
    g_ufoBaseInfo.pfnCtlBlit      = PnBlit;
    g_ufoBaseInfo.pfnCtlBlitEx    = PnBlitEx;
    g_ufoBaseInfo.pbEntryDirty    = &g_bUfoDirty;       /* bUfopaediaEntryDirty @ 0x187310 */
    g_ufoBaseInfo.pbScreenBuilt   = &g_bUfoBuilt;       /* 0x187308: clear, so the draw stops at 0x9B7D8 */
    g_ufoBaseInfo.pbUfopaediaOpen = &g_bUfoOpen;        /* 0x18730C */
    memset(&g_ufoGate, 0, sizeof(g_ufoGate));
    g_ufoGate.pbOpen       = &g_bUfoOpen;
    g_ufoGate.pbEntryDirty = &g_bUfoDirty;
    g_ufoGate.pbWidePanel  = &g_bUfoWide;               /* g_bUfopaediaWidePanel */
    g_bUfoBuilt = 0;

    memset(&g_psEnv, 0, sizeof(g_psEnv));
    g_psEnv.pColor          = &g_psColor;
    g_psEnv.pbPanelOpen     = &g_bPsOpen;              /* g_bPsiPanelOpen @ 0x15EEF0 */
    g_psEnv.pbOverlayOpen   = &g_bPsOverlay;           /* byte_1870F8 */
    g_psEnv.pScreenPad      = &g_psScreenPad;          /* 0x15EE78 */
    g_psEnv.nBaseIdx        = 0;                       /* byte_1870F4, read SIGNED at 0x94290 */
    g_psEnv.nWorkBaseIdx    = 0;                       /* byte_158834 */
    g_psEnv.pnPsiBaseIdx    = &g_nPsPsiBase;
    g_psEnv.pCtlConfirm     = &g_psCtlConfirm;         /* pGeoscapeCtlConfirm @ 0x186BA4 */
    g_psEnv.pCtlApply       = &g_psCtlApply;           /* pGeoscapeCtlApply @ 0x186BAC */
    g_psEnv.pfnCtlBlit      = PnBlit;
    g_psEnv.pfnCtlBlitEx    = PnBlitEx;
    g_psEnv.pTextRecs       = (const char *)g_abPnText;
    g_psEnv.apSiteRecs      = g_pnSites;
    g_psEnv.pnBaseCount     = &g_nPsBaseCount;         /* 0: the overlay list draws no rows */
    g_psEnv.pnActionFail    = &g_nPsActionFail;        /* 0: the shortfall panel does not run */
    g_psEnv.pnResidue       = &g_nPsResidue;
    g_nPsPsiBase = 0;
    g_nPsBaseCount = 0;
    g_nPsActionFail = 0;
    g_nPsResidue = 0;

    /* ---- calibration: what the two callees leave behind on this fixture ---- */
    g_bUfoOpen = 1;  g_bUfoDirty = 1;  g_bUfoWide = 1;
    ArReset();
    Ui_DrawBaseInfo(&g_ufoBaseInfo);
    Check((u16)BdPix(AR_CAL_X, AR_CAL_Y) == AR_CAL_INK
          && (u16)BdPix(AR_CAL_X + 1, AR_CAL_Y + 1) == AR_CAL_INK,
          "Ui_DrawBaseInfo blits the block's [0] itself (0x9B600): the fixture's screen glyph is on the surface");
    CheckEqI32(g_nPnBlits, 1, "and with bUfopaediaScreenBuilt clear it stops after its close control (0x9B7C0, 0x9B7D8)");
    Check(g_apnPnBlitPad[0] == &g_ufoClosePad, "and that control is g_pUiCloseCtl's pad");

    g_bPsOpen = 1;  g_bPsOverlay = 0;
    ArReset();
    PsiScreen_Draw(&g_psEnv);
    Check((u16)BdPix(AR_CAL_X, AR_CAL_Y + 4) == AR_CAL_INK,
          "PsiScreen_Draw blits 0x15EE78 (0x723C0) and draws its rails (0x723C8..0x72558)");
    Check((u16)BdPix(5, 3) != 0, "the left rail at x 5 is the marker that the psi frame ran");
    CheckEqI32(g_nPnBlits, 2, "and its own two geoscape controls are the only blits it records (0x72630, 0x7263C)");

    /* ---- screen 16, gate 1: open, entry NOT dirty -> the category pair 4..5 (0x941DC..0x94200) ---- */
    g_bUfoOpen = 1;  g_bUfoDirty = 0;  g_bUfoWide = 1;
    ArReset();
    Geoscape_DrawUfopaediaArm(&g_ufoBaseInfo, &g_ufoGate, &g_ufoArm);
    CheckEqI32(g_nPnBlits, 1 + (PUFO_GEOARM_CTLS_6 - PUFO_GEOARM_UFO_CAT_FIRST),
               "the open, entry-not-dirty arm blits two controls (CMP R0,#6 at 0x941FC)");
    Check(g_apnPnBlitPad[0] == &g_ufoClosePad, "the callee's close control comes FIRST (0x941B4 precedes 0x941DC)");
    Check(g_apnPnBlitPad[1] == &g_ufoCtlPad[4] && g_apnPnBlitPad[2] == &g_ufoCtlPad[5],
          "and they are slots 4 and 5 -- aUiBaseInfoCtlDesc[4] / [5], the category pair at x 200 y 18 / 38");

    /* ---- gate 2: entry dirty and the wide layout -> slots 0..3 (0x94220..0x94244) ---- */
    g_bUfoDirty = 1;  g_bUfoWide = 1;
    ArReset();
    Geoscape_DrawUfopaediaArm(&g_ufoBaseInfo, &g_ufoGate, &g_ufoArm);
    CheckEqI32(g_nPnBlits, 1 + PUFO_GEOARM_CTLS_4,
               "with the entry dirty and g_bUfopaediaWidePanel set, FOUR controls (CMP R0,#4 at 0x94240)");
    Check(g_apnPnBlitPad[1] == &g_ufoCtlPad[0] && g_apnPnBlitPad[2] == &g_ufoCtlPad[1]
          && g_apnPnBlitPad[3] == &g_ufoCtlPad[2] && g_apnPnBlitPad[4] == &g_ufoCtlPad[3],
          "slots 0..3: the text-panel pair at x 142 / 172 then the entry pair at x 10 / 40");
    CheckEqI32(ArFindPad(&g_ufoCtlPad[4]), -1,
               "and the category pair is NOT blitted: 0x941D8 BNE skips the first loop on the dirty byte");

    /* ---- gate 3: entry dirty, not wide -> the entry pair 2..3 only (0x9425C..0x94280) ---- */
    g_bUfoWide = 0;
    ArReset();
    Geoscape_DrawUfopaediaArm(&g_ufoBaseInfo, &g_ufoGate, &g_ufoArm);
    CheckEqI32(g_nPnBlits, 1 + (PUFO_GEOARM_CTLS_4 - PUFO_GEOARM_UFO_ENTRY_FIRST),
               "with g_bUfopaediaWidePanel clear the arm blits two controls starting at slot 2 (0x9425C MOV R0,#2)");
    Check(g_apnPnBlitPad[1] == &g_ufoCtlPad[2] && g_apnPnBlitPad[2] == &g_ufoCtlPad[3],
          "the entry prev/next pair only (0x94264..0x94280)");
    CheckEqI32(ArFindPad(&g_ufoCtlPad[1]), -1, "and nothing from slots 0 or 1");

    /* ---- the two halves are independent on the dirty byte, not on the open byte ---- */
    g_bUfoOpen = 0;  g_bUfoDirty = 1;  g_bUfoWide = 1;
    ArReset();
    Geoscape_DrawUfopaediaArm(&g_ufoBaseInfo, &g_ufoGate, &g_ufoArm);
    CheckEqI32(g_nPnBlits, 1 + PUFO_GEOARM_CTLS_4,
               "a CLOSED screen with a dirty entry still blits the four entry controls (0x941C8 BEQ lands on 0x94204)");
    g_bUfoDirty = 0;  g_bUfoWide = 0;
    ArReset();
    Geoscape_DrawUfopaediaArm(&g_ufoBaseInfo, &g_ufoGate, &g_ufoArm);
    CheckEqI32(g_nPnBlits, 1,
               "closed and not dirty: neither half runs, only the callee's control (0x9420C BEQ def_8C1B0)");

    /* ---- the arm guard: no control block, the callee still draws ---- */
    ArReset();
    GeoscapeScreenArmEnv noCtl = g_ufoArm;
    noCtl.apCtl = NULL;
    g_bUfoOpen = 1;  g_bUfoDirty = 1;  g_bUfoWide = 1;
    Geoscape_DrawUfopaediaArm(&g_ufoBaseInfo, &g_ufoGate, &noCtl);
    CheckEqI32(g_nPnBlits, 1, "a fixture with no control run blits only the callee's close control");
    Check((u16)BdPix(AR_CAL_X, AR_CAL_Y) == AR_CAL_INK, "and Ui_DrawBaseInfo still ran");

    /* ---- screen 17: overlay clear AND panel open -> the two scroll controls (0x942C0..0x942E4) ---- */
    g_bPsOpen = 1;  g_bPsOverlay = 0;
    ArReset();
    Geoscape_DrawPsiArm(&g_psEnv, &g_psArm);
    CheckEqI32(g_nPnBlits, 2 + PUFO_GEOARM_CTLS_2,
               "the psi arm adds two controls to the callee's two (CMP R0,#2 at 0x942E0)");
    Check(g_apnPnBlitPad[0] == &g_psConfirmPad && g_apnPnBlitPad[1] == &g_psApplyPad,
          "the callee's own buttons come FIRST (0x94298 precedes 0x942BC)");
    Check(g_apnPnBlitPad[2] == &g_psCtlPad[0] && g_apnPnBlitPad[3] == &g_psCtlPad[1],
          "then g_pPsiScrollPrevCtl @ 0x15EE7C and g_pPsiScrollNextCtl @ 0x15EE80 ([R3,#4], [R3,#8])");
    Check((u16)BdPix(AR_CAL_X, AR_CAL_Y + 4) == AR_CAL_INK, "and the psi screen's pad is on the surface");

    /* ---- gate 1: the base-picker overlay open stops the arm (0x9429C..0x942A8) ---- */
    g_bPsOverlay = 1;
    ArReset();
    Geoscape_DrawPsiArm(&g_psEnv, &g_psArm);
    CheckEqI32(g_nPnBlits, 2, "with byte_1870F8 set the arm takes the BNE and blits nothing of its own");
    Check(g_apnPnBlitPad[0] == &g_psConfirmPad, "while the callee still ran");
    g_bPsOverlay = 0;

    /* ---- gate 2: the panel closed (0x942AC..0x942B8) ---- */
    g_bPsOpen = 0;
    ArReset();
    Geoscape_DrawPsiArm(&g_psEnv, &g_psArm);
    CheckEqI32(g_nPnBlits, 0, "with g_bPsiPanelOpen clear the arm blits no control (0x942B8 BEQ)");
    Check((u16)BdPix(5, 3) == 0 && (u16)BdPix(AR_CAL_X, AR_CAL_Y + 4) == 0,
          "and the callee drew nothing either -- both read the same flag (0x723B8)");
    g_bPsOpen = 1;

    /* ---- the arm guard: no controls, the callee still draws ---- */
    ArReset();
    GeoscapeScreenArmEnv noPsCtl = g_psArm;
    noPsCtl.apCtl = NULL;
    Geoscape_DrawPsiArm(&g_psEnv, &noPsCtl);
    CheckEqI32(g_nPnBlits, 2, "a fixture with no scroll controls still gets the callee's two buttons");
    Check((u16)BdPix(5, 3) != 0, "and the psi rails are on the surface");
}

/* ---------------------------------------------------------------------------------------------
 * Geoscape_Draw's soldier arms: case 4 @ 0x92DAC..0x92EE4 and case 5 @ 0x92EE4..0x9309C.
 *
 * Case 4 writes the close control's position (0x92DB0 STRH #0xE0 / 0x92DC8 STRH #1), calls
 * SoldierScreen_Draw (0x92DD0), blits the block's +0x148 control pair, blits the close control (0x92E04) and
 * paints three captions. Case 5 blits the block's soldier pad (0x92EEC), writes the same (224,1) position,
 * and splits on bTransferFlag @ 0x159E10 (0x92F18): armed re-positions the close control to (198,77), draws
 * FIRST (0x92F38), runs its three controls after (0x92F48..0x92F64) and paints three centred captions;
 * unarmed runs the three controls FIRST (0x93070..0x9308C), draws after them (0x93094) and paints nothing.
 * Both paths end on the same close-control blit at 0x93054.
 *
 * The callee is observed through its two documented seams. With nGeoscapeScreen at 5 the callee IS the
 * caller's pfnTextInput (0x652A4..0x652B0), and the hook records how many control blits had already been
 * offered -- the only way the arm's own draw/control ORDER becomes visible, because SoldierScreen_Draw blits
 * no control of its own. With the screen closed the callee draws nothing (0x652CC), which is what makes the
 * caption comparison clean. Controls and the callee's pads go through PnBlit, which records the pad offered
 * without painting, so every blit list below is exact; the arm's own pad blit in case 5 is a real
 * PadImage_Blit. Ink comparisons are pixel-value comparisons against a reference draw the test builds itself
 * at the coordinates read off the ARM, so a wrong centre, a wrong y or a wrong colour pair all show up. */

static UiControl            g_solCtl[3];             /* the block's +0x148 pair and +0x150 triple */
static PadImage             g_solCtlPad[3];
static UiControl           *g_apSolCtl[3];
static GeoscapeScreenArmEnv g_solCtlEnv;
static GeoscapeSoldierArmEnv g_solArm;
static SoldierDrawEnv       g_solEnv;
static UiControl            g_solCloseCtl;
static PadImage             g_solClosePad;
static PadImage             g_solPad;                /* the block's soldier pad word @ 0x1598D4 */
static u8                   g_abSolPadGlyph[8];
static i32                  g_nSolScreenId, g_nSolCursor;
static u8                   g_bSolOpen, g_bSolTransfer;
static GfxColorEnv          g_solColor;
static i32                  g_nSolHookCalls, g_nSolHookCursor, g_nSolHookBlits;
static u16                  g_aSolRef[BD_SURF_W * BD_SURF_H];

static i32 SolHookTextInput(i32 nCursor, void *pUser)
{
    (void)pUser;
    ++g_nSolHookCalls;
    g_nSolHookCursor = nCursor;                      /* nSoldierCursor @ 0x159E08, what 0x652B0 passes on */
    g_nSolHookBlits  = g_nPnBlits;                   /* how many controls the arm had blitted by now */
    return 0;
}

/* The ARM's centring, written straight from the instructions: MOV R1,R0 / ADDMI R1,R1,#1 /
 * MOV R1,R1,ASR#1 / RSB R1,R1,#centre, i.e. centre - (nWidth >> 1) for a positive width. */
static i32 SolCentredX(i32 nCentre, const char *psz)
{
    const i32 nW = Text_MeasureWidth(psz);
    return nCentre - (nW >= 0 ? nW / 2 : (nW + 1) / 2);
}

static void SolSaveInk(void)
{
    memcpy(g_aSolRef, g_bdSurf.pPixels, sizeof(g_aSolRef));
}

/* Compare the saved arm result against the reference the surface holds now, pixel by pixel. */
static void SolInkIs(const char *pszWhat)
{
    const u16 *const pwNow = (const u16 *)g_bdSurf.pPixels;
    i32 nDiff = 0;
    for (i32 i = 0; i < BD_SURF_W * BD_SURF_H; ++i) {
        if (g_aSolRef[i] != pwNow[i]) {
            ++nDiff;
        }
    }
    if (nDiff == 0) {
        printf("  ok   %s\n", pszWhat);
        return;
    }
    printf("  FAIL %s: %d pixels differ from the reference draw\n", pszWhat, nDiff);
    ++g_nFailures;
}

static void TestGeoscapeSoldierArms(void)
{
    printf("\nGeoscape_Draw's soldier arms @ 0x92dac / 0x92ee4\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_aPalette[PN_TEST_PAL_SLOT][21] = (u32)AR_CAL_INK;

    PnSetText(PUFO_SOLARM_TEXT_TITLE, "SOLDIERS");   /* TEXTS[71] @ 0xB71AB */
    PnSetText(PUFO_SOLARM_TEXT_COL1,  "NAME1");      /* TEXTS[88] @ 0xB7F48 */
    PnSetText(PUFO_SOLARM_TEXT_COL2,  "SHIP2");      /* TEXTS[89] @ 0x4745 */
    PnSetText(PUFO_SOLTR_TEXT_HEAD,   "ARMOUR");     /* TEXTS[90] @ 0xB80E2 */
    PnSetText(PUFO_SOLTR_TEXT_ROW1,   "RANK1");      /* TEXTS[91] @ 0xB81AF */
    PnSetText(PUFO_SOLTR_TEXT_ROW2,   "MIN2");       /* TEXTS[92] @ 0xB827C */

    for (i32 i = 0; i < 3; ++i) {
        memset(&g_solCtl[i], 0, sizeof(g_solCtl[i]));
        memset(&g_solCtlPad[i], 0, sizeof(g_solCtlPad[i]));
        g_solCtlPad[i].uWidth  = 2;
        g_solCtlPad[i].uHeight = 2;
        g_solCtl[i].apPads[0]  = &g_solCtlPad[i];
        g_solCtl[i].bEnabled   = 1;
        g_solCtl[i].nMode      = 0;
        g_solCtl[i].nState     = 0;
        g_solCtl[i].x          = (i16)(12 + 12 * i); /* the row is recorded, never painted */
        g_solCtl[i].y          = 205;
        g_apSolCtl[i]          = &g_solCtl[i];
    }
    memset(&g_solCloseCtl, 0, sizeof(g_solCloseCtl));
    memset(&g_solClosePad, 0, sizeof(g_solClosePad));
    g_solClosePad.uWidth  = 2;
    g_solClosePad.uHeight = 2;
    g_solCloseCtl.apPads[0] = &g_solClosePad;
    g_solCloseCtl.bEnabled  = 1;
    g_solCloseCtl.nMode     = 0;
    g_solCloseCtl.nState    = 0;
    g_solCloseCtl.x         = 0;                     /* the arm must write the position itself */
    g_solCloseCtl.y         = 0;

    ArMakeGlyph(&g_solPad, g_abSolPadGlyph, AR_CAL_X, AR_CAL_Y);

    memset(&g_solCtlEnv, 0, sizeof(g_solCtlEnv));
    g_solCtlEnv.apCtl        = g_apSolCtl;
    g_solCtlEnv.pfnCtlBlit   = PnBlit;
    g_solCtlEnv.pfnCtlBlitEx = PnBlitEx;

    memset(&g_solArm, 0, sizeof(g_solArm));
    g_solArm.pColor         = &g_solColor;
    g_solArm.pTextRecs      = (const char *)g_abPnText;
    g_solArm.pCloseCtl      = &g_solCloseCtl;
    g_solArm.pbTransferFlag = &g_bSolTransfer;
    g_solArm.pPad           = &g_solPad;

    memset(&g_solEnv, 0, sizeof(g_solEnv));
    g_solEnv.pnScreenId     = &g_nSolScreenId;       /* nGeoscapeScreen @ 0x186BC4 */
    g_solEnv.pbOpen         = &g_bSolOpen;           /* 0x159DE0 */
    g_solEnv.pnCursor       = &g_nSolCursor;         /* nSoldierCursor @ 0x159E08 */
    g_solEnv.pPad           = &g_solPad;             /* 0x652D4's pad, the same word the arm blits */
    g_solEnv.pfnTextInput   = SolHookTextInput;
    g_nSolCursor = 7;

    /* ---- case 4, order: the callee runs before the arm's controls (0x92DD0 precedes 0x92DE0) ---- */
    g_nSolScreenId = 5;  g_bSolOpen = 1;  g_bSolTransfer = 0;
    g_nSolHookCalls = 0;  g_nSolHookBlits = -1;
    ArReset();
    Geoscape_DrawSoldierArm(&g_solEnv, &g_solArm, &g_solCtlEnv);
    CheckEqI32(g_nSolHookCalls, 1, "case 4 calls SoldierScreen_Draw once (0x92DD0)");
    CheckEqI32(g_nSolHookBlits, 0, "and the arm has blitted NOTHING yet, so the callee comes first");
    CheckEqI32(g_nSolHookCursor, 7, "the callee gets nSoldierCursor as its argument (0x652B0)");
    CheckEqI32(g_nPnBlits, PUFO_SOLARM_CTLS_2 + 1, "two block controls plus the close control (CMP #2 at 0x92DF8)");
    Check(g_apnPnBlitPad[0] == &g_solCtlPad[0] && g_apnPnBlitPad[1] == &g_solCtlPad[1],
          "the +0x148 pair at 0x1598B0 / 0x1598B4, in order");
    CheckEqI32(ArFindPad(&g_solCtlPad[2]), -1, "and no third control: case 4 stops at 2");
    Check(g_apnPnBlitPad[PUFO_SOLARM_CTLS_2] == &g_solClosePad,
          "g_pUiCloseCtl is blitted last, at 0x92E04");
    CheckEqI32(g_solCloseCtl.x, PUFO_SOLARM_CLOSE_X, "the arm writes the close control's x = 224 (0x92DB0)");
    CheckEqI32(g_solCloseCtl.y, PUFO_SOLARM_CLOSE_Y, "and its y = 1 (0x92DC8)");

    /* ---- case 4, ink: the three captions, and nothing else (the closed callee draws nothing, 0x652CC) ---- */
    g_nSolScreenId = 4;  g_bSolOpen = 0;
    ArReset();
    Geoscape_DrawSoldierArm(&g_solEnv, &g_solArm, &g_solCtlEnv);
    SolSaveInk();
    const u32 uPalLeft1 = g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_FIRST];
    const u32 uPalLeft2 = g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_SECOND];
    ArReset();
    (void)Gfx_PackRgb565Pair(&g_solColor, 0xFF, 0xFF, 0xFF, 10, 10, 10);           /* 0x92E08..0x92E28 */
    Text_DrawString(SolCentredX(PUFO_SOLARM_CAP_CENTRE, PnRec(PUFO_SOLARM_TEXT_TITLE)), 26,
                    PnRec(PUFO_SOLARM_TEXT_TITLE));                                /* 0x92E40..0x92E60 */
    (void)Gfx_PackRgb565Pair(&g_solColor, 0xFF, 0xFF, 0xFF, 10, 10, 10);           /* 0x92E64..0x92E80 */
    Text_DrawString(38, 50, PnRec(PUFO_SOLARM_TEXT_COL1));                         /* 0x92E9C */
    Text_DrawString(170, 50, PnRec(PUFO_SOLARM_TEXT_COL2));                        /* 0x92EB8 */
    (void)Gfx_PackRgb565Pair(&g_solColor, 0xFF, 0xC8, 0x14, 10, 10, 10);           /* 0x92EBC..0x92ED8 */
    SolInkIs("case 4 draws TEXTS[71] centred on 120 at y 26, TEXTS[88] at (38,50) and TEXTS[89] at (170,50) "
             "under the (255,255,255)/(10,10,10) pair -- and paints nothing else");
    (void)Gfx_PackRgb565Pair(&g_solColor, 0xFF, 0xC8, 0x14, 10, 10, 10);
    Check(g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_FIRST] == uPalLeft1
          && g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_SECOND] == uPalLeft2,
          "and its last act packs (255,200,20) over (10,10,10), a pair no text in the arm draws with "
          "(0x92EBC..0x92ED8)");

    /* ---- case 4 with the soldier screen open: the callee's own frame and pad (0x652D4..0x65538) ---- */
    g_nSolScreenId = 4;  g_bSolOpen = 1;
    g_nSolHookCalls = 0;
    ArReset();
    Geoscape_DrawSoldierArm(&g_solEnv, &g_solArm, &g_solCtlEnv);
    Check((u16)BdPix(2, 60) != 0 && (u16)BdPix(235, 60) != 0,
          "the open callee paints its two side rails (0x653D8, 0x65430)");
    Check((u16)BdPix(AR_CAL_X, AR_CAL_Y) == AR_CAL_INK,
          "and the soldier pad it blits at 0x652D4 is the same word the arm hands it");
    CheckEqI32(g_nSolHookCalls, 0, "with the screen id at 4 the callee takes the draw path, not the seam");
    CheckEqI32(g_nPnBlits, PUFO_SOLARM_CTLS_2 + 1, "and the arm's control row is unchanged");

    /* ---- case 5, transfer path: draw FIRST, controls after (0x92F38 before 0x92F48) ---- */
    g_nSolScreenId = 5;  g_bSolOpen = 1;  g_bSolTransfer = 1;
    g_nSolHookCalls = 0;  g_nSolHookBlits = -1;
    ArReset();
    Geoscape_DrawSoldierEditArm(&g_solEnv, &g_solArm, &g_solCtlEnv);
    CheckEqI32(g_nSolHookCalls, 1, "the armed arm calls SoldierScreen_Draw once (0x92F38)");
    CheckEqI32(g_nSolHookBlits, 0, "and it calls it BEFORE any of its three controls");
    CheckEqI32(g_nPnBlits, PUFO_SOLARM_CTLS_3 + 1, "three controls plus the close control (CMP #3 at 0x92F60)");
    Check(g_apnPnBlitPad[0] == &g_solCtlPad[0] && g_apnPnBlitPad[1] == &g_solCtlPad[1]
          && g_apnPnBlitPad[2] == &g_solCtlPad[2], "the +0x150 triple at 0x1598B8..0x1598C0, in order");
    Check(g_apnPnBlitPad[PUFO_SOLARM_CTLS_3] == &g_solClosePad, "and the close control last, at 0x93054");
    CheckEqI32(g_solCloseCtl.x, PUFO_SOLTR_CLOSE_X,
               "armed, the close control's final x is 198 -- 0x92EF4's 224 is overwritten by 0x92F20");
    CheckEqI32(g_solCloseCtl.y, PUFO_SOLTR_CLOSE_Y, "and its final y is 77 (0x92F28)");
    Check((u16)BdPix(AR_CAL_X, AR_CAL_Y) == AR_CAL_INK, "the arm blits the soldier pad itself, at 0x92EEC");

    /* ---- case 5, transfer ink: pad plus three centred captions under the cyan pair ---- */
    g_bSolOpen = 0;
    ArReset();
    Geoscape_DrawSoldierEditArm(&g_solEnv, &g_solArm, &g_solCtlEnv);
    SolSaveInk();
    ArReset();
    PadImage_Blit(&g_solPad);                                                      /* 0x92EEC */
    (void)Gfx_PackRgb565Pair(&g_solColor, 0x44, 0xF4, 0xFC, 4, 70, 75);            /* 0x92F68..0x92F90 */
    Text_DrawString(SolCentredX(PUFO_SOLTR_HEAD_CENTRE, PnRec(PUFO_SOLTR_TEXT_HEAD)), 38,
                    PnRec(PUFO_SOLTR_TEXT_HEAD));                                  /* 0x92FA8..0x92FC8 */
    (void)Gfx_PackRgb565Pair(&g_solColor, 0x44, 0xF4, 0xFC, 4, 70, 75);            /* 0x92FCC..0x92FE8 */
    Text_DrawString(SolCentredX(PUFO_SOLTR_ROW1_CENTRE, PnRec(PUFO_SOLTR_TEXT_ROW1)), 80,
                    PnRec(PUFO_SOLTR_TEXT_ROW1));                                  /* 0x92FFC..0x9301C */
    Text_DrawString(SolCentredX(PUFO_SOLTR_ROW2_CENTRE, PnRec(PUFO_SOLTR_TEXT_ROW2)), 80,
                    PnRec(PUFO_SOLTR_TEXT_ROW2));                                  /* 0x93030..0x93050 */
    SolInkIs("armed, the arm draws TEXTS[90] centred on 104 at y 38, TEXTS[91] on 90 and TEXTS[92] on 170, "
             "both at y 80, all under the (68,244,252)/(4,70,75) pair, over its pad blit and nothing else");

    /* ---- case 5, unarmed path: controls FIRST, draw after them, no captions (0x93064..0x93094) ---- */
    g_bSolTransfer = 0;
    g_nSolHookCalls = 0;  g_nSolHookBlits = -1;
    ArReset();
    Geoscape_DrawSoldierEditArm(&g_solEnv, &g_solArm, &g_solCtlEnv);
    CheckEqI32(g_nSolHookBlits, PUFO_SOLARM_CTLS_3,
               "unarmed, the three controls are already blitted when SoldierScreen_Draw runs (0x93094 after "
               "0x93070..0x9308C) -- the only arm whose order flips with a flag");
    CheckEqI32(g_nPnBlits, PUFO_SOLARM_CTLS_3 + 1, "the same three controls plus the close control (CMP #3 at 0x93088)");
    Check(g_apnPnBlitPad[PUFO_SOLARM_CTLS_3] == &g_solClosePad, "the shared 0x93054 blit both paths end on");
    CheckEqI32(g_solCloseCtl.x, PUFO_SOLARM_CLOSE_X,
               "unarmed, the close control keeps 224: 0x92F20 is behind the gate at 0x92F18");
    CheckEqI32(g_solCloseCtl.y, PUFO_SOLARM_CLOSE_Y, "and 1");
    SolSaveInk();
    ArReset();
    PadImage_Blit(&g_solPad);
    SolInkIs("and the unarmed path paints its pad only -- no caption at all (0x93064..0x9309C has no text call)");

    /* ---- the port's null guards: the original derefs the globals, the port does not crash ---- */
    g_nSolHookCalls = 0;
    ArReset();
    GeoscapeSoldierArmEnv noCtl = g_solArm;
    noCtl.pCloseCtl = NULL;
    noCtl.pPad = NULL;
    GeoscapeScreenArmEnv noRun = g_solCtlEnv;
    noRun.apCtl = NULL;
    Geoscape_DrawSoldierEditArm(&g_solEnv, &noCtl, &noRun);
    CheckEqI32(g_nPnBlits, 0, "with no controls and no close control the arm blits nothing");
    CheckEqI32(g_nSolHookCalls, 1, "and the callee still runs");
    Check((u16)BdPix(AR_CAL_X, AR_CAL_Y) == 0, "with no pad word there is no pad blit (0x92EE8 LDR would fault)");
}

/* ---------------------------------------------------------------------------------------------
 * Geoscape_Draw's base-facility arm, 0x92AF0..0x92DAC (screen 7, BaseFacScreen_Open).
 *
 * Rails, one separator, the glyph palette base, three callees, then two page controls that an armed
 * placement cancels. The fixture puts the three callees' own ink into bands the arm's rails and separator
 * cannot reach: the rails are 1 pixel wide at x 2 and 237 and 1 pixel tall at y 2 and 315, the separator is
 * the single row y 35 from x 2 to 209, the name grid sits at x 6..197 / y 96..287 (its own origin 6,60 plus
 * the 36-pixel top row and 32-pixel cells), the icon strip at x 211..236 / y 3..28 (PUFO_BASE_STRIP_*), and
 * the funds line the open dialog draws lands at (5,20). So ink in a band can only come from the callee that
 * owns it. Controls go through PnBlit, which records the pad without painting.
 *
 * The grid fixture is empty and the base count is 1 with the selection equal to it, so the strip paints its
 * 36 cells and nothing else, and the grid paints its 36 backdrop glyphs and nothing else (no days, no
 * stamps, no craft). The dialog is closed for every run except one, which is what makes the funds-line
 * difference a proof that the arm really called BaseFacScreen_DrawBuildDialog. */

#define BFA_PAD_X   220
#define BFA_PAD_Y   140
#define BFA_GRID_X0    6   /* the env's own origin, plus the 36-pixel name-grid top row */
#define BFA_GRID_Y0   96
#define BFA_GRID_X1  197
#define BFA_GRID_Y1  287
#define BFA_STRIP_X0 211   /* PUFO_BASE_STRIP_X_FIRST, and the strip's cells at 212 + 4*col */
#define BFA_STRIP_Y0   3   /* PUFO_BASE_STRIP_Y_FIRST */
#define BFA_STRIP_X1 236
#define BFA_STRIP_Y1  28
#define BFA_FUNDS_X0   4   /* the open dialog's funds line is at (5,20); x 2 is the left rail */
#define BFA_FUNDS_Y0  18
#define BFA_FUNDS_X1  60
#define BFA_FUNDS_Y1  33
#define BFA_MARK_X0   70   /* the fixture's marked cell: row 2, column 2 -> x 32*2+6, y 36+32*2+60 */
#define BFA_MARK_Y0  160
#define BFA_MARK_X1  101
#define BFA_MARK_Y1  191

static PadImage               g_bfPad;
static u8                     g_abBfPadGlyph[8];
static UiControl              g_bfCtl[2];
static PadImage               g_bfCtlPad[2];
static UiControl             *g_apBfCtl[2];
static GeoscapeBaseFacArmEnv  g_bfArm;
static FacNameGridEnv         g_bfGrid;
static BaseStripDrawEnv       g_bfStrip;
static BuildDialogEnv         g_bfDialog;
static GfxColorEnv            g_bfColor;
static u8                     g_abBfGrid[1524];
static u8                     g_nBfBaseCount, g_nBfSelBase;   /* the strip reads them signed (0x62520, 0x62590) */
static u8                     g_nBfCraftCount;
static i32                    g_nBfOriginX, g_nBfOriginY, g_nBfFunds;
static u8                     g_bBfArmed, g_bBfDialogOpen;

static i32 BfaInk(i32 nX0, i32 nY0, i32 nX1, i32 nY1)
{
    i32 n = 0;
    for (i32 y = nY0; y <= nY1; ++y) {
        for (i32 x = nX0; x <= nX1; ++x) {
            if ((u16)BdPix(x, y) != 0) {
                ++n;
            }
        }
    }
    return n;
}

static void TestGeoscapeBaseFacArm(void)
{
    printf("\nGeoscape_Draw's base-facility arm @ 0x92af0\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_aPalette[PN_TEST_PAL_SLOT][21] = (u32)AR_CAL_INK;

    ArMakeGlyph(&g_bfPad, g_abBfPadGlyph, BFA_PAD_X, BFA_PAD_Y);
    for (i32 i = 0; i < 2; ++i) {
        memset(&g_bfCtl[i], 0, sizeof(g_bfCtl[i]));
        memset(&g_bfCtlPad[i], 0, sizeof(g_bfCtlPad[i]));
        g_bfCtlPad[i].uWidth  = 2;
        g_bfCtlPad[i].uHeight = 2;
        g_bfCtl[i].apPads[0]  = &g_bfCtlPad[i];
        g_bfCtl[i].bEnabled   = 1;
        g_bfCtl[i].nMode      = 0;
        g_bfCtl[i].nState     = 0;
        g_bfCtl[i].x          = (i16)(30 + 12 * i);   /* BaseFacScreen_Init puts them at (35,37) and (3,37) */
        g_bfCtl[i].y          = 37;
        g_apBfCtl[i]          = &g_bfCtl[i];
    }

    memset(g_abBfGrid, 0, sizeof(g_abBfGrid));
    /* One marked cell, row 2 column 2: tileA 1 is the stamp NameGrid_BuildSprite maps to build icon 4,
     * tileB 5 is the layer the name sprite is picked from, and 7 days is what makes the days label draw. */
    g_abBfGrid[6 * 2 + 2]                                          = 7;   /* buildDays[0][2][2] */
    g_abBfGrid[PUFO_GRID_FAC_OFF + 6 * 2 + 2]                      = 1;   /* fac[0].tileA */
    g_abBfGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_TILEB_OFF + 6 * 2 + 2] = 5;  /* fac[0].tileB */
    g_nBfBaseCount  = 1;
    g_nBfSelBase    = 1;                    /* selection == count: no erase rect (0x62520) */
    g_nBfCraftCount = 0;                    /* the craft pass is skipped whole (0x62154) */
    g_nBfOriginX    = BFA_GRID_X0;
    g_nBfOriginY    = BFA_GRID_Y0 - 36;     /* the grid's first row is 36 below the origin */
    g_nBfFunds      = 4147000;

    memset(&g_bfGrid, 0, sizeof(g_bfGrid));
    g_bfGrid.abFacilityGrid = g_abBfGrid;
    g_bfGrid.pnCraftCount   = &g_nBfCraftCount;
    g_bfGrid.pnOriginX      = &g_nBfOriginX;
    g_bfGrid.pnOriginY      = &g_nBfOriginY;
    g_bfGrid.pColor         = &g_bfColor;
    g_bfGrid.nBaseIdx       = 0;

    memset(&g_bfStrip, 0, sizeof(g_bfStrip));
    g_bfStrip.abFacilityGrid = g_abBfGrid;
    g_bfStrip.pbBaseCount    = &g_nBfBaseCount;
    g_bfStrip.pbSelectedBase = &g_nBfSelBase;
    g_bfStrip.nHighlightRow  = 0;           /* deliberately wrong: 0x92D38 makes the arm overwrite it */

    memset(&g_bfDialog, 0, sizeof(g_bfDialog));
    g_bfDialog.pColor       = &g_bfColor;
    g_bfDialog.pnFunds      = &g_nBfFunds;
    g_bfDialog.pbDialogOpen = &g_bBfDialogOpen;
    g_bfDialog.pbPlaceArmed = &g_bBfArmed;  /* the same byte the arm gates its controls on */

    memset(&g_bfArm, 0, sizeof(g_bfArm));
    g_bfArm.pPad          = &g_bfPad;
    g_bfArm.apPanelCtl    = g_apBfCtl;
    g_bfArm.pbPlaceArmed  = &g_bBfArmed;
    g_bfArm.pfnCtlBlit    = PnBlit;
    g_bfArm.pfnCtlBlitEx  = PnBlitEx;
    g_bBfArmed     = 0;
    g_bBfDialogOpen = 0;

    /* ---- the plain run: rails, separator, the three callees, then the two controls ---- */
    g_nGlyphPalSlotBase = 7;                /* a value the arm must overwrite (0x92D20) */
    ArReset();
    Geoscape_DrawBaseFacArm(&g_bfArm, &g_bfGrid, &g_bfStrip, &g_bfDialog);
    Check((u16)BdPix(BFA_PAD_X, BFA_PAD_Y) == AR_CAL_INK, "the arm blits pBaseFacScreenPad @ 0x186BA0 (0x92AF8)");
    Check((u16)BdPix(2, 1) != 0 && (u16)BdPix(2, 168) != 0 && (u16)BdPix(2, 318) != 0,
          "the left rail covers y 1..168 climbing and 151..318 falling, 1 pixel wide at x 2");
    Check((u16)BdPix(2, 0) == 0 && (u16)BdPix(2, 319) == 0, "and no further: the 24 steps of 7 start at 1 and 151");
    Check((u16)BdPix(237, 1) != 0 && (u16)BdPix(237, 318) != 0 && (u16)BdPix(238, 1) == 0,
          "the right rail at x 237 (0x92B84 MOV R1,#0xED), one pixel wide");
    Check((u16)BdPix(3, 1) == 0, "and the left rail is one pixel wide too (0x92B28 MOV R3,#1)");
    Check((u16)BdPix(1, 315) != 0 && (u16)BdPix(120, 315) != 0 && (u16)BdPix(128, 315) != 0,
          "the top rail: climbing x 1..120 and falling from 128, both 5x1 at y 315");
    Check((u16)BdPix(121, 315) == 0 && (u16)BdPix(127, 315) == 0,
          "and the gap is exactly x 121..127 -- 24 rects of 5 from x 1 (0x92C44 MOV R3,#5), and the falling "
          "one really starts at 128");
    Check((u16)BdPix(1, 2) != 0 && (u16)BdPix(124, 2) != 0 && (u16)BdPix(239, 2) != 0,
          "the bottom rail has NO gap: its falling ramp starts at x 120 (0x92CC4 ADD R1,R9,#0x78), eight pixels "
          "left of the top one -- the original's own asymmetry");
    Check((u16)BdPix(0, 315) == 0 && (u16)BdPix(0, 2) == 0, "and neither row starts before x 1 (0x92C38 ADD R11,R9,#1)");
    Check((u16)BdPix(4, 35) != 0 && (u16)BdPix(209, 35) != 0 && (u16)BdPix(210, 35) == 0,
          "the separator is the row y 35 from x 2 for 208 pixels (0x92CE8..0x92D0C)");
    Check(BfaInk(BFA_MARK_X0, BFA_MARK_Y0, BFA_MARK_X1, BFA_MARK_Y1) > 0,
          "BaseFacScreen_DrawNameGrid ran: the marked cell at (70,160) draws its name sprite, its build icon "
          "and its days label, in a band no rail or separator reaches");
    Check(BfaInk(BFA_STRIP_X0, BFA_STRIP_Y0, BFA_STRIP_X1, BFA_STRIP_Y1) > 0,
          "Base_DrawFacilityIconStrip ran: the 6x6 cell blocks of its one base");
    CheckEqI32(g_nGlyphPalSlotBase, PUFO_BFA_GLYPH_PAL,
               "the arm leaves Map_nGlyphPalBase at 3 (0x92D18 / 0x92D20) and never restores it");
    CheckEqI32(g_nPnBlits, PUFO_BFA_CTLS_2, "then the two page controls (CMP #2 at 0x92D90)");
    Check(g_apnPnBlitPad[0] == &g_bfCtlPad[0] && g_apnPnBlitPad[1] == &g_bfCtlPad[1],
          "gBaseFacBuildBlock[+4] then [+8] @ 0x158874 / 0x158878, BaseFacScreen_PanelPrev then PanelNext "
          "(handlers stored at 0x5A20C / 0x5A21C)");
    Check(BfaInk(BFA_FUNDS_X0, BFA_FUNDS_Y0, BFA_FUNDS_X1, BFA_FUNDS_Y1) == 0,
          "with the dialog closed BaseFacScreen_DrawBuildDialog draws nothing at all (0x59B6C)");

    /* ---- an armed placement cancels the whole control run ---- */
    g_bBfArmed = 1;
    ArReset();
    Geoscape_DrawBaseFacArm(&g_bfArm, &g_bfGrid, &g_bfStrip, &g_bfDialog);
    CheckEqI32(g_nPnBlits, 0, "with bBaseFacPlaceArmed @ 0x158888 set the arm blits NO control (0x92D68 BNE)");
    Check((u16)BdPix(BFA_PAD_X, BFA_PAD_Y) == AR_CAL_INK && (u16)BdPix(2, 1) != 0,
          "while its own pad, rails and separator still draw -- the gate is only on the control loop");
    const i32 nFundsClosed = BfaInk(BFA_FUNDS_X0, BFA_FUNDS_Y0, BFA_FUNDS_X1, BFA_FUNDS_Y1);
    CheckEqI32(nFundsClosed, 0, "the dialog is still closed here");
    g_bBfDialogOpen = 1;
    ArReset();
    Geoscape_DrawBaseFacArm(&g_bfArm, &g_bfGrid, &g_bfStrip, &g_bfDialog);
    Check(BfaInk(BFA_FUNDS_X0, BFA_FUNDS_Y0, BFA_FUNDS_X1, BFA_FUNDS_Y1) > 0,
          "open, the dialog's funds line (5,20) appears -- so the arm really calls BaseFacScreen_DrawBuildDialog "
          "with the env it was given (0x92D50 LDR R5,=gBaseFacBuildBlock / 0x92D58)");
    CheckEqI32(g_nPnBlits, 0, "and the armed gate still blits no control");
    Check(BfaInk(BFA_MARK_X0, BFA_MARK_Y0, BFA_MARK_X1, BFA_MARK_Y1) > 0,
          "and the name grid is still drawn, before the dialog as 0x92D34 / 0x92D58 order them");
    g_bBfDialogOpen = 0;
    g_bBfArmed = 0;

    /* ---- the strip mark is the arm's own argument, measured directionally ----
     *
     * The old form compared the arm's run with a hand-set -1 copy and required the two surfaces to be
     * identical: an invariance check that cannot see the arm's constant itself moving, because both
     * sides move with it. Stated the other way round -- like the placement arm's pair at 0x922B8 --
     * a DIRECT call with highlight 0 is the control that shows where the chevron lands, and the arm's
     * run has to leave those same pixels as ordinary cell blocks (0x92D38 MOV R1,#0xFFFFFFFF). */
    const u16 uChevron = (u16)(((u32)0xFF >> 3) << 11); /* (0xFF,0,0) through the Blit_CopyToXY packing */
    ArReset();
    BaseStripDrawEnv ctlMark = g_bfStrip;
    ctlMark.nHighlightRow = 0;
    Base_DrawFacilityIconStrip(&ctlMark);
    Check((u16)BdPix(212, 4) == uChevron && (u16)BdPix(212, 26) == uChevron
              && (u16)BdPix(234, 26) == uChevron,
          "control: Base_DrawFacilityIconStrip with highlight 0 paints its chevron steps at (212+k, 4+k) "
          "and (212+k, 26-k), 23 of them, over the cell blocks (0x62720..0x627C4)");
    ArReset();
    Geoscape_DrawBaseFacArm(&g_bfArm, &g_bfGrid, &g_bfStrip, &g_bfDialog);   /* the env says highlight 0 */
    Check((u16)BdPix(212, 4) != uChevron && (u16)BdPix(212, 26) != uChevron
              && (u16)BdPix(234, 26) != uChevron,
          "the arm overwrites that argument with -1 before the call (0x92D38), so not one chevron step "
          "appears even though the env asked for base 0");
    Check(BfaInk(BFA_STRIP_X0, BFA_STRIP_Y0, BFA_STRIP_X1, BFA_STRIP_Y1) > 0,
          "while the strip still drew its 36 cell blocks -- the mark is the only thing suppressed");

    /* ---- the mark band belongs to the callee: clear the cell and the same band is empty ---- */
    g_abBfGrid[6 * 2 + 2] = 0;
    g_abBfGrid[PUFO_GRID_FAC_OFF + 6 * 2 + 2] = 0;
    g_abBfGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_TILEB_OFF + 6 * 2 + 2] = 0;
    ArReset();
    Geoscape_DrawBaseFacArm(&g_bfArm, &g_bfGrid, &g_bfStrip, &g_bfDialog);
    CheckEqI32(BfaInk(BFA_MARK_X0, BFA_MARK_Y0, BFA_MARK_X1, BFA_MARK_Y1), 0,
               "the 36 blank backdrop glyphs draw nothing (0x61CA4..0x61CC8), so that band's ink was the marked cell");
    Check((u16)BdPix(2, 1) != 0 && BfaInk(BFA_STRIP_X0, BFA_STRIP_Y0, BFA_STRIP_X1, BFA_STRIP_Y1) > 0,
          "while the arm's rails and the strip's 6x6 cells are still there");

    /* ---- the port's null guards ---- */
    ArReset();
    GeoscapeBaseFacArmEnv noPad = g_bfArm;
    noPad.pPad = NULL;
    noPad.apPanelCtl = NULL;
    Geoscape_DrawBaseFacArm(&noPad, &g_bfGrid, &g_bfStrip, &g_bfDialog);
    Check((u16)BdPix(BFA_PAD_X, BFA_PAD_Y) == 0, "with no pad word there is no pad blit");
    CheckEqI32(g_nPnBlits, 0, "with no control array the arm blits nothing (the original indexes the block blindly)");
    Check((u16)BdPix(2, 1) != 0, "and the rails are still drawn -- they need no env at all");
}

/* ---- fixtures for Geoscape_DrawBaseInfoArm (Geoscape_Draw case 3, 0x93F08) -------------------- */
#define BI_PAD_X  218   /* the pad's glyph, away from every rail (the right rail is x 234) */
#define BI_PAD_Y  140

static PadImage                g_biPad;
static u8                      g_abBiPadGlyph[8];
static PadImage               *g_pBiPadWord;      /* the word at 0x158828, which the arm also uses as a block base */
static UiControl               g_biCtl[2];
static PadImage                g_biCtlPad[2];
static UiControl              *g_apBiCtl[2];
static UiControl               g_biCloseCtl;
static PadImage                g_biClosePad;
static u8                      g_bBiOverlay;      /* 0x1870F8 */
static u8                      g_bBiBuilt;        /* bBaseInfoPanelsBuilt @ 0x15883C */
static i8                      g_nBiWorkBase;     /* byte_1870F4 */
static GeoscapeBaseInfoArmEnv  g_biArm;
static GeoscapeScreenArmEnv    g_biCtlEnv;
static BaseInfoDrawEnv         g_biInfo;
static GfxColorEnv             g_biColor;

static void TestGeoscapeBaseInfoArm(void)
{
    printf("\nGeoscape_Draw's base-info arm @ 0x93f08\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_aPalette[PN_TEST_PAL_SLOT][21] = (u32)AR_CAL_INK;
    ArMakeGlyph(&g_biPad, g_abBiPadGlyph, BI_PAD_X, BI_PAD_Y);
    g_pBiPadWord = &g_biPad;
    for (i32 i = 0; i < 2; ++i) {
        memset(&g_biCtl[i], 0, sizeof(g_biCtl[i]));
        memset(&g_biCtlPad[i], 0, sizeof(g_biCtlPad[i]));
        g_biCtlPad[i].uWidth  = 2;
        g_biCtlPad[i].uHeight = 2;
        g_biCtl[i].apPads[0]  = &g_biCtlPad[i];
        g_biCtl[i].bEnabled   = 1;
        g_biCtl[i].nMode      = 0;
        g_biCtl[i].nState     = 0;
        g_apBiCtl[i]          = &g_biCtl[i];
    }
    memset(&g_biClosePad, 0, sizeof(g_biClosePad));
    g_biClosePad.uWidth = 2;
    g_biClosePad.uHeight = 2;
    memset(&g_biCloseCtl, 0, sizeof(g_biCloseCtl));
    g_biCloseCtl.apPads[0] = &g_biClosePad;
    g_biCloseCtl.bEnabled   = 1;

    memset(&g_biArm, 0, sizeof(g_biArm));
    g_biArm.ppPad          = &g_pBiPadWord;
    g_biArm.pbOverlayShown = &g_bBiOverlay;
    g_biArm.pCloseCtl      = &g_biCloseCtl;
    memset(&g_biCtlEnv, 0, sizeof(g_biCtlEnv));
    g_biCtlEnv.apCtl         = g_apBiCtl;
    g_biCtlEnv.pfnCtlBlit    = PnBlit;
    g_biCtlEnv.pfnCtlBlitEx  = PnBlitEx;
    memset(&g_biInfo, 0, sizeof(g_biInfo));
    g_biInfo.pbBuilt       = &g_bBiBuilt;
    g_biInfo.pnWorkBaseIdx = &g_nBiWorkBase;
    g_biInfo.pbOverlayOpen = &g_bBiOverlay;
    g_biInfo.pColor        = &g_biColor;
    g_bBiOverlay = 0;
    g_bBiBuilt   = 0;
    g_nBiWorkBase = 0;

    /* ---- the plain run: pad, rails, the two control slots, the close control, callee off ---- */
    ArReset();
    Geoscape_DrawBaseInfoArm(&g_biArm, &g_biCtlEnv, &g_biInfo);
    Check((u16)BdPix(BI_PAD_X, BI_PAD_Y) == AR_CAL_INK,
          "the arm blits *pBaseInfoPad @ 0x158828 (0x93F08 LDR / 0x93F0C LDR [R0] / 0x93F10)");
    Check((u16)BdPix(5, 1) != 0 && (u16)BdPix(5, 168) != 0 && (u16)BdPix(5, 318) != 0,
          "the left rail is at x 5 (0x93F48 MOV R1,#5) -- case 7 has it at x 2");
    Check((u16)BdPix(5, 0) == 0 && (u16)BdPix(5, 319) == 0,
          "and 24 steps of 7 run y 1..168 climbing, 151..318 falling, no further (CMP #0xA8 at 0x93FD4)");
    Check((u16)BdPix(6, 1) == 0, "the rail is one pixel wide (0x93F38 MOV R3,#1)");
    Check((u16)BdPix(234, 1) != 0 && (u16)BdPix(234, 318) != 0 && (u16)BdPix(233, 1) == 0
              && (u16)BdPix(235, 1) == 0,
          "the right rail at x 234 (0x93F94 MOV R1,#0xED), one pixel wide");
    Check((u16)BdPix(1, 5) != 0 && (u16)BdPix(120, 5) != 0 && (u16)BdPix(119, 5) != 0
              && (u16)BdPix(238, 5) != 0 && (u16)BdPix(239, 5) == 0,
          "the lower edge row is y 5 (0x94004 MOV R2,#5), NOT case 7's y 2: climbing x 1..120, falling from 119 (0x9404C ADD R9,R8,#0x77) to 238");
    Check((u16)BdPix(1, 315) != 0 && (u16)BdPix(238, 315) != 0 && (u16)BdPix(239, 315) == 0,
          "the upper edge row (y 315) uses the SAME two ramps -- one immediate for both rows here");
    Check((u16)BdPix(121, 315) != 0 && (u16)BdPix(121, 5) != 0,
          "x 121 is lit on both rows -- it is covered only by the falling ramp, and it is exactly where case 7 leaves a gap");
    Check((u16)BdPix(1, 4) == 0 && (u16)BdPix(1, 6) == 0, "the edge rows are one pixel tall (0x93FEC STR nRectH=1)");
    Check((u16)BdPix(0, 315) == 0 && (u16)BdPix(0, 5) == 0, "neither row starts before x 1 (0x93FF8 ADD R11,R8,#1)");
    CheckEqI32(g_nPnBlits, 3, "two controls then the close control -- 0x940B8..0x940D4 (CMP #2) and the BLEQ at 0x940F0");
    Check(g_apnPnBlitPad[0] == &g_biCtlPad[0] && g_apnPnBlitPad[1] == &g_biCtlPad[1],
          "the two controls are the words AFTER the pad pointer, 0x15882C then 0x158830 (ADD R3,R1,R0,LSL#2 / LDR [R3,#4])");
    Check(g_apnPnBlitPad[2] == &g_biClosePad, "and the third blit is g_pUiCloseCtl");
    Check((u16)BdPix(20, 35) == 0 && (u16)BdPix(130, 35) == 0,
          "BaseInfoScreen_Draw draws nothing while bBaseInfoPanelsBuilt is clear (0x56FCC), so y 35 is empty");

    /* ---- the same run with the callee's gate open ---- */
    g_bBiBuilt = 1;
    ArReset();
    Geoscape_DrawBaseInfoArm(&g_biArm, &g_biCtlEnv, &g_biInfo);
    Check((u16)BdPix(20, 35) != 0 && (u16)BdPix(130, 35) != 0,
          "with the flag set its own 24-step ramp lands on y 35 at x+1 and x+119 (0x56FE8..0x57210) -- the arm really calls BaseInfoScreen_Draw (0x94100)");
    g_bBiBuilt = 0;

    /* ---- 0x1870F8 set: the close control is not blitted, everything else is ---- */
    g_bBiOverlay = 1;
    ArReset();
    Geoscape_DrawBaseInfoArm(&g_biArm, &g_biCtlEnv, &g_biInfo);
    CheckEqI32(g_nPnBlits, 2, "with byte 0x1870F8 set the close control blit is skipped (0x940E4 ANDS, LDREQ at 0x940EC, BLEQ at 0x940F0)");
    Check(g_apnPnBlitPad[0] == &g_biCtlPad[0] && g_apnPnBlitPad[1] == &g_biCtlPad[1],
          "the two block controls still blit -- the gate is only the predicated close-control load and call");
    Check((u16)BdPix(5, 1) != 0 && (u16)BdPix(1, 5) != 0 && (u16)BdPix(BI_PAD_X, BI_PAD_Y) == AR_CAL_INK,
          "while the pad and both rails are drawn as before");
    g_bBiOverlay = 0;

    /* ---- a null pad word: the port skips the blit, the rails do not care ---- */
    g_pBiPadWord = NULL;
    ArReset();
    Geoscape_DrawBaseInfoArm(&g_biArm, &g_biCtlEnv, &g_biInfo);
    Check((u16)BdPix(BI_PAD_X, BI_PAD_Y) == 0,
          "with a null pad word nothing is painted (deviation: PadImage_Blit @ 0x110DC dereferences +0x10 with no test)");
    CheckEqI32(g_nPnBlits, 3, "the controls and the close control still blit");
    Check((u16)BdPix(1, 5) != 0 && (u16)BdPix(234, 318) != 0, "and the rails are drawn -- they need no env at all");
    g_pBiPadWord = &g_biPad;
}

/* ---- fixtures for Geoscape_DrawFundingArm (Geoscape_Draw case 19, 0x94314) --------------------- */
#define FN_PAD_X   214   /* the report pad's glyph; the panel frame spans x 2..238 at y 80..170 */
#define FN_PAD_Y   200
#define FN_BAR_X   200   /* a column the FULL frame's outer bar reaches (x .. x+w+4) and a 1%-grown one does not */
#define FN_BAR_Y    80

static PadImage                g_fnPad;
static u8                      g_abFnPadGlyph[8];
static UiControl               g_fnCloseCtl;
static PadImage                g_fnClosePad;
static FundingDrawEnv          g_fnDraw;
static GeoscapeFundingArmEnv   g_fnArm;
static GfxColorEnv             g_fnColor;
static UiAnimBlitEnv           g_fnAnim;
static u8                      g_bFnAnimStarted;
static i8                      g_nFnAnimProgress;
static i32                     g_anFnShare[5], g_anFnBase[5], g_anFnBar[5];
static i32                     g_nFnFunds, g_nFnTotal, g_nFnMonth;
static u8                      g_nFnBaseCount;
static u8                      g_abFnGrid[1600];

/* Fill one base's 36 facility-type cells (Base_CalcMonthlyUpkeep reads abFacilityGrid + 480 + 144*idx). */
static void FnFillFacilities(i32 nBase, i32 nCells)
{
    const i32 nOff = 480 + 144 * nBase;
    for (i32 i = 0; i < 36; ++i) {
        g_abFnGrid[nOff + i] = (u8)((i < nCells) ? 12 : 255);
    }
}

/* One projected-balance run: everything fixed except the funds and the five bars. */
static void FnGateRun(i32 nFunds, const i32 *aBars)
{
    g_nFnFunds = nFunds;
    for (i32 i = 0; i < 5; ++i) {
        g_anFnBar[i] = (aBars != NULL) ? aBars[i] : 0;
    }
    ArReset();
    Geoscape_DrawFundingArm(&g_fnDraw, &g_fnArm);
}

static void TestGeoscapeFundingArm(void)
{
    printf("\nGeoscape_Draw's funding arm @ 0x94314\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_aPalette[PN_TEST_PAL_SLOT][21] = (u32)AR_CAL_INK;
    ArMakeGlyph(&g_fnPad, g_abFnPadGlyph, FN_PAD_X, FN_PAD_Y);
    memset(&g_fnClosePad, 0, sizeof(g_fnClosePad));
    g_fnClosePad.uWidth = 2;
    g_fnClosePad.uHeight = 2;
    memset(&g_fnCloseCtl, 0, sizeof(g_fnCloseCtl));
    g_fnCloseCtl.apPads[0] = &g_fnClosePad;
    g_fnCloseCtl.bEnabled   = 1;

    g_bFnAnimStarted = 1;              /* the animation is already running: the arm sees the progress byte only */
    g_nFnAnimProgress = 100;           /* complete */
    memset(&g_fnAnim, 0, sizeof(g_fnAnim));
    g_fnAnim.pbStarted   = &g_bFnAnimStarted;
    g_fnAnim.pnProgress  = &g_nFnAnimProgress;
    g_fnAnim.nFrameStyle = 0;          /* the ordinary frame colours */

    memset(&g_fnDraw, 0, sizeof(g_fnDraw));
    g_fnDraw.pPad         = &g_fnPad;
    g_fnDraw.pCloseCtl    = &g_fnCloseCtl;
    g_fnDraw.pfnCtlBlit   = PnBlit;
    g_fnDraw.pfnCtlBlitEx = PnBlitEx;
    g_fnDraw.pColor       = &g_fnColor;
    g_fnDraw.pTextRecs    = (const u8 *)g_abPnText;
    g_fnDraw.pnTotal      = &g_nFnTotal;
    g_fnDraw.anShare      = g_anFnShare;
    g_fnDraw.anBaseline   = g_anFnBase;
    g_fnDraw.anBar        = g_anFnBar;
    g_fnDraw.nMonthCounter = 5;        /* >= 3: the arrows are shown as they are */

    memset(g_anFnShare, 0, sizeof(g_anFnShare));
    memset(g_anFnBase, 0, sizeof(g_anFnBase));
    memset(g_abFnGrid, 255, sizeof(g_abFnGrid));   /* 255 is an EMPTY facility cell (Base_CalcMonthlyUpkeep) */
    memset(&g_fnArm, 0, sizeof(g_fnArm));
    g_fnArm.pbBaseCount   = &g_nFnBaseCount;
    g_fnArm.pnFunds       = &g_nFnFunds;
    g_fnArm.anFundBar     = g_anFnBar;
    g_fnArm.abFacilityGrid = g_abFnGrid;
    g_fnArm.pAnim         = &g_fnAnim;
    g_fnArm.pColor        = &g_fnColor;
    g_fnArm.pTextRecs     = g_abPnText;
    g_nFnBaseCount = 0;
    g_nFnMonth     = 5;

    /* ---- the report runs first, whatever the balance says ---- */
    FnGateRun(-3000000, NULL);
    Check((u16)BdPix(FN_PAD_X, FN_PAD_Y) == AR_CAL_INK,
          "FundingScreen_DrawExtended blits pFundPad (0x5B5D4) -- the arm calls it at 0x94318 with the ADDRESS of the global, which the callee never uses (0x5B5CC loads it itself)");
    CheckEqI32(g_nPnBlits, 1, "and its close control (0x5B77C) is the only CONTROL blit -- the pad is painted by PadImage_Blit itself, which is not in the injected log");
    Check(g_apnPnBlitPad[0] == &g_fnClosePad, "that one blit is g_pUiCloseCtl, and nothing of the ARM's own runs on this path");
    Check((u16)BdPix(FN_BAR_X, FN_BAR_Y) == 0,
          "at EXACTLY -3,000,000 the panel is not drawn: 0x943AC CMP #0xFFD23940 / BGE def_8C1B0, and 0xFFD23940 is -3,000,000 signed");

    /* ---- one credit deeper and the whole panel appears ---- */
    FnGateRun(-3000001, NULL);
    Check((u16)BdPix(FN_BAR_X, FN_BAR_Y) != 0,
          "one credit deeper the animated frame is drawn, mode 4 at (2,80) 232x90 (0x943B0..0x943EC) -- x 200 is inside its outer bar");

    /* ---- the animation gate at 0x943F0 ---- */
    g_nFnAnimProgress = 1;             /* 50*1 = 50 px square: clamped, but still short of 232x90 */
    FnGateRun(-3000001, NULL);
    Check((u16)BdPix(FN_BAR_X, FN_BAR_Y) == 0,
          "with the progress byte at 1 the blit reports the animation incomplete and the arm stops at 0x943F0 BEQ -- the frame is only 50 wide, so x 200 stays dark");
    g_nFnAnimProgress = 100;

    /* ---- the five bars are all in the projected balance (0x9437C..0x943A0) ---- */
    static const char *const aazBarSlot[5] = {
        "slot 0 alone crosses the limit -- g_nFundBar0 @ 0x186594 is in the sum (0x94394)",
        "slot 1 alone crosses it -- g_nFundBar1 @ 0x186598 (0x94380)",
        "slot 2 alone crosses it -- g_nFundBar2 @ 0x18659C (0x9437C)",
        "slot 3 alone crosses it -- g_nFundBar3 @ 0x1865A0 (0x94388)",
        "slot 4 alone crosses it -- g_nFundBar4 @ 0x1865A4 (0x943A0)",
    };
    for (i32 i = 0; i < 5; ++i) {
        i32 aBars[5] = { 0, 0, 0, 0, 0 };
        aBars[i] = -3000001;
        FnGateRun(0, aBars);
        Check((u16)BdPix(FN_BAR_X, FN_BAR_Y) != 0, aazBarSlot[i]);
    }
    {
        const i32 aSplit[5] = { 0, -1000000, 0, -2000001, 0 };
        FnGateRun(0, aSplit);
        Check((u16)BdPix(FN_BAR_X, FN_BAR_Y) != 0,
              "two bars split across slots still cross the limit -- it is one sum, not one term");
    }
    {
        const i32 aNear[5] = { 0, -1000000, 0, -1999999, 0 };
        FnGateRun(0, aNear);
        Check((u16)BdPix(FN_BAR_X, FN_BAR_Y) == 0, "and the same pair one credit short of -3,000,000 is not enough");
    }

    /* ---- the upkeep loop: every base in range, read from its own 144-byte facility slice ---- */
    memset(g_abFnGrid, 255, sizeof(g_abFnGrid));   /* 255 is an EMPTY facility cell (Base_CalcMonthlyUpkeep) */
    FnFillFacilities(0, 36);
    FnFillFacilities(1, 36);           /* 2 * 36 * 50,000 = 3,600,000 */
    g_nFnBaseCount = 3;
    FnGateRun(0, NULL);
    Check((u16)BdPix(FN_BAR_X, FN_BAR_Y) != 0,
          "36 filled facility cells at base 0 AND base 1 cross the limit (3,600,000 of upkeep), 0x9435C SUB R6,R6,R0 per base");
    memset(g_abFnGrid, 255, sizeof(g_abFnGrid));   /* 255 is an EMPTY facility cell (Base_CalcMonthlyUpkeep) */
    FnFillFacilities(1, 36);
    FnFillFacilities(2, 36);           /* the same total, shifted one base right */
    FnGateRun(0, NULL);
    Check((u16)BdPix(FN_BAR_X, FN_BAR_Y) != 0,
          "and the same total one base further right still crosses -- the slice follows the index (Base_CalcMonthlyUpkeep reads +480+144*base)");
    memset(g_abFnGrid, 255, sizeof(g_abFnGrid));   /* 255 is an EMPTY facility cell (Base_CalcMonthlyUpkeep) */
    FnFillFacilities(0, 36);
    FnFillFacilities(1, 36);
    g_nFnBaseCount = 1;                /* only base 0 is inside the loop now */
    FnGateRun(0, NULL);
    Check((u16)BdPix(FN_BAR_X, FN_BAR_Y) == 0,
          "with the base count at 1 only base 0 is summed (0x94330 LDRSB, 0x94370 CMP / BLT) -- 1,800,000 is not enough");
    g_nFnBaseCount = 0;
    memset(g_abFnGrid, 255, sizeof(g_abFnGrid));   /* 255 is an EMPTY facility cell (Base_CalcMonthlyUpkeep) */

    /* ---- the three captions: centred on 120 at y 100 / 120 / 140, in the arm's own colour pair ---- */
    PnSetText(PUFO_FUND_TEXT_CAP0, "");
    PnSetText(PUFO_FUND_TEXT_CAP1, "");
    PnSetText(PUFO_FUND_TEXT_CAP2, "");
    FnGateRun(-3000001, NULL);
    /* The reference is the same arm run with empty records, plus the three captions drawn by hand with the
     * original's own literals -- a wrong centre, y, record, colour pair or rounding shows up as a pixel. */
    (void)Gfx_PackRgb565Pair(&g_fnColor, 150, 50, 50, 50, 1, 1);            /* 0x943F8..0x94418 */
    PnSetText(PUFO_FUND_TEXT_CAP0, "MMMWW");
    PnSetText(PUFO_FUND_TEXT_CAP1, "WWQQQ");
    PnSetText(PUFO_FUND_TEXT_CAP2, "Q1A7");
    {
        const char *const psz0 = &g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_CAP0];
        const char *const psz1 = &g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_CAP1];
        const char *const psz2 = &g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_CAP2];
        Text_DrawString(120 - Text_MeasureWidth(psz0) / 2, 100, psz0);      /* 0x94444 MOV R2,#0x64 */
        Text_DrawString(120 - Text_MeasureWidth(psz1) / 2, 120, psz1);      /* 0x94474 MOV R2,#0x78 */
        Text_DrawString(120 - Text_MeasureWidth(psz2) / 2, 140, psz2);      /* 0x9449C B loc_92ACC */
        SolSaveInk();
    }
    FnGateRun(-3000001, NULL);
    SolInkIs("the three captions land centred on x 120 at y 100 / 120 / 140, from records 1712 / 1713 / 1714, "
             "drawn with the single (150,50,50) over (50,1,1) pair the arm packs at 0x943F8..0x94418");
    {
        /* The surface carries palette INDICES, so the pair itself has to be read where it lands. */
        const u32 uPair1 = g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_FIRST];
        const u32 uPair2 = g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_SECOND];
        (void)Gfx_PackRgb565Pair(&g_fnColor, 150, 50, 50, 50, 1, 1);   /* 0x94410 #0x96 / #0x32 / #0x32 and 0x32/1/1 */
        Check(g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_FIRST] == uPair1
              && g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_SECOND] == uPair2,
              "the pair in the text palette is what 0x943F8..0x94418 packs -- (150,50,50) over (50,1,1), not what the report left there");
    }

    /* Each record on its own pins the record-to-row pairing, not just "some text appeared". */
    PnSetText(PUFO_FUND_TEXT_CAP0, "");
    PnSetText(PUFO_FUND_TEXT_CAP1, "WWQQQ");
    PnSetText(PUFO_FUND_TEXT_CAP2, "");
    FnGateRun(-3000001, NULL);
    SolSaveInk();
    PnSetText(PUFO_FUND_TEXT_CAP0, "");
    PnSetText(PUFO_FUND_TEXT_CAP1, "");
    PnSetText(PUFO_FUND_TEXT_CAP2, "");
    FnGateRun(-3000001, NULL);
    (void)Gfx_PackRgb565Pair(&g_fnColor, 150, 50, 50, 50, 1, 1);
    {
        const char *const psz1 = &g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * PUFO_FUND_TEXT_CAP1];
        PnSetText(PUFO_FUND_TEXT_CAP1, "WWQQQ");
        Text_DrawString(120 - Text_MeasureWidth("WWQQQ") / 2, 120, "WWQQQ");
        (void)psz1;
    }
    SolInkIs("record 1713 alone appears at the y 120 row (0x94454..0x94480)");
    PnSetText(PUFO_FUND_TEXT_CAP1, "");
}

/* ---- fixtures for Geoscape_DrawPlaneArm (Geoscape_Draw case 6, 0x9309C) ---------------------- */
#define PL_PAD_X     118   /* the backdrop pad's glyph: clear of every rail and probe window */
#define PL_PAD_Y     290
#define PL_BAR_X     100   /* a column the 226-wide row bars reach, and nothing else here reaches */

static PadImage              g_plBackPad;
static PadImage             *g_pPlBackPad = &g_plBackPad;
static u8                    g_abPlBackGlyph[8];
/* One slot past the block (+0x6C) is a real control here, so any walk past +0x68 shows up in the log. */
static UiControl             g_plCtl[PUFO_PLANE_ARM_CTLS + 1];
static PadImage              g_plCtlPad[PUFO_PLANE_ARM_CTLS + 1];
static UiControl            *g_apPlCtl[PUFO_PLANE_ARM_CTLS + 1];
static Ship                  g_plCraft[2];
static Ship                 *g_apPlCraft[2];
static ObjectRecFull         g_plObj[5];
static ObjectRecFull        *g_apPlObj[106];
static GfxColorEnv           g_plColor;
static PlaneDrawEnv          g_plDraw;
static GeoscapePlaneArmEnv   g_plArm;
static GeoscapeScreenArmEnv  g_plCtlEnv;
static UiPanel               g_plPanelSel, g_plPanelCommit;
static UiPanel              *g_pPlPanelSel, *g_pPlPanelCommit;
static i32                   g_anPlStats[6];
static i32                   g_nPlRowCount, g_nPlCraftIdx, g_nPlDrawCraftIdx, g_nPlDrawCurY;
static i8                    g_nPlBase;
static i16                   g_nPlCurX, g_nPlCurY;
static u8                    g_bPlStores, g_bPlList, g_bPlDetail, g_bPlStatA, g_bPlStatB, g_bPlPreview;
static u8                    g_bPlDrawDetail, g_bPlDrawPanels, g_bPlDrawPreview;

static void PlSetStats(const i32 *aStats)
{
    for (i32 i = 0; i < PUFO_PLANE_STAT_ROWS; ++i) {
        g_anPlStats[i] = aStats[i];
    }
}

/* Replace one TEXTS record for the suite and keep the original so the run does not damage it. */
static void PlSetText(i32 nRec, const char *psz, char *pszSave)
{
    snprintf(pszSave, PUFO_TEXT_REC_STRIDE, "%s", &g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * nRec]);
    PnSetText(nRec, psz);
}

/* One arm run, with the two scroll panels' row slots parked so a TrackHold call is visible. */
static void PlRun(void)
{
    ArReset();
    *ArPanelRowSlot(&g_plPanelSel) = -999;
    *ArPanelRowSlot(&g_plPanelCommit) = -999;
    Geoscape_DrawPlaneArm(&g_plArm);
}

/* True when none of the first nWant blits is one of the pads listed after it. */
static bool PlNoBlitsOf(i32 nWant, i32 nFirstPad, i32 nPadCount)
{
    for (i32 i = 0; i < nWant; ++i) {
        for (i32 p = nFirstPad; p < nFirstPad + nPadCount; ++p) {
            if (g_apnPnBlitPad[i] == &g_plCtlPad[p]) {
                return false;
            }
        }
    }
    return true;
}

static void TestGeoscapePlaneArm(void)
{
    printf("\nGeoscape_Draw's hangar arm @ 0x9309c\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_aPalette[PN_TEST_PAL_SLOT][21] = (u32)AR_CAL_INK;
    ArMakeGlyph(&g_plBackPad, g_abPlBackGlyph, PL_PAD_X, PL_PAD_Y);

    /* The block at 0x161A48: its pad word, then the fifteen control words from +0x30 to +0x68. Every
     * control carries its own pad so a blit says which slot it came from. y = -1 is the parked value:
     * the arm writes y only for the six stat rows. */
    for (i32 i = 0; i < PUFO_PLANE_ARM_CTLS + 1; ++i) {
        memset(&g_plCtl[i], 0, sizeof(g_plCtl[i]));
        memset(&g_plCtlPad[i], 0, sizeof(g_plCtlPad[i]));
        g_plCtlPad[i].uWidth = 2;
        g_plCtlPad[i].uHeight = 2;
        g_plCtl[i].apPads[0] = &g_plCtlPad[i];
        g_plCtl[i].bEnabled  = 1;
        g_plCtl[i].x         = (i16)(10 + i);
        g_plCtl[i].y         = -1;
        g_apPlCtl[i]         = &g_plCtl[i];
    }

    memset(g_plCraft, 0, sizeof(g_plCraft));
    g_plCraft[0].bCrewSlots   = 2;
    g_plCraft[0].bCanDogfight = 1;
    g_apPlCraft[0] = &g_plCraft[0];
    g_apPlCraft[1] = &g_plCraft[1];

    /* apObjects @ 0x113AC0: only the five ammo slots the stat panel reads are non-null. */
    memset(g_apPlObj, 0, sizeof(g_apPlObj));
    memset(g_plObj, 0, sizeof(g_plObj));
    g_apPlObj[52] = &g_plObj[0];   /* Craft cannon rounds */
    g_apPlObj[53] = &g_plObj[1];   /* Stingray rocket */
    g_apPlObj[54] = &g_plObj[2];   /* Avalanche rocket */
    g_apPlObj[55] = &g_plObj[3];   /* Fusion ball */
    g_apPlObj[105] = &g_plObj[4];  /* Elerium-115, Map_apObjects105 */
    for (i32 i = 0; i < 5; ++i) {
        g_plObj[i].auPerBaseVal[1] = 1;
    }

    /* The callee's env. In the binary BasePlaneScreen_Draw gates its own detail branch on the SAME byte
     * 0x161F20 the arm gates its prefix on (0x7D37C), and with that byte set it hands the whole screen to
     * BasePlaneScreen_DrawCraftDetail @ 0x7baf8 -- 3,824 B that would paint over every probe here. This
     * suite tests the ARM's gate, so the callee's copy stays clear; the coupling itself is recorded in
     * pufo_plane.h:745 and exercised by test_plane.c. */
    memset(&g_plDraw, 0, sizeof(g_plDraw));
    g_plDraw.pColor           = &g_plColor;
    g_plDraw.pbShowCraftDetail = &g_bPlDrawDetail;
    g_plDraw.pbPanelsBuilt    = &g_bPlDrawPanels;
    g_plDraw.pbPreviewActive  = &g_bPlDrawPreview;
    g_plDraw.pnCursorY        = &g_nPlDrawCurY;
    g_plDraw.apCraft          = g_apPlCraft;
    g_plDraw.nCraftCount      = 2;
    g_plDraw.pnSelCraftIdx    = &g_nPlDrawCraftIdx;   /* nPlaneSelCraftIdx @ 0x161F04, a DIFFERENT global */
    g_plDraw.apObjects        = g_apPlObj;
    g_plDraw.nObjectCount     = 106;
    g_plDraw.pTextRecs        = g_abPnText;

    memset(&g_plCtlEnv, 0, sizeof(g_plCtlEnv));
    g_plCtlEnv.apCtl          = g_apPlCtl;
    g_plCtlEnv.pfnCtlBlit     = PnBlit;
    g_plCtlEnv.pfnCtlBlitEx   = PnBlitEx;

    memset(&g_plArm, 0, sizeof(g_plArm));
    g_plArm.ppBackdropPad     = &g_pPlBackPad;
    g_plArm.apCtl             = g_apPlCtl;
    g_plArm.pbStoresCommitted = &g_bPlStores;
    g_plArm.pbListPanelsBuilt = &g_bPlList;
    g_plArm.pbShowCraftDetail = &g_bPlDetail;
    g_plArm.pbStatPanelAOpen  = &g_bPlStatA;
    g_plArm.pbStatPanelBOpen  = &g_bPlStatB;
    g_plArm.pbPreviewOk       = &g_bPlPreview;
    memset(&g_plPanelSel, 0, sizeof(g_plPanelSel));
    memset(&g_plPanelCommit, 0, sizeof(g_plPanelCommit));
    g_plPanelSel.nPosY        = 100;   /* the row is (cursorY - nPosY) / nFont */
    g_plPanelSel.nFont        = 10;
    g_plPanelCommit.nPosY     = 200;
    g_plPanelCommit.nFont     = 10;
    g_pPlPanelSel             = &g_plPanelSel;
    g_pPlPanelCommit          = &g_plPanelCommit;
    g_plArm.ppSelRowPanel     = &g_pPlPanelSel;
    g_plArm.ppCommitRowPanel  = &g_pPlPanelCommit;
    g_plArm.pnCursorX         = &g_nPlCurX;
    g_plArm.pnCursorY         = &g_nPlCurY;
    g_plArm.pnSelCraftIdx     = &g_nPlCraftIdx;
    g_plArm.pnSelBaseIdx      = &g_nPlBase;
    g_plArm.apCraft           = g_apPlCraft;
    g_plArm.nCraftCount       = 2;
    g_plArm.apObjects         = g_apPlObj;
    g_plArm.nObjectCount      = 106;
    g_plArm.pColor            = &g_plColor;
    g_plArm.pnRowCount        = &g_nPlRowCount;
    g_plArm.pnStats           = g_anPlStats;
    g_plArm.pTextRecs         = g_abPnText;
    g_plArm.pPlaneEnv         = &g_plDraw;
    g_plArm.pCtlEnv           = &g_plCtlEnv;

    g_nPlCraftIdx = 0;
    g_nPlDrawCraftIdx = 0;
    g_nPlBase = 1;
    g_nPlCurX = 50;
    g_nPlCurY = 150;
    g_nPlDrawCurY = 150;
    g_bPlPreview = 1;

    /* Records 97/98/99 carry real lang.dat words ("Экипаж", "Оборуд.", "Нет"), but earlier suites in this
     * process blank records they probe, so the three this suite measures are set explicitly and put back. */
    char szOld97[PUFO_TEXT_REC_STRIDE], szOld98[PUFO_TEXT_REC_STRIDE], szOld99[PUFO_TEXT_REC_STRIDE];
    PlSetText(PUFO_PLANE_ARM_TEXT_CREW, "WWQQQ", szOld97);
    PlSetText(PUFO_PLANE_TEXT_EQUIP, "WWQQQ", szOld98);
    PlSetText(PUFO_PLANE_TEXT_STAT_HDR, "WWQ", szOld99);

    /* ---- the prefix: the two paired control runs, the crew slots and the two captions ---------- */
    const i32 aAllOne[6] = { 1, 1, 1, 1, 1, 1 };
    PlSetStats(aAllOne);
    g_nPlRowCount = 2;
    g_bPlDetail = 1; g_bPlStores = 1; g_bPlList = 1; g_bPlStatA = 1; g_bPlStatB = 0;
    g_plCraft[0].bCrewSlots = 2;
    g_plCraft[0].bCanDogfight = 1;
    PlRun();
    Check((u16)BdPix(PL_PAD_X, PL_PAD_Y) == AR_CAL_INK,
          "the arm blits *pPlaneBackdropPad @ 0x161A48 before anything else (0x930A0 / 0x930A4)");
    CheckEqI32(g_nPnBlits, 11,
               "two store controls, two nav controls, the stat header and the six stat rows -- the crew and caption "
               "loops need all four flags CLEAR (0x931A0..0x931C8), so neither runs here");
    Check(g_apnPnBlitPad[0] == &g_plCtlPad[13] && g_apnPnBlitPad[1] == &g_plCtlPad[14],
          "the store pair first: block +0x64 / +0x68, pPlaneStoreNavPrevCtl then NextCtl (0x9312C..0x93148)");
    Check(g_apnPnBlitPad[2] == &g_plCtlPad[11] && g_apnPnBlitPad[3] == &g_plCtlPad[12],
          "then the nav pair at block +0x5C / +0x60 (0x93164..0x93180) -- bPlaneListPanelsBuilt is the gate");
    Check(g_apnPnBlitPad[4] == &g_plCtlPad[4] && g_apnPnBlitPad[5] == &g_plCtlPad[5],
          "then the stat header control at +0x40 and the first row's at +0x44 (0x93460, 0x934C4)");
    Check(PlNoBlitsOf(11, 0, 4), "and none of the four crew/caption controls at +0x30..+0x3C appears on this path");
    Check(BfaInk(PL_BAR_X, 92, PL_BAR_X, 92) > 0 && BfaInk(PL_BAR_X, 108, PL_BAR_X, 108) > 0
          && BfaInk(PL_BAR_X, 112, PL_BAR_X, 112) > 0 && BfaInk(PL_BAR_X, 128, PL_BAR_X, 128) > 0,
          "nPlaneStatRowCount 2 draws both bars at y 92/108 and 112/128 -- 226 wide at x 7, 1 tall (0x933EC, 0x93420)");
    Check(BfaInk(PL_BAR_X, 132, PL_BAR_X, 148) == 0, "and no bars past the row count");
    Check(BfaInk(50, 94, 96, 100) > 0 && BfaInk(20, 94, 44, 100) == 0,
          "TEXTS[99] is centred on x 73 at y 92 (0x9344C..0x93488) -- the measure runs at 0x93454, the header "
          "control blits between it and the draw at 0x93460, and nothing reaches x 44");
    Check(g_plCtl[5].y == 112 && g_plCtl[6].y == 152 && g_plCtl[7].y == 192
          && g_plCtl[8].y == 232 && g_plCtl[9].y == 272 && g_plCtl[10].y == 312,
          "the six rows land at 112,152,192,232,272,312 -- NOT 112,132,...,212: each row advances the shared scratch counter twice (0x9349C and 0x93600..0x93608), the ORIGINAL BUG this port reproduces");

    /* ---- bPlaneShowCraftDetail clear: 0x93114 BEQ skips the whole prefix ---- */
    g_bPlDetail = 0;
    PlRun();
    CheckEqI32(g_nPnBlits, 7,
               "with the detail view off the arm blits only the stat header and the six rows -- no store, nav, crew or caption control");
    Check(PlNoBlitsOf(7, 0, 4) && PlNoBlitsOf(7, 11, 4),
          "none of the four crew controls or the four paired controls appears (0x93114 BEQ loc_93380)");
    Check(g_apnPnBlitPad[0] == &g_plCtlPad[4], "and the run starts at the header control at block +0x40");

    /* ---- the doubled counter, measured on the rails ---- */
    const i32 aTwo[6] = { 1, 0, 1, 0, 0, 0 };
    PlSetStats(aTwo);
    g_nPlRowCount = 1;
    for (i32 i = 0; i < PUFO_PLANE_ARM_CTLS + 1; ++i) {
        g_plCtl[i].y = -1;
    }
    PlRun();
    CheckEqI32(g_nPnBlits, 3, "only the header and the two rows with a count blit (0x934A4 BLE skips the rest)");
    Check(g_plCtl[5].y == 112 && g_plCtl[7].y == 172,
          "row 1 writes 92+20*1 and row 3 writes 92+20*4 -- the empty rows in between advanced the counter too");
    Check(g_plCtl[6].y == -1 && g_plCtl[8].y == -1 && g_plCtl[9].y == -1 && g_plCtl[10].y == -1,
          "an empty row never touches its control: the BLE lands past the STRH [R0,#2] at 0x934BC");
    Check(BfaInk(141, 252, 141, 252) > 0 && BfaInk(186, 252, 186, 252) > 0 && BfaInk(228, 252, 228, 252) > 0,
          "the three column rails at x 141 / 186 / 228 end EXACTLY at y 252 -- 20 * 8 from y 93 (0x93CE8 MUL R3,R0,R7 "
          "with the doubled counter, 0x93D08 MOV R2,#0x5D)");
    Check(BfaInk(141, 253, 141, 255) == 0 && BfaInk(186, 253, 228, 255) == 0,
          "and stop there -- a panel listing its 1+2 rows would need 60 pixels, not 160");

    /* ---- the crew markers, and the unbounded crew index ---- */
    g_bPlDetail = 1; g_bPlStores = 0; g_bPlList = 0; g_bPlStatA = 0; g_bPlStatB = 0;
    g_plCraft[0].bCrewSlots = 1;
    g_plCraft[0].bCanDogfight = 0;
    PlRun();
    CheckEqI32(g_nPnBlits, 1, "one slot, one control (0x931EC..0x93290) -- and with neither stat panel open the arm ends at 0x9339C");
    Check(BfaInk(28, 103, 34, 107) > 0, "the \"1\" marker is drawn at (28,103) (0x93234..0x93244, predicated HI on the slot count)");
    Check(BfaInk(205, 103, 211, 107) == 0, "and \"2\" is not: 0x93258 CMP R3,#1 / BLHI needs more than one slot");
    Check(BfaInk(PL_BAR_X, 92, PL_BAR_X, 108) == 0, "no stat bars either -- both stat panel bytes are clear");
    g_plCraft[0].bCrewSlots = 2;
    PlRun();
    Check(BfaInk(205, 103, 211, 107) > 0, "with two slots the second marker appears at (205,103) (0x9325C..0x9326C)");
    CheckEqI32(g_nPnBlits, 2, "and both crew controls blit");
    g_plCraft[0].bCrewSlots = 6;
    PlRun();
    CheckEqI32(g_nPnBlits, 6, "six slots walk the block's control words in order -- 0x931EC indexes +0x30 + 4*i with no bound at all");
    Check(g_apnPnBlitPad[4] == &g_plCtlPad[4] && g_apnPnBlitPad[5] == &g_plCtlPad[5],
          "so iterations five and six land on the STAT controls at +0x40 and +0x44: the block layout is what the arm reads");

    /* ---- the crew and caption loops need all four panel flags CLEAR ---- */
    PlSetStats(aAllOne);
    g_bPlDetail = 1; g_bPlStores = 1; g_bPlList = 0; g_bPlStatA = 1; g_bPlStatB = 0;
    g_plCraft[0].bCrewSlots = 2;
    g_plCraft[0].bCanDogfight = 1;
    PlRun();
    CheckEqI32(g_nPnBlits, 9,
               "with bPlaneStoresCommitted set, the crew controls do NOT blit (0x931C8 BNE loc_93298) and neither do "
               "the caption controls (0x932C4 BNE loc_93380): the store pair, the header and six rows");
    Check(PlNoBlitsOf(9, 0, 4), "none of the four controls at +0x30..+0x3C appears on this path");
    g_bPlStores = 0;
    g_bPlList = 1;
    PlRun();
    CheckEqI32(g_nPnBlits, 9, "and the same with bPlaneListPanelsBuilt set instead (0x931B8 and 0x932B4 BNE)");
    Check(PlNoBlitsOf(9, 0, 4), "again no crew or caption control blits");
    g_bPlStatA = 0;
    g_bPlStatB = 0;
    g_bPlStores = 1;
    g_bPlList = 0;
    PlRun();
    CheckEqI32(g_nPnBlits, 2,
               "with ONLY bPlaneStoresCommitted set the crew loop is gated off on that byte alone (0x931BC..0x931C8 "
               "BNE loc_93298) -- the stat panel is closed too, so only the store pair blits");
    Check(PlNoBlitsOf(2, 0, 4), "no control at +0x30..+0x3C appears");
    g_bPlStores = 0;
    g_bPlList = 1;
    PlRun();
    CheckEqI32(g_nPnBlits, 2, "and bPlaneListPanelsBuilt gates it just as hard on its own (0x931B4 ANDS / 0x931B8 BNE)");
    Check(PlNoBlitsOf(2, 0, 4), "again none of the four crew/caption controls");

    /* ---- the two captions, gated on Ship.bCanDogfight @ +0x21 ---- */
    g_bPlStores = 0;
    g_bPlList = 0;
    g_bPlStatA = 0;
    g_bPlStatB = 0;
    g_plCraft[0].bCrewSlots = 4;
    g_plCraft[0].bCanDogfight = 1;
    PlRun();
    CheckEqI32(g_nPnBlits, 6, "four crew controls plus the caption pair");
    Check(g_apnPnBlitPad[4] == &g_plCtlPad[2] && g_apnPnBlitPad[5] == &g_plCtlPad[3],
          "the caption pair is the block's last two crew controls, +0x38 and +0x3C");
    Check(BfaInk(20, 201, 90, 207) > 0, "TEXTS[97] \"Экипаж\" is centred on x 53 at y 201 (0x93314..0x9333C)");
    Check(BfaInk(20, 241, 90, 247) > 0, "TEXTS[98] \"Оборуд.\" on the same centre at y 241 (0x93340..0x93368)");
    g_plCraft[0].bCanDogfight = 0;
    PlRun();
    CheckEqI32(g_nPnBlits, 4, "with bCanDogfight clear the caption pair is skipped (0x932A8 BEQ loc_93380)");
    Check(BfaInk(20, 201, 90, 207) == 0 && BfaInk(20, 241, 90, 247) == 0, "and neither caption is drawn");

    /* ---- the two store lists track the map cursor, on two gates ---- */
    g_bPlDetail = 0;
    g_bPlStores = 1;
    g_bPlPreview = 1;
    PlRun();
    CheckEqI32(*ArPanelRowSlot(&g_plPanelSel), 5,
               "stores committed AND the root block's +0x35 byte set: pPlaneSelRowPanel gets (150 - 100) / 10 (0x930DC)");
    CheckEqI32(*ArPanelRowSlot(&g_plPanelCommit), -5,
               "and pPlaneCommitRowPanel gets the SAME cursor: (150 - 200) / 10 (0x930F0)");
    g_bPlPreview = 0;
    PlRun();
    CheckEqI32(*ArPanelRowSlot(&g_plPanelSel), -999,
               "with bBaseFacPreviewOk @ 0x18652D clear (0x930B8 LDRB [R7,#0x35]) neither list is touched");
    CheckEqI32(*ArPanelRowSlot(&g_plPanelCommit), -999, "not the commit list either");
    g_bPlPreview = 1;
    g_bPlStores = 0;
    PlRun();
    CheckEqI32(*ArPanelRowSlot(&g_plPanelSel), -999, "and bPlaneStoresCommitted @ 0x161F28 clear gates it just as hard (0x930B4 BEQ)");

    /* ---- the stock column reads each weapon's own ammo object ---- */
    PlSetStats(aAllOne);
    g_nPlRowCount = 1;
    g_bPlStores = 1;
    g_bPlStatA = 1;                /* the stat panel has to be open for any of this to draw */
    g_plObj[0].auPerBaseVal[1] = 65535;  /* apObjects[52], Craft cannon rounds */
    g_plObj[4].auPerBaseVal[1] = 65535;  /* apObjects[105], Elerium-115 */
    PlRun();
    Check(BfaInk(188, 114, 196, 120) > 0,
          "row 1's stock column runs out to x 193: apObjects[52]->auPerBaseVal[(char)nSelectedBaseIdx], the LDRH at +0x2A (0x93594..0x935FC)");
    Check(BfaInk(188, 154, 196, 160) == 0, "row 2's does not -- apObjects[53] holds 1 and \"1\" centres on 207");
    Check(BfaInk(188, 274, 196, 280) > 0, "row 5 reads apObjects[105]: Map_apObjects105, Elerium-115, the plasma cannon's ammo (0x93AE4..0x93B64)");
    /* Row 6 sits at y 312, and the hangar frame's own bottom rail runs along y 315 -- the probe skips that
     * line and reads the two bands around it. */
    Check(BfaInk(188, 313, 196, 314) == 0 && BfaInk(188, 316, 196, 318) == 0,
          "and row 6 reads apObjects[55] (Fusion ball), still 1 here (0x93C70)");
    Check(BfaInk(204, 234, 210, 240) > 0, "row 4, the laser cannon, draws the single \"-\" at x 205 with no measure at all (0x939D8..0x939F0)");
    Check(BfaInk(194, 234, 202, 240) == 0, "and nothing where its stock number would be: weapon 28 has no ammo object");
    g_plObj[4].auPerBaseVal[1] = 1;
    g_plObj[3].auPerBaseVal[1] = 65535;  /* apObjects[55], Fusion ball */
    PlRun();
    Check(BfaInk(188, 313, 196, 314) > 0 || BfaInk(188, 316, 196, 318) > 0,
          "with apObjects[55] the only big count, row 6's stock column runs out to x 193 (0x93C70..0x93CD8)");
    Check(BfaInk(188, 234, 196, 240) == 0,
          "while row 4 stays empty even though slot 55 is right there -- 0x939D8 loads asc_AB340 and draws it at x 205");
    g_plObj[3].auPerBaseVal[1] = 1;
    g_plObj[0].auPerBaseVal[1] = 1;
    g_plObj[4].auPerBaseVal[1] = 1;
    PnSetText(PUFO_PLANE_ARM_TEXT_CREW, szOld97);
    PnSetText(PUFO_PLANE_TEXT_EQUIP, szOld98);
    PnSetText(PUFO_PLANE_TEXT_STAT_HDR, szOld99);
}

/* ---- fixtures for Geoscape_DrawPlacementArm (Geoscape_Draw case 1, 0x91DC8) ------------------ */
#define PC_PAD_X     100   /* clear of the rails (x 2, 210, 237), the separator (y 35) and both caption columns */
#define PC_PAD_Y      60
#define PC_CAPDY      14   /* one text row's ink band: the font paints its glyph 3 rows below the y it is given */
#define PC_MONEY_X0    4   /* the money line's probe window -- the left rail is x 2, the strip starts at x 211 */
#define PC_MONEY_Y0   18
#define PC_MONEY_X1   60
#define PC_MONEY_Y1   33
#define PC_NAME_X0    80   /* the base name's probe window, centred on 120 */
#define PC_NAME_Y0     0
#define PC_NAME_X1   160
#define PC_NAME_Y1    15

/* The colour Blit_CopyToXY actually writes: the index supplies the 5-bit top field and the two
 * colour operands the middle and low ones (pufo_render.c:242). Every rail, separator and cursor
 * claim below is an exact pixel value, so a wrong colour index is caught, not just missing ink. */
#define PC_PACK(nIdx, nMid, nLow)                                                  \
    ((u16)(((u32)(nIdx) >> 3) << 11) + 32u * ((u32)(nMid) >> 2) + ((u32)(nLow) >> 3))

static PadImage                g_pcPad;
static u8                      g_abPcPadGlyph[8];
static UiControl               g_pcNavCtl[PUFO_PLA_NAV_CTLS];
static PadImage                g_pcNavPad[PUFO_PLA_NAV_CTLS];
static UiControl              *g_apPcNav[PUFO_PLA_NAV_CTLS];
static UiControl               g_pcCloseCtl;
static PadImage                g_pcClosePad;
static UiControl               g_pcMapCtl;
static PadImage                g_pcMapPad;
static GeoscapePlacementArmEnv g_pcArm;
static GeoscapeScreenArmEnv    g_pcCtlEnv;
static FacNameGridEnv          g_pcGrid;
static BaseStripDrawEnv        g_pcStrip;
static GfxColorEnv             g_pcColor;
static UiAnimBlitEnv           g_pcAnim;
static u8                      g_bPcAnimStarted;
static i8                      g_nPcAnimProgress;
static u8                      g_abPcGrid[PUFO_GRID_SIZE];
static u8                      g_bPcPlaceActive, g_bPcPlaceFail;
static u8                      g_nPcBaseCount, g_nPcSelBaseByte, g_nPcCraftCount, g_nPcMapPhase;
static i8                      g_nPcSelBase;
static i32                     g_nPcTapX, g_nPcTapY, g_nPcOriginX, g_nPcOriginY, g_nPcFunds;
static BaseSiteRec             g_aPcSites[PUFO_FAC_BASE_COUNT];
static BaseFacFlag             g_aPcFlags[PUFO_BASEFAC_COUNT];

/* Distinct bodies: identity is what these two checks assert (0x9203C vs 0x92764), and two EMPTY
 * stubs fold to one address under /OPT:ICF -- a swap would have been invisible. Each records an id. */
static i32 g_nPcSeamFired;
static void PcCloseClick(void) { g_nPcSeamFired = 1; } /* installed at 0x9203C */
static void PcMapClick(void)   { g_nPcSeamFired = 2; } /* installed at 0x92764 */

static void PcSetFlag(i32 nKind, const char *psz)
{
    snprintf(g_aPcFlags[nKind].szName, sizeof(g_aPcFlags[nKind].szName), "%s", psz);
}

static void PcSetName(i32 nBase, const char *psz)
{
    snprintf(g_aPcSites[nBase].abName, sizeof(g_aPcSites[nBase].abName), "%s", psz);
}

/* The byte the cursor block reads: fac[nSelBase].tileA[tapY][tapX], the same 24/6/62 arithmetic
 * the arm uses at 0x923F0..0x9241C. */
static u8 *PcCell(i32 nTapX, i32 nTapY)
{
    return &g_abPcGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * (i32)g_nPcSelBase
                       + PUFO_PLA_TAP_CELL_STRIDE * nTapY + nTapX];
}

static void PcRun(void)
{
    ArReset();
    Geoscape_DrawPlacementArm(&g_pcArm, &g_pcCtlEnv, &g_pcGrid, &g_pcStrip);
}

/* The arm as it stands now, parked in the diff baseline the PnDiffRect probes read against. */
static void PcBaseline(void)
{
    PcRun();
    memcpy(g_aPnBase, g_bdSurf.pPixels, sizeof(g_aPnBase));
}

/* Where a centred draw of psz ACTUALLY inks, measured from a reference draw on a clean surface at
 * centre - width/2. Glyph bearings make the first inked column differ from the call's x by a pixel or
 * two, so the arm's ink is compared against a reference span and not against an arithmetic one. */
static void PcRefSpan(i32 nCentre, i32 nY, const char *psz, i32 *pnMinX, i32 *pnMaxX)
{
    ArReset();
    (void)Text_DrawString(nCentre - (Text_MeasureWidth(psz) >> 1), nY, psz);
    *pnMinX = BD_SURF_W;
    *pnMaxX = -1;
    for (i32 y = nY; y <= nY + PC_CAPDY; ++y) {
        for (i32 x = 0; x < BD_SURF_W; ++x) {
            if ((u16)BdPix(x, y) != 0) {
                if (x < *pnMinX) {
                    *pnMinX = x;
                }
                if (x > *pnMaxX) {
                    *pnMaxX = x;
                }
            }
        }
    }
}

/* How many pixels of the window differ from its own top-left pixel: 0 means one flat fill. */
static i32 PcNotUniform(i32 nX0, i32 nY0, i32 nX1, i32 nY1)
{
    const u16 uWant = (u16)BdPix(nX0, nY0);
    i32 n = 0;
    for (i32 y = nY0; y <= nY1; ++y) {
        for (i32 x = nX0; x <= nX1; ++x) {
            if ((u16)BdPix(x, y) != uWant) {
                ++n;
            }
        }
    }
    return n;
}

static void TestGeoscapePlacementArm(void)
{
    printf("\nGeoscape_Draw's placement arm @ 0x91dc8\n");
    PnFixture(0x1E, 0, 0, 0);
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_aPalette[PN_TEST_PAL_SLOT][21] = (u32)AR_CAL_INK;

    /* The two pairs the arm ends with, packed here as the reference values. */
    const u32 uPairArm  = Gfx_PackRgb565Pair(&g_pcColor, 68, 244, 252, 4, 70, 75);
    const u32 uPairFail = Gfx_PackRgb565Pair(&g_pcColor, 0xFF, 0xFF, 0xFF, 104, 104, 104);

    ArMakeGlyph(&g_pcPad, g_abPcPadGlyph, PC_PAD_X, PC_PAD_Y);
    for (i32 i = 0; i < PUFO_PLA_NAV_CTLS; ++i) {
        memset(&g_pcNavCtl[i], 0, sizeof(g_pcNavCtl[i]));
        memset(&g_pcNavPad[i], 0, sizeof(g_pcNavPad[i]));
        g_pcNavPad[i].uWidth  = 2;
        g_pcNavPad[i].uHeight = 2;
        g_pcNavCtl[i].apPads[0] = &g_pcNavPad[i];
        g_pcNavCtl[i].bEnabled  = 1;
        g_apPcNav[i]            = &g_pcNavCtl[i];
    }
    memset(&g_pcCloseCtl, 0, sizeof(g_pcCloseCtl));
    memset(&g_pcClosePad, 0, sizeof(g_pcClosePad));
    g_pcClosePad.uWidth = 2;
    g_pcClosePad.uHeight = 2;
    g_pcCloseCtl.apPads[0] = &g_pcClosePad;
    g_pcCloseCtl.bEnabled  = 1;
    memset(&g_pcMapCtl, 0, sizeof(g_pcMapCtl));
    memset(&g_pcMapPad, 0, sizeof(g_pcMapPad));
    g_pcMapPad.uWidth  = 2;
    g_pcMapPad.uHeight = 2;
    g_pcMapCtl.apPads[0] = &g_pcMapPad;    /* pGeoscapeCtlA @ 0x186BB4 */
    g_pcMapCtl.bEnabled  = 1;
    g_pcMapCtl.uWidth    = 60;

    memset(g_abPcGrid, 0, sizeof(g_abPcGrid));
    /* The marked cell the two callees need, exactly as the base-fac arm's fixture has it: tileA 1 is
     * the stamp NameGrid_BuildSprite maps to a build icon, tileB 5 carries the name sprite, 7 days
     * makes the days label draw. Cleared again before the cursor runs below. */
    g_abPcGrid[6 * 2 + 2]                                             = 7;
    g_abPcGrid[PUFO_GRID_FAC_OFF + 6 * 2 + 2]                         = 1;
    g_abPcGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_TILEB_OFF + 6 * 2 + 2]  = 5;
    g_nPcBaseCount = 1;
    g_nPcSelBaseByte = 1;                 /* selection == count: the strip draws no erase rect */
    g_nPcCraftCount = 0;                  /* and the name grid skips its craft pass whole */
    g_nPcOriginX   = BFA_GRID_X0;         /* nSmapOriginX -- the same two globals the callee and the
                                           * cursor block use in the original */
    g_nPcOriginY   = BFA_GRID_Y0 - 36;    /* the name grid's first row is 36 below the origin */
    g_nPcFunds     = 1234567;
    g_nPcMapPhase  = 5;                   /* so the cursor colour is 10 * (5 + 15) = 200 */
    g_nPcSelBase   = 0;
    g_bPcAnimStarted  = 1;                /* the panel sees the progress byte only */
    g_nPcAnimProgress = 100;              /* complete, so mode 0 draws the full rect */
    g_pcAnim.pbStarted   = &g_bPcAnimStarted;
    g_pcAnim.pnProgress  = &g_nPcAnimProgress;
    g_pcAnim.nFrameStyle = 0;

    memset(&g_pcGrid, 0, sizeof(g_pcGrid));
    g_pcGrid.abFacilityGrid = g_abPcGrid;
    g_pcGrid.pnCraftCount   = &g_nPcCraftCount;
    g_pcGrid.pnOriginX      = &g_nPcOriginX;
    g_pcGrid.pnOriginY      = &g_nPcOriginY;
    g_pcGrid.pColor         = &g_pcColor;
    g_pcGrid.nBaseIdx       = 0;

    memset(&g_pcStrip, 0, sizeof(g_pcStrip));
    g_pcStrip.abFacilityGrid = g_abPcGrid;
    g_pcStrip.pbBaseCount    = &g_nPcBaseCount;
    g_pcStrip.pbSelectedBase = &g_nPcSelBaseByte;
    g_pcStrip.nHighlightRow  = 0;         /* deliberately wrong: 0x922B8 makes the arm overwrite it */

    memset(&g_pcArm, 0, sizeof(g_pcArm));
    g_pcArm.pPad          = &g_pcPad;
    g_pcArm.apNavCtl      = g_apPcNav;
    g_pcArm.pCloseCtl     = &g_pcCloseCtl;
    g_pcArm.pMapCtl       = &g_pcMapCtl;
    g_pcArm.pbPlaceActive = &g_bPcPlaceActive;
    g_pcArm.pbPlaceFail   = &g_bPcPlaceFail;
    g_pcArm.pnTapX        = &g_nPcTapX;
    g_pcArm.pnTapY        = &g_nPcTapY;
    g_pcArm.pnSmapOriginX = &g_nPcOriginX;
    g_pcArm.pnSmapOriginY = &g_nPcOriginY;
    g_pcArm.abFacilityGrid = g_abPcGrid;
    g_pcArm.pnSelBaseIdx  = &g_nPcSelBase;
    g_pcArm.pnMapPhase    = &g_nPcMapPhase;
    g_pcArm.pnFunds       = &g_nPcFunds;
    g_pcArm.apSiteRecs    = g_aPcSites;
    g_pcArm.paFacFlags    = g_aPcFlags;
    g_pcArm.pTextRecs     = g_abPnText;
    g_pcArm.pColor        = &g_pcColor;
    g_pcArm.pAnim         = &g_pcAnim;
    g_pcArm.pfnCloseClick = PcCloseClick;
    g_pcArm.pfnMapClick   = PcMapClick;

    memset(&g_pcCtlEnv, 0, sizeof(g_pcCtlEnv));
    g_pcCtlEnv.apCtl         = NULL;      /* this arm takes its controls from its own env */
    g_pcCtlEnv.pfnCtlBlit    = PnBlit;
    g_pcCtlEnv.pfnCtlBlitEx  = PnBlitEx;

    g_bPcPlaceActive = 0;
    g_bPcPlaceFail   = 0;
    g_nPcTapX = 0;
    g_nPcTapY = 0;

    /* ---- 1. the pad, the THREE side rails, the two edge rows, the separator ---- */
    g_nGlyphPalSlotBase = 7;              /* a value the arm must overwrite (0x92294 / 0x922AC) */
    PcRun();
    Check((u16)BdPix(PC_PAD_X, PC_PAD_Y) == AR_CAL_INK,
          "the arm blits pBaseFacScreenPad @ 0x186BA0, the same pad word case 7 uses (0x91DCC / 0x91DD0)");
    Check((u16)BdPix(PC_PAD_X + 2, PC_PAD_Y) == 0, "and the pad glyph is 2 wide, no further");

    const u16 uRise0 = PC_PACK(100, 100, 100);
    Check((u16)BdPix(2, 1) == uRise0 && (u16)BdPix(2, 7) == uRise0,
          "the left rail's first step: 1x7 at y 1, colour 100 (0x91DE4 MOV R5,#7, 0x91E18)");
    Check((u16)BdPix(2, 8) == PC_PACK(105, 105, 105),
          "the next step is colour 105 -- 24 steps of 7, the ramp gaining 5 (0x91EDC ADD R9,R9,#7)");
    Check((u16)BdPix(2, 168) == PC_PACK(215, 215, 215),
          "the climbing half ends at y 168 with colour 215 (CMP #0xA8 at 0x91EE0)");
    Check((u16)BdPix(2, 169) == PC_PACK(220, 220, 220),
          "y 169 is the first row the DESCENDING half shows: 230-10 = 220. The two halves overlap (1..168 "
          "crossing 151..318) and each k paints climbing-then-descending, so the higher k wins the overlap");
    Check((u16)BdPix(2, 151) == PC_PACK(205, 205, 205) && (u16)BdPix(2, 155) == PC_PACK(210, 210, 210),
          "the descending half's own first rects (y 151, colour 230 at 0x91E44) are painted over by the "
          "climbing pass's k=21 and k=22 (148..154 in 205, 155..161 in 210) -- visible here as 205 and 210");
    Check((u16)BdPix(2, 312) == PC_PACK(115, 115, 115) && (u16)BdPix(2, 318) == PC_PACK(115, 115, 115)
              && (u16)BdPix(2, 319) == 0,
          "and it does run to y 318 with its last colour 115 (0x91E20 ADD R2,R9,#0x97), with nothing past it");
    Check((u16)BdPix(1, 1) == 0 && (u16)BdPix(3, 1) == 0 && (u16)BdPix(2, 0) == 0,
          "one pixel wide and the first rect really starts at y 1");
    Check((u16)BdPix(237, 1) == uRise0 && (u16)BdPix(236, 1) == 0 && (u16)BdPix(238, 1) == 0,
          "the right rail at x 237 (0x91E58 MOV R1,#0xED)");
    Check((u16)BdPix(210, 1) == uRise0 && (u16)BdPix(209, 1) == 0 && (u16)BdPix(211, 1) == 0,
          "and a THIRD rail at x 210 (0x91EA0 MOV R1,#0xD2) -- the divider case 7 does not have");

    const u16 uFlat = PC_PACK(7, 7, 7);
    Check((u16)BdPix(1, 315) == uFlat && (u16)BdPix(6, 315) == uFlat && (u16)BdPix(120, 315) == uFlat,
          "the top row's climbing half is the FLAT colour 7 for every step x 1..120 -- 7 is the side "
          "rails' rect height, stored into the colour slots at 0x91F14, not a chosen colour");
    Check((u16)BdPix(121, 315) == 0 && (u16)BdPix(127, 315) == 0,
          "then the gap x 121..127, because the ramping half starts at 128 (0x91F50 ADD R1,R9,#0x80)");
    Check((u16)BdPix(128, 315) == PC_PACK(250, 250, 250) && (u16)BdPix(133, 315) == PC_PACK(245, 245, 245),
          "and that half runs 250, 245, ... in steps of 5 (0x91F5C)");
    Check((u16)BdPix(1, 314) == 0 && (u16)BdPix(1, 316) == 0, "the row is 1 pixel tall (0x91F1C MOV R3? h=1)");
    Check((u16)BdPix(1, 2) == uFlat && (u16)BdPix(115, 2) == uFlat,
          "the bottom row shares the flat climbing half (0x91F80)");
    Check((u16)BdPix(121, 2) == PC_PACK(250, 250, 250) && (u16)BdPix(124, 2) == PC_PACK(250, 250, 250)
              && (u16)BdPix(125, 2) == PC_PACK(245, 245, 245),
          "but its ramp lands at x 120..124 -- eight pixels left of the top row's, so this row has NO gap; "
          "x 120 itself shows the flat colour because the climbing pass's last step (116..120) runs later (0x91F94 ADD R1,R9,#0x78)");
    Check((u16)BdPix(120, 2) == uFlat && (u16)BdPix(120, 315) == uFlat,
          "x 120 is flat on BOTH rows, which is what separates 0x91F94's #0x78 from 0x91F50's #0x80");
    Check((u16)BdPix(128, 2) == PC_PACK(245, 245, 245) && (u16)BdPix(128, 315) == PC_PACK(250, 250, 250),
          "so x 128 is 245 on the bottom row and 250 on the top one: the two immediates differ");
    Check((u16)BdPix(0, 2) == 0 && (u16)BdPix(0, 315) == 0, "and neither row reaches x 0 (0x91F10 ADD R1,R9,#1)");

    const u16 uSep = PC_PACK(160, 160, 160);
    Check((u16)BdPix(2, 35) == uSep && (u16)BdPix(209, 35) == uSep,
          "the separator is 208 pixels of flat 160 at y 35 (0x91FB8..0x91FDC), the same bar case 7 draws");
    Check((u16)BdPix(2, 35) == uSep && (u16)BdPix(2, 34) == PC_PACK(120, 120, 120),
          "1 pixel tall: the rail colour 120 survives on the row above it, and the separator painted "
          "OVER the rail at x 2 -- the bar runs after the rails");
    Check((u16)BdPix(210, 35) == PC_PACK(120, 120, 120),
          "and stops at x 209: the third rail keeps its own colour 120 on that row");

    /* ---- 2. the ten nav controls, the close control, the two callees ---- */
    CheckEqI32(g_nGlyphPalSlotBase, PUFO_PLA_GLYPH_PAL,
               "the arm leaves Map_nGlyphPalBase at 3 (0x92294 MOV R0,#3 / 0x922AC STR) and never restores it");
    CheckEqI32(g_nPnBlits, PUFO_PLA_NAV_CTLS + 1,
               "ten nav controls then the close control (CMP #0xA at 0x92010, 0x92040)");
    bool bNavOrder = true;
    for (i32 i = 0; i < PUFO_PLA_NAV_CTLS; ++i) {
        if (g_apnPnBlitPad[i] != &g_pcNavPad[i]) {
            bNavOrder = false;
        }
    }
    Check(bNavOrder, "the nav controls go in slot order, 0x159888 + 4*i read through the grid block at +0x120 (0x91FF0)");
    Check(g_apnPnBlitPad[PUFO_PLA_NAV_CTLS] == &g_pcClosePad, "and the eleventh blit is the close control");
    CheckEqI32((i32)g_pcCloseCtl.x, PUFO_PLA_CLOSE_X, "the close control is moved to x 194 BEFORE the blit (0x9201C / 0x92028)");
    CheckEqI32((i32)g_pcCloseCtl.y, PUFO_PLA_CLOSE_Y, "and y 4 (0x92020 / 0x92034)");
    Check(g_pcCloseCtl.pfnClick == PcCloseClick, "with Geoscape_CloseUI_ThunkA stored into +0xC (0x9203C)");
    Check(BfaInk(BFA_MARK_X0, BFA_MARK_Y0, BFA_MARK_X1, BFA_MARK_Y1) > 0,
          "BaseFacScreen_DrawNameGrid ran: the marked cell at (70,160) draws its name sprite, build icon and days label");
    Check(BfaInk(BFA_STRIP_X0, BFA_STRIP_Y0, BFA_STRIP_X1, BFA_STRIP_Y1) > 0, "and Base_DrawFacilityIconStrip ran (0x922C0)");

    ArReset();
    UiControl *apNullNav[PUFO_PLA_NAV_CTLS];       /* a copy: apNavCtl is a pointer array, not a field */
    for (i32 i = 0; i < PUFO_PLA_NAV_CTLS; ++i) {
        apNullNav[i] = g_apPcNav[i];
    }
    apNullNav[3] = NULL;                            /* the original's own null test at 0x91FF8 */
    GeoscapePlacementArmEnv noNav = g_pcArm;
    noNav.apNavCtl = apNullNav;
    Geoscape_DrawPlacementArm(&noNav, &g_pcCtlEnv, &g_pcGrid, &g_pcStrip);
    CheckEqI32(g_nPnBlits, PUFO_PLA_NAV_CTLS, "a null nav slot is skipped, not a fault: nine nav plus close");
    Check(g_apnPnBlitPad[3] == &g_pcNavPad[4] && g_apnPnBlitPad[8] == &g_pcNavPad[9],
          "and the run closes up the gap -- the loop counts controls, not slots");

    ArReset();
    GeoscapePlacementArmEnv noPad = g_pcArm;
    noPad.pPad = NULL;
    Geoscape_DrawPlacementArm(&noPad, &g_pcCtlEnv, &g_pcGrid, &g_pcStrip);
    Check((u16)BdPix(PC_PAD_X, PC_PAD_Y) == 0, "with no pad word there is no pad blit");
    Check((u16)BdPix(2, 1) == uRise0 && (u16)BdPix(210, 1) == uRise0,
          "while the rails are still drawn -- they need no env at all");

    /* The strip mark, measured directionally. The chevron is the strip's own 2x2 step in colour
     * index 0xFF/0/0 (0x62720), and a DIRECT call with highlight 0 is the control that shows where it
     * lands; the arm's run has to leave those same pixels as ordinary cell blocks (0x922B8 MOV R1,#0xFFFFFFFF
     * overwrites the env's argument before the call). */
    const u16 uChevron = PC_PACK(0xFF, 0, 0);
    ArReset();
    BaseStripDrawEnv ctlMark = g_pcStrip;
    ctlMark.nHighlightRow = 0;
    Base_DrawFacilityIconStrip(&ctlMark);
    Check((u16)BdPix(234, 26) == uChevron && (u16)BdPix(212, 4) == uChevron
              && (u16)BdPix(212, 26) == uChevron,
          "control: Base_DrawFacilityIconStrip with highlight 0 paints its chevron steps at (212+k, 4+k) and "
          "(212+k, 26-k), 23 of them, over the cell blocks (0x62720..0x627C4)");
    ArReset();
    Geoscape_DrawPlacementArm(&g_pcArm, &g_pcCtlEnv, &g_pcGrid, &g_pcStrip);   /* the env says highlight 0 */
    Check((u16)BdPix(234, 26) != uChevron && (u16)BdPix(212, 4) != uChevron
              && (u16)BdPix(212, 26) != uChevron,
          "the arm overwrites that argument with -1 before calling, so not one chevron step appears even "
          "though the env asked for base 0 (0x922B8, the same dead mark case 7 passes)");
    Check(BfaInk(BFA_STRIP_X0, BFA_STRIP_Y0, BFA_STRIP_X1, BFA_STRIP_Y1) > 0,
          "while the strip itself still drew its 6x6 cell blocks -- the mark is the only thing suppressed");

    /* ---- 3. the ten action captions: five centred on 60, five on 180, 18 rows apart ----
     *
     * The captions run at 0x92070..0x92290, BEFORE the two callees -- and BaseFacScreen_DrawNameGrid's
     * 6x6 blank backdrop (0x61CA4..0x61CC8: 36 basebits glyphs at the scroll origin, each cell 32x32,
     * every pixel index 0, so palette slot 3 entry 0 is what lands there) paints over whatever it
     * covers. With the fixture's origin at (6,60) its cells reach y 287, which is exactly why the rows
     * 229, 247 and 265 of BOTH columns vanish from the surface while 283 and 301 survive -- the same
     * order the original has. Measuring the captions needs the backdrop parked above them, so the
     * origin moves to (0,0) for this block: its cells then cover y 36..227 and every caption row is
     * visible again. */
    g_abPcGrid[6 * 2 + 2] = 0;
    g_abPcGrid[PUFO_GRID_FAC_OFF + 6 * 2 + 2] = 0;
    g_abPcGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_TILEB_OFF + 6 * 2 + 2] = 0;
    g_nPcOriginX = 0;
    g_nPcOriginY = 0;
    PcBaseline();
    PnSetText(69, "A");      PnSetText(74, "F");
    PnSetText(70, "BB");     PnSetText(75, "GG");
    PnSetText(71, "CCC");    PnSetText(76, "HHH");
    PnSetText(72, "DDDD");   PnSetText(77, "IIII");
    PnSetText(73, "EEEEE");  PnSetText(78, "JJJJJ");
    PcRun();
    static const i32 g_anPcCapRec[10] = { 69, 70, 71, 72, 73, 74, 75, 76, 77, 78 };
    i32 aMinX[10], aMaxX[10];
    for (i32 i = 0; i < 10; ++i) {
        const i32 nRow = i % 5;                      /* both columns use the same five rows (0x92098 and 0x9223C) */
        const i32 nY = PUFO_PLA_CAP_Y0 + PUFO_PLA_CAP_STEP * nRow;
        const PnDiff d = (nRow == i) ? PnDiffRect(20, nY, 110, nY + PC_CAPDY)
                                     : PnDiffRect(130, nY, 230, nY + PC_CAPDY);
        aMinX[i] = d.nMinX;
        aMaxX[i] = d.nMaxX;
    }
    for (i32 i = 0; i < 10; ++i) {
        const i32 nRow = i % 5;
        const i32 nY = PUFO_PLA_CAP_Y0 + PUFO_PLA_CAP_STEP * nRow;
        const i32 nCentre = (nRow == i) ? PUFO_PLA_CAP_L : PUFO_PLA_CAP_R;
        const char *const psz = &g_abPnText[(size_t)PUFO_TEXT_REC_STRIDE * g_anPcCapRec[i]];
        i32 nRefMin, nRefMax;
        PcRefSpan(nCentre, nY, psz, &nRefMin, &nRefMax);
        char szWhat[192];
        snprintf(szWhat, sizeof(szWhat),
                 "TEXTS[%d] (\"%s\") lands exactly where a centred draw on %d puts it, on row y %d "
                 "(span %d..%d; 0x92070..0x92290 measures, then RSB #%02X)",
                 g_anPcCapRec[i], psz, nCentre, nY, nRefMin, nRefMax, (nRow == i) ? 0x3C : 0xB4);
        Check(aMinX[i] == nRefMin && aMaxX[i] == nRefMax, szWhat);
    }
    for (i32 i = 0; i < 10; ++i) {
        PnSetText(g_anPcCapRec[i], "");
    }
    g_nPcOriginX = BFA_GRID_X0;         /* back to the origin the cursor block's pixel probes below assume */
    g_nPcOriginY = BFA_GRID_Y0 - 36;

    /* ---- 4. the base name and the money line: reached only with bBasePlaceActive CLEAR ---- */
    PcSetName(0, "");
    PcBaseline();                        /* the money number alone, at a fixed (5, 20) */
    Check(BfaInk(PC_MONEY_X0, PC_MONEY_Y0, PC_MONEY_X1, PC_MONEY_Y1) > 0,
          "TEXTS[156] with itoa(nFunds) and \"$\" is drawn at (5,20) (0x923CC / 0x923D0)");
    PnSetText(156, "MONEY:");
    PcRun();
    PnDiff dMoney = PnDiffRect(0, PC_MONEY_Y0, BD_SURF_W - 1, PC_MONEY_Y1);
    CheckEqI32(dMoney.nMinX, PUFO_PLA_MONEY_X,
               "the money line starts at a FIXED x 5 -- the one string in this arm that is not centred");
    PnSetText(156, "");

    PcSetName(0, "ZZZ");
    PcRun();
    PnDiff dName = PnDiffRect(PC_NAME_X0, PC_NAME_Y0, PC_NAME_X1, PC_NAME_Y1);
    Check(dName.nCount > 0 && dName.nMinX == PUFO_PLA_NAME_CENTRE - (Text_MeasureWidth("ZZZ") >> 1),
          "the base name is centred on 120 at y 1 (0x92314 measure, 0x9233C RSB #0x78, 0x92344 draw)");
    PcSetName(0, "");
    g_nPcSelBase = 5;
    PcSetName(5, "QQQQQQ");
    PcRun();
    dName = PnDiffRect(PC_NAME_X0, PC_NAME_Y0, PC_NAME_X1, PC_NAME_Y1);
    Check(dName.nCount > 0 && dName.nMinX == PUFO_PLA_NAME_CENTRE - (Text_MeasureWidth("QQQQQQ") >> 1),
          "and it is apSiteRecs[nSelectedBaseIdx].abName -- base 5's name, at +0x1A of the 64-byte record "
          "(0x9230C ADD R7,R3,LSL#6 / 0x92310 ADD #0x1A)");
    PcSetName(5, "");
    g_nPcSelBase = 0;
    Check((i32)g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_SECOND]
              == (i32)Gfx_PackRgb565Pair(&g_pcColor, 130, 130, 250, 20, 20, 20),
          "the money half repacks (130,130,250) over (20,20,20) before drawing it (0x92348..0x92368)");

    /* ---- 5. the cursor block, reached only with bBasePlaceActive SET ---- */
    g_abPcGrid[6 * 2 + 2] = 0;                       /* the callees draw nothing for the cursor runs */
    g_abPcGrid[PUFO_GRID_FAC_OFF + 6 * 2 + 2] = 0;
    g_abPcGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_TILEB_OFF + 6 * 2 + 2] = 0;

    g_bPcPlaceActive = 1;
    g_nPcTapX = 2;
    g_nPcTapY = 1;
    *PcCell(2, 1) = 0;
    PcRun();
    CheckEqI32(g_bPcPlaceActive, 0,
               "an empty cell under the cursor CLEARS bBasePlaceActive -- 0x92424 BEQ / 0x92844 STRB R11,[R10]: "
               "a draw call changes map state");
    CheckEqI32(BfaInk(70, 128, 102, 196), 0, "and an empty cell draws no cursor box at all");
    CheckEqI32(BfaInk(PC_MONEY_X0, PC_MONEY_Y0, PC_MONEY_X1, PC_MONEY_Y1), 0,
               "while the name/money half is skipped whole (0x922F4 BNE)");
    CheckEqI32(g_nPnBlits, PUFO_PLA_NAV_CTLS + 1, "and the map control is not blitted: its tail is inside the non-empty branch");

    g_bPcPlaceActive = 1;
    *PcCell(2, 1) = FAC_LONG_RANGE_RADAR;            /* 5: below 9, so the one-cell cursor */
    PcBaseline();
    PcRun();
    const u16 uPhase = PC_PACK(0, 10 * (5 + PUFO_PLA_TAP_COLOR_BASE), 0);
    Check((u16)BdPix(70, 128) == uPhase && (u16)BdPix(101, 128) == uPhase && (u16)BdPix(102, 128) == uPhase,
          "cell 5 draws the 32-wide cursor: the top bar x 70..101 at y 128 and the right bar at x 101,102 "
          "(0x925D0 MOV R3,#0x20, 0x9267C ADD #0x1F)");
    Check((u16)BdPix(70, 129) == uPhase && (u16)BdPix(102, 129) == uPhase && (u16)BdPix(103, 129) == 0
              && (u16)BdPix(71, 140) == uPhase && (u16)BdPix(72, 140) == 0,
          "x = origin + 32*tap and y = origin + 32*tap + 36 (0x924D0 ADD #0x24): the top bar is 2 rows, the side "
          "bars 2 columns, and the frame's inside column x 72 stays empty");
    Check((u16)BdPix(70, 159) == uPhase && (u16)BdPix(101, 160) == uPhase && (u16)BdPix(102, 160) == 0
              && (u16)BdPix(70, 161) == 0,
          "the bottom bar lands 31 rows under the top one (0x926BC ADD R2,R2,#0x43, and 0x43 = 0x24 + 0x1F): "
          "y 159..160, 32 wide, closing a 32x32 frame -- the side bars' own 32 rows reach the same line");
    Check((u16)BdPix(85, 145) == 0, "the box is a FRAME: its interior stays empty");
    CheckEqI32(g_nPcTapX, 2, "a cell below 9 leaves the tap alone (tapX)");
    CheckEqI32(g_nPcTapY, 1, "and leaves the tap alone (tapY)");
    g_nPcMapPhase = 3;
    PcRun();
    Check((u16)BdPix(70, 128) == PC_PACK(0, 10 * (3 + PUFO_PLA_TAP_COLOR_BASE), 0)
              && (u16)BdPix(70, 128) != uPhase,
          "the cursor colour is 10 * (nMapPhase + 15): phase 3 gives 180 where phase 5 gave 200 (0x92464/0x92470/0x92478)");
    g_nPcMapPhase = 5;

    /* The two ends of the 9..12 band: just below and just above it, the cursor is the 32-wide one and
     * the tap is left where it was. */
    *PcCell(2, 1) = FAC_PSI_LABORATORY;        /* 8 */
    PcRun();
    Check((u16)BdPix(102, 128) == uPhase && (u16)BdPix(110, 128) == 0
              && (u16)BdPix(70, 128) == uPhase,
          "kind 8 takes the ONE-cell cursor: the top bar reaches x 102 and stops, x 110 is empty "
          "(0x92428 CMP #9 / BLT 0x925AC)");
    Check(g_nPcTapX == 2 && g_nPcTapY == 1, "and kind 8 does not move the tap");
    *PcCell(2, 1) = FAC_MISSILE_DEFENCES;      /* 13 */
    PcRun();
    Check((u16)BdPix(102, 128) == uPhase && (u16)BdPix(110, 128) == 0,
          "kind 13 takes it too -- 9..12 is the whole two-cell band (0x92428/0x925AC)");
    *PcCell(2, 1) = FAC_LONG_RANGE_RADAR;
    const i32 nFlagW = Text_MeasureWidth("FLAGFIVE");
    PcSetFlag(FAC_LONG_RANGE_RADAR, "FLAGFIVE");
    PcRun();
    const PnDiff dLabel = PnDiffRect(0, 16, 30, 30);
    Check(dLabel.nCount > 0 && dLabel.nMinX == PUFO_PLA_FAC_NAME_X,
          "the cell's name is drawn at a FIXED (6,16) from aBaseFacFlags[cell].szName -- no centring at all "
          "(0x92718 MOV R1,#6 / 0x92748 MOV R2,#0x10)");
    CheckEqI32((i32)g_pcMapCtl.x, nFlagW + PUFO_PLA_MAPCTL_X_ADD,
               "pGeoscapeCtlA's x is the truncated label width + 0xA (0x927A4 LSL#16/ASR#16, 0x927B0 ADD #0xA)");
    CheckEqI32((i32)g_pcMapCtl.y, PUFO_PLA_MAPCTL_Y, "and its y is 15 (0x927B8 MOV R3,#0xF / 0x927BC)");
    Check(g_pcMapCtl.pfnClick == PcMapClick, "with Geoscape_OnMapClick_Thunk installed at +0xC first (0x92764)");
    CheckEqI32(g_nPnBlits, PUFO_PLA_NAV_CTLS + 2, "and the map control is the twelfth blit (0x927C8)");
    Check(g_apnPnBlitPad[PUFO_PLA_NAV_CTLS + 1] == &g_pcMapPad, "after the close control, before nothing else");

    /* The same tail with the label empty: the control lands at x 10, so the hint has to move with it --
     * that is what makes the centring claim observable instead of just "there is ink". */
    PcSetFlag(FAC_LONG_RANGE_RADAR, "");
    PnSetText(PUFO_PLA_TEXT_DEMOLISH, "X");
    PcRun();
    const PnDiff dHint = PnDiffRect(20, 16, 68, 30);
    Check(dHint.nCount > 0
              && dHint.nMinX == (i32)g_pcMapCtl.x + ((i32)g_pcMapCtl.uWidth >> 1)
                                 - (Text_MeasureWidth("X") >> 1),
          "TEXTS[1240] \"Демонтаж\" is centred ON the control: its x + uWidth/2, read back after the write, and "
          "it follows the control when the label changes its x (0x927CC..0x92834)");
    Check(dHint.nMinY == dLabel.nMinY,
          "and one row below the control's y 15 -- the same text row the facility label uses (0x92830 ADD #1)");
    PnSetText(PUFO_PLA_TEXT_DEMOLISH, "");

    CheckEqI32((i32)g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_SECOND], (i32)uPairArm,
               "in the cursor path the arm's own (68,244,252) over (4,70,75) pack is the last one (0x926D0..0x926F8) "
               "-- the tail draws its text with it");
    *PcCell(2, 1) = 200;                             /* above every FacilityKind the table has */
    PcRun();
    CheckEqI32(BfaInk(4, 16, 60, 30), 0,
               "a cell byte of 200 draws NO name: the port bounds aBaseFacFlags to its 17 records "
               "(the original indexes 0x186E48 with the byte whatever it says)");
    CheckEqI32((i32)g_pcMapCtl.x, PUFO_PLA_MAPCTL_X_ADD, "and the control still gets x = 0 + 0xA from the empty label");
    *PcCell(2, 1) = 0;

    /* kinds 9..12: the two-cell cursor AND the tap moved first -- a draw that moves the map cursor */
    static const i32 g_anPcBig[4] = { FAC_HYPER_WAVE_DECODER, FAC_MIND_SHIELD, FAC_HANGAR, FAC_GRAVITY_SHIELD };
    for (i32 i = 0; i < 4; ++i) {
        g_bPcPlaceActive = 1;
        g_nPcTapX = 3;
        g_nPcTapY = 3;
        *PcCell(3, 3) = (u8)g_anPcBig[i];
        PcRun();
        const i32 nWantX = 3 - ((g_anPcBig[i] == FAC_MIND_SHIELD || g_anPcBig[i] == FAC_GRAVITY_SHIELD) ? 1 : 0);
        const i32 nWantY = 3 - ((g_anPcBig[i] == FAC_HANGAR || g_anPcBig[i] == FAC_GRAVITY_SHIELD) ? 1 : 0);
        const i32 nX = g_nPcOriginX + PUFO_PLA_TAP_CELL * nWantX;
        const i32 nY = g_nPcOriginY + PUFO_PLA_TAP_CELL * nWantY + PUFO_PLA_TAP_Y0;
        char szWhat[192];
        snprintf(szWhat, sizeof(szWhat),
                 "kind %d: the tap becomes (%d,%d) and the box is drawn from the MOVED tap at (%d,%d) "
                 "(0x92450/0x92458, 0x925A0)", g_anPcBig[i], nWantX, nWantY, nX, nY);
        Check(g_nPcTapX == nWantX && g_nPcTapY == nWantY, szWhat);
        snprintf(szWhat, sizeof(szWhat), "kind %d: the 64-wide box -- the top bar reaches x %d and stops at x %d "
                                         "(0x92480 MOV R3,#0x40, 0x9252C ADD #0x3F)",
                 g_anPcBig[i], nX + 64, nX + 65);
        Check((u16)BdPix(nX + 40, nY) == uPhase && (u16)BdPix(nX + 64, nY) == uPhase
                  && (u16)BdPix(nX + 65, nY) == 0, szWhat);
        snprintf(szWhat, sizeof(szWhat), "kind %d: and its bottom bar at +63, closing a 64x64 frame "
                                         "(0x92564 ADD R2,R2,#0x63, and 0x63 = 0x24 + 0x3F)", g_anPcBig[i]);
        Check((u16)BdPix(nX, nY + 63) == uPhase && (u16)BdPix(nX, nY + 64) == uPhase
                  && (u16)BdPix(nX, nY + 65) == 0, szWhat);
        *PcCell(nWantX, nWantY) = 0;
        if (nWantX != 3 || nWantY != 3) {
            *PcCell(3, 3) = 0;
        }
    }
    Check(g_nPcTapX == 2 && g_nPcTapY == 2,
          "the moved tap survives the run: after kind 12 the arm left nSmapTapX/nSmapTapY at (2,2), the "
          "values it stored at 0x925A4 -- the next screen reader sees the cursor one cell away");
    *PcCell(2, 2) = 0;
    g_bPcPlaceActive = 0;
    g_nPcTapX = 0;
    g_nPcTapY = 0;

    /* ---- 6. the demolish-failure panel, gated on bBasePlaceFail @ 0x1870B8 ---- */
    PcBaseline();                                       /* fail clear, every panel record empty */
    CheckEqI32(BfaInk(100, 100, 200, 150), 0,
               "with bBasePlaceFail clear nothing of the panel is drawn -- no frame, no body, no lines (0x92848)");
    g_bPcPlaceFail = 1;
    PcBaseline();                                       /* the shipped lang.dat state: all six records empty */
    Check(BfaInk(100, 100, 200, 150) > 0,
          "set, Ui_AnimExpandBlit paints the 236x50 panel at (0,100) with its bevelled frame (0x92854..0x92898)");
    Check(PcNotUniform(20, 112, 200, 140) == 0,
          "with every panel record empty the panel is ONE flat fill -- 181x29 interior pixels all equal, so "
          "Ui_AnimExpandBlit alone is what the gate runs and no text row is painted");
    CheckEqI32((i32)g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_SECOND], (i32)uPairFail,
               "the arm still ends with (255,255,255) over (104,104,104) -- 104 is (u8)abFacilityGrid, the "
               "grid-block pointer reused as a colour byte (0x929C0 STR R5,[g2])");

    PnSetText(1241, "AAA");
    PnSetText(1242, "BBBBB");
    PnSetText(1243, "CC");
    PcRun();
    Check(PnDiffRect(0, 107, BD_SURF_W - 1, 107).nCount > 0, "TEXTS[1241] is on the y 104 line (0x928C0..0x9291C)");
    Check(PnDiffRect(0, 121, BD_SURF_W - 1, 121).nCount > 0, "TEXTS[1242] on the y 118 line (0x9291C..0x92970)");
    Check(PnDiffRect(0, 135, BD_SURF_W - 1, 135).nCount > 0, "TEXTS[1243] on the y 132 line (0x92970..0x929B8)");
    CheckEqI32(PnDiffRect(0, 117, BD_SURF_W - 1, 120).nCount, 0,
               "with 14 pixels between the lines: rows 117..120 stay as the panel body drew them (0x92924 ADD #0xE)");
    CheckEqI32(PnDiffRect(0, 131, BD_SURF_W - 1, 134).nCount, 0, "and again at rows 131..134");
    CheckEqI32(PnDiffRect(0, 145, BD_SURF_W - 1, 155).nCount, 0, "and nothing below row 144: the second triple is empty");
    PnSetText(1241, "");
    PnSetText(1242, "");
    PnSetText(1243, "");

    PnSetText(80, "WWW");
    PnSetText(81, "WWW");
    PnSetText(82, "WWW");
    PcRun();
    Check(PnDiffRect(0, 111, BD_SURF_W - 1, 111).nCount > 0
              && PnDiffRect(0, 128, BD_SURF_W - 1, 128).nCount > 0
              && PnDiffRect(0, 145, BD_SURF_W - 1, 145).nCount > 0,
          "records 80, 81, 82 really are read, at y 108, 125, 142 -- 17 apart (0x929DC..0x92ADC): filling them "
          "paints all three rows, so the shipped panel half is empty by content, not by code");
    PnSetText(80, "");
    PnSetText(81, "");
    PnSetText(82, "");
    g_bPcPlaceFail = 0;
}

/* ---- Geoscape_Draw @ 0x8c180's own head (0x8C180..0x8C1B0) and its cases 0/2 radar branch
 *      (0x8C214..0x8C29C) ---------------------------------------------------------------------
 *
 * Every block of the geoscape frame was tested on its own already; the head that routes a screen id
 * to a block was not. Each run below hands Geoscape_Draw one screen id and looks only for the mark
 * the routed block leaves: the chrome's overlay origin, the crosshair's cooldown park, the status
 * panel's two control positions, an arm's rail columns, its control-run blit records, the BaseAt
 * confirm byte, the radar session's signal scroll. The frame end runs for every id, so every id also
 * hands back the present hook's value. */

#define DP_PAD_W       8
#define DP_PAD_H       8
#define DP_PAD_SLOT    5    /* a palette slot no other fixture uses */
#define DP_PAD_INK 0x2A00u
#define DP_RADAR_X    40    /* the radar arm's strip pad, clear of every other fixture's ink */
#define DP_RADAR_Y   200
#define DP_SENTINEL 0x7F7F  /* "this global was not touched" */

static PadImage   g_dpStripPad;
static u8         g_abDpStrip[4 + DP_PAD_W * DP_PAD_H];
static UiControl  g_dpRootCtl[13];
static PadImage   g_dpRootPad[13];
static UiControl *g_apDpRootCtl[13];
static u8         g_bDpRadar;
static GeoscapeRadarArmEnv g_dpRadar;
static GeoscapeFrameEndEnv g_dpFrame;
static GeoscapeDrawEnv     g_dpEnv;
static u8         g_bDpFnOpen;                /* bFundScreenOpen @ 0x1595EC, FundingScreen_Draw's gate */
static i32        g_nDpScreen, g_nDpPresents, g_nDpMode, g_nDpPhase;

static i32 DpPresent(void *pUser)
{
    (void)pUser;
    ++g_nDpPresents;
    return 3;
}

/* The radar arm's own fixtures: one strip pad whose rectangle the arm also fills, and the thirteen
 * root widgets the arm blits. */
static void DpRadarFixture(void)
{
    memset(g_abDpStrip, 0, sizeof(g_abDpStrip));
    g_abDpStrip[0] = (u8)DP_PAD_W;
    g_abDpStrip[2] = (u8)DP_PAD_H;
    for (i32 i = 0; i < DP_PAD_W * DP_PAD_H; ++i) {
        g_abDpStrip[4 + i] = 21;
    }
    memset(&g_dpStripPad, 0, sizeof(g_dpStripPad));
    g_dpStripPad.pData    = g_abDpStrip;
    g_dpStripPad.uWidth   = (u16)DP_PAD_W;
    g_dpStripPad.uHeight  = (u16)DP_PAD_H;
    g_dpStripPad.nOriginX = DP_RADAR_X;
    g_dpStripPad.nOriginY = DP_RADAR_Y;
    g_dpStripPad.nMode    = (u8)DP_PAD_SLOT;
    g_aPalette[DP_PAD_SLOT][21] = DP_PAD_INK;

    for (i32 i = 0; i < 13; ++i) {
        memset(&g_dpRootCtl[i], 0, sizeof(g_dpRootCtl[i]));
        memset(&g_dpRootPad[i], 0, sizeof(g_dpRootPad[i]));
        g_dpRootPad[i].uWidth  = 2;
        g_dpRootPad[i].uHeight = 2;
        g_dpRootCtl[i].apPads[0] = &g_dpRootPad[i];
        g_dpRootCtl[i].bEnabled  = 1;
        g_dpRootCtl[i].x = (i16)(10 + 10 * i);   /* the blits are recorded, not painted */
        g_dpRootCtl[i].y = 310;
        g_apDpRootCtl[i] = &g_dpRootCtl[i];
    }

    memset(&g_dpRadar, 0, sizeof(g_dpRadar));
    g_dpRadar.pbRadarActive = &g_bDpRadar;
    g_dpRadar.pStripPad     = &g_dpStripPad;
    g_dpRadar.apRootCtl     = g_apDpRootCtl;
    g_dpRadar.pfnCtlBlit    = PnBlit;
    g_dpRadar.pfnCtlBlitEx  = PnBlitEx;
    g_dpRadar.pRadarEnv     = &g_updEnv;         /* the session the radar suite already built */
}

/* The chrome's and the crosshair's starting state, re-applied per run. Every env pointer is written
 * again because the chrome and crosshair suites end on their "a missing part draws nothing" guards
 * (0x8C498 check sets g_mvEnv.pMapPad NULL at 4194, the crosshair's sets pChromePad NULL at 4239), and
 * nScreenId is what decides the Y half of the wrap (0x8C3F0). */
static void DpMvState(i32 nScrollX, i32 nScrollY, i32 bPending)
{
    memset(&g_mvView, 0, sizeof(g_mvView));
    g_mvView.nScrollX = nScrollX;
    g_mvView.nScrollY = nScrollY;
    g_mvView.nMapW    = MV_MAP_SIZE;
    g_mvView.nMapH    = MV_MAP_SIZE;
    memset(&g_mvTime, 0, sizeof(g_mvTime));
    g_mvTime.nHour   = 3;
    g_mvTime.nMinute = 30;
    g_nMvOverlay        = DP_SENTINEL;
    g_mvMapPad.nOriginX   = -1;
    g_mvMapPad.nOriginY   = -1;
    g_mvLayerPad.nOriginX = -1;
    g_mvLayerPad.nOriginY = -1;
    g_aPalette[MV_MAP_SLOT][1] = MV_INK;
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][1] = 0xFFFFu;
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][2] = 0x001Fu;
    g_aPalette[PUFO_RADAR_MAP_PAL_SLOT][3] = 0xF800u;
    g_mvEnv.pView            = &g_mvView;
    g_mvEnv.pMapPad          = &g_mvMapPad;
    g_mvEnv.pLayerPad        = &g_mvLayerPad;
    g_mvEnv.pWorldBits       = g_abMvWorld;
    g_mvEnv.pMapPal          = g_aPalette[PUFO_RADAR_MAP_PAL_SLOT];
    g_mvEnv.pTime            = &g_mvTime;
    g_mvEnv.pnOverlayOriginX = &g_nMvOverlay;
    g_mvChromePad.uHeight    = 20;                  /* the crosshair's vertical line is 320 - 20 tall */
    g_mvXh.pChromePad        = &g_mvChromePad;
    g_mvXh.nCrosshairX       = 100;
    g_mvXh.nCrosshairY       = 50;
    g_mvXh.pbPending         = &g_bMvPending;
    g_mvXh.pnCooldownR       = &g_nMvCooldown;
    g_bMvPending  = (u8)bPending;
    g_nMvCooldown = 0;
}

/* Non-zero pixels in a rectangle of the frame buffer -- the "was anything drawn here" measure. The
 * suite's PnDiffRect compares against a captured text baseline, which is not what a dispatch check
 * needs: here the surface is cleared before every run. */
static i32 DpInkRect(i32 nX0, i32 nY0, i32 nX1, i32 nY1)
{
    i32 n = 0;
    for (i32 y = nY0; y <= nY1; ++y) {
        for (i32 x = nX0; x <= nX1; ++x) {
            if ((u16)BdPix(x, y) != 0) { ++n; }
        }
    }
    return n;
}

/* What SoftSurf_BlitSrcRect @ 0xa4450 leaves behind: every 5-6-5 field of the destination blended with
 * the colour's field at nKey, (dst * (255 - nKey) + colour * nKey) >> 8, the colour being
 * (nIdx >> 3) << 11 | (nColorMid >> 2) << 5 | (nColorLow >> 3) (bugs.tsv 0xA4590, 0xA460C). */
static u16 DpBlend(u16 nDst, i32 nIdx, i32 nMid, i32 nLow, i32 nKey)
{
    const i32 nInv = 255 - nKey;
    /* The two scalings are shifted SEPARATELY and then added (0xA460C / 0xA4590): summing before the
     * shift gives a different rounding, which is what the first run of this test caught. */
    const i32 nDstR = ((i32)nDst >> 11) & 0x1F;
    const i32 nDstG = ((i32)nDst >> 5) & 0x3F;
    const i32 nDstB = (i32)nDst & 0x1F;
    const i32 nR = ((nDstR * nInv) >> 8) + (((nIdx >> 3) * nKey) >> 8);
    const i32 nG = ((nDstG * nInv) >> 8) + (((nMid >> 2) * nKey) >> 8);
    const i32 nB = ((nDstB * nInv) >> 8) + (((nLow >> 3) * nKey) >> 8);
    return (u16)((nR << 11) + (nG << 5) + nB);
}

static void DpCtlSentinels(void)
{
    g_pnCtlSfx.x = -1;  g_pnCtlSfx.y  = -1;
    g_pnCtlOpen.x = -1; g_pnCtlOpen.y = -1;
}

static i32 DpRun(i32 nScreenId)
{
    g_nDpScreen = nScreenId;
    g_mvEnv.nScreenId = nScreenId;   /* the chrome reads the same id (0x8C3F0) */
    ArReset();
    g_nDpPresents = 0;
    g_nDpPhase = -1;
    return Geoscape_Draw(&g_dpEnv);
}

static void TestGeoscapeDispatch(void)
{
    printf("\nGeoscape_Draw @ 0x8c180's dispatcher head and its cases 0/2 radar branch\n");
    PnFixture(' ', 0, 0, 0);                     /* line 0's flag ' ' is the panel arm we look for */
    if (!g_bPnReady) {
        Check(false, "the font is loaded");
        return;
    }
    g_pMainSurface = &g_bdSurf;
    DpRadarFixture();

    memset(&g_dpFrame, 0, sizeof(g_dpFrame));
    g_dpFrame.pnListMode           = &g_nDpMode;
    g_dpFrame.pnActionPhaseSave112 = &g_nDpPhase;
    g_dpFrame.pModalAnim           = &g_feModal;
    g_dpFrame.pModalLayout         = &g_feLayout;
    g_dpFrame.pTextRecs            = (const u8 *)g_abPnText;
    g_dpFrame.pfnPresent           = DpPresent;

    memset(&g_dpEnv, 0, sizeof(g_dpEnv));
    g_dpEnv.pnScreenId       = &g_nDpScreen;
    g_dpEnv.pRadarArm        = &g_dpRadar;
    g_dpEnv.pMapView         = &g_mvEnv;
    g_dpEnv.pCrosshair       = &g_mvXh;
    g_dpEnv.pStatusPanel     = &g_pnEnv;
    g_dpEnv.pPlacementArm    = &g_pcArm;
    g_dpEnv.pPlacementCtl    = &g_pcCtlEnv;
    g_dpEnv.pPlacementGrid   = &g_pcGrid;
    g_dpEnv.pPlacementStrip  = &g_pcStrip;
    g_dpEnv.pBaseInfoArm     = &g_biArm;
    g_dpEnv.pBaseInfoCtl     = &g_biCtlEnv;
    g_dpEnv.pBaseInfoDraw    = &g_biInfo;
    g_dpEnv.pSoldierDraw     = &g_solEnv;
    g_dpEnv.pSoldierArm      = &g_solArm;
    g_dpEnv.pSoldierPanelCtl = &g_solCtlEnv;
    g_dpEnv.pSoldierFindCtl  = &g_solCtlEnv;
    g_dpEnv.pPlaneArm        = &g_plArm;
    g_dpEnv.pBaseFacArm      = &g_bfArm;
    g_dpEnv.pBaseFacGrid     = &g_bfGrid;
    g_dpEnv.pBaseFacStrip    = &g_bfStrip;
    g_dpEnv.pBaseFacDialog   = &g_bfDialog;
    g_dpEnv.pResearchDraw    = &g_arResearch;
    g_dpEnv.pResearchCtl     = &g_arArm;
    g_dpEnv.pManufPad        = &g_arArmPad;
    g_dpEnv.pManufDraw       = &g_arManuf;
    g_dpEnv.pManufCtl        = &g_arArm;
    g_dpEnv.pTransferPad     = &g_arArmPad;
    g_dpEnv.pTransferDraw    = &g_arTransfer;
    g_dpEnv.pTransferCtl     = &g_arArm;
    g_dpEnv.pBuySellTrack    = &g_arTrack;
    g_dpEnv.pBuySellPad      = &g_arArmPad;
    g_dpEnv.pBuySellDraw     = &g_arBuySell;
    g_dpEnv.pBuySellCtl      = &g_arArm;
    g_dpEnv.pFundEnv         = &g_fnDraw;
    g_dpEnv.pFundingArm      = &g_fnArm;
    g_dpEnv.pUfopaediaDraw   = &g_ufoBaseInfo;
    g_dpEnv.pUfopaediaGates  = &g_ufoGate;
    g_dpEnv.pUfopaediaCtl    = &g_ufoArm;
    g_dpEnv.pPsiDraw         = &g_psEnv;
    g_dpEnv.pPsiCtl          = &g_psArm;
    g_dpEnv.pOptionsScreen   = &g_dsEnv;
    g_dpEnv.pBaseAtEnv       = &g_feBaseAt;
    g_dpEnv.pFrameEnd        = &g_dpFrame;
    g_nDpMode = 0;

    /* ---- 1. an id with no arm: only the frame end runs ---- */
    DpMvState(MV_SCROLL_X, MV_SCROLL_Y, 1);
    DpCtlSentinels();
    const i32 nRet = DpRun(13);
    CheckEqI32(nRet, 3, "id 13 hands back the present hook's value (0x94538 MOV R0,R4 / 0x9453C)");
    CheckEqI32(g_nDpPresents, 1, "and it presents exactly once -- Render_Frame's geoscape branch skips its own");
    CheckEqI32(g_nMvOverlay, DP_SENTINEL, "the chrome never ran: nSmapOverlayOriginX keeps its sentinel (0x8C2B0)");
    CheckEqI32(g_pnCtlSfx.y, -1, "the status panel never ran: its control positions keep their sentinels (0x8C760)");
    CheckEqI32(g_nPnBlits, 0, "and no control pad was offered");
    CheckEqI32(DpInkRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1), 0, "the default frame is bare");
    PnSaveFrame();
    CheckEqI32(DpRun(14), 3, "id 14 has its own table word (0x8C1EC) and the same target");
    CheckEqI32(PnFrameDiff(), 0, "and its frame is identical: both words land on 0x944B8");
    CheckEqI32(DpRun(99), 3, "an id above the CMP #0x14 bound takes the default word at 0x8C1B4");
    CheckEqI32(PnFrameDiff(), 0, "again the same bare frame");
    CheckEqI32(g_nDpPhase, -1, "mode 0 writes no Map.anActionPhaseSave0[112]");

    /* ---- 2. id 20 is the only id whose fall-through runs BaseAtScreen_Draw ---- */
    g_bFeShown = 0;
    g_bFeActive = 1;
    g_nFeLimit = 0;
    CheckEqI32(DpRun(20), 3, "id 20 reaches the frame end too");
    CheckEqI32(g_bFeShown, 1, "and only id 20 runs BaseAtScreen_Draw, by falling into 0x944B4");
    Check(DpInkRect(0, 286, BD_SURF_W - 1, 305) > 0, "with its own confirm row drawn");
    g_bFeShown = 0;
    (void)DpRun(19);
    CheckEqI32(g_bFeShown, 0, "id 19 branches to 0x944B8 and never reaches 0x944B4");

    /* ---- 3. cases 0 and 2 with the radar screen closed: chrome, crosshair, panel ---- */
    g_bDpRadar = 0;
    DpMvState(MV_SCROLL_X, MV_SCROLL_Y, 1);
    DpCtlSentinels();
    (void)DpRun(0);
    CheckEqI32(g_nMvOverlay, -35, "id 0 runs the chrome: 240 / -24 is -10, so hour 3 and minute 30 give -35 (0x8C2F4)");
    Check(g_mvMapPad.nOriginX != -1 && g_mvMapPad.nOriginY != -1,
          "the map pad's origins were written, which is what the blits do (0x8C498)");
    Check((u16)BdPix(103, 40) == (u16)MV_XHAIR_INK && (u16)BdPix(120, 53) == (u16)MV_XHAIR_INK,
          "the crosshair runs AFTER the chrome: its 1-wide vertical line at x 103 and its 240-wide "
          "horizontal line at y 53 both carry the crosshair's own colour, at rows above the panel the "
          "panel tail does not cover (0x8C6CC..0x8C72C)");
    CheckEqI32(g_nMvCooldown, 0, "with the pending flag set the cooldown is left alone (0x8C69C)");
    CheckEqI32(g_pnCtlSfx.y, PUFO_STATUS_CTL_Y_TALL, "the panel's ' ' arm set both control rows (0x8D2B8)");
    CheckEqI32(g_pnCtlOpen.x, PUFO_STATUS_CTL_X_OPEN, "including the right control's x (0x8D2E4)");
    Check(DpInkRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1) > 0, "and the map is painted");

    /* The crosshair's other half, which the original reaches by falling through from 0x8C75C. */
    DpMvState(MV_SCROLL_X, MV_SCROLL_Y, 0);
    DpCtlSentinels();
    (void)DpRun(0);
    CheckEqI32(g_bMvPending, 0, "a clear pending flag stays clear (0x8C750)");
    CheckEqI32(g_nMvCooldown, PUFO_RADAR_COOLDOWN_READY,
               "and the cooldown is parked at 0xFF, the value the radar arms read back (0x8C758)");
    Check((u16)BdPix(103, 40) != (u16)MV_XHAIR_INK && (u16)BdPix(120, 53) != (u16)MV_XHAIR_INK,
          "and on that path neither line position carries the crosshair's colour -- it parked instead of drawing");
    CheckEqI32(g_pnCtlSfx.y, PUFO_STATUS_CTL_Y_TALL,
               "while the status panel still runs -- both the drawn path (0x8C730 B) and this fall-through "
               "land on 0x8C760");

    /* The wrap half: nScrollX for both ids, nScrollY only for id 2. Out-of-band values, so the wrap
     * itself is the mark (200 + (-70) is 130 > 0, and 200 + (-110) is 90 > 0). */
    DpMvState(200, 200, 0);
    (void)DpRun(0);
    CheckEqI32(g_mvView.nScrollX, 100, "id 0 takes one map width off nScrollX (0x8C40C..0x8C414)");
    CheckEqI32(g_mvView.nScrollY, 200, "and 0x8C3F0's BNE leaves nScrollY exactly where it was");
    DpMvState(200, 200, 0);
    (void)DpRun(2);
    CheckEqI32(g_mvView.nScrollX, 100, "id 2 wraps nScrollX the same way");
    CheckEqI32(g_mvView.nScrollY, 100,
               "and wraps nScrollY too -- the only difference the two ids make in this arm (0x8C478)");
    DpMvState(-40, 10, 0);
    (void)DpRun(0);
    CheckEqI32(g_mvView.nScrollX, 60, "-40 + halfW is -110, under -nMapW, so one map width is ADDED (0x8C3DC)");
    CheckEqI32(g_mvView.nScrollY, 10, "id 0's Y half is skipped whatever the value (0x8C3F0)");

    /* ---- 4. cases 0 and 2 with the radar screen open: the strip arm, no map at all ---- */
    g_bDpRadar = 1;
    DpMvState(MV_SCROLL_X, MV_SCROLL_Y, 1);
    DpCtlSentinels();
    UpdFixture(0);
    UpdNoFire();
    g_nRadarScan = 0;
    g_aPalette[DP_PAD_SLOT][21] = DP_PAD_INK;   /* re-set: the radar suite has its own palette work */
    (void)DpRun(0);
    Check((u16)BdPix(DP_RADAR_X, DP_RADAR_Y) != (u16)DP_PAD_INK,
          "the strip pad's own ink is GONE at its top-left pixel: 0x8C290's rect covers exactly the "
          "pad's x/y/uWidth/uHeight (LDR R0,[R0] / LDRH R3,[R0,#0xA]) and ran after the pad blit");
    CheckEqI32((u16)BdPix(DP_RADAR_X, DP_RADAR_Y),
               (u16)DpBlend((u16)DP_PAD_INK, PUFO_GEO_RADAR_IDX, PUFO_GEO_RADAR_MID, PUFO_GEO_RADAR_LOW,
                            PUFO_GEO_RADAR_KEY),
               "and what survives is the pad's colour blended with index 60 / mid 120 / low 130 at key 150 "
               "-- the two operands, in this order (0x8C22C, then 0x8C264..0x8C290)");
    CheckEqI32(g_nPnBlits, PUFO_GEO_RADAR_CTLS,
               "thirteen root widgets blitted, the counter run 0..0xD out of the block 0x8C238 reloads (0x8C240)");
    Check(g_apnPnBlitPad[0] == &g_dpRootPad[0] && g_apnPnBlitPad[12] == &g_dpRootPad[12],
          "in block order: 0x1864F8 + 4*i, first widget then thirteenth");
    CheckEqI32(g_nRadarScan, 10, "RadarScreen_Update ran at 0x8C298: the signal-bar scroll advanced by 10 (0x61084)");
    CheckEqI32(g_nMvOverlay, DP_SENTINEL, "the radar branch draws NO map: the chrome is skipped whole (0x8C220 / 0x8C29C)");
    CheckEqI32(g_pnCtlSfx.y, -1, "and neither is the status panel");
    CheckEqI32(g_mvMapPad.nOriginX, -1, "not even the map pad's origins");
    g_bDpRadar = 0;
    UpdFixture(0);
    UpdNoFire();
    g_nRadarScan = 0;
    DpMvState(MV_SCROLL_X, MV_SCROLL_Y, 1);
    DpCtlSentinels();
    (void)DpRun(0);
    CheckEqI32(g_nRadarScan, 0, "with bRadarScreenActive clear the arm is not entered and the scroll does not move (0x8C218)");
    bool bRootPad = false;
    for (i32 i = 0; i < g_nPnBlits; ++i) {
        for (i32 k = 0; k < PUFO_GEO_RADAR_CTLS; ++k) {
            if (g_apnPnBlitPad[i] == &g_dpRootPad[k]) { bRootPad = true; }
        }
    }
    Check(!bRootPad, "and none of the thirteen root widgets was blitted");
    Check(g_nMvOverlay != DP_SENTINEL, "while the map arm runs instead -- the gate is the branch, not the id");
    g_bDpRadar = 1;

    /* ---- 5. ids 1, 3 and 7: three rail frames, three different rail columns ---- */
    PcRun();                                      /* the arm direct, as the reference frame */
    PnSaveFrame();
    (void)DpRun(1);
    CheckEqI32(PnFrameDiff(), 0, "id 1 reaches Geoscape_DrawPlacementArm: the frame is the arm's own");
    Check((u16)BdPix(210, 8) != 0, "id 1's third rail at x 210 is the mark no other arm paints (0x91EA0 MOV R1,#0xD2)");

    ArReset();
    (void)DpRun(7);
    Check((u16)BdPix(2, 8) != 0, "id 7's side rails are at x 2 and 237 (0x92B08)");
    Check((u16)BdPix(210, 8) == 0, "and case 7 has no x 210 rail -- the placement arm's third rail is its own");
    CheckEqI32(g_nGlyphPalSlotBase, 3, "case 7 forces Map_nGlyphPalBase to 3 and never restores it (0x92D20)");

    ArReset();
    (void)DpRun(3);
    Check((u16)BdPix(5, 8) != 0 && (u16)BdPix(2, 8) == 0,
          "id 3's rails are inset by three: x 5, not x 2 (0x93F48 MOV R1,#5)");

    /* ---- 6. the short arms: 8, 9, 10, 11, 12 ---- */
    ArReset();
    (void)DpRun(8);
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_2, "id 8 blits its two research controls (0x94120..0x94150)");
    Check((u16)BdPix(AR_ARM_X, AR_ARM_Y) == 0, "and blits no arm pad of its own -- the callee draws the frame (0x9411C)");

    ArReset();
    Geoscape_DrawManufArm(&g_arArmPad, &g_arManuf, &g_arArm);   /* the arm direct, as the reference frame */
    PnSaveFrame();
    (void)DpRun(9);
    CheckEqI32(PnFrameDiff(), 0, "id 9 reaches Geoscape_DrawManufArm: its pad and the manuf frame together");
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_2 + 1,
               "its two controls, then ManufScreen_Draw's own reset control -- the CALLEE blits too, "
               "so the arm's count is not the frame's count (0x94174..0x941A0)");
    Check(g_apnPnBlitPad[0] == &g_arCtlPad[0] && g_apnPnBlitPad[2] == &g_arFramePad,
          "in that order: the arm's own controls, the callee's reset control last (0x94174 / 0x941A0)");

    ArReset();
    (void)DpRun(10);
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_4, "id 10 has four controls (0x93E8C CMP #4)");

    ArReset();
    (void)DpRun(11);
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_2 + 1,
               "id 11: two from g_aBuySellCtl (0x93DE0) plus the buy/sell frame's unconditional close control");
    PnSaveFrame();
    (void)DpRun(12);
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_2 + 1, "id 12 the same three");
    CheckEqI32(PnFrameDiff(), 0,
               "and screens 11 and 12 really do draw the same frame: the prefixes are identical and they share "
               "the one BuySellScreen_Draw call at 0x93E10");

    /* ---- 7. ids 15 and 18: a callee call and nothing else ---- */
    g_bDpFnOpen = 1;                /* 0x5B210: FundingScreen_Draw draws NOTHING while bFundScreenOpen is 0 */
    g_fnDraw.pbOpen = &g_bDpFnOpen;
    ArReset();
    (void)DpRun(15);
    Check(DpInkRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1) > 0,
          "id 15 runs FundingScreen_Draw, with its open gate satisfied: pad and rails on the surface (0x94308)");
    CheckEqI32(g_nPnBlits, 1, "and the callee's close control is the only control blit -- the arm offers none (0x94304)");
    g_bDpFnOpen = 0;
    PnSaveFrame();
    ArReset();
    FnGateRun(g_nFnFunds, NULL);
    Check(PnFrameDiff() > 0, "while id 19's arm draws a DIFFERENT frame -- the two funding ids are not aliases");

    ArReset();
    DsFixture();
    (void)DpRun(18);
    Check(DpInkRect(0, 0, BD_SURF_W - 1, BD_SURF_H - 1) > 0, "id 18 runs Geoscape_DrawScreen (0x942F8)");

    /* ---- 8. the arms that position the shared close control: 4 and 5 ---- */
    g_bSolTransfer = 0;
    ArReset();
    (void)DpRun(4);
    CheckEqI32(g_solArm.pCloseCtl->x, PUFO_SOLARM_CLOSE_X, "id 4 parks g_pUiCloseCtl at (224,1) before drawing (0x92DB0)");
    CheckEqI32(g_solArm.pCloseCtl->y, PUFO_SOLARM_CLOSE_Y, "including the y (0x92DC8)");
    CheckEqI32(g_nPnBlits, PUFO_SOLARM_CTLS_2 + 1, "the two panel controls and then the close control (0x92DE0 / 0x92E04)");
    Check((u16)BdPix(2, 8) == 0, "and the soldier screen draws no rails at all");

    ArReset();
    (void)DpRun(5);
    CheckEqI32(g_solArm.pCloseCtl->x, PUFO_SOLARM_CLOSE_X, "id 5 parks it the same way while no transfer is armed");
    CheckEqI32(g_nPnBlits, PUFO_SOLARM_CTLS_3 + 1, "and blits its three find controls plus the close control (0x93070)");

    /* ---- 9. ids 6, 16, 17 ---- */
    PlRun();
    PnSaveFrame();
    (void)DpRun(6);
    CheckEqI32(PnFrameDiff(), 0, "id 6 reaches Geoscape_DrawPlaneArm: the hangar frame is the arm's own");

    g_bUfoOpen = 1;
    g_bUfoDirty = 0;
    ArReset();
    (void)DpRun(16);
    CheckEqI32(g_nPnBlits, 1 + (PUFO_GEOARM_CTLS_6 - PUFO_GEOARM_UFO_CAT_FIRST),
               "an open ufopaedia with no entry drawn blits slots 4 and 5 of its own block (0x941DC) plus the "
               "callee's close control (0x9B7C0)");

    g_bPsOverlay = 0;
    g_bPsOpen = 1;
    ArReset();
    (void)DpRun(17);
    CheckEqI32(g_nPnBlits, PUFO_GEOARM_CTLS_2 + 2,
               "id 17: its two scroll controls (0x942C0) plus the psi frame's own confirm and apply buttons, "
               "which the frame blits even with no scroll controls");

    CheckEqI32(g_nDpPresents, 1, "and the frame end ran for this id as it does for every id");
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
    TestResolveInterceptUI();
    TestSmapPointerUp();
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
    TestManufCountIsTheSharedByte();
    TestGeoscapeMapView();
    TestGeoscapeCrosshair();
    TestRadarBackdrop();
    TestRadarFuelGauge();
    TestRadarRangeBar();
    TestRadarSession();
    TestRadarDetect();
    TestRadarArmament();
    TestRadarOverlay();
    TestRadarUpdate();
    TestDrawStrategIcons();
    TestDrawClockPads();
    TestDrawScreen();
    TestSelectMapTarget();
    TestPulseUpdate();
    TestGlobeHover();
    TestStatusPanelArms();
    TestStatusPanelResearch();
    TestStatusPanelDefault();
    TestStatusPanelActionFail();
    TestStatusPanelContact();
    TestStatusPanelOrder();
    TestStatusPanelDetail();
    TestStatusPanelPause();
    TestStatusPanelCity();
    TestStatusPanelInterceptList();
    TestStatusPanelTargetPanel();
    TestStatusPanelMissionSite();
    TestStatusPanelTravel();
    TestStatusPanelDescentPanel();
    TestStatusPanelDescentTarget();
    TestStatusPanelDescentPanelAlt();
    TestStatusPanelAlienBase();
    TestStatusPanelChainTail();
    TestGeoscapeFrameEnd();
    TestGeoscapeScreenArms();
    TestGeoscapeUfoPsiArms();
    TestGeoscapeSoldierArms();
    TestGeoscapeBaseFacArm();
    TestGeoscapeBaseInfoArm();
    TestGeoscapeFundingArm();
    TestGeoscapePlaneArm();
    TestGeoscapePlacementArm();
    TestGeoscapeDispatch();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
