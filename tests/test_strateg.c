/* test_strateg.c - StrategList_UpdateMotion @ 0x80fd8, StrategList_UpdateDetect @ 0x81948,
 * Strateg_CalcDetect @ 0x81378, Ship_ClearExpiredIntercepts @ 0x8128c, CountActive @ 0x81a2c,
 * FindByShipId @ 0x5a9b4.
 *
 * The pinned oddities: the proximity distance is sqrt(dx^2 + dy^2 + thresh)
 * with the threshold INSIDE the root; the wrapped base distance is
 * min(|bx-ux+800|, |ux-bx+800|, |ux-bx|); every site state requires the
 * proximity timer to be idle; states 11/12 set bHyperDetect.
 */
#include "game/pufo_strateg.h"
#include "game/pufo_geoscape.h" /* PUFO_FAC_BASE_COUNT, the base array the range gate bounds by */
#include "game/pufo_baseui.h"  /* flag-27 Smap_ResolveInterceptUI */
#include "game/pufo_unit.h"     /* Soldier, the crew Craft_CountSoldiersAboard counts */
#include "game/pufo_ship.h"     /* real arrival -> loot handoff */
#include "ui/pufo_linelist.h"   /* UiLineList, the message list the event pass posts to */
#include "ui/pufo_panel.h"      /* UiPanel, the status panel it captions */
#include "ui/pufo_control.h"    /* the six speed controls GameTime_Set5Sec updates */
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

static StrategItem g_aItems[8];
static StrategItem *g_apItems[8];
static Ship g_aShips[4];
static Ship *g_apShips[4];
static u8 g_aSiteState[4];
static i32 g_aRadarPct[4]; /* unk_162008 is a dword per base */
static GameTime g_Time; /* stands for the apLines[101..115] block */
static BaseRec g_aBases[PUFO_FAC_BASE_COUNT];
static BaseRec *g_apBases[PUFO_FAC_BASE_COUNT];

static i32 g_nMsgCount, g_nMsgId, g_nMsgArg3;

static void Hook_Msg(i32 nMsg, i32 nArg3, i32 nArg4, void *pUser)
{
    (void)nArg4; (void)pUser;
    ++g_nMsgCount;
    g_nMsgId = nMsg;
    g_nMsgArg3 = nArg3;
}

static i32 Hook_FindBase(i32 nSiteId, void *pUser)
{
    (void)pUser;
    return nSiteId - 7; /* site id 7 -> base 0 in the fixtures */
}

static void SetupWorld(GeoscapeWorld *pWorld, StrategList *pList, i32 nItems,
                       i32 nShips)
{
    for (i32 i = 0; i < nItems; ++i) {
        memset(&g_aItems[i], 0, sizeof(g_aItems[i]));
        g_aItems[i].nSiteId = STRATEG_ID_UNASSIGNED;
        g_aItems[i].nDisplayId = STRATEG_ID_UNASSIGNED;
        g_apItems[i] = &g_aItems[i];
    }
    for (i32 i = 0; i < nShips; ++i) {
        memset(&g_aShips[i], 0, sizeof(g_aShips[i]));
        g_apShips[i] = &g_aShips[i];
    }
    memset(g_aSiteState, 0, sizeof(g_aSiteState));
    memset(g_aRadarPct, 0, sizeof(g_aRadarPct));

    pList->apItems = g_apItems;
    pList->nCount = nItems;
    pList->nNextDisplayId = 0;
    pList->nNextSiteId = 0;

    pWorld->apCraft = g_apShips;
    pWorld->nShipCount = (u8)nShips;
    pWorld->pbShipCount = NULL;
    pWorld->apSiteState = g_aSiteState;
    pWorld->apRadarPct = g_aRadarPct;
    pWorld->nTickSeconds = 60;
    pWorld->nRadarBoostOff = 1; /* no +7 unless the test asks for it */
    pWorld->nMinuteTicks = 0;
    memset(&g_Time, 0, sizeof(g_Time));
    pWorld->pTime = &g_Time;
}

static void TestCountAndFind(void)
{
    printf("CountActive / FindByShipId\n");
    StrategList list;
    GeoscapeWorld world;
    SetupWorld(&world, &list, 3, 1);

    g_aItems[0].nType = 1; g_aItems[0].bActive = 1;
    g_aItems[1].nType = 1; g_aItems[1].bActive = 0; g_aItems[1].nDisplayId = 42;
    g_aItems[2].nType = 0; g_aItems[2].bActive = 0;
    g_aShips[0].bId = 42;

    CheckEqI32(StrategList_CountActive(&list), 1, "counts type1 active1 only");
    CheckEqI32(StrategList_FindByShipId(&list, &world, 0), 1,
               "finds type1 active0 entry with matching display id");
    g_aShips[0].bId = 43;
    CheckEqI32(StrategList_FindByShipId(&list, &world, 0), -1, "no match -> -1");
}

static void TestFindIdxForShip(void)
{
    printf("StrategList_FindIdxForShip\n");
    StrategList list;
    GeoscapeWorld world;

    SetupWorld(&world, &list, 3, 1);
    memset(g_aItems, 0, sizeof(g_aItems));
    g_aItems[0].nType = 5; g_aItems[0].bActive = 1; g_aItems[0].nDisplayId = 7;
    g_aItems[1].nType = 1; g_aItems[1].bActive = 1; g_aItems[1].nDisplayId = 9;
    g_aShips[0].bId = 7;

    /* No type gate and no active gate: item 0 is type 5 and active, and still matches. */
    CheckEqI32(StrategList_FindIdxForShip(&list, &world, 0), 0,
               "matches with no type or active gate");
    CheckEqI32(StrategList_FindByShipId(&list, &world, 0), -1,
               "the other lookup refuses it, so the two are not the same function");

    g_aShips[0].bId = 9;
    CheckEqI32(StrategList_FindIdxForShip(&list, &world, 0), 1,
               "an active type-1 item matches too");

    g_aShips[0].bId = 11;
    CheckEqI32(StrategList_FindIdxForShip(&list, &world, 0), -1, "no match -> -1");

    list.nCount = 0;
    CheckEqI32(StrategList_FindIdxForShip(&list, &world, 0), -1, "empty list -> -1");

    SetupWorld(&world, &list, 3, 1);
    CheckEqI32(StrategList_FindIdxForShip(&list, &world, 99), -1,
               "out-of-range ship index is refused, where the original would fault");
}

static void TestAddItem(void)
{
    printf("StrategList_AddItem @ 0x826e8\n");
    PtrArray list;
    i32 seed = 0;
    StrategItem *p;

    PtrArray_ctor(&list);

    p = StrategList_AddItem(&list, &seed, 0, 1, 0, 2, 300, 400, 42, 1);
    Check(p != NULL, "allocates and appends");
    CheckEqI32(list.nCount, 1, "list grew by one");
    CheckEqI32(seed, 1, "the seed counter is incremented after the item is built");

    CheckEqI32(p->nSeed, 0, "item 0 carries seed 0");
    CheckEqI32(p->nType, 1, "nType");
    CheckEqI32(p->bActive, 0, "bActive");
    CheckEqI32(p->bCraftClass, 2, "bCraftClass");
    CheckEqI32(p->bVisible, 1, "bVisible");
    CheckEqI32(p->bRace, 255, "bRace is written as -1, so 255 in a byte");
    CheckEqI32(p->nHp, -1, "nHp starts at -1");
    CheckEqI32(p->nPhase, 0, "nPhase");
    CheckEqI32(p->bDone, 0, "bDone");
    CheckEqI32(p->nTimer, 0, "nTimer");
    CheckEqI32(p->bFlagA, 0, "bFlagA");
    CheckEqI32(p->bHyperDetect, 0, "bHyperDetect");
    CheckEqI32(p->bDetected, 0, "bDetected");

    /* nType 1 takes the display id and refuses the site id. */
    CheckEqI32(p->nDisplayId, 42, "type 1 gets nDisplayId = nId");
    CheckEqI32(p->nSiteId, -1, "type 1 gets nSiteId = -1");

    /* nType 0 and 3 are the mirror image. */
    p = StrategList_AddItem(&list, &seed, 0, 0, 1, 0, 10, 20, 7, 0);
    CheckEqI32(p->nSiteId, 7, "type 0 gets nSiteId = nId");
    CheckEqI32(p->nDisplayId, -1, "type 0 gets nDisplayId = -1");
    p = StrategList_AddItem(&list, &seed, 0, 3, 1, 0, 10, 20, 9, 0);
    CheckEqI32(p->nSiteId, 9, "type 3 gets nSiteId = nId");
    CheckEqI32(p->nDisplayId, -1, "type 3 gets nDisplayId = -1");
    CheckEqI32(p->nSeed, 2, "the third item carries seed 2");

    /* Only nType 5 converts coordinates, and only for zoom modes 1 and 2. */
    p = StrategList_AddItem(&list, &seed, 0, 5, 1, 0, 300, 300, 0, 1);
    CheckEqI32(p->nX, 300, "zoom mode 0 does not scale");
    CheckEqI32(p->nY, 300, "zoom mode 0 does not scale y either");

    p = StrategList_AddItem(&list, &seed, 1, 5, 1, 0, 300, 300, 0, 1);
    CheckEqI32(p->nX, 399, "zoom mode 1 scales by 1.333 and truncates, so 399 not 400");
    CheckEqI32(p->nStartY, 399, "nStartY carries the scaled value");
    CheckEqI32(p->nDestX, 399, "nDestX carries the scaled value");

    p = StrategList_AddItem(&list, &seed, 2, 5, 1, 0, 300, 300, 0, 1);
    CheckEqI32(p->nX, 508, "zoom mode 2 scales by 1.694");

    /* A non-5 type is never scaled, whatever the zoom mode. */
    p = StrategList_AddItem(&list, &seed, 2, 1, 1, 0, 300, 300, 5, 1);
    CheckEqI32(p->nX, 300, "a craft item is not scaled even at zoom mode 2");

    PtrArray_DeleteAll(&list);
}

static const StrategRollTables g_Roll = {
    /* unk_AAC98: mission subtype weights by craft class. */
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
    /* byte_AACD8: craft class weights by difficulty. */
    {
        { 22, 33, 35,  5,  5,  0,  0,  0 },
        { 10, 30, 35, 10, 10,  5,  0,  0 },
        {  8, 20, 20, 15, 15,  7, 10,  5 },
        {  0, 20, 20, 15, 15,  8, 17,  5 },
        {  0, 15, 15, 15, 15,  8, 20, 12 },
        {  0, 10, 15, 15, 15,  8, 22, 15 },
    },
    /* byte_AAD08: race weights by difficulty. */
    {
        { 100, 0, 0, 0, 0 },
        {  70, 0, 30, 0, 0 },
        {  33, 0, 33, 34, 0 },
        {  25, 25, 25, 25, 0 },
        {  20, 20, 20, 20, 20 },
        {  20, 20, 20, 20, 20 },
    },
};

static StrategItem *g_pSpawnedItem;
static i32          g_nSpawnSub;
static i32          g_nSpawnCalls;
static void        *g_apSlots[1];

static int SpawnProbe(StrategItem *pUfo, i32 nMissionSub, void *pUser)
{
    (void)pUser;
    g_pSpawnedItem = pUfo;
    g_nSpawnSub = nMissionSub;
    ++g_nSpawnCalls;
    return 7;
}

static void TestAddUfoAt(void)
{
    printf("StrategList_AddUfoAt\n");
    PtrArray list;
    StrategItem *p;
    i32 seed = 0;

    PtrArray_ctor(&list);

    p = StrategList_AddUfoAt(&list, &seed, 2, 300, 400, 3);
    Check(p != NULL, "appends a craft item");
    CheckEqI32(p->nType, 1, "nType is 1, a craft");
    CheckEqI32(p->bActive, 1, "bActive is 1");
    CheckEqI32(p->bCraftClass, 3, "the craft class is the only free argument");
    CheckEqI32(p->nDisplayId, 50000, "nId 50000 lands in nDisplayId because nType is 1");
    CheckEqI32(p->nSiteId, -1, "nSiteId is -1");
    CheckEqI32(p->bVisible, 0, "bVisible is 0");
    /* Only nType 5 is scaled, so zoom mode 2 leaves a craft where it was put. */
    CheckEqI32(p->nX, 300, "a craft is not scaled even at zoom mode 2");
    CheckEqI32(p->nY, 400, "nor is y");

    PtrArray_DeleteAll(&list);
}

static void TestRollCraftStats(void)
{
    printf("StrategList_RollCraftStats\n");
    PtrArray list;
    StrategItem item;
    i32 i;
    i32 nBadRace = 0, nBadCraft = 0, nBadSub = 0, nBadRet = 0, nRace0 = 0;

    PtrArray_ctor(&list);
    g_apSlots[0] = &item;
    list.apItems = g_apSlots;
    list.nCount = 1;
    memset(&item, 0, sizeof(item));

    /* Difficulty 0 has a race row of a single 100, so race 0 is forced whatever
     * the roll. That makes this deterministic without controlling the RNG. */
    for (i = 0; i < 200; ++i) {
        int nRet;
        item.bRace = 0;
        item.bCraftClass = 0;
        nRet = StrategList_RollCraftStats(&list, 0, 0, 99, &g_Roll, SpawnProbe, NULL);
        if (nRet != 7) {
            ++nBadRet;
        }
        if (item.bRace != 0) {
            ++nBadRace;
        } else {
            ++nRace0;
        }
        /* Difficulty 0 craft row totals 100 across columns 0..4, so 5..7 are
         * unreachable there. */
        if (item.bCraftClass > 4) {
            ++nBadCraft;
        }
        if (g_nSpawnSub < 0 || g_nSpawnSub > 7) {
            ++nBadSub;
        }
    }
    CheckEqI32(nBadRace, 0, "difficulty 0 can only roll race 0");
    CheckEqI32(nRace0, 200, "and it does it every time");
    CheckEqI32(nBadCraft, 0, "difficulty 0 craft class stays in 0..4");
    CheckEqI32(nBadRet, 0, "the spawn return value is passed straight through");
    CheckEqI32(nBadSub, 0, "the mission subtype handed to the spawn is always 0..7");
    Check(g_pSpawnedItem == &item, "the spawn gets the item at nIndex");

    /* The early gate caps the craft class at 2. */
    nBadCraft = 0;
    for (i = 0; i < 200; ++i) {
        StrategList_RollCraftStats(&list, 0, 5, 0, &g_Roll, SpawnProbe, NULL);
        if (item.bCraftClass > 2) {
            ++nBadCraft;
        }
    }
    CheckEqI32(nBadCraft, 0, "while the gate is below 15 no craft class above 2 survives");

    /* Out of range index: no spawn, no crash. */
    g_nSpawnCalls = 0;
    CheckEqI32(StrategList_RollCraftStats(&list, 9, 0, 99, &g_Roll, SpawnProbe, NULL), 0,
               "an out-of-range index returns without spawning");
    CheckEqI32(g_nSpawnCalls, 0, "and the spawn hook is not called");

    list.apItems = NULL;
    list.nCount = 0;
}

