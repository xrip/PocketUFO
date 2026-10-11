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

/* Two parallel 148-entry tables, both indexed by ObjectRecFull.uSortKey with no
 * bound check in the original:
 *   off_AA040 @ 0xaa040  - the sound Combat_ResolveShot plays on impact.
 *   word_AA168 @ 0xaa168 - the sound Tact_ProcessMoveAction @ 0x4f3f0 plays for a
 *                          queued shot (case 0xE at 0x50338 and case 0x12 at 0x50330).
 * 0xAA040 + 148*2 = 0xAA168, so 148 is the real extent of the first table; the
 * port used to claim 180, which ran 32 entries into the second table. */
#define PUFO_SHOT_SOUND_COUNT 148
extern const u16 g_aShotSound[PUFO_SHOT_SOUND_COUNT];
extern const u16 g_aFireSoundBySortKey[PUFO_SHOT_SOUND_COUNT];

/* Sound_Play(2) at 0x2f66c: a ceiling tile gave way under the shot. */
#define PUFO_SOUND_CEILING_BREAK 2

/* Everything the walk needs from the game context, injected so the function is
 * testable without the 152 KB Map and its globals. */
typedef struct CombatCtx {
    MapCellLookup pfnLookup;
    void         *pUser;
    const MapGeom *pGeom;
    TactState    *pState;    /* target of Unit_EnqueueIfApSpent */
    Soldier      *pSelUnit;  /* pCtxBase.pSelUnit @ 0x1139b8 */
    ObjectRecFull *pCtxWeapon; /* Map.nFireArg6 @ Map+0x14050 */
    i32           nShotOriginX; /* Map.nFireOrigX @ Map+0x14054 */
    i32           nShotOriginY; /* Map.nFireOrigY @ Map+0x14058 */
} CombatCtx;

/* Hooks for the two things Combat_ResolveShot touches that the port does not
 * model yet.
 *
 * pfnComputeFov is Map_ComputeFov @ 0x2deac (1776 bytes), unported. Its argument
 * list is not settled either: the area-fire call site sets R1 = R2 = 0, the
 * ceiling-break call site sets R1 = 20 - column, R2 = the row counter and R3 =
 * origin X, and Hex-Rays renders both as a one-argument call. The port calls the
 * hook at the two proven points and leaves the arguments to the real function.
 *
 * pfnAreaFire stands for the two writes at 0x2fa80 and 0x2fa94 -
 * `unk_157198->bUnk70115 = 1` (guarded by `pCtxBase.gap_2[0] == 0`) and
 * `unk_157198->nUnk70024[4] = 0`. The unk_157198 global has no port model yet. */
typedef struct ShotHooks {
    void (*pfnComputeFov)(void *pUser);
    void (*pfnAreaFire)(void *pUser);
    void *pUser;
} ShotHooks;

/* The three rosters whose per-shot "already hit" byte (Soldier+0x1B1) the shot
 * resolution clears. The original reads the counts from Map: nXcomUnitCount and
 * nEnemyUnitCount are tested against zero, nCivilianUnitCount against greater
 * than zero. */
typedef struct UnitRosters {
    Soldier **apXcom;
    i32       nXcom;
    Soldier **apEnemy;
    i32       nEnemy;
    Soldier **apCiv;
    i32       nCiv;
} UnitRosters;

typedef struct ShotOutcome {
    SplashMode eMode;   /* the original's a6, promoted to 2 by a ceiling break */
    i32        nImpactX;
    i32        nImpactY;
    i32        nDamage; /* direct unit hit only; -1 on the splash path */
    i32        nFovCalls;
} ShotOutcome;

/* Combat_ResolveShot @ 0x2f12c. Resolve one shot: fill the 41 x 41 hit window,
 * apply it, and either damage the unit at the impact cell directly or walk the
 * splash.
 *
 * nSlot is the original's nFireArg5, the slot the shot landed in: 0 floor,
 * 1 wall A, 2 wall B, 3 ceiling, 4 a unit. Slot 4 with bFireType 0 takes the
 * direct unit path; every other combination takes the splash path.
 *
 * apHits is the hit window, PUFO_SPLASH_DWORDS dwords. apHitsSave is the second
 * window the original keeps next to the first (Map.gap_TrajRest + 26896); the
 * shot resolution snapshots the window into it before it attenuates the layers
 * above and below. Pass NULL to skip the copy. */
void Combat_ResolveShot(CombatCtx *pCtx, const ShotHooks *pHooks,
                        const UnitRosters *pRosters, i32 nTgtX, i32 nTgtY,
                        i32 nLayer, i32 nSlot, const ObjectRecFull *pWeapon,
                        u32 *apHits, u32 *apHitsSave, ShotOutcome *pOut);

/* Research gate. The original asks the in-memory research table directly:
 *     aFacTypeTable[4 + 34 * byte_AA968[weapon.uSortKey]] != 0
 * A weapon may not be fired unless its research topic allows it. The table's
 * in-memory field order is still unresolved (the port's ResearchTopic order is
 * the on-disk order), so the check is injected as a callback rather than baked
 * into a layout guess here. */
typedef struct FireGate {
    bool (*pfnWeaponFireable)(i32 nSortKey, void *pUser);
    void *pUser;
} FireGate;

/* pCtxBase.nTrajFront0[1040..1135]: twelve action-mode slots, four bytes each.
 * [1040 + 4*mode] is the TU cost and [1088 + 4*mode] the accuracy term. Three of
 * the twelve are the fire modes; the rest belong to the other action modes. */
#define PUFO_ACTION_SLOT_COUNT 12
typedef struct ActionCostTable {
    i32 nTuCost[PUFO_ACTION_SLOT_COUNT];
    i32 nAccuracy[PUFO_ACTION_SLOT_COUNT];
} ActionCostTable;

/* Unit_SelectBestFireMode @ 0x478e4. Pick the weapon and fire mode for a shot.
 *
 * Each hand is a candidate when it holds an item, has ammo (nAmmo > 2) or an
 * unlimited weapon (bAmmoCap == 255), is not two-handed, and has at least one of
 * abArmorTu[0..1] non-zero. Mode 1 is tried from abArmorTu[1] and mode 0 from
 * abArmorTu[0], each costing auStat[0] * abArmorTu[mode] / 100 TU and accepted
 * only when that fits in uTu; mode 0 is checked second and so wins when both fit.
 * An operative whose research topic forbids the weapon has that hand's cost reset
 * to 0xFFFF. The right hand wins when it survived, otherwise the left.
 *
 * On success the chosen item and mode are written to ppSelWeapon / pnActionMode
 * and the two slots for that mode are filled in pCosts. */
bool Unit_SelectBestFireMode(Soldier *pUnit, const FireGate *pGate,
                             TactItem **ppSelWeapon, i32 *pnActionMode,
                             ActionCostTable *pCosts);

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
