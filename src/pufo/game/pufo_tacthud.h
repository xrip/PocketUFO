#ifndef PUFO_TACTHUD_H
#define PUFO_TACTHUD_H

/* Tactical HUD action buttons: the click handlers the tactical screen installs
 * through Tact_InitUiControls @ 0x3b498. They act on Map.pSelUnit directly and
 * are reached from UiControl_HitTest's pfnClick, not from a key.
 *
 * Evidence: TactHud_ToggleKneel @ 0x16570, TactHud_ClimbUp @ 0x1674c,
 *           TactHud_ClimbDown @ 0x16af0. */

#include "game/pufo_mapcell.h"
#include "game/pufo_mapctx.h"
#include "game/pufo_tactctl.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_tactui.h"
#include "game/pufo_invdrag.h"  /* InvHitState */
#include "render/pufo_render.h"
#include "game/pufo_unit.h"
#include "ui/pufo_control.h"
#include "ui/pufo_modal.h"
#include "ui/pufo_panel.h"

/* The texts.dat records the kneel action announces. Kneeling is the shot
 * accuracy bonus, so the message is about marksmanship, not about the pose:
 * aTextRecs + 205*1606 = 0x5060E and + 205*1607 = 0x506DB, read straight from
 * the ARM at 0x1661C and 0x16654. */
#define PUFO_TXTREC_KNEEL_ON  1606
#define PUFO_TXTREC_KNEEL_OFF 1607

typedef struct TactHudCtx {
    Soldier       *pSelUnit;      /* Map.pSelUnit @ 0x15334 */
    TactState     *pQueue;        /* unk_157198 */
    MapGeom        geom;          /* nMapDimX @ 0x1511B, nMapDimY @ 0x1511C */
    MapCellLookup  pfnLookup;
    void          *pLookupUser;

    bool (*pfnArmorTuWarn)(const Soldier *pUnit, i32 nTuCost, void *pUser);
    void (*pfnPushTextRecord)(i32 nRecord, void *pUser);
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void  *pCallbackUser;
} TactHudCtx;

/* TactHud_ToggleKneel @ 0x16570. Costs 2 TU and flips Soldier.bFlags bit 0.
 * The pose is adjusted by hand (nAnimPose +/- 1); Unit_UpdateAnimPose is not
 * called, unlike the queue's stand-from-kneel command. Nothing is queued. */
void TactHud_ToggleKneel(const TactHudCtx *pCtx);

/* TactHud_ClimbUp @ 0x1674c / TactHud_ClimbDown @ 0x16af0. Both check the cell
 * one layer up (down) and, when it is clear, enqueue a vertical step with
 * nFacing 1 (-1) and start the queue. Neither spends TU here; the cost is paid
 * when the queue reaches command 5. */
void TactHud_ClimbUp(const TactHudCtx *pCtx);
void TactHud_ClimbDown(const TactHudCtx *pCtx);

/* --- Inv_OpenScreen @ 0x16ef4 : the inventory button (widget 4) ---
 *
 * Gate order, straight from the ARM at 0x16efc..0x16f34:
 *   1. sample 16 plays only when LOBYTE(apUiSlotsA[2]) @ Map+0x68 is non-zero;
 *      the sample is skipped but the rest of the gate still runs.
 *   2. Map.nXcomUnitCount @ 0x1511F must be non-zero.
 *   3. pSelUnit->nUnitType must be 0..3, so only a humanoid gets an inventory.
 *
 * On success it resets the ground list to "all" (nInvListSel = 0xFF at 0x4A26,
 * nInvFilterType = 0 at 0x1A22), rebuilds it, selects the "All" filter pad and
 * clears the other six (pBank+0x2AA = 0xAC97A, pBank+0x30A = 0xAC9DA and the five
 * at 0xAC9FA/0xACA1A/0xACA3A/0xACA5A/0xACA7A), clears the action latch, re-stamps
 * the cursor at its own position, recomputes the unit's inventory burden, sets
 * nUiScreen = 1 and sets the iso-draw byte unk_138E68 @ 0x254B0.
 *
 * Deviation: the original reads pSelUnit without a NULL test after checking the
 * unit count; the port refuses a NULL selection. */
typedef struct TactHudInvOpenCtx {
    TactUiState  *pUi;                              /* 0x3A, 0x4A26, 0x1A22, pads */
    Soldier      *pSelUnit;                         /* Map+0x15334 */
    TactCursor   *pCursor;                          /* 0x15128 / 0x1512C / 0x15138 */
    MapGeom       geom;
    MapCellLookup pfnLookup;
    void         *pLookupUser;
    u8            nXcomCount;                       /* Map+0x1511F */
    u8            bSweepGate;                       /* LOBYTE(apUiSlotsA[2]) @ 0x68 */
    bool          bSelUnitLarge;                    /* Unit_bIsLarge(pSelUnit) */
    void        (*pfnPlaySample)(i32 nSample, void *pUser);
    /* Unit_RecalcInventoryStat @ 0x49ef0 needs these; a NULL pointer skips the
     * recalc, which is a port deviation. */
    const InvStatHooks *pInvStat;
    void         *pCallbackUser;
} TactHudInvOpenCtx;