static void TestAddAlienSiteAndTravel(void)
{
    printf("StrategList_AddAlienSiteIfValid @ 0x7e3b4, StrategEntry_InitTravel @ 0x7f60c\n");
    PtrArray list;
    StrategItem item;
    StrategItem *p;
    PadImage grid;
    Ship ship;
    Ship *apCraft[1];
    GeoscapeWorld world;
    i32 seed = 0;
    i32 i;
    i32 nBelowFloor = 0;
    u8 aGrid[6];

    for (i = 0; i < 6; ++i) {
        aGrid[i] = 7;
    }
    memset(&grid, 0, sizeof(grid));
    grid.uWidth = 3;
    grid.pData = aGrid;

    PtrArray_ctor(&list);

    /* Ocean refuses. */
    aGrid[0] = 255;
    Check(!StrategList_AddAlienSiteIfValid(&list, &seed, 0, &grid, 0, 0, 1),
          "a 255 tile is ocean and nothing is added");
    CheckEqI32(list.nCount, 0, "the list did not grow");

    /* Land adds the mirror image of a spawned craft. */
    aGrid[0] = 7;
    p = StrategList_AddAlienSiteIfValid(&list, &seed, 0, &grid, 40, 50, 1) ? list.apItems[0] : NULL;
    Check(p != NULL, "a land tile adds the site");
    CheckEqI32(p->nType, 0, "nType 0, a site");
    CheckEqI32(p->bActive, 1, "bActive 1");
    CheckEqI32(p->bCraftClass, 255, "bCraftClass -1 becomes 255 through the byte store");
    CheckEqI32(p->nSiteId, 50000, "nSiteId gets the 50000 because nType is 0");
    CheckEqI32(p->nDisplayId, -1, "nDisplayId is -1, the mirror of a craft");
    CheckEqI32(p->bVisible, 1, "bVisible is the discovered flag");
    CheckEqI32(p->nX, 40, "x");
    CheckEqI32(p->nY, 50, "y");
    PtrArray_DeleteAll(&list);

    /* ---- InitTravel ---- */
    memset(&item, 0, sizeof(item));
    memset(&ship, 0, sizeof(ship));
    apCraft[0] = &ship;
    memset(&world, 0, sizeof(world));
    world.apCraft = apCraft;
    world.nShipCount = 1;

    item.nType = 0;      /* a site: the speed branch never runs */
    item.dSpeed = 5.0;
    StrategEntry_InitTravel(&item, 100, 10, 150, 10, 0, 0, 1, 6, 1, &world, 4242);
    CheckEqI32(item.nX, 100, "bCopyStart copies the start into the position");
    CheckEqI32(item.nStartX, 100, "nStartX");
    CheckEqI32(item.nDestX, 150, "nDestX");
    CheckEqI32(item.nMissionSub, 6, "nMissionSub is the bC argument");
    CheckEqI32(item.nTravelEpochSec, 4242, "the epoch is the clock at apLines[105]");
    CheckEqI32(StrategEntry_InitTravel(&item, 100, 10, 150, 10, 0, 0, 1, 6, 1, &world, 77), 77,
               "the return value is the same clock value");
    /* A site is not a craft, so its speed is whatever it already had. */
    Check(item.dSpeed > 4.99 && item.dSpeed < 5.01, "a site item is given no speed of its own");
    Check(item.dTrackA > 0.099 && item.dTrackA < 0.101, "dTrackA is dSpeed times 0.02");
    CheckEqI32(item.nTravelStartSec, 50, "the plain leg wins over both wrap legs");
    CheckEqI32(item.bTravelDir, 1, "and it runs forwards");
    CheckEqI32(item.uTravelMin, 500, "sqrt(50^2) over dTrackA 0.1 is 500 minutes");

    /* The cylinder: 100 to 700 is 600 the long way and 200 through the wrap. */
    CheckEqI32(StrategEntry_InitTravel(&item, 100, 10, 700, 10, 0, 0, 1, 0, 1, &world, 0), 0,
               "wrap leg returns the clock");
    CheckEqI32(item.nTravelStartSec, 200, "the wrap-around leg is shorter and wins");
    CheckEqI32(item.bTravelDir, -1, "and it runs backwards");
    StrategEntry_InitTravel(&item, 700, 10, 100, 10, 0, 0, 1, 0, 1, &world, 0);
    CheckEqI32(item.nTravelStartSec, 200, "the other direction finds the same wrap leg");
    CheckEqI32(item.bTravelDir, 1, "and runs forwards");

    /* A dwelling item has no speed at all. */
    item.dSpeed = 5.0;
    StrategEntry_InitTravel(&item, 100, 10, 150, 10, 200, 0, 1, 1, 1, &world, 0);
    Check(item.dSpeed == 0.0, "nTimer above 0 forces the speed to zero, whatever it was");
    Check(item.dTrackA == 0.0, "so dTrackA is zero too");
    CheckEqI32(item.uTravelMin, 0, "and the port refuses the divide by zero the original does not");

    /* An inactive craft takes its speed from the matching ship's fuel capacity. */
    memset(&item, 0, sizeof(item));
    item.nType = 1;
    item.bActive = 0;
    item.nDisplayId = 42;
    ship.bId = 42;
    ship.uFuelCap = 120;
    StrategEntry_InitTravel(&item, 100, 10, 150, 10, 0, 0, 1, 0, 1, &world, 0);
    Check(item.dSpeed > 1.99 && item.dSpeed < 2.01, "120 fuel capacity over 60 is speed 2");
    StrategEntry_InitTravel(&item, 100, 10, 150, 10, 5, 0, 1, 0, 1, &world, 0);
    Check(item.dSpeed == 0.0, "an inactive craft with nFlag above 0 does not move");

    /* An active craft is floored at 10.0. Class 1 can draw 17 * 2 = 34 of jitter off a
     * base of 40, which is 6, so the floor is what holds it up. */
    for (i = 0; i < 300; ++i) {
        memset(&item, 0, sizeof(item));
        item.nType = 1;
        item.bActive = 1;
        item.bCraftClass = 1;
        StrategEntry_InitTravel(&item, 100, 10, 150, 10, 0, 0, 1, 0, 1, &world, 0);
        if (item.dSpeed < 10.0) {
            ++nBelowFloor;
        }
    }
    CheckEqI32(nBelowFloor, 0, "no active craft speed is ever left below 10.0");

    PtrArray_DeleteAll(&list);
}

static i32 g_nEventCalls;

static int EventProbe(void *pUser)
{
    (void)pUser;
    ++g_nEventCalls;
    return 11;
}

static void TestMaybeSpawnCraft(void)
{
    printf("StrategList_MaybeSpawnCraft_Process\n");
    PtrArray list;
    StrategItem item;
    i32 seed = 0;
    i32 i;
    i32 nBadX = 0, nBadY = 0;

    PtrArray_ctor(&list);

    /* bSpawn 0: no item, but the event pass still runs and its value is returned. */
    g_nEventCalls = 0;
    g_nSpawnCalls = 0;
    CheckEqI32(StrategList_MaybeSpawnCraft_Process(&list, &seed, 0, &g_Roll, 0, 99, 0,
                                                   SpawnProbe, EventProbe, NULL), 11,
               "the event pass runs even when nothing spawns");
    CheckEqI32(list.nCount, 0, "no item was added");
    CheckEqI32(g_nSpawnCalls, 0, "and neither was the mission spawn");

    /* The gate is exactly 1. */
    CheckEqI32(StrategList_MaybeSpawnCraft_Process(&list, &seed, 0, &g_Roll, 0, 99, 2,
                                                   SpawnProbe, EventProbe, NULL), 11,
               "bSpawn 2 is not 1, so it does not spawn either");
    CheckEqI32(list.nCount, 0, "bSpawn is compared, not tested");

    /* bSpawn 1: one craft, rolled, handed to the mission spawn. */
    for (i = 0; i < 50; ++i) {
        StrategList_MaybeSpawnCraft_Process(&list, &seed, 0, &g_Roll, 0, 99, 1,
                                            SpawnProbe, EventProbe, NULL);
    }
    CheckEqI32(list.nCount, 50, "each call adds exactly one item");
    CheckEqI32(g_nSpawnCalls, 50, "and each one goes through the mission spawn");
    CheckEqI32(seed, 50, "the seed counter advanced once per item");

    for (i = 0; i < 50; ++i) {
        StrategItem *p = (StrategItem *)list.apItems[i];
        if (p->nX < 20 || p->nX > 780) {
            ++nBadX;
        }
        if (p->nY < 20 || p->nY > 380) {
            ++nBadY;
        }
        if (p->nType != 1 || p->bActive != 1) {
            ++nBadX;
        }
    }
    CheckEqI32(nBadX, 0, "every spawn is a craft inside 20..780 in x");
    CheckEqI32(nBadY, 0, "and 20..380 in y, the map with a 20 tile margin");

    (void)item;
    PtrArray_DeleteAll(&list);
}

static void TestRemoveAtAndPurge(void)
{
    printf("StrategList_RemoveAt @ 0x82ab0, StrategList_PurgeType5 @ 0x6ab58\n");
    PtrArray list;
    Ship ship;
    Ship *apCraft[1];
    GeoscapeWorld world;
    StrategSpawnCtx ctx;
    u8 once;
    i32 seed = 0;
    i32 i;
    i32 nMonthCounter = 0;   /* unk_159648: the row index 0x81AB4 reads */
    u16 nAlienPoolAddend = 0; /* 0x1472C8: the early gate 0x81B7C reads, below 15 so the class caps at 2 */

    apCraft[0] = &ship;
    memset(&world, 0, sizeof(world));
    world.apCraft = apCraft;
    world.nShipCount = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pTables = &g_Roll;
    ctx.pnDifficulty = &nMonthCounter;
    ctx.pnEarlyGate = &nAlienPoolAddend;
    ctx.pnSeed = &seed;
    ctx.pfnSpawnMission = SpawnProbe;
    ctx.pfnProcessEvents = EventProbe;

    /* ---- RemoveAt re-points the intercepts that sit after the hole ---- */
    PtrArray_ctor(&list);
    for (i = 0; i < 3; ++i) {
        (void)StrategItem_AllocAdd(&list, i);
        ((StrategItem *)list.apItems[i])->nType = 1;
    }
    memset(&ship, 0, sizeof(ship));
    ship.bIntercepting = 1;
    ship.nStrategIdx = 2;
    once = 0;
    CheckEqI32(StrategList_RemoveAt(&list, &world, 0, &once, &ctx), 0,
               "with the one-shot flag clear it returns that flag unchanged");
    CheckEqI32(list.nCount, 2, "one item removed");
    CheckEqI32(ship.nStrategIdx, 1, "the intercept after the hole moved down to follow its target");

    ship.nStrategIdx = 0;
    (void)StrategList_RemoveAt(&list, &world, 0, &once, &ctx);
    CheckEqI32(ship.nStrategIdx, 0, "an intercept at or below the removed index is left alone");
    PtrArray_DeleteAll(&list);

    /* ---- the one-shot replacement pass ---- */
    /* Game_RandU16(3) has to land on exactly 1, so a single pass is a coin flip.
     * Repeat it: 40 passes is 120 rolls, and the flag is re-armed each time. */
    once = 1;
    g_nSpawnCalls = 0;
    g_nEventCalls = 0;
    PtrArray_ctor(&list);
    CheckEqI32(StrategList_RemoveAt(&list, &world, 0, &once, &ctx), 0,
               "a replacement pass returns 0, not the flag it read");
    CheckEqI32(once, 0, "and the one-shot byte is cleared, so it can only fire once");
    PtrArray_DeleteAll(&list);

    for (i = 0; i < 40; ++i) {
        PtrArray_ctor(&list);
        once = 1;
        (void)StrategList_RemoveAt(&list, &world, 0, &once, &ctx);
        PtrArray_DeleteAll(&list);
    }
    Check(g_nSpawnCalls > 0, "the replacement spawn does fire on a roll of exactly 1");
    Check(g_nEventCalls >= g_nSpawnCalls,
          "each replacement spawn also runs the geoscape event pass");

    /* ---- PurgeType5, and the cascade bug the original really has ---- */
    /* A claimed type-5 item survives and nothing else is touched. */
    PtrArray_ctor(&list);
    for (i = 0; i < 5; ++i) {
        (void)StrategItem_AllocAdd(&list, i);
        ((StrategItem *)list.apItems[i])->nType = (i == 0) ? 5 : 1;
    }
    memset(&ship, 0, sizeof(ship));
    ship.bIntercepting = 1;
    ship.nStrategIdx = 0;
    once = 0;
    CheckEqI32(StrategList_PurgeType5(&list, &world, &once, &ctx), 5,
               "a type-5 item a ship is chasing is kept, and so is everything else");
    PtrArray_DeleteAll(&list);

    /* No ships: the first item is unclaimed type 5, so it goes -- and then the flag
     * is still 0, so the next item goes too, whatever it is. Five items in, one out. */
    PtrArray_ctor(&list);
    for (i = 0; i < 5; ++i) {
        (void)StrategItem_AllocAdd(&list, i);
        ((StrategItem *)list.apItems[i])->nType = (i == 0) ? 5 : 1;
    }
    memset(&ship, 0, sizeof(ship));
    world.nShipCount = 0;
    once = 0;
    CheckEqI32(StrategList_PurgeType5(&list, &world, &once, &ctx), 1,
               "ORIGINAL BUG REPRODUCED: one unclaimed type-5 item cascades and deletes every item after it");
    PtrArray_DeleteAll(&list);

    /* Same bug from the middle: index 1 is the unclaimed type 5, so 1 and 2 go and
     * the walk then steps past the end. Five in, two out. */
    PtrArray_ctor(&list);
    for (i = 0; i < 5; ++i) {
        (void)StrategItem_AllocAdd(&list, i);
        ((StrategItem *)list.apItems[i])->nType = (i == 1) ? 5 : 1;
    }
    once = 0;
    CheckEqI32(StrategList_PurgeType5(&list, &world, &once, &ctx), 2,
               "the cascade stops only when the index reaches the last item");
    PtrArray_DeleteAll(&list);

    world.nShipCount = 1;
}

static void TestUpdateMotion(void)
{
    printf("StrategList_UpdateMotion\n");
    StrategList list;
    GeoscapeWorld world;
    SetupWorld(&world, &list, 2, 1);

    /* item 0: alien site being scanned; item 1: the ship's own entry */
    g_aItems[0].nType = 0; g_aItems[0].bActive = 1;
    g_aItems[0].nX = 100; g_aItems[0].nY = 100;
    g_aItems[1].nType = 1; g_aItems[1].bActive = 0; g_aItems[1].nDisplayId = 1;
    g_aItems[1].nX = 100; g_aItems[1].nY = 100;

    g_aShips[0].bId = 1;
    g_aShips[0].bAtBase = 0;
    g_aShips[0].bCraftType = 0; /* radius 15 */
    g_aShips[0].bWeaponsHot = 1;

    StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].nTimer, 1, "hot ship at distance grows the timer");
    CheckEqI32(g_aItems[0].bVisible, 0, "not visible yet");

    /* thresh 120: visible once the timer reaches 120 */
    for (i32 i = 1; i < 119; ++i)
        StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].bVisible, 0, "still hidden at timer 119");
    StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].bVisible, 1, "visible when timer reaches thresh");
    CheckEqI32(g_aItems[0].nTimer, 0, "timer cleared on visibility");
    CheckEqI32(g_aItems[0].nDisplayId, 0, "display id assigned on visibility");
    CheckEqI32(g_aItems[0].nSiteId, 0, "site id assigned on visibility");

    /* Ship cold -> timer decays to zero each pass. */
    g_aItems[0].bVisible = 0;
    g_aItems[0].nDisplayId = STRATEG_ID_UNASSIGNED;
    g_aItems[0].nSiteId = STRATEG_ID_UNASSIGNED;
    g_aShips[0].bWeaponsHot = 0;
    g_aItems[0].nTimer = 50;
    StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].nTimer, 0, "timer reset when no hot ship is near");

    /* Out of radius: craft type 0 radius 15, distance ~300. */
    g_aShips[0].bWeaponsHot = 1;
    g_aItems[1].nX = 400;
    StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].nTimer, 0, "out of radius does not grow the timer");

    /* Distance is sqrt(dx^2 + dy^2 + thresh): dx=11, dy=11, thresh=120 ->
     * sqrt(362) = 19.03 -> 19, which is outside radius 15 but inside the
     * type-2 radius 25. */
    g_aItems[1].nX = 111; g_aItems[1].nY = 111;
    g_aShips[0].bCraftType = 0;
    StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].nTimer, 0, "sqrt(11^2+11^2+120)=19 misses radius 15");
    g_aShips[0].bCraftType = 2;
    StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].nTimer, 1, "same distance hits radius 25");

    /* Ship at base is skipped entirely. */
    g_aShips[0].bAtBase = 1;
    g_aItems[0].nTimer = 7;
    StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].nTimer, 0, "ship at base is ignored");
    g_aShips[0].bAtBase = 0;

    /* Fast tick (3600 s) uses threshold 1: visible in a single pass. */
    world.nTickSeconds = 3600;
    g_aItems[0].bVisible = 0;
    g_aItems[0].nDisplayId = STRATEG_ID_UNASSIGNED;
    g_aItems[0].nSiteId = STRATEG_ID_UNASSIGNED;
    g_aItems[1].nX = 100; g_aItems[1].nY = 100;
    StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].bVisible, 1, "threshold 1 makes one pass enough");

    /* Type-1 active items become visible on proximity without weapons hot. */
    world.nTickSeconds = 60;
    g_aItems[0].nType = 1;
    g_aShips[0].bWeaponsHot = 0;
    g_aItems[0].bVisible = 0;
    StrategList_UpdateMotion(&list, &world);
    CheckEqI32(g_aItems[0].bVisible, 1, "type-1 item visible without hot weapons");
}

