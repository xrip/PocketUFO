/* test_invdrag.c - checks the inventory hit test Ui_HitTestTactItem @ 0x11ac0.
 *
 * The interesting parts are the ground-list column arithmetic, the first-match
 * rule of the 25-record table, and the slot mirror sentinel that redirects a hit
 * through the meta byte beside the slot.
 */
#include "game/pufo_invdrag.h"

#include <stdio.h>
#include <string.h>

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

/* --- fixture --------------------------------------------------------------- */

static Soldier      g_Unit;
static TactUiState  g_Ui;
static InvHitState  g_Hit;
static TactItem     g_Sentinel;      /* stands for unk_138EA0 */
static TactItem     g_aGround[4];
static TactItem     g_aPocket[4][2];
static TactItem     g_aPack[8];
static TactItem     g_aBelt[9];
static ObjectRec    g_Obj;
static ObjectRec    g_ObjWide;   /* 2 x 2 footprint */
static ObjectRec    g_ObjTall;   /* 1 x 3 footprint */
static ObjectRec    g_aGroundObj[4];
static MapCell      g_Cell;
static u8           g_bInvNoTuCharge;

static void Reset(void)
{
    memset(&g_Unit, 0, sizeof(g_Unit));
    memset(&g_Ui, 0, sizeof(g_Ui));
    memset(&g_Hit, 0, sizeof(g_Hit));
    memset(g_aGround, 0, sizeof(g_aGround));
    memset(g_aPocket, 0, sizeof(g_aPocket));
    memset(g_aPack, 0, sizeof(g_aPack));
    memset(g_aBelt, 0, sizeof(g_aBelt));
    memset(&g_Obj, 0, sizeof(g_Obj));
    memset(&g_ObjWide, 0, sizeof(g_ObjWide));
    memset(&g_ObjTall, 0, sizeof(g_ObjTall));
    memset(&g_Cell, 0, sizeof(g_Cell));
    memset(g_aGroundObj, 0, sizeof(g_aGroundObj));

    g_Obj.nFootW = 2;   /* -20 * 2 / 2 = -20 */
    g_Obj.nFootH = 3;   /* -20 * 3 / 2 = -30 */
    g_ObjWide.nFootW = 2;
    g_ObjWide.nFootH = 2;
    g_ObjTall.nFootW = 1;
    g_ObjTall.nFootH = 3;

    for (i32 i = 0; i < 4; ++i) {
        /* Distinct objects, so the visible list keeps one entry per item. */
        g_aGroundObj[i].nFootW = 2;
        g_aGroundObj[i].nFootH = 3;
        g_aGround[i].pObj = &g_aGroundObj[i];
        g_Ui.apInvList[i] = &g_aGround[i];
    }
    for (i32 p = 0; p < 4; ++p) {
        for (i32 s = 0; s < 2; ++s) {
            g_aPocket[p][s].pObj = &g_Obj;
            g_Unit.aPockets[p].apItem[s] = &g_aPocket[p][s];
        }
    }
    for (i32 i = 0; i < 8; ++i) {
        g_aPack[i].pObj = &g_Obj;
        g_Unit.apBackpack[i] = &g_aPack[i];
    }
    for (i32 i = 0; i < 9; ++i) {
        g_aBelt[i].pObj = &g_Obj;
        g_Unit.apBelt[i] = &g_aBelt[i];
    }
    g_Ui.nInvListCount = 4;
    g_Ui.nInvFilterType = 0;
}

static InvHitTestCtx Ctx(void)
{
    InvHitTestCtx c;

    memset(&c, 0, sizeof(c));
    c.pHit = &g_Hit;
    c.pUi = &g_Ui;
    c.pSelUnit = &g_Unit;
    c.pSentinel = &g_Sentinel;
    return c;
}

/* --- tests ----------------------------------------------------------------- */

static void TestGround(void)
{
    InvHitTestCtx ctx = Ctx();

    printf("Ui_HitTestTactItem -- the ground list window\n");
    Check(Ui_HitTestTactItem(&ctx, 23, 264), "  the top-left corner hits");
    CheckEqI32(g_Hit.nHitRegion, INV_REGION_GROUND, "  region 0 is the ground list");
    CheckEqI32(g_Hit.nHitSub0, 0, "  column 0");
    Check(g_Hit.pHitItem == &g_aGround[0], "  list entry 0");
    CheckEqI32(g_Hit.nDragOffX, -20, "  drag x = -20 * nFootW / 2");
    CheckEqI32(g_Hit.nDragOffY, -30, "  drag y = -20 * nFootH / 2");

    Check(Ui_HitTestTactItem(&ctx, 99, 264), "  column 2 hits");
    CheckEqI32(g_Hit.nHitSub0, 2, "  (99 - 23) / 38 = 2");
    Check(g_Hit.pHitItem == &g_aGround[2], "  list entry 2");

    Check(Ui_HitTestTactItem(&ctx, 173, 313), "  the bottom-right corner hits");
    CheckEqI32(g_Hit.nHitSub0, 3, "  column 3 is the last column");

    /* The scroll offset shifts the window: column 0 now reads list entry 1. */
    g_Ui.nInvFilterType = 1;
    Check(Ui_HitTestTactItem(&ctx, 23, 264), "  scrolled window hits");
    Check(g_Hit.pHitItem == &g_aGround[1], "  nInvFilterType shifts the index");
    CheckEqI32(g_Hit.nHitSub0, 0, "  sub0 stays the COLUMN, not the index");
    g_Ui.nInvFilterType = 0;

    /* Past the end of the list nothing is hit. */
    g_Ui.nInvListCount = 2;
    Check(!Ui_HitTestTactItem(&ctx, 99, 264), "  index 2 of a 2-entry list misses");
    Check(g_Hit.pHitItem == NULL, "  the hit item is cleared on entry");
    g_Ui.nInvListCount = 4;

    /* Outside the rectangle entirely. */
    Check(!Ui_HitTestTactItem(&ctx, 22, 264), "  x 22 is outside");
    Check(!Ui_HitTestTactItem(&ctx, 174, 264), "  x 174 is outside");
    Check(!Ui_HitTestTactItem(&ctx, 23, 263), "  y 263 is outside");
    Check(!Ui_HitTestTactItem(&ctx, 23, 314), "  y 314 is outside");
}

static void TestPockets(void)
{
    InvHitTestCtx ctx = Ctx();

    printf("Ui_HitTestTactItem -- the four pockets\n");
    Check(Ui_HitTestTactItem(&ctx, 30, 30), "  pocket 0 slot 0");
    CheckEqI32(g_Hit.nHitRegion, INV_REGION_POCKET0, "  region 1");
    Check(g_Hit.pHitItem == &g_aPocket[0][0], "  the slot's item");

    Check(Ui_HitTestTactItem(&ctx, 41, 30), "  the shared edge x 41");
    Check(g_Hit.pHitItem == &g_aPocket[0][0],
          "  overlapping records resolve by TABLE ORDER, not by area");

    Check(Ui_HitTestTactItem(&ctx, 50, 30), "  pocket 0 slot 1");
    Check(g_Hit.pHitItem == &g_aPocket[0][1], "  the second slot");

    Check(Ui_HitTestTactItem(&ctx, 130, 30), "  pocket 1 slot 1");
    CheckEqI32(g_Hit.nHitRegion, INV_REGION_POCKET1, "  region 2");
    Check(g_Hit.pHitItem == &g_aPocket[1][1], "  the slot's item");

    Check(Ui_HitTestTactItem(&ctx, 30, 170), "  pocket 2 slot 0");
    CheckEqI32(g_Hit.nHitRegion, INV_REGION_POCKET2, "  region 3");

    Check(Ui_HitTestTactItem(&ctx, 130, 170), "  pocket 3 slot 1");
    CheckEqI32(g_Hit.nHitRegion, INV_REGION_POCKET3, "  region 4");

    /* An empty slot is a miss, and the hit item stays NULL. */
    g_Unit.aPockets[0].apItem[0] = NULL;
    Check(!Ui_HitTestTactItem(&ctx, 30, 30), "  an empty pocket misses");
    Check(g_Hit.pHitItem == NULL, "  and leaves no hit item");

    /* The mirror sentinel redirects through the meta byte. */
    g_Unit.aPockets[0].apItem[0] = &g_Sentinel;
    g_Unit.aPockets[0].abMeta[0] = 1;
    Check(Ui_HitTestTactItem(&ctx, 30, 30), "  a mirror slot hits");
    Check(g_Hit.pHitItem == &g_aPocket[0][1], "  the hit follows abMeta to slot 1");
    CheckEqI32(g_Hit.nHitSub0, 1, "  and reports the meta byte in sub0");
}

static void TestHands(void)
{
    InvHitTestCtx ctx = Ctx();

    printf("Ui_HitTestTactItem -- the two hands\n");
    g_Unit.pHandL = &g_aPocket[0][0];
    g_Unit.pHandR = &g_aPocket[1][0];

    Check(Ui_HitTestTactItem(&ctx, 10, 90), "  the left hand band");
    CheckEqI32(g_Hit.nHitRegion, INV_REGION_HAND_L, "  region 5");
    Check(g_Hit.pHitItem == g_Unit.pHandL, "  pHandL");

    Check(Ui_HitTestTactItem(&ctx, 130, 90), "  the right hand band");
    CheckEqI32(g_Hit.nHitRegion, INV_REGION_HAND_R, "  region 6");
    Check(g_Hit.pHitItem == g_Unit.pHandR, "  pHandR");

    g_Unit.pHandL = NULL;
    Check(!Ui_HitTestTactItem(&ctx, 10, 90), "  an empty hand misses");
}

static void TestBackpack(void)
{
    InvHitTestCtx ctx = Ctx();

    printf("Ui_HitTestTactItem -- the backpack grid\n");
    g_Unit.abBackpackMeta[0] = 7;
    Check(Ui_HitTestTactItem(&ctx, 170, 170), "  row 0 slot 0");
    CheckEqI32(g_Hit.nHitRegion, INV_REGION_BACKPACK, "  region 7");
    Check(g_Hit.pHitItem == &g_aPack[0], "  apBackpack[0]");
    CheckEqI32(g_Hit.nHitSub0, 7, "  sub0 is meta A of the slot");
    CheckEqI32(g_Hit.nHitSub1, 0, "  sub1 is meta B of the slot");

    Check(Ui_HitTestTactItem(&ctx, 225, 170), "  row 0 slot 3");
    Check(g_Hit.pHitItem == &g_aPack[3], "  index slot + 4*col = 3");

    Check(Ui_HitTestTactItem(&ctx, 225, 190), "  row 1 slot 3");
    Check(g_Hit.pHitItem == &g_aPack[7], "  index 3 + 4*1 = 7");

    /* The table only carries six backpack rects: row 1 has slots 0 and 3, so the
     * middle two cells of that row cannot be hit at all. */
    Check(!Ui_HitTestTactItem(&ctx, 190, 190), "  row 1 slot 1 has no rect");
    Check(!Ui_HitTestTactItem(&ctx, 210, 190), "  row 1 slot 2 has no rect");

    g_Unit.abBackpackMeta[0] = 1;
    g_Unit.abBackpackMeta[8] = 1;   /* meta B of slot 0 */
    g_Unit.apBackpack[0] = &g_Sentinel;
    Check(Ui_HitTestTactItem(&ctx, 170, 170), "  a mirror backpack slot hits");
    Check(g_Hit.pHitItem == &g_aPack[1 + 4 * 1],
          "  the hit follows A + 4*B to slot 5");
    CheckEqI32(g_Hit.nHitSub0, 1, "  sub0 is the mirror column");
    CheckEqI32(g_Hit.nHitSub1, 1, "  sub1 is the mirror row");
}

