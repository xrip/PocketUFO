/* pufo_enemyai.h - the enemy AI's decision predicates.
 *
 * Evidence: EnemyAi_bMultiTargetTypeMatch @ 0x4b928,
 *           EnemyAi_EnqueueRandomTurn @ 0x4ab98,
 *           EnemyAi_TryFireOrWander @ 0x4abc8,
 *           Tact_EnemyAiTick @ 0x4cbe0.
 *
 * ## The unit-type table
 *
 * `byte_A7B80` @ 0xa7b80 is a 4-byte-stride table indexed by unit type, 24 rows. Three of
 * its columns are used by the AI:
 *
 *     byte[4*type + 0]   "flag 0"    (g_aUnitTypeFlag0)
 *     byte[4*type + 1]   "is large"  (g_aUnitTypeIsLarge)
 *     byte[4*type + 3]   "melee actor" (g_aUnitTypeFlag3)
 *
 * Decoded from the 96 bytes in the image:
 *
 *     type :  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
 *     flag0:  0  0  0  1  0  0  0  1  1  0  0  1  0  1  0  0
 *     large:  0  0  0  0  1  1  1  1  1  0  0  0  0  0  0  0
 *     flag3:  0  0  0  0  0  0  0  0  0  0  0  0  0  0  1  1
 *
 *     type : 16 17 18 19 20 21 22 23
 *     flag0:  0  0  1  0  0  0  0  0
 *     large:  0  0  1  0  1  1  0  0
 *     flag3:  1  1  0  1  1  0  0  0
 *
 * (An earlier copy of this table carried flag0's 1s at types 11 and 13 into the `large`
 * row; the re-dump says both rows are 0 there, and it is the `flag0` row that is 1.)
 */
#ifndef PUFO_ENEMYAI_H
#define PUFO_ENEMYAI_H

#include "game/pufo_mapcell.h"
#include "game/pufo_rmp.h"   /* RmpData, RmpNode */
#include "game/pufo_tactstate.h"
#include "game/pufo_turn.h"  /* TactCtx272, TurnHooks, PUFO_PHASE_ENEMY_AI */
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
    /* Unit_PathToCell @ 0x2c2a8, ported in pufo_path.c with a matching signature; its
     * pUser is a PathToCellCtx. Kept as a seam so the AI is testable without a grid. */
    bool    (*pfnPathToCell)(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser);
    void    (*pfnFillActionCosts)(void *pUser);          /* Ui_FillActionCosts @ 0x26104 */
    void    (*pfnPushStatus)(const char *pszMsg, void *pUser); /* StatusMsg_Push @ 0x2dda4 */
    /* The TEXTS-record form of the same sink. The enqueue callees push record 1 themselves when the
     * unit cannot pay (TactState_EnqueueFireBurst 0x4B0D8, TactState_EnqueueThrow 0x4B318), and the
     * port's helpers only report the failure, so the AI call sites push through here. An index rather
     * than a string because the port has no raw text-record accessor. */
    void    (*pfnPushTextRecord)(i32 nTextRecordIndex, void *pUser);
    void     *pPushTextUser;
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

