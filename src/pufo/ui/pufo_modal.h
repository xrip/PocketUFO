/* pufo_modal.h - the modal yes/no dialogue state.
 *
 * Evidence: UiModal_Begin @ 0x26430, UiModal_End @ 0x2648c.
 *
 * The original keeps the modal in `pCtxBase.apUiSlotsA`: slot 19's low byte is
 * the open flag, slot 12 the dialog width, slot 13 the first text record, slot 14
 * the line count, slots 15 and 17 the two button labels, slots 16 and 18 the two
 * button callbacks. `UiModal_Begin` also clears slot 24 (Map+0xC0 @ 0x113A78, the
 * hover/button cell the input dispatch reads at 0x14B68) and
 * `anActionPhaseSave0[112]` @ 0x128AA0, the two scratch cells a modal session uses.
 * The Render_Frame modal arm (0x224D4..0x22500) draws the dialogue straight from
 * the bank: width slot 12, text record slot 13, nLines slot 14, labels slots 15/17.
 *
 * The original functions live in the big UI bank and take an implicit context;
 * the port takes a `UiModalState *` instead, so the state can be tested without
 * the full game context.
 */
#ifndef PUFO_MODAL_H
#define PUFO_MODAL_H

#include "pufo_common.h"

#include "game/pufo_boot.h"
#include "render/pufo_render.h"

typedef struct UiModalState {
    i32 nField48;             /* apUiSlotsA[24] @ Map+0xC0, the hover/button cell, cleared on begin (0x26438) */
    i32 nActionPhaseSave112;  /* anActionPhaseSave0[112] @ 0x128AA0, cleared on begin (0x2644C);
                               * UiModal_DrawDialog uses it as the typewriter cursor (0x26D1C..) */
    i32 nActionPhaseSave113;  /* anActionPhaseSave0[113] @ 0x128AA4, the progressive-draw flag the
                               * dialog writes (0 sets whole lines at cursor 0, 0x26D34; the cursor
                               * wrap clears it at 0x26D54). UiModal_Begin does NOT clear it. */
    bool bOpen;               /* apUiSlotsA[19] low byte */
    i32  anRect12_14[3];      /* apUiSlotsA[12..14]: dialog width, first text record, line count */
    void *pLabel15;           /* apUiSlotsA[15], the first button label ("YES"/"OK") */
    void (*pfn16)(void);      /* apUiSlotsA[16], the first button callback */
    void *p17;                /* apUiSlotsA[17], the second button label ("NO"), 0 for one-button modals */
    void (*pfn18)(void);      /* apUiSlotsA[18], the second button callback */
} UiModalState;

/* UiModal_Begin @ 0x26430. Clears the two scratch cells, sets the open flag and
 * stores the three ints, the two labels, the two callbacks and the extra argument.
 * Slot 15 is a label STRING in every caller found so far (Menu_ConfirmQuitDialog
 * @ 0x18E74 passes "YES"), which is why the port types it as a pointer. */
void UiModal_Begin(UiModalState *pModal, i32 n12, i32 n13, i32 n14, void *pLabel15,
                   void (*pfn16)(void), void *p17, void (*pfn18)(void));

/* UiModal_End @ 0x2648c. Clears only the open flag; the stored fields survive. */
void UiModal_End(UiModalState *pModal);

/* UiModal_EndThunk @ 0x12254: the zero-argument wrapper that tail-calls UiModal_End, stored as the
 * modal-end callback in the eighteen modal tables Inv_HandlePointer dispatches (see InvDropHooks.pfnModal
 * in pufo_invdrag.h). Deviation: the original thunk is zero-arg because UiModal_End reads the modal fields
 * out of the global context; the port's modal state is caller-owned, so the thunk takes the state pointer
 * and forwards it -- same observable effect when the callback fires. */
void UiModal_EndThunk(UiModalState *pModal);

/* --- the modal dialogue renderer ------------------------------------------------
 *
 * UiModal_DrawDialog @ 0x264a0 draws the framed dialogue: a filled body, four border
 * strips, two travelling highlight bands, one or two 50x17 buttons with centred labels,
 * and the text lines copied out of the 205-byte text-record table.
 *
 * The original reaches everything through the game context: the typewriter cursor at
 * anActionPhaseSave0[112] (0x128AA0 = Map+0x150E8, 0x265D8/0x265E0 build the address), the
 * progressive-draw flag at anActionPhaseSave0[113] (0x128AA4 = Map+0x150EC), the animation phase
 * and the band selector on ONE cell, unk_1139EF (Map+0x37 -- the byte read at 0x26814 and the
 * dword mod 4 at 0x26648), the selected-button slot apUiSlotsA[24], and the output slots
 * apUiSlotsA[20..23] (the writes at 0x266D4, 0x268E4, 0x268F0 and 0x26AE4). An earlier note here
 * gave Map+0x86248 and Map+0x86252A for the two scratch cells; that was a wrong base. The port
 * passes these as UiModalAnim and UiModalLayout so the whole renderer is testable without a context.
 */
typedef struct UiModalAnim {
    i32  nCursor;        /* anActionPhaseSave0[112]: the typewriter cursor, wraps at 40 (0x26D1C..0x26D48) */
    i32  bProgressive;   /* anActionPhaseSave0[113]: 0 draws whole lines, non-zero draws a prefix */
    i32  nAnimPhase;     /* Map+0x37 read as a byte: every key formula is centred on abs(nAnimPhase - 4) */
    i32  nBand;          /* the same Map+0x37 cell: nBand % 4 picks the horizontal band (0x26648) */
    i32  nSelButton;     /* apUiSlotsA[24]: 1 highlights the left button, 2 the right */
    const GfxColorEnv *pColor;
} UiModalAnim;

typedef struct UiModalLayout {
    i32 nButtonX[2];     /* apUiSlotsA[20] and [21] */
    i32 nButtonY;        /* apUiSlotsA[22] */
    i32 nButtonCount;    /* apUiSlotsA[23] */
} UiModalLayout;

/* nRecIndex is the FIRST text record to draw; line N uses record nRecIndex + N.
 * pszBtn1 / pszBtn2 are the button labels; NULL means no button. Returns the length of
 * the last line drawn, or the cursor value before the update when nLines is not positive -
 * register residue the callers do not use, reproduced rather than invented. */
i32 UiModal_DrawDialog(UiModalAnim *pAnim, const u8 *pTextRecs, i32 nWidth,
                       i32 nRecIndex, i32 nLines, const char *pszBtn1,
                       const char *pszBtn2, UiModalLayout *pLayout);

#endif /* PUFO_MODAL_H */
