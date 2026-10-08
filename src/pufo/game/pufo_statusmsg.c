/* pufo_statusmsg.c - the on-screen status message stack.
 *
 * Evidence: see pufo_statusmsg.h for the addresses and the push policy.
 */
#include "game/pufo_statusmsg.h"

#include <string.h>

#include "util/pufo_str.h"

void StatusMsg_Shift(StatusStack *pStack)
{
    if (pStack == NULL || pStack->nCount == 0) {
        return;
    }
    /* Records i-1 <- i for i = 1..count-1, both text and TTL. Index 0 is the
     * OLDEST record, so this drops it. */
    for (i32 i = 1; i < pStack->nCount; ++i) {
        pStack->anTtl[i - 1] = pStack->anTtl[i];
        memmove(pStack->aszMsg[i - 1], pStack->aszMsg[i], PUFO_STATUS_REC_BYTES);
    }
    pStack->nCount -= 1;
}

/* Stores the message at index `nCount` and bumps the count and TTL. The original
 * copies with a strcpy loop, so an over-long message overruns its 100-byte slot;
 * the port truncates at 99 bytes and documents that (deviation 1). */
static void Status_Store(StatusStack *pStack, const char *pszMsg)
{
    i32 nLast = PUFO_STATUS_REC_BYTES - 1;

    if (pStack->nCount < 0 || pStack->nCount >= PUFO_STATUS_MAX) {
        return;
    }
    pufo_strncpy(pStack->aszMsg[pStack->nCount], pszMsg != NULL ? pszMsg : "",
                 (size_t)nLast);
    pStack->aszMsg[pStack->nCount][nLast] = '\0';
    pStack->anTtl[pStack->nCount] = PUFO_STATUS_TTL;
    pStack->nCount += 1;
}

bool StatusMsg_Push(StatusStack *pStack, const char *pszMsg, bool bForce,
                    const StatusHooks *pHooks)
{
    bool bBusy;

    if (pStack == NULL) {
        return false;
    }
    bBusy = (pHooks != NULL && pHooks->pfnIsBusy != NULL)
                ? pHooks->pfnIsBusy(pHooks->pUser)
                : false;

    /* The outer gate: a forced push always goes through. */
    if (!bForce && bBusy) {
        return false;
    }

    if (pStack->nCount < PUFO_STATUS_APPEND_MAX) {
        /* Below three messages, a push never discards anything. */
        Status_Store(pStack, pszMsg);
    } else if (!bBusy || pStack->nCount >= PUFO_STATUS_MAX) {
        /* At three or more: drop the oldest, but only when not busy or the
         * buffer is completely full. */
        StatusMsg_Shift(pStack);
        Status_Store(pStack, pszMsg);
    } else {
        /* Busy with 3..14 showing: the message is discarded. This is the
         * original's asymmetry, not a bug in the port. */
        return false;
    }

    /* The repaint is conditional on the display flag, in both arms of the
     * original (there is a single shared tail). */
    if (pHooks != NULL && pHooks->pfnShouldRender != NULL
        && pHooks->pfnShouldRender(pHooks->pUser)
        && pHooks->pfnRender != NULL) {
        pHooks->pfnRender(pHooks->pUser);
    }
    return true;
}

i32 StatusMsg_DrawStack(StatusStack *pStack, i32 *pnOutDrawY,
                        const StatusDrawHooks *pHooks)
{
    i32 nDrawY;

    if (pStack == NULL || pHooks == NULL) {
        return 0;
    }

    /* The colour pair is packed before anything else, unconditionally. */
    if (pHooks->pfnSetColor != NULL) {
        pHooks->pfnSetColor(255, 255, 55, 90, 90, 10, pHooks->pUser);
    }

    nDrawY = 320;

    /* The hud-pad branch: all four conditions must hold, otherwise the base stays
     * at 320. */
    if (pHooks->pfnPlayerInputEnabled != NULL
        && pHooks->pfnPlayerInputEnabled(pHooks->pUser)
        && pHooks->pfnSelectedIsOperative != NULL
        && pHooks->pfnSelectedIsOperative(pHooks->pUser)
        && pHooks->pfnIsBusy != NULL && !pHooks->pfnIsBusy(pHooks->pUser)
        && pHooks->pfnHudPadOriginY != NULL) {
        nDrawY = pHooks->pfnHudPadOriginY(pHooks->pUser);
    }

    /* Always one 14-pixel step before the per-message 12-pixel steps. */
    nDrawY -= 14;

    /* Newest first: the first record drawn is aszMsg[count-1]. */
    for (i32 i = 0; i < pStack->nCount; ++i) {
        i32 nIndex = pStack->nCount - 1 - i;
        i32 y = nDrawY - 12 * i;

        if (pHooks->pfnDraw != NULL) {
            pHooks->pfnDraw(PUFO_STATUS_DRAW_X, y, pStack->aszMsg[nIndex], pHooks->pUser);
        }
    }

    if (pnOutDrawY != NULL) {
        *pnOutDrawY = nDrawY;
    }
    return pStack->nCount;
}
