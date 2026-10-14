/* pufo_mainmenu.h - the start menu renderer.
 *
 * Evidence: MainMenu_Draw @ 0x1dbb8.
 *
 * The start screen draws one of three layouts, selected by the list-mode dword at 0x157188 — the
 * same value Menu_OpenSlotList clears to 0, MenuCtl1_OnClick sets to 2, Menu_LoadSlot sets to 3 and
 * SaveLoad_Slot sets to 1:
 *
 *   0        four menu rows, labels aTextRecs records 1076..1079
 *   1        four menu rows, labels aTextRecs records 1080..1083
 *   2 or 3   seven slot rows, labels the 7 x 40-byte block SaveSlot_FormatList @ 0x45904 writes,
 *            then the standalone control unk_1571B4 and its centred label aTextRecs record 1083
 *   anything else   no rows at all
 *
 * Each row is a five-piece frame: a filled body and four 1-pixel border strips, all through
 * SoftSurf_BlitSrcRect so the row's key byte blends them over the backdrop. The key comes from the
 * 7-dword table at unk_1568F0 and is forced to 255 when the matching byte at unk_156AB0 is set.
 * The same key scales the row's text colour: the cyan pair (26,244,252) / (13,122,126) times
 * key/256 — that is what the original's six itod / muld / dtoi constants compute (the doubles are
 * 26/256, 244/256, 252/256, 13/256, 122/256 and 126/256 exactly).
 *
 * Mode 3 additionally writes 40 into Map.anActionPhaseSave0[112] (0x128AA0) and draws a one-line
 * modal dialog from text record 1102. Every mode ends with the white/grey pair and the version
 * stamp "V1.26" (::psz @ 0xAAD28) at (5, 300).
 */
#ifndef PUFO_MAINMENU_H
#define PUFO_MAINMENU_H

#include "pufo_common.h"

#include "game/pufo_boot.h"     /* PUFO_TEXT_REC_STRIDE */
#include "render/pufo_render.h" /* GfxColorEnv */
#include "ui/pufo_control.h"    /* UiControl, UiControlBlitFn */
#include "ui/pufo_modal.h"      /* UiModalAnim, UiModalLayout */

#define PUFO_MAINMENU_ROW_COUNT       4 /* modes 0 and 1 */
#define PUFO_MAINMENU_SLOT_COUNT      7 /* modes 2 and 3 */
#define PUFO_MAINMENU_SLOT_LABEL_LEN 40
#define PUFO_MAINMENU_ROW_STEP       22

/* aTextRecs @ 0xB38D0 record indices the layouts pick. */
#define PUFO_MAINMENU_REC_MENU0  1076
#define PUFO_MAINMENU_REC_MENU1  1080
#define PUFO_MAINMENU_REC_CTL    1083
#define PUFO_MAINMENU_REC_MODAL  1102

/* The list-mode values, shared with pufo_menubtn.h's PUFO_MENU_MODE_* names. */
#define PUFO_MAINMENU_MODE_MAIN     0
#define PUFO_MAINMENU_MODE_SLOTCLICK 1
#define PUFO_MAINMENU_MODE_LOADLIST 2
#define PUFO_MAINMENU_MODE_LOADING  3

typedef struct MainMenuEnv {
    const u8   *pBgGlyph;            /* the start-screen backdrop block, blitted at (0,0) with
                                      * palette 20; its pointer lives at 0x1568D8 and the block is
                                      * StartUiShutdownEnv.pBlockA */
    i32         nListMode;           /* the dword at 0x157188 */
    const i32  *anRowKeys;           /* unk_1568F0, 7 dwords: per-row blend key and colour scale */
    const u8   *abRowDisabled;       /* unk_156AB0, 7 bytes: non-zero forces the key to 255 */
    const char *pSlotLabels;         /* 7 x 40 bytes, written by SaveSlot_FormatList into the block
                                      * whose pointer is kept at 0x156938 */
    UiControl  *pStartCtl;           /* unk_1571B4, the standalone control the slot list ends with */
    const u8   *pTextRecs;           /* aTextRecs @ 0xB38D0, 205-byte records */
    i32        *pnActionPhaseSave112;/* Map.anActionPhaseSave0[112] @ 0x128AA0, set to 40 by mode 3 */
    UiModalAnim *pModalAnim;         /* the modal renderer's anim state, mode 3 only */
    const GfxColorEnv *pColor;
    UiControlBlitFn  pfnCtlBlit;
    UiControlBlitFn  pfnCtlBlitEx;
    void            *pCtlUser;
} MainMenuEnv;

/* MainMenu_Draw @ 0x1dbb8. Backdrop first, then the layout for the current mode, then the modal
 * (mode 3), then the version stamp. The original returns the version draw's value; no caller tests
 * it, so the port returns void. */
void MainMenu_Draw(const MainMenuEnv *pEnv);

#endif /* PUFO_MAINMENU_H */
