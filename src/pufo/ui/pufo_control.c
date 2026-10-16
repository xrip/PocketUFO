/* pufo_control.c - the UiControl button widget.
 *
 * Evidence: see pufo_control.h for the address list and the mode/state machine.
 */
#include "ui/pufo_control.h"

#include <stdlib.h>  /* malloc/free, the pad allocations of UiControl_LoadPads */
#include <string.h>
#include "render/pufo_render.h" /* PadImage_ctor / FreeData / Load */

void UiControl_ctor(UiControl *pCtl)
{
    if (pCtl == NULL) {
        return;
    }
    memset(pCtl, 0, sizeof(*pCtl));
    pCtl->uWidth   = 10;
    pCtl->uHeight  = 10;
    pCtl->bEnabled = 1;
    pCtl->nMode    = 0;
    pCtl->nState   = 0;
    pCtl->pfnClick = NULL;
    pCtl->bUnk17   = 0;
}

void UiControl_FreePads(UiControl *pCtl)
{
    if (pCtl == NULL) {
        return;
    }
    for (i32 i = 0; i < PUFO_CONTROL_PADS; ++i) {
        if (pCtl->apPads[i] != NULL) {
            PadImage_FreeData(pCtl->apPads[i]);
            /* The original operator-deletes the PadImage next. The port cannot free
             * it here because the PadImage may be caller-owned; the caller releases
             * the struct. FreeData releases the pixel data, which is the part that
             * lives inside the pad. */
            pCtl->apPads[i] = NULL;
        }
    }
}

bool UiControl_BlitActivePad(UiControl *pCtl, UiControlBlitFn pfnBlit,
                             UiControlBlitFn pfnBlitEx, void *pUser)
{
    u8        nState;
    PadImage *pPad;

    if (pCtl == NULL) {
        return false;
    }

    /* A disabled control draws nothing, whatever the mode and state. */
    if (pCtl->bEnabled == 0) {
        return false;
    }

    switch (pCtl->nMode) {
        case 0:
            /* Mode 0 uses the state directly. */
            nState = pCtl->nState;
            break;
        case 1:
            /* Mode 1 clamps at the last real pad, so a state above 2 cannot index
             * past apPads[2]. */
            nState = pCtl->nState;
            if (nState >= PUFO_CONTROL_MAX_STATE) {
                nState = PUFO_CONTROL_MAX_STATE;
            }
            break;
        case 2:
            /* Mode 2 is inert: it draws nothing. */
            return false;
        default:
            /* An unknown mode is not in the original's switch; draw nothing rather
             * than guessing an index. */
            return false;
    }

    pPad = pCtl->apPads[nState];
    if (pPad == NULL) {
        return false;
    }

    /* The pad's position always follows its control. The original writes the two
     * dwords through unaligned stores, which is why it looks like a pair of
     * component writes. */
    pPad->nOriginX = pCtl->x;
    pPad->nOriginY = pCtl->y;

    if (pCtl->bUnk17 != 0) {
        if (pfnBlitEx != NULL) {
            pfnBlitEx(pPad, pUser);
        }
    } else if (pfnBlit != NULL) {
        pfnBlit(pPad, pUser);
    }
    return true;
}

