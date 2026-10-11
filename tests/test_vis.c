/* test_vis.c - parity checks for the visibility update.
 * Evidence: Unit_UpdateVisibility @ 0x48580. */
#include "game/pufo_unit.h"
#include "game/pufo_los.h"
#include "game/pufo_vistry.h"
#include "game/pufo_combat.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_ballist.h"
#include "game/pufo_turn.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_nChecks = 0;
static int g_nFails = 0;

static void Check(const char *pWhat, int bOk)
{
    ++g_nChecks;
    if (!bOk) {
        ++g_nFails;
        printf("  FAIL %s\n", pWhat);
    }
}

static void CheckEqI32(const char *pWhat, i32 nGot, i32 nWant)
{
    if (nGot != nWant) {
        printf("  FAIL %s (got %d, want %d)\n", pWhat, nGot, nWant);
    }
    Check(pWhat, nGot == nWant);
}

static VisRay g_aRays[PUFO_VIS_RAY_COUNT];
static u8 g_aSteps[PUFO_VIS_TABLE_BYTES];
static LosTables g_Tables;
static MapCell g_aCells[4][40][40];
static Soldier g_aUnits[8];
static Mc1Rec g_aSpr[8];
static Soldier *apEnemy[4];
static Soldier *apXcom[4];
static SpotCtx g_Spot;
static i32 g_nFlag1036;
static Soldier *g_pScratch;

static MapCell *CellLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nY < 0 || nLayer < 0 || nX >= 40 || nY >= 40 || nLayer >= 4) {
        return NULL;
    }
    return &g_aCells[nLayer][nY][nX];
}

static MapCell *At(i32 nX, i32 nY, i32 nL)
{
    return &g_aCells[nL][nY][nX];
}

static void Reset(void)
{
    memset(g_aCells, 0, sizeof(g_aCells));
    memset(g_aUnits, 0, sizeof(g_aUnits));
    memset(g_aSpr, 0, sizeof(g_aSpr));
    memset(&g_Spot, 0, sizeof(g_Spot));
    g_nFlag1036 = 9;
    g_Spot.bBusy = true;
    g_Spot.pnTrajFront1036 = &g_nFlag1036;
    g_pScratch = (Soldier *)0x1;
    memset(&g_Tables, 0, sizeof(g_Tables));   /* no loft masks: nothing is covered */
    /* The scan reads the unit cell's own coordinates, exactly as the original reads
     * pCell+26 / pCell+27 / pCell+28, so the grid must carry them. */
    for (i32 l = 0; l < 4; ++l) {
        for (i32 y = 0; y < 40; ++y) {
            for (i32 x = 0; x < 40; ++x) {
                g_aCells[l][y][x].nX = (u8)x;
                g_aCells[l][y][x].nY = (u8)y;
                g_aCells[l][y][x].nZ = (u8)l;
            }
        }
    }
}

/* Place a unit. bStatus 0 is operative; nUnitType 4 is a large type. */
static Soldier *Place(i32 nSlot, i32 nX, i32 nY, i32 nL, u8 bStatus, u8 nType)
{
    MapCell *pCell = At(nX, nY, nL);
    Soldier *pU = &g_aUnits[nSlot];

    memset(pU, 0, sizeof(*pU));
    pU->bStatus = bStatus;
    pU->nUnitType = nType;
    pU->pCell = pCell;
    pCell->pUnit = pU;
    return pU;
}

static int Flag29(MapCell *pCell, u32 nBit)
{
    return (pCell->bFlags29 & nBit) != 0;
}

static int Flag44(MapCell *pCell)
{
    return (pCell->bFlags44 & 1u) != 0;
}

static void Run(Soldier *pUnit, UnitRosters *pRosters, u8 nMissionType)
{
    VisCtx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.pSpot = &g_Spot;
    ctx.pTables = &g_Tables;
    ctx.pfnLookup = CellLookup;
    ctx.pUser = NULL;
    ctx.pGeom = &(MapGeom){ 4, 4 };
    ctx.apRays = g_aRays;
    ctx.pRosters = pRosters;
    ctx.ppScratchUnit = &g_pScratch;
    ctx.nMissionType = nMissionType;
    Unit_UpdateVisibility(pUnit, &ctx);
}

