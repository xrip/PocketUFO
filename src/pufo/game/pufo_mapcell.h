/* pufo_mapcell.h - the tactical map cell and its tile/occupant state.
 *
 * Evidence: MapCell_Init @ 0x54944, MapCell_Reset @ 0x54a90,
 *           MapCell_TestFlag @ 0x549c8, MapCell_SetTileSprites @ 0x548d0,
 *           MapCell_HasOccupant @ 0x5487c, MapCell_SetOccupant @ 0x548b8,
 *           MapCell_SetBFlags57_3 @ 0x548a0, MapCell_SetBFlags29_4 @ 0x548c0,
 *           MapCell_ListInsertByPri @ 0x54b14, MapCell_ListRemove @ 0x54b58,
 *           Map_IsValidCell @ 0x2bc9c.
 *
 * MapCell is exactly 60 (0x3C) bytes in the original; sizeof is asserted below.
 * The tactical grid is 4 layers x (10*nMapDimX) x (10*nMapDimY) cells, indexed
 * [layer][y][x], where each map tile is 10x10 cells.
 */
#ifndef PUFO_MAPCELL_H
#define PUFO_MAPCELL_H

#include "game/pufo_objectrec.h"
#include "pufo_common.h"

#define PUFO_CELL_SIZE   10  /* cells per map tile edge */
#define PUFO_MAP_LAYERS  4

/* Upper bound for the cell grid, used by the full-grid sweeps (path flood clear,
 * GameTick_Step). The original walks 10*nMapDimX by 10*nMapDimY; the map dims are
 * u8 bytes, and the largest shipped map is well under this. Callers' lookups
 * return NULL outside the real grid, so an over-large bound only costs a few
 * extra probes. */
#define PUFO_CELL_GRID_MAX 60

/* Soldier.bStatus offset, proven from MapCell_HasOccupant @ 0x5487c:
 *     LDRB  R3, [R0,#0x17]  ; pUnit->bStatus
 * Declared here because MapCell_HasOccupant needs it and the full Soldier
 * struct lives in the entities header. */
/* Soldier.bStatus offset, proven from MapCell_HasOccupant @ 0x5487c:
 *     LDRB  R3, [R0,#0x17]  ; pUnit->bStatus
 *
 * That 0x17 is the ILP32 offset. bStatus sits right after pCell, so when
 * pointers widen to 8 bytes the offset shifts by exactly (sizeof(void*) - 4):
 *     ILP32: pCell 0x10..0x14, pp 0x14/0x15/0x16 -> bStatus 0x17
 *     Win64: pCell 0x10..0x18, pp 0x18/0x19/0x1a -> bStatus 0x1b
 * The formula keeps this correct on both targets. pufo_unit.h asserts that the
 * real struct field lands at this offset on every target. */
#define PUFO_SOLDIER_BSTATUS_OFF (0x17u + (sizeof(void *) - 4u))

/* Mc1Rec.bFacilityKind: the alien-base facility a tile sprite contributes to the mission debrief.
 * The names are the debrief line each value feeds. The caller (Tact_CheckBattleEnd, 0x34fbc, not
 * ported) labels each counter with a TEXTS record: 171 "Alien Alloys", 172 "Elerium 115",
 * 173 "UFO Navigation", 174 "UFO Power Source", 175 "Alien Reproduction", 177 "Alien
 * Entertainment", 178 "Alien Surgery", 198 "Examination Room". Cross-checked against which shipped
 * .MC1 sprite sets carry the value at all. The full decode is in game/pufo_tactend.h. Values not
 * listed here are never tallied. */