bool TactHud_OpenInventory(const TactHudInvOpenCtx *pCtx);

/* --- TactHud_UseSpecialItem20 @ 0x17d54 : the special action pad (widget 40) ---
 *
 * The pad is action mode 8, and the handler writes that mode and closes the menu
 * screen BEFORE it looks at the weapon, so an unusable weapon still leaves
 * nActionMode = 8 and nUiScreen = 0 behind. It then branches on the held item's
 * ObjectRec.uSortKey @ 0xC8:
 *
 *   20  Spend anActionCost[8] from the unit's own TU (not through the queue),
 *       mark every enemy and every Xcom unit whose cell is within 10 cells
 *       (Euclidean distance on nX/nY, `sqrt(d2) > 10.0` in IEEE754) with
 *       Soldier.bSpecialMark @ 0x1B0 = 1 and clear the mark on the rest, then
 *       open nUiScreen = 6.
 *   21  The medikit. Find the target, refuse it, or enqueue command 18.
 *   23  Affordability only: when the unit can pay anActionCost[8], set the action
 *       latch Map+0x02 to 4 and stop. No queue, no target.
 *   any other value  stop, with mode 8 and screen 0 already written.
 *
 * All three gates compare pSelUnit->uTu against the full dword anActionCost[8];
 * the sort-key-20 subtraction uses the cost truncated to 16 signed bits (ARM
 * 0x180AC `MOV R3,R2,LSL#16 / SUB R3,R1,R3,LSR#16`), while the cost handed to the
 * queue is the full dword (0x1804C). Failure in any of the three pushes aTextRecs
 * record 1, "not enough time".
 *
 * The medikit target search, 0x17E00..0x17F3C:
 *   - the target cell is pSelUnit->pCell stepped one facing in the unit's own
 *     direction, using the same layer as the unit's cell;
 *   - Unit_CanStepExitDir @ 0x36d68 must say the step is clear, else there is no
 *     cell target;
 *   - the target is that cell's occupant MapCell.pUnit @ 0x30, which may be NULL;
 *   - when it is NULL, the first civilian standing on the SELECTED unit's own cell
 *     that is not large, is humanoid (nUnitType <= 3) and has uHp != 0 becomes the
 *     target, flagged as a civilian find.
 *   - a non-civilian target must be non-NULL, operative and not large, else the
 *     handler pushes records 9 and 10 ("To use a medikit" / "stand in front of the
 *     soldier") and stops.
 *   - a target with bKneelOrPrimed @ 0xF6 clear that is at full HP (uHp ==
 *     auStat[1]), has bMorale == 100 and has spent no more than uHp/2 TU pushes
 *     records 11 and 12 ("Does not need a medikit" plus an empty tail record) and
 *     stops.
 *   - otherwise nFireTgtX/Y/Z @ 0x14040/0x14044/0x14048 take the TARGET cell and
 *     its layer, and command 18 is enqueued with the target, pSelWeapon,
 *     anActionCost[8] and mode 8, then the queue is started.
 *
 * Deviation: the original dereferences pSelWeapon->pObj with no NULL test; the
 * port refuses a missing weapon or item record. Deviation: the original indexes
 * apCells for the target cell without bounds checks; the port's injected lookup
 * returns NULL out of range and the target search then falls through to the
 * civilian scan. */
#define PUFO_ACTION_MODE_SPECIAL 8
#define PUFO_SPECIAL_MARK_RADIUS  10.0
/* The three uSortKey values these branches test are defined next to the record
 * field they name, in game/pufo_objectrec.h. */

/* The four medikit refusal records, pushed in pairs. aTextRecs + 205*9 = 0xB4005
 * and + 205*10 = 0xB40E2 at ARM 0x17F74/0x17F90; + 205*11 = 0xB419F and
 * + 205*12 = 0xB4268 at 0x17FE4/0x18000. Records 9+10 read "To use a medikit"
 * plus "stand in front of the soldier"; 11+12 read "Does not need a medikit"
 * plus an empty tail. */
#define PUFO_TXTREC_MEDKIT_WHY   9
#define PUFO_TXTREC_MEDKIT_WHY2 10
#define PUFO_TXTREC_MEDKIT_FIT   11
#define PUFO_TXTREC_MEDKIT_FIT2 12

typedef struct TactHudSpecialCtx {
    GameMap     *pMap;          /* rosters, counts, pSelUnit, pSelWeapon, geom */
    TactUiState *pUi;           /* nActionMode, nUiScreen, nFireTgt*, anActionCost */
    TactState   *pQueue;        /* unk_157198 */
    MapCellLookup pfnLookup;
    void         *pLookupUser;
    void        (*pfnPlaySample)(i32 nSample, void *pUser);
    void        (*pfnPushTextRecord)(i32 nRecord, void *pUser);
    void         *pCallbackUser;
} TactHudSpecialCtx;

