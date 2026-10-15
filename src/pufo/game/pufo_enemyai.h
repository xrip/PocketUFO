/* pufo_enemyai.h - the enemy AI's decision predicates.
 *
 * Evidence: EnemyAi_bMultiTargetTypeMatch @ 0x4b928,
 *           EnemyAi_EnqueueRandomTurn @ 0x4ab98,
 *           EnemyAi_TryFireOrWander @ 0x4abc8.
 *
 * ## The unit-type table
 *
 * `byte_A7B80` @ 0xa7b80 is a 4-byte-stride table indexed by unit type. Two of its
 * columns are used by the AI:
 *
 *     byte[4*type + 0]   "flag 0"    (g_aUnitTypeFlag0)
 *     byte[4*type + 1]   "is large"  (g_aUnitTypeIsLarge)
 *
 * Decoded from the 64 bytes in the image:
 *
 *     type :  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
 *     flag0:  0  0  0  1  0  0  0  1  1  0  0  1  0  1  0  0
 *     large:  0  0  0  0  1  1  1  1  1  0  0  1  0  1  0  0
 */
#ifndef PUFO_ENEMYAI_H
#define PUFO_ENEMYAI_H

#include "game/pufo_mapcell.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

/* EnemyAi_bMultiTargetTypeMatch @ 0x4b928.
 *
 * Decides whether `pUnit`'s unit type is an acceptable multi-target for `pOther`,
 * based on `pOther->nMultiTarget` (the target's role code). The five cases:
 *
 *     nMultiTarget 1 -> flag0[type] == 1 AND large[type] == 0
 *     nMultiTarget 2 -> flag0[type] == 0 AND large[type] == 0
 *     nMultiTarget 3 -> flag0[type] == 1              (large allowed)
 *     nMultiTarget 4 -> flag0[type] == 0              (large allowed)
 *     anything else  -> true
 *
 * Cases 1 and 2 additionally require the unit to be SMALL; 3 and 4 test only the
 * flag column. That asymmetry is the original's and is reproduced.
 *
 * Note the argument order in the original: `unused` (a context pointer the body
 * ignores), `pUnit` (the candidate), `pOther` (the one whose role is read). The port
 * drops the unused first argument. */
bool EnemyAi_MultiTargetTypeMatch(const Soldier *pUnit, const Soldier *pOther);

/* EnemyAi_EnqueueRandomTurn @ 0x4ab98.
 *
 * Picks a random facing in 0..7 and enqueues a turn, then kicks the queue. The RNG
 * draw is injected so the caller owns the deterministic stream (the original uses
 * Game_RandU16). */
typedef struct EnemyAiHooks {
    u32  (*pfnRandU16)(u32 nMax, void *pUser);
    /* TactState_EnqueueTurn @ 0x49b84 plus TactState_KickQueue @ 0x4f334. Both are
     * called together in the original, so they are one hook. */
    void (*pfnEnqueueTurnAndKick)(Soldier *pUnit, u8 nFacing, void *pUser);
    void *pUser;
} EnemyAiHooks;

void EnemyAi_EnqueueRandomTurn(Soldier *pUnit, const EnemyAiHooks *pHooks);

/* The facing range the random turn draws from. */
#define PUFO_ENEMYAI_FACING_MAX 8u

