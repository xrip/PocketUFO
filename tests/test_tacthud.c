/* Tests for the tactical HUD action buttons.
 *
 * Evidence: TactHud_ToggleKneel @ 0x16570, TactHud_ClimbUp @ 0x1674c,
 *           TactHud_ClimbDown @ 0x16af0, MapCell_TestFlag @ 0x549c8,
 *           TactState_EnqueueVertStep @ 0x49ddc, TactState_KickQueue @ 0x4f334.
 *
 * The gate order, the layer bases and the large-unit neighbour set are all read
 * from the ARM, not from the pseudocode: 0x16798 (byte_A7B80[type][0]),
 * 0x16790/0x16794 (apCells bases 0x15338 and 0x18B78), 0x16938..0x16940
 * (neighbour bases 0x18B7C, 0x18C68, 0x18C6C), 0x1661C/0x16654 (the two status
 * record offsets 0x506DB and 0x5060E). */
#include "game/pufo_tacthud.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

#include <stdio.h>
#include <string.h>

#include "pufo_test.h"

static inline void Check(const char *pWhat, int bOk) { TestCheck(pWhat, bOk); }

/* A 10 x 10 x 4 grid, which is Map at nMapDimX = nMapDimY = 1. */
#define GRID 10

static MapCell g_aGrid[4][GRID][GRID];

/* Ground items for the inventory-open test. */
static ObjectRec g_aObj[2];
static TactItem  g_aItem[2];
static Mc1Rec  g_aSpr[8];
static TactState g_State;
static Soldier g_Unit;
static Soldier g_Other;
static TactHudCtx g_Ctx;

static i32 g_nSample;
static i32 g_nSampleCalls;
static i32 g_nRecord;
static i32 g_nWarnCalls;
static bool g_bWarn;

static MapCell *Lookup(i32 nX, i32 nY, i32 nL, void *pUser)
{
    (void)pUser;
    if (nL < 0 || nL > 3 || nX < 0 || nX >= GRID || nY < 0 || nY >= GRID) {
        return NULL;
    }
    return &g_aGrid[nL][nY][nX];
}

static i32 g_nFloorSlot;

static MapCell *At(i32 nX, i32 nY, i32 nL) { return &g_aGrid[nL][nY][nX]; }

static void CbPlay(i32 nSample, void *pUser)
{
    (void)pUser;
    g_nSample = nSample;
    g_nSampleCalls += 1;
}
static void CbPush(i32 nRecord, void *pUser)
{
    (void)pUser;
    g_nRecord = nRecord;
}
static bool CbWarn(const Soldier *pUnit, i32 nTuCost, void *pUser)
{
    (void)pUnit; (void)nTuCost; (void)pUser;
    g_nWarnCalls += 1;
    return g_bWarn;
}

/* A floor sprite that is neither transparent nor blocking, so a cell carrying
 * it is not blank and does not pass the ladder test until bBlocksSight is set. */
static Mc1Rec *Floor(u8 bBlocks)
{
    Mc1Rec *p = &g_aSpr[g_nFloorSlot++];
    memset(p, 0, sizeof(*p));
    p->bTransparent = 0;
    p->bBlocksSight = bBlocks;
    return p;
}

static Mc1Rec *Wall(u8 bTransparent)
{
    Mc1Rec *p = &g_aSpr[7];
    memset(p, 0, sizeof(*p));
    p->bTransparent = bTransparent;
    return p;
}

