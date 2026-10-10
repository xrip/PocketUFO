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
#include "pufo_mapcell.h"
#include "pufo_tactstate.h"

#define PUFO_SPLASH_HALF  20   /* the window extends 20 cells each side */
#define PUFO_SPLASH_SPAN  41   /* 2 * 20 + 1 */
#define PUFO_SPLASH_DWORDS (PUFO_SPLASH_SPAN * PUFO_SPLASH_SPAN * 4)

/* Tile power thresholds. `> 0x14` gates cell damage and the flag mutation,
 * `> 0x46` gates the flag mutation for weapon class 50. */
#define PUFO_POWER_MIN   0x14
#define PUFO_POWER_HIGH  0x46

/* Weapon class ids with special handling in the splash walk. */
#define PUFO_WEAPON_CLASS_50 50
#define PUFO_WEAPON_CLASS_15 15

/* FireWeapon: the record the fire pipeline carries through the shot. IDA types
 * the argument as `int`, but every use is a pointer dereference. Only four
 * offsets are proven by the code that reads them:
 *   +175 selector byte for the readiness source (Unit_ApplyCellDamage)
 *   +182 u16 TU cost added to a unit when class is 50
 *   +196 selector byte used when +175 is zero
 *   +200 u16 class id
 * Everything else stays an opaque gap until a function that reads it is ported. */
typedef struct FireWeapon {
    u8   gap_0[175];    /* 0x00 */
    u8   bReadySelA;    /* 0xAF (175) */
    u8   gap_B0[6];     /* 0xB0 */
    u16  uTuCost;       /* 0xB6 (182) */
    u8   gap_B8[12];    /* 0xB8 */
    u8   bReadySelB;    /* 0xC4 (196) */
    u8   gap_C5[3];     /* 0xC5 */
    u16  uClass;        /* 0xC8 (200) */
} FireWeapon;

_Static_assert(offsetof(FireWeapon, bReadySelA) == 175, "FireWeapon.bReadySelA at 175");
_Static_assert(offsetof(FireWeapon, uTuCost) == 182, "FireWeapon.uTuCost at 182");
_Static_assert(offsetof(FireWeapon, bReadySelB) == 196, "FireWeapon.bReadySelB at 196");
_Static_assert(offsetof(FireWeapon, uClass) == 200, "FireWeapon.uClass at 200");

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
    FireWeapon   *pCtxWeapon;/* Map.nFireArg6 @ Map+0x14050 */
} CombatCtx;

/* Unit_ApplyCellDamage @ 0x2e5d0. Walks the cell's item list and damages every
 * linked unit that has not already been hit in this shot. The original returns
 * a mixed pointer (last unit seen, or the cell after a list removal); no call
 * site uses it, so the port returns nothing.
 *
 * `nPower` is the slot-0 tile power of the hit record. Items whose unit is gone
 * (no unit, or uHp == 0) are dropped from the list, but only when nPower > 40,
 * and each removal restarts the walk from the list head. */
void Unit_ApplyCellDamage(MapCell *pCell, u16 nPower, const FireWeapon *pWeapon);

/* Combat_SplashDamageCells @ 0x2e7d8. Applies the 41 x 41 hit window.
 *
 * `apHits` is the window, PUFO_SPLASH_DWORDS dwords. `pShotWeapon` is the
 * original's `a5` (the firing weapon record); `pCtx->pCtxWeapon` is the Map's
 * fire-context record. They are separate parameters because the original reads
 * class from the Map one and from `a5` in another place. */
void Combat_SplashDamageCells(const CombatCtx *pCtx, i32 nX, i32 nY, i32 nLayer,
                              const FireWeapon *pShotWeapon, const u32 *apHits,
                              SplashMode eMode);

#endif /* PUFO_COMBAT_H */
