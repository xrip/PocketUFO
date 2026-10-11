/* test_ballist.c - parity checks for the aim block, the shot arc and the
 * throw solver. Evidence: Tact_ComputeAimVector @ 0x30368,
 * Tact_TraceProjectileArc @ 0x2fc38, Tact_SimulateThrowArc @ 0x3102c,
 * Unit_RollChance @ 0x25e6c. */
#include "game/pufo_ballist.h"
#include "game/pufo_los.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_unit.h"
#include "util/pufo_rand.h"
#include <string.h>


#include <math.h>
#include <stdio.h>
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

static void CheckNear(const char *pWhat, double dGot, double dWant)
{
    Check(pWhat, fabs(dGot - dWant) < 1e-6);
}

/* True when nGot is what _dtoi(trig(k) * dMag) gives for some whole radian
 * k in 0..6. A double-precision angle or a floor instead of _dtoi lands
 * outside this set. */
static int WholeRadianMatch(i32 nGot, double dMag, int bCos)
{
    for (i32 k = 0; k <= 6; ++k) {
        double d = bCos ? cos((double)k) : sin((double)k);
        if ((i32)(d * dMag) == nGot) {
            return 1;
        }
    }
    return 0;
}


/* A 60 x 60 x 4 cell grid, addressed the way the original addresses its own. */
static MapCell g_aCells[PUFO_MAP_LAYERS][60][60];

static MapCell *CellLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nY < 0 || nLayer < 0 || nX >= 60 || nY >= 60) {
        return NULL;
    }
    return &g_aCells[nLayer][nY][nX];
}

static MapCell g_aTraceCells[4][12][12];

static MapCell *TraceLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    return &g_aTraceCells[nLayer][nY][nX];
}

static void TestMovePieces(void)
{
    Soldier u;
    ObjectRec o;
    TactItem w;
    MapCell c;
    Mc1Rec ceil;
    MapGeom geom;
    MuzzleOffset m;

    printf("muzzle offset (unk_A9768)\n");
    memset(&u, 0, sizeof(u));
    memset(&o, 0, sizeof(o));
    u.nUnitType = 0;
    u.bFacing = 0;
    m = Tact_MuzzleOffset(&u, &o);
    Check("type 0 facing 0 is (5,-5,3)", m.nDx == 5 && m.nDy == -5 && m.nDz == 3);
    o.bListPri = 1;
    m = Tact_MuzzleOffset(&u, &o);
    Check("bListPri 1 selects the second group (6,-6,4)",
          m.nDx == 6 && m.nDy == -6 && m.nDz == 4);
    o.bListPri = 5;
    m = Tact_MuzzleOffset(&u, &o);
    Check("bListPri above 1 clamps to 0", m.nDx == 5 && m.nDy == -5 && m.nDz == 3);
    u.nUnitType = 4;
    u.bFacing = 7;
    m = Tact_MuzzleOffset(&u, &o);
    Check("type 4 facing 7 is zero", m.nDx == 0 && m.nDy == 0 && m.nDz == 0);
    u.nUnitType = 18;
    o.bListPri = 1;
    u.bFacing = 3;
    m = Tact_MuzzleOffset(&u, &o);
    Check("type 18 group 1 is (0,0,-3)", m.nDx == 0 && m.nDy == 0 && m.nDz == -3);
    u.nUnitType = 22;
    o.bListPri = 0;
    m = Tact_MuzzleOffset(&u, &o);
    Check("type 22 is zero", m.nDx == 0 && m.nDy == 0 && m.nDz == 0);

    printf("cell centre (0x5003C..0x50070)\n");
    memset(&c, 0, sizeof(c));
    c.nX = 3; c.nY = 4; c.nZ = 2; c.pCeilSpr = NULL;
    Check("x = 16*nX + 8", Map_CellCentreX(&c) == 56);
    Check("y = 16*nY + 8", Map_CellCentreY(&c) == 72);
    Check("z = 12*nZ + 6 with no ceiling", Map_CellCentreZ(&c) == 30);
    memset(&ceil, 0, sizeof(ceil));
    ceil.iCeilParam = -20;
    c.pCeilSpr = &ceil;
    Check("ceiling -20 lifts by (-12*-20)/24 = 10", Map_CellCentreZ(&c) == 40);
    ceil.iCeilParam = 5;
    Check("ceiling +5 lowers by -60/24 = -2 (truncates toward zero)",
          Map_CellCentreZ(&c) == 28);

    printf("charge one shot (0x502CC..0x50328)\n");
    memset(&o, 0, sizeof(o));
    memset(&w, 0, sizeof(w));
    w.pObj = &o;
    o.bAmmoCap = 0;
    o.uSortKey = 1;
    i32 nCount = 0;
    w.nAmmo = 10;
    Tact_ChargeShotAmmo(&w, &nCount);
    Check("normal weapon loses one round", w.nAmmo == 9);
    Check("shot counter advanced", nCount == 1);
    w.nAmmo = 1;
    Tact_ChargeShotAmmo(&w, &nCount);
    Check("ammo byte wraps 1 -> 0", w.nAmmo == 0);
    Tact_ChargeShotAmmo(&w, &nCount);
    Check("empty weapon stays empty", w.nAmmo == 0);
    Check("counter advances even when ammo is empty", nCount == 3);
    w.nAmmo = 7;
    o.bAmmoCap = 0xFF;
    Tact_ChargeShotAmmo(&w, &nCount);
    Check("bAmmoCap 255 never charges", w.nAmmo == 7);
    o.bAmmoCap = 0;
    o.uSortKey = 13;
    nCount = 0;
    w.nAmmo = 4;
    Tact_ChargeShotAmmo(&w, &nCount);
    Check("sort key 13 charges while the counter is zero", w.nAmmo == 3);
    Tact_ChargeShotAmmo(&w, &nCount);
    Check("sort key 13 does not charge once the counter is non-zero",
          w.nAmmo == 3);
    o.uSortKey = 35;
    w.nAmmo = 4;
    nCount = 2;
    Tact_ChargeShotAmmo(&w, &nCount);
    Check("sort key 35 does not charge once the counter is non-zero",
          w.nAmmo == 4);
    Check("counter still advances for the special keys", nCount == 3);

    printf("clear shot-trace flags (0x4FE20..0x4FE64)\n");
    geom.nDimX = 1;
    geom.nDimY = 1;
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x)
                g_aTraceCells[z][y][x].bFlags29 = 0xFF;
    Map_ClearShotTraceFlags(TraceLookup, NULL, &geom);
    int bAll = 1, bOut = 1;
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x) {
                u8 v = g_aTraceCells[z][y][x].bFlags29;
                if (y < 10 && x < 10) { if (v != 0xDF) bAll = 0; }
                else if (v != 0xFF) bOut = 0;
            }
    Check("10*dimY rows by 10*dimX columns on all 4 layers are cleared", bAll);
    Check("cells outside the sweep keep their flag", bOut);
}

