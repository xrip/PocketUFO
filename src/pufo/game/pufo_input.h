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

#include "game/pufo_mapcell.h"  /* MapCell, MapGeom, MapCellLookup */
#include "game/pufo_tactctl.h"  /* TactCursor, MapCursorPos, CamOffset */
#include "render/pufo_render.h" /* PadImage */
#include "ui/pufo_control.h"    /* UiControl, UiWidgetBank, UiControlHitCtx */

typedef struct TactState TactState;

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
    u8        nActionLatch;  /* 0x02 unk_1139BA: the tactical action latch, 0..4.
                              * 3 = multi-target selection is live (Tact_BeginMultiTarget
                              * @ 0x18380), 4 = a fire mode is armed (Tact_TryFireMode4/10),
                              * 0 = none. MapCursor_SetPos reads the same byte. */
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

    /* Fields only Input_OnPointer @ 0x13c6c touches. The anActionPhaseSave0 names
     * come from the original's byte array at Map+0x15078, NOT at Map+0x74. Hex-Rays
     * indexes it by byte, and the ARM immediates pin the base: [20] is unk_128A44
     * = 0x1508C (0x1480C), [93] is unk_128A8D = 0x150D5 (0x141A8), [95]/[96]/[97]
     * are unk_128A8F/90/91 = 0x150D7/8/9, and [108] is unk_128A9C = 0x150E4
     * (0x1462C). Each of 0x150D5..0x150D9 also has its own Map_ToggleFlagXXXX
     * handler, which is the second proof of the base. */
    u8   bNoHalfCursor;    /* 0x36 unk_1139EE: skip halving the drag coordinates */
    u8   bPlayerInputOn;   /* 0x38 unk_1139F0: the HUD panel owns the pointer */
    u8   bInvDragActive;   /* 0x3c unk_1139F4: an inventory drag is in progress */
    u32  nUiSlot0;         /* 0x60 apUiSlotsA[0], cleared by UiControl_HitTest */
    i32  nUiSlot3;         /* 0x6c unk_113A24: 8 = drag started, 0 = committed */
    i32  nUiSlot4;         /* 0x70 unk_113A28: 200 = the drag scrolled the view */
    u32  nUiSlot5;         /* 0x74 apUiSlotsA[5]: the four HUD edge flags, low byte
                            * = left, then right, top, bottom */
    u8   bSlot19Active;    /* 0xac unk_113A64: the status-line hit box is live */
    i32  nSlot20X;         /* 0xb0 unk_113A68: left status column */
    i32  nSlot21X;         /* 0xb4 unk_113A6C: right status column */
    i32  nSlot22Y;         /* 0xb8 unk_113A70: status row top */
    i32  nSlot23H;         /* 0xbc unk_113A74: status row height, must be > 0 */
    i32  nSlot24Hit;       /* 0xc0 unk_113A78: 1 left column, 2 right, 0 none */
    u8   bEdgeTestOn;      /* 0x150D5 anPhaseSave0[93]: run the HUD edge tests */
    u8   bClickCommits;    /* 0x150D7 anPhaseSave0[95]: a click on the same cell acts */
    u8   bDragScrollA;     /* 0x150D8 anPhaseSave0[96] */
    u8   bDragScrollB;     /* 0x150D9 anPhaseSave0[97] */
    u8   bInvPanelOpen;    /* 0x150E4 anPhaseSave0[108]: the inventory is open */
    i32  anCamDragX;       /* 0x4eb4, anCamWorldOff[2]: the drag start x */
    i32  anCamDragY;       /* 0x4eb8, anCamWorldOff[3]: the drag start y */

    /* Fields only Input_OnPointerUp @ 0x148d0 touches. */
    i32  nActionMode;                    /* 0x4c unk_113A04 */
    i32  anActionCost[12];               /* 0x4e4c, dword stride, Ui_FillActionCosts @ 0x26104 */
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
    void (*pfnCursorChanged)(void *pUser);  /* nullsub_3 (0x22b50) */
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
    /* Geoscape_CloseUI (0x8b94c), on the &unk_1864F8 screen object. */
    void (*pfnGeoscapeCloseUi)(void *pUser);
    /* Inv_CloseAndCommit (0x15a48), NOT ported. */
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
    /* Geoscape_OnPointer (0x872d0), the whole geoscape branch. */
    void (*pfnGeoscapePointer)(i32 nX, i32 nY, bool bDown, void *pUser);
    /* Ui_HitTestTactItem @ 0x11ac0: hit-test the ground list or the inventory
     * slots, true when something under the pointer was hit. Ported for real in
     * src/pufo/game/pufo_invdrag.c; the app wires this seam to that function, which
     * needs the Map drag state (pufo_invdrag.h documents the six Map fields). */
    bool (*pfnHitTestTactItem)(i32 nX, i32 nY, void *pUser);
    /* Inv_ClearHitSlot @ 0x11f40. Ported for real in src/pufo/game/pufo_invdrag.c
     * as Inv_ClearHitSlot(const InvHitTestCtx *); the app wires this seam to it. */
    void (*pfnInvClearHitSlot)(void *pUser);
    /* Inv_HandlePointer @ 0x12260, the drop path. Ported for real in
     * src/pufo/game/pufo_invdrag.c as Inv_HandlePointer(const InvDropCtx *); the app
     * wires this seam to it. The original reads the pen position from the widget bank
     * (pBank+0x1044 / +0x1048) rather than taking it as an argument. */
    void (*pfnInvHandlePointer)(struct UiWidgetBank *pBank, void *pUser);
    /* MapView_FocusOnUnit @ 0x2c758. Despite the name it centres the camera on a
     * CELL: it reads the cell's nX, nY and nZ, recentres the camera scroll and
     * sets the cursor layer to nZ + 1. */
    void (*pfnFocusOnCell)(MapCell *pCell, void *pUser);
    /* Geoscape_OnPointerUp (0x89450), on the &unk_1864F8 screen object. */
    void (*pfnGeoscapePointerUp)(void *pUser);
    /* Titles_Shutdown (0x3826c), NOT ported, then GameUI_InitStartup (0x38a28), screen 16. */
    void (*pfnTitlesToStartup)(void *pUser);
    /* EndScreen_Shutdown (0x389f8), NOT ported, screen 18. */
    void (*pfnEndScreenShutdown)(void *pUser);
    /* StatusMsg_Push @ 0x2dda4 with a record index into aTextRecs @ 0xb38d0.
     * Input_OnPointerUp uses record 1 ("not enough time"). */
    void (*pfnPushTextRecord)(i32 nRecord, void *pUser);
    void *pUser;
} TactInputHooks;

