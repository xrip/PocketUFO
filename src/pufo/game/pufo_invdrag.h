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

/* --- the drop path, Inv_HandlePointer @ 0x12260 ---
 *
 * The pointer position is not an argument: the original reads it from the widget
 * bank at pBank+0x1044 and pBank+0x1048 (ARM 0x12290 / 0x122A0), adds the drag
 * offset the hit test stored and 10, and re-tests that point against the same
 * 25-record table. The tail re-tests it a second time against the ground strip.
 *
 *     nDragOffX + nPtrX + 10, nDragOffY + nPtrY + 10
 *
 * ## Order
 *
 * 1. Table hit -> place into the slot (pocket, hand, backpack, belt).
 * 2. Not placed and the point is in the ground strip -> insert into the cell.
 * 3. Still not placed and the point is in the unload rect -> split the ammo.
 * 4. Placed -> sample 17. Not placed -> put the item back where it came from.
 * 5. Always Unit_RecalcInventoryStat then Unit_UpdateAnimPose.
 *
 * ## Time units
 *
 * The charge is skipped when Map+1 (unk_1139B9) is set or the item came from the
 * region it is dropped into, so moving an item inside its own region is free. The
 * action latch at Map+2 is separate and does not waive inventory TU. Cost by target:
 * pocket 0/1 = 4, pocket 2/3 = 5,
 * hand = 6, backpack = 7, belt = 8, ground strip = 5, ammo split = 6. A short
 * balance raises the modal at 160,31 and the drop falls through to the tail.
 *
 * ## The three modals
 *
 *     160, 31, 1  "OK"  not enough time units
 *     160, 45, 1  "OK"  the weapon already holds a clip
 *     200, 43, 2  "OK"  a priority weapon already fills the other hand
 *
 * ## The item pool
 *
 * The original keeps its TactItems in `unk_136DF0` (Map+0x23438) with the count in
 * `unk_128AF4` (Map+0x1513C). Loading a clip deletes the dragged item and closes
 * the hole with the LAST entry (ARM 0x12DB4 reads index count-1 through the alias
 * constant 0x23434), then decrements. The decrement is unconditional: an item that
 * was not in the pool at all still costs one entry, which is the original's
 * behaviour and is reproduced here.
 */

/* Ground strip and the ammo-split rect, from the ARM compares at 0x133A4 and
 * 0x1353C (0x17/0xAD/0x108/0x139 and 0xB9/0xDD/0x63/0x8C). */
#define PUFO_INV_DROP_GROUND_X0 185
#define PUFO_INV_DROP_GROUND_X1 221
#define PUFO_INV_DROP_GROUND_Y0 99
#define PUFO_INV_DROP_GROUND_Y1 140

/* Time-unit costs. */
#define PUFO_INV_TU_POCKET_A 4
#define PUFO_INV_TU_POCKET_B 5
#define PUFO_INV_TU_HAND     6
#define PUFO_INV_TU_BACKPACK 7
#define PUFO_INV_TU_BELT     8
#define PUFO_INV_TU_GROUND   5
#define PUFO_INV_TU_SPLIT    6

/* The TactItem pool capacity is not fixed in the original; the port bounds it so a
 * test fixture cannot run off the end. */
#define PUFO_INV_ITEM_POOL_MAX 256

typedef struct InvDropHooks {
    /* Sound_Play @ 0x9e0c0. Sample 6 on a successful clip load, 17 on any
     * successful drop. */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    /* UiModal_Begin @ 0x26430 with the label "OK" @ 0xA62D8 and UiModal_EndThunk
     * (0x12254) as the callback; the app supplies both. */
    void (*pfnModal)(i32 nX, i32 nY, i32 nCount, const char *szLabel, void *pUser);
    /* pCtxBase.apObjects[nIndex], the ammo-type match against a weapon's
     * anAmmoTypeId @ 0xA8. */
    ObjectRec *(*pfnFindObject)(i32 nIndex, void *pUser);
    /* operator new(0x18) then TactItem_ctor @ 0x3e718. NULL when it fails. */
    TactItem *(*pfnAllocItem)(void *pUser);
    /* operator delete (0xa115c). */
    void (*pfnFreeItem)(TactItem *pItem, void *pUser);
    /* unk_136DF0 @ Map+0x23438 and its count unk_128AF4 @ Map+0x1513C. */
    TactItem **apItemPool;
    i32       *pnItemCount;
    /* The cell grid, for the ground drop's floor search. */
    MapCellLookup pfnLookupCell;
    void         *pLookupUser;
    /* Unit_RecalcInventoryStat @ 0x49ef0. NULL skips it. */
    const InvStatHooks *pInvStat;
    void *pUser;
} InvDropHooks;

