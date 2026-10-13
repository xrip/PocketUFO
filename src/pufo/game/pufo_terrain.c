/* pufo_terrain.c - see pufo_terrain.h for evidence. Both tables are checked byte for byte against
 * pufobeta.exe by docs/parity/audit_tables.py. */
#include "game/pufo_terrain.h"

/* g_aTileSprIndex @ 0xA64E8, 64 u16 entries in 16 rows of 4: floor, wall A, wall B, ceiling. */
const u16 g_aTileSprIndex[PUFO_TILE_SPRITE_ROWS][PUFO_TILE_SPRITE_SLOTS] = {
    { 10, 16,  0,  0 },
    { 10,  0,  0,  0 },
    { 10,  0,  0,  0 },
    { 10, 16, 15,  4 },
    { 10,  0, 15,  0 },   /* rows 4..7 are the set the second stamping pass walks */
    { 10,  0,  0,  0 },
    { 10,  0,  0,  0 },
    { 10, 16, 15,  4 },
    { 66, 88,  0,  0 },
    { 66,  0,  0,  0 },
    { 66,  0,  0,  0 },
    { 66, 88, 87, 63 },
    { 66,  0, 87,  0 },
    { 66,  0,  0,  0 },
    { 66,  0,  0,  0 },
    { 66, 88, 87, 63 },
};

/* g_aTerrainModuleCount @ 0xA6AF0, indexed by Map_nMissionType: jungle 12, culta 19, forest 12,
 * xbase 21, ubase 16, urban 15, desert 12, mount 13, polar 14, mars 11. */
const u8 g_aTerrainModuleCount[PUFO_TERRAIN_TYPE_COUNT] = {
    12, 19, 12, 21, 16, 15, 12, 13, 14, 11
};