/* The shared precondition of the four directional handlers: no active fire mode
 * and no queued action. */
bool TactInput_CanScroll(const TactInputState *pState);

void Input_OnLeft(TactInputState *pState, const TactInputHooks *pHooks);
void Input_OnRight(TactInputState *pState, const TactInputHooks *pHooks);
void Input_OnUp(TactInputState *pState, const TactInputHooks *pHooks);
void Input_OnDown(TactInputState *pState, const TactInputHooks *pHooks);

/* Input_OnAction @ 0x1557c. Sets the original's UiBank_bActionFlag scratch flag. */
void Input_OnAction(TactInputState *pState, const TactInputHooks *pHooks,
                    bool *pbActionFlag);

/* Input_OnMenuEscape @ 0x162ec. */
void Input_OnMenuEscape(TactInputState *pState, const TactInputHooks *pHooks);

/* Input_OnConfirm @ 0x15f3c. The confirm key / action button.
 *
 * Gate: the geoscape branch only closes the geoscape UI; the tactical branch
 * needs bTactFlag plus TactInput_CanScroll. Then a 16-case switch on nUiScreen:
 *   0  commit or cancel the multi-target selection (the action latch at 0x02).
 *      Armed: clear *unk_1571E4+9 and the latch, re-place the cursor, and for a
 *      weapon whose
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

/* What the screen-0 tactical path of Input_OnSecondary needs beyond the Map
 * fields: the tactical queue (the original's unk_157198) and the cell grid. */
typedef struct TactInputWorld {
    TactState     *pQueue;
    MapCellLookup  pfnLookup;
    void          *pLookupUser;
} TactInputWorld;

/* Input_OnSecondary @ 0x156e0. The secondary / right-click key.
 *
 * Gate: the geoscape branch is a no-op (the original calls nullsub_8); the
 * tactical branch needs bTactFlag plus TactInput_CanScroll. Then nUiScreen:
 *   1, 2, 4  run Input_OnConfirm.
 *   6, 10, 11, 12, 15  clear the screen.
 *   3, 5, 7, 8, 9, 13, 14 and anything above 15  do nothing.
 *   0  the tactical path below.
 *
 * Screen 0, two modes picked by nActionLatch (byte 0x02):
 *   == 3  pop the last selected target: clear bFlags29 bit 6 on
 *         apMultiTargets[nMultiTargetCount-1], decrement, and when the count
 *         reaches 0 also clear *unk_1571E4+9. Nothing happens when it is <= 0.
 *   other set the escape-flag scratch (unk_AD712), then require a cursor inside
 *   the map. With the selected unit's cell (nX,nY,nZ) and the facing delta
 *   (dx,dy) from g_aStepDir:
 *     - a small unit on an even facing, a valid cell at (nX+dx, nY+dy, nZ) and
 *       MapCell.bFlags57[0] bit 3 set there -> Map_QueueDoorAction with the
 *       cursor cell apCells[nLayerScratch-1][nCursorY][nCursorX] and bFacing.
 *     - always afterwards: if the cursor cell differs from the unit's cell in x
 *       or y, Unit_FaceToward it and kick the queue. */
