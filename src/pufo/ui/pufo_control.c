/* pufo_control.c - the UiControl button widget.
 *
 * Evidence: see pufo_control.h for the address list and the mode/state machine.
 */
#include "ui/pufo_control.h"

#include <string.h>

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