void TactHud_UseSpecialItem(const TactHudSpecialCtx *pCtx);

/* PadImage_BlitDecimal @ 0x36c58. Draw a decimal number as pad images.
 *
 * The original reaches an 11-entry `PadImage *` table inside its HUD context at
 * offset 0xCC (0x36CC4 computes `pCtx + 4*idx` then reads `+51` dwords, i.e. +204).
 * Index 0..9 is the digit for `0..9` -- 0x36D28 computes `pCtx + 4*ch - 192`, and
 * 4*'0' - 192 is zero, so the character picks the slot directly. Index 10 is the
 * filler pad, reached as `pCtx + 4*10` when `bLeadZero` is set and `pCtx + 0`, i.e.
 * slot 0, when it is not.
 *
 * `nWidth` is a minimum field width. When it is larger than the digit count, the
 * difference is filled with the filler pad first. Every pad advance is its width
 * plus one, so digits and filler alike leave a one-pixel gap; the filler loop adds
 * the gap to `x` directly (0x36CF8) and the digit loop does it through the for
 * increment (0x36D14: `x = result + 1`).
 *
 * Each pad has its `nOriginX`/`nOriginY` written before it is blitted, so the pads
 * are stateful scratch, not immutable glyphs. Returns the right edge of the last
 * digit, `x + uWidth`, which is what the original returns from 0x36D54.
 *
 * All six callers are `TactHud_DrawSelectedUnitPanel (0x1d5ec)`. */
#define PUFO_HUD_DIGIT_PADS 11

typedef void (*HudPadBlitFn)(PadImage *pPad, void *pUser);

i32 TactHud_BlitDecimal(PadImage *const *apPads, i32 x, i32 y, i32 nVal,
                        i32 nWidth, bool bLeadZero,
                        HudPadBlitFn pfnBlitEx, void *pUser);

/* --- the HUD draw family that Render_Frame (0x22310, ported at pufo_renderframe.c:11) dispatches ----------
 *
 * All three read pad geometry out of pBank and centre a label on it. The bank offsets the
 * decompiler shows as "u16 field indices" are the pad records themselves:
 *
 *   Menu_DrawLabeledPads   848*2 = 1696 -> unk_ACD70, bank widgets 53..59, vertical +3
 *   TactHud_DrawModalPanel 1680*2 = 3360 -> unk_AD3F0, bank widgets 105..112, vertical +2
 *   TactHud_CopyStatusText  704*2 = 1408 -> unk_ACC50, bank widgets 44..52, centred on a
 *                                          fixed 23 rather than on the pad width
 *   TactHud_DrawTopBar      960*2 = 1920 -> unk_ACE50, bank widgets 60..88, labels on 60..69,
 *                                          78..82 and 83..87, also centred on a fixed 23
 *
 * Those three spans are the same groups Tact_Shutdown (0x3d568, ported at pufo_tactend.c:228) walks (offsets 53/7,
 * 105/8 and 44/9), which is the cross-check that the arithmetic is right. */
#define PUFO_HUD_MENU_PADS        7
#define PUFO_HUD_MODAL_PADS       8
#define PUFO_HUD_MODAL_LABELS     7
#define PUFO_HUD_MODAL_LABEL_STRIDE 40
#define PUFO_HUD_STATUS_PADS      9

/* The first widget of each span. Each one is a second identity, not a new number: the same
 * range is a group in g_aUiBankSpans (pufo_control.c:291 -- {32,12} {44,9} {53,7} {60,29} {89,9}
 * {105,8}), the range Tact_InitUiControls @ 0x3b498 installs its click handlers on (the twelve
 * fire-mode handlers on 32..43, the nine sub-mode handlers on 44..52, the seven menu rows on
 * 53..59, the option pads on 60..88, the nine action buttons on 89..97, SaveLoad_Slot0..6 +
 * MenuBtn_Confirm5 on 105..112), and the range the draw below blits. One span, three uses. */
#define PUFO_HUD_INV_FIRST    19  /* screen 1  -- TactHud_DrawInventoryScreen's 13 controls are
                                   * pBank + 0x260 (0x260 / 0x20 = 19), the same 13 widgets
                                   * Input_OnPointer @ 0x13c6c hit-tests on that screen */

/* The seven inventory filter pads are bank nState bytes, and they are NOT a run: widget 21
 * (0xAC97A), widget 24 (0xAC9DA, the "All" pad) and widgets 25..29 (0xAC9FA, 0xACA1A, 0xACA3A,
 * 0xACA5A, 0xACA7A). TactHud_OpenInventory @ 0x16ef4 lights 24 and darkens the six at 0x16F68
 * and 0x16F78..0x16F98; Ui_SetInvFilter re-lights them on a tap. */
