/* test_combat.c - parity checks for shot resolution.
 * Evidence: Combat_SplashDamageCells @ 0x2e7d8, Unit_ApplyCellDamage @ 0x2e5d0. */
#include "game/pufo_combat.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_tactctl.h"   /* FocusCtx, TactCursor for the proximity tick */
#include "game/pufo_vistry.h"   /* VisRay, PUFO_VIS_RAY_COUNT, Map_ScanBlastFootprint */
#include "util/pufo_rand.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pufo_test.h"

static inline void Check(const char *pWhat, int bOk) { TestCheck(pWhat, bOk); }
static inline void CheckEqI32(const char *pWhat, i32 nGot, i32 nWant)
{
    TestEqI32(pWhat, nGot, nWant);
}

static MapCell g_aCells[PUFO_MAP_LAYERS][60][60];
static MapGeom g_geom = { 6, 6 };
static TactState g_state;
static Soldier g_unit;
static TactItem g_item;
static ObjectRecFull g_shot;
static ObjectRecFull g_ctx;
static Mc1Rec g_spr;
static u32 g_aHits[PUFO_SPLASH_DWORDS];
static CombatCtx g_c;

/* The impact cell is the centre of the window, so row = col = 20. */
#define IMPACT_X 30
#define IMPACT_Y 30
#define IMPACT_LAYER 0

static MapCell *CellLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nY < 0 || nLayer < 0 || nX >= 60 || nY >= 60) {
        return NULL;
    }
    return &g_aCells[nLayer][nY][nX];
}

static MapCell *Impact(void)
{
    return &g_aCells[IMPACT_LAYER][IMPACT_Y][IMPACT_X];
}

static void Reset(void)
{
    memset(g_aCells, 0, sizeof(g_aCells));
    memset(g_aHits, 0, sizeof(g_aHits));
    memset(&g_state, 0, sizeof(g_state));
    memset(&g_unit, 0, sizeof(g_unit));
    memset(&g_item, 0, sizeof(g_item));
    memset(&g_shot, 0, sizeof(g_shot));
    memset(&g_ctx, 0, sizeof(g_ctx));
    memset(&g_spr, 0, sizeof(g_spr));
    g_pTactState = &g_state;

    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; nLayer++) {
        for (i32 y = 0; y < 60; y++) {
            for (i32 x = 0; x < 60; x++) {
                MapCell_Init(&g_aCells[nLayer][y][x], x, y, nLayer);
            }
        }
    }
    g_c.pfnLookup = CellLookup;
    g_c.pUser = NULL;
    g_c.pGeom = &g_geom;
    g_c.pState = &g_state;
    g_c.pSelUnit = NULL;
    g_c.pCtxWeapon = &g_ctx;

    /* A plain weapon class: neither 50 nor 15. */
    g_ctx.uSortKey = 7;
    g_shot.uSortKey = 7;
    srand(1);
}

static void SetHit(i32 nSlot, u32 nValue)
{
    g_aHits[Combat_HitIndex(20, 20, nSlot)] = nValue;
}

static void TestIndex(void)
{
    printf("Combat_HitIndex\n");
    CheckEqI32("row 0 col 0 slot 0", (i32)Combat_HitIndex(0, 0, 0), 0);
    CheckEqI32("row 0 col 0 slot 3", (i32)Combat_HitIndex(0, 0, 3), 3);
    CheckEqI32("row 0 col 1 slot 0", (i32)Combat_HitIndex(0, 1, 0), 4);
    CheckEqI32("row 1 col 0 slot 0", (i32)Combat_HitIndex(1, 0, 0), 4 * 41);
    CheckEqI32("last record", (i32)Combat_HitIndex(40, 40, 3),
               PUFO_SPLASH_DWORDS - 1);
}

static void TestEmptyWindow(void)
{
    MapCell *pCell = Impact();

    printf("Combat_SplashDamageCells with an empty window\n");
    Reset();
    pCell->pFloorSpr = &g_spr;
    g_spr.bStrength = 100;
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("no counter written", pCell->abFaceDamage[0], 0);
    CheckEqI32("no flags written", (i32)(pCell->nFlags32 & 7u), 0);
}

static void TestSpriteDamage(void)
{
    MapCell *pCell = Impact();

    printf("sprite damage accumulates in the per-slot counters\n");
    Reset();
    pCell->pFloorSpr = &g_spr;
    g_spr.bStrength = 100;
    SetHit(0, 200);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    /* d = 200 - 1.5 * 100 = 50, and 100 >= 0 + 50, so the tile only records it. */
    CheckEqI32("first hit counter", pCell->abFaceDamage[0], 50);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("second hit counter", pCell->abFaceDamage[0], 100);
    Check("sprite survives while the counter is under the hardness",
          pCell->pFloorSpr == &g_spr);

    printf("a third hit goes past the hardness\n");
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    /* Layer 0 with no extra variant: the original keeps the sprite standing. */
    Check("layer 0 keeps the sprite when it has no extra variant",
          pCell->pFloorSpr == &g_spr);

    printf("the same break on layer 1 removes the sprite\n");
    Reset();
    g_aCells[1][IMPACT_Y][IMPACT_X].pFloorSpr = &g_spr;
    g_spr.bStrength = 100;
    SetHit(0, 1000);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, 1, &g_shot, g_aHits,
                             SPLASH_MODE_NORMAL);
    Check("layer 1 clears the broken sprite",
          g_aCells[1][IMPACT_Y][IMPACT_X].pFloorSpr == NULL);

    printf("strength 255 is unbreakable\n");
    Reset();
    pCell->pFloorSpr = &g_spr;
    g_spr.bStrength = 255;
    SetHit(0, 4000);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("no counter on an unbreakable sprite", pCell->abFaceDamage[0], 0);
    Check("unbreakable sprite stays", pCell->pFloorSpr == &g_spr);
}

