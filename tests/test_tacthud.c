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
#include "ui/pufo_uigeom.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_unit.h"
#include "game/pufo_boot.h"
#include "game/pufo_font.h"
#include "pufo_common.h"

#include <stdio.h>
#include <string.h>

#include "pufo_test.h"
#include "util/pufo_str.h"

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

/* TactHud_OpenInventory @ 0x16ef4 (the original's name is Inv_OpenScreen). Plays sample 16 only when
 * LOBYTE(apUiSlotsA[2]) at +0x68 is non-zero; needs nXcomUnitCount non-zero and pSelUnit->nUnitType <= 3 or it
 * opens nothing at all. On the open path: nInvListSel = 0xFF (the Map selection byte at +0x4A26),
 * nInvFilterType = 0 (+0x1A22), the rebuild runs, the All pad (pBank + 0x30A) lights and the other six pad
 * globals go dark, Map_bActionLatch is cleared, and the cursor is re-stamped at its own position. Screen id
 * 1 is the observable half of the gate. */
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

    /* The gate is `nUnitType <= 3` at 0x16F30-ish: type 3 is the last one that opens, 4 the first refused. */
    Reset();
    ctx.nXcomCount = 4;
    g_Unit.nUnitType = 3;
    Check("unit type 3 still opens the inventory",
          TactHud_OpenInventory(&ctx) && ui.nUiScreen == 1);
    g_Unit.nUnitType = 4;
    ui.nUiScreen = 0;
    Check("unit type 4 is the first one refused",
          !TactHud_OpenInventory(&ctx) && ui.nUiScreen == 0);

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
    /* PadImage_BlitDecimal @ 0x36c58. The digit table is 11 PadImage* at ctx + 0xCC: the character byte
     * picks slots 0..9 (0x36D28 computes `ctx + 4*ch - 192`, and 4*'0' - 192 is 0), and the filler is
     * slot 10 when bLeadZero is set, slot 0 otherwise (0x36CB0). Both filler and digits advance by
     * uWidth + 1 (0x36CF8 for the filler, the for-increment at 0x36D14 for the digits); the return value
     * is the last digit's x + uWidth, with no trailing +1 (0x36D54). */
    ChkEq(g_nBlits, 2, "  42 blits two digit pads @ 0x36c58");
    ChkEq(g_aBlitX[0], 100, "  first digit at x @ 0x36c58");
    ChkEq(g_aBlitX[1], 106, "  next digit at width + 1 @ 0x36c58");
    ChkEq(nRet, 111, "  returns x + uWidth of the last digit, not +1 @ 0x36c58");
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
    ChkEq(g_aDigitPad[10].nOriginX, 212, "  slot 10 is the leading pad when bLeadZero is set @ 0x36c58");
    ChkEq(g_aDigitPad[0].nOriginX, -1, "  ... and slot 0 is the filler only when it is clear @ 0x36c58");

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
static u16 *g_pTestPix;

/* A pixel of the test surface. The surface is 16bpp, so the stride is two bytes. */
static i32 PixAt(i32 x, i32 y)
{
    return (i32)g_pTestPix[y * 240 + x];
}

/* Text_DrawString reaches glyphs through g_fontSys.apSprIndex, and Font_DrawGlyph returns
 * immediately when that is NULL. Without a font no text position is observable, so the top-bar test
 * installs a minimal one: every code point is the same one-pixel glyph, eight pixels wide.
 *
 * The glyph format is a command stream, not a bitmap: byte 0 is the start Y offset, byte 1 the start
 * X offset, then commands until 0xFF. 0xFE starts a new row. */
static u8 g_aFakeGlyph[4] = { 0, 0, 0x30, 0xFF };
static u8 *g_apFakeSpr[256];

static void InstallFakeFont(void)
{
    for (i32 i = 0; i < 256; ++i) {
        g_apFakeSpr[i] = g_aFakeGlyph;
        g_fontMetrics.aAdvance[i] = 8;
    }
    g_fontSys.apSprIndex = g_apFakeSpr;
    g_fontSys.nSprIndexCount = 256;
    g_fontSys.nGlyphBase = 0;

    /* Font_DrawGlyph writes pPal[nRun] with nPal 0, and Text_DrawString @ 0x2d798 forces the glyph
     * palette base to 2 while it draws (the original's text-colour trick: Gfx_PackRgb565Pair writes
     * the pair into slot 2 entries 27/33), so palette slot 2 needs a non-zero entry at the index
     * the fake glyph uses. */
    memset(g_aPalette[2], 0x7F, sizeof(g_aPalette[2]));

    /* Font_BlitGlyph refuses to draw unless the blit state's pixel mode is 1 or 2, and
     * Blit_LoadSurface does not set that field - only Blit_SetTargetParams does. Nothing in this
     * fixture calls the latter, so the mode has to be stated here. */
    g_blitState.nPixelMode = 2;
}

static void CountBlit(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
    g_nPadBlits += 1;
}