static void TestBelt(void)
{
    InvHitTestCtx ctx = Ctx();

    printf("Ui_HitTestTactItem -- the belt grid\n");
    g_Unit.abBeltMetaA[0] = 4;
    Check(Ui_HitTestTactItem(&ctx, 180, 30), "  row 0 slot 0");
    CheckEqI32(g_Hit.nHitRegion, INV_REGION_BELT, "  region 8");
    Check(g_Hit.pHitItem == &g_aBelt[0], "  apBelt[0]");
    CheckEqI32(g_Hit.nHitSub0, 4, "  sub0 is meta A of the slot");

    Check(Ui_HitTestTactItem(&ctx, 215, 70), "  row 2 slot 2");
    Check(g_Hit.pHitItem == &g_aBelt[8], "  index 2 + 3*2 = 8");

    g_Unit.abBeltMetaA[0] = 2;
    g_Unit.abBeltMetaB[0] = 2;   /* Soldier+0x199 */
    g_Unit.apBelt[0] = &g_Sentinel;
    Check(Ui_HitTestTactItem(&ctx, 180, 30), "  a mirror belt slot hits");
    Check(g_Hit.pHitItem == &g_aBelt[2 + 3 * 2], "  the hit follows A + 3*B to slot 8");
}

static void TestMisses(void)
{
    InvHitTestCtx ctx = Ctx();

    printf("Ui_HitTestTactItem -- misses and the state contract\n");
    Check(!Ui_HitTestTactItem(&ctx, 250, 250), "  a point outside everything misses");
    Check(g_Hit.pHitItem == NULL, "  no hit item");
    Check(!Ui_HitTestTactItem(&ctx, 0, 0), "  the origin misses");

    /* The state is cleared on entry even when nothing else is written. */
    g_Hit.pHitItem = &g_aGround[0];
    g_Hit.nHitRegion = INV_REGION_BELT;
    Check(!Ui_HitTestTactItem(&ctx, 0, 0), "  a stale state does not survive");
    Check(g_Hit.pHitItem == NULL, "  the hit item is cleared first");

    Check(!Ui_HitTestTactItem(NULL, 30, 30), "  a NULL context is refused");
}

static void TestClear(void)
{
    InvHitTestCtx ctx = Ctx();

    printf("Inv_ClearHitSlot @ 0x11f40 -- the footprint clear\n");

    /* Region 0: the item leaves the cell list and the visible list is rebuilt. */
    MapCell_Init(&g_Cell, 0, 0, 0);
    g_Unit.pCell = &g_Cell;
    g_Cell.pItemList = &g_aGround[0];
    g_aGround[0].pNext = &g_aGround[1];
    g_aGround[1].pNext = NULL;
    g_Ui.nInvListSel = 0xFF;
    g_Ui.nInvFilterType = 0;
    Inv_BuildVisibleList(&g_Ui, &g_Unit);
    CheckEqI32(g_Ui.nInvListCount, 2, "  two ground items are listed");
    g_Hit.nHitRegion = INV_REGION_GROUND;
    g_Hit.pHitItem = &g_aGround[0];
    Inv_ClearHitSlot(&ctx);
    Check(g_Cell.pItemList == &g_aGround[1], "  the hit item is out of the cell list");
    CheckEqI32(g_Ui.nInvListCount, 1, "  and the visible list is rebuilt");

    Reset();
    ctx = Ctx();

    /* A 2-wide pocket item clears both slots of the pair. */
    g_aPocket[0][0].pObj = &g_ObjWide;
    g_aPocket[0][1].pObj = &g_ObjWide;
    g_Hit.nHitRegion = INV_REGION_POCKET0;
    g_Hit.nHitSub0 = 0;
    g_Hit.pHitItem = &g_aPocket[0][0];
    Inv_ClearHitSlot(&ctx);
    Check(g_Unit.aPockets[0].apItem[0] == NULL &&
          g_Unit.aPockets[0].apItem[1] == NULL,
          "  a 2-wide pocket item clears both slots");

    /* A 1-wide item at slot 1 leaves slot 0 alone. */
    g_Hit.nHitRegion = INV_REGION_POCKET1;
    g_Hit.nHitSub0 = 1;
    g_Hit.pHitItem = &g_aPocket[1][1];
    Inv_ClearHitSlot(&ctx);
    Check(g_Unit.aPockets[1].apItem[1] == NULL, "  slot 1 cleared");
    Check(g_Unit.aPockets[1].apItem[0] == &g_aPocket[1][0], "  slot 0 untouched");

    /* The two hands. */
    g_Unit.pHandL = &g_aPocket[2][0];
    g_Unit.pHandR = &g_aPocket[3][0];
    g_Hit.nHitRegion = INV_REGION_HAND_L;
    Inv_ClearHitSlot(&ctx);
    Check(g_Unit.pHandL == NULL, "  region 5 clears pHandL");
    g_Hit.nHitRegion = INV_REGION_HAND_R;
    Inv_ClearHitSlot(&ctx);
    Check(g_Unit.pHandR == NULL, "  region 6 clears pHandR");

    /* A 2 x 2 backpack item clears four cells across two 4-slot rows. */
    g_aPack[0].pObj = &g_ObjWide;
    g_Hit.nHitRegion = INV_REGION_BACKPACK;
    g_Hit.nHitSub0 = 0;
    g_Hit.nHitSub1 = 0;
    g_Hit.pHitItem = &g_aPack[0];
    Inv_ClearHitSlot(&ctx);
    Check(g_Unit.apBackpack[0] == NULL && g_Unit.apBackpack[1] == NULL &&
          g_Unit.apBackpack[4] == NULL && g_Unit.apBackpack[5] == NULL,
          "  a 2x2 pack item clears slots 0,1,4,5 (row stride 4)");
    Check(g_Unit.apBackpack[2] == &g_aPack[2] && g_Unit.apBackpack[3] == &g_aPack[3] &&
          g_Unit.apBackpack[6] == &g_aPack[6] && g_Unit.apBackpack[7] == &g_aPack[7],
          "  and leaves the other six");

    /* A 1 x 3 belt item clears one column across two 3-slot rows. */
    g_aBelt[0].pObj = &g_ObjTall;
    g_Hit.nHitRegion = INV_REGION_BELT;
    g_Hit.nHitSub0 = 0;
    g_Hit.nHitSub1 = 0;
    g_Hit.pHitItem = &g_aBelt[0];
    Inv_ClearHitSlot(&ctx);
    Check(g_Unit.apBelt[0] == NULL && g_Unit.apBelt[3] == NULL &&
          g_Unit.apBelt[6] == NULL,
          "  a 1x3 belt item clears slots 0,3,6 (row stride 3)");
    Check(g_Unit.apBelt[1] == &g_aBelt[1] && g_Unit.apBelt[8] == &g_aBelt[8],
          "  and leaves the rest");

    /* An unknown region does nothing at all. */
    g_Hit.nHitRegion = INV_REGION_COUNT;
    g_Hit.pHitItem = &g_aBelt[1];
    Inv_ClearHitSlot(&ctx);
    Check(g_Unit.apBelt[1] == &g_aBelt[1], "  an unknown region clears nothing");
}

/* --- drop fixture --------------------------------------------------------- */

static MapCell   g_aGrid[4][6][6];
static Mc1Rec    g_FloorSpr;
static TactItem *g_apPool[PUFO_INV_ITEM_POOL_MAX];
static i32       g_nPool;
static TactItem  g_aAlloc[4];
static i32       g_nAlloc;
static i32       g_aSamples[16];
static i32       g_nSamples;
static i32       g_aModalX[8], g_aModalY[8], g_aModalN[8];
static i32       g_nModal;
static ObjectRec g_aObjById[8];
static i32       g_nFreed;
static i32       g_nWeightCalls;

static void Tr_Play(i32 nSample, void *pUser)
{
    (void)pUser;
    if (g_nSamples < (i32)(sizeof(g_aSamples) / sizeof(g_aSamples[0]))) {
        g_aSamples[g_nSamples++] = nSample;
    }
}

static void Tr_Modal(i32 nX, i32 nY, i32 nCount, const char *szLabel, void *pUser)
{
    (void)szLabel; (void)pUser;
    if (g_nModal < 8) {
        g_aModalX[g_nModal] = nX;
        g_aModalY[g_nModal] = nY;
        g_aModalN[g_nModal] = nCount;
        ++g_nModal;
    }
}

static ObjectRec *Tr_Find(i32 nIndex, void *pUser)
{
    (void)pUser;
    return (nIndex >= 0 && nIndex < 8) ? &g_aObjById[nIndex] : NULL;
}

static TactItem *Tr_Alloc(void *pUser)
{
    (void)pUser;
    if (g_nAlloc >= 4) {
        return NULL;
    }
    memset(&g_aAlloc[g_nAlloc], 0, sizeof(g_aAlloc[g_nAlloc]));
    return &g_aAlloc[g_nAlloc++];
}

static void Tr_Free(TactItem *pItem, void *pUser)
{
    (void)pItem; (void)pUser;
    ++g_nFreed;
}

static MapCell *Tr_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nLayer < 0 || nLayer > 3 || nY < 0 || nY > 5 || nX < 0 || nX > 5) {
        return NULL;
    }
    return &g_aGrid[nLayer][nY][nX];
}

static u8 Tr_Weight(TactItem *pItem, void *pUser)
{
    (void)pItem; (void)pUser;
    ++g_nWeightCalls;
    return 1;
}

static u8 Tr_TwoHanded(TactItem *pItem, void *pUser) { (void)pItem; (void)pUser; return 0; }
static u8 Tr_AmmoCap(TactItem *pItem, void *pUser) { (void)pItem; (void)pUser; return 255; }
static u8 Tr_AmmoWeight(i32 nIndex, void *pUser) { (void)nIndex; (void)pUser; return 1; }

static const InvStatHooks g_StatHooks = {
    Tr_Weight, Tr_TwoHanded, Tr_AmmoCap, Tr_AmmoWeight, NULL
};