static void TestClass15(void)
{
    MapCell *pCell = Impact();

    printf("weapon class 15 collapses the tile upward\n");
    Reset();
    g_ctx.uSortKey = PUFO_WEAPON_SORT_15;
    pCell->pFloorSpr = &g_spr;
    SetHit(3, 0x0B);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("floor cell flags", (i32)(pCell->nFlags32 & 7u), 3);
    CheckEqI32("floor cell second flags", (i32)(pCell->nFlags40 & 7u), 3);
    CheckEqI32("one layer up",
               (i32)(g_aCells[1][IMPACT_Y][IMPACT_X].nFlags32 & 7u), 3);
    CheckEqI32("two layers up",
               (i32)(g_aCells[2][IMPACT_Y][IMPACT_X].nFlags32 & 7u), 3);
    CheckEqI32("three layers up",
               (i32)(g_aCells[3][IMPACT_Y][IMPACT_X].nFlags32 & 7u), 3);

    printf("the collapse stops where the lookup returns nothing\n");
    Reset();
    g_ctx.uSortKey = PUFO_WEAPON_SORT_15;
    pCell->pFloorSpr = &g_spr;
    g_aCells[2][IMPACT_Y][IMPACT_X].pFloorSpr = NULL; /* marker for the test grid */
    SetHit(3, 0x0B);
    g_c.pfnLookup = CellLookup;
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("layers above still flagged while the grid is dense",
               (i32)(g_aCells[3][IMPACT_Y][IMPACT_X].nFlags32 & 7u), 3);

    printf("unit power 0x0A is not enough\n");
    Reset();
    g_ctx.uSortKey = PUFO_WEAPON_SORT_15;
    pCell->pFloorSpr = &g_spr;
    SetHit(3, 0x0A);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("threshold is strict", (i32)(pCell->nFlags32 & 7u), 0);
}

static void TestClass50(void)
{
    MapCell *pCell = Impact();

    printf("weapon class 50 drains TU instead of breaking the tile\n");
    Reset();
    g_ctx.uSortKey = PUFO_WEAPON_SORT_50;
    g_shot.uFirePower = 20;
    pCell->pUnit = &g_unit;
    g_unit.uHp = 100;
    SetHit(3, 0x42);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("TU spent", g_unit.uTuSpent, 20);
    CheckEqI32("no HP lost", g_unit.uHp, 100);

    printf("spent TU past HP is replaced by the random unconscious value\n");
    Reset();
    g_ctx.uSortKey = PUFO_WEAPON_SORT_50;
    g_shot.uFirePower = 200;
    pCell->pUnit = &g_unit;
    g_unit.uHp = 100;
    SetHit(3, 0x42);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    /* 10 * (rand(6) + 7) + uHp, so 170..229. */
    Check("replacement is in the 170..229 band",
          g_unit.uTuSpent >= 170 && g_unit.uTuSpent <= 229);

    printf("unit power 0x41 is not enough\n");
    Reset();
    g_ctx.uSortKey = PUFO_WEAPON_SORT_50;
    g_shot.uFirePower = 20;
    pCell->pUnit = &g_unit;
    g_unit.uHp = 100;
    SetHit(3, 0x41);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("threshold is strict", g_unit.uTuSpent, 0);
}

static void TestOccupantDamage(void)
{
    MapCell *pCell = Impact();

    printf("ceiling-slot power hurts the occupant\n");
    Reset();
    pCell->pUnit = &g_unit;
    g_unit.uHp = 50;
    g_unit.nArmorPoints = 1000;
    g_unit.auStat[1] = 40;
    SetHit(3, 100);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    Check("armour absorbs the whole hit", g_unit.uHp == 50);
    Check("armour took the damage",
          g_unit.nArmorPoints >= 900 && g_unit.nArmorPoints < 1000);
    CheckEqI32("unit marked as hit this shot", g_unit.gap_433, 1);

    printf("armour overflow carries into HP\n");
    Reset();
    pCell->pUnit = &g_unit;
    g_unit.uHp = 50;
    g_unit.nArmorPoints = 1;
    SetHit(3, 10);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("armour clamped to zero", g_unit.nArmorPoints, 0);
    /* damage is 10 - rand(2), so 8..10; the armour leaves 7..9 to spill. */
    Check("the spill came off HP", g_unit.uHp >= 41 && g_unit.uHp <= 43);
    CheckEqI32("spill recorded", g_unit.nUnk436, (i32)(50 - g_unit.uHp));

    printf("the selected unit is exempt in normal mode\n");
    Reset();
    g_c.pSelUnit = &g_unit;
    pCell->pUnit = &g_unit;
    g_unit.uHp = 50;
    SetHit(3, 100);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("selected unit untouched", g_unit.uHp, 50);
    CheckEqI32("and not marked", g_unit.gap_433, 0);

    printf("a unit already hit in this shot is skipped\n");
    Reset();
    pCell->pUnit = &g_unit;
    g_unit.uHp = 50;
    g_unit.gap_433 = 1;
    SetHit(3, 100);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("no second hit", g_unit.uHp, 50);

    printf("a dead occupant (bStatus >= 3) is not a target\n");
    Reset();
    pCell->pUnit = &g_unit;
    g_unit.uHp = 50;
    g_unit.bStatus = 3;
    SetHit(3, 100);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("not targeted", g_unit.uHp, 50);
}

static void TestCellDamage(void)
{
    MapCell *pCell = Impact();

    printf("Unit_ApplyCellDamage through the floor slot\n");
    Reset();
    pCell->pItemList = &g_item;
    g_item.pLinkedSoldier = &g_unit;
    g_unit.uHp = 50;
    g_unit.nArmorPoints = 1000;
    SetHit(0, 100);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("item unit was hit", g_unit.gap_433, 1);

    printf("the tile power gate is strict\n");
    Reset();
    pCell->pItemList = &g_item;
    g_item.pLinkedSoldier = &g_unit;
    g_unit.uHp = 50;
    SetHit(0, PUFO_POWER_MIN);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    CheckEqI32("power 0x14 does no unit damage", g_unit.gap_433, 0);

    printf("dead items are dropped only past power 40\n");
    Reset();
    pCell->pItemList = &g_item;
    g_item.pLinkedSoldier = NULL;
    SetHit(0, 41);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    Check("item removed", pCell->pItemList == NULL);

    Reset();
    pCell->pItemList = &g_item;
    g_item.pLinkedSoldier = NULL;
    SetHit(0, 40);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_NORMAL);
    Check("power 40 keeps the item", pCell->pItemList == &g_item);
}

