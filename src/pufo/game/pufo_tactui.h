/* pufo_tactui.h - tactical action-mode, UI-screen and option-pad transitions.
 *
 * Evidence: Tact_OpenCommandScreen @ 0x16ffc, MapView_LayerUp @ 0x16530,
 *           MapView_LayerDown @ 0x164f0, Inv_SetFilterAll @ 0x17410 and the six
 *           Inv_SetFilterN @ 0x17490/0x17510/0x17690/0x17610/0x17590/0x17710,
 *           Inv_ScrollUp @ 0x173e4, Inv_ScrollDown @ 0x173a8,
 *           UiBank_SelectRadio0..3 @ 0x16e04/0x16e40/0x16e7c/0x16eb8,
 *           Tact_SetFireModeAimed/Snap/Auto @ 0x17790/0x177fc/0x17868,
 *           Map_PlaySample11_SetMode0B @ 0x179c0,
 *           Map_UiScreen7_PlaySample0E @ 0x164b8,
 *           Map_UiScreen0F_PlaySample0F @ 0x164d4,
 *           Tact_TryFireMode4/10/5 @ 0x179e4/0x17a80/0x17b1c,
 *           Ui_FillActionCosts @ 0x26104.
 *
 * ## Field offsets (resolved against the `Map` type)
 *
 *     nActionMode       @ 0x004C
 *     nUiScreen         @ 0x003A (u16)
 *     nUnk40            @ 0x0028
 *     gap_2[1]          @ 0x0002  -- the action latch, shared with the roster
 *     nLayerScratch     @ 0x15138 (cursor flags)
 *     nInvFilterType    @ 0x1A22 (u8)
 *     apInvList         @ 0x1A24 (TactItem*[2048])
 *     anInvCount        @ 0x3A24 (u16[2048])
 *     nInvListCount     @ 0x4A24 (u16)
 *     nInvListSel       @ 0x4A26 (u8)
 *     nTrajFront0       @ 0x4A3C (dword array)
 *
 * ## Action-cost table
 *
 * `Ui_FillActionCosts` writes, for each action mode m in 0..11:
 *
 *     anActionCost[m]      = cost      (the TU price)
 *     anActionEnabled[m]   = enabled   (whether the mode is offered)
 *     pBank[32 + m].bEnabled = enabled != 0
 *
 * Hex-Rays renders the first two as `nTrajFront0[4*m + 1040]` and
 * `nTrajFront0[4*m + 1088]`. The ARM is flat and settles both: `0x263F0` stores at dword
 * index `m + 0x1393` and `0x26400` at `m + 0x139F`, which are the byte addresses
 * `Map+0x4E4C + 4*m` and `Map+0x4E7C + 4*m` (4*0x1393 = 0x4E4C, 4*0x139F = 0x4E7C).
 * `Tact_OpenLeftHandMenu @ 0x170D0` re-reads them with the literal immediates
 * `MOV R3,#0x4E7C` for enabled and `MOV LR,#0x4E00 / ORR LR,LR,#0x4C` for the cost,
 * which is the direct proof of both bases.
 *
 * `Map+0x4EBC` is a DIFFERENT array: the combat hit windows used by
 * `Combat_ResolveShot` (`MOV R0,#0x4E00 / ORR R0,R0,#0xBC`), indexed
 * `4*(41*row + col) + slot`. The two share the 0x4E00 page and were conflated in an
 * earlier note; the enabled table is 0x4E7C.
 *
 * `Input_OnPointerUp @ 0x14FE4` reads the cost the same way (`Map + 0x4E4C + 4*mode`), and
 * `Tact_TryFireMode4/10` read it for m = 4, 5 and 10, so the table and its readers are
 * one contract and are ported together. The enabled mirror at `0x2641C` is
 * `pBank + 0x400 + 32*m + 9`, i.e. `UiControl.bEnabled` of the twelve action-pad
 * widgets 32..43.
 */
#ifndef PUFO_TACTUI_H
#define PUFO_TACTUI_H

#include "game/pufo_options.h"
#include "game/pufo_tactctl.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

/* The invulnerable "action latch" byte gap_2[1]. CycleNext clears it; the fire
 * modes set it to 4 on success and 0 on failure. */
#define PUFO_ACTION_LATCH_NONE 0
#define PUFO_ACTION_LATCH_FIRE 4