static void TestHudDraw(void)
{
    UiControl aPad[PUFO_HUD_TOPBAR_PADS];
    PadImage aImg[PUFO_HUD_TOPBAR_PADS];
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

    for (i = 0; i < PUFO_HUD_TOPBAR_PADS; ++i) {
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
    TestEqI32("the descriptor tail rows stay at 152 + 23 * i portrait", aPad[4].y, 152);
    TestEqI32("  through the last", aPad[6].y, 198);

    /* QoL: the wide face centre-shifts the tail rows the drawer owns. */
    PufoUi_SetGeometry(40, -80);
    aPad[4].y = 152; aPad[5].y = 175; aPad[6].y = 198;
    (void)Menu_DrawLabeledPads(aPad, aRecs, &color, CountBlit, CountBlit, NULL);
    TestEqI32("wide tail rows ride at 112 + 23 * i", aPad[4].y, 112);
    TestEqI32("  through the last", aPad[6].y, 158);
    PufoUi_SetGeometry(0, 0);

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

    /* TactHud_DrawConfirmDialog @ 0x1eb68: a backdrop pad, one bank pad (widget 129), the pad label in cyan
     * (26,244,252 over 13,122,126), a title centred at y 10 and two fixed side labels at (10, 30) and
     * (170, 30) in green, then the two message panels, and the return is 1. With the fake font installed every
     * code point is one pixel and advances 8, so the three text positions are observable: "title" is 5 glyphs
     * = 40 wide and centres at (240 - 40) / 2 = 100. */
    InstallFakeFont();
    g_pTestPix = (u16 *)surf.pPixels;
    memset(surf.pPixels, 0, (size_t)240 * 320 * sizeof(u16));
    g_nPadBlits = 0;
    TestEqI32("the confirm dialog draws", TactHud_DrawConfirmDialog(&aPad[0], &aImg[0], "pad",
                                                                    "title", "left", "right",
                                                                    &panelA, &panelB, &color,
                                                                    CountBlit, CountBlit, NULL), 1);
    TestEqI32("the dialog blits its one pad", g_nPadBlits, 1);
    TestCheck("the title is centred at x 100 on y 10", PixAt(100, 10) != 0);
    TestCheck("  and nothing is drawn left of it", PixAt(99, 10) == 0);
    TestCheck("the left label sits at the constant (10, 30)", PixAt(10, 30) != 0);
    TestCheck("  with (9, 30) empty", PixAt(9, 30) == 0);
    TestCheck("the right label sits at the constant (170, 30)", PixAt(170, 30) != 0);
    TestCheck("  with (169, 30) empty", PixAt(169, 30) == 0);
    g_fontSys.apSprIndex = NULL;

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

    /* TactHud_DrawTopBar @ 0x20ef8.
     *
     * The pad spacing is re-done for this one: the earlier layout puts x at 10 + 20*i, which runs
     * off the 240-pixel screen past pad 11, and this function labels pads up to 27. */
    for (i = 0; i < PUFO_HUD_TOPBAR_PADS; ++i) {
        aPad[i].x = (i16)(10 + 8 * i);
        aPad[i].y = (i16)(30 + 10 * i);
    }

    InstallFakeFont();
    memset(surf.pPixels, 0, (size_t)surf.nWidth * (size_t)surf.nHeight * 2u);
    g_pTestPix = (u16 *)surf.pPixels;

    memcpy(aRecs + 32 * PUFO_TEXT_REC_STRIDE, "AB", 3);
    memcpy(aRecs + 1043 * PUFO_TEXT_REC_STRIDE,
           "012345678901234567890123456789", 31); /* 30 chars, 240 pixels wide */
    memcpy(aRecs + 391 * PUFO_TEXT_REC_STRIDE, "X", 2);
    memcpy(aRecs + 395 * PUFO_TEXT_REC_STRIDE, "Y", 2);

    g_nPadBlits = 0;
    TestEqI32("TactHud_DrawTopBar returns the last label loop's count",
              TactHud_DrawTopBar(aPad, aRecs, &color, CountBlit, CountBlit, NULL), 5);
    TestEqI32("29 pads blitted", g_nPadBlits, 29);

    /* Record 32 is "AB": 16 pixels wide, centred at (240 - 16) / 2 = 112. */
    TestCheck("record 32 is centred on the screen", PixAt(112, 0) != 0);
    TestCheck("its second glyph lands eight pixels later", PixAt(120, 0) != 0);

    /* Record 1043 is 240 pixels wide. Drawn at the constant x 40 it starts at 40; if it were
     * centred it would start at 0. This is the assertion that separates the two groups. */
    TestCheck("record 1043 is drawn at the constant x 40", PixAt(40, 200) != 0);
    TestCheck("and not centred, which would put it at x 0", PixAt(0, 200) == 0);

    /* Records 391..395 follow at x 40 in steps of 20. */
    TestCheck("record 391 lands at y 220", PixAt(40, 220) != 0);
    TestCheck("record 395 lands at y 300", PixAt(40, 300) != 0);

    /* Labels are (index % 5 + 1) centred on a fixed 23, so a one-digit label of width 8 lands at
     * pad x + (23 - 8) / 2 = pad x + 7, at pad y + 4. Pad 0 is at (10, 30). */
    TestCheck("pad 0 gets a label at 10 + 7", PixAt(17, 34) != 0);

    /* Pad 10 is in the blit loop but past its label cutoff. Its own slot in THIS layout is
     * x 10 + 8*10 = 90, y 30 + 10*10 = 130, so a label would land at 90 + 7, 134 -- probing the
     * old 10 + 20*i x (210) would stay green whatever the cutoff does. */
    TestCheck("pad 10 is blitted but never labelled", PixAt(10 + 8 * 10 + 7, 130 + 4) == 0);

    /* Pads 18 and 27 are labelled only by the two later loops, which blit nothing. */
    TestCheck("pad 18 is labelled by the second loop", PixAt(154 + 7, 210 + 4) != 0);
    TestCheck("pad 27 is labelled by the third loop", PixAt(226 + 7, 300 + 4) != 0);

    g_pMainSurface = NULL;
}

/* A probe font whose glyph k draws its single run at y + k, so the inked row reports the glyph index
 * as well as the position. Same trick as test_isodraw.c. */
static u8   g_aProbeGlyph[256][4];
static u8  *g_apProbeSpr[256];
static i32  g_nDecBlits;
static i32  g_aDecX[64];
static i32  g_aDecY[64];
static i32  g_nCtlBlits;
static i32  g_nFillerBlits;

/* PadImage_BlitEx has no NULL guard, matching the original, so the HUD pad needs real data. */
static u8 g_aHudPadData[8];

static void PanelDecBlit(PadImage *pPad, void *pUser)
{
    (void)pUser;
    if (g_nDecBlits < 64) {
        g_aDecX[g_nDecBlits] = pPad->nOriginX;
        g_aDecY[g_nDecBlits] = pPad->nOriginY;
    }
    /* Slot 10 is the leading-zero filler pad, slot 0 the plain zero digit. */
    if (pPad == &g_aDigitPad[10]) {
        g_nFillerBlits += 1;
    }
    g_nDecBlits += 1;
}

static void PanelCtlBlit(const PadImage *pPad, void *pUser)
{
    (void)pPad;
    (void)pUser;
    g_nCtlBlits += 1;
}

static i32 PanelInkRow(const SoftSurf *pSurf, i32 nCol)
{
    const u16 *p = (const u16 *)pSurf->pPixels;

    for (i32 y = 0; y < pSurf->nHeight; ++y) {
        if (p[y * pSurf->nWidth + nCol] != 0) {
            return y;
        }
    }
    return -1;
}

static void TestSelectedUnitPanel(void)
{
    UiControl  aBank[PUFO_HUD_PANEL_CONTROLS];
    PadImage   aBankPad[PUFO_HUD_PANEL_CONTROLS];
    PadImage   hudPad;
    TactHudPanelEnv env;
    TactItem   handL, handR;
    static const i32 nGamma1 = 1;   /* the gamma this fixture states: 0.1*1+0.7 = 1.1 */
    GfxColorEnv color = { &nGamma1, 0 };
    SoftSurf   surf;

    printf("TactHud_DrawSelectedUnitPanel @ 0x1d5ec\n");

    for (i32 i = 0; i < 256; ++i) {
        g_aProbeGlyph[i][0] = (u8)i;
        g_aProbeGlyph[i][1] = 0;
        g_aProbeGlyph[i][2] = 0x30;
        g_aProbeGlyph[i][3] = 0xFF;
        g_apProbeSpr[i] = g_aProbeGlyph[i];
    }
    g_fontSys.apSprIndex = g_apProbeSpr;
    g_fontSys.nSprIndexCount = 256;
    g_fontSys.nGlyphBase = 0;
    memset(g_aPalette[0], 0x7F, sizeof(g_aPalette[0]));
    g_blitState.nPixelMode = 2;

    for (i32 i = 0; i < PUFO_HUD_DIGIT_PADS; ++i) {
        memset(&g_aDigitPad[i], 0, sizeof(g_aDigitPad[i]));
        g_aDigitPad[i].uWidth = 5;
        g_apDigitPad[i] = &g_aDigitPad[i];
    }

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &surf;

    memset(aBank, 0, sizeof(aBank));
    memset(aBankPad, 0, sizeof(aBankPad));
    for (i32 i = 0; i < PUFO_HUD_PANEL_CONTROLS; ++i) {
        aBank[i].bEnabled = 1;
        aBank[i].apPads[0] = &aBankPad[i];
        aBank[i].apPads[1] = &aBankPad[i];
        aBank[i].apPads[2] = &aBankPad[i];
    }

    memset(&hudPad, 0, sizeof(hudPad));
    hudPad.nOriginX = 10;
    hudPad.uWidth = 2;
    hudPad.uHeight = 90;
    hudPad.pData = g_aHudPadData;

    memset(&env, 0, sizeof(env));
    env.pHudPad = &hudPad;
    env.apDigitPads = g_apDigitPad;
    env.bPlayerInputEnabled = 1;
    env.nBigObsBase = 20;
    env.nAnimMode = 3; /* no bar, so the first assertions see a clean surface */

    Reset();
    g_Unit.uTu = 45;
    g_Unit.uStunOrArmor = 7;
    g_Unit.uHp = 88;
    g_Unit.bMorale = 6;
    env.pSelUnit = &g_Unit;

    memset(&g_aObj[0], 0, sizeof(g_aObj[0]));
    memset(&g_aObj[1], 0, sizeof(g_aObj[1]));
    g_aObj[0].nFootW = 2;  g_aObj[0].nFootH = 3;
    g_aObj[0].uIconId = 3; g_aObj[0].bAmmoCap = 30;
    g_aObj[1] = g_aObj[0];
    g_aObj[1].uIconId = 4;
    memset(&handL, 0, sizeof(handL));
    handL.pObj = &g_aObj[0];
    handL.nAmmo = 12;
    memset(&handR, 0, sizeof(handR));
    handR.pObj = &g_aObj[1];
    handR.nAmmo = 7;
    g_Unit.pHandL = &handL;
    g_Unit.pHandR = &handR;

    /* The re-base runs before every gate. */
    env.bDeployPanelOpen = 1;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    ChkEq(hudPad.nOriginY, 303, "the deploy panel pins the HUD pad at 303");
    ChkEq(aBank[0].y, 20 + 303, "nextu is re-based to its own row plus 303");
    ChkEq(aBank[18].y, 39 + 303, "the last unnamed control uses row 39");

    env.bDeployPanelOpen = 0;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    ChkEq(hudPad.nOriginY, 230, "closed, the pad sits at 320 minus its 90 pixel height");
    ChkEq(aBank[13].y, 73 + 230, "autos keeps its own row 73");

    env.bPlayerInputEnabled = 0;
    Check("player input off skips the panel",
          !TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                         PanelCtlBlit, PanelCtlBlit, NULL));
    ChkEq(aBank[0].y, 20 + 230, "but the re-base still ran");
    env.bPlayerInputEnabled = 1;

    env.pSelUnit = NULL;
    Check("no selected unit skips the panel",
          !TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                         PanelCtlBlit, PanelCtlBlit, NULL));
    env.pSelUnit = &g_Unit;

    g_Unit.bStatus = 1;
    Check("a non-operative unit skips the panel",
          !TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                         PanelCtlBlit, PanelCtlBlit, NULL));
    g_Unit.bStatus = 0;

    /* A full draw. Only the two weapon glyphs reach the surface: the decimals and the controls go
     * through the injected callbacks. */
    memset(surf.pPixels, 0, (size_t)surf.nWidth * (size_t)surf.nHeight * 2u);
    g_nDecBlits = 0;
    g_nCtlBlits = 0;
    g_nFillerBlits = 0;
    Check("a valid unit draws the panel",
          TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL));
    ChkEq(g_nCtlBlits, PUFO_HUD_PANEL_CONTROLS, "19 bank controls are blitted");

    /* Left icon: index 3 + 20 - 1 = 22 at (10 + 9, 230 + 41), so the probe inks row 271 + 22.
     * Right icon: index 4 + 20 - 1 = 23 at (10 + 198, 271). */
    ChkEq(PanelInkRow(&surf, 19), 271 + 22,
          "the left icon is uIconId + bigobs - 1 drawn at the centred left column");
    ChkEq(PanelInkRow(&surf, 208), 271 + 23,
          "the right icon uses the same rule at column 198");

    /* Six decimal groups, in call order: left ammo 2, right ammo 2, time units 3, stun 3, hit points
     * 3, morale 3. Digit pads are 5 wide, so the advance is 6. */
    ChkEq(g_nDecBlits, 16, "ammo twice each, then four status values with leading-zero fillers");
    ChkEq(g_aDecX[0], 10 + 22,  "left ammo starts at origin plus 22");
    ChkEq(g_aDecY[0], 230 + 42, "and at origin plus 42");
    ChkEq(g_aDecX[2], 10 + 211, "right ammo starts at origin plus 211");
    ChkEq(g_aDecX[4], 10 + 109, "time units at origin plus 109");
    ChkEq(g_aDecY[4], 230 + 60, "on the first status row");
    ChkEq(g_aDecX[7], 10 + 109, "stun and armor below it on the second row");
    ChkEq(g_aDecY[7], 230 + 75, "at origin plus 75");
    ChkEq(g_aDecX[10], 10 + 160, "hit points at origin plus 160");
    ChkEq(g_aDecY[10], 230 + 60, "back on the first row");
    ChkEq(g_aDecX[13], 10 + 160, "morale shares the second row on the right");

    /* A weapon that never runs out has no clip to show. */
    g_aObj[0].bAmmoCap = 255;
    g_aObj[1].bAmmoCap = 255;
    g_nDecBlits = 0;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    ChkEq(g_nDecBlits, 12, "bAmmoCap 255 removes both ammo numbers and leaves the four status ones");
    g_aObj[0].bAmmoCap = 30;
    g_aObj[1].bAmmoCap = 30;

    /* Two-handed and category 3 or above hides the number unless the unit is large. */
    g_aObj[0].bTwoHanded = 1;
    g_aObj[0].nCategory = 5;
    g_aObj[1].bTwoHanded = 1;
    g_aObj[1].nCategory = 5;
    g_nDecBlits = 0;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    ChkEq(g_nDecBlits, 12, "a two-handed heavy weapon on a small unit shows no ammo");
    g_aObj[0].bTwoHanded = 0;
    g_aObj[0].nCategory = 1;
    g_aObj[1].bTwoHanded = 0;
    g_aObj[1].nCategory = 1;

    /* The suppressed time-unit flag draws a literal zero. The group is three pads either way, so what
     * changes is how many of them are filler: 45 is one filler and two digits, 0 is two fillers and a
     * digit. */
    g_nFillerBlits = 0;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    ChkEq(g_nFillerBlits, 7, "0 + 1 + 1 + 2 + 1 + 2 filler pads across the six groups");

    env.bTuSuppressed = 1;
    g_nFillerBlits = 0;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    ChkEq(g_nFillerBlits, 8, "suppressed time units become 0 and take one more filler pad");
    env.bTuSuppressed = 0;

    /* The bar: mode 1 draws it, mode 3 draws nothing at all. */
    memset(surf.pPixels, 0, (size_t)surf.nWidth * (size_t)surf.nHeight * 2u);
    env.nAnimMode = 1;
    env.nAnimCounter = 15;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    Check("mode 1 draws the bar row",
          PanelInkRow(&surf, 10 + 85) == 230 + 13);
    Check("and the 12 segments cover the last one",
          PanelInkRow(&surf, 10 + 85 + 69) == 230 + 13);
    Check("the 6 pixel pitch leaves a gap between segments",
          PanelInkRow(&surf, 10 + 85 + 4) == -1);

    memset(surf.pPixels, 0, (size_t)surf.nWidth * (size_t)surf.nHeight * 2u);
    env.nAnimMode = 3;
    env.nAnimCounter = 15;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    Check("mode 3 draws no bar",
          PanelInkRow(&surf, 10 + 85) == -1
          && PanelInkRow(&surf, 10 + 85 + 69) == -1);

    /* The rollover. The next mode is random, so only the reset and the range are assertable. */
    env.nAnimMode = 0;
    env.nAnimCounter = 128;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    ChkEq(env.nAnimCounter, 0, "a counter of 128 resets");
    Check("and the next mode is drawn at random from 0..2",
          env.nAnimMode >= 0 && env.nAnimMode <= 2);

    env.nAnimMode = 2;
    env.nAnimCounter = 119;
    (void)TactHud_DrawSelectedUnitPanel(aBank, &env, &color, PanelDecBlit, NULL,
                                        PanelCtlBlit, PanelCtlBlit, NULL);
    ChkEq(env.nAnimCounter, 119, "mode 2 runs to 119, not 127");

    g_pMainSurface = NULL;
    g_fontSys.apSprIndex = NULL;
}