static void TestThrowMode(void)
{
    MapCell *pCell = Impact();

    printf("throw mode rewrites the tile flags past the sprite threshold\n");
    Reset();
    pCell->pFloorSpr = &g_spr;
    g_spr.bStrength = 100;
    g_spr.bSplashResist = 30;
    SetHit(0, 30);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_THROW);
    CheckEqI32("nFlags32 low bits", (i32)(pCell->nFlags32 & 7u), 4);
    CheckEqI32("nFlags40 low bits", (i32)(pCell->nFlags40 & 7u), 2);
    CheckEqI32("bFlags36 bit 0 cleared", pCell->bFlags36 & 1, 0);

    printf("below the threshold nothing is set\n");
    Reset();
    pCell->pFloorSpr = &g_spr;
    g_spr.bStrength = 100;
    g_spr.bSplashResist = 31;
    SetHit(0, 30);
    Combat_SplashDamageCells(&g_c, IMPACT_X, IMPACT_Y, IMPACT_LAYER, &g_shot,
                             g_aHits, SPLASH_MODE_THROW);
    CheckEqI32("no flag rewrite", (i32)(pCell->nFlags32 & 7u), 0);
}

static ObjectRecFull g_wL, g_wR;
static TactItem g_handL, g_handR;
static ActionCostTable g_costs;
static TactItem *g_pSel;
static i32 g_nMode;

static bool GateAllowAll(i32 nSortKey, void *pUser)
{
    (void)nSortKey;
    (void)pUser;
    return true;
}

static bool GateDeny(i32 nSortKey, void *pUser)
{
    (void)nSortKey;
    (void)pUser;
    return false;
}

static void TestSelectFireMode(void)
{
    FireGate allow = { GateAllowAll, NULL };
    FireGate deny  = { GateDeny, NULL };

    printf("Unit_SelectBestFireMode\n");
    Reset();
    memset(&g_wL, 0, sizeof(g_wL));
    memset(&g_wR, 0, sizeof(g_wR));
    memset(&g_handL, 0, sizeof(g_handL));
    memset(&g_handR, 0, sizeof(g_handR));
    memset(&g_costs, 0, sizeof(g_costs));
    g_unit.pHandL = &g_handL;
    g_unit.pHandR = NULL;
    g_handL.pObj = &g_wL;
    g_handL.nAmmo = 10;
    g_wL.abArmorTu[0] = 20;
    g_wL.abCostPct[0] = 50;
    g_wL.uSortKey = 3;
    g_unit.auStat[0] = 100;
    g_unit.auStat[4] = 80;
    g_unit.uTu = 50;

    Check("an affordable left hand selects",
          Unit_SelectBestFireMode(&g_unit, &allow, &g_pSel, &g_nMode, &g_costs));
    CheckEqI32("mode 0 chosen (Unit_SelectBestFireMode @ 0x478e4)", g_nMode, 0);
    CheckEqI32("TU cost for mode 0", g_costs.nTuCost[0], 20);
    CheckEqI32("accuracy is pct * auStat[4] / 100", g_costs.nAccuracy[0], 40);
    Check("the chosen item is the left hand", g_pSel == &g_handL);

    printf("both modes affordable: mode 0 wins\n");
    g_wL.abArmorTu[1] = 10;
    Reset();
    g_unit.pHandL = &g_handL;
    g_unit.pHandR = NULL;
    g_unit.auStat[0] = 100;
    g_unit.uTu = 50;
    Unit_SelectBestFireMode(&g_unit, &allow, &g_pSel, &g_nMode, &g_costs);
    CheckEqI32("mode 0 overwrites mode 1", g_nMode, 0);

    printf("only the expensive mode fits the budget\n");
    Reset();
    g_wL.abArmorTu[0] = 90;
    g_wL.abArmorTu[1] = 10;
    g_unit.pHandL = &g_handL;
    g_unit.pHandR = NULL;
    g_unit.auStat[0] = 100;
    g_unit.uTu = 50;
    Check("mode 1 is used when mode 0 does not fit",
          Unit_SelectBestFireMode(&g_unit, &allow, &g_pSel, &g_nMode, &g_costs));
    CheckEqI32("mode 1", g_nMode, 1);
    CheckEqI32("TU cost for mode 1", g_costs.nTuCost[1], 10);
    g_wL.abArmorTu[0] = 20;

    printf("no candidate at all\n");
    Reset();
    g_unit.pHandL = NULL;
    g_unit.pHandR = NULL;
    Check("empty hands select nothing",
          !Unit_SelectBestFireMode(&g_unit, &allow, &g_pSel, &g_nMode, &g_costs));

    printf("out of ammo, two-handed, and research-denied\n");
    Reset();
    g_unit.pHandL = &g_handL;
    g_unit.pHandR = NULL;
    g_handL.nAmmo = 2;
    g_wL.bAmmoCap = 0;
    Check("two rounds left is not a candidate",
          !Unit_SelectBestFireMode(&g_unit, &allow, &g_pSel, &g_nMode, &g_costs));
    g_wL.bAmmoCap = 255;
    Check("an unlimited weapon is a candidate at any ammo count",
          Unit_SelectBestFireMode(&g_unit, &allow, &g_pSel, &g_nMode, &g_costs));
    g_wL.bAmmoCap = 0;
    g_handL.nAmmo = 10;
    g_wL.bTwoHanded = 1;
    Check("a two-handed weapon is not a candidate",
          !Unit_SelectBestFireMode(&g_unit, &allow, &g_pSel, &g_nMode, &g_costs));
    g_wL.bTwoHanded = 0;
    Check("research denial blocks the shot",
          !Unit_SelectBestFireMode(&g_unit, &deny, &g_pSel, &g_nMode, &g_costs));

    printf("the right hand wins when both are valid\n");
    Reset();
    g_handR.pObj = &g_wR;
    g_handR.nAmmo = 10;
    g_wR.abArmorTu[0] = 30;
    g_wR.uSortKey = 4;
    g_unit.pHandL = &g_handL;
    g_unit.pHandR = &g_handR;
    g_unit.auStat[0] = 100;
    g_unit.uTu = 50;
    Unit_SelectBestFireMode(&g_unit, &allow, &g_pSel, &g_nMode, &g_costs);
    Check("right hand selected", g_pSel == &g_handR);
    CheckEqI32("right hand cost written", g_costs.nTuCost[0], 30);
}