static void TestCalcDetect(void)
{
    printf("Strateg_CalcDetect\n");
    StrategList list;
    GeoscapeWorld world;
    StrategHooks hooks = { Hook_Msg, Hook_FindBase, 0 };
    SetupWorld(&world, &list, 2, 0);

    /* item 0: player base entry (type 0, inactive), site id 7 -> base 0 */
    g_aItems[0].nType = 0; g_aItems[0].bActive = 0;
    g_aItems[0].nSiteId = 7;
    g_aItems[0].nX = 10; g_aItems[0].nY = 100;

    /* item 1: the ufo, wrapped-close to the base: dx = 20, dy = 0 */
    g_aItems[1].nType = 1; g_aItems[1].bActive = 1;
    g_aItems[1].nX = 790; g_aItems[1].nY = 100;

    g_aSiteState[0] = 1;
    g_aRadarPct[0] = 100;
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "state 1 within wrapped distance 20 and radius 50 detects");
    CheckEqI32(g_nMsgId, 10, "radar contact posts message 10");
    CheckEqI32(g_nMsgArg3, 1, "message carries the ufo index");
    CheckEqI32(g_Time.bPaused, 1, "radar contact pauses the clock");
    CheckEqI32(g_aItems[1].bHyperDetect, 0, "state 1 is not hyper");

    /* Timer busy blocks every state. */
    g_nMsgCount = 0;
    g_aItems[1].nTimer = 3;
    Check(!Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "busy proximity timer blocks detection");
    CheckEqI32(g_nMsgCount, 0, "blocked detection posts nothing");
    g_aItems[1].nTimer = 0;

    /* Force the geometric result without consuming an RNG draw or posting another contact. */
    g_aItems[1].bVisible = 1;
    g_aItems[1].nX = 760; /* base x=10: wrapped dx is exactly 50 */
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 100, &list, &world, &hooks),
          "state 1 includes the wrapped 50-tile boundary");
    g_aItems[1].nX = 759; /* wrapped dx is 51 */
    Check(!Strateg_CalcDetect(&g_aItems[1], 1, 100, &list, &world, &hooks),
          "state 1 excludes wrapped distance 51");

    /* Keep the prior in-range state-2 call, including its RNG draw for later test order. */
    g_aSiteState[0] = 2;
    g_aItems[1].bVisible = 0;
    g_aItems[1].nX = 40; /* dx = 30 */
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "state 2 detects a UFO within radius 65");

    /* State 2 uses the 65-tile boundary without another RNG draw. */
    g_aItems[1].bVisible = 1;
    g_aItems[1].nX = 75; /* base x=10, dx=65 */
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 100, &list, &world, &hooks),
          "state 2 includes distance 65");
    g_aItems[1].nX = 76; /* dx=66 */
    Check(!Strateg_CalcDetect(&g_aItems[1], 1, 100, &list, &world, &hooks),
          "state 2 excludes distance 66");

    /* Keep the prior in-range state-11 call, including its RNG draw for later test order. */
    g_aSiteState[0] = 11;
    g_aItems[1].bVisible = 0;
    g_aItems[1].nX = 790; /* wrapped dx = 20 */
    g_aItems[1].bHyperDetect = 0;
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "state 11 detects a UFO within radius 50");
    CheckEqI32(g_aItems[1].bHyperDetect, 1, "state 11 sets hyper flag");

    /* Hyper states use the same ranges and set their extra marker only inside them. */
    g_aItems[1].bVisible = 1;
    g_aItems[1].nX = 760;
    g_aItems[1].bHyperDetect = 0;
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 100, &list, &world, &hooks),
          "state 11 includes the wrapped 50-tile boundary");
    CheckEqI32(g_aItems[1].bHyperDetect, 1, "state 11 sets hyper at the boundary");
    g_aItems[1].nX = 759;
    Check(!Strateg_CalcDetect(&g_aItems[1], 1, 100, &list, &world, &hooks),
          "state 11 excludes wrapped distance 51");
    CheckEqI32(g_aItems[1].bHyperDetect, 0, "state 11 does not set hyper outside range");

    g_aSiteState[0] = 12;
    g_aItems[1].nX = 75;
    g_aItems[1].bHyperDetect = 0;
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 100, &list, &world, &hooks),
          "state 12 includes distance 65");
    CheckEqI32(g_aItems[1].bHyperDetect, 1, "state 12 sets hyper flag");
    g_aItems[1].nX = 76;
    Check(!Strateg_CalcDetect(&g_aItems[1], 1, 100, &list, &world, &hooks),
          "state 12 excludes distance 66");
    CheckEqI32(g_aItems[1].bHyperDetect, 0, "state 12 does not set hyper outside range");
    g_aItems[1].nX = 790;
    g_aItems[1].bVisible = 0;

    /* Unknown state does nothing. */
    g_aSiteState[0] = 5;
    Check(!Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "unknown site state never detects");

    /* Radar 0% with the +7 boost off cannot detect (rand < 0 is never true). */
    g_aSiteState[0] = 1;
    g_aRadarPct[0] = 0;
    world.nRadarBoostOff = 1;
    Check(!Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "0% radar without boost cannot detect");

    /* Radar 93% with the boost on becomes 100 -> guaranteed detection. */
    g_aRadarPct[0] = 93;
    world.nRadarBoostOff = 0;
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "93% radar + 7 boost detects like 100%");

    /* nForcePct 100 pins detection regardless of the radar table. */
    g_aRadarPct[0] = 0;
    world.nRadarBoostOff = 1;
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 100, &list, &world, &hooks),
          "forced 100% detection wins");
}

static void TestUpdateDetectAndIntercepts(void)
{
    printf("StrategList_UpdateDetect @ 0x81948, Ship_ClearExpiredIntercepts @ 0x8128c\n");
    StrategList list;
    GeoscapeWorld world;
    StrategHooks hooks = { Hook_Msg, Hook_FindBase, 0 };
    SetupWorld(&world, &list, 2, 1);

    g_aItems[0].nType = 0; g_aItems[0].bActive = 0;
    g_aItems[0].nSiteId = 7; g_aItems[0].nX = 10; g_aItems[0].nY = 100;
    g_aItems[1].nType = 1; g_aItems[1].bActive = 1;
    g_aItems[1].nX = 790; g_aItems[1].nY = 100;
    g_aItems[1].bDetected = 1; /* forces 100% */
    g_aSiteState[0] = 1;
    g_aRadarPct[0] = 0;

    StrategList_UpdateDetect(&list, &world, &hooks);
    CheckEqI32(g_aItems[1].bDetected, 1, "already-detected ufo stays detected");
    CheckEqI32(g_aItems[1].bVisible, 1, "detected ufo is visible");
    CheckEqI32(g_aItems[1].nDisplayId, 0, "display id assigned");

    /* Not detected -> visible and hyper cleared. */
    g_aItems[1].bDetected = 0;
    g_aItems[1].bHyperDetect = 1;
    StrategList_UpdateDetect(&list, &world, &hooks);
    CheckEqI32(g_aItems[1].bDetected, 0, "0% radar loses detection");
    CheckEqI32(g_aItems[1].bVisible, 0, "lost detection hides the ufo");
    CheckEqI32(g_aItems[1].bHyperDetect, 0, "lost detection clears hyper");

    /* Intercept expiry. */
    g_aShips[0].bIntercepting = 1;
    g_aShips[0].nStrategIdx = 1;
    g_aItems[1].bVisible = 0;
    g_aItems[1].nDisplayId = 123; /* already assigned -> target is really gone */
    g_nMsgCount = 0;
    Ship_ClearExpiredIntercepts(&world, &list, &hooks);
    CheckEqI32(g_aShips[0].bIntercepting, 0, "intercept dropped when target lost");
    CheckEqI32(g_nMsgId, 13, "lost intercept posts message 13");
    CheckEqI32(g_Time.bPaused, 1, "lost intercept pauses the clock");

    g_aShips[0].bIntercepting = 1;
    g_aItems[1].nDisplayId = STRATEG_ID_UNASSIGNED; /* pending -> keep */
    Ship_ClearExpiredIntercepts(&world, &list, &hooks);
    CheckEqI32(g_aShips[0].bIntercepting, 1, "pending target keeps the intercept");

    g_aShips[0].bIntercepting = 1;
    g_aItems[1].nDisplayId = 123;
    g_aItems[1].bVisible = 1;
    Ship_ClearExpiredIntercepts(&world, &list, &hooks);
    CheckEqI32(g_aShips[0].bIntercepting, 1, "visible target keeps the intercept");
}

static void TestSmapView(void)
{
    SmapView view;
    u8 aTiles[8 * 4];
    PadImage grid;
    u8 aGrid[6];

    printf("SmapView @ 0x1863E8 -- Smap_ScrollBy @ 0x820d0 (Smap_ClampScrollY @ 0x837f8 inside it), "
           "Tilemap_GetAt @ 0x8217c, Tilemap_GetAtAlt @ 0x821a0, Smap_GetTileAtMode @ 0x821c4\n");

    memset(&view, 0, sizeof(view));
    view.nMapW = 8;
    view.nMapH = 4;

    /* Screen mode 2 skips the clamp, so this shows the drag arithmetic alone. */
    Smap_ScrollBy(&view, 10, 20, 2, 0);
    CheckEqI32(view.nScrollX, -10, "first drag moves the view by minus the delta");
    CheckEqI32(view.nLastDx, 10, "the delta is stored for the next drag");
    Smap_ScrollBy(&view, 4, 6, 2, 0);
    CheckEqI32(view.nScrollX, -4, "the previous delta is added back first");
    CheckEqI32(view.nScrollY, -6, "Y follows the same rule");

    /* nMapH 400 gives halfH = 40, so the clamps are reachable. */
    memset(&view, 0, sizeof(view));
    view.nMapW = 8;
    view.nMapH = 400;
    view.nScrollY = -100;
    Smap_ScrollBy(&view, 0, 0, 0, 0);
    CheckEqI32(view.nScrollY, -40, "the lower clamp is -(nMapH/2 - 160)");

    memset(&view, 0, sizeof(view));
    view.nMapW = 8;
    view.nMapH = 400;
    view.nScrollY = 300;
    Smap_ScrollBy(&view, 0, 0, 0, 0);
    CheckEqI32(view.nScrollY, 40, "the upper clamp uses the strip height");

    memset(&view, 0, sizeof(view));
    view.nMapW = 8;
    view.nMapH = 400;
    view.nScrollY = 300;
    Smap_ScrollBy(&view, 0, 0, 2, 0);
    CheckEqI32(view.nScrollY, 300, "screen mode 2 skips both clamps");

    /* Screen (120, 160) is the view centre. With scroll 0 and nMapW/2 = 4 the tile
     * under screen (116, 158) is (0, 0). */
    for (i32 i = 0; i < 32; ++i) {
        aTiles[i] = (u8)i;
    }
    memset(&view, 0, sizeof(view));
    view.nMapW = 8;
    view.nMapH = 4;
    CheckEqI32(Smap_GetTileAtScreen(&view, aTiles, 116, 158), 0, "centre maps to tile 0");
    CheckEqI32(view.nPickX, 0, "the pick x is written");
    CheckEqI32(view.nPickY, 0, "the pick y is written");
    CheckEqI32(Smap_GetTileAtScreen(&view, aTiles, 119, 159), 11, "one row down is stride 8");
    CheckEqI32(view.nPickX, 3, "tile x from the stride");
    CheckEqI32(view.nPickY, 1, "tile y from the stride");

    /* A negative coordinate wraps once. */
    CheckEqI32(Smap_GetTileAtScreen(&view, aTiles, 111, 158), 3, "a negative x wraps by nMapW");
    CheckEqI32(view.nPickX, 3, "the wrapped coordinate is the one stored");

    /* Past one wrap the original would read out of the grid; the port refuses. */
    CheckEqI32(Smap_GetTileAtScreen(&view, aTiles, 107, 158), 0, "a second wrap is refused");
    CheckEqI32(view.nPickX, -1, "the pick still records where it tried to read");
    CheckEqI32(Smap_GetTileAtScreen(&view, aTiles, 116 + 8, 158), 0, "an oversized x is refused");

    /* Tilemap_GetAt and Tilemap_GetAtAlt are the same expression over two globals. */
    for (i32 i = 0; i < 6; ++i) {
        aGrid[i] = (u8)(i + 1);
    }
    memset(&grid, 0, sizeof(grid));
    grid.uWidth = 3;
    grid.pData = aGrid;
    CheckEqI32(Tilemap_GetAt(&grid, 2, 1), 6, "tile grid lookup is pData + uWidth*y + x");
    CheckEqI32(Tilemap_GetAt(&grid, 1, 0), 2, "first row");

    /* Smap_GetTileAtMode @ 0x821c4 reads the OVERLAY pad the current mode selects, with the x origin
     * folded in first -- a different pad per zoom level, not the terrain layer above. */
    {
        static PadImage aOverlay[3];
        const PadImage *apOverlay[3] = { &aOverlay[0], &aOverlay[1], &aOverlay[2] };

        memset(aOverlay, 0, sizeof(aOverlay));
        aOverlay[0].uWidth = 4;
        aOverlay[0].pData = aTiles; /* mode 0 sees the tile bytes */
        aOverlay[1].uWidth = 3;
        aOverlay[1].pData = aGrid;  /* mode 1 sees the 1..6 grid */
        memset(&view, 0, sizeof(view));
        view.nMapW = 8;
        view.nMapH = 4;
        view.nMode = 0;
        CheckEqI32(Smap_GetOverlayAtMode(&view, apOverlay, 2, 5, 0), 3,
                   "mode 0 reads the first overlay pad at x - origin");
        view.nMode = 1;
        CheckEqI32(Smap_GetOverlayAtMode(&view, apOverlay, 2, 5, 0), 4,
                   "mode 1 swaps to the second pad, the same grid lookup");
        view.nMode = 3;
        CheckEqI32(Smap_GetOverlayAtMode(&view, apOverlay, 2, 5, 0), 0,
                   "a mode outside 0..2 is refused (the record reads an uninitialised byte)");
    }
}

static void TestSmapZoom(void)
{
    SmapView view;
    SmapZoomSet aSets[3];
    SmapWorldSize world;
    static PadImage aGrid[3];

    printf("Smap_ZoomIn @ 0x81e44 / Smap_ZoomOut @ 0x81c08\n");

    aGrid[0].uWidth = 800; aGrid[0].uHeight = 400;
    aGrid[1].uWidth = 600; aGrid[1].uHeight = 300;
    aGrid[2].uWidth = 472; aGrid[2].uHeight = 236;
    for (i32 i = 0; i < 3; ++i) {
        aSets[i].pTiles = NULL;
        aSets[i].pGrid = &aGrid[i];
        aSets[i].pOverlay = NULL;
    }
    aSets[0].uWorldW = 800; aSets[0].uWorldH = 400;
    aSets[1].uWorldW = 600; aSets[1].uWorldH = 300;
    aSets[2].uWorldW = 472; aSets[2].uWorldH = 236;

    memset(&view, 0, sizeof(view));
    view.nScrollX = 100;
    view.nScrollY = 100;

    Check(Smap_ZoomIn(&view, aSets, &world, 0), "level 0 has a zoom-in step");
    CheckEqI32(view.nMode, 1, "level 0 goes to level 1");
    CheckEqI32(view.nMapW, 600, "the map size comes from the level grid");
    CheckEqI32(view.nMapH, 300, "map height too");
    CheckEqI32(world.uW, 600, "the world pair is published");
    CheckEqI32(view.nScrollX, 75, "scroll scales by 0.7501875468867217 and truncates");
    CheckEqI32(view.nScrollY, -10, "zoom in clamps y with the new map height");

    Check(Smap_ZoomIn(&view, aSets, &world, 0), "level 1 has a zoom-in step");
    CheckEqI32(view.nMode, 2, "level 1 goes to level 2");
    CheckEqI32(view.nMapH, 236, "the closest level is 472 x 236");
    CheckEqI32(view.nScrollX, 59, "the second step scales by 1/1.27");
    CheckEqI32(view.nScrollY, -42, "y truncates toward zero before the clamp");

    Check(!Smap_ZoomIn(&view, aSets, &world, 0), "level 2 has no zoom-in step");
    CheckEqI32(view.nMode, 2, "a refused step changes nothing");

    Check(Smap_ZoomOut(&view, aSets, &world), "level 2 has a zoom-out step");
    CheckEqI32(view.nMode, 1, "level 2 goes back to level 1");
    CheckEqI32(view.nScrollX, 74, "zoom out scales by 1.27");
    CheckEqI32(view.nScrollY, -53, "zoom out applies no clamp");

    Check(Smap_ZoomOut(&view, aSets, &world), "level 1 has a zoom-out step");
    CheckEqI32(view.nMode, 0, "back to the widest level");
    CheckEqI32(view.nMapW, 800, "the widest level is 800 x 400");
    CheckEqI32(view.nScrollX, 98, "a full round trip does not restore the scroll");
    Check(!Smap_ZoomOut(&view, aSets, &world), "level 0 has no zoom-out step");
}

static i32 g_nSampleCalls;
static i32 g_aSampleLog[4];

static void RecordSample(i32 nSample, void *pUser)
{
    (void)pUser;
    if (g_nSampleCalls < 4) {
        g_aSampleLog[g_nSampleCalls] = nSample;
    }
    ++g_nSampleCalls;
}