/* The drop fixture: the unit stands on cell (3,3,2), the pool holds one item, and
 * the drag anchor is the stored drag offset plus 10, so a test picks an anchor by
 * writing the drag offset and leaving the pen at zero. */
static void ResetDrop(void)
{
    Reset();
    memset(g_aGrid, 0, sizeof(g_aGrid));
    memset(g_apPool, 0, sizeof(g_apPool));
    memset(g_aObjById, 0, sizeof(g_aObjById));
    g_nPool = 0;
    g_nAlloc = 0;
    g_nSamples = 0;
    g_nModal = 0;
    g_nFreed = 0;
    g_nWeightCalls = 0;
    g_bInvNoTuCharge = 0;

    for (i32 z = 0; z < 4; ++z) {
        for (i32 y = 0; y < 6; ++y) {
            for (i32 x = 0; x < 6; ++x) {
                g_aGrid[z][y][x].nX = (u8)x;
                g_aGrid[z][y][x].nY = (u8)y;
                g_aGrid[z][y][x].nZ = (u8)z;
            }
        }
    }
    g_Unit.pCell = &g_aGrid[2][3][3];
    g_Unit.uTu = 20;
    g_Ui.bActionLatch = 0;
    g_aGrid[2][3][3].pFloorSpr = &g_FloorSpr;   /* the unit stands on a floor */

    /* The hit fixture fills every slot; a drop test needs empty ones. */
    g_Unit.pHandL = NULL;
    g_Unit.pHandR = NULL;
    for (i32 p = 0; p < 4; ++p) {
        for (i32 s = 0; s < 2; ++s) {
            g_Unit.aPockets[p].apItem[s] = NULL;
        }
    }
    for (i32 i = 0; i < 8; ++i) {
        g_Unit.apBackpack[i] = NULL;
    }
    for (i32 i = 0; i < 9; ++i) {
        g_Unit.apBelt[i] = NULL;
    }
    g_Ui.nInvListCount = 0;
    for (i32 i = 0; i < 4; ++i) {
        g_Ui.apInvList[i] = NULL;
    }
}

static InvDropHooks DropHooks(void)
{
    InvDropHooks h;

    memset(&h, 0, sizeof(h));
    h.pfnPlaySample = Tr_Play;
    h.pfnModal = Tr_Modal;
    h.pfnFindObject = Tr_Find;
    h.pfnAllocItem = Tr_Alloc;
    h.pfnFreeItem = Tr_Free;
    h.apItemPool = g_apPool;
    h.pnItemCount = &g_nPool;
    h.pfnLookupCell = Tr_Lookup;
    h.pInvStat = &g_StatHooks;
    return h;
}

/* Arm the drag state: the item, the region it came from, its slot pair, and an
 * anchor point. */
static InvDropCtx DropCtx(TactItem *pItem, i32 nRegion, i32 nSub0, i32 nSub1,
                          i32 nAnchorX, i32 nAnchorY)
{
    static InvDropHooks s_h;
    InvDropCtx c;

    s_h = DropHooks();
    memset(&c, 0, sizeof(c));
    c.pHit = &g_Hit;
    c.pUi = &g_Ui;
    c.pbInvNoTuCharge = &g_bInvNoTuCharge;
    c.pSelUnit = &g_Unit;
    c.pSentinel = &g_Sentinel;
    c.nPtrX = 0;
    c.nPtrY = 0;
    c.pHooks = &s_h;

    g_Hit.pHitItem = pItem;
    /* A drag item must not carry a stale list link into the cell it lands in. */
    pItem->pNext = NULL;
    g_Hit.nHitRegion = nRegion;
    g_Hit.nHitSub0 = nSub0;
    g_Hit.nHitSub1 = nSub1;
    g_Hit.nDragOffX = nAnchorX - 10;
    g_Hit.nDragOffY = nAnchorY - 10;
    return c;
}

static i32 SampleAt(i32 i)
{
    return (i < g_nSamples) ? g_aSamples[i] : -1;
}

static void CheckSamples(const char *pszWhat, i32 n0, i32 n1)
{
    char szBuf[96];
    i32 nWant1 = (n1 < 0) ? -1 : n1;

    snprintf(szBuf, sizeof(szBuf), "%s (%d, %d)", pszWhat, (int)n0, (int)nWant1);
    Check(g_nSamples == ((n1 < 0) ? 1 : 2) && SampleAt(0) == n0 &&
          (n1 < 0 || SampleAt(1) == n1), szBuf);
}

/* --- drop tests ------------------------------------------------------------ */

static TactItem g_Item1x1;   /* 1 x 1, the ordinary case */
static TactItem g_Item2x1;   /* 2 x 1, a wide footprint */
static TactItem g_Weapon;    /* a weapon held in a hand */
static ObjectRec g_O1x1;
static ObjectRec g_O2x1;
static ObjectRec g_OWeapon;
static ObjectRec g_OClip;

static void SetupItems(void)
{
    memset(&g_O1x1, 0, sizeof(g_O1x1));
    memset(&g_O2x1, 0, sizeof(g_O2x1));
    memset(&g_OWeapon, 0, sizeof(g_OWeapon));
    memset(&g_OClip, 0, sizeof(g_OClip));
    memset(&g_Item1x1, 0, sizeof(g_Item1x1));
    memset(&g_Item2x1, 0, sizeof(g_Item2x1));
    memset(&g_Weapon, 0, sizeof(g_Weapon));
    g_O1x1.nFootW = 1;
    g_O1x1.nFootH = 1;
    g_O2x1.nFootW = 2;
    g_O2x1.nFootH = 1;
    g_OWeapon.nFootW = 1;
    g_OWeapon.nFootH = 1;
    g_OWeapon.anAmmoTypeId[0] = 4;   /* the weapon accepts object id 4 */
    g_OClip.nFootW = 1;
    g_OClip.nFootH = 1;
    g_OClip.bTwoHanded = 1;   /* the clip-load gate tests the DRAGGED item */
    g_Item1x1.pObj = &g_O1x1;
    g_Item2x1.pObj = &g_O2x1;
    g_Weapon.pObj = &g_OWeapon;
    /* apObjects[4] is the clip object the weapon's ammo id names. */
    g_aObjById[4].nFootW = 1;
    g_aObjById[4].nFootH = 1;
    g_aObjById[4].bTwoHanded = 1;
}

/* Put a weapon in one hand and a clip in the drag state. */
static void SetupClip(TactItem **ppHand)
{
    *ppHand = &g_Weapon;
    g_Weapon.nAmmo = 0;
    g_Item1x1.pObj = &g_aObjById[4];
    g_Item1x1.nAmmo = 7;
    g_Item1x1.nIndex = 4;
}

/* --- the drop path, Inv_HandlePointer @ 0x12260 -------------------------------------------------------------
 * One 6,668-byte function. The anchor is Map_nInvDragOffX/Y + (pBank + 0x1044, pBank + 0x1048) + 10, the hit
 * test walks aInvHitRects and the first hit wins, and the charge is per region: pocket 0/1 cost 4 TU,
 * pocket 2/3 cost 5, hand 6, backpack 7, belt 8, the ground strip 5, an ammo split 6. The charge is skipped
  * when Map+1 is set or the hit region is the region the item already occupies. Map+2 is separate. The refusal modals
 * are fixed triples: (160, 31, 1) for not enough time, (160, 45, 1) for a weapon already loaded, and
 * (200, 43, 2) when a priority weapon fills the other hand. The four drop tests below are the checks for this
 * address: which slot the item lands in, what the object's cargo column and the craft's total became, and
 * which modal a refusal opens. The charge checks below state 4, 5, 6, 7 and 8 as literals and not through
 * PUFO_INV_TU_* -- the constants are the thing under test, and an expectation built from them cannot fail. */
static void TestDropPocket(void)
{
    InvDropCtx c;

    printf("Inv_HandlePointer @ 0x12260 -- the pocket slots\n");
    ResetDrop();
    SetupItems();

    /* Pocket 0 slot 0 is the rect x 22..41, y 22..41. */
    c = DropCtx(&g_Item2x1, INV_REGION_GROUND, 0, 0, 30, 30);
    Check(Inv_HandlePointer(&c), "  a wide item drops into pocket 0");
    Check(g_Unit.aPockets[0].apItem[0] == &g_Item2x1, "  the anchor slot holds the item");
    Check(g_Unit.aPockets[0].apItem[1] == &g_Sentinel, "  the covered slot holds the sentinel");
    CheckEqI32(g_Unit.aPockets[0].abMeta[1], 0, "  its meta byte names slot 0");
    CheckEqI32(g_Unit.uTu, 20 - 4, "  pockets 0/1 cost 4 time units");
    CheckSamples("  sample 17 on a placed drop", 17, -1);
    Check(g_nWeightCalls > 0, "  Unit_RecalcInventoryStat ran");

    /* Pocket 2 costs 5. */
    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 1, 0, 110, 30);
    Check(Inv_HandlePointer(&c), "  pocket 1 slot 1");
    CheckEqI32(g_Unit.uTu, 20 - 4, "  pocket 1 also costs 4");
    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 30, 170);
    Check(Inv_HandlePointer(&c), "  pocket 2 slot 0");
    CheckEqI32(g_Unit.uTu, 20 - 5, "  pockets 2/3 cost 5");

    /* An occupied slot is refused before any charge, and the item goes back. */
    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 30, 30);
    g_Unit.aPockets[0].apItem[0] = &g_aPocket[0][0];
    g_Unit.pCell->pItemList = NULL;
    Check(!Inv_HandlePointer(&c), "  an occupied pocket is refused");
    CheckEqI32(g_Unit.uTu, 20, "  no time units were charged");
    Check(g_Unit.aPockets[0].apItem[0] == &g_aPocket[0][0], "  the slot keeps its item");

    /* A tall item does not fit a pocket at all. */
    ResetDrop();
    g_O1x1.nFootH = 2;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 30, 30);
    Check(!Inv_HandlePointer(&c), "  nFootH > 1 never fits a pocket");
    g_O1x1.nFootH = 1;

    /* A wide item at slot 1 runs past the pair. */
    ResetDrop();
    c = DropCtx(&g_Item2x1, INV_REGION_GROUND, 1, 0, 50, 30);
    Check(!Inv_HandlePointer(&c), "  slot 1 plus a 2-wide footprint is refused");
    CheckEqI32(g_Unit.uTu, 20, "  the bounds test runs before the charge");

    /* Not enough time: the modal fires and the drop still falls through. */
    ResetDrop();
    g_Unit.uTu = 3;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 30, 30);
    Check(!Inv_HandlePointer(&c), "  3 time units cannot pay for a pocket");
    CheckEqI32(g_nModal, 1, "  one modal was raised");
    CheckEqI32(g_aModalX[0], 160, "  the time modal is at 160,31");
    CheckEqI32(g_aModalY[0], 31, "  with one button");

    /* Map+1 waives the cost. Map+2 is a separate action latch. */
    ResetDrop();
    g_bInvNoTuCharge = 1;
    g_Unit.uTu = 0;
    g_Ui.bActionLatch = 0;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 30, 30);
    Check(Inv_HandlePointer(&c), "  Map+1 set and Map+2 clear still places the item");
    CheckEqI32(g_Unit.uTu, 0, "  Map+1 skips the pocket TU check and debit");
    CheckEqI32(g_nModal, 0, "  no low-TU modal is raised");

    ResetDrop();
    g_Ui.bActionLatch = 1;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 30, 30);
    Check(Inv_HandlePointer(&c), "  Map+2 set does not block the pocket drop");
    CheckEqI32(g_Unit.uTu, 20 - 4, "  Map+2 does not waive the Map+1-cleared charge");

    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_POCKET0, 1, 0, 50, 30);
    Check(Inv_HandlePointer(&c), "  pocket 0 slot 1");
    CheckEqI32(g_Unit.uTu, 20, "  a drop inside the source region is free");
}