/* --- Combat_ResolveShot @ 0x2f12c -------------------------------------------- */

static Soldier g_aRoster[1];
static Soldier *g_apRoster[1];
static u32 g_aSave[PUFO_SPLASH_DWORDS];
static ShotOutcome g_out;
static i32 g_nFov, g_nArea;
static Mc1Rec g_ceil, g_floor;
static TactItem g_dropItem;
static ObjectRecFull g_dropObj;

/* Part 106: the hook carries the eight Map_ComputeFov arguments the three call
 * sites pass (settled from 0x2f324/0x2f3a4/0x2f6e8). The last call's values are
 * kept for the per-site checks; the recorder leaves the window array untouched so
 * the existing window-content assertions keep measuring the impact record alone. */
static u32 *g_pFovWindow;
static i32 g_nFovColBase, g_nFovRowBase, g_nFovOrigX, g_nFovOrigY;
static i32 g_nFovLayer, g_nFovRange, g_nFovCostMode;
static u16 g_nFovSortKey;

static void HookFov(void *pUser, u32 *paWindow, i32 nColBase, i32 nRowBase,
                    i32 nOriginX, i32 nOriginY, i32 nLayer, i32 nRange,
                    i32 nCostMode, u16 nSortKey)
{
    (void)pUser;
    ++g_nFov;
    g_pFovWindow   = paWindow;
    g_nFovColBase  = nColBase;
    g_nFovRowBase  = nRowBase;
    g_nFovOrigX    = nOriginX;
    g_nFovOrigY    = nOriginY;
    g_nFovLayer    = nLayer;
    g_nFovRange    = nRange;
    g_nFovCostMode = nCostMode;
    g_nFovSortKey  = nSortKey;
}
static void HookArea(void *pUser) { (void)pUser;  ++g_nArea; }

static ShotHooks g_hooks = { HookFov, HookArea, NULL };
static UnitRosters g_rost;

static void ResolveSetup(void)
{
    Reset();
    g_nFov = 0;
    g_nArea = 0;
    memset(&g_ceil, 0, sizeof(g_ceil));
    memset(&g_floor, 0, sizeof(g_floor));
    memset(&g_out, 0, sizeof(g_out));
    memset(g_aSave, 0xAA, sizeof(g_aSave));
    memset(g_aRoster, 0, sizeof(g_aRoster));
    g_apRoster[0] = &g_aRoster[0];
    g_rost.apXcom = g_apRoster;
    g_rost.nXcom = 1;
    g_rost.apEnemy = NULL;
    g_rost.nEnemy = 0;
    g_rost.apCiv = NULL;
    g_rost.nCiv = 0;
    g_c.nShotOriginX = IMPACT_X;
    g_c.nShotOriginY = IMPACT_Y;
    /* A target that is not knocked down by the test hits unless the test says
     * otherwise, and never loses armour unless the test says so. */
    Impact()->pUnit = &g_unit;
    g_unit.uHp = 1000;
    g_unit.auStat[1] = 4000;
    g_unit.abReady252[4] = 100;
    g_shot.bFireType = 0;
    g_shot.bReadySel = 0;
    g_shot.uFirePower = 40;
}

