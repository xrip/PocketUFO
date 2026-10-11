/* pufo_uibank.h - the widget bank and the pad bank the HUD is built from.
 *
 * Evidence: Tact_InitUiControls @ 0x3b498, Tact_LoadUiPads @ 0x3b068,
 *           UiControl_Setup @ 0x1a75c, UiControl_LoadPads @ 0x1a604.
 *
 * ## The descriptor record (32 bytes, read by UiControl_Setup)
 *
 * The ARM at 0x1a76c..0x1a774 names the field offsets directly:
 *
 *     0x00 xOff i16     0x02 yOff i16
 *     0x04 szPad0[9]    0x0d szPad1[9]    0x16 szPad2[9]
 *     0x1f nMode u8
 *
 * An empty name means "no pad": UiControl_LoadPads compares each name against the
 * empty string at 0x10DA38 and leaves that pad slot NULL. The port keeps the names
 * as `const char *` in UiControlDesc instead of inline 9-byte buffers, because the
 * port's descriptors are static C data and never overlaid on raw memory; the values
 * and the stride are the same.
 *
 * ## The thirteen descriptor tables and the widget groups they fill
 *
 * pBank @ 0xAC6D0 holds UiControl at stride 0x20. Each group loop advances the
 * control pointer and the descriptor pointer together, so a group is a run of
 * consecutive widgets. Widget index = (store address - 0xAC6D0 - 0x0C) / 0x20,
 * which is how every group boundary below was pinned:
 *
 *     group          widgets     descriptor    pad
 *     tactical       0..18       off_A7ED8     nextu, uplev, map, sit, aims...
 *     inventory      19..31      unk_A8158     prev, next, alls, pistolvs...
 *     action pads    32..43      unk_A8418     firebtn / cfirebtn
 *     deploy grid    44..52      unk_A8918     initg / cinitg, 3x3
 *     menu column    53..59      unk_A8838     firebtn / cfirebtn, 7 rows
 *     options        60..88      off_A8A38     initg, flag, soundf, bclose
 *     research       89..97      unk_A82F8     pushpu / pushpd
 *     save slots     98..100     unk_A8598     bclose, up, down
 *     load slots     101..103    unk_A86B8     bclose, bprevs, bnexts
 *     quit           104         stru_A8718    bclose / bcloseu
 *     save-load list 105..112    unk_A8738     firebtn x7, bclose
 *     cancel 0       128         stru_A7CB8    pushpu / pushpd
 *     cancel 1       129         stru_A7CD8    pushpu / pushpd
 *
 * Widgets 113..127 are never set up. They are the screen-17 list rows, and they
 * sit in the 16-byte gap between the 105..112 group (ending at 0xAD4EF) and widget
 * 128 (0xAD6D0); the original leaves them at their static initial values.
 *
 * The tactical group is the only one positioned from the HUD pad: its base is the
 * centred HUD origin the same function computes. Every other group is set up with
 * xBase = yBase = 0, so its descriptor offset is its absolute position.
 *
 * ## Deviations
 *
 * 1. The original's allocator is `operator new(0x14)` per pad. The port takes the
 *    pad storage from the caller (`pPadStore`) so a test can run without a heap and
 *    the app shell can pool it. Nothing else in the body changes.
 * 2. The original writes a pad pointer even when the allocation failed (it stores
 *    the NULL and then calls PadImage_Load on it, which faults). The port stores
 *    NULL and skips the load, and records the difference here rather than
 *    reproducing a null dereference.
 */
#ifndef PUFO_UIBANK_H
#define PUFO_UIBANK_H

#include "pufo_common.h"
#include "render/pufo_render.h"
#include "ui/pufo_control.h"

/* Pad counts, from the loop bounds in Tact_LoadUiPads: 4 * 2 * 4 manual pads and
 * 11 numbered buttons. */