#define PUFO_HUD_INV_FILTER_X     21
#define PUFO_HUD_INV_FILTER_ALL   24
#define PUFO_HUD_INV_FILTER_FIRST 25
#define PUFO_HUD_ROSTER_FIRST 32  /* screen 3  */
#define PUFO_HUD_STATUS_FIRST 44  /* screen 5  */
#define PUFO_HUD_MENU_FIRST   53  /* screen 7  */
#define PUFO_HUD_TOPBAR_FIRST 60  /* screen 12 */
#define PUFO_HUD_ACTION_FIRST 89  /* screen 15 */
#define PUFO_HUD_MARKER_FIRST 104 /* screen 6  -- TactHud_DrawUnitMarkers's one control (pBank + 0xD00 = unk_AD3D0) */
#define PUFO_HUD_CONFIRM_FIRST 129 /* screen 0x13 -- TactHud_DrawConfirmDialog's one control (pBank + 4128 = unk_AD6F0) */
#define PUFO_HUD_MODAL_FIRST  105 /* screens 10 and 11 (Menu_OpenSaveSlots 0x188a4 / Menu_OpenLoadSlots 0x18864) */

/* Menu_DrawLabeledPads @ 0x217e0. Seven pads blitted and seven labels drawn, but the label
 * records are a sparse map, not the loop counter: index 5 reads record 402 and index 6 reads
 * record 23, while 0..4 read 18..22. */
/* Menu_DrawLabeledPads @ 0x217e0. Seven pads blitted and seven labels drawn, but the label
 * records are a sparse map, not the loop counter: index 5 reads record 402 and index 6 reads
 * record 23, while 0..4 read 18..22. Split out because that map is the whole interest of the
 * function and it is worth being able to assert it. */
i32 Menu_LabelRecordIndex(i32 nIdx);

i32 Menu_DrawLabeledPads(UiControl *pPads, const u8 *pTextRecs, const GfxColorEnv *pColor,
                         UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser);

/* TactHud_DrawModalLeft @ 0x21bac and TactHud_DrawModalRight @ 0x21cec. The same body; they
 * differ only in the modal text record, 1102 versus 1103. Eight pads are blitted but only
 * seven labels are drawn - the count mismatch is the original's. */
i32 TactHud_DrawModalPanel(UiControl *pPads, const u8 *pLabelTable, i32 nModalMode,
                           i32 nTextRecIndex, i32 *pnActionPhaseSave112,
                           UiModalAnim *pAnim, const u8 *pTextRecs,
                           UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser);

/* TactHud_CopyStatusText @ 0x21688. The status line is text record 5 (unk_B3CD1 =
 * aTextRecs + 205*5), drawn white centred at y = 100; then nine pads each get their own index
 * printed as a decimal. */
i32 TactHud_CopyStatusText(UiControl *pPads, const char *pszStatus, const GfxColorEnv *pColor,
                           UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser);

/* --- the nine tactical action buttons ---------------------------------------------
 *
 * TactHud_DrawActionButtons @ 0x21430. Bank widgets 89..97 (unk_AD1F0 = pBank + 2848,
 * and 1424*2 = 2848), the same span Tact_Shutdown (0x3d568, ported at pufo_tactend.c:228) walks as 89/9.
 *
 * Each button first has its bEnabled cleared, then a per-index rule decides whether it is
 * shown at all. A shown button gets bEnabled = 1 and its y REWRITTEN to 19 * (count of
 * buttons already drawn) + 87, so the visible buttons stack from y = 87 in 19-pixel steps
 * regardless of which ones were hidden. The hidden ones keep their stored y and are simply
 * not blitted. */
#define PUFO_HUD_ACTION_PADS 9

typedef struct TactHudActionGate {
    u8  nActionMode;        /* Map.gap_2[1] @ 0x0002 */
    i32 nMultiTargetCount;  /* Map.nMultiTargetCount */
    u8  bDeployPanelOpen;   /* Map.gap_86272[16] @ 0x15110 */
} TactHudActionGate;

/* The text record for button nIdx, or -1 when the button is hidden. Records 397 and 401 appear
 * in the ARM as `396 | 1` and `400 | 1`, which is the compiler sharing a constant, not an
 * operation the code means to perform. */
i32 TactHud_ActionButtonRecord(const TactHudActionGate *pGate, i32 nIdx);

i32 TactHud_DrawActionButtons(UiControl *pPads, const TactHudActionGate *pGate,
                              const u8 *pTextRecs, const GfxColorEnv *pColor,
                              UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser);

/* TactHud_DrawConfirmDialog @ 0x1eb68 (screen 0x13) and TactHud_DrawStatusBanner @ 0x1e920
 * (screen 0xD). Each owns one pad: bank widget 129 (unk_AD6F0 = pBank + 4128) and bank widget
 * 128 (unk_AD6D0 = pBank + 4096). The banner is skipped entirely when the fade counter
 * (apUiSlotsA[1]) is positive.
 *
 * The four labels the dialog draws are TEXTS records, not scratch: unk_EACE0 = TEXTS[1104] "OK",
 * unk_EADAD = TEXTS[1105] "Имя солдата", unk_EAE7A = TEXTS[1106] "Новое звание" and unk_EAF47 =
 * TEXTS[1107] "Продвижение". They read as zero-filled .bss because the boot record-copy loop that
 * fills them stores through a computed address and leaves no xref -- the same census trap as the
 * Map+0x30/0x32 STRHs in Part 81. The banner screen shares TEXTS[1104] for its own pad label. */
