/* test_unit.c - checks soldier entity predicates, facing and placement.
 *
 * The facing recipe uses deliberately truncated constants; the test pins the
 * sector boundaries so a later "cleanup" to exact pi values fails loudly.
 */
#include "asset/pufo_asset.h"
#include "game/pufo_boot.h"
#include "game/pufo_unit.h"
#include "game/pufo_combat.h"   /* UnitRosters */
#include "game/pufo_font.h"
#include "util/pufo_rand.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

static void TestLayout(void)
{
    printf("Soldier layout\n");
    /* Fields up to and including pCell are at the same offsets on every target. */
    Check(offsetof(Soldier, bFacing) == 0x05, "  bFacing at 0x05");
    Check(offsetof(Soldier, uTu) == 0x06, "  uTu at 0x06");
    Check(offsetof(Soldier, uStunOrArmor) == 0x08, "  uStunOrArmor at 0x08");
    Check(offsetof(Soldier, uHp) == 0x0a, "  uHp at 0x0a");
    Check(offsetof(Soldier, bMorale) == 0x0c, "  bMorale at 0x0c");
    Check(offsetof(Soldier, pCell) == 0x10, "  pCell at 0x10");

    /* bStatus tracks pointer width because it sits right after pCell. The macro
     * used by MapCell_HasOccupant must agree with the real field. */
    Check(offsetof(Soldier, bStatus) == PUFO_SOLDIER_BSTATUS_OFF,
          "  bStatus matches PUFO_SOLDIER_BSTATUS_OFF");

#if UINTPTR_MAX == 0xFFFFFFFFu
    /* ILP32 offsets, straight from the IDB. On Win64 the widened pCell shifts
     * everything below by 4, so these are asserted only for 32-bit. */
    Check(offsetof(Soldier, nAnimPose) == 0x14, "  nAnimPose at 0x14 (ILP32)");
    Check(offsetof(Soldier, nDrawOffX) == 0x15, "  nDrawOffX at 0x15 (ILP32)");
    Check(offsetof(Soldier, nDrawOffY) == 0x16, "  nDrawOffY at 0x16 (ILP32)");
    Check(offsetof(Soldier, bStatus) == 0x17, "  bStatus at 0x17 (ILP32)");
    Check(offsetof(Soldier, uTuSpent) == 0x18, "  uTuSpent at 0x18 (ILP32)");
    Check(sizeof(Soldier) == PUFO_SOLDIER_SIZE_ILP32, "  sizeof matches the original");
#else
    /* Win64: keep the same relative ordering, shifted by the pointer growth. */
    Check(offsetof(Soldier, nAnimPose) == 0x14 + 4, "  nAnimPose shifted by the pointer growth");
    Check(offsetof(Soldier, nDrawOffX) == 0x15 + 4, "  nDrawOffX shifted by the pointer growth");
    Check(offsetof(Soldier, nDrawOffY) == 0x16 + 4, "  nDrawOffY shifted by the pointer growth");
    Check(offsetof(Soldier, bStatus) == 0x17 + 4, "  bStatus shifted by the pointer growth");
    Check(offsetof(Soldier, uTuSpent) == 0x18 + 4, "  uTuSpent shifted by the pointer growth");
#endif
}

static void TestPredicates(void)
{
    Soldier unit;

    printf("Unit_bIsOperative / Unit_bIsLarge\n");
    Unit_ctor(&unit);
    unit.bStatus = UNIT_STATUS_OPERATIVE;
    Check(Unit_bIsOperative(&unit), "  status 0 is operative");
    unit.bStatus = UNIT_STATUS_1;
    Check(!Unit_bIsOperative(&unit), "  status 1 is not operative");

    /* byte_A7B80 decoded from the IDB: types 4..8,11,13 are large. */
    unit.nUnitType = 0;
    Check(!Unit_bIsLarge(&unit), "  type 0 is small");
    unit.nUnitType = 4;
    Check(Unit_bIsLarge(&unit), "  type 4 is large");
    unit.nUnitType = 8;
    Check(Unit_bIsLarge(&unit), "  type 8 is large");
    unit.nUnitType = 9;
    Check(!Unit_bIsLarge(&unit), "  type 9 is small");
    /* byte_A7B80 rows 11 and 13 are {1,0,1,0}: flag 0 set, is-large clear. */
    unit.nUnitType = 11;
    Check(!Unit_bIsLarge(&unit), "  type 11 is small");
    unit.nUnitType = 13;
    Check(!Unit_bIsLarge(&unit), "  type 13 is small");
    unit.nUnitType = 21;
    Check(Unit_bIsLarge(&unit), "  type 21 is large");
    /* Out of table range must be safe, not an overread. */
    unit.nUnitType = 200;
    Check(!Unit_bIsLarge(&unit), "  out-of-range type is small, not an overread");
}

static void TestResetBattleState(void)
{
    Soldier unit;
    u16     auStat[PUFO_UNIT_STAT_COUNT];
    i32     i;

    printf("Unit_ResetBattleState\n");
    Unit_ctor(&unit);
    for (i = 0; i < PUFO_UNIT_STAT_COUNT; ++i) {
        auStat[i] = (u16)(100 + i);
    }
    unit.bFlags = 0xFF;
    unit.bFacing = 9;
    Unit_ResetBattleState(&unit, 3, 0, auStat, PUFO_UNIT_STAT_COUNT);

    CheckEqI32(unit.bFacing, 3, "  bFacing from the argument");
    CheckEqI32(unit.bFlags, 0xC0, "  bFlags masked to top two bits");
    CheckEqI32(unit.nAnimPose, 1, "  anim pose set to 1");
    CheckEqI32(unit.nDrawOffX, 0, "  draw offset X cleared");
    CheckEqI32(unit.nDrawOffY, 0, "  draw offset Y cleared");
    Check(unit.pCell == NULL, "  cell cleared");
    CheckEqI32(unit.bMorale, 100, "  morale set to 100");
    CheckEqI32(unit.uTuSpent, 0, "  TU spent cleared");
    CheckEqI32(unit.nMultiTarget, 0, "  multi-target cleared");
    /* auStat[0] -> uTu, auStat[1] -> uHp, auStat[9] -> uStunOrArmor */
    CheckEqI32(unit.uTu, 100, "  uTu from auStat[0]");
    CheckEqI32(unit.uHp, 101, "  uHp from auStat[1]");
    CheckEqI32(unit.uStunOrArmor, 109, "  uStunOrArmor from auStat[9]");
    CheckEqI32(unit.nUnitType, 0, "  unit type stored");

    printf("Unit_ArmorForType\n");
    CheckEqI32(Unit_ArmorForType(0), 0, "  type 0 has no bonus armour");
    CheckEqI32(Unit_ArmorForType(2), 100, "  type 2 gets 100");
    CheckEqI32(Unit_ArmorForType(3), 150, "  type 3 gets 150");
    CheckEqI32(Unit_ArmorForType(9), 0, "  type 9 has no bonus armour");

    /* A short stat array must not be over-read. */
    printf("Unit_ResetBattleState with a short stat array\n");
    Unit_ctor(&unit);
    Unit_ResetBattleState(&unit, 1, 0, auStat, 1);
    CheckEqI32(unit.uTu, 100, "  uTu still read");
    CheckEqI32(unit.uHp, 0, "  uHp left zero rather than over-read");
    CheckEqI32(unit.uStunOrArmor, 0, "  stun left zero rather than over-read");
}

/* The sector boundaries are the thing to pin: the original uses 1.57075 and
 * 0.3926875, not the exact pi/2 and pi/8 values. */