/* Action modes that mean "cannot act". The fire-mode failures switch to 12. */
#define PUFO_ACTION_MODE_NONE 12
#define PUFO_ACTION_MODE_AUTO 0
#define PUFO_ACTION_MODE_SNAP 1
#define PUFO_ACTION_MODE_AIMED 2
#define PUFO_ACTION_MODE_MENU 11
#define PUFO_ACTION_MODE_HAND 12

/* The cursor layer bounds. LayerUp refuses at 4; LayerDown refuses at 1, so the
 * reachable set is 1..4 even though a layer 0 exists. */
#define PUFO_LAYER_MAX 4
#define PUFO_LAYER_MIN 1

/* Number of inventory filter pads and their names, in ADDRESS order. The
 * selection index does not match this order -- see Ui_SetInvFilter. */
#define PUFO_INV_FILTER_PADS 5

/* Action modes the cost table covers. */
#define PUFO_ACTION_MODE_COUNT 12

/* Capacity of the ground-item list. The Map block gives the pointer array 0x2000
 * bytes (0x1A24..0x3A23) and the count array 0x1000 bytes (0x3A24..0x4A23), so
 * both hold 2048 entries and the two capacities are equal by construction. */
#define PUFO_INV_LIST_MAX 2048

typedef struct TactUiState {
    i32 nActionMode;            /* 0x4C */
    u16 nUiScreen;              /* 0x3A */
    i32 nUnk40;                 /* 0x28 */
    u8  bActionLatch;           /* gap_2[1] @ 0x0002 */
    u8  bUnk1508C;              /* gap15088+4 @ 0x1508C, cleared by OpenCommandScreen */
    u8  nLayerScratch;          /* 0x15138 */
    /* The fire-target triple the confirm functions set (nFireTgtX/Y/Z). */
    i32 nFireTgtX;
    i32 nFireTgtY;
    i32 nFireTgtZ;
    u8  nInvFilterType;         /* 0x1A22 */
    u16 nInvListCount;          /* 0x4A24 */
    u8  nInvListSel;            /* 0x4A26 */
    /* The ground-item list Inv_BuildVisibleList builds: one entry per distinct
     * ObjectRec in the selected unit's cell, with the number of TactItems sharing
     * that ObjectRec in anInvCount. */
    TactItem *apInvList[PUFO_INV_LIST_MAX];   /* 0x1A24 */
    u16       anInvCount[PUFO_INV_LIST_MAX];  /* 0x3A24 */
    /* The filter pads, in address order 0xAC9FA/0xACA1A/0xACA3A/0xACA5A/0xACA7A. */
    u8  abFilterPad[PUFO_INV_FILTER_PADS];
    u8  bFilterPadX;            /* 0xAC97A */
    u8  bFilterPadAll;          /* 0xAC9DA */
    /* The radio bank's four one-hot bytes at 0xAC81A/0xAC83A/0xAC85A/0xAC87A. */
    u8  abRadio[4];
    /* The action-cost table, filled by Ui_FillActionCosts. */
    i32 anActionCost[PUFO_ACTION_MODE_COUNT];
    i32 anActionEnabled[PUFO_ACTION_MODE_COUNT];

    /* --- option bytes and their pad indicators ---
     * The five toggle functions address bytes 93..97 of `anActionPhaseSave0`,
     * which resolve to 0x150D5..0x150D9 -- exactly the option bytes the
     * `PufoOptions` header documents (scrollbypen, showborder, runbydblclick,
     * turnbypen, menubypen). They are the SAME bytes, reached in the original
     * through a raw index rather than by name. */
    u8  bScrollByPen;    /* 93 @ 0x150D5 */
    u8  bShowBorder;     /* 94 @ 0x150D6 */
    u8  bRunByDblClick;  /* 95 @ 0x150D7 */
    u8  bTurnByPen;      /* 96 @ 0x150D8 */
    u8  bMenuByPen;      /* 97 @ 0x150D9 */
    /* The five pad indicator bytes, at 0xACF9A/0xACFBA/0xACFDA/0xACFFA/0xAD01A
     * in the order scroll, show, run, turn, menu. */
    u8  abTogglePad[5];
    /* gap_86272[16] @ 0x15110 = bDeployPanelOpen. */
    u8  bDeployPanelOpen;
    /* LOBYTE(apUiSlotsA[8]) @ 0x00080, set by OpenCommandScreenFlagged. */
    u8  bSlot80Low;
    /* unk_138E68 @ 0x254B0. Render_Frame (0x22310) skips MapView_DrawIsoLayer
     * when this byte is 0 (ARM 0x22404 and 0x225c4), and Tact_InitMission @
     * 0x3c550 clears it at mission start. Inv_OpenScreen sets it to 1. */
    u8  bDrawIsoLayer;
} TactUiState;

