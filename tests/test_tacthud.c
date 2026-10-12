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
#include "game/pufo_boot.h"
#include "game/pufo_font.h"
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

static void ChkEq(i32 nGot, i32 nWant, const char *pWhat)
{
    Check(pWhat, nGot == nWant);
}

static PadImage g_aDigitPad[PUFO_HUD_DIGIT_PADS];
static PadImage *g_apDigitPad[PUFO_HUD_DIGIT_PADS];
static i32 g_nBlits;
static i32 g_aBlitX[32];

static void Dec_Blit(PadImage *pPad, void *pUser)
{
    (void)pUser;
    if (g_nBlits < 32) {
        g_aBlitX[g_nBlits] = pPad->nOriginX;
    }
    g_nBlits += 1;
}

static void TestBlitDecimal(void)
{
    i32 nRet;

    printf("PadImage_BlitDecimal\n");
    for (i32 i = 0; i < PUFO_HUD_DIGIT_PADS; ++i) {
        memset(&g_aDigitPad[i], 0, sizeof(g_aDigitPad[i]));
        g_aDigitPad[i].uWidth = 5;
        g_apDigitPad[i] = &g_aDigitPad[i];
    }

    /* No field width: two digits, one pixel apart. */
    g_nBlits = 0;
    nRet = TactHud_BlitDecimal(g_apDigitPad, 100, 50, 42, 0, false, Dec_Blit, NULL);
    ChkEq(g_nBlits, 2, "  42 blits two digit pads");
    ChkEq(g_aBlitX[0], 100, "  first digit at x");
    ChkEq(g_aBlitX[1], 106, "  next digit at width + 1");
    ChkEq(nRet, 111, "  returns the right edge of the last digit");
    ChkEq(g_aDigitPad[4].nOriginY, 50, "  the pad origin Y is written too");

    /* Field width 5 with the plain filler: three filler pads then the digits. */
    g_nBlits = 0;
    nRet = TactHud_BlitDecimal(g_apDigitPad, 100, 50, 42, 5, false, Dec_Blit, NULL);
    ChkEq(g_nBlits, 5, "  width 5 pads out to five blits");
    ChkEq(g_aBlitX[0], 100, "  filler starts at x");
    ChkEq(g_aBlitX[2], 112, "  third filler at 2 * (width + 1)");
    ChkEq(g_aBlitX[3], 118, "  the first digit follows the filler");
    ChkEq(nRet, 129, "  right edge after the padded field");

    /* With bLeadZero the filler comes from slot 10 instead of slot 0. */
    g_aDigitPad[0].nOriginX = -1;
    g_aDigitPad[10].nOriginX = -1;
    g_nBlits = 0;
    (void)TactHud_BlitDecimal(g_apDigitPad, 200, 50, 7, 4, true, Dec_Blit, NULL);
    ChkEq(g_nBlits, 4, "  three leading pads plus the digit");
    ChkEq(g_aDigitPad[10].nOriginX, 212, "  slot 10 is the leading pad; the last write wins");
    ChkEq(g_aDigitPad[0].nOriginX, -1, "  slot 0 is not used when bLeadZero is set");

    /* A width that is not larger than the digit count adds no filler. */
    g_nBlits = 0;
    (void)TactHud_BlitDecimal(g_apDigitPad, 0, 0, 1234, 3, false, Dec_Blit, NULL);
    ChkEq(g_nBlits, 4, "  width 3 does not pad a four-digit value");

    /* NULL table is tolerated. */
    ChkEq(TactHud_BlitDecimal(NULL, 0, 0, 1, 0, false, Dec_Blit, NULL), 0,
               "  NULL table returns 0");
}

/* --- the HUD draw family ---------------------------------------------------------
 *
 * Menu_DrawLabeledPads @ 0x217e0, TactHud_DrawModalLeft @ 0x21bac,
 * TactHud_DrawModalRight @ 0x21cec, TactHud_CopyStatusText @ 0x21688.
 *
 * What is asserted here is what the functions decide and can be observed: how many pads each
 * one walks, the sparse label-record map, and the modal mode gate. The centring itself is not
 * asserted: Text_DrawString writes into the font layer and is not injected, so a label's final
 * x is not observable from a test. */

