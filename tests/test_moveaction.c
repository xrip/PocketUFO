/* Tests for Tact_ProcessMoveAction @ 0x4f3f0.
 *
 * The point of these checks is the state the FSM leaves behind, because that is
 * what the animation and the turn code read next. Sounds are not asserted: the
 * port's Sound_Play is a no-op without an audio backend.
 */
#include "game/pufo_tactstate.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

#include <stdio.h>
#include <string.h>

#include "pufo_test.h"

static inline void Check(const char *pWhat, int bOk) { TestCheck(pWhat, bOk); }

static MapCell  g_aCells[4][12][12];
static Soldier  g_aUnits[4];
static TactState g_State;

static MapCell *Lookup(i32 nX, i32 nY, i32 nZ, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nX >= 12 || nY < 0 || nY >= 12 || nZ < 0 || nZ >= 4) {
        return NULL;
    }
    return &g_aCells[nZ][nY][nX];
}

static bool  g_bArmorWarn;
static int   g_nStatusPushed;
static int   g_nStatusMsg;
static int   g_nVisRefreshed;

static bool ArmorWarn(const Soldier *pUnit, i32 nTuCost, void *pUser)
{
    (void)pUnit; (void)nTuCost; (void)pUser;
    return g_bArmorWarn;
}
static void PushStatus(i32 nMsg, void *pUser)
{
    (void)pUser;
    ++g_nStatusPushed;
    g_nStatusMsg = nMsg;
}
static void UpdateVisibility(Soldier *pUnit, void *pUser)
{
    (void)pUnit; (void)pUser;
    ++g_nVisRefreshed;
}

static i8          g_nLayerScratch;
static i32         g_nMaxLayerClamp;
static i32         g_nAnimPhase;
static i32         g_nShotCount;
static u8          g_bActionLatch;
static u8          g_bFireStatus1;
static TactItem   *g_pSelWeapon;
static Soldier    *g_pThrowTarget;
static i32         g_nUnk1405C;
static Soldier    *g_apXcom[30];
static Soldier    *g_apEnemy[30];
static Soldier    *g_apCiv[30];
static u8          g_nXcomCount;
static u8          g_nEnemyCount;
static i32         g_nCivCount;
static ScatterOffset g_Scatter;
static double      g_aAdMt[10];
static MoveActionCtx g_Ctx;