/* The world pieces the UI transitions touch, plus the two effects they have. */
typedef struct TactUiHooks {
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    /* Map.pSelUnit @ 0x15334. The filter handlers rebuild the ground list of the
     * selected unit, so the world pointer they read is part of the contract. A
     * NULL value skips the rebuild (documented deviation: the original faults). */
    Soldier *pSelUnit;
    /* StatusMsg_Push @ 0x2dda4. The messages are runtime text records that the
     * port has not wired up, so the index is passed through instead of a string. */
    void (*pfnStatusMsg)(i32 nTextRecordIndex, void *pUser);
    /* The cursor flag writer (MapCursor_SetFlags @ 0x26f78). */
    void (*pfnSetCursorFlags)(u8 nFlags, void *pUser);
    void *pUser;
} TactUiHooks;

/* One filter-selection index: 0..5, or PUFO_INV_FILTER_ALL. */
#define PUFO_INV_FILTER_ALL (-1)

/* Inv_SetFilterAll @ 0x17410 and Inv_SetFilter0..5 @ 0x17490/0x17610/0x17690/
 * 0x17510/0x17590/0x17710.
 *
 * Every one of the seven: plays sample 17, clears all seven filter pads, sets
 * exactly one pad, zeroes nInvFilterType, sets nInvListSel (except
 * PUFO_INV_FILTER_ALL, which sets -1), then rebuilds the visible list.
 *
 * The selection index does NOT match the pad's address order. Measured from the
 * seven functions:
 *
 *     All -> bFilterPadAll         sel = -1
 *       0 -> abFilterPad[0]        sel = 0
 *       1 -> abFilterPad[3]        sel = 1
 *       2 -> abFilterPad[4]        sel = 2
 *       3 -> abFilterPad[1]        sel = 3
 *       4 -> abFilterPad[2]        sel = 4
 *       5 -> bFilterPadX           sel = 5
 *
 * That mapping is reproduced exactly; it is an original quirk, not a mistake in
 * this port.
 *
 * Returns false when nFilter is not one of the seven valid values. */
bool Ui_SetInvFilter(TactUiState *pUi, i32 nFilter, const TactUiHooks *pHooks);

/* Inv_ScrollUp @ 0x173e4: decrement nInvFilterType unless it is already 0. No
 * sample, no list rebuild, no filter-pad change. */
void Ui_InvScrollUp(TactUiState *pUi);

/* Inv_ScrollDown @ 0x173a8: increment nInvFilterType only while
 * `(u32)nInvFilterType + 4 < nInvListCount`. The comparison is UNSIGNED and the
 * left side is the u8 widened, reproduced as written. */
void Ui_InvScrollDown(TactUiState *pUi);

/* UiBank_SelectRadio0..3 @ 0x16e04/0x16e40/0x16e7c/0x16eb8. Plays sample 17 and
 * makes the chosen radio byte one-hot. Returns false for nRadio outside 0..3. */
bool Ui_SelectRadio(TactUiState *pUi, i32 nRadio, const TactUiHooks *pHooks);

/* Declared before the menu context that names it. */
typedef struct ActionCostInput ActionCostInput;