static void TestZoomButtons(void)
{
    SmapView view;
    SmapZoomSet aSets[3];
    SmapWorldSize world;
    static PadImage aGrid[3];

    printf("Smap_OnZoomIn @ 0x84ad8 / Smap_OnZoomOut @ 0x84ab8\n");

    aGrid[0].uWidth = 800; aGrid[0].uHeight = 400;
    aGrid[1].uWidth = 600; aGrid[1].uHeight = 300;
    aGrid[2].uWidth = 472; aGrid[2].uHeight = 236;
    for (i32 i = 0; i < 3; ++i) {
        aSets[i].pTiles = NULL;
        aSets[i].pGrid = &aGrid[i];
        aSets[i].pOverlay = NULL;
    }
    aSets[0].uWorldW = 800; aSets[0].uWorldH = 400;
    aSets[1].uWorldW = 600; aSets[1].uWorldH = 300;
    aSets[2].uWorldW = 472; aSets[2].uWorldH = 236;

    memset(&view, 0, sizeof(view));
    view.nScrollX = 100;
    view.nScrollY = 100;

    g_nSampleCalls = 0;
    Check(Smap_OnZoomIn(&view, aSets, &world, 0, RecordSample, NULL),
          "the zoom-in button reports the step it took");
    CheckEqI32(g_nSampleCalls, 1, "the button plays exactly one sample");
    CheckEqI32(g_aSampleLog[0], 17, "the zoom click is sample 17");
    CheckEqI32(view.nMode, 1, "the button moves the global view one level");
    CheckEqI32(view.nScrollY, -10, "the button keeps the zoom-in clamp");

    /* 0x84AE4 runs before the zoom, so a refused step still clicks. */
    Check(Smap_OnZoomIn(&view, aSets, &world, 0, RecordSample, NULL),
          "the button walks level 1 to level 2");
    CheckEqI32(view.nMode, 2, "the closest level is now reached");
    Check(!Smap_OnZoomIn(&view, aSets, &world, 0, RecordSample, NULL),
          "level 2 still refuses the zoom-in step");
    CheckEqI32(g_nSampleCalls, 3, "a refused step still plays the sample");
    CheckEqI32(view.nMode, 2, "and still changes nothing else");

    Check(Smap_OnZoomOut(&view, aSets, &world, RecordSample, NULL),
          "the zoom-out button reports the step it took");
    CheckEqI32(g_aSampleLog[3], 17, "the zoom-out click is the same sample");
    CheckEqI32(view.nMode, 1, "zoom out moves back one level");

    view.nMode = 0;
    Check(!Smap_OnZoomOut(&view, aSets, &world, RecordSample, NULL),
          "level 0 still refuses the zoom-out step");
    CheckEqI32(g_nSampleCalls, 5, "the widest level clicks too");
}

static void TestStrategListAlloc(void)
{
    PtrArray list;

    printf("StrategItem_AllocAdd @ 0x415cc (StrategItem_ctor @ 0x82bd4 inside it), "
           "StrategList_ctor @ 0x83890, StrategList_Clear @ 0x838ac\n");

    /* Both stores 0x83890 makes are pinned: the fields start dirty, so a missing store shows up. */
    list.nCount = 7;
    list.apItems = (void **)&list;
    StrategList_ctor(&list);
    CheckEqI32(list.nCount, 0, "StrategList_ctor leaves an empty list");
    Check(list.apItems == NULL, "StrategList_ctor clears the item pointer too");

    Check(StrategItem_AllocAdd(&list, 7), "the first item allocates");
    Check(StrategItem_AllocAdd(&list, 9), "the second item allocates");
    CheckEqI32(list.nCount, 2, "each item is appended");
    CheckEqI32(((StrategItem *)list.apItems[0])->nSeed, 7, "the ctor seeds +0x34");
    CheckEqI32(((StrategItem *)list.apItems[1])->nSeed, 9, "the seed is per item");

    StrategList_Clear(&list);
    CheckEqI32(list.nCount, 0, "StrategList_Clear empties the list");
}

/* ------------------------------------------------------------------ mission machine */

static i32 g_nLineCalls, g_nLineFlag, g_nLineArg2;
static i32 g_nInterceptCalls, g_nInterceptClass;
static i32 g_nContactCalls;

static void MissionLineHook(i32 nFlag, i32 nArg2, i32 nArg3, void *pUser)
{
    (void)nArg3; (void)pUser;
    ++g_nLineCalls;
    g_nLineFlag = nFlag;
    g_nLineArg2 = nArg2;
}

static i32 MissionInterceptHook(i32 bCraftClass, void *pUser)
{
    (void)pUser;
    ++g_nInterceptCalls;
    g_nInterceptClass = bCraftClass;
    return 0;
}

static void MissionContactHook(StrategItem *pUfo, void *pUser)
{
    (void)pUfo; (void)pUser;
    ++g_nContactCalls;
}

static void TestSpawnMissionSite(void)
{
    static PadImage g_Map;
    static u8 g_aTiles[400];
    static SmapView g_View;
    static PtrArray g_List;
    static i32 g_Seed;
    static BaseRec g_aBases[2];
    static BaseRec *g_apBases[2];
    static MissionSiteRec g_aSites[4];
    static u8 g_aGrid[700];
    static GameTime g_Time;
    static GeoscapeWorld g_World;
    static i32 g_Radar;
    static SpawnMissionCtx g_Ctx;
    StrategItem ufo;

    printf("Geoscape_SpawnMissionSite\n");

    memset(g_aTiles, 1, sizeof(g_aTiles));   /* every tile is dry ground */
    memset(&g_Map, 0, sizeof(g_Map));
    g_Map.uWidth = 20;
    g_Map.pData = g_aTiles;

    memset(&g_View, 0, sizeof(g_View));
    g_View.nMapW = 800;
    g_View.nMapH = 400;

    memset(g_aSites, 0, sizeof(g_aSites));
    g_aSites[0].x = 111; g_aSites[0].y = 222;
    g_aSites[1].x = -30; g_aSites[1].y = 799;

    memset(g_aBases, 0, sizeof(g_aBases));
    g_aBases[0].nUnk8 = 40;  g_aBases[0].nUnk12 = 50;
    g_aBases[1].nUnk8 = 600; g_aBases[1].nUnk12 = 700;
    g_apBases[0] = &g_aBases[0];
    g_apBases[1] = &g_aBases[1];

    memset(&g_World, 0, sizeof(g_World));
    g_World.pTime = &g_Time;
    g_World.nTickSeconds = 60;

    PtrArray_ctor(&g_List);

    memset(&g_Ctx, 0, sizeof(g_Ctx));
    g_Ctx.pList = &g_List;
    g_Ctx.pnSeed = &g_Seed;
    g_Ctx.pView = &g_View;
    g_Ctx.pTilemap = &g_Map;
    g_Ctx.apBases = g_apBases;
    g_Ctx.nBaseCount = 1;
    g_Ctx.apSites = g_aSites;
    g_Ctx.nSiteCount = 2;
    g_Ctx.abFacilityGrid = g_aGrid;
    g_Ctx.pnRadarPulseTimer = &g_Radar;
    g_Ctx.pWorld = &g_World;
    g_Ctx.hooks.pfnAppendLine = MissionLineHook;
    g_Ctx.hooks.pfnAddIntercept = MissionInterceptHook;
    g_Ctx.hooks.pfnContactScreen = MissionContactHook;

    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1;
    ufo.bActive = 1;
    ufo.bCraftClass = 2;
    ufo.bRace = 3;
    ufo.bVisible = 1;
    ufo.nX = 100;
    ufo.nY = 100;

    /* ---- starting a subtype */
    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
    CheckEqI32(Geoscape_SpawnMissionSite(&ufo, 4, &g_Ctx), 0, "starting a subtype returns the travel result");
    CheckEqI32(ufo.nMissionSub, 4, "the item now carries subtype 4");
    CheckEqI32(ufo.nPhase, 4, "subtype 4 starts at phase 4");
    CheckEqI32(ufo.nTargetX, 0, "starting clears the target x");
    CheckEqI32(ufo.nTargetY, 0, "and the target y");

    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
    Geoscape_SpawnMissionSite(&ufo, 0, &g_Ctx);
    CheckEqI32(ufo.nPhase, 3, "subtype 0 is the one that starts at phase 3, not 4");

    /* An argument outside -1..7 does nothing at all and returns argument + 1. */
    memset(&ufo, 0, sizeof(ufo));
    ufo.nPhase = 5;
    CheckEqI32(Geoscape_SpawnMissionSite(&ufo, 8, &g_Ctx), 9, "an unknown argument returns argument + 1");
    CheckEqI32(ufo.nPhase, 5, "and the item is untouched");

    /* ---- gate 2: the dwell and the teleport */
    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
    ufo.nMissionSub = 3;
    ufo.nPhase = 3;
    ufo.nX = 10; ufo.nY = 10;
    ufo.nTargetX = 300; ufo.nTargetY = 200;
    CheckEqI32(Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx), 2, "gate 2 returns the phase it left the gate at");
    CheckEqI32(ufo.nX, 300, "a target inside the viewport teleports the craft onto it");
    CheckEqI32(ufo.nY, 200, "in both axes");
    CheckEqI32(ufo.nTimer, 720, "subtype 3 dwells 720");
    CheckEqI32(ufo.nPhase, 2, "and drops to phase 2");

    /* A target outside the viewport still sets the dwell, but the craft does not move. */
    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
    ufo.nMissionSub = 1; ufo.nPhase = 3; ufo.nX = 10; ufo.nY = 10;
    ufo.nTargetX = 5000; ufo.nTargetY = 200;
    Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx);
    CheckEqI32(ufo.nX, 10, "a target past nMapW does not teleport");
    CheckEqI32(ufo.nTimer, 200, "but the dwell is still set");

    /* ---- gate 3, subtype 5: the intercept mission */
    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bRace = 7; ufo.bVisible = 1;
    ufo.nMissionSub = 5; ufo.nPhase = 2; ufo.nTimer = 0; ufo.nTargetIdx = 3;
    g_nInterceptCalls = 0; g_nLineCalls = 0; g_Radar = 0; g_Time.bPaused = 0;
    Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx);
    CheckEqI32(g_nInterceptCalls, 1, "subtype 5 at phase 2 raises an intercept mission");
    CheckEqI32(g_nInterceptClass, 2, "with the UFO's craft class");
    CheckEqI32(g_nLineFlag, 16, "and appends the radar line, flag 16");
    CheckEqI32(g_nLineArg2, 3, "carrying the site-table index it chose");
    CheckEqI32(g_Radar, 250, "and arms the radar pulse timer");
    CheckEqI32(g_Time.bPaused, 1, "and pauses the clock");

    /* ---- gate 3, subtype 4: the alien site, tagged with the UFO's race */
    {
        StrategItem *pSite;
        i32 nBefore = g_List.nCount;

        memset(&ufo, 0, sizeof(ufo));
        ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bRace = 5; ufo.bVisible = 1;
        ufo.nMissionSub = 4; ufo.nPhase = 2; ufo.nTimer = 0; ufo.nTargetIdx = 11;
        ufo.nX = 50; ufo.nY = 60;
        Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx);
        Check(g_List.nCount > nBefore, "subtype 4 at phase 2 puts an alien site on the list");
        pSite = (StrategItem *)PtrArray_Get(&g_List, g_List.nCount - 1);
        if (pSite != NULL) {
            CheckEqI32(pSite->nType, 0, "the new item is a site, nType 0");
            CheckEqI32(pSite->bRace, 5, "and it is stamped with the UFO's race");
            CheckEqI32(pSite->nTargetIdx, 11, "and with the UFO's target index");
        }
    }

    /* ---- gate 3, subtype 6: the base-contact screen */
    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
    ufo.nMissionSub = 6; ufo.nPhase = 2; ufo.nTimer = 0;
    g_nContactCalls = 0;
    Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx);
    CheckEqI32(g_nContactCalls, 1, "subtype 6 at phase 2 opens the base-contact screen");

    /* ---- gate 1, subtype 6: a base with no free kind-10 cell is accepted */
    memset(g_aGrid, 0, sizeof(g_aGrid));
    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
    ufo.nMissionSub = 6; ufo.nPhase = 4; ufo.nX = 10; ufo.nY = 10;
    Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx);
    CheckEqI32(ufo.nTargetX, 40, "the base's own coordinates become the target");
    CheckEqI32(ufo.nTargetY, 50, "in both axes");
    CheckEqI32(ufo.nTargetIdx, 0, "and the base index is remembered");

    /* A base that DOES have a free kind-10 cell is rejected, and with only one base in play the
     * search exhausts its 20 attempts and gives up before the gates. */
    memset(g_aGrid, 0, sizeof(g_aGrid));
    g_aGrid[480] = 10;   /* base 0, cell (0,0), kind 10, build days zero: free */
    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
    ufo.nMissionSub = 6; ufo.nPhase = 4; ufo.nTimer = 0; ufo.nX = 10; ufo.nY = 10;
    CheckEqI32(Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx), 4,
               "exhausting the base search returns the phase unchanged");
    CheckEqI32(ufo.nPhase, 4, "and the gates never run");
    CheckEqI32(ufo.nTimer, 0, "so no dwell was set");

    /* ---- gate 1, subtype 7: a random active site */
    {
        StrategItem *pAdded = StrategList_AddItem(&g_List, &g_Seed, 0, 0, 1, 0,
                                                  777, 321, 4242, 0);
        (void)pAdded;
        memset(&ufo, 0, sizeof(ufo));
        ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
        ufo.nMissionSub = 7; ufo.nPhase = 4; ufo.nX = 10; ufo.nY = 10;
        /* Isolate this two-candidate choice from the preceding base-retry RNG stream. */
        srand(6001u);
        Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx);
        CheckEqI32(ufo.nTargetX, 777, "subtype 7 targets an active type-0 item");
        CheckEqI32(ufo.nTargetY, 321, "and takes its coordinates");
    }

    /* ---- the shared tail */
    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 0;
    ufo.nMissionSub = 9;   /* not a subtype the machine knows */
    ufo.nPhase = 1; ufo.nTimer = 0;
    CheckEqI32(Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx), 9,
               "an unknown stored subtype returns the stored subtype, not nPhase");
    CheckEqI32(ufo.nPhase, 1, "and skips the decrement the shared tail would apply");
    CheckEqI32(ufo.bDone, 0, "so bDone is not set here");

    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 0;
    ufo.nMissionSub = 1; ufo.nPhase = 1; ufo.nTimer = 0;
    CheckEqI32(Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx), 1,
               "the tail returns nPhase from before the decrement");
    CheckEqI32(ufo.nPhase, 0, "and then drops it to 0");
    CheckEqI32(ufo.bDone, 1, "a finished mission nobody can see is marked done");

    memset(&ufo, 0, sizeof(ufo));
    ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
    ufo.nMissionSub = 1; ufo.nPhase = 1; ufo.nTimer = 0;
    Geoscape_SpawnMissionSite(&ufo, -1, &g_Ctx);
    CheckEqI32(ufo.bDone, 0, "a visible craft is never marked done");

    PtrArray_DeleteAll(&g_List);
}


static bool FindMissionBaseRetrySeed(u32 *pnSeed, i32 *pnCommittedBase,
                                    i32 *pnTargetX, i32 *pnTargetY)
{
    u32 nSeed;

    for (nSeed = 1; nSeed < 10000u; ++nSeed) {
        bool abSeen[PUFO_FAC_BASE_COUNT] = { false };
        i32 i;
        u16 nPick;
        i32 nRawX, nRawY, nOldX, nOldY;

        srand((unsigned)nSeed);
        for (i = 0; i < 20; ++i) {
            nPick = Game_RandU16((u16)PUFO_FAC_BASE_COUNT);
            if (nPick < PUFO_FAC_BASE_COUNT) {
                abSeen[nPick] = true;
            }
        }
        nPick = Game_RandU16((u16)PUFO_FAC_BASE_COUNT);
        if (nPick < PUFO_FAC_BASE_COUNT && !abSeen[nPick]) {
            srand((unsigned)nSeed);
            for (i = 0; i < 21; ++i) {
                (void)Game_RandU16((u16)PUFO_FAC_BASE_COUNT);
            }
            nRawX = (i32)Game_RandU16(0x2F8) + 20;
            nRawY = (i32)Game_RandU16(0x168) + 20;

            srand((unsigned)nSeed);
            for (i = 0; i < 20; ++i) {
                (void)Game_RandU16((u16)PUFO_FAC_BASE_COUNT);
            }
            nOldY = (i32)Game_RandU16(0x168) + 20;
            nOldX = (i32)Game_RandU16(0x2F8) + 20;
            if ((nRawX != nOldX || nRawY != nOldY) &&
                (nRawX != 100 + 10 * (i32)nPick || nRawY != 50 + 10 * (i32)nPick)) {
                *pnSeed = nSeed;
                *pnCommittedBase = (i32)nPick;
                *pnTargetX = nRawX;
                *pnTargetY = nRawY;
                return true;
            }
        }
    }
    return false;
}