#define PUFO_UIBANK_MAN_PAD_COUNT 32
#define PUFO_UIBANK_BTN_PAD_COUNT 11
#define PUFO_UIBANK_NAMED_PAD_COUNT 11

/* The manual pads are loaded in mode 34 with their origin forced to 50,55. */
#define PUFO_UIPAD_MAN_MODE 34
#define PUFO_UIPAD_MAN_ORIGIN_X 50
#define PUFO_UIPAD_MAN_ORIGIN_Y 55
#define PUFO_UIPAD_BTN_MODE 1

/* Where a loaded pad is stored. The original keeps them in the Map block; the port
 * hands the slot id to the caller so the app shell can own the storage. The Map
 * offset each id stands for is recorded next to each name. */
#define PUFO_UIPAD_MAN(n) (0 + (n))        /* Map+0x199C + 4n .. Map+0x1A1C */
#define PUFO_UIPAD_BTN(n) (32 + (n))       /* Map+0xCC + 4n  .. Map+0xF8   */
#define PUFO_UIPAD_HUD 43                  /* unk_113AB4, Map+0xFC  "dpanelv"  */
#define PUFO_UIPAD_INV 44                  /* unk_113AB8, Map+0x100 "invv"     */
#define PUFO_UIPAD_MARKER 45               /* unk_113ABC, Map+0x104 "mscanv"   */
#define PUFO_UIPAD_SOLDIER 46              /* unk_128AC0, Map+0x15108 "soldier" */
#define PUFO_UIPAD_SELSOLD 47              /* unk_156928 "selsold"  */
#define PUFO_UIPAD_SBSOLD 48               /* unk_138EB8 "sbsold"   */
#define PUFO_UIPAD_SOLD 49                 /* unk_156A80 "ssold"    */
#define PUFO_UIPAD_ENEMYSTEP 50            /* unk_156AA0 "enemystp" */
#define PUFO_UIPAD_ENDMISSION 51           /* unk_156A74 "endmiss"  */
#define PUFO_UIPAD_TWONONE 52              /* unk_156A70 "twonone"  */
#define PUFO_UIPAD_MINICURSOR 53           /* unk_1571F0 "minimcur" */
#define PUFO_UIPAD_SLOT_COUNT 54

/* What happens to a pad's origin after it loads. */
#define PUFO_PAD_ORIGIN_KEEP 0
#define PUFO_PAD_ORIGIN_ZERO 1
#define PUFO_PAD_ORIGIN_CENTRE 2

/* The tactical HUD is centred on a 240x320 field; the odd +1 branch fires only
 * when the pad is wider or taller than the field. */
#define PUFO_UIBANK_FIELD_W 240
#define PUFO_UIBANK_FIELD_H 320

/* The 32 manual pads, in the order the original loads them: the name table at
 * 0xA90F8 is MAN_0M0..MAN_3F3 at stride 9, and the loop walks it linearly while the
 * store pointer walks Map+0x199C at stride 4. */
extern const char *const g_aUiManPadName[PUFO_UIBANK_MAN_PAD_COUNT];

/* Every pad Tact_LoadUiPads loads, in load order, with its palette slot and
 * whether the loader resets its origin to zero afterwards. */
typedef struct UiPadSpec {
    const char *szName;
    u8          nMode;
    i32         nSlot;
    u8          nOrigin; /* PUFO_PAD_ORIGIN_KEEP / _ZERO / _CENTRE */
} UiPadSpec;

/* The eleven numbered buttons, named 'b' + the decimal index the original builds
 * with itoa. */
extern const char *const g_aUiBtnPadName[PUFO_UIBANK_BTN_PAD_COUNT];

/* The named pads, in load order. The 32 manual pads come first, then these, then
 * the 11 numbered buttons. */
extern const UiPadSpec g_aUiNamedPad[PUFO_UIBANK_NAMED_PAD_COUNT];