/* Tact_OpenLeftHandMenu @ 0x17030 and Tact_OpenRightHandMenu @ 0x17148. The two are
 * the same 280-byte body with a different hand slot read, so the port has one
 * function; the caller passes the hand it picked.
 *
 *   sample 17; bail when the action latch (Map+0x02) is not 0
 *   pSelWeapon = pUnit->pHandL (or pHandR); bail when it is NULL
 *   nSortKey = pSelWeapon->pObj->uSortKey
 *   bail when the weapon's research record says this weapon has no action menu:
 *   aFacTypeTable[13 + 34 * byte_AA968[nSortKey]] == 0 (ARM 0x17078..0x17094).
 *   Injected as pfnWeaponHasMenu because aFacTypeTable @ 0x15EF08 is built at
 *   runtime (the shipped image is all 0xFF) and its in-memory field order is not
 *   yet recovered -- see the open item in docs/parity/game-core.md.
 *   bail when nSortKey == 24 and the unit's byte at Soldier+0x1A6 is 0
 *   Ui_FillActionCosts, then scan modes 0..11 for the first one that is both
 *   enabled and affordable (cost <= uTu). None found -> push aTextRecs record 1
 *   ("not enough time") and leave the screen closed. Found -> nActionMode = 12 and
 *   nUiScreen = 3.
 *
 * The scan is a search for ANY affordable action, not for a specific one: the mode it
 * finds is discarded and only used as a yes/no. Returns true when the menu opened.
 *
 * Deviation: the original reads the enabled and cost slots without bounds-checking
 * the loop counter; the port walks the twelve modes it models, which is the same
 * set (the ARM stops at R1 == 0x30, i.e. twelve four-byte steps). */
typedef struct HandMenuCtx {
    const ActionCostInput *pCost; /* what Ui_FillActionCosts needs for this weapon */
    u8   bActionLatch;            /* Map+0x02: any non-zero value blocks the menu */
    u16  uTu;                     /* Soldier+0x06 */
    u16  uSortKey;                /* pObj->uSortKey @ 0xC8 */
    u8   bByte1A6;                /* Soldier+0x1A6, the sort-key-24 gate */
} HandMenuCtx;

bool Ui_OpenHandMenu(TactUiState *pUi, const HandMenuCtx *pCtx,
                     bool (*pfnWeaponHasMenu)(u16 uSortKey, void *pUser),
                     const TactUiHooks *pHooks, void *pGateUser);

/* Tact_OpenCommandScreen @ 0x16ffc: sample 16, clear bUnk1508C (the byte at
 * 0x1508C inside the gap15088 region), and set nUiScreen 4. */
void Ui_OpenCommandScreen(TactUiState *pUi, const TactUiHooks *pHooks);

/* Map_UiScreen7_PlaySample0E @ 0x164b8: nUiScreen = 7 then sample 14. */
void Ui_OpenScreen7(TactUiState *pUi, const TactUiHooks *pHooks);

/* Map_UiScreen0F_PlaySample0F @ 0x164d4: nUiScreen = 15 then sample 15. */
void Ui_OpenScreen15(TactUiState *pUi, const TactUiHooks *pHooks);

/* MapView_LayerUp @ 0x16530 / MapView_LayerDown @ 0x164f0. Both play sample 17
 * and then move the cursor flags by one, respecting the 1..4 bounds. */
void Ui_LayerUp(TactUiState *pUi, const TactUiHooks *pHooks);
void Ui_LayerDown(TactUiState *pUi, const TactUiHooks *pHooks);

/* The two weapon scalars the fire-mode test reads. Passed as plain values because
 * `TactItem.pObj` is the minimal `ObjectRec` in pufo_mapcell.h, which does not
 * carry the ammo-capacity byte; the full record is `ObjectRecFull` in
 * pufo_boot.h. Taking scalars keeps the two type views from colliding. */
typedef struct FireModeWeapon {
    u8 nAmmo;      /* TactItem.nAmmo @ 0x06 */
    u8 bAmmoCap;   /* pObj->bAmmoCap @ 0xAE; 255 means unlimited */
} FireModeWeapon;

/* TactItem_bTryReloadClip @ 0x4b6a4. When the weapon is empty, its object is
 * not unlimited (bAmmoCap != 255) and the owner carries a spare clip
 * (bHumanoidInv != 0), it consumes one clip and refills the weapon from the
 * clip object's own bAmmoCap, looked up by the weapon object's first ammo-type
 * id. Returns true when the weapon is loaded or unlimited afterwards.
 *
 * pWeapon->nAmmo is the live magazine; bWeaponAmmoCap is pObj->bAmmoCap and
 * nAmmoTypeId pObj->anAmmoTypeId[0], passed separately because TactItem.pObj is
 * the minimal ObjectRec with neither field. pfnClipAmmoCap stands in for
 * pCtxBase.apObjects[id]->bAmmoCap. */
