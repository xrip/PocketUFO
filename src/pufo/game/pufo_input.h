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