/* Tact_EnemyAiTick @ 0x4cbe0: the enemy turn's per-unit driver.
 *
 * It is the phase-3 arm of the action-phase machine (Tact_ProcessActionPhase @ 0x4d950
 * calls it through TurnHooks.pfnEnemyAiTick) and it handles ONE unit per call.
 *
 * ## Unit selection (0x4cc1c..0x4cc5c)
 *
 * `nEnemyUnitCount == 0` goes straight to the tail (0x4CC18 `BLS`). Otherwise the enemy
 * roster is walked in order and the first unit that passes every gate acts:
 *
 *     nAiTick > 15   -> skipped WITHOUT incrementing, so it stays skipped forever
 *     ++nAiTick
 *     bAiDone        -> skipped
 *     uTu >= 15      -> this unit acts
 *     otherwise      -> bAiDone = 1, skipped
 *
 * If no unit acts the function still runs its tail.
 *
 * ## What happens for the acting unit
 *
 * 1. `Unit_UpdateVisibility(pUnit)`, then `pTactCtx272->pTrackUnit = NULL` unless it
 *    already points at this unit (0x4CC78).
 * 2. `bMorale < 20 && nUnitType > 3` -> the panic AI: Tact_SelectUnit,
 *    EnemyAi_TryFireOrWander, focus saved, bAiDone = 1, tail (0x4CC94).
 * 3. Multi-target pick (0x4ccd0..0x4cd4c): up to three draws of
 *    `apMultiTargets[rand(nMultiTarget)]`, stopping at the first one that is not large.
 * 4. The pick is accepted when it is small, not type 16/17, `rand(5) == 0`, and the enemy
 *    roster is non-empty (0x4cd60..0x4cda0). On acceptance the function looks for ANOTHER
 *    enemy with `bPsiActive != 0`, `uTu > 15` and hands the action to THAT unit:
 *    Tact_SelectUnit(it), Map.pSelWeapon = &TactItem_156910, mode 4 or 5 by `rand(2)`,
 *    Map.nBurstShots = 0, the melee cell = the multi-target's cell, a command-18 enqueue
 *    against that cell's occupant, and then `Map_pSelUnit->uTu >>= 1`
 *    (0x4d660..0x4d75c). So the AI spends half the acting unit's TU on a different unit's
 *    melee attack.
 * 5. Otherwise (LABEL_24, 0x4cda4): `nMultiTarget == 0` jumps to the fallback move; else
 *    Tact_SelectUnit(this unit) and the branch splits on `g_aUnitTypeFlag3[type]`
 *    (0x4CDBC, set only for Chryssalid, Celacid, both civilians, Silacoid and Riper).
 *
 * ## The melee branch (0x4cde0..0x4d65c)
 *
 * Three attempts. Each draws a fresh multi-target `pTarget`, loads the cell one row north
 * of the unit's own cell and the two other cells of a large unit's 2x2 footprint, and
 * builds a candidate cell: the cell at the unit's facing delta on the TARGET's layer,
 * falling back to the target's own cell when that cell is out of bounds or is not occupied
 * by the target. Reach is `(int)sqrt(dx^2 + dy^2) < 2` from the unit's own cell, or -- for
 * a large unit -- from any of its four footprint cells. On reach: `uTu = 0`, the three
 * melee-cell globals, and a command-18 enqueue against the target.
 *
 * On no reach, eight directions are scanned (0x4d1d0..0x4d330): the layer is `nZ + 1` when
 * the unit's own ceiling sprite has `iCeilParam < -15`, otherwise `nZ`; an occupant counts
 * when `bStatus == 0` or its type is 16 or 17. A hit faces the unit and enqueues command
 * 13. Otherwise `Unit_PathToCell(target cell, bFlag = 0)`; success ends the tick.
 *
 * ## The throw / fire branch (0x4d360..0x4d8b0)
 *
 * A fresh multi-target and `rand(3)`. The throw needs roll 0, distance <= 10, a non-NULL
 * `apBelt[0]` whose object has `nCategory == 3`, and `nUnitType > 3`. It moves the belt
 * item into `pHandR` and into Map.pSelWeapon, refills the action costs, sets the item's +7
 * to 0 and +8 to 1, sets action mode 11, and enqueues the throw with `Map_nThrowTuCost` /
 * `Map_nThrowArg` -- which are just anActionCost[11] and anActionEnabled[11], loaded by
 * literal at 0x4D82C and 0x4D80C.
 *
 * Otherwise the weapon is the left hand for types > 3 (refusing when it is empty), or for
 * types <= 3 the last hand holding a one-handed object with `nCategory <= 2` (the right
 * hand wins ties). After `Ui_FillActionCosts` and `TactItem_bTryReloadClip`, the mode is 7
 * when the weapon object's `uSortKey == 13`, the target is not type 16/17 and the distance
 * is >= 10 -- and mode 7 also arms Map.nMultiTargetCount = 1, Map.nBurstShots = 1 and
 * Map.apMultiTargets[0]. Otherwise mode 1.
 *
 * The affordability gate is a soft-float comparison at 0x4d448/0x4d458:
 *
 *     (double)uTu >= anActionCost[mode] * (2.0 / 3.0) + 8.0
 *
 * and the charged cost is the truncated `(int)(cost * 2/3)`, not the table value.
 *
 * ## The fallback move (LABEL_82, 0x4d8b8..0x4d918)
 *
 * Rank 4 or 8 draws `rand(6)` and keeps it only when it is 1; every other rank draws
 * `rand(3)`. A draw of 0 turns the unit through EnemyAi_EnqueueRandomTurn and marks it
 * done. Otherwise the RMP node walk starts at `rand(nRmpCount)` and, because the index is
 * incremented BEFORE it is used, that first draw is never itself a candidate; the index
 * wraps at `nRmpCount - 1`. A node is a candidate when `sqrt(dx^2 + dy^2) <= 19.875` and
 * the node's role byte (RmpNode+0x1A, which the original reaches by casting the node to a
 * Soldier* so it lands on Soldier.nMultiTarget) matches the unit type. On a candidate the
 * unit is selected and `Unit_PathToCell(node, bFlag = 1)` runs; success saves the focus,
 * failure counts toward a limit of 10, past which the unit is marked done. After
 * `nRmpCount` nodes have been scanned the tick moves on to the NEXT enemy unit -- the only
 * path that does not end the loop.
 *
 * ## The tail (0x4d920)
 *
 * `pTactCtx272->nPhase = 3`, then `Tact_EndEnemyTurn` only when the focus byte at +0x100
 * is still 0, then `TactState_KickQueue(pMapTactical)`. Every path that queued something
 * sets that byte, so the enemy turn continues; a tick where nothing acted ends the turn. */
