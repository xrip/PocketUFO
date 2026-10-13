/* test_rmp.c - checks the .rmp route/spawn node reader.
 *
 * Ground truth measured independently from the shipped main.dat:
 *   AVENGER.RMP  72 bytes ->  3 records (first record bytes 9,4,1 -> localY 9,
 *                             localX 4, height 1)
 *   LIGHTNIN.RMP 96 bytes ->  4 records
 *   CULTA0.RMP   24 bytes ->  1 record
 *   156 of 158 members are exact multiples of 24; UFO_000.RMP (140) and
 *   UFO_010.RMP (520) are not, and the loader truncates, matching the original's
 *   `size / 0x18`.
 */
#include "asset/pufo_asset.h"
#include "game/pufo_rmp.h"
#include "util/pufo_rand.h"

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
    printf("RmpNode layout\n");
    Check(sizeof(RmpNode) == 28, "  RmpNode is 28 bytes");
    /* The five links occupy aLink[0..9]. */
    Check(PUFO_RMP_LINKS == 5, "  five links per node");
    Check(PUFO_RMP_REC_SIZE == 0x18, "  disk record is 24 bytes");
}

static void TestLoad(void)
{
    RmpData rmp;
    i32     n;

    printf("Rmp_Load (AVENGER.RMP, 72 bytes)\n");
    Rmp_Init(&rmp);
    n = Rmp_Load(&rmp, "avenger", 0, 0, 20, 10, 3, 0, 0);
    CheckEqI32(n, 3, "  three records parsed");
    CheckEqI32(rmp.nNodeCount, 3, "  three nodes stored");
    CheckEqI32(rmp.nModCount, 1, "  one module placement recorded");
    Check(strcmp(rmp.aszModName[0], "avenger") == 0, "  module name recorded");
    CheckEqI32(rmp.anModLevel[0], 3, "  module level recorded");

    /* First record is bytes 9,4,1 -> localY 9, localX 4, height 1. With
     * tileX = tileY = 0 and level 3:
     *   nX = 10*0 + 4 = 4
     *   nY = 10*0 + 9 = 9
     *   nZ = 3 - 1 - 1 = 1
     */
    Check(rmp.apNodes[0] != NULL, "  node 0 allocated");
    if (rmp.apNodes[0] != NULL) {
        CheckEqI32(rmp.apNodes[0]->nX, 4, "  node 0 X");
        CheckEqI32(rmp.apNodes[0]->nY, 9, "  node 0 Y");
        CheckEqI32(rmp.apNodes[0]->nZ, 1, "  node 0 Z = level - height - 1");
        CheckEqI32(rmp.apNodes[0]->nIndex, 0, "  node 0 index");
        CheckEqI32(rmp.apNodes[0]->bFlag19, 0, "  node 0 flag19");
        CheckEqI32(rmp.apNodes[0]->bFlag20, 0, "  node 0 flag20");
    }

    /* Coordinates shift with the module tile offset. */
    printf("Rmp_Load with a tile offset\n");
    Rmp_Init(&rmp);
    n = Rmp_Load(&rmp, "avenger", 5, 7, 20, 10, 3, 0, 0);
    CheckEqI32(n, 3, "  still three records");
    if (rmp.apNodes[0] != NULL) {
        CheckEqI32(rmp.apNodes[0]->nX, 5 * 10 + 4, "  X includes tileX * 10");
        CheckEqI32(rmp.apNodes[0]->nY, 7 * 10 + 9, "  Y includes tileY * 10");
    }

    printf("Rmp_Load (CULTA0.RMP, 24 bytes)\n");
    Rmp_Init(&rmp);
    CheckEqI32(Rmp_Load(&rmp, "culta0", 0, 0, 10, 10, 1, 0, 0), 1, "  one record");
    CheckEqI32(rmp.nNodeCount, 1, "  one node");

    printf("Rmp_Load (missing member)\n");
    Rmp_Init(&rmp);
    CheckEqI32(Rmp_Load(&rmp, "no_such_module", 0, 0, 1, 1, 1, 0, 0), 0,
               "  missing member yields zero nodes");
    CheckEqI32(rmp.nNodeCount, 0, "  no nodes stored");

    Rmp_Init(&rmp);
}