bool TactItem_TryReloadClip(TactItem *pItem, u8 bWeaponAmmoCap, u16 nAmmoTypeId,
                            Soldier *pOwner,
                            u8 (*pfnClipAmmoCap)(u16 nAmmoTypeId, void *pUser),
                            void *pUser);

/* Tact_SetFireModeAimed/Snap/Auto @ 0x17790/0x177fc/0x17868.
 *
 * Each plays sample 17, then refuses when the selected weapon is empty AND its
 * object's ammo capacity byte is not 255 (255 means "unlimited"). On refusal it
 * pushes the shared "out of ammo" text record and changes nothing. On success it
 * sets nActionMode to 2/1/0 and writes 1/1/3 into nUnk40. */
bool Ui_SetFireMode(TactUiState *pUi, i32 nMode, const FireModeWeapon *pWeapon,
                    const TactUiHooks *pHooks);

/* The text-record index the out-of-ammo refusal pushes (byte_B3D9E). */
#define PUFO_TXT_OUT_OF_AMMO 0

/* Map_PlaySample11_SetMode0B @ 0x179c0: sample 17, nActionMode = 11. */
void Ui_SetMode11(TactUiState *pUi, const TactUiHooks *pHooks);

/* Tact_TryFireMode4/10/5 @ 0x179e4/0x17a80/0x17b1c.
 *
 * Plays sample 17, sets nActionMode to the mode, then compares the selected
 * unit's `uTu` against the mode's TU cost from the filled cost table:
 *
 *     enough TU : latch = 4, nUnk40 = 0, nUiScreen = 0
 *     too little: nUiScreen = 0, nActionMode = 12, latch = 0, nUnk40 = 0, then the
 *                 out-of-TU status message
 *
 * Returns true when the action was accepted. */
bool Ui_TryFireMode(TactUiState *pUi, i32 nMode, u16 uTu, const TactUiHooks *pHooks);

/* The object fields Ui_FillActionCosts reads, by the offsets it uses. */
typedef struct ActionCostInput {
    u16 uSortKey;      /* pObj->uSortKey  @ 0xC8 */
    u8  nCategory;     /* pObj->nCategory @ 0xB8 */
    /* pObj->gap_0[176 + m] for m = 0..2, i.e. the bytes at 0xB0..0xB2. */
    u8  abCostPctPacked[3];  /* gap_AF[0..2] @ 0xB0 */
    /* pObj->gap_0[179 + m] for m = 0..2, i.e. 0xB3..0xB5 = abArmorTu. */
    u8  abFlagByMode[3];     /* abArmorTu[0..2] @ 0xB3 */
    u16 auStat0;       /* pSelUnit->auStat[0] */
    u16 auStat4;       /* pSelUnit->auStat[4] */
    u16 auStat5;       /* pSelUnit->auStat[5] */
    u8  bFlags;        /* pSelUnit->bFlags */
    u8  nUnitType;     /* pSelUnit->nUnitType */
    bool bOperative;   /* Unit_bIsOperative(pSelUnit) */
    u8  bWeaponUnk8;   /* pSelWeapon->bUnk8 @ 0x08 */
} ActionCostInput;

/* Ui_FillActionCosts @ 0x26104.
 *
 * Fills all twelve modes. The arithmetic for modes 0..2 is the original's
 * soft-double sequence, which resolves to a multiply by the constant 0.01:
 * the two literals are (1202590843, 1065646817) = the IEEE754 double
 * 0x3F847AE147AE147B = 0.01. Cost is `auStat[4] * pct * 0.01`, raised by a
 * quarter when bFlags bit 0 is set (kneeling raises the price).
 *
 * The other modes are straight table look-ups, transcribed case by case. */
void Ui_FillActionCosts(const ActionCostInput *pIn, TactUiState *pUi);

/* --- action confirmations ---------------------------------------------------
 *
 * The confirm functions read the action-cost table, so they only make sense after
 * Ui_FillActionCosts. Each returns the resulting nUiScreen value (or -1 when the
 * arguments are unusable).
 */

