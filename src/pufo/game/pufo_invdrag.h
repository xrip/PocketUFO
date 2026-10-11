/* pufo_invdrag.h - the inventory screen hit test and the drag slot state.
 *
 * Evidence: Ui_HitTestTactItem @ 0x11ac0, the hit-rect table aInvHitRects @
 * 0xA8DD8 (25 records of 14 bytes, read out of the executable).
 *
 * ## The Map state it writes
 *
 *     pInvHitItem    @ 0x0040 (unk_1139F8)  the item under the pointer
 *     nInvDragOffX   @ 0x4A28 (unk_1183E0)  -20 * pObj->nFootW @ 0xA6 / 2
 *     nInvDragOffY   @ 0x4A2C               -20 * pObj->nFootH @ 0xA7 / 2
 *     nInvHitRegion  @ 0x4A30               0..8, see InvHitRegion
 *     nInvHitSub0    @ 0x4A34 (unk_1183EC)  ground column, or the slot meta byte
 *     nInvHitSub1    @ 0x4A38               the second meta byte (pack, belt)
 *
 * ## Two hit sources, in this order
 *
 * 1. The ground list, a 4-column window at x 23..173, y 264..313. The column is
 *    `(nX - 23) / 38` through `_rt_sdiv` (ARM 0x11B1C), the list index is
 *    `nInvFilterType + column`, and the hit only lands when that index is below
 *    `nInvListCount`. Region 0, sub0 = the column.
 * 2. The 25-record table. The scan takes the FIRST record whose rect contains the
 *    point, so overlapping records resolve by table order.
 *
 * The two are not exclusive: the ground branch runs first and the table scan runs
 * afterwards. A table match whose slot is empty returns false and DISCARDS the
 * ground hit, because the local item pointer was already overwritten.
 *
 * ## The slot mirror sentinel
 *
 * A slot that holds the global sentinel `unk_138EA0` (Map+0x254E8) is not an item:
 * it mirrors another slot, named by the meta byte stored beside it. The hit is
 * re-read through that meta byte. The meta byte is reported in nInvHitSub0 either
 * way, which is how the drop path finds the slot back.
 *
 *     pocket  apItem[2] + abMeta[2]        mirror = apItem[abMeta[slot]]
 *     pack    apBackpack[8], meta at Soldier+0x15C (A) and +0x164 (B)
 *           index = slot + 4*col, mirror = apBackpack[A[idx] + 4*B[idx]]
 *     belt    apBelt[9], meta at Soldier+0x190 (A) and +0x199 (B)
 *           index = slot + 3*col, mirror = apBelt[A[idx] + 3*B[idx]]
 */
#ifndef PUFO_INVDRAG_H
#define PUFO_INVDRAG_H

#include "game/pufo_mapcell.h"
#include "game/pufo_tactui.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

/* The ground-list rectangle and its column width, from the ARM compares at
 * 0x11AF0..0x11B04 (0x17, 0xAD, 0x108, 0x139). */
#define PUFO_INV_GROUND_X0 23
#define PUFO_INV_GROUND_X1 173
#define PUFO_INV_GROUND_Y0 264
#define PUFO_INV_GROUND_Y1 313
#define PUFO_INV_GROUND_COL_W 38

/* Records in aInvHitRects @ 0xA8DD8 (the scan stops at byte offset 350). */
#define PUFO_INV_HITRECT_COUNT 25

/* The drag-offset scale: `MOV R8,#0xFFFFFFEC` (-20), then the product is halved
 * with `MOVS/ADDMI/ASR#1`, which for an even product is the same as /2. */
#define PUFO_INV_DRAG_SCALE (-20)

/* Region codes as the Map byte records them. The table's own region column is one
 * lower for every slot kind: table 0..3 are pockets 0..3, 4 is the left hand, 5
 * the right hand, 6 the backpack and 7 the belt. */
typedef enum InvHitRegion {
    INV_REGION_GROUND = 0,
    INV_REGION_POCKET0 = 1,
    INV_REGION_POCKET1 = 2,
    INV_REGION_POCKET2 = 3,
    INV_REGION_POCKET3 = 4,
    INV_REGION_HAND_L = 5,
    INV_REGION_HAND_R = 6,
    INV_REGION_BACKPACK = 7,
    INV_REGION_BELT = 8,
    INV_REGION_COUNT = 9
} InvHitRegion;

/* The table's region column, 0..7. */
#define PUFO_INV_TABLE_REGION_MAX 7

/* One aInvHitRects record: four u16 bounds, then region, slot and column. */
typedef struct InvHitRect {
    i32 x0;
    i32 y0;
    i32 x1;
    i32 y1;
    i32 nRegion;
    i32 nSlot;
    i32 nCol;
} InvHitRect;

/* The Map block's drag state. */
typedef struct InvHitState {
    TactItem *pHitItem;  /* 0x0040 */
    i32 nDragOffX;       /* 0x4A28 */
    i32 nDragOffY;       /* 0x4A2C */
    i32 nHitRegion;      /* 0x4A30 */
    i32 nHitSub0;        /* 0x4A34 */
    i32 nHitSub1;        /* 0x4A38 */
} InvHitState;

typedef struct InvHitTestCtx {
    InvHitState   *pHit;
    TactUiState   *pUi;        /* nInvFilterType, nInvListCount, apInvList */
    Soldier       *pSelUnit;   /* Map.pSelUnit @ 0x15334 */
    TactItem      *pSentinel;  /* unk_138EA0 @ Map+0x254E8 */
} InvHitTestCtx;

/* Ui_HitTestTactItem @ 0x11ac0. Returns false when nothing usable is under the
 * pointer. The state is cleared on entry, exactly as the original stores NULL into
 * Map.pInvHitItem at 0x11AD4.
 *
 * Deviations: the original dereferences pSelUnit and pObj without a test, and
 * indexes the pocket/pack/belt arrays with whatever the table says; the port
 * refuses a NULL unit, reports a zero drag offset for a NULL object, and treats an
 * out-of-range slot as no match. */
bool Ui_HitTestTactItem(const InvHitTestCtx *pCtx, i32 nX, i32 nY);

#endif /* PUFO_INVDRAG_H */