static bool FindXFirstTravelSeed(i32 nRawPrefixDraws, u16 nRawPrefixBound,
                                 i32 nOldPrefixDraws, u16 nOldPrefixBound,
                                 u32 *pnSeed, i32 *pnX, i32 *pnY)
{
    u32 nSeed;

    for (nSeed = 1; nSeed < 10000u; ++nSeed) {
        i32 i;
        i32 nRawX, nRawY, nOldX, nOldY;

        srand((unsigned)nSeed);
        for (i = 0; i < nRawPrefixDraws; ++i) {
            (void)Game_RandU16(nRawPrefixBound);
        }
        nRawX = (i32)Game_RandU16(0x2F8) + 20;
        nRawY = (i32)Game_RandU16(0x168) + 20;

        srand((unsigned)nSeed);
        for (i = 0; i < nOldPrefixDraws; ++i) {
            (void)Game_RandU16(nOldPrefixBound);
        }
        nOldY = (i32)Game_RandU16(0x168) + 20;
        nOldX = (i32)Game_RandU16(0x2F8) + 20;
        if (nRawX != nOldX || nRawY != nOldY) {
            *pnSeed = nSeed;
            *pnX = nRawX;
            *pnY = nRawY;
            return true;
        }
    }
    return false;
}

static void TestMissionTravelRawFallbacks(void)
{
    static PadImage map;
    static u8 aTiles[400];
    static SmapView view;
    static PtrArray list;
    static i32 nStrategSeed;
    static BaseRec aBases[PUFO_FAC_BASE_COUNT];
    static BaseRec *apBases[PUFO_FAC_BASE_COUNT];
    static MissionSiteRec aSites[PUFO_MISSION_SITE_COUNT];
    static u8 abFacilityGrid[480 + 144 * PUFO_FAC_BASE_COUNT];
    static GameTime time;
    static GeoscapeWorld world;
    static SpawnMissionCtx ctx;
    StrategItem ufo;
    u32 nSeed = 0;
    i32 nCommittedBase = -1;
    i32 nWantX = -1;
    i32 nWantY = -1;
    i32 i;

    printf("Mission travel raw fallback paths\n");

    memset(aTiles, 1, sizeof(aTiles));
    memset(&map, 0, sizeof(map));
    map.uWidth = 20;
    map.pData = aTiles;
    memset(&view, 0, sizeof(view));
    view.nMapW = 800;
    view.nMapH = 400;
    memset(&time, 0, sizeof(time));
    memset(&world, 0, sizeof(world));
    world.pTime = &time;
    world.nTickSeconds = 60;
    PtrArray_ctor(&list);

    memset(&ctx, 0, sizeof(ctx));
    ctx.pList = &list;
    ctx.pnSeed = &nStrategSeed;
    ctx.pView = &view;
    ctx.pTilemap = &map;
    ctx.apBases = apBases;
    ctx.apSites = aSites;
    ctx.nSiteCount = PUFO_MISSION_SITE_COUNT;
    ctx.abFacilityGrid = abFacilityGrid;
    ctx.pWorld = &world;

    for (i = 0; i < PUFO_FAC_BASE_COUNT; ++i) {
        apBases[i] = &aBases[i];
        aBases[i].nUnk8 = 100 + 10 * i;
        aBases[i].nUnk12 = 50 + 10 * i;
    }

    /* The first 20 base picks have a matching kind-10 cell with status zero;
     * pick 21 names the only base without one. Raw commits it, then falls back
     * before checking that base. This stays within the 8-base array. */
    Check(FindMissionBaseRetrySeed(&nSeed, &nCommittedBase, &nWantX, &nWantY),
          "find a seed whose 21st untested base is unique and whose fallback stream differs");
    if (nCommittedBase >= 0 && nCommittedBase < PUFO_FAC_BASE_COUNT) {
        memset(abFacilityGrid, 0, sizeof(abFacilityGrid));
        for (i = 0; i < PUFO_FAC_BASE_COUNT; ++i) {
            abFacilityGrid[480 + 144 * i] = 10;
        }
        abFacilityGrid[480 + 144 * nCommittedBase] = 0;

        memset(&ufo, 0, sizeof(ufo));
        ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
        ufo.nMissionSub = 6; ufo.nPhase = 4; ufo.nTargetIdx = -1;
        ufo.nX = 10; ufo.nY = 12;
        ctx.nBaseCount = PUFO_FAC_BASE_COUNT;
        srand((unsigned)nSeed);
        Geoscape_SpawnMissionSite(&ufo, -1, &ctx);
        CheckEqI32(ufo.nTargetIdx, nCommittedBase,
                   "candidate 21 is committed before the raw fallback branch");
        CheckEqI32(ufo.nTargetX, nWantX,
                   "fallback X replaces candidate 21 after its cell check is skipped");
        CheckEqI32(ufo.nTargetY, nWantY,
                   "fallback Y replaces candidate 21 after its cell check is skipped");
        CheckEqI32(ufo.nDestX, nWantX, "candidate 21's fallback X is the travel endpoint");
        CheckEqI32(ufo.nDestY, nWantY, "candidate 21's fallback Y is the travel endpoint");
        CheckEqI32(ufo.nStartX, 10, "the fallback keeps the current start X");
        CheckEqI32(ufo.nStartY, 12, "the fallback keeps the current start Y");
        CheckEqI32(ufo.nX, 10, "the fallback does not copy start X into current X");
        CheckEqI32(ufo.nY, 12, "the fallback does not copy start Y into current Y");
        CheckEqI32(ufo.nMissionSub, 0, "the fallback passes subtype byte 0");
        CheckEqI32(ufo.nPhase, 4, "the base give-up returns before the phase gates");
        CheckEqI32(ufo.nTimer, 0, "the base give-up keeps the zero timer");
        CheckEqI32(ufo.bRace, 0, "the base give-up leaves race unchanged");
        CheckEqI32(ufo.nDisplayId, 0, "the base give-up leaves display id unchanged");
    }

    /* One base always returns nonzero. The raw fallback consumes 21 bound-1 picks,
     * then draws X before Y and stores both target fields before travel init. */
    memset(abFacilityGrid, 0, sizeof(abFacilityGrid));
    abFacilityGrid[480] = 10;
    Check(FindXFirstTravelSeed(21, 1, 20, 1, &nSeed, &nWantX, &nWantY),
          "find a base fallback seed whose X/Y result differs from Y/X");
    if (nWantX >= 20 && nWantX < 20 + 0x2F8 && nWantY >= 20 && nWantY < 20 + 0x168) {
        memset(&ufo, 0, sizeof(ufo));
        ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
        ufo.nMissionSub = 6; ufo.nPhase = 4; ufo.nTargetIdx = -1;
        ufo.nX = 10; ufo.nY = 12; ufo.nHp = 73; ufo.bRace = 9;
        ctx.nBaseCount = 1;
        srand((unsigned)nSeed);
        Geoscape_SpawnMissionSite(&ufo, -1, &ctx);
        CheckEqI32(ufo.nTargetX, nWantX, "subtype 6 fallback saves the raw X draw");
        CheckEqI32(ufo.nTargetY, nWantY, "subtype 6 fallback saves the next Y draw");
        CheckEqI32(ufo.nDestX, nWantX, "subtype 6 fallback uses X as the travel endpoint");
        CheckEqI32(ufo.nDestY, nWantY, "subtype 6 fallback uses Y as the travel endpoint");
        CheckEqI32(ufo.nStartX, 10, "subtype 6 fallback keeps the current start X");
        CheckEqI32(ufo.nStartY, 12, "subtype 6 fallback keeps the current start Y");
        CheckEqI32(ufo.nX, 10, "subtype 6 fallback does not copy start X into current X");
        CheckEqI32(ufo.nY, 12, "subtype 6 fallback does not copy start Y into current Y");
        CheckEqI32(ufo.nMissionSub, 0, "raw fallback passes subtype byte 0");
        CheckEqI32(ufo.nPhase, 4, "subtype 6 give-up returns before the phase gates");
        CheckEqI32(ufo.nTimer, 0, "subtype 6 fallback keeps the zero timer");
        CheckEqI32(ufo.nTargetIdx, 0, "subtype 6 fallback keeps the last base index");
        CheckEqI32(ufo.bFlagA, 0, "subtype 6 fallback keeps the zero flag argument");
        CheckEqI32(ufo.nHp, 73, "subtype 6 fallback leaves HP unchanged");
        CheckEqI32(ufo.bRace, 9, "subtype 6 fallback leaves race unchanged");
        CheckEqI32(ufo.bVisible, 1, "subtype 6 fallback preserves visibility");
    }

    /* With no active type-0 sites, subtype 7 takes the other X-first path without prefix draws. */
    Check(FindXFirstTravelSeed(0, 1, 0, 1, &nSeed, &nWantX, &nWantY),
          "find a no-site fallback seed whose X/Y result differs from Y/X");
    if (nWantX >= 20 && nWantX < 20 + 0x2F8 && nWantY >= 20 && nWantY < 20 + 0x168) {
        memset(&ufo, 0, sizeof(ufo));
        ufo.nType = 1; ufo.bActive = 1; ufo.bCraftClass = 2; ufo.bVisible = 1;
        ufo.nMissionSub = 7; ufo.nPhase = 4; ufo.nTargetIdx = -1;
        ufo.nX = 10; ufo.nY = 12; ufo.nHp = 81; ufo.bRace = 11;
        srand((unsigned)nSeed);
        Geoscape_SpawnMissionSite(&ufo, -1, &ctx);
        CheckEqI32(ufo.nTargetX, nWantX, "subtype 7 no-site fallback saves the raw X draw");
        CheckEqI32(ufo.nTargetY, nWantY, "subtype 7 no-site fallback saves the next Y draw");
        CheckEqI32(ufo.nDestX, nWantX, "subtype 7 no-site fallback uses X as the travel endpoint");
        CheckEqI32(ufo.nDestY, nWantY, "subtype 7 no-site fallback uses Y as the travel endpoint");
        CheckEqI32(ufo.nStartX, 10, "subtype 7 fallback keeps the current start X");
        CheckEqI32(ufo.nStartY, 12, "subtype 7 fallback keeps the current start Y");
        CheckEqI32(ufo.nX, 10, "subtype 7 fallback does not copy start X into current X");
        CheckEqI32(ufo.nY, 12, "subtype 7 fallback does not copy start Y into current Y");
        CheckEqI32(ufo.nMissionSub, 0, "raw fallback passes subtype byte 0");
        CheckEqI32(ufo.nPhase, 3, "subtype 7 runs the shared phase decrement");
        CheckEqI32(ufo.nTimer, 0, "subtype 7 fallback keeps the zero timer");
        CheckEqI32(ufo.nTargetIdx, -1, "no-site fallback leaves target index unchanged");
        CheckEqI32(ufo.bFlagA, 0, "subtype 7 fallback keeps the zero flag argument");
        CheckEqI32(ufo.nHp, 81, "subtype 7 fallback leaves HP unchanged");
        CheckEqI32(ufo.bRace, 11, "subtype 7 fallback leaves race unchanged");
        CheckEqI32(ufo.bVisible, 1, "subtype 7 fallback preserves visibility");
    }

    PtrArray_DeleteAll(&list);
    Game_SeedRngFixed();
}

static void TestFindByStrategIdx(void)
{
    StrategList list;
    GeoscapeWorld world;

    printf("Ship_FindIndexByStrategType\n");

    SetupWorld(&world, &list, 3, 3);
    memset(g_aItems, 0, sizeof(g_aItems));
    g_aItems[0].nDisplayId = 7;
    g_aItems[1].nDisplayId = 9;
    g_aItems[2].nDisplayId = STRATEG_ID_UNASSIGNED;
    g_aShips[0].bId = 1;
    g_aShips[1].bId = 9;
    g_aShips[2].bId = 7;

    CheckEqI32(Ship_FindIndexByStrategType(&world, &list, 0), 2,
               "the item's display id picks the craft with that bId, wherever it sits");
    CheckEqI32(Ship_FindIndexByStrategType(&world, &list, 1), 1, "and the second item picks its own craft");
    CheckEqI32(Ship_FindIndexByStrategType(&world, &list, 2), -1, "an unassigned display id matches no craft");
    CheckEqI32(Ship_FindIndexByStrategType(&world, &list, 3), -1,
               "a strateg index past the list is refused, where the original faults");
    CheckEqI32(Ship_FindIndexByStrategType(&world, &list, -1), -1, "and so is a negative one");

    /* Two craft sharing a bId: the first one in array order wins, as in the original loop. */
    g_aShips[0].bId = 7;
    CheckEqI32(Ship_FindIndexByStrategType(&world, &list, 0), 0,
               "the first craft in array order wins when two share a bId");

    world.nShipCount = 0;
    CheckEqI32(Ship_FindIndexByStrategType(&world, &list, 0), -1,
               "no craft means no answer, checked before the item is read");
}

static void TestOverlayAtMode(void)
{
    static u8 aBytes[3][12];
    static PadImage aSet[3];
    static const PadImage *aOverlay[3];
    SmapView view;

    printf("Smap_GetOverlayAtMode\n");

    for (int n = 0; n < 3; ++n) {
        for (int i = 0; i < 12; ++i) {
            aBytes[n][i] = (u8)(n * 10 + i);
        }
        memset(&aSet[n], 0, sizeof(aSet[n]));
        aSet[n].pData = aBytes[n];
        aSet[n].uWidth = 4;
        aOverlay[n] = &aSet[n]; /* the three globals hold pointers, not structs */
    }

    memset(&view, 0, sizeof(view));

    view.nMode = 0;
    CheckEqI32(Smap_GetOverlayAtMode(&view, aOverlay, 0, 2, 1), 6,
               "mode 0 reads the first overlay, stride is the PadImage's own uWidth");
    view.nMode = 1;
    CheckEqI32(Smap_GetOverlayAtMode(&view, aOverlay, 0, 2, 1), 16,
               "mode 1 reads a different PadImage");
    view.nMode = 2;
    CheckEqI32(Smap_GetOverlayAtMode(&view, aOverlay, 0, 2, 1), 26, "and mode 2 a third");

    /* The x coordinate is offset by the origin before the lookup. */
    view.nMode = 0;
    CheckEqI32(Smap_GetOverlayAtMode(&view, aOverlay, 2, 4, 1), 6,
               "x is reduced by the origin, so the same byte is reached from a shifted x");
    CheckEqI32(Smap_GetOverlayAtMode(&view, aOverlay, 1, 4, 1), 7,
               "and a different origin reaches a different byte");

    view.nMode = 3;
    CheckEqI32(Smap_GetOverlayAtMode(&view, aOverlay, 0, 0, 0), 0,
               "a mode outside 0..2 returns 0, where the original returns an uninitialised byte");
    view.nMode = -1;
    CheckEqI32(Smap_GetOverlayAtMode(&view, aOverlay, 0, 0, 0), 0, "and the same for a negative mode");
}

static void TestDistWrapped(void)
{
    printf("Smap_DistWrapped @ 0x7df80\n");

    CheckEqI32(Smap_DistWrapped(0, 0, 0, 0), 0, "the same point is zero");
    CheckEqI32(Smap_DistWrapped(0, 0, 300, 0), 15000, "300 columns, scaled by 50");
    CheckEqI32(Smap_DistWrapped(0, 0, 700, 0), 5000,
               "700 east is the 100 west the wrap finds");
    CheckEqI32(Smap_DistWrapped(750, 0, 50, 0), 5000, "and the wrap runs the other way too");
    CheckEqI32(Smap_DistWrapped(0, 0, 800, 0), 0, "a delta of exactly 800 wraps to zero");
    CheckEqI32(Smap_DistWrapped(0, 0, 400, 0), 20000,
               "400 is the plain leg, so the min keeps it");
    CheckEqI32(Smap_DistWrapped(0, 0, 0, 400), 20000,
               "y is plain: the map wraps sideways only");
    CheckEqI32(Smap_DistWrapped(0, 0, 300, 400), 25000, "a 3-4-5 triangle scales by 50");
    CheckEqI32(Smap_DistWrapped(-100, -40, 100, 20), 10440,
               "wrapped x is 200, plain y is 60, sqrt(43600) * 50 truncates to 10440");
    CheckEqI32(Smap_DistWrapped(0, 0, 1, 1), 70, "sqrt(2) * 50 truncates toward zero");
}

/* The same five divisors the IDB's _muld operands encode; see g_anCraftRangeDivisor. */
static const i32 g_anRangeDivisor[5] = { 30000, 40000, 64000, 12000, 32000 };

