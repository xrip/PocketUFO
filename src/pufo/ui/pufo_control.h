/* pufo_control.h - the UiControl button widget.
 *
 * Evidence: UiControl_ctor @ 0x1a28c, UiControl_FreePads @ 0x1a2c8,
 *           UiControl_BlitActivePad @ 0x1a340, UiControl_SetPosMode @ 0x1a5c0,
 *           UiControl_Setup @ 0x1a75c, EmptyBtn_OnClick @ 0x1a228.
 *
 * ## Layout (32 bytes, confirmed by type_inspect)
 *
 *     0x00 x i16        0x02 y i16        0x04 uWidth u16   0x06 uHeight u16
 *     0x08 nMode u8     0x09 bEnabled u8  0x0A nState u8    0x0C pfnClick
 *     0x11 bUnk17 u8    0x14 pPad0        0x18 pPad1        0x1C pPad2
 *
 * `pPad0..pPad2` are the three visual states, so `nState` indexes them directly.
 * The struct is pointer-heavy, so its Win64 size grows; only the field ORDER and the
 * mode/state semantics need to match, and this widget is never serialised.
 *
 * ## The mode/state machine (UiControl_BlitActivePad)
 *
 * The pad drawn depends on BOTH `nMode` and `nState`:
 *
 *     disabled          -> nothing is drawn at all
 *     nMode 0           -> pad index = nState
 *     nMode 1           -> pad index = min(nState, 2)   (clamped)
 *     nMode 2           -> nothing is drawn (nMode 2 is an inert mode)
 *
 * The clamp in mode 1 is the interesting part: `nState` is a u8 but only pads 0..2
 * exist, so a state above 2 would read past `pPad2`. The original clamps with
 * `if (nState >= 2) nState = 2`, which the port reproduces rather than trusting the
 * caller.
 *
 * Before drawing, the widget writes its own `x`/`y` into the pad's origin fields, so
 * a pad's position always follows its control.
 */
#ifndef PUFO_CONTROL_H
#define PUFO_CONTROL_H

#include "pufo_common.h"
#include "render/pufo_render.h"

/* The three pad slots, by state. */
#define PUFO_CONTROL_PADS 3

/* The largest state value a pad exists for. */
#define PUFO_CONTROL_MAX_STATE 2

typedef struct UiControl {
    i16       x;            /* 0x00 */
    i16       y;            /* 0x02 */
    u16       uWidth;       /* 0x04 */
    u16       uHeight;      /* 0x06 */
    u8        nMode;        /* 0x08 */
    u8        bEnabled;     /* 0x09 */
    u8        nState;       /* 0x0A */
    void     (*pfnClick)(void); /* 0x0C */
    u8        bUnk17;       /* 0x11 */
    PadImage *apPads[PUFO_CONTROL_PADS]; /* 0x14, 0x18, 0x1C */
} UiControl;

/* pBank @ 0xAC6D0. The original is one flat block: a UiControl array at stride
 * 0x20, the escape-flag scratch byte at +0x1042 (byte_AD712) and the last pointer
 * position at +0x1044 / +0x1048. Input_OnPointer @ 0x13c6c stores the position on
 * entry and hit-tests these widgets by index:
 *
 *     screen 0   0..18, or only 2, 7, 9, 15 while multi-targeting
 *     screen 1   19..31        screen 3   32..43       screen 5   44..52
 *     screen 7   53..59        screen 12  60..88       screen 15  89..97
 *     screen 2   98..100       screen 4   101, then 102..103
 *     screen 6   104           screens 10,11 105..112
 *     screen 13  128           screen 19  129
 *
 * The port keeps the same indices; UiControl is wider than 0x20 on Win64 and the
 * bank is never overlaid on raw memory. */
#define PUFO_PBANK_WIDGET_COUNT 130