static void TestFacing(void)
{
    printf("Unit_FacingToward\n");
    /* Same tile returns 0 without calling atan2. */
    CheckEqI32(Unit_FacingToward(10, 10, 10, 10), 0, "  same cell -> 0");

    /* Cardinal directions. The grid is screen-like: +x is right, +y is down.
     * From (0,0) the original's arithmetic yields 0..7 in this arrangement;
     * the important property is that all eight are distinct and in range. */
    {
        bool bSeen[8];
        i32  anDx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
        i32  anDy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        i32  i;

        memset(bSeen, 0, sizeof(bSeen));
        for (i = 0; i < 8; ++i) {
            u8 nF = Unit_FacingToward(0, 0, anDx[i] * 100, anDy[i] * 100);

            if (nF > 7) {
                printf("  FAIL facing out of range: %u\n", (unsigned)nF);
                g_nFailures += 1;
                continue;
            }
            bSeen[nF] = true;
        }
        {
            i32 nCount = 0;

            for (i = 0; i < 8; ++i) {
                if (bSeen[i]) {
                    nCount += 1;
                }
            }
            CheckEqI32(nCount, 8, "  eight directions map to eight distinct facings");
        }
    }

    /* Every direction on a fine sweep must stay in range. */
    {
        bool bAllInRange = true;
        i32  nAngle;

        for (nAngle = 0; nAngle < 3600; ++nAngle) {
            double dRad = (double)nAngle * 3.14159265358979 / 1800.0;
            i32    nX = (i32)(cos(dRad) * 1000.0);
            i32    nY = (i32)(sin(dRad) * 1000.0);
            u8     nF;

            if (nX == 0 && nY == 0) {
                continue;
            }
            nF = Unit_FacingToward(0, 0, nX, nY);
            if (nF > 7) {
                bAllInRange = false;
            }
        }
        Check(bAllInRange, "  full-circle sweep stays in 0..7");
    }

    /* The truncated constants matter: using exact pi/2 would shift a boundary.
     * Assert that the tuning constants are the truncated values, not pi. */
    Check(PUFO_FACING_SUB < 1.5707963 && PUFO_FACING_SUB > 1.5707,
          "  facing subtract is the truncated 1.57075");
    Check(PUFO_FACING_ADD < 0.3926991 && PUFO_FACING_ADD > 0.3926,
          "  facing add is the truncated 0.3926875");
}

/* --- placement, with an injected cell grid --- */

#define TEST_DIM 10

typedef struct TestGrid {
    MapCell aCells[PUFO_MAP_LAYERS][TEST_DIM][TEST_DIM];
    bool    bInitialised;
} TestGrid;

static MapCell *Test_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    TestGrid *pGrid = (TestGrid *)pUser;

    if (nX < 0 || nY < 0 || nX >= TEST_DIM || nY >= TEST_DIM) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return &pGrid->aCells[nLayer][nY][nX];
}

static void TestGrid_Init(TestGrid *pGrid)
{
    i32 x;
    i32 y;
    i32 z;

    memset(pGrid, 0, sizeof(*pGrid));
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < TEST_DIM; ++y) {
            for (x = 0; x < TEST_DIM; ++x) {
                MapCell_Init(&pGrid->aCells[z][y][x], x, y, z);
            }
        }
    }
    pGrid->bInitialised = true;
}

static void TestPlacement(void)
{
    TestGrid grid;
    Soldier  unit;

    printf("Unit_PlaceOnMap (small unit)\n");
    TestGrid_Init(&grid);
    Unit_ctor(&unit);
    unit.nUnitType = 0; /* small */

    Unit_PlaceOnMap(&unit, 3, 4, 0, Test_Lookup, &grid);
    Check(unit.pCell == &grid.aCells[0][4][3], "  unit claims the target cell");
    Check(grid.aCells[0][4][3].pUnit == &unit, "  cell records the occupant");

    /* Moving clears the previous cell. */
    Unit_PlaceOnMap(&unit, 5, 6, 0, Test_Lookup, &grid);
    Check(grid.aCells[0][4][3].pUnit == NULL, "  previous cell released");
    Check(grid.aCells[0][6][5].pUnit == &unit, "  new cell recorded");
    Check(unit.pCell == &grid.aCells[0][6][5], "  unit points at the new cell");

    printf("Unit_PlaceOnMap (large unit claims 2x2)\n");
    TestGrid_Init(&grid);
    Unit_ctor(&unit);
    unit.nUnitType = 4; /* large */

    Unit_PlaceOnMap(&unit, 2, 2, 0, Test_Lookup, &grid);
    Check(grid.aCells[0][2][2].pUnit == &unit, "  base cell");
    Check(grid.aCells[0][2][3].pUnit == &unit, "  +1 in X");
    Check(grid.aCells[0][3][2].pUnit == &unit, "  +1 in Y");
    Check(grid.aCells[0][3][3].pUnit == &unit, "  +1 in both");
    Check(grid.aCells[0][2][4].pUnit == NULL, "  cell at +2 in X untouched");
    Check(grid.aCells[0][4][2].pUnit == NULL, "  cell at +2 in Y untouched");

    /* Moving releases all four. */
    Unit_PlaceOnMap(&unit, 6, 6, 1, Test_Lookup, &grid);
    Check(grid.aCells[0][2][2].pUnit == NULL, "  old base released");
    Check(grid.aCells[0][2][3].pUnit == NULL, "  old +1 X released");
    Check(grid.aCells[0][3][2].pUnit == NULL, "  old +1 Y released");
    Check(grid.aCells[0][3][3].pUnit == NULL, "  old +1 XY released");
    Check(grid.aCells[1][6][6].pUnit == &unit, "  new layer base claimed");
    Check(grid.aCells[1][6][7].pUnit == &unit, "  new layer +1 X claimed");

    /* A large unit at the grid edge must not fault on the out-of-range
     * neighbours; the injected lookup returns NULL for those. */
    printf("Unit_PlaceOnMap at the grid edge\n");
    TestGrid_Init(&grid);
    Unit_ctor(&unit);
    unit.nUnitType = 4;
    Unit_PlaceOnMap(&unit, TEST_DIM - 1, TEST_DIM - 1, 0, Test_Lookup, &grid);
    Check(unit.pCell == &grid.aCells[0][TEST_DIM - 1][TEST_DIM - 1],
          "  edge placement succeeds without touching out-of-range cells");

    printf("Unit_PlaceOnMap with a failing lookup\n");
    Unit_ctor(&unit);
    unit.nUnitType = 0;
    Unit_PlaceOnMap(&unit, -5, -5, 0, Test_Lookup, &grid);
    Check(unit.pCell == NULL, "  rejected placement leaves the unit unplaced");
}

static void TestRemove(void)
{
    Soldier *apUnits[3];
    i32      nCount;
    i32      i;

    printf("Unit_RemoveAt\n");
    for (i = 0; i < 3; ++i) {
        apUnits[i] = (Soldier *)malloc(sizeof(Soldier));
        Unit_ctor(apUnits[i]);
        apUnits[i]->nAiTick = 100 + i;
    }
    nCount = 3;

    nCount = Unit_RemoveAt(apUnits, nCount, 1);
    CheckEqI32(nCount, 2, "  count decremented");
    CheckEqI32(apUnits[0]->nAiTick, 100, "  element 0 unchanged");
    CheckEqI32(apUnits[1]->nAiTick, 102, "  element 2 shifted into slot 1");

    /* Out-of-range must be a no-op, not the original's one-past-the-end free. */
    nCount = Unit_RemoveAt(apUnits, nCount, 99);
    CheckEqI32(nCount, 2, "  out-of-range index is a no-op");
    nCount = Unit_RemoveAt(apUnits, nCount, -1);
    CheckEqI32(nCount, 2, "  negative index is a no-op");
    /* Zero count must not underflow. */
    nCount = Unit_RemoveAt(apUnits, 0, 0);
    CheckEqI32(nCount, 0, "  zero count stays zero");

    for (i = 0; i < 2; ++i) {
        free(apUnits[i]);
    }
}