void Input_OnSecondary(TactInputState *pState, const TactInputHooks *pHooks,
                       const TactInputWorld *pWorld, bool *pbEscapeFlag);

/* --------------------------------------------------- the held-key repeat timer */

/* UiPulse_OnTimer @ 0x36630, the timer installed as 0xC353.
 *
 * Gate: the geoscape branch hands over to Geoscape_PulseUpdate and stops; otherwise
 * the whole body needs `bTactFlag`.
 *
 * 1. `nUiSlot0` @ Map+0x60 (apUiSlotsA[0]) counts up and wraps past 3. On the wrap it
 *    force-releases the two inventory pulse controls and clears itself. ARM
 *    0x36688..0x366C0 passes x = 0, y = 0, bDown = 0 and bForceClick = 1, so a control
 *    sitting in state 1 fires its click with no pointer involved. UiControl_HitTest
 *    also clears Map+0x60 on a press, which is why the same slot serves both paths.
 *
 * 2. The four bytes at Map+0x74..0x77 -- `nUiSlot5`, the same four the pointer handler
 *    calls HUD edge flags -- are the held direction state, repeated in the order
 *    left, right, up, down.
 *
 * 3. Four more held flags live in the widget bank at +0x104C..0x104F (IDB
 *    unk_AD71C..unk_AD71F) and repeat up, down, left, right in that order, but only
 *    while `nUiScreen` is 0.
 *
 * 4. `nUiSlot3` @ Map+0x6C counts down while positive, `nUiSlot4` @ Map+0x70 counts up
 *    to 200, and on the tick where it reaches exactly 15 either opens screen 15 and
 *    plays sample 15, or runs the secondary key. Which one is decided by the byte at
 *    Map+0x150D9 (`bDragScrollB` here, `bMenuByPen` in pufo_tactui.h -- the same
 *    anPhaseSave0[97], named twice in the port). */
typedef struct UiPulseHooks {
    /* Geoscape_PulseUpdate (0x954dc), NOT ported. */
    void (*pfnPulseUpdate)(void *pUser);
    /* Sound_Play @ 0x9e0c0, sample 15. */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void *pUser;
} UiPulseHooks;

typedef struct UiPulseCtx {
    TactInputState         *pState;
    const TactInputHooks   *pHooks;
    const TactInputWorld   *pWorld;      /* for the secondary-key branch */
    bool                   *pbEscapeFlag;/* pBank+0x1042, passed on to Input_OnSecondary */
    UiControl              *pCtlPulseA;  /* unk_AC990 = pBank+0x2C0, widget 22 */
    UiControl              *pCtlPulseB;  /* unk_AC9B0 = pBank+0x2E0, widget 23 */
    const UiControlHitCtx  *pHit;
    const u8               *pbBankHeld;  /* pBank+0x104C..0x104F: up, down, left, right */
    const UiPulseHooks     *pTimers;
} UiPulseCtx;

void UiPulse_OnTimer(const UiPulseCtx *pCtx);

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

/* UiScreen_OnBack @ 0x15670. Clears UiBank_bActionFlag and, for screens 8/9/13, runs the
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

/* What Input_OnPointer reaches beyond the Map fields: the widget bank the window
 * procedure owns, the two standalone controls, the screen-17 list state, the HUD
 * pad whose height and origin drive the tactical hit boxes, and the camera scratch
 * the drag path writes. */
typedef struct TactPointerWorld {
    TactInputWorld  tact;        /* the queue plus the cell grid lookup */
    UiWidgetBank   *pBank;
    UiControl      *pLookCtl;    /* unk_1571E4, hit first on screen 0 */
    UiControl      *pListCtl;    /* unk_1571B4, hit by the screen-17 list mode */
    i32              nListMode;   /* *0x157188: 0/1 four rows, 2 seven rows + control */
    u8               abListHit[7];/* unk_156AB0: which list rows the pointer is over */
    PadImage        *pHudPad;     /* Map+0xfc unk_113AB4: nOriginY and uHeight are read */
    CamOffset       *pCamOffset;  /* Map+0x4a40 / 0x4a41, written by SetCamOffset */
    bool             bCursorBusy; /* unk_157180->bBusy, the MapCursor_SetPos gate */
    bool             bListRowSticky; /* unk_188D4C: screen 17 mode 0 keeps its row */
    void           (*pfnSlot16)(void); /* apUiSlotsA[16] @ Map+0xa0, screen-17 modal action */
    void           (*pfnSlot18)(void); /* apUiSlotsA[18] @ Map+0xa8, the other one */
    UiControlHitCtx  hit;         /* the pad blit callbacks plus &pState->nUiSlot0 */
    bool             bActionFlag; /* UiBank_bActionFlag, set by Input_OnAction */
} TactPointerWorld;