static void Reset(void)
{
    memset(g_aGrid, 0, sizeof(g_aGrid));
    memset(g_aSpr, 0, sizeof(g_aSpr));
    memset(&g_State, 0, sizeof(g_State));
    memset(&g_Unit, 0, sizeof(g_Unit));
    memset(&g_Other, 0, sizeof(g_Other));
    memset(&g_Ctx, 0, sizeof(g_Ctx));

    for (i32 z = 0; z < 4; ++z) {
        for (i32 y = 0; y < GRID; ++y) {
            for (i32 x = 0; x < GRID; ++x) {
                MapCell_Init(At(x, y, z), x, y, z);
            }
        }
    }

    g_nFloorSlot = 0;
    g_nSample = -1;
    g_nSampleCalls = 0;
    g_nRecord = -1;
    g_nWarnCalls = 0;
    g_bWarn = false;

    g_Unit.pCell = At(3, 3, 1);
    g_Unit.nUnitType = 0;
    g_Unit.uTu = 10;
    g_Unit.nAnimPose = 5;
    g_Unit.bStatus = 0;

    g_Ctx.pSelUnit     = &g_Unit;
    g_Ctx.pQueue       = &g_State;
    g_Ctx.geom.nDimX   = 1;
    g_Ctx.geom.nDimY   = 1;
    g_Ctx.pfnLookup    = Lookup;
    g_Ctx.pLookupUser  = NULL;
    g_Ctx.pfnArmorTuWarn = CbWarn;
    g_Ctx.pfnPushTextRecord = CbPush;
    g_Ctx.pfnPlaySample = CbPlay;

    /* A climbable pair by default: both floors block, neither is transparent. */
    g_Unit.pCell->pFloorSpr = Floor(1);
    At(3, 3, 2)->pFloorSpr   = Floor(1);
    At(3, 3, 0)->pFloorSpr   = Floor(1);
}

/* Every cell the climb handlers consult, cleared of sprites and occupants. */
static void ClearClimbCells(void)
{
    static const i8 aDx[4] = { 0, 0, 1, 0 };
    static const i8 aDy[4] = { 0, 0, 0, 1 };
    for (i32 i = 0; i < 4; ++i) {
        MapCell *p = At(3 + aDx[i], 3 + aDy[i], 2);
        p->pFloorSpr = NULL;
        p->pWallSprA = NULL;
        p->pWallSprB = NULL;
        p->pCeilSpr  = NULL;
        p->pUnit     = NULL;
    }
}

static bool Queued(i32 nAt, i32 nCmd, i32 nFacing)
{
    if (nAt >= g_State.nQueueLen) {
        return false;
    }
    return g_State.aQueue[nAt].nCmd == nCmd
        && g_State.aQueue[nAt].nFacing == nFacing
        && g_State.aQueue[nAt].pUnit == &g_Unit;
}

/* --- kneel ----------------------------------------------------------------- */

static void TestKneel(void)
{
    printf("TactHud_ToggleKneel @ 0x16570\n");

    TactHud_ToggleKneel(&g_Ctx);
    Check("kneel sets bFlags bit 0", (g_Unit.bFlags & 1u) != 0);
    Check("kneel spends 2 TU", g_Unit.uTu == 8);
    Check("kneel lowers nAnimPose by hand", g_Unit.nAnimPose == 4);
    Check("kneel pushes the marksmanship record",
          g_nRecord == PUFO_TXTREC_KNEEL_ON);
    Check("the click sample plays once", g_nSampleCalls == 1);
    Check("the click sample is 17", g_nSample == 17);

    TactHud_ToggleKneel(&g_Ctx);
    Check("the second press stands up", (g_Unit.bFlags & 1u) == 0);
    Check("standing restores nAnimPose", g_Unit.nAnimPose == 5);
    Check("standing costs another 2 TU", g_Unit.uTu == 6);
    Check("standing pushes the other record",
          g_nRecord == PUFO_TXTREC_KNEEL_OFF);
    Check("nothing was queued", g_State.nQueueLen == 0);

    Reset();
    g_Unit.nUnitType = 4;
    TactHud_ToggleKneel(&g_Ctx);
    Check("unit type 4 cannot kneel", (g_Unit.bFlags & 1u) == 0);
    Check("a blocked kneel still spends no TU", g_Unit.uTu == 10);

    Reset();
    g_Unit.nUnitType = 3;
    TactHud_ToggleKneel(&g_Ctx);
    Check("unit type 3 is the last type that can kneel",
          (g_Unit.bFlags & 1u) != 0);

    Reset();
    g_Unit.pCell->pFloorSpr = NULL;
    TactHud_ToggleKneel(&g_Ctx);
    Check("no floor sprite blocks the kneel", (g_Unit.bFlags & 1u) == 0);

    Reset();
    g_Unit.uTu = 1;
    TactHud_ToggleKneel(&g_Ctx);
    Check("one TU is not enough to kneel", (g_Unit.bFlags & 1u) == 0);
    Check("the click sample plays even when the gate fails", g_nSampleCalls == 1);

    Reset();
    g_bWarn = true;
    TactHud_ToggleKneel(&g_Ctx);
    Check("the armour-TU warning blocks the kneel", (g_Unit.bFlags & 1u) == 0);
    Check("the warning is asked once", g_nWarnCalls == 1);
    Check("a blocked kneel spends no TU", g_Unit.uTu == 10);
    Check("a blocked kneel pushes no record", g_nRecord == -1);
}