static void TestResolveShot(void)
{
    printf("Combat_ResolveShot: direct unit hit\n");
    ResolveSetup();
    g_aRoster[0].gap_433 = 0x7F;
    g_unit.uHp = 100;
    g_shot.uFirePower = 40;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       4, &g_shot, g_aHits, NULL, &g_out);
    Check("the roster hit flag is cleared before the shot", g_aRoster[0].gap_433 == 0);
    Check("damage is fire power less a random cut of up to half",
          g_out.nDamage >= 20 && g_out.nDamage <= 40);
    CheckEqI32("health drops by exactly the damage applied", g_unit.uHp,
               (u16)(100 - g_out.nDamage));
    CheckEqI32("the damage-taken counter records the same amount", g_unit.nUnk436,
               g_out.nDamage);
    CheckEqI32("a direct hit runs no splash walk", g_out.eMode, SPLASH_MODE_NORMAL);
    /* 0x2f3a4 runs before the 0x2f404 slot test: every bFireType-0 shot refreshes
     * the field, direct hit included. */
    CheckEqI32("a direct hit still refreshes the field of view once (0x2f3a4)",
               g_out.nFovCalls, 1);
    Check("the refresh is centred on the window (a2 = a3 = 0) at the shot origin",
          g_nFovColBase == 0 && g_nFovRowBase == 0
              && g_nFovOrigX == IMPACT_X && g_nFovOrigY == IMPACT_Y
              && g_nFovLayer == IMPACT_LAYER);
    Check("its range budget is ObjectRec+0xB6 and its cost mode is a8 = 1",
          g_nFovRange == 40 && g_nFovCostMode == 1 && g_nFovSortKey == 7
              && g_pFovWindow == g_aHits);
    CheckEqI32("a direct hit reports no area fire", g_nArea, 0);
    Check("the weapon is published into the map fire context",
          g_c.pCtxWeapon == &g_shot);

    printf("armour soaks the hit first\n");
    ResolveSetup();
    g_unit.uHp = 100;
    g_unit.nArmorPoints = 50;
    g_shot.uFirePower = 2;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       4, &g_shot, g_aHits, NULL, &g_out);
    CheckEqI32("no health damage when armour covers it", g_out.nDamage, 0);
    CheckEqI32("health untouched", g_unit.uHp, 100);
    Check("armour absorbed the hit",
          g_unit.nArmorPoints == 48 || g_unit.nArmorPoints == 49);

    printf("armour overflow carries into health\n");
    ResolveSetup();
    g_unit.uHp = 100;
    g_unit.nArmorPoints = 1;
    g_shot.uFirePower = 40;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       4, &g_shot, g_aHits, NULL, &g_out);
    CheckEqI32("armour is emptied", g_unit.nArmorPoints, 0);
    Check("the remainder hits health",
          g_unit.uHp == (u16)(100 - g_out.nDamage) && g_out.nDamage >= 19);

    printf("bReadySel picks the readiness byte\n");
    ResolveSetup();
    g_unit.bUnk248 = 10;
    g_shot.uFirePower = 100;
    g_unit.abReady252[4] = 100;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       4, &g_shot, g_aHits, NULL, &g_out);
    Check("readiness 100 subtracts no armour", g_out.nDamage >= 50);
    ResolveSetup();
    g_unit.bUnk248 = 10;
    g_shot.uFirePower = 100;
    g_unit.abReady252[4] = 50;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X,
                       IMPACT_Y, IMPACT_LAYER, 4, &g_shot, g_aHits, NULL, &g_out);
    Check("readiness below 100 subtracts bUnk248 in full",
          g_out.nDamage >= 45 && g_out.nDamage <= 90);
    ResolveSetup();
    g_unit.bUnk248 = 10;
    g_shot.uFirePower = 100;
    g_shot.bReadySel = 2;
    g_unit.abReady252[4] = 0;
    g_unit.abReady252[1] = 100;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       4, &g_shot, g_aHits, NULL, &g_out);
    Check("bReadySel 2 reads abReady252[1]", g_out.nDamage >= 50);

    printf("a strong hit knocks the unit down\n");
    ResolveSetup();
    g_unit.auStat[1] = 20;   /* half is 10, well under the damage */
    g_shot.uFirePower = 100;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       4, &g_shot, g_aHits, NULL, &g_out);
    {
        i32 nWant = Unit_bIsLarge(&g_unit)
                        ? 0 : (g_out.nDamage - (g_unit.auStat[1] >> 1)) / 10 + 1;
        CheckEqI32("bKneelOrPrimed grows by (damage - half auStat[1]) / 10 + 1",
                   g_unit.bKneelOrPrimed, (u8)nWant);
    }

    printf("Combat_ResolveShot: splash path\n");
    ResolveSetup();
    Impact()->pUnit = NULL;
    g_floor.bStrength = 30;
    Impact()->pFloorSpr = &g_floor;
    g_shot.uFirePower = 60;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       0, &g_shot, g_aHits, NULL, &g_out);
    CheckEqI32("the impact record holds the fire power",
               (i32)g_aHits[Combat_HitIndex(20, 20, 0)], 60);
    CheckEqI32("the floor slot damage counter grows by power - 1.5 * strength",
               g_aCells[IMPACT_LAYER][IMPACT_Y][IMPACT_X].abFaceDamage[0], 15);
    CheckEqI32("mode stays normal", g_out.eMode, SPLASH_MODE_NORMAL);
    CheckEqI32("the normal shot refreshes the field of view once, before the sweep",
               g_out.nFovCalls, 1);
    CheckEqI32("no ceiling refresh without a broken ceiling", g_nFovCostMode, 1);
    CheckEqI32("no area fire reported", g_nArea, 0);
    CheckEqI32("the window is not copied when no copy is asked for",
               (i32)g_aSave[0], (i32)0xAAAAAAAAu);

    printf("ceiling break\n");
    ResolveSetup();
    Impact()->pUnit = NULL;
    g_ceil.bCeilBreakable = 1;
    g_ceil.bStrength = 5;
    Impact()->pCeilSpr = &g_ceil;
    Impact()->abFaceDamage[3] = 0;
    g_shot.uFirePower = 30;
    /* Slot 3 is the ceiling: the impact record lands in hit dword 3, which is
     * exactly the half the ceiling-break sweep reads. */
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       3, &g_shot, g_aHits, g_aSave, &g_out);
    CheckEqI32("a broken ceiling promotes the walk to throw mode", g_out.eMode,
               SPLASH_MODE_THROW);
    /* 0x2f3a4 (normal) + 0x2f6e8 (one per broken ceiling). The ceiling call is the
     * last one recorded: fold base (20 - column, 20 - row) centred on the broken
     * window cell (20,20 here), range budget (int)(breakable byte 1 * 2.5) = 2,
     * cost mode a8 = 2. */
    CheckEqI32("one normal refresh plus one per broken ceiling", g_out.nFovCalls, 2);
    Check("the ceiling refresh is centred on the broken window cell (0x2f674/0x2f6cc)",
          g_nFovColBase == 0 && g_nFovRowBase == 0
              && g_nFovOrigX == IMPACT_X && g_nFovOrigY == IMPACT_Y);
    CheckEqI32("the ceiling refresh budgets (int)(sprite+0x18 * 2.5)", g_nFovRange, 2);
    CheckEqI32("the ceiling refresh takes cost mode a8 = 2", g_nFovCostMode, 2);
    CheckEqI32("the impact point moves to the broken cell", g_out.nImpactX, IMPACT_X);
    CheckEqI32("the impact row moves too", g_out.nImpactY, IMPACT_Y);
    CheckEqI32("the record is marked in the high half of dword 3",
               (i32)(g_aSave[Combat_HitIndex(20, 20, 3)] >> 16), 1);
    CheckEqI32("area fire is reported", g_nArea, 1);
    CheckEqI32("the impact window is snapshotted before attenuation",
               (i32)(g_aSave[Combat_HitIndex(20, 20, 3)] & 0xFFFFu), 30);
    Check("the live window is attenuated for the layers above",
          (i32)(g_aHits[Combat_HitIndex(20, 20, 3)] & 0xFFFFu) < 30);

    printf("area fire\n");
    ResolveSetup();
    Impact()->pUnit = NULL;
    g_shot.bFireType = 1;
    g_c.nShotOriginX = 31;
    g_c.nShotOriginY = 32;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       0, &g_shot, g_aHits, NULL, &g_out);
    CheckEqI32("area fire keeps the weapon's own mode", g_out.eMode,
               SPLASH_MODE_SECONDARY);
    CheckEqI32("area fire centres on the shot origin", g_out.nImpactX, 31);
    CheckEqI32("area fire centres on the shot origin row", g_out.nImpactY, 32);
    CheckEqI32("area fire refreshes the field of view once", g_out.nFovCalls, 1);
    Check("the area refresh (0x2f324) uses the shot origin cells, budget +0xB6, a8 = 2",
          g_nFovOrigX == 31 && g_nFovOrigY == 32 && g_nFovRange == 40
              && g_nFovCostMode == 2 && g_nFovColBase == 0 && g_nFovRowBase == 0);
    CheckEqI32("area fire is reported", g_nArea, 1);
    CheckEqI32("area fire writes no impact record",
               (i32)g_aHits[Combat_HitIndex(20, 20, 0)], 0);

    printf("items fall to the floor layer\n");
    ResolveSetup();
    Impact()->pUnit = NULL;
    memset(&g_dropItem, 0, sizeof(g_dropItem));
    memset(&g_dropObj, 0, sizeof(g_dropObj));
    g_dropObj.bListPri = 1;
    g_dropItem.pObj = &g_dropObj;
    g_aCells[2][10][10].pItemList = &g_dropItem;
    g_aCells[1][10][10].pFloorSpr = &g_floor;
    g_shot.uFirePower = 1;
    Combat_ResolveShot(&g_c, &g_hooks, &g_rost, IMPACT_X, IMPACT_Y, IMPACT_LAYER,
                       0, &g_shot, g_aHits, NULL, &g_out);
    Check("the item left the spriteless cell",
          g_aCells[2][10][10].pItemList == NULL);
    Check("the item landed on the cell with a floor sprite",
          g_aCells[1][10][10].pItemList == &g_dropItem);

    printf("shot sound table\n");
    /* 0xAA040 + 148*2 = 0xAA168, which is where the second table starts. */
    CheckEqI32("off_AA040 has 148 entries", PUFO_SHOT_SOUND_COUNT, 148);
    CheckEqI32("sort key 0 plays sample 18", g_aShotSound[0], 18);
    CheckEqI32("sort key 15 plays sample 2", g_aShotSound[15], 2);
    CheckEqI32("sort key 17 plays sample 2", g_aShotSound[17], 2);

    printf("fire sound table (word_AA168)\n");
    CheckEqI32("sort key 0 plays sample 20", g_aFireSoundBySortKey[0], 20);
    CheckEqI32("sort key 7 plays sample 3", g_aFireSoundBySortKey[7], 3);
    CheckEqI32("sort key 14 plays sample 18", g_aFireSoundBySortKey[14], 18);
    CheckEqI32("sort key 22 plays sample 20", g_aFireSoundBySortKey[22], 20);
    CheckEqI32("sort key 24 plays sample 21", g_aFireSoundBySortKey[24], 21);
    /* The two tables are adjacent, so the last entry of the first one and the
     * first entry of the second are different values. */
    Check("the two sound tables are distinct",
          g_aShotSound[147] != g_aFireSoundBySortKey[0]);
}
/* --- Combat_DetonateNearbyItems @ 0x32fa8 -----------------------------------
 *
 * The box is [nX-4, nX+4) by [nY-4, nY+4), so the far edge is excluded. A device is
 * only a candidate when its object is category 3 with sort key 16, its own bUnk8 is set,
 * and its countdown has reached zero. This function TESTS the countdown; the tick
 * function at 0x4b724 is the one that runs it down. */

