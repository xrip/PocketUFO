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

    g_Obj.nFootW = 2;   /* -20 * 2 / 2 = -20 */
    g_Obj.nFootH = 3;   /* -20 * 3 / 2 = -30 */

    for (i32 i = 0; i < 4; ++i) {
        g_aGround[i].pObj = &g_Obj;
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

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    Reset(); TestGround();
    Reset(); TestPockets();
    Reset(); TestHands();
    Reset(); TestBackpack();
    Reset(); TestBelt();
    Reset(); TestMisses();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