static void TestCraftRangeGate(void)
{
    printf("Smap_CraftRangeExhausted @ 0x7e074\n");

    for (i32 i = 0; i < PUFO_FAC_BASE_COUNT; ++i) {
        g_apBases[i] = &g_aBases[i];
    }

    GeoscapeWorld world;
    StrategList list;
    SmapCraftRangeEnv env = { &world, g_apBases, &list };

    /* Craft class 3 divides uRangeMax by 12000, so 1000 makes the factor 1/12. uFuelCap 3000
     * gives a burn of 50, and 50 / 12 truncates to 4. The item at 300 columns is 15000 range
     * units, a factor of 1/12 makes that 1250. */
    SetupWorld(&world, &list, 2, 1);
    memset(g_aBases, 0, sizeof(g_aBases));
    g_aBases[0].nUnk8 = 0;
    g_aBases[0].nUnk12 = 0;
    g_aItems[0].nType = 1; g_aItems[0].bActive = 0; g_aItems[0].nDisplayId = 7;
    g_aItems[0].nX = 300; g_aItems[0].nY = 0;
    g_aItems[1].nType = 1; g_aItems[1].bActive = 1; /* a UFO: FindByShipId ignores it */
    g_aShips[0].bId = 7; g_aShips[0].bCraftType = 3; g_aShips[0].bHomeBase = 0;
    g_aShips[0].uFuelCap = 3000; g_aShips[0].uRangeMax = 1000; g_aShips[0].uRangeRemain = 10;

    Check(Smap_CraftRangeExhausted(&env, 0), "1250 range units is past the 6 left");
    CheckEqI32(g_aShips[0].uRangeRemain, 6, "the burn of 4 is taken before the test runs");
    Check(Smap_CraftRangeExhausted(&env, 0), "the gate runs again on the next minute");
    CheckEqI32(g_aShips[0].uRangeRemain, 2, "and burns again");
    Check(Smap_CraftRangeExhausted(&env, 0), "still out of range");
    CheckEqI32(g_aShips[0].uRangeRemain, 2,
               "a budget already below the burn is left alone, so the decay bottoms out");

    /* The drain is not tied to the answer. 100 columns is 5000 range units, a factor of 1/12
     * makes that 416, and 1000 remaining covers it -- but the burn still runs. */
    g_aItems[0].nX = 100;
    g_aShips[0].uRangeRemain = 1000;
    Check(!Smap_CraftRangeExhausted(&env, 0), "416 range units is inside the 1000 remaining");
    CheckEqI32(g_aShips[0].uRangeRemain, 996, "and the burn was taken anyway");

    /* With uRangeMax equal to the class divisor the factor is exactly 1, so the gate is just
     * dist * 50 >= uRangeRemain. 20 columns is 1000 range units, 19 is 950. uFuelCap 0 keeps the
     * burn at zero so the budget stays readable between the two checks. */
    for (i32 nClass = 0; nClass < 5; ++nClass) {
        SetupWorld(&world, &list, 1, 1);
        memset(g_aBases, 0, sizeof(g_aBases));
        g_aItems[0].nType = 1; g_aItems[0].bActive = 0; g_aItems[0].nDisplayId = 7;
        g_aItems[0].nX = 20; g_aItems[0].nY = 0;
        g_aShips[0].bId = 7; g_aShips[0].bCraftType = (u8)nClass; g_aShips[0].bHomeBase = 0;
        g_aShips[0].uFuelCap = 0;
        g_aShips[0].uRangeMax = (u16)g_anRangeDivisor[nClass];
        g_aShips[0].uRangeRemain = 1000;

        Check(Smap_CraftRangeExhausted(&env, 0),
              "class N with uRangeMax == N: 1000 range units meets 1000 remaining");
        g_aItems[0].nX = 19;
        Check(!Smap_CraftRangeExhausted(&env, 0), "and 950 does not");
        CheckEqI32(g_aShips[0].uRangeRemain, 1000, "a zero burn leaves the budget intact");
    }

    /* The three refusals the original does not make. */
    g_aItems[0].nDisplayId = 11;
    Check(!Smap_CraftRangeExhausted(&env, 0),
          "a craft with no strateg item is refused, where PtrArray_Get gets -1");
    g_aItems[0].nDisplayId = 7;
    g_aShips[0].bCraftType = 5;
    Check(!Smap_CraftRangeExhausted(&env, 0),
          "craft class 5 is refused, where the multiplier pair is stack residue");
    g_aShips[0].bCraftType = 3;
    g_aShips[0].bHomeBase = 0xFF;
    Check(!Smap_CraftRangeExhausted(&env, 0),
          "a home base outside the base array is refused, where the raw u8 would index it");
    g_aShips[0].bHomeBase = 0;
    Check(!Smap_CraftRangeExhausted(&env, 9), "an out-of-range ship index is refused");

    g_apBases[0] = NULL;
    Check(!Smap_CraftRangeExhausted(&env, 0), "a NULL base slot is refused");
    g_apBases[0] = &g_aBases[0];
}

/* ------------------------------------------------- the geoscape event pass
 *
 * StrategList_ProcessEvents @ 0x7fcb0 and Strateg_ConfirmInterceptTarget @ 0x7fb98.
 *
 * The pinned oddities: _rt_udiv is called divisor-first, so every leg test is an UNSIGNED
 * division of the elapsed seconds by 60; the x leg is the wrapped 800 minimum WITH its winner
 * kept, so a target 500 columns east is reached 300 columns west; the arrival dispatch is chosen
 * by the TARGET's type and re-reads it from the list for every test; a craft-to-craft arrival with
 * the target timer already spent ends the whole pass; and the interpolation re-derives the
 * position from the clock instead of stepping it. */

static UiLineList g_Lines;
static UiPanel g_Panel;
static u8 g_aLine0[PUFO_PANEL_LINE_BYTES];
static Soldier g_aSol[2];
static Soldier *g_apSol[2];
static i32 g_nFund, g_nStratIdx, g_nSelShip, g_nIntShip, g_nIntStrat, g_nPulse;
static u8 g_bOnce, g_bSelFlag;
static i32 g_nSample, g_nSampleCalls;
static char *g_pTexts;

static void Hook_Sample(i32 nSample, void *pUser)
{
    (void)pUser;
    ++g_nSampleCalls;
    g_nSample = nSample;
}

/* The TEXTS table on its 205-byte stride, with only the three arrival captions filled. */
static const char *TextsRec(i32 nRec)
{
    if (g_pTexts == NULL) {
        g_pTexts = (char *)calloc(1148 * PUFO_TEXT_REC_STRIDE, 1);
        /* The three captions as lang.dat TEXTS.DAT holds them (cp1251 in the file). The fixture
         * carries them in the source's own encoding; what the checks pin is which record each
         * arrival prefixes its caption with, not the byte encoding. */
        snprintf(g_pTexts + PUFO_TEXT_REC_STRIDE * 1145, PUFO_TEXT_REC_STRIDE, "%s", "База пришельцев");
        snprintf(g_pTexts + PUFO_TEXT_REC_STRIDE * 1146, PUFO_TEXT_REC_STRIDE, "%s", "Район террора");
        snprintf(g_pTexts + PUFO_TEXT_REC_STRIDE * 1147, PUFO_TEXT_REC_STRIDE, "%s", "НЛО");
    }
    return g_pTexts + PUFO_TEXT_REC_STRIDE * nRec;
}

static void CheckCaption(i32 nRec, i32 nNumber, const char *pszWhat)
{
    char szWant[128];
    snprintf(szWant, sizeof(szWant), "%s%d", TextsRec(nRec), (int)nNumber);
    Check(strcmp((const char *)g_Panel.apLines[0], szWant) == 0, pszWhat);
    printf("       caption: %s\n", (const char *)g_Panel.apLines[0]);
}

static PtrArray g_EvList;
static Ship g_aEvShip[2];
static Ship *g_apEvShip[2];
static GeoscapeWorld g_EvWorld;
static GameTime g_EvTime;
static StrategSpawnCtx g_EvCtx;
static SpawnMissionCtx g_EvMission;
static SmapView g_EvView;
static PadImage g_EvMap;
static u8 g_aEvTiles[400];
static MissionSiteRec g_aEvSites[2];
static i32 g_EvSeed, g_EvRadarPulse;
static i32 g_EvMonth;  /* unk_159648: the roll's row index, read at 0x81AB4 */
static u16 g_EvAddend; /* 0x1472C8: the roll's early gate, read at 0x81B7C */
static GeoscapeWorld g_EvMissionWorld;
static StrategEventsEnv g_EvEnv;

static void EvReset(void)
{
    i32 i;

    PtrArray_ctor(&g_EvList);
    for (i = 0; i < 2; ++i) {
        memset(&g_aEvShip[i], 0, sizeof(g_aEvShip[i]));
        g_aEvShip[i].bId = (u8)(10 + i);
        g_apEvShip[i] = &g_aEvShip[i];
    }
    memset(&g_EvWorld, 0, sizeof(g_EvWorld));
    g_EvWorld.apCraft = g_apEvShip;
    g_EvWorld.nShipCount = 2;
    g_EvWorld.pTime = &g_EvTime;

    memset(&g_EvTime, 0, sizeof(g_EvTime));
    memset(&g_Lines, 0, sizeof(g_Lines));
    memset(&g_Panel, 0, sizeof(g_Panel));
    memset(g_aLine0, 0, sizeof(g_aLine0));
    g_Panel.apLines[0] = g_aLine0;
    g_Panel.anLineAux[0] = -1;
    g_Panel.nUnk3032 = -1;

    memset(g_aSol, 0, sizeof(g_aSol));
    for (i = 0; i < 2; ++i) {
        g_aSol[i].bUnk183 = 255;
        g_apSol[i] = &g_aSol[i];
    }

    g_nFund = 0;
    g_nStratIdx = -1;
    g_nSelShip = -1;
    g_nIntShip = -1;
    g_nIntStrat = -1;
    g_nPulse = -1;
    g_bOnce = 0;
    g_bSelFlag = 0;
    g_nSample = -1;

    /* The two roll cells the fixture hands the spawn context. Month 0 is weight row 0 and an addend
     * below 15 keeps 0x81B7C's class cap on, which is the state a new game is in (Game_StartNew
     * writes both to 0 at 0x946F8 and 0x95414 before its own spawn pass at 0x9542C). */
    g_EvMonth = 0;
    g_EvAddend = 0;
    memset(&g_EvCtx, 0, sizeof(g_EvCtx));
    g_EvCtx.pTables = &g_Roll;
    g_EvCtx.pnDifficulty = &g_EvMonth;
    g_EvCtx.pnEarlyGate = &g_EvAddend;
    g_EvCtx.pnSeed = &g_EvSeed;
    g_EvCtx.pfnSpawnMission = SpawnProbe;
    g_EvCtx.pfnProcessEvents = EventProbe;

    memset(&g_EvView, 0, sizeof(g_EvView));
    g_EvView.nMapW = 800;
    g_EvView.nMapH = 400;
    memset(g_aEvTiles, 1, sizeof(g_aEvTiles));
    memset(&g_EvMap, 0, sizeof(g_EvMap));
    g_EvMap.uWidth = 20;
    g_EvMap.pData = g_aEvTiles;
    memset(g_aEvSites, 0, sizeof(g_aEvSites));
    g_aEvSites[0].x = 100;
    g_aEvSites[0].y = 100;
    memset(&g_EvMissionWorld, 0, sizeof(g_EvMissionWorld));
    g_EvMissionWorld.pTime = &g_EvTime;
    memset(&g_EvMission, 0, sizeof(g_EvMission));
    g_EvMission.pList = &g_EvList;
    g_EvMission.pnSeed = &g_EvSeed;
    g_EvMission.pView = &g_EvView;
    g_EvMission.pTilemap = &g_EvMap;
    g_EvMission.apSites = g_aEvSites;
    g_EvMission.nSiteCount = 1;
    g_EvMission.pnRadarPulseTimer = &g_EvRadarPulse;
    g_EvMission.pWorld = &g_EvMissionWorld;

    memset(&g_EvEnv, 0, sizeof(g_EvEnv));
    g_EvEnv.pList = &g_EvList;
    g_EvEnv.pWorld = &g_EvWorld;
    g_EvEnv.pTime = &g_EvTime;
    g_EvEnv.pLines = &g_Lines;
    g_EvEnv.pStatusPanel = &g_Panel;
    g_EvEnv.pnFundPending = &g_nFund;
    g_EvEnv.pbSpawnOnce = &g_bOnce;
    g_EvEnv.pbSelectionFlag = &g_bSelFlag;
    g_EvEnv.pnMsgShipIdx = &g_nStratIdx;
    g_EvEnv.pnSelectedShipIdx = &g_nSelShip;
    g_EvEnv.pnInterceptShipIdx = &g_nIntShip;
    g_EvEnv.pnInterceptStrategIdx = &g_nIntStrat;
    g_EvEnv.pnRadarPulseTimer = &g_nPulse;
    g_EvEnv.pTextRecs = TextsRec(0);
    g_EvEnv.apSoldiers = g_apSol;
    g_EvEnv.nSoldierCount = 2;
    g_EvEnv.pSpawnCtx = &g_EvCtx;
    g_EvEnv.pMissionCtx = &g_EvMission;
    g_EvEnv.pfnPlaySample = Hook_Sample;
}

/* StrategItem_ctor writes nSeed and nothing else (0x82BDC), so the fixture zeroes each item
 * before setting it up -- otherwise the fields the pass reads arrive indeterminate. */
static StrategItem *EvAdd(i32 nType)
{
    StrategItem *pItem;

    (void)StrategItem_AllocAdd(&g_EvList, g_EvSeed++);
    pItem = (StrategItem *)g_EvList.apItems[g_EvList.nCount - 1];
    if (pItem != NULL) {
        memset(pItem, 0, sizeof(*pItem));
        pItem->nType = (u8)nType;
    }
    return pItem;
}

static void EvFree(void)
{
    PtrArray_DeleteAll(&g_EvList);
}

static i32 RunSmapBaseAssaultCountCase(i32 nSnapshotCount, i32 nCountAfterWire,
                                      bool bUseLiveCount, i32 *pnRet)
{
    u8 abGrid[PUFO_GRID_SIZE] = {0};
    u8 abTileTypes[36] = {0};
    u8 bMissionMode = 1;
    u8 bUnk1511E = 0, bSpawnAlt = 0, bUnk15115 = 0;
    u8 bMissionType = 0, bTerrainVariant = 0, bMapLayers = 0, bCraftIndex = 0;
    u8 bTickHold = 0, bBattleEnded = 0, bHomeBase = 0, bBaseRace = 0;
    u8 bAssault = 0, bFlag187084 = 0, bGeoscapeFlag = 0;
    u8 nBaseCount = 1, bSelectedBase = 0, nCraftCount = 0;
    u8 abSpriteSet[4] = {0};
    i32 nTierRaw = 0, nFunding = 100000, nLiveSoldierCount = 0;
    i32 nSelectedShip = -1, nMsgShip = -1, nSelectedStrateg = -1;
    SmapCraftSelectEnv env = {0};

    EvReset();
    memset(g_aBases, 0, sizeof(g_aBases));
    g_apBases[0] = &g_aBases[0];
    g_aSol[0].nKind = 0;        /* counted as occupied base space */
    g_aSol[0].bUnk183 = 254;    /* away in a craft, so not available for base assault */

    env.apCraft = g_apEvShip;
    env.pnCraftCount = &nCraftCount;
    env.pbMissionMode = &bMissionMode;
    env.pbUnk1511E = &bUnk1511E;
    env.pnMissionType = &bMissionType;
    env.abSpriteSet = abSpriteSet;
    env.pbSpawnAlt = &bSpawnAlt;
    env.pbUnk15115 = &bUnk15115;
    env.pnTerrainVariant = &bTerrainVariant;
    env.pnMapLayers = &bMapLayers;
    env.pnTierRaw = &nTierRaw;
    env.pnCraftIndex = &bCraftIndex;
    env.pbTickHold = &bTickHold;
    env.pbBattleEnded = &bBattleEnded;
    env.apBases = g_apBases;
    env.pbHomeBase = (i8 *)&bHomeBase;
    env.pbBaseRace = (i8 *)&bBaseRace;
    env.pStrategList = &g_EvList;
    env.pLines = &g_Lines;
    env.pTime = &g_EvTime;
    env.pnSelectedShipIdx = &nSelectedShip;
    env.pnMsgShipIdx = &nMsgShip;
    env.pnSelectedStrategIdx = &nSelectedStrateg;
    env.pbAssaultFlag = &bAssault;
    env.pbSelectionFlag = &g_bSelFlag;
    env.pbFlag187084 = &bFlag187084;
    env.pbGeoscapeFlag = &bGeoscapeFlag;
    env.pnRadarPulseTimer = &g_nPulse;
    env.pnFunding = &nFunding;
    env.pbBaseCount = &nBaseCount;
    env.pbSelectedBaseIdx = &bSelectedBase;
    env.abFacilityGrid = abGrid;
    env.abTileTypes = abTileTypes;
    env.apSoldiers = g_apSol;
    env.nSoldierCount = nSnapshotCount;
    env.pnSoldierCountLive = bUseLiveCount ? &nLiveSoldierCount : NULL;
    env.nObjectCount = 0;
    env.pView = &g_EvView;
    env.pShipEnv = NULL;
    env.pbSpawnOnceFlag = &g_bOnce;
    env.pSpawnCtx = &g_EvCtx;
    env.pDemolishEnv = NULL;
    env.pfnFreeGeoscapeUi = NULL;
    env.pfnInitMission = NULL;
    env.pfnInitEndScreen = NULL;
    env.pHookUser = NULL;

    if (bUseLiveCount) {
        nLiveSoldierCount = nCountAfterWire; /* mutate the owner after the env has been filled */
    }
    *pnRet = Smap_BeginCraftSelect(&env);
    return nFunding;
}