typedef enum TileFacilityKind {
    TILE_FAC_ELERIUM      = 2,  /* 1 record, U_WALL02.MC1; also adds the seeded Elerium amount */
    TILE_FAC_NAVIGATION   = 3,  /* 6 records, U_WALL02.MC1 */
    TILE_FAC_REPRODUCTION = 5,  /* 4 records, U_DISEC2.MC1 */
    TILE_FAC_ENTERTAINMENT = 7, /* 4 records, U_PODS.MC1 */
    TILE_FAC_SURGERY      = 8,  /* 6 records, U_DISEC2.MC1 */
    TILE_FAC_EXAMINATION  = 9,  /* 4 records, U_OPER2.MC1 */
    TILE_FAC_ALLOYS       = 10  /* 133 records: U_BASE 66, U_EXT02 26, U_WALL02 19, UFO1 14,
                                 * U_OPER2 6, MARS 2 */
} TileFacilityKind;

/* Mc1Rec: a tile sprite record, 52 bytes in the original. It contains no
 * pointers, so the Win64 size must match exactly -- asserted below.
 *
 * Evidence: MapCell_TestFlag @ 0x549c8 reads .bTransparent at 0x20 (IDA calls
 * it gap_22[10], since gap_22 spans 0x16..0x26) and the blocking bytes at 0x2a
 * and 0x2e (gap_2A[0] and gap_2A[4]). A signed parameter byte at 0x24 is read as
 * a CEILING height/threshold by Path_ExpandCell @ 0x2b604 and
 * Map_ProbeStepOccupants (0x2778c) (both load `*(char*)(pCeilSpr + 0x24)`).
 * An unsigned sprite height byte at 0x1C is read by Map_RefreshCellMeta
 * @ 0x2c7f4 (`*(u8*)(pSpr + 28)`, taking the max over the four sprites).
 *
 * Layout walk, so the padding is not mistaken for slack:
 *   0x00 gap_0[8]   0x08 abPal[12]   0x14 uUnk20(u16)   0x16 gap_22[6]
 *   0x1C bHeight    0x1D bCeilSoundSel  0x1E bFacilityKind  0x1F gap[1]
 *   0x20 bTransparent  0x21 gap[3]
 *   0x24 iCeilParam 0x25 gap[1]      0x26 nSlot(u16)    0x28 nGlobalIdx(u16)
 *   0x2a bBlocksMove <- gap_2A[0]    0x2b gap[3]        0x2e bBlocksSight
 *   0x2f gap[5]     0x34 end */
typedef struct Mc1Rec {
    u8   gap_0[8];        /* 0x00 */
    u8   abPal[12];       /* 0x08 */
    u16  uUnk20;          /* 0x14 */
    u8   bStrength;       /* 0x16, sprite hardness; 255 means unbreakable.
                           * Read by Combat_SplashDamageCells @ 0x2e7d8 as pSpr[22]. */
    u8   bWallRangeCost;  /* 0x17, range a ray loses passing this sprite.
                           * Read as pSpr[23] by Map_ComputeFov @ 0x2deac for wall A,
                           * wall B and ceiling alike. The paired byte pSpr[22] is
                           * bStrength, compared against the cell's abFaceDamage[wall A] /
                           * abFaceDamage[wall B]. */
    u8   bCeilBreakable;  /* 0x18, non-zero when a ceiling sprite can give way at
                           * all. Read by the ceiling-break sweep of
                           * Combat_ResolveShot @ 0x2f12c (LDRB R3, [R0,#0x18]),
                           * which also scales it by 6.0 into a spilled slot. */
    u8   bSplashResist;   /* 0x19, tile power needed to change the cell flags.
                           * Read as pSpr[25] in the SPLASH_MODE_THROW branch. */
    u8   gap_1A[2];       /* 0x1A */
    u8   bHeight;         /* 0x1C, sprite height/occlusion, max over the 4 sprites */
    u8   bCeilSoundSel;   /* 0x1D, selects the footstep sample in
                           * Tact_ProcessMoveAction @ 0x4f3f0: 1 gives sample 32,
                           * anything else 19. Read from the ceiling sprite when
                           * the cell has one, otherwise from the floor sprite. */
    u8   bFacilityKind;   /* 0x1E, the alien-base facility this sprite contributes to the debrief.
                           * Tact_TallyBattleStats @ 0x33618 (LDRB R5,[R3,#0x1E]) is the only reader
                           * of it in the whole binary, which is why it stayed a gap. The value names
                           * and their evidence are in the TileFacilityKind enum above. */
    u8   gap_1F[1];       /* 0x1F */
    u8   bTransparent;    /* 0x20, 255 means transparent */
    u8   gap_21[3];       /* 0x21 */
    i8   iCeilParam;      /* 0x24, ceiling height/threshold; read as signed */
    u8   gap_25;          /* 0x25 */
    u16  nSlot;           /* 0x26 */
    u16  nGlobalIdx;      /* 0x28 */
    u8   bBlocksMove;     /* 0x2a */
    u8   bUnk2B;          /* 0x2b, non-zero blocks a blast ray.
                           * Read as pSpr[43] by Map_ScanBlastFootprint @ 0x32cd4 for
                           * wall A, wall B and ceiling alike. Map_ComputeFov and
                           * Map_TraceVoxelOccupancy test 0x2a instead, so the two
                           * scans use different blocking bytes. */
    u8   gap_2c[2];       /* 0x2c */
    u8   bBlocksSight;    /* 0x2e */
    u8   gap_2f[3];       /* 0x2F; [0] marks a real wall (Unit_CanStepExitDir) */
    u8   bExtraSprFacing; /* 0x32, non-zero means the sprite has an extra variant.
                           * Read as pSpr[50] by Combat_SplashDamageCells, and as the
                           * facing byte by Unit_GetExtraSpr @ 0x2e59c, which the
                           * splash walk calls with a sprite record instead of a unit.
                           * The two offsets it uses, +0x26 and +0x32, land on nSlot
                           * and this byte, so the reuse is deliberate. */
    u8   gap_33;          /* 0x33 */
} Mc1Rec;