static i32 g_nPadBlits;

static void CountBlit(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
    g_nPadBlits += 1;
}

static void TestHudDraw(void)
{
    UiControl aPad[PUFO_HUD_ROSTER_PADS];
    PadImage aImg[PUFO_HUD_ROSTER_PADS];
    SoftSurf surf;
    /* PadImage_Blit reaches the glyph header through pData with no NULL guard, exactly as the
     * original does, so the test must hand it a real one: u16 width, u16 height, then pixels. */
    static u8 aGlyph[4 + 64];
    static u8 aRecs[1112 * PUFO_TEXT_REC_STRIDE];
    static u8 aLabels[PUFO_HUD_MODAL_LABELS * PUFO_HUD_MODAL_LABEL_STRIDE];
    GfxColorEnv color;
    UiModalAnim anim;
    TactHudActionGate gate;
    TactHudBannerState banner;
    UiPanel panelA;
    UiPanel panelB;
    TactHudRoster roster;
    TactHudMarkerEnv marks;
    static u8 aModeNames[PUFO_HUD_ROSTER_PADS * PUFO_HUD_ROSTER_NAME_STRIDE];
    Soldier xcom;
    Soldier enemy;
    Soldier *apXcom[1];
    Soldier *apEnemy[1];
    MapCell selCell;
    MapCell unitCell;
    i32 nScratch;
    i32 i;

    printf("HUD draw family\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &surf;
    g_fontSys.apSprIndex = NULL;

    memset(aRecs, 0, sizeof(aRecs));
    memcpy(aRecs + 5 * PUFO_TEXT_REC_STRIDE, "STATUS", 7);
    memset(aLabels, 0, sizeof(aLabels));
    for (i = 0; i < PUFO_HUD_MODAL_LABELS; ++i) {
        sprintf((char *)(aLabels + i * PUFO_HUD_MODAL_LABEL_STRIDE), "L%d", (int)i);
    }

    memset(&color, 0, sizeof(color));
    memset(&panelA, 0, sizeof(panelA));
    memset(&panelB, 0, sizeof(panelB));

    for (i = 0; i < PUFO_HUD_ROSTER_PADS; ++i) {
        memset(&aPad[i], 0, sizeof(aPad[i]));
        memset(&aImg[i], 0, sizeof(aImg[i]));
        aPad[i].x = (i16)(10 + 20 * i);
        aPad[i].y = (i16)(30 + 10 * i);
        aPad[i].uWidth = 40;
        aPad[i].uHeight = 17;
        aPad[i].nMode = 0;
        aPad[i].bEnabled = 1;
        aPad[i].nState = 0;
        aPad[i].apPads[0] = &aImg[i];
    }

    TestEqI32("menu label record for pad 0 is 18", Menu_LabelRecordIndex(0), 18);
    TestEqI32("menu label record for pad 4 is 22", Menu_LabelRecordIndex(4), 22);
    TestEqI32("menu label record for pad 5 is 402, not 23", Menu_LabelRecordIndex(5), 402);
    TestEqI32("menu label record for pad 6 is 23", Menu_LabelRecordIndex(6), 23);

    g_nPadBlits = 0;
    TestEqI32("Menu_DrawLabeledPads walks seven pads",
              Menu_DrawLabeledPads(aPad, aRecs, &color, CountBlit, CountBlit, NULL), 7);
    TestEqI32("seven pads blitted", g_nPadBlits, 7);

    memset(&anim, 0, sizeof(anim));
    anim.pColor = &color;

    g_nPadBlits = 0;
    nScratch = -1;
    TestEqI32("a modal mode other than 1 is returned unchanged",
              TactHud_DrawModalPanel(aPad, aLabels, 0, 1102, &nScratch, &anim, aRecs,
                                     CountBlit, CountBlit, NULL), 0);
    TestEqI32("eight pads blitted although only seven labels are drawn", g_nPadBlits, 8);
    TestEqI32("no modal dialog, so the action-phase scratch is untouched", nScratch, -1);

    g_nPadBlits = 0;
    nScratch = -1;
    (void)TactHud_DrawModalPanel(aPad, aLabels, 1, 0, &nScratch, &anim, aRecs,
                                 CountBlit, CountBlit, NULL);
    TestEqI32("mode 1 writes 40 into anActionPhaseSave0[112]", nScratch, 40);
    TestEqI32("eight pads blitted again", g_nPadBlits, 8);

    g_nPadBlits = 0;
    TestEqI32("TactHud_CopyStatusText walks nine pads",
              TactHud_CopyStatusText(aPad, (const char *)(aRecs + 5 * PUFO_TEXT_REC_STRIDE),
                                     &color, CountBlit, CountBlit, NULL), 9);
    TestEqI32("nine pads blitted", g_nPadBlits, 9);

    /* PadImage_Blit needs a real glyph, and the pad init loop above wipes aImg, so this goes
     * after it. */
    aGlyph[0] = 8; aGlyph[1] = 0;
    aGlyph[2] = 8; aGlyph[3] = 0;
    aImg[0].uWidth = 8;
    aImg[0].uHeight = 8;
    aImg[0].pData = aGlyph;
    aImg[0].nMode = 0;

    memset(aModeNames, 0, sizeof(aModeNames));
    for (i = 0; i < PUFO_HUD_ROSTER_PADS; ++i) {
        sprintf((char *)(aModeNames + i * PUFO_HUD_ROSTER_NAME_STRIDE), "M%d", (int)i);
    }

    /* TactHud_DrawActionButtons @ 0x21430: the per-button visibility rule. */
    memset(&gate, 0, sizeof(gate));
    gate.nActionMode = 0;
    TestEqI32("mode 0: button 0 is record 396", TactHud_ActionButtonRecord(&gate, 0), 396);
    TestEqI32("mode 0: button 1 is hidden", TactHud_ActionButtonRecord(&gate, 1), -1);
    TestEqI32("mode 0: button 3 is hidden", TactHud_ActionButtonRecord(&gate, 3), -1);
    TestEqI32("mode 0: button 4 is record 1110", TactHud_ActionButtonRecord(&gate, 4), 1110);
    TestEqI32("mode 0: button 5 is record 398", TactHud_ActionButtonRecord(&gate, 5), 398);
    TestEqI32("mode 0: button 6 is hidden", TactHud_ActionButtonRecord(&gate, 6), -1);
    TestEqI32("mode 0: button 8 is record 401, written as 400|1",
              TactHud_ActionButtonRecord(&gate, 8), 401);

    gate.nActionMode = 3;
    gate.nMultiTargetCount = 0;
    TestEqI32("mode 3: button 1 is record 1111, not 1108",
              TactHud_ActionButtonRecord(&gate, 1), 1111);
    TestEqI32("mode 3 with no multi-target: button 2 is hidden",
              TactHud_ActionButtonRecord(&gate, 2), -1);
    TestEqI32("mode 3: button 3 is record 397, written as 396|1",
              TactHud_ActionButtonRecord(&gate, 3), 397);
    gate.nMultiTargetCount = 1;
    TestEqI32("mode 3 with a multi-target: button 2 is record 1109",
              TactHud_ActionButtonRecord(&gate, 2), 1109);

    gate.nActionMode = 1;
    TestEqI32("mode 1: button 1 is record 1108", TactHud_ActionButtonRecord(&gate, 1), 1108);

    /* With mode 0 the visible set is 0, 4, 5, 7, 8. Their y is packed: 19 * (visible so far)
     * + 87, so hiding button 1 moves everything later up by one step. */
    memset(&gate, 0, sizeof(gate));
    for (i = 0; i < PUFO_HUD_ACTION_PADS; ++i) {
        aPad[i].y = (i16)(500 + i);
    }
    g_nPadBlits = 0;
    TestEqI32("TactHud_DrawActionButtons still walks nine pads",
              TactHud_DrawActionButtons(aPad, &gate, aRecs, &color, CountBlit, CountBlit, NULL), 9);
    TestEqI32("only five of the nine are visible", g_nPadBlits, 5);
    TestEqI32("hidden button 1 stays disabled", aPad[1].bEnabled, 0);
    TestEqI32("hidden button 1 keeps its stored y", aPad[1].y, 501);
    TestEqI32("visible button 0 lands at y 87", aPad[0].y, 87);
    TestEqI32("visible button 4 lands at y 106", aPad[4].y, 106);
    TestEqI32("visible button 8 lands at y 163", aPad[8].y, 163);

    /* TactHud_DrawStatusBanner @ 0x1e920 is skipped whole while the fade counter is positive. */
    memset(&banner, 0, sizeof(banner));
    banner.nFadeCount = 3;
    g_nPadBlits = 0;
    TestEqI32("a positive fade count returns it unchanged",
              TactHud_DrawStatusBanner(&aPad[0], NULL, &banner, "footer", "pad", NULL, NULL,
                                       &color, CountBlit, CountBlit, NULL), 3);
    TestEqI32("and nothing is blitted", g_nPadBlits, 0);

    banner.nFadeCount = 0;
    g_nPadBlits = 0;
    TestEqI32("a non-positive fade count draws the banner",
              TactHud_DrawStatusBanner(&aPad[0], &aImg[0], &banner, "footer", "pad", &panelA,
                                       &panelB, &color, CountBlit, CountBlit, NULL), 1);
    TestEqI32("the banner blits its one pad", g_nPadBlits, 1);

    g_nPadBlits = 0;
    TestEqI32("the confirm dialog draws", TactHud_DrawConfirmDialog(&aPad[0], &aImg[0], "pad",
                                                                    "title", "left", "right",
                                                                    &panelA, &panelB, &color,
                                                                    CountBlit, CountBlit, NULL), 1);
    TestEqI32("the dialog blits its one pad", g_nPadBlits, 1);

    /* TactHud_DrawUnitRoster @ 0x21908: twelve modes, only the ones with a non-zero entry drawn,
     * and a drawn pad's y rewritten to 23 * (drawn so far) + 67. */
    memset(&roster, 0, sizeof(roster));
    roster.pSelWeapon = NULL;
    g_nPadBlits = 0;
    TestEqI32("no selected weapon means no roster panel",
              TactHud_DrawUnitRoster(aPad, &roster, &color, CountBlit, CountBlit, NULL), 0);
    TestEqI32("nothing blitted", g_nPadBlits, 0);

    roster.pSelWeapon = aGlyph;
    roster.pNameTable = aModeNames;
    roster.szNumberPrefixA = "A";
    roster.szNumberPrefixB = "B";
    roster.anModeFlag[0] = 4;
    roster.anModeFlag[3] = 7;
    roster.anModeFlag[11] = 2;
    roster.anModeValue[0] = 40;
    roster.anModeValue[3] = 70;
    roster.anModeValue[11] = 20;
    for (i = 0; i < PUFO_HUD_ROSTER_PADS; ++i) {
        aPad[i].y = (i16)(500 + i);
        aPad[i].bEnabled = 1;   /* the action-button test above disabled some of them */
    }
    g_nPadBlits = 0;
    TestEqI32("the roster still walks twelve modes",
              TactHud_DrawUnitRoster(aPad, &roster, &color, CountBlit, CountBlit, NULL), 12);
    TestEqI32("only three modes are drawn", g_nPadBlits, 3);
    TestEqI32("drawn mode 0 lands at y 67", aPad[0].y, 67);
    TestEqI32("drawn mode 3 lands at y 90, not 136", aPad[3].y, 90);
    TestEqI32("drawn mode 11 lands at y 113", aPad[11].y, 113);
    TestEqI32("an undrawn mode keeps its stored y", aPad[5].y, 505);

    /* TactHud_DrawUnitMarkers @ 0x20c58: 11 pixels per cell of difference, centred on x = 120.
     * XCOM uses 11 * (dy + 14) = 11*dy + 154 and enemies 11 * dy + 155, one pixel apart. */
    MapCell_Init(&selCell, 5, 5, 0);
    MapCell_Init(&unitCell, 7, 6, 0);
    memset(&xcom, 0, sizeof(xcom));
    memset(&enemy, 0, sizeof(enemy));
    xcom.pCell = &unitCell;
    xcom.bSpecialMark = 1;
    xcom.nUnitType = 0;
    enemy.pCell = &unitCell;
    enemy.bSpecialMark = 1;
    enemy.nUnitType = 0;
    apXcom[0] = &xcom;
    apEnemy[0] = &enemy;

    memset(&marks, 0, sizeof(marks));
    /* The overlay pad must not be one a control points at: UiControl_BlitActivePad rewrites its
     * origin from the control at the end of the function. */
    marks.pOverlayPad = &aImg[10];
    marks.pLargeMark = &aImg[1];
    marks.pSmallMark = &aImg[2];
    marks.pSelCell = &selCell;
    aImg[1].pData = aGlyph;
    aImg[2].pData = aGlyph;
    aImg[10].pData = aGlyph;

    g_nPadBlits = 0;
    TestEqI32("TactHud_DrawUnitMarkers draws its one pad",
              TactHud_DrawUnitMarkers(&aPad[0], &marks, apXcom, 1, apEnemy, 1,
                                      CountBlit, CountBlit, NULL), 1);
    TestEqI32("the overlay pad is moved to (0,0)", aImg[10].nOriginX, 0);
    TestEqI32("and its y too", aImg[10].nOriginY, 0);

    /* XCOM alone: 11 * (dy + 14) = 11*1 + 154, then -4 for a small unit. */
    (void)TactHud_DrawUnitMarkers(&aPad[0], &marks, apXcom, 1, NULL, 0,
                                  CountBlit, CountBlit, NULL);
    TestEqI32("small XCOM mark x = 11*2 + 120 - 4", aImg[2].nOriginX, 138);
    TestEqI32("small XCOM mark y = 11*(1+14) - 4", aImg[2].nOriginY, 161);

    /* Enemies alone: 11 * dy + 155, then -4. One pixel lower than the XCOM band. */
    aImg[2].nOriginY = -1;
    (void)TactHud_DrawUnitMarkers(&aPad[0], &marks, NULL, 0, apEnemy, 1,
                                  CountBlit, CountBlit, NULL);
    TestEqI32("small enemy mark y = 11*1 + 155 - 4", aImg[2].nOriginY, 162);

    /* A large unit takes the large mark, offset 2 for XCOM and 8 for the enemy. Unit type 4 is
     * the large one in g_aUnitTypeIsLarge, which is a const table. */
    xcom.nUnitType = 4;
    enemy.nUnitType = 4;
    (void)TactHud_DrawUnitMarkers(&aPad[0], &marks, apXcom, 1, NULL, 0,
                                  CountBlit, CountBlit, NULL);
    TestEqI32("large XCOM mark x = 142 - 2", aImg[1].nOriginX, 140);
    TestEqI32("large XCOM mark y = 165 - 2", aImg[1].nOriginY, 163);
    (void)TactHud_DrawUnitMarkers(&aPad[0], &marks, NULL, 0, apEnemy, 1,
                                  CountBlit, CountBlit, NULL);
    TestEqI32("large enemy mark x = 142 - 8", aImg[1].nOriginX, 134);
    TestEqI32("large enemy mark y = 166 - 8", aImg[1].nOriginY, 158);
    xcom.nUnitType = 0;
    enemy.nUnitType = 0;

    /* An unmarked unit draws nothing. */
    xcom.bSpecialMark = 0;
    enemy.bSpecialMark = 0;
    aImg[2].nOriginX = -1;
    (void)TactHud_DrawUnitMarkers(&aPad[0], &marks, apXcom, 1, apEnemy, 1,
                                  CountBlit, CountBlit, NULL);
    TestEqI32("an unmarked unit leaves the mark alone", aImg[2].nOriginX, -1);

    g_pMainSurface = NULL;
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    Reset();
    TestKneel();
    Reset();
    TestBlitDecimal();
    Reset();
    TestClimbUp();
    Reset();
    TestClimbDown();
    Reset();
    TestOpenInventory();
    Reset();
    TestSpecialItem();
    Reset();
    TestHudDraw();
    return TestSummary("test_tacthud");
}