static UnitRosters NoRosters(void)
{
    UnitRosters r;

    memset(&r, 0, sizeof(r));
    r.apXcom = apXcom;
    r.apEnemy = apEnemy;
    return r;
}


/* An affordable weapon so Unit_SelectBestFireMode succeeds. auStat[3] is the
 * duel's reaction stat: 100 never loses the roll and 0 always does, which makes
 * Unit_bFacingAngleDuel deterministic without seeding the generator. */
static ObjectRecFull g_wWeapon;
static TactItem g_handL;
static TactCtx272 g_TactCtx;
static AimState g_Aim;
static ActionCostTable g_Costs;
static FireGate g_Gate;
static u8 g_bFireStatus1;
static TactItem *g_pSelItem;
static i32 g_nSelMode;

static bool GateAllowAll(i32 nSortKey, void *pUser)
{
    (void)nSortKey;
    (void)pUser;
    return true;
}

static void ArmShooter(Soldier *pU)
{
    memset(&g_wWeapon, 0, sizeof(g_wWeapon));
    memset(&g_handL, 0, sizeof(g_handL));
    memset(&g_Costs, 0, sizeof(g_Costs));
    memset(&g_TactCtx, 0, sizeof(g_TactCtx));
    memset(&g_Aim, 0, sizeof(g_Aim));
    g_bFireStatus1 = 7;
    g_Gate.pfnWeaponFireable = GateAllowAll;
    g_Gate.pUser = NULL;

    g_handL.pObj = &g_wWeapon;
    g_handL.nAmmo = 10;
    g_wWeapon.abArmorTu[0] = 20;
    g_wWeapon.abCostPct[0] = 50;
    g_wWeapon.uSortKey = 3;

    pU->pHandL = &g_handL;
    pU->pHandR = NULL;
    pU->auStat[0] = 100;
    pU->auStat[3] = 100;
    pU->auStat[4] = 80;
    pU->uTu = 50;

    /* Unit_bFacingAngleDuel is decided by Game_RandU16 draws. Seed 89 makes the
     * shooter's reaction roll non-zero, which is what the duel compares; with an
     * unseeded generator the first draw is 41 and the roll truncates to 0, so the
     * duel rejects the shot whatever the stats and facings are. */
    srand(89);
}

static void RunOpp(Soldier *pTarget, Soldier *pShooter, UnitRosters *pRosters)
{
    VisCtx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.pSpot = &g_Spot;
    ctx.pTables = &g_Tables;
    ctx.pfnLookup = CellLookup;
    ctx.pGeom = &(MapGeom){ 4, 4 };
    ctx.apRays = g_aRays;
    ctx.pRosters = pRosters;
    ctx.ppScratchUnit = &g_pScratch;
    ctx.pAim = &g_Aim;
    ctx.pTactCtx = &g_TactCtx;
    ctx.pbFireStatus1 = &g_bFireStatus1;
    Unit_bTryOpportunityFire(pTarget, pShooter, &g_Gate, &g_pSelItem, &g_nSelMode,
                             &g_Costs, &ctx);
}