/* --- Soldier_Generate, using the real extracted name tables --- */

static void TestGenerateNames(void)
{
    Soldier unit;
    i32     i;

    printf("Unit_GenerateName\n");
    memset(&unit, 0, sizeof(unit));

    /* Male: first name from 0..69, last from 0..63. */
    unit.bFemale = 0;
    for (i = 0; i < 50; ++i) {
        Unit_GenerateName(&unit);
        /* Name must be non-empty and contain exactly one separator. */
        if (unit.szName[0] == '\0') {
            printf("  FAIL empty male name at iteration %d\n", (int)i);
            g_nFailures += 1;
            break;
        }
    }
    Check(unit.szName[0] != '\0', "  male name generated");
    Check(strchr(unit.szName, ' ') != NULL, "  male name has a first/last separator");

    /* Female: first name from 70..87. */
    unit.bFemale = 1;
    for (i = 0; i < 50; ++i) {
        Unit_GenerateName(&unit);
        if (unit.szName[0] == '\0') {
            printf("  FAIL empty female name at iteration %d\n", (int)i);
            g_nFailures += 1;
            break;
        }
    }
    Check(unit.szName[0] != '\0', "  female name generated");
    Check(strchr(unit.szName, ' ') != NULL, "  female name has a first/last separator");

    /* Names must fit the 40-byte field. */
    Check(strlen(unit.szName) < sizeof(unit.szName), "  name fits the field");
}

static void TestGenerate(void)
{
    Soldier unit;
    i32     i;

    printf("Unit_Generate\n");
    for (i = 0; i < 200; ++i) {
        memset(&unit, 0, sizeof(unit));
        Unit_Generate(&unit, 0);
        if (unit.szName[0] == '\0') {
            printf("  FAIL empty name at iteration %d\n", (int)i);
            g_nFailures += 1;
            break;
        }
    }
    Check(unit.szName[0] != '\0', "  name generated");

    /* The stat ranges are inclusive; assert each landed in its documented band. */
    Check(unit.auStat[0] >= 50 && unit.auStat[0] <= 59, "  auStat[0] in 50..59");
    Check(unit.auStat[9] >= 40 && unit.auStat[9] <= 69, "  auStat[9] in 40..69");
    Check(unit.auStat[1] >= 40 && unit.auStat[1] <= 59, "  auStat[1] in 40..59");
    Check(unit.auStat[2] >= 10 && unit.auStat[2] <= 59, "  auStat[2] in 10..59");
    Check(unit.auStat[3] >= 30 && unit.auStat[3] <= 59, "  auStat[3] in 30..59");
    Check(unit.auStat[4] >= 40 && unit.auStat[4] <= 59, "  auStat[4] in 40..59");
    Check(unit.auStat[5] >= 30 && unit.auStat[5] <= 49, "  auStat[5] in 30..49");
    Check(unit.auStat[8] >= 20 && unit.auStat[8] <= 79, "  auStat[8] in 20..79");
    Check(unit.auStat[6] >= 10 && unit.auStat[6] <= 29, "  auStat[6] in 10..29");
    Check(unit.auStat[7] >= 1 && unit.auStat[7] <= 100, "  auStat[7] in 1..100");

    /* Derived fields. */
    Check(unit.uHp == unit.auStat[1], "  uHp mirrors auStat[1]");
    Check(unit.bMorale == 0 || unit.bMorale == 0, "  morale untouched by Generate");
    Check(unit.nKind == 0, "  kind stored");
    Check(unit.bRankType == 0, "  rank reset");
    Check(unit.uKills == 0, "  kills reset");
    Check(unit.uMissions == 0, "  missions reset");
    Check((unit.bFlags & 0x20) == 0, "  flag 0x20 cleared");

    /* The base-stat copy is offset by one at the tail, which is intentional. */
    Check(unit.auStatBase[0] == unit.auStat[0], "  base[0] = stat[0]");
    Check(unit.auStatBase[6] == unit.auStat[6], "  base[6] = stat[6]");
    Check(unit.auStatBase[7] == unit.auStat[8], "  base[7] = stat[8] (offset by one)");
    Check(unit.auStatBase[8] == unit.auStat[9], "  base[8] = stat[9] (offset by one)");

    /* Humanoid flag is set and never cleared here (types 9+ keep it). */
    Check(unit.bHumanoidInv == 1, "  bHumanoidInv set");
    Check(unit.abReady252[0] == 1 && unit.abReady252[4] == 1, "  ready flags set");
    Check(unit.pLinked1040 == NULL, "  tail link clear");
    Check(unit.nUnitType == 0, "  unit type reset");

    /* Female is roughly one in three. */
    {
        i32 nFemale = 0;

        for (i = 0; i < 600; ++i) {
            memset(&unit, 0, sizeof(unit));
            Unit_Generate(&unit, 0);
            if (unit.bFemale != 0) {
                nFemale += 1;
            }
        }
        Check(nFemale > 100 && nFemale < 300,
              "  female ratio is about one in three");
    }

    /* The RNG draw count must be exactly reproducible: two runs from the same
     * seed must produce identical soldiers. This is the property that keeps
     * saved rosters compatible. */
    printf("Unit_Generate is deterministic from a fixed seed\n");
    {
        Soldier a;
        Soldier b;

        /* SeedRng uses the wall clock, so drive srand directly for a fixed seed. */
        srand(12345);
        memset(&a, 0, sizeof(a));
        Unit_Generate(&a, 0);
        srand(12345);
        memset(&b, 0, sizeof(b));
        Unit_Generate(&b, 0);

        Check(strcmp(a.szName, b.szName) == 0, "  same seed gives the same name");
        Check(a.bFemale == b.bFemale, "  same gender");
        Check(a.nLook == b.nLook, "  same look");
        Check(memcmp(a.auStat, b.auStat, sizeof(a.auStat)) == 0, "  same stats");
    }
}


/* --- Unit_ApplyControlRecovery @ 0x4a444 --- */

