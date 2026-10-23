/* test_mapcell.c - checks the tactical map cell and map geometry.
 *
 * The flag-masking behaviour and the list-insert ordering are the parts most
 * likely to be "tidied" by a later reader, so they are asserted explicitly.
 */
#include "game/pufo_mapcell.h"

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

static void TestLayout(void)
{
    printf("Struct layout\n");
    Check(sizeof(Mc1Rec) == 52, "  Mc1Rec is 52 bytes");
    /* Offsets the cell code depends on. */
    {
        Mc1Rec rec;

        memset(&rec, 0, sizeof(rec));
        Check((u8 *)&rec.bTransparent - (u8 *)&rec == 0x20, "  bTransparent at 0x20");
        Check((u8 *)&rec.bBlocksMove - (u8 *)&rec == 0x2a, "  bBlocksMove at 0x2a");
        Check((u8 *)&rec.bBlocksSight - (u8 *)&rec == 0x2e, "  bBlocksSight at 0x2e");
        Check((u8 *)&rec.nSlot - (u8 *)&rec == 0x26, "  nSlot at 0x26");
        Check((u8 *)&rec.nGlobalIdx - (u8 *)&rec == 0x28, "  nGlobalIdx at 0x28");
    }
    {
        MapCell cell;

        memset(&cell, 0, sizeof(cell));
        /* MapCell holds pointers, so absolute offsets are only 0x18.. on ILP32.
         * On Win64 every pointer doubles, which shifts the tail by 16 bytes.
         * Assert the ILP32 offsets only on ILP32 targets. */
#if UINTPTR_MAX == 0xFFFFFFFFu
        Check((u8 *)&cell.nX - (u8 *)&cell == 0x1a, "  MapCell.nX at 0x1a");
        Check((u8 *)&cell.nY - (u8 *)&cell == 0x1b, "  MapCell.nY at 0x1b");
        Check((u8 *)&cell.nZ - (u8 *)&cell == 0x1c, "  MapCell.nZ at 0x1c");
        Check((u8 *)&cell.bFlags29 - (u8 *)&cell == 0x1d, "  MapCell.bFlags29 at 0x1d");
        Check((u8 *)&cell.nFlags32 - (u8 *)&cell == 0x20, "  MapCell.nFlags32 at 0x20");
        Check((u8 *)&cell.bFlags36 - (u8 *)&cell == 0x24, "  MapCell.bFlags36 at 0x24");
        Check((u8 *)&cell.nFlags40 - (u8 *)&cell == 0x28, "  MapCell.nFlags40 at 0x28");
        Check((u8 *)&cell.bFlags44 - (u8 *)&cell == 0x2c, "  MapCell.bFlags44 at 0x2c");
        Check((u8 *)&cell.bFlags57 - (u8 *)&cell == 0x39, "  MapCell.bFlags57 at 0x39");
        Check((u8 *)&cell.pUnit - (u8 *)&cell == 0x30, "  MapCell.pUnit at 0x30");
#else
        /* The non-pointer run from nX to bFlags44 has the same relative layout
         * on every target. These are the relations the code actually relies on. */
        Check((u8 *)&cell.nY - (u8 *)&cell.nX == 1, "  nY immediately follows nX");
        Check((u8 *)&cell.nZ - (u8 *)&cell.nX == 2, "  nZ immediately follows nY");
        Check((u8 *)&cell.bFlags29 - (u8 *)&cell.nX == 3, "  bFlags29 at nX+3");
        Check((u8 *)&cell.nFlags32 - (u8 *)&cell.nX == 6, "  nFlags32 at nX+6 (after 2-byte gap)");
        Check((u8 *)&cell.bFlags36 - (u8 *)&cell.nX == 10, "  bFlags36 at nX+10");
        Check((u8 *)&cell.nFlags40 - (u8 *)&cell.nX == 14, "  nFlags40 at nX+14 (after 3-byte gap)");
        Check((u8 *)&cell.bFlags44 - (u8 *)&cell.nX == 18, "  bFlags44 at nX+18 (after 3-byte gap)");
#endif
    }
}

/* Init preserves 0x88 of bFlags29 and clears the rest, sets coordinates, and
 * leaves the sprite pointers NULL. Verified against MapCell_Init @ 0x54944. */