typedef struct InvDropCtx {
    InvHitState       *pHit;    /* the state the hit test left behind */
    TactUiState       *pUi;     /* Map+2 action latch and the visible list */
    const u8          *pbInvNoTuCharge; /* Map+1 (unk_1139B9); NULL means clear */
    Soldier           *pSelUnit;
    TactItem          *pSentinel; /* unk_138EA0 @ Map+0x254E8 */
    i32                nPtrX;   /* pBank+0x1044 */
    i32                nPtrY;   /* pBank+0x1048 */
    const InvDropHooks *pHooks;
} InvDropCtx;

/* Inv_HandlePointer @ 0x12260. Returns the original's internal success flag (its
 * own return value is the constant 1). Deviations: the port refuses a NULL unit,
 * item or object instead of faulting, bounds every slot write to the array it
 * writes, and skips the field writes of a TactItem whose allocation failed.
 * Inv_ClearHitSlot runs before this in Input_OnPointer @ 0x146A4, so a failed drop
 * is a move of an item that has already left its slot. */
bool Inv_HandlePointer(const InvDropCtx *pCtx);

/* Inv_ClearHitSlot @ 0x11f40. Empties the slot the hit test selected, sized by the
 * item's own footprint: nFootW consecutive columns, nFootH rows, with a row stride
 * of 4 for the backpack and 3 for the belt. Region 0 is the ground list, which is
 * taken out of the cell's item list and the visible list is rebuilt.
 *
 * The slot bases prove the Map layout of the inventory block: the pocket cases
 * write dword index `sub0 + 67/70/73/76`, i.e. byte offsets 0x10C/0x118/0x124/0x130
 * = aPockets[0..3] with a 12-byte record; the backpack case writes index
 * `sub0 + 4*sub1 + 79` = 0x13C = apBackpack[0] with a 4-slot row; the belt case
 * writes `sub0 + 3*sub1 + 91` = 0x16C = apBelt[0] with a 3-slot row.
 *
 * Deviation: the original writes nFootW consecutive dwords with no bound, so a wide
 * item in pocket slot 1 also zeroes the four meta/spare bytes that follow the two
 * item pointers. The port clears item slots only. */
void Inv_ClearHitSlot(const InvHitTestCtx *pCtx);

/* -------------------------------------------------- the loadout from the craft */

/* The candidate order tables, read out of the executable. unk_A62F0 @ 0xA62F0 is the
 * left-hand weapon order (13 dwords) and unk_A6328 @ 0xA6328 the third pickup's order (6). */
#define PUFO_TACT_HAND_TABLE_COUNT 13
#define PUFO_TACT_PACK_TABLE_COUNT 6

typedef struct TactEquipCtx {
    MapCell      *pItemListCell;   /* the container at 0x157204; its pItemList @ 0x34 holds the loose items */
    Soldier     **apXcomUnits;     /* Map.apXcomUnits @ 0x15154 */
    u8           *pnXcomUnitCount; /* Map.nXcomUnitCount @ 0x1511F, a byte in the original */
    ObjectRec    **apObjects;      /* Map.apObjects @ Map+0x108 */
    InvHitState  *pHit;            /* drag offset and the dragged-item scratch */
    InvDropCtx   *pDrop;           /* pointer position, selected unit, item pool, drop hooks */
} TactEquipCtx;

/* Tact_EquipTeamFromCraft @ 0x3be24. Put every XCom soldier's declared loadout onto them by driving the
 * inventory drop path with a parked pointer, no mouse involved.
 *
 * The mechanism worth knowing: the original writes a hit rect origin into 0xAD714 and 0xAD718, and those two
 * addresses ARE pBank+0x1044 and pBank+0x1048, which are the pointer position Inv_HandlePointer reads. So
 * parking the pointer exactly on a slot's rect is what equips the item.
 *
 * Each soldier splits on Soldier.bUnk961 at +0x3C1, an already-equipped flag. Set, it re-runs the two 25-slot
 * inventory passes. Clear, it equips from the craft: a left-hand weapon, then ammo for that weapon, then a
 * third pickup that stores hit rect 10 and so lands in the backpack. Then it sets the flag.
 *
 * Deviations: the original dereferences a null soldier, a null object, and a null item pool entry without a
 * test, calls __rt_sdiv with a zero strength when auStat[8] is 0, and writes the pool without a bound. The
 * port refuses a null unit, treats a zero strength as too heavy, and bounds the pool. */
void Tact_EquipTeamFromCraft(TactEquipCtx *pCtx);