static TactItem      s_aDev[6];
static ObjectRecFull s_objBlast;   /* category 3, sort key 16 */
static ObjectRecFull s_objMark;    /* category 3, sort key 14 */
static ObjectRecFull s_objDet;     /* category 3, sort key 15 */
static ObjectRecFull s_objOther;   /* category 3, some other sort key */
static Soldier       s_occ;
static FocusCtx      s_focus;
static TactCursor    s_cursor;
static u8            s_bViewMoved;
static VisRay        s_aRays[PUFO_VIS_RAY_COUNT];
static u8            s_aSteps[PUFO_VIS_TABLE_BYTES];
static u8            s_bFlag;
static ProximityCtx  s_prox;

#define PROX_X 30
#define PROX_Y 30

static MapCell *ProxCell(i32 nOffX, i32 nOffY, i32 nLayer)
{
    return &g_aCells[nLayer][PROX_Y + nOffY][PROX_X + nOffX];
}

static void ProxFixture(void)
{
    /* Reuse the shot-resolution fixture: Combat_ResolveShot needs the rosters, the
     * weapon, and the map fire context already in place. */
    ResolveSetup();

    memset(s_aDev, 0, sizeof(s_aDev));
    memset(&s_objBlast, 0, sizeof(s_objBlast));
    memset(&s_objMark, 0, sizeof(s_objMark));
    memset(&s_objDet, 0, sizeof(s_objDet));
    memset(&s_objOther, 0, sizeof(s_objOther));
    memset(&s_occ, 0, sizeof(s_occ));
    memset(&s_focus, 0, sizeof(s_focus));
    memset(&s_cursor, 0, sizeof(s_cursor));
    s_bFlag = 0xAA;
    s_bViewMoved = 0xAA;

    for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (i32 y = 0; y < 60; ++y) {
            for (i32 x = 0; x < 60; ++x) {
                g_aCells[z][y][x].nX = (u8)x;
                g_aCells[z][y][x].nY = (u8)y;
                g_aCells[z][y][x].nZ = (u8)z;
            }
        }
    }

    s_objBlast.nCategory = 3;
    s_objBlast.uSortKey = 16;
    s_objMark.nCategory = 3;
    s_objMark.uSortKey = 14;
    s_objDet.nCategory = 3;
    s_objDet.uSortKey = 15;
    s_objOther.nCategory = 3;
    s_objOther.uSortKey = 20;

    s_focus.nMapTileW = 40;
    s_focus.nMapTileH = 40;
    s_focus.nDimX = 6;
    s_focus.nDimY = 6;

    for (i32 i = 0; i < 6; ++i) {
        s_aDev[i].pObj = &s_objBlast;
        s_aDev[i].bUnk8 = 1;
        s_aDev[i].bUnk7 = 0;   /* elapsed */
        s_aDev[i].pNext = NULL;
    }

    memset(s_aRays, 0, sizeof(s_aRays));
    memset(s_aSteps, 0, sizeof(s_aSteps));
    Tact_BuildVisRayTable(s_aRays, s_aSteps);

    memset(&s_prox, 0, sizeof(s_prox));
    s_prox.pCombat = &g_c;
    s_prox.pShotHooks = &g_hooks;
    s_prox.pRosters = &g_rost;
    s_prox.pGeom = &g_geom;
    s_prox.pfnLookupCell = CellLookup;
    s_prox.pLookupUser = NULL;
    s_prox.paSteps = s_aSteps;
    s_prox.pbBlastScanFlag = &s_bFlag;
    s_prox.pFocus = &s_focus;
    s_prox.pCursor = &s_cursor;
    s_prox.pbViewMoved = &s_bViewMoved;
}