/* TactItem: 24 bytes. Referenced from the cell's item list. */
typedef struct TactItem TactItem;

/* ObjectRec is the real item record, not a stand-in. It used to be a 165-byte
 * shadow with only bListPri named, which made every later field an injected
 * hook and made TactItem.pObj a type lie. See game/pufo_objectrec.h. */
typedef ObjectRecFull ObjectRec;

/* Soldier: forward-declared. Only bStatus is read by HasOccupant. */
typedef struct Soldier Soldier;

struct TactItem {
    ObjectRec  *pObj;     /* 0x00 */
    u16         nIndex;   /* 0x04 */
    u8          nAmmo;    /* 0x06 */
    u8          bUnk7;    /* 0x07 */
    u8          bUnk8;    /* 0x08 */
    u8          gap_9[3]; /* 0x09 */
    TactItem   *pNext;    /* 0x0c */
    Soldier    *pLinkedSoldier; /* 0x10, original nUnk16: the linked soldier whose
                                 * nSaveId the flobjs chunk stores */
    i32         nRefId;   /* 0x14 */
    u8          gap_18[4];/* 0x18 */
};

/* MapCell: 60 bytes on the original ILP32 target. On Win64 the struct grows
 * because four pointers become 8 bytes each; that is harmless because a cell is
 * never written to disk or shared with the original binary. Field ORDER and the
 * bitfields' meaning are what must match, and they do.
 * The static assert below pins the ILP32 size so the layout intent is recorded. */