typedef struct UiWidgetBank {
    UiControl aCtl[PUFO_PBANK_WIDGET_COUNT];
    u8        bEscapeFlag; /* +0x1042, byte_AD712 */
    i32       nPointerX;   /* +0x1044 */
    i32       nPointerY;   /* +0x1048 */
    /* The four held-key repeat flags the timer reads, in the order it applies them:
     * up, down, left, right. They repeat only while nUiScreen is 0. */
    u8        abHeldDir[4]; /* +0x104C..0x104F, UiBank_bHeldUp/Down/Left/Right */
} UiWidgetBank;

/* UiControl_ctor @ 0x1a28c. Zeroes position/mode/state, sets a 10x10 enabled
 * widget with no pads, and NULLs the click handler. */
void UiControl_ctor(UiControl *pCtl);

/* UiControl_FreePads @ 0x1a2c8. Frees each pad's pixel data and the pad itself, but
 * does NOT null the pointers -- the original leaves them dangling, so a second call
 * would double-free. The port nulls them as it goes and documents that (deviation 1),
 * because a double free is undefined rather than defined behaviour. */
void UiControl_FreePads(UiControl *pCtl);

/* The bank is not one flat array of 130 widgets: the original walks it as 14 fixed
 * groups, and the groups are exactly the per-screen ranges the hit test uses.
 * UiBank_CtorAll @ 0x11320 walks them in this order, ascending inside each group.
 * UiBank_FreeAllPads @ 0x1117c walks the SAME groups in reverse order, descending
 * inside each group, and together the two cover every slot exactly once.
 *
 * These boundaries are the same ones the descriptor groups in pufo_uibank.h
 * (`g_aUiBankGroup`) use; that table lists only the twelve spans Tact_InitUiControls
 * positions, in its own run order (53 first). The two spans it leaves out are 0..18,
 * built elsewhere, and 113..127, which get no click handler. */
typedef struct UiBankSpan {
    u8 nFirst;
    u8 nCount;
} UiBankSpan;

#define PUFO_PBANK_SPAN_COUNT 14
extern const UiBankSpan g_aUiBankSpans[PUFO_PBANK_SPAN_COUNT];

/* UiBank_CtorAll @ 0x11320. Constructs all 130 widgets group by group, then clears
 * the escape-flag scratch byte at bank +0x1042 and the action flag `UiBank_bActionFlag`.
 * `pbActionFlag` is the same flag `Input_OnAction @ 0x1557c` sets and
 * `UiScreen_OnBack @ 0x15670` clears; it lives in TactInputWorld in the port, so it
 * is passed rather than reached through bank memory. Returns the bank. */
UiWidgetBank *UiBank_CtorAll(UiWidgetBank *pBank, bool *pbActionFlag);

/* UiBank_FreeAllPads @ 0x1117c, reached through Bank_AtexitThunk (0x11170, a 12-byte thunk the
 * port does not reproduce). Frees the
 * pads of all 130 widgets in the reverse group order. */
void UiBank_FreeAllPads(UiWidgetBank *pBank);

/* UiControl_BlitActivePad @ 0x1a340. Returns true when a pad was drawn.
 *
 * `pfnBlit` stands in for PadImage_Blit @ 0x110dc and `pfnBlitEx` for
 * PadImage_BlitEx (0x11110), chosen by `bUnk17`. It is injected so a test can verify
 * the mode/state selection without a framebuffer. */
typedef void (*UiControlBlitFn)(const PadImage *pPad, void *pUser);

bool UiControl_BlitActivePad(UiControl *pCtl, UiControlBlitFn pfnBlit,
                             UiControlBlitFn pfnBlitEx, void *pUser);

/* UiControl_SetPosMode @ 0x1a5c0.
 *
 * Sets position and mode and resets `nState` to 0. In every mode except 2 it also
 * adopts pad 1's size, so mode 2 is the only one that keeps its current size. */
void UiControl_SetPosMode(UiControl *pCtl, i16 x, i16 y, u8 nMode);