#define PUFO_TEXT_REC_PROMOTION        1107 /* unk_EAF47 "Продвижение", centred at y 10 */
#define PUFO_TEXT_REC_PROMOTION_NAME   1105 /* unk_EADAD "Имя солдата", at (10, 30) */
#define PUFO_TEXT_REC_PROMOTION_RANK   1106 /* unk_EAE7A "Новое звание", at (170, 30) */
typedef struct TactHudBannerState {
    i32 nFadeCount;        /* apUiSlotsA[1]: a positive value skips the whole banner */
    i32 nReadyFlag;        /* anActionPhaseSave0[92]: non-zero draws the banner green */
    char szBanner[16];     /* anActionPhaseSave0[28], the centred banner text */
    char szTimer[16];      /* anActionPhaseSave0[58], drawn at (200, 260) */
    i32 nTimerValue;       /* anActionPhaseSave0[58] as a number: 45 turns it red */
} TactHudBannerState;

i32 TactHud_DrawConfirmDialog(UiControl *pPad, const PadImage *pBackdrop,
                              const char *szPadLabel, const char *szTitle,
                              const char *szLeft, const char *szRight,
                              UiPanel *pPanelA, UiPanel *pPanelB,
                              const GfxColorEnv *pColor,
                              UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser);

i32 TactHud_DrawStatusBanner(UiControl *pPad, const PadImage *pBackdrop,
                             const TactHudBannerState *pState, const char *szFooter,
                             const char *szPadLabel,
                             UiPanel *pPanelA, UiPanel *pPanelB,
                             const GfxColorEnv *pColor,
                             UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser);

/* --- the fire-mode roster panel (screen 3) ----------------------------------------
 *
 * TactHud_DrawUnitRoster @ 0x21908. Bank widgets 32..43 (unk_ACAD0 = pBank + 1024, and 512*2 =
 * 1024), the same span Tact_Shutdown (0x3d568, ported at pufo_tactend.c:228) walks as 32/12. Twelve fire modes;
 * only the ones with a non-zero entry are drawn, and a drawn pad has its y REWRITTEN to
 * 23 * (drawn count so far) + 67, so the panel packs the visible modes. */
#define PUFO_HUD_ROSTER_PADS 12
#define PUFO_HUD_ROSTER_NAME_STRIDE 30
/* The three fixed records this family reads by number. */
#define PUFO_TEXT_REC_TU_LABEL   389 /* unk_C7051 "Тч: ", the number 0x21A88 strcat onto */
#define PUFO_TEXT_REC_TIME_LABEL 390 /* unk_C711E "Вр.: ", the number 0x21B00 strcat onto */
#define PUFO_TEXT_REC_STATUS       5 /* unk_B3CD1, the status line TactHud_CopyStatusText centres at y 100 */

typedef struct TactHudRoster {
    const void *pSelWeapon;    /* Map.pSelWeapon: NULL skips the whole panel */
    i32 anModeFlag[12];        /* Map + 20092, the twelve per-mode values also drawn as a number */
    i32 anModeValue[12];       /* Map + 20044 */
    /* 0xB3768, twelve 30-byte mode names, BELOW aTextRecs @ 0xB38D0. Assets_LoadStartup fills it in
     * three steps (0x386BC..0x3875C): a 10-iteration loop at stride 205 out of the text blob offset
     * 0x90F1 (record 181) into 0xB3768 at stride 30, then one strcpy of blob 0x104145 into
     * 0xB3768 + 0x12C (entry 10), then one strcpy of blob 0xBD1C3 (record 191) into 0xB3768 + 0x14A
     * (entry 11). Those three destinations are exactly aszTextF (10 x 30), szTextG and szTextH of
     * pufo_boot.h, so the twelve mode names ARE those three tables laid end to end -- and
     * TactHud_DrawUnitRoster is the ONLY reader of 0xB3768 in the whole image, which is what makes
     * the boot header's "craft/terrain names" guess for aszTextF wrong: they are fire-mode names. */
    const u8 *pNameTable;
    /* unk_C7051 and unk_C711E are 0xB38D0 + 205 * 389 and 205 * 390, so they are TEXTS[389]
     * "Тч: " (the turn-cost label) and TEXTS[390] "Вр.: " (the time-cost label) -- the two
     * prefixes 0x21A88/0x21B00 strcat the number onto. Not scratch, despite having no write xref. */
    const char *szNumberPrefixA; /* TEXTS[389] */
    const char *szNumberPrefixB; /* TEXTS[390] */
} TactHudRoster;

i32 TactHud_DrawUnitRoster(UiControl *pPads, const TactHudRoster *pRoster,
                           const GfxColorEnv *pColor,
                           UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser);