static void TestControlRecovery(void)
{
    Soldier unit;
    MapCell cell;

    printf("Unit_ApplyControlRecovery\n");

    /* The idempotence guard: a flagged unit is left completely alone. */
    memset(&unit, 0, sizeof(unit));
    unit.bControlRecovered = 1;
    unit.uHp = 50;
    unit.bMorale = 10;
    Check(!Unit_ApplyControlRecovery(&unit), "  already-recovered unit is refused");
    CheckEqI32(unit.uHp, 50, "  HP untouched when already recovered");
    CheckEqI32(unit.bMorale, 10, "  morale untouched when already recovered");

    /* Damage: bKneelOrPrimed is subtracted from HP. */
    memset(&unit, 0, sizeof(unit));
    unit.uHp = 50;
    unit.bKneelOrPrimed = 20;
    Check(Unit_ApplyControlRecovery(&unit), "  first recovery applies");
    CheckEqI32(unit.uHp, 30, "  HP reduced by bKneelOrPrimed");
    Check(unit.bControlRecovered == 1, "  marked recovered");

    /* Overkill clamps to 0 rather than wrapping. */
    memset(&unit, 0, sizeof(unit));
    unit.uHp = 10;
    unit.bKneelOrPrimed = 40;
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.uHp, 0, "  HP clamps to 0, no wrap");

    /* Exact equality also takes the -= branch. */
    memset(&unit, 0, sizeof(unit));
    unit.uHp = 15;
    unit.bKneelOrPrimed = 15;
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.uHp, 0, "  HP equal to the pending damage goes to 0");

    /* TU rules, SMALL unit. */
    printf("Unit_ApplyControlRecovery TU rules (small unit)\n");
    memset(&cell, 0, sizeof(cell));

    /* Cell flag field non-zero -> +3. */
    memset(&unit, 0, sizeof(unit));
    unit.nUnitType = 0;
    unit.uTuSpent = 5;
    unit.pCell = &cell;
    cell.nFlags32 = 1; /* low 3 bits non-zero */
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.uTuSpent, 5 + 3, "  flagged cell adds 3 TU");

    /* Field zero and uTuSpent >= 10 -> -10. */
    memset(&unit, 0, sizeof(unit));
    unit.nUnitType = 0;
    unit.uTuSpent = 25;
    unit.pCell = &cell;
    cell.nFlags32 = 0xFFFFFFF8u; /* low 3 bits zero */
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.uTuSpent, 15, "  unsigned field subtracts 10 TU");

    /* Field zero and uTuSpent < 10 -> 0. */
    memset(&unit, 0, sizeof(unit));
    unit.nUnitType = 0;
    unit.uTuSpent = 7;
    unit.pCell = &cell;
    cell.nFlags32 = 0;
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.uTuSpent, 0, "  small spent value snaps to 0");

    /* A NULL cell must not fault. */
    memset(&unit, 0, sizeof(unit));
    unit.nUnitType = 0;
    unit.uTuSpent = 25;
    unit.pCell = NULL;
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.uTuSpent, 15, "  NULL cell treated as field zero");

    /* LARGE units keep their TU entirely. */
    printf("Unit_ApplyControlRecovery skips TU for large units\n");
    memset(&unit, 0, sizeof(unit));
    unit.nUnitType = 4; /* large */
    unit.uTuSpent = 33;
    unit.pCell = &cell;
    cell.nFlags32 = 1;
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.uTuSpent, 33, "  large unit TU untouched");

    /* Morale: +5 normally, 100 at or above the threshold. */
    printf("Unit_ApplyControlRecovery morale\n");
    memset(&unit, 0, sizeof(unit));
    unit.bMorale = 50;
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.bMorale, 55, "  morale +5 below the threshold");

    memset(&unit, 0, sizeof(unit));
    unit.bMorale = 94; /* just below 95 */
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.bMorale, 99, "  94 is below the threshold, so +5");

    memset(&unit, 0, sizeof(unit));
    unit.bMorale = 95; /* the threshold exactly */
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.bMorale, 100, "  95 snaps to 100");

    memset(&unit, 0, sizeof(unit));
    unit.bMorale = 200; /* malformed input */
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.bMorale, 100, "  above the threshold snaps to 100");

    /* bUnk1037 is cleared. */
    memset(&unit, 0, sizeof(unit));
    unit.bUnk1037 = 0xFF;
    (void)Unit_ApplyControlRecovery(&unit);
    CheckEqI32(unit.bUnk1037, 0, "  bUnk1037 cleared");
}

static void TestExtraSpr(void)
{
    Soldier s;
    Mc1Rec rec;
    Mc1Rec *apIndex[3];

    Unit_ctor(&s);
    memset(&rec, 0, sizeof(rec));
    apIndex[0] = &rec;
    apIndex[1] = &rec;
    apIndex[2] = &rec;

    /* Zero facing byte -> NULL even before a base is set. */
    Check(Unit_GetExtraSpr(&s) == NULL, "extra spr: zero facing returns NULL");

    g_fontSys.anMc1Base[2] = 1;
    g_fontSys.apMc1Index = apIndex;
    g_fontSys.nMc1IndexCount = 3;

    s.multiOrAnim.anim.uExtraSprBase = 2;
    s.multiOrAnim.anim.uFacingResetH = 1; /* index = anMc1Base[2] + 1 = 2 */
    Check(Unit_GetExtraSpr(&s) == apIndex[2], "extra spr: base + facing index");

    /* Only the low byte of uFacingResetH is added; 0x0100 -> 0 -> NULL. */
    s.multiOrAnim.anim.uFacingResetH = 0x0100;
    Check(Unit_GetExtraSpr(&s) == NULL, "extra spr: only low facing byte counts");

    s.multiOrAnim.anim.uFacingResetH = 5; /* index 6 is out of range -> NULL */
    Check(Unit_GetExtraSpr(&s) == NULL, "extra spr: out-of-range index returns NULL");

    g_fontSys.anMc1Base[2] = 0;
    g_fontSys.apMc1Index = NULL;
    g_fontSys.nMc1IndexCount = 0;
}

static void TestAnimPose(void)
{
    Soldier s;
    AnimHandInfo handL;
    AnimHandInfo handR;

    Unit_ctor(&s);
    memset(&handL, 0, sizeof(handL));
    memset(&handR, 0, sizeof(handR));

    /* Dead (flag 0x20): pose 18, flags masked to 0xE0. */
    s.bFlags = 0x3F;
    Unit_UpdateAnimPose(&s, NULL, NULL);
    CheckEqI32(s.nAnimPose, 18, "dead pose forces 18");
    CheckEqI32(s.bFlags, 0x20, "dead masks bFlags to 0xE0");

    /* Aiming poses. */
    s.bFlags = 0x04;
    Unit_UpdateAnimPose(&s, NULL, NULL);
    CheckEqI32(s.nAnimPose, 16, "aiming default pose 16");
    s.bFlags = 0x05;
    Unit_UpdateAnimPose(&s, NULL, NULL);
    CheckEqI32(s.nAnimPose, 15, "aiming + kneel pose 15");
    s.bFlags = 0x14;
    Unit_UpdateAnimPose(&s, NULL, NULL);
    CheckEqI32(s.nAnimPose, 17, "aiming + climb pose 17");

    /* Base poses with no hands. */
    s.bFlags = 0x01;
    Unit_UpdateAnimPose(&s, NULL, NULL);
    CheckEqI32(s.nAnimPose, 0, "kneel base pose 0");
    s.bFlags = 0x02;
    Unit_UpdateAnimPose(&s, NULL, NULL);
    CheckEqI32(s.nAnimPose, 3, "walk base pose 3");
    s.bFlags = 0x10;
    Unit_UpdateAnimPose(&s, NULL, NULL);
    CheckEqI32(s.nAnimPose, 2, "climb base pose 2");
    s.bFlags = 0x12;
    Unit_UpdateAnimPose(&s, NULL, NULL);
    CheckEqI32(s.nAnimPose, 4, "climb + walk base pose 4");
    s.bFlags = 0x00;
    Unit_UpdateAnimPose(&s, NULL, NULL);
    CheckEqI32(s.nAnimPose, 1, "idle base pose 1");

    /* A weapon in either hand adds 10. */
    s.bFlags = 0x00;
    handL.bPresent = true;
    handL.uSortKey = 22;
    Unit_UpdateAnimPose(&s, &handL, NULL);
    CheckEqI32(s.nAnimPose, 11, "left weapon adds 10");
    handL.uSortKey = 0;
    handL.bListPri = 1;
    Unit_UpdateAnimPose(&s, &handL, NULL);
    CheckEqI32(s.nAnimPose, 11, "list priority 1 also counts as a weapon");

    memset(&handL, 0, sizeof(handL));
    handR.bPresent = true;
    handR.uSortKey = 24;
    Unit_UpdateAnimPose(&s, NULL, &handR);
    CheckEqI32(s.nAnimPose, 11, "right weapon adds 10");

    /* Any other held item adds 5. */
    handR.uSortKey = 7;
    handR.bListPri = 0;
    Unit_UpdateAnimPose(&s, NULL, &handR);
    CheckEqI32(s.nAnimPose, 6, "other item adds 5");

    /* A left-hand weapon wins over a right-hand non-weapon. */
    handL.bPresent = true;
    handL.bListPri = 1;
    Unit_UpdateAnimPose(&s, &handL, &handR);
    CheckEqI32(s.nAnimPose, 11, "left weapon checked first");
}