/* Map_ScanBlastFootprint is the second gate on every candidate: it has to find an
 * occupant within its radius. One neighbour over is enough. */
static void ArmBlastAt(MapCell *pCell)
{
    MapCell *pOcc = &g_aCells[pCell->nZ][pCell->nY][pCell->nX + 1];

    pOcc->pUnit = &s_occ;
    s_occ.pCell = pOcc;
    s_occ.bStatus = 0;
}

static bool ListHas(MapCell *pCell, TactItem *pWant)
{
    for (TactItem *pItem = pCell->pItemList; pItem != NULL; pItem = pItem->pNext) {
        if (pItem == pWant) {
            return true;
        }
    }
    return false;
}

static void TestDetonateNearbyItems(void)
{
    printf("Combat_DetonateNearbyItems @ 0x32fa8\n");

    /* An elapsed blast device in the origin cell goes off and leaves the list. */
    ProxFixture();
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ArmBlastAt(ProxCell(0, 0, 0));
    g_c.nShotOriginX = -1;
    g_c.nShotOriginY = -1;

    Combat_DetonateNearbyItems(&s_prox, ProxCell(0, 0, 0));

    Check("the device left the cell list", !ListHas(ProxCell(0, 0, 0), &s_aDev[0]));
    CheckEqI32("origin x recorded", g_c.nShotOriginX, PROX_X);
    CheckEqI32("origin y recorded", g_c.nShotOriginY, PROX_Y);
    CheckEqI32("the blast scan flag is cleared", s_bFlag, 0);

    /* The countdown is only tested here, never decremented. */
    ProxFixture();
    s_aDev[0].bUnk7 = 3;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ArmBlastAt(ProxCell(0, 0, 0));
    Combat_DetonateNearbyItems(&s_prox, ProxCell(0, 0, 0));
    Check("an unelapsed device is not detonated", ListHas(ProxCell(0, 0, 0), &s_aDev[0]));
    CheckEqI32("and its countdown is left alone", s_aDev[0].bUnk7, 3);

    /* Only sort key 16 is a candidate, and bUnk8 must be set. */
    ProxFixture();
    s_aDev[0].pObj = &s_objMark;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ArmBlastAt(ProxCell(0, 0, 0));
    Combat_DetonateNearbyItems(&s_prox, ProxCell(0, 0, 0));
    Check("sort key 14 is not a blast device", ListHas(ProxCell(0, 0, 0), &s_aDev[0]));

    ProxFixture();
    s_aDev[0].bUnk8 = 0;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ArmBlastAt(ProxCell(0, 0, 0));
    Combat_DetonateNearbyItems(&s_prox, ProxCell(0, 0, 0));
    Check("an item without bUnk8 is not a proximity device", ListHas(ProxCell(0, 0, 0), &s_aDev[0]));

    /* The box is nine cells across and the far edge is excluded: three cells out is
     * inside, four cells out is not. */
    ProxFixture();
    ProxCell(3, 0, 0)->pItemList = &s_aDev[0];
    ProxCell(4, 0, 0)->pItemList = &s_aDev[1];
    ArmBlastAt(ProxCell(3, 0, 0));
    ArmBlastAt(ProxCell(4, 0, 0));
    Combat_DetonateNearbyItems(&s_prox, ProxCell(0, 0, 0));
    Check("three cells out is inside the box", !ListHas(ProxCell(3, 0, 0), &s_aDev[0]));
    Check("four cells out is outside it", ListHas(ProxCell(4, 0, 0), &s_aDev[1]));

    /* Every layer is swept, not just the source layer. */
    ProxFixture();
    ProxCell(0, 0, 2)->pItemList = &s_aDev[0];
    ArmBlastAt(ProxCell(0, 0, 2));
    Combat_DetonateNearbyItems(&s_prox, ProxCell(0, 0, 0));
    Check("layer 2 is swept as well", !ListHas(ProxCell(0, 0, 2), &s_aDev[0]));

    /* Two devices in one cell: the walk restarts at the head after each detonation, so
     * both go off and the list drains from the front. */
    ProxFixture();
    s_aDev[0].pNext = &s_aDev[1];
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ArmBlastAt(ProxCell(0, 0, 0));
    Combat_DetonateNearbyItems(&s_prox, ProxCell(0, 0, 0));
    Check("the first device went off", !ListHas(ProxCell(0, 0, 0), &s_aDev[0]));
    Check("and the walk restarted for the second", !ListHas(ProxCell(0, 0, 0), &s_aDev[1]));
}