static void TestSmapCraftSelectLiveSoldierCount(void)
{
    i32 nRetLive = -1, nRetFallback = -1;
    const i32 nFundingLive = RunSmapBaseAssaultCountCase(0, 1, true, &nRetLive);
    const i32 nFundingFallback = RunSmapBaseAssaultCountCase(1, 1, false, &nRetFallback);

    printf("Smap_BeginCraftSelect live soldier count\n");
    CheckEqI32(nRetLive, 1, "the live-count case leaves one base after the assault setup");
    CheckEqI32(nFundingLive, 99170, "the live soldier owner adds 30 to the base-assault cost");
    CheckEqI32(nRetFallback, 1, "a NULL live pointer uses the fixture count fallback");
    CheckEqI32(nFundingFallback, nFundingLive, "the fallback matches the live count when values agree");
}

static void RecordArrivalLootNotice(i32 nFlag, i32 nBase, i32 nObject, i32 nUnused,
                                     void *pUser)
{
    i32 *anNotice = (i32 *)pUser;
    (void)nUnused;
    anNotice[0] += 1;
    anNotice[1] = nFlag;
    anNotice[2] = nBase;
    anNotice[3] = nObject;
}

/* Send the craft home through the real flag-27 handler, then let its scheduled
 * leg reach the production event and loot functions. World callback construction
 * is outside this game-layer fixture. */
static void TestArrivalDeliversLootOnce(void)
{
    static ObjectRecFull objects[59];
    ObjectRecFull *apObjects[59];
    BaseRec bases[3] = {0};
    BaseRec *apBases[3] = { &bases[0], &bases[1], &bases[2] };
    u8 grid[1524];
    u32 dwRefresh = 0x12340000u;
    i32 anNotice[4] = {0};
    i32 nSoldierCount = 2;
    u8 bInterceptDone = 1;
    UiControl aSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT] = {0};
    UiControl *apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT];
    ShipCtorEnv shipCtor = {0};
    ShipRemovalEnv shipEnv = {0};
    SmapResolveInterceptEnv resolveEnv = {0};
    TacticalLootEnv loot = {0};
    Ship *pShip;
    StrategItem *pOwn;
    StrategItem *pMissionTarget;

    printf("flag-27 home order lands and delivers cargo once\n");
    EvReset();
    g_EvWorld.nShipCount = 1;
    memset(objects, 0, sizeof(objects));
    memset(grid, 255, sizeof(grid));
    memset(grid, 0, 36 * 8);
    for (i32 i = 0; i < 59; ++i) {
        apObjects[i] = &objects[i];
    }
    for (i32 i = 0; i < 3; ++i) {
        bases[i].bSlot = (u8)i;
        memset(grid + 444 + 144 * i, 0, 36);
    }
    grid[480 + 144 * 2] = 11;
    objects[56].auPerBaseVal[2] = 7;
    bases[2].nUnk8 = 100;
    bases[2].nUnk12 = 100;

    shipCtor.nShipCount = 0;
    shipCtor.apObjects = apObjects;
    shipCtor.nObjectCount = 59;
    pShip = &g_aEvShip[0];
    Check(Ship_Ctor(pShip, 1, 10, 2, 0, &shipCtor) == pShip,
          "the returning interceptor has valid type-1 fuel data");
    CheckEqI32(pShip->uFuelCap, 3100, "the type-1 constructor supplies the return speed input");
    pShip->bAtBase = 0;
    pShip->bWeaponsHot = 1;
    pShip->bIntercepting = 1;
    pShip->bContactMsg = 1;
    pShip->bWeaponL = 25;
    pShip->bWeaponR = -1;
    pShip->anCargoQty[0] = 9;

    pOwn = EvAdd(1); /* own marker index 0; ship array index is also 0 */
    pOwn->bActive = 0;
    pOwn->bVisible = 1;
    pOwn->nDisplayId = pShip->bId;
    pOwn->nX = 300;
    pOwn->nY = 200;
    pMissionTarget = EvAdd(4); /* mission target index 1, home base index 2 */
    pMissionTarget->bActive = 1;
    pMissionTarget->bVisible = 1;
    pMissionTarget->nDisplayId = 77;
    pMissionTarget->nX = 400;
    pMissionTarget->nY = 200;
    pShip->nStrategIdx = 1;

    loot.apCraft = g_apEvShip;
    loot.apBases = apBases;
    loot.apObjects = apObjects;
    loot.abFacilityGrid = grid;
    loot.pdwUiRefreshFlag = &dwRefresh;
    loot.nBaseCount = 3;
    loot.nShipCount = 1;
    loot.nObjectCount = 59;
    loot.pfnAppendLine = RecordArrivalLootNotice;
    loot.pUser = anNotice;
    g_EvEnv.pLootEnv = &loot;

    shipEnv.apCraft = g_apEvShip;
    shipEnv.pnShipCount = &g_EvWorld.nShipCount;
    shipEnv.apSoldiers = g_apSol;
    shipEnv.pnSoldierCount = &nSoldierCount;
    shipEnv.apObjects = apObjects;
    shipEnv.nObjectCount = 59;
    shipEnv.pStrategList = &g_EvList;
    shipEnv.pWorld = &g_EvWorld;
    shipEnv.pnSpawnOnceFlag = &g_bOnce;
    shipEnv.pSpawnCtx = &g_EvCtx;
    shipEnv.apBases = apBases;
    shipEnv.pnGameTotalSec = &g_EvTime.nTotalSec;

    for (i32 i = 0; i < PUFO_GEOSCAPE_SPEED_COUNT; ++i) {
        apSpeedCtl[i] = &aSpeedCtl[i];
    }
    resolveEnv.pLines = &g_Lines;
    resolveEnv.pbInterceptDone = &bInterceptDone;
    resolveEnv.pbSelectionFlag = &g_bSelFlag;
    resolveEnv.pWorld = &g_EvWorld;
    resolveEnv.apSpeedCtl = apSpeedCtl;
    resolveEnv.pShipEnv = &shipEnv;
    resolveEnv.pTime = &g_EvTime;

    g_EvTime.nTotalSec = 600;
    g_EvTime.bPaused = 1;
    g_bSelFlag = 1;
    /* Model the checked shutdown's target removal before the player order.
     * This local fixture leaves replacement spawning disabled (g_bOnce=0);
     * it does not simulate battle victory or the full shutdown/rebuild. */
    (void)StrategList_RemoveAt(&g_EvList, &g_EvWorld, 1, &g_bOnce, &g_EvCtx);
    CheckEqI32(g_EvList.nCount, 1, "mission target removal leaves the own marker");
    CheckEqI32(pShip->bIntercepting, 1, "target removal retains the surviving intercept byte");
    CheckEqI32(pShip->nStrategIdx, 1, "an equal target index survives compaction");
    UiLineList_Insert(&g_Lines, 0, 27, 0u, 0u, 0u, 0u);
    Smap_ResolveInterceptUI(&resolveEnv);

    CheckEqI32(g_EvList.nCount, 1, "the home order preserves the surviving own marker");
    CheckEqI32(pShip->bIntercepting, 0, "the return order clears the intercept state");
    CheckEqI32(pShip->bAtBase, 0, "the return order starts a leg without teleporting home");
    CheckEqI32(pShip->bWeaponsHot, 1, "the loot-pending flag stays hot during the home leg");
    CheckEqI32(pOwn->nTimer, 0, "the return order restarts the marker timer");
    CheckEqI32(pOwn->nStartX, 300, "the home leg starts at the own marker X");
    CheckEqI32(pOwn->nStartY, 200, "the home leg starts at the own marker Y");
    CheckEqI32(pOwn->nDestX, 100, "the home leg uses the home base X");
    CheckEqI32(pOwn->nDestY, 100, "the home leg uses the home base Y");
    CheckEqI32(pOwn->nMissionSub, 1, "the travel initializer arms the landing leg");
    Check(pOwn->uTravelMin > 0, "the type-1 craft's fuel data produces a real duration");
    CheckEqI32(pOwn->nTravelEpochSec, g_EvTime.nTotalSec,
               "the home leg records the live clock as its epoch");
    CheckEqI32(g_Lines.nLineCount, 0, "the return message is removed after retargeting");
    CheckEqI32(g_EvTime.bPaused, 0, "the empty message list resumes the geoscape clock");
    CheckEqI32(aSpeedCtl[0].nState, 1, "the return handler selects the five-second speed control");

    const i32 nHomeLegSeconds = (i32)pOwn->uTravelMin * 60;
    Check(nHomeLegSeconds > 0, "the scheduled home-leg duration is positive");
    g_EvTime.nTotalSec = pOwn->nTravelEpochSec + nHomeLegSeconds - 60;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pShip->bAtBase, 0, "the craft stays away one minute before its computed arrival");
    g_EvTime.nTotalSec += 60;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pShip->bAtBase, 1, "the computed travel duration lands the returning craft");
    CheckEqI32(pShip->bWeaponsHot, 0, "loot consumes the pending-delivery flag");
    CheckEqI32(pShip->anCargoQty[0], 0, "delivered cargo leaves the craft");
    CheckEqI32(objects[56].auPerBaseVal[2], 16, "home stock receives 9 units on top of 7");
    CheckEqI32(objects[56].auPerBaseVal[0], 0, "craft index does not select base stock");
    CheckEqI32(grid[444 + 144 * 2 + 7], 34, "type 1 is parked at home base 2");
    CheckEqI32(anNotice[0], 1, "arrival's zero argument announces the ammo shortage");
    CheckEqI32(anNotice[1], 40, "ammo shortage posts message 40");
    CheckEqI32(anNotice[2], 2, "the warning names home base 2");
    CheckEqI32(anNotice[3], 52, "the warning names the empty cannon-ammo record");
    CheckEqI32(dwRefresh, 1, "the shortage replaces the full UI refresh word with 1");

    g_EvTime.nTotalSec += 60;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(objects[56].auPerBaseVal[2], 16, "a later event pass does not credit cargo twice");
    CheckEqI32(anNotice[0], 1, "a later event pass does not repeat the shortage message");
    pShip->anCargoQty[0] = 5;
    Base_TransferTacticalLoot(&loot, false);
    CheckEqI32(pShip->anCargoQty[0], 5, "a cold craft's newly carried stores are left aboard");
    CheckEqI32(objects[56].auPerBaseVal[2], 16, "the cleared pending flag blocks a direct second transfer");
    CheckEqI32(anNotice[0], 1, "a cold craft does not repeat the ammo warning");
    g_EvEnv.pLootEnv = NULL;
    EvFree();
}

