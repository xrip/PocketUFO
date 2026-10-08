/* pufo_enemyai.h - the enemy AI's decision predicates.
 *
 * Evidence: EnemyAi_bMultiTargetTypeMatch @ 0x4b928,
 *           EnemyAi_EnqueueRandomTurn @ 0x4ab98.
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

#endif /* PUFO_ENEMYAI_H */