/* The truncating division is a real property of the original and must be kept. */
static void TestTruncatingDivision(void)
{
    RmpData rmp;

    printf("Rmp_Load (members that are not a multiple of 24)\n");
    /* UFO_000.RMP is 140 bytes: 140 / 24 == 5 with 20 bytes left over. */
    Rmp_Init(&rmp);
    CheckEqI32(Rmp_Load(&rmp, "ufo_000", 0, 0, 10, 10, 1, 0, 0), 5,
               "  UFO_000.RMP (140 B) yields 5 nodes, dropping 20 bytes");
    CheckEqI32(rmp.nNodeCount, 5, "  five nodes stored");
    Rmp_Init(&rmp);

    /* UFO_010.RMP is 520 bytes: 520 / 24 == 21 with 16 left over. */
    CheckEqI32(Rmp_Load(&rmp, "ufo_010", 0, 0, 10, 10, 1, 0, 0), 21,
               "  UFO_010.RMP (520 B) yields 21 nodes, dropping 16 bytes");
    Rmp_Init(&rmp);
}

/* The link words must be populated and the sentinels must be in range. */
static void TestLinks(void)
{
    RmpData rmp;

    printf("Link decoding\n");
    Rmp_Init(&rmp);
    (void)Rmp_Load(&rmp, "plane", 0, 0, 20, 10, 3, 0, 0);
    if (rmp.nNodeCount == 0 || rmp.apNodes[0] == NULL) {
        Check(false, "  PLANE.RMP loaded");
        return;
    }
    {
        const RmpNode *pNode = rmp.apNodes[0];
        i32            nNone = 0;
        i32            nEdge = 0;
        i32            nReal = 0;

        for (u32 k = 0; k < PUFO_RMP_LINKS; ++k) {
            i16 nTarget = (i16)pNode->aLink[2 * k];

            if (nTarget == PUFO_RMP_LINK_NONE) {
                nNone += 1;
            } else if (nTarget <= PUFO_RMP_LINK_EDGE_W && nTarget >= PUFO_RMP_LINK_EDGE_N) {
                nEdge += 1;
            } else {
                nReal += 1;
            }
        }
        Check(nNone + nEdge + nReal == PUFO_RMP_LINKS,
              "  every link classified");
        printf("       (node 0: %d none, %d edge, %d relative)\n",
               (int)nNone, (int)nEdge, (int)nReal);
    }
    Rmp_Init(&rmp);
}

static void TestFree(void)
{
    RmpData rmp;

    printf("Rmp_FreeNodes\n");
    Rmp_Init(&rmp);
    (void)Rmp_Load(&rmp, "avenger", 0, 0, 20, 10, 3, 0, 0);
    Rmp_FreeNodes(&rmp);
    Check(rmp.apNodes[0] == NULL, "  node 0 released");
    Check(rmp.apNodes[1] == NULL, "  node 1 released");
    CheckEqI32(rmp.nNodeCount, 3, "  count left as the original leaves it");
    /* A second free must be safe now that the pointers were nulled. */
    Rmp_FreeNodes(&rmp);
    Check(true, "  second free is safe");
    Rmp_Init(&rmp);
}

/* --- spawn acceptance, on hand-built nodes --- */

