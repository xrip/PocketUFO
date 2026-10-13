/* pufo_terrain.h - the mission-terrain builder and the shipped tables it reads.
 *
 * Evidence: Map_BuildMissionTerrain @ 0x22e10, plus the name tables it reads:
 *           aUfo1a @ 0xA6590, aPlane @ 0xA6668, aTerrainNameByMissionType @ 0xA67F8,
 *           aTerrainNameSuffix0 @ 0xA7B20, aTileSprIndex @ 0xA64E8, byte_A6AF0 @ 0xA6AF0.
 * All six are read only by that one function, so they live here rather than in a shared header.
 * tools/gen_terrain_tables.py emits the four name tables from the image; docs/parity/audit_tables.py
 * checks all six byte for byte.
 *
 * g_aTileSprIndex @ 0xA64E8 is 64 u16 entries in 16 rows of 4. A row is one tile type's four sprite
 * slots, in MapCell order: floor, wall A, wall B, ceiling. A zero entry means "store no sprite";
 * a non-zero entry is passed to Font_GetSprByIndex as `entry - 1`. The builder reads rows 0..3 in one
 * stamping pass and rows 4..7 in another, and the two later passes pick their row from a loop counter.
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

#include "game/pufo_font.h"    /* PufoFont, CraftSpritePatchEnv */
#include "game/pufo_mapcell.h" /* MapCell */
#include "game/pufo_rmp.h"     /* RmpData */
#include "pufo_common.h"

#define PUFO_TILE_SPRITE_ROWS   16  /* g_aTileSprIndex rows */
#define PUFO_TILE_SPRITE_SLOTS   4  /* floor, wall A, wall B, ceiling */
#define PUFO_TERRAIN_TYPE_COUNT 10  /* mission types 0..9 */

/* The builder's two 6x6 tile arrays: the module grid (bytes) and, on the XCom-base path, the
 * per-tile kind array (dwords). Both are indexed 6 * tileY + tileX. */
#define PUFO_TERRAIN_GRID_STRIDE  6
#define PUFO_TERRAIN_GRID_CELLS  36

/* The cell array the builder allocates into: 4 layers of 60 x 60 cells. In the original this is an
 * inline block in Map at Map+0x15338, 14400 dwords = 0xE100 bytes, ending exactly at Map+0x23438
 * where Map.apTactItems begins. */
#define PUFO_TERRAIN_LAYER_COUNT      4
#define PUFO_TERRAIN_CELLS_PER_LAYER 3600
#define PUFO_TERRAIN_CELL_SLOTS     14400

extern const u16 g_aTileSprIndex[PUFO_TILE_SPRITE_ROWS][PUFO_TILE_SPRITE_SLOTS];
extern const u8  g_aTerrainModuleCount[PUFO_TERRAIN_TYPE_COUNT];

/* The four module-name tables, nine bytes per entry. */
extern const char g_aUfoModuleName[15][9];
extern const char g_aPlaneName[3][9];
extern const char g_aTerrainNameByMission[PUFO_TERRAIN_TYPE_COUNT][9];
extern const char g_aTerrainNameSuffix[PUFO_TERRAIN_TYPE_COUNT][9];

/* ------------------------------------------------------------------ Map_BuildMissionTerrain */

/* Everything the builder touches that the port does not own. The original reaches all of it through
 * the global Map block at 0x1139B8; here each value is a caller-owned pointer or array.
 *
 * The builder ALLOCATES the 14400 MapCell objects and stores them into apCells, exactly as the
 * original does -- including the allocation-failure path, which stores NULL and still calls
 * MapCell_Init(NULL, x, y, layer). The original has no null guard there (nor in the tail's flag
 * clear), so a failed allocation is a crash on WinCE too. */