/* --- the marked-unit markers (screen 6) -------------------------------------------
 *
 * TactHud_DrawUnitMarkers @ 0x20c58. One overlay pad is blitted at (0,0) first, then every unit
 * carrying bSpecialMark gets a marker placed relative to the SELECTED unit's cell: 11 pixels per
 * cell of difference, centred on x = 120. XCOM rows start at 11 * (dy + 14) and enemy rows at
 * 11 * dy + 155, so the two bands are one pixel apart rather than symmetric. Large units use the
 * large mark with a further offset and small units the small mark. */
typedef struct TactHudMarkerEnv {
    PadImage *pOverlayPad;   /* Map.pMarkerPad, blitted at (0,0) */
    PadImage *pLargeMark;    /* unk_138EB8 */
    PadImage *pSmallMark;    /* unk_156A80 */
    const MapCell *pSelCell; /* pSelUnit->pCell */
} TactHudMarkerEnv;

i32 TactHud_DrawUnitMarkers(UiControl *pPad, const TactHudMarkerEnv *pEnv,
                            Soldier *const *apXcom, i32 nXcomCount,
                            Soldier *const *apEnemy, i32 nEnemyCount,
                            UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser);

/* TactHud_DrawTopBar @ 0x20ef8 draws 29 pads (bank widgets 60..88, unk_ACE50). */
#define PUFO_HUD_TOPBAR_PADS 29

/* TactHud_DrawTopBar @ 0x20ef8.
 *
 * Despite the name this is a fixed information layout, not a bar: four text records centred on the
 * 240-pixel screen at y 0, 50, 100 and 150, then six records left-aligned at x 40 and y 200..300 in
 * steps of 20, then 29 pad blits with decimal labels.
 *
 * The ten operands the decompiler shows as unk_B5270, unk_B533D, 0xE726B, 0xE7338, unk_E7C07 and
 * unk_C71EB..unk_C751F are all entries of aTextRecs @ 0xB38D0 at stride 205. The gaps between them
 * are exactly 205, and each address divided by 205 from the block base is a whole number:
 *
 *   unk_B5270  record 32    y 0    unk_B533D  record 33    y 50
 *   0xE726B    record 1031  y 100  0xE7338    record 1032  y 150
 *   unk_E7C07  record 1043  y 200  unk_C71EB..unk_C751F  records 391..395  y 220..300
 *
 * Only the four centred records are measured. The six at x 40 are drawn without Text_MeasureWidth,
 * so their x is the constant 40 and not a centred position.
 *
 * Labels are the decimal value of (index % 5 + 1), so they read 1 2 3 4 5 1 2 3 4 5. The first loop
 * labels only its first ten pads; the two later loops label pads 18..22 and 23..27 of the same
 * array without blitting anything. Pads 10..17 and 28 never get a label. */
i32 TactHud_DrawTopBar(UiControl *pPads, const u8 *pTextRecs, const GfxColorEnv *pColor,
                       UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser);

/* --- the selected-unit panel ---------------------------------------------------------
 *
 * TactHud_DrawSelectedUnitPanel @ 0x1d5ec.
 *
 * It starts by re-basing the panel and the 19 tactical HUD controls, before any gate:
 *
 *   nOriginY = 303 when the deploy panel is open, else 320 - pHudPad->uHeight
 *   pBank[i].y = aHudPanelBaseY[i] + nOriginY, for i in 0..18
 *
 * Those 19 are the named control table at off_A7ED8 @ 0xA7ED8, 32-byte UiControl records with
 * names in the binary: nextu, uplev, cuplev, map, invent, endturn, dnlev, cdnlev, sit, center,
 * alwaysrs, singles, aims, autos, mainmenu, pmenu, and three unnamed. Only the y field is rewritten;
 * the x values in the table are the controls' fixed columns.
 *
 * Then, gated on bPlayerInputEnabled, a selected unit, and Unit_bIsOperative:
 *
 *   the HUD pad itself is blitted, then each weapon hand's icon, then the 19 controls,
 *   then four status decimals, then the animation bar.
 *
 * Weapon icons: x = nOriginX + 19*(2 - nFootW)/2 + 9 on the left and + 198 on the right,
 * y = nOriginY + 17*(3 - nFootH)/2 + 41 - centring inside a 2x3 footprint of 19x17 cells. The glyph
 * index is pObj->uIconId + nBigObsBase - 1, so weapon icons come from the bigobs sprite group. In
 * the pseudocode that index reads as pObj->aItemQty[0x34]: index 52 of a 52-entry u16 array at
 * ObjectRec+0x3a is one past the end, and 0x3a + 104 = 0xA2, which is uIconId.
 *
 * The ammo number is shown when (bTwoHanded == 0 && nCategory < 3) || Unit_bIsLarge(unit), and only
 * when pObj->bAmmoCap != 255 (255 means the weapon never runs out). It is TactItem.nAmmo, drawn two
 * wide with leading zeros at nOriginX + 22 on the left and + 211 on the right.
 *
 * The four decimals are three wide with leading zeros: time units (forced to 0 when the tactical
 * state says the unit has spent them), stun/armor, hit points, morale - at (109,60), (109,75),
 * (160,60) and (160,75).
 *
 * The bar is 12 segments of 4x2 on a 6-pixel pitch at (nOriginX + 85, nOriginY + 13), 72 wide, and
 * its profile depends on the mode slot:
 *
 *   mode 0: key = 10 * v where v = counter below 16, else 15 up to 113, else 128 - counter
 *   mode 1: r = counter % 32, v = r above 15 ? 32 - r : r, key = 10 * v
 *   mode 2: r = counter % 24, v = r at least 12 ? 24 - r : r, then each segment k in 0..11 takes
 *     key = 120 - 10*abs(k - v), a peak that walks across the bar
 *   any other mode: the bar is skipped entirely
 *
 * When the counter reaches its end the next mode is chosen at RANDOM: the counter resets to 0 and the
 * mode becomes Game_RandU16(3). The end is 120 for mode 2 and 128 for modes 0 and 1. No test may
 * assert a specific mode following an animation.
 *
 * Deviation: the original returns whatever value it last happened to load - register residue, not a
 * status. The port returns whether it drew the panel.
 *
 * pEnv is not const because the rollover writes the counter and mode slots back. */