static void TestDropHands(void)
{
    InvDropCtx c;

    printf("Inv_HandlePointer @ 0x12260 -- the hands\n");
    ResetDrop();
    SetupItems();

    /* The left hand is x 3..42, y 61..119; the right hand x 122..161. */
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(Inv_HandlePointer(&c), "  an empty left hand takes the item");
    Check(g_Unit.pHandL == &g_Item1x1, "  pHandL");
    CheckEqI32(g_Unit.uTu, 20 - 6, "  a hand costs 6");

    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 130, 90);
    Check(Inv_HandlePointer(&c), "  an empty right hand takes the item");
    Check(g_Unit.pHandR == &g_Item1x1, "  pHandR");

    /* A priority weapon in the other hand blocks it, unless one side is category 3. */
    ResetDrop();
    SetupItems();
    g_Unit.pHandR = &g_Weapon;
    g_OWeapon.bListPri = 1;
    g_OWeapon.nCategory = 1;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(!Inv_HandlePointer(&c), "  a priority weapon blocks the other hand");
    CheckEqI32(g_Unit.uTu, 20, "  and nothing is charged");
    g_O1x1.nCategory = 3;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(Inv_HandlePointer(&c), "  a category-3 incoming item goes in anyway");
    g_O1x1.nCategory = 1;

    /* The mirror case: a priority incoming item is blocked unless the HELD weapon
     * is category 3. */
    ResetDrop();
    SetupItems();
    g_Unit.pHandR = &g_Weapon;
    g_O1x1.bListPri = 1;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(!Inv_HandlePointer(&c), "  a priority item cannot join a normal weapon");
    ResetDrop();
    SetupItems();
    g_Unit.pHandR = &g_Weapon;
    g_O1x1.bListPri = 1;
    g_OWeapon.nCategory = 3;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(Inv_HandlePointer(&c), "  unless the held weapon is category 3");
    g_O1x1.bListPri = 0;
    g_OWeapon.nCategory = 1;

    /* Loading a clip: the weapon is in the left hand, the dragged item is the
     * two-handed clip whose object one of the weapon's ammo ids names. */
    ResetDrop();
    SetupItems();
    SetupClip(&g_Unit.pHandL);
    g_apPool[0] = &g_Item2x1;
    g_apPool[1] = &g_Item1x1;   /* the dragged item, not the last entry */
    g_apPool[2] = &g_aGround[0];
    g_nPool = 3;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(Inv_HandlePointer(&c), "  a clip loads into the empty weapon");
    CheckEqI32(g_Weapon.nAmmo, 7, "  the weapon now holds the clip");
    CheckEqI32(g_Weapon.nIndex, 4, "  and its ammo index follows the clip");
    CheckEqI32(g_Unit.uTu, 20 - 6, "  loading costs 6");
    CheckSamples("  sample 6 for the load, then 17", 6, 17);
    CheckEqI32(g_nFreed, 1, "  the clip object was deleted");
    CheckEqI32(g_nPool, 2, "  the pool count dropped by one");
    Check(g_apPool[1] == &g_aGround[0], "  the hole closed with the LAST entry");

    /* A weapon that already holds a clip raises the 160,45 modal. */
    ResetDrop();
    SetupItems();
    SetupClip(&g_Unit.pHandL);
    g_Weapon.nAmmo = 3;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(!Inv_HandlePointer(&c), "  a loaded weapon refuses the clip");
    CheckEqI32(g_nModal, 1, "  one modal");
    CheckEqI32(g_aModalY[0], 45, "  at 160,45");
    CheckEqI32(g_Unit.uTu, 20, "  and no charge");

    /* A priority weapon with the other hand full raises the 200,43 modal. */
    ResetDrop();
    SetupItems();
    g_OWeapon.bListPri = 1;
    g_O1x1.bTwoHanded = 1;
    g_Unit.pHandL = &g_Weapon;
    g_Unit.pHandR = &g_aPocket[1][0];
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(!Inv_HandlePointer(&c), "  a two-handed item onto a priority weapon");
    CheckEqI32(g_nModal, 1, "  one modal was raised");
    CheckEqI32(g_aModalX[0], 200, "  blocks with the 200,43 modal");
    CheckEqI32(g_aModalN[0], 2, "  which has two buttons");

    /* A non-two-handed item on an occupied hand is simply refused. */
    ResetDrop();
    SetupItems();
    g_Unit.pHandL = &g_Weapon;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(!Inv_HandlePointer(&c), "  a one-handed item cannot stack on a held item");

    /* Map+1 skips the clip charge. Map+2 does not. */
    ResetDrop();
    SetupItems();
    SetupClip(&g_Unit.pHandR);
    g_Item1x1.nAmmo = 2;
    g_Unit.uTu = 0;
    g_bInvNoTuCharge = 1;
    g_Ui.bActionLatch = 0;
    c = DropCtx(&g_Item1x1, INV_REGION_HAND_R, 0, 0, 130, 90);
    Check(Inv_HandlePointer(&c), "  Map+1 lets a clip load proceed at zero TU");
    CheckEqI32(g_Unit.uTu, 0, "  Map+1 skips both the TU check and debit");
    CheckEqI32(g_Weapon.nAmmo, 2, "  the right-hand weapon took the clip");

    ResetDrop();
    SetupItems();
    SetupClip(&g_Unit.pHandR);
    g_Item1x1.nAmmo = 2;
    g_Unit.uTu = PUFO_INV_TU_HAND;
    g_Ui.bActionLatch = 1;
    c = DropCtx(&g_Item1x1, INV_REGION_HAND_R, 0, 0, 130, 90);
    Check(Inv_HandlePointer(&c), "  Map+2 does not block a clip load with enough TU");
    CheckEqI32(g_Unit.uTu, 0, "  the Map+1-cleared clip path still charges 6");
}

static void TestDropGrid(void)
{
    InvDropCtx c;

    printf("Inv_HandlePointer @ 0x12260 -- the backpack and the belt\n");
    ResetDrop();
    SetupItems();

    /* Backpack row 0 is y 160..179, columns at x 160/179/198/217. */
    c = DropCtx(&g_Item2x1, INV_REGION_GROUND, 0, 0, 170, 170);
    Check(Inv_HandlePointer(&c), "  a 2-wide item drops into backpack col 0");
    Check(g_Unit.apBackpack[0] == &g_Item2x1, "  anchor slot 0 holds the item");
    Check(g_Unit.apBackpack[1] == &g_Sentinel, "  slot 1 holds the sentinel");
    CheckEqI32(g_Unit.abBackpackMeta[1], 0, "  meta A names the column");
    CheckEqI32(g_Unit.abBackpackMeta[1 + 8], 0, "  meta B names the row");
    CheckEqI32(g_Unit.uTu, 20 - 7, "  the backpack costs 7");

    /* Row 1 is y 179..198 and only columns 0 and 3 have a rect. */
    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 3, 1, 225, 190);
    Check(Inv_HandlePointer(&c), "  backpack col 3 row 1");
    Check(g_Unit.apBackpack[3 + 4] == &g_Item1x1, "  index 3 + 4*1");
    CheckEqI32(g_Unit.abBackpackMeta[7], 3, "  meta A is 3");
    CheckEqI32(g_Unit.abBackpackMeta[15], 1, "  meta B is 1");

    /* A footprint that is not fully empty is refused. */
    ResetDrop();
    g_Unit.apBackpack[5] = &g_aPack[5];   /* slot 1 of row 1 is occupied */
    c = DropCtx(&g_Item2x1, INV_REGION_GROUND, 0, 1, 170, 190);
    Check(!Inv_HandlePointer(&c), "  an occupied cell inside the footprint blocks it");
    CheckEqI32(g_Unit.uTu, 20, "  and nothing was charged");

    /* A 2-tall item at row 1 runs past the grid. */
    ResetDrop();
    g_O1x1.nFootH = 2;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 1, 170, 190);
    Check(!Inv_HandlePointer(&c), "  nFootH + row > 2 is refused");
    g_O1x1.nFootH = 1;

    /* The belt is 3 x 3 at x 170..227, y 22..79. */
    ResetDrop();
    c = DropCtx(&g_Item2x1, INV_REGION_GROUND, 0, 0, 180, 30);
    Check(Inv_HandlePointer(&c), "  a 2-wide item drops into belt col 0 row 0");
    Check(g_Unit.apBelt[0] == &g_Item2x1, "  anchor slot 0");
    Check(g_Unit.apBelt[1] == &g_Sentinel, "  slot 1 holds the sentinel");
    CheckEqI32(g_Unit.abBeltMetaA[1], 0, "  meta A");
    CheckEqI32(g_Unit.abBeltMetaB[1], 0, "  meta B");
    CheckEqI32(g_Unit.uTu, 20 - 8, "  the belt costs 8");

    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 2, 2, 220, 70);
    Check(Inv_HandlePointer(&c), "  belt col 2 row 2");
    Check(g_Unit.apBelt[2 + 6] == &g_Item1x1, "  index 2 + 3*2");
    CheckEqI32(g_Unit.abBeltMetaA[8], 2, "  meta A is 2");
    CheckEqI32(g_Unit.abBeltMetaB[8], 2, "  meta B is 2");

    ResetDrop();
    g_O1x1.nFootH = 2;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 2, 180, 70);
    Check(!Inv_HandlePointer(&c), "  nFootH + row > 3 is refused on the belt");
    g_O1x1.nFootH = 1;
}