static MapCell *GridLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    MapCell (*pGrid)[30][40] = pUser;

    if (nX < 0 || nX >= 40 || nY < 0 || nY >= 30 || nLayer < 0 || nLayer >= 4) {
        return NULL;
    }
    return &pGrid[nLayer][nY][nX];
}

static void TestCanStepExitDir(void)
{
    static MapCell s_aGrid[4][30][40];
    MapCell origin;
    MapGeom geom;
    Mc1Rec wall;

    printf("Unit_CanStepExitDir\n");
    memset(s_aGrid, 0, sizeof(s_aGrid));
    memset(&wall, 0, sizeof(wall));
    memset(&origin, 0, sizeof(origin));
    geom.nDimX = 4;
    geom.nDimY = 3;
    origin.nX = 5;
    origin.nY = 6;
    origin.nZ = 1;

    /* nDir 0 steps (0,-1,0), so the probe base is (5,5,1). With no wall sprite
     * the direction is clear. */
    Check(Unit_CanStepExitDir(&origin, 0, &geom, GridLookup, s_aGrid),
          "clear when the probe cell has no wall");

    /* A wall-B sprite whose byte 0x2F is set blocks the step. bTransparent 0 and
     * bBlocksMove 0 make the blank/blocking test pass. */
    s_aGrid[1][5][5].pWallSprB = &wall;
    wall.gap_2f[0] = 1;
    Check(!Unit_CanStepExitDir(&origin, 0, &geom, GridLookup, s_aGrid),
          "blocked by a wall sprite byte 0x2F");

    /* The same sprite with a clear byte 0x2F does not block. */
    wall.gap_2f[0] = 0;
    Check(Unit_CanStepExitDir(&origin, 0, &geom, GridLookup, s_aGrid),
          "not blocked when byte 0x2F is clear");

    /* Out of bounds: (5,5,1) with nDir 0 from Y=0 goes to Y=-1. */
    origin.nY = 0;
    Check(!Unit_CanStepExitDir(&origin, 0, &geom, GridLookup, s_aGrid),
          "false when the probe base is out of bounds");

    /* An out-of-range direction is refused. */
    Check(!Unit_CanStepExitDir(&origin, 8, &geom, GridLookup, s_aGrid),
          "direction 8 is refused");
}

static void RecordStatus(i32 nWhich, void *pUser)
{
    (void)nWhich;
    ((i32 *)pUser)[0] += 1;
}

static void TestArmorTuWarn(void)
{
    Soldier unit;
    ArmorHandInfo handL;
    ArmorHandInfo handR;
    i32 nStatus[4];

    printf("Unit_bArmorTuWarn\n");
    memset(&unit, 0, sizeof(unit));
    memset(&handL, 0, sizeof(handL));
    memset(&handR, 0, sizeof(handR));
    unit.auStat[0] = 100;

    /* The byte_AC81A option disables the whole warning. */
    Check(!Unit_bArmorTuWarn(&unit, NULL, NULL, true, false, false, 0,
                             RecordStatus, nStatus),
          "option AC81A disables the warning");

    /* No hands -> no armour cost -> no warning. */
    Check(!Unit_bArmorTuWarn(&unit, NULL, NULL, false, false, false, 0,
                             RecordStatus, nStatus),
          "no hands means no warning");

    /* One hand with armour-TU 10 at auStat 100 -> cost 10. With uTu 15 spending 4
     * leaves 11 >= 10, so no warning. With uTu 12 spending 4 leaves 8 < 10. */
    handL.bPresent = true;
    handL.abArmorTu[0] = 10;
    memset(nStatus, 0, sizeof(nStatus));
    unit.uTu = 15;
    Check(!Unit_bArmorTuWarn(&unit, &handL, NULL, false, false, false, 4,
                             RecordStatus, nStatus),
          "no warning when TU stays above the armour cost");
    unit.uTu = 12;
    Check(Unit_bArmorTuWarn(&unit, &handL, NULL, false, false, false, 4,
                            RecordStatus, nStatus),
          "warning when the spend would drop below the armour cost");
    CheckEqI32(nStatus[0], 2, "two status messages pushed");

    /* Taking the larger hand: right hand cost 20 wins over left 10. */
    handR.bPresent = true;
    handR.abArmorTu[0] = 20;
    unit.uTu = 22;
    Check(Unit_bArmorTuWarn(&unit, &handL, &handR, false, false, false, 4,
                            RecordStatus, nStatus),
          "the larger hand's armour cost is used");

    /* A non-large unit ignores a two-handed weapon. */
    handR.bTwoHanded = 1;
    Check(!Unit_bArmorTuWarn(&unit, &handL, &handR, false, false, false, 4,
                             RecordStatus, nStatus),
          "two-handed weapon ignored by a non-large unit");
}

static void TestSpotCellOccupant(void)
{
    Soldier viewer;
    Soldier occ;
    MapCell cell;
    SpotCtx ctx;
    i32 nFlag1036 = 0;
    i32 nFocused = 0;

    printf("Unit_SpotCellOccupant\n");
    memset(&viewer, 0, sizeof(viewer));
    memset(&occ, 0, sizeof(occ));
    memset(&cell, 0, sizeof(cell));
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnTrajFront1036 = &nFlag1036;

    /* No occupant. */
    Check(!Unit_SpotCellOccupant(&viewer, &cell, &ctx), "empty cell returns false");

    /* Same-side occupant: reported but not queued. */
    cell.pUnit = &occ;
    viewer.bStatus = 0;
    occ.bStatus = 0;
    Check(Unit_SpotCellOccupant(&viewer, &cell, &ctx), "same-side occupant reported");
    CheckEqI32(viewer.nMultiTarget, 0, "same-side occupant not queued");

    /* Enemy occupant while busy: queued and spotted, flag set. */
    occ.bStatus = 1;
    ctx.bBusy = true;
    Check(Unit_SpotCellOccupant(&viewer, &cell, &ctx), "enemy occupant spotted");
    CheckEqI32(viewer.nMultiTarget, 1, "enemy added to the multi-target list");
    Check(viewer.multiOrAnim.apMultiTargets[0] == &occ, "the occupant pointer stored");
    CheckEqI32(viewer.nSpottedCount, 1, "occupant added to the spotted list");
    CheckEqI32(nFlag1036, 1, "traj-front flag set");

    /* Already in the multi-target list: reported without a second append. */
    Check(Unit_SpotCellOccupant(&viewer, &cell, &ctx), "re-spot reported");
    CheckEqI32(viewer.nMultiTarget, 1, "not appended twice");

    /* Idle world flags the occupant through bUnk148 instead. */
    {
        Soldier viewer2;
        Soldier occ2;
        MapCell cell2;
        SpotCtx ctx2;

        memset(&viewer2, 0, sizeof(viewer2));
        memset(&occ2, 0, sizeof(occ2));
        memset(&cell2, 0, sizeof(cell2));
        memset(&ctx2, 0, sizeof(ctx2));
        cell2.pUnit = &occ2;
        viewer2.bStatus = 0;
        occ2.bStatus = 1;
        Check(Unit_SpotCellOccupant(&viewer2, &cell2, &ctx2), "idle spot returns true");
        CheckEqI32(occ2.bUnk148, 1, "idle spot sets bUnk148");
        CheckEqI32(viewer2.nSpottedCount, 0, "idle spot does not touch the spotted list");
    }

    (void)nFocused;
}

