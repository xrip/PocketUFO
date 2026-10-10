/* test_ballist.c - parity checks for the aim block, the shot arc and the
 * throw solver. Evidence: Tact_ComputeAimVector @ 0x30368,
 * Tact_TraceProjectileArc @ 0x2fc38, Tact_SimulateThrowArc @ 0x3102c. */
#include "game/pufo_ballist.h"
#include "game/pufo_los.h"
#include "game/pufo_mapcell.h"

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

    printf("%d checks, %d failures\n", g_nChecks, g_nFails);
    return g_nFails == 0 ? 0 : 1;
}
