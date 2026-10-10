/* test_vis.c - parity checks for the visibility update.
 * Evidence: Unit_UpdateVisibility @ 0x48580. */
#include "game/pufo_unit.h"
#include "game/pufo_los.h"
#include "game/pufo_vistry.h"
#include "game/pufo_combat.h"
#include "game/pufo_mapcell.h"

#include <stdio.h>
#include <string.h>

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

int main(void)
{
    memset(g_aRays, 0, sizeof(g_aRays));
    memset(g_aSteps, 0, sizeof(g_aSteps));
    Tact_BuildVisRayTable(g_aRays, g_aSteps);

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