/* EnemyAi_TryFireOrWander @ 0x4abc8: the low-morale / panic AI.
 *
 * A LARGE unit does nothing at all — 0x4abdc branches straight to the function
 * epilogue, so it does not even push the status message.
 *
 * Everything else runs one of two paths, chosen at 0x4ac94 by `Game_RandU16(2)`
 * and by whether the unit holds a usable ONE-HANDED weapon:
 *
 *     roll == 0 AND a usable weapon  -> the random fire burst
 *     otherwise                      -> drop the weapons and wander
 *
 * A usable weapon is a hand that is non-NULL, whose ObjectRec.bTwoHanded is 0, and
 * which either has ammo or whose ObjectRec.bAmmoCap is 255. The left hand is tried
 * first, the right second, and the winner is stored into `Map.pSelWeapon` (0x113A00),
 * which is cleared to NULL first at 0x4AC20.
 *
 * Both paths raise the AI-action pair unconditionally BEFORE the choice:
 * `pMapTactical->pAiActionUnit = pUnit` (0x4AC04) and `bAiActionActive = 1`
 * (0x4AC1C). That is why the flag cannot mean "a wander is running".
 *
 * Both paths end at 0x4B028 pushing "<unit name>" through StatusMsg_Push with
 * bForce = 0. The original strcat's a second string from `unk_104078`, but that
 * buffer is all zeros and its only three cross-references in the whole binary are
 * this one call site — nothing ever writes it — so the message is just the name.
 *
 * ## The fire burst (0x4aca4..0x4ae28)
 *
 * `Map.nActionMode = 1`, then `Ui_FillActionCosts`. The shot count is
 * `bAmmoCap == 255 ? 3 : min(nAmmo, 3)`, and a count of 0 skips the loop entirely.
 * Each pass draws a target `x = pCell->nX + rand(5) - 2`, `y = pCell->nY + rand(5) - 2`,
 * `layer = rand(4)` and retries until `Map_IsValidCell` accepts it. Then:
 *
 *   - if the drawn x is NOT the unit's own column, enqueue at (drawn x, drawn y, layer);
 *   - if it IS the unit's own column, the y is replaced by the unit's own y, and then
 *     **a unit standing on row 0 fires at its own cell while a unit on any other row
 *     redraws from scratch without spending a shot** (0x4ada4 `BNE loc_4ACE0`).
 *
 * That last rule is the one Hex-Rays gets wrong: its pseudocode falls through to the
 * shot decrement, but the ARM jumps back to the random draw, so a same-column draw
 * costs nothing and the loop simply tries again.
 *
 * Each burst entry takes `nTuCost = Map.anActionCost[mode]` (Map+0x4E4C) and
 * `nArg = Map.anActionEnabled[mode]` (Map+0x4E7C) — the same two tables
 * `Tact_TryFireMode4` and `Input_OnPointerUp` read. After the loop the action latch is
 * cleared and the queue is kicked.
 *
 * ## The wander (0x4ae2c..0x4b024)
 *
 * `nUnitType <= 3` first drops both hands: from the unit's own layer it walks DOWN
 * while `MapCell.pFloorSpr == NULL` and stops at the first cell that has a floor —
 * testing layers nZ..1 only, so a unit with no floor anywhere ends on layer 0
 * untested. Both items go into that cell through `MapCell_ListInsertByPri`, the hands
 * are cleared, and `Unit_UpdateAnimPose` runs. Types 4..23 skip the whole block.
 *
 * Then it draws a target `x = pCell->nX + rand(20) - 10`, `y = pCell->nY + rand(20) - 10`
 * and retries until `Map_IsValidCell(map, x, y, 0)` accepts it — note the layer is
 * always 0 for the bounds test. The candidate cell is taken at the UNIT'S OWN layer.
 * If `g_aUnitTypeFlag0[type] == 0` the candidate is walked DOWN while `pFloorSpr ==
 * NULL`, and this walk tests layers nZ..0 inclusive — one layer deeper than the drop
 * walk above. The draw is retried while the candidate is the unit's own cell or
 * `MapCell_HasOccupant` says it is occupied.
 *
 * Finally `Unit_PathToCell(map, cell->nX, cell->nY, cell->nZ, 1)`. On failure the unit
 * is left with `uTu = 0` and `bAiActionActive` is cleared; on success the flag stays up
 * and the queue walker's completion tail (0x544D8) is what clears it.
 */
typedef struct EnemyAiActCtx {
    u32     (*pfnRandU16)(u32 nMax, void *pUser);        /* Game_RandU16 @ 0x3e59c */
    /* Unit_PathToCell (0x2c2a8, unported — it needs Unit_CanStepTo, 0x27bb8). */
    bool    (*pfnPathToCell)(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser);
    void    (*pfnFillActionCosts)(void *pUser);          /* Ui_FillActionCosts @ 0x26104 */
    void    (*pfnPushStatus)(const char *pszMsg, void *pUser); /* StatusMsg_Push @ 0x2dda4 */
    void    (*pfnFaceToward)(Soldier *pUnit, i32 nX, i32 nY, void *pUser);
    MapCell *(*pfnLookupCell)(i32 nX, i32 nY, i32 nLayer, void *pUser);

    TactState     *pState;          /* pMapTactical @ 0x157198 */
    const MapGeom *pGeom;
    TactItem     **ppSelWeapon;     /* Map.pSelWeapon @ Map+0x48 (0x113A00) */
    i32           *pnActionMode;    /* Map.nActionMode @ Map+0x4C (0x113A04) */
    const i32     *anActionCost;    /* Map+0x4E4C, 12 slots */
    const i32     *anActionEnabled; /* Map+0x4E7C, 12 slots */
    u8            *pbActionLatch;   /* Map.bActionLatch @ Map+0x2 (0x1139BA) */
    void           *pUser;
} EnemyAiActCtx;

void EnemyAi_TryFireOrWander(Soldier *pUnit, const EnemyAiActCtx *pCtx);

/* Game_RandU16(2): only a draw of 0 takes the fire path. */
#define PUFO_ENEMYAI_FIRE_ROLL_MAX 2u
/* nUnitType <= 3 drops both hands before wandering. */
#define PUFO_ENEMYAI_DROP_TYPE_MAX 3u
/* The burst never runs more than three shots, whatever the magazine says. */
#define PUFO_ENEMYAI_SHOTS_MAX 3u
/* Fire-burst draw: rand(nX + 5) - 2, layer rand(4). */
#define PUFO_ENEMYAI_FIRE_SPAN 5
#define PUFO_ENEMYAI_FIRE_DROP 2
#define PUFO_ENEMYAI_FIRE_LAYER_MAX 4
/* Wander draw: rand(nX + 20) - 10. */
#define PUFO_ENEMYAI_WANDER_SPAN 20
#define PUFO_ENEMYAI_WANDER_DROP 10
/* The original's pszMsg scratch, 0xD8 - 0x10 on the stack. */
#define PUFO_ENEMYAI_MSG_SIZE 200

#endif /* PUFO_ENEMYAI_H */