/* What UiControl_HitTest reaches outside the widget: the pad blit callbacks and the
 * UI slot the original clears at Map+0x60 (apUiSlotsA[0], IDB unk_113A18). */
typedef struct UiControlHitCtx {
    u32            *pnUiSlot0;
    UiControlBlitFn pfnBlit;
    UiControlBlitFn pfnBlitEx;
    void           *pUser;
} UiControlHitCtx;

/* UiControl_HitTest @ 0x1a3d8. The pointer press/release handler for one control.
 *
 * The original takes five arguments: (pCtl, x, y, bDown in R3, and a fifth byte on
 * the stack that Hex-Rays leaves as an uninitialised local). Input_OnPointer always
 * passes bDown = 1 and the stack byte = 0.
 *
 *   bEnabled == 0            -> 0, nothing happens.
 *   bForceClick && mode 0 && state 1 -> run pfnClick, return 1.
 *
 * The hit box is measured only when bDown is set, and its far edge is exclusive at
 * `x + uWidth - 1` and `y + uHeight - 1`.
 *
 * Press (bDown set), bHit = inside the box:
 *   mode 0 or 2: hit and state 0 -> state 1 and redraw; no hit and state 1 ->
 *                state 0 and redraw; either way Map+0x60 is cleared. Returns bHit.
 *   mode 1:      hit and state < 2 -> state += 2 and redraw; no hit and state > 1
 *                -> state -= 2 with NO redraw. Returns bHit.
 *   other mode:  returns bHit and changes nothing.
 *
 * Release (bDown clear), the hit box is never consulted:
 *   mode 0 or 2: state 1 -> state 0, redraw, click, return 1; otherwise 0.
 *   mode 1:      state > 1 -> state = (state == 2) ? 1 : 0, redraw, click, return 1;
 *                otherwise 0.
 *   other mode:  0.
 *
 * The click handler is called as a tail call in the original; the port calls it
 * normally, which is the same observable behaviour. */
bool UiControl_HitTest(UiControl *pCtl, i32 x, i32 y, bool bDown, bool bForceClick,
                       const UiControlHitCtx *pCtx);

/* A control's description, as UiControl_Setup reads it: an offset from the base
 * position, three pad names, and the mode. The field order is the original's own
 * 32-byte record order, proven from the offsets UiControl_Setup names in its ARM
 * (xOff +0, yOff +2, szPad0 +4, szPad1 +13, szPad2 +22, nMode +31); the port keeps
 * `const char *` instead of inline 9-byte buffers because its descriptors are static
 * C data and are never overlaid on raw memory. */
typedef struct UiControlDesc {
    i16         xOff;
    i16         yOff;
    const char *szPad0;
    const char *szPad1;
    const char *szPad2;
    u8          nMode;
} UiControlDesc;

/* UiControl_LoadPads @ 0x1a604: free the three pad slots, then load each non-empty descriptor
 * name into a fresh PadImage in mode 1. The mode is fixed at 1 here, not the control's mode. */
void UiControl_LoadPads(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2);

/* UiControl_Setup @ 0x1a75c.
 *
 * Loads the three pads (through UiControl_LoadPads below) and then positions the
 * control at `base + desc offset` in the description's mode. The pad loading is
 * injected because it reads the asset archives. */
void UiControl_Setup(UiControl *pCtl, i16 xBase, i16 yBase, const UiControlDesc *pDesc,
                     bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0,
                                         const char *psz1, const char *psz2,
                                         void *pUser),
                     void *pUser);

/* EmptyBtn_OnClick @ 0x1a228. Plays sample 16 and clears the geoscape draw mode.
 * The draw-mode store is injected because that global belongs to the renderer. */
#define PUFO_CONTROL_SAMPLE_CLICK 16

void EmptyBtn_OnClick(void (*pfnPlaySample)(i32 nSample, void *pUser),
                      void (*pfnClearDrawMode)(void *pUser), void *pUser);

#endif /* PUFO_CONTROL_H */