static void TestDropGround(void)
{
    InvDropCtx c;

    printf("Inv_HandlePointer @ 0x12260 -- the ground strip\n");
    ResetDrop();
    SetupItems();

    /* The strip is the hit-test window: x 23..173, y 264..313. */
    c = DropCtx(&g_Item1x1, INV_REGION_POCKET0, 0, 0, 100, 300);
    Check(Inv_HandlePointer(&c), "  a drop on the ground strip is placed");
    Check(g_aGrid[2][3][3].pItemList == &g_Item1x1, "  it went into the unit's own cell");
    CheckEqI32(g_Unit.uTu, 20 - 5, "  the ground costs 5");
    CheckEqI32(g_Ui.nInvListCount, 1, "  the visible list was rebuilt");
    Check(g_Ui.apInvList[0] == &g_Item1x1, "  with the dropped item in it");

    /* The cell search walks down to the lowest layer with a floor sprite. */
    ResetDrop();
    g_aGrid[2][3][3].pFloorSpr = NULL;   /* the unit's own layer has no floor */
    g_aGrid[1][3][3].pFloorSpr = NULL;
    g_aGrid[0][3][3].pFloorSpr = &g_FloorSpr;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 100, 300);
    Check(Inv_HandlePointer(&c), "  a floorless layer still drops");
    Check(g_aGrid[0][3][3].pItemList == &g_Item1x1, "  down to the first layer with a floor");
    Check(g_aGrid[2][3][3].pItemList == NULL, "  and not into the unit's own layer");

    /* A ground drop that cannot pay raises the modal and falls through to the
     * restore, which puts the item back in the pocket it came from. */
    ResetDrop();
    g_Unit.uTu = 4;
    c = DropCtx(&g_Item1x1, INV_REGION_HAND_L, 0, 0, 100, 300);
    Check(!Inv_HandlePointer(&c), "  4 time units cannot pay for the ground");
    CheckEqI32(g_aModalY[0], 31, "  the time modal fired");
    Check(g_Unit.pHandL == &g_Item1x1, "  the restore put it back in the left hand");
    CheckEqI32(g_Unit.uTu, 4, "  and charged nothing");
}

static void TestDropSplit(void)
{
    InvDropCtx c;

    printf("Inv_HandlePointer -- the ammo split\n");
    ResetDrop();
    SetupItems();

    /* The split rect is x 185..221, y 99..140. */
    g_Item1x1.nAmmo = 5;
    g_Item1x1.nIndex = 6;
    g_aObjById[6].nFootW = 1;
    g_aObjById[6].nFootH = 1;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 200, 120);
    Check(Inv_HandlePointer(&c), "  a clipped weapon splits on the hand panel");
    Check(g_Unit.pHandR == &g_Item1x1, "  the weapon goes to the right hand");
    CheckEqI32(g_Item1x1.nAmmo, 0, "  and is left empty");
    Check(g_Unit.pHandL == &g_aAlloc[0], "  the clip becomes a new item in the left hand");
    CheckEqI32(g_aAlloc[0].nAmmo, 5, "  carrying the clip count");
    Check(g_aAlloc[0].pObj == &g_aObjById[6], "  and the ammo object the index names");
    Check(g_apPool[0] == &g_aAlloc[0], "  the new item entered the pool");
    CheckEqI32(g_nPool, 1, "  and the count grew");
    CheckEqI32(g_Unit.uTu, 20 - 6, "  the split costs 6");
    CheckSamples("  sample 17 on the split", 17, -1);

    /* An empty weapon, a 255 weapon and a two-handed weapon are all refused. */
    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 200, 120);
    Check(!Inv_HandlePointer(&c), "  an item with no clip cannot split");
    ResetDrop();
    g_Item1x1.nAmmo = 255;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 200, 120);
    Check(!Inv_HandlePointer(&c), "  255 means infinite ammo, so no split");
    g_Item1x1.nAmmo = 5;
    ResetDrop();
    g_O1x1.bTwoHanded = 1;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 200, 120);
    Check(!Inv_HandlePointer(&c), "  a two-handed item cannot split");
    g_O1x1.bTwoHanded = 0;
    ResetDrop();
    g_Item1x1.nAmmo = 5;
    g_Unit.pHandL = &g_aPocket[0][0];
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 200, 120);
    Check(!Inv_HandlePointer(&c), "  a full left hand blocks the split");

    /* The split checks its TU minimum before its Map+1-gated debit. */
    ResetDrop();
    SetupItems();
    g_Item1x1.nAmmo = 5;
    g_Item1x1.nIndex = 6;
    g_aObjById[6].nFootW = 1;
    g_aObjById[6].nFootH = 1;
    g_Unit.uTu = 5;
    g_Ui.bActionLatch = 1;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 200, 120);
    Check(!Inv_HandlePointer(&c), "  the split still needs 6 time units with only Map+2 set");
    CheckEqI32(g_aModalY[0], 31, "  and raises the time modal");

    /* Map+1 does not skip the minimum check. */
    ResetDrop();
    SetupItems();
    g_Item1x1.nAmmo = 5;
    g_Item1x1.nIndex = 6;
    g_aObjById[6].nFootW = 1;
    g_aObjById[6].nFootH = 1;
    g_Unit.uTu = 5;
    g_bInvNoTuCharge = 1;
    g_Ui.bActionLatch = 0;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 200, 120);
    Check(!Inv_HandlePointer(&c), "  Map+1 still needs 6 time units for the split");
    CheckEqI32(g_aModalY[0], 31, "  Map+1 does not suppress the low-TU modal");

    /* With enough TU, Map+1 skips only the debit. */
    ResetDrop();
    SetupItems();
    g_Item1x1.nAmmo = 5;
    g_Item1x1.nIndex = 6;
    g_aObjById[6].nFootW = 1;
    g_aObjById[6].nFootH = 1;
    g_Unit.uTu = PUFO_INV_TU_SPLIT;
    g_bInvNoTuCharge = 1;
    g_Ui.bActionLatch = 0;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 200, 120);
    Check(Inv_HandlePointer(&c), "  Map+1 allows a funded split");
    CheckEqI32(g_Unit.uTu, PUFO_INV_TU_SPLIT, "  the split takes no TU under Map+1");
    CheckEqI32(g_nModal, 0, "  no low-TU modal is raised");

    ResetDrop();
    SetupItems();
    g_Item1x1.nAmmo = 5;
    g_Item1x1.nIndex = 6;
    g_aObjById[6].nFootW = 1;
    g_aObjById[6].nFootH = 1;
    g_Unit.uTu = PUFO_INV_TU_SPLIT;
    g_Ui.bActionLatch = 1;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 200, 120);
    Check(Inv_HandlePointer(&c), "  Map+2 does not waive a funded split");
    CheckEqI32(g_Unit.uTu, 0, "  the split charges 6 when Map+1 is clear");
}

static void TestDropRestore(void)
{
    InvDropCtx c;

    printf("Inv_HandlePointer -- the restore\n");
    ResetDrop();
    SetupItems();

    /* No rect contains (0,0), so nothing is attempted and the item goes back. */
    c = DropCtx(&g_Item2x1, INV_REGION_BACKPACK, 0, 1, 0, 0);
    Check(!Inv_HandlePointer(&c), "  a point outside every rect drops nothing");
    CheckEqI32(g_Unit.uTu, 20, "  nothing was charged");
    CheckEqI32(g_nSamples, 0, "  and no sample was played");
    Check(g_Unit.apBackpack[0 + 4] == &g_Item2x1, "  the backpack anchor was restored");
    Check(g_Unit.apBackpack[1 + 4] == &g_Sentinel, "  with its covered slot");
    CheckEqI32(g_Unit.abBackpackMeta[5], 0, "  meta A");
    CheckEqI32(g_Unit.abBackpackMeta[13], 1, "  meta B");

    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_BELT, 2, 1, 0, 0);
    Check(!Inv_HandlePointer(&c), "  a belt source restores the belt");
    Check(g_Unit.apBelt[2 + 3] == &g_Item1x1, "  index 2 + 3*1");

    ResetDrop();
    c = DropCtx(&g_Item1x1, INV_REGION_POCKET3, 1, 0, 0, 0);
    Check(!Inv_HandlePointer(&c), "  a pocket-3 source restores pocket 3");
    Check(g_Unit.aPockets[3].apItem[1] == &g_Item1x1, "  slot 1 of pocket 3");

    ResetDrop();
    c = DropCtx(&g_Item1x1, 99, 0, 0, 0, 0);
    Check(!Inv_HandlePointer(&c), "  an unknown source region restores nothing");
    Check(g_Unit.pHandL == NULL && g_Unit.pHandR == NULL, "  the hands stay empty");
    Check(g_aGrid[2][3][3].pItemList == NULL, "  and the cell stays empty");

    /* The pool decrement is unconditional, even when the item was never in the
     * pool. That is the original's behaviour. */
    ResetDrop();
    SetupItems();
    SetupClip(&g_Unit.pHandL);
    g_Weapon.nAmmo = 0;
    g_Item1x1.nAmmo = 1;
    g_apPool[0] = &g_aGround[0];
    g_nPool = 2;
    c = DropCtx(&g_Item1x1, INV_REGION_GROUND, 0, 0, 10, 90);
    Check(Inv_HandlePointer(&c), "  a clip loads from outside the pool");
    CheckEqI32(g_nPool, 1, "  the count still dropped by one");
    CheckEqI32(g_nFreed, 1, "  and the item was still deleted");
    Check(g_apPool[0] == &g_aGround[0], "  no hole was closed because none was found");
}

/* --- Tact_EquipTeamFromCraft @ 0x3be24 -------------------------------------
 *
 * The mechanism these tests are really about: the original writes a hit rect origin into
 * 0xAD714 / 0xAD718, and those are pBank+0x1044 / pBank+0x1048, the pointer position
 * Inv_HandlePointer reads. It then re-tests at ptr + drag + 10. With the drag cleared that is
 * rect.x0 + 10, rect.y0 + 10, which lands inside the very rect whose origin was written - for
 * every one of the 25. That is how this function equips a soldier with no mouse at all. */

static ObjectRec   s_aEquipObj[24];
static ObjectRec  *s_apEquipObjects[24];
static TactItem    s_aEquipItem[24];
static MapCell     s_EquipCell;
static Soldier    *s_apEquipUnits[4];
static InvDropCtx  s_EquipDrop;
static u8          g_nEquipCount;

static void EquipFixture(void)
{
    ResetDrop();
    memset(s_aEquipObj, 0, sizeof(s_aEquipObj));
    memset(s_aEquipItem, 0, sizeof(s_aEquipItem));
    memset(&s_EquipCell, 0, sizeof(s_EquipCell));

    for (i32 i = 0; i < 24; ++i) {
        s_aEquipObj[i].nFootW = 1;
        s_aEquipObj[i].nFootH = 1;
        s_aEquipObj[i].bWeight = 5;
        s_aEquipObj[i].bTwoHanded = 0;
        s_aEquipObj[i].nCategory = 1;
        s_aEquipObj[i].bAmmoCap = 255;   /* no ammo bookkeeping unless a test asks for it */
        s_aEquipObj[i].uSortKey = (u16)i;
        s_apEquipObjects[i] = &s_aEquipObj[i];

        s_aEquipItem[i].pObj = &s_aEquipObj[i];
        s_aEquipItem[i].nAmmo = 8;
        s_aEquipItem[i].nIndex = (u16)i;
        s_aEquipItem[i].pNext = NULL;
    }

    /* 4 * (1 + 5 + 3) = 36, so a strength of 100 accepts everything and 36 rejects it. */
    g_Unit.uInvBurden = 1;
    g_Unit.auStat[8] = 100;
    g_Hit.nHitRegion = 0;   /* the loose items came from the ground list */
}