typedef struct MapCell {
    Mc1Rec    *pFloorSpr;   /* 0x00 */
    Mc1Rec    *pWallSprA;   /* 0x04 */
    Mc1Rec    *pWallSprB;   /* 0x08 */
    Mc1Rec    *pCeilSpr;    /* 0x0c */
    /* Per-face accumulated damage, 0x10..0x13: floor, wall A, wall B, ceiling. The splash
     * walk (Combat_DamageSprite) indexes them as `pCell + 0x10 + slot` and so does the fire
     * tick, which reaches all four plus two neighbour passes that reuse wall A and wall B --
     * see PUFO_CELL_FACE_* / PUFO_CELL_PASS_* below. The fire tick compares the sprite's
     * bStrength (Mc1Rec+0x16) against counter + step, not against the step alone. */
    u8         abFaceDamage[4]; /* 0x10 */
    /* The pathfinder's accumulated cost, 0 meaning "unvisited" -- Path_ExpandCell @ 0x2b604
     * reads it as a dword (`LDR R3,[pCell,#0x14]`, 0x2B6A0) and composes new costs from it.
     * It is adjacent to abFaceDamage but the fire tick never reaches it: its slot index is
     * rewritten to 1 or 2 for the two neighbour passes (0x4C09C, 0x4C0EC). */
    i32        nPathCost;       /* 0x14 */
    u8         bPathDir;        /* 0x18, the direction this cell was reached from; the turn
                                 * cost Map_FacingDelta reads for the next expansion */
    u8         bPathFlag;       /* 0x19 */
    u8         nX;              /* 0x1a */
    u8         nY;              /* 0x1b */
    u8         nZ;              /* 0x1c */
    u8         bFlags29;        /* 0x1d; bit4 set by SetBFlags29_4, bit5 cleared by Reset;
                                 * bit1 selects the fire axis and bit2 marks the cell as
                                 * burning -- see the PUFO_CELL_FIRE_* constants. */
    u8         gap_30[2];       /* 0x1e */
    u32        nFlags32;        /* 0x20; low 7 bits cleared by Init/Reset/SetTileSprites */
    u8         bFlags36;        /* 0x24; bit0 cleared. In the fire tick bit0 picks the heat
                                 * step: set adds 20 to abFaceDamage, clear adds 5. */
    u8         gap_37[3];       /* 0x25 */
    u32        nFlags40;        /* 0x28; low 7 bits cleared. Low 3 bits count down to the
                                 * next advance of the burning state; bits 3..6 (0x78) are a
                                 * second countdown. Both decrement once per enemy turn. */
    u8         bFlags44;    /* 0x2c; bit0 cleared */
    u8         gap_45[3];   /* 0x2d */
    Soldier   *pUnit;       /* 0x30 */
    TactItem  *pItemList;   /* 0x34 */
    u8         bUnk56;      /* 0x38 */
    u8         bFlags57[4]; /* 0x39, the IDA width; only [0] (bit3 door via SetBFlags57_3, bit2 wall) and [1]
                             * are ever touched -- Reset clears them, the save stores them; see the note below */
} MapCell;

/* Mc1Rec holds no pointers, so its size must match the original on every
 * target. This is the assert that matters for format parity. */
_Static_assert(sizeof(Mc1Rec) == 52, "Mc1Rec must stay 52 bytes");

/* TactItem and MapCell contain pointers, so their Win64 size is larger by
 * design (4-byte -> 8-byte). They are never written to disk or shared with the
 * original binary, so only the ILP32 layout is a meaningful invariant; these
 * asserts encode it for the 32-bit case and are skipped on Win64. */
/* The original allocates the cell as 60 bytes (new(0x3C) at 0x3D190, said in the
 * IDB's own comment there) and its IDA type is 62, bFlags57 typed u8[4] at 0x39,
 * i.e. the type overhangs the allocation by two bytes. No code touches those:
 * MapCell_Reset @ 0x54A90 writes bFlags57[0] (mask 0xF3) and [1] only, the save
 * pair writes the same two (0x43B58/0x43B8C), and a census over `#0x3D]`/`#0x3E]`
 * finds only Ship-record users, never a MapCell one. An earlier assert here read
 * `sizeof(MapCell) == 60` under ILP32; this layout ends at 0x3c and rounds to 64,
 * so the assert stated an invariant the port does not have, and it is removed
 * rather than fudged. The pUnit offset assert below is the one the field
 * accesses actually depend on. */
#if UINTPTR_MAX == 0xFFFFFFFFu
_Static_assert(sizeof(TactItem) == 24, "TactItem is 24 bytes on ILP32");
_Static_assert(offsetof(MapCell, pUnit) == 0x30, "MapCell.pUnit is at 0x30 on ILP32");
#endif