static void TestInit(void)
{
    MapCell cell;
    u8     *pBytes = (u8 *)&cell;

    printf("MapCell_Init\n");
    memset(&cell, 0xFF, sizeof(cell));
    MapCell_Init(&cell, 3, 4, 5);
    CheckEqI32(cell.nX, 3, "  nX set");
    CheckEqI32(cell.nY, 4, "  nY set");
    CheckEqI32(cell.nZ, 5, "  nZ set");
    Check(cell.pFloorSpr == NULL, "  pFloorSpr cleared");
    Check(cell.pCeilSpr == NULL, "  pCeilSpr cleared");
    /* 0xFF & 0x88 == 0x88: only bits 3 and 7 survive. */
    CheckEqI32(cell.bFlags29, 0x88, "  bFlags29 masked to 0x88");
    CheckEqI32(cell.bFlags36, 0xFE, "  bFlags36 bit0 cleared");
    CheckEqI32(cell.bFlags44, 0xFE, "  bFlags44 bit0 cleared");
    CheckEqI32((i32)cell.nFlags32, (i32)0xFFFFFF80u, "  nFlags32 low 7 bits cleared");
    CheckEqI32((i32)cell.nFlags40, (i32)0xFFFFFF80u, "  nFlags40 low 7 bits cleared");
    CheckEqI32(cell.abFaceDamage[0], 0, "  abFaceDamage[0] cleared");
    CheckEqI32(cell.nPathCost, 0, "  nPathCost cleared");
    (void)pBytes;
}

/* Reset differs from Init: it clears the occupant and item list and preserves
 * a different set of bits. */
static void TestReset(void)
{
    MapCell cell;

    printf("MapCell_Reset\n");
    memset(&cell, 0xFF, sizeof(cell));
    MapCell_Reset(&cell);
    Check(cell.pUnit == NULL, "  occupant cleared");
    Check(cell.pItemList == NULL, "  item list cleared");
    CheckEqI32(cell.bFlags44, 0xFE, "  bFlags44 bit0 cleared");
    /* bFlags57[0] is masked with 0xF3 only. The ~0x20 in MapCell_Reset applies
     * to bFlags29, not to this field. 0xFF & 0xF3 == 0xF3. */
    CheckEqI32(cell.bFlags57[0], 0xF3, "  MapCell_Reset @ 0x54a90 masks bFlags57[0] to 0xF3 (the 0x20 mask is for bFlags29)");
    CheckEqI32(cell.bFlags57[1], 0, "  bFlags57[1] cleared");
    /* 0xFF & 0xAF == 0xAF, then & ~0x20 == 0x8F */
    CheckEqI32(cell.bFlags29, 0x8F, "  bFlags29 masked to 0x8F");
    CheckEqI32((i32)cell.nFlags32, (i32)0xFFFFFFF8u, "  nFlags32 low 3 bits cleared");
    CheckEqI32((i32)cell.nFlags40, (i32)0xFFFFFFF8u, "  nFlags40 low 3 bits cleared");
    CheckEqI32(cell.bFlags36, 0xFE, "  bFlags36 bit0 cleared");
}

static void TestTileSprites(void)
{
    MapCell cell;
    Mc1Rec  aFloor;
    Mc1Rec  aWallA;
    Mc1Rec  aWallB;
    Mc1Rec  aCeil;

    printf("MapCell_SetTileSprites\n");
    memset(&cell, 0, sizeof(cell));
    memset(&aFloor, 0, sizeof(aFloor));
    memset(&aWallA, 0, sizeof(aWallA));
    memset(&aWallB, 0, sizeof(aWallB));
    memset(&aCeil, 0, sizeof(aCeil));
    cell.pUnit    = (Soldier *)&cell;
    cell.nFlags32 = 0xFFFFFFFFu;
    cell.nFlags40 = 0xFFFFFFFFu;
    cell.bFlags36 = 0xFF;
    cell.bFlags44 = 0xFF;
    cell.bFlags29 = 0xFF;

    MapCell_SetTileSprites(&cell, &aFloor, &aWallA, &aWallB, &aCeil);
    Check(cell.pFloorSpr == &aFloor, "  floor sprite stored");
    Check(cell.pWallSprA == &aWallA, "  wall A stored");
    Check(cell.pWallSprB == &aWallB, "  wall B stored");
    Check(cell.pCeilSpr == &aCeil, "  ceiling stored");
    Check(cell.pUnit == NULL, "  occupant cleared");
    CheckEqI32((i32)cell.nFlags32, (i32)0xFFFFFF80u, "  nFlags32 low 7 cleared");
    CheckEqI32((i32)cell.nFlags40, (i32)0xFFFFFF80u, "  nFlags40 low 7 cleared");
    CheckEqI32(cell.bFlags36, 0xFE, "  bFlags36 bit0 cleared");
    CheckEqI32(cell.bFlags44, 0xFE, "  bFlags44 bit0 cleared");
    CheckEqI32(cell.bFlags29, 0xDF, "  bFlags29 bit5 cleared");

    /* NULL arguments must leave the existing sprite in place, not clear it. */
    printf("MapCell_SetTileSprites with NULLs keeps sprites\n");
    MapCell_SetTileSprites(&cell, NULL, NULL, NULL, NULL);
    Check(cell.pFloorSpr == &aFloor, "  MapCell_SetTileSprites @ 0x548d0 keeps the floor sprite for a NULL argument");
    Check(cell.pCeilSpr == &aCeil, "  ceiling kept");
}