/* Build the loose list in the given order. */
static void EquipLoose(const i32 *aIds, i32 nCount)
{
    TactItem *pPrev = NULL;

    s_EquipCell.pItemList = NULL;
    for (i32 i = 0; i < nCount; ++i) {
        TactItem *pItem = &s_aEquipItem[aIds[i]];
        pItem->pNext = NULL;
        if (pPrev == NULL) {
            s_EquipCell.pItemList = pItem;
        } else {
            pPrev->pNext = pItem;
        }
        pPrev = pItem;
    }
}

static bool EquipListHas(TactItem *pWant)
{
    for (TactItem *pItem = s_EquipCell.pItemList; pItem != NULL; pItem = pItem->pNext) {
        if (pItem == pWant) {
            return true;
        }
    }
    return false;
}

static TactEquipCtx EquipCtx(void)
{
    static InvDropHooks s_h;
    TactEquipCtx ctx;

    s_h = DropHooks();
    memset(&s_EquipDrop, 0, sizeof(s_EquipDrop));
    s_EquipDrop.pHit = &g_Hit;
    s_EquipDrop.pUi = &g_Ui;
    g_bInvNoTuCharge = 1; /* Tact_InitMission runs the equipment pass with Map+1 set. */
    s_EquipDrop.pbInvNoTuCharge = &g_bInvNoTuCharge;
    s_EquipDrop.pSelUnit = &g_Unit;
    s_EquipDrop.pSentinel = &g_Sentinel;
    s_EquipDrop.pHooks = &s_h;

    memset(&ctx, 0, sizeof(ctx));
    ctx.pItemListCell = &s_EquipCell;
    ctx.apXcomUnits = s_apEquipUnits;
    ctx.pnXcomUnitCount = &g_nEquipCount;
    ctx.apObjects = s_apEquipObjects;
    ctx.pHit = &g_Hit;
    ctx.pDrop = &s_EquipDrop;
    return ctx;
}

static void TestEquipLeftHand(void)
{
    static const i32 aLoose[2] = { 11, 6 };
    TactEquipCtx ctx;

    printf("Tact_EquipTeamFromCraft: left hand\n");

    /* Table order is 6, 11, 5, 9, ... while the list order is 11 then 6, so a correct
     * implementation takes 6. */
    EquipFixture();
    EquipLoose(aLoose, 2);
    g_Unit.bUnk961 = 0;
    s_apEquipUnits[0] = &g_Unit;
    g_nEquipCount = 1;

    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);

    Check(g_Unit.pHandL == &s_aEquipItem[6], "the left hand follows the candidate table, not the list order");
    Check(!EquipListHas(&s_aEquipItem[6]), "the taken item left the loose list");
    Check(EquipListHas(&s_aEquipItem[11]), "the item that was not chosen stays");
    CheckEqI32(g_Unit.bUnk961, 1, "an unequipped soldier is marked equipped afterwards");

    /* The gate boundary. 4 * (1 + 5 + 3) = 36: a strength of 36 divides to 1 and is refused,
     * 37 divides to 0 and is accepted. */
    EquipFixture();
    EquipLoose(aLoose, 2);
    g_Unit.bUnk961 = 0;
    g_Unit.auStat[8] = 36;
    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);
    Check(g_Unit.pHandL == NULL, "strength 36 cannot carry it: 36 / 36 is 1, not 0");

    EquipFixture();
    EquipLoose(aLoose, 2);
    g_Unit.bUnk961 = 0;
    g_Unit.auStat[8] = 37;
    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);
    Check(g_Unit.pHandL == &s_aEquipItem[6], "strength 37 can: 36 / 37 is 0");

    /* An empty candidate is skipped: the hand gate also needs nAmmo non-zero. */
    EquipFixture();
    EquipLoose(aLoose, 2);
    g_Unit.bUnk961 = 0;
    s_aEquipItem[6].nAmmo = 0;
    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);
    Check(g_Unit.pHandL == &s_aEquipItem[11], "an empty first candidate falls through to the next");

    /* A zero strength is a divide by zero in the original. The port reports too heavy. */
    EquipFixture();
    EquipLoose(aLoose, 2);
    g_Unit.bUnk961 = 0;
    g_Unit.auStat[8] = 0;
    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);
    Check(g_Unit.pHandL == NULL, "a zero strength is refused rather than dividing by zero");
}

static void TestEquipThirdPickup(void)
{
    static const i32 aLoose[1] = { 18 };
    TactEquipCtx ctx;

    printf("Tact_EquipTeamFromCraft: the third pickup, and the drag offset it never clears\n");

    /* unk_A6328 order is 18, 17, 15, 16, 19, 14. The pickup stores aInvHitRects[10], and rect 10 is
     * x 160..179 / y 160..179 - BACKPACK slot 0,0. The belt rects are 16 through 24, so this is not a
     * belt pickup at all. With the drag clear the re-test lands at 170,170, inside rect 10. */
    EquipFixture();
    EquipLoose(aLoose, 1);
    g_Unit.bUnk961 = 0;
    g_Hit.nDragOffX = 0;
    g_Hit.nDragOffY = 0;
    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);

    Check(g_Unit.apBackpack[0] == &s_aEquipItem[18], "the third pickup stores rect 10 and lands in backpack slot 0,0");
    Check(!EquipListHas(&s_aEquipItem[18]), "and the item left the loose list");
    Check(g_Unit.apBelt[0] == NULL, "nothing goes to the belt, whose rects start at 16");
    CheckEqI32(g_Unit.uTu, 20, "the init equipment drop is free while Map+1 is set");

    /* ORIGINAL BUG, reproduced: at 0x3C4C8 this pickup stores the unit, the item and the pointer, but
     * unlike the two slot passes and the ammo pickup it never zeroes nInvDragOffX/Y. It inherits the
     * footprint offset the previous re-test left. A 2-wide item leaves -20, moving the re-test to 150,150,
     * which is inside no rect at all, so the slot is missed. */
    EquipFixture();
    EquipLoose(aLoose, 1);
    g_Unit.bUnk961 = 0;
    g_Hit.nDragOffX = -20;
    g_Hit.nDragOffY = -20;
    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);

    Check(g_Unit.apBackpack[0] != &s_aEquipItem[18], "a stale drag offset makes it miss the slot it targets");
    Check(!EquipListHas(&s_aEquipItem[18]), "but the item is taken out of the loose list either way");
}

static void TestEquipSlotSwap(void)
{
    static const i32 aLoose[2] = { 6, 9 };
    TactEquipCtx ctx;

    printf("Tact_EquipTeamFromCraft: the slot ammo swap\n");

    /* Slot 0 declares object 6 and is meant to end up holding object 9. The item carrying
     * object 6 has rounds, so its rounds move into a new item that keeps object 6, and the
     * slot's item takes the found item's identity and rounds. */
    EquipFixture();
    EquipLoose(aLoose, 2);
    s_aEquipObj[6].bAmmoCap = 24;
    s_aEquipItem[6].nAmmo = 5;
    s_aEquipItem[9].nAmmo = 3;
    g_Unit.bUnk961 = 1;              /* the re-run half, not the equip-from-craft half */
    g_Unit.auUnk962[0] = 6;
    g_Unit.abUnk1012[0] = 9;
    s_apEquipUnits[0] = &g_Unit;
    g_nEquipCount = 1;

    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);

    CheckEqI32(g_nPool, 1, "the displaced rounds become a new pooled item");
    Check(g_apPool[0] != NULL, "and the pool slot was filled");
    if (g_apPool[0] != NULL) {
        Check(g_apPool[0]->pObj == &s_aEquipObj[6], "the new item keeps the object the slot held");
        CheckEqI32(g_apPool[0]->nAmmo, 5, "and carries the rounds that were in the slot");
    }
    CheckEqI32(s_aEquipItem[6].nIndex, 9, "the slot item takes the found item's index");
    CheckEqI32(s_aEquipItem[6].nAmmo, 3, "and its rounds");
    Check(!EquipListHas(&s_aEquipItem[9]), "the found item is removed from the loose list");
    CheckEqI32(g_Unit.bUnk961, 1, "an already-equipped soldier keeps the flag");

    /* No entry has the sort key the slot wants. The original walks the whole list a second time
     * for a match its own entry gate made impossible - it only got here because nIndex differs
     * from abUnk1012[slot], and the match needs them equal - reaches the end, and changes
     * nothing. The swap at 0x3C0F0 is unreachable. */
    EquipFixture();
    EquipLoose(aLoose, 2);
    s_aEquipObj[6].bAmmoCap = 24;
    s_aEquipItem[6].nAmmo = 5;
    g_Unit.bUnk961 = 1;
    g_Unit.auUnk962[0] = 6;
    g_Unit.abUnk1012[0] = 20;
    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);

    CheckEqI32(g_nPool, 0, "an unmatched slot wants a new item but gets none");
    CheckEqI32(s_aEquipItem[6].nIndex, 6, "and the slot item's index is left alone");

    /* bAmmoCap 255 means the weapon never runs out, so no bookkeeping runs at all. */
    EquipFixture();
    EquipLoose(aLoose, 2);
    s_aEquipItem[6].nAmmo = 5;
    g_Unit.bUnk961 = 1;
    g_Unit.auUnk962[0] = 6;
    g_Unit.abUnk1012[0] = 9;
    ctx = EquipCtx();
    Tact_EquipTeamFromCraft(&ctx);
    CheckEqI32(g_nPool, 0, "an uncapped weapon does no ammo bookkeeping");
    CheckEqI32(s_aEquipItem[6].nIndex, 6, "so its index is not rewritten");
}

/* ------------------------------- Inv_CloseAndCommit @ 0x15a48 ------------------------------- */

static Soldier      g_cUnit[2];
static Soldier     *g_cUnits[2];
static u8           g_cUnitCount;
static TactItem     g_cItem[8];
static ObjectRec    g_cObj[8];
static ObjectRec   *g_cObjects[8];
static TactItem    *g_cPool[8];
static i32          g_cItemCount;
static InvHitState  g_cHit;
static u8           g_cCommitFlag;
static i32          g_cUiScreen;
static i32          g_cDeployCalls, g_cFocusCalls, g_cRefreshCalls, g_cVisCalls;
static Soldier     *g_cVisUnit[4];
static TactItem    *g_cFreed;
static InvStatHooks g_cStat;