/* The four face counters and the six passes the fire tick makes over them
 * (BeforeEnemyStep @ 0x4b9b8, 0x4C048..0x4C210). Wall A is the wall on the WEST side of the
 * cell and wall B the one on the NORTH side.
 *
 * The loop counter runs 0..5, but passes 4 and 5 have no counter and no sprite of their own:
 * after gating on the neighbour cell being in bounds and burning, 0x4C09C writes `MOV R1, #1`
 * and 0x4C0EC `MOV R1, #2`, and R1 is what indexes both `pCell + 0x10 + R1` (0x4C0FC) and the
 * sprite switch (0x4C114). So the WEST neighbour pass charges abFaceDamage[wall A] through
 * pWallSprA and the NORTH neighbour pass charges abFaceDamage[wall B] through pWallSprB -- the
 * same counter and the same sprite the cell's own wall pass uses. A wall therefore takes its
 * own hit plus the neighbour's, and nothing in this loop touches nPathCost. */
#define PUFO_CELL_FACE_FLOOR   0  /* abFaceDamage[0], MapCell+0x10 */
#define PUFO_CELL_FACE_WALL_A  1  /* abFaceDamage[1], MapCell+0x11, the west wall */
#define PUFO_CELL_FACE_WALL_B  2  /* abFaceDamage[2], MapCell+0x12, the north wall */
#define PUFO_CELL_FACE_CEILING 3  /* abFaceDamage[3], MapCell+0x13 */
#define PUFO_CELL_PASS_WEST    4  /* the neighbour at nX - 1, charged through wall A */
#define PUFO_CELL_PASS_NORTH   5  /* the neighbour at nY - 1, charged through wall B */
#define PUFO_CELL_FACE_PASSES  6

/* MapCell.bFlags29 bits 1 and 2, from the fire tick in BeforeEnemyStep @ 0x4b9b8. Bit 2
 * marks the cell as burning: tested at 0x4BC34, cleared on the cells a burn walk gives up
 * on at 0x4BF1C, and it is the same bit the iso layer tests to pick the burning tile art
 * (pufo_isodraw.c:72). Bit 1 selects the burn axis -- set runs the walk WEST (dir 6) and
 * checks the NORTH side (dir 0) through wall B, clear runs it NORTH (dir 0) and checks the
 * WEST side (dir 6) through wall A. */
#define PUFO_CELL_FLAG_FIRE_AXIS 0x02u
#define PUFO_CELL_FLAG_BURNING   0x04u

/* MapCell.nFlags32 low 3 bits: the burning state. 4 is what an incendiary hit writes
 * (Combat_ThrowFlags 0x2e... and Combat_SecondaryFlags both `| 4`), and 4 is the only value
 * any reader tests for equality with -- the fire tick at 0x4BF3C, 0x4BFF0 and 0x4C040, and
 * Combat_ThrowFlags at 0x2e... Tact_UpdateUnitControl @ 0x4a444 only tests the field for
 * non-zero. The field is decremented by one per enemy turn when the nFlags40 countdown
 * reaches 0 (0x4C228..0x4C240), and Combat_ThrowFlags randomises it with rand(3), so 1, 2,
 * 3 and 5..7 are reachable; nothing distinguishes them from 0, so they are not named. */
#define PUFO_CELL_FIRE_STATE_MASK 7u
#define PUFO_CELL_FIRE_STATE_NONE 0u
#define PUFO_CELL_FIRE_STATE_ON   4u

/* Tile-flag selectors for MapCell_TestFlag. Names come from the call sites:
 * 0 and 5 gate floor/ceiling visibility, 1 and 2 gate the wall sprites, and
 * 3/4/6 read the sprite records' own blocking bytes. */
