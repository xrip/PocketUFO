/* pufo_enemyai.c - the enemy AI's decision predicates.
 *
 * Evidence: see pufo_enemyai.h for the address list and the table decode.
 */
#include "game/pufo_enemyai.h"

bool EnemyAi_MultiTargetTypeMatch(const Soldier *pUnit, const Soldier *pOther)
{
    u8 nRole;
    u8 bFlag0;

    if (pUnit == NULL || pOther == NULL) {
        return false;
    }
    /* The original indexes the 4-byte-stride table with no bounds check. An
     * out-of-range type would read past it; the port refuses instead and documents
     * that (deviation 1). */
    if (pUnit->nUnitType >= PUFO_UNIT_TYPE_COUNT) {
        return false;
    }

    /* The role code is read from the OTHER unit, not the candidate. */
    nRole = (u8)pOther->nMultiTarget;
    bFlag0 = g_aUnitTypeFlag0[pUnit->nUnitType];

    switch (nRole) {
        case 1:
            /* Flag set AND small. */
            return bFlag0 == 1 && g_aUnitTypeIsLarge[pUnit->nUnitType] == 0;
        case 2:
            /* Flag clear AND small. */
            return bFlag0 == 0 && g_aUnitTypeIsLarge[pUnit->nUnitType] == 0;
        case 3:
            /* Flag set; size is NOT tested here. */
            return bFlag0 == 1;
        case 4:
            /* Flag clear; size is NOT tested here. */
            return bFlag0 == 0;
        default:
            /* Every other role accepts any type. */
            return true;
    }
}

void EnemyAi_EnqueueRandomTurn(Soldier *pUnit, const EnemyAiHooks *pHooks)
{
    u32 nFacing;

    if (pUnit == NULL || pHooks == NULL || pHooks->pfnRandU16 == NULL) {
        return;
    }

    /* The draw comes first, then the enqueue, then the kick -- which in the original
     * are two separate calls performed back to back, so they are one hook here. */
    nFacing = pHooks->pfnRandU16(PUFO_ENEMYAI_FACING_MAX, pHooks->pUser);
    if (pHooks->pfnEnqueueTurnAndKick != NULL) {
        pHooks->pfnEnqueueTurnAndKick(pUnit, (u8)nFacing, pHooks->pUser);
    }
}