/* --- climb up -------------------------------------------------------------- */

static void TestClimbUp(void)
{
    printf("TactHud_ClimbUp @ 0x1674c\n");

    TactHud_ClimbUp(&g_Ctx);
    Check("a ladder pair enqueues a vertical step", Queued(0, TACTCMD_VERT_STEP, 1));
    Check("the queue holds one entry", g_State.nQueueLen == 1);
    Check("the queue starts", g_State.bQueueActive == 1);

    Reset();
    g_Unit.pCell->pFloorSpr->bBlocksSight = 0;
    TactHud_ClimbUp(&g_Ctx);
    Check("a non-blocking own floor blocks the climb", g_State.nQueueLen == 0);

    Reset();
    At(3, 3, 2)->pFloorSpr->bBlocksSight = 0;
    TactHud_ClimbUp(&g_Ctx);
    Check("a non-blocking upper floor blocks the climb", g_State.nQueueLen == 0);

    Reset();
    ClearClimbCells();
    g_Unit.pCell->pFloorSpr = Floor(1);
    TactHud_ClimbUp(&g_Ctx);
    Check("no floor sprite above fails the first ladder test",
          g_State.nQueueLen == 0);

    Reset();
    ClearClimbCells();
    TactHud_ClimbUp(&g_Ctx);
    Check("no floor anywhere blocks the climb", g_State.nQueueLen == 0);

    Reset();
    g_Unit.nUnitType = 3;
    ClearClimbCells();
    TactHud_ClimbUp(&g_Ctx);
    Check("the byte_A7B80[type][0] flag skips both ladder tests",
          g_State.nQueueLen == 1);

    Reset();
    g_Unit.nUnitType = 3;
    At(3, 3, 2)->pFloorSpr->bBlocksSight = 0;
    TactHud_ClimbUp(&g_Ctx);
    Check("the flag still obeys the second ladder test",
          g_State.nQueueLen == 0);

    Reset();
    g_Unit.pCell = At(3, 3, 3);
    TactHud_ClimbUp(&g_Ctx);
    Check("layer 3 has nothing above it", g_State.nQueueLen == 0);

    Reset();
    At(3, 3, 2)->pUnit = &g_Other;
    g_Other.bStatus = 0;
    TactHud_ClimbUp(&g_Ctx);
    Check("an occupant above blocks the climb", g_State.nQueueLen == 0);

    Reset();
    At(3, 3, 2)->pUnit = &g_Other;
    g_Other.bStatus = 3;
    TactHud_ClimbUp(&g_Ctx);
    Check("an occupant with status 3 is not in the way",
          g_State.nQueueLen == 1);

    Reset();
    At(3, 3, 2)->pCeilSpr = &g_aSpr[2];
    g_aSpr[2].bTransparent = 255;
    TactHud_ClimbUp(&g_Ctx);
    Check("a transparent ceiling makes the cell blank", g_State.nQueueLen == 0);

    Reset();
    g_Unit.nUnitType = 4;
    At(4, 3, 2)->pWallSprA = Wall(255);
    TactHud_ClimbUp(&g_Ctx);
    Check("a large unit needs room in its other tiles",
          g_State.nQueueLen == 0);

    Reset();
    g_Unit.nUnitType = 4;
    At(3, 4, 2)->pWallSprB = Wall(255);
    TactHud_ClimbUp(&g_Ctx);
    Check("the wall-B test covers the y neighbour", g_State.nQueueLen == 0);

    Reset();
    g_Unit.nUnitType = 4;
    At(4, 4, 2)->pUnit = &g_Other;
    g_Other.bStatus = 0;
    TactHud_ClimbUp(&g_Ctx);
    Check("the diagonal tile must be free of occupants",
          g_State.nQueueLen == 0);

    Reset();
    g_Unit.nUnitType = 4;
    TactHud_ClimbUp(&g_Ctx);
    Check("a large unit climbs when all three tiles are clear",
          Queued(0, TACTCMD_VERT_STEP, 1));

    Reset();
    g_Unit.nUnitType = 4;
    At(4, 3, 2)->pWallSprA = Wall(0);
    TactHud_ClimbUp(&g_Ctx);
    Check("a non-transparent wall A passes the blank test",
          g_State.nQueueLen == 1);

    Reset();
    g_Unit.bFlags = 1;
    TactHud_ClimbUp(&g_Ctx);
    Check("a kneeling unit stands first, then climbs",
          Queued(0, TACTCMD_STAND_FROM_KNEEL, 0)
          && Queued(1, TACTCMD_VERT_STEP, 1));
}