/* TactAction_ConfirmUseItem @ 0x178d4. Action mode 3.
 *
 * Plays sample 17, sets nActionMode 3, and reads the mode-3 cost. With enough TU it
 * clears the latch and nUnk40, then branches on the weapon's sort key:
 *
 *   sort key 14 -> nUiScreen 0, mark the weapon's bUnk8, DEDUCT the TU cost
 *   otherwise   -> nUiScreen 5 (open the item sub-screen, no TU spent yet)
 *
 * With too little TU it pushes the out-of-TU message, clears the screen, the latch
 * and nUnk40, and sets nActionMode 12. Returns the new nUiScreen.
 *
 * The cost read is the mode-3 entry of the same table Ui_FillActionCosts fills:
 * the original's `nTrajFront0[1052]` is `4*3 + 1040`. */
i32 Ui_ConfirmUseItem(TactUiState *pUi, Soldier *pUnit, TactItem *pWeapon,
                      u16 uSortKey, const TactUiHooks *pHooks);

/* TactAction_ConfirmFireBurst @ 0x18774.
 *
 * Plays sample 16, clears the latch, publishes the multi-target count into nUnk40,
 * clears bit 0x40 on every multi-target cell, zeroes the count, then enqueues the
 * fire burst aimed at the FIRST multi-target cell's coordinates and kicks the queue.
 *
 * The original dereferences `apMultiTargets[0]` unconditionally, so a zero count is
 * a NULL read there; the port refuses instead (documented deviation).
 *
 * `pnMultiTargetCount` is a pointer because the original zeroes the count as part of
 * the same sequence, after using it for nUnk40 and the mark-clearing loop. */
bool Ui_ConfirmFireBurst(TactUiState *pUi, TactState *pQueue, Soldier *pUnit,
                         TactItem *pWeapon, MapCell **apMultiTargets,
                         i32 *pnMultiTargetCount, u16 uAmmoInWeapon, u8 bAmmoCap,
                         void (*pfnFaceToward)(Soldier *pUnit, i32 nX, i32 nY,
                                               void *pUser),
                         void *pUser, const TactUiHooks *pHooks);

/* TactAction_ConfirmSpecial9 @ 0x18260. Action mode 9.
 *
 * Sample 17, mode 9, screen 0. Then the "nothing to do" guard: if the unit is not
 * kneel/primed AND is at full HP (`uHp == auStat[1]`) AND at full morale (100) AND
 * has spent TU, two status messages are pushed and no action is queued. Otherwise
 * the target is the unit's OWN cell and the action is enqueued at the mode-9 cost.
 *
 * Returns true when an action was queued. */
bool Ui_ConfirmSpecial9(TactUiState *pUi, Soldier *pUnit, TactItem *pWeapon,
                        MapCell *pCell, void *pEnqueueAction, void *pEnqueueUser,
                        const TactUiHooks *pHooks);

/* Tact_BeginMultiTarget @ 0x18380. Action mode 7.
 *
 * Sample 17, mode 7, then the same empty-weapon guard the fire modes use: refuse
 * with the out-of-ammo message when the magazine is empty and the capacity is not
 * 255. On success it parks the cursor at its current cell, zeroes the multi-target
 * count, and sets the action latch to **3** (not 4 -- 3 marks "picking targets").
 *
 * Returns the new latch value (3), or -1 when refused. */
i32 Ui_BeginMultiTarget(TactUiState *pUi, const FireModeWeapon *pWeapon,
                        i32 *pnMultiTargetCount, i32 nCursorX, i32 nCursorY,
                        void (*pfnSetCursor)(i32 nX, i32 nY, void *pUser),
                        void *pUser, const TactUiHooks *pHooks);

/* The latch value that means "multi-target picking in progress". */
#define PUFO_ACTION_LATCH_TARGETING 3

/* Tact_MapTypeToGlyphIdx @ 0x46664.
 *
 * Maps a unit type to its glyph index, with a four-entry probe table for the
 * mid-range types. The rules, in the original's order:
 *
 *     type <= 3   -> type + 5
 *     type <  9   -> 18
 *     type == 16  -> 14
 *     type == 17  -> 13
 *     else        -> scan the four bytes at gap_86272[22..25] for the type;
 *                    a match at index i gives i + 9, no match gives type + 5
 *
 * Note the early returns mean types 16 and 17 are handled BEFORE the table probe
 * even though they are above 9, and that a type which matches the table gets
 * `index + 9` -- an index-relative result, not a type-relative one. */