static void ResetWorld(void)
{
    memset(g_aCells, 0, sizeof(g_aCells));
    memset(g_aUnits, 0, sizeof(g_aUnits));
    memset(&g_State, 0, sizeof(g_State));
    memset(&g_Scatter, 0, sizeof(g_Scatter));
    memset(g_aAdMt, 0, sizeof(g_aAdMt));
    memset(g_apXcom, 0, sizeof(g_apXcom));
    memset(g_apEnemy, 0, sizeof(g_apEnemy));
    memset(g_apCiv, 0, sizeof(g_apCiv));

    for (i32 z = 0; z < 4; ++z) {
        for (i32 y = 0; y < 12; ++y) {
            for (i32 x = 0; x < 12; ++x) {
                MapCell_Init(&g_aCells[z][y][x], (u8)x, (u8)y, (u8)z);
            }
        }
    }

    g_bArmorWarn = false;
    g_nStatusPushed = 0;
    g_nStatusMsg = -1;
    g_nVisRefreshed = 0;
    g_nLayerScratch = 0;
    g_nMaxLayerClamp = 0;
    g_nAnimPhase = 0;
    g_nShotCount = 0;
    g_bActionLatch = 0;
    g_bFireStatus1 = 0;
    g_pSelWeapon = NULL;
    g_pThrowTarget = NULL;
    g_nUnk1405C = 0;
    g_nXcomCount = 0;
    g_nEnemyCount = 0;
    g_nCivCount = 0;

    g_Ctx.geom.nDimX = 1;
    g_Ctx.geom.nDimY = 1;
    g_Ctx.pfnLookup = Lookup;
    g_Ctx.pUser = NULL;
    g_Ctx.pnLayerScratch = &g_nLayerScratch;
    g_Ctx.pnMaxLayerClamp = &g_nMaxLayerClamp;
    g_Ctx.pnAnimPhase = &g_nAnimPhase;
    g_Ctx.pnShotCount = &g_nShotCount;
    g_Ctx.pbActionLatch = &g_bActionLatch;
    g_Ctx.pbFireStatus1 = &g_bFireStatus1;
    g_Ctx.ppSelWeapon = &g_pSelWeapon;
    g_Ctx.ppThrowTarget = &g_pThrowTarget;
    g_Ctx.pnUnk1405C = &g_nUnk1405C;
    g_Ctx.bAnimBusy = false;
    g_Ctx.pTrackUnit = NULL;
    g_Ctx.apShotCells = NULL;
    g_Ctx.apXcomUnits = g_apXcom;
    g_Ctx.apEnemyUnits = g_apEnemy;
    g_Ctx.apCivilians = g_apCiv;
    g_Ctx.pnXcomCount = &g_nXcomCount;
    g_Ctx.pnEnemyCount = &g_nEnemyCount;
    g_Ctx.pnCivilianCount = &g_nCivCount;
    g_Ctx.pAim = NULL;
    g_Ctx.pTables = NULL;
    g_Ctx.pScatter = &g_Scatter;
    g_Ctx.pAdMtX = g_aAdMt;
    g_Ctx.pAdMtY = g_aAdMt;
    g_Ctx.pAdMtZ = g_aAdMt;
    g_Ctx.pThrowArc = NULL;
    g_Ctx.pThrowLanding = NULL;
    g_Ctx.pThrowBudget = NULL;
    g_Ctx.pInvHooks = NULL;
    g_Ctx.bThrowHitStop = false;
    g_Ctx.pTraceExcludeSel = NULL;
    g_Ctx.pTraceExcludeExtra = NULL;
    g_Ctx.pfnArmorTuWarn = ArmorWarn;
    g_Ctx.pfnPushStatus = PushStatus;
    g_Ctx.pfnUpdateVisibility = UpdateVisibility;
    g_Ctx.pCallbackUser = NULL;
    g_Ctx.pVisCtx = NULL;

    g_State.nQueueCursor = 0;
    g_State.aQueue[0].pUnit = &g_aUnits[0];
    g_aUnits[0].pCell = &g_aCells[0][2][2];
    g_aUnits[0].uTu = 20;
    g_aUnits[0].auStat[8] = 10;
    g_aUnits[0].uStunOrArmor = 50;
    MapCell_SetOccupant(g_aUnits[0].pCell, &g_aUnits[0]);
}

static TactQueueEntry *Entry(void) { return &g_State.aQueue[0]; }
/* Tact_RefreshTeamVisibility @ 0x4f368 appends its end mark at cursor + 1
 * (0x4f388: byte 0x50 = aQueue base 0x18 + one 56-byte entry), while the abort
 * keeps its own 13 on the cursor's entry. */
static TactQueueEntry *NextEntry(void) { return &g_State.aQueue[1]; }
#define ABORTED (Entry()->nCmd == 13 && NextEntry()->nCmd == TACTCMD_END_MARK)

