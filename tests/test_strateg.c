/* test_strateg.c - StrategList_UpdateMotion @ 0x80fd8, StrategList_UpdateDetect
 * @ 0x81948, Strateg_CalcDetect @ 0x81378, Ship_ClearExpiredIntercepts @
 * 0x8128c, CountActive @ 0x81a2c, FindByShipId @ 0x5a9b4.
 *
 * The pinned oddities: the proximity distance is sqrt(dx^2 + dy^2 + thresh)
 * with the threshold INSIDE the root; the wrapped base distance is
 * min(|bx-ux+800|, |ux-bx+800|, |ux-bx|); every site state requires the
 * proximity timer to be idle; states 11/12 set bHyperDetect.
 */
#include "game/pufo_strateg.h"

#include <stdio.h>
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
static u8 g_aRadarPct[4];
static GameTime g_Time; /* stands for the apLines[101..115] block */

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
    printf("StrategList_AddItem\n");
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
    printf("AddAlienSiteIfValid / InitTravel\n");
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
    printf("RemoveAt / PurgeType5\n");
    PtrArray list;
    Ship ship;
    Ship *apCraft[1];
    GeoscapeWorld world;
    StrategSpawnCtx ctx;
    u8 once;
    i32 seed = 0;
    i32 i;

    apCraft[0] = &ship;
    memset(&world, 0, sizeof(world));
    world.apCraft = apCraft;
    world.nShipCount = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pTables = &g_Roll;
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
          "state 1 within 24 with 100% radar detects");
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

    /* Outside the 24 radius: dx 40. */
    g_aItems[1].nX = 50; /* dx = 40 */
    Check(!Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "state 1 outside 24 does not detect");
    g_aItems[1].nX = 790;

    /* State 2 reaches 31: dx 30 is inside. */
    g_aSiteState[0] = 2;
    g_aItems[1].nX = 40; /* dx = 30 */
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "state 2 within 31 detects");
    g_aItems[1].nX = 790;

    /* State 11 hyper-detects within 24. */
    g_aSiteState[0] = 11;
    g_aItems[1].bHyperDetect = 0;
    Check(Strateg_CalcDetect(&g_aItems[1], 1, 0, &list, &world, &hooks),
          "state 11 detects");
    CheckEqI32(g_aItems[1].bHyperDetect, 1, "state 11 sets hyper flag");

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
    printf("UpdateDetect / ClearExpiredIntercepts\n");
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

    printf("SmapView @ 0x1863E8\n");

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
}

static void TestSmapZoom(void)
{
    SmapView view;
    SmapZoomSet aSets[3];
    SmapWorldSize world;
    static PadImage aGrid[3];

    printf("Smap_ZoomIn / Smap_ZoomOut\n");

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

    printf("Smap_OnZoomIn / Smap_OnZoomOut\n");

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
    CheckEqI32(g_aSampleLog[2], 17, "the zoom-out click is the same sample");
    CheckEqI32(view.nMode, 1, "zoom out moves back one level");

    view.nMode = 0;
    Check(!Smap_OnZoomOut(&view, aSets, &world, RecordSample, NULL),
          "level 0 still refuses the zoom-out step");
    CheckEqI32(g_nSampleCalls, 5, "the widest level clicks too");
}

static void TestStrategListAlloc(void)
{
    PtrArray list;

    printf("StrategItem_AllocAdd\n");

    StrategList_ctor(&list);
    CheckEqI32(list.nCount, 0, "StrategList_ctor leaves an empty list");

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
    static MissionSiteRecord g_aSites[4];
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
    TestAddItem();
    TestAddUfoAt();
    TestRollCraftStats();
    TestAddAlienSiteAndTravel();
    TestMaybeSpawnCraft();
    TestRemoveAtAndPurge();
    TestSpawnMissionSite();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