#define PUFO_HUD_PANEL_CONTROLS 19

/* off_A7ED8 @ 0xA7ED8, the y field of each of the 19 named HUD controls. */
extern const i16 aHudPanelBaseY[PUFO_HUD_PANEL_CONTROLS];

typedef struct TactHudPanelEnv {
    PadImage          *pHudPad;              /* Map.pHudPad (Map+0xFC) */
    PadImage *const   *apDigitPads;          /* the 11-entry table the decimal helper reads */
    Soldier           *pSelUnit;             /* Map.pSelUnit */
    u8                 bDeployPanelOpen;     /* Map+0x15110 */
    u8                 bPlayerInputEnabled;  /* Map+0x38 */
    u8                 bTuSuppressed;        /* pMapTactical (0x157198)->bAiActionActive, TactState+0x111E2 */
    u16                nBigObsBase;          /* fontSys + 0x26, the bigobs group */
    i32                nAnimCounter;         /* Map.apUiSlotsA[25] */
    i32                nAnimMode;            /* Map.apUiSlotsA[26] */
} TactHudPanelEnv;

bool TactHud_DrawSelectedUnitPanel(UiControl *pBank, TactHudPanelEnv *pEnv,
                                   const GfxColorEnv *pColor,
                                   HudPadBlitFn pfnDecimalBlit, void *pDecimalUser,
                                   UiControlBlitFn pfnCtlBlit, UiControlBlitFn pfnCtlBlitEx,
                                   void *pCtlUser);

/* TactHud_DrawItemStrip @ 0x2cdac: the item art of the tactical inventory screen.
 *
 * Six regions, each with its own fixed anchor, all drawn from the bigobs sprite group with the index
 * pObj->uIconId + nBigObsBase - 1. The footprint centring is the same 19 by 17 recipe the selected-unit
 * panel uses.
 *
 *   strip, 4 columns   x = 38*col + HandSlot + 19*(2 - nFootW)/2 + 24,  y = EquipDelta + 17*(3 - nFootH)/2 + 265
 *   strip count        x = 38*col - Text_MeasureWidth + 52 (right aligned), y = 301
 *   right hand         x = HandSlot + 19*(2 - nFootW)/2 + 124,           y = EquipDelta + 17*(3 - nFootH)/2 + 63
 *   left hand          x = HandSlot + 19*(2 - nFootW)/2 + 5,             y = the same 63 row
 *   backpack 2 x 4     x = 19*col + HandSlot + 162,                      y = 19*row + EquipDelta + 163
 *   belt 3 x 3         x = 19*col + HandSlot + 172,                      y = 19*row + EquipDelta + 25
 *   pockets 2 and 3    x = 19*col + HandSlot + 23 and + 105,             y = EquipDelta + 163
 *   pockets 0 and 1    x = 19*col + HandSlot + 24 and + 105,             y = EquipDelta + 25
 *
 * An empty slot is pItem == NULL or pItem == pEmptySlot. 0x138EA0 is a sentinel TactItem pointer that
 * means "empty" alongside null, so one null test is not enough.
 *
 * The strip reads the ground-item list the way the hit test does: index col + nInvFilterType, and the
 * column stops as soon as that index reaches nInvListCount. The count under each icon comes from the
 * parallel anInvCount array at the same index.
 *
 * The empty-hand marker is drawn when exactly one hand holds an item and that item has bListPri == 1, at
 * (126, 64) for an empty right hand and (7, 64) for an empty left hand.
 *
 * Deviation: the original uses two .bss globals, unk_10DBA8 and unk_10DBAC, as loop counters shared by
 * three separate loops, which makes it non-reentrant. The port uses locals. The original's return value
 * is loop residue (always 2) and no caller reads it, so the port returns void.
 */
