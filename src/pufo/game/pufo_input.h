/* pufo_input.h - tactical/geoscape directional input handlers.
 *
 * Evidence: Input_OnLeft @ 0x116c0, Input_OnRight @ 0x117b0,
 *           Input_OnUp @ 0x118b0, Input_OnDown @ 0x119b0,
 *           Input_OnAction @ 0x1557c, Input_OnMenuEscape @ 0x162ec.
 *
 * The original reads and writes pCtxBase fields directly. The port models the
 * touched fields as TactInputState and injects the cross-subsystem effects
 * (geoscape panel/focus, inventory scroll, unit selection, sample playback and
 * the cursor-changed no-op) through TactInputHooks, so the handlers stay
 * testable without the whole game state.
 *
 * Field offsets (pCtxBase, the Map struct), all verified in the IDB:
 *   bTactFlag 0x00, gap_52[0] 0x34, gap_52[1] 0x35, nUiScreen 0x3a,
 *   bGeoscapeUi 0x44, bFireStatus0[1] 0x51, bFireStatus0[2] 0x52,
 *   anCamWorldOff[0] 0x4eac, bTrajPad27[2] 0x1406d,
 *   nMapDimX 0x1511b, nMapDimY 0x1511c, nCursorX 0x15128, nCursorY 0x1512c,
 *   the camera-scroll dwords 0x15130/0x15134 (IDA's pad86320) and
 *   nLayerScratch 0x15138.
 */
#ifndef PUFO_INPUT_H
#define PUFO_INPUT_H

#include "pufo_common.h"

typedef struct MapCell MapCell;
typedef struct TactItem TactItem;
typedef struct Soldier Soldier;

/* TactState.bQueueActive @ 0x111d6. */
typedef struct TactInputState {
    bool bGeoscapeUi;   /* 0x44 */
    bool bTactFlag;     /* 0x00 */
    u8   bFireStatus1;  /* 0x51, original bFireStatus0[1] */
    u8   bFireStatus2;  /* 0x52, original bFireStatus0[2] */
    u8   bTrajPad27_2;   /* 0x1406d, original bTrajPad27[2] */
    bool bQueueActive;  /* original unk_157198->bQueueActive */
    u16  nUiScreen;     /* 0x3a */
    i32  anCamWorldX;   /* 0x4eac, original anCamWorldOff[0] */
    i32  nCamScrollX;   /* 0x15130 */
    i32  nCamScrollY;   /* 0x15134 */
    u8   nTileShiftX;   /* 0x34, original gap_52[0] */
    u8   nTileShiftY;   /* 0x35, original gap_52[1] */
    u8   nMapDimX;      /* 0x1511b */
    u8   nMapDimY;      /* 0x1511c */
    u8   nLayerScratch; /* 0x15138 */
    bool bGameTickHold; /* 0x5a */
    bool bCancelTo19;   /* gap_6556[132] != 0, the screen-13 escape branch */
    u16  nUiSlot11;     /* apUiSlotsA[11] low word, screens 20..21 */

    /* Fields only Input_OnConfirm @ 0x15f3c touches. */
    u8        bMultiTargetArmed; /* 0x22 byte_1139BA: multi-target selection is live */
    i32       nMultiTargetCount; /* 0x04 */
    MapCell  *apMultiTargets[8]; /* 0x08 */
    TactItem *pSelWeapon;        /* 0x48 */
    Soldier  *pSelUnit;          /* 0x15334, written by the screen-4 path */
    u8        bDeployPanelOpen;  /* 0x15110 */
    u8        bUiSlot8Byte0;     /* apUiSlotsA[8] low byte @ 0x80 */
    u8        bLookCommitFlag;   /* unk_128A44 @ 0x1508C */
    void     *pLookCommitUnit;   /* unk_128A40 @ 0x15088 */
    i32       nCursorX;          /* 0x15128 */
    i32       nCursorY;          /* 0x1512C */
    i32       anCamWorldY;       /* 0x4eb0, anCamWorldOff[1] */
    u8       *pbScratchFlag9;    /* *unk_1571E4 + 9 */
} TactInputState;

/* The effects the handlers trigger in other subsystems, all injected. */
typedef struct TactInputHooks {
    void (*pfnGeoscapePanelA)(void *pUser);
    void (*pfnGeoscapePanelB)(void *pUser);
    void (*pfnGeoscapeFocusPrev)(void *pUser);
    void (*pfnGeoscapeFocusNext)(void *pUser);
    void (*pfnGeoscapeMapClick)(void *pUser);
    void (*pfnInvScrollUp)(void *pUser);
    void (*pfnInvScrollDown)(void *pUser);
    void (*pfnSelectNextUnit)(void *pUser);
    void (*pfnSelectPrevUnit)(void *pUser);
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void (*pfnCursorChanged)(void *pUser);  /* nullsub_3 @ 0x22b50 */
    void (*pfnHandleMapClick)(void *pUser); /* Tact_HandleMapClick */
    void (*pfnOnConfirm)(u16 nUiScreen, void *pUser);
    /* UiModal_End @ 0x2648c. */
    void (*pfnModalEnd)(void *pUser);
    /* Tact_Shutdown(0) + Geoscape_InitUI(1), the screen-13/19 escape path. */
    void (*pfnShutdownReinit)(void *pUser);
    /* Tact_Shutdown(1) + App_QuitDisplay, the screen-8 escape path. */
    void (*pfnShutdownQuit)(void *pUser);
    /* UiModal_End + Tact_CheckBattleEnd(abort=1), the screen-9 escape path. */
    void (*pfnModalEndCheckBattle)(void *pUser);
    /* UiScreen_OnCancel @ 0x16200. */
    void (*pfnOnCancel)(void *pUser);
    /* Geoscape_CloseUI @ 0x8b94c, on the &unk_1864F8 screen object. */
    void (*pfnGeoscapeCloseUi)(void *pUser);
    /* Inv_CloseAndCommit @ 0x15a48. */
    void (*pfnInvCloseAndCommit)(void *pUser);
    /* MapCursor_SetPos @ 0x27014, with the stored cursor position. */
    void (*pfnSetCursorPos)(i32 nX, i32 nY, void *pUser);
    /* MapView_FocusOnUnit @ 0x2c758 for the screen-2 path. The original reads a
     * unit pointer out of the view-cell array at Map+0x140F0, indexed
     * 3600*nLayer + 60*anCamWorldOff[1] + anCamWorldOff[0]; the port hands the
     * three indices over and lets the context layer do the lookup. */
    void (*pfnFocusOnViewCell)(i32 nLayer, i32 nViewY, i32 nViewX, void *pUser);
    /* Options_SaveToRegistry @ 0x114d8. */
    void (*pfnSaveOptions)(void *pUser);
    void *pUser;
} TactInputHooks;

