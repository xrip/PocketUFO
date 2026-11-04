/* pufo_los.h - voxel occupancy and line-of-sight tracing.
 *
 * Evidence: Tact_LoadLoftData @ 0x3bd6c,
 *           Map_TraceVoxelOccupancy @ 0x3242c (voxel test),
 *           Map_TraceLos @ 0x30890 (the trace).
 *
 * ## Voxel space
 *
 * The tactical grid is voxelised: each map cell is 16x16x12 voxels.
 * Map_TraceVoxelOccupancy converts a world voxel coordinate with
 *     cellX = x / 16, cellY = y / 16, cellZ = z / 12
 * and then tests one bit inside that cell:
 *     bit = (rowMask >> (15 - (x % 16))) & 1
 * where rowMask = pMaskTable[y % 16 + 16 * rowIndex] and rowIndex selects a
 * 16-row slice of the sprite. So each sprite is a stack of 16-bit row masks, one
 * per y line, and the bit position encodes x.
 *
 * ## Mask tables
 *
 *   unk_15718C <- "loftemps.dat"   3584 bytes = 1792 u16   (terrain sprites)
 *   unk_1571E8 <- "unitloft.dat"  43008 bytes = 21504 u16  (unit silhouettes)
 *
 * Confirmed from the shipped main.dat: LOFTEMPS.DAT is 3584 bytes and
 * UNITLOFT.DAT is 43008 bytes, matching the u16 counts above.
 *
 * Terrain sprite row selection, for a sprite record p and level z:
 *     rowIndex = p[8 + (z % 12)]
 * i.e. the record carries a 12-entry table at offset 8 mapping each voxel level
 * to a sprite index. The unit table instead selects by unit type, facing and
 * pose:
 *     rowIndex = 12 * row + (z % 12)
 *     row      = word_A9488[16 * nUnitType + 2 * bFacing + (bFlags & 1)]
 * and word_A9488 @ 0xa9488 is a 736-byte table -- 16 rows per unit type for types
 * 0..22, ending at g_aUnitMuzzleOffset @ 0xa9768. Types 0..3 use the fold
 * `row == facing + 8 * kneeling`; every other type has its own row window in
 * unitloft.dat (floater 11 -> 24, muton 10 -> 0, silacoid 19 -> 40, cyberdisk 18
 * and the tanks 4..8 -> 80, sectopod 21 -> 48). Type 23 has no row at all: the
 * original's read lands on the muzzle-offset table and is not a sprite row.
 *
 * ## Blocking rules
 *
 * For the wall sprites there is an extra condition beyond the voxel bit:
 *     blocks if bit && (sprite[42] == 0 || (cell->bFlags29 & 4) == 0)
 * Floor and ceiling sprites block on the voxel bit alone. Occupants block using
 * the unit silhouette table, with large units offset by 0..3 depending on which
 * of their four cells was hit.
 */
#ifndef PUFO_LOS_H
#define PUFO_LOS_H

#include "game/pufo_mapcell.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

/* Voxels per map cell. */
#define PUFO_VOX_PER_CELL_X 16
#define PUFO_VOX_PER_CELL_Y 16
#define PUFO_VOX_PER_CELL_Z 12
#define PUFO_MC1_ROWTAB_TO_Z 8   /* sprite byte holding the loft row id for a z */

/* Sprite-record field offsets the voxel test reads. */
#define PUFO_MC1_ROW_TABLE_OFF 8   /* 12 bytes, z -> sprite index */
#define PUFO_MC1_BLOCK_OFF     42  /* 0x2a; the "blocks movement" byte */

/* Why a voxel test stopped. The original's Map_TraceVoxelOccupancy @ 0x3242c does NOT
 * return these: it returns 0 on every blocked path and 1 for a clear voxel, and it
 * stores the reason in Map.nFireArg5 (0x1404C) on the shared store LABEL_56 @ 0x32C68
 * that all five blocked paths reach. The port returns the reason instead of the 0/1
 * pair, so a caller tests LOS_HIT_NONE for "clear" and anything else for "blocked";
 * the reason is also what a shot's landing slot later reads back (nSlot 4 is the
 * direct hit on a unit, Combat_ResolveShot @ 0x2f408). */