/* A probe font: glyph k starts k pixels below the requested y, so the inked row reports the index the
 * function asked for. One assertion then pins both the icon index and its position. */
static u8  g_aProbeGlyph[256][4];
static u8 *g_apProbeSpr[256];

static void InstallProbeFont(void)
{
    for (i32 i = 0; i < 256; ++i) {
        g_aProbeGlyph[i][0] = (u8)i;   /* start Y offset */
        g_aProbeGlyph[i][1] = 0;       /* start X offset */
        g_aProbeGlyph[i][2] = 0x30;    /* one run */
        g_aProbeGlyph[i][3] = 0xFF;    /* end of stream */
        g_apProbeSpr[i] = g_aProbeGlyph[i];
        g_fontMetrics.aAdvance[i] = 8;
    }
    g_fontSys.apSprIndex = g_apProbeSpr;
    g_fontSys.nSprIndexCount = 256;
    g_fontSys.nGlyphBase = 0;
    memset(g_aPalette[0], 0x7F, sizeof(g_aPalette[0]));
    g_blitState.nPixelMode = 2;
}

static void ClearStrip(void)
{
    memset(g_pTestPix, 0, (size_t)240 * 320 * sizeof(u16));
}

/* Ink inside one strip column's glyph cell: the anchor is x 38*col + 24, y 265 and the centring pair
 * can move the glyph by +/- 9, so a cell is probed rather than the anchor pixel. The count row (301)
 * is left out because the neighbouring column's own count overlaps this x range. A column that must
 * stay empty stays green only while nothing at all is drawn in it, whatever entry leaked there. */
