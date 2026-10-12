/* pufo_menubtn.h - the widget click adapters the widget bank installs.
 *
 * Evidence: MenuBtn_Confirm0 @ 0x19650, Confirm1 @ 0x196e8, Confirm2 @ 0x19884,
 *   Confirm3 @ 0x198a4, Confirm4 @ 0x19910, Confirm5 @ 0x1a014,
 *   ConfirmSnd15 @ 0x198f0, ConfirmClear @ 0x194c8, Secondary @ 0x19498,
 *   SecondaryDismiss @ 0x195e0, ActionBack0 @ 0x195a8, ActionBack1 @ 0x19618,
 *   Cancel0 @ 0x1a24c, Cancel1 @ 0x1a26c,
 *   Menu_OpenLoadSlots @ 0x18864, Menu_OpenSaveSlots @ 0x188a4,
 *   Menu_ConfirmQuitDialog @ 0x18e2c, Menu_CancelQuitDialog @ 0x18e04,
 *   App_QuitFromTact @ 0x18e18.
 *
 * ## What they are
 *
 * Every one of these is a UiControl.pfnClick handler installed by
 * Tact_InitUiControls @ 0x3b498 (see pufo_uibank.h for the widget table). They are
 * 24..56 bytes long, take the widget bank as their only argument, and do nothing
 * but play a sample, sometimes clear nUiScreen (Map+0x3A, STRH), and tail-call one
 * of the input handlers already ported in pufo_input.c. The port groups them by
 * behaviour instead of by widget, because the originals are duplicates:
 *
 *   Confirm0/1/2/3/4/5   identical, sample 16            -> MenuBtn_Confirm(16)
 *   ConfirmSnd15         identical but sample 15         -> MenuBtn_Confirm(15)
 *   ConfirmClear         sample 16, screen = 0, Confirm  -> MenuBtn_ConfirmClear
 *   Secondary            sample 16, screen = 0, Secondary
 *   SecondaryDismiss     the same plus UiScreen_DismissHigh
 *   ActionBack0/1        identical: sample 16, screen = 0, OnAction, OnBack
 *   Cancel0/1            identical: sample 16, OnCancel
 *
 * ## The screen clear makes part of the callees dead
 *
 * ConfirmClear, Secondary, SecondaryDismiss, ActionBack0 and ActionBack1 all write
 * nUiScreen = 0 BEFORE the handler runs (ARM at 0x194B0, 0x194E0, 0x195C0, 0x195F8,
 * 0x19630: `LDR R0,=0x1139B8; MOV R1,#0; STRH R1,[R0,#(unk_1139F2-0x1139B8)]`).
 * UiScreen_OnBack @ 0x15670 and UiScreen_DismissHigh @ 0x159ec only act on screens
 * 8, 9 and 13, so from these five widgets those branches can never run:
 * SecondaryDismiss always takes its no-op path and ActionBack's quit/abort/cancel
 * paths are dead. The port reproduces the order exactly rather than "fixing" it.
 *
 * ## The two scratch bytes
 *
 * Input_OnAction @ 0x1557c sets UiBank_bActionFlag (pBank+0x1041) and UiScreen_OnBack
 * @ 0x15674 clears the SAME byte, so MenuBtn_ActionBack leaves it 0.
 * Input_OnSecondary sets byte_AD712 (bUiEscapeFlag) in its screen-0 path and
 * UiScreen_DismissHigh clears it on entry. The port passes both through the
 * context so a caller owns the storage; NULL means "no scratch modelled".
 */
#ifndef PUFO_MENUBTN_H
#define PUFO_MENUBTN_H

#include "game/pufo_input.h"
#include "game/pufo_menu.h"
#include "pufo_common.h"
#include "ui/pufo_modal.h"

/* The samples the confirm adapters play. 16 is the default UI click; only
 * MenuBtn_ConfirmSnd15 uses 15. */
#define PUFO_BTN_SAMPLE_CONFIRM     16
#define PUFO_BTN_SAMPLE_CONFIRM_ALT 15