static void TestFlags(void)
{
    MapCell cell;
    Mc1Rec  aRec;

    printf("MapCell_TestFlag\n");
    memset(&cell, 0, sizeof(cell));
    memset(&aRec, 0, sizeof(aRec));

    Check(!MapCell_TestFlag(&cell, CELLFLAG_IS_BLANK), "  empty cell is not blank");
    Check(!MapCell_TestFlag(&cell, CELLFLAG_HAS_FLOOR), "  empty cell has no floor");

    aRec.bTransparent = 255;
    cell.pFloorSpr = &aRec;
    Check(MapCell_TestFlag(&cell, CELLFLAG_HAS_FLOOR), "  floor present");
    Check(MapCell_TestFlag(&cell, CELLFLAG_IS_BLANK), "  transparent floor counts as blank");

    /* BlocksMove returns true when the byte is non-zero. */
    aRec.bBlocksMove = 1;
    cell.pWallSprA = &aRec;
    Check(MapCell_TestFlag(&cell, CELLFLAG_WALL_A_BLOCKS), "  wall A blocks when set");
    aRec.bBlocksMove = 0;
    Check(!MapCell_TestFlag(&cell, CELLFLAG_WALL_A_BLOCKS), "  wall A does not block when clear");

    /* A missing wall must report "does not block" rather than crashing. */
    cell.pWallSprB = NULL;
    Check(!MapCell_TestFlag(&cell, CELLFLAG_WALL_B_BLOCKS), "  absent wall B does not block");
    Check(!MapCell_TestFlag(&cell, CELLFLAG_WALL_B_BLANK), "  absent wall B is not blank");

    aRec.bBlocksSight = 3;
    Check(MapCell_TestFlag(&cell, CELLFLAG_FLOOR_BLOCKS), "  floor blocks sight when set");

    printf("MapCell_SetBFlags57_3 / SetBFlags29_4\n");
    cell.bFlags57[0] = 0;
    MapCell_SetBFlags57_3(&cell, true);
    CheckEqI32(cell.bFlags57[0], 8, "  MapCell_SetBFlags57_3 @ 0x548a0 sets bit3 (& 0xF7 | 8*bSet)");
    MapCell_SetBFlags57_3(&cell, false);
    CheckEqI32(cell.bFlags57[0], 0, "  bit3 cleared");
    cell.bFlags29 = 0;
    MapCell_SetBFlags29_4(&cell);
    CheckEqI32(cell.bFlags29, 0x10, "  MapCell_SetBFlags29_4 @ 0x548c0 sets bit4");
}

static void TestOccupant(void)
{
    MapCell cell;
    u8      aFakeSoldier[64];
    u8      aDeadSoldier[64];

    printf("MapCell_HasOccupant\n");
    memset(&cell, 0, sizeof(cell));
    memset(aFakeSoldier, 0, sizeof(aFakeSoldier));
    memset(aDeadSoldier, 0, sizeof(aDeadSoldier));

    Check(!MapCell_HasOccupant(&cell), "  empty cell");
    /* Soldier.bStatus is at offset 0x17, proven from the MapCell_HasOccupant
     * disassembly (LDRB R3, [R0,#0x17]). Put the byte there, not at 0. */
    aFakeSoldier[PUFO_SOLDIER_BSTATUS_OFF] = 0; /* status 0 = alive */
    cell.pUnit = (Soldier *)aFakeSoldier;
    Check(MapCell_HasOccupant(&cell), "  status 0 occupies");
    aFakeSoldier[PUFO_SOLDIER_BSTATUS_OFF] = 2;
    Check(MapCell_HasOccupant(&cell), "  status 2 occupies");
    aDeadSoldier[PUFO_SOLDIER_BSTATUS_OFF] = 3;
    cell.pUnit = (Soldier *)aDeadSoldier;
    Check(!MapCell_HasOccupant(&cell), "  MapCell_HasOccupant @ 0x5487c: status 3 does not occupy (the test is bStatus < 3)");
    /* A byte at offset 0 must NOT be read as the status. */
    memset(aFakeSoldier, 0, sizeof(aFakeSoldier));
    aFakeSoldier[0] = 9; /* garbage where the old, wrong code read */
    aFakeSoldier[PUFO_SOLDIER_BSTATUS_OFF] = 0;
    cell.pUnit = (Soldier *)aFakeSoldier;
    Check(MapCell_HasOccupant(&cell), "  offset 0 is not the status byte");
}