typedef struct MapTerrainEnv {
    /* Map-block scalars, by pointer so the builder's writes land in the caller's block. */
    u8   *pnMapLayers;      /* Map+0x15114 */
    u8   *pnMissionType;    /* Map+0x1511A */
    u8   *pnTerrainVariant; /* Map+0x15111 */
    u8   *pnTier;           /* Map+0x15112, written from pnTierSource */
    i32  *pnTierSource;     /* Map+0x1510C, the dword the tier byte is taken from */
    u8   *pbUnk15115;       /* Map+0x15115, gates the ceiling-break shot count */
    u8   *pnMapDimX;        /* Map+0x1511B, map tiles across */
    u8   *pnMapDimY;        /* Map+0x1511C, map tiles down */
    /* Where the craft pad lands. These are Map_nCraftMapX / Map_nCraftMapY @ 0x15139 / 0x1513A --
     * the same two bytes Map_PatchCraftSprites reads at the tail (its ARM loads at 0x25BB4 and
     * 0x25BBC are literally 0x1513A and 0x15139), so the builder writes the craft's tile and the
     * tail immediately uses it. */
    u8   *pPlaneTileX;      /* Map+0x15139 */
    u8   *pPlaneTileY;      /* Map+0x1513A */
    i32  *pnShotX;          /* Map+0x14054, the shot origin X Combat_ResolveShot reads */
    i32  *pnShotY;          /* Map+0x14058 */
    i32  *pnSpawnTileX;     /* Map+0x1514C, the random module tile on the empty-layers path */
    i32  *pnSpawnTileY;     /* Map+0x15150 */
    u8   *pbSpawnAlt;       /* Map+0x15148, selects the "15" suffix over "0" */

    MapCell             **apCells;       /* 14400 slots, Map+0x15338 */
    const ObjectRec *const *apShotWeapons; /* Map+0x108; the shot's object is *(Map + 0x108 + 4*nSelector) */
    const u8        *paTileTypes;    /* aMissionTileTypes @ 0x159D5C, 36 bytes in rows of 6 */
    RmpData         *pRmp;           /* the module .rmp scratch the builder reloads per module */
    PufoFont        *pFont;

    /* Map_PatchCraftSprites @ 0x25ab4, called at the tail. Its craft tile is pPlaneTileX/Y above;
     * only the sprite group base is separate, and it lives in the font block. */
    u16  nCraftSprBase;     /* fontSys + 0x06, the u16 the flag-3 template base also uses */

    /* Font_LoadTactSprites (0x257c4, 752 B) is not ported; it loads the tactical sprite set the
     * stamping passes then index into. Same hook shape as TactLoadHooks.pfnLoadTactSprites.
     *
     * Combat_ResolveShot @ 0x2f12c IS ported, but its CombatCtx (TactState, the three rosters, the
     * 41x41 hit windows) belongs to the caller that owns the tactical context, not to the terrain
     * builder. The hook carries the original's six arguments minus pMap. */
    void (*pfnLoadTactSprites)(void *pUser);
    void (*pfnResolveShot)(i32 nTgtX, i32 nTgtY, i32 nTgtZ, i32 nSlot,
                           const ObjectRec *pObj, void *pUser);
    void *pUser;
} MapTerrainEnv;

/* Map_BuildMissionTerrain @ 0x22e10. 10,676 bytes, 313 blocks.
 *
 * Three mutually exclusive shapes, chosen by Map_nMapLayers and Map_nMissionType:
 *   nMapLayers != 0                  the normal mission: UFO module, craft pad, random modules,
 *                                    then the ceiling-break shots
 *   nMapLayers == 0, XCom base       a fixed 6x6 base read from aMissionTileTypes
 *   nMapLayers == 0, anything else   a fixed 6x6 fallback built from three hard-coded tiles
 *
 * All three end with the same tail: mask every cell's nFlags32 with 0xFFFFFF87 (clearing bits 3..6),
 * then Map_PatchCraftSprites. */
void Map_BuildMissionTerrain(MapTerrainEnv *pEnv);

#endif /* PUFO_TERRAIN_H */