typedef struct TactHudItemStripEnv {
    TactUiState *pUi;           /* nInvFilterType, nInvListCount, apInvList, anInvCount */
    Soldier     *pSelUnit;      /* Map.pSelUnit */
    PadImage    *pEmptyHandPad; /* dword_156A70, the "this hand is empty" marker */
    TactItem    *pEmptySlot;    /* unk_138EA0, the sentinel that means empty alongside NULL */
    u16          nBigObsBase;   /* fontSys + 0x26, the bigobs group */
} TactHudItemStripEnv;

void TactHud_DrawItemStrip(TactHudItemStripEnv *pEnv, const GfxColorEnv *pColor);

/* TactHud_DrawInventoryScreen @ 0x1f58c: the whole tactical inventory screen.
 *
 * Order matters. The backdrop pad, then the 13 panel controls at pBank + 0x260, then the hit highlight,
 * then the burden bar, then the unit header, then the item strip, then the unit portrait, then the hit
 * item's name and the drag ghost.
 *
 * The hit highlight is a nine-case switch on nInvHitRegion, drawn only when the panel is open AND
 * anActionPhaseSave0[108] is set. Every case uses colour key 0x78:
 *
 *   0  ground column:  x 38*sub0 + 22,            y 263,             37 x 52
 *   1  pocket 0 col:   x 19*sub0 + 23,            y 23,              19*nFootW - 1 x 19
 *   2  pocket 1 col:   x 19*sub0 + 104,           y 23,              19*nFootW - 1 x 19
 *   3  pocket 2 col:   x 19*sub0 + 23,            y 161,             19*nFootW - 1 x 19
 *   4  pocket 3 col:   x 19*sub0 + 104,           y 161,             19*nFootW - 1 x 19
 *   5  left hand:      x 4,                       y 62,              37 x 50
 *   6  right hand:     x 123,                     y 62,              37 x 50
 *   7  backpack:       x 19*sub0 + 161,           y 19*sub1 + 161,   19*nFootW - 1 x 19*nFootH - 1
 *   8  belt:           x 19*(sub0 + 9),           y 19*sub1 + 23,    19*nFootW - 1 x 19*nFootH - 1
 *
 * Any other region draws nothing. Regions 1 to 4 and 7 to 8 read the hit item's object with no test,
 * exactly as the original does.
 *
 * The burden bar colour comes from 4 * uInvBurden / auStat[8]: zero is green, one is yellow, two or more
 * is red. The number shown is uInvBurden - 1.
 *
 * The unit portrait pad table is at Map+0x199C, indexed 8*nUnitType + 4*bFemale + nLook.
 *
 * The hit item line appends " (" + nAmmo + ")" only when bTwoHanded is 0 or 1, nCategory is neither 3
 * nor 5, and bAmmoCap is not 255.
 *
 * The drag ghost is drawn only when anActionPhaseSave0[108] is CLEAR - the opposite of the highlight
 * gate - at the drag offset plus the cursor pixel pair.
 *
 * Deviation: the original builds both text lines in scratch at Map+0xAC and returns whatever the last
 * call left in R0. The port uses local buffers and returns void. */
/* The two records the burden line is built from: 55 is the label, 68 the suffix. In the shipped
 * language file 55 is "Вес: " and 68 is EMPTY, so the line reads "Вес: N" with no suffix. */
#define PUFO_TEXT_REC_BURDEN_LABEL  55
#define PUFO_TEXT_REC_BURDEN_SUFFIX 68

#define PUFO_HUD_INV_CONTROLS 13

typedef struct TactHudInventoryEnv {
    PadImage    *pInvPad;          /* dword_113AB8, Map+0x100: the inventory backdrop */
    UiControl   *aControls;        /* pBank + 0x260, 13 controls of stride 32 */
    Soldier     *pSelUnit;         /* Map.pSelUnit */
    InvHitState *pHit;             /* region, sub0, sub1, hit item, drag offsets */
    PadImage   **apPortraitPad;    /* Map+0x199C, indexed 8*nUnitType + 4*bFemale + nLook */
    const char  *pszBurdenLabel;   /* text record 55 */
    const char  *pszBurdenSuffix;  /* text record 68 */
    const char  *panRankLow;       /* rank table for Unit_FormatDisplayName, 0xB2200 */
    const char  *panRankOther;     /* rank table for Unit_FormatDisplayName, 0xB2148 */
    i32          nCursorX;         /* 0xAD714, the drag ghost position */
    i32          nCursorY;         /* 0xAD718 */
    u8           bItemHeld;        /* byte_1139F4 @ Map+0x3C: an inventory item is held */
    u8           bForceNoDrag;     /* Map_bForceNoDrag @ Map+0x150E4 = anActionPhaseSave0[108] */
    u16          nBigObsBase;      /* fontSys + 0x26, the bigobs group */
    TactHudItemStripEnv Strip;     /* the item strip, drawn with the same context */
} TactHudInventoryEnv;

void TactHud_DrawInventoryScreen(TactHudInventoryEnv *pEnv, const GfxColorEnv *pColor,
                                 UiControlBlitFn pfnPadBlit, UiControlBlitFn pfnPadBlitEx,
                                 void *pPadUser);

#endif /* PUFO_TACTHUD_H */