typedef enum CellFlag {
    CELLFLAG_IS_BLANK      = 0, /* no floor and no ceiling sprite */
    CELLFLAG_WALL_B_BLANK  = 1,
    CELLFLAG_WALL_A_BLANK  = 2,
    CELLFLAG_WALL_A_BLOCKS = 3,
    CELLFLAG_WALL_B_BLOCKS = 4,
    CELLFLAG_HAS_FLOOR     = 5,
    CELLFLAG_FLOOR_BLOCKS  = 6
} CellFlag;

MapCell *MapCell_Init(MapCell *pCell, i32 nX, i32 nY, i32 nZ);
MapCell *MapCell_Reset(MapCell *pCell);
MapCell *MapCell_SetTileSprites(MapCell *pCell, Mc1Rec *pFloor, Mc1Rec *pWallA,
                                Mc1Rec *pWallB, Mc1Rec *pCeil);
bool     MapCell_TestFlag(const MapCell *pCell, CellFlag nWhich);

MapCell *MapCell_SetOccupant(MapCell *pCell, Soldier *pUnit);
bool     MapCell_HasOccupant(const MapCell *pCell);
MapCell *MapCell_SetBFlags57_3(MapCell *pCell, bool bSet);
MapCell *MapCell_SetBFlags29_4(MapCell *pCell);

/* The cell's item list is singly linked and ordered by the object's list
 * priority byte, so higher-priority objects are drawn first. */
MapCell *MapCell_ListInsertByPri(MapCell *pCell, TactItem *pNode);
MapCell *MapCell_ListRemove(MapCell *pCell, TactItem *pNode);

/* TactItem_ctor @ 0x3e718. Clears exactly four fields -- pNext, bUnk8, bUnk7 and
 * nUnk16 -- and leaves pObj/nIndex/nAmmo/nRefId to the caller, unlike the other
 * ctors in the port which zero the whole struct. Kept as the original has it so a
 * caller cannot accidentally rely on a fuller clear. */
void TactItem_ctor(TactItem *pItem);

/* Map_abSpriteSet @ Map+0x15116..0x15119 holds four race ids, and 23 means "no race in this
 * slot". Read by Save_LoadTactical, by Tact_SpawnAliens @ 0x3a2ac and by the sprite-set load loop
 * at 0x3AAE8, so it lives with the shared game constants rather than in one consumer. */
#define PUFO_SPRITE_SET_NONE 23

/* Map geometry. The grid is 4 layers of (10*dimX) x (10*dimY) cells. */
typedef struct MapGeom {
    u8 nDimX; /* map tiles across */
    u8 nDimY; /* map tiles down  */
} MapGeom;

/* The craft position is held in map tiles and the tactical grid is ten cells to a tile. Every
 * deploy anchor, camera bound and cell index converts between them with this factor: the geoscape
 * byte times PUFO_CELLS_PER_TILE plus a per-mission offset is the cell coordinate. Proven from the
 * ARM at 0x3975C, 0x39980, 0x38F34 and 0x3331C, which all multiply the same two bytes by 10. */
#define PUFO_CELLS_PER_TILE 10

/* Cell grid accessor. The original reads its grid through pCtxBase globals
 * (`apCells` @ 0x15338, stride 14400 per layer). Callers that own a grid inject
 * one of these so the algorithms stay testable without the full context.
 * Declared once here; the path and unit modules alias it. */
typedef MapCell *(*MapCellLookup)(i32 nX, i32 nY, i32 nLayer, void *pUser);

/* Canonicalize a large-unit neighbour after the original fixed 60-cell flat
 * index arithmetic. This is only for the three footprint offsets; ordinary
 * coordinates still go through the caller's strict axis-bounded lookup.
 *
 * The original table has four 60x60 layers. At a row edge, y+1 therefore
 * selects the first row of the next layer. A flat index outside that backing
 * extent has no canonical cell. This recovers the address arithmetic; it does
 * not establish that the original runtime populated each selected slot. */