static void TestTryOpportunityFire(void)
{
    UnitRosters r = NoRosters();
    Soldier *pShooter;
    Soldier *pTarget;
    Soldier *pDecoy;

    printf("Unit_bTryOpportunityFire @ 0x47c38\n");

    /* Past the 15-cell limit nothing is offered. */
    Reset();
    pShooter = Place(0, 20, 20, 0, 0, 0);
    pShooter->bFacing = 2;
    ArmShooter(pShooter);
    pTarget = Place(1, 20, 38, 0, 1, 0);
    pTarget->auStat[3] = 0;
    apEnemy[0] = pTarget;
    r.nEnemy = 1;
    RunOpp(pTarget, pShooter, &r);
    Check("an 18-cell target is not offered", g_TactCtx.nOppFireCount == 0);

    /* Facing 2's cone EXCLUDES straight ahead, so a target directly along +X is
     * never offered. A diagonal target at bearing pi/4 is inside the cone and
     * inside the fan, so it is spotted and the shooter is enqueued. */
    Reset();
    pShooter = Place(0, 20, 20, 0, 0, 0);
    pShooter->bFacing = 2;
    ArmShooter(pShooter);
    pTarget = Place(1, 22, 22, 0, 1, 0);
    pTarget->auStat[3] = 0;
    apEnemy[0] = pTarget;
    r.nEnemy = 1;
    RunOpp(pTarget, pShooter, &r);
    Check("the shooter is enqueued", g_TactCtx.nOppFireCount == 1);
    Check("the enqueued unit is the shooter", g_TactCtx.apOppFire[0] == pShooter);
    Check("the chosen weapon is the left hand", g_pSelItem == &g_handL);
    Check("Map.bFireStatus0[1] is cleared", g_bFireStatus1 == 0);
    Check("the scratch target slot is cleared", g_pScratch == NULL);

    /* Facing 4 (+Y) rejects a target straight along +X: the angle error truncates
     * to -1, which is more than one facing step. */
    Reset();
    pShooter = Place(0, 20, 20, 0, 0, 0);
    pShooter->bFacing = 4;
    ArmShooter(pShooter);
    pTarget = Place(1, 22, 20, 0, 1, 0);
    pTarget->auStat[3] = 0;
    apEnemy[0] = pTarget;
    r.nEnemy = 1;
    RunOpp(pTarget, pShooter, &r);
    Check("a target two facing steps off is not offered", g_TactCtx.nOppFireCount == 0);

    /* The quirk. The target passes every gate but is not in the shooter's
     * multi-target list: it sits outside the fan and is not on the enemy roster,
     * while a decoy on that roster is spotted and makes the list non-empty. The
     * original still reports success, leaves the scratch slot pointing at the
     * shooter, does not clear bFireStatus0[1], and enqueues nothing. */
    Reset();
    pShooter = Place(0, 20, 20, 0, 0, 0);
    pShooter->bFacing = 2;
    ArmShooter(pShooter);
    pTarget = Place(1, 20, 22, 0, 1, 0);      /* bearing pi/2, outside the fan */
    pTarget->auStat[3] = 0;
    pDecoy = Place(2, 22, 20, 0, 1, 0);       /* on the enemy roster, inside it */
    apEnemy[0] = pDecoy;
    r.nEnemy = 1;
    RunOpp(pTarget, pShooter, &r);
    Check("the decoy is spotted, so the list is not empty", pShooter->nMultiTarget == 1);
    Check("nothing is enqueued", g_TactCtx.nOppFireCount == 0);
    Check("bFireStatus0[1] is left alone", g_bFireStatus1 == 7);
    Check("the scratch slot still names the shooter", g_pScratch == pShooter);
}

/* --- Tact_KillUnit ---------------------------------------------------------- */

static MapGeom          g_KillGeom = { 4, 4 };
static Mc1Rec           g_aKillFloorSpr[1];
static ObjectRecFull  g_aKillObj[132];
static ObjectRecFull *g_apKillObj[132];
static TactItem       g_aKillItem[8];
static TactItem      *g_apKillItem[8];
static i32            g_nKillItem;
static i32            g_nKillCount;
static i32            g_nBattleEndCalls;
static Soldier       *g_pSelUnit;
static Soldier       *g_apKillXcom[4];
static Soldier       *g_apKillEnemy[4];
static Soldier       *g_apKillCiv[8];
static UnitRosters      g_KillRostersStore;
static CombatCtx      g_KillCombat;
static KillCtx        g_KillCtx;
static KillHooks      g_KillHooks;

static void HookBattleEnd(void *pUser)
{
    (void)pUser;
    ++g_nBattleEndCalls;
}