/* The three injected operations. `pfnAlloc` stands in for operator new(0x14) plus
 * PadImage_ctor, `pfnLoad` for PadImage_Load @ 0x11020, and `pfnStore` for the Map
 * slot write. */
typedef struct UiPadHooks {
    PadImage *(*pfnAlloc)(void *pUser);
    bool (*pfnLoad)(PadImage *pPad, const char *pszName, u8 nMode, void *pUser);
    void (*pfnStore)(i32 nSlot, PadImage *pPad, void *pUser);
    void *pUser;
} UiPadHooks;

/* Tact_LoadUiPads @ 0x3b068.
 *
 * Loads the 32 manual pads (mode 34, origin forced to 50,55), the eleven named
 * pads, and the eleven buttons "b0".."b10" (name = 'b' + itoa(i), mode 1, origin
 * zeroed).
 *
 * The minimap cursor pad is centred with the odd +1 rule:
 * x = (uWidth > 240 ? 241 - uWidth : 240 - uWidth) >> 1, and the same shape for y
 * against 320.
 *
 * Returns false on the first allocation or load failure. */
bool Tact_LoadUiPads(const UiPadHooks *pHooks);

/* The click handler each widget slot gets, in the order Tact_InitUiControls stores
 * them. NULL where the original installs nothing (widgets 113..127). The names are
 * the IDB's own; they are kept here because they are the work list for the handlers
 * that are not ported yet. */
extern const char *const g_aUiClickName[PUFO_PBANK_WIDGET_COUNT];

/* One descriptor group: the first widget it fills, how many, and its descriptors. */
typedef struct UiBankGroup {
    u16                  nFirstWidget;
    u16                  nCount;
    const UiControlDesc *pDesc;
} UiBankGroup;

/* The twelve groups Tact_InitUiControls runs with xBase = yBase = 0, in the order
 * it runs them. The tactical group is separate because it is positioned from the
 * HUD origin. */
extern const UiBankGroup g_aUiBankGroup[12];

/* The descriptor the heap-allocated look control is set up with (pDesc @ 0xA8138). */
extern const UiControlDesc g_uiLookDesc;

/* What Tact_InitUiControls reaches outside the widget bank. */
typedef struct UiBankInitCtx {
    /* pHudPad->uWidth / uHeight drive the tactical origin, and its nOriginX /
     * nOriginY are written back. */
    PadImage *pHudPad;
    /* unk_1571E4, the screen-0 look control the original allocates here. */
    UiControl *pLookCtl;
    /* The handlers to install, indexed by widget. A NULL entry is installed as-is,
     * which is what the original does for a handler it has none of. */
    void (*const *apfnClick)(void);
    /* Stands in for UiControl_LoadPads @ 0x1a604. */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    /* The look control's handler, TactAction_ConfirmFireBurst @ 0x18774. */
    void (*pfnLookClick)(void);
    void *pUser;
} UiBankInitCtx;

/* Tact_InitUiControls @ 0x3b498.
 *
 * Installs the click handlers, sets up the look control at 0,0 with bEnabled clear
 * and bUnk17 set, centres the HUD pad, then runs the thirteen groups. After the
 * groups it applies the six field overrides the original writes directly into the
 * bank:
 *
 *     widget 16  uWidth 100, uHeight 30   (the command-screen button)
 *     widget 17  uWidth 34,  uHeight 53   (the left hand menu)
 *     widget 18  uWidth 34,  uHeight 53   (the right hand menu)
 *     widget 14  bUnk17 1                 (main menu, blits through BlitEx)
 *     widget 15  bUnk17 1                 (player menu)
 *     widget 31  bUnk17 1                 (the inventory info button)
 *     widget 10  nState 1                 (radio 0 starts selected)
 *
 * The three overridden widgets are all mode 2, the inert mode that keeps its own
 * size, which is why the original has to write the size by hand. */
void Tact_InitUiControls(UiWidgetBank *pBank, const UiBankInitCtx *pCtx);

#endif /* PUFO_UIBANK_H */