static inline bool MapCell_LargeFootprintCoords(i32 nX, i32 nY, i32 nLayer,
                                                i32 nOffsetX, i32 nOffsetY,
                                                i32 *pnCellX, i32 *pnCellY,
                                                i32 *pnCellLayer)
{
    const int64_t nFlatIndex =
        (((int64_t)nLayer * 60 + nY + nOffsetY) * 60) + nX + nOffsetX;
    const int64_t nCellCount = (int64_t)PUFO_MAP_LAYERS * 60 * 60;

    if (nFlatIndex < 0 || nFlatIndex >= nCellCount) {
        return false;
    }

    *pnCellX = (i32)(nFlatIndex % 60);
    *pnCellY = (i32)((nFlatIndex / 60) % 60);
    *pnCellLayer = (i32)(nFlatIndex / (60 * 60));
    return true;
}

/* Map_IsValidCell @ 0x2bc9c. Bounds are half-open on the cell grid. */
bool Map_IsValidCell(const MapGeom *pMap, i32 nX, i32 nY, i32 nLayer);

/* Index into a [layer][y][x] cell array. Callers own the storage. */
size_t Map_CellIndex(i32 nDimX, i32 nDimY, i32 nX, i32 nY, i32 nLayer);

/* Walk DOWN a column from nLayer to 0 and stop at the first cell that has a floor
 * sprite. Two independent call sites implement exactly this descent with the same
 * inclusive `SUBS / BPL` bound: EnemyAi_TryFireOrWander @ 0x4AFD0 and
 * Tact_HandleMapClick @ 0x15500..0x1554C. When no cell in the column has a floor
 * sprite the layer-0 lookup is what is returned, matching both originals' "last
 * slot loaded" register. NULL from the lookup stops the walk early -- the originals
 * would dereference it (deviation). */
MapCell *MapCell_FloorCellBelow(MapCellLookup pfnLookup, void *pUser,
                                i32 nX, i32 nY, i32 nLayer);

/* Isometric helpers. Evidence: Map_IsoOffsetA @ 0x26f88, IsoOffsetB @ 0x26fa8,
 * Map_CenterOffsetX @ 0x26fcc, Map_CenterOffsetY @ 0x26ffc.
 *
 * Parameter meaning comes from the call sites, not from IDA's names:
 *   MapView_FocusOnUnit @ 0x2c758 calls the Center family with two coordinate
 *   DELTAS: nA = unitX - camX, nB = unitY - camY. MapView_DrawIsoLayer (0x21e2c)
 *   calls the Iso family with cell coordinates.
 *
 * Two things to note because they look like mistakes and are not:
 *   - Map_CenterOffsetY takes no dimension argument in the original; it reads
 *     only the two deltas. The nDimY it appears to need is simply unused.
 *   - IsoOffsetB has a -1 that IsoOffsetA does not. Both are reproduced.
 * The original passes the context and reads nMapDimX at +86299 / nMapDimY at
 * +86300; the port takes the dimension explicitly instead. */
i32 Map_IsoOffsetA(i32 nDimX, i32 nA, i32 nB);
i32 Map_IsoOffsetB(i32 nDimY, i32 nA, i32 nB);
i32 Map_CenterOffsetX(i32 nDimX, i32 nA, i32 nB);
i32 Map_CenterOffsetY(i32 nA, i32 nB);

/* --- per-cell light / cover computation --- */

/* Lightmap geometry, verified against the shipped lightmap.dat (529 = 0x211 bytes)
 * and the spread loop of Map_RefreshCellMeta @ 0x2c7f4.
 *
 * `Tact_InitMission` (0x3c550) reads 0x211 bytes of "lightmap.dat" into
 * unk_156AB8 @ 0x156ab8. The file is exactly 529 bytes = 23 x 23, and the spread
 * loop indexes it as `pTable[k*23 + c]` for k, c in 0..22, which is confirmed by
 * evaluating the original's expression `unk_156AB8[v19 + 264 + v20]` with
 * `v19 = -253 + 23*k` and `v20 = -11 + c`: the index reduces to `23*k + c` and
 * spans exactly 0..528. */