typedef struct EnemyAiTickCtx {
    /* Seams. Unit_PathToCell @ 0x2c2a8 (ported in pufo_path.c, signature matches),
     * Ui_FillActionCosts @ 0x26104, Unit_UpdateVisibility @ 0x48580 (ported, but it needs a
     * whole VisCtx), AfterEnemyStep (0x4c4a0, unported) and the clip lookup inside
     * TactItem_bTryReloadClip are injected. pfnEnqueueTurnAndKick is the same pair
     * EnemyAi_EnqueueRandomTurn already uses. */
    u32   (*pfnRandU16)(u32 nMax, void *pUser);        /* Game_RandU16 @ 0x3e59c */
    bool  (*pfnPathToCell)(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser);
    void  (*pfnFillActionCosts)(void *pUser);
    void  (*pfnUpdateVisibility)(Soldier *pUnit, void *pUser);
    void  (*pfnEnqueueTurnAndKick)(Soldier *pUnit, u8 nFacing, void *pUser);
    void  (*pfnAfterEnemyStep)(TactCtx272 *pCtx, void *pUser);
    u8    (*pfnClipAmmoCap)(u16 nAmmoTypeId, void *pUser);
    MapCellLookup pfnLookup;

    MapGeom          geom;             /* nMapDimX @ 0x1511B, nMapDimY @ 0x1511C */
    TactState       *pQueue;           /* pMapTactical @ 0x157198 */
    TactSelectCtx   *pSelect;          /* Tact_SelectUnit @ 0x2c6c4 */
    const TurnHooks *pTurnHooks;       /* handed to Tact_EndEnemyTurn @ 0x4cb74 */
    Soldier        **apEnemyUnits;     /* Map_apEnemyUnits @ 0x128B84 */
    const u8        *pnEnemyCount;     /* nEnemyUnitCount @ 0x128ADC */
    Soldier        **ppSelUnit;        /* Map_pSelUnit @ 0x128CEC (Map + 0x15334) */
    Soldier         *pFirstXcom;       /* Map_apXcomUnits[0] @ 0x128B0C */
    TactItem      **ppSelWeapon;       /* Map.pSelWeapon @ Map+0x48 (0x113A00) */
    i32            *pnActionMode;      /* Map.nActionMode @ Map+0x4C (0x113A04) */
    i32            *pnMultiTargetCount;/* Map.nMultiTargetCount @ Map+0x04 (0x1139BC) */
    MapCell       **apMultiTargets;    /* Map.apMultiTargets[8] @ Map+0x08 (0x1139C0) */
    i32            *pnBurstShots;      /* Map.nBurstShots @ Map+0x28 (0x1139E0) */
    i32            *pnMeleeCellX;      /* Map_nMeleeCellX @ 0x1279F8 */
    i32            *pnMeleeCellY;      /* Map_nMeleeCellY @ 0x1279FC */
    i32            *pnMeleeCellLayer;  /* Map_nMeleeCellLayer @ 0x127A00 */
    const i32       *anActionCost;     /* Map_anActionCost @ 0x118804, 12 slots */
    const i32       *anActionEnabled;  /* Map_anActionEnabled @ 0x118834, 12 slots */
    TactItem        *pItemMelee;       /* TactItem_156910 @ 0x156910, the mode 4/5 weapon */
    TactItem        *pItemAction13;    /* TactItem_156A58 @ 0x156A58, the command-13 item */
    const RmpData   *pRmp;             /* pRmpMissionData @ 0x15881C */
    const EnemyAiActCtx *pActCtx;      /* the panic AI's own context, for the low-morale path */
    /* Same sink as EnemyAiActCtx's. Of this tick's four enqueues only the throw can refuse: the
     * three Tact_EnqueueActionOrStatus205 calls at 0x4D718, 0x4D790 and 0x4D7BC are given nTuCost 0
     * (`MOV R3, #0` at 0x4D77C and 0x4D7B4) so their gate at 0x4B42C cannot fire, and the fire burst
     * at 0x4D910 sits behind the site gate at 0x4d448 (uTu >= cost*(2/3) + 8.0) while charging only
     * (int)(cost*(2/3)), so 0x4B0D8 cannot fail there either. The throw at 0x4D844 is handed slot 11
     * of the cost table with no TU test on the way to it. */
    void (*pfnPushTextRecord)(i32 nTextRecordIndex, void *pUser);
    void  *pPushTextUser;
    void             *pUser;
} EnemyAiTickCtx;