static bool c_IsLarge(const Soldier *pUnit, void *pUser)
{
    (void)pUser;
    return pUnit == &g_cUnit[1];
}
static void c_Deploy(void *pUser) { (void)pUser; ++g_cDeployCalls; }
static void c_Focus(void *pUser) { (void)pUser; ++g_cFocusCalls; }
static void c_Refresh(void *pUser) { (void)pUser; ++g_cRefreshCalls; }
static void c_Vis(Soldier *pUnit, void *pUser)
{
    (void)pUser;
    if (g_cVisCalls < 4) { g_cVisUnit[g_cVisCalls] = pUnit; }
    ++g_cVisCalls;
}
static void c_Free(TactItem *pItem, void *pUser) { (void)pUser; g_cFreed = pItem; }
static u8 c_Weight(TactItem *pItem, void *pUser)
{
    (void)pUser;
    return (pItem != NULL && pItem->pObj != NULL) ? pItem->pObj->bWeight : 0;
}
static u8 c_TwoHanded(TactItem *pItem, void *pUser)
{
    (void)pUser;
    return (pItem != NULL && pItem->pObj != NULL) ? pItem->pObj->bTwoHanded : 0;
}
static u8 c_AmmoCap(TactItem *pItem, void *pUser)
{
    (void)pUser;
    return (pItem != NULL && pItem->pObj != NULL) ? pItem->pObj->bAmmoCap : 255;
}
static u8 c_AmmoWeight(i32 nIndex, void *pUser) { (void)nIndex; (void)pUser; return 1; }

static void c_Reset(void)
{
    i32 i;

    memset(g_cUnit, 0, sizeof(g_cUnit));
    memset(g_cItem, 0, sizeof(g_cItem));
    memset(g_cObj, 0, sizeof(g_cObj));
    memset(g_cObjects, 0, sizeof(g_cObjects));
    memset(g_cPool, 0, sizeof(g_cPool));
    memset(&g_cHit, 0, sizeof(g_cHit));
    memset(g_cVisUnit, 0, sizeof(g_cVisUnit));
    memset(&g_cStat, 0, sizeof(g_cStat));
    g_cUnitCount = 0;
    g_cItemCount = 0;
    g_cCommitFlag = 0;
    g_cUiScreen = 2;   /* the inventory screen id, so clearing it is observable */
    g_cDeployCalls = 0;
    g_cFocusCalls = 0;
    g_cRefreshCalls = 0;
    g_cVisCalls = 0;
    g_cFreed = NULL;
    g_cUnits[0] = &g_cUnit[0];
    g_cUnits[1] = &g_cUnit[1];

    for (i = 0; i < 8; ++i) {
        g_cObj[i].uSortKey = (u16)(10 + i);
        g_cObj[i].bWeight = 1;
        g_cObj[i].bAmmoCap = 255;
        g_cObjects[i] = &g_cObj[i];
        g_cItem[i].pObj = &g_cObj[i];
        g_cItem[i].nAmmo = 10;
        g_cItem[i].nIndex = (u16)i;
    }
    g_cUnit[0].auStat[8] = 100;
    g_cUnit[0].uInvBurden = 1;

    g_cStat.pfnObjWeight = c_Weight;
    g_cStat.pfnObjTwoHanded = c_TwoHanded;
    g_cStat.pfnObjAmmoCap = c_AmmoCap;
    g_cStat.pfnAmmoWeight = c_AmmoWeight;
}

static InvCommitCtx c_Ctx(void)
{
    static InvCommitHooks h;
    InvCommitCtx c;

    memset(&h, 0, sizeof(h));
    memset(&c, 0, sizeof(c));
    h.pfnUnitIsLarge = c_IsLarge;
    h.pfnDeploy = c_Deploy;
    h.pfnFocusSelected = c_Focus;
    h.pfnRefreshCells = c_Refresh;
    h.pfnUpdateVisibility = c_Vis;
    h.pfnFreeItem = c_Free;
    h.pInvStat = &g_cStat;

    c.apXcomUnits = g_cUnits;
    c.pnXcomUnitCount = &g_cUnitCount;
    c.apObjects = g_cObjects;
    c.pSelUnit = &g_cUnit[0];
    c.apItemPool = g_cPool;
    c.pnItemCount = &g_cItemCount;
    c.pHit = &g_cHit;
    c.pbCommitFlag = &g_cCommitFlag;
    c.pnUiScreen = &g_cUiScreen;
    c.pHooks = &h;
    return c;
}

static void TestCommitSnapshot(void)
{
    InvCommitCtx ctx;
    i32 i;

    printf("the inventory close snapshot\n");
    c_Reset();

    g_cUnitCount = 2;
    g_cCommitFlag = 1;

    /* Unit 0 carries a loadout. Unit 1 is large and must be skipped whole. */
    g_cUnit[0].aPockets[0].apItem[0] = &g_cItem[0];   /* slot 0: normal weapon */
    g_cItem[0].nAmmo = 12;
    g_cItem[0].nIndex = 9;
    g_cUnit[0].aPockets[0].apItem[1] = &g_cItem[1];   /* slot 1: a two-handed object */
    g_cObj[1].bTwoHanded = 1;
    g_cItem[1].nAmmo = 7;
    g_cItem[1].nIndex = 21;
    g_cUnit[0].pHandL = &g_cItem[2];                  /* slot 8: nAmmo 255, "never runs out" */
    g_cItem[2].nAmmo = 255;
    g_cItem[2].nIndex = 30;
    g_cUnit[0].pHandR = &g_cItem[3];                  /* slot 9: nAmmo 0 */
    g_cItem[3].nAmmo = 0;
    g_cItem[3].nIndex = 31;
    g_cUnit[0].apBackpack[7] = &g_cItem[4];           /* slot 15: the far backpack cell */
    g_cItem[4].nAmmo = 5;
    g_cItem[4].nIndex = 11;
    g_cUnit[0].apBelt[7] = &g_cItem[5];               /* slot 23: nIndex 200, low byte 0xC8 */
    g_cItem[5].nAmmo = 3;
    g_cItem[5].nIndex = 200;
    /* Cells the original's jumptable has no case for. */
    g_cUnit[0].apBackpack[5] = &g_cItem[6];
    g_cUnit[0].apBackpack[6] = &g_cItem[6];
    g_cUnit[0].apBelt[8] = &g_cItem[6];
    g_cUnit[1].pHandL = &g_cItem[0];

    for (i = 0; i < 25; ++i) {
        g_cUnit[0].auUnk962[i] = 7777;
        g_cUnit[0].abUnk1012[i] = 77;
        g_cUnit[1].auUnk962[i] = 7777;
        g_cUnit[1].abUnk1012[i] = 77;
    }

    ctx = c_Ctx();
    (void)Inv_CloseAndCommit(&ctx);

    CheckEqI32(g_cUnit[0].auUnk962[0], 10, "slot 0 (pocket 0 item 0) stores the object sort key (0x15BFC)");
    CheckEqI32(g_cUnit[0].abUnk1012[0], 9, "and the low byte of nIndex, the ammo type the weapon has loaded (0x15C28)");
    CheckEqI32(g_cUnit[0].auUnk962[1], 11, "slot 1 (pocket 0 item 1) stores its object too");
    CheckEqI32(g_cUnit[0].abUnk1012[1], 0, "but a two-handed object stores no ammo type (0x15C08..0x15C10)");
    CheckEqI32(g_cUnit[0].auUnk962[8], 12, "slot 8 is the LEFT hand, Soldier+0x104 (jumptable case 8)");
    CheckEqI32(g_cUnit[0].abUnk1012[8], 0, "and nAmmo 255 stores 0 (0x15C14..0x15C1C)");
    CheckEqI32(g_cUnit[0].auUnk962[9], 13, "slot 9 is the RIGHT hand, Soldier+0x108");
    CheckEqI32(g_cUnit[0].abUnk1012[9], 0, "and nAmmo 0 stores 0 as well (0x15C20..0x15C24)");
    CheckEqI32(g_cUnit[0].auUnk962[15], 14, "slot 15 is backpack[7] at Soldier+0x158 -- the walk jumps past 5 and 6");
    CheckEqI32(g_cUnit[0].abUnk1012[15], 11, "with its ammo type");
    CheckEqI32(g_cUnit[0].auUnk962[23], 15, "slot 23 is belt[7] at Soldier+0x188");
    CheckEqI32(g_cUnit[0].abUnk1012[23], 200 & 0xFF, "and only nIndex's low byte is stored: 200 becomes 0xC8");
    CheckEqI32(g_cUnit[0].auUnk962[10], 0, "an empty backpack slot is written as 0, not left alone (0x15C3C)");
    CheckEqI32(g_cUnit[0].auUnk962[24], 7777, "the 25th entry is outside the 24-slot walk");
    CheckEqI32(g_cUnit[0].abUnk1012[24], 77, "on both arrays");
    Check(g_cUnit[1].auUnk962[0] == 7777 && g_cUnit[1].abUnk1012[0] == 77,
          "the large unit is skipped whole, even though it carries an item (0x15AA0)");

    CheckEqI32(g_cCommitFlag, 0, "the commit flag is cleared (0x15C68)");
    CheckEqI32(g_cDeployCalls, 1, "then Tact_DeployTeamAndHwps runs once (0x15C6C)");
    CheckEqI32(g_cFocusCalls, 1, "MapView_FocusSelected once (0x15C70)");
    CheckEqI32(g_cRefreshCalls, 1, "and Map_RefreshCellMeta once (0x15C78)");
    CheckEqI32(g_cVisCalls, 2, "and Unit_UpdateVisibility runs for every unit, large ones included (0x15C98)");
    Check(g_cVisUnit[0] == &g_cUnit[0] && g_cVisUnit[1] == &g_cUnit[1], "in unit order");
}

static void TestCommitFlagClear(void)
{
    InvCommitCtx ctx;
    i32 i;

    printf("the snapshot only runs while the commit flag is set\n");
    c_Reset();

    g_cUnitCount = 2;
    g_cCommitFlag = 0;
    g_cUnit[0].pHandL = &g_cItem[0];
    for (i = 0; i < 25; ++i) {
        g_cUnit[0].auUnk962[i] = 7777;
    }

    ctx = c_Ctx();
    (void)Inv_CloseAndCommit(&ctx);

    CheckEqI32(g_cUnit[0].auUnk962[0], 7777, "nothing is snapshotted");
    CheckEqI32(g_cDeployCalls + g_cFocusCalls + g_cRefreshCalls + g_cVisCalls, 0,
               "and none of the four map refreshes runs");
    CheckEqI32(g_cUiScreen, 0, "while the screen id is cleared whatever the flag said (0x15F30)");
}

