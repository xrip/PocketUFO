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
#include "game/pufo_font.h"     /* PufoFont, for GameUI_InitStartup */
#include "game/pufo_options.h"  /* PufoOptions, for GameUI_InitStartup */
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
/* SaveGame_SlotN_CloseUI (0x6af80..0x6b148) sets this around the write. MainMenu_Draw has no layout
 * for it, so the start screen drops its rows while the slot file is being written. */
#define PUFO_MAINMENU_MODE_SAVING   4

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

/* GameUI_InitStartup @ 0x38a28: everything the start menu needs before the first frame.
 *
 * The order is the original's own: clear the reader, Font_Init, install the eleven click handlers
 * (controls 0..3 and 8..14 -- 4..7 keep whatever the static block held), set up the fifteen
 * controls from the a2s descriptors at 0xA7CF8 with 140x18 extents, load startg.pal into slot 20
 * and menu.pal into slot 1, read startg.pad into MainMenu_pBgGlyph and emptyb.pad into 0x1568EC,
 * set the glyph base to the running frame count BEFORE Font_LoadSpr("font"), build the index,
 * zero the seven row keys / seven disabled bytes / seven key-source dwords at 0x1571C8, build the
 * standalone control (desc "V", y 230, pads emptyb1/cemptyb1/empty, click EmptyBtn_OnClick), set
 * the list mode to 0 and the ui screen to 17, then the "tact" samples, music track 0, both
 * volumes and StartTimers_B.
 *
 * a2s @ 0xA7CF8 is 15 descriptors of 32 bytes: xOff 50 for all of them; yOff 115/137/159/181 for
 * the four main rows (repeated for controls 4..7) and 65..197 step 22 for the seven slot rows;
 * every pad name is empty (these are click zones) and every mode is 2.
 *
 * Deviations: the original installs fixed function addresses into the control block's +0xC; the
 * port installs the eleven handlers the caller supplies (the MenuCtl thunks live in the shell,
 * which owns the hook block). The original's operator new for the two pad blobs and the control
 * is unchecked; the port skips a failed allocation. The original leaves the standalone control's
 * descriptor mode byte as stack residue; the port zeroes it. */
typedef struct GameUiStartupEnv {
    PufoFont    *pFont;                    /* fontSys @ 0x158268 */
    UiControl   *paMenuControls;           /* the static block at 0xAD4F0 = pBank widgets 113..127;
                                            * the shell passes the bank slice so Input_OnPointerUp
                                            * reaches the same controls */
    void       (*apfnMenuClick[15])(void); /* the handlers installed at +0xC (0..3 and 8..14) */
    void       (*pfnEmptyClick)(void);     /* the standalone control's EmptyBtn_OnClick thunk */
    UiControl   *pStartCtl;                /* MainMenu_pStartCtl @ 0x1571B4, caller-owned */
    u8         **ppBgGlyph;                /* MainMenu_pBgGlyph @ 0x1568D8 */
    u8         **ppEmptyBtnPad;            /* 0x1568EC */
    i32         *anRowKeys;                /* MainMenu_anRowKeys @ 0x1568F0, 7 */
    u8          *abRowDisabled;            /* MainMenu_abRowDisabled @ 0x156AB0, 7 */
    i32         *anRowKeySrc;              /* unk_1571C8..0x1571E0, 7 dwords */
    i32         *pnListMode;               /* MainMenu_nListMode @ 0x157188 */
    i32         *pnUiScreen;               /* Map_nUiScreen @ Map+0x3A */
    const PufoOptions *pOptions;           /* the option block the two volume calls read */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser); /* UiControl_Setup's pad loader */
    void *pPadUser;
} GameUiStartupEnv;

void GameUI_InitStartup(GameUiStartupEnv *pEnv);

/* PocketUFO_WndProc (0xa0944, not ported) fragment at 0xA0AC8, the WM_TIMER 0xC352 branch for
 * screen 0x11: the start-menu row flicker. Per row: when the row's key source is armed the key is 32*acc-1, where acc is the
 * anim phase plus one random roll while the phase is under 4 (0xA0AEC re-seeds the accumulator
 * from the phase every row, so it never carries between rows -- the global is write-only
 * residue); when the source is 0 the key is a steady 200. Then a 1-in-400 roll either clears
 * this row's source (a further 5% chance, armed rows only) or zeroes every source and re-arms
 * only this row. nAnimPhase is the 0xC352 counter (unk_1139EF, incremented and wrapped 0..6 by
 * the shell's timer branch at 0xA0978). */
void MainMenu_TickRowKeys(i32 *anRowKeys, i32 *anRowKeySrc, i32 nAnimPhase, i32 *pnAccum);

#endif /* PUFO_MAINMENU_H */