/* The insert is a two-position splice, not a walk. Asserting the real shape
 * documents the oddity instead of letting a later reader "fix" it. */
static void TestItemList(void)
{
    MapCell   cell;
    TactItem  aItems[3];
    ObjectRec aObjs[3];

    printf("MapCell_ListInsertByPri\n");
    memset(&cell, 0, sizeof(cell));
    memset(aItems, 0, sizeof(aItems));
    memset(aObjs, 0, sizeof(aObjs));
    for (int i = 0; i < 3; ++i) {
        aItems[i].pObj = &aObjs[i];
    }
    aObjs[0].bListPri = 5;
    aObjs[1].bListPri = 1;
    aObjs[2].bListPri = 9;

    MapCell_ListInsertByPri(&cell, &aItems[0]); /* pri 5 -> head */
    Check(cell.pItemList == &aItems[0], "  first insert becomes head");

    MapCell_ListInsertByPri(&cell, &aItems[1]); /* pri 1; head 5 >= 1 -> spliced after head */
    Check(cell.pItemList == &aItems[0], "  MapCell_ListInsertByPri @ 0x54b14 splices after the head when head priority >= new");
    Check(aItems[0].pNext == &aItems[1], "  head now points at the new node");

    MapCell_ListInsertByPri(&cell, &aItems[2]); /* pri 9; head 5 < 9 -> new head */
    Check(cell.pItemList == &aItems[2], "  higher priority becomes new head");
    Check(aItems[2].pNext == &aItems[0], "  new head points at previous head");

    printf("MapCell_ListRemove\n");
    MapCell_ListRemove(&cell, &aItems[2]);
    Check(cell.pItemList == &aItems[0], "  MapCell_ListRemove @ 0x54b58 removing the head advances the list");
    MapCell_ListRemove(&cell, &aItems[0]);
    Check(cell.pItemList == &aItems[1], "  removing new head advances again");

    /* The walk branch, which the two head cases never reach. The original stops when the
     * successor is the node or when the successor is NULL, and the relink writes the node's
     * own successor -- so removing the tail clears the link instead of leaving it dangling. */
    aItems[0].pNext = &aItems[1];
    aItems[1].pNext = &aItems[2];
    aItems[2].pNext = NULL;
    cell.pItemList  = &aItems[0];
    MapCell_ListRemove(&cell, &aItems[1]);
    Check(cell.pItemList == &aItems[0] && aItems[0].pNext == &aItems[2],
          "  the walk relinks a middle node and leaves the head in place");
    MapCell_ListRemove(&cell, &aItems[2]);
    Check(cell.pItemList == &aItems[0] && aItems[0].pNext == NULL, "  removing the tail clears the link");
    MapCell_ListRemove(&cell, &aItems[1]);
    Check(cell.pItemList == &aItems[0] && aItems[0].pNext == NULL, "  a node that is not in the list changes nothing");
}

/* TactItem_ctor @ 0x3e718 clears exactly FOUR fields and leaves the rest alone.
 * That partial clear is the point: a caller must not assume a zeroed struct. */