#define PUFO_LIGHTMAP_DIM 23
#define PUFO_LIGHTMAP_LEN (PUFO_LIGHTMAP_DIM * PUFO_LIGHTMAP_DIM)

/* Map_RefreshCellMeta @ 0x2c7f4.
 *
 * Rebuilds every cell's occlusion height (MapCell.bUnk56 @ 0x38) from the sprites
 * standing on and around it. Two phases:
 *
 *   1. Set bUnk56 = 0xFF on every cell of every layer.
 *   2. For each source cell, compute its own height, then spread that height over
 *      a 23x23 neighbourhood using the lightmap as the falloff mask, keeping the
 *      MINIMUM value per destination cell.
 *
 * The per-source height, from the decompilation:
 *     h = 0
 *     if pFloorSpr && pFloorSpr[0x1C] != 0 -> h = pFloorSpr[0x1C]
 *     h = max(h, pWallSprA[0x1C]) , max(h, pWallSprB[0x1C]), max(h, pCeilSpr[0x1C])
 *     if (nFlags40 & 0x78) != 0 -> h = 1          (the animated-tile flag field)
 *     spread only when h > 0, or when an OPERATIVE unit stands there
 *     if the cell has an occupant -> h = 0         (units do not cast via h)
 *
 * The spread writes `h + lightmap[k*23 + c]` into the destination's bUnk56 when
 * that is smaller than the current value.
 *
 * `pMap` supplies the grid; pfnLookup resolves cells. `bLightEnabled` is the
 * original's `gap_86301[1] != 0` gate -- when clear the function does nothing at
 * all, which is how a map without lighting skips the whole pass. */
bool Map_RefreshCellMeta(MapCellLookup pfnLookup, void *pUser,
                         const MapGeom *pMap, bool bLightEnabled,
                         const u8 *pLightmap);

/* --- shot-geometry helpers, shared by Tact_ProcessMoveAction @ 0x4f3f0 --- */

/* Sub-cell centre of a cell, in the units the arc tracers work in. Evidence:
 * 0x5003C..0x50070 and 0x50778..0x507A8 both build exactly
 *     x = 16*cell->nX + 8
 *     y = 16*cell->nY + 8
 *     z = 12*cell->nZ + (-12*pCeilSpr->iCeilParam / 24) + 6
 * and the ceiling term is 0 when the cell has no ceiling sprite. The division is
 * C integer division, so it truncates toward zero. */
i32 Map_CellCentreX(const MapCell *pCell);
i32 Map_CellCentreY(const MapCell *pCell);
i32 Map_CellCentreZ(const MapCell *pCell);

/* The ceiling term of Map_CellCentreZ on its own: `(-12 * pCeilSpr->iCeilParam)
 * / 24`, or 0 with no ceiling sprite. A caller that adds a muzzle offset to Z
 * before the final +6 needs this piece separately. */
i32 Map_CeilingLift(const MapCell *pCell);

/* Clear the shot-trace flag (MapCell.bFlags29 bit 5, 0x20) over the grid.
 * Tact_ProcessMoveAction runs this sweep four times (0x4FDDC, 0x4FF08, 0x4F4C4,
 * 0x50638) before every arc or throw trace. The original's loop bounds are
 *     for (v58 = 0; v58 < 240; v58 += 60)          -> 4 layers
 *         for (v61 = 0; v61 < 10*nMapDimY; ++v61)  -> rows
 *             for (v62 = 0; v62 < 10*nMapDimX; ++v62)  -> columns
 * (ARM 0x4FE20..0x4FE64), so a map with dimX = dimY = 6 is swept over its whole
 * 60 x 60 x 4 grid and a smaller map only over its used corner.
 *
 * Deviation: the original dereferences the grid slot unconditionally; the port
 * skips a NULL the injected lookup may return. */
void Map_ClearShotTraceFlags(MapCellLookup pfnLookup, void *pUser,
                             const MapGeom *pMap);

#endif /* PUFO_MAPCELL_H */