static void TestCommitMergeLeftHand(void)
{
    InvCommitCtx ctx;
    i32 nRet;

    printf("merging a clip into the left-hand priority weapon\n");
    c_Reset();

    /* The left hand holds a one-handed object with list priority 1: that is the weapon. */
    g_cUnit[0].pHandL = &g_cItem[0];
    g_cObj[0].bTwoHanded = 0;
    g_cObj[0].bListPri = 1;
    g_cObj[0].anAmmoTypeId[0] = 0;  /* resolves to the weapon itself: no match */
    g_cObj[0].anAmmoTypeId[1] = 3;  /* resolves to the clip's object: the match */
    g_cObj[0].anAmmoTypeId[2] = 0;
    /* The right hand holds the clip. */
    g_cUnit[0].pHandR = &g_cItem[3];
    g_cItem[3].nAmmo = 17;

    g_cPool[0] = &g_cItem[3];
    g_cPool[1] = &g_cItem[4];
    g_cPool[2] = &g_cItem[5];
    g_cItemCount = 3;

    ctx = c_Ctx();
    nRet = Inv_CloseAndCommit(&ctx);

    CheckEqI32(g_cItem[0].nIndex, 3, "the weapon takes the matching ammo type id into nIndex (0x15D38..0x15D44)");
    CheckEqI32(g_cItem[0].nAmmo, 17, "and the clip's ammo count (0x15D58..0x15D60)");
    Check(g_cUnit[0].pHandR == NULL, "the clip leaves the right hand (0x15D70)");
    Check(g_cUnit[0].pHandL == &g_cItem[0], "the weapon stays in the left hand");
    Check(g_cFreed == &g_cItem[3], "the clip's TactItem is deleted (0x15EFC)");
    CheckEqI32(g_cItemCount, 2, "and the pool count drops (0x15F18)");
    Check(g_cPool[0] == &g_cItem[5], "the hole is closed with the LAST pool entry (0x15EE0)");
    Check(g_cPool[1] == &g_cItem[4], "and the rest of the pool is untouched");
    Check(g_cHit.pHitItem == &g_cItem[3], "the merge scratch holds the clip (0x15CF4)");
    CheckEqI32(g_cUiScreen, 0, "the screen id is cleared (0x15F30)");
    CheckEqI32(nRet, (i32)Unit_RecalcInventoryStat(&g_cUnit[0], &g_cStat),
               "the return value is Unit_RecalcInventoryStat's (0x15F28)");
}

static void TestCommitMergeNoMatch(void)
{
    InvCommitCtx ctx;

    printf("no ammo type match leaves both hands alone\n");
    c_Reset();

    g_cUnit[0].pHandL = &g_cItem[0];
    g_cObj[0].bTwoHanded = 0;
    g_cObj[0].bListPri = 1;
    g_cObj[0].anAmmoTypeId[0] = 0;
    g_cObj[0].anAmmoTypeId[1] = 1;
    g_cObj[0].anAmmoTypeId[2] = 2;   /* none of these is the clip's object */
    g_cUnit[0].pHandR = &g_cItem[3];
    g_cItem[3].nAmmo = 17;
    g_cPool[0] = &g_cItem[3];
    g_cPool[1] = &g_cItem[4];
    g_cItemCount = 2;

    ctx = c_Ctx();
    (void)Inv_CloseAndCommit(&ctx);

    CheckEqI32(g_cItem[0].nIndex, 0, "nIndex is untouched (it was 0 and no id matched)");
    CheckEqI32(g_cItem[0].nAmmo, 10, "the weapon's ammo count is untouched");
    Check(g_cUnit[0].pHandR == &g_cItem[3] && g_cUnit[0].pHandL == &g_cItem[0],
          "both hands keep their item (0x15D34 -> 0x15D84)");
    Check(g_cFreed == NULL, "nothing is deleted");
    CheckEqI32(g_cItemCount, 2, "and the pool count is untouched");
}

static void TestCommitMergeNotPriority(void)
{
    InvCommitCtx ctx;

    printf("the left-hand branch needs list priority 1\n");
    c_Reset();

    g_cUnit[0].pHandL = &g_cItem[0];
    g_cObj[0].bTwoHanded = 0;
    g_cObj[0].bListPri = 0;   /* not a priority weapon */
    g_cObj[0].anAmmoTypeId[1] = 3;
    g_cUnit[0].pHandR = &g_cItem[3];
    /* The right-hand branch would need the RIGHT object to be one-handed with priority 1; the
     * clip's object has neither, so nothing merges at all. */
    g_cObj[3].bListPri = 0;
    g_cPool[0] = &g_cItem[3];
    g_cItemCount = 1;

    ctx = c_Ctx();
    (void)Inv_CloseAndCommit(&ctx);

    Check(g_cUnit[0].pHandR == &g_cItem[3] && g_cUnit[0].pHandL == &g_cItem[0],
          "a left-hand object with bListPri 0 takes no clip (0x15CE8..0x15CF0)");
    Check(g_cFreed == NULL, "and nothing is freed");
    CheckEqI32(g_cItemCount, 1, "the pool is untouched");
}

static void TestCommitMergeRightHand(void)
{
    InvCommitCtx ctx;

    printf("merging a clip into the right-hand priority weapon\n");
    c_Reset();

    /* The LEFT hand holds the clip, and its object is not a priority weapon, so the left-hand
     * branch is skipped and the mirror at 0x15DE8 runs. */
    g_cUnit[0].pHandL = &g_cItem[3];
    g_cObj[3].bListPri = 0;
    g_cItem[3].nAmmo = 23;
    /* The RIGHT hand is the priority weapon, and TWO of its ammo ids resolve to the clip object. */
    g_cUnit[0].pHandR = &g_cItem[0];
    g_cObj[0].bTwoHanded = 0;
    g_cObj[0].bListPri = 1;
    g_cObj[0].anAmmoTypeId[0] = 3;
    g_cObj[0].anAmmoTypeId[1] = 4;
    g_cObj[0].anAmmoTypeId[2] = 0;
    g_cObjects[4] = &g_cObj[3];   /* the second id resolves to the same clip object */

    g_cPool[0] = &g_cItem[3];
    g_cPool[1] = &g_cItem[4];
    g_cPool[2] = &g_cItem[5];
    g_cItemCount = 3;

    ctx = c_Ctx();
    (void)Inv_CloseAndCommit(&ctx);

    CheckEqI32(g_cItem[0].nIndex, 4, "the loop does not stop at the first match, so the LAST one wins (0x15E80..0x15E88)");
    CheckEqI32(g_cItem[0].nAmmo, 23, "and the weapon takes the clip's ammo count (0x15E58..0x15E5C)");
    Check(g_cUnit[0].pHandL == NULL, "the clip leaves the left hand (0x15E6C)");
    Check(g_cUnit[0].pHandR == &g_cItem[0], "the weapon stays in the right hand");
    Check(g_cFreed == &g_cItem[3], "the clip's TactItem is deleted");
    CheckEqI32(g_cItemCount, 2, "the count drops");
    Check(g_cPool[0] == &g_cItem[5], "the hole is closed with the last entry");
    Check(g_cHit.pHitItem == &g_cItem[3], "and the scratch holds the clip (0x15E0C)");
}

static void TestCommitMergePoolMiss(void)
{
    InvCommitCtx ctx;

    printf("the pool decrement is unconditional\n");
    c_Reset();

    g_cUnit[0].pHandL = &g_cItem[0];
    g_cObj[0].bTwoHanded = 0;
    g_cObj[0].bListPri = 1;
    g_cObj[0].anAmmoTypeId[2] = 3;
    g_cUnit[0].pHandR = &g_cItem[3];
    g_cItem[3].nAmmo = 9;

    /* The clip is NOT in the pool. */
    g_cPool[0] = &g_cItem[4];
    g_cPool[1] = &g_cItem[5];
    g_cItemCount = 2;

    ctx = c_Ctx();
    (void)Inv_CloseAndCommit(&ctx);

    CheckEqI32(g_cItem[0].nIndex, 3, "the merge still happened");
    Check(g_cFreed == &g_cItem[3], "the clip is still deleted");
    CheckEqI32(g_cItemCount, 1, "and the count still drops even though the item was not in the pool (0x15DD0)");
    Check(g_cPool[0] == &g_cItem[4] && g_cPool[1] == &g_cItem[5], "no pool entry is moved");
}

static void TestCommitMergeSingleEntry(void)
{
    InvCommitCtx ctx;

    printf("a one-entry pool is not filled\n");
    c_Reset();

    g_cUnit[0].pHandL = &g_cItem[0];
    g_cObj[0].bTwoHanded = 0;
    g_cObj[0].bListPri = 1;
    g_cObj[0].anAmmoTypeId[0] = 3;
    g_cUnit[0].pHandR = &g_cItem[3];
    g_cItem[3].nAmmo = 9;
    g_cPool[0] = &g_cItem[3];
    g_cItemCount = 1;

    ctx = c_Ctx();
    (void)Inv_CloseAndCommit(&ctx);

    Check(g_cFreed == &g_cItem[3], "the clip is deleted");
    CheckEqI32(g_cItemCount, 0, "the count drops to 0");
    Check(g_cPool[0] == &g_cItem[3], "and nothing is copied over it: with a count of 1 the fill is skipped (0x15D9C)");
}

static void TestCommitGuards(void)
{
    InvCommitCtx ctx;

    printf("the close-and-commit guards\n");
    c_Reset();

    CheckEqI32(Inv_CloseAndCommit(NULL), 0, "a null context returns 0");

    g_cUnitCount = 1;
    g_cCommitFlag = 1;
    ctx = c_Ctx();
    ctx.apItemPool = NULL;
    CheckEqI32(Inv_CloseAndCommit(&ctx), 0, "a missing item pool refuses the whole close");
    CheckEqI32(g_cDeployCalls, 0, "so not even the map refresh runs");
    CheckEqI32(g_cUiScreen, 2, "and the screen id is left alone");

    ctx = c_Ctx();
    ctx.pSelUnit = NULL;
    CheckEqI32(Inv_CloseAndCommit(&ctx), 0, "so does a missing selected unit");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    Reset(); TestGround();
    Reset(); TestPockets();
    Reset(); TestHands();
    Reset(); TestBackpack();
    Reset(); TestBelt();
    Reset(); TestMisses();
    Reset(); TestClear();
    Reset(); TestDropPocket();
    Reset(); TestDropHands();
    Reset(); TestDropGrid();
    Reset(); TestDropGround();
    Reset(); TestDropSplit();
    Reset(); TestDropRestore();
    TestEquipLeftHand();
    TestEquipThirdPickup();
    TestEquipSlotSwap();
    TestCommitSnapshot();
    TestCommitFlagClear();
    TestCommitMergeLeftHand();
    TestCommitMergeNoMatch();
    TestCommitMergeNotPriority();
    TestCommitMergeRightHand();
    TestCommitMergePoolMiss();
    TestCommitMergeSingleEntry();
    TestCommitGuards();
    Reset(); TestPockets();
    Reset(); TestHands();
    Reset(); TestBackpack();
    Reset(); TestBelt();
    Reset(); TestMisses();
    Reset(); TestClear();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