static void TestItemCtor(void)
{
    TactItem item;
    ObjectRec obj;

    printf("TactItem_ctor\n");
    memset(&item, 0xAA, sizeof(item));
    memset(&obj, 0xAA, sizeof(obj));
    item.pObj = &obj;
    item.nIndex = 0xBEEF;
    item.nAmmo = 0x55;
    item.nRefId = 0x0BADF00D;

    TactItem_ctor(&item);

    /* The four cleared fields. */
    Check(item.pNext == NULL, "  pNext cleared");
    CheckEqI32(item.bUnk7, 0, "  bUnk7 cleared");
    CheckEqI32(item.bUnk8, 0, "  bUnk8 cleared");
    Check(item.pLinkedSoldier == NULL, "  pLinkedSoldier cleared");

    /* The fields it deliberately does NOT touch. */
    Check(item.pObj == &obj, "  pObj left alone");
    CheckEqI32(item.nIndex, 0xBEEF, "  nIndex left alone");
    CheckEqI32(item.nAmmo, 0x55, "  nAmmo left alone");
    CheckEqI32(item.nRefId, (i32)0x0BADF00D, "  nRefId left alone");

    TactItem_ctor(NULL);
    Check(true, "  NULL item is safe");
}

static void TestGeometry(void)
{
    MapGeom map;

    printf("Map_IsValidCell / Map_CellIndex\n");
    map.nDimX = 4;
    map.nDimY = 3;

    Check(!Map_IsValidCell(&map, -1, 0, 0), "  negative X rejected");
    Check(!Map_IsValidCell(&map, 0, -1, 0), "  negative Y rejected");
    Check(Map_IsValidCell(&map, 0, 0, 0), "  origin valid");
    Check(Map_IsValidCell(&map, 39, 29, 3), "  last cell valid");
    Check(!Map_IsValidCell(&map, 40, 0, 0), "  X == 10*dimX rejected");
    Check(!Map_IsValidCell(&map, 0, 30, 0), "  Y == 10*dimY rejected");
    Check(!Map_IsValidCell(&map, 0, 0, -1), "  negative layer rejected");
    Check(!Map_IsValidCell(&map, 0, 0, 4), "  layer 4 rejected");
    Check(Map_IsValidCell(&map, 0, 0, 3), "  layer 3 accepted");

    /* [layer][y][x] with 10*dimY rows of 10*dimX cells per layer. */
    CheckEqI32((i32)Map_CellIndex(4, 3, 0, 0, 0), 0, "  layer 0 origin");
    CheckEqI32((i32)Map_CellIndex(4, 3, 1, 0, 0), 1, "  +1 in X");
    CheckEqI32((i32)Map_CellIndex(4, 3, 0, 1, 0), 40, "  +1 in Y strides by row width");
    CheckEqI32((i32)Map_CellIndex(4, 3, 0, 0, 1), 1200, "  +1 in layer strides by layer size");
}

static void TestIsometric(void)
{
    printf("Isometric helpers\n");
    /* Map_IsoOffsetA: a - 5*dimX + b */
    CheckEqI32(Map_IsoOffsetA(4, 10, 2), 10 - 20 + 2, "  Map_IsoOffsetA @ 0x26f88 is a - 5*dimX + b");
    /* Map_IsoOffsetB: 5*dimY - a + b - 1 (note the -1) */
    CheckEqI32(Map_IsoOffsetB(3, 10, 2), 15 - 10 + 2 - 1, "  Map_IsoOffsetB @ 0x26fa8 is 5*dimY - a + b - 1");
    /* Map_CenterOffsetX: 5*dimX - (a+b+1)/2 + a */
    CheckEqI32(Map_CenterOffsetX(4, 10, 4), 20 - 7 + 10, "  CenterOffsetX");
    /* Map_CenterOffsetY takes no dimension; (a+b+1)/2 */
    CheckEqI32(Map_CenterOffsetY(10, 4), 7, "  CenterOffsetY");
    CheckEqI32(Map_CenterOffsetY(0, 0), 0, "  CenterOffsetY at origin round-trips");
    /* Integer division truncates toward zero, matching ARM sdiv. */
    CheckEqI32(Map_CenterOffsetY(1, 1), 1, "  CenterOffsetY odd sum truncates");
}


/* --- Map_RefreshCellMeta @ 0x2c7f4 --- */

#define LM_DIM 23

typedef struct MetaGrid {
    MapCell aCells[PUFO_MAP_LAYERS][30][30];
    Mc1Rec  aSprites[30][30][4];
    u8      aLight[LM_DIM * LM_DIM];
} MetaGrid;

static MapCell *MetaGrid_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    MetaGrid *pG = (MetaGrid *)pUser;

    if (nX < 0 || nY < 0 || nX >= 30 || nY >= 30) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return &pG->aCells[nLayer][nY][nX];
}