static i32 StripColumnInk(i32 nCol)
{
    i32 nInk = 0;
    for (i32 x = 38 * nCol + 12; x < 38 * nCol + 44; ++x) {
        for (i32 y = 265; y < 301; ++y) {
            if (PixAt(x, y) != 0) {
                ++nInk;
            }
        }
    }
    return nInk;
}

static void TestItemStrip(void)
{
    static TactUiState ui;
    static ObjectRec aObj[10];
    static TactItem  aItem[10];
    static TactItem  sentinelSlot;   /* stand-in for unk_138EA0 */
    static u8 aHandData[8];
    TactHudItemStripEnv env;
    GfxColorEnv color;
    PadImage emptyHand;
    Soldier unit;
    SoftSurf surf;
    i32 i;

    printf("Item strip @ 0x2cdac\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &surf;
    g_pTestPix = (u16 *)surf.pPixels;
    InstallProbeFont();

    /* Sort key 5 is answered zero by both TactItem_GetHandSlot and TactItem_GetEquipDelta, and a 2 x 3
     * footprint makes the centring pair zero, so every anchor shows up unshifted. */
    for (i = 0; i < 10; ++i) {
        memset(&aObj[i], 0, sizeof(aObj[i]));
        aObj[i].uIconId  = (u16)(i + 1);
        aObj[i].nFootW   = 2;
        aObj[i].nFootH   = 3;
        aObj[i].uSortKey = 5;
        memset(&aItem[i], 0, sizeof(aItem[i]));
        aItem[i].pObj = &aObj[i];
    }
    sentinelSlot.pObj = &aObj[0];

    memset(&ui, 0, sizeof(ui));
    memset(&unit, 0, sizeof(unit));
    memset(&color, 0, sizeof(color));
    memset(&emptyHand, 0, sizeof(emptyHand));
    aHandData[0] = 4; aHandData[1] = 0; aHandData[2] = 0; aHandData[3] = 0;
    emptyHand.pData  = aHandData;
    emptyHand.uWidth = 4;

    memset(&env, 0, sizeof(env));
    env.pUi           = &ui;
    env.pSelUnit      = &unit;
    env.pEmptyHandPad = &emptyHand;
    env.pEmptySlot    = &sentinelSlot;
    env.nBigObsBase   = 10;

    /* Four strip columns: x 38*col + 24, y 265, index uIconId + 10 - 1. */
    ClearStrip();
    ui.nInvListCount = 4;
    for (i = 0; i < 4; ++i) {
        ui.apInvList[i] = &aItem[i];
    }
    TactHud_DrawItemStrip(&env, &color);
    Check("strip column 0 draws at x 24, y 265 with index 10", PixAt(24, 275) != 0);
    Check("the inked row is the glyph index, not a fixed offset", PixAt(24, 276) == 0);
    Check("strip column 1 is 38 pixels right", PixAt(62, 276) != 0);
    Check("strip column 2", PixAt(100, 277) != 0);
    Check("strip column 3", PixAt(138, 278) != 0);

    /* The column loop stops as soon as col + filter reaches nInvListCount. */
    ClearStrip();
    ui.nInvListCount = 2;
    TactHud_DrawItemStrip(&env, &color);
    Check("two entries still draw the first two columns", PixAt(24, 275) != 0 && PixAt(62, 276) != 0);
    Check("and stop there", StripColumnInk(2) == 0 && StripColumnInk(3) == 0);

    /* nInvFilterType is a scroll offset into the same list. */
    ClearStrip();
    ui.nInvFilterType  = 1;
    ui.nInvListCount   = 3;
    TactHud_DrawItemStrip(&env, &color);
    Check("with the filter at 1 the first column shows the second entry", PixAt(24, 276) != 0);
    Check("and the third entry lands in the first column's slot", PixAt(62, 277) != 0);
    Check("nothing is drawn past the end of the list", StripColumnInk(2) == 0 && StripColumnInk(3) == 0);
    ui.nInvFilterType = 0;

    /* Both hands. */
    ClearStrip();
    unit.pHandR = &aItem[4];   /* icon 5, index 14 */
    unit.pHandL = &aItem[5];   /* icon 6, index 15 */
    TactHud_DrawItemStrip(&env, &color);
    Check("the right hand draws at x 124, y 63", PixAt(124, 77) != 0);
    Check("the left hand at x 5", PixAt(5, 78) != 0);
    Check("with both hands full no empty-hand marker is placed", emptyHand.nOriginX == 0);

    /* One hand holds a bListPri 1 item and the other is empty: mark the empty one. */
    ClearStrip();
    unit.pHandR = NULL;
    aObj[5].bListPri = 1;
    TactHud_DrawItemStrip(&env, &color);
    ChkEq(emptyHand.nOriginX, 126, "an empty right hand is marked at x 126");
    ChkEq(emptyHand.nOriginY, 64, "and y 64");

    ClearStrip();
    aObj[5].bListPri = 0;
    emptyHand.nOriginX = 0;
    emptyHand.nOriginY = 0;
    TactHud_DrawItemStrip(&env, &color);
    Check("a two-handed item in the left hand marks nothing", emptyHand.nOriginX == 0 && emptyHand.nOriginY == 0);

    ClearStrip();
    unit.pHandL = NULL;
    unit.pHandR = &aItem[4];
    aObj[4].bListPri = 1;
    emptyHand.nOriginX = 0;
    TactHud_DrawItemStrip(&env, &color);
    ChkEq(emptyHand.nOriginX, 7, "an empty left hand is marked at x 7");
    unit.pHandR = NULL;
    aObj[4].bListPri = 0;

    /* Backpack 2 x 4, belt 3 x 3, and the sentinel slot. */
    ClearStrip();
    unit.apBackpack[0] = &aItem[6];   /* icon 7, index 16 -> 163 + 16 = 179 */
    unit.apBackpack[1] = &sentinelSlot;
    unit.apBelt[5]     = &aItem[7];   /* row 1 col 2: x 19*2 + 172 = 210, y 19 + 25 = 44, index 17 */
    TactHud_DrawItemStrip(&env, &color);
    Check("backpack row 0 col 0 draws at x 162, y 163", PixAt(162, 179) != 0);
    Check("a slot holding the 0x138EA0 sentinel draws nothing", PixAt(181, 173) == 0);
    Check("belt row 1 col 2 draws at x 210, y 44", PixAt(210, 61) != 0);
    unit.apBackpack[0] = NULL;
    unit.apBackpack[1] = NULL;
    unit.apBelt[5] = NULL;

    /* Pockets are visited 2, 3, 0, 1 and pocket 2 sits one pixel left of pocket 0. */
    ClearStrip();
    unit.aPockets[2].apItem[0] = &aItem[8];  /* icon 9, index 18 -> 163 + 18 = 181 */
    unit.aPockets[0].apItem[0] = &aItem[9];  /* icon 10, index 19 -> 25 + 19 = 44 */
    TactHud_DrawItemStrip(&env, &color);
    Check("pocket 2 col 0 draws at x 23, y 163", PixAt(23, 181) != 0);
    Check("pocket 0 col 0 draws at x 24, y 25 - one pixel right of pocket 2", PixAt(24, 44) != 0);
    Check("and pocket 2 is not at 24", PixAt(24, 181) == 0);
    unit.aPockets[2].apItem[0] = NULL;
    unit.aPockets[0].apItem[0] = NULL;

    /* Footprint centring, and the truncation of a negative half. */
    ClearStrip();
    ui.nInvListCount = 1;
    ui.apInvList[0]  = &aItem[0];
    aObj[0].nFootW   = 3;
    TactHud_DrawItemStrip(&env, &color);
    Check("a 3-wide footprint shifts the icon left by 19/2 truncated to 9", PixAt(15, 275) != 0);
    Check("and no longer at the unshifted x", PixAt(24, 275) == 0);
    aObj[0].nFootW = 2;

    /* The count under each column is right aligned, so a wider number starts further left. */
    ClearStrip();
    InstallFakeFont();
    ui.nInvListCount = 1;
    ui.apInvList[0]  = &aItem[0];
    ui.anInvCount[0] = 42;
    TactHud_DrawItemStrip(&env, &color);
    Check("a two digit count starts at x 36 on row 301", PixAt(36, 301) != 0);
    Check("and its second digit eight pixels right", PixAt(44, 301) != 0);
    Check("nothing is drawn left of the number", PixAt(28, 301) == 0);
}

static char s_aInvRank[9][PUFO_RANK_NAME_STRIDE];

static void TestInventoryScreen(void)
{
    static TactUiState ui;
    static PadImage aPortrait[16];
    static PadImage *apPortrait[16];
    static u8 aPortraitData[16][6];
    static PadImage invPad;
    static u8 aInvPadData[6];
    static PadImage aCtlPad[PUFO_HUD_INV_CONTROLS];
    static PadImage aHandPad;
    static u8 aHandData[6];
    static ObjectRec obj;
    static TactItem item;
    static UiControl aCtl[PUFO_HUD_INV_CONTROLS];
    TactHudInventoryEnv env;
    InvHitState hit;
    GfxColorEnv color;
    Soldier unit;
    SoftSurf surf;
    i32 i;

    printf("Inventory screen @ 0x1f58c\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &surf;
    g_pTestPix = (u16 *)surf.pPixels;
    InstallFakeFont();

    for (i = 0; i < 9; ++i) {
        pufo_strcpy(s_aInvRank[i], "Squad");
    }

    memset(&unit, 0, sizeof(unit));
    unit.uTu = 25;
    unit.auStat[8] = 20;
    unit.uInvBurden = 1;
    pufo_strcpy(unit.szName, "Hallow");

    memset(&obj, 0, sizeof(obj));
    pufo_strcpy(obj.szName, "RIF");
    obj.nFootW = 2;
    obj.nFootH = 3;
    obj.bTwoHanded = 0;
    obj.nCategory = 0;
    obj.bAmmoCap = 30;
    obj.uIconId = 4;

    memset(&item, 0, sizeof(item));
    item.pObj = &obj;
    item.nAmmo = 7;

    memset(&hit, 0, sizeof(hit));
    hit.pHitItem = &item;

    /* An empty ground list keeps the item strip from drawing anything of its own. */
    memset(&ui, 0, sizeof(ui));

    /* PadImage_Blit and PadImage_BlitEx have no NULL guard, so every pad needs a real blob:
     * u16 width, u16 height, then indexed pixels. */
    for (i = 0; i < 16; ++i) {
        aPortraitData[i][0] = 1; aPortraitData[i][1] = 0;   /* uWidth 1 */
        aPortraitData[i][2] = 1; aPortraitData[i][3] = 0;   /* uHeight 1 */
        aPortraitData[i][4] = 1;                            /* one indexed pixel */
        PadImage_ctor(&aPortrait[i]);
        aPortrait[i].pData  = aPortraitData[i];
        aPortrait[i].uWidth = 1;
        aPortrait[i].uHeight = 1;
        aPortrait[i].nOriginX = 10 + 14 * i;
        aPortrait[i].nOriginY = 315;
        apPortrait[i] = &aPortrait[i];
    }

    aInvPadData[0] = 1; aInvPadData[1] = 0; aInvPadData[2] = 1; aInvPadData[3] = 0; aInvPadData[4] = 1;
    PadImage_ctor(&invPad);
    invPad.pData = aInvPadData;
    invPad.uWidth = 1;
    invPad.uHeight = 1;
    invPad.nOriginX = 235;
    invPad.nOriginY = 315;

    aHandData[0] = 1; aHandData[1] = 0; aHandData[2] = 1; aHandData[3] = 0; aHandData[4] = 1;
    PadImage_ctor(&aHandPad);
    aHandPad.pData = aHandData;
    aHandPad.uWidth = 1;
    aHandPad.uHeight = 1;

    memset(&color, 0, sizeof(color));

    for (i = 0; i < PUFO_HUD_INV_CONTROLS; ++i) {
        memset(&aCtl[i], 0, sizeof(aCtl[i]));
        memset(&aCtlPad[i], 0, sizeof(aCtlPad[i]));
        aCtl[i].apPads[0] = &aCtlPad[i];
        aCtl[i].bEnabled  = 1;   /* a disabled control blits nothing at all */
    }

    memset(&env, 0, sizeof(env));
    env.pInvPad        = &invPad;
    env.aControls      = aCtl;
    env.pSelUnit       = &unit;
    env.pHit           = &hit;
    env.apPortraitPad  = apPortrait;
    env.pszBurdenLabel = "";
    env.pszBurdenSuffix = "";
    env.panRankLow     = &s_aInvRank[0][0];
    env.panRankOther   = &s_aInvRank[0][0];
    env.nCursorX       = 100;
    env.nCursorY       = 100;
    env.bItemHeld      = 1;
    env.bForceNoDrag   = 1;
    env.nBigObsBase    = 10;
    env.Strip.pUi           = &ui;
    env.Strip.pSelUnit      = &unit;
    env.Strip.pEmptyHandPad = &aHandPad;
    env.Strip.pEmptySlot    = NULL;
    env.Strip.nBigObsBase   = 10;

    /* Region 0, ground column 2: x 38*2 + 22 = 98, y 263, 37 x 52. */
    ClearStrip();
    hit.nHitRegion = 0;
    hit.nHitSub0   = 2;
    g_nPadBlits = 0;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    ChkEq(g_nPadBlits, 13, "all thirteen panel controls are blitted");
    Check("region 0 highlights from x 98, y 263", PixAt(98, 263) != 0);
    Check("to x 134, y 314", PixAt(134, 314) != 0);
    Check("and no further right", PixAt(135, 263) == 0);
    Check("and no further down", PixAt(98, 315) == 0);

    /* Region 1, pocket column 1: x 19 + 23 = 42, y 23, width 19*nFootW - 1 = 37, height 19. */
    ClearStrip();
    hit.nHitRegion = 1;
    hit.nHitSub0   = 1;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("region 1 highlights from x 42, y 23", PixAt(42, 23) != 0);
    Check("to x 78, y 41", PixAt(78, 41) != 0);
    Check("its width comes from the item footprint", PixAt(79, 23) == 0);

    /* Region 7, backpack row 2 col 1: x 19 + 161 = 180, y 38 + 161 = 199, 37 x 56. */
    ClearStrip();
    hit.nHitRegion = 7;
    hit.nHitSub0   = 1;
    hit.nHitSub1   = 2;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("region 7 highlights from x 180, y 199", PixAt(180, 199) != 0);
    Check("and its height also comes from the footprint", PixAt(216, 254) != 0);
    Check("stopping at x 216", PixAt(217, 199) == 0);

    /* Region 8, belt: x 19*(1 + 9) = 190, y 38 + 23 = 61. */
    ClearStrip();
    hit.nHitRegion = 8;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("region 8 highlights from x 190, y 61", PixAt(190, 61) != 0);
    Check("to x 226, y 116", PixAt(226, 116) != 0);

    /* A region outside 0..8 draws no highlight, and both gates have to hold. */
    ClearStrip();
    hit.nHitRegion = 9;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("region 9 draws no highlight", PixAt(190, 61) == 0);

    /* Both gates are probed at the point region 0 with sub-column 2 actually paints
     * (x 38*2 + 22 = 98, y 263), so a gate that stops holding shows up. */
    ClearStrip();
    hit.nHitRegion = 0;
    hit.nHitSub0   = 2;
    env.bForceNoDrag = 0;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("forcenodrag clear draws no highlight", PixAt(98, 263) == 0);
    env.bForceNoDrag = 1;

    ClearStrip();
    env.bItemHeld = 0;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("no held item means no highlight either", PixAt(98, 263) == 0);
    Check("and with nothing held neither is the carried-item ghost", PixAt(100, 100) == 0);
    env.bItemHeld = 1;
    ClearStrip();
    env.bForceNoDrag = 0;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("held item plus forcenodrag clear draws the ghost instead of the highlight",
          PixAt(100, 100) != 0);
    Check("and the highlight really is absent there", PixAt(98, 263) == 0);
    hit.nDragOffX = 5;
    hit.nDragOffY = 7;
    ClearStrip();
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("the ghost lands at the drag offset plus the cursor pair", PixAt(105, 107) != 0);
    Check("and the highlight is absent in the same call", PixAt(98, 263) == 0);
    hit.nDragOffX = 0;
    hit.nDragOffY = 0;
    env.bForceNoDrag = 1;

    /* The other side of the same gate: 0x1FC18 draws the ghost only while forcenodrag is clear,
     * so with the flag set and the item still held the cursor stays bare. */
    ClearStrip();
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("with forcenodrag set the held item draws no ghost at the cursor",
          PixAt(100, 100) == 0);

    /* The burden number is uInvBurden - 1, drawn left aligned at x 185, row 140. */
    ClearStrip();
    unit.uInvBurden = 10;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("a burden of ten prints the single digit nine", PixAt(185, 140) != 0);
    Check("so nothing is drawn at the second glyph column", PixAt(193, 140) == 0);

    ClearStrip();
    unit.uInvBurden = 12;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("a burden of twelve prints two digits", PixAt(193, 140) != 0);

    /* The hit item line at (3, 203) appends " (ammo)" only under three conditions. */
    ClearStrip();
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("the item name starts at x 3, row 203", PixAt(3, 203) != 0);
    Check("and the ammo suffix follows it", PixAt(27, 203) != 0);

    ClearStrip();
    obj.bAmmoCap = 255;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("a weapon that never runs out shows no ammo count", PixAt(27, 203) == 0);
    obj.bAmmoCap = 30;

    ClearStrip();
    obj.nCategory = 3;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("category 3 shows no ammo count", PixAt(27, 203) == 0);
    obj.nCategory = 0;

    ClearStrip();
    obj.bTwoHanded = 2;
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("a bTwoHanded value other than 0 or 1 shows no ammo count", PixAt(27, 203) == 0);
    obj.bTwoHanded = 0;

    /* Portrait pad index is 8*nUnitType + 4*bFemale + nLook. Each pad sits at its own x on row 315. */
    ClearStrip();
    unit.nUnitType = 0;
    unit.bFemale   = 1;
    unit.nLook     = 2;   /* 0 + 4 + 2 = 6 -> x 10 + 14*6 = 94 */
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("portrait index 8*type + 4*female + look picks entry 6", PixAt(94, 315) != 0);
    Check("and not its neighbour", PixAt(108, 315) == 0);

    ClearStrip();
    unit.nUnitType = 1;
    unit.bFemale   = 0;
    unit.nLook     = 0;   /* 8 -> x 10 + 14*8 = 122 */
    TactHud_DrawInventoryScreen(&env, &color, CountBlit, CountBlit, NULL);
    Check("unit type 1 starts the second block of eight", PixAt(122, 315) != 0);
    Check("so entry 6 is no longer used", PixAt(94, 315) == 0);
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
    Reset();
    TestSelectedUnitPanel();
    Reset();
    TestItemStrip();
    Reset();
    TestInventoryScreen();
    return TestSummary("test_tacthud");
}