int main(void)
{
    MapGeom geom = {1, 1};   /* 10 x 10 cells, 160 x 160 voxels */
    LosTables empty = {NULL, 0, NULL, 0};
    AimState aim;
    ShotTrace tr;
    ThrowArc arc;

    printf("aim row table\n");
    Tact_ComputeAimVector(&aim, &geom, NULL, NULL, 0, 0, 0, 16, 16, 0, 1);
    Check("row id stored", aim.nRowId == 1);
    Check("row 1 entry 0 = 0x11", aim.anAimRow[0] == 0x11);
    Check("row 1 entry 10 = 0", aim.anAimRow[10] == 0);
    Tact_ComputeAimVector(&aim, &geom, NULL, NULL, 0, 0, 0, 16, 16, 0, 0);
    Check("row 0 entry 10 = 1", aim.anAimRow[10] == 1);
    Check("row 0 entry 0 = 3", aim.anAimRow[0] == 3);
    Tact_ComputeAimVector(&aim, &geom, NULL, NULL, 0, 0, 0, 16, 16, 0, 5);
    Check("row 5 entry 0 = 9", aim.anAimRow[0] == 9);
    Check("row 5 entry 10 = 0", aim.anAimRow[10] == 0);

    printf("diagonal angles\n");
    Tact_ComputeAimVector(&aim, &geom, NULL, NULL, 0, 0, 0, 16, 16, 0, 0);
    CheckNear("yaw = atan2(16,16)", aim.dYaw, 0.7853981633974483);
    CheckNear("pitch = 0", aim.dPitch, 0.0);
    CheckNear("bound = hypot(16,16)", aim.dBound, 22.627416997969522);
    Check("yaw is deterministic", !aim.bYawFromRandom);
    Check("travel starts at 0", aim.dTravel == 0.0);
    Check("origin stored", aim.nOriginX == 0 && aim.nOriginY == 0 && aim.nOriginZ == 0);
    Check("target stored", aim.nTargetX == 16 && aim.nTargetY == 16);

    printf("vertical shot\n");
    Tact_ComputeAimVector(&aim, &geom, NULL, NULL, 80, 80, 0, 80, 80, 24, 0);
    CheckNear("pitch = +1.57075", aim.dPitch, 1.57075);
    CheckNear("bound = dz", aim.dBound, 24.0);
    Check("yaw randomised", aim.bYawFromRandom);
    Check("yaw in [0, 6.2]", aim.dYaw >= 0.0 && aim.dYaw <= 6.2);
    Tact_ComputeAimVector(&aim, &geom, NULL, NULL, 80, 80, 24, 80, 80, 0, 0);
    CheckNear("downward pitch = -1.57075", aim.dPitch, -1.57075);

    printf("origin cell stamp (Map+0x1d bit 5)\n");
    for (i32 l = 0; l < PUFO_MAP_LAYERS; ++l) {
        for (i32 y = 0; y < 60; ++y) {
            for (i32 x = 0; x < 60; ++x) {
                g_aCells[l][y][x].bFlags29 = 0;
            }
        }
    }
    Tact_ComputeAimVector(&aim, &geom, CellLookup, NULL, 100, 100, 0, 116, 116, 0, 0);
    Check("cell (6,6,0) stamped", (g_aCells[0][6][6].bFlags29 & 0x20u) != 0);
    Tact_ComputeAimVector(&aim, &geom, CellLookup, NULL, 200, 100, 0, 216, 116, 0, 0);
    Check("out of bounds origin stamps nothing", g_aCells[0][6][12].bFlags29 == 0);
    Tact_ComputeAimVector(&aim, &geom, CellLookup, NULL, 100, 100, 13, 116, 116, 13, 0);
    Check("layer = z/12 = 1", (g_aCells[1][6][6].bFlags29 & 0x20u) != 0);

    printf("projectile arc, nothing blocks\n");
    /* 45 degrees, flat. The walk ends when x reaches 160 * nMapDimX = 160. */
    Check("returns the out-of-range flag",
          Tact_TraceProjectileArc(&aim, &empty, &geom, CellLookup, NULL, NULL, NULL,
                                  100, 100, 0, 116, 116, 0, 0, &tr));
    Check("out of range set", tr.bOutOfRange);
    Check("left the map volume", tr.bLeftMap);
    CheckNear("travel saved = 85.0", tr.dTravelSave, 85.0);
    Check("end voxel 160,160,0", tr.nEndX == 160 && tr.nEndY == 160 && tr.nEndZ == 0);
    Check("no target recorded (no voxel ever blocked)",
          tr.nTgtX == 0 && tr.nTgtY == 0 && tr.nTgtZ == 0);

    printf("throw solver\n");
    Tact_SimulateThrowArc(&arc, &geom, NULL, NULL, 0, 0, 0, 32, 0, 0);
    Check("flat 32-tile throw lands", arc.bLanded);
    CheckNear("pitch 0.31515", arc.dPitch, 0.31515000000000004);
    CheckNear("arc denominator 23.9437733", arc.dArcDenom, 23.943773332122607);
    CheckNear("v0^2 = 2*g*(dz+3)/sin^2", arc.dLaunchVelSq, 55.08 / (sin(0.31515) * sin(0.31515)));

    Tact_SimulateThrowArc(&arc, &geom, NULL, NULL, 0, 0, 0, 0, 48, 0);
    Check("flat 48-tile throw lands", arc.bLanded);
    CheckNear("arc denominator 31.6606068", arc.dArcDenom, 31.66060684562032);

    Tact_SimulateThrowArc(&arc, &geom, NULL, NULL, 0, 0, 0, 16, 16, 12);
    Check("upward throw lands", arc.bLanded);
    CheckNear("arc denominator 19.3139508", arc.dArcDenom, 19.31395079627728);

    /* |dx| < 3 and |dy| < 3 pushes the target 2..4 tiles away first. */
    Tact_SimulateThrowArc(&arc, &geom, NULL, NULL, 30, 30, 0, 31, 31, 0);
    Check("close target is pushed away",
          abs(arc.nTargetX - 31) >= 2 && abs(arc.nTargetY - 31) >= 2);

    printf("throw range budget (Tact_ResolveThrowLanding @ 0x31a18)\n");
    {
        ThrowBudget b;
        i32 nQuot = -1;
        i32 nRem = -1;

        /* auStat[8]*uStunOrArmor = 50*60 = 3000, auStat[9]*bWeight = 50*2 = 100.
         * 3000/100 = 30, shifted right by one gives 15. */
        b.nStatThrow = 50; b.nStunOrArmor = 60; b.nStatStrength = 50; b.nWeight = 2;
        CheckNear("quotient/2 is the budget", Throw_RangeBudget(&b, &nQuot, &nRem), 15.0);
        Check("quotient 30, remainder 0", nQuot == 30 && nRem == 0);

        /* 3000/350 = 8 with remainder 200. The remainder is dead: _itod converts
         * only the int in R0, so the budget is 8 >> 1 = 4. */
        b.nWeight = 7;
        CheckNear("a remainder changes nothing", Throw_RangeBudget(&b, &nQuot, &nRem), 4.0);
        Check("remainder 200 is left in R1 and ignored", nRem == 200);

        /* (3000/100)/2 with an odd quotient truncates: 31/2 = 15. */
        b.nStatThrow = 100; b.nStunOrArmor = 31; b.nWeight = 2;
        CheckNear("odd quotient truncates", Throw_RangeBudget(&b, &nQuot, &nRem), 15.0);

        /* 1/1 -> quotient 1 -> half 0 -> below the floor. */
        b.nStatThrow = 1; b.nStunOrArmor = 1; b.nStatStrength = 1; b.nWeight = 1;
        CheckNear("tiny budget clamps to 1.0", Throw_RangeBudget(&b, NULL, NULL), 1.0);

        /* 100*60/(50*2) = 60 -> half 30 -> above the ceiling. */
        b.nStatThrow = 100; b.nStunOrArmor = 60; b.nStatStrength = 50; b.nWeight = 2;
        CheckNear("big budget clamps to 22.0", Throw_RangeBudget(&b, NULL, NULL), 22.0);
    }

    printf("throw landing loop\n");
    {
        MapGeom wide = {4, 4};   /* 40 x 40 cells, 640 x 640 voxels */
        ThrowBudget b = {50, 50, 7, 60};   /* 3000 / 350 = 8 -> budget 4.0 */
        ThrowBudget bExact = {50, 50, 2, 60};
        ThrowLanding land;
        i32 nRet;

        /* A 160-voxel flat throw. The flag is clear on entry, so the loop runs
         * exactly once: dTravel takes one 0.5 bump and yaw is 0. */
        nRet = Tact_ResolveThrowLanding(&arc, &land, &empty, &wide, CellLookup, NULL,
                                        NULL, NULL, &bExact, 4, false,
                                        160, 160, 12, 320, 160, 12);
        CheckNear("one bump of 0.5", land.dTravel, 0.5);
        CheckNear("x follows yaw 0", land.dMtX, 160.5);
        CheckNear("y unchanged", land.dMtY, 160.0);
        Check("not landed (0.5 < 160 - 8)", !land.bLanded);
        Check("flag stays clear", !land.bHitStop);
        Check("not rejected by range", !land.bRejected);
        Check("returns the landed flag", nRet == 0);
        CheckNear("arc bound is the horizontal run", arc.dBound, 160.0);
        CheckNear("arc yaw is atan2(0, 160)", arc.dYaw, 0.0);

        /* Same throw with the flag set: the loop keeps stepping until the sample
         * reaches 160 * nMapDimX = 640. */
        nRet = Tact_ResolveThrowLanding(&arc, &land, &empty, &wide, CellLookup, NULL,
                                        NULL, NULL, &b, 4, true,
                                        160, 160, 12, 320, 160, 12);
        Check("loop ran on the caller's flag", land.dTravel > 100.0);
        Check("landed once travel passed bound - 8", land.bLanded);
        Check("loop ends on the map volume limit", land.dMtX >= 160.0 * 4.0);
        Check("out of budget at the end, so returns 0", nRet == 0);

        /* |dz| larger than the bound selects the other bump: bound/|dz| * 0.01.
         * The range test costs 16 * 0.0625 * (64/8) = 8.0, so this uses the
         * 15.0 budget rather than the 4.0 one. */
        nRet = Tact_ResolveThrowLanding(&arc, &land, &empty, &wide, CellLookup, NULL,
                                        NULL, NULL, &bExact, 4, false,
                                        160, 160, 0, 176, 160, 64);
        CheckNear("bound bump is 16/64 * 0.01", land.dTravel, 0.0025);
        Check("steep throw is not rejected", !land.bRejected);

        /* A short throw is landed immediately, so the tail test decides the
         * result: 0.5 * 0.0625 is well inside the budget. */
        nRet = Tact_ResolveThrowLanding(&arc, &land, &empty, &geom, CellLookup, NULL,
                                        NULL, NULL, &b, 1, true,
                                        160, 160, 12, 168, 160, 12);
        Check("short throw lands", land.bLanded);
        Check("returns 1 when landed and in budget", nRet == 1);

        /* Range rejection: 1600 voxels of run times a height factor of 10. */
        nRet = Tact_ResolveThrowLanding(&arc, &land, &empty, &wide, CellLookup, NULL,
                                        NULL, NULL, &b, 4, false,
                                        160, 160, 0, 1760, 160, 80);
        Check("too far is rejected", land.bRejected && nRet == 0);

        /* The height factor is an integer division, so a climb under 8 voxels
         * earns nothing and the throw is never rejected on height. */
        nRet = Tact_ResolveThrowLanding(&arc, &land, &empty, &wide, CellLookup, NULL,
                                        NULL, NULL, &b, 4, false,
                                        160, 160, 0, 1760, 160, 7);
        Check("a 7-voxel climb adds no range cost", !land.bRejected);
    }

    printf("Unit_RollChance scatter\n");
    {
        ScatterOffset sc;
        i32 nSeed100 = 0;

        /* The roll is rand16(100) + 1, so it lives in [1, 100]. */
        srand(1);
        sc.nDx = sc.nDy = sc.nDz = 7;
        Check("nChance 100 always succeeds", Unit_RollChance(30.0, 100, &sc) == 0);
        Check("success clears the x slot", sc.nDx == 0);
        Check("success clears the y slot", sc.nDy == 0);
        Check("success clears the z slot", sc.nDz == 0);

        /* Draw counts are part of the parity contract: one draw on the success
         * path, four on the failure path. */
        srand(1);
        (void)Unit_RollChance(30.0, 100, &sc);
        i32 nAfterSuccess = rand();
        srand(1);
        (void)rand();
        Check("success path consumes one draw", rand() == nAfterSuccess);

        srand(1);
        (void)Unit_RollChance(30.0, 0, &sc);
        i32 nAfterFail = rand();
        srand(1);
        for (int i = 0; i < 4; ++i) {
            (void)rand();
        }
        Check("failure path consumes four draws", rand() == nAfterFail);

        srand(1);
        sc.nDx = sc.nDy = sc.nDz = 7;
        i32 nRet = Unit_RollChance(30.0, 0, &sc);
        Check("failure returns the z slot", nRet == sc.nDz);

        srand(4242);
        i32 nRetA = Unit_RollChance(77.5, 20, &sc);
        ScatterOffset scA = sc;
        srand(4242);
        i32 nRetB = Unit_RollChance(77.5, 20, &sc);
        Check("fixed seed reproduces the triple",
              nRetA == nRetB && sc.nDx == scA.nDx && sc.nDy == scA.nDy
              && sc.nDz == scA.nDz);

        /* nMiss + 10 is in [10, 110], so no component can exceed the widest
         * magnitude the roll can produce. */
        double dMagMax = (double)((float)(40.0 * 0.27) * 110.0f * 0.01f);
        int bInBounds = 1;
        for (i32 nSeed = 1; nSeed <= 300; ++nSeed) {
            srand(nSeed);
            Unit_RollChance(40.0, 0, &sc);
            if (fabs((double)sc.nDx) > dMagMax || fabs((double)sc.nDy) > dMagMax
                || fabs((double)sc.nDz) > dMagMax) {
                bInBounds = 0;
            }
        }
        Check("scatter stays inside the magnitude bound", bInBounds);

        /* nChance 99 fails only on a roll of 100, and there nMiss is
         * Game_RandU16(1), which is always 0. That pins the magnitude exactly:
         * 27.0f * 10.0f * 0.01f, so every slot must be a whole-radian sample
         * of that magnitude. */
        for (i32 nSeed = 1; nSeed <= 20000 && nSeed100 == 0; ++nSeed) {
            srand(nSeed);
            if ((i32)Game_RandU16(100) + 1 == 100) {
                nSeed100 = nSeed;
            }
        }
        Check("a seed exists that rolls 100", nSeed100 != 0);
        if (nSeed100 != 0) {
            double dMag = (double)((float)(100.0 * 0.27) * 10.0f * 0.01f);
            CheckNear("pinned magnitude is 2.7", dMag, 2.7);
            srand(nSeed100);
            Unit_RollChance(100.0, 99, &sc);
            Check("x is a whole-radian cosine", WholeRadianMatch(sc.nDx, dMag, 1));
            Check("y is a whole-radian sine", WholeRadianMatch(sc.nDy, dMag, 0));
            Check("z is a whole-radian sine", WholeRadianMatch(sc.nDz, dMag, 0));
        }
    }

    TestMovePieces();

    printf("%d checks, %d failures\n", g_nChecks, g_nFails);
    return g_nFails == 0 ? 0 : 1;
}
