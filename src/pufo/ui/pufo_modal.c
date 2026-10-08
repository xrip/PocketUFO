/* pufo_modal.c - see pufo_modal.h for evidence. */
#include "ui/pufo_modal.h"

#include <string.h>

void UiModal_Begin(UiModalState *pModal, i32 n12, i32 n13, i32 n14, i32 n15,
                   void (*pfn16)(void), void *p17, void (*pfn18)(void))
{
    if (pModal == NULL) {
        return;
    }
    /* The original clears the dword at pCtx+48 and anActionPhaseSave0[112]. */
    pModal->nField48 = 0;
    pModal->nActionPhaseSave112 = 0;
    pModal->bOpen = true;   /* LOBYTE(apUiSlotsA[19]) = 1 */
    pModal->anRect12_15[0] = n12;
    pModal->anRect12_15[1] = n13;
    pModal->anRect12_15[2] = n14;
    pModal->anRect12_15[3] = n15;
    pModal->pfn16 = pfn16;
    pModal->p17 = p17;
    pModal->pfn18 = pfn18;
}

void UiModal_End(UiModalState *pModal)
{
    if (pModal == NULL) {
        return;
    }
    /* Only the open flag is cleared; the stored fields survive. */
    pModal->bOpen = false;
}