static u8 g_bWeightL;
static u8 g_bTwoHandedL;
static u8 g_bAmmoCapL;

static u8 WeightL(TactItem *pItem, void *pUser) { (void)pItem; (void)pUser; return g_bWeightL; }
static u8 TwoHandedL(TactItem *pItem, void *pUser) { (void)pItem; (void)pUser; return g_bTwoHandedL; }
static u8 AmmoCapL(TactItem *pItem, void *pUser) { (void)pItem; (void)pUser; return g_bAmmoCapL; }
static u8 AmmoWeight(i32 nIndex, void *pUser) { (void)pUser; return (u8)nIndex; }

#define SCAN_GRID 40
static MapCell g_aScanCells[4][SCAN_GRID][SCAN_GRID];

static MapCell *ScanLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)nY; (void)pUser;
    if (nX < 0 || nLayer < 0 || nX >= SCAN_GRID || nLayer >= 4) {
        return NULL;
    }
    return &g_aScanCells[nLayer][10][nX];
}

static void PlaceScanUnit(Soldier *pU, i32 nX, u8 bStatus, u8 nType)
{
    MapCell *pCell = &g_aScanCells[0][10][nX];

    memset(pU, 0, sizeof(*pU));
    memset(pCell, 0, sizeof(*pCell));
    pCell->nX = (u8)nX;
    pCell->nY = 10;
    pCell->pUnit = pU;
    pU->pCell = pCell;
    pU->bStatus = bStatus;
    pU->nUnitType = nType;
}

static void TestScanVisibleUnits(void)
{
    Soldier viewer, enemy, ally, allySpecial, distant;
    Soldier *apXcom[1] = { &ally };
    Soldier *apEnemy[2] = { &enemy, &allySpecial };
    SpotCtx ctx;
    UnitRosters rosters;
    Soldier *pScratch = &enemy;
    Soldier *pSelOuter = &ally;
    i32 nFlag1036 = 7;

    printf("Unit_ScanVisibleUnits\n");
    memset(g_aScanCells, 0, sizeof(g_aScanCells));
    PlaceScanUnit(&viewer, 10, 0, 0);
    PlaceScanUnit(&enemy, 12, 1, 0);
    PlaceScanUnit(&ally, 11, 0, 0);
    PlaceScanUnit(&allySpecial, 13, 0, 16);
    PlaceScanUnit(&distant, 30, 1, 0);

    memset(&ctx, 0, sizeof(ctx));
    ctx.bBusy = true;
    ctx.pnTrajFront1036 = &nFlag1036;
    ctx.pSelUnit = pSelOuter;
    memset(&rosters, 0, sizeof(rosters));
    rosters.apXcom = apXcom;
    rosters.nXcom = 1;
    rosters.apEnemy = apEnemy;
    rosters.nEnemy = 2;

    CheckEqI32(Unit_ScanVisibleUnits(&viewer, NULL, ScanLookup, NULL, &ctx, &rosters,
                                     &pScratch), 0, "scan returns 0");
    /* The scan clears the flag on entry; Unit_SpotCellOccupant sets it again when
     * it queues a spotted unit, so the value that survives is 1. */
    CheckEqI32(nFlag1036, 1, "cleared on entry, then set again by the spot");
    CheckEqI32(viewer.nMultiTarget, 1, "only the enemy-side unit is queued");
    CheckEqI32(viewer.nSpottedCount, 1, "it is spotted");
    Check(viewer.multiOrAnim.apMultiTargets[0] == &enemy, "the enemy pointer is stored");
    Check(ctx.pSelUnit == pSelOuter, "pSelUnit is restored");
    Check(pScratch == NULL, "the scratch target slot is cleared");

    /* The same-side unit of type 16 is reached but not queued, and the ally is
     * skipped outright. Replace the enemy with the far one: 20 cells away, past
     * the 15-cell limit, so nothing is traced at all. */
    apEnemy[0] = &distant;
    memset(&viewer, 0, sizeof(viewer));
    viewer.bStatus = 0;
    viewer.pCell = &g_aScanCells[0][10][10];
    Unit_ScanVisibleUnits(&viewer, NULL, ScanLookup, NULL, &ctx, &rosters, &pScratch);
    CheckEqI32(viewer.nMultiTarget, 0, "a unit 20 cells away is never traced");
}

static void TestRecalcInventory(void)
{
    Soldier unit;
    TactItem handL;
    TactItem handR;
    InvStatHooks hooks;

    printf("Unit_RecalcInventoryStat\n");
    memset(&unit, 0, sizeof(unit));
    memset(&handL, 0, sizeof(handL));
    memset(&handR, 0, sizeof(handR));
    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnObjWeight = WeightL;
    hooks.pfnObjTwoHanded = TwoHandedL;
    hooks.pfnObjAmmoCap = AmmoCapL;
    hooks.pfnAmmoWeight = AmmoWeight;

    /* Empty inventory: just the base burden of 1. */
    CheckEqI32(Unit_RecalcInventoryStat(&unit, &hooks), 1, "empty inventory burden 1");

    /* One hand item of weight 5, no ammo. */
    unit.pHandL = &handL;
    g_bWeightL = 5;
    g_bTwoHandedL = 0;
    g_bAmmoCapL = 12;
    handL.nAmmo = 0;
    CheckEqI32(Unit_RecalcInventoryStat(&unit, &hooks), 6, "hand item weight added");

    /* A loaded weapon adds its ammo object's weight (nIndex). */
    handL.nAmmo = 1;
    handL.nIndex = 7;
    CheckEqI32(Unit_RecalcInventoryStat(&unit, &hooks), 13,
               "loaded weapon adds the ammo object weight");

    /* An unlimited weapon does not add ammo weight. */
    g_bAmmoCapL = 255;
    CheckEqI32(Unit_RecalcInventoryStat(&unit, &hooks), 6,
               "unlimited weapon adds no ammo weight");

    /* A two-handed weapon counts as one item. */
    g_bAmmoCapL = 12;
    g_bTwoHandedL = 1;
    CheckEqI32(Unit_RecalcInventoryStat(&unit, &hooks), 6,
               "two-handed weapon adds no ammo weight");

    /* A belt item uses the same rules. Clear the hand so its weight does not
     * mix in, and set the shared weight to 3. */
    g_bTwoHandedL = 0;
    unit.pHandL = NULL;
    unit.apBelt[0] = &handR;
    handR.nAmmo = 0;
    g_bWeightL = 3;
    CheckEqI32(Unit_RecalcInventoryStat(&unit, &hooks), 4, "belt item weight added");

    CheckEqI32(Unit_RecalcInventoryStat(NULL, &hooks), 0, "NULL unit returns 0");
    CheckEqI32(Unit_RecalcInventoryStat(&unit, NULL), 0, "NULL hooks returns 0");
}

