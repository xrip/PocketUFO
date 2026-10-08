/* pufo_modal.h - the modal yes/no dialogue state.
 *
 * Evidence: UiModal_Begin @ 0x26430, UiModal_End @ 0x2648c.
 *
 * The original keeps the modal in `pCtxBase.apUiSlotsA`: slot 19's low byte is
 * the open flag, slots 12..15 the four rect/label ints, slots 16 and 18 the two
 * callbacks and slot 17 the extra (label-string) argument. `UiModal_Begin` also
 * clears the dword at pCtx+48 and `anActionPhaseSave0[112]`, the two scratch cells
 * that a modal session uses.
 *
 * The original functions live in the big UI bank and take an implicit context;
 * the port takes a `UiModalState *` instead, so the state can be tested without
 * the full game context.
 */
#ifndef PUFO_MODAL_H
#define PUFO_MODAL_H

#include "pufo_common.h"

typedef struct UiModalState {
    i32 nField48;             /* pCtx+48, cleared on begin */
    i32 nActionPhaseSave112;  /* anActionPhaseSave0[112], cleared on begin */
    bool bOpen;               /* apUiSlotsA[19] low byte */
    i32 anRect12_15[4];       /* apUiSlotsA[12..15] */
    void (*pfn16)(void);      /* apUiSlotsA[16] */
    void *p17;                /* apUiSlotsA[17] */
    void (*pfn18)(void);      /* apUiSlotsA[18] */
} UiModalState;

/* UiModal_Begin @ 0x26430. Clears the two scratch cells, sets the open flag and
 * stores the four ints, two callbacks and the extra argument. */
void UiModal_Begin(UiModalState *pModal, i32 n12, i32 n13, i32 n14, i32 n15,
                   void (*pfn16)(void), void *p17, void (*pfn18)(void));

/* UiModal_End @ 0x2648c. Clears only the open flag; the stored fields survive. */
void UiModal_End(UiModalState *pModal);

#endif /* PUFO_MODAL_H */