void Tact_EnemyAiTick(TactCtx272 *pTact, const EnemyAiTickCtx *pCtx);

/* Unit gates. nAiTick above this is skipped without being incremented; uTu at or above
 * this is what lets a unit act, and strictly above it is what makes another enemy a valid
 * melee neighbour (0x4CC34, 0x4CC54, 0x4D680). */
#define PUFO_ENEMYAI_TICK_MAX 15
#define PUFO_ENEMYAI_TU_MIN   15
/* bMorale strictly below this, with nUnitType > 3, sends the unit to the panic AI. */
#define PUFO_ENEMYAI_MORALE_MAX 20
/* Three draws of a non-large multi-target, then a 1-in-5 roll must come up 0. */
#define PUFO_ENEMYAI_MULTI_PICK_TRIES   3
#define PUFO_ENEMYAI_MULTI_REJECT_ROLL  5
/* Three melee attempts; reach is a truncated cell distance strictly below 2. */
#define PUFO_ENEMYAI_MELEE_TRIES 3
#define PUFO_ENEMYAI_MELEE_REACH 2
/* The eight compass directions the melee scan walks (the original loops j = 0..21 step 3
 * over aDirDeltaXY; the table's entries 8 and 9 are the vertical steps and are not used). */
#define PUFO_ENEMYAI_DIR_SCAN 8
/* A ceiling sprite below this raises the eight-direction scan one layer (0x4D230). */
#define PUFO_ENEMYAI_CEIL_RAISE (-15)
/* Throw: rand(3) == 0, distance at most 10, belt item in object category 3. */
#define PUFO_ENEMYAI_THROW_ROLL     3
#define PUFO_ENEMYAI_THROW_DIST_MAX 10
#define PUFO_ENEMYAI_THROW_CATEGORY 3
/* The multi-target burst needs the target at this distance or further (the `v65 >= 10`
 * test in the mode-7 arm, 0x4d770 region). */
#define PUFO_ENEMYAI_MULTI_DIST_MIN 10
/* The two action modes the neighbour-melee path picks between with rand(2) (0x4D6AC), and
 * the roll itself. They are the same two slots Tact_TryFireMode4/5 @ 0x179e4/0x17b1c arm
 * from the action pad. */
#define PUFO_ENEMYAI_NEIGHBOUR_ROLL 2u
#define PUFO_ENEMYAI_MODE_NEIGHBOUR_A 4
#define PUFO_ENEMYAI_MODE_NEIGHBOUR_B 5
/* A hand weapon is usable for a small unit when it is one-handed and its object's
 * category is at most this. */
#define PUFO_ENEMYAI_WEAPON_CATEGORY_MAX 2
/* The weapon object sort key that means "this weapon can burst". */
#define PUFO_ENEMYAI_SORTKEY_MULTI 13
/* The soft-float fire gate: uTu >= cost * 2/3 + 8.0, charged at (int)(cost * 2/3).
 * 0x3FE5555555555555 and 0x4020000000000000 in the constant pool at 0x4d448/0x4d458. */
#define PUFO_ENEMYAI_FIRE_COST_SCALE (2.0 / 3.0)
#define PUFO_ENEMYAI_FIRE_COST_BIAS  8.0
/* Ranks 4 and 8 draw rand(6) and keep only a 1; every other rank draws rand(3). */
#define PUFO_ENEMYAI_RANK_LEADER    4
#define PUFO_ENEMYAI_RANK_COMMANDER 8
#define PUFO_ENEMYAI_RANK_ROLL      6
#define PUFO_ENEMYAI_RANK_KEEP      1
#define PUFO_ENEMYAI_MOVE_ROLL      3
/* RMP walk: candidate radius 19.875 cells, and 10 failed paths before the unit is done. */
#define PUFO_ENEMYAI_RMP_DIST_MAX 19.875
#define PUFO_ENEMYAI_RMP_FAIL_MAX 10
/* The mode the melee branch enqueues through TactState_EnqueueAction18. */
#define PUFO_ENEMYAI_CMD13 13

#endif /* PUFO_ENEMYAI_H */