/* The shared precondition of the four directional handlers: no active fire mode
 * and no queued action. */
bool TactInput_CanScroll(const TactInputState *pState);

void Input_OnLeft(TactInputState *pState, const TactInputHooks *pHooks);
void Input_OnRight(TactInputState *pState, const TactInputHooks *pHooks);
void Input_OnUp(TactInputState *pState, const TactInputHooks *pHooks);
void Input_OnDown(TactInputState *pState, const TactInputHooks *pHooks);

/* Input_OnAction @ 0x1557c. Sets the original's byte_AD711 scratch flag. */
void Input_OnAction(TactInputState *pState, const TactInputHooks *pHooks,
                    bool *pbActionFlag);

/* Input_OnMenuEscape @ 0x162ec. */
void Input_OnMenuEscape(TactInputState *pState, const TactInputHooks *pHooks);

/* Input_OnConfirm @ 0x15f3c. The confirm key / action button.
 *
 * Gate: the geoscape branch only closes the geoscape UI; the tactical branch
 * needs bTactFlag plus TactInput_CanScroll. Then a 16-case switch on nUiScreen:
 *   0  commit or cancel the multi-target selection (byte 0x22). Armed: clear
 *      *unk_1571E4+9 and 0x22, re-place the cursor, and for a weapon whose
 *      uSortKey is 13 or 35 clear bFlags29 bit 6 on every selected target cell;
 *      the count is cleared either way. Not armed: sample 7 and toggle
 *      bDeployPanelOpen.
 *   1  sample 16 then Inv_CloseAndCommit.
 *   2  focus the view on the unit stored for the current camera cell, clear the
 *      screen, sample 16.
 *   3, 5, 10, 11, 15  UiModal_End then clear the screen.
 *   4  the command pad: when the commit flag at 0x1508C is set, adopt the saved
 *      unit pointer at 0x15088 as pSelUnit; nUiScreen becomes the low byte of
 *      apUiSlotsA[8] != 0; both scratch fields clear; sample 16.
 *   6  sample 16, clear the screen.   7  sample 14, clear the screen.
 *   12 Options_SaveToRegistry, clear the screen.
 *   8, 9, 13, 14 and anything above 15 do nothing.
 * The original's three arguments are unused except as the Inv_CloseAndCommit
 * argument, so the port takes only the state and the hooks. */
void Input_OnConfirm(TactInputState *pState, const TactInputHooks *pHooks);

/* UI-screen ids used by UiScreen_DismissHigh. */
#define PUFO_UI_SCREEN_TARGET_BEGIN 8u
#define PUFO_UI_SCREEN_TARGET_END   9u
#define PUFO_UI_SCREEN_CANCEL       13u

/* UiScreen_DismissHigh @ 0x159ec. Clears the original's byte_AD712 scratch flag
 * (pbEscapeFlag), and when the screen is the target modal (8/9) ends the modal
 * and clears the screen; screen 13 instead runs the cancel handler and keeps its
 * id. Returns PUFO_UI_DISMISS_MODAL, PUFO_UI_DISMISS_CANCEL or
 * PUFO_UI_DISMISS_NONE. The original's no-branch path returns an uninitialised
 * register, so the port returns an explicit NONE instead. */
#define PUFO_UI_DISMISS_NONE   (-1)
#define PUFO_UI_DISMISS_MODAL  0
#define PUFO_UI_DISMISS_CANCEL 1
i32 UiScreen_DismissHigh(TactInputState *pState, const TactInputHooks *pHooks,
                         bool *pbEscapeFlag);

/* UiScreen_OnBack @ 0x15670. Clears byte_AD711 and, for screens 8/9/13, runs the
 * quit, battle-end-abort or cancel path. */
void UiScreen_OnBack(TactInputState *pState, const TactInputHooks *pHooks,
                     bool *pbEscapeFlag);

/* UiScreen_OnCancel @ 0x16200. Returns the resulting screen id and stores it in
 * pState->nUiScreen when the original does. Screens below 8, or a tactical flag
 * that is clear, return immediately; the modal screens 8/9 clear to 0; screen 13
 * either escapes to 19 or tears the tactical game down and returns to the
 * geoscape; screen 19 does the same teardown; screens 20..21 adopt the low word
 * of apUiSlotsA[11]. */
i32 UiScreen_OnCancel(TactInputState *pState, const TactInputHooks *pHooks);

#endif /* PUFO_INPUT_H */
