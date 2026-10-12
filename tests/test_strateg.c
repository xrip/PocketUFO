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
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