static void KillSetup(void)
{
    Reset();
    memset(g_aKillObj, 0, sizeof(g_aKillObj));
    memset(g_aKillItem, 0, sizeof(g_aKillItem));
    memset(g_apKillItem, 0, sizeof(g_apKillItem));
    for (i32 n = 0; n < 132; ++n) {
        g_apKillObj[n] = &g_aKillObj[n];
    }
    for (i32 n = 0; n < 8; ++n) {
        g_apKillItem[n] = &g_aKillItem[n];
    }
    g_nKillItem = 0;
    g_nKillCount = 0;
    g_nBattleEndCalls = 0;

    /* Every cell has a floor sprite, so a corpse lands where the unit stood. */
    for (i32 l = 0; l < 4; ++l) {
        for (i32 y = 0; y < 40; ++y) {
            for (i32 x = 0; x < 40; ++x) {
                g_aCells[l][y][x].pFloorSpr = &g_aKillFloorSpr[0];
            }
        }
    }

    g_KillCombat.pfnLookup = CellLookup;
    g_KillCombat.pUser = NULL;
    g_KillCombat.pGeom = &g_KillGeom;
    g_KillCombat.pState = NULL;
    g_KillCombat.pSelUnit = NULL;
    g_KillCombat.pCtxWeapon = NULL;
    g_KillCombat.nShotOriginX = 0;
    g_KillCombat.nShotOriginY = 0;

    memset(&g_KillCtx, 0, sizeof(g_KillCtx));
    g_KillCtx.pCombat = &g_KillCombat;
    g_KillCtx.apTactItems = g_apKillItem;
    g_KillCtx.pnTactItem = &g_nKillItem;
    g_KillCtx.apObjects = g_apKillObj;
    g_KillCtx.ppSelUnit = &g_pSelUnit;
    g_KillCtx.pnKillCount = &g_nKillCount;
    g_KillCtx.pszTransformName = "Chryssalid";
    g_KillCtx.pStatus = NULL;
    g_KillCtx.pStatusHooks = NULL;
    g_KillCtx.pVisCtx = NULL;

    memset(&g_KillHooks, 0, sizeof(g_KillHooks));
    g_KillHooks.pfnCheckBattleEnd = HookBattleEnd;
}

static void KillRosters(void)
{
    g_KillCtx.pRosters = &g_KillRostersStore;
    g_KillCtx.pRosters->apXcom = g_apKillXcom;
    g_KillCtx.pRosters->nXcom = 0;
    g_KillCtx.pRosters->apEnemy = g_apKillEnemy;
    g_KillCtx.pRosters->nEnemy = 0;
    g_KillCtx.pRosters->apCiv = g_apKillCiv;
    g_KillCtx.pRosters->nCiv = 0;
}