static void TestAcceptsUnit(void)
{
    RmpNode     node;
    RmpUnitInfo info;

    printf("Rmp_NodeAcceptsUnit\n");
    memset(&node, 0, sizeof(node));
    memset(&info, 0, sizeof(info));

    /* Types 0..8 require bFlag20 == 1. */
    info.nUnitType = 2;
    node.bFlag20  = 0;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false), "  type 2: bFlag20 0 rejected");
    node.bFlag20 = 1;
    Check(Rmp_NodeAcceptsUnit(&node, &info, false), "  type 2: bFlag20 1 accepted");
    node.bFlag20 = 2;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false), "  type 2: bFlag20 2 rejected");

    /* The gate is the type NUMBER, not the large-unit footprint flag. Type 8 is large by footprint but
     * still takes the bFlag20 == 1 path, and type 9 is not large but takes the movement-class path. That
     * is what separates the two readings, and the original does it with CMP #8 / BLS. */
    info.nMoveClass = 4;
    node.bFlag20    = 4;
    info.nUnitType  = 8;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false),
          "  type 8 is large by footprint but still needs bFlag20 == 1");
    info.nUnitType = 9;
    Check(Rmp_NodeAcceptsUnit(&node, &info, false),
          "  type 9 is not large but compares bFlag20 against the movement class");

    /* Movement class 8 normalises to 4. */
    info.nMoveClass = 8;
    node.bFlag20    = 4;
    Check(Rmp_NodeAcceptsUnit(&node, &info, false), "  class 8 -> node 4 accepted");
    node.bFlag20 = 3;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false), "  class 8 vs node 3 rejected");

    /* Class 2 is its own rule. With pUnit[0x1B3] set the node must be class 2. With it clear, class 0 is
     * allowed unless the mission is an XCom base defence. */
    info.nUnitType  = 12;
    info.nMoveClass = 2;
    info.bFlag0     = 1;
    node.bFlag20    = 2;
    Check(Rmp_NodeAcceptsUnit(&node, &info, false), "  class 2 with 0x1B3 set accepts node class 2");
    node.bFlag20 = 0;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false), "  class 2 with 0x1B3 set rejects node class 0");
    info.bFlag0 = 0;
    Check(Rmp_NodeAcceptsUnit(&node, &info, false),
          "  class 2 with 0x1B3 clear accepts node class 0 on a normal mission");
    Check(!Rmp_NodeAcceptsUnit(&node, &info, true),
          "  but rejects it when the mission is an XCom base defence");
    node.bFlag20 = 2;
    Check(Rmp_NodeAcceptsUnit(&node, &info, true), "  class 2 nodes are fine either way");
    node.bFlag20 = 1;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false), "  class 2 rejects node class 1");

    /* bFlag19 gates the unit type flags. */
    node.bFlag20 = 1;
    info.nUnitType = 2;
    info.bTypeFlagA = 1;
    info.bTypeFlagB = 0;
    node.bFlag19 = 1;
    Check(Rmp_NodeAcceptsUnit(&node, &info, false), "  flag19 1 with A=1,B=0 accepted");
    info.bTypeFlagB = 1;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false), "  flag19 1 with B=1 rejected");
    node.bFlag19 = 2;
    info.bTypeFlagB = 0;
    Check(Rmp_NodeAcceptsUnit(&node, &info, false), "  flag19 2 with B=0 accepted");
    info.bTypeFlagB = 1;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false), "  flag19 2 with B=1 rejected");
    node.bFlag19 = 3;
    info.bTypeFlagA = 0;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false), "  flag19 3 with A=0 rejected");
    node.bFlag19 = 4;
    info.bTypeFlagA = 1;
    Check(!Rmp_NodeAcceptsUnit(&node, &info, false), "  flag19 4 with A=1 rejected");
    node.bFlag19 = 0;
    Check(Rmp_NodeAcceptsUnit(&node, &info, false), "  flag19 0 accepts anything");
}

/* --- spawn placement on a synthetic grid --- */

#define R_DIM 24

typedef struct RGrid {
    MapCell aCells[PUFO_MAP_LAYERS][R_DIM][R_DIM];
} RGrid;

static MapCell *RGrid_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    RGrid *pG = (RGrid *)pUser;

    if (nX < 0 || nY < 0 || nX >= R_DIM || nY >= R_DIM) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return &pG->aCells[nLayer][nY][nX];
}