/* --- climb down ------------------------------------------------------------ */

static void TestClimbDown(void)
{
    printf("TactHud_ClimbDown @ 0x16af0\n");

    TactHud_ClimbDown(&g_Ctx);
    Check("a clear cell below enqueues a vertical step",
          Queued(0, TACTCMD_VERT_STEP, 0xFF));
    Check("the facing byte is 0xFF, which the queue reads as -1",
          (i8)g_State.aQueue[0].nFacing == -1);

    Reset();
    g_Unit.pCell = At(3, 3, 0);
    TactHud_ClimbDown(&g_Ctx);
    Check("layer 0 has nothing below it", g_State.nQueueLen == 0);

    Reset();
    g_Unit.pCell->pFloorSpr = NULL;
    At(3, 3, 0)->pFloorSpr = NULL;
    TactHud_ClimbDown(&g_Ctx);
    Check("no own floor skips the ladder test", g_State.nQueueLen == 1);

    Reset();
    At(3, 3, 0)->pFloorSpr->bBlocksSight = 0;
    TactHud_ClimbDown(&g_Ctx);
    Check("a non-blocking lower floor blocks the climb", g_State.nQueueLen == 0);

    Reset();
    g_Unit.pCell->pFloorSpr->bBlocksSight = 0;
    At(3, 3, 0)->pFloorSpr->bBlocksSight = 0;
    TactHud_ClimbDown(&g_Ctx);
    Check("a non-blocking own floor blocks the climb", g_State.nQueueLen == 0);

    Reset();
    At(3, 3, 0)->pUnit = &g_Other;
    g_Other.bStatus = 0;
    TactHud_ClimbDown(&g_Ctx);
    Check("an occupant below blocks the climb", g_State.nQueueLen == 0);

    Reset();
    g_Unit.nUnitType = 4;
    At(4, 3, 0)->pWallSprA = Wall(255);
    TactHud_ClimbDown(&g_Ctx);
    Check("the large-unit test uses the lower layer", g_State.nQueueLen == 0);

    Reset();
    g_Unit.nUnitType = 4;
    At(4, 3, 1)->pWallSprA = Wall(255);
    TactHud_ClimbDown(&g_Ctx);
    Check("the own layer is not part of the large-unit test",
          g_State.nQueueLen == 1);

    Reset();
    g_Unit.bFlags = 1;
    TactHud_ClimbDown(&g_Ctx);
    Check("a kneeling unit stands first, then climbs down",
          Queued(0, TACTCMD_STAND_FROM_KNEEL, 0)
          && Queued(1, TACTCMD_VERT_STEP, 0xFF));
}