int main(void)
{
    printf("cmd 0 turn\n");
    ResetWorld();
    Entry()->nCmd = 0;
    Entry()->nFacing = 3;
    g_aUnits[0].bFacing = 1;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("bFlag0 set and bFlag1 cleared",
          g_State.bFlag0 == 1 && g_State.bFlag1 == 0);
    Check("nPhaseMax is the facing delta", g_State.nPhaseMax == 2);
    Check("nPhaseTick reset", g_State.nPhaseTick == 0);
    Check("nTurnDir is +/-1",
          g_State.aQueue[0].nTurnDir == 1 || g_State.aQueue[0].nTurnDir == -1);
    Check("no abort", Entry()->nCmd == 0 && g_nStatusPushed == 0);

    ResetWorld();
    g_aUnits[0].uTu = 0;
    Entry()->nCmd = 0;
    Entry()->nFacing = 3;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("a unit with no TU aborts", ABORTED);
    Check("and pushes the no-time record",
          g_nStatusPushed == 1 && g_nStatusMsg == MOVE_MSG_NO_TIME);

    printf("cmd 1 step\n");
    ResetWorld();
    Entry()->nCmd = 1;
    Entry()->nFacing = 2;   /* dx +1 */
    Entry()->nDelta = 0;
    g_aUnits[0].bFacing = 1;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("step into a free cell is not aborted", Entry()->nCmd == 1);
    Check("pStepFromCell records the origin",
          g_State.pStepFromCell == g_aUnits[0].pCell);
    Check("the layer scratch is the unit layer plus one", g_nLayerScratch == 1);
    Check("the animation phase is armed", g_nAnimPhase == 1);
    Check("nPhaseTick reset", g_State.nPhaseTick == 0);
    Check("facing 1 draws with offset (4,0)",
          g_State.nStepDrawOffX == 4 && g_State.nStepDrawOffY == 0
          && g_aUnits[0].nDrawOffX == 4 && g_aUnits[0].nDrawOffY == 0);
    Check("the walk flag is set", (g_aUnits[0].bFlags & 2u) != 0);
    Check("no ceiling means a zero frame divisor", g_State.nStepFrameDiv == 0);

    ResetWorld();
    Entry()->nCmd = 1;
    Entry()->nFacing = 2;
    Entry()->nDelta = 1;
    g_State.nStepHeightDst = 0;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("climbing one layer sets the destination height to 40",
          g_State.nStepHeightDst == 40 && g_State.nStepFrameDiv == 5);

    ResetWorld();
    MapCell_SetOccupant(&g_aCells[0][2][3], &g_aUnits[1]);
    Entry()->nCmd = 1;
    Entry()->nFacing = 2;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("a blocked destination aborts", ABORTED);
    Check("a blocked step does not set the walk flag",
          (g_aUnits[0].bFlags & 2u) == 0);

    ResetWorld();
    g_aUnits[0].uTu = 2;
    Entry()->nCmd = 1;
    Entry()->nFacing = 2;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("three TU is the price of a step", ABORTED);
    Check("and too little TU pushes the no-time record",
          g_nStatusPushed == 1 && g_nStatusMsg == MOVE_MSG_NO_TIME);

    ResetWorld();
    g_aUnits[0].uInvBurden = 100;
    g_aUnits[0].uStunOrArmor = 5;   /* 4*100/10 + 2 = 42 > 5 */
    Entry()->nCmd = 1;
    Entry()->nFacing = 2;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("an over-burdened unit aborts with the armour record",
          ABORTED && g_nStatusMsg == MOVE_MSG_ARMOUR_BLOCK);

    ResetWorld();
    g_bArmorWarn = true;
    Entry()->nCmd = 1;
    Entry()->nFacing = 2;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("the armour warning aborts without a message",
          ABORTED && g_nStatusPushed == 0);

    printf("cmd 2, 3, 7, 8, 0xA, 0xF, 0x10\n");
    ResetWorld();
    Entry()->nCmd = 2;
    g_aUnits[0].bFlags = 0x06;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("cmd 2 clears only the walk flag", g_aUnits[0].bFlags == 0x04);

    ResetWorld();
    Entry()->nCmd = 3;
    g_State.nPhaseTick = 7;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("cmd 3 restarts the phase", g_State.nPhaseTick == 0);
    Check("cmd 3 costs no TU here", g_aUnits[0].uTu == 20);

    ResetWorld();
    Entry()->nCmd = 7;
    g_aUnits[0].bFlags = 0x01;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("standing from a kneel costs two TU", g_aUnits[0].uTu == 18);
    Check("and clears the kneel flag", g_aUnits[0].bFlags == 0);

    ResetWorld();
    Entry()->nCmd = 7;
    g_aUnits[0].bFlags = 0x00;
    g_aUnits[0].uTu = 20;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("cmd 7 does nothing when the unit is not kneeling",
          g_aUnits[0].uTu == 20 && Entry()->nCmd == 7);

    ResetWorld();
    Entry()->nCmd = 8;
    g_aUnits[0].bFlags = 0x01;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("the death flash leaves no flash flag behind",
          (g_aUnits[0].bFlags & 0x20u) == 0 && g_nAnimPhase == 0);

    ResetWorld();
    Entry()->nCmd = 0xA;
    g_aUnits[0].bFlags = 0x07;
    g_nMaxLayerClamp = 2;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("cmd 0xA clears the aim flag", g_aUnits[0].bFlags == 0x03);
    Check("and reloads the layer from the clamp", g_nLayerScratch == 2);

    ResetWorld();
    Entry()->nCmd = 0xF;
    g_nMaxLayerClamp = 3;
    g_nUnk1405C = 9;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("cmd 0xF reloads the layer and sets the fire tick",
          g_nLayerScratch == 3 && g_nUnk1405C == 1);

    ResetWorld();
    Entry()->nCmd = 0x10;
    g_State.bPhaseActive = 1;
    g_State.gap_70112[1] = 1;
    g_State.nPhaseTick = 4;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("cmd 0x10 ends an active phase",
          g_State.gap_70112[1] == 0 && g_State.nPhaseTick == 0
          && Entry()->nCmd == 0x10);

    ResetWorld();
    Entry()->nCmd = 0x10;
    g_State.bPhaseActive = 0;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    /* LABEL_170 sets 13 and returns without the visibility refresh, so unlike
     * the other aborts this one leaves 13 in the entry. */
    Check("cmd 0x10 aborts when no phase is running", Entry()->nCmd == 13);

    printf("cmd 4 turn trace\n");
    ResetWorld();
    Entry()->nCmd = 4;
    Entry()->nFacing = 2;   /* dx +1, dy 0 */
    Entry()->nX = 2;
    Entry()->nY = 5;
    Entry()->nZ = 0;
    Entry()->nTurnDir = 3;
    g_aCells[0][5][2].bFlags29 = 0x00;
    g_aCells[0][5][3].bFlags29 = 0x00;
    g_aCells[0][5][4].bFlags29 = 0x00;
    g_aCells[0][5][5].bFlags29 = 0x00;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("the three swept cells carry bits 0 and 1",
          g_aCells[0][5][2].bFlags29 == 0x03
          && g_aCells[0][5][3].bFlags29 == 0x03
          && g_aCells[0][5][4].bFlags29 == 0x03);
    Check("the cell past the sweep is untouched",
          g_aCells[0][5][5].bFlags29 == 0x00);
    Check("the trace coordinates are left behind",
          g_State.nTraceX == 4 && g_State.nTraceY == 5 && g_State.nTraceZ == 0);

    ResetWorld();
    Entry()->nCmd = 4;
    Entry()->nFacing = 0;   /* dx 0, dy -1 */
    Entry()->nX = 2;
    Entry()->nY = 5;
    Entry()->nZ = 0;
    Entry()->nTurnDir = 2;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("a facing other than 2 clears bit 1 instead of setting it",
          g_aCells[0][5][2].bFlags29 == 0x01
          && g_aCells[0][4][2].bFlags29 == 0x01);

    printf("cmd 0xB recover\n");
    ResetWorld();
    Entry()->nCmd = 0xB;
    g_aUnits[0].nUnitType = 0;
    g_aUnits[0].bFlags = 0x20;
    g_aUnits[0].nAnimPose = 9;
    g_apCiv[0] = &g_aUnits[1];
    g_apCiv[1] = &g_aUnits[0];
    g_nCivCount = 2;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("an XCOM unit goes back on the XCOM roster",
          g_nXcomCount == 1 && g_apXcom[0] == &g_aUnits[0]
          && g_aUnits[0].bStatus == 0);
    Check("and off the civilian roster, last entry copied down",
          g_nCivCount == 1 && g_apCiv[1] == &g_aUnits[0]);
    Check("the unit is re-flagged as alive and posed",
          g_aUnits[0].bFlags == 0 && g_aUnits[0].nAnimPose == 1);
    Check("visibility was refreshed for the team", g_nVisRefreshed == 0);

    ResetWorld();
    Entry()->nCmd = 0xB;
    g_aUnits[0].nUnitType = 7;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("a type above 3 goes on the enemy roster",
          g_nEnemyCount == 1 && g_apEnemy[0] == &g_aUnits[0]
          && g_aUnits[0].bStatus == 1);

    printf("cmd 0x12 fire sound action\n");
    ResetWorld();
    Entry()->nCmd = 0x12;
    Entry()->nTuCost = 6;
    {
        ObjectRecFull Rec;
        TactItem      Weapon;
        memset(&Rec, 0, sizeof(Rec));
        memset(&Weapon, 0, sizeof(Weapon));
        Weapon.pObj = &Rec;
        Rec.uSortKey = 1;
        Entry()->pWeapon = &Weapon;
        g_aCells[0][3][3].bFlags29 = 0xFF;
        g_State.gap_70112[1] = 0;
        Tact_ProcessMoveAction(&g_State, &g_Ctx);
        Check("the TU cost is charged as a word", g_aUnits[0].uTu == 14);
        Check("the phase counter restarts", g_State.nPhaseCounter == 0);
        Check("and the fire flag is raised", g_State.gap_70112[1] == 1);
        Check("the sweep ran over the whole grid",
              g_aCells[0][3][3].bFlags29 == 0xDF);
    }

    ResetWorld();
    Entry()->nCmd = 0x12;
    Entry()->nTuCost = 30;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
    Check("too little TU aborts cmd 0x12 with the no-time record",
          ABORTED && g_nStatusMsg == MOVE_MSG_NO_TIME);

    return TestSummary("test_moveaction");
}