/* UiControl_HitTest @ 0x1a3d8. See the header for the press/release table. */
bool UiControl_HitTest(UiControl *pCtl, i32 x, i32 y, bool bDown, bool bForceClick,
                       const UiControlHitCtx *pCtx)
{
    bool bHit = false;

    if (pCtl == NULL || pCtx == NULL || pCtl->bEnabled == 0) {
        return false;
    }

    /* 0x1A3F0: the stack argument is a force flag. It fires a mode-0 control that
     * is already in state 1, with no position test at all. */
    if (bForceClick && pCtl->nMode == 0 && pCtl->nState == 1) {
        if (pCtl->pfnClick != NULL) {
            pCtl->pfnClick();
        }
        return true;
    }

    /* 0x1A42C: the box is only measured on a press, and the far edge is exclusive
     * one pixel short of x + uWidth and y + uHeight. */
    if (bDown) {
        bHit = x >= (i32)pCtl->x
            && y >= (i32)pCtl->y
            && x < (i32)pCtl->x + (i32)pCtl->uWidth - 1
            && y < (i32)pCtl->y + (i32)pCtl->uHeight - 1;
    }

    if (bDown) {
        switch (pCtl->nMode) {
        case 0:
        case 2:
            if (bHit) {
                if (pCtl->nState == 0) {
                    pCtl->nState = 1;
                    (void)UiControl_BlitActivePad(pCtl, pCtx->pfnBlit,
                                                  pCtx->pfnBlitEx, pCtx->pUser);
                }
            } else if (pCtl->nState == 1) {
                pCtl->nState = 0;
                (void)UiControl_BlitActivePad(pCtl, pCtx->pfnBlit,
                                              pCtx->pfnBlitEx, pCtx->pUser);
            }
            /* 0x1A530: every mode 0/2 press path clears the UI slot. */
            if (pCtx->pnUiSlot0 != NULL) {
                *pCtx->pnUiSlot0 = 0;
            }
            return bHit;

        case 1:
            if (bHit) {
                if (pCtl->nState < 2) {
                    pCtl->nState = (u8)(pCtl->nState + 2);
                    (void)UiControl_BlitActivePad(pCtl, pCtx->pfnBlit,
                                                  pCtx->pfnBlitEx, pCtx->pUser);
                }
            } else if (pCtl->nState > 1) {
                /* 0x1A4EC: the release of a press is the one state change that does
                 * not redraw. */
                pCtl->nState = (u8)(pCtl->nState - 2);
            }
            return bHit;

        default:
            return bHit;
        }
    }

    /* Release. 0x1A540: the hit box is never consulted here. */
    switch (pCtl->nMode) {
    case 0:
    case 2:
        if (pCtl->nState != 1) {
            return false;
        }
        pCtl->nState = 0;
        break;

    case 1:
        if (pCtl->nState <= 1) {
            return false;
        }
        pCtl->nState = (pCtl->nState == 2) ? 1u : 0u;
        break;

    default:
        return false;
    }

    (void)UiControl_BlitActivePad(pCtl, pCtx->pfnBlit, pCtx->pfnBlitEx, pCtx->pUser);
    if (pCtl->pfnClick != NULL) {
        pCtl->pfnClick();
    }
    return true;
}

void UiControl_SetPosMode(UiControl *pCtl, i16 x, i16 y, u8 nMode)
{
    if (pCtl == NULL) {
        return;
    }
    pCtl->x = x;
    pCtl->y = y;

    /* Every mode except 2 adopts pad 1's size, so mode 2 is the one mode that keeps
     * whatever size the control already had. */
    if (nMode != 2) {
        PadImage *pPad1 = pCtl->apPads[1];

        if (pPad1 != NULL) {
            pCtl->uWidth  = pPad1->uWidth;
            pCtl->uHeight = pPad1->uHeight;
        }
    }
    pCtl->nMode  = nMode;
    pCtl->nState = 0;
}

/* UiControl_LoadPads @ 0x1a604. Free whatever the three pad slots hold, null them, then load
 * each non-empty descriptor name into a fresh PadImage in mode 1 (the mode is fixed at 1 here,
 * not the control's mode). Deviation: the original runs PadImage_Load on the allocation even
 * when operator new returned null; the port skips the load when the pad is NULL. The original
 * returns the third strcmp's value; no caller uses it, so the port returns void. */
void UiControl_LoadPads(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2)
{
    const char *const apsz[3] = { psz0, psz1, psz2 };

    if (pCtl == NULL) {
        return;
    }
    for (i32 i = 0; i < PUFO_CONTROL_PADS; ++i) { /* 0x1A60C..0x1A698 */
        if (pCtl->apPads[i] != NULL) {
            PadImage_FreeData(pCtl->apPads[i]);
            free(pCtl->apPads[i]);
            pCtl->apPads[i] = NULL;
        }
    }
    for (i32 i = 0; i < PUFO_CONTROL_PADS; ++i) { /* 0x1A6A4..0x1A754 */
        if (strcmp(apsz[i], "") != 0) {
            PadImage *pPad = (PadImage *)malloc(sizeof(PadImage));
            if (pPad != NULL) {
                PadImage_ctor(pPad);
            }
            pCtl->apPads[i] = pPad;
            if (pPad != NULL) {
                (void)PadImage_Load(pPad, apsz[i], 1);
            }
        }
    }
}

