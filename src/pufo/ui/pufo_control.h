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

/* UiControl_ctor @ 0x1a28c. Zeroes position/mode/state, sets a 10x10 enabled
 * widget with no pads, and NULLs the click handler. */
void UiControl_ctor(UiControl *pCtl);

/* UiControl_FreePads @ 0x1a2c8. Frees each pad's pixel data and the pad itself, but
 * does NOT null the pointers -- the original leaves them dangling, so a second call
 * would double-free. The port nulls them as it goes and documents that (deviation 1),
 * because a double free is undefined rather than defined behaviour. */
void UiControl_FreePads(UiControl *pCtl);

/* UiControl_BlitActivePad @ 0x1a340. Returns true when a pad was drawn.
 *
 * `pfnBlit` stands in for PadImage_Blit @ 0x110dc and `pfnBlitEx` for
 * PadImage_BlitEx @ 0x11110, chosen by `bUnk17`. It is injected so a test can verify
 * the mode/state selection without a framebuffer. */
typedef void (*UiControlBlitFn)(const PadImage *pPad, void *pUser);

bool UiControl_BlitActivePad(UiControl *pCtl, UiControlBlitFn pfnBlit,
                             UiControlBlitFn pfnBlitEx, void *pUser);

/* UiControl_SetPosMode @ 0x1a5c0.
 *
 * Sets position and mode and resets `nState` to 0. In every mode except 2 it also
 * adopts pad 1's size, so mode 2 is the only one that keeps its current size. */
void UiControl_SetPosMode(UiControl *pCtl, i16 x, i16 y, u8 nMode);

/* A control's description, as UiControl_Setup reads it: three pad names, an offset
 * from the base position, and the mode. */
typedef struct UiControlDesc {
    const char *szPad0;
    const char *szPad1;
    const char *szPad2;
    i16         xOff;
    i16         yOff;
    u8          nMode;
} UiControlDesc;

/* UiControl_Setup @ 0x1a75c.
 *
 * Loads the three pads (through UiControl_LoadPads @ 0x1a604) and then positions the
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
