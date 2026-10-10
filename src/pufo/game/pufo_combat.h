/* pufo_combat.h - shot resolution: what a hit does to cells, sprites and units.
 *
 * Evidence: Combat_SplashDamageCells @ 0x2e7d8, Unit_ApplyCellDamage @ 0x2e5d0,
 *           Combat_ResolveShot @ 0x2f12c (caller).
 *
 * The shot pipeline writes a 41 x 41 window of per-(cell, slot) hit records into
 * Map.gap_TrajRest (Map+0x4EBC) and then hands that window to
 * Combat_SplashDamageCells, which walks it and applies damage. The window is
 * centred on the impact cell: cell (x - 20 + col, y - 20 + row) at a fixed layer.
 *
 * Record layout, from the addressing in the disassembly
 * (`LDR R3, [R4, #0x4EBC]`-style indexing with `MUL ..., #60` for the grid and
 * `4 * (41 * row + col) + slot` for the records): one dword per (cell, slot),
 * four slots per cell, so 16 bytes per cell and 26896 bytes per window. Only two
 * of the four dwords are ever read: slot 0 holds the tile power and slot 3 holds
 * the unit power.
 */
#ifndef PUFO_COMBAT_H
#define PUFO_COMBAT_H

#include "pufo_common.h"
#include "game/pufo_boot.h"
#include "pufo_mapcell.h"
#include "pufo_tactstate.h"

#define PUFO_SPLASH_HALF  20   /* the window extends 20 cells each side */
#define PUFO_SPLASH_SPAN  41   /* 2 * 20 + 1 */
#define PUFO_SPLASH_DWORDS (PUFO_SPLASH_SPAN * PUFO_SPLASH_SPAN * 4)

/* Tile power thresholds. `> 0x14` gates cell damage and the flag mutation,
 * `> 0x46` gates the flag mutation for weapon class 50. */
#define PUFO_POWER_MIN   0x14
#define PUFO_POWER_HIGH  0x46

/* Weapon ids with special handling in the splash walk. These are
 * ObjectRecFull.uSortKey values, which is also the index the shot sound table
 * `off_AA040` is read with. */
#define PUFO_WEAPON_SORT_50 50
#define PUFO_WEAPON_SORT_15 15

/* The weapon record is ObjectRecFull (208 bytes, defined in pufo_boot.h) -- the
 * same record the data layer parses out of main.dat. Combat_ResolveShot stores
 * it in `pMap.nFireArg6` before it starts the splash walk, so the two places the
 * walk reads a weapon from are the same object in every real shot.
 *
 * Offsets this module reads, all proven from the code that reads them:
 *   +0xAF bFireType   readiness source selector, and the branch selector of the
 *                     shot resolution (0 direct hit, 1 and 2 area fire)
 *   +0xB6 uFirePower  written into the hit window at the impact cell, and added
 *                     to a unit's uTuSpent for sort key 50
 *   +0xC4 bReadySel   readiness source when bFireType is zero
 *   +0xC8 uSortKey    weapon id and sound index
 */

/* Index into the hit window: one dword per (row, column, slot). */
size_t Combat_HitIndex(i32 nRow, i32 nCol, i32 nSlot);

/* Splash walk mode, IDA's `a6`. The original branches on 0, 1, 2 and treats
 * anything else as "reuse the readiness scale computed for the previous cell". */
typedef enum SplashMode {
    SPLASH_MODE_NORMAL   = 0, /* the shooter's own shot; skips the selected unit */
    SPLASH_MODE_SECONDARY = 1,
    SPLASH_MODE_THROW    = 2
} SplashMode;

/* Everything the walk needs from the game context, injected so the function is
 * testable without the 152 KB Map and its globals. */
typedef struct CombatCtx {
    MapCellLookup pfnLookup;
    void         *pUser;
    const MapGeom *pGeom;
    TactState    *pState;    /* target of Unit_EnqueueIfApSpent */
    Soldier      *pSelUnit;  /* pCtxBase.pSelUnit @ 0x1139b8 */
    ObjectRecFull *pCtxWeapon; /* Map.nFireArg6 @ Map+0x14050 */
} CombatCtx;

/* Unit_ApplyCellDamage @ 0x2e5d0. Walks the cell's item list and damages every
 * linked unit that has not already been hit in this shot. The original returns
 * a mixed pointer (last unit seen, or the cell after a list removal); no call
 * site uses it, so the port returns nothing.
 *
 * `nPower` is the slot-0 tile power of the hit record. Items whose unit is gone
 * (no unit, or uHp == 0) are dropped from the list, but only when nPower > 40,
 * and each removal restarts the walk from the list head. */
void Unit_ApplyCellDamage(MapCell *pCell, u16 nPower, const ObjectRecFull *pWeapon);

/* Combat_SplashDamageCells @ 0x2e7d8. Applies the 41 x 41 hit window.
 *
 * `apHits` is the window, PUFO_SPLASH_DWORDS dwords. `pShotWeapon` is the
 * original's `a5` (the firing weapon record); `pCtx->pCtxWeapon` is the Map's
 * fire-context record. They are separate parameters because the original reads
 * class from the Map one and from `a5` in another place. */
void Combat_SplashDamageCells(const CombatCtx *pCtx, i32 nX, i32 nY, i32 nLayer,
                              const ObjectRecFull *pShotWeapon, const u32 *apHits,
                              SplashMode eMode);

#endif /* PUFO_COMBAT_H */