/* Input_OnPointer @ 0x13c6c. WM_MOUSEMOVE (bDown = 0) and WM_LBUTTONDOWN
 * (bDown = 1) from PocketUFO_WndProc.
 *
 * On entry it stores the pointer position into the bank and clears the four HUD
 * edge flags at Map+0x74..0x77. The geoscape branch hands everything over and
 * stops. Screen 17 is a list of rows, not widgets: it clears abListHit and marks
 * the rows under the pointer, in one of three modes, and only mode 2 also
 * hit-tests a control.
 *
 * The tactical branch needs bTactFlag. Before the screen switch it evaluates the
 * status-line hit box (Map+0xac..0xc0) and, for screen 13, clears the two fire
 * flags, the trajectory flag and the queue-active flag. The screen switch then runs
 * only under the same gate the keyboard handlers use, TactInput_CanScroll.
 *
 * Screen 0 is the tactical map. It hit-tests the look control first and stops on a
 * hit; then the multi-target list rows under the HUD (five at most, 18 pixels
 * apart, based at 283 with the deploy panel open and 300 - HUD height otherwise);
 * a hit there recentres the camera on that target's cell and plays sample 17. With
 * no list hit it measures the HUD band height (303 open, 320 - HUD height closed)
 * and its thirds, and either sets one or more of the four edge flags, or, when the
 * pointer is over the map itself, runs the camera drag: SetCamOffset with the
 * halved pointer position, then re-place the cursor from the iso offsets plus the
 * camera scratch bytes, then commit a click (Input_OnAction) or mark a drag.
 * Finally it hit-tests the widgets: the four live ones while multi-targeting,
 * otherwise all nineteen.
 *
 * Screen 1 is the inventory: thirteen widgets, then the ground/slot hit test and
 * the drag commit through the injected inventory hooks.
 *
 * The original returns a value its only two callers ignore, so the port returns
 * void. */
void Input_OnPointer(TactInputState *pState, const TactInputHooks *pHooks,
                     TactPointerWorld *pWorld, i32 nX, i32 nY, bool bDown);

/* Input_OnPointerUp @ 0x148d0. WM_LBUTTONUP from PocketUFO_WndProc.
 *
 * It takes no coordinates: every UiControl_HitTest call passes x = 0, y = 0 and
 * bDown = 0, so the hit box is never measured and only the release half of the
 * control state machine runs. That is the whole point of the function: it releases
 * every control the current screen owns, which is what fires their click handlers.
 *
 * Prologue: the four HUD edge bytes at Map+0x74..0x77 are cleared individually and
 * apUiSlotsA[4] (Map+0x70) is set to 200. The geoscape branch stops there.
 *
 * Screens 16, 17 and 18 are handled before the tactical gate. 16 goes from the
 * titles to the startup screen, 18 shuts the end screen down, and 17 is the list:
 * it releases the list controls, then fires the click handler of the first row
 * marked in abListHit and clears the marks. The row block depends on the mode:
 * mode 0 rows 0..3 (bank index 113..116), mode 1 rows 4..7 (117..120), mode 2 rows
 * 8..14 (121..127) plus the list control. Mode 0 is the odd one: it clears only the
 * rows before the hit one, and clears the hit row itself only when the sticky flag
 * unk_188D4C is clear.
 *
 * The tactical gate is bTactFlag and (TactInput_CanScroll or bGameTickHold). When
 * the status line is live (Map+0xac) the release instead depends on the hover column
 * at Map+0xc0: 0 clears it, 1 and 2 end the modal and then run the stored action at
 * Map+0xa0 or Map+0xa8.
 *
 * Screen 3 is the action pad. After the release sweep it re-derives the action latch
 * from nActionMode (0..2 -> 2, 11 -> 1, 8 with a weapon whose uSortKey is not 23 ->
 * 0, 12 -> 0), refuses an unaffordable mode by pushing text record 1 and switching
 * to mode 12, re-places the cursor, and keeps screen 3 only for mode 3 or 8.
 * Screen 5 also clears the screen. Screens 20 and 21 adopt apUiSlotsA[11]. */
void Input_OnPointerUp(TactInputState *pState, const TactInputHooks *pHooks,
                       TactPointerWorld *pWorld);

#endif /* PUFO_INPUT_H */