static void TestOpenInventory(void)
{
    TactHudInvOpenCtx ctx;
    TactCursor cursor = { 3, 3, 1 };
    TactUiState ui;

    printf("TactHud_OpenInventory\n");

    Reset();
    memset(&ctx, 0, sizeof(ctx));
    memset(&ui, 0, sizeof(ui));
    ctx.pUi = &ui;
    ctx.pSelUnit = &g_Unit;
    ctx.pCursor = &cursor;
    ctx.geom.nDimX = 1;
    ctx.geom.nDimY = 1;
    ctx.pfnLookup = Lookup;
    ctx.nXcomCount = 4;
    ctx.bSweepGate = 1;
    ctx.bSelUnitLarge = false;
    ctx.pfnPlaySample = CbPlay;

    /* Two distinct objects on the selected unit's cell, so the rebuild the
     * original performs is visible as a list of two entries. */
    memset(g_aObj, 0, sizeof(g_aObj));
    memset(g_aItem, 0, sizeof(g_aItem));
    g_aObj[0].uSortKey = 40;
    g_aObj[1].uSortKey = 12;
    g_aItem[0].pObj = &g_aObj[0];
    g_aItem[0].pNext = &g_aItem[1];
    g_aItem[1].pObj = &g_aObj[1];
    g_Unit.pCell->pItemList = g_aItem;
    ui.nInvListCount = 9;   /* sentinel: a rebuild always rewrites it */

    Check("the inventory opens for a humanoid",
          TactHud_OpenInventory(&ctx) && ui.nUiScreen == 1);
    Check("the sweep gate picks sample 16", g_nSample == 16 && g_nSampleCalls == 1);
    Check("the filter resets to every category", ui.nInvListSel == 0xFFu);
    Check("the ground list scroll resets", ui.nInvFilterType == 0);
    Check("the All pad is the lit one",
          ui.bFilterPadAll == 1 && ui.bFilterPadX == 0
          && ui.abFilterPad[0] == 0 && ui.abFilterPad[1] == 0
          && ui.abFilterPad[2] == 0 && ui.abFilterPad[3] == 0
          && ui.abFilterPad[4] == 0);
    Check("the ground list is rebuilt and sorted",
          ui.nInvListCount == 2 && ui.apInvList[0]->pObj == &g_aObj[1]
          && ui.apInvList[1]->pObj == &g_aObj[0]);
    Check("the iso layer draw is switched on", ui.bDrawIsoLayer == 1);
    Check("the cursor is re-stamped on its own cell",
          cursor.nX == 3 && cursor.nY == 3);

    /* The sample is gated, the rest of the gate is not. */
    Reset();
    ctx.bSweepGate = 0;
    Check("the screen opens without the sample",
          TactHud_OpenInventory(&ctx) && g_nSampleCalls == 0);

    Reset();
    ctx.bSweepGate = 1;
    memset(&ui, 0, sizeof(ui));
    ui.bActionLatch = 3;
    ui.bFilterPadX = 1;
    ui.abFilterPad[2] = 1;
    ui.nInvFilterType = 7;
    ui.nInvListSel = 4;
    ctx.nXcomCount = 0;
    Check("no team units means no inventory",
          !TactHud_OpenInventory(&ctx) && ui.nUiScreen == 0);

    Reset();
    ctx.nXcomCount = 4;
    g_Unit.nUnitType = 4;
    Check("a non-humanoid gets no inventory", !TactHud_OpenInventory(&ctx));

    Reset();
    g_Unit.nUnitType = 0;
    TactHud_OpenInventory(&ctx);
    Check("the pads and the latch are reset on the way in",
          ui.bActionLatch == 0 && ui.bFilterPadX == 0 && ui.abFilterPad[2] == 0
          && ui.nInvFilterType == 0 && ui.nInvListSel == 0xFFu);
}

/* TactHud_UseSpecialItem20 @ 0x17d54. The three sort-key branches, the medikit
 * target search and the two refusal pairs. */