/* Unit_bFacingAngleDuel @ 0x4dcf4.
 * Facing 2 points down +X and facing 6 points down -X: the facing angle is
 * (bFacing - 2) * 0.785375. The game's own half turn is 3.1415, a hair under the
 * real pi, which is what makes an opposite bearing wrap. */
static void TestFacingAngleDuel(void)
{
    MapCell cellA, cellB;
    Soldier a, b;
    int nAWins, i;

    printf("Unit_bFacingAngleDuel\n");
    memset(&cellA, 0, sizeof(cellA));
    memset(&cellB, 0, sizeof(cellB));
    cellA.nX = 10;
    cellA.nY = 10;
    cellB.nX = 11;
    cellB.nY = 10;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.pCell = &cellA;
    b.pCell = &cellB;
    a.bFacing = 2;
    b.bFacing = 6;

    /* auStat[3] == 0 zeroes every reaction roll, and a zero roll can never beat
     * a non-negative one, so this side loses whatever the dice do. */
    a.auStat[3] = 0;
    b.auStat[3] = 100;
    bool bAlwaysLoses = true;
    srand(1);
    for (i = 0; i < 50; i++) {
        if (Unit_bFacingAngleDuel(&a, &b)) {
            bAlwaysLoses = false;
            break;
        }
    }
    Check(bAlwaysLoses, "a unit that fails its reaction roll never wins");

    /* Both turned at each other: A looks down +X, B looks down -X. The duel is
     * symmetric, so the wins split. */
    a.auStat[3] = 100;
    nAWins = 0;
    srand(2024);
    for (i = 0; i < 200; i++) {
        if (Unit_bFacingAngleDuel(&a, &b)) {
            ++nAWins;
        }
    }
    Check(nAWins > 70 && nAWins < 130, "a symmetric duel splits the wins");
    printf("  symmetric duel: A won %d/200\n", nAWins);

    /* B turns the other way. Its error is trunc(0 - pi) = -3, magnitude 3, which
     * is under the game's 3.1415 half turn, so it is not wrapped and scores 0. */
    b.bFacing = 2;
    nAWins = 0;
    srand(2024);
    for (i = 0; i < 200; i++) {
        if (Unit_bFacingAngleDuel(&a, &b)) {
            ++nAWins;
        }
    }
    Check(nAWins > 150, "the well turned unit wins almost every duel");
    printf("  A facing B, B facing away: A won %d/200\n", nAWins);
}

static ObjectRecFull g_aDropObj[32];
static TactItem g_aDropItem[32];

static void DropReset(Soldier *pUnit, MapCell *pCell)
{
    memset(pUnit, 0, sizeof(*pUnit));
    memset(pCell, 0, sizeof(*pCell));
    for (i32 n = 0; n < 32; ++n) {
        memset(&g_aDropObj[n], 0, sizeof(g_aDropObj[n]));
        memset(&g_aDropItem[n], 0, sizeof(g_aDropItem[n]));
        g_aDropObj[n].bListPri = (u8)n;
        g_aDropItem[n].pObj = &g_aDropObj[n];
    }
    pUnit->pCell = pCell;
}

static i32 DropListLen(const MapCell *pCell)
{
    i32 n = 0;

    for (TactItem *p = pCell->pItemList; p != NULL; p = p->pNext) {
        ++n;
    }
    return n;
}

static void TestDropItems(void)
{
    Soldier unit;
    MapCell cell;
    i32 nSlot;

    printf("Unit_DropAllItemsToCell @ 0x4b4f8\n");

    /* A small unit loses everything it carries. */
    DropReset(&unit, &cell);
    unit.nUnitType = 0;
    unit.pHandL = &g_aDropItem[0];
    unit.pHandR = &g_aDropItem[1];
    for (nSlot = 0; nSlot < 9; ++nSlot) {
        unit.apBelt[nSlot] = &g_aDropItem[2 + nSlot];
    }
    for (nSlot = 0; nSlot < 8; ++nSlot) {
        unit.apBackpack[nSlot] = &g_aDropItem[11 + nSlot];
    }
    for (nSlot = 0; nSlot < 4; ++nSlot) {
        unit.aPockets[nSlot].apItem[0] = &g_aDropItem[19 + 2 * nSlot];
        unit.aPockets[nSlot].apItem[1] = &g_aDropItem[20 + 2 * nSlot];
    }
    Unit_DropAllItemsToCell(&unit);

    CheckEqI32(DropListLen(&cell), 25, "a small unit drops 25 items");
    Check(unit.pHandL == NULL && unit.pHandR == NULL, "both hands are cleared");

    /* The belt walk reads 4*n + 6*sizeof(MapCell) + offsetof(pWallSprA), which is
     * apBelt[n]. All nine go. */
    {
        bool bAllClear = true;
        for (nSlot = 0; nSlot < 9; ++nSlot) {
            bAllClear = bAllClear && unit.apBelt[nSlot] == NULL;
        }
        Check(bAllClear, "all nine belt slots are cleared");
    }

    /* The backpack walk skips slots 5 and 6, and a skipped slot keeps its item. */
    {
        bool bCarried = unit.apBackpack[5] == &g_aDropItem[16]
                     && unit.apBackpack[6] == &g_aDropItem[17];
        bool bSixGone = unit.apBackpack[0] == NULL && unit.apBackpack[4] == NULL
                     && unit.apBackpack[7] == NULL;
        Check(bCarried, "backpack slots 5 and 6 keep their items");
        Check(bSixGone, "the other six backpack slots are cleared");
    }

    /* Two passes of four pockets: apItem[0] of each, then apItem[1] of each. */
    {
        bool bAllClear = true;
        for (nSlot = 0; nSlot < 4; ++nSlot) {
            bAllClear = bAllClear && unit.aPockets[nSlot].apItem[0] == NULL
                                   && unit.aPockets[nSlot].apItem[1] == NULL;
        }
        Check(bAllClear, "all eight pocket slots are cleared");
    }

    /* The empty-slot placeholder is not an item: the slot is cleared, but nothing
     * reaches the cell list. */
    DropReset(&unit, &cell);
    unit.apBelt[0] = &g_itemSlotEmpty;
    unit.apBelt[1] = &g_aDropItem[0];
    Unit_DropAllItemsToCell(&unit);
    CheckEqI32(DropListLen(&cell), 1, "the placeholder is not dropped");
    Check(unit.apBelt[0] == NULL, "the placeholder slot is still cleared");

    /* A large unit keeps its hands. */
    DropReset(&unit, &cell);
    unit.nUnitType = 4;
    unit.pHandL = &g_aDropItem[0];
    unit.pHandR = &g_aDropItem[1];
    unit.apBelt[0] = &g_aDropItem[2];
    Unit_DropAllItemsToCell(&unit);
    Check(unit.pHandL == &g_aDropItem[0] && unit.pHandR == &g_aDropItem[1],
          "a large unit keeps both hands");
    CheckEqI32(DropListLen(&cell), 1, "a large unit still drops its belt");
}

/* byte_A7B80 @ 0xa7b80 (24 rows) and unk_A9768 @ 0xa9768 (23 muzzle blocks).
 * Values dumped from pufobeta.exe; the index formula is proven from ARM at
 * 0x50074..0x50094. */