i32 Tact_MapTypeToGlyphIdx(u8 nType, const u8 *pProbeTable, i32 nProbeCount);

/* Offset of the glyph probe table within the context: gap_86272[22] = 0x15116. */
#define PUFO_GLYPH_PROBE_COUNT 4

/* --- combat-menu item queries ------------------------------------------------
 *
 * Two pure look-ups over the action-id space. Both were decompiled branch by
 * branch and are transcribed exactly, because the id sets are irregular and every
 * gap value has a distinct meaning.
 */

typedef enum HandSlot {
    HAND_SLOT_RIGHT   = 0,   /* action 16 */
    HAND_SLOT_LEFT    = 1,   /* actions 14, 18, 39 */
    HAND_SLOT_BOTH    = 2,   /* action 19 */
    HAND_SLOT_SPECIAL = -2,  /* action 36 */
    HAND_SLOT_NONE    = -1   /* actions 15, 17, 37, 38, and anything unlisted */
} HandSlot;

/* TactItem_GetHandSlot @ 0x3e620.
 *
 *   14 -> 1 (left)      19 -> 2 (both)     36 -> -2 (special)
 *   16 -> 0 (right)     39 -> 1 (left)     15, 17, 37, 38 -> -1 (none)
 *   18 -> 1 (left)      anything else -> 0
 *
 * Note the two fall-through defaults are NOT the same value: the <= 19 range
 * defaults to 0 (right) while the >= 20 range defaults to 0 as well, but the
 * in-range explicit "none" ids are 15/17 and the >= 20 ones are 37/38. */
i32 TactItem_GetHandSlot(i32 nActionId);

/* TactItem_GetEquipDelta @ 0x3e69c. Returns the inventory slot delta the action
 * moves an item by:
 *
 *   14, 15, 16, 18 -> -1     17 -> -2     19 -> -2
 *   37 -> 1                  39 -> -1
 *   36, 38 -> -3             anything else -> 0 */
i32 TactItem_GetEquipDelta(i32 nActionId);

/* ---------------------------------------------------------- weapon sub-modes */

/* Map_SetSubMode0..8_Play11 @ 0x18418/0x18478/0x184d4/0x18534/0x18594/0x185f4/0x18654/0x186b4/0x18714.
 *
 * All nine have the same body with one differing constant:
 *
 *     Sound_Play(17)
 *     pSelWeapon->bUnk8 = 1
 *     pSelWeapon->bUnk7 = nSubMode          (0..8)
 *     pSelUnit->uTu -= anActionCost[nActionMode]
 *
 * The TU subtraction uses the CURRENT action mode's cost, so it is the caller's
 * job to have filled the cost table first (Ui_FillActionCosts) and set
 * nActionMode. The subtract is a plain u16 wrap-down, reproduced as written: the
 * original does not clamp at zero.
 *
 * Returns false when the weapon or unit is NULL, or nSubMode is outside 0..8
 * (documented deviation: the original would dereference NULL). */
bool Ui_SetSubMode(TactUiState *pUi, TactItem *pWeapon, Soldier *pUnit,
                   i32 nSubMode, const TactUiHooks *pHooks);

/* ------------------------------------------------------------- option toggles */

/* Which option a toggle function drives. The order matches the pad array and the
 * byte order 93..97. */
typedef enum TactToggleId {
    TACT_TOGGLE_SCROLL = 0,  /* Map_ToggleFlag128A8D @ 0x19250, byte 93 */
    TACT_TOGGLE_SHOW,        /* Map_ToggleFlag128A8E @ 0x192d4, byte 94 */
    TACT_TOGGLE_RUN,         /* Map_ToggleFlag128A8F @ 0x19348, byte 95 */
    TACT_TOGGLE_TURN,        /* Map_ToggleFlag128A90 @ 0x193a0, byte 96 */
    TACT_TOGGLE_MENU,        /* Map_ToggleFlag128A91 @ 0x1941c, byte 97 */
    TACT_TOGGLE_COUNT
} TactToggleId;