/* The screens the two slot-list buttons open. SaveLoad_Slot0..6 @ 0x19de4..0x19fc4
 * pick SaveGame or LoadGame by testing nUiScreen == 10, so 10 is the save list. */
#define PUFO_UI_SCREEN_SAVE_SLOTS  10u
#define PUFO_UI_SCREEN_LOAD_SLOTS  11u
#define PUFO_UI_SCREEN_QUIT_DIALOG  8u

/* Everything the adapters need. pState and pHooks are required; the rest may be
 * NULL when the caller does not model that scratch or that screen family. */
typedef struct UiButtonCtx {
    TactInputState       *pState;
    const TactInputHooks *pHooks;
    const TactInputWorld *pWorld;      /* only the Secondary pair needs it */
    bool                 *pbActionFlag; /* UiBank_bActionFlag */
    bool                 *pbEscapeFlag; /* byte_AD712 */
} UiButtonCtx;

/* MenuBtn_Confirm0..5 @ 0x19650/0x196e8/0x19884/0x198a4/0x19910/0x1a014 and
 * MenuBtn_ConfirmSnd15 @ 0x198f0: play the sample, then Input_OnConfirm. The
 * screen is NOT cleared here, unlike the ConfirmClear variant. */
void MenuBtn_Confirm(const UiButtonCtx *pCtx, i32 nSample);

/* MenuBtn_ConfirmClear @ 0x194c8: sample 16, nUiScreen = 0, then Input_OnConfirm. */
void MenuBtn_ConfirmClear(const UiButtonCtx *pCtx);

/* MenuBtn_Secondary @ 0x19498: sample 16, nUiScreen = 0, then Input_OnSecondary. */
void MenuBtn_Secondary(const UiButtonCtx *pCtx);

/* MenuBtn_SecondaryDismiss @ 0x195e0: the same, then UiScreen_DismissHigh, whose
 * result is returned. Because the screen was just cleared to 0 the result is
 * always PUFO_UI_DISMISS_NONE; the port still returns it so the caller sees the
 * original's value. */
i32 MenuBtn_SecondaryDismiss(const UiButtonCtx *pCtx);

/* MenuBtn_ActionBack0 @ 0x195a8 and MenuBtn_ActionBack1 @ 0x19618, which are
 * identical: sample 16, nUiScreen = 0, Input_OnAction, then UiScreen_OnBack. */
void MenuBtn_ActionBack(const UiButtonCtx *pCtx);

/* MenuBtn_Cancel0 @ 0x1a24c and MenuBtn_Cancel1 @ 0x1a26c, identical: sample 16,
 * then UiScreen_OnCancel, whose result is returned. */
i32 MenuBtn_Cancel(const UiButtonCtx *pCtx);

/* Menu_OpenLoadSlots @ 0x18864 / Menu_OpenSaveSlots @ 0x188a4. Both rebuild the
 * seven-row slot list first (SaveSlot_FormatList @ 0x45904, injected because it
 * reads the save files), play sample 16, clear the list-mode byte *0x157188, and
 * open screen 10 for the save list or 11 for the load list. */
void Menu_OpenSlotList(u16 *pnUiScreen, i32 *pnListMode, bool bSaveMode,
                       void (*pfnFormatList)(void *pUser),
                       const MenuHooks *pHooks);

/* Menu_ConfirmQuitDialog @ 0x18e2c: sample 16, then UiModal_Begin(pCtx, 140, 24, 4,
 * "YES", App_QuitFromTact @ 0x18e18, "NO", Menu_CancelQuitDialog @ 0x18e04), then
 * nUiScreen = 8. The two callbacks are injected: App_QuitFromTact is
 * Tact_Shutdown(1) + App_QuitDisplay, which is the app shell's job. Returns the new
 * screen id, as the original does. */
i32 Menu_OpenQuitDialog(UiModalState *pModal, u16 *pnUiScreen,
                        void (*pfnYes)(void), void (*pfnNo)(void),
                        const MenuHooks *pHooks);

/* Menu_CancelQuitDialog @ 0x18e04, the NO callback: it only clears the screen. */
void Menu_CancelQuitDialog(u16 *pnUiScreen);

#endif /* PUFO_MENUBTN_H */