static void TestTypeTables(void)
{
    Soldier u;

    printf("unit type tables\n");
    CheckEqI32(PUFO_UNIT_TYPE_COUNT, 24, "byte_A7B80 has 24 rows");
    /* Types 11 and 13 are {1,0,1,0}: flag 0 set, NOT large. The old table had
     * them as large. */
    CheckEqI32(g_aUnitTypeIsLarge[11], 0, "type 11 is not large");
    CheckEqI32(g_aUnitTypeIsLarge[13], 0, "type 13 is not large");
    CheckEqI32(g_aUnitTypeFlag0[11], 1, "type 11 has flag 0");
    CheckEqI32(g_aUnitTypeFlag0[13], 1, "type 13 has flag 0");
    CheckEqI32(g_aUnitTypeIsLarge[4], 1, "type 4 is large");
    CheckEqI32(g_aUnitTypeIsLarge[8], 1, "type 8 is large");
    CheckEqI32(g_aUnitTypeIsLarge[18], 1, "type 18 is large");
    CheckEqI32(g_aUnitTypeIsLarge[20], 1, "type 20 is large");
    CheckEqI32(g_aUnitTypeIsLarge[21], 1, "type 21 is large");
    CheckEqI32(g_aUnitTypeIsLarge[22], 0, "type 22 is not large");
    CheckEqI32(g_aUnitTypeIsLarge[23], 0, "type 23 is not large");

    memset(&u, 0, sizeof(u));
    u.nUnitType = 11;
    Check(!Unit_bIsLarge(&u), "Unit_bIsLarge(11) is false");
    u.nUnitType = 13;
    Check(!Unit_bIsLarge(&u), "Unit_bIsLarge(13) is false");
    u.nUnitType = 21;
    Check(Unit_bIsLarge(&u), "Unit_bIsLarge(21) is true");

    printf("muzzle offset table\n");
    CheckEqI32(PUFO_UNIT_MUZZLE_TYPE_COUNT, 23, "unk_A9768 has 23 type blocks");
    CheckEqI32(g_aUnitMuzzleOffset[0][0][0].nDx, 5, "type 0 group 0 facing 0 dx");
    CheckEqI32(g_aUnitMuzzleOffset[0][0][0].nDy, -5, "type 0 group 0 facing 0 dy");
    CheckEqI32(g_aUnitMuzzleOffset[0][0][0].nDz, 3, "type 0 group 0 facing 0 dz");
    CheckEqI32(g_aUnitMuzzleOffset[0][1][0].nDx, 6, "type 0 group 1 facing 0 dx");
    CheckEqI32(g_aUnitMuzzleOffset[0][1][7].nDz, 4, "type 0 group 1 facing 7 dz");
    /* Types 4..8 share a block whose second half is all zero. */
    CheckEqI32(g_aUnitMuzzleOffset[4][0][7].nDz, 0, "type 4 group 0 facing 7 dz");
    CheckEqI32(g_aUnitMuzzleOffset[4][1][0].nDx, 0, "type 4 group 1 facing 0 dx");
    /* Type 18 is the only zero-ish block with a non-zero group. */
    CheckEqI32(g_aUnitMuzzleOffset[18][0][0].nDx, 0, "type 18 group 0 facing 0 dx");
    CheckEqI32(g_aUnitMuzzleOffset[18][1][0].nDz, -3, "type 18 group 1 facing 0 dz");
    CheckEqI32(g_aUnitMuzzleOffset[18][1][7].nDz, -3, "type 18 group 1 facing 7 dz");
    CheckEqI32(g_aUnitMuzzleOffset[22][1][7].nDz, 0, "type 22 group 1 facing 7 dz");
}


static void TestRemoveById(void)
{
    Soldier *apUnits[3];
    i32 nCount;

    printf("Soldier_RemoveById\n");

    for (int i = 0; i < 3; ++i) {
        apUnits[i] = (Soldier *)malloc(sizeof(Soldier));
        Unit_ctor(apUnits[i]);
        apUnits[i]->nUnk440 = 7 + i;
    }
    nCount = 3;

    nCount = Soldier_RemoveById(apUnits, nCount, 8);
    CheckEqI32(nCount, 2, "  the middle soldier is found by roster id and removed");
    CheckEqI32(apUnits[0]->nUnk440, 7, "  the one before it stays");
    CheckEqI32(apUnits[1]->nUnk440, 9, "  the one after it shifts down");

    /* The original has no found test: a failed search deletes apSoldiers[count] and shortens
     * the list anyway. The port leaves everything alone. */
    nCount = Soldier_RemoveById(apUnits, nCount, 999);
    CheckEqI32(nCount, 2, "  an id nobody has is a no-op, not a one-past-the-end free");
    CheckEqI32(apUnits[1]->nUnk440, 9, "  and the last slot is still there");

    nCount = Soldier_RemoveById(apUnits, nCount, 7);
    CheckEqI32(nCount, 1, "  removing the first slot works");
    CheckEqI32(apUnits[0]->nUnk440, 9, "  the survivor moves to slot 0");

    nCount = Soldier_RemoveById(apUnits, nCount, 9);
    CheckEqI32(nCount, 0, "  and the list can be emptied this way");

    /* Every soldier this test allocated was freed by the port as it removed it, so there is
     * nothing left for the test to free. Freeing apUnits[0] here would be a double free. */

    nCount = Soldier_RemoveById(NULL, 3, 1);
    CheckEqI32(nCount, 3, "  no array at all is a no-op");
}

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    /* The name tests need the real texts.dat tables, so the archives and the text
     * blob have to be loaded. Everything else in this file is pure logic. */
    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }
    {
        AssetReader rdr;
        u8         *pBlob = NULL;
        u32         nRecords = 0;

        AssetReader_Clear(&rdr);
        if (AssetReader_Find(&rdr, "texts.dat")) {
            u32 cb = AssetReader_Size(&rdr);
            u8  aHdr[PUFO_TEXT_HDR_SIZE];

            nRecords = (cb - PUFO_TEXT_HDR_SIZE) / PUFO_TEXT_REC_SIZE_DISK;
            pBlob = malloc((size_t)nRecords * PUFO_TEXT_REC_STRIDE);
            if (pBlob == NULL
                || !AssetReader_Read(&rdr, aHdr, PUFO_TEXT_HDR_SIZE)) {
                free(pBlob);
                pBlob = NULL;
            } else {
                for (u32 i = 0; i < nRecords; ++i) {
                    if (!AssetReader_Read(&rdr,
                                          pBlob + (size_t)i * PUFO_TEXT_REC_STRIDE,
                                          PUFO_TEXT_REC_SIZE_DISK)) {
                        nRecords = i;
                        break;
                    }
                }
            }
        }
        if (pBlob != NULL) {
            Check(TextTables_Extract(pBlob, nRecords * PUFO_TEXT_REC_STRIDE),
                  "text tables extracted from the real texts.dat");
        } else {
            printf("  FAIL could not load texts.dat\n");
            g_nFailures += 1;
        }
        free(pBlob);
    }

    TestLayout();
    TestPredicates();
    TestResetBattleState();
    TestFacing();
    TestPlacement();
    TestRemove();
    TestRemoveById();
    TestGenerateNames();
    TestGenerate();
    TestControlRecovery();
    TestExtraSpr();
    TestAnimPose();
    TestCanStepExitDir();
    TestArmorTuWarn();
    TestSpotCellOccupant();
    TestScanVisibleUnits();
    TestRecalcInventory();
    TestFacingAngleDuel();
    TestDropItems();
    TestTypeTables();

    TextTables_Free();
    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