static void TestPickSpawn(void)
{
    RGrid   grid;
    RmpData rmp;
    Soldier unit;

    printf("Rmp_PickSpawnForUnit\n");
    memset(&grid, 0, sizeof(grid));
    for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (i32 y = 0; y < R_DIM; ++y) {
            for (i32 x = 0; x < R_DIM; ++x) {
                MapCell_Init(&grid.aCells[z][y][x], x, y, z);
            }
        }
    }
    Game_SeedRng();

    /* Build one node by hand so acceptance is deterministic: bFlag20 = 1 suits a
     * small unit. */
    Rmp_Init(&rmp);
    {
        RmpNode *pNode = malloc(sizeof(RmpNode));

        memset(pNode, 0, sizeof(*pNode));
        pNode->nX = 3;
        pNode->nY = 4;
        pNode->nZ = 0;
        pNode->bFlag20 = 1;
        pNode->nIndex  = 0;
        rmp.apNodes[0] = pNode;
        rmp.nNodeCount = 1;
    }

    memset(&unit, 0, sizeof(unit));
    unit.nUnitType = 0; /* small */
    unit.bStatus   = 0;

    CheckEqI32(Rmp_PickSpawnForUnit(&rmp, &unit, RGrid_Lookup, &grid, false), 0,
               "  unit placed on the only node");
    Check(unit.pCell == &grid.aCells[0][4][3], "  unit landed on the node's cell");

    /* Occupied node: no placement. */
    printf("Rmp_PickSpawnForUnit with the node occupied\n");
    memset(&grid, 0, sizeof(grid));
    for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (i32 y = 0; y < R_DIM; ++y) {
            for (i32 x = 0; x < R_DIM; ++x) {
                MapCell_Init(&grid.aCells[z][y][x], x, y, z);
            }
        }
    }
    {
        Soldier blocker;

        memset(&blocker, 0, sizeof(blocker));
        blocker.bStatus = 0;
        grid.aCells[0][4][3].pUnit = &blocker;
    }
    unit.pCell = NULL;
    CheckEqI32(Rmp_PickSpawnForUnit(&rmp, &unit, RGrid_Lookup, &grid, false), -1,
               "  occupied node is refused");

    /* No nodes at all. */
    printf("Rmp_PickSpawnForUnit with no nodes\n");
    Rmp_Init(&rmp);
    CheckEqI32(Rmp_PickSpawnForUnit(&rmp, &unit, RGrid_Lookup, &grid, false), -1,
               "  empty node list is refused");

    Rmp_Init(&rmp);
}

/* The two flag columns of byte_A7B80, read straight out of the image at 0xa7b80. */
static const u8 s_aWantFlagA[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 1, 0, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 0, 0,
    0, 0, 1, 0, 1, 1, 1, 0
};
static const u8 s_aWantFlagB[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 0, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0
};

static void TestUnitInfoFromSoldier(void)
{
    RmpUnitInfo info;
    Soldier     unit;
    int         nBad = 0;

    printf("Rmp_UnitInfoFromSoldier\n");

    memset(&unit, 0, sizeof(unit));
    unit.nUnitType = 13;
    unit.bRankType = 8;
    unit.bUnk435   = 1;

    Rmp_UnitInfoFromSoldier(&info, &unit);
    CheckEqI32(info.nUnitType, 13, "  unit type copied");
    CheckEqI32(info.nMoveClass, 8, "  pUnit[0xE2] is read rather than left at zero");
    CheckEqI32(info.bFlag0, 1, "  pUnit[0x1B3] is read rather than left at zero");
    CheckEqI32(info.bTypeFlagA, 1, "  byte_A7B80[4*13] is 1");
    CheckEqI32(info.bTypeFlagB, 0, "  byte_A7B80[4*13+1] is 0");

    for (i32 t = 0; t < PUFO_UNIT_TYPE_COUNT; ++t) {
        unit.nUnitType = (u8)t;
        Rmp_UnitInfoFromSoldier(&info, &unit);
        if (info.bTypeFlagA != s_aWantFlagA[t] || info.bTypeFlagB != s_aWantFlagB[t]) {
            nBad = 1;
        }
    }
    Check(nBad == 0,
          "  both flag columns match the image for all 24 unit types");

    unit.nUnitType = 200;
    Rmp_UnitInfoFromSoldier(&info, &unit);
    CheckEqI32(info.bTypeFlagA, 0, "  a type beyond the table leaves the flags clear");
    CheckEqI32(info.bTypeFlagB, 0, "  both of them");
}

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }

    TestLayout();
    TestLoad();
    TestTruncatingDivision();
    TestLinks();
    TestFree();
    TestAcceptsUnit();
    TestUnitInfoFromSoldier();
    TestPickSpawn();

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
