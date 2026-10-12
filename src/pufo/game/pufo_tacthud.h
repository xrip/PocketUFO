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
#include "render/pufo_render.h"
#include "game/pufo_unit.h"

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
#define PUFO_SPECIAL_SORT_MARK    20 /* the mark-nearby branch  */
#define PUFO_SPECIAL_SORT_MEDIKIT 21 /* the medikit branch      */
#define PUFO_SPECIAL_SORT_LATCH   23 /* the latch-4 branch      */
#define PUFO_SPECIAL_MARK_RADIUS  10.0

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

#endif /* PUFO_TACTHUD_H */