void UiControl_Setup(UiControl *pCtl, i16 xBase, i16 yBase, const UiControlDesc *pDesc,
                     bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0,
                                         const char *psz1, const char *psz2,
                                         void *pUser),
                     void *pUser)
{
    if (pCtl == NULL || pDesc == NULL) {
        return;
    }
    if (pfnLoadPads != NULL) {
        (void)pfnLoadPads(pCtl, pDesc->szPad0, pDesc->szPad1, pDesc->szPad2, pUser);
    }
    /* The base offset is added in 16-bit arithmetic, exactly as the original does. */
    UiControl_SetPosMode(pCtl, (i16)(xBase + pDesc->xOff), (i16)(yBase + pDesc->yOff),
                         pDesc->nMode);
}

void EmptyBtn_OnClick(void (*pfnPlaySample)(i32 nSample, void *pUser),
                      void (*pfnClearDrawMode)(void *pUser), void *pUser)
{
    if (pfnPlaySample != NULL) {
        pfnPlaySample(PUFO_CONTROL_SAMPLE_CLICK, pUser);
    }
    if (pfnClearDrawMode != NULL) {
        pfnClearDrawMode(pUser);
    }
}

/* --------------------------------------------------------------------------
 * The whole bank: UiBank_CtorAll @ 0x11320 and UiBank_FreeAllPads @ 0x1117c.
 * -------------------------------------------------------------------------- */

/* Read straight out of the two loop bounds. UiBank_CtorAll runs
 *   19 @ 0, 13 @ 19, 12 @ 32, 9 @ 44, 7 @ 53, 29 @ 60, 9 @ 89, 3 @ 98,
 *   3 @ 101, 1 @ 104, 8 @ 105, 15 @ 113, 1 @ 128, 1 @ 129
 * and UiBank_FreeAllPads runs the same fourteen groups in the opposite order,
 * counting down inside each one. The groups are the per-screen widget ranges the
 * hit test uses, so the bank really is fourteen screens' worth of widgets laid end
 * to end. 19+13+12+9+7+29+9+3+3+1+8+15+1+1 = 130, every slot exactly once. */
const UiBankSpan g_aUiBankSpans[PUFO_PBANK_SPAN_COUNT] = {
    {  0, 19}, { 19, 13}, { 32, 12}, { 44,  9}, { 53,  7}, { 60, 29},
    { 89,  9}, { 98,  3}, {101,  3}, {104,  1}, {105,  8}, {113, 15},
    {128,  1}, {129,  1},
};

UiWidgetBank *UiBank_CtorAll(UiWidgetBank *pBank, bool *pbActionFlag)
{
    if (pBank == NULL) {
        return NULL;
    }
    for (i32 g = 0; g < PUFO_PBANK_SPAN_COUNT; ++g) {
        const UiBankSpan *pGroup = &g_aUiBankSpans[g];
        for (i32 i = 0; i < (i32)pGroup->nCount; ++i) {
            UiControl_ctor(&pBank->aCtl[pGroup->nFirst + i]);
        }
    }
    /* 0x114B8 and 0x114C8: the two bank-level flags are cleared last. */
    pBank->bEscapeFlag = 0;
    if (pbActionFlag != NULL) {
        *pbActionFlag = false;   /* UiBank_bActionFlag @ 0xAD711 */
    }
    return pBank;
}

void UiBank_FreeAllPads(UiWidgetBank *pBank)
{
    if (pBank == NULL) {
        return;
    }
    for (i32 g = PUFO_PBANK_SPAN_COUNT - 1; g >= 0; --g) {
        const UiBankSpan *pGroup = &g_aUiBankSpans[g];
        for (i32 i = (i32)pGroup->nCount - 1; i >= 0; --i) {
            UiControl_FreePads(&pBank->aCtl[pGroup->nFirst + i]);
        }
    }
}

/* Bank_AtexitThunk @ 0x11170: the atexit entry Bank_RegisterAtexit installs. In the original it names
 * the global bank at 0xAC6D0 outright; the port's bank is caller-owned, so it releases the bank
 * Bank_RegisterAtexit was handed. */
static UiWidgetBank *g_pBankToFree;

void Bank_AtexitThunk(void)
{
    if (g_pBankToFree != NULL) {
        UiBank_FreeAllPads(g_pBankToFree); /* 0x11174 */
    }
}

int Bank_RegisterAtexit(UiWidgetBank *pBank)
{
    g_pBankToFree = pBank;
    return atexit(Bank_AtexitThunk); /* 0x11168 */
}
