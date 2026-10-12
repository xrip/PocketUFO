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
#include "render/pufo_render.h"   /* PadImage, PadImage_FreeData */
#include "ui/pufo_control.h"      /* UiControl */
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
 * seven-row slot list first (SaveSlot_FormatList (0x45904), injected because it
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

/* ------------------------------------------------- the start menu buttons
 *
 * Evidence: MenuCtl0_OnClick @ 0x1a09c, MenuCtl1_OnClick @ 0x1a034,
 * MenuCtl2_OnClick @ 0x1a064, MenuCtl3_OnClick @ 0x1a080.
 *
 * All four play sample 16 and then shut the start UI down (StartUi_Shutdown (0x38d7c), not
 * ported, so it is a hook). What runs afterwards is the whole of the main menu:
 *
 *   0  Geoscape_InitUI(0)                                  new game
 *   1  SaveSlot_FormatList(ctx), then the list mode becomes 2   load
 *   2  Titles_Init                                        credits
 *   3  App_QuitDisplay                                    quit
 *
 * Only button 1 touches the list mode, and it does so AFTER the list is built (0x1A048 then
 * 0x1A054). The mode is the same global Menu_OpenSlotList clears to 0 and MenuCtl8..14 set
 * to 3. */
typedef struct StartMenuHooks {
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void (*pfnShutdownStartUi)(void *pUser); /* StartUi_Shutdown (0x38d7c) */
    void (*pfnNewGame)(void *pUser);         /* Geoscape_InitUI (0x857f4) */
    void (*pfnFormatSlots)(void *pUser);     /* SaveSlot_FormatList (0x45904) */
    void (*pfnTitles)(void *pUser);          /* Titles_Init (0x380d8) */
    void (*pfnQuit)(void *pUser);            /* App_QuitDisplay @ 0xa0388 */
    void *pUser;
} StartMenuHooks;

void MenuCtl0_OnClick(const StartMenuHooks *pHooks);
void MenuCtl1_OnClick(i32 *pnListMode, const StartMenuHooks *pHooks);
void MenuCtl2_OnClick(const StartMenuHooks *pHooks);
void MenuCtl3_OnClick(const StartMenuHooks *pHooks);

/* MenuCtl8..14_OnClick @ 0x1a0bc/0x1a0f0/0x1a124/0x1a158/0x1a18c/0x1a1c0/0x1a1f4: the seven
 * load-slot buttons, identical except for the slot number 0..6.
 *
 * Three details the source only shows in the disassembly. The mode is set to 3 BEFORE the
 * load runs (0x1A0CC before 0x1A0D4), so a load that re-enters the UI sees "loading". The
 * test on the result is on its LOW BYTE (`CBZ R0` after a byte compare at 0x1A0DC), not the
 * whole word. And a failed load puts the mode back to 2 -- the value MenuCtl1_OnClick set
 * when it opened the list -- and returns 2 rather than the callee's value. */
#define PUFO_MENU_MODE_LOAD_LIST 2
#define PUFO_MENU_MODE_LOADING   3

i32 Menu_LoadSlot(i32 *pnListMode, i32 nSlot,
                  i32 (*pfnLoadGame)(i32 nSlot, void *pUser), void *pUser);

/* ------------------------------------------------- the save/load screen's slot buttons
 *
 * SaveLoad_Slot0..6 @ 0x19de4/0x19e34/0x19e84/0x19ed4/0x19f24/0x19f74/0x19fc4: seven
 * identical handlers that differ only in the slot number.
 *
 * They are NOT the same as the start menu's load buttons, and the differences are
 * the whole point of them:
 *
 *   - the list mode becomes 1, not 3 (0x19DF4). 1 is the save/load list; 3 is the
 *     start menu's "loading" state.
 *   - the direction is decided by the CURRENT screen, not by which button was
 *     pressed: `nUiScreen == 10` is the save screen and runs SaveGame, anything
 *     else runs LoadGame (0x19E08). One button therefore means "save" or "load"
 *     depending on which list it is drawn in.
 *   - SaveGame's result is returned unchanged and the mode stays 1; only the load
 *     path has a failure case.
 *   - a failed load puts the mode back to 0, not 2, and returns 0 (0x19E2C). The
 *     start menu's buttons restore 2 because that is the value their opener set.
 *
 * As in Menu_LoadSlot, the test on the load result is on its LOW BYTE (0x19E28).
 *
 * SaveGame (0x44988) and LoadGame (0x44bbc) are not ported, so both are hooks. */
#define PUFO_MENU_MODE_SAVE_LOAD_LIST 1
#define PUFO_MENU_SCREEN_SAVE_LIST   10

typedef struct SaveLoadSlotHooks {
    i32 (*pfnSaveGame)(i32 nSlot, void *pUser); /* SaveGame (0x44988) */
    i32 (*pfnLoadGame)(i32 nSlot, void *pUser); /* LoadGame (0x44bbc) */
    void *pUser;
} SaveLoadSlotHooks;

i32 SaveLoad_Slot(i32 nSlot, i32 nUiScreen, i32 *pnListMode,
                  const SaveLoadSlotHooks *pHooks);

/* ------------------------------------------------- tearing down the start UI
 *
 * Evidence: StartUi_Shutdown @ 0x38d7c.
 *
 * The decompiler renders the pad loop as `v2[113].pPad0` with `v2 = v2 + 4`, which reads as
 * three pad pointers four WIDGETS apart. The ARM says otherwise: `MOV R3, #0xE34` is loaded once
 * outside both loops and never changed, the inner step is `ADD R4, R4, #4` -- four BYTES -- and
 * the outer step is `ADD R7, R7, #0x20`, one UiControl. So each of the 15 groups frees three
 * ADJACENT dwords at pBank + 32*i + 0xE34.
 *
 * 0xE34 is 3636, and 3636 = 32*113 + 0x14, which is widget 113's pPad0. The three dwords are
 * therefore pPad0, pPad1 and pPad2 of one widget, and the outer step moves to the next widget.
 * The whole loop frees the three pads of bank widgets 113 through 127 inclusive -- the tail of
 * the 130-widget bank, which is where the start-menu pads live. Nothing reaches past the bank.
 *
 * After that it frees the start UI's font, then the standalone control at unk_1571B4 if there is
 * one, then two blocks the start screen allocated, and finally sets the screen id to 22.
 *
 * Deviation worth naming: this is the one place the port deletes the PadImage structs themselves,
 * because the original operator-deletes each one straight after freeing its pixel data. Everywhere
 * else the port leaves the pad struct to its caller. */
typedef struct StartUiShutdownHooks {
    void (*pfnSoundShutdown)(void *pUser); /* seam for Sound_Shutdown (0x9e3d4) */
    void (*pfnKillTimers)(void *pUser);    /* seam for KillTimers_B (0x9e754) */
    void (*pfnFontFree)(void *pFont, void *pUser); /* seam for Font_Free (0x46144) */
    void *(*pfnFree)(void *pBlock, void *pUser);   /* operator delete for the two blocks */
    void *pUser;
} StartUiShutdownHooks;

typedef struct StartUiShutdownEnv {
    UiControl *pBank;      /* the 130-widget bank; widgets 113..127 are freed */
    UiControl *pStartCtl;  /* unk_1571B4, may be NULL */
    void *pFont;           /* the font the start UI loaded */
    void *pBlockA;         /* MEMORY[0x1568D8] */
    void *pBlockB;         /* MEMORY[0x1568EC] */
    i32 *pnUiScreen;       /* Map.nUiScreen @ +0x3A, set to 22 */
    StartUiShutdownHooks hooks;
} StartUiShutdownEnv;

void StartUi_Shutdown(const StartUiShutdownEnv *pEnv);

#endif /* PUFO_MENUBTN_H */