typedef enum LosHit {
    LOS_HIT_NONE      = -1,
    LOS_HIT_FLOOR     = 0,
    LOS_HIT_WALL_A    = 1,
    LOS_HIT_WALL_B    = 2,
    LOS_HIT_CEILING   = 3,
    LOS_HIT_OCCUPANT  = 4
} LosHit;

/* The loaded mask tables. Both are u16 arrays. */
typedef struct LosTables {
    const u16 *pLoftemps;  /* unk_15718C, 1792 entries */
    u32        nLoftemps;
    const u16 *pUnitloft;  /* unk_1571E8, 21504 entries */
    u32        nUnitloft;
} LosTables;

/* word_A9488 @ 0xa9488: 16 sprite rows per unit type, for types 0..22 (736 bytes
 * up to g_aUnitMuzzleOffset @ 0xa9768). The per-type windows are not identical --
 * see the decoded table in pufo_los.c. */
#define PUFO_UNITLOFT_ROWS_PER_TYPE 16
#define PUFO_UNITLOFT_TYPES         23

/* Tact_LoadLoftData @ 0x3bd6c. Loads LOFTEMPS.DAT and UNITLOFT.DAT into the two row-mask tables.
 * The caller owns the buffers; the original does not free the previous pair when it reloads. */
bool Tact_LoadLoftData(LosTables *pTables);

/* Reads one bit out of a row-mask table. Returns false when the index is out of
 * range, which the original does not check. */
bool Los_TestRowBit(const u16 *pTable, u32 nTableLen, u32 nRowIndex,
                    u32 nXBit /* 0..15, from the left */);

/* Map_TraceVoxelOccupancy @ 0x3242c.
 *
 * Returns LOS_HIT_NONE when the voxel is clear, otherwise the reason it is
 * blocked. pfnLookup resolves the cell; pUnitSelExclude and pUnitExtraExclude are
 * the two units the original skips (the selected unit and an action-scratch unit
 * at anActionPhaseSave0[120]). Pass NULL to skip neither.
 *
 * The large-unit neighbour scan is approximated: the original walks the unit's
 * four cells to derive a 0..3 sub-cell offset. Here that offset is derived from
 * the cell's own coordinates, which is equivalent because all four cells were
 * stamped by Unit_PlaceOnMap with the same unit and the offset depends only on
 * which of the four the ray is passing through. */
LosHit Map_TraceVoxelOccupancy(const LosTables *pTables,
                               MapCellLookup pfnLookup, void *pUser,
                               u32 nX, u32 nY, u32 nZ,
                               const Soldier *pUnitSelExclude,
                               const Soldier *pUnitExtraExclude);

/* Map_TraceLos @ 0x30890.
 *
 * Walks a ray from (x0,y0,z0) to (x1,y1,z1) in unit steps of dTravel, testing
 * the voxel at each sample. Returns true when the ray reaches the target without
 * being blocked.
 *
 * The original's angle setup, reproduced with the reduced constants it uses:
 *   yaw   = atan2(dy, dx)
 *   pitch = atan2(dz, sqrt(dx*dx + dy*dy))
 *   when dx == 0 and dy == 0 (a purely vertical ray) yaw is instead derived from
 *   a random 0..61 draw scaled by 0.1, and pitch is +pi/2 or -pi/2 by the sign of
 *   dz. That randomness is deliberate in the original and is reproduced.
 *   bound = sqrt(dz*dz + dist*dist)
 *
 * A cell change accrues the cell's nFlags32 low 3 bits as "cost"; the ray stops
 * when that exceeds 7, and also when a voxel blocks. The accumulated cost is
 * skipped for values 0 and 4, which is the original's rule for cells that do not
 * count toward blocking.
 *
 * pUnitSelExclude and pUnitExtraExclude are omitted from occupant-voxel hits.
 * The original callers supply the active viewer and target in these slots; callers
 * without live unit endpoints pass NULL. pOutYawRandom and pOutCost are optional.
 */
bool Map_TraceLos(const LosTables *pTables, MapCellLookup pfnLookup, void *pUser,
                  i32 nX0, i32 nY0, i32 nZ0,
                  i32 nX1, i32 nY1, i32 nZ1,
                  const Soldier *pUnitSelExclude, const Soldier *pUnitExtraExclude,
                  u8 *pOutYawRandom, i32 *pOutCost);

#endif /* PUFO_LOS_H */
