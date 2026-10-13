/* pufo_terrain.h - the shipped tables the mission-terrain builder reads.
 *
 * Evidence: Map_BuildMissionTerrain (0x22e10, not ported yet). All six xrefs to both tables are
 * inside that one
 * function, so they live here rather than in a shared header.
 *
 * g_aTileSprIndex @ 0xA64E8 is 64 u16 entries in 16 rows of 4. A row is one tile type's four sprite
 * slots, in MapCell order: floor, wall A, wall B, ceiling. A zero entry means "store no sprite";
 * a non-zero entry is passed to Font_GetSprByIndex as `entry - 1`. Map_BuildMissionTerrain reads
 * rows 0..3 in one stamping pass and rows 4..7 in another, and the two later passes pick their row
 * from a loop counter.
 *
 * The table is 128 bytes and ends at 0xA6568. Two things make that worth stating: IDA declares
 * `aTileSprIndex[128]`, which is 256 bytes and runs 128 bytes past the real end into the module-name
 * strings at 0xA6590; and the 40 bytes from 0xA6568 really are a different table, referenced only by
 * Smap_InitCraftSelectState (0x61b94, not ported).
 *
 * g_aTerrainModuleCount @ 0xA6AF0 is the per-mission-type module ceiling. The builder draws a module
 * number as Game_RandU16(g_aTerrainModuleCount[nMissionType]), except for Urban, which is
 * Game_RandU16(count - 3) + 3 -- so an Urban module number is 3..14 while every other terrain starts
 * at 0. The drawn number becomes the suffix of the module's .ma1 name.
 */
#ifndef PUFO_TERRAIN_H
#define PUFO_TERRAIN_H

#include "pufo_common.h"

#define PUFO_TILE_SPRITE_ROWS   16  /* g_aTileSprIndex rows */
#define PUFO_TILE_SPRITE_SLOTS   4  /* floor, wall A, wall B, ceiling */
#define PUFO_TERRAIN_TYPE_COUNT 10  /* mission types 0..9 */

extern const u16 g_aTileSprIndex[PUFO_TILE_SPRITE_ROWS][PUFO_TILE_SPRITE_SLOTS];
extern const u8  g_aTerrainModuleCount[PUFO_TERRAIN_TYPE_COUNT];

#endif /* PUFO_TERRAIN_H */