static void TestProcessEvents(void)
{
    StrategItem *pOwn, *pTarget;

    printf("StrategList_ProcessEvents\n");
    EvReset();

    /* ---- an expired terror site costs money and tells every craft that was chasing it ---- */
    EvReset();
    pOwn = EvAdd(3);
    pOwn->nTimer = 1;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 0;
    g_aEvShip[1].bIntercepting = 1;
    g_aEvShip[1].nStrategIdx = 1;          /* chasing something else */
    g_nFund = 5000;
    CheckEqI32(StrategList_ProcessEvents(&g_EvEnv), 0, "the pass ends on the count it stopped at");
    CheckEqI32(g_nFund, 4200, "an expired terror site takes 800 out of the pending total (0x7FDC4)");
    CheckEqI32(g_EvList.nCount, 0, "and the item is removed");
    CheckEqI32(g_Lines.nLineCount, 1, "only the craft that was chasing THIS item is told (0x7FD6C)");
    CheckEqI32(g_Lines.abLineFlags[0], 13, "  with message 13");
    CheckEqI32((i32)UiLineList_GetColor(&g_Lines, 0, 0), 0xFF, "  0xFF in colour slot 0");
    CheckEqI32((i32)UiLineList_GetColor(&g_Lines, 0, 1), 0, "  and the ship index in slot 1");
    CheckEqI32(g_nSample, 7, "  through the sample-7 seam");

    /* ---- an expired UFO arms the one-shot replacement byte, and the removal consumes it ---- */
    EvReset();
    g_nSpawnCalls = 0;
    g_nEventCalls = 0;
    for (int i = 0; i < 40; ++i) {
        EvFree();
        PtrArray_ctor(&g_EvList);
        g_bOnce = 0;
        pOwn = EvAdd(4);
        pOwn->nTimer = 1;
        (void)StrategList_ProcessEvents(&g_EvEnv);
    }
    Check(g_nSpawnCalls > 0,
          "an expired UFO arms 0x162028 (0x7FDE0) and the removal's tail spends it on a replacement roll");
    CheckEqI32(g_bOnce, 0, "the byte is cleared, so it is one-shot");
    Check(g_nEventCalls >= g_nSpawnCalls, "every replacement spawn also re-runs this pass");

    /* ---- a timer that has not reached zero just counts down ---- */
    EvReset();
    pOwn = EvAdd(3);
    pOwn->nTimer = 5;
    g_nFund = 1000;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nTimer, 4, "a site timer decrements (0x7FD24)");
    CheckEqI32(g_EvList.nCount, 1, "  and nothing is removed");
    CheckEqI32(g_nFund, 1000, "  and no money is spent");
    CheckEqI32(g_Lines.nLineCount, 0, "  and no message is posted");
    EvFree();

    EvReset();
    pOwn = EvAdd(1);
    pOwn->nTimer = 3;
    pOwn->nX = 77;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nTimer, 2, "a type-1 item with a timer only decrements (0x80620)");
    CheckEqI32(pOwn->nX, 77, "  and does not move -- this is how a parked craft idles");
    EvFree();

    EvReset();
    pOwn = EvAdd(1);
    pOwn->bActive = 0;
    pOwn->bVisible = 0;
    pOwn->nX = 88;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nX, 88, "an inactive invisible marker is skipped outright (0x7FE1C)");
    EvFree();

    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;                 /* g_aEvShip[0].bId */
    pOwn->nX = 100;
    pOwn->dTrackA = 10.0;
    g_aEvShip[0].bAtBase = 1;              /* not intercepting */
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nX, 100, "a marker whose craft is at base does not move (0x7FE88)");
    EvFree();

    /* ---- the chase: the wrapped x leg, keeping which candidate won ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 100;
    pOwn->nY = 100;
    pOwn->dTrackA = 10.0;
    pTarget = EvAdd(5);
    pTarget->nX = 200;
    pTarget->nY = 100;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nTravelStartSec, 100, "the 100-column leg beats the wrapped 700 and 900 (0x7FF38)");
    CheckEqI32((i32)pOwn->bTravelDir, 1, "  and runs east");
    CheckEqI32(pOwn->nX, 110, "the whole part of dFracX moves into nX (0x8046C)");
    Check(pOwn->dFracX == 0.0, "  and the remainder stays behind (0x804AC)");
    CheckEqI32(pOwn->nY, 100, "  with no y leg the y accumulator is untouched (0x80134)");
    EvFree();

    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 100;
    pOwn->nY = 100;
    pOwn->dTrackA = 10.0;
    pTarget = EvAdd(5);
    pTarget->nX = 600;                     /* 500 east */
    pTarget->nY = 100;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nTravelStartSec, 300, "a target 500 columns east is reached by the 300-column wrap (0x7FF18)");
    CheckEqI32((i32)pOwn->bTravelDir, -1, "  going WEST");
    CheckEqI32(pOwn->nX, 90, "  so the marker steps down to 90");
    EvFree();

    /* ---- arrival at a type-5 waypoint: abort the intercept and remove the waypoint ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 100;
    pOwn->nY = 100;
    pOwn->dTrackA = 10.0;
    pTarget = EvAdd(5);
    pTarget->nX = 101;
    pTarget->nY = 101;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    g_EvTime.bPaused = 0;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_EvTime.bPaused, 1, "arriving at the waypoint pauses the game (0x80634)");
    CheckEqI32(g_Lines.nLineCount, 1, "  and posts message 14");
    CheckEqI32(g_Lines.abLineFlags[0], 14, "  flag 14");
    CheckEqI32((i32)UiLineList_GetColor(&g_Lines, 0, 0), 0, "  with the ship index in slot 0");
    CheckEqI32(g_aEvShip[0].bIntercepting, 0, "  and the craft stops intercepting (0x80668)");
    CheckEqI32(g_EvList.nCount, 1, "  and the TARGET is removed, not the marker (0x80678)");
    CheckEqI32(pOwn->nX, 100, "the fractional move is skipped once the craft has arrived");
    Check(pOwn->dFracX > 7.0 && pOwn->dFracX < 7.1,
          "  but the accumulators were already loaded: 10 * 1 / sqrt(2) (0x80004)");
    EvFree();

    /* ---- arrival at a terror site ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 100;
    pOwn->nY = 100;
    pTarget = EvAdd(3);
    pTarget->nX = 100;
    pTarget->nY = 100;
    pTarget->nSiteId = 77;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    g_EvTime.bPaused = 0;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_EvTime.bPaused, 1, "a terror-site arrival pauses (0x80684)");
    CheckEqI32(g_nStratIdx, 0, "  and writes the ship index into 0x159E74 (0x80694)");
    CheckEqI32(g_Lines.abLineFlags[0], 21, "  with message 21 (0x80774)");
    CheckEqI32((i32)UiLineList_GetColor(&g_Lines, 0, 1), 1, "  and the target index in slot 1");
    CheckCaption(1146, 77, "  and the caption is TEXTS[1146] plus the site id (0x8069C)");
    CheckEqI32(g_Panel.anLineAux[0], 1, "  with the target index in the panel's aux column (0x80748)");
    CheckEqI32(g_Panel.nUnk3032, 0, "  and the panel cursor cleared (0x80760)");
    CheckEqI32(g_EvList.nCount, 2, "the site is left in the list for the player to dismiss");
    EvFree();

    /* ---- arrival at a UFO ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 100;
    pOwn->nY = 100;
    pTarget = EvAdd(4);
    pTarget->nX = 100;
    pTarget->nY = 100;
    pTarget->nDisplayId = 55;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    g_EvTime.bPaused = 0;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_EvTime.bPaused, 1, "a UFO contact pauses by storing 1 straight into the pause byte (0x807A0)");
    CheckEqI32(g_nIntShip, 0, "  and writes the intercept pair (0x807B8)");
    CheckEqI32(g_nIntStrat, 1, "  target index included (0x807C8)");
    CheckEqI32(g_nStratIdx, 0, "  plus 0x159E74 (0x807BC)");
    CheckEqI32(g_Lines.abLineFlags[0], 23, "  with message 23 (0x807D8)");
    CheckCaption(1147, 55, "  and the caption is TEXTS[1147] plus the display id (0x807E8)");
    EvFree();

    /* ---- arrival at an alien base ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 100;
    pOwn->nY = 100;
    pTarget = EvAdd(0);
    pTarget->nX = 100;
    pTarget->nY = 100;
    pTarget->nSiteId = 9;
    pTarget->bActive = 1;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_Lines.abLineFlags[0], 22, "an alien base posts message 22 (0x80990)");
    CheckCaption(1145, 9, "  with the TEXTS[1145] caption plus the site id (0x808B4)");
    CheckEqI32(g_nPulse, 250, "  and parks the radar pulse timer at 250 (0x80994)");
    EvFree();

    /* ---- an INACTIVE base is not an arrival: the type-0 test fails and nothing posts ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 100;
    pOwn->nY = 100;
    pTarget = EvAdd(0);
    pTarget->nX = 100;
    pTarget->nY = 100;
    pTarget->bActive = 0;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_Lines.nLineCount, 0, "an inactive base is not an arrival (0x803EC..0x803F4)");
    CheckEqI32(g_nPulse, -1, "  and the radar pulse timer is left alone");
    EvFree();

    /* ---- arrival at another craft whose timer is still running: the dogfight offer ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 100;
    pOwn->nY = 100;
    pTarget = EvAdd(1);
    pTarget->nX = 100;
    pTarget->nY = 100;
    pTarget->nDisplayId = 11;
    pTarget->nTimer = 4;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_nIntShip, 0, "a craft-to-craft arrival writes the intercept pair (0x80ECC)");
    CheckEqI32(g_nIntStrat, 1, "  with the target index (0x80ED4)");
    CheckEqI32(g_Lines.abLineFlags[0], 23, "  and offers message 23 (0x80F08)");
    CheckCaption(1147, 11, "  with the UFO caption plus the target's display id (0x80F18)");
    CheckEqI32(pTarget->nTimer, 4, "  the target's timer is untouched while it still runs");
    EvFree();

    /* ---- arrival at a craft whose timer is spent: the confirm runs and the PASS ENDS ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 100;
    pOwn->nY = 100;
    pTarget = EvAdd(1);
    pTarget->nX = 100;
    pTarget->nY = 100;
    pTarget->nDisplayId = 11;
    pTarget->nTimer = 0;
    pTarget = EvAdd(3);            /* index 2: a site that would expire if the pass continued */
    pTarget->nTimer = 1;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    g_nFund = 900;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_EvList.nCount, 3, "ORIGINAL SHAPE: the dogfight confirm tail-calls the epilogue, so the rest of the list is never visited (0x80FCC)");
    CheckEqI32(((StrategItem *)g_EvList.apItems[2])->nTimer, 1, "  the site after it keeps its timer");
    CheckEqI32(g_nFund, 900, "  and spends nothing");
    EvFree();

    /* ---- an active item whose leg is over goes to the mission pass ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bActive = 1;
    pOwn->nMissionSub = 0;
    pOwn->nPhase = 5;
    pOwn->uTravelMin = 1;
    pOwn->nTravelEpochSec = 0;
    pOwn->nX = 300;
    g_EvTime.nTotalSec = 120;                 /* 120 / 60 = 2 >= 1 */
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nPhase, 4, "a finished leg hands the item to Geoscape_SpawnMissionSite(-1) (0x809B8)");
    CheckEqI32(pOwn->nX, 300, "  and the position is not interpolated on that path");
    EvFree();

    /* ---- an active item with bDone set is dismissed and removed ---- */
    /* The replacement ctx is NULL for this run: the removal's own tail would otherwise roll a
     * replacement craft into the list and the count check would measure that instead. */
    EvReset();
    g_EvEnv.pSpawnCtx = NULL;
    pOwn = EvAdd(1);
    pOwn->bActive = 1;
    pOwn->bDone = 1;
    pOwn->uTravelMin = 100;                   /* the leg is NOT over, so the spawn gate is not taken */
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 0;
    g_Lines.nLineCount = 1;
    g_Lines.abLineFlags[0] = 23;
    g_EvTime.bPaused = 1;
    g_bSelFlag = 1;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_nStratIdx, 0, "a done item writes 0x159E74 (0x805B0)");
    CheckEqI32(g_nSelShip, 0, "  and 0x187054 (0x805B8)");
    CheckEqI32(g_Lines.nLineCount, 0, "  and Smap_DismissSelected drops the message line (0x805BC)");
    CheckEqI32(g_EvTime.bPaused, 0, "  which unpauses because the list came empty");
    CheckEqI32(g_bSelFlag, 0, "  and clears the selection flag");
    CheckEqI32(g_EvList.nCount, 0, "  then the item itself is removed (0x805E4)");
    CheckEqI32(g_bOnce, 1, "  the byte armed at 0x8057C stays armed here, because the removal tail only spends it when it has a replacement ctx");
    EvFree();

    /* The arming is real: with a replacement ctx the spent byte shows up as a replacement roll. */
    EvReset();
    g_nSpawnCalls = 0;
    for (int i = 0; i < 40; ++i) {
        EvFree();
        PtrArray_ctor(&g_EvList);
        g_bOnce = 0;
        pOwn = EvAdd(1);
        pOwn->bActive = 1;
        pOwn->bDone = 1;
        pOwn->uTravelMin = 100;
        (void)StrategList_ProcessEvents(&g_EvEnv);
    }
    Check(g_nSpawnCalls > 0, "a done item really does arm 0x162028 at 0x8057C -- the removal spends it on a replacement");
    CheckEqI32(g_bOnce, 0, "  and with that ctx present the byte ends clear (0x82B94)");

    /* ---- the interpolation re-derives the position from the clock ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bActive = 1;
    pOwn->bTravelDir = 1;
    pOwn->nStartX = 100;
    pOwn->nStartY = 200;
    pOwn->nDestY = 300;
    pOwn->nTravelStartSec = 400;
    pOwn->uTravelMin = 10;                    /* 10 minutes; 5 elapsed, so half the leg */
    g_EvTime.nTotalSec = 300;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nX, 300, "nX = nStartX + elapsed * nTravelStartSec / (60 * uTravelMin) (0x80A18)");
    CheckEqI32(pOwn->nY, 250, "nY = nStartY + elapsed * |destY - startY| / (60 * uTravelMin) (0x80C0C)");
    EvFree();

    EvReset();
    pOwn = EvAdd(1);
    pOwn->bActive = 1;
    pOwn->bTravelDir = -1;
    pOwn->nStartX = 100;
    pOwn->nStartY = 200;
    pOwn->nDestY = 100;
    pOwn->nTravelStartSec = 400;
    pOwn->uTravelMin = 10;
    g_EvTime.nTotalSec = 300;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nX, 700, "bTravelDir -1 subtracts the x progress (0x80B0C), and the negative result wraps to 700 (0x80DF0)");
    CheckEqI32(pOwn->nY, 150, "  and destY below startY subtracts the y progress (0x80CF0)");
    EvFree();

    EvReset();
    pOwn = EvAdd(1);
    pOwn->bActive = 1;
    pOwn->bTravelDir = 1;
    pOwn->nStartX = 700;
    pOwn->nTravelStartSec = 400;
    pOwn->uTravelMin = 10;
    g_EvTime.nTotalSec = 300;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nX, 100, "nX is wrapped ONCE above 800 (0x80DE0)");
    EvFree();

    EvReset();
    pOwn = EvAdd(1);
    pOwn->bActive = 1;
    pOwn->bTravelDir = -1;
    pOwn->nStartX = 100;
    pOwn->nTravelStartSec = 400;
    pOwn->uTravelMin = 10;
    g_EvTime.nTotalSec = 300;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(pOwn->nX, 700, "and once below 0 (0x80DF0)");
    EvFree();

    /* ---- an inactive marker on a mission leg that is over has LANDED ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nMissionSub = 1;
    pOwn->uTravelMin = 1;
    pOwn->nTravelEpochSec = 0;
    g_aEvShip[0].bAtBase = 0;
    g_aEvShip[0].bContactMsg = 1;
    g_EvTime.nTotalSec = 60;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_aEvShip[0].bAtBase, 1, "a finished mission leg lands the craft (0x80E5C)");
    CheckEqI32(g_aEvShip[0].bContactMsg, 0, "  clears its contact message (0x80E68)");
    CheckEqI32(pOwn->nMissionSub, 0, "  and the marker's mission subtype (0x80E6C)");
    CheckEqI32(pOwn->bVisible, 0, "  and hides the marker (0x80E70)");
    EvFree();

    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nMissionSub = 1;
    pOwn->uTravelMin = 100;
    g_aEvShip[0].bAtBase = 0;
    g_EvTime.nTotalSec = 60;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    CheckEqI32(g_aEvShip[0].bAtBase, 0, "a mission leg that is not over does not land (0x80E38)");
    EvFree();

    /* ---- an inactive marker whose craft IS intercepting keeps the chase position ---- */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->bVisible = 1;
    pOwn->nDisplayId = 10;
    pOwn->nX = 140;
    pOwn->nY = 140;
    pOwn->nStartX = 500;
    pOwn->nStartY = 500;
    pOwn->uTravelMin = 1;
    pTarget = EvAdd(5);
    pTarget->nX = 200;
    pTarget->nY = 200;
    g_aEvShip[0].bIntercepting = 1;
    g_aEvShip[0].nStrategIdx = 1;
    g_EvTime.nTotalSec = 300;
    (void)StrategList_ProcessEvents(&g_EvEnv);
    Check(pOwn->nX != 500, "the interpolation is skipped for an inactive marker whose craft is mid-intercept (0x809F0)");
    CheckEqI32(pOwn->nStartX, 500, "  nStartX is left where the travel put it");
    EvFree();
}

/* Strateg_ConfirmInterceptTarget @ 0x7fb98, reached through the craft-to-craft arrival. */
static void TestConfirmIntercept(void)
{
    StrategItem *pOwn, *pTarget;

    printf("Strateg_ConfirmInterceptTarget\n");

    /* The target's timer is already spent: the radar screen opens. pRadarEnv is NULL here (the app
     * does not own a radar session yet), so the observable part is that nothing else happens. */
    EvReset();
    pTarget = EvAdd(1);
    pTarget->nTimer = 0;
    g_nIntShip = 0;
    g_nIntStrat = 0;
    Strateg_ConfirmInterceptTarget(&g_EvEnv);
    CheckEqI32(g_Lines.nLineCount, 0, "a spent target timer goes to the radar screen, not to a message (0x7FCA4)");
    CheckEqI32(g_nPulse, -1, "  and the assault branch is not taken");
    EvFree();

    /* The craft carries soldiers: the ground-assault line, and the radar pulse parked at 250.
     * Craft_CountSoldiersAboard matches Soldier.bUnk183 against Ship.bId here -- see the
     * ORIGINAL BUG note on the port. */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->nDisplayId = 10;
    pTarget = EvAdd(1);
    pTarget->nTimer = 5;
    pTarget->nDisplayId = 11;
    g_nIntShip = 0;
    g_nIntStrat = 1;
    g_aSol[0].bUnk183 = 10;                  /* == g_aEvShip[0].bId */
    Strateg_ConfirmInterceptTarget(&g_EvEnv);
    CheckEqI32(g_nStratIdx, 0, "the assault offer writes 0x159E74 (0x7FC38)");
    CheckEqI32(g_Panel.anLineAux[0], 1, "  the target index into the panel aux column (0x7FC48)");
    CheckEqI32(g_Panel.nUnk3032, 0, "  and clears the panel cursor (0x7FC60)");
    CheckEqI32(g_Lines.abLineFlags[0], 23, "  with message 23 (0x7FC78)");
    CheckEqI32((i32)UiLineList_GetColor(&g_Lines, 0, 0), 0, "  ship index in slot 0");
    CheckEqI32((i32)UiLineList_GetColor(&g_Lines, 0, 1), 1, "  target index in slot 1");
    CheckEqI32(g_nPulse, 250, "  and parks the radar pulse timer (0x7FC84)");
    CheckEqI32(pOwn->nTimer, 0, "  the craft's own timer is left alone on this branch");
    EvFree();

    /* No soldiers aboard: the craft's OWN item timer is parked at 0xFFFF, which is the abort. */
    EvReset();
    pOwn = EvAdd(1);
    pOwn->nDisplayId = 10;                   /* g_aEvShip[0].bId */
    pTarget = EvAdd(1);
    pTarget->nTimer = 5;
    pTarget->nDisplayId = 11;
    g_nIntShip = 0;
    g_nIntStrat = 1;
    Strateg_ConfirmInterceptTarget(&g_EvEnv);
    CheckEqI32(pOwn->nTimer, 0xFFFF, "a craft with no crew parks its own marker timer at 0xFFFF (0x7FC10)");
    CheckEqI32(pTarget->nTimer, 5, "  and the target's timer is untouched");
    CheckEqI32(g_Lines.nLineCount, 0, "  with no message posted");
    EvFree();
}

int main(void)
{
    TestCountAndFind();
    TestUpdateMotion();
    TestCalcDetect();
    TestUpdateDetectAndIntercepts();
    TestSmapView();
    TestSmapZoom();
    TestZoomButtons();
    TestStrategListAlloc();
    TestFindIdxForShip();
    TestFindByStrategIdx();
    TestOverlayAtMode();
    TestAddItem();
    TestAddUfoAt();
    TestRollCraftStats();
    TestAddAlienSiteAndTravel();
    TestMaybeSpawnCraft();
    TestRemoveAtAndPurge();
    TestSpawnMissionSite();
    TestMissionTravelRawFallbacks();
    TestDistWrapped();
    TestCraftRangeGate();
    TestSmapCraftSelectLiveSoldierCount();
    TestProcessEvents();
    TestArrivalDeliversLootOnce();
    TestConfirmIntercept();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