/* ------------------------------------------------- closing the inventory screen
 *
 * Evidence: Inv_CloseAndCommit @ 0x15a48 (1268 B). Three parts, the first gated on the commit
 * flag at Map+1 (unk_1139B9):
 *
 * 1. 0x15A60..0x15C4C: for every XCom unit that is NOT large, snapshot its loadout into the two
 *    per-slot arrays the equip pass reads back -- auUnk962[nSlot] (Soldier+0x3C2) gets the slot
 *    item's object sort key (`LDRH [pObj,#0xC8]`), and abUnk1012[nSlot] (Soldier+0x3F4) gets the
 *    low byte of TactItem.nIndex, which is the ammo type the weapon has loaded. An empty slot, a
 *    two-handed object, or an item whose nAmmo is 0 or 255 (the "never runs out" marker) stores 0
 *    for the ammo half. 24 slots, in the order the jumptable at 0x15AB0 walks them; see
 *    InvCommitSlot for which Soldier field each one is.
 * 2. 0x15C68..0x15CB0: clear the flag, then Tact_DeployTeamAndHwps, MapView_FocusSelected,
 *    Map_RefreshCellMeta, and Unit_UpdateVisibility once per unit.
 * 3. 0x15CBC..0x15F24: merge a loose clip into the hand that can take it. Whichever hand holds an
 *    object with bTwoHanded == 0 and bListPri == 1 is the weapon; the other hand's item is the clip.
 *    The weapon's three anAmmoTypeId entries are looked up in Map.apObjects and compared with the
 *    clip's object; on a match the weapon takes the type id into nIndex and the clip's nAmmo into
 *    its own nAmmo, the clip leaves the hand, and the clip's TactItem is dropped from the pool
 *    (hole closed with the LAST entry, then operator delete, then the count decremented -- the
 *    decrement runs even when the item was not in the pool). The left-hand case stops at the first
 *    match (0x15D20); the right-hand case re-tests all three ids and a later match overwrites the
 *    earlier one (0x15E80..0x15E88).
 *
 * Then Unit_RecalcInventoryStat on the selected unit and Map_nUiScreen = 0.
 *
 * Deviations: the original dereferences the selected unit, both hands' items, their objects, the
 * unit array entries and the object table without a test, and writes the pool with no bound; the
 * port refuses a null context, treats a null item or object as an empty slot, and bounds the pool
 * walk to the count. */
#define PUFO_INV_COMMIT_SLOTS 24

typedef struct InvCommitHooks {
    /* Unit_bIsLarge @ 0x49ecc. Large units are skipped by the snapshot. */
    bool (*pfnUnitIsLarge)(const Soldier *pUnit, void *pUser);
    /* Tact_DeployTeamAndHwps @ 0x39980, MapView_FocusSelected @ 0x16710 and
     * Map_RefreshCellMeta @ 0x2c7f4: all three need the Map block, so they are injected. */
    void (*pfnDeploy)(void *pUser);
    void (*pfnFocusSelected)(void *pUser);
    void (*pfnRefreshCells)(void *pUser);
    /* Unit_UpdateVisibility @ 0x48580, once per unit after the commit. */
    void (*pfnUpdateVisibility)(Soldier *pUnit, void *pUser);
    /* operator delete (0xa115c) on the merged clip. NULL falls back to free(). */
    void (*pfnFreeItem)(TactItem *pItem, void *pUser);
    /* Unit_RecalcInventoryStat @ 0x49ef0. NULL skips it. */
    const InvStatHooks *pInvStat;
    void *pUser;
} InvCommitHooks;

typedef struct InvCommitCtx {
    Soldier     **apXcomUnits;   /* Map.apXcomUnits @ Map+0x15154 */
    u8           *pnXcomUnitCount; /* Map.nXcomUnitCount @ Map+0x1511F */
    ObjectRec   **apObjects;     /* Map.apObjects @ Map+0x108 */
    Soldier      *pSelUnit;      /* Map_pSelUnit @ 0x128CEC */
    TactItem    **apItemPool;    /* Map_apTactItems @ 0x136DF0 */
    i32          *pnItemCount;   /* Map_nTactItemCount @ 0x128AF4 */
    InvHitState  *pHit;          /* pHitItem @ Map+0x40, the scratch the merge uses */
    u8           *pbCommitFlag;  /* unk_1139B9 @ Map+1 */
    i32          *pnUiScreen;    /* Map_nUiScreen @ Map+0x3A */
    const InvCommitHooks *pHooks;
} InvCommitCtx;

/* Inv_CloseAndCommit @ 0x15a48. Returns what Unit_RecalcInventoryStat returns. */
i32 Inv_CloseAndCommit(InvCommitCtx *pCtx);

#endif /* PUFO_INVDRAG_H */