static void TestKillUnit(void)
{
    Soldier *pUnit;

    printf("Tact_KillUnit @ 0x4e430\n");

    /* The already-dead guard runs before anything else. */
    KillSetup();
    KillRosters();
    pUnit = Place(0, 5, 5, 0, 0, 0);
    pUnit->bFlags = 0x20;
    Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
    Check("an already dead unit is left alone", g_nKillItem == 0 && pUnit->bStatus == 0);
    CheckEqI32("no battle-end call either", g_nBattleEndCalls, 0);

    /* A plain human-sized kill. */
    KillSetup();
    KillRosters();
    pUnit = Place(0, 5, 5, 0, 0, 0);
    pUnit->uHp = 30;
    pUnit->auStat[1] = 40;
    pUnit->nUnk436 = 7;
    g_pSelUnit = pUnit;
    g_apKillXcom[0] = Place(1, 8, 8, 0, 0, 0);
    /* The morale shock divides by 3 * auStat[2] with no guard in the original, so
     * every rostered unit needs a non-zero dex or the kill faults. */
    g_apKillXcom[0]->auStat[2] = 10;
    g_KillCtx.pRosters->nXcom = 1;
    Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);

    Check("the death flag is set", (pUnit->bFlags & 0x20) != 0);
    CheckEqI32("status becomes 4", pUnit->bStatus, 4);
    Check("the cell occupant is cleared", At(5, 5, 0)->pUnit == NULL);
    CheckEqI32("the corpse is the one new item", g_nKillItem, 1);
    Check("the corpse object is the type-0 entry",
          g_apKillItem[0]->pObj == &g_aKillObj[70]);
    Check("the corpse links back to the unit", g_apKillItem[0]->pLinkedSoldier == pUnit);
    Check("the corpse is on the unit's cell",
          g_aCells[0][5][5].pItemList == &g_aKillItem[0]);
    Check("the corpse joins the civilian roster",
          g_KillCtx.pRosters->nCiv == 1 && g_KillCtx.pRosters->apCiv[0] == pUnit);
    Check("the selection moves off the dead unit",
          g_pSelUnit == g_KillCtx.pRosters->apXcom[0]);

    /* Fall damage: the landed cell's own nZ equals the layer index, so the term is
     * -1, the product clamps to zero, and HP is untouched. */
    CheckEqI32("no fall damage on a flat map", pUnit->uHp, 30);
    Check("the battle-end seam is called", g_nBattleEndCalls == 1);

    /* The mission kill counter only moves for a non-human type that is not 16 or 17
     * and reaches zero HP. */
    KillSetup();
    KillRosters();
    pUnit = Place(0, 5, 5, 0, 0, 9);
    pUnit->uHp = 0;
    Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
    CheckEqI32("type 9 at zero HP counts", g_nKillCount, 1);

    KillSetup();
    KillRosters();
    pUnit = Place(0, 5, 5, 0, 0, 16);
    pUnit->uHp = 0;
    Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
    CheckEqI32("type 16 does not count", g_nKillCount, 0);

    /* Morale shock, and its sign flips with the size of the dead unit. Enemy and
     * Xcom dex are both 10, so the divisor is 30 and 1000 / 30 is 33. */
    KillSetup();
    KillRosters();
    pUnit = Place(0, 5, 5, 0, 0, 0);
    pUnit->uHp = 10;
    {
        Soldier *pEnemy = Place(1, 9, 9, 0, 0, 5);
        Soldier *pAlly  = Place(2, 10, 10, 0, 0, 0);
        pEnemy->auStat[2] = 10;
        pEnemy->bMorale = 50;
        pAlly->auStat[2] = 10;
        pAlly->bMorale = 50;
        g_apKillEnemy[0] = pEnemy;
        g_apKillXcom[0] = pAlly;
        g_KillCtx.pRosters->nEnemy = 1;
        g_KillCtx.pRosters->nXcom = 1;
        Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
        CheckEqI32("a human death lifts enemy morale by the +34 term",
                   pEnemy->bMorale, 51);
        CheckEqI32("a human death drops Xcom morale", pAlly->bMorale, 17);
    }

    KillSetup();
    KillRosters();
    pUnit = Place(0, 5, 5, 0, 0, 10);
    pUnit->uHp = 0;
    {
        Soldier *pEnemy = Place(1, 9, 9, 0, 0, 5);
        Soldier *pAlly  = Place(2, 10, 10, 0, 0, 0);
        pEnemy->auStat[2] = 10;
        pEnemy->bMorale = 50;
        pAlly->auStat[2] = 10;
        pAlly->bMorale = 50;
        g_apKillEnemy[0] = pEnemy;
        g_apKillXcom[0] = pAlly;
        g_KillCtx.pRosters->nEnemy = 1;
        g_KillCtx.pRosters->nXcom = 1;
        Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
        CheckEqI32("an alien death hurts enemy morale", pEnemy->bMorale, 34);
        CheckEqI32("an alien death encourages Xcom", pAlly->bMorale, 51);
    }

    /* A large unit leaves four corpses and clears all four cells. */
    KillSetup();
    KillRosters();
    pUnit = Place(0, 12, 12, 0, 0, 4);
    pUnit->uHp = 20;
    At(13, 12, 0)->pUnit = pUnit;
    At(12, 13, 0)->pUnit = pUnit;
    At(13, 13, 0)->pUnit = pUnit;
    Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
    CheckEqI32("a large unit leaves four corpses", g_nKillItem, 4);
    Check("all four footprint cells are cleared",
          At(13, 12, 0)->pUnit == NULL && At(12, 13, 0)->pUnit == NULL
          && At(13, 13, 0)->pUnit == NULL);
    Check("the quadrant corpses use the quadrant table",
          g_apKillItem[1]->pObj == &g_aKillObj[125]
          && g_apKillItem[2]->pObj == &g_aKillObj[126]
          && g_apKillItem[3]->pObj == &g_aKillObj[127]);

    /* The chryssalid transform rewrites the unit instead of killing it. */
    KillSetup();
    KillRosters();
    pUnit = Place(0, 5, 5, 0, 0, 22);
    pUnit->uHp = 40;
    Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
    CheckEqI32("the transform turns the unit into type 14", pUnit->nUnitType, 14);
    CheckEqI32("status becomes 1", pUnit->bStatus, 1);
    Check("the transform name is copied in", pUnit->szName[0] == 'C');
    Check("the death flag is NOT set", (pUnit->bFlags & 0x20) == 0);
    Check("no corpse is left", g_nKillItem == 0);
    Check("the unit is not moved to the civilian roster",
          g_KillCtx.pRosters->nCiv == 0);
    CheckEqI32("stats come from the template", pUnit->auStat[0], 80);
    CheckEqI32("TU comes from the same template word", pUnit->uTu, 80);
    CheckEqI32("the ready selector block is loaded", pUnit->abReady252[2], 20);

    /* The spotted-flag sweep, armed by the byte at Map+0x20 (unk_113A20). */
    KillSetup();
    KillRosters();
    pUnit = Place(3, 20, 20, 0, 0, 0);
    pUnit->bFlags = 0;   /* slot 3 is unused by the earlier blocks */
    pUnit->uHp = 10;
    for (i32 l = 0; l < 4; ++l) {
        At(20, 20, l)->bFlags44 = 1;
        At(28, 20, l)->bFlags44 = 1;
        At(29, 20, l)->bFlags44 = 1;
        At(20, 20, l)->bFlags29 = 0x40;
    }
    g_KillCtx.bUiSlotArmed = true;
    Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
    /* Bounds proven from the ARM at 0x4F24C..0x4F2B8: the window is
     * (nX-8 .. nX+8) x (nY-8 .. nY+8), each end clamped with MOVMI to 0 and
     * SUBGE to 10 * nMapDim - 1, over four layers (R6 = 0, +0x3C, while < 0xF0),
     * and the clear is LDRB [cell+0x2C] / AND #0xFE / STRB [cell+0x2C], which is
     * MapCell.bFlags44 bit 0. The grid base is 0x15338 again. */
    CheckEqI32("the sweep clears the flag at the unit's cell", At(20, 20, 0)->bFlags44, 0);
    CheckEqI32("the sweep reaches the far edge of the window", At(28, 20, 0)->bFlags44, 0);
    CheckEqI32("the sweep stops one cell past the window", At(29, 20, 0)->bFlags44, 1);
    Check("the sweep runs on all four layers",
          At(20, 20, 1)->bFlags44 == 0 && At(20, 20, 2)->bFlags44 == 0
          && At(20, 20, 3)->bFlags44 == 0);
    Check("only bit 0 of the byte is cleared", At(20, 20, 0)->bFlags29 == 0x40);

    /* The gate: with Map+0x20 clear the whole sweep and the Xcom refresh are
     * skipped (BEQ loc_4F2C4 straight to the epilogue). */
    KillSetup();
    KillRosters();
    pUnit = Place(3, 20, 20, 0, 0, 0);
    pUnit->uHp = 10;
    for (i32 l = 0; l < 4; ++l) {
        At(20, 20, l)->bFlags44 = 1;
    }
    g_KillCtx.bUiSlotArmed = false;
    Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
    CheckEqI32("no sweep when the gate byte is clear", At(20, 20, 0)->bFlags44, 1);

    /* The clamp: a unit eight cells from the map edge has its window cut to the
     * grid, 10 * nMapDimX - 1 = 39 here. */
    KillSetup();
    KillRosters();
    pUnit = Place(3, 35, 35, 0, 0, 0);
    pUnit->uHp = 10;
    At(39, 35, 0)->bFlags44 = 1;
    At(30, 35, 0)->bFlags44 = 1;
    At(26, 35, 0)->bFlags44 = 1;
    g_KillCtx.bUiSlotArmed = true;
    Tact_KillUnit(&g_KillCtx, &g_KillHooks, pUnit);
    CheckEqI32("the clamped window still reaches the map edge", At(39, 35, 0)->bFlags44, 0);
    CheckEqI32("the clamped low end is nX - 8", At(30, 35, 0)->bFlags44, 0);
    CheckEqI32("one past the clamped low end is untouched", At(26, 35, 0)->bFlags44, 1);
}