static void TestSpecialItem(void)
{
    static GameMap map;
    static TactUiState ui;
    static TactHudSpecialCtx ctx;
    static TactItem item;
    static ObjectRec obj;
    static Soldier civ;
    static Soldier third;

    printf("TactHud_UseSpecialItem\n");

    Reset();
    memset(&map, 0, sizeof(map));
    memset(&ui, 0, sizeof(ui));
    memset(&ctx, 0, sizeof(ctx));
    memset(&item, 0, sizeof(item));
    memset(&obj, 0, sizeof(obj));
    memset(&civ, 0, sizeof(civ));
    memset(&third, 0, sizeof(third));

    map.geom.nDimX = 1;
    map.geom.nDimY = 1;
    map.pSelUnit = &g_Unit;
    item.pObj = &obj;
    map.pSelWeapon = &item;
    ui.anActionCost[PUFO_ACTION_MODE_SPECIAL] = 4;
    ctx.pMap = &map;
    ctx.pUi = &ui;
    ctx.pQueue = &g_State;
    ctx.pfnLookup = Lookup;
    ctx.pfnPlaySample = CbPlay;
    ctx.pfnPushTextRecord = CbPush;

    /* An unrecognised weapon still leaves mode 8 and screen 0 behind. */
    obj.uSortKey = 5;
    TactHud_UseSpecialItem(&ctx);
    Check("mode 8 and screen 0 are written before the weapon is read",
          ui.nActionMode == PUFO_ACTION_MODE_SPECIAL && ui.nUiScreen == 0);
    Check("an unrelated item does nothing else",
          g_State.nQueueLen == 0 && g_nRecord == -1 && g_Unit.uTu == 10u);

    /* --- sort key 20: pay, mark, open screen 6 --- */
    obj.uSortKey = PUFO_SPECIAL_SORT_MARK;
    g_Unit.uTu = 3;
    TactHud_UseSpecialItem(&ctx);
    Check("sort key 20 refuses without the TU", g_nRecord == 1 && g_Unit.uTu == 3u);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    g_Unit.pCell = At(0, 0, 1);
    g_Other.pCell = At(9, 9, 1);
    third.pCell = At(6, 8, 1);
    map.apEnemyUnits[0] = &g_Other;
    map.apEnemyUnits[1] = &third;
    map.nEnemyCount = 2;
    map.apXcomUnits[0] = &g_Unit;
    map.nXcomCount = 1;
    TactHud_UseSpecialItem(&ctx);
    Check("sort key 20 pays from the unit's own TU", g_Unit.uTu == 6u);
    Check("a unit 12.7 cells away loses its mark", g_Other.bSpecialMark == 0);
    Check("a unit exactly 10 cells away keeps its mark", third.bSpecialMark == 1);
    Check("the selected unit is marked too", g_Unit.bSpecialMark == 1);
    Check("sort key 20 opens screen 6", ui.nUiScreen == 6);
    Check("sort key 20 queues nothing", g_State.nQueueLen == 0);

    /* --- sort key 23: affordability only --- */
    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    obj.uSortKey = PUFO_SPECIAL_SORT_LATCH;
    TactHud_UseSpecialItem(&ctx);
    Check("sort key 23 arms the fire latch", map.bActionLatch == PUFO_ACTION_LATCH_FIRE);
    Check("sort key 23 spends nothing", g_Unit.uTu == 10u);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    map.bActionLatch = 0;
    g_Unit.uTu = 1;
    TactHud_UseSpecialItem(&ctx);
    Check("sort key 23 refuses without the TU",
          g_nRecord == 1 && map.bActionLatch == 0);

    /* --- sort key 21: the medikit --- */
    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    map.nEnemyCount = 0;
    map.nXcomCount = 0;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    g_Unit.bFacing = 0;                       /* north: the cell at (3,2,1) */
    g_Other.pCell = At(3, 2, 1);
    g_Other.bStatus = 0;                      /* operative */
    g_Other.nUnitType = 0;
    g_Other.bKneelOrPrimed = 1;               /* skip the "does not need it" test */
    At(3, 2, 1)->pUnit = &g_Other;
    TactHud_UseSpecialItem(&ctx);
    Check("the medikit enqueues command 18",
          g_State.nQueueLen == 1
          && g_State.aQueue[0].nCmd == TACTCMD_ACTION18);
    Check("the entry carries the target, the mode and the cost",
          g_State.aQueue[0].pTarget == &g_Other
          && g_State.aQueue[0].nMode == PUFO_ACTION_MODE_SPECIAL
          && g_State.aQueue[0].nTuCost == 4);
    Check("the fire target is the cell in front, on the healer's layer",
          ui.nFireTgtX == 3 && ui.nFireTgtY == 2 && ui.nFireTgtZ == 1);
    Check("the queue starts", g_State.bQueueActive == 1);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    g_Unit.bFacing = 0;
    TactHud_UseSpecialItem(&ctx);
    Check("an empty cell in front refuses with records 9 and 10",
          g_nRecord == PUFO_TXTREC_MEDKIT_WHY2 && g_State.nQueueLen == 0);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    g_Unit.bFacing = 0;
    At(3, 2, 1)->pUnit = &g_Other;
    g_Other.nUnitType = 4;                    /* large */
    g_Other.bStatus = 0;
    TactHud_UseSpecialItem(&ctx);
    Check("a large target refuses", g_nRecord == PUFO_TXTREC_MEDKIT_WHY2);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    g_Unit.bFacing = 0;
    At(3, 2, 1)->pUnit = &g_Other;
    g_Other.nUnitType = 0;
    g_Other.bStatus = 2;                      /* not operative */
    TactHud_UseSpecialItem(&ctx);
    Check("an inoperative target refuses", g_nRecord == PUFO_TXTREC_MEDKIT_WHY2);

    /* A civilian on the healer's own cell skips the operative and size tests. */
    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    g_Unit.bFacing = 0;
    civ.pCell = At(3, 3, 1);
    civ.nUnitType = 0;
    civ.uHp = 30;
    civ.bStatus = 5;
    civ.bKneelOrPrimed = 1;
    map.apCivilians[0] = &civ;
    map.nCivilianCount = 1;
    TactHud_UseSpecialItem(&ctx);
    Check("a civilian on the own cell is treated as a target",
          g_State.nQueueLen == 1 && g_State.aQueue[0].pTarget == &civ);
    Check("a civilian target aims at the healer's own cell",
          ui.nFireTgtX == 3 && ui.nFireTgtY == 3 && ui.nFireTgtZ == 1);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    g_Unit.bFacing = 0;
    map.nCivilianCount = 0;
    At(3, 2, 1)->pUnit = &g_Other;
    g_Other.bStatus = 0;
    g_Other.nUnitType = 0;
    g_Other.bKneelOrPrimed = 0;
    g_Other.uHp = 50;
    g_Other.auStat[1] = 50;
    g_Other.bMorale = 100;
    g_Other.uTuSpent = 20;
    TactHud_UseSpecialItem(&ctx);
    Check("a healthy target refuses with records 11 and 12",
          g_nRecord == PUFO_TXTREC_MEDKIT_FIT2 && g_State.nQueueLen == 0);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    g_Unit.bFacing = 0;
    At(3, 2, 1)->pUnit = &g_Other;
    g_Other.bStatus = 0;
    g_Other.nUnitType = 0;
    g_Other.bKneelOrPrimed = 0;
    g_Other.uHp = 50;
    g_Other.auStat[1] = 50;
    g_Other.bMorale = 100;
    g_Other.uTuSpent = 26;                    /* more than uHp/2 */
    TactHud_UseSpecialItem(&ctx);
    Check("spending more than half the HP in TU makes the kit worth it",
          g_State.nQueueLen == 1);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    g_Unit.bFacing = 0;
    At(3, 2, 1)->pWallSprB = Wall(0);
    At(3, 2, 1)->pWallSprB->gap_2f[0] = 1;
    TactHud_UseSpecialItem(&ctx);
    Check("a wall in the facing direction blocks the target search",
          g_nRecord == PUFO_TXTREC_MEDKIT_WHY2 && g_State.nQueueLen == 0);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = &item;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    g_Unit.uTu = 2;
    TactHud_UseSpecialItem(&ctx);
    Check("the medikit gate uses the full cost dword",
          g_nRecord == 1 && g_State.nQueueLen == 0);

    Reset();
    map.pSelUnit = &g_Unit;
    map.pSelWeapon = NULL;
    obj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    TactHud_UseSpecialItem(&ctx);
    Check("no selected weapon stops after the mode write",
          ui.nActionMode == PUFO_ACTION_MODE_SPECIAL && g_nRecord == -1);
}

int main(void)
{
    Reset();
    TestKneel();
    Reset();
    TestClimbUp();
    Reset();
    TestClimbDown();
    Reset();
    TestOpenInventory();
    Reset();
    TestSpecialItem();
    return TestSummary("test_tacthud");
}