static void MetaGrid_Init(MetaGrid *pG)
{
    memset(pG, 0, sizeof(*pG));
    for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (i32 y = 0; y < 30; ++y) {
            for (i32 x = 0; x < 30; ++x) {
                MapCell_Init(&pG->aCells[z][y][x], x, y, z);
            }
        }
    }
}

static void TestRefreshCellMeta(void)
{
    static MetaGrid grid;
    MapGeom          map;

    printf("Map_RefreshCellMeta\n");
    map.nDimX = 3;
    map.nDimY = 3;

    /* The gate: disabled means nothing is touched at all. */
    MetaGrid_Init(&grid);
    grid.aCells[0][0][0].bUnk56 = 0x42;
    Check(!Map_RefreshCellMeta(MetaGrid_Lookup, &grid, &map, false, grid.aLight),
          "  disabled light pass does nothing");
    CheckEqI32(grid.aCells[0][0][0].bUnk56, 0x42, "  cell untouched when disabled");

    /* Enabled with an empty lightmap and no sprites: every cell becomes 0xFF
     * because no source casts anything. */
    MetaGrid_Init(&grid);
    grid.aCells[0][0][0].bUnk56 = 0;
    Check(Map_RefreshCellMeta(MetaGrid_Lookup, &grid, &map, true, grid.aLight),
          "  enabled pass runs");
    CheckEqI32(grid.aCells[0][0][0].bUnk56, 0xFF, "  no casters leaves 0xFF");

    /* A single occluder with height 10 and an all-zero lightmap sets its
     * footprint to 10 on every layer. */
    MetaGrid_Init(&grid);
    grid.aSprites[3][3][0].bHeight = 10;
    grid.aCells[0][3][3].pFloorSpr = &grid.aSprites[3][3][0];
    (void)Map_RefreshCellMeta(MetaGrid_Lookup, &grid, &map, true, grid.aLight);
    CheckEqI32(grid.aCells[0][3][3].bUnk56, 10, "  occluder cell is 10");
    CheckEqI32(grid.aCells[0][3][0].bUnk56, 10, "  x-3 is covered (10)");
    CheckEqI32(grid.aCells[1][3][3].bUnk56, 10, "  the same value lands on layer 1");
    CheckEqI32(grid.aCells[3][4][4].bUnk56, 10, "  and on layer 3");

    /* Outside the footprint the value stays 0xFF. dx spans -11..11 and
     * dy spans 0..22, so a destination at y = 3-12 is out of range. */
    CheckEqI32(grid.aCells[0][20][3].bUnk56, 0xFF, "  far cell remains 0xFF");

    /* The minimum wins: a nearer tall occluder cannot raise an already-dark cell.
     * The lightmap adds to h, so a bright map value raises the result. */
    MetaGrid_Init(&grid);
    grid.aSprites[3][3][0].bHeight = 30;
    grid.aCells[0][3][3].pFloorSpr = &grid.aSprites[3][3][0];
    /* Centre of the lightmap is index 11*23+11 = 264 and is 0 by default. */
    (void)Map_RefreshCellMeta(MetaGrid_Lookup, &grid, &map, true, grid.aLight);
    CheckEqI32(grid.aCells[0][3][3].bUnk56, 30, "  taller occluder sets 30");

    /* The nFlags40 animated-tile field forces the minimum contribution of 1. */
    printf("Map_RefreshCellMeta animated-tile flag\n");
    MetaGrid_Init(&grid);
    grid.aCells[0][3][3].nFlags40 = 0x08; /* inside the 0x78 mask */
    (void)Map_RefreshCellMeta(MetaGrid_Lookup, &grid, &map, true, grid.aLight);
    CheckEqI32(grid.aCells[0][3][3].bUnk56, 1, "  flagged cell contributes 1");

    /* A NULL lightmap or lookup must be refused, not crash. */
    Check(!Map_RefreshCellMeta(NULL, &grid, &map, true, grid.aLight), "  NULL lookup refused");
    Check(!Map_RefreshCellMeta(MetaGrid_Lookup, &grid, &map, true, NULL), "  NULL lightmap refused");
    Check(!Map_RefreshCellMeta(MetaGrid_Lookup, &grid, NULL, true, grid.aLight), "  NULL map refused");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestLayout();
    TestInit();
    TestReset();
    TestTileSprites();
    TestFlags();
    TestOccupant();
    TestItemList();
    TestItemCtor();
    TestGeometry();
    TestIsometric();
    TestRefreshCellMeta();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