int main(void)
{
    memset(g_aRays, 0, sizeof(g_aRays));
    memset(g_aSteps, 0, sizeof(g_aSteps));
    Tact_BuildVisRayTable(g_aRays, g_aSteps);

    TestTryOpportunityFire();
    TestKillUnit();

    printf("Unit_UpdateVisibility @ 0x48580\n");

    /* A non-operative unit only runs the roster sweep. */
    {
        UnitRosters r = NoRosters();
        Soldier *pDead;

        Reset();
        pDead = Place(0, 20, 20, 0, 3, 0);
        Run(pDead, &r, 0);
        Check("a dead unit is not flagged in its own cell", !Flag29(At(20, 20, 0), 0x10u));
        Check("a dead unit queues nothing", pDead->nMultiTarget == 0);
    }

    /* An operative unit flags its own cell. */
    {
        UnitRosters r = NoRosters();
        Soldier *pU;

        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        Run(pU, &r, 0);
        Check("own cell gets bFlags29 bit 4", Flag29(At(20, 20, 0), 0x10u));
        Check("the traj-front flag is cleared on entry", g_nFlag1036 == 0);
        Check("a small unit leaves its neighbours alone", !Flag29(At(21, 20, 0), 0x10u)
              || Flag44(At(21, 20, 0)));
    }

    /* A large unit takes the whole 2x2 footprint. */
    {
        UnitRosters r = NoRosters();
        Soldier *pU;

        Reset();
        pU = Place(0, 20, 20, 0, 0, 4);   /* type 4 is a large type */
        Run(pU, &r, 0);
        Check("large unit flags (x+1, y)", Flag29(At(21, 20, 0), 0x10u)
              && Flag44(At(21, 20, 0)));
        Check("large unit flags (x, y+1)", Flag29(At(20, 21, 0), 0x10u)
              && Flag44(At(20, 21, 0)));
        Check("large unit flags (x+1, y+1)", Flag29(At(21, 21, 0), 0x10u)
              && Flag44(At(21, 21, 0)));
        /* The fan starts on the unit's own cell, and its column reveal sets
         * bFlags44 there too. Only the large-unit block is skipped for a small
         * unit. */
        Check("the unit's own cell is flagged by the fan as well",
              Flag29(At(20, 20, 0), 0x10u) && Flag44(At(20, 20, 0)));
    }

    /* Column above: revealed while the floor sprite is absent, or its
     * bBlocksSight byte is 1. */
    {
        UnitRosters r = NoRosters();
        Soldier *pU;

        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        At(20, 20, 1)->pFloorSpr = &g_aSpr[0];
        g_aSpr[0].bBlocksSight = 0;
        Run(pU, &r, 0);
        Check("a floor sprite with bBlocksSight 0 hides the cell above",
              !Flag29(At(20, 20, 1), 0x10u));
        Check("the cell two above is still revealed", Flag29(At(20, 20, 2), 0x10u));

        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        At(20, 20, 1)->pFloorSpr = &g_aSpr[0];
        g_aSpr[0].bBlocksSight = 1;
        Run(pU, &r, 0);
        Check("bBlocksSight 1 lets the cell above through", Flag29(At(20, 20, 1), 0x10u));
    }

    /* Column below: revealed when the cell one layer up has no occupant. */
    {
        UnitRosters r = NoRosters();
        Soldier *pU;
        Soldier *pAbove;

        Reset();
        pU = Place(0, 20, 20, 2, 0, 0);
        pAbove = Place(1, 20, 20, 1, 0, 0);
        pAbove->multiOrAnim.anim.bUnk12 = 0;
        /* A floor sprite in the unit's own cell stops the fan's own downward
         * reveal, so only the pre-fan column scan can reach layer 1. */
        At(20, 20, 2)->pFloorSpr = &g_aSpr[1];
        Run(pU, &r, 0);
        Check("an occupant with byte 0x2E clear hides the cell below",
              !Flag29(At(20, 20, 1), 0x10u));

        Reset();
        pU = Place(0, 20, 20, 2, 0, 0);
        pAbove = Place(1, 20, 20, 1, 0, 0);
        /* The byte that decides is the occupant of the cell ABOVE the one being
         * revealed, which here is the unit itself. */
        pU->multiOrAnim.anim.bUnk12 = 1;
        At(20, 20, 2)->pFloorSpr = &g_aSpr[1];
        Run(pU, &r, 0);
        Check("byte 0x2E == 1 lets the cell below through", Flag29(At(20, 20, 1), 0x10u));

        Reset();
        pU = Place(0, 20, 20, 2, 0, 0);
        pU->multiOrAnim.anim.bUnk12 = 1;
        At(20, 20, 2)->pFloorSpr = &g_aSpr[1];
        Run(pU, &r, 0);
        Check("an empty column is revealed down to layer 0",
              Flag29(At(20, 20, 1), 0x10u) && Flag29(At(20, 20, 0), 0x10u));
    }

    /* The ray fan follows the unit's facing. Facing 2 is +X. */
    {
        UnitRosters r = NoRosters();
        Soldier *pU;

        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        pU->bFacing = 2;
        Run(pU, &r, 0);
        Check("the fan reaches three cells along +X", Flag29(At(23, 20, 0), 0x10u));
        Check("the fan does not reach behind the unit", !Flag29(At(17, 20, 0), 0x10u));

        /* A wall A whose 0x2b byte is set, met exactly on a cell boundary, stops
         * the fan. */
        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        pU->bFacing = 2;
        At(21, 20, 0)->pWallSprA = &g_aSpr[0];
        g_aSpr[0].bUnk2B = 1;
        g_aSpr[0].bBlocksMove = 0;
        Run(pU, &r, 0);
        Check("wall A with byte 0x2b set stops the fan", !Flag29(At(23, 20, 0), 0x10u));

        /* The same wall with the cell opened (bFlags29 bit 2) no longer blocks. */
        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        pU->bFacing = 2;
        At(21, 20, 0)->pWallSprA = &g_aSpr[0];
        g_aSpr[0].bUnk2B = 1;
        g_aSpr[0].bBlocksMove = 1;   /* 0x2a non-zero: the flag can open it */
        At(21, 20, 0)->bFlags29 |= 4u;
        Run(pU, &r, 0);
        Check("bFlags29 bit 2 opens the wall to the fan", Flag29(At(23, 20, 0), 0x10u));
    }

    /* Enemy multi-target scan: facing 2 accepts only bearings between
     * 0.785375 and 5.497625, so straight ahead is rejected. */
    {
        UnitRosters r = NoRosters();
        Soldier *pU;
        Soldier *pE;

        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        pU->bFacing = 2;
        pE = Place(1, 22, 20, 0, 1, 0);
        apEnemy[0] = pE;
        r.nEnemy = 1;
        /* The fan reaches this cell and would spot it on its own, so every cell is
         * made expensive: nFlags32 & 7 == 7 costs 14 per visit and the 240.0 budget
         * is gone after 17 steps, one cell out. Only the multi-target scan can
         * reach two cells away. */
        for (i32 l = 0; l < 4; ++l) {
            for (i32 y = 0; y < 40; ++y) {
                for (i32 x = 0; x < 40; ++x) {
                    g_aCells[l][y][x].nFlags32 = 7;
                }
            }
        }
        Run(pU, &r, 0);
        Check("facing 2 rejects a bearing of exactly 0", pU->nMultiTarget == 0);

        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        pU->bFacing = 2;
        pE = Place(1, 20, 22, 0, 1, 0);   /* bearing pi/2, inside the cone */
        apEnemy[0] = pE;
        r.nEnemy = 1;
        Run(pU, &r, 0);
        Check("facing 2 accepts a bearing of pi/2", pU->nMultiTarget == 1);
        Check("the spotted enemy cell gets bFlags29 bit 4", Flag29(pE->pCell, 0x10u));
        Check("and bFlags44 bit 0", Flag44(pE->pCell));

        /* Facing 4 has the ordinary rule: one facing step either side. */
        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        pU->bFacing = 4;                       /* facing angle 1.57075 */
        pE = Place(1, 20, 22, 0, 1, 0);       /* bearing 1.5708 */
        apEnemy[0] = pE;
        r.nEnemy = 1;
        Run(pU, &r, 0);
        Check("facing 4 accepts a bearing one step off", pU->nMultiTarget == 1);

        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        pU->bFacing = 4;
        pE = Place(1, 22, 20, 0, 1, 0);       /* bearing 0, error trunc(-1.57) = -1 */
        apEnemy[0] = pE;
        r.nEnemy = 1;
        Run(pU, &r, 0);
        Check("facing 4 rejects a bearing two steps off", pU->nMultiTarget == 0);

        /* Past fifteen cells the enemy is not considered at all. */
        Reset();
        pU = Place(0, 20, 20, 0, 0, 0);
        pU->bFacing = 2;
        pE = Place(1, 20, 38, 0, 1, 0);       /* 18 cells away */
        apEnemy[0] = pE;
        r.nEnemy = 1;
        Run(pU, &r, 0);
        Check("an enemy 18 cells away is never traced", pU->nMultiTarget == 0);
    }

    printf("\n%s: %d check(s), %d failure(s)\n",
           g_nFails == 0 ? "PASS" : "FAIL", g_nChecks, g_nFails);
    return g_nFails == 0 ? 0 : 1;
}