static void TestTickProximityItems(void)
{
    printf("MapCell_TickProximityItems @ 0x4b724\n");

    /* The countdown runs whether or not the device goes off. */
    ProxFixture();
    s_aDev[0].bUnk7 = 3;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ArmBlastAt(ProxCell(0, 0, 0));
    g_c.nShotOriginX = -1;
    g_c.nShotOriginY = -1;
    MapCell_TickProximityItems(&s_prox, ProxCell(0, 0, 0));
    CheckEqI32("countdown drops by one", s_aDev[0].bUnk7, 2);
    Check("and nothing detonates", g_c.nShotOriginX == -1 && g_c.nShotOriginY == -1);

    /* Sort key 15 detonates and is removed. */
    ProxFixture();
    s_aDev[0].pObj = &s_objDet;
    s_aDev[0].bUnk7 = 1;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    g_c.nShotOriginX = -1;
    g_c.nShotOriginY = -1;
    MapCell_TickProximityItems(&s_prox, ProxCell(0, 0, 0));
    CheckEqI32("key 15 records the origin x", g_c.nShotOriginX, PROX_X);
    CheckEqI32("key 15 records the origin y", g_c.nShotOriginY, PROX_Y);
    Check("key 15 is removed from the cell", !ListHas(ProxCell(0, 0, 0), &s_aDev[0]));

    /* Sort key 14 is removed without detonating, and rewrites the animated-tile flags. */
    ProxFixture();
    s_aDev[0].pObj = &s_objMark;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ProxCell(0, 0, 0)->nFlags40 = 0;
    g_c.nShotOriginX = -1;
    MapCell_TickProximityItems(&s_prox, ProxCell(0, 0, 0));
    Check("key 14 is removed", !ListHas(ProxCell(0, 0, 0), &s_aDev[0]));
    Check("but it detonates nothing", g_c.nShotOriginX == -1);
    CheckEqI32("key 14 sets nFlags40 to (old & ~0x78) | 0x38", ProxCell(0, 0, 0)->nFlags40, 0x38);

    /* Sort key 16 with a blocked footprint stops the whole cell: the item behind it is
     * not even ticked. Ten cells away from the shot fixture there is no occupant inside
     * the footprint, so the scan finds nothing. */
    ProxFixture();
    s_aDev[0].bUnk7 = 1;
    s_aDev[1].bUnk7 = 3;
    s_aDev[0].pNext = &s_aDev[1];
    ProxCell(10, 10, 0)->pItemList = &s_aDev[0];
    g_c.nShotOriginX = -1;
    MapCell_TickProximityItems(&s_prox, ProxCell(10, 10, 0));
    Check("a blocked blast device detonates nothing", g_c.nShotOriginX == -1);
    CheckEqI32("and the next item in the cell is left unticked", s_aDev[1].bUnk7, 3);

    /* Sort key 16 with a clear footprint detonates but stays in the list. */
    ProxFixture();
    s_aDev[0].bUnk7 = 1;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ArmBlastAt(ProxCell(0, 0, 0));
    g_c.nShotOriginX = -1;
    MapCell_TickProximityItems(&s_prox, ProxCell(0, 0, 0));
    Check("a clear blast device detonates", g_c.nShotOriginX == PROX_X);
    Check("and stays in the cell", ListHas(ProxCell(0, 0, 0), &s_aDev[0]));

    /* Any other sort key detonates, is removed, and returns: the item behind it is not
     * ticked because the function has already left. */
    ProxFixture();
    s_aDev[0].pObj = &s_objOther;
    s_aDev[1].bUnk7 = 3;
    s_aDev[0].pNext = &s_aDev[1];
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    g_c.nShotOriginX = -1;
    MapCell_TickProximityItems(&s_prox, ProxCell(0, 0, 0));
    Check("another key detonates", g_c.nShotOriginX == PROX_X);
    Check("and is removed", !ListHas(ProxCell(0, 0, 0), &s_aDev[0]));
    CheckEqI32("and the walk stops there", s_aDev[1].bUnk7, 3);

    /* The view only moves when the cell's bFlags44 bit 0 is set. */
    ProxFixture();
    s_aDev[0].pObj = &s_objDet;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ProxCell(0, 0, 0)->bFlags44 = 0;
    MapCell_TickProximityItems(&s_prox, ProxCell(0, 0, 0));
    CheckEqI32("flag clear leaves the view alone", s_bViewMoved, 0);

    ProxFixture();
    s_aDev[0].pObj = &s_objDet;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    ProxCell(0, 0, 0)->bFlags44 = 1;
    MapCell_TickProximityItems(&s_prox, ProxCell(0, 0, 0));
    CheckEqI32("flag set reports the view moved", s_bViewMoved, 1);

    /* The compare is signed char, so a countdown already at zero wraps to -1, fires, and
     * leaves 255 behind to fire again next time. */
    ProxFixture();
    s_aDev[0].pObj = &s_objDet;
    s_aDev[0].bUnk7 = 0;
    ProxCell(0, 0, 0)->pItemList = &s_aDev[0];
    g_c.nShotOriginX = -1;
    MapCell_TickProximityItems(&s_prox, ProxCell(0, 0, 0));
    Check("an already-zero countdown still detonates", g_c.nShotOriginX == PROX_X);
}

int main(void)
{
    TestIndex();
    TestEmptyWindow();
    TestSpriteDamage();
    TestClass15();
    TestClass50();
    TestOccupantDamage();
    TestCellDamage();
    TestThrowMode();
    TestSelectFireMode();
    TestResolveShot();
    TestDetonateNearbyItems();
    TestTickProximityItems();

    return TestSummary("test_combat");
}