/* The five option toggles. They are NOT five copies of one function -- each has
 * its own coupling rule, all verified from the decompilation:
 *
 *   SCROLL : turning it OFF also clears SHOW (and SHOW's pad).
 *   SHOW   : turning it ON requires SCROLL to be on; otherwise it stays off.
 *            Unlike the others it does not flip -- it recomputes from SCROLL.
 *   RUN    : a plain toggle of its own byte.
 *   TURN   : turning it ON clears MENU (mutually exclusive).
 *   MENU   : turning it ON clears TURN (mutually exclusive).
 *
 * Each plays sample 17 first. Returns false for an id outside the five. */
bool Ui_ToggleOption(TactUiState *pUi, i32 nWhich, const TactUiHooks *pHooks);

/* ------------------------------------------------------- the ground item list */

/* Inv_BuildVisibleList @ 0x2d580. Rebuilds the visible ground-item list of the
 * selected unit's cell:
 *
 *   1. nInvListCount is cleared first, before anything is read.
 *   2. Walk MapCell.pItemList @ 0x34. Skip an item when its ObjectRec.bTwoHanded
 *      @ 0x28 is 2. Keep it when nInvListSel is 0xFF ("all") or when
 *      ObjectRec.nCategory @ 0xB8 equals nInvListSel.
 *   3. Merge by ObjectRec identity: if the list already holds an entry with the
 *      same ObjectRec*, bump that entry's u16 count and keep the TactItem with the
 *      LARGER nAmmo @ 0x06 (the comparison is `existing < new`, so a tie keeps the
 *      first one found).
 *   4. Otherwise append: anInvCount[n] = 1, apInvList[n] = item, n++.
 *   5. Selection-sort the list ASCENDING by ObjectRec.uSortKey @ 0xC8, swapping the
 *      count array in step with the pointer array. The outer pass runs while
 *      pos + 1 < count and the scan runs pos+1 .. count-1, so the last entry is
 *      only ever a candidate, never a scan target.
 *
 * Deviations: the original dereferences pSelUnit, pCell, pObj and every list entry
 * without a NULL test, and appends without a capacity check; the port refuses a
 * NULL unit/cell/pObj and stops appending at PUFO_INV_LIST_MAX. */
void Inv_BuildVisibleList(TactUiState *pUi, Soldier *pSelUnit);

/* ----------------------------------------------------------- screen helpers */

/* Map_ClearUiScreen @ 0x18a54: nUiScreen = 0. No sound. */
void Ui_ClearScreen(TactUiState *pUi);

/* Map_PlaySamples10_0E_ClearUiScreen @ 0x18e9c: sample 16, sample 14, screen 0. */
void Ui_Samples16_14_ClearScreen(TactUiState *pUi, const TactUiHooks *pHooks);

/* Map_UiScreen7_PlaySamples10_0E @ 0x194f8: screen 7, then samples 16 and 14. */
void Ui_Screen7_Samples16_14(TactUiState *pUi, const TactUiHooks *pHooks);

/* Map_PlaySamples16_7_SetDeployFlag @ 0x19528 and
 * Map_PlaySamples16_7_ClearDeployFlag @ 0x19568 (renamed in the IDB: the old name said
 * samples 10 and 7, the code at 0x19534 and 0x19540 plays 16 and 7): sample 16, sample 7,
 * set/clear bDeployPanelOpen (gap_86272[16] @ 0x15110), then screen 0. */
void Ui_Samples16_7_SetDeployFlag(TactUiState *pUi, const TactUiHooks *pHooks);
void Ui_Samples16_7_ClearDeployFlag(TactUiState *pUi, const TactUiHooks *pHooks);

/* Tact_OpenCommandScreenFlagged @ 0x198c4: sample 16, set LOBYTE(apUiSlotsA[8]),
 * screen 4. Differs from Ui_OpenCommandScreen only in setting that byte and in
 * NOT clearing bUnk1508C. */
void Ui_OpenCommandScreenFlagged(TactUiState *pUi, const TactUiHooks *pHooks);

/* Map_IncByte128AF0_Play11 @ 0x19670 and Map_DecByte128AF0_Play11 @ 0x196ac:
 * sample 17, then move the cursor layer by one with the same 1..4 bounds as
 * Ui_LayerUp/Ui_LayerDown, but WITHOUT calling MapCursor_SetFlags. */
void Ui_IncLayer(TactUiState *pUi, const TactUiHooks *pHooks);
void Ui_DecLayer(TactUiState *pUi, const TactUiHooks *pHooks);

#endif /* PUFO_TACTUI_H */
